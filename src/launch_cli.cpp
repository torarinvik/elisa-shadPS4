// SPDX-FileCopyrightText: Copyright 2025-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "launch_cli.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>

#include <CLI/CLI.hpp>
#include <SDL3/SDL_messagebox.h>

#include "common/logging/log.h"

#ifdef SHADPS4_ENABLE_ELISA_PORTS
#include "elisa/native/shadps4_elisa_launch_intent.h"
#endif

namespace LaunchCli {

namespace {

#ifdef SHADPS4_ENABLE_ELISA_PORTS

const char* ElisaString(uint8_t* value) {
    return value != nullptr ? reinterpret_cast<const char*>(value) : "";
}

bool ElisaLaunchIntentEnabled() {
    const char* value = std::getenv("SHADPS4_ELISA_LAUNCH_INTENT");
    return value != nullptr && (std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 ||
                                std::strcmp(value, "TRUE") == 0);
}

std::optional<ParseResult> TryParseWithElisa(int argc, char* argv[]) {
    if (!ElisaLaunchIntentEnabled() || argc <= 1 || argc > 13) {
        return std::nullopt;
    }

    std::array<uint8_t*, 13> elisa_argv{};
    for (auto& arg : elisa_argv) {
        arg = reinterpret_cast<uint8_t*>(const_cast<char*>(""));
    }
    for (int i = 0; i < argc; ++i) {
        elisa_argv[static_cast<size_t>(i)] = reinterpret_cast<uint8_t*>(argv[i]);
    }

    ShadLaunchIntentCABI intent{};
    const intptr_t abi_ok = shadps4_elisa_parse_launch_intent(
        argc, elisa_argv[0], elisa_argv[1], elisa_argv[2], elisa_argv[3], elisa_argv[4],
        elisa_argv[5], elisa_argv[6], elisa_argv[7], elisa_argv[8], elisa_argv[9],
        elisa_argv[10], elisa_argv[11], elisa_argv[12], &intent);
    if (!abi_ok || intent.game_arg_count > 1) {
        return std::nullopt;
    }

    ParseResult result{};
    auto& state = result.state;

    if (intent.kind == LaunchIntent::ElisaIntentError) {
        const char* message = ElisaString(intent.error_message);
        if (std::strcmp(message, "invalid argument for --fullscreen") == 0) {
            std::cerr << "Error: Invalid argument for --fullscreen (use true|false)\n";
        } else if (std::strcmp(message, "") != 0) {
            std::cerr << "Error: " << message << "\n";
        }
        result.should_exit = true;
        result.exit_code = static_cast<int>(intent.exit_code);
    }

    if (intent.kind == LaunchIntent::ElisaIntentBigPicture) {
        state.big_picture = true;
    } else if (intent.kind == LaunchIntent::ElisaIntentAddGameFolder) {
        state.add_game_folder = std::filesystem::path(ElisaString(intent.add_game_folder));
    } else if (intent.kind == LaunchIntent::ElisaIntentSetAddonFolder) {
        state.set_addon_folder = std::filesystem::path(ElisaString(intent.set_addon_folder));
    } else if (std::strcmp(ElisaString(intent.game_path), "") != 0) {
        state.game_path = ElisaString(intent.game_path);
    }

    if (intent.game_arg_count == 1) {
        state.game_args = {"--", ElisaString(intent.first_game_arg)};
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
    return result;
}

#else

std::optional<ParseResult> TryParseWithElisa(int, char*[]) {
    return std::nullopt;
}

#endif

} // namespace

ParseResult Parse(int argc, char* argv[]) {
    if (auto elisa_result = TryParseWithElisa(argc, argv)) {
        return *elisa_result;
    }

    CLI::App app{"shadPS4 Emulator CLI"};
    ParseResult result{};

    auto& state = result.state;

    app.add_option("-g,--game", state.game_path, "Game path or ID");
    app.add_option("-p,--patch", state.patch_file, "Patch file to apply");
    app.add_flag("-i,--ignore-game-patch", state.ignore_game_patch,
                 "Disable automatic loading of game patches");
    app.add_flag("-b,--big-picture", state.big_picture, "Start in Big Picture Mode");
    app.add_option("-f,--fullscreen", state.fullscreen, "Fullscreen mode (true|false)");
    app.add_option("--override-root", state.override_root)->check(CLI::ExistingDirectory);
    app.add_flag("--wait-for-debugger", state.wait_for_debugger);
    app.add_option("--wait-for-pid", state.wait_pid);
    app.add_flag("--show-fps", state.show_fps);
    app.add_flag("--config-clean", state.config_clean);
    app.add_flag("--config-global", state.config_global);
    app.add_flag("--log-append", state.log_append);
    app.add_option("--add-game-folder", state.add_game_folder)->check(CLI::ExistingDirectory);
    app.add_option("--set-addon-folder", state.set_addon_folder)->check(CLI::ExistingDirectory);

    app.allow_extras();
    app.parse_complete_callback([&]() {
        const auto& extras = app.remaining();
        if (!extras.empty()) {
            state.game_args = extras;
        }
    });

    if (argc == 1) {
        LaunchIntent::ShadowWithElisa(argc, argv, LaunchIntent::NoArgsShadow());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "shadPS4",
                                 "This is a CLI application. Please use the QTLauncher for a GUI:\n"
                                 "https://github.com/shadps4-emu/shadps4-qtlauncher/releases",
                                 nullptr);
        std::cout << app.help();
        result.should_exit = true;
        result.exit_code = -1;
        return result;
    }

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        result.should_exit = true;
        result.exit_code = app.exit(e);
        return result;
    }

    Common::Log::g_should_append = state.log_append;
    LaunchIntent::ShadowWithElisa(argc, argv, LaunchIntent::BuildShadow(state));
    return result;
}

} // namespace LaunchCli
