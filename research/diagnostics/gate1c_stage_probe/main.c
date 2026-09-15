#include <stdint.h>
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysmem/data_transfers.h>
#include <psp2kern/kernel/threadmgr/thread.h>
#include <psp2kern/lowio/iftu.h>
#include <taihen.h>
#include "../gate0_observer/observer_lifecycle_core.h"
#include "../gate0_observer/trace_ring_core.h"
#include "gate1c_protocol.h"

#define NID_A 0x0FCBF457u
#define NID_B 0xD64F4C6Bu
#define NID_ENABLE 0x0D7C02F7u

#define MAIN_ASSERT(n, e) typedef char n[(e) ? 1 : -1]
MAIN_ASSERT(g1c_csc_exact_3c, sizeof(SceIftuCscParams) == VBE_G1C_CSC_SIZE);
MAIN_ASSERT(g1c_csc_alignment_at_least_4, __alignof__(SceIftuCscParams) >= 4u);
#undef MAIN_ASSERT

static const uint32_t g_canonical_a[15] = {
    0x00000000u, 0x00000202u, 0x000003FFu, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u
};

typedef struct VbeG1cShadow {
    volatile uint32_t valid, generation, source_hash32, forward_hash32;
    SceIftuCscParams source;
    SceIftuCscParams forward;
} VbeG1cShadow;

static VbeG1cRecord g_records[VBE_G1C_CAPACITY];
static VbeTraceRingCore g_ring;
static VbeObserverLifecycleCore g_lifecycle;
static VbeG1cShadow g_shadow[2][2];
static volatile uint32_t g_sequence, g_completion, g_invocation, g_ring_epoch;
static volatile uint32_t g_hook_fail, g_transformed_b, g_null, g_baseline_mismatch;
static uint32_t g_fw;
static tai_hook_ref_t g_ref_a, g_ref_b, g_ref_enable;

static void zero_bytes(void *dst, uint32_t n) {
    volatile uint8_t *p = (volatile uint8_t *)dst;
    uint32_t i;
    for (i = 0; i < n; ++i) p[i] = 0u;
}

static uint32_t tid(void) {
    int x = ksceKernelGetThreadId();
    return x < 0 ? 0u : (uint32_t)x;
}

static uint32_t invocation(void) {
    return __sync_add_and_fetch(&g_invocation, 1u);
}

static VbeG1cRecord *reserve(int capture, uint16_t event, int plane, uint32_t t, uint32_t inv) {
    uint32_t ticket, slot;
    VbeG1cRecord *r;
    if (!capture) return 0;
    if (!vbe_trace_ring_reserve(&g_ring, VBE_G1C_CAPACITY, &ticket, &slot)) return 0;
    r = &g_records[slot];
    zero_bytes(r, sizeof(*r));
    r->sequence = __sync_add_and_fetch(&g_sequence, 1u);
    r->thread_id = t;
    r->invocation_id = inv;
    r->event_type = event;
    r->plane = (int16_t)plane;
    r->ring_epoch = g_ring_epoch;
    (void)ticket;
    return r;
}

static void commit(VbeG1cRecord *r) {
    if (!r) return;
    r->completion_sequence = __sync_add_and_fetch(&g_completion, 1u);
    __sync_synchronize();
    r->committed = VBE_G1C_COMMITTED;
}

static void finish(VbeG1cRecord *r, int raw) {
    if (!r) return;
    r->raw_return = raw;
    r->flags |= VBE_G1C_FLAG_RETURN_VALID;
    commit(r);
}

static int canonical_a(const SceIftuCscParams *p) {
    return vbe_g1c_equal_3c(p, g_canonical_a);
}

static void record_csc(
    VbeG1cRecord *r,
    const SceIftuCscParams *original_source,
    const SceIftuCscParams *source_snapshot,
    const SceIftuCscParams *forward_snapshot,
    uint32_t generation,
    int target_plane,
    uint32_t stage,
    VbeG1cPrepareResult prep
) {
    if (!r) return;
    r->source_pointer = (uint32_t)(uintptr_t)original_source;
    if (target_plane) r->flags |= VBE_G1C_FLAG_TARGET_PLANE;
    else r->flags |= VBE_G1C_FLAG_OUTSIDE_PLANE;

    if (prep == VBE_G1C_PREP_NULL) {
        r->flags |= VBE_G1C_FLAG_NULL;
        return;
    }
    if (prep == VBE_G1C_PREP_OUTSIDE_PLANE) return;

    r->flags |= VBE_G1C_FLAG_SOURCE_CAPTURED;
    vbe_g1c_copy_3c(r->source_payload, source_snapshot);
    vbe_g1c_copy_3c(r->forward_payload, forward_snapshot);
    r->source_hash32 = vbe_g1c_hash32_3c(r->source_payload);
    r->forward_hash32 = vbe_g1c_hash32_3c(r->forward_payload);
    r->generation = generation;

    if (stage == VBE_G1C_STAGE_A && canonical_a(source_snapshot))
        r->flags |= VBE_G1C_FLAG_CANONICAL_SOURCE;
    if (stage == VBE_G1C_STAGE_B && vbe_g1c_equal_3c(source_snapshot, vbe_g1c_canonical_b_words))
        r->flags |= VBE_G1C_FLAG_CANONICAL_SOURCE;

    if (prep == VBE_G1C_PREP_B_TRANSFORM) {
        r->flags |= VBE_G1C_FLAG_FORWARD_OWNED | VBE_G1C_FLAG_TRANSFORMED_B;
    } else if (prep == VBE_G1C_PREP_B_BASELINE_MISMATCH) {
        r->flags |= VBE_G1C_FLAG_BASELINE_MISMATCH;
    }
}

