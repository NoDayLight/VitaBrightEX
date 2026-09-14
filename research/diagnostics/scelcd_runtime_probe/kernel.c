#include <stdint.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysmem/data_transfers.h>
#include <taihen.h>
#include "../../../taihen_extra.h"
#include "../gate0_observer/retail_365_layout.h"
#include "probe_protocol.h"
#define VBE_MODULEMGR_FOR_KERNEL_365_NID 0x92C9FFC2u
#define VBE_GET_MODULE_INFO_365_NID 0xDAA90093u
typedef int (*VbeKernelGetModuleInfoFn)(SceUID pid,SceUID modid,SceKernelModuleInfo *info);
static VbeSceLcdRuntimeProbe g_probe;
static void zero_bytes(void *dst,uint32_t n){volatile uint8_t *p=(volatile uint8_t*)dst;uint32_t i;for(i=0;i<n;i++)p[i]=0;}
static void copy_bytes(uint8_t *dst,const volatile uint8_t *src,uint32_t n){uint32_t i;for(i=0;i<n;i++)dst[i]=src[i];}
int vbeSceLcdRuntimeProbeGet(VbeSceLcdRuntimeProbe *out){if(!out)return -1;return ksceKernelMemcpyKernelToUser(out,&g_probe,sizeof(g_probe));}
void _start() __attribute__((weak,alias("module_start")));
int module_start(SceSize argc,const void *args){
    SceKernelFwInfo fw;SceKernelModuleInfo info;tai_module_info_t lcd;uintptr_t get_info_addr=0u,writer=0u,reader=0u;VbeKernelGetModuleInfoFn get_info;uint32_t i;(void)argc;(void)args;
    zero_bytes(&g_probe,sizeof(g_probe));g_probe.magic=VBE_SCELCD_PROBE_MAGIC;g_probe.version=VBE_SCELCD_PROBE_VERSION;
    copy_bytes(g_probe.expected_writer,vbe_lcd_panel_writer_signature,VBE_SCELCD_EXPECTED_BYTES);copy_bytes(g_probe.expected_reader,vbe_lcd_panel_reader_signature,VBE_SCELCD_EXPECTED_BYTES);
    zero_bytes(&fw,sizeof(fw));fw.size=sizeof(fw);g_probe.get_fw_ret=ksceKernelGetSystemSwVersion(&fw);if(g_probe.get_fw_ret>=0)g_probe.firmware=fw.version;
    if(g_probe.get_fw_ret<0||g_probe.firmware!=VBE_SCELCD_PROBE_FW_365)return SCE_KERNEL_START_SUCCESS;
    g_probe.resolve_module_info_ret=module_get_export_func(KERNEL_PID,"SceKernelModulemgr",VBE_MODULEMGR_FOR_KERNEL_365_NID,VBE_GET_MODULE_INFO_365_NID,&get_info_addr);
    if(g_probe.resolve_module_info_ret<0||get_info_addr==0u)return SCE_KERNEL_START_SUCCESS;get_info=(VbeKernelGetModuleInfoFn)get_info_addr;
    zero_bytes(&lcd,sizeof(lcd));lcd.size=sizeof(lcd);g_probe.get_lcd_ret=taiGetModuleInfoForKernel(KERNEL_PID,"SceLcd",&lcd);if(g_probe.get_lcd_ret<0)return SCE_KERNEL_START_SUCCESS;g_probe.lcd_modid=(uint32_t)lcd.modid;
    g_probe.writer_offset_ret=module_get_offset(KERNEL_PID,lcd.modid,VBE_LCD_PANEL_SEGMENT,VBE_SCELCD_WRITER_OFFSET,&writer);if(g_probe.writer_offset_ret>=0&&writer!=0u){g_probe.writer_addr=(uint32_t)writer;copy_bytes(g_probe.writer_bytes,(const volatile uint8_t*)writer,VBE_SCELCD_CAPTURE_BYTES);}
    g_probe.reader_offset_ret=module_get_offset(KERNEL_PID,lcd.modid,VBE_LCD_PANEL_SEGMENT,VBE_SCELCD_READER_OFFSET,&reader);if(g_probe.reader_offset_ret>=0&&reader!=0u){g_probe.reader_addr=(uint32_t)reader;copy_bytes(g_probe.reader_bytes,(const volatile uint8_t*)reader,VBE_SCELCD_CAPTURE_BYTES);}
    zero_bytes(&info,sizeof(info));info.size=sizeof(info);g_probe.module_info_ret=get_info(KERNEL_PID,lcd.modid,&info);if(g_probe.module_info_ret>=0){for(i=0;i<4u;i++){g_probe.segment_base[i]=(uint32_t)(uintptr_t)info.segments[i].vaddr;g_probe.segment_size[i]=info.segments[i].memsz;}}
    return SCE_KERNEL_START_SUCCESS;
}
int module_stop(SceSize argc,const void *args){(void)argc;(void)args;return SCE_KERNEL_STOP_SUCCESS;}
