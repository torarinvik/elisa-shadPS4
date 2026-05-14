// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace Common::Trace {

inline std::atomic_bool aggressive_logging{false};
inline std::once_flag aggressive_logging_init_flag;

inline bool EnvEnabled(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
}

inline void EnsureAggressiveLoggingInitialized() {
    std::call_once(aggressive_logging_init_flag, [] {
        aggressive_logging.store(EnvEnabled("SHADPS4_TRACE_RENDER"), std::memory_order_relaxed);
    });
}

inline bool IsAggressiveLoggingEnabled() {
    EnsureAggressiveLoggingInitialized();
    return aggressive_logging.load(std::memory_order_relaxed);
}

inline bool SetAggressiveLoggingEnabled(bool enabled) {
    EnsureAggressiveLoggingInitialized();
    aggressive_logging.store(enabled, std::memory_order_relaxed);
    return enabled;
}

inline bool ToggleAggressiveLogging() {
    EnsureAggressiveLoggingInitialized();
    const bool enabled = !aggressive_logging.load(std::memory_order_relaxed);
    aggressive_logging.store(enabled, std::memory_order_relaxed);
    return enabled;
}

} // namespace Common::Trace
