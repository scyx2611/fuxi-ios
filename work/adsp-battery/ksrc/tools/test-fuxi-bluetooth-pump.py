#!/usr/bin/env python3
"""Compile and run the WCN7850 resident UART pump and H4 tracer (fuxi,bt=5) against a simulated GENI FIFO."""

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
#include "fuxi_bt_pump.h"

#define WIRE_CAP (1u << 18)

/* 模擬 GENI FIFO 的寫入規則：第一次呼叫開始一筆 ≤1024 bytes 的傳送，之後必須剛好帶剩餘量；
 * 每次最多收 budget bytes；整筆收完後要過 flight 步才算送完，期間新傳送回 AGAIN。 */
struct sim_uart {
    uint8_t wire[WIRE_CAP];      /* 控制器實際收到的 bytes */
    size_t wire_size;
    unsigned tx_remaining;
    unsigned in_flight;
    unsigned flight;
    unsigned budget;
    bool owns;
    uint8_t rx[WIRE_CAP];        /* 控制器要送給主機的 bytes */
    size_t rx_head, rx_tail;
    unsigned rx_chunk;
    unsigned reads, writes, reads_while_blocked;
    enum fuxi_bt_status fail_read, fail_write;
    unsigned fail_read_at, fail_write_at;
    bool sink_blocked;
    uint32_t random;
};

struct sim_sink {
    struct sim_uart *uart;
    uint8_t host_rx[WIRE_CAP];   /* 上層收到的 bytes */
    size_t host_rx_size;
    size_t deliver_cap;          /* 每次最多收多少；0 表示收不下 */
    bool discard;
    bool closed;                 /* 模擬沒有 RX 環：下一個封包開頭起丟棄 */
    bool accepting;              /* 目前封包的決定，只在 start 時改變（同 fuxi_bt_sk_receive） */
    const uint8_t *expect;       /* 分框測試：上層應依序看到的 bytes（不含除錯 ACL 與 desync byte） */
    const uint8_t *starts;       /* expect 每個位置是否為封包開頭 */
    size_t expect_size;
    size_t seen;
    size_t dropped;
    unsigned start_calls;
    uint8_t host_tx[WIRE_CAP];   /* 上層要送的 bytes */
    size_t host_tx_head, host_tx_tail;
    size_t take_cap;
};

struct trace_log {
    struct fuxi_bt_h4_record records[4096];
    unsigned count;
};

static bool same_record(const struct fuxi_bt_h4_record *a, const struct fuxi_bt_h4_record *b)
{
    return a->direction == b->direction && a->type == b->type && a->length == b->length &&
        a->opcode == b->opcode && a->handle == b->handle && a->event == b->event &&
        a->subevent == b->subevent && a->status == b->status && a->has_opcode == b->has_opcode &&
        a->has_status == b->has_status && a->keep_size == b->keep_size &&
        !memcmp(a->keep, b->keep, sizeof(a->keep));
}

static uint32_t next_random(uint32_t *state)
{
    *state = *state * 1103515245u + 12345u;
    return *state >> 8;
}

static void uart_tick(struct sim_uart *uart)
{
    if (uart->owns && !uart->tx_remaining) {
        if (uart->in_flight) {
            uart->in_flight--;
        }
        if (!uart->in_flight) {
            uart->owns = false;
        }
    }
}

static enum fuxi_bt_status sim_write(void *context, const uint8_t *bytes, size_t size,
    size_t *accepted)
{
    struct sim_uart *uart = context;
    size_t count;
    *accepted = 0;
    uart->writes++;
    if (uart->fail_write && uart->writes >= uart->fail_write_at) {
        return uart->fail_write;
    }
    assert(size <= 1024);
    assert(bytes || !size);
    uart_tick(uart);
    if (!size) {
        return FUXI_BT_OK;
    }
    if (uart->owns && !uart->tx_remaining) {
        return FUXI_BT_AGAIN;
    }
    if (uart->owns) {
        assert(size == uart->tx_remaining);
    } else {
        uart->owns = true;
        uart->tx_remaining = (unsigned)size;
        uart->in_flight = uart->flight;
    }
    count = size < uart->budget ? size : uart->budget;
    if (uart->random && (next_random(&uart->random) & 3) == 0) {
        return FUXI_BT_AGAIN;    /* FIFO 滿 */
    }
    assert(uart->wire_size + count <= WIRE_CAP);
    memcpy(uart->wire + uart->wire_size, bytes, count);
    uart->wire_size += count;
    uart->tx_remaining -= (unsigned)count;
    *accepted = count;
    if (!uart->tx_remaining && !uart->in_flight) {
        uart->owns = false;
    }
    return FUXI_BT_OK;
}

static enum fuxi_bt_status sim_read(void *context, uint8_t *bytes, size_t capacity,
    size_t *received)
{
    struct sim_uart *uart = context;
    size_t count = uart->rx_tail - uart->rx_head;
    *received = 0;
    uart->reads++;
    if (uart->sink_blocked) {
        uart->reads_while_blocked++;
    }
    if (uart->fail_read && uart->reads >= uart->fail_read_at) {
        return uart->fail_read;
    }
    uart_tick(uart);
    if (!count) {
        return FUXI_BT_AGAIN;
    }
    if (count > capacity) count = capacity;
    if (uart->rx_chunk && count > uart->rx_chunk) count = uart->rx_chunk;
    if (uart->random) {
        const size_t limit = 1 + next_random(&uart->random) % count;
        count = limit;
    }
    memcpy(bytes, uart->rx + uart->rx_head, count);
    uart->rx_head += count;
    *received = count;
    return FUXI_BT_OK;
}

static enum fuxi_bt_status sim_unused_acquire(void *context, unsigned baud)
{
    (void)context;
    (void)baud;
    return FUXI_BT_OK;
}

static enum fuxi_bt_status sim_unused_release(void *context)
{
    (void)context;
    return FUXI_BT_OK;
}

static size_t sink_deliver(void *context, const uint8_t *bytes, size_t size, bool start)
{
    struct sim_sink *sink = context;
    size_t count = size;
    assert(size);
    if (sink->expect) {
        /* 交出的每一段都必須是期望串流的下一段，且 start 剛好落在封包開頭 */
        assert(sink->seen + size <= sink->expect_size);
        assert(!memcmp(bytes, sink->expect + sink->seen, size));
        assert(start == (sink->starts[sink->seen] != 0));
    }
    if (start) {
        sink->accepting = !sink->closed;
        sink->start_calls++;
    }
    if (sink->discard || !sink->accepting) {
        sink->seen += size;
        sink->dropped += size;
        return size;
    }
    if (count > sink->deliver_cap) count = sink->deliver_cap;
    sink->seen += count;
    sink->uart->sink_blocked = count < size;
    assert(sink->host_rx_size + count <= WIRE_CAP);
    memcpy(sink->host_rx + sink->host_rx_size, bytes, count);
    sink->host_rx_size += count;
    return count;
}

static size_t sink_take(void *context, uint8_t *bytes, size_t capacity)
{
    struct sim_sink *sink = context;
    size_t count = sink->host_tx_tail - sink->host_tx_head;
    if (count > capacity) count = capacity;
    if (sink->take_cap && count > sink->take_cap) count = sink->take_cap;
    memcpy(bytes, sink->host_tx + sink->host_tx_head, count);
    sink->host_tx_head += count;
    return count;
}

static void trace_record(void *context, const struct fuxi_bt_h4_record *record)
{
    struct trace_log *log = context;
    if (log->count < sizeof(log->records) / sizeof(log->records[0])) {
        log->records[log->count] = *record;
    }
    log->count++;
}

struct world {
    struct sim_uart uart;
    struct sim_sink sink;
    struct trace_log log;
    struct fuxi_bt_uart_ops ops;
    struct fuxi_bt_pump_sink pump_sink;
    struct fuxi_bt_pump pump;
};

static void world_init(struct world *w)
{
    memset(w, 0, sizeof(*w));
    w->uart.flight = 3;
    w->uart.budget = 64;
    w->sink.uart = &w->uart;
    w->sink.deliver_cap = WIRE_CAP;
    w->ops.context = &w->uart;
    w->ops.acquire = sim_unused_acquire;
    w->ops.write = sim_write;
    w->ops.read = sim_read;
    w->ops.release = sim_unused_release;
    w->pump_sink.context = &w->sink;
    w->pump_sink.deliver = sink_deliver;
    w->pump_sink.take = sink_take;
    assert(fuxi_bt_pump_init(&w->pump, &w->ops, &w->pump_sink, trace_record, &w->log) == FUXI_BT_OK);
}

