#include <stdio.h>
#include <string.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#include "../status.h"
#include "../screen_filter.h"
#include "../lcd/lcd_lut.h"
#include "../oled/lut.h"

#define COLOR_WHITE RGBA8(255, 255, 255, 255)
#define COLOR_DIM   RGBA8(170, 170, 170, 255)
#define COLOR_BAD   RGBA8(255, 150, 150, 255)
#define COLOR_OK    RGBA8(160, 255, 180, 255)

int vitabrightReload(void);
int vitabrightColorSpaceGetMode(void);
int vitabrightColorSpaceSetMode(int mode);
int vitabrightOledGetLut(unsigned char out[LUT_SIZE]);
int vitabrightOledSetLut(unsigned char in[LUT_SIZE]);
int vitabrightLcdGetBrightnessValues(unsigned char out[LCD_LUT_LEVELS]);
int vitabrightLcdSetBrightnessValues(unsigned char in[LCD_LUT_LEVELS]);

static VitaBrightStatus g_status;
static ScreenFilterParams g_filter;
static unsigned char g_oled_lut[LUT_SIZE];
static unsigned char g_lcd_lut[LCD_LUT_LEVELS];
static int g_status_ok = 0;
static int g_filter_ok = 0;
static int g_lut_ok = 0;
static int g_color_ok = 0;
static int g_color_mode = -1;
static int g_cursor = 0;
static char g_notice[96] = "";

static const char *cap_name(int state) {
    switch (state) {
    case VBE_CAP_UNAVAILABLE: return "unavailable";
    case VBE_CAP_INACTIVE: return "inactive";
    case VBE_CAP_ACTIVE: return "active";
    case VBE_CAP_FAILED: return "FAILED";
    case VBE_CAP_UNSUPPORTED: return "unsupported";
    default: return "unknown";
    }
}

static int cap_usable(int state) {
    return state == VBE_CAP_ACTIVE || state == VBE_CAP_INACTIVE;
}

static void notice(const char *text) {
    snprintf(g_notice, sizeof(g_notice), "%s", text);
}

static int refresh_state(void) {
    memset(&g_status, 0, sizeof(g_status));
    g_status_ok = vitabrightGetStatus(&g_status) >= 0 && g_status.abi_version >= 2;
    if (!g_status_ok) {
        notice("Cannot read VitaBrightEX v1.4 status ABI.");
        g_filter_ok = 0;
        g_lut_ok = 0;
        g_color_ok = 0;
        g_color_mode = -1;
        return -1;
    }

    g_filter_ok = vitabrightFilterGetParams(&g_filter) >= 0;

    g_color_ok = 0;
    g_color_mode = -1;
    if (cap_usable(g_status.display_color_space)) {
        int mode = vitabrightColorSpaceGetMode();
        if (mode == 0 || mode == 1) {
            g_color_mode = mode;
            g_color_ok = 1;
        }
    }

    g_lut_ok = 0;
    if (g_status.hardware == VBE_HW_OLED &&
        g_status.brightness_core == VBE_CAP_ACTIVE &&
        g_status.brightness_table == VBE_CAP_ACTIVE) {
        g_lut_ok = vitabrightOledGetLut(g_oled_lut) >= 0;
        if (g_cursor >= LUT_SIZE) g_cursor = LUT_SIZE - 1;
    } else if (g_status.hardware == VBE_HW_LCD &&
               g_status.brightness_core == VBE_CAP_ACTIVE &&
               g_status.brightness_table == VBE_CAP_ACTIVE) {
        g_lut_ok = vitabrightLcdGetBrightnessValues(g_lcd_lut) >= 0;
        if (g_cursor >= LCD_LUT_LEVELS) g_cursor = LCD_LUT_LEVELS - 1;
    } else {
        g_cursor = 0;
    }
    return 0;
}

static int persist_oled(void) {
    const char *primary = "ur0:/tai/vitabright_lut.txt";
    const char *fallback = "ux0:/tai/vitabright_lut.txt";
    if (g_status.panel_type == OLED_PANEL_4) {
        primary = "ur0:/tai/vitabright_lut_p4.txt";
        fallback = "ux0:/tai/vitabright_lut_p4.txt";
    } else if (g_status.panel_type == OLED_PANEL_5) {
        primary = "ur0:/tai/vitabright_lut_p5.txt";
        fallback = "ux0:/tai/vitabright_lut_p5.txt";
    } else if (g_status.panel_type == OLED_PANEL_6) {
        primary = "ur0:/tai/vitabright_lut_p6.txt";
        fallback = "ux0:/tai/vitabright_lut_p6.txt";
    }

    FILE *f = fopen(primary, "w");
    if (!f) f = fopen(fallback, "w");
    if (!f) return -1;

    for (int row = 0; row < LUT_ROWS; ++row) {
        for (int col = 0; col < LUT_LINE_SIZE; ++col) {
            fprintf(f, col == LUT_LINE_SIZE - 1 ? "%02X\n" : "%02X ",
                    g_oled_lut[row * LUT_LINE_SIZE + col]);
        }
    }
    return fclose(f);
}

