#include "diagnostic.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t regs[0x20000/4], baseline[0x20000/4];
static unsigned held, calls, fail, mode, samples[4], writes;
static unsigned selected_group=43, selected_tag=70;
static int acquire(void *p) { (void)p; assert(!held); held=1; return 0; }
static void release(void *p) { (void)p; assert(held); held=0; }
static void wr(void *p,uint32_t a,uint32_t v)
{ (void)p; assert(held); ++writes; regs[a/4]=v; }
static uint32_t rd(void *p,uint32_t a)
{
    (void)p; assert(held);
    if (a>=0x8000 && (a-0x8000)%0x4000==0x70) {
        unsigned c=(a-0x8000)/0x4000,b=0x8000+c*0x4000;
        unsigned g=regs[(b+0x90)/4]&127u;
        const unsigned order[]={17,selected_tag,selected_group,selected_tag,17};
        unsigned i=samples[c]++;
        assert(i<5 && g==order[i]);
        assert(regs[(b+0x90)/4]==(g|(g<<16)|0x08000000u));
        assert(regs[(b+0x94)/4]==(g|(g<<16)|0x18001000u));
        if (i==2) return 0xaabbccdd;
        uint32_t v=0x20030042;
        if (mode==1 && i==4) v+=0x10000;
        if (mode==2 && i==3) v+=1;
        if (mode==3) v&=~0x20000000u;
        if (mode==4 && g==selected_tag) v+=0x10000;
        return v;
    }
    return regs[a/4];
}
static int state(void *p,Psp2GpuProfWorkState *v)
{
    (void)p; assert(held);
    if (++calls==fail) return PSP2_GPUPROF_OFFLINE;
    *v=(Psp2GpuProfWorkState){1,2,3,4,5,6};
    if (mode==5 && !(calls&1)) ++v->render_scene;
    return 0;
}
static void reset(void)
{ calls=writes=0; memset(samples,0,sizeof(samples)); memcpy(regs,baseline,sizeof(regs)); }
int main(void)
{
    struct gp_session s;
    struct gp_io io={NULL,acquire,release,rd,wr};
    Psp2GpuProfDiagnostic out, untouched;
    Psp2GpuProfDiagnosticConfig cfg={sizeof(cfg),PSP2_GPUPROF_ABI,43,70,{0}};
    assert(gp_diagnostic_config_valid(&cfg));
    cfg.size--; assert(!gp_diagnostic_config_valid(&cfg));cfg.size++;
    cfg.abi++;assert(!gp_diagnostic_config_valid(&cfg));cfg.abi--;
    for(unsigned i=0;i<4;++i) {
        cfg.reserved[i]=1;assert(!gp_diagnostic_config_valid(&cfg));cfg.reserved[i]=0;
    }
    assert(!gp_diagnostic_config_valid(NULL));
    gp_init(&s,&io);
    baseline[0x4000/4]=3;
    for(unsigned c=0;c<4;++c) {
        unsigned b=0x8000+c*0x4000;
        baseline[(b+0x78)/4]=0x100;
        baseline[(b+0x90)/4]=0x12345678;
        baseline[(b+0x94)/4]=0x98765432;
    }
    const unsigned flags[]={1,1|4|32,1|8|32,1|16,1|32,1|2};
    for(mode=0;mode<6;++mode) {
        reset();
        assert(!gp_diagnostic_read(&s,state,NULL,43,70,&out));
        assert(!held && calls==8 && !memcmp(regs,baseline,sizeof(regs)));
        for(unsigned c=0;c<4;++c) {
            assert(samples[c]==5 && out.cores[c].value==0xaabbccdd);
            assert(out.cores[c].flags==flags[mode]);
        }
    }
    mode=0;
    const unsigned groups[]={2,4,43,55,71,72,75,76,77,78,103,104,107,108,109,110};
    for(unsigned t=0;t<2;++t) for(unsigned g=0;g<sizeof(groups)/sizeof(*groups);++g) {
        selected_tag=t?102:70; selected_group=groups[g]; reset();
        assert(!gp_diagnostic_read(&s,state,NULL,selected_group,selected_tag,&out));
        assert(out.group==selected_group && out.tag_group==selected_tag);
        assert(!held && !memcmp(regs,baseline,sizeof(regs)));
    }
    selected_tag=70;selected_group=43;
    memset(&out,0xa5,sizeof(out)); untouched=out;
    for(fail=1;fail<=8;++fail) {
        reset();
        assert(gp_diagnostic_read(&s,state,NULL,43,70,&out)==PSP2_GPUPROF_OFFLINE);
        assert(!held && !memcmp(regs,baseline,sizeof(regs)));
        assert(!memcmp(&out,&untouched,sizeof(out)));
    }
    fail=0;reset();s.active=1;
    assert(gp_diagnostic_read(&s,state,NULL,43,70,&out)==PSP2_GPUPROF_BUSY);
    assert(!held && !writes && !calls);s.active=0;
    for(unsigned c=0;c<4;++c) {
        reset();regs[(0x8078+c*0x4000)/4]|=1;
        assert(gp_diagnostic_read(&s,state,NULL,43,70,&out)==PSP2_GPUPROF_BUSY);
        assert(!held && !writes && !calls);
    }
    reset();
    assert(gp_diagnostic_read(&s,state,NULL,127,70,&out)==PSP2_GPUPROF_INVALID);
    assert(gp_diagnostic_read(&s,state,NULL,43,17,&out)==PSP2_GPUPROF_INVALID);
    assert(!writes && !calls);
    puts("diagnostics: ordered brackets, transitions, restoration, no partial output passed");
}
