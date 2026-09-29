#ifndef PSP2_GPUPROF_DIAGNOSTIC_H
#define PSP2_GPUPROF_DIAGNOSTIC_H
#include "psp2_gpuprof_work.h"

#define PSP2_GPUPROF_CAP_DIAGNOSTIC 8u
#define PSP2_GPUPROF_DIAG_UNVERIFIED 1u
#define PSP2_GPUPROF_DIAG_SCHEDULER_CHANGED 2u
#define PSP2_GPUPROF_DIAG_PDS_CHANGED 4u
#define PSP2_GPUPROF_DIAG_TAG_CHANGED 8u
#define PSP2_GPUPROF_DIAG_INACTIVE 16u
#define PSP2_GPUPROF_DIAG_STAGE_DISAGREEMENT 32u

typedef struct Psp2GpuProfDiagnosticConfig {
    uint32_t size, abi;
    uint32_t group, tag_group; /* TAG: 70 (pipe0) or 102 (pipe1). */
    uint32_t reserved[4]; /* Must be zero. */
} Psp2GpuProfDiagnosticConfig;

typedef struct Psp2GpuProfDiagnosticCore {
    uint32_t timer_before, timer_after;
    Psp2GpuProfWorkState before, after;
    uint32_t pds_before, tag_before, value, tag_after, pds_after;
    uint32_t flags;
} Psp2GpuProfDiagnosticCore;

typedef struct Psp2GpuProfDiagnostic {
    uint32_t size, abi, group, tag_group;
    Psp2GpuProfDiagnosticCore cores[4];
    uint32_t reserved[4];
} Psp2GpuProfDiagnostic;

/* Experimental co-observations, NOT exclusive per-draw counters or timings.
 * Reads scheduler/PDS/TAG, one raw signal group, then TAG/PDS/scheduler.
 * All reads are sequential; equality cannot exclude switch-away/back.
 * UNVERIFIED is always set. Other flags identify detected association hazards.
 * Groups: 2,4,43,55,71,72,75,76,77,78,103,104,107,108,109,110.
 * Group selection is not a validated bottleneck metric. In particular sampled
 * request bits must not be used as counted cache hit/miss events.
 * BUSY during counter sessions or competing debug use; no output on failure.
 * Check CAP_DIAGNOSTIC before calling (use weak imports for older plugins).
 */
#ifdef __cplusplus
extern "C" {
#endif
int psp2GpuProfReadDiagnostic(const Psp2GpuProfDiagnosticConfig *,
                             Psp2GpuProfDiagnostic *);
#ifdef __cplusplus
}
#endif
#endif
