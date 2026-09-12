#include "screen_filter.h"
#include "config.h"
#include "log.h"
#include "main.h"
#include "state_lock.h"
#include "status.h"
#include "taihen_extra.h"
#include <stdint.h>
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/sysmem.h>
#include <taihen.h>

#define NID_DISPLAY_INVERT_COLORS 0x19140ACD

ScreenFilterParams g_screen_filter = {
    .cct = CCT_DEFAULT,
    .gamma = 1.0f,
    .contrast = 1.0f,
    .brightness = 0.0f,
    .invert = 0,
    .panel_enhance = 0,
};

static int (*ksceDisplaySetInvertColors)(int head, int enable) = NULL;

static int float_is_finite(float value) {
    union { float f; uint32_t u; } bits;
    bits.f = value;
    return (bits.u & 0x7F800000u) != 0x7F800000u;
}

static int resolve_invert(void) {
    if (ksceDisplaySetInvertColors != NULL) return 0;

    int ret = module_get_export_func(KERNEL_PID, "SceDisplay", TAI_ANY_LIBRARY,
        NID_DISPLAY_INVERT_COLORS, (uintptr_t *)&ksceDisplaySetInvertColors);
    if (ret < 0 || ksceDisplaySetInvertColors == NULL) {
        ksceDisplaySetInvertColors = NULL;
        g_vbe_status.invert = VBE_CAP_UNAVAILABLE;
        return ret < 0 ? ret : -1;
    }

    g_vbe_status.invert = VBE_CAP_INACTIVE;
    return 0;
}

static int params_valid(const ScreenFilterParams *p) {
    if (p->cct < 1000 || p->cct > 25100) return 0;
    if (!float_is_finite(p->gamma) || p->gamma < 0.1f || p->gamma > 8.0f) return 0;
    if (!float_is_finite(p->contrast) || p->contrast < 0.0f || p->contrast > 4.0f) return 0;
    if (!float_is_finite(p->brightness) || p->brightness < -1.0f || p->brightness > 1.0f) return 0;
    if (p->invert != 0 && p->invert != 1) return 0;
    if (p->panel_enhance < 0 || p->panel_enhance > 2) return 0;
    return 1;
}

static int advanced_filter_requested(const ScreenFilterParams *p) {
    return p->cct != CCT_DEFAULT || p->gamma != 1.0f ||
           p->contrast != 1.0f || p->brightness != 0.0f ||
           p->panel_enhance != 0;
}

void screen_filter_load_config(void) {
    ScreenFilterParams p;
    p.cct = (uint16_t)g_config.filter_cct;
    p.gamma = g_config.filter_gamma;
    p.contrast = g_config.filter_contrast;
    p.brightness = g_config.filter_brightness;
    p.invert = g_config.filter_invert ? 1 : 0;
    p.panel_enhance = g_config.filter_panel_enhance;
    g_screen_filter = p;
}

int screen_filter_apply(int is_oled) {
    (void)is_oled;

    /* No speculative persistent IFTU path.  The documented ksceIftuCsc API
     * is an explicit buffer conversion API, not an active-scanout setter.
     * Gamma/panel linearisation additionally require a nonlinear stage. */
    g_vbe_status.csc_filter = VBE_CAP_UNSUPPORTED;
    g_vbe_status.transfer_lut = VBE_CAP_UNSUPPORTED;

    if (resolve_invert() < 0) {
        if (g_screen_filter.invert) {
            status_set_error(VBE_ERR_DISPLAY_CAPABILITY, NID_DISPLAY_INVERT_COLORS);
            return -1;
        }
        return advanced_filter_requested(&g_screen_filter) ? -2 : 0;
    }

    int ret = ksceDisplaySetInvertColors(0, g_screen_filter.invert ? 1 : 0);
    if (ret < 0) {
        g_vbe_status.invert = VBE_CAP_FAILED;
        status_set_error(VBE_ERR_DISPLAY_CAPABILITY, ret);
        return ret;
    }
    g_vbe_status.invert = g_screen_filter.invert ? VBE_CAP_ACTIVE : VBE_CAP_INACTIVE;
    return advanced_filter_requested(&g_screen_filter) ? -2 : 0;
}

void screen_filter_set_cct(uint16_t cct, int is_oled) {
    ScreenFilterParams candidate = g_screen_filter;
    candidate.cct = cct;
    if (!params_valid(&candidate)) return;
    g_screen_filter = candidate;
    (void)screen_filter_apply(is_oled);
}

void screen_filter_reset(int is_oled) {
    ScreenFilterParams neutral = {
        .cct = CCT_DEFAULT,
        .gamma = 1.0f,
        .contrast = 1.0f,
        .brightness = 0.0f,
        .invert = 0,
        .panel_enhance = 0,
    };
    g_screen_filter = neutral;
    (void)screen_filter_apply(is_oled);
}

int vitabrightFilterGetParams(ScreenFilterParams *out) {
    int state;
    ENTER_SYSCALL(state);
    int ret = state_lock_acquire();
    if (ret < 0) { EXIT_SYSCALL(state); return ret; }

    ScreenFilterParams snapshot = g_screen_filter;
    state_lock_release();
    ret = ksceKernelMemcpyKernelToUser((void *)out, &snapshot, sizeof(snapshot));
    EXIT_SYSCALL(state);
    return ret;
}

int vitabrightFilterSetParams(const ScreenFilterParams *in, int is_oled_unused) {
    int state;
    ScreenFilterParams candidate;
    (void)is_oled_unused;
    ENTER_SYSCALL(state);

    int ret = ksceKernelMemcpyUserToKernel(&candidate, (const void *)in, sizeof(candidate));
    if (ret < 0 || !params_valid(&candidate)) {
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
    g_screen_filter = candidate;
    ret = screen_filter_apply(g_is_oled);
    state_lock_release();
    EXIT_SYSCALL(state);
    return ret;
}

int vitabrightFilterReset(int is_oled_unused) {
    int state;
    (void)is_oled_unused;
    ENTER_SYSCALL(state);
    int ret = state_lock_acquire();
    if (ret < 0) { EXIT_SYSCALL(state); return ret; }
    screen_filter_reset(g_is_oled);
    state_lock_release();
    EXIT_SYSCALL(state);
    return 0;
}
