#include "config_parser.h"
#include <limits.h>

#ifndef INT_MAX
#define INT_MAX 2147483647
#endif
#ifndef INT_MIN
#define INT_MIN (-INT_MAX - 1)
#endif

enum {
    CFG_START = 0, CFG_KEY, CFG_AFTER_KEY, CFG_BEFORE_VALUE,
    CFG_VALUE, CFG_IGNORE, CFG_COMMENT,
};

static int streq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        ++a; ++b;
    }
    return *a == '\0' && *b == '\0';
}

static int is_key_char(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static int key_known(const char *key) {
    static const char *const keys[] = {
        "oled_panel_lut_override", "panel_lut_path", "oled_dim_workaround",
        "color_r_bias", "color_g_bias", "color_b_bias",
        "oled_warm_enabled", "oled_warm_first_row", "oled_warm_r_offset",
        "oled_warm_g_offset", "oled_warm_b_offset",
        "night_mode_enabled", "night_mode_threshold",
        "display_color_space_mode", "lcd_color_space_mode",
        "lcd_rgb_range_mode", "lcd_saturation_boost", "lcd_ips_enhance",
        "filter_cct", "filter_gamma", "filter_contrast",
        "filter_brightness", "filter_invert", "filter_panel_enhance",
    };
    for (unsigned int i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
        if (streq(key, keys[i])) return 1;
    return 0;
}

static int parse_int_exact(const char *s, int *out) {
    int negative = 0;
    if (*s == '-' || *s == '+') { negative = *s == '-'; ++s; }
    if (*s < '0' || *s > '9') return -1;
    unsigned long long limit = negative ? (unsigned long long)INT_MAX + 1ull
                                        : (unsigned long long)INT_MAX;
    unsigned long long value = 0;
    int saturated = 0;
    while (*s >= '0' && *s <= '9') {
        unsigned int digit = (unsigned int)(*s - '0');
        if (!saturated) {
            if (value > (limit - digit) / 10ull) {
                value = limit; saturated = 1;
            } else value = value * 10ull + digit;
        }
        ++s;
    }
    if (*s != '\0') return -1;
    if (negative) {
        if (value >= (unsigned long long)INT_MAX + 1ull) *out = INT_MIN;
        else *out = -(int)value;
    } else *out = (int)value;
    return 0;
}

static int parse_float_exact(const char *s, float *out) {
    int negative = 0;
    if (*s == '-' || *s == '+') { negative = *s == '-'; ++s; }
    float value = 0.0f;
    int digits = 0;
    while (*s >= '0' && *s <= '9') {
        value = value < 1000000.0f ? value * 10.0f + (float)(*s - '0') : 1000000.0f;
        ++digits; ++s;
    }
    if (*s == '.') {
        float place = 0.1f; ++s;
        while (*s >= '0' && *s <= '9') {
            if (place > 0.0000001f) value += (float)(*s - '0') * place;
            place *= 0.1f; ++digits; ++s;
        }
    }
    if (digits == 0 || *s != '\0') return -1;
    *out = negative ? -value : value;
    return 0;
}

static int clamp_i(int value, int lo, int hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}
static float clamp_f(float value, float lo, float hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

void vbe_config_defaults(VitaBrightConfig *out) {
    VitaBrightConfig defaults = {
        .oled_panel_lut_override = 0,
        .panel_lut_path = "",
        .oled_dim_workaround = 1,
        .color_r_bias = 0,
        .color_g_bias = 0,
        .color_b_bias = 0,
        .oled_warm_enabled = 0,
        .oled_warm_first_row = 6,
        .oled_warm_r_offset = 0,
        .oled_warm_g_offset = 0,
        .oled_warm_b_offset = 0,
        .night_mode_enabled = 0,
        .night_mode_threshold = 6,
        .display_color_space_mode = 0,
        .display_color_space_scope = VBE_COLOR_SPACE_SCOPE_ANY_PANEL,
        .lcd_rgb_range_mode = 0,
        .filter_cct = 6500,
        .filter_gamma = 1.0f,
        .filter_contrast = 1.0f,
        .filter_brightness = 0.0f,
        .filter_invert = 0,
        .filter_panel_enhance = 0,
    };
    *out = defaults;
}

static void validate(VitaBrightConfig *cfg) {
    cfg->oled_panel_lut_override = !!cfg->oled_panel_lut_override;
    cfg->oled_dim_workaround = !!cfg->oled_dim_workaround;
    cfg->color_r_bias = clamp_i(cfg->color_r_bias, -127, 127);
    cfg->color_g_bias = clamp_i(cfg->color_g_bias, -127, 127);
    cfg->color_b_bias = clamp_i(cfg->color_b_bias, -127, 127);
    cfg->oled_warm_enabled = !!cfg->oled_warm_enabled;
    cfg->oled_warm_first_row = clamp_i(cfg->oled_warm_first_row, 0, 16);
    cfg->oled_warm_r_offset = clamp_i(cfg->oled_warm_r_offset, -127, 127);
    cfg->oled_warm_g_offset = clamp_i(cfg->oled_warm_g_offset, -127, 127);
    cfg->oled_warm_b_offset = clamp_i(cfg->oled_warm_b_offset, -127, 127);
    cfg->night_mode_enabled = !!cfg->night_mode_enabled;
    cfg->night_mode_threshold = clamp_i(cfg->night_mode_threshold, 0, 16);
    cfg->display_color_space_mode = !!cfg->display_color_space_mode;
    if (cfg->display_color_space_scope != VBE_COLOR_SPACE_SCOPE_LCD_ONLY)
        cfg->display_color_space_scope = VBE_COLOR_SPACE_SCOPE_ANY_PANEL;
    cfg->lcd_rgb_range_mode = clamp_i(cfg->lcd_rgb_range_mode, 0, 2);
    cfg->filter_cct = clamp_i(cfg->filter_cct, 1000, 25100);
    cfg->filter_gamma = clamp_f(cfg->filter_gamma, 0.1f, 8.0f);
    cfg->filter_contrast = clamp_f(cfg->filter_contrast, 0.0f, 4.0f);
    cfg->filter_brightness = clamp_f(cfg->filter_brightness, -1.0f, 1.0f);
    cfg->filter_invert = !!cfg->filter_invert;
    cfg->filter_panel_enhance = clamp_i(cfg->filter_panel_enhance, 0, 2);
}

static int copy_path(char *dst, int dst_size, const char *src) {
    int len = 0;
    while (src[len]) ++len;
    if (len >= dst_size) return -1;
    for (int i = 0; i <= len; ++i) dst[i] = src[i];
    return 0;
}

static int apply_color_space_alias(VitaBrightConfig *cfg, const char *key,
                                   const char *value) {
    int mode;
    int canonical = streq(key, "display_color_space_mode");
    int legacy = streq(key, "lcd_color_space_mode") ||
                 streq(key, "lcd_saturation_boost") ||
                 streq(key, "lcd_ips_enhance");
    if (!canonical && !legacy) return 0;
    if (parse_int_exact(value, &mode) < 0) return -1;
    cfg->display_color_space_mode = mode;
    cfg->display_color_space_scope = canonical ? VBE_COLOR_SPACE_SCOPE_ANY_PANEL
                                               : VBE_COLOR_SPACE_SCOPE_LCD_ONLY;
    return 1;
}

static int apply_value(VitaBrightConfig *cfg, const char *key, const char *value) {
    int ivalue = 0;
    float fvalue = 0.0f;
    if (streq(key, "panel_lut_path"))
        return copy_path(cfg->panel_lut_path, (int)sizeof(cfg->panel_lut_path), value);
    int alias = apply_color_space_alias(cfg, key, value);
    if (alias != 0) return alias < 0 ? -1 : 0;
#define APPLY_INT(name, field) \
    if (streq(key, name)) { if (parse_int_exact(value, &ivalue) < 0) return -1; cfg->field = ivalue; return 0; }
#define APPLY_FLOAT(name, field) \
    if (streq(key, name)) { if (parse_float_exact(value, &fvalue) < 0) return -1; cfg->field = fvalue; return 0; }
    APPLY_INT("oled_panel_lut_override", oled_panel_lut_override)
    APPLY_INT("oled_dim_workaround", oled_dim_workaround)
    APPLY_INT("color_r_bias", color_r_bias)
    APPLY_INT("color_g_bias", color_g_bias)
    APPLY_INT("color_b_bias", color_b_bias)
    APPLY_INT("oled_warm_enabled", oled_warm_enabled)
    APPLY_INT("oled_warm_first_row", oled_warm_first_row)
    APPLY_INT("oled_warm_r_offset", oled_warm_r_offset)
    APPLY_INT("oled_warm_g_offset", oled_warm_g_offset)
    APPLY_INT("oled_warm_b_offset", oled_warm_b_offset)
    APPLY_INT("night_mode_enabled", night_mode_enabled)
    APPLY_INT("night_mode_threshold", night_mode_threshold)
    APPLY_INT("lcd_rgb_range_mode", lcd_rgb_range_mode)
    APPLY_INT("filter_cct", filter_cct)
    APPLY_FLOAT("filter_gamma", filter_gamma)
    APPLY_FLOAT("filter_contrast", filter_contrast)
    APPLY_FLOAT("filter_brightness", filter_brightness)
    APPLY_INT("filter_invert", filter_invert)
    APPLY_INT("filter_panel_enhance", filter_panel_enhance)
#undef APPLY_INT
#undef APPLY_FLOAT
    return 0;
}

static void reset_line(VbeConfigParser *parser) {
    parser->key_len = 0; parser->value_len = 0; parser->known_key = 0;
    parser->state = CFG_START;
}

void vbe_config_parser_init(VbeConfigParser *parser, VitaBrightConfig *out) {
    vbe_config_defaults(out);
    parser->out = out;
    vbe_text_newline_init(&parser->newline);
    parser->failed = 0;
    reset_line(parser);
}

static int begin_value(VbeConfigParser *parser) {
    parser->key[parser->key_len] = '\0';
    parser->known_key = key_known(parser->key);
    parser->value_len = 0;
    parser->state = parser->known_key ? CFG_BEFORE_VALUE : CFG_IGNORE;
    return 0;
}

static int finish_value(VbeConfigParser *parser) {
    while (parser->value_len > 0 &&
           (parser->value[parser->value_len - 1] == ' ' ||
            parser->value[parser->value_len - 1] == '\t'))
        --parser->value_len;
    parser->value[parser->value_len] = '\0';
    if (apply_value(parser->out, parser->key, parser->value) < 0) {
        parser->failed = 1; return -1;
    }
    return 0;
}

static int end_line(VbeConfigParser *parser) {
    int ret = 0;
    if (parser->state == CFG_BEFORE_VALUE) {
        parser->value[0] = '\0';
        ret = apply_value(parser->out, parser->key, parser->value);
    } else if (parser->state == CFG_VALUE) ret = finish_value(parser);
    if (ret < 0) { parser->failed = 1; return -1; }
    reset_line(parser);
    return 0;
}

int vbe_config_parser_feed(VbeConfigParser *parser, unsigned char raw) {
    if (parser->failed) return -1;
    unsigned char c = 0;
    int decoded = vbe_text_newline_feed(&parser->newline, raw, &c);
    if (decoded < 0) { parser->failed = 1; return -1; }
    if (decoded == 0) return 0;
    if (c == '\n') return end_line(parser);
    switch (parser->state) {
    case CFG_START:
        if (c == ' ' || c == '\t') return 0;
        if (c == '#' || c == ';') { parser->state = CFG_COMMENT; return 0; }
        if (is_key_char(c)) { parser->key[0] = (char)c; parser->key_len = 1; parser->state = CFG_KEY; return 0; }
        parser->state = CFG_IGNORE; return 0;
    case CFG_KEY:
        if (is_key_char(c)) {
            if (parser->key_len >= VBE_CONFIG_KEY_MAX - 1) { parser->state = CFG_IGNORE; return 0; }
            parser->key[parser->key_len++] = (char)c; return 0;
        }
        if (c == ' ' || c == '\t') { parser->state = CFG_AFTER_KEY; return 0; }
        if (c == '=') return begin_value(parser);
        parser->state = CFG_IGNORE; return 0;
    case CFG_AFTER_KEY:
        if (c == ' ' || c == '\t') return 0;
        if (c == '=') return begin_value(parser);
        parser->state = CFG_IGNORE; return 0;
    case CFG_BEFORE_VALUE:
        if (c == ' ' || c == '\t') return 0;
        parser->value[0] = (char)c; parser->value_len = 1; parser->state = CFG_VALUE; return 0;
    case CFG_VALUE:
        if (parser->value_len >= VBE_CONFIG_VALUE_MAX - 1) { parser->failed = 1; return -1; }
        parser->value[parser->value_len++] = (char)c; return 0;
    case CFG_IGNORE:
    case CFG_COMMENT:
        return 0;
    default:
        parser->failed = 1; return -1;
    }
}

int vbe_config_parser_finish(VbeConfigParser *parser) {
    if (parser->failed || vbe_text_newline_finish(&parser->newline) < 0) {
        parser->failed = 1; return -1;
    }
    if (parser->state != CFG_START && end_line(parser) < 0) return -1;
    validate(parser->out);
    return 0;
}
