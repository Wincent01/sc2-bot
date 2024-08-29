#include "Production.h"

#include <sc2api/sc2_unit.h>
#include <sc2api/sc2_agent.h>
#include <sc2api/sc2_interfaces.h>

#include "Data.h"
#include "Collective.h"
#include "Utilities.h"
#include "Economy.h"
#include "Proletariat.h"
#include "Map.h"

scbot::Production::Production(std::shared_ptr<Collective> collective)
{
    m_Collective = collective;
}

std::optional<float> scbot::Production::TimeLeftForUnitRequirements(sc2::ABILITY_ID ability_id)
{
    const auto& requirements = scdata::AbilityRequirements.find(ability_id);

    if (requirements == scdata::AbilityRequirements.end()) {
        return 0.0f;
    }

    const auto& required_units = requirements->second;

    const auto& unit_data = m_Collective->Observation()->GetUnitTypeData();

    float time_left = 0.0f;

    for (const auto& unit_type : required_units) {
        const auto& units = m_Collective->GetAlliedUnitsOfType(unit_type);

        // This requirement has not even started
        if (units.empty()) {
            return std::nullopt;
        }

        if (!Utilities::AllInProgress(units)) {
            continue;
        }
        
        for (const auto& unit : units) {
            // Find the unit that has the shortest time left
            const auto build_time = scbot::Utilities::ToSecondsFromGameTime(
                unit_data.at(unit->unit_type).build_time
            );

            const auto remaining_time = (1.0f - unit->build_progress) * build_time;

            if (remaining_time > time_left) {
                time_left = remaining_time;
            }
        }
    }

    return time_left;
}

std::optional<float> scbot::Production::TimeLeftForEconomicRequirements(const Proletariat& proletariat, const Economy& economy, const scdata::ResourcePair& offset, sc2::ABILITY_ID ability_id)
{
    const auto& cost_iter = scdata::AbilityCosts.find(ability_id);

    if (cost_iter == scdata::AbilityCosts.end()) {
        return 0.0f;
    }

    const auto& cost = cost_iter->second;

    auto resources = economy.GetResources();

    resources -= offset;

    const auto minerals_needed = cost.minerals - resources.minerals;
    const auto vespene_needed = cost.vespene - resources.vespene;

    if (minerals_needed <= 0 && vespene_needed <= 0) {
        return 0.0f;
    }

    const auto& [income_mineral, income_vespene] = proletariat.GetIncomePerSecond();

    if (minerals_needed > 0 && income_mineral <= 0 || vespene_needed > 0 && income_vespene <= 0) {
        return std::nullopt;
    }

    // Avoid division by zero
    float time_left = 0.0f;

    if (minerals_needed > 0) {
        time_left = minerals_needed / income_mineral;
    }

    if (vespene_needed > 0) {
        const auto time_left_vespene = vespene_needed / income_vespene;

        if (time_left_vespene > time_left) {
            time_left = time_left_vespene;
        }
    }

    return time_left;
}

std::optional<sc2::Point2D> scbot::Production::IdealPositionForBuilding(sc2::ABILITY_ID ability_id)
{
    switch (ability_id) {
    case sc2::ABILITY_ID::BUILD_NEXUS:
        return IdealPositionForNexus();
    case sc2::ABILITY_ID::BUILD_PYLON:
        return IdealPositionForPylon();
    case sc2::ABILITY_ID::BUILD_GATEWAY:
        return IdealPositionForGateway();
    case sc2::ABILITY_ID::BUILD_ASSIMILATOR:
        return IdealPositionForAssimilator();
    case sc2::ABILITY_ID::BUILD_CYBERNETICSCORE:
        return IdealPositionForCyberneticsCore();
    case sc2::ABILITY_ID::BUILD_STARGATE:
        return IdealPositionForArbitrary2X2Building();
    case sc2::ABILITY_ID::BUILD_FORGE:
        return IdealPositionForArbitrary2X2Building();
    case sc2::ABILITY_ID::BUILD_ROBOTICSFACILITY:
        return IdealPositionForArbitrary2X2Building();
    case sc2::ABILITY_ID::BUILD_TWILIGHTCOUNCIL:
        return IdealPositionForArbitrary2X2Building();
    case sc2::ABILITY_ID::BUILD_DARKSHRINE:
        return IdealPositionForArbitrary2X2Building();
    case sc2::ABILITY_ID::BUILD_TEMPLARARCHIVE:
        return IdealPositionForArbitrary2X2Building();
    case sc2::ABILITY_ID::BUILD_ROBOTICSBAY:
        return IdealPositionForArbitrary2X2Building();
    case sc2::ABILITY_ID::BUILD_FLEETBEACON:
        return IdealPositionForArbitrary2X2Building();
    case sc2::ABILITY_ID::BUILD_PHOTONCANNON:
        return IdealPositionForArbitrary2X2Building();
    case sc2::ABILITY_ID::BUILD_SHIELDBATTERY:
        return IdealPositionForArbitrary2X2Building();
    default:
        return std::nullopt;
    }
}

