#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/io/fcntl.h>
#include <string.h>
#include <stdio.h>
#include "../../build_info.h"
#include "campaign2.h"

static const C2Probe probe_d0  = {"D0-C2",0,0,{0,0,0,0,512,0,0,0,512},1};
static const C2Probe probe_b02 = {"B02-C2",0,2,{512,0,512,0,512,0,0,0,512},0};
static const C2Probe probe_d2  = {"D2-C2",2,2,{512,0,0,0,512,0,0,0,0},1};
static const C2Probe probe_b01 = {"B01-C2",0,1,{512,512,0,0,512,0,0,0,512},0};
static const C2Probe probe_b20 = {"B20-C2",2,0,{512,0,0,0,512,0,512,0,512},0};
static const C2Probe probe_b21 = {"B21-C2",2,1,{512,0,0,0,512,0,0,512,512},0};
static const C2Probe probe_n02 = {"N02-C2",0,2,{512,0,-512,0,512,0,0,0,512},0};
static int campaign_started;

static int choose_change(const char *prompt,uint32_t *out) {
    static const char *const items[]={"NO","YES","AMBIG"};
    int v=c2_choose(prompt,items,3); if (v<0) return -1; *out=(uint32_t)v+1u; return 0;
}
static int choose_cross_appearance(const char *prompt,uint32_t *out) {
    static const char *const items[]={"RED","GREEN","BLUE","YELLOW","MAGENTA","CYAN","DARK BLACK","SAME HUE BRIGHTER","SAME HUE DARKER","OTHER","AMBIG"};
    static const uint32_t map[]={C2_APP_RED,C2_APP_GREEN,C2_APP_BLUE,C2_APP_YELLOW,C2_APP_MAGENTA,C2_APP_CYAN,C2_APP_DARK_BLACK,C2_APP_SAME_BRIGHTER,C2_APP_SAME_DARKER,C2_APP_OTHER,C2_APP_AMBIG};
    int v=c2_choose(prompt,items,(int)(sizeof(items)/sizeof(items[0]))); if (v<0) return -1; *out=map[v]; return 0;
}
static int choose_diag_appearance(const char *prompt,uint32_t *out) {
    static const char *const items[]={"BLACK DARK","VERY DARK","ALTERED HUE","OTHER","AMBIG"};
    static const uint32_t map[]={C2_APP_DARK_BLACK,C2_APP_VERY_DARK,C2_APP_ALTERED_HUE,C2_APP_OTHER,C2_APP_AMBIG};
    int v=c2_choose(prompt,items,5); if (v<0) return -1; *out=map[v]; return 0;
}
static int choose_conf(uint32_t *out) {
    static const char *const items[]={"CLEAR","AMBIG"};
    int v=c2_choose("CONFIDENCE",items,2); if (v<0) return -1; *out=(uint32_t)v+1u; return 0;
}

static int temporal_observe(const C2Probe *p,uint32_t *toggle_count) {
    uint32_t count=0; int probe_state=0;
    char title[32]; snprintf(title,sizeof(title),"PROBE %s",p->id); c2_ui_set_title(title); c2_ui_set_state("STATE NEUTRAL");
    for (;;) {
        char extra[64]; uint32_t b; int tr;
        snprintf(extra,sizeof(extra),"TOGGLES %u MIN %u MAX %u",count,C2_MIN_TOGGLES,C2_MAX_TOGGLES);
        c2_ui_draw("SQUARE NEUTRAL PROBE  X ANSWER",probe_state?"PROBE ACTIVE":"NEUTRAL REFERENCE",extra);
        b=c2_wait_button(SCE_CTRL_SQUARE|SCE_CTRL_CROSS|SCE_CTRL_TRIANGLE);
        if (b&SCE_CTRL_TRIANGLE) return -3;
        if (b&SCE_CTRL_SQUARE) {
            if (count>=C2_MAX_TOGGLES) continue;
            if (probe_state) { tr=c2_transition_neutral(p,"TOGGLE_NEUTRAL"); probe_state=0; c2_ui_set_state("STATE NEUTRAL"); }
            else { tr=c2_transition_probe(p,"TOGGLE_PROBE"); probe_state=1; c2_ui_set_state("STATE PROBE"); }
            ++count; if (tr<0) return tr; continue;
        }
        if (b&SCE_CTRL_CROSS) {
            if (count<C2_MIN_TOGGLES || !probe_state) continue;
            *toggle_count=count; return 0;
        }
    }
}

