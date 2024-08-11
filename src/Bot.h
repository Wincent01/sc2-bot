// The MIT License (MIT)
//
// Copyright (c) 2021-2024 Alexander Kurbatov

#pragma once

#include <sc2api/sc2_agent.h>
#include <sc2api/sc2_unit.h>

#include <optional>

// The main bot class.
struct Bot: sc2::Agent
{
    Bot() = default;

private:
    void OnGameStart() final;

    void OnGameEnd() final;

    void OnStep() final;

    void OnBuildingConstructionComplete(const sc2::Unit* building_) final;

    void OnUnitCreated(const sc2::Unit* unit_) final;

    void OnUnitIdle(const sc2::Unit* unit_) final;

    void OnUnitDestroyed(const sc2::Unit* unit_) final;

    void OnUpgradeCompleted(sc2::UpgradeID id_) final;

    void OnUnitDamaged(const sc2::Unit* unit_, float health_, float shields_) final;

    void OnNydusDetected() final;

    void OnNuclearLaunchDetected() final;

    void OnUnitEnterVision(const sc2::Unit* unit_) final;

    void OnError(const std::vector<sc2::ClientError>& client_errors,
        const std::vector<std::string>& protocol_errors = {}) final;

    struct Ramp
    {
        sc2::Point2D point;
    };

    struct DelayedOrder
    {
        sc2::ABILITY_ID ability_id;
        sc2::Point2D position;
        sc2::Tag target_unit_tag;
    };

    struct AbilityCost
    {
        uint32_t minerals;
        uint32_t vespene;

        // Subtract operator
        AbilityCost operator-(const AbilityCost& other) const
        {
            return {minerals - other.minerals, vespene - other.vespene};
        }

        bool operator<(const AbilityCost& other) const
        {
            return minerals < other.minerals || vespene < other.vespene;
        }

        bool operator>(const AbilityCost& other) const
        {
            return minerals > other.minerals || vespene > other.vespene;
        }

        bool operator==(const AbilityCost& other) const
        {
            return minerals == other.minerals && vespene == other.vespene;
        }

        bool operator!=(const AbilityCost& other) const
        {
            return minerals != other.minerals || vespene != other.vespene;
        }

        bool operator<=(const AbilityCost& other) const
        {
            return minerals <= other.minerals || vespene <= other.vespene;
        }

        bool operator>=(const AbilityCost& other) const
        {
            return minerals >= other.minerals || vespene >= other.vespene;
        }

        AbilityCost operator+(const AbilityCost& other) const
        {
            return {minerals + other.minerals, vespene + other.vespene};
        }
    };

    struct BuildResult
    {
        bool success;
        float time;
        AbilityCost cost;

        BuildResult(bool success_, float time_, AbilityCost cost_):
            success(success_),
            time(time_),
            cost(cost_) {}

        BuildResult(bool success_):
            success(success_),
            time(0.0f),
            cost({0, 0}) {}

        BuildResult():
            success(false),
            time(0.0f),
            cost({0, 0}) {}

        bool IsPlanning() const
        {
            return success || time > 0.0f;
        }

        bool IsSuccess() const
        {
            return success;
        }
    };

    std::vector<Ramp> m_Ramps;
    std::vector<sc2::Point3D> m_Expansions;

    sc2::Point2D GetClosestPlace(const sc2::Point2D& center, sc2::ABILITY_ID ability_id, float min_radius, float max_radius, float step_size = 45.0f);
    sc2::Point2D GetClosestPlace(const sc2::Point2D& center, const sc2::Point2D& pivot, sc2::ABILITY_ID ability_id, float min_radius, float max_radius, float step_size = 45.0f);
    sc2::Point2D GetClosestPlace(const sc2::Point2D& center, const sc2::Point2D& pivot, const sc2::Units& pylons, sc2::ABILITY_ID ability_id, float min_radius, float max_radius, float step_size = 45.0f);
    sc2::Point2D GetClosestPlace(const sc2::Point2D& pivot, const sc2::Units& pylons, sc2::ABILITY_ID ability_id, float min_radius, float max_radius, float step_size = 45.0f);
    sc2::Point2D GetClosestPlaceWhileAvoiding(const sc2::Point2D& center, const sc2::Point2D& pivot, const sc2::Units& avoid, sc2::ABILITY_ID ability_id, float min_radius, float max_radius, float avoid_radius, float step_size = 45.0f);
    sc2::Point2D GetBestCenter(const sc2::Units& units, sc2::ABILITY_ID ability_id, float min_radius, float max_radius, float benchmark_radius, float step_size = 45.0f);