static void host_send(struct world *w, const uint8_t *bytes, size_t size)
{
    assert(w->sink.host_tx_tail + size <= WIRE_CAP);
    memcpy(w->sink.host_tx + w->sink.host_tx_tail, bytes, size);
    w->sink.host_tx_tail += size;
}

static void controller_send(struct world *w, const uint8_t *bytes, size_t size)
{
    assert(w->uart.rx_tail + size <= WIRE_CAP);
    memcpy(w->uart.rx + w->uart.rx_tail, bytes, size);
    w->uart.rx_tail += size;
}

static void run_until_idle(struct world *w, unsigned limit)
{
    for (unsigned step = 0; step < limit; step++) {
        struct fuxi_bt_pump_activity activity;
        assert(fuxi_bt_pump_step(&w->pump, &activity) == FUXI_BT_OK);
        if (w->sink.host_tx_head == w->sink.host_tx_tail && !activity.tx_pending &&
            w->pump.tx_offset == w->pump.tx_size && w->uart.rx_head == w->uart.rx_tail &&
            !w->pump.rx_size && !w->pump.host.hold_active && !w->uart.owns) {
            return;
        }
    }
    assert(!"pump did not settle");
}

static void test_reset_roundtrip(void)
{
    static const uint8_t reset[] = {0x01, 0x03, 0x0c, 0x00};
    static const uint8_t complete[] = {0x04, 0x0e, 0x04, 0x01, 0x03, 0x0c, 0x00};
    static const uint8_t status[] = {0x04, 0x0f, 0x04, 0x00, 0x01, 0x01, 0x04};
    struct world *w = calloc(1, sizeof(*w));
    assert(w);
    world_init(w);
    host_send(w, reset, sizeof(reset));
    controller_send(w, complete, sizeof(complete));
    controller_send(w, status, sizeof(status));
    run_until_idle(w, 100);
    assert(w->uart.wire_size == sizeof(reset) && !memcmp(w->uart.wire, reset, sizeof(reset)));
    assert(w->sink.host_rx_size == sizeof(complete) + sizeof(status));
    assert(!memcmp(w->sink.host_rx, complete, sizeof(complete)));
    assert(w->log.count == 3);
    for (unsigned index = 0; index < w->log.count; index++) {
        const struct fuxi_bt_h4_record *r = &w->log.records[index];
        if (r->type == FUXI_BT_H4_COMMAND) {
            assert(r->direction == FUXI_BT_H4_TO_CONTROLLER && r->opcode == 0x0c03 && !r->length);
        } else if (r->event == 0x0e) {
            assert(r->direction == FUXI_BT_H4_TO_HOST && r->has_opcode && r->opcode == 0x0c03);
            assert(r->has_status && r->status == 0);
        } else {
            assert(r->event == 0x0f && r->has_status && !r->status && r->opcode == 0x0401);
        }
    }
    assert(w->pump.to_controller.packets[FUXI_BT_H4_COMMAND] == 1);
    assert(w->pump.to_host.packets[FUXI_BT_H4_EVENT] == 2);
    assert(w->pump.tx_bytes == 4 && w->pump.rx_bytes == 14 && w->pump.tx_chunks == 1);
    free(w);
}

static size_t make_acl(uint8_t *out, uint16_t handle, uint16_t length, uint8_t seed)
{
    out[0] = FUXI_BT_H4_ACL;
    out[1] = (uint8_t)handle;
    out[2] = (uint8_t)(handle >> 8);
    out[3] = (uint8_t)length;
    out[4] = (uint8_t)(length >> 8);
    for (uint16_t index = 0; index < length; index++) {
        out[5 + index] = (uint8_t)(seed + index * 7);
    }
    return 5u + length;
}

static void test_bulk_ordering(uint32_t seed, unsigned take_cap, unsigned budget, unsigned flight)
{
    struct world *w = calloc(1, sizeof(*w));
    uint8_t packet[1100];
    size_t host_total = 0, controller_total = 0;
    unsigned host_packets = 0, controller_packets = 0;
    assert(w);
    world_init(w);
    w->uart.random = seed;
    w->uart.budget = budget;
    w->uart.flight = flight;
    w->sink.take_cap = take_cap;
    w->sink.deliver_cap = 97;
    for (unsigned index = 0; index < 40; index++) {
        const uint16_t length = (uint16_t)(1 + (index * 131 + seed) % 1021);
        const size_t size = make_acl(packet, 0x2001, length, (uint8_t)index);
        host_send(w, packet, size);
        host_total += size;
        host_packets++;
    }
    for (unsigned index = 0; index < 30; index++) {
        const uint16_t length = (uint16_t)(index * 37 % 1021);
        const size_t size = make_acl(packet, 0x0001, length, (uint8_t)(index + 100));
        controller_send(w, packet, size);
        controller_total += size;
        controller_packets++;
    }
    run_until_idle(w, 200000);
    assert(w->uart.wire_size == host_total);
    assert(!memcmp(w->uart.wire, w->sink.host_tx, host_total));
    assert(w->sink.host_rx_size == controller_total);
    assert(!memcmp(w->sink.host_rx, w->uart.rx, controller_total));
    assert(w->pump.to_controller.packets[FUXI_BT_H4_ACL] == host_packets);
    assert(w->pump.to_host.packets[FUXI_BT_H4_ACL] == controller_packets);
    assert(!w->pump.to_controller.desync_bytes && !w->pump.to_host.desync_bytes);
    assert(w->pump.tx_bytes == host_total && w->pump.rx_delivered == controller_total);
    assert(w->pump.tx_chunks >= host_total / FUXI_BT_PUMP_TX_CHUNK);
    free(w);
}

static void test_backpressure(void)
{
    struct world *w = calloc(1, sizeof(*w));
    uint8_t packet[600];
    size_t total = 0;
    assert(w);
    world_init(w);
    for (unsigned index = 0; index < 8; index++) {
        const size_t size = make_acl(packet, 0x0002, 500, (uint8_t)index);
        controller_send(w, packet, size);
        total += size;
    }
    w->sink.deliver_cap = 0;
    for (unsigned step = 0; step < 50; step++) {
        struct fuxi_bt_pump_activity activity;
        assert(fuxi_bt_pump_step(&w->pump, &activity) == FUXI_BT_OK);
        if (step) {
            assert(activity.rx_blocked && !activity.rx_received);
        }
    }
    /* 被擋住期間只讀過第一次，之後不再讀 UART（硬體 RFR 讓控制器停送） */
    assert(w->uart.reads == 1 && w->uart.reads_while_blocked == 0);
    assert(w->pump.rx_stalls == 49);
    assert(w->uart.rx_head == FUXI_BT_PUMP_RX_CHUNK);
    w->sink.deliver_cap = 13;
    run_until_idle(w, 100000);
    assert(w->sink.host_rx_size == total && !memcmp(w->sink.host_rx, w->uart.rx, total));
    assert(w->uart.reads_while_blocked == 0);
    free(w);
}

static void test_discard_without_client(void)
{
    struct world *w = calloc(1, sizeof(*w));
    static const uint8_t complete[] = {0x04, 0x0e, 0x04, 0x01, 0x03, 0x0c, 0x00};
    assert(w);
    world_init(w);
    w->sink.discard = true;
    for (unsigned index = 0; index < 100; index++) {
        controller_send(w, complete, sizeof(complete));
    }
    run_until_idle(w, 1000);
    assert(!w->sink.host_rx_size && w->pump.rx_delivered == 700 && w->pump.to_host.packets[4] == 100);
    free(w);
}

/* 依期望串流核對：每段 host_rx 都要從封包開頭起算、沒有 desync；final 時也要在封包邊界結束 */
static void check_segment(const uint8_t *bytes, size_t size, bool final)
{
    struct fuxi_bt_h4_parser parser;
    fuxi_bt_h4_parser_init(&parser, FUXI_BT_H4_TO_HOST);
    fuxi_bt_h4_feed(&parser, bytes, size, NULL, NULL);
    assert(!parser.desync_bytes);
    if (final) {
        assert(!parser.type);
    }
}

