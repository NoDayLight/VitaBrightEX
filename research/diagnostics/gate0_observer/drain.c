#include <stdint.h>
#include <string.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/threadmgr.h>
#include "gate0_protocol_v7.h"
static VbeTraceRecord records[VBE_TRACE_RECORD_CAPACITY];
static int write_all(int fd,const void *ptr,unsigned n){const uint8_t *p=(const uint8_t*)ptr;while(n){int r=sceIoWrite(fd,p,n);if(r<=0)return -1;p+=r;n-=(unsigned)r;}return 0;}
int main(int argc,char **argv){VbeTraceStatus st;VbeTraceDumpHeader h;uint32_t n=0,i;int fd;(void)argc;(void)argv;
    if(vbeTracePause()<0)return 10;
    for(i=0;i<2000u;i++){if(vbeTraceGetStatus(&st)<0)return 11;if(st.lifecycle==VBE_OBSERVER_PAUSED&&st.active_producers==0u)break;sceKernelDelayThread(1000);}
    if(st.lifecycle!=VBE_OBSERVER_PAUSED||st.active_producers!=0u)return 12;
    if(st.version!=VBE_TRACE_VERSION||st.firmware_version!=VBE_TRACE_FW_365)return 13;
    if(st.missing_required_mask||st.hook_fail_mask||st.lost_records)return 14;
    if(vbeTraceRead(records,VBE_TRACE_RECORD_CAPACITY,&n)<0)return 15;
    memset(&h,0,sizeof(h));h.magic=VBE_TRACE_DUMP_MAGIC;h.version=VBE_TRACE_VERSION;h.header_size=sizeof(h);h.status_size=sizeof(st);h.record_size=sizeof(VbeTraceRecord);h.record_count=n;
    h.required_hook_mask=st.required_hook_mask;h.missing_required_mask=st.missing_required_mask;h.lost_records=st.lost_records;h.ring_epoch=st.ring_epoch;h.lifecycle=st.lifecycle;
    fd=sceIoOpen("ux0:data/vbe_gate0a_trace.bin",SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0666);if(fd<0)return 16;
    if(write_all(fd,&h,sizeof(h))<0||write_all(fd,&st,sizeof(st))<0||write_all(fd,records,n*sizeof(VbeTraceRecord))<0){sceIoClose(fd);return 17;}sceIoClose(fd);
    if(vbeTraceReset(1)<0)return 18;return 0;
}
