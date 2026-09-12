#include "hooks.h"
#include "../config.h"
#include "../log.h"
#include "../main.h"
#include "../persistence_core.h"
#include "../persistence_file.h"
#include "../source_authority.h"
#include "../state_lock.h"
#include "../status.h"
#include "../taihen_extra.h"
#include "../transaction_core.h"
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

typedef struct {
    unsigned char values[LUT_SIZE];
    VbeSourceIdentity source;
    int panel_type;
} OledCandidate;

typedef struct {
    int ownership;
    SceUID lut_inject;
    SceUID brightness_hook;
    SceUID power_hook;
    tai_hook_ref_t brightness_ref;
    tai_hook_ref_t power_ref;
    int panel_type;
    int dim_workaround_enabled;
    VbeSourceIdentity source;
    VbePersistenceFile persistence;
} OledBackend;

static OledBackend g_oled = {
    .ownership = VBE_OWNERSHIP_CLEAN,
    .lut_inject = -1,
    .brightness_hook = -1,
    .power_hook = -1,
    .brightness_ref = -1,
    .power_ref = 0,
    .panel_type = OLED_PANEL_UNKNOWN,
    .dim_workaround_enabled = 1,
    .source = { .kind = VBE_SOURCE_ID_NONE, .path = {0} },
    .persistence = { .fd = -1, .temp_owned = 0, .target = {0}, .temp = {0} },
};

unsigned char lookupNew[LUT_SIZE];

static int (*g_get_brightness)(void) = NULL;
static int (*g_set_brightness)(unsigned int brightness) = NULL;
static int (*g_get_ddb)(uint16_t *supplier_id, uint16_t *supplier_elective_data) = NULL;

static void brightness_error(int error, int detail) {
    status_set_error_domain(VBE_ERROR_DOMAIN_BRIGHTNESS, error, detail);
}

static void brightness_ok(void) {
    status_clear_error_domain(VBE_ERROR_DOMAIN_BRIGHTNESS);
}

static void brightness_clear_if(int error) {
    int current = VBE_ERR_NONE;
    int detail = 0;
    status_get_error_domain(VBE_ERROR_DOMAIN_BRIGHTNESS, &current, &detail);
    (void)detail;
    if (current == error) brightness_ok();
}

