cmake_minimum_required(VERSION 3.10)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK")
  endif()
endif()
project(vbe_gate1b_snapshot C)
include("${VITASDK}/share/vita.cmake" REQUIRED)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -O2 -std=gnu99")

add_executable(vbe_gate1b_snapshot_kernel main.c)
target_link_libraries(vbe_gate1b_snapshot_kernel
  taihenForKernel_stub
  taihenModuleUtils_stub
  SceKernelSuspendForDriver_stub
  SceModulemgrForDriver_stub
  SceSysmemForDriver_stub
  gcc)
set_target_properties(vbe_gate1b_snapshot_kernel PROPERTIES
  LINK_FLAGS "-nostdlib"
  COMPILE_FLAGS "-D__VITA_KERNEL__ -fno-pic -fno-PIC")
vita_create_self(vbe_gate1b_snapshot.skprx vbe_gate1b_snapshot_kernel
  CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/module.yml UNSAFE)
vita_create_stubs(vbe_gate1b_snapshot_stubs vbe_gate1b_snapshot_kernel
  ${CMAKE_CURRENT_SOURCE_DIR}/module.yml KERNEL)

add_executable(vbe_gate1b_snapshot_app snapshot.c)
add_dependencies(vbe_gate1b_snapshot_app vbe_gate1b_snapshot_stubs)
target_link_libraries(vbe_gate1b_snapshot_app
  ${CMAKE_CURRENT_BINARY_DIR}/libVbeGate1bSnapshot_stub.a
  SceIofilemgr_stub
  SceLibKernel_stub)
set(VITA_ELF_CREATE_FLAGS "")
set(VITA_MAKE_FSELF_FLAGS "")
vita_create_self(vbe_gate1b_snapshot.self vbe_gate1b_snapshot_app UNSAFE)
vita_create_vpk(vbe_gate1b_snapshot.vpk VBEG1BSN vbe_gate1b_snapshot.self
  VERSION 01.00 NAME "VBE Gate-1B Lifecycle Snapshot")
