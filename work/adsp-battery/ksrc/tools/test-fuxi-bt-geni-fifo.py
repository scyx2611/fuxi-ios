#!/usr/bin/env python3
"""Compile production FIFO C and run a GENI register/FIFO emulator."""

import argparse
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "xnu" / "fuxi-bluetooth"

HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "fuxi_bt_geni_fifo.h"

static const unsigned saved_regs[] = {
    0xe1c, 0x614, 0x644, 0x28, 0x24, 0x48, 0x4c, 0x7c,
    0x22c, 0x254, 0x25c, 0x260, 0x264, 0x268, 0x26c,
    0x280, 0x284, 0x288, 0x28c, 0x294, 0x2ac, 0x2a4,
    0x2a8, 0x80c, 0x810, 0x814, 0x270
};

struct model {
    uint32_t regs[0x2200 / 4];
    uint32_t original[0x2200 / 4];
    uint32_t tx[256];
    unsigned tx_head, tx_count, tx_depth;
    uint32_t rx[256];
    unsigned rx_head, rx_count, rx_depth, rx_valid;
    uint8_t wire[4096];
    unsigned wire_size, command_start, command_bytes;
    unsigned calls, writes, fifo_writes, fifo_reads, commands;
    unsigned fail_at;
    unsigned advance_at;
    uint64_t advance_us;
    unsigned master_cancel, secondary_cancel, master_abort, secondary_abort;
    unsigned restore_corrupt;
    uint32_t rx_status_override;
    bool override_rx, override_tx;
    uint32_t tx_status_override;
    bool cancel_ack, abort_ack, clear_tx, keep_active, control_stuck;
    bool delay_secondary, delay_master, hold_done, cts_blocked, fail_after_write;
    bool master_pending;
    uint64_t clock;
};

static struct fuxi_bt_geni_fifo_config config = {1, true, 0x30090000, 7372800, 0};

static void model_init(struct model *model)
{
    memset(model, 0, sizeof(*model));
    model->tx_depth = model->rx_depth = 16;
    model->regs[0xe24 / 4] = (32u << 24) | (16u << 16);
    model->regs[0xe28 / 4] = (32u << 24) | (16u << 16);
    model->regs[0x68 / 4] = 0x204;
    model->regs[0x240 / 4] = 0x4640;
    for (unsigned index = 0; index < sizeof(saved_regs) / sizeof(saved_regs[0]); index++) {
        model->regs[saved_regs[index] / 4] = 0x5000u + index;
    }
    memcpy(model->original, model->regs, sizeof(model->regs));
    model->clock = 100;
    model->cancel_ack = model->abort_ack = model->clear_tx = true;
}

static enum fuxi_bt_status model_read(void *context, uint32_t reg, uint32_t *value)
{
    struct model *model = context;
    assert(!(reg & 3) && reg < sizeof(model->regs));
    model->calls++;
    if (model->advance_at == model->calls) model->clock += model->advance_us;
    if (model->fail_at == model->calls) {
        return FUXI_BT_IO;
    }
    if (reg == 0x800) {
        *value = model->override_tx ? model->tx_status_override : model->tx_count;
    } else if (reg == 0x804) {
        *value = model->override_rx ? model->rx_status_override : model->rx_count;
        if (!model->override_rx && model->rx_count) {
            *value |= (1u << 31) | (model->rx_valid << 28);
        }
    } else if (reg == 0x780) {
        assert(model->rx_count);
        *value = model->rx[model->rx_head++ % 256];
        model->rx_count--;
        model->fifo_reads++;
    } else {
        *value = model->regs[reg / 4];
    }
    return FUXI_BT_OK;
}

static bool allowed_write(uint32_t reg)
{
    for (unsigned index = 0; index < sizeof(saved_regs) / sizeof(saved_regs[0]); index++) {
        if (saved_regs[index] == reg) {
            return true;
        }
    }
    return reg == 0x600 || reg == 0x604 || reg == 0x618 ||
        reg == 0x630 || reg == 0x634 || reg == 0x648 || reg == 0x700;
}

static enum fuxi_bt_status model_write(void *context, uint32_t reg, uint32_t value)
{
    struct model *model = context;
    bool failure;
    assert(allowed_write(reg));
    assert(reg != 0x20 && reg != 0x64 && reg != 0x258 && reg != 0xc58 && reg != 0xd58);
    model->calls++;
    if (model->advance_at == model->calls) model->clock += model->advance_us;
    failure = model->fail_at == model->calls;
    if (failure && !model->fail_after_write) {
        return FUXI_BT_IO;
    }
    model->writes++;
    if (reg == 0x618 || reg == 0x648) {
        model->regs[(reg - 8) / 4] &= ~value;
    } else if (reg == 0x630) {
        assert(value == (1u << 27));
        model->regs[reg / 4] = value;
        if (!model->delay_secondary) {
            model->regs[0x40 / 4] |= 1u << 12;
        }
    } else if (reg == 0x600) {
        assert(value == (1u << 27));
        assert(!(model->regs[0x40 / 4] & 1u));
        assert(!model->tx_count && !model->master_pending);
        model->regs[reg / 4] = value;
        model->command_start = model->wire_size;
        model->command_bytes = 0;
        model->commands++;
        model->master_pending = model->delay_master;
        if (!model->delay_master) {
            model->regs[0x40 / 4] |= 1u;
        }
    } else if (reg == 0x700) {
        assert(model->tx_count < model->tx_depth);
        model->tx[(model->tx_head + model->tx_count++) % 256] = value;
        model->fifo_writes++;
    } else if (reg == 0x604 || reg == 0x634) {
        const bool master = reg == 0x604;
        const bool aborting = value == 2;
        const uint32_t active = master ? 1u : 1u << 12;
        const uint32_t irq = master ? 0x610 : 0x640;
        assert(value == 2 || value == 4);
        if (master && aborting) model->master_abort++;
        if (master && !aborting) model->master_cancel++;
        if (!master && aborting) model->secondary_abort++;
        if (!master && !aborting) model->secondary_cancel++;
        model->regs[reg / 4] = value;
        if (aborting ? model->abort_ack : model->cancel_ack) {
            if (!model->control_stuck) model->regs[reg / 4] = 0;
            if (!model->keep_active) model->regs[0x40 / 4] &= ~active;
            model->regs[irq / 4] |= aborting ? 1u << 5 : 1u << 4;
            if (master) {
                model->master_pending = false;
                if (model->clear_tx) model->tx_count = 0;
            }
        }
    } else {
        model->regs[reg / 4] = value;
        if (model->restore_corrupt == reg && value == model->original[reg / 4]) {
            model->regs[reg / 4] ^= 1u;
        }
    }
    return failure ? FUXI_BT_IO : FUXI_BT_OK;
}

