/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Fuxi (SM8550) FastRPC reverse file service glue for ADSP sensors PD.
 * Adapted from PaganiSensors/PaganiSensorsRpc.cpp (BSD-2-Clause-Patent, Copyright (c) 2026 UniXNU contributors)
 * to Fuxi's XNU BSD/GLINK/SMMU stack:
 *   - GLINK channel "fastrpcglink-apps-dsp" via ShengAdsp.h (registered before ADSP boot)
 *   - 8 MiB write-combined arena below 4 GiB (default 0xe8000000, pure unreserved RAM in /proc/iomem e7cb0000..ebbfffff)
 *   - SM8550 apps-smmu pass-through for compute_cb11 (bank 4: SIDs 0x1004/0x80 and 0x1064 -> bypass bank 4)
 *   - Two-tier SSCPACK1 pack loader:
 *       1) RAM-staged pack from loader.c via ADT /chosen "fuxi,sensor-pack" (<u64 pa, u64 size>) — instant, zero disk writes
 *       2) Fallback vnode_open("/private/var/mobile/Media/fuxi-sensors-pack.bin") if no staged pack is present
 */
#include <pexpert/arm64/board_config.h>
#if defined(FUXI)
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/vnode.h>
#include <sys/uio.h>
#include <kern/clock.h>
#include <kern/locks.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <kern/kalloc.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include <pexpert/device_tree.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>
#include "ShengAdsp.h"
#include "SensorsRpc.h"
#include "fuxi_sensors_smmu.h"
#include "fuxi_sensors_hid.h"

extern uint64_t gPhysBase, gPhysSize;
extern kern_return_t pmap_enter_options_addr(pmap_t, vm_map_offset_t, uint64_t, vm_prot_t, vm_prot_t, unsigned, boolean_t,
    unsigned, void *);
extern kern_return_t kernel_thread_start(thread_continue_t, void *, thread_t *);
extern void thread_deallocate(thread_t);
extern kern_return_t clear_wait(thread_t, wait_result_t);
extern wait_result_t thread_block(thread_continue_t);
extern unsigned fuxi_gpu_access(uintptr_t, uint64_t *, unsigned, unsigned);

#define FUXI_SENSORS_RPC_PA 0xe8000000ull
#define FUXI_SENSORS_PACK_MAX 0x200000u
#define FUXI_SENSORS_RPC_PROCESS 4242u

static const char kFallbackDir[] = "/private/var/mobile/Media";
static const char kFallbackPath[] = "/private/var/mobile/Media/fuxi-sensors-pack.bin";

struct fs_window {
    uint64_t pa;
    vm_size_t size;
    vm_offset_t va;
};

static kern_return_t
fs_map_prot(struct fs_window *w, unsigned wimg, vm_prot_t prot)
{
    vm_offset_t va = 0;
    if (w->va) return KERN_SUCCESS;
    if (!w->size || (w->pa & PAGE_MASK) || (w->size & PAGE_MASK)) return KERN_INVALID_ARGUMENT;
    kern_return_t kr = kmem_alloc(kernel_map, &va, w->size, KMA_VAONLY | KMA_KOBJECT | KMA_NOPAGEWAIT, VM_KERN_MEMORY_IOKIT);
    if (kr != KERN_SUCCESS) return kr;
    for (vm_size_t off = 0; off < w->size; off += PAGE_SIZE) {
        kr = pmap_enter_options_addr(kernel_pmap, va + off, w->pa + off, prot, VM_PROT_NONE, wimg, TRUE,
            PMAP_OPTIONS_NOWAIT, NULL);
        if (kr != KERN_SUCCESS) {
            pmap_remove(kernel_pmap, va, va + w->size);
            kmem_free(kernel_map, va, w->size);
            return kr;
        }
    }
    w->va = va;
    return KERN_SUCCESS;
}

static void
fs_unmap(struct fs_window *w)
{
    if (!w->va) return;
    pmap_remove(kernel_pmap, w->va, w->va + w->size);
    kmem_free(kernel_map, w->va, w->size);
    w->va = 0;
}

