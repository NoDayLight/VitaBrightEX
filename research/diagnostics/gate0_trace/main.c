#include <stdint.h>
#include <stddef.h>
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysmem/data_transfers.h>
#include <psp2kern/kernel/threadmgr/thread.h>
#include <psp2kern/lowio/iftu.h>
#include <taihen.h>

#include "../../../taihen_extra.h"
#include "layout_365.h"
#include "trace_protocol.h"
#include "trace_ring_core.h"

#define NID_IFTU_CSC_A 0x0FCBF457u
#define NID_IFTU_CSC_B 0xD64F4C6Bu

static VbeTraceRecord g_records[VBE_TRACE_RECORD_CAPACITY];
static VbeTraceRingCore g_ring;
static volatile uint32_t g_enabled, g_sequence, g_completion_sequence;
static volatile uint32_t g_invocation_sequence, g_active_hooks;
static volatile uint32_t g_hook_fail_mask, g_installed_hook_mask;
static uint32_t g_firmware_version;
static tai_hook_ref_t g_ref_csc_a, g_ref_csc_b, g_ref_panel_write, g_ref_panel_read;
static SceUID g_hook_csc_a=-1, g_hook_csc_b=-1, g_hook_panel_write=-1, g_hook_panel_read=-1;

static void copy_bytes(uint8_t *dst,const volatile uint8_t *src,uint32_t n){uint32_t i;for(i=0;i<n;i++)dst[i]=src[i];}
static void zero_bytes(void *dst,uint32_t n){volatile uint8_t *p=(volatile uint8_t*)dst;uint32_t i;for(i=0;i<n;i++)p[i]=0;}
static int exact_signature(const volatile uint8_t *a,const uint8_t *e,uint32_t n){uint32_t i;if(!a||!e)return 0;for(i=0;i<n;i++)if(a[i]!=e[i])return 0;return 1;}
static uint32_t hook_enter(void){__sync_add_and_fetch(&g_active_hooks,1u);__sync_synchronize();return g_enabled!=0u;}
static void hook_leave(void){__sync_synchronize();__sync_sub_and_fetch(&g_active_hooks,1u);}
static uint32_t tid(void){int x=ksceKernelGetThreadId();return x<0?0u:(uint32_t)x;}
static uint32_t inv(void){return __sync_add_and_fetch(&g_invocation_sequence,1u);}

static VbeTraceRecord *reserve_record(uint32_t capture,uint16_t event,int plane,uint32_t thread,uint32_t invocation){
 uint32_t ticket,slot;VbeTraceRecord *r;if(!capture)return NULL;
 if(!vbe_trace_ring_reserve(&g_ring,VBE_TRACE_RECORD_CAPACITY,&ticket,&slot))return NULL;
 r=&g_records[slot];r->committed=0;r->sequence=__sync_add_and_fetch(&g_sequence,1u);r->completion_sequence=0;
 r->thread_id=thread;r->invocation_id=invocation;r->event_type=event;r->plane=(int16_t)plane;r->flags=0;r->raw_return=0;
 r->arg0=0;r->arg1=0;r->payload_length=0;r->lost_snapshot=g_ring.lost;(void)ticket;return r;
}
static void commit_record(VbeTraceRecord *r){if(!r)return;r->completion_sequence=__sync_add_and_fetch(&g_completion_sequence,1u);__sync_synchronize();r->committed=VBE_TRACE_COMMITTED;}
static void finish(VbeTraceRecord *r,int32_t ret){if(!r)return;r->raw_return=ret;r->flags|=VBE_TRACE_FLAG_RETURN_VALID;commit_record(r);}

