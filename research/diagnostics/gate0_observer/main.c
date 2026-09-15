#include <stddef.h>
#include <stdint.h>
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysmem/data_transfers.h>
#include <psp2kern/kernel/threadmgr/thread.h>
#include <psp2kern/lowio/iftu.h>
#include <taihen.h>

#include "../../../taihen_extra.h"
#include "gate0_protocol_v7.h"
#include "observer_lifecycle_core.h"
#include "panel_relocation_signature_core.h"
#include "retail_365_layout.h"
#include "trace_ring_core.h"

#define NID_IFTU_CSC_A 0x0FCBF457u
#define NID_IFTU_CSC_B 0xD64F4C6Bu
#define NID_IFTU_ENABLE 0x0D7C02F7u
#define VBE_MODULEMGR_FOR_KERNEL_365_NID 0x92C9FFC2u
#define VBE_GET_MODULE_INFO_365_NID 0xDAA90093u

typedef struct VbeLcdSegmentRange { uintptr_t base; uint32_t size; } VbeLcdSegmentRange;
typedef int (*VbeKernelGetModuleInfoFn)(SceUID pid,SceUID modid,SceKernelModuleInfo *info);

static VbeTraceRecord g_records[VBE_TRACE_RECORD_CAPACITY];
static VbeTraceRingCore g_ring;
static VbeObserverLifecycleCore g_lifecycle;
static VbeLcdSegmentRange g_lcd_segments[4];
static volatile uint32_t g_sequence, g_completion_sequence, g_invocation_sequence;
static volatile uint32_t g_hook_fail_mask, g_ring_epoch;
static uint32_t g_firmware_version;
static tai_hook_ref_t g_ref_csc_a, g_ref_csc_b, g_ref_iftu_enable, g_ref_panel_write, g_ref_panel_read;

static void zero_bytes(void *dst,uint32_t n){volatile uint8_t *p=(volatile uint8_t*)dst;uint32_t i;for(i=0;i<n;i++)p[i]=0;}
static void copy_csc(uint8_t *dst,const volatile uint8_t *src){uint32_t i;for(i=0;i<VBE_TRACE_CSC_PAYLOAD_SIZE;i++)dst[i]=src[i];}
static uint32_t trace_tid(void){int x=ksceKernelGetThreadId();return x<0?0u:(uint32_t)x;}
static uint32_t next_invocation(void){return __sync_add_and_fetch(&g_invocation_sequence,1u);}

static VbeTraceRecord *reserve_record(int capture,uint16_t event,int plane,uint32_t thread,uint32_t invocation){
    uint32_t ticket,slot;VbeTraceRecord *r;
    if(!capture)return NULL;
    if(!vbe_trace_ring_reserve(&g_ring,VBE_TRACE_RECORD_CAPACITY,&ticket,&slot))return NULL;
    r=&g_records[slot];zero_bytes(r,sizeof(*r));
    r->sequence=__sync_add_and_fetch(&g_sequence,1u);r->thread_id=thread;r->invocation_id=invocation;
    r->event_type=event;r->plane=(int16_t)plane;r->lost_snapshot=g_ring.lost;r->ring_epoch=g_ring_epoch;(void)ticket;
    return r;
}
static void commit_record(VbeTraceRecord *r){if(!r)return;r->completion_sequence=__sync_add_and_fetch(&g_completion_sequence,1u);__sync_synchronize();r->committed=VBE_TRACE_COMMITTED;}
static void finish_record(VbeTraceRecord *r,int32_t raw){if(!r)return;r->raw_return=raw;r->flags|=VBE_TRACE_FLAG_RETURN_VALID;commit_record(r);}

static uint32_t panel_pointer_provenance(const void *ptr,uint32_t len,uint32_t *range_overflow){
    uintptr_t start=(uintptr_t)ptr,end;uint32_t i;*range_overflow=0u;
    if(len==0u)return VBE_PTR_UNPROVEN;
    if(start==0u)return VBE_PTR_INVALID_RANGE;
    end=start+(uintptr_t)len;
    if(end<start){*range_overflow=1u;return VBE_PTR_INVALID_RANGE;}
    for(i=0;i<4u;i++){
        uintptr_t base=g_lcd_segments[i].base,limit;
        if(base==0u||g_lcd_segments[i].size==0u)continue;
        limit=base+(uintptr_t)g_lcd_segments[i].size;if(limit<base)continue;
        if(start>=base&&end<=limit)return VBE_PTR_SCE_LCD_SEGMENT_0+i;
    }
    return VBE_PTR_UNPROVEN;
}

