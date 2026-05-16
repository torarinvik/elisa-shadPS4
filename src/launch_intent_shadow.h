// SPDX-FileCopyrightText: Copyright 2025-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace LaunchIntent {

constexpr int64_t ElisaIntentRun = 0;
constexpr int64_t ElisaIntentNoArgs = 1;
constexpr int64_t ElisaIntentBigPicture = 2;
constexpr int64_t ElisaIntentAddGameFolder = 3;
constexpr int64_t ElisaIntentSetAddonFolder = 4;
constexpr int64_t ElisaIntentError = 5;

constexpr int64_t ElisaFullscreenUnset = 0;
constexpr int64_t ElisaFullscreenTrue = 1;
constexpr int64_t ElisaFullscreenFalse = 2;

constexpr int64_t ElisaConfigDefault = 0;
constexpr int64_t ElisaConfigClean = 1;
constexpr int64_t ElisaConfigGlobal = 2;

struct Shadow {
    int64_t kind = ElisaIntentError;
    intptr_t exit_code = 1;
    std::string game_path;
    std::string patch_file;
    std::string override_root;
    std::string add_game_folder;
    std::string set_addon_folder;
    int64_t fullscreen = ElisaFullscreenUnset;
    int64_t config_mode = ElisaConfigDefault;
    int64_t wait_pid = 0;
    int64_t game_arg_count = 0;
    std::string first_game_arg;
    bool ok = false;
    bool ignore_game_patch = false;
    bool show_fps = false;
    bool log_append = false;
    bool wait_for_debugger = false;
    bool has_wait_pid = false;
};

struct CliState {
    std::optional<std::string> game_path;
    std::vector<std::string> game_args;
    std::optional<std::filesystem::path> override_root;
    std::optional<int> wait_pid;
    bool wait_for_debugger = false;
    std::optional<std::string> fullscreen;
    bool ignore_game_patch = false;
    bool show_fps = false;
    bool config_clean = false;
    bool config_global = false;
    bool big_picture = false;
    std::optional<std::filesystem::path> add_game_folder;
    std::optional<std::filesystem::path> set_addon_folder;
    std::optional<std::string> patch_file;
    bool log_append = false;
};

Shadow NoArgsShadow();
Shadow BuildShadow(const CliState& state);
void ShadowWithElisa(int argc, char* argv[], const Shadow& cpp);

} // namespace LaunchIntent