static int hook_csc(
    uint32_t stage,
    int plane,
    const SceIftuCscParams *params,
    tai_hook_ref_t *ref,
    uint16_t event
) {
    int capture = vbe_observer_core_producer_enter(&g_lifecycle);
    uint32_t t = tid(), inv = invocation(), gen = 0u;
    int target_plane = (plane == 0 || plane == 1), ret;
    VbeG1cRecord *r = reserve(capture, event, plane, t, inv);
    SceIftuCscParams source_snapshot;
    SceIftuCscParams forward_snapshot;
    VbeG1cPrepareResult prep;
    const SceIftuCscParams *forward;

    prep = vbe_g1c_prepare(stage, target_plane, params, &source_snapshot, &forward_snapshot);

    if (prep == VBE_G1C_PREP_NULL) {
        __sync_add_and_fetch(&g_null, 1u);
    } else if (prep == VBE_G1C_PREP_A_OBSERVE || prep == VBE_G1C_PREP_B_TRANSFORM || prep == VBE_G1C_PREP_B_BASELINE_MISMATCH) {
        /* Only the two fixed wrappers are valid shadow owners. Keep even an
         * impossible/corrupted stage selector fail-open and out-of-bounds safe. */
        if (stage <= VBE_G1C_STAGE_B) {
            gen = __sync_add_and_fetch(&g_shadow[stage][plane].generation, 1u);
            vbe_g1c_copy_3c(&g_shadow[stage][plane].source, &source_snapshot);
            vbe_g1c_copy_3c(&g_shadow[stage][plane].forward, &forward_snapshot);
            g_shadow[stage][plane].source_hash32 = vbe_g1c_hash32_3c(&source_snapshot);
            g_shadow[stage][plane].forward_hash32 = vbe_g1c_hash32_3c(&forward_snapshot);
            __sync_synchronize();
            g_shadow[stage][plane].valid = 1u;
        }
        if (prep == VBE_G1C_PREP_B_TRANSFORM)
            __sync_add_and_fetch(&g_transformed_b, 1u);
        if (prep == VBE_G1C_PREP_B_BASELINE_MISMATCH)
            __sync_add_and_fetch(&g_baseline_mismatch, 1u);
    }

    record_csc(r, params, &source_snapshot, &forward_snapshot, gen, target_plane, stage, prep);
    forward = (const SceIftuCscParams *)vbe_g1c_forward_pointer(prep, params, &forward_snapshot);
    ret = TAI_CONTINUE(int, *ref, plane, forward);
    finish(r, ret);
    vbe_observer_core_producer_leave(&g_lifecycle);
    return ret;
}

static int hook_a(int plane, const SceIftuCscParams *p) {
    return hook_csc(VBE_G1C_STAGE_A, plane, p, &g_ref_a, VBE_G1C_CSC_A);
}

static int hook_b(int plane, const SceIftuCscParams *p) {
    return hook_csc(VBE_G1C_STAGE_B, plane, p, &g_ref_b, VBE_G1C_CSC_B);
}

static int hook_enable(int plane) {
    int capture = vbe_observer_core_producer_enter(&g_lifecycle);
    uint32_t t = tid(), inv = invocation();
    int ret;
    VbeG1cRecord *r = reserve(capture, VBE_G1C_ENABLE_ENTER, plane, t, inv);
    commit(r);
    ret = TAI_CONTINUE(int, g_ref_enable, plane);
    r = reserve(capture, VBE_G1C_ENABLE_EXIT, plane, t, inv);
    finish(r, ret);
    vbe_observer_core_producer_leave(&g_lifecycle);
    return ret;
}

static int install(tai_hook_ref_t *ref, uint32_t nid, const void *hook, uint32_t bit, uint32_t fail) {
    SceUID uid = taiHookFunctionExportForKernel(KERNEL_PID, ref, "SceLowio", TAI_ANY_LIBRARY, nid, hook);
    if (uid < 0) {
        g_hook_fail |= fail;
        vbe_observer_core_install_failed(&g_lifecycle);
        return -1;
    }
    vbe_observer_core_note_hook(&g_lifecycle, bit);
    return 0;
}