static void classify_panel_record(VbeTraceRecord *r,const void *ptr,uint32_t len){
    uint32_t overflow=0u,prov;
    r->pointer_value=(uint32_t)(uintptr_t)ptr;
    prov=panel_pointer_provenance(ptr,len,&overflow);r->pointer_provenance=prov;
    if(len==0u){r->payload_state=VBE_PAYLOAD_ZERO_LENGTH;return;}
    if(!ptr){r->flags|=VBE_TRACE_FLAG_NULL;r->payload_state=VBE_PAYLOAD_NULL_NONZERO;return;}
    if(prov==VBE_PTR_INVALID_RANGE){if(overflow)r->flags|=VBE_TRACE_FLAG_RANGE_OVERFLOW;r->payload_state=VBE_PAYLOAD_INVALID_RANGE;return;}
    r->payload_state=VBE_PAYLOAD_NOT_CAPTURED_GATE0A;
    /* Gate-0A deliberately never dereferences the panel range. */
}

static VbeTraceRecord *prepare_csc(int capture,uint16_t event,int plane,const SceIftuCscParams *params,uint32_t thread,uint32_t invocation){
    VbeTraceRecord *r=reserve_record(capture,event,plane,thread,invocation);if(!r)return NULL;
    r->pointer_value=(uint32_t)(uintptr_t)params;
    if(plane<0||plane>=5){r->flags|=VBE_TRACE_FLAG_INVALID_PLANE;r->payload_state=VBE_PAYLOAD_CSC_INVALID_PLANE;return r;}
    if(!params){r->flags|=VBE_TRACE_FLAG_NULL;r->payload_state=VBE_PAYLOAD_CSC_NULL;return r;}
    r->payload_state=VBE_PAYLOAD_CSC_EXACT_3C;r->payload_length=VBE_TRACE_CSC_PAYLOAD_SIZE;
    copy_csc(r->payload,(const volatile uint8_t*)params);return r;
}

static int hook_csc_a(int plane,const SceIftuCscParams *params){
    int capture=vbe_observer_core_producer_enter(&g_lifecycle);uint32_t t=trace_tid(),i=next_invocation();
    VbeTraceRecord *r=prepare_csc(capture,VBE_TRACE_CSC_A,plane,params,t,i);int ret=TAI_CONTINUE(int,g_ref_csc_a,plane,params);
    finish_record(r,ret);vbe_observer_core_producer_leave(&g_lifecycle);return ret;
}
static int hook_csc_b(int plane,const SceIftuCscParams *params){
    int capture=vbe_observer_core_producer_enter(&g_lifecycle);uint32_t t=trace_tid(),i=next_invocation();
    VbeTraceRecord *r=prepare_csc(capture,VBE_TRACE_CSC_B,plane,params,t,i);int ret=TAI_CONTINUE(int,g_ref_csc_b,plane,params);
    finish_record(r,ret);vbe_observer_core_producer_leave(&g_lifecycle);return ret;
}
static int hook_iftu_enable(int plane){
    int capture=vbe_observer_core_producer_enter(&g_lifecycle);uint32_t t=trace_tid(),i=next_invocation();int ret;VbeTraceRecord *r;
    r=reserve_record(capture,VBE_TRACE_IFTU_ENABLE_ENTER,plane,t,i);if(r)commit_record(r);
    ret=TAI_CONTINUE(int,g_ref_iftu_enable,plane);
    r=reserve_record(capture,VBE_TRACE_IFTU_ENABLE_EXIT,plane,t,i);finish_record(r,ret);
    vbe_observer_core_producer_leave(&g_lifecycle);return ret;
}
static int hook_panel_write(unsigned command,const void *ptr,unsigned len){
    int capture=vbe_observer_core_producer_enter(&g_lifecycle);uint32_t t=trace_tid(),i=next_invocation();
    VbeTraceRecord *r=reserve_record(capture,VBE_TRACE_PANEL_WRITE,-1,t,i);int ret;
    if(r){r->arg0=command;r->arg1=len;classify_panel_record(r,ptr,len);}
    ret=TAI_CONTINUE(int,g_ref_panel_write,command,ptr,len);finish_record(r,ret);
    vbe_observer_core_producer_leave(&g_lifecycle);return ret;
}
static int hook_panel_read(unsigned command,void *ptr,unsigned len){
    int capture=vbe_observer_core_producer_enter(&g_lifecycle);uint32_t t=trace_tid(),i=next_invocation();int ret;VbeTraceRecord *r;
    r=reserve_record(capture,VBE_TRACE_PANEL_READ_ENTER,-1,t,i);if(r){r->arg0=command;r->arg1=len;classify_panel_record(r,ptr,len);commit_record(r);}
    ret=TAI_CONTINUE(int,g_ref_panel_read,command,ptr,len);
    r=reserve_record(capture,VBE_TRACE_PANEL_READ_EXIT,-1,t,i);if(r){r->arg0=command;r->arg1=len;classify_panel_record(r,ptr,len);}finish_record(r,ret);
    vbe_observer_core_producer_leave(&g_lifecycle);return ret;
}

