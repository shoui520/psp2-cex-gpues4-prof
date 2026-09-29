#include <psp2_gpuprof_work.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <vita2d.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

/* Standalone test: one immediate context, three render targets.
 * No sampling of an unfinished target, no vertex-pool reuse before Finish.
 * Scene completion notifications are observations, NOT GPU timestamps.
 */
#define FRAMES 6u
#define SCENES (FRAMES*3u)
#define CAPACITY 8192u
#define MAX_DRAWS 160u
typedef struct Draw { unsigned vs, fs, count, primitive; int rc; } Draw;
typedef struct Scene {
    unsigned frame, slot, pass, draws, flags;
    int begin_rc, end_rc, draw_error;
    uint64_t begin, submitted;
    Draw draw[MAX_DRAWS];
} Scene;
typedef struct Record {
    uint64_t before, after;
    uint32_t ta_before, fragment_before, ta_after, fragment_after;
    int rc;
    Psp2GpuProfWork work;
} Record;
static Scene scenes[SCENES];
static Record records[CAPACITY];
static unsigned used;
static atomic_uint stop;
static volatile unsigned int *notifications;
static Scene *current_scene;
static const void *programs[32];
static unsigned program_count, vs_id, fs_id;
static const char *pass_names[]={"small-geometry", "large-overdraw", "textured-overdraw"};

/* Observe actual GXM bindings/draw calls made by vita2d. Tokens are local to
 * this app run, not GPU addresses. No shaders are destroyed during capture. */
