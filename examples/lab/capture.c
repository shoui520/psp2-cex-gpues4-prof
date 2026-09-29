#include "capture.h"
#include "notification_poll.h"
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <psp2_gpuprof_identity.h>
#include <psp2_gpuprof_presets.h>
#include <psp2_gpuprof_work.h>
#include <psp2_gpuprof_diagnostic.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#define NS 256u
#define ND 65536u
#ifdef GPUPROF_LAB_TIMING_VALIDATION
#define NR 16384u
#elif defined(GPUPROF_LAB_SIGNAL_VALIDATION)
#define NR 8192u
#else
#define NR 2048u
#endif
_Static_assert(NS == 256u, "Notification cache layout must match scene capacity");
const char *lab_pass = "startup", *lab_draw = "unknown";
unsigned lab_frame;
int lab_isolated;
int lab_sentinel;
int lab_benchmark_phase = -1;
static struct {
  const void *p;
  const char *name;
} programs[64];
static unsigned nprograms, vs, fs;
static struct {
  unsigned frame;
  const char *name;
  uint64_t begin, end;
  int begin_rc, end_rc;
  int identity_rc;
  Psp2GpuProfIdentity identity;
} scenes[NS];
static Psp2GpuProfIdentityTracker identity_tracker;
static SceGxmContext *identity_context;
static struct {
  unsigned frame;
  int rc;
} displays[NS];
static unsigned display_count;
void lab_identity_init(void) {
  /* This standalone app starts before any GXM calls. Retail 3.65 globals
   * start zero; this seed must not be reused by an injected profiler. */
  psp2GpuProfIdentitySeed(&identity_tracker, 1,
                          (uint32_t)sceKernelGetProcessId(), 0, 0);
}
static uint64_t context_id(SceGxmContext *c) {
  if (!identity_context)
    identity_context = c;
  if (c != identity_context) {
    psp2GpuProfIdentityInvalidate(&identity_tracker);
    return 0;
  }
  return 1; /* This example uses one context; never export its address. */
}
static struct {
  unsigned scene, ordinal, vs, fs, count;
  const char *name;
  int rc;
} draws[ND];
static struct Record {
  uint64_t begin, end;
  uint64_t read_begin, read_end;
  int rc, gpu0, gpu1, xbar;
  uint32_t ta[8], fragment[8], ta_before[8], fragment_before[8];
  union {
    Psp2GpuProfWork work;
    Psp2GpuProfDiagnostic diagnostic;
    Psp2GpuProfSignals signals;
    Psp2GpuProfSample counters;
  } data;
} records[NR];
static unsigned ns, nd, nr, ordinal, mode, level;
static int active, error, dropped, heavy, particles;
static atomic_uint stop;
static atomic_uint published_notifications;
static SceUID thread = -1;
static volatile unsigned *notifications;
static Psp2GpuProfSample baseline, final_sample;
static int final_rc, end_rc;
static Psp2GpuProfInfo backend;
static SceUID capture_pid;
static int capture_isolated;
static int capture_sentinel;
static int capture_benchmark_phase;
#ifdef GPUPROF_LAB_TIMING_VALIDATION
static unsigned timing_delay(void) {
  static const unsigned delays[] = {2000,500,100,100,500,2000};
  return capture_benchmark_phase >= 0 && capture_benchmark_phase < 6
             ? delays[capture_benchmark_phase] : 2000;
}
#endif
static char path[96];
static unsigned token(const void *p) {
  for (unsigned i = 0; i < nprograms; ++i)
    if (programs[i].p == p)
      return i + 1;
  if (nprograms == 64) {
    dropped = 1;
    return 0;
  }
  programs[nprograms].p = p;
  programs[nprograms].name = "library-shader";
  return ++nprograms;
}
void lab_program(const void *p, const char *name) {
  unsigned n = token(p);
  if (n)
    programs[n - 1].name = name;
}
void __real_sceGxmSetVertexProgram(SceGxmContext *,
                                   const SceGxmVertexProgram *);
void __wrap_sceGxmSetVertexProgram(SceGxmContext *c,
                                   const SceGxmVertexProgram *p) {
  vs = token(p);
  __real_sceGxmSetVertexProgram(c, p);
}
void __real_sceGxmSetFragmentProgram(SceGxmContext *,
                                     const SceGxmFragmentProgram *);
