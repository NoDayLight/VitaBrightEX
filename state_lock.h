#pragma once

int state_lock_init(void);
int state_lock_acquire(void);
int state_lock_release(void);
int state_lock_release_result(int operation_result);

int state_lock_begin_shutdown(void);
int state_lock_cancel_shutdown(void);
int state_lock_finish_shutdown(void);
int state_lock_exists(void);
