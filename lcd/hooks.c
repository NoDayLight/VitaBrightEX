#include "hooks.h"
#include "lcd_lut.h"
#include "../color_space.h"
#include "../config.h"
#include "../log.h"
#include "../lut_parser_core.h"
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
#define LCD_SOURCE_PATH_MAX      128
#define LCD_READ_CHUNK           256

static const uint8_t lcd_brightness_default[LCD_LUT_LEVELS] = {
    1, 3, 5, 8, 13, 20, 29, 41, 57, 76, 95, 116, 137, 161, 190, 220, 255
};

static const uint8_t lcd_stock_signature[LCD_LUT_LEVELS] = {
    31, 37, 43, 50, 58, 67, 77, 88, 100, 114, 129, 147, 166, 182, 203, 227, 255
};

static uint8_t lcd_brightness_values[LCD_LUT_LEVELS];
static char lcd_source_path[LCD_SOURCE_PATH_MAX];
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

static void path_copy(char dst[LCD_SOURCE_PATH_MAX], const char *src) {
    int i = 0;
    while (i < LCD_SOURCE_PATH_MAX - 1 && src[i]) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

static int lcd_brightness_to_index(unsigned int brightness) {
    if (brightness <= 2u) return 0;
    if (brightness >= 0x10000u) return LCD_LUT_LEVELS - 1;
    return (int)(16u * (brightness - 2u) / 65534u);
}

static int lcd_parse_lut_file(const char *path,
                              uint8_t out[LCD_LUT_LEVELS],
                              int *opened) {
    SceUID fd = ksceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) {
        *opened = 0;
        return fd;
    }
    *opened = 1;

    VbeLcdLutParser parser;
    vbe_lcd_lut_parser_init(&parser, out);
    uint8_t buffer[LCD_READ_CHUNK];
    int ret = 0;

    while (1) {
        int r = ksceIoRead(fd, buffer, sizeof(buffer));
        if (r < 0) {
            ret = r;
            break;
        }
        if (r == 0) {
            ret = vbe_lcd_lut_parser_finish(&parser);
            break;
        }
        for (int i = 0; i < r; ++i) {
            if (vbe_lcd_lut_parser_feed(&parser, buffer[i]) < 0) {
                ret = -1;
                break;
            }
        }
        if (ret < 0) break;
    }

    int close_ret = ksceIoClose(fd);
    if (ret == 0 && close_ret < 0) ret = close_ret;
    return ret;
}

static int lcd_load_disk_candidate(uint8_t out[LCD_LUT_LEVELS],
                                   char source[LCD_SOURCE_PATH_MAX]) {
    int opened = 0;
    int ret = lcd_parse_lut_file(LCD_LUT_FILE1, out, &opened);
    if (opened) {
        if (ret == 0) path_copy(source, LCD_LUT_FILE1);
        return ret;
    }

    ret = lcd_parse_lut_file(LCD_LUT_FILE2, out, &opened);
    if (opened) {
        if (ret == 0) path_copy(source, LCD_LUT_FILE2);
        return ret;
    }

    lut_copy(out, lcd_brightness_default);
    path_copy(source, LCD_LUT_FILE1);
    return 0;
}

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
    uint8_t table_value = lcd_brightness_values[old_index];
    LOG("[LCD:DIM] req=1 old_raw=%u index=%d table=%u allow=%d\n",
        old_brightness, old_index, (unsigned)table_value,
        old_brightness >= 2u && table_value >= LCD_DIMMED_VALUE);

    if (old_brightness >= 2u && table_value >= LCD_DIMMED_VALUE)
        return TAI_CONTINUE(int, lcd_set_brightness_ref, brightness);
    return TAI_CONTINUE(int, lcd_set_brightness_ref, old_brightness);
}

int hook_kscePowerSetDisplayMaxBrightnessForLcd(int limit) {
    LOG("[LCD:POWER] max brightness request=%d forced=65536\n", limit);
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
    if (!vbe_lcd_lut_values_valid(candidate)) return -1;

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
    if (!vbe_lcd_lut_values_valid(candidate)) return -1;

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
    char source[LCD_SOURCE_PATH_MAX];
    int ret = lcd_load_disk_candidate(candidate, source);
    if (ret < 0) {
        status_set_error(VBE_ERR_INVALID_USER_INPUT, ret);
        return ret;
    }

    ret = lcd_start_transaction(candidate);
    if (ret == 0) path_copy(lcd_source_path, source);
    return ret;
}

