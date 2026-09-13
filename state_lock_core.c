#include "state_lock_core.h"
#include "result_core.h"

int vbe_lock_can_acquire(int lifecycle) {
    return lifecycle == VBE_LOCK_RUNNING;
}

int vbe_lock_after_create(int lifecycle, int api_result) {
    if (lifecycle != VBE_LOCK_ABSENT) return VBE_LOCK_DEGRADED;
    return api_result < 0 ? VBE_LOCK_ABSENT : VBE_LOCK_RUNNING;
}

int vbe_lock_after_release(int lifecycle, int api_result) {
    if (lifecycle != VBE_LOCK_RUNNING) return VBE_LOCK_DEGRADED;
    return api_result < 0 ? VBE_LOCK_DEGRADED : VBE_LOCK_RUNNING;
}

int vbe_lock_after_begin_shutdown(int lifecycle, int api_result) {
    if (lifecycle != VBE_LOCK_RUNNING) return VBE_LOCK_DEGRADED;
    return api_result < 0 ? VBE_LOCK_RUNNING : VBE_LOCK_STOPPING;
}

int vbe_lock_after_cancel_shutdown(int lifecycle, int api_result) {
    if (lifecycle != VBE_LOCK_STOPPING) return VBE_LOCK_DEGRADED;
    return api_result < 0 ? VBE_LOCK_DEGRADED : VBE_LOCK_RUNNING;
}

int vbe_lock_after_shutdown_unlock(int lifecycle, int api_result) {
    if (lifecycle != VBE_LOCK_STOPPING) return VBE_LOCK_DEGRADED;
    return api_result < 0 ? VBE_LOCK_DEGRADED : VBE_LOCK_STOPPING;
}

int vbe_lock_after_delete(int lifecycle, int api_result) {
    if (lifecycle != VBE_LOCK_STOPPING) return VBE_LOCK_DEGRADED;
    return api_result < 0 ? VBE_LOCK_RUNNING : VBE_LOCK_ABSENT;
}

int vbe_lock_result_after_release(int operation_result, int release_result) {
    return vbe_result_compose(operation_result, release_result);
}
