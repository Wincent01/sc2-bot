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

using namespace scbot;

#define PROBE_RANGE 10.0f
#define PROBE_RANGE_SQUARED PROBE_RANGE * PROBE_RANGE

struct build_request {
    sc2::ABILITY_ID ability_id;
    const sc2::Unit* target;
    sc2::Point2D target_pos;
};

void Bot::OnGameStart()
{
    std::cout << "New game started!" << std::endl;

    FindRamps();
    FindExpansions();

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

    UpdateStepData();

    const auto& nexus_units = GetUnits(sc2::UNIT_TYPEID::PROTOSS_NEXUS);
    const auto& probes = GetUnits(sc2::UNIT_TYPEID::PROTOSS_PROBE);

    float time_in_seconds = obs->GetGameLoop() / 22.4f;

    m_Resources = { 
        static_cast<int32_t>(obs->GetMinerals()),
        static_cast<int32_t>(obs->GetVespene())
    };

    if (obs->GetGameLoop() % 50 == 0) {
        RedistributeWorkers(nexus_units);
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

    if (nexus_units.size() == 0) {
        AttemptBuild(sc2::ABILITY_ID::BUILD_NEXUS);
        return;
    }

    m_Resources = m_Resources - GetPlannedCosts();

    m_NextBuildDispatch = 5.0f;

    bool is_planning = false;

    for (auto it = m_BuildOrder.begin(); it != m_BuildOrder.end(); ++it) {
        const auto& build = *it;
        const auto result = AttemptBuild(build.ability_id);
        
        if (result.IsSuccess()) {
            m_Resources = m_Resources - result.cost;

            if (result.delayed_order_tag != 0) {
                if (result.time != 0.0f && is_planning) {
                    continue;
                }

                m_DelayedOrders[result.delayed_order_tag] = result.delayed_order;
            }

            std::cout << "Building with " << sc2::AbilityTypeToName(build.ability_id) << " (" << result.success << ") in " << result.time << " seconds at a cost of " << result.cost.minerals << " minerals and " << result.cost.vespene << " vespene" << std::endl;
            std::cout << "Resources left: " << m_Resources.minerals << " minerals and " << m_Resources.vespene << " vespene" << std::endl;
            
            m_NextBuildDispatch = std::min(m_NextBuildDispatch, result.time);

            // Remove from build order.
            m_BuildOrder.erase(it);
            
            break;
        }

        if (result.IsPlanning()) {
            m_Resources = m_Resources - result.cost;
            std::cout << "Planning to build with " << sc2::AbilityTypeToName(build.ability_id) << " (" << result.success << ") in " << result.time << " seconds at a cost of " << result.cost.minerals << " minerals and " << result.cost.vespene << " vespene" << std::endl;
            std::cout << "Resources left: " << m_Resources.minerals << " minerals and " << m_Resources.vespene << " vespene" << std::endl;

            m_NextBuildDispatch = std::min(m_NextBuildDispatch, result.time);

            is_planning = true;

            return;
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
    if (Utilities::IsWorker(unit)) {
        ReturnToMining(unit);
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

void Bot::FindExpansions()
{
    m_Expansions = sc2::search::CalculateExpansionLocations(Observation(), Query());
}

sc2::Point2D Bot::GetIdealPosition(sc2::ABILITY_ID ability_id)
{
    const auto* obs = Observation();

    const auto& requirementsIter = UnitRequirements.find(ability_id);

    if (requirementsIter == UnitRequirements.end()) {
        return sc2::Point2D(0.0f, 0.0f);
    }

    const auto& requirements = requirementsIter->second;

    if (ability_id == sc2::ABILITY_ID::BUILD_NEXUS) {
        // Build the Nexus at the closest expansion, or the closest one to a probe if we don't have a base.
        if (m_Expansions.size() == 0) {
            return sc2::Point2D(0.0f, 0.0f);
        }

        const auto& nexus_units = GetUnits(sc2::UNIT_TYPEID::PROTOSS_NEXUS);

        if (nexus_units.size() == 0) {
            const auto& probes = GetUnits(sc2::UNIT_TYPEID::PROTOSS_PROBE);

            if (probes.size() == 0) {
                return sc2::Point2D(0.0f, 0.0f);
            }

            const auto& probe = probes[0];

            const auto closest_expansion = Utilities::ClosestTo(m_Expansions, probe->pos);

            return closest_expansion;
        }

        const auto& closest_expansion = Utilities::ClosestAverageTo(m_Expansions, nexus_units);

        return closest_expansion;
    }

    const auto& pylons = GetUnits(sc2::UNIT_TYPEID::PROTOSS_PYLON);

    if (ability_id == sc2::ABILITY_ID::BUILD_PYLON) {
        // Build the Pylon at the closest ramp.
        const auto& nexus_units = GetUnits(sc2::UNIT_TYPEID::PROTOSS_NEXUS);

        if (nexus_units.empty()) {
            return sc2::Point2D(0.0f, 0.0f);
        }

        if (pylons.empty()) {
            const auto& closest_nexus = nexus_units[0];

            const auto& ramp = GetClosestRamp(closest_nexus->pos);

            return GetClosestPlace(ramp, closest_nexus->pos, ability_id, 2.0f, 4.0f);
        }

        const auto unpowered_structures = Utilities::FilterUnits(m_AllUnits, [this](const sc2::Unit* unit) {
            return Utilities::RequiresPower(unit) && !Utilities::IsPowered(unit);
        });

        if (unpowered_structures.size() != 0) {
            return GetBestCenter(unpowered_structures, ability_id, 3.0f, 5.0f, 5.0f);
        }

        const auto mining_points = Utilities::GetResourcePoints(m_AllUnits, true, false, true);

        const sc2::Unit* fewest_pylons = Utilities::SelectUnitMin(nexus_units, [this, &pylons](const sc2::Unit* nexus) {
            return Utilities::CountWithinRange(pylons, nexus->pos, 15.0f);
        });

        // Place while avoiding both pylons and mining points.
        sc2::Units avoid = Utilities::Union(pylons, mining_points);

        return GetClosestPlaceWhileAvoiding(fewest_pylons->pos, fewest_pylons->pos, avoid, ability_id, 5.0f, 10.0f, 6.0f, true);
    }

    if (ability_id == sc2::ABILITY_ID::BUILD_ASSIMILATOR) {
        // Build the Assimilator at the closest vespene geyser.
        const auto nexus_units = GetUnits(sc2::UNIT_TYPEID::PROTOSS_NEXUS);

        if (nexus_units.empty()) {
            return sc2::Point2D(0.0f, 0.0f);
        }

        const auto& assimilators = GetUnits(sc2::UNIT_TYPEID::PROTOSS_ASSIMILATOR);
        
        // Select a Nexus that has less than 2 assimilators within 15 units of it.
        const sc2::Unit* selected_nexus = Utilities::SelectUnitMin(nexus_units, [this, &assimilators](const sc2::Unit* nexus) {
            return Utilities::CountWithinRange(assimilators, nexus->pos, 15.0f);
        });

        const auto vespene_geysers = Utilities::GetResourcePoints(m_NeutralUnits, false, true, false);

        if (vespene_geysers.empty()) {
            return sc2::Point2D(0.0f, 0.0f);
        }

        const auto* closest_vespene_geyser = Utilities::ClosestTo(vespene_geysers, selected_nexus->pos);

        return closest_vespene_geyser->pos;
    }

    bool all_in_progress = Utilities::AllInProgress(pylons);

    if (ability_id == sc2::ABILITY_ID::BUILD_GATEWAY) {
        // Build the Gateway at the closest ramp.
        const auto& nexus_units = GetUnits(sc2::UNIT_TYPEID::PROTOSS_NEXUS);

        if (nexus_units.empty()) {
            return sc2::Point2D(0.0f, 0.0f);
        }

        const auto& closest_nexus = nexus_units[0];

        const auto& ramp = GetClosestRamp(closest_nexus->pos);

        // Check if there already is a Gateway at the ramp.
        const auto& gateways = GetUnits(sc2::UNIT_TYPEID::PROTOSS_GATEWAY);

        if (AnyWithingRange(gateways, ramp, 8.0f)) {
            return GetClosestPlace(closest_nexus->pos, pylons, ability_id, 0.0f, 5.0f);
        }

        if (all_in_progress) {
            return GetClosestPlace(ramp, ramp, sc2::ABILITY_ID::BUILD_BARRACKS, 0.0f, 8.0f);
        }

        return GetClosestPlace(ramp, ramp, pylons, ability_id, 0.0f, 8.0f);
    }

    if (ability_id == sc2::ABILITY_ID::BUILD_CYBERNETICSCORE) {
        // Build the Cybernetics Core at the closest ramp.
        const auto& nexus_units = GetUnits(sc2::UNIT_TYPEID::PROTOSS_NEXUS);

        if (nexus_units.size() == 0) {
            return sc2::Point2D(0.0f, 0.0f);
        }

        const auto& closest_nexus = nexus_units[0];

        const auto& ramp = GetClosestRamp(closest_nexus->pos);

        if (all_in_progress) {
            return GetClosestPlace(ramp, ramp, sc2::ABILITY_ID::BUILD_BARRACKS, 0.0f, 8.0f);
        }

        return GetClosestPlace(ramp, ramp, pylons, ability_id, 0.0f, 8.0f);
    }

    return sc2::Point2D(0.0f, 0.0f);
}

TrainResult Bot::GetIdealUnitProduction(sc2::ABILITY_ID ability_id)
{
    const auto& training_building_it = AssociatedBuilding.find(ability_id);

    if (training_building_it == AssociatedBuilding.end()) {
        throw std::runtime_error(std::string("No training building found for ability ") + sc2::AbilityTypeToName(ability_id));
    }

    const auto& training_building = training_building_it->second;

    // Find possible training buildings, select one that is complete and has the least amount of queued orders.
    const auto training_buildings = GetUnits(training_building);

    if (training_buildings.empty()) {
        return TrainResult(false);
    }

    const auto training_building_with_fewest_orders = Utilities::SelectUnitMin(training_buildings, [this](const sc2::Unit* unit) {
        return unit->orders.size();
    });

    return TrainResult(true, ability_id, training_building_with_fewest_orders);
}

AbilityCost Bot::GetPlannedCosts()
{
    AbilityCost planned_cost = {0, 0};

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
    auto* obs = Observation();

    return obs->GetGameLoop() / 22.4f;
}

float Bot::FromGameTimeSource(float time_)
{
    return time_ + ElapsedTime();
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

    if (!unit->build_progress == 1.0f) {
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

    const auto& requirements = UnitRequirements.find(delayed_order.ability_id);

    if (requirements != UnitRequirements.end()) {
        for (const auto& requirement : requirements->second) {
            if (Utilities::AllInProgress(GetUnits(requirement))) {
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

void Bot::UpdateStepData()
{
    m_Units.clear();

    m_NeutralUnits = Observation()->GetUnits(sc2::Unit::Alliance::Neutral);

    // Get all friendly units.
    m_AllUnits = Observation()->GetUnits(sc2::Unit::Alliance::Self);

    for (const auto& unit : m_AllUnits) {
        const auto& type = unit->unit_type;

        const auto& iter = m_Units.find(type);

        if (iter == m_Units.end()) {
            m_Units.emplace(type, sc2::Units{unit});
        } else {
            iter->second.push_back(unit);
        }
    }
}

const sc2::Units& Bot::GetUnits(sc2::UNIT_TYPEID unit_type_) const
{
    const auto& iter = m_Units.find(unit_type_);

    if (iter == m_Units.end()) {
        static sc2::Units empty;
        return empty;
    }

    return iter->second;
}

sc2::Point2D Bot::GetClosestPlace(const sc2::Point2D &center, sc2::ABILITY_ID ability_id, float min_radius, float max_radius, float step_size)
{
    return GetClosestPlace(center, center, ability_id, min_radius, max_radius, step_size);
}

sc2::Point2D Bot::GetClosestPlace(const sc2::Point2D &center, const sc2::Point2D &pivot, sc2::ABILITY_ID ability_id, float min_radius, float max_radius, float step_size)
{
    auto* query = Query();

    sc2::Point2D current_grid;

    sc2::Point2D previous_grid;

    int valid_queries = 0;

    std::vector<sc2::QueryInterface::PlacementQuery> queries;

    for (float r = min_radius; r < max_radius; r += 1.0f)
    {
        float loc = 0.0f;

        while (loc < 360.0f) {
            sc2::Point2D point = sc2::Point2D((r * std::cos((loc * 3.1415927f) / 180.0f)) + center.x,
                                    (r * std::sin((loc * 3.1415927f) / 180.0f)) + center.y);

            current_grid = sc2::Point2D(std::floor(point.x), std::floor(point.y));

            if (previous_grid != current_grid) {
                sc2::QueryInterface::PlacementQuery query(ability_id, point);
                queries.push_back(query);
                ++valid_queries;
            }

            previous_grid = current_grid;
            loc += step_size;
        }
    }

    auto result = query->Placement(queries);

    // Find the closest point to the center.
    auto closest_point = sc2::Point2D(0.0f, 0.0f);
    auto closest_distance = std::numeric_limits<float>::max();
    bool first = true;

    for (auto i = 0; i < result.size(); ++i) {
        if (!result[i]) {
            continue;
        }

        const auto& point = queries[i].target_pos;

        if (first) {
            closest_point = point;
            closest_distance = sc2::DistanceSquared2D(pivot, point);
            first = false;

            continue;
        }

        const auto distance = sc2::DistanceSquared2D(pivot, point);

        if (distance < closest_distance) {
            closest_point = point;
            closest_distance = distance;
        }
    }

    return closest_point;
}

sc2::Point2D Bot::GetClosestPlace(const sc2::Point2D &center, const sc2::Point2D &pivot, const sc2::Units &pylons, sc2::ABILITY_ID ability_id, float min_radius, float max_radius, float step_size)
{
    // Has to be within 5 units of a pylon.
    auto* query = Query();

    sc2::Point2D current_grid;

    sc2::Point2D previous_grid;

    int valid_queries = 0;

    std::vector<sc2::QueryInterface::PlacementQuery> queries;

    for (float r = min_radius; r < max_radius; r += 1.0f)
    {
        float loc = 0.0f;

        while (loc < 360.0f) {
            sc2::Point2D point = sc2::Point2D((r * std::cos((loc * 3.1415927f) / 180.0f)) + center.x,
                                    (r * std::sin((loc * 3.1415927f) / 180.0f)) + center.y);
            
            if (std::all_of(pylons.begin(), pylons.end(), [&point](const sc2::Unit* pylon) {
                return sc2::DistanceSquared2D(pylon->pos, point) > 5.0f * 5.0f;
            })) {
                loc += step_size;
                continue;
            }

            current_grid = sc2::Point2D(std::floor(point.x), std::floor(point.y));

            if (previous_grid != current_grid) {
                sc2::QueryInterface::PlacementQuery query(ability_id, point);
                queries.push_back(query);
                ++valid_queries;
            }

            previous_grid = current_grid;
            loc += step_size;
        }
    }

    auto result = query->Placement(queries);

    // Find the closest point to the center.
    auto closest_point = sc2::Point2D(0.0f, 0.0f);

    auto closest_distance = std::numeric_limits<float>::max();

    bool first = true;

    for (auto i = 0; i < result.size(); ++i) {
        if (!result[i]) {
            continue;
        }

        const auto& point = queries[i].target_pos;

        if (first) {
            closest_point = point;
            closest_distance = sc2::DistanceSquared2D(pivot, point);
            first = false;

            continue;
        }

        const auto distance = sc2::DistanceSquared2D(pivot, point);

        if (distance < closest_distance) {
            closest_point = point;
            closest_distance = distance;
        }
    }

    return closest_point;
}

sc2::Point2D Bot::GetClosestPlace(const sc2::Point2D &pivot, const sc2::Units &pylons, sc2::ABILITY_ID ability_id, float min_radius, float max_radius, float step_size)
{
    // Select the closest pylon and try to build as close to the pivot as possible.
    // If the first pylon has no possible placements, try the next one.
    // If no pylons have possible placements, return {0.0f, 0.0f}.
    auto* query = Query();

    sc2::Point2D current_grid;

    sc2::Point2D previous_grid;

    auto sorted = pylons;

    std::sort(sorted.begin(), sorted.end(), [&pivot](const sc2::Unit* a, const sc2::Unit* b) {
        return sc2::DistanceSquared2D(a->pos, pivot) < sc2::DistanceSquared2D(b->pos, pivot);
    });

    for (const auto& pylon : pylons) {
        int valid_queries = 0;

        std::vector<sc2::QueryInterface::PlacementQuery> queries;

        for (float r = min_radius; r < max_radius; r += 1.0f)
        {
            float loc = 0.0f;

            while (loc < 360.0f) {
                sc2::Point2D point = sc2::Point2D((r * std::cos((loc * 3.1415927f) / 180.0f)) + pylon->pos.x,
                                        (r * std::sin((loc * 3.1415927f) / 180.0f)) + pylon->pos.y);

                current_grid = sc2::Point2D(std::floor(point.x), std::floor(point.y));

                if (previous_grid != current_grid) {
                    sc2::QueryInterface::PlacementQuery query(ability_id, point);
                    queries.push_back(query);
                    ++valid_queries;
                }

                previous_grid = current_grid;
                loc += step_size;
            }
        }

        auto result = query->Placement(queries);

        // Find the closest point to the pivot.
        auto closest_point = sc2::Point2D(0.0f, 0.0f);
        auto closest_distance = std::numeric_limits<float>::max();
        bool first = true;
        bool any = false;

        for (auto i = 0; i < result.size(); ++i) {
            if (!result[i]) {
                continue;
            }

            any = true;

            const auto& point = queries[i].target_pos;

            if (first) {
                closest_point = point;
                closest_distance = sc2::DistanceSquared2D(pivot, point);
                first = false;

                continue;
            }

            const auto distance = sc2::DistanceSquared2D(pivot, point);

            if (distance < closest_distance) {
                closest_point = point;
                closest_distance = distance;
            }
        }

        if (any) {
            return closest_point;
        }
    }

    return sc2::Point2D(0.0f, 0.0f);
}

sc2::Point2D Bot::GetClosestPlaceWhileAvoiding(const sc2::Point2D &center, const sc2::Point2D &pivot, const sc2::Units &avoid, sc2::ABILITY_ID ability_id, float min_radius, float max_radius, float avoid_radius, bool prefer_distance, float step_size)
{
    auto* query = Query();

    sc2::Point2D current_grid;

    sc2::Point2D previous_grid;

    int valid_queries = 0;

    std::vector<sc2::QueryInterface::PlacementQuery> queries;

    for (float r = min_radius; r < max_radius; r += 1.0f)
    {
        float loc = 0.0f;

        while (loc < 360.0f) {
            sc2::Point2D point = sc2::Point2D((r * std::cos((loc * 3.1415927f) / 180.0f)) + center.x,
                                    (r * std::sin((loc * 3.1415927f) / 180.0f)) + center.y);

            current_grid = sc2::Point2D(std::floor(point.x), std::floor(point.y));

            if (previous_grid != current_grid) {
                sc2::QueryInterface::PlacementQuery query(ability_id, point);
                queries.push_back(query);
                ++valid_queries;
            }

            previous_grid = current_grid;
            loc += step_size;
        }
    }

    auto result = query->Placement(queries);

    // Find the closest point to the pivot.
    auto closest_point = sc2::Point2D(0.0f, 0.0f);
    auto closest_distance = prefer_distance ? std::numeric_limits<float>::max() : 0.0f;
    bool first = true;

    auto closest_unwanted_point = sc2::Point2D(0.0f, 0.0f);
    auto closest_unwanted_distance = prefer_distance ? std::numeric_limits<float>::max() : 0.0f;
    bool first_unwanted = true;

    for (auto i = 0; i < result.size(); ++i) {
        if (!result[i]) {
            continue;
        }

        const auto& point = queries[i].target_pos;

        bool unwanted = AnyWithingRange(avoid, point, avoid_radius);

        if (unwanted) {
            if (first_unwanted) {
                closest_unwanted_point = point;
                closest_unwanted_distance = sc2::DistanceSquared2D(pivot, point);
                first_unwanted = false;
            }

            const auto distance = sc2::DistanceSquared2D(pivot, point);

            if (prefer_distance ? distance < closest_unwanted_distance : distance > closest_unwanted_distance) {
                closest_unwanted_point = point;
                closest_unwanted_distance = distance;
            }

            continue;
        }

        if (first) {
            closest_point = point;
            closest_distance = sc2::DistanceSquared2D(pivot, point);
            first = false;

            continue;
        }

        const auto distance = sc2::DistanceSquared2D(pivot, point);

        if (prefer_distance ? distance < closest_distance : distance > closest_distance) {
            closest_point = point;
            closest_distance = distance;
        }
    }

    // If we have a point that is wanted, return it, otherwise return the closest unwanted point.
    if (!first) {
        return closest_point;
    }
    
    return closest_unwanted_point;
}

sc2::Point2D Bot::GetBestCenter(const sc2::Units &units, sc2::ABILITY_ID ability_id, float min_radius, float max_radius, float benchmark_radius, float step_size)
{
    auto* query = Query();

    sc2::Point2D current_grid;

    sc2::Point2D previous_grid;

    int valid_queries = 0;

    std::vector<sc2::QueryInterface::PlacementQuery> queries;
    
    for (const auto& unit : units) {
        for (float r = min_radius; r < max_radius; r += 1.0f)
        {
            float loc = 0.0f;

            while (loc < 360.0f) {
                sc2::Point2D point = sc2::Point2D((r * std::cos((loc * 3.1415927f) / 180.0f)) + unit->pos.x,
                                        (r * std::sin((loc * 3.1415927f) / 180.0f)) + unit->pos.y);

                current_grid = sc2::Point2D(std::floor(point.x), std::floor(point.y));

                if (previous_grid != current_grid) {
                    sc2::QueryInterface::PlacementQuery query(ability_id, point);
                    queries.push_back(query);
                    ++valid_queries;
                }

                previous_grid = current_grid;
                loc += step_size;
            }
        }
    }

    auto result = query->Placement(queries);

    // Find the center which covers the most points.
    sc2::Point2D best_center = sc2::Point2D(0.0f, 0.0f);
    int best_count = 0;
    
    for (auto i = 0; i < result.size(); ++i) {
        if (!result[i]) {
            continue;
        }

        const auto& point = queries[i].target_pos;

        int count = 0;

        for (const auto& unit : units) {
            if (sc2::DistanceSquared2D(unit->pos, point) < benchmark_radius * benchmark_radius) {
                ++count;
            }
        }

        if (count > best_count) {
            best_center = point;
            best_count = count;
        }
    }

    return best_center;
}

std::pair<sc2::Point2D, float> Bot::GetBestPath(const sc2::Unit* unit, const sc2::Point2D &center, float min_radius, float max_radius, float step_size)
{
    auto* query = Query();

    sc2::Point2D current_grid;

    sc2::Point2D previous_grid;

    int valid_queries = 0;

    std::vector<sc2::QueryInterface::PathingQuery> queries;

    for (float r = min_radius; r < max_radius; r += 1.0f)
    {
        float loc = 0.0f;

        while (loc < 360.0f) {
            sc2::Point2D point = sc2::Point2D((r * std::cos((loc * 3.1415927f) / 180.0f)) + center.x,
                                    (r * std::sin((loc * 3.1415927f) / 180.0f)) + center.y);

            current_grid = sc2::Point2D(std::floor(point.x), std::floor(point.y));

            if (previous_grid != current_grid) {
                sc2::QueryInterface::PathingQuery query{unit->tag, unit->pos, point};
                queries.push_back(query);
                ++valid_queries;
            }

            previous_grid = current_grid;
            loc += step_size;
        }
    }

    if (queries.size() == 0) {
        return std::make_pair(center, 0.0f);
    }

    auto result = query->PathingDistance(queries);

    // Find the shortest path.
    auto best_path = center;
    auto shortest_distance = std::numeric_limits<float>::max();

    for (auto i = 0; i < result.size(); ++i) {
        if (result[i] <= 0.0f) {
            continue;
        }

        const auto& point = queries[i].end_;

        if (result[i] < shortest_distance) {
            best_path = point;
            shortest_distance = result[i];
        }
    }

    return std::make_pair(best_path, shortest_distance);
}

sc2::Point2D Bot::GetClosestRamp(const sc2::Point2D &center)
{
    auto closest_ramp = m_Ramps[0].point;
    auto closest_distance = sc2::Distance2D(center, closest_ramp);

    for (auto i = 1; i < m_Ramps.size(); ++i) {
        const auto& ramp = m_Ramps[i].point;
        const auto distance = sc2::Distance2D(center, ramp);

        if (distance < closest_distance) {
            closest_ramp = ramp;
            closest_distance = distance;
        }
    }

    return closest_ramp;
}

bool Bot::AnyInProgress(const sc2::UNIT_TYPEID &unit_id)
{
    const auto& units = GetUnits(unit_id);

    return std::any_of(units.begin(), units.end(), [](const sc2::Unit* unit) {
        return Utilities::IsInProgress(unit);
    });
}

sc2::Units Bot::RedistributeWorkers(const sc2::Unit *base, int32_t& workers_needed)
{
    auto* actions = Actions();

    // Redistribute workers to the minerals and vespene geysers.
    // If the base has more workers than it can support, return the excess workers.
    // If the base has less workers than it can support, return the number of workers needed.
    const auto* obs = Observation();

    // Both mineral fields and vespene geysers are considered as points of interest.
    const auto& points = obs->GetUnits(sc2::Unit::Alliance::Neutral, [base](const sc2::Unit& unit_) {
        // If it's more than 15 units away, ignore it.
        if (sc2::DistanceSquared2D(unit_.pos, base->pos) > 15.0f * 15.0f) {
            return false;
        }

        return  unit_.unit_type == sc2::UNIT_TYPEID::NEUTRAL_MINERALFIELD ||
                unit_.unit_type == sc2::UNIT_TYPEID::NEUTRAL_MINERALFIELD450 ||
                unit_.unit_type == sc2::UNIT_TYPEID::NEUTRAL_MINERALFIELD750 ||
                unit_.unit_type == sc2::UNIT_TYPEID::NEUTRAL_RICHMINERALFIELD ||
                unit_.unit_type == sc2::UNIT_TYPEID::NEUTRAL_RICHMINERALFIELD750 ||
                unit_.unit_type == sc2::UNIT_TYPEID::NEUTRAL_PURIFIERMINERALFIELD ||
                unit_.unit_type == sc2::UNIT_TYPEID::NEUTRAL_PURIFIERMINERALFIELD750 ||
                unit_.unit_type == sc2::UNIT_TYPEID::NEUTRAL_PURIFIERRICHMINERALFIELD ||
                unit_.unit_type == sc2::UNIT_TYPEID::NEUTRAL_PURIFIERRICHMINERALFIELD750 ||
                unit_.unit_type == sc2::UNIT_TYPEID::NEUTRAL_LABMINERALFIELD ||
                unit_.unit_type == sc2::UNIT_TYPEID::NEUTRAL_LABMINERALFIELD750 ||
                unit_.unit_type == sc2::UNIT_TYPEID::NEUTRAL_BATTLESTATIONMINERALFIELD ||
                unit_.unit_type == sc2::UNIT_TYPEID::NEUTRAL_BATTLESTATIONMINERALFIELD750 ||
                unit_.unit_type == sc2::UNIT_TYPEID::PROTOSS_ASSIMILATOR;
    });
    
    auto probes = WithinRange(GetUnits(sc2::UNIT_TYPEID::PROTOSS_PROBE), base->pos, 15.0f);

    const auto num_workers = probes.size();

    std::unordered_set<const sc2::Unit*> assigned_workers;

    workers_needed = 0;
    uint32_t mineral_points = 0;

    for (const auto& point : points) {
        if (point->unit_type != sc2::UNIT_TYPEID::PROTOSS_ASSIMILATOR) {
            if (point->mineral_contents == 0) {
                continue;
            }

            workers_needed += 2;
            ++mineral_points;
            continue;
        }

        if (IsInProgress(point)) {
            continue;
        }

        if (point->vespene_contents == 0) {
            continue;
        }

        workers_needed += 3;

        if (assigned_workers.size() == num_workers) {
            continue;
        }

        // Find the amount of workers on this point.
        sc2::Units workers;

        for (const auto& probe : probes) {
            for (const auto& order : probe->orders) {
                if (order.target_unit_tag == point->tag) {
                    assigned_workers.emplace(probe);
                    workers.push_back(probe);
                    break;
                }
            }
        }

        const auto num_workers_on_point = workers.size();

        const auto num_workers_needed = 3 - num_workers_on_point;

        if (num_workers_needed <= 0) {
            continue;
        }
        
        while (num_workers_needed > 0) {
            // Find the closest non-assigned worker.
            const sc2::Unit* closest_worker = nullptr;
            float closest_distance = std::numeric_limits<float>::max();

            for (const auto& probe : probes) {
                if (assigned_workers.find(probe) != assigned_workers.end()) {
                    continue;
                }

                // Make sure it's targeting a mineral field.
                if (probe->orders.size() == 0) {
                    continue;
                }
                
                bool found = false;

                for (const auto& order : probe->orders) {
                    for (const auto& point : points) {
                        if (point->unit_type == sc2::UNIT_TYPEID::PROTOSS_ASSIMILATOR) {
                            continue;
                        }

                        if (order.target_unit_tag == point->tag) {
                            found = true;
                            break;
                        }
                    }
                }

                if (found) {
                    continue;
                }

                const auto distance = sc2::DistanceSquared2D(probe->pos, point->pos);

                if (distance < closest_distance) {
                    closest_worker = probe;
                    closest_distance = distance;
                }
            }

            if (closest_worker == nullptr) {
                break;
            }

            Actions()->UnitCommand(closest_worker, sc2::ABILITY_ID::HARVEST_GATHER, point);      

            assigned_workers.emplace(closest_worker);      
        }
    }

    // Assign the rest of the workers until we have (mineral points * 2) workers.
    for (const auto& probe : probes) {
        if (assigned_workers.find(probe) != assigned_workers.end()) {
            continue;
        }

        if (mineral_points * 2 == assigned_workers.size()) {
            break;
        }

        assigned_workers.emplace(probe);

        // If the probe is not already mining, assign it to the closest mineral field.
        if (!AnyQueuedOrder(probe, sc2::ABILITY_ID::HARVEST_RETURN)) {
            bool targeting_assimilator = false;

            for (const auto& order : probe->orders) {
                if (order.ability_id == sc2::ABILITY_ID::HARVEST_GATHER) {
                    for (const auto& point : points) {
                        if (point->unit_type == sc2::UNIT_TYPEID::PROTOSS_ASSIMILATOR && order.target_unit_tag == point->tag) {
                            targeting_assimilator = true;
                            break;
                        }
                    }
                }
            }

            if (!targeting_assimilator) {
                continue;
            }
        }

        const sc2::Unit* closest_point = nullptr;
        float closest_distance = std::numeric_limits<float>::max();

        for (const auto& point : points) {
            if (point->unit_type == sc2::UNIT_TYPEID::PROTOSS_ASSIMILATOR) {
                continue;
            }

            // Count the number of workers on this point.
            int32_t num_workers_on_point = std::count_if(probes.begin(), probes.end(), [&point](const sc2::Unit* probe) {
                for (const auto& order : probe->orders) {
                    if (MiningAbilities.find(order.ability_id) != MiningAbilities.end() && order.target_unit_tag == point->tag) {
                        return true;
                    }
                }

                return false;
            });

            if (num_workers_on_point >= 2) {
                continue;
            }

            const auto distance = sc2::DistanceSquared2D(probe->pos, point->pos);

            if (distance < closest_distance) {
                closest_point = point;
                closest_distance = distance;
            }
        }

        if (closest_point == nullptr) {
            continue;
        }

        Actions()->UnitCommand(probe, sc2::ABILITY_ID::HARVEST_GATHER, closest_point, true);
    }

    workers_needed -= assigned_workers.size();

    workers_needed = std::max(0, workers_needed);

    // Return units not assigned to any point.
    sc2::Units excess_workers;

    for (const auto& probe : probes) {
        if (assigned_workers.find(probe) == assigned_workers.end()) {
            excess_workers.push_back(probe);
        }
    }

    return excess_workers;
}

void Bot::ReturnToMining(const sc2::Unit *probe)
{
    if (probe->unit_type != sc2::UNIT_TYPEID::PROTOSS_PROBE) {
        return;
    }

    const auto mining_points = Observation()->GetUnits(sc2::Unit::Alliance::Neutral, [](const sc2::Unit& unit_) {
        return  unit_.unit_type == sc2::UNIT_TYPEID::NEUTRAL_MINERALFIELD ||
                unit_.unit_type == sc2::UNIT_TYPEID::NEUTRAL_MINERALFIELD450 ||
                unit_.unit_type == sc2::UNIT_TYPEID::NEUTRAL_MINERALFIELD750 ||
                unit_.unit_type == sc2::UNIT_TYPEID::NEUTRAL_RICHMINERALFIELD ||
                unit_.unit_type == sc2::UNIT_TYPEID::NEUTRAL_RICHMINERALFIELD750 ||
                unit_.unit_type == sc2::UNIT_TYPEID::NEUTRAL_PURIFIERMINERALFIELD ||
                unit_.unit_type == sc2::UNIT_TYPEID::NEUTRAL_PURIFIERMINERALFIELD750 ||
                unit_.unit_type == sc2::UNIT_TYPEID::NEUTRAL_PURIFIERRICHMINERALFIELD ||
                unit_.unit_type == sc2::UNIT_TYPEID::NEUTRAL_PURIFIERRICHMINERALFIELD750 ||
                unit_.unit_type == sc2::UNIT_TYPEID::NEUTRAL_LABMINERALFIELD ||
                unit_.unit_type == sc2::UNIT_TYPEID::NEUTRAL_LABMINERALFIELD750 ||
                unit_.unit_type == sc2::UNIT_TYPEID::NEUTRAL_BATTLESTATIONMINERALFIELD ||
                unit_.unit_type == sc2::UNIT_TYPEID::NEUTRAL_BATTLESTATIONMINERALFIELD750 ||
                unit_.unit_type == sc2::UNIT_TYPEID::PROTOSS_ASSIMILATOR;
    });

    const auto& probes = GetUnits(sc2::UNIT_TYPEID::PROTOSS_PROBE);
    const auto& nexus_units = GetUnits(sc2::UNIT_TYPEID::PROTOSS_NEXUS);

    // Find the closest:
    // * Mining point withing 15 units of a Nexus;
    // * That has less than 2 probes mining it (3 for gas);
    // * and that is not already being mined by a probe.
    
    const sc2::Unit* closest_mining_point = nullptr;
    float closest_distance = std::numeric_limits<float>::max();

    // Start by filtering out the mining points that are not within 15 units of a Nexus or have sufficient probes mining them.
    for (const auto& mining_point : mining_points) {
        float closest_nexus_distance = std::numeric_limits<float>::max();
        for (const auto& nexus : nexus_units) {
            if (nexus->build_progress < 1.0f) {
                continue;
            }

            const auto distance = sc2::DistanceSquared2D(mining_point->pos, nexus->pos);
            if (distance < closest_nexus_distance) {
                closest_nexus_distance = distance;
            }
        }

        if (closest_nexus_distance > PROBE_RANGE_SQUARED) {
            continue;
        }

        const auto num_gas_probes = std::count_if(probes.begin(), probes.end(), [&mining_point](const sc2::Unit* probe) {
            for (const auto& order : probe->orders) {
                if (MiningAbilities.find(order.ability_id) != MiningAbilities.end() && order.target_unit_tag == mining_point->tag) {
                    return true;
                }
            }

            return false;
        });

        if (mining_point->unit_type == sc2::UNIT_TYPEID::PROTOSS_ASSIMILATOR) {
            if (num_gas_probes >= 3) {
                continue;
            }
        } else {
            if (num_gas_probes >= 2) {
                continue;
            }
        }

        const auto distance = sc2::DistanceSquared2D(probe->pos, mining_point->pos);

        if (distance < closest_distance) {
            closest_distance = distance;
            closest_mining_point = mining_point;
        }
    }

    if (closest_mining_point == nullptr) {
        return;
    }

    auto* actions = Actions();

    actions->UnitCommand(probe, sc2::ABILITY_ID::HARVEST_GATHER, closest_mining_point);
}

bool Bot::IsMining(const sc2::Unit *probe)
{
    return std::any_of(probe->orders.begin(), probe->orders.end(), [](const sc2::UnitOrder& order) {
        return MiningAbilities.find(order.ability_id) != MiningAbilities.end();
    });
}

void Bot::RedistributeWorkers(const sc2::Units &bases)
{
    std::unordered_map<const sc2::Unit*, int32_t> workers_needed_map;
    std::unordered_map<const sc2::Unit*, sc2::Units> excess_workers_map;

    // Find all idle probes
    const auto& probes = GetUnits(sc2::UNIT_TYPEID::PROTOSS_PROBE);

    for (const auto* probe : probes) {
        if (probe->orders.size() == 0) {
            ReturnToMining(probe);
        }
    }

    for (const auto& base : bases) {
        if (base->build_progress < 0.9f) {
            continue;
        }

        int32_t workers_needed = 0;
        const auto& excess_workers = RedistributeWorkers(base, workers_needed);

        workers_needed_map.emplace(base, workers_needed);
        excess_workers_map.emplace(base, excess_workers);
    }

    // Redistribute excess workers, move them to the base that is closest to the base that can give away workers.
    for (const auto& base : bases) {
        const auto& excess_workers = excess_workers_map.find(base);

        if (excess_workers == excess_workers_map.end()) {
            continue;
        }

        auto excess = excess_workers->second;

        if (excess.empty()) {
            continue;
        }

        // Give away excess workers.
        for (const auto& other_base : bases) {
            if (other_base == base) {
                continue;
            }

            const auto& workers_needed = workers_needed_map.find(other_base);

            if (workers_needed == workers_needed_map.end()) {
                continue;
            }

            if (workers_needed->second <= 0) {
                continue;
            }

            const auto excess_size = excess.size();

            for (int i = 0; i < workers_needed->second; ++i) {
                if (excess.size() == 0) {
                    break;
                }

                const auto& worker = excess.back();

                Actions()->UnitCommand(worker, sc2::ABILITY_ID::MOVE_MOVE, other_base->pos);

                excess.pop_back();
            }
        }
    }
}

BuildResult Bot::AttemptBuild(sc2::ABILITY_ID ability_id)
{
    const auto* obs = Observation();

    const auto& requirements = UnitRequirements.find(ability_id);

    if (requirements == UnitRequirements.end()) {
        return BuildResult(false);
    }

    const auto& required_units = requirements->second;
    
    // Make sure we have all the required units.
    bool has_requirements = true;
    float approx_time_left_on_requirements = 0.0f;

    for (const auto& requirement : required_units) {
        bool found = false;
        float max_time_left = 0.0f;
        const auto& units = GetUnits(requirement);
        for (const auto& unit : units) {
            if (unit->unit_type == requirement) {
                if (unit->build_progress == 1.0f) {
                    found = true;
                    break;
                }
                
                auto build_time = obs->GetUnitTypeData().at(unit->unit_type).build_time / 22.4f;

                // Round up to the nearest second.
                //build_time = std::ceil(build_time);

                const auto time_left = (1.0f - unit->build_progress) * build_time;

                if (time_left > max_time_left) {
                    max_time_left = time_left;
                }
            }
        }

        if (!found) {
            if (max_time_left == 0.0f) {
                has_requirements = false;
                break;
            }

            if (max_time_left > approx_time_left_on_requirements) {
                approx_time_left_on_requirements = max_time_left;
            }
        }
    }

    if (!has_requirements) {
        return BuildResult(false);
    }

    // Get the closest probe to the ideal position.
    const auto& probes = GetUnits(sc2::UNIT_TYPEID::PROTOSS_PROBE);

    // Select the subset that are either idle or are gathering minerals.
    sc2::Units subset = {};
    uint32_t num_mineral_probes = 0;
    uint32_t num_gas_probes = 0;

    for (const auto& probe : probes) {
        if (probe->orders.size() == 0) {
            subset.push_back(probe);
            continue;
        }

        const auto& order = probe->orders[0];

        if (MiningAbilities.find(order.ability_id) != MiningAbilities.end()) {
            subset.push_back(probe);
            
            const auto target = obs->GetUnit(order.target_unit_tag);

            if (target != nullptr && target->unit_type == sc2::UNIT_TYPEID::PROTOSS_ASSIMILATOR) {
                num_gas_probes++;
            } else {
                num_mineral_probes++;
            }
        }
    }

    const auto& cost_iter = AbilityCosts.find(ability_id);

    if (cost_iter == AbilityCosts.end()) {
        return BuildResult(false);
    }

    const auto& cost = cost_iter->second;

    if (m_Resources < cost) {
        // Assume each probe can gather 1 mineral per second.
        const float mineral_rate = num_mineral_probes * 1.15f;
        const float vespene_rate = num_gas_probes * 1.15f;

        const float mineral_time = (cost.minerals - m_Resources.minerals) / mineral_rate;
        const float vespene_time = (cost.vespene - m_Resources.vespene) / vespene_rate;

        const auto max_time = std::max(mineral_time, vespene_time);

        approx_time_left_on_requirements = std::max(approx_time_left_on_requirements, max_time);
    }

    const auto is_unit = UnitTrainTypes.find(ability_id) != UnitTrainTypes.end();

    if (is_unit) {
        const auto ideal_production = GetIdealUnitProduction(ability_id);

        if (!ideal_production.success) {
            return BuildResult(false);
        }
        
        // TODO: Warpgate research.

        if (approx_time_left_on_requirements == 0.0f) {
            // Train the unit.
            Actions()->UnitCommand(ideal_production.building, ability_id);

            return BuildResult(true, 0.0f, cost);
        }

        // Add delay to the order.
        /*return BuildResult(true, approx_time_left_on_requirements, cost, ideal_production.building->tag, DelayedOrder{
            ability_id,
            ideal_production.warp_position,
            0,
            FromGameTimeSource(approx_time_left_on_requirements)
        });*/

        return BuildResult(false, approx_time_left_on_requirements, cost);
    }

    const auto is_upgrade = UpgradeTypes.find(ability_id) != UpgradeTypes.end();

    if (is_upgrade) {
        const auto upgrade_building = AssociatedBuilding.find(ability_id);

        if (upgrade_building == AssociatedBuilding.end()) {
            return BuildResult(false);
        }

        const auto& building_type = upgrade_building->second;

        const auto& buildings = Utilities::FilterOutInProgress(GetUnits(building_type));

        if (buildings.size() == 0) {
            return BuildResult(false);
        }
        
        // Select the building with the lowest amount of orders.
        const auto& building_iter = std::min_element(buildings.begin(), buildings.end(), [](const sc2::Unit* a, const sc2::Unit* b) {
            const auto& a_upgrades = a->orders;
            const auto& b_upgrades = b->orders;
            
            return a_upgrades.size() < b_upgrades.size();
        });

        if (building_iter == buildings.end()) {
            return BuildResult(false);
        }

        const auto& building = *building_iter;

        if (approx_time_left_on_requirements == 0.0f) {
            // Upgrade the building.
            Actions()->UnitCommand(building, ability_id);

            return BuildResult(true, 0.0f, cost);
        }

        return BuildResult(true, approx_time_left_on_requirements, cost, building->tag, DelayedOrder{
            ability_id,
            sc2::Point2D(0.0f, 0.0f),
            0,
            FromGameTimeSource(approx_time_left_on_requirements)
        });
    }
    

    // Get the ideal position to build the structure.
    const auto ideal_position = GetIdealPosition(ability_id);

    if (ideal_position.x == 0.0f && ideal_position.y == 0.0f) {
        std::cout << "No ideal position found for " << sc2::AbilityTypeToName(ability_id) << std::endl;
        return BuildResult(false);
    }

    if (probes.size() == 0) {
        return BuildResult(false);
    }

    const auto& probe_iter = std::min_element(subset.begin(), subset.end(), [&ideal_position](const sc2::Unit* a, const sc2::Unit* b) {
        return sc2::DistanceSquared2D(a->pos, ideal_position) < sc2::DistanceSquared2D(b->pos, ideal_position);
    });

    if (probe_iter == subset.end()) {
        return BuildResult(false);
    }

    const auto& probe = *probe_iter;

    const auto movePosition = GetBestPath(
        probe,
        ideal_position,
        ability_id == sc2::ABILITY_ID::BUILD_PYLON ? 1.0f : 2.0f,
        ability_id == sc2::ABILITY_ID::BUILD_PYLON ? 2.0f : 3.0f
    );

    /*
    auto* query = Query();

    sc2::QueryInterface::PathingQuery pathing_query;
    pathing_query.start_ = probe->pos;
    pathing_query.end_ = ideal_position;
    pathing_query.start_unit_tag_ = probe->tag;

    const auto result = query->PathingDistance({pathing_query});

    if (result.size() == 0) {
        return BuildResult(false);
    }

    const auto& pathing_result = result[0];*/

    // Get the unit movement speed.
    const auto& unit_data = obs->GetUnitTypeData().at(probe->unit_type);

    const auto& movement_speed = unit_data.movement_speed;

    // Calculate the time it will take to reach the ideal position.
    //const auto& distance = sc2::Distance2D(probe->pos, ideal_position);

    const auto& time_to_reach = movePosition.second / movement_speed;

    const sc2::Unit* target = nullptr;

    if (ability_id == sc2::ABILITY_ID::BUILD_ASSIMILATOR) {
        // Find the closest vespene geyser.
        const auto vespene_geysers = obs->GetUnits(sc2::Unit::Alliance::Neutral, [](const sc2::Unit& unit_) {
            return  unit_.unit_type == sc2::UNIT_TYPEID::NEUTRAL_VESPENEGEYSER ||
                    unit_.unit_type == sc2::UNIT_TYPEID::NEUTRAL_PROTOSSVESPENEGEYSER ||
                    unit_.unit_type == sc2::UNIT_TYPEID::NEUTRAL_PURIFIERVESPENEGEYSER ||
                    unit_.unit_type == sc2::UNIT_TYPEID::NEUTRAL_SHAKURASVESPENEGEYSER ||
                    unit_.unit_type == sc2::UNIT_TYPEID::NEUTRAL_RICHVESPENEGEYSER;
        });

        const auto& closest_vespene_geyser = std::min_element(vespene_geysers.begin(), vespene_geysers.end(), [&ideal_position](const sc2::Unit* a, const sc2::Unit* b) {
            return sc2::DistanceSquared2D(a->pos, ideal_position) < sc2::DistanceSquared2D(b->pos, ideal_position);
        });

        if (closest_vespene_geyser == vespene_geysers.end()) {
            return BuildResult(false);
        }
        
        target = *closest_vespene_geyser;
    }

    if (approx_time_left_on_requirements == 0.0f) {
        // Build the structure.

        if (target != nullptr) {
            Actions()->UnitCommand(probe, ability_id, target);
        } else {
            Actions()->UnitCommand(probe, ability_id, ideal_position);
        }

        return BuildResult(true, time_to_reach, cost);
    }

    // If the time to reach the ideal position is greater than the time left on the requirements, return.
    if (approx_time_left_on_requirements > time_to_reach) {
        return BuildResult(false, approx_time_left_on_requirements - time_to_reach - 0.5f, cost);
    }

    // Move the probe to the ideal position.
    Actions()->UnitCommand(probe, sc2::ABILITY_ID::MOVE_MOVE, movePosition.first);

    // Delay the order.
    return BuildResult(true, 0, cost, probe->tag, DelayedOrder{
        ability_id,
        ideal_position,
        target != nullptr ? target->tag : 0,
        FromGameTimeSource(time_to_reach),
    });
}

void Bot::FindRamps()
{
    const auto& gameInfo = Observation()->GetGameInfo();
    /*
    const auto& pathing_grid = gameInfo.pathing_grid;
    const auto& placement_grid = gameInfo.placement_grid;
    // Convert string to uint8_t vector.
    const auto& data_path = std::vector<uint8_t>(pathing_grid.data.begin(), pathing_grid.data.end());
    const auto& data_placement = std::vector<uint8_t>(placement_grid.data.begin(), placement_grid.data.end());

    // Create a delta map.
    std::vector<uint8_t> delta_map;
    auto size = pathing_grid.width * pathing_grid.height;
    delta_map.reserve(size);

    for (auto i = 0; i < size; ++i) {
        delta_map.push_back(data_path[i] - data_placement[i]);
    }

    // Find the ramps.
    std::vector<sc2::Point2D> ramps;

    for (auto x = 0; x < pathing_grid.width; ++x) {
        for (auto y = 0; y < pathing_grid.height; ++y) {
            if (delta_map[x + y * pathing_grid.width] != 0) {
                ramps.push_back(sc2::Point2D(static_cast<float>(x), static_cast<float>(y)));
                auto height = gameInfo.terrain_height.data[x + y * pathing_grid.width];

                Debug()->DebugTextOut(std::to_string(x) + ", " + std::to_string(y), sc2::Point3D(static_cast<float>(x), static_cast<float>(y), height + 1.0f), sc2::Colors::Green);
            }
        }
    }

    m_Ramps.resize(ramps.size());

    for (auto i = 0; i < ramps.size(); ++i) {
        m_Ramps[i].point = ramps[i];
    }
    */

    auto* debug = Debug();

    const auto* observation = Observation();

    std::vector<sc2::Point3D> rampTerrain;
    std::unordered_set<float> playableHeights;

    for (auto i = gameInfo.playable_min.x; i < gameInfo.playable_max.x; i++)
    {
        for (auto j = gameInfo.playable_min.y; j < gameInfo.playable_max.y; j++)
        {
            const auto& point = sc2::Point2D(i, j);
            const auto& pathing = observation->IsPathable(point);
            const auto& placement = observation->IsPlacable(point);
            const auto height = observation->TerrainHeight(point);

            if (placement)
            {
                playableHeights.insert(height);
            }

            if (pathing && !placement)
            {
                rampTerrain.push_back(sc2::Point3D(point.x, point.y, height));

                //debug->DebugBoxOut(sc2::Point3D(point.x-0.5f, point.y-0.5f, height), sc2::Point3D(point.x+0.5f, point.y+0.5f, height+1.0f), sc2::Colors::Green);
            }
        }
    }

    // Filter out those points which are not within 0.3 of a playable height.
    for (auto i = 0; i < rampTerrain.size(); i++)
    {
        const auto& point = rampTerrain[i];
        
        auto found = false;

        for (auto height : playableHeights)
        {
            if (std::abs(point.z - height) < 0.3f)
            {
                found = true;
                break;
            }
        }

        if (!found)
        {
            rampTerrain.erase(rampTerrain.begin() + i);
            i--;
        }
    }

    // Cluster the points.
    auto clusters = std::vector<std::pair<sc2::Point3D, std::vector<sc2::Point3D>>>();

    for (auto i = 0; i < rampTerrain.size(); i++)
    {
        const auto& point = rampTerrain[i];

        auto found = false;

        for (auto& cluster : clusters)
        {
            // Has to be within 15 units of the cluster and not different in height by more than 0.5.
            if (sc2::DistanceSquared2D(cluster.first, point) < 7.0f * 7.0f &&
                std::abs(cluster.first.z - point.z) < 0.5f)
            {
                cluster.second.push_back(point);
                found = true;

                // Recalculate the center of mass.
                sc2::Point3D center = {0.0f, 0.0f, 0.0f};

                for (auto& p : cluster.second)
                {
                    center.x += p.x;
                    center.y += p.y;
                    center.z += p.z;
                }

                center.x /= cluster.second.size();
                center.y /= cluster.second.size();
                center.z /= cluster.second.size();

                cluster.first = center;

                break;
            }
        }

        if (!found)
        {
            clusters.push_back({point, {point}});
        }
    }

    m_Ramps.resize(clusters.size());

    for (auto i = 0; i < clusters.size(); i++)
    {
        m_Ramps[i].point = clusters[i].first;

        //debug->DebugCreateUnit(sc2::UNIT_TYPEID::PROTOSS_OBSERVER, clusters[i].first);
    }

    debug->SendDebug();

    const auto* query = Query();
}
