#include <stdint.h>
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysmem/data_transfers.h>
#include <psp2kern/kernel/threadmgr/thread.h>
#include <psp2kern/lowio/iftu.h>
#include <taihen.h>
#include "../../../taihen_extra.h"
#include "gate1e_chain_protocol.h"

#define NID_STAGE_B 0xD64F4C6Bu

typedef int (*StageBEntry)(int plane, const SceIftuCscParams *params);

static const uint32_t g_identity_b[VBE_B_OBJECT_WORDS] = {
    0x00000000u, 0x00000000u, 0x000003FFu, 0x00000000u, 0x000003FFu,
    0x00000000u, 0x00000200u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000200u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000200u
};

static tai_hook_ref_t g_ref1, g_ref2;
static SceUID g_uid1, g_uid2;
static uintptr_t g_export_entry;
static volatile uint32_t g_flags, g_owned, g_fail;
static volatile uint32_t g_control_active, g_control_epoch, g_control_tid, g_control_runs;
static volatile uint32_t g_concurrent, g_overlap, g_event_count;
static volatile uint32_t g_depth[2];
static volatile uint32_t g_replay_attempted;
static volatile int32_t g_ret_p0, g_ret_p1;
static VbeG1eObserverEvent g_events[VBE_G1E_EVENT_CAPACITY];

static void zero_bytes(void *dst, uint32_t n) {
    volatile uint8_t *p = (volatile uint8_t *)dst;
    uint32_t i;
    for (i = 0; i < n; ++i) p[i] = 0u;
}

static uint32_t thread_id(void) {
    int x = ksceKernelGetThreadId();
    return x < 0 ? 0u : (uint32_t)x;
}

static void copy_3c(uint32_t dst[VBE_B_OBJECT_WORDS], const SceIftuCscParams *src) {
    const volatile uint32_t *p = (const volatile uint32_t *)(const void *)src;
    uint32_t i;
    for (i = 0; i < VBE_B_OBJECT_WORDS; ++i) dst[i] = p[i];
}

static void copy_event(VbeG1eObserverEvent *dst, const VbeG1eObserverEvent *src) {
    uint32_t i;
    dst->sequence = src->sequence;
    dst->node = src->node;
    dst->phase = src->phase;
    dst->plane = src->plane;
    dst->thread_id = src->thread_id;
    dst->control_epoch = src->control_epoch;
    dst->source_valid = src->source_valid;
    dst->raw_return = src->raw_return;
    for (i = 0; i < VBE_B_OBJECT_WORDS; ++i) dst->source_words[i] = src->source_words[i];
}

static void record_event(uint32_t node, uint32_t phase, int plane,
                         const SceIftuCscParams *params, int raw_return) {
    uint32_t slot, tid, epoch;
    VbeG1eObserverEvent *e;
    if (!__atomic_load_n(&g_control_active, __ATOMIC_ACQUIRE)) return;
    tid = thread_id();
    epoch = __atomic_load_n(&g_control_epoch, __ATOMIC_ACQUIRE);
    if (tid != __atomic_load_n(&g_control_tid, __ATOMIC_ACQUIRE))
        __atomic_add_fetch(&g_concurrent, 1u, __ATOMIC_RELAXED);
    slot = __atomic_fetch_add(&g_event_count, 1u, __ATOMIC_RELAXED);
    if (slot >= VBE_G1E_EVENT_CAPACITY) {
        __atomic_or_fetch(&g_flags, VBE_G1E_FLAG_EVENT_OVERFLOW, __ATOMIC_RELAXED);
        return;
    }
    e = &g_events[slot];
    zero_bytes(e, sizeof(*e));
    e->sequence = slot + 1u;
    e->node = node;
    e->phase = phase;
    e->plane = plane;
    e->thread_id = tid;
    e->control_epoch = epoch;
    e->raw_return = raw_return;
    if (phase == VBE_G1E_PHASE_ENTER && (plane == 0 || plane == 1) && params) {
        copy_3c(e->source_words, params);
        e->source_valid = 1u;
    }
    __atomic_thread_fence(__ATOMIC_RELEASE);
}

