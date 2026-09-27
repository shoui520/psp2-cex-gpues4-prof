#ifndef GPUPROF_CONTINUATIONS_H
#define GPUPROF_CONTINUATIONS_H
#include <stdint.h>

void gp_continuations_init(uintptr_t code_base);
int gp_original_misc(uintptr_t info, uint32_t *misc, uintptr_t process, uintptr_t argument);
int gp_original_power(uintptr_t node, int new_state, int old_state);
int gp_original_reset(uintptr_t info, int recovery);
int gp_original_dump(void *buffer, uint32_t size, uint32_t flags);
int gp_original_teardown(uint32_t index, uintptr_t arg1, uintptr_t arg2);
#endif
