/* Synthetic GXM observations through the real capture/labels/CSV pipeline. */
#include "psp2_gpuprof_gxm_observer.h"
#include "psp2_gpuprof_labels.h"
#include <assert.h>

static Psp2GpuProfCapture capture;
static uint64_t tick;
static void call(unsigned kind, uintptr_t context, uintptr_t a, uintptr_t b, uintptr_t d) {
    Psp2GpuProfGxmCall c={.kind=kind,.context=context,.thread=7,.args={a,b,d}};
    c.before_us=++tick; c.after_us=++tick;
    psp2GpuProfCaptureGxmCall(&capture,&c);
}
int main(int argc, char **argv) {
    (void)argv;
    Psp2GpuProfCaptureEvent rows[64];
    Psp2GpuProfCaptureConfig config={rows,64,NULL,NULL,NULL};
    Psp2GpuProfLabels labels;
    assert(!psp2GpuProfCaptureInit(&capture,&config));
    assert(!psp2GpuProfCaptureStart(&capture));
    assert(psp2GpuProfCaptureGxmIdentitySeed365(&capture,0,0,0)==PSP2_GPUPROF_CAPTURE_INVALID);
    assert(!psp2GpuProfCaptureGxmIdentitySeed365(&capture,7,0,0));
    assert(!psp2GpuProfLabelsBegin(&labels,&capture,7));
    call(9,10,0,0,0);
    call(11,0,20,30,40); call(12,0,20,31,41);
    call(7,10,40,0,0); call(8,10,41,0,0);
    assert(!psp2GpuProfShaderName(&labels,++tick,2,20,41,"Surface shader"));
    assert(!psp2GpuProfFrameName(&labels,++tick,55,"Frame 55"));
    assert(!psp2GpuProfScopePush(&labels,++tick,"Water, \"reflection\"\npass"));
    assert(!psp2GpuProfDrawName(&labels,++tick,"Mesh"));
    call(1,10,0,50,0);
    call(4,10,0,0,60); call(3,10,0,0,70);
    call(5,10,0,0,60); call(2,10,80,81,0);
    assert(!psp2GpuProfScopePop(&labels,++tick));
    call(16,0,20,41,0); call(12,0,20,31,41);
    call(8,10,41,0,0);
    call(1,10,0,50,0); call(4,10,0,0,60); call(2,10,80,81,0);
    if (argc>1) {
        while (capture.count<64) assert(!psp2GpuProfDrawName(&labels,++tick,"padding"));
        assert(psp2GpuProfDrawName(&labels,++tick,"lost")==PSP2_GPUPROF_CAPTURE_FULL);
    }
    assert(!psp2GpuProfCaptureStop(&capture));
    assert(!psp2GpuProfCaptureWriteCsv(&capture,stdout));
    return 0;
}
