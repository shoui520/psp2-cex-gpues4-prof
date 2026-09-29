#include <psp2_gpuprof_draw.h>
#include <psp2_gpuprof_attribution.h>
#include <psp2/kernel/threadmgr.h>
#include <vita2d.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

/* Experimental live sampling, not a draw-cost benchmark. Kernel ABI unchanged.
 * Worker owns records until joined; no file I/O in capture. Atomic stop is the
 * only shared mutable field. Samples remain GPU-wide, including other apps.
 */
#define CAPACITY 4096u
typedef struct Record {
    uint64_t before, after;
    int rc;
    Psp2GpuProfSignals signals;
} Record;
static Record records[CAPACITY];
static unsigned used;
static atomic_uint stop;
static Psp2GpuProfSceneLabel scenes[8];
static Psp2GpuProfDrawLabel labels[129];
static unsigned hits[8][129];

static int sampler(SceSize argc, void *argv)
{
    (void)argc; (void)argv;
    Psp2GpuProfSignalConfig cfg = psp2GpuProfFragmentDrawConfig();
    while (used < CAPACITY && !atomic_load(&stop)) {
        Record *r = &records[used++];
        r->before = sceKernelGetSystemTimeWide();
        r->rc = psp2GpuProfReadSignals(&cfg, &r->signals);
        r->after = sceKernelGetSystemTimeWide();
        /* Bound CPU/driver contention. This delay is not the sampling period;
         * use recorded timestamps to see the actual spacing and call cost. */
        sceKernelDelayThread(100);
    }
    return 0;
}

static void render(unsigned phase, Psp2GpuProfSceneLabel *scene)
{
    if (scene) scene->cpu_begin_us = sceKernelGetSystemTimeWide();
    vita2d_start_drawing();
    vita2d_clear_screen();
    for (unsigned slot=0; slot<128; ++slot) {
        unsigned draw = phase == 3 ? 127-slot : slot;
        int large = phase >= 2 && draw >= 64;
        vita2d_draw_rectangle((float)(draw%8)*4, (float)(draw%4)*4,
            large ? 900 : 16, large ? 500 : 16,
            0x80000000u | ((draw*7919u)&0xffffffu));
    }
    vita2d_end_drawing();
    vita2d_wait_rendering_done();
    if (scene) scene->cpu_end_us = sceKernelGetSystemTimeWide();
    vita2d_swap_buffers();
}

static void label_draws(unsigned phase)
{
    labels[0] = (Psp2GpuProfDrawLabel){0, 0, 1, 2,
        "clear-candidate", "setup", "vita2d-clear-vs", "vita2d-clear-fs"};
    for (unsigned slot=0; slot<128; ++slot) {
        unsigned object = phase==3 ? 127-slot : slot;
        int large = phase>=2 && object>=64;
        labels[slot+1] = (Psp2GpuProfDrawLabel){object+1, large ? 2 : 1, 3, 4,
            large ? "large-rectangle" : "small-rectangle",
            large ? "large-rectangles" : "small-rectangles",
            "vita2d-color-vs", "vita2d-color-fs"};
    }
}

static void report_attribution(FILE *f, unsigned phase)
{
    unsigned statuses[8] = {0};
    memset(hits, 0, sizeof(hits));
    for (unsigned i=0; i<used; ++i) {
        const Record *r = &records[i];
        if (r->rc || r->signals.group_count!=1 || r->signals.groups[0]!=17) continue;
        for (unsigned c=0; c<4; ++c) {
            Psp2GpuProfAttribution a;
            int rc = psp2GpuProfAttributeFragment(scenes, phase ? 8 : 0,
                r->before, r->after, r->signals.values[0][c], &a);
            if (rc) { fprintf(f,"metadata-error,%u,%d\n",phase,rc); return; }
            ++statuses[a.status];
            if (a.status==PSP2_GPUPROF_ATTR_CANDIDATE)
                ++hits[a.scene_index][a.observation.candidate_draw];
        }
    }
    for (unsigned s=0; s<8; ++s) fprintf(f,"status,%u,%u,%u\n",phase,s,statuses[s]);
    if (!phase) return;
    for (unsigned s=0; s<8; ++s) {
        const Psp2GpuProfSceneLabel *scene = &scenes[s];
        fprintf(f,"scene,%llu,%llu,%llu,%llu,%llu\n",
            (unsigned long long)scene->scene_id, (unsigned long long)scene->frame_id,
            (unsigned long long)scene->context_id, (unsigned long long)scene->cpu_begin_us,
            (unsigned long long)scene->cpu_end_us);
        for (unsigned d=0; d<129; ++d) {
            const Psp2GpuProfDrawLabel *label = &labels[d];
            fprintf(f,"candidate,%llu,%u,%llu,%s,%s,%s,%s,%u\n",
                (unsigned long long)scene->scene_id,d,(unsigned long long)label->draw_id,
                label->draw_name,label->pass_name,label->vertex_shader_name,
                label->fragment_shader_name,hits[s][d]);
        }
    }
}

