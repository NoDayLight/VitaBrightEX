#pragma once

#include <stdint.h>

typedef struct VbeTraceRingCore {
    volatile uint32_t read_ticket;
    volatile uint32_t write_ticket;
    volatile uint32_t lost;
} VbeTraceRingCore;

/*
 * Single atomic reservation, no locks and no waiting.  The read ticket only
 * advances while capture is disabled and all hooked calls are quiescent.
 * Once an epoch fills, later reservations are dropped until the drain consumes
 * that epoch.  Advancing write_ticket on drops keeps the hook path constant-time.
 */
static inline int vbe_trace_ring_reserve(VbeTraceRingCore *ring,
                                         uint32_t capacity,
                                         uint32_t *ticket,
                                         uint32_t *slot) {
    uint32_t t = __sync_fetch_and_add(&ring->write_ticket, 1u);
    uint32_t read = ring->read_ticket;
    uint32_t distance = t - read;
    if (distance >= capacity) {
        __sync_add_and_fetch(&ring->lost, 1u);
        return 0;
    }
    *ticket = t;
    *slot = t % capacity;
    return 1;
}

static inline uint32_t vbe_trace_ring_count(const VbeTraceRingCore *ring,
                                            uint32_t capacity) {
    uint32_t distance = ring->write_ticket - ring->read_ticket;
    return distance > capacity ? capacity : distance;
}

static inline uint32_t vbe_trace_ring_slot(uint32_t ticket, uint32_t capacity) {
    return ticket % capacity;
}

static inline void vbe_trace_ring_consume_epoch(VbeTraceRingCore *ring) {
    ring->read_ticket = ring->write_ticket;
    ring->lost = 0u;
    __sync_synchronize();
}
