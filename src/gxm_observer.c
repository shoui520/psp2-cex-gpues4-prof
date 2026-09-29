#include "psp2_gpuprof_gxm_observer.h"
#include <psp2/gxm.h>
#include <psp2/kernel/threadmgr.h>

static Psp2GpuProfGxmObserver observer;
static void *userdata;
void psp2GpuProfGxmSetObserver(Psp2GpuProfGxmObserver fn, void *data) {
    observer = fn;
    userdata = data;
}
static Psp2GpuProfGxmCall begin(unsigned kind, SceGxmContext *context) {
    Psp2GpuProfGxmCall c = {0};
    c.kind = kind;
    c.context = (uintptr_t)context;
    c.thread = (uint32_t)sceKernelGetThreadId();
    c.before_us = sceKernelGetSystemTimeWide();
    return c;
}
static int end(Psp2GpuProfGxmCall *c, int result) {
    c->after_us = sceKernelGetSystemTimeWide();
    c->result = result;
    observer(userdata, c);
    return result;
}

int __real_sceGxmDraw(SceGxmContext *, SceGxmPrimitiveType, SceGxmIndexFormat, const void *, unsigned);
int __wrap_sceGxmDraw(SceGxmContext *ctx, SceGxmPrimitiveType p,
                      SceGxmIndexFormat f, const void *indices, unsigned count) {
    if (!observer) return __real_sceGxmDraw(ctx, p, f, indices, count);
    Psp2GpuProfGxmCall c = begin(PSP2_GPUPROF_GXM_DRAW, ctx);
    c.args[0] = p; c.args[1] = f; c.args[2] = (uintptr_t)indices; c.args[3] = count;
    int rc = __real_sceGxmDraw(ctx, p, f, indices, count);
    return end(&c, rc);
}
int __real_sceGxmDrawInstanced(SceGxmContext *, SceGxmPrimitiveType, SceGxmIndexFormat, const void *, unsigned, unsigned);
int __wrap_sceGxmDrawInstanced(SceGxmContext *ctx, SceGxmPrimitiveType p,
                      SceGxmIndexFormat f, const void *indices, unsigned count, unsigned wrap) {
    if (!observer) return __real_sceGxmDrawInstanced(ctx, p, f, indices, count, wrap);
    Psp2GpuProfGxmCall c = begin(PSP2_GPUPROF_GXM_DRAW_INSTANCED, ctx);
    c.args[0] = p; c.args[1] = f; c.args[2] = (uintptr_t)indices;
    c.args[3] = count; c.args[4] = wrap;
    int rc = __real_sceGxmDrawInstanced(ctx, p, f, indices, count, wrap);
    return end(&c, rc);
}
int __real_sceGxmDrawPrecomputed(SceGxmContext *, const SceGxmPrecomputedDraw *);
int __wrap_sceGxmDrawPrecomputed(SceGxmContext *ctx, const SceGxmPrecomputedDraw *draw) {
    if (!observer) return __real_sceGxmDrawPrecomputed(ctx, draw);
    Psp2GpuProfGxmCall c = begin(PSP2_GPUPROF_GXM_DRAW_PRECOMPUTED, ctx);
    c.args[0] = (uintptr_t)draw;
    int rc = __real_sceGxmDrawPrecomputed(ctx, draw);
    return end(&c, rc);
}
void __real_sceGxmSetVertexProgram(SceGxmContext *, const SceGxmVertexProgram *);
void __wrap_sceGxmSetVertexProgram(SceGxmContext *ctx, const SceGxmVertexProgram *p) {
    if (!observer) { __real_sceGxmSetVertexProgram(ctx, p); return; }
    Psp2GpuProfGxmCall c = begin(PSP2_GPUPROF_GXM_VERTEX_BIND, ctx);
    c.args[0] = (uintptr_t)p;
    __real_sceGxmSetVertexProgram(ctx, p);
    end(&c, 0);
}
void __real_sceGxmSetFragmentProgram(SceGxmContext *, const SceGxmFragmentProgram *);
void __wrap_sceGxmSetFragmentProgram(SceGxmContext *ctx, const SceGxmFragmentProgram *p) {
    if (!observer) { __real_sceGxmSetFragmentProgram(ctx, p); return; }
    Psp2GpuProfGxmCall c = begin(PSP2_GPUPROF_GXM_FRAGMENT_BIND, ctx);
    c.args[0] = (uintptr_t)p;
    __real_sceGxmSetFragmentProgram(ctx, p);
    end(&c, 0);
}
#define FRAGMENT_ENABLE_WRAPPER(Face, Kind) \
void __real_sceGxmSet##Face##FragmentProgramEnable(SceGxmContext *, SceGxmFragmentProgramMode); \
void __wrap_sceGxmSet##Face##FragmentProgramEnable(SceGxmContext *ctx, SceGxmFragmentProgramMode mode) { \
    if (!observer) { __real_sceGxmSet##Face##FragmentProgramEnable(ctx, mode); return; } \
    Psp2GpuProfGxmCall c = begin(Kind, ctx); \
    c.args[0] = mode; \
    __real_sceGxmSet##Face##FragmentProgramEnable(ctx, mode); \
    end(&c, 0); \
}
FRAGMENT_ENABLE_WRAPPER(Front, PSP2_GPUPROF_GXM_FRONT_FRAGMENT_ENABLE)
FRAGMENT_ENABLE_WRAPPER(Back, PSP2_GPUPROF_GXM_BACK_FRAGMENT_ENABLE)
#undef FRAGMENT_ENABLE_WRAPPER

