#include "psp2_gpuprof_capture_diagnostic.h"

static void state(uint64_t *a, const Psp2GpuProfWorkState *s) {
    a[0]=s->ta_pid; a[1]=s->ta_frame; a[2]=s->ta_scene;
    a[3]=s->render_pid; a[4]=s->render_frame; a[5]=s->render_scene;
}
int psp2GpuProfCaptureDiagnostic(Psp2GpuProfCapture *capture,
    const Psp2GpuProfDiagnosticObservation *o, const Psp2GpuProfDiagnostic *sample) {
    if (!capture || !o || !o->sampler_thread || o->before_us>o->after_us ||
        (!o->result && (!sample || sample->size!=sizeof(*sample) ||
         sample->abi!=PSP2_GPUPROF_ABI || sample->group!=o->group ||
         sample->tag_group!=o->tag_group))) return PSP2_GPUPROF_CAPTURE_INVALID;
    Psp2GpuProfCaptureEvent rows[13]={{0}};
    for (unsigned i=0;i<13;++i) {
        rows[i].kind=i ? PSP2_GPUPROF_EVENT_DIAGNOSTIC_CORE : PSP2_GPUPROF_EVENT_DIAGNOSTIC;
        rows[i].thread=o->sampler_thread; rows[i].object=o->sample_id;
        rows[i].before_us=o->before_us; rows[i].after_us=o->after_us;
    }
    rows[0].result=o->result;
    rows[0].args[0]=o->group; rows[0].args[1]=o->tag_group;
    rows[0].args[2]=o->clock_before_mhz; rows[0].args[3]=o->clock_after_mhz;
    rows[0].args[4]=o->process_id; rows[0].args[5]=o->driver_fingerprint;
    rows[0].args[6]=PSP2_GPUPROF_ABI;
    if (o->result) return psp2GpuProfCaptureRecordBatch(capture,rows,1);
    for (unsigned i=0;i<4;++i) {
        const Psp2GpuProfDiagnosticCore *c=&sample->cores[i];
        Psp2GpuProfCaptureEvent *r=&rows[1+i*3];
        for (unsigned j=0;j<3;++j) { r[j].context=i; r[j].scope=j; }
        state(r[0].args,&c->before);
        r[0].args[6]=c->timer_before; r[0].args[7]=c->timer_after;
        state(r[1].args,&c->after);
        r[1].args[6]=c->pds_before; r[1].args[7]=c->tag_before;
        r[2].args[0]=c->value; r[2].args[1]=c->tag_after;
        r[2].args[2]=c->pds_after; r[2].args[3]=c->flags;
    }
    return psp2GpuProfCaptureRecordBatch(capture,rows,13);
}
