#include "hooks.h"
#include "lcd_lut.h"
#include "../color_space.h"
#include "../log.h"
#include "../lut_parser_core.h"
#include "../main.h"
#include "../persistence_core.h"
#include "../persistence_file.h"
#include "../source_authority.h"
#include "../state_lock.h"
#include "../status.h"
#include "../taihen_extra.h"
#include "../transaction_core.h"
#include <stdint.h>
#include <psp2kern/io/fcntl.h>
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysmem.h>
#include <taihen.h>

#define NID_LCD_GET_BRIGHTNESS   0x3A6D6AC3
#define NID_LCD_SET_BRIGHTNESS   0x581D3A87
#define NID_POWER_SET_MAX_BRIGHT 0x77027B6B
#define LCD_DIMMED_VALUE         25u
#define LCD_READ_CHUNK           256

static const uint8_t lcd_brightness_default[LCD_LUT_LEVELS] = {
    1, 3, 5, 8, 13, 20, 29, 41, 57, 76, 95, 116, 137, 161, 190, 220, 255
};
static const uint8_t lcd_stock_signature[LCD_LUT_LEVELS] = {
    31, 37, 43, 50, 58, 67, 77, 88, 100, 114, 129, 147, 166, 182, 203, 227, 255
};

typedef struct {
    uint8_t values[LCD_LUT_LEVELS];
    VbeSourceIdentity source;
} LcdCandidate;

typedef struct {
    int ownership;
    SceUID table_inject;
    SceUID brightness_hook;
    SceUID power_hook;
    tai_hook_ref_t brightness_ref;
    tai_hook_ref_t power_ref;
    int (*get_brightness)(void);
    int (*set_brightness)(unsigned int brightness);
    uint8_t runtime_lut[LCD_LUT_LEVELS];
    uint8_t committed_lut[LCD_LUT_LEVELS];
    VbeSourceIdentity source;
    VbePersistenceFile persistence;
} LcdBackend;

static LcdBackend g_lcd = {
    .ownership = VBE_OWNERSHIP_CLEAN,
    .table_inject = -1,
    .brightness_hook = -1,
    .power_hook = -1,
    .brightness_ref = -1,
    .power_ref = 0,
    .get_brightness = NULL,
    .set_brightness = NULL,
    .source = { .kind = VBE_SOURCE_ID_NONE, .path = {0} },
    .persistence = { .fd = -1, .fd_owned = 0, .temp_owned = 0,
                     .target = {0}, .temp = {0} },
};

