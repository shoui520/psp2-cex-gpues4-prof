#include "psp2_gpuprof_attribution.h"
#include <string.h>

static int builder_status(const Psp2GpuProfLabelBuilder *b)
{
    if (!b || !b->storage || !b->capacity || b->capacity > 8191 || b->count > b->capacity)
        return PSP2_GPUPROF_INVALID;
    return b->error;
}

int psp2GpuProfLabelsInit(Psp2GpuProfLabelBuilder *b,
                        Psp2GpuProfDrawLabel *storage, uint32_t capacity)
{
    if (!b) return PSP2_GPUPROF_INVALID;
    memset(b, 0, sizeof(*b));
    if (!storage || !capacity || capacity > 8191) {
        b->error = PSP2_GPUPROF_INVALID;
        return b->error;
    }
    b->storage = storage;
    b->capacity = capacity;
    return 0;
}

int psp2GpuProfLabelPass(Psp2GpuProfLabelBuilder *b, uint64_t id, const char *name)
{
    int rc = builder_status(b);
    if (rc) return rc;
    if (!name) return b->error = PSP2_GPUPROF_INVALID;
    b->current.pass_id = id;
    b->current.pass_name = name;
    return 0;
}

int psp2GpuProfLabelShaders(Psp2GpuProfLabelBuilder *b,
    uint64_t vertex_id, const char *vertex_name,
    uint64_t fragment_id, const char *fragment_name)
{
    int rc = builder_status(b);
    if (rc) return rc;
    if (!vertex_name || !fragment_name) return b->error = PSP2_GPUPROF_INVALID;
    b->current.vertex_shader_id = vertex_id;
    b->current.vertex_shader_name = vertex_name;
    b->current.fragment_shader_id = fragment_id;
    b->current.fragment_shader_name = fragment_name;
    return 0;
}

int psp2GpuProfLabelDraw(Psp2GpuProfLabelBuilder *b, uint64_t id, const char *name)
{
    int rc = builder_status(b);
    if (rc) return rc;
    if (!name || !b->current.pass_name || !b->current.vertex_shader_name ||
        !b->current.fragment_shader_name) return b->error = PSP2_GPUPROF_INVALID;
    if (b->count == b->capacity) return b->error = PSP2_GPUPROF_BUSY;
    b->current.draw_id = id;
    b->current.draw_name = name;
    b->storage[b->count++] = b->current;
    return 0;
}

int psp2GpuProfAttributeFragment(const Psp2GpuProfSceneLabel *scenes,
    uint32_t count, uint64_t before, uint64_t after, uint32_t raw,
    Psp2GpuProfAttribution *out)
{
    uint32_t intersections = 0, selected = UINT32_MAX;
    if (!out) return PSP2_GPUPROF_INVALID;
    memset(out, 0, sizeof(*out));
    out->scene_index = UINT32_MAX;
    out->status = PSP2_GPUPROF_ATTR_NO_SCENE;
    if ((!scenes && count) || after < before) return PSP2_GPUPROF_INVALID;
    for (uint32_t i=0; i<count; ++i) {
        const Psp2GpuProfSceneLabel *s = &scenes[i];
        if (s->cpu_end_us <= s->cpu_begin_us || !s->draws || !s->draw_count ||
            s->draw_count > 8191 || (s->flags & ~PSP2_GPUPROF_SCENE_ORDERED_ISOLATED))
            return PSP2_GPUPROF_INVALID;
        for (uint32_t j=0; j<i; ++j)
            if (scenes[j].scene_id == s->scene_id) return PSP2_GPUPROF_INVALID;
        if (before < s->cpu_end_us && after >= s->cpu_begin_us) {
            ++intersections;
            selected = i;
        }
    }
    out->observation = psp2GpuProfDecodePdsDraw(raw);
    if (!out->observation.active) out->status = PSP2_GPUPROF_ATTR_INACTIVE;
    else if (!out->observation.batch) out->status = PSP2_GPUPROF_ATTR_UNMAPPED_BATCH;
    else if (intersections > 1) out->status = PSP2_GPUPROF_ATTR_AMBIGUOUS;
    else if (intersections == 1) {
        const Psp2GpuProfSceneLabel *s = &scenes[selected];
        if (before < s->cpu_begin_us || after >= s->cpu_end_us)
            out->status = PSP2_GPUPROF_ATTR_BOUNDARY;
        else if (!(s->flags & PSP2_GPUPROF_SCENE_ORDERED_ISOLATED))
            out->status = PSP2_GPUPROF_ATTR_UNVERIFIED_ORDER;
        else if ((uint32_t)out->observation.candidate_draw >= s->draw_count)
            out->status = PSP2_GPUPROF_ATTR_OUT_OF_RANGE;
        else {
            out->status = PSP2_GPUPROF_ATTR_CANDIDATE;
            out->scene_index = selected;
            out->scene = s;
            out->draw = &s->draws[out->observation.candidate_draw];
        }
    }
    return 0;
}