static int install_export(tai_hook_ref_t *ref,const char *module,uint32_t nid,const void *hook,uint32_t bit,uint32_t failbit){
    SceUID uid=taiHookFunctionExportForKernel(KERNEL_PID,ref,module,TAI_ANY_LIBRARY,nid,hook);
    if(uid<0){g_hook_fail_mask|=failbit;vbe_observer_core_install_failed(&g_lifecycle);return -1;}
    vbe_observer_core_note_hook(&g_lifecycle,bit);return 0;
}
static int install_offset(tai_hook_ref_t *ref,SceUID modid,uint32_t offset,const void *hook,uint32_t bit,uint32_t failbit){
    SceUID uid=taiHookFunctionOffsetForKernel(KERNEL_PID,ref,modid,VBE_LCD_PANEL_SEGMENT,offset,1,hook);
    if(uid<0){g_hook_fail_mask|=failbit;vbe_observer_core_install_failed(&g_lifecycle);return -1;}
    vbe_observer_core_note_hook(&g_lifecycle,bit);return 0;
}

static int prepare_lcd(tai_module_info_t *lcd){
    uintptr_t writer=0u,reader=0u,module_info_addr=0u;
    VbeKernelGetModuleInfoFn get_module_info;
    SceKernelModuleInfo info;
    uint32_t i,segment1_target;
    int ret;
    ret=module_get_export_func(KERNEL_PID,"SceKernelModulemgr",VBE_MODULEMGR_FOR_KERNEL_365_NID,VBE_GET_MODULE_INFO_365_NID,&module_info_addr);
    if(ret<0||module_info_addr==0u){g_hook_fail_mask|=VBE_TRACE_FAIL_LCD_INFO;return -1;}
    get_module_info=(VbeKernelGetModuleInfoFn)module_info_addr;
    zero_bytes(lcd,sizeof(*lcd));lcd->size=sizeof(*lcd);ret=taiGetModuleInfoForKernel(KERNEL_PID,"SceLcd",lcd);if(ret<0){g_hook_fail_mask|=VBE_TRACE_FAIL_LCD_INFO;return -1;}
    zero_bytes(&info,sizeof(info));info.size=sizeof(info);ret=get_module_info(KERNEL_PID,lcd->modid,&info);if(ret<0){g_hook_fail_mask|=VBE_TRACE_FAIL_LCD_INFO;return -1;}
    for(i=0;i<4u;i++){g_lcd_segments[i].base=(uintptr_t)info.segments[i].vaddr;g_lcd_segments[i].size=info.segments[i].memsz;}
    if(!vbe_segment_target32(g_lcd_segments[1].base,g_lcd_segments[1].size,&segment1_target)){g_hook_fail_mask|=VBE_TRACE_FAIL_LCD_INFO;return -1;}
    ret=module_get_offset(KERNEL_PID,lcd->modid,VBE_LCD_PANEL_SEGMENT,VBE_LCD_PANEL_WRITER_OFFSET,&writer);
    if(ret<0||!vbe_relocation_normalized_signature((const volatile uint8_t*)writer,vbe_lcd_panel_writer_signature,7u,segment1_target)){g_hook_fail_mask|=VBE_TRACE_FAIL_PANEL_WRITE_SIG;return -1;}
    ret=module_get_offset(KERNEL_PID,lcd->modid,VBE_LCD_PANEL_SEGMENT,VBE_LCD_PANEL_READER_OFFSET,&reader);
    if(ret<0||!vbe_relocation_normalized_signature((const volatile uint8_t*)reader,vbe_lcd_panel_reader_signature,6u,segment1_target)){g_hook_fail_mask|=VBE_TRACE_FAIL_PANEL_READ_SIG;return -1;}
    return 0;
}

