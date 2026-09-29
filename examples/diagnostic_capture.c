/* Call from a dedicated application-owned sampler thread, never a GXM hook.
 * Supply GetInfo's successful result once at startup; choose your own cadence.
 * Stop/join that thread before stopping/exporting the capture. */
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <psp2_gpuprof_capture_diagnostic.h>

int example_capture_diagnostic_tick(Psp2GpuProfCapture *capture,
    const Psp2GpuProfInfo *info, uint64_t sample_id, uint32_t group) {
    if (!info || info->status || !(info->capabilities & PSP2_GPUPROF_CAP_DIAGNOSTIC))
        return PSP2_GPUPROF_CAPTURE_INVALID;
    Psp2GpuProfDiagnosticConfig config={.size=sizeof(config),.abi=PSP2_GPUPROF_ABI,
        .group=group,.tag_group=70};
    Psp2GpuProfDiagnostic data={0};
    Psp2GpuProfDiagnosticObservation o={.sampler_thread=(uint32_t)sceKernelGetThreadId(),
        .sample_id=sample_id,.group=group,.tag_group=70,
        .process_id=(uint32_t)sceKernelGetProcessId(),.driver_fingerprint=info->driver_fingerprint};
    int mhz=scePowerGetGpuClockFrequency();
    o.clock_before_mhz=mhz>0 ? (uint32_t)mhz : 0;
    o.before_us=sceKernelGetSystemTimeWide();
    o.result=psp2GpuProfReadDiagnostic(&config,&data);
    o.after_us=sceKernelGetSystemTimeWide();
    mhz=scePowerGetGpuClockFrequency();
    o.clock_after_mhz=mhz>0 ? (uint32_t)mhz : 0;
    /* Recording success is separate from GPU-read success (stored in CSV). */
    return psp2GpuProfCaptureDiagnostic(capture,&o,o.result ? NULL : &data);
}
