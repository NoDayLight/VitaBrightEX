#pragma once

#include "screen_filter.h"
#include "status.h"

typedef struct {
    int result;
    int csc_state;
    int transfer_state;
    int error;
} VbeFilterRequestPolicy;

VbeFilterRequestPolicy vbe_filter_request_policy(const ScreenFilterParams *params);
