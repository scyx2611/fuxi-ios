/* SPDX-License-Identifier: MIT */
/* FUXI 基頻（MPSS，SM8550 kalama-modem-pas）擁有者。開機參數 fuximodem，或 ADT /chosen u32 "fuxi,modem"（命令列已滿時）：
 *   0  關（預設）。
 *   1  唯讀探測：SMEM 的 APPS↔MPSS 分區與條目、modem 是否已在跑（slave-kernel）、遺言（SMEM 421）、modem_a 上的
 *      modem／modem_dtb 韌體是否齊全且放得進保留區、PAS 4／0x26 是否支援、EFS 四個分區、cmd-db 的 mss／cx。什麼都不寫。
 *   2  再開機：SMP2P 出條目、AOSS load_state modem、mss 代理投票、DSM 區指派給 MSS_MSA、modem_dtb 與 modem 經 PAS
 *      驗證啟動、等 ready，handover 後撤 mss 票。之後只輪詢 SMP2P，記錄狀態與遺言。
 *   3  再加 GLINK（SMEM 478/479/480）上的 IPCRTR、QRTR（記錄 modem 公告的服務、回答它的查詢）、服務定位（0x40）與
 *      rmtfs（服務 14；EFS 分區開機時整份讀進記憶體，寫入只留在記憶體）。
 * 其他 ADT 開關（u32，預設 0）：fuxi,modem-cx=1 另投 cx 最高檔（cx 全晶片共用，投了就不撤，只供測試）；
 * fuxi,modem-nodsm=1 不做 DSM 指派；fuxi,modem-open=1 由我們主動開 IPCRTR（預設等 modem 開）。
 *
 * 寫法照 fuxi_adsp.c（同一套 SMEM／SMP2P／QMP／RPMh／PAS／GLINK 模組，見 xnu/fuxi-adsp，UniXNU-sheng 6880068，
 * BSD-2-Clause-Patent）。與 ADSP 共存的規則：
 *   - IPCC 接收由 ADSP 執行緒獨占（它的 ipccPoll 會清掉看到的訊號）。這裡不打開 MPSS 的接收訊號，SMP2P 與 GLINK 純輪詢
 *     SMEM；只寫 IPCC SEND 叫 modem。GIC 的 IPCC 線（SPI 229）照 ADSP 的做法先遮罩。
 *   - AOSS QMP 信箱共用：ADSP 開機時開好的通道照用（QcomQmp 會沿用），送之前信箱要是空的；SMEM 配置用 TCSR 硬體鎖，
 *     而兩個應用處理器執行緒寫的是同一個鎖值，所以 modem 開機排在 ADSP ready（看 ADSP 的 SMP2P slave-kernel）之後。
 *   - RPMh 經 fuxi_rpmh_shared 的鎖，TCS 1 閒置時才用（與 ADSP 的 vote() 同法）。
 * 參考：Linux drivers/remoteproc/qcom_q6v5_pas.c sm8550_mpss_resource（pas 4、dtb pas 0x26、crash 421、load_state modem、
 * cx／mss、region_assign 第 2 區給 MSS_MSA）；HakiOS hakios.mpss.c（SM7325，APSL 2.0）的 rmtfs 與 MP_ASSIGN 版面（只取事實）；
 * 裝置樹事實來自本機 live DTB（work/camera-modem-survey/fuxi-live-20261116.dts）。全部位址都在 XNU RAM（0x8c0000000 起）之外。 */
#include <pexpert/arm64/board_config.h>
#if defined(FUXI)
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <kern/kalloc.h>
#include <kern/locks.h>
#include <libkern/libkern.h>
#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_ether.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/kpi_interface.h>
#include <netinet/in.h>
#include <sys/kern_event.h>
#include <sys/kpi_mbuf.h>
#include <sys/sockio.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include <pexpert/device_tree.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>
#include <arm/caches_internal.h>
#include "QcomGlink.h"
#include "QcomMdt.h"
#include "QcomQmp.h"
#include "QcomQrtr.h"
#include "QcomRpmh.h"
#include "QcomScm.h"
#include "QcomSmem.h"
#include "QcomSmp2p.h"
#include "fuxi_rpmh_shared.h"
#include "fuxi_modem_proto.h"
#include "QcomBaseband.h"

extern ppnum_t pmap_find_phys(pmap_t, addr64_t);
extern kern_return_t pmap_enter_options_addr(pmap_t, vm_map_offset_t, uint64_t, vm_prot_t, vm_prot_t, unsigned, boolean_t,
    unsigned, void *);
extern kern_return_t kernel_thread_start(thread_continue_t, void *, thread_t *);
extern kern_return_t thread_terminate(thread_t);
extern void thread_deallocate(thread_t);
extern void IOSleep(unsigned milliseconds);
extern int fuxi_modem_fw_open(const char *name, void **handle, uint64_t *size);
extern int fuxi_modem_fw_read(void *handle, uint64_t offset, void *to, size_t length);
extern void fuxi_modem_fw_close(void *handle);
extern int fuxi_modem_fw_read_file(const char *path, uint8_t **out_data, uint64_t *out_size, uint64_t max_bytes);
extern void fuxi_modem_fw_free_file(uint8_t *data, uint64_t size);
extern int fuxi_modem_fw_stat(const char *path, uint64_t *out_size);
extern int fuxi_ufs_part_find(const char *name, uint32_t *lun, uint64_t *first, uint64_t *blocks);
extern int fuxi_ufs_part_read(uint32_t lun, uint64_t lba, uint32_t blocks, void *dst);
extern unsigned fuxi_adsp_mode(void);
extern void proc_name(int pid, char *buf, int size);
void fuxi_modem_init(void);
int fuxi_modem_sandbox_check(int pid, user_addr_t arg);

enum Stage {
    StMapped = 1, StSurveyed = 2, StAdspWait = 3, StFirmware = 4, StSmp2p = 5, StLoadState = 6, StRails = 7, StDsm = 8, StDtb = 9,
    StLoaded = 10, StStarted = 11, StReady = 12, StGlink = 13, StChannel = 14, StHello = 15, StRmtfs = 16,
};
#define FM_SMEM 0x81d00000ull
#define FM_SMEM_SIZE 0x200000u
#define FM_TCSR 0x1f40000ull /* syscon；mutex 3 在 +0x3000 */
#define FM_MUTEX_OFF 0x3000u
#define FM_IPCC 0x408000ull
#define FM_AOSS 0xc300000ull
#define FM_RSC 0x17a20000ull
#define FM_RSC_SIZE 0x10000u
#define FM_RSC_TCS 0xd00u
#define FM_CMD_DB 0x81c60000ull
#define FM_CMD_DB_SIZE 0x20000u
#define FM_GICD 0x17100000ull
#define FM_INTID_IPCC (32u + 229u)
#define FM_MPSS_BASE 0x8a800000ull /* mpss_region */
#define FM_MPSS_SIZE 0x10800000ull
#define FM_DTB_BASE 0x9b000000ull /* q6_mpss_dtb_region */
#define FM_DTB_SIZE 0x80000ull
#define FM_IPA_FW_BASE 0x9b080000ull /* ipa_fw_region (0x10000) + ipa_gsi_region (0xa000) */
#define FM_IPA_FW_SIZE 0x1a000ull
#define FM_IPA_FW_SAFE_END 0x9b098000ull /* 避開 0x9b09a000 gpu_micro_code_region 的 16 KiB 頁 */
#define FM_DSM_BASE 0xd4d00000ull /* mpss_dsm_region（Linux region_assign_idx 2） */
#define FM_DSM_SIZE 0x3300000ull
#define FM_RMTFS_BASE 0x9c900000ull /* cdsp_region 開頭：這個移植從不啟動 CDSP；qcom,rmtfs_sharedmem 是動態配置、大小 0x280000 */
#define FM_RMTFS_SIZE 0x280000u
/* PAS metadata 的實體頁。小米 fuxi-t-oss drivers/remoteproc/qcom_q6v5_pas.c 的 kalama_mpss_resource 設 dma_phys_below_32b
 * （metadata 從 4 GiB 以下的 CMA 配）與 free_after_auth_reset；ADSP 沒有這個要求，所以 ADSP 用一般頁能過、基頻不行
 * （2026-11-20 趟：modem_dtb init_image 回 0xffcfffbb）。這個移植的 XNU RAM 從 0x8c0000000 起，kmem_alloc_contig 只給得出
 * 4 GiB 以上的頁，改用 cdsp_region（0x9c900000 + 32 MiB，HLOS 擁有、從不啟動 CDSP）裡 rmtfs 之後的固定格：modem_dtb 一格、
 * modem 一格，各自保留到該映像 auth_and_reset 之後才收回對映。 */
#define FM_META_BASE 0x9cc00000ull
#define FM_META_SLOT 0x10000u
#define FM_CDSP_END 0x9e900000ull
_Static_assert(FM_META_BASE >= FM_RMTFS_BASE + FM_RMTFS_SIZE && FM_META_BASE + 3u * FM_META_SLOT <= FM_CDSP_END &&
    FM_META_BASE + 3u * FM_META_SLOT <= 0x100000000ull, "metadata slots must stay in cdsp_region below 4 GiB");
#define FM_PAS_MODEM 4u
#define FM_PAS_DTB 0x26u
#define FM_PAS_IPA 0x0fu
#define FM_REMOTE_PID 1u
#define FM_ITEM_IN 435u
#define FM_ITEM_OUT 428u
#define FM_ITEM_CRASH 421u
#define FM_ITEM_GLINK_DESC 478u
#define FM_ITEM_GLINK_TX 479u
#define FM_ITEM_GLINK_RX 480u
#define FM_ITEM_IPA_FILTER 497u
#define FM_IPA_SMEM_SIZE 0xb000u
#define FM_GLINK_FIFO 0x4000u
#define FM_ADSP_PID 2u
#define FM_ADSP_ITEM_IN 443u
#define FM_IPCC_CLIENT_MPSS 2u
#define FM_FATAL (1u << 0)
#define FM_READY (1u << 1)
#define FM_HANDOVER (1u << 2)
#define FM_SCM_SVC_MP 0x0cu
#define FM_SCM_MP_ASSIGN 0x16u
#define FM_INTENT_SIZE 0x4400u /* DTB qcom,intents 最大的一種 */
#define FM_INTENT_COUNT 8u
#define FM_QUEUE 64u
#define FM_QUEUE_BYTES 4128u /* QRTR 標頭 32 + 最大負載 4096（QMI／TFTP），四位元組對齊 */
#define FM_FAST_MS 500u

struct fm_window { uint64_t pa; vm_size_t size; vm_offset_t va; };
static struct fm_window gSmemW = {FM_SMEM, FM_SMEM_SIZE, 0}, gTcsr = {FM_TCSR, 0x4000, 0}, gIpcc = {FM_IPCC, 0x4000, 0},
    gAoss = {FM_AOSS, 0x4000, 0}, gRsc = {FM_RSC, FM_RSC_SIZE, 0}, gGicd = {FM_GICD, 0x4000, 0}, gRmtfsW = {FM_RMTFS_BASE, FM_RMTFS_SIZE, 0};
static QcomSmem gSmem;
static QcomSmemPartition gPart;
static int gPartOk;
static QcomSmp2p gP2p;
static QcomQmp gQmp;
static QcomRscLayout gLayout;
static QcomRpmhResource gMss, gCx;
static uint32_t gBcmIp0, gBcmSn8, gBcmSh0, gBcmMc0, gBcmAcv;
static int gStarted, gStage, gError;
static unsigned gMode, gIpaMode, gBbMode;
static uint32_t gSlave, gSlaveKnown, gHandedOver, gVoted;
static uint32_t gIpaInVal, gIpaInKnown;
static int64_t gScmX0;
static uint64_t gScmX1;
static uint64_t gFastUntil;
static uint64_t gBegan;

/* GLINK／QRTR／rmtfs（模式 3） */
static QcomGlink gGlink;
static QcomGlinkChannel *gIpcrtr;
static int gGlinkStarted;
static uint8_t *gIntents;
static QcomQrtr gQrtr;
static FmRmtfs gRmtfs;
static struct { uint32_t length; uint8_t data[FM_QUEUE_BYTES]; } gQueue[FM_QUEUE];
static uint32_t gQueueHead, gQueueCount, gQueueDropped;
static uint32_t gLookups, gLookupAnswers, gServloc, gServers, gLinkUps;
static char gServerText[200];
static unsigned gServerTextAt, gServerLines;

/* QMI 用戶端探測（fuxi,modem-qmi，預設關） */
#define FM_QMI_PORT 0x4001u
#define FM_QMI_IND_LOG_LIMIT 16u
#define FM_QMI_BAD_LOG_LIMIT 8u
struct fm_qmi_target { uint32_t service, node, port; uint8_t found; };
static struct fm_qmi_target gQmiTargets[8] = {
    {FM_QMI_DMS, 0, 0, 0}, {FM_QMI_NAS, 0, 0, 0}, {FM_QMI_WMS, 0, 0, 0}, {FM_QMI_UIM, 0, 0, 0},
    {FM_QMI_WDS, 0, 0, 0}, {FM_QMI_WDA, 0, 0, 0}, {FM_QMI_DPM, 0, 0, 0},
    {FM_QMI_IPA, 0, 0, 0},
};
static unsigned gQmiMode;
static uint16_t gQmiTxn;
static uint32_t gQmiTx, gQmiRx, gQmiInd, gQmiBad, gQmiProbes, gQmiIndLogs, gQmiBadLogs;
static uint64_t gQmiNextProbe;
static uint8_t gQmiRevisionSent;
/* DMS 上線狀態機（對照 hakios.mpss.c dms_online）：0 閒置、1 已直接要求 online、2 已要求 low-power、3 等 2s 後送 online、
 * 4 已於 low-power 後要求 online、5 完成。 */
static uint8_t gDmsState, gDmsReqMode, gDmsAttempts;
static uint64_t gDmsActionAt;
/* M5 UIM / DPM / WDA / WDS 狀態機（fuxi,modem-qmi >= 3） */
static uint8_t gUimSwitched, gUimProvisioned;
static uint8_t gNasRegistered, gNasPsAttached;
static uint8_t gDpmSent, gDpmDone, gDpmOk;
static uint8_t gWdaSent, gWdaDone, gWdaOk;
static uint8_t gWdsBindSent, gWdsBindDone, gWdsBindOk;
static uint8_t gWdsIpFamSent, gWdsIpFamDone;
static uint8_t gWdsStartPending, gWdsUp, gWdsStartAttempts;
static uint32_t gWdsHandle;
static uint64_t gDataSetupBegan, gWdsNextTry;
/* M7 QMI WMS 0x05 簡訊探測狀態（fuxi,modem-qmi >= 4；只記中繼資料，絕不輸出內容或門號） */
static uint8_t gWmsEventSent, gWmsEventDone, gWmsEventOk;
static uint8_t gWmsRoutesSent, gWmsRoutesDone;
static uint16_t gWmsRouteCount;
static uint8_t gWmsStoreSentMask, gWmsStoreDoneMask;
static uint32_t gWmsStoreMax[2], gWmsStoreFree[2];
static uint8_t gWmsListSentMask, gWmsListDoneMask;
static uint32_t gWmsListCount[2];
static uint16_t gWmsTxnUimStore, gWmsTxnNvStore, gWmsTxnUimList, gWmsTxnNvList;
static uint32_t gWmsErrs, gWmsIndMt, gWmsIndRoute;

/* M6/M7 IPA v5.5 / GSI v5.0 / QMI IPA 0x31 / en1 資料面（fuxi,modem-ipa 0x200/0x400/0x800/0x1000，預設關） */
#define FM_IPA_HOST_PORT 4u
#define FM_IPA_CLIENT_PORT 0x4002u
#define FM_GSI_RING_COUNT 64u
#define FM_GSI_RING_BYTES (FM_GSI_RING_COUNT * FM_GSI_RING_ELEM_SIZE)
#define FM_GSI_RX_BUF_SIZE 8192u
#define FM_GSI_RX_BUFS 8u
#define FM_GSI_TX_BUF_SIZE 2048u
#define FM_GSI_TX_BUFS 16u
#define FM_CELL_FRAME_MAX 1518u
struct fm_gsi_chan {
    uint32_t ch_id, ev_id, ep_id;
    uint8_t toward_ipa, is_cmd, started;
    uint16_t max_tre;
    vm_offset_t tre_va, ev_va, rx_va;
    uint64_t tre_pa, ev_pa, rx_pa;
    uint32_t tre_idx, ev_idx;
    uint32_t rx_events, rx_bytes, rx_errs;
};
static struct fm_window gIpaReg = {FM_IPA5_REG_PA, FM_IPA5_REG_SIZE, 0};
static struct fm_window gIpaGsi = {FM_GSI5_MAP_PA, FM_GSI5_MAP_SIZE, 0};
static uint8_t gIpaRegMapped, gIpaGsiMapped, gIpaHwConfigured, gIpaGsiReady;
static uint32_t gIpaMemOffset, gIpaMemSize, gIpaCanariesWritten, gIpaCmdDone, gIpaCmdErrs;
static uint32_t gIpaSmemPhys, gIpaSmemSize;
static vm_offset_t gIpaPoolVa, gIpaLanRxVa, gIpaModemRxVa, gIpaModemTxVa;
static uint64_t gIpaPoolPa, gIpaLanRxPa, gIpaModemRxPa, gIpaModemTxPa;
static struct fm_gsi_chan gIpaChans[4];
static uint8_t gIpaInitSent, gIpaInitRsp, gIpaInitOk, gIpaIndReg, gIpaUcReady, gIpaIndSent;
static uint32_t gIpaIndNode, gIpaIndPort;
static uint32_t gIpaFilterReqs, gIpaFilterRules, gIpaOtherReqs, gIpaQmiBad;
static LCK_GRP_DECLARE(gCellLckGrp, "fuxi-modem-cell");
static LCK_MTX_DECLARE(gIpaTxLock, &gCellLckGrp);
static LCK_MTX_DECLARE(gCellSessLock, &gCellLckGrp);
static const uint8_t kCellMac[6] = {0x02u, 0x66u, 0x75u, 0x78u, 0x69u, 0x01u};
static const uint8_t kCellRouterMac[6] = {0x02u, 0x66u, 0x75u, 0x78u, 0x69u, 0xfeu};
static ifnet_t gCellIfp;
static uint8_t gCellAttached, gCellAttachFailed, gCellLinkUp, gCellActiveHint;
static FmCellSession gCellSession;
static uint32_t gIpaTxSubmitted, gIpaTxCompleted, gIpaTxBytes, gIpaTxDrops, gIpaTxErrs;
static uint32_t gCellArpReplies, gCellDhcpReplies, gCellTxIpv4, gCellTxDrop, gCellTxNonIpv4;
static uint32_t gCellRxIpv4, gCellRxDrop, gCellRxQmapCmds, gCellRxQmapBad, gCellTxQmapAcks;

/* M8a 原生基頻橋接（QcomBaseband / FuxiBaseband.cpp，fuxi,modem-bb，預設關） */
static uint8_t gBbPktBuf[FM_QUEUE_BYTES];
static void (*gBbDataRx)(uint32_t, const uint8_t *, uint32_t, uint32_t);
static uint32_t gBbTxPackets, gBbTxDrops, gBbRxPackets, gBbRxDrops;

/* TFTP over QRTR（fuxi,modem-tftp，預設關；唯讀提供 modem_a /image，寫入只留 RAM 不碰儲存裝置） */
#define FM_TFTP_PORT 3u
#define FM_TFTP_XFER_BASE 0x4080u /* 避開 QcomBaseband CBQ_PORT_BASE 0x4100..0x417f（CBQ_MAX_CLIENTS 128） */
#define FM_TFTP_SLOTS 4u
#define FM_TFTP_MEM_SLOTS 24u
#define FM_TFTP_WRITE_MAX (1u << 20)
#define FM_TFTP_READ_MAX (8u << 20)
#define FM_TFTP_IDLE_MS 10000u
#define FM_TFTP_LOG_LIMIT 80u
struct fm_tftp_xfer {
    uint8_t active, writing, borrowed, eof_sent;
    uint32_t local_port, node, peer;
    uint8_t *data;
    uint64_t size;
    uint32_t blksize, wsize, rsize, seek;
    uint32_t sent, write_pos, written_size, expected, retries;
    uint64_t last_active;
    char name[128];
};
struct fm_tftp_mem {
    char name[128];
    uint8_t *data;
    uint32_t size;
};
static unsigned gTftpMode;
static struct fm_tftp_xfer gTftpXfers[FM_TFTP_SLOTS];
static struct fm_tftp_mem gTftpMem[FM_TFTP_MEM_SLOTS];
static uint32_t gTftpRequests, gTftpServed, gTftpRefused, gTftpWrites, gTftpDiagLogs;
static uint8_t gTftpPktBuf[FM_QUEUE_BYTES];
static uint8_t gTftpDataBuf[4u + FM_TFTP_MAX_BLOCK];

