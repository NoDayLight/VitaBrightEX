#pragma once

#define CFG_FILE1 "ur0:tai/vitabrightex.cfg"
#define CFG_FILE2 "ux0:tai/vitabrightex.cfg"

typedef struct {
    int  oled_panel_lut_override;
    char panel_lut_path[128];
    int  oled_dim_workaround;

    int  color_r_bias;
    int  color_g_bias;
    int  color_b_bias;
    int  night_mode_enabled;
    int  night_mode_threshold;

    int  lcd_color_space_mode;
    int  lcd_rgb_range_mode;
    int  lcd_saturation_boost;
    int  lcd_ips_enhance;

    int   filter_cct;
    float filter_gamma;
    float filter_contrast;
    float filter_brightness;
    int   filter_invert;
    int   filter_panel_enhance;
} VitaBrightConfig;

extern VitaBrightConfig g_config;
void config_reset_defaults(void);
int config_load(void);
