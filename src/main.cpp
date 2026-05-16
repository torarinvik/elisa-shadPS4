// SPDX-FileCopyrightText: Copyright 2025-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/debugger.h"
#include "launch_cli.h"
#include "launch_pipeline.h"

#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    const auto parsed = LaunchCli::Parse(argc, argv);
    if (parsed.should_exit) {
        return parsed.exit_code;
    }

    auto state = parsed.state;

    if (state.wait_pid) {
        Core::Debugger::WaitForPid(*state.wait_pid);
    }

    LaunchPipeline::InitializeRuntimeSettings();

    if (LaunchPipeline::HandleUtilityCommand(state.big_picture, argv[0], state.add_game_folder,
                                             state.set_addon_folder)) {
        return 0;
    }

    if (!LaunchPipeline::NormalizeGamePathAndArgs(state.game_path, state.game_args)) {
        return 1;
    }

    if (!LaunchPipeline::ApplyLaunchFlags(state.patch_file, state.ignore_game_patch,
                                          state.fullscreen, state.show_fps, state.config_clean,
                                          state.config_global)) {
        return 1;
    }

    const auto ebootPath = LaunchPipeline::ResolveGamePathOrId(*state.game_path);
    if (!ebootPath) {
        return 1;
    }

    LaunchPipeline::RunEmulator(argv[0], state.wait_for_debugger, *ebootPath, state.game_args,
                                state.override_root);

    return 0;
}