int __real_sceGxmBeginScene(SceGxmContext *, unsigned, const SceGxmRenderTarget *,
    const SceGxmValidRegion *, SceGxmSyncObject *, SceGxmSyncObject *,
    const SceGxmColorSurface *, const SceGxmDepthStencilSurface *);
int __wrap_sceGxmBeginScene(SceGxmContext *ctx, unsigned flags, const SceGxmRenderTarget *rt,
    const SceGxmValidRegion *region, SceGxmSyncObject *vs, SceGxmSyncObject *fs,
    const SceGxmColorSurface *color, const SceGxmDepthStencilSurface *depth) {
    if (!observer) return __real_sceGxmBeginScene(ctx, flags, rt, region, vs, fs, color, depth);
    Psp2GpuProfGxmCall c = begin(PSP2_GPUPROF_GXM_BEGIN, ctx);
    c.args[0] = flags; c.args[1] = (uintptr_t)rt; c.args[2] = (uintptr_t)region;
    c.args[3] = (uintptr_t)vs; c.args[4] = (uintptr_t)fs;
    c.args[5] = (uintptr_t)color; c.args[6] = (uintptr_t)depth;
    int rc = __real_sceGxmBeginScene(ctx, flags, rt, region, vs, fs, color, depth);
    return end(&c, rc);
}
int __real_sceGxmEndScene(SceGxmContext *, const SceGxmNotification *, const SceGxmNotification *);
int __wrap_sceGxmEndScene(SceGxmContext *ctx, const SceGxmNotification *v, const SceGxmNotification *f) {
    if (!observer) return __real_sceGxmEndScene(ctx, v, f);
    Psp2GpuProfGxmCall c = begin(PSP2_GPUPROF_GXM_END, ctx);
    c.args[0] = (uintptr_t)v; c.args[1] = (uintptr_t)f;
    int rc = __real_sceGxmEndScene(ctx, v, f);
    return end(&c, rc);
}
int __real_sceGxmMidSceneFlush(SceGxmContext *, unsigned, SceGxmSyncObject *, const SceGxmNotification *);
int __wrap_sceGxmMidSceneFlush(SceGxmContext *ctx, unsigned flags, SceGxmSyncObject *sync,
                             const SceGxmNotification *notification) {
    if (!observer) return __real_sceGxmMidSceneFlush(ctx, flags, sync, notification);
    Psp2GpuProfGxmCall c = begin(PSP2_GPUPROF_GXM_FLUSH, ctx);
    c.args[0] = flags; c.args[1] = (uintptr_t)sync; c.args[2] = (uintptr_t)notification;
    int rc = __real_sceGxmMidSceneFlush(ctx, flags, sync, notification);
    return end(&c, rc);
}

