#include "hooks.h"
#include "lcd_lut.h"
#include "../color_space.h"
#include "../config.h"
#include "../log.h"
#include "../main.h"
#include "../state_lock.h"
#include "../status.h"
#include "../taihen_extra.h"
#include <stdint.h>
#include <psp2kern/io/fcntl.h>
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysmem.h>
#include <taihen.h>

#define NID_LCD_GET_BRIGHTNESS   0x3A6D6AC3
#define NID_LCD_SET_BRIGHTNESS   0x581D3A87
#define NID_POWER_SET_MAX_BRIGHT 0x77027B6B
#define LCD_DIMMED_VALUE         25u

static const uint8_t lcd_brightness_default[LCD_LUT_LEVELS] = {
    1, 3, 5, 8, 13, 20, 29, 41, 57, 76, 95, 116, 137, 161, 190, 220, 255
};

/* Stock table documented in the original VitaBright source. Raw offsets are
 * accepted only when the loaded SceLcd image still contains this signature. */
static const uint8_t lcd_stock_signature[LCD_LUT_LEVELS] = {
    31, 37, 43, 50, 58, 67, 77, 88, 100, 114, 129, 147, 166, 182, 203, 227, 255
};

static uint8_t lcd_brightness_values[LCD_LUT_LEVELS];
static SceUID lcd_table_inject = -1;
static SceUID lcd_set_brightness_hook = -1;
static SceUID power_set_max_bright_hook = -1;
static tai_hook_ref_t lcd_set_brightness_ref = -1;
static tai_hook_ref_t power_set_max_bright_ref = 0;
static int (*ksceLcdGetBrightness)(void) = NULL;
static int (*ksceLcdSetBrightness)(unsigned int brightness) = NULL;
static int g_lcd_hooks_active = 0;

static void lut_copy(uint8_t *dst, const uint8_t *src) {
    for (int i = 0; i < LCD_LUT_LEVELS; ++i) dst[i] = src[i];
}

static int lut_is_valid(const uint8_t values[LCD_LUT_LEVELS]) {
    for (int i = 1; i < LCD_LUT_LEVELS; ++i) {
        if (values[i] < values[i - 1]) return 0;
    }
    return 1;
}

/* Preserve original VitaBright semantics: GetBrightness returns the logical
 * 2..65536 coordinate used to select one of 17 table entries. The injected
 * 8-bit table is the output mapping, not that coordinate itself. */
static int lcd_brightness_to_index(unsigned int brightness) {
    if (brightness <= 2u) return 0;
    if (brightness >= 0x10000u) return LCD_LUT_LEVELS - 1;
    return (int)(16u * (brightness - 2u) / 65534u);
}

/* Parse one strict decimal LUT line. Returns 1 for blank/comment, 0 for a
 * value, negative for malformed data.  Silently clamping malformed user data
 * makes reload status dishonest, so values above 255 are rejected. */
static int parse_decimal_line(const char *line, int len, uint8_t *out) {
    int i = 0;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) ++i;
    if (i == len || line[i] == '#') return 1;

    unsigned int value = 0;
    int digits = 0;
    while (i < len && line[i] >= '0' && line[i] <= '9') {
        value = value * 10u + (unsigned int)(line[i] - '0');
        if (value > 255u) return -1;
        ++i;
        ++digits;
    }
    if (!digits) return -1;

    while (i < len && (line[i] == ' ' || line[i] == '\t')) ++i;
    if (i < len && line[i] != '#') return -1;

    *out = (uint8_t)value;
    return 0;
}

static int lcd_parse_lut_file(const char *path,
                              uint8_t out[LCD_LUT_LEVELS],
                              int *opened) {
    SceUID fd = ksceIoOpen(path, SCE_O_RDONLY, 6);
    if (fd < 0) {
        *opened = 0;
        return fd;
    }
    *opened = 1;

    int count = 0;
    char line[64];
    int li = 0;
    int malformed = 0;

    while (1) {
        char c = 0;
        int r = ksceIoRead(fd, &c, 1);
        int at_eof = r <= 0;

        if (!at_eof && c == '\r') continue;

        if (at_eof || c == '\n') {
            if (li > 0) {
                uint8_t value = 0;
                int parsed = parse_decimal_line(line, li, &value);
                if (parsed < 0) {
                    malformed = 1;
                    break;
                }
                if (parsed == 0) {
                    if (count >= LCD_LUT_LEVELS) {
                        malformed = 1;
                        break;
                    }
                    out[count++] = value;
                }
            }
            li = 0;
            if (at_eof) break;
            continue;
        }

        if (li >= (int)sizeof(line)) {
            malformed = 1;
            break;
        }
        line[li++] = c;
    }

    ksceIoClose(fd);
    if (malformed || count != LCD_LUT_LEVELS || !lut_is_valid(out)) return -1;
    return 0;
}

