#pragma once

#include <memory>
#include <optional>

#include <sc2api/sc2_common.h>
#include <sc2api/sc2_unit.h>

#include "Data.h"

namespace scbot
{

class Collective;
class Proletariat;
class Economy;

/**
 * @brief Manager for the production of units and buildings.
 */
class Production
{
public:
    struct AbilityRequirementResult
    {
        std::vector<sc2::UNIT_TYPEID> unit_types;
        scdata::ResourcePair resources;
    };

    /**
     * @brief Construct a new Production object
     * 
     * @param collective The bot instance
     */
    Production(std::shared_ptr<Collective> collective);

    /**
     * @brief Calculate how much time there is until the unit requirements for an ability are met.
     * 
     * @param ability_id The ability id
     * @return The time left in seconds, or std::nullopt if any requirement has not started
     */
    std::optional<float> TimeLeftForUnitRequirements(sc2::ABILITY_ID ability_id);

    /**
     * @brief Calculate how much time there is until the economic requirements for an ability are met.
     * 
     * @param proletariat The proletariat
     * @param economy The economy
     * @param offset The resource offset (e.g. planned expenses)
     * @param ability_id The ability id
     * @return The time left in seconds, or std::nullopt the requirements are not met and there is no income
     */
    std::optional<float> TimeLeftForEconomicRequirements(const Proletariat& proletariat, const Economy& economy, const scdata::ResourcePair& offset, sc2::ABILITY_ID ability_id);

    /**
     * @brief Calculate the ideal position for a building.
     * 
     * @param ability_id The ability id
     * @return The position, or std::nullopt if no position is found
     */
    std::optional<sc2::Point2D> IdealPositionForBuilding(sc2::ABILITY_ID ability_id);

    /**
     * @brief Get the ideal unit to produce a new unit.
     * 
     * @param ability_id The ability id
     * @return The unit, or std::nullopt if no unit is found
     */
    std::optional<const sc2::Unit*> IdealUnitForProduction(sc2::ABILITY_ID ability_id);

    /**
     * @brief Get the requirements for an ability to be met.
     * 
     * @param proletariat The proletariat
     * @param economy The economy
     * @param offset The resource offset (e.g. planned expenses)
     * @param ability_id The ability id
     * @return The requirements, or std::nullopt if something went wrong
     */
    std::optional<AbilityRequirementResult> GetAbilityRequirements(const Proletariat& proletariat, const Economy& economy, const scdata::ResourcePair& offset, sc2::ABILITY_ID ability_id);

    /**
     * @brief Move a probe to a position for building.
     * 
     * @param proletariat The proletariat
     * @param position The position
     * @param distance The distance to keep from the position
     * @param max_time The maximum time to spend moving the probe
     * @return The probe, or std::nullopt if no probe is found or the probe needs to move too far
     */
    std::optional<const sc2::Unit*> MoveProbeToPosition(Proletariat& proletariat, const sc2::Point2D& position, float distance, float max_time);

    /**
     * @brief Move a probe to a position for building.
     * 
     * @param probe The probe
     * @param position The position
     * @param distance The distance to keep from the position
     * @param max_time The maximum time to spend moving the probe
     * @return true if the probe is at the position, false otherwise
     */
    bool MoveProbeToPosition(const sc2::Unit* probe, const sc2::Point2D& position, float distance, float max_time);

    /**
     * @brief Build a building.
     * 
     * @param probe The probe
     * @param ability_id The ability id
     * @param position The position
     */
    void BuildBuilding(const sc2::Unit* probe, sc2::ABILITY_ID ability_id, const sc2::Point2D& position);

    /**
     * @brief Method to call every step.
     */
    void OnStep();

    /**
     * @brief Destroy the Production object
     */
    ~Production();

private:
    std::optional<sc2::Point2D> IdealPositionForNexus();
    std::optional<sc2::Point2D> IdealPositionForPylon();
    std::optional<sc2::Point2D> IdealPositionForGateway();
    std::optional<sc2::Point2D> IdealPositionForAssimilator();
    std::optional<sc2::Point2D> IdealPositionForCyberneticsCore();

    std::optional<sc2::Point2D> IdealPositionForArbitrary2X2Building();

    std::shared_ptr<Collective> m_Collective;
};

}