static bool
smmu_read_reg(void *context, uint32_t offset, uint32_t *value)
{
    const struct fs_window *w = (const struct fs_window *)context;
    uint64_t data = 0;
    if ((offset & 3u) || offset >= w->size || fuxi_gpu_access(w->va + offset, &data, 0, 0) ||
        (uint32_t)data == UINT32_MAX) {
        return false;
    }
    *value = (uint32_t)data;
    return true;
}

static bool
smmu_write_reg(void *context, uint32_t offset, uint32_t value)
{
    const struct fs_window *w = (const struct fs_window *)context;
    uint64_t data = value;
    if ((offset & 3u) || offset >= w->size) return false;
    __asm__ volatile("dsb sy" ::: "memory");
    if (fuxi_gpu_access(w->va + offset, &data, 1, 0)) return false;
    __asm__ volatile("dsb sy" ::: "memory");
    return true;
}

static SsrCore *gRpcCore;
static ShengAdspChannel *gRpcChannel;
static struct fs_window gArenaW;
static struct fs_window gPackW;
static uint64_t gPackRamSize;
static uint32_t gRpcBank = FUXI_SENSORS_SMMU_DEFAULT_BANK;
static volatile uint32_t gRpcPending;
static int gRpcStarted;

static uint64_t
rpc_now_ms(void *context)
{
    (void)context;
    uint64_t ns = 0;
    absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
    return ns / 1000000ull;
}

static void
rpc_wake(void)
{
    __atomic_store_n(&gRpcPending, 1u, __ATOMIC_RELEASE);
    thread_wakeup((event_t)&gRpcPending);
}

static void
rpc_channel_state(void *context, int up)
{
    (void)context;
    if (gRpcCore) ssr_core_channel(gRpcCore, up);
    rpc_wake();
}

static void
rpc_channel_receive(void *context, const uint8_t *data, uint32_t length)
{
    (void)context;
    if (gRpcCore) ssr_core_receive(gRpcCore, data, length);
    rpc_wake();
}

static int
rpc_op_send(void *context, const uint8_t *message, uint32_t length)
{
    (void)context;
    return sheng_adsp_send(gRpcChannel, message, length) == 0 ? 0 : -1;
}

static void
rpc_op_wait(void *context, uint32_t ms)
{
    (void)context;
    if (__atomic_exchange_n(&gRpcPending, 0u, __ATOMIC_ACQ_REL)) return;
    assert_wait_timeout((event_t)&gRpcPending, THREAD_UNINT, ms ? ms : 1u, 1000000u);
    if (__atomic_exchange_n(&gRpcPending, 0u, __ATOMIC_ACQ_REL)) {
        clear_wait(current_thread(), THREAD_AWAKENED);
        return;
    }
    thread_block(THREAD_CONTINUE_NULL);
    __atomic_store_n(&gRpcPending, 0u, __ATOMIC_RELAXED);
}

static void
rpc_op_trace(void *context, const char *line)
{
    (void)context;
    if (line) printf("%s\n", line);
}

static void
rpc_op_publish(void *context)
{
    (void)context;
    __asm__ volatile("dsb sy" ::: "memory");
}

static int
rpc_op_prepare(void *context)
{
    (void)context;
    struct fs_window smmu = {FUXI_APPS_SMMU_BASE, FUXI_APPS_SMMU_BYTES, 0};
    kern_return_t kr = fs_map_prot(&smmu, VM_WIMG_IO, VM_PROT_READ | VM_PROT_WRITE);
    if (kr != KERN_SUCCESS) {
        printf("[sensors] rpc smmu map failed kr=%d\n", (int)kr);
        return -12;
    }
    struct fuxi_sensors_smmu_info info;
    enum fuxi_sensors_smmu_status st = fuxi_sensors_smmu_attach(smmu_read_reg, smmu_write_reg, &smmu, gRpcBank, &info);
    fs_unmap(&smmu);
    printf("[sensors] rpc smmu rc=%d bank=%u slots=%d/%d smr=%#x/%#x ready=%u sync=%u gfsr=%#x->%#x gfsynr1=%#x\n",
        (int)st, info.bank, info.slots[0], info.slots[1], info.smr0, info.smr1,
        (unsigned)info.already_ready, (unsigned)info.tlb_synced,
        info.gfsr_before, info.gfsr_after, info.gfsynr1);
    return st == FUXI_SENSORS_SMMU_OK ? 0 : (int)st;
}