int __real_sceGxmCreateContext(const SceGxmContextParams *, SceGxmContext **);
int __wrap_sceGxmCreateContext(const SceGxmContextParams *params, SceGxmContext **out) {
    if (!observer) return __real_sceGxmCreateContext(params, out);
    Psp2GpuProfGxmCall c = begin(PSP2_GPUPROF_GXM_CONTEXT_CREATE, NULL);
    int rc = __real_sceGxmCreateContext(params, out);
    if (!rc && out) c.context = (uintptr_t)*out;
    return end(&c, rc);
}
int __real_sceGxmDestroyContext(SceGxmContext *);
int __wrap_sceGxmDestroyContext(SceGxmContext *ctx) {
    if (!observer) return __real_sceGxmDestroyContext(ctx);
    Psp2GpuProfGxmCall c = begin(PSP2_GPUPROF_GXM_CONTEXT_DESTROY, ctx);
    int rc = __real_sceGxmDestroyContext(ctx);
    return end(&c, rc);
}

int __real_sceGxmShaderPatcherCreateVertexProgram(SceGxmShaderPatcher *, SceGxmShaderPatcherId,
    const SceGxmVertexAttribute *, unsigned, const SceGxmVertexStream *, unsigned, SceGxmVertexProgram **);
int __wrap_sceGxmShaderPatcherCreateVertexProgram(SceGxmShaderPatcher *p, SceGxmShaderPatcherId id,
    const SceGxmVertexAttribute *a, unsigned na, const SceGxmVertexStream *s, unsigned ns, SceGxmVertexProgram **out) {
    if (!observer) return __real_sceGxmShaderPatcherCreateVertexProgram(p,id,a,na,s,ns,out);
    Psp2GpuProfGxmCall c = begin(PSP2_GPUPROF_GXM_VERTEX_CREATE,NULL);
    c.args[0] = (uintptr_t)p; c.args[1] = (uintptr_t)id;
    int rc = __real_sceGxmShaderPatcherCreateVertexProgram(p,id,a,na,s,ns,out);
    if (!rc && out) c.args[2] = (uintptr_t)*out;
    return end(&c,rc);
}
int __real_sceGxmShaderPatcherCreateFragmentProgram(SceGxmShaderPatcher *, SceGxmShaderPatcherId,
    SceGxmOutputRegisterFormat, SceGxmMultisampleMode, const SceGxmBlendInfo *, const SceGxmProgram *, SceGxmFragmentProgram **);
int __wrap_sceGxmShaderPatcherCreateFragmentProgram(SceGxmShaderPatcher *p, SceGxmShaderPatcherId id,
    SceGxmOutputRegisterFormat fmt, SceGxmMultisampleMode ms, const SceGxmBlendInfo *blend,
    const SceGxmProgram *vp, SceGxmFragmentProgram **out) {
    if (!observer) return __real_sceGxmShaderPatcherCreateFragmentProgram(p,id,fmt,ms,blend,vp,out);
    Psp2GpuProfGxmCall c = begin(PSP2_GPUPROF_GXM_FRAGMENT_CREATE,NULL);
    c.args[0] = (uintptr_t)p; c.args[1] = (uintptr_t)id;
    c.args[3] = fmt; c.args[4] = ms; c.args[5] = (uintptr_t)vp;
    int rc = __real_sceGxmShaderPatcherCreateFragmentProgram(p,id,fmt,ms,blend,vp,out);
    if (!rc && out) c.args[2] = (uintptr_t)*out;
    return end(&c,rc);
}

