#include "shadps4_elisa_launch_intent.h"

#include <CLI/CLI.hpp>

#include "launch_intent_shadow.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

LaunchIntent::Shadow ParseCppShadow(const std::vector<std::string>& args) {
    if (args.size() <= 1) {
        return LaunchIntent::NoArgsShadow();
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
        return LaunchIntent::Shadow{};
    }

    return LaunchIntent::BuildShadow({
        .game_path = game_path,
        .game_args = game_args,
        .wait_for_debugger = wait_for_debugger,
        .fullscreen = fullscreen_str,
        .ignore_game_patch = ignore_game_patch,
        .show_fps = show_fps,
        .config_clean = config_clean,
        .config_global = config_global,
        .big_picture = big_picture,
        .add_game_folder = add_game_folder,
        .set_addon_folder = set_addon_folder,
        .patch_file = patch_file,
        .log_append = log_append,
    });
}

const char* PtrToString(uint8_t* ptr) {
    return ptr != nullptr ? reinterpret_cast<const char*>(ptr) : "";
}

bool Matches(const ShadLaunchIntentCABI& elisa, const LaunchIntent::Shadow& cpp) {
    return elisa.kind == cpp.kind && elisa.exit_code == cpp.exit_code && elisa.ok == cpp.ok &&
           std::strcmp(PtrToString(elisa.game_path), cpp.game_path.c_str()) == 0 &&
           std::strcmp(PtrToString(elisa.patch_file), cpp.patch_file.c_str()) == 0 &&
           std::strcmp(PtrToString(elisa.override_root), cpp.override_root.c_str()) == 0 &&
           std::strcmp(PtrToString(elisa.add_game_folder), cpp.add_game_folder.c_str()) == 0 &&
           std::strcmp(PtrToString(elisa.set_addon_folder), cpp.set_addon_folder.c_str()) == 0 &&
           elisa.fullscreen == cpp.fullscreen && elisa.config_mode == cpp.config_mode &&
           elisa.wait_pid == cpp.wait_pid &&
           elisa.game_arg_count == cpp.game_arg_count &&
           std::strcmp(PtrToString(elisa.first_game_arg), cpp.first_game_arg.c_str()) == 0 &&
           std::strcmp(PtrToString(elisa.second_game_arg), cpp.second_game_arg.c_str()) == 0 &&
           std::strcmp(PtrToString(elisa.third_game_arg), cpp.third_game_arg.c_str()) == 0 &&
           std::strcmp(PtrToString(elisa.fourth_game_arg), cpp.fourth_game_arg.c_str()) == 0 &&
           static_cast<bool>(elisa.ignore_game_patch) == cpp.ignore_game_patch &&
           static_cast<bool>(elisa.show_fps) == cpp.show_fps &&
           static_cast<bool>(elisa.log_append) == cpp.log_append &&
           static_cast<bool>(elisa.wait_for_debugger) == cpp.wait_for_debugger &&
           static_cast<bool>(elisa.has_wait_pid) == cpp.has_wait_pid;
}

bool RunCase(const char* name, const std::vector<std::string>& args) {
    std::vector<uint8_t*> argv(args.size());
    for (size_t i = 0; i < args.size(); ++i) {
        argv[i] = reinterpret_cast<uint8_t*>(const_cast<char*>(args[i].c_str()));
    }

    ShadLaunchIntentCABI elisa{};
    const intptr_t abi_ok = shadps4_elisa_parse_launch_intent(
        static_cast<int64_t>(args.size()), argv.empty() ? nullptr : argv.data(), &elisa);
    const LaunchIntent::Shadow cpp = ParseCppShadow(args);
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
        {"positional_game", {"shadps4", "CUSA00264", "--show-fps"}},
        {"fullscreen_true", {"shadps4", "--game", "CUSA00264", "--fullscreen", "true"}},
        {"fullscreen_false", {"shadps4", "--game", "CUSA00264", "--fullscreen", "false"}},
        {"utility_add_folder", {"shadps4", "--add-game-folder", "/games"}},
        {"config_global_wins", {"shadps4", "--game", "CUSA00264", "--config-clean",
                                "--config-global"}},
        {"invalid_fullscreen", {"shadps4", "--game", "CUSA00264", "--fullscreen", "maybe"}},
        {"guest_args", {"shadps4", "--game", "CUSA00264", "--", "--guest-flag", "value"}},
        {"many_guest_args",
         {"shadps4", "--game", "CUSA00264", "--", "one", "two", "three", "four", "five",
          "six", "seven", "eight", "nine", "ten", "eleven", "twelve", "thirteen", "fourteen"}},
    };

    for (const auto& [name, args] : cases) {
        if (!RunCase(name, args)) {
            return 1;
        }
    }

    std::cout << "Elisa launch-intent shadow smoke ok: " << cases.size() << " cases\n";
    return 0;
}
