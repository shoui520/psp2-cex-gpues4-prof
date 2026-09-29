#ifndef PSP2_GPUPROF_VITAGL_H
#define PSP2_GPUPROF_VITAGL_H
#include "psp2_gpuprof_capture.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Configure at a graphics-producer quiescent point. Use alongside the GXM
 * observer: GL calls provide application intent, GXM calls actual submissions.
 * NULL disables. This adapter never calls glGetError, finishes, or flushes. */
void psp2GpuProfVitaGLCapture(Psp2GpuProfCapture *);
typedef enum Psp2GpuProfVitaGLCall {
    PSP2_GPUPROF_GL_CREATE_PROGRAM = 1,
    PSP2_GPUPROF_GL_DELETE_PROGRAM,
    PSP2_GPUPROF_GL_USE_PROGRAM,
    PSP2_GPUPROF_GL_DRAW_ARRAYS,
    PSP2_GPUPROF_GL_DRAW_ELEMENTS,
    PSP2_GPUPROF_GL_DRAW_RANGE_ELEMENTS,
    PSP2_GPUPROF_GL_SWAP,
    PSP2_GPUPROF_GL_DRAW_ARRAYS_INSTANCED,
    PSP2_GPUPROF_GL_DRAW_ELEMENTS_INSTANCED,
    PSP2_GPUPROF_GL_DRAW_ELEMENTS_BASE_VERTEX,
    PSP2_GPUPROF_GL_DRAW_RANGE_ELEMENTS_BASE_VERTEX
} Psp2GpuProfVitaGLCall;
#ifdef __cplusplus
}
#endif
#endif
