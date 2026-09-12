#pragma once
#include "source_authority.h"

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

typedef struct {
    int failed;
    int first_error;
} VbeStopAccumulator;

VbeTxnAttempt vbe_txn_ok(void);
VbeTxnAttempt vbe_txn_failed_clean(int error, int detail);
VbeTxnAttempt vbe_txn_failed_dirty(int error, int detail);

int vbe_txn_can_start(int ownership);
int vbe_txn_next_after_start(VbeTxnAttempt attempt);
int vbe_txn_next_after_release(VbeTxnAttempt attempt);
int vbe_txn_should_rollback(int had_previous,
                            int ownership,
                            VbeTxnAttempt requested);
int vbe_txn_public_result(VbeTxnAttempt requested,
                          int rollback_attempted,
                          VbeTxnAttempt rollback);

void vbe_txn_commit_source(VbeSourceIdentity *committed,
                           const VbeSourceIdentity *candidate);
int vbe_txn_file_persistence_allowed(int ownership,
                                     const VbeSourceIdentity *source);

void vbe_stop_init(VbeStopAccumulator *stop);
void vbe_stop_stage(VbeStopAccumulator *stop, int stage_result);
int vbe_stop_can_unload(const VbeStopAccumulator *stop);
