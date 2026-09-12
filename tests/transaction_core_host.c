#include <stdio.h>
#include <string.h>
#include "../transaction_core.h"

#define ERR_REQUEST   (-101)
#define ERR_RELEASE   (-202)
#define ERR_ROLLBACK  (-303)
#define ERR_FILTER    (-401)
#define ERR_COLOR     (-402)
#define ERR_BACKEND   (-403)

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

    VbeSourceIdentity source_a;
    VbeSourceIdentity source_b;
    VbeSourceIdentity committed;
    vbe_source_identity_file(&source_a, "ux0:tai/A.txt");
    vbe_source_identity_file(&source_b, "ur0:tai/B.txt");
    vbe_source_identity_copy(&committed, &source_a);

    /* Candidate B fails cleanly. No candidate source is committed. A legal
     * rollback commits A again, so persistence provenance remains A. */
    failures += check(vbe_txn_should_rollback(1, VBE_OWNERSHIP_CLEAN, request_clean),
                      "source rollback is legal only after clean candidate failure");
    vbe_txn_commit_source(&committed, &source_a);
    failures += check(committed.kind == VBE_SOURCE_ID_FILE &&
                      strcmp(committed.path, "ux0:tai/A.txt") == 0,
                      "successful rollback restores previous committed source A");
    failures += check(vbe_txn_file_persistence_allowed(VBE_OWNERSHIP_ACTIVE, &committed),
                      "restored file source A remains persistence eligible");

    /* Dirty candidate B never commits source B and cannot persist even though
     * the historical committed-source metadata still identifies A. */
    vbe_source_identity_copy(&committed, &source_a);
    failures += check(!vbe_txn_should_rollback(1, VBE_OWNERSHIP_DEGRADED, request_dirty),
                      "dirty source replacement forbids rollback");
    failures += check(strcmp(committed.path, "ux0:tai/A.txt") == 0,
                      "dirty failure does not overwrite committed source metadata");
    failures += check(!vbe_txn_file_persistence_allowed(VBE_OWNERSHIP_DEGRADED, &committed),
                      "degraded backend cannot persist historical source A");

    vbe_txn_commit_source(&committed, &source_b);
    failures += check(strcmp(committed.path, "ur0:tai/B.txt") == 0 &&
                      vbe_txn_file_persistence_allowed(VBE_OWNERSHIP_ACTIVE, &committed),
                      "successful candidate commits source B and enables persistence");

    VbeSourceIdentity compiled;
    vbe_source_identity_compiled(&compiled);
    failures += check(!vbe_txn_file_persistence_allowed(VBE_OWNERSHIP_ACTIVE, &compiled),
                      "compiled source is never normal file persistence target");

    VbeStopAccumulator stop;
    vbe_stop_init(&stop);
    vbe_stop_stage(&stop, 0);
    vbe_stop_stage(&stop, ERR_RELEASE);
    vbe_stop_stage(&stop, 0);
    failures += check(!vbe_stop_can_unload(&stop) && stop.first_error == ERR_RELEASE,
                      "later stop success never erases earlier teardown failure");

    vbe_stop_init(&stop);
    vbe_stop_stage(&stop, ERR_FILTER);
    vbe_stop_stage(&stop, 0);
    vbe_stop_stage(&stop, 0);
    failures += check(!vbe_stop_can_unload(&stop) && stop.first_error == ERR_FILTER,
                      "filter restore failure blocks unload despite later success");

    vbe_stop_init(&stop);
    vbe_stop_stage(&stop, 0);
    vbe_stop_stage(&stop, ERR_COLOR);
    vbe_stop_stage(&stop, 0);
    failures += check(!vbe_stop_can_unload(&stop) && stop.first_error == ERR_COLOR,
                      "color restore failure blocks unload");

    vbe_stop_init(&stop);
    vbe_stop_stage(&stop, 0);
    vbe_stop_stage(&stop, 0);
    vbe_stop_stage(&stop, ERR_BACKEND);
    failures += check(!vbe_stop_can_unload(&stop) && stop.first_error == ERR_BACKEND,
                      "backend teardown failure blocks unload");

    vbe_stop_init(&stop);
    vbe_stop_stage(&stop, 0);
    failures += check(vbe_stop_can_unload(&stop), "fully successful stop may unload");

    if (failures) return 1;
    puts("production transaction/ownership regressions: OK");
    return 0;
}
