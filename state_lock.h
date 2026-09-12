#pragma once

/*
 * Serializes user-facing reload/editor operations. Hooks themselves do not
 * take this mutex: they only read committed state, and teardown releases the
 * hook before mutating the state it observes.
 */
int state_lock_init(void);
void state_lock_destroy(void);
int state_lock_acquire(void);
void state_lock_release(void);
