#include "psp2_gpuprof_attribution.h"
#include <assert.h>
#include <stddef.h>

int main(void)
{
    Psp2GpuProfLabelBuilder builder;
    Psp2GpuProfDrawLabel storage[2];
    assert(!psp2GpuProfLabelsInit(&builder,storage,2));
    assert(!psp2GpuProfLabelPass(&builder,1,"world"));
    assert(!psp2GpuProfLabelShaders(&builder,2,"vs",3,"fs"));
    assert(!psp2GpuProfLabelDraw(&builder,4,"water"));
    assert(!psp2GpuProfLabelPass(&builder,5,"UI"));
    assert(!psp2GpuProfLabelShaders(&builder,6,"ui-vs",7,"ui-fs"));
    assert(!psp2GpuProfLabelDraw(&builder,8,"text"));
    assert(storage[0].pass_id==1 && storage[0].fragment_shader_id==3);
    assert(storage[1].pass_id==5 && storage[1].fragment_shader_id==7);
    assert(psp2GpuProfLabelDraw(&builder,9,"overflow")==PSP2_GPUPROF_BUSY);
    assert(psp2GpuProfLabelPass(&builder,1,"world")==PSP2_GPUPROF_BUSY);
    assert(builder.count==2);
    assert(!psp2GpuProfLabelsInit(&builder,storage,2));
    assert(psp2GpuProfLabelDraw(&builder,1,"missing binding")==PSP2_GPUPROF_INVALID);
    assert(builder.error==PSP2_GPUPROF_INVALID && builder.count==0);
    Psp2GpuProfDrawLabel a[] = {
        {10, 1, 2, 3, "clear", "setup", "clear-vs", "clear-fs"},
        {11, 4, 5, 6, "water", "world", "water-vs", "water-fs"}
    };
    Psp2GpuProfDrawLabel b[] = {
        {12, 7, 8, 9, "text", "UI", "ui-vs", "ui-fs"}
    };
    Psp2GpuProfSceneLabel scenes[] = {
        {1, 100, 1, 10, 20, a, 2, 1},
        {2, 100, 1, 20, 30, b, 1, 1}
    };
    Psp2GpuProfAttribution out;
#define CHECK(begin,end,raw,want) do { \
    assert(!psp2GpuProfAttributeFragment(scenes,2,begin,end,raw,&out)); \
    assert(out.status == want); \
    if (want != PSP2_GPUPROF_ATTR_CANDIDATE) assert(!out.scene && !out.draw); \
} while (0)
    CHECK(11,12,0x20020000,PSP2_GPUPROF_ATTR_CANDIDATE);
    assert(out.draw == &a[1] && out.draw->fragment_shader_id == 6);
    CHECK(21,22,0x20010000,PSP2_GPUPROF_ATTR_CANDIDATE);
    assert(out.draw == &b[0] && out.scene->scene_id == 2);
    CHECK(19,21,0x20010000,PSP2_GPUPROF_ATTR_AMBIGUOUS);
    CHECK(19,20,0x20010000,PSP2_GPUPROF_ATTR_AMBIGUOUS);
    CHECK(9,11,0x20010000,PSP2_GPUPROF_ATTR_BOUNDARY);
    CHECK(31,32,0x20010000,PSP2_GPUPROF_ATTR_NO_SCENE);
    CHECK(11,12,0x00020000,PSP2_GPUPROF_ATTR_INACTIVE);
    CHECK(11,12,0x20000000,PSP2_GPUPROF_ATTR_UNMAPPED_BATCH);
    CHECK(21,22,0x20020000,PSP2_GPUPROF_ATTR_OUT_OF_RANGE);
    scenes[0].flags = 0;
    CHECK(11,12,0x20010000,PSP2_GPUPROF_ATTR_UNVERIFIED_ORDER);
    scenes[0].flags = 1;
    scenes[1].cpu_begin_us = 15;
    CHECK(16,17,0x20010000,PSP2_GPUPROF_ATTR_AMBIGUOUS);
    scenes[1].scene_id = 1;
    assert(psp2GpuProfAttributeFragment(scenes,2,11,12,0,&out)==PSP2_GPUPROF_INVALID);
    assert(!out.scene && !out.draw);
    assert(!psp2GpuProfAttributeFragment(NULL,0,11,12,0x20010000,&out));
    assert(out.status == PSP2_GPUPROF_ATTR_NO_SCENE);
    assert(psp2GpuProfAttributeFragment(NULL,1,11,12,0,&out)==PSP2_GPUPROF_INVALID);
    assert(psp2GpuProfAttributeFragment(scenes,2,12,11,0,&out)==PSP2_GPUPROF_INVALID);
    scenes[1].scene_id = 2;
    scenes[0].draw_count = 8192;
    assert(psp2GpuProfAttributeFragment(scenes,2,11,12,0,&out)==PSP2_GPUPROF_INVALID);
    return 0;
}
