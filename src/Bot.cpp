// The MIT License (MIT)
//
// Copyright (c) 2021-2024 Alexander Kurbatov

#include "Bot.h"

#include <sc2api/sc2_common.h>
#include <sc2api/sc2_unit.h>
#include <sc2api/sc2_control_interfaces.h>
#include <sc2api/sc2_interfaces.h>
#include <sc2api/sc2_typeenums.h>
#include <sc2api/sc2_gametypes.h>
#include <sc2api/sc2_proto_interface.h>
#include <sc2api/sc2_agent.h>
#include <sc2api/sc2_client.h>
#include <sc2api/sc2_map_info.h>
#include <sc2api/sc2_score.h>

#include <sc2lib/sc2_search.h>

#include <iostream>
#include <algorithm>
#include <optional>
#include <unordered_set>
#include <limits>

#include "Utilities.h"
#include "Map.h"
#include "Production.h"
#include "Liberation.h"

// Should use Transposition?
//#define USE_TRANSPOSITION

using namespace scbot;

struct build_request {
    sc2::ABILITY_ID ability_id;
    const sc2::Unit* target;
    sc2::Point2D target_pos;
};

void Bot::OnGameStart()
{
    std::cout << "New game started!" << std::endl;

    m_Collective = std::make_shared<Collective>(this);
    m_Proletariat = std::make_shared<Proletariat>(m_Collective);
    m_Production = std::make_shared<Production>(m_Collective);
    m_Economy = std::make_shared<Economy>(m_Collective);
    m_Liberation = std::make_shared<Liberation>(m_Collective);

    m_NextBuildDispatch = 0;
}

void Bot::OnGameEnd()
{
    std::cout << "Game over!" << std::endl;

    // Save the replay.
    Control()->SaveReplay("/home/wincent/Documents/Projects/Starcraft/replays/live/replay.SC2Replay");
}

void Bot::OnBuildingConstructionComplete(const sc2::Unit* building_)
{
    std::cout << sc2::UnitTypeToName(building_->unit_type) <<
        "(" << building_->tag << ") constructed" << std::endl;
}

