#pragma once

/* Project-owned SceIofilemgr not-found value. Current VitaSDK vita-headers do
 * not expose the generic SCE errno table. Public Vita ecosystem code and
 * Vita3K independently identify 0x80010002 as the missing-path result. Keep
 * the platform value in this one project-owned definition. */
#define VBE_SCE_IO_ERROR_NOT_FOUND ((int)0x80010002u)

#define VBE_SOURCE_PATH_MAX 128

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

typedef enum {
    VBE_SOURCE_ID_NONE = 0,
    VBE_SOURCE_ID_FILE = 1,
    VBE_SOURCE_ID_COMPILED = 2,
} VbeSourceIdentityKind;

typedef struct {
    int kind;
    char path[VBE_SOURCE_PATH_MAX];
} VbeSourceIdentity;

VbeSourceOpenClass vbe_source_classify_open(int open_result);
VbeSourceOutcome vbe_source_evaluate(int open_result,
                                     int read_result,
                                     int parse_result,
                                     int close_result);

void vbe_source_identity_clear(VbeSourceIdentity *identity);
void vbe_source_identity_compiled(VbeSourceIdentity *identity);
int vbe_source_identity_file(VbeSourceIdentity *identity, const char *path);
void vbe_source_identity_copy(VbeSourceIdentity *dst,
                              const VbeSourceIdentity *src);
int vbe_source_identity_is_file(const VbeSourceIdentity *identity);
