#pragma once

typedef enum {
    VBE_LOCK_ABSENT = 0,
    VBE_LOCK_RUNNING = 1,
    VBE_LOCK_STOPPING = 2,
    VBE_LOCK_DEGRADED = 3,
} VbeLockLifecycle;

int vbe_lock_can_acquire(int lifecycle);
int vbe_lock_after_create(int lifecycle, int api_result);
int vbe_lock_after_release(int lifecycle, int api_result);
int vbe_lock_after_begin_shutdown(int lifecycle, int api_result);
int vbe_lock_after_cancel_shutdown(int lifecycle, int api_result);
int vbe_lock_after_shutdown_unlock(int lifecycle, int api_result);
int vbe_lock_after_delete(int lifecycle, int api_result);
int vbe_lock_result_after_release(int operation_result, int release_result);
