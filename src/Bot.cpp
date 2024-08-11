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

    // Redistribute workers every second.
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

    if (nexus_units.size() == 0) {
        AttemptBuild(sc2::ABILITY_ID::BUILD_NEXUS);
        return;
    }

    m_Resources = { obs->GetMinerals(), obs->GetVespene() };

    m_Resources = m_Resources - GetPlannedCosts();

    Bot::BuildResult result(false);

    if (!AnyQueuedOrders(probes, sc2::ABILITY_ID::BUILD_PYLON) && !AnyInProgress(sc2::UNIT_TYPEID::PROTOSS_PYLON)) {
        const auto supply = obs->GetFoodCap();
        const auto used_supply = obs->GetFoodUsed();

        if ((used_supply >= supply - 2) || (GetUnits(sc2::UNIT_TYPEID::PROTOSS_PYLON).size() == 0)) {
            result = AttemptBuild(sc2::ABILITY_ID::BUILD_PYLON);

            if (result.IsPlanning() && !result.IsSuccess()) {
                m_Resources = m_Resources - result.cost;
            }
        }
    }

    const auto& gateways = GetUnits(sc2::UNIT_TYPEID::PROTOSS_GATEWAY);
    const auto& cybernetics_cores = GetUnits(sc2::UNIT_TYPEID::PROTOSS_CYBERNETICSCORE);
    const auto& assimilators = GetUnits(sc2::UNIT_TYPEID::PROTOSS_ASSIMILATOR);

    if (!result.IsSuccess() && !AnyQueuedOrders(probes, sc2::ABILITY_ID::BUILD_GATEWAY) && !AnyInProgress(sc2::UNIT_TYPEID::PROTOSS_GATEWAY)) {
        // Only build one if we don't have a cybernetics core.
        bool should_build = (cybernetics_cores.size() == 0 && gateways.size() == 0) || cybernetics_cores.size() > 0;

        if (should_build) {
            result = AttemptBuild(sc2::ABILITY_ID::BUILD_GATEWAY);

            if (result.IsPlanning() && !result.IsSuccess()) {
                m_Resources = m_Resources - result.cost;
            }
        }
    }

    if (!result.IsSuccess() && !AnyQueuedOrders(probes, sc2::ABILITY_ID::BUILD_CYBERNETICSCORE) && !AnyInProgress(sc2::UNIT_TYPEID::PROTOSS_CYBERNETICSCORE)) {
        if (cybernetics_cores.size() == 0 && gateways.size() > 0) {
            result = AttemptBuild(sc2::ABILITY_ID::BUILD_CYBERNETICSCORE);

            if (result.IsPlanning() && !result.IsSuccess()) {
                m_Resources = m_Resources - result.cost;
            }
        }
    }

    if (!result.IsPlanning() && !AnyQueuedOrders(probes, sc2::ABILITY_ID::BUILD_ASSIMILATOR) && !AnyInProgress(sc2::UNIT_TYPEID::PROTOSS_ASSIMILATOR)) {
        bool should_build = gateways.size() > 0 && (assimilators.size() < nexus_units.size() * 2);
        int32_t build_amount = cybernetics_cores.size() == 0 ? 1 : (nexus_units.size() * 2) - assimilators.size();

        if (should_build && build_amount > 0) {
            result = AttemptBuild(sc2::ABILITY_ID::BUILD_ASSIMILATOR);

            if (result.IsPlanning() && !result.IsSuccess()) {
                m_Resources = m_Resources - result.cost;
            }
        }
    }

    if (!result.IsSuccess()) {
        for (const auto& nexus : nexus_units) {
            if (nexus->build_progress < 1.0f) {
                continue;
            }

            if (AnyQueuedOrder(nexus, sc2::ABILITY_ID::TRAIN_PROBE)) {
                continue;
            }

            // If there are less than 22 probes withing 15 units of the Nexus, build a probe.
            int32_t workers = std::count_if(probes.begin(), probes.end(), [&nexus](const sc2::Unit* probe) {
                return sc2::DistanceSquared2D(probe->pos, nexus->pos) < PROBE_RANGE_SQUARED;
            });

            // Get the number of assimilators within 15 units of the Nexus.
            const auto assimilatorCount = std::count_if(assimilators.begin(), assimilators.end(), [&nexus](const sc2::Unit* assimilator) {
                return sc2::DistanceSquared2D(assimilator->pos, nexus->pos) < PROBE_RANGE_SQUARED;
            });

            const auto idealWorkers = 16 + (3 * assimilatorCount);

            if (m_Resources.minerals >= 50 && workers < idealWorkers) {
                // Set the rally point to the closest mineral field or vespene geyser which is not saturated.
                const auto mining_points = obs->GetUnits(sc2::Unit::Alliance::Neutral, [probes](const sc2::Unit& unit_) {
                    bool is_correct_unit = 
                            unit_.unit_type == sc2::UNIT_TYPEID::NEUTRAL_MINERALFIELD ||
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

                    if (!is_correct_unit) {
                        return false;
                    }

                    // Make sure less than 2 probes are mining it (3 for gas).
                    const auto num_mining = std::count_if(probes.begin(), probes.end(), [&unit_](const sc2::Unit* probe) {
                        for (const auto& order : probe->orders) {
                            if (order.ability_id == sc2::ABILITY_ID::HARVEST_GATHER && order.target_unit_tag == unit_.tag) {
                                return true;
                            }
                        }

                        return false;
                    });

                    if (unit_.unit_type == sc2::UNIT_TYPEID::PROTOSS_ASSIMILATOR) {
                        return num_mining < 3;
                    }

                    return num_mining < 2;
                });

                const auto& closest_mining_point = std::min_element(mining_points.begin(), mining_points.end(), [&nexus](const sc2::Unit* a, const sc2::Unit* b) {
                    return sc2::DistanceSquared2D(a->pos, nexus->pos) < sc2::DistanceSquared2D(b->pos, nexus->pos);
                });

                actions->UnitCommand(nexus, sc2::ABILITY_ID::RALLY_NEXUS, *closest_mining_point);

                actions->UnitCommand(nexus, sc2::ABILITY_ID::TRAIN_PROBE);
                
                m_Resources.minerals -= 50;
            }
        }
    }

    // Expand
    if (!result.IsSuccess() && !AnyQueuedOrders(probes, sc2::ABILITY_ID::BUILD_NEXUS) && !AnyInProgress(sc2::UNIT_TYPEID::PROTOSS_NEXUS)) {
        result = AttemptBuild(sc2::ABILITY_ID::BUILD_NEXUS);

        if (result.IsPlanning() && !result.IsSuccess()) {
            m_Resources = m_Resources - result.cost;
        }
    }

    if (result.IsPlanning())
    {
        std::cout << "Planning to build (" << result.success << ") in " << result.time << " seconds at a cost of " << result.cost.minerals << " minerals and " << result.cost.vespene << " vespene" << std::endl;
    }

    // Remove delayed orders that have been executed.
    for (const auto& tag : m_OrdersExecuted) {
        const auto& delayed_order = m_DelayedOrders.find(tag);

        if (delayed_order != m_DelayedOrders.end()) {
            m_DelayedOrders.erase(delayed_order);
        }
    }
    
    m_OrdersExecuted.clear();
}

