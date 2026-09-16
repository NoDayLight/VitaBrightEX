#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/io/fcntl.h>
#include <string.h>
#include <stdio.h>
#include "../../build_info.h"
#include "campaign2.h"
#include "observer_core.h"

static const C2Probe probe_d0  = {"D0-C2",0,0,{0,0,0,0,512,0,0,0,512},1};
static const C2Probe probe_b02 = {"B02-C2",0,2,{512,0,512,0,512,0,0,0,512},0};
static const C2Probe probe_d2  = {"D2-C2",2,2,{512,0,0,0,512,0,0,0,0},1};
static const C2Probe probe_b01 = {"B01-C2",0,1,{512,512,0,0,512,0,0,0,512},0};
static const C2Probe probe_b20 = {"B20-C2",2,0,{512,0,0,0,512,0,512,0,512},0};
static const C2Probe probe_b21 = {"B21-C2",2,1,{512,0,0,0,512,0,0,512,512},0};
static const C2Probe probe_n02 = {"N02-C2",0,2,{512,0,-512,0,512,0,0,0,512},0};
static const C2Probe *const known_probes[] = {
    &probe_d0,&probe_b02,&probe_d2,&probe_b01,&probe_b20,&probe_b21,&probe_n02
};
static int campaign_started;

typedef struct {
    const C2Probe *probe;
    uint32_t toggle_count;
    uint32_t answer_step;
    int probe_state;
} C2Session;

static void session_begin(C2Session *s,const C2Probe *p) {
    char title[32];
    memset(s,0,sizeof(*s));
    s->probe=p;
    snprintf(title,sizeof(title),"PROBE %s",p->id);
    c2_ui_set_title(title);
    c2_ui_set_state("STATE NEUTRAL");
}

static int session_toggle(C2Session *s) {
    int r;
    if (s->toggle_count==UINT32_MAX) return -6;
    if (s->probe_state) {
        r=c2_transition_neutral(s->probe,"TOGGLE_NEUTRAL");
        if (r<0) return r;
        s->probe_state=0;
        c2_ui_set_state("STATE NEUTRAL");
    } else {
        r=c2_transition_probe(s->probe,"TOGGLE_PROBE");
        if (r<0) return r;
        s->probe_state=1;
        c2_ui_set_state("STATE PROBE");
    }
    if (c2_note_toggle(&s->toggle_count)<0) return -6;
    return 0;
}

static int session_choose(C2Session *s,const char *field,const char *prompt,
                          const char *const *items,int n,int *choice) {
    C2ChoiceCursor cursor;
    char notice[96]={0};
    c2_choice_init(&cursor);
    for (;;) {
        char shown_prompt[96];
        char extra[112];
        const char *shown_value;
        uint32_t b;
        int r,commit;

        snprintf(shown_prompt,sizeof(shown_prompt),"STEP %02u  %s",s->answer_step+1u,prompt);
        shown_value=cursor.selected ? items[cursor.index] : "-- SELECT --";
        if (notice[0]) {
            snprintf(extra,sizeof(extra),"%s",notice);
        } else if (s->toggle_count<C2_MIN_TOGGLES) {
            snprintf(extra,sizeof(extra),"SQUARE A/B: NEED %u TOGGLES",C2_MIN_TOGGLES);
        } else if (!s->probe_state) {
            snprintf(extra,sizeof(extra),"STATE NEUTRAL: SQUARE TO PROBE BEFORE X");
        } else {
            snprintf(extra,sizeof(extra),"TOGGLES %u  READY: X SAVES ANSWER",s->toggle_count);
        }
        c2_ui_draw(shown_prompt,shown_value,extra);
        b=c2_wait_button(SCE_CTRL_LEFT|SCE_CTRL_RIGHT|SCE_CTRL_CROSS|SCE_CTRL_TRIANGLE|SCE_CTRL_SQUARE);
        notice[0]=0;

        if (b&SCE_CTRL_TRIANGLE) return -3;
        if (b&SCE_CTRL_SQUARE) {
            r=session_toggle(s);
            if (r<0) return r;
            continue;
        }
        if (b&SCE_CTRL_LEFT) {
            c2_choice_move(&cursor,-1,n);
            continue;
        }
        if (b&SCE_CTRL_RIGHT) {
            c2_choice_move(&cursor,+1,n);
            continue;
        }
        if (!(b&SCE_CTRL_CROSS)) continue;

        commit=c2_choice_try_commit(&cursor,s->toggle_count,s->probe_state,C2_MIN_TOGGLES,choice);
        if (commit==C2_CHOICE_NEED_SELECTION) {
            snprintf(notice,sizeof(notice),"SELECT AN ANSWER WITH LEFT OR RIGHT");
            continue;
        }
        if (commit==C2_CHOICE_NEED_TOGGLES) {
            snprintf(notice,sizeof(notice),"DO NEUTRAL PROBE NEUTRAL PROBE FIRST");
            continue;
        }
        if (commit==C2_CHOICE_NEED_PROBE) {
            snprintf(notice,sizeof(notice),"ANSWER NOT SAVED: SQUARE TO STATE PROBE");
            continue;
        }
        if (commit!=C2_CHOICE_COMMITTED) return -4;

        ++s->answer_step;
        if (c2_log("ANSWER2|id=%s|step=%u|field=%s|value=%s|toggle_count=%u|state=PROBE\n",
                   s->probe->id,s->answer_step,field,items[*choice],s->toggle_count)<0)
            return -5;
        return 0;
    }
}

