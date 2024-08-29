#pragma once

#include <exception>

#define ENABLE_ASSERTIONS

// Begin assertions
#pragma region Assertions
#ifdef ENABLE_ASSERTIONS
#define ASSERT(x) if(!(x)) throw std::exception("Assertion failed: " #x)
#define ASSERT_MSG(x, msg) if(!(x)) throw std::exception("Assertion failed: " #x " " msg)
#else
#define ASSERT(x)
#define ASSERT_MSG(x, msg)
#endif
#define NON_NULL(x) ASSERT(x != nullptr)
#define NON_EMPTY(x) ASSERT(!x.empty())
#pragma endregion Assertions
// End assertions

#define PROBE_RANGE 10.0f
#define PROBE_RANGE_SQUARED PROBE_RANGE * PROBE_RANGE
