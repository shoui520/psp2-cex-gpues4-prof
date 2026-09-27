#include <psp2_gpuprof_trace.h>
#include <assert.h>
#include <string.h>

int main(void)
{
    Psp2GpuProfTrace t;
    Psp2GpuProfRecord storage[2], in = {0}, out;
    unsigned i;
    assert(psp2GpuProfTraceInit(&t, storage, 0) == PSP2_GPUPROF_INVALID);
    assert(psp2GpuProfTraceInit(&t, storage, 2) == 0);
    assert(psp2GpuProfTraceRead(&t, &out) == 0);
    assert(psp2GpuProfTraceAppend(&t, &in) == PSP2_GPUPROF_INVALID);
    in.kind = PSP2_GPUPROF_FRAME_BEGIN;
    in.id = 42;
    in.cpu_before_us = in.cpu_after_us = 100;
    memset(&in.data, 0xa5, sizeof(in.data));
    assert(psp2GpuProfTraceAppend(&t, &in) == 0);
    in.kind = PSP2_GPUPROF_FRAME_END;
    assert(psp2GpuProfTraceAppend(&t, &in) == 0);
    assert(psp2GpuProfTraceAppend(&t, &in) == PSP2_GPUPROF_BUSY);
    assert(t.dropped == 1 && t.next_sequence == 3);
    assert(psp2GpuProfTraceRead(&t, &out) == 1 && out.sequence == 0 && out.id == 42);
    assert(out.data.signals.values[7][3] == 0);
    assert(psp2GpuProfTraceAppend(&t, &in) == 0);
    assert(psp2GpuProfTraceRead(&t, &out) == 1 && out.sequence == 1);
    assert(psp2GpuProfTraceRead(&t, &out) == 1 && out.sequence == 3);
    assert(psp2GpuProfTraceRead(&t, &out) == 0 && t.dropped == 1);
    for (i = 0; i < 100; ++i) {
        in.kind = PSP2_GPUPROF_API_ERROR;
        in.data.error = PSP2_GPUPROF_STALE;
        assert(psp2GpuProfTraceAppend(&t, &in) == 0);
        assert(psp2GpuProfTraceRead(&t, &out) == 1);
        assert(out.data.error == PSP2_GPUPROF_STALE && out.sequence == 4+i);
    }
    in.kind = PSP2_GPUPROF_COUNTER_SAMPLE;
    assert(psp2GpuProfTraceAppend(&t, &in) == PSP2_GPUPROF_INVALID);
    memset(&in.data, 0, sizeof(in.data));
    in.data.counters.size = sizeof(Psp2GpuProfSample);
    in.data.counters.abi = PSP2_GPUPROF_ABI;
    in.data.counters.counters[3][7] = 1234;
    assert(psp2GpuProfTraceAppend(&t, &in) == 0);
    assert(psp2GpuProfTraceRead(&t, &out) == 1 && out.data.counters.counters[3][7] == 1234);
    in.kind = PSP2_GPUPROF_SIGNAL_SAMPLE;
    memset(&in.data, 0, sizeof(in.data));
    in.data.signals.size = sizeof(Psp2GpuProfSignals);
    in.data.signals.abi = PSP2_GPUPROF_ABI;
    in.data.signals.group_count = 8;
    in.data.signals.values[7][3] = 5678;
    assert(psp2GpuProfTraceAppend(&t, &in) == 0);
    assert(psp2GpuProfTraceRead(&t, &out) == 1 && out.data.signals.values[7][3] == 5678);
    in.cpu_after_us = 99;
    assert(psp2GpuProfTraceAppend(&t, &in) == PSP2_GPUPROF_INVALID);
    in.cpu_after_us = 100;
    t.next_sequence = UINT64_MAX;
    assert(psp2GpuProfTraceAppend(&t, &in) == PSP2_GPUPROF_INTERNAL);
    return 0;
}
