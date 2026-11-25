/* SPDX-License-Identifier: MIT */
/* fuxi_camera 的純邏輯（主機測試 tools/test-fuxi-camera.py 用同一份）：TLMM 腳位編碼、camcc RCG／GDSC 位元、RPMh VRM 命令、
 * 手動 I2C（bit-bang，開汲）引擎與 i2cdetect 式的探測規則。這裡不碰硬體：引擎經 fc_bus_ops 回呼放開／拉低腳位、讀電位。
 *
 * 參考：HakiOS hakios.camera.c（POCO X5 Pro，APSL 2.0）的「I2C by hand」：每個位元先放資料線再拉時脈、時脈放開後等
 * clock stretching、資料線被卡住時打 9 個時脈再 STOP（Linux i2c_generic_scl_recovery 同法）；這裡改成回呼形式以便主機模擬，
 * 另加錯誤碼與「EEPROM 範圍不送任何寫入位元組」的規則。位元定義對照 Linux pinctrl-msm.c／pinctrl-sm8550.c、clk-rcg2.c、
 * clk-branch.c、gdsc.c、rpmh-regulator.c、cmd-db.h（GPL，只對照位址與位元）與 i2cdetect（i2c-tools）的探測方式。 */
#ifndef FUXI_CAMERA_PROTO_H
#define FUXI_CAMERA_PROTO_H
#include <stdint.h>

/* ---- TLMM：一支腳 0x1000，CTL +0、IO +4 ---- */
#define FC_CTL_PULL_NONE 0u
#define FC_CTL_PULL_DOWN 1u
#define FC_CTL_PULL_UP 3u
#define FC_CTL_OE (1u << 9)
#define FC_CTL_EGPIO_PRESENT (1u << 11) /* 唯讀：這支腳可交給島區（LPASS／相機 AON） */
#define FC_CTL_EGPIO_ENABLE (1u << 12)  /* 設了才由 APPS 擁有（pinctrl-msm.c：選非 egpio_func 的功能時設） */
#define FC_IO_IN 1u
#define FC_IO_OUT 2u

static inline unsigned fc_ctl_func(uint32_t ctl) { return (ctl >> 2) & 0xfu; }
static inline unsigned fc_ctl_drive_ma(uint32_t ctl) { return (((ctl >> 6) & 7u) + 1u) * 2u; }
static inline unsigned fc_ctl_pull(uint32_t ctl) { return ctl & 3u; }
static inline uint32_t fc_drive_bits(unsigned ma)
{
    if (ma < 2) ma = 2;
    if (ma > 16) ma = 16;
    return ((ma / 2u - 1u) & 7u) << 6;
}
/* 新的 CTL：功能、驅動電流、上下拉、輸出致能；eGPIO 腳一律設 enable 由 APPS 接手（PRESENT 照抄回去，寫它沒有作用）。 */
static inline uint32_t fc_pin_ctl(uint32_t found, unsigned func, unsigned ma, unsigned pull, int output)
{
    const uint32_t egpio = (found & FC_CTL_EGPIO_PRESENT) ? FC_CTL_EGPIO_PRESENT | FC_CTL_EGPIO_ENABLE : 0u;
    return egpio | (output ? FC_CTL_OE : 0u) | fc_drive_bits(ma) | (func & 0xfu) << 2 | (pull & 3u);
}

/* ---- camcc／GCC：RCG、branch、GDSC ---- */
#define FC_RCG_CMD_UPDATE 1u
#define FC_RCG_CMD_ROOT_OFF (1u << 31)
#define FC_RCG_CFG_MASK 0x371fu /* [4:0] 分頻（2n-1）、[10:8] 來源、[13:12] M/N 模式 */
#define FC_CBCR_ENABLE 1u
#define FC_CBCR_HW_CTL (1u << 1) /* 硬體閘控（hwcg）：時脈由匯流排請求開關，CLK_OFF 閒置時就會是 1 */
#define FC_CBCR_OFF (1u << 31)
/* branch 是否可用。Linux clk-branch.c clk_branch_wait()：在 hwcg 模式（HW_CTL）跳過 CLK_OFF 檢查，只要 CLK_ENABLE；
 * 2026-11-20 趟實機 gcc camera ahb 讀 0x80000002、寫 enable 後 0x80000003（hwcg、閒置），Linux 就是在這個狀態讀 camcc。 */
static inline int fc_branch_is_on(uint32_t cbcr)
{
    return (cbcr & FC_CBCR_HW_CTL) ? (cbcr & FC_CBCR_ENABLE) != 0 : !(cbcr & FC_CBCR_OFF);
}
#define FC_GDSC_SW_COLLAPSE 1u
#define FC_GDSC_RETAIN_FF (1u << 11)
#define FC_GDSC_PWR_ON (1u << 31)
#define FC_GDSC_CFG_UP_DONE (1u << 16)   /* CFG_GDSCR（GDSCR+4）：POLL_CFG_GDSCR 的 GDSC 看這裡 */
#define FC_GDSC_CFG_DOWN_DONE (1u << 15)

/* MCLK 19.2 MHz：來源 0（XO，camcc parent_map_1 的 bi_tcxo）、分頻 1（hid 2*1-1）、不用 M/N；其他位元照舊。 */
static inline uint32_t fc_rcg_cfg(uint32_t found, unsigned src, unsigned hid)
{
    return (found & ~FC_RCG_CFG_MASK) | ((src & 7u) << 8) | (hid & 0x1fu);
}
static inline uint32_t fc_rcg_cfg_xo(uint32_t found) { return fc_rcg_cfg(found, 0u, 1u); }
static inline unsigned fc_rcg_src(uint32_t cfg) { return (cfg >> 8) & 7u; }
static inline unsigned fc_rcg_hid(uint32_t cfg) { return cfg & 0x1fu; }

/* ---- SM8550 cam_cc_pll0（Lucid OLE，對照 Linux clk-alpha-pll.c 與 camcc-sm8550.c） ---- */
#define FC_PLL_MODE 0x00u
#define FC_PLL_OPMODE 0x04u
#define FC_PLL_STATE 0x08u
#define FC_PLL_STATUS 0x0cu
#define FC_PLL_L_VAL 0x10u
#define FC_PLL_ALPHA 0x14u
#define FC_PLL_USER_CTL 0x18u
#define FC_PLL_USER_CTL_U 0x1cu
#define FC_PLL_CONFIG_CTL 0x20u
#define FC_PLL_CONFIG_CTL_U 0x24u
#define FC_PLL_CONFIG_CTL_U1 0x28u
#define FC_PLL_TEST_CTL 0x2cu
#define FC_PLL_TEST_CTL_U 0x30u
#define FC_PLL_TEST_CTL_U1 0x34u
#define FC_PLL_TEST_CTL_U2 0x38u
#define FC_PLL_OUTCTRL 1u
#define FC_PLL_RESET_N (1u << 2)
#define FC_PLL_LOCK_DET (1u << 31)
#define FC_PLL_OP_STANDBY 0u
#define FC_PLL_OP_RUN 1u
#define FC_PLL_OUT_MASK 0x7u
/* cam_cc_pll0_config：1200 MHz（L=0x3e, alpha=0x8000, cal=0x44, ringosc_cal=0x44；even=600, odd=400） */
#define FC_PLL0_L_VAL (0x3eu | (0x44u << 16) | (0x44u << 24))
#define FC_PLL0_ALPHA 0x8000u
#define FC_PLL0_USER_CTL 0x00008400u
#define FC_PLL0_USER_CTL_U 0x00000005u
#define FC_PLL0_CONFIG_CTL 0x20485699u
#define FC_PLL0_CONFIG_CTL_U 0x00182261u
#define FC_PLL0_CONFIG_CTL_U1 0x82aa299cu
#define FC_PLL0_TEST_CTL 0x00000000u
#define FC_PLL0_TEST_CTL_U 0x00000003u
#define FC_PLL0_TEST_CTL_U1 0x00009000u
#define FC_PLL0_TEST_CTL_U2 0x00000034u

static inline int fc_pll_is_enabled(uint32_t mode, uint32_t opmode)
{
    return (opmode & FC_PLL_OP_RUN) != 0u && (mode & FC_PLL_OUTCTRL) != 0u;
}
static inline unsigned fc_camss_hw_gen(uint32_t version) { return (version >> 28) & 0xfu; }
static inline unsigned fc_camss_hw_rev(uint32_t version) { return (version >> 16) & 0xfffu; }
static inline unsigned fc_camss_hw_step(uint32_t version) { return version & 0xffffu; }
static inline uint32_t fc_csiphy_pack_ver(uint32_t s12, uint32_t s13, uint32_t s14, uint32_t s15)
{
    return (s12 & 0xffu) | ((s13 & 0xffu) << 8) | ((s14 & 0xffu) << 16) | ((s15 & 0xffu) << 24);
}
/* 這顆 GDSC 是否通電（titan_top_gdsc 帶 POLL_CFG_GDSCR：只看 CFG_GDSCR 的 power-up complete；沒折疊才算） */
static inline int fc_gdsc_is_on(uint32_t gdscr, uint32_t cfg) { return !(gdscr & FC_GDSC_SW_COLLAPSE) && (cfg & FC_GDSC_CFG_UP_DONE); }
static inline int fc_gdsc_is_off(uint32_t gdscr, uint32_t cfg) { return (gdscr & FC_GDSC_SW_COLLAPSE) && (cfg & FC_GDSC_CFG_DOWN_DONE); }

/* ---- RPMh VRM（cmd-db 位址的 [19:16] 是 slave id：ARC 3、VRM 4、BCM 5） ---- */
#define FC_CMD_DB_SLAVE_VRM 4u
#define FC_CMD_DB_SLAVE_BCM 5u
#define FC_VRM_VOLTAGE 0u /* mV */
#define FC_VRM_ENABLE 4u
#define FC_VRM_MODE 8u
#define FC_VRM_MODE_HPM 7u /* PMIC5 LDO 的 HPM（rpmh-regulator.c pmic_mode_map_pmic5_ldo） */
#define FC_VRM_MODE_BOB_AUTO 6u /* PMIC5 BoB AUTO（rpmh-regulator.h PMIC5_BOB_MODE_AUTO） */

static inline int fc_is_vrm(uint32_t address) { return address && ((address >> 16) & 0xfu) == FC_CMD_DB_SLAVE_VRM && !(address & 0xffu); }
static inline int fc_is_bcm(uint32_t address) { return address != 0u && ((address >> 16) & 0xfu) == FC_CMD_DB_SLAVE_BCM; }
static inline uint32_t fc_bcm_vote(int enable, uint32_t vote)
{
    return (1u << 30) | (enable ? (1u << 29) : 0u) | (vote & 0x3fffu);
}
/* 開一顆電源軌（LDO 或 BoB）：電壓、模式、啟用，同一個 TCS 依序完成；回傳命令數。 */
static inline unsigned fc_vrm_on_mode(uint32_t base, uint32_t mv, uint32_t mode, uint32_t *addresses, uint32_t *values)
{
    addresses[0] = base + FC_VRM_VOLTAGE; values[0] = mv;
    addresses[1] = base + FC_VRM_MODE; values[1] = mode;
    addresses[2] = base + FC_VRM_ENABLE; values[2] = 1;
    return 3;
}
/* 開一顆 LDO（HPM 模式）：保留既有介面相容性。 */
static inline unsigned fc_vrm_on(uint32_t base, uint32_t mv, uint32_t *addresses, uint32_t *values)
{
    return fc_vrm_on_mode(base, mv, FC_VRM_MODE_HPM, addresses, values);
}
static inline unsigned fc_vrm_off(uint32_t base, uint32_t *addresses, uint32_t *values)
{
    addresses[0] = base + FC_VRM_ENABLE; values[0] = 0;
    return 1;
}

/* ---- I2C 探測規則 ---- */
#define FC_I2C_FIRST 0x08u
#define FC_I2C_LAST 0x77u
/* i2cdetect 的預設：0x30–0x37 與 0x50–0x5F 用「讀一個位元組」探測（有些 EEPROM 會被 quick write 弄壞），其餘用 quick write。 */
static inline int fc_probe_by_read(unsigned address) { return (address >= 0x30 && address <= 0x37) || (address >= 0x50 && address <= 0x5f); }
/* EEPROM 範圍（0x50–0x5F）永遠不送寫入位元組：連暫存器位址都不送，只做「目前位址讀」，校正資料不可能被改到。 */
static inline int fc_may_write(unsigned address) { return !(address >= 0x50 && address <= 0x5f); }

