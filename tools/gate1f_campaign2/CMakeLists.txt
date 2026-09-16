cmake_minimum_required(VERSION 3.10)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK")
  endif()
endif()
project(vbe_gate1f_campaign2 C)
include("${VITASDK}/share/vita.cmake" REQUIRED)
# The observer harness is user-mode research tooling, not a throughput-sensitive
# production path.  Prefer size optimization so vita-elf-create has ample room
# for SCE metadata even as explicit audit/logging checks are added.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -Os -std=gnu99")
if(NOT VBE_PRODUCTION_STUB)
  message(FATAL_ERROR "VBE_PRODUCTION_STUB must point to the exact VitaBrightEX production stub library")
endif()
add_executable(vbe_gate1f_campaign2 main.c backend.c ui.c observer_core.c)
target_include_directories(vbe_gate1f_campaign2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(vbe_gate1f_campaign2
  ${VBE_PRODUCTION_STUB}
  SceDisplay_stub
  SceCtrl_stub
  SceSysmem_stub
  SceProcessmgr_stub
  SceKernelThreadMgr_stub
  SceIofilemgr_stub
  SceLibKernel_stub)
set(VITA_ELF_CREATE_FLAGS "")
set(VITA_MAKE_FSELF_FLAGS "")
vita_create_self(vbe_gate1f_campaign2.self vbe_gate1f_campaign2 UNSAFE)
vita_create_vpk(vbe_gate1f_campaign2.vpk VBEG1F002 vbe_gate1f_campaign2.self
  VERSION 01.02 NAME "VBE Gate-1F Campaign 2 R3")
