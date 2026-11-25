/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* FUXI audio DSP (ADSP, LPASS) owner. Boot argument fuxiadsp (or ADT /chosen u32 "fuxi,adsp" when the command line is full):
 *   1  survey what the boot stages left (SMEM partition and items, the DSP's SMP2P state, the AOSS mailbox, the RSC, the GIC
 *      enables of the two SPIs used below) and write nothing;
 *   2  also start the DSP and bring up SMP2P, GLINK, the IPC router's service locator (the DSP's charger domain asks it before it
 *      starts) and the battery manager client (fuxi_adsp_battery.c), which logs what the DSP's battery manager reports.
 * Adapted from UniXNU-sheng package/AdspProbe.cpp (6880068), BSD-2-Clause-Patent: Copyright (c) 2026, UniXNU contributors
 * (LICENSE.UniXNU-sheng). The protocol modules (Qcom*.c, FatVolume.c, ShengBattmgr.c, ShengQrtr.c) are sheng's, unchanged.
 *
 * Devicetree facts (Fuxi live DTB, the same as sheng's SM8550 values): remoteproc-adsp (qcom,kalama-adsp-pas), PAS id 1 for adsp in
 * adspslpi 0x9ea00000 + 0x4680000 and id 36 for adsp_dtb in q6_adsp_dtb 0x9e980000 + 0x80000 (Fuxi's images fill exactly these:
 * checked offline against the modem_a backup); lcx/lmx proxy votes and the AOSS load_state "adsp"; SMEM 0x81d00000 + 0x200000 with
 * TCSR mutex 3 (syscon 0x1f40000, stride 0x1000); smp2p-adsp items 443 in / 429 out, remote pid 2; IPCC 0x408000 (SPI 229): LPASS
 * client 3 with signal 0 GLINK and 2 SMP2P, AOP client 0 signal 0; GLINK items 478/479/480; AOSS 0xc300000 + 0x400; apps RSC DRV 2
 * at 0x17a20000, TCS block +0xd00, SPI 5; cmd-db 0x81c60000. All of it lies outside XNU's RAM (0x8c0000000 up; mkadt-fuxi.py
 * refuses any overlap with a reserved-memory node).
 *
 * Differences from sheng. No interrupts: this kernel's GIC code has one SPI handler (the GPU fence's), so the thread polls the
 * IPCC receive register (1 ms during bring-up, 2 ms for 500 ms after any traffic, else 200 ms) and the RSC completion status. The
 * IPCC (SPI 229) and RSC (SPI 5) lines are masked in the distributor first: a level line left enabled by the boot stages would
 * otherwise storm sleh's ownerless EOI path. MMIO and the carveouts are mapped with kernel_map VA and pmap entries (never
 * ml_io_map: its 8 MiB submap is KMA_NOFAIL). The firmware comes from modem_a in place (fuxi_adsp_fw.c). Refused when fuxiwlan is
 * set until combined ADSP/WLAN hardware validation is complete (a test loader may opt in with ADT fuxi,adsp-wlan=1); both now use
 * fuxi_rpmh_shared's mutex.
 *
 * One kernel thread owns every step and all protocol state; clients (ShengAdsp.h) register channels and queue sends from any
 * thread, and their callbacks run on this thread (they must not block or call sheng_adsp_send). */
#include <pexpert/arm64/board_config.h>
#if defined(FUXI)
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
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
#include <arm/caches_internal.h>
#include "QcomGlink.h"
#include "QcomMdt.h"
#include "QcomQmp.h"
#include "QcomRpmh.h"
#include "QcomScm.h"
#include "QcomSmem.h"
#include "QcomSmp2p.h"
#include "ShengAdsp.h"
#include "fuxi_audio_smmu.h"
#include "fuxi_rpmh_shared.h"

extern ppnum_t pmap_find_phys(pmap_t, addr64_t);
extern kern_return_t pmap_enter_options_addr(pmap_t, vm_map_offset_t, uint64_t, vm_prot_t, vm_prot_t, unsigned, boolean_t,
    unsigned, void *);
extern kern_return_t kernel_thread_start(thread_continue_t, void *, thread_t *);
/* Declared here as in fuxi_keys.c / sweet_ufs.c: the BSD header export does not reliably show these (osfmk/kern/{thread,
 * sched_prim}.h prototypes). */
extern kern_return_t thread_terminate(thread_t);
extern void thread_deallocate(thread_t);
extern kern_return_t clear_wait(thread_t, wait_result_t);
extern wait_result_t thread_block(thread_continue_t);
extern void IOSleep(unsigned milliseconds);
extern int fuxi_adsp_fw_open(const char *name, void **handle, uint64_t *size);
extern int fuxi_adsp_fw_read(void *handle, uint64_t offset, void *to, size_t length);
extern void fuxi_adsp_fw_close(void *handle);
extern void sheng_qrtr_start(void);
extern void fuxi_adsp_battery_start(int publish);
extern void fuxi_amp_init(void);
extern void fuxi_pmic_init(void);
extern void fuxi_ramtest_init(void);
extern void fuxi_bt_init(void);
extern void fuxi_audio_spf_start(void);
extern void fuxi_audio_spf_poll(void);
extern unsigned fuxi_gpu_access(uintptr_t, uint64_t *, unsigned, unsigned);
extern volatile int32_t native_qrtr_stats[16];
extern char native_qrtr_text[256];
void fuxi_adsp_init(void);

enum Stage {
    StMapped = 1, StSurveyed = 2, StFirmware = 3, StSmp2p = 4, StDoorbells = 5, StLoadState = 6, StRails = 7, StDtb = 8, StLoaded = 9,
    StStarted = 10, StReady = 11, StGlink = 12, StChannel = 13,
};
#define FA_SMEM 0x81d00000ull
#define FA_SMEM_SIZE 0x200000u
#define FA_TCSR 0x1f40000ull /* syscon; mutex 3 at +0x3000 */
#define FA_MUTEX_OFF 0x3000u
#define FA_IPCC 0x408000ull
#define FA_AOSS 0xc300000ull
#define FA_RSC 0x17a20000ull
#define FA_RSC_SIZE 0x10000u
#define FA_RSC_TCS 0xd00u
#define FA_CMD_DB 0x81c60000ull
#define FA_CMD_DB_SIZE 0x20000u
#define FA_GICD 0x17100000ull
#define FA_ADSP_BASE 0x9ea00000ull
#define FA_ADSP_SIZE 0x4680000ull
#define FA_DTB_BASE 0x9e980000ull
#define FA_DTB_SIZE 0x80000ull
#define FA_PAS_ADSP 1u
#define FA_PAS_DTB 36u
#define FA_REMOTE_PID 2u
#define FA_ITEM_IN 443u
#define FA_ITEM_OUT 429u
#define FA_ITEM_CRASH 423u
#define FA_ITEM_GLINK_DESC 478u
#define FA_ITEM_GLINK_TX 479u
#define FA_ITEM_GLINK_RX 480u
#define FA_GLINK_FIFO 0x4000u
#define FA_INTID_IPCC (32u + 229u)
#define FA_INTID_RSC (32u + 5u)
#define FA_FATAL (1u << 0)
#define FA_READY (1u << 1)
#define FA_HANDOVER (1u << 2)
#define EV_SMP2P 1u
#define EV_GLINK 2u
#define EV_AOP 4u
#define EV_SEND 16u
#define FA_CLIENTS 6u
#define FA_QUEUE 16u
#define FA_FAST_MS 500u /* fast polling after the last traffic */