int vbeTraceGetStatus(VbeTraceStatus *out){
    VbeTraceStatus s;uint32_t cs;int ret;if(!out)return VBE_TRACE_ERR_INVALID;zero_bytes(&s,sizeof(s));
    s.magic=VBE_TRACE_MAGIC;s.version=VBE_TRACE_VERSION;s.firmware_version=g_firmware_version;s.lifecycle=g_lifecycle.state;s.record_capacity=VBE_TRACE_RECORD_CAPACITY;
    s.slots_reserved=vbe_trace_ring_count(&g_ring,VBE_TRACE_RECORD_CAPACITY);s.committed_records=g_completion_sequence;s.lost_records=g_ring.lost;s.last_sequence=g_sequence;
    s.active_producers=g_lifecycle.active_producers;s.owned_hook_mask=g_lifecycle.owned_hook_mask;s.required_hook_mask=g_lifecycle.required_hook_mask;
    s.missing_required_mask=g_lifecycle.required_hook_mask&~g_lifecycle.owned_hook_mask;s.hook_fail_mask=g_hook_fail_mask;s.ring_epoch=g_ring_epoch;
    s.install_publication_class=VBE_TRACE_INSTALL_PUBLICATION_UPSTREAM_RESIDUAL;s.flags=VBE_TRACE_STATUS_REBOOT_OWNED;
    ENTER_SYSCALL(cs);ret=ksceKernelMemcpyKernelToUser(out,&s,sizeof(s));EXIT_SYSCALL(cs);return ret;
}
int vbeTracePause(void){return vbe_observer_core_pause(&g_lifecycle)?0:VBE_TRACE_ERR_STATE;}
int vbeTraceReset(int enable_after_reset){uint32_t n;
    if(!vbe_observer_core_stable_paused(&g_lifecycle))return VBE_TRACE_ERR_BUSY;
    for(n=0;n<VBE_TRACE_RECORD_CAPACITY;n++)zero_bytes(&g_records[n],sizeof(g_records[n]));
    vbe_trace_ring_consume_epoch(&g_ring);g_sequence=0u;g_completion_sequence=0u;g_invocation_sequence=0u;g_ring_epoch++;
    __sync_synchronize();if(enable_after_reset&&!vbe_observer_core_resume(&g_lifecycle))return VBE_TRACE_ERR_STATE;return 0;
}
int vbeTraceRead(VbeTraceRecord *out,uint32_t capacity,uint32_t *written){
    uint32_t cs,n,i,start;int ret=0;if(!out||!written)return VBE_TRACE_ERR_INVALID;if(!vbe_observer_core_stable_paused(&g_lifecycle))return VBE_TRACE_ERR_BUSY;
    n=vbe_trace_ring_count(&g_ring,VBE_TRACE_RECORD_CAPACITY);if(n>capacity)n=capacity;start=g_ring.read_ticket;ENTER_SYSCALL(cs);
    for(i=0;i<n&&ret>=0;i++)ret=ksceKernelMemcpyKernelToUser(&out[i],&g_records[(start+i)%VBE_TRACE_RECORD_CAPACITY],sizeof(VbeTraceRecord));
    if(ret>=0){
        ret=ksceKernelMemcpyKernelToUser(written,&n,sizeof(n));
    }
    EXIT_SYSCALL(cs);
    return ret;
}
int vbeTraceMark(uint32_t marker){
    int capture;VbeTraceRecord *r;if(marker<1u||marker>VBE_MARK_MAX)return VBE_TRACE_ERR_INVALID;
    capture=vbe_observer_core_producer_enter(&g_lifecycle);if(!capture){vbe_observer_core_producer_leave(&g_lifecycle);return VBE_TRACE_ERR_STATE;}
    r=reserve_record(1,VBE_TRACE_MARKER,-1,trace_tid(),next_invocation());if(!r){vbe_observer_core_producer_leave(&g_lifecycle);return VBE_TRACE_ERR_DROPPED;}
    r->arg0=marker;commit_record(r);vbe_observer_core_producer_leave(&g_lifecycle);return 0;
}
int vbeTraceQuiesce(void){vbe_observer_core_quiesce(&g_lifecycle);return 0;}

