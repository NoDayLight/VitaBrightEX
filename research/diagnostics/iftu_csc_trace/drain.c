#include <stdint.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/threadmgr.h>

#include "trace_protocol.h"

#define TRACE_OUTPUT "ux0:data/vbe_color_trace.bin"

static VbeTraceRecord g_records[VBE_TRACE_RECORD_CAPACITY];

static int write_all(SceUID fd, const void *data, uint32_t size) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t done = 0;
    while (done < size) {
        int n = sceIoWrite(fd, p + done, size - done);
        if (n <= 0) return n < 0 ? n : -1;
        done += (uint32_t)n;
    }
    return 0;
}

int main(void) {
    VbeTraceStatus status;
    VbeTraceSnapshot snapshot;
    VbeTraceDumpHeader header;
    uint32_t record_count = 0;
    uint32_t snapshot_size = 0;
    SceUID fd;
    int ret;
    int i;

    ret = vbeTraceStop();
    if (ret < 0) return ret;

    for (i = 0; i < 200; ++i) {
        ret = vbeTraceGetStatus(&status);
        if (ret < 0) return ret;
        if (status.active_hooks == 0) break;
        sceKernelDelayThread(1000);
    }
    if (status.active_hooks != 0) return -2;

    ret = vbeTraceSnapshot(&snapshot);
    if (ret >= 0) snapshot_size = (uint32_t)sizeof(snapshot);

    ret = vbeTraceRead(g_records, VBE_TRACE_RECORD_CAPACITY, &record_count);
    if (ret < 0) return ret;

    ret = vbeTraceGetStatus(&status);
    if (ret < 0) return ret;

    header.magic = VBE_TRACE_DUMP_MAGIC;
    header.version = VBE_TRACE_VERSION;
    header.header_size = (uint32_t)sizeof(header);
    header.status_size = (uint32_t)sizeof(status);
    header.snapshot_size = snapshot_size;
    header.record_size = (uint32_t)sizeof(VbeTraceRecord);
    header.record_count = record_count;
    header.reserved = 0;

    fd = sceIoOpen(TRACE_OUTPUT, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0) return fd;

    ret = write_all(fd, &header, (uint32_t)sizeof(header));
    if (ret >= 0) ret = write_all(fd, &status, (uint32_t)sizeof(status));
    if (ret >= 0 && snapshot_size != 0) ret = write_all(fd, &snapshot, snapshot_size);
    if (ret >= 0 && record_count != 0)
        ret = write_all(fd, g_records, record_count * (uint32_t)sizeof(VbeTraceRecord));

    sceIoClose(fd);
    return ret;
}
