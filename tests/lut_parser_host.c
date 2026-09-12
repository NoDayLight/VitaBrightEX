#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../lut_parser_core.h"

static const uint8_t expected[LCD_LUT_LEVELS] = {
    1,3,5,8,13,20,29,41,57,76,95,116,137,161,190,220,255
};

static int lcd_bytes(const unsigned char *p, size_t n) {
    uint8_t out[LCD_LUT_LEVELS];
    VbeLcdLutParser s;
    vbe_lcd_lut_parser_init(&s, out);
    for (size_t i = 0; i < n; ++i)
        if (vbe_lcd_lut_parser_feed(&s, p[i]) < 0) return -1;
    return vbe_lcd_lut_parser_finish(&s);
}

static int lcd_text(const char *s) {
    return lcd_bytes((const unsigned char *)s, strlen(s));
}

static unsigned char *read_all(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) || (*n = (size_t)ftell(f), fseek(f, 0, SEEK_SET))) {
        fclose(f); return NULL;
    }
    unsigned char *p = malloc(*n ? *n : 1);
    if (!p) { fclose(f); return NULL; }
    if (fread(p, 1, *n, f) != *n) { free(p); fclose(f); return NULL; }
    fclose(f);
    return p;
}

static int lcd_file(const char *path) {
    size_t n = 0;
    unsigned char *p = read_all(path, &n);
    if (!p) return -1;
    uint8_t out[LCD_LUT_LEVELS];
    VbeLcdLutParser s;
    vbe_lcd_lut_parser_init(&s, out);
    int r = 0;
    for (size_t i = 0; i < n && r == 0; ++i) r = vbe_lcd_lut_parser_feed(&s, p[i]);
    if (r == 0) r = vbe_lcd_lut_parser_finish(&s);
    free(p);
    return r == 0 && memcmp(out, expected, sizeof(out)) == 0 ? 0 : -1;
}

static int oled_file(const char *path, int prepend_long_comment) {
    size_t n = 0;
    unsigned char *p = read_all(path, &n);
    if (!p) return -1;
    uint8_t out[LUT_SIZE];
    VbeOledLutParser s;
    vbe_oled_lut_parser_init(&s, out);
    int r = 0;
    if (prepend_long_comment) {
        r = vbe_oled_lut_parser_feed(&s, '#');
        for (int i = 0; i < 16384 && r == 0; ++i) r = vbe_oled_lut_parser_feed(&s, 'x');
        if (r == 0) r = vbe_oled_lut_parser_feed(&s, '\n');
    }
    for (size_t i = 0; i < n && r == 0; ++i) r = vbe_oled_lut_parser_feed(&s, p[i]);
    if (r == 0) r = vbe_oled_lut_parser_finish(&s);
    free(p);
    return r;
}

static int long_lcd_comment(void) {
    uint8_t out[LCD_LUT_LEVELS];
    VbeLcdLutParser s;
    vbe_lcd_lut_parser_init(&s, out);
    int r = vbe_lcd_lut_parser_feed(&s, '#');
    for (int i = 0; i < 16384 && r == 0; ++i) r = vbe_lcd_lut_parser_feed(&s, 'x');
    if (r == 0) r = vbe_lcd_lut_parser_feed(&s, '\n');
    for (int i = 0; i < LCD_LUT_LEVELS && r == 0; ++i) {
        char b[8];
        int k = snprintf(b, sizeof(b), "%u%s", (unsigned)expected[i],
                         i + 1 == LCD_LUT_LEVELS ? "" : "\n");
        for (int j = 0; j < k && r == 0; ++j) r = vbe_lcd_lut_parser_feed(&s, (unsigned char)b[j]);
    }
    if (r == 0) r = vbe_lcd_lut_parser_finish(&s);
    return r;
}

static int long_bad_lcd_data(void) {
    uint8_t out[LCD_LUT_LEVELS];
    VbeLcdLutParser s;
    vbe_lcd_lut_parser_init(&s, out);
    for (int i = 0; i < 4096; ++i)
        if (vbe_lcd_lut_parser_feed(&s, '9') < 0) return 0;
    return -1;
}

static int ok(int cond, const char *name) {
    if (cond) return 0;
    fprintf(stderr, "FAIL: %s\n", name);
    return 1;
}

int main(void) {
    int f = 0;
    f += ok(lcd_file("lcd/luts/vitabright_lcd_lut.txt") == 0, "source LCD asset");
    f += ok(lcd_file("release/ur0_tai/vitabright_lcd_lut.txt") == 0, "packaged LCD asset");
    f += ok(long_lcd_comment() == 0, "arbitrarily long LCD comment");
    f += ok(long_bad_lcd_data() == 0, "long malformed LCD data rejected");
    f += ok(lcd_text("1\n3\n5\n8\n13\n20\n29\n41\n57\n76\n95\n116\n137\n161\n190\n220\n255") == 0, "EOF without newline");
    f += ok(lcd_text("1\r\n3\r\n5\r\n8\r\n13\r\n20\r\n29\r\n41\r\n57\r\n76\r\n95\r\n116\r\n137\r\n161\r\n190\r\n220\r\n255\r\n") == 0, "CRLF");
    f += ok(lcd_text("1\n3\n5\n8\n13\n20\n29\n41\n57\n76\n95\n116\n137\n161\n190\n180\n255\n") < 0, "non-monotonic rejected");
    f += ok(lcd_text("1\n3\n5\n8\n13\n20\n29\n41\n57\n76\n95\n116\n137\n161\n190\n220\n256\n") < 0, ">255 rejected");
    f += ok(lcd_text("1\n3\n5\n8\n13\n20\n29\n41\n57\n76\n95\n116\n137\n161\n190\n220\n") < 0, "fewer than 17 rejected");
    f += ok(lcd_text("1\n3\n5\n8\n13\n20\n29\n41\n57\n76\n95\n116\n137\n161\n190\n220\n255\n255\n") < 0, "more than 17 rejected");
    f += ok(lcd_text("# c\n1 # inline\n3\n5\n8\n13\n20\n29\n41\n57\n76\n95\n116\n137\n161\n190\n220\n255\n") == 0, "comments");

    const char *oled[] = {
        "oled/luts/vitabright_lut.txt", "oled/luts/vitabright_lut_p4.txt",
        "oled/luts/vitabright_lut_p5.txt", "oled/luts/vitabright_lut_p6.txt",
        "release/ur0_tai/vitabright_lut.txt", "release/ur0_tai/vitabright_lut_p4.txt",
        "release/ur0_tai/vitabright_lut_p5.txt", "release/ur0_tai/vitabright_lut_p6.txt"
    };
    for (unsigned i = 0; i < sizeof(oled) / sizeof(oled[0]); ++i)
        f += ok(oled_file(oled[i], 0) == 0, oled[i]);
    f += ok(oled_file("oled/luts/vitabright_lut.txt", 1) == 0, "arbitrarily long OLED comment");

    if (f) return 1;
    puts("production LUT parser regressions: OK");
    return 0;
}