static int persist_lcd(void) {
    FILE *f = fopen("ur0:/tai/vitabright_lcd_lut.txt", "w");
    if (!f) f = fopen("ux0:/tai/vitabright_lcd_lut.txt", "w");
    if (!f) return -1;
    for (int i = 0; i < LCD_LUT_LEVELS; ++i)
        fprintf(f, "%u\n", (unsigned)g_lcd_lut[i]);
    return fclose(f);
}

static void toggle_invert(void) {
    if (!g_filter_ok || !cap_usable(g_status.invert)) {
        notice("Invert is not available on this system.");
        return;
    }

    ScreenFilterParams candidate = g_filter;
    /* Unsupported filter dimensions are kept neutral so this operation is
     * all-or-nothing: only the independently verified invert bit changes. */
    candidate.cct = CCT_DEFAULT;
    candidate.gamma = 1.0f;
    candidate.contrast = 1.0f;
    candidate.brightness = 0.0f;
    candidate.panel_enhance = 0;
    candidate.invert = !candidate.invert;

    int ret = vitabrightFilterSetParams(&candidate,
        g_status.hardware == VBE_HW_OLED ? 1 : 0);
    if (ret < 0) notice("Invert apply failed; previous state retained.");
    else notice(candidate.invert ? "Invert enabled." : "Invert disabled.");
    refresh_state();
}

static void toggle_color_space(void) {
    if (!g_color_ok || !cap_usable(g_status.display_color_space)) {
        notice("Panel color-space control is not available.");
        return;
    }

    int desired = g_color_mode ? 0 : 1;
    int ret = vitabrightColorSpaceSetMode(desired);
    if (ret < 0) notice("Color-space write/read-back failed; previous state retained.");
    else notice(desired ? "Alternate panel color-space enabled." : "Panel color-space mode 0 selected.");
    refresh_state();
}

static void edit_value(int delta) {
    if (!g_lut_ok || g_status.brightness_table != VBE_CAP_ACTIVE) {
        notice("Brightness table editing is unavailable.");
        return;
    }

    if (g_status.hardware == VBE_HW_OLED) {
        int value = (int)g_oled_lut[g_cursor] + delta;
        if (value < 0) value = 0;
        if (value > 255) value = 255;
        unsigned char old = g_oled_lut[g_cursor];
        g_oled_lut[g_cursor] = (unsigned char)value;
        int ret = vitabrightOledSetLut(g_oled_lut);
        if (ret < 0) {
            g_oled_lut[g_cursor] = old;
            notice("OLED LUT update rejected; previous table retained.");
        } else {
            notice("OLED LUT updated in RAM; Square persists to disk.");
        }
    } else if (g_status.hardware == VBE_HW_LCD) {
        int value = (int)g_lcd_lut[g_cursor] + delta;
        int min = g_cursor == 0 ? 0 : g_lcd_lut[g_cursor - 1];
        int max = g_cursor == LCD_LUT_LEVELS - 1 ? 255 : g_lcd_lut[g_cursor + 1];
        if (value < min) value = min;
        if (value > max) value = max;
        unsigned char old = g_lcd_lut[g_cursor];
        g_lcd_lut[g_cursor] = (unsigned char)value;
        int ret = vitabrightLcdSetBrightnessValues(g_lcd_lut);
        if (ret < 0) {
            g_lcd_lut[g_cursor] = old;
            notice("LCD LUT update rejected; previous table retained.");
        } else {
            notice("LCD LUT updated in RAM; Square persists to disk.");
        }
    }
    refresh_state();
}

static void draw_line(vita2d_pgf *font, float y, unsigned color, const char *text) {
    vita2d_pgf_draw_text(font, 24.0f, y, color, 1.0f, text);
}