static size_t make_packet(uint8_t *out, uint32_t *state, bool *debug)
{
    const unsigned kind = next_random(state) % 7;
    size_t length;
    *debug = false;
    switch (kind) {
    case 0:
    case 1:
        length = next_random(state) % 256;
        out[0] = FUXI_BT_H4_EVENT;
        out[1] = (uint8_t)(1 + next_random(state) % 255);   /* event code 0 保留不用 */
        out[2] = (uint8_t)length;
        for (size_t index = 0; index < length; index++) out[3 + index] = (uint8_t)next_random(state);
        return 3 + length;
    case 2:
        *debug = true;
        return make_acl(out, FUXI_BT_QCA_DEBUG_HANDLE, (uint16_t)(next_random(state) % 300), (uint8_t)kind);
    case 3:
        /* handle 0xedc 但 PB 旗標不同：不是除錯日誌 */
        return make_acl(out, 0x0edc, (uint16_t)(next_random(state) % 40), (uint8_t)kind);
    case 4:
        return make_acl(out, 0x2001, (uint16_t)(next_random(state) % 700), (uint8_t)kind);
    case 5:
        length = next_random(state) % 60;
        out[0] = FUXI_BT_H4_ISO;
        out[1] = 0x01;
        out[2] = 0x20;
        out[3] = (uint8_t)length;
        out[4] = (uint8_t)(0xc0 | (length >> 8));   /* 高 2 位是旗標，不算長度 */
        for (size_t index = 0; index < length; index++) out[5 + index] = (uint8_t)next_random(state);
        return 5 + length;
    default:
        length = next_random(state) % 30;
        out[0] = FUXI_BT_H4_SCO;
        out[1] = 0x03;
        out[2] = 0x00;
        out[3] = (uint8_t)length;
        for (size_t index = 0; index < length; index++) out[4 + index] = (uint8_t)next_random(state);
        return 4 + length;
    }
}

/* 控制器端夾雜除錯 ACL 與 IBS byte；上層在任意時機開關、任意收量下都只收到完整封包 */
static void test_framing(uint32_t seed, unsigned rx_chunk, size_t deliver_cap, unsigned toggle)
{
    struct world *w = calloc(1, sizeof(*w));
    uint8_t *expect = malloc(WIRE_CAP), *starts = calloc(1, WIRE_CAP), *segment = malloc(WIRE_CAP);
    uint8_t packet[1100];
    uint32_t state = seed * 2654435761u + 1;
    size_t expect_size = 0, segment_start = 0;
    unsigned debug_packets = 0, framed_packets = 0, desync = 0, disconnects = 0;
    uint64_t debug_bytes = 0;
    assert(w && expect && starts && segment);
    world_init(w);
    w->uart.random = seed;
    w->uart.rx_chunk = rx_chunk;
    w->sink.deliver_cap = deliver_cap;
    w->sink.expect = expect;
    w->sink.starts = starts;
    w->sink.closed = toggle != 0;
    while (w->uart.rx_tail < WIRE_CAP - 4096) {
        bool debug;
        size_t size;
        while ((next_random(&state) & 3) == 0) {
            static const uint8_t noise[] = {0xfc, 0xfd, 0xfe, 0x00, 0x09};
            const uint8_t byte = noise[next_random(&state) % sizeof(noise)];
            controller_send(w, &byte, 1);
            desync++;
        }
        size = make_packet(packet, &state, &debug);
        controller_send(w, packet, size);
        if (debug) {
            debug_packets++;
            debug_bytes += size;
        } else {
            starts[expect_size] = 1;
            memcpy(expect + expect_size, packet, size);
            expect_size += size;
            framed_packets++;
        }
        if (framed_packets + debug_packets >= 400) break;
    }
    w->sink.expect_size = expect_size;
    for (unsigned step = 0; step < 2000000; step++) {
        struct fuxi_bt_pump_activity activity;
        assert(fuxi_bt_pump_step(&w->pump, &activity) == FUXI_BT_OK);
        if (toggle && next_random(&state) % toggle == 0) {
            if (!w->sink.closed && (next_random(&state) & 1)) {
                /* 斷線（fuxi_bt_sk 的 pre_disconnect）：目前封包立刻停收，之前那段單獨核對 */
                check_segment(w->sink.host_rx + segment_start, w->sink.host_rx_size - segment_start, false);
                segment_start = w->sink.host_rx_size;
                w->sink.accepting = false;
                disconnects++;
            }
            w->sink.closed = !w->sink.closed;
        }
        if (w->uart.rx_head == w->uart.rx_tail && !w->pump.rx_size && !w->pump.host.header_need) {
            break;
        }
    }
    assert(w->uart.rx_head == w->uart.rx_tail && !w->pump.rx_size && !w->pump.host.header_need);
    assert(w->sink.seen == expect_size);
    assert(w->sink.host_rx_size + w->sink.dropped == expect_size);
    assert(w->pump.host.packets == framed_packets);
    assert(w->pump.host.debug_packets == debug_packets && w->pump.host.debug_bytes == debug_bytes);
    assert(w->pump.host.desync_bytes == desync);
    assert(w->sink.start_calls >= framed_packets);
    check_segment(w->sink.host_rx + segment_start, w->sink.host_rx_size - segment_start, true);
    if (!toggle) {
        assert(!w->sink.dropped && w->sink.host_rx_size == expect_size);
        assert(!memcmp(w->sink.host_rx, expect, expect_size));
    } else {
        assert(w->sink.dropped && w->sink.host_rx_size);
    }
    (void)disconnects;
    free(segment);
    free(starts);
    free(expect);
    free(w);
}

/* 通道在封包中間接上：那個封包整個不交出，之後的封包完整交出 */
static void test_framing_mid_packet_open(void)
{
    struct world *w = calloc(1, sizeof(*w));
    uint8_t first[300], second[40];
    const size_t first_size = make_acl(first, 0x2001, 295, 1);
    const size_t second_size = make_acl(second, 0x2001, 35, 2);
    static const uint8_t debug[] = {0x02, 0xdc, 0x2e, 0x02, 0x00, 0xaa, 0xbb};
    assert(w);
    world_init(w);
    w->uart.rx_chunk = 10;
    w->sink.closed = true;
    controller_send(w, first, first_size);
    controller_send(w, debug, sizeof(debug));
    controller_send(w, (const uint8_t *)"\xfd", 1);
    controller_send(w, second, second_size);
    for (unsigned step = 0; step < 5; step++) {
        assert(fuxi_bt_pump_step(&w->pump, NULL) == FUXI_BT_OK);
    }
    w->sink.closed = false;
    run_until_idle(w, 1000);
    assert(w->sink.host_rx_size == second_size && !memcmp(w->sink.host_rx, second, second_size));
    assert(w->sink.dropped == first_size);
    assert(w->pump.host.packets == 2 && w->pump.host.debug_packets == 1 && w->pump.host.debug_bytes == sizeof(debug));
    assert(w->pump.host.desync_bytes == 1);
    free(w);
}

static void step_n(struct world *w, unsigned count)
{
    for (unsigned step = 0; step < count; step++) {
        assert(fuxi_bt_pump_step(&w->pump, NULL) == FUXI_BT_OK);
    }
}

/* fuxi,bt-ibs：控制器 WAKE_IND 回 WAKE_ACK；主機第一次送之前先 WAKE_IND，收到 WAKE_ACK 才送，等滿照送；
 * 關閉時 IBS byte 只計數。 */
