#include "../lut_parser_core.h"

enum {
    OLED_START = 0,
    OLED_SECOND_NIBBLE,
    OLED_AFTER_BYTE,
    OLED_BETWEEN_BYTES,
    OLED_COMMENT,
};

static int is_space(unsigned char c) {
    return c == ' ' || c == '\t';
}

static int is_comment(unsigned char c) {
    return c == '#' || c == ';';
}

static int is_hex(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') ||
           (c >= 'a' && c <= 'f');
}

static unsigned int hex_value(unsigned char c) {
    if (c >= 'a' && c <= 'f') return (unsigned int)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return (unsigned int)(c - 'A' + 10);
    return (unsigned int)(c - '0');
}

void vbe_oled_lut_parser_init(VbeOledLutParser *parser,
                              uint8_t out[LUT_SIZE]) {
    parser->out = out;
    parser->high_nibble = 0;
    parser->rows = 0;
    parser->cols = 0;
    parser->state = OLED_START;
    parser->failed = 0;
}

static int commit_row(VbeOledLutParser *parser) {
    if (parser->cols != LUT_LINE_SIZE || parser->rows >= LUT_ROWS) {
        parser->failed = 1;
        return -1;
    }
    for (int i = 0; i < LUT_LINE_SIZE; ++i)
        parser->out[parser->rows * LUT_LINE_SIZE + i] = parser->row[i];
    ++parser->rows;
    parser->cols = 0;
    return 0;
}

static int end_line(VbeOledLutParser *parser) {
    if (parser->state == OLED_START || parser->state == OLED_COMMENT) {
        parser->state = OLED_START;
        parser->cols = 0;
        return 0;
    }
    if ((parser->state == OLED_AFTER_BYTE || parser->state == OLED_BETWEEN_BYTES) &&
        parser->cols == LUT_LINE_SIZE) {
        if (commit_row(parser) < 0) return -1;
        parser->state = OLED_START;
        return 0;
    }
    parser->failed = 1;
    return -1;
}

int vbe_oled_lut_parser_feed(VbeOledLutParser *parser, unsigned char c) {
    if (parser->failed) return -1;
    if (c == '\r') return 0;
    if (c == '\n') return end_line(parser);

    switch (parser->state) {
    case OLED_START:
        if (is_space(c)) return 0;
        if (is_comment(c)) {
            parser->state = OLED_COMMENT;
            return 0;
        }
        if (is_hex(c)) {
            parser->high_nibble = hex_value(c);
            parser->state = OLED_SECOND_NIBBLE;
            return 0;
        }
        break;
    case OLED_SECOND_NIBBLE:
        if (!is_hex(c) || parser->cols >= LUT_LINE_SIZE) break;
        parser->row[parser->cols++] =
            (uint8_t)((parser->high_nibble << 4) | hex_value(c));
        parser->state = OLED_AFTER_BYTE;
        return 0;
    case OLED_AFTER_BYTE:
        if (is_space(c)) {
            parser->state = OLED_BETWEEN_BYTES;
            return 0;
        }
        if (is_comment(c) && parser->cols == LUT_LINE_SIZE) {
            if (commit_row(parser) < 0) return -1;
            parser->state = OLED_COMMENT;
            return 0;
        }
        break;
    case OLED_BETWEEN_BYTES:
        if (is_space(c)) return 0;
        if (parser->cols == LUT_LINE_SIZE && is_comment(c)) {
            if (commit_row(parser) < 0) return -1;
            parser->state = OLED_COMMENT;
            return 0;
        }
        if (parser->cols < LUT_LINE_SIZE && is_hex(c)) {
            parser->high_nibble = hex_value(c);
            parser->state = OLED_SECOND_NIBBLE;
            return 0;
        }
        break;
    case OLED_COMMENT:
        return 0;
    default:
        break;
    }

    parser->failed = 1;
    return -1;
}

int vbe_oled_lut_parser_finish(VbeOledLutParser *parser) {
    if (parser->failed) return -1;
    if (parser->state == OLED_AFTER_BYTE || parser->state == OLED_BETWEEN_BYTES) {
        if (parser->cols != LUT_LINE_SIZE || commit_row(parser) < 0) return -1;
    } else if (parser->state != OLED_START && parser->state != OLED_COMMENT) {
        parser->failed = 1;
        return -1;
    }
    parser->state = OLED_START;
    if (parser->rows != LUT_ROWS) {
        parser->failed = 1;
        return -1;
    }
    return 0;
}
