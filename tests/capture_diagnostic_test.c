#include "psp2_gpuprof_capture_diagnostic.h"
#include <assert.h>

int main(void) {
    Psp2GpuProfCaptureEvent storage[14];
    Psp2GpuProfCapture c;
    Psp2GpuProfCaptureConfig cfg={storage,14,NULL,NULL,NULL};
    Psp2GpuProfDiagnosticObservation o={.sampler_thread=4,.sample_id=7,
        .before_us=100,.after_us=200,.group=43,.tag_group=70,
        .clock_before_mhz=111,.clock_after_mhz=111,.process_id=12};
    Psp2GpuProfDiagnostic s={.size=sizeof(s),.abi=PSP2_GPUPROF_ABI,.group=43,.tag_group=70};
    for (unsigned i=0;i<4;++i) {
        s.cores[i].before.ta_pid=100+i;
        s.cores[i].after.render_scene=200+i;
        s.cores[i].timer_before=300+i;
        s.cores[i].timer_after=400+i;
        s.cores[i].pds_before=500+i;
        s.cores[i].tag_before=600+i;
        s.cores[i].value=700+i;
        s.cores[i].tag_after=800+i;
        s.cores[i].pds_after=900+i;
        s.cores[i].flags=PSP2_GPUPROF_DIAG_UNVERIFIED;
    }
    assert(!psp2GpuProfCaptureInit(&c,&cfg));
    assert(!psp2GpuProfCaptureStart(&c));
    assert(!psp2GpuProfCaptureDiagnostic(&c,&o,&s));
    assert(c.count==13);
    for (unsigned i=0;i<4;++i) {
        unsigned n=1+i*3;
        assert(storage[n].context==i && storage[n+2].scope==2);
        assert(storage[n].args[0]==100+i && storage[n].args[6]==300+i);
        assert(storage[n+1].args[5]==200+i && storage[n+1].args[6]==500+i);
        assert(storage[n+2].args[0]==700+i && storage[n+2].args[2]==900+i);
    }
    assert(psp2GpuProfCaptureDiagnostic(&c,&o,&s)==PSP2_GPUPROF_CAPTURE_FULL);
    assert(c.count==13 && c.dropped==13); /* No partial observation. */
    o.result=-5;
    assert(!psp2GpuProfCaptureDiagnostic(&c,&o,NULL));
    assert(c.count==14 && storage[13].result==-5);
    assert(!psp2GpuProfCaptureStop(&c));
    return 0;
}
