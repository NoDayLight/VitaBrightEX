#include "screen_filter.h"
#include "config.h"
#include "filter_policy.h"
#include "filter_state_core.h"
#include "state_lock.h"
#include "status.h"
#include <stdint.h>
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/sysmem.h>

static VbeFilterStateCore g_filter_state;
static int g_filter_state_initialized = 0;

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

static void config_candidate(ScreenFilterParams *out) {
    out->cct = (uint16_t)g_config.filter_cct;
    out->gamma = g_config.filter_gamma;
    out->contrast = g_config.filter_contrast;
    out->brightness = g_config.filter_brightness;
    out->invert = g_config.filter_invert ? 1 : 0;
    out->panel_enhance = g_config.filter_panel_enhance;
}

static int apply_candidate(const ScreenFilterParams *candidate) {
    ensure_state_initialized();
    VbeFilterRequestPolicy policy = vbe_filter_request_policy(candidate);
    vbe_filter_state_begin_request(&g_filter_state, candidate,
                                   policy.requested_domains,
                                   policy.unsupported_domains);

    /* These are hardware capabilities, not request activity indicators. */
    g_vbe_status.invert = policy.invert_state;
    g_vbe_status.csc_filter = policy.csc_state;
    g_vbe_status.transfer_lut = policy.transfer_state;

    /* Unsupported requests are capability results, never runtime errors. No
     * generic filter hardware is touched in pseudo-v1.4. */
    status_clear_error_domain(VBE_ERROR_DOMAIN_FILTER);
    return policy.result;
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

    ret = screen_filter_reset(0);
    if (ret >= 0)
        status_stage_result(VBE_ERROR_DOMAIN_INPUT, 1,
                            VBE_ERR_INVALID_USER_INPUT, 0);

    ret = state_lock_release_result(ret);
    EXIT_SYSCALL(state);
    return ret;
}
