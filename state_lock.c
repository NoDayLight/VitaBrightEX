#include "state_lock.h"
#include "state_lock_core.h"
#include "status.h"
#include <psp2kern/kernel/threadmgr/mutex.h>

static SceUID g_state_mutex = -1;
static int g_lock_lifecycle = VBE_LOCK_ABSENT;

static int sync_failure(int detail) {
    g_vbe_status.state_lock = VBE_CAP_FAILED;
    status_set_error_domain(VBE_ERROR_DOMAIN_SYNC,
                            VBE_ERR_SYNCHRONIZATION, detail);
    return detail < 0 ? detail : -1;
}

static void sync_runtime_ok(void) {
    g_vbe_status.state_lock = VBE_CAP_ACTIVE;
    status_clear_error_domain(VBE_ERROR_DOMAIN_SYNC);
}

int state_lock_init(void) {
    if (g_lock_lifecycle == VBE_LOCK_RUNNING && g_state_mutex >= 0) return 0;
    if (g_lock_lifecycle != VBE_LOCK_ABSENT || g_state_mutex >= 0)
        return sync_failure(-1);

    SceUID id = ksceKernelCreateMutex("VitaBrightEXState", 0, 1, 0);
    g_lock_lifecycle = vbe_lock_after_create(g_lock_lifecycle, (int)id);
    if (id < 0) return sync_failure((int)id);

    g_state_mutex = id;
    sync_runtime_ok();
    return 0;
}

int state_lock_acquire(void) {
    if (g_lock_lifecycle == VBE_LOCK_STOPPING) return -1;
    if (!vbe_lock_can_acquire(g_lock_lifecycle) || g_state_mutex < 0)
        return sync_failure(-1);

    int ret = ksceKernelLockMutex(g_state_mutex, 1, 0);
    if (ret < 0) return sync_failure(ret);
    return ret;
}

int state_lock_release(void) {
    if (g_lock_lifecycle != VBE_LOCK_RUNNING || g_state_mutex < 0)
        return sync_failure(-1);

    int ret = ksceKernelUnlockMutex(g_state_mutex, 1);
    g_lock_lifecycle = vbe_lock_after_release(g_lock_lifecycle, ret);
    if (ret < 0) return sync_failure(ret);
    return ret;
}

int state_lock_release_result(int operation_result) {
    int release = state_lock_release();
    int result = vbe_lock_result_after_release(operation_result, release);
    if (release >= 0) sync_runtime_ok();
    return result;
}

int state_lock_begin_shutdown(void) {
    if (!vbe_lock_can_acquire(g_lock_lifecycle) || g_state_mutex < 0)
        return sync_failure(-1);

    int ret = ksceKernelLockMutex(g_state_mutex, 1, 0);
    g_lock_lifecycle = vbe_lock_after_begin_shutdown(g_lock_lifecycle, ret);
    if (ret < 0) return sync_failure(ret);
    return 0;
}

int state_lock_cancel_shutdown(void) {
    if (g_lock_lifecycle != VBE_LOCK_STOPPING || g_state_mutex < 0)
        return sync_failure(-1);

    int ret = ksceKernelUnlockMutex(g_state_mutex, 1);
    g_lock_lifecycle = vbe_lock_after_cancel_shutdown(g_lock_lifecycle, ret);
    if (ret < 0) return sync_failure(ret);

    sync_runtime_ok();
    return 0;
}

int state_lock_finish_shutdown(void) {
    if (g_lock_lifecycle != VBE_LOCK_STOPPING || g_state_mutex < 0)
        return sync_failure(-1);

    int ret = ksceKernelUnlockMutex(g_state_mutex, 1);
    g_lock_lifecycle = vbe_lock_after_shutdown_unlock(g_lock_lifecycle, ret);
    if (ret < 0) return sync_failure(ret);

    ret = ksceKernelDeleteMutex(g_state_mutex);
    g_lock_lifecycle = vbe_lock_after_delete(g_lock_lifecycle, ret);
    if (ret < 0) return sync_failure(ret);

    g_state_mutex = -1;
    g_vbe_status.state_lock = VBE_CAP_INACTIVE;
    status_clear_error_domain(VBE_ERROR_DOMAIN_SYNC);
    return 0;
}

int state_lock_exists(void) {
    return g_state_mutex >= 0;
}

int state_lock_lifecycle(void) {
    return g_lock_lifecycle;
}
