#include <psp2/io/fcntl.h>
#include "gate1b_snapshot_protocol.h"

static int exists(const char *path) {
    int fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return 0;
    sceIoClose(fd);
    return 1;
}

int main(int argc, char **argv) {
    static const char *paths[2] = {
        "ux0:data/vbe_gate1b_snapshot_0.bin",
        "ux0:data/vbe_gate1b_snapshot_1.bin"
    };
    VbeG1bSnapshot snap;
    const char *path;
    int fd, ret;
    (void)argc;
    (void)argv;

    if (!exists(paths[0])) path = paths[0];
    else if (!exists(paths[1])) path = paths[1];
    else return 20;

    ret = vbeG1bTakeSnapshot(&snap);
    if (ret < 0) return 10;

    fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) return 11;
    ret = sceIoWrite(fd, &snap, sizeof(snap));
    sceIoClose(fd);
    return ret == (int)sizeof(snap) ? 0 : 12;
}