static int choose_change(C2Session *s,const char *field,const char *prompt,uint32_t *out) {
    static const char *const items[]={"YES","NO","AMBIG"};
    static const uint32_t map[]={C2_CH_YES,C2_CH_NO,C2_CH_AMBIG};
    int v,r=session_choose(s,field,prompt,items,3,&v);
    if (r<0) return r;
    *out=map[v];
    return 0;
}
static int choose_cross_appearance(C2Session *s,const char *field,const char *prompt,uint32_t *out) {
    static const char *const items[]={"RED","GREEN","BLUE","YELLOW","MAGENTA","CYAN","DARK BLACK","SAME HUE BRIGHTER","SAME HUE DARKER","OTHER","AMBIG"};
    static const uint32_t map[]={C2_APP_RED,C2_APP_GREEN,C2_APP_BLUE,C2_APP_YELLOW,C2_APP_MAGENTA,C2_APP_CYAN,C2_APP_DARK_BLACK,C2_APP_SAME_BRIGHTER,C2_APP_SAME_DARKER,C2_APP_OTHER,C2_APP_AMBIG};
    int v,r=session_choose(s,field,prompt,items,(int)(sizeof(items)/sizeof(items[0])),&v);
    if (r<0) return r;
    *out=map[v];
    return 0;
}
static int choose_diag_appearance(C2Session *s,const char *field,const char *prompt,uint32_t *out) {
    static const char *const items[]={"DARK BLACK","VERY DARK","ALTERED HUE","OTHER","AMBIG"};
    static const uint32_t map[]={C2_APP_DARK_BLACK,C2_APP_VERY_DARK,C2_APP_ALTERED_HUE,C2_APP_OTHER,C2_APP_AMBIG};
    int v,r=session_choose(s,field,prompt,items,5,&v);
    if (r<0) return r;
    *out=map[v];
    return 0;
}
static int choose_conf(C2Session *s,uint32_t *out) {
    static const char *const items[]={"CLEAR","AMBIG"};
    int v,r=session_choose(s,"CONFIDENCE","CONFIDENCE",items,2,&v);
    if (r<0) return r;
    *out=(uint32_t)v+1u;
    return 0;
}

