#pragma once
#include <stdint.h>
typedef struct VbeTraceRingCore { volatile uint32_t read_ticket, write_ticket, lost; } VbeTraceRingCore;
static inline void vbe_trace_ring_init(VbeTraceRingCore *r){r->read_ticket=0;r->write_ticket=0;r->lost=0;__sync_synchronize();}
static inline int vbe_trace_ring_reserve(VbeTraceRingCore *r,uint32_t capacity,uint32_t *ticket,uint32_t *slot){uint32_t t=__sync_fetch_and_add(&r->write_ticket,1u),read=r->read_ticket;if((uint32_t)(t-read)>=capacity){__sync_add_and_fetch(&r->lost,1u);return 0;}*ticket=t;*slot=t%capacity;return 1;}
static inline uint32_t vbe_trace_ring_count(const VbeTraceRingCore *r,uint32_t capacity){uint32_t n=r->write_ticket-r->read_ticket;return n>capacity?capacity:n;}
/* Legal only while lifecycle is PAUSED and active_producers is zero. Dropped tickets are a tail once capacity is exhausted. */
static inline void vbe_trace_ring_consume_epoch(VbeTraceRingCore *r){r->read_ticket=r->write_ticket;r->lost=0u;__sync_synchronize();}
