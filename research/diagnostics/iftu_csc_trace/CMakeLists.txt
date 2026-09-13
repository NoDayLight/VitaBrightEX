cmake_minimum_required(VERSION 3.10)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VITASDK})
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
    else()
        message(FATAL_ERROR "Please define VITASDK")
    endif()
endif()

project(vbe_color_trace C)
include("${VITASDK}/share/vita.cmake" REQUIRED)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -O2 -std=gnu99")

add_executable(vbe_color_trace_kernel main.c)
target_link_libraries(vbe_color_trace_kernel
    taihenForKernel_stub
    taihenModuleUtils_stub
    SceModulemgrForDriver_stub
    SceSysmemForDriver_stub
    gcc
)
set_target_properties(vbe_color_trace_kernel PROPERTIES
    LINK_FLAGS "-nostdlib"
    COMPILE_FLAGS "-D__VITA_KERNEL__ -fno-pic -fno-PIC"
)
vita_create_self(vbe_color_trace.skprx vbe_color_trace_kernel
    CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/module.yml UNSAFE)
vita_create_stubs(vbe_color_trace_stubs vbe_color_trace_kernel
    ${CMAKE_CURRENT_SOURCE_DIR}/module.yml KERNEL)

add_executable(vbe_color_trace_drain drain.c)
add_dependencies(vbe_color_trace_drain vbe_color_trace_stubs)
target_link_libraries(vbe_color_trace_drain
    ${CMAKE_CURRENT_BINARY_DIR}/libVbeColorTrace_stub.a
    SceIofilemgr_stub
    SceLibKernel_stub
)
vita_create_self(vbe_color_trace_drain.self vbe_color_trace_drain UNSAFE)
vita_create_vpk(vbe_color_trace_drain.vpk VBETRACE vbe_color_trace_drain.self
    VERSION 01.00
    NAME "VBE Color Trace Drain"
)
