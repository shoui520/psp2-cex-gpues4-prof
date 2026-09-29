#ifndef PSP2_GPUPROF_GXM_STATE_H
#define PSP2_GPUPROF_GXM_STATE_H
#include "psp2_gpuprof_capture.h"
#include "psp2_gpuprof_gxm_observer.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Application-owned bounded cache. Kind: 0 free, 1 context, 2 vertex, 3 fragment.
 * Internal fields are public only to permit static allocation. */
typedef struct Psp2GpuProfGxmObject {
    uintptr_t owner, handle;
    uint64_t generation, refs, after_us, vertex, fragment;
    unsigned kind, open;
    unsigned front_fragment, back_fragment; /* 0 unknown, 1 enabled, 2 disabled */
} Psp2GpuProfGxmObject;
typedef struct Psp2GpuProfGxmState {
    Psp2GpuProfGxmObject *objects;
    size_t capacity;
    uint64_t next_generation;
    unsigned incomplete;
    void (*lock)(void *);
    void (*unlock)(void *);
    void *userdata;
    Psp2GpuProfCapture *capture;
} Psp2GpuProfGxmState;
/* Install StateObserve before any GXM initialization. Mutex must be distinct
 * from the output recorder's mutex; callbacks must not reenter this cache.
 * NULL callbacks are valid only with externally serialized producers. */
int psp2GpuProfGxmStateInit(Psp2GpuProfGxmState *, Psp2GpuProfGxmObject *, size_t,
    void (*lock)(void *), void (*unlock)(void *), void *userdata);
void psp2GpuProfGxmStateObserve(void *, const Psp2GpuProfGxmCall *);
/* At a producer-quiescent point, after CaptureStart, snapshot current live
 * objects and attach the recorder. No active scene is allowed. GPU work is
 * NOT drained. Scratch must hold capacity+1 records and not alias output storage.
 * Failure does not attach or write partial snapshots. Pre-capture GPU work
 * remains untracked; the snapshot must not be used to invent its draw history. */
int psp2GpuProfGxmStateAttach(Psp2GpuProfGxmState *, Psp2GpuProfCapture *,
    Psp2GpuProfCaptureEvent *scratch, size_t count, uint64_t time_us);
/* Quiesce producers first. Keeps lifetime tracking active between captures. */
void psp2GpuProfGxmStateDetach(Psp2GpuProfGxmState *);
#ifdef __cplusplus
}
#endif
#endif