/* ---- state, all owned by the thread except where noted ---- */
struct fa_window { uint64_t pa; vm_size_t size; vm_offset_t va; };
static struct fa_window gSmemW = {FA_SMEM, FA_SMEM_SIZE, 0}, gTcsr = {FA_TCSR, 0x4000, 0}, gIpcc = {FA_IPCC, 0x4000, 0},
    gAoss = {FA_AOSS, 0x4000, 0}, gRsc = {FA_RSC, FA_RSC_SIZE, 0}, gGicd = {FA_GICD, 0x4000, 0};
static QcomSmem gSmem;
static QcomSmemPartition gPart;
static QcomSmp2p gP2p;
static int gStopEntry = -1;
static QcomQmp gQmp;
static QcomGlink gGlink;
static int gGlinkStarted, gIpccOn;
static QcomRscLayout gLayout;
static uint32_t gRscBits;
static int gTcs = -1;
static volatile uint32_t gPending;
static uint32_t gSlave, gSlaveKnown, gHandedOver;
static int gStage, gError;
static uint64_t gFastUntil;
static uint32_t gIpccEvents, gIpccOther;
static int64_t gScmX0;
static uint64_t gScmX1;
static QcomRpmhResource gLcx, gLmx;
static uint32_t gVoteLcx, gVoteLmx, gVoteDone;

struct fa_message { uint32_t length; uint8_t data[SHENG_ADSP_MAX_MESSAGE]; };
struct ShengAdspChannel {
    char name[QCOM_GLINK_NAME];
    uint32_t intent_size, intent_count;
    int initiator;
    ShengAdspClient client;
    void *context;
    uint8_t *storage;
    QcomGlinkChannel *glink;
    struct fa_message *queue; /* FA_QUEUE slots */
    uint32_t head, count;
    uint8_t was_up;
};
static struct ShengAdspChannel gClients[FA_CLIENTS];
static uint32_t gClientCount;
static LCK_GRP_DECLARE(fa_grp, "fuxi_adsp");
static LCK_MTX_DECLARE(fa_client_lock, &fa_grp);
static int gStarted;

/* ---- MMIO ---- */
static uint32_t rd(const struct fa_window *w, uint64_t at) { return *(volatile uint32_t *)(w->va + at); }
static void wr(const struct fa_window *w, uint64_t at, uint32_t value)
{
    *(volatile uint32_t *)(w->va + at) = value;
    __asm__ volatile("dsb sy" ::: "memory");
}
/* kernel_map VA plus one pmap entry per page; never a partial window. */
static kern_return_t fa_map_prot(struct fa_window *w, unsigned wimg, vm_prot_t prot)
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

static kern_return_t fa_map(struct fa_window *w, unsigned wimg)
{
    return fa_map_prot(w, wimg, VM_PROT_READ | VM_PROT_WRITE);
}

static bool audio_smmu_read(void *context, uint32_t offset, uint32_t *value)
{
    const struct fa_window *window = context;
    uint64_t data = 0;
    if (offset & 3u || offset >= window->size ||
        fuxi_gpu_access(window->va + offset, &data, 0, 0) || (uint32_t)data == UINT32_MAX) {
        return false;
    }
    *value = (uint32_t)data;
    return true;
}

static struct fa_window gAudioDma;

bool fuxi_audio_dma_open(uint64_t *address);
void fuxi_audio_dma_close(void);

bool fuxi_audio_dma_open(uint64_t *address)
{
    DTEntry chosen;
    const void *property;
    unsigned bytes;
    struct fuxi_audio_dma_handoff handoff;
    if (gAudioDma.va || SecureDTLookupEntry(NULL, "/chosen", &chosen) != kSuccess ||
        SecureDTGetProperty(chosen, "fuxi,audio-dma", &property, &bytes) != kSuccess ||
        bytes != sizeof(handoff)) {
        return false;
    }
    memcpy(&handoff, property, sizeof(handoff));
    if (!fuxi_audio_dma_handoff_valid(&handoff)) {
        return false;
    }

    struct fa_window smmu = {FUXI_APPS_SMMU_BASE, FUXI_APPS_SMMU_BYTES, 0};
    if (fa_map_prot(&smmu, VM_WIMG_IO, VM_PROT_READ) != KERN_SUCCESS) {
        return false;
    }
    struct fuxi_audio_smmu_state state;
    const bool usable = fuxi_audio_smmu_read_state(audio_smmu_read, &smmu, &state) == FUXI_AUDIO_SMMU_OK &&
        fuxi_audio_smmu_routes_ready(&state) && fuxi_audio_smmu_bypass_ready(audio_smmu_read, &smmu, &state);
    pmap_remove(kernel_pmap, smmu.va, smmu.va + smmu.size);
    kmem_free(kernel_map, smmu.va, smmu.size);
    if (!usable) {
        return false;
    }

    gAudioDma = (struct fa_window){handoff.pa, handoff.bytes, 0};
    if (fa_map(&gAudioDma, VM_WIMG_WCOMB) != KERN_SUCCESS) {
        return false;
    }
    bzero((void *)gAudioDma.va, gAudioDma.size);
    __asm__ volatile("dsb sy" ::: "memory");
    *address = (uint64_t)FUXI_AUDIO_DMA_STREAM << 32 | handoff.pa;
    printf("FUXI AUDIO PCM: loader DMA region pa=%#x bytes=%#x stream=%u\n",
        handoff.pa, handoff.bytes, FUXI_AUDIO_DMA_STREAM);
    return true;
}

/* 出聲模式寫 PCM 用；fuxi_audio_dma_open 成功前與 close 之後都是 NULL。 */
void *fuxi_audio_dma_buffer(void);
void *fuxi_audio_dma_buffer(void)
{
    return gAudioDma.va ? (void *)gAudioDma.va : NULL;
}

void fuxi_audio_dma_close(void)
{
    if (gAudioDma.va) {
        pmap_remove(kernel_pmap, gAudioDma.va, gAudioDma.va + gAudioDma.size);
        kmem_free(kernel_map, gAudioDma.va, gAudioDma.size);
        /* The loader still reserves the physical storage for this boot. */
        gAudioDma = (struct fa_window){0};
    }
}

void fuxi_audio_dma_survey(void);

