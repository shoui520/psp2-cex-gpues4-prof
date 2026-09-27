#include "session.h"
#include <string.h>

#define DEBUG_ENABLE 0x78u
#define PERF_CONTROL 0x40u
#define GPU_TIMER 0x144u
#define CORE_BASE(c) (0x8000u + (c) * 0x4000u)
#define SELECTOR(c, p) (CORE_BASE(c) + 0x90u + (p) * 4u)
#define COUNTER(c, n) (CORE_BASE(c) + 0x44u + (n) * 4u)

static uint32_t rd(struct gp_session *s, uint32_t off)
{
    return s->io.read(s->io.ctx, off);
}

static void wr(struct gp_session *s, uint32_t off, uint32_t value)
{
    s->io.write(s->io.ctx, off, value);
}

void gp_init(struct gp_session *s, const struct gp_io *io)
{
    memset(s, 0, sizeof(*s));
    s->io = *io;
}

static int check(struct gp_session *s, int32_t pid, Psp2GpuProfSession h)
{
    uint64_t generation = ((uint64_t)h.hi << 32) | h.lo;
    if (!s->active || generation != s->generation)
        return PSP2_GPUPROF_STALE;
    if (pid != s->owner)
        return PSP2_GPUPROF_DENIED;
    return 0;
}

static void snapshot(struct gp_session *s, Psp2GpuProfSample *out)
{
    unsigned c, n;
    memset(out, 0, sizeof(*out));
    out->size = sizeof(*out);
    out->abi = PSP2_GPUPROF_ABI;
    out->session.lo = (uint32_t)s->generation;
    out->session.hi = (uint32_t)(s->generation >> 32);
    out->sequence = s->sequence++;
    out->gpu_timer_before = rd(s, GPU_TIMER);
    for (c = 0; c < 4; ++c)
        for (n = 0; n < 8; ++n)
            out->counters[c][n] = rd(s, COUNTER(c, n));
    out->gpu_timer_after = rd(s, GPU_TIMER);
}

static void restore(struct gp_session *s)
{
    unsigned c, p;
    wr(s, DEBUG_ENABLE, s->saved_enable & ~1u);
    for (c = 0; c < 4; ++c)
        for (p = 0; p < 4; ++p)
            wr(s, SELECTOR(c, p), s->saved_selectors[c][p]);
    wr(s, DEBUG_ENABLE, s->saved_enable);
}

int gp_begin(struct gp_session *s, int32_t pid, const Psp2GpuProfConfig *cfg,
             Psp2GpuProfSample *out)
{
    unsigned c, p;
    int rc;
    uint32_t value;
    s->begin_failure_offset = s->begin_failure_value = 0;
    if (!cfg || !out || pid < 0 || cfg->size != sizeof(*cfg) ||
        cfg->abi != PSP2_GPUPROF_ABI)
        return PSP2_GPUPROF_INVALID;
    for (p = 0; p < 4; ++p)
        if (cfg->reserved[p]) return PSP2_GPUPROF_INVALID;
    for (p = 0; p < 8; ++p)
        if (cfg->events[p].group > 127 || cfg->events[p].bit > 31)
            return PSP2_GPUPROF_INVALID;
    if (s->active) return PSP2_GPUPROF_BUSY;
    /* Never recycle a handle, including on generation overflow. */
    if (s->generation == UINT64_MAX) return PSP2_GPUPROF_INTERNAL;
    rc = s->io.acquire(s->io.ctx);
    if (rc) return rc;
    /* SGX543MP4 only; do not guess a different core/register layout. */
    value = rd(s, 0x4000);
    if ((value & 3u) != 3u) {
        s->begin_failure_offset = 0x4000;
        s->begin_failure_value = value;
        rc = PSP2_GPUPROF_UNSUPPORTED;
        goto done;
    }
    s->saved_enable = rd(s, DEBUG_ENABLE);
    if (s->saved_enable & 1u) {
        rc = PSP2_GPUPROF_BUSY;
        goto done;
    }
    for (c = 0; c < 4; ++c) {
        if (rd(s, CORE_BASE(c) + DEBUG_ENABLE) & 1u) {
            rc = PSP2_GPUPROF_BUSY;
            goto done;
        }
    }
    /* Retail request 3 leaves PERF=0xff on reset-only, but writes PERF=0
     * after event selection when starting collection. Accept only those two
     * observed driver states, with every debug-enable bit already clear.
     * Other control modes remain unsupported; never assert reset ourselves.
     */
    value = rd(s, PERF_CONTROL);
    if (value != 0 && value != 0xff) {
        s->begin_failure_offset = PERF_CONTROL;
        s->begin_failure_value = value;
        rc = PSP2_GPUPROF_UNSUPPORTED;
        goto done;
    }
    for (c = 0; c < 4; ++c) {
        value = rd(s, CORE_BASE(c) + PERF_CONTROL);
        if (value != 0 && value != 0xff) {
            s->begin_failure_offset = CORE_BASE(c) + PERF_CONTROL;
            s->begin_failure_value = value;
            rc = PSP2_GPUPROF_UNSUPPORTED;
            goto done;
        }
    }
    for (c = 0; c < 4; ++c)
        for (p = 0; p < 4; ++p)
            s->saved_selectors[c][p] = rd(s, SELECTOR(c, p));
    /* Match retail enable -> select events -> release PERF clear sequence. */
    wr(s, DEBUG_ENABLE, s->saved_enable | 1u);
    for (c = 0; c < 4; ++c) {
        for (p = 0; p < 4; ++p) {
            const Psp2GpuProfEvent *a = &cfg->events[p * 2];
            const Psp2GpuProfEvent *b = a + 1;
            wr(s, SELECTOR(c, p), a->group | (a->bit << 8) |
               (b->group << 16) | (b->bit << 24));
        }
    }
    wr(s, PERF_CONTROL, 0);
    /* Do not assume global-to-core propagation or readable zero mode. Check
     * it before returning a handle. On mismatch restore selectors/enables,
     * but never write the inherited 0xff back (that would assert clears).
     */
    for (c = 0; c < 5; ++c) {
        uint32_t offset = c ? CORE_BASE(c-1) + PERF_CONTROL : PERF_CONTROL;
        value = rd(s, offset);
        if (value != 0) {
            s->begin_failure_offset = offset;
            s->begin_failure_value = value;
            restore(s);
            rc = PSP2_GPUPROF_UNSUPPORTED;
            goto done;
        }
    }
    ++s->generation;
    s->owner = pid;
    s->sequence = 0;
    s->active = 1;
    snapshot(s, out);
    rc = 0;
done:
    s->io.release(s->io.ctx);
    return rc;
}

