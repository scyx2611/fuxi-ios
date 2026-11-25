/* SPDX-License-Identifier: MIT */
/* FUXI 前鏡頭（cam-sensor1：CCI1 master 1，CSIPHY 4）上電與辨識。開機參數 fuxicamera，或 ADT /chosen u32 "fuxi,camera"
 * （命令列已滿時）：
 *   0  關（預設）。
 *   1  唯讀：四支腳（gpio104 MCLK4、120 reset、208/209 CCI1 m1 的 aon_cci）的 CTL／IO、GCC 的相機時脈、camcc 的 titan top
 *      GDSC／MCLK4／CCI1 等（只有 GCC camera AHB 開著才讀 camcc，否則匯流排會卡）、cmd-db 的 ldom1／3／5 位址。什麼都不寫。
 *   2  再照 Android 的順序上電：reset 拉低 → vio（ldom5 1.8 V）→ vana（ldom3 2.8 V）→ vdig（ldom1 1.104 V）→ titan top GDSC
 *      → MCLK4 19.2 MHz（XO 直出）→ gpio104 切 cam_aon_mclk4 → reset 放開；在 208/209 上手動 I2C 掃 0x08–0x77、對 ACK 的位址
 *      讀常見 chip ID 暫存器、EEPROM 範圍只做「目前位址讀」16 個位元組；之後反序斷電，所有暫存器還原成找到時的樣子。
 * 其他 ADT 開關（u32，預設 0）：fuxi,camera-swap=1 先試 SDA=209／SCL=208（預設先試 208／209，沒有任何 ACK 才換）。
 *
 * 安全規則：只寫上面列的暫存器；RPMh 只投三顆相機 LDO（DT 的電壓，永不投 MMCX／CX，顯示器已經開著 MMCX，同一個 DRV 投票
 * 會蓋掉它的票）；EEPROM 範圍（0x50–0x5F）永不送寫入位元組；任何一步失敗就照反序把開過的東西關掉，不重試。
 * RPMh 經 fuxi_rpmh_shared 的鎖，TCS 1 閒置時才用（與 fuxi_modem.c／fuxi_adsp.c 的 vote() 同法）。
 *
 * 參考：HakiOS hakios.camera.c（POCO X5 Pro，APSL 2.0）的 cam=1／cam=2 分段與上電順序（reset 低 1 ms、各電源、MCLK 後 4 ms、
 * reset 放開後等待；GDSC 在 MCLK 之前開）及「I2C by hand」；Linux camcc-sm8550.c（MCLK4 RCG 0x154c0、branch 0x155ec、CCI1
 * 0x13a30／0x13b5c、titan top GDSC 0x14058 帶 POLL_CFG_GDSCR）、gcc-sm8550.c（camera AHB 0x26004、XO 0x26028）、
 * pinctrl-sm8550.c、gdsc.c（GPL，只對照位址與位元）；裝置樹事實來自本機 live DTB（work/camera-modem-survey/fuxi-live-20261116.dts：
 * cam-sensor1 的 gpios、cam_vio／cam_vdig／cam_clk／cam_vana 與電壓、cci_aon_i2c_active 的 qcom,apps／上拉／2 mA）。
 * camcc 基底 0xade0000、TLMM 0xf100000（APPS 的視窗；fuxi_amp.c 同）、GCC 0x100000、cmd-db 0x81c60000、apps RSC DRV2 0x17a20000。 */
#include <pexpert/arm64/board_config.h>
#if defined(FUXI)
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <miscfs/devfs/devfs.h>
#include <kern/clock.h>
#include <kern/locks.h>
#include <kern/sched_prim.h>
#include <kern/thread.h>
#include <kern/kalloc.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include <pexpert/device_tree.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>
#include "QcomRpmh.h"
#include "fuxi_rpmh_shared.h"
#include "fuxi_camera_proto.h"

extern kern_return_t pmap_enter_options_addr(pmap_t, vm_map_offset_t, uint64_t, vm_prot_t, vm_prot_t, unsigned, boolean_t,
    unsigned, void *);
extern ppnum_t pmap_find_phys(pmap_t, addr64_t);
extern kern_return_t kernel_thread_start(thread_continue_t, void *, thread_t *);
extern kern_return_t thread_terminate(thread_t);
extern void thread_deallocate(thread_t);
extern void IOSleep(unsigned milliseconds);
void fuxi_camera_init(void);

#define FC_TLMM 0xf100000ull
#define FC_GCC_WINDOW 0x124000ull /* GCC 0x100000 + 0x24000：涵蓋 0x26004–0x2602c */
#define FC_GCC_CAMERA_AHB 0x2004u /* 0x26004 */
#define FC_GCC_CAMERA_HF_AXI 0x2010u
#define FC_GCC_CAMERA_SF_AXI 0x201cu
#define FC_GCC_CAMERA_XO 0x2028u
#define FC_CAMCC 0xade0000ull
#define FC_CAMCC_SIZE 0x20000u
#define FC_TITAN_GDSCR 0x14058u
#define FC_GDSC_CLK 0x1419cu  /* cam_cc_gdsc_clk（Linux 視為常開） */
#define FC_SLEEP_CLK 0x142ccu /* cam_cc_sleep_clk（常開） */
#define FC_CPAS_AHB 0x13c90u
#define FC_CPAS_FAST_AHB 0x13c9cu /* cam_cc_cpas_fast_ahb_clk（BRANCH_HALT；SM8550 camss 不用 0x1406c core_ahb） */
#define FC_SLOW_AHB_RCG 0x10148u
#define FC_FAST_AHB_RCG 0x10018u /* cam_cc_fast_ahb_clk_src（XO 19.2 MHz / PLL0_OUT_EVEN div 2 = 300 MHz） */
#define FC_CAMNOC_AXI_RCG 0x13de0u
#define FC_CPHY_RX_RCG 0x11290u
#define FC_CSI4PHYTIMER_RCG 0x15e54u
#define FC_IFE_LITE_RCG 0x13000u
#define FC_IFE_LITE_CSID_RCG 0x1313cu
#define FC_CAMNOC_AXI_CBCR 0x13f0cu
#define FC_CPAS_IFE_LITE_CBCR 0x13138u
#define FC_IFE_LITE_AHB_CBCR 0x13278u
#define FC_IFE_LITE_CBCR 0x1312cu
#define FC_IFE_LITE_CPHY_RX_CBCR 0x13274u
#define FC_IFE_LITE_CSID_CBCR 0x13268u
#define FC_CSIPHY4_CBCR 0x15f84u
#define FC_CSI4PHYTIMER_CBCR 0x15f80u
#define FC_CSID_CSIPHY_RX_CBCR 0x15ab4u
#define FC_CPAS_PAGE 0xac10000ull
#define FC_CPAS_OFF 0x3000u
#define FC_LITE_PAGE 0xacc8000ull
#define FC_LITE_SIZE 0x8000u
#define FC_CSID_LITE0_OFF 0x2000u
#define FC_VFE_LITE0_OFF 0x3000u
#define FC_CSIPHY4_PAGE 0xacec000ull
#define FC_CSIPHY_CTRL6 0x1018u
#define FC_CSIPHY_STATUS12 0x10e0u
#define FC_TPG0_PAGE 0xacf4000ull
#define FC_TPG0_OFF 0x2000u
#define FC_TPG_WIDTH 256u
#define FC_TPG_HEIGHT 128u
#define FC_TPG_FRAME_BYTES (FC_TPG_WIDTH * FC_TPG_HEIGHT)
#define FC_TPG_CANARY 0x5au
#define FC_MCLK4_RCG 0x154c0u /* CMD；CFG +4、M +8、N +0xc、D +0x10 */
#define FC_MCLK4_CBCR 0x155ecu
#define FC_CCI1_RCG 0x13a30u
#define FC_CCI1_CBCR 0x13b5cu
#define FC_RSC 0x17a20000ull
#define FC_RSC_SIZE 0x10000u
#define FC_RSC_TCS 0xd00u
#define FC_CMD_DB 0x81c60000ull
#define FC_CMD_DB_SIZE 0x20000u
#define FC_PIN_MCLK 104u
#define FC_PIN_RESET 120u
#define FC_PIN_SDA 208u
#define FC_PIN_SCL 209u
#define FC_MAX_ACKS 8u
#define FC_PIN_COUNT 20u

struct fc_window { uint64_t pa; vm_size_t size; vm_offset_t va; };
static struct fc_window gGcc = {FC_GCC_WINDOW, 0x4000, 0}, gCamcc = {FC_CAMCC, FC_CAMCC_SIZE, 0}, gRsc = {FC_RSC, FC_RSC_SIZE, 0};
/* 所有待命鏡頭共用之 TLMM 腳位（16 KiB 頁去重）：
 * 前鏡 104/120/208/209、超廣角 101/117/91/110/111、長焦 103/119/57/114/115、主攝 102/118/69/66/112/113 */
static const unsigned kAllPins[FC_PIN_COUNT] = {
    104u, 120u, 208u, 209u,
    101u, 117u, 91u, 110u, 111u,
    103u, 119u, 57u, 114u, 115u,
    102u, 118u, 69u, 66u, 112u, 113u,
};
static struct fc_window gPin[FC_PIN_COUNT];
static uint32_t gFoundCtl[FC_PIN_COUNT], gFoundIo[FC_PIN_COUNT];
static QcomRscLayout gLayout;
static int gStarted;

/* 作用中鏡頭之 RPMh 電源軌狀態；shared_keep_on=1 者於斷電路徑永不投票關閉。 */
struct fc_rail_state { struct fc_sensor_rail spec; uint32_t address; int on; };
static struct fc_rail_state gRails[FC_MAX_SENSOR_RAILS];
static unsigned gRailCount;
static uint32_t gMm0Address;

