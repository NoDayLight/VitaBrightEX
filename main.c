#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysroot.h>
#include <taihen.h>

#include "color_space.h"
#include "config.h"
#include "lcd/hooks.h"
#include "log.h"
#include "main.h"
#include "module_lifecycle_core.h"
#include "oled/hooks.h"
#include "result_core.h"
#include "screen_filter.h"
#include "state_lock.h"
#include "status.h"
#include "transaction_core.h"

unsigned int sw_version = 0;
int g_is_oled = 0;

static int g_module_lifecycle = VBE_MODULE_INERT;

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

    if (state_lock_init() < 0)
        return SCE_KERNEL_START_SUCCESS;

    /* Successful synchronization creation is the runtime-init commit point.
     * Everything below may acquire session/backend ownership. */
    g_module_lifecycle = VBE_MODULE_RUNTIME;

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

    int filter_ret = screen_filter_apply_config();
    if (filter_ret == VBE_RESULT_UNSUPPORTED)
        LOG("[CORE] config requests an unsupported filter domain\n");
    else if (filter_ret < 0)
        LOG("[CORE] filter runtime failure: 0x%08X\n", filter_ret);

    return SCE_KERNEL_START_SUCCESS;
}

int vitabright_reload_locked(void) {
    int result = VBE_RESULT_OK;
    VbeConfigSnapshot previous_config;
    config_snapshot(&previous_config);

    int config_ret = config_load();
    result = vbe_result_compose(result, config_ret);

    int brightness_ret = g_is_oled ? oled_reload_backend() : lcd_reload_backend();
    result = vbe_result_compose(result, brightness_ret);

    /* The config/source candidate is not externally observable during this
     * locked transition. A failed backend replacement restores the previous
     * committed config and exact FILE/COMPILED identity before returning. */
    if (config_ret >= 0 && brightness_ret < 0)
        config_restore(&previous_config);

    int color_ret = color_space_apply_config();
    result = vbe_result_compose(result, color_ret);

    int filter_ret = screen_filter_apply_config();
    result = vbe_result_compose(result, filter_ret);

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

    int stop_mode = vbe_module_stop_mode(g_module_lifecycle,
                                         state_lock_lifecycle());
    if (stop_mode == VBE_MODULE_STOP_INERT)
        return SCE_KERNEL_STOP_SUCCESS;
    if (stop_mode != VBE_MODULE_STOP_RUNTIME)
        return SCE_KERNEL_STOP_FAIL;

    if (state_lock_begin_shutdown() < 0)
        return SCE_KERNEL_STOP_FAIL;

    VbeStopAccumulator stop;
    vbe_stop_init(&stop);

    vbe_stop_stage(&stop, screen_filter_reset(g_is_oled));
    vbe_stop_stage(&stop, color_space_shutdown());
    vbe_stop_stage(&stop, g_is_oled ? oled_disable_hooks() : lcd_disable_hooks());

    if (!vbe_stop_can_unload(&stop)) {
        if (state_lock_cancel_shutdown() < 0)
            return SCE_KERNEL_STOP_FAIL;
        return SCE_KERNEL_STOP_FAIL;
    }

    if (state_lock_finish_shutdown() < 0)
        return SCE_KERNEL_STOP_FAIL;

    g_module_lifecycle = VBE_MODULE_INERT;
    return SCE_KERNEL_STOP_SUCCESS;
}