/* ur0 is authoritative when present. ux0 is consulted only when ur0 does not
 * exist/open. A malformed authoritative file is an error, not a request to
 * silently fall back to another table. If neither exists, use safe defaults. */
static int lcd_load_disk_candidate(uint8_t out[LCD_LUT_LEVELS]) {
    int opened = 0;
    int ret = lcd_parse_lut_file(LCD_LUT_FILE1, out, &opened);
    if (opened) return ret;

    ret = lcd_parse_lut_file(LCD_LUT_FILE2, out, &opened);
    if (opened) return ret;

    lut_copy(out, lcd_brightness_default);
    return 0;
}

/* Keep this whitelist identical to the raw-layout provenance in original
 * VitaBright. 3.71-3.74 remain unsupported until independently verified. */
static int lcd_get_table_offset(uint32_t firmware, uint32_t *out) {
    switch (firmware >> 16) {
    case 0x360:
        *out = 0x1B00u;
        return 0;
    case 0x365:
    case 0x367:
    case 0x368:
    case 0x369:
    case 0x370:
        *out = 0x1B48u;
        return 0;
    default:
        return -1;
    }
}

static int lcd_validate_layout(const tai_module_info_t *info, uint32_t table_off) {
    uintptr_t address = 0;
    int ret = module_get_offset(KERNEL_PID, info->modid, 0, table_off, &address);
    if (ret < 0 || address == 0) return ret < 0 ? ret : -1;

    const volatile uint8_t *actual = (const volatile uint8_t *)address;
    for (int i = 0; i < LCD_LUT_LEVELS; ++i) {
        if (actual[i] != lcd_stock_signature[i]) return -(0x100 + i);
    }
    return 0;
}

int hook_ksceLcdSetBrightness(unsigned int brightness) {
    if (brightness != 1 || ksceLcdGetBrightness == NULL)
        return TAI_CONTINUE(int, lcd_set_brightness_ref, brightness);

    int old_raw = ksceLcdGetBrightness();
    if (old_raw < 0)
        return TAI_CONTINUE(int, lcd_set_brightness_ref, brightness);

    unsigned int old_brightness = (unsigned int)old_raw;
    int old_index = lcd_brightness_to_index(old_brightness);

    /* Inactivity dim maps to approximately 25 in the stock driver. Only
     * accept it when doing so cannot make a deliberately darker custom table
     * brighter than its current level. */
    if (old_brightness >= 2u && lcd_brightness_values[old_index] >= LCD_DIMMED_VALUE)
        return TAI_CONTINUE(int, lcd_set_brightness_ref, brightness);
    return TAI_CONTINUE(int, lcd_set_brightness_ref, old_brightness);
}

int hook_kscePowerSetDisplayMaxBrightnessForLcd(int limit) {
    (void)limit;
    if (power_set_max_bright_ref == 0) return 0;
    return TAI_CONTINUE(int, power_set_max_bright_ref, 0x10000);
}

static void lcd_release_transaction(void) {
    if (power_set_max_bright_hook >= 0) {
        (void)taiHookReleaseForKernel(power_set_max_bright_hook, power_set_max_bright_ref);
        power_set_max_bright_hook = -1;
        power_set_max_bright_ref = 0;
    }
    if (lcd_set_brightness_hook >= 0) {
        (void)taiHookReleaseForKernel(lcd_set_brightness_hook, lcd_set_brightness_ref);
        lcd_set_brightness_hook = -1;
        lcd_set_brightness_ref = -1;
    }
    if (lcd_table_inject >= 0) {
        (void)taiInjectReleaseForKernel(lcd_table_inject);
        lcd_table_inject = -1;
    }

    g_lcd_hooks_active = 0;
    if (g_vbe_status.brightness_core == VBE_CAP_ACTIVE)
        g_vbe_status.brightness_core = VBE_CAP_INACTIVE;
    if (g_vbe_status.brightness_table == VBE_CAP_ACTIVE)
        g_vbe_status.brightness_table = VBE_CAP_INACTIVE;
    if (g_vbe_status.brightness_hook == VBE_CAP_ACTIVE)
        g_vbe_status.brightness_hook = VBE_CAP_INACTIVE;
    if (g_vbe_status.power_limit_hook == VBE_CAP_ACTIVE)
        g_vbe_status.power_limit_hook = VBE_CAP_INACTIVE;
}

