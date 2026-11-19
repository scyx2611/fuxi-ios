/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* FUXI speaker amplifiers: two Cirrus CS35L41 on QUP2 SE0 I2C (live DT i2c@880000: cs35l41@40 "B", reset GPIO 183; cs35l41@42
 * "T", reset GPIO 182, both active low; pins gpio0 SDA / gpio1 SCL, function 2 = qup2_se0_l0/l1_mirb, 2 mA, pull-up; engine SPI
 * 373, level). Boot argument fuxiamp, or ADT /chosen u32 "fuxi,amp" (the command line is full):
 *   1  survey, reads only: the GCC votes 0x52008 / 0x52010 / 0x52018, the five QUP2 branches (core 0x233d4, s_ahb 0x233d0, m_ahb
 *      0x233cc, core_2x 0x23004, s0 0x1e004; gcc-sm8550.c), the SE0 clock source 0x1e010 (DFS state, which level is 19.2 MHz) and
 *      the four pins; only when all five branches run, also the engine: protocol, FIFO lock (GENI_IF_DISABLE bit 0 set means the
 *      engine is GSI only, as touch's QUP1 SE4 is), clock configuration, SCL counters and FIFO depths.
 *   2  brings the bus up as UniXNU-sheng AudioProbe.cpp setupClocks / setupPins / setupBus do for this same engine on sheng
 *      (6880068): GPLL0 and the five QUP2 votes, the I2C pins, the engine at 400 kHz from the 19.2 MHz DFS level if the boot stages
 *      left it unconfigured, FIFO mode (GeniI2c.c, sheng's, unchanged); SPI 373 is masked in the distributor first (no handler
 *      here, every transfer is polled). Then each amplifier comes out of reset (its eGPIO taken from the LPASS island, low 2 ms,
 *      high, 10 ms for its OTP boot; the pad's input is logged low and high) and DEVID
 *      0x0 (0x35a40 expected), REVID 0x4, IRQ1_STATUS3 0x10018 and IRQ1_STATUS4 0x1001c (OTP boot error / done) are read. Nothing
 *      is written to the amplifiers. At the end both are held in reset again and the pins and votes go back to what they were.
 *   3  brings up the lower amp only and checks two firmware XM regions with the output and DSP disabled.
 *   4  verifies all six stock XM/YM/PM regions under the same mute and reset conditions.
 *   5  also writes the captured bottom tuning and calibration (CAL_R, CAL_CHECKSUM, CAL_STATUS) and reads them back.
 *   6  applies the B2 errata and OTP trims first, loads as mode 5, then runs the protection firmware with GLOBAL_EN and AMP_EN
 *      clear and no I2S clocks (Linux cs35l41_hda.c cs35l41_smart_amp): HALO_STATE RUN, mailbox status and PAUSE are checked,
 *      then the core is stopped and the amplifier goes back into reset. No output stage, no boost, no volume write.
 *   7  先做 mode 6；只有 CSPL_STATE 0（CSPL_ST_RUNNING）、ERRORNO 0、CAL_STATUS 1 才繼續：下方喇叭播一秒 2 kHz、-20 dBFS，
 *      數位音量 -30 dB（0x6000 = 0xb885），不經 DSP（早期 Fuxi shim/fuxi-probe au_amp_on.h steps 12-31 與 au_tone_stage，
 *      當年 a-spk1 出過聲）。PCM 由 fuxi_audio_spf.c format 5 送，交握在 fuxi_amp_tone.h。使用者 2026-11-18 只同意這一檔、
 *      這一顆喇叭。
 * Facts: QUP2 SE0 window 0x880000 + 0x4000, GCC 0x100000, TLMM 0xf100000 (0x1000 per pin: ctl, io at +4), GICD 0x17100000.
 * Windows are mapped with kernel_map VA and pmap entries (never ml_io_map), as fuxi_adsp.c does. */
#include <pexpert/arm64/board_config.h>
#if defined(FUXI)
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include <pexpert/device_tree.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>
#include "GeniI2c.h"
#include "fuxi_amp_tone.h"

uint32_t fuxi_amp_tone; /* fuxi_amp_tone.h：mode 7 的交握狀態，其餘模式保持 IDLE */

extern kern_return_t pmap_enter_options_addr(pmap_t, vm_map_offset_t, uint64_t, vm_prot_t, vm_prot_t, unsigned, boolean_t,
    unsigned, void *);
extern kern_return_t kernel_thread_start(thread_continue_t, void *, thread_t *);
extern kern_return_t thread_terminate(thread_t);
extern void thread_deallocate(thread_t);
extern void IOSleep(unsigned milliseconds);
void fuxi_amp_init(void);

#define FM_GCC 0x100000ull
#define FM_TLMM 0xf100000ull
#define FM_SE 0x880000ull
#define FM_GICD 0x17100000ull
#define FM_INTID_SE (32u + 373u)
#define FM_VOTE1 0x52008u
#define FM_VOTE2 0x52010u
#define FM_VOTE2_BITS 0x1fu        /* wrap2 core 0, s_ahb 1, m_ahb 2, core_2x 3, s0 4 */
#define FM_GPLL0_VOTE 0x52018u     /* bit 0 */
#define FM_GPLL0_MODE 0x0u         /* bit 31 lock detect */
#define FM_RCG 0x1e010u            /* gcc_qupv3_wrap2_s0_clk_src */
#define FM_CLK_OFF (1u << 31)
#define FM_SCL_COUNTERS (5u << 20 | 11u << 10 | 22u) /* 400 kHz from 19.2 MHz (i2c-qcom-geni.c): high 5, low 11, cycle 22 */
#define FM_CLK_CFG (2u << 4 | 1u)                     /* divider 2, enable */
#define FM_DEVID 0x35a40u
/* Linux v6.12 cs35l41_hda.c checks STATUS4 for completion and STATUS3 bit 31 for OTP error. */
#define FM_OTP_BOOT_DONE 0x02u
#define FM_OTP_BOOT_ERR 0x80000000u
#define FM_PWR_CTRL1 0x2014u
#define FM_PWR_CTRL2 0x2018u
#define FM_CCM_CORE_CTRL 0x02bc1000u
/* Mode 6 registers: Linux v6.12 include/sound/cs35l41.h and cs_dsp.c (HALO base 0x2b80000). */
#define FM_OTPID 0x10u
#define FM_TEST_KEY 0x40u
#define FM_OTP_MEM0 0x400u
#define FM_PLL_CLK_CTRL 0x2c04u
#define FM_DSP_CLK_CTRL 0x2c08u
#define FM_IRQ1_STATUS1 0x10010u
#define FM_IRQ1_STATUS2 0x10014u
#define FM_IRQ2_STATUS2 0x10814u
#define FM_IRQ2_MASK2 0x10914u
#define FM_MBOX_STS 0x13004u            /* DSP_MBOX_2 */
#define FM_MBOX_CMD 0x13020u            /* DSP_VIRT1_MBOX_1 */
#define FM_MBOX_DRV (1u << 20)          /* VIRT1 mailbox bit: driver to DSP */
#define FM_MBOX_FW (1u << 21)           /* VIRT2 mailbox bit: the DSP's acknowledgement */
#define FM_HALO_SOFT_RESET 0x02b80010u
#define FM_HALO_RX1_RATE 0x02b80080u
#define FM_HALO_TX1_RATE 0x02b80280u
#define FM_HALO_MPU 0x02bc3000u
#define FM_HALO_MPU_LOCK 0x02bc3140u
#define FM_HALO_WDT 0x02bc7000u
#define FM_HALO_CORE_EN 0x1u
#define FM_HALO_CORE_RESET 0x200u
/* Stock protection WMFW controls (inspect-cs35l41-halo.py): FIRMWARE_HALO_CSPL 0x400a4 at XM word 0x1f4, CSPL 0xcd at 0x94;
 * ampBottomTuning checks both bases in the runtime directory before mode 6 uses them. */
#define FM_XM(word) (0x02800000u + 4u * (word))
#define FM_HALO_STATE FM_XM(0x1f4u)
#define FM_HALO_HEARTBEAT FM_XM(0x1f5u)
#define FM_FIRMWARE_STATE FM_XM(0x1fdu)
#define FM_MAX_LRCLK_DELAY FM_XM(0x201u)
#define FM_CSPL_STATE FM_XM(0x96u)
#define FM_CSPL_ERRORNO FM_XM(0x97u)
#define FM_CSPL_TEMPERATURE FM_XM(0x98u)
#define FM_CAL_R FM_XM(0x9au)
#define FM_CAL_STATUS FM_XM(0x9cu)
#define FM_CAL_R_SELECTED FM_XM(0x9eu)
#define FM_CAL_SET_STATUS FM_XM(0x9fu)
/* GPIO 182/183 are eGPIOs (pinctrl-sm8550.c: egpio_present bit 11, egpio_enable bit 12, egpio_func 9). The boot stages leave them
 * at 0x801: present, enable clear = the LPASS island TLMM owns the pad, so the APPS io register drives nothing. pinctrl-msm sets
 * enable whenever it muxes a non-egpio function (the DT's cirrus_reset_default: gpio, output-high, 16 mA, no bias). amp2 (ksrc
 * 417dcf3) wrote 1 << 9 alone, kept the island as owner, and both amplifiers stayed in reset: NACK at 0x40 and 0x42. */
#define FM_EGPIO_PRESENT (1u << 11)
#define FM_EGPIO_ENABLE (1u << 12)
#define FM_RESET_CTL (1u << 9 | 7u << 6)  /* GPIO, output enabled, 16 mA, no pull */
/* Mode 7（早期 au_amp_on.h／audio.c 的值，逐項對照）。 */
#define FM_IRQ1_STATUS3 0x10018u
#define FM_TONE_ERRORS 0x800281c0u     /* IRQ1_STATUS1：amp short、temp error/warn、boost short、DCM UVP、boost OVP */
#define FM_TONE_PUP_DONE (1u << 24)     /* IRQ1_STATUS1 */
#define FM_TONE_PDN_DONE (1u << 23)
#define FM_TONE_PLL_LOCK 0x2u           /* IRQ1_STATUS3 */
#define FM_TONE_PLL_UNLOCK (1u << 4)
#define FM_TONE_TEST 0x2084u            /* 測試鑰匙下的上電／下電值 0x002f1aa0／0x002f1aa3 */
#define FM_DIG_VOL 0x6000u
#define FM_DIG_VOL_MUTE 0xa678u         /* HPF 開，音量靜音 */
#define FM_DIG_VOL_LEVEL 0xb885u        /* HPF 開，-30 dB：使用者 2026-11-18 只同意這一檔 */
#define FM_AMP_GAIN 0x6c04u
#define FM_TONE_READY_MS 15000u         /* READY 後等 PCM 圖開出 TDM 時脈的上限 */
#define FM_TONE_ON_MS 5000u             /* 正弦一秒，加 PCM 端排隊與回報的餘裕 */
#define FM_TDM_CTL (1u << 2 | 3u << 6)  /* gpio121／123／124：function 1，8 mA，不拉（早期 au_pin） */

static const uint32_t kHalts[5] = {0x233d4u, 0x233d0u, 0x233ccu, 0x23004u, 0x1e004u};
static const unsigned kPins[4] = {0, 1, 183, 182};    /* SDA, SCL, reset B, reset T */
static const uint8_t kAddr[2] = {0x40, 0x42};
static const unsigned kReset[2] = {183, 182};
static const char kName[2] = {'B', 'T'};

/* ---- MMIO windows (16 KiB pages) ---- */
struct fm_window { uint64_t pa; vm_size_t size; vm_offset_t va; };
static struct fm_window gGcc0 = {FM_GCC, 0x4000, 0}, gGcc1c = {FM_GCC + 0x1c000, 0x4000, 0}, gGcc20 = {FM_GCC + 0x20000, 0x4000, 0},
    gGcc50 = {FM_GCC + 0x50000, 0x4000, 0}, gSe = {FM_SE, 0x4000, 0}, gGicd = {FM_GICD, 0x4000, 0};
static struct fm_window gPin[4];
/* 只有 mode 7 對應：TDM 時脈、幀同步、資料輸出；gpio121–123 在 0xf178000 頁，gpio124 在 0xf17c000 頁。 */
static const unsigned kTdmPins[3] = {121, 123, 124};
static struct fm_window gTdmPin[3];

static kern_return_t fm_map(struct fm_window *w)
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
            return kr; /* VA kept reserved, as in fuxi_adsp.c */
        }
    }
    w->va = va;
    return KERN_SUCCESS;
}
static uint32_t rd(const struct fm_window *w, uint64_t pa) { return *(volatile uint32_t *)(w->va + (pa - w->pa)); }
static void wr(const struct fm_window *w, uint64_t pa, uint32_t value)
{
    *(volatile uint32_t *)(w->va + (pa - w->pa)) = value;
    __asm__ volatile("dsb sy" ::: "memory");
}
/* A GCC register by its offset from the GCC base. */
static const struct fm_window *gccWin(uint32_t off)
{
    const uint64_t pa = FM_GCC + off;
    const struct fm_window *w[4] = {&gGcc0, &gGcc1c, &gGcc20, &gGcc50};
    for (unsigned i = 0; i < 4; ++i) if (pa >= w[i]->pa && pa + 4 <= w[i]->pa + w[i]->size) return w[i];
    return NULL;
}
static uint32_t gcc(uint32_t off) { return rd(gccWin(off), FM_GCC + off); }
static void gccWr(uint32_t off, uint32_t value) { wr(gccWin(off), FM_GCC + off, value); }
static uint64_t pinPa(unsigned gpio) { return FM_TLMM + (uint64_t)gpio * 0x1000u; }
static uint32_t pinCtl(unsigned i) { return rd(&gPin[i], pinPa(kPins[i])); }
static uint32_t pinIo(unsigned i) { return rd(&gPin[i], pinPa(kPins[i]) + 4); }
static unsigned pinIndex(unsigned gpio) { for (unsigned i = 0; i < 4; ++i) if (kPins[i] == gpio) return i; return 0; }