/* ---- MMIO ---- */
static uint32_t rd(const struct fm_window *w, uint64_t at) { return *(volatile uint32_t *)(w->va + at); }
static void wr(const struct fm_window *w, uint64_t at, uint32_t value)
{
    *(volatile uint32_t *)(w->va + at) = value;
    __asm__ volatile("dsb sy" ::: "memory");
}
static kern_return_t fm_map(struct fm_window *w, unsigned wimg)
{
    vm_offset_t va = 0;
    if (w->va) return KERN_SUCCESS;
    if (!w->size || (w->pa & PAGE_MASK) || (w->size & PAGE_MASK)) return KERN_INVALID_ARGUMENT;
    kern_return_t kr = kmem_alloc(kernel_map, &va, w->size, KMA_VAONLY | KMA_KOBJECT | KMA_NOPAGEWAIT, VM_KERN_MEMORY_IOKIT);
    if (kr != KERN_SUCCESS) return kr;
    for (vm_size_t off = 0; off < w->size; off += PAGE_SIZE) {
        kr = pmap_enter_options_addr(kernel_pmap, va + off, w->pa + off, VM_PROT_READ | VM_PROT_WRITE, VM_PROT_NONE, wimg, TRUE,
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
/* 頁表與 VA 一起收回（交給安全世界的保留區不能留著可能被推測讀取的對映）。 */
static void fm_unmap(struct fm_window *w)
{
    if (!w->va) return;
    pmap_remove(kernel_pmap, w->va, w->va + w->size);
    kmem_free(kernel_map, w->va, w->size);
    w->va = 0;
}

static void stage(int value) { gStage = value; }
static void fail(int step, int error)
{
    gStage = -step;
    gError = error;
    printf("FUXI MODEM: failed at stage %d error %d (scm x0=%lld x1=%#llx)\n", step, error, (long long)gScmX0,
        (unsigned long long)gScmX1);
}
static uint32_t msSince(uint64_t began)
{
    uint64_t ns = 0;
    absolutetime_to_nanoseconds(mach_absolute_time() - began, &ns);
    return (uint32_t)(ns / 1000000u);
}

/* ---- ADT／開機參數 ---- */
static uint32_t adtU32(const char *name, uint32_t fallback)
{
    DTEntry chosen;
    const void *prop;
    unsigned bytes;
    uint32_t value = fallback;
    if (SecureDTLookupEntry(NULL, "/chosen", &chosen) == kSuccess && SecureDTGetProperty(chosen, name, &prop, &bytes) == kSuccess &&
        bytes == sizeof(uint32_t))
        memcpy(&value, prop, sizeof(value));
    return value;
}
static unsigned fm_mode(void)
{
    unsigned mode = 0;
    if (PE_parse_boot_argn("fuximodem", &mode, sizeof(mode))) return mode > 3 ? 3 : mode;
    mode = adtU32("fuxi,modem", 0);
    return mode > 3 ? 3 : mode;
}

/* ---- 門鈴：只送不收 ---- */
static void ring(uint32_t signal) { wr(&gIpcc, QCOM_IPCC_SEND, qcom_ipcc_id(FM_IPCC_CLIENT_MPSS, signal)); }
static void kickAop(void *context) { (void)context; wr(&gIpcc, QCOM_IPCC_SEND, qcom_ipcc_id(QCOM_IPCC_CLIENT_AOP, QCOM_IPCC_SIGNAL_GLINK_QMP)); }
static void kickGlink(void *context) { (void)context; ring(QCOM_IPCC_SIGNAL_GLINK_QMP); }
static void kickSmp2p(void) { ring(QCOM_IPCC_SIGNAL_SMP2P); }
static int gicEnabled(unsigned intid) { return (rd(&gGicd, 0x100 + (intid / 32) * 4) >> (intid % 32)) & 1; }
static void gicMask(unsigned intid)
{
    wr(&gGicd, 0x180 + (intid / 32) * 4, 1u << (intid % 32));
    for (unsigned n = 0; (rd(&gGicd, 0) & 0x80000000u) && n < 100000; n++) delay(1);
}

/* ---- SMEM ---- */
static int lockSmem(void)
{
    for (unsigned i = 0; i < 1000; ++i) {
        wr(&gTcsr, FM_MUTEX_OFF, 1);
        if (rd(&gTcsr, FM_MUTEX_OFF) == 1) return 1;
        delay(10);
    }
    return 0;
}
static void unlockSmem(void) { wr(&gTcsr, FM_MUTEX_OFF, 0); }
static int allocItem(uint16_t id, uint32_t size)
{
    if (!lockSmem()) return -16;
    const int rc = qcom_smem_alloc(&gSmem, &gPart, id, size);
    unlockSmem();
    return rc == QcomSmemExists ? 0 : rc;
}
static volatile uint8_t *itemIn(const QcomSmemPartition *part, uint16_t id, uint32_t *size)
{
    uint32_t at = 0, bytes = 0;
    if (qcom_smem_find(&gSmem, part, id, &at, &bytes)) return NULL;
    if (size) *size = bytes;
    return (volatile uint8_t *)gSmemW.va + at;
}
static volatile uint8_t *item(uint16_t id, uint32_t *size) { return gPartOk ? itemIn(&gPart, id, size) : NULL; }
static void readCrashReason(void)
{
    QcomSmemPartition global;
    uint32_t at = 0, size = 0;
    if (qcom_smem_partition(&gSmem, QCOM_SMEM_HOST_GLOBAL, QCOM_SMEM_HOST_GLOBAL, &global) ||
        qcom_smem_find(&gSmem, &global, FM_ITEM_CRASH, &at, &size) || !size)
        return;
    char text[120];
    size_t n = 0;
    for (; n < sizeof(text) - 1 && n < size; ++n) {
        const char c = (char)((volatile uint8_t *)gSmemW.va)[at + n];
        if (!c) break;
        text[n] = c >= 32 && c < 127 ? c : '.';
    }
    text[n] = 0;
    if (n) printf("FUXI MODEM: crash reason \"%s\"\n", text);
}
/* 一個遠端 SMP2P 入條目裡某個項目的值；不在回 -1。 */
static int slaveValue(uint16_t remote_pid, uint16_t item_id, uint32_t *value)
{
    QcomSmemPartition part;
    if (qcom_smem_partition(&gSmem, QCOM_SMEM_HOST_APPS, remote_pid, &part)) return -1;
    volatile uint8_t *in = itemIn(&part, item_id, NULL);
    if (!in) return -1;
    QcomSmp2p view;
    bzero(&view, sizeof(view));
    view.in = (volatile uint32_t *)in;
    view.local_pid = 0;
    view.remote_pid = remote_pid;
    if (qcom_smp2p_check_in(view.in, 0, remote_pid)) return -1;
    const int index = qcom_smp2p_find_in(&view, "slave-kernel");
    if (index < 0) return -1;
    *value = qcom_smp2p_in_value(&view, index);
    return 0;
}

/* ---- 安全世界呼叫：svc、cmd，x2..x5 四個參數（MP_ASSIGN 要用到 x5） ---- */
struct scm_result { int64_t x0; uint64_t x1; };
static struct scm_result scm(uint32_t svc, uint32_t command, uint32_t arginfo, uint64_t a, uint64_t b, uint64_t c, uint64_t d)
{
    struct scm_result result = {-1, 0};
    for (unsigned busy = 0; busy < 20; ++busy) {
        uint64_t function = qcom_scm_fn(svc, command), session = 0;
        unsigned resumes = 0;
        for (;;) {
            register uint64_t x0 __asm__("x0") = function, x1 __asm__("x1") = arginfo, x2 __asm__("x2") = a, x3 __asm__("x3") = b,
                x4 __asm__("x4") = c, x5 __asm__("x5") = d, x6 __asm__("x6") = session, x7 __asm__("x7") = 0;
            __asm__ volatile("smc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x4), "+r"(x5), "+r"(x6), "+r"(x7)
                :: "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "memory");
            result.x0 = (int64_t)x0;
            result.x1 = x1;
            if ((int64_t)x0 != QCOM_SCM_INTERRUPTED || ++resumes > 100000) break;
            function = QCOM_SCM_INTERRUPTED;
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
static struct scm_result pil(uint32_t command, uint32_t arginfo, uint64_t a, uint64_t b, uint64_t c)
{
    return scm(QCOM_SCM_SVC_PIL, command, arginfo, a, b, c, 0);
}
static int pasSupported(uint32_t pas)
{
    const struct scm_result r = pil(QCOM_SCM_PAS_IS_SUPPORTED, qcom_scm_args(1, 0, 0, 0), pas, 0, 0);
    return r.x0 == 0 ? (int)(r.x1 != 0) : -1;
}
/* 一段記憶體的擁有者換人（Linux qcom_scm_assign_mem）。參數頁在實體連續的一頁裡，先清到 PoC。 */
static int assignMemory(uint64_t pa, uint64_t size, const uint32_t *sources, unsigned nsources, const uint32_t *vmids,
    const uint32_t *perms, unsigned ndest)
{
    vm_offset_t va = 0;
    uint64_t args[4];
    if (kmem_alloc_contig(kernel_map, &va, PAGE_SIZE, PAGE_MASK, 0, 0, KMA_KOBJECT | KMA_ZERO | KMA_NOPAGEWAIT,
        VM_KERN_MEMORY_IOKIT) != KERN_SUCCESS)
        return -12;
    const ppnum_t pn = pmap_find_phys(kernel_pmap, (addr64_t)va);
    int rc = pn ? 0 : -14;
    if (!rc && (PAGE_SIZE < FM_ASSIGN_PAGE ||
        fm_assign_page((uint8_t *)va, ptoa_64(pn), pa, size, sources, nsources, vmids, perms, ndest, args)))
        rc = -22;
    if (!rc) {
        CleanPoC_DcacheRegion_Force(va, (unsigned)PAGE_SIZE);
        rc = scmOk(scm(FM_SCM_SVC_MP, FM_SCM_MP_ASSIGN, FM_ASSIGN_ARGINFO, args[0], args[1], args[2], args[3])) ? 0 : -1;
    }
    kmem_free(kernel_map, va, PAGE_SIZE);
    return rc;
}

/* ---- RPMh：TCS 1 的一次 active 請求（fuxi_adsp.c vote() 同法，1 或 2 個資源） ---- */
static int readCmdDb(uint8_t *copy)
{
    struct fm_window db = {FM_CMD_DB, FM_CMD_DB_SIZE, 0};
    if (fm_map(&db, VM_WIMG_IO) != KERN_SUCCESS) return 0;
    for (uint32_t i = 0; i < FM_CMD_DB_SIZE; i += 4) {
        const uint32_t w = rd(&db, i);
        memcpy(copy + i, &w, 4);
    }
    fm_unmap(&db);
    return 1;
}
static int lookupRails(int with_cx)
{
    uint8_t *db = kalloc_data(FM_CMD_DB_SIZE, Z_WAITOK);
    int rc = !db || !readCmdDb(db) || qcom_cmd_db_find(db, FM_CMD_DB_SIZE, "mss.lvl", &gMss) ||
        (with_cx && qcom_cmd_db_find(db, FM_CMD_DB_SIZE, "cx.lvl", &gCx));
    if (db) {
        QcomRpmhResource bcm;
        gBcmIp0 = (!qcom_cmd_db_find(db, FM_CMD_DB_SIZE, "IP0", &bcm) && fm_is_bcm(bcm.address)) ? bcm.address : 0u;
        gBcmSn8 = (!qcom_cmd_db_find(db, FM_CMD_DB_SIZE, "SN8", &bcm) && fm_is_bcm(bcm.address)) ? bcm.address : 0u;
        gBcmSh0 = (!qcom_cmd_db_find(db, FM_CMD_DB_SIZE, "SH0", &bcm) && fm_is_bcm(bcm.address)) ? bcm.address : 0u;
        gBcmMc0 = (!qcom_cmd_db_find(db, FM_CMD_DB_SIZE, "MC0", &bcm) && fm_is_bcm(bcm.address)) ? bcm.address : 0u;
        gBcmAcv = (!qcom_cmd_db_find(db, FM_CMD_DB_SIZE, "ACV", &bcm) && fm_is_bcm(bcm.address)) ? bcm.address : 0u;
    }
    if (!rc && !with_cx) {
        QcomRpmhResource cx;
        if (!qcom_cmd_db_find(db, FM_CMD_DB_SIZE, "cx.lvl", &cx))
            printf("FUXI MODEM: cmd-db cx.lvl %#x (%u levels; not voted)\n", cx.address, cx.level_count);
    }
    if (db) kfree_data(db, FM_CMD_DB_SIZE);
    if (!rc) printf("FUXI MODEM: cmd-db mss.lvl %#x (%u levels)%s\n", gMss.address, gMss.level_count, with_cx ? " + cx.lvl" : "");
    printf("FUXI MODEM: cmd-db bcm IP0=%#x SN8=%#x SH0=%#x MC0=%#x ACV=%#x\n",
        gBcmIp0, gBcmSn8, gBcmSh0, gBcmMc0, gBcmAcv);
    return rc ? -2 : 0;
}
static int vote(const uint32_t *addresses, const uint32_t *values, unsigned count)
{
    const unsigned tcs = 1;
    const uint32_t bit = 1u << tcs;
    if (fuxi_rpmh_acquire()) return -EIO;
    const uint32_t base = tcs * gLayout.tcs_stride;
    if (!rd(&gRsc, FM_RSC_TCS + base + gLayout.status) || rd(&gRsc, FM_RSC_TCS + base + gLayout.control) ||
        rd(&gRsc, FM_RSC_TCS + base + gLayout.cmd_enable) || rd(&gRsc, FM_RSC_TCS + base + gLayout.wait_for_cmpl) ||
        (rd(&gRsc, FM_RSC_TCS + gLayout.irq_status) & bit)) {
        fuxi_rpmh_release(1);
        return -EBUSY;
    }
    QcomRpmhWrite writes[16];
    const unsigned n = qcom_rpmh_active_request(&gLayout, tcs, addresses, values, count, writes, 16);
    if (!n) { fuxi_rpmh_release(1); return -EINVAL; }
    const uint32_t irq_enable = rd(&gRsc, FM_RSC_TCS);
    wr(&gRsc, FM_RSC_TCS, irq_enable | bit);
    for (unsigned i = 0; i < n; ++i) wr(&gRsc, FM_RSC_TCS + writes[i].offset, writes[i].value);
    int done = 0;
    for (unsigned t = 0; t < 1000 && !done; ++t) {
        done = (rd(&gRsc, FM_RSC_TCS + gLayout.irq_status) & bit) != 0;
        if (!done) delay(100);
    }
    if (!done) {
        printf("FUXI MODEM: rpmh vote timed out; shared RSC quarantined\n");
        fuxi_rpmh_release(0);
        return -ETIMEDOUT;
    }
    const unsigned m = qcom_rpmh_finish(&gLayout, tcs, writes, 16);
    for (unsigned i = 0; i < m; ++i) wr(&gRsc, FM_RSC_TCS + writes[i].offset, writes[i].value);
    wr(&gRsc, FM_RSC_TCS + base + gLayout.wait_for_cmpl, 0);
    wr(&gRsc, FM_RSC_TCS, (rd(&gRsc, FM_RSC_TCS) & ~bit) | (irq_enable & bit));
    const int healthy = !rd(&gRsc, FM_RSC_TCS + base + gLayout.control) && !rd(&gRsc, FM_RSC_TCS + base + gLayout.cmd_enable) &&
        !rd(&gRsc, FM_RSC_TCS + base + gLayout.wait_for_cmpl) && !(rd(&gRsc, FM_RSC_TCS + gLayout.irq_status) & bit) &&
        rd(&gRsc, FM_RSC_TCS + base + gLayout.status) && (rd(&gRsc, FM_RSC_TCS) & bit) == (irq_enable & bit);
    fuxi_rpmh_release(healthy);
    if (!healthy) { printf("FUXI MODEM: rpmh cleanup failed; shared RSC quarantined\n"); return -EIO; }
    return 0;
}
static int voteMss(int on)
{
    uint32_t level = 0;
    if (on && qcom_rpmh_level_index(&gMss, 0xffffffffu, &level)) return -EINVAL;
    int rc = -EBUSY;
    for (unsigned tries = 0; tries < 50 && rc == -EBUSY; ++tries) {
        rc = vote(&gMss.address, &level, 1);
        if (rc == -EBUSY) IOSleep(20);
    }
    printf("FUXI MODEM: rpmh mss.lvl %s (index %u) rc=%d\n", on ? "max" : "off", level, rc);
    if (!rc) gVoted = on;
    return rc;
}
static int voteCxMax(void)
{
    uint32_t level = 0;
    if (qcom_rpmh_level_index(&gCx, 0xffffffffu, &level)) return -EINVAL;
    int rc = -EBUSY;
    for (unsigned tries = 0; tries < 50 && rc == -EBUSY; ++tries) {
        rc = vote(&gCx.address, &level, 1);
        if (rc == -EBUSY) IOSleep(20);
    }
    printf("FUXI MODEM: rpmh cx.lvl max (index %u, kept until reboot) rc=%d\n", level, rc);
    return rc;
}
static int voteBcm(const char *name, uint32_t address, int on)
{
    if (!fm_is_bcm(address)) return -ENOENT;
    const uint32_t val = fm_bcm_vote(on, on ? 0x3fffu : 0u);
    int rc = -EBUSY;
    for (unsigned tries = 0; tries < 50 && rc == -EBUSY; ++tries) {
        rc = vote(&address, &val, 1);
        if (rc == -EBUSY) IOSleep(20);
    }
    printf("FUXI MODEM: bcm %s (%#x) %s val=%#x rc=%d\n", name, address, on ? "on" : "off", val, rc);
    return rc;
}

/* ---- 韌體映像 ---- */
struct fm_image { void *file; uint64_t size; QcomMdtImage mdt; struct fm_window meta; };
static struct fm_image gDtbImage = {.meta = {FM_META_BASE, FM_META_SLOT, 0}},
                       gModemImage = {.meta = {FM_META_BASE + FM_META_SLOT, FM_META_SLOT, 0}},
                       gIpaImage = {.meta = {FM_META_BASE + 2u * FM_META_SLOT, FM_META_SLOT, 0}};
static int fileRead(void *context, uint64_t offset, void *to, size_t length) { return fuxi_modem_fw_read(context, offset, to, length); }
static void release(struct fm_image *image)
{
    if (image->file) fuxi_modem_fw_close(image->file);
    fm_unmap(&image->meta);
    image->file = NULL;
}
static int openImage(struct fm_image *image, const char *name)
{
    int rc = fuxi_modem_fw_open(name, &image->file, &image->size);
    if (rc) { image->file = NULL; return rc; }
    rc = qcom_mdt_parse(&image->mdt, image->size, fileRead, image->file);
    if (rc) { release(image); return 1000 - rc; }
    return 0;
}
static int openFirmware(int retry)
{
    static const uint32_t pauses[] = {1, 2, 4, 8, 16, 30, 30, 30};
    for (unsigned i = 0;; ++i) {
        int rc = openImage(&gDtbImage, "modem_dtb");
        if (!rc) {
            rc = openImage(&gModemImage, "modem");
            if (rc) release(&gDtbImage);
        }
        if (!rc) return 0;
        if (!retry || (rc != ENOENT && rc != ENXIO && rc != EIO) || i == sizeof(pauses) / sizeof(pauses[0])) return rc;
        IOSleep(pauses[i] * 1000);
    }
}
/* 每個可載入分段放不放得進保留區；回傳放不下的數目。 */
static unsigned checkFit(const char *name, const struct fm_image *image, uint64_t base, uint64_t size)
{
    unsigned bad = 0, loads = 0;
    uint64_t largest = 0, total = 0;
    for (unsigned i = 0; i < image->mdt.count; ++i) {
        const QcomMdtSegment *s = &image->mdt.segment[i];
        uint64_t at = 0;
        if (!s->load) continue;
        loads++;
        total += s->file_size;
        if (s->file_size > largest) largest = s->file_size;
        if (qcom_mdt_place(&image->mdt, i, base, size, &at) || at + s->memory_size > size) bad++;
    }
    printf("FUXI MODEM: firmware %s %llu KiB, %u headers, %u loads (%llu KiB in the file, largest %llu KiB), %s, span %#llx in %#llx: %s\n",
        name, image->size >> 10, image->mdt.count, loads, total >> 10, largest >> 10, image->mdt.relocatable ? "relocatable" : "fixed",
        qcom_mdt_span(&image->mdt), size, bad || qcom_mdt_span(&image->mdt) > size ? "DOES NOT FIT" : "fits");
    return bad + (qcom_mdt_span(&image->mdt) > size);
}
/* metadata 寫進這個映像的固定格（見 FM_META_BASE；Normal-NC 對映，對應 Linux 的 dma_alloc_coherent），再 init_image。
 * 呼叫期間安全世界會保護這一格，auth_and_reset 之前不再寫它，release() 才收回對映。 */
static int authenticateHeader(struct fm_image *image, uint32_t pas, uint64_t base)
{
    const uint64_t bytes = qcom_mdt_metadata_size(&image->mdt);
    if (!bytes || bytes > image->meta.size) return -22;
    if (fm_map(&image->meta, VM_WIMG_WCOMB) != KERN_SUCCESS) return -12;
    for (vm_size_t at = 0; at < image->meta.size; at += sizeof(uint64_t)) *(volatile uint64_t *)(image->meta.va + at) = 0;
    if (qcom_mdt_metadata(&image->mdt, fileRead, image->file, (uint8_t *)image->meta.va, bytes)) return -5;
    __asm__ volatile("dsb sy" ::: "memory");
    printf("FUXI MODEM: pas %#x metadata %llu bytes at %#llx (below 4 GiB)\n", pas, (unsigned long long)bytes,
        (unsigned long long)image->meta.pa);
    if (!scmOk(pil(QCOM_SCM_PAS_INIT_IMAGE, qcom_scm_args(2, QCOM_SCM_VAL, QCOM_SCM_RW, 0), pas, image->meta.pa, 0))) return -1;
    if (image->mdt.relocatable &&
        !scmOk(pil(QCOM_SCM_PAS_MEM_SETUP, qcom_scm_args(3, 0, 0, 0), pas, base, qcom_mdt_span(&image->mdt))))
        return -2;
    return 0;
}
/* 逐分段對映、寫入、收回對映（modem 區 264 MiB，不一次對映整區）。 */
static int loadSegments(struct fm_image *image, uint64_t base, uint64_t size)
{
    const size_t chunk = 256 * 1024;
    uint8_t *bounce = kalloc_data(chunk, Z_WAITOK);
    int rc = bounce ? 0 : -12;
    for (unsigned i = 0; !rc && i < image->mdt.count; ++i) {
        const QcomMdtSegment *s = &image->mdt.segment[i];
        uint64_t at = 0;
        if (!s->load) continue;
        if (qcom_mdt_place(&image->mdt, i, base, size, &at) || at + s->memory_size > size) { rc = -7; break; }
        const uint64_t start = trunc_page_64(base + at), end = round_page_64(base + at + s->memory_size);
        struct fm_window w = {start, (vm_size_t)(end - start), 0};
        if (fm_map(&w, VM_WIMG_WCOMB) != KERN_SUCCESS) { rc = -12; break; }
        uint8_t *to = (uint8_t *)w.va + (base + at - start);
        for (uint64_t done = 0; !rc && done < s->file_size;) {
            const size_t n = s->file_size - done < chunk ? (size_t)(s->file_size - done) : chunk;
            if (fuxi_modem_fw_read(image->file, s->file_offset + done, bounce, n)) { rc = -5; break; }
            memcpy(to + done, bounce, n);
            done += n;
        }
        if (!rc && s->memory_size > s->file_size) bzero(to + s->file_size, (size_t)(s->memory_size - s->file_size));
        __asm__ volatile("dsb sy" ::: "memory");
        fm_unmap(&w);
    }
    if (bounce) kfree_data(bounce, chunk);
    return rc;
}
static void shutdownModem(int dtb, int modem)
{
    if (modem) pil(QCOM_SCM_PAS_SHUTDOWN, qcom_scm_args(1, 0, 0, 0), FM_PAS_MODEM, 0, 0);
    if (dtb) pil(QCOM_SCM_PAS_SHUTDOWN, qcom_scm_args(1, 0, 0, 0), FM_PAS_DTB, 0, 0);
}

/* ---- SM8550 IPA v5.0 盤點／SMMU／PAS（fuxi,modem-ipa 位元開關，預設全關） ---- */
static void ipaAttachSmmu(void)
{
    struct fm_window smmu = {FM_SMMU_PA, FM_SMMU_SIZE, 0};
    if (fm_map(&smmu, VM_WIMG_IO) != KERN_SUCCESS) {
        printf("FUXI MODEM: ipa smmu map failed\n");
        return;
    }
    const uint32_t scr0 = rd(&smmu, 0x0u);
    const uint32_t idr0 = rd(&smmu, 0x20u);
    const uint32_t idr1 = rd(&smmu, 0x24u);
    const uint32_t idr2 = rd(&smmu, 0x28u);
    const unsigned stride = (idr1 & (1u << 31)) ? 0x10000u : 0x1000u;
    const unsigned bank_base = (1u << (((idr1 >> 28) & 7u) + 1u)) * stride;
    const unsigned slots = idr0 & 0xffu;
    const unsigned banks = idr1 & 0xffu;
    if ((scr0 & 9u) || (idr0 & 0x48000000u) != 0x48000000u || !(idr2 & 0x1000u) || !slots || slots > 128u ||
        banks <= FM_SMMU_BYPASS_BANK || banks > 128u || bank_base > FM_SMMU_SIZE ||
        banks > (FM_SMMU_SIZE - bank_base) / stride) {
        printf("FUXI MODEM: ipa smmu unsupported scr0=%#x idr=%#x/%#x/%#x\n", scr0, idr0, idr1, idr2);
        fm_unmap(&smmu);
        return;
    }
    uint32_t smrs[128], s2crs[128];
    int usb_cb4 = 0;
    for (unsigned idx = 0; idx < slots; ++idx) {
        smrs[idx] = rd(&smmu, 0x800u + 4u * idx);
        s2crs[idx] = rd(&smmu, 0xc00u + 4u * idx);
        if (fm_smmu_overlap(smrs[idx], 0x40u, 0u) && s2crs[idx] == FM_SMMU_BYPASS_BANK) usb_cb4++;
    }
    const uint32_t cb4_sctlr = rd(&smmu, bank_base + stride * FM_SMMU_BYPASS_BANK);
    const uint32_t cb4_cbar = rd(&smmu, stride + 4u * FM_SMMU_BYPASS_BANK);
    if (usb_cb4 != 1 || (cb4_sctlr & 1u) != 0u || ((cb4_cbar >> 16) & 3u) != 1u) {
        printf("FUXI MODEM: ipa smmu cb4 unsafe usb=%d sctlr=%#x cbar=%#x\n", usb_cb4, cb4_sctlr, cb4_cbar);
        fm_unmap(&smmu);
        return;
    }
    static const uint32_t kSids[2] = {FM_SMMU_IPA_AP_SID, FM_SMMU_IPA_UC_SID};
    for (unsigned sid_idx = 0; sid_idx < 2u; ++sid_idx) {
        const uint32_t sid = kSids[sid_idx];
        const int picked = fm_smmu_pick_high_slot(smrs, s2crs, slots, sid, 0u);
        if (picked < 0) {
            printf("FUXI MODEM: ipa smmu sid %#x unavailable rc=%d (slots=%u)\n", sid, picked, slots);
            continue;
        }
        const unsigned slot = (unsigned)picked;
        const uint32_t smr_off = 0x800u + 4u * slot;
        const uint32_t s2cr_off = 0xc00u + 4u * slot;
        const uint32_t want_smr = (1u << 31) | sid;
        wr(&smmu, s2cr_off, FM_SMMU_BYPASS_BANK);
        wr(&smmu, smr_off, want_smr);
        const uint32_t rb_s2cr = rd(&smmu, s2cr_off);
        const uint32_t rb_smr = rd(&smmu, smr_off);
        if (rb_s2cr != FM_SMMU_BYPASS_BANK || rb_smr != want_smr) {
            wr(&smmu, smr_off, smrs[slot]);
            wr(&smmu, s2cr_off, s2crs[slot]);
            printf("FUXI MODEM: ipa smmu sid %#x verify failed slot=%u smr=%#x s2cr=%#x\n", sid, slot, rb_smr, rb_s2cr);
        } else {
            smrs[slot] = rb_smr;
            s2crs[slot] = rb_s2cr;
            printf("FUXI MODEM: ipa smmu sid %#x attached slot=%u/%u smr=%#x s2cr=%#x\n", sid, slot, slots, rb_smr, rb_s2cr);
        }
    }
    fm_unmap(&smmu);
}

static void ipaCacheCleanInv(vm_offset_t va, vm_size_t len)
{
    const vm_offset_t start = va & ~(vm_offset_t)63u;
    const vm_offset_t end = (va + len + 63u) & ~(vm_offset_t)63u;
    __asm__ volatile("dsb sy" ::: "memory");
    for (vm_offset_t addr = start; addr < end; addr += 64u) {
        __asm__ volatile("dc civac, %0" :: "r"(addr) : "memory");
    }
    __asm__ volatile("dsb sy" ::: "memory");
}

static int ipaAllocDma(vm_size_t bytes, vm_offset_t *va_out, uint64_t *pa_out)
{
    const vm_size_t alloc_bytes = round_page(bytes);
    vm_offset_t va = 0;
    if (kmem_alloc_contig(kernel_map, &va, alloc_bytes, PAGE_MASK, 0, 0, KMA_DATA | KMA_ZERO,
            VM_KERN_MEMORY_IOKIT) != KERN_SUCCESS) {
        return -12;
    }
    const ppnum_t pn0 = pmap_find_phys(kernel_pmap, (addr64_t)va);
    if (!pn0) {
        kmem_free(kernel_map, va, alloc_bytes);
        return -14;
    }
    const uint64_t pa0 = ptoa_64(pn0);
    for (vm_size_t off = PAGE_SIZE; off < alloc_bytes; off += PAGE_SIZE) {
        const ppnum_t pn = pmap_find_phys(kernel_pmap, (addr64_t)(va + off));
        if (!pn || ptoa_64(pn) != pa0 + (uint64_t)off) {
            kmem_free(kernel_map, va, alloc_bytes);
            return -14;
        }
    }
    ipaCacheCleanInv(va, alloc_bytes);
    *va_out = va;
    *pa_out = pa0;
    return 0;
}

static uint32_t ipaReadRegs(const char *tag)
{
    struct fm_window ipa = {FM_IPA5_REG_PA, FM_IPA5_REG_SIZE, 0};
    struct fm_window gsi = {FM_GSI5_MAP_PA, FM_GSI5_MAP_SIZE, 0};
    if (fm_map(&ipa, VM_WIMG_IO) != KERN_SUCCESS) {
        printf("FUXI MODEM: ipa %s map ipa-reg failed\n", tag);
        return 0u;
    }
    const uint32_t flavor0 = rd(&ipa, FM_IPA5_FLAVOR_0);
    const uint32_t comp_cfg = rd(&ipa, FM_IPA5_COMP_CFG);
    const uint32_t shared_mem = rd(&ipa, FM_IPA5_SHARED_MEM_SIZE);
    fm_unmap(&ipa);
    gIpaMemOffset = fm_ipa_shared_mem_offset(shared_mem);
    gIpaMemSize = fm_ipa_shared_mem_size(shared_mem);
    if (fm_map(&gsi, VM_WIMG_IO) != KERN_SUCCESS) {
        printf("FUXI MODEM: ipa %s flavor0=%#x comp_cfg=%#x shared=%#x (off=%#x size=%#x) gsi map failed\n",
            tag, flavor0, comp_cfg, shared_mem, gIpaMemOffset, gIpaMemSize);
        return 0u;
    }
    const uint32_t gsi_status = rd(&gsi, FM_GSI5_STATUS);
    const uint32_t hw_param2 = rd(&gsi, FM_GSI5_HW_PARAM_2);
    const uint32_t hw_param4 = rd(&gsi, FM_GSI5_HW_PARAM_4);
    const uint32_t scratch0 = rd(&gsi, FM_GSI5_SCRATCH_0);
    fm_unmap(&gsi);
    printf("FUXI MODEM: ipa %s flavor0=%#x comp_cfg=%#x shared=%#x (off=%#x size=%#x) gsi_status=%#x hw2=%#x hw4=%#x scratch0=%#x\n",
        tag, flavor0, comp_cfg, shared_mem, gIpaMemOffset, gIpaMemSize, gsi_status, hw_param2, hw_param4, scratch0);
    return gsi_status;
}

static int ipaConfigHw(void)
{
    if (!gIpaRegMapped) {
        if (fm_map(&gIpaReg, VM_WIMG_IO) != KERN_SUCCESS) {
            printf("FUXI MODEM: ipa cfg map ipa-reg failed\n");
            return -12;
        }
        gIpaRegMapped = 1u;
    }
    /* 對照 Linux ipa_hardware_config (v5.5)：COMP_CFG、QSB、Qtime 19.2 MHz/192、IDLE、資源群組與 SRAM canaries */
    const uint32_t comp_cfg = rd(&gIpaReg, FM_IPA5_COMP_CFG) | (1u << 7) | (1u << 8);
    wr(&gIpaReg, FM_IPA5_COMP_CFG, comp_cfg);
    wr(&gIpaReg, FM_IPA5_QSB_MAX_WRITES, 0u);
    wr(&gIpaReg, FM_IPA5_QSB_MAX_READS, 12u | (8u << 4));
    wr(&gIpaReg, FM_IPA5_TIMERS_XO_CLK_DIV_CFG, 0u);
    wr(&gIpaReg, FM_IPA5_QTIME_TIMESTAMP_CFG, (14u << 8) | (24u << 16));
    wr(&gIpaReg, FM_IPA5_TIMERS_PULSE_GRAN_CFG, 3u | (4u << 3) | (5u << 6) | (5u << 9));
    wr(&gIpaReg, FM_IPA5_TIMERS_XO_CLK_DIV_CFG, 191u);
    wr(&gIpaReg, FM_IPA5_TIMERS_XO_CLK_DIV_CFG, 191u | (1u << 31));
    wr(&gIpaReg, FM_IPA5_IDLE_INDICATION_CFG, 256u);
    wr(&gIpaReg, FM_IPA5_IRQ_EN, 0u);
    wr(&gIpaReg, FM_IPA5_IRQ_CLR, 0xffffffffu);

    static const uint32_t kFilterEps[3] = {2u, 12u, 15u};
    for (unsigned idx = 0u; idx < 3u; ++idx) wr(&gIpaReg, FM_IPA5_ENDP_FILTER_CACHE_CFG(kFilterEps[idx]), 0u);
    for (uint32_t route_id = 0u; route_id < FM_IPA5_ROUTE_ENTRIES; ++route_id) {
        wr(&gIpaReg, FM_IPA5_ENDP_ROUTER_CACHE_CFG(route_id), 0u);
    }

    const uint32_t src_rsrc[5][3] = {
        {fm_ipa_rsrc_pair(3u, 9u, 4u, 10u), 0u, fm_ipa_rsrc_pair(1u, 63u, 0u, 63u)},
        {fm_ipa_rsrc_pair(9u, 9u, 12u, 12u), 0u, fm_ipa_rsrc_pair(10u, 10u, 0u, 0u)},
        {fm_ipa_rsrc_pair(9u, 9u, 24u, 24u), 0u, fm_ipa_rsrc_pair(20u, 20u, 0u, 0u)},
        {fm_ipa_rsrc_pair(0u, 63u, 0u, 63u), 0u, fm_ipa_rsrc_pair(1u, 63u, 0u, 63u)},
        {fm_ipa_rsrc_pair(22u, 22u, 16u, 16u), 0u, fm_ipa_rsrc_pair(16u, 16u, 0u, 0u)},
    };
    for (uint32_t rtype = 0u; rtype < 5u; ++rtype) {
        wr(&gIpaReg, FM_IPA5_SRC_RSRC_GRP_01(rtype), src_rsrc[rtype][0]);
        wr(&gIpaReg, FM_IPA5_SRC_RSRC_GRP_23(rtype), src_rsrc[rtype][1]);
        wr(&gIpaReg, FM_IPA5_SRC_RSRC_GRP_45(rtype), src_rsrc[rtype][2]);
    }
    const uint32_t dst_rsrc[3][4] = {
        {fm_ipa_rsrc_pair(6u, 6u, 5u, 5u), 0u, 0u, fm_ipa_rsrc_pair(39u, 39u, 0u, 0u)},
        {fm_ipa_rsrc_pair(0u, 3u, 0u, 3u), 0u, 0u, 0u},
        {fm_ipa_rsrc_pair(0u, 63u, 0u, 63u), 0u, 0u, 0u},
    };
    for (uint32_t rtype = 0u; rtype < 3u; ++rtype) {
        wr(&gIpaReg, FM_IPA5_DST_RSRC_GRP_01(rtype), dst_rsrc[rtype][0]);
        wr(&gIpaReg, FM_IPA5_DST_RSRC_GRP_23(rtype), dst_rsrc[rtype][1]);
        wr(&gIpaReg, FM_IPA5_DST_RSRC_GRP_45(rtype), dst_rsrc[rtype][2]);
        wr(&gIpaReg, FM_IPA5_DST_RSRC_GRP_67(rtype), dst_rsrc[rtype][3]);
    }

    struct fm_window sram = {FM_IPA5_SRAM_PA, FM_IPA5_SRAM_MAP_SIZE, 0};
    uint32_t canaries = 0u;
    if (fm_map(&sram, VM_WIMG_IO) == KERN_SUCCESS) {
        static const uint32_t kCanaryOffs[15] = {
            0x1288u, 0x1308u, 0x1388u, 0x1408u, 0x1488u, 0x1528u, 0x15c8u, 0x1668u,
            0x1708u, 0x1b40u, 0x2868u, 0x29b8u, 0x2cd0u, 0x3898u, 0x4ee8u,
        };
        for (unsigned idx = 0u; idx < 15u; ++idx) {
            const uint32_t abs_off = gIpaMemOffset + kCanaryOffs[idx];
            if (abs_off >= 8u && abs_off <= FM_IPA5_SRAM_SIZE) {
                wr(&sram, abs_off - 4u, FM_IPA5_CANARY_VAL);
                wr(&sram, abs_off - 8u, FM_IPA5_CANARY_VAL);
                canaries += 2u;
            }
        }
        fm_unmap(&sram);
    } else {
        printf("FUXI MODEM: ipa cfg map sram failed\n");
        return -12;
    }
    gIpaCanariesWritten = canaries;
    gIpaHwConfigured = 1u;
    printf("FUXI MODEM: ipa v5.5 hw cfg ok comp=%#x mem_off=%#x mem_sz=%#x canaries=%u\n",
        comp_cfg, gIpaMemOffset, gIpaMemSize, canaries);
    return 0;
}

static int ipaGsiWaitState(uint64_t cntxt0_reg, uint32_t want_state, uint32_t *final_state)
{
    uint32_t state = 0u;
    for (unsigned ms = 0u; ms < 50u; ++ms) {
        state = (rd(&gIpaGsi, cntxt0_reg) >> 20) & 0xfu;
        if (state == want_state) {
            if (final_state) *final_state = state;
            return 0;
        }
        IOSleep(1u);
    }
    if (final_state) *final_state = state;
    return -60;
}

static int ipaGsiAllocPair(struct fm_gsi_chan *chan)
{
    uint32_t state = 0u;
    wr(&gIpaGsi, FM_GSI5_EV_CMD, fm_gsi_cmd_word(chan->ev_id, FM_GSI_CMD_ALLOCATE));
    if (ipaGsiWaitState(FM_GSI5_EV_CNTXT_0(chan->ev_id), FM_GSI_STATE_ALLOCATED, &state)) {
        printf("FUXI MODEM: gsi ev %u alloc failed state=%u errlog=%#x\n",
            chan->ev_id, state, rd(&gIpaGsi, FM_GSI5_ERROR_LOG));
        return -5;
    }
    wr(&gIpaGsi, FM_GSI5_EV_CNTXT_0(chan->ev_id), fm_gsi_ev_cntxt0());
    wr(&gIpaGsi, FM_GSI5_EV_CNTXT_1(chan->ev_id), FM_GSI_RING_BYTES);
    wr(&gIpaGsi, FM_GSI5_EV_CNTXT_2(chan->ev_id), (uint32_t)chan->ev_pa);
    wr(&gIpaGsi, FM_GSI5_EV_CNTXT_3(chan->ev_id), (uint32_t)(chan->ev_pa >> 32));
    wr(&gIpaGsi, FM_GSI5_EV_CNTXT_8(chan->ev_id), 1u << 16); /* EV_MODC=1, EV_MODT=0 */
    wr(&gIpaGsi, FM_GSI5_EV_CNTXT_9(chan->ev_id), 0u);
    wr(&gIpaGsi, FM_GSI5_EV_CNTXT_10(chan->ev_id), 0u);
    wr(&gIpaGsi, FM_GSI5_EV_CNTXT_11(chan->ev_id), 0u);
    wr(&gIpaGsi, FM_GSI5_EV_CNTXT_12(chan->ev_id), 0u);
    wr(&gIpaGsi, FM_GSI5_EV_CNTXT_13(chan->ev_id), 0u);
    wr(&gIpaGsi, FM_GSI5_EV_DB_0(chan->ev_id), (uint32_t)chan->ev_pa + (FM_GSI_RING_COUNT - 1u) * FM_GSI_RING_ELEM_SIZE);

    wr(&gIpaGsi, FM_GSI5_CH_CMD, fm_gsi_cmd_word(chan->ch_id, FM_GSI_CMD_ALLOCATE));
    if (ipaGsiWaitState(FM_GSI5_CH_CNTXT_0(chan->ch_id), FM_GSI_STATE_ALLOCATED, &state)) {
        printf("FUXI MODEM: gsi ch %u alloc failed state=%u errlog=%#x\n",
            chan->ch_id, state, rd(&gIpaGsi, FM_GSI5_ERROR_LOG));
        return -5;
    }
    wr(&gIpaGsi, FM_GSI5_CH_CNTXT_0(chan->ch_id), fm_gsi_ch_cntxt0(chan->toward_ipa));
    wr(&gIpaGsi, FM_GSI5_CH_CNTXT_1(chan->ch_id), fm_gsi_ch_cntxt1(FM_GSI_RING_BYTES, chan->ev_id));
    wr(&gIpaGsi, FM_GSI5_CH_CNTXT_2(chan->ch_id), (uint32_t)chan->tre_pa);
    wr(&gIpaGsi, FM_GSI5_CH_CNTXT_3(chan->ch_id), (uint32_t)(chan->tre_pa >> 32));
    wr(&gIpaGsi, FM_GSI5_CH_QOS(chan->ch_id), fm_gsi_ch_qos(chan->is_cmd));
    wr(&gIpaGsi, FM_GSI5_CH_SCRATCH_0(chan->ch_id), 0u);
    wr(&gIpaGsi, FM_GSI5_CH_SCRATCH_1(chan->ch_id), 0u);
    wr(&gIpaGsi, FM_GSI5_CH_SCRATCH_2(chan->ch_id), ((uint32_t)chan->max_tre * FM_GSI_RING_ELEM_SIZE) << 16);
    const uint32_t scr3 = rd(&gIpaGsi, FM_GSI5_CH_SCRATCH_3(chan->ch_id)) & 0xffffu;
    wr(&gIpaGsi, FM_GSI5_CH_SCRATCH_3(chan->ch_id), ((2u * FM_GSI_RING_ELEM_SIZE) << 16) | scr3);
    return 0;
}

static int ipaGsiStartChan(struct fm_gsi_chan *chan)
{
    uint32_t state = 0u;
    wr(&gIpaGsi, FM_GSI5_CH_CMD, fm_gsi_cmd_word(chan->ch_id, FM_GSI_CMD_START));
    if (ipaGsiWaitState(FM_GSI5_CH_CNTXT_0(chan->ch_id), FM_GSI_STATE_STARTED, &state)) {
        printf("FUXI MODEM: gsi ch %u start failed state=%u errlog=%#x\n",
            chan->ch_id, state, rd(&gIpaGsi, FM_GSI5_ERROR_LOG));
        return -5;
    }
    chan->started = 1u;
    return 0;
}

static void ipaGsiPostRxAll(struct fm_gsi_chan *chan)
{
    if (!chan->started || !chan->rx_va) return;
    uint8_t *ring = (uint8_t *)chan->tre_va;
    for (uint32_t slot = 0u; slot < FM_GSI_RX_BUFS; ++slot) {
        const uint32_t idx = (chan->tre_idx + slot) & (FM_GSI_RING_COUNT - 1u);
        fm_gsi_tre_fill(ring + idx * FM_GSI_RING_ELEM_SIZE, chan->rx_pa + (uint64_t)slot * FM_GSI_RX_BUF_SIZE,
            FM_GSI_RX_BUF_SIZE, 0, 1);
    }
    chan->tre_idx = (chan->tre_idx + FM_GSI_RX_BUFS) & (FM_GSI_RING_COUNT - 1u);
    ipaCacheCleanInv(chan->rx_va, (vm_size_t)FM_GSI_RX_BUFS * FM_GSI_RX_BUF_SIZE);
    ipaCacheCleanInv(chan->tre_va, FM_GSI_RING_BYTES);
    wr(&gIpaGsi, FM_GSI5_CH_DB_0(chan->ch_id), (uint32_t)chan->tre_pa + (chan->tre_idx * FM_GSI_RING_ELEM_SIZE));
}

static void ipaHandleModemRx(const uint8_t *buf, uint32_t len);

static void ipaPollRxChan(struct fm_gsi_chan *chan)
{
    if (!chan->started || !chan->rx_va) return;
    const uint32_t wp = rd(&gIpaGsi, FM_GSI5_EV_CNTXT_4(chan->ev_id));
    const uint32_t hw_idx = ((wp - (uint32_t)chan->ev_pa) / FM_GSI_RING_ELEM_SIZE) & (FM_GSI_RING_COUNT - 1u);
    if (hw_idx == chan->ev_idx) return;
    ipaCacheCleanInv(chan->ev_va, FM_GSI_RING_BYTES);
    const uint8_t *ev_ring = (const uint8_t *)chan->ev_va;
    uint8_t *tre_ring = (uint8_t *)chan->tre_va;
    uint32_t reposted = 0u;
    while (chan->ev_idx != hw_idx) {
        const uint8_t *ev = ev_ring + chan->ev_idx * FM_GSI_RING_ELEM_SIZE;
        const uint16_t rx_len = fm_le16(ev + 8);
        const uint8_t code = ev[11];
        chan->rx_events++;
        chan->rx_bytes += rx_len;
        if (code != 1u && code != 2u) chan->rx_errs++;
        const uint32_t buf_slot = (chan->rx_events - 1u) % FM_GSI_RX_BUFS;
        if (((gIpaMode & 0x800u) || gBbMode) && chan == &gIpaChans[3] && (code == 1u || code == 2u) &&
            rx_len > 0u && rx_len <= FM_GSI_RX_BUF_SIZE) {
            const vm_offset_t buf_va = chan->rx_va + (vm_offset_t)buf_slot * FM_GSI_RX_BUF_SIZE;
            ipaCacheCleanInv(buf_va, rx_len);
            ipaHandleModemRx((const uint8_t *)buf_va, rx_len);
            ipaCacheCleanInv(buf_va, FM_GSI_RX_BUF_SIZE);
            __atomic_store_n(&gCellActiveHint, 1u, __ATOMIC_RELEASE);
        }
        fm_gsi_tre_fill(tre_ring + chan->tre_idx * FM_GSI_RING_ELEM_SIZE,
            chan->rx_pa + (uint64_t)buf_slot * FM_GSI_RX_BUF_SIZE, FM_GSI_RX_BUF_SIZE, 0, 1);
        chan->tre_idx = (chan->tre_idx + 1u) & (FM_GSI_RING_COUNT - 1u);
        chan->ev_idx = (chan->ev_idx + 1u) & (FM_GSI_RING_COUNT - 1u);
        reposted++;
    }
    wr(&gIpaGsi, FM_GSI5_EV_DB_0(chan->ev_id),
        (uint32_t)chan->ev_pa + (((chan->ev_idx - 1u) & (FM_GSI_RING_COUNT - 1u)) * FM_GSI_RING_ELEM_SIZE));
    if (reposted) {
        ipaCacheCleanInv(chan->tre_va, FM_GSI_RING_BYTES);
        wr(&gIpaGsi, FM_GSI5_CH_DB_0(chan->ch_id), (uint32_t)chan->tre_pa + (chan->tre_idx * FM_GSI_RING_ELEM_SIZE));
    }
}

static void ipaPollTxChanLocked(void)
{
    struct fm_gsi_chan *chan = &gIpaChans[2];
    if (!chan->started || !gIpaModemTxVa) return;
    const uint32_t wp = rd(&gIpaGsi, FM_GSI5_EV_CNTXT_4(chan->ev_id));
    const uint32_t hw_idx = ((wp - (uint32_t)chan->ev_pa) / FM_GSI_RING_ELEM_SIZE) & (FM_GSI_RING_COUNT - 1u);
    if (hw_idx == chan->ev_idx) return;
    ipaCacheCleanInv(chan->ev_va, FM_GSI_RING_BYTES);
    const uint8_t *ev_ring = (const uint8_t *)chan->ev_va;
    while (chan->ev_idx != hw_idx) {
        const uint8_t code = ev_ring[chan->ev_idx * FM_GSI_RING_ELEM_SIZE + 11u];
        if (code != 1u && code != 2u) gIpaTxErrs++;
        if (gIpaTxCompleted != gIpaTxSubmitted) gIpaTxCompleted++;
        chan->ev_idx = (chan->ev_idx + 1u) & (FM_GSI_RING_COUNT - 1u);
    }
    wr(&gIpaGsi, FM_GSI5_EV_DB_0(chan->ev_id),
        (uint32_t)chan->ev_pa + (((chan->ev_idx - 1u) & (FM_GSI_RING_COUNT - 1u)) * FM_GSI_RING_ELEM_SIZE));
}

static int ipaModemTxSubmit(int wrap_qmap, uint8_t mux_id, const uint8_t *data, uint32_t len)
{
    if (!data || !len) return EINVAL;
    lck_mtx_lock(&gIpaTxLock);
    struct fm_gsi_chan *chan = &gIpaChans[2];
    if (!gIpaGsiReady || !chan->started || !gIpaModemTxVa) {
        gIpaTxDrops++;
        lck_mtx_unlock(&gIpaTxLock);
        return ENETDOWN;
    }
    if (gIpaTxSubmitted - gIpaTxCompleted >= FM_GSI_TX_BUFS) ipaPollTxChanLocked();
    if (gIpaTxSubmitted - gIpaTxCompleted >= FM_GSI_TX_BUFS) {
        gIpaTxDrops++;
        lck_mtx_unlock(&gIpaTxLock);
        return EBUSY;
    }
    const uint32_t slot = gIpaTxSubmitted & (FM_GSI_TX_BUFS - 1u);
    uint8_t *dst = (uint8_t *)(gIpaModemTxVa + (vm_offset_t)slot * FM_GSI_TX_BUF_SIZE);
    uint32_t tx_len = 0u;
    if (wrap_qmap) {
        tx_len = fm_qmap_wrap(dst, FM_GSI_TX_BUF_SIZE, mux_id, data, len, 0);
    } else if (len <= FM_GSI_TX_BUF_SIZE) {
        memcpy(dst, data, len);
        tx_len = len;
    }
    if (!tx_len) {
        gIpaTxDrops++;
        lck_mtx_unlock(&gIpaTxLock);
        return EINVAL;
    }
    ipaCacheCleanInv((vm_offset_t)dst, tx_len);
    uint8_t *tre_ring = (uint8_t *)chan->tre_va;
    fm_gsi_tre_fill(tre_ring + chan->tre_idx * FM_GSI_RING_ELEM_SIZE,
        gIpaModemTxPa + (uint64_t)slot * FM_GSI_TX_BUF_SIZE, (uint16_t)tx_len, 0, 1);
    chan->tre_idx = (chan->tre_idx + 1u) & (FM_GSI_RING_COUNT - 1u);
    ipaCacheCleanInv(chan->tre_va, FM_GSI_RING_BYTES);
    wr(&gIpaGsi, FM_GSI5_CH_DB_0(chan->ch_id), (uint32_t)chan->tre_pa + (chan->tre_idx * FM_GSI_RING_ELEM_SIZE));
    gIpaTxSubmitted++;
    gIpaTxBytes += tx_len;
    lck_mtx_unlock(&gIpaTxLock);
    __atomic_store_n(&gCellActiveHint, 1u, __ATOMIC_RELEASE);
    return 0;
}

void coachz_modem_data_receiver(void (*receiver)(uint32_t, const uint8_t *, uint32_t, uint32_t))
{
    gBbDataRx = receiver;
}

int coachz_modem_data_send(uint32_t mux, const uint8_t *ip, uint32_t length)
{
    if (!gBbMode || !gIpaGsiReady || !gIpaIndSent || !gIpaModemTxVa) return ENETDOWN;
    if (!ip || !length || mux == 0u || mux > 0xffu) return EINVAL;
    const int err = ipaModemTxSubmit(1, (uint8_t)mux, ip, length);
    if (!err) gBbTxPackets++;
    else gBbTxDrops++;
    return err;
}

struct fm_ipa_cmd_desc {
    uint16_t opcode;
    uint64_t payload_pa;
};

static int ipaRunCmdBatch(const char *label, const struct fm_ipa_cmd_desc *cmds, uint32_t count)
{
    struct fm_gsi_chan *chan = &gIpaChans[0];
    if (!chan->started || !count || count >= FM_GSI_RING_COUNT) return -22;
    uint8_t *ring = (uint8_t *)chan->tre_va;
    for (uint32_t idx = 0u; idx < count; ++idx) {
        const uint32_t slot = (chan->tre_idx + idx) & (FM_GSI_RING_COUNT - 1u);
        fm_gsi_tre_fill(ring + slot * FM_GSI_RING_ELEM_SIZE, cmds[idx].payload_pa, cmds[idx].opcode, 1,
            (idx + 1u) == count);
    }
    chan->tre_idx = (chan->tre_idx + count) & (FM_GSI_RING_COUNT - 1u);
    ipaCacheCleanInv(gIpaPoolVa, PAGE_SIZE);
    wr(&gIpaGsi, FM_GSI5_CH_DB_0(chan->ch_id), (uint32_t)chan->tre_pa + (chan->tre_idx * FM_GSI_RING_ELEM_SIZE));
    uint32_t hw_idx = chan->ev_idx;
    for (unsigned ms = 0u; ms < 200u; ++ms) {
        const uint32_t wp = rd(&gIpaGsi, FM_GSI5_EV_CNTXT_4(chan->ev_id));
        hw_idx = ((wp - (uint32_t)chan->ev_pa) / FM_GSI_RING_ELEM_SIZE) & (FM_GSI_RING_COUNT - 1u);
        if (hw_idx != chan->ev_idx) break;
        IOSleep(1u);
    }
    if (hw_idx == chan->ev_idx) {
        gIpaCmdErrs++;
        printf("FUXI MODEM: ipa cmd batch %s (%u) timeout errlog=%#x\n", label, count, rd(&gIpaGsi, FM_GSI5_ERROR_LOG));
        return -60;
    }
    ipaCacheCleanInv(chan->ev_va, FM_GSI_RING_BYTES);
    const uint8_t *ev_ring = (const uint8_t *)chan->ev_va;
    uint8_t last_code = 0u;
    while (chan->ev_idx != hw_idx) {
        last_code = ev_ring[chan->ev_idx * FM_GSI_RING_ELEM_SIZE + 11u];
        chan->ev_idx = (chan->ev_idx + 1u) & (FM_GSI_RING_COUNT - 1u);
    }
    wr(&gIpaGsi, FM_GSI5_EV_DB_0(chan->ev_id),
        (uint32_t)chan->ev_pa + (((chan->ev_idx - 1u) & (FM_GSI_RING_COUNT - 1u)) * FM_GSI_RING_ELEM_SIZE));
    const uint32_t errlog = rd(&gIpaGsi, FM_GSI5_ERROR_LOG);
    if (errlog || (last_code != 1u && last_code != 2u)) {
        gIpaCmdErrs++;
        printf("FUXI MODEM: ipa cmd batch %s (%u) code=%u errlog=%#x\n", label, count, last_code, errlog);
        return -5;
    }
    gIpaCmdDone += count;
    printf("FUXI MODEM: ipa cmd batch %s (%u) ok code=%u err=0\n", label, count, last_code);
    return 0;
}

static void ipaProgramEndpoints(void)
{
    /* ep 14: AP_COMMAND_TX (DMA -> ep 16 LAN_RX) */
    wr(&gIpaReg, FM_IPA5_ENDP_INIT_CFG(FM_IPA5_EP_CMD_TX), 0u);
    wr(&gIpaReg, FM_IPA5_ENDP_INIT_NAT(FM_IPA5_EP_CMD_TX), 0u);
    wr(&gIpaReg, FM_IPA5_ENDP_INIT_HDR(FM_IPA5_EP_CMD_TX), 0u);
    wr(&gIpaReg, FM_IPA5_ENDP_INIT_HDR_EXT(FM_IPA5_EP_CMD_TX), 0u);
    wr(&gIpaReg, FM_IPA5_ENDP_INIT_MODE(FM_IPA5_EP_CMD_TX), 3u | (FM_IPA5_EP_LAN_RX << 4));
    wr(&gIpaReg, FM_IPA5_ENDP_INIT_AGGR(FM_IPA5_EP_CMD_TX), 0u);
    wr(&gIpaReg, FM_IPA5_ENDP_INIT_DEAGGR(FM_IPA5_EP_CMD_TX), 0u);
    wr(&gIpaReg, FM_IPA5_ENDP_INIT_RSRC_GRP(FM_IPA5_EP_CMD_TX), 0u);
    wr(&gIpaReg, FM_IPA5_ENDP_INIT_SEQ(FM_IPA5_EP_CMD_TX), 0u);
    wr(&gIpaReg, FM_IPA5_ENDP_STATUS(FM_IPA5_EP_CMD_TX), 0u);

    /* ep 16: AP_LAN_RX (exception / default route) */
    wr(&gIpaReg, FM_IPA5_ENDP_INIT_CFG(FM_IPA5_EP_LAN_RX), 0u);
    wr(&gIpaReg, FM_IPA5_ENDP_INIT_HDR(FM_IPA5_EP_LAN_RX), 0u);
    wr(&gIpaReg, FM_IPA5_ENDP_INIT_HDR_EXT(FM_IPA5_EP_LAN_RX), 0x800u);
    wr(&gIpaReg, FM_IPA5_ENDP_INIT_HDR_METADATA_MASK(FM_IPA5_EP_LAN_RX), 0u);
    wr(&gIpaReg, FM_IPA5_ENDP_INIT_AGGR(FM_IPA5_EP_LAN_RX), 0x000050f1u);
    wr(&gIpaReg, FM_IPA5_ENDP_INIT_HOL_BLOCK_EN(FM_IPA5_EP_LAN_RX), 0u);
    wr(&gIpaReg, FM_IPA5_ENDP_INIT_RSRC_GRP(FM_IPA5_EP_LAN_RX), 0u);
    wr(&gIpaReg, FM_IPA5_ENDP_STATUS(FM_IPA5_EP_LAN_RX), 1u);

    /* ep 2: AP_MODEM_TX (QMAP v1, status -> ep 22 Q6_WAN_CONS, status_pkt_suppress=1;
     * 對照 SM8550 原廠 ipam.ko ipa3_setup_sys_pipe 0x53864..0x538bc 與 ipa3_ep_mapping[21] client 0x35) */
    wr(&gIpaReg, FM_IPA5_ENDP_INIT_CFG(FM_IPA5_EP_TO_MODEM), 0u);
    wr(&gIpaReg, FM_IPA5_ENDP_INIT_NAT(FM_IPA5_EP_TO_MODEM), 0u);
    wr(&gIpaReg, FM_IPA5_ENDP_INIT_HDR(FM_IPA5_EP_TO_MODEM), 0x44u);
    wr(&gIpaReg, FM_IPA5_ENDP_INIT_HDR_EXT(FM_IPA5_EP_TO_MODEM), 1u);
    wr(&gIpaReg, FM_IPA5_ENDP_INIT_MODE(FM_IPA5_EP_TO_MODEM), 0u);
    wr(&gIpaReg, FM_IPA5_ENDP_INIT_AGGR(FM_IPA5_EP_TO_MODEM), 0u);
    wr(&gIpaReg, FM_IPA5_ENDP_INIT_DEAGGR(FM_IPA5_EP_TO_MODEM), 0u);
    wr(&gIpaReg, FM_IPA5_ENDP_INIT_RSRC_GRP(FM_IPA5_EP_TO_MODEM), 0u);
    wr(&gIpaReg, FM_IPA5_ENDP_INIT_SEQ(FM_IPA5_EP_TO_MODEM), 4u);
    wr(&gIpaReg, FM_IPA5_ENDP_STATUS(FM_IPA5_EP_TO_MODEM), fm_ipa5_endp_status(1, FM_IPA5_EP_Q6_WAN_CONS, 1));

    /* ep 24: AP_MODEM_RX (QMAP v1 DL, client 0x23, GSI ch 1, rsrc grp 1 DST_DL) */
    wr(&gIpaReg, FM_IPA5_ENDP_INIT_CFG(FM_IPA5_EP_FROM_MODEM), 0u);
    wr(&gIpaReg, FM_IPA5_ENDP_INIT_HDR(FM_IPA5_EP_FROM_MODEM), 0x002800c4u);
    wr(&gIpaReg, FM_IPA5_ENDP_INIT_HDR_EXT(FM_IPA5_EP_FROM_MODEM), 0x0bu);
    wr(&gIpaReg, FM_IPA5_ENDP_INIT_HDR_METADATA_MASK(FM_IPA5_EP_FROM_MODEM), 0xff000000u);
    wr(&gIpaReg, FM_IPA5_ENDP_INIT_AGGR(FM_IPA5_EP_FROM_MODEM),
        ((gIpaMode & 0x800u) || gBbMode) ? FM_IPA5_AGGR_M7 : 0x008050f1u);
    wr(&gIpaReg, FM_IPA5_ENDP_INIT_HOL_BLOCK_EN(FM_IPA5_EP_FROM_MODEM), 0u);
    wr(&gIpaReg, FM_IPA5_ENDP_INIT_RSRC_GRP(FM_IPA5_EP_FROM_MODEM), 1u);
    wr(&gIpaReg, FM_IPA5_ENDP_STATUS(FM_IPA5_EP_FROM_MODEM), 0u);
}

static int ipaSetupGsi(void)
{
    if (!gIpaRegMapped) {
        if (fm_map(&gIpaReg, VM_WIMG_IO) != KERN_SUCCESS) return -12;
        gIpaRegMapped = 1u;
    }
    if (!gIpaGsiMapped) {
        if (fm_map(&gIpaGsi, VM_WIMG_IO) != KERN_SUCCESS) {
            printf("FUXI MODEM: gsi map failed\n");
            return -12;
        }
        gIpaGsiMapped = 1u;
    }
    if (!(rd(&gIpaGsi, FM_GSI5_STATUS) & 1u)) {
        printf("FUXI MODEM: gsi not enabled\n");
        return -5;
    }
    if (!gIpaPoolVa && ipaAllocDma(PAGE_SIZE, &gIpaPoolVa, &gIpaPoolPa)) {
        printf("FUXI MODEM: gsi pool alloc failed\n");
        return -12;
    }
    const vm_size_t rx_bytes = (vm_size_t)FM_GSI_RX_BUFS * FM_GSI_RX_BUF_SIZE;
    if (!gIpaLanRxVa && ipaAllocDma(rx_bytes, &gIpaLanRxVa, &gIpaLanRxPa)) {
        printf("FUXI MODEM: gsi lan_rx alloc failed\n");
        return -12;
    }
    if (!gIpaModemRxVa && ipaAllocDma(rx_bytes, &gIpaModemRxVa, &gIpaModemRxPa)) {
        printf("FUXI MODEM: gsi modem_rx alloc failed\n");
        return -12;
    }
    if (((gIpaMode & 0x800u) || gBbMode) && !gIpaModemTxVa) {
        const vm_size_t tx_bytes = (vm_size_t)FM_GSI_TX_BUFS * FM_GSI_TX_BUF_SIZE;
        if (ipaAllocDma(tx_bytes, &gIpaModemTxVa, &gIpaModemTxPa)) {
            printf("FUXI MODEM: gsi modem_tx alloc failed\n");
            return -12;
        }
    }

    /* 清中斷遮罩與錯誤紀錄（輪詢模式，不啟 GIC IRQ） */
    wr(&gIpaGsi, FM_GSI5_CNTXT_INTSET, 1u);
    wr(&gIpaGsi, FM_GSI5_CNTXT_TYPE_IRQ_MSK, 0u);
    wr(&gIpaGsi, FM_GSI5_CNTXT_SRC_CH_IRQ_MSK, 0u);
    wr(&gIpaGsi, FM_GSI5_CNTXT_SRC_EV_IRQ_MSK, 0u);
    wr(&gIpaGsi, FM_GSI5_CNTXT_SRC_IEOB_IRQ_MSK, 0u);
    wr(&gIpaGsi, FM_GSI5_CNTXT_GLOB_IRQ_EN, 0u);
    wr(&gIpaGsi, FM_GSI5_CNTXT_GSI_IRQ_EN, 0u);
    wr(&gIpaGsi, FM_GSI5_INTER_EE_CH_IRQ_MSK, 0u);
    wr(&gIpaGsi, FM_GSI5_INTER_EE_EV_IRQ_MSK, 0u);
    wr(&gIpaGsi, FM_GSI5_CNTXT_SRC_CH_IRQ_CLR, 0xffffffffu);
    wr(&gIpaGsi, FM_GSI5_CNTXT_SRC_EV_IRQ_CLR, 0xffffffffu);
    wr(&gIpaGsi, FM_GSI5_CNTXT_SRC_IEOB_IRQ_CLR, 0xffffffffu);
    wr(&gIpaGsi, FM_GSI5_CNTXT_GLOB_IRQ_CLR, 0xffffffffu);
    wr(&gIpaGsi, FM_GSI5_CNTXT_GSI_IRQ_CLR, 0xffffffffu);
    wr(&gIpaGsi, FM_GSI5_ERROR_LOG, 0u);
    wr(&gIpaGsi, FM_GSI5_ERROR_LOG_CLR, 0xffffffffu);

    /* 單頁 16 KiB 配置：+0x0000 4×TRE、+0x1000 4×EV、+0x2000 zero(4K)、+0x3000 table(1K)、+0x3400 cmds */
    static const struct { uint32_t ch, ev, ep; uint8_t tx, cmd; uint16_t max_tre; } kSpecs[4] = {
        {FM_IPA5_CH_CMD_TX, 0u, FM_IPA5_EP_CMD_TX, 1u, 1u, 19u},
        {FM_IPA5_CH_LAN_RX, 1u, FM_IPA5_EP_LAN_RX, 0u, 0u, 1u},
        {FM_IPA5_CH_MODEM_TX, 2u, FM_IPA5_EP_TO_MODEM, 1u, 0u, 24u},
        {FM_IPA5_CH_MODEM_RX, 3u, FM_IPA5_EP_FROM_MODEM, 0u, 0u, 1u},
    };
    for (unsigned idx = 0u; idx < 4u; ++idx) {
        struct fm_gsi_chan *chan = &gIpaChans[idx];
        memset(chan, 0, sizeof *chan);
        chan->ch_id = kSpecs[idx].ch;
        chan->ev_id = kSpecs[idx].ev;
        chan->ep_id = kSpecs[idx].ep;
        chan->toward_ipa = kSpecs[idx].tx;
        chan->is_cmd = kSpecs[idx].cmd;
        chan->max_tre = kSpecs[idx].max_tre;
        chan->tre_va = gIpaPoolVa + idx * FM_GSI_RING_BYTES;
        chan->tre_pa = gIpaPoolPa + (uint64_t)idx * FM_GSI_RING_BYTES;
        chan->ev_va = gIpaPoolVa + 0x1000u + idx * FM_GSI_RING_BYTES;
        chan->ev_pa = gIpaPoolPa + 0x1000ull + (uint64_t)idx * FM_GSI_RING_BYTES;
        if (idx == 1u) { chan->rx_va = gIpaLanRxVa; chan->rx_pa = gIpaLanRxPa; }
        if (idx == 3u) { chan->rx_va = gIpaModemRxVa; chan->rx_pa = gIpaModemRxPa; }
        if (ipaGsiAllocPair(chan)) return -5;
    }
    ipaProgramEndpoints();
    if (ipaGsiStartChan(&gIpaChans[0])) return -5;

    /* 準備 zero 緩衝區與 route/filter 預設表（128 B 對齊，首項 zero rule、次項 filtered bitmap、後續 19 個指向 zero rule） */
    const uint64_t zero_pa = gIpaPoolPa + 0x2000ull;
    uint8_t *table_va = (uint8_t *)(gIpaPoolVa + 0x3000u);
    const uint64_t table_pa = gIpaPoolPa + 0x3000ull;
    fm_put64(table_va + 0u, 0ull);
    fm_put64(table_va + 8u, FM_IPA5_FILTERED_MASK);
    for (uint32_t entry = 0u; entry < FM_IPA5_ROUTE_ENTRIES; ++entry) {
        fm_put64(table_va + 16u + entry * 8u, table_pa);
    }
    const uint64_t filter_pa = table_pa + 8ull;
    const uint64_t route_pa = table_pa + 16ull;

    /* 批次 1：4 條記憶體初始化立即命令（對照 Linux ipa_mem_setup） */
    uint8_t *cmd_base_va = (uint8_t *)(gIpaPoolVa + 0x3400u);
    const uint64_t cmd_base_pa = gIpaPoolPa + 0x3400ull;
    struct fm_ipa_cmd_desc mem_cmds[4];
    fm_ipa_cmd_hdr_init_local(cmd_base_va + 0u * 64u, zero_pa, gIpaMemOffset + FM_IPA5_MEM_MODEM_HDR_OFF,
        (uint16_t)(FM_IPA5_MEM_MODEM_HDR_SIZE + FM_IPA5_MEM_AP_HDR_SIZE));
    mem_cmds[0].opcode = FM_IPA_CMD_HDR_INIT_LOCAL; mem_cmds[0].payload_pa = cmd_base_pa + 0ull * 64ull;
    fm_ipa_cmd_dma_shared_mem(cmd_base_va + 1u * 64u, zero_pa, gIpaMemOffset + FM_IPA5_MEM_MODEM_CTX_OFF,
        FM_IPA5_MEM_MODEM_CTX_SIZE, 1);
    mem_cmds[1].opcode = FM_IPA_CMD_DMA_SHARED_MEM; mem_cmds[1].payload_pa = cmd_base_pa + 1ull * 64ull;
    fm_ipa_cmd_dma_shared_mem(cmd_base_va + 2u * 64u, zero_pa, gIpaMemOffset + FM_IPA5_MEM_AP_CTX_OFF,
        FM_IPA5_MEM_AP_CTX_SIZE, 1);
    mem_cmds[2].opcode = FM_IPA_CMD_DMA_SHARED_MEM; mem_cmds[2].payload_pa = cmd_base_pa + 2ull * 64ull;
    fm_ipa_cmd_dma_shared_mem(cmd_base_va + 3u * 64u, zero_pa, gIpaMemOffset + FM_IPA5_MEM_MODEM_OFF,
        FM_IPA5_MEM_MODEM_SIZE, 1);
    mem_cmds[3].opcode = FM_IPA_CMD_DMA_SHARED_MEM; mem_cmds[3].payload_pa = cmd_base_pa + 3ull * 64ull;
    if (ipaRunCmdBatch("mem", mem_cmds, 4u)) return -5;
    wr(&gIpaReg, FM_IPA5_LOCAL_PKT_PROC_CNTXT, gIpaMemOffset + FM_IPA5_MEM_MODEM_CTX_OFF);

    /* 批次 2：8 條路由／過濾表初始化立即命令（對照 Linux ipa_table_setup） */
    struct fm_ipa_cmd_desc tbl_cmds[8];
    fm_ipa_cmd_table_init(cmd_base_va + 4u * 64u, route_pa, gIpaMemOffset + FM_IPA5_MEM_V4_RT_OFF, FM_IPA5_MEM_RT_SIZE,
        route_pa, gIpaMemOffset + FM_IPA5_MEM_V4_RT_HASH_OFF, FM_IPA5_MEM_RT_SIZE);
    tbl_cmds[0].opcode = FM_IPA_CMD_V4_ROUTING_INIT; tbl_cmds[0].payload_pa = cmd_base_pa + 4ull * 64ull;
    fm_ipa_cmd_table_init(cmd_base_va + 5u * 64u, route_pa, gIpaMemOffset + FM_IPA5_MEM_V6_RT_OFF, FM_IPA5_MEM_RT_SIZE,
        route_pa, gIpaMemOffset + FM_IPA5_MEM_V6_RT_HASH_OFF, FM_IPA5_MEM_RT_SIZE);
    tbl_cmds[1].opcode = FM_IPA_CMD_V6_ROUTING_INIT; tbl_cmds[1].payload_pa = cmd_base_pa + 5ull * 64ull;
    fm_ipa_cmd_table_init(cmd_base_va + 6u * 64u, filter_pa, gIpaMemOffset + FM_IPA5_MEM_V4_FLT_OFF, FM_IPA5_FILTER_INIT_SIZE,
        filter_pa, gIpaMemOffset + FM_IPA5_MEM_V4_FLT_HASH_OFF, FM_IPA5_FILTER_INIT_SIZE);
    tbl_cmds[2].opcode = FM_IPA_CMD_V4_FILTER_INIT; tbl_cmds[2].payload_pa = cmd_base_pa + 6ull * 64ull;
    fm_ipa_cmd_dma_shared_mem(cmd_base_va + 7u * 64u, zero_pa, gIpaMemOffset + FM_IPA5_MEM_V4_FLT_OFF + FM_IPA5_FILTER_INIT_SIZE,
        FM_IPA5_FILTER_TAIL_SIZE, 1);
    tbl_cmds[3].opcode = FM_IPA_CMD_DMA_SHARED_MEM; tbl_cmds[3].payload_pa = cmd_base_pa + 7ull * 64ull;
    fm_ipa_cmd_dma_shared_mem(cmd_base_va + 8u * 64u, zero_pa,
        gIpaMemOffset + FM_IPA5_MEM_V4_FLT_HASH_OFF + FM_IPA5_FILTER_INIT_SIZE, FM_IPA5_FILTER_TAIL_SIZE, 1);
    tbl_cmds[4].opcode = FM_IPA_CMD_DMA_SHARED_MEM; tbl_cmds[4].payload_pa = cmd_base_pa + 8ull * 64ull;
    fm_ipa_cmd_table_init(cmd_base_va + 9u * 64u, filter_pa, gIpaMemOffset + FM_IPA5_MEM_V6_FLT_OFF, FM_IPA5_FILTER_INIT_SIZE,
        filter_pa, gIpaMemOffset + FM_IPA5_MEM_V6_FLT_HASH_OFF, FM_IPA5_FILTER_INIT_SIZE);
    tbl_cmds[5].opcode = FM_IPA_CMD_V6_FILTER_INIT; tbl_cmds[5].payload_pa = cmd_base_pa + 9ull * 64ull;
    fm_ipa_cmd_dma_shared_mem(cmd_base_va + 10u * 64u, zero_pa, gIpaMemOffset + FM_IPA5_MEM_V6_FLT_OFF + FM_IPA5_FILTER_INIT_SIZE,
        FM_IPA5_FILTER_TAIL_SIZE, 1);
    tbl_cmds[6].opcode = FM_IPA_CMD_DMA_SHARED_MEM; tbl_cmds[6].payload_pa = cmd_base_pa + 10ull * 64ull;
    fm_ipa_cmd_dma_shared_mem(cmd_base_va + 11u * 64u, zero_pa,
        gIpaMemOffset + FM_IPA5_MEM_V6_FLT_HASH_OFF + FM_IPA5_FILTER_INIT_SIZE, FM_IPA5_FILTER_TAIL_SIZE, 1);
    tbl_cmds[7].opcode = FM_IPA_CMD_DMA_SHARED_MEM; tbl_cmds[7].payload_pa = cmd_base_pa + 11ull * 64ull;
    if (ipaRunCmdBatch("table", tbl_cmds, 8u)) return -5;

    if (ipaGsiStartChan(&gIpaChans[1])) return -5;
    ipaGsiPostRxAll(&gIpaChans[1]);
    wr(&gIpaReg, FM_IPA5_ROUTE, fm_ipa_route_reg(FM_IPA5_EP_LAN_RX));
    if (ipaGsiStartChan(&gIpaChans[2]) || ipaGsiStartChan(&gIpaChans[3])) return -5;
    ipaGsiPostRxAll(&gIpaChans[3]);
    gIpaGsiReady = 1u;
    printf("FUXI MODEM: gsi v5.0 ready 4ch(12/13/11/1) cmds=%u errs=%u route=%#x status2=%#x errlog=%#x\n",
        gIpaCmdDone, gIpaCmdErrs, fm_ipa_route_reg(FM_IPA5_EP_LAN_RX),
        rd(&gIpaReg, FM_IPA5_ENDP_STATUS(FM_IPA5_EP_TO_MODEM)), rd(&gIpaGsi, FM_GSI5_ERROR_LOG));
    return 0;
}

/* ---- M7 BSD ifnet en1（對照 fuxi_wifi_net.c 與 hakios.cell.c；絕不輸出 IP/GW/DNS 數值） ---- */
static void cellLinkEvent(ifnet_t ifp, uint32_t code)
{
    _Alignas(struct kern_event_msg) char message[sizeof(struct kern_event_msg) + sizeof(struct net_event_data)] = {0};
    struct kern_event_msg *header = (struct kern_event_msg *)message;
    struct net_event_data *about = (struct net_event_data *)(header + 1);
    header->total_size = sizeof(message);
    header->vendor_code = KEV_VENDOR_APPLE;
    header->kev_class = KEV_NETWORK_CLASS;
    header->kev_subclass = KEV_DL_SUBCLASS;
    header->event_code = code;
    about->if_family = ifnet_family(ifp);
    about->if_unit = ifnet_unit(ifp);
    strlcpy(about->if_name, ifnet_name(ifp), IFNAMSIZ);
    ifnet_event(ifp, header);
}

static void cellSetLinkState(int up)
{
    const uint8_t want = up ? 1u : 0u;
    if (!gCellIfp || want == __atomic_load_n(&gCellLinkUp, __ATOMIC_ACQUIRE)) return;
    __atomic_store_n(&gCellLinkUp, want, __ATOMIC_RELEASE);
    printf("FUXI MODEM: en1 link %s\n", want ? "up" : "down");
    cellLinkEvent(gCellIfp, want ? KEV_DL_LINK_ON : KEV_DL_LINK_OFF);
}

static void cellSetSessionDown(void)
{
    lck_mtx_lock(&gCellSessLock);
    bzero(&gCellSession, sizeof gCellSession);
    lck_mtx_unlock(&gCellSessLock);
    cellSetLinkState(0);
}

static void cellInputFrame(const uint8_t *frame, size_t length)
{
    if (!gCellIfp || !__atomic_load_n(&gCellLinkUp, __ATOMIC_ACQUIRE) ||
        length < ETHER_HDR_LEN || length > FM_CELL_FRAME_MAX) {
        __atomic_fetch_add(&gCellRxDrop, 1u, __ATOMIC_RELAXED);
        return;
    }
    unsigned int chunks = 1;
    mbuf_t packet = NULL;
    if (mbuf_allocpacket(MBUF_DONTWAIT, length, &chunks, &packet) != 0) {
        __atomic_fetch_add(&gCellRxDrop, 1u, __ATOMIC_RELAXED);
        return;
    }
    if (mbuf_copyback(packet, 0, length, frame, MBUF_DONTWAIT) != 0) {
        mbuf_freem(packet);
        __atomic_fetch_add(&gCellRxDrop, 1u, __ATOMIC_RELAXED);
        return;
    }
    char *header = mbuf_data(packet);
    mbuf_pkthdr_setrcvif(packet, gCellIfp);
    mbuf_pkthdr_setheader(packet, header);
    mbuf_adj(packet, ETHER_HDR_LEN);
    bpf_tap_in(gCellIfp, DLT_EN10MB, packet, header, ETHER_HDR_LEN);
    struct ifnet_stat_increment_param stats = {.packets_in = 1, .bytes_in = (uint32_t)length};
    ifnet_input(gCellIfp, packet, &stats);
}

static void cellInputIpv4(const uint8_t *ip, uint32_t ip_len)
{
    uint8_t frame[FM_CELL_FRAME_MAX];
    if (ip_len < 20u || ETHER_HDR_LEN + ip_len > sizeof frame) {
        __atomic_fetch_add(&gCellRxDrop, 1u, __ATOMIC_RELAXED);
        return;
    }
    memcpy(frame, kCellMac, 6u);
    memcpy(frame + 6u, kCellRouterMac, 6u);
    fm_put_be16(frame + 12u, 0x0800u);
    memcpy(frame + ETHER_HDR_LEN, ip, ip_len);
    __atomic_fetch_add(&gCellRxIpv4, 1u, __ATOMIC_RELAXED);
    cellInputFrame(frame, (size_t)ETHER_HDR_LEN + ip_len);
}

static void ipaHandleModemRx(const uint8_t *buf, uint32_t len)
{
    uint32_t pos = 0u;
    while (pos < len) {
        FmQmapPacket pkt;
        if (fm_qmap_parse(buf, len, pos, &pkt) != 0) {
            gCellRxQmapBad++;
            break;
        }
        const uint8_t *payload = buf + pkt.payload_off;
        if (pkt.is_cmd) {
            gCellRxQmapCmds++;
            FmQmapCmd cmd;
            if (fm_qmap_cmd_parse(payload, pkt.payload_len, &cmd) == 0) {
                if (gCellRxQmapCmds <= 16u) {
                    printf("FUXI MODEM: qmap cmd mux=%u name=%u type=%u txn=%u\n",
                        pkt.mux_id, cmd.cmd_name, cmd.cmd_type, cmd.transaction_id);
                }
                if (((gIpaMode & 0x1000u) || gBbMode) && cmd.cmd_type == FM_QMAP_CMD_TYPE_REQUEST) {
                    uint8_t ack[16];
                    const uint32_t ack_len = fm_qmap_cmd_ack(ack, sizeof ack, pkt.mux_id, &cmd);
                    if (ack_len && ipaModemTxSubmit(0, pkt.mux_id, ack, ack_len) == 0) gCellTxQmapAcks++;
                }
            }
        } else if (gBbMode) {
            const uint32_t ver = pkt.payload_len ? (uint32_t)(payload[0] >> 4) : 0u;
            if (gBbDataRx && ((ver == 4u && pkt.payload_len >= 20u) || (ver == 6u && pkt.payload_len >= 40u))) {
                gBbRxPackets++;
                gBbDataRx(pkt.mux_id, payload, pkt.payload_len, ver);
            } else {
                gBbRxDrops++;
            }
        } else if (pkt.mux_id == FM_DATA_MUX_ID && pkt.payload_len >= 20u && (payload[0] >> 4) == 4u) {
            cellInputIpv4(payload, pkt.payload_len);
        } else {
            __atomic_fetch_add(&gCellRxDrop, 1u, __ATOMIC_RELAXED);
        }
        pos = pkt.next_off;
    }
}

static errno_t cellOutput(ifnet_t ifp, mbuf_t packet)
{
    const size_t length = mbuf_pkthdr_len(packet);
    int sent = 0;
    uint8_t frame[FM_CELL_FRAME_MAX];
    if (length >= ETHER_HDR_LEN && length <= sizeof frame &&
        __atomic_load_n(&gCellLinkUp, __ATOMIC_ACQUIRE) &&
        mbuf_copydata(packet, 0, length, frame) == 0) {
        bpf_tap_out(ifp, DLT_EN10MB, packet, NULL, 0);
        FmCellSession sess;
        lck_mtx_lock(&gCellSessLock);
        sess = gCellSession;
        lck_mtx_unlock(&gCellSessLock);
        const uint16_t ethertype = fm_be16(frame + 12u);
        uint32_t udp_off = 0u;
        if (ethertype == 0x0806u) {
            uint8_t reply[64];
            const uint32_t rlen = fm_cell_arp_reply(&sess, kCellRouterMac, frame, (uint32_t)length, reply, sizeof reply);
            if (rlen) {
                __atomic_fetch_add(&gCellArpReplies, 1u, __ATOMIC_RELAXED);
                cellInputFrame(reply, rlen);
                sent = 1;
            }
        } else if (ethertype == 0x0800u && fm_cell_is_dhcp_request(frame, (uint32_t)length, &udp_off)) {
            uint8_t reply[384];
            uint8_t kind = 0u;
            const uint32_t rlen = fm_cell_dhcp_reply(&sess, kCellRouterMac, frame, (uint32_t)length, udp_off,
                reply, sizeof reply, &kind);
            if (rlen) {
                const uint32_t count = __atomic_fetch_add(&gCellDhcpReplies, 1u, __ATOMIC_RELAXED) + 1u;
                if (count <= 16u) {
                    printf("FUXI MODEM: en1 dhcp reply kind=%u mtu=%u\n", kind, sess.mtu);
                }
                cellInputFrame(reply, rlen);
                sent = 1;
            }
        } else if (ethertype == 0x0800u && sess.up) {
            if (ipaModemTxSubmit(1, FM_DATA_MUX_ID, frame + ETHER_HDR_LEN, (uint32_t)(length - ETHER_HDR_LEN)) == 0) {
                __atomic_fetch_add(&gCellTxIpv4, 1u, __ATOMIC_RELAXED);
                sent = 1;
            } else {
                __atomic_fetch_add(&gCellTxDrop, 1u, __ATOMIC_RELAXED);
            }
        } else {
            __atomic_fetch_add(&gCellTxNonIpv4, 1u, __ATOMIC_RELAXED);
        }
    }
    if (sent) {
        ifnet_stat_increment_out(ifp, 1, (uint32_t)length, 0);
    } else {
        ifnet_stat_increment_out(ifp, 0, 0, 1);
    }
    mbuf_freem(packet);
    return 0;
}

static errno_t cellIoctl(ifnet_t ifp, u_long command, void *data)
{
    const int media[] = {IFM_ETHER | IFM_AUTO};
    struct ifreq *request = (struct ifreq *)data;
    struct ifmediareq *media_request = (struct ifmediareq *)data;
    user_addr_t list;
    errno_t error = 0;
    switch (command) {
    case SIOCSIFADDR:
        ifnet_set_flags(ifp, IFF_UP, IFF_UP);
        break;
    case SIOCSIFFLAGS:
        ifnet_set_flags(ifp, (ifnet_flags(ifp) & IFF_UP) ? IFF_RUNNING : 0, IFF_RUNNING);
        break;
    case SIOCGIFMEDIA32:
    case SIOCGIFMEDIA64:
        list = command == SIOCGIFMEDIA64 ? ((struct ifmediareq64 *)media_request)->ifmu_ulist :
            CAST_USER_ADDR_T(((struct ifmediareq32 *)media_request)->ifmu_ulist);
        media_request->ifm_active = media_request->ifm_current = media[0];
        media_request->ifm_mask = 0;
        media_request->ifm_status = IFM_AVALID | (__atomic_load_n(&gCellLinkUp, __ATOMIC_ACQUIRE) ? IFM_ACTIVE : 0);
        if (list != USER_ADDR_NULL && media_request->ifm_count > 0) {
            error = copyout(media, list, sizeof(media));
        }
        media_request->ifm_count = 1;
        break;
    case SIOCGIFDEVMTU:
        request->ifr_devmtu.ifdm_current = ifnet_mtu(ifp);
        request->ifr_devmtu.ifdm_max = ETHERMTU;
        request->ifr_devmtu.ifdm_min = IF_MINMTU;
        break;
    case SIOCSIFMTU:
        error = (request->ifr_mtu > ETHERMTU || request->ifr_mtu < IF_MINMTU) ? EINVAL : ifnet_set_mtu(ifp, request->ifr_mtu);
        break;
    case SIOCADDMULTI:
    case SIOCDELMULTI:
        break;
    default:
        error = EOPNOTSUPP;
        break;
    }
    return error;
}

static void cellAttachIfnet(void)
{
    if (!(gIpaMode & 0x800u) || gBbMode || gCellAttached || gCellAttachFailed || !gIpaGsiReady || !gIpaIndSent) return;
    struct ifnet_init_eparams init = {
        .ver = IFNET_INIT_CURRENT_VERSION,
        .len = sizeof(init),
        .flags = IFNET_INIT_LEGACY | IFNET_INIT_NX_NOAUTO,
        .uniqueid = "fuxi-modem-en1",
        .uniqueid_len = sizeof("fuxi-modem-en1") - 1,
        .name = "en",
        .unit = 1,
        .family = IFNET_FAMILY_ETHERNET,
        .subfamily = IFNET_SUBFAMILY_ANY,
        .type = IFT_ETHER,
        .output = cellOutput,
        .demux = ether_demux,
        .add_proto = ether_add_proto,
        .del_proto = ether_del_proto,
        .check_multi = ether_check_multi,
        .framer_extended = ether_frameout_extended,
        .ioctl = cellIoctl,
        .broadcast_addr = etherbroadcastaddr,
        .broadcast_len = ETHER_ADDR_LEN,
    };
    ifnet_t ifp = NULL;
    errno_t error = ifnet_allocate_extended(&init, &ifp);
    if (error != 0) {
        gCellAttachFailed = 1u;
        printf("FUXI MODEM: en1 allocate failed err=%d\n", error);
        return;
    }
    ifnet_set_capabilities_enabled(ifp, 0, -1);
    ifnet_set_addrlen(ifp, ETHER_ADDR_LEN);
    ifnet_set_baudrate(ifp, 100 * 1000 * 1000);
    ifnet_set_mtu(ifp, 1400u);
    ifnet_set_flags(ifp, IFF_BROADCAST | IFF_MULTICAST | IFF_SIMPLEX, 0xffff);
    ifnet_set_hdrlen(ifp, ETHER_HDR_LEN);
    ifnet_set_offload(ifp, 0);
    error = ifnet_attach(ifp, NULL);
    if (error != 0) {
        gCellAttachFailed = 1u;
        ifnet_release(ifp);
        printf("FUXI MODEM: en1 attach failed err=%d\n", error);
        return;
    }
    ifnet_set_lladdr(ifp, kCellMac, ETHER_ADDR_LEN);
    bpfattach(ifp, DLT_EN10MB, ETHER_HDR_LEN);
    gCellIfp = ifp;
    gCellAttached = 1u;
    uint8_t sess_up = 0u;
    uint32_t sess_mtu = 1400u;
    lck_mtx_lock(&gCellSessLock);
    sess_up = gCellSession.up;
    if (gCellSession.mtu >= 576u && gCellSession.mtu <= ETHERMTU) sess_mtu = gCellSession.mtu;
    lck_mtx_unlock(&gCellSessLock);
    if (sess_up) {
        (void)ifnet_set_mtu(ifp, sess_mtu);
        cellSetLinkState(1);
    }
    printf("FUXI MODEM: en1 attached aggr=%#x tx_bufs=%u link=%u\n", FM_IPA5_AGGR_M7, FM_GSI_TX_BUFS, sess_up);
}

static void ipaProbeFirmwareInfo(void)
{
    static const char *const kPaths[3] = {"image/ipa_fws.b00", "image/ipa_fws.mdt", "image/ipa_fws.elf"};
    uint64_t sizes[3] = {0, 0, 0};
    int rcs[3] = {0, 0, 0};
    for (unsigned idx = 0; idx < 3u; ++idx) rcs[idx] = fuxi_modem_fw_stat(kPaths[idx], &sizes[idx]);
    printf("FUXI MODEM: survey ipa_fws b00=%d(%llu) mdt=%d(%llu) elf=%d(%llu)\n",
        rcs[0], (unsigned long long)sizes[0], rcs[1], (unsigned long long)sizes[1], rcs[2], (unsigned long long)sizes[2]);
    if (openImage(&gIpaImage, "ipa_fws") == 0) {
        (void)checkFit("ipa_fws", &gIpaImage, FM_IPA_FW_BASE, FM_IPA_FW_SIZE);
        for (unsigned seg_idx = 0; seg_idx < gIpaImage.mdt.count; ++seg_idx) {
            const QcomMdtSegment *seg = &gIpaImage.mdt.segment[seg_idx];
            if (!seg->load) continue;
            printf("FUXI MODEM: ipa_fws seg[%u] pa=%#llx mem=%#llx file=%#llx flags=%#x\n",
                seg_idx, (unsigned long long)seg->address, (unsigned long long)seg->memory_size,
                (unsigned long long)seg->file_size, seg->flags);
        }
        release(&gIpaImage);
    }
}

static void ipaBringup(void)
{
    if (!(gIpaMode & 0x3e0u)) return;
    if (gIpaMode & 0x80u) ipaAttachSmmu();
    if (gIpaMode & 0x100u) {
        if (gBcmSn8) (void)voteBcm("SN8", gBcmSn8, 1);
        if (gBcmSh0) (void)voteBcm("SH0", gBcmSh0, 1);
        if (gBcmMc0) (void)voteBcm("MC0", gBcmMc0, 1);
        if (gBcmAcv) (void)voteBcm("ACV", gBcmAcv, 1);
    }
    if (!(gIpaMode & 0x20u)) return;
    if (!gBcmIp0) {
        printf("FUXI MODEM: ipa skipped (IP0 BCM missing in cmd-db)\n");
        return;
    }
    if (voteBcm("IP0", gBcmIp0, 1) != 0) {
        printf("FUXI MODEM: ipa aborted (IP0 vote failed; refusing MMIO/PAS)\n");
        return;
    }
    (void)ipaReadRegs("pre");
    if ((gIpaMode & 0x200u) && ipaConfigHw() != 0) return;
    if (!(gIpaMode & 0x40u)) return;
    int rc = openImage(&gIpaImage, "ipa_fws");
    if (rc) {
        printf("FUXI MODEM: ipa_fws open failed rc=%d\n", rc);
        return;
    }
    if (checkFit("ipa_fws", &gIpaImage, FM_IPA_FW_BASE, FM_IPA_FW_SIZE)) {
        printf("FUXI MODEM: ipa_fws does not fit region; skipping PAS 0x0f\n");
        release(&gIpaImage);
        return;
    }
    for (unsigned seg_idx = 0; seg_idx < gIpaImage.mdt.count; ++seg_idx) {
        const QcomMdtSegment *seg = &gIpaImage.mdt.segment[seg_idx];
        uint64_t at = 0;
        if (!seg->load) continue;
        if (qcom_mdt_place(&gIpaImage.mdt, seg_idx, FM_IPA_FW_BASE, FM_IPA_FW_SIZE, &at) ||
            round_page_64(FM_IPA_FW_BASE + at + seg->memory_size) > FM_IPA_FW_SAFE_END) {
            printf("FUXI MODEM: ipa_fws seg[%u] end %#llx reaches GPU zap page; refusing load\n",
                seg_idx, (unsigned long long)(FM_IPA_FW_BASE + at + seg->memory_size));
            release(&gIpaImage);
            return;
        }
    }
    rc = authenticateHeader(&gIpaImage, FM_PAS_IPA, FM_IPA_FW_BASE);
    if (!rc) rc = loadSegments(&gIpaImage, FM_IPA_FW_BASE, FM_IPA_FW_SIZE);
    if (!rc && !scmOk(pil(QCOM_SCM_PAS_AUTH_AND_RESET, qcom_scm_args(1, 0, 0, 0), FM_PAS_IPA, 0, 0))) rc = -3;
    release(&gIpaImage);
    printf("FUXI MODEM: ipa_fws pas 0x0f rc=%d scm=%lld/%#llx\n", rc, (long long)gScmX0, (unsigned long long)gScmX1);
    if (!rc) {
        IOSleep(25);
        const uint32_t gsi_status = ipaReadRegs("post");
        if ((gIpaMode & 0x200u) && (gsi_status & 1u)) (void)ipaSetupGsi();
    }
}

/* ---- rmtfs：EFS 分區整份讀進記憶體、共用緩衝區 ---- */
static void loadEfs(void)
{
    fm_rmtfs_init(&gRmtfs);
    for (unsigned f = 0; f < FM_RMTFS_FILES; ++f) {
        FmRmtfsFile *file = &gRmtfs.file[f];
        uint32_t lun = 0;
        uint64_t first = 0, blocks = 0;
        if (!fuxi_ufs_part_find(file->partition, &lun, &first, &blocks) || !blocks || blocks * 4096u > (16u << 20)) {
            printf("FUXI MODEM: rmtfs partition %s: not found or too big (%llu blocks)\n", file->partition, blocks);
            continue;
        }
        const uint64_t bytes = blocks * 4096u;
        uint8_t *data = kalloc_data((vm_size_t)bytes, Z_WAITOK | Z_ZERO);
        int ok = data != NULL;
        for (uint64_t done = 0; ok && done < blocks;) {
            const uint32_t n = blocks - done > 256 ? 256u : (uint32_t)(blocks - done);
            ok = fuxi_ufs_part_read(lun, first + done, n, data + done * 4096u);
            done += n;
        }
        if (!ok) {
            if (data) kfree_data(data, (vm_size_t)bytes);
            printf("FUXI MODEM: rmtfs partition %s: read failed\n", file->partition);
            continue;
        }
        file->data = data;
        file->size = bytes;
        printf("FUXI MODEM: rmtfs %s <- %s (LUN %u, %llu KiB, in memory; writes are not stored)\n", file->path, file->partition, lun,
            bytes >> 10);
    }
}
static int shareRmtfsBuffer(void)
{
    gRmtfsW.pa = adtU32("fuxi,modem-rmtfs-pa", (uint32_t)FM_RMTFS_BASE);
    if (fm_map(&gRmtfsW, VM_WIMG_WCOMB) != KERN_SUCCESS) return -12;
    bzero((void *)gRmtfsW.va, gRmtfsW.size);
    __asm__ volatile("dsb sy" ::: "memory");
    const uint32_t sources[1] = {FM_VMID_HLOS}, vmids[2] = {FM_VMID_HLOS, FM_VMID_MSS_MSA}, perms[2] = {FM_PERM_RW, FM_PERM_RW};
    const int rc = assignMemory(gRmtfsW.pa, gRmtfsW.size, sources, 1, vmids, perms, 2);
    gRmtfs.window = (uint8_t *)gRmtfsW.va;
    gRmtfs.pa = gRmtfsW.pa;
    gRmtfs.size = FM_RMTFS_SIZE;
    gRmtfs.shared = rc == 0;
    printf("FUXI MODEM: rmtfs buffer %#llx + %#x assigned to HLOS+MSS_MSA rc=%d\n", gRmtfsW.pa, FM_RMTFS_SIZE, rc);
    return rc;
}

/* ---- 唯讀探測 ---- */
static void survey(void)
{
    uint32_t bits = 0;
    gPartOk = !qcom_smem_partition(&gSmem, QCOM_SMEM_HOST_APPS, FM_REMOTE_PID, &gPart);
    if (gPartOk) {
        bits |= 1u << 8;
        const uint16_t ids[5] = {FM_ITEM_IN, FM_ITEM_OUT, FM_ITEM_GLINK_DESC, FM_ITEM_GLINK_TX, FM_ITEM_GLINK_RX};
        for (unsigned i = 0; i < 5; ++i) if (item(ids[i], NULL)) bits |= 1u << i;
    }
    QcomSmemPartition global;
    if (!qcom_smem_partition(&gSmem, QCOM_SMEM_HOST_GLOBAL, QCOM_SMEM_HOST_GLOBAL, &global)) bits |= 1u << 9;
    gSlaveKnown = !slaveValue(FM_REMOTE_PID, FM_ITEM_IN, &gSlave);
    uint32_t adsp = 0;
    const int adsp_known = !slaveValue(FM_ADSP_PID, FM_ADSP_ITEM_IN, &adsp);
    printf("FUXI MODEM: survey items=%#x (bit0 smp2p-in 1 out 2-4 glink 8 partition 9 global) slave-kernel=%s%#x adsp=%s%#x "
        "aoss=%#x gic_ipcc_en=%d pas modem=%d dtb=%d ipa=%d adsp=%d\n", bits, gSlaveKnown ? "" : "none/", gSlave,
        adsp_known ? "" : "none/", adsp, rd(&gAoss, 0), gicEnabled(FM_INTID_IPCC), pasSupported(FM_PAS_MODEM),
        pasSupported(FM_PAS_DTB), pasSupported(FM_PAS_IPA), pasSupported(1u)); /* ADSP（PAS 1）能開機，當 IS_SUPPORTED 的對照 */
    readCrashReason();
    static const char *const efs[4] = {"modemst1", "modemst2", "fsc", "fsg"};
    for (unsigned i = 0; i < 4; ++i) {
        uint32_t lun = 0;
        uint64_t first = 0, blocks = 0;
        if (!fuxi_ufs_part_find(efs[i], &lun, &first, &blocks)) printf("FUXI MODEM: EFS %s missing\n", efs[i]);
    }
    lookupRails(0);
    ipaProbeFirmwareInfo();
}

/* ---- SMP2P 輪詢 ---- */
static void serviceSmp2p(void)
{
    if (!gP2p.out) return;
    if (!gP2p.in) {
        volatile uint8_t *in = item(FM_ITEM_IN, NULL);
        if (!in) return;
        gP2p.in = (volatile uint32_t *)in;
    }
    uint32_t value = 0, changed = 0;
    if (qcom_smp2p_service(&gP2p, "slave-kernel", &value, &changed)) kickSmp2p();
    if (value != gSlave || !gSlaveKnown) {
        printf("FUXI MODEM: slave-kernel %#x -> %#x at %u ms\n", gSlave, value, gBegan ? msSince(gBegan) : 0);
        uint64_t window;
        clock_interval_to_deadline(FM_FAST_MS, NSEC_PER_MSEC, &window);
        gFastUntil = window;
    }
    gSlave = value;
    gSlaveKnown = 1;
    if (changed & value & FM_FATAL) readCrashReason();
    if ((value & FM_HANDOVER) && !gHandedOver) {
        gHandedOver = 1;
        if (gVoted) voteMss(0);
    }
    if (gIpaMode & 0x0fu) {
        uint32_t ipa_val = 0, ipa_chg = 0;
        if (qcom_smp2p_service(&gP2p, "ipa", &ipa_val, &ipa_chg)) kickSmp2p();
        if ((ipa_val != gIpaInVal || !gIpaInKnown) && (ipa_val || gIpaInKnown)) {
            printf("FUXI MODEM: smp2p-in \"ipa\" %#x -> %#x at %u ms\n", gIpaInVal, ipa_val, gBegan ? msSince(gBegan) : 0);
            gIpaInVal = ipa_val;
            gIpaInKnown = 1;
        }
    }
}

/* ---- GLINK／QRTR（模式 3） ---- */
static int queueSend(void *context, const uint8_t *packet, uint32_t length)
{
    (void)context;
    if (gQueueCount == FM_QUEUE || length > FM_QUEUE_BYTES) { gQueueDropped++; return -1; }
    const uint32_t slot = (gQueueHead + gQueueCount) % FM_QUEUE;
    memcpy(gQueue[slot].data, packet, length);
    gQueue[slot].length = length;
    gQueueCount++;
    return 0;
}
static void pumpSends(void)
{
    while (gQueueCount && gIpcrtr && gIpcrtr->up) {
        const int rc = qcom_glink_send(&gGlink, gIpcrtr, gQueue[gQueueHead].data, gQueue[gQueueHead].length);
        if (rc == QcomGlinkAgain) break;
        if (rc != QcomGlinkOk) gQueueDropped++;
        gQueueHead = (gQueueHead + 1) % FM_QUEUE;
        gQueueCount--;
    }
}
static void hexAppend(uint32_t value)
{
    char digits[8];
    unsigned n = 0;
    do { digits[n++] = "0123456789abcdef"[value & 15]; value >>= 4; } while (value && n < 8);
    while (n && gServerTextAt + 1 < sizeof(gServerText)) gServerText[gServerTextAt++] = digits[--n];
    gServerText[gServerTextAt] = 0;
}
static void flushServers(void)
{
    if (!gServerTextAt) return;
    if (gServerLines++ < 40) printf("FUXI MODEM: qrtr servers [%s]\n", gServerText);
    gServerTextAt = 0;
    gServerText[0] = 0;
}
/* modem 公告的服務（service:instance@port，十六進位），湊滿一行才印。 */
static void noteServer(const uint8_t *packet, uint32_t length)
{
    if (length < FM_QRTR_HEADER + 20 || fm_le32(packet) != 1u) return;
    const uint32_t type = fm_le32(packet + 4);
    if (type != FM_QRTR_NEW_SERVER && type != FM_QRTR_DEL_SERVER) return;
    const uint8_t *w = packet + FM_QRTR_HEADER + 4;
    const uint32_t service = fm_le32(w), instance = fm_le32(w + 4), node = fm_le32(w + 8), port = fm_le32(w + 12);
    if (type == FM_QRTR_DEL_SERVER) {
        if (gBbMode && (service || port)) coachz_baseband_del_server(service, node, port);
        return;
    }
    if (!service && !instance) return;
    if (gBbMode) coachz_baseband_new_server(service, instance, node, port);
    for (unsigned idx = 0; idx < sizeof(gQmiTargets) / sizeof(gQmiTargets[0]); ++idx) {
        if (gQmiTargets[idx].service == service && !gQmiTargets[idx].found) {
            if (service == FM_QMI_IPA && (instance >> 8) != 2u) continue;
            gQmiTargets[idx].node = node;
            gQmiTargets[idx].port = port;
            gQmiTargets[idx].found = 1;
        }
    }
    gServers++;
    if (gServerTextAt + 30 >= sizeof(gServerText)) flushServers();
    if (gServerTextAt) gServerText[gServerTextAt++] = ' ';
    hexAppend(service);
    gServerText[gServerTextAt++] = ':';
    hexAppend(instance);
    gServerText[gServerTextAt++] = '@';
    hexAppend(port);
}
static const char *dmsModeName(uint8_t mode)
{
    switch (mode) {
    case 0: return "online";
    case 1: return "low-power";
    case 2: return "factory-test";
    case 3: return "offline";
    case 4: return "resetting";
    case 5: return "shutting-down";
    case 6: return "persistent-low-power";
    case 7: return "mode-only-low-power";
    default: return "other";
    }
}
static const char *qmiServiceName(uint32_t service)
{
    switch (service) {
    case FM_QMI_WDS: return "wds";
    case FM_QMI_DMS: return "dms";
    case FM_QMI_NAS: return "nas";
    case FM_QMI_WMS: return "wms";
    case FM_QMI_UIM: return "uim";
    case FM_QMI_WDA: return "wda";
    case FM_QMI_DPM: return "dpm";
    case FM_QMI_IPA: return "ipa";
    default: return "?";
    }
}
static const struct fm_qmi_target *qmiFindTarget(uint32_t service)
{
    for (unsigned idx = 0; idx < sizeof(gQmiTargets) / sizeof(gQmiTargets[0]); ++idx) {
        if (gQmiTargets[idx].service == service && gQmiTargets[idx].found) return &gQmiTargets[idx];
    }
    return NULL;
}
static uint16_t qmiNextTxn(void)
{
    if (!++gQmiTxn) gQmiTxn = 1;
    return gQmiTxn;
}
static int qmiSendRaw(uint32_t service, const uint8_t *req, uint32_t req_len)
{
    const struct fm_qmi_target *target = qmiFindTarget(service);
    if (!target || !req || !req_len) return -1;
    if (qcom_qrtr_send_data(&gQrtr, FM_QMI_PORT, target->node, target->port, req, req_len)) return -1;
    gQmiTx++;
    return 0;
}
static int qmiSend(uint32_t service, uint16_t message, uint8_t tlv_type, uint8_t value)
{
    uint8_t req[16];
    const uint32_t req_len = fm_qmi_request(req, sizeof req, qmiNextTxn(), message, tlv_type, value);
    return qmiSendRaw(service, req, req_len);
}
static void serviceDataSetup(void)
{
    if (gQmiMode < 3u || !gQrtr.hello_received || !gQmiProbes) return;
    const uint64_t now = mach_absolute_time();
    if (!gDataSetupBegan) gDataSetupBegan = now;
    const uint32_t elapsed_ms = msSince(gDataSetupBegan);
    if ((gIpaMode & 0x400u) && !gIpaIndSent && elapsed_ms < 20000u) return;
    if (!gDpmSent && qmiFindTarget(FM_QMI_DPM)) {
        uint8_t req[32];
        const uint32_t req_len = fm_qmi_dpm_open_port(req, sizeof req, qmiNextTxn(), FM_DATA_EP_EMBEDDED, FM_DATA_EP_IFACE,
            FM_IPA5_EP_TO_MODEM, FM_IPA5_EP_FROM_MODEM);
        if (qmiSendRaw(FM_QMI_DPM, req, req_len) == 0) {
            gDpmSent = 1;
            printf("FUXI MODEM: qmi dpm open port ep=%u iface=%u to=%u from=%u\n",
                FM_DATA_EP_EMBEDDED, FM_DATA_EP_IFACE, FM_IPA5_EP_TO_MODEM, FM_IPA5_EP_FROM_MODEM);
        }
    }
    if (!gWdaSent && qmiFindTarget(FM_QMI_WDA) && (gDpmDone || !qmiFindTarget(FM_QMI_DPM) || elapsed_ms >= 10000u)) {
        uint8_t req[64];
        const uint32_t dl_max_size = (gIpaMode & 0x800u) ? FM_WDA_DL_MAX_SIZE_M7 : FM_WDA_DL_MAX_SIZE;
        const uint32_t req_len = fm_qmi_wda_set_data_format(req, sizeof req, qmiNextTxn(), FM_DATA_EP_EMBEDDED, FM_DATA_EP_IFACE,
            FM_WDA_DL_MAX_DGRAMS, dl_max_size);
        if (qmiSendRaw(FM_QMI_WDA, req, req_len) == 0) {
            gWdaSent = 1;
            printf("FUXI MODEM: qmi wda set data format rawIP QMAP ep=%u iface=%u dl_max=%u\n",
                FM_DATA_EP_EMBEDDED, FM_DATA_EP_IFACE, dl_max_size);
        }
    }
    const int ready_for_bind = (gDpmDone || !qmiFindTarget(FM_QMI_DPM)) && (gWdaDone || !qmiFindTarget(FM_QMI_WDA));
    if (!gWdsBindSent && qmiFindTarget(FM_QMI_WDS) && (ready_for_bind || elapsed_ms >= 15000u)) {
        uint8_t req[32];
        const uint32_t req_len = fm_qmi_wds_bind_mux(req, sizeof req, qmiNextTxn(), FM_DATA_EP_EMBEDDED, FM_DATA_EP_IFACE,
            FM_DATA_MUX_ID);
        if (qmiSendRaw(FM_QMI_WDS, req, req_len) == 0) {
            gWdsBindSent = 1;
            printf("FUXI MODEM: qmi wds bind mux ep=%u iface=%u mux=%u\n",
                FM_DATA_EP_EMBEDDED, FM_DATA_EP_IFACE, FM_DATA_MUX_ID);
        }
    }
    if (gWdsBindDone && gWdsIpFamDone && gNasRegistered && !gWdsUp && !gWdsStartPending && gWdsStartAttempts < 4u &&
        now >= gWdsNextTry && qmiFindTarget(FM_QMI_WDS)) {
        uint8_t req[16];
        const uint32_t req_len = fm_qmi_wds_start_network(req, sizeof req, qmiNextTxn(), 4u);
        if (qmiSendRaw(FM_QMI_WDS, req, req_len) == 0) {
            gWdsStartPending = 1;
            gWdsStartAttempts++;
            clock_interval_to_deadline(6000u, NSEC_PER_MSEC, &gWdsNextTry);
            printf("FUXI MODEM: qmi wds start network ipv4 attempt=%u (ps=%u)\n", gWdsStartAttempts, gNasPsAttached);
        }
    }
}
static void formatHex(char *out, size_t capacity, const uint8_t *bytes, uint32_t count)
{
    static const char kHex[] = "0123456789abcdef";
    size_t pos = 0;
    for (uint32_t idx = 0; idx < count && pos + 3u < capacity; ++idx) {
        if (idx) out[pos++] = ' ';
        out[pos++] = kHex[bytes[idx] >> 4];
        out[pos++] = kHex[bytes[idx] & 0xfu];
    }
    out[pos] = 0;
}
static void qmiReceive(uint32_t src_node, uint32_t src_port, const uint8_t *payload, uint32_t size)
{
    if (!gQmiMode) return;
    uint32_t service = 0;
    for (unsigned idx = 0; idx < sizeof(gQmiTargets) / sizeof(gQmiTargets[0]); ++idx) {
        if (gQmiTargets[idx].found && gQmiTargets[idx].node == src_node && gQmiTargets[idx].port == src_port) {
            service = gQmiTargets[idx].service;
            break;
        }
    }
    if (!service) {
        gQmiBad++;
        if (gQmiBadLogs < FM_QMI_BAD_LOG_LIMIT) {
            gQmiBadLogs++;
            printf("FUXI MODEM: qmi unknown src=%u:%u len=%u type=%u msg=%#x\n", src_node, src_port, size,
                size ? payload[0] : 0u, size >= 5u ? fm_le16(payload + 3) : 0u);
        }
        return;
    }
    FmQmiAnswer ans;
    if (fm_qmi_decode(service, payload, size, &ans)) {
        gQmiBad++;
        if (gQmiBadLogs < FM_QMI_BAD_LOG_LIMIT) {
            gQmiBadLogs++;
            printf("FUXI MODEM: qmi decode bad %s len=%u type=%u msg=%#x\n", qmiServiceName(service), size,
                size ? payload[0] : 0u, size >= 5u ? fm_le16(payload + 3) : 0u);
        }
        return;
    }
    if (ans.is_indication) {
        gQmiInd++;
        if (service == FM_QMI_WMS && ans.message == FM_WMS_EVENT_REPORT_IND) {
            if (ans.wms_ind_has_mt) {
                gWmsIndMt++;
                printf("FUXI MODEM: qmi wms ind mt store=%u idx=%u\n", ans.wms_ind_store, ans.wms_ind_index);
            }
            if (ans.wms_ind_has_route) {
                gWmsIndRoute++;
                printf("FUXI MODEM: qmi wms ind route ack=%u fmt=%u len=%u\n",
                    ans.wms_ind_ack, ans.wms_ind_format, ans.wms_ind_data_len);
            }
            return;
        }
        if (gQmiIndLogs < FM_QMI_IND_LOG_LIMIT) {
            gQmiIndLogs++;
            printf("FUXI MODEM: qmi ind %s msg=%#x txn=%u len=%u\n", qmiServiceName(service), ans.message, ans.transaction, size);
        }
        return;
    }
    gQmiRx++;
    if (service == FM_QMI_DMS && ans.message == FM_DMS_SET_OPERATING_MODE) {
        printf("FUXI MODEM: qmi dms set operating mode %u (%s) -> result=%u error=%#x\n", gDmsReqMode, dmsModeName(gDmsReqMode),
            ans.result, ans.error);
        if (gDmsState == 1) {
            if (ans.result == 0) {
                gDmsState = 5;
                clock_interval_to_deadline(1000u, NSEC_PER_MSEC, &gQmiNextProbe);
            } else if (ans.error == FM_QMI_ERR_OP_MODE_UNSUPPORTED) {
                gDmsState = 2;
                gDmsReqMode = FM_DMS_MODE_LOW_POWER;
                if (qmiSend(FM_QMI_DMS, FM_DMS_SET_OPERATING_MODE, 0x01, FM_DMS_MODE_LOW_POWER) == 0)
                    printf("FUXI MODEM: qmi dms offline->online refused (0x34); requesting low-power first\n");
                else
                    gDmsState = 0;
            } else {
                gDmsState = gDmsAttempts < 2 ? 0 : 5;
            }
        } else if (gDmsState == 2) {
            if (ans.result == 0) {
                gDmsState = 3;
                clock_interval_to_deadline(2000u, NSEC_PER_MSEC, &gDmsActionAt);
                printf("FUXI MODEM: qmi dms low-power accepted; waiting 2000 ms before online\n");
            } else {
                gDmsState = gDmsAttempts < 2 ? 0 : 5;
            }
        } else if (gDmsState == 4) {
            gDmsState = (ans.result == 0 || gDmsAttempts >= 2) ? 5 : 0;
            if (ans.result == 0) clock_interval_to_deadline(1000u, NSEC_PER_MSEC, &gQmiNextProbe);
        }
        return;
    }
    if (service == FM_QMI_UIM && (ans.message == FM_UIM_SWITCH_SLOT || ans.message == FM_UIM_CHANGE_PROVISIONING)) {
        printf("FUXI MODEM: qmi uim %s -> result=%u error=%#x\n",
            ans.message == FM_UIM_SWITCH_SLOT ? "switch-slot" : "change-provisioning", ans.result, ans.error);
        clock_interval_to_deadline(1500u, NSEC_PER_MSEC, &gQmiNextProbe);
        return;
    }
    if (service == FM_QMI_DPM && ans.message == FM_DPM_OPEN_PORT) {
        gDpmDone = 1;
        gDpmOk = ans.result == 0u;
        printf("FUXI MODEM: qmi dpm open port -> result=%u error=%#x\n", ans.result, ans.error);
        serviceDataSetup();
        return;
    }
    if (service == FM_QMI_WDA && ans.message == FM_WDA_SET_DATA_FORMAT) {
        gWdaDone = 1;
        gWdaOk = ans.result == 0u;
        printf("FUXI MODEM: qmi wda set data format -> result=%u error=%#x link=%u ul=%u dl=%u dgrams=%u size=%u\n",
            ans.result, ans.error, ans.wda_link_layer, ans.wda_ul_aggr, ans.wda_dl_aggr, ans.wda_dl_max_dgrams,
            ans.wda_dl_max_size);
        serviceDataSetup();
        return;
    }
    if (service == FM_QMI_WDS && ans.message == FM_WDS_BIND_MUX_DATA_PORT) {
        gWdsBindDone = 1;
        gWdsBindOk = ans.result == 0u;
        printf("FUXI MODEM: qmi wds bind mux -> result=%u error=%#x\n", ans.result, ans.error);
        if (!gWdsIpFamSent) {
            uint8_t req[16];
            const uint32_t req_len = fm_qmi_wds_set_ip_family(req, sizeof req, qmiNextTxn(), 4u);
            if (qmiSendRaw(FM_QMI_WDS, req, req_len) == 0) gWdsIpFamSent = 1;
        }
        return;
    }
    if (service == FM_QMI_WDS && ans.message == FM_WDS_SET_IP_FAMILY) {
        gWdsIpFamDone = 1;
        printf("FUXI MODEM: qmi wds set ip family ipv4 -> result=%u error=%#x\n", ans.result, ans.error);
        clock_interval_to_deadline(1000u, NSEC_PER_MSEC, &gWdsNextTry);
        serviceDataSetup();
        return;
    }
    if (service == FM_QMI_WDS && ans.message == FM_WDS_START_NETWORK) {
        printf("FUXI MODEM: qmi wds start network -> result=%u error=%#x cer=%d/%u vcer=%d/%u:%u handle=%d/%#x\n",
            ans.result, ans.error, ans.has_call_end_reason, ans.call_end_reason, ans.has_verbose_reason,
            ans.verbose_reason_type, ans.verbose_reason, ans.has_wds_handle, ans.wds_handle);
        if (ans.result == 0u && ans.has_wds_handle) {
            gWdsHandle = ans.wds_handle;
            gWdsUp = 1;
            uint8_t req[16];
            const uint32_t req_len = fm_qmi_wds_get_current_settings(req, sizeof req, qmiNextTxn(), FM_WDS_SETTINGS_MASK);
            (void)qmiSendRaw(FM_QMI_WDS, req, req_len);
        } else {
            gWdsStartPending = 0;
            clock_interval_to_deadline(6000u, NSEC_PER_MSEC, &gWdsNextTry);
        }
        return;
    }
    if (service == FM_QMI_WDS && ans.message == FM_WDS_GET_CURRENT_SETTINGS && ans.has_wds_settings) {
        printf("FUXI MODEM: qmi wds settings has_ip=%u has_gw=%u has_mask=%u has_dns1=%u has_dns2=%u mtu=%u ip_family=%u\n",
            ans.wds_has_ip, ans.wds_has_gw, ans.wds_has_mask, ans.wds_has_dns1, ans.wds_has_dns2, ans.wds_mtu,
            ans.wds_ip_family);
        if ((gIpaMode & 0x800u) && ans.wds_has_ip) {
            const uint32_t mtu = (ans.wds_mtu >= 576u && ans.wds_mtu <= ETHERMTU) ? ans.wds_mtu : 1400u;
            lck_mtx_lock(&gCellSessLock);
            fm_cell_session_config(&gCellSession, 1, ans.wds_ip, ans.wds_has_dns1 ? ans.wds_dns1 : 0u,
                ans.wds_has_dns2 ? ans.wds_dns2 : 0u, mtu);
            lck_mtx_unlock(&gCellSessLock);
            if (gCellIfp) {
                (void)ifnet_set_mtu(gCellIfp, mtu);
                cellSetLinkState(1);
            }
            printf("FUXI MODEM: en1 session configured up=1 mtu=%u dns=%u%u attached=%u\n",
                mtu, ans.wds_has_dns1, ans.wds_has_dns2, gCellAttached);
        }
        return;
    }
    if (service == FM_QMI_WMS) {
        if (ans.result) gWmsErrs++;
        if (ans.message == FM_WMS_SET_EVENT_REPORT) {
            gWmsEventDone = 1u;
            gWmsEventOk = (ans.result == 0u) ? 1u : 0u;
            printf("FUXI MODEM: qmi wms set_event_report result=%u err=%#x\n", ans.result, ans.error);
        } else if (ans.message == FM_WMS_GET_ROUTES) {
            gWmsRoutesDone = 1u;
            if (ans.result == 0u && ans.wms_has_routes) gWmsRouteCount = ans.wms_route_count;
            printf("FUXI MODEM: qmi wms get_routes result=%u err=%#x count=%u\n", ans.result, ans.error, ans.wms_route_count);
        } else if (ans.message == FM_WMS_GET_STORE_MAX_SIZE) {
            const unsigned slot = (ans.transaction == gWmsTxnNvStore) ? 1u : 0u;
            gWmsStoreDoneMask |= (uint8_t)(1u << slot);
            if (ans.result == 0u) {
                if (ans.wms_has_store_size) gWmsStoreMax[slot] = ans.wms_store_max;
                if (ans.wms_has_store_free) gWmsStoreFree[slot] = ans.wms_store_free;
            }
            printf("FUXI MODEM: qmi wms store_size(%s) result=%u err=%#x max=%u free=%u\n",
                slot ? "nv" : "uim", ans.result, ans.error, ans.wms_store_max, ans.wms_store_free);
        } else if (ans.message == FM_WMS_LIST_MESSAGES) {
            const unsigned slot = (ans.transaction == gWmsTxnNvList) ? 1u : 0u;
            gWmsListDoneMask |= (uint8_t)(1u << slot);
            if (ans.result == 0u && ans.wms_has_list) gWmsListCount[slot] = ans.wms_list_count;
            printf("FUXI MODEM: qmi wms list(%s) result=%u err=%#x count=%u\n",
                slot ? "nv" : "uim", ans.result, ans.error, ans.wms_list_count);
        } else if (ans.message == FM_WMS_RAW_SEND) {
            printf("FUXI MODEM: qmi wms raw_send result=%u err=%#x msg_id=%u cause=%u\n",
                ans.result, ans.error, ans.wms_msg_id, ans.wms_cause);
        } else if (ans.message == FM_WMS_RAW_READ) {
            printf("FUXI MODEM: qmi wms raw_read result=%u err=%#x tag=%u fmt=%u len=%u\n",
                ans.result, ans.error, ans.wms_read_tag, ans.wms_read_format, ans.wms_read_len);
        } else {
            printf("FUXI MODEM: qmi wms msg=%#x result=%u err=%#x\n", ans.message, ans.result, ans.error);
        }
        return;
    }
    if (ans.result) {
        printf("FUXI MODEM: qmi %s msg=%#x error %u\n", qmiServiceName(service), ans.message, ans.error);
        return;
    }
    if (service == FM_QMI_DMS && ans.message == FM_DMS_GET_OPERATING_MODE && ans.has_mode) {
        printf("FUXI MODEM: qmi dms operating mode %u (%s) offline_reason=%#x(%u) hw_restricted=%u(%u)\n", ans.mode,
            dmsModeName(ans.mode), ans.offline_reason, ans.has_offline_reason, ans.hw_restricted, ans.has_hw_restricted);
        if (ans.mode != FM_DMS_MODE_ONLINE && size > 7u) {
            char raw_hex[148];
            const uint32_t dump_len = (size - 7u) < 48u ? (size - 7u) : 48u;
            formatHex(raw_hex, sizeof raw_hex, payload + 7u, dump_len);
            printf("FUXI MODEM: qmi dms opmode tlvs (%u B): %s\n", size - 7u, raw_hex);
        }
        if (ans.mode == FM_DMS_MODE_ONLINE) {
            gDmsState = 5;
        } else if (gQmiMode >= 2 && gDmsState == 0 && gDmsAttempts < 2) {
            gDmsAttempts++;
            gDmsState = 1;
            gDmsReqMode = FM_DMS_MODE_ONLINE;
            if (qmiSend(FM_QMI_DMS, FM_DMS_SET_OPERATING_MODE, 0x01, FM_DMS_MODE_ONLINE) == 0)
                printf("FUXI MODEM: qmi dms requesting operating mode online\n");
            else
                gDmsState = 0;
        }
    } else if (service == FM_QMI_DMS && ans.message == FM_DMS_GET_REVISION) {
        printf("FUXI MODEM: qmi dms revision %s\n", ans.revision[0] ? ans.revision : "(empty)");
    } else if (service == FM_QMI_UIM && ans.message == FM_UIM_GET_CARD_STATUS && ans.has_card) {
        printf("FUXI MODEM: qmi uim card slots=%u primary_gw=%#x state=%u upin=%u err=%u apps=%u type=%u app_state=%u perso=%u pin1=%u retries=%u/%u\n",
            ans.slots, ans.primary_gw, ans.card_state, ans.upin_state, ans.card_error, ans.apps, ans.app_type, ans.app_state,
            ans.perso_state, ans.pin1_state, ans.pin1_retries, ans.puk1_retries);
        for (unsigned slot_idx = 0; slot_idx < ans.slot_count; ++slot_idx) {
            const FmUimSlot *slot = &ans.slot[slot_idx];
            printf("FUXI MODEM: qmi uim slot[%u] state=%u upin=%u err=%u apps=%u type=%u app_state=%u perso=%u pin1=%u retries=%u/%u gw_app=%u(type=%u state=%u aid_len=%u)\n",
                slot_idx, slot->card_state, slot->upin_state, slot->card_error, slot->apps, slot->app_type, slot->app_state,
                slot->perso_state, slot->pin1_state, slot->pin1_retries, slot->puk1_retries, slot->has_gw_app,
                slot->gw_app_type, slot->gw_app_state, slot->gw_aid_len);
        }
        if (gQmiMode >= 3u) {
            if (!gUimSwitched && ans.slot_count >= 2u && ans.slot[0].card_state != 1u) {
                unsigned target_phys = 0u;
                for (unsigned slot_idx = 1u; slot_idx < ans.slot_count; ++slot_idx) {
                    if (ans.slot[slot_idx].card_state == 1u) {
                        target_phys = slot_idx + 1u;
                        if (ans.slot[slot_idx].has_gw_app && ans.slot[slot_idx].gw_app_state == 7u) break;
                    }
                }
                if (target_phys) {
                    uint8_t req[24];
                    const uint32_t req_len = fm_qmi_uim_switch_slot(req, sizeof req, qmiNextTxn(), 1u, target_phys);
                    if (qmiSendRaw(FM_QMI_UIM, req, req_len) == 0) {
                        gUimSwitched = 1;
                        printf("FUXI MODEM: qmi uim switching logical slot 1 -> physical slot %u\n", target_phys);
                    }
                }
            } else if (!gUimProvisioned && ans.primary_gw == 0xffffu && ans.slot_count >= 1u &&
                       ans.slot[0].card_state == 1u && ans.slot[0].has_gw_app &&
                       (ans.slot[0].gw_app_state == 7u || ans.slot[0].gw_app_state == 1u)) {
                uint8_t req[64];
                const uint32_t req_len = fm_qmi_uim_change_provisioning(req, sizeof req, qmiNextTxn(), 0u, 1u, 1u,
                    ans.slot[0].gw_aid, ans.slot[0].gw_aid_len);
                if (qmiSendRaw(FM_QMI_UIM, req, req_len) == 0) {
                    gUimProvisioned = 1;
                    printf("FUXI MODEM: qmi uim activating primary GW on slot 1 (type=%u state=%u aid_len=%u)\n",
                        ans.slot[0].gw_app_type, ans.slot[0].gw_app_state, ans.slot[0].gw_aid_len);
                }
            }
        }
    } else if (service == FM_QMI_NAS && ans.message == FM_NAS_GET_SERVING_SYSTEM && ans.has_serving) {
        char radios[48];
        size_t pos = 0;
        radios[0] = 0;
        for (unsigned idx = 0; idx < ans.radio_count && pos + 5 < sizeof radios; ++idx)
            pos += (size_t)snprintf(radios + pos, sizeof radios - pos, idx ? " %u" : "%u", ans.radio[idx]);
        gNasRegistered = ans.reg_state == 1u;
        gNasPsAttached = ans.ps_attach == 1u;
        printf("FUXI MODEM: qmi nas serving reg=%u cs=%u ps=%u net=%u radio=[%s]\n",
            ans.reg_state, ans.cs_attach, ans.ps_attach, ans.network, radios);
        if (gNasRegistered) serviceDataSetup();
    } else if (service == FM_QMI_NAS && ans.message == FM_NAS_GET_SIGNAL_INFO) {
        char sig[128];
        size_t pos = 0;
        sig[0] = 0;
        if (ans.has_gsm && pos < sizeof sig)
            pos += (size_t)snprintf(sig + pos, sizeof sig - pos, "%sgsm(rssi=%d)", pos ? " " : "", (int)ans.gsm_rssi);
        if (ans.has_wcdma && pos < sizeof sig)
            pos += (size_t)snprintf(sig + pos, sizeof sig - pos, "%swcdma(rssi=%d ecio=%d)", pos ? " " : "", (int)ans.wcdma_rssi,
                (int)ans.wcdma_ecio);
        if (ans.has_lte && pos < sizeof sig)
            pos += (size_t)snprintf(sig + pos, sizeof sig - pos, "%slte(rssi=%d rsrq=%d rsrp=%d snr=%d)", pos ? " " : "",
                (int)ans.lte_rssi, (int)ans.lte_rsrq, (int)ans.lte_rsrp, (int)ans.lte_snr);
        if (ans.has_nr && pos < sizeof sig)
            pos += (size_t)snprintf(sig + pos, sizeof sig - pos, "%snr(rsrp=%d snr=%d)", pos ? " " : "", (int)ans.nr_rsrp,
                (int)ans.nr_snr);
        printf("FUXI MODEM: qmi nas siginfo %s\n", pos ? sig : "none");
    }
}
static void serviceWms(void)
{
    if (gQmiMode < 4u || !gQrtr.hello_received || !gQmiProbes || !qmiFindTarget(FM_QMI_WMS)) return;
    if (!gWmsEventSent) {
        uint8_t req[16];
        const uint32_t req_len = fm_qmi_wms_set_event_report(req, sizeof req, qmiNextTxn(), 1u);
        if (qmiSendRaw(FM_QMI_WMS, req, req_len) == 0) {
            gWmsEventSent = 1u;
            printf("FUXI MODEM: qmi wms probe set_event_report(1)\n");
        }
    }
    if (!gWmsRoutesSent) {
        uint8_t req[16];
        const uint32_t req_len = fm_qmi_wms_get_routes(req, sizeof req, qmiNextTxn());
        if (qmiSendRaw(FM_QMI_WMS, req, req_len) == 0) gWmsRoutesSent = 1u;
    }
    if (!(gWmsStoreSentMask & 1u)) {
        uint8_t req[24];
        const uint16_t txn = qmiNextTxn();
        const uint32_t req_len = fm_qmi_wms_get_store_max_size(req, sizeof req, txn, FM_WMS_STORAGE_UIM, FM_WMS_MODE_GW);
        if (qmiSendRaw(FM_QMI_WMS, req, req_len) == 0) {
            gWmsTxnUimStore = txn;
            gWmsStoreSentMask |= 1u;
        }
    }
    if (!(gWmsStoreSentMask & 2u)) {
        uint8_t req[24];
        const uint16_t txn = qmiNextTxn();
        const uint32_t req_len = fm_qmi_wms_get_store_max_size(req, sizeof req, txn, FM_WMS_STORAGE_NV, FM_WMS_MODE_GW);
        if (qmiSendRaw(FM_QMI_WMS, req, req_len) == 0) {
            gWmsTxnNvStore = txn;
            gWmsStoreSentMask |= 2u;
        }
    }
    if (!(gWmsListSentMask & 1u)) {
        uint8_t req[24];
        const uint16_t txn = qmiNextTxn();
        const uint32_t req_len = fm_qmi_wms_list_messages(req, sizeof req, txn, FM_WMS_STORAGE_UIM, FM_WMS_MODE_GW);
        if (qmiSendRaw(FM_QMI_WMS, req, req_len) == 0) {
            gWmsTxnUimList = txn;
            gWmsListSentMask |= 1u;
        }
    }
    if (!(gWmsListSentMask & 2u)) {
        uint8_t req[24];
        const uint16_t txn = qmiNextTxn();
        const uint32_t req_len = fm_qmi_wms_list_messages(req, sizeof req, txn, FM_WMS_STORAGE_NV, FM_WMS_MODE_GW);
        if (qmiSendRaw(FM_QMI_WMS, req, req_len) == 0) {
            gWmsTxnNvList = txn;
            gWmsListSentMask |= 2u;
        }
    }
}
static void serviceQmi(void)
{
    if (!gQmiMode || gBbMode || !gQrtr.hello_received) return;
    const uint64_t now = mach_absolute_time();
    if (gDmsState == 3 && now >= gDmsActionAt) {
        gDmsState = 4;
        gDmsReqMode = FM_DMS_MODE_ONLINE;
        if (qmiSend(FM_QMI_DMS, FM_DMS_SET_OPERATING_MODE, 0x01, FM_DMS_MODE_ONLINE) == 0)
            printf("FUXI MODEM: qmi dms requesting operating mode online (after low-power)\n");
        else
            gDmsState = 0;
        pumpSends();
    }
    if (!gQmiNextProbe) {
        clock_interval_to_deadline(5000u, NSEC_PER_MSEC, &gQmiNextProbe);
        return;
    }
    serviceDataSetup();
    serviceWms();
    if (now < gQmiNextProbe) return;
    if (!gQmiRevisionSent && qmiSend(FM_QMI_DMS, FM_DMS_GET_REVISION, 0, 0) == 0) gQmiRevisionSent = 1;
    (void)qmiSend(FM_QMI_DMS, FM_DMS_GET_OPERATING_MODE, 0, 0);
    (void)qmiSend(FM_QMI_UIM, FM_UIM_GET_CARD_STATUS, 0, 0);
    (void)qmiSend(FM_QMI_NAS, FM_NAS_GET_SERVING_SYSTEM, 0, 0);
    (void)qmiSend(FM_QMI_NAS, FM_NAS_GET_SIGNAL_INFO, 0, 0);
    gQmiProbes++;
    serviceDataSetup();
    serviceWms();
    clock_interval_to_deadline(gQmiProbes < 8 ? 15000u : 600000u, NSEC_PER_MSEC, &gQmiNextProbe);
    pumpSends();
}

/* ---- QMI IPA 0x31 雙向交握（AP 服務 0x101@4、用戶端 0x4002 <-> modem 0x201） ---- */
static void ipaQmiMaybeComplete(void)
{
    if (!(gIpaMode & 0x400u) || gIpaIndSent || !gIpaInitOk || !gIpaUcReady || !gIpaIndReg) return;
    uint8_t ind[16];
    const uint32_t ind_len = fm_ipa_qmi_result_msg(ind, sizeof ind, FM_QMI_TYPE_INDICATION, qmiNextTxn(),
        FM_IPA_QMI_INIT_COMPLETE, 0u, 0u);
    if (ind_len && qcom_qrtr_send_data(&gQrtr, FM_IPA_HOST_PORT, gIpaIndNode, gIpaIndPort, ind, ind_len) == 0) {
        gIpaIndSent = 1u;
        printf("FUXI MODEM: ipa qmi 0x22 init_complete ind sent to %u:%#x at %u ms\n",
            gIpaIndNode, gIpaIndPort, gBegan ? msSince(gBegan) : 0u);
        if (!gBbMode) {
            cellAttachIfnet();
            serviceDataSetup();
        }
        pumpSends();
    }
}

static void ipaHostReceive(void *context, QcomQrtr *q, uint32_t port, uint32_t node, uint32_t from,
    const uint8_t *data, uint32_t length)
{
    (void)context; (void)port;
    if (length < 7u || data[0] != FM_QMI_TYPE_REQUEST || fm_le16(data + 5) > length - 7u) {
        gIpaQmiBad++;
        return;
    }
    const uint16_t txn = fm_le16(data + 1);
    const uint16_t msg_id = fm_le16(data + 3);
    if (msg_id == FM_IPA_QMI_IND_REGISTER) {
        gIpaIndNode = node;
        gIpaIndPort = from;
        gIpaIndReg = 1u;
        uint8_t rsp[16];
        const uint32_t rsp_len = fm_ipa_qmi_result_msg(rsp, sizeof rsp, FM_QMI_TYPE_RESPONSE, txn, msg_id, 0u, 0u);
        (void)qcom_qrtr_send_data(q, FM_IPA_HOST_PORT, node, from, rsp, rsp_len);
        printf("FUXI MODEM: ipa qmi 0x20 ind_register from %u:%#x -> ok at %u ms\n",
            node, from, gBegan ? msSince(gBegan) : 0u);
        ipaQmiMaybeComplete();
        pumpSends();
    } else if (msg_id == FM_IPA_QMI_DRIVER_INIT_COMPLETE) {
        gIpaUcReady = 1u;
        uint8_t rsp[16];
        const uint32_t rsp_len = fm_ipa_qmi_result_msg(rsp, sizeof rsp, FM_QMI_TYPE_RESPONSE, txn, msg_id, 0u, 0u);
        (void)qcom_qrtr_send_data(q, FM_IPA_HOST_PORT, node, from, rsp, rsp_len);
        printf("FUXI MODEM: ipa qmi 0x35 driver_init_complete from %u:%#x -> ok at %u ms\n",
            node, from, gBegan ? msSince(gBegan) : 0u);
        ipaQmiMaybeComplete();
        pumpSends();
    } else if (msg_id == FM_IPA_QMI_INSTALL_FILTER) {
        uint8_t rsp[320];
        uint32_t count = 0u;
        const uint32_t rsp_len = fm_ipa_qmi_filter_resp(rsp, sizeof rsp, txn, data, length, &count);
        if (rsp_len) (void)qcom_qrtr_send_data(q, FM_IPA_HOST_PORT, node, from, rsp, rsp_len);
        gIpaFilterReqs++;
        gIpaFilterRules += count;
        if (gIpaFilterReqs <= 16u) {
            printf("FUXI MODEM: ipa qmi 0x23 install_filter count=%u len=%u -> ok\n", count, length);
        }
        pumpSends();
    } else {
        uint8_t rsp[16];
        const uint32_t rsp_len = fm_ipa_qmi_result_msg(rsp, sizeof rsp, FM_QMI_TYPE_RESPONSE, txn, msg_id, 0u, 0u);
        (void)qcom_qrtr_send_data(q, FM_IPA_HOST_PORT, node, from, rsp, rsp_len);
        gIpaOtherReqs++;
        if (gIpaOtherReqs <= 16u) {
            printf("FUXI MODEM: ipa qmi %#x req len=%u -> ok\n", msg_id, length);
        }
        pumpSends();
    }
}

static void ipaClientReceive(uint32_t src_node, uint32_t src_port, const uint8_t *payload, uint32_t size)
{
    if (size < 7u || fm_le16(payload + 5) > size - 7u) {
        gIpaQmiBad++;
        return;
    }
    const uint8_t msg_type = payload[0];
    const uint16_t msg_id = fm_le16(payload + 3);
    if (msg_type == FM_QMI_TYPE_RESPONSE && msg_id == FM_IPA_QMI_INIT_DRIVER) {
        uint32_t rlen = 0u;
        const uint8_t *rval = fm_qmi_tlv(payload, size, 0x02u, &rlen);
        const uint16_t result = (rval && rlen >= 4u) ? fm_le16(rval) : 1u;
        const uint16_t error = (rval && rlen >= 4u) ? fm_le16(rval + 2u) : 0xffffu;
        gIpaInitRsp = 1u;
        gIpaInitOk = (result == 0u && error == 0u) ? 1u : 0u;
        printf("FUXI MODEM: ipa qmi 0x21 init_driver rsp from %u:%#x result=%u error=%u at %u ms\n",
            src_node, src_port, result, error, gBegan ? msSince(gBegan) : 0u);
        ipaQmiMaybeComplete();
    } else if (gIpaQmiBad++ < 8u) {
        printf("FUXI MODEM: ipa qmi client unexpected type=%u msg=%#x len=%u from %u:%#x\n",
            msg_type, msg_id, size, src_node, src_port);
    }
}

static void serviceIpa(void)
{
    if ((gIpaMode & 0x400u) && gQrtr.hello_received && !gIpaInitSent) {
        if (!(gIpaMode & 0x200u) || gIpaGsiReady) {
            const struct fm_qmi_target *ipa_srv = qmiFindTarget(FM_QMI_IPA);
            if (ipa_srv) {
                uint8_t req[224];
                const uint32_t req_len = fm_ipa_qmi_init_driver_req(req, sizeof req, qmiNextTxn(),
                    gIpaMemOffset, FM_IPA5_EP_FROM_MODEM, gIpaSmemSize, gIpaSmemPhys);
                if (req_len && qcom_qrtr_send_data(&gQrtr, FM_IPA_CLIENT_PORT, ipa_srv->node, ipa_srv->port,
                        req, req_len) == 0) {
                    gIpaInitSent = 1u;
                    printf("FUXI MODEM: ipa qmi 0x21 init_driver req sent to %u:%#x mem_off=%#x rx_ep=%u smem=%#x@%#x len=%u at %u ms\n",
                        ipa_srv->node, ipa_srv->port, gIpaMemOffset, FM_IPA5_EP_FROM_MODEM, gIpaSmemSize, gIpaSmemPhys,
                        req_len, gBegan ? msSince(gBegan) : 0u);
                    pumpSends();
                }
            }
        }
    }
    if (gIpaGsiReady) {
        ipaPollRxChan(&gIpaChans[1]);
        ipaPollRxChan(&gIpaChans[3]);
        if (((gIpaMode & 0x800u) || gBbMode) && gIpaModemTxVa) {
            lck_mtx_lock(&gIpaTxLock);
            ipaPollTxChanLocked();
            lck_mtx_unlock(&gIpaTxLock);
        }
        if ((gIpaMode & 0x800u) && !gBbMode && gIpaIndSent && !gCellAttached && !gCellAttachFailed) cellAttachIfnet();
    }
}

/* ---- TFTP over QRTR（tqftpserv，對照 hakios.mpss.c） ---- */
static int tftpSend(uint32_t from_port, uint32_t dst_node, uint32_t dst_port, const uint8_t *payload, uint32_t length)
{
    const uint32_t pkt_len = fm_qrtr_data(gTftpPktBuf, sizeof gTftpPktBuf, gQrtr.local_node, from_port, dst_node, dst_port, payload,
        length);
    if (!pkt_len || queueSend(NULL, gTftpPktBuf, pkt_len)) { gQrtr.tx_refused++; return -1; }
    gQrtr.tx_packets++;
    return 0;
}
static void tftpSendError(uint32_t from_port, uint32_t node, uint32_t port, uint16_t code, const char *text)
{
    uint8_t msg[64];
    const uint32_t n = fm_tftp_build_error(msg, sizeof msg, code, text);
    if (n) (void)tftpSend(from_port, node, port, msg, n);
}
static void tftpEnd(struct fm_tftp_xfer *xfer)
{
    if (!xfer->active) return;
    if (xfer->writing) {
        if (xfer->data) kfree_data(xfer->data, FM_TFTP_WRITE_MAX);
    } else if (xfer->data && !xfer->borrowed) {
        fuxi_modem_fw_free_file(xfer->data, xfer->size);
    }
    bzero(xfer, sizeof(*xfer));
}
static void tftpExpireIdle(void)
{
    if (!gTftpMode) return;
    const uint64_t now = mach_absolute_time();
    for (unsigned idx = 0; idx < FM_TFTP_SLOTS; ++idx) {
        if (gTftpXfers[idx].active && now >= gTftpXfers[idx].last_active) {
            if (gTftpDiagLogs < 24u) {
                gTftpDiagLogs++;
                printf("FUXI MODEM: tftp idle expire \"%s\" wr=%u sent=%u eof=%u rsize=%u size=%llu\n",
                    gTftpXfers[idx].name, gTftpXfers[idx].writing, gTftpXfers[idx].sent, gTftpXfers[idx].eof_sent,
                    gTftpXfers[idx].rsize, (unsigned long long)gTftpXfers[idx].size);
            }
            tftpEnd(&gTftpXfers[idx]);
        }
    }
}
static void tftpTouch(struct fm_tftp_xfer *xfer)
{
    clock_interval_to_deadline(FM_TFTP_IDLE_MS, NSEC_PER_MSEC, &xfer->last_active);
}
static void tftpSendWindow(struct fm_tftp_xfer *xfer, uint32_t acked_block)
{
    xfer->eof_sent = 0u;
    for (uint32_t step = 0; step < xfer->wsize; ++step) {
        const uint32_t block = acked_block + 1u + step;
        uint64_t offset = 0u;
        uint32_t take = 0u;
        int is_eof = 0;
        if (!fm_tftp_rrq_block(xfer->size, xfer->seek, xfer->rsize, xfer->blksize, block, &offset, &take, &is_eof)) {
            xfer->eof_sent = 1u;
            break;
        }
        gTftpDataBuf[0] = 0u;
        gTftpDataBuf[1] = FM_TFTP_DATA;
        gTftpDataBuf[2] = (uint8_t)(block >> 8);
        gTftpDataBuf[3] = (uint8_t)block;
        if (take) memcpy(gTftpDataBuf + 4, xfer->data + offset, take);
        if (tftpSend(xfer->local_port, xfer->node, xfer->peer, gTftpDataBuf, 4u + take)) break;
        xfer->sent = block;
        if (is_eof) {
            xfer->eof_sent = 1u;
            break;
        }
    }
}
static int tftpFindMem(const char *name)
{
    for (unsigned idx = 0; idx < FM_TFTP_MEM_SLOTS; ++idx) {
        if (gTftpMem[idx].data && strcmp(gTftpMem[idx].name, name) == 0) return (int)idx;
    }
    return -1;
}
static int tftpDeleteMem(const char *name)
{
    const int idx = tftpFindMem(name);
    if (idx < 0) return -1;
    kfree_data(gTftpMem[idx].data, gTftpMem[idx].size ? gTftpMem[idx].size : 1u);
    bzero(&gTftpMem[idx], sizeof(gTftpMem[idx]));
    return idx;
}
static void tftpStoreMem(const char *name, const uint8_t *data, uint32_t size)
{
    unsigned slot = FM_TFTP_MEM_SLOTS, free_slot = FM_TFTP_MEM_SLOTS;
    for (unsigned idx = 0; idx < FM_TFTP_MEM_SLOTS; ++idx) {
        if (gTftpMem[idx].data && strcmp(gTftpMem[idx].name, name) == 0) { slot = idx; break; }
        if (!gTftpMem[idx].data && free_slot == FM_TFTP_MEM_SLOTS) free_slot = idx;
    }
    if (slot == FM_TFTP_MEM_SLOTS) slot = free_slot;
    if (slot >= FM_TFTP_MEM_SLOTS) {
        printf("FUXI MODEM: tftp write \"%s\" (%u B) dropped (RAM cache full)\n", name, size);
        return;
    }
    if (gTftpMem[slot].data) {
        kfree_data(gTftpMem[slot].data, gTftpMem[slot].size ? gTftpMem[slot].size : 1u);
        gTftpMem[slot].data = NULL;
        gTftpMem[slot].size = 0;
    }
    uint8_t *copy = kalloc_data(size ? size : 1u, Z_WAITOK);
    if (!copy) return;
    if (size) memcpy(copy, data, size);
    gTftpMem[slot].data = copy;
    gTftpMem[slot].size = size;
    strlcpy(gTftpMem[slot].name, name, sizeof gTftpMem[slot].name);
    gTftpWrites++;
    if (gTftpWrites <= 40) printf("FUXI MODEM: tftp write \"%s\" (%u B) kept in RAM slot %u\n", name, size, slot);
}
static void tftpRequest(void *context, QcomQrtr *q, uint32_t port, uint32_t node, uint32_t from, const uint8_t *data, uint32_t length)
{
    (void)context; (void)q; (void)port;
    if (!gTftpMode) return;
    gTftpRequests++;
    FmTftpRequest req;
    if (fm_tftp_parse_request(data, length, &req)) {
        if (gTftpRequests <= FM_TFTP_LOG_LIMIT) {
            char raw_hex[80];
            formatHex(raw_hex, sizeof raw_hex, data, length < 24u ? length : 24u);
            printf("FUXI MODEM: tftp bad request (%u B): %s\n", length, raw_hex);
        }
        tftpSendError(FM_TFTP_PORT, node, from, FM_TFTP_EBADOP, "bad request");
        gTftpRefused++;
        return;
    }
    if (req.unknown_opts && gTftpRequests <= FM_TFTP_LOG_LIMIT)
        printf("FUXI MODEM: tftp \"%s\" unknown opts=%u (%s)\n", req.path, req.unknown_opts, req.unknown_name);
    tftpExpireIdle();
    unsigned slot = FM_TFTP_SLOTS;
    for (unsigned idx = 0; idx < FM_TFTP_SLOTS; ++idx) {
        if (!gTftpXfers[idx].active) { slot = idx; break; }
    }
    if (slot >= FM_TFTP_SLOTS) {
        if (gTftpRequests <= FM_TFTP_LOG_LIMIT)
            printf("FUXI MODEM: tftp %s \"%s\" busy\n", req.opcode == FM_TFTP_WRQ ? "wrq" : "rrq", req.path);
        tftpSendError(FM_TFTP_PORT, node, from, FM_TFTP_ENOENT, "busy");
        gTftpRefused++;
        return;
    }
    struct fm_tftp_xfer *xfer = &gTftpXfers[slot];
    bzero(xfer, sizeof(*xfer));
    xfer->local_port = FM_TFTP_XFER_BASE + slot;
    xfer->node = node;
    xfer->peer = from;
    xfer->blksize = req.blksize;
    xfer->wsize = req.wsize;
    xfer->rsize = req.rsize;
    xfer->seek = req.seek;
    strlcpy(xfer->name, req.path, sizeof xfer->name);
    if (req.opcode == FM_TFTP_WRQ) {
        if (req.has_unlink) {
            const int removed = tftpDeleteMem(req.path);
            if (removed < 0) {
                if (gTftpRequests <= FM_TFTP_LOG_LIMIT) printf("FUXI MODEM: tftp unlink \"%s\" not found\n", req.path);
                tftpSendError(FM_TFTP_PORT, node, from, FM_TFTP_ENOENT, "file not found");
                gTftpRefused++;
                return;
            }
            /* 原廠 vendor-fuxi tftp_server 0x17664→0xe1c0：unlink 回送 OACK 後即結束，不等 ACK(0) 也不佔用傳輸槽。 */
            if (gTftpRequests <= FM_TFTP_LOG_LIMIT)
                printf("FUXI MODEM: tftp unlink \"%s\" ok (slot %d)\n", req.path, removed);
            uint8_t oack[160];
            const uint32_t oack_len = fm_tftp_build_oack(oack, sizeof oack, &req, 0u);
            if (oack_len) {
                (void)tftpSend(xfer->local_port, node, from, oack, oack_len);
            } else {
                uint8_t ack[4];
                fm_tftp_build_ack(ack, 0u);
                (void)tftpSend(xfer->local_port, node, from, ack, sizeof ack);
            }
            gTftpServed++;
            bzero(xfer, sizeof(*xfer));
            return;
        }
        if (req.has_seek && req.seek > FM_TFTP_WRITE_MAX) {
            tftpSendError(FM_TFTP_PORT, node, from, FM_TFTP_ENOSPC, "seek too large");
            gTftpRefused++;
            return;
        }
        xfer->data = kalloc_data(FM_TFTP_WRITE_MAX, Z_WAITOK | Z_ZERO);
        if (!xfer->data) {
            tftpSendError(FM_TFTP_PORT, node, from, FM_TFTP_ENOSPC, "no memory");
            gTftpRefused++;
            return;
        }
        if (req.has_seek || req.has_append) {
            const int existing = tftpFindMem(req.path);
            if (existing >= 0) {
                const uint32_t copy_len = gTftpMem[existing].size <= FM_TFTP_WRITE_MAX ? gTftpMem[existing].size : FM_TFTP_WRITE_MAX;
                if (copy_len) memcpy(xfer->data, gTftpMem[existing].data, copy_len);
                xfer->written_size = copy_len;
            }
            xfer->write_pos = req.has_append ? xfer->written_size : req.seek;
        }
        xfer->active = 1u;
        xfer->writing = 1u;
        xfer->expected = 1u;
        tftpTouch(xfer);
        if (gTftpRequests <= FM_TFTP_LOG_LIMIT)
            printf("FUXI MODEM: tftp wrq \"%s\" blk=%u win=%u seek=%u(%u) app=%u (RAM only)\n", xfer->name, xfer->blksize,
                xfer->wsize, req.seek, req.has_seek, req.has_append);
        uint8_t oack[160];
        const uint32_t oack_len = req.has_options ? fm_tftp_build_oack(oack, sizeof oack, &req, req.tsize) : 0u;
        if (oack_len) {
            (void)tftpSend(xfer->local_port, node, from, oack, oack_len);
            return;
        }
        uint8_t ack[4];
        fm_tftp_build_ack(ack, 0u);
        (void)tftpSend(xfer->local_port, node, from, ack, sizeof ack);
        return;
    }
    if (req.has_unlink) {
        if (gTftpRequests <= FM_TFTP_LOG_LIMIT) printf("FUXI MODEM: tftp rrq \"%s\" refused (unlink in RRQ)\n", req.path);
        tftpSendError(FM_TFTP_PORT, node, from, FM_TFTP_EBADOP, "unlink in rrq");
        gTftpRefused++;
        return;
    }
    /* RRQ：先找本次開機 RAM 快取，再找 modem_a /image */
    const int mem_idx = tftpFindMem(req.path);
    if (mem_idx >= 0) {
        xfer->data = gTftpMem[mem_idx].data;
        xfer->size = gTftpMem[mem_idx].size;
        xfer->borrowed = 1u;
    }
    if (!xfer->borrowed) {
        char relpath[160];
        if (fm_tftp_fw_relpath(req.path, relpath, sizeof relpath)) {
            if (gTftpRequests <= FM_TFTP_LOG_LIMIT) printf("FUXI MODEM: tftp rrq \"%s\" refused (outside readonly)\n", req.path);
            tftpSendError(FM_TFTP_PORT, node, from, FM_TFTP_ENOENT, "file not found");
            gTftpRefused++;
            return;
        }
        const int rc = fuxi_modem_fw_read_file(relpath, &xfer->data, &xfer->size, FM_TFTP_READ_MAX);
        if (rc) {
            if (gTftpRequests <= FM_TFTP_LOG_LIMIT) printf("FUXI MODEM: tftp rrq \"%s\" (%s) not found rc=%d\n", req.path, relpath, rc);
            tftpSendError(FM_TFTP_PORT, node, from, FM_TFTP_ENOENT, "file not found");
            gTftpRefused++;
            return;
        }
    }
    xfer->active = 1u;
    tftpTouch(xfer);
    if (gTftpRequests <= FM_TFTP_LOG_LIMIT)
        printf("FUXI MODEM: tftp rrq \"%s\" size=%llu blk=%u win=%u rsize=%u(%u) seek=%u ts=%u%s\n", xfer->name,
            (unsigned long long)xfer->size, xfer->blksize, xfer->wsize, xfer->rsize, req.has_rsize, xfer->seek, req.has_tsize,
            xfer->borrowed ? " (ram)" : "");
    uint8_t oack[160];
    const uint32_t oack_len = req.has_options ? fm_tftp_build_oack(oack, sizeof oack, &req, xfer->size) : 0u;
    if (!oack_len) {
        tftpSendWindow(xfer, 0u);
        return;
    }
    (void)tftpSend(xfer->local_port, node, from, oack, oack_len);
}
static void tftpTransferReceive(uint32_t dst_port, uint32_t src_node, uint32_t src_port, const uint8_t *msg, uint32_t size)
{
    if (!gTftpMode || size < 4u) return;
    struct fm_tftp_xfer *xfer = &gTftpXfers[dst_port - FM_TFTP_XFER_BASE];
    if (!xfer->active || xfer->node != src_node || xfer->peer != src_port) return;
    tftpTouch(xfer);
    const uint16_t opcode = (uint16_t)((uint16_t)msg[0] << 8 | msg[1]);
    const uint16_t block = (uint16_t)((uint16_t)msg[2] << 8 | msg[3]);
    if (xfer->writing && opcode == FM_TFTP_DATA) {
        const uint32_t n = size - 4u;
        if (block != (uint16_t)xfer->expected) {
            tftpSendError(xfer->local_port, src_node, src_port, FM_TFTP_EBADOP, "block out of sequence");
            tftpEnd(xfer);
            return;
        }
        if (xfer->write_pos > FM_TFTP_WRITE_MAX || n > FM_TFTP_WRITE_MAX - xfer->write_pos) {
            tftpSendError(xfer->local_port, src_node, src_port, FM_TFTP_ENOSPC, "too big");
            tftpEnd(xfer);
            return;
        }
        if (n) memcpy(xfer->data + xfer->write_pos, msg + 4, n);
        xfer->write_pos += n;
        if (xfer->write_pos > xfer->written_size) xfer->written_size = xfer->write_pos;
        xfer->expected++;
        if (block % xfer->wsize == 0 || n < xfer->blksize) {
            uint8_t ack[4];
            fm_tftp_build_ack(ack, block);
            (void)tftpSend(xfer->local_port, src_node, src_port, ack, sizeof ack);
        }
        if (n < xfer->blksize) {
            tftpStoreMem(xfer->name, xfer->data, xfer->written_size);
            gTftpServed++;
            tftpEnd(xfer);
        }
        return;
    }
    if (opcode == FM_TFTP_ERROR) {
        if (block != FM_TFTP_END_OF_TRANSFER && gTftpRequests <= FM_TFTP_LOG_LIMIT)
            printf("FUXI MODEM: tftp \"%s\" ended with error %u\n", xfer->name, block);
        else if (block == FM_TFTP_END_OF_TRANSFER)
            gTftpServed++;
        tftpEnd(xfer);
        return;
    }
    if (opcode != FM_TFTP_ACK) {
        if (gTftpDiagLogs < 24u) {
            gTftpDiagLogs++;
            printf("FUXI MODEM: tftp \"%s\" unexpected op=%u blk=%u wr=%u\n", xfer->name, opcode, block, xfer->writing);
        }
        tftpSendError(xfer->local_port, src_node, src_port, FM_TFTP_EBADOP, "expected ACK");
        tftpEnd(xfer);
        return;
    }
    if (block > 0 && xfer->eof_sent && block == (uint16_t)xfer->sent) {
        gTftpServed++;
        tftpEnd(xfer);
        return;
    }
    if (block == 0u && xfer->sent > 0u) {
        xfer->retries++;
        if (gTftpDiagLogs < 24u) {
            gTftpDiagLogs++;
            printf("FUXI MODEM: tftp \"%s\" dup ACK(0) #%u (sent=%u eof=%u rsize=%u)\n",
                xfer->name, xfer->retries, xfer->sent, xfer->eof_sent, xfer->rsize);
        }
    }
    tftpSendWindow(xfer, block);
}
/* 名稱服務的查詢：回覆符合的本地服務，再送全零 NEW_SERVER 結束。 */
static int answerLookup(const uint8_t *packet, uint32_t length)
{
    uint32_t node = 0, port = 0, service = 0, instance = 0;
    if (fm_qrtr_lookup(packet, length, &node, &port, &service, &instance)) return 0;
    uint8_t reply[FM_QRTR_CTRL_PACKET];
    gLookups++;
    for (uint32_t i = 0; i < gQrtr.local_count; ++i) {
        if (!fm_lookup_matches(service, instance, gQrtr.local[i].service, gQrtr.local[i].instance)) continue;
        const uint32_t words[4] = {gQrtr.local[i].service, gQrtr.local[i].instance, gQrtr.local_node, gQrtr.local[i].port};
        if (!queueSend(NULL, reply, fm_qrtr_ctrl(reply, FM_QRTR_NEW_SERVER, gQrtr.local_node, node, port, words))) gLookupAnswers++;
    }
    queueSend(NULL, reply, fm_qrtr_ctrl(reply, FM_QRTR_NEW_SERVER, gQrtr.local_node, node, port, NULL));
    if (gLookups <= 24) printf("FUXI MODEM: qrtr lookup %#x:%#x from %u:%#x\n", service, instance, node, port);
    return 1;
}
/* 與 ShengQrtr.c 的表相同（modem 問的是 msm/modem 的服務；表一致免得兩個定位服務答得不一樣） */
static const char *const kAudio[] = {"avs/audio", NULL};
static const char *const kGps[] = {"gps/gps_service", NULL};
static const QcomPdDomain kDomains[] = {
    {"msm/adsp/audio_pd", 74, kAudio}, {"msm/adsp/root_pd", 74, NULL}, {"msm/adsp/charger_pd", 74, NULL},
    {"msm/adsp/sensor_pd", 74, NULL}, {"msm/cdsp/root_pd", 76, NULL}, {"msm/modem/root_pd", 180, kGps},
};
static void locator(void *context, QcomQrtr *q, uint32_t port, uint32_t node, uint32_t from, const uint8_t *data, uint32_t length)
{
    (void)context;
    uint8_t answer[480];
    gServloc++;
    char svc_name[64];
    svc_name[0] = 0;
    uint32_t svc_len = 0;
    const uint8_t *tlv = fm_qmi_tlv(data, length, 0x01u, &svc_len);
    if (tlv) {
        while (svc_len && !tlv[svc_len - 1u]) --svc_len;
        uint32_t copy_idx = 0;
        for (; copy_idx < svc_len && copy_idx + 1u < sizeof svc_name; ++copy_idx)
            svc_name[copy_idx] = (tlv[copy_idx] >= 0x20u && tlv[copy_idx] < 0x7fu) ? (char)tlv[copy_idx] : '?';
        svc_name[copy_idx] = 0;
    }
    const uint32_t n = qcom_servloc_answer(kDomains, sizeof kDomains / sizeof kDomains[0], data, length, answer, sizeof answer);
    if (n) qcom_qrtr_send_data(q, port, node, from, answer, n);
    if (gServloc <= 16) printf("FUXI MODEM: servloc request %u \"%s\" answered %u bytes\n", gServloc, svc_name, n);
}
static void rmtfs(void *context, QcomQrtr *q, uint32_t port, uint32_t node, uint32_t from, const uint8_t *data, uint32_t length)
{
    (void)context;
    uint8_t answer[64];
    const uint32_t before = gRmtfs.requests;
    const uint64_t read_before = gRmtfs.sectors_read;
    const uint64_t write_before = gRmtfs.sectors_written;
    const uint32_t n = fm_rmtfs_answer(&gRmtfs, data, length, answer, sizeof answer);
    __asm__ volatile("dsb sy" ::: "memory"); /* 讀出的磁區先落到緩衝區，再回覆 */
    if (n) qcom_qrtr_send_data(q, port, node, from, answer, n);
    if (gRmtfs.requests != before && (gRmtfs.requests <= 24 || gRmtfs.last_error)) {
        char detail[64];
        detail[0] = 0;
        if (gRmtfs.last_message == FM_RMTFS_OPEN) {
            uint32_t plen = 0;
            const uint8_t *pval = fm_qmi_tlv(data, length, 0x01u, &plen);
            if (pval) {
                uint32_t copy_idx = 0;
                for (; copy_idx < plen && copy_idx + 1u < sizeof detail; ++copy_idx)
                    detail[copy_idx] = (pval[copy_idx] >= 0x20u && pval[copy_idx] < 0x7fu) ? (char)pval[copy_idx] : '?';
                detail[copy_idx] = 0;
            }
        } else if (gRmtfs.last_message == FM_RMTFS_RW_IOVEC) {
            const unsigned caller = fm_rmtfs_caller(data, length);
            const uint64_t d_read = gRmtfs.sectors_read - read_before;
            const uint64_t d_write = gRmtfs.sectors_written - write_before;
            (void)snprintf(detail, sizeof detail, "file=%u %s=%llu", caller, d_write ? "wr_sec" : "rd_sec",
                (unsigned long long)(d_write ? d_write : d_read));
        }
        printf("FUXI MODEM: rmtfs msg=%u %s -> %s (o=%u r=%u w=%u ref=%u)\n", gRmtfs.last_message, detail,
            n && answer[10] == 0 ? "ok" : "refused", gRmtfs.opens, gRmtfs.reads, gRmtfs.writes, gRmtfs.refused);
    }
    if (gRmtfs.reads && gStage < StRmtfs) stage(StRmtfs);
}
static void linkState(void *context, QcomGlinkChannel *channel, int up)
{
    (void)context; (void)channel;
    printf("FUXI MODEM: channel IPCRTR %s at %u ms\n", up ? "up" : "down", msSince(gBegan));
    if (!up) return;
    gLinkUps++;
    stage(StChannel);
    gQrtr.hello_sent = gQrtr.hello_received = 0;
    gQrtr.remote_count = 0;
    gQrtr.remote_node = QCOM_QRTR_NODE_BROADCAST;
    for (unsigned idx = 0; idx < sizeof(gQmiTargets) / sizeof(gQmiTargets[0]); ++idx) gQmiTargets[idx].found = 0;
    gQmiNextProbe = 0;
    gQmiProbes = 0;
    gQmiRevisionSent = 0;
    gDmsState = 0;
    gDmsReqMode = 0;
    gDmsAttempts = 0;
    gDmsActionAt = 0;
    gIpaInitSent = gIpaInitRsp = gIpaInitOk = gIpaIndReg = gIpaUcReady = gIpaIndSent = 0u;
    gIpaIndNode = gIpaIndPort = 0u;
    gIpaFilterReqs = gIpaFilterRules = gIpaOtherReqs = gIpaQmiBad = 0u;
    gUimSwitched = gUimProvisioned = gNasRegistered = gNasPsAttached = 0u;
    gDpmSent = gDpmDone = gDpmOk = gWdaSent = gWdaDone = gWdaOk = 0u;
    gWdsBindSent = gWdsBindDone = gWdsBindOk = gWdsIpFamSent = gWdsIpFamDone = 0u;
    gWdsStartPending = gWdsUp = gWdsStartAttempts = 0u;
    gWdsHandle = 0u;
    gDataSetupBegan = gWdsNextTry = 0u;
    gWmsEventSent = gWmsEventDone = gWmsEventOk = gWmsRoutesSent = gWmsRoutesDone = 0u;
    gWmsRouteCount = 0u;
    gWmsStoreSentMask = gWmsStoreDoneMask = gWmsListSentMask = gWmsListDoneMask = 0u;
    gWmsStoreMax[0] = gWmsStoreMax[1] = gWmsStoreFree[0] = gWmsStoreFree[1] = 0u;
    gWmsListCount[0] = gWmsListCount[1] = 0u;
    gWmsErrs = gWmsIndMt = gWmsIndRoute = 0u;
    cellSetSessionDown();
    for (unsigned idx = 0; idx < FM_TFTP_SLOTS; ++idx) tftpEnd(&gTftpXfers[idx]);
    qcom_qrtr_start(&gQrtr);
}
static void linkReceive(void *context, QcomGlinkChannel *channel, const uint8_t *data, uint32_t length)
{
    (void)context; (void)channel;
    /* 直接處理 QRTR DATA 並回送正確格式的 RESUME_TX（不碰共用 QcomQrtr.c，避免與音訊／震動線衝突）。 */
    if (length >= FM_QRTR_HEADER && fm_le32(data) == 1u && fm_le32(data + 4) == QCOM_QRTR_DATA) {
        const uint32_t src_node = fm_le32(data + 8), src_port = fm_le32(data + 12);
        const uint32_t confirm = fm_le32(data + 16), size = fm_le32(data + 20);
        const uint32_t dst_node = fm_le32(data + 24), dst_port = fm_le32(data + 28);
        if (size > length - FM_QRTR_HEADER) { gQrtr.rx_bad++; return; }
        gQrtr.rx_packets++;
        gQrtr.rx_data++;
        const uint8_t *payload = data + FM_QRTR_HEADER;
        int routed = 0;
        if (dst_port == FM_QMI_PORT) {
            qmiReceive(src_node, src_port, payload, size);
            routed = 1;
        } else if (dst_port == FM_IPA_CLIENT_PORT) {
            ipaClientReceive(src_node, src_port, payload, size);
            routed = 1;
        } else if (dst_port >= FM_TFTP_XFER_BASE && dst_port < FM_TFTP_XFER_BASE + FM_TFTP_SLOTS) {
            tftpTransferReceive(dst_port, src_node, src_port, payload, size);
            routed = 1;
        } else if (gBbMode && coachz_baseband_qrtr(dst_port, src_node, src_port, payload, size)) {
            routed = 1;
        } else {
            for (uint32_t idx = 0; idx < gQrtr.local_count; ++idx) {
                if (gQrtr.local[idx].port == dst_port) {
                    gQrtr.local[idx].receive(gQrtr.local[idx].context, &gQrtr, dst_port, src_node, src_port, payload, size);
                    routed = 1;
                    break;
                }
            }
        }
        if (!routed) gQrtr.rx_unrouted++;
        if (confirm) {
            const uint32_t resume_node = dst_node != QCOM_QRTR_NODE_BROADCAST ? dst_node : gQrtr.local_node;
            uint8_t resume[FM_QRTR_CTRL_PACKET];
            const uint32_t rlen = fm_qrtr_resume_tx(resume, resume_node, dst_port, src_node, src_port);
            if (queueSend(NULL, resume, rlen) == 0) {
                gQrtr.tx_packets++;
                gQrtr.resumes_sent++;
            } else {
                gQrtr.tx_refused++;
            }
        }
        return;
    }
    if (answerLookup(data, length)) return;
    noteServer(data, length);
    const uint8_t hello = gQrtr.hello_received;
    if (hello && length >= FM_QRTR_HEADER && fm_le32(data) == 1u && fm_le32(data + 4) == QCOM_QRTR_HELLO && gTftpDiagLogs < 24u) {
        gTftpDiagLogs++;
        printf("FUXI MODEM: qrtr repeat hello from node %u at %u ms\n", fm_le32(data + 8), msSince(gBegan));
    }
    qcom_qrtr_receive(&gQrtr, data, length);
    if (!hello && gQrtr.hello_received) {
        stage(StHello);
        printf("FUXI MODEM: qrtr hello from node %u at %u ms\n", gQrtr.remote_node, msSince(gBegan));
    }
}
static void bbSendQrtr(uint32_t local_port, uint32_t node, uint32_t port, const void *body, uint32_t length)
{
    if (!gGlinkStarted) return;
    const uint32_t pkt_len = fm_qrtr_data(gBbPktBuf, sizeof gBbPktBuf, gQrtr.local_node, local_port, node, port,
        (const uint8_t *)body, length);
    if (!pkt_len || queueSend(NULL, gBbPktBuf, pkt_len) != 0) {
        gQrtr.tx_refused++;
        return;
    }
    gQrtr.tx_packets++;
    pumpSends();
}
static void bbDelClientQrtr(uint32_t local_port)
{
    if (!gGlinkStarted) return;
    uint8_t pkt[FM_QRTR_CTRL_PACKET];
    const uint32_t words[4] = {gQrtr.local_node, local_port, 0u, 0u};
    const uint32_t dst_node = gQrtr.remote_node != QCOM_QRTR_NODE_BROADCAST ? gQrtr.remote_node : QCOM_QRTR_NODE_BROADCAST;
    const uint32_t pkt_len = fm_qrtr_ctrl(pkt, FM_QRTR_DEL_CLIENT, gQrtr.local_node, dst_node, FM_QRTR_PORT_CTRL, words);
    if (queueSend(NULL, pkt, pkt_len) == 0) {
        gQrtr.tx_packets++;
        pumpSends();
    } else {
        gQrtr.tx_refused++;
    }
}
static int startGlink(void)
{
    int rc = allocItem(FM_ITEM_GLINK_DESC, 32);
    if (!rc) rc = allocItem(FM_ITEM_GLINK_TX, FM_GLINK_FIFO);
    uint32_t size = 0;
    volatile uint8_t *desc = item(FM_ITEM_GLINK_DESC, NULL), *txf = item(FM_ITEM_GLINK_TX, &size);
    gIntents = kalloc_data(FM_INTENT_SIZE * FM_INTENT_COUNT, Z_WAITOK | Z_ZERO);
    if (rc || !desc || !txf || !gIntents) { fail(StGlink, rc ? rc : -2); return 0; }
    volatile uint32_t *d = (volatile uint32_t *)desc;
    const QcomGlinkFifo tx = {&d[1], &d[0], txf, size}, rx = {&d[3], &d[2], NULL, 0};
    qcom_glink_init(&gGlink, tx, rx, kickGlink, NULL);
    qcom_qrtr_init(&gQrtr, 1, queueSend, NULL);
    qcom_qrtr_add_service(&gQrtr, QCOM_SERVLOC_SERVICE, QCOM_SERVLOC_INSTANCE, 1, locator, NULL);
    qcom_qrtr_add_service(&gQrtr, FM_RMTFS_SERVICE, FM_RMTFS_INSTANCE, 2, rmtfs, NULL);
    if (gTftpMode) qcom_qrtr_add_service(&gQrtr, FM_TQFTP_SERVICE, FM_TQFTP_INSTANCE, FM_TFTP_PORT, tftpRequest, NULL);
    if (gIpaMode & 0x400u)
        qcom_qrtr_add_service(&gQrtr, FM_QMI_IPA, FM_IPA_HOST_INSTANCE, FM_IPA_HOST_PORT, ipaHostReceive, NULL);
    static const QcomGlinkClient glue = {linkState, linkReceive};
    gIpcrtr = qcom_glink_channel(&gGlink, "IPCRTR", FM_INTENT_SIZE, FM_INTENT_COUNT, gIntents, &glue, NULL,
        adtU32("fuxi,modem-open", 0) == 1);
    if (!gIpcrtr) { fail(StGlink, -28); return 0; }
    gGlinkStarted = 1;
    printf("FUXI MODEM: glink started (fifo %#x)\n", size);
    if (gBbMode) coachz_baseband_configure(bbSendQrtr, bbDelClientQrtr);
    return 1;
}
static void serviceGlink(void)
{
    if (!gGlinkStarted) return;
    if (!gGlink.rx.data) {
        uint32_t size = 0;
        volatile uint8_t *rx = item(FM_ITEM_GLINK_RX, &size);
        if (rx) qcom_glink_set_rx(&gGlink, rx, size);
    }
    const uint8_t agreed = gGlink.version_agreed;
    const uint32_t before = gGlink.rx_messages;
    qcom_glink_poll(&gGlink);
    pumpSends();
    if (gGlink.rx_messages != before) {
        uint64_t window;
        clock_interval_to_deadline(FM_FAST_MS, NSEC_PER_MSEC, &window);
        gFastUntil = window;
    }
    if (!agreed && gGlink.version_agreed) {
        stage(StGlink);
        printf("FUXI MODEM: glink version agreed at %u ms\n", msSince(gBegan));
    }
}

/* ---- 開機 ---- */
static int qmpUntil(int want, uint32_t ms)
{
    uint64_t deadline;
    clock_interval_to_deadline(ms, NSEC_PER_MSEC, &deadline);
    while (qcom_qmp_step(&gQmp) != want) {
        if (gQmp.state < 0 || mach_absolute_time() >= deadline) return qcom_qmp_step(&gQmp) == want;
        IOSleep(1);
    }
    return 1;
}
/* ADSP 擁有者也在跑時，等它的 DSP ready（QMP 與 SMEM 配置都排在它之後）。 */
static void waitForAdsp(void)
{
    if (fuxi_adsp_mode() < 2) return;
    stage(StAdspWait);
    uint32_t value = 0;
    unsigned ms = 0;
    for (; ms < 180000; ms += 100) {
        if (!slaveValue(FM_ADSP_PID, FM_ADSP_ITEM_IN, &value) && (value & (FM_READY | FM_FATAL))) break;
        IOSleep(100);
    }
    printf("FUXI MODEM: adsp slave-kernel %#x after %u ms; starting 3 s later\n", value, ms);
    IOSleep(3000);
}
static int start(unsigned mode)
{
    int rc;
    waitForAdsp();
    if ((rc = openFirmware(1))) { fail(StFirmware, rc); return 0; }
    const unsigned misfit = checkFit("modem_dtb", &gDtbImage, FM_DTB_BASE, FM_DTB_SIZE) +
        checkFit("modem", &gModemImage, FM_MPSS_BASE, FM_MPSS_SIZE);
    stage(StFirmware);
    if (misfit) { fail(StFirmware, -27); release(&gDtbImage); release(&gModemImage); return 0; }
    if (!gPartOk) { fail(StSmp2p, -2); release(&gDtbImage); release(&gModemImage); return 0; }
    if (gSlaveKnown && gSlave) { fail(StFirmware, -114); release(&gDtbImage); release(&gModemImage); return 0; }
    if (mode >= 3) { loadEfs(); shareRmtfsBuffer(); }

    if ((rc = allocItem(FM_ITEM_OUT, QCOM_SMP2P_ITEM_BYTES))) { fail(StSmp2p, rc); release(&gDtbImage); release(&gModemImage); return 0; }
    qcom_smp2p_init_out(&gP2p, (volatile uint32_t *)item(FM_ITEM_OUT, NULL), 0, FM_REMOTE_PID);
    qcom_smp2p_add_entry(&gP2p, "master-kernel");
    if (gIpaMode & 0x0fu) {
        const int ipa_idx = qcom_smp2p_add_entry(&gP2p, "ipa");
        if (ipa_idx >= 0) {
            (void)qcom_smp2p_update(&gP2p, ipa_idx, 0xffffffffu, gIpaMode & 0x0fu);
            printf("FUXI MODEM: smp2p entry \"ipa\" idx=%d val=%#x\n", ipa_idx, gIpaMode & 0x0fu);
        }
    }
    if (gIpaMode & 0x10u) {
        const int ipa_rc = allocItem(FM_ITEM_IPA_FILTER, FM_IPA_SMEM_SIZE);
        uint32_t ipa_at = 0u, ipa_sz = 0u;
        if (!ipa_rc && gPartOk && qcom_smem_find(&gSmem, &gPart, FM_ITEM_IPA_FILTER, &ipa_at, &ipa_sz) == QcomSmemOk && ipa_sz) {
            for (uint32_t off = 0u; off + 4u <= ipa_sz; off += 4u) qcom_smem_write32(&gSmem, ipa_at + off, 0u);
            uint32_t per_words[FM_IPA5_PER_STATS_WORDS];
            const uint32_t per_cnt = fm_ipa_init_per_stats_words(per_words, FM_IPA5_PER_STATS_WORDS);
            if (per_cnt && ipa_sz >= per_cnt * 4u) {
                for (uint32_t idx = 0u; idx < per_cnt; ++idx) qcom_smem_write32(&gSmem, ipa_at + idx * 4u, per_words[idx]);
            }
            __asm__ volatile("dsb sy" ::: "memory");
            gIpaSmemPhys = (uint32_t)(FM_SMEM + (uint64_t)ipa_at);
            gIpaSmemSize = ipa_sz;
        }
        printf("FUXI MODEM: smem ipa item %u (%#x B pa=%#x) rc=%d\n", FM_ITEM_IPA_FILTER, gIpaSmemSize, gIpaSmemPhys, ipa_rc);
    }
    gicMask(FM_INTID_IPCC);
    kickSmp2p();
    stage(StSmp2p);

    qcom_qmp_init(&gQmp, (volatile uint32_t *)gAoss.va, 0x400, kickAop, NULL);
    for (int phase = 0; phase < 4 && qcom_qmp_step(&gQmp) > 0 && gQmp.state != QcomQmpOpen; ++phase) qmpUntil(QcomQmpOpen, 1000);
    int sent = QcomQmpBusy;
    for (unsigned tries = 0; gQmp.state == QcomQmpOpen && tries < 100 && (sent = qcom_qmp_send(&gQmp, "{class: image, res: load_state, name: modem, val: on}")) == QcomQmpBusy; ++tries)
        IOSleep(10);
    if (sent != QcomQmpSending || !qmpUntil(QcomQmpOpen, 1000)) {
        if (sent == QcomQmpSending) qcom_qmp_abandon(&gQmp);
        fail(StLoadState, gQmp.state < 0 ? gQmp.state : -60); release(&gDtbImage); release(&gModemImage); return 0;
    }
    stage(StLoadState);
    printf("FUXI MODEM: aoss load_state modem on\n");

    const int with_cx = adtU32("fuxi,modem-cx", 0) == 1;
    if ((rc = lookupRails(with_cx)) || (rc = voteMss(1)) || (with_cx && (rc = voteCxMax()))) {
        fail(StRails, rc); if (gVoted) voteMss(0); release(&gDtbImage); release(&gModemImage); return 0;
    }
    stage(StRails);

    if (adtU32("fuxi,modem-nodsm", 0) != 1) {
        const uint32_t sources[1] = {FM_VMID_HLOS}, vmids[1] = {FM_VMID_MSS_MSA}, perms[1] = {FM_PERM_RW};
        rc = assignMemory(FM_DSM_BASE, FM_DSM_SIZE, sources, 1, vmids, perms, 1);
        printf("FUXI MODEM: dsm %#llx + %#llx assigned to MSS_MSA rc=%d\n", FM_DSM_BASE, FM_DSM_SIZE, rc);
        if (rc) { fail(StDsm, rc); voteMss(0); release(&gDtbImage); release(&gModemImage); return 0; }
    }
    stage(StDsm);
    ipaBringup();

    gBegan = mach_absolute_time();
    if ((rc = authenticateHeader(&gDtbImage, FM_PAS_DTB, FM_DTB_BASE)) || (rc = loadSegments(&gDtbImage, FM_DTB_BASE, FM_DTB_SIZE)) ||
        (rc = scmOk(pil(QCOM_SCM_PAS_AUTH_AND_RESET, qcom_scm_args(1, 0, 0, 0), FM_PAS_DTB, 0, 0)) ? 0 : -3)) {
        fail(StDtb, rc); voteMss(0); release(&gDtbImage); release(&gModemImage); return 0;
    }
    stage(StDtb);
    printf("FUXI MODEM: modem_dtb authenticated and started at %u ms\n", msSince(gBegan));
    if ((rc = authenticateHeader(&gModemImage, FM_PAS_MODEM, FM_MPSS_BASE)) ||
        (rc = loadSegments(&gModemImage, FM_MPSS_BASE, FM_MPSS_SIZE))) {
        fail(StLoaded, rc); shutdownModem(1, 0); voteMss(0); release(&gDtbImage); release(&gModemImage); return 0;
    }
    stage(StLoaded);
    printf("FUXI MODEM: modem loaded at %u ms\n", msSince(gBegan));
    if (!scmOk(pil(QCOM_SCM_PAS_AUTH_AND_RESET, qcom_scm_args(1, 0, 0, 0), FM_PAS_MODEM, 0, 0))) {
        fail(StStarted, -3); shutdownModem(1, 0); voteMss(0); release(&gDtbImage); release(&gModemImage); return 0;
    }
    stage(StStarted);
    const uint64_t reset = mach_absolute_time();
    printf("FUXI MODEM: modem authenticated and reset at %u ms; waiting for ready\n", msSince(gBegan));
    if (mode >= 3) startGlink(); /* 遠端的 FIFO 晚點才出現，serviceGlink 會補上 */
    for (unsigned ms = 0; ms < 15000 && !(gSlave & (FM_READY | FM_FATAL)); ms += 5) {
        serviceSmp2p();
        serviceGlink();
        serviceIpa();
        IOSleep(5);
    }
    release(&gDtbImage);
    release(&gModemImage);
    if (!(gSlave & FM_READY) || (gSlave & FM_FATAL)) {
        readCrashReason();
        fail(StReady, (gSlave & FM_FATAL) ? -5 : -60);
        return 0; /* 不關掉：留著看它之後的狀態與遺言 */
    }
    stage(StReady);
    printf("FUXI MODEM: ready after %u ms (slave-kernel %#x)\n", msSince(reset), gSlave);
    return 1;
}

static void report(void)
{
    flushServers();
    unsigned tftp_active = 0;
    for (unsigned idx = 0; idx < FM_TFTP_SLOTS; ++idx) tftp_active += gTftpXfers[idx].active ? 1u : 0u;
    printf("FUXI MODEM: stage=%d err=%d slave=%#x ho=%u glink in=%u out=%u unk=%u(%s) drop=%u\n",
        gStage, gError, gSlave, gHandedOver, gGlink.rx_messages, gGlink.tx_messages, gGlink.unknown_opens, gGlink.unknown_names,
        gQueueDropped);
    printf("FUXI MODEM: qrtr node=%d rx=%u tx=%u ref=%u res=%u unr=%u bad=%u srv=%u lk=%u/%u sloc=%u rmt=%u(o%u r%u w%u x%u s%llu/%llu) qmi=%u/%u/%u/%u ind=%u tftp=%u/%u/%u/%u act=%u\n",
        gQrtr.hello_received ? (int)gQrtr.remote_node : -1, gQrtr.rx_packets, gQrtr.tx_packets, gQrtr.tx_refused, gQrtr.resumes_sent,
        gQrtr.rx_unrouted, gQrtr.rx_bad,
        gServers, gLookups, gLookupAnswers, gServloc, gRmtfs.requests, gRmtfs.opens, gRmtfs.reads, gRmtfs.writes, gRmtfs.refused,
        (unsigned long long)gRmtfs.sectors_read, (unsigned long long)gRmtfs.sectors_written, gQmiMode, gQmiTx, gQmiRx, gQmiBad,
        gQmiInd, gTftpRequests, gTftpServed, gTftpRefused, gTftpWrites, tftp_active);
    if (gQmiMode >= 3u) {
        printf("FUXI MODEM: data uim=%u/%u nas=%u/%u dpm=%u/%u wda=%u/%u bind=%u/%u ipfam=%u up=%u(%#x) att=%u\n",
            gUimSwitched, gUimProvisioned, gNasRegistered, gNasPsAttached, gDpmDone, gDpmOk, gWdaDone, gWdaOk,
            gWdsBindDone, gWdsBindOk, gWdsIpFamDone, gWdsUp, gWdsHandle, gWdsStartAttempts);
    }
    if (gQmiMode >= 4u) {
        printf("FUXI MODEM: wms found=%u ev=%u/%u routes=%u(%u) store=%#x(uim=%u/%u nv=%u/%u) list=%#x(uim=%u nv=%u) ind=%u/%u err=%u\n",
            qmiFindTarget(FM_QMI_WMS) ? 1u : 0u, gWmsEventDone, gWmsEventOk, gWmsRoutesDone, gWmsRouteCount,
            gWmsStoreDoneMask, gWmsStoreFree[0], gWmsStoreMax[0], gWmsStoreFree[1], gWmsStoreMax[1],
            gWmsListDoneMask, gWmsListCount[0], gWmsListCount[1], gWmsIndMt, gWmsIndRoute, gWmsErrs);
    }
    if (gIpaMode & 0x600u) {
        printf("FUXI MODEM: ipa hw=%u gsi=%u cmds=%u/%u qmi=%u/%u/%u uc=%u reg=%u ind=%u flt=%u(%u) oth=%u bad=%u rx_lan=%u/%u rx_mdm=%u/%u\n",
            gIpaHwConfigured, gIpaGsiReady, gIpaCmdDone, gIpaCmdErrs, gIpaInitSent, gIpaInitRsp, gIpaInitOk,
            gIpaUcReady, gIpaIndReg, gIpaIndSent, gIpaFilterReqs, gIpaFilterRules, gIpaOtherReqs, gIpaQmiBad,
            gIpaChans[1].rx_events, gIpaChans[1].rx_bytes, gIpaChans[3].rx_events, gIpaChans[3].rx_bytes);
    }
    if ((gIpaMode & 0x800u) && !gBbMode) {
        printf("FUXI MODEM: cell en1=%u link=%u arp=%u dhcp=%u tx_ip=%u tx_drop=%u tx_nonip=%u rx_ip=%u rx_drop=%u qmap_cmd=%u/%u qmap_bad=%u gsi_tx=%u/%u/%uB(d=%u e=%u)\n",
            gCellAttached, __atomic_load_n(&gCellLinkUp, __ATOMIC_ACQUIRE),
            __atomic_load_n(&gCellArpReplies, __ATOMIC_RELAXED), __atomic_load_n(&gCellDhcpReplies, __ATOMIC_RELAXED),
            __atomic_load_n(&gCellTxIpv4, __ATOMIC_RELAXED), __atomic_load_n(&gCellTxDrop, __ATOMIC_RELAXED),
            __atomic_load_n(&gCellTxNonIpv4, __ATOMIC_RELAXED), __atomic_load_n(&gCellRxIpv4, __ATOMIC_RELAXED),
            __atomic_load_n(&gCellRxDrop, __ATOMIC_RELAXED), gCellRxQmapCmds, gCellTxQmapAcks, gCellRxQmapBad,
            gIpaTxSubmitted, gIpaTxCompleted, gIpaTxBytes, gIpaTxDrops, gIpaTxErrs);
    }
    if (gBbMode) {
        printf("FUXI MODEM: bb mode=%#x tx=%u(d=%u) rx=%u(d=%u) gsi_tx=%u/%u/%uB(d=%u e=%u) qmap_cmd=%u/%u qmap_bad=%u\n",
            gBbMode, gBbTxPackets, gBbTxDrops, gBbRxPackets, gBbRxDrops,
            gIpaTxSubmitted, gIpaTxCompleted, gIpaTxBytes, gIpaTxDrops, gIpaTxErrs,
            gCellRxQmapCmds, gCellTxQmapAcks, gCellRxQmapBad);
        coachz_baseband_report(gBegan ? msSince(gBegan) / 1000u : 0u);
    }
}

static void modemThread(void *arg, wait_result_t result)
{
    (void)result;
    const unsigned mode = (unsigned)(uintptr_t)arg;
    if (fm_map(&gSmemW, VM_WIMG_IO) != KERN_SUCCESS || fm_map(&gTcsr, VM_WIMG_IO) != KERN_SUCCESS ||
        fm_map(&gIpcc, VM_WIMG_IO) != KERN_SUCCESS || fm_map(&gAoss, VM_WIMG_IO) != KERN_SUCCESS ||
        fm_map(&gRsc, VM_WIMG_IO) != KERN_SUCCESS || fm_map(&gGicd, VM_WIMG_IO) != KERN_SUCCESS) {
        fail(StMapped, -12);
        thread_terminate(current_thread());
        return;
    }
    gSmem.base = (volatile uint8_t *)gSmemW.va;
    gSmem.size = FM_SMEM_SIZE;
    gLayout = qcom_rsc_layout(rd(&gRsc, 0));
    stage(StMapped);
    /* 韌體檔：探測時也讀標頭（不重試，UFS 未就緒就記一筆） */
    if (openFirmware(mode >= 2) == 0) {
        checkFit("modem_dtb", &gDtbImage, FM_DTB_BASE, FM_DTB_SIZE);
        checkFit("modem", &gModemImage, FM_MPSS_BASE, FM_MPSS_SIZE);
        release(&gDtbImage);
        release(&gModemImage);
    } else {
        printf("FUXI MODEM: firmware not readable yet\n");
    }
    survey();
    stage(StSurveyed);
    if (mode >= 2) {
        start(mode);
        /* 之後只輪詢：最近有動靜時 2 ms，否則 50 ms（GLINK 開著）或 200 ms */
        uint64_t next_report;
        clock_interval_to_deadline(30000, NSEC_PER_MSEC, &next_report);
        unsigned reports = 0;
        for (;;) {
            serviceSmp2p();
            serviceGlink();
            serviceIpa();
            if (gBbMode) coachz_baseband_poll();
            serviceQmi();
            tftpExpireIdle();
            if (__atomic_exchange_n(&gCellActiveHint, 0u, __ATOMIC_ACQ_REL)) {
                uint64_t window = 0;
                clock_interval_to_deadline(2000u, NSEC_PER_MSEC, &window);
                if (window > gFastUntil) gFastUntil = window;
            }
            const int fast = mach_absolute_time() < gFastUntil || gQueueCount;
            const int cell_up = __atomic_load_n(&gCellLinkUp, __ATOMIC_ACQUIRE) ? 1 : 0;
            IOSleep((fast || (gBbMode && gGlinkStarted)) ? 2 : cell_up ? 5 : gGlinkStarted ? 50 : 200);
            if (mach_absolute_time() >= next_report) {
                report();
                clock_interval_to_deadline(++reports < 10 ? 30000u : 600000u, NSEC_PER_MSEC, &next_report);
            }
        }
    }
    report();
    thread_terminate(current_thread());
}

/* sweet_ufs.c 在 UFS 就緒、fuxi_adsp_init 之後呼叫一次（韌體經它讀取）。 */
void fuxi_modem_init(void)
{
    thread_t thread = NULL;
    const unsigned mode = fm_mode();
    if (!mode || gStarted) return;
    gStarted = 1;
    gMode = mode;
    gQmiMode = adtU32("fuxi,modem-qmi", 0);
    gTftpMode = adtU32("fuxi,modem-tftp", 0);
    gIpaMode = adtU32("fuxi,modem-ipa", 0);
    gBbMode = adtU32("fuxi,modem-bb", 0);
    if (kernel_thread_start(modemThread, (void *)(uintptr_t)mode, &thread) != KERN_SUCCESS) {
        fail(StMapped, -7);
        return;
    }
    thread_deallocate(thread);
    printf("FUXI MODEM: started mode %u (cx=%u nodsm=%u open=%u qmi=%u tftp=%u ipa=%#x bb=%#x)\n", mode, adtU32("fuxi,modem-cx", 0),
        adtU32("fuxi,modem-nodsm", 0), adtU32("fuxi,modem-open", 0), gQmiMode, gTftpMode, gIpaMode, gBbMode);
}

/*
 * Opt-in helper for SWEET Sandbox op 2 (sandbox_check) when fuxi,modem-bb != 0:
 * BlastDoor/Sandbox.swift aborts MessagesBlastDoorService with SIGTRAP
 * ("BlastDoor cannot run unsandboxed.") if sandbox_check(getpid(), NULL, 0) != 1,
 * dropping every incoming SMS after CommCenter ACKs QMI WMS. Only answers the
 * operation==NULL query for MessagesBlastDoorService / MessagesAirlockService;
 * every other process and every operation!=NULL check stays permissive (0).
 */
int fuxi_modem_sandbox_check(int pid, user_addr_t arg)
{
    static uint32_t gSandboxLogged;
    if (!gBbMode || !arg) return 0;
    FmSandboxCheckArgs req;
    if (copyin(arg, &req, sizeof(req)) != 0) return 0;
    if (req.out_result_ptr == 0u || req.operation != 0u || req.filter_type != 0u) return 0;
    char name[32];
    name[0] = '\0';
    proc_name(pid, name, (int)sizeof(name));
    if (fm_sandbox_check_wants_sandboxed(name, &req)) {
        const uint64_t one = 1u;
        const int err = copyout(&one, (user_addr_t)req.out_result_ptr, sizeof(one));
        const uint32_t n = __atomic_add_fetch(&gSandboxLogged, 1u, __ATOMIC_RELAXED);
        if (n <= 8u) {
            printf("FUXI MODEM: sandbox_check(NULL) -> 1 for %s (pid=%d err=%d n=%u)\n", name, pid, err, n);
        }
    }
    return 0;
}
#endif /* FUXI */
