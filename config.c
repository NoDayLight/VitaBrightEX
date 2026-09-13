#include "config.h"
#include "config_parser.h"
#include "config_state_core.h"
#include "log.h"
#include "source_authority.h"
#include "status.h"
#include <psp2kern/io/fcntl.h>

#define CONFIG_READ_CHUNK 256

VitaBrightConfig g_config;
static VbeSourceIdentity g_config_source = {
    .kind = VBE_SOURCE_ID_NONE,
    .path = {0},
};

void config_reset_defaults(void) {
    vbe_config_defaults(&g_config);
    vbe_source_identity_compiled(&g_config_source);
}

void config_get_source(VbeSourceIdentity *out) {
    vbe_source_identity_copy(out, &g_config_source);
}

void config_snapshot(VbeConfigSnapshot *out) {
    vbe_config_state_snapshot(out, &g_config, &g_config_source);
}

void config_restore(const VbeConfigSnapshot *snapshot) {
    vbe_config_state_restore(&g_config, &g_config_source, snapshot);
}

static VbeSourceOutcome config_read_source(const char *path,
                                           VitaBrightConfig *candidate) {
    SceUID fd = ksceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return vbe_source_evaluate(fd, 0, 0, 0);

    VbeConfigParser parser;
    vbe_config_parser_init(&parser, candidate);

    unsigned char buffer[CONFIG_READ_CHUNK];
    int read_result = 0;
    int parse_result = 0;

    while (read_result == 0 && parse_result == 0) {
        int read_ret = ksceIoRead(fd, buffer, sizeof(buffer));
        if (read_ret < 0) {
            read_result = read_ret;
            break;
        }
        if (read_ret == 0) {
            parse_result = vbe_config_parser_finish(&parser);
            break;
        }
        for (int i = 0; i < read_ret; ++i) {
            if (vbe_config_parser_feed(&parser, buffer[i]) < 0) {
                parse_result = -1;
                break;
            }
        }
    }

    int close_result = ksceIoClose(fd);
    return vbe_source_evaluate(fd, read_result, parse_result, close_result);
}

static int config_accept_source(VbeSourceOutcome source,
                                const VitaBrightConfig *candidate,
                                const char *path) {
    if (source.decision == VBE_SOURCE_USE) {
        VbeSourceIdentity candidate_source;
        if (vbe_source_identity_file(&candidate_source, path) < 0) {
            status_stage_result(VBE_ERROR_DOMAIN_CONFIG, 0, VBE_ERR_CONFIG, -1);
            return -1;
        }
        g_config = *candidate;
        vbe_source_identity_copy(&g_config_source, &candidate_source);
        status_stage_result(VBE_ERROR_DOMAIN_CONFIG, 1, VBE_ERR_CONFIG, 0);
        return 0;
    }

    if (source.decision == VBE_SOURCE_FAIL) {
        status_stage_result(VBE_ERROR_DOMAIN_CONFIG, 0, VBE_ERR_CONFIG,
                            source.error);
        return source.error;
    }

    return 1; /* Explicit NOT_FOUND: caller may try its documented fallback. */
}

int config_load(void) {
    VitaBrightConfig candidate;
    VbeSourceOutcome source = config_read_source(CFG_FILE1, &candidate);
    int decision = config_accept_source(source, &candidate, CFG_FILE1);
    if (decision == 0) {
        LOG("[CFG] Loaded authoritative source " CFG_FILE1 "\n");
        return 0;
    }
    if (decision < 0) {
        LOG("[CFG] Rejected authoritative source " CFG_FILE1 ": 0x%08X\n",
            decision);
        return decision;
    }

    source = config_read_source(CFG_FILE2, &candidate);
    decision = config_accept_source(source, &candidate, CFG_FILE2);
    if (decision == 0) {
        LOG("[CFG] Loaded fallback source " CFG_FILE2 "\n");
        return 0;
    }
    if (decision < 0) {
        LOG("[CFG] Rejected fallback source " CFG_FILE2 ": 0x%08X\n",
            decision);
        return decision;
    }

    vbe_config_defaults(&g_config);
    vbe_source_identity_compiled(&g_config_source);
    status_stage_result(VBE_ERROR_DOMAIN_CONFIG, 1, VBE_ERR_CONFIG, 0);
    LOG("[CFG] Both documented config sources are absent; using compiled defaults\n");
    return 0;
}
