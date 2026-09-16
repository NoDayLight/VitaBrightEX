#include <psp2/io/fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "campaign2.h"

#define ARRAY_COUNT(a) (sizeof(a)/sizeof((a)[0]))

#define STFAIL_TARGET            (1u << 0)
#define STFAIL_HOOK_OWNED        (1u << 1)
#define STFAIL_HOOK_FAIL         (1u << 2)
#define STFAIL_PENDING           (1u << 3)
#define STFAIL_REAPPLY           (1u << 4)
#define STFAIL_TX_FAULTS         (1u << 5)
#define STFAIL_AUTH_MASKS        (1u << 6)
#define STFAIL_P0_VALID          (1u << 7)
#define STFAIL_P1_VALID          (1u << 8)
#define STFAIL_P0_CLASS          (1u << 9)
#define STFAIL_P1_CLASS          (1u << 10)
#define STFAIL_P0_SONY           (1u << 11)
#define STFAIL_P1_SONY           (1u << 12)
#define STFAIL_P0_SOURCE         (1u << 13)
#define STFAIL_P1_SOURCE         (1u << 14)
#define STFAIL_PLANE_DIAGNOSTICS (1u << 15)

static const uint32_t canonical[15] = {
    0,0,0x3FF,0,0x3FF,0,0x200,0,0,0,0x200,0,0,0,0x200
};
static uint32_t natural0, natural1;

int c2_log(const char *fmt,...) {
    char buf[4096]; va_list ap; int n,fd,wr;
    va_start(ap,fmt); n=vsnprintf(buf,sizeof(buf),fmt,ap); va_end(ap);
    if (n<0 || n>=(int)sizeof(buf)) return -1;
    fd=sceIoOpen(C2_LOG_PATH,SCE_O_WRONLY|SCE_O_CREAT|SCE_O_APPEND,0666);
    if (fd<0) return fd;
    wr=sceIoWrite(fd,buf,(SceSize)n); sceIoClose(fd);
    return wr==n ? 0 : -1;
}

static int words_equal(const uint32_t *a,const uint32_t *b) {
    unsigned i; uint32_t d=0;
    for (i=0;i<15;++i) d |= a[i]^b[i];
    return d==0;
}
static int append_words(char *buf,size_t cap,int off,const uint32_t *w) {
    unsigned i;
    for (i=0;i<15;++i) {
        int n=snprintf(buf+off,cap-(size_t)off,"%s%08X",i?",":"",w[i]);
        if (n<0 || off+n>=(int)cap) return -1;
        off+=n;
    }
    return off;
}

int c2_log_status(const char *tag,const char *id,int action_result,const VbeMatrixBackendStatus *s) {
    char buf[4096];
    int off=snprintf(buf,sizeof(buf),
        "%s|id=%s|result=%d|target=%u|hook_owned=%u|hook_fail=%u|requested=%u|active=%u|enabled=%u|pending=%u|reapply=%u|last=%d|tx=%u|faults=%u|masks=%u|p0_valid=%u|p0_nat=%u|p0_class=%u|p0_applied=%u|p0_raw=%d|p0_mismatch=%u|p0_overflow=%u|p0_policy_fail=%u|p0_source=",
        tag,id,action_result,s->target_supported,s->hook_owned,s->hook_fail,
        s->requested_generation,s->active_published_generation,s->policy_enabled,
        s->pending_plane_mask,s->reapply_mode,s->last_request_result,s->transaction_state,
        s->transaction_fault_flags,s->authority_degraded_masks,s->planes[0].valid,
        s->planes[0].pristine_generation,s->planes[0].baseline_class,
        s->planes[0].last_forwarded_policy_generation,s->planes[0].last_sony_return,
        s->planes[0].baseline_mismatch_count,s->planes[0].overflow_count,
        s->planes[0].policy_read_fail_count);
    if (off<0) return -1;
    off=append_words(buf,sizeof(buf),off,s->planes[0].source_words); if (off<0) return -1;
    off+=snprintf(buf+off,sizeof(buf)-(size_t)off,"|p0_forward=");
    off=append_words(buf,sizeof(buf),off,s->planes[0].forward_words); if (off<0) return -1;
    off+=snprintf(buf+off,sizeof(buf)-(size_t)off,
        "|p1_valid=%u|p1_nat=%u|p1_class=%u|p1_applied=%u|p1_raw=%d|p1_mismatch=%u|p1_overflow=%u|p1_policy_fail=%u|p1_source=",
        s->planes[1].valid,s->planes[1].pristine_generation,s->planes[1].baseline_class,
        s->planes[1].last_forwarded_policy_generation,s->planes[1].last_sony_return,
        s->planes[1].baseline_mismatch_count,s->planes[1].overflow_count,
        s->planes[1].policy_read_fail_count);
    off=append_words(buf,sizeof(buf),off,s->planes[1].source_words); if (off<0) return -1;
    off+=snprintf(buf+off,sizeof(buf)-(size_t)off,"|p1_forward=");
    off=append_words(buf,sizeof(buf),off,s->planes[1].forward_words); if (off<0) return -1;
    if (off+2>=(int)sizeof(buf)) return -1;
    buf[off++]='\n'; buf[off]=0;
    return c2_log("%s",buf);
}