static int collect_observation(const C2Probe *p,C2Observation *o) {
    static const char *const primary_question[]={"R50 CHANGED","G50 CHANGED","B50 CHANGED"};
    static const char *const primary_field[]={"R50_CHANGED","G50_CHANGED","B50_CHANGED"};
    static const char *const primary_name[]={"R50","G50","B50"};
    static const char *const context_question[]={"YELLOW50 CHANGED","MAGENTA50 CHANGED","CYAN50 CHANGED","GRAY50 CHANGED"};
    static const char *const context_field[]={"YELLOW50_CHANGED","MAGENTA50_CHANGED","CYAN50_CHANGED","GRAY50_CHANGED"};
    C2Session s;
    unsigned i;
    int r;
    memset(o,0,sizeof(*o));
    session_begin(&s,p);
    for (i=0;i<3;++i) {
        r=choose_change(&s,primary_field[i],primary_question[i],&o->changed[i]);
        if (r<0) return r;
    }
    for (i=0;i<3;++i) if (o->changed[i]==C2_CH_YES) {
        char q[64],field[32];
        snprintf(q,sizeof(q),"%s PROBE APPEARANCE",primary_name[i]);
        snprintf(field,sizeof(field),"%s_APPEARANCE",primary_name[i]);
        if (p->diagonal) r=choose_diag_appearance(&s,field,q,&o->appearance[i]);
        else r=choose_cross_appearance(&s,field,q,&o->appearance[i]);
        if (r<0) return r;
    }
    for (i=0;i<4;++i) {
        r=choose_change(&s,context_field[i],context_question[i],&o->context[i]);
        if (r<0) return r;
    }
    r=choose_conf(&s,&o->confidence);
    if (r<0) return r;
    o->toggle_count=s.toggle_count;
    o->answer_count=s.answer_step;
    if (c2_log("OBS2|id=%s|r_changed=%s|g_changed=%s|b_changed=%s|r_appearance=%s|g_appearance=%s|b_appearance=%s|yellow_changed=%s|magenta_changed=%s|cyan_changed=%s|gray_changed=%s|confidence=%s|toggle_count=%u|answer_count=%u\n",
        p->id,c2_change_name(o->changed[0]),c2_change_name(o->changed[1]),c2_change_name(o->changed[2]),
        c2_app_name(o->appearance[0]),c2_app_name(o->appearance[1]),c2_app_name(o->appearance[2]),
        c2_change_name(o->context[0]),c2_change_name(o->context[1]),c2_change_name(o->context[2]),c2_change_name(o->context[3]),
        c2_conf_name(o->confidence),o->toggle_count,o->answer_count)<0)
        return -5;
    return 0;
}

static int wait_probe_complete(const C2Probe *p) {
    uint32_t b;
    c2_ui_set_title("PROBE COMPLETE");
    c2_ui_set_state("STATE NEUTRAL");
    c2_ui_draw(p->id,"OBSERVATION SAVED","X NEXT PROBE   TRI ABORT");
    b=c2_wait_button(SCE_CTRL_CROSS|SCE_CTRL_TRIANGLE);
    if (b&SCE_CTRL_TRIANGLE) return -3;
    if (c2_log("PROBE_DONE2|id=%s\n",p->id)<0) return -5;
    return 0;
}

