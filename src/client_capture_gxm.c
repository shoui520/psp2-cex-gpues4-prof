#include "psp2_gpuprof_capture.h"
#include "psp2_gpuprof_gxm_observer.h"

int psp2GpuProfCaptureGxmIdentitySeed365(Psp2GpuProfCapture *capture, uint32_t pid,
                                     uint32_t frame, uint32_t scene) {
    if (!pid || frame > 0xffffffu) return PSP2_GPUPROF_CAPTURE_INVALID;
    Psp2GpuProfCaptureEvent e = {0};
    e.kind = PSP2_GPUPROF_EVENT_GXM_IDENTITY_SEED;
    e.args[0] = 365; e.args[1] = pid; e.args[2] = frame; e.args[3] = scene;
    return psp2GpuProfCaptureRecord(capture, &e, NULL);
}

void psp2GpuProfCaptureGxmCall(void *userdata, const Psp2GpuProfGxmCall *call) {
    if (!userdata || !call) return;
    Psp2GpuProfCaptureEvent e = {0};
    e.kind = PSP2_GPUPROF_EVENT_GXM_CALL;
    e.before_us = call->before_us;
    e.after_us = call->after_us;
    e.thread = call->thread;
    /* Raw call context is an address, unlike the logical context field of
     * other event kinds. This distinction is part of the format contract. */
    e.context = call->context;
    e.object = call->kind;
    e.result = call->result;
    for (unsigned i = 0; i < 8; ++i) e.args[i] = call->args[i];
    psp2GpuProfCaptureRecord(userdata, &e, NULL);
}