void Bot::OnStep()
{
    auto* obs = Observation();
    auto* actions = Actions();
    auto* query = Query();
    auto* debug = Debug();

    m_Collective->OnStep();
    m_Proletariat->OnStep();
    m_Production->OnStep();
    m_Economy->OnStep();
    m_Liberation->OnStep();

    const auto& nexus_units = m_Collective->GetAlliedUnitsOfType(sc2::UNIT_TYPEID::PROTOSS_NEXUS);
    const auto& probes = m_Collective->GetAlliedUnitsOfType(sc2::UNIT_TYPEID::PROTOSS_PROBE);

    float time_in_seconds = ElapsedTime();

    auto resources = m_Economy->GetResources();

    if (obs->GetGameLoop() % 50 == 0) {
        m_Proletariat->RedistributeWorkers();
    }

    const auto check_copy = m_CheckDelayedOrders;

    for (const auto& tag : check_copy) {
        const auto* unit = obs->GetUnit(tag);

        if (unit == nullptr) {
            continue;
        }

        CheckDelayedOrder(unit);
    }

    // Check if the next build dispatch time has been reached.
    if (time_in_seconds < m_NextBuildDispatch) {
        return;
    }

    for (const auto& it : m_DelayedOrders) {
        const auto& delayed_order = it.second;

        const auto* unit = obs->GetUnit(it.first);

        if (unit == nullptr) {
            continue;
        }
        
        CheckDelayedOrder(unit);
    }

    resources -= GetPlannedCosts();

    m_NextBuildDispatch = 5.0f;

    ResourcePair planned_cost = {0, 0};

    bool is_planning = false;

    // Start a timer to measure the time it takes to evaluate the state.
    auto start = std::chrono::high_resolution_clock::now();
    auto state = GetState();
    auto moves = GetBestMove(state, 0.5f);
    // Stop the timer
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "Evaluation time: " << elapsed.count() << "s, reached depth: " << moves.size() << std::endl;

    for (const auto& move : moves) {
        const auto& name_it = UnitTypeNames.find(move.unit);

        if (name_it != UnitTypeNames.end()) {
            std::cout << "Build " << name_it->second << std::endl;
        } else {
            std::cout << "Passing" << std::endl;
        }
    }

    std::unordered_set<sc2::Tag> claimed_workers;

    std::vector<ActionPlan> build_order;
    build_order.reserve(moves.size());
    for (const auto& move : moves) {
        if (move.nullmove) {
            continue;
        }

        const auto& ability_it = UnitToAbility.find(move.unit);

        if (ability_it == UnitToAbility.end()) {
            continue;
        }

        const auto& ability = ability_it->second;

        build_order.push_back({ability, 0});
    }

    for (auto it = build_order.begin(); it != build_order.end(); ++it) {
        auto& plan = *it;

        if (plan.id == 0) {
            plan.id = ++m_ActionIndex;
        }

        const auto& ability_id = plan.ability_id;

        const auto& ability_cost_it = AbilityCosts.find(ability_id);

        if (ability_cost_it == AbilityCosts.end()) {
            continue;
        }

        const auto& ability_cost = ability_cost_it->second;

        /*if (plan.id >= 2) {
            // To counteract the initial delay in worker production.
            if (time_in_seconds < 10.0f) {
                continue;
            }
        }*/

        const auto unitsTimeLeft = m_Production->TimeLeftForUnitRequirements(ability_id);
        const auto economicTimeLeft = m_Production->TimeLeftForEconomicRequirements(*m_Proletariat, *m_Economy, planned_cost, ability_id);

        const auto& supplyIt = UnitSupply.find(ability_id);

        if (supplyIt != UnitSupply.end()) {
            const auto& supply = supplyIt->second;

            if (obs->GetFoodUsed() + supply > obs->GetFoodCap()) {
                continue;
            }
        }

        if (!unitsTimeLeft.has_value() || !economicTimeLeft.has_value()) {
            // TODO
            continue;
        }

        planned_cost += ability_cost;

        const auto time_left = std::max(unitsTimeLeft.value(), economicTimeLeft.value());
        
        m_NextBuildDispatch = std::min(m_NextBuildDispatch, time_left);

        if (StructureTypes.find(ability_id) != StructureTypes.end()) {
            // To counteract the initial delay in worker production.
            if (time_in_seconds < 10.0f) {
                continue;
            }
            
            const auto ideal_position = m_Production->IdealPositionForBuilding(ability_id);

            if (!ideal_position.has_value()) {
                continue;
            }

            const auto& position = ideal_position.value();

            float distance = 0.0f;

            switch (ability_id) {
            case sc2::ABILITY_ID::BUILD_NEXUS:
                distance = 4.0f;
                break;
            case sc2::ABILITY_ID::BUILD_PYLON:
                distance = 1.0f;
                break;
            default:
                distance = 2.0f;
                break;
            }

            if (ability_id == sc2::ABILITY_ID::BUILD_ASSIMILATOR) {
                int i = 0;
            }

            const sc2::Unit* probe = nullptr;
            float closest_distance = std::numeric_limits<float>::max();

            for (const auto& tag : m_BuildingWorkers) {
                if (claimed_workers.find(tag) != claimed_workers.end()) {
                    continue;
                }
                
                const auto* worker = obs->GetUnit(tag);

                if (worker == nullptr) {
                    continue;
                }

                const auto d = sc2::DistanceSquared2D(worker->pos, position);

                if (d < closest_distance) {
                    probe = worker;
                    closest_distance = d;
                }
            }

            /*const auto& builderWorkerIt = m_BuildingWorkers.find(plan.id);

            if (builderWorkerIt != m_BuildingWorkers.end()) {
                probe = obs->GetUnit(builderWorkerIt->second);

                if (probe != nullptr) {
                    m_Production->MoveProbeToPosition(probe, position, distance, time_left);
                }
            }*/
            if (probe != nullptr) {
                m_Production->MoveProbeToPosition(probe, position, distance, time_left);
            }

            if (probe == nullptr) {
                const auto moving_probe = m_Production->MoveProbeToPosition(*m_Proletariat, position, distance, time_left);

                if (!moving_probe.has_value()) {
                    continue;
                }

                probe = moving_probe.value(); // can't be null
            }

            m_BuildingWorkers.emplace(probe->tag);

            m_Proletariat->RegisterWorker(probe);

            claimed_workers.emplace(probe->tag);

            // Check if the probe is within range of the position (+ 0.5).
            if (sc2::Distance2D(probe->pos, position) > distance + 1.0f) {
                continue;
            }

            if (time_left > 0.0f) {
                is_planning = true;
                continue;
            }

            m_Production->BuildBuilding(probe, ability_id, position);

            m_Proletariat->UnregisterWorker(probe);

            m_BuildingWorkers.erase(plan.id);

            build_order.erase(it);

            break;
        }

        if (UnitTrainTypes.find(ability_id) != UnitTrainTypes.end()) {
            const auto ideal_unit = m_Production->IdealUnitForProduction(ability_id);

            if (!ideal_unit.has_value()) {
                continue;
            }

            const auto* unit = ideal_unit.value();

            if (time_left > 0.0f) {
                is_planning = true;
                continue;
            }

            actions->UnitCommand(unit, ability_id);

            build_order.erase(it);

            break;
        }
    }

    // If there are building workers that are not clained, unclaim them.
    auto building_workers_copy = std::vector<sc2::Tag>(m_BuildingWorkers.begin(), m_BuildingWorkers.end());
    for (auto it = building_workers_copy.begin(); it != building_workers_copy.end(); it++) {
        if (claimed_workers.find(*it) != claimed_workers.end()) {
            continue;
        }

        auto* worker = obs->GetUnit(*it);

        if (worker == nullptr) {
            continue;
        }

        m_Proletariat->UnregisterWorker(worker);

        m_BuildingWorkers.erase(*it);
    }

    for (const auto& tag : m_OrdersExecuted) {
        const auto& delayed_order = m_DelayedOrders.find(tag);

        if (delayed_order != m_DelayedOrders.end()) {
            m_DelayedOrders.erase(delayed_order);
        }
    }
    
    m_OrdersExecuted.clear();

    m_NextBuildDispatch = std::max(m_NextBuildDispatch, 0.25f);

    std::cout << "Next build dispatch in " << m_NextBuildDispatch << " seconds" << std::endl;

    m_NextBuildDispatch += time_in_seconds;
}

