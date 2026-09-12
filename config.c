#include "config.h"
#include "log.h"
#include <psp2kern/io/fcntl.h>

static const VitaBrightConfig k_default_config = {
    .oled_panel_lut_override  = 0,
    .panel_lut_path           = "",
    .color_r_bias             = 0,
    .color_g_bias             = 0,
    .color_b_bias             = 0,
    .night_mode_enabled       = 0,
    .night_mode_threshold     = 6,
    .lcd_color_space_mode     = 0,
    .lcd_rgb_range_mode       = 0,
    .lcd_saturation_boost     = 0,
    .lcd_ips_enhance          = 0,
    .oled_dim_workaround      = 1,
    .filter_cct               = 6500,
    .filter_gamma             = 1.0f,
    .filter_contrast          = 1.0f,
    .filter_brightness        = 0.0f,
    .filter_invert            = 0,
    .filter_panel_enhance     = 0,
};

VitaBrightConfig g_config = {
    .night_mode_threshold = 6,
    .oled_dim_workaround = 1,
    .filter_cct = 6500,
    .filter_gamma = 1.0f,
    .filter_contrast = 1.0f,
};

static int cfg_streq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static void cfg_strncpy(char *dst, const char *src, int dst_size) {
    int i;
    for (i = 0; i < dst_size - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static int cfg_atoi(const char *s) {
    int neg = 0, val = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    return neg ? -val : val;
}

static float cfg_atof(const char *s) {
    int neg = 0;
    float val = 0.0f;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') {
        val = val * 10.0f + (float)(*s - '0');
        s++;
    }
    if (*s == '.') {
        float place = 0.1f;
        s++;
        while (*s >= '0' && *s <= '9') {
            val += (float)(*s - '0') * place;
            place *= 0.1f;
            s++;
        }
    }
    return neg ? -val : val;
}

static int cfg_readline(SceUID fd, char *buf, int len) {
    int total = 0;
    char c;
    while (total < len - 1) {
        int r = ksceIoRead(fd, &c, 1);
        if (r <= 0) {
            buf[total] = '\0';
            return total == 0 ? -1 : total;
        }
        if (c == '\r') continue;
        if (c == '\n') break;
        buf[total++] = c;
    }
    buf[total] = '\0';
    return total;
}

static const char *cfg_ltrim(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static const char *cfg_split(const char *line) {
    while (*line && *line != '=') line++;
    return *line == '=' ? line + 1 : 0;
}

static int clamp_i(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float clamp_f(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void cfg_apply(VitaBrightConfig *cfg, const char *key, const char *val) {
    if      (cfg_streq(key, "oled_panel_lut_override")) cfg->oled_panel_lut_override = cfg_atoi(val);
    else if (cfg_streq(key, "panel_lut_path")) cfg_strncpy(cfg->panel_lut_path, val, sizeof(cfg->panel_lut_path));
    else if (cfg_streq(key, "color_r_bias")) cfg->color_r_bias = cfg_atoi(val);
    else if (cfg_streq(key, "color_g_bias")) cfg->color_g_bias = cfg_atoi(val);
    else if (cfg_streq(key, "color_b_bias")) cfg->color_b_bias = cfg_atoi(val);
    else if (cfg_streq(key, "night_mode_enabled")) cfg->night_mode_enabled = cfg_atoi(val);
    else if (cfg_streq(key, "night_mode_threshold")) cfg->night_mode_threshold = cfg_atoi(val);
    else if (cfg_streq(key, "lcd_color_space_mode")) cfg->lcd_color_space_mode = cfg_atoi(val);
    else if (cfg_streq(key, "lcd_rgb_range_mode")) cfg->lcd_rgb_range_mode = cfg_atoi(val);
    else if (cfg_streq(key, "lcd_saturation_boost")) cfg->lcd_saturation_boost = cfg_atoi(val);
    else if (cfg_streq(key, "lcd_ips_enhance")) cfg->lcd_ips_enhance = cfg_atoi(val);
    else if (cfg_streq(key, "oled_dim_workaround")) cfg->oled_dim_workaround = cfg_atoi(val);
    else if (cfg_streq(key, "filter_cct")) cfg->filter_cct = cfg_atoi(val);
    else if (cfg_streq(key, "filter_gamma")) cfg->filter_gamma = cfg_atof(val);
    else if (cfg_streq(key, "filter_contrast")) cfg->filter_contrast = cfg_atof(val);
    else if (cfg_streq(key, "filter_brightness")) cfg->filter_brightness = cfg_atof(val);
    else if (cfg_streq(key, "filter_invert")) cfg->filter_invert = cfg_atoi(val);
    else if (cfg_streq(key, "filter_panel_enhance")) cfg->filter_panel_enhance = cfg_atoi(val);
}

static void cfg_validate(VitaBrightConfig *cfg) {
    cfg->oled_panel_lut_override = !!cfg->oled_panel_lut_override;
    cfg->color_r_bias = clamp_i(cfg->color_r_bias, -127, 127);
    cfg->color_g_bias = clamp_i(cfg->color_g_bias, -127, 127);
    cfg->color_b_bias = clamp_i(cfg->color_b_bias, -127, 127);
    cfg->night_mode_enabled = !!cfg->night_mode_enabled;
    cfg->night_mode_threshold = clamp_i(cfg->night_mode_threshold, 0, 16);
    cfg->lcd_color_space_mode = !!cfg->lcd_color_space_mode;
    cfg->lcd_rgb_range_mode = !!cfg->lcd_rgb_range_mode;
    cfg->lcd_saturation_boost = !!cfg->lcd_saturation_boost;
    cfg->lcd_ips_enhance = !!cfg->lcd_ips_enhance;
    cfg->oled_dim_workaround = !!cfg->oled_dim_workaround;
    cfg->filter_cct = clamp_i(cfg->filter_cct, 1000, 25100);
    cfg->filter_gamma = clamp_f(cfg->filter_gamma, 0.1f, 8.0f);
    cfg->filter_contrast = clamp_f(cfg->filter_contrast, 0.0f, 4.0f);
    cfg->filter_brightness = clamp_f(cfg->filter_brightness, -1.0f, 1.0f);
    cfg->filter_invert = !!cfg->filter_invert;
    cfg->filter_panel_enhance = clamp_i(cfg->filter_panel_enhance, 0, 2);
}

int config_load(void) {
    VitaBrightConfig candidate = k_default_config;
    SceUID fd = ksceIoOpen(CFG_FILE1, SCE_O_RDONLY, 6);

    if (fd >= 0) {
        LOG("[CFG] Loaded from %s\n", CFG_FILE1);
    } else {
        fd = ksceIoOpen(CFG_FILE2, SCE_O_RDONLY, 6);
        if (fd >= 0) LOG("[CFG] Loaded from %s\n", CFG_FILE2);
    }

    if (fd >= 0) {
        char line[CFG_MAX_LINE];
        while (1) {
            int n = cfg_readline(fd, line, sizeof(line));
            if (n < 0) break;

            const char *trimmed = cfg_ltrim(line);
            if (!trimmed[0] || trimmed[0] == '#' || trimmed[0] == ';') continue;

            const char *val = cfg_split(trimmed);
            if (!val) continue;

            int key_len = (int)(val - trimmed) - 1;
            if (key_len <= 0 || key_len >= CFG_MAX_LINE) continue;

            char key[CFG_MAX_LINE];
            int k = 0;
            for (int i = 0; i < key_len; i++) {
                char ch = trimmed[i];
                if (ch != ' ' && ch != '\t') key[k++] = ch;
            }
            key[k] = '\0';
            cfg_apply(&candidate, key, cfg_ltrim(val));
        }
        ksceIoClose(fd);
    } else {
        LOG("[CFG] No config file found; using defaults\n");
    }

    cfg_validate(&candidate);
    g_config = candidate;
    return 0;
}
