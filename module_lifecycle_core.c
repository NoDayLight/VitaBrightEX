#include "module_lifecycle_core.h"
#include "state_lock_core.h"

int vbe_module_stop_mode(int module_lifecycle, int lock_lifecycle) {
    if (module_lifecycle == VBE_MODULE_INERT)
        return lock_lifecycle == VBE_LOCK_ABSENT
            ? VBE_MODULE_STOP_INERT : VBE_MODULE_STOP_UNSAFE;

    if (module_lifecycle == VBE_MODULE_RUNTIME)
        return lock_lifecycle == VBE_LOCK_RUNNING
            ? VBE_MODULE_STOP_RUNTIME : VBE_MODULE_STOP_UNSAFE;

    return VBE_MODULE_STOP_UNSAFE;
}
