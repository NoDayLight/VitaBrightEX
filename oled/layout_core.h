#pragma once
#include "lut.h"

int vbe_oled_stock_signature_match(int panel_type,
                                   const unsigned char actual[LUT_SIZE]);
