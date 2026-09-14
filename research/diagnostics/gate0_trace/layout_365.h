#pragma once

#include <stdint.h>

/* Exact retail-3.65 SceLcd runtime bindings. Static module hashes are CI guards;
 * these signatures independently guard the two private offset hooks at runtime. */
#define VBE_LCD_PANEL_SEGMENT             0
#define VBE_LCD_PANEL_WRITER_OFFSET       0xA54u
#define VBE_LCD_PANEL_READER_OFFSET       0x5B4u
#define VBE_LCD_STATE_SEGMENT             1
#define VBE_LCD_STATE_OFFSET              0xA0u
#define VBE_LCD_PRIVATE_SIGNATURE_LENGTH  16u

static const uint8_t vbe_lcd_panel_writer_signature[VBE_LCD_PRIVATE_SIGNATURE_LENGTH] = {
    0x2D, 0xE9, 0xF8, 0x43, 0x42, 0xF2, 0x00, 0x07,
    0xC8, 0xF2, 0x00, 0x17, 0x05, 0x46, 0x89, 0x46
};

static const uint8_t vbe_lcd_panel_reader_signature[VBE_LCD_PRIVATE_SIGNATURE_LENGTH] = {
    0x2D, 0xE9, 0xF8, 0x4F, 0x42, 0xF2, 0x00, 0x06,
    0xC8, 0xF2, 0x00, 0x16, 0x81, 0x46, 0x0F, 0x46
};
