#include <stdio.h>
#include <string.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#include "../build_info.h"
#include "../status.h"
#include "../screen_filter.h"
#include "../lcd/lcd_lut.h"
#include "../oled/lut.h"
#include "../oled/transform_state.h"

#ifndef VBE_BUILD_ID
#define VBE_BUILD_ID "unknown"
#endif

#define WHITE RGBA8(255,255,255,255)
#define DIM RGBA8(170,170,170,255)
#define BAD RGBA8(255,150,150,255)
#define OK RGBA8(160,255,180,255)

int vitabrightReload(void);
int vitabrightGetDiagnostics(VitaBrightDiagnostics *out);
int vitabrightGetBuildId(char out[VBE_BUILD_ID_SIZE]);
int vitabrightColorSpaceGetMode(void);
int vitabrightColorSpaceSetMode(int mode);
int vitabrightOledGetLut(unsigned char out[LUT_SIZE]);
int vitabrightOledSetLut(unsigned char in[LUT_SIZE]);
int vitabrightOledPersistLut(void);
int vitabrightOledGetTransformState(VitaBrightOledTransformState *out);
int vitabrightLcdGetBrightnessValues(unsigned char out[LCD_LUT_LEVELS]);
int vitabrightLcdSetBrightnessValues(unsigned char in[LCD_LUT_LEVELS]);
int vitabrightLcdPersistBrightnessValues(void);
int vitabrightFilterGetState(VitaBrightDisplayFilterState *out);

static VitaBrightStatus status;
static VitaBrightDiagnostics diagnostics;
static VitaBrightDisplayFilterState filter_state;
static VitaBrightOledTransformState oled_transform;
static unsigned char oled_lut[LUT_SIZE];
static unsigned char lcd_lut[LCD_LUT_LEVELS];
static char plugin_build[VBE_BUILD_ID_SIZE];
static int status_ok, diagnostics_ok, build_ok, build_match;
static int filter_ok, oled_transform_ok, lut_ok, color_ok, color_mode = -1, cursor;
static char msg[112];

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
static int brightness_operational(void) {
    return status.brightness_core == VBE_CAP_ACTIVE &&
           status.brightness_table == VBE_CAP_ACTIVE &&
           status.brightness_hook == VBE_CAP_ACTIVE &&
           status.power_limit_hook == VBE_CAP_ACTIVE;
}
static int brightness_recovery_failed(void) {
    if (!brightness_operational()) return 1;
    if (!diagnostics_ok) return 0;
    return diagnostics.brightness_error == VBE_ERR_LUT_ROLLBACK ||
           diagnostics.brightness_error == VBE_ERR_RESOURCE_RELEASE;
}

static int refresh(void) {
    memset(&status, 0, sizeof(status));
    memset(&diagnostics, 0, sizeof(diagnostics));
    memset(&filter_state, 0, sizeof(filter_state));
    memset(&oled_transform, 0, sizeof(oled_transform));
    memset(plugin_build, 0, sizeof(plugin_build));
    status_ok = vitabrightGetStatus(&status) >= 0 && status.abi_version >= 2;
    if (!status_ok) {
        diagnostics_ok = build_ok = build_match = filter_ok = 0;
        oled_transform_ok = lut_ok = color_ok = 0;
        color_mode = -1;
        set_msg("Cannot read VitaBrightEX v1.4 status ABI.");
        return -1;
    }
    diagnostics_ok = vitabrightGetDiagnostics(&diagnostics) >= 0 && diagnostics.abi_version == 1;
    build_ok = vitabrightGetBuildId(plugin_build) >= 0;
    plugin_build[VBE_BUILD_ID_SIZE - 1] = '\0';
    build_match = build_ok && strcmp(plugin_build, VBE_BUILD_ID) == 0;
    filter_ok = vitabrightFilterGetState(&filter_state) >= 0 &&
                filter_state.abi_version == VBE_DISPLAY_FILTER_STATE_ABI_VERSION;
    oled_transform_ok = 0;
    if (status.hardware == VBE_HW_OLED && status.brightness_core == VBE_CAP_ACTIVE)
        oled_transform_ok = vitabrightOledGetTransformState(&oled_transform) >= 0 &&
            oled_transform.abi_version == VBE_OLED_TRANSFORM_STATE_ABI_VERSION;
    color_ok = 0;
    color_mode = -1;
    if (usable(status.display_color_space)) {
        int mode = vitabrightColorSpaceGetMode();
        if (mode == 0 || mode == 1) { color_mode = mode; color_ok = 1; }
    }
    lut_ok = 0;
    if (status.hardware == VBE_HW_OLED && status.brightness_core == VBE_CAP_ACTIVE &&
        status.brightness_table == VBE_CAP_ACTIVE) {
        lut_ok = vitabrightOledGetLut(oled_lut) >= 0;
        if (cursor >= LUT_SIZE) cursor = LUT_SIZE - 1;
    } else if (status.hardware == VBE_HW_LCD && status.brightness_core == VBE_CAP_ACTIVE &&
               status.brightness_table == VBE_CAP_ACTIVE) {
        lut_ok = vitabrightLcdGetBrightnessValues(lcd_lut) >= 0;
        if (cursor >= LCD_LUT_LEVELS) cursor = LCD_LUT_LEVELS - 1;
    } else cursor = 0;
    return 0;
}

