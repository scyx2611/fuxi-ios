#!/usr/bin/env python3
"""End-to-end host check of the WCN7850 Bluetooth board glue (fuxi,bt=3).

Production fuxi_bt_board.c + fuxi_bt_geni_fifo.c + fuxi_bt.c run against a
simulated GCC / TLMM / QUP wrapper and the GENI register model that
test-fuxi-bt-geni-fifo.py already uses (reused, not copied), so the clock,
pin and BT_EN sequence, the QCA version query over the FIFO driver and the
full restore are exercised together.
"""

import argparse
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "xnu" / "fuxi-bluetooth"

BOARD = r'''
#include <stdlib.h>
#include "fuxi_bt_board.h"

#define GCC(offset) (FUXI_BT_GCC_BASE + (uint64_t)(offset))
#define RCG(offset) GCC(0x1e760u + (offset))
#define TLMM_CTL(pin) (FUXI_BT_TLMM_BASE + (uint64_t)(pin) * 0x1000u)
#define TLMM_IO(pin) (TLMM_CTL(pin) + 4u)

static const uint32_t branch_offsets[] = {0x233d4, 0x233d0, 0x233cc, 0x23004, 0x1e754};
static const uint32_t branch_votes[] = {1u << 0, 1u << 1, 1u << 2, 1u << 3, 1u << 10};

struct soc_reg {
    uint64_t pa;
    uint32_t value;
    uint32_t original;
    bool writable;
    unsigned writes;
};

struct soc {
    struct model uart;
    struct soc_reg regs[40];
    unsigned count;
    unsigned calls, fail_at, failed_call;
    unsigned rcg_pending;
    bool branch_stuck, rcg_stuck, rcg_ignore_cfg, rcg_root_off, pin_stuck, bt_en_dead;
    bool sw_ctrl_follows, replied;
    uint64_t bt_en_low_at, bt_en_high_at, bt_en_checked_at;
    unsigned bt_en_high_writes;
};

static struct soc_reg *soc_find(struct soc *soc, uint64_t pa)
{
    for (unsigned index = 0; index < soc->count; index++) {
        if (soc->regs[index].pa == pa) {
            return &soc->regs[index];
        }
    }
    fprintf(stderr, "access outside the model pa=%#llx\n", (unsigned long long)pa);
    assert(false);
    return NULL;
}

static void soc_add(struct soc *soc, uint64_t pa, uint32_t value, bool writable)
{
    assert(soc->count < sizeof(soc->regs) / sizeof(soc->regs[0]));
    soc->regs[soc->count++] = (struct soc_reg){pa, value, value, writable, 0};
}

static uint32_t soc_value(struct soc *soc, uint64_t pa)
{
    return soc_find(soc, pa)->value;
}

static void soc_init(struct soc *soc)
{
    memset(soc, 0, sizeof(*soc));
    model_init(&soc->uart);
    soc_add(soc, GCC(0x0), (1u << 31) | 7u, false);          /* GPLL0 MODE：LOCK_DET */
    soc_add(soc, GCC(0x18), 0x403u, false);                   /* USER_CTL：main+even、後除 2 */
    soc_add(soc, GCC(0x52018), 0x1u, false);                  /* GPLL0 投票 */
    soc_add(soc, GCC(0x52010), 0x10000u, true);               /* 實機觀察到的 QUP 投票值 */
    for (unsigned index = 0; index < 5; index++) {
        /* s_ahb、m_ahb 開著 HWCG，閒置時一直回報 off。 */
        soc_add(soc, GCC(branch_offsets[index]), index == 1 || index == 2 ? 2u : 0u, false);
    }
    soc_add(soc, RCG(0), 0, true);
    soc_add(soc, RCG(4), 0x100000u, true);                    /* TCXO、HW_CLK_CTRL */
    soc_add(soc, RCG(8), 0x10000u, true);
    soc_add(soc, RCG(12), 0x20000u, true);
    soc_add(soc, RCG(16), 0x30000u, true);
    soc_add(soc, FUXI_BT_WRAPPER_BASE + 4u, 0x30090000u, false);
    for (unsigned pin = 76; pin <= 79; pin++) {
        soc_add(soc, TLMM_CTL(pin), 0x800u | (pin < 78 ? 1u : 3u), true);
    }
    soc_add(soc, TLMM_CTL(80), 0x200u, false);                /* WLAN_EN：GPIO 輸出 */
    soc_add(soc, TLMM_IO(80), 0x2u, false);
    soc_add(soc, TLMM_CTL(81), 0x801u, true);                 /* BT_EN：輸入、下拉 */
    soc_add(soc, TLMM_IO(81), 0x0u, true);
    soc_add(soc, TLMM_IO(82), 0x0u, false);                   /* SW_CTRL */
    soc->sw_ctrl_follows = true;
}

static bool soc_fault(struct soc *soc)
{
    soc->calls++;
    if (soc->fail_at && soc->calls == soc->fail_at) {
        soc->failed_call = soc->calls;
        return true;
    }
    return false;
}

static bool soc_uart(uint64_t pa)
{
    return pa >= FUXI_BT_UART_BASE && pa < FUXI_BT_UART_BASE + 0x4000u;
}

static bool soc_bt_on(struct soc *soc)
{
    return !soc->bt_en_dead && (soc_value(soc, TLMM_CTL(81)) & (1u << 9)) &&
        (soc_value(soc, TLMM_IO(81)) & 2u);
}

/* 控制器只在時脈、腳位、BT_EN 全對時回應。 */
static bool soc_controller_ready(struct soc *soc)
{
    return soc_bt_on(soc) &&
        (soc_value(soc, GCC(0x52010)) & 0x40fu) == 0x40fu &&
        (soc_value(soc, RCG(4)) & 0x371fu) == 0x2601u &&
        (soc_value(soc, RCG(8)) & 0xffffu) == 384u &&
        (soc_value(soc, RCG(12)) & 0xffffu) == 0xc476u &&
        (soc_value(soc, RCG(16)) & 0xffffu) == 0xc2f6u &&
        (soc_value(soc, TLMM_CTL(78)) & 0x3fcu) == 4u &&
        (soc_value(soc, TLMM_CTL(79)) & 0x3fcu) == 4u;
}

static void soc_step(struct soc *soc)
{
    static const uint8_t reply[] = {
        4, 0x0e, 18, 1, 0, 0xfc, 0, 0x19, 0x0c,
        0x78, 0x56, 0x34, 0x12, 0xbc, 0x9a, 0x20, 0,
        0x44, 0x33, 0x22, 0x11
    };
    model_tx_step(&soc->uart);
    if (!soc->replied && soc->uart.wire_size == 5 && soc_controller_ready(soc)) {
        assert(!memcmp(soc->uart.wire, (uint8_t[]){1, 0, 0xfc, 1, 0x19}, 5));
        model_rx_load(&soc->uart, reply, sizeof(reply));
        soc->replied = true;
    }
}

static enum fuxi_bt_status soc_read(void *context, uint64_t pa, uint32_t *value)
{
    struct soc *soc = context;
    struct soc_reg *reg;
    uint32_t result;
    assert(!(pa & 3));
    if (soc_fault(soc)) {
        return FUXI_BT_IO;
    }
    if (soc_uart(pa)) {
        return model_read(&soc->uart, (uint32_t)(pa - FUXI_BT_UART_BASE), value);
    }
    reg = soc_find(soc, pa);
    result = reg->value;
    for (unsigned index = 0; index < 5; index++) {
        if (pa == GCC(branch_offsets[index])) {
            const bool hwcg = result & 2u;
            if (hwcg || !(soc_value(soc, GCC(0x52010)) & branch_votes[index]) ||
                (soc->branch_stuck && index == 4)) {
                result |= 1u << 31;
            }
        }
    }
    if (pa == RCG(0)) {
        result &= ~((1u << 31) | 1u);
        if (soc->rcg_pending) {
            result |= 1u;
            if (!soc->rcg_stuck) {
                soc->rcg_pending--;
            }
        }
        if (!(soc_value(soc, GCC(0x52010)) & (1u << 10)) || soc->rcg_root_off) {
            result |= 1u << 31;
        }
    }
    if (pa == TLMM_IO(81)) {
        result = (result & 2u) | (soc_bt_on(soc) ? 1u : 0u);
        soc->bt_en_checked_at = soc->uart.clock;
    } else if (pa == TLMM_IO(80)) {
        result |= (result >> 1) & 1u;
    } else if (pa == TLMM_IO(82) && soc->sw_ctrl_follows) {
        result = soc_bt_on(soc) ? 1u : 0u;
    }
    *value = result;
    return FUXI_BT_OK;
}

static enum fuxi_bt_status soc_write(void *context, uint64_t pa, uint32_t value)
{
    struct soc *soc = context;
    struct soc_reg *reg;
    assert(!(pa & 3));
    if (soc_fault(soc)) {
        return FUXI_BT_IO;
    }
    if (soc_uart(pa)) {
        return model_write(&soc->uart, (uint32_t)(pa - FUXI_BT_UART_BASE), value);
    }
    reg = soc_find(soc, pa);
    assert(reg->writable);
    reg->writes++;
    if (pa == RCG(0)) {
        assert(!(value & ~((1u << 31) | 1u)));
        if (value & 1u) {
            soc->rcg_pending = 2;
        }
        return FUXI_BT_OK;
    }
    if (pa == RCG(4) && soc->rcg_ignore_cfg) {
        return FUXI_BT_OK;
    }
    if (pa == TLMM_CTL(78) && soc->pin_stuck) {
        return FUXI_BT_OK;
    }
    if (pa == TLMM_IO(81)) {
        assert(!(value & ~2u));
        if (value & 2u) {
            soc->bt_en_high_at = soc->uart.clock;
            soc->bt_en_high_writes++;
        }
    }
    if (pa == TLMM_CTL(81) && (value & (1u << 9)) && !(soc_value(soc, TLMM_IO(81)) & 2u)) {
        soc->bt_en_low_at = soc->uart.clock;
    }
    reg->value = value;
    return FUXI_BT_OK;
}

static uint64_t soc_now(void *context)
{
    return ((struct soc *)context)->uart.clock;
}

static void soc_wait(void *context, unsigned microseconds)
{
    struct soc *soc = context;
    soc->uart.clock += microseconds;
    soc_step(soc);
}

static struct fuxi_bt_board_io soc_io(struct soc *soc)
{
    struct fuxi_bt_board_io io = {soc, soc_read, soc_write, soc_now, soc_wait};
    return io;
}

/* GCC／TLMM 全部回到原值（RCG CMD 只比可寫位元）。 */
static void soc_assert_restored(struct soc *soc, uint64_t except)
{
    for (unsigned index = 0; index < soc->count; index++) {
        const struct soc_reg *reg = &soc->regs[index];
        if (reg->pa != except) {
            assert(reg->value == reg->original);
        }
    }
}

static void soc_assert_untouched(struct soc *soc)
{
    for (unsigned index = 0; index < soc->count; index++) {
        assert(!soc->regs[index].writes);
    }
    assert(!soc->uart.writes);
}

static void test_success(void)
{
    struct soc soc;
    struct fuxi_bt_board board;
    struct fuxi_bt_board_io io;
    struct fuxi_bt_uart_ops ops;
    struct fuxi_bt_result result;
    bool ready = false;
    soc_init(&soc);
    io = soc_io(&soc);
    assert(fuxi_bt_board_wlan_ready(&io, &ready) == FUXI_BT_OK && ready);
    assert(fuxi_bt_board_power_on(&board, &io) == FUXI_BT_OK);
    assert(board.stage == FUXI_BT_STAGE_READY && board.error == FUXI_BT_OK);
    assert(board.vote == 0x10000u && board.added_votes == 0x40fu);
    assert(soc_value(&soc, GCC(0x52010)) == 0x1040fu);
    assert(board.rcg_saved[0] == 0x100000u && board.rcg_cfg == 0x102601u);
    assert(soc_value(&soc, RCG(4)) == 0x102601u);
    assert(soc_value(&soc, RCG(8)) == 0x10180u);
    assert(soc_value(&soc, RCG(12)) == 0x2c476u);
    assert(soc_value(&soc, RCG(16)) == 0x3c2f6u);
    assert(soc_value(&soc, TLMM_CTL(76)) == 0x805u && soc_value(&soc, TLMM_CTL(77)) == 0x805u);
    assert(soc_value(&soc, TLMM_CTL(78)) == 0x807u && soc_value(&soc, TLMM_CTL(79)) == 0x807u);
    assert(soc_value(&soc, TLMM_CTL(81)) == 0xa01u && soc_value(&soc, TLMM_IO(81)) == 2u);
    assert(board.qup_version == 0x30090000u && board.firmware == 0x204u);
    assert(!board.sw_ctrl_before && board.sw_ctrl_after == 1u);
    assert(soc.bt_en_high_writes == 1);
    assert(soc.bt_en_high_at - soc.bt_en_low_at >= FUXI_BT_BT_EN_LOW_US);
    assert(soc.bt_en_checked_at - soc.bt_en_high_at >= FUXI_BT_POWERUP_US);
    assert(!soc.uart.writes);
    fuxi_bt_board_uart_ops(&board, &ops);
    assert(fuxi_bt_uart_ops_valid(&ops));
    assert(fuxi_bt_probe_run(1, &ops, &result) == FUXI_BT_OK);
    assert(result.version_valid && result.cleanup == FUXI_BT_OK && result.tx_bytes == 5);
    assert(result.version.product == 0x12345678u && result.version.soc == 0x11223344u);
    assert(result.version.rom == 0x20u && result.version.patch == 0x9abcu);
    assert(board.fifo.tx_completed == 5 && board.fifo.restored && board.fifo.quiescent);
    assert(!board.fifo_used);
    for (unsigned index = 0; index < sizeof(saved_regs) / sizeof(saved_regs[0]); index++) {
        assert(soc.uart.regs[saved_regs[index] / 4] == soc.uart.original[saved_regs[index] / 4]);
    }
    assert(fuxi_bt_board_power_off(&board) == FUXI_BT_OK);
    soc_assert_restored(&soc, 0);
    assert(!board.votes_changed && !board.rcg_changed && !board.pins_changed && !board.bt_en_changed);
    assert(fuxi_bt_board_power_off(&board) == FUXI_BT_OK);
    puts("PASS board success: GPLL0 gate, 5 votes, HWCG skip, RCG 7.3728 MHz M/N/D/CFG, pinmux, BT_EN 50+100 ms, QCA version over the FIFO driver, full restore");
}

static void test_vote_already_present(void)
{
    struct soc soc;
    struct fuxi_bt_board board;
    struct fuxi_bt_board_io io;
    soc_init(&soc);
    soc_find(&soc, GCC(0x52010))->value = soc_find(&soc, GCC(0x52010))->original = 0x1040fu;
    io = soc_io(&soc);
    assert(fuxi_bt_board_power_on(&board, &io) == FUXI_BT_OK);
    assert(!board.added_votes && !board.votes_changed);
    assert(fuxi_bt_board_power_off(&board) == FUXI_BT_OK);
    assert(!soc_find(&soc, GCC(0x52010))->writes);
    soc_assert_restored(&soc, 0);
}

typedef void (*soc_setup)(struct soc *);
static void setup_wlan_off(struct soc *soc) { soc_find(soc, TLMM_IO(80))->value = 0; }
static void setup_wlan_func(struct soc *soc) { soc_find(soc, TLMM_CTL(80))->value = 0x204u; }
static void setup_wlan_input(struct soc *soc) { soc_find(soc, TLMM_CTL(80))->value = 0; }
static void setup_gpll0_vote(struct soc *soc) { soc_find(soc, GCC(0x52018))->value = 0; }
static void setup_gpll0_lock(struct soc *soc) { soc_find(soc, GCC(0x0))->value = 7u; }
static void setup_gpll0_even(struct soc *soc) { soc_find(soc, GCC(0x18))->value = 0x401u; }
static void setup_gpll0_div(struct soc *soc) { soc_find(soc, GCC(0x18))->value = 0x003u; }
static void setup_branch(struct soc *soc) { soc->branch_stuck = true; }
static void setup_rcg_busy(struct soc *soc) { soc->rcg_pending = 1; soc->rcg_stuck = true; }
static void setup_rcg_stuck(struct soc *soc) { soc->rcg_stuck = true; }
static void setup_rcg_cfg(struct soc *soc) { soc->rcg_ignore_cfg = true; }
static void setup_rcg_off(struct soc *soc) { soc->rcg_root_off = true; }
static void setup_wrapper_zero(struct soc *soc) { soc_find(soc, FUXI_BT_WRAPPER_BASE + 4u)->value = 0; }
static void setup_wrapper_ones(struct soc *soc) { soc_find(soc, FUXI_BT_WRAPPER_BASE + 4u)->value = ~0u; }
static void setup_protocol(struct soc *soc) { soc->uart.regs[0x68 / 4] = 0x104u; }
static void setup_disabled(struct soc *soc) { soc->uart.regs[0x64 / 4] = 1u; }
static void setup_pin(struct soc *soc) { soc->pin_stuck = true; }
static void setup_bt_en(struct soc *soc) { soc->bt_en_dead = true; }

static void test_refusals(void)
{
    static const struct {
        soc_setup setup;
        enum fuxi_bt_status status;
        enum fuxi_bt_board_stage stage;
        enum fuxi_bt_status cleanup;
        bool writes;
    } cases[] = {
        {setup_wlan_off, FUXI_BT_OFF, FUXI_BT_STAGE_WLAN, FUXI_BT_OK, false},
        {setup_wlan_func, FUXI_BT_OFF, FUXI_BT_STAGE_WLAN, FUXI_BT_OK, false},
        {setup_wlan_input, FUXI_BT_OFF, FUXI_BT_STAGE_WLAN, FUXI_BT_OK, false},
        {setup_gpll0_vote, FUXI_BT_NO_UART, FUXI_BT_STAGE_GPLL0, FUXI_BT_OK, false},
        {setup_gpll0_lock, FUXI_BT_NO_UART, FUXI_BT_STAGE_GPLL0, FUXI_BT_OK, false},
        {setup_gpll0_even, FUXI_BT_NO_UART, FUXI_BT_STAGE_GPLL0, FUXI_BT_OK, false},
        {setup_gpll0_div, FUXI_BT_NO_UART, FUXI_BT_STAGE_GPLL0, FUXI_BT_OK, false},
        {setup_branch, FUXI_BT_TIMEOUT, FUXI_BT_STAGE_BRANCH, FUXI_BT_OK, true},
        {setup_rcg_busy, FUXI_BT_BUSY, FUXI_BT_STAGE_RCG, FUXI_BT_OK, true},
        {setup_rcg_stuck, FUXI_BT_TIMEOUT, FUXI_BT_STAGE_RCG, FUXI_BT_TIMEOUT, true},
        {setup_rcg_cfg, FUXI_BT_IO, FUXI_BT_STAGE_RCG, FUXI_BT_OK, true},
        {setup_rcg_off, FUXI_BT_OFF, FUXI_BT_STAGE_RCG, FUXI_BT_OK, true},
        {setup_wrapper_zero, FUXI_BT_NO_UART, FUXI_BT_STAGE_WRAPPER, FUXI_BT_OK, true},
        {setup_wrapper_ones, FUXI_BT_NO_UART, FUXI_BT_STAGE_WRAPPER, FUXI_BT_OK, true},
        {setup_protocol, FUXI_BT_NO_UART, FUXI_BT_STAGE_ENGINE, FUXI_BT_OK, true},
        {setup_disabled, FUXI_BT_NO_UART, FUXI_BT_STAGE_ENGINE, FUXI_BT_OK, true},
        {setup_pin, FUXI_BT_IO, FUXI_BT_STAGE_PINS, FUXI_BT_OK, true},
        {setup_bt_en, FUXI_BT_IO, FUXI_BT_STAGE_BT_EN, FUXI_BT_OK, true},
    };
    for (unsigned index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
        struct soc soc;
        struct fuxi_bt_board board;
        struct fuxi_bt_board_io io;
        struct fuxi_bt_uart_ops ops;
        soc_init(&soc);
        cases[index].setup(&soc);
        for (unsigned reg = 0; reg < soc.count; reg++) {
            soc.regs[reg].original = soc.regs[reg].value;
        }
        io = soc_io(&soc);
        assert(fuxi_bt_board_power_on(&board, &io) == cases[index].status);
        assert(board.stage == cases[index].stage && board.error == cases[index].status);
        if (!cases[index].writes) {
            soc_assert_untouched(&soc);
        }
        if (cases[index].stage < FUXI_BT_STAGE_BT_EN) {
            assert(!soc.bt_en_high_writes && !soc_find(&soc, TLMM_CTL(81))->writes);
        }
        fuxi_bt_board_uart_ops(&board, &ops);
        assert(ops.acquire(ops.context, FUXI_BT_BOOT_BAUD) == FUXI_BT_NO_UART);
        assert(fuxi_bt_board_power_off(&board) == cases[index].cleanup);
        soc_assert_restored(&soc, 0);
        assert(!soc.uart.writes);
    }
    printf("PASS board refusals: %u stage gates (WLAN_EN, GPLL0, branch, RCG busy/stuck/readback/root-off, wrapper, SE protocol/disable, pinmux readback, BT_EN) each restore\n",
        (unsigned)(sizeof(cases) / sizeof(cases[0])));
}

static void test_guards(void)
{
    struct soc soc;
    struct fuxi_bt_board board;
    struct fuxi_bt_board_io io, broken;
    struct fuxi_bt_uart_ops ops;
    bool ready = true;
    size_t count = 0;
    uint8_t byte = 0;
    soc_init(&soc);
    io = soc_io(&soc);
    assert(fuxi_bt_board_wlan_ready(NULL, &ready) == FUXI_BT_ARGUMENT);
    assert(fuxi_bt_board_wlan_ready(&io, NULL) == FUXI_BT_ARGUMENT);
    assert(fuxi_bt_board_power_on(NULL, &io) == FUXI_BT_ARGUMENT);
    assert(fuxi_bt_board_power_on(&board, NULL) == FUXI_BT_ARGUMENT);
    broken = io;
    broken.write = NULL;
    assert(fuxi_bt_board_power_on(&board, &broken) == FUXI_BT_ARGUMENT);
    broken = io;
    broken.wait_us = NULL;
    assert(fuxi_bt_board_power_on(&board, &broken) == FUXI_BT_ARGUMENT);
    memset(&board, 0, sizeof(board));
    assert(fuxi_bt_board_power_off(&board) == FUXI_BT_ARGUMENT);
    assert(fuxi_bt_board_power_off(NULL) == FUXI_BT_ARGUMENT);
    assert(!strcmp(fuxi_bt_board_stage_name(FUXI_BT_STAGE_READY), "ready"));
    assert(!strcmp(fuxi_bt_board_stage_name((enum fuxi_bt_board_stage)99), "unknown"));
    fuxi_bt_board_uart_ops(NULL, &ops);
    assert(!ops.context && !ops.acquire);
    fuxi_bt_board_uart_ops(&board, NULL);
    soc_assert_untouched(&soc);
    assert(fuxi_bt_board_power_on(&board, &io) == FUXI_BT_OK);
    fuxi_bt_board_uart_ops(&board, &ops);
    assert(ops.release(ops.context) == FUXI_BT_OK);
    assert(ops.acquire(ops.context, 9600) == FUXI_BT_ARGUMENT);
    assert(!soc.uart.writes);
    assert(ops.acquire(ops.context, FUXI_BT_BOOT_BAUD) == FUXI_BT_OK);
    assert(board.fifo_used && board.fifo.state == FUXI_BT_FIFO_OPEN);
    assert(ops.acquire(ops.context, FUXI_BT_BOOT_BAUD) == FUXI_BT_BUSY);
    assert(ops.read(ops.context, &byte, 1, &count) == FUXI_BT_AGAIN && !count);
    assert(ops.now_us(ops.context) == soc.uart.clock);
    /* power_off 在 FIFO 還開著時先關 FIFO，再還原板級設定。 */
    assert(fuxi_bt_board_power_off(&board) == FUXI_BT_OK);
    assert(!board.fifo_used && board.fifo.restored);
    soc_assert_restored(&soc, 0);
    for (unsigned index = 0; index < sizeof(saved_regs) / sizeof(saved_regs[0]); index++) {
        assert(soc.uart.regs[saved_regs[index] / 4] == soc.uart.original[saved_regs[index] / 4]);
    }
    /* 模型本身的 helper 與共用設定。 */
    assert(model_io(&soc.uart).read == model_read && model_now(&soc.uart) == soc.uart.clock);
    assert(config.source_hz == FUXI_BT_SE_HZ && config.qup_version == board.qup_version);
    puts("PASS board guards: NULL/incomplete callbacks, stage names, ops before READY, baud check, double acquire, power_off closing an open FIFO");
}

static void test_no_reply(void)
{
    struct soc soc;
    struct fuxi_bt_board board;
    struct fuxi_bt_board_io io;
    struct fuxi_bt_uart_ops ops;
    struct fuxi_bt_result result;
    soc_init(&soc);
    soc.uart.cts_blocked = true; /* 控制器沒拉 CTS：TX 卡住 */
    io = soc_io(&soc);
    assert(fuxi_bt_board_power_on(&board, &io) == FUXI_BT_OK);
    fuxi_bt_board_uart_ops(&board, &ops);
    assert(fuxi_bt_probe_run(1, &ops, &result) != FUXI_BT_OK);
    assert(!result.version_valid && !board.fifo_used && result.cleanup == FUXI_BT_OK);
    assert(fuxi_bt_board_power_off(&board) == FUXI_BT_OK);
    soc_assert_restored(&soc, 0);
    printf("PASS board no-reply: CTS held -> probe %s cleanup %s after %llu us, board restored\n",
        fuxi_bt_status_name(result.status), fuxi_bt_status_name(result.cleanup),
        (unsigned long long)result.elapsed_us);
}

/* 每一次 MMIO 呼叫都注入一次失敗：power_on／probe 階段的失敗都要能完整還原，清理階段的失敗要回報。 */
static void test_faults(void)
{
    struct soc soc;
    struct fuxi_bt_board board;
    struct fuxi_bt_board_io io;
    struct fuxi_bt_uart_ops ops;
    struct fuxi_bt_result result;
    unsigned total, before_off, early = 0, cleanup_faults = 0;
    soc_init(&soc);
    io = soc_io(&soc);
    assert(fuxi_bt_board_power_on(&board, &io) == FUXI_BT_OK);
    fuxi_bt_board_uart_ops(&board, &ops);
    assert(fuxi_bt_probe_run(1, &ops, &result) == FUXI_BT_OK);
    before_off = soc.calls;
    assert(fuxi_bt_board_power_off(&board) == FUXI_BT_OK);
    total = soc.calls;
    for (unsigned fail = 1; fail <= total; fail++) {
        enum fuxi_bt_status status, cleanup;
        uint64_t failed_pa = 0;
        soc_init(&soc);
        soc.fail_at = fail;
        io = soc_io(&soc);
        status = fuxi_bt_board_power_on(&board, &io);
        if (status == FUXI_BT_OK) {
            fuxi_bt_board_uart_ops(&board, &ops);
            (void)fuxi_bt_probe_run(1, &ops, &result);
        }
        before_off = soc.calls;
        cleanup = fuxi_bt_board_power_off(&board);
        assert(soc.failed_call == fail);
        if (fail <= before_off) {
            early++;
            assert(cleanup == FUXI_BT_OK || board.fifo.io_fault || result.cleanup != FUXI_BT_OK ||
                board.fifo.state == FUXI_BT_FIFO_CLOSE_FAILED);
            soc_assert_restored(&soc, 0);
        } else {
            cleanup_faults++;
            assert(cleanup != FUXI_BT_OK);
            for (unsigned index = 0; index < soc.count; index++) {
                if (soc.regs[index].value != soc.regs[index].original) {
                    assert(!failed_pa);
                    failed_pa = soc.regs[index].pa;
                }
            }
            soc_assert_restored(&soc, failed_pa);
        }
        assert(!board.votes_changed && !board.rcg_changed && !board.pins_changed && !board.bt_en_changed);
    }
    printf("PASS board faults: %u MMIO fault sites (%u before restore fully undone, %u during restore reported)\n",
        total, early, cleanup_faults);
}

/*
 * fuxi,bt-baud：acquire(baud) 依速率表重設 UART SE 的 RCG（GPLL0_EVEN 300 MHz 經 M/N），GENI 除頻跟著變。
 * 期望值獨立手算（clk-rcg2：N 暫存器 ~(n-m)、D 暫存器 ~clamp(n, m, 2(n-m))，取低 16 位元）：
 * 921600＝14.7456 MHz 768/15625、2M＝32 MHz 8/75、3M＝48 MHz 4/25、3.2M＝51.2 MHz 64/375。
 */
static void test_baud_switch(void)
{
    static const struct { uint32_t baud, m, n, d; } rates[] = {
        {921600, 768, 0xc5f6, 0xc2f6},
        {2000000, 8, 0xffbc, 0xffb4},
        {3000000, 4, 0xffea, 0xffe6},
        {3200000, 64, 0xfec8, 0xfe88},
    };
    struct soc soc;
    struct fuxi_bt_board board;
    struct fuxi_bt_board_io io;
    struct fuxi_bt_uart_ops ops;
    struct fuxi_bt_result result;
    unsigned writes, total, before_off, early = 0, cleanup_faults = 0, switched = 0;
    soc_init(&soc);
    io = soc_io(&soc);
    assert(fuxi_bt_board_power_on(&board, &io) == FUXI_BT_OK);
    assert(board.rcg_hz == FUXI_BT_SE_HZ && board.rcg_switches == 0);
    fuxi_bt_board_uart_ops(&board, &ops);
    for (unsigned index = 0; index < sizeof(rates) / sizeof(rates[0]); index++) {
        assert(ops.acquire(ops.context, rates[index].baud) == FUXI_BT_OK);
        assert(soc_value(&soc, RCG(8)) == (0x10000u | rates[index].m));
        assert(soc_value(&soc, RCG(12)) == (0x20000u | rates[index].n));
        assert(soc_value(&soc, RCG(16)) == (0x30000u | rates[index].d));
        assert(soc_value(&soc, RCG(4)) == 0x102601u && board.rcg_cfg == 0x102601u);
        assert(soc.uart.regs[0x48 / 4] == 0x11 && soc.uart.regs[0x4c / 4] == 0x11);
        assert(board.baud == rates[index].baud && board.fifo.baud == rates[index].baud && board.fifo.divider == 1);
        assert(board.rcg_switches == index + 1 && board.error == FUXI_BT_OK);
        assert(ops.release(ops.context) == FUXI_BT_OK && !board.fifo_used && board.fifo.restored);
    }
    /* 同速率再取：RCG 不再寫 */
    writes = soc_find(&soc, RCG(8))->writes;
    assert(ops.acquire(ops.context, 3200000) == FUXI_BT_OK && board.rcg_switches == 4);
    assert(soc_find(&soc, RCG(8))->writes == writes);
    assert(ops.release(ops.context) == FUXI_BT_OK);
    /* 回 115200：模型控制器只在 7.3728 MHz 回應，probe 成功代表 RCG 已回開機值 */
    assert(fuxi_bt_probe_run(1, &ops, &result) == FUXI_BT_OK && result.version_valid);
    assert(board.rcg_switches == 5 && board.rcg_hz == FUXI_BT_SE_HZ && board.baud == FUXI_BT_BOOT_BAUD);
    assert(soc_value(&soc, RCG(8)) == 0x10180u && soc_value(&soc, RCG(12)) == 0x2c476u &&
           soc_value(&soc, RCG(16)) == 0x3c2f6u);
    /* 換速時 root off：回 OFF、rcg_hz 歸零、FIFO 沒開；狀況解除後 115200 能重新設好 */
    soc.rcg_root_off = true;
    assert(ops.acquire(ops.context, 3000000) == FUXI_BT_OFF);
    assert(!board.fifo_used && board.rcg_hz == 0 && board.rcg_changed && board.rcg_switches == 5);
    soc.rcg_root_off = false;
    assert(ops.acquire(ops.context, FUXI_BT_BOOT_BAUD) == FUXI_BT_OK);
    assert(board.rcg_hz == FUXI_BT_SE_HZ && board.rcg_switches == 6 && soc.uart.regs[0x48 / 4] == 0x41);
    assert(ops.release(ops.context) == FUXI_BT_OK);
    /* 新速率開著時 power_off：先關 FIFO，RCG 與其他板級設定回到開機前 */
    assert(ops.acquire(ops.context, 3200000) == FUXI_BT_OK && board.fifo_used);
    assert(fuxi_bt_board_power_off(&board) == FUXI_BT_OK);
    assert(!board.fifo_used && board.rcg_hz == 0 && !board.rcg_changed);
    soc_assert_restored(&soc, 0);
    for (unsigned index = 0; index < sizeof(saved_regs) / sizeof(saved_regs[0]); index++) {
        assert(soc.uart.regs[saved_regs[index] / 4] == soc.uart.original[saved_regs[index] / 4]);
    }

    /* 換速流程（power_on → 3.2M 取還 → probe 回 115200 → power_off）每一次 MMIO 都注入一次失敗 */
    soc_init(&soc);
    io = soc_io(&soc);
    assert(fuxi_bt_board_power_on(&board, &io) == FUXI_BT_OK);
    fuxi_bt_board_uart_ops(&board, &ops);
    assert(ops.acquire(ops.context, 3200000) == FUXI_BT_OK && ops.release(ops.context) == FUXI_BT_OK);
    assert(fuxi_bt_probe_run(1, &ops, &result) == FUXI_BT_OK);
    assert(fuxi_bt_board_power_off(&board) == FUXI_BT_OK);
    total = soc.calls;
    for (unsigned fail = 1; fail <= total; fail++) {
        enum fuxi_bt_status cleanup;
        uint64_t failed_pa = 0;
        soc_init(&soc);
        soc.fail_at = fail;
        io = soc_io(&soc);
        if (fuxi_bt_board_power_on(&board, &io) == FUXI_BT_OK) {
            fuxi_bt_board_uart_ops(&board, &ops);
            if (ops.acquire(ops.context, 3200000) == FUXI_BT_OK) {
                switched++;
            }
            (void)ops.release(ops.context);
            (void)fuxi_bt_probe_run(1, &ops, &result);
        }
        before_off = soc.calls;
        cleanup = fuxi_bt_board_power_off(&board);
        assert(soc.failed_call == fail);
        if (fail <= before_off) {
            early++;
            soc_assert_restored(&soc, 0);
        } else {
            cleanup_faults++;
            assert(cleanup != FUXI_BT_OK);
            for (unsigned index = 0; index < soc.count; index++) {
                if (soc.regs[index].value != soc.regs[index].original) {
                    assert(!failed_pa);
                    failed_pa = soc.regs[index].pa;
                }
            }
            soc_assert_restored(&soc, failed_pa);
        }
        assert(!board.votes_changed && !board.rcg_changed && !board.pins_changed && !board.bt_en_changed);
        assert(!board.fifo_used && board.rcg_hz == 0);
    }
    printf("PASS board baud switch: 4 rates M/N/D/CFG + GENI 0x11, same-rate reuse, back to 115200 probe, "
           "root-off recovery, power_off at 3.2M; %u fault sites (%u before restore, %u during restore, %u switched)\n",
        total, early, cleanup_faults, switched);
}

int main(void)
{
    test_success();
    test_vote_already_present();
    test_refusals();
    test_guards();
    test_no_reply();
    test_faults();
    test_baud_switch();
    puts("PASS production WCN7850 board glue: success path, refusals, guards, no-reply, exhaustive MMIO faults and baud switching");
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--cc", nargs="+")
    args = parser.parse_args()
    sys.dont_write_bytecode = True
    specification = importlib.util.spec_from_file_location(
        "fuxi_bluetooth_host_compiler", ROOT / "tools" / "test-fuxi-bluetooth.py")
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    fifo_specification = importlib.util.spec_from_file_location(
        "fuxi_bt_geni_fifo_model", ROOT / "tools" / "test-fuxi-bt-geni-fifo.py")
    fifo = importlib.util.module_from_spec(fifo_specification)
    fifo_specification.loader.exec_module(fifo)
    model = fifo.HARNESS[:fifo.HARNESS.index("static void open_ok(")]
    compiler, environment = module.compiler_configuration(args.cc)
    msvc = Path(compiler[0]).name.lower() in ("cl", "cl.exe")
    if msvc and args.sanitize:
        raise SystemExit("--sanitize requires Clang or GCC with ASan and UBSan.")
    sources = [str(SOURCE / name) for name in ("fuxi_bt_board.c", "fuxi_bt_geni_fifo.c", "fuxi_bt.c")]
    with tempfile.TemporaryDirectory(prefix="fuxi-bt-board-") as temporary:
        directory = Path(temporary)
        harness = directory / "board-model.c"
        harness.write_text(model + BOARD, encoding="utf-8")
        executable = directory / ("board-model.exe" if os.name == "nt" else "board-model")
        if msvc:
            command = compiler + [
                "/nologo", "/std:c11", "/W4", "/WX", "/Od", "/D_CRT_SECURE_NO_WARNINGS",
                f"/I{SOURCE}", *sources, str(harness), f"/Fe{executable}"
            ]
        else:
            command = compiler + [
                "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                "-I", str(SOURCE), *sources, str(harness), "-o", str(executable)
            ]
            if args.sanitize:
                command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, cwd=directory, env=environment, check=True)
        subprocess.run([str(executable)], cwd=directory, env=environment, check=True, timeout=120)
        print(f"Compiler {compiler[0]}")
        print(f"Sanitizers {'ASan/UBSan' if args.sanitize else 'disabled'}")
        print("GCC, TLMM, QUP wrapper, GENI FIFO traffic and the controller reply are emulated.")


if __name__ == "__main__":
    main()
