#ifndef PSP2_GPUPROF_ATTRIBUTION_H
#define PSP2_GPUPROF_ATTRIBUTION_H
#include "psp2_gpuprof_draw.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Application metadata, NOT kernel-derived ownership. Strings and arrays are
 * borrowed and must stay immutable until analysis finishes. Shader IDs should
 * identify a program generation, not a recyclable pointer alone.
 */
typedef struct Psp2GpuProfDrawLabel {
    uint64_t draw_id, pass_id, vertex_shader_id, fragment_shader_id;
    const char *draw_name, *pass_name, *vertex_shader_name, *fragment_shader_name;
} Psp2GpuProfDrawLabel;

/* One builder per submitted scene. Call RecordDraw only after a successful
 * supported draw; binding/pass changes affect subsequent records only. Include
 * draws issued by helper libraries. Overflow is sticky: discard that scene's
 * mapping instead of reusing ordinals after missing metadata. Not thread-safe.
 * Names are borrowed, not copied. Do not alias storage and builder.
 */
typedef struct Psp2GpuProfLabelBuilder {
    Psp2GpuProfDrawLabel *storage;
    uint32_t capacity, count;
    int32_t error;
    Psp2GpuProfDrawLabel current;
} Psp2GpuProfLabelBuilder;
int psp2GpuProfLabelsInit(Psp2GpuProfLabelBuilder *builder,
                        Psp2GpuProfDrawLabel *storage, uint32_t capacity);
int psp2GpuProfLabelPass(Psp2GpuProfLabelBuilder *builder,
                       uint64_t id, const char *name);
int psp2GpuProfLabelShaders(Psp2GpuProfLabelBuilder *builder,
    uint64_t vertex_id, const char *vertex_name,
    uint64_t fragment_id, const char *fragment_name);
int psp2GpuProfLabelDraw(Psp2GpuProfLabelBuilder *builder,
                       uint64_t id, const char *name);

/* Opt-in diagnostic contract: previous work has been drained, only this
 * scene/context is submitted in this window, and all draws (including clears)
 * are registered in hardware order. No deferred/precomputed/instanced draws,
 * mid-scene flushes or batch wrap unless independently validated by the app.
 * This flag cannot detect other processes' GPU work. Matches remain candidates.
 */
#define PSP2_GPUPROF_SCENE_ORDERED_ISOLATED 1u
typedef struct Psp2GpuProfSceneLabel {
    uint64_t scene_id, frame_id, context_id;
    uint64_t cpu_begin_us, cpu_end_us;
    const Psp2GpuProfDrawLabel *draws;
    uint32_t draw_count, flags;
} Psp2GpuProfSceneLabel;

enum Psp2GpuProfAttributionStatus {
    PSP2_GPUPROF_ATTR_CANDIDATE = 0,
    PSP2_GPUPROF_ATTR_INACTIVE,
    PSP2_GPUPROF_ATTR_UNMAPPED_BATCH,
    PSP2_GPUPROF_ATTR_NO_SCENE,
    PSP2_GPUPROF_ATTR_BOUNDARY,
    PSP2_GPUPROF_ATTR_AMBIGUOUS,
    PSP2_GPUPROF_ATTR_UNVERIFIED_ORDER,
    PSP2_GPUPROF_ATTR_OUT_OF_RANGE
};
typedef struct Psp2GpuProfAttribution {
    uint32_t status, scene_index;
    Psp2GpuProfDrawObservation observation;
    const Psp2GpuProfSceneLabel *scene;
    const Psp2GpuProfDrawLabel *draw;
} Psp2GpuProfAttribution;

/* Resolve ONE group17/core raw word, using CPU times surrounding ReadSignals.
 * Windows are [begin,end); a call ending on a boundary is conservatively
 * rejected. Overlap with ANY other scene rejects the sample, even when another
 * scene fully contains it. No GPU durations or utilization are computed.
 * Returns INVALID on malformed metadata; otherwise status describes resolution.
 * No allocation or syscalls; caller serializes access. Output must not alias
 * metadata. Scene IDs must be unique across the supplied capture.
 */
int psp2GpuProfAttributeFragment(const Psp2GpuProfSceneLabel *scenes,
    uint32_t scene_count, uint64_t cpu_before_us, uint64_t cpu_after_us,
    uint32_t raw, Psp2GpuProfAttribution *out);
#ifdef __cplusplus
}
#endif
#endif
