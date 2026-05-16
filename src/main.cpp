// SPDX-FileCopyrightText: Copyright 2025-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <filesystem>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <vector>
#include <CLI/CLI.hpp>
#include <SDL3/SDL_messagebox.h>

#include <core/emulator_settings.h>
#include <core/emulator_state.h>
#include "common/config.h"
#include "common/key_manager.h"
#include "common/logging/log.h"
#include "common/memory_patcher.h"
#include "common/path_util.h"
#include "core/debugger.h"
#include "core/file_sys/fs.h"
#include "core/ipc/ipc.h"
#include "emulator.h"
#include "imgui/big_picture/big_picture.h"

#ifdef _WIN32
#include <windows.h>
#endif
#include <core/user_settings.h>

#ifdef SHADPS4_ENABLE_ELISA_PORTS
#include "shadps4_elisa_launch_intent.h"
#endif

namespace {

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

struct LaunchIntentShadow {
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

#ifdef SHADPS4_ENABLE_ELISA_PORTS

bool ElisaLaunchIntentShadowEnabled() {
    const char* value = std::getenv("SHADPS4_ELISA_SHADOW_LAUNCH_INTENT");
    return value != nullptr && (std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 ||
                                std::strcmp(value, "TRUE") == 0);
}

const char* ElisaString(uint8_t* value) {
    return value != nullptr ? reinterpret_cast<const char*>(value) : "";
}

bool ElisaIntentMatches(const ShadLaunchIntentCABI& elisa, const LaunchIntentShadow& cpp) {
    return elisa.kind == cpp.kind && elisa.exit_code == cpp.exit_code &&
           std::strcmp(ElisaString(elisa.game_path), cpp.game_path.c_str()) == 0 &&
           std::strcmp(ElisaString(elisa.patch_file), cpp.patch_file.c_str()) == 0 &&
           std::strcmp(ElisaString(elisa.override_root), cpp.override_root.c_str()) == 0 &&
           std::strcmp(ElisaString(elisa.add_game_folder), cpp.add_game_folder.c_str()) == 0 &&
           std::strcmp(ElisaString(elisa.set_addon_folder), cpp.set_addon_folder.c_str()) == 0 &&
           elisa.fullscreen == cpp.fullscreen && elisa.config_mode == cpp.config_mode &&
           elisa.wait_pid == cpp.wait_pid && elisa.game_arg_count == cpp.game_arg_count &&
           std::strcmp(ElisaString(elisa.first_game_arg), cpp.first_game_arg.c_str()) == 0 &&
           static_cast<bool>(elisa.ok) == cpp.ok &&
           static_cast<bool>(elisa.ignore_game_patch) == cpp.ignore_game_patch &&
           static_cast<bool>(elisa.show_fps) == cpp.show_fps &&
           static_cast<bool>(elisa.log_append) == cpp.log_append &&
           static_cast<bool>(elisa.wait_for_debugger) == cpp.wait_for_debugger &&
           static_cast<bool>(elisa.has_wait_pid) == cpp.has_wait_pid;
}

std::string DescribeIntent(const LaunchIntentShadow& intent) {
    std::ostringstream out;
    out << "kind=" << intent.kind << " exit=" << intent.exit_code << " ok=" << intent.ok
        << " game='" << intent.game_path << "' patch='" << intent.patch_file << "' fullscreen="
        << intent.fullscreen << " config=" << intent.config_mode << " game_args="
        << intent.game_arg_count << " first_arg='" << intent.first_game_arg << "'";
    return out.str();
}

std::string DescribeIntent(const ShadLaunchIntentCABI& intent) {
    std::ostringstream out;
    out << "kind=" << intent.kind << " exit=" << intent.exit_code << " ok=" << intent.ok
        << " game='" << ElisaString(intent.game_path) << "' patch='" << ElisaString(intent.patch_file)
        << "' fullscreen=" << intent.fullscreen << " config=" << intent.config_mode
        << " game_args=" << intent.game_arg_count << " first_arg='"
        << ElisaString(intent.first_game_arg) << "'";
    return out.str();
}

void ShadowLaunchIntentWithElisa(int argc, char* argv[], const LaunchIntentShadow& cpp) {
    if (!ElisaLaunchIntentShadowEnabled()) {
        return;
    }
    if (argc < 0 || argc > 13) {
        std::cerr << "Elisa launch-intent shadow skipped: argc=" << argc
                  << " exceeds tiny v1 ABI limit\n";
        return;
    }

    std::array<uint8_t*, 13> elisa_argv{};
    for (auto& arg : elisa_argv) {
        arg = reinterpret_cast<uint8_t*>(const_cast<char*>(""));
    }
    for (int i = 0; i < argc; ++i) {
        elisa_argv[static_cast<size_t>(i)] = reinterpret_cast<uint8_t*>(argv[i]);
    }

    ShadLaunchIntentCABI elisa{};
    const intptr_t abi_ok = shadps4_elisa_parse_launch_intent(
        argc, elisa_argv[0], elisa_argv[1], elisa_argv[2], elisa_argv[3], elisa_argv[4],
        elisa_argv[5], elisa_argv[6], elisa_argv[7], elisa_argv[8], elisa_argv[9],
        elisa_argv[10], elisa_argv[11], elisa_argv[12], &elisa);
    if (!abi_ok || !ElisaIntentMatches(elisa, cpp)) {
        std::cerr << "Elisa launch-intent shadow mismatch:\n  C++:   " << DescribeIntent(cpp)
                  << "\n  Elisa: " << DescribeIntent(elisa) << "\n";
    }
}

#else

void ShadowLaunchIntentWithElisa(int, char*[], const LaunchIntentShadow&) {}

#endif

} // namespace

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
        ShadowLaunchIntentWithElisa(argc, argv,
                                    LaunchIntentShadow{.kind = ElisaIntentNoArgs,
                                                       .exit_code = -1,
                                                       .ok = true});
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

