#include "matrix_apply_txn_core.h"

static int terminal_state(VbeMatrixTxnState state) {
    return state == VBE_MATRIX_TXN_COMMITTED ||
           state == VBE_MATRIX_TXN_PENDING ||
           state == VBE_MATRIX_TXN_ROLLED_BACK ||
           state == VBE_MATRIX_TXN_DEGRADED;
}

void vbe_matrix_txn_init(VbeMatrixApplyTxn *txn, uint32_t max_rebases) {
    if (!txn) return;
    txn->state = VBE_MATRIX_TXN_IDLE;
    txn->final = VBE_MATRIX_TXN_FINAL_NONE;
    txn->preflight_deferred = 0u;
    txn->rebase_count = 0u;
    txn->max_rebases = max_rebases;
}

int vbe_matrix_txn_begin(VbeMatrixApplyTxn *txn) {
    if (!txn || txn->state != VBE_MATRIX_TXN_IDLE) return -1;
    txn->state = VBE_MATRIX_TXN_PREFLIGHT;
    return 0;
}

static int rebase_or_pending(VbeMatrixApplyTxn *txn) {
    if (txn->rebase_count < txn->max_rebases) {
        txn->rebase_count += 1u;
        txn->state = VBE_MATRIX_TXN_REPLAY_P0;
    } else {
        txn->state = VBE_MATRIX_TXN_PENDING;
        txn->final = VBE_MATRIX_TXN_FINAL_PENDING;
    }
    return 0;
}

