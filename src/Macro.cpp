#include "Macro.h"

#include "Data.h"
#include "Utilities.h"


using namespace scdata;
using namespace scbot;

// Should use Transposition?
//#define USE_TRANSPOSITION

scbot::Macro::Macro(std::shared_ptr<Collective> collective)
{
    m_Collective = collective;

    m_UnitTypes = m_Collective->Observation()->GetUnitTypeData();
}

scbot::Macro::~Macro()
{
}

void scbot::Macro::OnStep()
{
}

std::shared_ptr<MacroPromise> scbot::Macro::Search()
{
    auto cancellation_token = std::make_shared<bool>(false);
    auto result = std::make_shared<MoveSequence>();
    auto promise = std::make_shared<MacroPromise>();
    promise->m_CancellationToken = cancellation_token;
    promise->m_Result = result;
    auto in_state = GetState();
    promise->m_Thread = std::thread([this, in_state, cancellation_token, result]() {
        auto state = in_state;
        GetBestMove(state, cancellation_token, result);
    });;
    return promise;
}

MoveSequence Macro::SearchBuild(
    int32_t depth,
    double alpha,
    double beta,
    BoardState &state,
    std::shared_ptr<bool> cancellation_token)
{
    if (*cancellation_token) {
        return MoveSequence(EvaluateState(state), {});
    }

#ifdef USE_TRANSPOSITION
    // Lookup transposition table to reuse results
    MoveSequence ttResult;
    if (LookupTranspositionTable(state, depth, alpha, beta, ttResult)) {
        return ttResult;
    }
#endif

    if (depth <= 0 || state.terminal) {
        return MoveSequence(EvaluateState(state), {});
    }

    // Get all possible moves for the current state
    std::vector<Move> moves = GetPossibleMoves(state, 5.0f);
    SortMoves(moves);  // Sort moves using heuristic

    MoveSequence bestSequence(state.turn ? -INFINITY : INFINITY);  // Max for player, Min for opponent

    for (const Move& move : moves) {
        auto oldState = state;

        MakeMove(move, state);

        // Recursively search with reduced depth
        MoveSequence result = SearchBuild(depth - 1, alpha, beta, state, cancellation_token);

        if (!move.nullmove) {
            int i = 0;
        }

        state = oldState;

        /*UnmakeMove(state);

        if (!CompareStates(oldState, state)) {
            std::cout << "State mismatch!" << std::endl;
        }*/

        result.moves.insert(result.moves.begin(), move);

        // Update best score and alpha-beta bounds
        if (state.turn) {  // Maximizing player
            if (result.score > bestSequence.score) {
                bestSequence = result;
            }
            alpha = std::max(alpha, bestSequence.score);
        } else {  // Minimizing player
            if (result.score < bestSequence.score) {
                bestSequence = result;
            }
            beta = std::min(beta, bestSequence.score);
        }

        // Prune the branch if alpha-beta cutoff is reached
        if (beta <= alpha) {
            break;
        }

        if (*cancellation_token) {
            break;
        }
    }

#ifdef USE_TRANSPOSITION
    // Save the result to the transposition table before returning
    SaveToTranspositionTable(state, bestSequence.score, depth, alpha, beta, bestSequence);
#endif

    return bestSequence;
}


double Macro::EvaluateState(BoardState& state) {
    double score = 0.0;

    if (state.terminal) {
        return state.turn ? -INFINITY : INFINITY;
    }
    
    auto& friendly = state.friendly_units;
    auto& enemy = state.enemy_units;

    score += EvaluatePlayer(friendly, enemy);
    score -= EvaluatePlayer(enemy, friendly);

    return score;
}

void Macro::MakeMove(const Move &move, BoardState &state)
{
    auto& current = state.turn ? state.friendly_units : state.enemy_units;
    const auto& current_time = current.time;
    const auto& next_time = current_time + move.delta_time;
    current.steps.push_back(move);
    current.resources = current.resources + move.cost;
    // Loop through the steps, see if any of their complete times passed this time step
    for (auto it = current.steps.begin(); it != current.steps.end(); ++it) {
        auto& step = *it;
        if (current_time < step.complete_time && next_time >= step.complete_time) {
            if (step.nullmove) {
                continue;
            }
            current.units[step.unit] += 1;
            current.planned_units[step.unit] -= 1;
        }
    }
    if (!move.nullmove) {
        current.planned_units[move.unit] += 1;
    }
    current.time = next_time;
    state.turn = !state.turn;
}