static void test_ibs(void)
{
    static const uint8_t reset[] = {0x01, 0x03, 0x0c, 0x00};
    static const uint8_t complete[] = {0x04, 0x0e, 0x04, 0x01, 0x03, 0x0c, 0x00};
    const unsigned wakes = FUXI_BT_IBS_WAIT_STEPS / FUXI_BT_IBS_RETRY_STEPS;
    struct world *w = calloc(1, sizeof(*w));
    assert(w);
    world_init(w);
    w->pump.ibs = true;
    controller_send(w, (const uint8_t *)"\xfd", 1);
    controller_send(w, complete, sizeof(complete));
    step_n(w, 20);
    assert(w->uart.wire_size == 1 && w->uart.wire[0] == FUXI_BT_IBS_WAKE_ACK);
    assert(w->pump.ibs_acks_sent == 1 && w->pump.host.ibs_bytes[1] == 1);
    assert(w->sink.host_rx_size == sizeof(complete) && !memcmp(w->sink.host_rx, complete, sizeof(complete)));

    /* 主機要送：WAKE_IND 先上線，WAKE_ACK 之前命令留在泵內 */
    host_send(w, reset, sizeof(reset));
    step_n(w, 10);
    assert(w->uart.wire_size == 2 && w->uart.wire[1] == FUXI_BT_IBS_WAKE_IND && w->pump.ibs_wakes_sent == 1);
    controller_send(w, (const uint8_t *)"\xfc", 1);
    step_n(w, 20);
    assert(w->uart.wire_size == 2 + sizeof(reset) && !memcmp(w->uart.wire + 2, reset, sizeof(reset)));
    assert(w->pump.ibs_tx_awake && !w->pump.ibs_wake_timeouts && w->pump.host.ibs_bytes[0] == 1);
    /* 醒著之後直接送 */
    host_send(w, reset, sizeof(reset));
    step_n(w, 20);
    assert(w->uart.wire_size == 2 + 2 * sizeof(reset) && w->pump.ibs_wakes_sent == 1);
    assert(w->pump.host.desync_bytes == 2 && w->pump.host.desync_recent_pos == 2);
    assert(w->pump.host.desync_recent[0] == 0xfd && w->pump.host.desync_recent[1] == 0xfc);
    free(w);

    /* 等不到 WAKE_ACK：每 RETRY 步重送 WAKE_IND，等滿照送 */
    w = calloc(1, sizeof(*w));
    assert(w);
    world_init(w);
    w->pump.ibs = true;
    host_send(w, reset, sizeof(reset));
    step_n(w, FUXI_BT_IBS_WAIT_STEPS + 20);
    assert(w->pump.ibs_wake_timeouts == 1 && w->pump.ibs_wakes_sent == wakes);
    assert(w->uart.wire_size == wakes + sizeof(reset));
    for (unsigned index = 0; index < wakes; index++) {
        assert(w->uart.wire[index] == FUXI_BT_IBS_WAKE_IND);
    }
    assert(!memcmp(w->uart.wire + wakes, reset, sizeof(reset)));
    free(w);

    /* 關閉：0xfd 只計數，命令直接送 */
    w = calloc(1, sizeof(*w));
    assert(w);
    world_init(w);
    controller_send(w, (const uint8_t *)"\xfd", 1);
    host_send(w, reset, sizeof(reset));
    run_until_idle(w, 100);
    assert(w->uart.wire_size == sizeof(reset) && !memcmp(w->uart.wire, reset, sizeof(reset)));
    assert(w->pump.host.ibs_bytes[1] == 1 && !w->pump.ibs_acks_sent && !w->pump.ibs_wakes_sent);
    free(w);
}

/* fuxi,bt-optional：清單內 opcode 的 Command Complete 失敗改成 0；其他 opcode、Command Status、
 * 成功的回應不動；任意 RX 切塊與交付上限結果相同；清單空時原樣交出。 */
static void test_optional(void)
{
    static const uint8_t stream[] = {
        0x04, 0x0e, 0x04, 0x01, 0x5b, 0x0c, 0x01,   /* 0x0c5b 失敗 → 改 0 */
        0x04, 0x0e, 0x04, 0x01, 0x03, 0x0c, 0x01,   /* 0x0c03 不在清單 */
        0x04, 0x0f, 0x04, 0x01, 0x01, 0x5b, 0x0c,   /* Command Status 不動 */
        0x04, 0x0e, 0x04, 0x01, 0x5b, 0x0c, 0x00,   /* 已成功 */
        0x04, 0x0e, 0x05, 0x01, 0x5b, 0x0c, 0x11, 0x22, /* 帶多餘參數的失敗 → 只改 status */
    };
    uint8_t expected[sizeof(stream)];
    memcpy(expected, stream, sizeof(stream));
    expected[6] = 0;
    expected[34] = 0;
    for (unsigned chunk = 0; chunk <= 9; chunk++) {
        for (size_t cap = 1; cap <= 4; cap++) {
            struct world *w = calloc(1, sizeof(*w));
            assert(w);
            world_init(w);
            w->uart.rx_chunk = chunk;
            w->sink.deliver_cap = cap == 4 ? WIRE_CAP : cap;
            w->pump.optional[0] = 0x0c31;
            w->pump.optional[1] = 0x0c5b;
            w->pump.optional_count = 2;
            controller_send(w, stream, sizeof(stream));
            run_until_idle(w, 4000);
            assert(w->sink.host_rx_size == sizeof(expected));
            assert(!memcmp(w->sink.host_rx, expected, sizeof(expected)));
            assert(w->pump.optional_patched == 2);
            assert(w->pump.optional_last_opcode == 0x0c5b && w->pump.optional_last_status == 0x11);
            free(w);
        }
    }
    struct world *w = calloc(1, sizeof(*w));
    assert(w);
    world_init(w);
    controller_send(w, stream, sizeof(stream));
    run_until_idle(w, 4000);
    assert(w->sink.host_rx_size == sizeof(stream) && !memcmp(w->sink.host_rx, stream, sizeof(stream)));
    assert(!w->pump.optional_patched);
    free(w);

    /* 0xfc00：所有 vendor opcode 的失敗都改 0，標準命令不受影響 */
    static const uint8_t vendor[] = {
        0x04, 0x0e, 0x04, 0x01, 0xe2, 0xfc, 0x01,   /* 0xfce2 → 改 0 */
        0x04, 0x0e, 0x04, 0x01, 0x3c, 0xfd, 0x01,   /* 0xfd3c → 改 0 */
        0x04, 0x0e, 0x05, 0x01, 0x07, 0x20, 0x0c, 0x00, /* 0x2007 不在清單 */
    };
    uint8_t vendor_expected[sizeof(vendor)];
    memcpy(vendor_expected, vendor, sizeof(vendor));
    vendor_expected[6] = 0;
    vendor_expected[13] = 0;
    w = calloc(1, sizeof(*w));
    assert(w);
    world_init(w);
    w->uart.rx_chunk = 3;
    w->pump.optional[0] = FUXI_BT_OPTIONAL_VENDOR;
    w->pump.optional_count = 1;
    controller_send(w, vendor, sizeof(vendor));
    run_until_idle(w, 4000);
    assert(w->sink.host_rx_size == sizeof(vendor_expected));
    assert(!memcmp(w->sink.host_rx, vendor_expected, sizeof(vendor_expected)));
    assert(w->pump.optional_patched == 2 && w->pump.optional_last_opcode == 0xfd3c);
    free(w);
}

/* fuxi,bt-pad：清單內 opcode 只有 status 的 Command Complete 補「子命令回聲＋0」、長度 4 → 6，
 * 並先照 fuxi,bt-optional 改 status；帶參數的回應、其他 opcode、ACL 不動；任意切塊與交付上限結果相同；
 * 清單空時原樣交出。 */
