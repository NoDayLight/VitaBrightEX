#include <stdio.h>
#include <string.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#include "../status.h"
#include "../screen_filter.h"
#include "../lcd/lcd_lut.h"
#include "../oled/lut.h"

#define WHITE RGBA8(255,255,255,255)
#define DIM   RGBA8(170,170,170,255)
#define BAD   RGBA8(255,150,150,255)
#define OK    RGBA8(160,255,180,255)

int vitabrightReload(void);
int vitabrightColorSpaceGetMode(void);
int vitabrightColorSpaceSetMode(int mode);
int vitabrightOledGetLut(unsigned char out[LUT_SIZE]);
int vitabrightOledSetLut(unsigned char in[LUT_SIZE]);
int vitabrightOledPersistLut(void);
int vitabrightLcdGetBrightnessValues(unsigned char out[LCD_LUT_LEVELS]);
int vitabrightLcdSetBrightnessValues(unsigned char in[LCD_LUT_LEVELS]);
int vitabrightLcdPersistBrightnessValues(void);

static VitaBrightStatus status;
static ScreenFilterParams filter;
static unsigned char oled_lut[LUT_SIZE];
static unsigned char lcd_lut[LCD_LUT_LEVELS];
static int status_ok, filter_ok, lut_ok, color_ok, color_mode = -1, cursor;
static char msg[96];

static const char *cap(int s) {
    switch (s) {
    case VBE_CAP_UNAVAILABLE: return "unavailable";
    case VBE_CAP_INACTIVE: return "inactive";
    case VBE_CAP_ACTIVE: return "active";
    case VBE_CAP_FAILED: return "FAILED";
    case VBE_CAP_UNSUPPORTED: return "unsupported";
    default: return "unknown";
    }
}
static int usable(int s) { return s == VBE_CAP_ACTIVE || s == VBE_CAP_INACTIVE; }
static void set_msg(const char *s) { snprintf(msg, sizeof(msg), "%s", s); }

static int refresh(void) {
    memset(&status, 0, sizeof(status));
    status_ok = vitabrightGetStatus(&status) >= 0 && status.abi_version >= 2;
    if (!status_ok) {
        filter_ok = lut_ok = color_ok = 0; color_mode = -1;
        set_msg("Cannot read VitaBrightEX v1.4 status ABI.");
        return -1;
    }
    filter_ok = vitabrightFilterGetParams(&filter) >= 0;
    color_ok = 0; color_mode = -1;
    if (usable(status.display_color_space)) {
        int m = vitabrightColorSpaceGetMode();
        if (m == 0 || m == 1) { color_mode = m; color_ok = 1; }
    }
    lut_ok = 0;
    if (status.hardware == VBE_HW_OLED && status.brightness_core == VBE_CAP_ACTIVE && status.brightness_table == VBE_CAP_ACTIVE) {
        lut_ok = vitabrightOledGetLut(oled_lut) >= 0;
        if (cursor >= LUT_SIZE) cursor = LUT_SIZE - 1;
    } else if (status.hardware == VBE_HW_LCD && status.brightness_core == VBE_CAP_ACTIVE && status.brightness_table == VBE_CAP_ACTIVE) {
        lut_ok = vitabrightLcdGetBrightnessValues(lcd_lut) >= 0;
        if (cursor >= LCD_LUT_LEVELS) cursor = LCD_LUT_LEVELS - 1;
    } else cursor = 0;
    return 0;
}

static void save_lut(void) {
    if (!lut_ok || status.brightness_table != VBE_CAP_ACTIVE) { set_msg("No committed brightness table is available to persist."); return; }
    int r = status.hardware == VBE_HW_OLED ? vitabrightOledPersistLut() : status.hardware == VBE_HW_LCD ? vitabrightLcdPersistBrightnessValues() : -1;
    set_msg(r < 0 ? "Atomic LUT persistence failed; existing file retained." : "Committed LUT atomically persisted to its authoritative source.");
    refresh();
}

static void toggle_invert(void) {
    if (!filter_ok || !usable(status.invert)) { set_msg("Invert is unavailable."); return; }
    ScreenFilterParams p = filter;
    p.cct = CCT_DEFAULT; p.gamma = 1.0f; p.contrast = 1.0f; p.brightness = 0.0f; p.panel_enhance = 0; p.invert = !p.invert;
    int r = vitabrightFilterSetParams(&p, status.hardware == VBE_HW_OLED);
    set_msg(r < 0 ? "Invert failed; previous state retained." : p.invert ? "Invert enabled." : "Invert disabled.");
    refresh();
}

static void toggle_color(void) {
    if (!color_ok || !usable(status.display_color_space)) { set_msg("Panel color-space control is unavailable."); return; }
    int wanted = color_mode ? 0 : 1;
    int r = vitabrightColorSpaceSetMode(wanted);
    set_msg(r < 0 ? "Color-space write/read-back failed; previous state retained." : wanted ? "Alternate panel color-space enabled." : "Panel color-space mode 0 selected.");
    refresh();
}

