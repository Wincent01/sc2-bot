#pragma once

#include <exception>

#define ENABLE_ASSERTIONS


#ifdef ENABLE_ASSERTIONS
#define ASSERT(x) if(!(x)) throw std::exception("Assertion failed: " #x)
#define ASSERT(x, msg) if(!(x)) throw std::exception("Assertion failed: " #x " " msg)
#else
#define ASSERT(x)
#define ASSERT(x, msg)
#endif
#define NON_NULL(x) ASSERT(x != nullptr)
#define NON_EMPTY(x) ASSERT(!x.empty())