void Macro::UnmakeMove(BoardState &state)
{
    state.turn = !state.turn;
    auto& current = state.turn ? state.friendly_units : state.enemy_units;
    const auto& move = current.steps.back();
    current.resources = current.resources - move.cost;
    current.steps.pop_back();
    const auto& current_time = current.time;
    const auto& next_time = current_time - move.delta_time;
    // Loop through the steps, see if any of their complete times passed this time step
    for (auto it = current.steps.begin(); it != current.steps.end(); ++it) {
        auto& step = *it;
        if (current_time >= step.complete_time && next_time < step.complete_time) {
            if (step.nullmove) {
                continue;
            }
            current.units[step.unit] -= 1;
            current.planned_units[step.unit] += 1;
        }
    }
    if (!move.nullmove) {
        current.planned_units[move.unit] -= 1;
        if (current.planned_units[move.unit] == 0) {
            current.planned_units.erase(move.unit);
        }
    }
    current.time = next_time;
}

std::vector<Move> Macro::GetPossibleMoves(BoardState &state, float timestep)
{
    std::vector<Move> moves;

    auto& current = state.turn ? state.friendly_units : state.enemy_units;

    const auto& friendly_units = current.units;
    const auto& planned_units = current.planned_units;
    const auto& current_time = current.time;

    // Count all workers and extractors
    uint32_t num_workers = 0;
    uint32_t num_extractors = 0;
    uint32_t num_bases = 0;
    int32_t num_supply = 0;
    for (const auto& [type, count] : friendly_units) {
        switch (type) {
            case sc2::UNIT_TYPEID::PROTOSS_PROBE:
            case sc2::UNIT_TYPEID::TERRAN_SCV:
            case sc2::UNIT_TYPEID::ZERG_DRONE:
                num_workers += count;
                break;
            case sc2::UNIT_TYPEID::PROTOSS_ASSIMILATOR:
            case sc2::UNIT_TYPEID::TERRAN_REFINERY:
            case sc2::UNIT_TYPEID::ZERG_EXTRACTOR:
                num_extractors += count;
                break;
            case sc2::UNIT_TYPEID::PROTOSS_NEXUS:
            case sc2::UNIT_TYPEID::TERRAN_COMMANDCENTER:
            case sc2::UNIT_TYPEID::ZERG_HATCHERY:
            case sc2::UNIT_TYPEID::ZERG_HIVE:
            case sc2::UNIT_TYPEID::ZERG_LAIR:
                num_bases += count;
                num_supply += count * 15;
                break;
            case sc2::UNIT_TYPEID::PROTOSS_PYLON:
            case sc2::UNIT_TYPEID::TERRAN_SUPPLYDEPOT:
            case sc2::UNIT_TYPEID::ZERG_OVERLORD:
                num_supply += count * 8;
                break;
        }

        const auto& ability_it = UnitToAbility.find(type);

        if (ability_it != UnitToAbility.end()) {
            const auto& ability = ability_it->second;

            const auto& supply_it = UnitSupply.find(ability);

            if (supply_it != UnitSupply.end()) {
                const auto& supply = supply_it->second;

                num_supply -= count * supply;
            }
        }
    }

    int32_t vespene_workers = std::min(num_extractors * 3, num_workers);
    int32_t mineral_workers = std::min(num_bases * 12, num_workers - vespene_workers);
    int32_t excess_workers = num_workers - vespene_workers - mineral_workers;

    int32_t vespene_income = std::ceilf(vespene_workers * 0.94f * timestep);
    int32_t mineral_income = std::ceilf(mineral_workers * 1.256f * timestep);

    ResourcePair resources = {mineral_income, vespene_income};

    for (const auto& planned : planned_units) {
        const auto& ability_it = UnitToAbility.find(planned.first);

        if (ability_it != UnitToAbility.end()) {
            const auto& ability = ability_it->second;

            const auto& supply_it = UnitSupply.find(ability);

            if (supply_it != UnitSupply.end()) {
                const auto& supply = supply_it->second;

                num_supply -= planned.second * supply;
            }
        }

        switch (planned.first) {
            case sc2::UNIT_TYPEID::PROTOSS_PROBE:
            case sc2::UNIT_TYPEID::TERRAN_SCV:
            case sc2::UNIT_TYPEID::ZERG_DRONE:
                num_workers += planned.second;
                break;
            case sc2::UNIT_TYPEID::PROTOSS_ASSIMILATOR:
            case sc2::UNIT_TYPEID::TERRAN_REFINERY:
            case sc2::UNIT_TYPEID::ZERG_EXTRACTOR:
                num_extractors += planned.second;
                break;
            case sc2::UNIT_TYPEID::PROTOSS_NEXUS:
            case sc2::UNIT_TYPEID::TERRAN_COMMANDCENTER:
            case sc2::UNIT_TYPEID::ZERG_HATCHERY:
            case sc2::UNIT_TYPEID::ZERG_HIVE:
            case sc2::UNIT_TYPEID::ZERG_LAIR:
                num_bases += planned.second;
                break;
        }
    }

    // Generate all possible moves
    for (const auto& [ability, requirements] : AbilityRequirements) {
        if (ability == sc2::ABILITY_ID::TRAIN_PROBE) {
            if (num_workers >= (num_bases * 12 + num_extractors * 3)) {
                continue;
            }
        }

        if (ability == sc2::ABILITY_ID::BUILD_ASSIMILATOR) {
            if (num_extractors >= num_bases * 2) {
                continue;
            }
        }

        // Check if we have enough resources
        const auto& cost = AbilityCosts[ability];
        if (current.resources.minerals < cost.minerals || current.resources.vespene < cost.vespene) {
            continue;
        }

        // Check if we have the required units
        bool valid = true;
        for (const auto& type : requirements) {
            const auto& it = friendly_units.find(type);
            if (it == friendly_units.end() || it->second == 0) {
                valid = false;
                break;
            }
        }
        if (!valid) {
            continue;
        }

        const auto& supply_it = UnitSupply.find(ability);
        if (supply_it != UnitSupply.end()) {
            const auto& supply = supply_it->second;

            int32_t available_supply = num_supply - supply;

            if (available_supply < 0) {
                continue;
            }
        }

        const auto& ability_unit_it = AbilityToUnit.find(ability);

        if (ability_unit_it == AbilityToUnit.end()) {
            continue;
        }

        const auto& ability_unit = ability_unit_it->second;

        // Get the production time for the unit
        float production_time = 0.0f;
        const auto& unit_data = this->m_UnitTypes.at(sc2::UnitTypeID(ability_unit));
        production_time = Utilities::ToSecondsFromGameTime(unit_data.build_time);

        moves.push_back({false, ability_unit, resources - cost, current_time + production_time, 5.0f});
    }

    // Can always pass
    moves.push_back({true, sc2::UNIT_TYPEID::INVALID, resources, 0.0f, 5.0f});

    return moves;
}

