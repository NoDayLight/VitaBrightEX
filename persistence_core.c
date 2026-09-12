#include "persistence_core.h"

static VbePersistenceOutcome make_outcome(int stage, int error) {
    VbePersistenceOutcome out;
    out.stage = stage;
    out.error = error;
    out.cleanup_stage = VBE_PERSIST_STAGE_NONE;
    out.cleanup_error = 0;
    out.committed = 0;
    return out;
}

static void run_cleanup(const VbePersistenceOps *ops,
                        VbePersistenceOutcome *out) {
    int ret = ops->cleanup_temp(ops->context);
    if (ret < 0) {
        out->cleanup_stage = VBE_PERSIST_STAGE_CLEANUP;
        out->cleanup_error = ret;
    }
}

VbePersistenceOutcome vbe_persistence_execute(const VbePersistenceOps *ops) {
    if (ops == 0 || ops->prepare == 0 || ops->open_temp == 0 ||
        ops->write_payload == 0 || ops->sync_temp == 0 ||
        ops->close_temp == 0 || ops->rename_temp == 0 ||
        ops->cleanup_temp == 0)
        return make_outcome(VBE_PERSIST_STAGE_PREPARE, -1);

    int ret = ops->prepare(ops->context);
    if (ret < 0) return make_outcome(VBE_PERSIST_STAGE_PREPARE, ret);

    ret = ops->open_temp(ops->context);
    if (ret < 0) return make_outcome(VBE_PERSIST_STAGE_OPEN, ret);

    ret = ops->write_payload(ops->context);
    if (ret < 0) {
        VbePersistenceOutcome out = make_outcome(VBE_PERSIST_STAGE_WRITE, ret);
        run_cleanup(ops, &out);
        return out;
    }

    ret = ops->sync_temp(ops->context);
    if (ret < 0) {
        VbePersistenceOutcome out = make_outcome(VBE_PERSIST_STAGE_SYNC, ret);
        run_cleanup(ops, &out);
        return out;
    }

    ret = ops->close_temp(ops->context);
    if (ret < 0) {
        VbePersistenceOutcome out = make_outcome(VBE_PERSIST_STAGE_CLOSE, ret);
        run_cleanup(ops, &out);
        return out;
    }

    ret = ops->rename_temp(ops->context);
    if (ret < 0) {
        VbePersistenceOutcome out = make_outcome(VBE_PERSIST_STAGE_RENAME, ret);
        run_cleanup(ops, &out);
        return out;
    }

    VbePersistenceOutcome out = make_outcome(VBE_PERSIST_STAGE_NONE, 0);
    out.committed = 1;
    return out;
}
