#!/usr/bin/env python3
"""主機測試：xnu/fuxi-camera/fuxi_camera_proto.h（核心 fuxi_camera.c 用的同一份純邏輯）。

涵蓋 TLMM CTL 編碼（eGPIO、驅動電流、上下拉、輸出致能）、MCLK RCG 的 XO 設定值、titan GDSC 狀態判斷、RPMh VRM 命令與
cmd-db 位址類型；手動 I2C 引擎接上一個位元層級的開汲匯流排模擬（感測器從端：16 位元暫存器位址；EEPROM 從端：任何寫入
位元組都記為毀損），驗 quick write／讀位元組探測、16 位元暫存器讀、重複 START、目前位址讀、clock stretching、時脈被拉住、
資料線卡在位元組中間的 9 時脈救回，以及「EEPROM 範圍永不送寫入位元組」；最後亂數位址／卡住狀態跑完整掃描流程，
搭配 --sanitize（ASan／UBSan）確認不越界。
"""
import argparse
import os
from pathlib import Path
import runpy
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fuxi_camera_proto.h"
static unsigned checks;
#define CHECK(test) do { checks++; if (!(test)) { fprintf(stderr, "line %d: %s\n", __LINE__, #test); exit(1); } } while (0)

/* ---- 位元層級的開汲匯流排與從端 ---- */
enum { S_IDLE, S_ADDR, S_AACK, S_RX, S_RACK, S_TX, S_TXACK, S_IGNORE };
typedef struct {
    uint8_t address;
    int eeprom; /* 1：EEPROM（只做目前位址讀；任何寫入位元組都算毀損） */
    int reg8;   /* 1：8-bit 暫存器位址裝置（如 AK7314） */
    int wake_24618_on_f010;
    int state, bits, rw, master_ack, bitpos, sda_low;
    uint8_t shift, out;
    uint16_t pointer;
    unsigned received, data_writes, eeprom_bytes_written, acked_addresses;
    unsigned write_f010_count, write_f020_count, write_6020_count;
    uint8_t regs[0x10000];
} Slave;
typedef struct {
    int master_sda_low, master_scl_low, sda, scl;
    Slave *slaves[4];
    unsigned nslaves;
    unsigned stretch_each, stretching;
    int stretch_forever;
    unsigned waits;
} Bus;
#define PIN_SDA 208u
#define PIN_SCL 209u

static uint8_t s_read(Slave *s) { const uint8_t v = s->eeprom ? s->regs[s->pointer & 0xffu] : s->regs[s->pointer]; s->pointer++; return v; }
static void s_receive(Slave *s, uint8_t byte) {
    if (s->eeprom) s->eeprom_bytes_written++;
    else if (s->reg8) {
        if (s->received == 0) s->pointer = byte;
        else { s->regs[s->pointer++] = byte; s->data_writes++; }
    } else if (s->received == 0) s->pointer = (uint16_t)(byte << 8);
    else if (s->received == 1) s->pointer = (uint16_t)(s->pointer | byte);
    else {
        uint16_t reg = s->pointer++;
        s->regs[reg] = byte;
        s->data_writes++;
        if (reg == 0xf010u) {
            s->write_f010_count++;
            if (s->wake_24618_on_f010 && byte == 0x00u) {
                s->regs[0xf008u] = 0x00u; s->regs[0xf009u] = 0x06u;
                s->regs[0xf00au] = 0xceu; s->regs[0xf00bu] = 0xbfu;
            }
        } else if (reg == 0xf020u) s->write_f020_count++;
        else if (reg == 0x6020u) s->write_6020_count++;
    }
    s->received++;
}
static void s_start(Slave *s) { s->state = S_ADDR; s->bits = 0; s->shift = 0; s->sda_low = 0; s->received = 0; }
static void s_stop(Slave *s) { s->state = S_IDLE; s->sda_low = 0; }
static void s_rise(Slave *s, int sda) {
    if (s->state == S_ADDR || s->state == S_RX) { s->shift = (uint8_t)(s->shift << 1 | sda); s->bits++; }
    else if (s->state == S_TXACK) s->master_ack = !sda;
}
static void s_fall(Slave *s) {
    switch (s->state) {
    case S_ADDR:
        if (s->bits == 8) {
            if ((s->shift >> 1) == s->address) { s->rw = s->shift & 1; s->sda_low = 1; s->state = S_AACK; s->acked_addresses++; }
            else s->state = S_IGNORE;
        }
        break;
    case S_AACK:
        s->sda_low = 0;
        if (s->rw) { s->out = s_read(s); s->bitpos = 7; s->sda_low = !((s->out >> 7) & 1); s->state = S_TX; }
        else { s->state = S_RX; s->bits = 0; s->shift = 0; }
        break;
    case S_RX:
        if (s->bits == 8) { s_receive(s, s->shift); s->sda_low = 1; s->state = S_RACK; }
        break;
    case S_RACK: s->sda_low = 0; s->state = S_RX; s->bits = 0; s->shift = 0; break;
    case S_TX:
        if (s->bitpos > 0) { s->bitpos--; s->sda_low = !((s->out >> s->bitpos) & 1); }
        else { s->sda_low = 0; s->state = S_TXACK; }
        break;
    case S_TXACK:
        if (s->master_ack) { s->out = s_read(s); s->bitpos = 7; s->sda_low = !((s->out >> 7) & 1); s->state = S_TX; }
        else { s->state = S_IGNORE; s->sda_low = 0; }
        break;
    default: break;
    }
}
static int sda_now(const Bus *b) {
    if (b->master_sda_low) return 0;
    for (unsigned i = 0; i < b->nslaves; ++i) if (b->slaves[i]->sda_low) return 0;
    return 1;
}
static void update(Bus *b) {
    const int scl = !(b->master_scl_low || b->stretching);
    int sda = sda_now(b);
    if (scl != b->scl) {
        b->scl = scl;
        for (unsigned i = 0; i < b->nslaves; ++i) { if (scl) s_rise(b->slaves[i], sda); else s_fall(b->slaves[i]); }
        b->sda = sda_now(b); /* 時脈低時從端改資料線不算事件 */
        return;
    }
    if (sda != b->sda) {
        if (b->scl) for (unsigned i = 0; i < b->nslaves; ++i) { if (!sda) s_start(b->slaves[i]); else s_stop(b->slaves[i]); }
        b->sda = sda;
    }
}
static void op_line(void *context, unsigned pin, int high) {
    Bus *b = context;
    if (pin == PIN_SCL) {
        const int was_low = b->master_scl_low;
        b->master_scl_low = !high;
        if (high && was_low && b->stretch_each) b->stretching = b->stretch_each;
    } else {
        CHECK(pin == PIN_SDA);
        b->master_sda_low = !high;
    }
    update(b);
}
static int op_level(void *context, unsigned pin) {
    Bus *b = context;
    if (pin == PIN_SCL) {
        if (b->stretching && !b->stretch_forever) b->stretching--;
        update(b);
        return b->scl;
    }
    return b->sda;
}
static void op_wait(void *context, unsigned us) { Bus *b = context; b->waits += us; }
static const struct fc_bus_ops ops = {op_line, op_level, op_wait};

static Slave sensor, eeprom;
static Bus bus;
static struct fc_bus engine;
static void reset_bus(unsigned nslaves) {
    memset(&bus, 0, sizeof bus);
    bus.sda = bus.scl = 1;
    bus.slaves[0] = &sensor;
    bus.slaves[1] = &eeprom;
    bus.nslaves = nslaves;
    sensor.state = eeprom.state = S_IDLE;
    sensor.sda_low = eeprom.sda_low = 0;
    engine = (struct fc_bus){&ops, &bus, PIN_SDA, PIN_SCL, 5, 200, 0, 0, 0};
}
static void make_slaves(uint8_t sensor_address, uint8_t eeprom_address) {
    memset(&sensor, 0, sizeof sensor);
    memset(&eeprom, 0, sizeof eeprom);
    sensor.address = sensor_address;
    eeprom.address = eeprom_address;
    eeprom.eeprom = 1;
    for (unsigned i = 0; i < 256; ++i) eeprom.regs[i] = (uint8_t)(0xa0 ^ i);
    sensor.regs[0x300a] = 0x56; sensor.regs[0x300b] = 0x40; sensor.regs[0x300c] = 0x41;
    sensor.regs[0x0016] = 0x08; sensor.regs[0x0017] = 0x58;
    sensor.regs[0x0000] = 0x0d; sensor.regs[0x0001] = 0x41;
}

static void encodings(void) {
    CHECK(fc_pin_ctl(FC_CTL_EGPIO_PRESENT, 1, 4, FC_CTL_PULL_NONE, 0) == 0x1844u); /* MCLK4：func 1、4 mA、APPS 接手 eGPIO */
    CHECK(fc_pin_ctl(0, 0, 2, FC_CTL_PULL_UP, 1) == 0x203u);
    CHECK(fc_pin_ctl(0x1fffu & ~FC_CTL_EGPIO_PRESENT, 0, 2, FC_CTL_PULL_NONE, 1) == 0x200u); /* 其他舊位元不帶過來 */
    CHECK(fc_pin_ctl(FC_CTL_EGPIO_PRESENT | FC_CTL_EGPIO_ENABLE, 0, 2, FC_CTL_PULL_UP, 0) == 0x1803u);
    CHECK(fc_drive_bits(1) == 0 && fc_drive_bits(2) == 0 && fc_drive_bits(16) == 7u << 6 && fc_drive_bits(40) == 7u << 6);
    for (unsigned ma = 2; ma <= 16; ma += 2) CHECK(fc_ctl_drive_ma(fc_drive_bits(ma)) == ma);
    CHECK(fc_ctl_func(0x1844u) == 1 && fc_ctl_pull(0x203u) == 3);
    /* RCG：來源 0、分頻 1、M/N 旁路；其他位元（如 HW_CLK_CTRL bit 20）照舊 */
    CHECK(fc_rcg_cfg_xo(1u << 20 | 0x2305u) == (1u << 20 | 1u));
    CHECK(fc_rcg_src(fc_rcg_cfg_xo(0x5ffu)) == 0 && fc_rcg_hid(fc_rcg_cfg_xo(0x5ffu)) == 1);
    CHECK(fc_rcg_cfg(1u << 20 | 0x371fu, 2u, 14u) == (1u << 20 | 2u << 8 | 14u));
    CHECK(fc_rcg_src(fc_rcg_cfg(0u, 1u, 5u)) == 1u && fc_rcg_hid(fc_rcg_cfg(0u, 1u, 5u)) == 5u);
    CHECK(fc_rcg_src(fc_rcg_cfg(0u, 2u, 3u)) == 2u && fc_rcg_hid(fc_rcg_cfg(0u, 2u, 3u)) == 3u);
    /* PLL0（Lucid OLE）與 CAMSS 版本解碼 */
    CHECK(FC_PLL0_L_VAL == 0x4444003eu && FC_PLL0_ALPHA == 0x8000u && FC_PLL0_USER_CTL == 0x00008400u);
    CHECK(!fc_pll_is_enabled(0u, 0u) && !fc_pll_is_enabled(FC_PLL_OUTCTRL, FC_PLL_OP_STANDBY));
    CHECK(!fc_pll_is_enabled(0u, FC_PLL_OP_RUN) && fc_pll_is_enabled(FC_PLL_OUTCTRL | FC_PLL_LOCK_DET, FC_PLL_OP_RUN));
    CHECK(fc_camss_hw_gen(0x60800010u) == 6u && fc_camss_hw_rev(0x60800010u) == 0x80u && fc_camss_hw_step(0x60800010u) == 0x10u);
    CHECK(fc_csiphy_pack_ver(0x10u, 0x02u, 0x00u, 0x20u) == 0x20000210u);
    CHECK(fc_csiphy_pack_ver(0x1ffu, 0x2aau, 0x355u, 0x401u) == 0x0155aaffu);
    /* GDSC（POLL_CFG_GDSCR） */
    CHECK(fc_gdsc_is_on(0, FC_GDSC_CFG_UP_DONE) && !fc_gdsc_is_on(FC_GDSC_SW_COLLAPSE, FC_GDSC_CFG_UP_DONE));
    CHECK(!fc_gdsc_is_on(FC_GDSC_PWR_ON, 0)); /* 只看 CFG_GDSCR */
    /* branch：hwcg 只看 enable（Linux clk_branch_wait），否則看 CLK_OFF；實機值 0x80000002／0x80000003。 */
    CHECK(!fc_branch_is_on(0x80000002u) && fc_branch_is_on(0x80000003u));
    CHECK(!fc_branch_is_on(0x80000000u) && !fc_branch_is_on(0x80000001u) && fc_branch_is_on(0x00000001u));
    CHECK(fc_branch_is_on(0x00000003u) && !fc_branch_is_on(0x00000002u));
    CHECK(fc_gdsc_is_off(FC_GDSC_SW_COLLAPSE, FC_GDSC_CFG_DOWN_DONE) && !fc_gdsc_is_off(0, FC_GDSC_CFG_DOWN_DONE));
    /* VRM */
    CHECK(fc_is_vrm(0x40a00u) && fc_is_vrm(0x44000u) && !fc_is_vrm(0x30010u) && !fc_is_vrm(0x50000u) && !fc_is_vrm(0x40a04u) &&
          !fc_is_vrm(0));
    uint32_t a[3], v[3];
    CHECK(fc_vrm_on(0x40100u, 1104, a, v) == 3);
    CHECK(a[0] == 0x40100u && v[0] == 1104 && a[1] == 0x40108u && v[1] == 7 && a[2] == 0x40104u && v[2] == 1);
    CHECK(fc_vrm_off(0x40100u, a, v) == 1 && a[0] == 0x40104u && v[0] == 0);
    /* 探測規則 */
    CHECK(fc_probe_by_read(0x30) && fc_probe_by_read(0x36) && fc_probe_by_read(0x50) && fc_probe_by_read(0x5f));
    CHECK(!fc_probe_by_read(0x2f) && !fc_probe_by_read(0x38) && !fc_probe_by_read(0x10) && !fc_probe_by_read(0x60));
    CHECK(!fc_may_write(0x50) && !fc_may_write(0x57) && !fc_may_write(0x5f) && fc_may_write(0x4f) && fc_may_write(0x36) &&
          fc_may_write(0x60));
    CHECK(FC_ID_PROBES == 4 && fc_id_probes[2].reg == 0x300a && fc_id_probes[2].bytes == 3);
}

