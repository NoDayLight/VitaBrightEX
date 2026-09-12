#pragma once

int state_lock_init(void);
int state_lock_destroy(void);
int state_lock_acquire(void);
int state_lock_release(void);
int state_lock_exists(void);
