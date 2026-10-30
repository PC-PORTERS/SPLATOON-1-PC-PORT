#pragma once

#include "rebrewu/logger.hpp"

#ifndef RBREW_DISABLE_LOGGING
#include <cstdio>
#include <cstdlib>
#endif

// The shipped game removes diagnostics at compile time, including argument
// evaluation and opt-in trace/profiler files. Test/replay tools retain them.
//
// Console messages go through the colored logger (RBREW_LOG_INFO, ...).
// RBREW_LOGF stays as the raw fprintf path for diagnostic files.
#ifdef RBREW_DISABLE_LOGGING
#define RBREW_LOGF(...) ((void)0)
#define RBREW_PRINT(...) ((void)0)
inline const char* rbrew_diagnostic_env(const char*) {
    return nullptr;
}
#else
#define RBREW_LOGF(...) std::fprintf(__VA_ARGS__)
#define RBREW_PRINT(...) ::rbrew::log::write(::rbrew::log::Level::Info, nullptr, __VA_ARGS__)
inline const char* rbrew_diagnostic_env(const char* name) {
    return std::getenv(name);
}
#endif
