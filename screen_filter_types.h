#pragma once
#include <stdint.h>

typedef struct {
    uint16_t cct;
    float gamma;
    float contrast;
    float brightness;
    int invert;
    int panel_enhance;
} ScreenFilterParams;

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
