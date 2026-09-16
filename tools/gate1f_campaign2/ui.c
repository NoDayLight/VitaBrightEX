#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "campaign2.h"

#define WIDTH 960
#define HEIGHT 544
#define PITCH 1024
#define FB_BYTES (PITCH * HEIGHT * 4u)
#define FB_ALIGN 0x40000u

static uint32_t *g_fb;
static SceUID g_fb_uid = -1;
static SceDisplayFrameBuf g_old_fb;
static int g_old_fb_valid;
static char g_title[32] = "SOURCE CHECK";
static char g_state[24] = "STATE NEUTRAL";

uint32_t c2_pack_rgb(uint32_t r, uint32_t g, uint32_t b) {
    return 0xFF000000u | ((b & 0xFFu) << 16) | ((g & 0xFFu) << 8) | (r & 0xFFu);
}

static const uint8_t digits[10][7] = {
    {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},{2,6,10,18,31,2,2},
    {31,16,16,30,1,1,30},{14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},{14,17,17,15,1,1,14}
};
static const uint8_t alpha[26][7] = {
    {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},{30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},{7,2,2,2,2,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},{30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}
};

static uint8_t glyph_row(char c, int row) {
    if (c >= '0' && c <= '9') return digits[c - '0'][row];
    if (c >= 'A' && c <= 'Z') return alpha[c - 'A'][row];
    if (c == '-') return row == 3 ? 31 : 0;
    if (c == ':') return (row == 2 || row == 4) ? 4 : 0;
    if (c == '/') return (uint8_t)(1u << (4 - (row * 4 / 6)));
    return 0;
}

static void put_px(int x, int y, uint32_t c) {
    if ((unsigned)x < WIDTH && (unsigned)y < HEIGHT) g_fb[y * PITCH + x] = c;
}
static void fill_rect(int x, int y, int w, int h, uint32_t c) {
    int yy, xx;
    for (yy = y; yy < y + h; ++yy)
        for (xx = x; xx < x + w; ++xx) put_px(xx, yy, c);
}
static void border_rect(int x, int y, int w, int h, uint32_t c) {
    fill_rect(x,y,w,2,c); fill_rect(x,y+h-2,w,2,c);
    fill_rect(x,y,2,h,c); fill_rect(x+w-2,y,2,h,c);
}
static void draw_char(int x, int y, char ch, uint32_t c, int scale) {
    int r,col,sx,sy;
    for (r=0;r<7;++r) {
        uint8_t bits=glyph_row(ch,r);
        for (col=0;col<5;++col) if (bits & (1u << (4-col)))
            for (sy=0;sy<scale;++sy) for (sx=0;sx<scale;++sx)
                put_px(x+col*scale+sx,y+r*scale+sy,c);
    }
}
static void draw_text(int x, int y, const char *s, uint32_t c, int scale) {
    while (*s) { if (*s != ' ') draw_char(x,y,*s,c,scale); x += 6*scale; ++s; }
}
static void draw_patch(int x,int y,int w,int h,uint32_t color,const char *label) {
    uint32_t white=c2_pack_rgb(255,255,255);
    fill_rect(x,y,w,h,color); border_rect(x,y,w,h,white); draw_text(x+4,y+h+5,label,white,1);
}

void c2_ui_set_title(const char *s) {
    snprintf(g_title,sizeof(g_title),"%s",s ? s : "-");
}
void c2_ui_set_state(const char *s) {
    snprintf(g_state,sizeof(g_state),"%s",s ? s : "-");
}
void c2_ui_draw(const char *prompt,const char *value,const char *extra) {
    uint32_t bg=c2_pack_rgb(10,10,10), white=c2_pack_rgb(255,255,255);
    fill_rect(0,0,WIDTH,HEIGHT,bg);
    draw_text(18,12,"GATE 1F CAMPAIGN 2 R3",white,2);
    draw_text(18,38,g_title,white,2);
    draw_text(650,38,g_state,white,1);
    draw_patch(20,70,280,58,c2_pack_rgb(128,0,0),"R50");
    draw_patch(340,70,280,58,c2_pack_rgb(0,128,0),"G50");
    draw_patch(660,70,280,58,c2_pack_rgb(0,0,128),"B50");
    draw_patch(20,166,280,58,c2_pack_rgb(128,128,0),"YELLOW50");
    draw_patch(340,166,280,58,c2_pack_rgb(128,0,128),"MAGENTA50");
    draw_patch(660,166,280,58,c2_pack_rgb(0,128,128),"CYAN50");
    draw_patch(180,262,280,58,c2_pack_rgb(128,128,128),"GRAY50");
    draw_patch(500,262,280,58,c2_pack_rgb(0,0,0),"BLACK");
    fill_rect(0,356,WIDTH,188,c2_pack_rgb(20,20,20));
    if (prompt) draw_text(18,368,prompt,white,2);
    if (value) draw_text(18,400,value,white,2);
    if (extra) draw_text(18,430,extra,white,1);
    draw_text(18,474,"SQUARE A B   LEFT RIGHT SELECT",white,1);
    draw_text(18,494,"X SAVE ANSWER   TRI ABORT",white,1);
    draw_text(18,514,"NEW QUESTION STARTS UNSELECTED",white,1);
    sceDisplayWaitVblankStart();
}

uint32_t c2_wait_button(uint32_t mask) {
    SceCtrlData pad;
    uint32_t previous=0;
    for (;;) {
        sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);
        memset(&pad,0,sizeof(pad)); sceCtrlPeekBufferPositive(0,&pad,1);
        if ((pad.buttons & mask) == 0u) break;
        sceKernelDelayThread(16000);
    }
    for (;;) {
        uint32_t current,pressed;
        sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);
        memset(&pad,0,sizeof(pad)); sceCtrlPeekBufferPositive(0,&pad,1);
        current=pad.buttons&mask; pressed=current&~previous; previous=current;
        if (pressed) return pressed;
        sceKernelDelayThread(16000);
    }
}

int c2_ui_alloc(void) {
    SceSize alloc_size=(FB_BYTES+FB_ALIGN-1u)&~(FB_ALIGN-1u);
    SceDisplayFrameBuf fb;
    memset(&g_old_fb,0,sizeof(g_old_fb)); g_old_fb.size=sizeof(g_old_fb);
    g_old_fb_valid=sceDisplayGetFrameBuf(&g_old_fb,SCE_DISPLAY_SETBUF_NEXTFRAME)>=0;
    g_fb_uid=sceKernelAllocMemBlock("gate1f_c2_fb",SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,alloc_size,NULL);
    if (g_fb_uid<0) return -1;
    if (sceKernelGetMemBlockBase(g_fb_uid,(void **)&g_fb)<0 || !g_fb) return -2;
    memset(&fb,0,sizeof(fb)); fb.size=sizeof(fb); fb.base=g_fb; fb.pitch=PITCH;
    fb.pixelformat=SCE_DISPLAY_PIXELFORMAT_A8B8G8R8; fb.width=WIDTH; fb.height=HEIGHT;
    if (sceDisplaySetFrameBuf(&fb,SCE_DISPLAY_SETBUF_NEXTFRAME)<0) return -3;
    sceDisplayWaitVblankStart();
    return 0;
}
void c2_ui_restore(void) {
    if (g_old_fb_valid) { sceDisplaySetFrameBuf(&g_old_fb,SCE_DISPLAY_SETBUF_NEXTFRAME); sceDisplayWaitVblankStart(); }
    if (g_fb_uid>=0) { sceKernelFreeMemBlock(g_fb_uid); g_fb_uid=-1; g_fb=NULL; }
}
