#include "common.h"
#include <psp2/io/fcntl.h>

int gate1d_write_file(const char *path, const void *data, uint32_t size) {
    const uint8_t *p = (const uint8_t *)data;
    int fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) return fd;
    while (size != 0u) {
        int ret = sceIoWrite(fd, p, size);
        if (ret <= 0) {
            sceIoClose(fd);
            return ret < 0 ? ret : -1;
        }
        p += ret;
        size -= (uint32_t)ret;
    }
    return sceIoClose(fd);
}
