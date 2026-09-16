#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../build_info.h"
#include "../../matrix_backend.h"

#define G1F_BUILD_ID "a637f54f"
#define LOG_PATH "ux0:data/vbe_gate1f_ctm_semantics.txt"
#define WIDTH 960
#define HEIGHT 544
#define PITCH 1024
#define FB_BYTES (PITCH * HEIGHT * 4u)
#define FB_ALIGN 0x40000u
#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

#define OBS_R 1u
#define OBS_G 2u
#define OBS_B 3u
#define OBS_MIXED 4u
#define OBS_NONE 5u
#define DIR_INCREASE 1u
#define DIR_DECREASE 2u
#define DIR_OTHER 3u
#define DIR_NONE 4u
#define ISO_YES 1u
#define ISO_NO 2u
#define ISO_AMBIG 3u

typedef struct {
    char id[4];
    uint32_t row;
    uint32_t col;
    int32_t matrix[9];
} Probe;

typedef struct {
    uint32_t patch;
    uint32_t component;
    uint32_t direction;
    uint32_t isolated;
} Observation;

static const uint32_t k_canonical[15] = {
    0,0,0x3FF,0,0x3FF,0,0x200,0,0,0x200,0,0,0,0x200
};

static const Probe k_basis_probes[9] = {
    {"D0",0,0,{256,0,0,0,512,0,0,0,512}},
    {"D1",1,1,{512,0,0,0,256,0,0,0,512}},
    {"D2",2,2,{512,0,0,0,512,0,0,0,256}},
    {"B01",0,1,{512,128,0,0,512,0,0,0,512}},
    {"B02",0,2,{512,0,128,0,512,0,0,0,512}},
    {"B10",1,0,{512,0,0,128,512,0,0,0,512}},
    {"B12",1,2,{512,0,0,0,512,128,0,0,512}},
    {"B20",2,0,{512,0,0,0,512,0,128,0,512}},
    {"B21",2,1,{512,0,0,0,512,0,0,128,512}},
};

static uint32_t *g_fb;
static SceUID g_fb_uid = -1;
static SceDisplayFrameBuf g_old_fb;
static int g_old_fb_valid;
static char g_probe_title[32] = "SOURCE CHECK";
static uint32_t g_nat0;
static uint32_t g_nat1;
static int g_campaign_started;
static Observation g_obs[10];

static uint32_t pack_rgb(uint32_t r, uint32_t g, uint32_t b) {
    return 0xFF000000u | ((b & 0xFFu) << 16) | ((g & 0xFFu) << 8) | (r & 0xFFu);
}

static const uint8_t k_digits[10][7] = {
    {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},{2,6,10,18,31,2,2},
    {31,16,16,30,1,1,30},{14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},{14,17,17,15,1,1,14}
};
static const uint8_t k_alpha[26][7] = {
    {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},{30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},{7,2,2,2,2,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},{30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}
};

static uint8_t glyph_row(char c, int row) {
    if (c >= '0' && c <= '9') return k_digits[c - '0'][row];
    if (c >= 'A' && c <= 'Z') return k_alpha[c - 'A'][row];
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
    int r, col, sx, sy;
    for (r = 0; r < 7; ++r) {
        uint8_t bits = glyph_row(ch, r);
        for (col = 0; col < 5; ++col) if (bits & (1u << (4-col)))
            for (sy = 0; sy < scale; ++sy)
                for (sx = 0; sx < scale; ++sx)
                    put_px(x + col*scale + sx, y + r*scale + sy, c);
    }
}

static void draw_text(int x, int y, const char *s, uint32_t c, int scale) {
    while (*s) {
        if (*s != ' ') draw_char(x,y,*s,c,scale);
        x += 6 * scale;
        ++s;
    }
}

static void draw_patch(int x, int y, uint32_t color, const char *label) {
    uint32_t white = pack_rgb(255,255,255);
    fill_rect(x,y,190,88,color);
    border_rect(x,y,190,88,white);
    draw_text(x+4,y+94,label,white,1);
}