void Bot::OnUnitCreated(const sc2::Unit* unit_)
{
    std::cout << sc2::UnitTypeToName(unit_->unit_type) <<
        "(" << unit_->tag << ") was created" << std::endl;
}

void Bot::OnUnitIdle(const sc2::Unit* unit)
{
    auto* actions = Actions();
    const auto* obs = Observation();

    const auto minerals = obs->GetMinerals();
    const auto vespene = obs->GetVespene();

    const auto& delayed_order = m_DelayedOrders.find(unit->tag);

    if (delayed_order != m_DelayedOrders.end()) {
        m_CheckDelayedOrders.emplace(unit->tag);

        CheckDelayedOrder(unit);

        return;
    }

    std::cout << sc2::UnitTypeToName(unit->unit_type) <<
         "(" << unit->tag << ") is idle" << std::endl;

    // If the unit is a probe, send it to mine minerals or gas.
    /*if (Utilities::IsWorker(unit) && !m_Proletariat->IsWorkerAllocated(unit)) {
        m_Proletariat->ReturnToMining(unit);
    }*/
}

void Bot::OnUnitDestroyed(const sc2::Unit* unit)
{
    std::cout << sc2::UnitTypeToName(unit->unit_type) <<
         "(" << unit->tag << ") was destroyed" << std::endl;

    // Remove from delayed orders.
    const auto& it = m_DelayedOrders.find(unit->tag);

    if (it != m_DelayedOrders.end()) {
        m_DelayedOrders.erase(it);
    }

    // Remove from check delayed orders.
    const auto& check_it = m_CheckDelayedOrders.find(unit->tag);

    if (check_it != m_CheckDelayedOrders.end()) {
        m_CheckDelayedOrders.erase(check_it);
    }

    // Remove from orders executed.
    const auto& executed_it = m_OrdersExecuted.find(unit->tag);

    if (executed_it != m_OrdersExecuted.end()) {
        m_OrdersExecuted.erase(executed_it);
    }

    // Remove from build order.
    /*for (auto it = m_BuildingWorkers.begin(); it != m_BuildingWorkers.end(); ++it) {
        if (it->second == unit->tag) {
            m_BuildingWorkers.erase(it);
            break;
        }
    }*/
    m_BuildingWorkers.erase(unit->tag);

    m_Proletariat->UnregisterWorker(unit);

}

void Bot::OnUpgradeCompleted(sc2::UpgradeID id_)
{
    std::cout << sc2::UpgradeIDToName(id_) << " completed" << std::endl;
}

void Bot::OnUnitDamaged(const sc2::Unit* unit_, float health_, float shields_)
{
    std::cout << sc2::UnitTypeToName(unit_->unit_type) <<
        "(" << unit_->tag << ") was damaged" << std::endl;
}

void Bot::OnNydusDetected()
{
    std::cout << "Nydus detected!" << std::endl;
}

void Bot::OnNuclearLaunchDetected()
{
    std::cout << "Nuclear launch detected!" << std::endl;
}

void Bot::OnUnitEnterVision(const sc2::Unit* unit_)
{
    std::cout << sc2::UnitTypeToName(unit_->unit_type) <<
        "(" << unit_->tag << ") entered vision" << std::endl;
}

void Bot::OnError(const std::vector<sc2::ClientError>& client_errors,
        const std::vector<std::string>& protocol_errors)
{
    for (const auto i : client_errors) {
        std::cerr << "Encountered client error: " <<
            static_cast<int>(i) << std::endl;
    }

    for (const auto& i : protocol_errors)
        std::cerr << "Encountered protocol error: " << i << std::endl;
}

ResourcePair Bot::GetPlannedCosts()
{
    ResourcePair planned_cost = {0, 0};

    for (const auto& order : m_DelayedOrders) {
        const auto& ability_cost = AbilityCosts.find(order.second.ability_id);

        if (ability_cost != AbilityCosts.end()) {
            planned_cost = planned_cost + ability_cost->second;
        }
    }

    return planned_cost;
}

