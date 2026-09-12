#include "hooks.h"
#include "../color_space.h"
#include "../config.h"
#include "../log.h"
#include "../main.h"
#include "../screen_filter.h"
#include "../state_lock.h"
#include "../status.h"
#include "../taihen_extra.h"
#include "lut.h"
#include "parser.h"
#include <stdint.h>
#include <psp2kern/io/fcntl.h>
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysmem.h>
#include <taihen.h>

#define NID_OLED_GET_BRIGHTNESS  0x43EF811A
#define NID_OLED_SET_BRIGHTNESS  0xF9624C47
#define NID_OLED_GET_DDB         0xC9D5987C
#define NID_POWER_SET_MAX_BRIGHT 0x77027B6B

#define OLED_LUT_OFF_P4      0x1AB8u
#define OLED_LUT_OFF_P5      0x1C20u
#define OLED_LUT_OFF_DEFAULT 0x1E00u

static SceUID g_lut_inject = -1;
static SceUID g_brightness_hook = -1;
static SceUID g_power_hook = -1;
static tai_hook_ref_t g_brightness_ref = -1;
static tai_hook_ref_t g_power_ref = 0;
static int g_active = 0;
static int g_panel_type = OLED_PANEL_UNKNOWN;
static int g_dim_workaround_enabled = 1;
static char g_lut_source_path[LUT_SOURCE_PATH_MAX];

unsigned char lookupNew[LUT_SIZE];

int (*ksceOledGetBrightness)(void) = NULL;
int (*ksceOledSetBrightness)(unsigned int brightness) = NULL;
int (*ksceOledGetDDB)(uint16_t *supplier_id, uint16_t *supplier_elective_data) = NULL;

static void lut_copy(unsigned char *dst, const unsigned char *src) {
    for (int i = 0; i < LUT_SIZE; ++i) dst[i] = src[i];
}