void __wrap_sceGxmSetFragmentProgram(SceGxmContext *c,
                                     const SceGxmFragmentProgram *p) {
  fs = token(p);
  __real_sceGxmSetFragmentProgram(c, p);
}
int __real_sceGxmDraw(SceGxmContext *, SceGxmPrimitiveType, SceGxmIndexFormat,
                      const void *, unsigned);
int __wrap_sceGxmDraw(SceGxmContext *c, SceGxmPrimitiveType p,
                      SceGxmIndexFormat f, const void *i, unsigned n) {
  int rc = __real_sceGxmDraw(c, p, f, i, n);
  if (rc)
    error = rc;
  if (active) {
    if (nd < ND && ns && ns <= NS) {
      draws[nd].scene = ns - 1;
      draws[nd].ordinal = ordinal;
      draws[nd].vs = vs;
      draws[nd].fs = fs;
      draws[nd].count = n;
      draws[nd].name = lab_draw;
      draws[nd++].rc = rc;
    } else
      dropped = 1;
    ++ordinal;
  }
  return rc;
}
int __real_sceGxmBeginScene(SceGxmContext *, unsigned,
                            const SceGxmRenderTarget *,
                            const SceGxmValidRegion *, SceGxmSyncObject *,
                            SceGxmSyncObject *, const SceGxmColorSurface *,
                            const SceGxmDepthStencilSurface *);
int __wrap_sceGxmBeginScene(SceGxmContext *c, unsigned f,
                            const SceGxmRenderTarget *t,
                            const SceGxmValidRegion *v, SceGxmSyncObject *a,
                            SceGxmSyncObject *b, const SceGxmColorSurface *s,
                            const SceGxmDepthStencilSurface *d) {
  uint64_t now = sceKernelGetSystemTimeWide();
  int rc = __real_sceGxmBeginScene(c, f, t, v, a, b, s, d);
  psp2GpuProfIdentityBegin(&identity_tracker, context_id(c), rc);
  if (rc)
    error = rc;
  if (active) {
    if (ns < NS) {
      scenes[ns].frame = lab_frame;
      scenes[ns].name = lab_pass;
      scenes[ns].begin = now;
      scenes[ns].begin_rc = rc;
    } else
      dropped = 1;
    ++ns;
    ordinal = 0;
  }
  return rc;
}
int __real_sceGxmEndScene(SceGxmContext *, const SceGxmNotification *,
                          const SceGxmNotification *);
int __wrap_sceGxmEndScene(SceGxmContext *c, const SceGxmNotification *v,
                          const SceGxmNotification *f) {
  SceGxmNotification a, b;
  if (active && ns && ns <= NS && !v && !f) {
    a = (SceGxmNotification){notifications + ns - 1, ns};
    b = (SceGxmNotification){notifications + NS + ns - 1, ns};
    v = &a;
    f = &b;
    atomic_store_explicit(&published_notifications, ns, memory_order_release);
  }
  int rc = __real_sceGxmEndScene(c, v, f);
  Psp2GpuProfIdentity identity;
  int identity_rc =
      psp2GpuProfIdentityEnd(&identity_tracker, context_id(c), rc, &identity);
  if (rc)
    error = rc;
  if (active && ns && ns <= NS) {
    scenes[ns - 1].end = sceKernelGetSystemTimeWide();
    scenes[ns - 1].end_rc = rc;
    scenes[ns - 1].identity_rc = identity_rc;
    scenes[ns - 1].identity = identity;
  }
  return rc;
}
int __real_sceGxmDisplayQueueAddEntry(SceGxmSyncObject *, SceGxmSyncObject *,
                                      const void *);
