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
#include "transaction_core.h"

unsigned int sw_version = 0;
int g_is_oled = 0;

static int detect_is_lcd(void) {
    return (*(uint8_t *)(ksceKernelSysrootGetKblParam() + 0xE8) & 9) != 0;
}

static void keep_first_result(int *result, int stage_result) {
    if (*result == VBE_RESULT_OK && stage_result != VBE_RESULT_OK)
        *result = stage_result;
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
        return SCE_KERNEL_START_SUCCESS;
    }
    g_vbe_status.state_lock = VBE_CAP_ACTIVE;
    status_clear_error_domain(VBE_ERROR_DOMAIN_SYNC);

    int config_ret = config_load();
    if (config_ret < 0)
        LOG("[CORE] authoritative config rejected: 0x%08X\n", config_ret);

    int brightness_ret = is_lcd ? lcd_enable_hooks() : oled_enable_hooks();
    if (brightness_ret < 0)
        LOG("[CORE] selected brightness backend unavailable: 0x%08X\n",
            brightness_ret);

    int color_ret = color_space_apply_config();
    if (color_ret < 0)
        LOG("[CORE] color-space capability unavailable: 0x%08X\n", color_ret);

    screen_filter_load_config();
    int filter_ret = screen_filter_apply(g_is_oled);
    if (filter_ret == VBE_RESULT_UNSUPPORTED)
        LOG("[CORE] config requests an unsupported filter capability\n");
    else if (filter_ret < 0)
        LOG("[CORE] filter runtime failure: 0x%08X\n", filter_ret);

    return SCE_KERNEL_START_SUCCESS;
}

int vitabright_reload_locked(void) {
    int result = VBE_RESULT_OK;

    int config_ret = config_load();
    keep_first_result(&result, config_ret);

    int brightness_ret = g_is_oled ? oled_reload_backend() : lcd_reload_backend();
    keep_first_result(&result, brightness_ret);

    int color_ret = color_space_apply_config();
    keep_first_result(&result, color_ret);

    screen_filter_load_config();
    int filter_ret = screen_filter_apply(g_is_oled);
    keep_first_result(&result, filter_ret);

    return result;
}

int vitabrightReload(void) {
    int state;
    ENTER_SYSCALL(state);
    int ret = state_lock_acquire();
    if (ret < 0) {
        EXIT_SYSCALL(state);
        return ret;
    }

    int result = vitabright_reload_locked();
    result = state_lock_release_result(result);
    EXIT_SYSCALL(state);
    return result;
}

int module_stop(SceSize argc, const void *args) {
    (void)argc;
    (void)args;

    if (state_lock_begin_shutdown() < 0)
        return SCE_KERNEL_STOP_FAIL;

    VbeStopAccumulator stop;
    vbe_stop_init(&stop);

    vbe_stop_stage(&stop, screen_filter_reset(g_is_oled));
    vbe_stop_stage(&stop, color_space_shutdown());
    vbe_stop_stage(&stop, g_is_oled ? oled_disable_hooks() : lcd_disable_hooks());

    if (!vbe_stop_can_unload(&stop)) {
        if (state_lock_cancel_shutdown() < 0)
            g_vbe_status.state_lock = VBE_CAP_FAILED;
        return SCE_KERNEL_STOP_FAIL;
    }

    if (state_lock_finish_shutdown() < 0) {
        g_vbe_status.state_lock = VBE_CAP_FAILED;
        return SCE_KERNEL_STOP_FAIL;
    }

    g_vbe_status.state_lock = VBE_CAP_INACTIVE;
    status_clear_error_domain(VBE_ERROR_DOMAIN_SYNC);
    return SCE_KERNEL_STOP_SUCCESS;
}
