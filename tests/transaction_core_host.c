#include <stdio.h>
#include "../transaction_core.h"

#define ERR_REQUEST   (-101)
#define ERR_RELEASE   (-202)
#define ERR_ROLLBACK  (-303)

static int check(int condition, const char *name) {
    if (condition) return 0;
    fprintf(stderr, "FAIL: %s\n", name);
    return 1;
}

int main(void) {
    int failures = 0;
    VbeTxnAttempt ok = vbe_txn_ok();
    VbeTxnAttempt request_clean = vbe_txn_failed_clean(10, ERR_REQUEST);
    VbeTxnAttempt request_dirty = vbe_txn_failed_dirty(20, ERR_RELEASE);
    VbeTxnAttempt rollback_clean = vbe_txn_failed_clean(30, ERR_ROLLBACK);
    VbeTxnAttempt rollback_dirty = vbe_txn_failed_dirty(20, ERR_RELEASE);

    failures += check(vbe_txn_can_start(VBE_OWNERSHIP_CLEAN), "start allowed only from clean");
    failures += check(!vbe_txn_can_start(VBE_OWNERSHIP_ACTIVE), "start blocked from active");
    failures += check(!vbe_txn_can_start(VBE_OWNERSHIP_DEGRADED), "start blocked from degraded");

    failures += check(vbe_txn_next_after_release(ok) == VBE_OWNERSHIP_CLEAN,
                      "active release success becomes clean");
    failures += check(vbe_txn_next_after_release(request_dirty) == VBE_OWNERSHIP_DEGRADED,
                      "release failure becomes degraded");

    failures += check(vbe_txn_next_after_start(ok) == VBE_OWNERSHIP_ACTIVE,
                      "candidate success becomes active");
    failures += check(vbe_txn_next_after_start(request_clean) == VBE_OWNERSHIP_CLEAN,
                      "clean candidate failure remains clean");
    failures += check(vbe_txn_next_after_start(request_dirty) == VBE_OWNERSHIP_DEGRADED,
                      "dirty candidate failure becomes degraded");

    failures += check(vbe_txn_should_rollback(1, VBE_OWNERSHIP_CLEAN, request_clean),
                      "clean candidate failure permits rollback");
    failures += check(!vbe_txn_should_rollback(1, VBE_OWNERSHIP_DEGRADED, request_dirty),
                      "dirty candidate failure forbids rollback");
    failures += check(!vbe_txn_should_rollback(0, VBE_OWNERSHIP_CLEAN, request_clean),
                      "no previous transaction means no rollback");

    failures += check(vbe_txn_public_result(request_clean, 1, ok) == ERR_REQUEST,
                      "recovered request reports requested failure");
    failures += check(vbe_txn_public_result(request_clean, 1, rollback_clean) == ERR_ROLLBACK,
                      "clean rollback failure dominates public result");
    failures += check(vbe_txn_public_result(request_clean, 1, rollback_dirty) == ERR_RELEASE,
                      "dirty rollback cleanup failure dominates public result");
    failures += check(vbe_txn_public_result(request_dirty, 0, ok) == ERR_RELEASE,
                      "dirty candidate cleanup failure is returned directly");

    VbeStopAccumulator stop;
    vbe_stop_init(&stop);
    vbe_stop_stage(&stop, 0);
    vbe_stop_stage(&stop, ERR_RELEASE);
    vbe_stop_stage(&stop, 0);
    failures += check(!vbe_stop_can_unload(&stop) && stop.first_error == ERR_RELEASE,
                      "later stop success never erases earlier teardown failure");

    vbe_stop_init(&stop);
    vbe_stop_stage(&stop, 0);
    failures += check(vbe_stop_can_unload(&stop), "fully successful stop may unload");

    if (failures) return 1;
    puts("production transaction/ownership regressions: OK");
    return 0;
}
