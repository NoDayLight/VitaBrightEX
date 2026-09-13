#include "color_space.h"
#include "color_space_state_core.h"
#include "config.h"
#include "main.h"
#include "state_lock.h"
#include "status.h"
#include "taihen_extra.h"
#include <stdint.h>
#include <psp2kern/kernel/cpu.h>
#include <taihen.h>

#define NID_LCD_GET_COLOR_SPACE   0x17F66722
#define NID_LCD_SET_COLOR_SPACE   0xD40968FB
#define NID_OLED_GET_COLOR_SPACE  0x4F8A1D4A
#define NID_OLED_SET_COLOR_SPACE  0xDABBD9D3

static int (*g_get_mode)(void) = NULL;
static int (*g_set_mode)(int mode) = NULL;
static int g_resolved_for_oled = -1;
static VbeColorSpaceOwnership g_color_ownership = {
    .original_mode = -1,
    .restore_required = 0,
};

static void clear_resolution(void) {
    g_get_mode = NULL;
    g_set_mode = NULL;
    g_resolved_for_oled = -1;
}

static void forget_clean_session(void) {
    clear_resolution();
    g_color_ownership.original_mode = -1;
    g_color_ownership.restore_required = 0;
}

static void status_set_capability(int state) {
    g_vbe_status.lcd_color_space = state;
}

static void color_error(int detail) {
    status_set_error_domain(VBE_ERROR_DOMAIN_COLOR_SPACE,
                            VBE_ERR_DISPLAY_CAPABILITY, detail);
}

static int valid_mode(int mode) {
    return mode == 0 || mode == 1;
}

static void observe_mode(int mode) {
    vbe_color_space_ownership_observe(&g_color_ownership, mode);
    status_set_capability(g_color_ownership.restore_required
                              ? VBE_CAP_ACTIVE : VBE_CAP_INACTIVE);
}

static int resolve_locked(void) {
    if (g_get_mode && g_set_mode && g_resolved_for_oled == g_is_oled)
        return 0;

    /* Never discard an owned restoration obligation merely to re-resolve
     * exports. Losing the authoritative setter/getter while ownership exists
     * is unload-unsafe and must remain visible. */
    if (g_color_ownership.restore_required) {
        status_set_capability(VBE_CAP_FAILED);
        color_error(-1);
        return -1;
    }

    forget_clean_session();
    const char *module = g_is_oled ? "SceOled" : "SceLcd";
    uint32_t get_nid = g_is_oled ? NID_OLED_GET_COLOR_SPACE
                                 : NID_LCD_GET_COLOR_SPACE;
    uint32_t set_nid = g_is_oled ? NID_OLED_SET_COLOR_SPACE
                                 : NID_LCD_SET_COLOR_SPACE;

    int ret = module_get_export_func(KERNEL_PID, module, TAI_ANY_LIBRARY,
                                     get_nid, (uintptr_t *)&g_get_mode);
    if (ret < 0 || !g_get_mode) {
        forget_clean_session();
        status_set_capability(VBE_CAP_UNAVAILABLE);
        return ret < 0 ? ret : -1;
    }
    ret = module_get_export_func(KERNEL_PID, module, TAI_ANY_LIBRARY,
                                 set_nid, (uintptr_t *)&g_set_mode);
    if (ret < 0 || !g_set_mode) {
        forget_clean_session();
        status_set_capability(VBE_CAP_UNAVAILABLE);
        return ret < 0 ? ret : -1;
    }

    int mode = g_get_mode();
    if (!valid_mode(mode)) {
        int detail = mode < 0 ? mode : -mode;
        forget_clean_session();
        status_set_capability(VBE_CAP_FAILED);
        color_error(detail);
        return detail ? detail : -1;
    }

    g_resolved_for_oled = g_is_oled;
    vbe_color_space_ownership_init(&g_color_ownership, mode);
    status_set_capability(VBE_CAP_INACTIVE);
    return 0;
}

static int set_mode_locked(int mode) {
    int ret = resolve_locked();
    if (ret < 0) {
        color_error(ret);
        return ret;
    }

    int current = g_get_mode();
    if (!valid_mode(current)) {
        status_set_capability(VBE_CAP_FAILED);
        color_error(current < 0 ? current : -current);
        return current < 0 ? current : -1;
    }
    observe_mode(current);

    if (current != mode) {
        ret = g_set_mode(mode);
        if (ret < 0) {
            status_set_capability(VBE_CAP_FAILED);
            color_error(ret);
            return ret;
        }

        /* The successful plugin write, not observation, creates restoration
         * ownership when it moves hardware away from the original mode. */
        vbe_color_space_ownership_note_write(&g_color_ownership, mode);

        current = g_get_mode();
        if (!valid_mode(current)) {
            status_set_capability(VBE_CAP_FAILED);
            color_error(current < 0 ? current : -current);
            return current < 0 ? current : -1;
        }
        observe_mode(current);
        if (current != mode) {
            int detail = -(0x200 + current);
            status_set_capability(VBE_CAP_FAILED);
            color_error(detail);
            return detail;
        }
    }

    status_clear_error_domain(VBE_ERROR_DOMAIN_COLOR_SPACE);
    return 0;
}

