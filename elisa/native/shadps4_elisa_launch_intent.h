#ifndef SHADPS4_ELISA_LAUNCH_INTENT_H
#define SHADPS4_ELISA_LAUNCH_INTENT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ShadLaunchIntentCABI {
    int64_t kind;
    intptr_t exit_code;
    uint8_t* executable_name;
    uint8_t* error_message;
    uint8_t* game_path;
    uint8_t* patch_file;
    uint8_t* override_root;
    uint8_t* add_game_folder;
    uint8_t* set_addon_folder;
    int64_t fullscreen;
    int64_t config_mode;
    int64_t wait_pid;
    int64_t game_arg_start_index;
    int64_t game_arg_count;
    uint8_t* first_game_arg;
    uint8_t* second_game_arg;
    uint8_t* third_game_arg;
    uint8_t* fourth_game_arg;
    intptr_t ok;
    intptr_t ignore_game_patch;
    intptr_t show_fps;
    intptr_t log_append;
    intptr_t wait_for_debugger;
    intptr_t has_wait_pid;
} ShadLaunchIntentCABI;

intptr_t shadps4_elisa_parse_launch_intent(int64_t argc, uint8_t** argv,
                                           ShadLaunchIntentCABI* out);

#ifdef __cplusplus
}
#endif

#endif