/* 常見感測器的 chip ID 暫存器（16 位元暫存器位址）。只對 fc_may_write 允許的位址讀。 */
struct fc_id_probe { uint16_t reg; uint8_t bytes; const char *family; };
static const struct fc_id_probe fc_id_probes[] = {
    {0x0016, 2, "sony"},       /* IMX：model id 0x0016–0x0017 */
    {0x0000, 2, "samsung"},    /* S5K：0x0000–0x0001 */
    {0x300a, 3, "omnivision"}, /* OV：0x300A–0x300C */
    {0x0f16, 2, "hynix"},      /* Hi-xxx：0x0F16–0x0F17 */
};
#define FC_ID_PROBES (sizeof(fc_id_probes) / sizeof(fc_id_probes[0]))

/* ---- 手動 I2C 引擎 ---- */
struct fc_bus_ops {
    void (*line)(void *context, unsigned pin, int high); /* high：放開（輸入＋上拉）；low：輸出 0 */
    int (*level)(void *context, unsigned pin);
    void (*wait_us)(void *context, unsigned us);
};
struct fc_bus {
    const struct fc_bus_ops *ops;
    void *context;
    unsigned sda, scl;
    unsigned half_us;     /* 半個位元的時間：5 µs＝最多 100 kHz */
    unsigned stretch_max; /* 時脈放開後最多等幾個半位元 */
    unsigned stretched, recovered, stuck;
};
enum { FC_I2C_OK = 0, FC_I2C_NACK_ADDR = 1, FC_I2C_NACK_DATA = 2, FC_I2C_BUSY = -1, FC_I2C_STUCK = -2, FC_I2C_REFUSED = -3,
    FC_I2C_TIMEOUT = -4 };

static inline void fc_half(struct fc_bus *b) { b->ops->wait_us(b->context, b->half_us); }
static inline void fc_set(struct fc_bus *b, unsigned pin, int high) { b->ops->line(b->context, pin, high); }
static inline int fc_get(struct fc_bus *b, unsigned pin) { return b->ops->level(b->context, pin) ? 1 : 0; }

/* 時脈放開並等它真的變高（從端可以拉住時脈要我們等）。逾時記一筆並回 0。 */
static inline int fc_scl_high(struct fc_bus *b)
{
    fc_set(b, b->scl, 1);
    for (unsigned waited = 0; !fc_get(b, b->scl); ++waited) {
        if (waited >= b->stretch_max) { b->stuck++; return 0; }
        b->stretched++;
        fc_half(b);
    }
    return 1;
}
static inline void fc_i2c_start(struct fc_bus *b)
{
    fc_set(b, b->sda, 1);
    (void)fc_scl_high(b);
    fc_half(b);
    fc_set(b, b->sda, 0);
    fc_half(b);
    fc_set(b, b->scl, 0);
    fc_half(b);
}
static inline void fc_i2c_stop(struct fc_bus *b)
{
    fc_set(b, b->sda, 0);
    fc_half(b);
    (void)fc_scl_high(b);
    fc_half(b);
    fc_set(b, b->sda, 1);
    fc_half(b);
}
/* 一個位元：資料線在時脈低時改，時脈高時取樣。回傳取樣到的電位；時脈卡住回 -1。 */
static inline int fc_i2c_bit(struct fc_bus *b, int out)
{
    fc_set(b, b->sda, out);
    fc_half(b);
    if (!fc_scl_high(b)) return -1;
    fc_half(b);
    const int in = fc_get(b, b->sda);
    fc_set(b, b->scl, 0);
    return in;
}
/* 寫一個位元組；回傳 1＝對方 ACK、0＝NACK、-1＝時脈卡住。 */
static inline int fc_i2c_write_byte(struct fc_bus *b, uint8_t byte)
{
    for (int bit = 7; bit >= 0; --bit)
        if (fc_i2c_bit(b, (byte >> bit) & 1) < 0) return -1;
    const int ack = fc_i2c_bit(b, 1);
    return ack < 0 ? -1 : !ack;
}
/* 讀一個位元組，more＝還要再讀（送 ACK），否則送 NACK。時脈卡住回 -1。 */
static inline int fc_i2c_read_byte(struct fc_bus *b, int more)
{
    unsigned byte = 0;
    for (int bit = 7; bit >= 0; --bit) {
        const int in = fc_i2c_bit(b, 1);
        if (in < 0) return -1;
        byte = byte << 1 | (unsigned)in;
    }
    if (fc_i2c_bit(b, !more) < 0) return -1;
    return (int)byte;
}
/* 資料線被從端拉住（上次停在位元組中間）：打最多 9 個時脈直到它放開，再 STOP。回傳打了幾個時脈，0＝救不回來。 */
static inline unsigned fc_i2c_recover(struct fc_bus *b)
{
    for (unsigned clocks = 1; clocks <= 9; ++clocks) {
        fc_set(b, b->scl, 0);
        fc_half(b);
        if (!fc_scl_high(b)) return 0;
        fc_half(b);
        if (fc_get(b, b->sda)) {
            fc_i2c_stop(b);
            b->recovered++;
            return clocks;
        }
    }
    return 0;
}
/* 匯流排是否閒置（兩條線都放開且為高）；資料線單獨被拉住時先試著救回。 */
static inline int fc_i2c_idle(struct fc_bus *b)
{
    fc_set(b, b->sda, 1);
    fc_set(b, b->scl, 1);
    fc_half(b);
    if (fc_get(b, b->sda) && fc_get(b, b->scl)) return 1;
    if (fc_get(b, b->scl) && !fc_get(b, b->sda) && fc_i2c_recover(b) && fc_get(b, b->scl) && fc_get(b, b->sda)) return 1;
    return 0;
}
/* 一筆交易：寫 out（暫存器位址，寫入時再加資料），有 in 時重複 START 後讀 in_count 個位元組。address 是 7 位元位址。
 * EEPROM 範圍（fc_may_write 不允許）只接受 out_count==0。 */
static inline int fc_i2c_transfer(struct fc_bus *b, unsigned address, const uint8_t *out, unsigned out_count, uint8_t *in,
    unsigned in_count)
{
    if (address > 0x7f || (out_count && !fc_may_write(address))) return FC_I2C_REFUSED;
    if (!fc_i2c_idle(b)) return FC_I2C_BUSY;
    int rc = FC_I2C_OK;
    fc_i2c_start(b);
    const int read_only = out_count == 0 && in_count != 0;
    int ack = fc_i2c_write_byte(b, (uint8_t)(address << 1 | (read_only ? 1u : 0u)));
    if (ack < 0) rc = FC_I2C_STUCK;
    else if (!ack) rc = FC_I2C_NACK_ADDR;
    for (unsigned i = 0; rc == FC_I2C_OK && i < out_count; ++i) {
        ack = fc_i2c_write_byte(b, out[i]);
        if (ack < 0) rc = FC_I2C_STUCK;
        else if (!ack) rc = FC_I2C_NACK_DATA;
    }
    if (rc == FC_I2C_OK && in_count && !read_only) {
        fc_i2c_start(b); /* 重複 START */
        ack = fc_i2c_write_byte(b, (uint8_t)(address << 1 | 1u));
        if (ack < 0) rc = FC_I2C_STUCK;
        else if (!ack) rc = FC_I2C_NACK_ADDR;
    }
    for (unsigned i = 0; rc == FC_I2C_OK && i < in_count; ++i) {
        const int byte = fc_i2c_read_byte(b, i + 1 < in_count);
        if (byte < 0) rc = FC_I2C_STUCK;
        else in[i] = (uint8_t)byte;
    }
    fc_i2c_stop(b);
    return rc;
}
/* i2cdetect 式探測一個位址：回傳 1＝有人 ACK、0＝沒有、負值＝匯流排問題。 */
static inline int fc_i2c_probe(struct fc_bus *b, unsigned address)
{
    uint8_t byte;
    int rc;
    if (fc_probe_by_read(address)) rc = fc_i2c_transfer(b, address, 0, 0, &byte, 1);
    else {
        /* quick write：只送位址（寫方向）就 STOP */
        if (!fc_i2c_idle(b)) return FC_I2C_BUSY;
        fc_i2c_start(b);
        const int ack = fc_i2c_write_byte(b, (uint8_t)(address << 1));
        fc_i2c_stop(b);
        rc = ack < 0 ? FC_I2C_STUCK : ack ? FC_I2C_OK : FC_I2C_NACK_ADDR;
    }
    if (rc == FC_I2C_OK) return 1;
    if (rc == FC_I2C_NACK_ADDR) return 0;
    return rc;
}
/* 讀 16 位元暫存器位址的 bytes 個位元組（寫兩個位址位元組、重複 START、讀）。 */
static inline int fc_i2c_read16(struct fc_bus *b, unsigned address, uint16_t reg, uint8_t *in, unsigned bytes)
{
    const uint8_t at[2] = {(uint8_t)(reg >> 8), (uint8_t)reg};
    return fc_i2c_transfer(b, address, at, 2, in, bytes);
}
/* 寫 16 位元暫存器位址的 1 位元組值；EEPROM 範圍（0x50–0x5F）由 fc_i2c_transfer 一律拒絕。 */
static inline int fc_i2c_write16_u8(struct fc_bus *b, unsigned address, uint16_t reg, uint8_t value)
{
    const uint8_t out[3] = {(uint8_t)(reg >> 8), (uint8_t)reg, value};
    return fc_i2c_transfer(b, address, out, 3, 0, 0);
}
/* 寫 16 位元暫存器位址的 2 位元組值（大端序 MSB 先送）；EEPROM 範圍由 fc_i2c_transfer 一律拒絕。 */
static inline int fc_i2c_write16_u16(struct fc_bus *b, unsigned address, uint16_t reg, uint16_t value)
{
    const uint8_t out[4] = {(uint8_t)(reg >> 8), (uint8_t)reg, (uint8_t)(value >> 8), (uint8_t)value};
    return fc_i2c_transfer(b, address, out, 4, 0, 0);
}
/* 輪詢 16 位元暫存器位址的 1 位元組值直到等於 expected；I2C 錯誤立即中止，逾時回傳 FC_I2C_TIMEOUT。 */
static inline int fc_i2c_poll16_u8(struct fc_bus *b, unsigned address, uint16_t reg, uint8_t expected, unsigned max_tries,
    unsigned poll_wait_us)
{
    const unsigned tries = max_tries ? max_tries : 1u;
    for (unsigned idx = 0u; idx < tries; ++idx) {
        uint8_t got = 0u;
        const int rc = fc_i2c_read16(b, address, reg, &got, 1u);
        if (rc != FC_I2C_OK) return rc;
        if (got == expected) return FC_I2C_OK;
        if (idx + 1u < tries && poll_wait_us) b->ops->wait_us(b->context, poll_wait_us);
    }
    return FC_I2C_TIMEOUT;
}
/* 寫 8 位元暫存器位址的 1 位元組值（AK7314 致動器等 8-bit register 裝置）。 */
static inline int fc_i2c_write8_u8(struct fc_bus *b, unsigned address, uint8_t reg, uint8_t value)
{
    const uint8_t out[2] = {reg, value};
    return fc_i2c_transfer(b, address, out, 2u, 0, 0);
}
/* 寫 8 位元暫存器位址的 2 位元組值（大端序 MSB 先送）。 */
static inline int fc_i2c_write8_u16(struct fc_bus *b, unsigned address, uint8_t reg, uint16_t value)
{
    const uint8_t out[3] = {reg, (uint8_t)(value >> 8), (uint8_t)value};
    return fc_i2c_transfer(b, address, out, 3u, 0, 0);
}
/* 讀 8 位元暫存器位址的 2 位元組值（大端序 MSB 先讀）。 */
static inline int fc_i2c_read8_u16(struct fc_bus *b, unsigned address, uint8_t reg, uint16_t *out)
{
    uint8_t in[2] = {0u, 0u};
    if (!out) return FC_I2C_REFUSED;
    const int rc = fc_i2c_transfer(b, address, &reg, 1u, in, 2u);
    if (rc == FC_I2C_OK) *out = (uint16_t)(((uint16_t)in[0] << 8) | in[1]);
    return rc;
}
/* 讀 16 位元暫存器位址的 4 位元組值（大端序 MSB 先讀，OIS BU24721/BU24618 32-bit 暫存器）。 */
static inline int fc_i2c_read16_u32(struct fc_bus *b, unsigned address, uint16_t reg, uint32_t *out)
{
    uint8_t in[4] = {0u, 0u, 0u, 0u};
    if (!out) return FC_I2C_REFUSED;
    const int rc = fc_i2c_read16(b, address, reg, in, 4u);
    if (rc == FC_I2C_OK) {
        *out = ((uint32_t)in[0] << 24) | ((uint32_t)in[1] << 16) | ((uint32_t)in[2] << 8) | in[3];
    }
    return rc;
}
/* 寫 16 位元暫存器位址的 4 位元組值（大端序 MSB 先送）。 */
static inline int fc_i2c_write16_u32(struct fc_bus *b, unsigned address, uint16_t reg, uint32_t value)
{
    const uint8_t out[6] = {
        (uint8_t)(reg >> 8), (uint8_t)reg,
        (uint8_t)(value >> 24), (uint8_t)(value >> 16), (uint8_t)(value >> 8), (uint8_t)value,
    };
    return fc_i2c_transfer(b, address, out, 6u, 0, 0);
}
/* 輪詢 16 位元暫存器位址的 4 位元組值直到等於 expected；I2C 錯誤立即中止。 */
static inline int fc_i2c_poll16_u32(struct fc_bus *b, unsigned address, uint16_t reg, uint32_t expected, unsigned max_tries,
    unsigned poll_wait_us)
{
    const unsigned tries = max_tries ? max_tries : 1u;
    for (unsigned idx = 0u; idx < tries; ++idx) {
        uint32_t got = 0u;
        const int rc = fc_i2c_read16_u32(b, address, reg, &got);
        if (rc != FC_I2C_OK) return rc;
        if (got == expected) return FC_I2C_OK;
        if (idx + 1u < tries && poll_wait_us) b->ops->wait_us(b->context, poll_wait_us);
    }
    return FC_I2C_TIMEOUT;
}