int vbe_matrix_txn_step(VbeMatrixApplyTxn *txn, VbeMatrixTxnEvent event) {
    if (!txn || terminal_state(txn->state)) return -1;

    switch (txn->state) {
    case VBE_MATRIX_TXN_PREFLIGHT:
        if (event == VBE_MATRIX_TXN_EVENT_PREFLIGHT_READY) {
            txn->preflight_deferred = 0u;
            txn->state = VBE_MATRIX_TXN_PUBLISHED;
            return 0;
        }
        if (event == VBE_MATRIX_TXN_EVENT_PREFLIGHT_DEFER) {
            txn->preflight_deferred = 1u;
            txn->state = VBE_MATRIX_TXN_PUBLISHED;
            return 0;
        }
        break;

    case VBE_MATRIX_TXN_PUBLISHED:
        if (event == VBE_MATRIX_TXN_EVENT_PUBLISH_OK) {
            if (txn->preflight_deferred) {
                txn->state = VBE_MATRIX_TXN_PENDING;
                txn->final = VBE_MATRIX_TXN_FINAL_PENDING;
            } else {
                txn->state = VBE_MATRIX_TXN_REPLAY_P0;
            }
            return 0;
        }
        if (event == VBE_MATRIX_TXN_EVENT_PUBLISH_FAIL) {
            /* Nothing reached hardware; the previous state remained intact. */
            txn->state = VBE_MATRIX_TXN_ROLLED_BACK;
            txn->final = VBE_MATRIX_TXN_FINAL_ROLLED_BACK;
            return 0;
        }
        break;

    case VBE_MATRIX_TXN_REPLAY_P0:
        if (event == VBE_MATRIX_TXN_EVENT_REPLAY_OK) {
            txn->state = VBE_MATRIX_TXN_REPLAY_P1;
            return 0;
        }
        if (event == VBE_MATRIX_TXN_EVENT_REPLAY_FAIL ||
            event == VBE_MATRIX_TXN_EVENT_VERIFY_FAIL) {
            txn->state = VBE_MATRIX_TXN_ROLLBACK_PUBLISH;
            return 0;
        }
        if (event == VBE_MATRIX_TXN_EVENT_REBASE_REQUIRED)
            return rebase_or_pending(txn);
        break;

    case VBE_MATRIX_TXN_REPLAY_P1:
        if (event == VBE_MATRIX_TXN_EVENT_REPLAY_OK) {
            txn->state = VBE_MATRIX_TXN_VERIFY;
            return 0;
        }
        if (event == VBE_MATRIX_TXN_EVENT_REPLAY_FAIL ||
            event == VBE_MATRIX_TXN_EVENT_VERIFY_FAIL) {
            txn->state = VBE_MATRIX_TXN_ROLLBACK_PUBLISH;
            return 0;
        }
        if (event == VBE_MATRIX_TXN_EVENT_REBASE_REQUIRED)
            return rebase_or_pending(txn);
        break;

    case VBE_MATRIX_TXN_VERIFY:
        if (event == VBE_MATRIX_TXN_EVENT_VERIFY_OK) {
            txn->state = VBE_MATRIX_TXN_COMMITTED;
            txn->final = VBE_MATRIX_TXN_FINAL_APPLIED;
            return 0;
        }
        if (event == VBE_MATRIX_TXN_EVENT_VERIFY_FAIL) {
            txn->state = VBE_MATRIX_TXN_ROLLBACK_PUBLISH;
            return 0;
        }
        if (event == VBE_MATRIX_TXN_EVENT_REBASE_REQUIRED)
            return rebase_or_pending(txn);
        break;

    case VBE_MATRIX_TXN_ROLLBACK_PUBLISH:
        if (event == VBE_MATRIX_TXN_EVENT_ROLLBACK_PUBLISH_OK) {
            txn->state = VBE_MATRIX_TXN_ROLLBACK_P0;
            return 0;
        }
        if (event == VBE_MATRIX_TXN_EVENT_ROLLBACK_PUBLISH_FAIL) {
            txn->state = VBE_MATRIX_TXN_DEGRADED;
            txn->final = VBE_MATRIX_TXN_FINAL_DEGRADED;
            return 0;
        }
        break;

    case VBE_MATRIX_TXN_ROLLBACK_P0:
        if (event == VBE_MATRIX_TXN_EVENT_ROLLBACK_REPLAY_OK) {
            txn->state = VBE_MATRIX_TXN_ROLLBACK_P1;
            return 0;
        }
        if (event == VBE_MATRIX_TXN_EVENT_ROLLBACK_REPLAY_FAIL) {
            txn->state = VBE_MATRIX_TXN_DEGRADED;
            txn->final = VBE_MATRIX_TXN_FINAL_DEGRADED;
            return 0;
        }
        break;

    case VBE_MATRIX_TXN_ROLLBACK_P1:
        if (event == VBE_MATRIX_TXN_EVENT_ROLLBACK_REPLAY_OK) {
            txn->state = VBE_MATRIX_TXN_ROLLBACK_VERIFY;
            return 0;
        }
        if (event == VBE_MATRIX_TXN_EVENT_ROLLBACK_REPLAY_FAIL) {
            txn->state = VBE_MATRIX_TXN_DEGRADED;
            txn->final = VBE_MATRIX_TXN_FINAL_DEGRADED;
            return 0;
        }
        break;

    case VBE_MATRIX_TXN_ROLLBACK_VERIFY:
        if (event == VBE_MATRIX_TXN_EVENT_ROLLBACK_VERIFY_OK) {
            txn->state = VBE_MATRIX_TXN_ROLLED_BACK;
            txn->final = VBE_MATRIX_TXN_FINAL_ROLLED_BACK;
            return 0;
        }
        if (event == VBE_MATRIX_TXN_EVENT_ROLLBACK_VERIFY_FAIL) {
            txn->state = VBE_MATRIX_TXN_DEGRADED;
            txn->final = VBE_MATRIX_TXN_FINAL_DEGRADED;
            return 0;
        }
        break;

    default:
        break;
    }
    return -1;
}

int vbe_matrix_txn_is_terminal(const VbeMatrixApplyTxn *txn) {
    return txn ? terminal_state(txn->state) : 0;
}
