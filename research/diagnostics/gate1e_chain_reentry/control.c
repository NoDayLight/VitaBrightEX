#include <stdint.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include "gate1e_chain_protocol.h"

#define OUT_PATH "ux0:data/vbe_gate1e_chain_identity.bin"

static void zero_bytes(void *dst, uint32_t n) {
    uint8_t *p = (uint8_t *)dst;
    uint32_t i;
    for (i = 0; i < n; ++i) p[i] = 0u;
}

static int write_all(const char *path, const void *data, uint32_t size) {
    int fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    const uint8_t *p = (const uint8_t *)data;
    uint32_t done = 0u;
    int n;
    if (fd < 0) return fd;
    while (done < size) {
        n = sceIoWrite(fd, p + done, size - done);
        if (n <= 0) { sceIoClose(fd); return n < 0 ? n : -1; }
        done += (uint32_t)n;
    }
    sceIoClose(fd);
    return 0;
}

int main(void) {
    VbeG1eIdentityBundle b;
    int ret;
    zero_bytes(&b, sizeof(b));
    b.magic = VBE_G1E_MAGIC;
    b.version = VBE_G1E_VERSION;
    b.before_status_return = vitabrightMatrixGetStatus(&b.before);
    b.replay_return = vbeG1eReplayIdentity();
    b.observer_status_return = vbeG1eGetStatus(&b.observer);
    b.after_status_return = vitabrightMatrixGetStatus(&b.after);
    ret = write_all(OUT_PATH, &b, sizeof(b));
    sceKernelExitProcess(ret < 0 ? 1 : 0);
    return 0;
}