static void transfers(void) {
    uint8_t in[16];
    make_slaves(0x36, 0x50);
    reset_bus(2);
    CHECK(fc_i2c_probe(&engine, 0x36) == 1 && fc_i2c_probe(&engine, 0x37) == 0 && fc_i2c_probe(&engine, 0x50) == 1);
    CHECK(fc_i2c_probe(&engine, 0x10) == 0 && fc_i2c_probe(&engine, 0x51) == 0);
    CHECK(eeprom.eeprom_bytes_written == 0 && sensor.data_writes == 0);
    /* OV 式 16 位元暫存器讀，3 個位元組（中間兩個 ACK、最後 NACK） */
    memset(in, 0, sizeof in);
    CHECK(fc_i2c_read16(&engine, 0x36, 0x300a, in, 3) == FC_I2C_OK);
    CHECK(in[0] == 0x56 && in[1] == 0x40 && in[2] == 0x41 && sensor.pointer == 0x300d && sensor.data_writes == 0);
    CHECK(fc_i2c_read16(&engine, 0x36, 0x0016, in, 2) == FC_I2C_OK && in[0] == 0x08 && in[1] == 0x58);
    CHECK(fc_i2c_read16(&engine, 0x37, 0x0016, in, 2) == FC_I2C_NACK_ADDR);
    /* EEPROM：寫入一律拒絕、目前位址讀 */
    CHECK(fc_i2c_read16(&engine, 0x50, 0x0000, in, 2) == FC_I2C_REFUSED);
    CHECK(fc_i2c_transfer(&engine, 0x50, in, 1, NULL, 0) == FC_I2C_REFUSED);
    CHECK(fc_i2c_transfer(&engine, 0x80, NULL, 0, in, 1) == FC_I2C_REFUSED);
    eeprom.pointer = 0;
    CHECK(fc_i2c_transfer(&engine, 0x50, NULL, 0, in, 16) == FC_I2C_OK);
    for (unsigned i = 0; i < 16; ++i) CHECK(in[i] == (uint8_t)(0xa0 ^ i));
    CHECK(eeprom.eeprom_bytes_written == 0);
    /* 寫入交易本身（感測器） */
    const uint8_t w[3] = {0x01, 0x00, 0x5a};
    CHECK(fc_i2c_transfer(&engine, 0x36, w, 3, NULL, 0) == FC_I2C_OK && sensor.regs[0x0100] == 0x5a && sensor.data_writes == 1);
    /* OV32C preInit 類 16 位元暫存器單一位元組寫入：感測器成功、EEPROM 一律拒絕 */
    CHECK(fc_i2c_write16_u8(&engine, 0x50, 0x1000, 0x00) == FC_I2C_REFUSED && eeprom.eeprom_bytes_written == 0);
    CHECK(fc_i2c_write16_u8(&engine, 0x36, 0x1000, 0x00) == FC_I2C_OK && sensor.regs[0x1000] == 0x00);
    CHECK(fc_i2c_write16_u8(&engine, 0x36, 0x1001, 0x04) == FC_I2C_OK && sensor.regs[0x1001] == 0x04 && sensor.data_writes == 3);
    CHECK(bus.sda == 1 && bus.scl == 1 && sensor.state == S_IDLE && eeprom.state == S_IDLE);
    CHECK(engine.stuck == 0 && engine.recovered == 0);
}

static void stretching(void) {
    uint8_t in[3];
    make_slaves(0x36, 0x50);
    reset_bus(2);
    bus.stretch_each = 3;
    CHECK(fc_i2c_read16(&engine, 0x36, 0x300a, in, 3) == FC_I2C_OK && in[0] == 0x56 && in[2] == 0x41);
    CHECK(engine.stretched > 0 && engine.stuck == 0);
    reset_bus(2);
    bus.stretch_each = 1;
    bus.stretch_forever = 1;
    CHECK(fc_i2c_read16(&engine, 0x36, 0x300a, in, 3) == FC_I2C_STUCK && engine.stuck > 0);
    CHECK(sensor.data_writes == 0);
}

static void wedged(void) {
    uint8_t in[3];
    for (int bitpos = 0; bitpos <= 7; ++bitpos) {
        make_slaves(0x36, 0x50);
        reset_bus(2);
        /* 感測器停在送出位元組的中間，資料線拉著 0 */
        sensor.state = S_TX;
        sensor.out = 0x00;
        sensor.bitpos = bitpos;
        sensor.sda_low = 1;
        bus.sda = 0;
        CHECK(fc_i2c_read16(&engine, 0x36, 0x300a, in, 3) == FC_I2C_OK && in[1] == 0x40);
        CHECK(engine.recovered == 1);
    }
    /* 外力一直拉住資料線：不閒置、不送任何東西 */
    make_slaves(0x36, 0x50);
    reset_bus(2);
    sensor.state = S_IGNORE;
    sensor.sda_low = 1;
    bus.sda = 0;
    const unsigned before = sensor.acked_addresses;
    CHECK(fc_i2c_read16(&engine, 0x36, 0x300a, in, 3) == FC_I2C_BUSY && sensor.acked_addresses == before);
    CHECK(fc_i2c_probe(&engine, 0x36) == FC_I2C_BUSY);
}

/* 照核心的掃描流程：探測全部位址，ACK 的位址依規則讀 ID 或做目前位址讀。 */
static unsigned scan_like_kernel(uint8_t *acks) {
    unsigned count = 0;
    for (unsigned a = FC_I2C_FIRST; a <= FC_I2C_LAST; ++a) if (fc_i2c_probe(&engine, a) == 1 && count < 8) acks[count++] = (uint8_t)a;
    for (unsigned i = 0; i < count; ++i) {
        uint8_t bytes[16];
        if (!fc_may_write(acks[i])) { CHECK(fc_i2c_transfer(&engine, acks[i], NULL, 0, bytes, 16) == FC_I2C_OK); continue; }
        for (unsigned p = 0; p < FC_ID_PROBES; ++p)
            CHECK(fc_i2c_read16(&engine, acks[i], fc_id_probes[p].reg, bytes, fc_id_probes[p].bytes) == FC_I2C_OK);
    }
    return count;
}
static uint32_t seed = 0x13579bdfu;
static uint32_t next(void) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }

static void fuzz(void) {
    unsigned scans = 0, recovered = 0;
    for (unsigned round = 0; round < 1500; ++round) {
        uint8_t s = (uint8_t)(FC_I2C_FIRST + next() % (FC_I2C_LAST - FC_I2C_FIRST + 1));
        uint8_t e = (uint8_t)(0x50 + next() % 16);
        if (s == e) s = 0x36;
        make_slaves(s, e);
        if (!fc_may_write(s)) sensor.eeprom = 1; /* EEPROM 範圍的「感測器」也當 EEPROM 看待 */
        reset_bus(1 + next() % 2);
        bus.stretch_each = next() % 4;
        if (next() % 3 == 0) {
            sensor.state = S_TX;
            sensor.out = (uint8_t)next();
            sensor.bitpos = (int)(next() % 8);
            sensor.sda_low = !((sensor.out >> sensor.bitpos) & 1);
            bus.sda = sda_now(&bus);
        }
        uint8_t acks[8];
        const unsigned count = scan_like_kernel(acks);
        unsigned seen_sensor = 0, seen_eeprom = 0;
        for (unsigned i = 0; i < count; ++i) { seen_sensor += acks[i] == s; seen_eeprom += acks[i] == e; }
        CHECK(seen_sensor == 1 && seen_eeprom == (unsigned)(bus.nslaves == 2) && count == bus.nslaves);
        CHECK(eeprom.eeprom_bytes_written == 0 && sensor.eeprom_bytes_written == 0);
        CHECK(sensor.data_writes == 0 && eeprom.data_writes == 0);
        CHECK(engine.stuck == 0 && bus.sda == 1 && bus.scl == 1);
        recovered += engine.recovered;
        scans++;
    }
    printf("fuzz: %u scans, %u wedged buses recovered\n", scans, recovered);
    CHECK(recovered > 100);
}

