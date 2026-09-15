#pragma once
#include <stdint.h>
#include "matrix_backend.h"

typedef struct {
    int32_t action_result;
    VbeMatrixBackendStatus status;
} VbeGate1dActionBundle;

typedef struct {
    VbeMatrixCapabilities capabilities;
    VbeMatrixBackendStatus status;
} VbeGate1dStatusBundle;

int gate1d_write_file(const char *path, const void *data, uint32_t size);
