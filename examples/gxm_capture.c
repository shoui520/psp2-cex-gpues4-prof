/* Integration fragment, not a renderer. The caller owns the active scene,
 * render state, resources and synchronization. Install the observer before
 * graphics initialization using submission_capture.c (or the state cache). */
#include <psp2/gxm.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2_gpuprof_labels.h>

extern Psp2GpuProfCapture *submission_capture_buffer(void);

/* One label stack per submitting thread. Call after capture starts. */
void example_gxm_labels_init(Psp2GpuProfLabels *labels) {
    psp2GpuProfLabelsBegin(labels, submission_capture_buffer(),
                          (uint32_t)sceKernelGetThreadId());
}

/* Call at your application's frame boundary, not for each draw. */
int example_gxm_frame(Psp2GpuProfLabels *labels, uint64_t frame) {
    return psp2GpuProfFrameName(labels, sceKernelGetSystemTimeWide(), frame,
                              "Main frame");
}

/* Substitute your existing draw and bindings here. Label failures never skip
 * rendering. Inspect the capture footer for dropped events. No GPU waits,
 * scene splits, notification writes or sampler calls are introduced. */
int example_gxm_draw(Psp2GpuProfLabels *labels, SceGxmContext *context,
    SceGxmShaderPatcher *patcher, const SceGxmVertexProgram *vertex,
    const SceGxmFragmentProgram *fragment, const void *indices, unsigned count) {
    psp2GpuProfShaderName(labels, sceKernelGetSystemTimeWide(), 1,
        (uintptr_t)patcher, (uintptr_t)vertex, "Mesh vertex shader");
    psp2GpuProfShaderName(labels, sceKernelGetSystemTimeWide(), 2,
        (uintptr_t)patcher, (uintptr_t)fragment, "Surface fragment shader");
    int scope = psp2GpuProfScopePush(labels, sceKernelGetSystemTimeWide(),
                                   "Opaque geometry");
    psp2GpuProfDrawName(labels, sceKernelGetSystemTimeWide(), "Surface mesh");
    sceGxmSetVertexProgram(context, vertex);
    sceGxmSetFragmentProgram(context, fragment);
    int result = sceGxmDraw(context, SCE_GXM_PRIMITIVE_TRIANGLES,
                           SCE_GXM_INDEX_FORMAT_U16, indices, count);
    psp2GpuProfDrawName(labels, sceKernelGetSystemTimeWide(), NULL);
    if (!scope) psp2GpuProfScopePop(labels, sceKernelGetSystemTimeWide());
    return result;
}
