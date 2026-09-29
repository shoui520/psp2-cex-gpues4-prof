#include "psp2_gpuprof_vitagl.h"
#include "psp2_gpuprof_gxm_observer.h"
#include <vitaGL.h>
#include <psp2/kernel/threadmgr.h>
#include <assert.h>

static Psp2GpuProfCapture capture;
static Psp2GpuProfCaptureEvent rows[64];
static unsigned calls;
static int indices;
SceUID sceKernelGetThreadId(void) { return 7; }
SceInt64 sceKernelGetSystemTimeWide(void) { static SceInt64 t; return ++t; }
GLuint __real_glCreateProgram(void) { ++calls; return 42; }
void __real_glUseProgram(GLuint p) { assert(p==42); ++calls; }
void __real_glDeleteProgram(GLuint p) { assert(p==42); ++calls; }
void __real_glDrawArrays(GLenum mode,GLint first,GLsizei count) {
    assert(mode==GL_TRIANGLES && first==3 && count==6); ++calls;
    /* A GL implementation may split one request into several GXM draws. */
    for (unsigned i=0;i<2;++i) {
        Psp2GpuProfGxmCall c={.kind=PSP2_GPUPROF_GXM_DRAW,.thread=7,.context=123};
        c.before_us=sceKernelGetSystemTimeWide();
        c.after_us=sceKernelGetSystemTimeWide();
        psp2GpuProfCaptureGxmCall(&capture,&c);
    }
}
void __real_glDrawElements(GLenum mode,GLsizei count,GLenum type,const void *p) {
    assert(mode==GL_TRIANGLES && count==6 && type==GL_UNSIGNED_SHORT && p==&indices); ++calls;
}
void __real_glDrawRangeElements(GLenum mode,GLuint start,GLuint end,GLsizei count,GLenum type,const void *p) {
    assert(start==1 && end==9); __real_glDrawElements(mode,count,type,p);
}
void __real_glDrawArraysInstanced(GLenum mode,GLint first,GLsizei count,GLsizei instances) {
    assert(mode==GL_TRIANGLES && first==3 && count==6 && instances==4); ++calls;
}
void __real_glDrawElementsInstanced(GLenum mode,GLsizei count,GLenum type,const void *p,GLsizei instances) {
    assert(instances==4); __real_glDrawElements(mode,count,type,p);
}
void __real_glDrawElementsBaseVertex(GLenum mode,GLsizei count,GLenum type,const void *p,GLint base) {
    assert(base==-2); __real_glDrawElements(mode,count,type,p);
}
void __real_glDrawRangeElementsBaseVertex(GLenum mode,GLuint start,GLuint end,GLsizei count,GLenum type,void *p,GLint base) {
    assert(base==-2); __real_glDrawRangeElements(mode,start,end,count,type,p);
}
void __real_vglSwapBuffers(GLboolean dialog) { assert(dialog==GL_TRUE); ++calls; }
int main(void) {
    Psp2GpuProfCaptureConfig cfg={rows,64,NULL,NULL,NULL};
    assert(!psp2GpuProfCaptureInit(&capture,&cfg));
    assert(!psp2GpuProfCaptureStart(&capture));
    psp2GpuProfVitaGLCapture(&capture);
    assert(glCreateProgram()==42);
    assert(rows[0].args[7]==0 && rows[1].args[7]==1 && rows[1].args[0]==42);
    glUseProgram(42);
    glDrawArrays(GL_TRIANGLES,3,6);
    assert(rows[4].kind==PSP2_GPUPROF_EVENT_VITAGL_CALL && rows[4].args[7]==0);
    assert(rows[5].kind==PSP2_GPUPROF_EVENT_GXM_CALL);
    assert(rows[6].kind==PSP2_GPUPROF_EVENT_GXM_CALL);
    assert(rows[7].kind==PSP2_GPUPROF_EVENT_VITAGL_CALL && rows[7].args[7]==1);
    assert(rows[7].before_us==rows[4].before_us && rows[7].after_us>rows[6].after_us);
    glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_SHORT,&indices);
    glDrawRangeElements(GL_TRIANGLES,1,9,6,GL_UNSIGNED_SHORT,&indices);
    glDrawArraysInstanced(GL_TRIANGLES,3,6,4);
    glDrawElementsInstanced(GL_TRIANGLES,6,GL_UNSIGNED_SHORT,&indices,4);
    glDrawElementsBaseVertex(GL_TRIANGLES,6,GL_UNSIGNED_SHORT,&indices,-2);
    assert(rows[capture.count-1].args[4]==(uint64_t)(int64_t)-2);
    glDrawRangeElementsBaseVertex(GL_TRIANGLES,1,9,6,GL_UNSIGNED_SHORT,&indices,-2);
    vglSwapBuffers(GL_TRUE);
    glDeleteProgram(42);
    assert(calls==11 && capture.count==24);
    psp2GpuProfVitaGLCapture(NULL);
    glUseProgram(42);
    assert(calls==12 && capture.count==24);
    assert(!psp2GpuProfCaptureStop(&capture));
    return 0;
}
