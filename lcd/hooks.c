#include "hooks.h"
#include "lcd_lut.h"
#include "../main.h"
#include "../log.h"
#include "../config.h"
#include "../status.h"
#include "../taihen_extra.h"
#include <stdint.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/io/fcntl.h>
#include <taihen.h>

#define NID_LCD_GET_BRIGHTNESS      0x3A6D6AC3
#define NID_LCD_SET_BRIGHTNESS      0x581D3A87
#define NID_LCD_SET_COLOR_SPACE     0xD40968FB
#define NID_POWER_SET_MAX_BRIGHT    0x77027B6B

static const uint8_t lcd_brightness_default[LCD_LUT_LEVELS] = {
    1, 3, 5, 8, 13, 20, 29, 41, 57, 76, 95, 116, 137, 161, 190, 220, 255
};

static uint8_t lcd_brightness_values[LCD_LUT_LEVELS];

static SceUID lcd_table_inject = -1;
static SceUID lcd_set_brightness_hook = -1;
static SceUID power_set_max_bright_hook = -1;
static tai_hook_ref_t lcd_set_brightness_ref = -1;
static tai_hook_ref_t power_set_max_bright_ref = 0;

static int (*ksceLcdGetBrightness)(void) = NULL;
static int (*ksceLcdSetBrightness)(unsigned int brightness) = NULL;
static int (*ksceLcdSetDisplayColorSpaceMode)(int mode) = NULL;

static int g_lcd_hooks_active = 0;

static int lut_is_valid(const uint8_t values[LCD_LUT_LEVELS]) {
    for (int i = 1; i < LCD_LUT_LEVELS; i++) {
        if (values[i] < values[i - 1]) return 0;
    }
    return 1;
}

static int lcd_brightness_to_index(unsigned int brightness) {
    if (brightness == 0) return 16;
    int best_idx = 0;
    unsigned int best_dist = 0xFFFFFFFFu;
    for (int i = 0; i < LCD_LUT_LEVELS; i++) {
        unsigned int expected = ((unsigned int)lcd_brightness_values[i] * 65535u) / 255u;
        unsigned int dist = brightness > expected ? brightness - expected : expected - brightness;
        if (dist < best_dist) {
            best_dist = dist;
            best_idx = i;
        }
    }
    return best_idx;
}

static int lcd_parse_lut_file(const char *path, uint8_t out[LCD_LUT_LEVELS]) {
    SceUID fd = ksceIoOpen(path, SCE_O_RDONLY, 6);
    if (fd < 0) return fd;

    int count = 0;
    char line[16];
    int li = 0;
    while (count < LCD_LUT_LEVELS) {
        char c;
        int r = ksceIoRead(fd, &c, 1);
        if (r <= 0) break;
        if (c == '\r') continue;
        if (c == '\n' || li == (int)sizeof(line) - 1) {
            line[li] = '\0';
            li = 0;
            if (line[0] == '#' || line[0] == '\0') continue;
            int val = 0;
            int i = 0;
            for (; line[i] >= '0' && line[i] <= '9'; i++) val = val * 10 + (line[i] - '0');
            if (val > 255) val = 255;
            out[count++] = (uint8_t)val;
        } else {
            line[li++] = c;
        }
    }
    ksceIoClose(fd);
    if (count != LCD_LUT_LEVELS || !lut_is_valid(out)) return -1;
    return 0;
}

static void lcd_load_lut_from_disk(void) {
    uint8_t candidate[LCD_LUT_LEVELS];
    for (int i = 0; i < LCD_LUT_LEVELS; i++) candidate[i] = lcd_brightness_default[i];

    int ret = lcd_parse_lut_file(LCD_LUT_FILE1, candidate);
    if (ret < 0) ret = lcd_parse_lut_file(LCD_LUT_FILE2, candidate);
    if (ret < 0) {
        for (int i = 0; i < LCD_LUT_LEVELS; i++) candidate[i] = lcd_brightness_default[i];
    }
    for (int i = 0; i < LCD_LUT_LEVELS; i++) lcd_brightness_values[i] = candidate[i];
}

