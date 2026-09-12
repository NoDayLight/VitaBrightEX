#pragma once
#include "lut.h"

#define LUT_SOURCE_PATH_MAX 128

/* Strictly parse one explicit LUT. Returns 0 on success, negative on open or
 * format failure. A valid file contains exactly 17 data rows x 21 hex bytes;
 * blank/comment lines are allowed. */
int parse_lut_from_file(const char *path, unsigned char out[LUT_SIZE]);

/* Resolve one authoritative source for a panel. Search order is panel-specific
 * ur0 -> panel-specific ux0 -> generic ur0 -> generic ux0. A source is skipped
 * only when it cannot be opened. If it opens but is malformed, loading fails
 * rather than silently switching to another file. On success source_path is
 * the exact path that produced `out`. */
int parse_lut(int panel_type, unsigned char out[LUT_SIZE],
              char source_path[LUT_SOURCE_PATH_MAX]);

/* Explicit config override. On success source_path is copied from path. */
int parse_lut_override(const char *path, unsigned char out[LUT_SIZE],
                       char source_path[LUT_SOURCE_PATH_MAX]);
