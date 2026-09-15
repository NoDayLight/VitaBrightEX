#include "matrix_backend.h"
#include "matrix_policy_core.h"
#include "state_lock.h"
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/lowio/iftu.h>
#include <taihen.h>

#define NID_STAGE_B 0xD64F4C6Bu
#define PLANE_STATUS_READ_RETRIES 8u
#define MATRIX_ASSERT(n, e) typedef char n[(e) ? 1 : -1]
MATRIX_ASSERT(matrix_b_object_3c, sizeof(VbeBStageObject) == 0x3Cu);
MATRIX_ASSERT(matrix_sony_csc_3c, sizeof(SceIftuCscParams) == 0x3Cu);
MATRIX_ASSERT(matrix_owned_alignment, __alignof__(VbeBStageObject) >= 4u);
#undef MATRIX_ASSERT

typedef struct {
    volatile uint32_t writer;
    volatile uint32_t seq;
    uint32_t valid;
    uint32_t pristine_generation;
    uint32_t baseline_class;
    uint32_t last_forwarded_policy_generation;
    int32_t last_sony_return;
    uint32_t source_fnv1a;
    uint32_t forward_fnv1a;
    uint32_t source_words[VBE_B_OBJECT_WORDS];
    uint32_t forward_words[VBE_B_OBJECT_WORDS];
} VbeMatrixPlaneStore;

static VbeMatrixPolicyStore g_policy;
static VbeMatrixPlaneStore g_planes[2];
static volatile uint32_t g_pristine_generation[2];
static volatile uint32_t g_applied_generation[2];
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
static tai_hook_ref_t g_b_ref;

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