static void c2b_pipeline(void) {
    CHECK(fc_is_bcm(0x50004u) && !fc_is_bcm(0x40000u) && !fc_is_bcm(0u));
    CHECK(fc_bcm_vote(1, 0x3fffu) == 0x60003fffu);
    CHECK(fc_bcm_vote(0, 0u) == 0x40000000u);
    CHECK(fc_tpg_hbi(256u, 8u, 4u) == 349u);
    CHECK(fc_tpg_hbi(256u, 8u, 0u) == 0u);
    CHECK(fc_tpg_vbi(128u) == 12u);
    CHECK(fc_tpg_dt_cfg0(256u, 128u) == 0x01000080u);
    CHECK(fc_tpg_dt_cfg2(FC_TPG_VER_2_0_0, FC_TPG_MODE_COLOR_BARS, FC_TPG_ENC_RAW8, FC_TPG_USER_PAYLOAD) == 0x00100be8u);
    CHECK(fc_tpg_dt_cfg2(FC_TPG_VER_2_1_0, FC_TPG_MODE_COLOR_BARS, FC_TPG_ENC_RAW8, FC_TPG_USER_PAYLOAD) == 0x10000be8u);
    CHECK(fc_tpg_ctrl(1, 4u) == 0x31u && fc_tpg_ctrl(0, 4u) == 0u);
    CHECK(fc_csid_rx_cfg0_tpg(4u, 0u) == 0x18032103u);
    CHECK(fc_csid_rdi_cfg0(FC_MIPI_DT_RAW8, 0u, 0u, 0) == 0x002af140u);
    CHECK(fc_csid_rdi_cfg0(FC_MIPI_DT_RAW8, 0u, 0u, 1) == 0x802af140u);
    CHECK(fc_csid_rdi_cfg1() == 0x85e0u);
    CHECK(FC_CSID_RDI0_SUBSAMPLE_PATTERN == 0x548u && FC_CSID_RDI0_SUBSAMPLE_PERIOD == 0x54cu);
    CHECK(fc_vfe_wm_addr_valid(0x8c0000000ull) && fc_vfe_wm_addr(0x8c0000000ull) == 0x08c00000u);
    CHECK(!fc_vfe_wm_addr_valid(0ull) && !fc_vfe_wm_addr_valid(0x8c0000080ull) && !fc_vfe_wm_addr_valid(0x10000000000ull));
    CHECK(fc_vfe_wm_frame_incr(256u, 128u) == 0x80u);
    CHECK(fc_vfe_wm_frame_incr(250u, 128u) == 0x80u);

    uint32_t smrs[8], s2crs[8];
    for (unsigned idx = 0; idx < 8u; ++idx) { smrs[idx] = 0u; s2crs[idx] = FC_SMMU_FAULT_S2CR; }
    smrs[0] = (1u << 31) | 0x40u; s2crs[0] = FC_SMMU_BYPASS_BANK;
    smrs[7] = (1u << 31) | 0x1401u; s2crs[7] = 1u;
    CHECK(fc_smmu_pick_high_slot(smrs, s2crs, 8u, FC_SMMU_IFE_SID, FC_SMMU_IFE_MASK) == 6);
    smrs[2] = (1u << 31) | 0x820u; s2crs[2] = 0u;
    CHECK(fc_smmu_overlap(smrs[2], FC_SMMU_IFE_SID, FC_SMMU_IFE_MASK));
    CHECK(fc_smmu_pick_high_slot(smrs, s2crs, 8u, FC_SMMU_IFE_SID, FC_SMMU_IFE_MASK) == -2);
    smrs[2] = 0u; s2crs[2] = 0u;
    for (unsigned idx = 0; idx < 8u; ++idx) s2crs[idx] = 0u;
    CHECK(fc_smmu_pick_high_slot(smrs, s2crs, 8u, FC_SMMU_IFE_SID, FC_SMMU_IFE_MASK) == -1);

    uint8_t frame[16];
    memset(frame, 0x5a, sizeof(frame));
    struct fc_frame_stats stats;
    fc_frame_analyze(frame, (uint32_t)sizeof(frame), 0x5au, &stats);
    CHECK(stats.non_canary == 0u && stats.min_val == 0x5au && stats.max_val == 0x5au && stats.transitions == 0u);
    frame[0] = 0x10u; frame[1] = 0x20u; frame[2] = 0xf0u;
    fc_frame_analyze(frame, (uint32_t)sizeof(frame), 0x5au, &stats);
    CHECK(stats.non_canary == 3u && stats.min_val == 0x10u && stats.max_val == 0xf0u && stats.transitions == 3u);
}

static void c3_sensor_and_table(void) {
    CHECK(FC_MIPI_DT_RAW10 == 0x2bu);
    CHECK(fc_raw10_bytes_per_line(1280u) == 1600u);
    CHECK(fc_vfe_wm_frame_incr(1600u, 720u) == 0x1194u);
    CHECK(fc_csid_rx_cfg0_phy(4u, 0x3210u, 4u, 0) == 0x00532103u);
    CHECK(fc_csid_rx_cfg0_phy(3u, 0x0210u, 2u, 1) == 0x01302102u);
    CHECK(fc_csiphy_ctrl5(4u) == 0xd5u && fc_csiphy_ctrl5(2u) == 0x85u);
    CHECK(fc_csiphy_ctrl5_cphy(3u) == 0x2au);
    CHECK(FC_CSIPHY_CTRL(0) == 0x1000u && FC_CSIPHY_CTRL(5) == 0x1014u && FC_CSIPHY_CTRL(6) == 0x1018u);
    CHECK(FC_CSIPHY_STATUS(12) == 0x10e0u);

    uint8_t buf[FC_TAB_HDR_BYTES + 2u * FC_TAB_ENTRY_BYTES];
    memset(buf, 0, sizeof(buf));
    memcpy(buf, "FUXICAM1", 8);
    buf[8] = 1u; buf[10] = 0x10u; buf[11] = 4u;
    buf[12] = (uint8_t)(1280u & 0xffu); buf[13] = (uint8_t)(1280u >> 8);
    buf[14] = (uint8_t)(720u & 0xffu); buf[15] = (uint8_t)(720u >> 8);
    buf[16] = FC_MIPI_DT_RAW10; buf[17] = 25u;
    buf[18] = 0x10u; buf[19] = 0x32u;
    buf[20] = 2u;
    /* entry 0: 0x0103=0x01 delay 5 ms; entry 1: 0x300a=0x56 delay 0 */
    buf[28] = 0x03u; buf[29] = 0x01u; buf[30] = 0x01u; buf[31] = 5u;
    buf[32] = 0x0au; buf[33] = 0x30u; buf[34] = 0x56u; buf[35] = 0u;
    const uint32_t crc = fc_crc32(buf + FC_TAB_HDR_BYTES, 8u);
    buf[24] = (uint8_t)crc; buf[25] = (uint8_t)(crc >> 8);
    buf[26] = (uint8_t)(crc >> 16); buf[27] = (uint8_t)(crc >> 24);

    struct fc_tab_header hdr;
    CHECK(fc_tab_parse(buf, (uint32_t)sizeof(buf), &hdr) == 0);
    CHECK(hdr.version == 1u && hdr.slave7 == 0x10u && hdr.lanes == 4u && hdr.width == 1280u && hdr.height == 720u);
    CHECK(hdr.dt == FC_MIPI_DT_RAW10 && hdr.settle == 25u && hdr.lane_assign == 0x3210u && hdr.count == 2u && hdr.crc32 == crc);
    struct fc_tab_entry e0, e1;
    fc_tab_entry_at(buf, 0u, &e0);
    fc_tab_entry_at(buf, 1u, &e1);
    CHECK(e0.reg == 0x0103u && e0.val == 0x01u && e0.delay_ms == 5u);
    CHECK(e1.reg == 0x300au && e1.val == 0x56u && e1.delay_ms == 0u);

    /* 損壞驗證與 EEPROM 位址拒絕 */
    buf[30] ^= 0x80u;
    CHECK(fc_tab_parse(buf, (uint32_t)sizeof(buf), &hdr) == -8);
    buf[30] ^= 0x80u;
    buf[10] = 0x50u;
    CHECK(fc_tab_parse(buf, (uint32_t)sizeof(buf), &hdr) == -4);
    buf[10] = 0x10u;
    CHECK(fc_tab_parse(buf, (uint32_t)sizeof(buf) - 1u, &hdr) == -7);

    const uint8_t fr1[6] = {1, 2, 3, 4, 5, 6};
    const uint8_t fr2[6] = {1, 9, 3, 8, 5, 6};
    CHECK(fc_frame_diff(fr1, fr2, 6u) == 2u && fc_frame_diff(fr1, fr1, 6u) == 0u);
}

