#ifndef SHADPS4_ELISA_C_API_H
#define SHADPS4_ELISA_C_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int64_t shadps4_elisa_probe_value(void);
const char* shadps4_elisa_probe_message(void);
int shadps4_elisa_find_ufc1(char* out_path, uint64_t out_path_cap);
int shadps4_elisa_run_ufc_trace(const char* root_dir, const char* profile, uint32_t timeout_ms,
                                char* out_log_path, uint64_t out_log_path_cap,
                                int* out_exit_code, int* out_timed_out);
const char* shadps4_elisa_read_file(const char* path);
const char* shadps4_elisa_last_log_path(void);
const char* shadps4_elisa_last_error(void);

#ifdef __cplusplus
}
#endif

#endif
