#include "psp2_gpuprof_identity.h"
#include <assert.h>
#include <stddef.h>

int main(void)
{
    Psp2GpuProfIdentityTracker t;
    Psp2GpuProfIdentity id;
    assert(!psp2GpuProfIdentitySeed(&t,1,42,0,0));
    for (unsigned frame=0; frame<100; ++frame) {
        for (unsigned scene=0; scene<3; ++scene) {
            /* Different sequential contexts share process-global scene IDs. */
            assert(!psp2GpuProfIdentityBegin(&t,scene+1,0));
            assert(!psp2GpuProfIdentityEnd(&t,scene+1,0,&id));
            assert(id.pid==42 && id.frame==frame && id.scene==scene);
            assert(id.context==scene+1 && id.epoch==1);
        }
        assert(!psp2GpuProfIdentityDisplay(&t,0));
    }
    assert(psp2GpuProfIdentityDisplay(&t,-1)==PSP2_GPUPROF_STALE);
    assert(psp2GpuProfIdentityBegin(&t,1,0)==PSP2_GPUPROF_STALE);
    for (int failure=0; failure<5; ++failure) {
        assert(!psp2GpuProfIdentitySeed(&t,2,42,0,0));
        assert(!psp2GpuProfIdentityBegin(&t,1,0));
        if (failure==0) assert(psp2GpuProfIdentityBegin(&t,2,0)<0);
        if (failure==1) assert(psp2GpuProfIdentityDisplay(&t,0)<0);
        if (failure==2) assert(psp2GpuProfIdentityEnd(&t,2,0,&id)<0);
        if (failure==3) assert(psp2GpuProfIdentityEnd(&t,1,-1,&id)<0);
        if (failure==4) psp2GpuProfIdentityInvalidate(&t);
        assert(!t.valid);
        id.pid=99;
        assert(psp2GpuProfIdentityEnd(&t,1,0,&id)<0 && id.pid==0);
    }
    assert(!psp2GpuProfIdentitySeed(&t,3,42,0xffffff,0));
    assert(psp2GpuProfIdentityDisplay(&t,0)<0 && !t.valid);
    assert(!psp2GpuProfIdentitySeed(&t,4,42,0,UINT32_MAX));
    assert(!psp2GpuProfIdentityBegin(&t,1,0));
    assert(psp2GpuProfIdentityEnd(&t,1,0,&id)<0 && !id.pid);
    assert(psp2GpuProfIdentitySeed(&t,0,42,0,0)<0 && !t.valid);
    assert(psp2GpuProfIdentitySeed(&t,1,42,0x1000000,0)<0 && !t.valid);
    assert(psp2GpuProfIdentitySeed(NULL,1,42,0,0)<0);
    Psp2GpuProfDrawLabel draws[2] = {{.draw_id=1}, {.draw_id=2}};
    Psp2GpuProfIdentifiedScene scenes[2] = {
        {{1,1,42,7,0},10,30,draws,2,1},
        {{1,2,42,7,1},10,30,draws,2,1}
    };
    Psp2GpuProfWorkCore work = {0};
    work.before.render_pid=42; work.before.render_frame=7;
    work.after=work.before; work.fragment_signal=0x20020000;
    work.flags=PSP2_GPUPROF_WORK_UNVERIFIED;
    Psp2GpuProfIdentityMatch match;
    /* Overlapping CPU lifetimes are disambiguated by different firmware IDs. */
    assert(!psp2GpuProfMatchWork(scenes,2,42,11,12,&work,&match));
    assert(match.status==PSP2_GPUPROF_MATCH_CANDIDATE && match.draw==&draws[1]);
    assert(match.source_flags==PSP2_GPUPROF_WORK_UNVERIFIED);
    scenes[1].identity.scene=0; scenes[1].identity.epoch=2;
    assert(!psp2GpuProfMatchWork(scenes,2,42,11,12,&work,&match));
    assert(match.status==PSP2_GPUPROF_MATCH_COLLISION && !match.draw);
    scenes[1].identity.scene=1;
    work.after.render_scene=1;
    assert(!psp2GpuProfMatchWork(scenes,2,42,11,12,&work,&match));
    assert(match.status==PSP2_GPUPROF_MATCH_CHANGED);
    work.after=work.before;
    assert(!psp2GpuProfMatchWork(scenes,2,99,11,12,&work,&match));
    assert(match.status==PSP2_GPUPROF_MATCH_FOREIGN);
    assert(!psp2GpuProfMatchWork(scenes,2,42,11,30,&work,&match));
    assert(match.status==PSP2_GPUPROF_MATCH_BOUNDARY);
    scenes[0].complete_order=0;
    assert(!psp2GpuProfMatchWork(scenes,2,42,11,12,&work,&match));
    assert(match.status==PSP2_GPUPROF_MATCH_ORDER);
    scenes[0].complete_order=1; work.fragment_signal=0x20030000;
    assert(!psp2GpuProfMatchWork(scenes,2,42,11,12,&work,&match));
    assert(match.status==PSP2_GPUPROF_MATCH_BATCH && !match.draw);
    return 0;
}
