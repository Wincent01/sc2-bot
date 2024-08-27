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

    for (auto it = m_BuildOrder.begin(); it != m_BuildOrder.end(); ++it) {
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

        const auto unitsTimeLeft = m_Production->TimeLeftForUnitRequirements(ability_id);
        const auto economicTimeLeft = m_Production->TimeLeftForEconomicRequirements(*m_Proletariat, *m_Economy, planned_cost, ability_id);

        if (!unitsTimeLeft.has_value() || !economicTimeLeft.has_value()) {
            // TODO
            continue;
        }

        planned_cost += ability_cost;

        const auto time_left = std::max(unitsTimeLeft.value(), economicTimeLeft.value());
        
        m_NextBuildDispatch = std::min(m_NextBuildDispatch, time_left);

        if (StructureTypes.find(ability_id) != StructureTypes.end()) {
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

            const auto& builderWorkerIt = m_BuildingWorkers.find(plan.id);

            if (builderWorkerIt != m_BuildingWorkers.end()) {
                probe = obs->GetUnit(builderWorkerIt->second);

                if (probe != nullptr) {
                    m_Production->MoveProbeToPosition(probe, position, distance, time_left);
                }
            }

            if (probe == nullptr) {
                const auto moving_probe = m_Production->MoveProbeToPosition(*m_Proletariat, position, distance, time_left);

                if (!moving_probe.has_value()) {
                    continue;
                }

                probe = moving_probe.value(); // can't be null
            }

            m_BuildingWorkers.emplace(plan.id, probe->tag);

            m_Proletariat->RegisterWorker(probe);

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

            m_BuildOrder.erase(it);

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

            m_BuildOrder.erase(it);

            break;
        }
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
    if (Utilities::IsWorker(unit) && !m_Proletariat->IsWorkerAllocated(unit)) {
        m_Proletariat->ReturnToMining(unit);
    }
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
    for (auto it = m_BuildingWorkers.begin(); it != m_BuildingWorkers.end(); ++it) {
        if (it->second == unit->tag) {
            m_BuildingWorkers.erase(it);
            break;
        }
    }

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
