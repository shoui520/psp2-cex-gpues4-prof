#ifndef PSP2_GPUPROF_H
#define PSP2_GPUPROF_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PSP2_GPUPROF_ABI 1u
#define PSP2_GPUPROF_CORES 4u
#define PSP2_GPUPROF_COUNTERS 8u
#define PSP2_GPUPROF_MAX_GROUPS 8u
#define PSP2_GPUPROF_CAP_COUNTERS 1u
#define PSP2_GPUPROF_CAP_SIGNALS 2u

enum Psp2GpuProfResult {
    PSP2_GPUPROF_OK = 0,
    PSP2_GPUPROF_INVALID = -1,
    PSP2_GPUPROF_BUSY = -2,
    PSP2_GPUPROF_OFFLINE = -3,
    PSP2_GPUPROF_STALE = -4,
    PSP2_GPUPROF_DENIED = -5,
    PSP2_GPUPROF_UNSUPPORTED = -6,
    PSP2_GPUPROF_INTERNAL = -7
};

/* Raw SGX543 selectors; event interpretation requires an appropriate event map. */
typedef struct Psp2GpuProfEvent {
    uint32_t group; /* 0..127 */
    uint32_t bit;   /* 0..31 */
} Psp2GpuProfEvent;

typedef struct Psp2GpuProfConfig {
    uint32_t size;
    uint32_t abi;
    Psp2GpuProfEvent events[PSP2_GPUPROF_COUNTERS];
    uint32_t reserved[4]; /* Must be zero. */
} Psp2GpuProfConfig;

/* Opaque generation, bound to the calling process. Never a kernel pointer. */
typedef struct Psp2GpuProfSession {
    uint32_t lo;
    uint32_t hi;
} Psp2GpuProfSession;

typedef struct Psp2GpuProfInfo {
    uint32_t size;
    uint32_t abi;
    int32_t status; /* Zero when ready; startup error if resident but disabled. */
    uint32_t driver_fingerprint;
    uint32_t cores;
    uint32_t counters;
    uint32_t capabilities;
    uint32_t max_groups;
    uint32_t reserved[2];
} Psp2GpuProfInfo;

typedef struct Psp2GpuProfSignalConfig {
    uint32_t size, abi, group_count;
    uint32_t groups[PSP2_GPUPROF_MAX_GROUPS];
    uint32_t reserved[4];
} Psp2GpuProfSignalConfig;

/* Each group is sampled sequentially across the cores. The timer pair brackets
 * that group, not a simultaneous snapshot. Unused entries are zero.
 * Values are instantaneous signal words, NOT accumulated counter deltas.
 */
typedef struct Psp2GpuProfSignals {
    uint32_t size, abi, group_count;
    uint32_t groups[PSP2_GPUPROF_MAX_GROUPS];
    uint32_t timer_before[PSP2_GPUPROF_MAX_GROUPS];
    uint32_t values[PSP2_GPUPROF_MAX_GROUPS][PSP2_GPUPROF_CORES];
    uint32_t timer_after[PSP2_GPUPROF_MAX_GROUPS];
    uint32_t reserved[4];
} Psp2GpuProfSignals;

/* GPU-wide raw counters, NOT per-process utilization. Reads are sequential.
 * Subtract consecutive uint32_t counters modulo 2^32; multiple wraps cannot
 * be detected. Timer frequency is intentionally not assumed to equal clocks.
 */
typedef struct Psp2GpuProfSample {
    uint32_t size;
    uint32_t abi;
    Psp2GpuProfSession session;
    uint32_t sequence;
    uint32_t gpu_timer_before;
    uint32_t counters[PSP2_GPUPROF_CORES][PSP2_GPUPROF_COUNTERS];
    uint32_t gpu_timer_after;
    uint32_t reserved[4];
} Psp2GpuProfSample;

/* Begin returns the initial sample, including the handle. Power loss/reset
 * invalidates that handle; STALE requires a new Begin. BUSY is retryable.
 * Config must have the size and ABI above. Outputs are filled in full; the
 * caller allocates the corresponding structure. No background polling occurs.
 * Begin accepts idle PERF=0 or the driver's reset-only PERF=0xff state.
 * It writes PERF=0 using the retail enable sequence and verifies readback.
 * Other control modes are UNSUPPORTED. It never asserts counter clear or
 * restores a prior 0xff clear command; selectors/enable settings are restored.
 */
int psp2GpuProfBegin(const Psp2GpuProfConfig *config, Psp2GpuProfSample *initial);
int psp2GpuProfRead(const Psp2GpuProfSession *session, Psp2GpuProfSample *sample);
int psp2GpuProfEnd(const Psp2GpuProfSession *session);
int psp2GpuProfGetInfo(Psp2GpuProfInfo *info);
/* One bounded transaction; restores selectors before returning. BUSY while
 * counter capture or an OS diagnostic dump owns the selectors. Does not wake
 * the GPU. Accepts only groups present in the supported retail driver's table.
 */
int psp2GpuProfReadSignals(const Psp2GpuProfSignalConfig *config,
                         Psp2GpuProfSignals *signals);

#ifdef __cplusplus
}
#endif
#endif
