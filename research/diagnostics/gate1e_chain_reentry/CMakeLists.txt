cmake_minimum_required(VERSION 3.10)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK")
  endif()
endif()
project(vbe_gate1e_chain_reentry C)
include("${VITASDK}/share/vita.cmake" REQUIRED)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -O2 -std=gnu99")
if(NOT VBE_PRODUCTION_STUB)
  message(FATAL_ERROR "VBE_PRODUCTION_STUB must point to the exact Gate-1D libvitabright_stub.a")
endif()

add_executable(vbe_gate1e_chain_kernel main.c)
target_include_directories(vbe_gate1e_chain_kernel PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
target_link_libraries(vbe_gate1e_chain_kernel
  taihenForKernel_stub
  taihenModuleUtils_stub
  SceSysmemForDriver_stub
  SceThreadmgrForDriver_stub
  gcc)
set_target_properties(vbe_gate1e_chain_kernel PROPERTIES
  LINK_FLAGS "-nostdlib"
  COMPILE_FLAGS "-D__VITA_KERNEL__ -fno-pic -fno-PIC")
vita_create_self(vbe_gate1e_chain.skprx vbe_gate1e_chain_kernel
  CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/module.yml UNSAFE)
vita_create_stubs(vbe_gate1e_chain_stubs vbe_gate1e_chain_kernel
  ${CMAKE_CURRENT_SOURCE_DIR}/module.yml KERNEL)

add_executable(vbe_gate1e_chain_identity control.c)
add_dependencies(vbe_gate1e_chain_identity vbe_gate1e_chain_stubs)
target_include_directories(vbe_gate1e_chain_identity PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
target_link_libraries(vbe_gate1e_chain_identity
  ${CMAKE_CURRENT_BINARY_DIR}/libVbeGate1eChain_stub.a
  ${VBE_PRODUCTION_STUB}
  SceIofilemgr_stub
  SceLibKernel_stub)
set(VITA_ELF_CREATE_FLAGS "")
set(VITA_MAKE_FSELF_FLAGS "")
vita_create_self(vbe_gate1e_chain_identity.self vbe_gate1e_chain_identity UNSAFE)
vita_create_vpk(vbe_gate1e_chain_identity.vpk VBEG1ECH0 vbe_gate1e_chain_identity.self
  VERSION 01.00 NAME "VBE Gate-1E Chain Identity")
