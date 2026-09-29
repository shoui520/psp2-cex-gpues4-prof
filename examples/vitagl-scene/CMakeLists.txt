cmake_minimum_required(VERSION 3.16)
project(gpuprof_vitagl_scene C CXX)
set(CMAKE_C_STANDARD 11)
include("${VITASDK}/share/vita.cmake" REQUIRED)
find_package(psp2_gpuprof CONFIG REQUIRED)
find_library(GPUPROF_STUB NAMES psp2_gpuprof_stub REQUIRED)
set(GPUPROF_SCENE_SHADERS "" CACHE PATH "Directory containing scene_v/f and post_v/f GXP files")
foreach(shader scene_v scene_f post_v post_f)
    if(NOT EXISTS "${GPUPROF_SCENE_SHADERS}/${shader}.gxp")
        message(FATAL_ERROR "Compile shaders/${shader}.cg and set GPUPROF_SCENE_SHADERS")
    endif()
endforeach()
add_executable(gpuprof_vitagl_scene main.c)
set_target_properties(gpuprof_vitagl_scene PROPERTIES LINKER_LANGUAGE CXX)
target_compile_options(gpuprof_vitagl_scene PRIVATE -O2 -Wall -Wextra -Werror)
target_link_libraries(gpuprof_vitagl_scene PRIVATE psp2_gpuprof::vitagl vitaGL
    ${GPUPROF_STUB} vitashark SceShaccCg_stub SceShaccCgExt taihen_stub mathneon
    SceDisplay_stub SceCommonDialog_stub SceSysmodule_stub SceAppMgr_stub
    SceIofilemgr_stub SceGxm_stub SceKernelDmacMgr_stub SceSysmem_stub ScePower_stub m)
vita_create_self(eboot.bin gpuprof_vitagl_scene UNSAFE)
vita_create_vpk(gpuprof-vitagl-scene.vpk GPFSCN001 eboot.bin
    VERSION 01.02 NAME "GPU Profiler 3D Scene"
    FILE ${GPUPROF_SCENE_SHADERS}/scene_v.gxp shaders/scene_v.gxp
         ${GPUPROF_SCENE_SHADERS}/scene_f.gxp shaders/scene_f.gxp
         ${GPUPROF_SCENE_SHADERS}/post_v.gxp shaders/post_v.gxp
         ${GPUPROF_SCENE_SHADERS}/post_f.gxp shaders/post_f.gxp)