static uint64_t model_now(void *context)
{
    return ((struct model *)context)->clock;
}

static struct fuxi_bt_geni_fifo_io model_io(struct model *model)
{
    struct fuxi_bt_geni_fifo_io io = {model, model_read, model_write, model_now};
    return io;
}

static void model_complete_tx(struct model *model)
{
    assert(model->command_bytes == model->regs[0x270 / 4]);
    assert(!model->tx_count);
    model->regs[0x40 / 4] &= ~1u;
    model->regs[0x610 / 4] |= 1u;
}

static void model_tx_step(struct model *model)
{
    uint32_t word;
    if (model->cts_blocked) return;
    if (model->master_pending) {
        model->master_pending = false;
        model->regs[0x40 / 4] |= 1u;
    }
    if (!model->tx_count || !(model->regs[0x40 / 4] & 1u)) return;
    word = model->tx[model->tx_head++ % 256];
    model->tx_count--;
    for (unsigned byte = 0; byte < 4; byte++) {
        const uint32_t packing = model->regs[(byte < 2 ? 0x260 : 0x264) / 4];
        const uint32_t vector = (packing >> (10 * (byte % 2))) & 0x3ffu;
        const unsigned start = (vector >> 5) & 31u;
        assert(((vector >> 1) & 7u) == 7 && !(vector & 16u));
        assert((vector & 1u) == (byte == 3 ? 1u : 0u));
        if (model->command_bytes < model->regs[0x270 / 4]) {
            assert(model->wire_size < sizeof(model->wire));
            model->wire[model->wire_size++] = (uint8_t)(word >> start);
            model->command_bytes++;
        }
    }
    if (model->command_bytes == model->regs[0x270 / 4] && !model->hold_done) {
        model_complete_tx(model);
    }
}

static void model_rx_load(struct model *model, const uint8_t *bytes, unsigned size)
{
    assert(!model->rx_count);
    assert(size <= model->rx_depth * 4);
    model->rx_head = 0;
    model->rx_count = (size + 3) / 4;
    model->rx_valid = size % 4;
    for (unsigned index = 0; index < model->rx_count; index++) {
        uint32_t word = 0;
        for (unsigned byte = 0; byte < 4; byte++) {
            const unsigned offset = index * 4 + byte;
            const uint8_t value = offset < size ? bytes[offset] : 0xa5;
            word |= (uint32_t)value << (8 * byte);
        }
        model->rx[index] = word;
    }
    model->regs[0x640 / 4] |= (1u << 26) | (1u << 27);
}

static void open_ok(struct model *model, struct fuxi_bt_geni_fifo *fifo)
{
    struct fuxi_bt_geni_fifo_io io = model_io(model);
    memset(fifo, 0, sizeof(*fifo));
    assert(fuxi_bt_geni_fifo_open(fifo, &io, &config) == FUXI_BT_OK);
    assert(fifo->state == FUXI_BT_FIFO_OPEN && fifo->owns_secondary);
    assert(model->regs[0x48 / 4] == 0x41 && model->regs[0x4c / 4] == 0x41);
    assert(fifo->baud == FUXI_BT_BOOT_BAUD && fifo->divider == 4);
    assert(model->regs[0x260 / 4] == 0x4380e && model->regs[0x264 / 4] == 0xc3e0e);
    assert(model->regs[0x284 / 4] == 0x4380e && model->regs[0x288 / 4] == 0xc3e0e);
    assert(model->regs[0x25c / 4] == (model->original[0x25c / 4] & ~3u));
    assert(model->regs[0x280 / 4] == (model->original[0x280 / 4] & ~(1u << 4)));
    assert(model->regs[0x2ac / 4] == 0);
    assert(model->regs[0x814 / 4] == model->rx_depth - 2);
}

static enum fuxi_bt_status close_until_done(struct model *model,
    struct fuxi_bt_geni_fifo *fifo, unsigned tick)
{
    enum fuxi_bt_status result = FUXI_BT_AGAIN;
    for (unsigned attempt = 0; attempt <= FUXI_BT_FIFO_MAX_POLLS; attempt++) {
        result = fuxi_bt_geni_fifo_close_poll(fifo);
        if (result != FUXI_BT_AGAIN) return result;
        model->clock += tick;
    }
    assert(false);
    return result;
}