void
fuxi_sensors_rpc_log_smmu(const char *tag)
{
    struct fs_window smmu = {FUXI_APPS_SMMU_BASE, FUXI_APPS_SMMU_BYTES, 0};
    if (fs_map_prot(&smmu, VM_WIMG_IO, VM_PROT_READ) != KERN_SUCCESS) return;
    uint32_t gfsr = 0, gfsynr0 = 0, gfsynr1 = 0;
    (void)smmu_read_reg(&smmu, 0x48u, &gfsr);
    (void)smmu_read_reg(&smmu, 0x50u, &gfsynr0);
    (void)smmu_read_reg(&smmu, 0x54u, &gfsynr1);
    fs_unmap(&smmu);
    printf("[sensors] smmu(%s) gfsr=%#x gfsynr0=%#x gfsynr1=%#x\n",
        tag ? tag : "?", gfsr, gfsynr0, gfsynr1);
}

static int
rpc_op_pack_size(void *context, uint64_t *size)
{
    (void)context;
    if (gPackW.va && gPackRamSize) {
        *size = gPackRamSize;
        return SsrPackOk;
    }
    vfs_context_t ctx = vfs_context_kernel();
    vnode_t dir = NULL;
    int err = vnode_open(kFallbackDir, FREAD | O_NOFOLLOW | O_NONBLOCK, 0, 0, &dir, ctx);
    if (err) return err == ENOENT ? SsrPackNotYet : SsrPackError;
    vnode_close(dir, FREAD, ctx);

    vnode_t vp = NULL;
    err = vnode_open(kFallbackPath, FREAD | O_NOFOLLOW | O_NONBLOCK, 0, 0, &vp, ctx);
    if (err) return err == ENOENT ? SsrPackAbsent : SsrPackError;
    struct vnode_attr va;
    VATTR_INIT(&va);
    VATTR_WANTED(&va, va_data_size);
    err = vnode_getattr(vp, &va, ctx);
    const int ok = !err && VATTR_IS_SUPPORTED(&va, va_data_size);
    vnode_close(vp, FREAD, ctx);
    if (!ok) return SsrPackError;
    *size = va.va_data_size;
    return SsrPackOk;
}

static int
rpc_op_pack_read(void *context, uint64_t offset, void *to, uint32_t length)
{
    (void)context;
    if (!length) return 0;
    if (gPackW.va && gPackRamSize) {
        if (offset > gPackRamSize || length > gPackRamSize - offset) return -1;
        memcpy(to, (const uint8_t *)gPackW.va + offset, length);
        return 0;
    }
    vfs_context_t ctx = vfs_context_kernel();
    vnode_t vp = NULL;
    int err = vnode_open(kFallbackPath, FREAD | O_NOFOLLOW | O_NONBLOCK, 0, 0, &vp, ctx);
    if (err) return -1;
    int resid = 0;
    err = vn_rdwr(UIO_READ, vp, (caddr_t)to, (int)length, (off_t)offset, UIO_SYSSPACE, IO_NODELOCKED,
        vfs_context_ucred(ctx), &resid, vfs_context_proc(ctx));
    vnode_close(vp, FREAD, ctx);
    return (!err && resid == 0) ? 0 : -1;
}

static void *
rpc_op_alloc(void *context, uint32_t bytes)
{
    (void)context;
    if (!bytes) return NULL;
    return kalloc_data(bytes, Z_WAITOK | Z_ZERO);
}

