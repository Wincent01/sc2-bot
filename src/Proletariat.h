#pragma once

#include <sc2api/sc2_typeenums.h>
#include <sc2api/sc2_common.h>
#include <sc2api/sc2_unit.h>
#include <sc2api/sc2_interfaces.h>
#include <sc2api/sc2_agent.h>

#include "Collective.h"
#include "config.h"

namespace scbot
{

/**
 * @brief Class that manages the worker units. Takes care of the worker distribution and worker production.
 *        Can be used to allocate workers to building.
 */
class Proletariat
{
public:
    /**
     * @brief Construct a new Proletariat object
     * 
     * @param collective The bot instance
     */
    Proletariat(std::shared_ptr<Collective> collective);

    /**
     * @brief Destroy the Proletariat object
     */
    ~Proletariat();

    /**
     * @brief Distribute the workers to the mineral fields and vespen geyser extractors.
     */
    void RedistributeWorkers();

    /**
     * @brief Return a worker to mining.
     * 
     * @param worker The worker unit
     */
    void ReturnToMining(const sc2::Unit* probe);

    /**
     * @brief Get the number of workers that are currently gathering minerals and extractors.
     * 
     * @return A pair of integers. The first integer is the number of workers gathering minerals and the second integer is the number of workers gathering gas.
     */
    const std::pair<int32_t, int32_t>& GetWorkerCount() const;

    /**
     * @brief Get an estimate of the mineral and gas income per second.
     * 
     * @return A pair of floats. The first float is the mineral income per second and the second float is the gas income per second.
     */
    const std::pair<float, float>& GetIncomePerSecond() const;

    /**
     * @brief Get an ideal worker that can be used to build a building.
     * 
     * @param position The position where the building should be built
     * @return The worker unit
     */
    const sc2::Unit* GetWorkerForBuilding(const sc2::Point2D& position);

    /**
     * @brief Method to call every step.
     */
    void OnStep();

private:
    sc2::Units RedistributeWorkers(const sc2::Unit* base, int32_t& workers_needed);

    std::pair<int32_t, int32_t> CalculateWorkCount();

    std::pair<float, float> CalculateIncomePerSecond();

    std::shared_ptr<Collective> m_Collective;

    std::pair<int32_t, int32_t> m_WorkerCount;

    std::pair<float, float> m_IncomePerSecond;
};

} // namespace scbot