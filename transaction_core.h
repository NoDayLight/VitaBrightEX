#pragma once

typedef enum {
    VBE_OWNERSHIP_CLEAN = 0,
    VBE_OWNERSHIP_ACTIVE = 1,
    VBE_OWNERSHIP_DEGRADED = 2,
} VbeOwnershipState;

typedef enum {
    VBE_TXN_OK = 0,
    VBE_TXN_FAILED_CLEAN = 1,
    VBE_TXN_FAILED_DIRTY = 2,
} VbeTxnClass;

typedef struct {
    int state;
    int error;
    int detail;
} VbeTxnAttempt;
