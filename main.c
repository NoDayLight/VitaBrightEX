#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysroot.h>
#include <taihen.h>

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

    int lock_ret = state_lock_init();
    if (lock_ret < 0) {
        g_vbe_status.state_lock = VBE_CAP_FAILED;
        status_set_error(VBE_ERR_SYNCHRONIZATION, lock_ret);
        /* No mutable backend is started without serialization support. */
        return SCE_KERNEL_START_SUCCESS;
    }
    g_vbe_status.state_lock = VBE_CAP_ACTIVE;

    config_load();

    int ret = is_lcd ? lcd_enable_hooks() : oled_enable_hooks();
    if (ret < 0)
        LOG("[CORE] selected brightness backend unavailable: 0x%08X\n", ret);

    /* Optional display capabilities are not part of the boot-success contract. */
    screen_filter_load_config();
    (void)screen_filter_apply(g_is_oled);

    return SCE_KERNEL_START_SUCCESS;
}

int vitabrightReload(void) {
    int state;
    ENTER_SYSCALL(state);
    int ret = state_lock_acquire();
    if (ret < 0) { EXIT_SYSCALL(state); return ret; }

    if (g_is_oled) {
        /* OLED reload is fail-open.  A bad replacement may disable the
         * enhancement, but cannot initialize the LCD backend or block boot. */
        oled_disable_hooks();
        config_load();
        ret = oled_enable_hooks();
    } else {
        ret = lcd_reload_backend();
    }

    screen_filter_load_config();
    (void)screen_filter_apply(g_is_oled);

    state_lock_release();
    EXIT_SYSCALL(state);
    return ret;
}

int module_stop(SceSize argc, const void *args) {
    (void)argc;
    (void)args;

    int locked = state_lock_acquire() >= 0;
    screen_filter_reset(g_is_oled);
    if (g_is_oled) oled_disable_hooks();
    else lcd_disable_hooks();
    if (locked) state_lock_release();
    state_lock_destroy();
    return SCE_KERNEL_STOP_SUCCESS;
}