    sc2::Point2D GetClosestRamp(const sc2::Point2D& center);

    BuildResult AttemptBuild(sc2::ABILITY_ID ability_id);

    bool AnyQueuedOrders(const sc2::Units& units, sc2::ABILITY_ID ability_id);
    bool AnyQueuedOrder(const sc2::Unit* unit, sc2::ABILITY_ID ability_id);

    bool AnyInProgress(const sc2::UNIT_TYPEID& unit_id);

    sc2::Units FilterOutInProgress(const sc2::Units& units);
    bool IsInProgress(const sc2::Unit* unit);

    void RedistributeWorkers(const sc2::Units& bases);

    sc2::Units RedistributeWorkers(const sc2::Unit* base, int32_t& workers_needed);

    void FindRamps();

    void FindExpansions();

    std::unordered_map<sc2::ABILITY_ID, std::unordered_set<sc2::UNIT_TYPEID>> m_Requirements = {
        {sc2::ABILITY_ID::BUILD_NEXUS, {}},
        {sc2::ABILITY_ID::BUILD_PYLON, {}},
        {sc2::ABILITY_ID::BUILD_ASSIMILATOR, {sc2::UNIT_TYPEID::PROTOSS_NEXUS}},
        {sc2::ABILITY_ID::BUILD_GATEWAY, {sc2::UNIT_TYPEID::PROTOSS_PYLON}},
        {sc2::ABILITY_ID::BUILD_FORGE, {sc2::UNIT_TYPEID::PROTOSS_PYLON}},
        {sc2::ABILITY_ID::BUILD_CYBERNETICSCORE, {sc2::UNIT_TYPEID::PROTOSS_PYLON, sc2::UNIT_TYPEID::PROTOSS_GATEWAY}},
        {sc2::ABILITY_ID::BUILD_ROBOTICSFACILITY, {sc2::UNIT_TYPEID::PROTOSS_PYLON, sc2::UNIT_TYPEID::PROTOSS_CYBERNETICSCORE}},
        {sc2::ABILITY_ID::BUILD_STARGATE, {sc2::UNIT_TYPEID::PROTOSS_PYLON, sc2::UNIT_TYPEID::PROTOSS_CYBERNETICSCORE}},
        {sc2::ABILITY_ID::BUILD_TEMPLARARCHIVE, {sc2::UNIT_TYPEID::PROTOSS_PYLON, sc2::UNIT_TYPEID::PROTOSS_CYBERNETICSCORE}},
        {sc2::ABILITY_ID::BUILD_DARKSHRINE, {sc2::UNIT_TYPEID::PROTOSS_PYLON, sc2::UNIT_TYPEID::PROTOSS_CYBERNETICSCORE}},
        {sc2::ABILITY_ID::BUILD_TWILIGHTCOUNCIL, {sc2::UNIT_TYPEID::PROTOSS_PYLON, sc2::UNIT_TYPEID::PROTOSS_CYBERNETICSCORE}},
        {sc2::ABILITY_ID::BUILD_FLEETBEACON, {sc2::UNIT_TYPEID::PROTOSS_PYLON, sc2::UNIT_TYPEID::PROTOSS_STARGATE}},
        {sc2::ABILITY_ID::BUILD_ROBOTICSBAY, {sc2::UNIT_TYPEID::PROTOSS_PYLON, sc2::UNIT_TYPEID::PROTOSS_ROBOTICSFACILITY}},
        {sc2::ABILITY_ID::BUILD_PHOTONCANNON, {sc2::UNIT_TYPEID::PROTOSS_PYLON, sc2::UNIT_TYPEID::PROTOSS_FORGE}}
    };

