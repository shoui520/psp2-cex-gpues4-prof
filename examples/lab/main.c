#include "capture.h"
#include <math.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vita2d.h>

#define PI 3.14159265f
#define U 48
#define V 24
typedef struct Vertex {
  float p[3], n[3], uv[2];
} Vertex;
static Vertex *vertices;
static uint16_t *indices;
static SceUID memory = -1;
static SceGxmProgram *programs[5];
static SceGxmShaderPatcherId ids[5];
static unsigned registered;
static SceGxmVertexProgram *vp;
static SceGxmFragmentProgram *fp[5];
static const SceGxmProgramParameter *mvp_param, *world_param, *tint_param[5];
static vita2d_texture *target, *checker;
#ifdef GPUPROF_LAB_SIGNAL_VALIDATION
static vita2d_texture *large_checker;
#endif
static vita2d_pgf *font;
static float viewproj[16];
static SceGxmContext *ctx;
static int failure;
static void check(int rc) {
  if (rc < 0 && !failure)
    failure = rc;
}
static void multiply(float *o, const float *a, const float *b) {
  for (unsigned r = 0; r < 4; ++r)
    for (unsigned c = 0; c < 4; ++c) {
      o[r * 4 + c] = 0;
      for (unsigned k = 0; k < 4; ++k)
        o[r * 4 + c] += a[r * 4 + k] * b[k * 4 + c];
    }
}
static SceGxmProgram *load(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  if (fseek(f, 0, SEEK_END)) {
    fclose(f);
    return NULL;
  }
  long n = ftell(f);
  if (n <= 0 || n > 1048576) {
    fclose(f);
    return NULL;
  }
  rewind(f);
  void *p = malloc((size_t)n);
  if (p && fread(p, 1, (size_t)n, f) != (size_t)n) {
    free(p);
    p = NULL;
  }
  fclose(f);
  return p;
}
static int setup(void) {
  ctx = vita2d_get_context();
  target = vita2d_create_empty_texture_rendertarget(
      960, 544, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR);
  checker = vita2d_create_empty_texture(256, 256);
  font = vita2d_load_default_pgf();
  if (!target || !checker || !font)
    return -1;
  uint8_t *pixels = vita2d_texture_get_datap(checker);
  unsigned stride = vita2d_texture_get_stride(checker);
  for (unsigned y = 0; y < 256; ++y)
    for (unsigned x = 0; x < 256; ++x) {
      unsigned c = ((x / 16) ^ (y / 16)) & 1;
      ((uint32_t *)(pixels + y * stride))[x] = c ? 0xffe0c080u : 0xff705030u;
    }
  vita2d_texture_set_filters(checker, SCE_GXM_TEXTURE_FILTER_LINEAR,
                             SCE_GXM_TEXTURE_FILTER_LINEAR);
#ifdef GPUPROF_LAB_SIGNAL_VALIDATION
  large_checker = vita2d_create_empty_texture(2048, 2048);
  if (!large_checker)
    return -1;
  uint8_t *large_pixels = vita2d_texture_get_datap(large_checker);
  unsigned large_stride = vita2d_texture_get_stride(large_checker);
  for (unsigned y = 0; y < 2048; ++y)
    for (unsigned x = 0; x < 2048; ++x)
      ((uint32_t *)(large_pixels + y * large_stride))[x] =
          (((x / 128) ^ (y / 128)) & 1) ? 0xffe0c080u : 0xff705030u;
  vita2d_texture_set_filters(large_checker, SCE_GXM_TEXTURE_FILTER_LINEAR,
                             SCE_GXM_TEXTURE_FILTER_LINEAR);
#endif
  memory = sceKernelAllocMemBlock(
      "lab-mesh", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 0x20000, NULL);
  if (memory < 0)
    return memory;
  void *base = NULL;
  check(sceKernelGetMemBlockBase(memory, &base));
  if (failure)
    return failure;
  check(sceGxmMapMemory(base, 0x20000, SCE_GXM_MEMORY_ATTRIB_READ));
  if (failure)
    return failure;
  vertices = base;
  indices = (uint16_t *)((uint8_t *)base + 0x10000);
  for (unsigned u = 0; u <= U; ++u)
    for (unsigned v = 0; v <= V; ++v) {
      float a = 2 * PI * u / U, b = 2 * PI * v / V, c = cosf(a), s = sinf(a),
            r = 1.0f + 0.34f * cosf(b);
      Vertex *t = &vertices[u * (V + 1) + v];
      *t = (Vertex){{r * c, 0.34f * sinf(b), r * s},
                    {c * cosf(b), sinf(b), s * cosf(b)},
                    {(float)u / U, (float)v / V}};
    }
  unsigned n = 0;
  for (unsigned u = 0; u < U; ++u)
    for (unsigned v = 0; v < V; ++v) {
      uint16_t a = u * (V + 1) + v, b = a + V + 1;
      indices[n++] = a;
      indices[n++] = b;
      indices[n++] = a + 1;
      indices[n++] = a + 1;
      indices[n++] = b;
      indices[n++] = b + 1;
    }
  const char *paths[] = {"app0:shaders/scene_v.gxp", "app0:shaders/lit_f.gxp",
                         "app0:shaders/heavy_f.gxp", "app0:shaders/warp_f.gxp",
                         "app0:shaders/fetch_f.gxp"};
  SceGxmShaderPatcher *patcher = vita2d_get_shader_patcher();
  for (unsigned i = 0; i < 5; ++i) {
    programs[i] = load(paths[i]);
    if (!programs[i])
      return -1;
    check(sceGxmShaderPatcherRegisterProgram(patcher, programs[i], &ids[i]));
    if (failure)
      return failure;
    ++registered;
  }
  SceGxmVertexAttribute attrs[3] = {0};
  const char *names[] = {"position", "normal", "uv"};
  const unsigned offsets[] = {0, 12, 24};
  for (unsigned i = 0; i < 3; ++i) {
    const SceGxmProgramParameter *p =
        sceGxmProgramFindParameterByName(programs[0], names[i]);
    if (!p)
      return -1;
    attrs[i].offset = offsets[i];
    attrs[i].format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
    attrs[i].componentCount = i == 2 ? 2 : 3;
    attrs[i].regIndex = sceGxmProgramParameterGetResourceIndex(p);
  }
  SceGxmVertexStream stream = {sizeof(Vertex),
                               SCE_GXM_INDEX_SOURCE_INDEX_16BIT};
  check(sceGxmShaderPatcherCreateVertexProgram(patcher, ids[0], attrs, 3,
                                               &stream, 1, &vp));
  SceGxmBlendInfo blend = {0};
  blend.colorMask = SCE_GXM_COLOR_MASK_ALL;
  blend.colorFunc = blend.alphaFunc = SCE_GXM_BLEND_FUNC_ADD;
  blend.colorSrc = blend.alphaSrc = SCE_GXM_BLEND_FACTOR_SRC_ALPHA;
  blend.colorDst = blend.alphaDst = SCE_GXM_BLEND_FACTOR_ONE;
  for (unsigned i = 0; i < 5; ++i) {
    unsigned shader = i >= 3 ? i : i == 1 ? 2 : 1;
    check(sceGxmShaderPatcherCreateFragmentProgram(
        patcher, ids[shader], SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,
        SCE_GXM_MULTISAMPLE_NONE, i == 2 ? &blend : NULL, programs[0], &fp[i]));
    tint_param[i] = sceGxmProgramFindParameterByName(programs[shader], "tint");
    if (!tint_param[i])
      return -1;
  }
  mvp_param = sceGxmProgramFindParameterByName(programs[0], "mvp");
  world_param = sceGxmProgramFindParameterByName(programs[0], "world");
  if (!mvp_param || !world_param)
    return -1;
  lab_program(vp, "mesh-transform-normal-vs");
  lab_program(fp[0], "textured-lighting-fs");
  lab_program(fp[1], "12-fetch-warp-lighting-fs");
  lab_program(fp[2], "additive-lighting-fs");
  lab_program(fp[3], "12-warp-1-fetch-lighting-fs");
  lab_program(fp[4], "12-fetch-simple-uv-lighting-fs");
  float projection[16] = {0.9815f, 0, 0,      0,        0, 1.732f, 0, 0,
                          0,       0, 1.001f, -0.1001f, 0, 0,      1, 0};
  float view[16] = {1, 0,        0,       0,       0, .95534f, .29552f, -.1763f,
                    0, -.29552f, .95534f, 23.084f, 0, 0,       0,       1};
  multiply(viewproj, projection, view);
  return failure;
}
static void object(float x, float y, float z, float scale, float angle,
                   unsigned shader, const float *tint) {
  float c = cosf(angle) * scale, s = sinf(angle) * scale;
  float world[16] = {c, -s, 0, x, s, c, 0, y, 0, 0, scale, z, 0, 0, 0, 1},
        mvp[16];
  multiply(mvp, viewproj, world);
  sceGxmSetVertexProgram(ctx, vp);
  sceGxmSetFragmentProgram(ctx, fp[shader]);
  vita2d_texture *surface = checker;
#ifdef GPUPROF_LAB_SIGNAL_VALIDATION
  if (lab_benchmark_phase >= 8)
    surface = large_checker;
#endif
  check(sceGxmSetFragmentTexture(ctx, 0, &surface->gxm_tex));
  void *uniform = NULL;
  check(sceGxmReserveVertexDefaultUniformBuffer(ctx, &uniform));
  if (failure)
    return;
  check(sceGxmSetUniformDataF(uniform, mvp_param, 0, 16, mvp));
  check(sceGxmSetUniformDataF(uniform, world_param, 0, 16, world));
  check(sceGxmReserveFragmentDefaultUniformBuffer(ctx, &uniform));
  if (failure)
    return;
  check(sceGxmSetUniformDataF(uniform, tint_param[shader], 0, 4, tint));
  check(sceGxmSetVertexStream(ctx, 0, vertices));
  check(sceGxmDraw(ctx, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U16,
                   indices, U * V * 6));
}
int main(void) {
  lab_identity_init();
  if (vita2d_init_advanced(4 * 1024 * 1024) <= 0)
    return 1;
  int rc = setup();
  if (rc) {
    FILE *f = fopen("ux0:data/gpuprof-lab-startup.log", "w");
    if (f) {
      fprintf(f, "setup,%08x\n", (unsigned)rc);
      fclose(f);
    }
    return 1;
  }
  sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
  unsigned level = 2, mode = 3, old_buttons = 0;
  unsigned benchmark_frames = 0;
  int heavy = 1, particles = 0, paused = 1, last_capture = 0, auto_capture = 0;
#ifdef GPUPROF_LAB_SIGNAL_VALIDATION
  /* Two captures per known shader workload; no manual frame matching. */
  lab_benchmark_phase = 0;
  auto_capture = 1;
#endif
#ifdef GPUPROF_LAB_TIMING_VALIDATION
  lab_benchmark_phase = 0;
  auto_capture = 1;
#endif
  float time = 1.25f, frame_ms = 0;
  uint64_t previous = sceKernelGetSystemTimeWide(), capture_start = 0,
           launched = previous;
  while (!failure && !lab_capture_error()) {
    uint64_t now = sceKernelGetSystemTimeWide();
    float dt = (now - previous) / 1000000.0f;
    previous = now;
    frame_ms = frame_ms * .9f + dt * 1000 * .1f;
    if (!paused)
      time += dt;
    SceCtrlData pad;
    sceCtrlPeekBufferPositive(0, &pad, 1);
    unsigned pressed = pad.buttons & ~old_buttons;
    old_buttons = pad.buttons;
    if ((pad.buttons & (SCE_CTRL_START | SCE_CTRL_SELECT)) ==
        (SCE_CTRL_START | SCE_CTRL_SELECT))
      break;
    if (!lab_capture_active() && lab_benchmark_phase < 0) {
      if (pressed & SCE_CTRL_RTRIGGER) {
        lab_benchmark_phase = 0;
        benchmark_frames = 0;
        auto_capture = 1;
      }
      if (pressed & SCE_CTRL_RIGHT && level < 4)
        ++level;
      if (pressed & SCE_CTRL_LEFT && level > 1)
        --level;
      if (pressed & SCE_CTRL_TRIANGLE)
        heavy = !heavy;
      if (pressed & SCE_CTRL_SQUARE)
        particles = !particles;
      if (pressed & SCE_CTRL_SELECT)
        paused = !paused;
      if (pressed & SCE_CTRL_CROSS)
        mode = (mode + 1) % 4;
      if (pressed & SCE_CTRL_CIRCLE)
        lab_isolated = !lab_isolated;
      if (pressed & SCE_CTRL_LTRIGGER)
        lab_sentinel = !lab_sentinel;
      if (lab_benchmark_phase < 0 && ((pressed & SCE_CTRL_START) ||
          (!auto_capture && now - launched > 2000000))) {
        auto_capture = 1;
        last_capture = lab_capture_start(mode, level, heavy, particles);
        capture_start = now;
      }
    }
    if (lab_benchmark_phase >= 0) {
      /* Reproduce geometry and bindings without manual timing. Twelve warmup
       * frames precede each capture. Sampling remains asynchronous. */
      level = 2;
      heavy = lab_benchmark_phase >= 2;
      mode = lab_benchmark_phase % 2;
#ifdef GPUPROF_LAB_TIMING_VALIDATION
      heavy = 1;
      mode = 0;
#ifdef GPUPROF_LAB_TIMING_DIAGNOSTIC
      mode = 3;
#endif
#endif
#ifdef GPUPROF_LAB_SIGNAL_VALIDATION
      mode = 3;
#endif
      particles = 0;
      lab_sentinel = lab_isolated = 0;
      paused = 1;
      time = 1.25f;
      if (!lab_capture_active() && benchmark_frames >= 12) {
        last_capture = lab_capture_start(mode, level, heavy, particles);
        if (last_capture) {
          failure = last_capture;
          break;
        }
        benchmark_frames = 0;
      }
    }
    lab_pass = "opaque-3d";
    lab_draw = "clear-offscreen";
    vita2d_start_drawing_advanced(target,
                                  SCE_GXM_SCENE_FRAGMENT_SET_DEPENDENCY);
    if (lab_sentinel) {
      /* Shift every offscreen draw ordinal by one without changing the final
       * image. The following clear overwrites this one-pixel diagnostic draw.
       * Explicit depth state avoids reading the uncleared depth surface. */
      lab_draw = "one-pixel-sentinel";
      sceGxmSetFrontDepthFunc(ctx, SCE_GXM_DEPTH_FUNC_ALWAYS);
      sceGxmSetBackDepthFunc(ctx, SCE_GXM_DEPTH_FUNC_ALWAYS);
      sceGxmSetFrontDepthWriteEnable(ctx, SCE_GXM_DEPTH_WRITE_DISABLED);
      sceGxmSetBackDepthWriteEnable(ctx, SCE_GXM_DEPTH_WRITE_DISABLED);
      vita2d_draw_rectangle(0, 0, 1, 1, 0xffffffff);
    }
    lab_draw = "clear-offscreen";
    vita2d_set_clear_color(0xff20100au);
    vita2d_clear_screen();
    sceGxmSetCullMode(ctx, SCE_GXM_CULL_NONE);
    sceGxmSetFrontDepthFunc(ctx, SCE_GXM_DEPTH_FUNC_LESS_EQUAL);
    sceGxmSetBackDepthFunc(ctx, SCE_GXM_DEPTH_FUNC_LESS_EQUAL);
    sceGxmSetFrontDepthWriteEnable(ctx, SCE_GXM_DEPTH_WRITE_ENABLED);
    sceGxmSetBackDepthWriteEnable(ctx, SCE_GXM_DEPTH_WRITE_ENABLED);
    unsigned side = level + 3;
    lab_draw = "lit-torus-grid";
    for (unsigned z = 0; z < side; ++z)
      for (unsigned x = 0; x < side; ++x) {
        float tint[4] = {.35f + .6f * x / side, .4f + .5f * z / side, .85f, 1};
        object(((float)x - (side - 1) * .5f) * 2.9f,
               sinf(time + x * .7f + z) * .5f,
               ((float)z - (side - 1) * .5f) * 2.9f, .9f, time * .4f + x * .2f,
#ifdef GPUPROF_LAB_TIMING_VALIDATION
               1, tint);
#else
               lab_benchmark_phase >= 8 ? 4 :
               lab_benchmark_phase >= 4 ? 3 + (lab_benchmark_phase - 4) / 2 :
                                         heavy ? 1 : 0, tint);
#endif
      }
    if (particles) {
      lab_draw = "additive-orbit-shells";
      sceGxmSetFrontDepthWriteEnable(ctx, SCE_GXM_DEPTH_WRITE_DISABLED);
      sceGxmSetBackDepthWriteEnable(ctx, SCE_GXM_DEPTH_WRITE_DISABLED);
      for (unsigned i = 0; i < level * 8; ++i) {
        float a = time * .35f + i * 2 * PI / (level * 8);
        float tint[4] = {.15f, .5f, 1, .12f};
        object(cosf(a) * 5, 2 + sinf(a * 3 + time), sinf(a) * 5, 1.8f, a, 2,
               tint);
      }
    }
    vita2d_end_drawing();
    /* Diagnostic control: remove scene overlap without changing their draws. */
    if (lab_isolated)
      vita2d_wait_rendering_done();
    lab_pass = "composite-and-hud";
    lab_draw = "clear-display";
    vita2d_start_drawing_advanced(NULL,
                                  SCE_GXM_SCENE_VERTEX_WAIT_FOR_DEPENDENCY);
    vita2d_clear_screen();
    sceGxmSetFrontDepthFunc(ctx, SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetBackDepthFunc(ctx, SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetFrontDepthWriteEnable(ctx, SCE_GXM_DEPTH_WRITE_DISABLED);
    sceGxmSetBackDepthWriteEnable(ctx, SCE_GXM_DEPTH_WRITE_DISABLED);
    lab_draw = "render-target-composite";
    vita2d_draw_texture(target, 0, 0);
    lab_draw = "hud";
    vita2d_draw_rectangle(0, 0, 960, 106, 0xc0201008);
    if (lab_benchmark_phase >= 0) {
      /* Identical HUD geometry/text in every benchmark phase. */
      vita2d_pgf_draw_text(font, 16, 49, 0xffffffff, .8f,
                         "Automatic benchmark running. Please wait.");
    } else {
    vita2d_pgf_draw_textf(font, 16, 25, 0xff80e0ff, 1.1f,
                          "GPU LAB | %s | %s | level %u",
                          heavy ? "12-fetch shader" : "lit shader",
                          particles ? "additive ON" : "additive OFF", level);
    vita2d_pgf_draw_textf(font, 16, 49, 0xffffffff, .8f,
                          "CPU frame %.1f ms (not GPU time) | ES4 %d MHz | %s",
                          frame_ms, scePowerGetGpuClockFrequency(),
                          lab_capture_active() ? "CAPTURING" : "ready");
    vita2d_pgf_draw_textf(font, 16, 72, 0xffffffff, .7f,
                          "Left/Right density | Triangle shader | Square "
                          "transparency | Select freeze | X mode: %s",
                          mode == 0   ? "WORK"
                          : mode == 1 ? "COUNTERS"
                          : mode == 2 ? "SIGNALS" : "DIAGNOSTIC");
    vita2d_pgf_draw_textf(
        font, 16, 95, 0xffa0ffa0, .7f,
        "Start capture | R: auto test | Circle: %s | L: sentinel %s | %08x",
        lab_isolated ? "drained" : "queued", lab_sentinel ? "ON" : "OFF",
        (unsigned)last_capture);
    }
    vita2d_end_drawing();
    vita2d_wait_rendering_done();
    /* HUD/text allocations live in vita2d's per-frame temporary pool.
     * Reclaim only after both scenes have finished using their vertices. */
    vita2d_pool_reset();
    vita2d_swap_buffers();
    ++lab_frame;
    if (lab_benchmark_phase >= 0)
      ++benchmark_frames;
    if (lab_capture_active() &&
        (lab_benchmark_phase >= 0 ? benchmark_frames >= LAB_TEST_FRAMES :
                                   now - capture_start > 2000000)) {
      last_capture = lab_capture_stop();
      if (lab_capture_active())
        return 1;
      if (lab_benchmark_phase >= 0) {
        benchmark_frames = 0;
        if (last_capture || ++lab_benchmark_phase == LAB_TEST_PHASES)
          lab_benchmark_phase = -1;
      }
    }
  }
  vita2d_wait_rendering_done();
  if (lab_capture_active())
    lab_capture_stop();
  if (lab_capture_active())
    return 1;
  /* Process exit also releases resources; explicit teardown after GPU drain. */
  SceGxmShaderPatcher *patcher = vita2d_get_shader_patcher();
  for (unsigned i = 0; i < 5; ++i)
    if (fp[i])
      sceGxmShaderPatcherReleaseFragmentProgram(patcher, fp[i]);
  if (vp)
    sceGxmShaderPatcherReleaseVertexProgram(patcher, vp);
  for (unsigned i = 0; i < registered; ++i)
    sceGxmShaderPatcherUnregisterProgram(patcher, ids[i]);
  for (unsigned i = 0; i < 5; ++i)
    free(programs[i]);
  sceGxmUnmapMemory(vertices);
  sceKernelFreeMemBlock(memory);
  vita2d_free_texture(target);
  vita2d_free_texture(checker);
#ifdef GPUPROF_LAB_SIGNAL_VALIDATION
  vita2d_free_texture(large_checker);
#endif
  vita2d_free_pgf(font);
  vita2d_fini();
  return failure || lab_capture_error() ? 1 : 0;
}