static int collect_observation(const C2Probe *p,C2Observation *o) {
    static const char *const primary[]={"R50","G50","B50"};
    static const char *const context[]={"YELLOW50 CHANGED","MAGENTA50 CHANGED","CYAN50 CHANGED","GRAY50 CHANGED"};
    unsigned i; int r; memset(o,0,sizeof(*o));
    r=temporal_observe(p,&o->toggle_count); if (r<0) return r;
    for (i=0;i<3;++i) { char q[48]; snprintf(q,sizeof(q),"%s CHANGED",primary[i]); if (choose_change(q,&o->changed[i])<0) return -3; }
    for (i=0;i<3;++i) if (o->changed[i]==C2_CH_YES) {
        char q[64]; snprintf(q,sizeof(q),"%s PROBE APPEARANCE",primary[i]);
        if (p->diagonal) { if (choose_diag_appearance(q,&o->appearance[i])<0) return -3; }
        else { if (choose_cross_appearance(q,&o->appearance[i])<0) return -3; }
    }
    for (i=0;i<4;++i) if (choose_change(context[i],&o->context[i])<0) return -3;
    if (choose_conf(&o->confidence)<0) return -3;
    c2_log("OBS2|id=%s|r_changed=%s|g_changed=%s|b_changed=%s|r_appearance=%s|g_appearance=%s|b_appearance=%s|yellow_changed=%s|magenta_changed=%s|cyan_changed=%s|gray_changed=%s|confidence=%s|toggle_count=%u\n",
        p->id,c2_change_name(o->changed[0]),c2_change_name(o->changed[1]),c2_change_name(o->changed[2]),
        c2_app_name(o->appearance[0]),c2_app_name(o->appearance[1]),c2_app_name(o->appearance[2]),
        c2_change_name(o->context[0]),c2_change_name(o->context[1]),c2_change_name(o->context[2]),c2_change_name(o->context[3]),
        c2_conf_name(o->confidence),o->toggle_count);
    return 0;
}

static int run_probe(const C2Probe *p,C2Observation *o) {
    int r;
    c2_log("MATRIX2|id=%s|row=%u|col=%u|m=%d,%d,%d,%d,%d,%d,%d,%d,%d\n",p->id,p->row,p->col,
        p->matrix[0],p->matrix[1],p->matrix[2],p->matrix[3],p->matrix[4],p->matrix[5],p->matrix[6],p->matrix[7],p->matrix[8]);
    r=collect_observation(p,o); if (r<0) return r;
    r=c2_verify_probe(p,"POSTOBS2"); if (r<0) return r;
    r=c2_transition_neutral(p,"RESET2"); if (r<0) return r;
    c2_ui_set_state("STATE NEUTRAL"); return 0;
}

static int d0_control_ok(const C2Observation *o) {
    return o->changed[0]==C2_CH_YES && o->changed[1]==C2_CH_NO && o->changed[2]==C2_CH_NO &&
        (o->appearance[0]==C2_APP_DARK_BLACK || o->appearance[0]==C2_APP_VERY_DARK) && o->confidence==C2_CONF_CLEAR;
}
static int b02_control_ok(const C2Observation *o) {
    return o->changed[0]==C2_CH_NO && o->changed[1]==C2_CH_NO && o->changed[2]==C2_CH_YES &&
        o->appearance[2]==C2_APP_MAGENTA && o->confidence==C2_CONF_CLEAR;
}

