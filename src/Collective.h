#pragma once

#include <sc2api/sc2_agent.h>
#include <sc2api/sc2_unit.h>

#include <unordered_map>

#include "config.h"

namespace scbot
{

/**
 * @brief Common data and methods that can be shared between different parts of the bot.
 */
class Collective
{
public:
    /**
     * @brief Construct a new Collective object from an Agent instance.
     * 
     * @param bot The bot instance
     */
    Collective(sc2::Agent* bot);

    /**
     * @brief Destroy the Collective object
     */
    ~Collective();

    /**
     * @brief Get the bot instance.
     * 
     * @return The bot instance
     */
    sc2::Agent* GetBot();

    /**
     * @brief Get the bot instance.
     * 
     * @return The bot instance
     */
    const sc2::Agent* GetBot() const;

    /**
     * @brief Get all allied units of a specific type.
     * 
     * @param type The type of the units
     * @return A collection of units
     */
    const sc2::Units& GetAlliedUnitsOfType(sc2::UNIT_TYPEID type) const;

    /**
     * @brief Get all enemy units of a specific type.
     * 
     * @param type The type of the units
     * @return A collection of units
     */
    const sc2::Units& GetEnemyUnitsOfType(sc2::UNIT_TYPEID type) const;

    /**
     * @brief Get all neutral units of a specific type.
     * 
     * @param type The type of the units
     * @return A collection of units
     */
    const sc2::Units& GetNeutralUnitsOfType(sc2::UNIT_TYPEID type) const;

    /**
     * @brief Get all allied units.
     * 
     * @return A collection of units
     */
    const sc2::Units& GetAlliedUnits() const;

    /**
     * @brief Get all enemy units.
     * 
     * @return A collection of units
     */
    const sc2::Units& GetEnemyUnits() const;

    /**
     * @brief Get all neutral units.
     * 
     * @return A collection of units
     */
    const sc2::Units& GetNeutralUnits() const;

    /**
     * @brief Get all units.
     * 
     * @return A collection of units
     */
    const sc2::Units& GetAllUnits() const;

    /**
     * @brief Method to call every step to update the units.
     */
    void OnStep();

    /**
     * @brief Get the Actions object for the bot.
     * 
     * @return The Actions object
     */
    sc2::ActionInterface* Actions();

    /**
     * @brief Get the ActionsFeatureLayer object for the bot.
     * 
     * @return The ActionsFeatureLayer object
     */
    sc2::ActionFeatureLayerInterface* ActionsFeatureLayer();

    /**
     * @brief Get the AgentControl object for the bot.
     * 
     * @return The AgentControl object
     */
    sc2::AgentControlInterface* AgentControl();

    /**
     * @brief Get the Observation object for the bot.
     * 
     * @return The Observation object
     */
    const sc2::ObservationInterface* Observation() const;

    /**
     * @brief Get the Query object for the bot.
     * 
     * @return The Query object
     */
    sc2::QueryInterface* Query() const;

    /**
     * @brief Get the Debug object for the bot.
     * 
     * @return The Debug object
     */
    sc2::DebugInterface* Debug();


private:
    sc2::Agent* bot;

    sc2::Units m_AllUnits;

    sc2::Units m_AlliedUnits;
    sc2::Units m_EnemyUnits;
    sc2::Units m_NeutralUnits;

    std::unordered_map<sc2::UNIT_TYPEID, sc2::Units> m_AlliedUnitsByType;
    std::unordered_map<sc2::UNIT_TYPEID, sc2::Units> m_EnemyUnitsByType;
    std::unordered_map<sc2::UNIT_TYPEID, sc2::Units> m_NeutralUnitsByType;

    static sc2::Units s_EmptyUnits;

    void UpdateUnits();
};

}