/* ---- clocks ---- */
/* sheng's test: a branch is running unless it reports CLK_OFF without being under hardware control (bit 1). */
static int branchesRun(uint32_t *mask)
{
    int ok = 1;
    uint32_t off = 0;
    for (unsigned i = 0; i < 5; ++i) {
        const uint32_t v = gcc(kHalts[i]);
        if ((v & FM_CLK_OFF) && !(v & 2u)) { ok = 0; off |= 1u << i; }
    }
    if (mask) *mask = off;
    return ok;
}
/* The DFS level of the SE0 clock source that runs at 19.2 MHz (TCXO, no divider, no M/N), or -1. */
static int level19m2(void)
{
    for (unsigned l = 0; l < 8; ++l) {
        const uint32_t cfg = gcc(FM_RCG + 0x1c + 4 * l);
        const unsigned src = (cfg >> 8) & 7, mode = (cfg >> 12) & 3, hid = cfg & 0x1f;
        if (src == 0 && mode == 0 && hid <= 1) return (int)l; /* hid 0 or 1: divide by 1 */
    }
    return -1;
}

/* ---- the GIC: SPI 373 masked (polled engine, no handler in this kernel owns the line) ---- */
static void gicMask(unsigned intid)
{
    wr(&gGicd, FM_GICD + 0x180 + (intid / 32) * 4, 1u << (intid % 32));
    for (unsigned n = 0; (rd(&gGicd, FM_GICD) & 0x80000000u) && n < 100000; n++) delay(1); /* GICD_CTLR.RWP */
}
static int gicEnabled(unsigned intid) { return (rd(&gGicd, FM_GICD + 0x100 + (intid / 32) * 4) >> (intid % 32)) & 1; }

/* ---- the engine ---- */
static uint32_t ioRd(void *ctx, uint32_t reg) { (void)ctx; return rd(&gSe, FM_SE + reg); }
static void ioWr(void *ctx, uint32_t reg, uint32_t value) { (void)ctx; wr(&gSe, FM_SE + reg, value); }
static GeniI2c gBus;

static int ampTransferDone(void)
{
    for (unsigned stage = 0; stage < 3; stage++) {
        for (unsigned poll = 0; poll < 1000; poll++) {
            int result = geni_i2c_poll(&gBus);
            if (result == GeniDone) return 1;
            if (result == GeniFailed) return 0;
            delay(20);
        }
        if (geni_i2c_timeout(&gBus) != GeniPending) return 0;
    }
    return 0;
}

/* One register read on the bus (32-bit address, 32-bit data, both big-endian on the wire): 1 and *value, or 0. Polled in 20 us steps,
 * 20 ms per stage of GeniI2c's cancel/abort ladder, as sheng's transfer(). */
static int ampRead(uint8_t address, uint32_t reg, uint32_t *value)
{
    const uint8_t bytes[4] = {(uint8_t)(reg >> 24), (uint8_t)(reg >> 16), (uint8_t)(reg >> 8), (uint8_t)reg};
    gBus.address = address;
    if (geni_i2c_start_read_bytes(&gBus, bytes, 4, 4)) return 0;
    if (!ampTransferDone()) return 0;
    *value = __builtin_bswap32(gBus.rx);
    return 1;
}