static void brightness_error(int error, int detail) {
    status_set_error_domain(VBE_ERROR_DOMAIN_BRIGHTNESS, error, detail);
}
static void brightness_ok(void) {
    status_clear_error_domain(VBE_ERROR_DOMAIN_BRIGHTNESS);
}
static void lut_copy(uint8_t *dst, const uint8_t *src) {
    for (int i = 0; i < LCD_LUT_LEVELS; ++i) dst[i] = src[i];
}
static int lcd_brightness_to_index(unsigned int brightness) {
    if (brightness <= 2u) return 0;
    if (brightness >= 0x10000u) return LCD_LUT_LEVELS - 1;
    return (int)(16u * (brightness - 2u) / 65534u);
}
static int source_error_code(VbeSourceOutcome source) {
    return source.stage == VBE_SOURCE_STAGE_PARSE
        ? VBE_ERR_INVALID_USER_INPUT : VBE_ERR_SOURCE_IO;
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

static VbeSourceOutcome lcd_parse_lut_file(const char *path,
                                           uint8_t out[LCD_LUT_LEVELS]) {
    SceUID fd = ksceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return vbe_source_evaluate(fd, 0, 0, 0);
    VbeLcdLutParser parser;
    vbe_lcd_lut_parser_init(&parser, out);
    uint8_t buffer[LCD_READ_CHUNK];
    int read_result = 0, parse_result = 0;
    while (read_result == 0 && parse_result == 0) {
        int r = ksceIoRead(fd, buffer, sizeof(buffer));
        if (r < 0) { read_result = r; break; }
        if (r == 0) { parse_result = vbe_lcd_lut_parser_finish(&parser); break; }
        for (int i = 0; i < r; ++i) {
            if (vbe_lcd_lut_parser_feed(&parser, buffer[i]) < 0) {
                parse_result = -1; break;
            }
        }
    }
    int close_result = ksceIoClose(fd);
    return vbe_source_evaluate(fd, read_result, parse_result, close_result);
}

static int lcd_load_candidate(LcdCandidate *candidate, int *error_code) {
    VbeSourceOutcome primary = lcd_parse_lut_file(LCD_LUT_FILE1, candidate->values);
    if (primary.decision == VBE_SOURCE_USE) {
        if (vbe_source_identity_file(&candidate->source, LCD_LUT_FILE1) < 0) return -1;
        *error_code = VBE_ERR_NONE; return 0;
    }
    if (primary.decision == VBE_SOURCE_FAIL) {
        *error_code = source_error_code(primary); return primary.error;
    }
    VbeSourceOutcome fallback = lcd_parse_lut_file(LCD_LUT_FILE2, candidate->values);
    if (fallback.decision == VBE_SOURCE_USE) {
        if (vbe_source_identity_file(&candidate->source, LCD_LUT_FILE2) < 0) return -1;
        *error_code = VBE_ERR_NONE; return 0;
    }
    if (fallback.decision == VBE_SOURCE_FAIL) {
        *error_code = source_error_code(fallback); return fallback.error;
    }
    lut_copy(candidate->values, lcd_brightness_default);
    vbe_source_identity_compiled(&candidate->source);
    *error_code = VBE_ERR_NONE;
    return 0;
}

static int lcd_get_table_offset(uint32_t firmware, uint32_t *out) {
    switch (firmware >> 16) {
    case 0x360: *out = 0x1B00u; return 0;
    case 0x365:
    case 0x367:
    case 0x368:
    case 0x369:
    case 0x370: *out = 0x1B48u; return 0;
    default: return -1;
    }
}
static int lcd_validate_layout(const tai_module_info_t *info, uint32_t table_off) {
    uintptr_t address = 0;
    int ret = module_get_offset(KERNEL_PID, info->modid, 0, table_off, &address);
    if (ret < 0 || address == 0) return ret < 0 ? ret : -1;
    const volatile uint8_t *actual = (const volatile uint8_t *)address;
    for (int i = 0; i < LCD_LUT_LEVELS; ++i)
        if (actual[i] != lcd_stock_signature[i]) return -(0x100 + i);
    return 0;
}

int hook_ksceLcdSetBrightness(unsigned int brightness) {
    if (brightness != 1 || g_lcd.get_brightness == NULL)
        return TAI_CONTINUE(int, g_lcd.brightness_ref, brightness);
    int old_raw = g_lcd.get_brightness();
    if (old_raw < 0) return TAI_CONTINUE(int, g_lcd.brightness_ref, brightness);
    unsigned int old_brightness = (unsigned int)old_raw;
    int old_index = lcd_brightness_to_index(old_brightness);
    uint8_t table_value = g_lcd.runtime_lut[old_index];
    LOG("[LCD:DIM] req=1 old_raw=%u index=%d table=%u allow=%d\n",
        old_brightness, old_index, (unsigned)table_value,
        old_brightness >= 2u && table_value >= LCD_DIMMED_VALUE);
    if (old_brightness >= 2u && table_value >= LCD_DIMMED_VALUE)
        return TAI_CONTINUE(int, g_lcd.brightness_ref, brightness);
    return TAI_CONTINUE(int, g_lcd.brightness_ref, old_brightness);
}
int hook_kscePowerSetDisplayMaxBrightnessForLcd(int limit) {
    LOG("[LCD:POWER] max brightness request=%d forced=65536\n", limit);
    (void)limit;
    if (g_lcd.power_ref == 0) return 0;
    return TAI_CONTINUE(int, g_lcd.power_ref, 0x10000);
}

static VbeTxnAttempt lcd_release_resources(void) {
    int ret;
    if (g_lcd.ownership == VBE_OWNERSHIP_CLEAN) return vbe_txn_ok();
    if (g_lcd.power_hook >= 0) {
        ret = taiHookReleaseForKernel(g_lcd.power_hook, g_lcd.power_ref);
        if (ret < 0) {
            g_lcd.ownership = VBE_OWNERSHIP_DEGRADED; publish_failed();
            brightness_error(VBE_ERR_RESOURCE_RELEASE, ret);
            return vbe_txn_failed_dirty(VBE_ERR_RESOURCE_RELEASE, ret);
        }
        g_lcd.power_hook = -1; g_lcd.power_ref = 0;
    }
    if (g_lcd.brightness_hook >= 0) {
        ret = taiHookReleaseForKernel(g_lcd.brightness_hook, g_lcd.brightness_ref);
        if (ret < 0) {
            g_lcd.ownership = VBE_OWNERSHIP_DEGRADED; publish_failed();
            brightness_error(VBE_ERR_RESOURCE_RELEASE, ret);
            return vbe_txn_failed_dirty(VBE_ERR_RESOURCE_RELEASE, ret);
        }
        g_lcd.brightness_hook = -1; g_lcd.brightness_ref = -1;
    }
    if (g_lcd.table_inject >= 0) {
        ret = taiInjectReleaseForKernel(g_lcd.table_inject);
        if (ret < 0) {
            g_lcd.ownership = VBE_OWNERSHIP_DEGRADED; publish_failed();
            brightness_error(VBE_ERR_RESOURCE_RELEASE, ret);
            return vbe_txn_failed_dirty(VBE_ERR_RESOURCE_RELEASE, ret);
        }
        g_lcd.table_inject = -1;
    }
    g_lcd.ownership = VBE_OWNERSHIP_CLEAN;
    publish_inactive();
    return vbe_txn_ok();
}

static int lcd_resolve_core(tai_module_info_t *info) {
    info->size = sizeof(*info);
    int ret = taiGetModuleInfoForKernel(KERNEL_PID, "SceLcd", info);
    if (ret < 0) return ret;
    g_lcd.get_brightness = NULL; g_lcd.set_brightness = NULL;
    ret = module_get_export_func(KERNEL_PID, "SceLcd", TAI_ANY_LIBRARY,
        NID_LCD_GET_BRIGHTNESS, (uintptr_t *)&g_lcd.get_brightness);
    if (ret < 0 || !g_lcd.get_brightness) return ret < 0 ? ret : -1;
    ret = module_get_export_func(KERNEL_PID, "SceLcd", TAI_ANY_LIBRARY,
        NID_LCD_SET_BRIGHTNESS, (uintptr_t *)&g_lcd.set_brightness);
    if (ret < 0 || !g_lcd.set_brightness) return ret < 0 ? ret : -1;
    return 0;
}
static VbeTxnAttempt lcd_abort_start(int error, int detail) {
    VbeTxnAttempt cleanup = lcd_release_resources();
    if (cleanup.state != VBE_TXN_OK) return cleanup;
    publish_failed(); brightness_error(error, detail);
    return vbe_txn_failed_clean(error, detail);
}

static VbeTxnAttempt lcd_start_transaction(const LcdCandidate *candidate) {
    if (!vbe_txn_can_start(g_lcd.ownership)) {
        publish_failed(); brightness_error(VBE_ERR_RESOURCE_RELEASE, -1);
        return vbe_txn_failed_dirty(VBE_ERR_RESOURCE_RELEASE, -1);
    }
    if (!vbe_lcd_lut_values_valid(candidate->values)) {
        publish_failed(); brightness_error(VBE_ERR_INVALID_USER_INPUT, -1);
        return vbe_txn_failed_clean(VBE_ERR_INVALID_USER_INPUT, -1);
    }
    tai_module_info_t info;
    int ret = lcd_resolve_core(&info);
    if (ret < 0) {
        publish_failed(); brightness_error(VBE_ERR_EXPORT_RESOLUTION, ret);
        return vbe_txn_failed_clean(VBE_ERR_EXPORT_RESOLUTION, ret);
    }
    uint32_t table_off = 0;
    if (lcd_get_table_offset(sw_version, &table_off) < 0) {
        g_vbe_status.firmware_layout = VBE_CAP_UNSUPPORTED;
        g_vbe_status.brightness_table = VBE_CAP_UNSUPPORTED;
        brightness_error(VBE_ERR_FIRMWARE_UNSUPPORTED, (int)sw_version);
        return vbe_txn_failed_clean(VBE_ERR_FIRMWARE_UNSUPPORTED, -1);
    }
    ret = lcd_validate_layout(&info, table_off);
    if (ret < 0) {
        g_vbe_status.firmware_layout = VBE_CAP_FAILED; publish_failed();
        brightness_error(VBE_ERR_LAYOUT_MISMATCH, ret);
        return vbe_txn_failed_clean(VBE_ERR_LAYOUT_MISMATCH, ret);
    }
    g_vbe_status.firmware_layout = VBE_CAP_ACTIVE;
    g_lcd.table_inject = taiInjectDataForKernel(KERNEL_PID, info.modid, 0,
        table_off, candidate->values, LCD_LUT_LEVELS);
    if (g_lcd.table_inject < 0) {
        ret = (int)g_lcd.table_inject; g_lcd.table_inject = -1;
        publish_failed(); brightness_error(VBE_ERR_TABLE_INJECTION, ret);
        return vbe_txn_failed_clean(VBE_ERR_TABLE_INJECTION, ret);
    }
    g_lcd.ownership = VBE_OWNERSHIP_DEGRADED;
    lut_copy(g_lcd.runtime_lut, candidate->values);
    g_lcd.brightness_hook = taiHookFunctionExportForKernel(KERNEL_PID,
        &g_lcd.brightness_ref, "SceLcd", TAI_ANY_LIBRARY,
        NID_LCD_SET_BRIGHTNESS, hook_ksceLcdSetBrightness);
    if (g_lcd.brightness_hook < 0) {
        ret = (int)g_lcd.brightness_hook; g_lcd.brightness_hook = -1;
        return lcd_abort_start(VBE_ERR_BRIGHTNESS_HOOK, ret);
    }
    g_lcd.power_hook = taiHookFunctionExportForKernel(KERNEL_PID,
        &g_lcd.power_ref, "ScePower", TAI_ANY_LIBRARY,
        NID_POWER_SET_MAX_BRIGHT, hook_kscePowerSetDisplayMaxBrightnessForLcd);
    if (g_lcd.power_hook < 0) {
        ret = (int)g_lcd.power_hook; g_lcd.power_hook = -1;
        return lcd_abort_start(VBE_ERR_POWER_HOOK, ret);
    }
    int current = g_lcd.get_brightness();
    if (current < 0) return lcd_abort_start(VBE_ERR_BACKEND, current);
    ret = g_lcd.set_brightness((unsigned int)current);
    if (ret < 0) return lcd_abort_start(VBE_ERR_BACKEND, ret);
    lut_copy(g_lcd.committed_lut, candidate->values);
    vbe_txn_commit_source(&g_lcd.source, &candidate->source);
    g_lcd.ownership = VBE_OWNERSHIP_ACTIVE;
    publish_active(); brightness_ok();
    return vbe_txn_ok();
}

static int lcd_replace_candidate(const LcdCandidate *candidate) {
    LcdCandidate previous;
    int had_previous = g_lcd.ownership == VBE_OWNERSHIP_ACTIVE;
    if (had_previous) {
        lut_copy(previous.values, g_lcd.committed_lut);
        vbe_source_identity_copy(&previous.source, &g_lcd.source);
    }
    if (g_lcd.ownership == VBE_OWNERSHIP_DEGRADED) {
        brightness_error(VBE_ERR_RESOURCE_RELEASE, -1); return -1;
    }
    if (g_lcd.ownership == VBE_OWNERSHIP_ACTIVE) {
        VbeTxnAttempt release = lcd_release_resources();
        if (release.state != VBE_TXN_OK) return release.detail;
    }
    VbeTxnAttempt requested = lcd_start_transaction(candidate);
    if (requested.state == VBE_TXN_OK) return 0;
    if (!vbe_txn_should_rollback(had_previous, g_lcd.ownership, requested))
        return vbe_txn_public_result(requested, 0, vbe_txn_ok());
    VbeTxnAttempt rollback = lcd_start_transaction(&previous);
    if (rollback.state == VBE_TXN_OK)
        brightness_error(requested.error, requested.detail);
    else if (rollback.state == VBE_TXN_FAILED_CLEAN) {
        publish_failed(); brightness_error(VBE_ERR_LUT_ROLLBACK, rollback.detail);
    } else {
        publish_failed(); brightness_error(VBE_ERR_RESOURCE_RELEASE, rollback.detail);
    }
    if (rollback.state != VBE_TXN_OK)
        LOG("[LCD] replacement failed 0x%08X; recovery failed 0x%08X state=%d\n",
            requested.detail, rollback.detail, rollback.state);
    return vbe_txn_public_result(requested, 1, rollback);
}

int lcd_enable_hooks(void) {
    if (g_lcd.ownership == VBE_OWNERSHIP_ACTIVE) return 0;
    if (!vbe_txn_can_start(g_lcd.ownership)) return -1;
    LcdCandidate candidate;
    vbe_source_identity_clear(&candidate.source);
    int error_code = VBE_ERR_NONE;
    int ret = lcd_load_candidate(&candidate, &error_code);
    if (ret < 0) { brightness_error(error_code, ret); return ret; }
    VbeTxnAttempt start = lcd_start_transaction(&candidate);
    return start.state == VBE_TXN_OK ? 0 : start.detail;
}

static int persistence_error(const VbePersistenceOutcome *out) {
    if (out->cleanup_error < 0) return VBE_ERR_PERSISTENCE_CLEANUP;
    if (out->stage == VBE_PERSIST_STAGE_RENAME) return VBE_ERR_PERSISTENCE_COMMIT;
    return VBE_ERR_PERSISTENCE_PREPARE;
}
static int lcd_persist_prepare(void *c) { return vbe_persist_file_prepare(&((LcdBackend *)c)->persistence); }
static int lcd_persist_open(void *c) { return vbe_persist_file_open(&((LcdBackend *)c)->persistence); }
static int lcd_persist_sync(void *c) { return vbe_persist_file_sync(&((LcdBackend *)c)->persistence); }
static int lcd_persist_close(void *c) { return vbe_persist_file_close(&((LcdBackend *)c)->persistence); }
static int lcd_persist_rename(void *c) { return vbe_persist_file_rename(&((LcdBackend *)c)->persistence); }
static int lcd_persist_cleanup(void *c) { return vbe_persist_file_cleanup(&((LcdBackend *)c)->persistence); }

static int decimal_line(uint8_t value, char out[4]) {
    int pos = 0;
    if (value >= 100) {
        out[pos++] = (char)('0' + value / 100); value = (uint8_t)(value % 100);
        out[pos++] = (char)('0' + value / 10); out[pos++] = (char)('0' + value % 10);
    } else if (value >= 10) {
        out[pos++] = (char)('0' + value / 10); out[pos++] = (char)('0' + value % 10);
    } else out[pos++] = (char)('0' + value);
    out[pos++] = '\n'; return pos;
}
static int lcd_persist_write(void *context) {
    LcdBackend *backend = (LcdBackend *)context;
    if (backend->persistence.fd < 0) return -1;
    for (int i = 0; i < LCD_LUT_LEVELS; ++i) {
        char line[4]; int len = decimal_line(backend->committed_lut[i], line);
        int written = ksceIoWrite(backend->persistence.fd, line, (SceSize)len);
        if (written != len) return written < 0 ? written : -1;
    }
    return 0;
}
static int persist_lcd_locked(void) {
    if (g_lcd.source.kind == VBE_SOURCE_ID_COMPILED &&
        g_lcd.ownership == VBE_OWNERSHIP_ACTIVE)
        return VBE_RESULT_NO_FILE_SOURCE;
    if (!vbe_txn_file_persistence_allowed(g_lcd.ownership, &g_lcd.source)) return -1;
    int cleanup = vbe_persist_file_cleanup(&g_lcd.persistence);
    if (cleanup < 0) { brightness_error(VBE_ERR_PERSISTENCE_CLEANUP, cleanup); return cleanup; }
    if (vbe_persist_file_set_target(&g_lcd.persistence, g_lcd.source.path) < 0) {
        brightness_error(VBE_ERR_PERSISTENCE_PREPARE, -1); return -1;
    }
    VbePersistenceOps ops = {
        .context = &g_lcd, .prepare = lcd_persist_prepare,
        .open_temp = lcd_persist_open, .write_payload = lcd_persist_write,
        .sync_temp = lcd_persist_sync, .close_temp = lcd_persist_close,
        .rename_temp = lcd_persist_rename, .cleanup_temp = lcd_persist_cleanup,
    };
    VbePersistenceOutcome out = vbe_persistence_execute(&ops);
    if (!out.committed) {
        int detail = out.cleanup_error < 0 ? out.cleanup_error : out.error;
        brightness_error(persistence_error(&out), detail);
        LOG("[LCD:PERSIST] stage=%d error=0x%08X cleanup=%d/0x%08X\n",
            out.stage, out.error, out.cleanup_stage, out.cleanup_error);
        return detail < 0 ? detail : -1;
    }
    int current_error = VBE_ERR_NONE, current_detail = 0;
    status_get_error_domain(VBE_ERROR_DOMAIN_BRIGHTNESS, &current_error, &current_detail);
    (void)current_detail;
    if (current_error == VBE_ERR_PERSISTENCE_PREPARE ||
        current_error == VBE_ERR_PERSISTENCE_COMMIT ||
        current_error == VBE_ERR_PERSISTENCE_CLEANUP)
        brightness_ok();
    return 0;
}

int lcd_disable_hooks(void) {
    int first_error = 0;
    int persist = vbe_persist_file_cleanup(&g_lcd.persistence);
    if (persist < 0) { brightness_error(VBE_ERR_PERSISTENCE_CLEANUP, persist); first_error = persist; }
    VbeTxnAttempt release = lcd_release_resources();
    if (release.state != VBE_TXN_OK) first_error = release.detail;
    if (release.state == VBE_TXN_OK) {
        g_lcd.get_brightness = NULL; g_lcd.set_brightness = NULL;
    }
    if (first_error == 0) brightness_ok();
    return first_error;
}
int lcd_reload_backend(void) {
    LcdCandidate candidate;
    vbe_source_identity_clear(&candidate.source);
    int error_code = VBE_ERR_NONE;
    int ret = lcd_load_candidate(&candidate, &error_code);
    if (ret < 0) { brightness_error(error_code, ret); return ret; }
    return lcd_replace_candidate(&candidate);
}
int lcd_backend_mutation_safe(void) {
    return g_lcd.ownership != VBE_OWNERSHIP_DEGRADED;
}

int vitabrightLcdPersistBrightnessValues(void) {
    int state; ENTER_SYSCALL(state);
    int ret = state_lock_acquire();
    if (ret < 0) { EXIT_SYSCALL(state); return ret; }
    if (g_is_oled || g_lcd.ownership != VBE_OWNERSHIP_ACTIVE) {
        ret = state_lock_release_result(-1); EXIT_SYSCALL(state); return ret;
    }
    ret = persist_lcd_locked(); ret = state_lock_release_result(ret);
    EXIT_SYSCALL(state); return ret;
}
int vitabrightLcdGetBrightnessValues(uint8_t out[LCD_LUT_LEVELS]) {
    int state; ENTER_SYSCALL(state);
    int ret = state_lock_acquire();
    if (ret < 0) { EXIT_SYSCALL(state); return ret; }
    if (g_is_oled || g_lcd.ownership != VBE_OWNERSHIP_ACTIVE) {
        ret = state_lock_release_result(-1); EXIT_SYSCALL(state); return ret;
    }
    uint8_t snapshot[LCD_LUT_LEVELS];
    lut_copy(snapshot, g_lcd.committed_lut);
    ret = state_lock_release_result(0);
    if (ret >= 0)
        ret = ksceKernelMemcpyKernelToUser((void *)out, snapshot, sizeof(snapshot));
    EXIT_SYSCALL(state); return ret;
}
int vitabrightLcdSetBrightnessValues(uint8_t in[LCD_LUT_LEVELS]) {
    int state; LcdCandidate candidate; ENTER_SYSCALL(state);
    int ret = ksceKernelMemcpyUserToKernel(candidate.values, (const void *)in,
                                           sizeof(candidate.values));
    if (ret < 0 || !vbe_lcd_lut_values_valid(candidate.values)) {
        int detail = ret < 0 ? ret : -1;
        int lock = state_lock_acquire();
        if (lock >= 0) {
            status_stage_result(VBE_ERROR_DOMAIN_INPUT, 0,
                                VBE_ERR_INVALID_USER_INPUT, detail);
            detail = state_lock_release_result(detail);
        } else detail = lock;
        EXIT_SYSCALL(state); return detail;
    }
    ret = state_lock_acquire();
    if (ret < 0) { EXIT_SYSCALL(state); return ret; }
    status_stage_result(VBE_ERROR_DOMAIN_INPUT, 1, VBE_ERR_INVALID_USER_INPUT, 0);
    if (g_is_oled || g_lcd.ownership != VBE_OWNERSHIP_ACTIVE) {
        ret = state_lock_release_result(-1); EXIT_SYSCALL(state); return ret;
    }
    vbe_source_identity_copy(&candidate.source, &g_lcd.source);
    ret = lcd_replace_candidate(&candidate);
    ret = state_lock_release_result(ret); EXIT_SYSCALL(state); return ret;
}
int vitabrightLcdReapplyColor(void) {
    int state; ENTER_SYSCALL(state);
    int ret = state_lock_acquire();
    if (ret < 0) { EXIT_SYSCALL(state); return ret; }
    if (g_is_oled) {
        ret = state_lock_release_result(-1); EXIT_SYSCALL(state); return ret;
    }
    ret = color_space_apply_config();
    ret = state_lock_release_result(ret); EXIT_SYSCALL(state); return ret;
}
