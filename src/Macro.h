#pragma once

#include <thread>

#include "Collective.h"

#include <sc2api/sc2_interfaces.h>

namespace scbot {

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
    std::unordered_map<sc2::UNIT_TYPEID, float> planned_units;
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
    bool simple;
    
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

class MacroPromise {
    friend class Macro;
public:
    std::shared_ptr<MoveSequence> Complete();

    ~MacroPromise();

private:
    std::shared_ptr<bool> m_CancellationToken;
    std::shared_ptr<MoveSequence> m_Result;
    std::thread m_Thread;
};

class Macro {
public:
    /**
     * @brief Construct a new Macro object
     * 
     * @param collective The collective object
     */
    Macro(std::shared_ptr<Collective> collective);

    /**
     * @brief Destroy the Macro object
     */
    ~Macro();

    /**
     * @brief Method that is called every frame.
     */
    void OnStep();

    /**
     * @brief Start searching for the best move sequence.
     * 
     * @return A promise with the result of the search that can be completed later
     */
    std::shared_ptr<MacroPromise> Search();

private:
    MoveSequence SearchBuild(
        int32_t depth,
        double alpha,
        double beta,
        BoardState& state,
        std::shared_ptr<bool> cancellation_token
    );

    double EvaluateState(
        BoardState& state
    );

    void MakeMove(
        BoardState& state,
        const Move& move
    );

    void UnmakeMove(
        BoardState& state,
        const Move& move
    );

    std::vector<Move> GetPossibleMoves(
        BoardState& state, float timestep
    );

    void GetBestMove(
        BoardState& state,
        std::shared_ptr<bool> cancellation_token,
        std::shared_ptr<MoveSequence> result_ptr
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

    uint64_t ComputeHash(const BoardState& state);

    bool LookupTranspositionTable(const BoardState& state, int32_t depth, double alpha, double beta, MoveSequence& outResult);

    void SaveToTranspositionTable(const BoardState& state, double score, int32_t depth, double alpha, double beta, const MoveSequence& bestSequence);

    BoardState GetState();
    
    std::unordered_map<uint64_t, TranspositionEntry> m_TranspositionTable;

    std::shared_ptr<Collective> m_Collective;

    sc2::UnitTypes m_UnitTypes;
};

} // namespace scbot
