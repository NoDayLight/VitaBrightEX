#include <psp2/io/fcntl.h>
#include "gate1b_snapshot_protocol.h"

int main(int argc, char **argv) {
    const char *path = "ux0:data/vbe_gate1b_capture.bin";
    VbeG1bCaptureBundle bundle;
    int fd, ret;
    (void)argc;
    (void)argv;

    ret = vbeG1bGetCaptureBundle(&bundle);
    if (ret < 0) return 10;

    fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) return 11;
    ret = sceIoWrite(fd, &bundle, sizeof(bundle));
    sceIoClose(fd);
    return ret == (int)sizeof(bundle) ? 0 : 12;
}