static VbeTraceRecord *prepare_csc(uint32_t capture,uint16_t event,int plane,const SceIftuCscParams *p,uint32_t thread){
 VbeTraceRecord *r=reserve_record(capture,event,plane,thread,0);if(!r)return NULL;
 if(!p){r->flags|=VBE_TRACE_FLAG_NULL;return r;}
 if(plane<0||plane>=5){r->flags|=VBE_TRACE_FLAG_BOUNDS_REJECTED;return r;}
 r->payload_length=VBE_TRACE_CSC_PAYLOAD_SIZE;copy_bytes(r->payload,(const volatile uint8_t*)p,VBE_TRACE_CSC_PAYLOAD_SIZE);return r;
}
static VbeTraceRecord *prepare_writer(uint32_t capture,unsigned command,const void *p,unsigned len,uint32_t thread){
 VbeTraceRecord *r=reserve_record(capture,VBE_TRACE_PANEL_WRITE,-1,thread,0);if(!r)return NULL;r->arg0=command&0xffu;r->arg1=len;
 if(len==0)return r;if(!p){r->flags|=VBE_TRACE_FLAG_NULL|VBE_TRACE_FLAG_BOUNDS_REJECTED;return r;}
 if(len>VBE_TRACE_PANEL_WRITE_MAX){r->flags|=VBE_TRACE_FLAG_BOUNDS_REJECTED;return r;}
 r->payload_length=len;copy_bytes(r->payload,(const volatile uint8_t*)p,len);return r;
}
static void reader_event(uint32_t capture,uint16_t event,unsigned command,unsigned len,int32_t ret,uint32_t thread,uint32_t invocation,int have_ret){
 VbeTraceRecord *r=reserve_record(capture,event,-1,thread,invocation);if(!r)return;r->arg0=command&0xffu;r->arg1=len;r->flags|=VBE_TRACE_FLAG_READ_NOT_CAPTURED_UNPROVEN;
 if(have_ret)finish(r,ret);else commit_record(r);
}

static int hook_csc_a(int plane,const SceIftuCscParams *p){uint32_t c=hook_enter(),t=tid();VbeTraceRecord *r=prepare_csc(c,VBE_TRACE_CSC_A,plane,p,t);int ret=TAI_CONTINUE(int,g_ref_csc_a,plane,p);finish(r,ret);hook_leave();return ret;}
static int hook_csc_b(int plane,const SceIftuCscParams *p){uint32_t c=hook_enter(),t=tid();VbeTraceRecord *r=prepare_csc(c,VBE_TRACE_CSC_B,plane,p,t);int ret=TAI_CONTINUE(int,g_ref_csc_b,plane,p);finish(r,ret);hook_leave();return ret;}
static int hook_panel_write(unsigned command,const void *p,unsigned len){uint32_t c=hook_enter(),t=tid();VbeTraceRecord *r=prepare_writer(c,command,p,len,t);int ret=TAI_CONTINUE(int,g_ref_panel_write,command,p,len);finish(r,ret);hook_leave();return ret;}
static int hook_panel_read(unsigned command,void *p,unsigned len){uint32_t c=hook_enter(),t=tid(),i=inv();int ret;reader_event(c,VBE_TRACE_PANEL_READ_ENTER,command,len,0,t,i,0);ret=TAI_CONTINUE(int,g_ref_panel_read,command,p,len);reader_event(c,VBE_TRACE_PANEL_READ_EXIT,command,len,ret,t,i,1);hook_leave();return ret;}

