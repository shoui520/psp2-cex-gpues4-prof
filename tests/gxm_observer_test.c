#include "psp2_gpuprof_gxm_observer.h"
#include <psp2/gxm.h>
#include <psp2/kernel/threadmgr.h>
#include <assert.h>
#include <string.h>

static Psp2GpuProfGxmCall seen;
static unsigned calls, observed;
static SceGxmContext *context = (SceGxmContext *)(uintptr_t)0x1234;
static SceGxmNotification vn, fn;
SceUID sceKernelGetThreadId(void) { return 71; }
SceInt64 sceKernelGetSystemTimeWide(void) { static SceInt64 tick; return ++tick; }
static void observe(void *data, const Psp2GpuProfGxmCall *c) {
    assert(data == &seen);
    assert(calls == observed + 1);
    assert(c->thread == 71);
    assert(!c->context || c->context == (uintptr_t)context);
    assert(c->before_us < c->after_us);
    seen = *c;
    ++observed;
}
int __real_sceGxmDraw(SceGxmContext *c, SceGxmPrimitiveType p, SceGxmIndexFormat f,
                     const void *i, unsigned n) {
    assert(c == context && p == SCE_GXM_PRIMITIVE_TRIANGLES);
    assert(f == SCE_GXM_INDEX_FORMAT_U16 && i == &seen && n == 17);
    ++calls; return -123;
}
int __real_sceGxmDrawInstanced(SceGxmContext *c, SceGxmPrimitiveType p, SceGxmIndexFormat f,
                              const void *i, unsigned n, unsigned wrap) {
    assert(wrap == 5); return __real_sceGxmDraw(c,p,f,i,n);
}
int __real_sceGxmDrawPrecomputed(SceGxmContext *c, const SceGxmPrecomputedDraw *p) {
    assert(c == context && p == (void *)&seen); ++calls; return 0;
}
void __real_sceGxmSetVertexProgram(SceGxmContext *c, const SceGxmVertexProgram *p) {
    assert(c == context && p == (void *)&seen); ++calls;
}
void __real_sceGxmSetFragmentProgram(SceGxmContext *c, const SceGxmFragmentProgram *p) {
    assert(c == context && p == (void *)&seen); ++calls;
}
void __real_sceGxmSetFrontFragmentProgramEnable(SceGxmContext *c, SceGxmFragmentProgramMode mode) {
    assert(c == context && mode == SCE_GXM_FRAGMENT_PROGRAM_DISABLED); ++calls;
}
void __real_sceGxmSetBackFragmentProgramEnable(SceGxmContext *c, SceGxmFragmentProgramMode mode) {
    assert(c == context && mode == SCE_GXM_FRAGMENT_PROGRAM_ENABLED); ++calls;
}
int __real_sceGxmBeginScene(SceGxmContext *c, unsigned flags, const SceGxmRenderTarget *rt,
    const SceGxmValidRegion *region, SceGxmSyncObject *v, SceGxmSyncObject *f,
    const SceGxmColorSurface *color, const SceGxmDepthStencilSurface *depth) {
    assert(c == context && flags == 42 && rt == (void *)&seen);
    assert(!region && !v && !f && !color && !depth); ++calls; return 0;
}
int __real_sceGxmEndScene(SceGxmContext *c, const SceGxmNotification *v, const SceGxmNotification *f) {
    assert(c == context && v == &vn && f == &fn); ++calls; return -7;
}
int __real_sceGxmMidSceneFlush(SceGxmContext *c, unsigned flags, SceGxmSyncObject *s,
                             const SceGxmNotification *v) {
    assert(c == context && flags == 9 && !s && v == &vn); ++calls; return 0;
}
int __real_sceGxmCreateContext(const SceGxmContextParams *params, SceGxmContext **out) {
    assert(!params); *out = context; ++calls; return 0;
}
int __real_sceGxmDestroyContext(SceGxmContext *c) {
    assert(c == context); ++calls; return 0;
}
int __real_sceGxmShaderPatcherCreateVertexProgram(SceGxmShaderPatcher *p, SceGxmShaderPatcherId id,
    const SceGxmVertexAttribute *a, unsigned na, const SceGxmVertexStream *s, unsigned ns, SceGxmVertexProgram **out) {
    assert(!p && !id && !a && !na && !s && !ns);
    *out = (void *)&seen; ++calls; return 0;
}
int __real_sceGxmShaderPatcherCreateFragmentProgram(SceGxmShaderPatcher *p, SceGxmShaderPatcherId id,
    SceGxmOutputRegisterFormat fmt, SceGxmMultisampleMode ms, const SceGxmBlendInfo *blend,
    const SceGxmProgram *vp, SceGxmFragmentProgram **out) {
    assert(!p && !id && !fmt && !ms && !blend && !vp);
    *out = (void *)&seen; ++calls; return 0;
}
#define REF_MOCK(Name, Type) \
int __real_##Name(SceGxmShaderPatcher *p, Type *program) { \
    assert(!p && program == (void *)&seen); ++calls; return 0; \
}
int __real_sceGxmShaderPatcherCreateMaskUpdateFragmentProgram(SceGxmShaderPatcher *p, SceGxmFragmentProgram **out) {
    assert(!p); *out = (void *)&seen; ++calls; return 0;
}
REF_MOCK(sceGxmShaderPatcherAddRefVertexProgram,SceGxmVertexProgram)
REF_MOCK(sceGxmShaderPatcherAddRefFragmentProgram,SceGxmFragmentProgram)
REF_MOCK(sceGxmShaderPatcherReleaseVertexProgram,SceGxmVertexProgram)
REF_MOCK(sceGxmShaderPatcherReleaseFragmentProgram,SceGxmFragmentProgram)
int __real_sceGxmShaderPatcherDestroy(SceGxmShaderPatcher *p) {
    assert(!p); ++calls; return 0;
}
int __real_sceGxmDisplayQueueAddEntry(SceGxmSyncObject *a,SceGxmSyncObject *b,const void *data) {
    assert(!a && !b && data==&seen); ++calls; return 0;
}
int __real_sceGxmTransferCopy(uint32_t w,uint32_t h,uint32_t key,uint32_t mask,SceGxmTransferColorKeyMode mode,
    SceGxmTransferFormat sf,SceGxmTransferType st,const void *src,uint32_t sx,uint32_t sy,int32_t ss,
    SceGxmTransferFormat df,SceGxmTransferType dt,void *dst,uint32_t dx,uint32_t dy,int32_t ds,
    SceGxmSyncObject *sync,uint32_t flags,const SceGxmNotification *n) {
    assert(w==16 && h==32 && key==7 && mask==8 && mode==0 && sf==0 && st==0);
    assert(src==&seen && sx==1 && sy==2 && ss==128 && df==0 && dt==0);
    assert(dst==&vn && dx==3 && dy==4 && ds==256 && !sync && flags==9 && n==&fn);
    ++calls; return -12;
}
int __real_sceGxmTransferFill(uint32_t color,SceGxmTransferFormat fmt,void *dst,uint32_t x,uint32_t y,uint32_t w,uint32_t h,int32_t stride,SceGxmSyncObject *sync,uint32_t flags,const SceGxmNotification *n) {
    assert(color==7 && fmt==0 && dst==&vn && x==1 && y==2 && w==16 && h==32);
    assert(stride==128 && !sync && flags==9 && n==&fn); ++calls; return 0;
}
int __real_sceGxmTransferDownscale(SceGxmTransferFormat sf,const void *src,unsigned sx,unsigned sy,unsigned w,unsigned h,int ss,
    SceGxmTransferFormat df,void *dst,unsigned dx,unsigned dy,int ds,SceGxmSyncObject *sync,unsigned flags,const SceGxmNotification *n) {
    assert(sf==0 && src==&seen && sx==1 && sy==2 && w==16 && h==32 && ss==128);
    assert(df==0 && dst==&vn && dx==3 && dy==4 && ds==256 && !sync && flags==9 && n==&fn);
    ++calls; return 0;
}
int __real_sceGxmTransferFinish(void) { ++calls; return 0; }
int main(void) {
    psp2GpuProfGxmSetObserver(observe, &seen);
    assert(!sceGxmBeginScene(context,42,(void *)&seen,NULL,NULL,NULL,NULL,NULL));
    assert(seen.kind == PSP2_GPUPROF_GXM_BEGIN && seen.args[0] == 42);
    sceGxmSetVertexProgram(context,(void *)&seen);
    assert(seen.kind == PSP2_GPUPROF_GXM_VERTEX_BIND);
    sceGxmSetFragmentProgram(context,(void *)&seen);
    assert(seen.kind == PSP2_GPUPROF_GXM_FRAGMENT_BIND);
    assert(sceGxmDraw(context,SCE_GXM_PRIMITIVE_TRIANGLES,SCE_GXM_INDEX_FORMAT_U16,&seen,17) == -123);
    assert(seen.kind == PSP2_GPUPROF_GXM_DRAW && seen.result == -123 && seen.args[3] == 17);
    assert(sceGxmDrawInstanced(context,SCE_GXM_PRIMITIVE_TRIANGLES,SCE_GXM_INDEX_FORMAT_U16,&seen,17,5) == -123);
    assert(seen.kind == PSP2_GPUPROF_GXM_DRAW_INSTANCED && seen.args[4] == 5);
    assert(!sceGxmDrawPrecomputed(context,(void *)&seen));
    assert(seen.kind == PSP2_GPUPROF_GXM_DRAW_PRECOMPUTED);
    vn.value = 0x12345678; fn.value = 0xabcdef;
    assert(!sceGxmMidSceneFlush(context,9,NULL,&vn));
    assert(seen.kind == PSP2_GPUPROF_GXM_FLUSH);
    assert(sceGxmEndScene(context,&vn,&fn) == -7);
    assert(seen.kind == PSP2_GPUPROF_GXM_END && seen.result == -7);
    assert(vn.value == 0x12345678 && fn.value == 0xabcdef);
    assert(observed == 8 && calls == 8);
    SceGxmContext *created = NULL;
    assert(!sceGxmCreateContext(NULL,&created) && created == context);
    assert(seen.kind == PSP2_GPUPROF_GXM_CONTEXT_CREATE);
    assert(!sceGxmDestroyContext(created));
    SceGxmVertexProgram *vp = NULL;
    SceGxmFragmentProgram *fp = NULL;
    assert(!sceGxmShaderPatcherCreateVertexProgram(NULL,0,NULL,0,NULL,0,&vp));
    assert(seen.args[2] == (uintptr_t)vp);
    assert(!sceGxmShaderPatcherCreateFragmentProgram(NULL,0,0,0,NULL,NULL,&fp));
    assert(seen.args[2] == (uintptr_t)fp);
    assert(!sceGxmShaderPatcherAddRefVertexProgram(NULL,vp));
    assert(!sceGxmShaderPatcherAddRefFragmentProgram(NULL,fp));
    assert(!sceGxmShaderPatcherReleaseVertexProgram(NULL,vp));
    assert(!sceGxmShaderPatcherReleaseFragmentProgram(NULL,fp));
    assert(observed == 16 && calls == 16);
    assert(!sceGxmShaderPatcherDestroy(NULL));
    assert(seen.kind==PSP2_GPUPROF_GXM_PATCHER_DESTROY);
    assert(!sceGxmDisplayQueueAddEntry(NULL,NULL,&seen));
    assert(seen.kind==PSP2_GPUPROF_GXM_PRESENT);
    assert(sceGxmTransferCopy(16,32,7,8,0,0,0,&seen,1,2,128,0,0,&vn,3,4,256,NULL,9,&fn)==-12);
    assert(seen.kind==PSP2_GPUPROF_GXM_TRANSFER_COPY && seen.args[7]==(uintptr_t)&fn);
    assert(!sceGxmTransferFill(7,0,&vn,1,2,16,32,128,NULL,9,&fn));
    assert(seen.kind==PSP2_GPUPROF_GXM_TRANSFER_FILL);
    assert(!sceGxmTransferDownscale(0,&seen,1,2,16,32,128,0,&vn,3,4,256,NULL,9,&fn));
    assert(seen.kind==PSP2_GPUPROF_GXM_TRANSFER_DOWNSCALE);
    assert(!sceGxmTransferFinish());
    assert(seen.kind==PSP2_GPUPROF_GXM_TRANSFER_FINISH);
    sceGxmSetFrontFragmentProgramEnable(context,SCE_GXM_FRAGMENT_PROGRAM_DISABLED);
    assert(seen.kind==PSP2_GPUPROF_GXM_FRONT_FRAGMENT_ENABLE && seen.args[0]==0x200000);
    sceGxmSetBackFragmentProgramEnable(context,SCE_GXM_FRAGMENT_PROGRAM_ENABLED);
    assert(seen.kind==PSP2_GPUPROF_GXM_BACK_FRAGMENT_ENABLE && seen.args[0]==0);
    assert(!sceGxmShaderPatcherCreateMaskUpdateFragmentProgram(NULL,&fp));
    assert(seen.kind==PSP2_GPUPROF_GXM_MASK_FRAGMENT_CREATE && seen.args[2]==(uintptr_t)fp);
    psp2GpuProfGxmSetObserver(NULL,NULL);
    assert(sceGxmEndScene(context,&vn,&fn) == -7);
    assert(calls == 26 && observed == 25);
    return 0;
}
