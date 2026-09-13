#include <stdio.h>
#include <string.h>
#include <psp2kern/io/fcntl.h>
#include <psp2kern/lowio/iftu.h>
#include <taihen.h>

#define NID_IFTU_CSC_A 0x0FCBF457u
#define NID_IFTU_CSC_B 0xD64F4C6Bu
#define TRACE_FILE "ur0:data/vbe_iftu_csc_trace.log"

static tai_hook_ref_t g_ref_a;
static tai_hook_ref_t g_ref_b;
static SceUID g_hook_a = -1;
static SceUID g_hook_b = -1;
static volatile unsigned int g_sequence = 0;

static void trace_reset(void) {
    SceUID fd = ksceIoOpen(TRACE_FILE, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 6);
    if (fd >= 0) ksceIoClose(fd);
}

static void trace_event(char setter, int plane, const SceIftuCscParams *p) {
    char line[320];
    unsigned int seq = __sync_add_and_fetch(&g_sequence, 1u);
    int n;
    if (p == NULL) {
        n = snprintf(line, sizeof(line), "%u %c plane=%d NULL\n", seq, setter, plane);
    } else {
        const unsigned int *w = (const unsigned int *)p;
        n = snprintf(line, sizeof(line),
            "%u %c plane=%d ptr=%p words="
            "%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X\n",
            seq, setter, plane, (const void *)p,
            w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8],
            w[9], w[10], w[11], w[12], w[13], w[14]);
    }
    if (n <= 0) return;
    if ((size_t)n > sizeof(line)) n = (int)sizeof(line);
    SceUID fd = ksceIoOpen(TRACE_FILE, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 6);
    if (fd < 0) return;
    (void)ksceIoWrite(fd, line, (SceSize)n);
    (void)ksceIoClose(fd);
}

static int hook_csc_a(int plane, const SceIftuCscParams *params) {
    trace_event('A', plane, params);
    return TAI_CONTINUE(int, g_ref_a, plane, params);
}

static int hook_csc_b(int plane, const SceIftuCscParams *params) {
    trace_event('B', plane, params);
    return TAI_CONTINUE(int, g_ref_b, plane, params);
}

void _start(void) __attribute__((weak, alias("module_start")));
int module_start(SceSize argc, const void *args) {
    (void)argc;
    (void)args;
    trace_reset();
    g_sequence = 0;

    g_hook_a = taiHookFunctionExportForKernel(KERNEL_PID, &g_ref_a,
        "SceLowio", TAI_ANY_LIBRARY, NID_IFTU_CSC_A, hook_csc_a);
    if (g_hook_a < 0) {
        g_hook_a = -1;
        return SCE_KERNEL_START_SUCCESS;
    }

    g_hook_b = taiHookFunctionExportForKernel(KERNEL_PID, &g_ref_b,
        "SceLowio", TAI_ANY_LIBRARY, NID_IFTU_CSC_B, hook_csc_b);
    if (g_hook_b < 0) {
        g_hook_b = -1;
        int release = taiHookReleaseForKernel(g_hook_a, g_ref_a);
        if (release >= 0) g_hook_a = -1;
        /* If release failed, remain resident rather than unload beneath a hook. */
        return SCE_KERNEL_START_SUCCESS;
    }

    return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize argc, const void *args) {
    (void)argc;
    (void)args;
    int failed = 0;

    if (g_hook_b >= 0) {
        int ret = taiHookReleaseForKernel(g_hook_b, g_ref_b);
        if (ret < 0) failed = 1;
        else g_hook_b = -1;
    }
    if (g_hook_a >= 0) {
        int ret = taiHookReleaseForKernel(g_hook_a, g_ref_a);
        if (ret < 0) failed = 1;
        else g_hook_a = -1;
    }

    return failed ? SCE_KERNEL_STOP_FAIL : SCE_KERNEL_STOP_SUCCESS;
}