int gp_read(struct gp_session *s, int32_t pid, Psp2GpuProfSession h,
            Psp2GpuProfSample *out)
{
    int rc;
    if (!out) return PSP2_GPUPROF_INVALID;
    rc = check(s, pid, h);
    if (rc) return rc;
    rc = s->io.acquire(s->io.ctx);
    if (rc) return rc;
    snapshot(s, out);
    s->io.release(s->io.ctx);
    return 0;
}

int gp_end(struct gp_session *s, int32_t pid, Psp2GpuProfSession h)
{
    int rc = check(s, pid, h);
    if (rc) return rc;
    rc = s->io.acquire(s->io.ctx);
    if (rc) return rc;
    restore(s);
    s->active = 0;
    s->io.release(s->io.ctx);
    return 0;
}

void gp_invalidate(struct gp_session *s, int powered)
{
    if (!s->active) return;
    if (powered) restore(s);
    s->active = 0;
}

static int valid_group(uint32_t g)
{
    return g <= 56 || (g >= 64 && g <= 66) || (g >= 68 && g <= 90) ||
           (g >= 96 && g <= 98) || (g >= 100 && g <= 122);
}

int gp_signals(struct gp_session *s, const Psp2GpuProfSignalConfig *cfg,
               Psp2GpuProfSignals *out)
{
    uint32_t saved[4][3];
    unsigned c, i;
    int rc;
    if (!cfg || !out || cfg->size != sizeof(*cfg) || cfg->abi != PSP2_GPUPROF_ABI ||
        !cfg->group_count || cfg->group_count > PSP2_GPUPROF_MAX_GROUPS)
        return PSP2_GPUPROF_INVALID;
    for (i = 0; i < 4; ++i)
        if (cfg->reserved[i]) return PSP2_GPUPROF_INVALID;
    for (i = 0; i < PSP2_GPUPROF_MAX_GROUPS; ++i)
        if (i < cfg->group_count ? !valid_group(cfg->groups[i]) : cfg->groups[i] != 0)
            return PSP2_GPUPROF_INVALID;
    if (s->active) return PSP2_GPUPROF_BUSY;
    rc = s->io.acquire(s->io.ctx);
    if (rc) return rc;
    if ((rd(s, 0x4000) & 3u) != 3u) { rc = PSP2_GPUPROF_UNSUPPORTED; goto done; }
    if (rd(s, DEBUG_ENABLE) & 1u) { rc = PSP2_GPUPROF_BUSY; goto done; }
    for (c = 0; c < 4; ++c) {
        saved[c][0] = rd(s, CORE_BASE(c) + DEBUG_ENABLE);
        if (saved[c][0] & 1u) { rc = PSP2_GPUPROF_BUSY; goto done; }
        saved[c][1] = rd(s, SELECTOR(c, 0));
        saved[c][2] = rd(s, SELECTOR(c, 1));
    }
    memset(out, 0, sizeof(*out));
    out->size = sizeof(*out);
    out->abi = PSP2_GPUPROF_ABI;
    out->group_count = cfg->group_count;
    for (c = 0; c < 4; ++c)
        wr(s, CORE_BASE(c) + DEBUG_ENABLE, saved[c][0] | 1u);
    for (i = 0; i < cfg->group_count; ++i) {
        uint32_t g = cfg->groups[i];
        out->groups[i] = g;
        out->timer_before[i] = rd(s, GPU_TIMER);
        for (c = 0; c < 4; ++c) {
            wr(s, SELECTOR(c, 0), g | (g << 16) | 0x08000000u);
            wr(s, SELECTOR(c, 1), g | (g << 16) | 0x18001000u);
            out->values[i][c] = rd(s, CORE_BASE(c) + 0x70u);
        }
        out->timer_after[i] = rd(s, GPU_TIMER);
    }
    for (c = 0; c < 4; ++c) {
        wr(s, SELECTOR(c, 0), saved[c][1]);
        wr(s, SELECTOR(c, 1), saved[c][2]);
        wr(s, CORE_BASE(c) + DEBUG_ENABLE, saved[c][0]);
    }
    rc = 0;
done:
    s->io.release(s->io.ctx);
    return rc;
}