static int observe(uint32_t node, volatile uint32_t *depth,
                   tai_hook_ref_t ref, int plane, const SceIftuCscParams *params) {
    uint32_t d = __atomic_add_fetch(depth, 1u, __ATOMIC_ACQ_REL);
    int ret;
    if (__atomic_load_n(&g_control_active, __ATOMIC_ACQUIRE) && d != 1u)
        __atomic_add_fetch(&g_overlap, 1u, __ATOMIC_RELAXED);
    record_event(node, VBE_G1E_PHASE_ENTER, plane, params, 0);
    ret = TAI_CONTINUE(int, ref, plane, params);
    record_event(node, VBE_G1E_PHASE_EXIT, plane, 0, ret);
    __atomic_sub_fetch(depth, 1u, __ATOMIC_RELEASE);
    return ret;
}

static int observer1(int plane, const SceIftuCscParams *params) {
    return observe(VBE_G1E_NODE_1, &g_depth[0], g_ref1, plane, params);
}

static int observer2(int plane, const SceIftuCscParams *params) {
    return observe(VBE_G1E_NODE_2, &g_depth[1], g_ref2, plane, params);
}

static int install_observer(tai_hook_ref_t *ref, const void *hook, uint32_t bit, uint32_t fail_bit) {
    SceUID uid = taiHookFunctionExportForKernel(KERNEL_PID, ref, "SceLowio",
                                                 TAI_ANY_LIBRARY, NID_STAGE_B, hook);
    if (uid < 0) {
        __atomic_or_fetch(&g_fail, fail_bit, __ATOMIC_RELAXED);
        return uid;
    }
    __atomic_or_fetch(&g_owned, bit, __ATOMIC_RELEASE);
    return uid;
}