int main(void)
{
    int rc, initialized = 0;
    Psp2GpuProfInfo info;
    FILE *f = fopen("ux0:data/gpuprof-draws.csv", "a");
    if (!f) return 1;
    FILE *attribution = fopen("ux0:data/gpuprof-attribution.csv", "a");
    if (!attribution) { fclose(f); return 1; }
    fprintf(attribution,"probe,named-draws-v1,candidate-core-sample-counts-not-time\n");
    fprintf(f, "probe,draw-activity-v1\n");
    rc = psp2GpuProfGetInfo(&info);
    if (rc) goto done;
    fprintf(f, "backend,%08x,%d,%u\n", info.driver_fingerprint, info.status, info.capabilities);
    if (info.status || info.abi != PSP2_GPUPROF_ABI ||
        !(info.capabilities & PSP2_GPUPROF_CAP_SIGNALS)) { rc = -1; goto done; }
    if (vita2d_init() != 1) { rc = -1; goto done; }
    initialized = 1;
    vita2d_set_clear_color(0xff302010);
    render(1, NULL);
    for (unsigned phase=0; phase<4; ++phase) {
        used = 0;
        atomic_store(&stop, 0);
        label_draws(phase);
        for (unsigned s=0; s<8; ++s)
            scenes[s] = (Psp2GpuProfSceneLabel){phase*8+s+1,phase*8+s+1,1,
                0,0,labels,129,PSP2_GPUPROF_SCENE_ORDERED_ISOLATED};
        fprintf(f, "phase,%u,0-idle-1-small-2-large-last-3-large-first\n", phase);
        fflush(f);
        SceUID thread = sceKernelCreateThread("gpuprof-draw-sampler", sampler,
                                             0x10000100, 0x10000, 0, 0, NULL);
        if (thread < 0) { rc = thread; break; }
        rc = sceKernelStartThread(thread, 0, NULL);
        if (rc < 0) { sceKernelDeleteThread(thread); break; }
        uint64_t begin = sceKernelGetSystemTimeWide();
        if (!phase) sceKernelDelayThread(200000);
        else for (unsigned frame=0; frame<8; ++frame) render(phase, &scenes[frame]);
        uint64_t end = sceKernelGetSystemTimeWide();
        atomic_store(&stop, 1);
        rc = sceKernelWaitThreadEnd(thread, NULL, NULL);
        if (rc < 0) {
            /* Do not access worker-owned storage or tear down GXM on failed join. */
            fprintf(f, "join-error,%08x\n", (unsigned)rc);
            fclose(f);
            fclose(attribution);
            return 1;
        }
        sceKernelDeleteThread(thread);
        report_attribution(attribution, phase);
        fprintf(f, "window,%u,%llu,%llu,%u,%u\n", phase,
                (unsigned long long)begin, (unsigned long long)end, used, used==CAPACITY);
        for (unsigned i=0; i<used; ++i) {
            const Record *r = &records[i];
            fprintf(f, "call,%u,%u,%llu,%llu,%08x\n", phase, i,
                (unsigned long long)r->before, (unsigned long long)r->after, (unsigned)r->rc);
            if (r->rc) continue;
            if (r->signals.group_count != 1 || r->signals.groups[0] != 17) { rc=-1; break; }
            for (unsigned c=0; c<4; ++c) {
                Psp2GpuProfDrawObservation d = psp2GpuProfDecodePdsDraw(r->signals.values[0][c]);
                fprintf(f, "draw,%u,%u,%u,%u,%u,%08x,%u,%u,%d\n", phase, i, c,
                    r->signals.timer_before[0], r->signals.timer_after[0],
                    d.raw, d.batch, d.active, (int)d.candidate_draw);
            }
        }
        if (rc) break;
    }
done:
    if (initialized) { vita2d_wait_rendering_done(); vita2d_fini(); }
    fprintf(f, "result,%08x\n", (unsigned)rc);
    fprintf(attribution, "result,%08x\n", (unsigned)rc);
    if (ferror(attribution)) rc=-1;
    if (fclose(attribution)) rc=-1;
    if (ferror(f)) rc=-1;
    if (fclose(f)) rc=-1;
    return rc ? 1 : 0;
}
