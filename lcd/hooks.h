#pragma once
#include <stdint.h>
#include "lcd_lut.h"

int lcd_enable_hooks(void);
int lcd_disable_hooks(void);
int lcd_reload_backend(void);
int lcd_backend_mutation_safe(void);

int vitabrightLcdGetBrightnessValues(uint8_t out[LCD_LUT_LEVELS]);
int vitabrightLcdSetBrightnessValues(uint8_t in[LCD_LUT_LEVELS]);
int vitabrightLcdPersistBrightnessValues(void);
int vitabrightLcdReapplyColor(void);