static void plane_store_publish(uint32_t plane, uint32_t pristine_generation,
                                VbeBBaselineClass cls,
                                uint32_t applied_generation,
                                int sony_return,
                                const VbeBStageObject *source,
                                const VbeBStageObject *forwarded) {
    VbeMatrixPlaneStore *s;
    uint32_t seq, i, expected = 0u;
    if (plane > 1u || !source || !forwarded) return;
    s = &g_planes[plane];
    if (!__atomic_compare_exchange_n(&s->writer, &expected, 1u, 0,
                                     __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        __atomic_add_fetch(&g_status_update_drop_count, 1u, __ATOMIC_RELAXED);
        return;
    }
    seq = __atomic_load_n(&s->seq, __ATOMIC_RELAXED);
    if ((seq & 1u) != 0u) ++seq;
    __atomic_store_n(&s->seq, seq + 1u, __ATOMIC_RELEASE);
    __atomic_store_n(&s->valid, 1u, __ATOMIC_RELAXED);
    __atomic_store_n(&s->pristine_generation, pristine_generation, __ATOMIC_RELAXED);
    __atomic_store_n(&s->baseline_class, (uint32_t)cls, __ATOMIC_RELAXED);
    __atomic_store_n(&s->last_forwarded_policy_generation, applied_generation,
                     __ATOMIC_RELAXED);
    __atomic_store_n(&s->last_sony_return, sony_return, __ATOMIC_RELAXED);
    __atomic_store_n(&s->source_fnv1a, vbe_b_object_fnv1a(source), __ATOMIC_RELAXED);
    __atomic_store_n(&s->forward_fnv1a, vbe_b_object_fnv1a(forwarded), __ATOMIC_RELAXED);
    for (i = 0; i < VBE_B_OBJECT_WORDS; ++i) {
        __atomic_store_n(&s->source_words[i], source->words[i], __ATOMIC_RELAXED);
        __atomic_store_n(&s->forward_words[i], forwarded->words[i], __ATOMIC_RELAXED);
    }
    __atomic_thread_fence(__ATOMIC_RELEASE);
    __atomic_store_n(&s->seq, seq + 2u, __ATOMIC_RELEASE);
    __atomic_store_n(&s->writer, 0u, __ATOMIC_RELEASE);
}

static int plane_store_read(uint32_t plane, VbeMatrixPlaneStatus *out) {
    uint32_t attempt;
    VbeMatrixPlaneStore *s;
    if (plane > 1u || !out) return VBE_MATRIX_RESULT_INVALID_REQUEST;
    s = &g_planes[plane];
    for (attempt = 0; attempt < PLANE_STATUS_READ_RETRIES; ++attempt) {
        uint32_t seq1, seq2, i;
        VbeMatrixPlaneStatus tmp;
        seq1 = __atomic_load_n(&s->seq, __ATOMIC_ACQUIRE);
        if ((seq1 & 1u) != 0u) continue;
        tmp.valid = __atomic_load_n(&s->valid, __ATOMIC_RELAXED);
        tmp.pristine_generation = __atomic_load_n(&s->pristine_generation, __ATOMIC_RELAXED);
        tmp.baseline_class = __atomic_load_n(&s->baseline_class, __ATOMIC_RELAXED);
        tmp.last_forwarded_policy_generation =
            __atomic_load_n(&s->last_forwarded_policy_generation, __ATOMIC_RELAXED);
        tmp.last_sony_return = __atomic_load_n(&s->last_sony_return, __ATOMIC_RELAXED);
        tmp.source_fnv1a = __atomic_load_n(&s->source_fnv1a, __ATOMIC_RELAXED);
        tmp.forward_fnv1a = __atomic_load_n(&s->forward_fnv1a, __ATOMIC_RELAXED);
        for (i = 0; i < VBE_B_OBJECT_WORDS; ++i) {
            tmp.source_words[i] = __atomic_load_n(&s->source_words[i], __ATOMIC_RELAXED);
            tmp.forward_words[i] = __atomic_load_n(&s->forward_words[i], __ATOMIC_RELAXED);
        }
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        seq2 = __atomic_load_n(&s->seq, __ATOMIC_ACQUIRE);
        if (seq1 == seq2 && (seq2 & 1u) == 0u) {
            tmp.baseline_mismatch_count =
                __atomic_load_n(&g_baseline_mismatch_count[plane], __ATOMIC_RELAXED);
            tmp.overflow_count =
                __atomic_load_n(&g_overflow_count[plane], __ATOMIC_RELAXED);
            tmp.policy_read_fail_count =
                __atomic_load_n(&g_policy_read_fail_count[plane], __ATOMIC_RELAXED);
            *out = tmp;
            return 0;
        }
    }
    return VBE_MATRIX_RESULT_BUSY;
}

static int hook_b(int plane, const SceIftuCscParams *params) {
    VbeBStageObject source_snapshot;
    VbeBStageObject owned_result;
    VbeBStageObject forwarded_snapshot;
    VbeMatrixPolicySnapshot policy;
    VbeBBaselineClass cls;
    const SceIftuCscParams *forward = params;
    uint32_t pristine_generation, applied_generation;
    int prep = VBE_MATRIX_CORE_INVALID;
    int ret;

    /* Unsupported planes and NULL are true passthroughs: no source dereference. */
    if ((plane != 0 && plane != 1) || params == 0)
        return TAI_CONTINUE(int, g_b_ref, plane, params);

    /* The Sony source is read exactly once into this pristine local snapshot. */
    vbe_b_object_copy_from_volatile(&source_snapshot, params);
    forwarded_snapshot = source_snapshot;
    cls = vbe_b_baseline_classify(&source_snapshot);
    pristine_generation = __atomic_add_fetch(&g_pristine_generation[plane], 1u,
                                               __ATOMIC_RELAXED);
    applied_generation = __atomic_load_n(&g_applied_generation[plane],
                                         __ATOMIC_RELAXED);

    if (vbe_matrix_policy_read(&g_policy, &policy) < 0) {
        __atomic_add_fetch(&g_policy_read_fail_count[plane], 1u, __ATOMIC_RELAXED);
    } else if (!policy.enabled) {
        /* Neutral policy means exact Sony object, even if a future baseline is unknown. */
        prep = VBE_MATRIX_CORE_OK;
    } else if (cls == VBE_B_BASELINE_UNKNOWN) {
        __atomic_add_fetch(&g_baseline_mismatch_count[plane], 1u, __ATOMIC_RELAXED);
        prep = VBE_MATRIX_CORE_UNKNOWN_BASELINE;
    } else {
        prep = vbe_b_object_compose(&source_snapshot, &policy.matrix,
                                    &owned_result, &cls);
        if (prep == VBE_MATRIX_CORE_OK) {
            forwarded_snapshot = owned_result;
            forward = (const SceIftuCscParams *)(const void *)&owned_result;
        } else if (prep == VBE_MATRIX_CORE_OVERFLOW) {
            __atomic_add_fetch(&g_overflow_count[plane], 1u, __ATOMIC_RELAXED);
        }
    }

    ret = TAI_CONTINUE(int, g_b_ref, plane, forward);
    if (ret >= 0 && prep == VBE_MATRIX_CORE_OK) {
        applied_generation = policy.generation;
        __atomic_store_n(&g_applied_generation[plane], applied_generation,
                         __ATOMIC_RELEASE);
    }
    plane_store_publish((uint32_t)plane, pristine_generation, cls,
                        applied_generation, ret,
                        &source_snapshot, &forwarded_snapshot);
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

static int publish_request_locked(const VbeMatrixS39 *matrix, uint32_t enabled) {
    uint32_t generation = 0u;
    int ret;
    if (!__atomic_load_n(&g_target_supported, __ATOMIC_ACQUIRE) ||
        !__atomic_load_n(&g_hook_owned, __ATOMIC_ACQUIRE))
        return VBE_MATRIX_RESULT_UNSUPPORTED_TARGET;
    ret = vbe_matrix_policy_publish(&g_policy, matrix, enabled, &generation);
    if (ret < 0) return VBE_MATRIX_RESULT_POLICY;
    __atomic_store_n(&g_requested_generation, generation, __ATOMIC_RELEASE);
    __atomic_store_n(&g_last_request_result,
                     VBE_MATRIX_RESULT_ACCEPTED_PENDING_REPLAY, __ATOMIC_RELEASE);
    return VBE_MATRIX_RESULT_ACCEPTED_PENDING_REPLAY;
}

int matrix_backend_init(int is_lcd, uint32_t firmware) {
    SceUID uid;
    uint32_t i;
    vbe_matrix_policy_init(&g_policy);
    for (i = 0; i < 2u; ++i) {
        g_planes[i].writer = 0u;
        g_planes[i].seq = 0u;
        g_planes[i].valid = 0u;
        g_pristine_generation[i] = 0u;
        g_applied_generation[i] = 0u;
        g_baseline_mismatch_count[i] = 0u;
        g_overflow_count[i] = 0u;
        g_policy_read_fail_count[i] = 0u;
    }
    g_status_update_drop_count = 0u;
    g_request_validation_fail_count = 0u;
    g_requested_generation = 1u;
    g_last_request_result = VBE_MATRIX_RESULT_APPLIED;
    g_target_supported = 0u;
    g_hook_owned = 0u;
    g_hook_fail = 0u;
    g_b_ref = 0;

    if (!is_lcd || firmware != VBE_MATRIX_FW_365)
        return VBE_MATRIX_RESULT_UNSUPPORTED_TARGET;
    __atomic_store_n(&g_target_supported, 1u, __ATOMIC_RELEASE);
    uid = taiHookFunctionExportForKernel(KERNEL_PID, &g_b_ref, "SceLowio",
                                         TAI_ANY_LIBRARY, NID_STAGE_B, hook_b);
    if (uid < 0) {
        __atomic_store_n(&g_hook_fail, 1u, __ATOMIC_RELEASE);
        return VBE_MATRIX_RESULT_HOOK_INSTALL;
    }
    __atomic_store_n(&g_hook_owned, 1u, __ATOMIC_RELEASE);
    return VBE_MATRIX_RESULT_APPLIED;
}

int matrix_backend_can_unload(void) {
    /* The B hook is reboot-owned. Never unload code while taiHEN can enter it. */
    return __atomic_load_n(&g_hook_owned, __ATOMIC_ACQUIRE) ? 0 : 1;
}

int vitabrightMatrixGetCapabilities(VbeMatrixCapabilities *out) {
    VbeMatrixCapabilities c = {0};
    int state, ret;
    if (!out) return VBE_MATRIX_RESULT_INVALID_REQUEST;
    c.size = sizeof(c);
    c.abi_version = VBE_MATRIX_API_VERSION;
    c.target_pch2000_fw365_verified = 1u;
    c.matrix_backend_supported = __atomic_load_n(&g_target_supported, __ATOMIC_ACQUIRE);
    c.immediate_reapply_supported = 0u;
    c.reapply_mode = VBE_MATRIX_REAPPLY_BLOCKED_TAIHEN_CHAIN_ORDER;
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
    ret = state_lock_acquire();
    if (ret < 0) {
        EXIT_SYSCALL(state);
        return ret;
    }
    ret = publish_request_locked(&matrix, matrix_is_identity(&matrix) ? 0u : 1u);
    ret = state_lock_release_result(ret);
    EXIT_SYSCALL(state);
    return ret;
}

int vitabrightMatrixReset(void) {
    VbeMatrixS39 identity;
    int state, ret;
    ENTER_SYSCALL(state);
    ret = state_lock_acquire();
    if (ret < 0) {
        EXIT_SYSCALL(state);
        return ret;
    }
    vbe_matrix_identity(&identity);
    ret = publish_request_locked(&identity, 0u);
    ret = state_lock_release_result(ret);
    EXIT_SYSCALL(state);
    return ret;
}

int vitabrightMatrixGetStatus(VbeMatrixBackendStatus *out) {
    VbeMatrixBackendStatus s = {0};
    VbeMatrixPolicySnapshot policy;
    uint32_t plane;
    int state, ret;
    if (!out) return VBE_MATRIX_RESULT_INVALID_REQUEST;
    s.size = sizeof(s);
    s.abi_version = VBE_MATRIX_API_VERSION;
    s.target_supported = __atomic_load_n(&g_target_supported, __ATOMIC_ACQUIRE);
    s.hook_owned = __atomic_load_n(&g_hook_owned, __ATOMIC_ACQUIRE);
    s.hook_fail = __atomic_load_n(&g_hook_fail, __ATOMIC_ACQUIRE);
    s.requested_generation = __atomic_load_n(&g_requested_generation, __ATOMIC_ACQUIRE);
    s.reapply_mode = VBE_MATRIX_REAPPLY_BLOCKED_TAIHEN_CHAIN_ORDER;
    s.last_request_result = __atomic_load_n(&g_last_request_result, __ATOMIC_ACQUIRE);
    s.request_validation_fail_count =
        __atomic_load_n(&g_request_validation_fail_count, __ATOMIC_RELAXED);
    s.status_update_drop_count =
        __atomic_load_n(&g_status_update_drop_count, __ATOMIC_RELAXED);
    if (vbe_matrix_policy_read(&g_policy, &policy) < 0)
        return VBE_MATRIX_RESULT_BUSY;
    s.active_published_generation = policy.generation;
    s.policy_enabled = policy.enabled;
    for (plane = 0; plane < 2u; ++plane) {
        ret = plane_store_read(plane, &s.planes[plane]);
        if (ret < 0) return ret;
        if (__atomic_load_n(&g_applied_generation[plane], __ATOMIC_ACQUIRE) !=
            policy.generation)
            s.pending_plane_mask |= 1u << plane;
    }
    ENTER_SYSCALL(state);
    ret = ksceKernelMemcpyKernelToUser(out, &s, sizeof(s));
    EXIT_SYSCALL(state);
    return ret;
}
