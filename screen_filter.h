#pragma once
#include <stdint.h>
#include "screen_filter_types.h"

#define VBE_DISPLAY_FILTER_STATE_ABI_VERSION 1u

typedef struct {
    uint32_t abi_version;
    ScreenFilterParams requested;
    ScreenFilterParams committed;
    uint32_t requested_domains;
    uint32_t committed_domains;
    uint32_t unsupported_domains;
    uint32_t failed_domains;
} VitaBrightDisplayFilterState;

int screen_filter_apply_config(void);
int screen_filter_reset(int is_oled_unused);

int vitabrightFilterGetParams(ScreenFilterParams *out);
int vitabrightFilterGetState(VitaBrightDisplayFilterState *out);
int vitabrightFilterSetParams(const ScreenFilterParams *in, int is_oled_unused);
int vitabrightFilterReset(int is_oled_unused);
