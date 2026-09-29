/* Integration fragment: call on the GL-owning thread with existing GL state.
 * Compile submission_capture.c with GPUPROF_EXAMPLE_VITAGL and call its start
 * before vglInit. This example does not initialize or change render targets. */
#include <vitaGL.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2_gpuprof_labels.h>

extern Psp2GpuProfCapture *submission_capture_buffer(void);

void example_gl_labels_init(Psp2GpuProfLabels *labels) {
    psp2GpuProfLabelsBegin(labels,submission_capture_buffer(),
                           (uint32_t)sceKernelGetThreadId());
}

/* vertex attributes, textures, uniforms and target must already be set up. */
void example_gl_draw(Psp2GpuProfLabels *labels, uint64_t frame, GLuint program,
                     GLsizei vertices) {
    psp2GpuProfFrameName(labels,sceKernelGetSystemTimeWide(),frame,"Main frame");
    psp2GpuProfShaderName(labels,sceKernelGetSystemTimeWide(),3,0,program,"Surface shader");
    int scope=psp2GpuProfScopePush(labels,sceKernelGetSystemTimeWide(),"Opaque geometry");
    psp2GpuProfDrawName(labels,sceKernelGetSystemTimeWide(),"Surface mesh");
    glUseProgram(program);
    glDrawArrays(GL_TRIANGLES,0,vertices);
    psp2GpuProfDrawName(labels,sceKernelGetSystemTimeWide(),NULL);
    if (!scope) psp2GpuProfScopePop(labels,sceKernelGetSystemTimeWide());
}