static void c4_stream_and_exposure(void) {
    /* ABI 與 redwood_camera.h 二進位相容 */
    CHECK(sizeof(struct fuxi_camera_frame) == 120u);
    CHECK(sizeof(struct fuxi_camera_command) == 24u);
    CHECK(offsetof(struct fuxi_camera_frame, sequence) == 40u);
    CHECK(offsetof(struct fuxi_camera_frame, exposure_lines) == 56u);
    CHECK(offsetof(struct fuxi_camera_frame, reserved) == 100u);

    /* OV32C 類比增益階梯（0x100..0xff0；步距 2/4/8/16） */
    CHECK(fc_ov32c_quantize_gain(0u) == 0x0100u);
    CHECK(fc_ov32c_quantize_gain(0x0101u) == 0x0100u);
    CHECK(fc_ov32c_quantize_gain(0x01ffu) == 0x01feu);
    CHECK(fc_ov32c_quantize_gain(0x0203u) == 0x0200u);
    CHECK(fc_ov32c_quantize_gain(0x0205u) == 0x0204u);
    CHECK(fc_ov32c_quantize_gain(0x0407u) == 0x0400u);
    CHECK(fc_ov32c_quantize_gain(0x0409u) == 0x0408u);
    CHECK(fc_ov32c_quantize_gain(0x080fu) == 0x0800u);
    CHECK(fc_ov32c_quantize_gain(0x0815u) == 0x0810u);
    CHECK(fc_ov32c_quantize_gain(0x2000u) == 0x0ff0u);

    /* 曝光夾限與自動拉長 VTS */
    struct fc_ov32c_exposure exp;
    fc_ov32c_clamp_exposure(1u, 0x0105u, 0u, &exp);
    CHECK(exp.lines == FC_OV32C_MIN_LINES && exp.gain == 0x0104u && exp.frame_lines == FC_OV32C_VTS_MIN);
    fc_ov32c_clamp_exposure(2400u, 0x080fu, FC_OV32C_VTS_MIN, &exp);
    CHECK(exp.lines == 2400u && exp.gain == 0x0800u && exp.frame_lines == 2400u + FC_OV32C_MARGIN);
    fc_ov32c_clamp_exposure(999999u, 0x9999u, 999999u, &exp);
    CHECK(exp.lines == FC_OV32C_VTS_MAX - FC_OV32C_MARGIN && exp.gain == FC_OV32C_GAIN_MAX && exp.frame_lines == FC_OV32C_VTS_MAX);

    /* Group Hold 3 雙組 15 筆序列編碼與 I2C 模擬寫入 */
    struct fc_tab_entry seq[FC_OV32C_EXP_REGS];
    fc_ov32c_clamp_exposure(2400u, 0x0800u, FC_OV32C_VTS_30FPS, &exp);
    CHECK(fc_ov32c_encode_exposure(&exp, seq) == 15u);
    CHECK(seq[0].reg == 0x3208u && seq[0].val == 0x03u);
    CHECK(seq[1].reg == 0x380eu && seq[1].val == 0x0au && seq[2].reg == 0x380fu && seq[2].val == 0x36u);
    CHECK(seq[3].reg == 0x3500u && seq[3].val == 0x00u && seq[4].reg == 0x3501u && seq[4].val == 0x09u &&
          seq[5].reg == 0x3502u && seq[5].val == 0x60u);
    CHECK(seq[6].reg == 0x3540u && seq[6].val == 0x00u && seq[7].reg == 0x3541u && seq[7].val == 0x09u &&
          seq[8].reg == 0x3542u && seq[8].val == 0x60u);
    CHECK(seq[9].reg == 0x3508u && seq[9].val == 0x08u && seq[10].reg == 0x3509u && seq[10].val == 0x00u);
    CHECK(seq[11].reg == 0x3548u && seq[11].val == 0x08u && seq[12].reg == 0x3549u && seq[12].val == 0x00u);
    CHECK(seq[13].reg == 0x3208u && seq[13].val == 0x13u && seq[14].reg == 0x3208u && seq[14].val == 0xa3u);

    Slave ov;
    memset(&ov, 0, sizeof(ov));
    ov.address = FC_OV32C_SLAVE7;
    Bus wire;
    memset(&wire, 0, sizeof(wire));
    wire.sda = 1;
    wire.scl = 1;
    wire.slaves[0] = &ov;
    wire.nslaves = 1;
    static const struct fc_bus_ops kOps = {op_line, op_level, op_wait};
    struct fc_bus c4_bus = {&kOps, &wire, PIN_SDA, PIN_SCL, 5u, 200u, 0u, 0u, 0u};
    CHECK(fc_ov32c_apply_exposure(&c4_bus, FC_OV32C_SLAVE7, &exp) == FC_I2C_OK);
    CHECK(ov.data_writes == 15u);
    CHECK(ov.regs[0x3208u] == 0xa3u);
    CHECK(ov.regs[0x380eu] == 0x0au && ov.regs[0x380fu] == 0x36u);
    CHECK(ov.regs[0x3500u] == 0x00u && ov.regs[0x3501u] == 0x09u && ov.regs[0x3502u] == 0x60u);
    CHECK(ov.regs[0x3540u] == 0x00u && ov.regs[0x3541u] == 0x09u && ov.regs[0x3542u] == 0x60u);
    CHECK(ov.regs[0x3508u] == 0x08u && ov.regs[0x3509u] == 0x00u);
    CHECK(ov.regs[0x3548u] == 0x08u && ov.regs[0x3549u] == 0x00u);
    CHECK(fc_ov32c_apply_exposure(&c4_bus, 0x12u, &exp) == FC_I2C_NACK_ADDR);

    /* SMMU FSR 錯誤遮罩 */
    CHECK(!fc_smmu_fsr_fault(0u));
    CHECK(!fc_smmu_fsr_fault(1u));
    CHECK(!fc_smmu_fsr_fault(0x00000200u));
    CHECK(fc_smmu_fsr_fault(0x00000002u));
    CHECK(fc_smmu_fsr_fault(0x80000000u));

    /* RAW10 亮度取樣（4 像素 = 5 位元組，10-bit） */
    uint8_t raw[8 * 10];
    for (unsigned row = 0u; row < 8u; ++row) {
        for (unsigned g = 0u; g < 2u; ++g) {
            uint8_t *p = raw + row * 10u + g * 5u;
            const uint32_t v0 = 100u, v1 = 200u, v2 = 300u, v3 = 400u;
            p[0] = (uint8_t)(v0 >> 2);
            p[1] = (uint8_t)(v1 >> 2);
            p[2] = (uint8_t)(v2 >> 2);
            p[3] = (uint8_t)(v3 >> 2);
            p[4] = (uint8_t)((v0 & 3u) | ((v1 & 3u) << 2) | ((v2 & 3u) << 4) | ((v3 & 3u) << 6));
        }
    }
    uint32_t mn = 0u, mx = 0u;
    CHECK(fc_raw10_mean_sample(raw, 10u, 8u, 8u, &mn, &mx) == 250u && mn == 100u && mx == 400u);
    CHECK(fc_raw10_mean_sample(NULL, 10u, 8u, 8u, &mn, &mx) == 0u && mn == 0u && mx == 0u);
    CHECK(fc_raw10_mean_sample(raw, 4u, 8u, 8u, NULL, NULL) == 0u);

    /* 三槽環形緩衝挑選：優先 FREE、其次最舊 READY，絕不挑 WRITING/READING */
    struct fc_ring_slot ring[FC_RING_SLOTS];
    memset(ring, 0, sizeof(ring));
    ring[0].state = FC_SLOT_WRITING;
    ring[1].state = FC_SLOT_FREE;
    ring[2].state = FC_SLOT_READY; ring[2].sequence = 5u;
    CHECK(fc_ring_pick_next_write(ring) == 1);
    ring[1].state = FC_SLOT_READY; ring[1].sequence = 4u;
    CHECK(fc_ring_pick_next_write(ring) == 1);
    ring[1].state = FC_SLOT_READING;
    CHECK(fc_ring_pick_next_write(ring) == 2);
    ring[2].state = FC_SLOT_READING;
    CHECK(fc_ring_pick_next_write(ring) == -1);
    ring[1].state = FC_SLOT_READY; ring[1].sequence = 4u;
    ring[2].state = FC_SLOT_READY; ring[2].sequence = 9u;
    CHECK(fc_ring_pick_newest_read(ring, 0u) == 2);
    CHECK(fc_ring_pick_newest_read(ring, 8u) == 2);
    CHECK(fc_ring_pick_newest_read(ring, 9u) == -1);

    /* 模糊測試：隨機曝光、增益與環形狀態 */
    uint32_t rng = 0xc4f00du;
    for (unsigned i = 0u; i < 2000u; ++i) {
        rng = rng * 1664525u + 1013904223u;
        struct fc_ov32c_exposure fe;
        fc_ov32c_clamp_exposure(rng & 0x1ffffu, (rng >> 12) & 0x3fffu, (rng >> 16) & 0x1ffffu, &fe);
        CHECK(fe.lines >= FC_OV32C_MIN_LINES && fe.lines + FC_OV32C_MARGIN <= fe.frame_lines);
        CHECK(fe.frame_lines >= FC_OV32C_VTS_MIN && fe.frame_lines <= FC_OV32C_VTS_MAX);
        CHECK(fe.gain >= FC_OV32C_GAIN_UNITY && fe.gain <= FC_OV32C_GAIN_MAX);
        struct fc_tab_entry fseq[FC_OV32C_EXP_REGS];
        CHECK(fc_ov32c_encode_exposure(&fe, fseq) == 15u);

        /* 30 fps 夾限：req_vts=0 或小於 2614 時皆維持 >= FC_OV32C_VTS_30FPS */
        struct fc_ov32c_exposure fe30;
        fc_ov32c_clamp_exposure_30fps(rng & 0x1ffffu, (rng >> 12) & 0x3fffu, (rng >> 16) & 0x1ffffu, &fe30);
        CHECK(fe30.lines >= FC_OV32C_MIN_LINES && fe30.lines + FC_OV32C_MARGIN <= fe30.frame_lines);
        CHECK(fe30.frame_lines >= FC_OV32C_VTS_30FPS && fe30.frame_lines <= FC_OV32C_VTS_MAX);
        CHECK(fe30.gain >= FC_OV32C_GAIN_UNITY && fe30.gain <= FC_OV32C_GAIN_MAX);
    }
    struct fc_ov32c_exposure e30;
    fc_ov32c_clamp_exposure_30fps(500u, 0x0100u, 0u, &e30);
    CHECK(e30.frame_lines == FC_OV32C_VTS_30FPS && e30.lines == 500u && e30.gain == 0x0100u);
    fc_ov32c_clamp_exposure_30fps(500u, 0x0100u, FC_OV32C_VTS_MIN, &e30);
    CHECK(e30.frame_lines == FC_OV32C_VTS_30FPS);
    fc_ov32c_clamp_exposure_30fps(3000u, 0x0400u, 0u, &e30);
    CHECK(e30.frame_lines == 3000u + FC_OV32C_MARGIN && e30.lines == 3000u);

    CHECK(!fc_elapsed_exceeded(10000u, 10001u, 1500u));
    CHECK(!fc_elapsed_exceeded(0u, UINT64_MAX, 1500u));
    CHECK(!fc_elapsed_exceeded(10000u, 10000u, 1500u));
    CHECK(!fc_elapsed_exceeded(11500u, 10000u, 1500u));
    CHECK(fc_elapsed_exceeded(11501u, 10000u, 1500u));
}

static void put_le16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void verify_real_tab(const char *path, uint32_t exp_sid, uint16_t exp_ver, uint8_t exp_slave7,
    uint16_t exp_w, uint16_t exp_h, uint32_t exp_count, uint8_t exp_lanes, uint8_t exp_flags)
{
    FILE *fp = fopen(path, "rb");
    CHECK(fp != NULL);
    CHECK(fseek(fp, 0, SEEK_END) == 0);
    long sz = ftell(fp);
    CHECK(sz > 0 && sz < 1024 * 1024);
    rewind(fp);
    uint8_t *buf = malloc((size_t)sz);
    CHECK(buf != NULL);
    CHECK(fread(buf, 1, (size_t)sz, fp) == (size_t)sz);
    fclose(fp);

    struct fc_tab_header hdr;
    uint8_t sid = 99u;
    CHECK(fc_tab_parse_any(buf, (uint32_t)sz, &hdr, &sid) == 0);
    CHECK(hdr.version == exp_ver);
    CHECK(hdr.slave7 == exp_slave7);
    CHECK(hdr.width == exp_w && hdr.height == exp_h);
    CHECK(hdr.dt == FC_MIPI_DT_RAW10 && hdr.lanes == exp_lanes && hdr.flags == exp_flags && hdr.count == exp_count);
    if (exp_ver == 1u) CHECK(sid == 0u);
    else CHECK(sid == (uint8_t)exp_sid);

    for (uint32_t i = 0u; i < hdr.count; ++i) {
        struct fc_tab2_entry e;
        fc_tab_entry_any_at(buf, &hdr, i, &e);
        CHECK(e.data_type == 1u || e.data_type == 2u);
        CHECK(e.op == FC_OP_WRITE || e.op == FC_OP_POLL_EQ);
        if (e.data_type == 1u) CHECK(e.val <= 0xffu);
    }
    free(buf);
}