/* ---- C2b：TPG0 → CSID-lite0 → VFE-lite0 RDI0 → apps_smmu CB4 收幀純邏輯 ----
 * 暫存器與位元定義對照 Linux 主線 drivers/media/platform/qcom/camss/camss-tpg-gen1.c、
 * camss-csid-gen3.c、camss-vfe-gen3.c（SM8550 Titan Gen3 / VFE 780 lite，GPL，只取位址與位元事實），
 * SMMU 檢查對照本庫 fuxi-adsp/fuxi_audio_smmu.h（ARM SMMU-500 legacy SMR/S2CR）。 */
#define FC_TPG_HW_VERSION 0x00u
#define FC_TPG_HW_STATUS 0x04u
#define FC_TPG_CTRL 0x64u
#define FC_TPG_VC0_CFG0 0x68u
#define FC_TPG_VC0_LFSR 0x6cu
#define FC_TPG_VC0_HBI 0x70u
#define FC_TPG_VC0_VBI 0x74u
#define FC_TPG_VC0_CBARS 0x78u
#define FC_TPG_DT0_CFG0 0x7cu
#define FC_TPG_DT0_CFG1 0x80u
#define FC_TPG_DT0_CFG2 0x84u
#define FC_TPG_CLEAR 0x1f4u
#define FC_TPG_VER_2_0_0 0x20000000u
#define FC_TPG_VER_2_1_0 0x20010000u
#define FC_TPG_MODE_COLOR_BARS 9u
#define FC_TPG_ENC_RAW8 1u
#define FC_TPG_USER_PAYLOAD 0xbeu
#define FC_TPG_LFSR_SEED 0x12345678u
#define FC_TPG_CBARS_ROTATE_STD (0xau << 8)
#define FC_MIPI_DT_RAW8 0x2au
#define FC_MIPI_DT_RAW10 0x2bu

#define FC_CSID_RST_CFG 0x0cu
#define FC_CSID_RST_CMD 0x10u
#define FC_CSID_IRQ_CMD 0x14u
#define FC_CSID_RUP_AUP_CMD 0x18u
#define FC_CSID_TOP_IRQ_STATUS 0x7cu
#define FC_CSID_TOP_IRQ_MASK 0x80u
#define FC_CSID_TOP_IRQ_CLEAR 0x84u
#define FC_CSID_BUF_DONE_IRQ_STATUS 0x8cu
#define FC_CSID_BUF_DONE_IRQ_MASK 0x90u
#define FC_CSID_BUF_DONE_IRQ_CLEAR 0x94u
#define FC_CSID_RX_IRQ_STATUS 0x9cu
#define FC_CSID_RX_IRQ_MASK 0xa0u
#define FC_CSID_RX_IRQ_CLEAR 0xa4u
#define FC_CSID_RDI0_IRQ_STATUS 0xecu
#define FC_CSID_RDI0_IRQ_MASK 0xf0u
#define FC_CSID_RDI0_IRQ_CLEAR 0xf4u
#define FC_CSID_RX_CFG0 0x200u
#define FC_CSID_RX_CFG1 0x204u
#define FC_CSID_RDI0_CFG0 0x500u
#define FC_CSID_RDI0_CTRL 0x504u
#define FC_CSID_RDI0_CFG1 0x510u
#define FC_CSID_RDI0_SUBSAMPLE_PATTERN 0x548u
#define FC_CSID_RDI0_SUBSAMPLE_PERIOD 0x54cu
#define FC_CSID_TOP_RESET_DONE 1u
#define FC_CSID_BUF_DONE_RDI0 (1u << 1)
#define FC_CSID_RDI_RUP_DONE (1u << 23)
#define FC_CSID_RUP_AUP_RDI0 ((1u << 4) | (1u << 20))

#define FC_VFE_BUS_CGC_OVERRIDE 0x208u
#define FC_VFE_BUS_TEST_BUS_CTRL 0x2dcu
#define FC_VFE_WM0_CFG 0x400u
#define FC_VFE_WM0_IMAGE_ADDR 0x404u
#define FC_VFE_WM0_FRAME_INCR 0x408u
#define FC_VFE_WM0_IMAGE_CFG0 0x40cu
#define FC_VFE_WM0_IMAGE_CFG2 0x414u
#define FC_VFE_WM0_PACKER_CFG 0x418u
#define FC_VFE_WM0_IRQ_PERIOD 0x430u
#define FC_VFE_WM0_IRQ_PATTERN 0x434u
#define FC_VFE_WM0_FDROP_PERIOD 0x438u
#define FC_VFE_WM0_FDROP_PATTERN 0x43cu
#define FC_VFE_WM0_MMU_PREFETCH_CFG 0x460u
#define FC_VFE_WM0_MMU_PREFETCH_MAX 0x464u

#define FC_SMMU_PA 0x15000000ull
#define FC_SMMU_SIZE 0x100000u
#define FC_SMMU_BYPASS_BANK 4u
#define FC_SMMU_FAULT_S2CR (2u << 16)
#define FC_SMMU_IFE_SID 0x800u
#define FC_SMMU_IFE_MASK 0x20u
#define FC_SMMU_IFE_SMR ((1u << 31) | (FC_SMMU_IFE_MASK << 16) | FC_SMMU_IFE_SID)

static inline uint32_t fc_tpg_hbi(uint32_t width, uint32_t bpp, uint32_t lanes)
{
    if (!lanes) return 0u;
    const uint32_t denom = 8u * lanes * 100u;
    return (width * bpp * 545u + denom - 1u) / denom;
}
static inline uint32_t fc_tpg_vbi(uint32_t height) { return (height * 10u) / 100u; }
static inline uint32_t fc_tpg_dt_cfg0(uint32_t width, uint32_t height)
{
    return ((width & 0xffffu) << 16) | (height & 0xffffu);
}
static inline uint32_t fc_tpg_dt_cfg2(uint32_t hw_version, uint32_t mode, uint32_t encode_format, uint32_t user_payload)
{
    const uint32_t payload = mode > 0u ? (mode - 1u) : 0u;
    if (hw_version == FC_TPG_VER_2_0_0) {
        return (payload & 0xfu) | ((user_payload & 0xffffu) << 4) | ((encode_format & 0xfu) << 20);
    }
    return (payload & 0xfu) | ((user_payload & 0xffffffu) << 4) | ((encode_format & 0xfu) << 28);
}
static inline uint32_t fc_tpg_ctrl(int enable, uint32_t lanes)
{
    if (!enable || !lanes) return 0u;
    return 1u | (((lanes - 1u) & 3u) << 4);
}
static inline uint32_t fc_csid_rx_cfg0_tpg(uint32_t lanes, uint32_t tpg_id)
{
    const uint32_t lane_cnt = lanes ? ((lanes - 1u) & 3u) : 3u;
    return lane_cnt | (0x3210u << 4) | (1u << 27) | (((tpg_id + 1u) & 3u) << 28);
}
static inline uint32_t fc_csid_rx_cfg0_phy(uint32_t lanes, uint32_t lane_assign, uint32_t phy_id, int is_cphy)
{
    const uint32_t lane_cnt = lanes ? ((lanes - 1u) & 3u) : 3u;
    const uint32_t phy_sel = (phy_id + 1u) & 7u;
    return lane_cnt | ((lane_assign & 0xffffu) << 4) | (phy_sel << 20) | (is_cphy ? (1u << 24) : 0u);
}
static inline uint32_t fc_csid_rdi_cfg0(uint32_t data_type, uint32_t vc, uint32_t dt_id, int enable)
{
    return (1u << 6) | (1u << 8) | (0xfu << 12) | ((data_type & 0x3fu) << 16) | ((vc & 0x1fu) << 22) |
        ((dt_id & 3u) << 27) | (enable ? (1u << 31) : 0u);
}
static inline uint32_t fc_csid_rdi_cfg1(void)
{
    return (1u << 15) | (1u << 10) | (1u << 8) | (1u << 7) | (1u << 6) | (1u << 5);
}
static inline int fc_vfe_wm_addr_valid(uint64_t phys_addr)
{
    return phys_addr != 0ull && (phys_addr & 0xffull) == 0ull && (phys_addr >> 8) <= 0xffffffffull;
}
static inline uint32_t fc_vfe_wm_addr(uint64_t phys_addr) { return (uint32_t)(phys_addr >> 8); }
#define FC_VFE_WM_STRIDE_ALIGN 16u
#define FC_DMA_GUARD_BYTES (256u * 1024u)

static inline uint32_t fc_vfe_wm_line_stride(uint32_t bytes_per_line)
{
    return (bytes_per_line + (FC_VFE_WM_STRIDE_ALIGN - 1u)) & ~(FC_VFE_WM_STRIDE_ALIGN - 1u);
}

static inline uint32_t fc_vfe_wm_alloc_bytes(uint32_t frame_bytes, uint32_t page_mask)
{
    const uint64_t total = (uint64_t)frame_bytes + (uint64_t)FC_DMA_GUARD_BYTES + (uint64_t)page_mask;
    return (uint32_t)(total & ~(uint64_t)page_mask);
}

static inline uint32_t fc_vfe_wm_frame_incr(uint32_t bytes_per_line, uint32_t height)
{
    const uint32_t stride = fc_vfe_wm_line_stride(bytes_per_line);
    return (stride * height) >> 8;
}

static inline uint32_t fc_guard_scan(const uint8_t *buf, uint32_t start_off, uint32_t end_off, uint8_t canary)
{
    uint32_t dirty = 0u;
    if (!buf || end_off <= start_off) return 0u;
    for (uint32_t i = start_off; i < end_off; ++i) {
        if (buf[i] != canary) ++dirty;
    }
    return dirty;
}
static inline int fc_smmu_overlap(uint32_t smr, uint32_t sid, uint32_t mask)
{
    if (!(smr & (1u << 31))) return 0;
    const uint32_t smr_mask = (smr >> 16) & 0x7fffu;
    return (((smr ^ sid) & ~(smr_mask | (mask & 0x7fffu)) & 0x7fffu) == 0u);
}
static inline int fc_smmu_pick_high_slot(const uint32_t *smrs, const uint32_t *s2crs, unsigned slots, uint32_t sid, uint32_t mask)
{
    if (!smrs || !s2crs || !slots) return -1;
    for (unsigned idx = 0; idx < slots; ++idx) {
        if (fc_smmu_overlap(smrs[idx], sid, mask)) return -2;
    }
    for (unsigned rev = slots; rev > 0u; --rev) {
        const unsigned slot = rev - 1u;
        if (!(smrs[slot] & (1u << 31)) && s2crs[slot] == FC_SMMU_FAULT_S2CR) return (int)slot;
    }
    return -1;
}

struct fc_frame_stats {
    uint32_t non_canary;
    uint32_t transitions;
    uint32_t fnv1a;
    uint8_t min_val;
    uint8_t max_val;
};
static inline void fc_frame_analyze(const uint8_t *buf, uint32_t length, uint8_t canary, struct fc_frame_stats *out)
{
    out->non_canary = 0u;
    out->transitions = 0u;
    out->fnv1a = 2166136261u;
    out->min_val = length ? 255u : 0u;
    out->max_val = 0u;
    for (uint32_t idx = 0; idx < length; ++idx) {
        const uint8_t val = buf[idx];
        if (val != canary) out->non_canary++;
        if (val < out->min_val) out->min_val = val;
        if (val > out->max_val) out->max_val = val;
        if (idx > 0u && val != buf[idx - 1u]) out->transitions++;
        out->fnv1a ^= val;
        out->fnv1a *= 16777619u;
    }
}
static inline uint32_t fc_frame_diff(const uint8_t *first, const uint8_t *second, uint32_t length)
{
    uint32_t diffs = 0u;
    for (uint32_t idx = 0u; idx < length; ++idx) {
        if (first[idx] != second[idx]) diffs++;
    }
    return diffs;
}

