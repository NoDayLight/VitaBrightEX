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

    /* v1.3 compatibility keys. The 21 OLED bytes are documented as seven RGB
     * gamma-reference triplets, but the driver-specific register-code-to-
     * voltage conversion needed for arbitrary transfer-curve synthesis is not
     * public. v1.4 therefore preserves these values for config compatibility
     * without applying guessed arithmetic directly to voltage-control codes. */
    int  color_r_bias;
    int  color_g_bias;
    int  color_b_bias;
    int  night_mode_enabled;
    int  night_mode_threshold;

    /* Session-scoped panel colour-space mode. The historical key name is
     * retained internally; config.c also accepts display_color_space_mode.
     * Mode 1 requests the alternate panel colour-space through the matched
     * SceLcd/SceOled Get/SetDisplayColorSpaceMode exports. No registry write
     * is performed. saturation/IPS names remain compatibility aliases for the
     * same switch, not independent image-processing stages. */
    int  lcd_color_space_mode;
    int  lcd_rgb_range_mode;       /* parsed 0..2, unsupported on PCH handheld */
    int  lcd_saturation_boost;
    int  lcd_ips_enhance;

    /* Filter ABI compatibility. Hardware invert is implemented. Arbitrary
     * CCT/gamma/contrast/brightness remain unsupported until a verified
     * active-scanout transfer stage or exact OLED gamma-code model exists. */
    int   filter_cct;
    float filter_gamma;
    float filter_contrast;
    float filter_brightness;
    int   filter_invert;
    int   filter_panel_enhance;
} VitaBrightConfig;

extern VitaBrightConfig g_config;
int config_load(void);
