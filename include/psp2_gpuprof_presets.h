#ifndef PSP2_GPUPROF_PRESETS_H
#define PSP2_GPUPROF_PRESETS_H
#include "psp2_gpuprof.h"

/* Reference-derived selectors; physical calibration is still required.
 * Slots 4..7 count idle signals, not active cycles. Do not divide by the raw
 * GPU timer and label the result a percentage without calibrating its units.
 */
static inline Psp2GpuProfConfig psp2GpuProfOverviewConfig(void)
{
    Psp2GpuProfConfig cfg = {0};
    static const uint32_t groups[8] = {0, 0, 0, 0, 2, 2, 2, 2};
    static const uint32_t bits[8] = {0, 1, 2, 25, 7, 10, 16, 19};
    unsigned i;
    cfg.size = sizeof(cfg);
    cfg.abi = PSP2_GPUPROF_ABI;
    for (i = 0; i < 8; ++i) {
        cfg.events[i].group = groups[i];
        cfg.events[i].bit = bits[i];
    }
    return cfg;
}

/* Parameter-buffer page fields and selected tile/batch fields. Snapshots may
 * miss peaks and short jobs. Inactive stages can contain stale field values.
 */
static inline Psp2GpuProfSignalConfig psp2GpuProfPipelineSignalConfig(void)
{
    Psp2GpuProfSignalConfig cfg = {0};
    static const uint32_t groups[8] = {0, 3, 41, 17, 38, 46, 49, 51};
    unsigned i;
    cfg.size = sizeof(cfg);
    cfg.abi = PSP2_GPUPROF_ABI;
    cfg.group_count = 8;
    for (i = 0; i < 8; ++i) cfg.groups[i] = groups[i];
    return cfg;
}
#endif
