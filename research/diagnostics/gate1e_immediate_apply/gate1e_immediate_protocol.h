#pragma once
#include <stdint.h>
#include "../../../matrix_backend.h"
#include "../gate1e_chain_reentry/gate1e_chain_protocol.h"

#define VBE_G1E_IMMEDIATE_MAGIC 0x49453156u /* V1EI */
#define VBE_G1E_IMMEDIATE_VERSION 1u
#define VBE_G1E_IMMEDIATE_ACTION_MILD 1u
#define VBE_G1E_IMMEDIATE_ACTION_RESET 2u
#define VBE_G1E_IMMEDIATE_HEADER_SIZE 48u
#define VBE_G1E_IMMEDIATE_BUNDLE_SIZE 2748u

#define VBE_G1E_IMMEDIATE_ERR_STATE (-1)
#define VBE_G1E_IMMEDIATE_ERR_NOT_READY (-2)
#define VBE_G1E_IMMEDIATE_ERR_BUSY (-3)

#define VBE_G1E_CONTROL_IDLE 0u
#define VBE_G1E_CONTROL_PREPARE 1u
#define VBE_G1E_CONTROL_RECORD 2u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t action_kind;
    int32_t before_status_return;
    int32_t action_result;
    int32_t published_status_return;
    int32_t replay_return;
    int32_t observer_status_return;
    int32_t after_status_return;
    uint32_t reserved[3];
    VbeMatrixBackendStatus before;
    VbeMatrixBackendStatus published;
    VbeG1eObserverStatus observer;
    VbeMatrixBackendStatus after;
} VbeG1eImmediateBundle;

#define VBE_G1E_IMMEDIATE_ASSERT(n,e) typedef char n[(e)?1:-1]
VBE_G1E_IMMEDIATE_ASSERT(g1e_immediate_header_size,
    (sizeof(VbeG1eImmediateBundle) - sizeof(VbeMatrixBackendStatus) * 3u - sizeof(VbeG1eObserverStatus)) == VBE_G1E_IMMEDIATE_HEADER_SIZE);
VBE_G1E_IMMEDIATE_ASSERT(g1e_immediate_bundle_size,
    sizeof(VbeG1eImmediateBundle) == VBE_G1E_IMMEDIATE_BUNDLE_SIZE);
#undef VBE_G1E_IMMEDIATE_ASSERT

int vbeG1eImmediateReplayCanonical(void);
int vbeG1eImmediateGetStatus(VbeG1eObserverStatus *out);