static SceUID install_export(tai_hook_ref_t *r,const char *module,uint32_t nid,const void *hook,uint32_t bit,uint32_t fail){SceUID u=taiHookFunctionExportForKernel(KERNEL_PID,r,module,TAI_ANY_LIBRARY,nid,hook);if(u<0){g_hook_fail_mask|=fail;return -1;}g_installed_hook_mask|=bit;return u;}
static int setup_panel(void){tai_module_info_t lcd;uintptr_t w=0,r=0;int ret;lcd.size=sizeof(lcd);ret=taiGetModuleInfoForKernel(KERNEL_PID,"SceLcd",&lcd);if(ret<0){g_hook_fail_mask|=VBE_TRACE_FAIL_PANEL_WRITE|VBE_TRACE_FAIL_PANEL_READ;return ret;}
 ret=module_get_offset(KERNEL_PID,lcd.modid,VBE_LCD_PANEL_SEGMENT,VBE_LCD_PANEL_WRITER_OFFSET,&w);if(ret<0||!exact_signature((const volatile uint8_t*)w,vbe_lcd_panel_writer_signature,VBE_LCD_PRIVATE_SIGNATURE_LENGTH))g_hook_fail_mask|=VBE_TRACE_FAIL_PANEL_WRITE_SIG;else{g_hook_panel_write=taiHookFunctionOffsetForKernel(KERNEL_PID,&g_ref_panel_write,lcd.modid,VBE_LCD_PANEL_SEGMENT,VBE_LCD_PANEL_WRITER_OFFSET,1,hook_panel_write);if(g_hook_panel_write<0){g_hook_panel_write=-1;g_hook_fail_mask|=VBE_TRACE_FAIL_PANEL_WRITE;}else g_installed_hook_mask|=VBE_TRACE_HOOK_PANEL_WRITE;}
 ret=module_get_offset(KERNEL_PID,lcd.modid,VBE_LCD_PANEL_SEGMENT,VBE_LCD_PANEL_READER_OFFSET,&r);if(ret<0||!exact_signature((const volatile uint8_t*)r,vbe_lcd_panel_reader_signature,VBE_LCD_PRIVATE_SIGNATURE_LENGTH))g_hook_fail_mask|=VBE_TRACE_FAIL_PANEL_READ_SIG;else{g_hook_panel_read=taiHookFunctionOffsetForKernel(KERNEL_PID,&g_ref_panel_read,lcd.modid,VBE_LCD_PANEL_SEGMENT,VBE_LCD_PANEL_READER_OFFSET,1,hook_panel_read);if(g_hook_panel_read<0){g_hook_panel_read=-1;g_hook_fail_mask|=VBE_TRACE_FAIL_PANEL_READ;}else g_installed_hook_mask|=VBE_TRACE_HOOK_PANEL_READ;}return 0;}
static int release_hook(SceUID *u,tai_hook_ref_t r,uint32_t bit){int ret;if(*u<0)return 0;ret=taiHookReleaseForKernel(*u,r);if(ret<0)return ret;*u=-1;g_installed_hook_mask&=~bit;return 0;}
static uint32_t committed_count(void){uint32_t n=vbe_trace_ring_count(&g_ring,VBE_TRACE_RECORD_CAPACITY),i,c=0,start=g_ring.read_ticket;for(i=0;i<n;i++)if(g_records[(start+i)%VBE_TRACE_RECORD_CAPACITY].committed==VBE_TRACE_COMMITTED)c++;return c;}

int vbeTraceGetStatus(VbeTraceStatus *out){VbeTraceStatus s;uint32_t cs;int ret;if(!out)return VBE_TRACE_ERR_INVALID;zero_bytes(&s,sizeof(s));s.magic=VBE_TRACE_MAGIC;s.version=VBE_TRACE_VERSION;s.firmware_version=g_firmware_version;s.enabled=g_enabled;s.record_capacity=VBE_TRACE_RECORD_CAPACITY;s.slots_reserved=vbe_trace_ring_count(&g_ring,VBE_TRACE_RECORD_CAPACITY);s.committed_records=committed_count();s.lost_records=g_ring.lost;s.last_sequence=g_sequence;s.active_hooks=g_active_hooks;s.installed_hook_mask=g_installed_hook_mask;s.required_hook_mask=VBE_TRACE_REQUIRED_HOOKS;s.missing_required_mask=VBE_TRACE_REQUIRED_HOOKS&~g_installed_hook_mask;s.required_snapshot_mask=0;s.missing_required_snapshot_mask=0;s.hook_fail_mask=g_hook_fail_mask;ENTER_SYSCALL(cs);ret=ksceKernelMemcpyKernelToUser(out,&s,sizeof(s));EXIT_SYSCALL(cs);return ret;}
int vbeTraceStop(void){g_enabled=0;__sync_synchronize();return 0;}
int vbeTraceReset(int enable){uint32_t i;if(g_enabled!=0u)return VBE_TRACE_ERR_BUSY;if(g_active_hooks!=0u)return VBE_TRACE_ERR_BUSY;for(i=0;i<VBE_TRACE_RECORD_CAPACITY;i++)g_records[i].committed=0;vbe_trace_ring_consume_epoch(&g_ring);g_sequence=0;g_completion_sequence=0;g_invocation_sequence=0;__sync_synchronize();g_enabled=enable?1u:0u;return 0;}
int vbeTraceRead(VbeTraceRecord *out,uint32_t capacity,uint32_t *written){uint32_t cs,n,i,start;int ret=0;if(!out||!written)return VBE_TRACE_ERR_INVALID;if(g_enabled||g_active_hooks)return VBE_TRACE_ERR_BUSY;n=vbe_trace_ring_count(&g_ring,VBE_TRACE_RECORD_CAPACITY);if(n>capacity)n=capacity;start=g_ring.read_ticket;ENTER_SYSCALL(cs);for(i=0;i<n&&ret>=0;i++)ret=ksceKernelMemcpyKernelToUser(&out[i],&g_records[(start+i)%VBE_TRACE_RECORD_CAPACITY],sizeof(VbeTraceRecord));if(ret>=0)ret=ksceKernelMemcpyKernelToUser(written,&n,sizeof(n));EXIT_SYSCALL(cs);return ret;}
int vbeTraceSnapshot(VbeTraceSnapshot *out){VbeTraceSnapshot s;uint32_t cs;int ret;if(!out)return VBE_TRACE_ERR_INVALID;if(g_enabled||g_active_hooks)return VBE_TRACE_ERR_BUSY;zero_bytes(&s,sizeof(s));s.magic=VBE_TRACE_MAGIC;s.version=VBE_TRACE_VERSION;s.firmware_version=g_firmware_version;s.flags=VBE_TRACE_SNAPSHOT_STABLE;ENTER_SYSCALL(cs);ret=ksceKernelMemcpyKernelToUser(out,&s,sizeof(s));EXIT_SYSCALL(cs);return ret;}
int vbeTraceMark(uint32_t marker){VbeTraceRecord *r;if(marker<1u||marker>VBE_MARK_MAX)return VBE_TRACE_ERR_INVALID;r=reserve_record(g_enabled,VBE_TRACE_MARKER,-1,tid(),0);if(!r)return VBE_TRACE_ERR_DROPPED;r->arg0=marker;commit_record(r);return 0;}

