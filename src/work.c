#include "work.h"
#include <string.h>

int gp_work(struct gp_session *s, gp_work_reader reader, void *ctx,
            Psp2GpuProfWork *out)
{
    uint32_t saved[4][3];
    Psp2GpuProfWork result;
    int rc;
    if (!reader || !out) return PSP2_GPUPROF_INVALID;
    if (s->active) return PSP2_GPUPROF_BUSY;
    rc = s->io.acquire(s->io.ctx);
    if (rc) return rc;
#define RD(o) s->io.read(s->io.ctx, (o))
#define WR(o,v) s->io.write(s->io.ctx, (o), (v))
    if ((RD(0x4000) & 3u) != 3u) { rc=PSP2_GPUPROF_UNSUPPORTED; goto done; }
    if (RD(0x78) & 1u) { rc=PSP2_GPUPROF_BUSY; goto done; }
    for (unsigned c=0; c<4; ++c) {
        uint32_t base=0x8000+c*0x4000;
        saved[c][0]=RD(base+0x78);
        if (saved[c][0]&1u) { rc=PSP2_GPUPROF_BUSY; goto done; }
        saved[c][1]=RD(base+0x90);
        saved[c][2]=RD(base+0x94);
    }
    memset(&result, 0, sizeof(result));
    result.size=sizeof(result);
    result.abi=PSP2_GPUPROF_ABI;
    for (unsigned c=0; c<4; ++c) {
        uint32_t base=0x8000+c*0x4000;
        Psp2GpuProfWorkCore *v=&result.cores[c];
        WR(base+0x78, saved[c][0]|1u);
        WR(base+0x90, 0x08110011u);
        WR(base+0x94, 0x18111011u);
        v->timer_before=RD(0x144);
        rc=reader(ctx, &v->before);
        if (!rc) {
            v->fragment_signal=RD(base+0x70);
            rc=reader(ctx, &v->after);
        }
        v->timer_after=RD(0x144);
        /* Restore even if either scheduler read fails. */
        WR(base+0x90, saved[c][1]);
        WR(base+0x94, saved[c][2]);
        WR(base+0x78, saved[c][0]);
        if (rc) goto done;
        v->flags=PSP2_GPUPROF_WORK_UNVERIFIED;
        if (memcmp(&v->before, &v->after, sizeof(v->before)))
            v->flags|=PSP2_GPUPROF_WORK_CHANGED;
        if (!(v->fragment_signal & (1u<<29)))
            v->flags|=PSP2_GPUPROF_WORK_INACTIVE;
    }
    *out=result;
done:
    s->io.release(s->io.ctx);
    return rc;
#undef RD
#undef WR
}
