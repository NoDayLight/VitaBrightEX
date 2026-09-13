cmake_minimum_required(VERSION 3.10)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VITASDK})
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
    else()
        message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
    endif()
endif()
project(vitabright C)
include("${VITASDK}/share/vita.cmake" REQUIRED)
include("${CMAKE_SOURCE_DIR}/cmake/VbeBuildId.cmake")
vbe_resolve_build_id(VBE_BUILD_ID "${CMAKE_SOURCE_DIR}")
message(STATUS "VitaBrightEX build id: ${VBE_BUILD_ID}")
option(ENABLE_LOGGING "Enable debug logging to ur0:data/vitabright_log.txt" OFF)
if(ENABLE_LOGGING) add_definitions(-DENABLE_LOGGING=1) endif()
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -Wall -Wextra -Werror -O3 -fno-pic -fno-PIC -std=gnu99")
add_executable(vitabright
 main.c main.h module_lifecycle_core.c module_lifecycle_core.h log.c log.h build_info.c build_info.h
 config.c config.h config_parser.c config_parser.h text_stream.c text_stream.h source_authority.c source_authority.h
 transaction_core.c transaction_core.h persistence_core.c persistence_core.h persistence_file.c persistence_file.h
 status.c status.h status_error_core.h state_lock_core.c state_lock_core.h state_lock.c state_lock.h
 color_space.c color_space.h display_domains.h screen_filter_types.h filter_policy.c filter_policy.h filter_state_core.c filter_state_core.h
 screen_filter.c screen_filter.h affine_core.c affine_core.h taihen_extra.h lut_parser_core.h
 oled/lut_parser.c oled/parser.c oled/parser.h oled/hooks.c oled/hooks.h oled/lut.h
 oled/transform_core.c oled/transform_core.h oled/state_core.c oled/state_core.h
 lcd/lut_parser.c lcd/hooks.c lcd/hooks.h lcd/lcd_lut.h)
target_compile_definitions(vitabright PRIVATE VBE_BUILD_ID="${VBE_BUILD_ID}")
target_link_libraries(vitabright SceDisplayForDriver_stub taihenForKernel_stub taihenModuleUtils_stub SceIofilemgrForDriver_stub SceThreadmgrForDriver_stub gcc SceSysclibForDriver_stub SceSysmemForDriver_stub SceSysrootForKernel_stub SceSysrootForDriver_stub)
set_target_properties(vitabright PROPERTIES LINK_FLAGS "-nostdlib" COMPILE_FLAGS "-D__VITA_KERNEL__")
vita_create_self(vitabright.skprx vitabright CONFIG ${CMAKE_SOURCE_DIR}/module.yml UNSAFE)
vita_create_stubs(vitabright-stubs vitabright module.yml KERNEL)
install(DIRECTORY ${CMAKE_BINARY_DIR}/vitabright-stubs/ DESTINATION lib FILES_MATCHING PATTERN "*.a")
set(PSVITAIP "192.168.1.10" CACHE STRING "PS Vita IP address for FTP deploy")
set(VBE_FTP_ROOT "ftp://${PSVITAIP}:1337")
set(VBE_FTP_UR0 "${VBE_FTP_ROOT}//ur0:")
set(VBE_FTP_UX0 "${VBE_FTP_ROOT}//ux0:")
set(VBE_FTP_UR0_TAI "${VBE_FTP_UR0}/tai")
add_custom_target(send
 COMMAND curl --ftp-method nocwd -T vitabright.skprx ${VBE_FTP_UR0_TAI}/vitabright.skprx
 COMMAND curl --ftp-method nocwd -T ${CMAKE_SOURCE_DIR}/oled/luts/vitabright_lut.txt ${VBE_FTP_UR0_TAI}/vitabright_lut.txt
 COMMAND curl --ftp-method nocwd -T ${CMAKE_SOURCE_DIR}/oled/luts/vitabright_lut_p4.txt ${VBE_FTP_UR0_TAI}/vitabright_lut_p4.txt
 COMMAND curl --ftp-method nocwd -T ${CMAKE_SOURCE_DIR}/oled/luts/vitabright_lut_p5.txt ${VBE_FTP_UR0_TAI}/vitabright_lut_p5.txt
 COMMAND curl --ftp-method nocwd -T ${CMAKE_SOURCE_DIR}/oled/luts/vitabright_lut_p6.txt ${VBE_FTP_UR0_TAI}/vitabright_lut_p6.txt
 COMMAND curl --ftp-method nocwd -T ${CMAKE_SOURCE_DIR}/lcd/luts/vitabright_lcd_lut.txt ${VBE_FTP_UR0_TAI}/vitabright_lcd_lut.txt
 COMMAND curl --ftp-method nocwd -T ${CMAKE_SOURCE_DIR}/vitabrightex.cfg ${VBE_FTP_UR0_TAI}/vitabrightex.cfg DEPENDS vitabright.skprx)
