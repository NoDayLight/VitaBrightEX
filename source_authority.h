#pragma once

/* Generic SCE errno code returned by SceIofilemgr when a path does not exist.
 * VitaSDK's public vita-headers do not currently expose the generic
 * SCE_ERROR_ERRNO_* table, so keep the platform name/value here once, guarded
 * so a future SDK definition wins automatically. */
#ifndef SCE_ERROR_ERRNO_ENOENT
#define SCE_ERROR_ERRNO_ENOENT ((int)0x80010002u)
#endif

typedef enum {
    VBE_SOURCE_OPENED = 0,
    VBE_SOURCE_NOT_FOUND = 1,
    VBE_SOURCE_IO_ERROR = 2,
} VbeSourceOpenClass;

typedef enum {
    VBE_SOURCE_USE = 0,
    VBE_SOURCE_FALLBACK = 1,
    VBE_SOURCE_FAIL = 2,
} VbeSourceDecision;

typedef enum {
    VBE_SOURCE_STAGE_NONE = 0,
    VBE_SOURCE_STAGE_OPEN = 1,
    VBE_SOURCE_STAGE_READ = 2,
    VBE_SOURCE_STAGE_PARSE = 3,
    VBE_SOURCE_STAGE_CLOSE = 4,
} VbeSourceFailureStage;

typedef struct {
    int decision;
    int stage;
    int error;
} VbeSourceOutcome;

VbeSourceOpenClass vbe_source_classify_open(int open_result);
VbeSourceOutcome vbe_source_evaluate(int open_result,
                                     int read_result,
                                     int parse_result,
                                     int close_result);
