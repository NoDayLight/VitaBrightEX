#pragma once
#include <stdint.h>
#define VBE_G1_CSC_SIZE 0x3Cu
static inline void vbe_g1_copy_3c(void *dst,const void *src){uint32_t i;uint8_t *d=(uint8_t*)dst;const volatile uint8_t *s=(const volatile uint8_t*)src;for(i=0;i<VBE_G1_CSC_SIZE;i++)d[i]=s[i];}
static inline int vbe_g1_equal_3c(const void *a,const void *b){uint32_t i;const uint8_t *x=(const uint8_t*)a,*y=(const uint8_t*)b;uint8_t diff=0;for(i=0;i<VBE_G1_CSC_SIZE;i++)diff|=(uint8_t)(x[i]^y[i]);return diff==0;}
static inline uint32_t vbe_g1_hash32_3c(const void *src){uint32_t i,h=2166136261u;const uint8_t *p=(const uint8_t*)src;for(i=0;i<VBE_G1_CSC_SIZE;i++){h^=p[i];h*=16777619u;}return h;}
