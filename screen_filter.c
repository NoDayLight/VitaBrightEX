#include "screen_filter.h"
#include "config.h"
#include "display_domains.h"
#include "filter_policy.h"
#include "filter_state_core.h"
#include "main.h"
#include "state_lock.h"
#include "status.h"
#include "taihen_extra.h"
#include <stdint.h>
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/sysmem.h>
#include <taihen.h>

#define NID_DISPLAY_INVERT_COLORS 0x19140ACD

static VbeFilterStateCore g_filter_state;
static int g_filter_state_initialized = 0;
static int (*ksceDisplaySetInvertColors)(int head, int enable) = NULL;
static int g_invert_programmed = 0;
static int g_invert_value = 0;

static void ensure_state_initialized(void) {
    if (g_filter_state_initialized) return;
    vbe_filter_state_init(&g_filter_state);
    g_filter_state_initialized = 1;
}

static int float_is_finite(float value) {
    union { float f; uint32_t u; } bits;
    bits.f = value;
    return (bits.u & 0x7F800000u) != 0x7F800000u;
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

static void config_candidate(ScreenFilterParams *out) {
    out->cct = (uint16_t)g_config.filter_cct;
    out->gamma = g_config.filter_gamma;
    out->contrast = g_config.filter_contrast;
    out->brightness = g_config.filter_brightness;
    out->invert = g_config.filter_invert ? 1 : 0;
    out->panel_enhance = g_config.filter_panel_enhance;
}

static int apply_invert(const ScreenFilterParams *candidate) {
    ensure_state_initialized();

    if (!candidate->invert && !g_invert_programmed) {
        g_vbe_status.invert = VBE_CAP_INACTIVE;
        vbe_filter_state_commit_invert(&g_filter_state, 0);
        return 0;
    }

    int ret = resolve_invert();
    if (ret < 0) {
        g_vbe_status.invert = VBE_CAP_FAILED;
        vbe_filter_state_mark_failed(&g_filter_state, VBE_DISPLAY_DOMAIN_INVERT);
        status_set_error_domain(VBE_ERROR_DOMAIN_FILTER,
                                VBE_ERR_DISPLAY_CAPABILITY, ret);
        return ret;
    }

    int requested = candidate->invert ? 1 : 0;
    if (!g_invert_programmed || g_invert_value != requested) {
        ret = ksceDisplaySetInvertColors(0, requested);
        if (ret < 0) {
            g_vbe_status.invert = VBE_CAP_FAILED;
            vbe_filter_state_mark_failed(&g_filter_state, VBE_DISPLAY_DOMAIN_INVERT);
            status_set_error_domain(VBE_ERROR_DOMAIN_FILTER,
                                    VBE_ERR_DISPLAY_CAPABILITY, ret);
            return ret;
        }
    }

    g_invert_value = requested;
    g_invert_programmed = requested;
    g_vbe_status.invert = requested ? VBE_CAP_ACTIVE : VBE_CAP_INACTIVE;
    vbe_filter_state_commit_invert(&g_filter_state, requested);
    return 0;
}

static int apply_candidate(const ScreenFilterParams *candidate) {
    ensure_state_initialized();
    VbeFilterRequestPolicy policy = vbe_filter_request_policy(candidate);
    vbe_filter_state_begin_request(&g_filter_state, candidate,
                                   policy.requested_domains,
                                   policy.unsupported_domains);
    g_vbe_status.csc_filter = policy.csc_state;
    g_vbe_status.transfer_lut = policy.transfer_state;

    int ret = apply_invert(candidate);
    if (ret < 0) return ret;

    status_clear_error_domain(VBE_ERROR_DOMAIN_FILTER);
    if (policy.unsupported_domains != 0)
        return VBE_RESULT_UNSUPPORTED;
    return VBE_RESULT_OK;
}

int screen_filter_apply_config(void) {
    ScreenFilterParams candidate;
    config_candidate(&candidate);
    return apply_candidate(&candidate);
}

int screen_filter_reset(int is_oled_unused) {
    (void)is_oled_unused;
    ScreenFilterParams neutral;
    vbe_filter_params_neutral(&neutral);
    return apply_candidate(&neutral);
}

int vitabrightFilterGetParams(ScreenFilterParams *out) {
    int state;
    ENTER_SYSCALL(state);
    int ret = state_lock_acquire();
    if (ret < 0) {
        EXIT_SYSCALL(state);
        return ret;
    }

    ensure_state_initialized();
    ScreenFilterParams snapshot = g_filter_state.committed;
    ret = state_lock_release_result(0);
    if (ret >= 0)
        ret = ksceKernelMemcpyKernelToUser((void *)out, &snapshot, sizeof(snapshot));
    EXIT_SYSCALL(state);
    return ret;
}

int vitabrightFilterGetState(VitaBrightDisplayFilterState *out) {
    int state;
    ENTER_SYSCALL(state);
    int ret = state_lock_acquire();
    if (ret < 0) {
        EXIT_SYSCALL(state);
        return ret;
    }

    ensure_state_initialized();
    VitaBrightDisplayFilterState snapshot = {
        .abi_version = VBE_DISPLAY_FILTER_STATE_ABI_VERSION,
        .requested = g_filter_state.requested,
        .committed = g_filter_state.committed,
        .requested_domains = g_filter_state.requested_domains,
        .committed_domains = g_filter_state.committed_domains,
        .unsupported_domains = g_filter_state.unsupported_domains,
        .failed_domains = g_filter_state.failed_domains,
    };
    ret = state_lock_release_result(0);
    if (ret >= 0)
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
        int lock = state_lock_acquire();
        if (lock >= 0) {
            status_stage_result(VBE_ERROR_DOMAIN_INPUT, 0,
                                VBE_ERR_INVALID_USER_INPUT, detail);
            detail = state_lock_release_result(detail);
        } else {
            detail = lock;
        }
        EXIT_SYSCALL(state);
        return detail;
    }

    ret = state_lock_acquire();
    if (ret < 0) {
        EXIT_SYSCALL(state);
        return ret;
    }
    status_stage_result(VBE_ERROR_DOMAIN_INPUT, 1,
                        VBE_ERR_INVALID_USER_INPUT, 0);

    ret = apply_candidate(&candidate);
    ret = state_lock_release_result(ret);
    EXIT_SYSCALL(state);
    return ret;
}

int vitabrightFilterReset(int is_oled_unused) {
    int state;
    (void)is_oled_unused;
    ENTER_SYSCALL(state);
    int ret = state_lock_acquire();
    if (ret < 0) {
        EXIT_SYSCALL(state);
        return ret;
    }

    ret = screen_filter_reset(g_is_oled);
    if (ret >= 0)
        status_stage_result(VBE_ERROR_DOMAIN_INPUT, 1,
                            VBE_ERR_INVALID_USER_INPUT, 0);

    ret = state_lock_release_result(ret);
    EXIT_SYSCALL(state);
    return ret;
}