    LaunchIntentShadow launchIntentShadow{};
    launchIntentShadow.kind = ElisaIntentRun;
    launchIntentShadow.exit_code = 0;
    launchIntentShadow.ok = true;
    launchIntentShadow.patch_file = patchFile.value_or("");
    launchIntentShadow.override_root = overrideRoot ? overrideRoot->string() : "";
    launchIntentShadow.ignore_game_patch = ignoreGamePatch;
    launchIntentShadow.show_fps = showFps;
    launchIntentShadow.log_append = Common::Log::g_should_append;
    launchIntentShadow.wait_for_debugger = waitForDebugger;
    launchIntentShadow.has_wait_pid = waitPid.has_value();
    launchIntentShadow.wait_pid = waitPid.value_or(0);
    if (fullscreenStr) {
        if (*fullscreenStr == "true") {
            launchIntentShadow.fullscreen = ElisaFullscreenTrue;
        } else if (*fullscreenStr == "false") {
            launchIntentShadow.fullscreen = ElisaFullscreenFalse;
        } else {
            launchIntentShadow.kind = ElisaIntentError;
            launchIntentShadow.exit_code = 1;
            launchIntentShadow.ok = false;
        }
    }
    if (configClean) {
        launchIntentShadow.config_mode = ElisaConfigClean;
    }
    if (configGlobal) {
        launchIntentShadow.config_mode = ElisaConfigGlobal;
    }
    if (bigPicture) {
        launchIntentShadow.kind = ElisaIntentBigPicture;
    } else if (addGameFolder) {
        launchIntentShadow.kind = ElisaIntentAddGameFolder;
        launchIntentShadow.add_game_folder = addGameFolder->string();
    } else if (setAddonFolder) {
        launchIntentShadow.kind = ElisaIntentSetAddonFolder;
        launchIntentShadow.set_addon_folder = setAddonFolder->string();
    } else if (!gamePath.has_value()) {
        if (!gameArgs.empty()) {
            launchIntentShadow.game_path = gameArgs.front();
            launchIntentShadow.game_arg_count = static_cast<int64_t>(gameArgs.size() - 1);
            if (gameArgs.size() > 1) {
                launchIntentShadow.first_game_arg = gameArgs[1];
            }
        } else {
            launchIntentShadow.kind = ElisaIntentError;
            launchIntentShadow.exit_code = 1;
            launchIntentShadow.ok = false;
        }
    } else {
        launchIntentShadow.game_path = *gamePath;
        if (!gameArgs.empty() && gameArgs.front() == "--") {
            launchIntentShadow.game_arg_count = static_cast<int64_t>(gameArgs.size() - 1);
            if (gameArgs.size() > 1) {
                launchIntentShadow.first_game_arg = gameArgs[1];
            }
        } else if (!gameArgs.empty()) {
            launchIntentShadow.kind = ElisaIntentError;
            launchIntentShadow.exit_code = 1;
            launchIntentShadow.ok = false;
        }
    }
    ShadowLaunchIntentWithElisa(argc, argv, launchIntentShadow);

