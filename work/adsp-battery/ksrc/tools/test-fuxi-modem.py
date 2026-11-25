#!/usr/bin/env python3
"""主機測試：xnu/fuxi-modem/fuxi_modem_proto.h（核心 fuxi_modem.c 用的同一份純邏輯）。

涵蓋 MP_ASSIGN 參數頁版面（對照 Linux qcom_scm_assign_mem）、QMI TLV 解析、rmtfs 的開檔／配置緩衝區／磁區讀寫／
關檔／錯誤查詢與所有拒絕路徑（含「一個請求不會只做一半」），以及 QRTR 控制封包與 NEW_LOOKUP 解析；最後用亂數訊息
打 fm_rmtfs_answer，搭配 --sanitize（ASan／UBSan）確認不越界。
"""
import argparse
import os
from pathlib import Path
import runpy
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define FUXI 1
#include "fuxi_modem_proto.h"
#include "coachz_bb_qmux.h"
#include "coachz_bb_data.h"
#include "coachz_bb_pdp.h"
static unsigned checks;
#define CHECK(test) do { checks++; if (!(test)) { fprintf(stderr, "line %d: %s\n", __LINE__, #test); exit(1); } } while (0)
static uint64_t le64(const uint8_t *p) { return (uint64_t)fm_le32(p) | (uint64_t)fm_le32(p + 4) << 32; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* ---- QMI 訊息組裝 ---- */
typedef struct { uint8_t b[512]; uint32_t n; } Msg;
static void begin(Msg *m, uint16_t txn, uint16_t id) {
    memset(m, 0, sizeof(*m));
    m->b[0] = 0; m->b[1] = (uint8_t)txn; m->b[2] = (uint8_t)(txn >> 8); m->b[3] = (uint8_t)id; m->b[4] = (uint8_t)(id >> 8);
    m->n = 7;
}
static void tlv(Msg *m, uint8_t type, const void *value, uint16_t size) {
    m->b[m->n] = type; fm_put16(m->b + m->n + 1, size); memcpy(m->b + m->n + 3, value, size);
    m->n += 3u + size;
    fm_put16(m->b + 5, m->n - 7);
}
static void tlv32(Msg *m, uint8_t type, uint32_t v) { uint8_t b[4]; fm_put32(b, v); tlv(m, type, b, 4); }
static void tlv8(Msg *m, uint8_t type, uint8_t v) { tlv(m, type, &v, 1); }
static void iovec(Msg *m, unsigned count, const uint32_t (*e)[3]) {
    uint8_t b[1 + 12 * 20];
    b[0] = (uint8_t)count;
    for (unsigned i = 0; i < count; ++i) { fm_put32(b + 1 + 12 * i, e[i][0]); fm_put32(b + 5 + 12 * i, e[i][1]); fm_put32(b + 9 + 12 * i, e[i][2]); }
    tlv(m, 3, b, (uint16_t)(1 + 12 * count));
}
/* 回應的結果 TLV；回傳 result，*error 為錯誤碼。順便檢查標頭。 */
static unsigned answered(const uint8_t *out, uint32_t n, const Msg *request, uint16_t *error) {
    CHECK(n >= 14);
    CHECK(out[0] == 2 && out[1] == request->b[1] && out[2] == request->b[2] && out[3] == request->b[3] && out[4] == request->b[4]);
    CHECK(le16(out + 5) == n - 7);
    CHECK(out[7] == 0x02 && le16(out + 8) == 4);
    *error = le16(out + 12);
    return le16(out + 10);
}

static void assign_page(void) {
    static uint8_t page[FM_ASSIGN_PAGE];
    uint64_t args[4];
    const uint32_t src[1] = {FM_VMID_HLOS}, vm[2] = {FM_VMID_HLOS, FM_VMID_MSS_MSA}, perm[2] = {FM_PERM_RW, FM_PERM_RW};
    memset(page, 0xa5, sizeof page);
    CHECK(fm_assign_page(page, 0x812345000ull, 0x9c900000ull, 0x280000ull, src, 1, vm, perm, 2, args) == 0);
    CHECK(args[0] == 0x812345000ull && args[1] == 16 && args[2] == 0x812345040ull && args[3] == 0x8123450f0ull);
    CHECK(le64(page) == 0x9c900000ull && le64(page + 8) == 0x280000ull);
    CHECK(fm_le32(page + 64) == 3 && fm_le32(page + 68) == 0);
    CHECK(fm_le32(page + 128) == 3 && fm_le32(page + 132) == 6 && le64(page + 136) == 0 && fm_le32(page + 144) == 0);
    CHECK(fm_le32(page + 152) == 15 && fm_le32(page + 156) == 6 && le64(page + 160) == 0);
    CHECK(le64(page + 240) == 4 && le64(page + 248) == 0x812345080ull && le64(page + 256) == 48 && le64(page + 264) == 0);
    for (unsigned i = 272; i < FM_ASSIGN_PAGE; ++i) CHECK(page[i] == 0);
    CHECK(FM_ASSIGN_ARGINFO == (7u | 1u << 4 | 1u << 8 | 1u << 12)); /* QCOM_SCM_ARGS(7, RO, VAL, RO, VAL, RO, VAL, VAL) */
    /* DSM：一個目的地 */
    const uint32_t msa[1] = {FM_VMID_MSS_MSA}, rw[1] = {FM_PERM_RW};
    CHECK(fm_assign_page(page, 0x1000, 0xd4d00000ull, 0x3300000ull, src, 1, msa, rw, 1, args) == 0);
    CHECK(le64(page + 256) == 24 && fm_le32(page + 128) == 15 && fm_le32(page + 152) == 0);
    /* 拒絕 */
    CHECK(fm_assign_page(page, 0x1800, 1, 1, src, 1, msa, rw, 1, args) == -1);
    CHECK(fm_assign_page(page, 0x1000, 1, 0, src, 1, msa, rw, 1, args) == -1);
    CHECK(fm_assign_page(page, 0x1000, 1, 1, src, 0, msa, rw, 1, args) == -1);
    CHECK(fm_assign_page(page, 0x1000, 1, 1, src, 1, msa, rw, 4, args) == -1);
    CHECK(fm_assign_page(NULL, 0x1000, 1, 1, src, 1, msa, rw, 1, args) == -1);
}

static void qmi_tlvs(void) {
    Msg m;
    uint32_t size = 0;
    begin(&m, 7, 3);
    tlv32(&m, 1, 0x11223344u);
    tlv8(&m, 2, 9);
    const uint8_t *v = fm_qmi_tlv(m.b, m.n, 1, &size);
    CHECK(v && size == 4 && fm_le32(v) == 0x11223344u);
    v = fm_qmi_tlv(m.b, m.n, 2, &size);
    CHECK(v && size == 1 && *v == 9);
    CHECK(!fm_qmi_tlv(m.b, m.n, 3, &size));
    CHECK(!fm_qmi_tlv(m.b, 6, 1, &size));
    /* 宣告長度超過實際：只看得到實際範圍內完整的 TLV */
    fm_put16(m.b + 5, 400);
    CHECK(fm_qmi_tlv(m.b, m.n, 2, &size) != NULL);
    CHECK(!fm_qmi_tlv(m.b, m.n - 1, 2, &size));
    /* TLV 自稱的長度超出訊息 */
    begin(&m, 1, 1);
    tlv32(&m, 1, 5);
    fm_put16(m.b + 8, 40);
    CHECK(!fm_qmi_tlv(m.b, m.n, 1, &size));
}

#define WINDOW 0x10000u
#define PA 0x9c900000u
static uint8_t gFiles[4][0x4000];
static void setup(FmRmtfs *r, uint8_t *window, int shared) {
    fm_rmtfs_init(r);
    for (unsigned f = 0; f < 4; ++f) {
        for (unsigned i = 0; i < sizeof gFiles[f]; ++i) gFiles[f][i] = (uint8_t)(i * 7 + f * 31 + (i >> 9));
        r->file[f].data = f == 2 ? NULL : gFiles[f]; /* fsc 沒讀到 */
        r->file[f].size = sizeof gFiles[f];
    }
    r->window = window; r->pa = PA; r->size = WINDOW; r->shared = (uint8_t)shared;
    memset(window, 0, WINDOW);
}
static unsigned ask(FmRmtfs *r, const Msg *m, uint8_t *out, uint32_t *n, uint16_t *error) {
    *n = fm_rmtfs_answer(r, m->b, m->n, out, 64);
    return answered(out, *n, m, error);
}
static void open_file(FmRmtfs *r, const char *path, unsigned *caller, unsigned *result) {
    Msg m; uint8_t out[64]; uint32_t n; uint16_t error;
    begin(&m, 0x101, FM_RMTFS_OPEN);
    tlv(&m, 1, path, (uint16_t)strlen(path));
    *result = ask(r, &m, out, &n, &error);
    if (!*result) {
        CHECK(n == 21 && out[14] == 0x10 && le16(out + 15) == 4);
        *caller = fm_le32(out + 17);
    } else {
        CHECK(n == 14 && error == FM_QMI_ERR_INTERNAL);
    }
}
static unsigned rw(FmRmtfs *r, unsigned caller, int write, unsigned count, const uint32_t (*e)[3], uint16_t *error) {
    Msg m; uint8_t out[64]; uint32_t n;
    begin(&m, 0x202, FM_RMTFS_RW_IOVEC);
    tlv32(&m, 1, caller);
    tlv8(&m, 2, (uint8_t)write);
    iovec(&m, count, e);
    tlv8(&m, 4, 1);
    const unsigned result = ask(r, &m, out, &n, error);
    CHECK(n == 14);
    return result;
}

static void rmtfs_flow(void) {
    static uint8_t window[WINDOW];
    FmRmtfs r;
    Msg m; uint8_t out[64]; uint32_t n; uint16_t error;
    unsigned caller = 0, result = 0;
    setup(&r, window, 1);
    CHECK(!strcmp(r.file[0].partition, "modemst1") && !strcmp(r.file[3].path, "/boot/modem_fsg"));
    open_file(&r, "/boot/modem_fs1", &caller, &result);
    CHECK(!result && caller == 1 && r.file[0].open && r.opens == 1);
    open_file(&r, "/boot/modem_fsg", &caller, &result);
    CHECK(!result && caller == 4);
    open_file(&r, "/boot/modem_fsc", &caller, &result); /* 分區沒讀到 */
    CHECK(result == FM_QMI_RESULT_FAILURE);
    open_file(&r, "/boot/modem_fs", &caller, &result); /* 前綴不算 */
    CHECK(result == FM_QMI_RESULT_FAILURE);
    open_file(&r, "/boot/modem_fs12", &caller, &result);
    CHECK(result == FM_QMI_RESULT_FAILURE);
    /* 配置緩衝區 */
    begin(&m, 3, FM_RMTFS_ALLOC_BUFF);
    tlv32(&m, 1, 1);
    tlv32(&m, 2, 0x8000);
    CHECK(!ask(&r, &m, out, &n, &error) && n == 25 && out[14] == 0x10 && le16(out + 15) == 8 && le64(out + 17) == PA);
    begin(&m, 3, FM_RMTFS_ALLOC_BUFF);
    tlv32(&m, 1, 1);
    tlv32(&m, 2, WINDOW + 1);
    CHECK(ask(&r, &m, out, &n, &error) == FM_QMI_RESULT_FAILURE && n == 14);
    /* 讀：兩段 */
    const uint32_t reads[2][3] = {{0, PA, 2}, {4, PA + 0x1000, 1}};
    CHECK(!rw(&r, 1, 0, 2, reads, &error));
    CHECK(!memcmp(window, gFiles[0], 1024) && !memcmp(window + 0x1000, gFiles[0] + 4 * 512, 512));
    CHECK(window[1024] == 0 && r.reads == 1 && r.sectors_read == 3);
    /* 寫：改緩衝區後寫回第 1 磁區（只改記憶體副本） */
    memset(window + 0x2000, 0x5a, 512);
    const uint32_t writes[1][3] = {{1, PA + 0x2000, 1}};
    CHECK(!rw(&r, 1, 1, 1, writes, &error));
    CHECK(gFiles[0][512] == 0x5a && gFiles[0][1023] == 0x5a && gFiles[0][1024] != 0x5a && r.writes == 1 && r.sectors_written == 1);
    /* 剛好到檔尾、剛好到緩衝區尾 */
    const uint32_t edge[1][3] = {{31, PA + WINDOW - 512, 1}};
    CHECK(!rw(&r, 4, 0, 1, edge, &error));
    CHECK(!memcmp(window + WINDOW - 512, gFiles[3] + 31 * 512, 512));
    /* 一半合法、一半越界：整個拒絕，合法那段也不搬 */
    memset(window, 0, WINDOW);
    const uint32_t half[2][3] = {{0, PA, 1}, {32, PA + 512, 1}};
    CHECK(rw(&r, 1, 0, 2, half, &error) == FM_QMI_RESULT_FAILURE && error == FM_QMI_ERR_MALFORMED);
    CHECK(window[0] == 0 && window[1] == 0);
    const uint32_t beyond_window[1][3] = {{0, PA + WINDOW - 256, 1}};
    CHECK(rw(&r, 1, 0, 1, beyond_window, &error) == FM_QMI_RESULT_FAILURE);
    const uint32_t below[1][3] = {{0, PA - 512, 1}};
    CHECK(rw(&r, 1, 0, 1, below, &error) == FM_QMI_RESULT_FAILURE);
    const uint32_t huge[1][3] = {{0, PA, 0xffffffffu}};
    CHECK(rw(&r, 1, 0, 1, huge, &error) == FM_QMI_RESULT_FAILURE);
    const uint32_t far[1][3] = {{0xffffffffu, PA, 1}};
    CHECK(rw(&r, 1, 1, 1, far, &error) == FM_QMI_RESULT_FAILURE);
    /* 沒開的檔、不存在的 caller */
    CHECK(rw(&r, 2, 0, 1, reads, &error) == FM_QMI_RESULT_FAILURE);
    CHECK(rw(&r, 0, 0, 1, reads, &error) == FM_QMI_RESULT_FAILURE);
    CHECK(rw(&r, 9, 0, 1, reads, &error) == FM_QMI_RESULT_FAILURE);
    /* iovec 長度和筆數對不上 */
    begin(&m, 4, FM_RMTFS_RW_IOVEC);
    tlv32(&m, 1, 1);
    tlv8(&m, 2, 0);
    { uint8_t b[14] = {2}; tlv(&m, 3, b, 13); }
    CHECK(ask(&r, &m, out, &n, &error) == FM_QMI_RESULT_FAILURE);
    /* 方向 TLV 缺 */
    begin(&m, 4, FM_RMTFS_RW_IOVEC);
    tlv32(&m, 1, 1);
    iovec(&m, 1, reads);
    CHECK(ask(&r, &m, out, &n, &error) == FM_QMI_RESULT_FAILURE);
    /* 裝置錯誤查詢 */
    begin(&m, 5, FM_RMTFS_GET_DEV_ERROR);
    tlv32(&m, 1, 1);
    CHECK(!ask(&r, &m, out, &n, &error) && n == 18 && out[14] == 0x10 && le16(out + 15) == 1 && out[17] == 0);
    /* 關檔後不能再讀 */
    begin(&m, 6, FM_RMTFS_CLOSE);
    tlv32(&m, 1, 1);
    CHECK(!ask(&r, &m, out, &n, &error) && n == 14 && !r.file[0].open);
    CHECK(rw(&r, 1, 0, 1, reads, &error) == FM_QMI_RESULT_FAILURE);
    begin(&m, 6, FM_RMTFS_CLOSE);
    tlv32(&m, 1, 7);
    CHECK(ask(&r, &m, out, &n, &error) == FM_QMI_RESULT_FAILURE && error == FM_QMI_ERR_MALFORMED);
    /* 不認得的訊息 */
    begin(&m, 8, 0x20);
    CHECK(ask(&r, &m, out, &n, &error) == FM_QMI_RESULT_FAILURE && r.last_message == 0x20);
    /* 不是請求、或輸出太小：不回 */
    begin(&m, 8, FM_RMTFS_GET_DEV_ERROR);
    m.b[0] = 2;
    CHECK(fm_rmtfs_answer(&r, m.b, m.n, out, 64) == 0);
    m.b[0] = 0;
    CHECK(fm_rmtfs_answer(&r, m.b, m.n, out, 31) == 0);
    CHECK(fm_rmtfs_answer(&r, m.b, 6, out, 64) == 0);
    CHECK(fm_rmtfs_answer(NULL, m.b, m.n, out, 64) == 0);
}

static void rmtfs_unshared(void) {
    static uint8_t window[WINDOW];
    FmRmtfs r;
    Msg m; uint8_t out[64]; uint32_t n; uint16_t error;
    unsigned caller = 0, result = 0;
    setup(&r, window, 0); /* TrustZone 指派失敗：緩衝區不能給 */
    open_file(&r, "/boot/modem_fs2", &caller, &result);
    CHECK(!result && caller == 2);
    begin(&m, 3, FM_RMTFS_ALLOC_BUFF);
    tlv32(&m, 1, 2);
    tlv32(&m, 2, 0x1000);
    CHECK(ask(&r, &m, out, &n, &error) == FM_QMI_RESULT_FAILURE);
    const uint32_t reads[1][3] = {{0, PA, 1}};
    CHECK(rw(&r, 2, 0, 1, reads, &error) == FM_QMI_RESULT_FAILURE);
    CHECK(r.refused == 2 && r.opens == 1);
}

static void qrtr(void) {
    uint8_t p[FM_QRTR_CTRL_PACKET];
    const uint32_t words[4] = {14, 1, 1, 2};
    CHECK(fm_qrtr_ctrl(p, FM_QRTR_NEW_SERVER, 1, 0, 0x4000, words) == 52);
    CHECK(fm_le32(p) == 1 && fm_le32(p + 4) == 4 && fm_le32(p + 8) == 1 && fm_le32(p + 12) == FM_QRTR_PORT_CTRL);
    CHECK(fm_le32(p + 16) == 0 && fm_le32(p + 20) == 20 && fm_le32(p + 24) == 0 && fm_le32(p + 28) == 0x4000);
    CHECK(fm_le32(p + 32) == 4 && fm_le32(p + 36) == 14 && fm_le32(p + 40) == 1 && fm_le32(p + 44) == 1 && fm_le32(p + 48) == 2);
    CHECK(fm_qrtr_ctrl(p, FM_QRTR_NEW_SERVER, 1, 0, 7, NULL) == 52);
    for (unsigned i = 36; i < 52; ++i) CHECK(p[i] == 0);
    /* modem 的查詢：src 0:0x4002，查 14 instance 0 */
    uint8_t q[64] = {0};
    fm_put32(q, 1); fm_put32(q + 4, FM_QRTR_NEW_LOOKUP); fm_put32(q + 8, 0); fm_put32(q + 12, 0x4002);
    fm_put32(q + 20, 20); fm_put32(q + 24, 1); fm_put32(q + 28, FM_QRTR_PORT_CTRL);
    fm_put32(q + 32, FM_QRTR_NEW_LOOKUP); fm_put32(q + 36, 14); fm_put32(q + 40, 0);
    uint32_t node = 9, port = 9, service = 9, instance = 9;
    CHECK(fm_qrtr_lookup(q, 52, &node, &port, &service, &instance) == 0);
    CHECK(node == 0 && port == 0x4002 && service == 14 && instance == 0);
    CHECK(fm_lookup_matches(service, instance, FM_RMTFS_SERVICE, FM_RMTFS_INSTANCE));
    CHECK(fm_lookup_matches(14, 1, FM_RMTFS_SERVICE, FM_RMTFS_INSTANCE));
    CHECK(!fm_lookup_matches(14, 0x101, FM_RMTFS_SERVICE, FM_RMTFS_INSTANCE));
    CHECK(!fm_lookup_matches(0x40, 0, FM_RMTFS_SERVICE, FM_RMTFS_INSTANCE));
    CHECK(fm_qrtr_lookup(q, 43, &node, &port, &service, &instance) == -1);
    fm_put32(q + 20, 40);
    CHECK(fm_qrtr_lookup(q, 52, &node, &port, &service, &instance) == -1);
    fm_put32(q + 20, 20);
    fm_put32(q + 4, FM_QRTR_NEW_SERVER);
    CHECK(fm_qrtr_lookup(q, 52, &node, &port, &service, &instance) == -1);
    fm_put32(q + 4, FM_QRTR_NEW_LOOKUP);
    fm_put32(q, 2);
    CHECK(fm_qrtr_lookup(q, 52, &node, &port, &service, &instance) == -1);
}

static uint32_t seed = 0x1234567u;
static uint32_t next(void) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }
static void fuzz(void) {
    uint8_t *window = malloc(WINDOW);
    FmRmtfs r;
    CHECK(window != NULL);
    setup(&r, window, 1);
    for (unsigned f = 0; f < 4; ++f) r.file[f].open = 1;
    unsigned accepted = 0;
    for (unsigned round = 0; round < 200000; ++round) {
        Msg m;
        begin(&m, (uint16_t)next(), (uint16_t)(1 + next() % 6));
        const unsigned tlvs = next() % 5;
        for (unsigned t = 0; t < tlvs && m.n < 400; ++t) {
            uint8_t value[64];
            const unsigned type = 1 + next() % 4, size = type == 3 ? 1 + 12 * (next() % 4) : next() % 6;
            for (unsigned i = 0; i < size; ++i) value[i] = (uint8_t)next();
            if (type == 3) {
                value[0] = (uint8_t)((size - 1) / 12 + (next() % 8 == 0));
                for (unsigned e = 0; e < value[0] && e < 4; ++e) {
                    fm_put32(value + 1 + 12 * e, next() % 40);
                    fm_put32(value + 5 + 12 * e, PA - 1024 + next() % (WINDOW + 2048));
                    fm_put32(value + 9 + 12 * e, next() % 40);
                }
            } else if (type == 1 && size == 4) {
                fm_put32(value, next() % 6);
            }
            tlv(&m, (uint8_t)type, value, (uint16_t)size);
        }
        if (next() % 2) { /* 結構完整的讀寫請求：caller、方向、1–4 段落在邊界附近的磁區與位址 */
            const unsigned count = 1 + next() % 4;
            uint32_t e[4][3];
            for (unsigned i = 0; i < count; ++i) {
                e[i][0] = next() % 36;
                e[i][1] = PA - 1024 + (next() % ((WINDOW + 2048) / 256)) * 256;
                e[i][2] = next() % 12;
            }
            for (unsigned f = 0; f < 4; ++f) r.file[f].open = 1; /* 亂數 CLOSE 會把檔關掉；fsc 開著但沒有資料 */
            begin(&m, (uint16_t)next(), FM_RMTFS_RW_IOVEC);
            tlv32(&m, 1, next() % 6);
            tlv8(&m, 2, (uint8_t)(next() % 3));
            iovec(&m, count, (const uint32_t (*)[3])e);
        }
        uint32_t length = m.n;
        if (next() % 16 == 0) length = next() % (m.n + 1);
        uint8_t out[32];
        const uint32_t n = fm_rmtfs_answer(&r, m.b, length, out, sizeof out);
        CHECK(n == 0 || (n >= 14 && n <= sizeof out));
        if (n && !le16(out + 10)) accepted++;
    }
    printf("fuzz: %u of 200000 random requests accepted, reads %u writes %u refused %u\n", accepted, r.reads, r.writes, r.refused);
    CHECK(accepted > 1000 && r.reads > 1000 && r.writes > 1000);
    free(window);
}