/* ---- C3：CSIPHY4（SM8550 Gen2 2.1.2）與 FUXICAM1 感測器暫存器表純邏輯 ----
 * 暫存器位址與位元對照 Linux drivers/media/platform/qcom/camss/camss-csiphy-3ph-1-0.c（GPL，只取位址與位元事實）。 */
#define FC_CSIPHY_CTRL(idx) (0x1000u + 4u * (idx))
#define FC_CSIPHY_STATUS(idx) (0x10b0u + 4u * (idx))
#define FC_TAB_HDR_BYTES 28u
#define FC_TAB_ENTRY_BYTES 4u
#define FC_TAB_MAX_ENTRIES 4096u
#define FC_TAB_VERSION 1u

struct fc_tab_header {
    uint16_t version;
    uint8_t slave7;
    uint8_t lanes;
    uint16_t width;
    uint16_t height;
    uint8_t dt;
    uint8_t settle;
    uint16_t lane_assign;
    uint32_t count;
    uint32_t crc32;
    uint8_t flags;
};
struct fc_tab_entry {
    uint16_t reg;
    uint8_t val;
    uint8_t delay_ms;
};
static inline uint16_t fc_le16(const uint8_t *ptr)
{
    return (uint16_t)((uint16_t)ptr[0] | ((uint16_t)ptr[1] << 8));
}
static inline uint32_t fc_le32(const uint8_t *ptr)
{
    return (uint32_t)ptr[0] | ((uint32_t)ptr[1] << 8) | ((uint32_t)ptr[2] << 16) | ((uint32_t)ptr[3] << 24);
}
static inline uint32_t fc_crc32(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xffffffffu;
    for (uint32_t idx = 0u; idx < length; ++idx) {
        crc ^= data[idx];
        for (unsigned bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xedb88320u) : (crc >> 1);
        }
    }
    return ~crc;
}
static inline uint32_t fc_raw10_bytes_per_line(uint32_t width)
{
    return (width * 10u + 7u) / 8u;
}
static inline uint32_t fc_csiphy_ctrl5(uint32_t lanes)
{
    uint32_t mask = 0x80u;
    for (uint32_t lane = 0u; lane < lanes && lane < 4u; ++lane) mask |= 1u << (lane * 2u);
    return mask;
}
/* SM8550 CSIPHY v2.1.2 C-PHY CTRL(5) trio 使能遮罩：3 trio 時寫入 0x2a（bits 1,3,5，對應 camera.ko .rodata+0x89f48）。 */
static inline uint32_t fc_csiphy_ctrl5_cphy(uint32_t trios)
{
    uint32_t mask = 0u;
    for (uint32_t trio = 0u; trio < trios && trio < 3u; ++trio) mask |= 2u << (trio * 2u);
    return mask;
}
#define FC_TAB_FLAG_CPHY 1u
#define FC_TAB2_HDR_BYTES 32u
#define FC_TAB2_ENTRY_BYTES 8u
#define FC_TAB2_VERSION 2u
#define FC_OP_WRITE 0u
#define FC_OP_POLL_EQ 4u

struct fc_tab2_entry {
    uint16_t reg;
    uint16_t val;
    uint16_t delay_ms;
    uint8_t data_type;
    uint8_t op;
};

static inline int fc_tab_validate_common(const struct fc_tab_header *out)
{
    if (!fc_may_write(out->slave7) || out->slave7 > 0x7fu) return -4;
    if (out->lanes < 1u || out->lanes > 4u || !out->width || !out->height || out->width > 8192u || out->height > 8192u) return -5;
    if (!out->dt || out->count == 0u || out->count > FC_TAB_MAX_ENTRIES) return -6;
    return 0;
}

static inline int fc_tab_parse(const uint8_t *buf, uint32_t length, struct fc_tab_header *out)
{
    static const uint8_t kMagic[8] = {'F', 'U', 'X', 'I', 'C', 'A', 'M', '1'};
    if (!buf || !out || length < FC_TAB_HDR_BYTES) return -1;
    for (unsigned idx = 0u; idx < 8u; ++idx) {
        if (buf[idx] != kMagic[idx]) return -2;
    }
    out->version = fc_le16(buf + 8);
    out->slave7 = buf[10];
    out->lanes = buf[11];
    out->width = fc_le16(buf + 12);
    out->height = fc_le16(buf + 14);
    out->dt = buf[16];
    out->settle = buf[17];
    out->lane_assign = fc_le16(buf + 18);
    out->count = fc_le32(buf + 20);
    out->crc32 = fc_le32(buf + 24);
    out->flags = 0u;
    if (out->version != FC_TAB_VERSION) return -3;
    const int common_rc = fc_tab_validate_common(out);
    if (common_rc != 0) return common_rc;
    const uint32_t entry_bytes = out->count * FC_TAB_ENTRY_BYTES;
    if (length != FC_TAB_HDR_BYTES + entry_bytes) return -7;
    if (fc_crc32(buf + FC_TAB_HDR_BYTES, entry_bytes) != out->crc32) return -8;
    return 0;
}
static inline void fc_tab_entry_at(const uint8_t *buf, uint32_t idx, struct fc_tab_entry *out)
{
    const uint8_t *entry = buf + FC_TAB_HDR_BYTES + idx * FC_TAB_ENTRY_BYTES;
    out->reg = fc_le16(entry);
    out->val = entry[2];
    out->delay_ms = entry[3];
}
/* 解析 FUXICAM2（32B 表頭 + 每筆 8B 支援 u8/u16 與輪詢）；sensor_id_out 可為 NULL。 */
static inline int fc_tab2_parse(const uint8_t *buf, uint32_t length, struct fc_tab_header *out, uint8_t *sensor_id_out)
{
    static const uint8_t kMagic2[8] = {'F', 'U', 'X', 'I', 'C', 'A', 'M', '2'};
    if (!buf || !out || length < FC_TAB2_HDR_BYTES) return -1;
    for (unsigned idx = 0u; idx < 8u; ++idx) {
        if (buf[idx] != kMagic2[idx]) return -2;
    }
    out->version = fc_le16(buf + 8);
    out->slave7 = buf[10];
    out->lanes = buf[11];
    out->width = fc_le16(buf + 12);
    out->height = fc_le16(buf + 14);
    out->dt = buf[16];
    out->settle = buf[17];
    out->lane_assign = fc_le16(buf + 18);
    out->count = fc_le32(buf + 20);
    out->crc32 = fc_le32(buf + 24);
    out->flags = buf[29];
    if (out->version != FC_TAB2_VERSION) return -3;
    const int common_rc = fc_tab_validate_common(out);
    if (common_rc != 0) return common_rc;
    const uint32_t entry_bytes = out->count * FC_TAB2_ENTRY_BYTES;
    if (length != FC_TAB2_HDR_BYTES + entry_bytes) return -7;
    if (fc_crc32(buf + FC_TAB2_HDR_BYTES, entry_bytes) != out->crc32) return -8;
    if (sensor_id_out) *sensor_id_out = buf[28];
    return 0;
}
/* 統一解析 FUXICAM1 或 FUXICAM2；FUXICAM1 時將 *sensor_id_out 設為 0。 */
static inline int fc_tab_parse_any(const uint8_t *buf, uint32_t length, struct fc_tab_header *out, uint8_t *sensor_id_out)
{
    if (!buf || !out || length < 8u) return -1;
    if (buf[0] == 'F' && buf[1] == 'U' && buf[2] == 'X' && buf[3] == 'I' &&
        buf[4] == 'C' && buf[5] == 'A' && buf[6] == 'M') {
        if (buf[7] == '1') {
            const int rc = fc_tab_parse(buf, length, out);
            if (rc == 0 && sensor_id_out) *sensor_id_out = 0u;
            return rc;
        }
        if (buf[7] == '2') return fc_tab2_parse(buf, length, out, sensor_id_out);
    }
    return -2;
}
/* 統一依表版本讀取第 idx 筆條目，FUXICAM1 自動展開為 data_type=1, op=FC_OP_WRITE。 */
static inline void fc_tab_entry_any_at(const uint8_t *buf, const struct fc_tab_header *hdr, uint32_t idx, struct fc_tab2_entry *out)
{
    if (hdr->version == FC_TAB_VERSION) {
        struct fc_tab_entry e1;
        fc_tab_entry_at(buf, idx, &e1);
        out->reg = e1.reg;
        out->val = e1.val;
        out->delay_ms = e1.delay_ms;
        out->data_type = 1u;
        out->op = FC_OP_WRITE;
        return;
    }
    const uint8_t *entry = buf + FC_TAB2_HDR_BYTES + idx * FC_TAB2_ENTRY_BYTES;
    out->reg = fc_le16(entry);
    out->val = fc_le16(entry + 2u);
    out->delay_ms = fc_le16(entry + 4u);
    out->data_type = entry[6];
    out->op = entry[7];
}

/* ---- C4：/dev/fuxi-camera ABI、OV32C 曝光／增益編碼與三槽環形緩衝狀態機 ----
 * ABI 與 work/camera-modem-survey/src/redfin/redwood_camera.h 二進位相容（struct fuxi_camera_frame = 120 B、
 * struct fuxi_camera_command = 24 B）；OV32C 暫存器與增益階梯對照本機 com.qti.sensor.fuxi_sunny_ov32c_front.so
 *（real_to_register_gain 0x3ee0 與 Group Hold 3 序列 0x4030）及 sensormodule RES[3]（1280x720 RAW10）。 */
#define FUXI_CAMERA_DEVICE "/dev/fuxi-camera"
#define FUXI_CAMERA_MAGIC "RWFRAME1"
#define FUXI_CAMERA_NO_FOCUS 0xffffffffu
#define FUXI_CAMERA_FOCUS_MAX 1023u
#define FUXI_CAMERA_R_FLAGS 0u
#define FUXI_CAMERA_R_SOF_LO 1u
#define FUXI_CAMERA_R_SOF_HI 2u
#define FUXI_CAMERA_R_DGAIN 3u
#define FUXI_CAMERA_R_FPS_MILLI 4u
#define FUXI_CAMERA_FLAG_FLASH_LIT (1u << 0)
#define FUXI_CAMERA_FLAG_LENS_MOVING (1u << 1)
#define FUXI_CAMERA_FLAG_MODE_CHANGED (1u << 2)
#define FUXI_CAMERA_EXPOSURE 1u
#define FUXI_CAMERA_KEEP 2u
#define FUXI_CAMERA_SENSOR 3u
#define FUXI_CAMERA_FOCUS 4u
#define FUXI_CAMERA_FLASH 5u
#define FUXI_CAMERA_FOCUS_MOVE 0u
#define FUXI_CAMERA_FOCUS_PARK 1u
#define FC_MODE_AUTO 0u
#define FC_MODE_PHOTO 1u
#define FC_MODE_VIDEO 2u
#define FC_MODE_VIDEO60 3u
#define FC_MODE_ANY 0xffffffffu

struct fuxi_camera_frame {
    char magic[8];
    uint32_t header_bytes;
    uint32_t width, height;
    uint32_t stride;
    uint32_t format;
    uint32_t bayer;
    uint32_t black;
    uint64_t sequence;
    uint64_t time;
    uint32_t exposure_lines;
    uint32_t line_ns;
    uint32_t gain;
    uint32_t gain_unity;
    uint32_t gain_max;
    uint32_t frame_lines;
    uint32_t frame_lines_min;
    uint32_t frame_lines_max;
    uint32_t margin;
    uint32_t sensor;
    uint32_t focus;
    uint32_t reserved[5];
};

struct fuxi_camera_command {
    uint32_t command;
    uint32_t bytes;
    uint32_t a, b, c, d;
};

#define FC_OV32C_SLAVE7 0x10u
#define FC_OV32C_HTS 1224u
#define FC_OV32C_VTS_MIN 652u
#define FC_OV32C_VTS_30FPS 2614u
#define FC_OV32C_VTS_MAX 0xfff0u
#define FC_OV32C_MIN_LINES 4u
#define FC_OV32C_MARGIN 12u
#define FC_OV32C_LINE_NS 12739u
#define FC_OV32C_FPS_MILLI 120400u
#define FC_OV32C_FPS_30_MILLI 30000u
#define FC_OV32C_GAIN_UNITY 0x0100u
#define FC_OV32C_GAIN_MAX 0x0ff0u
/* Vendor com.qti.sensormodule.fuxi_sunny_ov32c_front.bin RES[0..3] CFA=0 (CamX BGGR) -> camera_isp bayer 3 (BGGR). */
#define FC_OV32C_BAYER 3u
#define FC_OV32C_BLACK 64u
#define FC_OV32C_SENSOR_FRONT 2u
#define FC_OV32C_EXP_REGS 15u

