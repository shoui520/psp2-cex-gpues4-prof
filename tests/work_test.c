#include "work.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t regs[0x20000/4];
static unsigned held, reads, writes, states, fail_state, changed;
static int acquire_failure;
static int acquire(void *p) { (void)p; assert(!held); if(acquire_failure) return acquire_failure; held=1; return 0; }
static void release(void *p) { (void)p; assert(held); held=0; }
static uint32_t rd(void *p,uint32_t a) { (void)p; assert(held); ++reads; return regs[a/4]; }
static void wr(void *p,uint32_t a,uint32_t v) { (void)p; assert(held); ++writes; regs[a/4]=v; }
static int state(void *p,Psp2GpuProfWorkState *s)
{
    (void)p; assert(held); ++states;
    if(states==fail_state) return PSP2_GPUPROF_OFFLINE;
    *s=(Psp2GpuProfWorkState){100,2,3,200,4,5};
    if(changed && !(states&1)) s->render_scene=6;
    return 0;
}
int main(void)
{
    struct gp_session s;
    struct gp_io io={NULL,acquire,release,rd,wr};
    Psp2GpuProfWork out, untouched;
    uint32_t original[sizeof(regs)/sizeof(*regs)];
    gp_init(&s,&io);
    regs[0x4000/4]=3;
    for(unsigned c=0;c<4;++c) {
        unsigned b=0x8000+c*0x4000;
        regs[(b+0x90)/4]=0xabcdef01;
        regs[(b+0x94)/4]=0x76543210;
        regs[(b+0x78)/4]=0x100;
        regs[(b+0x70)/4]=(c?1u<<29:0)|0x230000;
    }
    memcpy(original,regs,sizeof(regs));
    assert(!gp_work(&s,state,NULL,&out));
    assert(!held && states==8);
    assert(out.size==sizeof(out) && out.abi==1);
    assert(out.cores[0].flags==(PSP2_GPUPROF_WORK_UNVERIFIED|PSP2_GPUPROF_WORK_INACTIVE));
    for(unsigned c=1;c<4;++c) assert(out.cores[c].flags==PSP2_GPUPROF_WORK_UNVERIFIED);
    assert(!memcmp(original,regs,sizeof(regs)));
    changed=1; states=0;
    assert(!gp_work(&s,state,NULL,&out));
    for(unsigned c=0;c<4;++c) assert(out.cores[c].flags&PSP2_GPUPROF_WORK_CHANGED);
    changed=0;
    memset(&out,0xa5,sizeof(out)); untouched=out;
    for(fail_state=1;fail_state<=8;++fail_state) {
        states=0;
        assert(gp_work(&s,state,NULL,&out)==PSP2_GPUPROF_OFFLINE);
        assert(!held && !memcmp(original,regs,sizeof(regs)));
        assert(!memcmp(&out,&untouched,sizeof(out)));
    }
    reads=writes=states=0; s.active=1;
    assert(gp_work(&s,state,NULL,&out)==PSP2_GPUPROF_BUSY);
    assert(!reads && !writes && !states && !held);
    s.active=0; acquire_failure=PSP2_GPUPROF_OFFLINE;
    assert(gp_work(&s,state,NULL,&out)==PSP2_GPUPROF_OFFLINE);
    assert(!reads && !writes && !states && !held);
    acquire_failure=0;
    regs[0x4000/4]=0;
    assert(gp_work(&s,state,NULL,&out)==PSP2_GPUPROF_UNSUPPORTED);
    assert(!writes && !states && !held);
    regs[0x4000/4]=3;
    regs[0x78/4]=1;
    assert(gp_work(&s,state,NULL,&out)==PSP2_GPUPROF_BUSY);
    assert(!writes && !states && !held);
    regs[0x78/4]=0;
    for(unsigned c=0;c<4;++c) {
        unsigned a=0x8078+c*0x4000;
        regs[a/4]|=1;
        assert(gp_work(&s,state,NULL,&out)==PSP2_GPUPROF_BUSY);
        assert(!writes && !states && !held);
        regs[a/4]&=~1u;
    }
    assert(gp_work(&s,NULL,NULL,&out)==PSP2_GPUPROF_INVALID);
    assert(gp_work(&s,state,NULL,NULL)==PSP2_GPUPROF_INVALID);
    puts("work observations: restoration, errors, transitions and unverified ownership passed");
}