void fuxi_audio_dma_survey(void)
{
    struct fa_window window = {FUXI_APPS_SMMU_BASE, FUXI_APPS_SMMU_BYTES, 0};
    kern_return_t error = fa_map_prot(&window, VM_WIMG_IO, VM_PROT_READ);
    if (error != KERN_SUCCESS) {
        printf("FUXI AUDIO DMA: apps_smmu read-only map error=%d\n", error);
        return;
    }
    struct fuxi_audio_smmu_state state;
    enum fuxi_audio_smmu_result result = fuxi_audio_smmu_read_state(audio_smmu_read, &window, &state);
    pmap_remove(kernel_pmap, window.va, window.va + window.size);
    kmem_free(kernel_map, window.va, window.size);
    printf("FUXI AUDIO DMA: apps_smmu survey=%u sCR0=%#x IDR=%#x/%#x/%#x GFSR=%#x\n",
        (unsigned)result, state.scr0, state.idr0, state.idr1, state.idr2, state.gfsr);
    if (result != FUXI_AUDIO_SMMU_OK) {
        return;
    }
    printf("FUXI AUDIO DMA: SMRs=%u banks=%u blank=%u first=%d/%d inactive-unreferenced=%u first-bank=%d WLAN-slot=%d S2CR=%#x\n",
        state.slots, state.banks, state.blank_slots, state.first_slots[0], state.first_slots[1],
        state.unused_banks, state.first_bank, state.wlan.slot, state.wlan.s2cr);
    printf("FUXI AUDIO DMA: SID 0x1001 mask 0x80 matches=%u first=%d SMR=%#x S2CR=%#x\n",
        state.audio[0].matches, state.audio[0].slot, state.audio[0].smr, state.audio[0].s2cr);
    printf("FUXI AUDIO DMA: SID 0x1061 matches=%u first=%d SMR=%#x S2CR=%#x\n",
        state.audio[1].matches, state.audio[1].slot, state.audio[1].smr, state.audio[1].s2cr);
}

/* Page table entries only: no stale mapping of a carveout the secure world takes over (a Normal-NC mapping may be read
 * speculatively). The VA range stays reserved, as fuxi_adreno.c keeps its windows; kmem_free of a KMA_VAONLY range is used
 * nowhere else in this kernel. At most three windows go this way per boot (cmd-db, adsp_dtb, adsp). */
static void fa_unmap(struct fa_window *w)
{
    if (!w->va) return;
    pmap_remove(kernel_pmap, w->va, w->va + w->size);
    w->va = 0;
}

static void stage(int value) { gStage = value; }
static void fail(int step, int error)
{
    gStage = -step;
    gError = error;
    printf("FUXI ADSP: failed at stage %d error %d (scm x0=%lld x1=%#llx)\n", step, error, (long long)gScmX0,
        (unsigned long long)gScmX1);
}

/* ---- the GIC: mask the two level lines this owner makes the hardware raise (no handler in this kernel owns them) ---- */
static int fa_gic_enabled(unsigned intid) { return (rd(&gGicd, 0x100 + (intid / 32) * 4) >> (intid % 32)) & 1; }
static void fa_gic_mask(unsigned intid)
{
    wr(&gGicd, 0x180 + (intid / 32) * 4, 1u << (intid % 32));
    for (unsigned n = 0; (rd(&gGicd, 0) & 0x80000000u) && n < 100000; n++) delay(1); /* GICD_CTLR.RWP */
}

/* ---- events: the IPCC receive register, polled ---- */
static void wake(uint32_t bits)
{
    __atomic_fetch_or(&gPending, bits, __ATOMIC_RELEASE);
    thread_wakeup((event_t)&gPending);
}
static uint32_t ipccPoll(void)
{
    uint32_t bits = 0;
    if (!gIpccOn) return 0;
    for (unsigned n = 0; n < 32; ++n) {
        const uint32_t id = rd(&gIpcc, QCOM_IPCC_RECV_ID);
        if (id == QCOM_IPCC_NONE) break;
        wr(&gIpcc, QCOM_IPCC_RECV_CLEAR, id);
        if (id == qcom_ipcc_id(QCOM_IPCC_CLIENT_LPASS, QCOM_IPCC_SIGNAL_SMP2P)) bits |= EV_SMP2P;
        else if (id == qcom_ipcc_id(QCOM_IPCC_CLIENT_LPASS, QCOM_IPCC_SIGNAL_GLINK_QMP)) bits |= EV_GLINK;
        else if (id == qcom_ipcc_id(QCOM_IPCC_CLIENT_AOP, QCOM_IPCC_SIGNAL_GLINK_QMP)) bits |= EV_AOP;
        else gIpccOther++;
    }
    if (bits) gIpccEvents++;
    return bits;
}
/* Events within ms milliseconds (0: none), polling every poll_ms. */
static uint32_t waitEvents(uint32_t ms, uint32_t poll_ms)
{
    uint64_t deadline;
    clock_interval_to_deadline(ms, NSEC_PER_MSEC, &deadline);
    for (;;) {
        uint32_t bits = __atomic_exchange_n(&gPending, 0u, __ATOMIC_ACQ_REL) | ipccPoll();
        if (bits) return bits;
        const uint64_t now = mach_absolute_time();
        if (now >= deadline) return 0;
        uint64_t left = 0;
        absolutetime_to_nanoseconds(deadline - now, &left);
        uint32_t step = (uint32_t)(left / 1000000u) + 1u;
        if (step > poll_ms) step = poll_ms;
        assert_wait_timeout((event_t)&gPending, THREAD_UNINT, step, NSEC_PER_MSEC);
        if (__atomic_load_n(&gPending, __ATOMIC_ACQUIRE)) {
            clear_wait(current_thread(), THREAD_AWAKENED);
            continue;
        }
        thread_block(THREAD_CONTINUE_NULL);
    }
}
static void ring(uint32_t client, uint32_t signal) { wr(&gIpcc, QCOM_IPCC_SEND, qcom_ipcc_id(client, signal)); }
static void kickAop(void *context) { (void)context; ring(QCOM_IPCC_CLIENT_AOP, QCOM_IPCC_SIGNAL_GLINK_QMP); }
static void kickGlink(void *context) { (void)context; ring(QCOM_IPCC_CLIENT_LPASS, QCOM_IPCC_SIGNAL_GLINK_QMP); }
static void kickSmp2p(void) { ring(QCOM_IPCC_CLIENT_LPASS, QCOM_IPCC_SIGNAL_SMP2P); }

/* ---- SMEM (allocation under the TCSR hardware mutex) ---- */
static int lockSmem(void)
{
    for (unsigned i = 0; i < 1000; ++i) {
        wr(&gTcsr, FA_MUTEX_OFF, 1);
        if (rd(&gTcsr, FA_MUTEX_OFF) == 1) return 1;
        delay(10);
    }
    return 0;
}
static void unlockSmem(void) { wr(&gTcsr, FA_MUTEX_OFF, 0); }
static int allocItem(uint16_t id, uint32_t size)
{
    if (!lockSmem()) return -16;
    const int rc = qcom_smem_alloc(&gSmem, &gPart, id, size);
    unlockSmem();
    return rc == QcomSmemExists ? 0 : rc;
}
static volatile uint8_t *item(uint16_t id, uint32_t *size)
{
    uint32_t at = 0, bytes = 0;
    if (qcom_smem_find(&gSmem, &gPart, id, &at, &bytes)) return NULL;
    if (size) *size = bytes;
    return (volatile uint8_t *)gSmemW.va + at;
}
static void readCrashReason(void)
{
    QcomSmemPartition global;
    uint32_t at = 0, size = 0;
    if (qcom_smem_partition(&gSmem, QCOM_SMEM_HOST_GLOBAL, QCOM_SMEM_HOST_GLOBAL, &global) ||
        qcom_smem_find(&gSmem, &global, FA_ITEM_CRASH, &at, &size) || !size)
        return;
    char text[80];
    size_t n = 0;
    for (; n < sizeof(text) - 1 && n < size; ++n) {
        const char c = (char)((volatile uint8_t *)gSmemW.va)[at + n];
        if (!c) break;
        text[n] = c >= 32 && c < 127 ? c : '.';
    }
    text[n] = 0;
    if (n) printf("FUXI ADSP: crash reason \"%s\"\n", text);
}

