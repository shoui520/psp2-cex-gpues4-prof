#include "psp2_gpuprof_vitagl.h"
#include <vitaGL.h>
#include <psp2/kernel/threadmgr.h>

static Psp2GpuProfCapture *capture;
void psp2GpuProfVitaGLCapture(Psp2GpuProfCapture *c) { capture=c; }
static Psp2GpuProfCaptureEvent begin(unsigned kind) {
    Psp2GpuProfCaptureEvent e={0};
    e.kind=PSP2_GPUPROF_EVENT_VITAGL_CALL;
    e.object=kind;
    e.thread=(uint32_t)sceKernelGetThreadId();
    e.before_us=e.after_us=sceKernelGetSystemTimeWide();
    return e;
}
static void enter(Psp2GpuProfCaptureEvent *e) {
    psp2GpuProfCaptureRecord(capture,e,NULL);
}
static void leave(Psp2GpuProfCaptureEvent *e) {
    e->after_us=sceKernelGetSystemTimeWide();
    e->args[7]=1;
    psp2GpuProfCaptureRecord(capture,e,NULL);
}

GLuint __real_glCreateProgram(void);
GLuint __wrap_glCreateProgram(void) {
    if (!capture) return __real_glCreateProgram();
    Psp2GpuProfCaptureEvent e=begin(PSP2_GPUPROF_GL_CREATE_PROGRAM);
    enter(&e);
    GLuint p=__real_glCreateProgram();
    e.args[0]=p; leave(&e); return p;
}
#define PROGRAM_WRAPPER(Name, Kind) \
void __real_##Name(GLuint); \
void __wrap_##Name(GLuint p) { \
    if (!capture) { __real_##Name(p); return; } \
    Psp2GpuProfCaptureEvent e=begin(Kind); e.args[0]=p; enter(&e); \
    __real_##Name(p); leave(&e); \
}
PROGRAM_WRAPPER(glUseProgram,PSP2_GPUPROF_GL_USE_PROGRAM)
PROGRAM_WRAPPER(glDeleteProgram,PSP2_GPUPROF_GL_DELETE_PROGRAM)

void __real_glDrawArrays(GLenum,GLint,GLsizei);
void __wrap_glDrawArrays(GLenum mode,GLint first,GLsizei count) {
    if (!capture) { __real_glDrawArrays(mode,first,count); return; }
    Psp2GpuProfCaptureEvent e=begin(PSP2_GPUPROF_GL_DRAW_ARRAYS);
    e.args[0]=mode; e.args[1]=(uint64_t)(int64_t)first; e.args[2]=(uint64_t)(int64_t)count;
    enter(&e); __real_glDrawArrays(mode,first,count); leave(&e);
}
void __real_glDrawElements(GLenum,GLsizei,GLenum,const GLvoid *);
void __wrap_glDrawElements(GLenum mode,GLsizei count,GLenum type,const GLvoid *indices) {
    if (!capture) { __real_glDrawElements(mode,count,type,indices); return; }
    Psp2GpuProfCaptureEvent e=begin(PSP2_GPUPROF_GL_DRAW_ELEMENTS);
    e.args[0]=mode; e.args[1]=(uint64_t)(int64_t)count; e.args[2]=type; e.args[3]=(uintptr_t)indices;
    enter(&e); __real_glDrawElements(mode,count,type,indices); leave(&e);
}
void __real_glDrawRangeElements(GLenum,GLuint,GLuint,GLsizei,GLenum,const void *);
void __wrap_glDrawRangeElements(GLenum mode,GLuint start,GLuint end,GLsizei count,GLenum type,const void *indices) {
    if (!capture) { __real_glDrawRangeElements(mode,start,end,count,type,indices); return; }
    Psp2GpuProfCaptureEvent e=begin(PSP2_GPUPROF_GL_DRAW_RANGE_ELEMENTS);
    e.args[0]=mode; e.args[1]=start; e.args[2]=end; e.args[3]=(uint64_t)(int64_t)count;
    e.args[4]=type; e.args[5]=(uintptr_t)indices;
    enter(&e); __real_glDrawRangeElements(mode,start,end,count,type,indices); leave(&e);
}
void __real_glDrawArraysInstanced(GLenum,GLint,GLsizei,GLsizei);
void __wrap_glDrawArraysInstanced(GLenum mode,GLint first,GLsizei count,GLsizei instances) {
    if (!capture) { __real_glDrawArraysInstanced(mode,first,count,instances); return; }
    Psp2GpuProfCaptureEvent e=begin(PSP2_GPUPROF_GL_DRAW_ARRAYS_INSTANCED);
    e.args[0]=mode; e.args[1]=(uint64_t)(int64_t)first; e.args[2]=(uint64_t)(int64_t)count;
    e.args[3]=(uint64_t)(int64_t)instances;
    enter(&e); __real_glDrawArraysInstanced(mode,first,count,instances); leave(&e);
}
#define ELEMENTS_EXTRA(Name,Kind) \
void __real_##Name(GLenum,GLsizei,GLenum,const void *,GLint); \
void __wrap_##Name(GLenum mode,GLsizei count,GLenum type,const void *indices,GLint extra) { \
    if (!capture) { __real_##Name(mode,count,type,indices,extra); return; } \
    Psp2GpuProfCaptureEvent e=begin(Kind); \
    e.args[0]=mode; e.args[1]=(uint64_t)(int64_t)count; e.args[2]=type; \
    e.args[3]=(uintptr_t)indices; e.args[4]=(uint64_t)(int64_t)extra; \
    enter(&e); __real_##Name(mode,count,type,indices,extra); leave(&e); \
}
ELEMENTS_EXTRA(glDrawElementsInstanced,PSP2_GPUPROF_GL_DRAW_ELEMENTS_INSTANCED)
ELEMENTS_EXTRA(glDrawElementsBaseVertex,PSP2_GPUPROF_GL_DRAW_ELEMENTS_BASE_VERTEX)
void __real_glDrawRangeElementsBaseVertex(GLenum,GLuint,GLuint,GLsizei,GLenum,void *,GLint);
void __wrap_glDrawRangeElementsBaseVertex(GLenum mode,GLuint start,GLuint end,GLsizei count,GLenum type,void *indices,GLint base) {
    if (!capture) { __real_glDrawRangeElementsBaseVertex(mode,start,end,count,type,indices,base); return; }
    Psp2GpuProfCaptureEvent e=begin(PSP2_GPUPROF_GL_DRAW_RANGE_ELEMENTS_BASE_VERTEX);
    e.args[0]=mode; e.args[1]=start; e.args[2]=end; e.args[3]=(uint64_t)(int64_t)count;
    e.args[4]=type; e.args[5]=(uintptr_t)indices; e.args[6]=(uint64_t)(int64_t)base;
    enter(&e); __real_glDrawRangeElementsBaseVertex(mode,start,end,count,type,indices,base); leave(&e);
}
void __real_vglSwapBuffers(GLboolean);
void __wrap_vglSwapBuffers(GLboolean dialog) {
    if (!capture) { __real_vglSwapBuffers(dialog); return; }
    Psp2GpuProfCaptureEvent e=begin(PSP2_GPUPROF_GL_SWAP);
    e.args[0]=dialog;
    enter(&e); __real_vglSwapBuffers(dialog); leave(&e);
}
