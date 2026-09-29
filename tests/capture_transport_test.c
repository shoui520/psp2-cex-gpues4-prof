#include "psp2_gpuprof_capture.h"
#include "psp2_gpuprof_gxm_observer.h"
#include <assert.h>
#include <string.h>

static unsigned held;
static void acquire(void *p) { (void)p; assert(!held); held = 1; }
static void release(void *p) { (void)p; assert(held); held = 0; }
int main(void) {
    Psp2GpuProfCaptureEvent storage[2];
    Psp2GpuProfCapture c;
    Psp2GpuProfCaptureConfig cfg = {storage, 2, acquire, release, NULL};
    assert(!psp2GpuProfCaptureInit(&c, &cfg));
    FILE *f = tmpfile();
    assert(f);
    assert(psp2GpuProfCaptureWriteCsv(&c, f) == PSP2_GPUPROF_CAPTURE_STATE);
    assert(!psp2GpuProfCaptureStart(&c));
    Psp2GpuProfCaptureEvent event = {.kind = PSP2_GPUPROF_EVENT_DRAW,
        .context = 7, .thread = 9, .before_us = 11, .after_us = 12};
    char label[] = "water, \"reflection\"\npass";
    assert(!psp2GpuProfCaptureRecord(&c, &event, label));
    label[0] = 'X';
    assert(storage[0].label[0] == 'w');
    char long_label[200];
    memset(long_label, 'a', sizeof(long_label));
    long_label[199] = 0;
    assert(!psp2GpuProfCaptureRecord(&c, &event, long_label));
    assert(storage[1].flags & PSP2_GPUPROF_EVENT_LABEL_TRUNCATED);
    assert(storage[1].sequence == 1);
    assert(psp2GpuProfCaptureRecord(&c, &event, NULL) == PSP2_GPUPROF_CAPTURE_FULL);
    assert(!psp2GpuProfCaptureStop(&c));
    assert(psp2GpuProfCaptureRecord(&c, &event, NULL) == PSP2_GPUPROF_CAPTURE_STATE);
    assert(!psp2GpuProfCaptureWriteCsv(&c, f));
    rewind(f);
    char text[2048] = {0};
    assert(fread(text, 1, sizeof(text)-1, f));
    assert(strstr(text, "gpuprof-capture,1\n"));
    assert(strstr(text, "\"water, \"\"reflection\"\"\npass\""));
    assert(strstr(text, "end,2,1\n"));
    fclose(f);
    assert(!psp2GpuProfCaptureStart(&c));
    Psp2GpuProfGxmCall call = {.kind = PSP2_GPUPROF_GXM_DRAW_INSTANCED,
        .context = 0xabc, .thread = 4, .before_us = 10, .after_us = 12,
        .args = {1,2,3,4,5,6,7,8}, .result = -11};
    psp2GpuProfCaptureGxmCall(&c,&call);
    assert(c.count == 1 && storage[0].kind == PSP2_GPUPROF_EVENT_GXM_CALL);
    assert(storage[0].object == PSP2_GPUPROF_GXM_DRAW_INSTANCED);
    assert(storage[0].context == 0xabc && storage[0].args[7] == 8);
    assert(storage[0].result == -11);
    assert(!psp2GpuProfCaptureStop(&c));
    assert(!held);
    return 0;
}