int vbeG1cGetStatus(VbeG1cStatus *out) {
    VbeG1cStatus s;
    uint32_t cs;
    int ret;
    if (!out) return VBE_G1C_ERR_INVALID;
    zero_bytes(&s, sizeof(s));
    s.magic = VBE_G1C_MAGIC;
    s.version = VBE_G1C_VERSION;
    s.firmware_version = g_fw;
    s.lifecycle = g_lifecycle.state;
    s.record_capacity = VBE_G1C_CAPACITY;
    s.slots_reserved = vbe_trace_ring_count(&g_ring, VBE_G1C_CAPACITY);
    s.committed_records = g_completion;
    s.lost_records = g_ring.lost;
    s.last_sequence = g_sequence;
    s.active_producers = g_lifecycle.active_producers;
    s.owned_hook_mask = g_lifecycle.owned_hook_mask;
    s.required_hook_mask = g_lifecycle.required_hook_mask;
    s.missing_required_mask = g_lifecycle.required_hook_mask & ~g_lifecycle.owned_hook_mask;
    s.hook_fail_mask = g_hook_fail;
    s.ring_epoch = g_ring_epoch;
    s.transformed_b = g_transformed_b;
    s.null_calls = g_null;
    s.baseline_mismatch_count = g_baseline_mismatch;
    ENTER_SYSCALL(cs);
    ret = ksceKernelMemcpyKernelToUser(out, &s, sizeof(s));
    EXIT_SYSCALL(cs);
    return ret;
}

int vbeG1cPause(void) {
    return vbe_observer_core_pause(&g_lifecycle) ? 0 : VBE_G1C_ERR_STATE;
}

int vbeG1cReset(int enable_after_reset) {
    uint32_t i;
    if (!vbe_observer_core_stable_paused(&g_lifecycle)) return VBE_G1C_ERR_BUSY;
    for (i = 0; i < VBE_G1C_CAPACITY; ++i) zero_bytes(&g_records[i], sizeof(g_records[i]));
    vbe_trace_ring_consume_epoch(&g_ring);
    g_sequence = g_completion = g_invocation = 0u;
    g_ring_epoch++;
    __sync_synchronize();
    if (enable_after_reset && !vbe_observer_core_resume(&g_lifecycle)) return VBE_G1C_ERR_STATE;
    return 0;
}

int vbeG1cRead(VbeG1cRecord *out, uint32_t capacity, uint32_t *written) {
    uint32_t cs, n, i, start;
    int ret = 0;
    if (!out || !written) return VBE_G1C_ERR_INVALID;
    if (!vbe_observer_core_stable_paused(&g_lifecycle)) return VBE_G1C_ERR_BUSY;
    n = vbe_trace_ring_count(&g_ring, VBE_G1C_CAPACITY);
    if (n > capacity) n = capacity;
    start = g_ring.read_ticket;
    ENTER_SYSCALL(cs);
    for (i = 0; i < n && ret >= 0; ++i)
        ret = ksceKernelMemcpyKernelToUser(&out[i], &g_records[(start + i) % VBE_G1C_CAPACITY], sizeof(VbeG1cRecord));
    if (ret >= 0) ret = ksceKernelMemcpyKernelToUser(written, &n, sizeof(n));
    EXIT_SYSCALL(cs);
    return ret;
}

void _start() __attribute__((weak, alias("module_start")));
int module_start(SceSize argc, const void *args) {
    SceKernelFwInfo fw;
    (void)argc;
    (void)args;
    zero_bytes(g_records, sizeof(g_records));
    zero_bytes(g_shadow, sizeof(g_shadow));
    vbe_trace_ring_init(&g_ring);
    vbe_observer_core_init(&g_lifecycle, VBE_G1C_REQUIRED);
    g_sequence = g_completion = g_invocation = 0u;
    g_ring_epoch = 1u;
    g_hook_fail = g_transformed_b = g_null = g_baseline_mismatch = 0u;
    g_fw = 0u;
    zero_bytes(&fw, sizeof(fw));
    fw.size = sizeof(fw);
    if (ksceKernelGetSystemSwVersion(&fw) < 0 || fw.version != VBE_G1C_FW_365) {
        g_hook_fail |= VBE_G1C_FAIL_FW;
        vbe_observer_core_install_failed(&g_lifecycle);
        return SCE_KERNEL_START_SUCCESS;
    }
    g_fw = fw.version;
    if (install(&g_ref_a, NID_A, hook_a, VBE_G1C_HOOK_A, VBE_G1C_FAIL_A) < 0) return SCE_KERNEL_START_SUCCESS;
    if (install(&g_ref_b, NID_B, hook_b, VBE_G1C_HOOK_B, VBE_G1C_FAIL_B) < 0) return SCE_KERNEL_START_SUCCESS;
    if (install(&g_ref_enable, NID_ENABLE, hook_enable, VBE_G1C_HOOK_ENABLE, VBE_G1C_FAIL_ENABLE) < 0) return SCE_KERNEL_START_SUCCESS;
    if (!vbe_observer_core_start_capture(&g_lifecycle)) vbe_observer_core_install_failed(&g_lifecycle);
    return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize argc, const void *args) {
    (void)argc;
    (void)args;
    vbe_observer_core_quiesce(&g_lifecycle);
    return vbe_observer_core_can_unload(&g_lifecycle) ? SCE_KERNEL_STOP_SUCCESS : SCE_KERNEL_STOP_FAIL;
}