static void multi_sensor_and_tab2(const char *tabs_dir) {
    /* 1. 多鏡頭硬體描述符表：安全紅線與 16 KiB CSIPHY 偏移不變量 */
    CHECK(fc_sensor_hw_for(0u) == NULL && fc_sensor_hw_for(5u) == NULL);
    const uint32_t sids[4] = {FC_SENSOR_WIDE, FC_SENSOR_FRONT, FC_SENSOR_ULTRA, FC_SENSOR_TELE};
    for (unsigned i = 0u; i < 4u; ++i) {
        const struct fc_sensor_hw *hw = fc_sensor_hw_for(sids[i]);
        CHECK(hw != NULL && hw->sensor_id == sids[i]);
        CHECK(hw->name != NULL && hw->adt_tab_prop != NULL);
        CHECK((hw->phy_page & 0x3fffu) == 0ull);
        CHECK(hw->phy_off < 0x4000u);
        CHECK(hw->pin_mclk != hw->pin_reset && hw->pin_sda != hw->pin_scl);
        CHECK(hw->rail_count >= 3u && hw->rail_count <= FC_MAX_SENSOR_RAILS);
        if (sids[i] == FC_SENSOR_WIDE) {
            CHECK(hw->pin_avdd == 69u && hw->pin_avdd2 == 66u && hw->pin_mclk == 102u && hw->pin_reset == 118u);
            CHECK(hw->pin_sda == 112u && hw->pin_scl == 113u && hw->csiphy_idx == 2u && hw->phy_page == 0xace8000ull && hw->phy_off == 0u);
            CHECK(hw->is_cphy == 1u && hw->actuator_slave7 == FC_AK7314_SLAVE7 && hw->ois_slave7 == FC_OIS_SLAVE7);
            CHECK(hw->wake_aon_slave7 == 0u && hw->phytimer_pll_src == 2u && hw->phytimer_pll_hid == 2u);
        } else if (sids[i] == FC_SENSOR_FRONT) {
            CHECK(hw->pin_avdd == FC_GPIO_NONE && hw->pin_avdd2 == FC_GPIO_NONE && hw->csiphy_idx == 4u && hw->phy_off == 0u);
            CHECK(hw->is_cphy == 0u && hw->actuator_slave7 == 0u && hw->ois_slave7 == 0u);
            CHECK(hw->wake_aon_slave7 == 0x18u && hw->phytimer_pll_src == 3u && hw->phytimer_pll_hid == 1u);
        } else if (sids[i] == FC_SENSOR_ULTRA) {
            CHECK(hw->pin_avdd == 91u && hw->pin_avdd2 == FC_GPIO_NONE && hw->csiphy_idx == 0u && hw->phy_off == 0u);
            CHECK(hw->is_cphy == 0u && hw->actuator_slave7 == 0u && hw->ois_slave7 == 0u);
            CHECK(hw->wake_aon_slave7 == 0u && hw->phytimer_pll_src == 2u && hw->phytimer_pll_hid == 2u);
        } else {
            CHECK(hw->pin_avdd == 57u && hw->pin_avdd2 == FC_GPIO_NONE && hw->csiphy_idx == 1u);
            CHECK(hw->phy_page == 0xace4000ull && hw->phy_off == 0x2000u);
            CHECK(hw->is_cphy == 0u && hw->actuator_slave7 == FC_AK7314_SLAVE7 && hw->ois_slave7 == FC_OIS_SLAVE7);
            CHECK(hw->wake_aon_slave7 == 0u && hw->phytimer_pll_src == 2u && hw->phytimer_pll_hid == 2u);
        }
        for (unsigned r = 0u; r < hw->rail_count; ++r) {
            const struct fc_sensor_rail *sr = &hw->rails[r];
            CHECK(sr->name != NULL && sr->mv > 0u);
            CHECK(strstr(sr->name, "mmcx") == NULL && strcmp(sr->name, "cx") != 0 && strstr(sr->name, "cx.") == NULL);
            if (strncmp(sr->name, "bob", 3) == 0) CHECK(sr->mode == FC_VRM_MODE_BOB_AUTO);
            else CHECK(sr->mode == FC_VRM_MODE_HPM);
            const int is_shared = fc_rail_must_keep_on(sr->name);
            CHECK(sr->shared_keep_on == (uint8_t)is_shared);
        }
    }
    CHECK(fc_rail_must_keep_on("bobb1") && fc_rail_must_keep_on("bobb2") && fc_rail_must_keep_on("ldob14") &&
        fc_rail_must_keep_on("ldob16") && fc_rail_must_keep_on("ldoc2"));
    CHECK(!fc_rail_must_keep_on("ldom2") && !fc_rail_must_keep_on("ldom4") && !fc_rail_must_keep_on("ldom7") &&
        !fc_rail_must_keep_on("ldob11") && !fc_rail_must_keep_on(NULL));
    uint32_t va[3], vv[3];
    CHECK(fc_vrm_on_mode(0x40100u, 3600u, FC_VRM_MODE_BOB_AUTO, va, vv) == 3u);
    CHECK(vv[0] == 3600u && vv[1] == 6u && vv[2] == 1u);

    /* 2. 16-bit 資料 I2C 寫入、輪詢與 EEPROM 寫入防護 */
    Slave s5k, ee2;
    memset(&s5k, 0, sizeof(s5k));
    memset(&ee2, 0, sizeof(ee2));
    s5k.address = FC_S5K3K1_SLAVE7;
    ee2.address = 0x52u;
    ee2.eeprom = 1;
    Bus mwire;
    memset(&mwire, 0, sizeof(mwire));
    mwire.sda = 1;
    mwire.scl = 1;
    mwire.slaves[0] = &s5k;
    mwire.slaves[1] = &ee2;
    mwire.nslaves = 2;
    static const struct fc_bus_ops kMultiOps = {op_line, op_level, op_wait};
    struct fc_bus mbus = {&kMultiOps, &mwire, PIN_SDA, PIN_SCL, 5u, 200u, 0u, 0u, 0u};

    CHECK(fc_i2c_write16_u16(&mbus, FC_S5K3K1_SLAVE7, 0x0100u, 0x0123u) == FC_I2C_OK);
    CHECK(s5k.regs[0x0100u] == 0x01u && s5k.regs[0x0101u] == 0x23u);
    CHECK(fc_i2c_write16_u16(&mbus, 0x52u, 0x0010u, 0x1234u) == FC_I2C_REFUSED);
    CHECK(ee2.eeprom_bytes_written == 0u);

    s5k.regs[0x0005u] = 0xffu;
    CHECK(fc_i2c_poll16_u8(&mbus, FC_S5K3K1_SLAVE7, 0x0005u, 0xffu, 5u, 1000u) == FC_I2C_OK);
    s5k.regs[0x0005u] = 0x11u;
    const unsigned waits0 = mwire.waits;
    CHECK(fc_i2c_poll16_u8(&mbus, FC_S5K3K1_SLAVE7, 0x0005u, 0xffu, 3u, 0u) == FC_I2C_TIMEOUT);
    const unsigned i2c_only_waits = mwire.waits - waits0;
    const unsigned waits1 = mwire.waits;
    CHECK(fc_i2c_poll16_u8(&mbus, FC_S5K3K1_SLAVE7, 0x0005u, 0xffu, 3u, 1000u) == FC_I2C_TIMEOUT);
    CHECK((mwire.waits - waits1) == i2c_only_waits + 2000u);
    CHECK(fc_i2c_poll16_u8(&mbus, 0x29u, 0x0005u, 0xffu, 2u, 500u) == FC_I2C_NACK_ADDR);

    /* 3. FUXICAM2 解析與負測試 */
    uint8_t tab2[FC_TAB2_HDR_BYTES + 3u * FC_TAB2_ENTRY_BYTES];
    memset(tab2, 0, sizeof(tab2));
    memcpy(tab2, "FUXICAM2", 8u);
    put_le16(tab2 + 8u, 2u);
    tab2[10] = FC_S5K3K1_SLAVE7;
    tab2[11] = 4u;
    put_le16(tab2 + 12u, 3264u);
    put_le16(tab2 + 14u, 2448u);
    tab2[16] = FC_MIPI_DT_RAW10;
    tab2[17] = 26u;
    put_le16(tab2 + 18u, 0x3210u);
    put_le32(tab2 + 20u, 3u);
    tab2[28] = FC_SENSOR_TELE;
    uint8_t *eptr = tab2 + FC_TAB2_HDR_BYTES;
    put_le16(eptr + 0u, 0x6010u); put_le16(eptr + 2u, 0x0001u); put_le16(eptr + 4u, 5u); eptr[6] = 2u; eptr[7] = FC_OP_WRITE;
    put_le16(eptr + 8u, 0x0bccu); put_le16(eptr + 10u, 0x0001u); put_le16(eptr + 12u, 0u); eptr[14] = 1u; eptr[15] = FC_OP_WRITE;
    put_le16(eptr + 16u, 0x0005u); put_le16(eptr + 18u, 0x00ffu); put_le16(eptr + 20u, 30u); eptr[22] = 1u; eptr[23] = FC_OP_POLL_EQ;
    put_le32(tab2 + 24u, fc_crc32(eptr, 3u * FC_TAB2_ENTRY_BYTES));

    struct fc_tab_header hdr2;
    uint8_t sid2 = 0u;
    CHECK(fc_tab_parse_any(tab2, sizeof(tab2), &hdr2, &sid2) == 0);
    CHECK(hdr2.version == 2u && sid2 == FC_SENSOR_TELE && hdr2.count == 3u && hdr2.width == 3264u && hdr2.height == 2448u);
    struct fc_tab2_entry out_e;
    fc_tab_entry_any_at(tab2, &hdr2, 0u, &out_e);
    CHECK(out_e.reg == 0x6010u && out_e.val == 0x0001u && out_e.delay_ms == 5u && out_e.data_type == 2u && out_e.op == FC_OP_WRITE);
    fc_tab_entry_any_at(tab2, &hdr2, 2u, &out_e);
    CHECK(out_e.reg == 0x0005u && out_e.val == 0x00ffu && out_e.delay_ms == 30u && out_e.data_type == 1u && out_e.op == FC_OP_POLL_EQ);

    tab2[sizeof(tab2) - 1u] ^= 1u;
    CHECK(fc_tab_parse_any(tab2, sizeof(tab2), &hdr2, &sid2) == -8);
    tab2[sizeof(tab2) - 1u] ^= 1u;
    CHECK(fc_tab_parse_any(tab2, sizeof(tab2) - 1u, &hdr2, &sid2) == -7);
    tab2[10] = 0x50u;
    CHECK(fc_tab_parse_any(tab2, sizeof(tab2), &hdr2, &sid2) == -4);
    tab2[10] = FC_S5K3K1_SLAVE7;
    put_le16(tab2 + 8u, 3u);
    CHECK(fc_tab_parse_any(tab2, sizeof(tab2), &hdr2, &sid2) == -3);
    tab2[7] = '9';
    CHECK(fc_tab_parse_any(tab2, sizeof(tab2), &hdr2, &sid2) == -2);

    /* 4. OV13B10 與 S5K3K1 曝光量化／寫入與串流控制 */
    CHECK(fc_ov13b10_quantize_gain(0x0010u) == 0x0100u && fc_ov13b10_quantize_gain(0x0115u) == 0x0110u);
    CHECK(fc_ov13b10_quantize_gain(0x0225u) == 0x0220u && fc_ov13b10_quantize_gain(0x0455u) == 0x0440u);
    CHECK(fc_ov13b10_quantize_gain(0x0895u) == 0x0880u && fc_ov13b10_quantize_gain(0x1200u) == 0x0f80u);
    CHECK(fc_s5k3k1_quantize_gain(10u) == 32u && fc_s5k3k1_quantize_gain(77u) == 77u && fc_s5k3k1_quantize_gain(999u) == 512u);

    Slave ov13;
    memset(&ov13, 0, sizeof(ov13));
    ov13.address = FC_OV13B10_SLAVE7;
    mwire.slaves[0] = &ov13;
    mwire.nslaves = 1;
    struct fc_ov32c_exposure e13;
    CHECK(fc_sensor_clamp_exposure_30fps(FC_SENSOR_ULTRA, 2000u, 0x0455u, 0u, &e13) == 0);
    CHECK(e13.lines == 2000u && e13.gain == 0x0440u && e13.frame_lines == FC_OV13B10_VTS_30FPS);
    CHECK(fc_sensor_apply_exposure(FC_SENSOR_ULTRA, &mbus, FC_OV13B10_SLAVE7, &e13) == FC_I2C_OK);
    CHECK(ov13.regs[0x3208u] == 0xa0u && ov13.regs[0x380eu] == 0x0cu && ov13.regs[0x380fu] == 0x67u);
    CHECK(ov13.regs[0x3501u] == 0x07u && ov13.regs[0x3502u] == 0xd0u && ov13.regs[0x3508u] == 0x04u && ov13.regs[0x3509u] == 0x40u);
    CHECK(fc_sensor_stream_on(FC_SENSOR_ULTRA, &mbus, FC_OV13B10_SLAVE7) == FC_I2C_OK && ov13.regs[0x0100u] == 0x01u);
    CHECK(fc_sensor_stream_off(FC_SENSOR_ULTRA, &mbus, FC_OV13B10_SLAVE7) == FC_I2C_OK && ov13.regs[0x0100u] == 0x00u);

    mwire.slaves[0] = &s5k;
    struct fc_ov32c_exposure e3k;
    CHECK(fc_sensor_clamp_exposure_30fps(FC_SENSOR_TELE, 3000u, 96u, 0u, &e3k) == 0);
    CHECK(e3k.lines == 3000u && e3k.gain == 96u && e3k.frame_lines == FC_S5K3K1_VTS_30FPS);
    CHECK(fc_sensor_apply_exposure(FC_SENSOR_TELE, &mbus, FC_S5K3K1_SLAVE7, &e3k) == FC_I2C_OK);
    CHECK(s5k.regs[0x0104u] == 0x00u && s5k.regs[0x0340u] == 0x11u && s5k.regs[0x0341u] == 0x94u);
    CHECK(s5k.regs[0x0202u] == 0x0bu && s5k.regs[0x0203u] == 0xb8u && s5k.regs[0x0204u] == 0x00u && s5k.regs[0x0205u] == 0x60u);
    CHECK(fc_sensor_stream_on(FC_SENSOR_TELE, &mbus, FC_S5K3K1_SLAVE7) == FC_I2C_OK);
    CHECK(s5k.regs[0x0100u] == 0x01u && s5k.regs[0x0101u] == 0x00u && s5k.regs[0x6028u] == 0x40u && s5k.regs[0x6029u] == 0x00u);
    s5k.regs[0x0005u] = 0xffu;
    CHECK(fc_sensor_stream_off(FC_SENSOR_TELE, &mbus, FC_S5K3K1_SLAVE7) == FC_I2C_OK && s5k.regs[0x0100u] == 0x00u);
    s5k.regs[0x0005u] = 0x00u;
    CHECK(fc_sensor_stream_off(FC_SENSOR_TELE, &mbus, FC_S5K3K1_SLAVE7) == FC_I2C_TIMEOUT);
    CHECK(fc_sensor_clamp_exposure_30fps(99u, 100u, 100u, 0u, &e3k) == -1);
    CHECK(fc_sensor_stream_on(99u, &mbus, FC_S5K3K1_SLAVE7) == FC_I2C_REFUSED);

    uint32_t rng = 0x2d36u;
    for (unsigned i = 0u; i < 1000u; ++i) {
        rng = rng * 1664525u + 1013904223u;
        struct fc_ov32c_exposure fe;
        CHECK(fc_sensor_clamp_exposure_30fps(FC_SENSOR_ULTRA, rng & 0x1ffffu, (rng >> 8) & 0x3fffu, (rng >> 16) & 0x1ffffu, &fe) == 0);
        CHECK(fe.lines >= FC_OV13B10_MIN_LINES && fe.lines + FC_OV13B10_MARGIN <= fe.frame_lines);
        CHECK(fe.frame_lines >= FC_OV13B10_VTS_30FPS && fe.frame_lines <= FC_OV13B10_VTS_MAX);
        CHECK(fe.gain >= FC_OV13B10_GAIN_UNITY && fe.gain <= FC_OV13B10_GAIN_MAX);
        CHECK(fc_sensor_clamp_exposure_30fps(FC_SENSOR_TELE, rng & 0x1ffffu, (rng >> 8) & 0xfffu, (rng >> 16) & 0x1ffffu, &fe) == 0);
        CHECK(fe.lines >= FC_S5K3K1_MIN_LINES && fe.lines + FC_S5K3K1_MARGIN <= fe.frame_lines);
        CHECK(fe.frame_lines >= FC_S5K3K1_VTS_30FPS && fe.frame_lines <= FC_S5K3K1_VTS_MAX);
        CHECK(fe.gain >= FC_S5K3K1_GAIN_UNITY && fe.gain <= FC_S5K3K1_GAIN_MAX);
    }

    /* 5. 若有真實 .tab 目錄，在 ASan/UBSan 下逐筆解析三份二進位表 */
    if (tabs_dir && tabs_dir[0]) {
        char pbuf[1024];
        snprintf(pbuf, sizeof(pbuf), "%s/ov32c-front.tab", tabs_dir);
        verify_real_tab(pbuf, FC_SENSOR_FRONT, 1u, FC_OV32C_SLAVE7, 1280u, 720u, 1685u, 4u, 0u);
        snprintf(pbuf, sizeof(pbuf), "%s/ov13b10-ultra.tab", tabs_dir);
        verify_real_tab(pbuf, FC_SENSOR_ULTRA, 1u, FC_OV13B10_SLAVE7, 2000u, 1500u, 182u, 4u, 0u);
        snprintf(pbuf, sizeof(pbuf), "%s/s5k3k1-tele.tab", tabs_dir);
        verify_real_tab(pbuf, FC_SENSOR_TELE, 2u, FC_S5K3K1_SLAVE7, 3264u, 2448u, 454u, 4u, 0u);
        snprintf(pbuf, sizeof(pbuf), "%s/imx800-wide.tab", tabs_dir);
        verify_real_tab(pbuf, FC_SENSOR_WIDE, 2u, FC_IMX800_SLAVE7, 1920u, 1440u, 302u, 3u, FC_TAB_FLAG_CPHY);
        snprintf(pbuf, sizeof(pbuf), "%s/imx800-wide-m1.tab", tabs_dir);
        verify_real_tab(pbuf, FC_SENSOR_WIDE, 2u, FC_IMX800_SLAVE7, 4096u, 3072u, 302u, 3u, FC_TAB_FLAG_CPHY);
    }

    /* 6. 16-byte WM0 stride alignment and 256 KiB DMA guard boundary checks */
    CHECK(fc_vfe_wm_line_stride(fc_raw10_bytes_per_line(2000u)) == 2512u);
    CHECK(fc_vfe_wm_line_stride(fc_raw10_bytes_per_line(3264u)) == 4080u);
    CHECK(fc_vfe_wm_line_stride(fc_raw10_bytes_per_line(1920u)) == 2400u);
    CHECK(fc_vfe_wm_line_stride(fc_raw10_bytes_per_line(4096u)) == 5120u);
    CHECK(fc_vfe_wm_frame_incr(2500u, 1500u) == ((2512u * 1500u) >> 8));
    CHECK(fc_vfe_wm_alloc_bytes(2512u * 1500u, 16383u) == 4030464u);
    CHECK(fc_vfe_wm_alloc_bytes(2512u * 1500u, 16383u) >= (2512u * 1500u) + FC_DMA_GUARD_BYTES);
    CHECK(fc_vfe_wm_alloc_bytes(2400u * 1440u, 16383u) == 3719168u);
    CHECK(fc_vfe_wm_alloc_bytes(5120u * 3072u, 16383u) == 15990784u);
    {
        uint8_t gbuf[64];
        memset(gbuf, 0x5au, sizeof(gbuf));
        CHECK(fc_guard_scan(NULL, 0u, 16u, 0x5au) == 0u);
        CHECK(fc_guard_scan(gbuf, 32u, 32u, 0x5au) == 0u);
        CHECK(fc_guard_scan(gbuf, 40u, 16u, 0x5au) == 0u);
        CHECK(fc_guard_scan(gbuf, 16u, 48u, 0x5au) == 0u);
        gbuf[0] = 0x11u;
        gbuf[15] = 0x22u;
        gbuf[48] = 0x33u;
        CHECK(fc_guard_scan(gbuf, 16u, 48u, 0x5au) == 0u);
        gbuf[16] = 0x00u;
        gbuf[47] = 0xffu;
        CHECK(fc_guard_scan(gbuf, 16u, 48u, 0x5au) == 2u);
    }

    /* 7. IMX800 Sony 曝光/類比增益（Q8）單調性與邊界飽和 */
    CHECK(fc_imx800_gain_to_reg(128u) == 0u && fc_imx800_gain_to_reg(256u) == 0u);
    CHECK(fc_imx800_gain_to_reg(512u) == 0x2000u && fc_imx800_gain_to_reg(1024u) == 0x3000u);
    CHECK(fc_imx800_gain_to_reg(16384u) == 0x3f00u && fc_imx800_gain_to_reg(32768u) == 0x3f00u);
    uint32_t prev_reg = 0u;
    for (uint32_t gq = 256u; gq <= 16384u; gq += 17u) {
        const uint32_t reg = fc_imx800_gain_to_reg(gq);
        CHECK(reg >= prev_reg && reg <= 0x3f00u);
        prev_reg = reg;
    }
    struct fc_ov32c_exposure imx_exp;
    CHECK(fc_sensor_clamp_exposure_30fps(FC_SENSOR_WIDE, 1u, 1024u, 100u, &imx_exp) == 0);
    CHECK(imx_exp.lines == FC_IMX800_MIN_LINES && imx_exp.gain == 1024u && imx_exp.frame_lines == FC_IMX800_VTS_30FPS);
    struct fc_tab_entry imx_seq[FC_IMX800_EXP_REGS];
    CHECK(fc_imx800_encode_exposure(&imx_exp, imx_seq) == FC_IMX800_EXP_REGS);
    CHECK(imx_seq[0].reg == 0x0104u && imx_seq[0].val == 0x01u);
    CHECK(imx_seq[1].reg == 0x3150u && imx_seq[1].val == 0x00u);
    CHECK(imx_seq[6].reg == 0x0204u && imx_seq[6].val == 0x30u && imx_seq[7].reg == 0x0205u && imx_seq[7].val == 0x00u);
    CHECK(imx_seq[8].reg == 0x020eu && imx_seq[8].val == 0x01u && imx_seq[9].reg == 0x020fu && imx_seq[9].val == 0x00u);
    CHECK(imx_seq[10].reg == 0x0104u && imx_seq[10].val == 0x00u);

    /* 8. IMX800 stream_on/off、AK7314 8-bit I2C mock 與 BU24721/BU24618 32-bit OIS mock */
    Slave imx_s, ak_s, ois_s;
    memset(&imx_s, 0, sizeof(imx_s));
    memset(&ak_s, 0, sizeof(ak_s));
    memset(&ois_s, 0, sizeof(ois_s));
    imx_s.address = FC_IMX800_SLAVE7;
    ak_s.address = FC_AK7314_SLAVE7;
    ak_s.reg8 = 1;
    ois_s.address = FC_OIS_SLAVE7;
    ois_s.regs[0xf008u] = 0x00u; ois_s.regs[0xf009u] = 0x06u; ois_s.regs[0xf00au] = 0xceu; ois_s.regs[0xf00bu] = 0xbfu;

    Bus xwire;
    memset(&xwire, 0, sizeof(xwire));
    xwire.sda = 1; xwire.scl = 1;
    xwire.slaves[0] = &imx_s;
    xwire.slaves[1] = &ak_s;
    xwire.slaves[2] = &ois_s;
    xwire.nslaves = 3;
    static const struct fc_bus_ops kWideOps = {op_line, op_level, op_wait};
    struct fc_bus xbus = {&kWideOps, &xwire, PIN_SDA, PIN_SCL, 5u, 200u, 0u, 0u, 0u};

    CHECK(fc_sensor_stream_on(FC_SENSOR_WIDE, &xbus, FC_IMX800_SLAVE7) == FC_I2C_OK);
    CHECK(imx_s.regs[0x0101u] == 0x03u && imx_s.regs[0x0100u] == 0x01u);
    CHECK(fc_sensor_stream_off(FC_SENSOR_WIDE, &xbus, FC_IMX800_SLAVE7) == FC_I2C_OK);
    CHECK(imx_s.regs[0x0100u] == 0x00u);

    CHECK(fc_i2c_write8_u8(&xbus, FC_AK7314_SLAVE7, 0x02u, 0x00u) == FC_I2C_OK);
    CHECK(ak_s.regs[0x02u] == 0x00u);
    CHECK(FC_AK7314_DEFAULT_INIT_POS >= 350u && FC_AK7314_DEFAULT_INIT_POS <= 400u);
    CHECK(FC_AK7314_SETTLE_MS == 15u);
    CHECK(fc_ak7314_decode_pos(fc_ak7314_encode_pos(FC_AK7314_DEFAULT_INIT_POS)) == FC_AK7314_DEFAULT_INIT_POS);
    CHECK(fc_i2c_write8_u16(&xbus, FC_AK7314_SLAVE7, 0x00u, fc_ak7314_encode_pos(512u)) == FC_I2C_OK);
    CHECK(ak_s.regs[0x00u] == 0x80u && ak_s.regs[0x01u] == 0x00u);
    ak_s.regs[0x84u] = ak_s.regs[0x00u];
    ak_s.regs[0x85u] = ak_s.regs[0x01u];
    uint16_t ak_rb = 0u;
    CHECK(fc_i2c_read8_u16(&xbus, FC_AK7314_SLAVE7, 0x84u, &ak_rb) == FC_I2C_OK);
    CHECK(fc_ak7314_decode_pos(ak_rb) == 512u);
    CHECK(fc_i2c_write8_u8(&xbus, FC_AK7314_SLAVE7, 0x02u, 0x40u) == FC_I2C_OK);
    CHECK(ak_s.regs[0x02u] == 0x40u);

    CHECK(fc_i2c_poll16_u32(&xbus, FC_OIS_SLAVE7, 0xf008u, FC_BU24618_CHIP_ID, 3u, 0u) == FC_I2C_OK);
    ois_s.regs[0xf00bu] = 0x00u;
    CHECK(fc_i2c_poll16_u32(&xbus, FC_OIS_SLAVE7, 0xf008u, FC_BU24618_CHIP_ID, 2u, 0u) == FC_I2C_TIMEOUT);
    CHECK(fc_i2c_write16_u32(&xbus, FC_OIS_SLAVE7, 0x3fffu, 0x40299869u) == FC_I2C_OK);
    CHECK(ois_s.regs[0x3fffu] == 0x40u && ois_s.regs[0x4000u] == 0x29u &&
          ois_s.regs[0x4001u] == 0x98u && ois_s.regs[0x4002u] == 0x69u);

    /* 9. IMX800 rails (ldom4/ldom7 3104u, custom2) 與 CIT_LSHIFT (0x3150 = 0..7) 回歸 */
    const struct fc_sensor_hw *wide_hw = fc_sensor_hw_for(FC_SENSOR_WIDE);
    CHECK(wide_hw && wide_hw->rail_count == 5u);
    CHECK(fc_str_eq(wide_hw->rails[1].role, "vaf") && fc_str_eq(wide_hw->rails[1].name, "ldom4") &&
          wide_hw->rails[1].mv == 3104u && wide_hw->rails[1].shared_keep_on == 0u);
    CHECK(fc_str_eq(wide_hw->rails[2].role, "custom2") && fc_str_eq(wide_hw->rails[2].name, "ldom7") &&
          wide_hw->rails[2].mv == 3104u && wide_hw->rails[2].shared_keep_on == 0u);
    {
        uint32_t cit = 0u;
        CHECK(fc_imx800_calc_lshift(0xffc0u, &cit) == 0u && cit == 0xffc0u);
        CHECK(fc_imx800_calc_lshift(0xffcfu, &cit) == 1u && cit == 0x7fe7u);
        CHECK(fc_imx800_calc_lshift(0x1ff9du, &cit) == 2u && cit == 0x7fe7u);
        CHECK(fc_imx800_calc_lshift(0x3ff39u, &cit) == 3u && cit == 0x7fe7u);
        CHECK(fc_imx800_calc_lshift(0x7fe71u, &cit) == 4u && cit == 0x7fe7u);
        CHECK(fc_imx800_calc_lshift(0xffce1u, &cit) == 5u && cit == 0x7fe7u);
        CHECK(fc_imx800_calc_lshift(0x1ff9c1u, &cit) == 6u && cit == 0x7fe7u);
        CHECK(fc_imx800_calc_lshift(0x3ff381u, &cit) == 7u && cit == 0x7fe7u);
        CHECK(fc_imx800_calc_lshift(0x7fe700u, &cit) == 7u && cit == (FC_IMX800_VTS_MAX - FC_IMX800_MARGIN));
        CHECK(fc_imx800_calc_lshift(0xffffffffu, &cit) == 7u && cit == (FC_IMX800_VTS_MAX - FC_IMX800_MARGIN));
        struct fc_ov32c_exposure lexp = { .lines = 0x40000u, .gain = 256u, .frame_lines = 4000u };
        struct fc_tab_entry lseq[FC_IMX800_EXP_REGS];
        CHECK(fc_imx800_encode_exposure(&lexp, lseq) == FC_IMX800_EXP_REGS);
        CHECK(lseq[1].reg == 0x3150u && lseq[1].val == 3u);
        CHECK(lseq[4].reg == 0x0202u && lseq[4].val == 0x80u && lseq[5].reg == 0x0203u && lseq[5].val == 0x00u);
    }

    /* 10. OIS 辨識/釋放安全性：Tele BU24721 絕不先寫 0xf010 且 release 寫 0xf020=0x01 + poll 0xf024==0x01 */
    {
        memset(&ois_s, 0, sizeof(ois_s));
        ois_s.address = FC_OIS_SLAVE7;
        ois_s.regs[0xf024u] = 0x01u;
        int is_24618 = -1;
        fc_ois_detect_24618(&xbus, FC_OIS_SLAVE7, FC_SENSOR_TELE, &is_24618, 0u);
        CHECK(is_24618 == 0 && ois_s.write_f010_count == 0u);
        CHECK(fc_ois_release(&xbus, FC_OIS_SLAVE7, 1, (uint8_t)is_24618, 0u) == FC_I2C_OK);
        CHECK(ois_s.write_f020_count == 1u && ois_s.regs[0xf020u] == 0x01u && ois_s.write_6020_count == 0u);

        memset(&ois_s, 0, sizeof(ois_s));
        ois_s.address = FC_OIS_SLAVE7;
        ois_s.wake_24618_on_f010 = 1;
        is_24618 = -1;
        fc_ois_detect_24618(&xbus, FC_OIS_SLAVE7, FC_SENSOR_TELE, &is_24618, 0u);
        CHECK(is_24618 == 1 && ois_s.write_f010_count == 1u && ois_s.regs[0xf010u] == 0x00u);
        CHECK(fc_ois_release(&xbus, FC_OIS_SLAVE7, 1, (uint8_t)is_24618, 0u) == FC_I2C_OK);
        CHECK(ois_s.write_6020_count == 1u && ois_s.regs[0x6020u] == 0x00u && ois_s.write_f020_count == 0u);
    }

    /* 11. M5 錄影多模式切換 / IMX800 RES[3] 4K60 / S5K3K1 60fps / 動態 VTS_MIN / 480 MHz RCG */
    {
        CHECK(FC_S5K3K1_VTS_MIN == 2250u && FC_S5K3K1_VTS_30FPS == 4500u);
        const struct fc_sensor_hw *hw_wide = fc_sensor_hw_for(FC_SENSOR_WIDE);
        const struct fc_sensor_hw *hw_tele = fc_sensor_hw_for(FC_SENSOR_TELE);
        const struct fc_sensor_hw *hw_ultra = fc_sensor_hw_for(FC_SENSOR_ULTRA);
        const struct fc_sensor_hw *hw_front = fc_sensor_hw_for(FC_SENSOR_FRONT);
        CHECK(hw_wide && hw_tele && hw_ultra && hw_front);

        CHECK(fc_sensor_vts_floor(hw_wide, FC_MODE_PHOTO) == FC_IMX800_VTS_30FPS);
        CHECK(fc_sensor_vts_floor(hw_wide, FC_MODE_VIDEO60) == FC_IMX800_VTS_MIN);
        const uint32_t wide_fps30 = fc_sensor_fps_milli(hw_wide->line_ns, fc_sensor_vts_floor(hw_wide, FC_MODE_PHOTO));
        const uint32_t wide_fps60 = fc_sensor_fps_milli(hw_wide->line_ns, fc_sensor_vts_floor(hw_wide, FC_MODE_VIDEO60));
        CHECK(wide_fps30 >= 29990u && wide_fps30 <= 30010u);
        CHECK(wide_fps60 >= 59990u && wide_fps60 <= 60010u);

        CHECK(fc_sensor_vts_floor(hw_tele, FC_MODE_PHOTO) == FC_S5K3K1_VTS_30FPS);
        CHECK(fc_sensor_vts_floor(hw_tele, FC_MODE_VIDEO60) == 2250u);
        const uint32_t tele_fps60 = fc_sensor_fps_milli(hw_tele->line_ns, fc_sensor_vts_floor(hw_tele, FC_MODE_VIDEO60));
        CHECK(tele_fps60 >= 60000u && tele_fps60 <= 60100u);

        CHECK(fc_sensor_vts_floor(hw_ultra, FC_MODE_VIDEO) == FC_OV13B10_VTS_30FPS);
        CHECK(fc_sensor_vts_floor(hw_front, FC_MODE_VIDEO60) == 1307u);

        struct fc_ov32c_exposure e30, e60, et60;
        CHECK(fc_sensor_clamp_exposure_mode(FC_SENSOR_WIDE, FC_MODE_PHOTO, 1000u, 256u, 3742u, &e30) == 0);
        CHECK(e30.frame_lines == FC_IMX800_VTS_30FPS);
        CHECK(fc_sensor_clamp_exposure_mode(FC_SENSOR_WIDE, FC_MODE_VIDEO60, 1000u, 256u, 3742u, &e60) == 0);
        CHECK(e60.frame_lines == 3742u);
        CHECK(fc_sensor_clamp_exposure_mode(FC_SENSOR_WIDE, FC_MODE_VIDEO60, 1000u, 256u, 100u, &e60) == 0);
        CHECK(e60.frame_lines == 3742u);
        CHECK(fc_sensor_clamp_exposure_mode(FC_SENSOR_TELE, FC_MODE_VIDEO60, 1000u, 32u, 2250u, &et60) == 0);
        CHECK(et60.frame_lines == 2250u);

        CHECK(fc_sensor_needs_480mhz(4384u, 2464u, 60000u) == 1);
        CHECK(fc_sensor_needs_480mhz(3648u, 2052u, 60000u) == 1);
        CHECK(fc_sensor_needs_480mhz(4096u, 3072u, 30000u) == 0);
        CHECK(fc_sensor_needs_480mhz(1920u, 1440u, 30000u) == 0);
        CHECK((fc_rcg_cfg(0u, 1u, 4u) & 0x71fu) == 0x104u);

        /* BUG-CH2-01 硬防護回歸：所有 (sensor, mode) 與實測高度組合皆須滿足 vts_floor >= height + margin */
        static const struct {
            uint32_t sid;
            uint32_t mode;
            uint32_t active_h;
            uint32_t expect_min_vts;
        } kVtsMatrix[] = {
            { FC_SENSOR_WIDE,  FC_MODE_PHOTO,   3072u, FC_IMX800_VTS_30FPS },
            { FC_SENSOR_WIDE,  FC_MODE_VIDEO,   3072u, FC_IMX800_VTS_30FPS },
            { FC_SENSOR_WIDE,  FC_MODE_VIDEO60, 2464u, FC_IMX800_VTS_MIN },
            { FC_SENSOR_WIDE,  FC_MODE_VIDEO60, 3072u, FC_IMX800_VTS_MIN },
            { FC_SENSOR_FRONT, FC_MODE_PHOTO,   2448u, FC_OV32C_VTS_30FPS },
            { FC_SENSOR_FRONT, FC_MODE_VIDEO,   2448u, FC_OV32C_VTS_30FPS },
            { FC_SENSOR_FRONT, FC_MODE_VIDEO60,  720u, 1307u },
            { FC_SENSOR_FRONT, FC_MODE_VIDEO60, 2448u, FC_OV32C_VTS_30FPS },
            { FC_SENSOR_ULTRA, FC_MODE_PHOTO,   1500u, FC_OV13B10_VTS_30FPS },
            { FC_SENSOR_ULTRA, FC_MODE_VIDEO,   1500u, FC_OV13B10_VTS_30FPS },
            { FC_SENSOR_ULTRA, FC_MODE_VIDEO60, 1500u, FC_OV13B10_VTS_MIN },
            { FC_SENSOR_TELE,  FC_MODE_PHOTO,   2448u, FC_S5K3K1_VTS_30FPS },
            { FC_SENSOR_TELE,  FC_MODE_VIDEO,   2448u, FC_S5K3K1_VTS_30FPS },
            { FC_SENSOR_TELE,  FC_MODE_VIDEO60, 2052u, FC_S5K3K1_VTS_MIN },
            { FC_SENSOR_TELE,  FC_MODE_VIDEO60, 2448u, FC_S5K3K1_VTS_30FPS },
        };
        for (size_t mi = 0u; mi < sizeof(kVtsMatrix) / sizeof(kVtsMatrix[0]); mi++) {
            const struct fc_sensor_hw *shw = fc_sensor_hw_for(kVtsMatrix[mi].sid);
            CHECK(shw != NULL);
            const uint32_t vf = fc_sensor_vts_floor_for_height(shw, kVtsMatrix[mi].mode, kVtsMatrix[mi].active_h);
            CHECK(vf == kVtsMatrix[mi].expect_min_vts);
            CHECK(vf >= kVtsMatrix[mi].active_h + shw->margin);
            struct fc_ov32c_exposure ce;
            CHECK(fc_sensor_clamp_exposure_for_height(kVtsMatrix[mi].sid, kVtsMatrix[mi].mode,
                kVtsMatrix[mi].active_h, 100u, shw->gain_unity, 1u, &ce) == 0);
            CHECK(ce.frame_lines >= kVtsMatrix[mi].active_h + shw->margin);
        }

        if (tabs_dir) {
            static const struct {
                const char *fname;
                uint32_t sid;
                uint32_t mode;
                uint32_t expect_w;
                uint32_t expect_h;
                uint32_t expect_cnt;
            } kTabs[] = {
                { "imx800-wide-m1.tab",  FC_SENSOR_WIDE,  FC_MODE_PHOTO,   4096u, 3072u, 302u },
                { "imx800-wide-4k60.tab",FC_SENSOR_WIDE,  FC_MODE_VIDEO60, 4384u, 2464u, 302u },
                { "ov32c-front-m1.tab",  FC_SENSOR_FRONT, FC_MODE_PHOTO,   3264u, 2448u, 1685u },
                { "ov32c-front.tab",     FC_SENSOR_FRONT, FC_MODE_VIDEO60, 1280u,  720u, 1685u },
                { "ov13b10-ultra.tab",   FC_SENSOR_ULTRA, FC_MODE_VIDEO,   2000u, 1500u, 182u },
                { "s5k3k1-tele.tab",     FC_SENSOR_TELE,  FC_MODE_PHOTO,   3264u, 2448u, 454u },
                { "s5k3k1-tele-60.tab",  FC_SENSOR_TELE,  FC_MODE_VIDEO60, 3648u, 2052u, 454u },
            };
            for (size_t ti = 0u; ti < sizeof(kTabs) / sizeof(kTabs[0]); ti++) {
                char tp[512];
                snprintf(tp, sizeof(tp), "%s/%s", tabs_dir, kTabs[ti].fname);
                FILE *fp = fopen(tp, "rb");
                CHECK(fp != NULL);
                uint8_t tbuf[8192];
                size_t tn = fread(tbuf, 1, sizeof(tbuf), fp);
                fclose(fp);
                struct fc_tab_header th;
                uint8_t tsid = 0u;
                CHECK(fc_tab_parse_any(tbuf, (uint32_t)tn, &th, &tsid) == 0);
                if (tsid != 0u) CHECK(tsid == kTabs[ti].sid);
                CHECK(th.width == kTabs[ti].expect_w && th.height == kTabs[ti].expect_h);
                CHECK(th.count == kTabs[ti].expect_cnt);
                const struct fc_sensor_hw *thw = fc_sensor_hw_for(kTabs[ti].sid);
                const uint32_t tvf = fc_sensor_vts_floor_for_height(thw, kTabs[ti].mode, th.height);
                CHECK(tvf >= th.height + thw->margin);
            }
        }
    }
}


