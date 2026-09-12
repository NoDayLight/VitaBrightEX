#include "config.h"
#include "config_parser.h"
#include "log.h"
#include <psp2kern/io/fcntl.h>

#define CONFIG_READ_CHUNK 256

VitaBrightConfig g_config;

void config_reset_defaults(void) {
    vbe_config_defaults(&g_config);
}

int config_load(void) {
    VitaBrightConfig candidate;
    VbeConfigParser parser;
    vbe_config_parser_init(&parser, &candidate);

    SceUID fd = ksceIoOpen(CFG_FILE1, SCE_O_RDONLY, 0);
    if (fd >= 0) {
        LOG("[CFG] Loaded authoritative source %s\n", CFG_FILE1);
    } else {
        fd = ksceIoOpen(CFG_FILE2, SCE_O_RDONLY, 0);
        if (fd >= 0) LOG("[CFG] Loaded fallback source %s\n", CFG_FILE2);
    }

    if (fd < 0) {
        g_config = candidate;
        LOG("[CFG] No config file found; using defaults\n");
        return 0;
    }

    unsigned char buffer[CONFIG_READ_CHUNK];
    int ret = 0;
    while (1) {
        int read_ret = ksceIoRead(fd, buffer, sizeof(buffer));
        if (read_ret < 0) {
            ret = read_ret;
            break;
        }
        if (read_ret == 0) {
            ret = vbe_config_parser_finish(&parser);
            break;
        }
        for (int i = 0; i < read_ret; ++i) {
            if (vbe_config_parser_feed(&parser, buffer[i]) < 0) {
                ret = -1;
                break;
            }
        }
        if (ret < 0) break;
    }

    int close_ret = ksceIoClose(fd);
    if (ret == 0 && close_ret < 0) ret = close_ret;
    if (ret < 0) {
        LOG("[CFG] Rejected authoritative config: 0x%08X\n", ret);
        return ret;
    }

    g_config = candidate;
    return 0;
}