static void test_pad(void)
{
    /* BD_VSC_OLYMPIC_ANTENNA_PREF_B：0xfce2 子命令 0x15 */
    static const uint8_t antenna[] = {0x01, 0xe2, 0xfc, 0x0a, 0x15, 0x00, 0x00, 0x00, 0xff, 0x0f, 0x02,
        0x00, 0x00, 0x00};
    static const uint8_t settings[] = {0x01, 0xe2, 0xfc, 0x03, 0x0b, 0x01, 0x02};
    static const uint8_t reset[] = {0x01, 0x03, 0x0c, 0x00};
    static const uint8_t stream[] = {
        0x04, 0x0e, 0x04, 0x01, 0xe2, 0xfc, 0x01,         /* 0xfce2 失敗 → 改 0、補 0x0b 00 */
        0x02, 0x01, 0x20, 0x02, 0x00, 0xaa, 0xbb,         /* ACL 不動 */
        0x04, 0x0e, 0x04, 0x01, 0x03, 0x0c, 0x00,         /* 0x0c03 不在清單 */
        0x04, 0x0e, 0x05, 0x01, 0xe2, 0xfc, 0x01, 0x15,   /* 已帶參數：只改 status */
        0x04, 0x0f, 0x04, 0x00, 0x01, 0xe2, 0xfc,         /* Command Status 不動 */
        0x04, 0x0e, 0x04, 0x01, 0xe2, 0xfc, 0x00,         /* 成功也補，子命令取最近一筆 0x15 */
    };
    static const uint8_t expected[] = {
        0x04, 0x0e, 0x06, 0x01, 0xe2, 0xfc, 0x00, 0x0b, 0x00,
        0x02, 0x01, 0x20, 0x02, 0x00, 0xaa, 0xbb,
        0x04, 0x0e, 0x04, 0x01, 0x03, 0x0c, 0x00,
        0x04, 0x0e, 0x05, 0x01, 0xe2, 0xfc, 0x00, 0x15,
        0x04, 0x0f, 0x04, 0x00, 0x01, 0xe2, 0xfc,
        0x04, 0x0e, 0x06, 0x01, 0xe2, 0xfc, 0x00, 0x15, 0x00,
    };
    for (unsigned chunk = 0; chunk <= 9; chunk++) {
        for (size_t cap = 1; cap <= 4; cap++) {
            struct world *w = calloc(1, sizeof(*w));
            assert(w);
            world_init(w);
            w->uart.rx_chunk = chunk;
            w->sink.deliver_cap = cap == 4 ? WIRE_CAP : cap;
            w->pump.optional[0] = FUXI_BT_OPTIONAL_VENDOR;
            w->pump.optional_count = 1;
            w->pump.pad[0] = 0xfce2;
            w->pump.pad_count = 1;
            host_send(w, settings, sizeof(settings));
            run_until_idle(w, 4000);
            controller_send(w, stream, 21);
            run_until_idle(w, 4000);
            host_send(w, reset, sizeof(reset));
            host_send(w, antenna, sizeof(antenna));
            run_until_idle(w, 4000);
            controller_send(w, stream + 21, sizeof(stream) - 21);
            run_until_idle(w, 4000);
            assert(w->uart.wire_size == sizeof(settings) + sizeof(reset) + sizeof(antenna));
            assert(w->sink.host_rx_size == sizeof(expected));
            assert(!memcmp(w->sink.host_rx, expected, sizeof(expected)));
            assert(w->pump.padded == 2 && w->pump.pad_last_opcode == 0xfce2 && w->pump.pad_last_subop == 0x15);
            assert(w->pump.optional_patched == 2 && !w->pump.host.hold_active);
            assert(w->pump.host.packets == 6);
            /* 外部 trace 照常收到 TX 命令 */
            assert(w->pump.to_controller.packets[FUXI_BT_H4_COMMAND] == 3);
            free(w);
        }
    }
    /* 清單空：原樣交出 */
    struct world *w = calloc(1, sizeof(*w));
    assert(w);
    world_init(w);
    w->uart.rx_chunk = 2;
    host_send(w, antenna, sizeof(antenna));
    run_until_idle(w, 4000);
    controller_send(w, stream, sizeof(stream));
    run_until_idle(w, 4000);
    assert(w->sink.host_rx_size == sizeof(stream) && !memcmp(w->sink.host_rx, stream, sizeof(stream)));
    assert(!w->pump.padded && !w->pump.optional_patched);
    free(w);

    /* 交付途中上層收不下：hold 留在泵內，之後照順序交出；沒有對應命令時子命令補 0 */
    static const uint8_t lone[] = {0x04, 0x0e, 0x04, 0x01, 0xe2, 0xfc, 0x0c, 0x04, 0x0e, 0x04, 0x01, 0x03,
        0x0c, 0x00};
    static const uint8_t lone_expected[] = {0x04, 0x0e, 0x06, 0x01, 0xe2, 0xfc, 0x0c, 0x00, 0x00, 0x04,
        0x0e, 0x04, 0x01, 0x03, 0x0c, 0x00};
    w = calloc(1, sizeof(*w));
    assert(w);
    world_init(w);
    w->pump.pad[0] = FUXI_BT_OPTIONAL_VENDOR;
    w->pump.pad_count = 1;
    w->sink.deliver_cap = 0;
    controller_send(w, lone, sizeof(lone));
    step_n(w, 10);
    assert(!w->sink.host_rx_size && w->pump.host.hold_active);
    w->sink.deliver_cap = 3;
    run_until_idle(w, 4000);
    assert(w->sink.host_rx_size == sizeof(lone_expected));
    assert(!memcmp(w->sink.host_rx, lone_expected, sizeof(lone_expected)));
    assert(w->pump.padded == 1 && w->pump.pad_last_subop == 0 && !w->pump.optional_patched);
    free(w);
}

/* fuxi,bt-mask：0x2003（LE Read Local Supported Features）回傳參數 offset 2（功能 byte 1）清 0x30，
 * 其他 opcode、Command Status、已清的位元不動；與 fuxi,bt-optional 同時開；任意切塊與交付上限結果相同；
 * 清單空時原樣交出。 */
static void test_mask(void)
{
    static const uint8_t stream[] = {
        0x04, 0x0e, 0x0c, 0x01, 0x03, 0x20, 0x00, 0xff, 0x7f, 0x01, 0x30, 0x60, 0x00, 0x00, 0x00, /* 清 0x30 */
        0x04, 0x0e, 0x04, 0x01, 0x5b, 0x0c, 0x01,                                           /* optional */
        0x04, 0x0e, 0x0c, 0x01, 0x03, 0x10, 0x00, 0xff, 0x7f, 0x01, 0x30, 0x60, 0x00, 0x00, 0x00, /* 0x1003 不動 */
        0x04, 0x0f, 0x04, 0x00, 0x01, 0x03, 0x20,                                           /* Command Status */
        0x04, 0x0e, 0x0c, 0x01, 0x03, 0x20, 0x00, 0xff, 0x0f, 0x01, 0x30, 0x60, 0x00, 0x00, 0x00, /* 已清 */
    };
    uint8_t expected[sizeof(stream)];
    memcpy(expected, stream, sizeof(stream));
    expected[8] = 0x4f;
    expected[21] = 0;
    for (unsigned chunk = 0; chunk <= 9; chunk++) {
        for (size_t cap = 1; cap <= 4; cap++) {
            for (unsigned optional = 0; optional <= 1; optional++) {
                struct world *w = calloc(1, sizeof(*w));
                assert(w);
                world_init(w);
                w->uart.rx_chunk = chunk;
                w->sink.deliver_cap = cap == 4 ? WIRE_CAP : cap;
                w->pump.optional[0] = 0x0c5b;
                w->pump.optional_count = optional;
                w->pump.mask[0] = 0x20030230u;
                w->pump.mask_count = 1;
                controller_send(w, stream, sizeof(stream));
                run_until_idle(w, 4000);
                assert(w->sink.host_rx_size == sizeof(stream));
                if (optional) {
                    assert(!memcmp(w->sink.host_rx, expected, sizeof(expected)));
                    assert(w->pump.optional_patched == 1);
                } else {
                    assert(!memcmp(w->sink.host_rx, expected, 21) && w->sink.host_rx[21] == 0x01 &&
                        !memcmp(w->sink.host_rx + 22, expected + 22, sizeof(expected) - 22));
                    assert(!w->pump.optional_patched);
                }
                assert(w->pump.masked == 1 && w->pump.mask_last_opcode == 0x2003);
                assert(w->pump.mask_last_offset == 2 && w->pump.mask_last_before == 0x7f &&
                    w->pump.mask_last_after == 0x4f);
                assert(w->pump.host.packets == 5);
                free(w);
            }
        }
    }
    struct world *w = calloc(1, sizeof(*w));
    assert(w);
    world_init(w);
    w->uart.rx_chunk = 5;
    controller_send(w, stream, sizeof(stream));
    run_until_idle(w, 4000);
    assert(w->sink.host_rx_size == sizeof(stream) && !memcmp(w->sink.host_rx, stream, sizeof(stream)));
    assert(!w->pump.masked);
    free(w);
}

/* fuxi,bt-txmask：送出命令的參數依規則清位元。0x200b 的 Own_Address_Type（參數 5）1→0、0x200d 的
 * （參數 12）1→0 但同一命令的 Peer_Address_Type（參數 5）不動、0x2041（參數 0）3→2；ACL payload 裡長得像
 * 命令的 bytes、沒有規則的 0x200c、已經是 0 的位址類型都不動。任意 take 切塊、FIFO AGAIN 結果相同；
 * TX 追蹤器看到改寫後的值；清單空時原樣送出；recover 後走訪狀態回到封包邊界。 */
