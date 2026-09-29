#include "psp2_gpuprof_gxm_state.h"
#include <string.h>

int psp2GpuProfGxmStateInit(Psp2GpuProfGxmState *s,Psp2GpuProfGxmObject *objects,size_t n,
    void (*lock)(void *),void (*unlock)(void *),void *userdata) {
    if (!s || !objects || !n || n>SIZE_MAX/sizeof(*objects) || (!!lock!=!!unlock))
        return PSP2_GPUPROF_CAPTURE_INVALID;
    memset(s,0,sizeof(*s)); memset(objects,0,n*sizeof(*objects));
    s->objects=objects; s->capacity=n; s->lock=lock; s->unlock=unlock; s->userdata=userdata;
    return 0;
}
static Psp2GpuProfGxmObject *lookup(Psp2GpuProfGxmState *s,unsigned kind,uintptr_t owner,uintptr_t handle) {
    for (size_t i=0;i<s->capacity;++i) {
        Psp2GpuProfGxmObject *o=&s->objects[i];
        if (o->kind==kind && o->owner==owner && o->handle==handle) return o;
    }
    return NULL;
}
static Psp2GpuProfGxmObject *create(Psp2GpuProfGxmState *s,unsigned kind,uintptr_t owner,uintptr_t handle) {
    if (!handle || s->next_generation==UINT64_MAX) { s->incomplete=1; return NULL; }
    for (size_t i=0;i<s->capacity;++i) if (!s->objects[i].kind) {
        Psp2GpuProfGxmObject *o=&s->objects[i];
        memset(o,0,sizeof(*o)); o->kind=kind; o->owner=owner; o->handle=handle;
        o->generation=++s->next_generation;
        return o;
    }
    s->incomplete=1; return NULL;
}
static uint64_t binding(Psp2GpuProfGxmState *s,unsigned kind,uintptr_t handle) {
    uint64_t id=0;
    for (size_t i=0;i<s->capacity;++i) if (s->objects[i].kind==kind && s->objects[i].handle==handle) {
        if (id) { s->incomplete=1; return 0; }
        id=s->objects[i].generation;
    }
    if (!id) s->incomplete=1;
    return id;
}
static void update(Psp2GpuProfGxmState *s,const Psp2GpuProfGxmCall *c) {
    if (c->result) return;
    unsigned k=c->kind;
    if (k==PSP2_GPUPROF_GXM_MASK_FRAGMENT_CREATE) k=PSP2_GPUPROF_GXM_FRAGMENT_CREATE;
    if (k>=PSP2_GPUPROF_GXM_PRESENT && k<=PSP2_GPUPROF_GXM_TRANSFER_FINISH) return;
    if (k==PSP2_GPUPROF_GXM_PATCHER_DESTROY) {
        for (size_t i=0;i<s->capacity;++i)
            if (s->objects[i].kind>=2 && s->objects[i].owner==c->args[0]) s->objects[i].kind=0;
        return;
    }
    if (k>=PSP2_GPUPROF_GXM_VERTEX_CREATE && k<=PSP2_GPUPROF_GXM_FRAGMENT_RELEASE) {
        unsigned kind=(k%2) ? 2 : 3;
        int is_create=k==PSP2_GPUPROF_GXM_VERTEX_CREATE || k==PSP2_GPUPROF_GXM_FRAGMENT_CREATE;
        Psp2GpuProfGxmObject *o=lookup(s,kind,c->args[0],c->args[is_create ? 2 : 1]);
        if (!o && is_create) o=create(s,kind,c->args[0],c->args[2]);
        if (!o) { s->incomplete=1; return; }
        if (c->before_us<o->after_us) s->incomplete=1;
        o->after_us=c->after_us;
        if (k>=PSP2_GPUPROF_GXM_VERTEX_RELEASE) {
            if (!o->refs) s->incomplete=1;
            else if (!--o->refs) o->kind=0;
        } else {
            if (o->refs==UINT64_MAX) s->incomplete=1;
            else ++o->refs;
        }
        return;
    }
    Psp2GpuProfGxmObject *o=lookup(s,1,0,c->context);
    if (k==PSP2_GPUPROF_GXM_CONTEXT_CREATE) {
        if (o) s->incomplete=1;
        else o=create(s,1,0,c->context);
    }
    if (!o) { s->incomplete=1; return; }
    if (c->before_us<o->after_us) s->incomplete=1;
    o->after_us=c->after_us;
    switch (k) {
    case PSP2_GPUPROF_GXM_CONTEXT_DESTROY: o->kind=0; break;
    case PSP2_GPUPROF_GXM_BEGIN:
        if (o->open) s->incomplete=1;
        o->open=1; break;
    case PSP2_GPUPROF_GXM_END:
        if (!o->open) s->incomplete=1;
        o->open=0; break;
    case PSP2_GPUPROF_GXM_VERTEX_BIND: o->vertex=binding(s,2,c->args[0]); break;
    case PSP2_GPUPROF_GXM_FRAGMENT_BIND: o->fragment=binding(s,3,c->args[0]); break;
    case PSP2_GPUPROF_GXM_FRONT_FRAGMENT_ENABLE:
        o->front_fragment=c->args[0]==0 ? 1 : c->args[0]==0x200000 ? 2 : 0; break;
    case PSP2_GPUPROF_GXM_BACK_FRAGMENT_ENABLE:
        o->back_fragment=c->args[0]==0 ? 1 : c->args[0]==0x200000 ? 2 : 0; break;
    default: break;
    }
}
void psp2GpuProfGxmStateObserve(void *userdata,const Psp2GpuProfGxmCall *c) {
    Psp2GpuProfGxmState *s=userdata;
    if (!s || !c) return;
    if (s->lock) s->lock(s->userdata);
    unsigned was_incomplete=s->incomplete;
    update(s,c);
    if (s->capture) {
        if (!was_incomplete && s->incomplete) {
            Psp2GpuProfCaptureEvent e={.kind=PSP2_GPUPROF_EVENT_COVERAGE_GAP,
                .before_us=c->before_us,.after_us=c->after_us,.thread=c->thread};
            psp2GpuProfCaptureRecord(s->capture,&e,"GXM lifetime cache incomplete");
        }
        psp2GpuProfCaptureGxmCall(s->capture,c);
    }
    if (s->unlock) s->unlock(s->userdata);
}
int psp2GpuProfGxmStateAttach(Psp2GpuProfGxmState *s,Psp2GpuProfCapture *c,
    Psp2GpuProfCaptureEvent *scratch,size_t count,uint64_t time) {
    if (!s || !c || !scratch || count<=s->capacity) return PSP2_GPUPROF_CAPTURE_INVALID;
    if (s->capture || s->incomplete) return PSP2_GPUPROF_CAPTURE_STATE;
    size_t used=1;
    memset(&scratch[0],0,sizeof(scratch[0]));
    scratch[0].kind=PSP2_GPUPROF_EVENT_GXM_SNAPSHOT;
    scratch[0].before_us=scratch[0].after_us=time;
    for (size_t i=0;i<s->capacity;++i) {
        const Psp2GpuProfGxmObject *o=&s->objects[i];
        if (!o->kind) continue;
        if (o->open) return PSP2_GPUPROF_CAPTURE_STATE;
        Psp2GpuProfCaptureEvent *e=&scratch[used++]; memset(e,0,sizeof(*e));
        e->kind=PSP2_GPUPROF_EVENT_GXM_SNAPSHOT;
        e->before_us=e->after_us=time;
        e->object=o->kind; e->context=o->handle;
        e->args[0]=o->owner; e->args[1]=o->generation; e->args[2]=o->refs;
        e->args[3]=o->vertex; e->args[4]=o->fragment;
        e->args[5]=o->front_fragment; e->args[6]=o->back_fragment;
    }
    int rc=psp2GpuProfCaptureRecordBatch(c,scratch,used);
    if (!rc) s->capture=c;
    return rc;
}
void psp2GpuProfGxmStateDetach(Psp2GpuProfGxmState *s) { if (s) s->capture=NULL; }
