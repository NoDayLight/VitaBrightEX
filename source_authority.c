#include "source_authority.h"

static VbeSourceOutcome outcome(int decision, int stage, int error) {
    VbeSourceOutcome out;
    out.decision = decision;
    out.stage = stage;
    out.error = error;
    return out;
}

VbeSourceOpenClass vbe_source_classify_open(int open_result) {
    if (open_result >= 0) return VBE_SOURCE_OPENED;
    if (open_result == VBE_SCE_IO_ERROR_NOT_FOUND) return VBE_SOURCE_NOT_FOUND;
    return VBE_SOURCE_IO_ERROR;
}

VbeSourceOutcome vbe_source_evaluate(int open_result,
                                     int read_result,
                                     int parse_result,
                                     int close_result) {
    VbeSourceOpenClass open_class = vbe_source_classify_open(open_result);
    if (open_class == VBE_SOURCE_NOT_FOUND)
        return outcome(VBE_SOURCE_FALLBACK, VBE_SOURCE_STAGE_OPEN, open_result);
    if (open_class == VBE_SOURCE_IO_ERROR)
        return outcome(VBE_SOURCE_FAIL, VBE_SOURCE_STAGE_OPEN, open_result);

    if (read_result < 0)
        return outcome(VBE_SOURCE_FAIL, VBE_SOURCE_STAGE_READ, read_result);
    if (parse_result < 0)
        return outcome(VBE_SOURCE_FAIL, VBE_SOURCE_STAGE_PARSE, parse_result);
    if (close_result < 0)
        return outcome(VBE_SOURCE_FAIL, VBE_SOURCE_STAGE_CLOSE, close_result);

    return outcome(VBE_SOURCE_USE, VBE_SOURCE_STAGE_NONE, 0);
}

void vbe_source_identity_clear(VbeSourceIdentity *identity) {
    if (identity == 0) return;
    identity->kind = VBE_SOURCE_ID_NONE;
    identity->path[0] = '\0';
}

void vbe_source_identity_compiled(VbeSourceIdentity *identity) {
    if (identity == 0) return;
    identity->kind = VBE_SOURCE_ID_COMPILED;
    identity->path[0] = '\0';
}

int vbe_source_identity_file(VbeSourceIdentity *identity, const char *path) {
    if (identity == 0 || path == 0) return -1;

    int i = 0;
    while (i < VBE_SOURCE_PATH_MAX - 1 && path[i] != '\0') {
        identity->path[i] = path[i];
        ++i;
    }
    if (path[i] != '\0') {
        vbe_source_identity_clear(identity);
        return -1;
    }

    identity->path[i] = '\0';
    identity->kind = VBE_SOURCE_ID_FILE;
    return 0;
}

void vbe_source_identity_copy(VbeSourceIdentity *dst,
                              const VbeSourceIdentity *src) {
    if (dst == 0 || src == 0) return;
    dst->kind = src->kind;
    int i = 0;
    while (i < VBE_SOURCE_PATH_MAX - 1 && src->path[i] != '\0') {
        dst->path[i] = src->path[i];
        ++i;
    }
    dst->path[i] = '\0';
}

int vbe_source_identity_is_file(const VbeSourceIdentity *identity) {
    return identity != 0 && identity->kind == VBE_SOURCE_ID_FILE &&
           identity->path[0] != '\0';
}
