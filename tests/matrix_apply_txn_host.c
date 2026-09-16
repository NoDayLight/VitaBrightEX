#include <assert.h>
#include <stdio.h>
#include "matrix_apply_txn_core.h"

static VbeMatrixApplyTxn start_ready(void) {
    VbeMatrixApplyTxn t;
    vbe_matrix_txn_init(&t, 1u);
    assert(vbe_matrix_txn_begin(&t) == 0);
    assert(vbe_matrix_txn_step(&t, VBE_MATRIX_TXN_EVENT_PREFLIGHT_READY) == 0);
    assert(vbe_matrix_txn_step(&t, VBE_MATRIX_TXN_EVENT_PUBLISH_OK) == 0);
    assert(t.state == VBE_MATRIX_TXN_REPLAY_P0);
    return t;
}

static void rollback_success(VbeMatrixApplyTxn *t) {
    assert(t->state == VBE_MATRIX_TXN_ROLLBACK_PUBLISH);
    assert(vbe_matrix_txn_step(t, VBE_MATRIX_TXN_EVENT_ROLLBACK_PUBLISH_OK) == 0);
    assert(vbe_matrix_txn_step(t, VBE_MATRIX_TXN_EVENT_ROLLBACK_REPLAY_OK) == 0);
    assert(vbe_matrix_txn_step(t, VBE_MATRIX_TXN_EVENT_ROLLBACK_REPLAY_OK) == 0);
    assert(vbe_matrix_txn_step(t, VBE_MATRIX_TXN_EVENT_ROLLBACK_VERIFY_OK) == 0);
    assert(t->final == VBE_MATRIX_TXN_FINAL_ROLLED_BACK);
}