std::optional<const sc2::Unit*> scbot::Production::IdealUnitForProduction(sc2::ABILITY_ID ability_id)
{
    const auto& training_building_it = scdata::AssociatedBuilding.find(ability_id);

    if (training_building_it == scdata::AssociatedBuilding.end()) {
        return std::nullopt;
    }

    const auto& training_building_type = training_building_it->second;

    const auto& buildings = m_Collective->GetAlliedUnitsOfType(training_building_type);

    if (buildings.empty()) {
        return std::nullopt;
    }

    const auto complete = scbot::Utilities::FilterOutInProgress(buildings);

    if (complete.empty()) {
        return std::nullopt;
    }

    const auto* least_busy = scbot::Utilities::LeastBusy(complete);

    return least_busy;
}

std::optional<scbot::Production::AbilityRequirementResult> scbot::Production::GetAbilityRequirements(const Proletariat& proletariat, const Economy& economy, const scdata::ResourcePair& offset, sc2::ABILITY_ID ability_id)
{
    const auto& requirements = scdata::AbilityRequirements.find(ability_id);

    if (requirements == scdata::AbilityRequirements.end()) {
        return std::nullopt;
    }

    const auto& required_units = requirements->second;

    std::vector<sc2::UNIT_TYPEID> missing_units;

    for (const auto& unit_type : required_units) {
        const auto& units = m_Collective->GetAlliedUnitsOfType(unit_type);

        if (units.empty()) {
            missing_units.push_back(unit_type);
        }
    }

    const auto& cost_iter = scdata::AbilityCosts.find(ability_id);

    if (cost_iter == scdata::AbilityCosts.end()) {
        return std::nullopt;
    }

    const auto& cost = cost_iter->second;

    auto resources = economy.GetResources();

    resources -= offset;

    const auto minerals_needed = cost.minerals - resources.minerals;
    const auto vespene_needed = cost.vespene - resources.vespene;

    return AbilityRequirementResult{missing_units, {
        std::max(0, minerals_needed),
        std::max(0, vespene_needed)
    }};
}

std::optional<const sc2::Unit*> scbot::Production::MoveProbeToPosition(Proletariat& proletariat, const sc2::Point2D& position, float distance, float max_time)
{
    const auto* probe = proletariat.GetWorkerForBuilding(position);

    if (probe == nullptr) {
        return std::nullopt;
    }

    const auto& [movePosition, moveDistance] = scbot::Map::GetBestPath(
        m_Collective->Query(),
        probe,
        position,
        distance,
        distance + 1.0f
    );

    if (movePosition.x == 0.0f && movePosition.y == 0.0f) {
        return probe;
    }
    
    const auto& unit_data = m_Collective->Observation()->GetUnitTypeData();

    const auto& movement_speed = unit_data.at(probe->unit_type).movement_speed;

    const auto time_to_move = moveDistance / movement_speed;

    if (time_to_move < max_time) {
        return std::nullopt;
    }

    auto* actions = m_Collective->Actions();

    actions->UnitCommand(probe, sc2::ABILITY_ID::MOVE_MOVE, movePosition);

    return probe;
}

bool scbot::Production::MoveProbeToPosition(const sc2::Unit* probe, const sc2::Point2D& position, float distance, float max_time)
{
    NON_NULL(probe);

    const auto& [movePosition, moveDistance] = scbot::Map::GetBestPath(
        m_Collective->Query(),
        probe,
        position,
        distance,
        distance + 1.0f
    );

    if (movePosition.x == 0.0f && movePosition.y == 0.0f) {
        return true;
    }

    const auto& unit_data = m_Collective->Observation()->GetUnitTypeData();

    const auto& movement_speed = unit_data.at(probe->unit_type).movement_speed;

    const auto time_to_move = moveDistance / movement_speed;

    if (time_to_move < max_time) {
        return false;
    }

    auto* actions = m_Collective->Actions();

    actions->UnitCommand(probe, sc2::ABILITY_ID::MOVE_MOVE, movePosition);

    return true;
}

