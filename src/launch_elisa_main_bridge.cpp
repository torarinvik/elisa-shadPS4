// SPDX-FileCopyrightText: Copyright 2025-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "launch_pipeline.h"

#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <SDL3/SDL_messagebox.h>
#include <core/emulator_settings.h>

#include "common/config.h"
#include "common/key_manager.h"
#include "common/logging/log.h"
#include "common/memory_patcher.h"
#include "common/path_util.h"
#include "core/emulator_state.h"
#include "core/file_sys/fs.h"
#include "core/ipc/ipc.h"
#include "core/user_settings.h"
#include "imgui/big_picture/big_picture.h"

namespace {

const char* ElisaString(uint8_t* value) {
    return value != nullptr ? reinterpret_cast<const char*>(value) : "";
}

constexpr const char* ElisaHelpText =
    R"(shadPS4 Emulator CLI
Usage: shadps4 [OPTIONS] [game path or ID] [-- guest args...]

Options:
  -h,--help                   Print this help text
  -g,--game <path|ID>         Game path or ID
  -p,--patch <path>           Patch file to apply
  -i,--ignore-game-patch      Disable automatic loading of game patches
  -b,--big-picture            Start in Big Picture Mode
  -f,--fullscreen <true|false>
                               Fullscreen mode
  --override-root <dir>       Override root directory
  --wait-for-debugger         Wait for debugger before running the game
  --wait-for-pid <pid>        Wait for an existing process before launch
  --show-fps                  Show FPS counter
  --config-clean              Use clean config mode
  --config-global             Use global config mode
  --log-append                Append to the log file
  --add-game-folder <dir>     Add a game install directory
  --set-addon-folder <dir>    Set the addon install directory
)";

void PrintElisaHelp() {
    std::cout << ElisaHelpText;
}

} // namespace

extern "C" intptr_t shadps4_elisa_main_no_args(int64_t argc, uint8_t** argv) {
    (void)argc;
    (void)argv;
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "shadPS4",
                             "This is a CLI application. Please use the QTLauncher for a GUI:\n"
                             "https://github.com/shadps4-emu/shadps4-qtlauncher/releases",
                             nullptr);
    PrintElisaHelp();
    return -1;
}

extern "C" intptr_t shadps4_elisa_main_help(int64_t argc, uint8_t** argv) {
    (void)argc;
    (void)argv;
    PrintElisaHelp();
    return 0;
}

extern "C" intptr_t shadps4_elisa_main_print_error(uint8_t* message) {
    const char* text = ElisaString(message);
    if (std::strcmp(text, "") != 0) {
        std::cerr << "Error: " << text << "\n";
    }
    return 1;
}

extern "C" intptr_t shadps4_elisa_pipeline_set_log_append() {
    Common::Log::g_should_append = true;
    return 1;
}

extern "C" intptr_t shadps4_elisa_pipeline_setup_boot_log() {
    Common::Log::Setup("shad_log.txt");
    return 1;
}

extern "C" intptr_t shadps4_elisa_pipeline_initialize_ipc() {
    IPC::Instance().Init();
    return 1;
}

extern "C" intptr_t shadps4_elisa_pipeline_initialize_emulator_state() {
    auto emu_state = std::make_shared<EmulatorState>();
    EmulatorState::SetInstance(emu_state);
    return 1;
}

extern "C" intptr_t shadps4_elisa_pipeline_load_user_settings() {
    UserSettings.Load();
    return 1;
}

extern "C" intptr_t shadps4_elisa_pipeline_load_config() {
    const auto user_dir = Common::FS::GetUserPath(Common::FS::PathType::UserDir);
    Config::load(user_dir / "config.toml");
    return 1;
}

extern "C" intptr_t shadps4_elisa_pipeline_load_keys() {
    auto key_manager = KeyManager::GetInstance();
    key_manager->LoadFromFile();
    if (key_manager->GetAllKeys().TrophyKeySet.ReleaseTrophyKey.empty() &&
        !Config::getTrophyKey().empty()) {
        auto keys = key_manager->GetAllKeys();
        if (keys.TrophyKeySet.ReleaseTrophyKey.empty() && !Config::getTrophyKey().empty()) {
            keys.TrophyKeySet.ReleaseTrophyKey =
                KeyManager::HexStringToBytes(Config::getTrophyKey());
            key_manager->SetAllKeys(keys);
            key_manager->SaveToFile();
        }
    }
    return 1;
}

extern "C" intptr_t shadps4_elisa_pipeline_load_emulator_settings() {
    std::shared_ptr<EmulatorSettingsImpl> emu_settings = std::make_shared<EmulatorSettingsImpl>();
    EmulatorSettingsImpl::SetInstance(emu_settings);
    emu_settings->Load();
    return 1;
}

extern "C" intptr_t shadps4_elisa_pipeline_restart_log_from_settings() {
    Common::Log::Shutdown();
    Common::Log::g_should_append |= EmulatorSettings.IsLogAppend();
    Common::Log::Setup("shad_log.txt");
    return 1;
}

extern "C" intptr_t shadps4_elisa_pipeline_big_picture(uint8_t* executable_name) {
    BigPictureMode::Launch(reinterpret_cast<char*>(executable_name));
    return 1;
}

extern "C" intptr_t shadps4_elisa_pipeline_add_game_folder(uint8_t* path) {
    EmulatorSettings.AddGameInstallDir(std::filesystem::path(ElisaString(path)));
    EmulatorSettings.Save();
    std::cout << "Game folder successfully saved.\n";
    return 1;
}

extern "C" intptr_t shadps4_elisa_pipeline_set_addon_folder(uint8_t* path) {
    EmulatorSettings.SetAddonInstallDir(std::filesystem::path(ElisaString(path)));
    EmulatorSettings.Save();
    std::cout << "Addon folder successfully saved.\n";
    return 1;
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

extern "C" intptr_t shadps4_elisa_pipeline_path_exists(uint8_t* path) {
    return std::filesystem::exists(std::filesystem::path(ElisaString(path))) ? 1 : 0;
}

extern "C" int64_t shadps4_elisa_pipeline_game_install_dir_count() {
    return static_cast<int64_t>(EmulatorSettings.GetGameInstallDirs().size());
}

extern "C" uint8_t* shadps4_elisa_pipeline_game_install_dir_at(int64_t index) {
    thread_local std::string install_dir_path;
    install_dir_path.clear();

    const auto& install_dirs = EmulatorSettings.GetGameInstallDirs();
    if (index >= 0 && static_cast<size_t>(index) < install_dirs.size()) {
        install_dir_path = install_dirs[static_cast<size_t>(index)].string();
    }

    return reinterpret_cast<uint8_t*>(install_dir_path.data());
}

extern "C" uint8_t* shadps4_elisa_pipeline_find_game_by_id(uint8_t* install_dir,
                                                            uint8_t* game_id) {
    thread_local std::string resolved_path;
    resolved_path.clear();

    constexpr int max_depth = 5;
    if (auto found_path =
            Common::FS::FindGameByID(std::filesystem::path(ElisaString(install_dir)),
                                     ElisaString(game_id), max_depth)) {
        resolved_path = found_path->string();
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