/* ---- QMI 用戶端：請求編碼、各回應解碼、拒絕路徑與亂數回應 ---- */
static void response(Msg *m, uint16_t txn, uint16_t id, uint16_t result, uint16_t error) {
    begin(m, txn, id);
    m->b[0] = 2;
    uint8_t r[4]; fm_put16(r, result); fm_put16(r + 2, error);
    tlv(m, 0x02, r, 4);
}
static void qmi_client(void) {
    uint8_t out[16];
    CHECK(fm_qmi_request(out, sizeof out, 0x1234, FM_DMS_GET_REVISION, 0, 0) == 7);
    CHECK(out[0] == 0 && le16(out + 1) == 0x1234 && le16(out + 3) == 0x23 && le16(out + 5) == 0);
    CHECK(fm_qmi_request(out, sizeof out, 7, FM_DMS_SET_OPERATING_MODE, 0x01, FM_DMS_MODE_ONLINE) == 11);
    CHECK(le16(out + 3) == 0x2e && le16(out + 5) == 4 && out[7] == 1 && le16(out + 8) == 1 && out[10] == 0);
    CHECK(fm_qmi_request(out, 10, 7, FM_DMS_SET_OPERATING_MODE, 0x01, 0) == 0);
    CHECK(fm_qmi_request(NULL, 16, 7, FM_DMS_GET_REVISION, 0, 0) == 0);

    Msg m;
    FmQmiAnswer a;
    response(&m, 3, FM_DMS_GET_OPERATING_MODE, 0, 0);
    tlv8(&m, 0x01, 6);
    CHECK(fm_qmi_decode(FM_QMI_DMS, m.b, m.n, &a) == 0 && a.has_result && !a.result && a.has_mode && a.mode == 6 && a.transaction == 3);
    response(&m, 4, FM_DMS_GET_OPERATING_MODE, 1, 0x1a); /* 失敗：只有結果 */
    tlv8(&m, 0x01, 0);
    CHECK(fm_qmi_decode(FM_QMI_DMS, m.b, m.n, &a) == 0 && a.result == 1 && a.error == 0x1a && !a.has_mode);
    response(&m, 5, FM_DMS_GET_REVISION, 0, 0);
    tlv(&m, 0x01, "MPSS.DE.3.1\x01", 12);
    CHECK(fm_qmi_decode(FM_QMI_DMS, m.b, m.n, &a) == 0 && !strcmp(a.revision, "MPSS.DE.3.1?"));
    char longer[100]; memset(longer, 'A', sizeof longer);
    response(&m, 5, FM_DMS_GET_REVISION, 0, 0);
    tlv(&m, 0x01, longer, sizeof longer);
    CHECK(fm_qmi_decode(FM_QMI_DMS, m.b, m.n, &a) == 0 && strlen(a.revision) == sizeof a.revision - 1);

    /* UIM：一個槽、卡在、USIM ready、AID 16 位元組 */
    uint8_t card[64]; unsigned c = 0;
    for (unsigned i = 0; i < 8; ++i) card[c++] = 0xff;
    card[c++] = 1;                                     /* 槽數 */
    card[c++] = 1; card[c++] = 0; card[c++] = 3; card[c++] = 10; card[c++] = 0; card[c++] = 1; /* 卡、UPIN、次數、錯誤、應用數 */
    card[c++] = 2; card[c++] = 7; card[c++] = 3; card[c++] = 0; card[c++] = 0; card[c++] = 0; /* USIM ready perso ready */
    card[c++] = 16; for (unsigned i = 0; i < 16; ++i) card[c++] = (uint8_t)(0xa0 + i);
    card[c++] = 0; card[c++] = 2; card[c++] = 3; card[c++] = 10; card[c++] = 0; card[c++] = 3; card[c++] = 10;
    response(&m, 6, FM_UIM_GET_CARD_STATUS, 0, 0);
    tlv(&m, 0x10, card, (uint16_t)c);
    CHECK(fm_qmi_decode(FM_QMI_UIM, m.b, m.n, &a) == 0 && a.has_card && a.slots == 1 && a.card_state == 1 && a.apps == 1);
    CHECK(a.app_type == 2 && a.app_state == 7 && a.perso_state == 3 && a.pin1_state == 2 && a.pin1_retries == 3 && a.puk1_retries == 10);
    for (unsigned cut = 9; cut < c; ++cut) { /* 截短的版面一律拒絕 */
        response(&m, 6, FM_UIM_GET_CARD_STATUS, 0, 0);
        tlv(&m, 0x10, card, (uint16_t)cut);
        CHECK(fm_qmi_decode(FM_QMI_UIM, m.b, m.n, &a) == -1);
    }
    card[8] = 0; /* 沒有卡槽 */
    response(&m, 6, FM_UIM_GET_CARD_STATUS, 0, 0);
    tlv(&m, 0x10, card, 9);
    CHECK(fm_qmi_decode(FM_QMI_UIM, m.b, m.n, &a) == 0 && a.has_card && !a.slots);

    /* NAS 服務系統：註冊、PS 附著、3GPP、LTE＋NR */
    const uint8_t serving[] = {1, 1, 1, 2, 2, 8, 12};
    response(&m, 8, FM_NAS_GET_SERVING_SYSTEM, 0, 0);
    tlv(&m, 0x01, serving, sizeof serving);
    CHECK(fm_qmi_decode(FM_QMI_NAS, m.b, m.n, &a) == 0 && a.has_serving && a.reg_state == 1 && a.ps_attach == 1 && a.network == 2);
    CHECK(a.radio_count == 2 && a.radio[0] == 8 && a.radio[1] == 12);
    response(&m, 8, FM_NAS_GET_SERVING_SYSTEM, 0, 0);
    tlv(&m, 0x01, serving, 6); /* 數量與長度不合 */
    CHECK(fm_qmi_decode(FM_QMI_NAS, m.b, m.n, &a) == -1);
    uint8_t many[5 + 20] = {2, 0, 0, 2, 20};
    response(&m, 8, FM_NAS_GET_SERVING_SYSTEM, 0, 0);
    tlv(&m, 0x01, many, sizeof many);
    CHECK(fm_qmi_decode(FM_QMI_NAS, m.b, m.n, &a) == 0 && a.radio_count == 8);

    /* NAS 訊號 */
    const uint8_t lte[6] = {(uint8_t)-70, (uint8_t)-11, 0x9c, 0xff, 0x2c, 0x01}; /* rsrp -100、snr 300 */
    const uint8_t nr[4] = {0x92, 0xff, 0x64, 0x00};                                 /* rsrp -110、snr 100 */
    response(&m, 9, FM_NAS_GET_SIGNAL_INFO, 0, 0);
    tlv(&m, 0x14, lte, 6);
    tlv(&m, 0x17, nr, 4);
    tlv8(&m, 0x12, (uint8_t)-90);
    CHECK(fm_qmi_decode(FM_QMI_NAS, m.b, m.n, &a) == 0 && a.has_lte && a.lte_rssi == -70 && a.lte_rsrq == -11 && a.lte_rsrp == -100);
    CHECK(a.lte_snr == 300 && a.has_nr && a.nr_rsrp == -110 && a.nr_snr == 100 && a.has_gsm && a.gsm_rssi == -90 && !a.has_wcdma);

    /* Indication（type 4）、非法 type、沒有結果 TLV、服務不對 */
    response(&m, 9, FM_NAS_GET_SIGNAL_INFO, 0, 0);
    m.b[0] = 4;
    CHECK(fm_qmi_decode(FM_QMI_NAS, m.b, m.n, &a) == 0 && a.is_indication && a.message == FM_NAS_GET_SIGNAL_INFO && !a.has_result);
    m.b[0] = 0;
    CHECK(fm_qmi_decode(FM_QMI_NAS, m.b, m.n, &a) == -1);
    m.b[0] = 3;
    CHECK(fm_qmi_decode(FM_QMI_NAS, m.b, m.n, &a) == -1);
    m.b[0] = 4; m.b[5] = 0xff; m.b[6] = 0; /* payload 超長 */
    CHECK(fm_qmi_decode(FM_QMI_NAS, m.b, m.n, &a) == -1);
    begin(&m, 9, FM_NAS_GET_SIGNAL_INFO); m.b[0] = 2;
    CHECK(fm_qmi_decode(FM_QMI_NAS, m.b, m.n, &a) == -1);
    response(&m, 3, FM_DMS_GET_OPERATING_MODE, 0, 0);
    tlv8(&m, 0x01, 0);
    CHECK(fm_qmi_decode(FM_QMI_NAS, m.b, m.n, &a) == 0 && !a.has_mode);
    CHECK(fm_qmi_decode(FM_QMI_DMS, NULL, 7, &a) == -1 && fm_qmi_decode(FM_QMI_DMS, m.b, 6, &a) == -1);

    /* 亂數回應：不越界（--sanitize） */
    uint32_t qseed = 0x5eed1234u, decoded = 0;
    static const uint32_t services[3] = {FM_QMI_DMS, FM_QMI_NAS, FM_QMI_UIM};
    static const uint16_t messages[6] = {FM_DMS_GET_OPERATING_MODE, FM_DMS_GET_REVISION, FM_UIM_GET_CARD_STATUS,
                                         FM_NAS_GET_SERVING_SYSTEM, FM_NAS_GET_SIGNAL_INFO, 0x55};
    for (unsigned round = 0; round < 100000; ++round) {
        qseed = qseed * 1103515245u + 12345u;
        response(&m, (uint16_t)round, messages[(qseed >> 8) % 6], (qseed >> 20) % 4 == 0, 0);
        const unsigned tlvs = (qseed >> 4) % 4;
        for (unsigned t = 0; t < tlvs; ++t) {
            uint8_t value[80];
            qseed = qseed * 1103515245u + 12345u;
            const uint16_t size = (uint16_t)((qseed >> 9) % sizeof value);
            for (unsigned i = 0; i < size; ++i) { qseed = qseed * 1103515245u + 12345u; value[i] = (uint8_t)(qseed >> 16); }
            static const uint8_t types[8] = {0x01, 0x10, 0x12, 0x13, 0x14, 0x17, 0x02, 0x11};
            tlv(&m, types[(qseed >> 3) % 8], value, size);
        }
        uint32_t length = m.n;
        qseed = qseed * 1103515245u + 12345u;
        if (qseed % 8 == 0) length = (qseed >> 10) % (m.n + 1);
        if (fm_qmi_decode(services[(qseed >> 13) % 3], m.b, length, &a) == 0) decoded++;
        CHECK(strlen(a.revision) < sizeof a.revision && a.radio_count <= sizeof a.radio);
    }
    printf("qmi fuzz: %u of 100000 random responses decoded\n", decoded);
    CHECK(decoded > 10000);
}

