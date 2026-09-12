#include "build_info.h"
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/sysmem.h>

#ifndef VBE_BUILD_ID
#define VBE_BUILD_ID "unknown"
#endif

static const char g_build_id[VBE_BUILD_ID_SIZE] = VBE_BUILD_ID;

int vitabrightGetBuildId(char out[VBE_BUILD_ID_SIZE]) {
    int state;
    ENTER_SYSCALL(state);
    int ret = ksceKernelMemcpyKernelToUser((void *)out, g_build_id,
                                           sizeof(g_build_id));
    EXIT_SYSCALL(state);
    return ret;
}
