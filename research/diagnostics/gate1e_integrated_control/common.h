#pragma once
#include <stdint.h>
#include "matrix_backend.h"

typedef struct {
    int32_t action_result;
    VbeMatrixBackendStatus status;
} VbeGate1eActionBundle;

typedef struct {
    VbeMatrixCapabilities capabilities;
    VbeMatrixBackendStatus status;
} VbeGate1eStatusBundle;

typedef struct {
    int32_t injection_result;
    int32_t action_result;
    VbeMatrixBackendStatus status;
} VbeGate1eRollbackBundle;

int gate1e_write_file(const char *path, const void *data, uint32_t size);