static void dms_offline_and_tftp(void) {
    Msg m;
    FmQmiAnswer a;
    /* DMS GET_OPERATING_MODE 帶離線原因（0x10）與硬體限制（0x11） */
    response(&m, 11, FM_DMS_GET_OPERATING_MODE, 0, 0);
    tlv8(&m, 0x01, FM_DMS_MODE_OFFLINE);
    const uint8_t reason[2] = {0x04, 0x00};
    tlv(&m, 0x10, reason, 2);
    tlv8(&m, 0x11, 1);
    CHECK(fm_qmi_decode(FM_QMI_DMS, m.b, m.n, &a) == 0 && a.has_mode && a.mode == FM_DMS_MODE_OFFLINE);
    CHECK(a.has_offline_reason && a.offline_reason == 4u && a.has_hw_restricted && a.hw_restricted == 1u);

    /* QRTR DATA 封包封裝（含 4-byte padding 與上限拒絕） */
    uint8_t pkt[128];
    const uint8_t payload[5] = {0xde, 0xad, 0xbe, 0xef, 0x42};
    CHECK(fm_qrtr_data(pkt, sizeof pkt, 1, 0x4100, 0, 0x22, payload, 5) == FM_QRTR_HEADER + 8u);
    CHECK(fm_le32(pkt) == 1u && fm_le32(pkt + 4) == FM_QRTR_DATA && fm_le32(pkt + 8) == 1u && fm_le32(pkt + 12) == 0x4100u);
    CHECK(fm_le32(pkt + 20) == 5u && fm_le32(pkt + 24) == 0u && fm_le32(pkt + 28) == 0x22u);
    CHECK(pkt[FM_QRTR_HEADER + 4] == 0x42u && pkt[FM_QRTR_HEADER + 5] == 0u && pkt[FM_QRTR_HEADER + 7] == 0u);
    CHECK(fm_qrtr_data(pkt, FM_QRTR_HEADER + 4u, 1, 3, 0, 1, payload, 5) == 0);

    /* QRTR RESUME_TX（流控恢復）：標頭 from=(local,dst_port) dst=(src_node,src_port)，payload={7,local,dst_port,0,0} */
    uint8_t resume[FM_QRTR_CTRL_PACKET];
    CHECK(fm_qrtr_resume_tx(resume, 1u, 3u, 0u, 122u) == FM_QRTR_CTRL_PACKET);
    CHECK(fm_le32(resume) == 1u && fm_le32(resume + 4) == FM_QRTR_RESUME_TX);
    CHECK(fm_le32(resume + 8) == 1u && fm_le32(resume + 12) == 3u && fm_le32(resume + 16) == 0u && fm_le32(resume + 20) == 20u);
    CHECK(fm_le32(resume + 24) == 0u && fm_le32(resume + 28) == 122u);
    CHECK(fm_le32(resume + 32) == FM_QRTR_RESUME_TX && fm_le32(resume + 36) == 1u && fm_le32(resume + 40) == 3u);
    CHECK(fm_le32(resume + 44) == 0u && fm_le32(resume + 48) == 0u);

    /* TFTP RRQ 解析、選項夾限、WRQ 解析、非法請求拒絕 */
    static const uint8_t rrq[] =
        "\0\x01/readonly/firmware/image/modem_pr/mcfg/a.mbn\0octet\0"
        "blksize\0""16384\0wsize\0""8\0rsize\0""4096\0seek\0""512\0tsize\0""0\0timeoutms\0""2500\0";
    FmTftpRequest req;
    CHECK(fm_tftp_parse_request(rrq, (uint32_t)(sizeof rrq - 1), &req) == 0);
    CHECK(req.opcode == FM_TFTP_RRQ && strcmp(req.path, "/readonly/firmware/image/modem_pr/mcfg/a.mbn") == 0);
    CHECK(req.has_options && req.has_blksize && req.has_wsize && req.has_rsize && req.has_seek && req.has_tsize && req.has_timeoutms);
    CHECK(!req.has_timeout && !req.has_append && !req.has_unlink && req.unknown_opts == 0);
    CHECK(req.blksize == FM_TFTP_MAX_BLOCK && req.wsize == FM_TFTP_MAX_WINDOW && req.rsize == 4096u && req.seek == 512u && req.timeoutms == 2500u);
    static const uint8_t wrq[] =
        "\0\x02/readwrite/mcfg.tmp\0octet\0blksize\0""4\0wsize\0""0\0seek\0""0\0append\0""1\0unlink\0""0\0foo\0""bar\0";
    CHECK(fm_tftp_parse_request(wrq, (uint32_t)(sizeof wrq - 1), &req) == 0);
    CHECK(req.opcode == FM_TFTP_WRQ && req.blksize == 8u && req.wsize == 1u && !req.has_tsize && !req.has_timeoutms);
    CHECK(req.has_seek && req.seek == 0u && req.has_append && req.has_unlink && req.unknown_opts == 1u && strcmp(req.unknown_name, "foo") == 0);
    static const uint8_t bad_num[] = "\0\x01/readonly/firmware/image/a\0octet\0blksize\0abc\0";
    CHECK(fm_tftp_parse_request(bad_num, (uint32_t)(sizeof bad_num - 1), &req) == -1);
    static const uint8_t bad_op[] = "\0\x03/readonly/firmware/image/a\0octet\0";
    CHECK(fm_tftp_parse_request(bad_op, (uint32_t)(sizeof bad_op - 1), &req) == -1);
    CHECK(fm_tftp_parse_request(rrq, 3, &req) == -1);

    /* 唯讀路徑對應與穿越防護 */
    char rel[128];
    CHECK(fm_tftp_fw_relpath("/readonly/firmware/image/modem_pr/so/810_0_0.mbn", rel, sizeof rel) == 0);
    CHECK(strcmp(rel, "image/modem_pr/so/810_0_0.mbn") == 0);
    CHECK(fm_tftp_fw_relpath("/readonly/vendor/firmware_mnt/image/modemr.jsn", rel, sizeof rel) == 0);
    CHECK(strcmp(rel, "image/modemr.jsn") == 0);
    CHECK(fm_tftp_fw_relpath("/readonly/vendor/firmware/image/a.bin", rel, sizeof rel) == 0 && strcmp(rel, "image/a.bin") == 0);
    CHECK(fm_tftp_fw_relpath("/readonly/firmware/image/../secret", rel, sizeof rel) == -1);
    CHECK(fm_tftp_fw_relpath("/readwrite/server_check.txt", rel, sizeof rel) == -1);
    CHECK(fm_tftp_fw_relpath("/readonly/firmware/image/", rel, sizeof rel) == -1);

    /* OACK／ERROR／ACK 封包建立 */
    uint8_t oack[160], err[64], ack[4];
    CHECK(fm_tftp_parse_request(rrq, (uint32_t)(sizeof rrq - 1), &req) == 0);
    const uint32_t olen = fm_tftp_build_oack(oack, sizeof oack, &req, 123456);
    CHECK(olen > 20 && oack[0] == 0 && oack[1] == FM_TFTP_OACK);
    CHECK(fm_tftp_build_oack(oack, 16, &req, 123456) == 0);
    /* 只回送 client 請求的選項，且 seek=0／append／unlink 皆保留 */
    CHECK(fm_tftp_parse_request(wrq, (uint32_t)(sizeof wrq - 1), &req) == 0);
    const uint32_t wolen = fm_tftp_build_oack(oack, sizeof oack, &req, 0);
    static const uint8_t expected_woack[] =
        "\0\x06""blksize\0""8\0wsize\0""1\0seek\0""0\0append\0""0\0unlink\0""0\0";
    CHECK(wolen == (uint32_t)(sizeof expected_woack - 1) && memcmp(oack, expected_woack, wolen) == 0);
    /* 只有未知選項時不送空 OACK */
    static const uint8_t unk_only[] = "\0\x02/readwrite/x\0octet\0mystery\0""1\0";
    CHECK(fm_tftp_parse_request(unk_only, (uint32_t)(sizeof unk_only - 1), &req) == 0);
    CHECK(fm_tftp_build_oack(oack, sizeof oack, &req, 0) == 0);
    const uint32_t elen = fm_tftp_build_error(err, sizeof err, FM_TFTP_ENOENT, "file not found");
    CHECK(elen == 4u + 15u && err[1] == FM_TFTP_ERROR && err[3] == FM_TFTP_ENOENT && strcmp((const char *)err + 4, "file not found") == 0);
    fm_tftp_build_ack(ack, 0x1234u);
    CHECK(ack[0] == 0 && ack[1] == FM_TFTP_ACK && ack[2] == 0x12u && ack[3] == 0x34u);

    /* RRQ 區塊切分：rsize 或檔案大小正好是 blksize 整倍數時，必須續送 0 位元組 DATA 結尾包（實機 834_0_0.mbn rsize=4096 blk=2048） */
    uint64_t off = 0;
    uint32_t take = 0;
    int is_eof = 0;
    CHECK(fm_tftp_rrq_block(65536u, 0u, 4096u, 2048u, 1u, &off, &take, &is_eof) == 1 && off == 0u && take == 2048u && !is_eof);
    CHECK(fm_tftp_rrq_block(65536u, 0u, 4096u, 2048u, 2u, &off, &take, &is_eof) == 1 && off == 2048u && take == 2048u && !is_eof);
    CHECK(fm_tftp_rrq_block(65536u, 0u, 4096u, 2048u, 3u, &off, &take, &is_eof) == 1 && off == 4096u && take == 0u && is_eof);
    CHECK(fm_tftp_rrq_block(65536u, 0u, 4096u, 2048u, 4u, &off, &take, &is_eof) == 0);
    /* 無 rsize 且檔案剩餘長度為 blksize 整倍數（含 seek） */
    CHECK(fm_tftp_rrq_block(5000u, 904u, 0u, 2048u, 1u, &off, &take, &is_eof) == 1 && off == 904u && take == 2048u && !is_eof);
    CHECK(fm_tftp_rrq_block(5000u, 904u, 0u, 2048u, 2u, &off, &take, &is_eof) == 1 && off == 2952u && take == 2048u && !is_eof);
    CHECK(fm_tftp_rrq_block(5000u, 904u, 0u, 2048u, 3u, &off, &take, &is_eof) == 1 && off == 5000u && take == 0u && is_eof);
    CHECK(fm_tftp_rrq_block(5000u, 904u, 0u, 2048u, 4u, &off, &take, &is_eof) == 0);
    /* 短檔、空檔、非整倍數 rsize、seek 超過檔尾、非法參數 */
    CHECK(fm_tftp_rrq_block(32u, 0u, 0u, 2048u, 1u, &off, &take, &is_eof) == 1 && off == 0u && take == 32u && is_eof);
    CHECK(fm_tftp_rrq_block(32u, 0u, 0u, 2048u, 2u, &off, &take, &is_eof) == 0);
    CHECK(fm_tftp_rrq_block(0u, 0u, 0u, 2048u, 1u, &off, &take, &is_eof) == 1 && off == 0u && take == 0u && is_eof);
    CHECK(fm_tftp_rrq_block(5000u, 100u, 3000u, 2048u, 1u, &off, &take, &is_eof) == 1 && off == 100u && take == 2048u && !is_eof);
    CHECK(fm_tftp_rrq_block(5000u, 100u, 3000u, 2048u, 2u, &off, &take, &is_eof) == 1 && off == 2148u && take == 952u && is_eof);
    CHECK(fm_tftp_rrq_block(5000u, 100u, 3000u, 2048u, 3u, &off, &take, &is_eof) == 0);
    CHECK(fm_tftp_rrq_block(1000u, 2000u, 0u, 2048u, 1u, &off, &take, &is_eof) == 1 && off == 2000u && take == 0u && is_eof);
    CHECK(fm_tftp_rrq_block(4096u, 0u, 0u, 0u, 1u, &off, &take, &is_eof) == 0);
    CHECK(fm_tftp_rrq_block(4096u, 0u, 0u, 2048u, 0u, &off, &take, &is_eof) == 0);

    /* 亂數 TFTP 封包：不越界 */
    uint32_t tseed = 0x7f7f1234u;
    for (unsigned round = 0; round < 20000; ++round) {
        uint8_t buf[180];
        tseed = tseed * 1103515245u + 12345u;
        const uint32_t len = (tseed >> 16) % sizeof buf;
        for (uint32_t idx = 0; idx < len; ++idx) { tseed = tseed * 1103515245u + 12345u; buf[idx] = (uint8_t)(tseed >> 24); }
        if (len >= 2) { buf[0] = 0; buf[1] = (uint8_t)(1u + (round % 3u)); }
        if (fm_tftp_parse_request(buf, len, &req) == 0) {
            CHECK(req.blksize >= 8u && req.blksize <= FM_TFTP_MAX_BLOCK && req.wsize >= 1u && req.wsize <= FM_TFTP_MAX_WINDOW);
            (void)fm_tftp_fw_relpath(req.path, rel, sizeof rel);
        }
    }
}