void lcd_disable_hooks(void) {
    lcd_release_transaction();
    ksceLcdGetBrightness = NULL;
    ksceLcdSetBrightness = NULL;
}

int lcd_reload_backend(void) {
    VitaBrightConfig previous_config = g_config;
    uint8_t candidate[LCD_LUT_LEVELS];
    char candidate_source[LCD_SOURCE_PATH_MAX];

    int ret = config_load();
    if (ret < 0) {
        g_config = previous_config;
        status_set_error(VBE_ERR_CONFIG, ret);
        return ret;
    }

    ret = lcd_load_disk_candidate(candidate, candidate_source);
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

    path_copy(lcd_source_path, candidate_source);
    return 0;
}

static int build_temp_path(char out[LCD_SOURCE_PATH_MAX], const char *path) {
    int i = 0;
    while (i < LCD_SOURCE_PATH_MAX - 5 && path[i]) {
        out[i] = path[i];
        ++i;
    }
    if (path[i] != '\0') return -1;
    out[i++] = '.';
    out[i++] = 't';
    out[i++] = 'm';
    out[i++] = 'p';
    out[i] = '\0';
    return 0;
}

static int decimal_line(uint8_t value, char out[4]) {
    int pos = 0;
    if (value >= 100) {
        out[pos++] = (char)('0' + value / 100);
        value = (uint8_t)(value % 100);
        out[pos++] = (char)('0' + value / 10);
        out[pos++] = (char)('0' + value % 10);
    } else if (value >= 10) {
        out[pos++] = (char)('0' + value / 10);
        out[pos++] = (char)('0' + value % 10);
    } else {
        out[pos++] = (char)('0' + value);
    }
    out[pos++] = '\n';
    return pos;
}

static int persist_lcd_locked(void) {
    if (!g_lcd_hooks_active || lcd_source_path[0] == '\0') return -1;

    char temp_path[LCD_SOURCE_PATH_MAX];
    if (build_temp_path(temp_path, lcd_source_path) < 0) return -1;

    (void)ksceIoRemove(temp_path);
    SceUID fd = ksceIoOpen(temp_path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) return fd;

    int ret = 0;
    for (int i = 0; i < LCD_LUT_LEVELS; ++i) {
        char line[4];
        int len = decimal_line(lcd_brightness_values[i], line);
        int written = ksceIoWrite(fd, line, (SceSize)len);
        if (written != len) {
            ret = written < 0 ? written : -1;
            break;
        }
    }

    if (ret == 0) {
        int sync_status = 0;
        int sync_ret = ksceIoSyncByFd(fd, &sync_status);
        if (sync_ret < 0 || sync_status < 0)
            ret = sync_ret < 0 ? sync_ret : sync_status;
    }

    int close_ret = ksceIoClose(fd);
    if (ret == 0 && close_ret < 0) ret = close_ret;

    if (ret == 0) ret = ksceIoRename(temp_path, lcd_source_path);
    if (ret < 0) (void)ksceIoRemove(temp_path);
    return ret;
}

int vitabrightLcdPersistBrightnessValues(void) {
    int state;
    ENTER_SYSCALL(state);
    int ret = state_lock_acquire();
    if (ret < 0) { EXIT_SYSCALL(state); return ret; }

    if (g_is_oled || !g_lcd_hooks_active) {
        state_lock_release();
        EXIT_SYSCALL(state);
        return -1;
    }

    ret = persist_lcd_locked();
    if (ret < 0) status_set_error(VBE_ERR_BACKEND, ret);
    else status_clear_error();

    state_lock_release();
    EXIT_SYSCALL(state);
    return ret;
}

int vitabrightLcdGetBrightnessValues(uint8_t out[LCD_LUT_LEVELS]) {
    int state;
    ENTER_SYSCALL(state);
    int ret = state_lock_acquire();
    if (ret < 0) { EXIT_SYSCALL(state); return ret; }

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
    if (ret < 0 || !vbe_lcd_lut_values_valid(candidate)) {
        int detail = ret < 0 ? ret : -1;
        if (state_lock_acquire() >= 0) {
            status_set_error(VBE_ERR_INVALID_USER_INPUT, detail);
            state_lock_release();
        }
        EXIT_SYSCALL(state);
        return detail;
    }

    ret = state_lock_acquire();
    if (ret < 0) { EXIT_SYSCALL(state); return ret; }
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

int vitabrightLcdReapplyColor(void) {
    int state;
    ENTER_SYSCALL(state);
    int ret = state_lock_acquire();
    if (ret < 0) { EXIT_SYSCALL(state); return ret; }
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