static void draw_chart(const char *prompt, const char *value) {
    uint32_t bg = pack_rgb(10,10,10), white = pack_rgb(255,255,255);
    fill_rect(0,0,WIDTH,HEIGHT,bg);
    draw_text(24,18,"GATE 1F CTM SEMANTICS",white,2);
    draw_text(24,45,g_probe_title,white,2);
    draw_patch(20,86,pack_rgb(0,0,0),"BLACK");
    draw_patch(255,86,pack_rgb(128,128,128),"GRAY50");
    draw_patch(490,86,pack_rgb(192,0,0),"RED75");
    draw_patch(725,86,pack_rgb(0,192,0),"GREEN75");
    draw_patch(20,222,pack_rgb(0,0,192),"BLUE75");
    draw_patch(255,222,pack_rgb(128,128,0),"YELLOW50");
    draw_patch(490,222,pack_rgb(128,0,128),"MAGENTA50");
    draw_patch(725,222,pack_rgb(0,128,128),"CYAN50");
    fill_rect(0,350,WIDTH,194,pack_rgb(20,20,20));
    if (prompt) draw_text(24,374,prompt,white,2);
    if (value) draw_text(24,410,value,white,2);
    draw_text(24,474,"LEFT RIGHT CHANGE",white,1);
    draw_text(24,494,"X CONFIRM",white,1);
    draw_text(24,514,"TRI ABORT",white,1);
    sceDisplayWaitVblankStart();
}

static uint32_t wait_button(void) {
    SceCtrlData pad;
    const uint32_t mask = SCE_CTRL_LEFT|SCE_CTRL_RIGHT|SCE_CTRL_CROSS|SCE_CTRL_TRIANGLE;
    uint32_t previous = 0;
    /* A held confirmation from the previous prompt must never answer the next one. */
    for (;;) {
        sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);
        memset(&pad,0,sizeof(pad));
        sceCtrlPeekBufferPositive(0,&pad,1);
        if ((pad.buttons & mask) == 0u) break;
        sceKernelDelayThread(16000);
    }
    for (;;) {
        uint32_t current, pressed;
        sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);
        memset(&pad,0,sizeof(pad));
        sceCtrlPeekBufferPositive(0,&pad,1);
        current = pad.buttons & mask;
        pressed = current & ~previous;
        previous = current;
        if (pressed) return pressed;
        sceKernelDelayThread(16000);
    }
}

static int choose(const char *prompt, const char *const *items, int n) {
    int i = 0;
    for (;;) {
        uint32_t b;
        draw_chart(prompt, items[i]);
        b = wait_button();
        if (b & SCE_CTRL_TRIANGLE) return -1;
        if (b & SCE_CTRL_CROSS) return i;
        if (b & SCE_CTRL_LEFT) i = (i + n - 1) % n;
        if (b & SCE_CTRL_RIGHT) i = (i + 1) % n;
    }
}

static int append_log(const char *fmt, ...) {
    char buf[4096];
    va_list ap;
    int n, fd, wr;
    va_start(ap,fmt);
    n = vsnprintf(buf,sizeof(buf),fmt,ap);
    va_end(ap);
    if (n < 0 || n >= (int)sizeof(buf)) return -1;
    fd = sceIoOpen(LOG_PATH,SCE_O_WRONLY|SCE_O_CREAT|SCE_O_APPEND,0666);
    if (fd < 0) return fd;
    wr = sceIoWrite(fd,buf,(SceSize)n);
    sceIoClose(fd);
    return wr == n ? 0 : -1;
}

static int words_equal(const uint32_t *a, const uint32_t *b) {
    unsigned i;
    uint32_t d = 0;
    for (i=0;i<15;++i) d |= a[i]^b[i];
    return d == 0;
}

static int append_words(char *buf, size_t cap, int off, const uint32_t *w) {
    unsigned i;
    for (i=0;i<15;++i) {
        int n = snprintf(buf+off,cap-(size_t)off,"%s%08X",i ? ",":"",w[i]);
        if (n < 0 || off + n >= (int)cap) return -1;
        off += n;
    }
    return off;
}