static void flash_and_fps_format(void) {
    uint32_t lv = 99u, ms = 99u, mode = 99u;
    /* OFF and level=0 disable flash cleanly */
    CHECK(fc_clamp_flash_cmd(4u, 500u, FUXI_CAMERA_FLASH_OFF, &lv, &ms, &mode) == 0);
    CHECK(lv == 0u && ms == 0u && mode == FUXI_CAMERA_FLASH_OFF);
    CHECK(fc_clamp_flash_cmd(0u, 500u, FUXI_CAMERA_FLASH_TORCH_FILL, &lv, &ms, &mode) == 0);
    /* Safe torch-fill clamps level <= 4 and duration <= 2000ms */
    CHECK(fc_clamp_flash_cmd(10u, 9999u, FUXI_CAMERA_FLASH_TORCH_FILL, &lv, &ms, &mode) == 1);
    CHECK(lv == FUXI_CAMERA_FLASH_MAX_LEVEL && ms == FUXI_CAMERA_FLASH_DEFAULT_MS && mode == FUXI_CAMERA_FLASH_TORCH_FILL);
    CHECK(fc_clamp_flash_cmd(3u, 0u, FUXI_CAMERA_FLASH_TORCH_FILL, &lv, &ms, &mode) == 1);
    CHECK(lv == 3u && ms == FUXI_CAMERA_FLASH_DEFAULT_MS);
    /* Negative: unverified high-current strobe and invalid mode must fail closed */
    CHECK(fc_clamp_flash_cmd(1u, 300u, FUXI_CAMERA_FLASH_STROBE, &lv, &ms, &mode) < 0);
    CHECK(fc_clamp_flash_cmd(1u, 300u, 99u, &lv, &ms, &mode) < 0);
    /* Flash state window & teardown/close clear lifecycle */
    {
        int active = 1;
        uint32_t flv = 4u;
        uint64_t until = 2500ull;
        CHECK(fc_flash_step_lit(&active, &flv, &until, 2500ull) == 1);
        CHECK(active == 1 && flv == 4u && until == 2500ull);
        /* Expired window clears all three fields and returns 0 */
        CHECK(fc_flash_step_lit(&active, &flv, &until, 2501ull) == 0);
        CHECK(active == 0 && flv == 0u && until == 0ull);
        /* Zero level with active=1 fails closed and clears state */
        active = 1; flv = 0u; until = 5000ull;
        CHECK(fc_flash_step_lit(&active, &flv, &until, 1000ull) == 0);
        CHECK(active == 0 && flv == 0u && until == 0ull);
    }
    /* Integer @%u.%ufps kprintf formatting parity (P0-5) */
    char fps_buf[32];
    const uint32_t m30 = 30000u, m601 = 60120u, m299 = 29970u;
    snprintf(fps_buf, sizeof(fps_buf), "%u.%u", m30 / 1000u, (m30 % 1000u) / 100u);
    CHECK(strcmp(fps_buf, "30.0") == 0);
    snprintf(fps_buf, sizeof(fps_buf), "%u.%u", m601 / 1000u, (m601 % 1000u) / 100u);
    CHECK(strcmp(fps_buf, "60.1") == 0);
    snprintf(fps_buf, sizeof(fps_buf), "%u.%u", m299 / 1000u, (m299 % 1000u) / 100u);
    CHECK(strcmp(fps_buf, "29.9") == 0);
}