void _start() __attribute__((weak,alias("module_start")));
int module_start(SceSize argc,const void *args){SceKernelFwInfo fw;tai_module_info_t lcd;(void)argc;(void)args;
    zero_bytes(g_records,sizeof(g_records));zero_bytes(g_lcd_segments,sizeof(g_lcd_segments));vbe_trace_ring_init(&g_ring);vbe_observer_core_init(&g_lifecycle,VBE_TRACE_REQUIRED_HOOKS);
    g_sequence=g_completion_sequence=g_invocation_sequence=0u;g_hook_fail_mask=0u;g_ring_epoch=1u;g_firmware_version=0u;zero_bytes(&fw,sizeof(fw));fw.size=sizeof(fw);
    if(ksceKernelGetSystemSwVersion(&fw)<0||fw.version!=VBE_TRACE_FW_365){g_hook_fail_mask|=VBE_TRACE_FAIL_UNSUPPORTED_FW;vbe_observer_core_install_failed(&g_lifecycle);return SCE_KERNEL_START_SUCCESS;}
    g_firmware_version=fw.version;if(prepare_lcd(&lcd)<0){vbe_observer_core_install_failed(&g_lifecycle);return SCE_KERNEL_START_SUCCESS;}
    if(install_export(&g_ref_csc_a,"SceLowio",NID_IFTU_CSC_A,hook_csc_a,VBE_TRACE_HOOK_CSC_A,VBE_TRACE_FAIL_CSC_A)<0)return SCE_KERNEL_START_SUCCESS;
    if(install_export(&g_ref_csc_b,"SceLowio",NID_IFTU_CSC_B,hook_csc_b,VBE_TRACE_HOOK_CSC_B,VBE_TRACE_FAIL_CSC_B)<0)return SCE_KERNEL_START_SUCCESS;
    if(install_export(&g_ref_iftu_enable,"SceLowio",NID_IFTU_ENABLE,hook_iftu_enable,VBE_TRACE_HOOK_IFTU_ENABLE,VBE_TRACE_FAIL_IFTU_ENABLE)<0)return SCE_KERNEL_START_SUCCESS;
    if(install_offset(&g_ref_panel_write,lcd.modid,VBE_LCD_PANEL_WRITER_OFFSET,hook_panel_write,VBE_TRACE_HOOK_PANEL_WRITE,VBE_TRACE_FAIL_PANEL_WRITE)<0)return SCE_KERNEL_START_SUCCESS;
    if(install_offset(&g_ref_panel_read,lcd.modid,VBE_LCD_PANEL_READER_OFFSET,hook_panel_read,VBE_TRACE_HOOK_PANEL_READ,VBE_TRACE_FAIL_PANEL_READ)<0)return SCE_KERNEL_START_SUCCESS;
    if(!vbe_observer_core_start_capture(&g_lifecycle))vbe_observer_core_install_failed(&g_lifecycle);
    return SCE_KERNEL_START_SUCCESS;
}
int module_stop(SceSize argc,const void *args){(void)argc;(void)args;vbe_observer_core_quiesce(&g_lifecycle);return vbe_observer_core_can_unload(&g_lifecycle)?SCE_KERNEL_STOP_SUCCESS:SCE_KERNEL_STOP_FAIL;}
