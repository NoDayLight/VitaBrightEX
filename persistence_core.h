#pragma once

typedef enum {
    VBE_PERSIST_STAGE_NONE = 0,
    VBE_PERSIST_STAGE_PREPARE = 1,
    VBE_PERSIST_STAGE_OPEN = 2,
    VBE_PERSIST_STAGE_WRITE = 3,
    VBE_PERSIST_STAGE_SYNC = 4,
    VBE_PERSIST_STAGE_CLOSE = 5,
    VBE_PERSIST_STAGE_RENAME = 6,
    VBE_PERSIST_STAGE_CLEANUP = 7,
} VbePersistenceStage;

typedef struct {
    int stage;
    int error;
    int cleanup_stage;
    int cleanup_error;
    int committed;
} VbePersistenceOutcome;

typedef struct {
    void *context;
    int (*prepare)(void *context);
    int (*open_temp)(void *context);
    int (*write_payload)(void *context);
    int (*sync_temp)(void *context);
    int (*close_temp)(void *context);
    int (*rename_temp)(void *context);
    int (*cleanup_temp)(void *context);
} VbePersistenceOps;

VbePersistenceOutcome vbe_persistence_execute(const VbePersistenceOps *ops);