/* ---- the survey (fuxiadsp=1, and the first step of 2): reads only ---- */
static void survey(void)
{
    uint32_t bits = 0;
    if (!qcom_smem_partition(&gSmem, QCOM_SMEM_HOST_APPS, FA_REMOTE_PID, &gPart)) {
        bits |= 1u << 8;
        const uint16_t ids[5] = {FA_ITEM_IN, FA_ITEM_OUT, FA_ITEM_GLINK_DESC, FA_ITEM_GLINK_TX, FA_ITEM_GLINK_RX};
        for (unsigned i = 0; i < 5; ++i) if (item(ids[i], NULL)) bits |= 1u << i;
    }
    QcomSmemPartition global;
    if (!qcom_smem_partition(&gSmem, QCOM_SMEM_HOST_GLOBAL, QCOM_SMEM_HOST_GLOBAL, &global)) bits |= 1u << 9;
    gSlaveKnown = 0;
    volatile uint8_t *in = item(FA_ITEM_IN, NULL);
    if (in) {
        QcomSmp2p view;
        bzero(&view, sizeof(view));
        view.in = (volatile uint32_t *)in;
        view.local_pid = 0;
        view.remote_pid = FA_REMOTE_PID;
        const int index = qcom_smp2p_check_in(view.in, 0, FA_REMOTE_PID) ? -1 : qcom_smp2p_find_in(&view, "slave-kernel");
        if (index >= 0) { gSlave = qcom_smp2p_in_value(&view, index); gSlaveKnown = 1; }
    }
    const uint32_t up = 0xffff;
    const uint32_t aoss = (rd(&gAoss, 0) == 0x4d41494cu ? 1u : 0u) << 12 | (uint32_t)(rd(&gAoss, 0xc) == up) << 8 |
        (uint32_t)(rd(&gAoss, 0x24) == up) << 9 | (uint32_t)(rd(&gAoss, 0x14) == up) << 10 | (uint32_t)(rd(&gAoss, 0x2c) == up) << 11;
    printf("FUXI ADSP: survey items=%#x (bit0 smp2p-in 1 out 2-4 glink 8 partition 9 global) slave-kernel=%s%#x aoss=%#x "
        "rsc_drv=%#x gic_ipcc_en=%d gic_rsc_en=%d\n", bits, gSlaveKnown ? "" : "none/", gSlave, aoss, rd(&gRsc, 0),
        fa_gic_enabled(FA_INTID_IPCC), fa_gic_enabled(FA_INTID_RSC));
    readCrashReason();
}

static void service(uint32_t bits);

/* ---- RPMh proxy votes: active TCS 1 (WLAN uses 0), completion polled ---- */
static int readCmdDb(uint8_t *copy)
{
    struct fa_window db = {FA_CMD_DB, FA_CMD_DB_SIZE, 0};
    if (fa_map(&db, VM_WIMG_IO) != KERN_SUCCESS) return 0;
    for (uint32_t i = 0; i < FA_CMD_DB_SIZE; i += 4) {
        const uint32_t w = rd(&db, i);
        memcpy(copy + i, &w, 4);
    }
    fa_unmap(&db);
    return 1;
}
static int vote(uint32_t lcx, uint32_t lmx)
{
    if (fuxi_rpmh_acquire()) return -EIO;
    if (gTcs < 0) {
        gTcs = 1;
        gRscBits = 1u << gTcs;
    }
    const uint32_t base = (unsigned)gTcs * gLayout.tcs_stride;
    if (!rd(&gRsc, FA_RSC_TCS + base + gLayout.status) ||
        rd(&gRsc, FA_RSC_TCS + base + gLayout.control) ||
        rd(&gRsc, FA_RSC_TCS + base + gLayout.cmd_enable) ||
        rd(&gRsc, FA_RSC_TCS + base + gLayout.wait_for_cmpl) ||
        (rd(&gRsc, FA_RSC_TCS + gLayout.irq_status) & gRscBits)) {
        fuxi_rpmh_release(1);
        return -EBUSY;
    }
    const uint32_t addresses[2] = {gLcx.address, gLmx.address}, values[2] = {lcx, lmx};
    QcomRpmhWrite writes[16];
    const unsigned n = qcom_rpmh_active_request(&gLayout, (unsigned)gTcs, addresses, values, 2, writes, 16);
    if (!n) {
        fuxi_rpmh_release(1);
        return -EINVAL;
    }
    const uint32_t irq_enable = rd(&gRsc, FA_RSC_TCS);
    wr(&gRsc, FA_RSC_TCS, irq_enable | gRscBits);
    for (unsigned i = 0; i < n; ++i) wr(&gRsc, FA_RSC_TCS + writes[i].offset, writes[i].value);
    int done = 0;
    for (unsigned t = 0; t < 1000 && !done; ++t) { /* up to ~100 ms */
        done = (rd(&gRsc, FA_RSC_TCS + gLayout.irq_status) & gRscBits) != 0;
        if (!done) delay(100);
    }
    if (!done) {
        gVoteDone = 0;
        printf("FUXI ADSP: rpmh vote timed out; shared RSC quarantined\n");
        fuxi_rpmh_release(0);
        return -ETIMEDOUT;
    }
    const unsigned m = qcom_rpmh_finish(&gLayout, (unsigned)gTcs, writes, 16);
    for (unsigned i = 0; i < m; ++i) wr(&gRsc, FA_RSC_TCS + writes[i].offset, writes[i].value);
    wr(&gRsc, FA_RSC_TCS + base + gLayout.wait_for_cmpl, 0);
    wr(&gRsc, FA_RSC_TCS, (rd(&gRsc, FA_RSC_TCS) & ~gRscBits) | (irq_enable & gRscBits));
    const int healthy = !rd(&gRsc, FA_RSC_TCS + base + gLayout.control) &&
        !rd(&gRsc, FA_RSC_TCS + base + gLayout.cmd_enable) &&
        !rd(&gRsc, FA_RSC_TCS + base + gLayout.wait_for_cmpl) &&
        !(rd(&gRsc, FA_RSC_TCS + gLayout.irq_status) & gRscBits) &&
        rd(&gRsc, FA_RSC_TCS + base + gLayout.status) &&
        (rd(&gRsc, FA_RSC_TCS) & gRscBits) == (irq_enable & gRscBits);
    fuxi_rpmh_release(healthy);
    if (!healthy) {
        gVoteDone = 0;
        printf("FUXI ADSP: rpmh cleanup failed; shared RSC quarantined\n");
        return -EIO;
    }
    gVoteLcx = lcx; gVoteLmx = lmx; gVoteDone = (uint32_t)done;
    printf("FUXI ADSP: rpmh vote lcx=%u lmx=%u done=%d\n", lcx, lmx, done);
    return 0;
}

