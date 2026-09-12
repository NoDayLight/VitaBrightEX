#pragma once

/* Atomic configuration: every load starts from defaults, parses a temporary
 * candidate, validates it, then publishes one complete snapshot. */
#define CFG_FILE1 "ur0:tai/vitabrightex.cfg"
#define CFG_FILE2 "ux0:tai/vitabrightex.cfg"
#define CFG_MAX_LINE 128

typedef struct {
    /* OLED brightness backend. */
    int  oled_panel_lut_override;
    char panel_lut_path[128];
    int  oled_dim_workaround;

    /* Legacy v1.3 keys retained only so old config files continue to parse.
     * v1.4 does NOT apply arithmetic to opaque OLED panel-control bytes. */
    int  color_r_bias;
    int  color_g_bias;
    int  color_b_bias;
    int  night_mode_enabled;
    int  night_mode_threshold;

    /* LCD optional capabilities.  Only the live SceLcd color-space call has
     * a current implementation. Persistent registry RGB-range writes are not
     * performed; saturation/IPS names are legacy aliases for requesting the
     * live color-space capability, not separate image-processing stages. */
    int  lcd_color_space_mode;
    int  lcd_rgb_range_mode;
    int  lcd_saturation_boost;
    int  lcd_ips_enhance;

    /* Filter ABI compatibility. Hardware invert is implemented. The other
     * fields are preserved and validated but reported unsupported until a
     * verified active-scanout CSC/nonlinear transfer stage exists. */
    int   filter_cct;
    float filter_gamma;
    float filter_contrast;
    float filter_brightness;
    int   filter_invert;
    int   filter_panel_enhance;
} VitaBrightConfig;

extern VitaBrightConfig g_config;
int config_load(void);