static void closed_ok(struct model *model, struct fuxi_bt_geni_fifo *fifo)
{
    unsigned calls;
    assert(close_until_done(model, fifo, 100) == FUXI_BT_OK);
    assert(fifo->quiescent && fifo->restored && !fifo->modified);
    assert(!(model->regs[0x40 / 4] & 0x1001u));
    for (unsigned index = 0; index < sizeof(saved_regs) / sizeof(saved_regs[0]); index++) {
        assert(model->regs[saved_regs[index] / 4] == model->original[saved_regs[index] / 4]);
    }
    assert(model->regs[0x240 / 4] == 0x4640);
    calls = model->calls;
    assert(fuxi_bt_geni_fifo_close_poll(fifo) == FUXI_BT_OK);
    assert(model->calls == calls);
}

static void test_guards(void)
{
    struct model model;
    struct fuxi_bt_geni_fifo fifo = {0};
    struct fuxi_bt_geni_fifo_io io;
    struct fuxi_bt_geni_fifo_config candidate = config;
    model_init(&model);
    io = model_io(&model);
    candidate.opt_in = 0;
    assert(fuxi_bt_geni_fifo_open(&fifo, NULL, &candidate) == FUXI_BT_OFF);
    assert(!model.calls && !model.writes);
    candidate = config;
    candidate.resources_owned = false;
    assert(fuxi_bt_geni_fifo_open(&fifo, &io, &candidate) == FUXI_BT_ARGUMENT);
    candidate = config;
    candidate.source_hz = 7372801;
    assert(fuxi_bt_geni_fifo_open(&fifo, &io, &candidate) == FUXI_BT_ARGUMENT);
    assert(!model.calls);
    for (unsigned guard = 0; guard < 13; guard++) {
        model_init(&model);
        memset(&fifo, 0, sizeof(fifo));
        io = model_io(&model);
        switch (guard) {
            case 0: model.regs[0x64 / 4] = 1; break;
            case 1: model.regs[0x258 / 4] = 1; break;
            case 2: model.regs[0xe18 / 4] = 1; break;
            case 3: model.regs[0x68 / 4] = 0x304; break;
            case 4: model.regs[0x40 / 4] = 1; break;
            case 5: model.regs[0x40 / 4] = 1u << 12; break;
            case 6: model.regs[0x604 / 4] = 4; break;
            case 7: model.regs[0xe24 / 4] = 16u << 24; break;
            case 8: model.regs[0xe28 / 4] = (32u << 24) | (2u << 16); break;
            case 9: model.override_tx = true; model.tx_status_override = 1; break;
            case 10: model.override_rx = true; model.rx_status_override = 1; break;
            case 11: model.regs[0x604 / 4] = 1; break;
            case 12: model.regs[0x634 / 4] = 1; break;
        }
        assert(fuxi_bt_geni_fifo_open(&fifo, &io, &config) != FUXI_BT_OK);
        assert(!model.writes);
        if (!guard) assert(model.calls == 1 && fifo.interface_disable == 1);
        assert(fuxi_bt_geni_fifo_close_poll(&fifo) == FUXI_BT_OK);
        assert(!model.writes && !fifo.quiescent);
    }
    model_init(&model);
    open_ok(&model, &fifo);
    io = model_io(&model);
    assert(fuxi_bt_geni_fifo_open(&fifo, &io, &config) == FUXI_BT_BUSY);
    closed_ok(&model, &fifo);
    model_init(&model);
    memset(&fifo, 0, sizeof(fifo));
    candidate = config;
    candidate.qup_version = 0x20040000;
    io = model_io(&model);
    assert(fuxi_bt_geni_fifo_open(&fifo, &io, &candidate) == FUXI_BT_OK);
    assert(model.regs[0x48 / 4] == 0x21);
    closed_ok(&model, &fifo);

    /* fuxi,bt-baud：速率表的時脈來源對應的 GENI 除頻（source／(baud×16)） */
    {
        static const struct { uint32_t source, baud, reg, divider; } rates[] = {
            {7372800, 115200, 0x41, 4}, {14745600, 115200, 0x81, 8}, {14745600, 921600, 0x11, 1},
            {32000000, 2000000, 0x11, 1}, {48000000, 3000000, 0x11, 1}, {51200000, 3200000, 0x11, 1},
        };
        static const struct { uint32_t qup, source, baud; } bad[] = {
            {0x30090000, 7372800, 921600},    /* 7.3728 MHz 不夠 921600×16 */
            {0x30090000, 51200000, 3000000},  /* 除不盡 */
            {0x30090000, 102400000, 6400000}, /* 超過 FUXI_BT_MAX_BAUD */
            {0x20040000, 51200000, 3200000},  /* 舊 QUP 取樣 32：除頻 0.5 */
        };
        for (unsigned index = 0; index < sizeof(rates) / sizeof(rates[0]); index++) {
            model_init(&model);
            memset(&fifo, 0, sizeof(fifo));
            candidate = config;
            candidate.source_hz = rates[index].source;
            candidate.baud = rates[index].baud;
            io = model_io(&model);
            assert(fuxi_bt_geni_fifo_open(&fifo, &io, &candidate) == FUXI_BT_OK);
            assert(model.regs[0x48 / 4] == rates[index].reg && model.regs[0x4c / 4] == rates[index].reg);
            assert(fifo.baud == rates[index].baud && fifo.divider == rates[index].divider);
            closed_ok(&model, &fifo);
        }
        for (unsigned index = 0; index < sizeof(bad) / sizeof(bad[0]); index++) {
            model_init(&model);
            memset(&fifo, 0, sizeof(fifo));
            candidate = config;
            candidate.qup_version = bad[index].qup;
            candidate.source_hz = bad[index].source;
            candidate.baud = bad[index].baud;
            io = model_io(&model);
            assert(fuxi_bt_geni_fifo_open(&fifo, &io, &candidate) == FUXI_BT_ARGUMENT);
            assert(!model.calls && !model.writes);
        }
    }
}

