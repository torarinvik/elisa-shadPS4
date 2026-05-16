#include "shadps4_elisa_launch_intent.h"

#include <CLI/CLI.hpp>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr int64_t KindRun = 0;
constexpr int64_t KindNoArgs = 1;
constexpr int64_t KindBigPicture = 2;
constexpr int64_t KindAddGameFolder = 3;
constexpr int64_t KindSetAddonFolder = 4;
constexpr int64_t KindError = 5;

constexpr int64_t FullscreenUnset = 0;
constexpr int64_t FullscreenTrue = 1;
constexpr int64_t FullscreenFalse = 2;

constexpr int64_t ConfigDefault = 0;
constexpr int64_t ConfigClean = 1;
constexpr int64_t ConfigGlobal = 2;

struct ShadowIntent {
    int64_t kind = KindError;
    int exit_code = 1;
    std::string game_path;
    std::string patch_file;
    std::string add_game_folder;
    std::string set_addon_folder;
    int64_t fullscreen = FullscreenUnset;
    int64_t config_mode = ConfigDefault;
    int64_t game_arg_count = 0;
    std::string first_game_arg;
    bool ok = false;
    bool ignore_game_patch = false;
    bool show_fps = false;
    bool log_append = false;
    bool wait_for_debugger = false;
};