int main(int argc, char **argv) {
    encodings();
    transfers();
    stretching();
    wedged();
    fuzz();
    c2b_pipeline();
    c3_sensor_and_table();
    c4_stream_and_exposure();
    multi_sensor_and_tab2(argc > 1 ? argv[1] : NULL);
    flash_and_fps_format();
    printf("PASS fuxi camera proto: %u checks; TLMM/RCG/GDSC/VRM encodings, bit-banged I2C probe/read16/write16_u16/poll16_u8, "
           "clock stretching, stuck clock, 9-clock recovery, EEPROM write refusal, scan fuzz, C2b TPG/CSID/VFE/SMMU, C3 OV32C/CSIPHY4, "
           "C4 ABI/exposure/ring, multi-sensor hw/FUXICAM2/OV13B10/S5K3K1\n", checks);
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--sanitize', action='store_true')
    arguments = parser.parse_args()
    tools = runpy.run_path(str(ROOT / 'tools/test-fuxi-audio-prep.py'))
    compiler, environment = tools['compiler_environment']()
    microsoft = Path(compiler[0].strip('"')).name.lower() in ('cl', 'cl.exe')
    with tempfile.TemporaryDirectory(prefix='fuxi-camera-') as temporary:
        directory = Path(temporary)
        source = directory / 'camera.c'
        source.write_text(SOURCE, encoding='utf-8', newline='\n')
        binary = directory / ('camera.exe' if os.name == 'nt' else 'camera')
        if microsoft:
            flags = ['/nologo', '/std:c11', '/W4', '/WX', '/Zi', '/Od', '/MD', '/utf-8']
            if arguments.sanitize:
                flags.append('/fsanitize=address')
            command = compiler + flags + [f'/I{ROOT / "xnu/fuxi-camera"}', str(source), f'/Fe:{binary}']
        else:
            flags = ['-std=c11', '-g', '-Wall', '-Wextra', '-Werror', '-O1']
            if arguments.sanitize:
                flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            command = compiler + flags + ['-I', str(ROOT / 'xnu/fuxi-camera'), str(source), '-o', str(binary)]
        subprocess.run(command, cwd=directory, env=environment, check=True, timeout=120)
        run_cmd = [str(binary)]
        tabs_dir = ROOT.parent / 'out/tabs'
        if (tabs_dir / 's5k3k1-tele.tab').is_file():
            run_cmd.append(str(tabs_dir))
        subprocess.run(run_cmd, cwd=directory, env=environment, check=True, timeout=120)
    cam_src = ROOT / 'xnu/fuxi-camera/fuxi_camera.c'
    if cam_src.is_file():
        cam_txt = cam_src.read_text(encoding='utf-8')
        td_idx = cam_txt.index('teardown:')
        td_end = cam_txt.index('lck_mtx_unlock(&gC4Lock);', td_idx)
        td_blk = cam_txt[td_idx:td_end]
        assert 'gC4FlashActive = 0;' in td_blk, 'teardown must clear gC4FlashActive'
        assert 'gC4FlashLevel = 0u;' in td_blk, 'teardown must clear gC4FlashLevel'
        assert 'gC4FlashUntilMs = 0ull;' in td_blk, 'teardown must clear gC4FlashUntilMs'
        cl_idx = cam_txt.index('static int camdevClose(')
        cl_end = cam_txt.index('lck_mtx_unlock(&gC4Lock);', cl_idx)
        cl_blk = cam_txt[cl_idx:cl_end]
        assert 'gC4FlashActive = 0;' in cl_blk, 'camdevClose must clear gC4FlashActive'
        assert 'gC4FlashLevel = 0u;' in cl_blk, 'camdevClose must clear gC4FlashLevel'
        assert 'gC4FlashUntilMs = 0ull;' in cl_blk, 'camdevClose must clear gC4FlashUntilMs'


if __name__ == '__main__':
    main()
