// The MIT License (MIT)
//
// Copyright (c) 2021-2024 Alexander Kurbatov

#pragma once

#include <sc2api/sc2_agent.h>
#include <sc2api/sc2_unit.h>

#include <chrono>

#include <Data.h>

using namespace scdata;

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

    std::vector<Ramp> m_Ramps;
    std::vector<sc2::Point3D> m_Expansions;

    sc2::Point2D GetClosestPlace(const sc2::Point2D& center, sc2::ABILITY_ID ability_id, float min_radius, float max_radius, float step_size = 45.0f);
    sc2::Point2D GetClosestPlace(const sc2::Point2D& center, const sc2::Point2D& pivot, sc2::ABILITY_ID ability_id, float min_radius, float max_radius, float step_size = 45.0f);
    sc2::Point2D GetClosestPlace(const sc2::Point2D& center, const sc2::Point2D& pivot, const sc2::Units& pylons, sc2::ABILITY_ID ability_id, float min_radius, float max_radius, float step_size = 45.0f);
    sc2::Point2D GetClosestPlace(const sc2::Point2D& pivot, const sc2::Units& pylons, sc2::ABILITY_ID ability_id, float min_radius, float max_radius, float step_size = 45.0f);
    sc2::Point2D GetClosestPlaceWhileAvoiding(const sc2::Point2D& center, const sc2::Point2D& pivot, const sc2::Units& avoid, sc2::ABILITY_ID ability_id, float min_radius, float max_radius, float avoid_radius, bool prefer_distance, float step_size = 45.0f);
    sc2::Point2D GetBestCenter(const sc2::Units& units, sc2::ABILITY_ID ability_id, float min_radius, float max_radius, float benchmark_radius, float step_size = 45.0f);
    std::pair<sc2::Point2D, float> GetBestPath(const sc2::Unit* unit, const sc2::Point2D& center, float min_radius, float max_radius, float step_size = 45.0f);

    sc2::Point2D GetClosestRamp(const sc2::Point2D& center);

    BuildResult AttemptBuild(sc2::ABILITY_ID ability_id);

    bool AnyInProgress(const sc2::UNIT_TYPEID& unit_id);

    void RedistributeWorkers(const sc2::Units& bases);

    sc2::Units RedistributeWorkers(const sc2::Unit* base, int32_t& workers_needed);

    void ReturnToMining(const sc2::Unit* probe);

    void FindRamps();

    void FindExpansions();
    
    std::vector<ActionPlan> m_BuildOrder = {
        {sc2::ABILITY_ID::TRAIN_PROBE},
        {sc2::ABILITY_ID::TRAIN_PROBE},
        {sc2::ABILITY_ID::BUILD_PYLON},
        {sc2::ABILITY_ID::TRAIN_PROBE},
        {sc2::ABILITY_ID::BUILD_GATEWAY},
        {sc2::ABILITY_ID::TRAIN_PROBE},
        {sc2::ABILITY_ID::TRAIN_PROBE},
        {sc2::ABILITY_ID::BUILD_ASSIMILATOR},
        {sc2::ABILITY_ID::BUILD_CYBERNETICSCORE},
        {sc2::ABILITY_ID::BUILD_NEXUS},
        {sc2::ABILITY_ID::TRAIN_PROBE},
        {sc2::ABILITY_ID::BUILD_ASSIMILATOR},
        {sc2::ABILITY_ID::TRAIN_PROBE},
        {sc2::ABILITY_ID::BUILD_PYLON},
        {sc2::ABILITY_ID::TRAIN_PROBE},
        {sc2::ABILITY_ID::TRAIN_ADEPT},
        {sc2::ABILITY_ID::TRAIN_PROBE},
        {sc2::ABILITY_ID::TRAIN_PROBE},
        {sc2::ABILITY_ID::TRAIN_PROBE},
        {sc2::ABILITY_ID::BUILD_STARGATE},
        {sc2::ABILITY_ID::RESEARCH_WARPGATE},
        {sc2::ABILITY_ID::TRAIN_ADEPT}
    };

    sc2::Point2D GetIdealPosition(sc2::ABILITY_ID ability_id);
    TrainResult GetIdealUnitProduction(sc2::ABILITY_ID ability_id);

    // Step-data
    sc2::Units m_AllUnits;
    sc2::Units m_NeutralUnits;
    std::unordered_map<sc2::UNIT_TYPEID, sc2::Units> m_Units;

    std::unordered_map<sc2::Tag, DelayedOrder> m_DelayedOrders;
    std::unordered_set<sc2::Tag> m_OrdersExecuted;
    std::unordered_set<sc2::Tag> m_CheckDelayedOrders;

    float m_NextBuildDispatch;

    AbilityCost m_Resources;

    AbilityCost GetPlannedCosts();

    float ElapsedTime();

    void CheckDelayedOrder(const sc2::Unit* unit_);

    void UpdateStepData();

    const sc2::Units& GetUnits(sc2::UNIT_TYPEID unit_type_) const;
};