static void render(vita2d_pgf *font) {
    char line[160];
    float y = 35.0f;
    vita2d_start_drawing();
    vita2d_clear_screen();

    draw_line(font, y, COLOR_WHITE, "VitaBrightEX pseudo-v1.4 capability editor"); y += 28.0f;
    if (!g_status_ok) {
        draw_line(font, y, COLOR_BAD, "Plugin status ABI unavailable."); y += 28.0f;
        draw_line(font, y, COLOR_DIM, g_notice);
        vita2d_end_drawing();
        vita2d_swap_buffers();
        return;
    }

    snprintf(line, sizeof(line), "Hardware: %s   firmware: 0x%08X   status ABI: %u",
        g_status.hardware == VBE_HW_OLED ? "PCH-1000 OLED" :
        g_status.hardware == VBE_HW_LCD ? "PCH-2000 LCD" : "unknown",
        (unsigned)g_status.firmware, (unsigned)g_status.abi_version);
    draw_line(font, y, COLOR_WHITE, line); y += 24.0f;

    snprintf(line, sizeof(line), "Core=%s  table=%s  layout=%s  state-lock=%s",
        cap_name(g_status.brightness_core), cap_name(g_status.brightness_table),
        cap_name(g_status.firmware_layout), cap_name(g_status.state_lock));
    draw_line(font, y, COLOR_WHITE, line); y += 24.0f;

    snprintf(line, sizeof(line), "Brightness hook=%s  power hook=%s  invert=%s",
        cap_name(g_status.brightness_hook), cap_name(g_status.power_limit_hook),
        cap_name(g_status.invert));
    draw_line(font, y, COLOR_WHITE, line); y += 24.0f;

    snprintf(line, sizeof(line), "Panel color-space=%s  mode=%s  CSC=%s  transfer=%s",
        cap_name(g_status.display_color_space),
        g_color_ok ? (g_color_mode ? "1" : "0") : "n/a",
        cap_name(g_status.csc_filter), cap_name(g_status.transfer_lut));
    draw_line(font, y, g_color_ok ? COLOR_WHITE : COLOR_DIM, line); y += 24.0f;

    snprintf(line, sizeof(line), "Last kernel error: %d  detail: 0x%08X",
        g_status.last_error, (unsigned)g_status.last_error_detail);
    draw_line(font, y, g_status.last_error ? COLOR_BAD : COLOR_OK, line); y += 32.0f;

    if (g_status.hardware == VBE_HW_OLED) {
        snprintf(line, sizeof(line), "Panel type: %d", g_status.panel_type);
        draw_line(font, y, COLOR_WHITE, line); y += 24.0f;
        if (g_lut_ok) {
            int row = g_cursor / LUT_LINE_SIZE;
            int col = g_cursor % LUT_LINE_SIZE;
            snprintf(line, sizeof(line), "OLED LUT byte %d/%d (row %d col %d): 0x%02X",
                g_cursor + 1, LUT_SIZE, row, col, g_oled_lut[g_cursor]);
            draw_line(font, y, COLOR_WHITE, line); y += 24.0f;
        }
    } else if (g_status.hardware == VBE_HW_LCD && g_lut_ok) {
        snprintf(line, sizeof(line), "LCD brightness level %d/%d: %u",
            g_cursor, LCD_LUT_LEVELS - 1, (unsigned)g_lcd_lut[g_cursor]);
        draw_line(font, y, COLOR_WHITE, line); y += 24.0f;
    }

    draw_line(font, y, COLOR_DIM,
        "Left/Right select | Up/Down edit | X invert | Triangle color-space | Square save");
    y += 24.0f;
    draw_line(font, y, COLOR_DIM,
        "Circle reload | Select refresh | Start exit");
    y += 24.0f;
    draw_line(font, y, COLOR_DIM,
        "CCT/gamma/contrast/panel curves remain disabled when status reports unsupported.");
    y += 30.0f;
    if (g_notice[0]) draw_line(font, y, COLOR_WHITE, g_notice);

    vita2d_end_drawing();
    vita2d_swap_buffers();
}

int main(void) {
    vita2d_init();
    vita2d_set_clear_color(RGBA8(32, 32, 36, 255));
    vita2d_pgf *font = vita2d_load_default_pgf();
    if (!font) {
        vita2d_fini();
        sceKernelExitProcess(-1);
        return -1;
    }

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    refresh_state();

    SceCtrlData pad = {0};
    SceCtrlData old = {0};
    int running = 1;
    while (running) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        unsigned pressed = pad.buttons & ~old.buttons;

        int count = g_status.hardware == VBE_HW_OLED ? LUT_SIZE : LCD_LUT_LEVELS;
        if (count < 1) count = 1;
        if (pressed & SCE_CTRL_LEFT) g_cursor = (g_cursor + count - 1) % count;
        if (pressed & SCE_CTRL_RIGHT) g_cursor = (g_cursor + 1) % count;
        if (pressed & SCE_CTRL_UP) edit_value(1);
        if (pressed & SCE_CTRL_DOWN) edit_value(-1);
        if (pressed & SCE_CTRL_CROSS) toggle_invert();
        if (pressed & SCE_CTRL_TRIANGLE) toggle_color_space();
        if (pressed & SCE_CTRL_SELECT) { refresh_state(); notice("Status refreshed."); }
        if (pressed & SCE_CTRL_CIRCLE) {
            int ret = vitabrightReload();
            refresh_state();
            notice(ret < 0 ? "Reload completed with a requested capability unavailable." : "Reload successful.");
        }
        if (pressed & SCE_CTRL_SQUARE) {
            int ret = -1;
            if (g_lut_ok && g_status.hardware == VBE_HW_OLED) ret = persist_oled();
            else if (g_lut_ok && g_status.hardware == VBE_HW_LCD) ret = persist_lcd();
            notice(ret < 0 ? "Could not persist LUT file." : "LUT persisted to tai directory.");
        }
        if (pressed & SCE_CTRL_START) running = 0;

        render(font);
        old = pad;
    }

    vita2d_free_pgf(font);
    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}
