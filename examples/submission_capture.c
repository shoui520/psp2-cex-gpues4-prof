/* Minimal standalone submission recorder. This records host/GXM events,
 * not yet attributed GPU time. Call on the application lifecycle thread;
 * quiesce graphics producers before start/stop. */
#include <psp2/kernel/threadmgr.h>
#include <psp2_gpuprof_capture.h>
#include <psp2_gpuprof_gxm_observer.h>
#include <stdlib.h>
#ifdef GPUPROF_EXAMPLE_VITAGL
#include <psp2_gpuprof_vitagl.h>
#endif

static Psp2GpuProfCaptureEvent events[4096];
static Psp2GpuProfCapture capture;
static SceUID mutex = -1;

static void acquire(void *unused) {
    (void)unused;
    if (sceKernelLockMutex(mutex, 1, NULL) < 0) abort();
}
static void release(void *unused) {
    (void)unused;
    if (sceKernelUnlockMutex(mutex, 1) < 0) abort();
}

/* Call before initializing GXM (or vitaGL) for complete startup history.
 * In a long-running application, a persistent identity registry is required
 * before enabling capture later; this example intentionally captures startup. */
int submission_capture_start(void) {
    if (mutex >= 0) return PSP2_GPUPROF_CAPTURE_STATE;
    mutex = sceKernelCreateMutex("gpuprof-capture", 0, 0, NULL);
    if (mutex < 0) return mutex;
    Psp2GpuProfCaptureConfig config = {events, 4096, acquire, release, NULL};
    int rc = psp2GpuProfCaptureInit(&capture, &config);
    if (!rc) rc = psp2GpuProfCaptureStart(&capture);
    if (rc) {
        sceKernelDeleteMutex(mutex);
        mutex = -1;
        return rc;
    }
    psp2GpuProfGxmSetObserver(psp2GpuProfCaptureGxmCall, &capture);
#ifdef GPUPROF_EXAMPLE_VITAGL
    psp2GpuProfVitaGLCapture(&capture);
#endif
    return 0;
}

/* Use this capture with a separate Psp2GpuProfLabels per submitting thread. */
Psp2GpuProfCapture *submission_capture_buffer(void) { return &capture; }

/* Graphics producers must be quiescent. No GPU drain is added here.
 * File is caller-owned, e.g. fopen("ux0:data/my-capture.csv", "w"). */
int submission_capture_save(FILE *file) {
    if (mutex < 0) return PSP2_GPUPROF_CAPTURE_STATE;
#ifdef GPUPROF_EXAMPLE_VITAGL
    psp2GpuProfVitaGLCapture(NULL);
#endif
    psp2GpuProfGxmSetObserver(NULL, NULL);
    int rc = psp2GpuProfCaptureStop(&capture);
    if (!rc) rc = psp2GpuProfCaptureWriteCsv(&capture, file);
    sceKernelDeleteMutex(mutex);
    mutex = -1;
    return rc;
}
