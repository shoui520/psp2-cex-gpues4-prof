#ifndef GPUPROF_SESSION_H
#define GPUPROF_SESSION_H

#include "psp2_gpuprof.h"

/* Backend contract: caller holds the profiler guard around every operation.
 * acquire is NONBLOCKING, also checks device-on state and protects its MMIO
 * lifetime with the driver power lock. On failure it retains no resources.
 * read/write execute ordered 32-bit MMIO only while acquire is held.
 * Hooks must participate in the same guard. No callbacks into the engine.
 */
struct gp_io {
    void *ctx;
    int (*acquire)(void *ctx);
    void (*release)(void *ctx);
    uint32_t (*read)(void *ctx, uint32_t offset);
    void (*write)(void *ctx, uint32_t offset, uint32_t value);
};

struct gp_session {
    uint32_t begin_failure_offset, begin_failure_value;
    struct gp_io io;
    uint64_t generation;
    int32_t owner;
    uint32_t active;
    uint32_t sequence;
    uint32_t saved_enable;
    uint32_t saved_selectors[4][4];
};

void gp_init(struct gp_session *s, const struct gp_io *io);
int gp_begin(struct gp_session *s, int32_t pid, const Psp2GpuProfConfig *config,
             Psp2GpuProfSample *sample);
int gp_read(struct gp_session *s, int32_t pid, Psp2GpuProfSession handle,
            Psp2GpuProfSample *sample);
int gp_end(struct gp_session *s, int32_t pid, Psp2GpuProfSession handle);
/* Hook-only: guard held; powered means the DRIVER guarantees accessible MMIO.
 * Never acquires the driver power lock (the hook may already hold it).
 */
void gp_invalidate(struct gp_session *s, int powered);
int gp_signals(struct gp_session *s, const Psp2GpuProfSignalConfig *config,
               Psp2GpuProfSignals *signals);

#endif