static void m5_qmi_and_ipa(void) {
    /* 雙槽 UIM：slot 0 ERROR、slot 1 PRESENT USIM READY + 16B AID，primary_gw = 0xffff */
    uint8_t card[128]; unsigned pos = 0;
    for (unsigned idx = 0; idx < 8; ++idx) card[pos++] = 0xff;
    card[pos++] = 2; /* 2 槽 */
    /* slot 0: state=2(ERROR), upin=0, upin_ret=0, upuk_ret=0, err=3, apps=0 */
    card[pos++] = 2; card[pos++] = 0; card[pos++] = 0; card[pos++] = 0; card[pos++] = 3; card[pos++] = 0;
    /* slot 1: state=1(PRESENT), upin=0, ret=3, puk=10, err=0, apps=1 (USIM READY + 16B AID) */
    card[pos++] = 1; card[pos++] = 0; card[pos++] = 3; card[pos++] = 10; card[pos++] = 0; card[pos++] = 1;
    card[pos++] = 2; card[pos++] = 7; card[pos++] = 3; card[pos++] = 0; card[pos++] = 0; card[pos++] = 0;
    card[pos++] = 16; for (unsigned idx = 0; idx < 16; ++idx) card[pos++] = (uint8_t)(0xb0 + idx);
    card[pos++] = 0; card[pos++] = 2; card[pos++] = 3; card[pos++] = 10; card[pos++] = 0; card[pos++] = 3; card[pos++] = 10;
    Msg msg; FmQmiAnswer ans;
    response(&msg, 11, FM_UIM_GET_CARD_STATUS, 0, 0);
    tlv(&msg, 0x10, card, (uint16_t)pos);
    CHECK(fm_qmi_decode(FM_QMI_UIM, msg.b, msg.n, &ans) == 0 && ans.has_card && ans.slots == 2 && ans.slot_count == 2);
    CHECK(ans.primary_gw == 0xffffu && ans.card_state == 2 && ans.card_error == 3 && ans.apps == 0);
    CHECK(ans.slot[0].card_state == 2 && ans.slot[0].card_error == 3 && !ans.slot[0].has_gw_app);
    CHECK(ans.slot[1].card_state == 1 && ans.slot[1].apps == 1 && ans.slot[1].has_gw_app);
    CHECK(ans.slot[1].gw_app_type == 2 && ans.slot[1].gw_app_state == 7 && ans.slot[1].gw_aid_len == 16);
    CHECK(ans.slot[1].gw_aid[0] == 0xb0 && ans.slot[1].gw_aid[15] == 0xbf);
    CHECK(ans.slot[1].pin1_state == 2 && ans.slot[1].pin1_retries == 3 && ans.slot[1].puk1_retries == 10);
    for (unsigned cut = 9; cut < pos; ++cut) {
        FmQmiAnswer cut_ans;
        response(&msg, 11, FM_UIM_GET_CARD_STATUS, 0, 0);
        tlv(&msg, 0x10, card, (uint16_t)cut);
        CHECK(fm_qmi_decode(FM_QMI_UIM, msg.b, msg.n, &cut_ans) == -1);
    }

    /* UIM 超過 FM_UIM_MAX_SLOTS（5 槽空卡）安全截斷至 4 槽保存 */
    uint8_t five[9 + 5 * 6];
    memset(five, 0, sizeof five);
    five[0] = 0; five[1] = 0; five[8] = 5;
    FmQmiAnswer ans5;
    response(&msg, 12, FM_UIM_GET_CARD_STATUS, 0, 0);
    tlv(&msg, 0x10, five, (uint16_t)sizeof five);
    CHECK(fm_qmi_decode(FM_QMI_UIM, msg.b, msg.n, &ans5) == 0 && ans5.slots == 5 && ans5.slot_count == FM_UIM_MAX_SLOTS);
    CHECK(ans5.primary_gw == 0);

    /* 請求編碼器：UIM switch / provision、DPM、WDA、WDS */
    uint8_t buf[64];
    uint32_t len = fm_qmi_uim_switch_slot(buf, sizeof buf, 0x34, 1, 2);
    CHECK(len == 18 && buf[0] == 0 && le16(buf + 3) == FM_UIM_SWITCH_SLOT && le16(buf + 5) == 11);
    CHECK(buf[7] == 1 && le16(buf + 8) == 1 && buf[10] == 1 && buf[11] == 2 && le16(buf + 12) == 4 && fm_le32(buf + 14) == 2);
    CHECK(fm_qmi_uim_switch_slot(buf, 17, 1, 1, 2) == 0 && fm_qmi_uim_switch_slot(buf, sizeof buf, 1, 0, 2) == 0);

    len = fm_qmi_uim_change_provisioning(buf, sizeof buf, 0x37, 0, 1, 1, ans.slot[1].gw_aid, ans.slot[1].gw_aid_len);
    CHECK(len == 33 && le16(buf + 3) == FM_UIM_CHANGE_PROVISIONING && le16(buf + 5) == 26);
    CHECK(buf[7] == 1 && le16(buf + 8) == 2 && buf[10] == 0 && buf[11] == 1);
    CHECK(buf[12] == 0x10 && le16(buf + 13) == 18 && buf[15] == 1 && buf[16] == 16 && buf[17] == 0xb0 && buf[32] == 0xbf);
    CHECK(fm_qmi_uim_change_provisioning(buf, 32, 1, 0, 1, 1, ans.slot[1].gw_aid, 16) == 0);
    CHECK(fm_qmi_uim_change_provisioning(buf, sizeof buf, 1, 0, 1, 1, ans.slot[1].gw_aid, 0) == 0);

    len = fm_qmi_dpm_open_port(buf, sizeof buf, 0x45, FM_DATA_EP_EMBEDDED, FM_DATA_EP_IFACE, FM_IPA5_EP_TO_MODEM,
        FM_IPA5_EP_FROM_MODEM);
    CHECK(len == 27 && le16(buf + 3) == FM_DPM_OPEN_PORT && le16(buf + 5) == 20);
    CHECK(buf[7] == 0x11 && le16(buf + 8) == 17 && buf[10] == 1 && fm_le32(buf + 11) == 4 && fm_le32(buf + 15) == 1);
    CHECK(fm_le32(buf + 19) == 2 && fm_le32(buf + 23) == 24);

    len = fm_qmi_wda_set_data_format(buf, sizeof buf, 0x43, FM_DATA_EP_EMBEDDED, FM_DATA_EP_IFACE, FM_WDA_DL_MAX_DGRAMS,
        FM_WDA_DL_MAX_SIZE);
    CHECK(len == 53 && le16(buf + 3) == FM_WDA_SET_DATA_FORMAT && le16(buf + 5) == 46);
    CHECK(buf[7] == 0x11 && fm_le32(buf + 10) == 2 && buf[14] == 0x12 && fm_le32(buf + 17) == 5 && buf[21] == 0x13 &&
        fm_le32(buf + 24) == 5 && buf[28] == 0x15 && fm_le32(buf + 31) == 16 && buf[35] == 0x16 && fm_le32(buf + 38) == 0x3800);
    CHECK(buf[42] == 0x17 && fm_le32(buf + 45) == 4 && fm_le32(buf + 49) == 1);

    len = fm_qmi_wds_bind_mux(buf, sizeof buf, 0x44, FM_DATA_EP_EMBEDDED, FM_DATA_EP_IFACE, FM_DATA_MUX_ID);
    CHECK(len == 22 && le16(buf + 3) == FM_WDS_BIND_MUX_DATA_PORT && le16(buf + 5) == 15);
    CHECK(buf[7] == 0x10 && fm_le32(buf + 10) == 4 && fm_le32(buf + 14) == 1 && buf[18] == 0x11 && buf[21] == 1);
    CHECK(fm_qmi_wds_bind_mux(buf, sizeof buf, 1, 4, 1, 0) == 0);
    CHECK(fm_qmi_wds_set_ip_family(buf, sizeof buf, 0x46, 4) == 11 && le16(buf + 3) == FM_WDS_SET_IP_FAMILY && buf[10] == 4);
    CHECK(fm_qmi_wds_start_network(buf, sizeof buf, 0x41, 4) == 11 && le16(buf + 3) == FM_WDS_START_NETWORK && buf[7] == 0x19 &&
        buf[10] == 4);
    len = fm_qmi_wds_get_current_settings(buf, sizeof buf, 0x42, FM_WDS_SETTINGS_MASK);
    CHECK(len == 14 && le16(buf + 3) == FM_WDS_GET_CURRENT_SETTINGS && buf[7] == 0x10 && fm_le32(buf + 10) == 0x0000a310u);

    /* 解碼器：WDA format、WDS start（成功與錯誤理由）、WDS settings（隱私安全） */
    response(&msg, 20, FM_WDA_SET_DATA_FORMAT, 0, 0);
    tlv32(&msg, 0x11, 2); tlv32(&msg, 0x12, 5); tlv32(&msg, 0x13, 5); tlv32(&msg, 0x15, 16); tlv32(&msg, 0x16, 0x3800);
    CHECK(fm_qmi_decode(FM_QMI_WDA, msg.b, msg.n, &ans) == 0 && ans.has_wda_format && ans.wda_link_layer == 2 &&
        ans.wda_ul_aggr == 5 && ans.wda_dl_aggr == 5 && ans.wda_dl_max_dgrams == 16 && ans.wda_dl_max_size == 0x3800);

    response(&msg, 21, FM_WDS_START_NETWORK, 0, 0);
    tlv32(&msg, 0x01, 0x12345678u);
    CHECK(fm_qmi_decode(FM_QMI_WDS, msg.b, msg.n, &ans) == 0 && ans.has_wds_handle && ans.wds_handle == 0x12345678u);

    response(&msg, 22, FM_WDS_START_NETWORK, 1, 14);
    const uint8_t cer[2] = {0x02, 0x00};
    const uint8_t vcer[4] = {0x06, 0x00, 0x21, 0x00};
    tlv(&msg, 0x10, cer, 2); tlv(&msg, 0x11, vcer, 4);
    CHECK(fm_qmi_decode(FM_QMI_WDS, msg.b, msg.n, &ans) == 0 && ans.result == 1 && ans.error == 14 && !ans.has_wds_handle);
    CHECK(ans.has_call_end_reason && ans.call_end_reason == 2 && ans.has_verbose_reason && ans.verbose_reason_type == 6 &&
        ans.verbose_reason == 0x21);

    response(&msg, 23, FM_WDS_GET_CURRENT_SETTINGS, 0, 0);
    tlv32(&msg, 0x1e, 0x0a000001u); tlv32(&msg, 0x20, 0x0a000002u); tlv32(&msg, 0x21, 0xfffffffcu);
    tlv32(&msg, 0x15, 0x08080808u); tlv32(&msg, 0x16, 0u); tlv32(&msg, 0x29, 1420u); tlv8(&msg, 0x2b, 4u);
    CHECK(fm_qmi_decode(FM_QMI_WDS, msg.b, msg.n, &ans) == 0 && ans.has_wds_settings);
    CHECK(ans.wds_has_ip && ans.wds_has_gw && ans.wds_has_mask && ans.wds_has_dns1 && !ans.wds_has_dns2);
    CHECK(ans.wds_mtu == 1420u && ans.wds_ip_family == 4u);

    /* IPA v5.0 / BCM / SMMU 純邏輯 */
    CHECK(fm_is_bcm(0x00050123u) && !fm_is_bcm(0x00040100u) && !fm_is_bcm(0u));
    CHECK(fm_bcm_vote(1, 0x3fffu) == 0x60003fffu && fm_bcm_vote(0, 0u) == 0x40000000u);
    CHECK(fm_ipa_shared_mem_offset(0x02000400u) == 0x1000u && fm_ipa_shared_mem_size(0x02000400u) == 0x2000u);
    uint32_t smrs[8] = {0}, s2crs[8];
    for (unsigned idx = 0; idx < 8; ++idx) s2crs[idx] = FM_SMMU_FAULT_S2CR;
    smrs[0] = (1u << 31) | 0x40u; s2crs[0] = FM_SMMU_BYPASS_BANK;
    CHECK(fm_smmu_overlap(smrs[0], 0x40u, 0u) && !fm_smmu_overlap(smrs[0], 0x4a0u, 0u));
    CHECK(fm_smmu_pick_high_slot(smrs, s2crs, 8, FM_SMMU_IPA_AP_SID, 0u) == 7);
    smrs[7] = (1u << 31) | FM_SMMU_IPA_AP_SID; s2crs[7] = FM_SMMU_BYPASS_BANK;
    CHECK(fm_smmu_pick_high_slot(smrs, s2crs, 8, FM_SMMU_IPA_AP_SID, 0u) == -2);
    CHECK(fm_smmu_pick_high_slot(smrs, s2crs, 8, FM_SMMU_IPA_UC_SID, 0u) == 6);
    for (unsigned idx = 0; idx < 8; ++idx) s2crs[idx] = FM_SMMU_BYPASS_BANK;
    CHECK(fm_smmu_pick_high_slot(smrs, s2crs, 8, 0x4a4u, 0u) == -1);
}

