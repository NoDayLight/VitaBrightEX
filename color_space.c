#include "color_space.h"
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
static int g_original_mode = -1;
static int g_last_mode = -1;
static int g_changed = 0;

static void resolution_reset(void) {
    g_get_mode = NULL;
    g_set_mode = NULL;
    g_resolved_for_oled = -1;
    g_original_mode = -1;
    g_last_mode = -1;
    g_changed = 0;
}

/* Historical ABI note: status field lcd_color_space is retained at the same
 * struct offset for binary compatibility, but in v1.4 it reports the selected
 * panel's colour-space capability (LCD or OLED). */
static void status_set_capability(int state) {
    g_vbe_status.lcd_color_space = state;
}

static int resolve_locked(void) {
    if (g_get_mode != NULL && g_set_mode != NULL &&
        g_resolved_for_oled == g_is_oled)
        return 0;

    resolution_reset();

    const char *module = g_is_oled ? "SceOled" : "SceLcd";
    uint32_t get_nid = g_is_oled ? NID_OLED_GET_COLOR_SPACE : NID_LCD_GET_COLOR_SPACE;
    uint32_t set_nid = g_is_oled ? NID_OLED_SET_COLOR_SPACE : NID_LCD_SET_COLOR_SPACE;

    int ret = module_get_export_func(KERNEL_PID, module, TAI_ANY_LIBRARY,
        get_nid, (uintptr_t *)&g_get_mode);
    if (ret < 0 || g_get_mode == NULL) {
        resolution_reset();
        status_set_capability(VBE_CAP_UNAVAILABLE);
        return ret < 0 ? ret : -1;
    }

    ret = module_get_export_func(KERNEL_PID, module, TAI_ANY_LIBRARY,
        set_nid, (uintptr_t *)&g_set_mode);
    if (ret < 0 || g_set_mode == NULL) {
        resolution_reset();
        status_set_capability(VBE_CAP_UNAVAILABLE);
        return ret < 0 ? ret : -1;
    }

    int mode = g_get_mode();
    /* Only the independently observed 0/1 panel modes are exposed.  If a
     * firmware reports another state, fail closed rather than guessing its
     * semantics or overwriting it. */
    if (mode < 0 || mode > 1) {
        int detail = mode < 0 ? mode : -mode;
        resolution_reset();
        status_set_capability(VBE_CAP_FAILED);
        status_set_error(VBE_ERR_DISPLAY_CAPABILITY, detail);
        return detail ? detail : -1;
    }

    g_resolved_for_oled = g_is_oled;
    g_original_mode = mode;
    g_last_mode = mode;
    g_changed = 0;
    status_set_capability(VBE_CAP_INACTIVE);
    return 0;
}

static int set_locked(int mode) {
    if (mode != 0 && mode != 1) {
        status_set_error(VBE_ERR_INVALID_USER_INPUT, mode);
        return -1;
    }

    int ret = resolve_locked();
    if (ret < 0) return ret;

    int current = g_get_mode();
    if (current < 0 || current > 1) {
        status_set_capability(VBE_CAP_FAILED);
        status_set_error(VBE_ERR_DISPLAY_CAPABILITY, current < 0 ? current : -current);
        return current < 0 ? current : -1;
    }

    if (current != mode) {
        ret = g_set_mode(mode);
        if (ret < 0) {
            status_set_capability(VBE_CAP_FAILED);
            status_set_error(VBE_ERR_DISPLAY_CAPABILITY, ret);
            return ret;
        }

        current = g_get_mode();
        if (current < 0 || current != mode) {
            int detail = current < 0 ? current : -(0x200 + current);
            status_set_capability(VBE_CAP_FAILED);
            status_set_error(VBE_ERR_DISPLAY_CAPABILITY, detail);
            return detail;
        }
    }

    g_last_mode = current;
    g_changed = current != g_original_mode;
    status_set_capability(g_changed ? VBE_CAP_ACTIVE : VBE_CAP_INACTIVE);
    return 0;
}

static int config_requests_enhanced_mode(void) {
    /* lcd_saturation_boost/lcd_ips_enhance were v1.3 names for the same live
     * colour-space switch.  Preserve their behaviour as compatibility aliases
     * without pretending that they are independent IPS/transfer-LUT stages. */
    return g_config.lcd_color_space_mode ||
           g_config.lcd_saturation_boost ||
           g_config.lcd_ips_enhance;
}

int color_space_apply_config(void) {
    int requested = config_requests_enhanced_mode();
    int ret = resolve_locked();
    if (ret < 0)
        return requested ? ret : 0;

    if (requested)
        return set_locked(1);

    /* A neutral config means "leave firmware/native state alone".  If this
     * plugin previously changed the mode in the same session, restore exactly
     * the value that was observed before our first write. */
    if (g_changed)
        return set_locked(g_original_mode);

    status_set_capability(VBE_CAP_INACTIVE);
    return 0;
}

void color_space_shutdown(void) {
    if (g_changed && g_set_mode != NULL && g_get_mode != NULL &&
        g_original_mode >= 0 && g_original_mode <= 1) {
        int current = g_get_mode();
        if (current >= 0 && current <= 1 && current != g_original_mode) {
            if (g_set_mode(g_original_mode) >= 0)
                (void)g_get_mode();
        }
    }
    resolution_reset();
    if (g_vbe_status.lcd_color_space == VBE_CAP_ACTIVE)
        status_set_capability(VBE_CAP_INACTIVE);
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
        if (ret < 0 || ret > 1) {
            int detail = ret < 0 ? ret : -ret;
            status_set_capability(VBE_CAP_FAILED);
            status_set_error(VBE_ERR_DISPLAY_CAPABILITY, detail);
            ret = detail ? detail : -1;
        } else {
            g_last_mode = ret;
            g_changed = ret != g_original_mode;
            status_set_capability(g_changed ? VBE_CAP_ACTIVE : VBE_CAP_INACTIVE);
        }
    }

    state_lock_release();
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

    ret = set_locked(mode);
    if (ret == 0) status_clear_error();

    state_lock_release();
    EXIT_SYSCALL(state);
    return ret;
}