struct fc_ov32c_exposure {
    uint32_t lines;
    uint32_t gain;
    uint32_t frame_lines;
};

static inline uint32_t fc_ov32c_quantize_gain(uint32_t gain)
{
    if (gain < FC_OV32C_GAIN_UNITY) gain = FC_OV32C_GAIN_UNITY;
    if (gain > FC_OV32C_GAIN_MAX) gain = FC_OV32C_GAIN_MAX;
    if (gain < 0x0200u) return gain & ~1u;
    if (gain < 0x0400u) return gain & ~3u;
    if (gain < 0x0800u) return gain & ~7u;
    return gain & ~15u;
}

static inline void fc_ov32c_clamp_exposure(uint32_t req_lines, uint32_t req_gain, uint32_t req_vts, struct fc_ov32c_exposure *out)
{
    uint32_t vts = req_vts ? req_vts : FC_OV32C_VTS_MIN;
    uint32_t lines = req_lines;
    if (vts < FC_OV32C_VTS_MIN) vts = FC_OV32C_VTS_MIN;
    if (vts > FC_OV32C_VTS_MAX) vts = FC_OV32C_VTS_MAX;
    if (lines < FC_OV32C_MIN_LINES) lines = FC_OV32C_MIN_LINES;
    if (lines > FC_OV32C_VTS_MAX - FC_OV32C_MARGIN) lines = FC_OV32C_VTS_MAX - FC_OV32C_MARGIN;
    if (lines + FC_OV32C_MARGIN > vts) vts = lines + FC_OV32C_MARGIN;
    if (vts > FC_OV32C_VTS_MAX) vts = FC_OV32C_VTS_MAX;
    out->lines = lines;
    out->gain = fc_ov32c_quantize_gain(req_gain);
    out->frame_lines = vts;
}

/* /dev/fuxi-camera 30 fps 串流模式：使用者層 AE 在正常亮度下送 req_vts=0 表示該模式最短幀長，夾在 FC_OV32C_VTS_30FPS (2614)。 */
static inline void fc_ov32c_clamp_exposure_30fps(uint32_t req_lines, uint32_t req_gain, uint32_t req_vts, struct fc_ov32c_exposure *out)
{
    const uint32_t vts = req_vts < FC_OV32C_VTS_30FPS ? FC_OV32C_VTS_30FPS : req_vts;
    fc_ov32c_clamp_exposure(req_lines, req_gain, vts, out);
}

static inline unsigned fc_ov32c_encode_exposure(const struct fc_ov32c_exposure *exp, struct fc_tab_entry out[FC_OV32C_EXP_REGS])
{
    struct fc_ov32c_exposure clamped;
    fc_ov32c_clamp_exposure(exp->lines, exp->gain, exp->frame_lines, &clamped);
    out[0].reg = 0x3208u; out[0].val = 0x03u; out[0].delay_ms = 0u;
    out[1].reg = 0x380eu; out[1].val = (uint8_t)((clamped.frame_lines >> 8) & 0xffu); out[1].delay_ms = 0u;
    out[2].reg = 0x380fu; out[2].val = (uint8_t)(clamped.frame_lines & 0xffu); out[2].delay_ms = 0u;
    out[3].reg = 0x3500u; out[3].val = (uint8_t)((clamped.lines >> 16) & 0x0fu); out[3].delay_ms = 0u;
    out[4].reg = 0x3501u; out[4].val = (uint8_t)((clamped.lines >> 8) & 0xffu); out[4].delay_ms = 0u;
    out[5].reg = 0x3502u; out[5].val = (uint8_t)(clamped.lines & 0xffu); out[5].delay_ms = 0u;
    out[6].reg = 0x3540u; out[6].val = out[3].val; out[6].delay_ms = 0u;
    out[7].reg = 0x3541u; out[7].val = out[4].val; out[7].delay_ms = 0u;
    out[8].reg = 0x3542u; out[8].val = out[5].val; out[8].delay_ms = 0u;
    out[9].reg = 0x3508u; out[9].val = (uint8_t)((clamped.gain >> 8) & 0x1fu); out[9].delay_ms = 0u;
    out[10].reg = 0x3509u; out[10].val = (uint8_t)(clamped.gain & 0xffu); out[10].delay_ms = 0u;
    out[11].reg = 0x3548u; out[11].val = out[9].val; out[11].delay_ms = 0u;
    out[12].reg = 0x3549u; out[12].val = out[10].val; out[12].delay_ms = 0u;
    out[13].reg = 0x3208u; out[13].val = 0x13u; out[13].delay_ms = 0u;
    out[14].reg = 0x3208u; out[14].val = 0xa3u; out[14].delay_ms = 0u;
    return FC_OV32C_EXP_REGS;
}

static inline int fc_apply_tab_entries(struct fc_bus *bus, uint8_t slave7, const struct fc_tab_entry *seq, unsigned count)
{
    for (unsigned idx = 0u; idx < count; ++idx) {
        const int rc = fc_i2c_write16_u8(bus, slave7, seq[idx].reg, seq[idx].val);
        if (rc != FC_I2C_OK) return rc;
    }
    return FC_I2C_OK;
}

static inline int fc_ov32c_apply_exposure(struct fc_bus *bus, uint8_t slave7, const struct fc_ov32c_exposure *exp)
{
    struct fc_tab_entry seq[FC_OV32C_EXP_REGS];
    const unsigned count = fc_ov32c_encode_exposure(exp, seq);
    return fc_apply_tab_entries(bus, slave7, seq, count);
}

/* ---- 多鏡頭硬體描述符、IMX800／OV13B10／S5K3K1 曝光控制與統一串流分派（對照 DTS 與廠商 .so／sensormodule） ---- */
#define FC_SENSOR_WIDE 1u
#define FC_SENSOR_FRONT 2u
#define FC_SENSOR_ULTRA 3u
#define FC_SENSOR_TELE 4u
#define FC_GPIO_NONE 0xffffu
#define FC_MAX_SENSOR_RAILS 6u

#define FC_AK7314_SLAVE7 0x0cu
#define FC_OIS_SLAVE7 0x3eu
#define FC_BU24618_CHIP_ID 0x0006cebfu

#define FC_IMX800_SLAVE7 0x10u
#define FC_IMX800_VTS_MIN 3742u
#define FC_IMX800_VTS_30FPS 7484u
#define FC_IMX800_VTS_MAX 0xfff0u
#define FC_IMX800_MIN_LINES 8u
#define FC_IMX800_MARGIN 48u
#define FC_IMX800_LINE_NS 4454u
#define FC_IMX800_GAIN_UNITY 256u
#define FC_IMX800_GAIN_MAX 16384u
#define FC_IMX800_EXP_REGS 11u

#define FC_OV13B10_SLAVE7 0x36u
#define FC_OV13B10_VTS_MIN 1587u
#define FC_OV13B10_VTS_30FPS 3175u
#define FC_OV13B10_VTS_MAX 0xfff0u
#define FC_OV13B10_MIN_LINES 4u
#define FC_OV13B10_MARGIN 8u
#define FC_OV13B10_LINE_NS 10500u
#define FC_OV13B10_GAIN_UNITY 0x0100u
#define FC_OV13B10_GAIN_MAX 0x0f80u
#define FC_OV13B10_EXP_REGS 10u

#define FC_S5K3K1_SLAVE7 0x2du
#define FC_S5K3K1_VTS_MIN 2250u
#define FC_S5K3K1_VTS_30FPS 4500u
#define FC_S5K3K1_VTS_MAX 0xfff0u
#define FC_S5K3K1_MIN_LINES 6u
#define FC_S5K3K1_MARGIN 12u
#define FC_S5K3K1_LINE_NS 7400u
#define FC_S5K3K1_GAIN_UNITY 32u
#define FC_S5K3K1_GAIN_MAX 512u
#define FC_S5K3K1_EXP_REGS 8u
/* Xiaomi 13 sensormodule fuxi_semco_s5k3k1_tele CFA=2 (CamX GRBG) -> camera_isp bayer 1 (GRBG). */
#define FC_S5K3K1_BAYER 1u

static inline void fc_exposure_clamp_core(uint32_t req_lines, uint32_t gain_q, uint32_t req_vts, uint32_t vts_min,
    uint32_t vts_max, uint32_t min_lines, uint32_t margin, struct fc_ov32c_exposure *out)
{
    uint32_t vts = req_vts ? req_vts : vts_min;
    uint32_t lines = req_lines;
    if (vts < vts_min) vts = vts_min;
    if (vts > vts_max) vts = vts_max;
    if (lines < min_lines) lines = min_lines;
    if (lines > vts_max - margin) lines = vts_max - margin;
    if (lines + margin > vts) vts = lines + margin;
    if (vts > vts_max) vts = vts_max;
    out->lines = lines;
    out->gain = gain_q;
    out->frame_lines = vts;
}

static inline uint32_t fc_ov13b10_quantize_gain(uint32_t gain)
{
    if (gain < FC_OV13B10_GAIN_UNITY) gain = FC_OV13B10_GAIN_UNITY;
    if (gain > FC_OV13B10_GAIN_MAX) gain = FC_OV13B10_GAIN_MAX;
    if (gain < 0x0200u) return gain & ~0x0fu;
    if (gain < 0x0400u) return gain & ~0x1fu;
    if (gain < 0x0800u) return gain & ~0x3fu;
    return gain & ~0x7fu;
}

static inline void fc_ov13b10_clamp_exposure(uint32_t req_lines, uint32_t req_gain, uint32_t req_vts,
    struct fc_ov32c_exposure *out)
{
    fc_exposure_clamp_core(req_lines, fc_ov13b10_quantize_gain(req_gain), req_vts, FC_OV13B10_VTS_MIN,
        FC_OV13B10_VTS_MAX, FC_OV13B10_MIN_LINES, FC_OV13B10_MARGIN, out);
}

static inline void fc_ov13b10_clamp_exposure_30fps(uint32_t req_lines, uint32_t req_gain, uint32_t req_vts,
    struct fc_ov32c_exposure *out)
{
    const uint32_t vts = req_vts < FC_OV13B10_VTS_30FPS ? FC_OV13B10_VTS_30FPS : req_vts;
    fc_ov13b10_clamp_exposure(req_lines, req_gain, vts, out);
}

static inline unsigned fc_ov13b10_encode_exposure(const struct fc_ov32c_exposure *exp,
    struct fc_tab_entry out[FC_OV13B10_EXP_REGS])
{
    struct fc_ov32c_exposure clamped;
    fc_ov13b10_clamp_exposure(exp->lines, exp->gain, exp->frame_lines, &clamped);
    out[0].reg = 0x3208u; out[0].val = 0x00u; out[0].delay_ms = 0u;
    out[1].reg = 0x380eu; out[1].val = (uint8_t)((clamped.frame_lines >> 8) & 0xffu); out[1].delay_ms = 0u;
    out[2].reg = 0x380fu; out[2].val = (uint8_t)(clamped.frame_lines & 0xffu); out[2].delay_ms = 0u;
    out[3].reg = 0x3500u; out[3].val = (uint8_t)((clamped.lines >> 16) & 0x0fu); out[3].delay_ms = 0u;
    out[4].reg = 0x3501u; out[4].val = (uint8_t)((clamped.lines >> 8) & 0xffu); out[4].delay_ms = 0u;
    out[5].reg = 0x3502u; out[5].val = (uint8_t)(clamped.lines & 0xffu); out[5].delay_ms = 0u;
    out[6].reg = 0x3508u; out[6].val = (uint8_t)((clamped.gain >> 8) & 0x1fu); out[6].delay_ms = 0u;
    out[7].reg = 0x3509u; out[7].val = (uint8_t)(clamped.gain & 0xffu); out[7].delay_ms = 0u;
    out[8].reg = 0x3208u; out[8].val = 0x10u; out[8].delay_ms = 0u;
    out[9].reg = 0x3208u; out[9].val = 0xa0u; out[9].delay_ms = 0u;
    return FC_OV13B10_EXP_REGS;
}

static inline int fc_ov13b10_apply_exposure(struct fc_bus *bus, uint8_t slave7, const struct fc_ov32c_exposure *exp)
{
    struct fc_tab_entry seq[FC_OV13B10_EXP_REGS];
    const unsigned count = fc_ov13b10_encode_exposure(exp, seq);
    return fc_apply_tab_entries(bus, slave7, seq, count);
}

