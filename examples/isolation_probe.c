#include <psp2_gpuprof.h>
#include <psp2_gpuprof_presets.h>
#include <vita2d.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <stdio.h>
#include <string.h>

/* Compile as two apps: no signal calls in the counter executable and no
 * counter-session calls in the signal executable. No kernel changes needed.
 * vita2d rectangle draws submit indexed GXM primitives using color shaders.
 */
static void frame(unsigned n)
{
    vita2d_start_drawing();
    vita2d_clear_screen();
    for (unsigned i=0; i<64; ++i)
        vita2d_draw_rectangle((float)((i*97+n*7)%800), (float)((i*53+n*3)%420),
                             160, 120, 0xff000000u | ((i*3917+n*179)&0xffffffu));
    vita2d_end_drawing();
    vita2d_wait_rendering_done(); /* sceGxmFinish: don't sample queued work. */
    vita2d_swap_buffers();
}

#ifndef PROBE_SIGNALS
static int work(unsigned kind, unsigned count, void *memory)
{
    if (kind == 0) return sceKernelDelayThread(10000);
    for (unsigned n=0;n<count;++n) {
        if (kind == 2) frame(n);
        else {
            int rc=sceGxmTransferFill(0xff112233, SCE_GXM_TRANSFER_FORMAT_U8U8U8U8_ABGR,
                                     memory,0,0,1024,512,4096,NULL,0,NULL);
            if (rc) return rc;
            rc=sceGxmTransferFinish();
            if (rc) return rc;
        }
    }
    return 0;
}

static void sample(FILE *f, unsigned run, const char *phase, const Psp2GpuProfSample *s)
{
    for (unsigned c=0;c<4;++c) {
        fprintf(f,"sample,%u,%s,%08x,%08x,%u,%u,%u,%u",run,phase,
                s->session.hi,s->session.lo,s->sequence,s->gpu_timer_before,s->gpu_timer_after,c);
        for (unsigned n=0;n<8;++n) fprintf(f,",%u",s->counters[c][n]);
        fputc('\n',f);
    }
}

static int counters(FILE *f, void *memory)
{
    unsigned run=0;
    for (unsigned preset=0;preset<2;++preset) {
        Psp2GpuProfConfig cfg=psp2GpuProfOverviewConfig();
        if (!preset) for (unsigned n=0;n<8;++n) {
            cfg.events[n].group=0; cfg.events[n].bit=n&1;
        }
        for (unsigned n=0;n<8;++n)
            fprintf(f,"selector,%u,%u,%u,%u\n",preset,n,cfg.events[n].group,cfg.events[n].bit);
        for (unsigned kind=0;kind<3;++kind) for (unsigned size=0;size<3;++size,++run) {
            static const unsigned counts[]={1,8,32};
            Psp2GpuProfSample before,after;
            int rc,read_rc,end_rc;
            frame(run); /* Warm-up outside lease, even for idle/transfer. */
            fprintf(f,"window,%u,preset,%u,work,%u,count,%u\n",run,preset,kind,counts[size]);
            fprintf(f,"call,%u,Begin-work-Read-End\n",run);
            rc=psp2GpuProfBegin(&cfg,&before);
            if (rc) { fprintf(f,"begin,%u,%08x\n",run,(unsigned)rc); return rc; }
            /* No file I/O within the lease. */
            rc=work(kind,counts[size],memory);
            read_rc=psp2GpuProfRead(&before.session,&after);
            end_rc=psp2GpuProfEnd(&before.session);
            for (unsigned retry=0; (end_rc==PSP2_GPUPROF_BUSY || (uint32_t)end_rc==0xbffffffeu)
                                  && retry<20; ++retry) {
                sceKernelDelayThread(1000);
                end_rc=psp2GpuProfEnd(&before.session);
            }
            fprintf(f,"capture,%u,%08x,%08x,%08x\n",run,(unsigned)rc,(unsigned)read_rc,(unsigned)end_rc);
            sample(f,run,"before",&before);
            if (!read_rc) {
                sample(f,run,"after",&after);
                for (unsigned c=0;c<4;++c) {
                    fprintf(f,"delta,%u,%u",run,c);
                    for (unsigned n=0;n<8;++n)
                        fprintf(f,",%u",after.counters[c][n]-before.counters[c][n]);
                    fputc('\n',f);
                }
            }
            if (rc || read_rc || end_rc) return rc ? rc : read_rc ? read_rc : end_rc;
            if (kind==1) {
                unsigned mismatch=0;
                const volatile uint32_t *pixels=memory;
                for (unsigned n=0;n<1024*512;++n) mismatch+=pixels[n]!=0xff112233;
                fprintf(f,"verify-transfer,%u,%u\n",run,mismatch);
                if (mismatch) return -1;
            }
        }
    }
    return 0;
}
#else
static int signals(FILE *f)
{
    Psp2GpuProfSignalConfig cfg=psp2GpuProfPipelineSignalConfig();
    for (unsigned run=0;run<8;++run) {
        Psp2GpuProfSignals s;
        frame(run);
        fprintf(f,"call,%u,ReadSignals-after-render\n",run);
        int rc=psp2GpuProfReadSignals(&cfg,&s);
        fprintf(f,"signals-result,%u,%08x\n",run,(unsigned)rc);
        if (rc) return rc;
        if (s.group_count!=cfg.group_count) return -1;
        for (unsigned g=0;g<s.group_count;++g) {
            fprintf(f,"signals,%u,%u,%u,%u",run,s.groups[g],s.timer_before[g],s.timer_after[g]);
            for (unsigned c=0;c<4;++c) fprintf(f,",%08x",s.values[g][c]);
            fputc('\n',f);
        }
    }
    return 0;
}
#endif

