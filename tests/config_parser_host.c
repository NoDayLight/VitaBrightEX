#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../config_parser.h"

static int parse_bytes(const unsigned char *p, size_t n, VitaBrightConfig *out) {
    VbeConfigParser parser;
    vbe_config_parser_init(&parser, out);
    for (size_t i = 0; i < n; ++i)
        if (vbe_config_parser_feed(&parser, p[i]) < 0) return -1;
    return vbe_config_parser_finish(&parser);
}

static int parse_text(const char *text, VitaBrightConfig *out) {
    return parse_bytes((const unsigned char *)text, strlen(text), out);
}

static unsigned char *read_all(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long length = ftell(f);
    if (length < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    *size = (size_t)length;
    unsigned char *bytes = malloc(*size ? *size : 1);
    if (!bytes) { fclose(f); return NULL; }
    if (fread(bytes, 1, *size, f) != *size) { free(bytes); fclose(f); return NULL; }
    fclose(f);
    return bytes;
}

static int parse_file(const char *path, VitaBrightConfig *out) {
    size_t size = 0;
    unsigned char *bytes = read_all(path, &size);
    if (!bytes) return -1;
    int ret = parse_bytes(bytes, size, out);
    free(bytes);
    return ret;
}

static int long_comment(unsigned char marker) {
    VitaBrightConfig cfg;
    VbeConfigParser parser;
    vbe_config_parser_init(&parser, &cfg);
    if (vbe_config_parser_feed(&parser, marker) < 0) return -1;
    for (int i = 0; i < 16384; ++i)
        if (vbe_config_parser_feed(&parser, 'x') < 0) return -1;
    const char *tail = "\nfilter_cct=7000\n";
    for (const char *p = tail; *p; ++p)
        if (vbe_config_parser_feed(&parser, (unsigned char)*p) < 0) return -1;
    if (vbe_config_parser_finish(&parser) < 0) return -1;
    return cfg.filter_cct == 7000 ? 0 : -1;
}

static int overlong_directive(int with_valid_tail) {
    VitaBrightConfig cfg;
    VbeConfigParser parser;
    vbe_config_parser_init(&parser, &cfg);
    const char *prefix = "panel_lut_path=";
    for (const char *p = prefix; *p; ++p)
        if (vbe_config_parser_feed(&parser, (unsigned char)*p) < 0) return 0;
    for (int i = 0; i < 512; ++i)
        if (vbe_config_parser_feed(&parser, 'x') < 0) return 0;
    if (with_valid_tail) {
        const char *tail = "\nfilter_cct=7000\n";
        for (const char *p = tail; *p; ++p)
            if (vbe_config_parser_feed(&parser, (unsigned char)*p) < 0) return 0;
    }
    return vbe_config_parser_finish(&parser) < 0 ? 0 : -1;
}

static int ok(int condition, const char *name) {
    if (condition) return 0;
    fprintf(stderr, "FAIL: %s\n", name);
    return 1;
}

int main(void) {
    int failures = 0;
    VitaBrightConfig cfg;

    failures += ok(parse_file("vitabrightex.cfg", &cfg) == 0 &&
                   cfg.filter_cct == 6500 && cfg.filter_gamma == 1.0f,
                   "source config");
    failures += ok(parse_file("release/ur0_tai/vitabrightex.cfg", &cfg) == 0,
                   "packaged config");
    failures += ok(long_comment('#') == 0, "16K hash comment");
    failures += ok(long_comment(';') == 0, "16K semicolon comment");
    failures += ok(overlong_directive(0) == 0, "overlong known directive rejected");
    failures += ok(overlong_directive(1) == 0, "overlong directive tail never reinterpreted");

    failures += ok(parse_text("filter_cct=7000", &cfg) == 0 && cfg.filter_cct == 7000,
                   "EOF without final newline");
    failures += ok(parse_text("filter_cct=7000\r\nfilter_gamma=1.5\r\n", &cfg) == 0 &&
                   cfg.filter_cct == 7000 && cfg.filter_gamma > 1.49f && cfg.filter_gamma < 1.51f,
                   "CRLF config");
    failures += ok(parse_text("filter_cct=65\r00\n", &cfg) < 0,
                   "interior CR rejected");
    failures += ok(parse_text("filter_cct=6500\r", &cfg) < 0,
                   "terminal CR rejected");
    failures += ok(parse_text("   filter_cct   =   7000   \n", &cfg) == 0 && cfg.filter_cct == 7000,
                   "leading and separator whitespace");
    failures += ok(parse_text("future_option=garbage\n", &cfg) == 0 && cfg.filter_cct == 6500,
                   "unknown key ignored");
    failures += ok(parse_text("filter_cct=7100\n", &cfg) == 0 && cfg.filter_cct == 7100,
                   "known key applied");
    failures += ok(parse_text("filter_cct 7100\n", &cfg) == 0 && cfg.filter_cct == 6500,
                   "missing equals ignored");
    failures += ok(parse_text("filter_cct=\n", &cfg) < 0,
                   "empty numeric value rejected");
    failures += ok(parse_text("panel_lut_path=\n", &cfg) == 0 && cfg.panel_lut_path[0] == '\0',
                   "empty path allowed");
    failures += ok(parse_text("filter_cct=999999999999999999999\nfilter_brightness=-999999.0\n", &cfg) == 0 &&
                   cfg.filter_cct == 25100 && cfg.filter_brightness == -1.0f,
                   "extreme values clamp safely");
    failures += ok(parse_text("filter_cct=6500garbage\n", &cfg) < 0,
                   "integer suffix garbage rejected");
    failures += ok(parse_text("filter_gamma=1.2nonsense\n", &cfg) < 0,
                   "float suffix garbage rejected");
    failures += ok(parse_text("filter_cct=7000\nfilter_cct=7200\n", &cfg) == 0 && cfg.filter_cct == 7200,
                   "duplicate key last valid occurrence wins");

    if (failures) return 1;
    puts("production config parser regressions: OK");
    return 0;
}