void Bot::OnUnitCreated(const sc2::Unit* unit_)
{
    std::cout << sc2::UnitTypeToName(unit_->unit_type) <<
        "(" << unit_->tag << ") was created" << std::endl;
}

void Bot::OnUnitIdle(const sc2::Unit* unit_)
{
    auto* actions = Actions();
    const auto* obs = Observation();

    const auto minerals = obs->GetMinerals();
    const auto vespene = obs->GetVespene();

    const auto& delayed_order = m_DelayedOrders.find(unit_->tag);

    if (delayed_order != m_DelayedOrders.end()) {
        m_CheckDelayedOrders.emplace(unit_->tag);

        return;
    }

    std::cout << sc2::UnitTypeToName(unit_->unit_type) <<
         "(" << unit_->tag << ") is idle" << std::endl;

    // If the unit is a probe, send it to mine minerals or gas.
    if (unit_->unit_type == sc2::UNIT_TYPEID::PROTOSS_PROBE) {
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
                    if (order.ability_id == sc2::ABILITY_ID::HARVEST_GATHER && order.target_unit_tag == mining_point->tag) {
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

            const auto distance = sc2::DistanceSquared2D(unit_->pos, mining_point->pos);

            if (distance < closest_distance) {
                closest_distance = distance;
                closest_mining_point = mining_point;
            }
        }

        if (closest_mining_point == nullptr) {
            return;
        }

        actions->UnitCommand(unit_, sc2::ABILITY_ID::HARVEST_GATHER, closest_mining_point);
    }   
}

