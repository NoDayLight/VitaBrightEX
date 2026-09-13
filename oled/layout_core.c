#include "layout_core.h"
#include "../generated/oled_stock_signatures.h"

static const unsigned char *stock_for_panel(int panel_type) {
    if (panel_type == OLED_PANEL_4) return vbe_oled_stock_p4;
    if (panel_type == OLED_PANEL_5) return vbe_oled_stock_p5;
    if (panel_type == OLED_PANEL_6 || panel_type == OLED_PANEL_UNKNOWN)
        return vbe_oled_stock_generic;
    return 0;
}

int vbe_oled_stock_signature_match(int panel_type,
                                   const unsigned char actual[LUT_SIZE]) {
    const unsigned char *expected = stock_for_panel(panel_type);
    if (expected == 0) return -1;
    for (int i = 0; i < LUT_SIZE; ++i) {
        if (actual[i] != expected[i]) return -(0x400 + i);
    }
    return 0;
}