static int log_status(const char *tag, const char *id, int action_result,
                      const VbeMatrixBackendStatus *s) {
    char buf[4096];
    int off = snprintf(buf,sizeof(buf),
        "%s|id=%s|result=%d|requested=%u|active=%u|enabled=%u|pending=%u|reapply=%u|last=%d|tx=%u|faults=%u|masks=%u|p0_nat=%u|p0_class=%u|p0_applied=%u|p0_raw=%d|p0_source=",
        tag,id,action_result,s->requested_generation,s->active_published_generation,
        s->policy_enabled,s->pending_plane_mask,s->reapply_mode,s->last_request_result,
        s->transaction_state,s->transaction_fault_flags,s->authority_degraded_masks,
        s->planes[0].pristine_generation,s->planes[0].baseline_class,
        s->planes[0].last_forwarded_policy_generation,s->planes[0].last_sony_return);
    if (off < 0) return -1;
    off = append_words(buf,sizeof(buf),off,s->planes[0].source_words); if (off < 0) return -1;
    off += snprintf(buf+off,sizeof(buf)-(size_t)off,"|p0_forward=");
    off = append_words(buf,sizeof(buf),off,s->planes[0].forward_words); if (off < 0) return -1;
    off += snprintf(buf+off,sizeof(buf)-(size_t)off,
        "|p1_nat=%u|p1_class=%u|p1_applied=%u|p1_raw=%d|p1_source=",
        s->planes[1].pristine_generation,s->planes[1].baseline_class,
        s->planes[1].last_forwarded_policy_generation,s->planes[1].last_sony_return);
    off = append_words(buf,sizeof(buf),off,s->planes[1].source_words); if (off < 0) return -1;
    off += snprintf(buf+off,sizeof(buf)-(size_t)off,"|p1_forward=");
    off = append_words(buf,sizeof(buf),off,s->planes[1].forward_words); if (off < 0) return -1;
    if (off + 2 >= (int)sizeof(buf)) return -1;
    buf[off++]='\n'; buf[off]=0;
    return append_log("%s",buf);
}

static int status_common_ok(const VbeMatrixBackendStatus *s) {
    return s->target_supported == 1u && s->hook_owned == 1u && s->hook_fail == 0u &&
           s->pending_plane_mask == 0u && s->reapply_mode == VBE_MATRIX_REAPPLY_TAIHEN_CHAIN_REENTRY &&
           s->transaction_fault_flags == 0u && s->authority_degraded_masks == 0u &&
           s->planes[0].valid == 1u && s->planes[1].valid == 1u &&
           s->planes[0].baseline_class == VBE_B_BASELINE_CANONICAL_IDENTITY &&
           s->planes[1].baseline_class == VBE_B_BASELINE_CANONICAL_IDENTITY &&
           s->planes[0].last_sony_return == 0 && s->planes[1].last_sony_return == 0 &&
           words_equal(s->planes[0].source_words,k_canonical) &&
           words_equal(s->planes[1].source_words,k_canonical);
}

static int natural_unchanged(const VbeMatrixBackendStatus *s) {
    return s->planes[0].pristine_generation == g_nat0 &&
           s->planes[1].pristine_generation == g_nat1;
}

static void expected_forward(const Probe *p, uint32_t out[15]) {
    unsigned i;
    for (i=0;i<6;++i) out[i]=k_canonical[i];
    for (i=0;i<9;++i) out[6+i]=(uint32_t)p->matrix[i] & 0xFFFu;
}

static int applied_ok(const Probe *p, const VbeMatrixBackendStatus *s) {
    uint32_t exp[15];
    expected_forward(p,exp);
    return status_common_ok(s) && natural_unchanged(s) && s->policy_enabled == 1u &&
           s->last_request_result == VBE_MATRIX_RESULT_APPLIED &&
           s->planes[0].last_forwarded_policy_generation == s->active_published_generation &&
           s->planes[1].last_forwarded_policy_generation == s->active_published_generation &&
           words_equal(s->planes[0].forward_words,exp) && words_equal(s->planes[1].forward_words,exp);
}

