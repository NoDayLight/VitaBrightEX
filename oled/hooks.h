#pragma once
#include <stdint.h>
#include "lut.h"
#include "transform_state.h"

int oled_enable_hooks(void);
int oled_disable_hooks(void);
int oled_reload_backend(void);
int oled_backend_mutation_safe(void);

int oled_detect_panel(void);
int oled_reinject_lut(void);

int vitabrightOledGetLevel(void);
int vitabrightOledSetLevel(unsigned int level);
int vitabrightOledGetLut(unsigned char oledLut[LUT_SIZE]);
int vitabrightOledSetLut(unsigned char oledLut[LUT_SIZE]);
int vitabrightOledPersistLut(void);
int vitabrightOledReload(void);
int vitabrightOledGetPanelType(void);
int vitabrightOledGetTransformState(VitaBrightOledTransformState *out);
