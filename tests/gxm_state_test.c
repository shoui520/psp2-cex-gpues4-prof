#include "psp2_gpuprof_gxm_state.h"
#include <assert.h>

static uint64_t now;
static void call(Psp2GpuProfGxmState *s,unsigned kind,uintptr_t ctx,uintptr_t a,uintptr_t b,uintptr_t d) {
    Psp2GpuProfGxmCall c={.kind=kind,.context=ctx,.args={a,b,d},.thread=1};
    c.before_us=++now; c.after_us=++now;
    psp2GpuProfGxmStateObserve(s,&c);
}
int main(void) {
    Psp2GpuProfGxmState s;
    Psp2GpuProfGxmObject objects[4];
    Psp2GpuProfCapture c;
    Psp2GpuProfCaptureEvent rows[32],scratch[5];
    Psp2GpuProfCaptureConfig cfg={rows,32,NULL,NULL,NULL};
    assert(!psp2GpuProfGxmStateInit(&s,objects,4,NULL,NULL,NULL));
    call(&s,9,10,0,0,0);
    call(&s,11,0,20,30,40);
    call(&s,25,0,20,0,41); /* Mask-update programs use fragment lifetimes. */
    call(&s,12,0,20,31,41); /* Cached create acquires another reference. */
    call(&s,7,10,40,0,0);
    call(&s,8,10,41,0,0);
    call(&s,23,10,0x200000,0,0);
    call(&s,24,10,0,0,0);
    assert(!s.incomplete);
    assert(!psp2GpuProfCaptureInit(&c,&cfg));
    assert(!psp2GpuProfCaptureStart(&c));
    assert(!psp2GpuProfGxmStateAttach(&s,&c,scratch,5,++now));
    assert(c.count==4 && rows[3].args[2]==2);
    assert(rows[1].args[4]==rows[3].args[1]);
    assert(rows[1].args[5]==2 && rows[1].args[6]==1);
    /* Contextless transfer/presentation calls must be forwarded, not treated
       as missing context history or allowed to consume object-cache slots. */
    for (unsigned kind=18;kind<=22;++kind) {
        call(&s,kind,0,100,200,300);
        assert(!s.incomplete);
        assert(rows[c.count-1].object==kind);
        assert(rows[c.count-1].context==0);
    }
    call(&s,1,10,0,0,0); call(&s,4,10,0,0,0); call(&s,2,10,0,0,0);
    psp2GpuProfGxmStateDetach(&s);
    assert(!psp2GpuProfCaptureStop(&c));
    call(&s,16,0,20,41,0); call(&s,16,0,20,41,0);
    uint64_t old=objects[2].generation;
    call(&s,12,0,20,31,41);
    assert(objects[2].generation>old);
    assert(!psp2GpuProfCaptureStart(&c));
    call(&s,1,10,0,0,0);
    assert(psp2GpuProfGxmStateAttach(&s,&c,scratch,5,++now)==PSP2_GPUPROF_CAPTURE_STATE);
    assert(!c.count);
    call(&s,2,10,0,0,0);
    assert(!psp2GpuProfGxmStateAttach(&s,&c,scratch,5,++now));
    psp2GpuProfGxmStateDetach(&s);
    call(&s,9,11,0,0,0);
    call(&s,9,12,0,0,0); /* Fifth live object: bounded cache overflow. */
    assert(s.incomplete);
    assert(psp2GpuProfGxmStateAttach(&s,&c,scratch,5,++now)==PSP2_GPUPROF_CAPTURE_STATE);
    return 0;
}