int __real_sceGxmShaderPatcherCreateMaskUpdateFragmentProgram(SceGxmShaderPatcher *, SceGxmFragmentProgram **);
int __wrap_sceGxmShaderPatcherCreateMaskUpdateFragmentProgram(SceGxmShaderPatcher *p, SceGxmFragmentProgram **out) {
    if (!observer) return __real_sceGxmShaderPatcherCreateMaskUpdateFragmentProgram(p,out);
    Psp2GpuProfGxmCall c = begin(PSP2_GPUPROF_GXM_MASK_FRAGMENT_CREATE,NULL);
    c.args[0] = (uintptr_t)p;
    int rc = __real_sceGxmShaderPatcherCreateMaskUpdateFragmentProgram(p,out);
    if (!rc && out) c.args[2] = (uintptr_t)*out;
    return end(&c,rc);
}

/* Record reference operations without querying potentially freed programs or
 * making additional GXM calls. Repeated successful Create may share a cached
 * program, so consumers must account for references, not count births. */
#define REF_WRAPPER(Name, Type, Kind) \
int __real_##Name(SceGxmShaderPatcher *, Type *); \
int __wrap_##Name(SceGxmShaderPatcher *p, Type *program) { \
    if (!observer) return __real_##Name(p,program); \
    Psp2GpuProfGxmCall c = begin(Kind,NULL); \
    c.args[0] = (uintptr_t)p; c.args[1] = (uintptr_t)program; \
    int rc = __real_##Name(p,program); \
    return end(&c,rc); \
}
REF_WRAPPER(sceGxmShaderPatcherAddRefVertexProgram, SceGxmVertexProgram, PSP2_GPUPROF_GXM_VERTEX_ADDREF)
REF_WRAPPER(sceGxmShaderPatcherAddRefFragmentProgram, SceGxmFragmentProgram, PSP2_GPUPROF_GXM_FRAGMENT_ADDREF)
REF_WRAPPER(sceGxmShaderPatcherReleaseVertexProgram, SceGxmVertexProgram, PSP2_GPUPROF_GXM_VERTEX_RELEASE)
REF_WRAPPER(sceGxmShaderPatcherReleaseFragmentProgram, SceGxmFragmentProgram, PSP2_GPUPROF_GXM_FRAGMENT_RELEASE)

int __real_sceGxmShaderPatcherDestroy(SceGxmShaderPatcher *);
int __wrap_sceGxmShaderPatcherDestroy(SceGxmShaderPatcher *p) {
    if (!observer) return __real_sceGxmShaderPatcherDestroy(p);
    Psp2GpuProfGxmCall c = begin(PSP2_GPUPROF_GXM_PATCHER_DESTROY,NULL);
    c.args[0]=(uintptr_t)p;
    int rc=__real_sceGxmShaderPatcherDestroy(p);
    return end(&c,rc);
}

int __real_sceGxmDisplayQueueAddEntry(SceGxmSyncObject *,SceGxmSyncObject *,const void *);
int __wrap_sceGxmDisplayQueueAddEntry(SceGxmSyncObject *old,SceGxmSyncObject *next,const void *data) {
    if (!observer) return __real_sceGxmDisplayQueueAddEntry(old,next,data);
    Psp2GpuProfGxmCall c=begin(PSP2_GPUPROF_GXM_PRESENT,NULL);
    c.args[0]=(uintptr_t)old; c.args[1]=(uintptr_t)next; c.args[2]=(uintptr_t)data;
    int rc=__real_sceGxmDisplayQueueAddEntry(old,next,data);
    return end(&c,rc);
}
int __real_sceGxmTransferCopy(uint32_t,uint32_t,uint32_t,uint32_t,SceGxmTransferColorKeyMode,
    SceGxmTransferFormat,SceGxmTransferType,const void *,uint32_t,uint32_t,int32_t,
    SceGxmTransferFormat,SceGxmTransferType,void *,uint32_t,uint32_t,int32_t,
    SceGxmSyncObject *,uint32_t,const SceGxmNotification *);