/* CS35L41 B2: Linux cs35l41_revb2_errata_patch clears PWR_CTRL2, then
 * cs35l41_register_errata_patch clears CCM_CORE_CTRL. This muted RAM test
 * uses just the two disable writes. GLOBAL_EN must already be clear. */
static int ampWriteZero(uint8_t address, uint32_t reg)
{
    const uint8_t packet[8] = {(uint8_t)(reg >> 24), (uint8_t)(reg >> 16),
        (uint8_t)(reg >> 8), (uint8_t)reg, 0, 0, 0, 0};
    uint32_t value;
    gBus.address = address;
    return !geni_i2c_start_write_bytes(&gBus, packet, sizeof(packet)) &&
        ampTransferDone() && ampRead(address, reg, &value) && value == 0;
}

static int ampOutputDisabled(uint8_t address, int prepare)
{
    uint32_t power, amplifier, core;
    if (!ampRead(address, FM_PWR_CTRL1, &power) || !ampRead(address, FM_PWR_CTRL2, &amplifier) ||
        !ampRead(address, FM_CCM_CORE_CTRL, &core)) {
        printf("FUXI AMP: could not verify mute registers\n");
        return 0;
    }
    if (prepare && !(power & 1u) && ((amplifier | core) & 1u)) {
        if (!ampWriteZero(address, FM_PWR_CTRL2) || !ampWriteZero(address, FM_CCM_CORE_CTRL) ||
            !ampRead(address, FM_PWR_CTRL1, &power) || !ampRead(address, FM_PWR_CTRL2, &amplifier) ||
            !ampRead(address, FM_CCM_CORE_CTRL, &core)) {
            printf("FUXI AMP: could not establish muted B2 state\n");
            return 0;
        }
    }
    if ((power | amplifier | core) & 1u) {
        printf("FUXI AMP: mute check refused global=%#x amplifier=%#x core=%#x\n", power, amplifier, core);
        return 0;
    }
    if (prepare) printf("FUXI AMP: muted B2 state verified global=%#x amplifier=%#x core=%#x\n", power, amplifier, core);
    return 1;
}

/* One group of four packed DSP words, or one unpacked 24-bit word. */
static int ampWriteVerified(uint8_t address, uint32_t reg, const uint8_t *data, unsigned chunk)
{
    uint8_t readback[20], packet[4 + sizeof(readback)];
    if (chunk != 4 && chunk != 12 && chunk != 20) return 0;
    packet[0] = (uint8_t)(reg >> 24);
    packet[1] = (uint8_t)(reg >> 16);
    packet[2] = (uint8_t)(reg >> 8);
    packet[3] = (uint8_t)reg;
    memcpy(packet + 4, data, chunk);
    gBus.address = address;
    if (geni_i2c_start_write_buffer(&gBus, packet, 4 + chunk) || !ampTransferDone() ||
        geni_i2c_start_read_buffer(&gBus, packet, 4, readback, chunk) || !ampTransferDone()) {
        printf("FUXI AMP: HALO transfer failed reg=%#x bytes=%u error=%u\n", reg, chunk, gBus.error);
        return 0;
    }
    for (unsigned i = 0; i < chunk; i++) {
        if (readback[i] != data[i]) {
            if (reg >= 0x02800000u && reg < 0x02c00000u) {
                printf("FUXI AMP: control readback differs reg=%#x byte=%u\n", reg, i);
                return 0;
            }
            uint32_t independent = 0;
            int ok = ampRead(address, reg + (i & ~3u), &independent);
            printf("FUXI AMP: HALO readback differs reg=%#x byte=%u want=%#x got=%#x independent[%u]=%#x\n",
                reg, i, data[i], readback[i], ok, independent);
            return 0;
        }
    }
    return 1;
}

/* Xiaomi SM8550 wm_halo_setup_algs reads the runtime directory through
 * UNPACK24 XM. These values are from the pinned Fuxi protection WMFW.
 * Check the complete directory before using its algorithm bases. */
static int ampBottomTuning(uint8_t address, const uint8_t *cal)
{
    static const uint32_t directory[] = {
        0x40000, 0x30000, 2, 0x400a4, 0x2b01, 0x1f4, 0xe, 0, 0, 2,
        0xcd, 0x1d3f01, 0x94, 0x50, 0xe, 0x7bc, 0xf20b, 0x100, 0x176, 0x4c, 0, 0, 0xbedead
    };
    for (unsigned i = 0; i < sizeof(directory) / sizeof(directory[0]); i++) {
        uint32_t value = 0;
        if (!ampRead(address, 0x02800000u + 4*i, &value) || value != directory[i]) {
            printf("FUXI AMP: muted HALO directory mismatch word=%u got=%#x expected=%#x\n", i, value, directory[i]);
            return 0;
        }
    }
    printf("FUXI AMP: muted HALO directory verified CSPL id=0xcd version=0x1d3f01\n");
    /* Xiaomi wm_adsp_load_coeff: first map the algorithm's word base,
     * then add WMDR's byte offset. Both records fit CSPL's 0x7bc YM words. */
    uint32_t packed = 0x02c00000u + ((directory[14]*3u) & ~3u) + 308;
    uint32_t unpacked = 0x03400000u + directory[14]*4u + 6008;
    for (unsigned i = 0; i < 4200; i += 12)
        if (!ampWriteVerified(address, packed + i, cal + 12 + i, 12)) return 0;
    if (!ampWriteVerified(address, unpacked, cal + 4212, 4)) return 0;
    /* cs35l41-cal.c writes CAL_R, CAL_CHECKSUM, then CAL_STATUS = 1 for
     * protection firmware. CAL_AMBIENT belongs to calibration firmware. */
    uint32_t xm = 0x02800000u + 4u * directory[12];
    static const uint8_t status[] = {0, 0, 0, 1};
    if (!ampWriteVerified(address, xm + 6*4, cal + 4, 4) ||
        !ampWriteVerified(address, xm + 9*4, cal + 8, 4) ||
        !ampWriteVerified(address, xm + 8*4, status, 4) || !ampOutputDisabled(address, 0)) return 0;
    printf("FUXI AMP: muted bottom tuning verified bytes=4204 calibration=3 controls\n");
    return 1;
}

/* ---- mode 6: the protection firmware run, muted ---- */
/* One whole register, written without a readback: test key, mailbox, sticky status bits, MPU. */
static int ampWrite32(uint8_t address, uint32_t reg, uint32_t value)
{
    const uint8_t packet[8] = {(uint8_t)(reg >> 24), (uint8_t)(reg >> 16), (uint8_t)(reg >> 8), (uint8_t)reg,
        (uint8_t)(value >> 24), (uint8_t)(value >> 16), (uint8_t)(value >> 8), (uint8_t)value};
    gBus.address = address;
    if (!geni_i2c_start_write_bytes(&gBus, packet, sizeof(packet)) && ampTransferDone()) return 1;
    printf("FUXI AMP: write failed reg=%#x error=%u\n", reg, gBus.error);
    return 0;
}

static int ampRmw(uint8_t address, uint32_t reg, uint32_t mask, uint32_t value)
{
    uint32_t was = 0;
    return ampRead(address, reg, &was) && ampWrite32(address, reg, (was & ~mask) | (value & mask));
}

/* Linux v6.12 cs35l41-lib.c otp_map_1 (OTPID 1 or 8: from word 2 bit 16, LSB first). The same 99 fields as early Fuxi
 * au_amp_on.h au_otp_map1, compared entry by entry on 2026-11-19; that sequence ran on this amplifier before its tones.
 * Address 0 only uses up its bits. */
