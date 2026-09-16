cmake_minimum_required(VERSION 3.10)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK")
  endif()
endif()
project(vbe_gate1e_immediate_apply C)
include("${VITASDK}/share/vita.cmake" REQUIRED)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -O2 -std=gnu99")
if(NOT VBE_PRODUCTION_STUB)
  message(FATAL_ERROR "VBE_PRODUCTION_STUB must point to the exact Gate-1D libvitabright_stub.a")
endif()

add_executable(vbe_gate1e_immediate_kernel main.c)
target_include_directories(vbe_gate1e_immediate_kernel PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
target_link_libraries(vbe_gate1e_immediate_kernel
  taihenForKernel_stub
  taihenModuleUtils_stub
  SceSysmemForDriver_stub
  SceThreadmgrForDriver_stub
  gcc)
set_target_properties(vbe_gate1e_immediate_kernel PROPERTIES
  LINK_FLAGS "-nostdlib"
  COMPILE_FLAGS "-D__VITA_KERNEL__ -fno-pic -fno-PIC")
vita_create_self(vbe_gate1e_immediate.skprx vbe_gate1e_immediate_kernel
  CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/module.yml UNSAFE)
vita_create_stubs(vbe_gate1e_immediate_stubs vbe_gate1e_immediate_kernel
  ${CMAKE_CURRENT_SOURCE_DIR}/module.yml KERNEL)

add_executable(vbe_gate1e_immediate_mild mild.c action_common.c)
add_dependencies(vbe_gate1e_immediate_mild vbe_gate1e_immediate_stubs)
target_include_directories(vbe_gate1e_immediate_mild PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
target_link_libraries(vbe_gate1e_immediate_mild
  ${CMAKE_CURRENT_BINARY_DIR}/libVbeGate1eImmediate_stub.a
  ${VBE_PRODUCTION_STUB}
  SceIofilemgr_stub
  SceLibKernel_stub)

add_executable(vbe_gate1e_immediate_reset reset.c action_common.c)
add_dependencies(vbe_gate1e_immediate_reset vbe_gate1e_immediate_stubs)
target_include_directories(vbe_gate1e_immediate_reset PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
target_link_libraries(vbe_gate1e_immediate_reset
  ${CMAKE_CURRENT_BINARY_DIR}/libVbeGate1eImmediate_stub.a
  ${VBE_PRODUCTION_STUB}
  SceIofilemgr_stub
  SceLibKernel_stub)

set(VITA_ELF_CREATE_FLAGS "")
set(VITA_MAKE_FSELF_FLAGS "")
vita_create_self(vbe_gate1e_immediate_mild.self vbe_gate1e_immediate_mild UNSAFE)
vita_create_vpk(vbe_gate1e_immediate_mild.vpk VBEG1EMD0 vbe_gate1e_immediate_mild.self
  VERSION 01.00 NAME "VBE Gate-1E Immediate Mild")
vita_create_self(vbe_gate1e_immediate_reset.self vbe_gate1e_immediate_reset UNSAFE)
vita_create_vpk(vbe_gate1e_immediate_reset.vpk VBEG1ERS0 vbe_gate1e_immediate_reset.self
  VERSION 01.00 NAME "VBE Gate-1E Immediate Reset")
