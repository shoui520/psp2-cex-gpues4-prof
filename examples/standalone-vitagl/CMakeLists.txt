cmake_minimum_required(VERSION 3.16)
project(gpuprof_capture_example C CXX)
include("${VITASDK}/share/vita.cmake" REQUIRED)
find_package(psp2_gpuprof CONFIG REQUIRED)
add_executable(gpuprof_capture_example main.c)
set_target_properties(gpuprof_capture_example PROPERTIES LINKER_LANGUAGE CXX)
target_compile_options(gpuprof_capture_example PRIVATE -Wall -Wextra -Werror)
target_link_libraries(gpuprof_capture_example PRIVATE psp2_gpuprof::vitagl vitaGL
    vitashark SceShaccCg_stub SceShaccCgExt taihen_stub mathneon
    SceDisplay_stub SceCommonDialog_stub SceSysmodule_stub SceAppMgr_stub
    SceIofilemgr_stub SceGxm_stub SceKernelDmacMgr_stub SceSysmem_stub m)
vita_create_self(eboot.bin gpuprof_capture_example UNSAFE)
vita_create_vpk(gpuprof-capture-example.vpk GPFCAP001 eboot.bin
    VERSION 01.01 NAME "GPU Capture Example")