static int run_probe(const C2Probe *p,C2Observation *o) {
    int r;
    if (c2_log("MATRIX2|id=%s|row=%u|col=%u|m=%d,%d,%d,%d,%d,%d,%d,%d,%d\n",p->id,p->row,p->col,
        p->matrix[0],p->matrix[1],p->matrix[2],p->matrix[3],p->matrix[4],p->matrix[5],p->matrix[6],p->matrix[7],p->matrix[8])<0)
        return -5;
    r=collect_observation(p,o); if (r<0) return r;
    r=c2_verify_probe(p,"POSTOBS2"); if (r<0) return r;
    r=c2_transition_neutral(p,"RESET2"); if (r<0) return r;
    c2_ui_set_state("STATE NEUTRAL");
    return wait_probe_complete(p);
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
    C2Session s;
    int r;
    memset(o,0,sizeof(*o));
    snprintf(o->witness,sizeof(o->witness),"MAGENTA50");
    session_begin(&s,&probe_n02);
    if (c2_log("MATRIX2|id=%s|row=%u|col=%u|m=%d,%d,%d,%d,%d,%d,%d,%d,%d\n",probe_n02.id,probe_n02.row,probe_n02.col,
        probe_n02.matrix[0],probe_n02.matrix[1],probe_n02.matrix[2],probe_n02.matrix[3],probe_n02.matrix[4],probe_n02.matrix[5],probe_n02.matrix[6],probe_n02.matrix[7],probe_n02.matrix[8])<0)
        return -5;
    r=choose_change(&s,"MAGENTA50_CHANGED","MAGENTA50 CHANGED",&o->changed);
    if (r<0) return r;
    if (o->changed==C2_CH_YES) {
        r=choose_cross_appearance(&s,"MAGENTA50_APPEARANCE","MAGENTA50 PROBE APPEARANCE",&o->appearance);
        if (r<0) return r;
    }
    r=choose_conf(&s,&o->confidence);
    if (r<0) return r;
    o->toggle_count=s.toggle_count;
    o->answer_count=s.answer_step;
    if (c2_log("SIGNED_OBS2|id=N02-C2|positive=B02-C2|witness=%s|changed=%s|probe_appearance=%s|confidence=%s|toggle_count=%u|answer_count=%u\n",
        o->witness,c2_change_name(o->changed),c2_app_name(o->appearance),c2_conf_name(o->confidence),o->toggle_count,o->answer_count)<0)
        return -5;
    r=c2_verify_probe(&probe_n02,"POSTOBS2"); if (r<0) return r;
    r=c2_transition_neutral(&probe_n02,"RESET2"); if (r<0) return r;
    c2_ui_set_state("STATE NEUTRAL");
    if (c2_log("PROBE_DONE2|id=N02-C2\n")<0) return -5;
    return 0;
}

static void stop_and_cleanup(const char *reason,const C2Probe *p) {
    int rr=0;
    c2_log("STOP2|reason=%s|id=%s\n",reason,p?p->id:"-");
    if (campaign_started) rr=c2_transition_neutral(p,"CLEANUP_RESET2");
    c2_log("CLEANUP2|reset=%d|log_fault=%d\n",rr,c2_log_failed());
    c2_ui_restore();
}
static int fail_probe(int r,const C2Probe *p) {
    if (r==-100) { stop_and_cleanup("CAMPAIGN2_CONTAMINATED",p); return 40; }
    if (r==-3) { stop_and_cleanup("OBSERVER_ABORT",p); return 41; }
    if (r==-5 || c2_log_failed()) { stop_and_cleanup("EVIDENCE_IO",p); return 43; }
    if (r==-6) { stop_and_cleanup("TOGGLE_COUNTER_OVERFLOW",p); return 44; }
    stop_and_cleanup("CAMPAIGN2_INVARIANT",p); return 42;
}

static const C2Probe *known_active_probe(const VbeMatrixBackendStatus *s) {
    unsigned i;
    for (i=0;i<sizeof(known_probes)/sizeof(known_probes[0]);++i)
        if (c2_status_matches_probe(s,known_probes[i])) return known_probes[i];
    return NULL;
}