static const struct { uint32_t reg; uint8_t shift, size; } kOtpMap1[99] = {
    {0x02030, 0, 4},{0x02030, 7, 1},{0x0208c, 24, 6},{0x02090, 14, 4},{0x02090, 10, 4},{0x0300c, 11, 4},
    {0x0394c, 23, 2},{0x03950, 0, 7},{0x03950, 8, 7},{0x03950, 16, 8},{0x03950, 24, 8},{0x03954, 0, 7},
    {0x03954, 8, 7},{0x03954, 16, 8},{0x03954, 24, 8},{0x03958, 0, 7},{0x03958, 8, 7},{0x03958, 16, 8},
    {0x03958, 24, 8},{0x0395c, 0, 7},{0x0395c, 8, 7},{0x0395c, 16, 8},{0x0395c, 24, 8},{0x0416c, 0, 8},
    {0x04160, 0, 7},{0x0416c, 8, 8},{0x04160, 16, 10},{0x0416c, 16, 12},{0x0416c, 28, 1},{0x04170, 0, 6},
    {0x04170, 6, 1},{0x04170, 8, 6},{0x04170, 14, 1},{0x04170, 16, 9},{0x04360, 0, 5},{0x04360, 6, 9},
    {0x04448, 0, 8},{0x04448, 8, 8},{0x04448, 16, 8},{0x04448, 24, 8},{0x0444c, 0, 3},{0x06e30, 0, 5},
    {0x06e30, 8, 5},{0x06e30, 16, 5},{0x06e30, 24, 5},{0x06e34, 0, 5},{0x06e34, 8, 5},{0x06e34, 16, 5},
    {0x06e34, 24, 5},{0x06e38, 0, 5},{0x06e38, 8, 5},{0x06e38, 16, 5},{0x06e38, 24, 5},{0x06e3c, 0, 5},
    {0x06e3c, 8, 5},{0x06e3c, 16, 5},{0x06e3c, 24, 5},{0x06e40, 0, 5},{0x06e40, 8, 5},{0x06e40, 16, 5},
    {0x06e40, 24, 5},{0x06e44, 0, 5},{0x06e48, 0, 10},{0x06e48, 10, 10},{0x06e48, 20, 10},{0x06e4c, 0, 10},
    {0x06e4c, 10, 10},{0x06e4c, 20, 10},{0x06e50, 0, 10},{0x06e50, 10, 10},{0x06e50, 20, 10},{0x06e54, 0, 10},
    {0x06e54, 10, 10},{0x06e54, 20, 10},{0x06e58, 0, 10},{0x06e58, 10, 10},{0x06e58, 20, 10},{0x06e5c, 0, 10},
    {0x06e5c, 10, 10},{0x06e5c, 20, 10},{0x06e60, 0, 10},{0x06e60, 10, 10},{0x06e60, 20, 10},{0x06e64, 0, 10},
    {0x07418, 7, 5},{0x0741c, 0, 5},{0x0741c, 11, 4},{0x0741c, 19, 4},{0x07434, 17, 1},{0x07434, 18, 7},
    {0x07068, 0, 9},{0x0410c, 7, 1},{0x0400c, 0, 7},{0x00000, 0, 1},{0x17040, 0, 8},{0x17040, 8, 8},
    {0x17040, 16, 8},{0x17040, 24, 8},{0x17044, 0, 24},
};

/* cs35l41_otp_unpack. The caller holds the test key open. Some trims read back otherwise, so only the bus is checked. */
static int ampOtpUnpack(uint8_t address, unsigned *fields)
{
    uint32_t words[32];
    unsigned bit = 80;
    for (unsigned i = 0; i < 32; i++)
        if (!ampRead(address, FM_OTP_MEM0 + 4u * i, &words[i])) return 0;
    for (unsigned i = 0; i < sizeof(kOtpMap1) / sizeof(kOtpMap1[0]); i++) {
        uint32_t value = 0;
        for (unsigned b = 0; b < kOtpMap1[i].size; b++, bit++) {
            if (bit >= 32u * 32u) return 0;
            value |= ((words[bit / 32u] >> (bit % 32u)) & 1u) << b;
        }
        if (!kOtpMap1[i].reg) continue;
        const uint32_t mask = ((1u << kOtpMap1[i].size) - 1u) << kOtpMap1[i].shift;
        if (!ampRmw(address, kOtpMap1[i].reg, mask, value << kOtpMap1[i].shift)) return 0;
        ++*fields;
    }
    return 1;
}

/* Linux cs35l41_register_errata_patch (B2) and cs35l41_otp_unpack under the test key, as early Fuxi au_on_prepare steps 6-11.
 * PWR_CTRL2 and AMP_GAIN_CTRL are among the errata zeros. The key is locked again on every path. */
static int ampB2Prepare(uint8_t address)
{
    static const uint32_t errata[][2] = {
        {0x4100, 0}, {0x4310, 0}, {0x4400, 0}, {0x381c, 0x51}, {0x02bc20e0, 0}, {0x02bc2020, 0},
        {FM_PWR_CTRL2, 0}, {0x6c04, 0}, {0x4c28, 0}, {0x4c2c, 0},
    };
    uint32_t otp = 0, power = 1;
    unsigned fields = 0;
    if (!ampRead(address, FM_OTPID, &otp) || (otp != 1u && otp != 8u) || !ampRead(address, FM_PWR_CTRL1, &power) ||
        (power & 1u)) {
        printf("FUXI AMP: B2 prepare refused otpid=%#x global=%#x\n", otp, power);
        return 0;
    }
    int ok = ampWrite32(address, FM_TEST_KEY, 0x55) && ampWrite32(address, FM_TEST_KEY, 0xaa);
    for (unsigned i = 0; ok && i < sizeof(errata) / sizeof(errata[0]); i++) ok = ampWrite32(address, errata[i][0], errata[i][1]);
    ok = ok && ampWrite32(address, FM_CCM_CORE_CTRL, 0) && ampOtpUnpack(address, &fields);
    const int locked = ampWrite32(address, FM_TEST_KEY, 0xcc) && ampWrite32(address, FM_TEST_KEY, 0x33);
    printf("FUXI AMP: B2 errata %s otpid=%u trims=%u test key %s\n", ok ? "applied" : "failed", otp, fields,
        locked ? "locked" : "lock failed");
    return ok && locked;
}

/* GLOBAL_EN (PWR_CTRL1 bit 0) and AMP_EN (PWR_CTRL2 bit 0) both clear: no output stage, no boost. */
static int ampNoOutput(uint8_t address)
{
    uint32_t power = 1, amplifier = 1;
    if (ampRead(address, FM_PWR_CTRL1, &power) && ampRead(address, FM_PWR_CTRL2, &amplifier) && !((power | amplifier) & 1u))
        return 1;
    printf("FUXI AMP: output check refused global=%#x amplifier=%#x\n", power, amplifier);
    return 0;
}

/* Xiaomi cs35l41.c cs35l41_set_csplmboxcmd: clear both sticky mailbox bits, unmask the DSP's, write the command, poll the
 * acknowledgement and DSP_MBOX_2 (Linux cs35l41_set_cspl_mbox_cmd polls 5 x 1 ms; 10 here), put the mask back. */
static int ampMailbox(uint8_t address, uint32_t command, uint32_t want, uint32_t *status, unsigned *ack)
{
    uint32_t mask = 0, sticky = 0;
    *status = 0xffffffffu;
    *ack = 0;
    if (!ampWrite32(address, FM_IRQ2_STATUS2, FM_MBOX_DRV) || !ampWrite32(address, FM_IRQ1_STATUS2, FM_MBOX_FW) ||
        !ampRead(address, FM_IRQ2_MASK2, &mask) || !ampWrite32(address, FM_IRQ2_MASK2, mask & ~FM_MBOX_DRV)) return 0;
    int ok = ampWrite32(address, FM_MBOX_CMD, command);
    for (unsigned i = 0; ok && i < 10 && *status != want; i++) {
        IOSleep(1);
        ok = ampRead(address, FM_IRQ1_STATUS2, &sticky) && ampRead(address, FM_MBOX_STS, status);
        if (ok && (sticky & FM_MBOX_FW) && !*ack) *ack = i + 1;
    }
    if (ok && *ack) ok = ampWrite32(address, FM_IRQ1_STATUS2, FM_MBOX_FW);
    ok = ampWrite32(address, FM_IRQ2_MASK2, mask) && ok;
    return ok && *status == want;
}

/* Linux v6.12 cs35l41_hda.c cs35l41_smart_amp after the load: fs errata (cs35l41_configure_cs_dsp), cs_dsp_run (MPU with
 * lock_regions 0xffffffff, cs_dsp_halo_start_core), HALO_STATE RUN (2), DSP_MBOX_2 RUNNING or PAUSED, then PAUSE. Xiaomi
 * wm_halo_apply_calibration also sets MAX_LRCLK_DELAY 0x20 before the run. The HDA driver runs this with GLOBAL_EN clear and
 * no stream; so does this. Then cs_dsp_stop: watchdog off, CORE_EN clear, soft reset; the caller holds the amp in reset.
 * 成功時把八個控制值（順序同 controls[]）複製到 out，mode 7 依此決定能否出聲。 */
