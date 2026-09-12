#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysroot.h>
#include <taihen.h>

#include "main.h"
#include "config.h"
#include "lcd/hooks.h"
#include "log.h"
#include "oled/hooks.h"
#include "screen_filter.h"
#include "status.h"

unsigned int sw_version = 0;
int g_is_oled = 0;

static int detect_is_lcd(void) {
    /* Boot type indicator 1: bit 0 + bit 3 identifies LCD hardware. */
    return (*(uint8_t *)(ksceKernelSysrootGetKblParam() + 0xE8) & 9) != 0;
}

void _start() __attribute__((weak, alias("module_start")));
int module_start(SceSize argc, const void *args) {
    (void)argc;
    (void)args;

    sw_version = ksceKernelSysrootGetSystemSwVersion();
    config_load();

    int is_lcd = detect_is_lcd();
    g_is_oled = !is_lcd;
    status_init(is_lcd ? VBE_HW_LCD : VBE_HW_OLED, sw_version);

    /* Exactly one brightness backend participates in startup. */
    if (is_lcd) {
        int ret = lcd_enable_hooks();
        if (ret < 0) {
            /* Fail open: retain the error in status and still finish boot. */
            LOG("[CORE] LCD brightness backend unavailable: 0x%08X\n", ret);
        }
    } else {
        oled_enable_hooks();
        /* OLED backend predates status-aware return codes; mark selected core. */
        g_vbe_status.brightness_core = VBE_CAP_ACTIVE;
    }

    /* Optional display capabilities are never boot-critical. */
    screen_filter_load_config();
    (void)screen_filter_apply(g_is_oled);

    return SCE_KERNEL_START_SUCCESS;
}

int vitabrightReload(void) {
    int state;
    int ret = 0;
    ENTER_SYSCALL(state);

    /* Stop only the backend that exists on this device. */
    if (g_is_oled) oled_disable_hooks();
    else lcd_disable_hooks();

    config_load();

    if (g_is_oled) {
        oled_enable_hooks();
        g_vbe_status.brightness_core = VBE_CAP_ACTIVE;
    } else {
        ret = lcd_enable_hooks();
    }

    screen_filter_load_config();
    (void)screen_filter_apply(g_is_oled);

    EXIT_SYSCALL(state);
    return ret;
}

int module_stop(SceSize argc, const void *args) {
    (void)argc;
    (void)args;

    screen_filter_reset(g_is_oled);
    if (g_is_oled) oled_disable_hooks();
    else lcd_disable_hooks();
    return SCE_KERNEL_STOP_SUCCESS;
}