static void m6_ipa_gsi_qmi(void) {
    /* QRTR lookup 萬用字元：service 須完全相符，版本（低 8 位）與執行個體號（高 24 位）各自 0 表不限 */
    CHECK(fm_lookup_matches(FM_QMI_IPA, FM_IPA_HOST_INSTANCE, FM_QMI_IPA, FM_IPA_HOST_INSTANCE));
    CHECK(fm_lookup_matches(FM_QMI_IPA, 0u, FM_QMI_IPA, FM_IPA_HOST_INSTANCE));
    CHECK(fm_lookup_matches(FM_QMI_IPA, 0x001u, FM_QMI_IPA, FM_IPA_HOST_INSTANCE));
    CHECK(fm_lookup_matches(FM_QMI_IPA, 0x100u, FM_QMI_IPA, FM_IPA_HOST_INSTANCE));
    CHECK(!fm_lookup_matches(FM_QMI_IPA, 0x102u, FM_QMI_IPA, FM_IPA_HOST_INSTANCE));
    CHECK(!fm_lookup_matches(FM_QMI_IPA, 0x201u, FM_QMI_IPA, FM_IPA_HOST_INSTANCE));
    CHECK(!fm_lookup_matches(FM_QMI_IPA, 0x200u, FM_QMI_IPA, FM_IPA_HOST_INSTANCE));
    CHECK(!fm_lookup_matches(FM_QMI_IPA, 0x002u, FM_QMI_IPA, FM_IPA_HOST_INSTANCE));
    CHECK(!fm_lookup_matches(0u, 0u, FM_QMI_IPA, FM_IPA_HOST_INSTANCE));
    CHECK(!fm_lookup_matches(FM_QMI_DMS, 0u, FM_QMI_IPA, FM_IPA_HOST_INSTANCE));

    /* IPA v5.5 暫存器與路由 helper */
    CHECK(fm_ipa_rsrc_pair(1u, 2u, 3u, 4u) == (1u | (2u << 8) | (3u << 16) | (4u << 24)));
    CHECK(fm_ipa_rsrc_pair(0x7fu, 0xffu, 0x45u, 0xc9u) == (0x3fu | (0x3fu << 8) | (0x05u << 16) | (0x09u << 24)));
    CHECK(fm_ipa_route_reg(FM_IPA5_EP_LAN_RX) == (16u | (16u << 8) | (1u << 26) | (1u << 27)));
    CHECK(FM_IPA5_SRC_RSRC_GRP_01(2u) == 0x540u && FM_IPA5_DST_RSRC_GRP_67(1u) == 0x62cu);
    CHECK(FM_IPA5_ENDP_INIT_CFG(14u) == 0x1708u && FM_IPA5_ENDP_ROUTER_CACHE_CFG(1u) == 0x10e0u);

    /* GSI v5.0 暫存器與 TRE helper */
    CHECK(fm_gsi_ev_cntxt0() == (2u | (1u << 7) | (16u << 24)));
    CHECK(fm_gsi_ch_cntxt0(0) == (2u | (16u << 24)));
    CHECK(fm_gsi_ch_cntxt0(1) == (2u | (1u << 7) | (16u << 24)));
    CHECK(fm_gsi_ch_cntxt1(0x1000u, 3u) == (0x1000u | (3u << 24)));
    CHECK(fm_gsi_ch_qos(1) == (0xfu | (1u << 24)));
    CHECK(fm_gsi_ch_qos(0) == ((1u << 10) | (1u << 24)));
    CHECK(fm_gsi_cmd_word(FM_IPA5_CH_CMD_TX, FM_GSI_CMD_START) == (12u | (1u << 24)));
    CHECK(fm_gsi_cmd_word(FM_IPA5_CH_MODEM_RX, FM_GSI_CMD_ALLOCATE) == 1u);

    uint8_t tre[16];
    fm_gsi_tre_fill(tre, 0x812345678ull, 0x2000u, 0, 1);
    CHECK(le64(tre) == 0x812345678ull && le16(tre + 8) == 0x2000u && le16(tre + 10) == 0u);
    CHECK(fm_le32(tre + 12) == (((uint32_t)FM_GSI_RE_XFER << 16) | (1u << 9)));
    fm_gsi_tre_fill(tre, 0x89abcdef0ull, FM_IPA_CMD_DMA_SHARED_MEM, 1, 0);
    CHECK(le64(tre) == 0x89abcdef0ull && le16(tre + 8) == FM_IPA_CMD_DMA_SHARED_MEM && le16(tre + 10) == 0u);
    CHECK(fm_le32(tre + 12) == (((uint32_t)FM_GSI_RE_IMMD_CMD << 16) | 1u));
    fm_gsi_tre_fill(tre, 0x800001000ull, FM_IPA_CMD_V4_FILTER_INIT, 1, 1);
    CHECK(fm_le32(tre + 12) == (((uint32_t)FM_GSI_RE_IMMD_CMD << 16) | (1u << 9)));

    /* IPA 立即命令 payload：HDR_INIT_LOCAL、DMA_SHARED_MEM、TABLE_INIT */
    uint8_t cmd16[16];
    fm_ipa_cmd_hdr_init_local(cmd16, 0x811223340ull, FM_IPA5_MEM_MODEM_HDR_OFF, FM_IPA5_MEM_MODEM_HDR_SIZE);
    CHECK(le64(cmd16) == 0x811223340ull);
    CHECK(fm_le32(cmd16 + 8) == ((uint32_t)FM_IPA5_MEM_MODEM_HDR_SIZE | (FM_IPA5_MEM_MODEM_HDR_OFF << 12)));
    CHECK(fm_le32(cmd16 + 12) == 0u);

    fm_ipa_cmd_dma_shared_mem(cmd16, 0x822334450ull, FM_IPA5_MEM_AP_CTX_OFF, FM_IPA5_MEM_AP_CTX_SIZE, 1);
    CHECK(le16(cmd16) == 0u && le16(cmd16 + 2) == FM_IPA5_MEM_AP_CTX_SIZE && le16(cmd16 + 4) == FM_IPA5_MEM_AP_CTX_OFF);
    CHECK(le16(cmd16 + 6) == 0u && le64(cmd16 + 8) == 0x822334450ull);
    fm_ipa_cmd_dma_shared_mem(cmd16, 0x833445560ull, 0x100u, 0x40u, 0);
    CHECK(le16(cmd16 + 6) == 1u && le64(cmd16 + 8) == 0x833445560ull);

    uint8_t cmd24[24];
    fm_ipa_cmd_table_init(cmd24, 0x844556670ull, FM_IPA5_MEM_V4_FLT_OFF, FM_IPA5_FILTER_INIT_SIZE, 0x8deadbeefull, 0x1288u, 0u);
    CHECK(le64(cmd24) == 0ull && le64(cmd24 + 16) == 0x844556670ull);
    CHECK(le64(cmd24 + 8) == (((uint64_t)FM_IPA5_FILTER_INIT_SIZE << 28) | ((uint64_t)FM_IPA5_MEM_V4_FLT_OFF << 40)));

    fm_ipa_cmd_table_init(cmd24, 0x855667780ull, FM_IPA5_MEM_V4_RT_OFF, FM_IPA5_MEM_RT_SIZE,
        0x866778890ull, FM_IPA5_MEM_V4_RT_HASH_OFF, FM_IPA5_MEM_RT_SIZE);
    CHECK(le64(cmd24) == 0x866778890ull && le64(cmd24 + 16) == 0x855667780ull);
    CHECK(le64(cmd24 + 8) == (((uint64_t)FM_IPA5_MEM_RT_SIZE) | ((uint64_t)FM_IPA5_MEM_V4_RT_HASH_OFF << 12) |
        ((uint64_t)FM_IPA5_MEM_RT_SIZE << 28) | ((uint64_t)FM_IPA5_MEM_V4_RT_OFF << 40)));

    /* QMI IPA 0x31 編碼器：result_msg（14 B） */
    uint8_t msg[320];
    CHECK(fm_ipa_qmi_result_msg(NULL, 14u, FM_QMI_TYPE_RESPONSE, 1u, FM_IPA_QMI_IND_REGISTER, 0u, 0u) == 0u);
    CHECK(fm_ipa_qmi_result_msg(msg, 13u, FM_QMI_TYPE_RESPONSE, 1u, FM_IPA_QMI_IND_REGISTER, 0u, 0u) == 0u);
    uint32_t len = fm_ipa_qmi_result_msg(msg, sizeof msg, FM_QMI_TYPE_INDICATION, 0x1234u, FM_IPA_QMI_INIT_COMPLETE, 0u, 0u);
    CHECK(len == 14u && msg[0] == FM_QMI_TYPE_INDICATION && le16(msg + 1) == 0x1234u &&
        le16(msg + 3) == FM_IPA_QMI_INIT_COMPLETE && le16(msg + 5) == 7u);
    CHECK(msg[7] == 0x02u && le16(msg + 8) == 4u && le16(msg + 10) == 0u && le16(msg + 12) == 0u);

    /* SMEM 497 peripheral stats 標頭（43 個 u32 = 172 B；對照 ipam.ko ipa3_peripheral_stats_init） */
    uint32_t per_words[FM_IPA5_PER_STATS_WORDS];
    CHECK(fm_ipa_init_per_stats_words(NULL, FM_IPA5_PER_STATS_WORDS) == 0u);
    CHECK(fm_ipa_init_per_stats_words(per_words, FM_IPA5_PER_STATS_WORDS - 1u) == 0u);
    CHECK(fm_ipa_init_per_stats_words(per_words, FM_IPA5_PER_STATS_WORDS) == FM_IPA5_PER_STATS_WORDS);
    CHECK(per_words[0] == 0u && per_words[1] == 20u && per_words[2] == 0x00040000u && per_words[3] == 0u);
    CHECK(per_words[14] == 0x00080006u && per_words[15] == 0u && per_words[16] == 0u && per_words[17] == 0x00040007u);
    CHECK(per_words[41] == 0x00040013u && per_words[42] == 0u);

    /* QMI IPA 0x21 INIT_DRIVER 請求（189 B，全部 20 個 TLV 含 SM8550 0x23/0x24/0x25） */
    const uint32_t mem_off = 0x1000u;
    const uint32_t smem_sz = 0xb000u, smem_pa = 0x81d12340u;
    CHECK(FM_IPA5_EP_FROM_MODEM == 24u);
    CHECK(fm_ipa_qmi_init_driver_req(NULL, 189u, 7u, mem_off, FM_IPA5_EP_FROM_MODEM, smem_sz, smem_pa) == 0u);
    CHECK(fm_ipa_qmi_init_driver_req(msg, 188u, 7u, mem_off, FM_IPA5_EP_FROM_MODEM, smem_sz, smem_pa) == 0u);
    len = fm_ipa_qmi_init_driver_req(msg, sizeof msg, 0x0042u, mem_off, FM_IPA5_EP_FROM_MODEM, smem_sz, smem_pa);
    CHECK(len == 189u && msg[0] == FM_QMI_TYPE_REQUEST && le16(msg + 1) == 0x0042u &&
        le16(msg + 3) == FM_IPA_QMI_INIT_DRIVER && le16(msg + 5) == 182u);
    uint32_t tlen = 0u;
    const uint8_t *tv = NULL;
    tv = fm_qmi_tlv(msg, len, 0x10u, &tlen); CHECK(tv && tlen == 4u && fm_le32(tv) == 3u);
    tv = fm_qmi_tlv(msg, len, 0x11u, &tlen);
    CHECK(tv && tlen == 8u && fm_le32(tv) == mem_off + FM_IPA5_MEM_MODEM_HDR_OFF &&
        fm_le32(tv + 4) == mem_off + FM_IPA5_MEM_MODEM_HDR_OFF + FM_IPA5_MEM_MODEM_HDR_SIZE - 1u);
    tv = fm_qmi_tlv(msg, len, 0x12u, &tlen);
    CHECK(tv && tlen == 8u && fm_le32(tv) == mem_off + FM_IPA5_MEM_V4_RT_OFF && fm_le32(tv + 4) == FM_IPA5_MODEM_ROUTE_COUNT - 1u);
    tv = fm_qmi_tlv(msg, len, 0x13u, &tlen);
    CHECK(tv && tlen == 8u && fm_le32(tv) == mem_off + FM_IPA5_MEM_V6_RT_OFF && fm_le32(tv + 4) == FM_IPA5_MODEM_ROUTE_COUNT - 1u);
    tv = fm_qmi_tlv(msg, len, 0x14u, &tlen); CHECK(tv && tlen == 4u && fm_le32(tv) == mem_off + FM_IPA5_MEM_V4_FLT_OFF);
    tv = fm_qmi_tlv(msg, len, 0x15u, &tlen); CHECK(tv && tlen == 4u && fm_le32(tv) == mem_off + FM_IPA5_MEM_V6_FLT_OFF);
    tv = fm_qmi_tlv(msg, len, 0x16u, &tlen);
    CHECK(tv && tlen == 8u && fm_le32(tv) == mem_off + FM_IPA5_MEM_MODEM_OFF && fm_le32(tv + 4) == FM_IPA5_MEM_MODEM_SIZE);
    tv = fm_qmi_tlv(msg, len, 0x17u, &tlen); CHECK(tv && tlen == 4u && fm_le32(tv) == FM_IPA5_EP_FROM_MODEM);
    CHECK(fm_qmi_tlv(msg, len, 0x18u, &tlen) == NULL);
    tv = fm_qmi_tlv(msg, len, 0x19u, &tlen);
    CHECK(tv && tlen == 8u && fm_le32(tv) == mem_off + FM_IPA5_MEM_MODEM_CTX_OFF &&
        fm_le32(tv + 4) == mem_off + FM_IPA5_MEM_MODEM_CTX_OFF + FM_IPA5_MEM_MODEM_CTX_SIZE - 1u);
    tv = fm_qmi_tlv(msg, len, 0x1bu, &tlen);
    CHECK(tv && tlen == 8u && fm_le32(tv) == mem_off + FM_IPA5_MEM_V4_RT_HASH_OFF && fm_le32(tv + 4) == FM_IPA5_MODEM_ROUTE_COUNT - 1u);
    tv = fm_qmi_tlv(msg, len, 0x1cu, &tlen);
    CHECK(tv && tlen == 8u && fm_le32(tv) == mem_off + FM_IPA5_MEM_V6_RT_HASH_OFF && fm_le32(tv + 4) == FM_IPA5_MODEM_ROUTE_COUNT - 1u);
    tv = fm_qmi_tlv(msg, len, 0x1du, &tlen); CHECK(tv && tlen == 4u && fm_le32(tv) == mem_off + FM_IPA5_MEM_V4_FLT_HASH_OFF);
    tv = fm_qmi_tlv(msg, len, 0x1eu, &tlen); CHECK(tv && tlen == 4u && fm_le32(tv) == mem_off + FM_IPA5_MEM_V6_FLT_HASH_OFF);
    tv = fm_qmi_tlv(msg, len, 0x1fu, &tlen); CHECK(tv && tlen == 4u && fm_le32(tv) == mem_off + FM_IPA5_MEM_QUOTA_MODEM_OFF);
    tv = fm_qmi_tlv(msg, len, 0x20u, &tlen); CHECK(tv && tlen == 4u && fm_le32(tv) == FM_IPA5_MEM_QUOTA_MODEM_SIZE);
    tv = fm_qmi_tlv(msg, len, 0x21u, &tlen); CHECK(tv && tlen == 4u && fm_le32(tv) == mem_off + FM_IPA5_MEM_STATS_DROP_OFF);
    tv = fm_qmi_tlv(msg, len, 0x22u, &tlen); CHECK(tv && tlen == 4u && fm_le32(tv) == FM_IPA5_MEM_STATS_DROP_SIZE);
    tv = fm_qmi_tlv(msg, len, 0x23u, &tlen);
    CHECK(tv && tlen == 10u && fm_le32(tv) == FM_IPA5_MEM_HW_FLT_STATS_OFF && fm_le32(tv + 4) == FM_IPA5_HW_FLT_STATS_SIZE &&
        tv[8] == FM_IPA5_HW_FLT_STATS_START_IDX && tv[9] == FM_IPA5_HW_FLT_STATS_END_IDX);
    tv = fm_qmi_tlv(msg, len, 0x24u, &tlen);
    CHECK(tv && tlen == 8u && fm_le32(tv) == 0u && fm_le32(tv + 4) == smem_sz);
    tv = fm_qmi_tlv(msg, len, 0x25u, &tlen);
    CHECK(tv && tlen == 8u && fm_le32(tv) == smem_pa && fm_le32(tv + 4) == FM_IPA5_PER_STATS_SMEM_SIZE);
    /* 無 SMEM 時省略 TLV 0x24/0x25（167 B） */
    len = fm_ipa_qmi_init_driver_req(msg, sizeof msg, 0x0043u, mem_off, FM_IPA5_EP_FROM_MODEM, 0u, 0u);
    CHECK(len == 167u && le16(msg + 5) == 160u && fm_qmi_tlv(msg, len, 0x23u, &tlen) != NULL &&
        fm_qmi_tlv(msg, len, 0x24u, &tlen) == NULL && fm_qmi_tlv(msg, len, 0x25u, &tlen) == NULL);

    /* QMI IPA 0x23 INSTALL_FILTER_RULE 回覆：正常 count=2、長度不符 fallback、超限 fallback、buffer 不足 */
    uint8_t flt_req[7u + 3u + 1u + 2u * FM_IPA_QMI_FILTER_SPEC_SIZE];
    memset(flt_req, 0, sizeof flt_req);
    const uint16_t flt_tlv_len = (uint16_t)(1u + 2u * FM_IPA_QMI_FILTER_SPEC_SIZE);
    flt_req[0] = FM_QMI_TYPE_REQUEST;
    fm_put16(flt_req + 1, 0x0099u);
    fm_put16(flt_req + 3, FM_IPA_QMI_INSTALL_FILTER);
    fm_put16(flt_req + 5, (uint16_t)(3u + flt_tlv_len));
    flt_req[7] = 0x15u;
    fm_put16(flt_req + 8, flt_tlv_len);
    flt_req[10] = 2u;
    fm_put32(flt_req + 11u + 0u * FM_IPA_QMI_FILTER_SPEC_SIZE + FM_IPA_QMI_FILTER_ID_OFF, 0x11223344u);
    fm_put32(flt_req + 11u + 1u * FM_IPA_QMI_FILTER_SPEC_SIZE + FM_IPA_QMI_FILTER_ID_OFF, 0x55667788u);
    uint32_t out_count = 99u;
    len = fm_ipa_qmi_filter_resp(msg, sizeof msg, 0x0099u, flt_req, (uint32_t)sizeof flt_req, &out_count);
    CHECK(len == 26u && out_count == 2u && msg[0] == FM_QMI_TYPE_RESPONSE && le16(msg + 1) == 0x0099u &&
        le16(msg + 3) == FM_IPA_QMI_INSTALL_FILTER && le16(msg + 5) == 19u);
    tv = fm_qmi_tlv(msg, len, 0x02u, &tlen); CHECK(tv && tlen == 4u && fm_le32(tv) == 0u);
    tv = fm_qmi_tlv(msg, len, 0x11u, &tlen);
    CHECK(tv && tlen == 9u && tv[0] == 2u && fm_le32(tv + 1) == 0x11223344u && fm_le32(tv + 5) == 0x55667788u);
    /* buffer 不足：回 0 */
    CHECK(fm_ipa_qmi_filter_resp(msg, 25u, 0x0099u, flt_req, (uint32_t)sizeof flt_req, &out_count) == 0u && out_count == 0u);
    CHECK(fm_ipa_qmi_filter_resp(msg, 13u, 0x0099u, flt_req, (uint32_t)sizeof flt_req, &out_count) == 0u);
    /* TLV 0x15 長度不符 -> fallback 14 B */
    fm_put16(flt_req + 8, (uint16_t)(flt_tlv_len - 1u));
    fm_put16(flt_req + 5, (uint16_t)(3u + flt_tlv_len - 1u));
    len = fm_ipa_qmi_filter_resp(msg, sizeof msg, 0x009au, flt_req, (uint32_t)sizeof flt_req - 1u, &out_count);
    CHECK(len == 14u && out_count == 0u && le16(msg + 1) == 0x009au && fm_qmi_tlv(msg, len, 0x11u, &tlen) == NULL);
    /* count > FM_IPA_QMI_FILTER_MAX (65) -> fallback 14 B */
    uint8_t over_req[16] = {FM_QMI_TYPE_REQUEST, 0x9bu, 0u, FM_IPA_QMI_INSTALL_FILTER, 0u, 4u, 0u, 0x15u, 1u, 0u, 65u};
    len = fm_ipa_qmi_filter_resp(msg, sizeof msg, 0x009bu, over_req, 11u, &out_count);
    CHECK(len == 14u && out_count == 0u && le16(msg + 1) == 0x009bu);
    /* 請求為 NULL -> fallback 14 B */
    len = fm_ipa_qmi_filter_resp(msg, sizeof msg, 0x009cu, NULL, 0u, &out_count);
    CHECK(len == 14u && out_count == 0u);
}