    if (waitPid)
        Core::Debugger::WaitForPid(*waitPid);

    // Start default log
    Common::Log::Setup("shad_log.txt");

    IPC::Instance().Init();

    auto emu_state = std::make_shared<EmulatorState>();
    EmulatorState::SetInstance(emu_state);
    UserSettings.Load();

    const auto user_dir = Common::FS::GetUserPath(Common::FS::PathType::UserDir);
    Config::load(user_dir / "config.toml");

    // ---- Trophy key migration ----
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

    // Load configurations
    std::shared_ptr<EmulatorSettingsImpl> emu_settings = std::make_shared<EmulatorSettingsImpl>();
    EmulatorSettingsImpl::SetInstance(emu_settings);
    emu_settings->Load();

    Common::Log::Shutdown();
    // Start configured log
    Common::Log::g_should_append |= EmulatorSettings.IsLogAppend();
    Common::Log::Setup("shad_log.txt");

    if (bigPicture) {
        BigPictureMode::Launch(argv[0]);
        return 0;
    }

    // ---- Utility commands ----
    if (addGameFolder) {
        EmulatorSettings.AddGameInstallDir(*addGameFolder);
        EmulatorSettings.Save();
        std::cout << "Game folder successfully saved.\n";
        return 0;
    }

    if (setAddonFolder) {
        EmulatorSettings.SetAddonInstallDir(*setAddonFolder);
        EmulatorSettings.Save();
        std::cout << "Addon folder successfully saved.\n";
        return 0;
    }

    if (!gamePath.has_value()) {
        if (!gameArgs.empty()) {
            gamePath = gameArgs.front();
            gameArgs.erase(gameArgs.begin());
        } else {
            std::cerr << "Error: Please provide a game path or ID.\n";
            return 1;
        }
    }
    if (!gameArgs.empty()) {
        if (gameArgs.front() == "--") {
            gameArgs.erase(gameArgs.begin());
        } else {
            std::cerr << "Error: unhandled flags\n";
            return 1;
        }
    }

    // ---- Apply flags ----
    if (patchFile)
        MemoryPatcher::patch_file = *patchFile;

    if (ignoreGamePatch)
        Core::FileSys::MntPoints::ignore_game_patches = true;

    if (fullscreenStr) {
        if (*fullscreenStr == "true") {
            EmulatorSettings.SetFullScreen(true);
        } else if (*fullscreenStr == "false") {
            EmulatorSettings.SetFullScreen(false);
        } else {
            std::cerr << "Error: Invalid argument for --fullscreen (use true|false)\n";
            return 1;
        }
    }

    if (showFps)
        EmulatorSettings.SetShowFpsCounter(true);

    if (configClean)
        EmulatorSettings.SetConfigMode(ConfigMode::Clean);

    if (configGlobal)
        EmulatorSettings.SetConfigMode(ConfigMode::Global);

    // ---- Resolve game path or ID ----
    std::filesystem::path ebootPath(*gamePath);
    if (!std::filesystem::exists(ebootPath)) {
        bool found = false;
        constexpr int maxDepth = 5;
        for (const auto& installDir : EmulatorSettings.GetGameInstallDirs()) {
            if (auto foundPath = Common::FS::FindGameByID(installDir, *gamePath, maxDepth)) {
                ebootPath = *foundPath;
                found = true;
                break;
            }
        }
        if (!found) {
            std::cerr << "Error: Game ID or file path not found: " << *gamePath << "\n";
            return 1;
        }
    }

    auto* emulator = Common::Singleton<Core::Emulator>::Instance();
    emulator->executableName = argv[0];
    emulator->waitForDebuggerBeforeRun = waitForDebugger;
    emulator->Run(ebootPath, gameArgs, overrideRoot);

    return 0;
}