int main(void)
{
#ifdef PROBE_SIGNALS
    FILE *f=fopen("ux0:data/gpuprof-signals-only.csv","a");
#else
    FILE *f=fopen("ux0:data/gpuprof-counters-only.csv","a");
    SceUID block=-1;
    void *memory=NULL;
    int mapped=0;
#endif
    Psp2GpuProfInfo info;
    int rc,initialized=0;
    if (!f) return 1;
    setvbuf(f,NULL,_IONBF,0);
    fprintf(f,"probe,isolation-v1\ncall,GetInfo\n");
    rc=psp2GpuProfGetInfo(&info);
    fprintf(f,"info,%08x\n",(unsigned)rc);
    if (rc) goto done;
    fprintf(f,"backend,%d,%08x,%u\n",info.status,info.driver_fingerprint,info.abi);
    if (info.status || info.abi!=PSP2_GPUPROF_ABI) { rc=-1; goto done; }
    fprintf(f,"call,vita2d_init\n");
    rc=vita2d_init();
    fprintf(f,"vita2d_init,%d\n",rc);
    if (rc!=1) { rc=-1; goto done; }
    initialized=1;
    vita2d_set_clear_color(0xff302010);
#ifdef PROBE_SIGNALS
    rc=signals(f);
#else
    block=sceKernelAllocMemBlock("gpuprof-isolation",SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,1024*512*4,NULL);
    if (block<0) { rc=block; goto done; }
    rc=sceKernelGetMemBlockBase(block,&memory);
    if (rc) goto done;
    rc=sceGxmMapMemory(memory,1024*512*4,SCE_GXM_MEMORY_ATTRIB_RW);
    if (rc) goto done;
    mapped=1;
    rc=counters(f,memory);
#endif
done:
    if (initialized) vita2d_wait_rendering_done();
#ifndef PROBE_SIGNALS
    if (mapped) {
        int finish=sceGxmTransferFinish();
        fprintf(f,"transfer-finish,%08x\n",(unsigned)finish);
        if (!finish) {
            int unmap=sceGxmUnmapMemory(memory);
            fprintf(f,"unmap,%08x\n",(unsigned)unmap);
            if (!unmap) mapped=0;
            if (!rc) rc=unmap;
        } else if (!rc) rc=finish;
    }
    if (block>=0 && !mapped) {
        int freed=sceKernelFreeMemBlock(block);
        fprintf(f,"free,%08x\n",(unsigned)freed);
        if (!rc) rc=freed;
    }
    /* On unsafe transfer cleanup let process teardown reclaim resources. */
    if (mapped) initialized=0;
#endif
    if (initialized) { int fini=vita2d_fini(); fprintf(f,"vita2d_fini,%d\n",fini); }
    fprintf(f,"result,%08x\n",(unsigned)rc);
    if (ferror(f)) rc=-1;
    if (fclose(f)) rc=-1;
    return rc ? 1 : 0;
}
