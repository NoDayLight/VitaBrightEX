#pragma once
#include <stdint.h>

/*
 * v1.4 filter state.
 *
 * Hardware invert is currently the only verified generic screen-filter
 * capability. LCD CCT/contrast requires a verified persistent scanout CSC
 * path; gamma/panel linearisation requires a verified nonlinear transfer
 * stage. Those parameters are retained in the ABI for editor compatibility
 * and capability reporting, but are not silently approximated.
 */
typedef struct {
    uint16_t cct;
    float gamma;
    float contrast;
    float brightness;
    int invert;
    int panel_enhance;
} ScreenFilterParams;

extern ScreenFilterParams g_screen_filter;

#define CCT_DEFAULT         6500
#define CCT_AQUARIUM        10000
#define CCT_OVERCAST_SKY    7500
#define CCT_DAYLIGHT        5500
#define CCT_FLUORESCENT     4200
#define CCT_HALOGEN         3400
#define CCT_INCANDESCENT    2700
#define CCT_WARM_INCAN      2300
#define CCT_CANDLE          1900
#define CCT_EMBER           1200

void screen_filter_load_config(void);
int screen_filter_apply(int is_oled);
void screen_filter_set_cct(uint16_t cct, int is_oled);
void screen_filter_reset(int is_oled);

int vitabrightFilterGetParams(ScreenFilterParams *out);
int vitabrightFilterSetParams(const ScreenFilterParams *in, int is_oled);
int vitabrightFilterReset(int is_oled);