static void m7_qmap_cell_wms(void) {
    /* IPA v5.5 ENDP_INIT_AGGR 位元驗算（對照 linux-ipa_reg-v5.5.c） */
    CHECK(fm_ipa5_endp_init_aggr(1u, 4u, 7u, 5u, 0u, 1) == 0x008050f1u);
    CHECK(fm_ipa5_endp_init_aggr(1u, 4u, 5u, 5u, 0u, 1) == FM_IPA5_AGGR_M7);
    CHECK(FM_IPA5_AGGR_M7 == 0x008050b1u);
    CHECK(FM_WDA_DL_MAX_SIZE_M7 == 0x1800u);

    /* WDS GET_CURRENT_SETTINGS IPv4 記憶體欄位解碼 */
    uint8_t wds_rsp[64];
    uint32_t wpos = 7u;
    wds_rsp[wpos++] = 0x02u; fm_put16(wds_rsp + wpos, 4u); wpos += 2u; fm_put32(wds_rsp + wpos, 0u); wpos += 4u;
    wds_rsp[wpos++] = 0x1eu; fm_put16(wds_rsp + wpos, 4u); wpos += 2u; fm_put32(wds_rsp + wpos, 0x0a010205u); wpos += 4u;
    wds_rsp[wpos++] = 0x20u; fm_put16(wds_rsp + wpos, 4u); wpos += 2u; fm_put32(wds_rsp + wpos, 0x0a010206u); wpos += 4u;
    wds_rsp[wpos++] = 0x21u; fm_put16(wds_rsp + wpos, 4u); wpos += 2u; fm_put32(wds_rsp + wpos, 0xfffffffcu); wpos += 4u;
    wds_rsp[wpos++] = 0x15u; fm_put16(wds_rsp + wpos, 4u); wpos += 2u; fm_put32(wds_rsp + wpos, 0x08080808u); wpos += 4u;
    wds_rsp[wpos++] = 0x16u; fm_put16(wds_rsp + wpos, 4u); wpos += 2u; fm_put32(wds_rsp + wpos, 0x01010101u); wpos += 4u;
    wds_rsp[wpos++] = 0x29u; fm_put16(wds_rsp + wpos, 4u); wpos += 2u; fm_put32(wds_rsp + wpos, 1420u); wpos += 4u;
    wds_rsp[0] = FM_QMI_TYPE_RESPONSE; fm_put16(wds_rsp + 1, 9u); fm_put16(wds_rsp + 3, FM_WDS_GET_CURRENT_SETTINGS);
    fm_put16(wds_rsp + 5, (uint16_t)(wpos - 7u));
    FmQmiAnswer ans;
    CHECK(fm_qmi_decode(FM_QMI_WDS, wds_rsp, wpos, &ans) == 0);
    CHECK(ans.has_wds_settings && ans.wds_has_ip && ans.wds_ip == 0x0a010205u && ans.wds_gw == 0x0a010206u &&
        ans.wds_mask == 0xfffffffcu && ans.wds_dns1 == 0x08080808u && ans.wds_dns2 == 0x01010101u && ans.wds_mtu == 1420u);

    /* QMI WMS 0x05：7 支請求編碼 + 回應／指示解碼 */
    uint8_t buf[300];
    uint32_t n = fm_qmi_wms_set_event_report(buf, sizeof buf, 11u, 1u);
    CHECK(n == 11u && buf[0] == FM_QMI_TYPE_REQUEST && le16(buf + 3) == FM_WMS_SET_EVENT_REPORT && buf[7] == 0x10u && buf[10] == 1u);
    const uint8_t tpdu[6] = {0x00u, 0x01u, 0x00u, 0x02u, 0x91u, 0x21u};
    CHECK(fm_qmi_wms_raw_send(buf, 18u, 12u, FM_WMS_FORMAT_3GPP, tpdu, 6u) == 0u);
    n = fm_qmi_wms_raw_send(buf, sizeof buf, 12u, FM_WMS_FORMAT_3GPP, tpdu, 6u);
    CHECK(n == 19u && le16(buf + 3) == FM_WMS_RAW_SEND && buf[7] == 0x01u && le16(buf + 8) == 9u &&
        buf[10] == FM_WMS_FORMAT_3GPP && le16(buf + 11) == 6u && memcmp(buf + 13, tpdu, 6u) == 0);
    n = fm_qmi_wms_raw_read(buf, sizeof buf, 13u, FM_WMS_STORAGE_UIM, 7u, FM_WMS_MODE_GW);
    CHECK(n == 19u && le16(buf + 3) == FM_WMS_RAW_READ && buf[10] == FM_WMS_STORAGE_UIM &&
        fm_le32(buf + 11) == 7u && buf[15] == 0x10u && buf[18] == FM_WMS_MODE_GW);
    n = fm_qmi_wms_delete(buf, sizeof buf, 14u, FM_WMS_STORAGE_NV, 3u, FM_WMS_MODE_GW);
    CHECK(n == 22u && le16(buf + 3) == FM_WMS_DELETE && buf[10] == FM_WMS_STORAGE_NV &&
        fm_le32(buf + 14) == 3u && buf[18] == 0x12u && buf[21] == FM_WMS_MODE_GW);
    n = fm_qmi_wms_get_routes(buf, sizeof buf, 15u);
    CHECK(n == 7u && le16(buf + 3) == FM_WMS_GET_ROUTES);
    n = fm_qmi_wms_list_messages(buf, sizeof buf, 16u, FM_WMS_STORAGE_UIM, FM_WMS_MODE_GW);
    CHECK(n == 15u && le16(buf + 3) == FM_WMS_LIST_MESSAGES && buf[10] == FM_WMS_STORAGE_UIM && buf[11] == 0x11u && buf[14] == 1u);
    n = fm_qmi_wms_get_store_max_size(buf, sizeof buf, 17u, FM_WMS_STORAGE_NV, FM_WMS_MODE_GW);
    CHECK(n == 15u && le16(buf + 3) == FM_WMS_GET_STORE_MAX_SIZE && buf[10] == FM_WMS_STORAGE_NV && buf[11] == 0x10u && buf[14] == 1u);

    /* WMS RAW_SEND 成功與失敗 cause 解碼 */
    uint8_t wms_send_ok[19] = {FM_QMI_TYPE_RESPONSE, 12, 0, 0x20, 0, 12, 0, 0x02, 4, 0, 0, 0, 0, 0, 0x01, 2, 0, 0x34, 0x12};
    CHECK(fm_qmi_decode(FM_QMI_WMS, wms_send_ok, sizeof wms_send_ok, &ans) == 0 && ans.result == 0u &&
        ans.wms_has_msg_id && ans.wms_msg_id == 0x1234u);
    uint8_t wms_send_err[19] = {FM_QMI_TYPE_RESPONSE, 12, 0, 0x20, 0, 12, 0, 0x02, 4, 0, 1, 0, 0x03, 0, 0x10, 2, 0, 0x29, 0x00};
    CHECK(fm_qmi_decode(FM_QMI_WMS, wms_send_err, sizeof wms_send_err, &ans) == 0 && ans.result == 1u &&
        ans.error == 0x03u && ans.wms_has_cause && ans.wms_cause == 0x29u);
    /* WMS RAW_READ / GET_ROUTES / LIST_MESSAGES / GET_STORE_MAX_SIZE 回應解碼 */
    uint8_t wms_read_ok[27] = {FM_QMI_TYPE_RESPONSE, 13, 0, 0x22, 0, 20, 0, 0x02, 4, 0, 0, 0, 0, 0,
        0x01, 10, 0, 1, FM_WMS_FORMAT_3GPP, 6, 0, 1, 2, 3, 4, 5, 6};
    CHECK(fm_qmi_decode(FM_QMI_WMS, wms_read_ok, sizeof wms_read_ok, &ans) == 0 && ans.wms_has_read &&
        ans.wms_read_tag == 1u && ans.wms_read_format == FM_WMS_FORMAT_3GPP && ans.wms_read_len == 6u);
    uint8_t wms_routes_ok[27] = {FM_QMI_TYPE_RESPONSE, 15, 0, 0x27, 0, 20, 0, 0x02, 4, 0, 0, 0, 0, 0,
        0x01, 10, 0, 2, 0, 0, 0, 1, 1, 0, 1, 1, 3};
    CHECK(fm_qmi_decode(FM_QMI_WMS, wms_routes_ok, sizeof wms_routes_ok, &ans) == 0 && ans.wms_has_routes && ans.wms_route_count == 2u);
    uint8_t wms_list_ok[26] = {FM_QMI_TYPE_RESPONSE, 16, 0, 0x31, 0, 19, 0, 0x02, 4, 0, 0, 0, 0, 0,
        0x01, 9, 0, 1, 0, 0, 0, 5, 0, 0, 0, 1};
    CHECK(fm_qmi_decode(FM_QMI_WMS, wms_list_ok, sizeof wms_list_ok, &ans) == 0 && ans.wms_has_list && ans.wms_list_count == 1u);
    uint8_t wms_store_ok[28] = {FM_QMI_TYPE_RESPONSE, 17, 0, 0x33, 0, 21, 0, 0x02, 4, 0, 0, 0, 0, 0,
        0x01, 4, 0, 50, 0, 0, 0, 0x10, 4, 0, 48, 0, 0, 0};
    CHECK(fm_qmi_decode(FM_QMI_WMS, wms_store_ok, sizeof wms_store_ok, &ans) == 0 && ans.wms_has_store_size &&
        ans.wms_store_max == 50u && ans.wms_has_store_free && ans.wms_store_free == 48u);
    /* WMS EVENT_REPORT_IND（TLV 0x11 與 TLV 0x10 雙格式） */
    uint8_t wms_ind[26] = {FM_QMI_TYPE_INDICATION, 1, 0, 0x01, 0, 19, 0,
        0x11, 5, 0, FM_WMS_STORAGE_NV, 9, 0, 0, 0,
        0x10, 8, 0, 1, FM_WMS_FORMAT_3GPP, 4, 0, 0xaa, 0xbb, 0xcc, 0xdd};
    CHECK(fm_qmi_decode(FM_QMI_WMS, wms_ind, sizeof wms_ind, &ans) == 0 && ans.is_indication &&
        ans.wms_ind_has_mt && ans.wms_ind_store == FM_WMS_STORAGE_NV && ans.wms_ind_index == 9u &&
        ans.wms_ind_has_route && ans.wms_ind_ack == 1u && ans.wms_ind_format == FM_WMS_FORMAT_3GPP && ans.wms_ind_data_len == 4u);

    /* IPA v5.5 ENDP_STATUS（ipam.ko ipareg_construct_endp_status_n_v5_0、pipe 22 Q6_WAN_CONS、pkt_suppress=1） */
    CHECK(FM_IPA5_EP_Q6_WAN_CONS == 22u);
    CHECK(fm_ipa5_endp_status(0, 0u, 0) == 0u);
    CHECK(fm_ipa5_endp_status(1, FM_IPA5_EP_Q6_WAN_CONS, 0) == 0x2du);
    CHECK(fm_ipa5_endp_status(1, FM_IPA5_EP_Q6_WAN_CONS, 1) == 0x22du);

    /* QMAP v1/v5 聚合、解聚合與指令 ACK */
    uint8_t ip_pkt[24];
    memset(ip_pkt, 0, sizeof ip_pkt);
    ip_pkt[0] = 0x45u; fm_put_be16(ip_pkt + 2, 24u);
    uint8_t qmap_agg[128];
    uint32_t q1 = fm_qmap_wrap(qmap_agg, sizeof qmap_agg, 1u, ip_pkt, sizeof ip_pkt, 0);
    CHECK(q1 == 28u && qmap_agg[0] == 0u && qmap_agg[1] == 1u && fm_be16(qmap_agg + 2) == 24u);
    /* 附加一筆 QMAP v5 含 8B csum ext 與 2B padding 的封包 */
    uint32_t q2_off = q1;
    qmap_agg[q2_off] = 0x42u; /* has_v5=1, pad=2 */
    qmap_agg[q2_off + 1] = 1u;
    fm_put_be16(qmap_agg + q2_off + 2, 26u);
    memset(qmap_agg + q2_off + 4, 0, 8u);
    memcpy(qmap_agg + q2_off + 12, ip_pkt, 24u);
    qmap_agg[q2_off + 36] = 0u; qmap_agg[q2_off + 37] = 0u;
    uint32_t total_agg = q2_off + 38u;
    FmQmapPacket qp;
    CHECK(fm_qmap_parse(qmap_agg, total_agg, 0u, &qp) == 0 && !qp.is_cmd && !qp.has_v5_ext &&
        qp.mux_id == 1u && qp.payload_off == 4u && qp.payload_len == 24u && qp.next_off == q1);
    CHECK(fm_qmap_parse(qmap_agg, total_agg, qp.next_off, &qp) == 0 && !qp.is_cmd && qp.has_v5_ext &&
        qp.pad_len == 2u && qp.payload_off == q2_off + 12u && qp.payload_len == 24u && qp.next_off == total_agg);
    CHECK(fm_qmap_parse(qmap_agg, total_agg - 1u, q2_off, &qp) == -1);
    /* QMAP flow control command + ACK */
    uint8_t cmd_body[8] = {FM_QMAP_CMD_FLOW_DISABLE, FM_QMAP_CMD_TYPE_REQUEST, 0, 0, 0x12, 0x34, 0x56, 0x78};
    uint8_t cmd_pkt[16];
    uint32_t clen = fm_qmap_wrap(cmd_pkt, sizeof cmd_pkt, 1u, cmd_body, 8u, 1);
    CHECK(clen == 12u && fm_qmap_parse(cmd_pkt, clen, 0u, &qp) == 0 && qp.is_cmd && qp.payload_len == 8u);
    FmQmapCmd qc;
    CHECK(fm_qmap_cmd_parse(cmd_pkt + qp.payload_off, qp.payload_len, &qc) == 0 &&
        qc.cmd_name == FM_QMAP_CMD_FLOW_DISABLE && qc.cmd_type == FM_QMAP_CMD_TYPE_REQUEST && qc.transaction_id == 0x12345678u);
    uint8_t ack_pkt[16];
    uint32_t alen = fm_qmap_cmd_ack(ack_pkt, sizeof ack_pkt, 1u, &qc);
    CHECK(alen == 12u && fm_qmap_parse(ack_pkt, alen, 0u, &qp) == 0 && qp.is_cmd);
    FmQmapCmd qack;
    CHECK(fm_qmap_cmd_parse(ack_pkt + qp.payload_off, qp.payload_len, &qack) == 0 &&
        qack.cmd_name == FM_QMAP_CMD_FLOW_DISABLE && qack.cmd_type == FM_QMAP_CMD_TYPE_ACK && qack.transaction_id == 0x12345678u);

    /* en1 合成路由器：Session、ARP、DHCPv4 DISCOVER/OFFER 與 REQUEST/ACK/NAK */
    const uint8_t cli_mac[6] = {0x02u, 0x66u, 0x75u, 0x78u, 0x69u, 0x01u};
    const uint8_t rtr_mac[6] = {0x02u, 0x66u, 0x75u, 0x78u, 0x69u, 0xfeu};
    FmCellSession sess;
    fm_cell_session_config(&sess, 1, 0x0a010201u, 0x08080808u, 0x01010101u, 1420u);
    CHECK(sess.up == 1u && sess.router == 0x0a010202u && sess.mask == 0xffffff00u);
    fm_cell_session_config(&sess, 1, 0x0a010205u, 0x08080808u, 0x01010101u, 1420u);
    CHECK(sess.up == 1u && sess.router == 0x0a010201u);

    uint8_t arp_req[42];
    memset(arp_req, 0, sizeof arp_req);
    memset(arp_req, 0xff, 6u);
    memcpy(arp_req + 6, cli_mac, 6u);
    fm_put_be16(arp_req + 12, 0x0806u);
    fm_put_be32(arp_req + 14, 0x00010800u);
    arp_req[18] = 6u; arp_req[19] = 4u; fm_put_be16(arp_req + 20, 1u);
    memcpy(arp_req + 22, cli_mac, 6u);
    fm_put_be32(arp_req + 28, sess.address);
    fm_put_be32(arp_req + 38, sess.router);
    uint8_t arp_out[64];
    CHECK(fm_cell_arp_reply(&sess, rtr_mac, arp_req, sizeof arp_req, arp_out, sizeof arp_out) == 42u);
    CHECK(memcmp(arp_out, cli_mac, 6u) == 0 && memcmp(arp_out + 6, rtr_mac, 6u) == 0 &&
        fm_be16(arp_out + 20) == 2u && memcmp(arp_out + 22, rtr_mac, 6u) == 0 && fm_be32(arp_out + 28) == sess.router);
    /* DAD 探測自己的 IP 不回覆 */
    fm_put_be32(arp_req + 38, sess.address);
    CHECK(fm_cell_arp_reply(&sess, rtr_mac, arp_req, sizeof arp_req, arp_out, sizeof arp_out) == 0u);

    uint8_t dhcp_req[14u + 20u + 8u + 260u];
    memset(dhcp_req, 0, sizeof dhcp_req);
    memset(dhcp_req, 0xff, 6u);
    memcpy(dhcp_req + 6, cli_mac, 6u);
    fm_put_be16(dhcp_req + 12, 0x0800u);
    dhcp_req[14] = 0x45u; dhcp_req[23] = 17u;
    fm_put_be16(dhcp_req + 34, 68u); fm_put_be16(dhcp_req + 36, 67u);
    uint8_t *bp = dhcp_req + 42u;
    bp[0] = 1u; bp[1] = 1u; bp[2] = 6u;
    fm_put_be32(bp + 4, 0xdeadbeefu);
    memcpy(bp + 28, cli_mac, 6u);
    fm_put_be32(bp + 236, 0x63825363u);
    bp[240] = 53u; bp[241] = 1u; bp[242] = FM_DHCP_DISCOVER; bp[243] = 255u;
    uint32_t udp_off = 0u;
    CHECK(fm_cell_is_dhcp_request(dhcp_req, sizeof dhcp_req, &udp_off) == 1 && udp_off == 34u);
    uint8_t dhcp_out[384];
    uint8_t dkind = 0u;
    uint32_t dlen = fm_cell_dhcp_reply(&sess, rtr_mac, dhcp_req, sizeof dhcp_req, udp_off, dhcp_out, sizeof dhcp_out, &dkind);
    CHECK(dlen == 14u + 20u + 8u + 300u && dkind == FM_DHCP_OFFER && fm_ipv4_checksum(dhcp_out + 14, 20u) == 0u);
    CHECK(fm_be32(dhcp_out + 42 + 16) == sess.address && fm_be32(dhcp_out + 42 + 20) == sess.router);
    bp[242] = FM_DHCP_REQUEST; bp[243] = 50u; bp[244] = 4u; fm_put_be32(bp + 245, sess.address); bp[249] = 255u;
    dlen = fm_cell_dhcp_reply(&sess, rtr_mac, dhcp_req, sizeof dhcp_req, udp_off, dhcp_out, sizeof dhcp_out, &dkind);
    CHECK(dlen >= 342u && dkind == FM_DHCP_ACK && fm_ipv4_checksum(dhcp_out + 14, 20u) == 0u);
    fm_put_be32(bp + 245, 0x0a090909u);
    dlen = fm_cell_dhcp_reply(&sess, rtr_mac, dhcp_req, sizeof dhcp_req, udp_off, dhcp_out, sizeof dhcp_out, &dkind);
    CHECK(dlen >= 342u && dkind == FM_DHCP_NAK && fm_be32(dhcp_out + 42 + 16) == 0u);

    /* 模糊測試：隨機緩衝區絕不越界 */
    uint32_t rng_state = 0x51a7c3d9u;
    for (unsigned iter = 0u; iter < 2000u; ++iter) {
        uint8_t rnd[400];
        rng_state = rng_state * 1664525u + 1013904223u;
        uint32_t rlen = rng_state % sizeof rnd;
        for (uint32_t k = 0u; k < rlen; ++k) {
            rng_state = rng_state * 1664525u + 1013904223u;
            rnd[k] = (uint8_t)(rng_state >> 24);
        }
        (void)fm_qmi_decode(FM_QMI_WMS, rnd, rlen, &ans);
        if (fm_qmap_parse(rnd, rlen, 0u, &qp) == 0) {
            CHECK(qp.payload_off + qp.payload_len <= rlen && qp.next_off <= rlen);
            if (qp.is_cmd) (void)fm_qmap_cmd_parse(rnd + qp.payload_off, qp.payload_len, &qc);
        }
        (void)fm_cell_arp_reply(&sess, rtr_mac, rnd, rlen, arp_out, sizeof arp_out);
        uint32_t uo = 0u;
        if (fm_cell_is_dhcp_request(rnd, rlen, &uo)) {
            (void)fm_cell_dhcp_reply(&sess, rtr_mac, rnd, rlen, uo, dhcp_out, sizeof dhcp_out, &dkind);
        }
    }
}

