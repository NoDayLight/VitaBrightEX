#pragma once
#include <stdint.h>
#include "lut.h"

extern int (*ksceOledGetBrightness)(void);
extern int (*ksceOledSetBrightness)(unsigned int brightness);
extern int (*ksceOledGetDDB)(uint16_t *supplier_id, uint16_t *supplier_elective_data);

/* Last committed/injected panel LUT. */
extern unsigned char lookupNew[LUT_SIZE];

/* Lifecycle: success is explicit; failure leaves no partial active state. */
int oled_enable_hooks(void);
void oled_disable_hooks(void);

int oled_detect_panel(void);
int oled_reinject_lut(void);

int vitabrightOledGetLevel(void);
int vitabrightOledSetLevel(unsigned int level);
int vitabrightOledGetLut(unsigned char oledLut[LUT_SIZE]);
int vitabrightOledSetLut(unsigned char oledLut[LUT_SIZE]);
int vitabrightOledReload(void);
int vitabrightOledGetPanelType(void);
