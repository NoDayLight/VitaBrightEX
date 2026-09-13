#pragma once
#include <stdint.h>
#include "screen_filter_types.h"

typedef struct {
    ScreenFilterParams requested;
    ScreenFilterParams committed;
    uint32_t requested_domains;
    uint32_t committed_domains;
    uint32_t unsupported_domains;
    uint32_t failed_domains;
} VbeFilterStateCore;

void vbe_filter_params_neutral(ScreenFilterParams *params);
void vbe_filter_state_init(VbeFilterStateCore *state);
void vbe_filter_state_begin_request(VbeFilterStateCore *state,
                                    const ScreenFilterParams *requested,
                                    uint32_t requested_domains,
                                    uint32_t unsupported_domains);
void vbe_filter_state_commit_invert(VbeFilterStateCore *state, int enabled);
void vbe_filter_state_mark_failed(VbeFilterStateCore *state, uint32_t domains);
