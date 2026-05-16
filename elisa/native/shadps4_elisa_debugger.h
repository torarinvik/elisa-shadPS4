#ifndef SHADPS4_ELISA_DEBUGGER_H
#define SHADPS4_ELISA_DEBUGGER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

intptr_t shadps4_elisa_current_pid(void);
intptr_t shadps4_elisa_process_exists(intptr_t pid);
intptr_t shadps4_elisa_wait_for_pid_exit(intptr_t pid, uint32_t poll_ms, int64_t max_polls);

#ifdef __cplusplus
}
#endif

#endif
