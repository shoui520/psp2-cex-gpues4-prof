#include <psp2_gpuprof.h>
#include <psp2_gpuprof_presets.h>
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <stdio.h>
#include <string.h>

/* Offscreen GPU transfer probe: no shaders, display hooks, or libperf.
 * This exercises transfers, not a representative TA/3D rendering workload.
 * No counter activity threshold is assumed before hardware calibration.
 */
#define WIDTH 1024u
#define HEIGHT 512u
#define BYTES (WIDTH * HEIGHT * 4u)

static int fill(void *memory, uint32_t color)
{
    int rc = sceGxmTransferFill(color, SCE_GXM_TRANSFER_FORMAT_U8U8U8U8_ABGR,
                               memory, 0, 0, WIDTH, HEIGHT, WIDTH * 4,
                               NULL, 0, NULL);
    return rc ? rc : sceGxmTransferFinish();
}

static void log_sample(FILE *log, unsigned run, const char *phase,
                       const Psp2GpuProfSample *sample)
{
    unsigned c, n;
    for (c = 0; c < PSP2_GPUPROF_CORES; ++c) {
        fprintf(log, "sample,%u,%s,%08x,%08x,%u,%u,%u,%u", run, phase,
                sample->session.hi, sample->session.lo, sample->sequence,
                sample->gpu_timer_before, sample->gpu_timer_after, c);
        for (n = 0; n < PSP2_GPUPROF_COUNTERS; ++n)
            fprintf(log, ",%u", sample->counters[c][n]);
        fputc('\n', log);
    }
}

