#include <stdio.h>
#include "../source_authority.h"

#define TEST_OPEN_ERROR  (-12345)
#define TEST_READ_ERROR  (-23456)
#define TEST_CLOSE_ERROR (-34567)

static int check(int condition, const char *name) {
    if (condition) return 0;
    fprintf(stderr, "FAIL: %s\n", name);
    return 1;
}

int main(void) {
    int failures = 0;
    VbeSourceOutcome o;

    failures += check(vbe_source_classify_open(4) == VBE_SOURCE_OPENED, "open ok");
    failures += check(vbe_source_classify_open(VBE_SCE_IO_ERROR_NOT_FOUND) == VBE_SOURCE_NOT_FOUND, "not found");
    failures += check(vbe_source_classify_open(TEST_OPEN_ERROR) == VBE_SOURCE_IO_ERROR, "open io error");

    o = vbe_source_evaluate(4, 0, 0, 0);
    failures += check(o.decision == VBE_SOURCE_USE && o.error == 0, "use preferred");

    o = vbe_source_evaluate(VBE_SCE_IO_ERROR_NOT_FOUND, 0, 0, 0);
    failures += check(o.decision == VBE_SOURCE_FALLBACK && o.stage == VBE_SOURCE_STAGE_OPEN, "fallback only on not found");

    o = vbe_source_evaluate(TEST_OPEN_ERROR, 0, 0, 0);
    failures += check(o.decision == VBE_SOURCE_FAIL && o.stage == VBE_SOURCE_STAGE_OPEN, "open failure terminal");

    o = vbe_source_evaluate(4, TEST_READ_ERROR, 0, 0);
    failures += check(o.decision == VBE_SOURCE_FAIL && o.stage == VBE_SOURCE_STAGE_READ, "read failure terminal");

    o = vbe_source_evaluate(4, VBE_SCE_IO_ERROR_NOT_FOUND, 0, 0);
    failures += check(o.decision == VBE_SOURCE_FAIL && o.stage == VBE_SOURCE_STAGE_READ,
                      "ENOENT after successful open is read failure, never fallback");

    o = vbe_source_evaluate(4, 0, -1, 0);
    failures += check(o.decision == VBE_SOURCE_FAIL && o.stage == VBE_SOURCE_STAGE_PARSE, "parse failure terminal");

    o = vbe_source_evaluate(4, 0, 0, TEST_CLOSE_ERROR);
    failures += check(o.decision == VBE_SOURCE_FAIL && o.stage == VBE_SOURCE_STAGE_CLOSE, "close failure terminal");

    o = vbe_source_evaluate(4, 0, 0, VBE_SCE_IO_ERROR_NOT_FOUND);
    failures += check(o.decision == VBE_SOURCE_FAIL && o.stage == VBE_SOURCE_STAGE_CLOSE,
                      "ENOENT on close is terminal, never fallback");

    VbeSourceOutcome primary = vbe_source_evaluate(VBE_SCE_IO_ERROR_NOT_FOUND, 0, 0, 0);
    VbeSourceOutcome fallback = vbe_source_evaluate(7, 0, 0, 0);
    failures += check(primary.decision == VBE_SOURCE_FALLBACK && fallback.decision == VBE_SOURCE_USE, "missing primary uses valid fallback");

    fallback = vbe_source_evaluate(7, 0, -1, 0);
    failures += check(primary.decision == VBE_SOURCE_FALLBACK && fallback.decision == VBE_SOURCE_FAIL, "malformed fallback terminal");

    fallback = vbe_source_evaluate(VBE_SCE_IO_ERROR_NOT_FOUND, 0, 0, 0);
    failures += check(primary.decision == VBE_SOURCE_FALLBACK && fallback.decision == VBE_SOURCE_FALLBACK,
                      "both missing remains explicit no-source decision for subsystem policy");

    VbeSourceIdentity source;
    VbeSourceIdentity copy;
    vbe_source_identity_clear(&source);
    failures += check(source.kind == VBE_SOURCE_ID_NONE && source.path[0] == '\0', "none source identity");
    vbe_source_identity_compiled(&source);
    failures += check(source.kind == VBE_SOURCE_ID_COMPILED && source.path[0] == '\0', "compiled source has no fake path");
    failures += check(!vbe_source_identity_is_file(&source), "compiled source is not persistent file");
    failures += check(vbe_source_identity_file(&source, "ux0:tai/example.txt") == 0, "file source accepted");
    failures += check(vbe_source_identity_is_file(&source), "file source is persistent");
    vbe_source_identity_copy(&copy, &source);
    failures += check(copy.kind == VBE_SOURCE_ID_FILE && copy.path[0] == 'u' && copy.path[1] == 'x', "source identity copy");

    if (failures) return 1;
    puts("production source-authority regressions: OK");
    return 0;
}
