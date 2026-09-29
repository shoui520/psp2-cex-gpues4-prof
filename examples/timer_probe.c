#include <psp2_gpuprof_work.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <vita2d.h>
#include <stdatomic.h>
#include <stdio.h>

/* No clock changes, no kernel changes. CPU times bound each timer read.
 * No log I/O while sampling. Delays are requests, never elapsed-time inputs. */
#define CAPACITY 2048u
typedef struct Record {
    uint64_t before,after;
    int gpu_before,gpu_after,xbar_before,xbar_after,rc;
    uint32_t timer_before,timer_after,active_mask,delay;
} Record;
static Record records[CAPACITY];
static unsigned used;
static atomic_uint stop;
static int sampler(SceSize n,void *p)
{
    static const unsigned delays[]={1000,5000,10000};
    (void)n; (void)p;
    while(used<CAPACITY && !atomic_load(&stop)) {
        Record *r=&records[used];
        Psp2GpuProfWork w;
        *r=(Record){0};
        r->delay=delays[used%3];
        r->gpu_before=scePowerGetGpuClockFrequency();
        r->xbar_before=scePowerGetGpuXbarClockFrequency();
        r->before=sceKernelGetSystemTimeWide();
        r->rc=psp2GpuProfReadWork(&w);
        r->after=sceKernelGetSystemTimeWide();
        r->gpu_after=scePowerGetGpuClockFrequency();
        r->xbar_after=scePowerGetGpuXbarClockFrequency();
        if(!r->rc) {
            r->timer_before=w.cores[0].timer_before;
            r->timer_after=w.cores[0].timer_after;
            for(unsigned c=0;c<4;++c)
                if(w.cores[c].fragment_signal&(1u<<29)) r->active_mask|=1u<<c;
        }
        ++used;
        sceKernelDelayThread(r->delay);
    }
    return 0;
}
int main(void)
{
    FILE *f=fopen("ux0:data/gpuprof-timer.csv","w");
    Psp2GpuProfInfo info;
    int rc=0,initialized=0;
    if(!f) return 1;
    fprintf(f,"probe,timer-calibration-v1\n");
    rc=psp2GpuProfGetInfo(&info);
    if(rc) goto done;
    fprintf(f,"backend,%08x,%d,%08x\n",info.driver_fingerprint,info.status,info.capabilities);
    if(info.status || !(info.capabilities&PSP2_GPUPROF_CAP_WORK_OBSERVATIONS)) {
        rc=PSP2_GPUPROF_UNSUPPORTED; goto done;
    }
    if(vita2d_init()<=0) { rc=-1; goto done; }
    initialized=1;
    vita2d_set_vblank_wait(0);
    for(unsigned phase=0;phase<3;++phase) {
        vita2d_wait_rendering_done();
        used=0; atomic_store(&stop,0);
        fprintf(f,"phase,%u,idle-heavy-bursty\n",phase); fflush(f);
        SceUID thread=sceKernelCreateThread("gpuprof-timer",sampler,0x10000100,0x10000,0,0,NULL);
        if(thread<0) { rc=thread; break; }
        rc=sceKernelStartThread(thread,0,NULL);
        if(rc<0) { sceKernelDeleteThread(thread); break; }
        uint64_t start=sceKernelGetSystemTimeWide();
        if(!phase) sceKernelDelayThread(3000000);
        else while(sceKernelGetSystemTimeWide()-start<3000000) {
            vita2d_start_drawing(); vita2d_clear_screen();
            for(unsigned d=0;d<96;++d)
                vita2d_draw_rectangle(0,0,900,500,0x80102030u+d*37);
            vita2d_end_drawing();
            vita2d_wait_rendering_done(); vita2d_swap_buffers();
            if(phase==2) sceKernelDelayThread(100000);
        }
        atomic_store(&stop,1);
        rc=sceKernelWaitThreadEnd(thread,NULL,NULL);
        if(rc<0) { fprintf(f,"join-error,%08x\n",(unsigned)rc); fclose(f); return 1; }
        sceKernelDeleteThread(thread);
        fprintf(f,"coverage,%u,%u,%u\n",phase,used,used==CAPACITY);
        for(unsigned i=0;i<used;++i) {
            Record *r=&records[i];
            fprintf(f,"sample,%u,%u,%llu,%llu,%08x,%u,%u,%d,%d,%d,%d,%u,%u\n",
                    phase,i,(unsigned long long)r->before,(unsigned long long)r->after,
                    (unsigned)r->rc,r->timer_before,r->timer_after,r->gpu_before,
                    r->gpu_after,r->xbar_before,r->xbar_after,r->active_mask,r->delay);
        }
    }
done:
    if(initialized) { vita2d_wait_rendering_done(); vita2d_fini(); }
    fprintf(f,"result,%08x\n",(unsigned)rc);
    if(ferror(f)) rc=-1;
    if(fclose(f)) rc=-1;
    return rc?1:0;
}