float Bot::ElapsedTime()
{
    return Utilities::ToSecondsFromGameTime(Observation()->GetGameLoop());
}

void Bot::CheckDelayedOrder(const sc2::Unit *unit)
{
    auto* actions = Actions();
    const auto* obs = Observation();

    const auto minerals = obs->GetMinerals();
    const auto vespene = obs->GetVespene();

    // Check if the unit has a delayed order.
    const auto& it = m_DelayedOrders.find(unit->tag);

    if (it == m_DelayedOrders.end()) {
        m_CheckDelayedOrders.erase(unit->tag);
        return;
    }

    if (m_OrdersExecuted.find(unit->tag) != m_OrdersExecuted.end()) {
        m_CheckDelayedOrders.erase(unit->tag);
        return;
    }

    if (Utilities::IsInProgress(unit)) {
        m_CheckDelayedOrders.emplace(unit->tag);
        return;
    }

    const auto& delayed_order = it->second;

    // Check if the time has passed.
    if (delayed_order.time > ElapsedTime()) {
        m_CheckDelayedOrders.emplace(unit->tag);
        return;
    }

    const auto& ability_cost = AbilityCosts.find(delayed_order.ability_id);

    if (ability_cost != AbilityCosts.end()) {
        if (minerals < ability_cost->second.minerals || vespene < ability_cost->second.vespene) {
            m_CheckDelayedOrders.emplace(unit->tag);
            return;
        }
    }

    const auto& requirements = AbilityRequirements.find(delayed_order.ability_id);

    if (requirements != AbilityRequirements.end()) {
        for (const auto& requirement : requirements->second) {
            if (Utilities::AllInProgress(m_Collective->GetAlliedUnitsOfType(requirement))) {
                m_CheckDelayedOrders.emplace(unit->tag);
                return;
            } 
        }
    }

    if (delayed_order.target_unit_tag == 0) {
        std::cout << "Performing delayed order for " << sc2::UnitTypeToName(unit->unit_type) << " with ability " 
                << sc2::AbilityTypeToName(delayed_order.ability_id) << " at position " << delayed_order.position.x << ", " << delayed_order.position.y << std::endl;
        actions->UnitCommand(unit, delayed_order.ability_id, delayed_order.position);
    } else {
        std::cout << "Performing delayed order for " << sc2::UnitTypeToName(unit->unit_type) << " with ability " 
                << sc2::AbilityTypeToName(delayed_order.ability_id) << " at unit " << delayed_order.target_unit_tag << std::endl;
        actions->UnitCommand(unit, delayed_order.ability_id, delayed_order.target_unit_tag);
    }

    m_OrdersExecuted.emplace(unit->tag);

    m_CheckDelayedOrders.erase(unit->tag);

    return;
}

double Bot::EvaluateState(BoardState& state) {
    double score = 0.0;

    if (state.terminal) {
        return state.turn ? -INFINITY : INFINITY;
    }
    
    auto& friendly = state.friendly_units;
    auto& enemy = state.enemy_units;

    score += EvaluatePlayer(friendly, enemy);
    score -= EvaluatePlayer(enemy, friendly);

    return score;
}

void Bot::MakeMove(const Move &move, BoardState &state)
{
    auto& current = state.turn ? state.friendly_units : state.enemy_units;
    const auto& current_time = current.time;
    const auto& next_time = current_time + move.delta_time;
    current.steps.push_back(move);
    current.resources = current.resources + move.cost;
    // Loop through the steps, see if any of their complete times passed this time step
    for (auto it = current.steps.begin(); it != current.steps.end(); ++it) {
        auto& step = *it;
        if (current_time < step.complete_time && next_time >= step.complete_time) {
            if (step.nullmove) {
                continue;
            }
            current.units[step.unit] += 1;
            current.planned_units[step.unit] -= 1;
        }
    }
    if (!move.nullmove) {
        current.planned_units[move.unit] += 1;
    }
    current.time = next_time;
    state.turn = !state.turn;
}

void Bot::UnmakeMove(BoardState &state)
{
    state.turn = !state.turn;
    auto& current = state.turn ? state.friendly_units : state.enemy_units;
    const auto& move = current.steps.back();
    current.resources = current.resources - move.cost;
    current.steps.pop_back();
    const auto& current_time = current.time;
    const auto& next_time = current_time - move.delta_time;
    // Loop through the steps, see if any of their complete times passed this time step
    for (auto it = current.steps.begin(); it != current.steps.end(); ++it) {
        auto& step = *it;
        if (current_time >= step.complete_time && next_time < step.complete_time) {
            if (step.nullmove) {
                continue;
            }
            current.units[step.unit] -= 1;
            current.planned_units[step.unit] += 1;
        }
    }
    if (!move.nullmove) {
        current.planned_units[move.unit] -= 1;
        if (current.planned_units[move.unit] == 0) {
            current.planned_units.erase(move.unit);
        }
    }
    current.time = next_time;
}