static inline uint32_t fc_s5k3k1_quantize_gain(uint32_t gain)
{
    if (gain < FC_S5K3K1_GAIN_UNITY) return FC_S5K3K1_GAIN_UNITY;
    if (gain > FC_S5K3K1_GAIN_MAX) return FC_S5K3K1_GAIN_MAX;
    return gain;
}

static inline void fc_s5k3k1_clamp_exposure(uint32_t req_lines, uint32_t req_gain, uint32_t req_vts,
    struct fc_ov32c_exposure *out)
{
    fc_exposure_clamp_core(req_lines, fc_s5k3k1_quantize_gain(req_gain), req_vts, FC_S5K3K1_VTS_MIN,
        FC_S5K3K1_VTS_MAX, FC_S5K3K1_MIN_LINES, FC_S5K3K1_MARGIN, out);
}

static inline void fc_s5k3k1_clamp_exposure_30fps(uint32_t req_lines, uint32_t req_gain, uint32_t req_vts,
    struct fc_ov32c_exposure *out)
{
    const uint32_t vts = req_vts < FC_S5K3K1_VTS_30FPS ? FC_S5K3K1_VTS_30FPS : req_vts;
    fc_s5k3k1_clamp_exposure(req_lines, req_gain, vts, out);
}

static inline unsigned fc_s5k3k1_encode_exposure(const struct fc_ov32c_exposure *exp,
    struct fc_tab_entry out[FC_S5K3K1_EXP_REGS])
{
    struct fc_ov32c_exposure clamped;
    fc_s5k3k1_clamp_exposure(exp->lines, exp->gain, exp->frame_lines, &clamped);
    out[0].reg = 0x0104u; out[0].val = 0x01u; out[0].delay_ms = 0u;
    out[1].reg = 0x0340u; out[1].val = (uint8_t)((clamped.frame_lines >> 8) & 0xffu); out[1].delay_ms = 0u;
    out[2].reg = 0x0341u; out[2].val = (uint8_t)(clamped.frame_lines & 0xffu); out[2].delay_ms = 0u;
    out[3].reg = 0x0202u; out[3].val = (uint8_t)((clamped.lines >> 8) & 0xffu); out[3].delay_ms = 0u;
    out[4].reg = 0x0203u; out[4].val = (uint8_t)(clamped.lines & 0xffu); out[4].delay_ms = 0u;
    out[5].reg = 0x0204u; out[5].val = (uint8_t)((clamped.gain >> 8) & 0xffu); out[5].delay_ms = 0u;
    out[6].reg = 0x0205u; out[6].val = (uint8_t)(clamped.gain & 0xffu); out[6].delay_ms = 0u;
    out[7].reg = 0x0104u; out[7].val = 0x00u; out[7].delay_ms = 0u;
    return FC_S5K3K1_EXP_REGS;
}

static inline int fc_s5k3k1_apply_exposure(struct fc_bus *bus, uint8_t slave7, const struct fc_ov32c_exposure *exp)
{
    struct fc_tab_entry seq[FC_S5K3K1_EXP_REGS];
    const unsigned count = fc_s5k3k1_encode_exposure(exp, seq);
    return fc_apply_tab_entries(bus, slave7, seq, count);
}

/* IMX800 Sony 類比增益暫存器轉換（Q8 固定點 256=1.0x）：reg = clamp(16384 - round(16384 * 256 / gain_q8), 0, 0x3f00)。 */
static inline uint32_t fc_imx800_gain_to_reg(uint32_t gain_q8)
{
    if (gain_q8 <= FC_IMX800_GAIN_UNITY) return 0u;
    const uint32_t denom = (16384u * FC_IMX800_GAIN_UNITY + gain_q8 / 2u) / gain_q8;
    if (denom >= 16384u) return 0u;
    const uint32_t reg = 16384u - denom;
    return reg > 0x3f00u ? 0x3f00u : reg;
}

static inline uint32_t fc_imx800_quantize_gain(uint32_t gain_q8)
{
    if (gain_q8 < FC_IMX800_GAIN_UNITY) return FC_IMX800_GAIN_UNITY;
    if (gain_q8 > FC_IMX800_GAIN_MAX) return FC_IMX800_GAIN_MAX;
    return gain_q8;
}

#define FC_IMX800_LSHIFT_THRESHOLD 0xffcfu
#define FC_IMX800_LSHIFT_MAX_LINES 0x7fe700u

static inline uint8_t fc_imx800_calc_lshift(uint32_t req_lines, uint32_t *cit_out)
{
    if (req_lines < FC_IMX800_LSHIFT_THRESHOLD) {
        uint32_t cit = req_lines < FC_IMX800_MIN_LINES ? FC_IMX800_MIN_LINES : req_lines;
        if (cit > FC_IMX800_VTS_MAX - FC_IMX800_MARGIN) cit = FC_IMX800_VTS_MAX - FC_IMX800_MARGIN;
        if (cit_out) *cit_out = cit;
        return 0u;
    }
    uint8_t lshift;
    uint32_t div;
    if (req_lines < 0x1ff9du)       { lshift = 1u; div = 2u; }
    else if (req_lines < 0x3ff39u)  { lshift = 2u; div = 4u; }
    else if (req_lines < 0x7fe71u)  { lshift = 3u; div = 8u; }
    else if (req_lines < 0xffce1u)  { lshift = 4u; div = 16u; }
    else if (req_lines < 0x1ff9c1u) { lshift = 5u; div = 32u; }
    else if (req_lines <= 0x3ff380u){ lshift = 6u; div = 64u; }
    else                            { lshift = 7u; div = 128u; }
    uint32_t cit = (req_lines > FC_IMX800_LSHIFT_MAX_LINES) ? 0xffceu : (req_lines / div);
    if (cit > FC_IMX800_VTS_MAX - FC_IMX800_MARGIN) cit = FC_IMX800_VTS_MAX - FC_IMX800_MARGIN;
    if (cit_out) *cit_out = cit;
    return lshift;
}

static inline void fc_imx800_clamp_exposure(uint32_t req_lines, uint32_t req_gain_q8, uint32_t req_vts,
    struct fc_ov32c_exposure *out)
{
    if (req_lines < FC_IMX800_LSHIFT_THRESHOLD) {
        fc_exposure_clamp_core(req_lines, fc_imx800_quantize_gain(req_gain_q8), req_vts, FC_IMX800_VTS_MIN,
            FC_IMX800_VTS_MAX, FC_IMX800_MIN_LINES, FC_IMX800_MARGIN, out);
        return;
    }
    uint32_t cit = 0u;
    (void)fc_imx800_calc_lshift(req_lines, &cit);
    struct fc_ov32c_exposure tmp;
    fc_exposure_clamp_core(cit, fc_imx800_quantize_gain(req_gain_q8), req_vts, FC_IMX800_VTS_MIN,
        FC_IMX800_VTS_MAX, FC_IMX800_MIN_LINES, FC_IMX800_MARGIN, &tmp);
    out->lines = req_lines > FC_IMX800_LSHIFT_MAX_LINES ? FC_IMX800_LSHIFT_MAX_LINES : req_lines;
    out->gain = tmp.gain;
    out->frame_lines = tmp.frame_lines;
}

static inline void fc_imx800_clamp_exposure_30fps(uint32_t req_lines, uint32_t req_gain_q8, uint32_t req_vts,
    struct fc_ov32c_exposure *out)
{
    const uint32_t vts = req_vts < FC_IMX800_VTS_30FPS ? FC_IMX800_VTS_30FPS : req_vts;
    fc_imx800_clamp_exposure(req_lines, req_gain_q8, vts, out);
}

static inline unsigned fc_imx800_encode_exposure(const struct fc_ov32c_exposure *exp,
    struct fc_tab_entry out[FC_IMX800_EXP_REGS])
{
    struct fc_ov32c_exposure clamped;
    fc_imx800_clamp_exposure(exp->lines, exp->gain, exp->frame_lines, &clamped);
    uint32_t cit = 0u;
    const uint8_t lshift = fc_imx800_calc_lshift(clamped.lines, &cit);
    const uint32_t again_reg = fc_imx800_gain_to_reg(clamped.gain);
    out[0].reg = 0x0104u; out[0].val = 0x01u; out[0].delay_ms = 0u;
    out[1].reg = 0x3150u; out[1].val = lshift; out[1].delay_ms = 0u;
    out[2].reg = 0x0340u; out[2].val = (uint8_t)((clamped.frame_lines >> 8) & 0xffu); out[2].delay_ms = 0u;
    out[3].reg = 0x0341u; out[3].val = (uint8_t)(clamped.frame_lines & 0xffu); out[3].delay_ms = 0u;
    out[4].reg = 0x0202u; out[4].val = (uint8_t)((cit >> 8) & 0xffu); out[4].delay_ms = 0u;
    out[5].reg = 0x0203u; out[5].val = (uint8_t)(cit & 0xffu); out[5].delay_ms = 0u;
    out[6].reg = 0x0204u; out[6].val = (uint8_t)((again_reg >> 8) & 0xffu); out[6].delay_ms = 0u;
    out[7].reg = 0x0205u; out[7].val = (uint8_t)(again_reg & 0xffu); out[7].delay_ms = 0u;
    out[8].reg = 0x020eu; out[8].val = 0x01u; out[8].delay_ms = 0u;
    out[9].reg = 0x020fu; out[9].val = 0x00u; out[9].delay_ms = 0u;
    out[10].reg = 0x0104u; out[10].val = 0x00u; out[10].delay_ms = 0u;
    return FC_IMX800_EXP_REGS;
}

static inline int fc_imx800_apply_exposure(struct fc_bus *bus, uint8_t slave7, const struct fc_ov32c_exposure *exp)
{
    struct fc_tab_entry seq[FC_IMX800_EXP_REGS];
    const unsigned count = fc_imx800_encode_exposure(exp, seq);
    return fc_apply_tab_entries(bus, slave7, seq, count);
}

/* AK7314 預設起始停駐位置（中遠距 360，對齊 [300, 900] 映射約 62.5 cm）與寫入後穩定等待毫秒數。 */
#define FC_AK7314_DEFAULT_INIT_POS 360u
#define FC_AK7314_SETTLE_MS        15u

/* AK7314 10-bit 位置 (0..1023) 編解碼：寫暫存器 0x00 為 (pos << 6)，讀 0x84 再右移 6。 */
static inline uint16_t fc_ak7314_encode_pos(uint32_t focus)
{
    const uint32_t clamped = focus > 1023u ? 1023u : focus;
    return (uint16_t)(clamped << 6);
}

static inline uint32_t fc_ak7314_decode_pos(uint16_t raw)
{
    return (uint32_t)((raw >> 6) & 0x3ffu);
}

/*
 * OIS 晶片辨識（BU24618 vs BU24721）：
 * 1) 先唯讀 0xf008（BU24618 chip_id == 0x0006cebf）；
 * 2) 再唯讀 0xf024（BU24721 ACK == 0x01）；
 * 3) 絕不在未確認前寫入 0xf010=0x00；僅當 0xf024 != 1 且 0xf008 未回傳 BU24721 值時，
 *    對 Tele 二供嘗試 BU24618 待機復位 (0xf010=0x00) 後輪詢 0xf008。
 */
static inline void fc_ois_detect_24618(struct fc_bus *bus, uint8_t slave7, uint32_t sensor_id,
    int *is_24618_out, unsigned wait_step_us)
{
    uint32_t chip_id = 0u;
    const int rc_id = fc_i2c_read16_u32(bus, slave7, 0xf008u, &chip_id);
    if (rc_id == FC_I2C_OK && chip_id == FC_BU24618_CHIP_ID) {
        *is_24618_out = 1;
        return;
    }
    uint8_t ack24721 = 0u;
    const int rc_ack = fc_i2c_read16(bus, slave7, 0xf024u, &ack24721, 1u);
    if (rc_ack == FC_I2C_OK && ack24721 == 0x01u) {
        *is_24618_out = 0;
        return;
    }
    if (sensor_id == FC_SENSOR_TELE && !(rc_id == FC_I2C_OK && chip_id != 0u && chip_id != FC_BU24618_CHIP_ID)) {
        if (fc_i2c_write16_u8(bus, slave7, 0xf010u, 0x00u) == FC_I2C_OK) {
            bus->ops->wait_us(bus->context, 200u);
            if (fc_i2c_poll16_u32(bus, slave7, 0xf008u, FC_BU24618_CHIP_ID, 5u, wait_step_us) == FC_I2C_OK) {
                *is_24618_out = 1;
                return;
            }
        }
    }
    *is_24618_out = 0;
}