static void lut_copy(unsigned char *dst, const unsigned char *src) {
    for (int i = 0; i < LUT_SIZE; ++i) dst[i] = src[i];
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

static void publish_active(void) {
    g_vbe_status.brightness_core = VBE_CAP_ACTIVE;
    g_vbe_status.brightness_table = VBE_CAP_ACTIVE;
    g_vbe_status.brightness_hook = VBE_CAP_ACTIVE;
    g_vbe_status.power_limit_hook = VBE_CAP_ACTIVE;
}

static void publish_inactive(void) {
    if (g_vbe_status.brightness_core == VBE_CAP_ACTIVE)
        g_vbe_status.brightness_core = VBE_CAP_INACTIVE;
    if (g_vbe_status.brightness_table == VBE_CAP_ACTIVE)
        g_vbe_status.brightness_table = VBE_CAP_INACTIVE;
    if (g_vbe_status.brightness_hook == VBE_CAP_ACTIVE)
        g_vbe_status.brightness_hook = VBE_CAP_INACTIVE;
    if (g_vbe_status.power_limit_hook == VBE_CAP_ACTIVE)
        g_vbe_status.power_limit_hook = VBE_CAP_INACTIVE;
}

static void publish_failed(void) {
    g_vbe_status.brightness_core = VBE_CAP_FAILED;
    g_vbe_status.brightness_table = VBE_CAP_FAILED;
    g_vbe_status.brightness_hook = VBE_CAP_FAILED;
    g_vbe_status.power_limit_hook = VBE_CAP_FAILED;
}

static int resolve_core(tai_module_info_t *info) {
    info->size = sizeof(*info);
    int ret = taiGetModuleInfoForKernel(KERNEL_PID, "SceOled", info);
    if (ret < 0) return ret;

    g_get_brightness = NULL;
    g_set_brightness = NULL;
    g_get_ddb = NULL;

    ret = module_get_export_func(KERNEL_PID, "SceOled", TAI_ANY_LIBRARY,
        NID_OLED_GET_BRIGHTNESS, (uintptr_t *)&g_get_brightness);
    if (ret < 0 || g_get_brightness == NULL) return ret < 0 ? ret : -1;

    ret = module_get_export_func(KERNEL_PID, "SceOled", TAI_ANY_LIBRARY,
        NID_OLED_SET_BRIGHTNESS, (uintptr_t *)&g_set_brightness);
    if (ret < 0 || g_set_brightness == NULL) return ret < 0 ? ret : -1;

    ret = module_get_export_func(KERNEL_PID, "SceOled", TAI_ANY_LIBRARY,
        NID_OLED_GET_DDB, (uintptr_t *)&g_get_ddb);
    if (ret < 0 || g_get_ddb == NULL) return ret < 0 ? ret : -1;
    return 0;
}

static int read_panel(int *panel_type, uint32_t *lut_offset) {
    uint16_t supplier_id = 0;
    uint16_t sed = 0;
    if (g_get_ddb == NULL) return -1;

    int ret = g_get_ddb(&supplier_id, &sed);
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

static int load_disk_candidate(int panel_type, OledCandidate *candidate,
                               int *error_code) {
    char path[LUT_SOURCE_PATH_MAX];
    int ret;
    path[0] = '\0';
    if (g_config.oled_panel_lut_override && g_config.panel_lut_path[0] != '\0')
        ret = parse_lut_override(g_config.panel_lut_path, candidate->values,
                                 path, error_code);
    else
        ret = parse_lut(panel_type, candidate->values, path, error_code);

    if (ret < 0) return ret;
    if (!lut_is_sane(candidate->values)) {
        if (error_code != NULL) *error_code = VBE_ERR_INVALID_USER_INPUT;
        return -1;
    }
    if (vbe_source_identity_file(&candidate->source, path) < 0) {
        if (error_code != NULL) *error_code = VBE_ERR_SOURCE_IO;
        return -1;
    }
    candidate->panel_type = panel_type;
    return 0;
}

static int prepare_disk_candidate(OledCandidate *candidate, int *error_code) {
    if (!firmware_layout_supported(sw_version)) {
        *error_code = VBE_ERR_FIRMWARE_UNSUPPORTED;
        return -1;
    }

    tai_module_info_t info;
    int ret = resolve_core(&info);
    if (ret < 0) {
        *error_code = VBE_ERR_EXPORT_RESOLUTION;
        return ret;
    }

    int panel_type = OLED_PANEL_UNKNOWN;
    uint32_t offset = 0;
    ret = read_panel(&panel_type, &offset);
    if (ret < 0) {
        *error_code = VBE_ERR_LAYOUT_MISMATCH;
        return ret;
    }
    (void)offset;
    return load_disk_candidate(panel_type, candidate, error_code);
}

int hook_ksceOledSetBrightness(unsigned int brightness) {
    if (brightness == 1 && g_oled.dim_workaround_enabled &&
        g_config.oled_dim_workaround && g_get_brightness != NULL) {
        int old_brightness = g_get_brightness();
        if (old_brightness >= 0 && old_brightness <= 4 * 0x1000)
            return TAI_CONTINUE(int, g_oled.brightness_ref,
                                (unsigned int)old_brightness);
    }
    return TAI_CONTINUE(int, g_oled.brightness_ref, brightness);
}

int hook_kscePowerSetDisplayMaxBrightnessForOled(int limit) {
    if (g_oled.power_ref == 0) return 0;
    if (limit < 0x10000 && limit >= 0)
        limit = 0x10000 - 2 * 0x1000;
    else
        limit = 0x10000;
    return TAI_CONTINUE(int, g_oled.power_ref, limit);
}

static VbeTxnAttempt release_resources(void) {
    int ret;

    if (g_oled.ownership == VBE_OWNERSHIP_CLEAN)
        return vbe_txn_ok();

    if (g_oled.power_hook >= 0) {
        ret = taiHookReleaseForKernel(g_oled.power_hook, g_oled.power_ref);
        if (ret < 0) {
            g_oled.ownership = VBE_OWNERSHIP_DEGRADED;
            publish_failed();
            brightness_error(VBE_ERR_RESOURCE_RELEASE, ret);
            return vbe_txn_failed_dirty(VBE_ERR_RESOURCE_RELEASE, ret);
        }
        g_oled.power_hook = -1;
        g_oled.power_ref = 0;
    }

    if (g_oled.brightness_hook >= 0) {
        ret = taiHookReleaseForKernel(g_oled.brightness_hook, g_oled.brightness_ref);
        if (ret < 0) {
            g_oled.ownership = VBE_OWNERSHIP_DEGRADED;
            publish_failed();
            brightness_error(VBE_ERR_RESOURCE_RELEASE, ret);
            return vbe_txn_failed_dirty(VBE_ERR_RESOURCE_RELEASE, ret);
        }
        g_oled.brightness_hook = -1;
        g_oled.brightness_ref = -1;
    }

    if (g_oled.lut_inject >= 0) {
        ret = taiInjectReleaseForKernel(g_oled.lut_inject);
        if (ret < 0) {
            g_oled.ownership = VBE_OWNERSHIP_DEGRADED;
            publish_failed();
            brightness_error(VBE_ERR_RESOURCE_RELEASE, ret);
            return vbe_txn_failed_dirty(VBE_ERR_RESOURCE_RELEASE, ret);
        }
        g_oled.lut_inject = -1;
    }

    g_oled.ownership = VBE_OWNERSHIP_CLEAN;
    publish_inactive();
    return vbe_txn_ok();
}

static VbeTxnAttempt abort_start(int error, int detail) {
    VbeTxnAttempt cleanup = release_resources();
    if (cleanup.state != VBE_TXN_OK) return cleanup;
    publish_failed();
    brightness_error(error, detail);
    return vbe_txn_failed_clean(error, detail);
}

static VbeTxnAttempt start_transaction(const OledCandidate *candidate) {
    if (!vbe_txn_can_start(g_oled.ownership)) {
        publish_failed();
        brightness_error(VBE_ERR_RESOURCE_RELEASE, -1);
        return vbe_txn_failed_dirty(VBE_ERR_RESOURCE_RELEASE, -1);
    }
    if (!firmware_layout_supported(sw_version)) {
        g_vbe_status.firmware_layout = VBE_CAP_UNSUPPORTED;
        brightness_error(VBE_ERR_FIRMWARE_UNSUPPORTED, (int)sw_version);
        return vbe_txn_failed_clean(VBE_ERR_FIRMWARE_UNSUPPORTED, -1);
    }
    if (!lut_is_sane(candidate->values)) {
        publish_failed();
        brightness_error(VBE_ERR_INVALID_USER_INPUT, -1);
        return vbe_txn_failed_clean(VBE_ERR_INVALID_USER_INPUT, -1);
    }

    tai_module_info_t info;
    int ret = resolve_core(&info);
    if (ret < 0) {
        publish_failed();
        brightness_error(VBE_ERR_EXPORT_RESOLUTION, ret);
        return vbe_txn_failed_clean(VBE_ERR_EXPORT_RESOLUTION, ret);
    }

    int panel_type = OLED_PANEL_UNKNOWN;
    uint32_t lut_offset = 0;
    ret = read_panel(&panel_type, &lut_offset);
    if (ret < 0) {
        g_vbe_status.firmware_layout = VBE_CAP_FAILED;
        publish_failed();
        brightness_error(VBE_ERR_LAYOUT_MISMATCH, ret);
        return vbe_txn_failed_clean(VBE_ERR_LAYOUT_MISMATCH, ret);
    }
    if (candidate->panel_type != OLED_PANEL_UNKNOWN &&
        candidate->panel_type != panel_type) {
        g_vbe_status.firmware_layout = VBE_CAP_FAILED;
        publish_failed();
        brightness_error(VBE_ERR_LAYOUT_MISMATCH, -1);
        return vbe_txn_failed_clean(VBE_ERR_LAYOUT_MISMATCH, -1);
    }

    ret = validate_layout(&info, lut_offset);
    if (ret < 0) {
        g_vbe_status.firmware_layout = VBE_CAP_FAILED;
        publish_failed();
        brightness_error(VBE_ERR_LAYOUT_MISMATCH, ret);
        return vbe_txn_failed_clean(VBE_ERR_LAYOUT_MISMATCH, ret);
    }
    g_vbe_status.firmware_layout = VBE_CAP_ACTIVE;

    g_oled.lut_inject = taiInjectDataForKernel(KERNEL_PID, info.modid, 0,
        lut_offset, candidate->values, LUT_SIZE);
    if (g_oled.lut_inject < 0) {
        ret = (int)g_oled.lut_inject;
        g_oled.lut_inject = -1;
        publish_failed();
        brightness_error(VBE_ERR_TABLE_INJECTION, ret);
        return vbe_txn_failed_clean(VBE_ERR_TABLE_INJECTION, ret);
    }
    g_oled.ownership = VBE_OWNERSHIP_DEGRADED;

    g_oled.brightness_hook = taiHookFunctionExportForKernel(KERNEL_PID,
        &g_oled.brightness_ref, "SceOled", TAI_ANY_LIBRARY,
        NID_OLED_SET_BRIGHTNESS, hook_ksceOledSetBrightness);
    if (g_oled.brightness_hook < 0) {
        ret = (int)g_oled.brightness_hook;
        g_oled.brightness_hook = -1;
        return abort_start(VBE_ERR_BRIGHTNESS_HOOK, ret);
    }

    g_oled.power_hook = taiHookFunctionExportForKernel(KERNEL_PID,
        &g_oled.power_ref, "ScePower", TAI_ANY_LIBRARY,
        NID_POWER_SET_MAX_BRIGHT, hook_kscePowerSetDisplayMaxBrightnessForOled);
    if (g_oled.power_hook < 0) {
        ret = (int)g_oled.power_hook;
        g_oled.power_hook = -1;
        return abort_start(VBE_ERR_POWER_HOOK, ret);
    }

    int current = g_get_brightness();
    if (current < 0) return abort_start(VBE_ERR_BACKEND, current);

    ret = g_set_brightness((unsigned int)current);
    if (ret < 0) return abort_start(VBE_ERR_BACKEND, ret);

    lut_copy(lookupNew, candidate->values);
    vbe_source_identity_copy(&g_oled.source, &candidate->source);
    g_oled.panel_type = panel_type;
    g_vbe_status.panel_type = panel_type;
    g_oled.ownership = VBE_OWNERSHIP_ACTIVE;
    publish_active();
    brightness_ok();
    return vbe_txn_ok();
}

static int replace_candidate(const OledCandidate *candidate) {
    OledCandidate previous;
    int had_previous = g_oled.ownership == VBE_OWNERSHIP_ACTIVE;
    if (had_previous) {
        lut_copy(previous.values, lookupNew);
        vbe_source_identity_copy(&previous.source, &g_oled.source);
        previous.panel_type = g_oled.panel_type;
    }

    if (g_oled.ownership == VBE_OWNERSHIP_DEGRADED) {
        brightness_error(VBE_ERR_RESOURCE_RELEASE, -1);
        return -1;
    }

    if (g_oled.ownership == VBE_OWNERSHIP_ACTIVE) {
        VbeTxnAttempt release = release_resources();
        if (release.state != VBE_TXN_OK) return release.detail;
    }

    VbeTxnAttempt requested = start_transaction(candidate);
    if (requested.state == VBE_TXN_OK) return 0;
    if (!vbe_txn_should_rollback(had_previous, g_oled.ownership, requested))
        return vbe_txn_public_result(requested, 0, vbe_txn_ok());

    VbeTxnAttempt rollback = start_transaction(&previous);
    if (rollback.state == VBE_TXN_OK) {
        brightness_error(requested.error, requested.detail);
    } else if (rollback.state == VBE_TXN_FAILED_CLEAN) {
        publish_failed();
        brightness_error(VBE_ERR_LUT_ROLLBACK, rollback.detail);
    } else {
        publish_failed();
        brightness_error(VBE_ERR_RESOURCE_RELEASE, rollback.detail);
    }

    if (rollback.state != VBE_TXN_OK)
        LOG("[OLED] requested replacement failed 0x%08X; recovery failed 0x%08X state=%d\n",
            requested.detail, rollback.detail, rollback.state);
    return vbe_txn_public_result(requested, 1, rollback);
}

int oled_enable_hooks(void) {
    if (g_oled.ownership == VBE_OWNERSHIP_ACTIVE) return 0;
    if (!vbe_txn_can_start(g_oled.ownership)) return -1;

    OledCandidate candidate;
    vbe_source_identity_clear(&candidate.source);
    candidate.panel_type = OLED_PANEL_UNKNOWN;
    int error_code = VBE_ERR_NONE;
    int ret = prepare_disk_candidate(&candidate, &error_code);
    if (ret < 0) {
        brightness_error(error_code, ret);
        return ret;
    }

    VbeTxnAttempt start = start_transaction(&candidate);
    return start.state == VBE_TXN_OK ? 0 : start.detail;
}

int oled_detect_panel(void) {
    int panel = OLED_PANEL_UNKNOWN;
    uint32_t offset = 0;
    if (g_get_ddb == NULL) return OLED_PANEL_UNKNOWN;
    if (read_panel(&panel, &offset) < 0) return OLED_PANEL_UNKNOWN;
    return panel;
}

static int persistence_error(const VbePersistenceOutcome *out) {
    if (out->cleanup_error < 0) return VBE_ERR_PERSISTENCE_CLEANUP;
    if (out->stage == VBE_PERSIST_STAGE_RENAME) return VBE_ERR_PERSISTENCE_COMMIT;
    return VBE_ERR_PERSISTENCE_PREPARE;
}

static int oled_persist_prepare(void *context) {
    OledBackend *backend = (OledBackend *)context;
    return vbe_persist_file_prepare(&backend->persistence);
}
static int oled_persist_open(void *context) {
    OledBackend *backend = (OledBackend *)context;
    return vbe_persist_file_open(&backend->persistence);
}
static int oled_persist_sync(void *context) {
    OledBackend *backend = (OledBackend *)context;
    return vbe_persist_file_sync(&backend->persistence);
}
static int oled_persist_close(void *context) {
    OledBackend *backend = (OledBackend *)context;
    return vbe_persist_file_close(&backend->persistence);
}
static int oled_persist_rename(void *context) {
    OledBackend *backend = (OledBackend *)context;
    return vbe_persist_file_rename(&backend->persistence);
}
static int oled_persist_cleanup(void *context) {
    OledBackend *backend = (OledBackend *)context;
    return vbe_persist_file_cleanup(&backend->persistence);
}

static int oled_persist_write(void *context) {
    OledBackend *backend = (OledBackend *)context;
    if (backend->persistence.fd < 0) return -1;
    static const char hex[] = "0123456789ABCDEF";
    for (int row = 0; row < LUT_ROWS; ++row) {
        char line[LUT_LINE_SIZE * 3];
        int pos = 0;
        for (int col = 0; col < LUT_LINE_SIZE; ++col) {
            unsigned char value = lookupNew[row * LUT_LINE_SIZE + col];
            line[pos++] = hex[value >> 4];
            line[pos++] = hex[value & 0x0F];
            line[pos++] = col == LUT_LINE_SIZE - 1 ? '\n' : ' ';
        }
        int written = ksceIoWrite(backend->persistence.fd, line, (SceSize)pos);
        if (written != pos) return written < 0 ? written : -1;
    }
    return 0;
}

static int persist_lut_locked(void) {
    if (g_oled.ownership != VBE_OWNERSHIP_ACTIVE ||
        !vbe_source_identity_is_file(&g_oled.source))
        return -1;

    int cleanup = vbe_persist_file_cleanup(&g_oled.persistence);
    if (cleanup < 0) {
        brightness_error(VBE_ERR_PERSISTENCE_CLEANUP, cleanup);
        return cleanup;
    }
    if (vbe_persist_file_set_target(&g_oled.persistence, g_oled.source.path) < 0) {
        brightness_error(VBE_ERR_PERSISTENCE_PREPARE, -1);
        return -1;
    }

    VbePersistenceOps ops = {
        .context = &g_oled,
        .prepare = oled_persist_prepare,
        .open_temp = oled_persist_open,
        .write_payload = oled_persist_write,
        .sync_temp = oled_persist_sync,
        .close_temp = oled_persist_close,
        .rename_temp = oled_persist_rename,
        .cleanup_temp = oled_persist_cleanup,
    };
    VbePersistenceOutcome out = vbe_persistence_execute(&ops);
    if (!out.committed) {
        int detail = out.cleanup_error < 0 ? out.cleanup_error : out.error;
        brightness_error(persistence_error(&out), detail);
        LOG("[OLED:PERSIST] stage=%d error=0x%08X cleanup=%d/0x%08X\n",
            out.stage, out.error, out.cleanup_stage, out.cleanup_error);
        return detail < 0 ? detail : -1;
    }

    int current_error = VBE_ERR_NONE;
    int current_detail = 0;
    status_get_error_domain(VBE_ERROR_DOMAIN_BRIGHTNESS,
                            &current_error, &current_detail);
    (void)current_detail;
    if (current_error == VBE_ERR_PERSISTENCE_PREPARE ||
        current_error == VBE_ERR_PERSISTENCE_COMMIT ||
        current_error == VBE_ERR_PERSISTENCE_CLEANUP)
        brightness_ok();
    return 0;
}

int oled_disable_hooks(void) {
    int first_error = 0;
    int persist = vbe_persist_file_cleanup(&g_oled.persistence);
    if (persist < 0) {
        brightness_error(VBE_ERR_PERSISTENCE_CLEANUP, persist);
        first_error = persist;
    }

    VbeTxnAttempt release = release_resources();
    if (release.state != VBE_TXN_OK) first_error = release.detail;

    if (release.state == VBE_TXN_OK) {
        g_get_brightness = NULL;
        g_set_brightness = NULL;
        g_get_ddb = NULL;
    }
    if (first_error == 0) brightness_ok();
    return first_error;
}

int oled_reload_backend(void) {
    if (g_oled.ownership == VBE_OWNERSHIP_DEGRADED) return -1;

    OledCandidate candidate;
    vbe_source_identity_clear(&candidate.source);
    candidate.panel_type = OLED_PANEL_UNKNOWN;
    int error_code = VBE_ERR_NONE;
    int ret = prepare_disk_candidate(&candidate, &error_code);
    if (ret < 0) {
        brightness_error(error_code, ret);
        return ret;
    }
    return replace_candidate(&candidate);
}

int oled_reinject_lut(void) {
    if (g_oled.ownership != VBE_OWNERSHIP_ACTIVE) return -1;
    OledCandidate candidate;
    lut_copy(candidate.values, lookupNew);
    vbe_source_identity_copy(&candidate.source, &g_oled.source);
    candidate.panel_type = g_oled.panel_type;
    return replace_candidate(&candidate);
}

int vitabrightOledPersistLut(void) {
    int state;
    ENTER_SYSCALL(state);
    int ret = state_lock_acquire();
    if (ret < 0) { EXIT_SYSCALL(state); return ret; }

    if (!g_is_oled || g_oled.ownership != VBE_OWNERSHIP_ACTIVE) {
        (void)state_lock_release();
        EXIT_SYSCALL(state);
        return -1;
    }

    ret = persist_lut_locked();
    (void)state_lock_release();
    EXIT_SYSCALL(state);
    return ret;
}

int vitabrightOledGetLevel(void) {
    int state;
    ENTER_SYSCALL(state);
    int ret = state_lock_acquire();
    if (ret < 0) { EXIT_SYSCALL(state); return ret; }

    if (!g_is_oled || g_oled.ownership != VBE_OWNERSHIP_ACTIVE ||
        g_get_brightness == NULL) {
        (void)state_lock_release();
        EXIT_SYSCALL(state);
        return -1;
    }

    int brightness = g_get_brightness();
    if (brightness < 0) {
        brightness_error(VBE_ERR_BACKEND, brightness);
        (void)state_lock_release();
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

    (void)state_lock_release();
    EXIT_SYSCALL(state);
    return level;
}

int vitabrightOledSetLevel(unsigned int level) {
    int state;
    ENTER_SYSCALL(state);
    int ret = state_lock_acquire();
    if (ret < 0) { EXIT_SYSCALL(state); return ret; }

    if (!g_is_oled || g_oled.ownership != VBE_OWNERSHIP_ACTIVE ||
        g_set_brightness == NULL) {
        (void)state_lock_release();
        EXIT_SYSCALL(state);
        return -1;
    }
    if (level > 16u) {
        status_stage_result(VBE_ERROR_DOMAIN_INPUT, 0,
                            VBE_ERR_INVALID_USER_INPUT, (int)level);
        (void)state_lock_release();
        EXIT_SYSCALL(state);
        return -1;
    }
    status_stage_result(VBE_ERROR_DOMAIN_INPUT, 1,
                        VBE_ERR_INVALID_USER_INPUT, 0);

    unsigned int brightness;
    if (level == 16u) brightness = 1;
    else if (level == 15u) brightness = 0xFFF;
    else if (level == 0u) brightness = 0x10000;
    else brightness = (unsigned int)(0x1000 * (15 - (int)level));

    g_oled.dim_workaround_enabled = 0;
    ret = g_set_brightness(brightness);
    g_oled.dim_workaround_enabled = 1;
    if (ret < 0) brightness_error(VBE_ERR_BACKEND, ret);
    else brightness_clear_if(VBE_ERR_BACKEND);

    (void)state_lock_release();
    EXIT_SYSCALL(state);
    return ret < 0 ? ret : (int)level;
}

int vitabrightOledGetLut(unsigned char oledLut[LUT_SIZE]) {
    int state;
    ENTER_SYSCALL(state);
    int ret = state_lock_acquire();
    if (ret < 0) { EXIT_SYSCALL(state); return ret; }

    if (!g_is_oled || g_oled.ownership != VBE_OWNERSHIP_ACTIVE) {
        (void)state_lock_release();
        EXIT_SYSCALL(state);
        return -1;
    }

    unsigned char snapshot[LUT_SIZE];
    lut_copy(snapshot, lookupNew);
    (void)state_lock_release();
    ret = ksceKernelMemcpyKernelToUser((void *)oledLut, snapshot, LUT_SIZE);
    EXIT_SYSCALL(state);
    return ret;
}

int vitabrightOledSetLut(unsigned char oledLut[LUT_SIZE]) {
    int state;
    OledCandidate candidate;
    ENTER_SYSCALL(state);

    int ret = ksceKernelMemcpyUserToKernel(candidate.values,
                                           (const void *)oledLut, LUT_SIZE);
    if (ret < 0 || !lut_is_sane(candidate.values)) {
        int detail = ret < 0 ? ret : -1;
        if (state_lock_acquire() >= 0) {
            status_stage_result(VBE_ERROR_DOMAIN_INPUT, 0,
                                VBE_ERR_INVALID_USER_INPUT, detail);
            (void)state_lock_release();
        }
        EXIT_SYSCALL(state);
        return detail;
    }

    ret = state_lock_acquire();
    if (ret < 0) { EXIT_SYSCALL(state); return ret; }
    status_stage_result(VBE_ERROR_DOMAIN_INPUT, 1,
                        VBE_ERR_INVALID_USER_INPUT, 0);
    if (!g_is_oled || g_oled.ownership != VBE_OWNERSHIP_ACTIVE) {
        (void)state_lock_release();
        EXIT_SYSCALL(state);
        return -1;
    }

    vbe_source_identity_copy(&candidate.source, &g_oled.source);
    candidate.panel_type = g_oled.panel_type;
    ret = replace_candidate(&candidate);
    (void)state_lock_release();
    EXIT_SYSCALL(state);
    return ret;
}

int vitabrightOledReload(void) {
    int state;
    ENTER_SYSCALL(state);
    int ret = state_lock_acquire();
    if (ret < 0) { EXIT_SYSCALL(state); return ret; }
    if (!g_is_oled) {
        (void)state_lock_release();
        EXIT_SYSCALL(state);
        return -1;
    }

    ret = vitabright_reload_locked();
    (void)state_lock_release();
    EXIT_SYSCALL(state);
    return ret;
}

int vitabrightOledGetPanelType(void) {
    int state;
    ENTER_SYSCALL(state);
    int ret = state_lock_acquire();
    if (ret < 0) { EXIT_SYSCALL(state); return ret; }
    int panel = g_is_oled ? g_oled.panel_type : OLED_PANEL_UNKNOWN;
    (void)state_lock_release();
    EXIT_SYSCALL(state);
    return panel;
}