static int collect_signed(C2SignedObservation *o) {
    int r; memset(o,0,sizeof(*o)); snprintf(o->witness,sizeof(o->witness),"MAGENTA50");
    r=temporal_observe(&probe_n02,&o->toggle_count); if (r<0) return r;
    if (choose_change("MAGENTA50 CHANGED",&o->changed)<0) return -3;
    if (o->changed==C2_CH_YES && choose_cross_appearance("MAGENTA50 PROBE APPEARANCE",&o->appearance)<0) return -3;
    if (choose_conf(&o->confidence)<0) return -3;
    c2_log("SIGNED_OBS2|id=N02-C2|positive=B02-C2|witness=%s|changed=%s|probe_appearance=%s|confidence=%s|toggle_count=%u\n",
        o->witness,c2_change_name(o->changed),c2_app_name(o->appearance),c2_conf_name(o->confidence),o->toggle_count);
    r=c2_verify_probe(&probe_n02,"POSTOBS2"); if (r<0) return r;
    r=c2_transition_neutral(&probe_n02,"RESET2"); if (r<0) return r;
    c2_ui_set_state("STATE NEUTRAL"); return 0;
}

static void stop_and_cleanup(const char *reason,const char *id) {
    int rr=0; c2_log("STOP2|reason=%s|id=%s\n",reason,id?id:"-"); if (campaign_started) rr=vitabrightMatrixReset(); c2_log("CLEANUP2|reset=%d\n",rr); c2_ui_restore();
}
static int fail_probe(int r,const char *id) {
    if (r==-100) { stop_and_cleanup("CAMPAIGN2_CONTAMINATED",id); return 40; }
    if (r==-3) { stop_and_cleanup("OBSERVER_ABORT",id); return 41; }
    stop_and_cleanup("CAMPAIGN2_INVARIANT",id); return 42;
}

