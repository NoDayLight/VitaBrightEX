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
    int fd_owned;
    int temp_owned;
} VbePersistenceState;

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

void vbe_persistence_state_init(VbePersistenceState *state);
int vbe_persistence_state_can_open(const VbePersistenceState *state);
void vbe_persistence_state_opened(VbePersistenceState *state);
void vbe_persistence_state_closed(VbePersistenceState *state);
void vbe_persistence_state_renamed(VbePersistenceState *state);
void vbe_persistence_state_temp_removed(VbePersistenceState *state);
int vbe_persistence_state_clean(const VbePersistenceState *state);

VbePersistenceOutcome vbe_persistence_execute(const VbePersistenceOps *ops);
