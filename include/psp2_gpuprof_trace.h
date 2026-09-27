#ifndef PSP2_GPUPROF_TRACE_H
#define PSP2_GPUPROF_TRACE_H
#include "psp2_gpuprof.h"
#ifdef __cplusplus
extern "C" {
#endif

enum Psp2GpuProfRecordKind {
    PSP2_GPUPROF_FRAME_BEGIN = 1, PSP2_GPUPROF_FRAME_END,
    PSP2_GPUPROF_SCENE_BEGIN, PSP2_GPUPROF_SCENE_END,
    PSP2_GPUPROF_USER_BEGIN, PSP2_GPUPROF_USER_END,
    PSP2_GPUPROF_COUNTER_SAMPLE, PSP2_GPUPROF_SIGNAL_SAMPLE,
    PSP2_GPUPROF_API_ERROR
};

/* CPU timestamps supplied by the application, e.g. GetProcessTimeWide around
 * a sample API call. Marker before/after should be equal. Never GPU execution
 * boundaries. IDs are application-defined; thread_id is informational only.
 * sequence is assigned by Append and includes records dropped due to overflow.
 */
typedef struct Psp2GpuProfRecord {
    uint64_t sequence, cpu_before_us, cpu_after_us;
    uint32_t kind, id, thread_id;
    union {
        Psp2GpuProfSample counters;
        Psp2GpuProfSignals signals;
        int32_t error;
    } data;
} Psp2GpuProfRecord;

/* Caller-owned fixed storage. All operations must be serialized by the caller
 * (including a producer/consumer on separate threads). No allocation, syscalls,
 * worker thread, or file I/O. Do not modify fields after initialization.
 * This is an in-memory C interface, NOT a stable file/wire serialization format.
 */
typedef struct Psp2GpuProfTrace {
    Psp2GpuProfRecord *storage;
    uint32_t capacity, head, count;
    uint64_t next_sequence, dropped;
} Psp2GpuProfTrace;

int psp2GpuProfTraceInit(Psp2GpuProfTrace *trace, Psp2GpuProfRecord *storage,
                       uint32_t capacity);
/* Full buffer: drops the incoming record, increments dropped, returns BUSY.
 * Invalid records are rejected without consuming sequence numbers.
 * Input record must not overlap trace storage or trace metadata.
 */
int psp2GpuProfTraceAppend(Psp2GpuProfTrace *trace, const Psp2GpuProfRecord *record);
/* Returns 1 for a record, 0 if empty, or INVALID. Output must not overlap trace
 * storage/metadata. Read does not clear the cumulative dropped count.
 */
int psp2GpuProfTraceRead(Psp2GpuProfTrace *trace, Psp2GpuProfRecord *record);

#ifdef __cplusplus
}
#endif
#endif
