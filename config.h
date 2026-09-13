#pragma once
#include "source_authority.h"

#define CFG_FILE1 "ur0:tai/vitabrightex.cfg"
#define CFG_FILE2 "ux0:tai/vitabrightex.cfg"
#define VBE_CONFIG_PATH_MAX 128

enum {
    VBE_COLOR_SPACE_SCOPE_ANY_PANEL = 0,
    VBE_COLOR_SPACE_SCOPE_LCD_ONLY = 1,
};

typedef struct {
    int oled_panel_lut_override;
    char panel_lut_path[VBE_CONFIG_PATH_MAX];
    int oled_dim_workaround;
    int color_r_bias;
    int color_g_bias;
    int color_b_bias;
    int oled_warm_enabled;
    int oled_warm_first_row;
    int oled_warm_r_offset;
    int oled_warm_g_offset;
    int oled_warm_b_offset;
    int night_mode_enabled;
    int night_mode_threshold;
    int display_color_space_mode;
    int display_color_space_scope;
    int lcd_rgb_range_mode;
    int filter_cct;
    float filter_gamma;
    float filter_contrast;
    float filter_brightness;
    int filter_invert;
    int filter_panel_enhance;
} VitaBrightConfig;

typedef struct {
    VitaBrightConfig config;
    VbeSourceIdentity source;
} VbeConfigCandidate;

extern VitaBrightConfig g_config;

void config_reset_defaults(void);
int config_load_candidate(VbeConfigCandidate *out);
void config_commit_request(const VbeConfigCandidate *candidate);
void config_get_source(VbeSourceIdentity *out);
