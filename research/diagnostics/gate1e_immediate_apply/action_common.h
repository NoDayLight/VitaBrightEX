#pragma once
#include <stdint.h>
#include "gate1e_immediate_protocol.h"

int gate1e_write_file(const char *path, const void *data, uint32_t size);
