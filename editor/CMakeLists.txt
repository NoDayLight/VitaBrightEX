cmake_minimum_required(VERSION 3.10)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VITASDK})
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
    else()
        message(FATAL_ERROR "Please define VITASDK")
    endif()
endif()

project(vitabrightex-editor C)
include("${VITASDK}/share/vita.cmake" REQUIRED)
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/VbeBuildId.cmake")
vbe_resolve_build_id(VBE_BUILD_ID "${CMAKE_CURRENT_SOURCE_DIR}/..")
message(STATUS "VitaBrightEX editor build id: ${VBE_BUILD_ID}")

set(VITA_TITLEID VBRIGHT14)
add_executable(vitabrightex-editor.elf app.c)
target_include_directories(vitabrightex-editor.elf PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_options(vitabrightex-editor.elf PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(vitabrightex-editor.elf PRIVATE VBE_BUILD_ID="${VBE_BUILD_ID}")

target_link_libraries(vitabrightex-editor.elf
    vitabright_stub_weak
    vita2d
    SceDisplay_stub
    SceGxm_stub
    SceSysmodule_stub
    SceCtrl_stub
    ScePgf_stub
    ScePvf_stub
    SceCommonDialog_stub
    SceAppMgr_stub
    freetype
    png
    jpeg
    z
    m
    c
)

vita_create_self(vitabrightex-editor.self vitabrightex-editor.elf UNSAFE UNCOMPRESSED)
vita_create_vpk(vitabrightex-editor.vpk ${VITA_TITLEID} vitabrightex-editor.self
    VERSION "01.40"
    NAME "VitaBrightEX v1.4"
)
