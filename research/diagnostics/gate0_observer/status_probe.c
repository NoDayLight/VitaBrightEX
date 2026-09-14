#include <stdint.h>
#include <psp2/io/fcntl.h>
#include "gate0_protocol_v7.h"

#define VBE_STATUS_PROBE_MAGIC 0x56535039u
#define VBE_STATUS_PROBE_VERSION 1u

typedef struct VbeStatusProbeDump {
    uint32_t magic;
    uint32_t version;
    int32_t status_return;
    uint32_t reserved;
    VbeTraceStatus status;
} VbeStatusProbeDump;

static void zero_bytes(void *dst, uint32_t n) {
    volatile uint8_t *p = (volatile uint8_t *)dst;
    uint32_t i;
    for (i = 0; i < n; ++i) p[i] = 0;
}

static int write_all(int fd, const void *ptr, unsigned n) {
    const uint8_t *p = (const uint8_t *)ptr;
    while (n) {
        int r = sceIoWrite(fd, p, n);
        if (r <= 0) return -1;
        p += r;
        n -= (unsigned)r;
    }
    return 0;
}

int main(int argc, char **argv) {
    VbeStatusProbeDump dump;
    int fd;
    (void)argc;
    (void)argv;

    zero_bytes(&dump, sizeof(dump));
    dump.magic = VBE_STATUS_PROBE_MAGIC;
    dump.version = VBE_STATUS_PROBE_VERSION;

    /* Read-only diagnostic: no pause/reset/read/mark/quiesce call. */
    dump.status_return = vbeTraceGetStatus(&dump.status);

    fd = sceIoOpen("ux0:data/vbe_gate0a_status_c9.bin",
                   SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) return 20;
    if (write_all(fd, &dump, sizeof(dump)) < 0) {
        sceIoClose(fd);
        return 21;
    }
    sceIoClose(fd);
    return dump.status_return < 0 ? 22 : 0;
}