static int reset_ok(const VbeMatrixBackendStatus *s) {
    return status_common_ok(s) && natural_unchanged(s) && s->policy_enabled == 0u &&
           s->last_request_result == VBE_MATRIX_RESULT_APPLIED &&
           s->planes[0].last_forwarded_policy_generation == s->active_published_generation &&
           s->planes[1].last_forwarded_policy_generation == s->active_published_generation &&
           words_equal(s->planes[0].forward_words,k_canonical) &&
           words_equal(s->planes[1].forward_words,k_canonical);
}

static void log_matrix(const Probe *p) {
    append_log("MATRIX|id=%s|row=%u|col=%u|m=%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
        p->id,p->row,p->col,p->matrix[0],p->matrix[1],p->matrix[2],p->matrix[3],p->matrix[4],
        p->matrix[5],p->matrix[6],p->matrix[7],p->matrix[8]);
}

static const char *obs_name(uint32_t v) {
    static const char *const n[] = {"INVALID","R","G","B","MIXED","NONE"};
    return v < ARRAY_COUNT(n) ? n[v] : "INVALID";
}
static const char *dir_name(uint32_t v) {
    static const char *const n[] = {"INVALID","INCREASE","DECREASE","OTHER","NONE"};
    return v < ARRAY_COUNT(n) ? n[v] : "INVALID";
}
static const char *iso_name(uint32_t v) {
    static const char *const n[] = {"INVALID","YES","NO","AMBIG"};
    return v < ARRAY_COUNT(n) ? n[v] : "INVALID";
}

static int collect_observation(const Probe *p, Observation *o) {
    static const char *const rgb[] = {"R","G","B","MIXED","NONE"};
    static const char *const dir[] = {"INCREASE","DECREASE","OTHER","NONE"};
    static const char *const iso[] = {"YES","NO","AMBIG"};
    int v;
    snprintf(g_probe_title,sizeof(g_probe_title),"PROBE %s",p->id);
    v=choose("PURE PRIMARY PATCH CHANGED",rgb,5); if(v<0)return -1; o->patch=(uint32_t)v+1u;
    v=choose("PHYSICAL COMPONENT CHANGED",rgb,5); if(v<0)return -1; o->component=(uint32_t)v+1u;
    v=choose("DIRECTION OF COMPONENT",dir,4); if(v<0)return -1; o->direction=(uint32_t)v+1u;
    v=choose("IS RESPONSE ISOLATED",iso,3); if(v<0)return -1; o->isolated=(uint32_t)v+1u;
    return 0;
}

static int run_probe(const Probe *p, Observation *o) {
    VbeMatrixRequestV1 req;
    VbeMatrixBackendStatus s;
    int ret;
    unsigned i;
    memset(&req,0,sizeof(req));
    req.size=sizeof(req); req.version=VBE_MATRIX_REQUEST_VERSION;
    for(i=0;i<9;++i) req.hardware_component_s3_9[i]=p->matrix[i];
    log_matrix(p);
    ret=vitabrightMatrixSetRequest(&req);
    memset(&s,0,sizeof(s));
    if(vitabrightMatrixGetStatus(&s)<0 || log_status("APPLY",p->id,ret,&s)<0 || ret!=VBE_MATRIX_RESULT_APPLIED || !applied_ok(p,&s))
        return -2;
    if(collect_observation(p,o)<0) return -3;
    append_log("OBS|id=%s|patch=%s|component=%s|direction=%s|isolated=%s\n",
               p->id,obs_name(o->patch),obs_name(o->component),dir_name(o->direction),iso_name(o->isolated));
    memset(&s,0,sizeof(s));
    if(vitabrightMatrixGetStatus(&s)<0 || log_status("POSTOBS",p->id,0,&s)<0 || !applied_ok(p,&s))
        return -4;
    ret=vitabrightMatrixReset();
    memset(&s,0,sizeof(s));
    if(vitabrightMatrixGetStatus(&s)<0 || log_status("RESET",p->id,ret,&s)<0 || ret!=VBE_MATRIX_RESULT_APPLIED || !reset_ok(&s))
        return -5;
    return 0;
}

