#include "matrix_backend.h"
#include "matrix_policy_core.h"
#include "matrix_authority_core.h"
#include "matrix_apply_txn_core.h"
#include "taihen_extra.h"
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/kernel/threadmgr/thread.h>
#include <psp2kern/lowio/iftu.h>
#include <taihen.h>

#define NID_STAGE_B 0xD64F4C6Bu
#define STORE_READ_RETRIES 8u
#define IMMEDIATE_MAX_REBASE 1u
#define REPLAY_OK 0
#define REPLAY_RACE 1
#define REPLAY_FAIL (-1)
#define MATRIX_ASSERT(n, e) typedef char n[(e) ? 1 : -1]
MATRIX_ASSERT(matrix_b_object_3c, sizeof(VbeBStageObject) == 0x3Cu);
MATRIX_ASSERT(matrix_sony_csc_3c, sizeof(SceIftuCscParams) == 0x3Cu);
MATRIX_ASSERT(matrix_owned_alignment, __alignof__(VbeBStageObject) >= 4u);
#undef MATRIX_ASSERT

typedef int (*StageBEntry)(int plane, const SceIftuCscParams *params);

typedef struct {
    volatile uint32_t writer;
    volatile uint32_t seq;
    uint32_t valid;
    uint32_t stale;
    uint32_t natural_generation;
    uint32_t baseline_class;
    uint32_t source_words[VBE_B_OBJECT_WORDS];
} VbeNaturalAuthorityStore;

typedef struct {
    volatile uint32_t writer;
    volatile uint32_t seq;
    uint32_t valid;
    uint32_t forward_words[VBE_B_OBJECT_WORDS];
} VbeAppliedForwardStore;

typedef struct {
    volatile uint32_t active;
    volatile uint32_t epoch;
    volatile uint32_t owner_thread_id;
    volatile uint32_t expected_plane;
    volatile uint32_t expected_natural_generation;
    volatile uint32_t expected_baseline_class;
    volatile uint32_t seen_count;
    volatile uint32_t seen_epoch;
    volatile uint32_t fault_flags;
    VbeBStageObject expected_source;
} VbeReplayContext;

typedef struct {
    VbeMatrixPolicySnapshot old_policy;
    VbeNaturalBSource authority[2];
    uint32_t old_applied_generation[2];
} VbeTxnPreflight;

static VbeMatrixPolicyStore g_policy;
static VbeNaturalAuthorityStore g_authority[2];
static VbeAppliedForwardStore g_forward[2];
static VbeReplayContext g_replay_ctx;

static volatile uint32_t g_applied_generation[2];
static volatile int32_t g_last_sony_return[2];
static volatile uint32_t g_baseline_mismatch_count[2];
static volatile uint32_t g_overflow_count[2];
static volatile uint32_t g_policy_read_fail_count[2];
static volatile uint32_t g_status_update_drop_count;
static volatile uint32_t g_request_validation_fail_count;
static volatile uint32_t g_requested_generation;
static volatile int32_t g_last_request_result;
static volatile uint32_t g_target_supported;
static volatile uint32_t g_hook_owned;
static volatile uint32_t g_hook_fail;
static volatile uint32_t g_immediate_supported;
static volatile uint32_t g_authority_stale_mask;
static volatile uint32_t g_degraded_plane_mask;
static volatile uint32_t g_transaction_state;
static volatile uint32_t g_transaction_fault_flags;
static volatile uint32_t g_transaction_owner;
static volatile uint32_t g_replay_epoch;
#ifdef VBE_ENABLE_RESEARCH_FAULT_INJECTION
static volatile uint32_t g_test_abort_before_p1_once;
#endif
static uintptr_t g_replay_entry;
static tai_hook_ref_t g_b_ref;

static void zero_bytes(void *dst, uint32_t size) {
    uint8_t *p = (uint8_t *)dst;
    uint32_t i;
    for (i = 0; i < size; ++i) p[i] = 0u;
}

static void copy_b(VbeBStageObject *dst, const VbeBStageObject *src) {
    uint32_t i;
    for (i = 0; i < VBE_B_OBJECT_WORDS; ++i) dst->words[i] = src->words[i];
}

static void copy_authority(VbeNaturalBSource *dst,
                           const VbeNaturalBSource *src) {
    dst->valid = src->valid;
    dst->stale = src->stale;
    dst->natural_generation = src->natural_generation;
    dst->baseline_class = src->baseline_class;
    copy_b(&dst->source, &src->source);
}

static int matrix_equal(const VbeMatrixS39 *a, const VbeMatrixS39 *b) {
    uint32_t i;
    if (!a || !b) return 0;
    for (i = 0; i < VBE_MATRIX_COEFFS; ++i)
        if (a->v[i] != b->v[i]) return 0;
    return 1;
}

static int matrix_is_identity(const VbeMatrixS39 *m) {
    VbeMatrixS39 identity;
    vbe_matrix_identity(&identity);
    return matrix_equal(m, &identity);
}

static uint32_t current_thread_id(void) {
    int tid = ksceKernelGetThreadId();
    return tid < 0 ? 0u : (uint32_t)tid;
}

static void authority_store_init(uint32_t plane) {
    VbeNaturalAuthorityStore *s = &g_authority[plane];
    uint32_t i;
    s->writer = 0u;
    s->seq = 0u;
    s->valid = 0u;
    s->stale = 0u;
    s->natural_generation = 0u;
    s->baseline_class = VBE_B_BASELINE_UNKNOWN;
    for (i = 0; i < VBE_B_OBJECT_WORDS; ++i) s->source_words[i] = 0u;
}

static void forward_store_init(uint32_t plane) {
    VbeAppliedForwardStore *s = &g_forward[plane];
    uint32_t i;
    s->writer = 0u;
    s->seq = 0u;
    s->valid = 0u;
    for (i = 0; i < VBE_B_OBJECT_WORDS; ++i) s->forward_words[i] = 0u;
}

static int authority_store_read(uint32_t plane, VbeNaturalBSource *out) {
    VbeNaturalAuthorityStore *s;
    uint32_t attempt;
    if (plane > 1u || !out) return VBE_AUTHORITY_INVALID;
    s = &g_authority[plane];
    for (attempt = 0u; attempt < STORE_READ_RETRIES; ++attempt) {
        uint32_t seq1, seq2, i;
        VbeNaturalBSource tmp;
        seq1 = __atomic_load_n(&s->seq, __ATOMIC_ACQUIRE);
        if (seq1 & 1u) continue;
        tmp.valid = __atomic_load_n(&s->valid, __ATOMIC_RELAXED);
        tmp.stale = __atomic_load_n(&s->stale, __ATOMIC_RELAXED);
        tmp.natural_generation =
            __atomic_load_n(&s->natural_generation, __ATOMIC_RELAXED);
        tmp.baseline_class = (VbeBBaselineClass)
            __atomic_load_n(&s->baseline_class, __ATOMIC_RELAXED);
        for (i = 0; i < VBE_B_OBJECT_WORDS; ++i)
            tmp.source.words[i] = __atomic_load_n(&s->source_words[i], __ATOMIC_RELAXED);
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        seq2 = __atomic_load_n(&s->seq, __ATOMIC_ACQUIRE);
        if (seq1 == seq2 && !(seq2 & 1u)) {
            copy_authority(out, &tmp);
            return VBE_AUTHORITY_OK;
        }
    }
    return VBE_AUTHORITY_STALE;
}

