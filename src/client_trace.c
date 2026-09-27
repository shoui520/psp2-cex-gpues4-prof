#include "psp2_gpuprof_trace.h"
#include <string.h>

static int valid(const Psp2GpuProfTrace *t)
{
    return t && t->storage && t->capacity && t->head < t->capacity &&
           t->count <= t->capacity;
}

int psp2GpuProfTraceInit(Psp2GpuProfTrace *t, Psp2GpuProfRecord *storage,
                       uint32_t capacity)
{
    if (!t || !storage || !capacity || capacity > UINT32_MAX / sizeof(*storage))
        return PSP2_GPUPROF_INVALID;
    memset(t, 0, sizeof(*t));
    t->storage = storage;
    t->capacity = capacity;
    return 0;
}

int psp2GpuProfTraceAppend(Psp2GpuProfTrace *t, const Psp2GpuProfRecord *r)
{
    Psp2GpuProfRecord *out;
    uint32_t index;
    if (!valid(t) || !r || r->kind < PSP2_GPUPROF_FRAME_BEGIN ||
        r->kind > PSP2_GPUPROF_API_ERROR || r->cpu_after_us < r->cpu_before_us)
        return PSP2_GPUPROF_INVALID;
    if (r->kind == PSP2_GPUPROF_COUNTER_SAMPLE &&
        (r->data.counters.size != sizeof(r->data.counters) ||
         r->data.counters.abi != PSP2_GPUPROF_ABI)) return PSP2_GPUPROF_INVALID;
    if (r->kind == PSP2_GPUPROF_SIGNAL_SAMPLE &&
        (r->data.signals.size != sizeof(r->data.signals) ||
         r->data.signals.abi != PSP2_GPUPROF_ABI ||
         !r->data.signals.group_count || r->data.signals.group_count > 8))
        return PSP2_GPUPROF_INVALID;
    if (t->next_sequence == UINT64_MAX) return PSP2_GPUPROF_INTERNAL;
    if (t->count == t->capacity) {
        ++t->next_sequence;
        if (t->dropped != UINT64_MAX) ++t->dropped;
        return PSP2_GPUPROF_BUSY;
    }
    /* Avoid overflow of head+count even for very large caller buffers. */
    index = t->count >= t->capacity - t->head ?
        t->count - (t->capacity - t->head) : t->head + t->count;
    out = &t->storage[index];
    memset(out, 0, sizeof(*out));
    out->sequence = t->next_sequence++;
    out->cpu_before_us = r->cpu_before_us;
    out->cpu_after_us = r->cpu_after_us;
    out->kind = r->kind;
    out->id = r->id;
    out->thread_id = r->thread_id;
    if (r->kind == PSP2_GPUPROF_COUNTER_SAMPLE) out->data.counters = r->data.counters;
    else if (r->kind == PSP2_GPUPROF_SIGNAL_SAMPLE) out->data.signals = r->data.signals;
    else if (r->kind == PSP2_GPUPROF_API_ERROR) out->data.error = r->data.error;
    ++t->count;
    return 0;
}

int psp2GpuProfTraceRead(Psp2GpuProfTrace *t, Psp2GpuProfRecord *r)
{
    if (!valid(t) || !r) return PSP2_GPUPROF_INVALID;
    if (!t->count) return 0;
    *r = t->storage[t->head];
    if (++t->head == t->capacity) t->head = 0;
    --t->count;
    return 1;
}
