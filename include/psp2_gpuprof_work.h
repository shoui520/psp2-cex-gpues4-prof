#ifndef PSP2_GPUPROF_WORK_H
#define PSP2_GPUPROF_WORK_H
#include "psp2_gpuprof.h"

/* Experimental retail scheduler observations, not an atomic ownership ticket.
 * IDs originate in GPU firmware, NOT the thread calling this API. They may
 * remain after completion; frame/scene values wrap and are not app label IDs.
 * Render PID also covers transfer work: never assume it identifies a GXM draw.
 * No context addresses, kernel pointers or resource addresses are exported.
 */
#define PSP2_GPUPROF_CAP_WORK_OBSERVATIONS 4u
#define PSP2_GPUPROF_WORK_UNVERIFIED 1u
#define PSP2_GPUPROF_WORK_CHANGED 2u
#define PSP2_GPUPROF_WORK_INACTIVE 4u

typedef struct Psp2GpuProfWorkState {
    uint32_t ta_pid, ta_frame, ta_scene;
    uint32_t render_pid, render_frame, render_scene;
} Psp2GpuProfWorkState;

typedef struct Psp2GpuProfWorkCore {
    uint32_t timer_before, timer_after;
    Psp2GpuProfWorkState before, after;
    uint32_t fragment_signal; /* Group 17: use psp2GpuProfDecodePdsDraw. */
    uint32_t flags;
} Psp2GpuProfWorkCore;

typedef struct Psp2GpuProfWork {
    uint32_t size, abi;
    Psp2GpuProfWorkCore cores[PSP2_GPUPROF_CORES];
    uint32_t reserved[4];
} Psp2GpuProfWork;

/* Each core's signal read is bracketed by scheduler reads under one driver
 * power lock. No GPU pause, new hooks, firmware patches or background thread.
 * CHANGED rejects detected transitions; equal reads DO NOT prove atomicity
 * (including switch-away/back or partially published state). UNVERIFIED is
 * always set in this version. Do not feed these into definitive per-job costs.
 * BUSY during counter capture/OS dumps, OFFLINE while powered off. On failure
 * output is not delivered. Requires CAP_WORK_OBSERVATIONS on the new plugin.
 */
#ifdef __cplusplus
extern "C" {
#endif
int psp2GpuProfReadWork(Psp2GpuProfWork *work);
#ifdef __cplusplus
}
#endif
#endif