static void path_copy(char dst[LUT_SOURCE_PATH_MAX], const char *src) {
    int i = 0;
    while (i < LUT_SOURCE_PATH_MAX - 1 && src[i]) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

static int lut_is_sane(const unsigned char lut[LUT_SIZE]) {
    int any_nonzero = 0;
    int any_not_ff = 0;
    for (int i = 0; i < LUT_SIZE; ++i) {
        if (lut[i] != 0x00) any_nonzero = 1;
        if (lut[i] != 0xFF) any_not_ff = 1;
    }
    return any_nonzero && any_not_ff;
}

static int firmware_layout_supported(uint32_t firmware) {
    switch (firmware >> 16) {
    case 0x360:
    case 0x365:
    case 0x367:
    case 0x368:
    case 0x369:
    case 0x370:
        return 1;
    default:
        return 0;
    }
}

static int resolve_core(tai_module_info_t *info) {
    info->size = sizeof(*info);
    int ret = taiGetModuleInfoForKernel(KERNEL_PID, "SceOled", info);
    if (ret < 0) return ret;

    ksceOledGetBrightness = NULL;
    ksceOledSetBrightness = NULL;
    ksceOledGetDDB = NULL;

    ret = module_get_export_func(KERNEL_PID, "SceOled", TAI_ANY_LIBRARY,
        NID_OLED_GET_BRIGHTNESS, (uintptr_t *)&ksceOledGetBrightness);
    if (ret < 0 || ksceOledGetBrightness == NULL) return ret < 0 ? ret : -1;

    ret = module_get_export_func(KERNEL_PID, "SceOled", TAI_ANY_LIBRARY,
        NID_OLED_SET_BRIGHTNESS, (uintptr_t *)&ksceOledSetBrightness);
    if (ret < 0 || ksceOledSetBrightness == NULL) return ret < 0 ? ret : -1;

    ret = module_get_export_func(KERNEL_PID, "SceOled", TAI_ANY_LIBRARY,
        NID_OLED_GET_DDB, (uintptr_t *)&ksceOledGetDDB);
    if (ret < 0 || ksceOledGetDDB == NULL) return ret < 0 ? ret : -1;

    return 0;
}

static int read_panel(int *panel_type, uint32_t *lut_offset) {
    uint16_t supplier_id = 0;
    uint16_t sed = 0;
    if (ksceOledGetDDB == NULL) return -1;

    int ret = ksceOledGetDDB(&supplier_id, &sed);
    if (ret < 0) return ret;

    LOG("[OLED] DDB supplier=0x%04X data=0x%04X\n", supplier_id, sed);
    switch (sed & 0xFF) {
    case 4:
        *panel_type = OLED_PANEL_4;
        *lut_offset = OLED_LUT_OFF_P4;
        break;
    case 5:
        *panel_type = OLED_PANEL_5;
        *lut_offset = OLED_LUT_OFF_P5;
        break;
    case 6:
        *panel_type = OLED_PANEL_6;
        *lut_offset = OLED_LUT_OFF_DEFAULT;
        break;
    default:
        *panel_type = OLED_PANEL_UNKNOWN;
        *lut_offset = OLED_LUT_OFF_DEFAULT;
        break;
    }
    return 0;
}

static int validate_layout(const tai_module_info_t *info, uint32_t lut_offset) {
    uintptr_t address = 0;
    int ret = module_get_offset(KERNEL_PID, info->modid, 0, lut_offset, &address);
    if (ret < 0 || address == 0) return ret < 0 ? ret : -1;

    unsigned char snapshot[LUT_SIZE];
    const volatile unsigned char *source = (const volatile unsigned char *)address;
    for (int i = 0; i < LUT_SIZE; ++i) snapshot[i] = source[i];
    return lut_is_sane(snapshot) ? 0 : -1;
}

int oled_detect_panel(void) {
    int panel = OLED_PANEL_UNKNOWN;
    uint32_t offset = 0;
    if (read_panel(&panel, &offset) < 0) return OLED_PANEL_UNKNOWN;
    return panel;
}

static int load_disk_candidate(int panel_type, unsigned char out[LUT_SIZE],
                               char source_path[LUT_SOURCE_PATH_MAX]) {
    int ret;
    if (g_config.oled_panel_lut_override && g_config.panel_lut_path[0] != '\0')
        ret = parse_lut_override(g_config.panel_lut_path, out, source_path);
    else
        ret = parse_lut(panel_type, out, source_path);

    if (ret < 0) return ret;
    return lut_is_sane(out) ? 0 : -1;
}

int hook_ksceOledSetBrightness(unsigned int brightness) {
    if (brightness == 1 && g_dim_workaround_enabled && g_config.oled_dim_workaround &&
        ksceOledGetBrightness != NULL) {
        int old_brightness = ksceOledGetBrightness();
        if (old_brightness >= 0 && old_brightness <= 4 * 0x1000)
            return TAI_CONTINUE(int, g_brightness_ref, (unsigned int)old_brightness);
    }
    return TAI_CONTINUE(int, g_brightness_ref, brightness);
}

int hook_kscePowerSetDisplayMaxBrightnessForOled(int limit) {
    if (g_power_ref == 0) return 0;
    if (limit < 0x10000 && limit >= 0)
        limit = 0x10000 - 2 * 0x1000;
    else
        limit = 0x10000;
    return TAI_CONTINUE(int, g_power_ref, limit);
}

static void release_transaction(void) {
    if (g_power_hook >= 0) {
        (void)taiHookReleaseForKernel(g_power_hook, g_power_ref);
        g_power_hook = -1;
        g_power_ref = 0;
    }
    if (g_brightness_hook >= 0) {
        (void)taiHookReleaseForKernel(g_brightness_hook, g_brightness_ref);
        g_brightness_hook = -1;
        g_brightness_ref = -1;
    }
    if (g_lut_inject >= 0) {
        (void)taiInjectReleaseForKernel(g_lut_inject);
        g_lut_inject = -1;
    }

    g_active = 0;
    if (g_vbe_status.brightness_core == VBE_CAP_ACTIVE)
        g_vbe_status.brightness_core = VBE_CAP_INACTIVE;
    if (g_vbe_status.brightness_table == VBE_CAP_ACTIVE)
        g_vbe_status.brightness_table = VBE_CAP_INACTIVE;
    if (g_vbe_status.brightness_hook == VBE_CAP_ACTIVE)
        g_vbe_status.brightness_hook = VBE_CAP_INACTIVE;
    if (g_vbe_status.power_limit_hook == VBE_CAP_ACTIVE)
        g_vbe_status.power_limit_hook = VBE_CAP_INACTIVE;
}

static int start_transaction(const unsigned char supplied[LUT_SIZE]) {
    if (g_active) return 0;

    if (!firmware_layout_supported(sw_version)) {
        g_vbe_status.firmware_layout = VBE_CAP_UNSUPPORTED;
        status_set_error(VBE_ERR_FIRMWARE_UNSUPPORTED, (int)sw_version);
        return -1;
    }

    tai_module_info_t info;
    int ret = resolve_core(&info);
    if (ret < 0) {
        g_vbe_status.brightness_core = VBE_CAP_FAILED;
        status_set_error(VBE_ERR_EXPORT_RESOLUTION, ret);
        return ret;
    }

    int panel_type = OLED_PANEL_UNKNOWN;
    uint32_t lut_offset = 0;
    ret = read_panel(&panel_type, &lut_offset);
    if (ret < 0) {
        g_vbe_status.firmware_layout = VBE_CAP_FAILED;
        status_set_error(VBE_ERR_LAYOUT_MISMATCH, ret);
        return ret;
    }

    ret = validate_layout(&info, lut_offset);
    if (ret < 0) {
        g_vbe_status.firmware_layout = VBE_CAP_FAILED;
        g_vbe_status.brightness_table = VBE_CAP_FAILED;
        status_set_error(VBE_ERR_LAYOUT_MISMATCH, ret);
        return ret;
    }
    g_vbe_status.firmware_layout = VBE_CAP_ACTIVE;

    unsigned char candidate[LUT_SIZE];
    char source_path[LUT_SOURCE_PATH_MAX];
    source_path[0] = '\0';
    if (supplied != NULL) {
        lut_copy(candidate, supplied);
    } else {
        ret = load_disk_candidate(panel_type, candidate, source_path);
        if (ret < 0) {
            status_set_error(VBE_ERR_INVALID_USER_INPUT, ret);
            return ret;
        }
    }

    if (!lut_is_sane(candidate)) {
        status_set_error(VBE_ERR_INVALID_USER_INPUT, -1);
        return -1;
    }

    g_lut_inject = taiInjectDataForKernel(KERNEL_PID, info.modid, 0,
        lut_offset, candidate, LUT_SIZE);
    if (g_lut_inject < 0) {
        ret = (int)g_lut_inject;
        release_transaction();
        g_vbe_status.brightness_table = VBE_CAP_FAILED;
        status_set_error(VBE_ERR_TABLE_INJECTION, ret);
        return ret;
    }
    g_vbe_status.brightness_table = VBE_CAP_ACTIVE;

    g_brightness_hook = taiHookFunctionExportForKernel(KERNEL_PID,
        &g_brightness_ref, "SceOled", TAI_ANY_LIBRARY,
        NID_OLED_SET_BRIGHTNESS, hook_ksceOledSetBrightness);
    if (g_brightness_hook < 0) {
        ret = (int)g_brightness_hook;
        release_transaction();
        g_vbe_status.brightness_hook = VBE_CAP_FAILED;
        status_set_error(VBE_ERR_BRIGHTNESS_HOOK, ret);
        return ret;
    }
    g_vbe_status.brightness_hook = VBE_CAP_ACTIVE;

    g_power_hook = taiHookFunctionExportForKernel(KERNEL_PID,
        &g_power_ref, "ScePower", TAI_ANY_LIBRARY,
        NID_POWER_SET_MAX_BRIGHT, hook_kscePowerSetDisplayMaxBrightnessForOled);
    if (g_power_hook < 0) {
        ret = (int)g_power_hook;
        release_transaction();
        g_vbe_status.power_limit_hook = VBE_CAP_FAILED;
        status_set_error(VBE_ERR_POWER_HOOK, ret);
        return ret;
    }
    g_vbe_status.power_limit_hook = VBE_CAP_ACTIVE;

    int current = ksceOledGetBrightness();
    if (current < 0) {
        ret = current;
        release_transaction();
        g_vbe_status.brightness_core = VBE_CAP_FAILED;
        status_set_error(VBE_ERR_BACKEND, ret);
        return ret;
    }

    ret = ksceOledSetBrightness((unsigned int)current);
    if (ret < 0) {
        release_transaction();
        g_vbe_status.brightness_core = VBE_CAP_FAILED;
        status_set_error(VBE_ERR_BACKEND, ret);
        return ret;
    }

    lut_copy(lookupNew, candidate);
    if (supplied == NULL) path_copy(g_lut_source_path, source_path);
    g_panel_type = panel_type;
    g_vbe_status.panel_type = panel_type;
    g_active = 1;
    g_vbe_status.brightness_core = VBE_CAP_ACTIVE;
    return 0;
}

static int replace_candidate(const unsigned char candidate[LUT_SIZE]) {
    if (!lut_is_sane(candidate)) {
        status_set_error(VBE_ERR_INVALID_USER_INPUT, -1);
        return -1;
    }

    unsigned char previous[LUT_SIZE];
    int had_previous = g_active;
    if (had_previous) lut_copy(previous, lookupNew);

    release_transaction();
    int ret = start_transaction(candidate);
    if (ret >= 0) return ret;

    int original_error = g_vbe_status.last_error;
    int original_detail = g_vbe_status.last_error_detail;
    if (had_previous) {
        int rollback = start_transaction(previous);
        if (rollback < 0) {
            status_set_error(VBE_ERR_LUT_ROLLBACK, rollback);
            return ret;
        }
        status_set_error(original_error, original_detail);
    }
    return ret;
}

int oled_enable_hooks(void) {
    return start_transaction(NULL);
}

void oled_disable_hooks(void) {
    release_transaction();
    ksceOledGetBrightness = NULL;
    ksceOledSetBrightness = NULL;
    ksceOledGetDDB = NULL;
}

int oled_reload_backend(void) {
    if (!firmware_layout_supported(sw_version)) {
        g_vbe_status.firmware_layout = VBE_CAP_UNSUPPORTED;
        status_set_error(VBE_ERR_FIRMWARE_UNSUPPORTED, (int)sw_version);
        return -1;
    }

    tai_module_info_t info;
    int ret = 0;
    if (ksceOledGetDDB == NULL || ksceOledGetBrightness == NULL ||
        ksceOledSetBrightness == NULL) {
        ret = resolve_core(&info);
        if (ret < 0) {
            g_vbe_status.brightness_core = VBE_CAP_FAILED;
            status_set_error(VBE_ERR_EXPORT_RESOLUTION, ret);
            return ret;
        }
    }

    int panel_type = OLED_PANEL_UNKNOWN;
    uint32_t offset = 0;
    ret = read_panel(&panel_type, &offset);
    if (ret < 0) {
        g_vbe_status.firmware_layout = VBE_CAP_FAILED;
        status_set_error(VBE_ERR_LAYOUT_MISMATCH, ret);
        return ret;
    }

    VitaBrightConfig previous_config = g_config;
    ret = config_load();
    if (ret < 0) {
        g_config = previous_config;
        status_set_error(VBE_ERR_CONFIG, ret);
        return ret;
    }

    unsigned char candidate[LUT_SIZE];
    char candidate_path[LUT_SOURCE_PATH_MAX];
    ret = load_disk_candidate(panel_type, candidate, candidate_path);
    if (ret < 0) {
        g_config = previous_config;
        status_set_error(VBE_ERR_INVALID_USER_INPUT, ret);
        return ret;
    }

    ret = replace_candidate(candidate);
    if (ret < 0) {
        g_config = previous_config;
        return ret;
    }

    path_copy(g_lut_source_path, candidate_path);
    return 0;
}

int oled_reinject_lut(void) {
    unsigned char candidate[LUT_SIZE];
    lut_copy(candidate, lookupNew);
    return replace_candidate(candidate);
}

static int build_temp_path(char out[LUT_SOURCE_PATH_MAX], const char *path) {
    int i = 0;
    while (i < LUT_SOURCE_PATH_MAX - 5 && path[i]) {
        out[i] = path[i];
        ++i;
    }
    if (path[i] != '\0') return -1;
    out[i++] = '.';
    out[i++] = 't';
    out[i++] = 'm';
    out[i++] = 'p';
    out[i] = '\0';
    return 0;
}

static int persist_lut_locked(void) {
    if (!g_active || g_lut_source_path[0] == '\0') return -1;

    char temp_path[LUT_SOURCE_PATH_MAX];
    if (build_temp_path(temp_path, g_lut_source_path) < 0) return -1;

    (void)ksceIoRemove(temp_path);
    SceUID fd = ksceIoOpen(temp_path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) return fd;

    static const char hex[] = "0123456789ABCDEF";
    int ret = 0;
    for (int row = 0; row < LUT_ROWS; ++row) {
        char line[LUT_LINE_SIZE * 3];
        int pos = 0;
        for (int col = 0; col < LUT_LINE_SIZE; ++col) {
            unsigned char value = lookupNew[row * LUT_LINE_SIZE + col];
            line[pos++] = hex[value >> 4];
            line[pos++] = hex[value & 0x0F];
            line[pos++] = col == LUT_LINE_SIZE - 1 ? '\n' : ' ';
        }
        int written = ksceIoWrite(fd, line, (SceSize)pos);
        if (written != pos) {
            ret = written < 0 ? written : -1;
            break;
        }
    }

    if (ret == 0) {
        int sync_status = 0;
        int sync_ret = ksceIoSyncByFd(fd, &sync_status);
        if (sync_ret < 0 || sync_status < 0)
            ret = sync_ret < 0 ? sync_ret : sync_status;
    }

    int close_ret = ksceIoClose(fd);
    if (ret == 0 && close_ret < 0) ret = close_ret;

    if (ret == 0) {
        ret = ksceIoRename(temp_path, g_lut_source_path);
    }
    if (ret < 0) (void)ksceIoRemove(temp_path);
    return ret;
}

int vitabrightOledPersistLut(void) {
    int state;
    ENTER_SYSCALL(state);
    int ret = state_lock_acquire();
    if (ret < 0) { EXIT_SYSCALL(state); return ret; }

    if (!g_is_oled || !g_active) {
        state_lock_release();
        EXIT_SYSCALL(state);
        return -1;
    }

    ret = persist_lut_locked();
    if (ret < 0) status_set_error(VBE_ERR_BACKEND, ret);
    else status_clear_error();

    state_lock_release();
    EXIT_SYSCALL(state);
    return ret;
}

int vitabrightOledGetLevel(void) {
    int state;
    ENTER_SYSCALL(state);
    int ret = state_lock_acquire();
    if (ret < 0) { EXIT_SYSCALL(state); return ret; }

    if (!g_is_oled || !g_active || ksceOledGetBrightness == NULL) {
        state_lock_release();
        EXIT_SYSCALL(state);
        return -1;
    }

    int brightness = ksceOledGetBrightness();
    if (brightness < 0) {
        state_lock_release();
        EXIT_SYSCALL(state);
        return brightness;
    }

    int level;
    if (brightness == 0) level = -1;
    else if (brightness == 1) level = 16;
    else if (brightness <= 0xFFF) level = 15;
    else if (brightness >= 0x10000) level = 0;
    else {
        level = 15 - (brightness / 0x1000);
        if (level < 1) level = 1;
        if (level > 14) level = 14;
    }

    state_lock_release();
    EXIT_SYSCALL(state);
    return level;
}

int vitabrightOledSetLevel(unsigned int level) {
    int state;
    ENTER_SYSCALL(state);
    int ret = state_lock_acquire();
    if (ret < 0) { EXIT_SYSCALL(state); return ret; }

    if (!g_is_oled || !g_active || ksceOledSetBrightness == NULL) {
        state_lock_release();
        EXIT_SYSCALL(state);
        return -1;
    }
    if (level > 16u) {
        status_set_error(VBE_ERR_INVALID_USER_INPUT, (int)level);
        state_lock_release();
        EXIT_SYSCALL(state);
        return -1;
    }

    unsigned int brightness;
    if (level == 16u) brightness = 1;
    else if (level == 15u) brightness = 0xFFF;
    else if (level == 0u) brightness = 0x10000;
    else brightness = (unsigned int)(0x1000 * (15 - (int)level));

    g_dim_workaround_enabled = 0;
    ret = ksceOledSetBrightness(brightness);
    g_dim_workaround_enabled = 1;
    if (ret >= 0) status_clear_error();

    state_lock_release();
    EXIT_SYSCALL(state);
    return ret < 0 ? ret : (int)level;
}

int vitabrightOledGetLut(unsigned char oledLut[LUT_SIZE]) {
    int state;
    ENTER_SYSCALL(state);
    int ret = state_lock_acquire();
    if (ret < 0) { EXIT_SYSCALL(state); return ret; }

    if (!g_is_oled || !g_active) {
        state_lock_release();
        EXIT_SYSCALL(state);
        return -1;
    }

    unsigned char snapshot[LUT_SIZE];
    lut_copy(snapshot, lookupNew);
    state_lock_release();
    ret = ksceKernelMemcpyKernelToUser((void *)oledLut, snapshot, LUT_SIZE);
    EXIT_SYSCALL(state);
    return ret;
}

int vitabrightOledSetLut(unsigned char oledLut[LUT_SIZE]) {
    int state;
    unsigned char candidate[LUT_SIZE];
    ENTER_SYSCALL(state);

    int ret = ksceKernelMemcpyUserToKernel(candidate, (const void *)oledLut, LUT_SIZE);
    if (ret < 0 || !lut_is_sane(candidate)) {
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
    if (!g_is_oled || !g_active) {
        state_lock_release();
        EXIT_SYSCALL(state);
        return -1;
    }

    ret = replace_candidate(candidate);
    if (ret == 0) status_clear_error();
    state_lock_release();
    EXIT_SYSCALL(state);
    return ret;
}

int vitabrightOledReload(void) {
    int state;
    ENTER_SYSCALL(state);
    int ret = state_lock_acquire();
    if (ret < 0) { EXIT_SYSCALL(state); return ret; }
    if (!g_is_oled) {
        state_lock_release();
        EXIT_SYSCALL(state);
        return -1;
    }

    int result = oled_reload_backend();
    int color_ret = color_space_apply_config();
    if (result >= 0 && color_ret < 0) result = color_ret;

    screen_filter_load_config();
    int filter_ret = screen_filter_apply(1);
    if (result >= 0 && filter_ret < 0) result = filter_ret;

    if (result >= 0) status_clear_error();
    state_lock_release();
    EXIT_SYSCALL(state);
    return result;
}

int vitabrightOledGetPanelType(void) {
    int state;
    ENTER_SYSCALL(state);
    int ret = state_lock_acquire();
    if (ret < 0) { EXIT_SYSCALL(state); return ret; }
    int panel = g_is_oled ? g_panel_type : OLED_PANEL_UNKNOWN;
    state_lock_release();
    EXIT_SYSCALL(state);
    return panel;
}
