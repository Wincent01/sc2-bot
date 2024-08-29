// The MIT License (MIT)
//
// Copyright (c) 2021-2024 Alexander Kurbatov

#pragma once

#include <sc2api/sc2_agent.h>
#include <sc2api/sc2_unit.h>

#include <chrono>
#include <memory>

#include "Data.h"
#include "Collective.h"
#include "Proletariat.h"
#include "Production.h"
#include "Economy.h"
#include "Liberation.h"

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

    std::shared_ptr<scbot::Collective> m_Collective;
    std::shared_ptr<scbot::Proletariat> m_Proletariat;
    std::shared_ptr<scbot::Production> m_Production;
    std::shared_ptr<scbot::Economy> m_Economy;
    std::shared_ptr<scbot::Liberation> m_Liberation;

    //std::unordered_map<int32_t, sc2::Tag> m_BuildingWorkers;
    std::unordered_set<sc2::Tag> m_BuildingWorkers;

    ResourcePair GetPlannedCosts();

    float ElapsedTime();

    void CheckDelayedOrder(const sc2::Unit* unit_);

    struct Move {
        bool nullmove;
        sc2::UNIT_TYPEID unit;
        scdata::ResourcePair cost;
        float complete_time;
        float delta_time;

        static bool equals(const Move& lhs, const Move& rhs) {
            return lhs.nullmove == rhs.nullmove &&
                lhs.unit == rhs.unit &&
                lhs.cost == rhs.cost &&
                lhs.complete_time == rhs.complete_time &&
                lhs.delta_time == rhs.delta_time;
        }
    };

    struct PlayerState {
        std::unordered_map<sc2::UNIT_TYPEID, uint32_t> units;
        std::unordered_map<sc2::UNIT_TYPEID, uint32_t> planned_units;
        std::deque<Move> steps;
        scdata::ResourcePair resources;
        float time;
        
        static bool equals(const PlayerState& lhs, const PlayerState& rhs) {
            // Compare units and planned_units (unordered_map) and deque steps
            if (lhs.units.size() != rhs.units.size()) return false;
            if (lhs.planned_units.size() != rhs.planned_units.size()) return false;
            if (lhs.steps.size() != rhs.steps.size()) return false;

            // Compare each unit in the unordered_map
            for (const auto& [key, value] : lhs.units) {
                if (rhs.units.find(key) == rhs.units.end() || rhs.units.at(key) != value) return false;
            }

            // Compare each planned unit in the unordered_map
            for (const auto& [key, value] : lhs.planned_units) {
                if (rhs.planned_units.find(key) == rhs.planned_units.end() || rhs.planned_units.at(key) != value) return false;
            }

            // Compare each Move step in the deque
            for (size_t i = 0; i < lhs.steps.size(); ++i) {
                if (!Move::equals(lhs.steps[i], rhs.steps[i])) return false;
            }

            // Compare resources and time
            return lhs.resources == rhs.resources && lhs.time == rhs.time;
        }
    };

    struct BoardState {
        PlayerState friendly_units;
        PlayerState enemy_units;
        bool terminal;
        bool turn;
        
        static bool equals(const BoardState& lhs, const BoardState& rhs) {
            // Compare friendly and enemy PlayerState, terminal, and turn
            return PlayerState::equals(lhs.friendly_units, rhs.friendly_units) &&
                PlayerState::equals(lhs.enemy_units, rhs.enemy_units) &&
                lhs.terminal == rhs.terminal &&
                lhs.turn == rhs.turn;
        }
    };

    struct MoveSequence {
        double score;
        std::vector<Move> moves;

        MoveSequence(double s = 0.0, std::vector<Move> m = {}) : score(s), moves(m) {}
    };

    struct TranspositionEntry {
        double score;
        int32_t depth;
        double alpha;
        double beta;
        std::vector<Move> bestMoveSequence;
    };

    std::unordered_map<uint64_t, TranspositionEntry> m_TranspositionTable;

    double EvaluateState(
        BoardState& state
    );

    void MakeMove(
        const Move& move,
        BoardState& state
    );

    void UnmakeMove(
        BoardState& state
    );

    std::vector<Move> GetPossibleMoves(
        BoardState& state, float timestep
    );

    MoveSequence SearchBuild(
        int32_t depth,
        double alpha,
        double beta,
        BoardState& state,
        const std::chrono::time_point<std::chrono::high_resolution_clock>& start,
        double timeLimit
    );

    std::vector<Move> GetBestMove(
        BoardState& state,
        double timeLimit
    );

    double MoveHeuristic(
        const Move& move
    );

    void SortMoves(
        std::vector<Move>& moves
    );

    double EvaluatePlayer(
        PlayerState& a,
        PlayerState& b
    );

    bool CompareStates(
        BoardState& a,
        BoardState& b
    );

    bool IsTimeUp(const std::chrono::time_point<std::chrono::high_resolution_clock>& start, double timeLimit);

    uint64_t ComputeHash(const BoardState& state);

    bool LookupTranspositionTable(const BoardState& state, int32_t depth, double alpha, double beta, MoveSequence& outResult);

    void SaveToTranspositionTable(const BoardState& state, double score, int32_t depth, double alpha, double beta, const MoveSequence& bestSequence);

    BoardState GetState();
};
