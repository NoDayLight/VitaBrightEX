#include <stdio.h>
#include "../persistence_core.h"

#define FAIL_PREPARE (-11)
#define FAIL_OPEN    (-12)
#define FAIL_WRITE   (-13)
#define FAIL_SYNC    (-14)
#define FAIL_CLOSE   (-15)
#define FAIL_RENAME  (-16)
#define FAIL_CLEANUP (-17)

typedef struct {
    int fail_stage;
    int cleanup_fail;
    int prepare_calls;
    int open_calls;
    int write_calls;
    int sync_calls;
    int close_calls;
    int rename_calls;
    int cleanup_calls;
    int authoritative_replaced;
    VbePersistenceState state;
} Fake;

static int stage_result(Fake *f, int stage, int error) {
    return f->fail_stage == stage ? error : 0;
}

static int op_prepare(void *ctx) {
    Fake *f = ctx;
    ++f->prepare_calls;
    return stage_result(f, VBE_PERSIST_STAGE_PREPARE, FAIL_PREPARE);
}
static int op_open(void *ctx) {
    Fake *f = ctx;
    ++f->open_calls;
    int ret = stage_result(f, VBE_PERSIST_STAGE_OPEN, FAIL_OPEN);
    if (ret >= 0) vbe_persistence_state_opened(&f->state);
    return ret;
}
static int op_write(void *ctx) {
    Fake *f = ctx;
    ++f->write_calls;
    return stage_result(f, VBE_PERSIST_STAGE_WRITE, FAIL_WRITE);
}
static int op_sync(void *ctx) {
    Fake *f = ctx;
    ++f->sync_calls;
    return stage_result(f, VBE_PERSIST_STAGE_SYNC, FAIL_SYNC);
}
static int op_close(void *ctx) {
    Fake *f = ctx;
    ++f->close_calls;
    int ret = stage_result(f, VBE_PERSIST_STAGE_CLOSE, FAIL_CLOSE);
    if (ret >= 0) vbe_persistence_state_closed(&f->state);
    return ret;
}
static int op_rename(void *ctx) {
    Fake *f = ctx;
    ++f->rename_calls;
    int ret = stage_result(f, VBE_PERSIST_STAGE_RENAME, FAIL_RENAME);
    if (ret >= 0) {
        vbe_persistence_state_renamed(&f->state);
        f->authoritative_replaced = 1;
    }
    return ret;
}
static int op_cleanup(void *ctx) {
    Fake *f = ctx;
    ++f->cleanup_calls;
    if (f->cleanup_fail) return FAIL_CLEANUP;
    if (f->state.fd_owned) vbe_persistence_state_closed(&f->state);
    if (f->state.temp_owned) vbe_persistence_state_temp_removed(&f->state);
    return 0;
}

static VbePersistenceOutcome run(Fake *fake) {
    VbePersistenceOps ops = {
        .context = fake,
        .prepare = op_prepare,
        .open_temp = op_open,
        .write_payload = op_write,
        .sync_temp = op_sync,
        .close_temp = op_close,
        .rename_temp = op_rename,
        .cleanup_temp = op_cleanup,
    };
    return vbe_persistence_execute(&ops);
}

static int check(int condition, const char *name) {
    if (condition) return 0;
    fprintf(stderr, "FAIL: %s\n", name);
    return 1;
}

static Fake clean_fake(void) {
    Fake f = {0};
    vbe_persistence_state_init(&f.state);
    return f;
}

int main(void) {
    int failures = 0;
    Fake f = clean_fake();
    VbePersistenceOutcome o = run(&f);
    failures += check(o.committed && o.error == 0 && f.rename_calls == 1 &&
                      f.cleanup_calls == 0 && f.authoritative_replaced &&
                      vbe_persistence_state_clean(&f.state),
                      "successful save commits exactly once and relinquishes temp ownership");

    int stages[] = { VBE_PERSIST_STAGE_PREPARE, VBE_PERSIST_STAGE_OPEN,
                     VBE_PERSIST_STAGE_WRITE, VBE_PERSIST_STAGE_SYNC,
                     VBE_PERSIST_STAGE_CLOSE, VBE_PERSIST_STAGE_RENAME };
    for (unsigned i = 0; i < sizeof(stages) / sizeof(stages[0]); ++i) {
        Fake x = clean_fake();
        x.fail_stage = stages[i];
        o = run(&x);
        failures += check(!o.committed && o.stage == stages[i], "failure reports exact stage");
        failures += check(!x.authoritative_replaced,
                          "failed persistence never marks authoritative file replaced");
        if (stages[i] < VBE_PERSIST_STAGE_RENAME)
            failures += check(x.rename_calls == 0, "pre-commit failure never renames");
        if (stages[i] >= VBE_PERSIST_STAGE_WRITE)
            failures += check(x.cleanup_calls == 1, "created temp is cleaned after failure");
        if (stages[i] != VBE_PERSIST_STAGE_CLOSE || !x.cleanup_fail)
            failures += check(vbe_persistence_state_clean(&x.state),
                              "successful cleanup relinquishes fd/temp ownership");
    }

    Fake close_cleanup = clean_fake();
    close_cleanup.fail_stage = VBE_PERSIST_STAGE_CLOSE;
    o = run(&close_cleanup);
    failures += check(!o.committed && close_cleanup.cleanup_calls == 1 &&
                      vbe_persistence_state_clean(&close_cleanup.state),
                      "close failure is recoverable only after cleanup confirms close/remove");

    Fake cleanup = clean_fake();
    cleanup.fail_stage = VBE_PERSIST_STAGE_WRITE;
    cleanup.cleanup_fail = 1;
    o = run(&cleanup);
    failures += check(o.stage == VBE_PERSIST_STAGE_WRITE && o.error == FAIL_WRITE,
                      "primary persistence failure retained");
    failures += check(o.cleanup_stage == VBE_PERSIST_STAGE_CLEANUP && o.cleanup_error == FAIL_CLEANUP,
                      "cleanup failure is independently visible");
    failures += check(cleanup.state.fd_owned && cleanup.state.temp_owned,
                      "failed cleanup retains uncertain fd/temp ownership");
    failures += check(!cleanup.authoritative_replaced,
                      "cleanup failure cannot become persistence success");

    VbePersistenceState s;
    vbe_persistence_state_init(&s);
    failures += check(vbe_persistence_state_can_open(&s), "clean state may open temp");
    vbe_persistence_state_opened(&s);
    failures += check(s.fd_owned && s.temp_owned && !vbe_persistence_state_can_open(&s),
                      "open success acquires fd and temp ownership");
    vbe_persistence_state_closed(&s);
    failures += check(!s.fd_owned && s.temp_owned,
                      "close success releases fd but keeps temp ownership");
    vbe_persistence_state_renamed(&s);
    failures += check(vbe_persistence_state_clean(&s),
                      "rename success commits and relinquishes temp ownership");

    if (failures) return 1;
    puts("production persistence regressions: OK");
    return 0;
}