int main(void) {
    VbeMatrixApplyTxn t;

    /* Happy path. */
    t = start_ready();
    assert(vbe_matrix_txn_step(&t, VBE_MATRIX_TXN_EVENT_REPLAY_OK) == 0);
    assert(vbe_matrix_txn_step(&t, VBE_MATRIX_TXN_EVENT_REPLAY_OK) == 0);
    assert(vbe_matrix_txn_step(&t, VBE_MATRIX_TXN_EVENT_VERIFY_OK) == 0);
    assert(t.final == VBE_MATRIX_TXN_FINAL_APPLIED);

    /* Missing/unsupported/stale preflight all use the deferred publication path. */
    vbe_matrix_txn_init(&t, 1u);
    assert(vbe_matrix_txn_begin(&t) == 0);
    assert(vbe_matrix_txn_step(&t, VBE_MATRIX_TXN_EVENT_PREFLIGHT_DEFER) == 0);
    assert(vbe_matrix_txn_step(&t, VBE_MATRIX_TXN_EVENT_PUBLISH_OK) == 0);
    assert(t.final == VBE_MATRIX_TXN_FINAL_PENDING);

    /* Publication failure leaves the old world intact. */
    vbe_matrix_txn_init(&t, 1u);
    assert(vbe_matrix_txn_begin(&t) == 0);
    assert(vbe_matrix_txn_step(&t, VBE_MATRIX_TXN_EVENT_PREFLIGHT_READY) == 0);
    assert(vbe_matrix_txn_step(&t, VBE_MATRIX_TXN_EVENT_PUBLISH_FAIL) == 0);
    assert(t.final == VBE_MATRIX_TXN_FINAL_ROLLED_BACK);

    /* p0 replay failure. */
    t = start_ready();
    assert(vbe_matrix_txn_step(&t, VBE_MATRIX_TXN_EVENT_REPLAY_FAIL) == 0);
    rollback_success(&t);

    /* p0 success / p1 failure. */
    t = start_ready();
    assert(vbe_matrix_txn_step(&t, VBE_MATRIX_TXN_EVENT_REPLAY_OK) == 0);
    assert(vbe_matrix_txn_step(&t, VBE_MATRIX_TXN_EVENT_REPLAY_FAIL) == 0);
    rollback_success(&t);

    /* Verification failure after p0 is represented before p1. */
    t = start_ready();
    assert(vbe_matrix_txn_step(&t, VBE_MATRIX_TXN_EVENT_VERIFY_FAIL) == 0);
    rollback_success(&t);

    /* Verification failure after p1. */
    t = start_ready();
    assert(vbe_matrix_txn_step(&t, VBE_MATRIX_TXN_EVENT_REPLAY_OK) == 0);
    assert(vbe_matrix_txn_step(&t, VBE_MATRIX_TXN_EVENT_REPLAY_OK) == 0);
    assert(vbe_matrix_txn_step(&t, VBE_MATRIX_TXN_EVENT_VERIFY_FAIL) == 0);
    rollback_success(&t);

    /* Natural generation changes: before p0, between p0/p1 and after p1. */
    t = start_ready();
    assert(vbe_matrix_txn_step(&t, VBE_MATRIX_TXN_EVENT_REBASE_REQUIRED) == 0);
    assert(t.state == VBE_MATRIX_TXN_REPLAY_P0 && t.rebase_count == 1u);
    assert(vbe_matrix_txn_step(&t, VBE_MATRIX_TXN_EVENT_REBASE_REQUIRED) == 0);
    assert(t.final == VBE_MATRIX_TXN_FINAL_PENDING);

    t = start_ready();
    assert(vbe_matrix_txn_step(&t, VBE_MATRIX_TXN_EVENT_REPLAY_OK) == 0);
    assert(vbe_matrix_txn_step(&t, VBE_MATRIX_TXN_EVENT_REBASE_REQUIRED) == 0);
    assert(t.state == VBE_MATRIX_TXN_REPLAY_P0 && t.rebase_count == 1u);

    t = start_ready();
    assert(vbe_matrix_txn_step(&t, VBE_MATRIX_TXN_EVENT_REPLAY_OK) == 0);
    assert(vbe_matrix_txn_step(&t, VBE_MATRIX_TXN_EVENT_REPLAY_OK) == 0);
    assert(vbe_matrix_txn_step(&t, VBE_MATRIX_TXN_EVENT_REBASE_REQUIRED) == 0);
    assert(t.state == VBE_MATRIX_TXN_REPLAY_P0 && t.rebase_count == 1u);

    /* Rollback failure permutations all degrade deterministically. */
    t = start_ready();
    assert(vbe_matrix_txn_step(&t, VBE_MATRIX_TXN_EVENT_REPLAY_FAIL) == 0);
    assert(vbe_matrix_txn_step(&t, VBE_MATRIX_TXN_EVENT_ROLLBACK_PUBLISH_FAIL) == 0);
    assert(t.final == VBE_MATRIX_TXN_FINAL_DEGRADED);

    t = start_ready();
    assert(vbe_matrix_txn_step(&t, VBE_MATRIX_TXN_EVENT_REPLAY_FAIL) == 0);
    assert(vbe_matrix_txn_step(&t, VBE_MATRIX_TXN_EVENT_ROLLBACK_PUBLISH_OK) == 0);
    assert(vbe_matrix_txn_step(&t, VBE_MATRIX_TXN_EVENT_ROLLBACK_REPLAY_FAIL) == 0);
    assert(t.final == VBE_MATRIX_TXN_FINAL_DEGRADED);

    t = start_ready();
    assert(vbe_matrix_txn_step(&t, VBE_MATRIX_TXN_EVENT_REPLAY_FAIL) == 0);
    assert(vbe_matrix_txn_step(&t, VBE_MATRIX_TXN_EVENT_ROLLBACK_PUBLISH_OK) == 0);
    assert(vbe_matrix_txn_step(&t, VBE_MATRIX_TXN_EVENT_ROLLBACK_REPLAY_OK) == 0);
    assert(vbe_matrix_txn_step(&t, VBE_MATRIX_TXN_EVENT_ROLLBACK_REPLAY_FAIL) == 0);
    assert(t.final == VBE_MATRIX_TXN_FINAL_DEGRADED);

    t = start_ready();
    assert(vbe_matrix_txn_step(&t, VBE_MATRIX_TXN_EVENT_REPLAY_FAIL) == 0);
    assert(vbe_matrix_txn_step(&t, VBE_MATRIX_TXN_EVENT_ROLLBACK_PUBLISH_OK) == 0);
    assert(vbe_matrix_txn_step(&t, VBE_MATRIX_TXN_EVENT_ROLLBACK_REPLAY_OK) == 0);
    assert(vbe_matrix_txn_step(&t, VBE_MATRIX_TXN_EVENT_ROLLBACK_REPLAY_OK) == 0);
    assert(vbe_matrix_txn_step(&t, VBE_MATRIX_TXN_EVENT_ROLLBACK_VERIFY_FAIL) == 0);
    assert(t.final == VBE_MATRIX_TXN_FINAL_DEGRADED);

    /* Illegal concurrent/busy ownership is adapter-level; illegal core reuse is rejected. */
    t = start_ready();
    assert(vbe_matrix_txn_begin(&t) < 0);

    puts("GATE1E_TXN_HAPPY=PASS");
    puts("GATE1E_TXN_PENDING=PASS");
    puts("GATE1E_TXN_P0_P1_FAILURE_ROLLBACK=PASS");
    puts("GATE1E_TXN_REBASE_BOUNDED=PASS");
    puts("GATE1E_TXN_ROLLBACK_FAILURE_DEGRADED=PASS");
    return 0;
}
