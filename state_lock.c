#include "state_lock.h"
#include "status.h"
#include <psp2kern/kernel/threadmgr/mutex.h>

#define LOCK_ABSENT 0
#define LOCK_RUNNING 1
#define LOCK_STOPPING 2

static SceUID g_state_mutex = -1;
static int g_lock_lifecycle = LOCK_ABSENT;

static int sync_failure(int detail) {
    g_vbe_status.state_lock = VBE_CAP_FAILED;
    status_set_error_domain(VBE_ERROR_DOMAIN_SYNC,
                            VBE_ERR_SYNCHRONIZATION, detail);
    return detail;
}

int state_lock_init(void) {
    if (g_lock_lifecycle == LOCK_RUNNING && g_state_mutex >= 0) return 0;
    if (g_lock_lifecycle != LOCK_ABSENT || g_state_mutex >= 0)
        return sync_failure(-1);

    SceUID id = ksceKernelCreateMutex("VitaBrightEXState", 0, 1, 0);
    if (id < 0) return sync_failure((int)id);
    g_state_mutex = id;
    g_lock_lifecycle = LOCK_RUNNING;
    return 0;
}

int state_lock_acquire(void) {
    if (g_lock_lifecycle != LOCK_RUNNING || g_state_mutex < 0)
        return sync_failure(-1);
    int ret = ksceKernelLockMutex(g_state_mutex, 1, 0);
    return ret < 0 ? sync_failure(ret) : ret;
}

int state_lock_release(void) {
    if (g_lock_lifecycle != LOCK_RUNNING || g_state_mutex < 0)
        return sync_failure(-1);
    int ret = ksceKernelUnlockMutex(g_state_mutex, 1);
    return ret < 0 ? sync_failure(ret) : ret;
}

int state_lock_release_result(int operation_result) {
    int release = state_lock_release();
    return release < 0 ? release : operation_result;
}

int state_lock_begin_shutdown(void) {
    if (g_lock_lifecycle != LOCK_RUNNING || g_state_mutex < 0)
        return sync_failure(-1);
    int ret = ksceKernelLockMutex(g_state_mutex, 1, 0);
    if (ret < 0) return sync_failure(ret);
    g_lock_lifecycle = LOCK_STOPPING;
    return 0;
}

int state_lock_cancel_shutdown(void) {
    if (g_lock_lifecycle != LOCK_STOPPING || g_state_mutex < 0)
        return sync_failure(-1);
    int ret = ksceKernelUnlockMutex(g_state_mutex, 1);
    if (ret < 0) return sync_failure(ret);
    g_lock_lifecycle = LOCK_RUNNING;
    return 0;
}

int state_lock_finish_shutdown(void) {
    if (g_lock_lifecycle != LOCK_STOPPING || g_state_mutex < 0)
        return sync_failure(-1);

    int ret = ksceKernelUnlockMutex(g_state_mutex, 1);
    if (ret < 0) return sync_failure(ret);

    ret = ksceKernelDeleteMutex(g_state_mutex);
    if (ret < 0) {
        g_lock_lifecycle = LOCK_RUNNING;
        return sync_failure(ret);
    }

    g_state_mutex = -1;
    g_lock_lifecycle = LOCK_ABSENT;
    return 0;
}

int state_lock_exists(void) {
    return g_state_mutex >= 0;
}
