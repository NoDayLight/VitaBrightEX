#pragma once
#include "lut.h"

#define LUT_SOURCE_PATH_MAX 128

/* Strictly parse one explicit LUT. Returns 0 on success. An explicit path has
 * no fallback semantics: NOT_FOUND, I/O, parse and close failures all return
 * the underlying negative result. */
int parse_lut_from_file(const char *path, unsigned char out[LUT_SIZE]);

/* Resolve one authoritative source for a panel. Search order is panel-specific
 * ur0 -> panel-specific ux0 -> generic ur0 -> generic ux0. ONLY explicit
 * SCE_ERROR_ERRNO_ENOENT advances to the next candidate. Once a source opens,
 * read/parse/close failure is terminal. On success source_path identifies the
 * exact source; on failure error_code classifies parse vs I/O for diagnostics. */
int parse_lut(int panel_type, unsigned char out[LUT_SIZE],
              char source_path[LUT_SOURCE_PATH_MAX], int *error_code);

/* Explicit config override: never falls back. */
int parse_lut_override(const char *path, unsigned char out[LUT_SIZE],
                       char source_path[LUT_SOURCE_PATH_MAX], int *error_code);