static void test_tx(void)
{
    static const unsigned depths[] = {1, 2, 16, 63, 255};
    struct model model;
    struct fuxi_bt_geni_fifo fifo;
    uint8_t storage[1026];
    for (unsigned depth_index = 0; depth_index < 5; depth_index++) {
        for (unsigned length = 1; length <= 1024; length++) {
            size_t total = 0;
            model_init(&model);
            model.tx_depth = depths[depth_index];
            model.regs[0xe24 / 4] = (32u << 24) | (model.tx_depth << 16);
            config.qup_version = 0x300a0000;
            open_ok(&model, &fifo);
            for (unsigned index = 0; index < length; index++) storage[index + 1] = (uint8_t)(index * 37 + length);
            for (unsigned attempt = 0; attempt < 2048; attempt++) {
                size_t accepted = 0;
                if (total != length) {
                    const enum fuxi_bt_status result = fuxi_bt_geni_fifo_write(
                        &fifo, storage + 1 + total, length - total, &accepted);
                    assert(result == FUXI_BT_OK || result == FUXI_BT_AGAIN);
                    assert(accepted <= FUXI_BT_FIFO_WORD_BUDGET * 4);
                    total += accepted;
                }
                model_tx_step(&model);
                model.clock += 10;
                assert(fuxi_bt_geni_fifo_poll(&fifo) == FUXI_BT_OK);
                if (total == length && !fifo.owns_master) break;
            }
            assert(total == length && fifo.tx_completed == length);
            assert(fifo.tx_accepted == length && model.commands == 1);
            assert(model.wire_size == length && !memcmp(model.wire, storage + 1, length));
            closed_ok(&model, &fifo);
        }
    }
    config.qup_version = 0x30090000;
    model_init(&model);
    model.tx_depth = 1;
    model.regs[0xe24 / 4] = (32u << 24) | (1u << 16);
    open_ok(&model, &fifo);
    {
        const uint8_t command[] = {1, 0, 0xfc, 1, 0x19};
        size_t accepted = 0;
        assert(fuxi_bt_geni_fifo_write(&fifo, command, 5, &accepted) == FUXI_BT_OK && accepted == 4);
        assert(fuxi_bt_geni_fifo_write(&fifo, command + 4, 1, &accepted) == FUXI_BT_AGAIN && !accepted);
        assert(fuxi_bt_geni_fifo_write(&fifo, command, 2, &accepted) == FUXI_BT_ARGUMENT);
        model_tx_step(&model);
        model.hold_done = true;
        assert(fuxi_bt_geni_fifo_write(&fifo, command + 4, 1, &accepted) == FUXI_BT_OK && accepted == 1);
        model_tx_step(&model);
        assert(fuxi_bt_geni_fifo_poll(&fifo) == FUXI_BT_OK && fifo.tx_completed == 0);
        model_complete_tx(&model);
        assert(fuxi_bt_geni_fifo_poll(&fifo) == FUXI_BT_OK && fifo.tx_completed == 5);
        assert(model.wire_size == 5 && !memcmp(model.wire, command, 5));
        model.hold_done = false;
        assert(fuxi_bt_geni_fifo_write(&fifo, command, 5, &accepted) == FUXI_BT_OK);
        while (model.tx_count) model_tx_step(&model);
        if (fifo.tx_remaining) {
            assert(fuxi_bt_geni_fifo_write(&fifo, command + 4, 1, &accepted) == FUXI_BT_OK);
            model_tx_step(&model);
        }
        assert(fuxi_bt_geni_fifo_poll(&fifo) == FUXI_BT_OK && fifo.tx_completed == 10);
    }
    closed_ok(&model, &fifo);
}

static void test_rx(void)
{
    static const unsigned capacities[] = {1, 2, 3, 4, 5, 7, 64, 512};
    struct model model;
    struct fuxi_bt_geni_fifo fifo;
    uint8_t input[1020], output[1024];
    for (unsigned length = 1; length <= 257; length++) {
        for (unsigned capacity_index = 0; capacity_index < 8; capacity_index++) {
            size_t total = 0;
            model_init(&model);
            model.rx_depth = 255;
            model.regs[0xe28 / 4] = (32u << 24) | (255u << 16);
            config.qup_version = 0x300a0000;
            open_ok(&model, &fifo);
            for (unsigned index = 0; index < length; index++) input[index] = (uint8_t)(index * 13 + length);
            model_rx_load(&model, input, length);
            memset(output, 0xcc, sizeof(output));
            for (unsigned attempt = 0; attempt < 1024 && total < length; attempt++) {
                size_t received = 0;
                const unsigned before = model.fifo_reads;
                assert(fuxi_bt_geni_fifo_read(&fifo, output + total, capacities[capacity_index], &received) == FUXI_BT_OK);
                assert(received <= capacities[capacity_index]);
                assert(model.fifo_reads - before <= FUXI_BT_FIFO_WORD_BUDGET);
                total += received;
            }
            assert(total == length && !memcmp(input, output, length) && output[length] == 0xcc);
            assert(fifo.rx_delivered == length);
            {
                size_t received;
                assert(fuxi_bt_geni_fifo_read(&fifo, output, 1, &received) == FUXI_BT_AGAIN && !received);
            }
            closed_ok(&model, &fifo);
        }
    }
    config.qup_version = 0x30090000;
    for (unsigned valid = 0; valid <= 7; valid++) {
        size_t received;
        model_init(&model);
        open_ok(&model, &fifo);
        model_rx_load(&model, (uint8_t[]){1, 2, 3, 4}, 4);
        model.rx_valid = valid;
        if (valid <= 4) {
            assert(fuxi_bt_geni_fifo_read(&fifo, output, 8, &received) == FUXI_BT_OK);
            assert(received == (valid ? valid : 4));
        } else {
            assert(fuxi_bt_geni_fifo_read(&fifo, output, 8, &received) == FUXI_BT_PROTOCOL);
            assert(!received && !model.fifo_reads);
            model.rx_valid = 4;
        }
        closed_ok(&model, &fifo);
    }
    model_init(&model);
    open_ok(&model, &fifo);
    model.override_rx = true;
    model.rx_status_override = 17;
    {
        size_t received;
        assert(fuxi_bt_geni_fifo_read(&fifo, output, 8, &received) == FUXI_BT_PROTOCOL && !received);
    }
    model.override_rx = false;
    closed_ok(&model, &fifo);
}

