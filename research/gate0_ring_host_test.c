#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include "diagnostics/gate0_trace/trace_ring_core.h"
#define CAP 64u
static VbeTraceRingCore r;
static void *producer(void *x){unsigned i;uint32_t t,s;(void)x;for(i=0;i<1000;i++)(void)vbe_trace_ring_reserve(&r,CAP,&t,&s);return 0;}
int main(void){uint32_t t,s,i;pthread_t th[4];vbe_trace_ring_init(&r);for(i=0;i<CAP;i++){assert(vbe_trace_ring_reserve(&r,CAP,&t,&s));assert(t==i);assert(s==i);}assert(!vbe_trace_ring_reserve(&r,CAP,&t,&s));assert(r.lost==1);assert(vbe_trace_ring_count(&r,CAP)==CAP);vbe_trace_ring_consume_epoch(&r);assert(vbe_trace_ring_count(&r,CAP)==0);assert(r.lost==0);assert(vbe_trace_ring_reserve(&r,CAP,&t,&s));assert(s==(CAP+1)%CAP);vbe_trace_ring_consume_epoch(&r);for(i=0;i<4;i++)assert(!pthread_create(&th[i],0,producer,0));for(i=0;i<4;i++)pthread_join(th[i],0);assert(vbe_trace_ring_count(&r,CAP)==CAP);assert(r.lost==4000-CAP);puts("gate0 ring host tests: PASS");return 0;}
