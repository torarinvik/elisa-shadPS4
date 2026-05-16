// SPDX-FileCopyrightText: Copyright 2025-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <filesystem>
#include <iostream>
#include <optional>
#include <vector>
#include <CLI/CLI.hpp>
#include <SDL3/SDL_messagebox.h>

#include "common/logging/log.h"
#include "core/debugger.h"
#include "launch_pipeline.h"
#include "launch_intent_shadow.h"

#ifdef _WIN32
#include <windows.h>
#endif
#include <core/user_settings.h>

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    CLI::App app{"shadPS4 Emulator CLI"};

    // ---- CLI state ----
    std::optional<std::string> gamePath;
    std::vector<std::string> gameArgs;
    std::optional<std::filesystem::path> overrideRoot;
    std::optional<int> waitPid;
    bool waitForDebugger = false;

    std::optional<std::string> fullscreenStr;
    bool ignoreGamePatch = false;
    bool showFps = false;
    bool configClean = false;
    bool configGlobal = false;
    bool bigPicture = false;

    std::optional<std::filesystem::path> addGameFolder;
    std::optional<std::filesystem::path> setAddonFolder;
    std::optional<std::string> patchFile;

    // ---- Options ----
    app.add_option("-g,--game", gamePath, "Game path or ID");
    app.add_option("-p,--patch", patchFile, "Patch file to apply");
    app.add_flag("-i,--ignore-game-patch", ignoreGamePatch,
                 "Disable automatic loading of game patches");

    app.add_flag("-b,--big-picture", bigPicture, "Start in Big Picture Mode");

    // FULLSCREEN: behavior-identical
    app.add_option("-f,--fullscreen", fullscreenStr, "Fullscreen mode (true|false)");

    app.add_option("--override-root", overrideRoot)->check(CLI::ExistingDirectory);

    app.add_flag("--wait-for-debugger", waitForDebugger);
    app.add_option("--wait-for-pid", waitPid);

    app.add_flag("--show-fps", showFps);
    app.add_flag("--config-clean", configClean);
    app.add_flag("--config-global", configGlobal);
    app.add_flag("--log-append", Common::Log::g_should_append);

    app.add_option("--add-game-folder", addGameFolder)->check(CLI::ExistingDirectory);
    app.add_option("--set-addon-folder", setAddonFolder)->check(CLI::ExistingDirectory);

    // ---- Capture args after `--` verbatim ----
    app.allow_extras();
    app.parse_complete_callback([&]() {
        const auto& extras = app.remaining();
        if (!extras.empty()) {
            gameArgs = extras;
        }
    });

    // ---- No-args behavior ----
    if (argc == 1) {
        LaunchIntent::ShadowWithElisa(argc, argv, LaunchIntent::NoArgsShadow());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "shadPS4",
                                 "This is a CLI application. Please use the QTLauncher for a GUI:\n"
                                 "https://github.com/shadps4-emu/shadps4-qtlauncher/releases",
                                 nullptr);
        std::cout << app.help();
        return -1;
    }

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    LaunchIntent::CliState launchIntentState{
        .game_path = gamePath,
        .game_args = gameArgs,
        .override_root = overrideRoot,
        .wait_pid = waitPid,
        .wait_for_debugger = waitForDebugger,
        .fullscreen = fullscreenStr,
        .ignore_game_patch = ignoreGamePatch,
        .show_fps = showFps,
        .config_clean = configClean,
        .config_global = configGlobal,
        .big_picture = bigPicture,
        .add_game_folder = addGameFolder,
        .set_addon_folder = setAddonFolder,
        .patch_file = patchFile,
        .log_append = Common::Log::g_should_append,
    };
    LaunchIntent::ShadowWithElisa(argc, argv, LaunchIntent::BuildShadow(launchIntentState));

    if (waitPid)
        Core::Debugger::WaitForPid(*waitPid);

    LaunchPipeline::InitializeRuntimeSettings();

    if (LaunchPipeline::HandleUtilityCommand(bigPicture, argv[0], addGameFolder, setAddonFolder)) {
        return 0;
    }

    if (!LaunchPipeline::NormalizeGamePathAndArgs(gamePath, gameArgs)) {
        return 1;
    }

    if (!LaunchPipeline::ApplyLaunchFlags(patchFile, ignoreGamePatch, fullscreenStr, showFps,
                                          configClean, configGlobal)) {
        return 1;
    }

    const auto ebootPath = LaunchPipeline::ResolveGamePathOrId(*gamePath);
    if (!ebootPath) {
        return 1;
    }

    LaunchPipeline::RunEmulator(argv[0], waitForDebugger, *ebootPath, gameArgs, overrideRoot);

    return 0;
}
