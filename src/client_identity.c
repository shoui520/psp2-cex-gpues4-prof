#include "psp2_gpuprof_identity.h"
#include <string.h>

void psp2GpuProfIdentityInvalidate(Psp2GpuProfIdentityTracker *t)
{
    if (t) { t->valid = 0; t->open = 0; }
}

static int invalidate(Psp2GpuProfIdentityTracker *t)
{
    psp2GpuProfIdentityInvalidate(t);
    return PSP2_GPUPROF_STALE;
}

int psp2GpuProfIdentitySeed(Psp2GpuProfIdentityTracker *t, uint64_t epoch,
                          uint32_t pid, uint32_t frame, uint32_t scene)
{
    if (!t) return PSP2_GPUPROF_INVALID;
    memset(t, 0, sizeof(*t));
    if (!epoch || !pid || frame > 0xffffffu) return PSP2_GPUPROF_INVALID;
    t->next.epoch = epoch; t->next.pid = pid;
    t->next.frame = frame; t->next.scene = scene; t->valid = 1;
    return 0;
}

int psp2GpuProfIdentityBegin(Psp2GpuProfIdentityTracker *t, uint64_t context, int result)
{
    if (!t) return PSP2_GPUPROF_INVALID;
    if (!t->valid || t->open || !context || result) return invalidate(t);
    t->next.context = context; t->open = 1;
    return 0;
}

int psp2GpuProfIdentityEnd(Psp2GpuProfIdentityTracker *t, uint64_t context, int result,
                         Psp2GpuProfIdentity *out)
{
    if (out) memset(out, 0, sizeof(*out));
    if (!t) return PSP2_GPUPROF_INVALID;
    if (!out || !t->valid || !t->open || t->next.context != context || result ||
        t->next.scene == UINT32_MAX) return invalidate(t);
    *out = t->next;
    ++t->next.scene; t->open = 0;
    return 0;
}

int psp2GpuProfIdentityDisplay(Psp2GpuProfIdentityTracker *t, int result)
{
    if (!t) return PSP2_GPUPROF_INVALID;
    if (!t->valid || t->open || result || t->next.frame == 0xffffffu)
        return invalidate(t);
    ++t->next.frame; t->next.scene = 0;
    return 0;
}

int psp2GpuProfMatchWork(const Psp2GpuProfIdentifiedScene *scenes, uint32_t count,
                       uint32_t pid, uint64_t before, uint64_t after,
                       const Psp2GpuProfWorkCore *work, Psp2GpuProfIdentityMatch *out)
{
    const Psp2GpuProfIdentifiedScene *selected = NULL;
    unsigned matches = 0;
    if (!out) return PSP2_GPUPROF_INVALID;
    memset(out, 0, sizeof(*out));
    out->status = PSP2_GPUPROF_MATCH_MISSING;
    if (!work || (!scenes && count) || !pid || before > after ||
        (work->flags & ~(PSP2_GPUPROF_WORK_UNVERIFIED | PSP2_GPUPROF_WORK_CHANGED |
                         PSP2_GPUPROF_WORK_INACTIVE))) return PSP2_GPUPROF_INVALID;
    out->source_flags = work->flags;
    for (uint32_t i=0; i<count; ++i) {
        const Psp2GpuProfIdentifiedScene *s = &scenes[i];
        if (!s->identity.epoch || !s->identity.context || !s->identity.pid ||
            s->identity.frame > 0xffffff || s->begin_us >= s->complete_us ||
            !s->draws || !s->draw_count || s->draw_count > 8191 ||
            s->complete_order > 1) return PSP2_GPUPROF_INVALID;
        if (s->identity.pid == work->before.render_pid &&
            s->identity.frame == work->before.render_frame &&
            s->identity.scene == work->before.render_scene) {
            selected = s; ++matches;
        }
    }
    Psp2GpuProfDrawObservation draw = psp2GpuProfDecodePdsDraw(work->fragment_signal);
    if ((work->flags & PSP2_GPUPROF_WORK_CHANGED) ||
        memcmp(&work->before, &work->after, sizeof(work->before)))
        out->status = PSP2_GPUPROF_MATCH_CHANGED;
    else if (!draw.active || (work->flags & PSP2_GPUPROF_WORK_INACTIVE))
        out->status = PSP2_GPUPROF_MATCH_INACTIVE;
    else if (work->before.render_pid != pid) out->status = PSP2_GPUPROF_MATCH_FOREIGN;
    else if (matches > 1) out->status = PSP2_GPUPROF_MATCH_COLLISION;
    else if (matches == 1) {
        if (before < selected->begin_us || after >= selected->complete_us)
            out->status = PSP2_GPUPROF_MATCH_BOUNDARY;
        else if (!selected->complete_order) out->status = PSP2_GPUPROF_MATCH_ORDER;
        else if (draw.candidate_draw < 0 || (uint32_t)draw.candidate_draw >= selected->draw_count)
            out->status = PSP2_GPUPROF_MATCH_BATCH;
        else {
            out->status = PSP2_GPUPROF_MATCH_CANDIDATE;
            out->scene = selected; out->draw = &selected->draws[draw.candidate_draw];
        }
    }
    return 0;
}