std::vector<Bot::Move> Bot::GetPossibleMoves(BoardState &state, float timestep)
{
    std::vector<Move> moves;

    auto& current = state.turn ? state.friendly_units : state.enemy_units;

    const auto& friendly_units = current.units;
    const auto& planned_units = current.planned_units;
    const auto& current_time = current.time;

    // Count all workers and extractors
    uint32_t num_workers = 0;
    uint32_t num_extractors = 0;
    uint32_t num_bases = 0;
    int32_t num_supply = 0;
    for (const auto& [type, count] : friendly_units) {
        switch (type) {
            case sc2::UNIT_TYPEID::PROTOSS_PROBE:
            case sc2::UNIT_TYPEID::TERRAN_SCV:
            case sc2::UNIT_TYPEID::ZERG_DRONE:
                num_workers += count;
                break;
            case sc2::UNIT_TYPEID::PROTOSS_ASSIMILATOR:
            case sc2::UNIT_TYPEID::TERRAN_REFINERY:
            case sc2::UNIT_TYPEID::ZERG_EXTRACTOR:
                num_extractors += count;
                break;
            case sc2::UNIT_TYPEID::PROTOSS_NEXUS:
            case sc2::UNIT_TYPEID::TERRAN_COMMANDCENTER:
            case sc2::UNIT_TYPEID::ZERG_HATCHERY:
            case sc2::UNIT_TYPEID::ZERG_HIVE:
            case sc2::UNIT_TYPEID::ZERG_LAIR:
                num_bases += count;
                num_supply += count * 15;
                break;
            case sc2::UNIT_TYPEID::PROTOSS_PYLON:
            case sc2::UNIT_TYPEID::TERRAN_SUPPLYDEPOT:
            case sc2::UNIT_TYPEID::ZERG_OVERLORD:
                num_supply += count * 8;
                break;
        }

        const auto& ability_it = UnitToAbility.find(type);

        if (ability_it != UnitToAbility.end()) {
            const auto& ability = ability_it->second;

            const auto& supply_it = UnitSupply.find(ability);

            if (supply_it != UnitSupply.end()) {
                const auto& supply = supply_it->second;

                num_supply -= count * supply;
            }
        }
    }

    int32_t vespene_workers = std::min(num_extractors * 3, num_workers);
    int32_t mineral_workers = std::min(num_bases * 12, num_workers - vespene_workers);
    int32_t excess_workers = num_workers - vespene_workers - mineral_workers;

    int32_t vespene_income = std::ceilf(vespene_workers * 0.94f * timestep);
    int32_t mineral_income = std::ceilf(mineral_workers * 1.256f * timestep);

    ResourcePair resources = {mineral_income, vespene_income};

    for (const auto& planned : planned_units) {
        const auto& ability_it = UnitToAbility.find(planned.first);

        if (ability_it != UnitToAbility.end()) {
            const auto& ability = ability_it->second;

            const auto& supply_it = UnitSupply.find(ability);

            if (supply_it != UnitSupply.end()) {
                const auto& supply = supply_it->second;

                num_supply -= planned.second * supply;
            }
        }

        switch (planned.first) {
            case sc2::UNIT_TYPEID::PROTOSS_PROBE:
            case sc2::UNIT_TYPEID::TERRAN_SCV:
            case sc2::UNIT_TYPEID::ZERG_DRONE:
                num_workers += planned.second;
                break;
            case sc2::UNIT_TYPEID::PROTOSS_ASSIMILATOR:
            case sc2::UNIT_TYPEID::TERRAN_REFINERY:
            case sc2::UNIT_TYPEID::ZERG_EXTRACTOR:
                num_extractors += planned.second;
                break;
            case sc2::UNIT_TYPEID::PROTOSS_NEXUS:
            case sc2::UNIT_TYPEID::TERRAN_COMMANDCENTER:
            case sc2::UNIT_TYPEID::ZERG_HATCHERY:
            case sc2::UNIT_TYPEID::ZERG_HIVE:
            case sc2::UNIT_TYPEID::ZERG_LAIR:
                num_bases += planned.second;
                break;
        }
    }

    // Generate all possible moves
    for (const auto& [ability, requirements] : AbilityRequirements) {
        if (ability == sc2::ABILITY_ID::TRAIN_PROBE) {
            if (num_workers >= (num_bases * 12 + num_extractors * 3)) {
                continue;
            }
        }

        if (ability == sc2::ABILITY_ID::BUILD_ASSIMILATOR) {
            if (num_extractors >= num_bases * 2) {
                continue;
            }
        }

        // Check if we have enough resources
        const auto& cost = AbilityCosts[ability];
        if (current.resources.minerals < cost.minerals || current.resources.vespene < cost.vespene) {
            continue;
        }

        // Check if we have the required units
        bool valid = true;
        for (const auto& type : requirements) {
            const auto& it = friendly_units.find(type);
            if (it == friendly_units.end() || it->second == 0) {
                valid = false;
                break;
            }
        }
        if (!valid) {
            continue;
        }

        const auto& supply_it = UnitSupply.find(ability);
        if (supply_it != UnitSupply.end()) {
            const auto& supply = supply_it->second;

            int32_t available_supply = num_supply - supply;

            if (available_supply < 0) {
                continue;
            }
        }

        const auto& ability_unit_it = AbilityToUnit.find(ability);

        if (ability_unit_it == AbilityToUnit.end()) {
            continue;
        }

        const auto& ability_unit = ability_unit_it->second;

        // Get the production time for the unit
        float production_time = 0.0f;
        const auto& unit_data = Observation()->GetUnitTypeData().at(sc2::UnitTypeID(ability_unit));
        production_time = Utilities::ToSecondsFromGameTime(unit_data.build_time);

        moves.push_back({false, ability_unit, resources - cost, current_time + production_time, 5.0f});
    }

    // Can always pass
    moves.push_back({true, sc2::UNIT_TYPEID::INVALID, resources, 0.0f, 5.0f});

    return moves;
}

