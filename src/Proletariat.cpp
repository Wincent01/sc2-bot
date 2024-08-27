#include "Proletariat.h"

#include "Utilities.h"

scbot::Proletariat::Proletariat(std::shared_ptr<Collective> collective)
{
    this->m_Collective = collective;
}

scbot::Proletariat::~Proletariat()
{
}

void scbot::Proletariat::RedistributeWorkers()
{
    auto* actions = m_Collective->Actions();

    std::unordered_map<const sc2::Unit*, int32_t> workers_needed_map;
    std::unordered_map<const sc2::Unit*, sc2::Units> excess_workers_map;

    const auto& bases = m_Collective->GetAlliedUnitsOfType(sc2::UNIT_TYPEID::PROTOSS_NEXUS);

    // Find all idle probes
    const auto& probes = m_Collective->GetAlliedUnitsOfType(sc2::UNIT_TYPEID::PROTOSS_PROBE);

    for (const auto* probe : probes) {
        if (probe->orders.empty()) {
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
                if (excess.empty()) {
                    break;
                }

                const auto& worker = excess.back();

                if (IsWorkerAllocated(worker)) {
                    excess.pop_back();
                    continue;
                }

                actions->UnitCommand(worker, sc2::ABILITY_ID::MOVE_MOVE, other_base->pos);

                excess.pop_back();
            }
        }
    }
}