/* ---- MMIO ---- */
static uint32_t rd(const struct fc_window *w, uint32_t at) { return *(volatile uint32_t *)(w->va + at); }
static void wr(const struct fc_window *w, uint32_t at, uint32_t value)
{
    *(volatile uint32_t *)(w->va + at) = value;
    __asm__ volatile("dsb sy" ::: "memory");
}
static kern_return_t fc_map(struct fc_window *w)
{
    vm_offset_t va = 0;
    if (w->va) return KERN_SUCCESS;
    if (!w->size || (w->pa & PAGE_MASK) || (w->size & PAGE_MASK)) return KERN_INVALID_ARGUMENT;
    kern_return_t kr = kmem_alloc(kernel_map, &va, w->size, KMA_VAONLY | KMA_KOBJECT | KMA_NOPAGEWAIT, VM_KERN_MEMORY_IOKIT);
    if (kr != KERN_SUCCESS) return kr;
    for (vm_size_t off = 0; off < w->size; off += PAGE_SIZE) {
        kr = pmap_enter_options_addr(kernel_pmap, va + off, w->pa + off, VM_PROT_READ | VM_PROT_WRITE, VM_PROT_NONE, VM_WIMG_IO, TRUE,
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
static void fc_unmap(struct fc_window *w)
{
    if (!w->va) return;
    pmap_remove(kernel_pmap, w->va, w->va + w->size);
    kmem_free(kernel_map, w->va, w->size);
    w->va = 0;
}
static uint64_t pinPa(unsigned gpio) { return FC_TLMM + (uint64_t)gpio * 0x1000u; }
static int pinIndex(unsigned gpio)
{
    if (gpio == FC_GPIO_NONE) return -1;
    for (unsigned i = 0; i < FC_PIN_COUNT; ++i) if (kAllPins[i] == gpio) return (int)i;
    return -1;
}
static uint32_t pinOff(unsigned gpio, unsigned idx) { return (uint32_t)(pinPa(gpio) - gPin[idx].pa); }
static uint32_t pinCtl(unsigned gpio)
{
    const int idx = pinIndex(gpio);
    return idx >= 0 ? rd(&gPin[idx], pinOff(gpio, (unsigned)idx)) : 0u;
}
static uint32_t pinIo(unsigned gpio)
{
    const int idx = pinIndex(gpio);
    return idx >= 0 ? rd(&gPin[idx], pinOff(gpio, (unsigned)idx) + 4u) : 0u;
}
static void pinSetCtl(unsigned gpio, uint32_t v)
{
    const int idx = pinIndex(gpio);
    if (idx >= 0) wr(&gPin[idx], pinOff(gpio, (unsigned)idx), v);
}
static void pinSetIo(unsigned gpio, uint32_t v)
{
    const int idx = pinIndex(gpio);
    if (idx >= 0) wr(&gPin[idx], pinOff(gpio, (unsigned)idx) + 4u, v);
}
static uint32_t foundCtl(unsigned gpio)
{
    const int idx = pinIndex(gpio);
    return idx >= 0 ? gFoundCtl[idx] : 0u;
}
static void pinRestore(unsigned gpio)
{
    const int idx = pinIndex(gpio);
    if (idx < 0) return;
    pinSetIo(gpio, gFoundIo[idx] & FC_IO_OUT);
    pinSetCtl(gpio, gFoundCtl[idx]);
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
static uint32_t fc_pick_sensor(const char *prop_name)
{
    const uint32_t id = adtU32(prop_name, FC_SENSOR_FRONT);
    return fc_sensor_hw_for(id) ? id : FC_SENSOR_FRONT;
}
static unsigned fc_mode(void)
{
    unsigned mode = 0;
    if (PE_parse_boot_argn("fuxicamera", &mode, sizeof(mode))) return mode > 8 ? 8 : mode;
    mode = adtU32("fuxi,camera", 0);
    return mode > 8 ? 8 : mode;
}
/* 等某個暫存器的 mask 位元變成 expected；每 10 µs 看一次，最多 us 微秒。回傳 1＝等到。 */
static int waitBits(const struct fc_window *w, uint32_t at, uint32_t mask, uint32_t expected, unsigned us)
{
    for (unsigned t = 0; t <= us; t += 10) {
        if ((rd(w, at) & mask) == expected) return 1;
        delay(10);
    }
    return (rd(w, at) & mask) == expected;
}

/* ---- RPMh：TCS 1 的一次 active 請求（fuxi_modem.c vote() 同法） ---- */
static int readRailsFor(const struct fc_sensor_hw *hw)
{
    const unsigned target_count = hw->rail_count <= FC_MAX_SENSOR_RAILS ? hw->rail_count : FC_MAX_SENSOR_RAILS;
    struct fc_rail_state tmp[FC_MAX_SENSOR_RAILS];
    bzero(tmp, sizeof(tmp));
    for (unsigned i = 0; i < target_count; ++i) tmp[i].spec = hw->rails[i];
    struct fc_window db = {FC_CMD_DB, FC_CMD_DB_SIZE, 0};
    uint8_t *copy = kalloc_data(FC_CMD_DB_SIZE, Z_WAITOK);
    unsigned found = 0u;
    uint32_t mm0_found = 0u;
    if (copy && fc_map(&db) == KERN_SUCCESS) {
        for (uint32_t i = 0; i < FC_CMD_DB_SIZE; i += 4) {
            const uint32_t w = rd(&db, i);
            memcpy(copy + i, &w, 4);
        }
        fc_unmap(&db);
        for (unsigned i = 0; i < target_count; ++i) {
            QcomRpmhResource r;
            if (!qcom_cmd_db_find(copy, FC_CMD_DB_SIZE, tmp[i].spec.name, &r) && fc_is_vrm(r.address)) {
                tmp[i].address = r.address;
                ++found;
            }
        }
        QcomRpmhResource mmcx;
        const uint32_t mmcx_address = qcom_cmd_db_find(copy, FC_CMD_DB_SIZE, "mmcx.lvl", &mmcx) ? 0 : mmcx.address;
        QcomRpmhResource mm0;
        mm0_found = (!qcom_cmd_db_find(copy, FC_CMD_DB_SIZE, "MM0", &mm0) && fc_is_bcm(mm0.address)) ? mm0.address : 0u;
        printf("FUXI CAMERA: cmd-db [%s] rails=%u/%u (mmcx.lvl %#x never voted, MM0 %#x)\n",
            hw->name, found, target_count, mmcx_address, mm0_found);
        for (unsigned i = 0; i < target_count; ++i) {
            printf("FUXI CAMERA:   rail %u: %s %s=%#x %u mV mode=%u shared=%u\n", i, tmp[i].spec.role,
                tmp[i].spec.name, tmp[i].address, tmp[i].spec.mv, tmp[i].spec.mode, tmp[i].spec.shared_keep_on);
        }
    } else {
        printf("FUXI CAMERA: cmd-db not readable\n");
    }
    if (copy) kfree_data(copy, FC_CMD_DB_SIZE);
    if (found != target_count) return 0;
    /* 僅在全部電源軌皆成功定位後才原子替換 gRails/gRailCount，避免局部失敗錯位或誤關共用軌。 */
    bzero(gRails, sizeof(gRails));
    memcpy(gRails, tmp, sizeof(struct fc_rail_state) * target_count);
    gRailCount = target_count;
    gMm0Address = mm0_found;
    return 1;
}
static int vote(const uint32_t *addresses, const uint32_t *values, unsigned count)
{
    const unsigned tcs = 1;
    const uint32_t bit = 1u << tcs;
    if (fuxi_rpmh_acquire()) return -EIO;
    const uint32_t base = tcs * gLayout.tcs_stride;
    if (!rd(&gRsc, FC_RSC_TCS + base + gLayout.status) || rd(&gRsc, FC_RSC_TCS + base + gLayout.control) ||
        rd(&gRsc, FC_RSC_TCS + base + gLayout.cmd_enable) || rd(&gRsc, FC_RSC_TCS + base + gLayout.wait_for_cmpl) ||
        (rd(&gRsc, FC_RSC_TCS + gLayout.irq_status) & bit)) {
        fuxi_rpmh_release(1);
        return -EBUSY;
    }
    QcomRpmhWrite writes[16];
    const unsigned n = qcom_rpmh_active_request(&gLayout, tcs, addresses, values, count, writes, 16);
    if (!n) { fuxi_rpmh_release(1); return -EINVAL; }
    const uint32_t irq_enable = rd(&gRsc, FC_RSC_TCS);
    wr(&gRsc, FC_RSC_TCS, irq_enable | bit);
    for (unsigned i = 0; i < n; ++i) wr(&gRsc, FC_RSC_TCS + writes[i].offset, writes[i].value);
    int done = 0;
    for (unsigned t = 0; t < 1000 && !done; ++t) {
        done = (rd(&gRsc, FC_RSC_TCS + gLayout.irq_status) & bit) != 0;
        if (!done) delay(100);
    }
    if (!done) {
        printf("FUXI CAMERA: rpmh vote timed out; shared RSC quarantined\n");
        fuxi_rpmh_release(0);
        return -ETIMEDOUT;
    }
    const unsigned m = qcom_rpmh_finish(&gLayout, tcs, writes, 16);
    for (unsigned i = 0; i < m; ++i) wr(&gRsc, FC_RSC_TCS + writes[i].offset, writes[i].value);
    wr(&gRsc, FC_RSC_TCS + base + gLayout.wait_for_cmpl, 0);
    wr(&gRsc, FC_RSC_TCS, (rd(&gRsc, FC_RSC_TCS) & ~bit) | (irq_enable & bit));
    const int healthy = !rd(&gRsc, FC_RSC_TCS + base + gLayout.control) && !rd(&gRsc, FC_RSC_TCS + base + gLayout.cmd_enable) &&
        !rd(&gRsc, FC_RSC_TCS + base + gLayout.wait_for_cmpl) && !(rd(&gRsc, FC_RSC_TCS + gLayout.irq_status) & bit) &&
        rd(&gRsc, FC_RSC_TCS + base + gLayout.status) && (rd(&gRsc, FC_RSC_TCS) & bit) == (irq_enable & bit);
    fuxi_rpmh_release(healthy);
    if (!healthy) { printf("FUXI CAMERA: rpmh cleanup failed; shared RSC quarantined\n"); return -EIO; }
    return 0;
}
static int voteRail(struct fc_rail_state *rail, int on)
{
    if (!on && (rail->spec.shared_keep_on || fc_rail_must_keep_on(rail->spec.name))) {
        rail->on = 0;
        printf("FUXI CAMERA: rail %s %s shared (keep-on preserved)\n", rail->spec.role, rail->spec.name);
        return 0;
    }
    uint32_t addresses[3], values[3];
    const unsigned count = on ? fc_vrm_on_mode(rail->address, rail->spec.mv, rail->spec.mode, addresses, values) :
        fc_vrm_off(rail->address, addresses, values);
    int rc = -EBUSY;
    for (unsigned tries = 0; tries < 50 && rc == -EBUSY; ++tries) {
        rc = vote(addresses, values, count);
        if (rc == -EBUSY) IOSleep(20);
    }
    printf("FUXI CAMERA: rail %s %s %u mV (mode=%u) %s rc=%d\n", rail->spec.role, rail->spec.name, rail->spec.mv,
        rail->spec.mode, on ? "on" : "off", rc);
    if (!rc) rail->on = on;
    return rc;
}
static int voteBcm(const char *name, uint32_t address, int on)
{
    if (!fc_is_bcm(address)) return -ENOENT;
    const uint32_t value = fc_bcm_vote(on, on ? 0x3fffu : 0u);
    int rc = -EBUSY;
    for (unsigned tries = 0; tries < 50u && rc == -EBUSY; ++tries) {
        rc = vote(&address, &value, 1u);
        if (rc == -EBUSY) IOSleep(20);
    }
    printf("FUXI CAMERA: bcm %s (%#x) %s val=%#x rc=%d\n", name, address, on ? "on" : "off", value, rc);
    return rc;
}

/* ---- 唯讀盤點 ---- */
static void reportPin(unsigned gpio)
{
    if (gpio == FC_GPIO_NONE) return;
    const uint32_t ctl = pinCtl(gpio), io = pinIo(gpio);
    printf("FUXI CAMERA: pin %u ctl %#x io %#x (func %u, %u mA, pull %u, oe %u, egpio present %u enable %u, in %u out %u)\n", gpio,
        ctl, io, fc_ctl_func(ctl), fc_ctl_drive_ma(ctl), fc_ctl_pull(ctl), !!(ctl & FC_CTL_OE), !!(ctl & FC_CTL_EGPIO_PRESENT),
        !!(ctl & FC_CTL_EGPIO_ENABLE), io & FC_IO_IN, !!(io & FC_IO_OUT));
}
static void reportSensorPins(const struct fc_sensor_hw *hw)
{
    reportPin(hw->pin_mclk);
    reportPin(hw->pin_reset);
    if (hw->pin_avdd != FC_GPIO_NONE) reportPin(hw->pin_avdd);
    if (hw->pin_avdd2 != FC_GPIO_NONE) reportPin(hw->pin_avdd2);
    reportPin(hw->pin_sda);
    reportPin(hw->pin_scl);
}
static int camccReadable(void) { return fc_branch_is_on(rd(&gGcc, FC_GCC_CAMERA_AHB)); }
static void reportCamcc(const char *when, const struct fc_sensor_hw *hw)
{
    if (!camccReadable()) {
        printf("FUXI CAMERA: camcc %s: not read (gcc camera ahb %#x is off)\n", when, rd(&gGcc, FC_GCC_CAMERA_AHB));
        return;
    }
    printf("FUXI CAMERA: camcc %s [%s]: titan gdscr %#x cfg %#x, gdsc_clk %#x, sleep_clk %#x, cpas_ahb %#x, cpas_fast_ahb %#x\n",
        when, hw->name, rd(&gCamcc, FC_TITAN_GDSCR), rd(&gCamcc, FC_TITAN_GDSCR + 4), rd(&gCamcc, FC_GDSC_CLK),
        rd(&gCamcc, FC_SLEEP_CLK), rd(&gCamcc, FC_CPAS_AHB), rd(&gCamcc, FC_CPAS_FAST_AHB));
    printf("FUXI CAMERA: camcc %s [%s]: mclk cmd %#x cfg %#x m %#x n %#x d %#x cbcr %#x; cci cmd %#x cfg %#x cbcr %#x\n",
        when, hw->name, rd(&gCamcc, hw->mclk_rcg), rd(&gCamcc, hw->mclk_rcg + 4), rd(&gCamcc, hw->mclk_rcg + 8),
        rd(&gCamcc, hw->mclk_rcg + 0xc), rd(&gCamcc, hw->mclk_rcg + 0x10), rd(&gCamcc, hw->mclk_cbcr),
        rd(&gCamcc, hw->cci_rcg), rd(&gCamcc, hw->cci_rcg + 4), rd(&gCamcc, hw->cci_cbcr));
}
static void survey(const struct fc_sensor_hw *hw)
{
    reportSensorPins(hw);
    printf("FUXI CAMERA: gcc camera ahb %#x xo %#x hf_axi %#x sf_axi %#x\n", rd(&gGcc, FC_GCC_CAMERA_AHB), rd(&gGcc, FC_GCC_CAMERA_XO),
        rd(&gGcc, FC_GCC_CAMERA_HF_AXI), rd(&gGcc, FC_GCC_CAMERA_SF_AXI));
    reportCamcc("found", hw);
}

/* ---- 上電／斷電：記錄自己開了什麼，斷電只關那些且永不關共用電源軌 ---- */
static struct {
    int reset_taken, avdd_taken, avdd2_taken, ahb_switched, xo_switched, gdsc_clk_switched, gdsc_switched, mclk_switched, mclk_pin_taken;
    uint16_t reset_pin, avdd_pin, avdd2_pin, mclk_pin;
    uint32_t mclk_rcg, mclk_cbcr;
    uint32_t gdscr, mclk_cfg, mclk_cbcr_saved;
} gUp;

static int branchOn(const struct fc_window *w, uint32_t at, const char *name, int *switched)
{
    const uint32_t found = rd(w, at);
    if (fc_branch_is_on(found)) return 1;
    wr(w, at, found | FC_CBCR_ENABLE);
    *switched = !(found & FC_CBCR_ENABLE);
    /* hwcg：照 Linux clk_branch_wait 不等 CLK_OFF。 */
    const int ok = (found & FC_CBCR_HW_CTL) ? fc_branch_is_on(rd(w, at)) : waitBits(w, at, FC_CBCR_OFF, 0, 2000);
    printf("FUXI CAMERA: %s was off (%#x); enabled %s (%#x%s)\n", name, found, ok ? "ok" : "BUT STILL OFF", rd(w, at),
        (found & FC_CBCR_HW_CTL) ? ", hw-gated" : "");
    return ok;
}
static void branchOff(const struct fc_window *w, uint32_t at, int *switched)
{
    if (!*switched) return;
    const uint32_t now = rd(w, at);
    wr(w, at, now & ~FC_CBCR_ENABLE);
    if (!(now & FC_CBCR_HW_CTL)) (void)waitBits(w, at, FC_CBCR_OFF, FC_CBCR_OFF, 2000);
    *switched = 0;
}
static void hexLine(char *text, size_t size, const uint8_t *bytes, unsigned count);

/* 顯式 DC CIVAC + DSB SY（比照 fuxi_wlan.c，依 CTR_EL0.DminLine 計算快取行大小）。 */
static void dcacheCivac(vm_offset_t va, vm_size_t bytes)
{
    uint64_t ctr = 0;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    const vm_size_t line = (vm_size_t)4u << ((ctr >> 16) & 0xfu);
    for (vm_size_t off = 0; off < bytes; off += line) {
        const vm_offset_t addr = va + off;
        __asm__ volatile("dc civac, %0" :: "r"(addr) : "memory");
    }
    __asm__ volatile("dsb sy" ::: "memory");
}

struct fc_smmu_ctx {
    struct fc_window win;
    int attached;
    unsigned slot;
    unsigned stride;
    unsigned bank_base;
    uint32_t old_smr;
    uint32_t old_s2cr;
};

/* C2b SMMU 掛載：驗證 apps_smmu 與 ABL/USB 共用 CB4 bypass，從最高空槽掛 msm_cam_smmu_ife（0x800/0x20）。 */
static int smmuAttachIfe(struct fc_smmu_ctx *ctx)
{
    bzero(ctx, sizeof(*ctx));
    ctx->win.pa = FC_SMMU_PA;
    ctx->win.size = FC_SMMU_SIZE;
    if (fc_map(&ctx->win) != KERN_SUCCESS) {
        printf("FUXI CAMERA: c2b smmu map failed\n");
        return 0;
    }
    const uint32_t scr0 = rd(&ctx->win, 0x0u);
    const uint32_t idr0 = rd(&ctx->win, 0x20u);
    const uint32_t idr1 = rd(&ctx->win, 0x24u);
    const uint32_t idr2 = rd(&ctx->win, 0x28u);
    const uint32_t gfsr = rd(&ctx->win, 0x48u);
    const unsigned stride = (idr1 & (1u << 31)) ? 0x10000u : 0x1000u;
    const unsigned bank_base = (1u << (((idr1 >> 28) & 7u) + 1u)) * stride;
    const unsigned slots = idr0 & 0xffu;
    const unsigned banks = idr1 & 0xffu;
    if ((scr0 & 9u) || (idr0 & 0x48000000u) != 0x48000000u || !(idr2 & 0x1000u) || !slots || slots > 128u ||
        banks <= FC_SMMU_BYPASS_BANK || banks > 128u || bank_base > FC_SMMU_SIZE ||
        banks > (FC_SMMU_SIZE - bank_base) / stride) {
        printf("FUXI CAMERA: c2b smmu unsupported scr0=%#x idr=%#x/%#x/%#x\n", scr0, idr0, idr1, idr2);
        fc_unmap(&ctx->win);
        return 0;
    }
    uint32_t smrs[128], s2crs[128];
    int usb_cb4 = 0;
    for (unsigned idx = 0; idx < slots; ++idx) {
        smrs[idx] = rd(&ctx->win, 0x800u + 4u * idx);
        s2crs[idx] = rd(&ctx->win, 0xc00u + 4u * idx);
        if (fc_smmu_overlap(smrs[idx], 0x40u, 0u) && s2crs[idx] == FC_SMMU_BYPASS_BANK) usb_cb4++;
    }
    const uint32_t cb4_sctlr = rd(&ctx->win, bank_base + stride * FC_SMMU_BYPASS_BANK);
    const uint32_t cb4_cbar = rd(&ctx->win, stride + 4u * FC_SMMU_BYPASS_BANK);
    const uint32_t cb4_fsr = rd(&ctx->win, bank_base + stride * FC_SMMU_BYPASS_BANK + 0x58u);
    if (usb_cb4 != 1 || (cb4_sctlr & 1u) != 0u || ((cb4_cbar >> 16) & 3u) != 1u) {
        printf("FUXI CAMERA: c2b smmu cb4 unsafe usb=%d sctlr=%#x cbar=%#x\n", usb_cb4, cb4_sctlr, cb4_cbar);
        fc_unmap(&ctx->win);
        return 0;
    }
    const int picked = fc_smmu_pick_high_slot(smrs, s2crs, slots, FC_SMMU_IFE_SID, FC_SMMU_IFE_MASK);
    if (picked < 0) {
        printf("FUXI CAMERA: c2b smmu slot unavailable rc=%d (slots=%u)\n", picked, slots);
        fc_unmap(&ctx->win);
        return 0;
    }
    const unsigned slot = (unsigned)picked;
    const uint32_t smr_off = 0x800u + 4u * slot;
    const uint32_t s2cr_off = 0xc00u + 4u * slot;
    ctx->slot = slot;
    ctx->stride = stride;
    ctx->bank_base = bank_base;
    ctx->old_smr = smrs[slot];
    ctx->old_s2cr = s2crs[slot];
    wr(&ctx->win, s2cr_off, FC_SMMU_BYPASS_BANK);
    wr(&ctx->win, smr_off, FC_SMMU_IFE_SMR);
    const uint32_t rb_s2cr = rd(&ctx->win, s2cr_off);
    const uint32_t rb_smr = rd(&ctx->win, smr_off);
    if (rb_s2cr != FC_SMMU_BYPASS_BANK || rb_smr != FC_SMMU_IFE_SMR) {
        wr(&ctx->win, smr_off, ctx->old_smr);
        wr(&ctx->win, s2cr_off, ctx->old_s2cr);
        printf("FUXI CAMERA: c2b smmu verify failed slot=%u smr=%#x s2cr=%#x\n", slot, rb_smr, rb_s2cr);
        fc_unmap(&ctx->win);
        return 0;
    }
    ctx->attached = 1;
    printf("FUXI CAMERA: c2b smmu attached slot=%u/%u smr=%#x s2cr=%#x (gfsr=%#x cb4_fsr=%#x)\n",
        slot, slots, rb_smr, rb_s2cr, gfsr, cb4_fsr);
    return 1;
}

static void smmuDetachIfe(struct fc_smmu_ctx *ctx)
{
    if (!ctx->win.va) return;
    const uint32_t gfsr = rd(&ctx->win, 0x48u);
    const uint32_t gfsynr1 = rd(&ctx->win, 0x54u);
    const uint32_t cb4 = ctx->bank_base + ctx->stride * FC_SMMU_BYPASS_BANK;
    const uint32_t cb4_fsr = rd(&ctx->win, cb4 + 0x58u);
    const uint32_t cb4_far_lo = rd(&ctx->win, cb4 + 0x60u);
    const uint32_t cb4_far_hi = rd(&ctx->win, cb4 + 0x64u);
    const uint32_t cb4_fsynr0 = rd(&ctx->win, cb4 + 0x68u);
    uint32_t rb_smr = 0u, rb_s2cr = 0u;
    if (ctx->attached) {
        const uint32_t smr_off = 0x800u + 4u * ctx->slot;
        const uint32_t s2cr_off = 0xc00u + 4u * ctx->slot;
        wr(&ctx->win, smr_off, ctx->old_smr);
        wr(&ctx->win, s2cr_off, ctx->old_s2cr);
        rb_smr = rd(&ctx->win, smr_off);
        rb_s2cr = rd(&ctx->win, s2cr_off);
        ctx->attached = 0;
    }
    printf("FUXI CAMERA: c2b smmu detached slot=%u (smr=%#x s2cr=%#x) faults gfsr=%#x gfsynr1=%#x cb4 fsr=%#x far=%#llx fsynr0=%#x\n",
        ctx->slot, rb_smr, rb_s2cr, gfsr, gfsynr1, cb4_fsr,
        ((uint64_t)cb4_far_hi << 32) | (uint64_t)cb4_far_lo, cb4_fsynr0);
    fc_unmap(&ctx->win);
}

static void csidClearAllIrqs(const struct fc_window *wLite)
{
    const uint32_t base = FC_CSID_LITE0_OFF;
    wr(wLite, base + FC_CSID_TOP_IRQ_CLEAR, 0xffffffffu);
    wr(wLite, base + FC_CSID_BUF_DONE_IRQ_CLEAR, 0xffffffffu);
    wr(wLite, base + FC_CSID_RX_IRQ_CLEAR, 0xffffffffu);
    wr(wLite, base + FC_CSID_RDI0_IRQ_CLEAR, 0xffffffffu);
    wr(wLite, base + FC_CSID_IRQ_CMD, 1u);
}

/* C2b：TPG0（256x128 RAW8 色條）→ CSID-lite0 → VFE-lite0 RDI0 WM0 → apps_smmu CB4 → DDR 首幀收幀測試。 */
static void camssTpgCapture(const struct fc_window *wLite)
{
    struct fc_window wTpg = {FC_TPG0_PAGE, 0x4000u, 0};
    struct fc_smmu_ctx smmu;
    bzero(&smmu, sizeof(smmu));
    vm_offset_t dma_va = 0;
    if (fc_map(&wTpg) != KERN_SUCCESS) {
        printf("FUXI CAMERA: c2b TPG0 map failed\n");
        return;
    }
    const uint32_t tpg_ver = rd(&wTpg, FC_TPG0_OFF + FC_TPG_HW_VERSION);
    const uint32_t tpg_st0 = rd(&wTpg, FC_TPG0_OFF + FC_TPG_HW_STATUS);
    printf("FUXI CAMERA: c2b TPG0 hw_version=%#x (%u.%u.%u) status=%#x\n", tpg_ver,
        fc_camss_hw_gen(tpg_ver), fc_camss_hw_rev(tpg_ver), fc_camss_hw_step(tpg_ver), tpg_st0);
    if (!tpg_ver || tpg_ver == 0xffffffffu) {
        printf("FUXI CAMERA: c2b TPG0 version invalid; skipping capture\n");
        fc_unmap(&wTpg);
        return;
    }
    if (!smmuAttachIfe(&smmu)) {
        fc_unmap(&wTpg);
        return;
    }
    if (kmem_alloc_contig(kernel_map, &dma_va, FC_TPG_FRAME_BYTES, PAGE_MASK, 0, 0, KMA_DATA | KMA_ZERO,
            VM_KERN_MEMORY_IOKIT) != KERN_SUCCESS) {
        printf("FUXI CAMERA: c2b DMA alloc %u B failed\n", FC_TPG_FRAME_BYTES);
        smmuDetachIfe(&smmu);
        fc_unmap(&wTpg);
        return;
    }
    const ppnum_t pn0 = pmap_find_phys(kernel_pmap, (addr64_t)dma_va);
    const ppnum_t pn1 = pmap_find_phys(kernel_pmap, (addr64_t)(dma_va + PAGE_SIZE));
    const uint64_t pa0 = ptoa_64(pn0);
    const uint64_t pa1 = ptoa_64(pn1);
    if (!pn0 || !pn1 || pa1 != pa0 + PAGE_SIZE || !fc_vfe_wm_addr_valid(pa0)) {
        printf("FUXI CAMERA: c2b DMA phys invalid pa0=%#llx pa1=%#llx\n",
            (unsigned long long)pa0, (unsigned long long)pa1);
        kmem_free(kernel_map, dma_va, FC_TPG_FRAME_BYTES);
        smmuDetachIfe(&smmu);
        fc_unmap(&wTpg);
        return;
    }
    memset((void *)dma_va, FC_TPG_CANARY, FC_TPG_FRAME_BYTES);
    dcacheCivac(dma_va, FC_TPG_FRAME_BYTES);
    printf("FUXI CAMERA: c2b DMA buf pa=%#llx wm_addr=%#x bytes=%u (canary=%#x)\n",
        (unsigned long long)pa0, fc_vfe_wm_addr(pa0), FC_TPG_FRAME_BYTES, FC_TPG_CANARY);

    const uint32_t csid = FC_CSID_LITE0_OFF;
    const uint32_t vfe = FC_VFE_LITE0_OFF;
    csidClearAllIrqs(wLite);
    wr(wLite, csid + FC_CSID_TOP_IRQ_MASK, FC_CSID_TOP_RESET_DONE);
    wr(wLite, csid + FC_CSID_BUF_DONE_IRQ_MASK, 0x1fu);
    wr(wLite, csid + FC_CSID_RX_IRQ_MASK, 0xffffffffu);
    wr(wLite, csid + FC_CSID_RDI0_IRQ_MASK, 0xffffffffu);
    wr(wLite, csid + FC_CSID_RST_CFG, 0x11u);
    wr(wLite, csid + FC_CSID_RST_CMD, 0x5u);
    const int reset_ok = waitBits(wLite, csid + FC_CSID_TOP_IRQ_STATUS, FC_CSID_TOP_RESET_DONE, FC_CSID_TOP_RESET_DONE, 50000u);
    const uint32_t top_after_rst = rd(wLite, csid + FC_CSID_TOP_IRQ_STATUS);
    csidClearAllIrqs(wLite);
    printf("FUXI CAMERA: c2b CSID-lite0 reset %s (top_irq=%#x)\n", reset_ok ? "ok" : "TIMEOUT", top_after_rst);

    wr(wLite, vfe + FC_VFE_BUS_CGC_OVERRIDE, 0x07ffffffu);
    wr(wLite, vfe + FC_VFE_BUS_TEST_BUS_CTRL, 0u);
    wr(wLite, vfe + FC_VFE_WM0_FRAME_INCR, fc_vfe_wm_frame_incr(FC_TPG_WIDTH, FC_TPG_HEIGHT));
    wr(wLite, vfe + FC_VFE_WM0_IMAGE_CFG0, 0xffffu);
    wr(wLite, vfe + FC_VFE_WM0_IMAGE_CFG2, 0xffffu);
    wr(wLite, vfe + FC_VFE_WM0_PACKER_CFG, 0u);
    wr(wLite, vfe + FC_VFE_WM0_FDROP_PERIOD, 0u);
    wr(wLite, vfe + FC_VFE_WM0_FDROP_PATTERN, 1u);
    wr(wLite, vfe + FC_VFE_WM0_IRQ_PERIOD, 0u);
    wr(wLite, vfe + FC_VFE_WM0_IRQ_PATTERN, 1u);
    wr(wLite, vfe + FC_VFE_WM0_MMU_PREFETCH_CFG, 1u);
    wr(wLite, vfe + FC_VFE_WM0_MMU_PREFETCH_MAX, 0xffffffffu);
    wr(wLite, vfe + FC_VFE_WM0_CFG, 0x10001u);
    wr(wLite, vfe + FC_VFE_WM0_IMAGE_ADDR, fc_vfe_wm_addr(pa0));
    wr(wLite, csid + FC_CSID_RUP_AUP_CMD, FC_CSID_RUP_AUP_RDI0);

    const uint32_t rdi_cfg0 = fc_csid_rdi_cfg0(FC_MIPI_DT_RAW8, 0u, 0u, 0);
    wr(wLite, csid + FC_CSID_RDI0_CFG0, rdi_cfg0);
    wr(wLite, csid + FC_CSID_RDI0_CFG1, fc_csid_rdi_cfg1());
    wr(wLite, csid + FC_CSID_RDI0_SUBSAMPLE_PERIOD, 0u);
    wr(wLite, csid + FC_CSID_RDI0_SUBSAMPLE_PATTERN, 1u);
    wr(wLite, csid + FC_CSID_RDI0_CTRL, 0u);
    wr(wLite, csid + FC_CSID_RDI0_CFG0, rdi_cfg0 | (1u << 31));
    wr(wLite, csid + FC_CSID_RX_CFG0, fc_csid_rx_cfg0_tpg(4u, 0u));
    wr(wLite, csid + FC_CSID_RX_CFG1, 1u);
    wr(wLite, csid + FC_CSID_RDI0_CTRL, 1u);
    wr(wLite, csid + FC_CSID_RUP_AUP_CMD, FC_CSID_RUP_AUP_RDI0);

    wr(&wTpg, FC_TPG0_OFF + FC_TPG_CTRL, 0u);
    wr(&wTpg, FC_TPG0_OFF + FC_TPG_CLEAR, 1u);
    wr(&wTpg, FC_TPG0_OFF + FC_TPG_VC0_CFG0, 0u);
    wr(&wTpg, FC_TPG0_OFF + FC_TPG_VC0_LFSR, FC_TPG_LFSR_SEED);
    wr(&wTpg, FC_TPG0_OFF + FC_TPG_VC0_HBI, fc_tpg_hbi(FC_TPG_WIDTH, 8u, 4u));
    wr(&wTpg, FC_TPG0_OFF + FC_TPG_VC0_VBI, fc_tpg_vbi(FC_TPG_HEIGHT));
    wr(&wTpg, FC_TPG0_OFF + FC_TPG_VC0_CBARS, FC_TPG_CBARS_ROTATE_STD);
    wr(&wTpg, FC_TPG0_OFF + FC_TPG_DT0_CFG0, fc_tpg_dt_cfg0(FC_TPG_WIDTH, FC_TPG_HEIGHT));
    wr(&wTpg, FC_TPG0_OFF + FC_TPG_DT0_CFG1, FC_MIPI_DT_RAW8);
    wr(&wTpg, FC_TPG0_OFF + FC_TPG_DT0_CFG2,
        fc_tpg_dt_cfg2(tpg_ver, FC_TPG_MODE_COLOR_BARS, FC_TPG_ENC_RAW8, FC_TPG_USER_PAYLOAD));
    wr(&wTpg, FC_TPG0_OFF + FC_TPG_CTRL, fc_tpg_ctrl(1, 4u));

    uint32_t seen_buf = 0u, seen_rdi0 = 0u, seen_rx = 0u, tpg_st = 0u;
    unsigned buf_ms = 0u, rup_ms = 0u;
    for (unsigned ms = 1u; ms <= 200u; ++ms) {
        IOSleep(1);
        const uint32_t bd = rd(wLite, csid + FC_CSID_BUF_DONE_IRQ_STATUS);
        const uint32_t r0 = rd(wLite, csid + FC_CSID_RDI0_IRQ_STATUS);
        const uint32_t rx = rd(wLite, csid + FC_CSID_RX_IRQ_STATUS);
        tpg_st = rd(&wTpg, FC_TPG0_OFF + FC_TPG_HW_STATUS);
        if (!rup_ms && (r0 & FC_CSID_RDI_RUP_DONE)) rup_ms = ms;
        if (!buf_ms && (bd & FC_CSID_BUF_DONE_RDI0)) buf_ms = ms;
        seen_buf |= bd;
        seen_rdi0 |= r0;
        seen_rx |= rx;
        if (seen_buf & FC_CSID_BUF_DONE_RDI0) break;
    }

    wr(&wTpg, FC_TPG0_OFF + FC_TPG_CTRL, 0u);
    wr(&wTpg, FC_TPG0_OFF + FC_TPG_CLEAR, 1u);
    wr(wLite, csid + FC_CSID_RDI0_CTRL, 0u);
    wr(wLite, csid + FC_CSID_RDI0_CFG0, 0u);
    wr(wLite, vfe + FC_VFE_WM0_CFG, 0u);
    IOSleep(2);
    wr(wLite, csid + FC_CSID_RST_CFG, 0x11u);
    wr(wLite, csid + FC_CSID_RST_CMD, 0x5u);
    (void)waitBits(wLite, csid + FC_CSID_TOP_IRQ_STATUS, FC_CSID_TOP_RESET_DONE, FC_CSID_TOP_RESET_DONE, 20000u);
    wr(wLite, csid + FC_CSID_TOP_IRQ_MASK, 0u);
    wr(wLite, csid + FC_CSID_BUF_DONE_IRQ_MASK, 0u);
    wr(wLite, csid + FC_CSID_RX_IRQ_MASK, 0u);
    wr(wLite, csid + FC_CSID_RDI0_IRQ_MASK, 0u);
    csidClearAllIrqs(wLite);

    smmuDetachIfe(&smmu);
    fc_unmap(&wTpg);

    dcacheCivac(dma_va, FC_TPG_FRAME_BYTES);
    struct fc_frame_stats stats;
    fc_frame_analyze((const uint8_t *)dma_va, FC_TPG_FRAME_BYTES, FC_TPG_CANARY, &stats);
    char row0_hex[56], row64_hex[56];
    hexLine(row0_hex, sizeof(row0_hex), (const uint8_t *)dma_va, 16u);
    hexLine(row64_hex, sizeof(row64_hex), (const uint8_t *)(dma_va + 64u * FC_TPG_WIDTH), 16u);
    printf("FUXI CAMERA: c2b TPG0 capture buf_done=%#x(%u ms) rdi0_irq=%#x(rup=%u ms) rx_irq=%#x tpg_st=%#x\n",
        seen_buf, buf_ms, seen_rdi0, rup_ms, seen_rx, tpg_st);
    printf("FUXI CAMERA: c2b frame non_canary=%u/%u min=%u max=%u trans=%u fnv1a=%#010x row0=[%s] row64=[%s]\n",
        stats.non_canary, FC_TPG_FRAME_BYTES, stats.min_val, stats.max_val, stats.transitions, stats.fnv1a, row0_hex, row64_hex);
    kmem_free(kernel_map, dma_va, FC_TPG_FRAME_BYTES);
}

/* SM8550 CSIPHY Gen2 2.1.2 4-lane D-PHY 暫存器序（對照 Linux camss-csiphy-3ph-1-0.c lane_regs_sm8550，100 筆）。 */
struct fc_csiphy_reg { uint16_t off; uint8_t val, delay_us, is_settle; };
static const struct fc_csiphy_reg kSm8550LaneRegs[] = {
    {0x0e90u, 0x0fu, 0u, 0u}, {0x0e98u, 0x08u, 0u, 0u}, {0x0e94u, 0x07u, 1u, 0u},
    {0x00a0u, 0x00u, 0u, 0u}, {0x0090u, 0x0fu, 0u, 0u}, {0x0098u, 0x08u, 0u, 0u}, {0x0094u, 0x07u, 1u, 0u},
    {0x0494u, 0x00u, 0u, 0u}, {0x04a0u, 0x00u, 0u, 0u}, {0x0490u, 0x0fu, 0u, 0u}, {0x0498u, 0x08u, 0u, 0u}, {0x0494u, 0x07u, 1u, 0u},
    {0x0894u, 0x00u, 0u, 0u}, {0x08a0u, 0x00u, 0u, 0u}, {0x0890u, 0x0fu, 0u, 0u}, {0x0898u, 0x08u, 0u, 0u}, {0x0894u, 0x07u, 1u, 0u},
    {0x0c94u, 0x00u, 0u, 0u}, {0x0ca0u, 0x00u, 0u, 0u}, {0x0c90u, 0x0fu, 0u, 0u}, {0x0c98u, 0x08u, 0u, 0u}, {0x0c94u, 0x07u, 1u, 0u},
    {0x0e30u, 0x00u, 0u, 0u}, {0x0e28u, 0x04u, 0u, 0u}, {0x0e00u, 0x80u, 0u, 0u}, {0x0e0cu, 0xffu, 0u, 0u},
    {0x0e38u, 0x1fu, 0u, 0u}, {0x0e2cu, 0x01u, 0u, 0u}, {0x0e34u, 0x0fu, 0u, 0u}, {0x0e1cu, 0x0au, 0u, 0u},
    {0x0e14u, 0x60u, 0u, 0u}, {0x0e3cu, 0xb8u, 0u, 0u}, {0x0e04u, 0x0cu, 0u, 0u}, {0x0e20u, 0x00u, 0u, 0u},
    {0x0e08u, 0x10u, 0u, 1u}, {0x0e10u, 0x52u, 0u, 0u},
    {0x0030u, 0x00u, 0u, 0u}, {0x0000u, 0x8eu, 0u, 0u}, {0x0038u, 0xfeu, 0u, 0u}, {0x002cu, 0x01u, 0u, 0u},
    {0x0034u, 0x0fu, 0u, 0u}, {0x001cu, 0x0au, 0u, 0u}, {0x0014u, 0x60u, 0u, 0u}, {0x003cu, 0xb8u, 0u, 0u},
    {0x0004u, 0x0cu, 0u, 0u}, {0x0020u, 0x00u, 0u, 0u}, {0x0008u, 0x10u, 0u, 1u}, {0x0010u, 0x52u, 0u, 0u},
    {0x0430u, 0x00u, 0u, 0u}, {0x0400u, 0x8eu, 0u, 0u}, {0x0438u, 0xfeu, 0u, 0u}, {0x042cu, 0x01u, 0u, 0u},
    {0x0434u, 0x0fu, 0u, 0u}, {0x041cu, 0x0au, 0u, 0u}, {0x0414u, 0x60u, 0u, 0u}, {0x043cu, 0xb8u, 0u, 0u},
    {0x0404u, 0x0cu, 0u, 0u}, {0x0420u, 0x00u, 0u, 0u}, {0x0408u, 0x10u, 0u, 1u}, {0x0410u, 0x52u, 0u, 0u},
    {0x0830u, 0x00u, 0u, 0u}, {0x0800u, 0x8eu, 0u, 0u}, {0x0838u, 0xfeu, 0u, 0u}, {0x082cu, 0x01u, 0u, 0u},
    {0x0834u, 0x0fu, 0u, 0u}, {0x081cu, 0x0au, 0u, 0u}, {0x0814u, 0x60u, 0u, 0u}, {0x083cu, 0xb8u, 0u, 0u},
    {0x0804u, 0x0cu, 0u, 0u}, {0x0820u, 0x00u, 0u, 0u}, {0x0808u, 0x10u, 0u, 1u}, {0x0810u, 0x52u, 0u, 0u},
    {0x0c30u, 0x00u, 0u, 0u}, {0x0c00u, 0x8eu, 0u, 0u}, {0x0c38u, 0xfeu, 0u, 0u}, {0x0c2cu, 0x01u, 0u, 0u},
    {0x0c34u, 0x0fu, 0u, 0u}, {0x0c1cu, 0x0au, 0u, 0u}, {0x0c14u, 0x60u, 0u, 0u}, {0x0c3cu, 0xb8u, 0u, 0u},
    {0x0c04u, 0x0cu, 0u, 0u}, {0x0c20u, 0x00u, 0u, 0u}, {0x0c08u, 0x10u, 0u, 1u}, {0x0c10u, 0x52u, 0u, 0u},
    {0x0094u, 0xd7u, 0u, 0u}, {0x005cu, 0x04u, 0u, 0u}, {0x0060u, 0xbdu, 0u, 0u}, {0x0064u, 0x7fu, 0u, 0u},
    {0x0494u, 0xd7u, 0u, 0u}, {0x045cu, 0x04u, 0u, 0u}, {0x0460u, 0xbdu, 0u, 0u}, {0x0464u, 0x7fu, 0u, 0u},
    {0x0894u, 0xd7u, 0u, 0u}, {0x085cu, 0x04u, 0u, 0u}, {0x0860u, 0xbdu, 0u, 0u}, {0x0864u, 0x7fu, 0u, 0u},
    {0x0c94u, 0xd7u, 0u, 0u}, {0x0c5cu, 0x04u, 0u, 0u}, {0x0c60u, 0xbdu, 0u, 0u}, {0x0c64u, 0x7fu, 0u, 0u},
};

/* SM8550 CSIPHY Gen2 2.1.2 3-trio C-PHY（對照 Android camera.ko v2.1.2 實機反組譯）。 */
static const struct fc_csiphy_reg kSm8550CphyCommonRegs[] = {
    {0x1084u, 0x00u, 0u, 0u}, {0x108cu, 0x00u, 1u, 0u}, {0x101cu, 0x7au, 0u, 0u}, {0x1018u, 0x01u, 0u, 0u},
};
static const struct fc_csiphy_reg kSm8550Cphy3p0Regs[] = {
    {0x0268u, 0xf1u, 0u, 0u}, {0x0294u, 0x01u, 0u, 0u}, {0x0288u, 0x20u, 0u, 0u}, {0x0278u, 0x17u, 0u, 0u},
    {0x026cu, 0x3du, 0u, 0u}, {0x028cu, 0x30u, 0u, 0u}, {0x0270u, 0x01u, 0u, 0u}, {0x0274u, 0x00u, 0u, 0u},
    {0x0668u, 0xf1u, 0u, 0u}, {0x0694u, 0x01u, 0u, 0u}, {0x0688u, 0x20u, 0u, 0u}, {0x0678u, 0x17u, 0u, 0u},
    {0x066cu, 0x3du, 0u, 0u}, {0x068cu, 0x30u, 0u, 0u}, {0x0670u, 0x01u, 0u, 0u}, {0x0674u, 0x00u, 0u, 0u},
    {0x0a68u, 0xf1u, 0u, 0u}, {0x0a94u, 0x01u, 0u, 0u}, {0x0a88u, 0x20u, 0u, 0u}, {0x0a78u, 0x17u, 0u, 0u},
    {0x0a6cu, 0x3du, 0u, 0u}, {0x0a8cu, 0x30u, 0u, 0u}, {0x0a70u, 0x01u, 0u, 0u}, {0x0a74u, 0x00u, 10u, 0u},
    {0x020cu, 0x20u, 0u, 1u}, {0x0208u, 0x00u, 0u, 2u}, {0x0210u, 0x00u, 0u, 0u}, {0x0214u, 0x00u, 0u, 0u},
    {0x060cu, 0x20u, 0u, 1u}, {0x0608u, 0x00u, 0u, 2u}, {0x0610u, 0x00u, 0u, 0u}, {0x0614u, 0x00u, 0u, 0u},
    {0x0a0cu, 0x20u, 0u, 1u}, {0x0a08u, 0x00u, 0u, 2u}, {0x0a10u, 0x00u, 0u, 0u}, {0x0a14u, 0x00u, 0u, 0u},
};
static const struct fc_csiphy_reg kSm8550Cphy3phRegs[] = {
    {0x02f4u, 0x00u, 0u, 0u}, {0x02f8u, 0x00u, 0u, 0u}, {0x02fcu, 0x00u, 0u, 0u}, {0x02f0u, 0xefu, 3u, 0u},
    {0x06f4u, 0x00u, 0u, 0u}, {0x06f8u, 0x00u, 0u, 0u}, {0x06fcu, 0x00u, 0u, 0u}, {0x06f0u, 0xefu, 3u, 0u},
    {0x0af4u, 0x00u, 0u, 0u}, {0x0af8u, 0x00u, 0u, 0u}, {0x0afcu, 0x00u, 0u, 0u}, {0x0af0u, 0xefu, 3u, 0u},
    {0x0204u, 0x00u, 0u, 0u}, {0x02e4u, 0x00u, 0u, 0u}, {0x02e8u, 0x7fu, 0u, 0u}, {0x02ecu, 0x7fu, 0u, 0u},
    {0x0218u, 0x3eu, 0u, 0u}, {0x021cu, 0x41u, 0u, 0u}, {0x0220u, 0x41u, 0u, 0u}, {0x0224u, 0x7fu, 0u, 0u},
    {0x0228u, 0x00u, 0u, 0u}, {0x022cu, 0x00u, 0u, 0u}, {0x0264u, 0x01u, 0u, 0u}, {0x0244u, 0xb2u, 0u, 0u},
    {0x0310u, 0x35u, 0u, 0u}, {0x02bcu, 0xd0u, 0u, 0u}, {0x0254u, 0x00u, 0u, 0u}, {0x0240u, 0x00u, 0u, 0u},
    {0x0260u, 0xa8u, 0u, 0u}, {0x0284u, 0x00u, 0u, 0u}, {0x0290u, 0x02u, 0u, 0u},
    {0x0604u, 0x00u, 0u, 0u}, {0x06e4u, 0x00u, 0u, 0u}, {0x06e8u, 0x7fu, 0u, 0u}, {0x06ecu, 0x7fu, 0u, 0u},
    {0x0618u, 0x3eu, 0u, 0u}, {0x061cu, 0x41u, 0u, 0u}, {0x0620u, 0x41u, 0u, 0u}, {0x0624u, 0x7fu, 0u, 0u},
    {0x0628u, 0x00u, 0u, 0u}, {0x062cu, 0x00u, 0u, 0u}, {0x0664u, 0x01u, 0u, 0u}, {0x0644u, 0xb2u, 0u, 0u},
    {0x0710u, 0x35u, 0u, 0u}, {0x06bcu, 0xd0u, 0u, 0u}, {0x0654u, 0x00u, 0u, 0u}, {0x0640u, 0x00u, 0u, 0u},
    {0x0660u, 0xa8u, 0u, 0u}, {0x0684u, 0x00u, 0u, 0u}, {0x0690u, 0x02u, 0u, 0u},
    {0x0a04u, 0x00u, 0u, 0u}, {0x0ae4u, 0x00u, 0u, 0u}, {0x0ae8u, 0x7fu, 0u, 0u}, {0x0aecu, 0x7fu, 0u, 0u},
    {0x0a18u, 0x3eu, 0u, 0u}, {0x0a1cu, 0x41u, 0u, 0u}, {0x0a20u, 0x41u, 0u, 0u}, {0x0a24u, 0x7fu, 0u, 0u},
    {0x0a28u, 0x00u, 0u, 0u}, {0x0a2cu, 0x00u, 0u, 0u}, {0x0a64u, 0x01u, 0u, 0u}, {0x0a44u, 0xb2u, 0u, 0u},
    {0x0b10u, 0x35u, 0u, 0u}, {0x0abcu, 0xd0u, 0u, 0u}, {0x0a54u, 0x00u, 0u, 0u}, {0x0a40u, 0x00u, 0u, 0u},
    {0x0a60u, 0xa8u, 0u, 0u}, {0x0a84u, 0x00u, 0u, 0u}, {0x0a90u, 0x02u, 0u, 0u},
};
static const struct fc_csiphy_reg kSm8550CphyIrqRegs[] = {
    {0x102cu, 0xffu, 0u, 0u}, {0x1030u, 0xffu, 0u, 0u}, {0x1034u, 0xfbu, 0u, 0u}, {0x1038u, 0xffu, 0u, 0u},
    {0x103cu, 0x7fu, 0u, 0u}, {0x1040u, 0xffu, 0u, 0u}, {0x1044u, 0xffu, 0u, 0u}, {0x1048u, 0xefu, 0u, 0u},
    {0x104cu, 0xffu, 0u, 0u}, {0x1050u, 0xffu, 0u, 0u}, {0x1054u, 0xffu, 100u, 0u},
};

static void programCsiphyRegs(const struct fc_window *wPhy, uint32_t po, const struct fc_csiphy_reg *regs,
    unsigned count, uint8_t settle)
{
    for (unsigned idx = 0u; idx < count; ++idx) {
        uint32_t value = (uint32_t)regs[idx].val;
        if (regs[idx].is_settle == 1u) value = settle ? (uint32_t)(settle & 0xffu) : value;
        else if (regs[idx].is_settle == 2u) value = settle ? (uint32_t)((settle >> 8) & 0xffu) : value;
        wr(wPhy, po + regs[idx].off, value);
        if (regs[idx].delay_us) delay((int)regs[idx].delay_us);
    }
}

static unsigned programCsiphy(const struct fc_window *wPhy, uint32_t po, const struct fc_tab_header *hdr, int is_cphy)
{
    wr(wPhy, po + FC_CSIPHY_CTRL(0), 0x1u);
    IOSleep(6);
    wr(wPhy, po + FC_CSIPHY_CTRL(0), 0x0u);
    if (is_cphy) {
        wr(wPhy, po + FC_CSIPHY_CTRL(5), fc_csiphy_ctrl5_cphy(hdr->lanes));
        programCsiphyRegs(wPhy, po, kSm8550CphyCommonRegs, (unsigned)(sizeof(kSm8550CphyCommonRegs) / sizeof(kSm8550CphyCommonRegs[0])), hdr->settle);
        programCsiphyRegs(wPhy, po, kSm8550Cphy3p0Regs, (unsigned)(sizeof(kSm8550Cphy3p0Regs) / sizeof(kSm8550Cphy3p0Regs[0])), hdr->settle);
        programCsiphyRegs(wPhy, po, kSm8550Cphy3phRegs, (unsigned)(sizeof(kSm8550Cphy3phRegs) / sizeof(kSm8550Cphy3phRegs[0])), hdr->settle);
        programCsiphyRegs(wPhy, po, kSm8550CphyIrqRegs, (unsigned)(sizeof(kSm8550CphyIrqRegs) / sizeof(kSm8550CphyIrqRegs[0])), hdr->settle);
        wr(wPhy, po + FC_CSIPHY_CTRL(0), 0x000eu);
        IOSleep(4); /* >= 3048 us 離線解鎖延遲；不可覆寫 CTRL(11..21) IRQ 暫存器 */
        return (unsigned)(sizeof(kSm8550Cphy3p0Regs) / sizeof(kSm8550Cphy3p0Regs[0])) +
            (unsigned)(sizeof(kSm8550Cphy3phRegs) / sizeof(kSm8550Cphy3phRegs[0]));
    }
    wr(wPhy, po + FC_CSIPHY_CTRL(5), fc_csiphy_ctrl5(hdr->lanes));
    wr(wPhy, po + FC_CSIPHY_CTRL(6), 0x01u);
    wr(wPhy, po + FC_CSIPHY_CTRL(7), 0x02u);
    wr(wPhy, po + FC_CSIPHY_CTRL(0), 0x00u);
    const unsigned dphy_cnt = (unsigned)(sizeof(kSm8550LaneRegs) / sizeof(kSm8550LaneRegs[0]));
    programCsiphyRegs(wPhy, po, kSm8550LaneRegs, dphy_cnt, hdr->settle);
    for (unsigned ctrl_idx = 11u; ctrl_idx <= 21u; ++ctrl_idx) wr(wPhy, po + FC_CSIPHY_CTRL(ctrl_idx), 0u);
    IOSleep(2);
    return dphy_cnt;
}

static void stopCsiphy(const struct fc_window *wPhy, uint32_t po, int is_cphy)
{
    wr(wPhy, po + FC_CSIPHY_CTRL(5), 0u);
    wr(wPhy, po + FC_CSIPHY_CTRL(6), 0u);
    if (is_cphy) {
        wr(wPhy, po + FC_CSIPHY_CTRL(7), 0u);
        wr(wPhy, po + 0x1084u, 0u);
        wr(wPhy, po + 0x108cu, 0u);
    }
}

static int allocContigBuffer(uint32_t alloc_bytes, vm_offset_t *va_out, uint64_t *pa_out)
{
    vm_offset_t dma_va = 0;
    if (kmem_alloc_contig(kernel_map, &dma_va, alloc_bytes, PAGE_MASK, 0, 0, KMA_DATA | KMA_ZERO,
            VM_KERN_MEMORY_IOKIT) != KERN_SUCCESS) {
        return -1;
    }
    const ppnum_t pn0 = pmap_find_phys(kernel_pmap, (addr64_t)dma_va);
    const uint64_t pa0 = ptoa_64(pn0);
    if (!pn0 || !fc_vfe_wm_addr_valid(pa0)) {
        kmem_free(kernel_map, dma_va, alloc_bytes);
        return -2;
    }
    for (uint32_t off = PAGE_SIZE; off < alloc_bytes; off += PAGE_SIZE) {
        const ppnum_t pn = pmap_find_phys(kernel_pmap, (addr64_t)(dma_va + off));
        if (!pn || ptoa_64(pn) != pa0 + (uint64_t)off) {
            kmem_free(kernel_map, dma_va, alloc_bytes);
            return -3;
        }
    }
    *va_out = dma_va;
    *pa_out = pa0;
    return 0;
}

static void busLine(void *context, unsigned pin, int high);
static int busLevel(void *context, unsigned pin);
static void busWait(void *context, unsigned us);
static const char *rcName(int rc);
static void hexLine(char *text, size_t size, const uint8_t *bytes, unsigned count);

static unsigned runSensorTab(struct fc_bus *bus, const uint8_t *tab, const struct fc_tab_header *hdr,
    unsigned *written_out, int *first_err_out, uint16_t *first_reg_out)
{
    unsigned written = 0u, errs = 0u;
    int first_err = FC_I2C_OK;
    uint16_t first_reg = 0u;
    for (unsigned idx = 0u; idx < hdr->count; ++idx) {
        struct fc_tab2_entry entry;
        fc_tab_entry_any_at(tab, hdr, idx, &entry);
        int wrc = FC_I2C_REFUSED;
        if (entry.op == FC_OP_POLL_EQ && entry.data_type == 1u) {
            wrc = fc_i2c_poll16_u8(bus, hdr->slave7, entry.reg, (uint8_t)entry.val, 30u, 1000u);
        } else if (entry.op == FC_OP_WRITE && entry.data_type == 2u) {
            wrc = fc_i2c_write16_u16(bus, hdr->slave7, entry.reg, entry.val);
        } else if (entry.op == FC_OP_WRITE && entry.data_type == 1u) {
            wrc = fc_i2c_write16_u8(bus, hdr->slave7, entry.reg, (uint8_t)entry.val);
        }
        if (wrc != FC_I2C_OK) {
            errs++;
            if (first_err == FC_I2C_OK) {
                first_err = wrc;
                first_reg = entry.reg;
            }
            if (errs >= 4u) break;
        } else {
            written++;
        }
        if (entry.delay_ms) IOSleep(entry.delay_ms);
    }
    if (written_out) *written_out = written;
    if (first_err_out) *first_err_out = first_err;
    if (first_reg_out) *first_reg_out = first_reg;
    return errs;
}

/* C3（模式 6）：從 ADT /chosen 讀取感測器 FUXICAM1/2 暫存器表，設定對應 CSIPHY→CSID-lite0→VFE-lite0 RDI0，
 * 連續收 2 幀後停流並反序撤銷。 */
static void camssSensorCapture(const struct fc_sensor_hw *hw, const struct fc_window *wLite, const struct fc_window *wPhy,
    unsigned sda, unsigned scl)
{
    DTEntry chosen;
    const void *prop = NULL;
    unsigned prop_bytes = 0u;
    if (SecureDTLookupEntry(NULL, "/chosen", &chosen) != kSuccess ||
        SecureDTGetProperty(chosen, hw->adt_tab_prop, &prop, &prop_bytes) != kSuccess || !prop) {
        printf("FUXI CAMERA: c3 [%s] skipped (ADT %s missing)\n", hw->name, hw->adt_tab_prop);
        return;
    }
    const uint8_t *tab = (const uint8_t *)prop;
    struct fc_tab_header hdr;
    uint8_t tab_sid = 0u;
    const int parse_rc = fc_tab_parse_any(tab, prop_bytes, &hdr, &tab_sid);
    if (parse_rc != 0 || (tab_sid != 0u && tab_sid != hw->sensor_id)) {
        printf("FUXI CAMERA: c3 [%s] invalid %s (%u B, rc=%d sid=%u)\n", hw->name, hw->adt_tab_prop, prop_bytes, parse_rc, tab_sid);
        return;
    }
    const uint32_t bytes_per_line = fc_vfe_wm_line_stride(fc_raw10_bytes_per_line(hdr.width));
    const uint32_t frame_bytes = bytes_per_line * hdr.height;
    const uint32_t alloc_bytes = fc_vfe_wm_alloc_bytes(frame_bytes, (uint32_t)PAGE_MASK);
    printf("FUXI CAMERA: c3 [%s] tab ok v%u slave=%#x %ux%u dt=%#x lanes=%u settle=%u assign=%#x regs=%u crc=%#010x frame=%u B (alloc=%u B)\n",
        hw->name, hdr.version, hdr.slave7, hdr.width, hdr.height, hdr.dt, hdr.lanes, hdr.settle, hdr.lane_assign, hdr.count, hdr.crc32,
        frame_bytes, alloc_bytes);

    struct fc_smmu_ctx smmu;
    bzero(&smmu, sizeof(smmu));
    if (!smmuAttachIfe(&smmu)) return;

    vm_offset_t dma0_va = 0, dma1_va = 0;
    uint64_t dma0_pa = 0, dma1_pa = 0;
    if (allocContigBuffer(alloc_bytes, &dma0_va, &dma0_pa) != 0) {
        printf("FUXI CAMERA: c3 DMA buf0 alloc %u B failed\n", alloc_bytes);
        smmuDetachIfe(&smmu);
        return;
    }
    const int have_buf1 = allocContigBuffer(alloc_bytes, &dma1_va, &dma1_pa) == 0;
    memset((void *)dma0_va, FC_TPG_CANARY, alloc_bytes);
    dcacheCivac(dma0_va, alloc_bytes);
    if (have_buf1) {
        memset((void *)dma1_va, FC_TPG_CANARY, alloc_bytes);
        dcacheCivac(dma1_va, alloc_bytes);
    }
    printf("FUXI CAMERA: c3 DMA buf0=%#llx buf1=%#llx (two_buf=%d)\n",
        (unsigned long long)dma0_pa, (unsigned long long)dma1_pa, have_buf1);

    const uint32_t csid = FC_CSID_LITE0_OFF;
    const uint32_t vfe = FC_VFE_LITE0_OFF;
    csidClearAllIrqs(wLite);
    wr(wLite, csid + FC_CSID_TOP_IRQ_MASK, FC_CSID_TOP_RESET_DONE);
    wr(wLite, csid + FC_CSID_BUF_DONE_IRQ_MASK, 0x1fu);
    wr(wLite, csid + FC_CSID_RX_IRQ_MASK, 0xffffffffu);
    wr(wLite, csid + FC_CSID_RDI0_IRQ_MASK, 0xffffffffu);
    wr(wLite, csid + FC_CSID_RST_CFG, 0x11u);
    wr(wLite, csid + FC_CSID_RST_CMD, 0x5u);
    const int reset_ok = waitBits(wLite, csid + FC_CSID_TOP_IRQ_STATUS, FC_CSID_TOP_RESET_DONE, FC_CSID_TOP_RESET_DONE, 50000u);
    const uint32_t top_after_rst = rd(wLite, csid + FC_CSID_TOP_IRQ_STATUS);
    csidClearAllIrqs(wLite);
    printf("FUXI CAMERA: c3 CSID-lite0 reset %s (top_irq=%#x)\n", reset_ok ? "ok" : "TIMEOUT", top_after_rst);

    wr(wLite, vfe + FC_VFE_BUS_CGC_OVERRIDE, 0x07ffffffu);
    wr(wLite, vfe + FC_VFE_BUS_TEST_BUS_CTRL, 0u);
    wr(wLite, vfe + FC_VFE_WM0_FRAME_INCR, fc_vfe_wm_frame_incr(bytes_per_line, hdr.height));
    wr(wLite, vfe + FC_VFE_WM0_IMAGE_CFG0, 0xffffu);
    wr(wLite, vfe + FC_VFE_WM0_IMAGE_CFG2, 0xffffu);
    wr(wLite, vfe + FC_VFE_WM0_PACKER_CFG, 0u);
    wr(wLite, vfe + FC_VFE_WM0_FDROP_PERIOD, 0u);
    wr(wLite, vfe + FC_VFE_WM0_FDROP_PATTERN, 1u);
    wr(wLite, vfe + FC_VFE_WM0_IRQ_PERIOD, 0u);
    wr(wLite, vfe + FC_VFE_WM0_IRQ_PATTERN, 1u);
    wr(wLite, vfe + FC_VFE_WM0_MMU_PREFETCH_CFG, 1u);
    wr(wLite, vfe + FC_VFE_WM0_MMU_PREFETCH_MAX, 0xffffffffu);
    wr(wLite, vfe + FC_VFE_WM0_CFG, 0x10001u);
    wr(wLite, vfe + FC_VFE_WM0_IMAGE_ADDR, fc_vfe_wm_addr(dma0_pa));
    wr(wLite, csid + FC_CSID_RUP_AUP_CMD, FC_CSID_RUP_AUP_RDI0);

    const int is_cphy = hw->is_cphy || ((hdr.flags & FC_TAB_FLAG_CPHY) != 0u);
    const uint32_t rdi_cfg0 = fc_csid_rdi_cfg0(hdr.dt, 0u, 0u, 0);
    wr(wLite, csid + FC_CSID_RDI0_CFG0, rdi_cfg0);
    wr(wLite, csid + FC_CSID_RDI0_CFG1, fc_csid_rdi_cfg1());
    wr(wLite, csid + FC_CSID_RDI0_SUBSAMPLE_PERIOD, 0u);
    wr(wLite, csid + FC_CSID_RDI0_SUBSAMPLE_PATTERN, 1u);
    wr(wLite, csid + FC_CSID_RDI0_CTRL, 0u);
    wr(wLite, csid + FC_CSID_RDI0_CFG0, rdi_cfg0 | (1u << 31));
    wr(wLite, csid + FC_CSID_RX_CFG0, fc_csid_rx_cfg0_phy(hdr.lanes, hdr.lane_assign, hw->csiphy_idx, is_cphy));
    wr(wLite, csid + FC_CSID_RX_CFG1, 1u);
    wr(wLite, csid + FC_CSID_RDI0_CTRL, 1u);
    wr(wLite, csid + FC_CSID_RUP_AUP_CMD, FC_CSID_RUP_AUP_RDI0);

    /* CSIPHY Gen2 2.1.2（支援 D-PHY 4-lane 與 IMX800 3-trio C-PHY）。 */
    const uint32_t po = hw->phy_off;
    const unsigned lane_reg_count = programCsiphy(wPhy, po, &hdr, is_cphy);
    printf("FUXI CAMERA: c3 CSIPHY%u (off=%#x cphy=%d) enabled (%u lane regs, ctrl5=%#x ctrl6=%#x rx_cfg0=%#x)\n",
        hw->csiphy_idx, po, is_cphy, lane_reg_count, rd(wPhy, po + FC_CSIPHY_CTRL(5)), rd(wPhy, po + FC_CSIPHY_CTRL(6)),
        rd(wLite, csid + FC_CSID_RX_CFG0));

    static const struct fc_bus_ops kC3BusOps = {busLine, busLevel, busWait};
    struct fc_bus bus = {&kC3BusOps, NULL, sda, scl, 5u, 200u, 0u, 0u, 0u};
    pinSetIo(sda, 0u);
    pinSetIo(scl, 0u);
    busLine(NULL, sda, 1);
    busLine(NULL, scl, 1);
    delay(50);

    unsigned written = 0u;
    int first_err = FC_I2C_OK;
    uint16_t first_err_reg = 0u;
    const unsigned write_errors = runSensorTab(&bus, tab, &hdr, &written, &first_err, &first_err_reg);
    printf("FUXI CAMERA: c3 [%s] i2c table %u/%u ok (err=%u first=%s@%#06x stretched=%u stuck=%u rec=%u)\n",
        hw->name, written, hdr.count, write_errors, rcName(first_err), first_err_reg, bus.stretched, bus.stuck, bus.recovered);

    uint32_t f1_buf = 0u, f1_rdi = 0u, f1_rx = 0u;
    uint32_t f2_buf = 0u, f2_rdi = 0u, f2_rx = 0u;
    unsigned f1_ms = 0u, f1_rup_ms = 0u, f2_ms = 0u, f2_rup_ms = 0u;
    int stream_rc = FC_I2C_REFUSED;
    if (write_errors == 0u) {
        csidClearAllIrqs(wLite);
        stream_rc = fc_sensor_stream_on(hw->sensor_id, &bus, hdr.slave7);
        printf("FUXI CAMERA: c3 [%s] sensor streamOn %s\n", hw->name, rcName(stream_rc));
        if (stream_rc == FC_I2C_OK) {
            for (unsigned ms = 1u; ms <= 1000u; ++ms) {
                IOSleep(1);
                const uint32_t bd = rd(wLite, csid + FC_CSID_BUF_DONE_IRQ_STATUS);
                const uint32_t r0 = rd(wLite, csid + FC_CSID_RDI0_IRQ_STATUS);
                const uint32_t rx = rd(wLite, csid + FC_CSID_RX_IRQ_STATUS);
                if (!f1_rup_ms && (r0 & FC_CSID_RDI_RUP_DONE)) f1_rup_ms = ms;
                if (!f1_ms && (bd & FC_CSID_BUF_DONE_RDI0)) f1_ms = ms;
                f1_buf |= bd;
                f1_rdi |= r0;
                f1_rx |= rx;
                if (f1_buf & FC_CSID_BUF_DONE_RDI0) break;
            }
            if ((f1_buf & FC_CSID_BUF_DONE_RDI0) && have_buf1) {
                csidClearAllIrqs(wLite);
                wr(wLite, vfe + FC_VFE_WM0_IMAGE_ADDR, fc_vfe_wm_addr(dma1_pa));
                wr(wLite, csid + FC_CSID_RUP_AUP_CMD, FC_CSID_RUP_AUP_RDI0);
                for (unsigned ms = 1u; ms <= 1000u; ++ms) {
                    IOSleep(1);
                    const uint32_t bd = rd(wLite, csid + FC_CSID_BUF_DONE_IRQ_STATUS);
                    const uint32_t r0 = rd(wLite, csid + FC_CSID_RDI0_IRQ_STATUS);
                    const uint32_t rx = rd(wLite, csid + FC_CSID_RX_IRQ_STATUS);
                    if (!f2_rup_ms && (r0 & FC_CSID_RDI_RUP_DONE)) f2_rup_ms = ms;
                    if (!f2_ms && (bd & FC_CSID_BUF_DONE_RDI0)) f2_ms = ms;
                    f2_buf |= bd;
                    f2_rdi |= r0;
                    f2_rx |= rx;
                    if (f2_buf & FC_CSID_BUF_DONE_RDI0) break;
                }
            }
            const int stop_rc = fc_sensor_stream_off(hw->sensor_id, &bus, hdr.slave7);
            printf("FUXI CAMERA: c3 [%s] sensor streamOff %s\n", hw->name, rcName(stop_rc));
        }
    }
    pinRestore(hw->pin_sda);
    pinRestore(hw->pin_scl);

    const uint32_t phy_st0 = rd(wPhy, po + FC_CSIPHY_STATUS(0));
    const uint32_t phy_st1 = rd(wPhy, po + FC_CSIPHY_STATUS(1));
    wr(wLite, csid + FC_CSID_RDI0_CTRL, 0u);
    wr(wLite, csid + FC_CSID_RDI0_CFG0, 0u);
    wr(wLite, vfe + FC_VFE_WM0_CFG, 0u);
    IOSleep(2);
    wr(wLite, csid + FC_CSID_RST_CFG, 0x11u);
    wr(wLite, csid + FC_CSID_RST_CMD, 0x5u);
    (void)waitBits(wLite, csid + FC_CSID_TOP_IRQ_STATUS, FC_CSID_TOP_RESET_DONE, FC_CSID_TOP_RESET_DONE, 20000u);
    wr(wLite, csid + FC_CSID_TOP_IRQ_MASK, 0u);
    wr(wLite, csid + FC_CSID_BUF_DONE_IRQ_MASK, 0u);
    wr(wLite, csid + FC_CSID_RX_IRQ_MASK, 0u);
    wr(wLite, csid + FC_CSID_RDI0_IRQ_MASK, 0u);
    csidClearAllIrqs(wLite);
    stopCsiphy(wPhy, po, is_cphy);
    smmuDetachIfe(&smmu);

    dcacheCivac(dma0_va, frame_bytes);
    struct fc_frame_stats stats1;
    fc_frame_analyze((const uint8_t *)dma0_va, frame_bytes, FC_TPG_CANARY, &stats1);
    char f1_row0[56], f1_mid[56];
    const uint32_t mid_off = (hdr.height / 2u) * bytes_per_line;
    hexLine(f1_row0, sizeof(f1_row0), (const uint8_t *)dma0_va, 16u);
    hexLine(f1_mid, sizeof(f1_mid), (const uint8_t *)(dma0_va + mid_off), 16u);
    printf("FUXI CAMERA: c3 f1 buf_done=%#x(%u ms) rdi0=%#x(rup=%u ms) rx=%#x phy_st=%#x/%#x\n",
        f1_buf, f1_ms, f1_rdi, f1_rup_ms, f1_rx, phy_st0, phy_st1);
    printf("FUXI CAMERA: c3 f1 non_canary=%u/%u min=%u max=%u trans=%u fnv1a=%#010x row0=[%s] mid=[%s]\n",
        stats1.non_canary, frame_bytes, stats1.min_val, stats1.max_val, stats1.transitions, stats1.fnv1a, f1_row0, f1_mid);
    if (have_buf1) {
        dcacheCivac(dma1_va, frame_bytes);
        struct fc_frame_stats stats2;
        fc_frame_analyze((const uint8_t *)dma1_va, frame_bytes, FC_TPG_CANARY, &stats2);
        const uint32_t diff_bytes = fc_frame_diff((const uint8_t *)dma0_va, (const uint8_t *)dma1_va, frame_bytes);
        char f2_row0[56], f2_mid[56];
        hexLine(f2_row0, sizeof(f2_row0), (const uint8_t *)dma1_va, 16u);
        hexLine(f2_mid, sizeof(f2_mid), (const uint8_t *)(dma1_va + mid_off), 16u);
        printf("FUXI CAMERA: c3 f2 buf_done=%#x(%u ms) rdi0=%#x(rup=%u ms) rx=%#x diff=%u/%u\n",
            f2_buf, f2_ms, f2_rdi, f2_rup_ms, f2_rx, diff_bytes, frame_bytes);
        printf("FUXI CAMERA: c3 f2 non_canary=%u/%u min=%u max=%u trans=%u fnv1a=%#010x row0=[%s] mid=[%s]\n",
            stats2.non_canary, frame_bytes, stats2.min_val, stats2.max_val, stats2.transitions, stats2.fnv1a, f2_row0, f2_mid);
        kmem_free(kernel_map, dma1_va, alloc_bytes);
    }
    kmem_free(kernel_map, dma0_va, alloc_bytes);
}

/* C2a／C2b／C3：在 titan_top_gdsc 通電期間讀取 CAMSS（CPAS／CSID-lite0／VFE-lite0／CSIPHYx）硬體版本並反序還原。 */
static void camssProbe(const struct fc_sensor_hw *hw, int use_pll, unsigned cam_mode, int use_mmnoc,
    unsigned active_sda, unsigned active_scl)
{
    const int run_tpg = cam_mode == 5u;
    const int run_sensor = cam_mode >= 6u;
    const uint32_t pll_mode0 = rd(&gCamcc, FC_PLL_MODE), pll_op0 = rd(&gCamcc, FC_PLL_OPMODE);
    const uint32_t pll_state0 = rd(&gCamcc, FC_PLL_STATE), pll_status0 = rd(&gCamcc, FC_PLL_STATUS);
    const uint32_t pll_l0 = rd(&gCamcc, FC_PLL_L_VAL), pll_alpha0 = rd(&gCamcc, FC_PLL_ALPHA);
    const uint32_t pll_u0 = rd(&gCamcc, FC_PLL_USER_CTL), pll_uu0 = rd(&gCamcc, FC_PLL_USER_CTL_U);
    printf("FUXI CAMERA: camcc pll0 found mode=%#x op=%#x st=%#x/%#x l=%#x a=%#x u=%#x/%#x (use_pll=%d)\n",
        pll_mode0, pll_op0, pll_state0, pll_status0, pll_l0, pll_alpha0, pll_u0, pll_uu0, use_pll);
    int pll_enabled = 0;
    if (use_pll && !fc_pll_is_enabled(pll_mode0, pll_op0)) {
        wr(&gCamcc, FC_PLL_L_VAL, FC_PLL0_L_VAL);
        wr(&gCamcc, FC_PLL_ALPHA, FC_PLL0_ALPHA);
        wr(&gCamcc, FC_PLL_CONFIG_CTL, FC_PLL0_CONFIG_CTL);
        wr(&gCamcc, FC_PLL_CONFIG_CTL_U, FC_PLL0_CONFIG_CTL_U);
        wr(&gCamcc, FC_PLL_CONFIG_CTL_U1, FC_PLL0_CONFIG_CTL_U1);
        wr(&gCamcc, FC_PLL_USER_CTL, FC_PLL0_USER_CTL);
        wr(&gCamcc, FC_PLL_USER_CTL_U, FC_PLL0_USER_CTL_U);
        wr(&gCamcc, FC_PLL_TEST_CTL, FC_PLL0_TEST_CTL);
        wr(&gCamcc, FC_PLL_TEST_CTL_U, FC_PLL0_TEST_CTL_U);
        wr(&gCamcc, FC_PLL_TEST_CTL_U1, FC_PLL0_TEST_CTL_U1);
        wr(&gCamcc, FC_PLL_TEST_CTL_U2, FC_PLL0_TEST_CTL_U2);
        wr(&gCamcc, FC_PLL_MODE, rd(&gCamcc, FC_PLL_MODE) & ~FC_PLL_OUTCTRL);
        wr(&gCamcc, FC_PLL_OPMODE, FC_PLL_OP_STANDBY);
        wr(&gCamcc, FC_PLL_MODE, rd(&gCamcc, FC_PLL_MODE) | FC_PLL_RESET_N);
        wr(&gCamcc, FC_PLL_OPMODE, FC_PLL_OP_RUN);
        const int locked = waitBits(&gCamcc, FC_PLL_MODE, FC_PLL_LOCK_DET, FC_PLL_LOCK_DET, 2000);
        if (!locked) {
            printf("FUXI CAMERA: camcc pll0 FAILED to lock (mode=%#x op=%#x st=%#x/%#x)\n",
                rd(&gCamcc, FC_PLL_MODE), rd(&gCamcc, FC_PLL_OPMODE), rd(&gCamcc, FC_PLL_STATE), rd(&gCamcc, FC_PLL_STATUS));
            wr(&gCamcc, FC_PLL_OPMODE, FC_PLL_OP_STANDBY);
            if (!(pll_mode0 & FC_PLL_RESET_N)) wr(&gCamcc, FC_PLL_MODE, rd(&gCamcc, FC_PLL_MODE) & ~FC_PLL_RESET_N);
            return;
        }
        wr(&gCamcc, FC_PLL_USER_CTL, rd(&gCamcc, FC_PLL_USER_CTL) | FC_PLL_OUT_MASK);
        wr(&gCamcc, FC_PLL_MODE, rd(&gCamcc, FC_PLL_MODE) | FC_PLL_OUTCTRL);
        pll_enabled = 1;
        printf("FUXI CAMERA: camcc pll0 locked 1200 MHz (mode=%#x op=%#x u=%#x)\n",
            rd(&gCamcc, FC_PLL_MODE), rd(&gCamcc, FC_PLL_OPMODE), rd(&gCamcc, FC_PLL_USER_CTL));
    }

    const struct { const char *name; uint32_t cmd; uint8_t has_mnd, pll_src, pll_hid; } kRcgs[7] = {
        {"slow_ahb", FC_SLOW_AHB_RCG, 1u, 2u, 14u},
        {"fast_ahb", FC_FAST_AHB_RCG, 0u, 2u, 3u},
        {"camnoc_axi", FC_CAMNOC_AXI_RCG, 0u, 2u, 3u},
        {"cphy_rx", FC_CPHY_RX_RCG, 0u, 1u, 5u},
        {"phytimer", hw->phytimer_rcg, 0u, hw->phytimer_pll_src, hw->phytimer_pll_hid},
        {"ife_lite", FC_IFE_LITE_RCG, 0u, 1u, 5u},
        {"ife_lite_csid", FC_IFE_LITE_CSID_RCG, 0u, 1u, 5u},
    };
    struct { int saved; uint32_t cfg, m_val, n_val, d_val; } rcg_save[7];
    bzero(rcg_save, sizeof(rcg_save));
    int rcgs_ok = 1;
    for (unsigned idx = 0; idx < 7u; ++idx) {
        const uint32_t base = kRcgs[idx].cmd;
        const uint32_t found_cmd = rd(&gCamcc, base);
        rcg_save[idx].cfg = rd(&gCamcc, base + 4u);
        if (kRcgs[idx].has_mnd) {
            rcg_save[idx].m_val = rd(&gCamcc, base + 8u);
            rcg_save[idx].n_val = rd(&gCamcc, base + 0xcu);
            rcg_save[idx].d_val = rd(&gCamcc, base + 0x10u);
            wr(&gCamcc, base + 8u, 0u);
            wr(&gCamcc, base + 0xcu, 0u);
            wr(&gCamcc, base + 0x10u, 0u);
        }
        rcg_save[idx].saved = 1;
        const unsigned target_src = use_pll ? kRcgs[idx].pll_src : 0u;
        const unsigned target_hid = use_pll ? kRcgs[idx].pll_hid : 1u;
        wr(&gCamcc, base + 4u, fc_rcg_cfg(rcg_save[idx].cfg, target_src, target_hid));
        wr(&gCamcc, base, found_cmd | FC_RCG_CMD_UPDATE);
        const int ok = waitBits(&gCamcc, base, FC_RCG_CMD_UPDATE, 0u, 10000u);
        if (!ok) {
            printf("FUXI CAMERA: camss rcg %s FAILED (found cmd=%#x cfg=%#x -> cmd=%#x cfg=%#x)\n",
                kRcgs[idx].name, found_cmd, rcg_save[idx].cfg, rd(&gCamcc, base), rd(&gCamcc, base + 4u));
            rcgs_ok = 0;
            break;
        }
    }
    if (rcgs_ok) printf("FUXI CAMERA: camss 7 RCGs set (%s)\n", use_pll ? "PLL0 SVS" : "XO 19.2 MHz");

    struct fc_branch_spec { const struct fc_window *win; uint32_t reg; const char *name; };
    const struct fc_branch_spec branches[13] = {
        {&gGcc, FC_GCC_CAMERA_HF_AXI, "gcc hf_axi"},
        {&gGcc, FC_GCC_CAMERA_SF_AXI, "gcc sf_axi"},
        {&gCamcc, FC_CPAS_FAST_AHB, "camcc cpas_fast_ahb"},
        {&gCamcc, FC_CPAS_AHB, "camcc cpas_ahb"},
        {&gCamcc, FC_CAMNOC_AXI_CBCR, "camcc camnoc_axi"},
        {&gCamcc, FC_CPAS_IFE_LITE_CBCR, "camcc cpas_ife_lite"},
        {&gCamcc, FC_IFE_LITE_AHB_CBCR, "camcc ife_lite_ahb"},
        {&gCamcc, FC_IFE_LITE_CBCR, "camcc ife_lite"},
        {&gCamcc, FC_IFE_LITE_CPHY_RX_CBCR, "camcc ife_lite_cphy_rx"},
        {&gCamcc, FC_IFE_LITE_CSID_CBCR, "camcc ife_lite_csid"},
        {&gCamcc, hw->csiphy_cbcr, "camcc csiphy"},
        {&gCamcc, hw->phytimer_cbcr, "camcc phytimer"},
        {&gCamcc, FC_CSID_CSIPHY_RX_CBCR, "camcc csid_csiphy_rx"},
    };
    const unsigned branch_count = (run_tpg || run_sensor) ? 13u : 12u;
    int br_sw[13], br_ok[13];
    int mm0_voted = 0;
    bzero(br_sw, sizeof(br_sw));
    bzero(br_ok, sizeof(br_ok));
    if (rcgs_ok) {
        if ((run_tpg || run_sensor) && use_mmnoc && gMm0Address) {
            mm0_voted = voteBcm("MM0", gMm0Address, 1) == 0;
        }
        for (unsigned idx = 0; idx < branch_count; ++idx) {
            br_ok[idx] = branchOn(branches[idx].win, branches[idx].reg, branches[idx].name, &br_sw[idx]);
        }
        const int cpas_ready = br_ok[0] && br_ok[1] && br_ok[2] && br_ok[3] && br_ok[4];
        const int vfe_ready = cpas_ready && br_ok[5] && br_ok[6] && br_ok[7];
        const int csid_ready = vfe_ready && br_ok[8] && br_ok[9];
        const int phy_ready = cpas_ready && br_ok[10] && br_ok[11];
        const int tpg_ready = run_tpg && csid_ready && vfe_ready && br_ok[12];
        struct fc_window wCpas = {FC_CPAS_PAGE, 0x4000u, 0};
        struct fc_window wLite = {FC_LITE_PAGE, FC_LITE_SIZE, 0};
        struct fc_window wPhy = {hw->phy_page, 0x4000u, 0};
        if (cpas_ready && fc_map(&wCpas) == KERN_SUCCESS) {
            const uint32_t ver = rd(&wCpas, FC_CPAS_OFF);
            printf("FUXI CAMERA: camss CPAS hw_version=%#x (%u.%u.%u)\n", ver,
                fc_camss_hw_gen(ver), fc_camss_hw_rev(ver), fc_camss_hw_step(ver));
        } else {
            printf("FUXI CAMERA: camss CPAS skipped (clocks=%d)\n", cpas_ready);
        }
        int phy_mapped = 0;
        if (phy_ready && fc_map(&wPhy) == KERN_SUCCESS) {
            phy_mapped = 1;
            const uint32_t po = hw->phy_off;
            const uint32_t ctrl6 = rd(&wPhy, po + FC_CSIPHY_CTRL6);
            wr(&wPhy, po + FC_CSIPHY_CTRL6, ctrl6 | 0x2u);
            const uint32_t s12 = rd(&wPhy, po + FC_CSIPHY_STATUS12);
            const uint32_t s13 = rd(&wPhy, po + FC_CSIPHY_STATUS12 + 4u);
            const uint32_t s14 = rd(&wPhy, po + FC_CSIPHY_STATUS12 + 8u);
            const uint32_t s15 = rd(&wPhy, po + FC_CSIPHY_STATUS12 + 12u);
            wr(&wPhy, po + FC_CSIPHY_CTRL6, ctrl6);
            const uint32_t ver = fc_csiphy_pack_ver(s12, s13, s14, s15);
            printf("FUXI CAMERA: camss CSIPHY%u (off=%#x) hw_version=%#x (ctrl6=%#x)\n", hw->csiphy_idx, po, ver, ctrl6);
        } else {
            printf("FUXI CAMERA: camss CSIPHY%u skipped (clocks=%d)\n", hw->csiphy_idx, phy_ready);
        }
        if ((vfe_ready || csid_ready) && fc_map(&wLite) == KERN_SUCCESS) {
            if (csid_ready) {
                const uint32_t ver = rd(&wLite, FC_CSID_LITE0_OFF);
                printf("FUXI CAMERA: camss CSID-lite0 hw_version=%#x (%u.%u.%u)\n", ver,
                    fc_camss_hw_gen(ver), fc_camss_hw_rev(ver), fc_camss_hw_step(ver));
            } else {
                printf("FUXI CAMERA: camss CSID-lite0 skipped (clocks=0)\n");
            }
            if (vfe_ready) {
                const uint32_t ver = rd(&wLite, FC_VFE_LITE0_OFF);
                printf("FUXI CAMERA: camss VFE-lite0 hw_version=%#x (%u.%u.%u)\n", ver,
                    fc_camss_hw_gen(ver), fc_camss_hw_rev(ver), fc_camss_hw_step(ver));
            } else {
                printf("FUXI CAMERA: camss VFE-lite0 skipped (clocks=0)\n");
            }
            if (tpg_ready) {
                camssTpgCapture(&wLite);
            } else if (run_tpg) {
                printf("FUXI CAMERA: c2b TPG0 skipped (csid=%d vfe=%d tpg_clk=%d)\n", csid_ready, vfe_ready, br_ok[12]);
            }
            if (run_sensor && csid_ready && vfe_ready && phy_mapped && br_ok[12]) {
                camssSensorCapture(hw, &wLite, &wPhy, active_sda, active_scl);
            } else if (run_sensor) {
                printf("FUXI CAMERA: c3 skipped (csid=%d vfe=%d phy=%d rx_clk=%d)\n", csid_ready, vfe_ready, phy_mapped, br_ok[12]);
            }
        } else {
            printf("FUXI CAMERA: camss CSID/VFE-lite0 skipped (csid=%d vfe=%d)\n", csid_ready, vfe_ready);
        }
        fc_unmap(&wPhy);
        fc_unmap(&wLite);
        fc_unmap(&wCpas);
    }

    for (int idx = (int)branch_count - 1; idx >= 0; --idx) {
        branchOff(branches[idx].win, branches[idx].reg, &br_sw[idx]);
    }
    if (mm0_voted) (void)voteBcm("MM0", gMm0Address, 0);
    for (int idx = 6; idx >= 0; --idx) {
        if (!rcg_save[idx].saved) continue;
        const uint32_t base = kRcgs[idx].cmd;
        if (kRcgs[idx].has_mnd) {
            wr(&gCamcc, base + 8u, rcg_save[idx].m_val);
            wr(&gCamcc, base + 0xcu, rcg_save[idx].n_val);
            wr(&gCamcc, base + 0x10u, rcg_save[idx].d_val);
        }
        wr(&gCamcc, base + 4u, rcg_save[idx].cfg);
        wr(&gCamcc, base, rd(&gCamcc, base) | FC_RCG_CMD_UPDATE);
        (void)waitBits(&gCamcc, base, FC_RCG_CMD_UPDATE, 0u, 10000u);
    }
    if (pll_enabled) {
        wr(&gCamcc, FC_PLL_MODE, rd(&gCamcc, FC_PLL_MODE) & ~FC_PLL_OUTCTRL);
        wr(&gCamcc, FC_PLL_USER_CTL, rd(&gCamcc, FC_PLL_USER_CTL) & ~FC_PLL_OUT_MASK);
        wr(&gCamcc, FC_PLL_OPMODE, FC_PLL_OP_STANDBY);
        if (!(pll_mode0 & FC_PLL_RESET_N)) wr(&gCamcc, FC_PLL_MODE, rd(&gCamcc, FC_PLL_MODE) & ~FC_PLL_RESET_N);
        printf("FUXI CAMERA: camcc pll0 disabled again (mode=%#x op=%#x)\n",
            rd(&gCamcc, FC_PLL_MODE), rd(&gCamcc, FC_PLL_OPMODE));
    }
    printf("FUXI CAMERA: camss probe restored\n");
}
static int gdscOn(void)
{
    gUp.gdscr = rd(&gCamcc, FC_TITAN_GDSCR);
    const uint32_t cfg = rd(&gCamcc, FC_TITAN_GDSCR + 4);
    if (fc_gdsc_is_on(gUp.gdscr, cfg)) {
        printf("FUXI CAMERA: titan gdsc already on (gdscr %#x cfg %#x)\n", gUp.gdscr, cfg);
        return 1;
    }
    wr(&gCamcc, FC_TITAN_GDSCR, gUp.gdscr & ~FC_GDSC_SW_COLLAPSE);
    gUp.gdsc_switched = 1;
    const uint64_t began = mach_absolute_time();
    const int ok = waitBits(&gCamcc, FC_TITAN_GDSCR + 4, FC_GDSC_CFG_UP_DONE, FC_GDSC_CFG_UP_DONE, 100000);
    uint64_t ns = 0;
    absolutetime_to_nanoseconds(mach_absolute_time() - began, &ns);
    if (ok) wr(&gCamcc, FC_TITAN_GDSCR, rd(&gCamcc, FC_TITAN_GDSCR) | FC_GDSC_RETAIN_FF); /* gdsc.c：RETAIN_FF_ENABLE 的 GDSC 開後設 */
    printf("FUXI CAMERA: titan gdsc %s after %llu us (found %#x, now gdscr %#x cfg %#x)\n", ok ? "on" : "DID NOT COME ON",
        (unsigned long long)(ns / 1000ull), gUp.gdscr, rd(&gCamcc, FC_TITAN_GDSCR), rd(&gCamcc, FC_TITAN_GDSCR + 4));
    return ok;
}
static void gdscRestore(void)
{
    if (!gUp.gdsc_switched) return;
    wr(&gCamcc, FC_TITAN_GDSCR, gUp.gdscr | FC_GDSC_SW_COLLAPSE);
    const int ok = waitBits(&gCamcc, FC_TITAN_GDSCR + 4, FC_GDSC_CFG_DOWN_DONE, FC_GDSC_CFG_DOWN_DONE, 100000);
    printf("FUXI CAMERA: titan gdsc %s (gdscr %#x cfg %#x)\n", ok ? "collapsed again" : "NOT COLLAPSED", rd(&gCamcc, FC_TITAN_GDSCR),
        rd(&gCamcc, FC_TITAN_GDSCR + 4));
    gUp.gdsc_switched = 0;
}
static int mclkOnFor(const struct fc_sensor_hw *hw)
{
    gUp.mclk_rcg = hw->mclk_rcg;
    gUp.mclk_cbcr = hw->mclk_cbcr;
    gUp.mclk_cfg = rd(&gCamcc, gUp.mclk_rcg + 4u);
    gUp.mclk_cbcr_saved = rd(&gCamcc, gUp.mclk_cbcr);
    gUp.mclk_switched = 1;
    wr(&gCamcc, gUp.mclk_rcg + 4u, fc_rcg_cfg_xo(gUp.mclk_cfg));
    wr(&gCamcc, gUp.mclk_rcg, rd(&gCamcc, gUp.mclk_rcg) | FC_RCG_CMD_UPDATE);
    if (!waitBits(&gCamcc, gUp.mclk_rcg, FC_RCG_CMD_UPDATE, 0, 10000)) {
        printf("FUXI CAMERA: [%s] mclk rcg does not take its configuration (cmd %#x cfg %#x)\n", hw->name,
            rd(&gCamcc, gUp.mclk_rcg), rd(&gCamcc, gUp.mclk_rcg + 4u));
        return 0;
    }
    wr(&gCamcc, gUp.mclk_cbcr, gUp.mclk_cbcr_saved | FC_CBCR_ENABLE);
    const int ok = waitBits(&gCamcc, gUp.mclk_cbcr, FC_CBCR_OFF, 0, 10000);
    printf("FUXI CAMERA: [%s] mclk 19.2 MHz %s (cmd %#x cfg %#x cbcr %#x; found cfg %#x cbcr %#x)\n", hw->name,
        ok ? "on" : "DID NOT COME ON", rd(&gCamcc, gUp.mclk_rcg), rd(&gCamcc, gUp.mclk_rcg + 4u),
        rd(&gCamcc, gUp.mclk_cbcr), gUp.mclk_cfg, gUp.mclk_cbcr_saved);
    return ok;
}
static void mclkRestore(void)
{
    if (!gUp.mclk_switched) return;
    wr(&gCamcc, gUp.mclk_cbcr, (rd(&gCamcc, gUp.mclk_cbcr) & ~FC_CBCR_ENABLE) | (gUp.mclk_cbcr_saved & FC_CBCR_ENABLE));
    if (!(gUp.mclk_cbcr_saved & FC_CBCR_ENABLE)) (void)waitBits(&gCamcc, gUp.mclk_cbcr, FC_CBCR_OFF, FC_CBCR_OFF, 10000);
    wr(&gCamcc, gUp.mclk_rcg + 4u, gUp.mclk_cfg);
    wr(&gCamcc, gUp.mclk_rcg, rd(&gCamcc, gUp.mclk_rcg) | FC_RCG_CMD_UPDATE);
    (void)waitBits(&gCamcc, gUp.mclk_rcg, FC_RCG_CMD_UPDATE, 0, 10000);
    gUp.mclk_switched = 0;
}

/* ---- 手動 I2C 的腳位回呼：func 0（GPIO）、2 mA、內部上拉；拉低＝輸出致能（IO 的 OUT 先清 0） ---- */
static void busLine(void *context, unsigned pin, int high)
{
    (void)context;
    pinSetCtl(pin, fc_pin_ctl(foundCtl(pin), 0, 2, FC_CTL_PULL_UP, !high));
}
static int busLevel(void *context, unsigned pin) { (void)context; return pinIo(pin) & FC_IO_IN; }
static void busWait(void *context, unsigned us) { (void)context; delay((int)us); }
static const struct fc_bus_ops kBusOps = {busLine, busLevel, busWait};

static const char *rcName(int rc)
{
    switch (rc) {
    case FC_I2C_OK: return "ok";
    case FC_I2C_NACK_ADDR: return "no ack";
    case FC_I2C_NACK_DATA: return "data nack";
    case FC_I2C_BUSY: return "bus not idle";
    case FC_I2C_STUCK: return "clock held";
    case FC_I2C_TIMEOUT: return "timeout";
    default: return "refused";
    }
}
static void hexLine(char *text, size_t size, const uint8_t *bytes, unsigned count)
{
    static const char digits[] = "0123456789abcdef";
    size_t at = 0;
    for (unsigned i = 0; i < count && at + 3 < size; ++i) {
        text[at++] = digits[bytes[i] >> 4];
        text[at++] = digits[bytes[i] & 15];
        text[at++] = ' ';
    }
    text[at ? at - 1 : 0] = 0;
}
/* 一種 SDA／SCL 指派下掃一次；回傳 ACK 的位址數（負值＝匯流排不閒置）。 */
static int scan(const struct fc_sensor_hw *hw, unsigned sda, unsigned scl, unsigned mode, uint8_t *acks, unsigned *count)
{
    struct fc_bus bus = {&kBusOps, NULL, sda, scl, 5, 200, 0, 0, 0};
    *count = 0;
    pinSetIo(sda, 0);
    pinSetIo(scl, 0);
    busLine(NULL, sda, 1);
    busLine(NULL, scl, 1);
    delay(50);
    const unsigned idle_sda = busLevel(NULL, sda), idle_scl = busLevel(NULL, scl);
    if (!fc_i2c_idle(&bus)) {
        printf("FUXI CAMERA: bus sda %u scl %u not idle (levels %u/%u, recovered %u)\n", sda, scl, idle_sda, idle_scl, bus.recovered);
        return -1;
    }
    unsigned errors = 0;
    for (unsigned a = FC_I2C_FIRST; a <= FC_I2C_LAST; ++a) {
        const int rc = fc_i2c_probe(&bus, a);
        if (rc == 1 && *count < FC_MAX_ACKS) acks[(*count)++] = (uint8_t)a;
        else if (rc < 0) ++errors;
    }
    char text[64] = "none";
    size_t at = 0;
    for (unsigned i = 0; i < *count && at + 6 < sizeof(text); ++i) at += (size_t)snprintf(text + at, sizeof(text) - at, "%s%#x", i ? " " : "", acks[i]);
    printf("FUXI CAMERA: scan [%s] sda %u scl %u (idle %u/%u): acks %s (n=%u, errors %u, stretched %u, stuck %u, recovered %u)\n",
        hw->name, sda, scl, idle_sda, idle_scl, text, *count, errors, bus.stretched, bus.stuck, bus.recovered);
    for (unsigned i = 0; i < *count; ++i) {
        const unsigned a = acks[i];
        uint8_t bytes[16];
        char hex[64];
        if (!fc_may_write(a)) {
            const int rc = fc_i2c_transfer(&bus, a, NULL, 0, bytes, sizeof(bytes));
            hexLine(hex, sizeof(hex), bytes, rc == FC_I2C_OK ? sizeof(bytes) : 0);
            printf("FUXI CAMERA: eeprom-range %#x current-address read 16: %s (%s)\n", a, rc == FC_I2C_OK ? hex : "-", rcName(rc));
            continue;
        }
        for (unsigned p = 0; p < FC_ID_PROBES; ++p) {
            const int rc = fc_i2c_read16(&bus, a, fc_id_probes[p].reg, bytes, fc_id_probes[p].bytes);
            hexLine(hex, sizeof(hex), bytes, rc == FC_I2C_OK ? fc_id_probes[p].bytes : 0);
            printf("FUXI CAMERA: id %#x reg %#06x (%s) = %s (%s)\n", a, fc_id_probes[p].reg, fc_id_probes[p].family,
                rc == FC_I2C_OK ? hex : "-", rcName(rc));
        }
    }
    /* 模式 3（僅 OV32C，wake_aon_slave7 != 0）：喚醒 AON 主核並讀 OmniVision Chip ID。 */
    if (mode >= 3 && hw->wake_aon_slave7 != 0u) {
        int has_aon = 0, has_main = 0;
        for (unsigned i = 0; i < *count; ++i) {
            if (acks[i] == hw->wake_aon_slave7) has_aon = 1;
            if (acks[i] == hw->default_slave7) has_main = 1;
        }
        if (has_aon && !has_main) {
            const int w0 = fc_i2c_write16_u8(&bus, hw->wake_aon_slave7, 0x1000u, 0x00u);
            const int w1 = w0 == FC_I2C_OK ? fc_i2c_write16_u8(&bus, hw->wake_aon_slave7, 0x1001u, 0x04u) : w0;
            printf("FUXI CAMERA: ov32c preInit %#x (0x1000=0x00 %s, 0x1001=0x04 %s)\n",
                hw->wake_aon_slave7, rcName(w0), rcName(w1));
            if (w0 == FC_I2C_OK && w1 == FC_I2C_OK) {
                IOSleep(5);
                const int p10 = fc_i2c_probe(&bus, hw->default_slave7);
                const int p18 = fc_i2c_probe(&bus, hw->wake_aon_slave7);
                printf("FUXI CAMERA: ov32c post-preInit probe %#x=%d %#x=%d\n",
                    hw->default_slave7, p10, hw->wake_aon_slave7, p18);
                if (p10 == 1) {
                    static const struct { uint16_t reg; uint8_t bytes; const char *label; } kOvRegs[] = {
                        {0x300au, 3, "chip_id"},
                        {0x3000u, 4, "sc_top"},
                        {0x302au, 1, "rev"},
                    };
                    for (unsigned idx = 0; idx < sizeof(kOvRegs) / sizeof(kOvRegs[0]); ++idx) {
                        uint8_t bytes[8];
                        char hex[32];
                        const int rc = fc_i2c_read16(&bus, hw->default_slave7, kOvRegs[idx].reg, bytes, kOvRegs[idx].bytes);
                        hexLine(hex, sizeof(hex), bytes, rc == FC_I2C_OK ? kOvRegs[idx].bytes : 0);
                        printf("FUXI CAMERA: ov32c %#x reg %#06x (%s) = %s (%s)\n", hw->default_slave7,
                            kOvRegs[idx].reg, kOvRegs[idx].label, rc == FC_I2C_OK ? hex : "-", rcName(rc));
                    }
                }
            }
        }
    }
    return (int)*count;
}

static void powerDownSensor(const struct fc_sensor_hw *hw)
{
    if (gUp.reset_taken) {
        pinSetIo(gUp.reset_pin, 0); /* 先回 reset */
        IOSleep(1);
    }
    if (gUp.mclk_pin_taken) { pinRestore(gUp.mclk_pin); gUp.mclk_pin_taken = 0; }
    if (gCamcc.va && camccReadable()) {
        mclkRestore();
        gdscRestore();
        branchOff(&gCamcc, FC_GDSC_CLK, &gUp.gdsc_clk_switched);
    }
    branchOff(&gGcc, FC_GCC_CAMERA_XO, &gUp.xo_switched);
    branchOff(&gGcc, FC_GCC_CAMERA_AHB, &gUp.ahb_switched);
    if (gUp.avdd2_taken) {
        pinSetIo(gUp.avdd2_pin, 0);
        IOSleep(1);
        pinRestore(gUp.avdd2_pin);
        gUp.avdd2_taken = 0;
    }
    if (gUp.avdd_taken) {
        pinSetIo(gUp.avdd_pin, 0);
        IOSleep(1);
        pinRestore(gUp.avdd_pin);
        gUp.avdd_taken = 0;
    }
    for (int i = (int)gRailCount - 1; i >= 0; --i) {
        if (!gRails[i].on) continue;
        (void)voteRail(&gRails[i], 0);
        IOSleep(2);
    }
    if (gUp.reset_taken) { pinRestore(gUp.reset_pin); gUp.reset_taken = 0; }
    pinRestore(hw->pin_sda);
    pinRestore(hw->pin_scl);
    printf("FUXI CAMERA: [%s] powered down (gdsc=%d mclk=%d avdd=%d avdd2=%d)\n", hw->name, gUp.gdsc_switched,
        gUp.mclk_switched, gUp.avdd_taken, gUp.avdd2_taken);
}
static int powerUpSensor(const struct fc_sensor_hw *hw)
{
    /* reset 先拉低 1 ms（輸出 0、2 mA、不上下拉） */
    pinSetIo(hw->pin_reset, 0);
    pinSetCtl(hw->pin_reset, fc_pin_ctl(foundCtl(hw->pin_reset), 0, 2, FC_CTL_PULL_NONE, 1));
    gUp.reset_pin = hw->pin_reset;
    gUp.reset_taken = 1;
    IOSleep(1);
    for (unsigned i = 0; i < gRailCount; ++i) {
        if (strcmp(gRails[i].spec.role, "vdig") == 0) {
            if (hw->pin_avdd != FC_GPIO_NONE && !gUp.avdd_taken) {
                pinSetIo(hw->pin_avdd, FC_IO_OUT);
                pinSetCtl(hw->pin_avdd, fc_pin_ctl(foundCtl(hw->pin_avdd), 0, 2, FC_CTL_PULL_NONE, 1));
                gUp.avdd_pin = hw->pin_avdd;
                gUp.avdd_taken = 1;
                IOSleep(1);
            }
            if (hw->pin_avdd2 != FC_GPIO_NONE && !gUp.avdd2_taken) {
                pinSetIo(hw->pin_avdd2, FC_IO_OUT);
                pinSetCtl(hw->pin_avdd2, fc_pin_ctl(foundCtl(hw->pin_avdd2), 0, 2, FC_CTL_PULL_NONE, 1));
                gUp.avdd2_pin = hw->pin_avdd2;
                gUp.avdd2_taken = 1;
                IOSleep(1);
            }
        }
        if (voteRail(&gRails[i], 1)) return 0;
        IOSleep(2);
    }
    if (hw->pin_avdd != FC_GPIO_NONE && !gUp.avdd_taken) {
        pinSetIo(hw->pin_avdd, FC_IO_OUT);
        pinSetCtl(hw->pin_avdd, fc_pin_ctl(foundCtl(hw->pin_avdd), 0, 2, FC_CTL_PULL_NONE, 1));
        gUp.avdd_pin = hw->pin_avdd;
        gUp.avdd_taken = 1;
        IOSleep(1);
    }
    if (hw->pin_avdd2 != FC_GPIO_NONE && !gUp.avdd2_taken) {
        pinSetIo(hw->pin_avdd2, FC_IO_OUT);
        pinSetCtl(hw->pin_avdd2, fc_pin_ctl(foundCtl(hw->pin_avdd2), 0, 2, FC_CTL_PULL_NONE, 1));
        gUp.avdd2_pin = hw->pin_avdd2;
        gUp.avdd2_taken = 1;
        IOSleep(1);
    }
    if (!branchOn(&gGcc, FC_GCC_CAMERA_AHB, "gcc camera ahb", &gUp.ahb_switched) ||
        !branchOn(&gGcc, FC_GCC_CAMERA_XO, "gcc camera xo", &gUp.xo_switched))
        return 0;
    if (!branchOn(&gCamcc, FC_GDSC_CLK, "camcc gdsc_clk", &gUp.gdsc_clk_switched) || !gdscOn() || !mclkOnFor(hw)) return 0;
    pinSetCtl(hw->pin_mclk, fc_pin_ctl(foundCtl(hw->pin_mclk), hw->mclk_func, hw->mclk_ma, FC_CTL_PULL_NONE, 0));
    gUp.mclk_pin = hw->pin_mclk;
    gUp.mclk_pin_taken = 1;
    IOSleep(4);
    pinSetIo(hw->pin_reset, FC_IO_OUT); /* reset 放開 */
    IOSleep(20);
    return 1;
}
static int powerUpAndScan(const struct fc_sensor_hw *hw, unsigned mode)
{
    int result = -1;
    if (!readRailsFor(hw)) {
        printf("FUXI CAMERA: [%s] a camera rail is missing from cmd-db; not powered\n", hw->name);
        return -2;
    }
    if (fc_map(&gRsc) != KERN_SUCCESS) return -3;
    gLayout = qcom_rsc_layout(rd(&gRsc, 0));
    if (!powerUpSensor(hw)) goto down;
    reportCamcc("powered", hw);
    uint8_t acks[FC_MAX_ACKS];
    unsigned count = 0;
    const int swap_first = adtU32("fuxi,camera-swap", 0) != 0;
    const unsigned first_sda = swap_first ? hw->pin_scl : hw->pin_sda;
    const unsigned first_scl = swap_first ? hw->pin_sda : hw->pin_scl;
    unsigned active_sda = first_sda, active_scl = first_scl;
    int found = scan(hw, first_sda, first_scl, mode, acks, &count);
    pinRestore(hw->pin_sda);
    pinRestore(hw->pin_scl);
    if (found == 0) {
        found = scan(hw, first_scl, first_sda, mode, acks, &count);
        if (found > 0) { active_sda = first_scl; active_scl = first_sda; }
        pinRestore(hw->pin_sda);
        pinRestore(hw->pin_scl);
    }
    if (mode >= 4) camssProbe(hw, adtU32("fuxi,camera-pll", 0) != 0, mode, adtU32("fuxi,camera-mmnoc", 0) != 0, active_sda, active_scl);
    result = found;
down:
    powerDownSensor(hw);
    return result;
}

/* ---- C4（模式 7/8）：/dev/fuxi-camera 字元裝置、三槽連續串流與即時 Group Hold 曝光／多鏡頭切換 ----
 * 參考：HakiOS hakios.camera.c（APSL 2.0）cdevsw／三槽環形緩衝／reader drain／1.5s 閒置關電；
 * ABI 相容 redfin redwood_camera.h（120 B frame header + 24 B command）。 */
static LCK_GRP_DECLARE(gC4LckGrp, "fuxi-camera-c4");
static LCK_MTX_DECLARE(gC4Lock, &gC4LckGrp);
static int gC4DevMade, gC4Broken, gC4Open, gC4Wanted, gC4Streaming, gC4SwitchWanted;
static unsigned gC4Readers;
static uint64_t gC4LastRead, gC4FramesDone, gC4Given, gC4Late;
static struct fc_ring_slot gC4Slots[FC_RING_SLOTS];
static vm_offset_t gC4BufVa[FC_RING_SLOTS];
static uint64_t gC4BufPa[FC_RING_SLOTS];
static uint32_t gC4BufAllocBytes;

static void c4FreeRingBuffers(void)
{
    for (unsigned slot = 0u; slot < FC_RING_SLOTS; ++slot) {
        if (gC4BufVa[slot] && gC4BufAllocBytes != 0u) {
            kmem_free(kernel_map, gC4BufVa[slot], gC4BufAllocBytes);
        }
        gC4BufVa[slot] = 0;
        gC4BufPa[slot] = 0;
    }
    gC4BufAllocBytes = 0u;
}
static uint32_t gC4SensorId = FC_SENSOR_FRONT;
static uint32_t gC4TabSensorId;
static uint32_t gC4NextSensorId = FC_SENSOR_FRONT;
static uint32_t gC4Mode = FC_MODE_PHOTO;
static uint32_t gC4TabMode = FC_MODE_PHOTO;
static uint32_t gC4NextMode = FC_MODE_PHOTO;
static uint32_t gC4VtsMin = FC_OV32C_VTS_30FPS;
static uint32_t gC4ActiveHeight = 2448u;
static uint32_t gC4FpsMilli = FC_OV32C_FPS_30_MILLI;
static int gC4FlashActive;
static uint32_t gC4FlashLevel;
static uint64_t gC4FlashUntilMs;
static uint32_t gC4ModeChangedPending;
static const uint8_t *gC4Tab;
static struct fc_tab_header gC4Hdr;
static uint32_t gC4Stride, gC4FrameBytes, gC4AllocBytes;
static int gC4TabLoaded;
static struct fc_ov32c_exposure gC4Ask = {2400u, 0x0400u, FC_OV32C_VTS_30FPS};
static int gC4AskPending;
static uint32_t gC4SlotFocus[FC_RING_SLOTS];
static uint32_t gC4SlotFlags[FC_RING_SLOTS];
static uint32_t gC4FocusCur = FUXI_CAMERA_NO_FOCUS;
static uint32_t gC4FocusAsk = FC_AK7314_DEFAULT_INIT_POS;
static int gC4FocusPending;
static int gC4ActuatorOk;
static int gC4OisOk;
static uint8_t gC4OisIs24618;
static uint64_t gC4LensMovingUntilMs;

static uint64_t c4NowMs(void)
{
    uint64_t ns = 0;
    absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
    return ns / 1000000ull;
}

static void c4ProbeAndInitActuator(struct fc_bus *bus, const struct fc_sensor_hw *hw, uint32_t init_pos,
    uint32_t *pos_out, int *ok_out)
{
    *ok_out = 0;
    *pos_out = FUXI_CAMERA_NO_FOCUS;
    if (!hw || !hw->actuator_slave7) return;
    if (fc_i2c_probe(bus, hw->actuator_slave7) <= 0) {
        printf("FUXI CAMERA: [%s] actuator %#x no ACK (no focus)\n", hw->name, hw->actuator_slave7);
        return;
    }
    if (fc_i2c_write8_u8(bus, hw->actuator_slave7, 0x02u, 0x00u) != FC_I2C_OK) {
        printf("FUXI CAMERA: [%s] AK7314 wake failed\n", hw->name);
        return;
    }
    IOSleep(3);
    const uint32_t target = init_pos > FUXI_CAMERA_FOCUS_MAX ? FUXI_CAMERA_FOCUS_MAX : init_pos;
    if (fc_i2c_write8_u16(bus, hw->actuator_slave7, 0x00u, fc_ak7314_encode_pos(target)) != FC_I2C_OK) {
        printf("FUXI CAMERA: [%s] AK7314 initial move failed\n", hw->name);
        return;
    }
    IOSleep(FC_AK7314_SETTLE_MS);
    uint16_t raw = 0u;
    if (fc_i2c_read8_u16(bus, hw->actuator_slave7, 0x84u, &raw) == FC_I2C_OK) {
        *pos_out = fc_ak7314_decode_pos(raw);
    } else {
        *pos_out = target;
    }
    *ok_out = 1;
    printf("FUXI CAMERA: [%s] AK7314 ready (%#x, pos=%u)\n", hw->name, hw->actuator_slave7, *pos_out);
}

static void c4MoveActuator(struct fc_bus *bus, const struct fc_sensor_hw *hw, uint32_t target,
    uint32_t *cur_out, uint64_t *moving_until_ms)
{
    if (!hw || !hw->actuator_slave7) return;
    const uint32_t clamped = target > FUXI_CAMERA_FOCUS_MAX ? FUXI_CAMERA_FOCUS_MAX : target;
    if (fc_i2c_write8_u16(bus, hw->actuator_slave7, 0x00u, fc_ak7314_encode_pos(clamped)) == FC_I2C_OK) {
        *cur_out = clamped;
        *moving_until_ms = c4NowMs() + 15ull;
    }
}

static void c4ParkAndSleepActuator(struct fc_bus *bus, const struct fc_sensor_hw *hw, uint32_t cur_pos)
{
    if (!hw || !hw->actuator_slave7) return;
    uint32_t p = cur_pos > FUXI_CAMERA_FOCUS_MAX ? 0u : cur_pos;
    for (unsigned step = 0u; step < 3u && p > 0u; ++step) {
        p = (step == 2u) ? 0u : (p / 2u);
        (void)fc_i2c_write8_u16(bus, hw->actuator_slave7, 0x00u, fc_ak7314_encode_pos(p));
        IOSleep(2);
    }
    (void)fc_i2c_write8_u8(bus, hw->actuator_slave7, 0x02u, 0x40u);
}

static int c4OisWriteAndPoll8(struct fc_bus *bus, uint8_t slave7, uint16_t reg, uint8_t val, uint16_t poll_reg,
    unsigned tries, unsigned wait_us)
{
    int rc = fc_i2c_write16_u8(bus, slave7, reg, val);
    if (rc != FC_I2C_OK) return rc;
    return fc_i2c_poll16_u8(bus, slave7, poll_reg, 0x01u, tries, wait_us);
}

static int c4OisWriteAndPoll16(struct fc_bus *bus, uint8_t slave7, uint16_t reg, uint16_t val, uint16_t poll_reg,
    unsigned tries, unsigned wait_us)
{
    int rc = fc_i2c_write16_u16(bus, slave7, reg, val);
    if (rc != FC_I2C_OK) return rc;
    return fc_i2c_poll16_u8(bus, slave7, poll_reg, 0x01u, tries, wait_us);
}

static void c4ProbeAndInitOis(struct fc_bus *bus, const struct fc_sensor_hw *hw, int *ok_out, uint8_t *is_24618_out)
{
    *ok_out = 0;
    *is_24618_out = 0u;
    if (!hw || !hw->ois_slave7) return;
    const uint8_t s7 = hw->ois_slave7;
    if (fc_i2c_probe(bus, s7) <= 0) {
        printf("FUXI CAMERA: [%s] OIS %#x no ACK (degraded, streaming continues)\n", hw->name, s7);
        return;
    }
    int is_24618 = 0;
    fc_ois_detect_24618(bus, s7, hw->sensor_id, &is_24618, 2000u);
    if (is_24618) {
        static const struct { uint16_t reg; uint16_t val; uint8_t dt; } k24618[] = {
            {0xf006u, 0x00u, 1u}, {0x614fu, 0x01u, 1u}, {0x6020u, 0x01u, 1u}, {0x6021u, 0x6bu, 1u},
            {0x618eu, 0x79u, 1u}, {0x6025u, 0x90u, 1u}, {0x617du, 0x16u, 1u}, {0x617eu, 0x0048u, 2u},
            {0x6020u, 0x01u, 1u},
        };
        for (unsigned i = 0u; i < (unsigned)(sizeof(k24618) / sizeof(k24618[0])); ++i) {
            const int rc = (k24618[i].dt == 2u)
                ? c4OisWriteAndPoll16(bus, s7, k24618[i].reg, k24618[i].val, 0x6024u, 40u, 2000u)
                : c4OisWriteAndPoll8(bus, s7, k24618[i].reg, (uint8_t)k24618[i].val, 0x6024u, 40u, 2000u);
            if (rc != FC_I2C_OK) {
                printf("FUXI CAMERA: [%s] BU24618 init step %u failed (%s); degraded\n", hw->name, i, rcName(rc));
                return;
            }
        }
        *ok_out = 1;
        *is_24618_out = 1u;
        printf("FUXI CAMERA: [%s] BU24618 OIS centering locked\n", hw->name);
        return;
    }

    static const struct { uint16_t reg; uint8_t val; uint8_t sleep_ms; } k24721Init8[] = {
        {0xf000u, 0x01u, 0u}, {0xff0fu, 0xa5u, 0u}, {0x8000u, 0xe5u, 40u},
        {0xa800u, 0xe9u, 2u}, {0xaa00u, 0xe9u, 2u},
        {0xac00u, 0xe0u, 2u}, {0xac80u, 0xe0u, 2u}, {0xad00u, 0xe0u, 2u}, {0xbf80u, 0xe0u, 2u},
    };
    for (unsigned i = 0u; i < (unsigned)(sizeof(k24721Init8) / sizeof(k24721Init8[0])); ++i) {
        const int rc = fc_i2c_write16_u8(bus, s7, k24721Init8[i].reg, k24721Init8[i].val);
        if (rc != FC_I2C_OK) {
            printf("FUXI CAMERA: [%s] BU24721 init8[%u] failed (%s); degraded\n", hw->name, i, rcName(rc));
            return;
        }
        if (k24721Init8[i].sleep_ms) IOSleep(k24721Init8[i].sleep_ms);
    }
    const struct { uint16_t reg; uint32_t val; } k24721Init32[] = {
        {0x3f80u, 0x00000015u}, {0x3f81u, 0x4244581cu}, {0x3f82u, 0xa50000dbu},
        {0x3f83u, 0x000063e1u}, {0x3f84u, 0x03030000u},
        {0x3ffbu, 0x00021108u}, {0x3ffcu, 0x0e007c00u}, {0x3ffdu, 0x00001000u},
        {0x3ffeu, 0x00000000u}, {0x3fffu, hw->sensor_id == FC_SENSOR_WIDE ? 0x40299869u : 0x40299a27u},
    };
    for (unsigned i = 0u; i < (unsigned)(sizeof(k24721Init32) / sizeof(k24721Init32[0])); ++i) {
        const int rc = fc_i2c_write16_u32(bus, s7, k24721Init32[i].reg, k24721Init32[i].val);
        if (rc != FC_I2C_OK) {
            printf("FUXI CAMERA: [%s] BU24721 init32[%u] failed (%s); degraded\n", hw->name, i, rcName(rc));
            return;
        }
    }
    const uint16_t fw_first = (hw->sensor_id == FC_SENSOR_WIDE) ? 0x3666u : 0x4000u;
    if (c4OisWriteAndPoll16(bus, s7, 0xf1cau, fw_first, 0xf024u, 30u, 1000u) != FC_I2C_OK ||
        c4OisWriteAndPoll16(bus, s7, 0xf1ccu, 0x4000u, 0xf024u, 30u, 1000u) != FC_I2C_OK ||
        c4OisWriteAndPoll8(bus, s7, 0xf020u, 0x01u, 0xf024u, 45u, 2000u) != FC_I2C_OK ||
        c4OisWriteAndPoll8(bus, s7, 0xf17cu, 0x01u, 0xf024u, 30u, 2000u) != FC_I2C_OK ||
        c4OisWriteAndPoll8(bus, s7, 0xf12bu, 0x02u, 0xf024u, 30u, 2000u) != FC_I2C_OK ||
        c4OisWriteAndPoll8(bus, s7, 0xf02au, 0x04u, 0xf024u, 30u, 2000u) != FC_I2C_OK ||
        c4OisWriteAndPoll8(bus, s7, 0xf023u, 0x00u, 0xf024u, 30u, 2000u) != FC_I2C_OK ||
        c4OisWriteAndPoll8(bus, s7, 0xf021u, 0x6bu, 0xf024u, 30u, 2000u) != FC_I2C_OK ||
        c4OisWriteAndPoll8(bus, s7, 0xf18eu, 0x79u, 0xf024u, 30u, 2000u) != FC_I2C_OK ||
        c4OisWriteAndPoll8(bus, s7, 0xf025u, 0x90u, 0xf024u, 30u, 2000u) != FC_I2C_OK ||
        (hw->sensor_id == FC_SENSOR_WIDE && c4OisWriteAndPoll8(bus, s7, 0xf0aau, 0x0fu, 0xf024u, 20u, 1000u) != FC_I2C_OK) ||
        c4OisWriteAndPoll8(bus, s7, 0xf17eu, 0x01u, 0xf024u, 20u, 1000u) != FC_I2C_OK ||
        c4OisWriteAndPoll16(bus, s7, 0xf174u, 0x0000u, 0xf024u, 20u, 1000u) != FC_I2C_OK ||
        c4OisWriteAndPoll16(bus, s7, 0xf176u, 0x0000u, 0xf024u, 20u, 1000u) != FC_I2C_OK ||
        c4OisWriteAndPoll8(bus, s7, 0xf020u, 0x01u, 0xf024u, 30u, 1000u) != FC_I2C_OK) {
        printf("FUXI CAMERA: [%s] BU24721 centering poll failed; degraded\n", hw->name);
        return;
    }
    *ok_out = 1;
    printf("FUXI CAMERA: [%s] BU24721 OIS centering locked\n", hw->name);
}

static void c4ReleaseOis(struct fc_bus *bus, const struct fc_sensor_hw *hw, int ois_ok, uint8_t is_24618)
{
    if (!hw) return;
    (void)fc_ois_release(bus, hw->ois_slave7, ois_ok, is_24618, 1000u);
}

static int c4LoadTabForMode(uint32_t sensor_id, uint32_t mode, const uint8_t **tab_out, struct fc_tab_header *hdr_out)
{
    const struct fc_sensor_hw *hw = fc_sensor_hw_for(sensor_id);
    if (!hw) return 0;
    DTEntry chosen;
    if (SecureDTLookupEntry(NULL, "/chosen", &chosen) != kSuccess) {
        printf("FUXI CAMERA: c4 [%s] ADT /chosen missing\n", hw->name);
        return 0;
    }
    const char *prop_name = hw->adt_tab_prop;
    const void *prop = NULL;
    unsigned prop_bytes = 0u;
    if (sensor_id == FC_SENSOR_WIDE && mode == FC_MODE_VIDEO60) {
        const void *p4k = NULL;
        unsigned b4k = 0u;
        if (SecureDTGetProperty(chosen, "fuxi,camera-tab-wide-4k60", &p4k, &b4k) == kSuccess && p4k != NULL) {
            prop_name = "fuxi,camera-tab-wide-4k60";
            prop = p4k;
            prop_bytes = b4k;
        }
    } else if (sensor_id == FC_SENSOR_TELE && mode == FC_MODE_VIDEO60) {
        const void *pt60 = NULL;
        unsigned bt60 = 0u;
        if (SecureDTGetProperty(chosen, "fuxi,camera-tab-tele-60", &pt60, &bt60) == kSuccess && pt60 != NULL) {
            prop_name = "fuxi,camera-tab-tele-60";
            prop = pt60;
            prop_bytes = bt60;
        }
    }
    if (!prop && (SecureDTGetProperty(chosen, prop_name, &prop, &prop_bytes) != kSuccess || !prop)) {
        printf("FUXI CAMERA: c4 [%s] ADT %s missing\n", hw->name, prop_name);
        return 0;
    }
    struct fc_tab_header hdr;
    uint8_t tab_sid = 0u;
    const int rc = fc_tab_parse_any((const uint8_t *)prop, prop_bytes, &hdr, &tab_sid);
    if (rc != 0 || (tab_sid != 0u && tab_sid != sensor_id)) {
        printf("FUXI CAMERA: c4 [%s] invalid %s (%u B, rc=%d sid=%u)\n", hw->name, prop_name, prop_bytes, rc, tab_sid);
        return 0;
    }
    if (tab_out) *tab_out = (const uint8_t *)prop;
    if (hdr_out) *hdr_out = hdr;
    return 1;
}

static int c4LoadTabFor(uint32_t sensor_id, const uint8_t **tab_out, struct fc_tab_header *hdr_out)
{
    return c4LoadTabForMode(sensor_id, FC_MODE_PHOTO, tab_out, hdr_out);
}

static int c4ActivateTabMode(uint32_t sensor_id, uint32_t mode)
{
    const struct fc_sensor_hw *hw = fc_sensor_hw_for(sensor_id);
    if (!hw) return 0;
    if (gC4TabLoaded && gC4TabSensorId == sensor_id && gC4TabMode == mode) return 1;
    const uint8_t *tab = NULL;
    struct fc_tab_header hdr;
    if (!c4LoadTabForMode(sensor_id, mode, &tab, &hdr)) return 0;
    const uint32_t raw_floor = fc_sensor_vts_floor(hw, mode);
    const uint32_t vts_floor = fc_sensor_vts_floor_for_height(hw, mode, hdr.height);
    if (vts_floor != raw_floor) {
        printf("FUXI CAMERA: c4 [%s] mode=%u vts_floor %u < height(%u)+margin(%u); clamped to %u\n",
            hw->name, mode, raw_floor, hdr.height, hw->margin, vts_floor);
    }
    const uint32_t fps_milli = fc_sensor_fps_milli(hw->line_ns, vts_floor);
    gC4Tab = tab;
    gC4Hdr = hdr;
    gC4Stride = fc_vfe_wm_line_stride(fc_raw10_bytes_per_line(hdr.width));
    gC4FrameBytes = gC4Stride * hdr.height;
    gC4AllocBytes = fc_vfe_wm_alloc_bytes(gC4FrameBytes, (uint32_t)PAGE_MASK);
    gC4TabLoaded = 1;
    gC4TabSensorId = sensor_id;
    gC4TabMode = mode;
    lck_mtx_lock(&gC4Lock);
    gC4SensorId = sensor_id;
    gC4Mode = mode;
    gC4VtsMin = vts_floor;
    gC4ActiveHeight = hdr.height;
    gC4FpsMilli = fps_milli;
    gC4ModeChangedPending = 1u;
    lck_mtx_unlock(&gC4Lock);
    printf("FUXI CAMERA: c4 [%s] tab ready v%u mode=%u slave=%#x %ux%u@%u.%ufps stride=%u frame=%u B (alloc=%u B, regs=%u)\n",
        hw->name, hdr.version, mode, hdr.slave7, hdr.width, hdr.height, fps_milli / 1000u, (fps_milli % 1000u) / 100u,
        gC4Stride, gC4FrameBytes, gC4AllocBytes, hdr.count);
    return 1;
}

static void c4DrainReaders(void)
{
    lck_mtx_lock(&gC4Lock);
    while (gC4Readers != 0u) {
        (void)msleep(&gC4Readers, &gC4Lock, PZERO, "fuxi-cam-drain", NULL);
    }
    lck_mtx_unlock(&gC4Lock);
}

static int c4CheckSmmuFault(const struct fc_smmu_ctx *smmu, uint32_t *gfsr_out, uint32_t *cb4_out)
{
    if (!smmu->win.va) return 0;
    const uint32_t gfsr = rd(&smmu->win, 0x48u);
    const uint32_t cb4 = smmu->bank_base + smmu->stride * FC_SMMU_BYPASS_BANK;
    const uint32_t cb4_fsr = rd(&smmu->win, cb4 + 0x58u);
    if (gfsr_out) *gfsr_out = gfsr;
    if (cb4_out) *cb4_out = cb4_fsr;
    return fc_smmu_fsr_fault(cb4_fsr);
}

static int c4WakeSensor(const struct fc_sensor_hw *hw, unsigned sda, unsigned scl, uint8_t slave7)
{
    struct fc_bus bus = {&kBusOps, NULL, sda, scl, 5u, 200u, 0u, 0u, 0u};
    pinSetIo(sda, 0u);
    pinSetIo(scl, 0u);
    busLine(NULL, sda, 1);
    busLine(NULL, scl, 1);
    delay(50);
    if (!fc_i2c_idle(&bus)) return 0;
    if (fc_i2c_probe(&bus, slave7) == 1) return 1;
    if (hw->wake_aon_slave7 != 0u && fc_i2c_probe(&bus, hw->wake_aon_slave7) == 1) {
        const int w0 = fc_i2c_write16_u8(&bus, hw->wake_aon_slave7, 0x1000u, 0x00u);
        const int w1 = w0 == FC_I2C_OK ? fc_i2c_write16_u8(&bus, hw->wake_aon_slave7, 0x1001u, 0x04u) : w0;
        if (w0 == FC_I2C_OK && w1 == FC_I2C_OK) {
            IOSleep(5);
            if (fc_i2c_probe(&bus, slave7) == 1) return 1;
        }
    }
    return 0;
}

static int c4SessionRun(uint32_t sensor_id, uint32_t mode, int selftest, unsigned selftest_frames)
{
    const struct fc_sensor_hw *hw = fc_sensor_hw_for(sensor_id);
    if (!hw) return -1;
    if (!c4ActivateTabMode(sensor_id, mode)) return -1;
    if (!readRailsFor(hw)) {
        printf("FUXI CAMERA: c4 [%s] missing camera rail from cmd-db\n", hw->name);
        return -2;
    }
    if (fc_map(&gRsc) != KERN_SUCCESS) return -3;
    gLayout = qcom_rsc_layout(rd(&gRsc, 0));

    const int use_pll = adtU32("fuxi,camera-pll", 0) != 0;
    const int use_mmnoc = adtU32("fuxi,camera-mmnoc", 0) != 0;
    const int swap_first = adtU32("fuxi,camera-swap", 0) != 0;
    const unsigned first_sda = swap_first ? hw->pin_scl : hw->pin_sda;
    const unsigned first_scl = swap_first ? hw->pin_sda : hw->pin_scl;
    unsigned active_sda = first_sda, active_scl = first_scl;

    int rc = -1;
    int pll_enabled = 0, mm0_voted = 0, streaming_started = 0, smmu_ok = 0;
    int br_sw[13], br_ok[13];
    bzero(br_sw, sizeof(br_sw));
    bzero(br_ok, sizeof(br_ok));
    struct { int saved; uint32_t cfg, m_val, n_val, d_val; } rcg_save[7];
    bzero(rcg_save, sizeof(rcg_save));
    uint32_t pll_mode0 = 0u;
    struct fc_window wLite = {FC_LITE_PAGE, FC_LITE_SIZE, 0};
    struct fc_window wPhy = {hw->phy_page, 0x4000u, 0};
    const uint32_t po = hw->phy_off;
    const int is_cphy = hw->is_cphy || ((gC4Hdr.flags & FC_TAB_FLAG_CPHY) != 0u);
    uint32_t focus_cur = FUXI_CAMERA_NO_FOCUS;
    int actuator_ok = 0, ois_ok = 0;
    uint8_t ois_24618 = 0u;
    uint64_t lens_moving_until = 0ull;
    struct fc_smmu_ctx smmu;
    bzero(&smmu, sizeof(smmu));

    const uint8_t pipe_hid = fc_sensor_needs_480mhz(gC4Hdr.width, gC4Hdr.height, gC4FpsMilli) ? 4u : 5u;
    if (use_pll && pipe_hid == 4u) {
        printf("FUXI CAMERA: c4 [%s] high-bandwidth mode %ux%u@%u.%ufps -> camcc 480 MHz (hid=4)\n",
            hw->name, gC4Hdr.width, gC4Hdr.height, gC4FpsMilli / 1000u, (gC4FpsMilli % 1000u) / 100u);
    }
    const struct { const char *name; uint32_t cmd; uint8_t has_mnd, pll_src, pll_hid; } kRcgs[7] = {
        {"slow_ahb", FC_SLOW_AHB_RCG, 1u, 2u, 14u},
        {"fast_ahb", FC_FAST_AHB_RCG, 0u, 2u, 3u},
        {"camnoc_axi", FC_CAMNOC_AXI_RCG, 0u, 2u, 3u},
        {"cphy_rx", FC_CPHY_RX_RCG, 0u, 1u, pipe_hid},
        {"phytimer", hw->phytimer_rcg, 0u, hw->phytimer_pll_src, hw->phytimer_pll_hid},
        {"ife_lite", FC_IFE_LITE_RCG, 0u, 1u, pipe_hid},
        {"ife_lite_csid", FC_IFE_LITE_CSID_RCG, 0u, 1u, pipe_hid},
    };
    const struct { const struct fc_window *win; uint32_t reg; const char *name; } branches[13] = {
        {&gGcc, FC_GCC_CAMERA_HF_AXI, "gcc hf_axi"},
        {&gGcc, FC_GCC_CAMERA_SF_AXI, "gcc sf_axi"},
        {&gCamcc, FC_CPAS_FAST_AHB, "camcc cpas_fast_ahb"},
        {&gCamcc, FC_CPAS_AHB, "camcc cpas_ahb"},
        {&gCamcc, FC_CAMNOC_AXI_CBCR, "camcc camnoc_axi"},
        {&gCamcc, FC_CPAS_IFE_LITE_CBCR, "camcc cpas_ife_lite"},
        {&gCamcc, FC_IFE_LITE_AHB_CBCR, "camcc ife_lite_ahb"},
        {&gCamcc, FC_IFE_LITE_CBCR, "camcc ife_lite"},
        {&gCamcc, FC_IFE_LITE_CPHY_RX_CBCR, "camcc ife_lite_cphy_rx"},
        {&gCamcc, FC_IFE_LITE_CSID_CBCR, "camcc ife_lite_csid"},
        {&gCamcc, hw->csiphy_cbcr, "camcc csiphy"},
        {&gCamcc, hw->phytimer_cbcr, "camcc phytimer"},
        {&gCamcc, FC_CSID_CSIPHY_RX_CBCR, "camcc csid_csiphy_rx"},
    };

    if (gC4BufAllocBytes < gC4AllocBytes || !gC4BufVa[0]) {
        c4FreeRingBuffers();
        gC4BufAllocBytes = gC4AllocBytes;
        for (unsigned slot = 0u; slot < FC_RING_SLOTS; ++slot) {
            if (allocContigBuffer(gC4AllocBytes, &gC4BufVa[slot], &gC4BufPa[slot]) != 0) {
                printf("FUXI CAMERA: c4 DMA slot %u alloc %u B failed\n", slot, gC4AllocBytes);
                c4FreeRingBuffers();
                rc = -4;
                goto teardown;
            }
        }
    }
    for (unsigned slot = 0u; slot < FC_RING_SLOTS; ++slot) {
        memset((void *)gC4BufVa[slot], FC_TPG_CANARY, gC4AllocBytes);
        dcacheCivac(gC4BufVa[slot], gC4AllocBytes);
    }

    if (!powerUpSensor(hw)) {
        rc = -5;
        goto teardown;
    }

    if (!c4WakeSensor(hw, first_sda, first_scl, gC4Hdr.slave7)) {
        pinRestore(hw->pin_sda);
        pinRestore(hw->pin_scl);
        if (!c4WakeSensor(hw, first_scl, first_sda, gC4Hdr.slave7)) {
            printf("FUXI CAMERA: c4 [%s] wake failed\n", hw->name);
            rc = -8;
            goto teardown;
        }
        active_sda = first_scl;
        active_scl = first_sda;
    }

    pll_mode0 = rd(&gCamcc, FC_PLL_MODE);
    const uint32_t pll_op0 = rd(&gCamcc, FC_PLL_OPMODE);
    if (use_pll && !fc_pll_is_enabled(pll_mode0, pll_op0)) {
        wr(&gCamcc, FC_PLL_L_VAL, FC_PLL0_L_VAL);
        wr(&gCamcc, FC_PLL_ALPHA, FC_PLL0_ALPHA);
        wr(&gCamcc, FC_PLL_CONFIG_CTL, FC_PLL0_CONFIG_CTL);
        wr(&gCamcc, FC_PLL_CONFIG_CTL_U, FC_PLL0_CONFIG_CTL_U);
        wr(&gCamcc, FC_PLL_CONFIG_CTL_U1, FC_PLL0_CONFIG_CTL_U1);
        wr(&gCamcc, FC_PLL_USER_CTL, FC_PLL0_USER_CTL);
        wr(&gCamcc, FC_PLL_USER_CTL_U, FC_PLL0_USER_CTL_U);
        wr(&gCamcc, FC_PLL_TEST_CTL, FC_PLL0_TEST_CTL);
        wr(&gCamcc, FC_PLL_TEST_CTL_U, FC_PLL0_TEST_CTL_U);
        wr(&gCamcc, FC_PLL_TEST_CTL_U1, FC_PLL0_TEST_CTL_U1);
        wr(&gCamcc, FC_PLL_TEST_CTL_U2, FC_PLL0_TEST_CTL_U2);
        wr(&gCamcc, FC_PLL_MODE, rd(&gCamcc, FC_PLL_MODE) & ~FC_PLL_OUTCTRL);
        wr(&gCamcc, FC_PLL_OPMODE, FC_PLL_OP_STANDBY);
        wr(&gCamcc, FC_PLL_MODE, rd(&gCamcc, FC_PLL_MODE) | FC_PLL_RESET_N);
        wr(&gCamcc, FC_PLL_OPMODE, FC_PLL_OP_RUN);
        if (!waitBits(&gCamcc, FC_PLL_MODE, FC_PLL_LOCK_DET, FC_PLL_LOCK_DET, 2000)) {
            printf("FUXI CAMERA: c4 camcc pll0 FAILED to lock\n");
            wr(&gCamcc, FC_PLL_OPMODE, FC_PLL_OP_STANDBY);
            if (!(pll_mode0 & FC_PLL_RESET_N)) wr(&gCamcc, FC_PLL_MODE, rd(&gCamcc, FC_PLL_MODE) & ~FC_PLL_RESET_N);
            rc = -9;
            goto teardown;
        }
        wr(&gCamcc, FC_PLL_USER_CTL, rd(&gCamcc, FC_PLL_USER_CTL) | FC_PLL_OUT_MASK);
        wr(&gCamcc, FC_PLL_MODE, rd(&gCamcc, FC_PLL_MODE) | FC_PLL_OUTCTRL);
        pll_enabled = 1;
    }
    for (unsigned idx = 0u; idx < 7u; ++idx) {
        const uint32_t base = kRcgs[idx].cmd;
        const uint32_t found_cmd = rd(&gCamcc, base);
        rcg_save[idx].cfg = rd(&gCamcc, base + 4u);
        if (kRcgs[idx].has_mnd) {
            rcg_save[idx].m_val = rd(&gCamcc, base + 8u);
            rcg_save[idx].n_val = rd(&gCamcc, base + 0xcu);
            rcg_save[idx].d_val = rd(&gCamcc, base + 0x10u);
            wr(&gCamcc, base + 8u, 0u);
            wr(&gCamcc, base + 0xcu, 0u);
            wr(&gCamcc, base + 0x10u, 0u);
        }
        rcg_save[idx].saved = 1;
        wr(&gCamcc, base + 4u, fc_rcg_cfg(rcg_save[idx].cfg, use_pll ? kRcgs[idx].pll_src : 0u, use_pll ? kRcgs[idx].pll_hid : 1u));
        wr(&gCamcc, base, found_cmd | FC_RCG_CMD_UPDATE);
        if (!waitBits(&gCamcc, base, FC_RCG_CMD_UPDATE, 0u, 10000u)) {
            printf("FUXI CAMERA: c4 rcg %s failed\n", kRcgs[idx].name);
            rc = -10;
            goto teardown;
        }
    }
    if (use_mmnoc && gMm0Address) mm0_voted = voteBcm("MM0", gMm0Address, 1) == 0;
    for (unsigned idx = 0u; idx < 13u; ++idx) {
        br_ok[idx] = branchOn(branches[idx].win, branches[idx].reg, branches[idx].name, &br_sw[idx]);
        if (!br_ok[idx]) { rc = -11; goto teardown; }
    }
    if (fc_map(&wLite) != KERN_SUCCESS || fc_map(&wPhy) != KERN_SUCCESS) { rc = -12; goto teardown; }
    if (!smmuAttachIfe(&smmu)) { rc = -13; goto teardown; }
    smmu_ok = 1;

    const uint32_t csid = FC_CSID_LITE0_OFF;
    const uint32_t vfe = FC_VFE_LITE0_OFF;
    csidClearAllIrqs(&wLite);
    wr(&wLite, csid + FC_CSID_TOP_IRQ_MASK, FC_CSID_TOP_RESET_DONE);
    wr(&wLite, csid + FC_CSID_BUF_DONE_IRQ_MASK, 0x1fu);
    wr(&wLite, csid + FC_CSID_RX_IRQ_MASK, 0xffffffffu);
    wr(&wLite, csid + FC_CSID_RDI0_IRQ_MASK, 0xffffffffu);
    wr(&wLite, csid + FC_CSID_RST_CFG, 0x11u);
    wr(&wLite, csid + FC_CSID_RST_CMD, 0x5u);
    (void)waitBits(&wLite, csid + FC_CSID_TOP_IRQ_STATUS, FC_CSID_TOP_RESET_DONE, FC_CSID_TOP_RESET_DONE, 50000u);
    csidClearAllIrqs(&wLite);

    wr(&wLite, vfe + FC_VFE_BUS_CGC_OVERRIDE, 0x07ffffffu);
    wr(&wLite, vfe + FC_VFE_BUS_TEST_BUS_CTRL, 0u);
    wr(&wLite, vfe + FC_VFE_WM0_FRAME_INCR, fc_vfe_wm_frame_incr(gC4Stride, gC4Hdr.height));
    wr(&wLite, vfe + FC_VFE_WM0_IMAGE_CFG0, 0xffffu);
    wr(&wLite, vfe + FC_VFE_WM0_IMAGE_CFG2, 0xffffu);
    wr(&wLite, vfe + FC_VFE_WM0_PACKER_CFG, 0u);
    wr(&wLite, vfe + FC_VFE_WM0_FDROP_PERIOD, 0u);
    wr(&wLite, vfe + FC_VFE_WM0_FDROP_PATTERN, 1u);
    wr(&wLite, vfe + FC_VFE_WM0_IRQ_PERIOD, 0u);
    wr(&wLite, vfe + FC_VFE_WM0_IRQ_PATTERN, 1u);
    wr(&wLite, vfe + FC_VFE_WM0_MMU_PREFETCH_CFG, 1u);
    wr(&wLite, vfe + FC_VFE_WM0_MMU_PREFETCH_MAX, 0xffffffffu);
    wr(&wLite, vfe + FC_VFE_WM0_CFG, 0x10001u);
    wr(&wLite, vfe + FC_VFE_WM0_IMAGE_ADDR, fc_vfe_wm_addr(gC4BufPa[0]));
    wr(&wLite, csid + FC_CSID_RUP_AUP_CMD, FC_CSID_RUP_AUP_RDI0);

    const uint32_t rdi_cfg0 = fc_csid_rdi_cfg0(gC4Hdr.dt, 0u, 0u, 0);
    wr(&wLite, csid + FC_CSID_RDI0_CFG0, rdi_cfg0);
    wr(&wLite, csid + FC_CSID_RDI0_CFG1, fc_csid_rdi_cfg1());
    wr(&wLite, csid + FC_CSID_RDI0_SUBSAMPLE_PERIOD, 0u);
    wr(&wLite, csid + FC_CSID_RDI0_SUBSAMPLE_PATTERN, 1u);
    wr(&wLite, csid + FC_CSID_RDI0_CTRL, 0u);
    wr(&wLite, csid + FC_CSID_RDI0_CFG0, rdi_cfg0 | (1u << 31));
    wr(&wLite, csid + FC_CSID_RX_CFG0, fc_csid_rx_cfg0_phy(gC4Hdr.lanes, gC4Hdr.lane_assign, hw->csiphy_idx, is_cphy));
    wr(&wLite, csid + FC_CSID_RX_CFG1, 1u);
    wr(&wLite, csid + FC_CSID_RDI0_CTRL, 1u);
    wr(&wLite, csid + FC_CSID_RUP_AUP_CMD, FC_CSID_RUP_AUP_RDI0);

    (void)programCsiphy(&wPhy, po, &gC4Hdr, is_cphy);

    struct fc_bus bus = {&kBusOps, NULL, active_sda, active_scl, 5u, 200u, 0u, 0u, 0u};
    unsigned written = 0u;
    int first_err = FC_I2C_OK;
    uint16_t first_err_reg = 0u;
    const unsigned write_errors = runSensorTab(&bus, gC4Tab, &gC4Hdr, &written, &first_err, &first_err_reg);
    if (write_errors != 0u) {
        printf("FUXI CAMERA: c4 [%s] i2c table failed (%u/%u ok, err=%u first=%s@%#06x)\n",
            hw->name, written, gC4Hdr.count, write_errors, rcName(first_err), first_err_reg);
        rc = -14;
        goto teardown;
    }

    uint32_t init_focus_ask = FC_AK7314_DEFAULT_INIT_POS;
    lck_mtx_lock(&gC4Lock);
    if (gC4FocusPending) {
        init_focus_ask = gC4FocusAsk;
        gC4FocusPending = 0;
    }
    lck_mtx_unlock(&gC4Lock);
    c4ProbeAndInitActuator(&bus, hw, init_focus_ask, &focus_cur, &actuator_ok);
    c4ProbeAndInitOis(&bus, hw, &ois_ok, &ois_24618);

    struct fc_ov32c_exposure cur_exp, next_exp;
    const uint32_t vts_floor = fc_sensor_vts_floor_for_height(hw, mode, gC4Hdr.height);
    if (selftest) {
        (void)fc_sensor_clamp_exposure_for_height(hw->sensor_id, mode, gC4Hdr.height, hw->default_lines / 4u, hw->gain_unity * 2u, vts_floor, &cur_exp);
    } else {
        lck_mtx_lock(&gC4Lock);
        (void)fc_sensor_clamp_exposure_for_height(hw->sensor_id, mode, gC4Hdr.height, gC4Ask.lines, gC4Ask.gain, gC4Ask.frame_lines, &cur_exp);
        gC4AskPending = 0;
        lck_mtx_unlock(&gC4Lock);
    }
    next_exp = cur_exp;
    uint64_t next_exp_from = 0u;
    const int init_exp_rc = fc_sensor_apply_exposure(hw->sensor_id, &bus, gC4Hdr.slave7, &cur_exp);
    if (init_exp_rc != FC_I2C_OK) {
        printf("FUXI CAMERA: c4 [%s] initial exposure failed (%s)\n", hw->name, rcName(init_exp_rc));
        rc = -15;
        goto teardown;
    }

    lck_mtx_lock(&gC4Lock);
    for (unsigned s = 0u; s < FC_RING_SLOTS; ++s) {
        bzero(&gC4Slots[s], sizeof(gC4Slots[s]));
        gC4Slots[s].state = FC_SLOT_FREE;
        gC4SlotFocus[s] = focus_cur;
        gC4SlotFlags[s] = 0u;
    }
    gC4Slots[0].state = FC_SLOT_WRITING;
    gC4FocusCur = focus_cur;
    gC4ActuatorOk = actuator_ok;
    gC4OisOk = ois_ok;
    gC4OisIs24618 = ois_24618;
    gC4LensMovingUntilMs = 0ull;
    gC4FramesDone = 0u;
    gC4Given = 0u;
    gC4Late = 0u;
    gC4Streaming = 1;
    gC4LastRead = c4NowMs();
    lck_mtx_unlock(&gC4Lock);

    csidClearAllIrqs(&wLite);
    if (fc_sensor_stream_on(hw->sensor_id, &bus, gC4Hdr.slave7) != FC_I2C_OK) {
        printf("FUXI CAMERA: c4 [%s] sensor streamOn failed\n", hw->name);
        rc = -16;
        goto teardown;
    }
    streaming_started = 1;

    unsigned cur_slot = 0u;
    int prev_slot = -1;
    uint64_t seq = 0u;
    uint64_t last_frame_ms = c4NowMs();
    uint64_t sof_time = mach_absolute_time();
    unsigned smmu_faults = 0u;
    int live_exp_rc = FC_I2C_OK;
    uint32_t low_mean_sum = 0u, low_mean_cnt = 0u, high_mean_sum = 0u, high_mean_cnt = 0u;
    uint32_t rb_vts = 0u, rb_lines = 0u, rb_gain = 0u;
    const char *stop_why = selftest ? "selftest-done" : "closed";

    for (;;) {
        IOSleep(1);
        const uint32_t bd = rd(&wLite, csid + FC_CSID_BUF_DONE_IRQ_STATUS);
        const uint32_t r0 = rd(&wLite, csid + FC_CSID_RDI0_IRQ_STATUS);
        if (r0 & FC_CSID_RDI_RUP_DONE) sof_time = mach_absolute_time();
        const uint64_t now_ms = c4NowMs();

        if (bd & FC_CSID_BUF_DONE_RDI0) {
            csidClearAllIrqs(&wLite);
            const uint64_t now_abs = mach_absolute_time();
            const uint32_t frame_ms = (uint32_t)(now_ms - last_frame_ms);
            last_frame_ms = now_ms;
            seq++;
            if (next_exp_from != 0u && seq >= next_exp_from) {
                cur_exp = next_exp;
                next_exp_from = 0u;
            }

            int next_slot = -1;
            int do_exp = 0, do_focus = 0;
            uint32_t want_focus = 0u;
            struct fc_ov32c_exposure want_exp;
            bzero(&want_exp, sizeof(want_exp));

            lck_mtx_lock(&gC4Lock);
            gC4Slots[cur_slot].state = FC_SLOT_READY;
            gC4Slots[cur_slot].sequence = seq;
            gC4Slots[cur_slot].time = now_abs;
            gC4Slots[cur_slot].sof_time = sof_time;
            gC4Slots[cur_slot].exposure_lines = cur_exp.lines;
            gC4Slots[cur_slot].gain = cur_exp.gain;
            gC4Slots[cur_slot].frame_lines = cur_exp.frame_lines;
            gC4SlotFocus[cur_slot] = focus_cur;
            const int flash_lit = fc_flash_step_lit(&gC4FlashActive, &gC4FlashLevel, &gC4FlashUntilMs, now_ms);
            gC4SlotFlags[cur_slot] = ((lens_moving_until != 0ull && now_ms <= lens_moving_until)
                ? FUXI_CAMERA_FLAG_LENS_MOVING : 0u) |
                (flash_lit ? FUXI_CAMERA_FLAG_FLASH_LIT : 0u) |
                (gC4ModeChangedPending ? FUXI_CAMERA_FLAG_MODE_CHANGED : 0u);
            gC4ModeChangedPending = 0u;
            gC4FramesDone = seq;
            next_slot = fc_ring_pick_next_write(gC4Slots);
            if (next_slot < 0) {
                next_slot = (int)cur_slot;
                gC4Late++;
            }
            gC4Slots[next_slot].state = FC_SLOT_WRITING;
            if (!selftest && gC4AskPending) {
                want_exp = gC4Ask;
                gC4AskPending = 0;
                do_exp = 1;
            }
            if (!selftest && gC4FocusPending) {
                want_focus = gC4FocusAsk;
                gC4FocusPending = 0;
                do_focus = 1;
            }
            lck_mtx_unlock(&gC4Lock);

            wr(&wLite, vfe + FC_VFE_WM0_IMAGE_ADDR, fc_vfe_wm_addr(gC4BufPa[next_slot]));
            wr(&wLite, csid + FC_CSID_RUP_AUP_CMD, FC_CSID_RUP_AUP_RDI0);
            wakeup(&gC4FramesDone);

            if (do_focus && actuator_ok) {
                c4MoveActuator(&bus, hw, want_focus, &focus_cur, &lens_moving_until);
                lck_mtx_lock(&gC4Lock);
                gC4FocusCur = focus_cur;
                gC4LensMovingUntilMs = lens_moving_until;
                lck_mtx_unlock(&gC4Lock);
            }

            if (selftest && seq == 4u) {
                (void)fc_sensor_clamp_exposure_for_height(hw->sensor_id, mode, gC4Hdr.height, hw->default_lines, hw->gain_unity * 8u, vts_floor, &want_exp);
                do_exp = 1;
            }
            if (do_exp) {
                struct fc_ov32c_exposure clamped;
                (void)fc_sensor_clamp_exposure_for_height(hw->sensor_id, mode, gC4Hdr.height, want_exp.lines, want_exp.gain, want_exp.frame_lines, &clamped);
                const int erc = fc_sensor_apply_exposure(hw->sensor_id, &bus, gC4Hdr.slave7, &clamped);
                if (erc == FC_I2C_OK) {
                    next_exp = clamped;
                    next_exp_from = seq + 2u;
                }
                if (selftest) {
                    live_exp_rc = erc;
                    printf("FUXI CAMERA: c4 [%s] live exposure -> lines=%u gain=%#x vts=%u (%s)\n",
                        hw->name, clamped.lines, clamped.gain, clamped.frame_lines, rcName(erc));
                }
            }
            if (selftest && seq == 7u) {
                if (hw->sensor_id == FC_SENSOR_TELE || hw->sensor_id == FC_SENSOR_WIDE) {
                    uint8_t r_vts[2] = {0}, r_exp[2] = {0}, r_gain[2] = {0};
                    if (fc_i2c_read16(&bus, gC4Hdr.slave7, 0x0340u, r_vts, 2) == FC_I2C_OK)
                        rb_vts = ((uint32_t)r_vts[0] << 8) | r_vts[1];
                    if (fc_i2c_read16(&bus, gC4Hdr.slave7, 0x0202u, r_exp, 2) == FC_I2C_OK)
                        rb_lines = ((uint32_t)r_exp[0] << 8) | r_exp[1];
                    if (fc_i2c_read16(&bus, gC4Hdr.slave7, 0x0204u, r_gain, 2) == FC_I2C_OK)
                        rb_gain = ((uint32_t)r_gain[0] << 8) | r_gain[1];
                } else {
                    uint8_t r_vts[2] = {0}, r_exp[3] = {0}, r_gain[2] = {0};
                    if (fc_i2c_read16(&bus, gC4Hdr.slave7, 0x380eu, r_vts, 2) == FC_I2C_OK)
                        rb_vts = ((uint32_t)r_vts[0] << 8) | r_vts[1];
                    if (fc_i2c_read16(&bus, gC4Hdr.slave7, 0x3500u, r_exp, 3) == FC_I2C_OK)
                        rb_lines = ((uint32_t)(r_exp[0] & 0x0fu) << 16) | ((uint32_t)r_exp[1] << 8) | r_exp[2];
                    if (fc_i2c_read16(&bus, gC4Hdr.slave7, 0x3508u, r_gain, 2) == FC_I2C_OK)
                        rb_gain = ((uint32_t)(r_gain[0] & 0x1fu) << 8) | r_gain[1];
                }
                printf("FUXI CAMERA: c4 [%s] live exposure latched -> rb vts=%u lines=%u gain=%#x\n",
                    hw->name, rb_vts, rb_lines, rb_gain);
            }

            uint32_t gfsr = 0u, cb4_fsr = 0u;
            if (c4CheckSmmuFault(&smmu, &gfsr, &cb4_fsr)) {
                smmu_faults++;
                printf("FUXI CAMERA: c4 SMMU fault at frame %llu (gfsr=%#x cb4_fsr=%#x)\n",
                    (unsigned long long)seq, gfsr, cb4_fsr);
                lck_mtx_lock(&gC4Lock);
                gC4Broken = 1;
                lck_mtx_unlock(&gC4Lock);
                stop_why = "smmu-fault";
                break;
            }

            if (selftest || seq <= 3u) {
                dcacheCivac(gC4BufVa[cur_slot], gC4AllocBytes);
            }
            if (seq <= 3u) {
                const uint32_t raw_bpl = fc_raw10_bytes_per_line(gC4Hdr.width);
                const uint32_t raw_bytes = raw_bpl * gC4Hdr.height;
                const uint32_t tail_bytes = (gC4FrameBytes > raw_bytes) ? (gC4FrameBytes - raw_bytes) : 0u;
                const uint32_t guard_bytes = (gC4AllocBytes > gC4FrameBytes) ? (gC4AllocBytes - gC4FrameBytes) : 0u;
                const uint8_t *slot_buf = (const uint8_t *)gC4BufVa[cur_slot];
                const uint32_t tail_dirty = fc_guard_scan(slot_buf, raw_bytes, gC4FrameBytes, FC_TPG_CANARY);
                const uint32_t guard_dirty = fc_guard_scan(slot_buf, gC4FrameBytes, gC4AllocBytes, FC_TPG_CANARY);
                printf("FUXI CAMERA: c4 [%s] f%llu dma_check stride=%u raw_bpl=%u tail_dirty=%u/%u guard_dirty=%u/%u\n",
                       hw->name, (unsigned long long)seq, gC4Stride, raw_bpl,
                       tail_dirty, tail_bytes, guard_dirty, guard_bytes);
                if (guard_dirty != 0u) {
                    printf("FUXI CAMERA: c4 [%s] f%llu DMA GUARD OVERFLOW guard_dirty=%u/%u!\n",
                           hw->name, (unsigned long long)seq, guard_dirty, guard_bytes);
                }
            }
            if (selftest) {
                struct fc_frame_stats st;
                fc_frame_analyze((const uint8_t *)gC4BufVa[cur_slot], gC4FrameBytes, FC_TPG_CANARY, &st);
                uint32_t min10 = 0u, max10 = 0u;
                const uint32_t mean10 = fc_raw10_mean_sample((const uint8_t *)gC4BufVa[cur_slot], gC4Stride,
                    gC4Hdr.width, gC4Hdr.height, &min10, &max10);
                uint32_t diff = 0u;
                if (prev_slot >= 0 && (unsigned)prev_slot != cur_slot) {
                    diff = fc_frame_diff((const uint8_t *)gC4BufVa[prev_slot], (const uint8_t *)gC4BufVa[cur_slot], gC4FrameBytes);
                }
                if (seq >= 2u && seq <= 4u) { low_mean_sum += mean10; low_mean_cnt++; }
                if (seq >= 7u && seq <= 10u) { high_mean_sum += mean10; high_mean_cnt++; }
                printf("FUXI CAMERA: c4 f%llu dt=%u ms slot=%u non_canary=%u/%u diff=%u mean10=%u(min=%u max=%u) exp=%u/%#x\n",
                    (unsigned long long)seq, frame_ms, cur_slot, st.non_canary, gC4FrameBytes, diff, mean10, min10, max10,
                    cur_exp.lines, cur_exp.gain);
                prev_slot = (int)cur_slot;
                if (seq >= selftest_frames) {
                    rc = (smmu_faults == 0u && live_exp_rc == FC_I2C_OK && st.non_canary > gC4FrameBytes / 2u) ? 0 : -17;
                    break;
                }
            } else {
                prev_slot = (int)cur_slot;
            }
            cur_slot = (unsigned)next_slot;
        }

        const uint64_t chk_ms = c4NowMs();
        if (selftest) {
            if (fc_elapsed_exceeded(chk_ms, last_frame_ms, 1500u)) {
                stop_why = "frame-timeout";
                rc = -18;
                break;
            }
        } else {
            lck_mtx_lock(&gC4Lock);
            const int open_now = gC4Open;
            const uint64_t last_rd = gC4LastRead;
            const int broken_now = gC4Broken;
            const int sw_now = gC4SwitchWanted;
            lck_mtx_unlock(&gC4Lock);
            const uint64_t idle_now = c4NowMs();
            if (broken_now) { stop_why = "fault"; break; }
            if (sw_now) { stop_why = "sensor-switch"; rc = 0; break; }
            if (!open_now) { stop_why = "closed"; rc = 0; break; }
            if (fc_elapsed_exceeded(idle_now, last_rd, 1500u)) { stop_why = "idle-1500ms"; rc = 0; break; }
            if (fc_elapsed_exceeded(idle_now, last_frame_ms, 2000u)) { stop_why = "frame-timeout"; rc = -18; break; }
        }
    }

    if (selftest) {
        const uint32_t low_avg = low_mean_cnt ? (low_mean_sum / low_mean_cnt) : 0u;
        const uint32_t high_avg = high_mean_cnt ? (high_mean_sum / high_mean_cnt) : 0u;
        printf("FUXI CAMERA: c4 [%s] selftest frames=%llu/%u exp_rc=%s low_mean=%u high_mean=%u rb=%u/%u/%#x smmu_fault=%u dev=%d (%s)\n",
            hw->name, (unsigned long long)seq, selftest_frames, rcName(live_exp_rc), low_avg, high_avg, rb_vts, rb_lines, rb_gain,
            smmu_faults, gC4DevMade, stop_why);
    } else {
        printf("FUXI CAMERA: c4 [%s] darwin session stopped (%s): done=%llu given=%llu late=%llu\n",
            hw->name, stop_why, (unsigned long long)gC4FramesDone, (unsigned long long)gC4Given, (unsigned long long)gC4Late);
    }

teardown:
    lck_mtx_lock(&gC4Lock);
    gC4Streaming = 0;
    gC4FlashActive = 0;
    gC4FlashLevel = 0u;
    gC4FlashUntilMs = 0ull;
    gC4FocusCur = FUXI_CAMERA_NO_FOCUS;
    gC4ActuatorOk = 0;
    gC4OisOk = 0;
    gC4LensMovingUntilMs = 0ull;
    if (!selftest && !gC4SwitchWanted && hw) {
        gC4Ask.lines = hw->default_lines;
        gC4Ask.gain = hw->default_gain;
        gC4Ask.frame_lines = fc_sensor_vts_floor(hw, mode);
        gC4AskPending = 0;
        gC4FocusPending = 0;
    }
    for (unsigned s = 0u; s < FC_RING_SLOTS; ++s) {
        if (gC4Slots[s].state != FC_SLOT_READING) gC4Slots[s].state = FC_SLOT_FREE;
    }
    lck_mtx_unlock(&gC4Lock);
    wakeup(&gC4FramesDone);

    if (streaming_started) {
        struct fc_bus stop_bus = {&kBusOps, NULL, active_sda, active_scl, 5u, 200u, 0u, 0u, 0u};
        (void)fc_sensor_stream_off(hw->sensor_id, &stop_bus, gC4Hdr.slave7);
        if (actuator_ok) c4ParkAndSleepActuator(&stop_bus, hw, focus_cur);
        if (ois_ok) c4ReleaseOis(&stop_bus, hw, ois_ok, ois_24618);
    }
    pinRestore(hw->pin_sda);
    pinRestore(hw->pin_scl);

    if (wLite.va) {
        wr(&wLite, FC_CSID_LITE0_OFF + FC_CSID_RDI0_CTRL, 0u);
        wr(&wLite, FC_CSID_LITE0_OFF + FC_CSID_RDI0_CFG0, 0u);
        wr(&wLite, FC_VFE_LITE0_OFF + FC_VFE_WM0_CFG, 0u);
        IOSleep(2);
        wr(&wLite, FC_CSID_LITE0_OFF + FC_CSID_RST_CFG, 0x11u);
        wr(&wLite, FC_CSID_LITE0_OFF + FC_CSID_RST_CMD, 0x5u);
        (void)waitBits(&wLite, FC_CSID_LITE0_OFF + FC_CSID_TOP_IRQ_STATUS, FC_CSID_TOP_RESET_DONE, FC_CSID_TOP_RESET_DONE, 20000u);
        wr(&wLite, FC_CSID_LITE0_OFF + FC_CSID_TOP_IRQ_MASK, 0u);
        wr(&wLite, FC_CSID_LITE0_OFF + FC_CSID_BUF_DONE_IRQ_MASK, 0u);
        wr(&wLite, FC_CSID_LITE0_OFF + FC_CSID_RX_IRQ_MASK, 0u);
        wr(&wLite, FC_CSID_LITE0_OFF + FC_CSID_RDI0_IRQ_MASK, 0u);
        csidClearAllIrqs(&wLite);
    }
    if (wPhy.va) {
        stopCsiphy(&wPhy, po, is_cphy);
    }
    if (smmu_ok) smmuDetachIfe(&smmu);
    fc_unmap(&wPhy);
    fc_unmap(&wLite);
    for (int idx = 12; idx >= 0; --idx) branchOff(branches[idx].win, branches[idx].reg, &br_sw[idx]);
    if (mm0_voted) (void)voteBcm("MM0", gMm0Address, 0);
    for (int idx = 6; idx >= 0; --idx) {
        if (!rcg_save[idx].saved) continue;
        const uint32_t base = kRcgs[idx].cmd;
        if (kRcgs[idx].has_mnd) {
            wr(&gCamcc, base + 8u, rcg_save[idx].m_val);
            wr(&gCamcc, base + 0xcu, rcg_save[idx].n_val);
            wr(&gCamcc, base + 0x10u, rcg_save[idx].d_val);
        }
        wr(&gCamcc, base + 4u, rcg_save[idx].cfg);
        wr(&gCamcc, base, rd(&gCamcc, base) | FC_RCG_CMD_UPDATE);
        (void)waitBits(&gCamcc, base, FC_RCG_CMD_UPDATE, 0u, 10000u);
    }
    if (pll_enabled) {
        wr(&gCamcc, FC_PLL_MODE, rd(&gCamcc, FC_PLL_MODE) & ~FC_PLL_OUTCTRL);
        wr(&gCamcc, FC_PLL_USER_CTL, rd(&gCamcc, FC_PLL_USER_CTL) & ~FC_PLL_OUT_MASK);
        wr(&gCamcc, FC_PLL_OPMODE, FC_PLL_OP_STANDBY);
        if (!(pll_mode0 & FC_PLL_RESET_N)) wr(&gCamcc, FC_PLL_MODE, rd(&gCamcc, FC_PLL_MODE) & ~FC_PLL_RESET_N);
    }
    powerDownSensor(hw);
    c4DrainReaders();
    if (selftest != 0 || rc != 0) {
        c4FreeRingBuffers();
    }
    return rc;
}

static int camdevOpen(__unused dev_t dev, __unused int flags, __unused int type, __unused struct proc *p)
{
    lck_mtx_lock(&gC4Lock);
    if (gC4Broken) {
        lck_mtx_unlock(&gC4Lock);
        return EIO;
    }
    gC4Open = 1;
    lck_mtx_unlock(&gC4Lock);
    return 0;
}

static int camdevClose(__unused dev_t dev, __unused int flags, __unused int type, __unused struct proc *p)
{
    lck_mtx_lock(&gC4Lock);
    gC4Open = 0;
    gC4Wanted = 0;
    gC4FlashActive = 0;
    gC4FlashLevel = 0u;
    gC4FlashUntilMs = 0ull;
    lck_mtx_unlock(&gC4Lock);
    wakeup(&gC4Open);
    return 0;
}

static int camdevRead(__unused dev_t dev, struct uio *uio, __unused int ioflag)
{
    struct fuxi_camera_frame frame;
    const uint64_t since = c4NowMs();
    int chosen = -1;
    int error = 0;
    vm_offset_t slot_va = 0;
    uint32_t frame_bytes = 0u;

    lck_mtx_lock(&gC4Lock);
    gC4LastRead = c4NowMs();
    if (!gC4Streaming && !gC4Wanted) {
        gC4Wanted = 1;
        wakeup(&gC4Open);
    }
    for (;;) {
        if (gC4Broken) {
            lck_mtx_unlock(&gC4Lock);
            return EIO;
        }
        chosen = fc_ring_pick_newest_read(gC4Slots, gC4Given);
        if (chosen >= 0) break;
        if (fc_elapsed_exceeded(c4NowMs(), since, gC4Streaming ? 3000u : 15000u)) {
            lck_mtx_unlock(&gC4Lock);
            return EAGAIN;
        }
        if (!gC4Streaming && !gC4Wanted) {
            gC4Wanted = 1;
            wakeup(&gC4Open);
        }
        struct timespec ts = {0, 100 * NSEC_PER_MSEC};
        error = msleep(&gC4FramesDone, &gC4Lock, PZERO | PCATCH, "fuxi-camera", &ts);
        if (error != 0 && error != EWOULDBLOCK) {
            lck_mtx_unlock(&gC4Lock);
            return error;
        }
        gC4LastRead = c4NowMs();
    }
    const struct fc_sensor_hw *hw = fc_sensor_hw_for(gC4SensorId);
    if (!hw) hw = fc_sensor_hw_for(FC_SENSOR_FRONT);
    gC4Slots[chosen].state = FC_SLOT_READING;
    gC4Readers++;
    slot_va = gC4BufVa[chosen];
    frame_bytes = gC4FrameBytes;
    gC4Given = gC4Slots[chosen].sequence;
    bzero(&frame, sizeof(frame));
    memcpy(frame.magic, FUXI_CAMERA_MAGIC, 8);
    frame.header_bytes = (uint32_t)sizeof(frame);
    frame.width = gC4Hdr.width;
    frame.height = gC4Hdr.height;
    frame.stride = gC4Stride;
    frame.format = 1u;
    frame.bayer = hw->bayer;
    frame.black = hw->black;
    frame.sequence = gC4Slots[chosen].sequence;
    frame.time = gC4Slots[chosen].time;
    frame.exposure_lines = gC4Slots[chosen].exposure_lines;
    frame.line_ns = hw->line_ns;
    frame.gain = gC4Slots[chosen].gain;
    frame.gain_unity = hw->gain_unity;
    frame.gain_max = hw->gain_max;
    frame.frame_lines = gC4Slots[chosen].frame_lines;
    frame.frame_lines_min = gC4VtsMin;
    frame.frame_lines_max = hw->vts_max;
    frame.margin = hw->margin;
    frame.sensor = hw->sensor_id;
    frame.focus = gC4SlotFocus[chosen];
    frame.reserved[FUXI_CAMERA_R_FLAGS] = gC4SlotFlags[chosen];
    frame.reserved[FUXI_CAMERA_R_SOF_LO] = (uint32_t)gC4Slots[chosen].sof_time;
    frame.reserved[FUXI_CAMERA_R_SOF_HI] = (uint32_t)(gC4Slots[chosen].sof_time >> 32);
    frame.reserved[FUXI_CAMERA_R_FPS_MILLI] = gC4FpsMilli;
    lck_mtx_unlock(&gC4Lock);

    error = uiomove((caddr_t)&frame, (int)MIN(sizeof(frame), (size_t)uio_resid(uio)), uio);
    if (error == 0 && uio_resid(uio) > 0 && slot_va != 0) {
        const uint32_t copy_n = (uint32_t)MIN((uint64_t)frame_bytes, (uint64_t)uio_resid(uio));
        dcacheCivac(slot_va, copy_n);
        error = uiomove((caddr_t)slot_va, (int)copy_n, uio);
    }
    lck_mtx_lock(&gC4Lock);
    if (gC4Slots[chosen].state == FC_SLOT_READING) gC4Slots[chosen].state = FC_SLOT_READY;
    gC4LastRead = c4NowMs();
    gC4Readers--;
    lck_mtx_unlock(&gC4Lock);
    wakeup(&gC4Readers);
    return error;
}

static int camdevWrite(__unused dev_t dev, struct uio *uio, __unused int ioflag)
{
    struct fuxi_camera_command cmd;
    if (uio_resid(uio) < (user_ssize_t)sizeof(cmd)) return EINVAL;
    const int error = uiomove((caddr_t)&cmd, (int)sizeof(cmd), uio);
    if (error != 0) return error;
    if (cmd.command == FUXI_CAMERA_EXPOSURE) {
        lck_mtx_lock(&gC4Lock);
        struct fc_ov32c_exposure clamped;
        if (fc_sensor_clamp_exposure_for_height(gC4SensorId, gC4Mode, gC4ActiveHeight, cmd.a, cmd.b, cmd.c, &clamped) != 0) {
            lck_mtx_unlock(&gC4Lock);
            return EINVAL;
        }
        gC4Ask = clamped;
        gC4AskPending = 1;
        lck_mtx_unlock(&gC4Lock);
        return 0;
    }
    if (cmd.command == FUXI_CAMERA_KEEP) {
        lck_mtx_lock(&gC4Lock);
        gC4LastRead = c4NowMs();
        lck_mtx_unlock(&gC4Lock);
        uio_setresid(uio, 0);
        return 0;
    }
    if (cmd.command == FUXI_CAMERA_SENSOR) {
        if (!fc_sensor_hw_for(cmd.a)) return EINVAL;
        const uint32_t req_mode = (cmd.b == FC_MODE_ANY) ? cmd.c : cmd.b;
        const uint8_t *target_tab = NULL;
        struct fc_tab_header target_hdr;
        if (!c4LoadTabForMode(cmd.a, req_mode, &target_tab, &target_hdr)) return ENODEV;
        lck_mtx_lock(&gC4Lock);
        if (gC4Broken) {
            lck_mtx_unlock(&gC4Lock);
            return EIO;
        }
        if (cmd.a == gC4SensorId && req_mode == gC4Mode && !gC4SwitchWanted) {
            lck_mtx_unlock(&gC4Lock);
            return 0;
        }
        gC4NextSensorId = cmd.a;
        gC4NextMode = req_mode;
        gC4SwitchWanted = 1;
        gC4Wanted = 1;
        gC4FocusPending = 0;
        gC4LastRead = c4NowMs();
        lck_mtx_unlock(&gC4Lock);
        wakeup(&gC4Open);
        return 0;
    }
    if (cmd.command == FUXI_CAMERA_FOCUS) {
        lck_mtx_lock(&gC4Lock);
        if (gC4Broken) {
            lck_mtx_unlock(&gC4Lock);
            return EIO;
        }
        const struct fc_sensor_hw *hw = fc_sensor_hw_for(gC4SensorId);
        if (!hw || !hw->actuator_slave7) {
            gC4LastRead = c4NowMs();
            lck_mtx_unlock(&gC4Lock);
            return ENOTSUP;
        }
        const uint32_t target = (cmd.b == FUXI_CAMERA_FOCUS_PARK)
            ? 0u
            : (cmd.a > FUXI_CAMERA_FOCUS_MAX ? FUXI_CAMERA_FOCUS_MAX : cmd.a);
        gC4FocusAsk = target;
        gC4FocusPending = 1;
        gC4LastRead = c4NowMs();
        lck_mtx_unlock(&gC4Lock);
        return 0;
    }
    if (cmd.command == FUXI_CAMERA_FLASH) {
        uint32_t lv = 0u, ms = 0u, fmode = FUXI_CAMERA_FLASH_OFF;
        int rc;
        lck_mtx_lock(&gC4Lock);
        if (gC4Broken) {
            lck_mtx_unlock(&gC4Lock);
            return EIO;
        }
        const struct fc_sensor_hw *hw = fc_sensor_hw_for(gC4SensorId);
        if (!hw || hw->sensor_id == FC_SENSOR_FRONT) {
            gC4LastRead = c4NowMs();
            lck_mtx_unlock(&gC4Lock);
            return ENOTSUP;
        }
        /* Legacy zero-mode nonzero-level strobe requests (cmd.c == 0 && cmd.b != 0) must not guess PMIC strobe */
        uint32_t req_mode = (cmd.c == 0u && cmd.b != 0u) ? FUXI_CAMERA_FLASH_STROBE : cmd.c;
        rc = fc_clamp_flash_cmd(cmd.b, cmd.a, req_mode, &lv, &ms, &fmode);
        if (rc < 0) {
            gC4LastRead = c4NowMs();
            lck_mtx_unlock(&gC4Lock);
            return ENOTSUP;
        }
        const uint64_t now_ms = c4NowMs();
        gC4LastRead = now_ms;
        if (rc == 0) {
            gC4FlashActive = 0;
            gC4FlashLevel = 0u;
            gC4FlashUntilMs = 0ull;
        } else {
            gC4FlashActive = 1;
            gC4FlashLevel = lv;
            gC4FlashUntilMs = now_ms + ms;
        }
        const uint32_t armed_lv = gC4FlashLevel;
        const char *hname = hw->name;
        lck_mtx_unlock(&gC4Lock);
        if (rc > 0) {
            printf("FUXI CAMERA: c4 [%s] flash torch-fill armed level=%u ms=%u\n", hname, armed_lv, ms);
        }
        return 0;
    }
    return ENOTSUP;
}

static const struct cdevsw gCamDevCdevsw = {
    .d_open = camdevOpen, .d_close = camdevClose, .d_read = camdevRead, .d_write = camdevWrite,
    .d_ioctl = eno_ioctl, .d_stop = eno_stop, .d_reset = eno_reset, .d_select = eno_select,
    .d_mmap = eno_mmap, .d_strategy = eno_strat, .d_type = 0
};

static void camdevMake(void)
{
    if (gC4DevMade) return;
    const int major = cdevsw_add(-1, &gCamDevCdevsw);
    if (major >= 0 && devfs_make_node(makedev(major, 0), DEVFS_CHAR, UID_ROOT, GID_WHEEL, 0666, "fuxi-camera") != NULL) {
        gC4DevMade = 1;
        printf("FUXI CAMERA: registered %s (major=%d, hdr=%zu B, cmd=%zu B)\n",
            FUXI_CAMERA_DEVICE, major, sizeof(struct fuxi_camera_frame), sizeof(struct fuxi_camera_command));
    } else {
        printf("FUXI CAMERA: failed to register %s (major=%d)\n", FUXI_CAMERA_DEVICE, major);
    }
}

static void serveDarwin(void)
{
    const uint32_t init_sensor = fc_pick_sensor("fuxi,camera-active");
    const struct fc_sensor_hw *init_hw = fc_sensor_hw_for(init_sensor);
    if (!gC4DevMade || !c4ActivateTabMode(init_sensor, FC_MODE_PHOTO)) return;
    lck_mtx_lock(&gC4Lock);
    gC4SensorId = init_sensor;
    gC4NextSensorId = init_sensor;
    gC4Mode = FC_MODE_PHOTO;
    gC4NextMode = FC_MODE_PHOTO;
    if (init_hw) {
        gC4Ask.lines = init_hw->default_lines;
        gC4Ask.gain = init_hw->default_gain;
        gC4Ask.frame_lines = gC4VtsMin;
    }
    gC4AskPending = 0;
    lck_mtx_unlock(&gC4Lock);
    printf("FUXI CAMERA: %s standby (%ux%u RAW10, sensor=%u %s)\n",
        FUXI_CAMERA_DEVICE, gC4Hdr.width, gC4Hdr.height, init_sensor, init_hw ? init_hw->name : "?");
    while (1) {
        lck_mtx_lock(&gC4Lock);
        while (!gC4Broken && !gC4SwitchWanted && !(gC4Open && gC4Wanted)) {
            (void)msleep(&gC4Open, &gC4Lock, PZERO, "fuxi-cam-idle", NULL);
        }
        if (gC4Broken) {
            lck_mtx_unlock(&gC4Lock);
            break;
        }
        const int do_switch = gC4SwitchWanted;
        const uint32_t next_id = gC4NextSensorId;
        const uint32_t next_mode = gC4NextMode;
        gC4SwitchWanted = 0;
        const int should_run = gC4Open && gC4Wanted;
        gC4Wanted = 0;
        lck_mtx_unlock(&gC4Lock);
        if (do_switch) {
            if (!c4ActivateTabMode(next_id, next_mode)) {
                printf("FUXI CAMERA: sensor switch to %u (mode=%u) failed (tab missing)\n", next_id, next_mode);
                continue;
            }
            const struct fc_sensor_hw *nhw = fc_sensor_hw_for(next_id);
            lck_mtx_lock(&gC4Lock);
            if (nhw) {
                gC4Ask.lines = nhw->default_lines;
                gC4Ask.gain = nhw->default_gain;
                gC4Ask.frame_lines = fc_sensor_vts_floor_for_height(nhw, next_mode, gC4Hdr.height);
            }
            gC4AskPending = 0;
            const int open_after = gC4Open;
            lck_mtx_unlock(&gC4Lock);
            printf("FUXI CAMERA: switched active sensor -> %u mode=%u (%s)\n", next_id, next_mode, nhw ? nhw->name : "?");
            if (open_after) (void)c4SessionRun(next_id, next_mode, 0, 0u);
            continue;
        }
        if (should_run) {
            lck_mtx_lock(&gC4Lock);
            const uint32_t active_id = gC4SensorId;
            const uint32_t active_mode = gC4Mode;
            lck_mtx_unlock(&gC4Lock);
            (void)c4SessionRun(active_id, active_mode, 0, 0u);
        }
    }
    printf("FUXI CAMERA: %s stopped permanently\n", FUXI_CAMERA_DEVICE);
}

static void cameraThread(void *arg, wait_result_t result)
{
    (void)result;
    const unsigned mode = (unsigned)(uintptr_t)arg;
    IOSleep(8000); /* 讓開機初期的 RPMh 使用者（ADSP、顯示、Wi-Fi）先做完 */
    int ok = fc_map(&gGcc) == KERN_SUCCESS && fc_map(&gCamcc) == KERN_SUCCESS;
    for (unsigned i = 0; ok && i < FC_PIN_COUNT; ++i) {
        gPin[i].pa = pinPa(kAllPins[i]) & ~(uint64_t)0x3fffu;
        gPin[i].size = 0x4000;
        for (unsigned j = 0; j < i; ++j) if (gPin[j].pa == gPin[i].pa) gPin[i].va = gPin[j].va;
        ok = fc_map(&gPin[i]) == KERN_SUCCESS;
    }
    if (!ok) {
        printf("FUXI CAMERA: mapping failed\n");
        thread_terminate(current_thread());
        return;
    }
    for (unsigned i = 0; i < FC_PIN_COUNT; ++i) {
        const uint32_t off = pinOff(kAllPins[i], i);
        gFoundCtl[i] = rd(&gPin[i], off);
        gFoundIo[i] = rd(&gPin[i], off + 4u);
    }
    const uint32_t probe_id = fc_pick_sensor("fuxi,camera-probe-sensor");
    const struct fc_sensor_hw *probe_hw = fc_sensor_hw_for(probe_id);
    if (!probe_hw) {
        printf("FUXI CAMERA: unknown sensor %u\n", probe_id);
        thread_terminate(current_thread());
        return;
    }
    survey(probe_hw);
    int rc = 0;
    if (mode >= 7u) {
        if (mode == 8u) rc = c4SessionRun(probe_id, FC_MODE_PHOTO, 1, 10u);
        reportSensorPins(probe_hw);
        reportCamcc("after", probe_hw);
        printf("FUXI CAMERA: done mode %u sensor=%u (%s) result %d\n", mode, probe_id, probe_hw->name, rc);
        serveDarwin();
        thread_terminate(current_thread());
        return;
    }
    if (mode >= 2) rc = powerUpAndScan(probe_hw, mode);
    else (void)readRailsFor(probe_hw);
    if (mode >= 2) {
        reportSensorPins(probe_hw);
        reportCamcc("after", probe_hw);
    }
    printf("FUXI CAMERA: done mode %u sensor=%u (%s) result %d\n", mode, probe_id, probe_hw->name, rc);
    thread_terminate(current_thread());
}

/* sweet_ufs.c 在 fuxi_modem_init() 之後呼叫一次。 */
void fuxi_camera_init(void)
{
    thread_t thread = NULL;
    const unsigned mode = fc_mode();
    if (!mode || gStarted) return;
    gStarted = 1;
    if (mode >= 7u) camdevMake();
    if (kernel_thread_start(cameraThread, (void *)(uintptr_t)mode, &thread) != KERN_SUCCESS) {
        printf("FUXI CAMERA: thread start failed\n");
        return;
    }
    thread_deallocate(thread);
    printf("FUXI CAMERA: started mode %u (probe=%u active=%u swap=%u pll=%u mmnoc=%u)\n",
        mode, fc_pick_sensor("fuxi,camera-probe-sensor"), fc_pick_sensor("fuxi,camera-active"),
        adtU32("fuxi,camera-swap", 0), adtU32("fuxi,camera-pll", 0), adtU32("fuxi,camera-mmnoc", 0));
}
#endif /* FUXI */