static int ampRunMutedProtection(uint8_t address, uint32_t out[8])
{
    static const uint8_t lrclk[4] = {0, 0, 0, 0x20};
    static const uint16_t mpu[20] = {0x00, 0x04, 0x08, 0x0c, 0x14, 0x18, 0x1c, 0x20, 0x24, 0x2c,
        0x30, 0x34, 0x38, 0x3c, 0x44, 0x48, 0x4c, 0x50, 0x54, 0x5c};
    static const uint32_t controls[8] = {FM_FIRMWARE_STATE, FM_CSPL_STATE, FM_CSPL_ERRORNO, FM_CSPL_TEMPERATURE, FM_CAL_R,
        FM_CAL_STATUS, FM_CAL_R_SELECTED, FM_CAL_SET_STATUS};
    uint32_t pll = 0, dspclk = 0, core = 0, state = 0, beat0 = 0, beat1 = 0, mbox = 0xffffffffu, paused = 0xffffffffu;
    uint32_t value[8] = {0}, irq[4] = {0};
    unsigned ms = 0, ack = 0, mismatched = 0, stage = 1;
    int ok = 0, started = 0;

    if (!ampNoOutput(address) || !ampRead(address, FM_PLL_CLK_CTRL, &pll) || !ampRead(address, FM_DSP_CLK_CTRL, &dspclk) ||
        !ampWriteVerified(address, FM_MAX_LRCLK_DELAY, lrclk, 4)) goto stop;
    stage = 2;
    for (unsigned i = 0; i < 16; i++) {
        const uint32_t reg = (i < 8 ? FM_HALO_RX1_RATE : FM_HALO_TX1_RATE) + 8u * (i % 8u);
        uint32_t got = 0;
        if (!ampWrite32(address, reg, 1) || !ampRead(address, reg, &got)) goto stop;
        if (got != 1) mismatched++;
    }
    stage = 3;
    if (!ampWrite32(address, FM_HALO_MPU_LOCK, 0x5555) || !ampWrite32(address, FM_HALO_MPU_LOCK, 0xaaaa)) goto stop;
    for (unsigned i = 0; i < 20; i++)
        if (!ampWrite32(address, FM_HALO_MPU + mpu[i], 0xffffffffu)) goto stop;
    if (!ampWrite32(address, FM_HALO_MPU_LOCK, 0)) goto stop;
    stage = 4;
    if (!ampRead(address, FM_HALO_STATE, &state) || !ampRead(address, FM_HALO_HEARTBEAT, &beat0) || !ampNoOutput(address))
        goto stop;
    printf("FUXI AMP: protection run start pll=%#x dspclk=%#x rates=16 mismatched=%u halo_state=%#x heartbeat=%#x\n", pll,
        dspclk, mismatched, state, beat0);
    started = 1;
    if (!ampRmw(address, FM_CCM_CORE_CTRL, FM_HALO_CORE_RESET | FM_HALO_CORE_EN, FM_HALO_CORE_RESET | FM_HALO_CORE_EN) ||
        !ampRmw(address, FM_CCM_CORE_CTRL, FM_HALO_CORE_RESET, 0) || !ampRead(address, FM_CCM_CORE_CTRL, &core)) goto stop;
    stage = 5;
    for (ms = 0;; ms++) {
        if (!ampRead(address, FM_HALO_STATE, &state)) goto stop;
        if (state == 2 || ms >= 100) break;
        IOSleep(1);
    }
    if (state != 2) goto stop;
    stage = 6;
    if (!ampRead(address, FM_HALO_HEARTBEAT, &beat0)) goto stop;
    IOSleep(20);
    if (!ampRead(address, FM_HALO_HEARTBEAT, &beat1) || !ampRead(address, FM_MBOX_STS, &mbox) || mbox > 1) goto stop;
    stage = 7;
    if (!ampMailbox(address, 1 /* CSPL_MBOX_CMD_PAUSE */, 1 /* CSPL_MBOX_STS_PAUSED */, &paused, &ack)) goto stop;
    stage = 8;
    for (unsigned i = 0; i < 8; i++)
        if (!ampRead(address, controls[i], &value[i])) goto stop;
    if (!ampNoOutput(address)) goto stop;
    printf("FUXI AMP: protection controls firmware_state=%#x cspl_state=%#x errorno=%#x temperature=%#x cal_r=%#x "
        "cal_status=%#x cal_r_selected=%#x cal_set_status=%#x\n", value[0], value[1], value[2], value[3], value[4], value[5],
        value[6], value[7]);
    ok = 1;
stop:;
    /* A bus failure here still fails the run: the status is unread or the core may still be on (the caller resets the amp). */
    int clean = 1;
    for (unsigned i = 0; i < 4; i++)
        if (!ampRead(address, FM_IRQ1_STATUS1 + 4u * i, &irq[i])) { irq[i] = 0xdeadu; clean = 0; }
    if (started) {
        const int stopped = ampRmw(address, FM_HALO_WDT, 1, 0) && ampRmw(address, FM_CCM_CORE_CTRL, FM_HALO_CORE_EN, 0) &&
            ampRmw(address, FM_HALO_SOFT_RESET, 1, 1) && ampRead(address, FM_CCM_CORE_CTRL, &core);
        printf("FUXI AMP: protection core %s ccm=%#x irq1=%#x/%#x/%#x/%#x\n", stopped ? "stopped" : "stop failed", core,
            irq[0], irq[1], irq[2], irq[3]);
        clean = clean && stopped;
    }
    if (ok && !clean) {
        ok = 0;
        stage = 9;
    }
    if (ok) {
        printf("FUXI AMP: protection running confirmed halo_state=2 after %u ms heartbeat %#x->%#x mbox %u paused %u ack %u, "
            "output off\n", ms, beat0, beat1, mbox, paused, ack);
        memcpy(out, value, sizeof(value));
    } else {
        printf("FUXI AMP: protection run refused stage=%u halo_state=%#x ms=%u mbox=%#x paused=%#x ack=%u core=%#x "
            "irq1=%#x/%#x/%#x/%#x\n", stage, state, ms, mbox, paused, ack, core, irq[0], irq[1], irq[2], irq[3]);
    }
    return ok;
}

/* Stock section boundaries are checked in full before the first bus write. 回傳 1 表示這個模式的每一步都通過。 */
static int ampLoadMutedProtection(uint8_t address, unsigned mode)
{
    static const struct { uint32_t reg, length, group; } regions[] = {
        {0x02000000, 72, 12}, {0x0200009c, 528, 12}, {0x020003cc, 1596, 12},
        {0x02000a14, 1260, 12}, {0x02c00000, 6000, 12}, {0x03800000, 20420, 20},
    };
    const unsigned count = mode >= 4 ? sizeof(regions) / sizeof(regions[0]) : 2;
    const void *property;
    DTEntry chosen;
    unsigned length;
    uint32_t header[2];
    unsigned verified = 0;
    const uint8_t *cal = NULL;
    uint32_t controls[8] = {0};
    if (mode < 3 || mode > 7 || address != kAddr[0] ||
        SecureDTLookupEntry(NULL, mode >= 4 ? "/chosen/fuxi-speaker" : "/chosen", &chosen) != kSuccess ||
        SecureDTGetProperty(chosen, "fuxi,cs35l41-prot", &property, &length) != kSuccess ||
        length < sizeof(header)) goto refused;
    const uint8_t *plan = property;
    memcpy(header, plan, sizeof(header));
    if (header[0] != (mode >= 4 ? 0x32505343u : 0x31505343u) || header[1] != count) goto refused;
    if (mode >= 5) {
        unsigned size;
        if (SecureDTGetProperty(chosen, "fuxi,cs35l41-cal", &property, &size) != kSuccess || size != 4216) goto refused;
        cal = property;
        uint32_t resistance, checksum;
        memcpy(&resistance, cal + 4, 4);
        memcpy(&checksum, cal + 8, 4);
        resistance = __builtin_bswap32(resistance);
        if (memcmp(cal, "CSB1", 4) || !resistance || resistance >= 0xffffff ||
            __builtin_bswap32(checksum) != resistance + 1) goto refused;
    }
    unsigned offset = sizeof(header);
    for (unsigned i = 0; i < count; i++) {
        if (length - offset < sizeof(header)) goto refused;
        memcpy(header, plan + offset, sizeof(header));
        offset += sizeof(header);
        if (header[0] != regions[i].reg || header[1] != regions[i].length ||
            header[1] > length - offset) goto refused;
        offset += header[1];
    }
    if (offset != length || (mode >= 6 && !ampB2Prepare(address)) || !ampOutputDisabled(address, 1)) goto refused;

    offset = sizeof(header);
    for (unsigned i = 0; i < count; i++) {
        offset += sizeof(header);
        for (unsigned position = 0; position < regions[i].length;) {
            uint32_t reg = regions[i].reg + position;
            /* HALO packed XM: four 24-bit DSP words span three 32-bit
             * registers (Cirrus cs_dsp_test_bin.c). Keep each group together. */
            unsigned chunk = regions[i].group;
            if (!ampWriteVerified(address, reg, plan + offset + position, chunk)) goto refused;
            position += chunk;
            verified += chunk;
        }
        offset += regions[i].length;
        if (mode >= 4) printf("FUXI AMP: muted HALO region reg=%#x verified=%u\n", regions[i].reg, regions[i].length);
    }
    if (!ampOutputDisabled(address, 0)) goto refused;
    if (mode >= 4) printf("FUXI AMP: full muted HALO verified bytes=%u sections=%u\n", verified, count);
    else printf("FUXI AMP: muted HALO XM probe verified bytes=%u sections=%u\n", verified, count);
    if (mode >= 5 && !ampBottomTuning(address, cal)) goto refused;
    if (mode >= 6 && !ampRunMutedProtection(address, controls)) goto refused;
    /* 出聲前的閘門：保護韌體在跑（CSPL_ST_RUNNING = 0，Xiaomi cs35l41.h）、沒有錯誤碼、校正已套用。 */
    if (mode == 7 && (controls[1] != 0 || controls[2] != 0 || controls[5] != 1)) {
        printf("FUXI AMP: tone refused cspl_state=%#x errorno=%#x cal_status=%#x\n", controls[1], controls[2], controls[5]);
        goto refused;
    }
    return 1;
refused:
    printf("FUXI AMP: muted HALO firmware refused verified=%u, resetting lower amp\n", verified);
    return 0;
}

