#include "Macro.h"

using namespace scbot;

Generator<std::shared_ptr<MoveSequence>>
Macro::SearchBuild(int32_t depth, double alpha, double beta, BoardState& state)
{
    auto result = std::make_shared<MoveSequence>();

    co_yield result;

    co_return;
}
