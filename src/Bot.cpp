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

    m_Ramps = Map::FindRamps(Query(), Observation());
    m_Expansions = sc2::search::CalculateExpansionLocations(Observation(), Query());

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

    const auto& nexus_units = m_Collective->GetAlliedUnitsOfType(sc2::UNIT_TYPEID::PROTOSS_NEXUS);
    const auto& probes = m_Collective->GetAlliedUnitsOfType(sc2::UNIT_TYPEID::PROTOSS_PROBE);

    float time_in_seconds = ElapsedTime();

    m_Resources = { 
        static_cast<int32_t>(obs->GetMinerals()),
        static_cast<int32_t>(obs->GetVespene())
    };

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

    if (nexus_units.empty()) {
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
        if (m_Expansions.empty()) {
            return sc2::Point2D(0.0f, 0.0f);
        }

        const auto& nexus_units = m_Collective->GetAlliedUnitsOfType(sc2::UNIT_TYPEID::PROTOSS_NEXUS);

        if (nexus_units.empty()) {
            const auto& probes = m_Collective->GetAlliedUnitsOfType(sc2::UNIT_TYPEID::PROTOSS_PROBE);

            if (probes.empty()) {
                return sc2::Point2D(0.0f, 0.0f);
            }

            const auto& probe = probes[0];

            const auto closest_expansion = Utilities::ClosestTo(m_Expansions, probe->pos);

            return closest_expansion;
        }

        const auto& closest_expansion = Utilities::ClosestAverageTo(m_Expansions, nexus_units);

        return closest_expansion;
    }

    const auto& pylons = m_Collective->GetAlliedUnitsOfType(sc2::UNIT_TYPEID::PROTOSS_PYLON);

    if (ability_id == sc2::ABILITY_ID::BUILD_PYLON) {
        // Build the Pylon at the closest ramp.
        const auto& nexus_units = m_Collective->GetAlliedUnitsOfType(sc2::UNIT_TYPEID::PROTOSS_NEXUS);

        if (nexus_units.empty()) {
            return sc2::Point2D(0.0f, 0.0f);
        }

        if (pylons.empty()) {
            const auto& closest_nexus = nexus_units[0];

            const auto& ramp = GetClosestRamp(closest_nexus->pos);

            return Map::GetClosestPlace(Query(), ramp, closest_nexus->pos, ability_id, 2.0f, 4.0f);
        }

        const auto unpowered_structures = Utilities::FilterUnits(m_AllUnits, [this](const sc2::Unit* unit) {
            return Utilities::RequiresPower(unit) && !Utilities::IsPowered(unit);
        });

        if (unpowered_structures.size() != 0) {
            return Map::GetBestCenter(Query(), unpowered_structures, ability_id, 3.0f, 5.0f, 5.0f);
        }

        const auto mining_points = Utilities::GetResourcePoints(m_AllUnits, true, false, true);

        const sc2::Unit* fewest_pylons = Utilities::SelectUnitMin(nexus_units, [this, &pylons](const sc2::Unit* nexus) {
            return static_cast<float>(Utilities::CountWithinRange(pylons, nexus->pos, 15.0f));
        });

        // Place while avoiding both pylons and mining points.
        sc2::Units avoid = Utilities::Union(pylons, mining_points);

        return Map::GetClosestPlaceWhileAvoiding(Query(), fewest_pylons->pos, fewest_pylons->pos, avoid, ability_id, 5.0f, 10.0f, 6.0f, true);
    }

    if (ability_id == sc2::ABILITY_ID::BUILD_ASSIMILATOR) {
        // Build the Assimilator at the closest vespene geyser.
        const auto nexus_units = m_Collective->GetAlliedUnitsOfType(sc2::UNIT_TYPEID::PROTOSS_NEXUS);

        if (nexus_units.empty()) {
            return sc2::Point2D(0.0f, 0.0f);
        }

        const auto& assimilators = m_Collective->GetAlliedUnitsOfType(sc2::UNIT_TYPEID::PROTOSS_ASSIMILATOR);
        
        // Select a Nexus that has less than 2 assimilators within 15 units of it.
        const sc2::Unit* selected_nexus = Utilities::SelectUnitMin(nexus_units, [this, &assimilators](const sc2::Unit* nexus) {
            return static_cast<float>(Utilities::CountWithinRange(assimilators, nexus->pos, 15.0f));
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
        const auto& nexus_units = m_Collective->GetAlliedUnitsOfType(sc2::UNIT_TYPEID::PROTOSS_NEXUS);

        if (nexus_units.empty()) {
            return sc2::Point2D(0.0f, 0.0f);
        }

        const auto& closest_nexus = nexus_units[0];

        const auto& ramp = GetClosestRamp(closest_nexus->pos);

        // Check if there already is a Gateway at the ramp.
        const auto& gateways = m_Collective->GetAlliedUnitsOfType(sc2::UNIT_TYPEID::PROTOSS_GATEWAY);

        if (Utilities::AnyWithinRange(gateways, ramp, 8.0f)) {
            return Map::GetClosestPlace(Query(), closest_nexus->pos, pylons, ability_id, 0.0f, 5.0f);
        }

        if (all_in_progress) {
            return Map::GetClosestPlace(Query(), ramp, ramp, sc2::ABILITY_ID::BUILD_BARRACKS, 0.0f, 8.0f);
        }

        return Map::GetClosestPlace(Query(), ramp, ramp, pylons, ability_id, 0.0f, 8.0f);
    }

    if (ability_id == sc2::ABILITY_ID::BUILD_CYBERNETICSCORE) {
        // Build the Cybernetics Core at the closest ramp.
        const auto& nexus_units = m_Collective->GetAlliedUnitsOfType(sc2::UNIT_TYPEID::PROTOSS_NEXUS);

        if (nexus_units.empty()) {
            return sc2::Point2D(0.0f, 0.0f);
        }

        const auto& closest_nexus = nexus_units[0];

        const auto& ramp = GetClosestRamp(closest_nexus->pos);

        if (all_in_progress) {
            return Map::GetClosestPlace(Query(), ramp, ramp, sc2::ABILITY_ID::BUILD_BARRACKS, 0.0f, 8.0f);
        }

        return Map::GetClosestPlace(Query(), ramp, ramp, pylons, ability_id, 0.0f, 8.0f);
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
    const auto training_buildings = m_Collective->GetAlliedUnitsOfType(training_building);

    if (training_buildings.empty()) {
        return TrainResult(false);
    }

    const auto training_building_with_fewest_orders = Utilities::LeastBusy(training_buildings);

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

    const auto& requirements = UnitRequirements.find(delayed_order.ability_id);

    if (requirements != UnitRequirements.end()) {
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

sc2::Point2D Bot::GetClosestRamp(const sc2::Point2D &center)
{
    const auto& closest = std::min_element(m_Ramps.begin(), m_Ramps.end(), [center](const auto& a, const auto& b) {
        return sc2::DistanceSquared2D(center, a.point) < sc2::DistanceSquared2D(center, b.point);
    });

    return closest == m_Ramps.end() ? sc2::Point2D(0.0f, 0.0f) : closest->point;
}

bool Bot::AnyInProgress(const sc2::UNIT_TYPEID &unit_id)
{
    const auto& units = m_Collective->GetAlliedUnitsOfType(unit_id);

    return std::any_of(units.begin(), units.end(), [](const sc2::Unit* unit) {
        return Utilities::IsInProgress(unit);
    });
}

BuildResult Bot::AttemptBuild(sc2::ABILITY_ID ability_id)
{
    const auto* obs = Observation();
    auto* actions = Actions();

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
        const auto& units = m_Collective->GetAlliedUnitsOfType(requirement);
        for (const auto& unit : units) {
            if (unit->unit_type != requirement) {
                continue;
            }
            
            if (!Utilities::IsInProgress(unit)) {
                found = true;
                break;
            }
            
            auto build_time = Utilities::ToSecondsFromGameTime(obs->GetUnitTypeData().at(unit->unit_type).build_time);

            const auto time_left = (1.0f - unit->build_progress) * build_time;

            if (time_left > max_time_left) {
                max_time_left = time_left;
            }
        }

        if (found) {
            continue;
        }

        if (max_time_left == 0.0f) {
            has_requirements = false;
            break;
        }

        if (max_time_left > approx_time_left_on_requirements) {
            approx_time_left_on_requirements = max_time_left;
        }
    }

    if (!has_requirements) {
        return BuildResult(false);
    }

    // Get the closest probe to the ideal position.
    const auto& probes = m_Collective->GetAlliedUnitsOfType(sc2::UNIT_TYPEID::PROTOSS_PROBE);

    // Select the subset that are either idle or are gathering minerals.
    const auto& cost_iter = AbilityCosts.find(ability_id);

    if (cost_iter == AbilityCosts.end()) {
        return BuildResult(false);
    }

    const auto& cost = cost_iter->second;

    if (m_Resources < cost) {
        const auto& [mineral_rate, vespene_rate] = m_Proletariat->GetIncomePerSecond();

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
            actions->UnitCommand(ideal_production.building, ability_id);

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

        const auto& buildings = Utilities::FilterOutInProgress(m_Collective->GetAlliedUnitsOfType(building_type));

        if (buildings.empty()) {
            return BuildResult(false);
        }

        const auto& building = Utilities::LeastBusy(buildings);
        
        if (approx_time_left_on_requirements == 0.0f) {
            // Upgrade the building.
            actions->UnitCommand(building, ability_id);

            return BuildResult(true, 0.0f, cost);
        }

        return BuildResult(true, approx_time_left_on_requirements, cost, building->tag, DelayedOrder{
            ability_id,
            sc2::Point2D(0.0f, 0.0f),
            0,
            approx_time_left_on_requirements + ElapsedTime()
        });
    }
    

    // Get the ideal position to build the structure.
    const auto ideal_position = GetIdealPosition(ability_id);

    if (ideal_position.x == 0.0f && ideal_position.y == 0.0f) {
        std::cout << "No ideal position found for " << sc2::AbilityTypeToName(ability_id) << std::endl;
        return BuildResult(false);
    }

    if (probes.empty()) {
        return BuildResult(false);
    }

    const auto* probe = m_Proletariat->GetWorkerForBuilding(ideal_position);

    const auto movePosition = Map::GetBestPath(
        Query(),
        probe,
        ideal_position,
        ability_id == sc2::ABILITY_ID::BUILD_PYLON ? 1.0f : 2.0f,
        ability_id == sc2::ABILITY_ID::BUILD_PYLON ? 2.0f : 3.0f
    );

    // Get the unit movement speed.
    const auto& unit_data = obs->GetUnitTypeData().at(probe->unit_type);

    const auto& movement_speed = unit_data.movement_speed;

    const auto& time_to_reach = movePosition.second / movement_speed;

    const sc2::Unit* target = nullptr;

    if (ability_id == sc2::ABILITY_ID::BUILD_ASSIMILATOR) {
        // Find the closest vespene geyser.
        const auto vespene_geysers = Utilities::GetResourcePoints(m_NeutralUnits, false, true, false);

        if (vespene_geysers.empty()) {
            return BuildResult(false);
        }

        target = Utilities::ClosestTo(vespene_geysers, ideal_position);
    }

    if (approx_time_left_on_requirements == 0.0f) {
        // Build the structure.

        if (target != nullptr) {
            actions->UnitCommand(probe, ability_id, target);
        } else {
            actions->UnitCommand(probe, ability_id, ideal_position);
        }

        return BuildResult(true, time_to_reach, cost);
    }

    // If the time to reach the ideal position is greater than the time left on the requirements, return.
    if (approx_time_left_on_requirements > time_to_reach) {
        return BuildResult(false, approx_time_left_on_requirements - time_to_reach - 0.5f, cost);
    }

    // Move the probe to the ideal position.
    actions->UnitCommand(probe, sc2::ABILITY_ID::MOVE_MOVE, movePosition.first);

    // Delay the order.
    return BuildResult(true, 0, cost, probe->tag, DelayedOrder{
        ability_id,
        ideal_position,
        target != nullptr ? target->tag : 0,
        time_to_reach + ElapsedTime()
    });
}