void scbot::Proletariat::ReturnToMining(const sc2::Unit* probe)
{
    if (probe == nullptr) {
        return;
    }

    if (IsWorkerAllocated(probe)) {
        return;
    }

    if (probe->unit_type != sc2::UNIT_TYPEID::PROTOSS_PROBE) {
        return;
    }

    const auto mining_points = Utilities::GetResourcePoints(
        m_Collective->GetNeutralUnits(),
        true,
        false,
        true
    );

    const auto& probes = m_Collective->GetAlliedUnitsOfType(sc2::UNIT_TYPEID::PROTOSS_PROBE);
    const auto& nexus_units = Utilities::FilterOutInProgress(m_Collective->GetAlliedUnitsOfType(sc2::UNIT_TYPEID::PROTOSS_NEXUS));

    if (mining_points.empty() || probes.empty() || nexus_units.empty()) {
        return;
    }

    // Find the closest:
    // * Mining point withing 15 units of a Nexus;
    // * That has less than 2 probes mining it (3 for gas);
    // * and that is not already being mined by a probe.
    
    const sc2::Unit* closest_mining_point = nullptr;
    float closest_distance = std::numeric_limits<float>::max();

    // Start by filtering out the mining points that are not within 15 units of a Nexus or have sufficient probes mining them.
    for (const auto& mining_point : mining_points) {
        float closest_nexus_distance = Utilities::DistanceToClosest(nexus_units, mining_point->pos);

        if (closest_nexus_distance > PROBE_RANGE) {
            continue;
        }

        const auto num_gas_probes = std::count_if(probes.begin(), probes.end(), [&mining_point](const sc2::Unit* probe) {
            return Utilities::IsGatheringFrom(probe, mining_point);
        });

        if (Utilities::IsExtractor(mining_point)) {
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

    auto* actions = m_Collective->Actions();

    actions->UnitCommand(probe, sc2::ABILITY_ID::HARVEST_GATHER, closest_mining_point);
}

const std::pair<int32_t, int32_t>& scbot::Proletariat::GetWorkerCount() const
{
    return m_WorkerCount;
}

const std::pair<float, float>& scbot::Proletariat::GetIncomePerSecond() const
{
    return m_IncomePerSecond;
}

std::pair<int32_t, int32_t> scbot::Proletariat::CalculateWorkCount()
{
    const auto& probes = m_Collective->GetAlliedUnitsOfType(sc2::UNIT_TYPEID::PROTOSS_PROBE);

    int32_t mineral_workers = 0;
    int32_t gas_workers = 0;

    for (const auto& probe : probes) {
        if (!Utilities::IsGathering(probe)) {
            continue;
        }

        const auto& orders = probe->orders;

        for (const auto& order : orders) {
            auto* target = m_Collective->Observation()->GetUnit(order.target_unit_tag);

            if (target == nullptr) {
                continue;
            }

            if (Utilities::IsExtractor(target)) {
                ++gas_workers;
            } else {
                ++mineral_workers;
            }
        }
    }

    return {mineral_workers, gas_workers};
}

std::pair<float, float> scbot::Proletariat::CalculateIncomePerSecond()
{
    auto workers = GetWorkerCount();

    return {
        workers.first * 1.256f,
        workers.second * 0.94f
    };
}

const sc2::Unit* scbot::Proletariat::GetWorkerForBuilding(const sc2::Point2D& position)
{
    const auto& probes = m_Collective->GetAlliedUnitsOfType(sc2::UNIT_TYPEID::PROTOSS_PROBE);

    if (probes.empty()) {
        return nullptr;
    }

    const auto filtered = Utilities::FilterUnits(probes, [this](const sc2::Unit* probe) {
        return !IsWorkerAllocated(probe);
    });

    if (filtered.empty()) {
        return nullptr;
    }

    const auto closest_probe = Utilities::ClosestTo(
        filtered,
        position
    );

    return closest_probe;
}

void scbot::Proletariat::RegisterWorker(const sc2::Unit* worker)
{
    m_AllocatedWorkers.emplace(worker->tag);
}

void scbot::Proletariat::UnregisterWorker(const sc2::Unit* worker)
{
    m_AllocatedWorkers.erase(worker->tag);
}

bool scbot::Proletariat::IsWorkerAllocated(const sc2::Unit* worker) const
{
    return m_AllocatedWorkers.find(worker->tag) != m_AllocatedWorkers.end();
}

void scbot::Proletariat::OnStep()
{
    m_WorkerCount = CalculateWorkCount();
    m_IncomePerSecond = CalculateIncomePerSecond();
}

sc2::Units scbot::Proletariat::RedistributeWorkers(const sc2::Unit* base, int32_t& workers_needed) {
    auto* actions = m_Collective->Actions();
    const auto* obs = m_Collective->Observation();

    // Gather resource points and nearby probes
    auto points = Utilities::WithinRange(
        Utilities::GetResourcePoints(Utilities::Union(
            m_Collective->GetAllUnits(), m_Collective->GetNeutralUnits()
        ), true, false, true),
        base->pos, 15.0f
    );
    
    auto probes = Utilities::WithinRange(
        m_Collective->GetAlliedUnitsOfType(sc2::UNIT_TYPEID::PROTOSS_PROBE),
        base->pos, 15.0f
    );

    std::unordered_set<const sc2::Unit*> assigned_workers;
    workers_needed = 0;

    // Helper lambda to find the closest available worker
    auto find_closest_worker = [&](const sc2::Unit* point) -> const sc2::Unit* {
        const sc2::Unit* closest_worker = nullptr;
        float closest_distance = std::numeric_limits<float>::max();

        for (const auto& probe : probes) {
            bool is_assigned = false;
            for (const auto& assigned : m_WorkerOnPoints) {
                if (assigned.second.find(probe->tag) != assigned.second.end()) {
                    is_assigned = true;
                    break;
                }
            }

            if (is_assigned) continue; // Skip already assigned workers

            if (assigned_workers.find(probe) != assigned_workers.end()) continue; // Skip already assigned workers
            if (probe->orders.empty()) continue; // Skip idle workers

            float distance = sc2::DistanceSquared2D(probe->pos, point->pos);
            if (distance < closest_distance) {
                closest_worker = probe;
                closest_distance = distance;
            }
        }
        return closest_worker;
    };

    // Helper lambda to assign workers to a resource point
    auto assign_workers_to_point = [&](const sc2::Unit* point, int max_workers_needed) {
        int workers_on_point = std::count_if(probes.begin(), probes.end(),
            [&](const sc2::Unit* probe) {
                const auto pointIt = m_WorkerOnPoints.find(point->tag);
                if (pointIt == m_WorkerOnPoints.end()) return false;
                return pointIt->second.find(probe->tag) != pointIt->second.end();
            }
        );

        int workers_to_assign = max_workers_needed - workers_on_point;
        while (workers_to_assign > 0) {
            const sc2::Unit* worker = find_closest_worker(point);
            if (!worker || IsWorkerAllocated(worker)) break; // No more workers available

            actions->UnitCommand(worker, sc2::ABILITY_ID::HARVEST_GATHER, point);
            assigned_workers.emplace(worker);
            --workers_to_assign;
            m_WorkerOnPoints[point->tag].emplace(worker->tag);
        }
    };

    // Loop through points to assign workers
    for (const auto& point : points) {
        if (Utilities::IsInProgress(point)) continue;

        int required_workers = (Utilities::IsExtractor(point)) ? 3 : 2;
        workers_needed += required_workers;

        if (assigned_workers.size() < probes.size()) {
            assign_workers_to_point(point, required_workers);
        }
    }

    // Assign remaining workers to under-saturated points
    for (const auto& probe : probes) {
        if (assigned_workers.find(probe) != assigned_workers.end()) continue;

        const sc2::Unit* closest_point = nullptr;
        float closest_distance = std::numeric_limits<float>::max();

        // Find the closest non-saturated resource point
        for (const auto& point : points) {
            int max_workers = Utilities::IsExtractor(point) ? 3 : 2;

            int workers_on_point = std::count_if(probes.begin(), probes.end(),
                [&](const sc2::Unit* p) { return Utilities::IsGatheringFrom(p, point); }
            );
            if (workers_on_point >= max_workers) continue; // Skip saturated points

            float distance = sc2::DistanceSquared2D(probe->pos, point->pos);
            if (distance < closest_distance) {
                closest_point = point;
                closest_distance = distance;
            }
        }

        if (closest_point && !IsWorkerAllocated(probe)) {
            actions->UnitCommand(probe, sc2::ABILITY_ID::HARVEST_GATHER, closest_point);
            assigned_workers.emplace(probe);
            m_WorkerOnPoints[closest_point->tag].emplace(probe->tag);
        }
    }

    workers_needed = std::max(0, workers_needed - static_cast<int>(assigned_workers.size()));

    // Return excess workers
    sc2::Units excess_workers;
    for (const auto& probe : probes) {
        if (assigned_workers.find(probe) == assigned_workers.end()) {
            excess_workers.push_back(probe);
            for (auto& it : m_WorkerOnPoints) {
                it.second.erase(probe->tag);
            }
        }
    }

    return excess_workers;
}