#include <stdio.h>
#include "../module_lifecycle_core.h"
#include "../state_lock_core.h"

#define ERR_UNLOCK (-202)
#define ERR_DELETE (-303)

static int check(int condition, const char *name) {
    if (condition) return 0;
    fprintf(stderr, "FAIL: %s\n", name);
    return 1;
}

int main(void) {
    int failures = 0;
    int module = VBE_MODULE_INERT;
    int lock = VBE_LOCK_ABSENT;

    lock = vbe_lock_after_create(lock, -1);
    failures += check(lock == VBE_LOCK_ABSENT,
                      "failed lock creation owns no mutex");
    failures += check(vbe_module_stop_mode(module, lock) == VBE_MODULE_STOP_INERT,
                      "failed lock creation leaves clean inert module unloadable");

    lock = vbe_lock_after_create(VBE_LOCK_ABSENT, 4);
    module = VBE_MODULE_RUNTIME;
    failures += check(vbe_module_stop_mode(module, lock) == VBE_MODULE_STOP_RUNTIME,
                      "successful lock creation crosses runtime stop boundary");

    int degraded = vbe_lock_after_release(lock, ERR_UNLOCK);
    failures += check(degraded == VBE_LOCK_DEGRADED &&
                      vbe_module_stop_mode(module, degraded) == VBE_MODULE_STOP_UNSAFE,
                      "runtime unlock failure remains unload-unsafe");

    int stopping = vbe_lock_after_begin_shutdown(VBE_LOCK_RUNNING, 0);
    degraded = vbe_lock_after_shutdown_unlock(stopping, ERR_UNLOCK);
    failures += check(degraded == VBE_LOCK_DEGRADED &&
                      vbe_module_stop_mode(module, degraded) == VBE_MODULE_STOP_UNSAFE,
                      "stop unlock failure remains unload-unsafe");

    stopping = vbe_lock_after_begin_shutdown(VBE_LOCK_RUNNING, 0);
    int after_unlock = vbe_lock_after_shutdown_unlock(stopping, 0);
    int after_delete = vbe_lock_after_delete(after_unlock, ERR_DELETE);
    failures += check(after_delete == VBE_LOCK_RUNNING &&
                      vbe_module_stop_mode(module, after_delete) == VBE_MODULE_STOP_RUNTIME,
                      "failed mutex delete retains runtime stop responsibility");

    stopping = vbe_lock_after_begin_shutdown(VBE_LOCK_RUNNING, 0);
    after_unlock = vbe_lock_after_shutdown_unlock(stopping, 0);
    after_delete = vbe_lock_after_delete(after_unlock, 0);
    module = VBE_MODULE_INERT;
    failures += check(after_delete == VBE_LOCK_ABSENT &&
                      vbe_module_stop_mode(module, after_delete) == VBE_MODULE_STOP_INERT,
                      "confirmed clean stop returns to inert absent state");
    failures += check(vbe_module_stop_mode(module, after_delete) == VBE_MODULE_STOP_INERT,
                      "clean inert stop is idempotent");

    failures += check(vbe_module_stop_mode(VBE_MODULE_INERT, VBE_LOCK_RUNNING) ==
                          VBE_MODULE_STOP_UNSAFE,
                      "inert marker cannot hide an owned mutex");
    failures += check(vbe_module_stop_mode(VBE_MODULE_RUNTIME, VBE_LOCK_ABSENT) ==
                          VBE_MODULE_STOP_UNSAFE,
                      "runtime marker cannot treat unexpected absent lock as clean");
    failures += check(vbe_module_stop_mode(VBE_MODULE_RUNTIME, VBE_LOCK_STOPPING) ==
                          VBE_MODULE_STOP_UNSAFE,
                      "runtime module cannot bypass an in-progress stop state");

    if (failures) return 1;
    puts("production module lifecycle regressions: OK");
    return 0;
}
