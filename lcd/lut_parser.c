#include "../lut_parser_core.h"

enum {
    LCD_START = 0,
    LCD_VALUE,
    LCD_AFTER_VALUE,
    LCD_COMMENT,
};

static int is_space(unsigned char c) {
    return c == ' ' || c == '\t';
}

static int is_comment(unsigned char c) {
    return c == '#' || c == ';';
}

int vbe_lcd_lut_values_valid(const uint8_t values[LCD_LUT_LEVELS]) {
    for (int i = 1; i < LCD_LUT_LEVELS; ++i) {
        if (values[i] < values[i - 1]) return 0;
    }
    return 1;
}

void vbe_lcd_lut_parser_init(VbeLcdLutParser *parser,
                             uint8_t out[LCD_LUT_LEVELS]) {
    parser->out = out;
    parser->value = 0;
    parser->count = 0;
    parser->state = LCD_START;
    parser->failed = 0;
}

static int commit_value(VbeLcdLutParser *parser) {
    if (parser->count >= LCD_LUT_LEVELS || parser->value > 255u) {
        parser->failed = 1;
        return -1;
    }
    if (parser->count > 0 && parser->value < parser->out[parser->count - 1]) {
        parser->failed = 1;
        return -1;
    }
    parser->out[parser->count++] = (uint8_t)parser->value;
    parser->value = 0;
    return 0;
}

static int end_line(VbeLcdLutParser *parser) {
    if (parser->state == LCD_VALUE || parser->state == LCD_AFTER_VALUE) {
        if (commit_value(parser) < 0) return -1;
    }
    parser->state = LCD_START;
    parser->value = 0;
    return 0;
}

int vbe_lcd_lut_parser_feed(VbeLcdLutParser *parser, unsigned char c) {
    if (parser->failed) return -1;
    if (c == '\r') return 0;
    if (c == '\n') return end_line(parser);

    switch (parser->state) {
    case LCD_START:
        if (is_space(c)) return 0;
        if (is_comment(c)) {
            parser->state = LCD_COMMENT;
            return 0;
        }
        if (c >= '0' && c <= '9') {
            parser->value = (unsigned int)(c - '0');
            parser->state = LCD_VALUE;
            return 0;
        }
        break;
    case LCD_VALUE:
        if (c >= '0' && c <= '9') {
            parser->value = parser->value * 10u + (unsigned int)(c - '0');
            if (parser->value <= 255u) return 0;
            break;
        }
        if (is_space(c)) {
            parser->state = LCD_AFTER_VALUE;
            return 0;
        }
        if (is_comment(c)) {
            if (commit_value(parser) < 0) return -1;
            parser->state = LCD_COMMENT;
            return 0;
        }
        break;
    case LCD_AFTER_VALUE:
        if (is_space(c)) return 0;
        if (is_comment(c)) {
            if (commit_value(parser) < 0) return -1;
            parser->state = LCD_COMMENT;
            return 0;
        }
        break;
    case LCD_COMMENT:
        return 0;
    default:
        break;
    }

    parser->failed = 1;
    return -1;
}

int vbe_lcd_lut_parser_finish(VbeLcdLutParser *parser) {
    if (parser->failed) return -1;
    if (parser->state == LCD_VALUE || parser->state == LCD_AFTER_VALUE) {
        if (commit_value(parser) < 0) return -1;
    }
    parser->state = LCD_START;
    if (parser->count != LCD_LUT_LEVELS) {
        parser->failed = 1;
        return -1;
    }
    return 0;
}