/* ---- secure world calls ---- */
struct scm_result { int64_t x0; uint64_t x1; };
static struct scm_result scm(uint32_t command, uint32_t arginfo, uint64_t a, uint64_t b, uint64_t c)
{
    struct scm_result result = {-1, 0};
    for (unsigned busy = 0; busy < 20; ++busy) {
        uint64_t function = qcom_scm_fn(QCOM_SCM_SVC_PIL, command), session = 0;
        unsigned resumes = 0;
        for (;;) {
            register uint64_t x0 __asm__("x0") = function, x1 __asm__("x1") = arginfo, x2 __asm__("x2") = a, x3 __asm__("x3") = b,
                x4 __asm__("x4") = c, x5 __asm__("x5") = 0, x6 __asm__("x6") = session, x7 __asm__("x7") = 0;
            __asm__ volatile("smc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x4), "+r"(x5), "+r"(x6), "+r"(x7)
                :: "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "memory");
            result.x0 = (int64_t)x0;
            result.x1 = x1;
            if ((int64_t)x0 != QCOM_SCM_INTERRUPTED || ++resumes > 100000) break;
            function = QCOM_SCM_INTERRUPTED; /* resume the interrupted call in its session */
            session = x6;
        }
        if (result.x0 != QCOM_SCM_V2_EBUSY) break;
        IOSleep(30);
    }
    gScmX0 = result.x0;
    gScmX1 = result.x1;
    return result;
}
static int scmOk(struct scm_result r) { return r.x0 == 0 && r.x1 == 0; }

/* ---- a firmware image: parsed from its file, metadata handed over, segments written into the carveout ---- */
struct fa_image {
    void *file;
    uint64_t size;
    QcomMdtImage mdt;
    vm_offset_t meta;
    vm_size_t meta_bytes;
};
static struct fa_image gDtbImage, gAdspImage;
static int fileRead(void *context, uint64_t offset, void *to, size_t length) { return fuxi_adsp_fw_read(context, offset, to, length); }
static void release(struct fa_image *image)
{
    if (image->file) fuxi_adsp_fw_close(image->file);
    if (image->meta) kmem_free(kernel_map, image->meta, image->meta_bytes);
    image->file = NULL;
    image->meta = 0;
}
static int openImage(struct fa_image *image, const char *name)
{
    int rc = fuxi_adsp_fw_open(name, &image->file, &image->size);
    if (rc) { image->file = NULL; return rc; }
    rc = qcom_mdt_parse(&image->mdt, image->size, fileRead, image->file);
    return rc ? 1000 - rc : 0;
}
/* init_image with the metadata in physically contiguous memory, then the memory setup of a relocatable image. */
static int authenticateHeader(struct fa_image *image, uint32_t pas, uint64_t base)
{
    const uint64_t bytes = qcom_mdt_metadata_size(&image->mdt);
    vm_offset_t va = 0;
    if (!bytes || bytes > 1024 * 1024) return -22;
    image->meta_bytes = round_page((vm_size_t)bytes);
    if (kmem_alloc_contig(kernel_map, &va, image->meta_bytes, PAGE_MASK, 0, 0, KMA_KOBJECT | KMA_ZERO | KMA_NOPAGEWAIT,
        VM_KERN_MEMORY_IOKIT) != KERN_SUCCESS)
        return -12;
    image->meta = va;
    if (qcom_mdt_metadata(&image->mdt, fileRead, image->file, (uint8_t *)va, bytes)) return -5;
    CleanPoC_DcacheRegion_Force(va, (unsigned)image->meta_bytes);
    const ppnum_t pn = pmap_find_phys(kernel_pmap, (addr64_t)va);
    if (!pn) return -14;
    const uint64_t phys = ptoa_64(pn);
    if (!scmOk(scm(QCOM_SCM_PAS_INIT_IMAGE, qcom_scm_args(2, QCOM_SCM_VAL, QCOM_SCM_RW, 0), pas, phys, 0))) return -1;
    if (image->mdt.relocatable &&
        !scmOk(scm(QCOM_SCM_PAS_MEM_SETUP, qcom_scm_args(3, 0, 0, 0), pas, base, qcom_mdt_span(&image->mdt))))
        return -2;
    return 0;
}
static int loadSegments(struct fa_image *image, uint64_t base, uint64_t size)
{
    struct fa_window region = {base, (vm_size_t)size, 0};
    const size_t chunk = 256 * 1024;
    if (fa_map(&region, VM_WIMG_WCOMB) != KERN_SUCCESS) return -12;
    uint8_t *bounce = kalloc_data(chunk, Z_WAITOK);
    int rc = bounce ? 0 : -12;
    for (unsigned i = 0; !rc && i < image->mdt.count; ++i) {
        const QcomMdtSegment *s = &image->mdt.segment[i];
        uint64_t at = 0;
        if (!s->load) continue;
        if (qcom_mdt_place(&image->mdt, i, base, size, &at)) { rc = -7; break; }
        uint8_t *to = (uint8_t *)region.va + at;
        for (uint64_t done = 0; !rc && done < s->file_size;) {
            const size_t n = s->file_size - done < chunk ? (size_t)(s->file_size - done) : chunk;
            if (fuxi_adsp_fw_read(image->file, s->file_offset + done, bounce, n)) { rc = -5; break; }
            memcpy(to + done, bounce, n);
            done += n;
        }
        if (!rc && s->memory_size > s->file_size) bzero(to + s->file_size, (size_t)(s->memory_size - s->file_size));
    }
    __asm__ volatile("dsb sy" ::: "memory");
    if (bounce) kfree_data(bounce, chunk);
    fa_unmap(&region);
    return rc;
}

/* ---- events ---- */
static void serviceSmp2p(void)
{
    if (!gP2p.in) {
        volatile uint8_t *in = item(FA_ITEM_IN, NULL);
        if (!in) return;
        gP2p.in = (volatile uint32_t *)in;
    }
    uint32_t value = 0, changed = 0;
    if (qcom_smp2p_service(&gP2p, "slave-kernel", &value, &changed)) kickSmp2p();
    if (value != gSlave || !gSlaveKnown) printf("FUXI ADSP: slave-kernel %#x -> %#x\n", gSlave, value);
    gSlave = value;
    gSlaveKnown = 1;
    if (changed & value & FA_FATAL) readCrashReason();
    if ((value & FA_HANDOVER) && !gHandedOver) {
        gHandedOver = 1;
        vote(0, 0); /* the DSP holds its own votes now */
    }
}

static void applyClients(void);
static void pumpSends(void);
static void serviceGlink(void)
{
    if (!gGlinkStarted) return;
    if (!gGlink.rx.data) {
        uint32_t size = 0;
        volatile uint8_t *rx = item(FA_ITEM_GLINK_RX, &size);
        if (rx) qcom_glink_set_rx(&gGlink, rx, size);
    }
    const uint8_t agreed = gGlink.version_agreed;
    qcom_glink_poll(&gGlink);
    pumpSends();
    if (!agreed && gGlink.version_agreed) { stage(StGlink); printf("FUXI ADSP: glink version agreed\n"); }
    for (unsigned i = 0; i < gClientCount; ++i) {
        struct ShengAdspChannel *c = &gClients[i];
        const uint8_t up = c->glink && c->glink->up;
        if (up != c->was_up) {
            c->was_up = up;
            printf("FUXI ADSP: channel %s %s\n", c->name, up ? "up" : "down");
            if (up) stage(StChannel);
        }
    }
}
static void service(uint32_t bits)
{
    if (bits & EV_AOP) qcom_qmp_step(&gQmp);
    if (bits & (EV_SMP2P | EV_GLINK)) serviceSmp2p();
    if (bits & (EV_GLINK | EV_SEND)) { applyClients(); serviceGlink(); }
    if (bits) {
        uint64_t window;
        clock_interval_to_deadline(FA_FAST_MS, NSEC_PER_MSEC, &window);
        gFastUntil = window;
    }
}

/* ---- client glue (thread side) ---- */
static void clientState(void *context, QcomGlinkChannel *channel, int up)
{
    (void)channel;
    struct ShengAdspChannel *c = context;
    if (c->client.state) c->client.state(c->context, up);
}
static void clientReceive(void *context, QcomGlinkChannel *channel, const uint8_t *data, uint32_t length)
{
    (void)channel;
    struct ShengAdspChannel *c = context;
    if (c->client.receive) c->client.receive(c->context, data, length);
}
static void applyClients(void)
{
    if (!gGlinkStarted) return;
    const QcomGlinkClient glue = {clientState, clientReceive};
    /* Channels are only appended, never removed: entries below the published count are stable without the lock. A state callback
     * from qcom_glink_channel therefore runs without fa_client_lock held. */
    const uint32_t count = __atomic_load_n(&gClientCount, __ATOMIC_ACQUIRE);
    for (unsigned i = 0; i < count; ++i) {
        struct ShengAdspChannel *c = &gClients[i];
        if (!c->glink) c->glink = qcom_glink_channel(&gGlink, c->name, c->intent_size, c->intent_count, c->storage, &glue, c, c->initiator);
    }
}
static void pumpSends(void)
{
    const uint32_t count = __atomic_load_n(&gClientCount, __ATOMIC_ACQUIRE);
    for (unsigned i = 0; i < count; ++i) {
        struct ShengAdspChannel *c = &gClients[i];
        if (!c->glink || !c->glink->up) continue;
        for (;;) {
            lck_mtx_lock(&fa_client_lock);
            if (!c->count) { lck_mtx_unlock(&fa_client_lock); break; }
            struct fa_message *m = &c->queue[c->head];
            lck_mtx_unlock(&fa_client_lock);
            /* Only this thread takes messages off the queue, so m stays valid while the lock is dropped for the send. */
            if (qcom_glink_send(&gGlink, c->glink, m->data, m->length) != QcomGlinkOk) break;
            lck_mtx_lock(&fa_client_lock);
            c->head = (c->head + 1) % FA_QUEUE;
            c->count--;
            lck_mtx_unlock(&fa_client_lock);
        }
    }
}

/* ---- the bring-up ---- */
static int startDoorbells(void)
{
    /* Mask first: no handler here owns these level lines. */
    fa_gic_mask(FA_INTID_IPCC);
    fa_gic_mask(FA_INTID_RSC);
    if (fa_gic_enabled(FA_INTID_IPCC) || fa_gic_enabled(FA_INTID_RSC)) return -16;
    const uint32_t enable[3] = {qcom_ipcc_id(QCOM_IPCC_CLIENT_LPASS, QCOM_IPCC_SIGNAL_SMP2P),
        qcom_ipcc_id(QCOM_IPCC_CLIENT_LPASS, QCOM_IPCC_SIGNAL_GLINK_QMP), qcom_ipcc_id(QCOM_IPCC_CLIENT_AOP, QCOM_IPCC_SIGNAL_GLINK_QMP)};
    for (unsigned i = 0; i < 3; ++i) wr(&gIpcc, QCOM_IPCC_RECV_ENABLE, enable[i]);
    gIpccOn = 1;
    return 0;
}
/* The firmware partition may not be readable yet: a few tries with growing pauses (about two minutes in all). */
static int openFirmware(struct fa_image *dtb, struct fa_image *adsp)
{
    static const uint32_t pauses[] = {1, 2, 4, 8, 16, 30, 30, 30};
    for (unsigned i = 0;; ++i) {
        int rc = openImage(dtb, "adsp_dtb");
        if (!rc) {
            rc = openImage(adsp, "adsp");
            if (rc) release(dtb);
        }
        if (!rc) return 0;
        if ((rc != ENOENT && rc != ENXIO && rc != EIO) || i == sizeof(pauses) / sizeof(pauses[0])) return rc;
        IOSleep(pauses[i] * 1000);
    }
}
static void shutdownDsp(int dtb, int adsp)
{
    if (adsp) scm(QCOM_SCM_PAS_SHUTDOWN, qcom_scm_args(1, 0, 0, 0), FA_PAS_ADSP, 0, 0);
    if (dtb) scm(QCOM_SCM_PAS_SHUTDOWN, qcom_scm_args(1, 0, 0, 0), FA_PAS_DTB, 0, 0);
}
/* Runs events with fast polling until the AOSS mailbox reaches want or ms pass. */
static int qmpUntil(int want, uint32_t ms)
{
    uint64_t deadline;
    clock_interval_to_deadline(ms, NSEC_PER_MSEC, &deadline);
    while (qcom_qmp_step(&gQmp) != want) {
        if (gQmp.state < 0 || mach_absolute_time() >= deadline) return qcom_qmp_step(&gQmp) == want;
        service(waitEvents(10, 1));
    }
    return 1;
}
static int start(void)
{
    struct fa_image *dtb = &gDtbImage, *adsp = &gAdspImage;
    int rc = openFirmware(dtb, adsp);
    if (rc) { fail(StFirmware, rc); return 0; }
    printf("FUXI ADSP: firmware adsp %llu KiB, adsp_dtb %llu KiB, %u + %u segments\n", adsp->size >> 10, dtb->size >> 10,
        adsp->mdt.count, dtb->mdt.count);
    stage(StFirmware);
    if (gSlaveKnown && gSlave) { fail(StFirmware, -114); release(dtb); release(adsp); return 0; } /* running already: left alone */

    /* Our SMP2P item, before the DSP starts (it looks for it at boot). */
    if ((rc = allocItem(FA_ITEM_OUT, QCOM_SMP2P_ITEM_BYTES))) { fail(StSmp2p, rc); release(dtb); release(adsp); return 0; }
    qcom_smp2p_init_out(&gP2p, (volatile uint32_t *)item(FA_ITEM_OUT, NULL), 0, FA_REMOTE_PID);
    gStopEntry = qcom_smp2p_add_entry(&gP2p, "master-kernel");
    stage(StSmp2p);
    if ((rc = startDoorbells())) { fail(StDoorbells, rc); release(dtb); release(adsp); return 0; }
    kickSmp2p();
    stage(StDoorbells);

    qcom_qmp_init(&gQmp, (volatile uint32_t *)gAoss.va, 0x400, kickAop, NULL);
    for (int phase = 0; phase < 4 && qcom_qmp_step(&gQmp) > 0 && gQmp.state != QcomQmpOpen; ++phase) qmpUntil(QcomQmpOpen, 1000);
    if (gQmp.state != QcomQmpOpen) { fail(StLoadState, gQmp.state); release(dtb); release(adsp); return 0; }
    qcom_qmp_send(&gQmp, "{class: image, res: load_state, name: adsp, val: on}");
    if (!qmpUntil(QcomQmpOpen, 1000)) { qcom_qmp_abandon(&gQmp); fail(StLoadState, -60); release(dtb); release(adsp); return 0; }
    stage(StLoadState);
    printf("FUXI ADSP: aoss load_state adsp on\n");

    /* A copy of the command database, held only while the levels are looked up. */
    uint8_t *db = kalloc_data(FA_CMD_DB_SIZE, Z_WAITOK);
    uint32_t lcx = 0, lmx = 0;
    rc = !db || !readCmdDb(db) || qcom_cmd_db_find(db, FA_CMD_DB_SIZE, "lcx.lvl", &gLcx) ||
        qcom_cmd_db_find(db, FA_CMD_DB_SIZE, "lmx.lvl", &gLmx) || qcom_rpmh_level_index(&gLcx, 0xffffffffu, &lcx) ||
        qcom_rpmh_level_index(&gLmx, 0xffffffffu, &lmx);
    if (db) kfree_data(db, FA_CMD_DB_SIZE);
    if (rc) { fail(StRails, -2); release(dtb); release(adsp); return 0; }
    printf("FUXI ADSP: cmd-db lcx.lvl %#x (%u levels) lmx.lvl %#x (%u levels)\n", gLcx.address, gLcx.level_count, gLmx.address,
        gLmx.level_count);
    if ((rc = vote(lcx, lmx))) { fail(StRails, rc); release(dtb); release(adsp); return 0; }
    stage(StRails);

    /* The DSP's devicetree image first, as its own authenticated image, then the DSP image. */
    if ((rc = authenticateHeader(dtb, FA_PAS_DTB, FA_DTB_BASE)) || (rc = loadSegments(dtb, FA_DTB_BASE, FA_DTB_SIZE)) ||
        (rc = scmOk(scm(QCOM_SCM_PAS_AUTH_AND_RESET, qcom_scm_args(1, 0, 0, 0), FA_PAS_DTB, 0, 0)) ? 0 : -3)) {
        fail(StDtb, rc); vote(0, 0); release(dtb); release(adsp); return 0;
    }
    stage(StDtb);
    printf("FUXI ADSP: adsp_dtb authenticated and started\n");
    if ((rc = authenticateHeader(adsp, FA_PAS_ADSP, FA_ADSP_BASE)) || (rc = loadSegments(adsp, FA_ADSP_BASE, FA_ADSP_SIZE))) {
        fail(StLoaded, rc); shutdownDsp(1, 0); vote(0, 0); release(dtb); release(adsp); return 0;
    }
    stage(StLoaded);
    const uint64_t began = mach_absolute_time();
    if (!scmOk(scm(QCOM_SCM_PAS_AUTH_AND_RESET, qcom_scm_args(1, 0, 0, 0), FA_PAS_ADSP, 0, 0))) {
        fail(StStarted, -3); shutdownDsp(1, 0); vote(0, 0); release(dtb); release(adsp); return 0;
    }
    stage(StStarted);
    printf("FUXI ADSP: adsp authenticated and reset; waiting for ready\n");
    uint64_t deadline;
    clock_interval_to_deadline(5000, NSEC_PER_MSEC, &deadline);
    while (!(gSlave & (FA_READY | FA_FATAL)) && mach_absolute_time() < deadline) service(waitEvents(10, 1));
    uint64_t ns = 0;
    absolutetime_to_nanoseconds(mach_absolute_time() - began, &ns);
    release(dtb);
    release(adsp); /* the metadata may go once the images are authenticated */
    if (!(gSlave & FA_READY) || (gSlave & FA_FATAL)) {
        readCrashReason();
        fail(StReady, (gSlave & FA_FATAL) ? -5 : -60);
        shutdownDsp(1, 1);
        if (!gHandedOver) vote(0, 0);
        return 0;
    }
    stage(StReady);
    printf("FUXI ADSP: ready after %llu ms (slave-kernel %#x)\n", ns / 1000000u, gSlave);
    return 1;
}

static int startGlink(void)
{
    int rc = allocItem(FA_ITEM_GLINK_DESC, 32);
    if (!rc) rc = allocItem(FA_ITEM_GLINK_TX, FA_GLINK_FIFO);
    uint32_t size = 0;
    volatile uint8_t *desc = item(FA_ITEM_GLINK_DESC, NULL), *txf = item(FA_ITEM_GLINK_TX, &size);
    if (rc || !desc || !txf) { fail(StGlink, rc ? rc : -2); return 0; }
    volatile uint32_t *d = (volatile uint32_t *)desc;
    /* Descriptor words: our FIFO's tail and head, then the DSP's FIFO's tail and head. */
    const QcomGlinkFifo tx = {&d[1], &d[0], txf, size}, rx = {&d[3], &d[2], NULL, 0};
    qcom_glink_init(&gGlink, tx, rx, kickGlink, NULL);
    gGlinkStarted = 1;
    applyClients();
    serviceGlink();
    return 1;
}

static void report(void)
{
    printf("FUXI ADSP: stage=%d ipcc=%u other=%u glink in=%u out=%u unknown_opens=%u (%s) qrtr stage=%d in=%d out=%d "
        "locator=%d/%d dsp_node=%d services=%d\n", gStage, gIpccEvents, gIpccOther, gGlink.rx_messages, gGlink.tx_messages,
        gGlink.unknown_opens, gGlink.unknown_names, native_qrtr_stats[0], native_qrtr_stats[1], native_qrtr_stats[4],
        native_qrtr_stats[6], native_qrtr_stats[7], native_qrtr_stats[8], native_qrtr_stats[9]);
    if (native_qrtr_stats[9] > 0) {
        printf("FUXI ADSP QRTR: services=%d [%s]\n", native_qrtr_stats[9], native_qrtr_text);
    }
}

static void adspThread(void *arg, wait_result_t result)
{
    (void)result;
    const unsigned mode = (unsigned)(uintptr_t)arg;
    if (fa_map(&gSmemW, VM_WIMG_IO) != KERN_SUCCESS || fa_map(&gTcsr, VM_WIMG_IO) != KERN_SUCCESS ||
        fa_map(&gIpcc, VM_WIMG_IO) != KERN_SUCCESS || fa_map(&gAoss, VM_WIMG_IO) != KERN_SUCCESS ||
        fa_map(&gRsc, VM_WIMG_IO) != KERN_SUCCESS || fa_map(&gGicd, VM_WIMG_IO) != KERN_SUCCESS) {
        fail(StMapped, -12);
        thread_terminate(current_thread());
        return;
    }
    gSmem.base = (volatile uint8_t *)gSmemW.va;
    gSmem.size = FA_SMEM_SIZE;
    gLayout = qcom_rsc_layout(rd(&gRsc, 0));
    stage(StMapped);
    survey();
    stage(StSurveyed);
    if (mode >= 2 && start() && startGlink()) {
        /* From here on the thread answers doorbells and client sends. Fast polling while traffic is recent, else 200 ms. */
        uint64_t next_report;
        clock_interval_to_deadline(30000, NSEC_PER_MSEC, &next_report);
        unsigned reports = 0;
        for (;;) {
            const int fast = mach_absolute_time() < gFastUntil;
            int pending = 0;
            const uint32_t count = __atomic_load_n(&gClientCount, __ATOMIC_ACQUIRE);
            for (unsigned i = 0; i < count; ++i) pending |= gClients[i].count != 0;
            service(waitEvents(fast || pending ? 50 : 200, fast || pending ? 2 : 200) | (pending ? EV_SEND : 0));
            fuxi_audio_spf_poll();
            if (mach_absolute_time() >= next_report) {
                report();
                clock_interval_to_deadline(++reports < 10 ? 30000u : 600000u, NSEC_PER_MSEC, &next_report);
            }
        }
    }
    report();
    thread_terminate(current_thread());
}

ShengAdspChannel *sheng_adsp_channel(const char *name, uint32_t intent_size, uint32_t intent_count, int initiator,
    const ShengAdspClient *client, void *context)
{
    if (!gStarted || !name || !client || !intent_size || !intent_count || intent_count > QCOM_GLINK_MAX_LOCAL_INTENTS) return NULL;
    const vm_size_t storage_bytes = (vm_size_t)intent_size * intent_count, queue_bytes = sizeof(struct fa_message) * FA_QUEUE;
    uint8_t *storage = kalloc_data(storage_bytes, Z_WAITOK | Z_ZERO);
    struct fa_message *queue = kalloc_data(queue_bytes, Z_WAITOK | Z_ZERO);
    struct ShengAdspChannel *c = NULL;
    lck_mtx_lock(&fa_client_lock);
    if (gClientCount < FA_CLIENTS && storage && queue) {
        c = &gClients[gClientCount];
        bzero(c, sizeof(*c));
        strlcpy(c->name, name, sizeof(c->name));
        c->intent_size = intent_size; c->intent_count = intent_count; c->initiator = initiator;
        c->client = *client; c->context = context; c->storage = storage; c->queue = queue;
        __atomic_store_n(&gClientCount, gClientCount + 1, __ATOMIC_RELEASE);
    }
    lck_mtx_unlock(&fa_client_lock);
    if (!c) {
        if (storage) kfree_data(storage, storage_bytes);
        if (queue) kfree_data(queue, queue_bytes);
        return NULL;
    }
    wake(EV_SEND);
    return c;
}

int sheng_adsp_send(ShengAdspChannel *c, const void *data, uint32_t length)
{
    if (!c || !data || !length) return -2;
    if (length > SHENG_ADSP_MAX_MESSAGE) return -1;
    lck_mtx_lock(&fa_client_lock);
    if (c->count == FA_QUEUE) { lck_mtx_unlock(&fa_client_lock); return -1; }
    struct fa_message *m = &c->queue[(c->head + c->count) % FA_QUEUE];
    memcpy(m->data, data, length);
    m->length = length;
    c->count++;
    lck_mtx_unlock(&fa_client_lock);
    wake(EV_SEND);
    return 0;
}

/* Boot argument fuxiadsp, else ADT /chosen u32 "fuxi,adsp" (the command line is nearly full). */
static unsigned fa_mode(void)
{
    unsigned mode = 0;
    DTEntry chosen;
    const void *prop;
    unsigned bytes;
    if (PE_parse_boot_argn("fuxiadsp", &mode, sizeof(mode))) return mode;
    if (SecureDTLookupEntry(NULL, "/chosen", &chosen) == kSuccess &&
        SecureDTGetProperty(chosen, "fuxi,adsp", &prop, &bytes) == kSuccess && bytes == sizeof(uint32_t))
        memcpy(&mode, prop, sizeof(mode));
    return mode;
}

/* ADT /chosen u32 "fuxi,adsp-wlan" = 1 (test loaders, for the combined hardware validation): 2 and 3 run alongside fuxiwlan, the
 * two sharing the apps RSC through fuxi_rpmh_shared's mutex. Any other value or none keeps the guard. */
static int fa_with_wlan(void)
{
    uint32_t value = 0;
    DTEntry chosen;
    const void *prop;
    unsigned bytes;
    if (SecureDTLookupEntry(NULL, "/chosen", &chosen) == kSuccess &&
        SecureDTGetProperty(chosen, "fuxi,adsp-wlan", &prop, &bytes) == kSuccess && bytes == sizeof(uint32_t))
        memcpy(&value, prop, sizeof(value));
    return value == 1;
}

/* The mode that runs: 0 off, 1 survey, 2 start the DSP and log the battery manager, 3 also feed SweetBattery. 2 and 3 fall back to
 * 1 with fuxiwlan until combined hardware validation, unless fuxi,adsp-wlan opts in. Read again on every call, so SweetBattery
 * can ask before or after fuxi_adsp_init ran. */
unsigned fuxi_adsp_mode(void);
unsigned fuxi_adsp_mode(void)
{
    char wlan[8];
    const unsigned mode = fa_mode();
    if (!mode) return 0;
    if (mode >= 2 && PE_parse_boot_argn("fuxiwlan", wlan, sizeof(wlan)) && !fa_with_wlan()) return 1;
    return mode > 3 ? 3 : mode;
}

/* fuxi,adsp=4 (test loaders only): runs as 3, and the boot tour may ask fuxi_adsp_battery.c for a staged "unplugged, 57%" reading
 * to check that iOS's battery display follows a published change (fuxi_adsp_battery_test_unplug). */
unsigned fuxi_adsp_battery_test_mode(void);
unsigned fuxi_adsp_battery_test_mode(void)
{
    return fa_mode() == 4 && fuxi_adsp_mode() == 3;
}

/* Called once from sweet_ufs.c after the UFS driver is ready (the firmware is read through it). */
void fuxi_adsp_init(void)
{
    const unsigned asked = fa_mode(), run = fuxi_adsp_mode();
    thread_t thread = NULL;
    fuxi_pmic_init();
    fuxi_ramtest_init();
    fuxi_bt_init();
    fuxi_amp_init(); /* the speaker amplifiers' own switch (fuxi_amp.c, fuxiamp / ADT fuxi,amp), independent of fuxiadsp */
    if (!run || gStarted) return;
    if (asked >= 2 && run < 2) {
        printf("FUXI ADSP: fuxiadsp=%u refused with fuxiwlan (combined hardware validation pending); surveying only\n", asked);
    } else if (run >= 2 && fa_with_wlan()) {
        char wlan[8];
        if (PE_parse_boot_argn("fuxiwlan", wlan, sizeof(wlan)))
            printf("FUXI ADSP: fuxiadsp=%u alongside fuxiwlan (fuxi,adsp-wlan=1: combined test, shared RSC mutex)\n", run);
    }
    gStarted = 1;
    if (run >= 2) {
        sheng_qrtr_start();               /* the router link registers before the DSP can open it */
        fuxi_adsp_battery_start(run >= 3); /* PMIC_RTR_ADSP_APPS */
        fuxi_audio_spf_start();
    }
    if (kernel_thread_start(adspThread, (void *)(uintptr_t)run, &thread) != KERN_SUCCESS) {
        fail(StMapped, -7);
        return;
    }
    thread_deallocate(thread);
    printf("FUXI ADSP: started mode %u\n", run);
}
#endif /* FUXI */