static int lcd_get_table_offset(uint32_t firmware, uint32_t *out) {
    switch (firmware >> 16) {
    case 0x360:
        *out = 0x1B00;
        return 0;
    case 0x365: case 0x367: case 0x368: case 0x369: case 0x370:
    case 0x371: case 0x372: case 0x373: case 0x374:
        *out = 0x1B48;
        return 0;
    default:
        return -1;
    }
}

int hook_ksceLcdSetBrightness(unsigned int brightness) {
    if (brightness != 1 || ksceLcdGetBrightness == NULL)
        return TAI_CONTINUE(int, lcd_set_brightness_ref, brightness);

    unsigned int old_brightness = (unsigned int)ksceLcdGetBrightness();
    int old_index = lcd_brightness_to_index(old_brightness);
    if (old_index > 4)
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
        taiHookReleaseForKernel(power_set_max_bright_hook, power_set_max_bright_ref);
        power_set_max_bright_hook = -1;
        power_set_max_bright_ref = 0;
    }
    if (lcd_set_brightness_hook >= 0) {
        taiHookReleaseForKernel(lcd_set_brightness_hook, lcd_set_brightness_ref);
        lcd_set_brightness_hook = -1;
        lcd_set_brightness_ref = -1;
    }
    if (lcd_table_inject >= 0) {
        taiInjectReleaseForKernel(lcd_table_inject);
        lcd_table_inject = -1;
    }

    g_lcd_hooks_active = 0;
    g_vbe_status.brightness_core = VBE_CAP_INACTIVE;
    g_vbe_status.brightness_table = VBE_CAP_INACTIVE;
    g_vbe_status.brightness_hook = VBE_CAP_INACTIVE;
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

static int lcd_start_transaction(void) {
    if (g_lcd_hooks_active) return 0;

    tai_module_info_t info;
    int ret = lcd_resolve_core(&info);
    if (ret < 0) {
        status_set_error(VBE_ERR_EXPORT_RESOLUTION, ret);
        g_vbe_status.brightness_core = VBE_CAP_FAILED;
        return ret;
    }

    uint32_t table_off;
    if (lcd_get_table_offset(sw_version, &table_off) < 0) {
        status_set_error(VBE_ERR_FIRMWARE_UNSUPPORTED, (int)sw_version);
        g_vbe_status.brightness_table = VBE_CAP_UNSUPPORTED;
        return -1;
    }

    lcd_table_inject = taiInjectDataForKernel(KERNEL_PID, info.modid, 0,
        table_off, lcd_brightness_values, sizeof(lcd_brightness_values));
    if (lcd_table_inject < 0) {
        status_set_error(VBE_ERR_TABLE_INJECTION, lcd_table_inject);
        g_vbe_status.brightness_table = VBE_CAP_FAILED;
        lcd_release_transaction();
        return lcd_table_inject;
    }
    g_vbe_status.brightness_table = VBE_CAP_ACTIVE;

    lcd_set_brightness_hook = taiHookFunctionExportForKernel(KERNEL_PID,
        &lcd_set_brightness_ref, "SceLcd", TAI_ANY_LIBRARY,
        NID_LCD_SET_BRIGHTNESS, hook_ksceLcdSetBrightness);
    if (lcd_set_brightness_hook < 0) {
        status_set_error(VBE_ERR_BRIGHTNESS_HOOK, lcd_set_brightness_hook);
        g_vbe_status.brightness_hook = VBE_CAP_FAILED;
        lcd_release_transaction();
        return lcd_set_brightness_hook;
    }
    g_vbe_status.brightness_hook = VBE_CAP_ACTIVE;

    power_set_max_bright_hook = taiHookFunctionExportForKernel(KERNEL_PID,
        &power_set_max_bright_ref, "ScePower", TAI_ANY_LIBRARY,
        NID_POWER_SET_MAX_BRIGHT, hook_kscePowerSetDisplayMaxBrightnessForLcd);
    if (power_set_max_bright_hook < 0) {
        status_set_error(VBE_ERR_POWER_HOOK, power_set_max_bright_hook);
        g_vbe_status.power_limit_hook = VBE_CAP_FAILED;
        lcd_release_transaction();
        return power_set_max_bright_hook;
    }
    g_vbe_status.power_limit_hook = VBE_CAP_ACTIVE;

    ret = ksceLcdSetBrightness((unsigned int)ksceLcdGetBrightness());
    if (ret < 0) {
        status_set_error(VBE_ERR_BACKEND, ret);
        lcd_release_transaction();
        return ret;
    }

    g_lcd_hooks_active = 1;
    g_vbe_status.brightness_core = VBE_CAP_ACTIVE;
    return 0;
}