static void m8b3_sandbox_check(void) {
    FmSandboxCheckArgs args = { .out_result_ptr = 0x16d753700ull, .pid = 407, .operation = 0u, .filter_type = 0u };
    CHECK(sizeof(args) == 32u);
    CHECK(fm_sandbox_check_wants_sandboxed("MessagesBlastDoorService", &args) == 1);
    CHECK(fm_sandbox_check_wants_sandboxed("MessagesBlastDoo", &args) == 1);
    CHECK(fm_sandbox_check_wants_sandboxed("MessagesAirlockService", &args) == 1);
    CHECK(fm_sandbox_check_wants_sandboxed("launchd", &args) == 0);
    CHECK(fm_sandbox_check_wants_sandboxed("CommCenter", &args) == 0);
    CHECK(fm_sandbox_check_wants_sandboxed("SpringBoard", &args) == 0);
    CHECK(fm_sandbox_check_wants_sandboxed("", &args) == 0);
    CHECK(fm_sandbox_check_wants_sandboxed(NULL, &args) == 0);
    CHECK(fm_sandbox_check_wants_sandboxed("MessagesBlastDoorService", NULL) == 0);
    FmSandboxCheckArgs bad_op = args;
    bad_op.operation = 0x180001000ull;
    CHECK(fm_sandbox_check_wants_sandboxed("MessagesBlastDoorService", &bad_op) == 0);
    FmSandboxCheckArgs bad_filt = args;
    bad_filt.filter_type = 1u;
    CHECK(fm_sandbox_check_wants_sandboxed("MessagesBlastDoorService", &bad_filt) == 0);
    FmSandboxCheckArgs bad_out = args;
    bad_out.out_result_ptr = 0u;
    CHECK(fm_sandbox_check_wants_sandboxed("MessagesBlastDoorService", &bad_out) == 0);
}

static uint32_t build_dpm_rsp(uint8_t *out, uint16_t txn, uint16_t err) {
    out[0] = 0x02u;
    cbd_wr16(out + 1, txn);
    cbd_wr16(out + 3, CBD_DPM_OPEN_PORT);
    cbd_wr16(out + 5, 7u);
    out[7] = 0x02u; cbd_wr16(out + 8, 4u);
    cbd_wr16(out + 10, err ? 1u : 0u);
    cbd_wr16(out + 12, err);
    return 14u;
}

static uint32_t build_wda_rsp(uint8_t *out, uint16_t txn, uint16_t err, uint32_t llp, uint32_t ul, uint32_t dl, uint32_t dgrams, uint32_t dsize) {
    uint32_t w = 0u;
    uint8_t *t = out + 7u;
    uint8_t res[4];
    cbd_wr16(res, err ? 1u : 0u);
    cbd_wr16(res + 2, err);
    w += cbd_tlv(t + w, 128u - w, 0x02u, res, 4u);
    if (!err) {
        w += cbd_tlv_u32(t + w, 128u - w, 0x11u, llp);
        w += cbd_tlv_u32(t + w, 128u - w, 0x12u, ul);
        w += cbd_tlv_u32(t + w, 128u - w, 0x13u, dl);
        w += cbd_tlv_u32(t + w, 128u - w, 0x15u, dgrams);
        w += cbd_tlv_u32(t + w, 128u - w, 0x16u, dsize);
    }
    out[0] = 0x02u;
    cbd_wr16(out + 1, txn);
    cbd_wr16(out + 3, CBD_WDA_SET_DATA_FORMAT);
    cbd_wr16(out + 5, w);
    return 7u + w;
}

static int bb_pdp_is_ready(const struct cbd_setup *s) {
    return s->step == CBD_DONE && !s->dpm_error && !s->wda_error &&
           s->llp == CBD_LLP_RAW_IP && s->ul_dap == CBD_DAP_QMAP && s->dl_dap == CBD_DAP_QMAP;
}

