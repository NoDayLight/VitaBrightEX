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
    if (open_result == SCE_ERROR_ERRNO_ENOENT) return VBE_SOURCE_NOT_FOUND;
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
