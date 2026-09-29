#include "psp2_gpuprof_labels.h"
#include <string.h>

int psp2GpuProfLabelsBegin(Psp2GpuProfLabels *l, Psp2GpuProfCapture *c, uint64_t thread) {
    if (!l || !c || !thread) return PSP2_GPUPROF_CAPTURE_INVALID;
    memset(l,0,sizeof(*l)); l->capture=c; l->thread=thread;
    return 0;
}
static Psp2GpuProfCaptureEvent event(Psp2GpuProfLabels *l, uint64_t time, unsigned kind) {
    Psp2GpuProfCaptureEvent e = {0};
    e.kind=kind; e.thread=l->thread; e.frame=l->frame;
    e.before_us=e.after_us=time;
    e.scope=l->depth ? l->scopes[l->depth-1] : 0;
    return e;
}
static int valid(Psp2GpuProfLabels *l) { return l && l->capture && l->thread; }
static int gap(Psp2GpuProfLabels *l, uint64_t time, const char *why) {
    Psp2GpuProfCaptureEvent e=event(l,time,PSP2_GPUPROF_EVENT_COVERAGE_GAP);
    psp2GpuProfCaptureRecord(l->capture,&e,why);
    return PSP2_GPUPROF_CAPTURE_STATE;
}
int psp2GpuProfFrameName(Psp2GpuProfLabels *l, uint64_t time, uint64_t frame, const char *name) {
    if (!valid(l)) return PSP2_GPUPROF_CAPTURE_INVALID;
    l->frame=frame;
    Psp2GpuProfCaptureEvent e=event(l,time,PSP2_GPUPROF_EVENT_FRAME);
    return psp2GpuProfCaptureRecord(l->capture,&e,name);
}
int psp2GpuProfScopePush(Psp2GpuProfLabels *l, uint64_t time, const char *name) {
    if (!valid(l)) return PSP2_GPUPROF_CAPTURE_INVALID;
    if (l->depth==PSP2_GPUPROF_SCOPE_DEPTH || l->next_scope==UINT64_MAX)
        return gap(l,time,"scope capacity exceeded");
    Psp2GpuProfCaptureEvent e=event(l,time,PSP2_GPUPROF_EVENT_SCOPE_PUSH);
    e.object=l->next_scope+1;
    int rc=psp2GpuProfCaptureRecord(l->capture,&e,name);
    if (!rc) {
        l->next_scope=e.object;
        l->scopes[l->depth++]=e.object;
    }
    return rc;
}
int psp2GpuProfScopePop(Psp2GpuProfLabels *l, uint64_t time) {
    if (!valid(l)) return PSP2_GPUPROF_CAPTURE_INVALID;
    if (!l->depth) return gap(l,time,"unmatched scope pop");
    Psp2GpuProfCaptureEvent e=event(l,time,PSP2_GPUPROF_EVENT_SCOPE_POP);
    --l->depth;
    return psp2GpuProfCaptureRecord(l->capture,&e,NULL);
}
int psp2GpuProfDrawName(Psp2GpuProfLabels *l, uint64_t time, const char *name) {
    if (!valid(l)) return PSP2_GPUPROF_CAPTURE_INVALID;
    Psp2GpuProfCaptureEvent e=event(l,time,PSP2_GPUPROF_EVENT_DRAW_NAME);
    return psp2GpuProfCaptureRecord(l->capture,&e,name);
}
int psp2GpuProfShaderName(Psp2GpuProfLabels *l, uint64_t time,
                         unsigned space, uintptr_t patcher, uintptr_t handle, const char *name) {
    if (!valid(l) || space<1 || space>3 || !handle ||
        (space!=3 && !patcher) || (space==3 && patcher)) return PSP2_GPUPROF_CAPTURE_INVALID;
    Psp2GpuProfCaptureEvent e=event(l,time,PSP2_GPUPROF_EVENT_SHADER_NAME);
    e.args[0]=space; e.args[1]=patcher; e.args[2]=handle;
    return psp2GpuProfCaptureRecord(l->capture,&e,name);
}