// Function to check if time limit has been exceeded
bool Bot::IsTimeUp(const std::chrono::time_point<std::chrono::high_resolution_clock>& start, double timeLimit) {
    auto now = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(now - start).count();
    return elapsed >= timeLimit;
}

uint64_t Bot::ComputeHash(const BoardState& state) {
    uint64_t hash = 0;
    for (const auto& [type, count] : state.friendly_units.units) {
        hash ^= static_cast<int32_t>(type) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= count + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    }
    for (const auto& [type, count] : state.enemy_units.units) {
        hash ^= static_cast<int32_t>(type) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= count + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    }
    return hash;
}

// Transposition table lookup
bool Bot::LookupTranspositionTable(const BoardState& state, int32_t depth, double alpha, double beta, MoveSequence& outResult) {
    uint64_t hash = ComputeHash(state); // A function that computes a unique hash for each position
    if (m_TranspositionTable.find(hash) != m_TranspositionTable.end()) {
        const TranspositionEntry& entry = m_TranspositionTable[hash];
        if (entry.depth >= depth) {  // Check if this depth is as deep or deeper than the current search
            if (entry.score <= alpha) {
                outResult.score = entry.score;
                outResult.moves = entry.bestMoveSequence;
                return true; // Alpha cutoff
            }
            if (entry.score >= beta) {
                outResult.score = entry.score;
                outResult.moves = entry.bestMoveSequence;
                return true; // Beta cutoff
            }
        }
    }
    return false;
}

// Save to the transposition table
void Bot::SaveToTranspositionTable(const BoardState& state, double score, int32_t depth, double alpha, double beta, const MoveSequence& bestSequence) {
    uint64_t hash = ComputeHash(state);
    m_TranspositionTable[hash] = { score, depth, alpha, beta, bestSequence.moves };
}