static void save_lut(void) {
    if (!lut_ok || status.brightness_table != VBE_CAP_ACTIVE) {
        set_msg("No committed brightness table is available to persist."); return;
    }
    int r = status.hardware == VBE_HW_OLED ? vitabrightOledPersistLut()
          : status.hardware == VBE_HW_LCD ? vitabrightLcdPersistBrightnessValues() : -1;
    refresh();
    if (r == VBE_RESULT_NO_FILE_SOURCE)
        set_msg("Compiled fallback LUT has no authoritative file to overwrite.");
    else if (r < 0)
        set_msg("Atomic LUT persistence failed; see brightness diagnostics.");
    else
        set_msg("Committed base LUT atomically persisted to its authoritative source.");
}

static void toggle_color(void) {
    if (!color_ok || !usable(status.display_color_space)) {
        set_msg("Panel color-space control is unavailable."); return;
    }
    int wanted = color_mode ? 0 : 1;
    int r = vitabrightColorSpaceSetMode(wanted);
    refresh();
    if (r < 0) set_msg("Color-space write/read-back failed; inspect diagnostics.");
    else set_msg(wanted ? "Alternate panel color-space enabled."
                        : "Panel color-space mode 0 selected.");
}

static void report_lut_result(int r, int is_oled) {
    refresh();
    if (r == VBE_RESULT_UNSUPPORTED) {
        set_msg(is_oled ? "Base LUT committed; requested OLED transform remains unsupported on this panel."
                        : "Requested capability is unsupported.");
        return;
    }
    if (r >= 0) {
        set_msg(is_oled ? "OLED base LUT updated in RAM; Square persists the base."
                        : "LCD LUT updated in RAM; Square atomically persists it.");
        return;
    }
    if (brightness_recovery_failed())
        set_msg("LUT update failed and recovery failed; backend is degraded.");
    else
        set_msg("LUT update rejected; previous committed hardware state retained/restored.");
}

static void edit(int delta) {
    if (!lut_ok || status.brightness_table != VBE_CAP_ACTIVE) {
        set_msg("Brightness table editing is unavailable."); return;
    }
    if (status.hardware == VBE_HW_OLED) {
        int v = (int)oled_lut[cursor] + delta;
        if (v < 0) v = 0; if (v > 255) v = 255;
        oled_lut[cursor] = (unsigned char)v;
        report_lut_result(vitabrightOledSetLut(oled_lut), 1);
    } else if (status.hardware == VBE_HW_LCD) {
        int v = (int)lcd_lut[cursor] + delta;
        int lo = cursor ? lcd_lut[cursor - 1] : 0;
        int hi = cursor == LCD_LUT_LEVELS - 1 ? 255 : lcd_lut[cursor + 1];
        if (v < lo) v = lo; if (v > hi) v = hi;
        lcd_lut[cursor] = (unsigned char)v;
        report_lut_result(vitabrightLcdSetBrightnessValues(lcd_lut), 0);
    }
}

static void line(vita2d_pgf *font, float y, unsigned c, const char *s) {
    vita2d_pgf_draw_text(font, 24.0f, y, c, 1.0f, s);
}

