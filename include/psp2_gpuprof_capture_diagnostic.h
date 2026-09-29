#ifndef PSP2_GPUPROF_CAPTURE_DIAGNOSTIC_H
#define PSP2_GPUPROF_CAPTURE_DIAGNOSTIC_H
#include "psp2_gpuprof_capture.h"
#include "psp2_gpuprof_diagnostic.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct Psp2GpuProfDiagnosticObservation {
    uint64_t sampler_thread, sample_id;
    uint64_t before_us, after_us;
    uint32_t group, tag_group;
    uint32_t clock_before_mhz, clock_after_mhz;
    uint32_t process_id, driver_fingerprint;
    int32_t result;
} Psp2GpuProfDiagnosticObservation;
/* Serializes a completed read. Does not access the GPU or start threads.
 * sample_id must be unique on this sampler thread for the capture duration.
 * Success emits 13 rows atomically, failure one row (sample may be NULL).
 * Clock values are observations, not a guaranteed clock during the read. */
int psp2GpuProfCaptureDiagnostic(Psp2GpuProfCapture *,
    const Psp2GpuProfDiagnosticObservation *, const Psp2GpuProfDiagnostic *);
#ifdef __cplusplus
}
#endif
#endif