static void test_txmask(void)
{
    static const uint8_t scan_params[] = {0x01, 0x0b, 0x20, 0x07, 0x00, 0xe0, 0x01, 0x30, 0x00, 0x01, 0x00};
    static const uint8_t scan_enable[] = {0x01, 0x0c, 0x20, 0x02, 0x01, 0x01};
    static const uint8_t scan_public[] = {0x01, 0x0b, 0x20, 0x07, 0x01, 0x60, 0x00, 0x30, 0x00, 0x00, 0x00};
    static const uint8_t ext_scan[] = {0x01, 0x41, 0x20, 0x08, 0x03, 0x00, 0x01, 0x00, 0x60, 0x00, 0x30, 0x00};
    static const uint8_t acl[] = {0x02, 0x01, 0x20, 0x0b, 0x00,
        0x01, 0x0b, 0x20, 0x07, 0x00, 0xe0, 0x01, 0x30, 0x00, 0x01, 0x00};
    uint8_t connect[4 + 25];
    uint8_t stream[256], expected[256];
    size_t size = 0, connect_own, scan_own, ext_own;
    memset(connect, 0, sizeof(connect));
    connect[0] = 0x01;
    connect[1] = 0x0d;
    connect[2] = 0x20;
    connect[3] = 25;
    connect[4 + 5] = 0x01;   /* Peer_Address_Type：不在規則內 */
    connect[4 + 12] = 0x01;  /* Own_Address_Type */
#define APPEND(packet) do { memcpy(stream + size, packet, sizeof(packet)); size += sizeof(packet); } while (0)
    scan_own = size + 4 + 5;
    APPEND(scan_params);
    APPEND(acl);
    APPEND(scan_enable);
    APPEND(scan_public);
    connect_own = size + 4 + 12;
    APPEND(connect);
    ext_own = size + 4;
    APPEND(ext_scan);
#undef APPEND
    memcpy(expected, stream, size);
    expected[scan_own] = 0x00;
    expected[connect_own] = 0x00;
    expected[ext_own] = 0x02;
    for (unsigned take = 0; take <= 9; take++) {
        for (unsigned random = 0; random <= 1; random++) {
            struct world *w = calloc(1, sizeof(*w));
            unsigned commands = 0;
            assert(w);
            world_init(w);
            w->sink.take_cap = take;
            w->uart.random = random ? 0x5eed + take : 0;
            w->uart.budget = random ? 3 : 64;
            w->pump.txmask[0] = 0x200b0501u;
            w->pump.txmask[1] = 0x200d0c01u;
            w->pump.txmask[2] = 0x20410001u;
            w->pump.txmask_count = 3;
            host_send(w, stream, size);
            run_until_idle(w, 20000);
            assert(w->uart.wire_size == size && !memcmp(w->uart.wire, expected, size));
            assert(w->pump.txmasked == 3 && w->pump.txmask_last_opcode == 0x2041);
            assert(w->pump.txmask_last_offset == 0 && w->pump.txmask_last_before == 0x03 &&
                w->pump.txmask_last_after == 0x02);
            assert(!w->pump.txw_type && w->pump.to_controller.packets[FUXI_BT_H4_ACL] == 1);
            for (unsigned index = 0; index < w->log.count; index++) {
                const struct fuxi_bt_h4_record *r = &w->log.records[index];
                if (r->type == FUXI_BT_H4_COMMAND && r->opcode == 0x200b) {
                    /* 追蹤器看到改寫後的參數 */
                    assert(r->keep_size == FUXI_BT_H4_KEEP && r->keep[5] == 0x00);
                    commands++;
                }
            }
            assert(commands == 2);
            free(w);
        }
    }
    {
        struct world *w = calloc(1, sizeof(*w));
        assert(w);
        world_init(w);
        w->sink.take_cap = 5;
        host_send(w, stream, size);
        run_until_idle(w, 20000);
        assert(w->uart.wire_size == size && !memcmp(w->uart.wire, stream, size));
        assert(!w->pump.txmasked);

        /* 命令送到一半就 recover：走訪狀態歸零，下一個命令照規則改寫 */
        world_init(w);
        w->pump.txmask[0] = 0x200b0501u;
        w->pump.txmask_count = 1;
        w->sink.take_cap = 6;
        host_send(w, scan_params, 6);
        step_n(w, 3);
        assert(w->pump.txw_type == FUXI_BT_H4_COMMAND);
        fuxi_bt_pump_recover(&w->pump);
        assert(!w->pump.txw_type && !w->pump.txw_remaining);
        w->sink.take_cap = 0;
        host_send(w, scan_params, sizeof(scan_params));
        run_until_idle(w, 2000);
        assert(w->uart.wire_size == 6 + sizeof(scan_params) && w->uart.wire[6 + 9] == 0x00);
        assert(w->pump.txmasked == 1);
        free(w);
    }
}

/* 殘留資料：RX 的 0x01、event code 0 的假 header 不當封包，後面的 IBS WAKE_IND 照樣回 WAKE_ACK，
 * 真正的 Command Complete 完整交出；header 跨塊時同樣成立 */
static void test_rx_garbage(void)
{
    static const uint8_t garbage[] = {0x01, 0xda, 0x01, 0xff, 0x04, 0x00, 0x60, 0xfd};
    static const uint8_t complete[] = {0x04, 0x0e, 0x04, 0x01, 0x03, 0x0c, 0x00};
    for (unsigned chunk = 0; chunk <= 9; chunk++) {
        struct world *w = calloc(1, sizeof(*w));
        assert(w);
        world_init(w);
        w->pump.ibs = true;
        w->uart.rx_chunk = chunk;
        controller_send(w, garbage, sizeof(garbage));
        controller_send(w, complete, sizeof(complete));
        step_n(w, 200);
        assert(w->sink.host_rx_size == sizeof(complete) && !memcmp(w->sink.host_rx, complete, sizeof(complete)));
        assert(w->pump.host.packets == 1 && !w->pump.host.header_need);
        assert(w->pump.host.ibs_bytes[1] == 1 && w->pump.ibs_acks_sent == 1);
        assert(w->uart.wire_size == 1 && w->uart.wire[0] == FUXI_BT_IBS_WAKE_ACK);
        /* 04 00 跨塊時只少算上一塊的 00，其餘 byte 都算 desync */
        assert(w->pump.host.desync_bytes >= 7 && w->pump.host.desync_bytes <= 8);
        assert(w->pump.host.invalid_headers <= 1);
        assert(!w->pump.to_host.packets[FUXI_BT_H4_COMMAND] && w->pump.to_host.packets[FUXI_BT_H4_EVENT] == 1);
        free(w);
    }
}

/* 新連線重新對齊：上一個連線的半個封包與尚未交出的 RX 丟掉，下一個封包從開頭交出 */
static void test_rx_resync(void)
{
    static const uint8_t complete[] = {0x04, 0x0e, 0x04, 0x01, 0x03, 0x0c, 0x00};
    uint8_t packet[120];
    const size_t size = make_acl(packet, 0x2001, 100, 1);
    struct world *w = calloc(1, sizeof(*w));
    assert(w && size > 50);
    world_init(w);

    /* 上層收不下：RX 卡在 pump 裡，header 已開始 */
    w->sink.deliver_cap = 0;
    controller_send(w, packet, 50);
    step_n(w, 5);
    assert(w->pump.rx_size && w->pump.host.header_need);
    fuxi_bt_pump_resync_host(&w->pump);
    assert(w->pump.host_resyncs == 1 && w->pump.resync_partial == 1 && w->pump.resync_dropped);
    assert(!w->pump.rx_size && !w->pump.rx_offset && !w->pump.host.header_need && !w->pump.to_host.type);
    w->sink.deliver_cap = WIRE_CAP;
    controller_send(w, complete, sizeof(complete));
    run_until_idle(w, 1000);
    assert(w->sink.host_rx_size == sizeof(complete) && !memcmp(w->sink.host_rx, complete, sizeof(complete)));

    /* 封包交到一半：已交出的留著，剩下的不再等 */
    controller_send(w, packet, 50);
    step_n(w, 5);
    assert(w->sink.host_rx_size == sizeof(complete) + 50 && w->pump.host.header_need && !w->pump.rx_size);
    fuxi_bt_pump_resync_host(&w->pump);
    assert(w->pump.host_resyncs == 2 && w->pump.resync_partial == 2);
    controller_send(w, complete, sizeof(complete));
    run_until_idle(w, 1000);
    assert(w->sink.host_rx_size == 2 * sizeof(complete) + 50);
    assert(!memcmp(w->sink.host_rx + sizeof(complete) + 50, complete, sizeof(complete)));

    /* 封包邊界上重新對齊：不算半包 */
    fuxi_bt_pump_resync_host(&w->pump);
    assert(w->pump.host_resyncs == 3 && w->pump.resync_partial == 2);
    fuxi_bt_pump_resync_host(NULL);
    free(w);
}

