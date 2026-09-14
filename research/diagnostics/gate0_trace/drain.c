#include <stdint.h>
#include <string.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/threadmgr.h>
#include "trace_protocol.h"

static VbeTraceRecord records[VBE_TRACE_RECORD_CAPACITY];
static int write_all(int fd,const void *p,unsigned n){const uint8_t *b=(const uint8_t*)p;while(n){int r=sceIoWrite(fd,b,n);if(r<=0)return -1;b+=r;n-=(unsigned)r;}return 0;}
int main(int argc,char **argv){VbeTraceStatus st;VbeTraceSnapshot pre,post;VbeTraceDumpHeader h;uint32_t n=0,i;int fd,partial=0;(void)argc;(void)argv;
 if(vbeTraceStop()<0)return 10;
 for(i=0;i<2000;i++){if(vbeTraceGetStatus(&st)<0)return 11;if(st.active_hooks==0)break;sceKernelDelayThread(1000);}if(st.active_hooks!=0)return 12;
 memset(&pre,0,sizeof(pre));memset(&post,0,sizeof(post));(void)vbeTraceSnapshot(&pre);
 if(vbeTraceRead(records,VBE_TRACE_RECORD_CAPACITY,&n)<0)return 13;(void)vbeTraceSnapshot(&post);
 if(st.lost_records||st.missing_required_mask||st.missing_required_snapshot_mask)partial=1;
 for(i=0;i<n;i++)if(records[i].flags&VBE_TRACE_FLAG_BOUNDS_REJECTED)partial=1;
 memset(&h,0,sizeof(h));h.magic=VBE_TRACE_DUMP_MAGIC;h.version=VBE_TRACE_VERSION;h.header_size=sizeof(h);h.status_size=sizeof(st);h.pre_snapshot_size=sizeof(pre);h.post_snapshot_size=sizeof(post);h.record_size=sizeof(VbeTraceRecord);h.record_count=n;h.capture_quality=partial?VBE_TRACE_CAPTURE_PARTIAL:VBE_TRACE_CAPTURE_AUTHORITATIVE;h.pre_snapshot_present=1;h.affine_required_mask=VBE_TRACE_REQUIRED_AFFINE_HOOKS;h.affine_missing_required_mask=VBE_TRACE_REQUIRED_AFFINE_HOOKS&~st.installed_hook_mask;h.panel_required_mask=VBE_TRACE_REQUIRED_PANEL_HOOKS;h.panel_missing_required_mask=VBE_TRACE_REQUIRED_PANEL_HOOKS&~st.installed_hook_mask;h.affine_required_snapshot_mask=0;h.panel_required_snapshot_mask=0;h.affine_capture_quality=(h.affine_missing_required_mask||st.lost_records)?VBE_TRACE_CAPTURE_PARTIAL:VBE_TRACE_CAPTURE_AUTHORITATIVE;h.panel_capture_quality=(h.panel_missing_required_mask||partial)?VBE_TRACE_CAPTURE_PARTIAL:VBE_TRACE_CAPTURE_AUTHORITATIVE;h.panel_read_validity=VBE_TRACE_PANEL_READ_NONE;
 for(i=0;i<n;i++)if(records[i].event_type==VBE_TRACE_PANEL_READ_ENTER||records[i].event_type==VBE_TRACE_PANEL_READ_EXIT)h.panel_read_validity=VBE_TRACE_PANEL_READ_UNPROVEN_SEEN;
 fd=sceIoOpen("ux0:data/vbe_gate0_trace.bin",SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0666);if(fd<0)return 14;
 if(write_all(fd,&h,sizeof(h))<0||write_all(fd,&st,sizeof(st))<0||write_all(fd,&pre,sizeof(pre))<0||write_all(fd,&post,sizeof(post))<0||write_all(fd,records,n*sizeof(VbeTraceRecord))<0){sceIoClose(fd);return 15;}sceIoClose(fd);
 if(vbeTraceReset(1)<0)return 16;return partial?2:0;
}
