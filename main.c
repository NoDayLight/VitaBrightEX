#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysroot.h>
#include <taihen.h>

#include "color_space.h"
#include "config.h"
#include "lcd/hooks.h"
#include "log.h"
#include "main.h"
#include "oled/hooks.h"
#include "screen_filter.h"
#include "state_lock.h"
#include "status.h"

unsigned int sw_version = 0;
int g_is_oled = 0;

static int detect_is_lcd(void) {
    return (*(uint8_t *)(ksceKernelSysrootGetKblParam() + 0xE8) & 9) != 0;
}

void _start() __attribute__((weak, alias("module_start")));
int module_start(SceSize argc, const void *args) {
    (void)argc;
    (void)args;

    sw_version = ksceKernelSysrootGetSystemSwVersion();
    int is_lcd = detect_is_lcd();
    g_is_oled = !is_lcd;
    status_init(is_lcd ? VBE_HW_LCD : VBE_HW_OLED, sw_version);
    config_reset_defaults();

    int lock_ret = state_lock_init();
    if (lock_ret < 0) {
        g_vbe_status.state_lock = VBE_CAP_FAILED;
        status_set_error_domain(VBE_ERROR_DOMAIN_SYNC,
                                VBE_ERR_SYNCHRONIZATION, lock_ret);
        return SCE_KERNEL_START_SUCCESS;
    }
    g_vbe_status.state_lock = VBE_CAP_ACTIVE;

    int config_ret = config_load();
    if (config_ret < 0) {
        status_set_error_domain(VBE_ERROR_DOMAIN_CONFIG, VBE_ERR_CONFIG,
                                config_ret);
        LOG("[CORE] authoritative config rejected: 0x%08X\n", config_ret);
    } else {
        status_clear_error_domain(VBE_ERROR_DOMAIN_CONFIG);
    }

    int ret = is_lcd ? lcd_enable_hooks() : oled_enable_hooks();
    if (ret < 0) {
        LOG("[CORE] selected brightness backend unavailable: 0x%08X\n", ret);
    } else {
        status_clear_error_domain(VBE_ERROR_DOMAIN_BRIGHTNESS);
    }

    int color_ret = color_space_apply_config();
    if (color_ret < 0)
        LOG("[CORE] color-space capability unavailable: 0x%08X\n", color_ret);

    screen_filter_load_config();
    int filter_ret = screen_filter_apply(g_is_oled);
    if (filter_ret < 0)
        LOG("[CORE] optional filter capability unavailable: 0x%08X\n", filter_ret);

    return SCE_KERNEL_START_SUCCESS;
}

int vitabrightReload(void) {
    int state;
    ENTER_SYSCALL(state);
    int ret = state_lock_acquire();
    if (ret < 0) {
        EXIT_SYSCALL(state);
        return ret;
    }

    int result = g_is_oled ? oled_reload_backend() : lcd_reload_backend();
    if (result >= 0) {
        status_clear_error_domain(VBE_ERROR_DOMAIN_CONFIG);
        status_clear_error_domain(VBE_ERROR_DOMAIN_BRIGHTNESS);
    }

    int color_ret = color_space_apply_config();
    if (result >= 0 && color_ret < 0) result = color_ret;

    screen_filter_load_config();
    int filter_ret = screen_filter_apply(g_is_oled);
    if (result >= 0 && filter_ret < 0) result = filter_ret;

    state_lock_release();
    EXIT_SYSCALL(state);
    return result;
}

int module_stop(SceSize argc, const void *args) {
    (void)argc;
    (void)args;

    int locked = state_lock_acquire() >= 0;
    screen_filter_reset(g_is_oled);
    color_space_shutdown();
    if (g_is_oled) oled_disable_hooks();
    else lcd_disable_hooks();
    if (locked) state_lock_release();
    state_lock_destroy();
    return SCE_KERNEL_STOP_SUCCESS;
}