// Modified GetBestMove function with time control and iterative deepening
void Macro::GetBestMove(
    BoardState& state, 
    std::shared_ptr<bool> cancellation_token,
    std::shared_ptr<MoveSequence> result_ptr
) {
    // Start with a shallow depth and increase iteratively
    int32_t depth = 1;
    *result_ptr = MoveSequence(state.turn ? -INFINITY : INFINITY);  // Max for player, Min for opponent

    // Iterative deepening loop
    while (!*cancellation_token) {
        MoveSequence currentBestSequence(state.turn ? -INFINITY : INFINITY);  // Store current best move sequence

        // Get all possible moves
        std::vector<Move> moves = GetPossibleMoves(state, 5.0f);
        SortMoves(moves);  // Sort moves using a heuristic to improve pruning efficiency

        // Search with current depth
        for (const Move& move : moves) {
            MakeMove(move, state);

            // Search for the best move sequence with time control
            MoveSequence result = SearchBuild(depth, -INFINITY, INFINITY, state, cancellation_token);

            UnmakeMove(state);

            // Add the current move to the result sequence
            result.moves.insert(result.moves.begin(), move);

            // Update best sequence based on the current player's turn
            if (state.turn) {  // Maximizing player
                if (result.score > currentBestSequence.score) {
                    currentBestSequence = result;
                }
            } else {  // Minimizing player
                if (result.score < currentBestSequence.score) {
                    currentBestSequence = result;
                }
            }
        }

        // Increase depth for iterative deepening
        depth++;

        // Update the result with the best sequence found so far
        *result_ptr = currentBestSequence;
    }
}