static int lcd_resolve_core(tai_module_info_t *info) {
    info->size = sizeof(*info);
    int ret = taiGetModuleInfoForKernel(KERNEL_PID, "SceLcd", info);
    if (ret < 0) return ret;

    ksceLcdGetBrightness = NULL;
    ksceLcdSetBrightness = NULL;
    ret = module_get_export_func(KERNEL_PID, "SceLcd", TAI_ANY_LIBRARY,
        NID_LCD_GET_BRIGHTNESS, (uintptr_t *)&ksceLcdGetBrightness);
    if (ret < 0 || ksceLcdGetBrightness == NULL) return ret < 0 ? ret : -1;

    ret = module_get_export_func(KERNEL_PID, "SceLcd", TAI_ANY_LIBRARY,
        NID_LCD_SET_BRIGHTNESS, (uintptr_t *)&ksceLcdSetBrightness);
    if (ret < 0 || ksceLcdSetBrightness == NULL) return ret < 0 ? ret : -1;
    return 0;
}

static int lcd_start_transaction(const uint8_t candidate[LCD_LUT_LEVELS]) {
    if (g_lcd_hooks_active) return 0;
    if (!lut_is_valid(candidate)) return -1;

    tai_module_info_t info;
    int ret = lcd_resolve_core(&info);
    if (ret < 0) {
        g_vbe_status.brightness_core = VBE_CAP_FAILED;
        status_set_error(VBE_ERR_EXPORT_RESOLUTION, ret);
        return ret;
    }

    uint32_t table_off = 0;
    if (lcd_get_table_offset(sw_version, &table_off) < 0) {
        g_vbe_status.firmware_layout = VBE_CAP_UNSUPPORTED;
        g_vbe_status.brightness_table = VBE_CAP_UNSUPPORTED;
        status_set_error(VBE_ERR_FIRMWARE_UNSUPPORTED, (int)sw_version);
        return -1;
    }

    ret = lcd_validate_layout(&info, table_off);
    if (ret < 0) {
        g_vbe_status.firmware_layout = VBE_CAP_FAILED;
        g_vbe_status.brightness_table = VBE_CAP_FAILED;
        status_set_error(VBE_ERR_LAYOUT_MISMATCH, ret);
        return ret;
    }
    g_vbe_status.firmware_layout = VBE_CAP_ACTIVE;

    lcd_table_inject = taiInjectDataForKernel(KERNEL_PID, info.modid, 0,
        table_off, candidate, LCD_LUT_LEVELS);
    if (lcd_table_inject < 0) {
        ret = (int)lcd_table_inject;
        status_set_error(VBE_ERR_TABLE_INJECTION, ret);
        lcd_release_transaction();
        g_vbe_status.brightness_table = VBE_CAP_FAILED;
        return ret;
    }
    g_vbe_status.brightness_table = VBE_CAP_ACTIVE;

    /* Publish before the hook can observe the candidate. A failed later step
     * tears down the transaction; live replacement restores the old table. */
    lut_copy(lcd_brightness_values, candidate);

    lcd_set_brightness_hook = taiHookFunctionExportForKernel(KERNEL_PID,
        &lcd_set_brightness_ref, "SceLcd", TAI_ANY_LIBRARY,
        NID_LCD_SET_BRIGHTNESS, hook_ksceLcdSetBrightness);
    if (lcd_set_brightness_hook < 0) {
        ret = (int)lcd_set_brightness_hook;
        status_set_error(VBE_ERR_BRIGHTNESS_HOOK, ret);
        lcd_release_transaction();
        g_vbe_status.brightness_hook = VBE_CAP_FAILED;
        return ret;
    }
    g_vbe_status.brightness_hook = VBE_CAP_ACTIVE;

    power_set_max_bright_hook = taiHookFunctionExportForKernel(KERNEL_PID,
        &power_set_max_bright_ref, "ScePower", TAI_ANY_LIBRARY,
        NID_POWER_SET_MAX_BRIGHT, hook_kscePowerSetDisplayMaxBrightnessForLcd);
    if (power_set_max_bright_hook < 0) {
        ret = (int)power_set_max_bright_hook;
        status_set_error(VBE_ERR_POWER_HOOK, ret);
        lcd_release_transaction();
        g_vbe_status.power_limit_hook = VBE_CAP_FAILED;
        return ret;
    }
    g_vbe_status.power_limit_hook = VBE_CAP_ACTIVE;

    int current = ksceLcdGetBrightness();
    if (current < 0) {
        ret = current;
        status_set_error(VBE_ERR_BACKEND, ret);
        lcd_release_transaction();
        g_vbe_status.brightness_core = VBE_CAP_FAILED;
        return ret;
    }

    ret = ksceLcdSetBrightness((unsigned int)current);
    if (ret < 0) {
        status_set_error(VBE_ERR_BACKEND, ret);
        lcd_release_transaction();
        g_vbe_status.brightness_core = VBE_CAP_FAILED;
        return ret;
    }

    g_lcd_hooks_active = 1;
    g_vbe_status.brightness_core = VBE_CAP_ACTIVE;
    return 0;
}

