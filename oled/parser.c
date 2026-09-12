#include "lut.h"
#include "parser.h"
#include "../log.h"
#include <psp2kern/io/fcntl.h>

#define PARSER_LINE_MAX 160

static int is_hex(unsigned char c) {
    return ('0' <= c && c <= '9') || ('A' <= c && c <= 'F') ||
           ('a' <= c && c <= 'f');
}

static int parse_hex_digit(unsigned char c) {
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return c - '0';
}

static void path_copy(char dst[LUT_SOURCE_PATH_MAX], const char *src) {
    int i = 0;
    while (i < LUT_SOURCE_PATH_MAX - 1 && src[i]) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

/* 1 = blank/comment, 0 = exactly one 21-byte row, <0 = malformed. */
static int parse_data_line(const char *line, int len,
                           unsigned char out[LUT_LINE_SIZE]) {
    int pos = 0;
    while (pos < len && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
    if (pos == len || line[pos] == '#') return 1;

    for (int i = 0; i < LUT_LINE_SIZE; ++i) {
        if (pos + 2 > len || !is_hex((unsigned char)line[pos]) ||
            !is_hex((unsigned char)line[pos + 1]))
            return -1;

        out[i] = (unsigned char)(16 * parse_hex_digit((unsigned char)line[pos]) +
                                 parse_hex_digit((unsigned char)line[pos + 1]));
        pos += 2;

        if (i != LUT_LINE_SIZE - 1) {
            if (pos >= len || (line[pos] != ' ' && line[pos] != '\t')) return -1;
            while (pos < len && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
        }
    }

    while (pos < len && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
    if (pos < len && line[pos] != '#') return -1;
    return 0;
}

/* `opened` distinguishes an absent candidate from an authoritative-but-bad
 * candidate. That distinction is what prevents a malformed ur0 file from
 * silently falling through to ux0 or a generic LUT. */
static int parse_candidate(const char *path, unsigned char out[LUT_SIZE],
                           int *opened) {
    SceUID fd = ksceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) {
        *opened = 0;
        return fd;
    }
    *opened = 1;
    LOG("[LUT] Parsing authoritative source: %s\n", path);

    int rows = 0;
    int malformed = 0;
    char line[PARSER_LINE_MAX];
    int len = 0;

    while (1) {
        char c = 0;
        int r = ksceIoRead(fd, &c, 1);
        int eof = r <= 0;

        if (!eof && c == '\r') continue;

        if (eof || c == '\n') {
            if (len > 0) {
                unsigned char row[LUT_LINE_SIZE];
                int parsed = parse_data_line(line, len, row);
                if (parsed < 0) {
                    malformed = 1;
                    break;
                }
                if (parsed == 0) {
                    if (rows >= LUT_ROWS) {
                        malformed = 1;
                        break;
                    }
                    for (int i = 0; i < LUT_LINE_SIZE; ++i)
                        out[rows * LUT_LINE_SIZE + i] = row[i];
                    ++rows;
                }
            }
            len = 0;
            if (eof) break;
            continue;
        }

        if (len >= PARSER_LINE_MAX) {
            malformed = 1;
            break;
        }
        line[len++] = c;
    }

    ksceIoClose(fd);
    if (malformed || rows != LUT_ROWS) {
        LOG("[LUT] Rejected %s: rows=%d malformed=%d\n", path, rows, malformed);
        return -1;
    }

    LOG("[LUT] Accepted %s (%d rows)\n", path, rows);
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
    int candidate_count = panel_ur0 != NULL ? 4 : 2;
    int first = panel_ur0 != NULL ? 0 : 2;
    int last_open_error = -1;

    for (int i = first; i < candidate_count; ++i) {
        const char *path = candidates[i];
        if (path == NULL) continue;

        int present = 0;
        int ret = try_authoritative(path, out, source_path, &present);
        if (present) return ret; /* success or authoritative format failure */
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