static void test_errors_and_deadlines(void)
{
    struct model model;
    struct fuxi_bt_geni_fifo fifo;
    struct fuxi_bt_geni_fifo_io io;
    uint8_t bytes[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    for (unsigned kind = 0; kind < 2; kind++) {
        const uint32_t mask = kind ? (0xeu | (15u << 9) | (3u << 24)) : (0xeu | (3u << 24) | (3u << 28));
        for (unsigned bit = 0; bit < 32; bit++) {
            size_t received;
            if (!(mask & (1u << bit))) continue;
            model_init(&model);
            open_ok(&model, &fifo);
            model_rx_load(&model, bytes, 8);
            model.regs[(kind ? 0x640 : 0x610) / 4] |= 1u << bit;
            assert(fuxi_bt_geni_fifo_read(&fifo, bytes, 8, &received) == FUXI_BT_IO);
            assert(!received && !model.fifo_reads);
            closed_ok(&model, &fifo);
            assert(fifo.error == FUXI_BT_IO);
        }
    }
    model_init(&model);
    model.delay_secondary = true;
    memset(&fifo, 0, sizeof(fifo));
    io = model_io(&model);
    assert(fuxi_bt_geni_fifo_open(&fifo, &io, &config) == FUXI_BT_AGAIN);
    model.regs[0x40 / 4] |= 1u << 12;
    assert(fuxi_bt_geni_fifo_poll(&fifo) == FUXI_BT_OK);
    closed_ok(&model, &fifo);
    model_init(&model);
    model.delay_secondary = true;
    memset(&fifo, 0, sizeof(fifo));
    io = model_io(&model);
    assert(fuxi_bt_geni_fifo_open(&fifo, &io, &config) == FUXI_BT_AGAIN);
    model.clock += 20000;
    assert(fuxi_bt_geni_fifo_poll(&fifo) == FUXI_BT_TIMEOUT);
    assert(close_until_done(&model, &fifo, 100) == FUXI_BT_TIMEOUT);
    assert(!fifo.quiescent && !fifo.restored && fifo.modified);
    model_init(&model);
    open_ok(&model, &fifo);
    model.delay_master = true;
    {
        size_t accepted;
        assert(fuxi_bt_geni_fifo_write(&fifo, bytes, 8, &accepted) == FUXI_BT_OK && accepted == 8);
        assert(fuxi_bt_geni_fifo_poll(&fifo) == FUXI_BT_AGAIN);
        while (model.tx_count) model_tx_step(&model);
        assert(fuxi_bt_geni_fifo_poll(&fifo) == FUXI_BT_OK && fifo.tx_completed == 8);
    }
    closed_ok(&model, &fifo);
    model_init(&model);
    open_ok(&model, &fifo);
    {
        size_t accepted;
        assert(fuxi_bt_geni_fifo_write(&fifo, bytes, 8, &accepted) == FUXI_BT_OK);
        model.cts_blocked = true;
        model.clock += FUXI_BT_FIFO_TX_TIMEOUT_US;
        assert(fuxi_bt_geni_fifo_poll(&fifo) == FUXI_BT_TIMEOUT);
        assert(fifo.tx_accepted == 8 && fifo.tx_completed == 0);
    }
    closed_ok(&model, &fifo);
    model_init(&model);
    open_ok(&model, &fifo);
    {
        size_t accepted;
        assert(fuxi_bt_geni_fifo_write(&fifo, bytes, 8, &accepted) == FUXI_BT_OK);
        for (unsigned attempt = 1; attempt < FUXI_BT_FIFO_MAX_POLLS; attempt++) {
            assert(fuxi_bt_geni_fifo_poll(&fifo) == FUXI_BT_OK);
        }
        assert(fuxi_bt_geni_fifo_poll(&fifo) == FUXI_BT_TIMEOUT);
        assert(fifo.tx_polls == FUXI_BT_FIFO_MAX_POLLS);
    }
    closed_ok(&model, &fifo);
    model_init(&model);
    open_ok(&model, &fifo);
    model.regs[0xe24 / 4] |= 0xc0u << 16;
    closed_ok(&model, &fifo);
    {
        struct fuxi_bt_geni_fifo_io masked_io = model_io(&model);
        assert(fuxi_bt_geni_fifo_open(&fifo, &masked_io, &config) == FUXI_BT_OK);
        assert(fifo.tx_depth == 16);
    }
    closed_ok(&model, &fifo);
    model_init(&model);
    open_ok(&model, &fifo);
    model.clock--;
    assert(fuxi_bt_geni_fifo_poll(&fifo) == FUXI_BT_IO);
    assert(close_until_done(&model, &fifo, 100) == FUXI_BT_IO);
}

static void test_cleanup(void)
{
    struct model model;
    struct fuxi_bt_geni_fifo fifo;
    uint8_t bytes[1020];
    memset(bytes, 0xab, sizeof(bytes));
    model_init(&model);
    open_ok(&model, &fifo);
    model.cancel_ack = false;
    assert(close_until_done(&model, &fifo, 1000) == FUXI_BT_OK);
    assert(model.secondary_cancel == 1 && model.secondary_abort == 1);
    assert(fifo.quiescent && fifo.restored);
    model_init(&model);
    open_ok(&model, &fifo);
    model.cancel_ack = model.abort_ack = false;
    assert(close_until_done(&model, &fifo, 1000) == FUXI_BT_TIMEOUT);
    assert(!fifo.quiescent && !fifo.restored && fifo.modified);
    model_init(&model);
    open_ok(&model, &fifo);
    model.keep_active = true;
    assert(close_until_done(&model, &fifo, 1000) == FUXI_BT_TIMEOUT);
    assert(!fifo.quiescent && model.secondary_abort == 1);
    model_init(&model);
    open_ok(&model, &fifo);
    model.control_stuck = true;
    assert(close_until_done(&model, &fifo, 1000) == FUXI_BT_TIMEOUT);
    assert(!fifo.restored);
    model_init(&model);
    open_ok(&model, &fifo);
    model.cancel_ack = false;
    assert(close_until_done(&model, &fifo, 0) == FUXI_BT_TIMEOUT);
    assert(fifo.stop_polls == FUXI_BT_FIFO_MAX_POLLS);
    model_init(&model);
    model.delay_secondary = true;
    memset(&fifo, 0, sizeof(fifo));
    {
        struct fuxi_bt_geni_fifo_io io = model_io(&model);
        assert(fuxi_bt_geni_fifo_open(&fifo, &io, &config) == FUXI_BT_AGAIN);
    }
    assert(fuxi_bt_geni_fifo_close_poll(&fifo) == FUXI_BT_AGAIN);
    assert(fuxi_bt_geni_fifo_close_poll(&fifo) == FUXI_BT_AGAIN);
    assert(!fifo.quiescent && !fifo.restored && !model.secondary_cancel);
    model.regs[0x40 / 4] |= 1u << 12;
    assert(close_until_done(&model, &fifo, 1000) == FUXI_BT_OK);
    assert(model.secondary_abort == 1 && fifo.restored);
    model_init(&model);
    open_ok(&model, &fifo);
    model.delay_master = true;
    {
        size_t accepted;
        assert(fuxi_bt_geni_fifo_write(&fifo, bytes, 1, &accepted) == FUXI_BT_OK);
    }
    model.tx_count = 0;
    assert(close_until_done(&model, &fifo, 1000) == FUXI_BT_TIMEOUT);
    assert(model.master_pending && !fifo.quiescent && !fifo.restored && fifo.modified);
    model_init(&model);
    open_ok(&model, &fifo);
    model.clear_tx = false;
    {
        size_t accepted;
        assert(fuxi_bt_geni_fifo_write(&fifo, bytes, 8, &accepted) == FUXI_BT_OK);
    }
    assert(close_until_done(&model, &fifo, 100) == FUXI_BT_CLEANUP);
    assert(model.tx_count == 2 && !fifo.restored && model.commands == 1);
    model_init(&model);
    model.rx_depth = 255;
    model.regs[0xe28 / 4] = (32u << 24) | (255u << 16);
    config.qup_version = 0x300a0000;
    open_ok(&model, &fifo);
    model_rx_load(&model, bytes, 1020);
    closed_ok(&model, &fifo);
    assert(fifo.rx_discarded == 1020);
    config.qup_version = 0x30090000;
    model_init(&model);
    open_ok(&model, &fifo);
    model_rx_load(&model, bytes, 7);
    {
        size_t received;
        assert(fuxi_bt_geni_fifo_read(&fifo, bytes, 1, &received) == FUXI_BT_OK && received == 1);
    }
    closed_ok(&model, &fifo);
    assert(fifo.rx_discarded == 6);
    model_init(&model);
    open_ok(&model, &fifo);
    {
        size_t accepted;
        assert(fuxi_bt_geni_fifo_write(&fifo, bytes, 8, &accepted) == FUXI_BT_OK);
        while (model.tx_count) model_tx_step(&model);
        model.advance_at = model.calls + 4;
        model.advance_us = FUXI_BT_FIFO_TX_TIMEOUT_US;
        /* 已送完的傳送晚到的輪詢仍算完成（常駐收送的排程延遲） */
        assert(fuxi_bt_geni_fifo_poll(&fifo) == FUXI_BT_OK && fifo.tx_completed == 8);
        assert(!fifo.owns_master);
    }
    closed_ok(&model, &fifo);
    model_init(&model);
    open_ok(&model, &fifo);
    {
        size_t accepted;
        assert(fuxi_bt_geni_fifo_write(&fifo, bytes, 8, &accepted) == FUXI_BT_OK);
        model.advance_at = model.calls + 4;
        model.advance_us = FUXI_BT_FIFO_TX_TIMEOUT_US;
        assert(fuxi_bt_geni_fifo_poll(&fifo) == FUXI_BT_TIMEOUT && !fifo.tx_completed);
    }
    closed_ok(&model, &fifo);
    model_init(&model);
    open_ok(&model, &fifo);
    model.restore_corrupt = 0x260;
    assert(close_until_done(&model, &fifo, 100) == FUXI_BT_CLEANUP);
    assert(fifo.quiescent && !fifo.restored);
}

static void test_callback_faults(void)
{
    struct model model;
    struct fuxi_bt_geni_fifo fifo;
    struct fuxi_bt_geni_fifo_io io;
    unsigned open_calls, close_calls, tx_calls, rx_calls;
    uint8_t bytes[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    model_init(&model);
    open_ok(&model, &fifo);
    open_calls = model.calls;
    closed_ok(&model, &fifo);
    close_calls = model.calls - open_calls;
    model_init(&model);
    open_ok(&model, &fifo);
    model.advance_at = model.calls + close_calls;
    model.advance_us = FUXI_BT_FIFO_STOP_TIMEOUT_US;
    assert(close_until_done(&model, &fifo, 100) == FUXI_BT_TIMEOUT);
    assert(fifo.quiescent && !fifo.restored);
    model_init(&model);
    open_ok(&model, &fifo);
    {
        size_t accepted;
        const unsigned calls = model.calls;
        assert(fuxi_bt_geni_fifo_write(&fifo, bytes, 8, &accepted) == FUXI_BT_OK);
        tx_calls = model.calls - calls;
    }
    closed_ok(&model, &fifo);
    model_init(&model);
    open_ok(&model, &fifo);
    model_rx_load(&model, bytes, 8);
    {
        size_t received;
        const unsigned calls = model.calls;
        assert(fuxi_bt_geni_fifo_read(&fifo, bytes, 8, &received) == FUXI_BT_OK);
        rx_calls = model.calls - calls;
    }
    closed_ok(&model, &fifo);
    for (unsigned after_write = 0; after_write < 2; after_write++) {
        for (unsigned failure = 1; failure <= open_calls; failure++) {
            unsigned calls;
            model_init(&model);
            model.fail_at = failure;
            model.fail_after_write = after_write != 0;
            memset(&fifo, 0, sizeof(fifo));
            io = model_io(&model);
            assert(fuxi_bt_geni_fifo_open(&fifo, &io, &config) == FUXI_BT_IO);
            calls = model.calls;
            assert(fuxi_bt_geni_fifo_close_poll(&fifo) == (fifo.modified ? FUXI_BT_IO : FUXI_BT_OK));
            assert(model.calls == calls && !fifo.restored);
        }
        for (unsigned failure = 1; failure <= close_calls; failure++) {
            unsigned calls;
            model_init(&model);
            open_ok(&model, &fifo);
            model.fail_at = model.calls + failure;
            model.fail_after_write = after_write != 0;
            assert(close_until_done(&model, &fifo, 100) == FUXI_BT_IO);
            calls = model.calls;
            assert(fuxi_bt_geni_fifo_close_poll(&fifo) == FUXI_BT_IO && model.calls == calls);
            assert(!fifo.restored && fifo.modified);
        }
        for (unsigned failure = 1; failure <= tx_calls; failure++) {
            size_t accepted;
            unsigned calls;
            enum fuxi_bt_status status;
            model_init(&model);
            open_ok(&model, &fifo);
            model.fail_at = model.calls + failure;
            model.fail_after_write = after_write != 0;
            status = fuxi_bt_geni_fifo_write(&fifo, bytes, 8, &accepted);
            assert(status == (accepted ? FUXI_BT_OK : FUXI_BT_IO));
            assert(accepted <= 8 && fifo.error == FUXI_BT_IO && fifo.io_fault);
            calls = model.calls;
            assert(fuxi_bt_geni_fifo_write(&fifo, bytes, 8, &accepted) == FUXI_BT_IO);
            assert(!accepted && model.calls == calls);
            assert(fuxi_bt_geni_fifo_close_poll(&fifo) == FUXI_BT_IO && model.calls == calls);
        }
        for (unsigned failure = 1; failure <= rx_calls; failure++) {
            size_t received;
            unsigned calls;
            enum fuxi_bt_status status;
            model_init(&model);
            open_ok(&model, &fifo);
            model_rx_load(&model, bytes, 8);
            model.fail_at = model.calls + failure;
            model.fail_after_write = after_write != 0;
            status = fuxi_bt_geni_fifo_read(&fifo, bytes, 8, &received);
            assert(status == (received ? FUXI_BT_OK : FUXI_BT_IO));
            assert(received <= 8 && fifo.error == FUXI_BT_IO && fifo.io_fault);
            calls = model.calls;
            assert(fuxi_bt_geni_fifo_read(&fifo, bytes, 8, &received) == FUXI_BT_IO);
            assert(!received && model.calls == calls);
            assert(fuxi_bt_geni_fifo_close_poll(&fifo) == FUXI_BT_IO && model.calls == calls);
        }
    }
    model_init(&model);
    open_ok(&model, &fifo);
    model.fail_at = model.calls + 9;
    model.fail_after_write = true;
    {
        size_t accepted;
        const enum fuxi_bt_status result = fuxi_bt_geni_fifo_write(&fifo, bytes, 8, &accepted);
        assert(result == FUXI_BT_OK && accepted == 4 && model.fifo_writes == 2);
        assert(fifo.error == FUXI_BT_IO && fifo.io_fault);
        assert(fuxi_bt_geni_fifo_write(&fifo, bytes + 4, 4, &accepted) == FUXI_BT_IO && !accepted);
    }
    assert(fuxi_bt_geni_fifo_close_poll(&fifo) == FUXI_BT_IO);
    model_init(&model);
    open_ok(&model, &fifo);
    model_rx_load(&model, bytes, 8);
    model.fail_at = model.calls + 6;
    {
        size_t received;
        assert(fuxi_bt_geni_fifo_read(&fifo, bytes, 8, &received) == FUXI_BT_OK && received == 4);
        assert(fifo.error == FUXI_BT_IO && model.fifo_reads == 1);
        assert(fuxi_bt_geni_fifo_read(&fifo, bytes, 4, &received) == FUXI_BT_IO && !received);
    }
    printf("callback fault sites open=%u close=%u TX=%u RX=%u; before/after-write modes\n",
        open_calls, close_calls, tx_calls, rx_calls);
}

struct probe_session {
    struct model model;
    struct fuxi_bt_geni_fifo fifo;
    bool replied;
};

static enum fuxi_bt_status probe_acquire(void *context, unsigned baud)
{
    struct probe_session *session = context;
    struct fuxi_bt_geni_fifo_io io = model_io(&session->model);
    assert(baud == 115200);
    return fuxi_bt_geni_fifo_open(&session->fifo, &io, &config);
}

static void probe_step(struct probe_session *session)
{
    static const uint8_t reply[] = {
        4, 0x0e, 18, 1, 0, 0xfc, 0, 0x19, 0,
        0x78, 0x56, 0x34, 0x12, 0xbc, 0x9a, 0x20, 0,
        0x44, 0x33, 0x22, 0x11
    };
    model_tx_step(&session->model);
    if (!session->replied && session->model.wire_size == 5) {
        assert(!memcmp(session->model.wire, (uint8_t[]){1, 0, 0xfc, 1, 0x19}, 5));
        model_rx_load(&session->model, reply, sizeof(reply));
        session->replied = true;
    }
}

static enum fuxi_bt_status probe_write(void *context, const uint8_t *bytes,
    size_t capacity, size_t *accepted)
{
    struct probe_session *session = context;
    return fuxi_bt_geni_fifo_write(&session->fifo, bytes, capacity, accepted);
}

static enum fuxi_bt_status probe_read(void *context, uint8_t *bytes,
    size_t capacity, size_t *received)
{
    struct probe_session *session = context;
    probe_step(session);
    return fuxi_bt_geni_fifo_read(&session->fifo, bytes, capacity, received);
}

static enum fuxi_bt_status probe_release(void *context)
{
    struct probe_session *session = context;
    return close_until_done(&session->model, &session->fifo, 100);
}

static uint64_t probe_now(void *context)
{
    return ((struct probe_session *)context)->model.clock;
}

static void probe_wait(void *context, unsigned usec)
{
    struct probe_session *session = context;
    session->model.clock += usec;
    probe_step(session);
}

static void test_qca_entry(void)
{
    struct probe_session session = {0};
    struct fuxi_bt_result result;
    struct fuxi_bt_uart_ops ops = {
        &session, probe_acquire, probe_write, probe_read, probe_release, probe_now, probe_wait
    };
    model_init(&session.model);
    assert(fuxi_bt_probe_run(1, &ops, &result) == FUXI_BT_OK);
    assert(result.version_valid && result.cleanup == FUXI_BT_OK && result.tx_bytes == 5);
    assert(result.version.product == 0x12345678 && result.version.soc == 0x11223344);
    assert(result.version.rom == 0x20 && result.version.patch == 0x9abc);
    assert(session.fifo.tx_completed == 5 && session.fifo.restored && session.fifo.quiescent);
    puts("PASS QCA normal entry: existing fuxi_bt_probe_run -> FIFO callbacks -> 5 wire bytes -> real parser -> clean close");
}

int main(void)
{
    assert(FUXI_BT_FIFO_IF_DISABLE == 0x64 && FUXI_BT_FIFO_TX == 0x700);
    assert(FUXI_BT_FIFO_RX == 0x780 && FUXI_BT_FIFO_RX_STATUS == 0x804);
    test_guards();
    test_tx();
    test_rx();
    test_errors_and_deadlines();
    test_cleanup();
    test_callback_faults();
    test_qca_entry();
    puts("PASS production GENI FIFO C: 5120 TX length/depth cases, 2056 RX length/capacity cases, packing/LAST, partial TX/RX, CTS/delayed completion, guards, errors, time/poll bounds, cancel/abort/idle/restore and exhaustive callback faults");
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
    compiler, environment = module.compiler_configuration(args.cc)
    msvc = Path(compiler[0]).name.lower() in ("cl", "cl.exe")
    if msvc and args.sanitize:
        raise SystemExit("--sanitize requires Clang or GCC with ASan and UBSan.")
    with tempfile.TemporaryDirectory(prefix="fuxi-bt-geni-fifo-") as temporary:
        directory = Path(temporary)
        harness = directory / "fifo-model.c"
        harness.write_text(HARNESS, encoding="utf-8")
        executable = directory / ("fifo-model.exe" if os.name == "nt" else "fifo-model")
        if msvc:
            command = compiler + [
                "/nologo", "/std:c11", "/W4", "/WX", "/Od", "/D_CRT_SECURE_NO_WARNINGS",
                f"/I{SOURCE}", str(SOURCE / "fuxi_bt_geni_fifo.c"),
                str(SOURCE / "fuxi_bt.c"), str(harness),
                f"/Fe{executable}"
            ]
        else:
            command = compiler + [
                "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                "-I", str(SOURCE), str(SOURCE / "fuxi_bt_geni_fifo.c"),
                str(SOURCE / "fuxi_bt.c"), str(harness),
                "-o", str(executable)
            ]
            if args.sanitize:
                command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, cwd=directory, env=environment, check=True)
        subprocess.run([str(executable)], cwd=directory, env=environment, check=True, timeout=60)
        print(f"Compiler {compiler[0]}")
        print(f"Sanitizers {'ASan/UBSan' if args.sanitize else 'disabled'}")
        print("Registers, FIFO traffic, controller completion and cleanup are emulated.")


if __name__ == "__main__":
    main()
