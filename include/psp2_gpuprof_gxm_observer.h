#ifndef PSP2_GPUPROF_GXM_OBSERVER_H
#define PSP2_GPUPROF_GXM_OBSERVER_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum Psp2GpuProfGxmCallKind {
    PSP2_GPUPROF_GXM_BEGIN = 1,
    PSP2_GPUPROF_GXM_END,
    PSP2_GPUPROF_GXM_FLUSH,
    PSP2_GPUPROF_GXM_DRAW,
    PSP2_GPUPROF_GXM_DRAW_INSTANCED,
    PSP2_GPUPROF_GXM_DRAW_PRECOMPUTED,
    PSP2_GPUPROF_GXM_VERTEX_BIND,
    PSP2_GPUPROF_GXM_FRAGMENT_BIND,
    PSP2_GPUPROF_GXM_CONTEXT_CREATE,
    PSP2_GPUPROF_GXM_CONTEXT_DESTROY,
    PSP2_GPUPROF_GXM_VERTEX_CREATE,
    PSP2_GPUPROF_GXM_FRAGMENT_CREATE,
    PSP2_GPUPROF_GXM_VERTEX_ADDREF,
    PSP2_GPUPROF_GXM_FRAGMENT_ADDREF,
    PSP2_GPUPROF_GXM_VERTEX_RELEASE,
    PSP2_GPUPROF_GXM_FRAGMENT_RELEASE,
    PSP2_GPUPROF_GXM_PATCHER_DESTROY,
    PSP2_GPUPROF_GXM_PRESENT,
    PSP2_GPUPROF_GXM_TRANSFER_COPY,
    PSP2_GPUPROF_GXM_TRANSFER_FILL,
    PSP2_GPUPROF_GXM_TRANSFER_DOWNSCALE,
    PSP2_GPUPROF_GXM_TRANSFER_FINISH,
    PSP2_GPUPROF_GXM_FRONT_FRAGMENT_ENABLE,
    PSP2_GPUPROF_GXM_BACK_FRAGMENT_ENABLE,
    PSP2_GPUPROF_GXM_MASK_FRAGMENT_CREATE
} Psp2GpuProfGxmCallKind;

/* Raw call observation, not a GPU duration or hardware draw identity.
 * Addresses identify live objects only; the capture adapter must assign
 * generations. Callback execution may be concurrent on different threads.
 * This value is stack-owned and must be copied before the callback returns. */
typedef struct Psp2GpuProfGxmCall {
    uint64_t before_us, after_us;
    uintptr_t context;
    uint32_t thread, kind;
    int32_t result;
    uintptr_t args[8];
} Psp2GpuProfGxmCall;

typedef void (*Psp2GpuProfGxmObserver)(void *, const Psp2GpuProfGxmCall *);
/* Configure only while every graphics producer is quiescent. NULL disables.
 * The callback must not call GXM. No waits or notification writes are added.
 * Install before GXM initialization to observe complete object lifetimes.
 * Transfer records contain summary metadata, not replayable commands. */
void psp2GpuProfGxmSetObserver(Psp2GpuProfGxmObserver, void *userdata);

/* Capture observer callback: userdata is Psp2GpuProfCapture*. Emits raw call
 * records, not attributed/timed GPU work. Capture must be recording. */
void psp2GpuProfCaptureGxmCall(void *userdata, const Psp2GpuProfGxmCall *);

#ifdef __cplusplus
}
#endif
#endif
