#include "status.h"
#include "state_lock.h"
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/sysmem.h>

VitaBrightStatus g_vbe_status;

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
}

void status_set_error(int error, int detail) {
    g_vbe_status.last_error = error;
    g_vbe_status.last_error_detail = detail;
}

void status_clear_error(void) {
    g_vbe_status.last_error = VBE_ERR_NONE;
    g_vbe_status.last_error_detail = 0;
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