void Bot::OnUnitDestroyed(const sc2::Unit* unit_)
{
    std::cout << sc2::UnitTypeToName(unit_->unit_type) <<
         "(" << unit_->tag << ") was destroyed" << std::endl;

    // Remove from delayed orders.
    const auto& it = m_DelayedOrders.find(unit_->tag);

    if (it != m_DelayedOrders.end()) {
        m_DelayedOrders.erase(it);
    }

    // Remove from check delayed orders.
    const auto& check_it = m_CheckDelayedOrders.find(unit_->tag);

    if (check_it != m_CheckDelayedOrders.end()) {
        m_CheckDelayedOrders.erase(check_it);
    }

    // Remove from orders executed.
    const auto& executed_it = m_OrdersExecuted.find(unit_->tag);

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

    const auto& requirementsIter = m_Requirements.find(ability_id);

    if (requirementsIter == m_Requirements.end()) {
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

            const auto& closest_expansion = std::min_element(m_Expansions.begin(), m_Expansions.end(), [&probe](const sc2::Point3D& a, const sc2::Point3D& b) {
                return sc2::DistanceSquared2D(a, probe->pos) < sc2::DistanceSquared2D(b, probe->pos);
            });

            return *closest_expansion;
        }

        // The closest expansion with the smallest average distance to all Nexus units.
        const auto& closest_expansion = std::min_element(m_Expansions.begin(), m_Expansions.end(), [&nexus_units](const sc2::Point3D& a, const sc2::Point3D& b) {
            float avg_distance_a = 0.0f;
            float avg_distance_b = 0.0f;

            for (const auto& nexus : nexus_units) {
                avg_distance_a += sc2::Distance2D(a, nexus->pos);
                avg_distance_b += sc2::Distance2D(b, nexus->pos);
            }

            avg_distance_a /= nexus_units.size();
            avg_distance_b /= nexus_units.size();

            return avg_distance_a < avg_distance_b;
        });

        return *closest_expansion;
    }

    const auto& pylons = GetUnits(sc2::UNIT_TYPEID::PROTOSS_PYLON);

    if (ability_id == sc2::ABILITY_ID::BUILD_PYLON) {
        // Build the Pylon at the closest ramp.
        const auto& nexus_units = GetUnits(sc2::UNIT_TYPEID::PROTOSS_NEXUS);

        if (nexus_units.size() == 0) {
            return sc2::Point2D(0.0f, 0.0f);
        }

        if (pylons.size() == 0) {
            const auto& closest_nexus = nexus_units[0];

            const auto& ramp = GetClosestRamp(closest_nexus->pos);

            return GetClosestPlace(ramp, closest_nexus->pos, ability_id, 5.0f, 7.0f);
        }

        // Build pylons where:
        // * there are none within 15 units of a Nexus;
        // * not within 5 units of another pylon;
        // * not within 4 units of a mineral field or vespene geyser;
        // * within range of unpowers structures (priority).
        const auto unpowered_structures = obs->GetUnits(sc2::Unit::Alliance::Self, [this](const sc2::Unit& unit_) {
            return unit_.is_powered == false && unit_.build_progress == 1.0f &&
                   m_PoweredStructures.find(unit_.unit_type) == m_PoweredStructures.end();
        });

        if (unpowered_structures.size() != 0) {
            return GetBestCenter(unpowered_structures, ability_id, 3.0f, 5.0f, 5.0f);
        }

        const auto mining_points = obs->GetUnits(sc2::Unit::Alliance::Neutral, [](const sc2::Unit& unit_) {
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

        const sc2::Unit* fewest_pylons = nullptr;
        int32_t fewest_pylons_count = std::numeric_limits<int32_t>::max();
        
        for (const auto& nexus : nexus_units) {
            const auto& closest_pylon = std::min_element(pylons.begin(), pylons.end(), [&nexus](const sc2::Unit* a, const sc2::Unit* b) {
                return sc2::DistanceSquared2D(a->pos, nexus->pos) < sc2::DistanceSquared2D(b->pos, nexus->pos);
            });

            if (sc2::DistanceSquared2D((*closest_pylon)->pos, nexus->pos) > 15.0f * 15.0f) {
                return GetClosestPlaceWhileAvoiding(nexus->pos, nexus->pos, mining_points, ability_id, 5.0f, 10.0f, 4.0f);
            }

            const auto& pylons_within_range = std::count_if(pylons.begin(), pylons.end(), [&nexus](const sc2::Unit* pylon) {
                return sc2::DistanceSquared2D(pylon->pos, nexus->pos) < 15.0f * 15.0f;
            });

            if (pylons_within_range < fewest_pylons_count) {
                fewest_pylons = *closest_pylon;
                fewest_pylons_count = pylons_within_range;
            }
        }

        // Place while avoiding both pylons and mining points.
        sc2::Units avoid;

        for (const auto& pylon : pylons) {
            avoid.push_back(pylon);
        }

        for (const auto& mining_point : mining_points) {
            avoid.push_back(mining_point);
        }

        return GetClosestPlaceWhileAvoiding(fewest_pylons->pos, fewest_pylons->pos, avoid, ability_id, 5.0f, 10.0f, 5.0f);
    }

    if (ability_id == sc2::ABILITY_ID::BUILD_ASSIMILATOR) {
        // Build the Assimilator at the closest vespene geyser.
        const auto nexus_units = FilterOutInProgress(GetUnits(sc2::UNIT_TYPEID::PROTOSS_NEXUS));

        if (nexus_units.size() == 0) {
            return sc2::Point2D(0.0f, 0.0f);
        }

        const auto& assimilators = GetUnits(sc2::UNIT_TYPEID::PROTOSS_ASSIMILATOR);
        
        // Select a Nexus that has less than 2 assimilators within 15 units of it.
        const auto& selected_nexus = std::min_element(nexus_units.begin(), nexus_units.end(), [&requirements, &assimilators](const sc2::Unit* a, const sc2::Unit* b) {
            const auto& count = std::count_if(assimilators.begin(), assimilators.end(), [&a](const sc2::Unit* assimilator) {
                return sc2::DistanceSquared2D(assimilator->pos, a->pos) < 15.0f * 15.0f;
            });

            return count < 2;
        });

        const auto vespene_geysers = Observation()->GetUnits(sc2::Unit::Alliance::Neutral, [](const sc2::Unit& unit_) {
            return  unit_.unit_type == sc2::UNIT_TYPEID::NEUTRAL_VESPENEGEYSER ||
                    unit_.unit_type == sc2::UNIT_TYPEID::NEUTRAL_PROTOSSVESPENEGEYSER ||
                    unit_.unit_type == sc2::UNIT_TYPEID::NEUTRAL_PURIFIERVESPENEGEYSER ||
                    unit_.unit_type == sc2::UNIT_TYPEID::NEUTRAL_SHAKURASVESPENEGEYSER ||
                    unit_.unit_type == sc2::UNIT_TYPEID::NEUTRAL_RICHVESPENEGEYSER;
        });

        const auto& closest_vespene_geyser = std::min_element(vespene_geysers.begin(), vespene_geysers.end(), [&selected_nexus](const sc2::Unit* a, const sc2::Unit* b) {
            return sc2::DistanceSquared2D(a->pos, (*selected_nexus)->pos) < sc2::DistanceSquared2D(b->pos, (*selected_nexus)->pos);
        });

        if (closest_vespene_geyser == vespene_geysers.end()) {
            return sc2::Point2D(0.0f, 0.0f);
        }

        return (*closest_vespene_geyser)->pos;
    }

    bool all_in_progress = std::all_of(pylons.begin(), pylons.end(), [](const sc2::Unit* pylon) {
        return pylon->build_progress < 1.0f;
    });

    if (ability_id == sc2::ABILITY_ID::BUILD_GATEWAY) {
        // Build the Gateway at the closest ramp.
        const auto& nexus_units = GetUnits(sc2::UNIT_TYPEID::PROTOSS_NEXUS);

        if (nexus_units.size() == 0) {
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
            return GetClosestPlace(ramp, ramp, sc2::ABILITY_ID::BUILD_PYLON, 0.0f, 8.0f);
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
            return GetClosestPlace(ramp, ramp, sc2::ABILITY_ID::BUILD_PYLON, 0.0f, 8.0f);
        }

        return GetClosestPlace(ramp, ramp, pylons, ability_id, 0.0f, 8.0f);
    }

    return sc2::Point2D(0.0f, 0.0f);
}

Bot::AbilityCost Bot::GetPlannedCosts()
{
    Bot::AbilityCost planned_cost = {0, 0};

    for (const auto& order : m_DelayedOrders) {
        const auto& ability_cost = m_AbilityCosts.find(order.second.ability_id);

        if (ability_cost != m_AbilityCosts.end()) {
            planned_cost = planned_cost + ability_cost->second;
        }
    }

    return planned_cost;
}

void Bot::CheckDelayedOrder(const sc2::Unit *unit_)
{
    auto* actions = Actions();
    const auto* obs = Observation();

    const auto minerals = obs->GetMinerals();
    const auto vespene = obs->GetVespene();

    // Check if the unit has a delayed order.
    const auto& it = m_DelayedOrders.find(unit_->tag);

    if (it != m_DelayedOrders.end()) {
        if (m_OrdersExecuted.find(unit_->tag) != m_OrdersExecuted.end()) {
            m_CheckDelayedOrders.erase(unit_->tag);
            return;
        }

        const auto& delayed_order = it->second;

        const auto& ability_cost = m_AbilityCosts.find(delayed_order.ability_id);

        if (ability_cost != m_AbilityCosts.end()) {
            if (minerals < ability_cost->second.minerals || vespene < ability_cost->second.vespene) {
                m_CheckDelayedOrders.emplace(unit_->tag);
                return;
            }
        }

        const auto& requirements = m_Requirements.find(delayed_order.ability_id);

        if (requirements != m_Requirements.end()) {
            bool all_requirements_met = true;
            for (const auto& requirement : requirements->second) {
                bool any_unit_found = false;

                const auto& units = GetUnits(requirement);
                for (const auto& unit : units) {
                    if (unit->build_progress == 1.0f) {
                        any_unit_found = true;
                        break;
                    }
                }

                if (any_unit_found) {
                    continue;
                }
                
                all_requirements_met = false;

                break;
            }

            if (!all_requirements_met) {
                m_CheckDelayedOrders.emplace(unit_->tag);
                return;
            }
        }

        if (delayed_order.target_unit_tag == 0) {
            actions->UnitCommand(unit_, delayed_order.ability_id, delayed_order.position);
        } else {
            actions->UnitCommand(unit_, delayed_order.ability_id, delayed_order.target_unit_tag);
        }

        std::cout << "Unit " << sc2::UnitTypeToName(unit_->unit_type) <<
            "(" << unit_->tag << ") executing delayed order" << std::endl;

        m_OrdersExecuted.emplace(unit_->tag);

        m_CheckDelayedOrders.erase(unit_->tag);

        return;
    }
}

bool Bot::AnyWithingRange(const sc2::Units &units, const sc2::Point2D &point, float range)
{
    range *= range;

    return std::any_of(units.begin(), units.end(), [&point, range](const sc2::Unit* unit) {
        return sc2::DistanceSquared2D(unit->pos, point) < range;
    });
}

sc2::Units Bot::WithinRange(const sc2::Units &units, const sc2::Point2D &point, float range)
{
    range *= range;

    sc2::Units within_range;

    std::copy_if(units.begin(), units.end(), std::back_inserter(within_range), [&point, range](const sc2::Unit* unit) {
        return sc2::DistanceSquared2D(unit->pos, point) < range;
    });

    return within_range;
}

void Bot::UpdateStepData()
{
    m_Units.clear();

    // Get all friendly units.
    const auto units = Observation()->GetUnits(sc2::Unit::Alliance::Self);

    for (const auto& unit : units) {
        const auto& type = unit->unit_type;

        const auto& iter = m_Units.find(type);

        if (iter == m_Units.end()) {
            m_Units.emplace(type, sc2::Units{unit});
        } else {
            iter->second.push_back(unit);
        }
    }
}

const sc2::Units &Bot::GetUnits(sc2::UNIT_TYPEID unit_type_) const
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

sc2::Point2D Bot::GetClosestPlaceWhileAvoiding(const sc2::Point2D &center, const sc2::Point2D &pivot, const sc2::Units &avoid, sc2::ABILITY_ID ability_id, float min_radius, float max_radius, float avoid_radius, float step_size)
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
    auto closest_distance = std::numeric_limits<float>::max();
    bool first = true;
    bool any_with_avoided = false;

    for (auto i = 0; i < result.size(); ++i) {
        if (!result[i]) {
            continue;
        }

        const auto& point = queries[i].target_pos;

        bool unwanted = AnyWithingRange(avoid, point, avoid_radius);

        if (any_with_avoided && unwanted) {
            continue;
        }

        if (!unwanted) {
            any_with_avoided = true;
        }

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

bool Bot::AnyQueuedOrders(const sc2::Units &units, sc2::ABILITY_ID ability_id)
{
    for (const auto& unit : units) {
        // Check delayed orders.
        const auto& it = m_DelayedOrders.find(unit->tag);

        if (it != m_DelayedOrders.end() && it->second.ability_id == ability_id) {
            return true;
        }

        if (unit->orders.size() == 0) {
            continue;
        }
        
        for (const auto& order : unit->orders) {
            if (order.ability_id == ability_id) {
                return true;
            }
        }
    }

    return false;
}

bool Bot::AnyQueuedOrder(const sc2::Unit* unit, sc2::ABILITY_ID ability_id)
{
    for (const auto& order : unit->orders) {
        if (order.ability_id == ability_id) {
            return true;
        }
    }

    return false;
}

bool Bot::AnyInProgress(const sc2::UNIT_TYPEID &unit_id)
{
    const auto& units = GetUnits(unit_id);

    return std::any_of(units.begin(), units.end(), [](const sc2::Unit* unit) {
        return unit->build_progress < 1.0f;
    });
}

sc2::Units Bot::FilterOutInProgress(const sc2::Units &units)
{
    sc2::Units filtered;

    for (const auto& unit : units) {
        if (unit->build_progress == 1.0f) {
            filtered.push_back(unit);
        }
    }

    return filtered;
}

bool Bot::IsInProgress(const sc2::Unit *unit)
{
    return unit->build_progress < 1.0f;
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

    // Remove probes that are not gathering resources, excluding idle probes.
    /*probes.erase(std::remove_if(probes.begin(), probes.end(), [this](const sc2::Unit* probe) {
        for (const auto& order : probe->orders) {
            if (m_MiningAbilities.find(order.ability_id) != m_MiningAbilities.end()) {
                return false;
            }

            // If the probe is targeting a mineral field or vespene geyser, it's gathering resources.
            if (points.size() == 0) {
                continue;
            }

            if (std::any_of(points.begin(), points.end(), [&order](const sc2::Unit* point) {
                return order.target_unit_tag == point->tag;
            })) {
                return false;
            }
        }
        
        if (probe->orders.size() == 0) {
            return false;
        }

        return true;
    }), probes.end());*/

    // If we have 16 workers, 2 should be on vespene geysers. If we have more than 16 workers, 3 should be on vespene geysers.
    // If we have more than 19 workers, 16 should be on minerals and the rest on vespene geysers.
    // If we have more than 22 workers, return the excess workers.
    // If we have less than 14 workers, all workers should be on minerals.
    // If we have less than 22 workers, return the number of workers needed.
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

void Bot::RedistributeWorkers(const sc2::Units &bases)
{
    std::unordered_map<const sc2::Unit*, int32_t> workers_needed_map;
    std::unordered_map<const sc2::Unit*, sc2::Units> excess_workers_map;

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

Bot::BuildResult Bot::AttemptBuild(sc2::ABILITY_ID ability_id)
{
    // Get the closest probe to the ideal position.
    const auto& probes = GetUnits(sc2::UNIT_TYPEID::PROTOSS_PROBE);

    const auto* obs = Observation();

    const auto& requirements = m_Requirements.find(ability_id);

    if (requirements == m_Requirements.end()) {
        return Bot::BuildResult(false);
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
                
                const auto build_time = obs->GetUnitTypeData().at(unit->unit_type).build_time / 22.4f;

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
        return Bot::BuildResult(false);
    }

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

        if (order.ability_id == sc2::ABILITY_ID::HARVEST_GATHER) {
            subset.push_back(probe);
            
            const auto target = obs->GetUnit(order.target_unit_tag);

            if (target != nullptr && target->unit_type == sc2::UNIT_TYPEID::PROTOSS_ASSIMILATOR) {
                num_gas_probes++;
            } else {
                num_mineral_probes++;
            }
        }
    }

    const auto& cost_iter = m_AbilityCosts.find(ability_id);

    if (cost_iter == m_AbilityCosts.end()) {
        return Bot::BuildResult(false);
    }

    const auto& cost = cost_iter->second;

    if (m_Resources < cost) {
        // Assume each probe can gather 1 mineral per second.
        const float mineral_rate = num_mineral_probes * 0.9f;
        const float vespene_rate = num_gas_probes * 0.9f;

        const float mineral_time = (cost.minerals - m_Resources.minerals) / mineral_rate;
        const float vespene_time = (cost.vespene - m_Resources.vespene) / vespene_rate;

        const auto max_time = std::max(mineral_time, vespene_time);

        approx_time_left_on_requirements = std::max(approx_time_left_on_requirements, max_time);
    }

    // Get the ideal position to build the structure.
    const auto ideal_position = GetIdealPosition(ability_id);

    if (ideal_position.x == 0.0f && ideal_position.y == 0.0f) {
        std::cout << "No ideal position found for " << sc2::AbilityTypeToName(ability_id) << std::endl;
        return Bot::BuildResult(false);
    }

    if (probes.size() == 0) {
        return Bot::BuildResult(false);
    }

    const auto& probe_iter = std::min_element(subset.begin(), subset.end(), [&ideal_position](const sc2::Unit* a, const sc2::Unit* b) {
        return sc2::DistanceSquared2D(a->pos, ideal_position) < sc2::DistanceSquared2D(b->pos, ideal_position);
    });

    if (probe_iter == subset.end()) {
        return Bot::BuildResult(false);
    }

    const auto& probe = *probe_iter;

    auto* query = Query();

    sc2::QueryInterface::PathingQuery pathing_query;
    pathing_query.start_ = probe->pos;
    pathing_query.end_ = ideal_position;
    pathing_query.start_unit_tag_ = probe->tag;

    const auto result = query->PathingDistance({pathing_query});

    if (result.size() == 0) {
        return Bot::BuildResult(false);
    }

    const auto& pathing_result = result[0];

    // Get the unit movement speed.
    const auto& unit_data = obs->GetUnitTypeData().at(probe->unit_type);

    const auto& movement_speed = unit_data.movement_speed;

    // Calculate the time it will take to reach the ideal position.
    const auto& distance = sc2::Distance2D(probe->pos, ideal_position);

    const auto& time_to_reach = distance / movement_speed;

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
            return Bot::BuildResult(false);
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

        return Bot::BuildResult(true, time_to_reach, cost);
    }

    // If the time to reach the ideal position is greater than the time left on the requirements, return.
    if (approx_time_left_on_requirements > time_to_reach) {
        return Bot::BuildResult(false, approx_time_left_on_requirements, cost);
    }

    // Move the probe to the ideal position.
    Actions()->UnitCommand(probe, sc2::ABILITY_ID::MOVE_MOVE, ideal_position);

    // Delay the order.
    m_DelayedOrders.emplace(probe->tag, DelayedOrder{ability_id, ideal_position, target != nullptr ? target->tag : 0});

    return Bot::BuildResult(true, time_to_reach, cost);
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
