// SPDX-FileCopyrightText: Copyright 2025-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "launch_cli.h"
#include "launch_pipeline.h"

#include <cstring>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <core/emulator_settings.h>

#include "common/logging/log.h"
#include "common/memory_patcher.h"
#include "core/file_sys/fs.h"
#include "elisa/native/shadps4_elisa_launch_intent.h"

namespace {

const char* ElisaString(uint8_t* value) {
    return value != nullptr ? reinterpret_cast<const char*>(value) : "";
}

} // namespace

extern "C" intptr_t shadps4_elisa_main_no_args(int64_t argc, uint8_t** argv) {
    if (argc < 0) {
        return 1;
    }
    std::vector<char*> cpp_argv(static_cast<size_t>(argc));
    for (int64_t index = 0; index < argc; ++index) {
        cpp_argv[static_cast<size_t>(index)] =
            reinterpret_cast<char*>(argv[static_cast<size_t>(index)]);
    }

    const auto parsed = LaunchCli::Parse(static_cast<int>(argc), cpp_argv.data());
    return parsed.should_exit ? parsed.exit_code : 1;
}

extern "C" intptr_t shadps4_elisa_main_help(int64_t argc, uint8_t** argv) {
    if (argc < 0) {
        return 1;
    }

    std::vector<char*> cpp_argv(static_cast<size_t>(argc));
    for (int64_t index = 0; index < argc; ++index) {
        cpp_argv[static_cast<size_t>(index)] =
            reinterpret_cast<char*>(argv[static_cast<size_t>(index)]);
    }

    const char* old_disable = std::getenv("SHADPS4_DISABLE_ELISA_LAUNCH_INTENT");
    const std::string old_value = old_disable != nullptr ? old_disable : "";
    setenv("SHADPS4_DISABLE_ELISA_LAUNCH_INTENT", "1", 1);
    const auto parsed = LaunchCli::Parse(static_cast<int>(argc), cpp_argv.data());
    if (old_disable != nullptr) {
        setenv("SHADPS4_DISABLE_ELISA_LAUNCH_INTENT", old_value.c_str(), 1);
    } else {
        unsetenv("SHADPS4_DISABLE_ELISA_LAUNCH_INTENT");
    }
    return parsed.should_exit ? parsed.exit_code : 1;
}

extern "C" intptr_t shadps4_elisa_main_report_error(ShadLaunchIntentCABI* intent) {
    if (intent == nullptr) {
        return 1;
    }
    const char* message = ElisaString(intent->error_message);
    if (std::strcmp(message, "invalid argument for --fullscreen") == 0) {
        std::cerr << "Error: Invalid argument for --fullscreen (use true|false)\n";
    } else if (std::strcmp(message, "") != 0) {
        std::cerr << "Error: " << message << "\n";
    }
    return static_cast<int>(intent->exit_code);
}

extern "C" intptr_t shadps4_elisa_pipeline_set_log_append() {
    Common::Log::g_should_append = true;
    return 1;
}

extern "C" intptr_t shadps4_elisa_pipeline_initialize_runtime_settings() {
    LaunchPipeline::InitializeRuntimeSettings();
    return 1;
}

extern "C" intptr_t shadps4_elisa_pipeline_big_picture(uint8_t* executable_name) {
    LaunchPipeline::HandleUtilityCommand(true, reinterpret_cast<char*>(executable_name), std::nullopt,
                                         std::nullopt);
    return 1;
}

extern "C" intptr_t shadps4_elisa_pipeline_add_game_folder(uint8_t* path) {
    return LaunchPipeline::HandleUtilityCommand(false, nullptr,
                                                std::filesystem::path(ElisaString(path)),
                                                std::nullopt)
               ? 1
               : 0;
}

extern "C" intptr_t shadps4_elisa_pipeline_set_addon_folder(uint8_t* path) {
    return LaunchPipeline::HandleUtilityCommand(false, nullptr, std::nullopt,
                                                std::filesystem::path(ElisaString(path)))
               ? 1
               : 0;
}

extern "C" intptr_t shadps4_elisa_pipeline_set_patch_file(uint8_t* path) {
    MemoryPatcher::patch_file = ElisaString(path);
    return 1;
}

extern "C" intptr_t shadps4_elisa_pipeline_set_ignore_game_patch() {
    Core::FileSys::MntPoints::ignore_game_patches = true;
    return 1;
}

extern "C" intptr_t shadps4_elisa_pipeline_set_fullscreen(intptr_t enabled) {
    EmulatorSettings.SetFullScreen(enabled != 0);
    return 1;
}

extern "C" intptr_t shadps4_elisa_pipeline_set_show_fps() {
    EmulatorSettings.SetShowFpsCounter(true);
    return 1;
}

extern "C" intptr_t shadps4_elisa_pipeline_set_config_mode(int64_t mode) {
    if (mode == LaunchIntent::ElisaConfigClean) {
        EmulatorSettings.SetConfigMode(ConfigMode::Clean);
        return 1;
    }
    if (mode == LaunchIntent::ElisaConfigGlobal) {
        EmulatorSettings.SetConfigMode(ConfigMode::Global);
        return 1;
    }
    return 0;
}

extern "C" uint8_t* shadps4_elisa_pipeline_resolve_game_path(uint8_t* game_path) {
    thread_local std::string resolved_path;
    resolved_path.clear();

    const auto eboot_path = LaunchPipeline::ResolveGamePathOrId(ElisaString(game_path));
    if (eboot_path) {
        resolved_path = eboot_path->string();
    }

    return reinterpret_cast<uint8_t*>(resolved_path.data());
}

extern "C" intptr_t shadps4_elisa_pipeline_run_emulator(uint8_t* executable_name,
                                                         intptr_t wait_for_debugger,
                                                         uint8_t* resolved_game_path,
                                                         int64_t argc, uint8_t** argv,
                                                         int64_t game_arg_start_index,
                                                         int64_t game_arg_count,
                                                         uint8_t* override_root) {
    if (argc < 0 || game_arg_count < 0) {
        return 1;
    }

    const auto resolved = ElisaString(resolved_game_path);
    if (std::strcmp(resolved, "") == 0) {
        return 1;
    }

    std::vector<std::string> game_args;
    game_args.reserve(static_cast<size_t>(game_arg_count));
    for (int64_t i = 0; i < game_arg_count; ++i) {
        const auto argv_index = game_arg_start_index + i;
        if (argv_index >= 0 && argv_index < argc) {
            game_args.emplace_back(reinterpret_cast<const char*>(argv[static_cast<size_t>(argv_index)]));
        }
    }

    std::optional<std::filesystem::path> override_path;
    if (std::strcmp(ElisaString(override_root), "") != 0) {
        override_path = std::filesystem::path(ElisaString(override_root));
    }

    LaunchPipeline::RunEmulator(ElisaString(executable_name), wait_for_debugger != 0, resolved,
                                game_args, override_path);
    return 0;
}
