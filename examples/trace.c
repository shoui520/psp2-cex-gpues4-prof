#include <psp2_gpuprof_trace.h>
#include <psp2_gpuprof_presets.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

/* Call only while holding the application's trace lock, or on its sole trace
 * thread. Drain with TraceRead outside the measured region. Tags/IDs can be
 * resolved to application names by your report writer.
 */
int record_frame_begin(Psp2GpuProfTrace *trace, uint32_t frame)
{
    Psp2GpuProfRecord record = {0};
    record.kind = PSP2_GPUPROF_FRAME_BEGIN;
    record.id = frame;
    record.thread_id = (uint32_t)sceKernelGetThreadId();
    record.cpu_before_us = record.cpu_after_us = sceKernelGetProcessTimeWide();
    return psp2GpuProfTraceAppend(trace, &record);
}

/* Counter capture must not be active. Return the GPU API result independently
 * of the buffer append result: an API failure is a useful trace record too.
 */
int record_pipeline_signals(Psp2GpuProfTrace *trace, uint32_t frame, int *api_result)
{
    Psp2GpuProfRecord record = {0};
    Psp2GpuProfSignalConfig cfg = psp2GpuProfPipelineSignalConfig();
    int rc;
    if (!api_result) return PSP2_GPUPROF_INVALID;
    record.id = frame;
    record.thread_id = (uint32_t)sceKernelGetThreadId();
    record.cpu_before_us = sceKernelGetProcessTimeWide();
    rc = psp2GpuProfReadSignals(&cfg, &record.data.signals);
    record.cpu_after_us = sceKernelGetProcessTimeWide();
    record.kind = rc ? PSP2_GPUPROF_API_ERROR : PSP2_GPUPROF_SIGNAL_SAMPLE;
    if (rc) record.data.error = rc;
    *api_result = rc;
    return psp2GpuProfTraceAppend(trace, &record);
}
