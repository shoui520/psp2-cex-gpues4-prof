#include <psp2_gpuprof.h>
#include <assert.h>
#include <string.h>

int capture_gpu_delta(void (*workload)(void), uint32_t delta[4][8],
                      Psp2GpuProfSession *pending);

static int begin_result, read_result, end_result;
static unsigned began, worked, read_count, ended;

static void workload(void)
{
    assert(began == 1 && !read_count && !ended);
    ++worked;
}

int psp2GpuProfBegin(const Psp2GpuProfConfig *config, Psp2GpuProfSample *sample)
{
    unsigned c, n;
    assert(config->abi == PSP2_GPUPROF_ABI && config->size == sizeof(*config));
    for (n = 0; n < 8; ++n)
        assert(config->events[n].group == 0 && config->events[n].bit == (n & 1));
    ++began;
    if (begin_result) return begin_result;
    memset(sample, 0, sizeof(*sample));
    sample->session.lo = 123;
    sample->session.hi = 456;
    for (c = 0; c < 4; ++c)
        for (n = 0; n < 8; ++n)
            sample->counters[c][n] = UINT32_MAX - n;
    return 0;
}

int psp2GpuProfRead(const Psp2GpuProfSession *session, Psp2GpuProfSample *sample)
{
    unsigned c, n;
    assert(worked == 1 && !ended && session->lo == 123 && session->hi == 456);
    ++read_count;
    if (read_result) return read_result;
    memset(sample, 0, sizeof(*sample));
    for (c = 0; c < 4; ++c)
        for (n = 0; n < 8; ++n)
            sample->counters[c][n] = c;
    return 0;
}

int psp2GpuProfEnd(const Psp2GpuProfSession *session)
{
    assert(read_count == 1 && session->lo == 123 && session->hi == 456);
    ++ended;
    return end_result;
}

int main(void)
{
    const int results[] = {0, PSP2_GPUPROF_BUSY, PSP2_GPUPROF_OFFLINE,
                          PSP2_GPUPROF_STALE, PSP2_GPUPROF_INTERNAL};
    unsigned b, r, e, c, n;
    uint32_t delta[4][8];
    Psp2GpuProfSession pending;
    assert(capture_gpu_delta(0, delta, &pending) == PSP2_GPUPROF_INVALID);
    assert(capture_gpu_delta(workload, 0, &pending) == PSP2_GPUPROF_INVALID);
    assert(capture_gpu_delta(workload, delta, 0) == PSP2_GPUPROF_INVALID);
    assert(!began);
    for (b = 0; b < 5; ++b)
        for (r = 0; r < 5; ++r)
            for (e = 0; e < 5; ++e) {
                int result;
                begin_result = results[b];
                read_result = results[r];
                end_result = results[e];
                began = worked = read_count = ended = 0;
                memset(delta, 0xa5, sizeof(delta));
                pending.lo = pending.hi = 999;
                result = capture_gpu_delta(workload, delta, &pending);
                assert(result == (begin_result ? begin_result :
                                 read_result ? read_result : end_result));
                assert(began == 1 && worked == !b && read_count == !b && ended == !b);
                if (b || !e || end_result == PSP2_GPUPROF_STALE)
                    assert(!pending.lo && !pending.hi);
                else
                    assert(pending.lo == 123 && pending.hi == 456);
                for (c = 0; c < 4; ++c)
                    for (n = 0; n < 8; ++n)
                        assert(delta[c][n] == (result ? 0xa5a5a5a5u : c + n + 1));
            }
    return 0;
}
