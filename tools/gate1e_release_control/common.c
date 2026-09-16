#include "common.h"
#include <psp2/io/fcntl.h>
int vbe_release_write_file(const char *path, const void *data, uint32_t size) {
    const uint8_t *p=(const uint8_t *)data;
    int fd=sceIoOpen(path,SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0666);
    if(fd<0) return fd;
    while(size){ int r=sceIoWrite(fd,p,size); if(r<=0){sceIoClose(fd); return r<0?r:-1;} p+=r; size-=(uint32_t)r; }
    return sceIoClose(fd);
}
