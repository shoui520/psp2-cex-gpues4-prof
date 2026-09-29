#include "psp2_gpuprof_labels.h"
#include <assert.h>
#include <string.h>
int main(void) {
    Psp2GpuProfCapture c;
    Psp2GpuProfCaptureEvent records[128];
    Psp2GpuProfCaptureConfig cfg={records,128,NULL,NULL,NULL};
    Psp2GpuProfLabels a,b;
    assert(!psp2GpuProfCaptureInit(&c,&cfg));
    assert(!psp2GpuProfCaptureStart(&c));
    assert(!psp2GpuProfLabelsBegin(&a,&c,1));
    assert(!psp2GpuProfLabelsBegin(&b,&c,2));
    assert(!psp2GpuProfFrameName(&a,1,100,"frame"));
    assert(!psp2GpuProfScopePush(&a,2,"world"));
    assert(!psp2GpuProfScopePush(&b,3,"other thread"));
    assert(!psp2GpuProfScopePush(&a,4,"water"));
    assert(records[3].scope==1 && records[3].object==2 && records[3].thread==1);
    assert(records[2].scope==0 && records[2].object==1 && records[2].thread==2);
    assert(!psp2GpuProfDrawName(&a,5,"surface"));
    assert(records[4].scope==2 && records[4].frame==100);
    assert(!psp2GpuProfShaderName(&a,6,2,0x100,0x200,"water shader"));
    assert(!strcmp(records[5].label,"water shader"));
    assert(!psp2GpuProfScopePop(&a,7));
    assert(!psp2GpuProfScopePop(&a,8));
    assert(psp2GpuProfScopePop(&a,9)==PSP2_GPUPROF_CAPTURE_STATE);
    assert(records[8].kind==PSP2_GPUPROF_EVENT_COVERAGE_GAP);
    for (unsigned i=0;i<PSP2_GPUPROF_SCOPE_DEPTH;++i)
        assert(!psp2GpuProfScopePush(&a,10+i,"nested"));
    assert(psp2GpuProfScopePush(&a,50,"overflow")==PSP2_GPUPROF_CAPTURE_STATE);
    assert(!psp2GpuProfCaptureStop(&c));
    assert(!psp2GpuProfLabelsBegin(&a,&c,1));
    assert(psp2GpuProfScopePush(&a,51,"stopped")==PSP2_GPUPROF_CAPTURE_STATE);
    assert(!a.depth && !a.next_scope);
    assert(!psp2GpuProfCaptureStart(&c));
    for (unsigned i=0;i<128;++i) assert(!psp2GpuProfDrawName(&a,52+i,"fill"));
    assert(psp2GpuProfScopePush(&a,200,"full")==PSP2_GPUPROF_CAPTURE_FULL);
    assert(!a.depth && !a.next_scope && c.dropped==1);
    return 0;
}
