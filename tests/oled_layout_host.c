#include <stdio.h>
#include <string.h>
#include "../oled/layout_core.h"
#include "../generated/oled_stock_signatures.h"

static int ok(int condition, const char *name) {
    if (condition) return 0;
    fprintf(stderr, "FAIL: %s\n", name);
    return 1;
}

int main(void) {
    int failures = 0;
    unsigned char copy[LUT_SIZE];

    failures += ok(vbe_oled_stock_signature_match(OLED_PANEL_4,
                                                   vbe_oled_stock_p4) == 0,
                   "P4 exact stock signature authorizes");
    failures += ok(vbe_oled_stock_signature_match(OLED_PANEL_5,
                                                   vbe_oled_stock_p5) == 0,
                   "P5 exact stock signature authorizes");
    failures += ok(vbe_oled_stock_signature_match(OLED_PANEL_6,
                                                   vbe_oled_stock_generic) == 0,
                   "P6 exact generic stock signature authorizes");
    failures += ok(vbe_oled_stock_signature_match(OLED_PANEL_UNKNOWN,
                                                   vbe_oled_stock_generic) == 0,
                   "unknown panel requires and accepts exact generic stock only");

    memcpy(copy, vbe_oled_stock_p4, LUT_SIZE);
    copy[211] ^= 1;
    failures += ok(vbe_oled_stock_signature_match(OLED_PANEL_4, copy) < 0,
                   "one-byte mismatch rejects before injection");
    failures += ok(vbe_oled_stock_signature_match(OLED_PANEL_5,
                                                   vbe_oled_stock_p4) < 0,
                   "wrong panel stock at expected offset rejects");
    failures += ok(vbe_oled_stock_signature_match(OLED_PANEL_UNKNOWN,
                                                   vbe_oled_stock_p4) < 0,
                   "unknown panel cannot borrow P4 authorization");

    if (failures) return 1;
    puts("OLED exact stock-signature authorization regressions: OK");
    return 0;
}
