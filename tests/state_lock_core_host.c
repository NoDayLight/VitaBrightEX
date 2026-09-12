#include <stdio.h>
#include "../state_lock_core.h"

#define ERR_OP      (-101)
#define ERR_UNLOCK  (-202)
#define ERR_DELETE  (-303)

static int check(int condition, const char *name) {
    if (condition) return 0;
    fprintf(stderr, "FAIL: %s\n", name);
    return 1;
}

int main(void) {
    int failures = 0;

    failures += check(vbe_lock_after_create(VBE_LOCK_ABSENT, 4) == VBE_LOCK_RUNNING,
                      "successful create enters running state");
    failures += check(vbe_lock_after_create(VBE_LOCK_ABSENT, -1) == VBE_LOCK_ABSENT,
                      "failed create owns no mutex");
    failures += check(vbe_lock_can_acquire(VBE_LOCK_RUNNING),
                      "runtime acquisition allowed while running");
    failures += check(!vbe_lock_can_acquire(VBE_LOCK_STOPPING),
                      "runtime acquisition rejected while stop owns serialization");
    failures += check(!vbe_lock_can_acquire(VBE_LOCK_DEGRADED),
                      "runtime acquisition rejected after uncertain unlock");

    failures += check(vbe_lock_after_release(VBE_LOCK_RUNNING, 0) == VBE_LOCK_RUNNING,
                      "successful runtime unlock remains running");
    failures += check(vbe_lock_after_release(VBE_LOCK_RUNNING, ERR_UNLOCK) == VBE_LOCK_DEGRADED,
                      "failed runtime unlock degrades ownership");

    failures += check(vbe_lock_after_begin_shutdown(VBE_LOCK_RUNNING, 0) == VBE_LOCK_STOPPING,
                      "begin shutdown quiesces runtime acquisitions");
    failures += check(vbe_lock_after_begin_shutdown(VBE_LOCK_RUNNING, ERR_OP) == VBE_LOCK_RUNNING,
                      "failed begin shutdown leaves runtime ownership unchanged");
    failures += check(vbe_lock_after_cancel_shutdown(VBE_LOCK_STOPPING, 0) == VBE_LOCK_RUNNING,
                      "cancel shutdown returns to running after confirmed unlock");
    failures += check(vbe_lock_after_cancel_shutdown(VBE_LOCK_STOPPING, ERR_UNLOCK) == VBE_LOCK_DEGRADED,
                      "failed cancel unlock leaves degraded ownership");

    failures += check(vbe_lock_after_shutdown_unlock(VBE_LOCK_STOPPING, 0) == VBE_LOCK_STOPPING,
                      "confirmed stop unlock retains mutex identity until delete");
    failures += check(vbe_lock_after_shutdown_unlock(VBE_LOCK_STOPPING, ERR_UNLOCK) == VBE_LOCK_DEGRADED,
                      "failed stop unlock degrades ownership");
    failures += check(vbe_lock_after_delete(VBE_LOCK_STOPPING, 0) == VBE_LOCK_ABSENT,
                      "confirmed delete relinquishes mutex ownership");
    failures += check(vbe_lock_after_delete(VBE_LOCK_STOPPING, ERR_DELETE) == VBE_LOCK_RUNNING,
                      "delete failure keeps resident mutex available for runtime recovery");

    failures += check(vbe_lock_result_after_release(ERR_OP, 0) == ERR_OP,
                      "operation failure survives successful unlock");
    failures += check(vbe_lock_result_after_release(0, ERR_UNLOCK) == ERR_UNLOCK,
                      "unlock failure dominates successful operation");
    failures += check(vbe_lock_result_after_release(ERR_OP, ERR_UNLOCK) == ERR_UNLOCK,
                      "unlock ownership failure dominates scalar return while domains retain operation error");

    if (failures) return 1;
    puts("production synchronization lifecycle regressions: OK");
    return 0;
}
