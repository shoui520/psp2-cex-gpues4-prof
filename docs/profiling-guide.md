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

Usage the analyzer on a GPU capture .csv:
```sh
python3 tools/analyze.py capture.csv
```

Example output:
```text

  GPU Profiling Summary
  ───────────────────────────────────────────────────────────────────────────────────────────

  Dominant pass  Transparent overdraw
                 64.4% of attributed fragment time

  Attributed     2,130.998 ms     Draws with timing  813/1,206

  Fragment processing · capture totals · four-core average

  Passes

  Name                              Relative time              Time   Share     Timed draws
  Transparent overdraw              ████████████████   1,372.986 ms   64.4%         232/240
  Opaque textured geometry          █████                426.073 ms   20.0%         444/780
  Offscreen composite               ███                  217.387 ms   10.2%           28/60
  Unlabelled                        █                    114.552 ms    5.4%         109/126

  Most expensive draw groups

  Name                              Relative time              Time   Share     Timed draws
  Color and depth clear             ████████████████     298.803 ms   14.0%           28/30
    Opaque textured geometry / fs1
  Five-tap filtered fullscreen      ████████████         217.387 ms   10.2%           28/30
    Offscreen composite / fs4
  Transparent shell 7               ███████████          196.252 ms    9.2%           29/30
    Transparent overdraw / fs3
  Transparent shell 5               ██████████           189.736 ms    8.9%           29/30
    Transparent overdraw / fs3
  Transparent shell 6               ██████████           180.212 ms    8.5%           29/30
    Transparent overdraw / fs3
  Transparent shell 3               █████████            174.421 ms    8.2%           29/30
    Transparent overdraw / fs3
  Transparent shell 4               █████████            168.070 ms    7.9%           29/30
    Transparent overdraw / fs3
  Transparent shell 2               █████████            160.688 ms    7.5%           29/30
    Transparent overdraw / fs3
  … 30 more in --json

  Fragment shaders

  Name                              Relative time              Time   Share     Timed draws
  fs3                               ████████████████   1,372.986 ms   64.4%         232/240
  fs1                               ████                 312.212 ms   14.7%           76/93
  fs2                               ███                  228.413 ms   10.7%         477/783
  fs4                               ███                  217.387 ms   10.2%           28/30
  fs5 / GXM mask update             ·                             —       —            0/60

  Share = attributed time only. Timed draws = with timing / recorded.
  Estimated timings. Unsampled work is excluded.

  ───────────────────────────────────────────────────────────────────────────────────────────
  Pipeline activity

  Observed during matched application draw windows.
  Sample hits, not time or utilization. Signals overlap; groups are read separately.

  Shader execution
  Signal                                  Sample hits         Rate  Hits / samples    Units
  Shader engine non-idle                  ███████▋           47.6%     1,282/2,694    16/16
  Shader datapath running                 ███████            43.8%     1,179/2,694    16/16
  Fragment instruction activity           ██████▉            42.9%     1,154/2,693    16/16
  Vertex instruction activity             ▏                   1.0%        28/2,693    16/16
  Shader datapath stalled                 ▏                   0.4%        10/2,694    16/16
  Firmware instruction activity           ▏                  <0.1%         1/2,693    16/16
  Tile-end instruction activity           ·                   0.0%         0/2,693    16/16

  Shader waits
  Signal                                  Sample hits         Rate  Hits / samples    Units
  Texture issue stall                     ▏                   0.3%         7/2,675    16/16
  Load/store issue stall                  ·                   0.0%         0/2,675    16/16
  Pixel-output interface stall            ·                   0.0%         0/2,675    16/16
  PDS interface stall                     ·                   0.0%         0/2,675    16/16
  ISP interface stall                     ·                   0.0%         0/2,675    16/16
  MTE interface stall                     ·                   0.0%         0/2,675    16/16
  SOC interface stall                     ·                   0.0%         0/2,675    16/16

  Texture / data cache
  Signal                                  Sample hits         Rate  Hits / samples    Units
  Texture requests outstanding            ██▏                13.3%          91/686      4/4
  Data-cache return stall                 ▏                   0.3%           2/683      4/4
  Texture L1/L2 stall                     ·                   0.0%           0/686      4/4
  Texture memory-interface stall          ·                   0.0%           0/686      4/4
  Texture internal FIFO stall             ·                   0.0%           0/686      4/4
  Data-cache L1/L2 stall                  ·                   0.0%           0/683      4/4
  Data-cache memory-interface stall       ·                   0.0%           0/683      4/4

  Shader feed
  Signal                                  Sample hits         Rate  Hits / samples    Units
  PDS shader-task queue stall             ▎                   1.4%          10/692      4/4
  PDS pixel partition stall               ▏                   1.2%           8/692      4/4
  PDS pixel dependency stall              ·                   0.0%           0/692      4/4
  PDS data-cache wait                     ·                   0.0%           0/692      4/4
  PDS code-cache wait                     ·                   0.0%           0/692      4/4

  Idle signals
  Signal                                  Sample hits         Rate  Hits / samples    Units
  Texture pipe idle                       ████████████       75.3%     1,019/1,354      8/8
  Shader datapath idle                    ████████▉          55.8%     1,511/2,708    16/16
  Units = observed / expected core-and-pipe combinations.

  Pipeline observations by pass
  Pass                                Shader running     Texture issue     Texture L1/L2
  Transparent overdraw                 42.3% / 1,661      0.1% / 1,666        0.0% / 416
  Opaque textured geometry               12.6% / 523        0.0% / 518        0.0% / 140
  Offscreen composite                    96.3% / 297        1.7% / 295         0.0% / 71
  Unlabelled                             58.7% / 213        0.0% / 196         0.0% / 59

  Pipeline observations by fragment shader
  Fragment shader                     Shader running     Texture issue     Texture L1/L2
  fs3                                  42.3% / 1,661      0.1% / 1,666        0.0% / 416
  fs1                                     3.5% / 398        0.0% / 372        0.0% / 106
  fs2                                    52.4% / 338        0.0% / 342         0.0% / 93
  fs4                                    96.3% / 297        1.7% / 295         0.0% / 71
  fs5                                              —                 —                 —
  Cells: hit rate / samples. Texture columns show stalls, not exclusive costs.

  ───────────────────────────────────────────────────────────────────────────────────────────
  Fragment timing by GPU core

  Core 0  ████████████████████████    2,122.205 ms
  Core 1  ████████████████████████    2,146.608 ms
  Core 2  ████████████████████████    2,117.945 ms
  Core 3  ████████████████████████    2,137.232 ms
  Concurrent core estimates; the rankings above use their average.

  ───────────────────────────────────────────────────────────────────────────────────────────
  Capture

  Recorded events             58,741    Draw calls                   1,206
  Hardware samples             3,859    Presentations                   93
  Scenes closed            123 / 123    Transfer calls                   7
  Labelled draw frames            30    Fragment shaders                 5
  Recording span             3.398 s    Dropped events                   0
  Observed GPU clocks   111 MHz (3,859 reads)

  Timing coverage

  Draws without timing   393 / 1,206    Failed samples                   0
  Unassigned gaps       1,197.776 ms   (CPU-clock envelope)
  Sample gap · mean         862.9 µs    Sample gap · max        1,641.0 µs

  Excluded timing observations
  inactive/unmapped PDS                       3,875
  PDS changed                                   749
  scheduler changed                              12

  Matched signal observations: 10,800
  outside matched PDS draw windows                  4,636
```
Pass `--json` for JSON output.  

If timing is unavailable, the report prints the reason. Common causes: no
identity seed, capture buffer overflow, or instanced/precomputed draws and
mid-scene flushes during the capture.

## Example

[`examples/vitagl-scene/`](../examples/vitagl-scene) is a complete profiled
3D app.