static int recover_startup_policy(void) {
    VbeMatrixBackendStatus s;
    const C2Probe *owner;
    uint32_t b;
    int sr,rr;
    memset(&s,0,sizeof(s));
    sr=vitabrightMatrixGetStatus(&s);
    if (c2_log_status("STARTUP2","-",sr,&s)<0) return -5;
    if (sr<0) return -1;
    if (c2_preflight_ok(&s)) return 0;
    if (!c2_active_policy_base_ok(&s)) return -2;
    owner=known_active_probe(&s);
    if (!owner) {
        c2_log("RECOVERY2|result=REFUSED_UNKNOWN_ACTIVE\n");
        c2_ui_set_title("RECOVERY BLOCKED");
        c2_ui_set_state("STATE ACTIVE MATRIX");
        c2_ui_draw("UNKNOWN ACTIVE MATRIX","NO RESET PERFORMED","TRI EXIT");
        c2_wait_button(SCE_CTRL_TRIANGLE);
        return -7;
    }

    c2_ui_set_title("RECOVERY");
    c2_ui_set_state("STATE ACTIVE MATRIX");
    c2_ui_draw("KNOWN CAMPAIGN2 MATRIX ACTIVE",owner->id,"X RESET TO NEUTRAL  TRI ABORT");
    b=c2_wait_button(SCE_CTRL_CROSS|SCE_CTRL_TRIANGLE);
    if (b&SCE_CTRL_TRIANGLE) {
        c2_log("RECOVERY2|result=ABORT|id=%s\n",owner->id);
        return -3;
    }

    rr=vitabrightMatrixReset();
    memset(&s,0,sizeof(s));
    sr=vitabrightMatrixGetStatus(&s);
    if (c2_log_status("RECOVERY_RESET2",owner->id,rr,&s)<0) return -5;
    if (sr<0 || rr!=VBE_MATRIX_RESULT_APPLIED || !c2_preflight_ok(&s)) {
        c2_log("RECOVERY2|result=FAIL|id=%s\n",owner->id);
        return -4;
    }
    if (c2_log("RECOVERY2|result=PASS|id=%s\n",owner->id)<0) return -5;
    return 1;
}

