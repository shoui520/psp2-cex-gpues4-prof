#pragma once
#include <psp2/gxm.h>
extern const char *lab_pass, *lab_draw;
extern unsigned lab_frame;
extern int lab_isolated;
extern int lab_sentinel;
extern int lab_benchmark_phase;
#ifdef GPUPROF_LAB_TIMING_VALIDATION
#define LAB_TEST_FRAMES 24
#define LAB_TEST_PHASES 6
#elif defined(GPUPROF_LAB_SIGNAL_VALIDATION)
#define LAB_TEST_FRAMES 96
#define LAB_TEST_PHASES 10
#else
#define LAB_TEST_FRAMES 24
#define LAB_TEST_PHASES 8
#endif
void lab_identity_init(void);
void lab_program(const void *p, const char *name);
int lab_capture_start(unsigned mode, unsigned level, int heavy, int particles);
int lab_capture_stop(void);
int lab_capture_active(void);
int lab_capture_error(void);
const char *lab_capture_path(void);
