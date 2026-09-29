#include "psp2_gpuprof_capture.h"
#include <pthread.h>
#include <assert.h>

enum { THREADS=4, BATCHES=1000, ROWS=3, CAPACITY=7000 };
static Psp2GpuProfCaptureEvent storage[CAPACITY];
static Psp2GpuProfCapture capture;
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static void acquire(void *p) { assert(!pthread_mutex_lock(p)); }
static void release(void *p) { assert(!pthread_mutex_unlock(p)); }
static void *produce(void *p) {
    uint64_t id=(uintptr_t)p;
    for (unsigned n=0;n<BATCHES;++n) {
        Psp2GpuProfCaptureEvent rows[ROWS]={{0}};
        for (unsigned j=0;j<ROWS;++j) {
            rows[j].kind=PSP2_GPUPROF_EVENT_DRAW;
            rows[j].thread=id; rows[j].object=n; rows[j].args[0]=j;
        }
        int rc=psp2GpuProfCaptureRecordBatch(&capture,rows,ROWS);
        assert(rc==0 || rc==PSP2_GPUPROF_CAPTURE_FULL);
    }
    return NULL;
}
static void *late_producer(void *unused) {
    (void)unused;
    Psp2GpuProfCaptureEvent row={.kind=PSP2_GPUPROF_EVENT_GXM_CALL};
    for (unsigned i=0;i<10000;++i)
        assert(psp2GpuProfCaptureRecord(&capture,&row,NULL)==PSP2_GPUPROF_CAPTURE_STATE);
    return NULL;
}
int main(void) {
    Psp2GpuProfCaptureConfig cfg={storage,CAPACITY,acquire,release,&mutex};
    assert(!psp2GpuProfCaptureInit(&capture,&cfg));
    assert(!psp2GpuProfCaptureStart(&capture));
    pthread_t workers[THREADS];
    for (uintptr_t i=0;i<THREADS;++i)
        assert(!pthread_create(&workers[i],NULL,produce,(void *)(i+1)));
    for (unsigned i=0;i<THREADS;++i) assert(!pthread_join(workers[i],NULL));
    assert(!psp2GpuProfCaptureStop(&capture));
    assert(capture.count==CAPACITY-CAPACITY%ROWS);
    assert(capture.count+capture.dropped==THREADS*BATCHES*ROWS);
    for (size_t i=0;i<capture.count;++i) {
        assert(storage[i].sequence==i);
        assert(storage[i].args[0]==i%ROWS);
        assert(storage[i].thread==storage[i-i%ROWS].thread);
        assert(storage[i].object==storage[i-i%ROWS].object);
    }
    /* A process-lifetime recorder can be sealed without detaching callbacks.
       Rejected late records must not mutate the snapshot during export. */
    pthread_t late;
    assert(!pthread_create(&late,NULL,late_producer,NULL));
    FILE *file=tmpfile();
    assert(file);
    assert(!psp2GpuProfCaptureWriteCsv(&capture,file));
    assert(!pthread_join(late,NULL));
    assert(!fclose(file));
    assert(capture.count==CAPACITY-CAPACITY%ROWS);
    assert(capture.count+capture.dropped==THREADS*BATCHES*ROWS);
    return 0;
}
