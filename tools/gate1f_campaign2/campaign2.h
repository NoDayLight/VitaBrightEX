#pragma once
#include <stdint.h>
#include "../../matrix_backend.h"

#define C2_BUILD_ID "a637f54f"
#define C1_RAW_SHA "740c6f7a345a9544dc0b9c79b38040baa663e6f8a8dbb27483d304b9901278bd"
#define C2_WORK_LOG_PATH "ux0:data/vbe_gate1f_campaign2.partial.txt"
#define C2_FINAL_LOG_PATH "ux0:data/vbe_gate1f_campaign2.txt"
#define C2_BACKUP_LOG_PATH "ux0:data/vbe_gate1f_campaign2.previous.txt"
#define C2_MIN_TOGGLES 3u

#define C2_CH_NO 1u
#define C2_CH_YES 2u
#define C2_CH_AMBIG 3u

#define C2_APP_NA 0u
#define C2_APP_RED 1u
#define C2_APP_GREEN 2u
#define C2_APP_BLUE 3u
#define C2_APP_YELLOW 4u
#define C2_APP_MAGENTA 5u
#define C2_APP_CYAN 6u
#define C2_APP_DARK_BLACK 7u
#define C2_APP_SAME_BRIGHTER 8u
#define C2_APP_SAME_DARKER 9u
#define C2_APP_OTHER 10u
#define C2_APP_AMBIG 11u
#define C2_APP_VERY_DARK 12u
#define C2_APP_ALTERED_HUE 13u

#define C2_CONF_CLEAR 1u
#define C2_CONF_AMBIG 2u

typedef struct {
    const char *id;
    uint32_t row;
    uint32_t col;
    int32_t matrix[9];
    uint32_t diagonal;
} C2Probe;

typedef struct {
    uint32_t changed[3];
    uint32_t appearance[3];
    uint32_t context[4];
    uint32_t confidence;
    uint32_t toggle_count;
    uint32_t answer_count;
} C2Observation;

typedef struct {
    uint32_t changed;
    uint32_t appearance;
    uint32_t confidence;
    uint32_t toggle_count;
    uint32_t answer_count;
    char witness[16];
} C2SignedObservation;

uint32_t c2_pack_rgb(uint32_t r, uint32_t g, uint32_t b);
int c2_ui_alloc(void);
void c2_ui_restore(void);
void c2_ui_set_title(const char *s);
void c2_ui_set_state(const char *s);
void c2_ui_draw(const char *prompt, const char *value, const char *extra);
uint32_t c2_wait_button(uint32_t mask);

int c2_log_begin(void);
int c2_log_finalize(void);
int c2_log(const char *fmt, ...);
int c2_log_failed(void);
int c2_log_status(const char *tag, const char *id, int action_result, const VbeMatrixBackendStatus *s);
int c2_preflight_ok(const VbeMatrixBackendStatus *s);
int c2_active_policy_base_ok(const VbeMatrixBackendStatus *s);
int c2_status_matches_probe(const VbeMatrixBackendStatus *s, const C2Probe *p);
void c2_set_natural(uint32_t p0, uint32_t p1);
int c2_transition_probe(const C2Probe *p, const char *tag);
int c2_transition_neutral(const C2Probe *p, const char *tag);
int c2_verify_probe(const C2Probe *p, const char *tag);
const char *c2_change_name(uint32_t v);
const char *c2_app_name(uint32_t v);
const char *c2_conf_name(uint32_t v);