double Macro::MoveHeuristic(const Move &move)
{
    if (move.nullmove) {
        return 0.0;
    }

    double heuristic = 0.0;

    const auto& ability_it = UnitToAbility.find(move.unit);

    if (ability_it != UnitToAbility.end()) {
        const auto& ability = ability_it->second;

        const auto& supply_it = UnitSupply.find(ability);

        if (supply_it != UnitSupply.end()) {
            const auto& supply = supply_it->second;

            heuristic += supply * 100;
        }

        const auto& cost_it = AbilityCosts.find(ability);

        if (cost_it != AbilityCosts.end()) {
            const auto& cost = cost_it->second;

            heuristic += cost.minerals + cost.vespene * 1.5;
        }
    }

    return heuristic;
}

void Macro::SortMoves(std::vector<Move>& moves)
{
    std::sort(moves.begin(), moves.end(), [this](const Move& a, const Move& b) {
        return MoveHeuristic(a) > MoveHeuristic(b);
    });
}

double Macro::EvaluatePlayer(PlayerState &a, PlayerState &b)
{
    double score = 0.0;

    score -= a.resources.minerals * 0.5;
    score -= a.resources.vespene * 0.75;

    int32_t base_count = 0;
    int32_t worker_count = 0;
    int32_t assimilator_count = 0;
    int32_t supply_count = 0;

    for (const auto& [type, count] : a.planned_units) {
        switch (type) {
            case sc2::UNIT_TYPEID::PROTOSS_NEXUS:
            case sc2::UNIT_TYPEID::TERRAN_COMMANDCENTER:
            case sc2::UNIT_TYPEID::ZERG_HATCHERY:
            case sc2::UNIT_TYPEID::ZERG_HIVE:
            case sc2::UNIT_TYPEID::ZERG_LAIR:
                base_count += count;
                supply_count += count * 15;
                break;
            case sc2::UNIT_TYPEID::PROTOSS_PROBE:
            case sc2::UNIT_TYPEID::TERRAN_SCV:
            case sc2::UNIT_TYPEID::ZERG_DRONE:
                worker_count += count;
                break;
            case sc2::UNIT_TYPEID::PROTOSS_ASSIMILATOR:
            case sc2::UNIT_TYPEID::TERRAN_REFINERY:
            case sc2::UNIT_TYPEID::ZERG_EXTRACTOR:
                assimilator_count += count;
                break;
            case sc2::UNIT_TYPEID::PROTOSS_PYLON:
            case sc2::UNIT_TYPEID::TERRAN_SUPPLYDEPOT:
            case sc2::UNIT_TYPEID::ZERG_OVERLORD:
                supply_count += count * 8;
                break;
        }
    }

    for (const auto& [type, count] : a.units) {
        switch (type) {
            case sc2::UNIT_TYPEID::PROTOSS_NEXUS:
            case sc2::UNIT_TYPEID::TERRAN_COMMANDCENTER:
            case sc2::UNIT_TYPEID::ZERG_HATCHERY:
            case sc2::UNIT_TYPEID::ZERG_HIVE:
            case sc2::UNIT_TYPEID::ZERG_LAIR:
                base_count += count;
                supply_count += count * 15;
                break;
            case sc2::UNIT_TYPEID::PROTOSS_PROBE:
            case sc2::UNIT_TYPEID::TERRAN_SCV:
            case sc2::UNIT_TYPEID::ZERG_DRONE:
                worker_count += count;
                break;
            case sc2::UNIT_TYPEID::PROTOSS_ASSIMILATOR:
            case sc2::UNIT_TYPEID::TERRAN_REFINERY:
            case sc2::UNIT_TYPEID::ZERG_EXTRACTOR:
                assimilator_count += count;
                break;
            case sc2::UNIT_TYPEID::PROTOSS_PYLON:
            case sc2::UNIT_TYPEID::TERRAN_SUPPLYDEPOT:
            case sc2::UNIT_TYPEID::ZERG_OVERLORD:
                supply_count += count * 8;
                break;
        }
        
        const auto& ability_it = UnitToAbility.find(type);

        if (ability_it != UnitToAbility.end()) {
            const auto& ability = ability_it->second;

            const auto& supply_it = UnitSupply.find(ability);

            if (supply_it != UnitSupply.end()) {
                const auto& supply = supply_it->second;

                score += count * supply * 250; //(ability == sc2::ABILITY_ID::TRAIN_PROBE ? 50 : 400);

                supply_count -= count * supply;
            }

            const auto& ability_cost_it = AbilityCosts.find(ability);

            if (ability_cost_it != AbilityCosts.end()) {
                const auto& cost = ability_cost_it->second;

                score += count * (cost.minerals + cost.vespene * 1.5);

                /*if (ability == sc2::ABILITY_ID::BUILD_PYLON || ability == sc2::ABILITY_ID::TRAIN_OVERLORD || ability == sc2::ABILITY_ID::BUILD_SUPPLYDEPOT) {
                    score += count * 100;
                }*/
            }
        }

        const auto& counters_it = UnitCounters.find(type);

        if (counters_it != UnitCounters.end()) {
            const auto& counters = counters_it->second;

            for (const auto& counter : counters) {
                const auto& counter_count = b.units.find(counter);

                if (counter_count != b.units.end()) {
                    score += counter_count->second * 100;
                }
            }
        }
    }

    score += base_count * 100;

    // If we have more workers than the bases can support, penalize
    int32_t excess_workers = worker_count - (base_count * 12 + assimilator_count * 3);
    if (excess_workers > 0) {
        score -= excess_workers * 75;
    }

    // If we have more extractors than bases, penalize
    int32_t excess_extractors = assimilator_count - base_count * 2;
    if (excess_extractors > 0) {
        score -= excess_extractors * 500;
    }
    
    // If we are close to supply cap, penalize, exponentially
    int32_t supply_left = supply_count;
    if (supply_left < 1) {
        score -= 1000;
    }

    score -= supply_left * 50;

    return score;
}

