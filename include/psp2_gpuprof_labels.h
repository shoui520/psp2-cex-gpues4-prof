#ifndef PSP2_GPUPROF_LABELS_H
#define PSP2_GPUPROF_LABELS_H
#include "psp2_gpuprof_capture.h"
#ifdef __cplusplus
extern "C" {
#endif

#define PSP2_GPUPROF_SCOPE_DEPTH 32u
/* One instance per producer thread, initialized once per capture. Thread IDs
 * must distinguish thread lifetimes. Shared captures require mutex callbacks.
 * No labels are process-global; scopes can nest across GXM scene boundaries. */
typedef struct Psp2GpuProfLabels {
    Psp2GpuProfCapture *capture;
    uint64_t thread, frame, next_scope;
    uint64_t scopes[PSP2_GPUPROF_SCOPE_DEPTH];
    unsigned depth;
} Psp2GpuProfLabels;

int psp2GpuProfLabelsBegin(Psp2GpuProfLabels *, Psp2GpuProfCapture *, uint64_t thread);
int psp2GpuProfFrameName(Psp2GpuProfLabels *, uint64_t time_us, uint64_t frame, const char *);
int psp2GpuProfScopePush(Psp2GpuProfLabels *, uint64_t time_us, const char *);
int psp2GpuProfScopePop(Psp2GpuProfLabels *, uint64_t time_us);
/* Persistent on this thread until replaced (NULL clears). */
int psp2GpuProfDrawName(Psp2GpuProfLabels *, uint64_t time_us, const char *);
/* Raw shader handle in the selected namespace. Applies to current lifetime,
 * never all future objects at that address. 1=GXM vertex, 2=GXM fragment,
 * 3=vitaGL program. Patcher is required for GXM, zero for vitaGL. */
int psp2GpuProfShaderName(Psp2GpuProfLabels *, uint64_t time_us,
                         unsigned space, uintptr_t patcher, uintptr_t handle,
                         const char *);
#ifdef __cplusplus
}
#endif
#endif
