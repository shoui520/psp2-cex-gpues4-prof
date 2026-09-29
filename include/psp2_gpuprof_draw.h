#ifndef PSP2_GPUPROF_DRAW_H
#define PSP2_GPUPROF_DRAW_H
#include "psp2_gpuprof.h"

/* Experimental SGX543 PDS batch observations, not application draw IDs.
 * A valid observation still needs scene/process and command-order correlation.
 * Batch zero is unmapped; inactive hardware can retain an old nonzero batch.
 * No time, utilization, or shader cost is inferred here.
 */
typedef struct Psp2GpuProfDrawObservation {
    uint32_t raw, batch, active;
    int32_t candidate_draw; /* -1 unless active and batch is nonzero. */
} Psp2GpuProfDrawObservation;

static inline Psp2GpuProfDrawObservation psp2GpuProfDecodePdsDraw(uint32_t raw)
{
    Psp2GpuProfDrawObservation out;
    out.raw = raw;
    out.batch = (raw >> 16) & 0x1fffu;
    out.active = (raw >> 29) & 1u;
    out.candidate_draw = out.active && out.batch ? (int32_t)out.batch - 1 : -1;
    return out;
}

/* Fragment-only minimizes mux work. Add vertex groups18..21 separately if
 * needed. All four cores are read sequentially, not simultaneously.
 * Use ReadSignals, not Begin; this is an instantaneous identity signal.
 */
static inline Psp2GpuProfSignalConfig psp2GpuProfFragmentDrawConfig(void)
{
    Psp2GpuProfSignalConfig out = {0};
    out.size = sizeof(out);
    out.abi = PSP2_GPUPROF_ABI;
    out.group_count = 1;
    out.groups[0] = 17;
    return out;
}
#endif
