#ifndef PSP2_GPUPROF_IDENTITY_H
#define PSP2_GPUPROF_IDENTITY_H
#include "psp2_gpuprof.h"
#include "psp2_gpuprof_attribution.h"
#include "psp2_gpuprof_work.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Experimental software mirror of the retail 3.65 GXM display/scene counters.
 * NOT evidence of atomic GPU ownership or a measured execution duration.
 * One tracker for the entire process, not one per context. Caller serializes
 * all tracked calls. Requires known initial counters and complete interception
 * of immediate BeginScene/EndScene and DisplayQueueAddEntry calls. Unsupported
 * APIs, missing hooks, reinitialization or concurrent calls require Invalidate.
 * No opaque GXM memory is accessed. epoch is a caller-owned capture generation;
 * it is not present in firmware observations and must NOT disambiguate reused
 * hardware IDs without an independently bounded capture lifetime.
 */
typedef struct Psp2GpuProfIdentity {
    uint64_t epoch, context;
    uint32_t pid, frame, scene;
} Psp2GpuProfIdentity;

typedef struct Psp2GpuProfIdentityTracker {
    Psp2GpuProfIdentity next;
    uint32_t valid, open;
} Psp2GpuProfIdentityTracker;

/* Seed only at a known, drained boundary with no open scene. A guessed offset
 * is not a known seed. frame must fit 24 bits; context IDs must be nonzero. */
int psp2GpuProfIdentitySeed(Psp2GpuProfIdentityTracker *t, uint64_t epoch,
                          uint32_t pid, uint32_t frame, uint32_t scene);
void psp2GpuProfIdentityInvalidate(Psp2GpuProfIdentityTracker *t);
/* Call after each real API returns. Any nonzero Begin/End/Display result
 * invalidates: EndScene can advance its counter before a final-helper failure.
 * Identity is delivered only after successful End, never before submission.
 * Hardware counter wrap invalidates instead of silently reusing an identity.
 */
int psp2GpuProfIdentityBegin(Psp2GpuProfIdentityTracker *t, uint64_t context, int result);
int psp2GpuProfIdentityEnd(Psp2GpuProfIdentityTracker *t, uint64_t context, int result,
                         Psp2GpuProfIdentity *out);
int psp2GpuProfIdentityDisplay(Psp2GpuProfIdentityTracker *t, int result);

/* complete_order is a caller assertion that every supported immediate draw
 * was recorded, with no overflow, unsupported draw APIs or untracked flushes.
 * Lifetime is a conservative CPU-clock envelope from before submission to
 * after observed GPU completion, NOT CPU EndScene return or GPU duration.
 */
typedef struct Psp2GpuProfIdentifiedScene {
    Psp2GpuProfIdentity identity;
    uint64_t begin_us, complete_us;
    const Psp2GpuProfDrawLabel *draws;
    uint32_t draw_count, complete_order;
} Psp2GpuProfIdentifiedScene;

enum Psp2GpuProfIdentityMatchStatus {
    PSP2_GPUPROF_MATCH_CANDIDATE,
    PSP2_GPUPROF_MATCH_INACTIVE,
    PSP2_GPUPROF_MATCH_CHANGED,
    PSP2_GPUPROF_MATCH_FOREIGN,
    PSP2_GPUPROF_MATCH_MISSING,
    PSP2_GPUPROF_MATCH_COLLISION,
    PSP2_GPUPROF_MATCH_BOUNDARY,
    PSP2_GPUPROF_MATCH_ORDER,
    PSP2_GPUPROF_MATCH_BATCH
};
typedef struct Psp2GpuProfIdentityMatch {
    uint32_t status, source_flags;
    const Psp2GpuProfIdentifiedScene *scene;
    const Psp2GpuProfDrawLabel *draw;
} Psp2GpuProfIdentityMatch;

/* All matching PID/frame/scene keys are considered, even across different
 * epochs/contexts: firmware observations cannot distinguish those. A duplicate
 * key rejects rather than picking one. Retains source UNVERIFIED flags; a
 * candidate is NOT an atomic ownership proof, cost or utilization estimate.
 * Pass successful ReadWork output only. Invalid inputs return INVALID; zero
 * return means inspect status, not that attribution succeeded. No aliasing
 * between output and input objects; metadata must remain immutable.
 */
int psp2GpuProfMatchWork(const Psp2GpuProfIdentifiedScene *scenes, uint32_t count,
                       uint32_t pid, uint64_t before_us, uint64_t after_us,
                       const Psp2GpuProfWorkCore *work, Psp2GpuProfIdentityMatch *out);

#ifdef __cplusplus
}
#endif
#endif