static void test_errors_and_recover(void)
{
    struct world *w = calloc(1, sizeof(*w));
    uint8_t packet[600];
    size_t size;
    struct fuxi_bt_pump_activity activity;
    unsigned reads, writes;
    assert(w);
    world_init(w);
    w->uart.fail_read = FUXI_BT_IO;
    w->uart.fail_read_at = 1;
    assert(fuxi_bt_pump_step(&w->pump, &activity) == FUXI_BT_IO);
    reads = w->uart.reads;
    writes = w->uart.writes;
    for (unsigned step = 0; step < 5; step++) {
        assert(fuxi_bt_pump_step(&w->pump, &activity) == FUXI_BT_IO);
    }
    assert(w->uart.reads == reads && w->uart.writes == writes);
    w->uart.fail_read = FUXI_BT_OK;
    fuxi_bt_pump_recover(&w->pump);
    assert(w->pump.error == FUXI_BT_OK && w->pump.recoveries == 1 && !w->pump.tx_dropped);

    /* 寫到一半出錯：已取出的未送部分計入 tx_dropped，之後的資料照常送 */
    size = make_acl(packet, 0x2001, 500, 1);
    host_send(w, packet, size);
    w->uart.fail_write = FUXI_BT_TIMEOUT;
    w->uart.fail_write_at = w->uart.writes + 2;
    assert(fuxi_bt_pump_step(&w->pump, &activity) == FUXI_BT_OK);
    assert(activity.tx_taken == FUXI_BT_PUMP_TX_CHUNK && activity.tx_accepted == 64 && activity.tx_pending);
    assert(fuxi_bt_pump_step(&w->pump, &activity) == FUXI_BT_TIMEOUT);
    assert(w->pump.error == FUXI_BT_TIMEOUT);
    w->uart.fail_write = FUXI_BT_OK;
    w->uart.owns = false;
    w->uart.tx_remaining = 0;
    w->uart.in_flight = 0;
    fuxi_bt_pump_recover(&w->pump);
    assert(w->pump.tx_dropped == FUXI_BT_PUMP_TX_CHUNK - 64);
    run_until_idle(w, 10000);
    assert(w->uart.wire_size == size - (FUXI_BT_PUMP_TX_CHUNK - 64));
    assert(!memcmp(w->uart.wire, packet, 64));
    assert(!memcmp(w->uart.wire + 64, packet + FUXI_BT_PUMP_TX_CHUNK, size - FUXI_BT_PUMP_TX_CHUNK));
    /* 追蹤器從丟掉的塊之後重新同步：之後的 bytes 從封包中間開始，會被當成新類型或 desync */
    assert(w->pump.to_controller.bytes == size);
    free(w);
}

static enum fuxi_bt_status lying_read(void *context, uint8_t *bytes, size_t capacity,
    size_t *received)
{
    (void)context;
    (void)bytes;
    *received = capacity + 1;
    return FUXI_BT_OK;
}

static enum fuxi_bt_status lying_write(void *context, const uint8_t *bytes, size_t size,
    size_t *accepted)
{
    (void)context;
    (void)bytes;
    *accepted = size + 1;
    return FUXI_BT_OK;
}

static size_t lying_take(void *context, uint8_t *bytes, size_t capacity)
{
    (void)context;
    (void)bytes;
    return capacity + 1;
}

static void test_protocol_guards(void)
{
    struct world *w = calloc(1, sizeof(*w));
    struct fuxi_bt_pump_activity activity;
    assert(w);
    world_init(w);
    w->ops.read = lying_read;
    assert(fuxi_bt_pump_init(&w->pump, &w->ops, &w->pump_sink, NULL, NULL) == FUXI_BT_OK);
    assert(fuxi_bt_pump_step(&w->pump, &activity) == FUXI_BT_PROTOCOL);

    world_init(w);
    w->ops.write = lying_write;
    assert(fuxi_bt_pump_init(&w->pump, &w->ops, &w->pump_sink, NULL, NULL) == FUXI_BT_OK);
    host_send(w, (const uint8_t *)"\x01\x03\x0c\x00", 4);
    assert(fuxi_bt_pump_step(&w->pump, &activity) == FUXI_BT_PROTOCOL);

    world_init(w);
    w->pump_sink.take = lying_take;
    assert(fuxi_bt_pump_init(&w->pump, &w->ops, &w->pump_sink, NULL, NULL) == FUXI_BT_OK);
    assert(fuxi_bt_pump_step(&w->pump, &activity) == FUXI_BT_PROTOCOL);

    /* 參數檢查 */
    world_init(w);
    assert(fuxi_bt_pump_init(NULL, &w->ops, &w->pump_sink, NULL, NULL) == FUXI_BT_ARGUMENT);
    assert(fuxi_bt_pump_init(&w->pump, NULL, &w->pump_sink, NULL, NULL) == FUXI_BT_ARGUMENT);
    assert(fuxi_bt_pump_step(&w->pump, &activity) == FUXI_BT_ARGUMENT);
    assert(fuxi_bt_pump_init(&w->pump, &w->ops, NULL, NULL, NULL) == FUXI_BT_ARGUMENT);
    w->ops.read = NULL;
    assert(fuxi_bt_pump_init(&w->pump, &w->ops, &w->pump_sink, NULL, NULL) == FUXI_BT_ARGUMENT);
    w->ops.read = sim_read;
    w->pump_sink.deliver = NULL;
    assert(fuxi_bt_pump_init(&w->pump, &w->ops, &w->pump_sink, NULL, NULL) == FUXI_BT_ARGUMENT);
    assert(fuxi_bt_pump_step(NULL, &activity) == FUXI_BT_ARGUMENT);
    assert(fuxi_bt_pump_step(NULL, NULL) == FUXI_BT_ARGUMENT);
    fuxi_bt_pump_recover(NULL);
    free(w);
}

static void test_tracer(void)
{
    /* LE 廣播報告、Command Complete 無 status、零長度事件、未知類型 byte、各種長度欄位 */
    static const uint8_t stream[] = {
        0x00, 0xff,                                     /* 2 個 desync byte */
        0x04, 0x3e, 0x03, 0x02, 0x01, 0x00,             /* LE Advertising Report */
        0x04, 0x3e, 0x02, 0x0d, 0x01,                   /* LE Extended Advertising Report */
        0x04, 0x3e, 0x02, 0x01, 0x00,                   /* LE Connection Complete（非廣播） */
        0x04, 0x0e, 0x03, 0x01, 0x05, 0x10,             /* Command Complete 無 status */
        0x04, 0x13, 0x00,                               /* 零長度事件 */
        0x03, 0x02, 0x00, 0x02, 0xaa, 0xbb,             /* SCO */
        0x05, 0x03, 0x20, 0x02, 0xc0, 0xde, 0xad,       /* ISO：長度只取低 14 位 */
        0x02, 0x01, 0x20, 0x00, 0x00,                   /* 零長度 ACL */
        0x04, 0x0f, 0x03, 0x00, 0x01, 0x05              /* 截斷的 Command Status（長度 3） */
    };
    struct trace_log *bulk = calloc(1, sizeof(*bulk));
    struct trace_log *split = calloc(1, sizeof(*split));
    struct fuxi_bt_h4_parser a, b;
    assert(bulk && split);
    fuxi_bt_h4_parser_init(&a, FUXI_BT_H4_TO_HOST);
    fuxi_bt_h4_parser_init(&b, FUXI_BT_H4_TO_HOST);
    fuxi_bt_h4_feed(&a, stream, sizeof(stream), trace_record, bulk);
    for (size_t index = 0; index < sizeof(stream); index++) {
        fuxi_bt_h4_feed(&b, stream + index, 1, trace_record, split);
    }
    assert(bulk->count == 9 && split->count == 9);
    for (unsigned index = 0; index < bulk->count; index++) {
        assert(same_record(&bulk->records[index], &split->records[index]));
    }
    assert(a.desync_bytes == 2 && b.desync_bytes == 2);
    assert(a.le_adv_reports == 2);
    assert(bulk->records[0].event == 0x3e && bulk->records[0].subevent == 0x02);
    assert(bulk->records[2].subevent == 0x01);
    assert(bulk->records[3].has_opcode && bulk->records[3].opcode == 0x1005 && !bulk->records[3].has_status);
    assert(bulk->records[4].event == 0x13 && !bulk->records[4].length);
    assert(bulk->records[5].type == FUXI_BT_H4_SCO && bulk->records[5].handle == 0x002 && bulk->records[5].length == 2);
    assert(bulk->records[6].type == FUXI_BT_H4_ISO && bulk->records[6].length == 0x0002 && bulk->records[6].keep_size == 2);
    assert(bulk->records[7].type == FUXI_BT_H4_ACL && bulk->records[7].handle == 0x001 && !bulk->records[7].length);
    assert(bulk->records[8].event == 0x0f && !bulk->records[8].has_status && !bulk->records[8].has_opcode);
    assert(a.packets[4] == 6 && !a.packets[1] && a.packets[2] == 1 && a.packets[3] == 1 && a.packets[5] == 1);
    {
        /* 控制器不會送 Command：RX 方向的 01 01 10 00 全是 desync，TX 方向照常是零長度命令 */
        static const uint8_t command[] = {0x01, 0x01, 0x10, 0x00};
        struct trace_log *log = calloc(1, sizeof(*log));
        struct fuxi_bt_h4_parser rx, tx;
        assert(log);
        fuxi_bt_h4_parser_init(&rx, FUXI_BT_H4_TO_HOST);
        fuxi_bt_h4_feed(&rx, command, sizeof(command), trace_record, log);
        assert(!log->count && rx.desync_bytes == 4 && !rx.packets[1]);
        fuxi_bt_h4_parser_init(&tx, FUXI_BT_H4_TO_CONTROLLER);
        fuxi_bt_h4_feed(&tx, command, sizeof(command), trace_record, log);
        assert(log->count == 1 && log->records[0].type == FUXI_BT_H4_COMMAND && log->records[0].opcode == 0x1001);
        assert(!tx.desync_bytes && tx.packets[1] == 1);
        free(log);
    }
    assert(!strcmp(fuxi_bt_h4_type_name(1), "cmd") && !strcmp(fuxi_bt_h4_type_name(4), "evt") &&
        !strcmp(fuxi_bt_h4_type_name(9), "?"));
    fuxi_bt_h4_feed(NULL, stream, 1, NULL, NULL);
    fuxi_bt_h4_feed(&a, NULL, 1, NULL, NULL);
    fuxi_bt_h4_parser_init(NULL, 0);
    free(bulk);
    free(split);
}

