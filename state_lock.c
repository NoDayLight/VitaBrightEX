#include "state_lock.h"
#include <psp2kern/kernel/threadmgr/mutex.h>

static SceUID g_state_mutex = -1;

int state_lock_init(void) {
    if (g_state_mutex >= 0) return 0;
    g_state_mutex = ksceKernelCreateMutex("VitaBrightEXState", 0, 1, 0);
    return g_state_mutex < 0 ? (int)g_state_mutex : 0;
}

void state_lock_destroy(void) {
    if (g_state_mutex >= 0) {
        ksceKernelDeleteMutex(g_state_mutex);
        g_state_mutex = -1;
    }
}

int state_lock_acquire(void) {
    if (g_state_mutex < 0) return -1;
    return ksceKernelLockMutex(g_state_mutex, 1, 0);
}

void state_lock_release(void) {
    if (g_state_mutex >= 0)
        (void)ksceKernelUnlockMutex(g_state_mutex, 1);
}