static void edit(int delta) {
    if (!lut_ok || status.brightness_table != VBE_CAP_ACTIVE) { set_msg("Brightness table editing is unavailable."); return; }
    if (status.hardware == VBE_HW_OLED) {
        int v = (int)oled_lut[cursor] + delta;
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        unsigned char old = oled_lut[cursor]; oled_lut[cursor] = (unsigned char)v;
        if (vitabrightOledSetLut(oled_lut) < 0) { oled_lut[cursor] = old; set_msg("OLED LUT update rejected; previous table retained."); }
        else set_msg("OLED LUT updated in RAM; Square atomically persists it.");
    } else if (status.hardware == VBE_HW_LCD) {
        int v = (int)lcd_lut[cursor] + delta;
        int lo = cursor ? lcd_lut[cursor - 1] : 0;
        int hi = cursor == LCD_LUT_LEVELS - 1 ? 255 : lcd_lut[cursor + 1];
        if (v < lo) v = lo;
        if (v > hi) v = hi;
        unsigned char old = lcd_lut[cursor]; lcd_lut[cursor] = (unsigned char)v;
        if (vitabrightLcdSetBrightnessValues(lcd_lut) < 0) { lcd_lut[cursor] = old; set_msg("LCD LUT update rejected; previous table retained."); }
        else set_msg("LCD LUT updated in RAM; Square atomically persists it.");
    }
    refresh();
}

static void line(vita2d_pgf *font, float y, unsigned c, const char *s) { vita2d_pgf_draw_text(font, 24.0f, y, c, 1.0f, s); }

static void draw(vita2d_pgf *font) {
    char b[160]; float y = 35.0f;
    vita2d_start_drawing(); vita2d_clear_screen();
    line(font, y, WHITE, "VitaBrightEX pseudo-v1.4 capability editor"); y += 28;
    if (!status_ok) { line(font, y, BAD, "Plugin status ABI unavailable."); y += 28; line(font, y, DIM, msg); goto out; }
    snprintf(b, sizeof(b), "Hardware: %s  firmware: 0x%08X  ABI: %u", status.hardware == VBE_HW_OLED ? "PCH-1000 OLED" : status.hardware == VBE_HW_LCD ? "PCH-2000 LCD" : "unknown", (unsigned)status.firmware, (unsigned)status.abi_version); line(font, y, WHITE, b); y += 24;
    snprintf(b, sizeof(b), "Core=%s  table=%s  layout=%s  lock=%s", cap(status.brightness_core), cap(status.brightness_table), cap(status.firmware_layout), cap(status.state_lock)); line(font, y, WHITE, b); y += 24;
    snprintf(b, sizeof(b), "Brightness hook=%s  power hook=%s  invert=%s", cap(status.brightness_hook), cap(status.power_limit_hook), cap(status.invert)); line(font, y, WHITE, b); y += 24;
    snprintf(b, sizeof(b), "Panel color-space=%s mode=%s  CSC=%s transfer=%s", cap(status.display_color_space), color_ok ? (color_mode ? "1" : "0") : "n/a", cap(status.csc_filter), cap(status.transfer_lut)); line(font, y, color_ok ? WHITE : DIM, b); y += 24;
    snprintf(b, sizeof(b), "Last kernel error: %d detail: 0x%08X", status.last_error, (unsigned)status.last_error_detail); line(font, y, status.last_error ? BAD : OK, b); y += 32;
    if (status.hardware == VBE_HW_OLED) {
        snprintf(b, sizeof(b), "Panel type: %d", status.panel_type); line(font, y, WHITE, b); y += 24;
        if (lut_ok) { snprintf(b, sizeof(b), "OLED LUT byte %d/%d (row %d col %d): 0x%02X", cursor + 1, LUT_SIZE, cursor / LUT_LINE_SIZE, cursor % LUT_LINE_SIZE, oled_lut[cursor]); line(font, y, WHITE, b); y += 24; }
    } else if (status.hardware == VBE_HW_LCD && lut_ok) {
        snprintf(b, sizeof(b), "LCD LUT entry %d/%d: %u", cursor, LCD_LUT_LEVELS - 1, (unsigned)lcd_lut[cursor]); line(font, y, WHITE, b); y += 24;
    }
    line(font, y, DIM, "Left/Right select | Up/Down edit | X invert | Triangle color-space | Square save"); y += 24;
    line(font, y, DIM, "Circle reload | Select refresh | Start exit"); y += 24;
    line(font, y, DIM, "CCT/gamma/contrast/panel curves stay disabled when status reports unsupported."); y += 30;
    if (msg[0]) line(font, y, WHITE, msg);
out:
    vita2d_end_drawing(); vita2d_swap_buffers();
}

int main(void) {
    vita2d_init(); vita2d_set_clear_color(RGBA8(32,32,36,255));
    vita2d_pgf *font = vita2d_load_default_pgf();
    if (!font) { vita2d_fini(); sceKernelExitProcess(-1); return -1; }
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG); refresh();
    SceCtrlData pad = {0}, old = {0}; int running = 1;
    while (running) {
        sceCtrlPeekBufferPositive(0, &pad, 1); unsigned p = pad.buttons & ~old.buttons;
        int count = status.hardware == VBE_HW_OLED ? LUT_SIZE : LCD_LUT_LEVELS;
        if (count < 1) count = 1;
        if (p & SCE_CTRL_LEFT) cursor = (cursor + count - 1) % count;
        if (p & SCE_CTRL_RIGHT) cursor = (cursor + 1) % count;
        if (p & SCE_CTRL_UP) edit(1);
        if (p & SCE_CTRL_DOWN) edit(-1);
        if (p & SCE_CTRL_CROSS) toggle_invert();
        if (p & SCE_CTRL_TRIANGLE) toggle_color();
        if (p & SCE_CTRL_SELECT) { refresh(); set_msg("Status refreshed."); }
        if (p & SCE_CTRL_CIRCLE) { int r = vitabrightReload(); refresh(); set_msg(r < 0 ? "Reload retained previous state where a request failed." : "Reload successful."); }
        if (p & SCE_CTRL_SQUARE) save_lut();
        if (p & SCE_CTRL_START) running = 0;
        draw(font); old = pad;
    }
    vita2d_free_pgf(font); vita2d_fini(); sceKernelExitProcess(0); return 0;
}
