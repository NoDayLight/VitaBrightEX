#pragma once

#include <stdint.h>

/* Hardware selected once at boot. */
typedef enum {
    VBE_HW_UNKNOWN = 0,
    VBE_HW_OLED = 1,
    VBE_HW_LCD = 2,
} VbeHardware;

/* Capability state is explicit: unavailable is different from failed. */
typedef enum {
    VBE_CAP_UNKNOWN = 0,
    VBE_CAP_UNAVAILABLE = 1,
    VBE_CAP_INACTIVE = 2,
    VBE_CAP_ACTIVE = 3,
    VBE_CAP_FAILED = 4,
    VBE_CAP_UNSUPPORTED = 5,
} VbeCapabilityState;

typedef enum {
    VBE_ERR_NONE = 0,
    VBE_ERR_CONFIG = 1,
    VBE_ERR_BACKEND = 2,
    VBE_ERR_FIRMWARE_UNSUPPORTED = 3,
    VBE_ERR_EXPORT_RESOLUTION = 4,
    VBE_ERR_TABLE_INJECTION = 5,
    VBE_ERR_BRIGHTNESS_HOOK = 6,
    VBE_ERR_POWER_HOOK = 7,
    VBE_ERR_INVALID_USER_INPUT = 8,
    VBE_ERR_DISPLAY_CAPABILITY = 9,
} VbeError;

typedef struct {
    uint32_t abi_version;
    uint32_t firmware;
    int hardware;

    int brightness_core;
    int brightness_table;
    int brightness_hook;
    int power_limit_hook;
    int invert;
    int lcd_color_space;
    int csc_filter;
    int transfer_lut;
    int registry_api;

    int last_error;
    int last_error_detail;
} VitaBrightStatus;

extern VitaBrightStatus g_vbe_status;

void status_init(int hardware, uint32_t firmware);
void status_set_error(int error, int detail);
void status_clear_error(void);

int vitabrightGetStatus(VitaBrightStatus *out);