int __wrap_sceGxmTransferCopy(uint32_t w,uint32_t h,uint32_t key,uint32_t mask,SceGxmTransferColorKeyMode mode,
    SceGxmTransferFormat sf,SceGxmTransferType st,const void *src,uint32_t sx,uint32_t sy,int32_t ss,
    SceGxmTransferFormat df,SceGxmTransferType dt,void *dst,uint32_t dx,uint32_t dy,int32_t ds,
    SceGxmSyncObject *sync,uint32_t flags,const SceGxmNotification *notification) {
    if (!observer) return __real_sceGxmTransferCopy(w,h,key,mask,mode,sf,st,src,sx,sy,ss,df,dt,dst,dx,dy,ds,sync,flags,notification);
    Psp2GpuProfGxmCall c=begin(PSP2_GPUPROF_GXM_TRANSFER_COPY,NULL);
    c.args[0]=w; c.args[1]=h; c.args[2]=(uintptr_t)src; c.args[3]=(uintptr_t)dst;
    c.args[4]=sf; c.args[5]=df; c.args[6]=flags; c.args[7]=(uintptr_t)notification;
    int rc=__real_sceGxmTransferCopy(w,h,key,mask,mode,sf,st,src,sx,sy,ss,df,dt,dst,dx,dy,ds,sync,flags,notification);
    return end(&c,rc);
}
int __real_sceGxmTransferFill(uint32_t,SceGxmTransferFormat,void *,uint32_t,uint32_t,uint32_t,uint32_t,int32_t,SceGxmSyncObject *,uint32_t,const SceGxmNotification *);
int __wrap_sceGxmTransferFill(uint32_t color,SceGxmTransferFormat fmt,void *dst,uint32_t x,uint32_t y,uint32_t w,uint32_t h,int32_t stride,SceGxmSyncObject *sync,uint32_t flags,const SceGxmNotification *notification) {
    if (!observer) return __real_sceGxmTransferFill(color,fmt,dst,x,y,w,h,stride,sync,flags,notification);
    Psp2GpuProfGxmCall c=begin(PSP2_GPUPROF_GXM_TRANSFER_FILL,NULL);
    c.args[0]=w; c.args[1]=h; c.args[2]=color; c.args[3]=(uintptr_t)dst;
    c.args[4]=fmt; c.args[5]=(uintptr_t)sync; c.args[6]=flags; c.args[7]=(uintptr_t)notification;
    int rc=__real_sceGxmTransferFill(color,fmt,dst,x,y,w,h,stride,sync,flags,notification);
    return end(&c,rc);
}
int __real_sceGxmTransferDownscale(SceGxmTransferFormat,const void *,unsigned,unsigned,unsigned,unsigned,int,
    SceGxmTransferFormat,void *,unsigned,unsigned,int,SceGxmSyncObject *,unsigned,const SceGxmNotification *);
int __wrap_sceGxmTransferDownscale(SceGxmTransferFormat sf,const void *src,unsigned sx,unsigned sy,unsigned w,unsigned h,int ss,
    SceGxmTransferFormat df,void *dst,unsigned dx,unsigned dy,int ds,SceGxmSyncObject *sync,unsigned flags,const SceGxmNotification *notification) {
    if (!observer) return __real_sceGxmTransferDownscale(sf,src,sx,sy,w,h,ss,df,dst,dx,dy,ds,sync,flags,notification);
    Psp2GpuProfGxmCall c=begin(PSP2_GPUPROF_GXM_TRANSFER_DOWNSCALE,NULL);
    c.args[0]=w; c.args[1]=h; c.args[2]=(uintptr_t)src; c.args[3]=(uintptr_t)dst;
    c.args[4]=sf; c.args[5]=df; c.args[6]=flags; c.args[7]=(uintptr_t)notification;
    int rc=__real_sceGxmTransferDownscale(sf,src,sx,sy,w,h,ss,df,dst,dx,dy,ds,sync,flags,notification);
    return end(&c,rc);
}
int __real_sceGxmTransferFinish(void);
int __wrap_sceGxmTransferFinish(void) {
    if (!observer) return __real_sceGxmTransferFinish();
    Psp2GpuProfGxmCall c=begin(PSP2_GPUPROF_GXM_TRANSFER_FINISH,NULL);
    int rc=__real_sceGxmTransferFinish();
    return end(&c,rc);
}