static int lcd_replace_candidate(const uint8_t candidate[LCD_LUT_LEVELS]) {
    if (!lut_is_valid(candidate)) return -1;

    uint8_t previous[LCD_LUT_LEVELS];
    int had_previous = g_lcd_hooks_active;
    if (had_previous) lut_copy(previous, lcd_brightness_values);

    lcd_release_transaction();
    int ret = lcd_start_transaction(candidate);
    if (ret >= 0) return ret;

    int original_error = g_vbe_status.last_error;
    int original_detail = g_vbe_status.last_error_detail;
    if (had_previous) {
        int rollback = lcd_start_transaction(previous);
        if (rollback < 0) {
            status_set_error(VBE_ERR_LUT_ROLLBACK, rollback);
            return ret;
        }
        status_set_error(original_error, original_detail);
    }
    return ret;
}

int lcd_enable_hooks(void) {
    if (g_lcd_hooks_active) return 0;

    uint8_t candidate[LCD_LUT_LEVELS];
    int ret = lcd_load_disk_candidate(candidate);
    if (ret < 0) {
        status_set_error(VBE_ERR_INVALID_USER_INPUT, ret);
        return ret;
    }
    return lcd_start_transaction(candidate);
}

void lcd_disable_hooks(void) {
    lcd_release_transaction();
    ksceLcdGetBrightness = NULL;
    ksceLcdSetBrightness = NULL;
}

int lcd_reload_backend(void) {
    VitaBrightConfig previous_config = g_config;
    uint8_t candidate[LCD_LUT_LEVELS];

    int ret = config_load();
    if (ret < 0) {
        g_config = previous_config;
        status_set_error(VBE_ERR_CONFIG, ret);
        return ret;
    }

    ret = lcd_load_disk_candidate(candidate);
    if (ret < 0) {
        g_config = previous_config;
        status_set_error(VBE_ERR_INVALID_USER_INPUT, ret);
        return ret;
    }

    ret = lcd_replace_candidate(candidate);
    if (ret < 0) {
        g_config = previous_config;
        return ret;
    }
    return 0;
}

int vitabrightLcdGetBrightnessValues(uint8_t out[LCD_LUT_LEVELS]) {
    int state;
    ENTER_SYSCALL(state);
    int ret = state_lock_acquire();
    if (ret < 0) {
        EXIT_SYSCALL(state);
        return ret;
    }

    if (g_is_oled || !g_lcd_hooks_active) {
        state_lock_release();
        EXIT_SYSCALL(state);
        return -1;
    }

    uint8_t snapshot[LCD_LUT_LEVELS];
    lut_copy(snapshot, lcd_brightness_values);
    state_lock_release();
    ret = ksceKernelMemcpyKernelToUser((void *)out, snapshot, sizeof(snapshot));
    EXIT_SYSCALL(state);
    return ret;
}

int vitabrightLcdSetBrightnessValues(uint8_t in[LCD_LUT_LEVELS]) {
    int state;
    uint8_t candidate[LCD_LUT_LEVELS];
    ENTER_SYSCALL(state);

    int ret = ksceKernelMemcpyUserToKernel(candidate, (const void *)in, sizeof(candidate));
    if (ret < 0 || !lut_is_valid(candidate)) {
        int detail = ret < 0 ? ret : -1;
        if (state_lock_acquire() >= 0) {
            status_set_error(VBE_ERR_INVALID_USER_INPUT, detail);
            state_lock_release();
        }
        EXIT_SYSCALL(state);
        return detail;
    }

    ret = state_lock_acquire();
    if (ret < 0) {
        EXIT_SYSCALL(state);
        return ret;
    }
    if (g_is_oled || !g_lcd_hooks_active) {
        state_lock_release();
        EXIT_SYSCALL(state);
        return -1;
    }

    ret = lcd_replace_candidate(candidate);
    if (ret == 0) status_clear_error();
    state_lock_release();
    EXIT_SYSCALL(state);
    return ret;
}

/* Retained for ABI compatibility with the older companion editor. Colour
 * space is now backend-neutral and transactional in color_space.c. */
int vitabrightLcdReapplyColor(void) {
    int state;
    ENTER_SYSCALL(state);
    int ret = state_lock_acquire();
    if (ret < 0) {
        EXIT_SYSCALL(state);
        return ret;
    }
    if (g_is_oled) {
        state_lock_release();
        EXIT_SYSCALL(state);
        return -1;
    }

    ret = color_space_apply_config();
    state_lock_release();
    EXIT_SYSCALL(state);
    return ret;
}
