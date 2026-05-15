// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdlib>
#include <cstring>
#include <limits>
#include "common/logging/log.h"
#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/renderer_vulkan/vk_gpu_command_diagnostics.h"

namespace Vulkan {

inline u64 GpuWaitTimeoutNs() {
    static const u64 timeout = [] {
        const char* value = std::getenv("SHADPS4_GPU_WAIT_TIMEOUT_MS");
        if (value == nullptr || value[0] == '\0') {
            return std::numeric_limits<u64>::max();
        }
        char* end = nullptr;
        const unsigned long long timeout_ms = std::strtoull(value, &end, 10);
        if (end == value || timeout_ms == 0) {
            return std::numeric_limits<u64>::max();
        }
        constexpr u64 ns_per_ms = 1'000'000;
        if (timeout_ms > std::numeric_limits<u64>::max() / ns_per_ms) {
            return std::numeric_limits<u64>::max();
        }
        return static_cast<u64>(timeout_ms) * ns_per_ms;
    }();
    return timeout;
}

inline bool IsFiniteGpuWaitTimeoutEnabled() {
    return GpuWaitTimeoutNs() != std::numeric_limits<u64>::max();
}

inline bool IsMoltenVkSafeModeEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("SHADPS4_MOLTENVK_SAFE_MODE");
        return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
    }();
    return enabled;
}

inline void LogGpuWaitTimeout(const char* wait_name, u64 timeout_ns) {
    LOG_ERROR(Render_Vulkan, "GPU wait timeout: wait={} timeout_ms={}", wait_name,
              timeout_ns / 1'000'000);
    DumpGpuCommandDiagnostics(wait_name);
}

inline void LogGpuWaitFailure(const char* wait_name, vk::Result result) {
    LOG_ERROR(Render_Vulkan, "GPU wait failed: wait={} result={}", wait_name,
              vk::to_string(result));
    DumpGpuCommandDiagnostics(wait_name);
}

} // namespace Vulkan