static inline int fc_ois_release(struct fc_bus *bus, uint8_t slave7, int ois_ok, uint8_t is_24618, unsigned poll_wait_us)
{
    if (!bus || !slave7 || !ois_ok) return FC_I2C_OK;
    if (is_24618) return fc_i2c_write16_u8(bus, slave7, 0x6020u, 0x00u);
    const int rc = fc_i2c_write16_u8(bus, slave7, 0xf020u, 0x01u);
    if (rc != FC_I2C_OK) return rc;
    return fc_i2c_poll16_u8(bus, slave7, 0xf024u, 0x01u, 30u, poll_wait_us);
}

static inline int fc_str_eq(const char *a, const char *b)
{
    if (!a || !b) return 0;
    while (*a && *b) {
        if (*a != *b) return 0;
        ++a;
        ++b;
    }
    return *a == *b;
}

/* A-05 共用/系統電源軌硬防護欄：絕不送 off（PM8550/PM8550VS 共用 BOB 與 I/O/VAF 軌）。 */
static inline int fc_rail_must_keep_on(const char *name)
{
    if (!name) return 0;
    return fc_str_eq(name, "bobb1") || fc_str_eq(name, "bobb2") || fc_str_eq(name, "ldob14") ||
        fc_str_eq(name, "ldob16") || fc_str_eq(name, "ldoc2");
}

struct fc_sensor_rail {
    const char *role;
    const char *name;
    uint32_t mv;
    uint8_t mode;
    uint8_t shared_keep_on;
};

struct fc_sensor_hw {
    uint32_t sensor_id;
    const char *name;
    const char *adt_tab_prop;
    uint16_t pin_mclk;
    uint16_t pin_reset;
    uint16_t pin_avdd;
    uint16_t pin_avdd2;
    uint16_t pin_sda;
    uint16_t pin_scl;
    uint8_t mclk_func;
    uint8_t mclk_ma;
    uint8_t csiphy_idx;
    uint8_t phytimer_pll_src;
    uint8_t phytimer_pll_hid;
    uint8_t default_slave7;
    uint8_t wake_aon_slave7;
    uint8_t is_cphy;
    uint8_t actuator_slave7;
    uint8_t ois_slave7;
    uint8_t rail_count;
    uint32_t mclk_rcg;
    uint32_t mclk_cbcr;
    uint32_t cci_rcg;
    uint32_t cci_cbcr;
    uint32_t csiphy_cbcr;
    uint32_t phytimer_rcg;
    uint32_t phytimer_cbcr;
    uint64_t phy_page;
    uint32_t phy_off;
    uint32_t bayer;
    uint32_t black;
    uint32_t line_ns;
    uint32_t fps_30_milli;
    uint32_t gain_unity;
    uint32_t gain_max;
    uint32_t vts_min;
    uint32_t vts_30fps;
    uint32_t vts_max;
    uint32_t min_lines;
    uint32_t margin;
    uint32_t default_lines;
    uint32_t default_gain;
    struct fc_sensor_rail rails[FC_MAX_SENSOR_RAILS];
};

static inline const struct fc_sensor_hw *fc_sensor_hw_for(uint32_t sensor_id)
{
    static const struct fc_sensor_hw kFront = {
        .sensor_id = FC_SENSOR_FRONT,
        .name = "front-ov32c",
        .adt_tab_prop = "fuxi,camera-tab",
        .pin_mclk = 104u,
        .pin_reset = 120u,
        .pin_avdd = FC_GPIO_NONE,
        .pin_avdd2 = FC_GPIO_NONE,
        .pin_sda = 208u,
        .pin_scl = 209u,
        .mclk_func = 1u,
        .mclk_ma = 4u,
        .csiphy_idx = 4u,
        .phytimer_pll_src = 3u,
        .phytimer_pll_hid = 1u,
        .default_slave7 = FC_OV32C_SLAVE7,
        .wake_aon_slave7 = 0x18u,
        .is_cphy = 0u,
        .actuator_slave7 = 0u,
        .ois_slave7 = 0u,
        .rail_count = 3u,
        .mclk_rcg = 0x154c0u,
        .mclk_cbcr = 0x155ecu,
        .cci_rcg = 0x13a30u,
        .cci_cbcr = 0x13b5cu,
        .csiphy_cbcr = 0x15f84u,
        .phytimer_rcg = 0x15e54u,
        .phytimer_cbcr = 0x15f80u,
        .phy_page = 0xacec000ull,
        .phy_off = 0u,
        .bayer = FC_OV32C_BAYER,
        .black = FC_OV32C_BLACK,
        .line_ns = FC_OV32C_LINE_NS,
        .fps_30_milli = FC_OV32C_FPS_30_MILLI,
        .gain_unity = FC_OV32C_GAIN_UNITY,
        .gain_max = FC_OV32C_GAIN_MAX,
        .vts_min = FC_OV32C_VTS_MIN,
        .vts_30fps = FC_OV32C_VTS_30FPS,
        .vts_max = FC_OV32C_VTS_MAX,
        .min_lines = FC_OV32C_MIN_LINES,
        .margin = FC_OV32C_MARGIN,
        .default_lines = 2400u,
        .default_gain = 0x0400u,
        .rails = {
            {"vana", "ldom3", 2800u, FC_VRM_MODE_HPM, 0u},
            {"vio", "ldom5", 1800u, FC_VRM_MODE_HPM, 0u},
            {"vdig", "ldom1", 1104u, FC_VRM_MODE_HPM, 0u},
        },
    };
    static const struct fc_sensor_hw kUltra = {
        .sensor_id = FC_SENSOR_ULTRA,
        .name = "ultra-ov13b10",
        .adt_tab_prop = "fuxi,camera-tab-ultra",
        .pin_mclk = 101u,
        .pin_reset = 117u,
        .pin_avdd = 91u,
        .pin_avdd2 = FC_GPIO_NONE,
        .pin_sda = 110u,
        .pin_scl = 111u,
        .mclk_func = 1u,
        .mclk_ma = 6u,
        .csiphy_idx = 0u,
        .phytimer_pll_src = 2u,
        .phytimer_pll_hid = 2u,
        .default_slave7 = FC_OV13B10_SLAVE7,
        .wake_aon_slave7 = 0u,
        .is_cphy = 0u,
        .actuator_slave7 = 0u,
        .ois_slave7 = 0u,
        .rail_count = 3u,
        .mclk_rcg = 0x15130u,
        .mclk_cbcr = 0x1525cu,
        .cci_rcg = 0x13900u,
        .cci_cbcr = 0x13a2cu,
        .csiphy_cbcr = 0x15ab0u,
        .phytimer_rcg = 0x15980u,
        .phytimer_cbcr = 0x15aacu,
        .phy_page = 0xace4000ull,
        .phy_off = 0u,
        .bayer = 3u,
        .black = 64u,
        .line_ns = FC_OV13B10_LINE_NS,
        .fps_30_milli = 30000u,
        .gain_unity = FC_OV13B10_GAIN_UNITY,
        .gain_max = FC_OV13B10_GAIN_MAX,
        .vts_min = FC_OV13B10_VTS_MIN,
        .vts_30fps = FC_OV13B10_VTS_30FPS,
        .vts_max = FC_OV13B10_VTS_MAX,
        .min_lines = FC_OV13B10_MIN_LINES,
        .margin = FC_OV13B10_MARGIN,
        .default_lines = 2000u,
        .default_gain = 0x0200u,
        .rails = {
            {"bob2", "bobb2", 3104u, FC_VRM_MODE_BOB_AUTO, 1u},
            {"vdig", "ldoc1", 1200u, FC_VRM_MODE_HPM, 0u},
            {"vio", "ldoc2", 1800u, FC_VRM_MODE_HPM, 1u},
        },
    };
    static const struct fc_sensor_hw kTele = {
        .sensor_id = FC_SENSOR_TELE,
        .name = "tele-s5k3k1",
        .adt_tab_prop = "fuxi,camera-tab-tele",
        .pin_mclk = 103u,
        .pin_reset = 119u,
        .pin_avdd = 57u,
        .pin_avdd2 = FC_GPIO_NONE,
        .pin_sda = 114u,
        .pin_scl = 115u,
        .mclk_func = 1u,
        .mclk_ma = 6u,
        .csiphy_idx = 1u,
        .phytimer_pll_src = 2u,
        .phytimer_pll_hid = 2u,
        .default_slave7 = FC_S5K3K1_SLAVE7,
        .wake_aon_slave7 = 0u,
        .is_cphy = 0u,
        .actuator_slave7 = FC_AK7314_SLAVE7,
        .ois_slave7 = FC_OIS_SLAVE7,
        .rail_count = 6u,
        .mclk_rcg = 0x15390u,
        .mclk_cbcr = 0x154bcu,
        .cci_rcg = 0x13a30u,
        .cci_cbcr = 0x13b5cu,
        .csiphy_cbcr = 0x15be8u,
        .phytimer_rcg = 0x15ab8u,
        .phytimer_cbcr = 0x15be4u,
        .phy_page = 0xace4000ull,
        .phy_off = 0x2000u,
        .bayer = FC_S5K3K1_BAYER,
        .black = 64u,
        .line_ns = FC_S5K3K1_LINE_NS,
        .fps_30_milli = 30000u,
        .gain_unity = FC_S5K3K1_GAIN_UNITY,
        .gain_max = FC_S5K3K1_GAIN_MAX,
        .vts_min = FC_S5K3K1_VTS_MIN,
        .vts_30fps = FC_S5K3K1_VTS_30FPS,
        .vts_max = FC_S5K3K1_VTS_MAX,
        .min_lines = FC_S5K3K1_MIN_LINES,
        .margin = FC_S5K3K1_MARGIN,
        .default_lines = 3000u,
        .default_gain = 64u,
        .rails = {
            {"bob", "bobb1", 3600u, FC_VRM_MODE_BOB_AUTO, 1u},
            {"bob2", "bobb2", 3104u, FC_VRM_MODE_BOB_AUTO, 1u},
            {"vaf", "ldob14", 3104u, FC_VRM_MODE_HPM, 1u},
            {"vio", "ldoc2", 1800u, FC_VRM_MODE_HPM, 1u},
            {"custom1", "ldob16", 3104u, FC_VRM_MODE_HPM, 1u},
            {"vdig", "ldob11", 1056u, FC_VRM_MODE_HPM, 0u},
        },
    };
    static const struct fc_sensor_hw kWide = {
        .sensor_id = FC_SENSOR_WIDE,
        .name = "wide-imx800",
        .adt_tab_prop = "fuxi,camera-tab-wide",
        .pin_mclk = 102u,
        .pin_reset = 118u,
        .pin_avdd = 69u,
        .pin_avdd2 = 66u,
        .pin_sda = 112u,
        .pin_scl = 113u,
        .mclk_func = 1u,
        .mclk_ma = 6u,
        .csiphy_idx = 2u,
        .phytimer_pll_src = 2u,
        .phytimer_pll_hid = 2u,
        .default_slave7 = FC_IMX800_SLAVE7,
        .wake_aon_slave7 = 0u,
        .is_cphy = 1u,
        .actuator_slave7 = FC_AK7314_SLAVE7,
        .ois_slave7 = FC_OIS_SLAVE7,
        .rail_count = 5u,
        .mclk_rcg = 0x15260u,
        .mclk_cbcr = 0x1538cu,
        .cci_rcg = 0x13900u,
        .cci_cbcr = 0x13a2cu,
        .csiphy_cbcr = 0x15d1cu,
        .phytimer_rcg = 0x15becu,
        .phytimer_cbcr = 0x15d18u,
        .phy_page = 0xace8000ull,
        .phy_off = 0u,
        .bayer = 3u,
        .black = 64u,
        .line_ns = FC_IMX800_LINE_NS,
        .fps_30_milli = 30000u,
        .gain_unity = FC_IMX800_GAIN_UNITY,
        .gain_max = FC_IMX800_GAIN_MAX,
        .vts_min = FC_IMX800_VTS_MIN,
        .vts_30fps = FC_IMX800_VTS_30FPS,
        .vts_max = FC_IMX800_VTS_MAX,
        .min_lines = FC_IMX800_MIN_LINES,
        .margin = FC_IMX800_MARGIN,
        .default_lines = 4000u,
        .default_gain = 1024u,
        .rails = {
            {"bob2", "bobb2", 3104u, FC_VRM_MODE_BOB_AUTO, 1u},
            {"vaf", "ldom4", 3104u, FC_VRM_MODE_HPM, 0u},
            {"custom2", "ldom7", 3104u, FC_VRM_MODE_HPM, 0u},
            {"vdig", "ldom2", 1104u, FC_VRM_MODE_HPM, 0u},
            {"vio", "ldoc2", 1800u, FC_VRM_MODE_HPM, 1u},
        },
    };
    switch (sensor_id) {
    case FC_SENSOR_WIDE: return &kWide;
    case FC_SENSOR_FRONT: return &kFront;
    case FC_SENSOR_ULTRA: return &kUltra;
    case FC_SENSOR_TELE: return &kTele;
    default: return 0;
    }
}