static uint32_t common_fail(const VbeMatrixBackendStatus *s) {
    uint32_t m=0;
    if (s->target_supported!=1u) m|=STFAIL_TARGET;
    if (s->hook_owned!=1u) m|=STFAIL_HOOK_OWNED;
    if (s->hook_fail!=0u) m|=STFAIL_HOOK_FAIL;
    if (s->pending_plane_mask!=0u) m|=STFAIL_PENDING;
    if (s->reapply_mode!=VBE_MATRIX_REAPPLY_TAIHEN_CHAIN_REENTRY) m|=STFAIL_REAPPLY;
    if (s->transaction_fault_flags!=0u) m|=STFAIL_TX_FAULTS;
    if (s->authority_degraded_masks!=0u) m|=STFAIL_AUTH_MASKS;
    if (s->planes[0].valid!=1u) m|=STFAIL_P0_VALID;
    if (s->planes[1].valid!=1u) m|=STFAIL_P1_VALID;
    if (s->planes[0].baseline_class!=VBE_B_BASELINE_CANONICAL_IDENTITY) m|=STFAIL_P0_CLASS;
    if (s->planes[1].baseline_class!=VBE_B_BASELINE_CANONICAL_IDENTITY) m|=STFAIL_P1_CLASS;
    if (s->planes[0].last_sony_return!=0) m|=STFAIL_P0_SONY;
    if (s->planes[1].last_sony_return!=0) m|=STFAIL_P1_SONY;
    if (!words_equal(s->planes[0].source_words,canonical)) m|=STFAIL_P0_SOURCE;
    if (!words_equal(s->planes[1].source_words,canonical)) m|=STFAIL_P1_SOURCE;
    if (s->planes[0].baseline_mismatch_count!=0u || s->planes[1].baseline_mismatch_count!=0u ||
        s->planes[0].overflow_count!=0u || s->planes[1].overflow_count!=0u ||
        s->planes[0].policy_read_fail_count!=0u || s->planes[1].policy_read_fail_count!=0u)
        m|=STFAIL_PLANE_DIAGNOSTICS;
    return m;
}

void c2_set_natural(uint32_t p0,uint32_t p1) { natural0=p0; natural1=p1; }
static int natural_unchanged(const VbeMatrixBackendStatus *s) {
    return s->planes[0].pristine_generation==natural0 && s->planes[1].pristine_generation==natural1;
}
static void expected_forward(const C2Probe *p,uint32_t out[15]) {
    unsigned i;
    for (i=0;i<6;++i) out[i]=canonical[i];
    for (i=0;i<9;++i) out[6+i]=(uint32_t)p->matrix[i]&0xFFFu;
}

