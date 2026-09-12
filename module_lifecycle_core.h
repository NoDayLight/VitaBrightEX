#pragma once

typedef enum {
    VBE_MODULE_INERT = 0,
    VBE_MODULE_RUNTIME = 1,
} VbeModuleLifecycle;

typedef enum {
    VBE_MODULE_STOP_UNSAFE = -1,
    VBE_MODULE_STOP_INERT = 0,
    VBE_MODULE_STOP_RUNTIME = 1,
} VbeModuleStopMode;

/* Module orchestration only: resource ownership remains authoritative in the
 * lock/backend/session subsystems. */
int vbe_module_stop_mode(int module_lifecycle, int lock_lifecycle);