int main(void) {
    char build[VBE_BUILD_ID_SIZE]={0}; VbeMatrixCapabilities caps; VbeMatrixBackendStatus pre;
    C2Observation d0,b02,d2,b01,b20,b21; C2SignedObservation n02;
    uint32_t b; int sr,r;
    sceIoRemove(C2_LOG_PATH);
    c2_log("GATE1F_C2|format=1|base_commit=a637f54fec4e66a665874944fbea8af016d55f32|campaign1_sha=%s|expected_runtime=%s|observer=TEMPORAL_AB\n",C1_RAW_SHA,C2_BUILD_ID);
    c2_log("STIMULUS2|cross=512|diag_zero=0|signed=-512|rgb8_component50=128|fraction50_num=256|max_toggles=%u|min_toggles=%u\n",C2_MAX_TOGGLES,C2_MIN_TOGGLES);
    if (vitabrightGetBuildId(build)<0 || memcmp(build,C2_BUILD_ID,8)!=0) { c2_log("STOP2|reason=BUILD_ID|actual=%s\n",build); return 1; }
    memset(&caps,0,sizeof(caps));
    if (vitabrightMatrixGetCapabilities(&caps)<0 || caps.matrix_backend_supported!=1u || caps.immediate_reapply_supported!=1u ||
        caps.reapply_mode!=VBE_MATRIX_REAPPLY_TAIHEN_CHAIN_REENTRY || caps.channel_order_proven!=0u || caps.cct_supported!=0u || caps.saturation_supported!=0u) {
        c2_log("STOP2|reason=CAPABILITIES\n"); return 2;
    }
    c2_log("CAPS2|matrix=%u|immediate=%u|reapply=%u|channel_order=%u|cct=%u|saturation=%u|additive=%u|gamma=%u\n",
        caps.matrix_backend_supported,caps.immediate_reapply_supported,caps.reapply_mode,caps.channel_order_proven,caps.cct_supported,caps.saturation_supported,caps.additive_affine_supported,caps.gamma_transfer_supported);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
    if (c2_ui_alloc()<0) { c2_log("STOP2|reason=FRAMEBUFFER_SETUP\n"); c2_ui_restore(); return 3; }
    c2_log("FRAMEBUFFER2|format=SCE_DISPLAY_PIXELFORMAT_A8B8G8R8|format_value=%u|width=960|height=544|pitch=1024|BLACK=%08X|GRAY50=%08X|R50=%08X|G50=%08X|B50=%08X|YELLOW50=%08X|MAGENTA50=%08X|CYAN50=%08X\n",
        SCE_DISPLAY_PIXELFORMAT_A8B8G8R8,c2_pack_rgb(0,0,0),c2_pack_rgb(128,128,128),c2_pack_rgb(128,0,0),c2_pack_rgb(0,128,0),c2_pack_rgb(0,0,128),c2_pack_rgb(128,128,0),c2_pack_rgb(128,0,128),c2_pack_rgb(0,128,128));
    c2_ui_set_title("SOURCE CHECK"); c2_ui_set_state("STATE NEUTRAL"); c2_ui_draw("CONFIRM R50 G50 B50 LABELS","X YES  TRI ABORT",NULL);
    b=c2_wait_button(SCE_CTRL_CROSS|SCE_CTRL_TRIANGLE); if (b&SCE_CTRL_TRIANGLE) { c2_log("SOURCE_CONFIRM2|mask=0\n"); c2_ui_restore(); return 4; } c2_log("SOURCE_CONFIRM2|mask=7\n");
    memset(&pre,0,sizeof(pre)); sr=vitabrightMatrixGetStatus(&pre); c2_log_status("PREFLIGHT2","-",sr,&pre);
    if (sr<0 || !c2_preflight_ok(&pre)) { stop_and_cleanup("PREFLIGHT_STATUS","-"); return 5; }
    c2_set_natural(pre.planes[0].pristine_generation,pre.planes[1].pristine_generation); campaign_started=1;
    c2_log("NATURAL_BASELINE2|p0=%u|p1=%u\n",pre.planes[0].pristine_generation,pre.planes[1].pristine_generation);

    r=run_probe(&probe_d0,&d0); if (r<0) return fail_probe(r,probe_d0.id);
    r=run_probe(&probe_b02,&b02); if (r<0) return fail_probe(r,probe_b02.id);
    c2_log("CONTROL2|id=D0-C2|result=%s\n",d0_control_ok(&d0)?"PASS":"FAIL");
    c2_log("CONTROL2|id=B02-C2|result=%s\n",b02_control_ok(&b02)?"PASS":"FAIL");
    if (!d0_control_ok(&d0) || !b02_control_ok(&b02)) { stop_and_cleanup("CAMPAIGN2_OBSERVER_VALIDATION","-"); return 6; }

    r=run_probe(&probe_d2,&d2); if (r<0) return fail_probe(r,probe_d2.id);
    r=run_probe(&probe_b01,&b01); if (r<0) return fail_probe(r,probe_b01.id);
    r=run_probe(&probe_b20,&b20); if (r<0) return fail_probe(r,probe_b20.id);
    r=run_probe(&probe_b21,&b21); if (r<0) return fail_probe(r,probe_b21.id);

    c2_log("SIGNED_SETUP2|positive=B02-C2|input=B|output=R|witness=MAGENTA50|coefficient=-512\n");
    r=collect_signed(&n02); if (r<0) return fail_probe(r,probe_n02.id);

    c2_log("COMPLETE2|controls=PASS|targets=4|signed_probe=N02-C2|natural_p0=%u|natural_p1=%u\n",pre.planes[0].pristine_generation,pre.planes[1].pristine_generation);
    c2_ui_set_title("CAMPAIGN 2 COMPLETE"); c2_ui_set_state("STATE NEUTRAL"); c2_ui_draw("EVIDENCE SAVED","X EXIT",NULL);
    do { b=c2_wait_button(SCE_CTRL_CROSS); } while (!(b&SCE_CTRL_CROSS));
    c2_ui_restore(); return 0;
}
