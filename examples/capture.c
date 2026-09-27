#include <psp2_gpuprof.h>

/* Call from a VitaSDK application's ordinary thread, with GXM initialized.
 * workload must submit and finish the GPU work you want to measure, e.g.
 * with sceGxmFinish(context). Other processes' GPU work is counted too.
 * This example returns raw deltas, not a percentage or elapsed nanoseconds.
 */
/* pending receives a handle that still needs End if End returns BUSY. The
 * caller must retry End later in that case; do not discard this handle.
 */
int capture_gpu_delta(void (*workload)(void), uint32_t delta[4][8],
                      Psp2GpuProfSession *pending)
{
    Psp2GpuProfConfig config = {0};
    Psp2GpuProfSample before, after;
    int rc, end_rc;
    unsigned core, counter;
    if (!workload || !delta || !pending) return PSP2_GPUPROF_INVALID;
    pending->lo = pending->hi = 0;
    config.size = sizeof(config);
    config.abi = PSP2_GPUPROF_ABI;
    /* SGX543 event-map group 0, bit 0: TA processing; bit 1: 3D processing.
     * Remaining slots duplicate those two. Physical calibration is pending.
     */
    for (counter = 0; counter < 8; ++counter)
        config.events[counter].bit = counter & 1;
    rc = psp2GpuProfBegin(&config, &before);
    if (rc) return rc; /* BUSY/OFFLINE: retry later, never spin here. */
    *pending = before.session;
    workload();
    rc = psp2GpuProfRead(&before.session, &after);
    end_rc = psp2GpuProfEnd(&before.session);
    if (!end_rc || end_rc == PSP2_GPUPROF_STALE)
        pending->lo = pending->hi = 0;
    if (rc) return rc; /* STALE: power/reset interrupted the measurement. */
    if (end_rc) return end_rc;
    for (core = 0; core < 4; ++core)
        for (counter = 0; counter < 8; ++counter)
            delta[core][counter] = after.counters[core][counter] - before.counters[core][counter];
    return 0;
}
