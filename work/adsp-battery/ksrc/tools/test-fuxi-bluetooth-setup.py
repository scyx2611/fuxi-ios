#!/usr/bin/env python3
"""Compile and run the WCN7850 controller setup (rampatch/NVM/HCI reset) against a simulated controller."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "xnu" / "fuxi-bluetooth"

HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fuxi_bt_setup.h"

#define PATCH_CAP 8192
#define NVM_CAP 512

struct sim {
    struct fuxi_bt_uart_ops ops;
    uint64_t clock;
    uint8_t command[300];
    unsigned command_size;
    uint8_t out[1 << 17];
    size_t out_head, out_tail;
    unsigned rx_chunk, tx_chunk, write_calls, stall_every;
    uint8_t patch_mode;
    size_t patch_size;
    bool skip_final_ack, silent, ibs_after_nvm, reset_error, vendor_noise, board_error;
    bool bad_framing;
    int fail_segment, fail_stale_segment;
    uint16_t board_id;
    uint8_t got_patch[PATCH_CAP];
    size_t got_patch_size;
    uint8_t got_nvm[NVM_CAP];
    size_t got_nvm_size;
    bool board_seen;
    unsigned segments, acquires, releases, commands;
    uint32_t random;
    /* 速率模型：host_baud＝目前 acquire 的速率（0＝沒取），ctrl_baud＝控制器速率。
     * 0xfc48 只登記 pending，host 歸還 UART 時才生效（控制器在送完後的等待期間換速）；
     * 兩邊不同時寫出的位元組控制器收不到、控制器送的位元組 host 收不到。 */
    unsigned host_baud, ctrl_baud, pending_baud;
    unsigned acquire_fail_baud, rx_dead_baud, tx_dead_baud;
    bool baud_reject, baud_ack_old, junk_once, nolog_error, build_seen;
    unsigned baud_commands, dropped, acquire_log[8], acquire_count;
    uint8_t baud_last;
    unsigned nolog_seen;
    size_t nolog_nvm;
};

struct files {
    uint8_t patch[PATCH_CAP];
    size_t patch_size;
    bool patch_present;
    uint8_t nvm[NVM_CAP];
    size_t nvm_size;
    const char *nvm_present;
    unsigned loads, releases;
    char asked[8][FUXI_BT_SETUP_NAME];
    unsigned asked_count;
    size_t oversize;
};

static void push(struct sim *s, const uint8_t *bytes, size_t size)
{
    assert(s->out_tail + size <= sizeof(s->out));
    memcpy(s->out + s->out_tail, bytes, size);
    s->out_tail += size;
}

static void vendor_complete(struct sim *s, uint8_t cresp, uint8_t rtype, const uint8_t *data, unsigned size)
{
    uint8_t event[300] = {4, 0x0e, (uint8_t)(5 + size), 1, 0x00, 0xfc, cresp, rtype};
    if (s->vendor_noise) {
        static const uint8_t noise[] = {4, 0xff, 3, 0x01, 0x02, 0x03};
        push(s, noise, sizeof(noise));
    }
    if (size) memcpy(event + 8, data, size);
    push(s, event, 8 + size);
}

static void complete(struct sim *s, uint16_t opcode, const uint8_t *params, unsigned size)
{
    uint8_t event[300] = {4, 0x0e, (uint8_t)(3 + size), 1, (uint8_t)opcode, (uint8_t)(opcode >> 8)};
    memcpy(event + 6, params, size);
    push(s, event, 6 + size);
}

static unsigned qca_baud(uint8_t qca)
{
    switch (qca) {
        case 0: return 115200;
        case 10: return 921600;
        case 13: return 2000000;
        case 14: return 3000000;
        case 17: return 3200000;
        default: return 0;
    }
}

static void handle(struct sim *s)
{
    const uint8_t *c = s->command;
    const uint16_t opcode = (uint16_t)(c[1] | c[2] << 8);
    const uint8_t *p = c + 4;
    s->commands++;
    if (s->silent) {
        return;
    }
    if (s->junk_once && s->ctrl_baud != FUXI_BT_BOOT_BAUD) {
        static const uint8_t junk[] = {0x00, 0x7f};
        s->junk_once = false;
        push(s, junk, sizeof(junk));
    }
    if (opcode == 0xfc48) {
        assert(c[3] == 1);
        s->baud_commands++;
        s->baud_last = p[0];
        if (s->baud_ack_old) {
            const uint8_t status = 0;
            complete(s, opcode, &status, 1);
        }
        if (!s->baud_reject && qca_baud(p[0])) {
            s->pending_baud = qca_baud(p[0]);
        }
    } else if (opcode == 0xfc17) {
        const uint8_t status = s->nolog_error ? 0x01 : 0;
        assert(c[3] == 2 && p[0] == 0x14 && p[1] == 0x00);
        assert(s->board_seen && !s->build_seen);
        s->nolog_seen++;
        s->nolog_nvm = s->got_nvm_size;
        complete(s, opcode, &status, 1);
    } else if (opcode == 0xfc00 && p[0] == 0x19) {
        static const uint8_t version[] = {0x0c, 0x19, 0, 0, 0, 0xfb, 0x43, 0x00, 0x02, 0x00, 0x02, 0x17, 0x40};
        vendor_complete(s, 0, 0x19, version, sizeof(version));
    } else if (opcode == 0xfc00 && p[0] == 0x1e) {
        const unsigned size = p[1];
        const unsigned index = s->segments++;
        assert(c[3] == size + 2 && size >= 1 && size <= FUXI_BT_TLV_SEGMENT);
        if (!s->board_seen) {
            assert(s->got_patch_size + size <= PATCH_CAP);
            memcpy(s->got_patch + s->got_patch_size, p + 2, size);
            s->got_patch_size += size;
            if ((int)index == s->fail_segment) {
                vendor_complete(s, 0x01, 0x1e, NULL, 0);
            } else if ((int)index == s->fail_stale_segment) {
                vendor_complete(s, 0x11, 0x1e, NULL, 0);
            } else if (s->patch_mode == 0 ||
                       (s->got_patch_size == s->patch_size && !s->skip_final_ack)) {
                vendor_complete(s, 0, 0x1e, NULL, 0);
            }
        } else {
            assert(s->got_nvm_size + size <= NVM_CAP);
            memcpy(s->got_nvm + s->got_nvm_size, p + 2, size);
            s->got_nvm_size += size;
            vendor_complete(s, 0, 0x1e, NULL, 0);
        }
    } else if (opcode == 0xfc00 && p[0] == 0x23) {
        const uint8_t bid[] = {2, (uint8_t)(s->board_id >> 8), (uint8_t)s->board_id};
        s->board_seen = true;
        vendor_complete(s, s->board_error ? 0x01 : 0, 0x23, bid, sizeof(bid));
    } else if (opcode == 0xfc00 && p[0] == 0x20) {
        static const uint8_t info[] = {10, 'B', 'T', 'F', 'M', '.', 'H', 'M', 'T', 0x01, 0};
        s->build_seen = true;
        if (s->ibs_after_nvm) {
            static const uint8_t sleep[] = {0xfe};
            push(s, sleep, 1);
        }
        vendor_complete(s, 0, 0x20, info, sizeof(info));
    } else if (opcode == 0x0c03) {
        const uint8_t status = s->reset_error ? 0x0c : 0;
        complete(s, opcode, &status, 1);
    } else if (opcode == 0x1001) {
        static const uint8_t version[] = {0, 0x0d, 0x00, 0x00, 0x0d, 0x1d, 0x00, 0x34, 0x12};
        complete(s, opcode, version, sizeof(version));
    } else if (opcode == 0x1009) {
        static const uint8_t address[] = {0, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11};
        complete(s, opcode, address, sizeof(address));
    } else {
        assert(!"unexpected command");
    }
}

static enum fuxi_bt_status sim_acquire(void *context, unsigned baud)
{
    struct sim *s = context;
    assert(fuxi_bt_rate_find(baud) && !s->host_baud);
    if (s->acquire_count < 8) {
        s->acquire_log[s->acquire_count] = baud;
    }
    s->acquire_count++;
    if (baud == s->acquire_fail_baud) {
        return FUXI_BT_IO;
    }
    s->host_baud = baud;
    s->acquires++;
    return FUXI_BT_OK;
}

static enum fuxi_bt_status sim_write(void *context, const uint8_t *bytes, size_t size, size_t *accepted)
{
    struct sim *s = context;
    size_t count = size < s->tx_chunk ? size : s->tx_chunk;
    s->write_calls++;
    *accepted = 0;
    if (s->stall_every && s->write_calls % s->stall_every == 0) {
        return FUXI_BT_AGAIN;
    }
    assert(s->host_baud);
    if (s->host_baud != s->ctrl_baud || s->host_baud == s->tx_dead_baud) {
        s->dropped += (unsigned)count;
        s->command_size = 0;
        *accepted = count;
        return FUXI_BT_OK;
    }
    for (size_t index = 0; index < count; index++) {
        if (!s->command_size) {
            assert(bytes[index] == 1);
        }
        assert(s->command_size < sizeof(s->command));
        s->command[s->command_size++] = bytes[index];
        if (s->command_size >= 4 && s->command_size == 4u + s->command[3]) {
            handle(s);
            s->command_size = 0;
        }
    }
    *accepted = count;
    return FUXI_BT_OK;
}

static enum fuxi_bt_status sim_read(void *context, uint8_t *bytes, size_t capacity, size_t *received)
{
    struct sim *s = context;
    size_t count = s->out_tail - s->out_head;
    if (s->bad_framing && count == 0) {
        s->random ^= s->random << 13;
        s->random ^= s->random >> 17;
        s->random ^= s->random << 5;
        bytes[0] = (uint8_t)s->random;
        *received = 1;
        return FUXI_BT_OK;
    }
    if (count > capacity) count = capacity;
    if (count > s->rx_chunk) count = s->rx_chunk;
    assert(s->host_baud);
    if (s->host_baud == s->rx_dead_baud) {
        *received = 0;
        return FUXI_BT_AGAIN;
    }
    if (s->host_baud != s->ctrl_baud) {
        /* 速率不同：每個位元組都變成雜訊 */
        for (size_t index = 0; index < count; index++) {
            bytes[index] = (uint8_t)(0x40 + (s->out[s->out_head + index] & 0x3f));
        }
        s->out_head += count;
        *received = count;
        return count ? FUXI_BT_OK : FUXI_BT_AGAIN;
    }
    memcpy(bytes, s->out + s->out_head, count);
    s->out_head += count;
    *received = count;
    return count ? FUXI_BT_OK : FUXI_BT_AGAIN;
}

static enum fuxi_bt_status sim_release(void *context)
{
    struct sim *s = context;
    if (!s->host_baud) {
        return FUXI_BT_OK; /* 和 board 一樣：沒取就歸還是空操作 */
    }
    s->releases++;
    s->host_baud = 0;
    s->out_head = s->out_tail = 0; /* FIFO 關閉時沒讀的 RX 丟掉 */
    s->command_size = 0;
    if (s->pending_baud) {
        s->ctrl_baud = s->pending_baud;
        s->pending_baud = 0;
    }
    return FUXI_BT_OK;
}

static uint64_t sim_now(void *context)
{
    struct sim *s = context;
    return s->clock += 3;
}

static void sim_wait(void *context, unsigned microseconds)
{
    struct sim *s = context;
    s->clock += microseconds;
}

static enum fuxi_bt_status file_load(void *context, const char *name, size_t limit, struct fuxi_bt_blob *blob)
{
    struct files *f = context;
    assert(fuxi_bt_setup_name_valid(name));
    assert(f->asked_count < 8);
    strcpy(f->asked[f->asked_count++], name);
    if (!strcmp(name, "hmtbtfw20.tlv") && f->patch_present) {
        assert(limit == FUXI_BT_PATCH_MAX);
        blob->data = malloc(f->patch_size + f->oversize);
        memcpy(blob->data, f->patch, f->patch_size);
        blob->size = f->patch_size + f->oversize;
        f->loads++;
        return FUXI_BT_OK;
    }
    if (f->nvm_present && !strcmp(name, f->nvm_present)) {
        assert(limit == FUXI_BT_NVM_MAX);
        blob->data = malloc(f->nvm_size);
        memcpy(blob->data, f->nvm, f->nvm_size);
        blob->size = f->nvm_size;
        f->loads++;
        return FUXI_BT_OK;
    }
    return FUXI_BT_OFF;
}

static void file_release(void *context, struct fuxi_bt_blob *blob)
{
    struct files *f = context;
    free(blob->data);
    blob->data = NULL;
    f->releases++;
}

static void put32(uint8_t *b, uint32_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24); }

static size_t make_patch(uint8_t *b, size_t total, uint8_t mode)
{
    memset(b, 0, total);
    put32(b, 1u | (uint32_t)(total - 4) << 8);
    put32(b + 4, (uint32_t)total);
    b[4 + 10] = mode;
    b[4 + 12] = 0x19;
    b[4 + 14] = 0x00; b[4 + 15] = 0x02;
    b[4 + 16] = 0xfb; b[4 + 17] = 0x43;
    for (size_t index = 28; index < total; index++) b[index] = (uint8_t)(index * 7 + 3);
    return total;
}

static size_t add_tag(uint8_t *b, size_t at, uint16_t tag, const uint8_t *data, uint16_t size)
{
    b[at] = (uint8_t)tag; b[at + 1] = (uint8_t)(tag >> 8);
    b[at + 2] = (uint8_t)size; b[at + 3] = (uint8_t)(size >> 8);
    memset(b + at + 4, 0xaa, 8);
    memcpy(b + at + 12, data, size);
    return at + 12 + size;
}

static size_t make_nvm(uint8_t *b, bool hci, bool sleep)
{
    static const uint8_t address[] = {1, 2, 3, 4, 5, 6};
    static const uint8_t hci_value[] = {0x8e, 0x0f, 0x55, 0x01};
    static const uint8_t sleep_value[] = {0x03};
    static const uint8_t other[10] = {9, 9, 9, 9, 9, 9, 9, 9, 9, 9};
    size_t at = 4;
    memset(b, 0, NVM_CAP);
    at = add_tag(b, at, 2, address, sizeof(address));
    if (hci) at = add_tag(b, at, 17, hci_value, sizeof(hci_value));
    if (sleep) at = add_tag(b, at, 27, sleep_value, sizeof(sleep_value));
    at = add_tag(b, at, 100, other, sizeof(other));
    put32(b, 2u | (uint32_t)(at - 4) << 8);
    return at;
}

static void sim_init(struct sim *s, unsigned rx_chunk, unsigned tx_chunk)
{
    memset(s, 0, sizeof(*s));
    s->ops.context = s;
    s->ops.acquire = sim_acquire;
    s->ops.write = sim_write;
    s->ops.read = sim_read;
    s->ops.release = sim_release;
    s->ops.now_us = sim_now;
    s->ops.wait_us = sim_wait;
    s->rx_chunk = rx_chunk;
    s->tx_chunk = tx_chunk;
    s->fail_segment = -1;
    s->fail_stale_segment = -1;
    s->board_id = 0x010a;
    s->random = 0x78501234;
    s->ctrl_baud = FUXI_BT_BOOT_BAUD;
}

static void files_init(struct files *f, size_t patch_size, uint8_t mode, const char *nvm_name)
{
    memset(f, 0, sizeof(*f));
    f->patch_size = make_patch(f->patch, patch_size, mode);
    f->patch_present = true;
    f->nvm_size = make_nvm(f->nvm, true, true);
    f->nvm_present = nvm_name;
}

static struct fuxi_bt_files files_ops(struct files *f)
{
    struct fuxi_bt_files ops = {f, file_load, file_release};
    return ops;
}

static void expect_nvm_baud(const struct sim *s, const struct files *f, uint8_t qca)
{
    uint8_t expected[NVM_CAP];
    memcpy(expected, f->nvm, f->nvm_size);
    /* tag 17 在 4+18 起：資料 4+18+12；data[1]＝實際速率的 qca 列舉值 */
    expected[4 + 18 + 12] = 0x0e;
    expected[4 + 18 + 12 + 1] = qca;
    expected[4 + 18 + 16 + 12] = 0x02;
    assert(s->got_nvm_size == f->nvm_size);
    assert(!memcmp(s->got_nvm, expected, f->nvm_size));
}

static void expect_nvm(const struct sim *s, const struct files *f)
{
    expect_nvm_baud(s, f, 0);
}

static void run_good(unsigned rx_chunk, unsigned tx_chunk, unsigned stall, uint8_t mode, size_t patch_size,
    const char *nvm_name, bool noise)
{
    struct sim s;
    struct files f;
    struct fuxi_bt_files ops;
    struct fuxi_bt_setup_result r;
    sim_init(&s, rx_chunk, tx_chunk);
    s.stall_every = stall;
    s.vendor_noise = noise;
    files_init(&f, patch_size, mode, nvm_name);
    s.patch_mode = mode;
    s.patch_size = f.patch_size;
    ops = files_ops(&f);
    assert(fuxi_bt_setup_run(&s.ops, &ops, &r) == FUXI_BT_OK);
    assert(r.stage == FUXI_BT_SETUP_DONE && r.cleanup == FUXI_BT_OK);
    assert(s.acquires == 1 && s.releases == 1 && f.loads == f.releases && f.loads == 2);
    assert(r.version.soc == 0x40170200 && r.version.rom == 0x200 && r.version.patch == 0x43fb && r.rom_ver == 0x20);
    assert(!strcmp(r.patch_name, "hmtbtfw20.tlv"));
    assert(!strcmp(r.nvm_name, nvm_name));
    assert(s.got_patch_size == f.patch_size && !memcmp(s.got_patch, f.patch, f.patch_size));
    expect_nvm(&s, &f);
    assert(r.patch.segments == (f.patch_size + 242) / 243);
    assert(r.patch.acks == (mode == 0 ? r.patch.segments : 1u) && r.patch.final_ack);
    assert(r.patch.download_mode == mode && r.patch.product == 0x19 && r.patch.rom == 0x200 && r.patch.patch == 0x43fb);
    assert(r.nvm.hci_tag && r.nvm.hci_before[0] == 0x8e && r.nvm.hci_after[0] == 0x0e && r.nvm.hci_after[1] == 0);
    assert(r.nvm.sleep_tag && r.nvm.sleep_before == 3 && r.nvm.sleep_after == 2 && r.nvm.tags == 4);
    assert(r.board_id_valid && r.board_id == 0x010a && r.board_id_status == FUXI_BT_OK);
    assert(!strcmp(r.build_info, "BTFM.HMT?"));
    assert(r.local_version_valid && r.manufacturer == 0x1d && r.lmp_subversion == 0x1234 && r.hci_version == 0x0d);
    assert(r.bdaddr_valid && r.bdaddr[0] == 0x66 && r.bdaddr[5] == 0x11);
    assert(r.vendor_events == (noise ? (mode == 0 ? r.patch.segments : 1u) + r.nvm.segments + 3u : 0u));
    assert(r.elapsed_us < FUXI_BT_SETUP_TIMEOUT_US);
    assert(r.baud_requested == FUXI_BT_BOOT_BAUD && r.baud_active == FUXI_BT_BOOT_BAUD);
    assert(r.baud_status == FUXI_BT_OFF && r.baud_fallback == FUXI_BT_OFF && r.nolog_status == FUXI_BT_OFF);
    assert(!s.baud_commands && !s.nolog_seen && !s.dropped);
}

static struct fuxi_bt_setup_result run_options(struct sim *s, struct files *f,
    uint32_t baud, bool nolog)
{
    struct fuxi_bt_files ops = files_ops(f);
    const struct fuxi_bt_setup_options options = {baud, nolog};
    struct fuxi_bt_setup_result r;
    s->patch_mode = f->patch[4 + 10];
    s->patch_size = f->patch_size;
    (void)fuxi_bt_setup_run_options(&s->ops, &ops, &options, &r);
    assert(s->acquires == s->releases && !s->host_baud);
    assert(f->loads == f->releases);
    assert(r.elapsed_us <= FUXI_BT_SETUP_TIMEOUT_US + 1000000u);
    return r;
}

/* 換速成功：01 48 fc 01 <enum>，host 歸還 UART、以新速率取回、重查版本，NVM data[1] 跟著新速率 */
static void baud_good(unsigned rx_chunk, unsigned tx_chunk, unsigned stall, uint32_t baud, uint8_t qca,
    bool ack_old, bool junk, bool nolog)
{
    struct sim s;
    struct files f;
    struct fuxi_bt_setup_result r;
    sim_init(&s, rx_chunk, tx_chunk);
    s.stall_every = stall;
    s.baud_ack_old = ack_old;
    s.junk_once = junk;
    files_init(&f, 1801, 3, "hmtnv20.b10a");
    r = run_options(&s, &f, baud, nolog);
    assert(r.status == FUXI_BT_OK && r.stage == FUXI_BT_SETUP_DONE && r.cleanup == FUXI_BT_OK);
    assert(r.baud_requested == baud && r.baud_active == baud && r.baud_status == FUXI_BT_OK);
    assert(r.baud_fallback == FUXI_BT_OFF && r.baud_attempts == (junk ? 2u : 1u));
    assert(s.baud_commands == 1 && s.baud_last == qca && s.ctrl_baud == baud && !s.dropped);
    assert(s.acquires == 2 && s.acquire_count == 2);
    assert(s.acquire_log[0] == FUXI_BT_BOOT_BAUD && s.acquire_log[1] == baud);
    assert(r.baud_early_bytes == (ack_old ? 7u : 0u));
    if (ack_old) {
        static const uint8_t ack[] = {4, 0x0e, 4, 1, 0x48, 0xfc, 0};
        assert(r.baud_early_size == 7 && !memcmp(r.baud_early, ack, 7));
    }
    assert(r.baud_us >= FUXI_BT_BAUD_SETTLE_US);
    assert(r.version.soc == 0x40170200 && r.local_version_valid && r.bdaddr_valid);
    expect_nvm_baud(&s, &f, qca);
    assert(r.nvm.hci_after[1] == qca);
    if (nolog) {
        assert(r.nolog_status == FUXI_BT_OK && s.nolog_seen == 1 && s.nolog_nvm == f.nvm_size);
    } else {
        assert(r.nolog_status == FUXI_BT_OFF && !s.nolog_seen);
    }
}

static void test_baud(void)
{
    struct sim s;
    struct files f;
    struct fuxi_bt_setup_result r;
    static const unsigned chunks[] = {1, 3, 64};
    static const struct { uint32_t baud; uint8_t qca; } rates[] = {
        {921600, 10}, {2000000, 13}, {3000000, 14}, {3200000, 17}
    };
    unsigned runs = 0;
    for (unsigned rate = 0; rate < 4; rate++) {
        for (unsigned rx = 0; rx < 3; rx++) {
            for (unsigned tx = 0; tx < 3; tx++) {
                baud_good(chunks[rx], chunks[tx], (rx + tx) % 2 ? 3 : 0, rates[rate].baud, rates[rate].qca,
                    (rx + tx + rate) & 1, rx == tx, (rate + rx) & 1);
                runs++;
            }
        }
    }

    /* 115200 明確指定＝不換速 */
    sim_init(&s, 64, 64); files_init(&f, 972, 0, "hmtnv20.bin");
    r = run_options(&s, &f, FUXI_BT_BOOT_BAUD, false);
    assert(r.status == FUXI_BT_OK && r.baud_status == FUXI_BT_OFF && s.acquires == 1 && !s.baud_commands);
    expect_nvm(&s, &f);

    /* 不在速率表：不取 UART 就回 ARGUMENT */
    sim_init(&s, 64, 64); files_init(&f, 972, 0, "hmtnv20.bin");
    r = run_options(&s, &f, 9600, false);
    assert(r.status == FUXI_BT_ARGUMENT && !s.acquire_count && !s.commands);
    sim_init(&s, 64, 64); files_init(&f, 972, 0, "hmtnv20.bin");
    r = run_options(&s, &f, 4000000, false);
    assert(r.status == FUXI_BT_ARGUMENT && !s.acquire_count);
    {
        struct fuxi_bt_files ops = files_ops(&f);
        sim_init(&s, 64, 64);
        assert(fuxi_bt_setup_run_options(&s.ops, &ops, NULL, &r) == FUXI_BT_ARGUMENT && !s.acquire_count);
    }

    /* 控制器不換速：新速率下雙方都收不到，確認逾時 3 次，退回 115200 確認成功，以 115200 繼續 */
    sim_init(&s, 64, 64); files_init(&f, 972, 0, "hmtnv20.bin"); s.baud_reject = true;
    r = run_options(&s, &f, 3200000, true);
    assert(r.status == FUXI_BT_OK && r.stage == FUXI_BT_SETUP_DONE);
    assert(r.baud_status == FUXI_BT_TIMEOUT && r.baud_fallback == FUXI_BT_OK);
    assert(r.baud_active == FUXI_BT_BOOT_BAUD && r.baud_attempts == FUXI_BT_BAUD_VERIFY_ATTEMPTS + 1);
    assert(s.acquire_count == 3 && s.acquire_log[1] == 3200000 && s.acquire_log[2] == FUXI_BT_BOOT_BAUD);
    assert(s.baud_commands == 1 && s.dropped > 0 && s.ctrl_baud == FUXI_BT_BOOT_BAUD);
    assert(r.nolog_status == FUXI_BT_OK);
    expect_nvm(&s, &f);

    /* 控制器換了、host 在新速率收不到：以新速率送 0xfc48 0 要它回 115200，再確認 */
    sim_init(&s, 64, 64); files_init(&f, 972, 0, "hmtnv20.bin"); s.rx_dead_baud = 3000000;
    r = run_options(&s, &f, 3000000, false);
    assert(r.status == FUXI_BT_OK && r.baud_status == FUXI_BT_TIMEOUT && r.baud_fallback == FUXI_BT_OK);
    assert(r.baud_active == FUXI_BT_BOOT_BAUD && s.baud_commands == 2 && s.baud_last == 0);
    assert(s.ctrl_baud == FUXI_BT_BOOT_BAUD);
    expect_nvm(&s, &f);

    /* 新速率取 UART 失敗、控制器沒換：退回確認成功，baud_status 記 IO */
    sim_init(&s, 64, 64); files_init(&f, 972, 0, "hmtnv20.bin");
    s.acquire_fail_baud = 3200000; s.baud_reject = true;
    r = run_options(&s, &f, 3200000, false);
    assert(r.status == FUXI_BT_OK && r.baud_status == FUXI_BT_IO && r.baud_fallback == FUXI_BT_OK);
    assert(r.baud_active == FUXI_BT_BOOT_BAUD && s.acquire_count == 3 && s.baud_commands == 1);

    /* 新速率取 UART 失敗、控制器已換：115200 下確認不到，setup 失敗在 baud 階段 */
    sim_init(&s, 64, 64); files_init(&f, 972, 0, "hmtnv20.bin"); s.acquire_fail_baud = 3200000;
    r = run_options(&s, &f, 3200000, false);
    assert(r.status == FUXI_BT_TIMEOUT && r.stage == FUXI_BT_SETUP_BAUD);
    assert(r.baud_status == FUXI_BT_IO && r.baud_fallback == FUXI_BT_TIMEOUT && f.loads == 0);

    /* 控制器換了、host 在新速率送不出去：退回的 0xfc48 也送不到，setup 失敗 */
    sim_init(&s, 64, 64); files_init(&f, 972, 0, "hmtnv20.bin"); s.tx_dead_baud = 2000000;
    r = run_options(&s, &f, 2000000, false);
    assert(r.status == FUXI_BT_TIMEOUT && r.stage == FUXI_BT_SETUP_BAUD);
    assert(r.baud_status == FUXI_BT_TIMEOUT && r.baud_fallback == FUXI_BT_TIMEOUT);
    assert(s.baud_commands == 1 && s.ctrl_baud == 2000000 && f.loads == 0);

    /* 0xfc17 回錯：只記錄，照常完成 */
    sim_init(&s, 64, 64); files_init(&f, 972, 0, "hmtnv20.bin"); s.nolog_error = true;
    r = run_options(&s, &f, 0, true);
    assert(r.status == FUXI_BT_OK && r.nolog_status == FUXI_BT_CONTROLLER && s.nolog_seen == 1);
    assert(r.stage == FUXI_BT_SETUP_DONE && !s.baud_commands);

    /* 換速階段控制器完全不回（silent）：第一次版本查詢就逾時，不送 0xfc48 */
    sim_init(&s, 64, 64); files_init(&f, 972, 0, "hmtnv20.bin"); s.silent = true;
    r = run_options(&s, &f, 3200000, true);
    assert(r.status == FUXI_BT_TIMEOUT && r.stage == FUXI_BT_SETUP_VERSION && s.acquire_count == 1);

    /* 雜訊模糊測試：任何結果都不能越界，取還次數一致 */
    for (unsigned trial = 0; trial < 500; trial++) {
        sim_init(&s, trial % 7 + 1, trial % 5 + 1);
        s.random ^= trial * 2654435761u;
        s.bad_framing = true;
        s.silent = true;
        files_init(&f, 972, 0, "hmtnv20.bin");
        r = run_options(&s, &f, trial & 1 ? 3200000 : 921600, trial & 2);
        assert(r.status != FUXI_BT_OK && r.frames <= FUXI_BT_SETUP_MAX_FRAMES);
    }
    printf("PASS bt setup baud: %u switch runs (4 rates, chunking, old-baud ack, junk retry, nolog), "
           "boot/invalid rates, controller reject, RX/TX dead, acquire failure, nolog error, 500 fuzz trials\n", runs);
}

static struct fuxi_bt_setup_result run_case(struct sim *s, struct files *f)
{
    struct fuxi_bt_files ops = files_ops(f);
    struct fuxi_bt_setup_result r;
    s->patch_size = f->patch_size + f->oversize;
    (void)fuxi_bt_setup_run(&s->ops, &ops, &r);
    assert(s->acquires == s->releases);
    assert(f->loads == f->releases);
    assert(r.elapsed_us <= FUXI_BT_SETUP_TIMEOUT_US + 1000000u);
    return r;
}

static void test_failures(void)
{
    struct sim s;
    struct files f;
    struct fuxi_bt_setup_result r;

    /* 省略回應模式，最後一段也不回：記 final_ack=0，後續命令正常就算完成 */
    sim_init(&s, 64, 64); files_init(&f, 1801, 3, "hmtnv20.b10a"); s.patch_mode = 3; s.skip_final_ack = true;
    r = run_case(&s, &f);
    assert(r.status == FUXI_BT_OK && !r.patch.final_ack && r.patch.acks == 0);

    /* 檔名對照 Linux qca_get_nvm_name_by_board 的 "%s%02x.b%02x"：低於 0x100 補到兩位，否則照實際位數 */
    sim_init(&s, 64, 64); files_init(&f, 972, 0, "hmtnv20.b0a"); s.patch_mode = 0; s.board_id = 0x000a;
    r = run_case(&s, &f);
    assert(r.status == FUXI_BT_OK && !strcmp(r.nvm_name, "hmtnv20.b0a"));
    assert(f.asked_count == 2 && !strcmp(f.asked[0], "hmtbtfw20.tlv") && !strcmp(f.asked[1], "hmtnv20.b0a"));
    sim_init(&s, 64, 64); files_init(&f, 972, 0, "hmtnv20.b14"); s.patch_mode = 0; s.board_id = 0x0014;
    r = run_case(&s, &f);
    assert(r.status == FUXI_BT_OK && !strcmp(r.nvm_name, "hmtnv20.b14") && f.asked_count == 2);
    sim_init(&s, 64, 64); files_init(&f, 972, 0, "hmtnv20.b1234"); s.patch_mode = 0; s.board_id = 0x1234;
    r = run_case(&s, &f);
    assert(r.status == FUXI_BT_OK && !strcmp(r.nvm_name, "hmtnv20.b1234") && f.asked_count == 2);
    /* 板級檔不存在才改讀 .bin（Linux qca_get_alt_nvm_file） */
    sim_init(&s, 64, 64); files_init(&f, 972, 0, "hmtnv20.bin"); s.patch_mode = 0;
    r = run_case(&s, &f);
    assert(r.status == FUXI_BT_OK && !strcmp(r.nvm_name, "hmtnv20.bin") && f.asked_count == 3);
    assert(!strcmp(f.asked[1], "hmtnv20.b10a") && !strcmp(f.asked[2], "hmtnv20.bin"));
    /* board ID 0 或 0xffff 直接用 .bin */
    sim_init(&s, 64, 64); files_init(&f, 972, 0, "hmtnv20.bin"); s.patch_mode = 0; s.board_id = 0;
    r = run_case(&s, &f);
    assert(r.status == FUXI_BT_OK && f.asked_count == 2 && r.board_id_valid && !r.board_id);
    sim_init(&s, 64, 64); files_init(&f, 972, 0, "hmtnv20.bin"); s.patch_mode = 0; s.board_id = 0xffff;
    r = run_case(&s, &f);
    assert(r.status == FUXI_BT_OK && f.asked_count == 2 && r.board_id_valid && r.board_id == 0xffff);
    sim_init(&s, 64, 64); files_init(&f, 972, 0, "hmtnv20.bin"); s.patch_mode = 0; s.board_error = true;
    r = run_case(&s, &f);
    assert(r.status == FUXI_BT_OK && r.board_id_status == FUXI_BT_CONTROLLER && !r.board_id_valid);
    sim_init(&s, 64, 64); files_init(&f, 972, 0, "hmtnv20.c0a"); s.patch_mode = 0;
    r = run_case(&s, &f);
    assert(r.status == FUXI_BT_OFF && r.stage == FUXI_BT_SETUP_NVM_LOAD);

    /* 缺 rampatch */
    sim_init(&s, 64, 64); files_init(&f, 972, 0, "hmtnv20.bin"); f.patch_present = false;
    r = run_case(&s, &f);
    assert(r.status == FUXI_BT_OFF && r.stage == FUXI_BT_SETUP_PATCH_LOAD && s.segments == 0);

    /* 控制器拒絕第 5 段（每段都等）；省略模式下中途回錯 */
    sim_init(&s, 64, 64); files_init(&f, 1801, 0, "hmtnv20.bin"); s.patch_mode = 0; s.fail_segment = 5;
    r = run_case(&s, &f);
    assert(r.status == FUXI_BT_CONTROLLER && r.stage == FUXI_BT_SETUP_PATCH_SEND && r.controller_status == 1 && r.patch.segments == 6);
    sim_init(&s, 64, 64); files_init(&f, 1801, 3, "hmtnv20.bin"); s.patch_mode = 3; s.fail_stale_segment = 2;
    r = run_case(&s, &f);
    assert(r.status == FUXI_BT_CONTROLLER && r.stage == FUXI_BT_SETUP_PATCH_SEND && r.controller_status == 0x11);

    /* IBS 位元組、reset 失敗、完全不回應 */
    sim_init(&s, 64, 64); files_init(&f, 972, 0, "hmtnv20.bin"); s.patch_mode = 0; s.ibs_after_nvm = true;
    r = run_case(&s, &f);
    assert(r.status == FUXI_BT_IBS && r.stage == FUXI_BT_SETUP_BUILD_INFO);
    sim_init(&s, 64, 64); files_init(&f, 972, 0, "hmtnv20.bin"); s.patch_mode = 0; s.reset_error = true;
    r = run_case(&s, &f);
    assert(r.status == FUXI_BT_CONTROLLER && r.stage == FUXI_BT_SETUP_RESET && r.controller_status == 0x0c);
    sim_init(&s, 64, 64); files_init(&f, 972, 0, "hmtnv20.bin"); s.silent = true;
    r = run_case(&s, &f);
    assert(r.status == FUXI_BT_TIMEOUT && r.stage == FUXI_BT_SETUP_VERSION && f.loads == 0);

    /* 檔案格式 */
    sim_init(&s, 64, 64); files_init(&f, 972, 0, "hmtnv20.bin"); f.patch[0] = 2;
    r = run_case(&s, &f);
    assert(r.status == FUXI_BT_PROTOCOL && r.stage == FUXI_BT_SETUP_PATCH_PARSE && s.segments == 0);
    sim_init(&s, 64, 64); files_init(&f, 972, 0, "hmtnv20.bin"); memcpy(f.patch, "\177ELF", 4);
    r = run_case(&s, &f);
    assert(r.status == FUXI_BT_PROTOCOL && r.stage == FUXI_BT_SETUP_PATCH_PARSE);
    sim_init(&s, 64, 64); files_init(&f, 972, 0, "hmtnv20.bin"); f.patch[4 + 10] = 4;
    r = run_case(&s, &f);
    assert(r.status == FUXI_BT_PROTOCOL && r.stage == FUXI_BT_SETUP_PATCH_PARSE);
    sim_init(&s, 64, 64); files_init(&f, 972, 0, "hmtnv20.bin"); put32(f.patch, 1u | 972u << 8);
    r = run_case(&s, &f);
    assert(r.status == FUXI_BT_PROTOCOL && r.stage == FUXI_BT_SETUP_PATCH_PARSE);
    sim_init(&s, 64, 64); files_init(&f, 972, 0, "hmtnv20.bin"); f.nvm_size = make_nvm(f.nvm, false, true);
    s.patch_mode = 0;
    r = run_case(&s, &f);
    assert(r.status == FUXI_BT_PROTOCOL && r.stage == FUXI_BT_SETUP_NVM_PARSE && s.got_nvm_size == 0);
    sim_init(&s, 64, 64); files_init(&f, 972, 0, "hmtnv20.bin"); f.nvm[4 + 2] = 0xff; s.patch_mode = 0;
    r = run_case(&s, &f);
    assert(r.status == FUXI_BT_PROTOCOL && r.stage == FUXI_BT_SETUP_NVM_PARSE);
    sim_init(&s, 64, 64); files_init(&f, 972, 0, "hmtnv20.bin"); f.oversize = FUXI_BT_PATCH_MAX;
    r = run_case(&s, &f);
    assert(r.status == FUXI_BT_IO && r.stage == FUXI_BT_SETUP_PATCH_LOAD);

    /* 參數 */
    sim_init(&s, 64, 64); files_init(&f, 972, 0, "hmtnv20.bin");
    {
        struct fuxi_bt_files bad = {&f, NULL, file_release};
        assert(fuxi_bt_setup_run(&s.ops, &bad, &r) == FUXI_BT_ARGUMENT && s.acquires == 0);
        assert(fuxi_bt_setup_run(NULL, &bad, &r) == FUXI_BT_NO_UART);
        assert(fuxi_bt_setup_run(&s.ops, &bad, NULL) == FUXI_BT_ARGUMENT);
    }
}

static void test_fuzz(void)
{
    for (unsigned trial = 0; trial < 3000; trial++) {
        struct sim s;
        struct files f;
        struct fuxi_bt_setup_result r;
        sim_init(&s, trial % 7 + 1, trial % 5 + 1);
        s.random ^= trial * 2654435761u;
        s.bad_framing = true;
        s.silent = true;
        files_init(&f, 972, 0, "hmtnv20.bin");
        r = run_case(&s, &f);
        assert(r.status != FUXI_BT_OK && r.frames <= FUXI_BT_SETUP_MAX_FRAMES);
    }
}

int main(void)
{
    static const unsigned chunks[] = {1, 2, 3, 7, 64};
    unsigned runs = 0;
    for (unsigned rx = 0; rx < 5; rx++) {
        for (unsigned tx = 0; tx < 5; tx++) {
            run_good(chunks[rx], chunks[tx], (rx + tx) % 3 ? 0 : 3, 0, 1801, "hmtnv20.b10a", false);
            run_good(chunks[rx], chunks[tx], 0, 3, 1801, "hmtnv20.b10a", (rx + tx) & 1);
            runs += 2;
        }
    }
    run_good(64, 64, 0, 0, 972, "hmtnv20.b10a", true);
    run_good(64, 64, 0, 1, 243, "hmtnv20.b10a", false);
    run_good(64, 64, 0, 3, 28, "hmtnv20.b10a", false);
    runs += 3;
    test_failures();
    test_fuzz();
    test_baud();
    assert(fuxi_bt_setup_name_valid("hmtbtfw20.tlv") && !fuxi_bt_setup_name_valid("../x") &&
           !fuxi_bt_setup_name_valid(".hidden") && !fuxi_bt_setup_name_valid("a/b") &&
           !fuxi_bt_setup_name_valid("") && !fuxi_bt_setup_name_valid("0123456789012345678901234"));
    assert(!strcmp(fuxi_bt_setup_stage_name(FUXI_BT_SETUP_DONE), "done") &&
           !strcmp(fuxi_bt_setup_stage_name(FUXI_BT_SETUP_BAUD), "baud") &&
           !strcmp(fuxi_bt_setup_stage_name(FUXI_BT_SETUP_NOLOG), "nolog") &&
           !strcmp(fuxi_bt_setup_stage_name((enum fuxi_bt_setup_stage)99), "unknown"));
    printf("PASS bt setup: %u good runs (RX/TX chunking, FIFO stalls, download modes 0/1/3, vendor noise), "
           "NVM fallback names, 6 controller faults, 8 malformed inputs, 3000 fuzz trials\n", runs);
    return 0;
}
'''


def compiler_configuration(explicit):
    if explicit:
        return explicit
    for name in ("clang", "gcc", "cc"):
        compiler = shutil.which(name)
        if compiler:
            return [compiler]
    raise SystemExit("A C11 compiler (clang or gcc) is required. Pass --cc COMPILER [ARG ...].")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--cc", nargs="+")
    args = parser.parse_args()
    compiler = compiler_configuration(args.cc)
    with tempfile.TemporaryDirectory(prefix="fuxi-bt-setup-") as temporary:
        directory = Path(temporary)
        harness = directory / "test.c"
        harness.write_text(HARNESS, encoding="utf-8")
        executable = directory / ("test.exe" if os.name == "nt" else "test")
        command = compiler + [
            "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-I", str(SOURCE), str(SOURCE / "fuxi_bt.c"), str(SOURCE / "fuxi_bt_setup.c"), str(harness),
            "-o", str(executable)
        ]
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
        subprocess.run(command, cwd=directory, check=True)
        subprocess.run([str(executable)], cwd=directory, check=True, timeout=300)
        print(f"Compiler {compiler[0]}")
        print(f"Sanitizers {'ASan/UBSan' if args.sanitize else 'disabled'}")


if __name__ == "__main__":
    main()
