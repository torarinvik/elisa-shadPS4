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
    int64_t game_arg_count;
    uint8_t* first_game_arg;
    intptr_t ok;
    intptr_t ignore_game_patch;
    intptr_t show_fps;
    intptr_t log_append;
    intptr_t wait_for_debugger;
    intptr_t has_wait_pid;
} ShadLaunchIntentCABI;

intptr_t shadps4_elisa_parse_launch_intent(int64_t argc, uint8_t* arg0, uint8_t* arg1,
                                           uint8_t* arg2, uint8_t* arg3, uint8_t* arg4,
                                           uint8_t* arg5, uint8_t* arg6, uint8_t* arg7,
                                           uint8_t* arg8, uint8_t* arg9, uint8_t* arg10,
                                           uint8_t* arg11, uint8_t* arg12,
                                           ShadLaunchIntentCABI* out);

#ifdef __cplusplus
}
#endif

#endif
