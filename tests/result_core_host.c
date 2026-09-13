#include <stdio.h>
#include "../result_core.h"
#include "../status.h"

static int ok(int condition, const char *name) {
    if (condition) return 0;
    fprintf(stderr, "FAIL: %s\n", name);
    return 1;
}

int main(void) {
    int failures = 0;
    failures += ok(vbe_result_compose(VBE_RESULT_OK, VBE_RESULT_OK) == VBE_RESULT_OK,
                   "success plus success");
    failures += ok(vbe_result_compose(VBE_RESULT_OK, VBE_RESULT_UNSUPPORTED) == VBE_RESULT_UNSUPPORTED,
                   "success plus capability result");
    failures += ok(vbe_result_compose(VBE_RESULT_UNSUPPORTED, -42) == -42,
                   "capability result cannot hide later runtime failure");
    failures += ok(vbe_result_compose(-17, VBE_RESULT_UNSUPPORTED) == -17,
                   "runtime failure dominates later capability result");

    int reload = VBE_RESULT_OK;
    reload = vbe_result_compose(reload, VBE_RESULT_UNSUPPORTED); /* OLED */
    reload = vbe_result_compose(reload, -123);                   /* color/filter */
    failures += ok(reload == -123,
                   "OLED unsupported followed by runtime failure returns runtime failure");

    if (failures) return 1;
    puts("result severity regressions: OK");
    return 0;
}
