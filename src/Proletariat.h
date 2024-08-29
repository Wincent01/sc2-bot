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
     * @brief Register a worker as allocated.
     * 
     * @param worker The worker unit
     */
    void RegisterWorker(const sc2::Unit* worker);

    /**
     * @brief Unregister a worker as allocated.
     *
     * @param worker The worker unit
     */
    void UnregisterWorker(const sc2::Unit* worker);

    /**
     * @brief Check if a worker is allocated.
     * 
     * @param worker The worker unit
     * @return true if the worker is allocated, false otherwise
     */
    bool IsWorkerAllocated(const sc2::Unit* worker) const;

    /**
     * @brief Method to call every step.
     */
    void OnStep();

private:
    sc2::Units RedistributeWorkers(const sc2::Unit* base, int32_t& workers_needed);

    void AllocateWorkersToPoint(const sc2::Units& workers, const sc2::Unit* point);

    std::pair<int32_t, int32_t> CalculateWorkCount();

    std::pair<float, float> CalculateIncomePerSecond();

    std::shared_ptr<Collective> m_Collective;

    std::pair<int32_t, int32_t> m_WorkerCount;

    std::pair<float, float> m_IncomePerSecond;

    std::unordered_set<sc2::Tag> m_AllocatedWorkers;

    std::unordered_map<sc2::Tag, sc2::Tag> m_WorkerPoints;
};

} // namespace scbot