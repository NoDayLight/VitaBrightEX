#pragma once
#include <stdint.h>

typedef enum { VBE_HW_UNKNOWN = 0, VBE_HW_OLED = 1, VBE_HW_LCD = 2 } VbeHardware;
typedef enum { VBE_CAP_UNKNOWN = 0, VBE_CAP_UNAVAILABLE = 1, VBE_CAP_INACTIVE = 2, VBE_CAP_ACTIVE = 3, VBE_CAP_FAILED = 4, VBE_CAP_UNSUPPORTED = 5 } VbeCapabilityState;

typedef enum {
    VBE_RESULT_OK = 0,
    /* Capability result, not a runtime failure. Callers must handle this
     * explicitly instead of treating every negative result as an error. */
    VBE_RESULT_UNSUPPORTED = -2,
} VbeResult;

typedef enum {
    VBE_ERR_NONE = 0, VBE_ERR_CONFIG = 1, VBE_ERR_BACKEND = 2,
    VBE_ERR_FIRMWARE_UNSUPPORTED = 3, VBE_ERR_EXPORT_RESOLUTION = 4,
    VBE_ERR_TABLE_INJECTION = 5, VBE_ERR_BRIGHTNESS_HOOK = 6,
    VBE_ERR_POWER_HOOK = 7, VBE_ERR_INVALID_USER_INPUT = 8,
    VBE_ERR_DISPLAY_CAPABILITY = 9, VBE_ERR_LAYOUT_MISMATCH = 10,
    VBE_ERR_SYNCHRONIZATION = 11, VBE_ERR_LUT_ROLLBACK = 12,
    VBE_ERR_SOURCE_IO = 13, VBE_ERR_RESOURCE_RELEASE = 14,
} VbeError;

typedef enum {
    VBE_ERROR_DOMAIN_CONFIG = 0,
    VBE_ERROR_DOMAIN_BRIGHTNESS = 1,
    VBE_ERROR_DOMAIN_COLOR_SPACE = 2,
    VBE_ERROR_DOMAIN_FILTER = 3,
    VBE_ERROR_DOMAIN_INPUT = 4,
    VBE_ERROR_DOMAIN_SYNC = 5,
    VBE_ERROR_DOMAIN_COUNT = 6,
} VbeErrorDomain;

typedef struct {
    uint32_t abi_version;
    uint32_t firmware;
    int hardware;
    int panel_type;
    int state_lock;
    int firmware_layout;
    int brightness_core;
    int brightness_table;
    int brightness_hook;
    int power_limit_hook;
    int invert;
    union { int display_color_space; int lcd_color_space; };
    int csc_filter;
    int transfer_lut;
    int registry_api;
    /* Compatibility summary only. Internal code must use error domains. */
    int last_error;
    int last_error_detail;
} VitaBrightStatus;

typedef struct {
    uint32_t abi_version;
    int config_error, config_detail;
    int brightness_error, brightness_detail;
    int color_space_error, color_space_detail;
    int filter_error, filter_detail;
    int input_error, input_detail;
    int synchronization_error, synchronization_detail;
} VitaBrightDiagnostics;

extern VitaBrightStatus g_vbe_status;
void status_init(int hardware, uint32_t firmware);
void status_set_error_domain(int domain, int error, int detail);
void status_clear_error_domain(int domain);
void status_stage_result(int domain, int succeeded, int error, int detail);
void status_get_error_domain(int domain, int *error, int *detail);
void status_recovery_result(int domain, int succeeded,
                            int requested_error, int requested_detail,
                            int recovery_error, int recovery_detail);
int vitabrightGetStatus(VitaBrightStatus *out);
int vitabrightGetDiagnostics(VitaBrightDiagnostics *out);