Bot::MoveSequence Bot::SearchBuild(int32_t depth, double alpha, double beta, BoardState& state,
                              const std::chrono::time_point<std::chrono::high_resolution_clock>& start, double timeLimit) {
    // Check if time is up
    if (IsTimeUp(start, timeLimit)) {
        return MoveSequence(EvaluateState(state), {});
    }

#ifdef USE_TRANSPOSITION
    // Lookup transposition table to reuse results
    MoveSequence ttResult;
    if (LookupTranspositionTable(state, depth, alpha, beta, ttResult)) {
        return ttResult;
    }
#endif

    // Base case: if we've reached max depth or the game is over
    if (depth <= 0 || state.terminal) {
        return MoveSequence(EvaluateState(state), {});
    }

    // Get all possible moves for the current state
    std::vector<Move> moves = GetPossibleMoves(state, 5.0f);
    SortMoves(moves);  // Sort moves using heuristic

    MoveSequence bestSequence(state.turn ? -INFINITY : INFINITY);  // Store best score and moves

    for (const Move& move : moves) {
        auto oldState = state;

        MakeMove(move, state);

        // Recursively search with reduced depth
        MoveSequence result = SearchBuild(depth - 1, alpha, beta, state, start, timeLimit);

        if (!move.nullmove) {
            int i = 0;
        }

        state = oldState;

        /*UnmakeMove(state);

        if (!CompareStates(oldState, state)) {
            std::cout << "State mismatch!" << std::endl;
        }*/

        result.moves.insert(result.moves.begin(), move);

        // Update best score and alpha-beta bounds
        if (state.turn) {  // Maximizing player
            if (result.score > bestSequence.score) {
                bestSequence = result;
            }
            alpha = std::max(alpha, bestSequence.score);
        } else {  // Minimizing player
            if (result.score < bestSequence.score) {
                bestSequence = result;
            }
            beta = std::min(beta, bestSequence.score);
        }

        // Prune the branch if alpha-beta cutoff is reached
        if (beta <= alpha) {
            break;
        }

        if (IsTimeUp(start, timeLimit)) {
            break;
        }
    }

#ifdef USE_TRANSPOSITION
    // Save the result to the transposition table before returning
    SaveToTranspositionTable(state, bestSequence.score, depth, alpha, beta, bestSequence);
#endif

    return bestSequence;
}
// Modified GetBestMove function with time control and iterative deepening
std::vector<Bot::Move> Bot::GetBestMove(BoardState& state, double maxTime) {
    std::chrono::time_point<std::chrono::high_resolution_clock> start = std::chrono::high_resolution_clock::now();

    // Start with a shallow depth and increase iteratively
    int32_t depth = 1;
    MoveSequence bestSequence(state.turn ? -INFINITY : INFINITY);  // Max for player, Min for opponent

    // Iterative deepening loop
    while (!IsTimeUp(start, maxTime)) {
        MoveSequence currentBestSequence(state.turn ? -INFINITY : INFINITY);  // Store current best move sequence

        // Get all possible moves
        std::vector<Move> moves = GetPossibleMoves(state, 5.0f);
        SortMoves(moves);  // Sort moves using a heuristic to improve pruning efficiency

        // Search with current depth
        for (const Move& move : moves) {
            MakeMove(move, state);

            // Search for the best move sequence with time control
            MoveSequence result = SearchBuild(depth, -INFINITY, INFINITY, state, start, maxTime);

            UnmakeMove(state);

            // Add the current move to the result sequence
            result.moves.insert(result.moves.begin(), move);

            // Update best sequence based on the current player's turn
            if (state.turn) {  // Maximizing player
                if (result.score > currentBestSequence.score) {
                    currentBestSequence = result;
                }
            } else {  // Minimizing player
                if (result.score < currentBestSequence.score) {
                    currentBestSequence = result;
                }
            }
        }

        // If a new best sequence was found, update the bestSequence
        if (currentBestSequence.score != bestSequence.score) {
            bestSequence = currentBestSequence;
        }

        // Increase depth for iterative deepening
        depth++;
    }

    // Return the best sequence of moves found within the time limit
    return bestSequence.moves;
}

double Bot::MoveHeuristic(const Move &move)
{
    if (move.nullmove) {
        return 0.0;
    }

    double heuristic = 0.0;

    const auto& ability_it = UnitToAbility.find(move.unit);

    if (ability_it != UnitToAbility.end()) {
        const auto& ability = ability_it->second;

        const auto& supply_it = UnitSupply.find(ability);

        if (supply_it != UnitSupply.end()) {
            const auto& supply = supply_it->second;

            heuristic += supply * 100;
        }

        const auto& cost_it = AbilityCosts.find(ability);

        if (cost_it != AbilityCosts.end()) {
            const auto& cost = cost_it->second;

            heuristic += cost.minerals + cost.vespene * 1.5;
        }
    }

    return heuristic;
}

void Bot::SortMoves(std::vector<Move>& moves)
{
    std::sort(moves.begin(), moves.end(), [this](const Move& a, const Move& b) {
        return MoveHeuristic(a) > MoveHeuristic(b);
    });
}