ShadowIntent ParseCppShadow(const std::vector<std::string>& args) {
    if (args.size() <= 1) {
        return ShadowIntent{.kind = KindNoArgs, .exit_code = -1, .ok = true};
    }

    CLI::App app{"shadPS4 Emulator CLI shadow"};
    std::optional<std::string> game_path;
    std::vector<std::string> game_args;
    std::optional<std::string> fullscreen_str;
    std::optional<std::string> patch_file;
    std::optional<std::string> add_game_folder;
    std::optional<std::string> set_addon_folder;
    bool ignore_game_patch = false;
    bool show_fps = false;
    bool config_clean = false;
    bool config_global = false;
    bool big_picture = false;
    bool wait_for_debugger = false;
    bool log_append = false;

    app.add_option("-g,--game", game_path, "Game path or ID");
    app.add_option("-p,--patch", patch_file, "Patch file to apply");
    app.add_flag("-i,--ignore-game-patch", ignore_game_patch);
    app.add_flag("-b,--big-picture", big_picture);
    app.add_option("-f,--fullscreen", fullscreen_str);
    app.add_flag("--wait-for-debugger", wait_for_debugger);
    app.add_flag("--show-fps", show_fps);
    app.add_flag("--config-clean", config_clean);
    app.add_flag("--config-global", config_global);
    app.add_flag("--log-append", log_append);
    app.add_option("--add-game-folder", add_game_folder);
    app.add_option("--set-addon-folder", set_addon_folder);
    app.allow_extras();
    app.parse_complete_callback([&]() {
        const auto& extras = app.remaining();
        if (!extras.empty()) {
            game_args = extras;
        }
    });

    std::vector<char*> argv;
    argv.reserve(args.size());
    for (const auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    try {
        app.parse(static_cast<int>(argv.size()), argv.data());
    } catch (const CLI::ParseError&) {
        return ShadowIntent{};
    }

    ShadowIntent out{};
    out.exit_code = 0;
    out.ok = true;
    out.ignore_game_patch = ignore_game_patch;
    out.show_fps = show_fps;
    out.log_append = log_append;
    out.wait_for_debugger = wait_for_debugger;
    out.patch_file = patch_file.value_or("");
    out.fullscreen = FullscreenUnset;
    out.config_mode = ConfigDefault;

    if (big_picture) {
        out.kind = KindBigPicture;
        return out;
    }
    if (add_game_folder) {
        out.kind = KindAddGameFolder;
        out.add_game_folder = *add_game_folder;
        return out;
    }
    if (set_addon_folder) {
        out.kind = KindSetAddonFolder;
        out.set_addon_folder = *set_addon_folder;
        return out;
    }
    if (!game_path) {
        if (!game_args.empty()) {
            game_path = game_args.front();
            game_args.erase(game_args.begin());
        } else {
            out.kind = KindError;
            out.exit_code = 1;
            out.ok = false;
            return out;
        }
    }
    if (!game_args.empty()) {
        if (game_args.front() == "--") {
            game_args.erase(game_args.begin());
        } else {
            out.kind = KindError;
            out.exit_code = 1;
            out.ok = false;
            return out;
        }
    }
    if (fullscreen_str) {
        if (*fullscreen_str == "true") {
            out.fullscreen = FullscreenTrue;
        } else if (*fullscreen_str == "false") {
            out.fullscreen = FullscreenFalse;
        } else {
            out.kind = KindError;
            out.exit_code = 1;
            out.ok = false;
            return out;
        }
    }
    if (config_clean) {
        out.config_mode = ConfigClean;
    }
    if (config_global) {
        out.config_mode = ConfigGlobal;
    }

    out.kind = KindRun;
    out.game_path = *game_path;
    out.game_arg_count = static_cast<int64_t>(game_args.size());
    if (!game_args.empty()) {
        out.first_game_arg = game_args.front();
    }
    return out;
}

const char* PtrToString(uint8_t* ptr) {
    return ptr != nullptr ? reinterpret_cast<const char*>(ptr) : "";
}

bool Matches(const ShadLaunchIntentCABI& elisa, const ShadowIntent& cpp) {
    return elisa.kind == cpp.kind && elisa.exit_code == cpp.exit_code && elisa.ok == cpp.ok &&
           std::strcmp(PtrToString(elisa.game_path), cpp.game_path.c_str()) == 0 &&
           std::strcmp(PtrToString(elisa.patch_file), cpp.patch_file.c_str()) == 0 &&
           std::strcmp(PtrToString(elisa.add_game_folder), cpp.add_game_folder.c_str()) == 0 &&
           std::strcmp(PtrToString(elisa.set_addon_folder), cpp.set_addon_folder.c_str()) == 0 &&
           elisa.fullscreen == cpp.fullscreen && elisa.config_mode == cpp.config_mode &&
           elisa.game_arg_count == cpp.game_arg_count &&
           std::strcmp(PtrToString(elisa.first_game_arg), cpp.first_game_arg.c_str()) == 0 &&
           static_cast<bool>(elisa.ignore_game_patch) == cpp.ignore_game_patch &&
           static_cast<bool>(elisa.show_fps) == cpp.show_fps &&
           static_cast<bool>(elisa.log_append) == cpp.log_append &&
           static_cast<bool>(elisa.wait_for_debugger) == cpp.wait_for_debugger;
}

bool RunCase(const char* name, const std::vector<std::string>& args) {
    std::vector<uint8_t*> argv(13, reinterpret_cast<uint8_t*>(const_cast<char*>("")));
    for (size_t i = 0; i < args.size() && i < argv.size(); ++i) {
        argv[i] = reinterpret_cast<uint8_t*>(const_cast<char*>(args[i].c_str()));
    }

    ShadLaunchIntentCABI elisa{};
    const intptr_t abi_ok = shadps4_elisa_parse_launch_intent(
        static_cast<int64_t>(args.size()), argv[0], argv[1], argv[2], argv[3], argv[4], argv[5],
        argv[6], argv[7], argv[8], argv[9], argv[10], argv[11], argv[12], &elisa);
    const ShadowIntent cpp = ParseCppShadow(args);
    if (!abi_ok || !Matches(elisa, cpp)) {
        std::cerr << "Launch-intent shadow mismatch in " << name << ": abi_ok=" << abi_ok
                  << " elisa.kind=" << elisa.kind << " cpp.kind=" << cpp.kind
                  << " elisa.game=" << PtrToString(elisa.game_path) << " cpp.game=" << cpp.game_path
                  << "\n";
        return false;
    }
    return true;
}

} // namespace

int main() {
    const std::vector<std::pair<const char*, std::vector<std::string>>> cases = {
        {"no_args", {"shadps4"}},
        {"normal_game", {"shadps4", "--game", "CUSA00264"}},
        {"fullscreen_true", {"shadps4", "--game", "CUSA00264", "--fullscreen", "true"}},
        {"fullscreen_false", {"shadps4", "--game", "CUSA00264", "--fullscreen", "false"}},
        {"utility_add_folder", {"shadps4", "--add-game-folder", "/games"}},
        {"config_global_wins", {"shadps4", "--game", "CUSA00264", "--config-clean",
                                "--config-global"}},
        {"invalid_fullscreen", {"shadps4", "--game", "CUSA00264", "--fullscreen", "maybe"}},
        {"guest_args", {"shadps4", "--game", "CUSA00264", "--", "--guest-flag", "value"}},
    };

    for (const auto& [name, args] : cases) {
        if (!RunCase(name, args)) {
            return 1;
        }
    }

    std::cout << "Elisa launch-intent shadow smoke ok: " << cases.size() << " cases\n";
    return 0;
}