/* The survey reads the engine only with every branch plainly on (CLK_OFF clear, hardware control or not): a read of an unclocked
 * block stalls the bus. */
static int branchesOn(void)
{
    for (unsigned i = 0; i < 5; ++i) if (gcc(kHalts[i]) & FM_CLK_OFF) return 0;
    return 1;
}

static void survey(int *running)
{
    uint32_t off = 0;
    const int run = branchesRun(&off) && branchesOn();
    const int level = level19m2();
    printf("FUXI AMP: survey vote 0x52008=%#x 0x52010=%#x gpll0=%#x/%#x branches core=%#x s_ahb=%#x m_ahb=%#x core2x=%#x s0=%#x "
        "(off mask %#x) rcg cmd=%#x dfsr=%#x level19.2=%d gic_se_en=%d\n", gcc(FM_VOTE1), gcc(FM_VOTE2), gcc(FM_GPLL0_VOTE),
        gcc(FM_GPLL0_MODE), gcc(kHalts[0]), gcc(kHalts[1]), gcc(kHalts[2]), gcc(kHalts[3]), gcc(kHalts[4]), off, gcc(FM_RCG),
        gcc(FM_RCG + 0x14), level, gicEnabled(FM_INTID_SE));
    printf("FUXI AMP: pins gpio0 %#x/%#x gpio1 %#x/%#x gpio183 %#x/%#x gpio182 %#x/%#x (ctl/io)\n", pinCtl(0), pinIo(0), pinCtl(1),
        pinIo(1), pinCtl(2), pinIo(2), pinCtl(3), pinIo(3));
    if (run) {
        printf("FUXI AMP: engine fw=%#x proto=%u if_disable=%#x (%s) clk_cfg=%#x clk_sel=%#x scl=%#x hw0=%#x hw1=%#x m_irq=%#x/%#x\n",
            ioRd(NULL, GENI_FW_REVISION), (ioRd(NULL, GENI_FW_REVISION) >> 8) & 0xff, ioRd(NULL, GENI_IF_DISABLE),
            (ioRd(NULL, GENI_IF_DISABLE) & 1u) ? "FIFO locked: GSI only" : "FIFO usable", ioRd(NULL, GENI_CLK_CFG), ioRd(NULL, 0x7c),
            ioRd(NULL, GENI_SCL_COUNTERS), ioRd(NULL, GENI_HW_PARAM_0), ioRd(NULL, GENI_HW_PARAM_1), ioRd(NULL, GENI_M_IRQ_STATUS),
            ioRd(NULL, GENI_M_IRQ_EN));
    } else {
        printf("FUXI AMP: engine not read: QUP2 clocks off\n");
    }
    *running = run;
}

/* ---- mode 7：下方喇叭出聲短測 ---- */
/* 早期 au_on_set：整個暫存器寫入後讀回，不同就停。 */
static int ampSet(uint8_t address, uint32_t reg, uint32_t value)
{
    uint32_t got = 0;
    if (ampWrite32(address, reg, value) && ampRead(address, reg, &got) && got == value) return 1;
    printf("FUXI AMP: tone write refused reg=%#x want=%#x got=%#x\n", reg, value, got);
    return 0;
}

/* 早期 au_on_rmw：讀、改、寫回，再讀回比對。 */
static int ampSetBits(uint8_t address, uint32_t reg, uint32_t mask, uint32_t value)
{
    uint32_t was = 0;
    if (!ampRead(address, reg, &was)) {
        printf("FUXI AMP: tone read failed reg=%#x\n", reg);
        return 0;
    }
    return ampSet(address, reg, (was & ~mask) | (value & mask));
}

/* au_on_prepare steps 12–26（steps 6–11 的 B2 errata 與 OTP 已由 ampB2Prepare 做過），right_slot、不開 VI 監看。
 * 全程 GLOBAL_EN 關、音量靜音、類比增益最低；PLL 參考時脈設成 BCLK。 */
static int ampTonePrepare(uint8_t address)
{
    static const uint32_t sets[][2] = {
        {FM_PWR_CTRL2, 0x20}, {FM_AMP_GAIN, 0}, {FM_DIG_VOL, FM_DIG_VOL_MUTE},                    /* 15–16 */
        {0x2c0c, 3}, {0x4804, 0x28}, {0x4808, 0x20200000u}, {0x4840, 0x18}, {0x4830, 0x18},       /* 17–19 */
        {0x4820, 0x401}, {0x480c, 3}, {0x4c00, 8}, {0x4c20, 0}, {0x4c24, 0}, {0x4c28, 0}, {0x4c2c, 0}, /* 20–22 */
        {0x2d10, 0x18010}, {0x4800, 0x10000},                                                    /* 23–24 */
    };
    static const uint32_t pll[6][2] = {{0x800, 0x800}, {0x7e0, 0x28u << 5}, {0x10, 0}, {0x7, 0}, {0x800, 0}, {0x10, 0x10}};
    uint32_t status = 0;
    if (!ampNoOutput(address)) return 0;
    /* 12：舊的中斷旗標寫回清掉 */
    for (unsigned i = 0; i < 4; i++)
        if (!ampRead(address, FM_IRQ1_STATUS1 + 4u * i, &status) || !ampWrite32(address, FM_IRQ1_STATUS1 + 4u * i, status))
            return 0;
    /* 13–14：升壓 1000 nH、15 uF、峰值 4000 mA，先於任何能開升壓的寫入 */
    if (!ampSetBits(address, 0x3810, 0xffff, 0x2424) || !ampSetBits(address, 0x3814, 0xff03, 0x7500) ||
        !ampSetBits(address, 0x3808, 0x7f, 0x40)) return 0;
    if (!ampRead(address, 0x3800, &status) || status != 0) {
        printf("FUXI AMP: tone refused 0x3800=%#x\n", status);
        return 0;
    }
    for (unsigned i = 0; i < sizeof(sets) / sizeof(sets[0]); i++)
        if (!ampSet(address, sets[i][0], sets[i][1])) return 0;
    for (unsigned i = 0; i < 6; i++)
        if (!ampSetBits(address, FM_PLL_CLK_CTRL, pll[i][0], pll[i][1])) return 0;
    return ampSet(address, FM_PWR_CTRL2, 0x21); /* 26：AMP_EN，GLOBAL_EN 仍關 */
}

/* au_on_power_up steps 28–31：TDM 時脈已在跑。PUP_DONE 最多等 100 ms，PLL 要鎖住。 */
static int ampTonePowerUp(uint8_t address, unsigned *ms, uint32_t *status3)
{
    uint32_t status = 0;
    *status3 = 0;
    const int unlocked = ampWrite32(address, FM_TEST_KEY, 0x55) && ampWrite32(address, FM_TEST_KEY, 0xaa);
    const int testOk = unlocked && ampWrite32(address, FM_TONE_TEST, 0x002f1aa0u);
    const int locked = unlocked && ampWrite32(address, FM_TEST_KEY, 0xcc) && ampWrite32(address, FM_TEST_KEY, 0x33);
    if (!testOk || !locked || !ampSetBits(address, FM_PWR_CTRL1, 1, 1)) return 0;
    for (*ms = 0;; ++*ms) {
        if (!ampRead(address, FM_IRQ1_STATUS1, &status) || (status & FM_TONE_ERRORS)) {
            printf("FUXI AMP: tone power-up fault irq1_status1=%#x after %u ms\n", status, *ms);
            return 0;
        }
        if (status & FM_TONE_PUP_DONE) break;
        if (*ms >= 100) {
            printf("FUXI AMP: tone power-up timeout irq1_status1=%#x\n", status);
            return 0;
        }
        IOSleep(1);
    }
    if (!ampWrite32(address, FM_IRQ1_STATUS1, FM_TONE_PUP_DONE) || !ampRead(address, FM_IRQ1_STATUS3, status3)) return 0;
    if ((*status3 & FM_TONE_PLL_LOCK) && !(*status3 & (FM_TONE_PLL_UNLOCK | FM_OTP_BOOT_ERR))) return 1;
    printf("FUXI AMP: tone PLL not locked irq1_status3=%#x\n", *status3);
    return 0;
}

