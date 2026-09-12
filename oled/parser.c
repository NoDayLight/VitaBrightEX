#include "lut.h"
#include "parser.h"
#include "../log.h"
#include "../lut_parser_core.h"
#include <psp2kern/io/fcntl.h>

#define OLED_READ_CHUNK 256

static void path_copy(char dst[LUT_SOURCE_PATH_MAX], const char *src) {
    int i = 0;
    while (i < LUT_SOURCE_PATH_MAX - 1 && src[i]) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

/* `opened` distinguishes an absent candidate from an authoritative-but-bad
 * candidate. A malformed ur0 source therefore never falls through to ux0. */
static int parse_candidate(const char *path, unsigned char out[LUT_SIZE],
                           int *opened) {
    SceUID fd = ksceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) {
        *opened = 0;
        return fd;
    }
    *opened = 1;
    LOG("[LUT] Parsing authoritative source: %s\n", path);

    VbeOledLutParser parser;
    vbe_oled_lut_parser_init(&parser, out);
    unsigned char buffer[OLED_READ_CHUNK];
    int ret = 0;

    while (1) {
        int r = ksceIoRead(fd, buffer, sizeof(buffer));
        if (r < 0) {
            ret = r;
            break;
        }
        if (r == 0) {
            ret = vbe_oled_lut_parser_finish(&parser);
            break;
        }
        for (int i = 0; i < r; ++i) {
            if (vbe_oled_lut_parser_feed(&parser, buffer[i]) < 0) {
                ret = -1;
                break;
            }
        }
        if (ret < 0) break;
    }

    int close_ret = ksceIoClose(fd);
    if (ret == 0 && close_ret < 0) ret = close_ret;
    if (ret < 0) {
        LOG("[LUT] Rejected %s: 0x%08X\n", path, ret);
        return ret;
    }

    LOG("[LUT] Accepted %s (%d rows)\n", path, LUT_ROWS);
    return 0;
}

int parse_lut_from_file(const char *path, unsigned char out[LUT_SIZE]) {
    int opened = 0;
    return parse_candidate(path, out, &opened);
}

static int try_authoritative(const char *path, unsigned char out[LUT_SIZE],
                             char source_path[LUT_SOURCE_PATH_MAX],
                             int *was_present) {
    int opened = 0;
    int ret = parse_candidate(path, out, &opened);
    if (!opened) {
        *was_present = 0;
        return ret;
    }

    *was_present = 1;
    if (ret < 0) return ret;
    path_copy(source_path, path);
    return 0;
}

int parse_lut(int panel_type, unsigned char out[LUT_SIZE],
              char source_path[LUT_SOURCE_PATH_MAX]) {
    const char *panel_ur0 = NULL;
    const char *panel_ux0 = NULL;

    switch (panel_type) {
    case OLED_PANEL_4:
        panel_ur0 = LUT_FILE_P4_1;
        panel_ux0 = LUT_FILE_P4_2;
        break;
    case OLED_PANEL_5:
        panel_ur0 = LUT_FILE_P5_1;
        panel_ux0 = LUT_FILE_P5_2;
        break;
    case OLED_PANEL_6:
        panel_ur0 = LUT_FILE_P6_1;
        panel_ux0 = LUT_FILE_P6_2;
        break;
    default:
        break;
    }

    const char *candidates[4] = { panel_ur0, panel_ux0, LUT_FILE1, LUT_FILE2 };
    int first = panel_ur0 != NULL ? 0 : 2;
    int last_open_error = -1;

    for (int i = first; i < 4; ++i) {
        const char *path = candidates[i];
        if (path == NULL) continue;

        int present = 0;
        int ret = try_authoritative(path, out, source_path, &present);
        if (present) return ret;
        last_open_error = ret;
    }

    source_path[0] = '\0';
    LOG("[LUT] No LUT source found for panel %d\n", panel_type);
    return last_open_error;
}

int parse_lut_override(const char *path, unsigned char out[LUT_SIZE],
                       char source_path[LUT_SOURCE_PATH_MAX]) {
    int opened = 0;
    int ret = parse_candidate(path, out, &opened);
    if (ret == 0) path_copy(source_path, path);
    else source_path[0] = '\0';
    return ret;
}