static inline uint32_t fc_sensor_vts_floor(const struct fc_sensor_hw *hw, uint32_t mode)
{
    if (!hw) return 0u;
    if (mode == FC_MODE_VIDEO60) {
        if (hw->sensor_id == FC_SENSOR_FRONT) {
            const uint32_t v60 = hw->vts_30fps / 2u;
            return v60 > hw->vts_min ? v60 : hw->vts_min;
        }
        return hw->vts_min;
    }
    return hw->vts_30fps;
}

/* 強制物理時序不變量：VTS 下限絕不可小於作用中輸出高度加上垂直消隱邊際 (active_height + margin)。 */
static inline uint32_t fc_sensor_vts_floor_for_height(const struct fc_sensor_hw *hw, uint32_t mode,
    uint32_t active_height)
{
    if (!hw) return 0u;
    uint32_t vts_floor = fc_sensor_vts_floor(hw, mode);
    const uint32_t min_phys = active_height ? (active_height + hw->margin) : 0u;
    if (min_phys > 0u && vts_floor < min_phys) {
        vts_floor = (hw->vts_30fps >= min_phys) ? hw->vts_30fps : min_phys;
    }
    if (vts_floor > hw->vts_max) {
        vts_floor = hw->vts_max;
    }
    return vts_floor;
}

static inline uint32_t fc_sensor_fps_milli(uint32_t line_ns, uint32_t vts_floor)
{
    if (!line_ns || !vts_floor) return 30000u;
    const uint64_t denom = (uint64_t)line_ns * (uint64_t)vts_floor;
    return (uint32_t)((1000000000000ull + denom / 2ull) / denom);
}

static inline int fc_sensor_needs_480mhz(uint32_t width, uint32_t height, uint32_t fps_milli)
{
    return (uint64_t)width * (uint64_t)height * (uint64_t)fps_milli >= 400000000000ull;
}

static inline int fc_sensor_clamp_exposure_for_height(uint32_t sensor_id, uint32_t mode, uint32_t active_height,
    uint32_t req_lines, uint32_t req_gain, uint32_t req_vts, struct fc_ov32c_exposure *out)
{
    const struct fc_sensor_hw *hw = fc_sensor_hw_for(sensor_id);
    if (!hw || !out) return -1;
    const uint32_t floor_vts = fc_sensor_vts_floor_for_height(hw, mode, active_height);
    const uint32_t vts = req_vts < floor_vts ? floor_vts : req_vts;
    switch (sensor_id) {
    case FC_SENSOR_WIDE:
        fc_imx800_clamp_exposure(req_lines, req_gain, vts, out);
        break;
    case FC_SENSOR_FRONT:
        fc_ov32c_clamp_exposure(req_lines, req_gain, vts, out);
        break;
    case FC_SENSOR_ULTRA:
        fc_ov13b10_clamp_exposure(req_lines, req_gain, vts, out);
        break;
    case FC_SENSOR_TELE:
        fc_s5k3k1_clamp_exposure(req_lines, req_gain, vts, out);
        break;
    default:
        return -1;
    }
    if (out->frame_lines < floor_vts) {
        out->frame_lines = floor_vts;
    }
    return 0;
}

static inline int fc_sensor_clamp_exposure_mode(uint32_t sensor_id, uint32_t mode, uint32_t req_lines,
    uint32_t req_gain, uint32_t req_vts, struct fc_ov32c_exposure *out)
{
    return fc_sensor_clamp_exposure_for_height(sensor_id, mode, 0u, req_lines, req_gain, req_vts, out);
}

static inline int fc_sensor_clamp_exposure_30fps(uint32_t sensor_id, uint32_t req_lines, uint32_t req_gain, uint32_t req_vts,
    struct fc_ov32c_exposure *out)
{
    return fc_sensor_clamp_exposure_mode(sensor_id, FC_MODE_PHOTO, req_lines, req_gain, req_vts, out);
}

static inline int fc_sensor_apply_exposure(uint32_t sensor_id, struct fc_bus *bus, uint8_t slave7,
    const struct fc_ov32c_exposure *exp)
{
    switch (sensor_id) {
    case FC_SENSOR_WIDE: return fc_imx800_apply_exposure(bus, slave7, exp);
    case FC_SENSOR_FRONT: return fc_ov32c_apply_exposure(bus, slave7, exp);
    case FC_SENSOR_ULTRA: return fc_ov13b10_apply_exposure(bus, slave7, exp);
    case FC_SENSOR_TELE: return fc_s5k3k1_apply_exposure(bus, slave7, exp);
    default: return FC_I2C_REFUSED;
    }
}

static inline int fc_sensor_stream_on(uint32_t sensor_id, struct fc_bus *bus, uint8_t slave7)
{
    if (sensor_id == FC_SENSOR_WIDE) {
        const int rc = fc_i2c_write16_u8(bus, slave7, 0x0101u, 0x03u);
        if (rc != FC_I2C_OK) return rc;
        return fc_i2c_write16_u8(bus, slave7, 0x0100u, 0x01u);
    }
    if (sensor_id == FC_SENSOR_TELE) {
        const int rc = fc_i2c_write16_u16(bus, slave7, 0x0100u, 0x0100u);
        if (rc != FC_I2C_OK) return rc;
        return fc_i2c_write16_u16(bus, slave7, 0x6028u, 0x4000u);
    }
    if (sensor_id == FC_SENSOR_FRONT || sensor_id == FC_SENSOR_ULTRA) {
        return fc_i2c_write16_u8(bus, slave7, 0x0100u, 0x01u);
    }
    return FC_I2C_REFUSED;
}

static inline int fc_sensor_stream_off(uint32_t sensor_id, struct fc_bus *bus, uint8_t slave7)
{
    if (sensor_id == FC_SENSOR_TELE) {
        const int rc = fc_i2c_write16_u8(bus, slave7, 0x0100u, 0x00u);
        if (rc != FC_I2C_OK) return rc;
        bus->ops->wait_us(bus->context, 100u);
        return fc_i2c_poll16_u8(bus, slave7, 0x0005u, 0xffu, 30u, 1000u);
    }
    if (sensor_id == FC_SENSOR_WIDE || sensor_id == FC_SENSOR_FRONT || sensor_id == FC_SENSOR_ULTRA) {
        return fc_i2c_write16_u8(bus, slave7, 0x0100u, 0x00u);
    }
    return FC_I2C_REFUSED;
}

static inline int fc_smmu_fsr_fault(uint32_t fsr)
{
    return (fsr & 0x800001feu) != 0u;
}

static inline uint32_t fc_raw10_mean_sample(const uint8_t *buf, uint32_t stride, uint32_t width, uint32_t height,
    uint32_t *out_min10, uint32_t *out_max10)
{
    if (!buf || width < 4u || !height || stride < fc_raw10_bytes_per_line(width)) {
        if (out_min10) *out_min10 = 0u;
        if (out_max10) *out_max10 = 0u;
        return 0u;
    }
    const uint32_t row_step = height >= 64u ? 8u : 1u;
    const uint32_t groups = width / 4u;
    const uint32_t grp_step = groups >= 64u ? 8u : 1u;
    uint64_t sum = 0u;
    uint32_t count = 0u;
    uint32_t min_v = 1023u;
    uint32_t max_v = 0u;
    for (uint32_t row = 0u; row < height; row += row_step) {
        const uint8_t *line = buf + (uint64_t)row * stride;
        for (uint32_t grp = 0u; grp < groups; grp += grp_step) {
            const uint8_t *ptr = line + (uint64_t)grp * 5u;
            const uint8_t lsbs = ptr[4];
            const uint32_t px[4] = {
                ((uint32_t)ptr[0] << 2) | (lsbs & 3u),
                ((uint32_t)ptr[1] << 2) | ((lsbs >> 2) & 3u),
                ((uint32_t)ptr[2] << 2) | ((lsbs >> 4) & 3u),
                ((uint32_t)ptr[3] << 2) | ((lsbs >> 6) & 3u),
            };
            for (unsigned lane = 0u; lane < 4u; ++lane) {
                sum += px[lane];
                if (px[lane] < min_v) min_v = px[lane];
                if (px[lane] > max_v) max_v = px[lane];
                count++;
            }
        }
    }
    if (out_min10) *out_min10 = count ? min_v : 0u;
    if (out_max10) *out_max10 = count ? max_v : 0u;
    return count ? (uint32_t)(sum / count) : 0u;
}

#define FC_RING_SLOTS 3u
#define FC_SLOT_FREE 0u
#define FC_SLOT_WRITING 1u
#define FC_SLOT_READY 2u
#define FC_SLOT_READING 3u

struct fc_ring_slot {
    uint8_t state;
    uint64_t sequence;
    uint64_t time;
    uint64_t sof_time;
    uint32_t exposure_lines;
    uint32_t gain;
    uint32_t frame_lines;
};

static inline int fc_ring_pick_next_write(const struct fc_ring_slot slots[FC_RING_SLOTS])
{
    int oldest_ready = -1;
    for (unsigned idx = 0u; idx < FC_RING_SLOTS; ++idx) {
        if (slots[idx].state == FC_SLOT_FREE) return (int)idx;
        if (slots[idx].state == FC_SLOT_READY) {
            if (oldest_ready < 0 || slots[idx].sequence < slots[oldest_ready].sequence) oldest_ready = (int)idx;
        }
    }
    return oldest_ready;
}

static inline int fc_ring_pick_newest_read(const struct fc_ring_slot slots[FC_RING_SLOTS], uint64_t last_given)
{
    int newest = -1;
    for (unsigned idx = 0u; idx < FC_RING_SLOTS; ++idx) {
        if (slots[idx].state == FC_SLOT_READY && slots[idx].sequence > last_given) {
            if (newest < 0 || slots[idx].sequence > slots[newest].sequence) newest = (int)idx;
        }
    }
    return newest;
}

static inline int fc_elapsed_exceeded(uint64_t now_ms, uint64_t ref_ms, uint64_t limit_ms)
{
    return now_ms > ref_ms && (now_ms - ref_ms) > limit_ms;
}

#define FUXI_CAMERA_FLASH_OFF 0u
#define FUXI_CAMERA_FLASH_TORCH_FILL 1u
#define FUXI_CAMERA_FLASH_STROBE 2u
#define FUXI_CAMERA_FLASH_MAX_LEVEL 4u
#define FUXI_CAMERA_FLASH_DEFAULT_MS 2000u

static inline int fc_clamp_flash_cmd(uint32_t level, uint32_t duration_ms, uint32_t mode,
    uint32_t *out_level, uint32_t *out_ms, uint32_t *out_mode)
{
    if (out_level) *out_level = 0u;
    if (out_ms) *out_ms = 0u;
    if (out_mode) *out_mode = FUXI_CAMERA_FLASH_OFF;
    if (mode == FUXI_CAMERA_FLASH_OFF || (mode == FUXI_CAMERA_FLASH_TORCH_FILL && level == 0u)) {
        return 0;
    }
    if (mode == FUXI_CAMERA_FLASH_STROBE) {
        return -1; /* High-current PMIC strobe unverified: fail closed (ENOTSUP) */
    }
    if (mode != FUXI_CAMERA_FLASH_TORCH_FILL) {
        return -2;
    }
    uint32_t lv = level;
    if (lv > FUXI_CAMERA_FLASH_MAX_LEVEL) lv = FUXI_CAMERA_FLASH_MAX_LEVEL;
    uint32_t ms = duration_ms == 0u ? FUXI_CAMERA_FLASH_DEFAULT_MS : duration_ms;
    if (ms > FUXI_CAMERA_FLASH_DEFAULT_MS) ms = FUXI_CAMERA_FLASH_DEFAULT_MS;
    if (out_level) *out_level = lv;
    if (out_ms) *out_ms = ms;
    if (out_mode) *out_mode = FUXI_CAMERA_FLASH_TORCH_FILL;
    return 1;
}

static inline int fc_flash_step_lit(int *active, uint32_t *level, uint64_t *until_ms, uint64_t now_ms)
{
    if (!active || !*active) {
        return 0;
    }
    const uint64_t until = until_ms ? *until_ms : 0ull;
    const uint32_t lv = level ? *level : 0u;
    if (until != 0ull && now_ms <= until && lv != 0u) {
        return 1;
    }
    *active = 0;
    if (level) *level = 0u;
    if (until_ms) *until_ms = 0ull;
    return 0;
}
#endif
