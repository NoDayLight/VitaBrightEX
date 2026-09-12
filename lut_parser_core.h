#pragma once
#include <stdint.h>
#include "lcd/lcd_lut.h"
#include "oled/lut.h"
#include "text_stream.h"

typedef struct {
    uint8_t *out;
    VbeTextNewlineDecoder newline;
    unsigned int value;
    int count;
    int state;
    int failed;
} VbeLcdLutParser;

void vbe_lcd_lut_parser_init(VbeLcdLutParser *parser, uint8_t out[LCD_LUT_LEVELS]);
int vbe_lcd_lut_parser_feed(VbeLcdLutParser *parser, unsigned char c);
int vbe_lcd_lut_parser_finish(VbeLcdLutParser *parser);
int vbe_lcd_lut_values_valid(const uint8_t values[LCD_LUT_LEVELS]);

typedef struct {
    uint8_t *out;
    VbeTextNewlineDecoder newline;
    uint8_t row[LUT_LINE_SIZE];
    unsigned int high_nibble;
    int rows;
    int cols;
    int state;
    int failed;
} VbeOledLutParser;

void vbe_oled_lut_parser_init(VbeOledLutParser *parser, uint8_t out[LUT_SIZE]);
int vbe_oled_lut_parser_feed(VbeOledLutParser *parser, unsigned char c);
int vbe_oled_lut_parser_finish(VbeOledLutParser *parser);