/* au_on_check：四個狀態字都留著，STATUS1 有故障位元就停。 */
static int ampToneCheck(uint8_t address, uint32_t status[4])
{
    for (unsigned i = 0; i < 4; i++)
        if (!ampRead(address, FM_IRQ1_STATUS1 + 4u * i, &status[i])) return 0;
    return !(status[0] & FM_TONE_ERRORS);
}

/* au_on_down：每一步都試，回傳是否全部成功且看到 PDN_DONE。之後呼叫端照樣把功放壓回 reset。 */
static int ampToneDown(uint8_t address)
{
    uint32_t status = 0;
    int ok = ampWrite32(address, FM_AMP_GAIN, 0);
    ok = ampWrite32(address, FM_DIG_VOL, FM_DIG_VOL_MUTE) && ok;
    ok = ampRmw(address, FM_PWR_CTRL1, 1, 0) && ok;
    int down = 0;
    for (unsigned i = 0; i < 100; i++) {
        if (!ampRead(address, FM_IRQ1_STATUS1, &status)) break;
        if ((down = (status & FM_TONE_PDN_DONE) != 0)) break;
        IOSleep(1);
    }
    ok = ampWrite32(address, FM_IRQ1_STATUS1, FM_TONE_PDN_DONE) && ok && down;
    const int unlocked = ampWrite32(address, FM_TEST_KEY, 0x55) && ampWrite32(address, FM_TEST_KEY, 0xaa);
    const int testOk = unlocked && ampWrite32(address, FM_TONE_TEST, 0x002f1aa3u);
    const int locked = unlocked && ampWrite32(address, FM_TEST_KEY, 0xcc) && ampWrite32(address, FM_TEST_KEY, 0x33);
    ok = testOk && locked && ok;
    ok = ampRmw(address, FM_PWR_CTRL2, 1, 0) && ok;
    ok = ampWrite32(address, 0x4800, 0) && ok;
    return ok;
}

/* 早期 au_tone_stage 的功放端順序：PCM 圖先送 100 ms 靜音（CLOCKED），steps 12–26，100 ms，上電，200 ms，檢查，-30 dB，
 * 正弦期間每 100 ms 檢查一次，正弦送完（DRAIN）後再檢查一次，下電。任何一步失敗：先標 FAILED 讓 PCM 停送正弦，再下電。
 * 呼叫端之後一律把功放壓回 reset。 */
static int ampTone(uint8_t address)
{
    uint32_t status[4] = {0}, status3 = 0, state = FUXI_TONE_PENDING;
    unsigned waited = 0, pup = 0, on = 0, checks = 0, stage = 1;
    int ok = 0, touched = 0, quiet = 1;

    if (!fuxi_amp_tone_move(FUXI_TONE_PENDING, FUXI_TONE_READY)) {
        printf("FUXI AMP: tone refused, PCM side already gave up (state %u)\n", fuxi_amp_tone_get());
        return 0;
    }
    printf("FUXI AMP: tone ready, bottom amplifier muted, waiting for the TDM clock\n");
    while ((state = fuxi_amp_tone_get()) == FUXI_TONE_READY && waited < FM_TONE_READY_MS) {
        IOSleep(5);
        waited += 5;
    }
    if (state == FUXI_TONE_READY && !fuxi_amp_tone_move(FUXI_TONE_READY, FUXI_TONE_FAILED)) state = fuxi_amp_tone_get();
    if (state != FUXI_TONE_CLOCKED) goto out;
    stage = 2;
    touched = 1;
    if (!ampTonePrepare(address)) goto out;
    IOSleep(100);
    stage = 3;
    if (!ampTonePowerUp(address, &pup, &status3)) goto out;
    IOSleep(200);
    stage = 4;
    if (!ampToneCheck(address, status)) goto out;
    stage = 5;
    if (!ampSet(address, FM_DIG_VOL, FM_DIG_VOL_LEVEL)) goto out;
    stage = 6;
    if (!fuxi_amp_tone_move(FUXI_TONE_CLOCKED, FUXI_TONE_ON)) goto out;
    printf("FUXI AMP: tone on -30 dB after %u ms, pup %u ms, pll %#x, irq1=%#x/%#x/%#x/%#x\n", waited, pup, status3,
        status[0], status[1], status[2], status[3]);
    stage = 7;
    while ((state = fuxi_amp_tone_get()) == FUXI_TONE_ON && on < FM_TONE_ON_MS) {
        IOSleep(10);
        on += 10;
        if (on % 100u == 0) {
            checks++;
            if (!ampToneCheck(address, status)) break;
        }
    }
    if (state != FUXI_TONE_DRAIN) goto out;
    stage = 8;
    if (!ampToneCheck(address, status)) goto out;
    ok = 1;
out:
    if (!ok && !fuxi_amp_tone_move(FUXI_TONE_DRAIN, FUXI_TONE_FAILED)) fuxi_amp_tone_abandon();
    if (touched) quiet = ampToneDown(address);
    if (ok && !(quiet && fuxi_amp_tone_move(FUXI_TONE_DRAIN, FUXI_TONE_OFF))) {
        ok = 0;
        stage = 9;
        fuxi_amp_tone_move(FUXI_TONE_DRAIN, FUXI_TONE_FAILED);
    }
    printf("FUXI AMP: tone %s stage=%u waited=%u ms on=%u ms checks=%u irq1=%#x/%#x/%#x/%#x down %s state=%u\n",
        ok ? "done" : "refused", stage, waited, on, checks, status[0], status[1], status[2], status[3],
        touched ? (quiet ? "ok" : "incomplete") : "not needed", fuxi_amp_tone_get());
    return ok;
}