static int alloc_framebuffer(void) {
    SceSize alloc_size=(FB_BYTES+FB_ALIGN-1u)&~(FB_ALIGN-1u);
    SceDisplayFrameBuf fb;
    memset(&g_old_fb,0,sizeof(g_old_fb)); g_old_fb.size=sizeof(g_old_fb);
    g_old_fb_valid = sceDisplayGetFrameBuf(&g_old_fb,SCE_DISPLAY_SETBUF_NEXTFRAME) >= 0;
    g_fb_uid=sceKernelAllocMemBlock("gate1f_fb",SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,alloc_size,NULL);
    if(g_fb_uid<0) return -1;
    if(sceKernelGetMemBlockBase(g_fb_uid,(void **)&g_fb)<0 || !g_fb) return -2;
    memset(&fb,0,sizeof(fb));
    fb.size=sizeof(fb); fb.base=g_fb; fb.pitch=PITCH; fb.pixelformat=SCE_DISPLAY_PIXELFORMAT_A8B8G8R8; fb.width=WIDTH; fb.height=HEIGHT;
    if(sceDisplaySetFrameBuf(&fb,SCE_DISPLAY_SETBUF_NEXTFRAME)<0) return -3;
    sceDisplayWaitVblankStart();
    return 0;
}

static void restore_framebuffer(void) {
    if(g_old_fb_valid) {
        sceDisplaySetFrameBuf(&g_old_fb,SCE_DISPLAY_SETBUF_NEXTFRAME);
        sceDisplayWaitVblankStart();
    }
    if(g_fb_uid>=0) { sceKernelFreeMemBlock(g_fb_uid); g_fb_uid=-1; g_fb=NULL; }
}

static int is_unambiguous_rgb(const Observation *o) {
    return o->patch>=OBS_R && o->patch<=OBS_B && o->component>=OBS_R && o->component<=OBS_B && o->isolated==ISO_YES;
}

static Probe negative_from(const Probe *positive) {
    Probe n;
    unsigned i;
    memset(&n,0,sizeof(n));
    n.id[0]='N'; n.id[1]=(char)('0'+positive->row); n.id[2]=(char)('0'+positive->col); n.id[3]=0;
    n.row=positive->row; n.col=positive->col;
    for(i=0;i<9;++i)n.matrix[i]=0;
    n.matrix[0]=n.matrix[4]=n.matrix[8]=512;
    n.matrix[n.row*3u+n.col]=-128;
    return n;
}

static void stop_and_cleanup(const char *reason, const char *id) {
    int rr=0;
    append_log("STOP|reason=%s|id=%s\n",reason,id?id:"-");
    if(g_campaign_started) rr=vitabrightMatrixReset();
    append_log("CLEANUP|reset=%d\n",rr);
    restore_framebuffer();
}