bool Macro::CompareStates(BoardState& a, BoardState& b)
{
    return BoardState::equals(a, b);
}

BoardState Macro::GetState()
{
    BoardState state;
    const auto* obs = m_Collective->Observation();

    state.friendly_units.resources.minerals = obs->GetMinerals();
    state.friendly_units.resources.vespene = obs->GetVespene();

    for (const auto& unit : obs->GetUnits()) {
        if (unit->alliance == sc2::Unit::Alliance::Self) {
            state.friendly_units.units[unit->unit_type] += 1;
        } else if (unit->alliance == sc2::Unit::Alliance::Enemy) {
            state.enemy_units.units[unit->unit_type] += 1;
        }
    }

    // TODO: Update based on scounted info
    state.enemy_units.units[sc2::UNIT_TYPEID::PROTOSS_NEXUS] = 1;
    state.enemy_units.units[sc2::UNIT_TYPEID::PROTOSS_PROBE] = 12;
    state.enemy_units.resources.minerals = 50;
    state.enemy_units.resources.vespene = 0;

    state.terminal = false;
    state.turn = true;

    state.friendly_units.time = 0;
    state.enemy_units.time = 0;

    return state;
}

uint64_t Macro::ComputeHash(const BoardState& state) {
    uint64_t hash = 0;
    for (const auto& [type, count] : state.friendly_units.units) {
        hash ^= static_cast<int32_t>(type) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= count + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    }
    for (const auto& [type, count] : state.enemy_units.units) {
        hash ^= static_cast<int32_t>(type) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= count + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    }
    return hash;
}

// Transposition table lookup
bool Macro::LookupTranspositionTable(const BoardState& state, int32_t depth, double alpha, double beta, MoveSequence& outResult) {
    uint64_t hash = ComputeHash(state); // A function that computes a unique hash for each position
    if (m_TranspositionTable.find(hash) != m_TranspositionTable.end()) {
        const TranspositionEntry& entry = m_TranspositionTable[hash];
        if (entry.depth >= depth) {  // Check if this depth is as deep or deeper than the current search
            if (entry.score <= alpha) {
                outResult.score = entry.score;
                outResult.moves = entry.bestMoveSequence;
                return true; // Alpha cutoff
            }
            if (entry.score >= beta) {
                outResult.score = entry.score;
                outResult.moves = entry.bestMoveSequence;
                return true; // Beta cutoff
            }
        }
    }
    return false;
}

// Save to the transposition table
void Macro::SaveToTranspositionTable(const BoardState& state, double score, int32_t depth, double alpha, double beta, const MoveSequence& bestSequence) {
    uint64_t hash = ComputeHash(state);
    m_TranspositionTable[hash] = { score, depth, alpha, beta, bestSequence.moves };
}

std::shared_ptr<MoveSequence> MacroPromise::Complete()
{
    if (m_CancellationToken) {
        *m_CancellationToken = true;
        m_Thread.join();
    }

    return m_Result;
}

scbot::MacroPromise::~MacroPromise()
{
    if (m_CancellationToken) {
        *m_CancellationToken = true;
    }
}