int vbeG1eReplayIdentity(void) {
    StageBEntry fn;
    uint32_t expected = 0u;
    int r0, r1 = VBE_G1E_SKIPPED_RETURN;
    if ((__atomic_load_n(&g_owned, __ATOMIC_ACQUIRE) & VBE_G1E_OBSERVER_MASK) != VBE_G1E_OBSERVER_MASK ||
        !(__atomic_load_n(&g_flags, __ATOMIC_ACQUIRE) & VBE_G1E_FLAG_EXPORT_RESOLVED) ||
        g_export_entry == 0u)
        return VBE_G1E_ERR_NOT_READY;
    if (!__atomic_compare_exchange_n(&g_control_runs, &expected, 1u, 0,
                                     __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
        return VBE_G1E_ERR_ALREADY_RUN;

    zero_bytes(g_events, sizeof(g_events));
    g_event_count = 0u;
    g_concurrent = 0u;
    g_overlap = 0u;
    g_replay_attempted = 1u;
    g_ret_p0 = VBE_G1E_SKIPPED_RETURN;
    g_ret_p1 = VBE_G1E_SKIPPED_RETURN;
    g_control_tid = thread_id();
    __atomic_add_fetch(&g_control_epoch, 1u, __ATOMIC_RELAXED);
    __atomic_store_n(&g_control_active, 1u, __ATOMIC_RELEASE);

    fn = (StageBEntry)(uintptr_t)g_export_entry;
    r0 = fn(0, (const SceIftuCscParams *)(const void *)g_identity_b);
    g_ret_p0 = r0;
    if (r0 >= 0) {
        r1 = fn(1, (const SceIftuCscParams *)(const void *)g_identity_b);
        g_ret_p1 = r1;
    }

    __atomic_store_n(&g_control_active, 0u, __ATOMIC_RELEASE);
    __atomic_or_fetch(&g_flags, VBE_G1E_FLAG_REPLAY_COMPLETE, __ATOMIC_RELEASE);
    return r0 < 0 ? r0 : r1;
}

int vbeG1eGetStatus(VbeG1eObserverStatus *out) {
    VbeG1eObserverStatus s;
    uint32_t state, i, n;
    int ret;
    if (!out) return VBE_G1E_ERR_STATE;
    zero_bytes(&s, sizeof(s));
    s.magic = VBE_G1E_MAGIC;
    s.version = VBE_G1E_VERSION;
    s.flags = __atomic_load_n(&g_flags, __ATOMIC_ACQUIRE);
    s.owned_hook_mask = __atomic_load_n(&g_owned, __ATOMIC_ACQUIRE);
    s.fail_mask = __atomic_load_n(&g_fail, __ATOMIC_ACQUIRE);
    s.export_entry = (uint32_t)g_export_entry;
    s.control_epoch = __atomic_load_n(&g_control_epoch, __ATOMIC_ACQUIRE);
    s.control_thread_id = __atomic_load_n(&g_control_tid, __ATOMIC_ACQUIRE);
    s.control_run_count = __atomic_load_n(&g_control_runs, __ATOMIC_ACQUIRE);
    s.concurrent_control_event_count = __atomic_load_n(&g_concurrent, __ATOMIC_ACQUIRE);
    s.recursion_or_overlap_count = __atomic_load_n(&g_overlap, __ATOMIC_ACQUIRE);
    n = __atomic_load_n(&g_event_count, __ATOMIC_ACQUIRE);
    s.event_count = n;
    s.replay_attempted = __atomic_load_n(&g_replay_attempted, __ATOMIC_ACQUIRE);
    s.replay_return_p0 = __atomic_load_n(&g_ret_p0, __ATOMIC_ACQUIRE);
    s.replay_return_p1 = __atomic_load_n(&g_ret_p1, __ATOMIC_ACQUIRE);
    if (n > VBE_G1E_EVENT_CAPACITY) n = VBE_G1E_EVENT_CAPACITY;
    for (i = 0; i < n; ++i) copy_event(&s.events[i], &g_events[i]);
    ENTER_SYSCALL(state);
    ret = ksceKernelMemcpyKernelToUser(out, &s, sizeof(s));
    EXIT_SYSCALL(state);
    return ret;
}

int module_start(SceSize argc, const void *args) {
    uintptr_t entry = 0u;
    int ret;
    (void)argc; (void)args;
    zero_bytes(g_events, sizeof(g_events));
    g_uid1 = g_uid2 = -1;
    g_ref1 = g_ref2 = 0;
    g_export_entry = 0u;
    g_flags = g_owned = g_fail = 0u;
    g_control_active = g_control_epoch = g_control_tid = g_control_runs = 0u;
    g_concurrent = g_overlap = g_event_count = 0u;
    g_depth[0] = g_depth[1] = 0u;
    g_replay_attempted = 0u;
    g_ret_p0 = g_ret_p1 = VBE_G1E_SKIPPED_RETURN;

    g_uid1 = install_observer(&g_ref1, observer1, 1u << 0, VBE_G1E_FAIL_HOOK1);
    g_uid2 = install_observer(&g_ref2, observer2, 1u << 1, VBE_G1E_FAIL_HOOK2);
    if ((__atomic_load_n(&g_owned, __ATOMIC_ACQUIRE) & VBE_G1E_OBSERVER_MASK) == VBE_G1E_OBSERVER_MASK)
        __atomic_or_fetch(&g_flags, VBE_G1E_FLAG_OBSERVERS_READY, __ATOMIC_RELEASE);

    ret = module_get_export_func(KERNEL_PID, "SceLowio", TAI_ANY_LIBRARY,
                                 NID_STAGE_B, &entry);
    if (ret < 0 || entry == 0u) {
        __atomic_or_fetch(&g_fail, VBE_G1E_FAIL_EXPORT, __ATOMIC_RELAXED);
    } else {
        g_export_entry = entry;
        __atomic_or_fetch(&g_flags, VBE_G1E_FLAG_EXPORT_RESOLVED, __ATOMIC_RELEASE);
    }
    return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize argc, const void *args) {
    (void)argc; (void)args;
    /* Observer hooks are reboot-owned just like the Gate-1D production hook. */
    return SCE_KERNEL_STOP_CANCEL;
}
