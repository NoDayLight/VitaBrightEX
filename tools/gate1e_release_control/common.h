#pragma once
#include <stdint.h>
#include "build_info.h"
#include "matrix_backend.h"
typedef struct {
    char build_id[VBE_BUILD_ID_SIZE];
    uint8_t pad[3];
    VbeMatrixCapabilities capabilities;
    VbeMatrixBackendStatus status;
} VbeReleasePreflightBundle;
typedef struct {
    int32_t action_result;
    VbeMatrixBackendStatus status;
} VbeReleaseActionBundle;
int vbe_release_write_file(const char *path, const void *data, uint32_t size);
