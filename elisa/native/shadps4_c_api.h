#ifndef SHADPS4_ELISA_C_API_H
#define SHADPS4_ELISA_C_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int64_t shadps4_elisa_probe_value(void);
const char* shadps4_elisa_probe_message(void);

#ifdef __cplusplus
}
#endif

#endif
