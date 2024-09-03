// The MIT License (MIT)
//
// Copyright (c) 2021-2024 Alexander Kurbatov

#pragma once

#include <sc2api/sc2_agent.h>
#include <sc2api/sc2_unit.h>

#include <chrono>
#include <memory>
#include <coroutine>

#include "Data.h"
#include "Collective.h"
#include "Proletariat.h"
#include "Production.h"
#include "Economy.h"
#include "Liberation.h"
#include "Macro.h"

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
        {sc2::ABILITY_ID::BUILD_ASSIMILATOR},
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
        {sc2::ABILITY_ID::TRAIN_ADEPT},
        {sc2::ABILITY_ID::BUILD_GATEWAY},
        {sc2::ABILITY_ID::BUILD_GATEWAY},
        {sc2::ABILITY_ID::BUILD_PYLON},
        {sc2::ABILITY_ID::BUILD_NEXUS},
        {sc2::ABILITY_ID::BUILD_PYLON},
        {sc2::ABILITY_ID::BUILD_GATEWAY},
        {sc2::ABILITY_ID::BUILD_GATEWAY},
    };

    int32_t m_ActionIndex = 0;
    
    // Step-data

    sc2::Units m_AllUnits;
    sc2::Units m_NeutralUnits;
    std::unordered_map<sc2::UNIT_TYPEID, sc2::Units> m_Units;

    std::unordered_map<sc2::Tag, DelayedOrder> m_DelayedOrders;
    std::unordered_set<sc2::Tag> m_OrdersExecuted;
    std::unordered_set<sc2::Tag> m_CheckDelayedOrders;

    float m_NextBuildDispatch;
    float m_NextMacroDispatch;

    std::shared_ptr<scbot::Collective> m_Collective;
    std::shared_ptr<scbot::Proletariat> m_Proletariat;
    std::shared_ptr<scbot::Production> m_Production;
    std::shared_ptr<scbot::Economy> m_Economy;
    std::shared_ptr<scbot::Liberation> m_Liberation;
    std::shared_ptr<scbot::Macro> m_Macro;

    //std::unordered_map<int32_t, sc2::Tag> m_BuildingWorkers;
    std::unordered_set<sc2::Tag> m_BuildingWorkers;

    std::shared_ptr<scbot::MacroPromise> m_MacroPromise;
    bool m_HasMacroPromise = false;

    ResourcePair GetPlannedCosts();

    float ElapsedTime();

    void CheckDelayedOrder(const sc2::Unit* unit_);
};