double Bot::EvaluatePlayer(PlayerState &a, PlayerState &b)
{
    double score = 0.0;

    score -= a.resources.minerals * 0.5;
    score -= a.resources.vespene * 0.75;

    int32_t base_count = 0;
    int32_t worker_count = 0;
    int32_t assimilator_count = 0;
    int32_t supply_count = 0;

    for (const auto& [type, count] : a.planned_units) {
        switch (type) {
            case sc2::UNIT_TYPEID::PROTOSS_NEXUS:
            case sc2::UNIT_TYPEID::TERRAN_COMMANDCENTER:
            case sc2::UNIT_TYPEID::ZERG_HATCHERY:
            case sc2::UNIT_TYPEID::ZERG_HIVE:
            case sc2::UNIT_TYPEID::ZERG_LAIR:
                base_count += count;
                supply_count += count * 15;
                break;
            case sc2::UNIT_TYPEID::PROTOSS_PROBE:
            case sc2::UNIT_TYPEID::TERRAN_SCV:
            case sc2::UNIT_TYPEID::ZERG_DRONE:
                worker_count += count;
                break;
            case sc2::UNIT_TYPEID::PROTOSS_ASSIMILATOR:
            case sc2::UNIT_TYPEID::TERRAN_REFINERY:
            case sc2::UNIT_TYPEID::ZERG_EXTRACTOR:
                assimilator_count += count;
                break;
            case sc2::UNIT_TYPEID::PROTOSS_PYLON:
            case sc2::UNIT_TYPEID::TERRAN_SUPPLYDEPOT:
            case sc2::UNIT_TYPEID::ZERG_OVERLORD:
                supply_count += count * 8;
                break;
        }
    }

    for (const auto& [type, count] : a.units) {
        switch (type) {
            case sc2::UNIT_TYPEID::PROTOSS_NEXUS:
            case sc2::UNIT_TYPEID::TERRAN_COMMANDCENTER:
            case sc2::UNIT_TYPEID::ZERG_HATCHERY:
            case sc2::UNIT_TYPEID::ZERG_HIVE:
            case sc2::UNIT_TYPEID::ZERG_LAIR:
                base_count += count;
                supply_count += count * 15;
                break;
            case sc2::UNIT_TYPEID::PROTOSS_PROBE:
            case sc2::UNIT_TYPEID::TERRAN_SCV:
            case sc2::UNIT_TYPEID::ZERG_DRONE:
                worker_count += count;
                break;
            case sc2::UNIT_TYPEID::PROTOSS_ASSIMILATOR:
            case sc2::UNIT_TYPEID::TERRAN_REFINERY:
            case sc2::UNIT_TYPEID::ZERG_EXTRACTOR:
                assimilator_count += count;
                break;
            case sc2::UNIT_TYPEID::PROTOSS_PYLON:
            case sc2::UNIT_TYPEID::TERRAN_SUPPLYDEPOT:
            case sc2::UNIT_TYPEID::ZERG_OVERLORD:
                supply_count += count * 8;
                break;
        }
        
        const auto& ability_it = UnitToAbility.find(type);

        if (ability_it != UnitToAbility.end()) {
            const auto& ability = ability_it->second;

            const auto& supply_it = UnitSupply.find(ability);

            if (supply_it != UnitSupply.end()) {
                const auto& supply = supply_it->second;

                score += count * supply * 250; //(ability == sc2::ABILITY_ID::TRAIN_PROBE ? 50 : 400);

                supply_count -= count * supply;
            }

            const auto& ability_cost_it = AbilityCosts.find(ability);

            if (ability_cost_it != AbilityCosts.end()) {
                const auto& cost = ability_cost_it->second;

                score += count * (cost.minerals + cost.vespene * 1.5);

                /*if (ability == sc2::ABILITY_ID::BUILD_PYLON || ability == sc2::ABILITY_ID::TRAIN_OVERLORD || ability == sc2::ABILITY_ID::BUILD_SUPPLYDEPOT) {
                    score += count * 100;
                }*/
            }
        }

        const auto& counters_it = UnitCounters.find(type);

        if (counters_it != UnitCounters.end()) {
            const auto& counters = counters_it->second;

            for (const auto& counter : counters) {
                const auto& counter_count = b.units.find(counter);

                if (counter_count != b.units.end()) {
                    score += counter_count->second * 100;
                }
            }
        }
    }

    score += base_count * 100;

    // If we have more workers than the bases can support, penalize
    int32_t excess_workers = worker_count - (base_count * 12 + assimilator_count * 3);
    if (excess_workers > 0) {
        score -= excess_workers * 75;
    }

    // If we have more extractors than bases, penalize
    int32_t excess_extractors = assimilator_count - base_count * 2;
    if (excess_extractors > 0) {
        score -= excess_extractors * 500;
    }
    
    // If we are close to supply cap, penalize, exponentially
    int32_t supply_left = supply_count;
    if (supply_left < 1) {
        score -= 1000;
    }

    score -= supply_left * 50;

    return score;
}

bool Bot::CompareStates(BoardState& a, BoardState& b)
{
    return BoardState::equals(a, b);
}

Bot::BoardState Bot::GetState()
{
    BoardState state;
    const auto* obs = Observation();

    state.friendly_units.resources.minerals = obs->GetMinerals();
    state.friendly_units.resources.vespene = obs->GetVespene();

    for (const auto& unit : obs->GetUnits()) {
        if (unit->alliance == sc2::Unit::Alliance::Self) {
            state.friendly_units.units[unit->unit_type] += 1;
        } else if (unit->alliance == sc2::Unit::Alliance::Enemy) {
            state.enemy_units.units[unit->unit_type] += 1;
        }
    }

    // TODO: Update based on scounted info
    state.enemy_units.units[sc2::UNIT_TYPEID::PROTOSS_NEXUS] = 1;
    state.enemy_units.units[sc2::UNIT_TYPEID::PROTOSS_PROBE] = 12;
    state.enemy_units.resources.minerals = 50;
    state.enemy_units.resources.vespene = 0;

    state.terminal = false;
    state.turn = true;

    state.friendly_units.time = 0;
    state.enemy_units.time = 0;

    return state;
}
