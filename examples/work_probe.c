#include <psp2_gpuprof_work.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>
#include <stdatomic.h>
#include <stdio.h>

#define CAPACITY 4096u
typedef struct Record {
    uint64_t begin, end;
    int rc;
    Psp2GpuProfWork work;
} Record;
static Record records[CAPACITY];
static unsigned used;
static atomic_uint stop;
static int sampler(SceSize n, void *p)
{
    (void)n; (void)p;
    while(used<CAPACITY && !atomic_load(&stop)) {
        Record *r=&records[used++];
        r->begin=sceKernelGetSystemTimeWide();
        r->rc=psp2GpuProfReadWork(&r->work);
        r->end=sceKernelGetSystemTimeWide();
        sceKernelDelayThread(200);
    }
    return 0;
}
static void fields(FILE *f,const Psp2GpuProfWorkState *s)
{
    fprintf(f,",%08x,%u,%u,%08x,%u,%u",s->ta_pid,s->ta_frame,s->ta_scene,
            s->render_pid,s->render_frame,s->render_scene);
}
int main(void)
{
    FILE *f=fopen("ux0:data/gpuprof-work.csv","w");
    Psp2GpuProfInfo info;
    uint64_t submits[16][2];
    int initialized=0,rc;
    if(!f) return 1;
    fprintf(f,"probe,work-observations-v1\npid,%08x\n",(unsigned)sceKernelGetProcessId());
    rc=psp2GpuProfGetInfo(&info);
    if(rc) goto done;
    fprintf(f,"backend,%08x,%d,%08x\n",info.driver_fingerprint,info.status,info.capabilities);
    if(info.status || !(info.capabilities&PSP2_GPUPROF_CAP_WORK_OBSERVATIONS)) {
        rc=PSP2_GPUPROF_UNSUPPORTED; goto done;
    }
    if(vita2d_init()<=0) { rc=-1; goto done; }
    initialized=1;
    for(unsigned phase=0;phase<3;++phase) {
        /* Phase 1 drains each scene. Phase 2 omits that explicit drain, so
         * CPU submission windows must NOT be interpreted as GPU ownership.
         * Swap/display queue may still introduce normal backpressure. */
        vita2d_wait_rendering_done();
        used=0; atomic_store(&stop,0);
        fprintf(f,"phase,%u,idle-serialized-queued\n",phase); fflush(f);
        SceUID thread=sceKernelCreateThread("gpuprof-work",sampler,0x10000100,0x10000,0,0,NULL);
        if(thread<0) { rc=thread; break; }
        rc=sceKernelStartThread(thread,0,NULL);
        if(rc<0) { sceKernelDeleteThread(thread); break; }
        if(!phase) sceKernelDelayThread(200000);
        else for(unsigned scene=0;scene<16;++scene) {
            submits[scene][0]=sceKernelGetSystemTimeWide();
            vita2d_start_drawing(); vita2d_clear_screen();
            for(unsigned draw=0;draw<128;++draw)
                vita2d_draw_rectangle(0,0,scene&1?900:32,scene&1?500:32,
                                     0x80102030u+draw*37);
            vita2d_end_drawing();
            submits[scene][1]=sceKernelGetSystemTimeWide();
            if(phase==1) vita2d_wait_rendering_done();
            vita2d_swap_buffers();
        }
        vita2d_wait_rendering_done();
        atomic_store(&stop,1);
        rc=sceKernelWaitThreadEnd(thread,NULL,NULL);
        if(rc<0) { fprintf(f,"join-error,%08x\n",(unsigned)rc); fclose(f); return 1; }
        sceKernelDeleteThread(thread);
        fprintf(f,"coverage,%u,%u,%u\n",phase,used,used==CAPACITY);
        if(phase) for(unsigned s=0;s<16;++s)
            fprintf(f,"submit,%u,%u,%s,%llu,%llu\n",phase,s,s&1?"large":"small",
                    (unsigned long long)submits[s][0],(unsigned long long)submits[s][1]);
        for(unsigned i=0;i<used;++i) {
            Record *r=&records[i];
            fprintf(f,"call,%u,%u,%llu,%llu,%08x\n",phase,i,
                    (unsigned long long)r->begin,(unsigned long long)r->end,(unsigned)r->rc);
            if(r->rc) continue;
            for(unsigned c=0;c<4;++c) {
                Psp2GpuProfWorkCore *v=&r->work.cores[c];
                fprintf(f,"work,%u,%u,%u,%u,%u,%08x,%08x",phase,i,c,
                        v->timer_before,v->timer_after,v->fragment_signal,v->flags);
                fields(f,&v->before); fields(f,&v->after); fputc('\n',f);
            }
        }
    }
done:
    if(initialized) { vita2d_wait_rendering_done(); vita2d_fini(); }
    fprintf(f,"result,%08x\n",(unsigned)rc);
    if(ferror(f)) rc=-1;
    if(fclose(f)) rc=-1;
    return rc?1:0;
}
