#include <psp2/io/fcntl.h>
#include "gate1a_protocol.h"
int main(int argc,char **argv){VbeG1Status st;int fd,r;(void)argc;(void)argv;r=vbeG1GetStatus(&st);if(r<0)return 10;fd=sceIoOpen("ux0:data/vbe_gate1a_status.bin",SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0666);if(fd<0)return 11;r=sceIoWrite(fd,&st,sizeof(st));sceIoClose(fd);return r==(int)sizeof(st)?0:12;}