void _start() __attribute__((weak,alias("module_start")));
int module_start(SceSize argc,const void *args){SceKernelFwInfo fw;(void)argc;(void)args;zero_bytes(g_records,sizeof(g_records));vbe_trace_ring_init(&g_ring);g_enabled=0;g_sequence=g_completion_sequence=g_invocation_sequence=g_active_hooks=0;g_hook_fail_mask=g_installed_hook_mask=0;fw.size=sizeof(fw);if(ksceKernelGetSystemSwVersion(&fw)<0){g_hook_fail_mask=VBE_TRACE_FAIL_UNSUPPORTED_FW;return SCE_KERNEL_START_SUCCESS;}g_firmware_version=fw.version;if(fw.version!=VBE_TRACE_FW_365){g_hook_fail_mask=VBE_TRACE_FAIL_UNSUPPORTED_FW;return SCE_KERNEL_START_SUCCESS;}(void)setup_panel();g_hook_csc_a=install_export(&g_ref_csc_a,"SceLowio",NID_IFTU_CSC_A,hook_csc_a,VBE_TRACE_HOOK_CSC_A,VBE_TRACE_FAIL_CSC_A);g_hook_csc_b=install_export(&g_ref_csc_b,"SceLowio",NID_IFTU_CSC_B,hook_csc_b,VBE_TRACE_HOOK_CSC_B,VBE_TRACE_FAIL_CSC_B);if((g_installed_hook_mask&VBE_TRACE_REQUIRED_HOOKS)==VBE_TRACE_REQUIRED_HOOKS)g_enabled=1;return SCE_KERNEL_START_SUCCESS;}
int module_stop(SceSize argc,const void *args){int failed=0;(void)argc;(void)args;g_enabled=0;__sync_synchronize();if(g_active_hooks)return SCE_KERNEL_STOP_FAIL;if(release_hook(&g_hook_panel_read,g_ref_panel_read,VBE_TRACE_HOOK_PANEL_READ)<0)failed=1;if(release_hook(&g_hook_panel_write,g_ref_panel_write,VBE_TRACE_HOOK_PANEL_WRITE)<0)failed=1;if(release_hook(&g_hook_csc_b,g_ref_csc_b,VBE_TRACE_HOOK_CSC_B)<0)failed=1;if(release_hook(&g_hook_csc_a,g_ref_csc_a,VBE_TRACE_HOOK_CSC_A)<0)failed=1;return failed?SCE_KERNEL_STOP_FAIL:SCE_KERNEL_STOP_SUCCESS;}
