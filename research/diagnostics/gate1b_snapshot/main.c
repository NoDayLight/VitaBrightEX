#include <stdint.h>
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysmem/data_transfers.h>
#include <taihen.h>

#include "../../../taihen_extra.h"
#include "gate1b_snapshot_protocol.h"

#define CACHE_A_OFFSET 0x10Cu
#define CACHE_B_OFFSET 0x148u
#define ENABLE_STATE_OFFSET 0x1E8u
#define PRIVATE_CONTROL_OFFSET 0x1F8u
#define LIVE_CSC_CONTROL_OFFSET 0x100u

static uintptr_t g_plane_state[VBE_G1B_PLANE_COUNT];
static uint32_t g_status_flags;
static uint32_t g_firmware_version;
static uint32_t g_lowio_modid;
static uint32_t g_lowio_module_nid;
static volatile uint32_t g_sequence;

static void zero_bytes(void *dst, uint32_t n) {
    volatile uint8_t *p = (volatile uint8_t *)dst;
    uint32_t i;
    for (i = 0; i < n; ++i) p[i] = 0u;
}

static uint32_t read32(uintptr_t addr) {
    return *(const volatile uint32_t *)addr;
}

static void copy_3c_from_volatile(uint8_t dst[VBE_G1B_CSC_SIZE], uintptr_t src) {
    const volatile uint8_t *p = (const volatile uint8_t *)src;
    uint32_t i;
    for (i = 0; i < VBE_G1B_CSC_SIZE; ++i) dst[i] = p[i];
}

static int known_iftu_mmio(uint32_t base) {
    return base == 0xE5020000u || base == 0xE5021000u ||
           base == 0xE5030000u || base == 0xE5031000u;
}

static void capture_plane(VbeG1bPlaneSnapshot *p, uint32_t plane, uintptr_t state) {
    uint32_t mmio;
    p->plane = plane;
    p->state_address = (uint32_t)state;
    if (state == 0u) return;

    p->flags |= VBE_G1B_PLANE_STATE_RESOLVED;
    p->enable_state_before = read32(state + ENABLE_STATE_OFFSET);
    copy_3c_from_volatile(p->cache_a, state + CACHE_A_OFFSET);
    copy_3c_from_volatile(p->cache_b, state + CACHE_B_OFFSET);
    p->private_control = read32(state + PRIVATE_CONTROL_OFFSET);
    p->flags |= VBE_G1B_PLANE_CACHE_READ;

    mmio = read32(state);
    p->mmio_base = mmio;
    if (known_iftu_mmio(mmio)) {
        p->flags |= VBE_G1B_PLANE_MMIO_WHITELISTED;
        p->live_csc_control = read32((uintptr_t)mmio + LIVE_CSC_CONTROL_OFFSET);
        p->flags |= VBE_G1B_PLANE_CONTROL_READ;
    }

    p->enable_state_after = read32(state + ENABLE_STATE_OFFSET);
    if (p->enable_state_before == p->enable_state_after)
        p->flags |= VBE_G1B_PLANE_ENABLE_STABLE;
}

int vbeG1bTakeSnapshot(VbeG1bSnapshot *out) {
    VbeG1bSnapshot s;
    uint32_t cs;
    int ret;
    if (!out) return VBE_G1B_ERR_INVALID;

    zero_bytes(&s, sizeof(s));
    s.magic = VBE_G1B_SNAPSHOT_MAGIC;
    s.version = VBE_G1B_SNAPSHOT_VERSION;
    s.struct_size = sizeof(s);
    s.firmware_version = g_firmware_version;
    s.sequence = __sync_add_and_fetch(&g_sequence, 1u);
    s.status_flags = g_status_flags;
    s.lowio_modid = g_lowio_modid;
    s.lowio_module_nid = g_lowio_module_nid;
    s.lowio_segment_index = VBE_G1B_LOWIO_SEGMENT;

    if (g_status_flags & VBE_G1B_STATUS_PLANE0_RESOLVED)
        capture_plane(&s.planes[0], 0u, g_plane_state[0]);
    else
        s.planes[0].plane = 0u;

    if (g_status_flags & VBE_G1B_STATUS_PLANE1_RESOLVED)
        capture_plane(&s.planes[1], 1u, g_plane_state[1]);
    else
        s.planes[1].plane = 1u;

    ENTER_SYSCALL(cs);
    ret = ksceKernelMemcpyKernelToUser(out, &s, sizeof(s));
    EXIT_SYSCALL(cs);
    return ret;
}

void _start() __attribute__((weak, alias("module_start")));
int module_start(SceSize argc, const void *args) {
    SceKernelFwInfo fw;
    tai_module_info_t lowio;
    uintptr_t addr = 0u;
    int ret;
    (void)argc;
    (void)args;

    zero_bytes(g_plane_state, sizeof(g_plane_state));
    g_status_flags = 0u;
    g_firmware_version = 0u;
    g_lowio_modid = 0u;
    g_lowio_module_nid = 0u;
    g_sequence = 0u;

    zero_bytes(&fw, sizeof(fw));
    fw.size = sizeof(fw);
    if (ksceKernelGetSystemSwVersion(&fw) < 0) return SCE_KERNEL_START_SUCCESS;
    g_firmware_version = fw.version;
    if (fw.version != VBE_G1B_FW_365) return SCE_KERNEL_START_SUCCESS;
    g_status_flags |= VBE_G1B_STATUS_FW_OK;

    zero_bytes(&lowio, sizeof(lowio));
    lowio.size = sizeof(lowio);
    ret = taiGetModuleInfoForKernel(KERNEL_PID, "SceLowio", &lowio);
    if (ret < 0) return SCE_KERNEL_START_SUCCESS;
    g_lowio_modid = (uint32_t)lowio.modid;
    g_lowio_module_nid = lowio.module_nid;
    g_status_flags |= VBE_G1B_STATUS_LOWIO_FOUND;

    ret = module_get_offset(KERNEL_PID, lowio.modid, VBE_G1B_LOWIO_SEGMENT,
                            VBE_G1B_PLANE0_OFFSET, &addr);
    if (ret >= 0 && addr != 0u && (addr & 3u) == 0u) {
        g_plane_state[0] = addr;
        g_status_flags |= VBE_G1B_STATUS_PLANE0_RESOLVED;
    }

    addr = 0u;
    ret = module_get_offset(KERNEL_PID, lowio.modid, VBE_G1B_LOWIO_SEGMENT,
                            VBE_G1B_PLANE1_OFFSET, &addr);
    if (ret >= 0 && addr != 0u && (addr & 3u) == 0u) {
        g_plane_state[1] = addr;
        g_status_flags |= VBE_G1B_STATUS_PLANE1_RESOLVED;
    }

    return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize argc, const void *args) {
    (void)argc;
    (void)args;
    return SCE_KERNEL_STOP_SUCCESS;
}
