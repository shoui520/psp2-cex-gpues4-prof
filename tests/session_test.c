#include "session.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct device {
    uint32_t regs[0x20000 / 4];
    unsigned reads, writes, held, releases;
    int failure;
    int broadcast, stuck;
    unsigned offsets[128];
};

static int acquire(void *ctx)
{
    struct device *d = ctx;
    assert(!d->held);
    if (d->failure) return d->failure;
    d->held = 1;
    return 0;
}

static void release(void *ctx)
{
    struct device *d = ctx;
    assert(d->held);
    d->held = 0;
    ++d->releases;
}

static uint32_t read_reg(void *ctx, uint32_t offset)
{
    struct device *d = ctx;
    assert(d->held && !(offset & 3) && offset < sizeof(d->regs));
    ++d->reads;
    return d->regs[offset / 4];
}

static void write_reg(void *ctx, uint32_t offset, uint32_t value)
{
    struct device *d = ctx;
    assert(d->held && !(offset & 3) && offset < sizeof(d->regs));
    assert(offset != 0xe80 && (offset != 0x40 || value == 0));
    if (d->writes < 128) d->offsets[d->writes] = offset;
    ++d->writes;
    if (offset == 0x40 && d->stuck) return;
    d->regs[offset / 4] = value;
    if (offset == 0x40 && d->broadcast)
        for (unsigned c=0; c<4; ++c) d->regs[(0x8040+c*0x4000)/4] = value;
}

static void setup(struct gp_session *s, struct device *d, Psp2GpuProfConfig *cfg)
{
    struct gp_io io = {d, acquire, release, read_reg, write_reg};
    unsigned c, p;
    memset(d, 0, sizeof(*d));
    memset(cfg, 0, sizeof(*cfg));
    cfg->size = sizeof(*cfg);
    cfg->abi = PSP2_GPUPROF_ABI;
    for (p = 0; p < 8; ++p) {
        cfg->events[p].group = p;
        cfg->events[p].bit = 31-p;
    }
    d->regs[0x4000/4] = 3;
    d->regs[0x78/4] = 0x100; /* Preserve non-enable bits. */
    for (c = 0; c < 4; ++c)
        for (p = 0; p < 4; ++p)
            d->regs[(0x8090+c*0x4000+p*4)/4] = 0x12340000+c*4+p;
    gp_init(s, &io);
}

