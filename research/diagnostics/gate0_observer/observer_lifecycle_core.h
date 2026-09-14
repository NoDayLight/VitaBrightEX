#pragma once
#include <stdint.h>

typedef enum VbeObserverLifecycle {
    VBE_OBSERVER_INERT = 0,
    VBE_OBSERVER_RUNNING = 1,
    VBE_OBSERVER_PAUSED = 2,
    VBE_OBSERVER_QUIESCING = 3,
    VBE_OBSERVER_QUIESCED = 4,
    VBE_OBSERVER_PARTIAL_OWNED = 5
} VbeObserverLifecycle;

typedef struct VbeObserverLifecycleCore {
    volatile uint32_t state;
    volatile uint32_t active_producers;
    volatile uint32_t owned_hook_mask;
    uint32_t required_hook_mask;
} VbeObserverLifecycleCore;

static inline void vbe_observer_core_init(VbeObserverLifecycleCore *c, uint32_t required) {
    c->state = VBE_OBSERVER_INERT;
    c->active_producers = 0u;
    c->owned_hook_mask = 0u;
    c->required_hook_mask = required;
    __sync_synchronize();
}

static inline void vbe_observer_core_note_hook(VbeObserverLifecycleCore *c, uint32_t bit) {
    __sync_fetch_and_or(&c->owned_hook_mask, bit);
}

static inline void vbe_observer_core_install_failed(VbeObserverLifecycleCore *c) {
    uint32_t owned = c->owned_hook_mask;
    __sync_synchronize();
    c->state = owned ? VBE_OBSERVER_PARTIAL_OWNED : VBE_OBSERVER_INERT;
    __sync_synchronize();
}

static inline int vbe_observer_core_start_capture(VbeObserverLifecycleCore *c) {
    if (c->state != VBE_OBSERVER_INERT || c->active_producers != 0u ||
        c->owned_hook_mask != c->required_hook_mask) return 0;
    __sync_synchronize();
    return __sync_bool_compare_and_swap(&c->state, VBE_OBSERVER_INERT, VBE_OBSERVER_RUNNING);
}

/* Every ring-mutating operation enters this protocol before observing state. */
static inline int vbe_observer_core_producer_enter(VbeObserverLifecycleCore *c) {
    __sync_add_and_fetch(&c->active_producers, 1u);
    __sync_synchronize();
    return c->state == VBE_OBSERVER_RUNNING;
}

static inline void vbe_observer_core_producer_leave(VbeObserverLifecycleCore *c) {
    uint32_t remaining;
    __sync_synchronize();
    remaining = __sync_sub_and_fetch(&c->active_producers, 1u);
    if (remaining == 0u && c->state == VBE_OBSERVER_QUIESCING) {
        (void)__sync_bool_compare_and_swap(&c->state, VBE_OBSERVER_QUIESCING, VBE_OBSERVER_QUIESCED);
    }
    __sync_synchronize();
}

static inline int vbe_observer_core_pause(VbeObserverLifecycleCore *c) {
    if (c->state == VBE_OBSERVER_PAUSED) return 1;
    return __sync_bool_compare_and_swap(&c->state, VBE_OBSERVER_RUNNING, VBE_OBSERVER_PAUSED);
}

static inline int vbe_observer_core_stable_paused(const VbeObserverLifecycleCore *c) {
    __sync_synchronize();
    return c->state == VBE_OBSERVER_PAUSED && c->active_producers == 0u;
}

static inline int vbe_observer_core_resume(VbeObserverLifecycleCore *c) {
    if (c->owned_hook_mask != c->required_hook_mask || c->active_producers != 0u) return 0;
    __sync_synchronize();
    return __sync_bool_compare_and_swap(&c->state, VBE_OBSERVER_PAUSED, VBE_OBSERVER_RUNNING);
}

static inline void vbe_observer_core_quiesce(VbeObserverLifecycleCore *c) {
    /* Two bounded CAS attempts cover the only reversible source states. */
    if (!__sync_bool_compare_and_swap(&c->state, VBE_OBSERVER_RUNNING, VBE_OBSERVER_QUIESCING))
        (void)__sync_bool_compare_and_swap(&c->state, VBE_OBSERVER_PAUSED, VBE_OBSERVER_QUIESCING);
    __sync_synchronize();
    if (c->state == VBE_OBSERVER_QUIESCING && c->active_producers == 0u) {
        (void)__sync_bool_compare_and_swap(&c->state, VBE_OBSERVER_QUIESCING, VBE_OBSERVER_QUIESCED);
    }
    __sync_synchronize();
}

static inline int vbe_observer_core_can_unload(const VbeObserverLifecycleCore *c) {
    __sync_synchronize();
    return c->owned_hook_mask == 0u && c->active_producers == 0u;
}