void scbot::Production::BuildBuilding(const sc2::Unit *probe, sc2::ABILITY_ID ability_id, const sc2::Point2D &position)
{
    NON_NULL(probe);

    auto* actions = m_Collective->Actions();

    if (ability_id == sc2::ABILITY_ID::BUILD_ASSIMILATOR) {
        const auto& vespene_geysers = scbot::Utilities::GetResourcePoints(m_Collective->GetNeutralUnits(), false, true, false);

        const auto* closest_vespene_geyser = scbot::Utilities::ClosestTo(vespene_geysers, position);

        actions->UnitCommand(probe, ability_id, closest_vespene_geyser);
        return;
    }

    actions->UnitCommand(probe, ability_id, position);
}

void scbot::Production::OnStep()
{
}

scbot::Production::~Production()
{
}

std::optional<sc2::Point2D> scbot::Production::IdealPositionForNexus()
{
    const auto& expansions = m_Collective->GetExpansions();

    if (expansions.empty()) {
        return std::nullopt;
    }

    const auto& nexuses = m_Collective->GetAlliedUnitsOfType(sc2::UNIT_TYPEID::PROTOSS_NEXUS);

    if (!nexuses.empty()) {
        return scbot::Utilities::ClosestAverageTo(expansions, nexuses);
    }

    const auto& probes = m_Collective->GetAlliedUnitsOfType(sc2::UNIT_TYPEID::PROTOSS_PROBE);

    if (probes.empty()) {
        return std::nullopt;
    }

    return scbot::Utilities::ClosestAverageTo(expansions, probes);    
}

std::optional<sc2::Point2D> scbot::Production::IdealPositionForPylon()
{
    const auto& nexuses = m_Collective->GetAlliedUnitsOfType(sc2::UNIT_TYPEID::PROTOSS_NEXUS);

    if (nexuses.empty()) {
        const auto& probes = m_Collective->GetAlliedUnitsOfType(sc2::UNIT_TYPEID::PROTOSS_PROBE);

        if (probes.empty()) {
            return std::nullopt;
        }
        
        for (const auto& probe : probes) {
            const auto result = Map::GetClosestPlace(
                m_Collective->Query(),
                probe->pos,
                probe->pos,
                sc2::ABILITY_ID::BUILD_PYLON,
                0.0f,
                8.0f
            );

            if (result.x != 0.0f && result.y != 0.0f) {
                return result;
            }
        }

        return std::nullopt;
    }

    const auto& pylons = m_Collective->GetAlliedUnitsOfType(sc2::UNIT_TYPEID::PROTOSS_PYLON);

    if (pylons.empty()) {
        const auto& closest_nexus = scbot::Utilities::ClosestTo(nexuses, m_Collective->GetBot()->Observation()->GetStartLocation());

        const auto& closest_ramp = m_Collective->GetClosestRamp(closest_nexus->pos);

        auto result = scbot::Map::GetClosestPlace(
            m_Collective->Query(),
            closest_ramp,
            closest_nexus->pos,
            sc2::ABILITY_ID::BUILD_PYLON,
            3.0f,
            6.0f
        );

        if (result.x == 0.0f && result.y == 0.0f) {
            result = scbot::Map::GetClosestPlace(
                m_Collective->Query(),
                closest_ramp,
                closest_nexus->pos,
                sc2::ABILITY_ID::BUILD_PYLON,
                2.0f,
                3.0f
            );

            if (result.x != 0.0f && result.y != 0.0f) {
                return result;
            }
        }
        else {
            return result;
        }
    }

    const auto unpowered_structures = scbot::Utilities::FilterUnits(m_Collective->GetAlliedUnits(), [this](const sc2::Unit* unit) {
        return scbot::Utilities::RequiresPower(unit) && !scbot::Utilities::IsPowered(unit);
    });

    if (!unpowered_structures.empty()) {
        const auto result = Map::GetBestCenter(
            m_Collective->Query(),
            unpowered_structures,
            sc2::ABILITY_ID::BUILD_PYLON,
            3.0f,
            5.0f,
            5.0f
        );

        if (result.x != 0.0f && result.y != 0.0f) {
            return result;
        }
    }

    const auto mining_points = scbot::Utilities::GetResourcePoints(m_Collective->GetAlliedUnits(), true, true, true);

    const sc2::Unit* fewest_pylons = scbot::Utilities::SelectUnitMin(nexuses, [this, &pylons](const sc2::Unit* nexus) {
        return static_cast<float>(scbot::Utilities::CountWithinRange(pylons, nexus->pos, 15.0f));
    });

    sc2::Units avoid = Utilities::Union(pylons, mining_points);

    const auto result = Map::GetClosestPlaceWhileAvoiding(
        m_Collective->Query(),
        fewest_pylons->pos,
        fewest_pylons->pos,
        avoid,
        sc2::ABILITY_ID::BUILD_PYLON,
        5.0f,
        10.0f,
        6.0f,
        true
    );

    if (result.x != 0.0f && result.y != 0.0f) {
        return result;
    }

    return std::nullopt;
}

