#include "diagnostic.h"
#include <string.h>

_Static_assert(sizeof(Psp2GpuProfDiagnosticConfig)==32, "diagnostic config ABI");
_Static_assert(sizeof(Psp2GpuProfDiagnosticCore)==80, "diagnostic core ABI");
_Static_assert(sizeof(Psp2GpuProfDiagnostic)==352, "diagnostic output ABI");
int gp_diagnostic_config_valid(const Psp2GpuProfDiagnosticConfig *c)
{
    if (!c || c->size!=sizeof(*c) || c->abi!=PSP2_GPUPROF_ABI)
        return 0;
    for(unsigned i=0;i<4;++i) if(c->reserved[i]) return 0;
    return 1;
}

static uint32_t rd(struct gp_session *s, uint32_t a)
{ return s->io.read(s->io.ctx, a); }
static void wr(struct gp_session *s, uint32_t a, uint32_t v)
{ s->io.write(s->io.ctx, a, v); }
static uint32_t signal(struct gp_session *s, uint32_t base, unsigned group)
{
    wr(s, base+0x90, group | (group<<16) | 0x08000000u);
    wr(s, base+0x94, group | (group<<16) | 0x18001000u);
    return rd(s, base+0x70);
}

int gp_diagnostic_read(struct gp_session *s, gp_work_reader reader, void *ctx,
                       unsigned group, unsigned tag_group,
                       Psp2GpuProfDiagnostic *out)
{
    uint32_t saved[4][3];
    Psp2GpuProfDiagnostic result = {0};
    int rc;
    /* Only the audited diagnostic groups; no arbitrary register access. */
    if (!reader || !out || (tag_group != 70 && tag_group != 102))
        return PSP2_GPUPROF_INVALID;
    switch (group) {
    case 2: case 4: case 43: case 55:
    case 71: case 72: case 75: case 76: case 77: case 78:
    case 103: case 104: case 107: case 108: case 109: case 110: break;
    default: return PSP2_GPUPROF_INVALID;
    }
    if (s->active) return PSP2_GPUPROF_BUSY;
    rc = s->io.acquire(s->io.ctx);
    if (rc) return rc;
    if ((rd(s,0x4000)&3u)!=3u) { rc=PSP2_GPUPROF_UNSUPPORTED; goto done; }
    if (rd(s,0x78)&1u) { rc=PSP2_GPUPROF_BUSY; goto done; }
    for (unsigned c=0;c<4;++c) {
        uint32_t b=0x8000+c*0x4000;
        saved[c][0]=rd(s,b+0x78);
        if (saved[c][0]&1u) { rc=PSP2_GPUPROF_BUSY; goto done; }
        saved[c][1]=rd(s,b+0x90);
        saved[c][2]=rd(s,b+0x94);
    }
    result.group=group;
    result.size=sizeof(result);
    result.abi=PSP2_GPUPROF_ABI;
    result.tag_group=tag_group;
    for (unsigned c=0;c<4;++c) {
        uint32_t b=0x8000+c*0x4000;
        Psp2GpuProfDiagnosticCore *v=&result.cores[c];
        wr(s,b+0x78,saved[c][0]|1u);
        v->timer_before=rd(s,0x144);
        rc=reader(ctx,&v->before);
        if (!rc) {
            v->pds_before=signal(s,b,17);
            v->tag_before=signal(s,b,tag_group);
            v->value=signal(s,b,group);
            v->tag_after=signal(s,b,tag_group);
            v->pds_after=signal(s,b,17);
            rc=reader(ctx,&v->after);
        }
        v->timer_after=rd(s,0x144);
        wr(s,b+0x90,saved[c][1]);
        wr(s,b+0x94,saved[c][2]);
        wr(s,b+0x78,saved[c][0]);
        if (rc) goto done;
        v->flags=GP_DIAG_UNVERIFIED;
        if (memcmp(&v->before,&v->after,sizeof(v->before)))
            v->flags|=GP_DIAG_SCHEDULER_CHANGED;
        /* Include tile changes, not only batch changes, in rejection flags. */
        if (v->pds_before!=v->pds_after) v->flags|=GP_DIAG_PDS_CHANGED;
        if (v->tag_before!=v->tag_after) v->flags|=GP_DIAG_TAG_CHANGED;
        uint32_t words[]={v->pds_before,v->pds_after,v->tag_before,v->tag_after};
        for (unsigned i=0;i<4;++i)
            if (!(words[i]&0x20000000u) || !(words[i]&0x1fff0000u))
                v->flags|=GP_DIAG_INACTIVE;
        if ((v->pds_before&0x1fffffffu)!=(v->tag_before&0x1fffffffu) ||
            (v->pds_after&0x1fffffffu)!=(v->tag_after&0x1fffffffu))
            v->flags|=GP_DIAG_STAGE_DISAGREEMENT;
    }
    *out=result;
done:
    s->io.release(s->io.ctx);
    return rc;
}