static int config_requests_enhanced_mode(void) {
    if (!g_config.display_color_space_mode) return 0;
    if (g_config.display_color_space_scope == VBE_COLOR_SPACE_SCOPE_LCD_ONLY &&
        g_is_oled)
        return 0;
    return 1;
}

int color_space_apply_config(void) {
    int requested = config_requests_enhanced_mode();
    int ret = resolve_locked();
    if (ret < 0) {
        if (!requested && !g_color_ownership.restore_required) {
            status_clear_error_domain(VBE_ERROR_DOMAIN_COLOR_SPACE);
            return 0;
        }
        color_error(ret);
        return ret;
    }

    if (requested)
        return set_mode_locked(1);
    if (g_color_ownership.restore_required)
        return set_mode_locked(g_color_ownership.original_mode);

    status_set_capability(VBE_CAP_INACTIVE);
    status_clear_error_domain(VBE_ERROR_DOMAIN_COLOR_SPACE);
    return 0;
}

int color_space_shutdown(void) {
    if (!g_color_ownership.restore_required) {
        forget_clean_session();
        if (g_vbe_status.lcd_color_space == VBE_CAP_ACTIVE)
            status_set_capability(VBE_CAP_INACTIVE);
        return 0;
    }

    if (!g_get_mode || !g_set_mode ||
        !valid_mode(g_color_ownership.original_mode)) {
        status_set_capability(VBE_CAP_FAILED);
        color_error(-1);
        return -1;
    }

    int current = g_get_mode();
    if (!valid_mode(current)) {
        int detail = current < 0 ? current : -current;
        status_set_capability(VBE_CAP_FAILED);
        color_error(detail);
        return detail ? detail : -1;
    }
    observe_mode(current);

    if (g_color_ownership.restore_required) {
        int ret = g_set_mode(g_color_ownership.original_mode);
        if (ret < 0) {
            status_set_capability(VBE_CAP_FAILED);
            color_error(ret);
            return ret;
        }
        vbe_color_space_ownership_note_write(&g_color_ownership,
                                             g_color_ownership.original_mode);
        current = g_get_mode();
        if (!valid_mode(current)) {
            int detail = current < 0 ? current : -current;
            status_set_capability(VBE_CAP_FAILED);
            color_error(detail);
            return detail ? detail : -1;
        }
        observe_mode(current);
        if (current != g_color_ownership.original_mode ||
            g_color_ownership.restore_required) {
            int detail = -(0x300 + current);
            status_set_capability(VBE_CAP_FAILED);
            color_error(detail);
            return detail;
        }
    }

    status_set_capability(VBE_CAP_INACTIVE);
    status_clear_error_domain(VBE_ERROR_DOMAIN_COLOR_SPACE);
    forget_clean_session();
    return 0;
}

int vitabrightColorSpaceGetMode(void) {
    int state;
    ENTER_SYSCALL(state);
    int ret = state_lock_acquire();
    if (ret < 0) {
        EXIT_SYSCALL(state);
        return ret;
    }

    ret = resolve_locked();
    if (ret >= 0) {
        ret = g_get_mode();
        if (!valid_mode(ret)) {
            int detail = ret < 0 ? ret : -ret;
            status_set_capability(VBE_CAP_FAILED);
            color_error(detail);
            ret = detail ? detail : -1;
        } else {
            observe_mode(ret);
            status_clear_error_domain(VBE_ERROR_DOMAIN_COLOR_SPACE);
        }
    } else {
        color_error(ret);
    }

    ret = state_lock_release_result(ret);
    EXIT_SYSCALL(state);
    return ret;
}

int vitabrightColorSpaceSetMode(int mode) {
    int state;
    ENTER_SYSCALL(state);
    int ret = state_lock_acquire();
    if (ret < 0) {
        EXIT_SYSCALL(state);
        return ret;
    }
    if (!valid_mode(mode)) {
        status_stage_result(VBE_ERROR_DOMAIN_INPUT, 0,
                            VBE_ERR_INVALID_USER_INPUT, mode);
        ret = state_lock_release_result(-1);
        EXIT_SYSCALL(state);
        return ret;
    }
    status_stage_result(VBE_ERROR_DOMAIN_INPUT, 1,
                        VBE_ERR_INVALID_USER_INPUT, 0);
    ret = set_mode_locked(mode);
    ret = state_lock_release_result(ret);
    EXIT_SYSCALL(state);
    return ret;
}