static void draw(vita2d_pgf *font) {
    char b[190]; float y = 28.0f;
    vita2d_start_drawing(); vita2d_clear_screen();
    line(font, y, WHITE, "VitaBrightEX pseudo-v1.4 capability editor"); y += 21;
    if (!status_ok) { line(font, y, BAD, "Plugin status ABI unavailable."); y += 21; line(font, y, DIM, msg); goto out; }
    snprintf(b, sizeof(b), "Build plugin=%s editor=%s %s", build_ok ? plugin_build : "unavailable", VBE_BUILD_ID, build_match ? "MATCH" : "MISMATCH");
    line(font, y, build_match ? OK : BAD, b); y += 21;
    snprintf(b, sizeof(b), "Hardware: %s firmware=0x%08X ABI=%u", status.hardware == VBE_HW_OLED ? "PCH-1000 OLED" : status.hardware == VBE_HW_LCD ? "PCH-2000 LCD" : "unknown", (unsigned)status.firmware, (unsigned)status.abi_version); line(font, y, WHITE, b); y += 21;
    snprintf(b, sizeof(b), "Core=%s table=%s layout=%s lock=%s", cap(status.brightness_core), cap(status.brightness_table), cap(status.firmware_layout), cap(status.state_lock)); line(font, y, WHITE, b); y += 21;
    snprintf(b, sizeof(b), "Brightness hook=%s power=%s invert=%s", cap(status.brightness_hook), cap(status.power_limit_hook), cap(status.invert)); line(font, y, WHITE, b); y += 21;
    snprintf(b, sizeof(b), "Color-space=%s mode=%s CSC=%s transfer=%s", cap(status.display_color_space), color_ok ? (color_mode ? "1" : "0") : "n/a", cap(status.csc_filter), cap(status.transfer_lut)); line(font, y, color_ok ? WHITE : DIM, b); y += 21;
    if (filter_ok) {
        snprintf(b, sizeof(b), "Generic filter req=%X committed=%X unsupported=%X failed=%X", (unsigned)filter_state.requested_domains, (unsigned)filter_state.committed_domains, (unsigned)filter_state.unsupported_domains, (unsigned)filter_state.failed_domains); line(font, y, filter_state.failed_domains ? BAD : DIM, b); y += 21;
    }
    if (status.hardware == VBE_HW_OLED && oled_transform_ok) {
        snprintf(b, sizeof(b), "OLED bias req=(%d,%d,%d) applied=(%d,%d,%d) result=%d", oled_transform.requested_bias.r_offset, oled_transform.requested_bias.g_offset, oled_transform.requested_bias.b_offset, oled_transform.applied_bias.r_offset, oled_transform.applied_bias.g_offset, oled_transform.applied_bias.b_offset, oled_transform.capability_result); line(font, y, oled_transform.capability_result == VBE_RESULT_UNSUPPORTED ? DIM : WHITE, b); y += 21;
        snprintf(b, sizeof(b), "OLED warm req=%d row=%d (%d,%d,%d) applied=%d row=%d source=%d", oled_transform.requested_warm.enabled, oled_transform.requested_warm.first_row, oled_transform.requested_warm.r_offset, oled_transform.requested_warm.g_offset, oled_transform.requested_warm.b_offset, oled_transform.applied_warm.enabled, oled_transform.applied_warm.first_row, oled_transform.source_kind); line(font, y, WHITE, b); y += 21;
    }
    snprintf(b, sizeof(b), "Last error=%d detail=0x%08X", status.last_error, (unsigned)status.last_error_detail); line(font, y, status.last_error ? BAD : OK, b); y += 21;
    if (diagnostics_ok) {
        snprintf(b, sizeof(b), "Errors cfg=%d bright=%d color=%d filter=%d input=%d sync=%d", diagnostics.config_error, diagnostics.brightness_error, diagnostics.color_space_error, diagnostics.filter_error, diagnostics.input_error, diagnostics.synchronization_error); line(font, y, WHITE, b); y += 21;
    }
    if (status.hardware == VBE_HW_OLED) {
        snprintf(b, sizeof(b), "Panel type: %d", status.panel_type); line(font, y, WHITE, b); y += 21;
        if (lut_ok) { snprintf(b, sizeof(b), "OLED base byte %d/%d row=%d col=%d: 0x%02X", cursor + 1, LUT_SIZE, cursor / LUT_LINE_SIZE, cursor % LUT_LINE_SIZE, oled_lut[cursor]); line(font, y, WHITE, b); y += 21; }
    } else if (status.hardware == VBE_HW_LCD && lut_ok) {
        snprintf(b, sizeof(b), "LCD LUT entry %d/%d: %u", cursor, LCD_LUT_LEVELS - 1, (unsigned)lcd_lut[cursor]); line(font, y, WHITE, b); y += 21;
    }
    line(font, y, DIM, "Left/Right select | Up/Down edit | Triangle color-space | Square save"); y += 21;
    line(font, y, DIM, "Circle reload | Select refresh | Start exit"); y += 21;
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
        if (p & SCE_CTRL_TRIANGLE) toggle_color();
        if (p & SCE_CTRL_SELECT) { refresh(); set_msg("Status refreshed."); }
        if (p & SCE_CTRL_CIRCLE) {
            int r = vitabrightReload(); refresh();
            if (r == VBE_RESULT_UNSUPPORTED)
                set_msg("Reload reconciled supported domains; one or more accepted requests are unsupported.");
            else if (r < 0)
                set_msg("Reload fail-open: accepted request retained; inspect domain diagnostics.");
            else set_msg("Reload successful.");
        }
        if (p & SCE_CTRL_SQUARE) save_lut();
        if (p & SCE_CTRL_START) running = 0;
        draw(font); old = pad;
    }
    vita2d_free_pgf(font); vita2d_fini(); sceKernelExitProcess(0); return 0;
}
