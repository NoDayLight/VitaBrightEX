#include "transaction_core.h"

static VbeTxnAttempt attempt(int state, int error, int detail) {
    VbeTxnAttempt out;
    out.state = state;
    out.error = error;
    out.detail = detail;
    return out;
}

VbeTxnAttempt vbe_txn_ok(void) {
    return attempt(VBE_TXN_OK, 0, 0);
}

VbeTxnAttempt vbe_txn_failed_clean(int error, int detail) {
    return attempt(VBE_TXN_FAILED_CLEAN, error, detail);
}

VbeTxnAttempt vbe_txn_failed_dirty(int error, int detail) {
    return attempt(VBE_TXN_FAILED_DIRTY, error, detail);
}

int vbe_txn_can_start(int ownership) {
    return ownership == VBE_OWNERSHIP_CLEAN;
}

int vbe_txn_next_after_start(VbeTxnAttempt attempt_result) {
    if (attempt_result.state == VBE_TXN_OK) return VBE_OWNERSHIP_ACTIVE;
    if (attempt_result.state == VBE_TXN_FAILED_CLEAN) return VBE_OWNERSHIP_CLEAN;
    return VBE_OWNERSHIP_DEGRADED;
}

int vbe_txn_next_after_release(VbeTxnAttempt attempt_result) {
    return attempt_result.state == VBE_TXN_OK
        ? VBE_OWNERSHIP_CLEAN : VBE_OWNERSHIP_DEGRADED;
}

int vbe_txn_should_rollback(int had_previous,
                            int ownership,
                            VbeTxnAttempt requested) {
    return had_previous &&
           ownership == VBE_OWNERSHIP_CLEAN &&
           requested.state == VBE_TXN_FAILED_CLEAN;
}

int vbe_txn_public_result(VbeTxnAttempt requested,
                          int rollback_attempted,
                          VbeTxnAttempt rollback) {
    if (requested.state == VBE_TXN_OK) return 0;
    if (rollback_attempted && rollback.state != VBE_TXN_OK)
        return rollback.detail != 0 ? rollback.detail : -1;
    return requested.detail != 0 ? requested.detail : -1;
}

void vbe_stop_init(VbeStopAccumulator *stop) {
    if (stop == 0) return;
    stop->failed = 0;
    stop->first_error = 0;
}

void vbe_stop_stage(VbeStopAccumulator *stop, int stage_result) {
    if (stop == 0 || stage_result >= 0) return;
    stop->failed = 1;
    if (stop->first_error == 0) stop->first_error = stage_result;
}

int vbe_stop_can_unload(const VbeStopAccumulator *stop) {
    return stop != 0 && !stop->failed;
}
