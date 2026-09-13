#pragma once
#include <stdint.h>
#include "display_domains.h"
#include "screen_filter_types.h"
#include "status.h"

typedef struct {
    uint32_t requested_domains;
    uint32_t attempt_domains;
    uint32_t unsupported_domains;
    int csc_state;
    int transfer_state;
} VbeFilterRequestPolicy;

VbeFilterRequestPolicy vbe_filter_request_policy(const ScreenFilterParams *params);