int main(void) {
    char build[VBE_BUILD_ID_SIZE]={0};
    VbeMatrixCapabilities caps;
    VbeMatrixBackendStatus pre;
    uint32_t b;
    int i,ret,negative_index=-1;
    Probe neg;

    sceIoRemove(LOG_PATH);
    append_log("GATE1F|format=1|base_commit=a637f54fec4e66a665874944fbea8af016d55f32|expected_runtime=%s\n",G1F_BUILD_ID);
    if(vitabrightGetBuildId(build)<0 || memcmp(build,G1F_BUILD_ID,8)!=0) {
        append_log("STOP|reason=BUILD_ID|actual=%s\n",build); return 1;
    }
    memset(&caps,0,sizeof(caps));
    if(vitabrightMatrixGetCapabilities(&caps)<0 || caps.matrix_backend_supported!=1u || caps.immediate_reapply_supported!=1u ||
       caps.reapply_mode!=VBE_MATRIX_REAPPLY_TAIHEN_CHAIN_REENTRY || caps.channel_order_proven!=0u || caps.cct_supported!=0u || caps.saturation_supported!=0u) {
        append_log("STOP|reason=CAPABILITIES\n"); return 2;
    }
    append_log("CAPS|matrix=%u|immediate=%u|reapply=%u|channel_order=%u|cct=%u|saturation=%u|additive=%u|gamma=%u\n",
        caps.matrix_backend_supported,caps.immediate_reapply_supported,caps.reapply_mode,caps.channel_order_proven,
        caps.cct_supported,caps.saturation_supported,caps.additive_affine_supported,caps.gamma_transfer_supported);

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
    if(alloc_framebuffer()<0) { append_log("STOP|reason=FRAMEBUFFER_SETUP\n"); restore_framebuffer(); return 3; }
    append_log("FRAMEBUFFER|format=SCE_DISPLAY_PIXELFORMAT_A8B8G8R8|format_value=%u|width=%u|height=%u|pitch=%u|BLACK=%08X|GRAY50=%08X|RED75=%08X|GREEN75=%08X|BLUE75=%08X|YELLOW50=%08X|MAGENTA50=%08X|CYAN50=%08X\n",
        SCE_DISPLAY_PIXELFORMAT_A8B8G8R8,WIDTH,HEIGHT,PITCH,pack_rgb(0,0,0),pack_rgb(128,128,128),pack_rgb(192,0,0),pack_rgb(0,192,0),
        pack_rgb(0,0,192),pack_rgb(128,128,0),pack_rgb(128,0,128),pack_rgb(0,128,128));

    snprintf(g_probe_title,sizeof(g_probe_title),"SOURCE CHECK");
    draw_chart("CONFIRM RED GREEN BLUE LABELS","X YES TRI ABORT");
    b=wait_button();
    if(b&SCE_CTRL_TRIANGLE){ append_log("SOURCE_CONFIRM|mask=0\n"); restore_framebuffer(); return 4; }
    append_log("SOURCE_CONFIRM|mask=7\n");

    memset(&pre,0,sizeof(pre));
    if(vitabrightMatrixGetStatus(&pre)<0 || !status_common_ok(&pre) || pre.policy_enabled!=0u ||
       !words_equal(pre.planes[0].forward_words,k_canonical) || !words_equal(pre.planes[1].forward_words,k_canonical)) {
        log_status("PREFLIGHT","-",0,&pre); stop_and_cleanup("PREFLIGHT_STATUS","-"); return 5;
    }
    g_nat0=pre.planes[0].pristine_generation; g_nat1=pre.planes[1].pristine_generation;
    log_status("PREFLIGHT","-",0,&pre);
    append_log("NATURAL_BASELINE|p0=%u|p1=%u\n",g_nat0,g_nat1);
    g_campaign_started=1;

    for(i=0;i<9;++i){
        ret=run_probe(&k_basis_probes[i],&g_obs[i]);
        if(ret<0){ stop_and_cleanup(ret==-3?"OBSERVER_ABORT":"PROBE_INVARIANT",k_basis_probes[i].id); return 10-i; }
    }
    for(i=3;i<9;++i) if(is_unambiguous_rgb(&g_obs[i])){ negative_index=i; break; }
    if(negative_index<0){ stop_and_cleanup("NO_UNAMBIGUOUS_SIGN_PAIR","-"); return 20; }
    neg=negative_from(&k_basis_probes[negative_index]);
    ret=run_probe(&neg,&g_obs[9]);
    if(ret<0){ stop_and_cleanup(ret==-3?"OBSERVER_ABORT":"SIGNED_PROBE_INVARIANT",neg.id); return 21; }

    append_log("COMPLETE|basis_probes=9|signed_probe=%s|natural_p0=%u|natural_p1=%u\n",neg.id,g_nat0,g_nat1);
    snprintf(g_probe_title,sizeof(g_probe_title),"CAMPAIGN COMPLETE");
    draw_chart("EVIDENCE SAVED","X EXIT");
    do { b=wait_button(); } while(!(b&SCE_CTRL_CROSS));
    restore_framebuffer();
    return 0;
}
