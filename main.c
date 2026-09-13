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

static int selected_backend_mutation_safe(void) {
    return g_is_oled ? oled_backend_mutation_safe()
                     : lcd_backend_mutation_safe();
}

static void reconcile_after_brightness(int *result) {
    if (selected_backend_mutation_safe()) {
        int color_ret = color_space_apply_config();
        *result = vbe_result_compose(*result, color_ret);
        if (color_ret < 0)
            LOG("[CORE] color-space reconcile failed: 0x%08X\n", color_ret);
    } else {
        LOG("[CORE] brightness ownership degraded; skipping further hardware mutation\n");
    }

    /* Generic filter v1.4 reconciliation is request-state only: all mutable
     * hardware domains are capability-gated unsupported. It remains safe to
     * record the accepted request even when brightness ownership is degraded. */
    int filter_ret = screen_filter_apply_config();
    *result = vbe_result_compose(*result, filter_ret);
    if (filter_ret == VBE_RESULT_UNSUPPORTED)
        LOG("[CORE] accepted config requests unsupported generic filter domain\n");
    else if (filter_ret < 0)
        LOG("[CORE] filter request-state reconcile failed: 0x%08X\n", filter_ret);
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

    g_module_lifecycle = VBE_MODULE_RUNTIME;

    VbeConfigCandidate config_candidate;
    int config_ret = config_load_candidate(&config_candidate);
    if (config_ret >= 0)
        config_commit_request(&config_candidate);
    else
        LOG("[CORE] config candidate rejected; retained compiled-safe accepted request: 0x%08X\n",
            config_ret);

    int brightness_ret = is_lcd ? lcd_enable_hooks() : oled_enable_hooks();
    if (brightness_ret < 0)
        LOG("[CORE] selected brightness backend unavailable: 0x%08X\n",
            brightness_ret);

    int result = vbe_result_compose(config_ret, brightness_ret);
    reconcile_after_brightness(&result);
    (void)result;
    return SCE_KERNEL_START_SUCCESS;
}

int vitabright_reload_locked(void) {
    VbeConfigCandidate candidate;
    int config_ret = config_load_candidate(&candidate);
    if (config_ret < 0)
        return config_ret;

    /* A valid document is accepted request/provenance. Hardware domains then
     * reconcile independently; one failed-clean domain never reverts it. */
    config_commit_request(&candidate);

    int result = VBE_RESULT_OK;
    int brightness_ret = g_is_oled ? oled_reload_backend() : lcd_reload_backend();
    result = vbe_result_compose(result, brightness_ret);
    reconcile_after_brightness(&result);
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
