# Programming Guide

Record a capture on the Vita, then rank passes,
draws and shaders by fragment-processing time.

## Build and install

```sh
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DVITASDK="$VITASDK" \
  -DCMAKE_BUILD_TYPE=Release \
  -DGPUPROF_BUILD_VITAGL_ADAPTER=ON \
  -DCMAKE_INSTALL_PREFIX="$VITASDK/arm-vita-eabi"
cmake --build build --target psp2_gpuprof.skprx-self gpuprof_stubs \
  psp2_gpuprof_client psp2_gpuprof_gxm_observer psp2_gpuprof_vitagl
cmake --install build
```

For raw GXM, drop the vitaGL option and the `psp2_gpuprof_vitagl` target.

## Link

```cmake
find_package(psp2_gpuprof CONFIG REQUIRED)
find_library(GPUPROF_STUB NAMES psp2_gpuprof_stub REQUIRED)
target_link_libraries(my_game PRIVATE
    psp2_gpuprof::vitagl vitaGL   # raw GXM: psp2_gpuprof::client psp2_gpuprof::gxm
    ${GPUPROF_STUB} ScePower_stub)
```

## Start capturing

Do this before `vglInit` / `sceGxmInitialize`:

```c
#include <psp2_gpuprof_capture_diagnostic.h>
#include <psp2_gpuprof_gxm_observer.h>
#include <psp2_gpuprof_labels.h>
#include <psp2_gpuprof_vitagl.h>

static Psp2GpuProfCaptureEvent events[131072];
static Psp2GpuProfCapture capture;
static SceUID mutex;

static void lock(void *p)   { sceKernelLockMutex(mutex, 1, NULL); }
static void unlock(void *p) { sceKernelUnlockMutex(mutex, 1); }

mutex = sceKernelCreateMutex("gpuprof", 0, 0, NULL);
Psp2GpuProfCaptureConfig cfg = {events, 131072, lock, unlock, NULL};
psp2GpuProfCaptureInit(&capture, &cfg);
psp2GpuProfCaptureStart(&capture);
psp2GpuProfCaptureGxmIdentitySeed365(&capture, sceKernelGetProcessId(), 0, 0);
psp2GpuProfGxmSetObserver(psp2GpuProfCaptureGxmCall, &capture);
psp2GpuProfVitaGLCapture(&capture);   /* vitaGL only */
```

## Label your frame

One `Psp2GpuProfLabels` per rendering thread:

```c
Psp2GpuProfLabels labels;
psp2GpuProfLabelsBegin(&labels, &capture, sceKernelGetThreadId());

psp2GpuProfFrameName(&labels, sceKernelGetSystemTimeWide(), frame, "Gameplay");

int scope = psp2GpuProfScopePush(&labels, sceKernelGetSystemTimeWide(), "Opaque");
psp2GpuProfDrawName(&labels, sceKernelGetSystemTimeWide(), "Terrain");
/* draw calls */
psp2GpuProfDrawName(&labels, sceKernelGetSystemTimeWide(), NULL);
if (!scope) psp2GpuProfScopePop(&labels, sceKernelGetSystemTimeWide());
```

Name shaders once after creating them. Space `2` is a GXM fragment program,
`1` a vertex program, `3` a vitaGL program (patcher `0`):

```c
psp2GpuProfShaderName(&labels, sceKernelGetSystemTimeWide(), 3, 0, program, "Water");
```

## Sample the GPU

Run this on its own thread after graphics init:

```c
static int sampler(SceSize size, void *arg) {
    static const unsigned groups[] = {2,4,43,55,71,72,75,76,77,78,103,104,107,108,109,110};
    Psp2GpuProfInfo info = {.size = sizeof(info), .abi = PSP2_GPUPROF_ABI};
    psp2GpuProfGetInfo(&info);
    for (uint64_t id = 0; !stop; ++id) {
        unsigned group = groups[id % 16];
        Psp2GpuProfDiagnosticConfig cfg = {.size = sizeof(cfg), .abi = PSP2_GPUPROF_ABI,
            .group = group, .tag_group = group >= 103 ? 102 : 70};
        Psp2GpuProfDiagnostic data = {0};
        Psp2GpuProfDiagnosticObservation o = {
            .sampler_thread = sceKernelGetThreadId(), .sample_id = id,
            .group = group, .tag_group = cfg.tag_group,
            .process_id = sceKernelGetProcessId(),
            .driver_fingerprint = info.driver_fingerprint};
        o.clock_before_mhz = scePowerGetGpuClockFrequency();
        o.before_us = sceKernelGetSystemTimeWide();
        o.result = psp2GpuProfReadDiagnostic(&cfg, &data);
        o.after_us = sceKernelGetSystemTimeWide();
        o.clock_after_mhz = scePowerGetGpuClockFrequency();
        psp2GpuProfCaptureDiagnostic(&capture, &o, o.result ? NULL : &data);
        sceKernelDelayThread(500);
    }
    return 0;
}
```

## Save

Stop the sampler thread first, then:

```c
psp2GpuProfCaptureStop(&capture);
FILE *f = fopen("ux0:data/capture.csv", "w");
psp2GpuProfCaptureWriteCsv(&capture, f);
fclose(f);
```

## Analyze

```sh
python3 tools/analyze.py capture.csv
```

```text
Estimated fragment processing — captured intervals, four-core average
Transparent overdraw             ████████████████████████████████  1372.986 ms
Opaque textured geometry         ██████████                         426.073 ms
Offscreen composite              █████                              217.387 ms
Unlabelled                       ███                                114.552 ms
```

If timing is unavailable, the report prints the reason. Common causes: no
identity seed, capture buffer overflow, or instanced/precomputed draws and
mid-scene flushes during the capture.

## Example

[`examples/vitagl-scene/`](../examples/vitagl-scene) is a complete profiled
3D app.