int main(void)
{
    FILE *log = fopen("ux0:data/gpuprof-transfer.csv", "a");
    SceGxmInitializeParams params = {0};
    Psp2GpuProfInfo info;
    Psp2GpuProfConfig config = {0};
    SceUID block = -1;
    void *memory = NULL;
    int initialized = 0, mapped = 0, rc, cleanup_rc;
    unsigned run, n;
    if (!log) return 1;
    /* Keep the last completed checkpoint if a syscall or GPU operation fails
     * to return. File I/O is outside the measured transfer/read interval.
     */
    setvbuf(log, NULL, _IONBF, 0);
    fprintf(log, "probe,transfer-api-v2\n");
    fprintf(log, "call,GetInfo\n");
    rc = psp2GpuProfGetInfo(&info);
    fprintf(log, "info,%d\n", rc);
    if (rc) goto done;
    fprintf(log, "backend,%d,%08x,%u,%u,%u\n", info.status,
            info.driver_fingerprint, info.abi, info.cores, info.counters);
    if ((rc = info.status)) goto done;
    fprintf(log, "capabilities,%08x,%u\n", info.capabilities, info.max_groups);
    if (info.abi != PSP2_GPUPROF_ABI || info.cores != PSP2_GPUPROF_CORES ||
        info.counters != PSP2_GPUPROF_COUNTERS) {
        rc = PSP2_GPUPROF_UNSUPPORTED;
        goto done;
    }
    fprintf(log, "call,GxmInitialize\n");
    params.parameterBufferSize = SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE;
    rc = sceGxmInitialize(&params);
    fprintf(log, "initialize,%08x\n", (unsigned)rc);
    if (rc) goto done;
    initialized = 1;
    block = sceKernelAllocMemBlock("gpuprof-transfer", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
                                  BYTES, NULL);
    fprintf(log, "allocate,%08x\n", (unsigned)block);
    if (block < 0) { rc = block; goto done; }
    rc = sceKernelGetMemBlockBase(block, &memory);
    fprintf(log, "memory-base,%08x\n", (unsigned)rc);
    if (rc) goto done;
    memset(memory, 0, BYTES);
    rc = sceGxmMapMemory(memory, BYTES, SCE_GXM_MEMORY_ATTRIB_RW);
    fprintf(log, "map,%08x\n", (unsigned)rc);
    if (rc) goto done;
    mapped = 1;
    config.size = sizeof(config);
    config.abi = PSP2_GPUPROF_ABI;
    /* Duplicate selectors permit checking repeatability across counter slots.
     * Transfer work may not increment either TA/3D selector: zero is not failure.
     */
    for (n = 0; n < PSP2_GPUPROF_COUNTERS; ++n)
        config.events[n].bit = n & 1;
    for (run = 0; run < 8; ++run) {
        Psp2GpuProfSample before, after;
        uint32_t color = 0xff000000u | (run + 1) * 0x00010101u;
        unsigned mismatch = 0, attempt;
        int work_rc, read_rc, end_rc;
        /* Warm-up is outside capture. Begin may still return OFFLINE if the
         * driver powers down immediately; report that, do not fake a sample.
         */
        fprintf(log, "call,%u,warmup\n", run);
        rc = fill(memory, ~color);
        fprintf(log, "warmup,%u,%08x\n", run, (unsigned)rc);
        if (rc) break;
        fprintf(log, "call,%u,Begin\n", run);
        rc = psp2GpuProfBegin(&config, &before);
        fprintf(log, "begin,%u,%d\n", run, rc);
        if (rc) {
            break;
        }
        fprintf(log, "call,%u,transfer-and-Read\n", run);
        work_rc = fill(memory, color);
        read_rc = psp2GpuProfRead(&before.session, &after);
        fprintf(log, "read,%u,%d\n", run, read_rc);
        fprintf(log, "call,%u,End\n", run);
        end_rc = psp2GpuProfEnd(&before.session);
        for (attempt = 0; end_rc == PSP2_GPUPROF_BUSY && attempt < 20; ++attempt) {
            sceKernelDelayThread(1000);
            end_rc = psp2GpuProfEnd(&before.session);
        }
        fprintf(log, "capture,%u,%08x,%d,%d\n", run, (unsigned)work_rc, read_rc, end_rc);
        log_sample(log, run, "before", &before);
        if (!read_rc) log_sample(log, run, "after", &after);
        if (!work_rc) {
            const volatile uint32_t *pixels = memory;
            for (n = 0; n < WIDTH * HEIGHT; ++n)
                mismatch += pixels[n] != color;
            fprintf(log, "verify,%u,%08x,%u\n", run, color, mismatch);
        }
        rc = work_rc ? work_rc : read_rc ? read_rc : end_rc ? end_rc : mismatch ? -1 : 0;
        fflush(log);
        if (rc) break; /* Process exit also triggers plugin lease cleanup. */
        {
            Psp2GpuProfSignalConfig signals_config = psp2GpuProfPipelineSignalConfig();
            Psp2GpuProfSignals signals;
            unsigned g, c;
            fprintf(log, "call,%u,ReadSignals\n", run);
            rc = psp2GpuProfReadSignals(&signals_config, &signals);
            fprintf(log, "signals-result,%u,%d\n", run, rc);
            if (rc) break;
            if (signals.group_count != signals_config.group_count) {
                rc = PSP2_GPUPROF_INTERNAL;
                break;
            }
            for (g = 0; g < signals.group_count; ++g) {
                fprintf(log, "signals,%u,%u,%u,%u", run, signals.groups[g],
                        signals.timer_before[g], signals.timer_after[g]);
                for (c = 0; c < PSP2_GPUPROF_CORES; ++c)
                    fprintf(log, ",%08x", signals.values[g][c]);
                fputc('\n', log);
            }
        }
    }
done:
    if (initialized) {
        cleanup_rc = sceGxmTransferFinish();
        fprintf(log, "finish,%08x\n", (unsigned)cleanup_rc);
        /* Never unmap/free memory while submitted work may still reference it. */
        if (!cleanup_rc && mapped) {
            cleanup_rc = sceGxmUnmapMemory(memory);
            fprintf(log, "unmap,%08x\n", (unsigned)cleanup_rc);
            if (!cleanup_rc) mapped = 0;
        }
        if (!cleanup_rc && !mapped) {
            cleanup_rc = sceGxmTerminate();
            fprintf(log, "terminate,%08x\n", (unsigned)cleanup_rc);
        }
        if (!rc && cleanup_rc) rc = cleanup_rc;
    }
    if (block >= 0 && !mapped) {
        cleanup_rc = sceKernelFreeMemBlock(block);
        fprintf(log, "free,%08x\n", (unsigned)cleanup_rc);
        if (!rc && cleanup_rc) rc = cleanup_rc;
    }
    fprintf(log, "result,%08x\n", (unsigned)rc);
    /* A capture without its evidence file must not report success. */
    if (fflush(log) != 0 || ferror(log)) rc = -1;
    if (fclose(log) != 0) rc = -1;
    return rc ? 1 : 0;
}