static int authority_store_commit_natural(uint32_t plane,
                                          const VbeBStageObject *source,
                                          int sony_return) {
    VbeNaturalAuthorityStore *s;
    VbeNaturalBSource current, next;
    uint32_t expected = 0u, seq, i;
    int ret;
    if (plane > 1u || !source) return VBE_AUTHORITY_INVALID;
    if (sony_return < 0) return VBE_AUTHORITY_NO_CHANGE;
    s = &g_authority[plane];
    if (!__atomic_compare_exchange_n(&s->writer, &expected, 1u, 0,
                                     __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        __atomic_store_n(&s->stale, 1u, __ATOMIC_RELEASE);
        __atomic_or_fetch(&g_authority_stale_mask, 1u << plane, __ATOMIC_RELAXED);
        return VBE_AUTHORITY_STALE;
    }
    /* A later uncontended natural call may repair a previous stale authority.
     * Any new collision during THIS write sets the sticky bit again. */
    __atomic_and_fetch(&g_authority_stale_mask, ~(1u << plane),
                       __ATOMIC_ACQ_REL);
    seq = __atomic_load_n(&s->seq, __ATOMIC_RELAXED);
    if (seq & 1u) ++seq;
    __atomic_store_n(&s->seq, seq + 1u, __ATOMIC_RELEASE);

    current.valid = __atomic_load_n(&s->valid, __ATOMIC_RELAXED);
    current.stale = __atomic_load_n(&s->stale, __ATOMIC_RELAXED);
    current.natural_generation = __atomic_load_n(&s->natural_generation, __ATOMIC_RELAXED);
    current.baseline_class = (VbeBBaselineClass)
        __atomic_load_n(&s->baseline_class, __ATOMIC_RELAXED);
    for (i = 0; i < VBE_B_OBJECT_WORDS; ++i)
        current.source.words[i] = __atomic_load_n(&s->source_words[i], __ATOMIC_RELAXED);

    ret = vbe_natural_authority_after_call(&current,
            VBE_B_ORIGIN_NATURAL_SONY, source, sony_return, &next);
    if (ret == VBE_AUTHORITY_OK || ret == VBE_AUTHORITY_OVERFLOW) {
        __atomic_store_n(&s->valid, next.valid, __ATOMIC_RELAXED);
        __atomic_store_n(&s->stale, next.stale, __ATOMIC_RELAXED);
        __atomic_store_n(&s->natural_generation, next.natural_generation, __ATOMIC_RELAXED);
        __atomic_store_n(&s->baseline_class, (uint32_t)next.baseline_class, __ATOMIC_RELAXED);
        for (i = 0; i < VBE_B_OBJECT_WORDS; ++i)
            __atomic_store_n(&s->source_words[i], next.source.words[i], __ATOMIC_RELAXED);
        /* If another natural writer collided while this writer was active,
         * its source could have been missed. Preserve stale until a later
         * uncontended natural call refreshes the authority. */
        if (__atomic_load_n(&g_authority_stale_mask, __ATOMIC_ACQUIRE) &
            (1u << plane)) {
            next.stale = 1u;
            __atomic_store_n(&s->stale, 1u, __ATOMIC_RELAXED);
        } else if (next.stale) {
            __atomic_or_fetch(&g_authority_stale_mask, 1u << plane,
                              __ATOMIC_RELAXED);
        }
    }
    __atomic_thread_fence(__ATOMIC_RELEASE);
    __atomic_store_n(&s->seq, seq + 2u, __ATOMIC_RELEASE);
    __atomic_store_n(&s->writer, 0u, __ATOMIC_RELEASE);
    return ret;
}

static int forward_store_read(uint32_t plane, VbeBStageObject *out, uint32_t *valid) {
    VbeAppliedForwardStore *s;
    uint32_t attempt;
    if (plane > 1u || !out || !valid) return -1;
    s = &g_forward[plane];
    for (attempt = 0u; attempt < STORE_READ_RETRIES; ++attempt) {
        uint32_t seq1, seq2, i, v;
        VbeBStageObject tmp;
        seq1 = __atomic_load_n(&s->seq, __ATOMIC_ACQUIRE);
        if (seq1 & 1u) continue;
        v = __atomic_load_n(&s->valid, __ATOMIC_RELAXED);
        for (i = 0; i < VBE_B_OBJECT_WORDS; ++i)
            tmp.words[i] = __atomic_load_n(&s->forward_words[i], __ATOMIC_RELAXED);
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        seq2 = __atomic_load_n(&s->seq, __ATOMIC_ACQUIRE);
        if (seq1 == seq2 && !(seq2 & 1u)) {
            *valid = v;
            copy_b(out, &tmp);
            return 0;
        }
    }
    return -1;
}

static void forward_store_publish(uint32_t plane, const VbeBStageObject *forwarded) {
    VbeAppliedForwardStore *s;
    uint32_t expected = 0u, seq, i;
    if (plane > 1u || !forwarded) return;
    s = &g_forward[plane];
    if (!__atomic_compare_exchange_n(&s->writer, &expected, 1u, 0,
                                     __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        __atomic_add_fetch(&g_status_update_drop_count, 1u, __ATOMIC_RELAXED);
        return;
    }
    seq = __atomic_load_n(&s->seq, __ATOMIC_RELAXED);
    if (seq & 1u) ++seq;
    __atomic_store_n(&s->seq, seq + 1u, __ATOMIC_RELEASE);
    __atomic_store_n(&s->valid, 1u, __ATOMIC_RELAXED);
    for (i = 0; i < VBE_B_OBJECT_WORDS; ++i)
        __atomic_store_n(&s->forward_words[i], forwarded->words[i], __ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    __atomic_store_n(&s->seq, seq + 2u, __ATOMIC_RELEASE);
    __atomic_store_n(&s->writer, 0u, __ATOMIC_RELEASE);
}

static void replay_fault_or(uint32_t flags) {
    __atomic_or_fetch(&g_replay_ctx.fault_flags, flags, __ATOMIC_RELAXED);
}

static VbeBCallOrigin replay_context_classify(int plane,
                                               const VbeBStageObject *source) {
    VbeNaturalBSource auth;
    uint32_t seen, i;
    if (!__atomic_load_n(&g_replay_ctx.active, __ATOMIC_ACQUIRE))
        return VBE_B_ORIGIN_NATURAL_SONY;
    if (current_thread_id() !=
        __atomic_load_n(&g_replay_ctx.owner_thread_id, __ATOMIC_ACQUIRE))
        return VBE_B_ORIGIN_NATURAL_SONY;

    seen = __atomic_fetch_add(&g_replay_ctx.seen_count, 1u, __ATOMIC_ACQ_REL);
    if (seen != 0u) replay_fault_or(VBE_MATRIX_TXFAULT_RECURSION_OR_OVERLAP);
    __atomic_store_n(&g_replay_ctx.seen_epoch,
                     __atomic_load_n(&g_replay_ctx.epoch, __ATOMIC_RELAXED),
                     __ATOMIC_RELEASE);
    if (plane < 0 || plane > 1 ||
        (uint32_t)plane != __atomic_load_n(&g_replay_ctx.expected_plane, __ATOMIC_RELAXED))
        replay_fault_or(VBE_MATRIX_TXFAULT_PLANE_MISMATCH);
    if (!source) {
        replay_fault_or(VBE_MATRIX_TXFAULT_SOURCE_MISMATCH);
    } else {
        for (i = 0; i < VBE_B_OBJECT_WORDS; ++i) {
            if (source->words[i] != g_replay_ctx.expected_source.words[i]) {
                replay_fault_or(VBE_MATRIX_TXFAULT_SOURCE_MISMATCH);
                break;
            }
        }
    }
    if (plane < 0 || plane > 1 || authority_store_read((uint32_t)plane, &auth) < 0 ||
        auth.natural_generation !=
            __atomic_load_n(&g_replay_ctx.expected_natural_generation, __ATOMIC_RELAXED) ||
        (uint32_t)auth.baseline_class !=
            __atomic_load_n(&g_replay_ctx.expected_baseline_class, __ATOMIC_RELAXED))
        replay_fault_or(VBE_MATRIX_TXFAULT_AUTHORITY_CHANGED);
    return VBE_B_ORIGIN_INTERNAL_REPLAY;
}

static int replay_context_begin(uint32_t plane, const VbeNaturalBSource *auth,
                                const VbeBStageObject *seed) {
    uint32_t i, epoch;
    if (!auth || !seed || __atomic_load_n(&g_replay_ctx.active, __ATOMIC_ACQUIRE))
        return -1;
    epoch = __atomic_add_fetch(&g_replay_epoch, 1u, __ATOMIC_RELAXED);
    __atomic_store_n(&g_replay_ctx.epoch, epoch, __ATOMIC_RELAXED);
    __atomic_store_n(&g_replay_ctx.owner_thread_id, current_thread_id(), __ATOMIC_RELAXED);
    __atomic_store_n(&g_replay_ctx.expected_plane, plane, __ATOMIC_RELAXED);
    __atomic_store_n(&g_replay_ctx.expected_natural_generation,
                     auth->natural_generation, __ATOMIC_RELAXED);
    __atomic_store_n(&g_replay_ctx.expected_baseline_class,
                     (uint32_t)auth->baseline_class, __ATOMIC_RELAXED);
    __atomic_store_n(&g_replay_ctx.seen_count, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&g_replay_ctx.seen_epoch, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&g_replay_ctx.fault_flags, 0u, __ATOMIC_RELAXED);
    for (i = 0; i < VBE_B_OBJECT_WORDS; ++i)
        g_replay_ctx.expected_source.words[i] = seed->words[i];
    __atomic_thread_fence(__ATOMIC_RELEASE);
    __atomic_store_n(&g_replay_ctx.active, 1u, __ATOMIC_RELEASE);
    return 0;
}

static uint32_t replay_context_finish(void) {
    uint32_t seen, faults, epoch, seen_epoch;
    __atomic_store_n(&g_replay_ctx.active, 0u, __ATOMIC_RELEASE);
    seen = __atomic_load_n(&g_replay_ctx.seen_count, __ATOMIC_ACQUIRE);
    epoch = __atomic_load_n(&g_replay_ctx.epoch, __ATOMIC_RELAXED);
    seen_epoch = __atomic_load_n(&g_replay_ctx.seen_epoch, __ATOMIC_RELAXED);
    faults = __atomic_load_n(&g_replay_ctx.fault_flags, __ATOMIC_ACQUIRE);
    if (seen == 0u || seen_epoch != epoch) faults |= VBE_MATRIX_TXFAULT_HOOK_NOT_SEEN;
    if (seen > 1u) faults |= VBE_MATRIX_TXFAULT_RECURSION_OR_OVERLAP;
    __atomic_store_n(&g_replay_ctx.fault_flags, faults, __ATOMIC_RELEASE);
    return faults;
}

static int hook_b(int plane, const SceIftuCscParams *params) {
    VbeBStageObject source_snapshot, owned_result, forwarded_snapshot;
    VbeMatrixPolicySnapshot policy;
    VbeBBaselineClass cls;
    VbeBCallOrigin origin;
    const SceIftuCscParams *forward = params;
    uint32_t applied_generation;
    int prep = VBE_MATRIX_CORE_INVALID;
    int policy_ok = 0;
    int ret;

    if ((plane != 0 && plane != 1) || params == 0) {
        if (__atomic_load_n(&g_replay_ctx.active, __ATOMIC_ACQUIRE) &&
            current_thread_id() ==
                __atomic_load_n(&g_replay_ctx.owner_thread_id, __ATOMIC_ACQUIRE))
            (void)replay_context_classify(plane, 0);
        return TAI_CONTINUE(int, g_b_ref, plane, params);
    }

    /* The invocation source is dereferenced exactly once into this local snapshot. */
    vbe_b_object_copy_from_volatile(&source_snapshot, params);
    copy_b(&forwarded_snapshot, &source_snapshot);
    origin = replay_context_classify(plane, &source_snapshot);
    cls = vbe_b_baseline_classify(&source_snapshot);

    if (vbe_matrix_policy_read(&g_policy, &policy) < 0) {
        __atomic_add_fetch(&g_policy_read_fail_count[plane], 1u, __ATOMIC_RELAXED);
    } else {
        policy_ok = 1;
        if (!policy.enabled) {
            prep = VBE_MATRIX_CORE_OK;
        } else if (cls == VBE_B_BASELINE_UNKNOWN) {
            __atomic_add_fetch(&g_baseline_mismatch_count[plane], 1u, __ATOMIC_RELAXED);
            prep = VBE_MATRIX_CORE_UNKNOWN_BASELINE;
        } else {
            prep = vbe_b_object_compose(&source_snapshot, &policy.matrix,
                                        &owned_result, &cls);
            if (prep == VBE_MATRIX_CORE_OK) {
                copy_b(&forwarded_snapshot, &owned_result);
                forward = (const SceIftuCscParams *)(const void *)&owned_result;
            } else if (prep == VBE_MATRIX_CORE_OVERFLOW) {
                __atomic_add_fetch(&g_overflow_count[plane], 1u, __ATOMIC_RELAXED);
            }
        }
    }

    ret = TAI_CONTINUE(int, g_b_ref, plane, forward);
    __atomic_store_n(&g_last_sony_return[plane], ret, __ATOMIC_RELEASE);

    if (ret >= 0) {
        if (origin == VBE_B_ORIGIN_NATURAL_SONY)
            (void)authority_store_commit_natural((uint32_t)plane,
                                                 &source_snapshot, ret);
        if (policy_ok && prep == VBE_MATRIX_CORE_OK)
            applied_generation = policy.generation;
        else
            applied_generation = 0u;
        __atomic_store_n(&g_applied_generation[plane], applied_generation,
                         __ATOMIC_RELEASE);
        forward_store_publish((uint32_t)plane, &forwarded_snapshot);
    }
    return ret;
}

static int request_valid(const VbeMatrixRequestV1 *r, VbeMatrixS39 *matrix) {
    uint32_t i;
    if (!r || !matrix || r->size != sizeof(*r) ||
        r->version != VBE_MATRIX_REQUEST_VERSION || r->flags != 0u ||
        r->reserved0 != 0u)
        return 0;
    for (i = 0; i < 4u; ++i) if (r->reserved[i] != 0u) return 0;
    for (i = 0; i < VBE_MATRIX_COEFFS; ++i)
        matrix->v[i] = r->hardware_component_s3_9[i];
    return vbe_matrix_validate(matrix) == VBE_MATRIX_CORE_OK;
}

static int publish_policy(const VbeMatrixS39 *matrix, uint32_t enabled,
                          uint32_t *generation) {
    uint32_t gen = 0u;
    int ret;
    ret = vbe_matrix_policy_publish(&g_policy, matrix, enabled, &gen);
    if (ret < 0) return VBE_MATRIX_RESULT_POLICY;
    __atomic_store_n(&g_requested_generation, gen, __ATOMIC_RELEASE);
    if (generation) *generation = gen;
    return VBE_MATRIX_RESULT_APPLIED;
}

static int authority_preflight(VbeNaturalBSource auth[2]) {
    VbeBStageObject seed;
    int result = VBE_MATRIX_RESULT_APPLIED;
    uint32_t plane;
    if (!__atomic_load_n(&g_immediate_supported, __ATOMIC_ACQUIRE))
        return VBE_MATRIX_RESULT_ACCEPTED_PENDING_REPLAY;
    for (plane = 0u; plane < 2u; ++plane) {
        int ar = authority_store_read(plane, &auth[plane]);
        if (ar < 0 || auth[plane].stale) {
            result = VBE_MATRIX_RESULT_STALE_PRISTINE;
            continue;
        }
        ar = vbe_natural_authority_seed(&auth[plane], &seed);
        if (ar == VBE_AUTHORITY_NO_SOURCE) {
            if (result == VBE_MATRIX_RESULT_APPLIED)
                result = VBE_MATRIX_RESULT_ACCEPTED_PENDING_REPLAY;
        } else if (ar == VBE_AUTHORITY_UNSUPPORTED) {
            if (result != VBE_MATRIX_RESULT_STALE_PRISTINE)
                result = VBE_MATRIX_RESULT_UNSUPPORTED_BASELINE;
        } else if (ar == VBE_AUTHORITY_STALE || ar < 0) {
            result = VBE_MATRIX_RESULT_STALE_PRISTINE;
        }
    }
    return result;
}

static int authority_pair_changed(const VbeNaturalBSource a[2],
                                  const VbeNaturalBSource b[2]) {
    uint32_t plane;
    for (plane = 0u; plane < 2u; ++plane) {
        if (a[plane].valid != b[plane].valid ||
            a[plane].stale != b[plane].stale ||
            a[plane].natural_generation != b[plane].natural_generation ||
            a[plane].baseline_class != b[plane].baseline_class)
            return 1;
    }
    return 0;
}

static int pair_applied(uint32_t generation) {
    uint32_t plane;
    for (plane = 0u; plane < 2u; ++plane) {
        if (__atomic_load_n(&g_applied_generation[plane], __ATOMIC_ACQUIRE) != generation ||
            __atomic_load_n(&g_last_sony_return[plane], __ATOMIC_ACQUIRE) < 0)
            return 0;
    }
    return 1;
}

static int replay_plane(uint32_t plane, const VbeNaturalBSource *auth,
                        uint32_t target_generation) {
    VbeBStageObject seed;
    StageBEntry fn;
    uint32_t faults, serious;
    int ret;
    if (!auth || plane > 1u || !g_replay_entry)
        return REPLAY_FAIL;
    if (vbe_natural_authority_seed(auth, &seed) != VBE_AUTHORITY_OK)
        return REPLAY_RACE;
    if (replay_context_begin(plane, auth, &seed) < 0) {
        __atomic_or_fetch(&g_transaction_fault_flags,
                          VBE_MATRIX_TXFAULT_RECURSION_OR_OVERLAP,
                          __ATOMIC_RELAXED);
        return REPLAY_FAIL;
    }

    fn = (StageBEntry)(uintptr_t)g_replay_entry;
    ret = fn((int)plane, (const SceIftuCscParams *)(const void *)&seed);
    faults = replay_context_finish();
    if (ret < 0) faults |= VBE_MATRIX_TXFAULT_ENTRY_RETURN;
    if (__atomic_load_n(&g_applied_generation[plane], __ATOMIC_ACQUIRE) !=
            target_generation ||
        __atomic_load_n(&g_last_sony_return[plane], __ATOMIC_ACQUIRE) < 0)
        faults |= VBE_MATRIX_TXFAULT_APPLY_VERIFY;
    __atomic_or_fetch(&g_transaction_fault_flags, faults, __ATOMIC_RELAXED);
    if (faults == 0u) return REPLAY_OK;
    serious = faults & ~(VBE_MATRIX_TXFAULT_AUTHORITY_CHANGED |
                         VBE_MATRIX_TXFAULT_APPLY_VERIFY);
    if (serious == 0u && (faults & VBE_MATRIX_TXFAULT_AUTHORITY_CHANGED))
        return REPLAY_RACE;
    return REPLAY_FAIL;
}

static void sync_tx_state(const VbeMatrixApplyTxn *txn) {
    if (txn)
        __atomic_store_n(&g_transaction_state, (uint32_t)txn->state,
                         __ATOMIC_RELEASE);
}

static int tx_step(VbeMatrixApplyTxn *txn, VbeMatrixTxnEvent event) {
    int ret = vbe_matrix_txn_step(txn, event);
    sync_tx_state(txn);
    return ret;
}

static int applied_or_rebase(VbeMatrixApplyTxn *txn,
                             const VbeNaturalBSource seed_auth[2],
                             uint32_t target_generation) {
    VbeNaturalBSource now[2];
    int ready = authority_preflight(now);
    if (pair_applied(target_generation)) return VBE_MATRIX_RESULT_APPLIED;
    if (ready != VBE_MATRIX_RESULT_APPLIED || authority_pair_changed(seed_auth, now)) {
        __atomic_or_fetch(&g_transaction_fault_flags,
                          VBE_MATRIX_TXFAULT_AUTHORITY_CHANGED,
                          __ATOMIC_RELAXED);
        if (tx_step(txn, VBE_MATRIX_TXN_EVENT_REBASE_REQUIRED) < 0)
            return VBE_MATRIX_RESULT_REPLAY_FAILED;
        if (txn->final == VBE_MATRIX_TXN_FINAL_PENDING)
            return VBE_MATRIX_RESULT_ACCEPTED_PENDING_REPLAY;
        return VBE_MATRIX_RESULT_BUSY; /* internal sentinel: retry */
    }
    return VBE_MATRIX_RESULT_REPLAY_FAILED;
}

static int immediate_apply_generation(VbeMatrixApplyTxn *txn,
                                      uint32_t target_generation,
                                      const VbeNaturalBSource initial_auth[2],
                                      int allow_test_abort) {
    VbeNaturalBSource seed_auth[2], now[2];
    uint32_t plane;
    int rr, check;
#ifndef VBE_ENABLE_RESEARCH_FAULT_INJECTION
    (void)allow_test_abort;
#endif

    for (plane = 0u; plane < 2u; ++plane)
        copy_authority(&seed_auth[plane], &initial_auth[plane]);

    for (;;) {
        if (txn->rebase_count != 0u) {
            if (authority_preflight(seed_auth) != VBE_MATRIX_RESULT_APPLIED) {
                if (tx_step(txn, VBE_MATRIX_TXN_EVENT_REBASE_REQUIRED) < 0)
                    return VBE_MATRIX_RESULT_REPLAY_FAILED;
                if (txn->final == VBE_MATRIX_TXN_FINAL_PENDING)
                    return VBE_MATRIX_RESULT_ACCEPTED_PENDING_REPLAY;
                continue;
            }
        }

        if (authority_preflight(now) != VBE_MATRIX_RESULT_APPLIED ||
            authority_pair_changed(seed_auth, now)) {
            __atomic_or_fetch(&g_transaction_fault_flags,
                              VBE_MATRIX_TXFAULT_AUTHORITY_CHANGED,
                              __ATOMIC_RELAXED);
            if (tx_step(txn, VBE_MATRIX_TXN_EVENT_REBASE_REQUIRED) < 0)
                return VBE_MATRIX_RESULT_REPLAY_FAILED;
            if (txn->final == VBE_MATRIX_TXN_FINAL_PENDING)
                return VBE_MATRIX_RESULT_ACCEPTED_PENDING_REPLAY;
            continue;
        }

        if (pair_applied(target_generation)) {
            (void)tx_step(txn, VBE_MATRIX_TXN_EVENT_REPLAY_OK);
            (void)tx_step(txn, VBE_MATRIX_TXN_EVENT_REPLAY_OK);
            (void)tx_step(txn, VBE_MATRIX_TXN_EVENT_VERIFY_OK);
            return VBE_MATRIX_RESULT_APPLIED;
        }

        rr = replay_plane(0u, &seed_auth[0], target_generation);
        if (rr == REPLAY_RACE) {
            check = applied_or_rebase(txn, seed_auth, target_generation);
            if (check == VBE_MATRIX_RESULT_APPLIED) {
                (void)tx_step(txn, VBE_MATRIX_TXN_EVENT_REPLAY_OK);
                (void)tx_step(txn, VBE_MATRIX_TXN_EVENT_REPLAY_OK);
                (void)tx_step(txn, VBE_MATRIX_TXN_EVENT_VERIFY_OK);
                return VBE_MATRIX_RESULT_APPLIED;
            }
            if (check == VBE_MATRIX_RESULT_BUSY) continue;
            return check;
        }
        if (rr != REPLAY_OK) {
            (void)tx_step(txn, VBE_MATRIX_TXN_EVENT_REPLAY_FAIL);
            return VBE_MATRIX_RESULT_REPLAY_FAILED;
        }
        (void)tx_step(txn, VBE_MATRIX_TXN_EVENT_REPLAY_OK);

#ifdef VBE_ENABLE_RESEARCH_FAULT_INJECTION
        if (allow_test_abort &&
            __atomic_exchange_n(&g_test_abort_before_p1_once, 0u, __ATOMIC_ACQ_REL)) {
            __atomic_or_fetch(&g_transaction_fault_flags,
                              VBE_MATRIX_TXFAULT_INJECTED_P1_ABORT,
                              __ATOMIC_RELAXED);
            (void)tx_step(txn, VBE_MATRIX_TXN_EVENT_REPLAY_FAIL);
            return VBE_MATRIX_RESULT_REPLAY_FAILED;
        }
#endif

        if (authority_preflight(now) != VBE_MATRIX_RESULT_APPLIED ||
            authority_pair_changed(seed_auth, now)) {
            check = applied_or_rebase(txn, seed_auth, target_generation);
            if (check == VBE_MATRIX_RESULT_APPLIED) {
                (void)tx_step(txn, VBE_MATRIX_TXN_EVENT_REPLAY_OK);
                (void)tx_step(txn, VBE_MATRIX_TXN_EVENT_VERIFY_OK);
                return VBE_MATRIX_RESULT_APPLIED;
            }
            if (check == VBE_MATRIX_RESULT_BUSY) continue;
            return check;
        }

        rr = replay_plane(1u, &seed_auth[1], target_generation);
        if (rr == REPLAY_RACE) {
            check = applied_or_rebase(txn, seed_auth, target_generation);
            if (check == VBE_MATRIX_RESULT_APPLIED) {
                (void)tx_step(txn, VBE_MATRIX_TXN_EVENT_REPLAY_OK);
                (void)tx_step(txn, VBE_MATRIX_TXN_EVENT_VERIFY_OK);
                return VBE_MATRIX_RESULT_APPLIED;
            }
            if (check == VBE_MATRIX_RESULT_BUSY) continue;
            return check;
        }
        if (rr != REPLAY_OK) {
            (void)tx_step(txn, VBE_MATRIX_TXN_EVENT_REPLAY_FAIL);
            return VBE_MATRIX_RESULT_REPLAY_FAILED;
        }
        (void)tx_step(txn, VBE_MATRIX_TXN_EVENT_REPLAY_OK);

        if (pair_applied(target_generation)) {
            (void)tx_step(txn, VBE_MATRIX_TXN_EVENT_VERIFY_OK);
            return VBE_MATRIX_RESULT_APPLIED;
        }
        check = applied_or_rebase(txn, seed_auth, target_generation);
        if (check == VBE_MATRIX_RESULT_BUSY) continue;
        if (check == VBE_MATRIX_RESULT_ACCEPTED_PENDING_REPLAY) return check;
        (void)tx_step(txn, VBE_MATRIX_TXN_EVENT_VERIFY_FAIL);
        return VBE_MATRIX_RESULT_REPLAY_FAILED;
    }
}

static void update_degraded_mask(uint32_t expected_generation) {
    uint32_t mask = 0u, plane;
    for (plane = 0u; plane < 2u; ++plane)
        if (__atomic_load_n(&g_applied_generation[plane], __ATOMIC_ACQUIRE) !=
            expected_generation)
            mask |= 1u << plane;
    __atomic_store_n(&g_degraded_plane_mask, mask, __ATOMIC_RELEASE);
}

static int rollback_previous(VbeMatrixApplyTxn *txn,
                             const VbeMatrixPolicySnapshot *old_policy) {
    VbeNaturalBSource auth[2];
    uint32_t rollback_generation = 0u;
    int ret, rr;
    if (!txn || !old_policy) return VBE_MATRIX_RESULT_DEGRADED_PARTIAL_APPLY;

    ret = publish_policy(&old_policy->matrix, old_policy->enabled,
                         &rollback_generation);
    if (ret < 0) {
        __atomic_or_fetch(&g_transaction_fault_flags,
                          VBE_MATRIX_TXFAULT_ROLLBACK_FAILED,
                          __ATOMIC_RELAXED);
        (void)tx_step(txn, VBE_MATRIX_TXN_EVENT_ROLLBACK_PUBLISH_FAIL);
        update_degraded_mask(old_policy->generation);
        return VBE_MATRIX_RESULT_DEGRADED_PARTIAL_APPLY;
    }
    (void)tx_step(txn, VBE_MATRIX_TXN_EVENT_ROLLBACK_PUBLISH_OK);

    if (authority_preflight(auth) != VBE_MATRIX_RESULT_APPLIED) {
        __atomic_or_fetch(&g_transaction_fault_flags,
                          VBE_MATRIX_TXFAULT_ROLLBACK_FAILED |
                          VBE_MATRIX_TXFAULT_AUTHORITY_STALE,
                          __ATOMIC_RELAXED);
        (void)tx_step(txn, VBE_MATRIX_TXN_EVENT_ROLLBACK_REPLAY_FAIL);
        update_degraded_mask(rollback_generation);
        return VBE_MATRIX_RESULT_DEGRADED_PARTIAL_APPLY;
    }

    rr = replay_plane(0u, &auth[0], rollback_generation);
    if (rr == REPLAY_RACE && authority_preflight(auth) == VBE_MATRIX_RESULT_APPLIED)
        rr = replay_plane(0u, &auth[0], rollback_generation);
    if (rr != REPLAY_OK) {
        __atomic_or_fetch(&g_transaction_fault_flags,
                          VBE_MATRIX_TXFAULT_ROLLBACK_FAILED,
                          __ATOMIC_RELAXED);
        (void)tx_step(txn, VBE_MATRIX_TXN_EVENT_ROLLBACK_REPLAY_FAIL);
        update_degraded_mask(rollback_generation);
        return VBE_MATRIX_RESULT_DEGRADED_PARTIAL_APPLY;
    }
    (void)tx_step(txn, VBE_MATRIX_TXN_EVENT_ROLLBACK_REPLAY_OK);

    if (authority_preflight(auth) != VBE_MATRIX_RESULT_APPLIED) {
        __atomic_or_fetch(&g_transaction_fault_flags,
                          VBE_MATRIX_TXFAULT_ROLLBACK_FAILED |
                          VBE_MATRIX_TXFAULT_AUTHORITY_STALE,
                          __ATOMIC_RELAXED);
        (void)tx_step(txn, VBE_MATRIX_TXN_EVENT_ROLLBACK_REPLAY_FAIL);
        update_degraded_mask(rollback_generation);
        return VBE_MATRIX_RESULT_DEGRADED_PARTIAL_APPLY;
    }
    rr = replay_plane(1u, &auth[1], rollback_generation);
    if (rr == REPLAY_RACE && authority_preflight(auth) == VBE_MATRIX_RESULT_APPLIED)
        rr = replay_plane(1u, &auth[1], rollback_generation);
    if (rr != REPLAY_OK) {
        __atomic_or_fetch(&g_transaction_fault_flags,
                          VBE_MATRIX_TXFAULT_ROLLBACK_FAILED,
                          __ATOMIC_RELAXED);
        (void)tx_step(txn, VBE_MATRIX_TXN_EVENT_ROLLBACK_REPLAY_FAIL);
        update_degraded_mask(rollback_generation);
        return VBE_MATRIX_RESULT_DEGRADED_PARTIAL_APPLY;
    }
    (void)tx_step(txn, VBE_MATRIX_TXN_EVENT_ROLLBACK_REPLAY_OK);

    if (!pair_applied(rollback_generation)) {
        __atomic_or_fetch(&g_transaction_fault_flags,
                          VBE_MATRIX_TXFAULT_ROLLBACK_FAILED |
                          VBE_MATRIX_TXFAULT_APPLY_VERIFY,
                          __ATOMIC_RELAXED);
        (void)tx_step(txn, VBE_MATRIX_TXN_EVENT_ROLLBACK_VERIFY_FAIL);
        update_degraded_mask(rollback_generation);
        return VBE_MATRIX_RESULT_DEGRADED_PARTIAL_APPLY;
    }
    (void)tx_step(txn, VBE_MATRIX_TXN_EVENT_ROLLBACK_VERIFY_OK);
    __atomic_store_n(&g_degraded_plane_mask, 0u, __ATOMIC_RELEASE);
    return VBE_MATRIX_RESULT_ROLLED_BACK;
}

static int run_transaction(const VbeMatrixS39 *matrix, uint32_t enabled) {
    VbeTxnPreflight pre;
    VbeMatrixApplyTxn txn;
    uint32_t generation = 0u, expected = 0u;
    int preflight_result, ret, apply_result;

    if (!__atomic_load_n(&g_target_supported, __ATOMIC_ACQUIRE) ||
        !__atomic_load_n(&g_hook_owned, __ATOMIC_ACQUIRE))
        return VBE_MATRIX_RESULT_UNSUPPORTED_TARGET;
    if (!__atomic_compare_exchange_n(&g_transaction_owner, &expected, 1u, 0,
                                     __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
        return VBE_MATRIX_RESULT_BUSY;

    __atomic_store_n(&g_transaction_fault_flags, 0u, __ATOMIC_RELEASE);
    vbe_matrix_txn_init(&txn, IMMEDIATE_MAX_REBASE);
    (void)vbe_matrix_txn_begin(&txn);
    sync_tx_state(&txn);

    if (vbe_matrix_policy_read(&g_policy, &pre.old_policy) < 0) {
        ret = VBE_MATRIX_RESULT_POLICY;
        goto out;
    }
    pre.old_applied_generation[0] =
        __atomic_load_n(&g_applied_generation[0], __ATOMIC_ACQUIRE);
    pre.old_applied_generation[1] =
        __atomic_load_n(&g_applied_generation[1], __ATOMIC_ACQUIRE);
    preflight_result = authority_preflight(pre.authority);
    if (preflight_result == VBE_MATRIX_RESULT_APPLIED)
        (void)tx_step(&txn, VBE_MATRIX_TXN_EVENT_PREFLIGHT_READY);
    else
        (void)tx_step(&txn, VBE_MATRIX_TXN_EVENT_PREFLIGHT_DEFER);

    ret = publish_policy(matrix, enabled, &generation);
    if (ret < 0) {
        (void)tx_step(&txn, VBE_MATRIX_TXN_EVENT_PUBLISH_FAIL);
        ret = VBE_MATRIX_RESULT_POLICY;
        goto out;
    }
    (void)tx_step(&txn, VBE_MATRIX_TXN_EVENT_PUBLISH_OK);

    if (preflight_result != VBE_MATRIX_RESULT_APPLIED) {
        ret = preflight_result;
        goto out;
    }

    apply_result = immediate_apply_generation(&txn, generation,
                                              pre.authority, 1);
    if (apply_result == VBE_MATRIX_RESULT_APPLIED) {
        __atomic_store_n(&g_degraded_plane_mask, 0u, __ATOMIC_RELEASE);
        ret = VBE_MATRIX_RESULT_APPLIED;
        goto out;
    }
    if (apply_result == VBE_MATRIX_RESULT_ACCEPTED_PENDING_REPLAY) {
        ret = apply_result;
        goto out;
    }

    /* Every rollback enters the pure state machine explicitly. */
    if (txn.state == VBE_MATRIX_TXN_VERIFY)
        (void)tx_step(&txn, VBE_MATRIX_TXN_EVENT_VERIFY_FAIL);
    else if (txn.state == VBE_MATRIX_TXN_REPLAY_P0 ||
             txn.state == VBE_MATRIX_TXN_REPLAY_P1)
        (void)tx_step(&txn, VBE_MATRIX_TXN_EVENT_REPLAY_FAIL);
    if (txn.state != VBE_MATRIX_TXN_ROLLBACK_PUBLISH) {
        __atomic_or_fetch(&g_transaction_fault_flags,
                          VBE_MATRIX_TXFAULT_ROLLBACK_FAILED,
                          __ATOMIC_RELAXED);
        __atomic_store_n(&g_degraded_plane_mask, VBE_MATRIX_TARGET_PLANE_MASK,
                         __ATOMIC_RELEASE);
        ret = VBE_MATRIX_RESULT_DEGRADED_PARTIAL_APPLY;
        goto out;
    }
    ret = rollback_previous(&txn, &pre.old_policy);

out:
    __atomic_store_n(&g_last_request_result, ret, __ATOMIC_RELEASE);
    __atomic_store_n(&g_transaction_owner, 0u, __ATOMIC_RELEASE);
    return ret;
}

static int fill_plane_status(uint32_t plane, VbeMatrixPlaneStatus *out) {
    VbeNaturalBSource auth;
    VbeBStageObject fwd;
    uint32_t fwd_valid = 0u, i;
    if (!out || plane > 1u) return VBE_MATRIX_RESULT_INVALID_REQUEST;
    if (authority_store_read(plane, &auth) < 0) return VBE_MATRIX_RESULT_BUSY;
    if (forward_store_read(plane, &fwd, &fwd_valid) < 0)
        return VBE_MATRIX_RESULT_BUSY;
    out->valid = auth.valid;
    out->pristine_generation = auth.natural_generation;
    out->baseline_class = (uint32_t)auth.baseline_class;
    out->last_forwarded_policy_generation =
        __atomic_load_n(&g_applied_generation[plane], __ATOMIC_ACQUIRE);
    out->last_sony_return = __atomic_load_n(&g_last_sony_return[plane], __ATOMIC_ACQUIRE);
    out->baseline_mismatch_count =
        __atomic_load_n(&g_baseline_mismatch_count[plane], __ATOMIC_RELAXED);
    out->overflow_count = __atomic_load_n(&g_overflow_count[plane], __ATOMIC_RELAXED);
    out->policy_read_fail_count =
        __atomic_load_n(&g_policy_read_fail_count[plane], __ATOMIC_RELAXED);
    out->source_fnv1a = auth.valid ? vbe_b_object_fnv1a(&auth.source) : 0u;
    out->forward_fnv1a = fwd_valid ? vbe_b_object_fnv1a(&fwd) : 0u;
    for (i = 0; i < VBE_B_OBJECT_WORDS; ++i) {
        out->source_words[i] = auth.valid ? auth.source.words[i] : 0u;
        out->forward_words[i] = fwd_valid ? fwd.words[i] : 0u;
    }
    return 0;
}

int matrix_backend_init(int is_lcd, uint32_t firmware) {
    SceUID uid;
    uintptr_t entry = 0u;
    uint32_t i;
    int ret;

    vbe_matrix_policy_init(&g_policy);
    for (i = 0u; i < 2u; ++i) {
        authority_store_init(i);
        forward_store_init(i);
        g_applied_generation[i] = 0u;
        g_last_sony_return[i] = 0;
        g_baseline_mismatch_count[i] = 0u;
        g_overflow_count[i] = 0u;
        g_policy_read_fail_count[i] = 0u;
    }
    zero_bytes(&g_replay_ctx, sizeof(g_replay_ctx));
    g_status_update_drop_count = 0u;
    g_request_validation_fail_count = 0u;
    g_requested_generation = 1u;
    g_last_request_result = VBE_MATRIX_RESULT_APPLIED;
    g_target_supported = 0u;
    g_hook_owned = 0u;
    g_hook_fail = 0u;
    g_immediate_supported = 0u;
    g_authority_stale_mask = 0u;
    g_degraded_plane_mask = 0u;
    g_transaction_state = VBE_MATRIX_TXN_IDLE;
    g_transaction_fault_flags = 0u;
    g_transaction_owner = 0u;
    g_replay_epoch = 0u;
#ifdef VBE_ENABLE_RESEARCH_FAULT_INJECTION
    g_test_abort_before_p1_once = 0u;
#endif
    g_replay_entry = 0u;
    g_b_ref = 0;

    if (!is_lcd || firmware != VBE_MATRIX_FW_365)
        return VBE_MATRIX_RESULT_UNSUPPORTED_TARGET;
    __atomic_store_n(&g_target_supported, 1u, __ATOMIC_RELEASE);

    /* Install first; resolving the export afterward yields the patched chain head. */
    uid = taiHookFunctionExportForKernel(KERNEL_PID, &g_b_ref, "SceLowio",
                                         TAI_ANY_LIBRARY, NID_STAGE_B, hook_b);
    if (uid < 0) {
        __atomic_store_n(&g_hook_fail, 1u, __ATOMIC_RELEASE);
        return VBE_MATRIX_RESULT_HOOK_INSTALL;
    }
    __atomic_store_n(&g_hook_owned, 1u, __ATOMIC_RELEASE);

    ret = module_get_export_func(KERNEL_PID, "SceLowio", TAI_ANY_LIBRARY,
                                 NID_STAGE_B, &entry);
    if (ret >= 0 && entry != 0u) {
        g_replay_entry = entry;
        __atomic_store_n(&g_immediate_supported, 1u, __ATOMIC_RELEASE);
    }
    /* Natural replay remains fully usable even if immediate resolution fails. */
    return VBE_MATRIX_RESULT_APPLIED;
}

int matrix_backend_can_unload(void) {
    return __atomic_load_n(&g_hook_owned, __ATOMIC_ACQUIRE) ? 0 : 1;
}

int vitabrightMatrixGetCapabilities(VbeMatrixCapabilities *out) {
    VbeMatrixCapabilities c;
    int state, ret;
    if (!out) return VBE_MATRIX_RESULT_INVALID_REQUEST;
    zero_bytes(&c, sizeof(c));
    c.size = sizeof(c);
    c.abi_version = VBE_MATRIX_API_VERSION;
    c.target_pch2000_fw365_verified = 1u;
    c.matrix_backend_supported =
        __atomic_load_n(&g_target_supported, __ATOMIC_ACQUIRE) &&
        __atomic_load_n(&g_hook_owned, __ATOMIC_ACQUIRE);
    c.immediate_reapply_supported = c.matrix_backend_supported &&
        __atomic_load_n(&g_immediate_supported, __ATOMIC_ACQUIRE);
    c.reapply_mode = c.immediate_reapply_supported ?
        VBE_MATRIX_REAPPLY_TAIHEN_CHAIN_REENTRY :
        VBE_MATRIX_REAPPLY_NATURAL_SONY_REPLAY_ONLY;
    c.gamma_transfer_supported = 0u;
    c.additive_affine_supported = 0u;
    c.cct_supported = 0u;
    c.saturation_supported = 0u;
    c.channel_order_proven = 0u;
    ENTER_SYSCALL(state);
    ret = ksceKernelMemcpyKernelToUser(out, &c, sizeof(c));
    EXIT_SYSCALL(state);
    return ret;
}

int vitabrightMatrixSetRequest(const VbeMatrixRequestV1 *in) {
    VbeMatrixRequestV1 request;
    VbeMatrixS39 matrix;
    int state, ret;
    ENTER_SYSCALL(state);
    ret = ksceKernelMemcpyUserToKernel(&request, in, sizeof(request));
    if (ret < 0 || !request_valid(&request, &matrix)) {
        __atomic_add_fetch(&g_request_validation_fail_count, 1u, __ATOMIC_RELAXED);
        __atomic_store_n(&g_last_request_result, VBE_MATRIX_RESULT_INVALID_REQUEST,
                         __ATOMIC_RELEASE);
        EXIT_SYSCALL(state);
        return ret < 0 ? ret : VBE_MATRIX_RESULT_INVALID_REQUEST;
    }
    ret = run_transaction(&matrix, matrix_is_identity(&matrix) ? 0u : 1u);
    EXIT_SYSCALL(state);
    return ret;
}

int vitabrightMatrixReset(void) {
    VbeMatrixS39 identity;
    int state, ret;
    ENTER_SYSCALL(state);
    vbe_matrix_identity(&identity);
    ret = run_transaction(&identity, 0u);
    EXIT_SYSCALL(state);
    return ret;
}

int vitabrightMatrixGetStatus(VbeMatrixBackendStatus *out) {
    VbeMatrixBackendStatus s;
    VbeMatrixPolicySnapshot policy;
    uint32_t plane, pending = 0u;
    int state, ret;
    if (!out) return VBE_MATRIX_RESULT_INVALID_REQUEST;
    zero_bytes(&s, sizeof(s));
    s.size = sizeof(s);
    s.abi_version = VBE_MATRIX_API_VERSION;
    s.target_supported = __atomic_load_n(&g_target_supported, __ATOMIC_ACQUIRE);
    s.hook_owned = __atomic_load_n(&g_hook_owned, __ATOMIC_ACQUIRE);
    s.hook_fail = __atomic_load_n(&g_hook_fail, __ATOMIC_ACQUIRE);
    s.requested_generation = __atomic_load_n(&g_requested_generation, __ATOMIC_ACQUIRE);
    if (vbe_matrix_policy_read(&g_policy, &policy) < 0)
        return VBE_MATRIX_RESULT_BUSY;
    s.active_published_generation = policy.generation;
    s.policy_enabled = policy.enabled;
    for (plane = 0u; plane < 2u; ++plane) {
        if (__atomic_load_n(&g_applied_generation[plane], __ATOMIC_ACQUIRE) !=
            policy.generation)
            pending |= 1u << plane;
        ret = fill_plane_status(plane, &s.planes[plane]);
        if (ret < 0) return ret;
    }
    s.pending_plane_mask = pending;
    s.reapply_mode = __atomic_load_n(&g_immediate_supported, __ATOMIC_ACQUIRE) ?
        VBE_MATRIX_REAPPLY_TAIHEN_CHAIN_REENTRY :
        VBE_MATRIX_REAPPLY_NATURAL_SONY_REPLAY_ONLY;
    s.last_request_result = __atomic_load_n(&g_last_request_result, __ATOMIC_ACQUIRE);
    s.request_validation_fail_count =
        __atomic_load_n(&g_request_validation_fail_count, __ATOMIC_RELAXED);
    s.status_update_drop_count =
        __atomic_load_n(&g_status_update_drop_count, __ATOMIC_RELAXED);
    s.transaction_state = __atomic_load_n(&g_transaction_state, __ATOMIC_ACQUIRE);
    s.transaction_fault_flags =
        __atomic_load_n(&g_transaction_fault_flags, __ATOMIC_ACQUIRE);
    s.authority_degraded_masks =
        (__atomic_load_n(&g_authority_stale_mask, __ATOMIC_ACQUIRE) & 0xFFFFu) |
        ((__atomic_load_n(&g_degraded_plane_mask, __ATOMIC_ACQUIRE) & 0xFFFFu) << 16);
    ENTER_SYSCALL(state);
    ret = ksceKernelMemcpyKernelToUser(out, &s, sizeof(s));
    EXIT_SYSCALL(state);
    return ret;
}

#ifdef VBE_ENABLE_RESEARCH_FAULT_INJECTION
int vitabrightMatrixTestInjectP1AbortOnce(void) {
    if (__atomic_load_n(&g_transaction_owner, __ATOMIC_ACQUIRE))
        return VBE_MATRIX_RESULT_BUSY;
    __atomic_store_n(&g_test_abort_before_p1_once, 1u, __ATOMIC_RELEASE);
    return VBE_MATRIX_RESULT_APPLIED;
}
#endif