static void test_tracer_fuzz(void)
{
    uint32_t state = 0x5eed;
    uint8_t *stream = malloc(65536);
    struct trace_log *bulk = calloc(1, sizeof(*bulk));
    struct trace_log *split = calloc(1, sizeof(*split));
    assert(stream && bulk && split);
    for (unsigned round = 0; round < 64; round++) {
        struct fuxi_bt_h4_parser a, b;
        const size_t size = 1 + next_random(&state) % 65536;
        size_t offset = 0;
        for (size_t index = 0; index < size; index++) {
            stream[index] = (uint8_t)next_random(&state);
            if ((next_random(&state) & 7) == 0) stream[index] = (uint8_t)(1 + next_random(&state) % 5);
        }
        memset(bulk, 0, sizeof(*bulk));
        memset(split, 0, sizeof(*split));
        fuxi_bt_h4_parser_init(&a, FUXI_BT_H4_TO_CONTROLLER);
        fuxi_bt_h4_parser_init(&b, FUXI_BT_H4_TO_CONTROLLER);
        fuxi_bt_h4_feed(&a, stream, size, trace_record, bulk);
        while (offset < size) {
            size_t chunk = 1 + next_random(&state) % 300;
            if (chunk > size - offset) chunk = size - offset;
            fuxi_bt_h4_feed(&b, stream + offset, chunk, trace_record, split);
            offset += chunk;
        }
        assert(bulk->count == split->count);
        {
            const unsigned kept = bulk->count < 4096 ? bulk->count : 4096;
            for (unsigned index = 0; index < kept; index++) {
                assert(same_record(&bulk->records[index], &split->records[index]));
            }
        }
        assert(a.desync_bytes == b.desync_bytes && a.le_adv_reports == b.le_adv_reports);
        assert(a.bytes == size && b.bytes == size);
    }
    free(stream);
    free(bulk);
    free(split);
}

static void test_link_detail(void)
{
    struct fuxi_bt_h4_record r = { .direction = FUXI_BT_H4_TO_HOST, .type = FUXI_BT_H4_EVENT,
                                  .keep_size = 1, .length = 1 };
    int value = 0;
    memset(r.keep, 0xa5, sizeof(r.keep));
    const uint8_t opaque[] = {2, 4, 0x16, 0x17, 0x18, 0x22, 0x2f, 0x31, 0x32, 0x33, 0x34, 0x35};
    for (unsigned i = 0; i < sizeof(opaque); ++i) {
        r.event = opaque[i];
        assert(fuxi_bt_link_detail(&r, &value) && value == -1);
    }
    r.event = 0x02;
    assert(fuxi_bt_link_is_inquiry(&r));
    r.event = 0x05;
    r.keep_size = 4;
    r.keep[0] = 0x00;
    r.keep[1] = 0x02;
    r.keep[2] = 0x00;
    r.keep[3] = 0x13;
    assert(!fuxi_bt_link_is_inquiry(&r));
    assert(fuxi_bt_link_detail(&r, &value) && value == 0x13);
    r.keep[0] = 0x0c;
    assert(fuxi_bt_link_detail(&r, &value) && value == 0x0c13);
    r.keep_size = 3;
    assert(fuxi_bt_link_detail(&r, &value) && value == -1);
    r.event = 0x08;
    r.keep_size = 4;
    r.keep[0] = 0x00;
    r.keep[3] = 0x02;
    assert(fuxi_bt_link_detail(&r, &value) && value == 0x02);
    r.event = 0x10;
    r.keep_size = 1;
    r.keep[0] = 0x2a;
    assert(fuxi_bt_link_detail(&r, &value) && value == 0x2a);
    r.event = 0x30;
    r.keep[0] = 0x00;
    assert(fuxi_bt_link_detail(&r, &value) && value == 0x00);
    r.event = 0x36;
    r.keep[0] = 0xa5;
    assert(fuxi_bt_link_detail(&r, &value) && value == 0xa5);
    r.keep_size = 0;
    assert(fuxi_bt_link_detail(&r, &value) && value == -1);
    r.event = 0x3e;
    assert(!fuxi_bt_link_detail(&r, &value));
    r.has_opcode = true;
    r.opcode = 0x405;
    r.has_status = true;
    r.status = 0x0c;
    assert(!fuxi_bt_link_is_inquiry(&r));
    assert(fuxi_bt_link_detail(&r, &value) && value == 0x0c);
    r.direction = FUXI_BT_H4_TO_CONTROLLER;
    assert(fuxi_bt_link_detail(&r, &value) && value == -1);
    r.opcode = 0x0401;
    assert(fuxi_bt_link_is_inquiry(&r));
    const uint16_t opaque_ops[] = {0x040b, 0x040c, 0x042e, 0x042f, 0x0430, 0x0433, 0x0434};
    for (unsigned i = 0; i < sizeof(opaque_ops) / sizeof(opaque_ops[0]); ++i) {
        r.opcode = opaque_ops[i];
        r.keep_size = 6;
        r.length = 6;
        assert(fuxi_bt_link_detail(&r, &value) && value == -1);
    }
    r.opcode = 0x0413;
    r.length = 3;
    r.keep_size = 3;
    r.keep[2] = 1;
    assert(fuxi_bt_link_detail(&r, &value) && value == 1);
    r.keep[2] = 2;
    assert(fuxi_bt_link_detail(&r, &value) && value == -1);
    r.opcode = 0x0c1a;
    r.length = 1;
    r.keep_size = 1;
    r.keep[0] = 4;
    assert(fuxi_bt_link_detail(&r, &value) && value == -1);
    r.keep[0] = 3;
    assert(fuxi_bt_link_detail(&r, &value) && value == 3);
    r.opcode = 0x0c56;
    r.keep[0] = 1;
    assert(fuxi_bt_link_detail(&r, &value) && value == 1);
    r.opcode = 0xfce9;
    assert(!fuxi_bt_link_detail(&r, &value));
}

int main(void)
{
    test_link_detail();
    test_reset_roundtrip();
    test_bulk_ordering(0, 0, 64, 3);
    test_bulk_ordering(1, 0, 4, 0);
    test_bulk_ordering(0x1234, 7, 64, 9);
    test_bulk_ordering(0xbeef, 300, 1024, 1);
    test_backpressure();
    test_discard_without_client();
    test_framing(1, 0, WIRE_CAP, 0);
    test_framing(2, 1, WIRE_CAP, 0);
    test_framing(3, 0, 1, 0);
    test_framing(4, 3, 2, 0);
    test_framing(5, 0, 97, 7);
    test_framing(6, 1, 3, 3);
    test_framing(7, 2, WIRE_CAP, 2);
    test_framing(8, 0, 5, 50);
    test_framing_mid_packet_open();
    test_ibs();
    test_optional();
    test_pad();
    test_mask();
    test_txmask();
    test_rx_garbage();
    test_rx_resync();
    test_errors_and_recover();
    test_protocol_guards();
    test_tracer();
    test_tracer_fuzz();
    puts("fuxi_bt_pump host test passed");
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
    with tempfile.TemporaryDirectory(prefix="fuxi-bt-pump-") as temporary:
        directory = Path(temporary)
        harness = directory / "test.c"
        harness.write_text(HARNESS, encoding="utf-8")
        executable = directory / ("test.exe" if os.name == "nt" else "test")
        command = compiler + [
            "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-I", str(SOURCE), str(SOURCE / "fuxi_bt.c"), str(SOURCE / "fuxi_bt_pump.c"), str(harness),
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
