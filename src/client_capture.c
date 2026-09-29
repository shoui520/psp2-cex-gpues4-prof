#include "psp2_gpuprof_capture.h"
#include <inttypes.h>
#include <string.h>

enum { READY = 1, RECORDING, STOPPED };
static void lock(Psp2GpuProfCapture *c) {
    if (c->config.lock) c->config.lock(c->config.lock_userdata);
}
static void unlock(Psp2GpuProfCapture *c) {
    if (c->config.unlock) c->config.unlock(c->config.lock_userdata);
}

int psp2GpuProfCaptureInit(Psp2GpuProfCapture *c,
                          const Psp2GpuProfCaptureConfig *cfg) {
    if (!c || !cfg || !cfg->events || !cfg->capacity ||
        cfg->capacity > SIZE_MAX / sizeof(*cfg->events) ||
        (!!cfg->lock != !!cfg->unlock)) return PSP2_GPUPROF_CAPTURE_INVALID;
    memset(c, 0, sizeof(*c));
    c->config = *cfg;
    c->state = READY;
    return 0;
}

int psp2GpuProfCaptureStart(Psp2GpuProfCapture *c) {
    if (!c || !c->state) return PSP2_GPUPROF_CAPTURE_INVALID;
    lock(c);
    if (c->state == RECORDING) { unlock(c); return PSP2_GPUPROF_CAPTURE_STATE; }
    c->count = 0;
    c->dropped = 0;
    c->next_sequence = 0;
    c->state = RECORDING;
    unlock(c);
    return 0;
}

int psp2GpuProfCaptureRecord(Psp2GpuProfCapture *c,
                            const Psp2GpuProfCaptureEvent *event,
                            const char *label) {
    if (!c || !event || event->kind < PSP2_GPUPROF_EVENT_CONTEXT_CREATE ||
        event->kind > PSP2_GPUPROF_EVENT_GXM_IDENTITY_SEED ||
        event->before_us > event->after_us)
        return PSP2_GPUPROF_CAPTURE_INVALID;
    lock(c);
    if (c->state != RECORDING) { unlock(c); return PSP2_GPUPROF_CAPTURE_STATE; }
    if (c->count == c->config.capacity) {
        if (c->dropped != UINT64_MAX) ++c->dropped;
        unlock(c);
        return PSP2_GPUPROF_CAPTURE_FULL;
    }
    Psp2GpuProfCaptureEvent *out = &c->config.events[c->count++];
    *out = *event;
    out->sequence = c->next_sequence++;
    memset(out->label, 0, sizeof(out->label));
    if (label) {
        size_t i = 0;
        while (i + 1 < sizeof(out->label) && label[i]) {
            out->label[i] = label[i];
            ++i;
        }
        if (label[i]) out->flags |= PSP2_GPUPROF_EVENT_LABEL_TRUNCATED;
    }
    unlock(c);
    return 0;
}

int psp2GpuProfCaptureRecordBatch(Psp2GpuProfCapture *c,
                                 const Psp2GpuProfCaptureEvent *events, size_t count) {
    if (!c || !events || !count) return PSP2_GPUPROF_CAPTURE_INVALID;
    for (size_t i=0;i<count;++i) {
        const Psp2GpuProfCaptureEvent *e=&events[i];
        if (e->kind<PSP2_GPUPROF_EVENT_CONTEXT_CREATE ||
            e->kind>PSP2_GPUPROF_EVENT_GXM_IDENTITY_SEED ||
            e->before_us>e->after_us || !memchr(e->label,0,sizeof(e->label)))
            return PSP2_GPUPROF_CAPTURE_INVALID;
    }
    lock(c);
    if (c->state!=RECORDING) { unlock(c); return PSP2_GPUPROF_CAPTURE_STATE; }
    if (count>c->config.capacity-c->count) {
        c->dropped=UINT64_MAX-c->dropped<count ? UINT64_MAX : c->dropped+count;
        unlock(c);
        return PSP2_GPUPROF_CAPTURE_FULL;
    }
    for (size_t i=0;i<count;++i) {
        Psp2GpuProfCaptureEvent *out=&c->config.events[c->count++];
        *out=events[i];
        out->sequence=c->next_sequence++;
    }
    unlock(c);
    return 0;
}

int psp2GpuProfCaptureStop(Psp2GpuProfCapture *c) {
    if (!c) return PSP2_GPUPROF_CAPTURE_INVALID;
    lock(c);
    if (c->state != RECORDING) { unlock(c); return PSP2_GPUPROF_CAPTURE_STATE; }
    c->state = STOPPED;
    unlock(c);
    return 0;
}

static void quoted(FILE *f, const char *s) {
    fputc('"', f);
    for (; *s; ++s) {
        if (*s == '"') fputc('"', f);
        fputc((unsigned char)*s, f);
    }
    fputc('"', f);
}

int psp2GpuProfCaptureWriteCsv(Psp2GpuProfCapture *c, FILE *f) {
    if (!c || !f) return PSP2_GPUPROF_CAPTURE_INVALID;
    lock(c);
    unsigned state = c->state;
    unlock(c);
    if (state != STOPPED) return PSP2_GPUPROF_CAPTURE_STATE;
    fprintf(f, "gpuprof-capture,%u\n", PSP2_GPUPROF_CAPTURE_VERSION);
    fputs("columns,sequence,kind,before_us,after_us,thread,context,frame,scene,scope,object,arg0,arg1,arg2,arg3,arg4,arg5,arg6,arg7,flags,result,label\n", f);
    for (size_t i = 0; i < c->count; ++i) {
        const Psp2GpuProfCaptureEvent *e = &c->config.events[i];
        fprintf(f, "event,%" PRIu64 ",%u,%" PRIu64 ",%" PRIu64
                ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
                ",%" PRIu64 ",%" PRIu64,
                e->sequence, e->kind, e->before_us, e->after_us,
                e->thread, e->context, e->frame, e->scene, e->scope, e->object);
        for (unsigned j = 0; j < 8; ++j) fprintf(f, ",%" PRIu64, e->args[j]);
        fprintf(f, ",%u,%" PRId32 ",", e->flags, e->result);
        quoted(f, e->label);
        fputc('\n', f);
    }
    /* Some Vita printf implementations do not support the z modifier.
     * Use the same fixed-width formatting already used for event fields. */
    fprintf(f, "end,%" PRIu64 ",%" PRIu64 "\n", (uint64_t)c->count, c->dropped);
    return fflush(f) || ferror(f) ? PSP2_GPUPROF_CAPTURE_IO : 0;
}
