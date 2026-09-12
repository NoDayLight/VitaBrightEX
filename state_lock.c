#include "state_lock.h"
#include <psp2kern/kernel/threadmgr/mutex.h>

static SceUID g_state_mutex = -1;

int state_lock_init(void) {
    if (g_state_mutex >= 0) return 0;
    SceUID id = ksceKernelCreateMutex("VitaBrightEXState", 0, 1, 0);
    if (id < 0) return (int)id;
    g_state_mutex = id;
    return 0;
}

int state_lock_destroy(void) {
    if (g_state_mutex < 0) return 0;
    int ret = ksceKernelDeleteMutex(g_state_mutex);
    if (ret < 0) return ret;
    g_state_mutex = -1;
    return 0;
}

int state_lock_acquire(void) {
    if (g_state_mutex < 0) return -1;
    return ksceKernelLockMutex(g_state_mutex, 1, 0);
}

int state_lock_release(void) {
    if (g_state_mutex < 0) return -1;
    return ksceKernelUnlockMutex(g_state_mutex, 1);
}

int state_lock_exists(void) {
    return g_state_mutex >= 0;
}
