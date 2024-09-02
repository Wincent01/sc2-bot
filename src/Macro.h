#pragma once

#include <coroutine>

#include "Collective.h"
#include "Generator.h"

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

    MoveSequence() : score(0.0) {}

    MoveSequence(double s = 0.0, std::vector<Move> m = {}) : score(s), moves(m) {}
};

class Macro {
public:
    Generator<std::shared_ptr<MoveSequence>>
    SearchBuild(
        int32_t depth,
        double alpha,
        double beta,
        BoardState& state
    );
};

} // namespace scbot
