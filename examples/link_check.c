#include <psp2_gpuprof.h>
#include <psp2_gpuprof_presets.h>
#include <psp2_gpuprof_trace.h>

/* Link-only smoke check: does not configure counters. */
int main(void)
{
    Psp2GpuProfInfo info;
    Psp2GpuProfTrace trace;
    Psp2GpuProfRecord records[2];
    if (psp2GpuProfTraceInit(&trace, records, 2)) return 1;
    int rc = psp2GpuProfGetInfo(&info);
    /* Link the optional API without accessing the GPU in this smoke check. */
    int (*volatile signals_api)(const Psp2GpuProfSignalConfig *, Psp2GpuProfSignals *) = psp2GpuProfReadSignals;
    (void)signals_api;
    return rc ? rc : info.status;
}