static unsigned program_id(const void *p)
{
    if(!p) return 0;
    for(unsigned i=0;i<program_count;++i) if(programs[i]==p) return i+1;
    if(program_count==32) { if(current_scene) current_scene->draw_error=-1; return 0; }
    programs[program_count++]=p;
    return program_count;
}
void __real_sceGxmSetVertexProgram(SceGxmContext *,const SceGxmVertexProgram *);
void __wrap_sceGxmSetVertexProgram(SceGxmContext *c,const SceGxmVertexProgram *p)
{
    vs_id=program_id(p); __real_sceGxmSetVertexProgram(c,p);
}
void __real_sceGxmSetFragmentProgram(SceGxmContext *,const SceGxmFragmentProgram *);
void __wrap_sceGxmSetFragmentProgram(SceGxmContext *c,const SceGxmFragmentProgram *p)
{
    fs_id=program_id(p); __real_sceGxmSetFragmentProgram(c,p);
}
int __real_sceGxmDraw(SceGxmContext *,SceGxmPrimitiveType,SceGxmIndexFormat,const void *,unsigned);
int __wrap_sceGxmDraw(SceGxmContext *c,SceGxmPrimitiveType p,SceGxmIndexFormat f,const void *d,unsigned n)
{
    int rc=__real_sceGxmDraw(c,p,f,d,n);
    if(current_scene) {
        if(current_scene->draws<MAX_DRAWS)
            current_scene->draw[current_scene->draws++]=(Draw){vs_id,fs_id,n,(unsigned)p,rc};
        else current_scene->draw_error=-1;
        if(rc) current_scene->draw_error=rc;
    }
    return rc;
}
static uint32_t completed(unsigned base)
{
    uint32_t mask=0;
    for(unsigned i=0;i<SCENES;++i) if(notifications[base+i]==i+1) mask|=1u<<i;
    return mask;
}
static int sampler(SceSize n,void *p)
{
    (void)n; (void)p;
    while(used<CAPACITY && !atomic_load(&stop)) {
        Record *r=&records[used++];
        r->before=sceKernelGetSystemTimeWide();
        r->ta_before=completed(0); r->fragment_before=completed(SCENES);
        r->rc=psp2GpuProfReadWork(&r->work);
        r->ta_after=completed(0); r->fragment_after=completed(SCENES);
        r->after=sceKernelGetSystemTimeWide();
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
    FILE *f=fopen("ux0:data/gpuprof-scenes.csv","w");
    vita2d_texture *targets[3]={0},*texture=NULL;
    SceGxmSyncObject *sync[3]={0};
    Psp2GpuProfInfo info;
    int rc=0,initialized=0;
    if(!f) return 1;
    fprintf(f,"probe,multiscene-v1\npid,%08x\n",(unsigned)sceKernelGetProcessId());
    rc=psp2GpuProfGetInfo(&info);
    if(rc) goto done;
    fprintf(f,"backend,%08x,%d,%08x\n",info.driver_fingerprint,info.status,info.capabilities);
    if(info.status || !(info.capabilities&PSP2_GPUPROF_CAP_WORK_OBSERVATIONS)) {
        rc=PSP2_GPUPROF_UNSUPPORTED; goto done;
    }
    if(vita2d_init_advanced(4*1024*1024)<=0) { rc=-1; goto done; }
    initialized=1;
    notifications=sceGxmGetNotificationRegion();
    if(!notifications) { rc=-1; goto done; }
    for(unsigned i=0;i<3;++i) {
        targets[i]=vita2d_create_empty_texture_rendertarget(960,544,SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR);
        if(!targets[i]) { rc=-1; goto done; }
        rc=sceGxmSyncObjectCreate(&sync[i]);
        if(rc) goto done;
    }
    texture=vita2d_create_empty_texture(16,16);
    if(!texture) { rc=-1; goto done; }
    unsigned char *pixels=vita2d_texture_get_datap(texture);
    unsigned stride=vita2d_texture_get_stride(texture);
    for(unsigned y=0;y<16;++y) for(unsigned x=0;x<16;++x)
        ((uint32_t *)(pixels+y*stride))[x]=((x^y)&1)?0x8080ffffu:0x80ff4080u;
    for(unsigned phase=0;phase<5;++phase) {
        unsigned submitted=0;
        uint64_t frame_end[FRAMES]={0};
        int heartbeat[FRAMES]={0};
        sceGxmFinish(vita2d_get_context());
        vita2d_pool_reset();
        memset(scenes,0,sizeof(scenes));
        for(unsigned i=0;i<2*SCENES;++i) notifications[i]=0;
        used=0; atomic_store(&stop,0);
        fprintf(f,"phase,%u,idle-serialized-queued-reversed-dependent\n",phase); fflush(f);
        SceUID thread=sceKernelCreateThread("gpuprof-scenes",sampler,0x10000100,0x10000,0,0,NULL);
        if(thread<0) { rc=thread; break; }
        rc=sceKernelStartThread(thread,0,NULL);
        if(rc<0) { sceKernelDeleteThread(thread); break; }
        if(!phase) sceKernelDelayThread(200000);
        else for(unsigned frame=0;frame<FRAMES && !rc;++frame) {
            for(unsigned slot=0;slot<3 && !rc;++slot) {
                unsigned index=frame*3+slot,pass=phase==3?2-slot:slot;
                Scene *s=&scenes[index];
                s->frame=frame; s->slot=slot; s->pass=pass;
                /* Explicit fragment->vertex chain protects render-to-texture
                 * reads AND reuse by the next frame, without CPU Finish. */
                if(phase==4) s->flags=SCE_GXM_SCENE_FRAGMENT_SET_DEPENDENCY |
                    (index?SCE_GXM_SCENE_VERTEX_WAIT_FOR_DEPENDENCY:0);
                s->begin=sceKernelGetSystemTimeWide();
                rc=sceGxmBeginScene(vita2d_get_context(),s->flags,targets[pass]->gxm_rtgt,NULL,
                                   NULL,sync[pass],&targets[pass]->gxm_sfc,&targets[pass]->gxm_sfd);
                s->begin_rc=rc;
                if(rc) { submitted=index+1; break; }
                current_scene=s;
                vita2d_clear_screen();
                for(unsigned draw=0;draw<(pass?96u:128u) && !s->draw_error;++draw) {
                    if(pass==2 && phase==4)
                        vita2d_draw_texture_scale(targets[1],0,0,900.0f/960,500.0f/544);
                    else if(pass==2) vita2d_draw_texture_scale(texture,0,0,56.25f,31.25f);
                    else vita2d_draw_rectangle((float)(draw%8)*2,0,pass?900:16,pass?500:16,
                                               0x80102030u+draw*37);
                }
                current_scene=NULL;
                SceGxmNotification ta={notifications+index,index+1};
                SceGxmNotification fragment={notifications+SCENES+index,index+1};
                s->end_rc=sceGxmEndScene(vita2d_get_context(),&ta,&fragment);
                s->submitted=sceKernelGetSystemTimeWide();
                submitted=index+1;
                rc=s->end_rc?s->end_rc:s->draw_error;
                if(!rc && s->draws<(pass?96u:128u)) rc=-1;
                if(phase==1) sceGxmFinish(vita2d_get_context());
            }
            if(!rc) {
                /* One explicit frame delimiter after three scenes. No display
                 * queue: avoid confusing swap pacing with GPU workload. */
                heartbeat[frame]=sceGxmPadHeartbeat(NULL,NULL);
                frame_end[frame]=sceKernelGetSystemTimeWide();
                rc=heartbeat[frame];
            }
        }
        sceGxmFinish(vita2d_get_context());
        uint32_t final_ta=completed(0),final_fragment=completed(SCENES);
        atomic_store(&stop,1);
        int join=sceKernelWaitThreadEnd(thread,NULL,NULL);
        if(join<0) { fprintf(f,"join-error,%08x\n",(unsigned)join); fclose(f); return 1; }
        sceKernelDeleteThread(thread);
        fprintf(f,"coverage,%u,%u,%u\n",phase,used,used==CAPACITY);
        fprintf(f,"completion,%u,%08x,%08x\n",phase,final_ta,final_fragment);
        for(unsigned i=0;i<submitted;++i) {
            Scene *s=&scenes[i];
            fprintf(f,"scene,%u,%u,%u,%u,%s,%u,%llu,%llu,%08x,%08x,%08x,%08x\n",
                    phase,i,s->frame,s->slot,pass_names[s->pass],s->draws,
                    (unsigned long long)s->begin,(unsigned long long)s->submitted,
                    (unsigned)s->begin_rc,(unsigned)s->end_rc,(unsigned)s->draw_error,s->flags);
            for(unsigned d=0;d<s->draws;++d) {
                Draw *v=&s->draw[d];
                fprintf(f,"binding,%u,%u,%u,%u,%u,%u,%u,%08x\n",phase,i,d,
                        v->vs,v->fs,v->count,v->primitive,(unsigned)v->rc);
            }
        }
        for(unsigned i=0;i<FRAMES;++i) if(frame_end[i])
            fprintf(f,"frame-end,%u,%u,%llu,%08x\n",phase,i,(unsigned long long)frame_end[i],(unsigned)heartbeat[i]);
        for(unsigned i=0;i<used;++i) {
            Record *r=&records[i];
            fprintf(f,"call,%u,%u,%llu,%llu,%08x,%08x,%08x,%08x,%08x\n",phase,i,
                    (unsigned long long)r->before,(unsigned long long)r->after,(unsigned)r->rc,
                    r->ta_before,r->fragment_before,r->ta_after,r->fragment_after);
            if(r->rc) continue;
            for(unsigned c=0;c<4;++c) {
                Psp2GpuProfWorkCore *v=&r->work.cores[c];
                fprintf(f,"work,%u,%u,%u,%u,%u,%08x,%08x",phase,i,c,
                        v->timer_before,v->timer_after,v->fragment_signal,v->flags);
                fields(f,&v->before); fields(f,&v->after); fputc('\n',f);
            }
        }
        if(!rc && phase && (final_ta!=((1u<<SCENES)-1) || final_fragment!=((1u<<SCENES)-1))) rc=-1;
        if(rc) break;
    }
done:
    if(initialized) {
        sceGxmFinish(vita2d_get_context());
        if(texture) vita2d_free_texture(texture);
        for(unsigned i=0;i<3;++i) {
            if(sync[i]) sceGxmSyncObjectDestroy(sync[i]);
            if(targets[i]) vita2d_free_texture(targets[i]);
        }
        vita2d_fini();
    }
    fprintf(f,"result,%08x\n",(unsigned)rc);
    if(ferror(f)) rc=-1;
    if(fclose(f)) rc=-1;
    return rc?1:0;
}