/* Mode 2. Saved state is put back on every exit path. */
static void bringUp(int wasRunning, unsigned mode)
{
    uint32_t savedCtl[4], savedIo[4], tdmCtl[3] = {0}, tdmIo[3] = {0};
    for (unsigned i = 0; i < 4; ++i) { savedCtl[i] = pinCtl(i); savedIo[i] = pinIo(i); }
    for (unsigned i = 0; mode == 7 && i < 3; ++i) {
        tdmCtl[i] = rd(&gTdmPin[i], pinPa(kTdmPins[i]));
        tdmIo[i] = rd(&gTdmPin[i], pinPa(kTdmPins[i]) + 4);
    }
    const uint32_t savedVote2 = gcc(FM_VOTE2), savedGpll0 = gcc(FM_GPLL0_VOTE);
    int bus = 0, step = 0, tdmMuxed = 0;
    uint32_t config = 0;

    /* clocks */
    gccWr(FM_GPLL0_VOTE, savedGpll0 | 1u);
    for (unsigned i = 0; i < 10000 && !(gcc(FM_GPLL0_MODE) & FM_CLK_OFF); ++i) delay(10); /* bit 31: lock detect */
    gccWr(FM_VOTE2, savedVote2 | FM_VOTE2_BITS);
    int run = 0;
    for (unsigned i = 0; i < 2000 && !(run = branchesRun(NULL)); ++i) delay(10);
    step = 1;
    if (!run) goto out; /* never touch an engine without running clocks */

    /* pins: I2C function 2, 2 mA, pull-up (DT active state) */
    for (unsigned i = 0; i < 2; ++i) wr(&gPin[i], pinPa(kPins[i]), 2u << 2 | 0u << 6 | 3u);
    step = 2;

    /* the engine */
    const uint32_t proto = (ioRd(NULL, GENI_FW_REVISION) >> 8) & 0xff;
    if (proto != GENI_PROTO_I2C) goto out;
    step = 3;
    if (!(ioRd(NULL, GENI_CLK_CFG) & 1u) || !ioRd(NULL, GENI_SCL_COUNTERS)) {
        const int level = level19m2();
        if (level < 0) goto out;
        ioWr(NULL, 0x28, ioRd(NULL, 0x28) | 0x7fu);   /* clock gating controls on */
        ioWr(NULL, 0xe30, ioRd(NULL, 0xe30) | 0xfu);  /* DMA clock gating controls on */
        ioWr(NULL, 0x24, 0x7fu);                      /* default output control */
        ioWr(NULL, 0x20, 1u);                         /* force default */
        ioWr(NULL, 0xe18, 0);                         /* no GSI events */
        ioWr(NULL, 0x7c, (uint32_t)level);
        ioWr(NULL, GENI_CLK_CFG, FM_CLK_CFG);
        ioWr(NULL, GENI_SCL_COUNTERS, FM_SCL_COUNTERS);
    }
    step = 4;
    gicMask(FM_INTID_SE);
    if (gicEnabled(FM_INTID_SE)) goto out;
    const GeniIo io = {ioRd, ioWr, NULL};
    if (geni_i2c_prepare(&gBus, &io, kAddr[0], &config)) goto out; /* also refuses a GSI-only engine */
    bus = 1;
    step = 5;

    /* the amplifiers, one at a time */
    for (unsigned a = 0; a < (mode >= 3 ? 1u : 2u); ++a) {
        const unsigned p = pinIndex(kReset[a]);
        const uint32_t egpio = (savedCtl[p] & FM_EGPIO_PRESENT) ? FM_EGPIO_ENABLE : 0;
        wr(&gPin[p], pinPa(kReset[a]) + 4, 0);           /* output low */
        wr(&gPin[p], pinPa(kReset[a]), egpio | FM_RESET_CTL); /* APPS takes the pad (eGPIO), GPIO out, 16 mA */
        IOSleep(2);
        const uint32_t ctlNow = pinCtl(p), lowIn = pinIo(p) & 1u;
        wr(&gPin[p], pinPa(kReset[a]) + 4, 1u << 1);     /* release */
        IOSleep(10);
        const uint32_t highIn = pinIo(p) & 1u;
        uint32_t devid = 0, revid = 0, st3 = 0, st4 = 0;
        const int ok = ampRead(kAddr[a], 0x0, &devid);
        const int ok2 = ok && ampRead(kAddr[a], 0x4, &revid) && ampRead(kAddr[a], 0x10018, &st3) && ampRead(kAddr[a], 0x1001c, &st4);
        printf("FUXI AMP: amp %c@%#x %s devid=%#x%s revid=%#x irq1_status3=%#x irq1_status4=%#x (geni error %u status %#x done %u "
            "failed %u timeouts %u; reset gpio%u ctl %#x in low/high %u/%u)\n", kName[a], kAddr[a], ok2 ? "answers" : ok ? "partly" :
            "no answer", devid, devid == FM_DEVID ? " (CS35L41)" : "", revid, st3, st4, gBus.error, gBus.error_status, gBus.done,
            gBus.failed, gBus.timeouts, kReset[a], ctlNow, lowIn, highIn);
        if (mode >= 3) {
            if (ok2 && devid == FM_DEVID && revid == 0xb2 && !(st3 & FM_OTP_BOOT_ERR) && (st4 & FM_OTP_BOOT_DONE)) {
                if (ampLoadMutedProtection(kAddr[a], mode) && mode == 7) {
                    /* 早期 au_pin：TDM 三支腳切到 function 1、8 mA；eGPIO 就從 LPASS 取回。時脈要等 PCM 圖開了才有。 */
                    for (unsigned i = 0; i < 3; ++i) {
                        const uint32_t egpio = tdmCtl[i] & FM_EGPIO_PRESENT ? FM_EGPIO_PRESENT | FM_EGPIO_ENABLE : 0;
                        wr(&gTdmPin[i], pinPa(kTdmPins[i]), egpio | FM_TDM_CTL);
                    }
                    tdmMuxed = 1;
                    ampTone(kAddr[a]);
                }
            } else {
                printf("FUXI AMP: muted XM skipped; lower amp identity or OTP boot check failed\n");
            }
        }
        wr(&gPin[p], pinPa(kReset[a]) + 4, 0);           /* back into reset */
        if (tdmMuxed) {
            for (unsigned i = 0; i < 3; ++i) {
                wr(&gTdmPin[i], pinPa(kTdmPins[i]) + 4, tdmIo[i] & ~1u);
                wr(&gTdmPin[i], pinPa(kTdmPins[i]), tdmCtl[i]);
            }
            tdmMuxed = 0;
            printf("FUXI AMP: TDM pins restored gpio121 %#x gpio123 %#x gpio124 %#x\n", tdmCtl[0], tdmCtl[1], tdmCtl[2]);
        }
    }
    step = 6;
out:
    /* Put back what the boot stages left: reset pins first (still low while their old configuration returns), then the I2C pins and
     * the votes. A clock the boot stages had running stays running. */
    for (unsigned i = 0; i < 4; ++i) {
        wr(&gPin[i], pinPa(kPins[i]) + 4, savedIo[i] & ~1u);
        wr(&gPin[i], pinPa(kPins[i]), savedCtl[i]);
    }
    if (!wasRunning) {
        gccWr(FM_VOTE2, savedVote2);
        gccWr(FM_GPLL0_VOTE, savedGpll0);
    }
    printf("FUXI AMP: bring-up stopped at step %d (6 = both read; bus %s, config %#x); pins%s restored\n", step,
        bus ? "prepared" : "not prepared", config, wasRunning ? "" : " and votes");
}

/* Boot argument fuxiamp, else ADT /chosen u32 "fuxi,amp". */
static unsigned fm_mode(void)
{
    unsigned mode = 0;
    DTEntry chosen;
    const void *prop;
    unsigned bytes;
    if (PE_parse_boot_argn("fuxiamp", &mode, sizeof(mode))) return mode;
    if (SecureDTLookupEntry(NULL, "/chosen", &chosen) == kSuccess &&
        SecureDTGetProperty(chosen, "fuxi,amp", &prop, &bytes) == kSuccess && bytes == sizeof(uint32_t))
        memcpy(&mode, prop, sizeof(mode));
    return mode;
}

static void ampThread(void *arg, wait_result_t result)
{
    (void)result;
    const unsigned mode = (unsigned)(uintptr_t)arg;
    int ok = fm_map(&gGcc0) == KERN_SUCCESS && fm_map(&gGcc1c) == KERN_SUCCESS && fm_map(&gGcc20) == KERN_SUCCESS &&
        fm_map(&gGcc50) == KERN_SUCCESS && fm_map(&gSe) == KERN_SUCCESS && fm_map(&gGicd) == KERN_SUCCESS;
    for (unsigned i = 0; ok && i < 4; ++i) {
        gPin[i].pa = pinPa(kPins[i]) & ~(uint64_t)0x3fffu;
        gPin[i].size = 0x4000;
        for (unsigned j = 0; j < i; ++j) if (gPin[j].pa == gPin[i].pa) gPin[i].va = gPin[j].va; /* gpio0 and gpio1 share a page */
        ok = fm_map(&gPin[i]) == KERN_SUCCESS;
    }
    for (unsigned i = 0; ok && mode == 7 && i < 3; ++i) {
        gTdmPin[i].pa = pinPa(kTdmPins[i]) & ~(uint64_t)0x3fffu;
        gTdmPin[i].size = 0x4000;
        for (unsigned j = 0; j < i; ++j) if (gTdmPin[j].pa == gTdmPin[i].pa) gTdmPin[i].va = gTdmPin[j].va; /* 121、123 同頁 */
        ok = fm_map(&gTdmPin[i]) == KERN_SUCCESS;
    }
    if (!ok) {
        printf("FUXI AMP: mapping failed\n");
    } else {
        int running = 0;
        survey(&running);
        if (mode >= 2) bringUp(running, mode);
    }
    /* mode 7 任何沒走到 OFF 的路徑（對應失敗、身分或韌體不符、閘門拒絕）都讓 PCM 端別再等。 */
    if (mode == 7) {
        const uint32_t was = fuxi_amp_tone_abandon();
        if (was >= FUXI_TONE_PENDING && was <= FUXI_TONE_ON)
            printf("FUXI AMP: tone abandoned in state %u, PCM side plays zeros only\n", was);
    }
    thread_terminate(current_thread());
}

/* Called from fuxi_adsp_init (sweet_ufs.c, after the UFS driver is ready). */
void fuxi_amp_init(void)
{
    static int started;
    thread_t thread = NULL;
    const unsigned mode = fm_mode();
    if (!mode || mode > 7 || started) return;
    started = 1;
    /* 先於 ADSP 執行緒設好，PCM 圖一開始就知道要等功放。 */
    if (mode == 7) __atomic_store_n(&fuxi_amp_tone, FUXI_TONE_PENDING, __ATOMIC_RELEASE);
    if (kernel_thread_start(ampThread, (void *)(uintptr_t)mode, &thread) != KERN_SUCCESS) {
        if (mode == 7) fuxi_amp_tone_abandon();
        printf("FUXI AMP: no thread\n");
        return;
    }
    thread_deallocate(thread);
    printf("FUXI AMP: started mode %u\n", mode);
}
#endif /* FUXI */
