# psp2-cex-gpues4-prof

Experimental GPU profiling for retail PS Vitas.

## Install

Include `psp2_gpuprof.skprx` under `*KERNEL` in your taiHEN config.

Startup log: `ur0:tai/psp2_gpuprof.log`.

## VitaSDK setup

With VitaSDK and taiHEN development libraries installed, run from this project:

```sh
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DCMAKE_INSTALL_PREFIX="$VITASDK/arm-vita-eabi" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --target psp2_gpuprof.skprx-self gpuprof_stubs psp2_gpuprof_client
cmake --install build
```
The install command installs the headers and libraries into VitaSDK.

Link your application's CMake target:

```cmake
target_link_libraries(your_app PRIVATE psp2_gpuprof_stub)
```

## Programming

```c
#include <psp2_gpuprof.h>
#include <psp2_gpuprof_presets.h>
```

Call from an ordinary application thread with GXM initialized and the plugin
loaded. Check availability with `psp2GpuProfGetInfo()`; both its return value
and `info.status` must be zero.

To capture counters:

1. Create a configuration with `psp2GpuProfOverviewConfig()`.
2. Call `psp2GpuProfBegin(&config, &before)` to select events and get a baseline.
3. Submit your GPU work and wait for completion with `sceGxmFinish(context)`.
4. Call `psp2GpuProfRead(&before.session, &after)`.
5. Call `psp2GpuProfEnd(&before.session)`, even if the read failed.

Check every return value. After a successful capture, subtract unsigned values:

```c
uint32_t delta = after.counters[core][slot] - before.counters[core][slot];
```

There are four cores and eight counter slots per core. Set
`config.events[slot].group` and `.bit` to choose a raw event. Counters are
GPU-wide, not per-process; raw counts are not utilization percentages.
Only one counter session can be active. Retain the handle and retry if `End`
returns busy; discard measurements invalidated by a reset or power transition.
Retail syscall errors may have bit 30 cleared, so log their hexadecimal values.

For signal snapshots, outside a counter session:

```c
Psp2GpuProfSignalConfig config = psp2GpuProfPipelineSignalConfig();
Psp2GpuProfSignals signals;
int rc = psp2GpuProfReadSignals(&config, &signals);
if (rc == 0) {
    uint32_t value = signals.values[0][0]; /* First group, first core. */
    /* Use value here. */
}
```

Signals are instantaneous values, not counter deltas.
See [the API header](include/psp2_gpuprof.h) and
[the rendering example](examples/isolation_probe.c) for the full capture flow.