int c2_preflight_ok(const VbeMatrixBackendStatus *s) {
    return common_fail(s)==0u && s->policy_enabled==0u &&
        words_equal(s->planes[0].forward_words,canonical) &&
        words_equal(s->planes[1].forward_words,canonical);
}
int c2_preflight_recoverable_active(const VbeMatrixBackendStatus *s) {
    return common_fail(s)==0u && s->policy_enabled==1u;
}
static int probe_ok(const C2Probe *p,const VbeMatrixBackendStatus *s) {
    uint32_t exp[15]; expected_forward(p,exp);
    return common_fail(s)==0u && natural_unchanged(s) && s->policy_enabled==1u &&
        s->last_request_result==VBE_MATRIX_RESULT_APPLIED &&
        s->planes[0].last_forwarded_policy_generation==s->active_published_generation &&
        s->planes[1].last_forwarded_policy_generation==s->active_published_generation &&
        words_equal(s->planes[0].forward_words,exp) && words_equal(s->planes[1].forward_words,exp);
}
static int neutral_ok(const VbeMatrixBackendStatus *s) {
    return common_fail(s)==0u && natural_unchanged(s) && s->policy_enabled==0u &&
        s->last_request_result==VBE_MATRIX_RESULT_APPLIED &&
        s->planes[0].last_forwarded_policy_generation==s->active_published_generation &&
        s->planes[1].last_forwarded_policy_generation==s->active_published_generation &&
        words_equal(s->planes[0].forward_words,canonical) && words_equal(s->planes[1].forward_words,canonical);
}

int c2_transition_probe(const C2Probe *p,const char *tag) {
    VbeMatrixRequestV1 req; VbeMatrixBackendStatus s; int ret,sr; unsigned i;
    memset(&req,0,sizeof(req)); req.size=sizeof(req); req.version=VBE_MATRIX_REQUEST_VERSION;
    for (i=0;i<9;++i) req.hardware_component_s3_9[i]=p->matrix[i];
    ret=vitabrightMatrixSetRequest(&req); memset(&s,0,sizeof(s)); sr=vitabrightMatrixGetStatus(&s);
    c2_log_status(tag,p->id,ret,&s);
    if (sr<0) return -1;
    if (!natural_unchanged(&s)) return -100;
    if (ret!=VBE_MATRIX_RESULT_APPLIED || !probe_ok(p,&s)) return -2;
    return 0;
}
int c2_transition_neutral(const C2Probe *p,const char *tag) {
    VbeMatrixBackendStatus s; int ret,sr;
    ret=vitabrightMatrixReset(); memset(&s,0,sizeof(s)); sr=vitabrightMatrixGetStatus(&s);
    c2_log_status(tag,p->id,ret,&s);
    if (sr<0) return -1;
    if (!natural_unchanged(&s)) return -100;
    if (ret!=VBE_MATRIX_RESULT_APPLIED || !neutral_ok(&s)) return -2;
    return 0;
}
int c2_verify_probe(const C2Probe *p,const char *tag) {
    VbeMatrixBackendStatus s; int sr;
    memset(&s,0,sizeof(s)); sr=vitabrightMatrixGetStatus(&s); c2_log_status(tag,p->id,sr,&s);
    if (sr<0) return -1;
    if (!natural_unchanged(&s)) return -100;
    return probe_ok(p,&s) ? 0 : -2;
}

const char *c2_change_name(uint32_t v) {
    static const char *const n[]={"INVALID","NO","YES","AMBIG"};
    return v<ARRAY_COUNT(n) ? n[v] : "INVALID";
}
const char *c2_conf_name(uint32_t v) {
    static const char *const n[]={"INVALID","CLEAR","AMBIG"};
    return v<ARRAY_COUNT(n) ? n[v] : "INVALID";
}
const char *c2_app_name(uint32_t v) {
    static const char *const n[]={"NA","RED","GREEN","BLUE","YELLOW","MAGENTA","CYAN","DARK_BLACK","SAME_HUE_BRIGHTER","SAME_HUE_DARKER","OTHER","AMBIG","VERY_DARK","ALTERED_HUE"};
    return v<ARRAY_COUNT(n) ? n[v] : "INVALID";
}
