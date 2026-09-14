#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include "diagnostics/gate0_observer/observer_lifecycle_core.h"
#include "diagnostics/gate0_observer/trace_ring_core.h"
#include "diagnostics/gate0_observer/gate0_protocol_v7.h"
#define REQ VBE_TRACE_REQUIRED_HOOKS
static unsigned checks;
#define CHECK(x) do{assert(x);checks++;}while(0)
typedef struct Hold{VbeObserverLifecycleCore*c;pthread_mutex_t m;pthread_cond_t cv;int entered,release,captured;}Hold;
static void*hold_producer(void*vp){Hold*h=(Hold*)vp;h->captured=vbe_observer_core_producer_enter(h->c);pthread_mutex_lock(&h->m);h->entered=1;pthread_cond_broadcast(&h->cv);while(!h->release)pthread_cond_wait(&h->cv,&h->m);pthread_mutex_unlock(&h->m);vbe_observer_core_producer_leave(h->c);return NULL;}
static void hold_init(Hold*h,VbeObserverLifecycleCore*c){h->c=c;h->entered=h->release=h->captured=0;pthread_mutex_init(&h->m,NULL);pthread_cond_init(&h->cv,NULL);}
static void wait_enter(Hold*h){pthread_mutex_lock(&h->m);while(!h->entered)pthread_cond_wait(&h->cv,&h->m);pthread_mutex_unlock(&h->m);}
static void release_hold(Hold*h){pthread_mutex_lock(&h->m);h->release=1;pthread_cond_broadcast(&h->cv);pthread_mutex_unlock(&h->m);}
static void hold_destroy(Hold*h){pthread_cond_destroy(&h->cv);pthread_mutex_destroy(&h->m);}
static VbeObserverLifecycleCore full(void){VbeObserverLifecycleCore c;vbe_observer_core_init(&c,REQ);c.owned_hook_mask=REQ;CHECK(vbe_observer_core_start_capture(&c));return c;}
int main(void){VbeObserverLifecycleCore c;VbeTraceRingCore r;uint32_t ticket,slot;int cap;pthread_t th;Hold h;
 c=full();CHECK(c.state==VBE_OBSERVER_RUNNING);CHECK(vbe_observer_core_pause(&c));CHECK(vbe_observer_core_stable_paused(&c));CHECK(vbe_observer_core_resume(&c));
 hold_init(&h,&c);CHECK(pthread_create(&th,NULL,hold_producer,&h)==0);wait_enter(&h);CHECK(h.captured);CHECK(vbe_observer_core_pause(&c));CHECK(!vbe_observer_core_stable_paused(&c));CHECK(!vbe_observer_core_resume(&c));release_hold(&h);CHECK(pthread_join(th,NULL)==0);CHECK(vbe_observer_core_stable_paused(&c));hold_destroy(&h);CHECK(vbe_observer_core_resume(&c));
 hold_init(&h,&c);CHECK(pthread_create(&th,NULL,hold_producer,&h)==0);wait_enter(&h);CHECK(h.captured);CHECK(vbe_observer_core_pause(&c));CHECK(!vbe_observer_core_stable_paused(&c));release_hold(&h);CHECK(pthread_join(th,NULL)==0);CHECK(vbe_observer_core_stable_paused(&c));hold_destroy(&h);CHECK(vbe_observer_core_resume(&c));
 hold_init(&h,&c);CHECK(pthread_create(&th,NULL,hold_producer,&h)==0);wait_enter(&h);CHECK(h.captured);vbe_observer_core_quiesce(&c);CHECK(c.state==VBE_OBSERVER_QUIESCING);CHECK(!vbe_observer_core_resume(&c));release_hold(&h);CHECK(pthread_join(th,NULL)==0);CHECK(c.state==VBE_OBSERVER_QUIESCED);hold_destroy(&h);cap=vbe_observer_core_producer_enter(&c);CHECK(!cap);vbe_observer_core_producer_leave(&c);CHECK(c.state==VBE_OBSERVER_QUIESCED);CHECK(!vbe_observer_core_can_unload(&c));vbe_observer_core_quiesce(&c);CHECK(c.state==VBE_OBSERVER_QUIESCED);
 vbe_observer_core_init(&c,REQ);vbe_observer_core_install_failed(&c);CHECK(c.state==VBE_OBSERVER_INERT);CHECK(vbe_observer_core_can_unload(&c));CHECK(!vbe_observer_core_start_capture(&c));
 vbe_observer_core_init(&c,REQ);vbe_observer_core_note_hook(&c,VBE_TRACE_HOOK_CSC_A);vbe_observer_core_install_failed(&c);CHECK(c.state==VBE_OBSERVER_PARTIAL_OWNED);CHECK(!vbe_observer_core_start_capture(&c));CHECK(!vbe_observer_core_resume(&c));CHECK(!vbe_observer_core_can_unload(&c));vbe_observer_core_quiesce(&c);CHECK(c.state==VBE_OBSERVER_PARTIAL_OWNED);
 vbe_observer_core_init(&c,REQ);vbe_observer_core_note_hook(&c,VBE_TRACE_HOOK_CSC_A|VBE_TRACE_HOOK_CSC_B|VBE_TRACE_HOOK_IFTU_ENABLE);vbe_observer_core_install_failed(&c);CHECK(c.state==VBE_OBSERVER_PARTIAL_OWNED);CHECK(!vbe_observer_core_start_capture(&c));CHECK(!vbe_observer_core_can_unload(&c));
 vbe_observer_core_init(&c,REQ);c.owned_hook_mask=REQ^VBE_TRACE_HOOK_PANEL_READ;CHECK(!vbe_observer_core_start_capture(&c));c.owned_hook_mask=REQ;CHECK(vbe_observer_core_start_capture(&c));
 vbe_trace_ring_init(&r);for(ticket=0;ticket<VBE_TRACE_RECORD_CAPACITY;ticket++)CHECK(vbe_trace_ring_reserve(&r,VBE_TRACE_RECORD_CAPACITY,&ticket,&slot));CHECK(!vbe_trace_ring_reserve(&r,VBE_TRACE_RECORD_CAPACITY,&ticket,&slot));CHECK(r.lost==1u);vbe_trace_ring_consume_epoch(&r);CHECK(r.lost==0u);CHECK(vbe_trace_ring_count(&r,VBE_TRACE_RECORD_CAPACITY)==0u);CHECK(vbe_trace_ring_reserve(&r,VBE_TRACE_RECORD_CAPACITY,&ticket,&slot));
 printf("gate0 v7 lifecycle/concurrency/ring invariants: %u PASS\n",checks);return 0;}