std::optional<sc2::Point2D> scbot::Production::IdealPositionForGateway()
{
    return IdealPositionForArbitrary2X2Building();
}

std::optional<sc2::Point2D> scbot::Production::IdealPositionForAssimilator()
{
    const auto& nexuses = m_Collective->GetAlliedUnitsOfType(sc2::UNIT_TYPEID::PROTOSS_NEXUS);

    if (nexuses.empty()) {
        return std::nullopt;
    }

    const auto& assimilators = m_Collective->GetAlliedUnitsOfType(sc2::UNIT_TYPEID::PROTOSS_ASSIMILATOR);

    const sc2::Unit* selected_nexus = Utilities::SelectUnitMin(nexuses, [this, &assimilators](const sc2::Unit* nexus) {
        return static_cast<float>(Utilities::CountWithinRange(assimilators, nexus->pos, 15.0f));
    });

    const auto vespene_geysers = scbot::Utilities::GetResourcePoints(m_Collective->GetNeutralUnits(), false, true, false);

    if (vespene_geysers.empty()) {
        return std::nullopt;
    }

    for (const auto& vespene_geyser : vespene_geysers) {
        // Check if there already is a assimilator on this vespene geyser
        bool already_has_assimilator = false;
        for (const auto& assimilator : assimilators) {
            if (sc2::DistanceSquared2D(assimilator->pos, vespene_geyser->pos) < 1.0f) {
                already_has_assimilator = true;
                break;
            }
        }

        if (already_has_assimilator) {
            continue;
        }
        
        return vespene_geyser->pos;
    }

    return std::nullopt;
}

std::optional<sc2::Point2D> scbot::Production::IdealPositionForCyberneticsCore()
{
    return IdealPositionForArbitrary2X2Building();
}

std::optional<sc2::Point2D> scbot::Production::IdealPositionForArbitrary2X2Building()
{
    const auto& nexuses = m_Collective->GetAlliedUnitsOfType(sc2::UNIT_TYPEID::PROTOSS_NEXUS);

    if (nexuses.empty()) {
        return std::nullopt;
    }

    const auto& gateways = m_Collective->GetAlliedUnitsOfType(sc2::UNIT_TYPEID::PROTOSS_GATEWAY);
    const auto& cybernetics_cores = m_Collective->GetAlliedUnitsOfType(sc2::UNIT_TYPEID::PROTOSS_CYBERNETICSCORE);
    const auto& pylons = m_Collective->GetAlliedUnitsOfType(sc2::UNIT_TYPEID::PROTOSS_PYLON);

    if (gateways.empty() || cybernetics_cores.empty()) {
        const auto& closest_nexus = nexuses.front();

        const auto closest_ramp = m_Collective->GetClosestRamp(closest_nexus->pos);

        if (Utilities::AnyWithinRange(pylons, closest_ramp, 5.0f)) {
            auto result = Map::GetClosestPlace(
                m_Collective->Query(),
                closest_ramp,
                closest_ramp,
                pylons,
                sc2::ABILITY_ID::BUILD_BARRACKS,
                0.0f,
                8.0f
            );

            if (result.x != 0.0f && result.y != 0.0f) {
                return result;
            }
        }
    }

    const auto sorted_pylons = Utilities::SortByAverageDistance(pylons, nexuses);

    for (const auto& pylon : sorted_pylons) {
        const auto& closest_nexus = Utilities::ClosestTo(nexuses, pylon->pos);

        const auto result = Map::GetClosestPlace(
            m_Collective->Query(),
            pylon->pos,
            closest_nexus->pos,
            pylons,
            sc2::ABILITY_ID::BUILD_BARRACKS,
            2.0f,
            8.0f
        );

        if (result.x != 0.0f && result.y != 0.0f) {
            return result;
        }
    }

    return std::nullopt;
}
