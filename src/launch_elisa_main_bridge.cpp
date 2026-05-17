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

#include "common/logging/log.h"
#include "elisa/native/shadps4_elisa_launch_intent.h"

namespace {

const char* ElisaString(uint8_t* value) {
    return value != nullptr ? reinterpret_cast<const char*>(value) : "";
}

LaunchIntent::CliState BuildCliState(const ShadLaunchIntentCABI& intent, int64_t argc,
                                     uint8_t** argv) {
    LaunchIntent::CliState state{};

    if (intent.kind == LaunchIntent::ElisaIntentBigPicture) {
        state.big_picture = true;
    } else if (intent.kind == LaunchIntent::ElisaIntentAddGameFolder) {
        state.add_game_folder = std::filesystem::path(ElisaString(intent.add_game_folder));
    } else if (intent.kind == LaunchIntent::ElisaIntentSetAddonFolder) {
        state.set_addon_folder = std::filesystem::path(ElisaString(intent.set_addon_folder));
    } else if (std::strcmp(ElisaString(intent.game_path), "") != 0) {
        state.game_path = ElisaString(intent.game_path);
    }

    if (intent.game_arg_count > 0) {
        state.game_args = {"--"};
        for (int64_t i = 0; i < intent.game_arg_count; ++i) {
            const auto argv_index = intent.game_arg_start_index + i;
            if (argv_index >= 0 && argv_index < argc) {
                state.game_args.emplace_back(
                    reinterpret_cast<const char*>(argv[static_cast<size_t>(argv_index)]));
            }
        }
    }
    if (std::strcmp(ElisaString(intent.patch_file), "") != 0) {
        state.patch_file = ElisaString(intent.patch_file);
    }
    if (std::strcmp(ElisaString(intent.override_root), "") != 0) {
        state.override_root = std::filesystem::path(ElisaString(intent.override_root));
    }
    if (intent.fullscreen == LaunchIntent::ElisaFullscreenTrue) {
        state.fullscreen = "true";
    } else if (intent.fullscreen == LaunchIntent::ElisaFullscreenFalse) {
        state.fullscreen = "false";
    }
    if (intent.config_mode == LaunchIntent::ElisaConfigClean) {
        state.config_clean = true;
    } else if (intent.config_mode == LaunchIntent::ElisaConfigGlobal) {
        state.config_global = true;
    }
    if (intent.has_wait_pid) {
        state.wait_pid = static_cast<int>(intent.wait_pid);
    }

    state.ignore_game_patch = static_cast<bool>(intent.ignore_game_patch);
    state.show_fps = static_cast<bool>(intent.show_fps);
    state.log_append = static_cast<bool>(intent.log_append);
    state.wait_for_debugger = static_cast<bool>(intent.wait_for_debugger);
    Common::Log::g_should_append = state.log_append;
    return state;
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

extern "C" intptr_t shadps4_elisa_main_run(uint8_t* executable_name, int64_t argc,
                                           uint8_t** argv, ShadLaunchIntentCABI* intent) {
    if (intent == nullptr || argc < 0) {
        return 1;
    }
    auto state = BuildCliState(*intent, argc, argv);
    return LaunchPipeline::RunParsedLaunch(ElisaString(executable_name), std::move(state));
}
