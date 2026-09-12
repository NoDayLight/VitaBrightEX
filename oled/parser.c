#include "lut.h"
#include "parser.h"
#include "../log.h"
#include "../lut_parser_core.h"
#include "../source_authority.h"
#include "../status.h"
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

static int source_error_code(VbeSourceOutcome source) {
    return source.stage == VBE_SOURCE_STAGE_PARSE
        ? VBE_ERR_INVALID_USER_INPUT : VBE_ERR_SOURCE_IO;
}

static VbeSourceOutcome parse_candidate(const char *path,
                                        unsigned char out[LUT_SIZE]) {
    SceUID fd = ksceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return vbe_source_evaluate(fd, 0, 0, 0);

    LOG("[LUT] Parsing authoritative source: %s\n", path);
    VbeOledLutParser parser;
    vbe_oled_lut_parser_init(&parser, out);
    unsigned char buffer[OLED_READ_CHUNK];
    int read_result = 0;
    int parse_result = 0;

    while (read_result == 0 && parse_result == 0) {
        int r = ksceIoRead(fd, buffer, sizeof(buffer));
        if (r < 0) {
            read_result = r;
            break;
        }
        if (r == 0) {
            parse_result = vbe_oled_lut_parser_finish(&parser);
            break;
        }
        for (int i = 0; i < r; ++i) {
            if (vbe_oled_lut_parser_feed(&parser, buffer[i]) < 0) {
                parse_result = -1;
                break;
            }
        }
    }

    int close_result = ksceIoClose(fd);
    VbeSourceOutcome source = vbe_source_evaluate(fd, read_result,
                                                   parse_result, close_result);
    if (source.decision == VBE_SOURCE_USE)
        LOG("[LUT] Accepted %s (%d rows)\n", path, LUT_ROWS);
    else
        LOG("[LUT] Rejected %s at source stage %d: 0x%08X\n",
            path, source.stage, source.error);
    return source;
}

int parse_lut_from_file(const char *path, unsigned char out[LUT_SIZE]) {
    VbeSourceOutcome source = parse_candidate(path, out);
    return source.decision == VBE_SOURCE_USE ? 0 : source.error;
}

int parse_lut(int panel_type, unsigned char out[LUT_SIZE],
              char source_path[LUT_SOURCE_PATH_MAX], int *error_code) {
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

    for (int i = first; i < 4; ++i) {
        const char *path = candidates[i];
        if (path == NULL) continue;

        VbeSourceOutcome source = parse_candidate(path, out);
        if (source.decision == VBE_SOURCE_USE) {
            path_copy(source_path, path);
            if (error_code != NULL) *error_code = VBE_ERR_NONE;
            return 0;
        }
        if (source.decision == VBE_SOURCE_FAIL) {
            source_path[0] = '\0';
            if (error_code != NULL) *error_code = source_error_code(source);
            return source.error;
        }
    }

    source_path[0] = '\0';
    if (error_code != NULL) *error_code = VBE_ERR_SOURCE_IO;
    LOG("[LUT] All documented LUT sources absent for panel %d\n", panel_type);
    return VBE_SCE_IO_ERROR_NOT_FOUND;
}

int parse_lut_override(const char *path, unsigned char out[LUT_SIZE],
                       char source_path[LUT_SOURCE_PATH_MAX], int *error_code) {
    VbeSourceOutcome source = parse_candidate(path, out);
    if (source.decision == VBE_SOURCE_USE) {
        path_copy(source_path, path);
        if (error_code != NULL) *error_code = VBE_ERR_NONE;
        return 0;
    }

    source_path[0] = '\0';
    if (error_code != NULL) *error_code = source_error_code(source);
    return source.error;
}
