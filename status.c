#include "status.h"
#include "state_lock.h"
#include "status_error_core.h"
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/sysmem.h>

VitaBrightStatus g_vbe_status;
static VbeErrorState g_error_state;

static void refresh_summary(void) {
    VbeErrorSlot summary = vbe_error_state_summary(&g_error_state);
    g_vbe_status.last_error = summary.error;
    g_vbe_status.last_error_detail = summary.detail;
}

void status_init(int hardware, uint32_t firmware) {
    VitaBrightStatus s = {0};
    s.abi_version = 2;
    s.firmware = firmware;
    s.hardware = hardware;
    s.panel_type = 0;
    s.state_lock = VBE_CAP_INACTIVE;
    s.firmware_layout = VBE_CAP_UNKNOWN;
    s.brightness_core = VBE_CAP_INACTIVE;
    s.brightness_table = VBE_CAP_INACTIVE;
    s.brightness_hook = VBE_CAP_INACTIVE;
    s.power_limit_hook = VBE_CAP_INACTIVE;
    s.invert = VBE_CAP_UNKNOWN;
    s.lcd_color_space = VBE_CAP_UNKNOWN;
    s.csc_filter = VBE_CAP_UNSUPPORTED;
    s.transfer_lut = VBE_CAP_UNSUPPORTED;
    s.registry_api = VBE_CAP_UNSUPPORTED;
    s.last_error = VBE_ERR_NONE;
    s.last_error_detail = 0;
    g_vbe_status = s;
    vbe_error_state_init(&g_error_state);
}

void status_set_error_domain(int domain, int error, int detail) {
    vbe_error_state_set(&g_error_state, domain, error, detail);
    refresh_summary();
}

void status_set_error(int error, int detail) {
    int domain = VBE_ERROR_DOMAIN_BRIGHTNESS;
    if (error == VBE_ERR_CONFIG) domain = VBE_ERROR_DOMAIN_CONFIG;
    else if (error == VBE_ERR_DISPLAY_CAPABILITY)
        domain = VBE_ERROR_DOMAIN_COLOR_SPACE;
    else if (error == VBE_ERR_INVALID_USER_INPUT)
        domain = VBE_ERROR_DOMAIN_INPUT;
    else if (error == VBE_ERR_SYNCHRONIZATION)
        domain = VBE_ERROR_DOMAIN_SYNC;
    status_set_error_domain(domain, error, detail);
}

void status_clear_error_domain(int domain) {
    vbe_error_state_clear(&g_error_state, domain);
    refresh_summary();
}

int vitabrightGetStatus(VitaBrightStatus *out) {
    int state;
    int ret;
    ENTER_SYSCALL(state);

    ret = state_lock_acquire();
    if (ret < 0) {
        EXIT_SYSCALL(state);
        return ret;
    }

    VitaBrightStatus snapshot = g_vbe_status;
    ret = ksceKernelMemcpyKernelToUser((void *)out, &snapshot, sizeof(snapshot));
    state_lock_release();
    EXIT_SYSCALL(state);
    return ret;
}

int vitabrightGetDiagnostics(VitaBrightDiagnostics *out) {
    int state;
    int ret;
    ENTER_SYSCALL(state);

    ret = state_lock_acquire();
    if (ret < 0) {
        EXIT_SYSCALL(state);
        return ret;
    }

    VitaBrightDiagnostics d = {0};
    d.abi_version = 1;
    VbeErrorSlot slot = vbe_error_state_get(&g_error_state, VBE_ERROR_DOMAIN_CONFIG);
    d.config_error = slot.error;
    d.config_detail = slot.detail;
    slot = vbe_error_state_get(&g_error_state, VBE_ERROR_DOMAIN_BRIGHTNESS);
    d.brightness_error = slot.error;
    d.brightness_detail = slot.detail;
    slot = vbe_error_state_get(&g_error_state, VBE_ERROR_DOMAIN_COLOR_SPACE);
    d.color_space_error = slot.error;
    d.color_space_detail = slot.detail;
    slot = vbe_error_state_get(&g_error_state, VBE_ERROR_DOMAIN_FILTER);
    d.filter_error = slot.error;
    d.filter_detail = slot.detail;
    slot = vbe_error_state_get(&g_error_state, VBE_ERROR_DOMAIN_INPUT);
    d.input_error = slot.error;
    d.input_detail = slot.detail;
    slot = vbe_error_state_get(&g_error_state, VBE_ERROR_DOMAIN_SYNC);
    d.synchronization_error = slot.error;
    d.synchronization_detail = slot.detail;

    ret = ksceKernelMemcpyKernelToUser((void *)out, &d, sizeof(d));
    state_lock_release();
    EXIT_SYSCALL(state);
    return ret;
}
