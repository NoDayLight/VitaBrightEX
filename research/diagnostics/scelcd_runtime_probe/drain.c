#include <stdint.h>
#include <psp2/io/fcntl.h>
#include "probe_protocol.h"
static int write_all(int fd,const void *ptr,unsigned n){const uint8_t *p=(const uint8_t*)ptr;while(n){int r=sceIoWrite(fd,p,n);if(r<=0)return -1;p+=r;n-=(unsigned)r;}return 0;}
int main(int argc,char **argv){VbeSceLcdRuntimeProbe p;int fd,ret;(void)argc;(void)argv;ret=vbeSceLcdRuntimeProbeGet(&p);if(ret<0)return 10;fd=sceIoOpen("ux0:data/vbe_scelcd_runtime_probe.bin",SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0666);if(fd<0)return 11;if(write_all(fd,&p,sizeof(p))<0){sceIoClose(fd);return 12;}sceIoClose(fd);return 0;}