static void
rpc_thread(void *arg, wait_result_t wr)
{
    (void)arg;
    (void)wr;
    while (ssr_core_step(gRpcCore)) {}
    printf("[sensors] rpc thread stopped st=%d err=%d calls=%u opens=%u missing=%u note=%s\n",
        (int)gRpcCore->status.stage, (int)gRpcCore->status.error,
        gRpcCore->status.calls, gRpcCore->status.opens, gRpcCore->status.missing,
        gRpcCore->status.note);
    fuxi_sensors_rpc_log_smmu("rpc-stop");
}

static int
ranges_overlap(uint64_t a_base, uint64_t a_size, uint64_t b_base, uint64_t b_size)
{
    if (!a_size || !b_size) return 0;
    return a_base < b_base + b_size && b_base < a_base + a_size;
}

static uint64_t
rpc_cfg_arena_pa(void)
{
    uint64_t pa = FUXI_SENSORS_RPC_PA;
    if (PE_parse_boot_argn("fuxisensorspa", &pa, sizeof(pa))) return pa;
    DTEntry chosen;
    const void *prop = NULL;
    unsigned bytes = 0;
    if (SecureDTLookupEntry(NULL, "/chosen", &chosen) == kSuccess &&
        SecureDTGetProperty(chosen, "fuxi,sensors-pa", &prop, &bytes) == kSuccess) {
        if (bytes == sizeof(uint64_t)) {
            memcpy(&pa, prop, sizeof(pa));
        } else if (bytes == sizeof(uint32_t)) {
            uint32_t pa32 = 0;
            memcpy(&pa32, prop, sizeof(pa32));
            pa = pa32;
        }
    }
    return pa;
}

static uint32_t
rpc_cfg_bank(void)
{
    uint32_t bank = FUXI_SENSORS_SMMU_DEFAULT_BANK;
    if (PE_parse_boot_argn("fuxisensorsbank", &bank, sizeof(bank))) return bank;
    DTEntry chosen;
    const void *prop = NULL;
    unsigned bytes = 0;
    if (SecureDTLookupEntry(NULL, "/chosen", &chosen) == kSuccess &&
        SecureDTGetProperty(chosen, "fuxi,sensors-bank", &prop, &bytes) == kSuccess &&
        bytes == sizeof(uint32_t)) {
        memcpy(&bank, prop, sizeof(bank));
    }
    return bank;
}

static void
rpc_map_staged_pack(uint64_t arena_pa)
{
    DTEntry chosen;
    const void *prop = NULL;
    unsigned bytes = 0;
    if (SecureDTLookupEntry(NULL, "/chosen", &chosen) != kSuccess ||
        SecureDTGetProperty(chosen, "fuxi,sensor-pack", &prop, &bytes) != kSuccess ||
        bytes != 16u) {
        return;
    }
    uint64_t vals[2] = {0, 0};
    memcpy(vals, prop, sizeof(vals));
    const uint64_t pa = vals[0], size = vals[1];
    if (!pa || (pa & PAGE_MASK) || size < SSP_HEADER || size > FUXI_SENSORS_PACK_MAX) {
        printf("[sensors] rpc staged pack invalid pa=%#llx size=%llu\n", pa, size);
        return;
    }
    const vm_size_t map_size = (vm_size_t)((size + PAGE_MASK) & ~(uint64_t)PAGE_MASK);
    if (pa > 0x100000000ull - map_size ||
        ranges_overlap(pa, map_size, arena_pa, SSR_ARENA_BYTES) ||
        ranges_overlap(pa, map_size, gPhysBase, gPhysSize)) {
        printf("[sensors] rpc staged pack overlap pa=%#llx size=%llu\n", pa, size);
        return;
    }
    gPackW = (struct fs_window){pa, map_size, 0};
    kern_return_t kr = fs_map_prot(&gPackW, VM_WIMG_WCOMB, VM_PROT_READ);
    if (kr != KERN_SUCCESS) {
        printf("[sensors] rpc staged pack map failed kr=%d\n", (int)kr);
        gPackW = (struct fs_window){0};
        return;
    }
    gPackRamSize = size;
    printf("[sensors] rpc staged RAM pack pa=%#llx size=%llu\n", pa, size);
}