int main(void)
{
    static struct device d;
    struct gp_session s;
    Psp2GpuProfConfig cfg;
    Psp2GpuProfSample sample, other;
    Psp2GpuProfSession old;
    Psp2GpuProfSignalConfig signal_cfg = {0};
    Psp2GpuProfSignals signals;
    unsigned c, p, reads, writes;
    _Static_assert(sizeof(Psp2GpuProfConfig) == 88, "config ABI");
    _Static_assert(sizeof(Psp2GpuProfSample) == 172, "sample ABI");
    _Static_assert(sizeof(Psp2GpuProfSession) == 8, "handle ABI");
    _Static_assert(sizeof(Psp2GpuProfInfo) == 40, "info ABI");
    _Static_assert(sizeof(Psp2GpuProfSignalConfig) == 60, "signal config ABI");
    _Static_assert(sizeof(Psp2GpuProfSignals) == 252, "signals ABI");

    setup(&s, &d, &cfg);
    cfg.events[0].group = 128;
    assert(gp_begin(&s, 7, &cfg, &sample) == PSP2_GPUPROF_INVALID);
    cfg.events[0].group = 0;
    cfg.reserved[3] = 1;
    assert(gp_begin(&s, 7, &cfg, &sample) == PSP2_GPUPROF_INVALID);
    cfg.reserved[3] = 0;
    assert(!d.reads && !d.writes && !d.releases);
    d.failure = PSP2_GPUPROF_OFFLINE;
    assert(gp_begin(&s, 7, &cfg, &sample) == PSP2_GPUPROF_OFFLINE);
    assert(!d.reads && !d.writes && !d.releases);
    d.failure = 0;
    d.regs[0x4000/4] = 1;
    assert(gp_begin(&s, 7, &cfg, &sample) == PSP2_GPUPROF_UNSUPPORTED);
    assert(!d.writes && !d.held);
    d.regs[0x4000/4] = 3;
    d.regs[0x8078/4] = 1;
    assert(gp_begin(&s, 7, &cfg, &sample) == PSP2_GPUPROF_BUSY);
    assert(!d.writes && !d.held);
    d.regs[0x8078/4] = 0;

    /* Reject inherited reset/SUM_MUX/reserved modes without changing MMIO,
     * consuming a handle, or filling the caller's sample. Check every bank.
     */
    memset(&sample, 0xa5, sizeof(sample));
    other = sample;
    for (c = 0; c < 5; ++c) {
        unsigned off = c ? 0x8040 + (c-1)*0x4000 : 0x40;
        for (p = 0; p < 32; ++p) {
            d.regs[off/4] = UINT32_C(1) << p;
            assert(gp_begin(&s, 7, &cfg, &sample) == PSP2_GPUPROF_UNSUPPORTED);
            assert(!d.writes && !d.held && !s.active && !s.generation);
            assert(memcmp(&sample, &other, sizeof(sample)) == 0);
        }
        d.regs[off/4] = 0;
    }

    assert(gp_begin(&s, 7, &cfg, &sample) == 0);
    assert(sample.sequence == 0 && sample.abi == 1 && sample.size == sizeof(sample));
    assert(d.writes == 18 && d.regs[0x78/4] == 0x101 && !d.held);
    assert(d.offsets[0] == 0x78 && d.offsets[17] == 0x40);
    for (c = 0; c < 4; ++c)
        for (p = 0; p < 4; ++p)
            assert(d.regs[(0x8090+c*0x4000+p*4)/4] ==
                   ((p*2) | ((31-p*2)<<8) | ((p*2+1)<<16) | ((30-p*2)<<24)));
    old = sample.session;
    reads = d.reads;
    writes = d.writes;
    assert(gp_begin(&s, 8, &cfg, &other) == PSP2_GPUPROF_BUSY);
    assert(gp_read(&s, 8, old, &other) == PSP2_GPUPROF_DENIED);
    assert(gp_end(&s, 8, old) == PSP2_GPUPROF_DENIED);
    assert(d.reads == reads && d.writes == writes);
    d.regs[0x8044/4] = UINT32_MAX;
    d.regs[0x14060/4] = 42;
    assert(gp_read(&s, 7, old, &other) == 0 && other.sequence == 1);
    assert(other.counters[0][0] == UINT32_MAX && other.counters[3][7] == 42);
    d.failure = PSP2_GPUPROF_BUSY;
    assert(gp_end(&s, 7, old) == PSP2_GPUPROF_BUSY && s.active);
    d.failure = 0;
    assert(gp_end(&s, 7, old) == 0 && !s.active && !d.held);
    assert(d.regs[0x78/4] == 0x100 && d.regs[0x40/4] == 0);
    for (c = 0; c < 4; ++c)
        for (p = 0; p < 4; ++p)
            assert(d.regs[(0x8090+c*0x4000+p*4)/4] == 0x12340000+c*4+p);
    assert(gp_read(&s, 7, old, &other) == PSP2_GPUPROF_STALE);
    assert(gp_begin(&s, 7, &cfg, &sample) == 0);
    assert(gp_read(&s, 7, old, &other) == PSP2_GPUPROF_STALE);
    assert(acquire(&d) == 0); /* Driver transition hook already holds power lock. */
    gp_invalidate(&s, 1);
    release(&d);
    assert(!s.active && d.regs[0x78/4] == 0x100);
    assert(gp_begin(&s, 7, &cfg, &sample) == 0);
    writes = d.writes;
    gp_invalidate(&s, 0); /* Unpowered invalidation must not access registers. */
    assert(!s.active && d.writes == writes && !d.held);
    setup(&s, &d, &cfg);
    s.generation = UINT64_MAX;
    assert(gp_begin(&s, 7, &cfg, &sample) == PSP2_GPUPROF_INTERNAL);
    assert(!d.reads && !d.writes);
    /* Measured global FF, and explicit models of possible per-core readback.
     * Neither model claims physical broadcast semantics. */
    for (unsigned mode=0; mode<4; ++mode) {
        setup(&s, &d, &cfg);
        d.regs[0x40/4] = 0xff;
        if (mode == 1 || mode == 2)
            for (c=0;c<4;++c) d.regs[(0x8040+c*0x4000)/4] = 0xff;
        d.broadcast = mode == 1;
        d.stuck = mode == 3;
        memset(&sample,0xa5,sizeof(sample)); other=sample;
        int result=gp_begin(&s,7,&cfg,&sample);
        if (mode < 2) {
            assert(result == 0 && s.active);
            assert(d.regs[0x40/4] == 0);
            assert(gp_end(&s,7,sample.session) == 0);
            assert(d.regs[0x40/4] == 0); /* never replay reset */
        } else {
            assert(result == PSP2_GPUPROF_UNSUPPORTED && !s.active && !s.generation);
            assert(!memcmp(&sample,&other,sizeof(sample)));
            assert(s.begin_failure_offset == (mode == 2 ? 0x8040u : 0x40u));
            assert(s.begin_failure_value == 0xff);
        }
        assert(d.regs[0x78/4] == 0x100 && !d.held);
        for (c=0;c<4;++c) for(p=0;p<4;++p)
            assert(d.regs[(0x8090+c*0x4000+p*4)/4] == 0x12340000+c*4+p);
    }
    setup(&s, &d, &cfg);
    signal_cfg.size = sizeof(signal_cfg);
    signal_cfg.abi = PSP2_GPUPROF_ABI;
    signal_cfg.group_count = 2;
    signal_cfg.groups[0] = 41;
    signal_cfg.groups[1] = 46;
    for (c = 0; c < 4; ++c) d.regs[(0x8070+c*0x4000)/4] = 100+c;
    assert(gp_signals(&s, &signal_cfg, &signals) == 0);
    assert(signals.size == sizeof(signals) && signals.group_count == 2);
    assert(!s.active && !d.held && d.writes == 32);
    for (c = 0; c < 4; ++c) {
        assert(signals.values[0][c] == 100+c && signals.values[1][c] == 100+c);
        assert(d.regs[(0x8078+c*0x4000)/4] == 0);
        for (p = 0; p < 4; ++p)
            assert(d.regs[(0x8090+c*0x4000+p*4)/4] == 0x12340000+c*4+p);
    }
    assert(signals.groups[0] == 41 && signals.groups[1] == 46);
    assert(!signals.values[2][0] && !signals.timer_after[7] && !signals.reserved[3]);
    writes = d.writes;
    signal_cfg.groups[0] = 67;
    assert(gp_signals(&s, &signal_cfg, &signals) == PSP2_GPUPROF_INVALID);
    signal_cfg.groups[0] = 41;
    signal_cfg.groups[7] = 1;
    assert(gp_signals(&s, &signal_cfg, &signals) == PSP2_GPUPROF_INVALID);
    signal_cfg.groups[7] = 0;
    d.regs[0x14078/4] = 1;
    assert(gp_signals(&s, &signal_cfg, &signals) == PSP2_GPUPROF_BUSY);
    assert(d.writes == writes && !d.held);
    d.regs[0x14078/4] = 0;
    assert(gp_begin(&s, 7, &cfg, &sample) == 0);
    writes = d.writes;
    assert(gp_signals(&s, &signal_cfg, &signals) == PSP2_GPUPROF_BUSY);
    assert(d.writes == writes);
    assert(gp_end(&s, 7, sample.session) == 0);
    d.failure = PSP2_GPUPROF_OFFLINE;
    assert(gp_signals(&s, &signal_cfg, &signals) == PSP2_GPUPROF_OFFLINE);
    puts("PASS: session ABI, validation, ownership, snapshots, restore, invalidation, lock failures");
    return 0;
}
