#include "config.h"
#include "config_parser.h"
#include "log.h"
#include "source_authority.h"
#include "status.h"
#include <psp2kern/io/fcntl.h>

#define CONFIG_READ_CHUNK 256

VitaBrightConfig g_config;

void config_reset_defaults(void) {
    vbe_config_defaults(&g_config);
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

static int config_commit_source(const char *path, int fallback) {
    VitaBrightConfig candidate;
    VbeSourceOutcome source = config_read_source(path, &candidate);

    if (source.decision == VBE_SOURCE_FALLBACK)
        return SCE_ERROR_ERRNO_ENOENT;

    if (source.decision == VBE_SOURCE_FAIL) {
        status_stage_result(VBE_ERROR_DOMAIN_CONFIG, 0, VBE_ERR_CONFIG,
                            source.error);
        LOG("[CFG] Rejected %s at source stage %d: 0x%08X\n",
            path, source.stage, source.error);
        return source.error;
    }

    g_config = candidate;
    status_stage_result(VBE_ERROR_DOMAIN_CONFIG, 1, VBE_ERR_CONFIG, 0);
    LOG("[CFG] Loaded %s source %s\n",
        fallback ? "fallback" : "authoritative", path);
    return 0;
}

int config_load(void) {
    int ret = config_commit_source(CFG_FILE1, 0);
    if (ret != SCE_ERROR_ERRNO_ENOENT) return ret;

    ret = config_commit_source(CFG_FILE2, 1);
    if (ret != SCE_ERROR_ERRNO_ENOENT) return ret;

    VitaBrightConfig defaults;
    vbe_config_defaults(&defaults);
    g_config = defaults;
    status_stage_result(VBE_ERROR_DOMAIN_CONFIG, 1, VBE_ERR_CONFIG, 0);
    LOG("[CFG] Both documented config sources are absent; using defaults\n");
    return 0;
}
