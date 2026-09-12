#pragma once
#include "lut.h"
#include "../source_authority.h"

#define LUT_SOURCE_PATH_MAX VBE_SOURCE_PATH_MAX

int parse_lut_from_file(const char *path, unsigned char out[LUT_SIZE]);

int parse_lut(int panel_type, unsigned char out[LUT_SIZE],
              char source_path[LUT_SOURCE_PATH_MAX], int *error_code);

int parse_lut_override(const char *path, unsigned char out[LUT_SIZE],
                       char source_path[LUT_SOURCE_PATH_MAX], int *error_code);