int
fuxi_sensors_rpc_start(uint32_t log_level)
{
    if (gRpcStarted) return 0;
    const uint64_t arena_pa = rpc_cfg_arena_pa();
    const uint32_t bank = rpc_cfg_bank();
    if (!arena_pa || (arena_pa & PAGE_MASK) ||
        arena_pa > 0x100000000ull - SSR_ARENA_BYTES ||
        ranges_overlap(arena_pa, SSR_ARENA_BYTES, gPhysBase, gPhysSize)) {
        printf("[sensors] rpc bad arena pa=%#llx (xnu=%#llx+%#llx)\n", arena_pa, gPhysBase, gPhysSize);
        return -22;
    }
    if (bank < FUXI_SENSORS_SMMU_BANK_MIN || bank > FUXI_SENSORS_SMMU_BANK_MAX) {
        printf("[sensors] rpc bad bank=%u (need 3..7)\n", bank);
        return -22;
    }
    gRpcBank = bank;
    gArenaW = (struct fs_window){arena_pa, SSR_ARENA_BYTES, 0};
    kern_return_t kr = fs_map_prot(&gArenaW, VM_WIMG_WCOMB, VM_PROT_READ | VM_PROT_WRITE);
    if (kr != KERN_SUCCESS) {
        printf("[sensors] rpc arena map failed kr=%d\n", (int)kr);
        return -12;
    }
    bzero((void *)gArenaW.va, gArenaW.size);
    __asm__ volatile("dsb sy" ::: "memory");

    rpc_map_staged_pack(arena_pa);

    gRpcCore = (SsrCore *)kalloc_data(sizeof(SsrCore), Z_WAITOK | Z_ZERO);
    uint8_t *bounce = (uint8_t *)kalloc_data(SSR_BOUNCE, Z_WAITOK | Z_ZERO);
    if (!gRpcCore || !bounce) {
        printf("[sensors] rpc alloc failed\n");
        return -12;
    }

    static const SsrOps kOps = {
        .context = NULL,
        .now_ms = rpc_now_ms,
        .send = rpc_op_send,
        .wait = rpc_op_wait,
        .trace = rpc_op_trace,
        .publish = rpc_op_publish,
        .prepare = rpc_op_prepare,
        .pack_size = rpc_op_pack_size,
        .pack_read = rpc_op_pack_read,
        .alloc = rpc_op_alloc,
    };
    const SsrConfig cfg = {
        .arena = (uint8_t *)gArenaW.va,
        .arena_pa = arena_pa,
        .arena_size = SSR_ARENA_BYTES,
        .bounce = bounce,
        .process = FUXI_SENSORS_RPC_PROCESS,
        .bank = gRpcBank,
        .log = log_level,
    };
    ssr_core_init(gRpcCore, &kOps, &cfg);

    static const ShengAdspClient kClient = {rpc_channel_state, rpc_channel_receive};
    gRpcChannel = sheng_adsp_channel(SSR_CHANNEL, SSR_INTENT_SIZE, SSR_INTENT_COUNT, 0, &kClient, NULL);
    if (!gRpcChannel) {
        printf("[sensors] rpc glink channel %s registration failed\n", SSR_CHANNEL);
        return -19;
    }

    thread_t thread = NULL;
    if (kernel_thread_start(rpc_thread, NULL, &thread) != KERN_SUCCESS) {
        printf("[sensors] rpc thread start failed\n");
        return -7;
    }
    thread_deallocate(thread);
    gRpcStarted = 1;
    printf("[sensors] rpc started channel=%s pa=%#llx bank=%u staged_pack=%llu\n",
        SSR_CHANNEL, arena_pa, gRpcBank, gPackRamSize);
    return 0;
}

int fuxi_sensors_rpc_status(SsrStatus *out);
int
fuxi_sensors_rpc_status(SsrStatus *out)
{
    if (!out || !gRpcCore) return -19;
    *out = gRpcCore->status;
    return 0;
}
#endif /* FUXI */
