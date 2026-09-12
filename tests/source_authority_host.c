#include <stdio.h>
#include "../source_authority.h"

static int check(int condition, const char *name) {
    if (condition) return 0;
    fprintf(stderr, "FAIL: %s\n", name);
    return 1;
}

int main(void) {
    int failures = 0;
    VbeSourceOutcome o;

    failures += check(vbe_source_classify_open(4) == VBE_SOURCE_OPENED, "open ok");
    failures += check(vbe_source_classify_open(SCE_ERROR_ERRNO_ENOENT) == VBE_SOURCE_NOT_FOUND, "not found");
    failures += check(vbe_source_classify_open((int)0x8001000Du) == VBE_SOURCE_IO_ERROR, "open io error");

    o = vbe_source_evaluate(4, 0, 0, 0);
    failures += check(o.decision == VBE_SOURCE_USE && o.error == 0, "use preferred");

    o = vbe_source_evaluate(SCE_ERROR_ERRNO_ENOENT, 0, 0, 0);
    failures += check(o.decision == VBE_SOURCE_FALLBACK && o.stage == VBE_SOURCE_STAGE_OPEN, "fallback only on not found");

    o = vbe_source_evaluate((int)0x8001000Du, 0, 0, 0);
    failures += check(o.decision == VBE_SOURCE_FAIL && o.stage == VBE_SOURCE_STAGE_OPEN, "open failure terminal");

    o = vbe_source_evaluate(4, (int)0x80010005u, 0, 0);
    failures += check(o.decision == VBE_SOURCE_FAIL && o.stage == VBE_SOURCE_STAGE_READ, "read failure terminal");

    o = vbe_source_evaluate(4, 0, -1, 0);
    failures += check(o.decision == VBE_SOURCE_FAIL && o.stage == VBE_SOURCE_STAGE_PARSE, "parse failure terminal");

    o = vbe_source_evaluate(4, 0, 0, (int)0x80010005u);
    failures += check(o.decision == VBE_SOURCE_FAIL && o.stage == VBE_SOURCE_STAGE_CLOSE, "close failure terminal");

    VbeSourceOutcome primary = vbe_source_evaluate(SCE_ERROR_ERRNO_ENOENT, 0, 0, 0);
    VbeSourceOutcome fallback = vbe_source_evaluate(7, 0, 0, 0);
    failures += check(primary.decision == VBE_SOURCE_FALLBACK && fallback.decision == VBE_SOURCE_USE, "missing primary uses valid fallback");

    fallback = vbe_source_evaluate(7, 0, -1, 0);
    failures += check(primary.decision == VBE_SOURCE_FALLBACK && fallback.decision == VBE_SOURCE_FAIL, "malformed fallback terminal");

    if (failures) return 1;
    puts("production source-authority regressions: OK");
    return 0;
}
