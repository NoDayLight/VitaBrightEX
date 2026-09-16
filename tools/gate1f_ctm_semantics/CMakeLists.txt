cmake_minimum_required(VERSION 3.10)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK")
  endif()
endif()
project(vbe_gate1f_ctm_semantics C)
include("${VITASDK}/share/vita.cmake" REQUIRED)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -O2 -std=gnu99")
if(NOT VBE_PRODUCTION_STUB)
  message(FATAL_ERROR "VBE_PRODUCTION_STUB must point to the exact VitaBrightEX production stub library")
endif()
add_executable(vbe_gate1f_ctm_semantics gate1f.c)
target_include_directories(vbe_gate1f_ctm_semantics PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(vbe_gate1f_ctm_semantics
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
vita_create_self(vbe_gate1f_ctm_semantics.self vbe_gate1f_ctm_semantics UNSAFE)
vita_create_vpk(vbe_gate1f_ctm_semantics.vpk VBEG1F001 vbe_gate1f_ctm_semantics.self
  VERSION 01.00 NAME "VBE Gate-1F CTM Semantics")