static void b020_data_port_race(void) {
    uint8_t sdu[128], rsp[140];
    uint32_t svc = 0u, n = 0u;

    /* 1. 正常情境：DPM 與 WDA 皆已存在，一次跑完 DPM -> WDA -> CBD_DONE */
    struct cbd_setup s;
    memset(&s, 0, sizeof s);
    s.has_dpm = 1; s.has_wda = 1;
    n = cbd_next(&s, sdu, sizeof sdu, &svc);
    CHECK(n > 0u && svc == CBD_SVC_DPM && s.step == CBD_DPM && s.txn == 1u);
    CHECK(cbd_rd16(sdu + 3) == CBD_DPM_OPEN_PORT);
    uint32_t rlen = build_dpm_rsp(rsp, (uint16_t)s.txn, 0u);
    CHECK(cbd_answer(&s, CBD_SVC_DPM, rsp, rlen) == 1 && s.step == CBD_WAIT_WDA && s.dpm_error == 0u);
    n = cbd_next(&s, sdu, sizeof sdu, &svc);
    CHECK(n > 0u && svc == CBD_SVC_WDA && s.step == CBD_WDA && s.txn == 2u);
    CHECK(cbd_rd16(sdu + 3) == CBD_WDA_SET_DATA_FORMAT);
    rlen = build_wda_rsp(rsp, (uint16_t)s.txn, 0u, CBD_LLP_RAW_IP, CBD_DAP_QMAP, CBD_DAP_QMAP, 16u, 63u);
    CHECK(cbd_answer(&s, CBD_SVC_WDA, rsp, rlen) == 1 && s.step == CBD_DONE && s.wda_error == 0u);
    CHECK(bb_pdp_is_ready(&s) == 1);
    CHECK(cbd_next(&s, sdu, sizeof sdu, &svc) == 0u && s.step == CBD_DONE);

    /* 2. 完整復現 B-020 實機競態：DPM(0x2f@47) 與 WDS(0x01@49) 先到，WDA(0x1a@4b) 延後到達 */
    struct cbq_bridge qmux;
    cbq_init(&qmux);
    CHECK(cbq_server_new(&qmux, CBD_SVC_DPM, 1u, 0u, 0x47u) == 1);
    CHECK(cbq_server_new(&qmux, CBD_SVC_WDS, 1u, 0u, 0x49u) == 1);
    memset(&s, 0, sizeof s);
    s.has_dpm = cbq_server_find(&qmux, CBD_SVC_DPM) != NULL;
    s.has_wda = cbq_server_find(&qmux, CBD_SVC_WDA) != NULL;
    CHECK(s.has_dpm == 1u && s.has_wda == 0u);
    n = cbd_next(&s, sdu, sizeof sdu, &svc);
    CHECK(n > 0u && svc == CBD_SVC_DPM && s.step == CBD_DPM && s.txn == 1u);
    /* 在途時誤呼 cbd_next 絕不破壞狀態或 txn */
    CHECK(cbd_next(&s, sdu, sizeof sdu, &svc) == 0u && s.step == CBD_DPM && s.txn == 1u);
    /* DPM 回答成功，但 WDA 仍未到達：必須停在 CBD_WAIT_WDA，絕不跳 CBD_DONE(0xffff) */
    rlen = build_dpm_rsp(rsp, (uint16_t)s.txn, 0u);
    CHECK(cbd_answer(&s, CBD_SVC_DPM, rsp, rlen) == 1 && s.step == CBD_WAIT_WDA && s.dpm_error == 0u);
    for (int retry = 0; retry < 3; ++retry) {
        s.has_wda = cbq_server_find(&qmux, CBD_SVC_WDA) != NULL;
        CHECK(cbd_next(&s, sdu, sizeof sdu, &svc) == 0u && s.step == CBD_WAIT_WDA);
    }
    /* 此時 WDS 雖已 active 1，在 WDA 完成前 pdp 尚未 ready */
    struct cbp_client pdp_clients[2];
    memset(pdp_clients, 0, sizeof pdp_clients);
    pdp_clients[0].mux = 1u; pdp_clients[0].family = 4u; pdp_clients[0].active = 1u;
    pdp_clients[1].mux = 2u; pdp_clients[1].family = 4u; pdp_clients[1].active = 1u;
    CHECK(bb_pdp_is_ready(&s) == 0);
    /* 幾毫秒後 WDA (0x1a@4b) 到達 -> 立刻推進送出 WDA Set Data Format */
    CHECK(cbq_server_new(&qmux, CBD_SVC_WDA, 1u, 0u, 0x4bu) == 1);
    s.has_wda = cbq_server_find(&qmux, CBD_SVC_WDA) != NULL;
    CHECK(s.has_wda == 1u);
    n = cbd_next(&s, sdu, sizeof sdu, &svc);
    CHECK(n > 0u && svc == CBD_SVC_WDA && s.step == CBD_WDA && s.txn == 2u);
    CHECK(cbd_next(&s, sdu, sizeof sdu, &svc) == 0u && s.step == CBD_WDA && s.txn == 2u);
    /* 錯 txn / 錯 service 回應必須被拒 */
    rlen = build_wda_rsp(rsp, 99u, 0u, CBD_LLP_RAW_IP, CBD_DAP_QMAP, CBD_DAP_QMAP, 16u, 63u);
    CHECK(cbd_answer(&s, CBD_SVC_WDA, rsp, rlen) == 0 && s.step == CBD_WDA);
    rlen = build_wda_rsp(rsp, (uint16_t)s.txn, 0u, CBD_LLP_RAW_IP, CBD_DAP_QMAP, CBD_DAP_QMAP, 16u, 63u);
    CHECK(cbd_answer(&s, CBD_SVC_DPM, rsp, rlen) == 0 && s.step == CBD_WDA);
    /* 正確 WDA 回覆 -> CBD_DONE 且 pdp_ip0 / pdp_ip1 解鎖 */
    CHECK(cbd_answer(&s, CBD_SVC_WDA, rsp, rlen) == 1 && s.step == CBD_DONE);
    CHECK(bb_pdp_is_ready(&s) == 1);
    CHECK(cbp_active_families(pdp_clients, 2u, 1u) == 1u);
    CHECK(cbp_active_families(pdp_clients, 2u, 2u) == 1u);

    /* 3. 極端競態：WDS 先到時 DPM 與 WDA 皆未到，且 WDA 比 DPM 先到 -> 絕不跳過 DPM */
    memset(&s, 0, sizeof s);
    CHECK(cbd_next(&s, sdu, sizeof sdu, &svc) == 0u && s.step == CBD_IDLE);
    s.has_wda = 1u;
    CHECK(cbd_next(&s, sdu, sizeof sdu, &svc) == 0u && s.step == CBD_IDLE);
    /* room 太小不破壞狀態 */
    s.has_dpm = 1u;
    CHECK(cbd_next(&s, sdu, 8u, &svc) == 0u && s.step == CBD_IDLE && s.txn == 0u);
    /* DPM 到後先 DPM 再 WDA */
    n = cbd_next(&s, sdu, sizeof sdu, &svc);
    CHECK(n > 0u && svc == CBD_SVC_DPM && s.step == CBD_DPM && s.txn == 1u);
    rlen = build_dpm_rsp(rsp, (uint16_t)s.txn, 0u);
    CHECK(cbd_answer(&s, CBD_SVC_DPM, rsp, rlen) == 1 && s.step == CBD_WAIT_WDA);
    CHECK(cbd_next(&s, sdu, 8u, &svc) == 0u && s.step == CBD_WAIT_WDA && s.txn == 1u);
    n = cbd_next(&s, sdu, sizeof sdu, &svc);
    CHECK(n > 0u && svc == CBD_SVC_WDA && s.step == CBD_WDA && s.txn == 2u);
    rlen = build_wda_rsp(rsp, (uint16_t)s.txn, 0u, CBD_LLP_RAW_IP, CBD_DAP_QMAP, CBD_DAP_QMAP, 16u, 63u);
    CHECK(cbd_answer(&s, CBD_SVC_WDA, rsp, rlen) == 1 && s.step == CBD_DONE && bb_pdp_is_ready(&s) == 1);

    /* 4. 請求在途收到 DEL_SERVER -> cbd_server_lost 退回等待狀態，服務重來可恢復 */
    memset(&s, 0, sizeof s);
    s.has_dpm = 1u; s.has_wda = 1u;
    n = cbd_next(&s, sdu, sizeof sdu, &svc);
    CHECK(n > 0u && svc == CBD_SVC_DPM && s.step == CBD_DPM && s.txn == 1u);
    cbd_server_lost(&s, CBD_SVC_DPM);
    CHECK(s.step == CBD_IDLE && s.has_dpm == 0u);
    CHECK(cbd_next(&s, sdu, sizeof sdu, &svc) == 0u && s.step == CBD_IDLE);
    s.has_dpm = 1u;
    n = cbd_next(&s, sdu, sizeof sdu, &svc);
    CHECK(n > 0u && svc == CBD_SVC_DPM && s.step == CBD_DPM && s.txn == 2u);
    rlen = build_dpm_rsp(rsp, (uint16_t)s.txn, 0u);
    CHECK(cbd_answer(&s, CBD_SVC_DPM, rsp, rlen) == 1 && s.step == CBD_WAIT_WDA);
    n = cbd_next(&s, sdu, sizeof sdu, &svc);
    CHECK(n > 0u && svc == CBD_SVC_WDA && s.step == CBD_WDA && s.txn == 3u);
    cbd_server_lost(&s, CBD_SVC_WDA);
    CHECK(s.step == CBD_WAIT_WDA && s.has_wda == 0u);
    s.has_wda = 1u;
    n = cbd_next(&s, sdu, sizeof sdu, &svc);
    CHECK(n > 0u && svc == CBD_SVC_WDA && s.step == CBD_WDA && s.txn == 4u);
    rlen = build_wda_rsp(rsp, (uint16_t)s.txn, 0u, CBD_LLP_RAW_IP, CBD_DAP_QMAP, CBD_DAP_QMAP, 16u, 63u);
    CHECK(cbd_answer(&s, CBD_SVC_WDA, rsp, rlen) == 1 && s.step == CBD_DONE && bb_pdp_is_ready(&s) == 1);
}

int main(void) {
    assign_page();
    qmi_tlvs();
    rmtfs_flow();
    rmtfs_unshared();
    qrtr();
    fuzz();
    qmi_client();
    dms_offline_and_tftp();
    m5_qmi_and_ipa();
    m6_ipa_gsi_qmi();
    m7_qmap_cell_wms();
    m8b3_sandbox_check();
    b020_data_port_race();
    printf("PASS fuxi modem proto: %u checks; MP_ASSIGN page, QMI TLVs, rmtfs open/alloc/rw/close/refusals, QRTR lookup and control packets, QMI DMS/NAS/WMS/UIM/DPM/WDA/WDS/IND codecs, TFTP over QRTR, IPA v5.5/GSI v5.0/QMI IPA 0x31/QMAP/en1/SMMU/BlastDoor-sandbox/B020-WDA-race\n", checks);
    return 0;
}
'''


FALLBACK_SOURCE = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "coachz_bb_data.h"
static unsigned checks;
#define CHECK(test) do { checks++; if (!(test)) { fprintf(stderr, "fallback line %d: %s\n", __LINE__, #test); exit(1); } } while (0)

static uint32_t dpm_ok(uint8_t *out, uint16_t txn) {
    out[0] = 0x02u; cbd_wr16(out + 1, txn); cbd_wr16(out + 3, CBD_DPM_OPEN_PORT); cbd_wr16(out + 5, 7u);
    out[7] = 0x02u; cbd_wr16(out + 8, 4u); cbd_wr16(out + 10, 0u); cbd_wr16(out + 12, 0u);
    return 14u;
}
static uint32_t wda_rsp(uint8_t *out, uint16_t txn, uint16_t err, uint32_t dap) {
    uint32_t w = 0u; uint8_t *t = out + 7u; uint8_t res[4];
    cbd_wr16(res, err ? 1u : 0u); cbd_wr16(res + 2, err);
    w += cbd_tlv(t + w, 96u - w, 0x02u, res, 4u);
    if (!err) {
        w += cbd_tlv_u32(t + w, 96u - w, 0x11u, CBD_LLP_RAW_IP);
        w += cbd_tlv_u32(t + w, 96u - w, 0x12u, dap);
        w += cbd_tlv_u32(t + w, 96u - w, 0x13u, dap);
    }
    out[0] = 0x02u; cbd_wr16(out + 1, txn); cbd_wr16(out + 3, CBD_WDA_SET_DATA_FORMAT); cbd_wr16(out + 5, w);
    return 7u + w;
}
int main(void) {
    uint8_t sdu[128], rsp[128]; uint32_t svc = 0u, n = 0u;
    struct cbd_setup s;
    memset(&s, 0, sizeof s);
    s.has_dpm = 1u; s.has_wda = 1u;
    n = cbd_next(&s, sdu, sizeof sdu, &svc);
    CHECK(n > 0u && svc == CBD_SVC_DPM && s.step == CBD_DPM);
    CHECK(cbd_answer(&s, CBD_SVC_DPM, rsp, dpm_ok(rsp, (uint16_t)s.txn)) == 1 && s.step == CBD_WAIT_WDA);
    /* 首輪問 QMAPv4 (8) -> modem 回錯誤 0x03 -> 進入 CBD_WDA_NEXT */
    n = cbd_next(&s, sdu, sizeof sdu, &svc);
    CHECK(n > 0u && svc == CBD_SVC_WDA && s.step == CBD_WDA && s.dap_index == 0u);
    CHECK(cbd_answer(&s, CBD_SVC_WDA, rsp, wda_rsp(rsp, (uint16_t)s.txn, 0x03u, 0u)) == 1 && s.step == CBD_WDA_NEXT);
    /* 若 WDA 暫失則留在 CBD_WDA_NEXT；回來後送第二個 DAP (QMAP=5) -> 完成 */
    s.has_wda = 0u;
    CHECK(cbd_next(&s, sdu, sizeof sdu, &svc) == 0u && s.step == CBD_WDA_NEXT);
    s.has_wda = 1u;
    n = cbd_next(&s, sdu, sizeof sdu, &svc);
    CHECK(n > 0u && svc == CBD_SVC_WDA && s.step == CBD_WDA && s.dap_index == 1u);
    CHECK(cbd_answer(&s, CBD_SVC_WDA, rsp, wda_rsp(rsp, (uint16_t)s.txn, 0u, CBD_DAP_QMAP)) == 1);
    CHECK(s.step == CBD_DONE && s.wda_error == 0u && s.dl_dap == CBD_DAP_QMAP);
    printf("PASS coachz_bb_data dual-DAP fallback: %u checks\n", checks);
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
    with tempfile.TemporaryDirectory(prefix='fuxi-modem-') as temporary:
        directory = Path(temporary)
        for stem, code in (('modem', SOURCE), ('cbd_fallback', FALLBACK_SOURCE)):
            source = directory / f'{stem}.c'
            source.write_text(code, encoding='utf-8', newline='\n')
            binary = directory / (f'{stem}.exe' if os.name == 'nt' else stem)
            if microsoft:
                flags = ['/nologo', '/std:c11', '/W4', '/WX', '/Zi', '/Od', '/MD', '/utf-8']
                if arguments.sanitize:
                    flags.append('/fsanitize=address')
                command = compiler + flags + [f'/I{ROOT / "xnu/fuxi-modem"}', str(source), f'/Fe:{binary}']
            else:
                flags = ['-std=c11', '-g', '-Wall', '-Wextra', '-Werror', '-O1']
                if arguments.sanitize:
                    flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
                command = compiler + flags + ['-I', str(ROOT / 'xnu/fuxi-modem'), str(source), '-o', str(binary)]
            subprocess.run(command, cwd=directory, env=environment, check=True, timeout=120)
            subprocess.run([str(binary)], cwd=directory, env=environment, check=True, timeout=120)


if __name__ == '__main__':
    main()
