#ifndef PSP2_GPUPROF_CAPTURE_H
#define PSP2_GPUPROF_CAPTURE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Versioned event transport shared by graphics adapters and hardware samplers.
 * IDs are capture-client identities, not pointers or inferred firmware IDs.
 * Retired context/shader identities must never be reused in a session. */
#define PSP2_GPUPROF_CAPTURE_VERSION 1u
#define PSP2_GPUPROF_CAPTURE_LABEL_BYTES 96u

typedef enum Psp2GpuProfEventKind {
    PSP2_GPUPROF_EVENT_CONTEXT_CREATE = 1,
    PSP2_GPUPROF_EVENT_CONTEXT_DESTROY,
    PSP2_GPUPROF_EVENT_SHADER_CREATE,
    PSP2_GPUPROF_EVENT_SHADER_DESTROY,
    PSP2_GPUPROF_EVENT_SHADER_BIND,
    PSP2_GPUPROF_EVENT_FRAME,
    PSP2_GPUPROF_EVENT_SCOPE_PUSH,
    PSP2_GPUPROF_EVENT_SCOPE_POP,
    PSP2_GPUPROF_EVENT_SCENE_BEGIN,
    PSP2_GPUPROF_EVENT_SCENE_END,
    PSP2_GPUPROF_EVENT_DRAW,
    PSP2_GPUPROF_EVENT_FLUSH,
    PSP2_GPUPROF_EVENT_TRANSFER,
    PSP2_GPUPROF_EVENT_PRESENT,
    PSP2_GPUPROF_EVENT_COMPLETION,
    PSP2_GPUPROF_EVENT_COVERAGE_GAP,
    PSP2_GPUPROF_EVENT_GXM_CALL,
    PSP2_GPUPROF_EVENT_SHADER_NAME,
    PSP2_GPUPROF_EVENT_DRAW_NAME,
    PSP2_GPUPROF_EVENT_VITAGL_CALL,
    PSP2_GPUPROF_EVENT_DIAGNOSTIC,
    PSP2_GPUPROF_EVENT_DIAGNOSTIC_CORE,
    PSP2_GPUPROF_EVENT_GXM_SNAPSHOT,
    PSP2_GPUPROF_EVENT_GXM_IDENTITY_SEED
} Psp2GpuProfEventKind;

typedef struct Psp2GpuProfCaptureEvent {
    uint64_t sequence;
    uint64_t before_us, after_us;
    uint64_t thread, context, frame, scene, scope;
    uint64_t object; /* Shader generation, draw ordinal, or operation ID. */
    uint64_t args[8]; /* Kind-specific payload. */
    uint32_t kind, flags;
    int32_t result;
    char label[PSP2_GPUPROF_CAPTURE_LABEL_BYTES];
} Psp2GpuProfCaptureEvent;

enum {
    PSP2_GPUPROF_CAPTURE_OK = 0,
    PSP2_GPUPROF_CAPTURE_INVALID = -1,
    PSP2_GPUPROF_CAPTURE_STATE = -2,
    PSP2_GPUPROF_CAPTURE_FULL = -3,
    PSP2_GPUPROF_CAPTURE_IO = -4,
    PSP2_GPUPROF_EVENT_LABEL_TRUNCATED = 1u
};

/* Supply a paired mutex lock/unlock, or NULL/NULL for single-threaded use.
 * Calls must not reenter this recorder. No graphics call runs under this lock.
 * Storage and mutex must outlive all producers. Init requires exclusive access.
 * Stop freezes storage; late events are rejected. Export requires exclusive
 * lifecycle ownership (no concurrent Init/Start/Export). */
typedef struct Psp2GpuProfCaptureConfig {
    Psp2GpuProfCaptureEvent *events;
    size_t capacity;
    void (*lock)(void *);
    void (*unlock)(void *);
    void *lock_userdata;
} Psp2GpuProfCaptureConfig;

typedef struct Psp2GpuProfCapture {
    Psp2GpuProfCaptureConfig config;
    size_t count;
    uint64_t dropped;
    uint64_t next_sequence;
    unsigned state;
} Psp2GpuProfCapture;

int psp2GpuProfCaptureInit(Psp2GpuProfCapture *, const Psp2GpuProfCaptureConfig *);
int psp2GpuProfCaptureStart(Psp2GpuProfCapture *);
/* Opt-in retail 3.65 identity model, not a timing measurement. Call once before
 * the first observed GXM operation, with known counters (fresh GXM: 0,0).
 * Caller guarantees complete immediate-mode interception throughout capture,
 * no GXM reinitialization and no hidden/deferred submissions. Do not guess a
 * seed for an already-running renderer. The reader rejects loss, unsupported
 * submission paths, overlapping scene lifetimes and counter wrap. */
int psp2GpuProfCaptureGxmIdentitySeed365(Psp2GpuProfCapture *, uint32_t pid,
    uint32_t frame, uint32_t scene);
int psp2GpuProfCaptureRecord(Psp2GpuProfCapture *,
                            const Psp2GpuProfCaptureEvent *, const char *label);
/* All-or-none insertion, one lock, consecutive sequence numbers. Labels in
 * each input event must be NUL-terminated. Overflow counts every lost row.
 * Input storage must not overlap the capture's output storage. */
int psp2GpuProfCaptureRecordBatch(Psp2GpuProfCapture *,
                                 const Psp2GpuProfCaptureEvent *, size_t count);
int psp2GpuProfCaptureStop(Psp2GpuProfCapture *);
/* Writes to the caller's already-open stream; never opens or closes files.
 * No export while recording. Returns IO on write/flush failure. */
int psp2GpuProfCaptureWriteCsv(Psp2GpuProfCapture *, FILE *);

#ifdef __cplusplus
}
#endif
#endif