int __wrap_sceGxmDisplayQueueAddEntry(SceGxmSyncObject *old,
                                      SceGxmSyncObject *next,
                                      const void *data) {
  int rc = __real_sceGxmDisplayQueueAddEntry(old, next, data);
  psp2GpuProfIdentityDisplay(&identity_tracker, rc);
  if (rc)
    error = rc;
  if (active) {
    if (display_count < NS) {
      displays[display_count].frame = lab_frame;
      displays[display_count++].rc = rc;
    } else
      dropped = 1;
  }
  return rc;
}
static void poll_notifications(LabNotificationCache *cache,
                               uint32_t *ta, uint32_t *fragment) {
  lab_poll_notifications(cache, notifications,
      atomic_load_explicit(&published_notifications, memory_order_acquire), ta, fragment);
}
static int sample(SceSize n, void *p) {
  (void)n;
  (void)p;
  Psp2GpuProfSignalConfig cfg = psp2GpuProfPipelineSignalConfig();
  LabNotificationCache cache = {0};
  uint32_t jitter = 0x19a76531u;
  while (nr < NR && !atomic_load(&stop)) {
    struct Record *r = &records[nr++];
    memset(r, 0, sizeof(*r));
    r->gpu0 = scePowerGetGpuClockFrequency();
    r->xbar = scePowerGetGpuXbarClockFrequency();
    r->begin = sceKernelGetSystemTimeWide();
    poll_notifications(&cache, r->ta_before, r->fragment_before);
    r->read_begin = sceKernelGetSystemTimeWide();
    if (mode == 0)
      r->rc = psp2GpuProfReadWork(&r->data.work);
    else if (mode == 1)
      r->rc = psp2GpuProfRead(&baseline.session, &r->data.counters);
    else if (mode == 3) {
#if defined(GPUPROF_LAB_SIGNAL_VALIDATION) || defined(GPUPROF_LAB_TIMING_DIAGNOSTIC)
      static const unsigned groups[]={4,43,77,78,109,110};
#else
      static const unsigned groups[]={2,4,43,55,71,72,75,76,77,78,103,104,107,108,109,110};
#endif
      unsigned ngroups=sizeof(groups)/sizeof(groups[0]);
      unsigned group=groups[(nr-1)%ngroups];
      Psp2GpuProfDiagnosticConfig dc={sizeof(dc),PSP2_GPUPROF_ABI,group,
          group>=103 ? 102u : group>=71 ? 70u : ((nr-1)/ngroups)%2 ? 102u : 70u,{0}};
      r->rc=psp2GpuProfReadDiagnostic(&dc,&r->data.diagnostic);
    } else
      r->rc = psp2GpuProfReadSignals(&cfg, &r->data.signals);
    r->read_end = sceKernelGetSystemTimeWide();
    poll_notifications(&cache, r->ta, r->fragment);
    r->end = sceKernelGetSystemTimeWide();
    r->gpu1 = scePowerGetGpuClockFrequency();
#ifdef GPUPROF_LAB_SIGNAL_VALIDATION
    jitter ^= jitter << 13;
    jitter ^= jitter >> 17;
    jitter ^= jitter << 5;
    sceKernelDelayThread(1500 + jitter % 1001);
#else
    (void)jitter;
#ifdef GPUPROF_LAB_TIMING_VALIDATION
    sceKernelDelayThread(timing_delay());
#else
    sceKernelDelayThread(2000);
#endif
#endif
  }
  return 0;
}
int lab_capture_start(unsigned m, unsigned l, int h, int p) {
  if (active || m > 3)
    return -1;
  Psp2GpuProfInfo info;
  int rc = psp2GpuProfGetInfo(&info);
  if (rc || info.status ||
      !(info.capabilities & (m == 0   ? 4u
                             : m == 3 ? PSP2_GPUPROF_CAP_DIAGNOSTIC
                             : m == 1 ? 1u
                                      : 2u)))
    return rc ? rc : -1;
  backend = info;
  capture_pid = sceKernelGetProcessId();
  capture_isolated = lab_isolated;
  capture_sentinel = lab_sentinel;
  capture_benchmark_phase = lab_benchmark_phase;
  notifications = sceGxmGetNotificationRegion();
  if (!notifications)
    return -1;
  mode = m;
  level = l;
  heavy = h;
  particles = p;
  ns = nd = nr = 0;
  atomic_store(&published_notifications, 0);
  display_count = 0;
  dropped = 0;
  final_rc = end_rc = 0;
  memset(scenes, 0, sizeof(scenes));
  for (unsigned i = 0; i < NS * 2; ++i)
    notifications[i] = 0;
  if (mode == 1) {
    Psp2GpuProfConfig cfg = psp2GpuProfOverviewConfig();
    rc = psp2GpuProfBegin(&cfg, &baseline);
    if (rc)
      return rc;
  }
  thread = sceKernelCreateThread("gpu-lab-sample", sample, 0x10000100, 0x10000,
                                 0, 0, NULL);
  if (thread < 0) {
    if (mode == 1)
      psp2GpuProfEnd(&baseline.session);
    return thread;
  }
  atomic_store(&stop, 0);
  rc = sceKernelStartThread(thread, 0, NULL);
  if (rc < 0) {
    sceKernelDeleteThread(thread);
    thread = -1;
    if (mode == 1)
      psp2GpuProfEnd(&baseline.session);
    return rc;
  }
  active = 1;
  return 0;
}
static void counters(FILE *f, const char *tag, unsigned index,
                     const Psp2GpuProfSample *s) {
  for (unsigned c = 0; c < 4; ++c) {
    fprintf(f, "%s,%u,%u,%u,%u,%u", tag, index, c, s->sequence,
            s->gpu_timer_before, s->gpu_timer_after);
    for (unsigned j = 0; j < 8; ++j)
      fprintf(f, ",%u", s->counters[c][j]);
    fputc('\n', f);
  }
}
int lab_capture_stop(void) {
  if (!active)
    return -1;
  atomic_store(&stop, 1);
  int rc = sceKernelWaitThreadEnd(thread, NULL, NULL);
  if (rc < 0)
    return rc;
  sceKernelDeleteThread(thread);
  thread = -1;
  active = 0;
  if (mode == 1) {
    final_rc = psp2GpuProfRead(&baseline.session, &final_sample);
    for (unsigned i = 0; i < 20; ++i) {
      end_rc = psp2GpuProfEnd(&baseline.session);
      if (!end_rc)
        break;
      sceKernelDelayThread(1000);
    }
  }
  if (end_rc)
    error = end_rc;
  snprintf(path, sizeof(path), "ux0:data/gpuprof-lab-%llu.csv",
           (unsigned long long)sceKernelGetSystemTimeWide());
  FILE *f = fopen(path, "w");
  if (!f)
    return -1;
  fprintf(f, "probe,gpu-lab-v1\nconfig,%u,%u,%d,%d\ncoverage,%u,%u,%u,%d\n",
          mode, level, heavy, particles, nr, ns, nd, dropped || nr == NR);
  fprintf(f, "build,lab-diagnostic-v1\n");
  fprintf(f, "backend,%08x,%u,%u,%u\n", backend.driver_fingerprint, backend.abi,
          backend.capabilities, backend.cores);
  fprintf(f, "application,%08x\nexperiment,%s\n", (unsigned)capture_pid,
          capture_isolated ? "drained-scenes" : "queued-scenes");
  fprintf(f, "identity-state,%u\n", identity_tracker.valid);
  fprintf(f, "draw-order-sentinel,%d\n", capture_sentinel);
  if (capture_benchmark_phase >= 0)
#ifdef GPUPROF_LAB_TIMING_VALIDATION
#ifdef GPUPROF_LAB_TIMING_DIAGNOSTIC
    fprintf(f, "timing-validation,pds-diagnostic-rate-v1,%d,%d,%u\n",
            capture_benchmark_phase, LAB_TEST_FRAMES, timing_delay());
#else
    fprintf(f, "timing-validation,pds-rate-v1,%d,%d,%u\n",
            capture_benchmark_phase, LAB_TEST_FRAMES, timing_delay());
#endif
#elif defined(GPUPROF_LAB_SIGNAL_VALIDATION)
    fprintf(f, "validation,signal-validation-v2,%d,%d\n", capture_benchmark_phase, LAB_TEST_FRAMES);
#else
    fprintf(f, "benchmark,shader-ablation-v1,%d,24\n", capture_benchmark_phase);
#endif
#ifdef GPUPROF_LAB_SIGNAL_VALIDATION
  fprintf(f, "validation-texture,%u,%u\n", capture_benchmark_phase >= 8 ? 2048u : 256u,
          capture_benchmark_phase >= 8 ? 2048u : 256u);
  fprintf(f, "validation-sampling,focused6-jitter1500-2500\n");
#endif
  for (unsigned i = 0; i < display_count; ++i)
    fprintf(f, "display,%u,%08x\n", displays[i].frame,
            (unsigned)displays[i].rc);
  for (unsigned i = 0; i < ns && i < NS; ++i) {
    const Psp2GpuProfIdentity *id = &scenes[i].identity;
    fprintf(f, "identity,%u,%08x,%llu,%llu,%08x,%u,%u\n", i,
            (unsigned)scenes[i].identity_rc, (unsigned long long)id->epoch,
            (unsigned long long)id->context, id->pid, id->frame, id->scene);
  }
  for (unsigned i = 0; i < nprograms; ++i)
    fprintf(f, "shader,%u,%s\n", i + 1, programs[i].name);
  for (unsigned i = 0; i < ns && i < NS; ++i)
    fprintf(f, "scene,%u,%u,%s,%llu,%llu,%08x,%08x,%u,%u\n", i, scenes[i].frame,
            scenes[i].name, (unsigned long long)scenes[i].begin,
            (unsigned long long)scenes[i].end, (unsigned)scenes[i].begin_rc,
            (unsigned)scenes[i].end_rc, notifications[i],
            notifications[NS + i]);
  for (unsigned i = 0; i < nd; ++i)
    fprintf(f, "draw,%u,%u,%s,%u,%u,%u,%08x\n", draws[i].scene,
            draws[i].ordinal, draws[i].name, draws[i].vs, draws[i].fs,
            draws[i].count, (unsigned)draws[i].rc);
  if (mode == 1) {
    counters(f, "baseline", 0, &baseline);
    if (!final_rc)
      counters(f, "final", 0, &final_sample);
    fprintf(f, "counter-end,%08x,%08x\n", (unsigned)final_rc, (unsigned)end_rc);
  }
  for (unsigned i = 0; i < nr; ++i) {
    struct Record *r = &records[i];
    fprintf(f, "read-window,%u,%llu,%llu\n", i,
            (unsigned long long)r->read_begin, (unsigned long long)r->read_end);
    fprintf(f, "call,%u,%llu,%llu,%08x,%d,%d,%d\n", i,
            (unsigned long long)r->begin, (unsigned long long)r->end,
            (unsigned)r->rc, r->gpu0, r->gpu1, r->xbar);
    fprintf(f, "done,%u", i);
    for (unsigned j = 0; j < 8; ++j)
      fprintf(f, ",%08x,%08x", r->ta[j], r->fragment[j]);
    fputc('\n', f);
    fprintf(f, "done-before,%u", i);
    for (unsigned j = 0; j < 8; ++j)
      fprintf(f, ",%08x,%08x", r->ta_before[j], r->fragment_before[j]);
    fputc('\n', f);
    if (r->rc)
      continue;
    if (mode == 1)
      counters(f, "counter", i, &r->data.counters);
    else if (mode == 2) {
      Psp2GpuProfSignals *s = &r->data.signals;
      for (unsigned g = 0; g < s->group_count; ++g) {
        fprintf(f, "signal,%u,%u,%u,%u", i, s->groups[g], s->timer_before[g],
                s->timer_after[g]);
        for (unsigned c = 0; c < 4; ++c)
          fprintf(f, ",%08x", s->values[g][c]);
        fputc('\n', f);
      }
    } else if (mode == 3) {
      Psp2GpuProfDiagnostic *d=&r->data.diagnostic;
      for(unsigned c=0;c<4;++c) {
        Psp2GpuProfDiagnosticCore *v=&d->cores[c];
        fprintf(f,"diagnostic,%u,%u,%u,%u,%u,%u,%08x,%08x,%08x,%08x,%08x,%08x",
            i,c,d->group,d->tag_group,v->timer_before,v->timer_after,
            v->pds_before,v->tag_before,v->value,v->tag_after,v->pds_after,v->flags);
        Psp2GpuProfWorkState *s=&v->before;
        fprintf(f,",%08x,%u,%u,%08x,%u,%u",s->ta_pid,s->ta_frame,s->ta_scene,
            s->render_pid,s->render_frame,s->render_scene);
        s=&v->after;
        fprintf(f,",%08x,%u,%u,%08x,%u,%u\n",s->ta_pid,s->ta_frame,s->ta_scene,
            s->render_pid,s->render_frame,s->render_scene);
      }
    } else
      for (unsigned c = 0; c < 4; ++c) {
        Psp2GpuProfWorkCore *v = &r->data.work.cores[c];
        fprintf(f, "work,%u,%u,%u,%u,%08x,%08x", i, c, v->timer_before,
                v->timer_after, v->fragment_signal, v->flags);
        Psp2GpuProfWorkState *s = &v->before;
        fprintf(f, ",%08x,%u,%u,%08x,%u,%u", s->ta_pid, s->ta_frame,
                s->ta_scene, s->render_pid, s->render_frame, s->render_scene);
        s = &v->after;
        fprintf(f, ",%08x,%u,%u,%08x,%u,%u\n", s->ta_pid, s->ta_frame,
                s->ta_scene, s->render_pid, s->render_frame, s->render_scene);
      }
  }
  fprintf(f, "result,%08x\n", (unsigned)error);
  if (ferror(f))
    rc = -1;
  if (fclose(f))
    rc = -1;
  return rc ? rc : final_rc ? final_rc : end_rc ? end_rc : error;
}
int lab_capture_active(void) { return active; }
int lab_capture_error(void) { return error; }
const char *lab_capture_path(void) { return path; }