    std::unordered_set<sc2::UNIT_TYPEID> m_PoweredStructures = {
        sc2::UNIT_TYPEID::PROTOSS_GATEWAY,
        sc2::UNIT_TYPEID::PROTOSS_FORGE,
        sc2::UNIT_TYPEID::PROTOSS_CYBERNETICSCORE,
        sc2::UNIT_TYPEID::PROTOSS_ROBOTICSFACILITY,
        sc2::UNIT_TYPEID::PROTOSS_STARGATE,
        sc2::UNIT_TYPEID::PROTOSS_TEMPLARARCHIVE,
        sc2::UNIT_TYPEID::PROTOSS_DARKSHRINE,
        sc2::UNIT_TYPEID::PROTOSS_TWILIGHTCOUNCIL,
        sc2::UNIT_TYPEID::PROTOSS_FLEETBEACON,
        sc2::UNIT_TYPEID::PROTOSS_ROBOTICSBAY,
        sc2::UNIT_TYPEID::PROTOSS_PHOTONCANNON
    };

    std::unordered_set<sc2::ABILITY_ID> m_MiningAbilities = {
        sc2::ABILITY_ID::HARVEST_GATHER,
        sc2::ABILITY_ID::HARVEST_RETURN,
        sc2::ABILITY_ID::HARVEST_GATHER_PROBE,
        sc2::ABILITY_ID::HARVEST_RETURN_PROBE
    };

    std::unordered_map<sc2::ABILITY_ID, AbilityCost> m_AbilityCosts = {
        {sc2::ABILITY_ID::BUILD_NEXUS, {400, 0}},
        {sc2::ABILITY_ID::BUILD_PYLON, {100, 0}},
        {sc2::ABILITY_ID::BUILD_ASSIMILATOR, {75, 0}},
        {sc2::ABILITY_ID::BUILD_GATEWAY, {150, 0}},
        {sc2::ABILITY_ID::BUILD_FORGE, {150, 0}},
        {sc2::ABILITY_ID::BUILD_CYBERNETICSCORE, {150, 0}},
        {sc2::ABILITY_ID::BUILD_ROBOTICSFACILITY, {200, 100}},
        {sc2::ABILITY_ID::BUILD_STARGATE, {150, 150}},
        {sc2::ABILITY_ID::BUILD_TEMPLARARCHIVE, {150, 200}},
        {sc2::ABILITY_ID::BUILD_DARKSHRINE, {150, 150}},
        {sc2::ABILITY_ID::BUILD_TWILIGHTCOUNCIL, {150, 100}},
        {sc2::ABILITY_ID::BUILD_FLEETBEACON, {300, 200}},
        {sc2::ABILITY_ID::BUILD_ROBOTICSBAY, {200, 200}},
        {sc2::ABILITY_ID::BUILD_PHOTONCANNON, {150, 0}}
    };

    sc2::Point2D GetIdealPosition(sc2::ABILITY_ID ability_id);

    // Step-data
    std::unordered_map<sc2::UNIT_TYPEID, sc2::Units> m_Units;

    std::unordered_map<sc2::Tag, DelayedOrder> m_DelayedOrders;
    std::unordered_set<sc2::Tag> m_OrdersExecuted;
    std::unordered_set<sc2::Tag> m_CheckDelayedOrders;

    AbilityCost m_Resources;

    AbilityCost GetPlannedCosts();

    void CheckDelayedOrder(const sc2::Unit* unit_);

    bool AnyWithingRange(const sc2::Units& units, const sc2::Point2D& point, float range);

    sc2::Units WithinRange(const sc2::Units& units, const sc2::Point2D& point, float range);

    void UpdateStepData();

    const sc2::Units& GetUnits(sc2::UNIT_TYPEID unit_type_) const;
};