int main(void) {
    char build[VBE_BUILD_ID_SIZE]={0};
    VbeMatrixCapabilities caps;
    VbeMatrixBackendStatus pre;
    C2Observation d0,b02,d2,b01,b20,b21;
    C2SignedObservation n02;
    uint32_t b;
    int sr,r;

    if (c2_log_begin()<0) return 90;
    if (c2_log("GATE1F_C2|format=2|base_commit=a637f54fec4e66a665874944fbea8af016d55f32|campaign1_sha=%s|expected_runtime=%s|observer=TEMPORAL_AB_EXPLICIT_COMMIT\n",C1_RAW_SHA,C2_BUILD_ID)<0)
        return 90;
    if (c2_log("STIMULUS2|cross=512|diag_zero=0|signed=-512|rgb8_component50=128|fraction50_num=256|min_toggles=%u|max_toggles=UNBOUNDED\n",C2_MIN_TOGGLES)<0)
        return 90;
    if (vitabrightGetBuildId(build)<0 || memcmp(build,C2_BUILD_ID,8)!=0) {
        c2_log("STOP2|reason=BUILD_ID|actual=%s\n",build);
        return 1;
    }
    memset(&caps,0,sizeof(caps));
    if (vitabrightMatrixGetCapabilities(&caps)<0 || caps.matrix_backend_supported!=1u || caps.immediate_reapply_supported!=1u ||
        caps.reapply_mode!=VBE_MATRIX_REAPPLY_TAIHEN_CHAIN_REENTRY || caps.channel_order_proven!=0u || caps.cct_supported!=0u || caps.saturation_supported!=0u) {
        c2_log("STOP2|reason=CAPABILITIES\n");
        return 2;
    }
    if (c2_log("CAPS2|matrix=%u|immediate=%u|reapply=%u|channel_order=%u|cct=%u|saturation=%u|additive=%u|gamma=%u\n",
        caps.matrix_backend_supported,caps.immediate_reapply_supported,caps.reapply_mode,caps.channel_order_proven,caps.cct_supported,caps.saturation_supported,caps.additive_affine_supported,caps.gamma_transfer_supported)<0)
        return 90;
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
    if (c2_ui_alloc()<0) {
        c2_log("STOP2|reason=FRAMEBUFFER_SETUP\n");
        c2_ui_restore();
        return 3;
    }
    if (c2_log("FRAMEBUFFER2|format=SCE_DISPLAY_PIXELFORMAT_A8B8G8R8|format_value=%u|width=960|height=544|pitch=1024|BLACK=%08X|GRAY50=%08X|R50=%08X|G50=%08X|B50=%08X|YELLOW50=%08X|MAGENTA50=%08X|CYAN50=%08X\n",
        SCE_DISPLAY_PIXELFORMAT_A8B8G8R8,c2_pack_rgb(0,0,0),c2_pack_rgb(128,128,128),c2_pack_rgb(128,0,0),c2_pack_rgb(0,128,0),c2_pack_rgb(0,0,128),c2_pack_rgb(128,128,0),c2_pack_rgb(128,0,128),c2_pack_rgb(0,128,128))<0) {
        c2_ui_restore(); return 90;
    }

    r=recover_startup_policy();
    if (r<0) {
        c2_log("STOP2|reason=STARTUP_RECOVERY|code=%d\n",r);
        c2_ui_restore();
        return 4;
    }

    c2_ui_set_title("SOURCE CHECK");
    c2_ui_set_state("STATE NEUTRAL");
    c2_ui_draw("CONFIRM R50 G50 B50 LABELS","X YES  TRI ABORT",NULL);
    b=c2_wait_button(SCE_CTRL_CROSS|SCE_CTRL_TRIANGLE);
    if (b&SCE_CTRL_TRIANGLE) {
        c2_log("SOURCE_CONFIRM2|mask=0\n");
        c2_ui_restore();
        return 5;
    }
    if (c2_log("SOURCE_CONFIRM2|mask=7\n")<0) { c2_ui_restore(); return 90; }
    memset(&pre,0,sizeof(pre));
    sr=vitabrightMatrixGetStatus(&pre);
    if (c2_log_status("PREFLIGHT2","-",sr,&pre)<0) { c2_ui_restore(); return 90; }
    if (sr<0 || !c2_preflight_ok(&pre)) {
        c2_log("STOP2|reason=PREFLIGHT_STATUS|id=-\n");
        c2_ui_restore();
        return 6;
    }
    c2_set_natural(pre.planes[0].pristine_generation,pre.planes[1].pristine_generation);
    campaign_started=1;
    if (c2_log("NATURAL_BASELINE2|p0=%u|p1=%u\n",pre.planes[0].pristine_generation,pre.planes[1].pristine_generation)<0)
        return fail_probe(-5,NULL);

    r=run_probe(&probe_d0,&d0); if (r<0) return fail_probe(r,&probe_d0);
    r=run_probe(&probe_b02,&b02); if (r<0) return fail_probe(r,&probe_b02);
    if (c2_log("CONTROL2|id=D0-C2|result=%s\n",d0_control_ok(&d0)?"PASS":"FAIL")<0 ||
        c2_log("CONTROL2|id=B02-C2|result=%s\n",b02_control_ok(&b02)?"PASS":"FAIL")<0)
        return fail_probe(-5,NULL);
    if (!d0_control_ok(&d0) || !b02_control_ok(&b02)) {
        stop_and_cleanup("CAMPAIGN2_OBSERVER_VALIDATION",NULL);
        return 7;
    }

    r=run_probe(&probe_d2,&d2); if (r<0) return fail_probe(r,&probe_d2);
    r=run_probe(&probe_b01,&b01); if (r<0) return fail_probe(r,&probe_b01);
    r=run_probe(&probe_b20,&b20); if (r<0) return fail_probe(r,&probe_b20);
    r=run_probe(&probe_b21,&b21); if (r<0) return fail_probe(r,&probe_b21);

    if (c2_log("SIGNED_SETUP2|positive=B02-C2|input=B|output=R|witness=MAGENTA50|coefficient=-512\n")<0)
        return fail_probe(-5,&probe_n02);
    r=collect_signed(&n02); if (r<0) return fail_probe(r,&probe_n02);

    if (c2_log("COMPLETE2|controls=PASS|targets=4|signed_probe=N02-C2|natural_p0=%u|natural_p1=%u\n",pre.planes[0].pristine_generation,pre.planes[1].pristine_generation)<0)
        return fail_probe(-5,NULL);
    c2_ui_set_title("CAMPAIGN 2 COMPLETE");
    c2_ui_set_state("STATE NEUTRAL");
    c2_ui_draw("EVIDENCE SAVED","X EXIT",NULL);
    do { b=c2_wait_button(SCE_CTRL_CROSS); } while (!(b&SCE_CTRL_CROSS));
    c2_ui_restore();
    return 0;
}