static void lcd_probe_optional_color(void) {
    ksceLcdSetDisplayColorSpaceMode = NULL;
    int ret = module_get_export_func(KERNEL_PID, "SceLcd", TAI_ANY_LIBRARY,
        NID_LCD_SET_COLOR_SPACE, (uintptr_t *)&ksceLcdSetDisplayColorSpaceMode);
    if (ret < 0 || ksceLcdSetDisplayColorSpaceMode == NULL) {
        g_vbe_status.lcd_color_space = VBE_CAP_UNAVAILABLE;
        return;
    }
    g_vbe_status.lcd_color_space = VBE_CAP_INACTIVE;

    if (g_config.lcd_color_space_mode || g_config.lcd_ips_enhance || g_config.lcd_saturation_boost) {
        ret = ksceLcdSetDisplayColorSpaceMode(g_config.lcd_color_space_mode ? 1 : 0);
        if (ret < 0) {
            g_vbe_status.lcd_color_space = VBE_CAP_FAILED;
            status_set_error(VBE_ERR_DISPLAY_CAPABILITY, ret);
        } else {
            g_vbe_status.lcd_color_space = VBE_CAP_ACTIVE;
        }
    }

    /* No verified session-scoped RGB-range API: do not write persistent registry keys. */
    g_vbe_status.registry_api = VBE_CAP_UNSUPPORTED;
}

int lcd_enable_hooks(void) {
    if (g_lcd_hooks_active) return 0;
    lcd_load_lut_from_disk();
    int ret = lcd_start_transaction();
    if (ret == 0) lcd_probe_optional_color();
    return ret;
}

void lcd_disable_hooks(void) {
    lcd_release_transaction();
    ksceLcdGetBrightness = NULL;
    ksceLcdSetBrightness = NULL;
    ksceLcdSetDisplayColorSpaceMode = NULL;
}

int vitabrightLcdGetBrightnessValues(uint8_t out[LCD_LUT_LEVELS]) {
    int state;
    int ret;
    ENTER_SYSCALL(state);
    ret = ksceKernelMemcpyKernelToUser((void *)out, lcd_brightness_values,
                                       sizeof(lcd_brightness_values));
    EXIT_SYSCALL(state);
    return ret;
}

int vitabrightLcdSetBrightnessValues(uint8_t in[LCD_LUT_LEVELS]) {
    int state;
    int ret;
    uint8_t candidate[LCD_LUT_LEVELS];

    ENTER_SYSCALL(state);
    ret = ksceKernelMemcpyUserToKernel(candidate, (const void *)in, sizeof(candidate));
    if (ret < 0 || !lut_is_valid(candidate)) {
        status_set_error(VBE_ERR_INVALID_USER_INPUT, ret < 0 ? ret : -1);
        EXIT_SYSCALL(state);
        return ret < 0 ? ret : -1;
    }

    lcd_release_transaction();
    for (int i = 0; i < LCD_LUT_LEVELS; i++) lcd_brightness_values[i] = candidate[i];
    ret = lcd_start_transaction(); /* reuses RAM candidate; no disk reload */
    EXIT_SYSCALL(state);
    return ret;
}

int vitabrightLcdReapplyColor(void) {
    int state;
    ENTER_SYSCALL(state);
    lcd_probe_optional_color();
    EXIT_SYSCALL(state);
    return g_vbe_status.lcd_color_space == VBE_CAP_FAILED ? -1 : 0;
}
