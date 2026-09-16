#pragma once
#include <stdint.h>
#include "../../../matrix_backend.h"

#define VBE_G1E_MAGIC 0x45314756u /* VG1E */
#define VBE_G1E_VERSION 1u
#define VBE_G1E_EVENT_CAPACITY 16u
#define VBE_G1E_OBSERVER_MASK 0x3u
#define VBE_G1E_NODE_1 1u
#define VBE_G1E_NODE_2 2u
#define VBE_G1E_PHASE_ENTER 1u
#define VBE_G1E_PHASE_EXIT 2u
#define VBE_G1E_STATUS_SIZE 1548u
#define VBE_G1E_BUNDLE_SIZE 2340u

#define VBE_G1E_FLAG_OBSERVERS_READY (1u << 0)
#define VBE_G1E_FLAG_EXPORT_RESOLVED (1u << 1)
#define VBE_G1E_FLAG_REPLAY_COMPLETE (1u << 2)
#define VBE_G1E_FLAG_EVENT_OVERFLOW (1u << 3)

#define VBE_G1E_FAIL_HOOK1 (1u << 0)
#define VBE_G1E_FAIL_HOOK2 (1u << 1)
#define VBE_G1E_FAIL_EXPORT (1u << 2)

#define VBE_G1E_ERR_STATE (-1)
#define VBE_G1E_ERR_NOT_READY (-2)
#define VBE_G1E_ERR_ALREADY_RUN (-3)

#define VBE_G1E_SKIPPED_RETURN ((int32_t)0x80010001)

typedef struct {
    uint32_t sequence;
    uint32_t node;
    uint32_t phase;
    int32_t plane;
    uint32_t thread_id;
    uint32_t control_epoch;
    uint32_t source_valid;
    int32_t raw_return;
    uint32_t source_words[VBE_B_OBJECT_WORDS];
} VbeG1eObserverEvent;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t flags;
    uint32_t owned_hook_mask;
    uint32_t fail_mask;
    uint32_t export_entry;
    uint32_t control_epoch;
    uint32_t control_thread_id;
    uint32_t control_run_count;
    uint32_t concurrent_control_event_count;
    uint32_t recursion_or_overlap_count;
    uint32_t event_count;
    uint32_t replay_attempted;
    int32_t replay_return_p0;
    int32_t replay_return_p1;
    uint32_t reserved[4];
    VbeG1eObserverEvent events[VBE_G1E_EVENT_CAPACITY];
} VbeG1eObserverStatus;

typedef struct {
    uint32_t magic;
    uint32_t version;
    int32_t before_status_return;
    int32_t replay_return;
    int32_t observer_status_return;
    int32_t after_status_return;
    VbeMatrixBackendStatus before;
    VbeG1eObserverStatus observer;
    VbeMatrixBackendStatus after;
} VbeG1eIdentityBundle;

#define VBE_G1E_ASSERT(n,e) typedef char n[(e)?1:-1]
VBE_G1E_ASSERT(g1e_event_size, sizeof(VbeG1eObserverEvent) == 92u);
VBE_G1E_ASSERT(g1e_status_size, sizeof(VbeG1eObserverStatus) == VBE_G1E_STATUS_SIZE);
VBE_G1E_ASSERT(g1e_bundle_size, sizeof(VbeG1eIdentityBundle) == VBE_G1E_BUNDLE_SIZE);
#undef VBE_G1E_ASSERT

int vbeG1eReplayIdentity(void);
int vbeG1eGetStatus(VbeG1eObserverStatus *out);
