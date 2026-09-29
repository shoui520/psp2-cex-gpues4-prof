/* Standalone application using the public recorder, with no GPU Lab code.
 * The fixed-function triangle exercises vitaGL -> GXM recording. It is not
 * a performance benchmark and does not collect hardware samples. */
#include <vitaGL.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2_gpuprof_capture.h>
#include <psp2_gpuprof_gxm_observer.h>
#include <psp2_gpuprof_labels.h>
#include <psp2_gpuprof_vitagl.h>
#include <stdio.h>
#include <stdlib.h>

static Psp2GpuProfCaptureEvent events[16384];
static Psp2GpuProfCapture capture;
static SceUID mutex;
static void lock(void *unused) {
    (void)unused;
    if (sceKernelLockMutex(mutex,1,NULL)<0) abort();
}
static void unlock(void *unused) {
    (void)unused;
    if (sceKernelUnlockMutex(mutex,1)<0) abort();
}
static void status(const char *message, int code) {
    FILE *file=fopen("ux0:data/gpuprof-example/status.txt","w");
    if (file) {
        fprintf(file,"%s: %d\n",message,code);
        fclose(file);
    }
}
int main(void) {
    sceIoMkdir("ux0:data/gpuprof-example",0777);
    status("starting",0);
    mutex=sceKernelCreateMutex("capture-example",0,0,NULL);
    if (mutex<0) { status("mutex failed",mutex); return 1; }
    Psp2GpuProfCaptureConfig cfg={events,16384,lock,unlock,NULL};
    int rc=psp2GpuProfCaptureInit(&capture,&cfg);
    if (!rc) rc=psp2GpuProfCaptureStart(&capture);
    if (rc) { status("capture initialization failed",rc); return 1; }
    psp2GpuProfGxmSetObserver(psp2GpuProfCaptureGxmCall,&capture);
    psp2GpuProfVitaGLCapture(&capture);
    /* vitaGL returns a resolution-adjustment flag, not a success boolean.
     * Zero is the normal return for the requested resolution. */
    GLboolean resolution_adjusted=vglInit(1024*1024);
    status("vitaGL init returned (resolution adjustment flag)",resolution_adjusted);
    Psp2GpuProfLabels labels;
    psp2GpuProfLabelsBegin(&labels,&capture,(uint32_t)sceKernelGetThreadId());
    static const GLfloat vertices[]={-0.7f,-0.6f, 0.7f,-0.6f, 0.0f,0.7f};
    glViewport(0,0,960,544);
    glDisable(GL_DEPTH_TEST);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2,GL_FLOAT,0,vertices);
    for (unsigned frame=0;frame<30;++frame) {
        psp2GpuProfFrameName(&labels,sceKernelGetSystemTimeWide(),frame,"Example frame");
        int scope=psp2GpuProfScopePush(&labels,sceKernelGetSystemTimeWide(),"Main pass");
        psp2GpuProfDrawName(&labels,sceKernelGetSystemTimeWide(),"Clear");
        glClearColor(0.03f,0.05f,0.10f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        psp2GpuProfDrawName(&labels,sceKernelGetSystemTimeWide(),"Triangle");
        glColor4f(0.2f,0.8f,0.9f,1.0f);
        glDrawArrays(GL_TRIANGLES,0,3);
        psp2GpuProfDrawName(&labels,sceKernelGetSystemTimeWide(),NULL);
        if (!scope) psp2GpuProfScopePop(&labels,sceKernelGetSystemTimeWide());
        vglSwapBuffers(GL_FALSE);
    }
    /* Stop locks the recorder and seals its retained rows. Any later worker
     * callbacks are rejected. Never detach or destroy their static storage.
     * No capture restart occurs; process exit eventually reclaims everything. */
    rc=psp2GpuProfCaptureStop(&capture);
    FILE *file=fopen("ux0:data/gpuprof-example/capture.csv","w");
    if (!file) rc=PSP2_GPUPROF_CAPTURE_IO;
    if (!rc) rc=psp2GpuProfCaptureWriteCsv(&capture,file);
    if (file && fclose(file)) rc=PSP2_GPUPROF_CAPTURE_IO;
    status(rc ? "capture export failed" : "capture saved",rc);
    sceKernelDelayThread(1000000);
    sceKernelExitProcess(rc ? 1 : 0);
    return 0;
}
