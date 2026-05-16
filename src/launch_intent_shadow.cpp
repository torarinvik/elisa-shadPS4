// SPDX-FileCopyrightText: Copyright 2025-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "launch_intent_shadow.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <vector>

#ifdef SHADPS4_ENABLE_ELISA_PORTS
#include "shadps4_elisa_launch_intent.h"
#endif

namespace LaunchIntent {

namespace {

void CaptureShadowGameArgs(Shadow& out, const std::vector<std::string>& args, size_t first_index) {
    if (args.size() <= first_index) {
        return;
    }
    out.game_arg_count = static_cast<int64_t>(args.size() - first_index);
    if (out.game_arg_count >= 1) {
        out.first_game_arg = args[first_index];
    }
    if (out.game_arg_count >= 2) {
        out.second_game_arg = args[first_index + 1];
    }
    if (out.game_arg_count >= 3) {
        out.third_game_arg = args[first_index + 2];
    }
    if (out.game_arg_count >= 4) {
        out.fourth_game_arg = args[first_index + 3];
    }
}

} // namespace

Shadow NoArgsShadow() {
    Shadow out{};
    out.kind = ElisaIntentNoArgs;
    out.exit_code = -1;
    out.ok = true;
    return out;
}

Shadow BuildShadow(const CliState& state) {
    Shadow out{};
    out.kind = ElisaIntentRun;
    out.exit_code = 0;
    out.ok = true;
    out.patch_file = state.patch_file.value_or("");
    out.override_root = state.override_root ? state.override_root->string() : "";
    out.ignore_game_patch = state.ignore_game_patch;
    out.show_fps = state.show_fps;
    out.log_append = state.log_append;
    out.wait_for_debugger = state.wait_for_debugger;
    out.has_wait_pid = state.wait_pid.has_value();
    out.wait_pid = state.wait_pid.value_or(0);

    if (state.fullscreen) {
        if (*state.fullscreen == "true") {
            out.fullscreen = ElisaFullscreenTrue;
        } else if (*state.fullscreen == "false") {
            out.fullscreen = ElisaFullscreenFalse;
        } else {
            out.kind = ElisaIntentError;
            out.exit_code = 1;
            out.ok = false;
        }
    }
    if (state.config_clean) {
        out.config_mode = ElisaConfigClean;
    }
    if (state.config_global) {
        out.config_mode = ElisaConfigGlobal;
    }
    if (state.big_picture) {
        out.kind = ElisaIntentBigPicture;
    } else if (state.add_game_folder) {
        out.kind = ElisaIntentAddGameFolder;
        out.add_game_folder = state.add_game_folder->string();
    } else if (state.set_addon_folder) {
        out.kind = ElisaIntentSetAddonFolder;
        out.set_addon_folder = state.set_addon_folder->string();
    } else if (!state.game_path.has_value()) {
        if (!state.game_args.empty()) {
            out.game_path = state.game_args.front();
            CaptureShadowGameArgs(out, state.game_args, 1);
        } else {
            out.kind = ElisaIntentError;
            out.exit_code = 1;
            out.ok = false;
        }
    } else {
        out.game_path = *state.game_path;
        if (!state.game_args.empty() && state.game_args.front() == "--") {
            CaptureShadowGameArgs(out, state.game_args, 1);
        } else if (!state.game_args.empty()) {
            out.kind = ElisaIntentError;
            out.exit_code = 1;
            out.ok = false;
        }
    }
    return out;
}

#ifdef SHADPS4_ENABLE_ELISA_PORTS

namespace {

bool ElisaLaunchIntentShadowEnabled() {
    const char* value = std::getenv("SHADPS4_ELISA_SHADOW_LAUNCH_INTENT");
    return value != nullptr && (std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 ||
                                std::strcmp(value, "TRUE") == 0);
}

const char* ElisaString(uint8_t* value) {
    return value != nullptr ? reinterpret_cast<const char*>(value) : "";
}

bool ElisaIntentMatches(const ShadLaunchIntentCABI& elisa, const Shadow& cpp) {
    return elisa.kind == cpp.kind && elisa.exit_code == cpp.exit_code &&
           std::strcmp(ElisaString(elisa.game_path), cpp.game_path.c_str()) == 0 &&
           std::strcmp(ElisaString(elisa.patch_file), cpp.patch_file.c_str()) == 0 &&
           std::strcmp(ElisaString(elisa.override_root), cpp.override_root.c_str()) == 0 &&
           std::strcmp(ElisaString(elisa.add_game_folder), cpp.add_game_folder.c_str()) == 0 &&
           std::strcmp(ElisaString(elisa.set_addon_folder), cpp.set_addon_folder.c_str()) == 0 &&
           elisa.fullscreen == cpp.fullscreen && elisa.config_mode == cpp.config_mode &&
           elisa.wait_pid == cpp.wait_pid &&
           elisa.game_arg_count == cpp.game_arg_count &&
           std::strcmp(ElisaString(elisa.first_game_arg), cpp.first_game_arg.c_str()) == 0 &&
           std::strcmp(ElisaString(elisa.second_game_arg), cpp.second_game_arg.c_str()) == 0 &&
           std::strcmp(ElisaString(elisa.third_game_arg), cpp.third_game_arg.c_str()) == 0 &&
           std::strcmp(ElisaString(elisa.fourth_game_arg), cpp.fourth_game_arg.c_str()) == 0 &&
           static_cast<bool>(elisa.ok) == cpp.ok &&
           static_cast<bool>(elisa.ignore_game_patch) == cpp.ignore_game_patch &&
           static_cast<bool>(elisa.show_fps) == cpp.show_fps &&
           static_cast<bool>(elisa.log_append) == cpp.log_append &&
           static_cast<bool>(elisa.wait_for_debugger) == cpp.wait_for_debugger &&
           static_cast<bool>(elisa.has_wait_pid) == cpp.has_wait_pid;
}

std::string DescribeIntent(const Shadow& intent) {
    std::ostringstream out;
    out << "kind=" << intent.kind << " exit=" << intent.exit_code << " ok=" << intent.ok
        << " game='" << intent.game_path << "' patch='" << intent.patch_file << "' fullscreen="
        << intent.fullscreen << " config=" << intent.config_mode << " game_arg_start="
        << intent.game_arg_start_index << " game_args=" << intent.game_arg_count << " args=['" << intent.first_game_arg << "','"
        << intent.second_game_arg << "','" << intent.third_game_arg << "','"
        << intent.fourth_game_arg << "']";
    return out.str();
}

std::string DescribeIntent(const ShadLaunchIntentCABI& intent) {
    std::ostringstream out;
    out << "kind=" << intent.kind << " exit=" << intent.exit_code << " ok=" << intent.ok
        << " game='" << ElisaString(intent.game_path) << "' patch='" << ElisaString(intent.patch_file)
        << "' fullscreen=" << intent.fullscreen << " config=" << intent.config_mode
        << " game_arg_start=" << intent.game_arg_start_index << " game_args=" << intent.game_arg_count << " args=['"
        << ElisaString(intent.first_game_arg) << "','" << ElisaString(intent.second_game_arg)
        << "','" << ElisaString(intent.third_game_arg) << "','"
        << ElisaString(intent.fourth_game_arg) << "']";
    return out.str();
}

} // namespace

void ShadowWithElisa(int argc, char* argv[], const Shadow& cpp) {
    if (!ElisaLaunchIntentShadowEnabled()) {
        return;
    }
    if (argc < 0) {
        std::cerr << "Elisa launch-intent shadow skipped: negative argc=" << argc << "\n";
        return;
    }

    std::vector<uint8_t*> elisa_argv(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        elisa_argv[static_cast<size_t>(i)] = reinterpret_cast<uint8_t*>(argv[i]);
    }

    ShadLaunchIntentCABI elisa{};
    const intptr_t abi_ok = shadps4_elisa_parse_launch_intent(
        argc, elisa_argv.empty() ? nullptr : elisa_argv.data(), &elisa);
    if (!abi_ok || !ElisaIntentMatches(elisa, cpp)) {
        std::cerr << "Elisa launch-intent shadow mismatch:\n  C++:   " << DescribeIntent(cpp)
                  << "\n  Elisa: " << DescribeIntent(elisa) << "\n";
    }
}

#else

void ShadowWithElisa(int, char*[], const Shadow&) {}

#endif

} // namespace LaunchIntent
