#!/usr/bin/env python3
"""Host tests (with optional ASan/UBSan) for fuxi-sensors portable core, QcomQrtr client/RESUME_TX fix, SMMU, and real pack.bin."""
import argparse
import os
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ADSP_DIR = ROOT / "xnu/fuxi-adsp"
SNS_DIR = ROOT / "xnu/fuxi-sensors"
PACK_BIN = next((p for p in (
    ROOT.parent / "sensor-dump/pack.bin",
    (ROOT.parent / "harness").resolve().parent / "sensor-dump/pack.bin",
) if p.is_file()), ROOT.parent / "sensor-dump/pack.bin")
sys.dont_write_bytecode = True

HARNESS = r'''
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "QcomQrtr.h"
#include "fuxi_sensors_smmu.h"
#include "SensorsPb.h"
#include "SensorsSsc.h"
#include "SensorsFixed.h"
#include "SensorsOrient.h"
#include "SensorsCompass.h"
#include "SensorsPack.h"
#include "SensorsRpc.h"
#include "SensorsCore.h"
#include "SensorsHid.h"

static unsigned gChecks = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL at line %d: %s\n", __LINE__, #cond); \
        exit(1); \
    } \
    gChecks++; \
} while (0)

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* ---- 1. QcomQrtr client ports + RESUME_TX fix ---- */
struct qrtr_mock {
    uint8_t tx[8][560];
    uint32_t tx_len[8];
    unsigned tx_count;
    unsigned rx_calls;
    uint32_t last_port, last_from_node, last_from_port, last_len;
    unsigned server_notifies;
};

static int mock_qrtr_send(void *ctx, const uint8_t *pkt, uint32_t len)
{
    struct qrtr_mock *m = (struct qrtr_mock *)ctx;
    if (m->tx_count >= 8 || len > sizeof(m->tx[0])) return -1;
    memcpy(m->tx[m->tx_count], pkt, len);
    m->tx_len[m->tx_count] = len;
    m->tx_count++;
    return 0;
}

static void mock_qrtr_rx(void *ctx, QcomQrtr *q, uint32_t port, uint32_t node, uint32_t from_port,
                         const uint8_t *data, uint32_t len)
{
    (void)q; (void)data;
    struct qrtr_mock *m = (struct qrtr_mock *)ctx;
    m->rx_calls++;
    m->last_port = port;
    m->last_from_node = node;
    m->last_from_port = from_port;
    m->last_len = len;
}

static void mock_qrtr_srv_notify(void *ctx)
{
    struct qrtr_mock *m = (struct qrtr_mock *)ctx;
    m->server_notifies++;
}

static void test_qrtr(void)
{
    struct qrtr_mock m;
    memset(&m, 0, sizeof(m));
    QcomQrtr q;
    qcom_qrtr_init(&q, 1u, mock_qrtr_send, &m);
    qcom_qrtr_set_server_notify(&q, mock_qrtr_srv_notify, &m);
    CHECK(qcom_qrtr_add_service(&q, QCOM_SERVLOC_SERVICE, QCOM_SERVLOC_INSTANCE, 1u, mock_qrtr_rx, &m) == 0);
    CHECK(qcom_qrtr_add_client(&q, 0x100u, mock_qrtr_rx, &m) == 0);
    CHECK(qcom_qrtr_add_client(&q, 0x100u, mock_qrtr_rx, &m) == -1); /* duplicate rejected */
    CHECK(qcom_qrtr_add_client(&q, 0u, mock_qrtr_rx, &m) == -1);
    CHECK(qcom_qrtr_add_client(&q, QCOM_QRTR_PORT_CTRL, mock_qrtr_rx, &m) == -1);

    /* Remote HELLO from node 7: should send HELLO + NEW_SERVER for port 1 ONLY (not client port 0x100). */
    uint8_t hello[QCOM_QRTR_HEADER + 20];
    memset(hello, 0, sizeof(hello));
    wr32(hello + 0, 1u);
    wr32(hello + 4, QCOM_QRTR_HELLO);
    wr32(hello + 8, 7u);
    wr32(hello + 12, QCOM_QRTR_PORT_CTRL);
    wr32(hello + 20, 20u);
    wr32(hello + 24, QCOM_QRTR_NODE_BROADCAST);
    wr32(hello + 28, QCOM_QRTR_PORT_CTRL);
    wr32(hello + 32, QCOM_QRTR_HELLO);
    CHECK(qcom_qrtr_receive(&q, hello, sizeof(hello)) == 0);
    CHECK(m.tx_count == 2);
    CHECK(rd32(m.tx[0] + 4) == QCOM_QRTR_HELLO);
    CHECK(rd32(m.tx[1] + 4) == QCOM_QRTR_NEW_SERVER);
    CHECK(rd32(m.tx[1] + 32 + 4) == QCOM_SERVLOC_SERVICE);
    CHECK(rd32(m.tx[1] + 32 + 16) == 1u);

    /* Remote announces SSC service 0x190 on node 7 port 42. */
    uint8_t ns[QCOM_QRTR_HEADER + 20];
    memset(ns, 0, sizeof(ns));
    wr32(ns + 0, 1u);
    wr32(ns + 4, QCOM_QRTR_NEW_SERVER);
    wr32(ns + 8, 7u);
    wr32(ns + 12, QCOM_QRTR_PORT_CTRL);
    wr32(ns + 20, 20u);
    wr32(ns + 24, 1u);
    wr32(ns + 28, QCOM_QRTR_PORT_CTRL);
    wr32(ns + 32, QCOM_QRTR_NEW_SERVER);
    wr32(ns + 36, SSC_QMI_SERVICE);
    wr32(ns + 40, 1u);
    wr32(ns + 44, 7u);
    wr32(ns + 48, 42u);
    CHECK(qcom_qrtr_receive(&q, ns, sizeof(ns)) == 0);
    CHECK(m.server_notifies == 1);
    uint32_t rnode = 0, rport = 0, rinst = 0;
    CHECK(qcom_qrtr_find_remote_any(&q, SSC_QMI_SERVICE, &rnode, &rport, &rinst) == 0);
    CHECK(rnode == 7u && rport == 42u && rinst == 1u);

    /* Incoming DATA to client port 0x100 with confirm_rx=1 -> verify RESUME_TX targets node 7 with {local_node=1, dst_port=0x100}! */
    uint8_t dpkt[QCOM_QRTR_HEADER + 8];
    memset(dpkt, 0, sizeof(dpkt));
    wr32(dpkt + 0, 1u);
    wr32(dpkt + 4, QCOM_QRTR_DATA);
    wr32(dpkt + 8, 7u);
    wr32(dpkt + 12, 42u);
    wr32(dpkt + 16, 1u); /* confirm_rx = 1 */
    wr32(dpkt + 20, 4u);
    wr32(dpkt + 24, 1u);
    wr32(dpkt + 28, 0x100u);
    m.tx_count = 0;
    CHECK(qcom_qrtr_receive(&q, dpkt, sizeof(dpkt)) == 0);
    CHECK(m.rx_calls == 1 && m.last_port == 0x100u && m.last_from_node == 7u && m.last_from_port == 42u);
    CHECK(m.tx_count == 1 && q.resumes_sent == 1);
    CHECK(rd32(m.tx[0] + 4) == QCOM_QRTR_RESUME_TX);
    CHECK(rd32(m.tx[0] + 24) == 7u); /* dst_node = sender (DSP node 7) */
    CHECK(rd32(m.tx[0] + 28) == QCOM_QRTR_PORT_CTRL);
    CHECK(rd32(m.tx[0] + 32 + 0) == QCOM_QRTR_RESUME_TX);
    CHECK(rd32(m.tx[0] + 32 + 4) == 1u);      /* OUR local_node */
    CHECK(rd32(m.tx[0] + 32 + 8) == 0x100u);  /* OUR dst_port */
}

/* ---- 2. fuxi_sensors_smmu.h ---- */
struct smmu_mock {
    uint32_t scr0, idr0, idr1, idr2, gfsr, gfsynr1;
    uint32_t smr[128];
    uint32_t s2cr[128];
    uint32_t cbar[64];
    uint32_t cb_sctlr[64];
    unsigned writes, tlb_inv, tlb_sync;
    int fail_write_at;
};

static void smmu_mock_init(struct smmu_mock *s)
{
    memset(s, 0, sizeof(*s));
    s->idr0 = 0x48000000u | 64u; /* 64 SMR slots */
    s->idr1 = (0u << 31) | (3u << 28) | 32u; /* stride 0x1000, bank_base 0x10000, 32 banks */
    s->idr2 = 0x1000u;
    for (unsigned i = 0; i < 64u; ++i) s->s2cr[i] = FUXI_SMMU_FAULT_S2CR;
    /* USB SID 0x40 -> bank 4 (bypass ready: SCTLR=0, CBAR bits 17:16 = 1). */
    s->smr[0] = 0x80000040u;
    s->s2cr[0] = FUXI_AUDIO_BYPASS_BANK;
    s->cbar[FUXI_AUDIO_BYPASS_BANK] = (1u << 16);
    s->cb_sctlr[FUXI_AUDIO_BYPASS_BANK] = 0u;
    s->fail_write_at = -1;
}

static bool smmu_mock_read(void *ctx, uint32_t off, uint32_t *val)
{
    struct smmu_mock *s = (struct smmu_mock *)ctx;
    if (off == 0x0u) { *val = s->scr0; return true; }
    if (off == 0x20u) { *val = s->idr0; return true; }
    if (off == 0x24u) { *val = s->idr1; return true; }
    if (off == 0x28u) { *val = s->idr2; return true; }
    if (off == 0x48u) { *val = s->gfsr; return true; }
    if (off == 0x54u) { *val = s->gfsynr1; return true; }
    if (off == 0x74u) { *val = 0u; return true; }
    if (off >= 0x800u && off < 0xa00u) { *val = s->smr[(off - 0x800u) / 4u]; return true; }
    if (off >= 0xc00u && off < 0xe00u) { *val = s->s2cr[(off - 0xc00u) / 4u]; return true; }
    if (off >= 0x1000u && off < 0x1100u) { *val = s->cbar[(off - 0x1000u) / 4u]; return true; }
    if (off >= 0x10000u && off < 0x30000u) {
        unsigned bank = (off - 0x10000u) / 0x1000u;
        unsigned reg = (off - 0x10000u) % 0x1000u;
        if (reg == 0u) { *val = s->cb_sctlr[bank]; return true; }
        *val = 0u;
        return true;
    }
    return false;
}

static bool smmu_mock_write(void *ctx, uint32_t off, uint32_t val)
{
    struct smmu_mock *s = (struct smmu_mock *)ctx;
    int idx = (int)s->writes++;
    if (s->fail_write_at >= 0 && idx == s->fail_write_at) return false;
    if (off == FUXI_SMMU_GR0_TLBIALLNSNH) { s->tlb_inv++; return true; }
    if (off == FUXI_SMMU_GR0_TLBGSYNC) { s->tlb_sync++; return true; }
    if (off >= 0x800u && off < 0xa00u) { s->smr[(off - 0x800u) / 4u] = val; return true; }
    if (off >= 0xc00u && off < 0xe00u) { s->s2cr[(off - 0xc00u) / 4u] = val; return true; }
    return false;
}

static void test_smmu(void)
{
    struct smmu_mock s;
    struct fuxi_sensors_smmu_info info;
    smmu_mock_init(&s);
    CHECK(fuxi_sensors_smmu_attach(smmu_mock_read, smmu_mock_write, &s, 2u, &info) == FUXI_SENSORS_SMMU_BAD_BANK);
    CHECK(fuxi_sensors_smmu_attach(smmu_mock_read, smmu_mock_write, &s, 8u, &info) == FUXI_SENSORS_SMMU_BAD_BANK);
    CHECK(s.writes == 0);

    /* Conflict test: SID 0x1084 already owned by another slot -> refuse without writes! */
    s.smr[5] = 0x80001084u;
    s.s2cr[5] = 7u;
    CHECK(fuxi_sensors_smmu_attach(smmu_mock_read, smmu_mock_write, &s, 4u, &info) == FUXI_SENSORS_SMMU_CONFLICT);
    CHECK(s.writes == 0);

    /* Rollback test: fail on 3rd write (second slot S2CR) -> first slot rolled back cleanly! */
    smmu_mock_init(&s);
    s.fail_write_at = 2;
    CHECK(fuxi_sensors_smmu_attach(smmu_mock_read, smmu_mock_write, &s, 4u, &info) == FUXI_SENSORS_SMMU_WRITE_ERROR);
    CHECK((s.smr[1] & (1u << 31)) == 0u && s.s2cr[1] == FUXI_SMMU_FAULT_S2CR);

    /* Happy path for bank 4 -> slots 1 and 2 programmed + TLB synced, idempotent second call! */
    smmu_mock_init(&s);
    CHECK(fuxi_sensors_smmu_attach(smmu_mock_read, smmu_mock_write, &s, 4u, &info) == FUXI_SENSORS_SMMU_OK);
    CHECK(info.already_ready == 0 && info.tlb_synced == 1);
    CHECK(info.slots[0] == 1 && info.slots[1] == 2);
    CHECK(s.smr[1] == 0x80801004u && s.s2cr[1] == FUXI_AUDIO_BYPASS_BANK);
    CHECK(s.smr[2] == 0x80001064u && s.s2cr[2] == FUXI_AUDIO_BYPASS_BANK);
    CHECK(s.tlb_inv == 1 && s.tlb_sync == 1);
    unsigned w_after = s.writes;
    CHECK(fuxi_sensors_smmu_attach(smmu_mock_read, smmu_mock_write, &s, 4u, &info) == FUXI_SENSORS_SMMU_OK);
    CHECK(info.already_ready == 1 && s.writes == w_after);
}

/* ---- 3. Fixed-point + Orient + Core end-to-end ---- */
static uint32_t float_bits(float v)
{
    uint32_t u = 0;
    memcpy(&u, &v, 4);
    return u;
}

static void test_fixed_and_orient(void)
{
    /* Upright portrait on Android: +9.80665 m/s^2 on +Y -> iOS -1.0g (-65536) on Y -> SNO_PORTRAIT. */
    int32_t ay = sfx_accel_ios(float_bits(9.80665f));
    CHECK(ay <= -65530 && ay >= -65542);
    SnoState st;
    sno_init(&st);
    CHECK(sno_update(&st, 0, ay, 0, 1000u) == -1);
    CHECK(sno_update(&st, 0, ay, 0, 1000u + SNO_HOLD_MS) == SNO_PORTRAIT);
    /* Tilt to landscape left: +9.80665 m/s^2 on +X -> iOS -1.0g on X -> SNO_LANDSCAPE_LEFT. */
    int32_t ax = sfx_accel_ios(float_bits(9.80665f));
    CHECK(sno_update(&st, ax, 0, 0, 2000u) == -1);
    CHECK(sno_update(&st, ax, 0, 0, 2000u + SNO_HOLD_MS) == SNO_LANDSCAPE_LEFT);
    /* Face up: +9.80665 m/s^2 on +Z -> iOS -1.0g on Z -> SNO_FACE_UP. */
    CHECK(sno_update(&st, 0, 0, ax, 3000u) == -1);
    CHECK(sno_update(&st, 0, 0, ax, 3000u + SNO_HOLD_MS) == SNO_FACE_UP);
}

/* ---- 4. Real Xiaomi 13 pack.bin + SensorsPack + SensorsRpc ---- */
struct pack_mem {
    uint8_t *buf;
    size_t len;
    uint64_t now_ms;
    uint8_t last_tx[64];
    uint32_t last_tx_len;
    unsigned tx_count;
    SsrCore *core;
    int auto_reply;
    void *allocs[8];
    unsigned alloc_count;
    uint32_t next_handle;
    uint32_t next_sc;
    uint8_t next_in[4096];
    uint32_t next_in_len;
    int inject_next2;
    uint32_t last_rctx;
    int32_t last_result;
    uint8_t last_out[4096];
    uint32_t last_out_len;
};

static uint64_t rpc_now(void *ctx) { return ((struct pack_mem *)ctx)->now_ms; }
static int rpc_send(void *ctx, const uint8_t *msg, uint32_t len)
{
    struct pack_mem *p = (struct pack_mem *)ctx;
    if (len > sizeof(p->last_tx)) return -1;
    memcpy(p->last_tx, msg, len);
    p->last_tx_len = len;
    p->tx_count++;
    if (p->inject_next2 && p->core) {
        uint8_t *arena = p->core->cfg.arena;
        uint32_t sc = 0, handle = 0;
        memcpy(&handle, msg + 16, 4);
        memcpy(&sc, msg + 20, 4);
        if (handle == 3u && ((sc >> 24) & 0x1fu) == 4u) {
            /* Capture previous result + out blob from arena */
            uint32_t rctx = 0, res = 0, out_len = 0;
            memcpy(&rctx, arena + 0x400, 4);
            memcpy(&res, arena + 0x404, 4);
            memcpy(&out_len, arena + 0x408, 4);
            p->last_rctx = rctx;
            p->last_result = (int32_t)res;
            p->last_out_len = out_len < sizeof(p->last_out) ? out_len : (uint32_t)sizeof(p->last_out);
            if (p->last_out_len) memcpy(p->last_out, arena + 0x10000, p->last_out_len);
            /* Inject next incoming DSP call into kPrimOut (0x800) and kInBlob (0x1000) */
            static uint32_t seq = 100u;
            wr32(arena + 0x800, ++seq);
            wr32(arena + 0x804, p->next_handle);
            wr32(arena + 0x808, p->next_sc);
            wr32(arena + 0x80c, p->next_in_len);
            if (p->next_in_len) memcpy(arena + 0x1000, p->next_in, p->next_in_len);
            p->inject_next2 = 0;
        }
    }
    if (p->auto_reply && p->core && len >= 16u) {
        uint8_t rsp[12];
        memcpy(rsp, msg + 8, 8);
        wr32(rsp + 8, 0u);
        ssr_core_receive(p->core, rsp, sizeof(rsp));
    }
    return 0;
}
static void rpc_wait(void *ctx, uint32_t ms) { ((struct pack_mem *)ctx)->now_ms += ms; }
static void rpc_trace(void *ctx, const char *line) { (void)ctx; (void)line; }
static void rpc_publish(void *ctx) { (void)ctx; }
static int rpc_prepare(void *ctx) { (void)ctx; return 0; }
static int rpc_pack_size(void *ctx, uint64_t *size)
{
    *size = (uint64_t)((struct pack_mem *)ctx)->len;
    return SsrPackOk;
}
static int rpc_pack_read(void *ctx, uint64_t off, void *to, uint32_t len)
{
    struct pack_mem *p = (struct pack_mem *)ctx;
    if (off > p->len || len > p->len - off) return -1;
    memcpy(to, p->buf + off, len);
    return 0;
}
static void *rpc_alloc(void *ctx, uint32_t bytes)
{
    struct pack_mem *p = (struct pack_mem *)ctx;
    CHECK(p->alloc_count < 8u);
    void *ptr = calloc(1, bytes);
    p->allocs[p->alloc_count++] = ptr;
    return ptr;
}

struct rpc_buf {
    const void *p;
    uint32_t n;
};

static int32_t run_dsp_call(struct pack_mem *pm, uint32_t handle, uint32_t method,
    const struct rpc_buf *ins, uint32_t in_count, const uint32_t *outs, uint32_t out_count,
    uint8_t *out_blob, uint32_t *out_blob_len)
{
    uint32_t at = 0;
    memset(pm->next_in, 0, sizeof(pm->next_in));
    for (uint32_t i = 0; i < in_count; ++i) {
        wr32(pm->next_in + at, ins[i].n);
        at += 4;
        if (ins[i].n) {
            at = (at + 7u) & ~7u;
            CHECK(at + ins[i].n <= sizeof(pm->next_in));
            memcpy(pm->next_in + at, ins[i].p, ins[i].n);
            at += ins[i].n;
        }
    }
    for (uint32_t i = 0; i < out_count; ++i) {
        wr32(pm->next_in + at, outs[i]);
        at += 4;
    }
    pm->next_handle = handle;
    pm->next_sc = ((method < 31u ? method : 31u) << 24) | (in_count << 16) | (out_count << 8);
    pm->next_in_len = at;
    pm->inject_next2 = 1;
    CHECK(ssr_core_step(pm->core) == 1); /* invokes next2, receives call, executes call() */
    int32_t rc = pm->core->result;
    uint32_t olen = pm->core->out_length;
    if (out_blob_len) *out_blob_len = olen;
    if (out_blob && olen) memcpy(out_blob, pm->core->cfg.arena + 0x10000, olen);
    return rc;
}

static uint32_t out_buf_at(const uint8_t *blob, uint32_t blob_len, uint32_t index, uint32_t *len_out)
{
    uint32_t at = 0;
    for (uint32_t i = 0; i <= index; ++i) {
        CHECK(at + 4u <= blob_len);
        uint32_t n = 0;
        memcpy(&n, blob + at, 4);
        at += 4u;
        if (n) at = (at + 7u) & ~7u;
        CHECK(at + n <= blob_len);
        if (i == index) {
            if (len_out) *len_out = n;
            return at;
        }
        at += n;
    }
    return 0;
}

static void test_ssp_path_dotdot(void)
{
    char out[SSP_PATH];
    const char *p1 = "/mnt/vendor/persist/sensors/registry/registry/../sns_reg_version";
    CHECK(ssp_path(p1, (uint32_t)strlen(p1) + 1u, out, sizeof(out)) > 0);
    CHECK(strcmp(out, "/mnt/vendor/persist/sensors/registry/sns_reg_version") == 0);
    const char *p2 = "/mnt/vendor/persist/sensors/registry/registry/../temp.json";
    CHECK(ssp_path(p2, (uint32_t)strlen(p2) + 1u, out, sizeof(out)) > 0);
    CHECK(strcmp(out, "/mnt/vendor/persist/sensors/registry/temp.json") == 0);
    const char *p3 = "/a/b/../../..";
    CHECK(ssp_path(p3, (uint32_t)strlen(p3) + 1u, out, sizeof(out)) == 1);
    CHECK(strcmp(out, "/") == 0);
    const char *p4 = "/persist/sensors/registry/registry/../sns_reg_version";
    CHECK(ssp_path(p4, (uint32_t)strlen(p4) + 1u, out, sizeof(out)) > 0);
    CHECK(strcmp(out, "/mnt/vendor/persist/sensors/registry/sns_reg_version") == 0);
    const char *p5 = "relative/path";
    CHECK(ssp_path(p5, (uint32_t)strlen(p5) + 1u, out, sizeof(out)) == -1);
}

static void test_real_pack_and_rpc(const char *pack_path)
{
    FILE *f = fopen(pack_path, "rb");
    CHECK(f != NULL);
    CHECK(fseek(f, 0, SEEK_END) == 0);
    long flen = ftell(f);
    CHECK(flen > (long)SSP_HEADER);
    CHECK(fseek(f, 0, SEEK_SET) == 0);
    uint8_t *buf = (uint8_t *)malloc((size_t)flen);
    CHECK(buf != NULL);
    CHECK(fread(buf, 1, (size_t)flen, f) == (size_t)flen);
    fclose(f);

    SspHeader hdr;
    CHECK(ssp_header(buf, (uint32_t)flen, &hdr) == 0);
    CHECK(hdr.total == (uint64_t)flen);
    CHECK(hdr.count >= 400u);
    uint32_t tbytes = ssp_table_bytes(&hdr);
    SspPack pack;
    CHECK(ssp_open(&pack, &hdr, buf + SSP_HEADER) == 0);
    int lib_idx = ssp_find(&pack, SSR_LIBRARY_PATH_FILE);
    CHECK(lib_idx >= 0);
    SspEntry le;
    ssp_entry(&pack, (uint32_t)lib_idx, &le);
    CHECK(le.size > 10u && le.offset + le.size <= (uint64_t)flen);
    /* Verify Xiaomi 13 orientation files exist in pack.bin */
    int orient_found = 0;
    for (uint32_t i = 0; i < pack.count; ++i) {
        SspEntry e;
        ssp_entry(&pack, i, &e);
        if (strstr(e.name, "bmi26x_0_platform.orient")) orient_found = 1;
    }
    CHECK(orient_found == 1);
    (void)tbytes;

    /* Boot SsrCore through Channel -> Pack -> SMMU -> Attach -> Open -> Register -> Init2 -> Listen! */
    uint8_t *arena = (uint8_t *)calloc(1, SSR_ARENA_BYTES);
    uint8_t *bounce = (uint8_t *)calloc(1, SSR_BOUNCE);
    SsrCore *core = (SsrCore *)calloc(1, sizeof(SsrCore));
    CHECK(arena && bounce && core);
    struct pack_mem pm;
    memset(&pm, 0, sizeof(pm));
    pm.buf = buf;
    pm.len = (size_t)flen;
    pm.now_ms = 100u;
    pm.core = core;
    pm.auto_reply = 1;
    SsrOps ops = {&pm, rpc_now, rpc_send, rpc_wait, rpc_trace, rpc_publish, rpc_prepare, rpc_pack_size, rpc_pack_read, rpc_alloc};
    SsrConfig cfg = {arena, 0xe8000000ull, SSR_ARENA_BYTES, bounce, 4242u, 4u, 0u};
    ssr_core_init(core, &ops, &cfg);
    ssr_core_channel(core, 1);
    CHECK(ssr_core_step(core) == 1); /* Channel -> Pack */
    CHECK(ssr_core_step(core) == 1); /* Pack loaded */
    CHECK(core->status.stage == SsrStPack);
    CHECK(strstr(core->library_path, "/odm/lib/rfsa/adsp") != NULL);

    /* Attach invoke: sends 40B invoke, auto-replied -> verifies ctx, bank=4 in high bits of addr, and stage SsrStAttach! */
    CHECK(ssr_core_step(core) == 1);
    CHECK(pm.tx_count == 1 && pm.last_tx_len == SSR_MESSAGE);
    uint64_t ctx_sent = 0, addr_sent = 0;
    memcpy(&ctx_sent, pm.last_tx + 8, 8);
    memcpy(&addr_sent, pm.last_tx + 24, 8);
    CHECK((ctx_sent & 0xffu) == SSR_PD_SENSORS);
    CHECK((addr_sent >> 32) == 4u);
    CHECK((addr_sent & 0xffffffffull) == 0xe8000000ull);
    CHECK(core->status.stage == SsrStAttach);

    /* Advance Open -> Register -> Close -> Init2 -> PhaseServe */
    CHECK(ssr_core_step(core) == 1); /* Open */
    CHECK(ssr_core_step(core) == 1); /* Register */
    CHECK(ssr_core_step(core) == 1); /* Close */
    CHECK(ssr_core_step(core) == 1); /* Init2 -> PhaseServe */
    CHECK(core->status.stage == SsrStListen);

    uint8_t out_blob[4096];
    uint32_t out_len = 0, pos = 0, blen = 0;

    /* 1. apps_std_stat (method 31 extended) on JSON config -> verifies mtime == 1230768000 and non-zero ino/size */
    {
        const char *cfg_path = "/odm/etc/sensors/config/sm8550_bmi26x_0.json";
        uint8_t prim[8];
        wr32(prim, 31u);
        wr32(prim + 4, (uint32_t)strlen(cfg_path) + 1u);
        struct rpc_buf ins[2] = {{prim, 8u}, {cfg_path, (uint32_t)strlen(cfg_path) + 1u}};
        uint32_t outs[1] = {96u};
        CHECK(run_dsp_call(&pm, 1u, 31u, ins, 2u, outs, 1u, out_blob, &out_len) == SSR_OK);
        pos = out_buf_at(out_blob, out_len, 0u, &blen);
        CHECK(blen == 96u);
        uint64_t ino = 0, sz = 0, atime = 0, mtime = 0, ctime = 0;
        memcpy(&ino, out_blob + pos + 16, 8);
        memcpy(&sz, out_blob + pos + 40, 8);
        memcpy(&atime, out_blob + pos + 48, 8);
        memcpy(&mtime, out_blob + pos + 64, 8);
        memcpy(&ctime, out_blob + pos + 80, 8);
        CHECK(ino > 0u && sz > 1000u);
        CHECK(atime == 1230768000ull && mtime == 1230768000ull && ctime == 1230768000ull);
    }

    /* 2. fopen("r+") on ../sns_reg_version -> copy-on-write succeeds, reads "version=14", writes back */
    {
        const char *vp = "/mnt/vendor/persist/sensors/registry/registry/../sns_reg_version";
        const char *md = "r+";
        uint8_t prim[8];
        wr32(prim, (uint32_t)strlen(vp) + 1u);
        wr32(prim + 4, (uint32_t)strlen(md) + 1u);
        struct rpc_buf ins[3] = {{prim, 8u}, {vp, (uint32_t)strlen(vp) + 1u}, {md, (uint32_t)strlen(md) + 1u}};
        uint32_t outs[1] = {4u};
        CHECK(run_dsp_call(&pm, 1u, 0u, ins, 3u, outs, 1u, out_blob, &out_len) == SSR_OK);
        pos = out_buf_at(out_blob, out_len, 0u, &blen);
        uint32_t fd = 0;
        memcpy(&fd, out_blob + pos, 4);
        CHECK(fd >= 16u);
        /* fread 32 bytes */
        wr32(prim, fd);
        wr32(prim + 4, 32u);
        struct rpc_buf rins[1] = {{prim, 8u}};
        uint32_t routs[2] = {8u, 32u};
        CHECK(run_dsp_call(&pm, 1u, 4u, rins, 1u, routs, 2u, out_blob, &out_len) == SSR_OK);
        pos = out_buf_at(out_blob, out_len, 0u, &blen);
        uint32_t got = 0;
        memcpy(&got, out_blob + pos, 4);
        CHECK(got == 11u);
        pos = out_buf_at(out_blob, out_len, 1u, &blen);
        CHECK(memcmp(out_blob + pos, "version=14", 10) == 0);
        /* fclose */
        wr32(prim, fd);
        struct rpc_buf cins[1] = {{prim, 4u}};
        CHECK(run_dsp_call(&pm, 1u, 3u, cins, 1u, NULL, 0u, out_blob, &out_len) == SSR_OK);
    }

    /* 3. Create ../temp.json ("w"), write multi-block payload (>1200B), ftrunc to 1050B, close, frename over pack file, verify readback & removal */
    {
        const char *tp = "/mnt/vendor/persist/sensors/registry/registry/../temp.json";
        const char *dp = "/mnt/vendor/persist/sensors/registry/registry/ak0991x_0";
        const char *md = "w";
        uint8_t prim[12];
        wr32(prim, (uint32_t)strlen(tp) + 1u);
        wr32(prim + 4, (uint32_t)strlen(md) + 1u);
        struct rpc_buf ins[3] = {{prim, 8u}, {tp, (uint32_t)strlen(tp) + 1u}, {md, (uint32_t)strlen(md) + 1u}};
        uint32_t outs[1] = {4u};
        CHECK(run_dsp_call(&pm, 1u, 0u, ins, 3u, outs, 1u, out_blob, &out_len) == SSR_OK);
        pos = out_buf_at(out_blob, out_len, 0u, &blen);
        uint32_t fd = 0;
        memcpy(&fd, out_blob + pos, 4);

        uint8_t payload[1200];
        for (uint32_t i = 0; i < sizeof(payload); ++i) payload[i] = (uint8_t)('A' + (i % 26u));
        wr32(prim, fd);
        wr32(prim + 4, (uint32_t)sizeof(payload));
        struct rpc_buf wins[2] = {{prim, 8u}, {payload, (uint32_t)sizeof(payload)}};
        uint32_t wouts[1] = {8u};
        CHECK(run_dsp_call(&pm, 1u, 5u, wins, 2u, wouts, 1u, out_blob, &out_len) == SSR_OK);
        pos = out_buf_at(out_blob, out_len, 0u, &blen);
        uint32_t written = 0, eof_flag = 99u;
        memcpy(&written, out_blob + pos, 4);
        memcpy(&eof_flag, out_blob + pos + 4, 4);
        CHECK(written == sizeof(payload) && eof_flag == 0u);

        /* ftrunc (method 32 extended: +0=32, +4=fd, +8=1050) */
        wr32(prim, 32u);
        wr32(prim + 4, fd);
        wr32(prim + 8, 1050u);
        struct rpc_buf tins[1] = {{prim, 12u}};
        CHECK(run_dsp_call(&pm, 1u, 32u, tins, 1u, NULL, 0u, out_blob, &out_len) == SSR_OK);

        /* flen (method 10) == 1050 */
        wr32(prim, fd);
        struct rpc_buf lins[1] = {{prim, 4u}};
        uint32_t louts[1] = {8u};
        CHECK(run_dsp_call(&pm, 1u, 10u, lins, 1u, louts, 1u, out_blob, &out_len) == SSR_OK);
        pos = out_buf_at(out_blob, out_len, 0u, &blen);
        uint64_t fl = 0;
        memcpy(&fl, out_blob + pos, 8);
        CHECK(fl == 1050ull);

        /* fclose */
        CHECK(run_dsp_call(&pm, 1u, 3u, lins, 1u, NULL, 0u, out_blob, &out_len) == SSR_OK);

        /* frename (method 33 extended) temp.json -> ak0991x_0 */
        wr32(prim, 33u);
        wr32(prim + 4, (uint32_t)strlen(tp) + 1u);
        wr32(prim + 8, (uint32_t)strlen(dp) + 1u);
        struct rpc_buf rnins[3] = {{prim, 12u}, {tp, (uint32_t)strlen(tp) + 1u}, {dp, (uint32_t)strlen(dp) + 1u}};
        CHECK(run_dsp_call(&pm, 1u, 33u, rnins, 3u, NULL, 0u, out_blob, &out_len) == SSR_OK);

        /* temp.json no longer exists */
        wr32(prim, (uint32_t)strlen(tp) + 1u);
        struct rpc_buf eins[2] = {{prim, 4u}, {tp, (uint32_t)strlen(tp) + 1u}};
        uint32_t eouts[1] = {1u};
        CHECK(run_dsp_call(&pm, 1u, 22u, eins, 2u, eouts, 1u, out_blob, &out_len) == SSR_OK);
        pos = out_buf_at(out_blob, out_len, 0u, &blen);
        CHECK(out_blob[pos] == 0u);

        /* ak0991x_0 now has 1050 bytes matching payload[0..1049] */
        const char *rmd = "r";
        wr32(prim, (uint32_t)strlen(dp) + 1u);
        wr32(prim + 4, (uint32_t)strlen(rmd) + 1u);
        struct rpc_buf oins[3] = {{prim, 8u}, {dp, (uint32_t)strlen(dp) + 1u}, {rmd, (uint32_t)strlen(rmd) + 1u}};
        CHECK(run_dsp_call(&pm, 1u, 0u, oins, 3u, outs, 1u, out_blob, &out_len) == SSR_OK);
        pos = out_buf_at(out_blob, out_len, 0u, &blen);
        memcpy(&fd, out_blob + pos, 4);
        wr32(prim, fd);
        wr32(prim + 4, 1200u);
        struct rpc_buf rdins[1] = {{prim, 8u}};
        uint32_t rdouts[2] = {8u, 1200u};
        CHECK(run_dsp_call(&pm, 1u, 4u, rdins, 1u, rdouts, 2u, out_blob, &out_len) == SSR_OK);
        pos = out_buf_at(out_blob, out_len, 0u, &blen);
        uint32_t got = 0;
        memcpy(&got, out_blob + pos, 4);
        CHECK(got == 1050u);
        pos = out_buf_at(out_blob, out_len, 1u, &blen);
        CHECK(memcmp(out_blob + pos, payload, 1050u) == 0);

        /* fremove while still open -> whiteouts pack entry without breaking open fd, then close frees unlinked slot */
        wr32(prim, (uint32_t)strlen(dp) + 1u);
        struct rpc_buf rmins[2] = {{prim, 4u}, {dp, (uint32_t)strlen(dp) + 1u}};
        CHECK(run_dsp_call(&pm, 1u, 24u, rmins, 2u, NULL, 0u, out_blob, &out_len) == SSR_OK);
        /* exists(dp) == 0 (whiteout hides pack.bin entry!) */
        CHECK(run_dsp_call(&pm, 1u, 22u, rmins, 2u, eouts, 1u, out_blob, &out_len) == SSR_OK);
        pos = out_buf_at(out_blob, out_len, 0u, &blen);
        CHECK(out_blob[pos] == 0u);
        /* close unlinked fd cleanly */
        wr32(prim, fd);
        CHECK(run_dsp_call(&pm, 1u, 3u, lins, 1u, NULL, 0u, out_blob, &out_len) == SSR_OK);
        CHECK(core->status.refused == 0u);
        CHECK(core->status.writes == 1u);
    }

    /* 4. mkdir + create file inside + readdir + rmdir */
    {
        const char *nd = "/mnt/vendor/persist/sensors/tmp_dir";
        const char *nf = "/mnt/vendor/persist/sensors/tmp_dir/probe.txt";
        uint8_t prim[8];
        wr32(prim, (uint32_t)strlen(nd) + 1u);
        wr32(prim + 4, 0755u);
        struct rpc_buf mins[2] = {{prim, 8u}, {nd, (uint32_t)strlen(nd) + 1u}};
        CHECK(run_dsp_call(&pm, 1u, 29u, mins, 2u, NULL, 0u, out_blob, &out_len) == SSR_OK);
        const char *md = "w";
        wr32(prim, (uint32_t)strlen(nf) + 1u);
        wr32(prim + 4, (uint32_t)strlen(md) + 1u);
        struct rpc_buf oins[3] = {{prim, 8u}, {nf, (uint32_t)strlen(nf) + 1u}, {md, (uint32_t)strlen(md) + 1u}};
        uint32_t outs[1] = {4u};
        CHECK(run_dsp_call(&pm, 1u, 0u, oins, 3u, outs, 1u, out_blob, &out_len) == SSR_OK);
        pos = out_buf_at(out_blob, out_len, 0u, &blen);
        uint32_t fd = 0;
        memcpy(&fd, out_blob + pos, 4);
        wr32(prim, fd);
        struct rpc_buf cins[1] = {{prim, 4u}};
        CHECK(run_dsp_call(&pm, 1u, 3u, cins, 1u, NULL, 0u, out_blob, &out_len) == SSR_OK);

        /* opendir + readdir sees "probe.txt" once then EOF */
        wr32(prim, (uint32_t)strlen(nd) + 1u);
        struct rpc_buf odins[2] = {{prim, 4u}, {nd, (uint32_t)strlen(nd) + 1u}};
        uint32_t odouts[1] = {8u};
        CHECK(run_dsp_call(&pm, 1u, 26u, odins, 2u, odouts, 1u, out_blob, &out_len) == SSR_OK);
        pos = out_buf_at(out_blob, out_len, 0u, &blen);
        uint64_t dh = 0;
        memcpy(&dh, out_blob + pos, 8);
        CHECK(dh > 0u);
        uint8_t dprim[8];
        memcpy(dprim, &dh, 8);
        struct rpc_buf rdins[1] = {{dprim, 8u}};
        uint32_t rdouts[1] = {264u};
        CHECK(run_dsp_call(&pm, 1u, 28u, rdins, 1u, rdouts, 1u, out_blob, &out_len) == SSR_OK);
        pos = out_buf_at(out_blob, out_len, 0u, &blen);
        CHECK(strcmp((const char *)(out_blob + pos + 4), "probe.txt") == 0);
        CHECK(run_dsp_call(&pm, 1u, 28u, rdins, 1u, rdouts, 1u, out_blob, &out_len) == SSR_OK);
        pos = out_buf_at(out_blob, out_len, 0u, &blen);
        uint32_t done = 0;
        memcpy(&done, out_blob + pos + 260, 4);
        CHECK(done == 1u);
        CHECK(run_dsp_call(&pm, 1u, 27u, rdins, 1u, NULL, 0u, out_blob, &out_len) == SSR_OK);
    }

    for (unsigned i = 0; i < pm.alloc_count; ++i) free(pm.allocs[i]);
    free(core);
    free(bounce);
    free(arena);
    free(buf);
}

/* ---- 5. Compass (SensorsCompass + SscCalEvent + SnsCore rotv/mag_cal + HID DevMotion) ---- */
static uint32_t f2bits(float value)
{
    uint32_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static float bits2f(uint32_t bits)
{
    float value = 0.0f;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static float rd_f32(const uint8_t *buf, unsigned offset)
{
    return bits2f(rd32(buf + offset));
}

static uint16_t rd16(const uint8_t *buf)
{
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static void check_angle_close(double got_deg, double want_deg, double tol_deg)
{
    double diff = fabs(got_deg - want_deg);
    if (diff > 180.0) diff = 360.0 - diff;
    if (diff > tol_deg) {
        fprintf(stderr, "ANGLE FAIL: got=%.3f want=%.3f diff=%.3f tol=%.3f\n", got_deg, want_deg, diff, tol_deg);
    }
    CHECK(diff <= tol_deg);
}

/* Exact locationd 0x1007b41e4 gravity + 0x1007b4274 NWU row0 (North) + 0x1006bdc60 horizMag verification */
static void verify_locationd_report(const uint8_t rep[SNS_COMPASS_REPORT_BYTES],
    double want_hdg_deg, const double want_grav[3], double tol_deg)
{
    CHECK(rep[0] == 2u);
    CHECK(rep[1] == 5u);
    const uint16_t flags = rd16(rep + 0x02);
    CHECK((flags & 0x251cu) == 0x251cu);
    CHECK((flags & (uint16_t)~0x271cu) == 0u);
    CHECK(rd32(rep + 0x04) == 0u && rd32(rep + 0x08) == 0u);
    CHECK(fabs(rd_f32(rep, 0x0c) - 1.0f) < 1e-5);

    const double qx = rd_f32(rep, 0x1c);
    const double qy = rd_f32(rep, 0x20);
    const double qz = rd_f32(rep, 0x24);
    const double qw = rd_f32(rep, 0x28);
    const double qnorm = sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
    CHECK(fabs(qnorm - 1.0) < 1e-4);

    const double r00 = 1.0 - 2.0 * (qy * qy + qz * qz);
    const double r01 = 2.0 * (qx * qy - qw * qz);
    const double r02 = 2.0 * (qx * qz + qw * qy);
    const double r11 = 1.0 - 2.0 * (qx * qx + qz * qz);
    const double r12 = 2.0 * (qy * qz - qw * qx);

    const double gx = 2.0 * (qw * qy - qx * qz);
    const double gy = -2.0 * (qy * qz + qw * qx);
    const double gz = 2.0 * (qx * qx + qy * qy) - 1.0;
    CHECK(fabs(gx - want_grav[0]) < 0.01);
    CHECK(fabs(gy - want_grav[1]) < 0.01);
    CHECK(fabs(gz - want_grav[2]) < 0.01);

    /* Verify NWU heading directly from locationd quaternion rows (Row0=North, Row1=West). */
    double quat_hdg;
    if (fabs(r01) + fabs(r11) >= fabs(r02) + fabs(r12)) {
        quat_hdg = atan2(-r11, r01) * 180.0 / 3.14159265358979323846;
    } else {
        quat_hdg = atan2(r12, -r02) * 180.0 / 3.14159265358979323846;
    }
    if (quat_hdg < 0.0) quat_hdg += 360.0;
    check_angle_close(quat_hdg, want_hdg_deg, tol_deg);

    const double hdg = rd_f32(rep, 0x44);
    const double acc = rd_f32(rep, 0x48);
    CHECK(hdg >= 0.0 && hdg < 360.0);
    CHECK(acc >= 5.0 && acc <= 45.0);
    check_angle_close(hdg, want_hdg_deg, tol_deg);

    double cm[3];
    for (unsigned i = 0; i < 3u; ++i) {
        cm[i] = (double)rd_f32(rep, 0x10 + i * 4u) - (double)rd_f32(rep, 0x4c + i * 4u);
    }
    const double dot_g = cm[0] * gx + cm[1] * gy + cm[2] * gz;
    double hx = cm[0] - dot_g * gx;
    double hy = cm[1] - dot_g * gy;
    double hz = cm[2] - dot_g * gz;
    const double hnorm = sqrt(hx * hx + hy * hy + hz * hz);
    CHECK(hnorm >= 10.0 && hnorm <= 90.0);
    /* Horizontal calibrated mag must align with NWU Row0 (locationd 0x1007b4274 North vector). */
    const double align = (hx * r00 + hy * r01 + hz * r02) / hnorm;
    CHECK(align > 0.99);
}

struct compass_core_mock {
    uint64_t now_ms;
    SnsCore *core;
    unsigned tx_count;
    uint32_t last_rotv_rate_bits;
    uint32_t last_rate_bits[PaganiSensorKinds];
    unsigned config_count[PaganiSensorKinds];
    unsigned disable_count[PaganiSensorKinds];
    unsigned suid_req_count;
    char suid_req_type[16][32];
    uint64_t suid_req_default_only[16];
    unsigned compass_count;
    int32_t last_heading_mdeg;
    uint8_t last_report[SNS_COMPASS_REPORT_BYTES];
    unsigned mag_sample_count;
    int32_t last_mag_value[3];
    unsigned q_head, q_tail;
    struct { unsigned conn; uint32_t node, port; uint16_t txn; } q[32];
};

static uint64_t cc_now(void *ctx) { return ((struct compass_core_mock *)ctx)->now_ms; }
static void cc_trace(void *ctx, const char *line) { (void)ctx; (void)line; }
static void cc_sample(void *ctx, const SnsSample *s)
{
    struct compass_core_mock *m = (struct compass_core_mock *)ctx;
    if (s && s->kind == PaganiSensorMag) {
        m->mag_sample_count++;
        for (unsigned i = 0; i < 3u; ++i) m->last_mag_value[i] = s->value[i];
    }
}
static void cc_compass(void *ctx, uint64_t ts, const uint8_t rep[SNS_COMPASS_REPORT_BYTES], int32_t hdg_mdeg)
{
    (void)ts;
    struct compass_core_mock *m = (struct compass_core_mock *)ctx;
    m->compass_count++;
    m->last_heading_mdeg = hdg_mdeg;
    memcpy(m->last_report, rep, SNS_COMPASS_REPORT_BYTES);
}

static uint32_t build_qmi_resp_ok(uint8_t *out, uint16_t txn, uint64_t client_id)
{
    out[0] = SSC_QMI_RESPONSE;
    out[1] = (uint8_t)(txn & 0xffu);
    out[2] = (uint8_t)(txn >> 8);
    out[3] = SSC_QMI_REQ;
    out[4] = 0;
    out[5] = 18;
    out[6] = 0;
    /* TLV 0x02: result=0, error=0 */
    out[7] = 0x02; out[8] = 4; out[9] = 0;
    out[10] = 0; out[11] = 0; out[12] = 0; out[13] = 0;
    /* TLV 0x10: client_id */
    out[14] = 0x10; out[15] = 8; out[16] = 0;
    for (unsigned i = 0; i < 8; ++i) out[17 + i] = (uint8_t)(client_id >> (i * 8));
    return 25u;
}

static void cc_flush(struct compass_core_mock *m)
{
    while (m->q_head != m->q_tail) {
        unsigned idx = m->q_tail % 32u;
        unsigned conn = m->q[idx].conn;
        uint32_t node = m->q[idx].node;
        uint32_t port = m->q[idx].port;
        uint16_t txn = m->q[idx].txn;
        m->q_tail++;
        uint8_t rsp[32];
        uint32_t rlen = build_qmi_resp_ok(rsp, txn, 100ull + conn);
        sns_core_receive(m->core, conn, node, port, rsp, rlen);
    }
}

static uint32_t build_qmi_report(uint8_t *out, uint16_t txn, const SscSuid *suid, uint32_t msg_id,
    uint64_t ts, const uint8_t *payload, uint32_t payload_len)
{
    uint8_t ev_inner[256];
    SpbOut o;
    spb_out(&o, ev_inner, sizeof(ev_inner));
    spb_put_fixed32(&o, 1, msg_id);
    spb_put_fixed64(&o, 2, ts);
    spb_put_bytes(&o, 3, payload, payload_len);
    uint32_t ev_len = spb_done(&o);

    uint8_t suid_buf[32];
    spb_out(&o, suid_buf, sizeof(suid_buf));
    spb_put_fixed64(&o, 1, suid->low);
    spb_put_fixed64(&o, 2, suid->high);
    uint32_t suid_len = spb_done(&o);

    uint8_t msg[320];
    spb_out(&o, msg, sizeof(msg));
    spb_put_bytes(&o, 1, suid_buf, suid_len);
    spb_put_bytes(&o, 2, ev_inner, ev_len);
    uint32_t msg_len = spb_done(&o);

    uint32_t tlv_len = 3u + 2u + msg_len;
    out[0] = SSC_QMI_INDICATION;
    out[1] = (uint8_t)(txn & 0xffu);
    out[2] = (uint8_t)(txn >> 8);
    out[3] = SSC_QMI_REPORT;
    out[4] = 0;
    out[5] = (uint8_t)(tlv_len & 0xffu);
    out[6] = (uint8_t)(tlv_len >> 8);
    out[7] = 0x02;
    out[8] = (uint8_t)((2u + msg_len) & 0xffu);
    out[9] = (uint8_t)((2u + msg_len) >> 8);
    out[10] = (uint8_t)(msg_len & 0xffu);
    out[11] = (uint8_t)(msg_len >> 8);
    memcpy(out + 12, msg, msg_len);
    return 12u + msg_len;
}

static int cc_send(void *ctx, unsigned conn, uint32_t node, uint32_t port, const uint8_t *data, uint32_t length)
{
    struct compass_core_mock *m = (struct compass_core_mock *)ctx;
    m->tx_count++;
    SscQmi q;
    CHECK(ssc_qmi_parse(data, length, &q) == 0);
    /* Capture SUID lookup requests (data_type and default_only flag). */
    if (conn == SNS_CONN_LOOKUP) {
        uint32_t tsz = 0;
        const uint8_t *tlv = ssc_qmi_tlv(&q, 0x01, &tsz);
        if (tlv && tsz >= 2u) {
            uint32_t mlen = (uint32_t)tlv[0] | ((uint32_t)tlv[1] << 8);
            if (mlen + 2u <= tsz) {
                SpbIn in;
                uint32_t f, w, msg_id = 0;
                const uint8_t *req_bytes = NULL;
                uint32_t req_len = 0;
                spb_in(&in, tlv + 2, mlen);
                while (spb_next(&in, &f, &w) == 1) {
                    if (f == 2 && w == SPB_I32) spb_fixed32_value(&in, w, &msg_id);
                    else if (f == 4 && w == SPB_LEN) spb_bytes_value(&in, w, &req_bytes, &req_len);
                    else spb_skip(&in, w);
                }
                if (msg_id == SSC_MSG_SUID_REQ && req_bytes && m->suid_req_count < 16u) {
                    SpbIn ri;
                    spb_in(&ri, req_bytes, req_len);
                    while (spb_next(&ri, &f, &w) == 1) {
                        const uint8_t *pl = NULL;
                        uint32_t pl_len = 0;
                        if (f == 2 && w == SPB_LEN && spb_bytes_value(&ri, w, &pl, &pl_len) == 0) {
                            SpbIn pi;
                            spb_in(&pi, pl, pl_len);
                            unsigned slot = m->suid_req_count++;
                            m->suid_req_type[slot][0] = 0;
                            m->suid_req_default_only[slot] = 99;
                            while (spb_next(&pi, &f, &w) == 1) {
                                if (f == 1 && w == SPB_LEN) {
                                    const uint8_t *sb = NULL;
                                    uint32_t sl = 0;
                                    if (spb_bytes_value(&pi, w, &sb, &sl) == 0) {
                                        if (sl >= sizeof(m->suid_req_type[slot])) sl = sizeof(m->suid_req_type[slot]) - 1u;
                                        memcpy(m->suid_req_type[slot], sb, sl);
                                        m->suid_req_type[slot][sl] = 0;
                                    }
                                }
                                else if (f == 3 && w == SPB_VARINT) spb_varint_value(&pi, w, &m->suid_req_default_only[slot]);
                                else spb_skip(&pi, w);
                            }
                        } else spb_skip(&ri, w);
                    }
                }
            }
        }
    }
    /* Capture per-sensor config rates and disable requests */
    if (conn >= 1u && conn < 1u + PaganiSensorKinds) {
        const unsigned skind = conn - 1u;
        uint32_t tsz = 0;
        const uint8_t *tlv = ssc_qmi_tlv(&q, 0x01, &tsz);
        if (tlv && tsz >= 2u) {
            uint32_t mlen = (uint32_t)tlv[0] | ((uint32_t)tlv[1] << 8);
            if (mlen + 2u <= tsz) {
                SpbIn in;
                uint32_t f, w;
                uint32_t msg_id = 0;
                const uint8_t *req_bytes = NULL;
                uint32_t req_len = 0;
                spb_in(&in, tlv + 2, mlen);
                while (spb_next(&in, &f, &w) == 1) {
                    if (f == 2 && w == SPB_I32) spb_fixed32_value(&in, w, &msg_id);
                    else if (f == 4 && w == SPB_LEN) spb_bytes_value(&in, w, &req_bytes, &req_len);
                    else spb_skip(&in, w);
                }
                if (msg_id == SSC_MSG_DISABLE_REQ) {
                    m->disable_count[skind]++;
                }
                if (msg_id == SSC_MSG_SENSOR_CONFIG && req_bytes) {
                    SpbIn ri;
                    spb_in(&ri, req_bytes, req_len);
                    while (spb_next(&ri, &f, &w) == 1) {
                        const uint8_t *pl = NULL;
                        uint32_t pl_len = 0;
                        if (f == 2 && w == SPB_LEN && spb_bytes_value(&ri, w, &pl, &pl_len) == 0) {
                            SpbIn pi;
                            spb_in(&pi, pl, pl_len);
                            while (spb_next(&pi, &f, &w) == 1) {
                                if (f == 1 && w == SPB_I32) {
                                    uint32_t rate_bits = 0;
                                    spb_fixed32_value(&pi, w, &rate_bits);
                                    m->last_rate_bits[skind] = rate_bits;
                                    m->config_count[skind]++;
                                    if (skind == PaganiSensorRotv) m->last_rotv_rate_bits = rate_bits;
                                } else spb_skip(&pi, w);
                            }
                        } else spb_skip(&ri, w);
                    }
                }
            }
        }
    }
    CHECK(m->q_head - m->q_tail < 32u);
    unsigned idx = m->q_head % 32u;
    m->q[idx].conn = conn;
    m->q[idx].node = node;
    m->q[idx].port = port;
    m->q[idx].txn = q.transaction;
    m->q_head++;
    return 0;
}

static void test_compass_and_core(void)
{
    /* 1. atan2 table accuracy across 360 degrees */
    for (int deg = -180; deg <= 180; deg += 3) {
        double rad = (double)deg * 3.14159265358979323846 / 180.0;
        int32_t x = (int32_t)llround(cos(rad) * 100000.0);
        int32_t y = (int32_t)llround(sin(rad) * 100000.0);
        int32_t got = sns_compass_atan2_mdeg(y, x);
        double want = atan2((double)y, (double)x) * 180000.0 / 3.14159265358979323846;
        double diff = fabs((double)got - want);
        if (diff > 180000.0) diff = 360000.0 - diff;
        CHECK(diff < 15.0); /* < 0.015 deg */
    }

    /* 2. Float ratio conversion */
    CHECK(fabs(bits2f(sns_compass_float_signed(0, 65536)) - 0.0f) < 1e-6);
    CHECK(fabs(bits2f(sns_compass_float_signed(65536, 65536)) - 1.0f) < 1e-6);
    CHECK(fabs(bits2f(sns_compass_float_signed(-65536 * 25, 65536)) - (-25.0f)) < 1e-4);
    CHECK(fabs(bits2f(sns_compass_float_signed(1073741824ll / 2, 1073741824ull)) - 0.5f) < 1e-6);

    /* 3. Flat headings (0, 37, 90, 135, 180, 225, 270, 315) via rotv */
    const double test_headings[] = {0.0, 37.0, 90.0, 135.0, 180.0, 225.0, 270.0, 315.0};
    const double flat_grav[3] = {0.0, 0.0, -1.0};
    for (unsigned idx = 0; idx < sizeof(test_headings) / sizeof(test_headings[0]); ++idx) {
        SnsCompass comp;
        sns_compass_init(&comp);
        sns_compass_set_mode(&comp, SNS_COMPASS_MODE_ROTV);
        int32_t acc[3] = {0, 0, -65536};
        int32_t gyro[3] = {0, 0, 0};
        int32_t mag[3] = {200 * 65536, 50 * 65536, 440 * 65536}; /* huge uncalibrated offset */
        sns_compass_set_accel(&comp, acc);
        sns_compass_set_gyro(&comp, gyro);
        sns_compass_set_mag(&comp, mag, 3);
        /* Heading H clockwise from North = yaw -H around ENU +Z */
        double yaw = -test_headings[idx] * 3.14159265358979323846 / 180.0;
        uint32_t q[4] = {f2bits(0.0f), f2bits(0.0f), f2bits((float)sin(yaw / 2.0)), f2bits((float)cos(yaw / 2.0))};
        CHECK(sns_compass_set_rotv(&comp, q, 3) == 0);
        uint8_t rep[SNS_COMPASS_REPORT_BYTES];
        int32_t hdg = -1;
        CHECK(sns_compass_build_report(&comp, rep, &hdg) == 0);
        verify_locationd_report(rep, test_headings[idx], flat_grav, 0.2);
        CHECK(rd16(rep + 0x02) == 0x271cu);
        /* User accel at rest should be ~0 */
        CHECK(fabs(rd_f32(rep, 0x38)) < 0.02f);
        CHECK(fabs(rd_f32(rep, 0x3c)) < 0.02f);
        CHECK(fabs(rd_f32(rep, 0x40)) < 0.02f);
    }

    /* 4. Upright portrait (pitched +90 deg around X, rotated by heading H around world Z) */
    const double upright_grav[3] = {0.0, -1.0, 0.0};
    for (unsigned idx = 0; idx < 4u; ++idx) {
        double hdeg = idx * 90.0;
        double yaw = -hdeg * 3.14159265358979323846 / 180.0;
        /* q = q_z(yaw) * q_x(+90deg) */
        double cz = cos(yaw / 2.0), sz = sin(yaw / 2.0);
        double cx = sqrt(0.5), sx = sqrt(0.5);
        double qx = cz * sx;
        double qy = sz * sx;
        double qz = sz * cx;
        double qw = cz * cx;
        SnsCompass comp;
        sns_compass_init(&comp);
        sns_compass_set_mode(&comp, SNS_COMPASS_MODE_ROTV);
        int32_t acc[3] = {0, -65536, 0};
        sns_compass_set_accel(&comp, acc);
        uint32_t qb[4] = {f2bits((float)qx), f2bits((float)qy), f2bits((float)qz), f2bits((float)qw)};
        CHECK(sns_compass_set_rotv(&comp, qb, 2) == 0);
        uint8_t rep[SNS_COMPASS_REPORT_BYTES];
        int32_t hdg = -1;
        CHECK(sns_compass_build_report(&comp, rep, &hdg) == 0);
        verify_locationd_report(rep, hdeg, upright_grav, 0.2);
        CHECK(rd16(rep + 0x02) == 0x251cu);
    }

    /* 5. Fallback accel + mag + mag_cal (without rotv) covering all 4 mat3->quat branches */
    for (unsigned idx = 0; idx < 4u; ++idx) {
        double hdeg = idx * 90.0;
        double rad = hdeg * 3.14159265358979323846 / 180.0;
        /* Flat phone rotated CW by hdeg: ENU North in body frame is (-sin H, cos H, 0) */
        double mx = -35.0 * sin(rad);
        double my = 35.0 * cos(rad);
        double mz = -25.0;
        SnsCompass comp;
        sns_compass_init(&comp);
        int32_t acc[3] = {0, 0, -65536};
        int32_t bias[3] = {200 * 65536, 40 * 65536, 450 * 65536};
        int32_t raw_mag[3] = {
            bias[0] + (int32_t)llround(mx * 65536.0),
            bias[1] + (int32_t)llround(my * 65536.0),
            bias[2] + (int32_t)llround(mz * 65536.0),
        };
        sns_compass_set_accel(&comp, acc);
        sns_compass_set_mag(&comp, raw_mag, 3);
        sns_compass_set_mag_cal(&comp, bias, 3);
        uint8_t rep[SNS_COMPASS_REPORT_BYTES];
        int32_t hdg = -1;
        CHECK(sns_compass_build_report(&comp, rep, &hdg) == 0);
        verify_locationd_report(rep, hdeg, flat_grav, 0.5);
        CHECK(rd16(rep + 0x02) == 0x251cu);
        /* Real calibrated mag should be preserved verbatim */
        CHECK(fabs((double)rd_f32(rep, 0x10) - (double)rd_f32(rep, 0x4c) - mx) < 0.05);
        CHECK(fabs((double)rd_f32(rep, 0x14) - (double)rd_f32(rep, 0x50) - my) < 0.05);
    }

    /* 6. NaN / Inf / zero quaternion rejected */
    {
        SnsCompass comp;
        sns_compass_init(&comp);
        uint32_t qnan[4] = {0x7fc00000u, 0, 0, f2bits(1.0f)};
        uint32_t qinf[4] = {0x7f800000u, 0, 0, f2bits(1.0f)};
        uint32_t qzero[4] = {0, 0, 0, 0};
        CHECK(sns_compass_set_rotv(&comp, qnan, 3) != 0);
        CHECK(sns_compass_set_rotv(&comp, qinf, 3) != 0);
        CHECK(sns_compass_set_rotv(&comp, qzero, 3) != 0);
        uint8_t rep[SNS_COMPASS_REPORT_BYTES];
        CHECK(sns_compass_build_report(&comp, rep, NULL) != 0);
    }

    /* 7. ssc_parse_cal_event (packed & unpacked) + HID DevMotion metadata */
    {
        uint8_t buf[64];
        SpbOut o;
        spb_out(&o, buf, sizeof(buf));
        spb_put_fixed32(&o, 1, f2bits(204.5f));
        spb_put_fixed32(&o, 1, f2bits(43.25f));
        spb_put_fixed32(&o, 1, f2bits(447.75f));
        spb_put_varint(&o, 4, 3);
        SscCalEvent cev;
        CHECK(ssc_parse_cal_event(buf, spb_done(&o), &cev) == 0);
        CHECK(cev.count == 3u && cev.status == 3);
        CHECK(fabs(bits2f(cev.bias[0]) - 204.5f) < 1e-4);
        CHECK(fabs(bits2f(cev.bias[1]) - 43.25f) < 1e-4);
        CHECK(fabs(bits2f(cev.bias[2]) - 447.75f) < 1e-4);

        const SnsHidKind *dm = sns_hid_kind(SNS_HID_DEVMOTION);
        CHECK(dm && dm->page == 0xff0cu && dm->usage == 0x08u);
        uint32_t dlen = 0;
        CHECK(sns_hid_descriptor(SNS_HID_DEVMOTION, &dlen) != NULL && dlen > 8u);
        uint32_t dem = 0;
        CHECK(sns_hid_demand(SNS_HID_DEVMOTION, SNS_HID_KEY_REPORT_INTERVAL, 40000u, &dem) == 1 && dem == 40000u);
    }

    /* 8. End-to-end SnsCore with rotv [5,200] rate clamp + mag_cal + compass demand */
    {
        SnsCore *core = (SnsCore *)calloc(1, sizeof(SnsCore));
        CHECK(core != NULL);
        struct compass_core_mock m;
        memset(&m, 0, sizeof(m));
        m.now_ms = 1000;
        m.core = core;
        SnsOps ops;
        memset(&ops, 0, sizeof(ops));
        ops.context = &m;
        ops.now_ms = cc_now;
        ops.send = cc_send;
        ops.trace = cc_trace;
        ops.compass = cc_compass;

        SnsConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.mode = 3u;
        cfg.compass = 1u;
        cfg.compass_mode = SNS_COMPASS_MODE_ROTV;
        cfg.log = 2u;
        /* Leave PaganiSensorLight empty (als=0) and verify akm_rotv uses default_only=0. */
        strcpy(cfg.type[PaganiSensorRotv], "akm_rotv");
        strcpy(cfg.type[PaganiSensorMagCal], "mag_cal");
        sns_core_init(core, &ops, &cfg);
        sns_core_server(core, 1, 7u, 42u, 1u, 1u);
        cc_flush(&m);

        CHECK(m.suid_req_count == 2u);
        CHECK(strcmp(m.suid_req_type[0], "akm_rotv") == 0 && m.suid_req_default_only[0] == 0u);
        CHECK(strcmp(m.suid_req_type[1], "mag_cal") == 0 && m.suid_req_default_only[1] == 1u);
        CHECK(core->sensor[PaganiSensorLight].found == 0);

        const SscSuid rotv_suid = {0x1111222233334444ull, 0x5555666677778888ull};
        const SscSuid mcal_suid = {0xaaaabbbbccccddddull, 0x123456789abcdef0ull};

        /* Inject SUID events for rotv and mag_cal */
        for (int which = 0; which < 2; ++which) {
            const char *name = which == 0 ? "akm_rotv" : "mag_cal";
            const SscSuid *suid = which == 0 ? &rotv_suid : &mcal_suid;
            uint8_t sb[32], pl[96], qmi[384];
            SpbOut o;
            spb_out(&o, sb, sizeof(sb));
            spb_put_fixed64(&o, 1, suid->low);
            spb_put_fixed64(&o, 2, suid->high);
            uint32_t sbl = spb_done(&o);
            spb_out(&o, pl, sizeof(pl));
            spb_put_string(&o, 1, name);
            spb_put_bytes(&o, 2, sb, sbl);
            uint32_t qlen = build_qmi_report(qmi, (uint16_t)(10 + which), &ssc_suid_lookup,
                SSC_MSG_SUID_EVENT, 1000, pl, spb_done(&o));
            sns_core_receive(core, SNS_CONN_LOOKUP, 7u, 42u, qmi, qlen);
            cc_flush(&m);
        }
        CHECK(core->sensor[PaganiSensorRotv].found == 1);
        CHECK(core->sensor[PaganiSensorMagCal].found == 1);

        /* Inject rotv attributes with rates=[5.0, 200.0] and stream_type=0 */
        {
            uint8_t d1[16], d2[16], av[48], a_rates[64], a_st[48], st_d[16], st_v[24], pl[160], qmi[384];
            SpbOut o;
            spb_out(&o, d1, sizeof(d1)); spb_put_fixed32(&o, 3, f2bits(5.0f)); uint32_t d1l = spb_done(&o);
            spb_out(&o, d2, sizeof(d2)); spb_put_fixed32(&o, 3, f2bits(200.0f)); uint32_t d2l = spb_done(&o);
            spb_out(&o, av, sizeof(av)); spb_put_bytes(&o, 1, d1, d1l); spb_put_bytes(&o, 1, d2, d2l); uint32_t avl = spb_done(&o);
            spb_out(&o, a_rates, sizeof(a_rates)); spb_put_varint(&o, 1, SSC_ATTR_RATES); spb_put_bytes(&o, 2, av, avl); uint32_t arl = spb_done(&o);
            spb_out(&o, st_d, sizeof(st_d)); spb_put_fixed64(&o, 4, SSC_STREAM_STREAMING); uint32_t stdl = spb_done(&o);
            spb_out(&o, st_v, sizeof(st_v)); spb_put_bytes(&o, 1, st_d, stdl); uint32_t stvl = spb_done(&o);
            spb_out(&o, a_st, sizeof(a_st)); spb_put_varint(&o, 1, SSC_ATTR_STREAM_TYPE); spb_put_bytes(&o, 2, st_v, stvl); uint32_t astl = spb_done(&o);
            spb_out(&o, pl, sizeof(pl)); spb_put_bytes(&o, 1, a_rates, arl); spb_put_bytes(&o, 1, a_st, astl);
            uint32_t qlen = build_qmi_report(qmi, 20, &rotv_suid, SSC_MSG_ATTR_EVENT, 2000, pl, spb_done(&o));
            sns_core_receive(core, 1u + PaganiSensorRotv, 7u, 42u, qmi, qlen);
            cc_flush(&m);
        }
        CHECK(core->sensor[PaganiSensorRotv].attr_state == 2);

        /* Demand compass at 40000 us (25 Hz) -> rotv must stream at 25 Hz (NOT 200 Hz!) */
        sns_core_compass_demand(core, 40000u);
        cc_flush(&m);
        CHECK(core->sensor[PaganiSensorRotv].active == SNS_STREAMING);
        CHECK(fabs(bits2f(m.last_rotv_rate_bits) - 25.0f) < 0.1f);

        /* Inject mag_cal SSC_MSG_CAL_EVENT and rotv SSC_MSG_SENSOR_EVENT (East = 90 deg) */
        {
            uint8_t pl[64], qmi[384];
            SpbOut o;
            spb_out(&o, pl, sizeof(pl));
            spb_put_fixed32(&o, 1, f2bits(10.0f));
            spb_put_fixed32(&o, 1, f2bits(20.0f));
            spb_put_fixed32(&o, 1, f2bits(30.0f));
            spb_put_varint(&o, 4, 3);
            uint32_t qlen = build_qmi_report(qmi, 21, &mcal_suid, SSC_MSG_CAL_EVENT, 3000, pl, spb_done(&o));
            sns_core_receive(core, 1u + PaganiSensorMagCal, 7u, 42u, qmi, qlen);
            CHECK(core->compass.mag_cal_valid == 1 && core->compass.mag_cal_acc == 3);

            double yaw = -90.0 * 3.14159265358979323846 / 180.0;
            spb_out(&o, pl, sizeof(pl));
            spb_put_fixed32(&o, 1, f2bits(0.0f));
            spb_put_fixed32(&o, 1, f2bits(0.0f));
            spb_put_fixed32(&o, 1, f2bits((float)sin(yaw / 2.0)));
            spb_put_fixed32(&o, 1, f2bits((float)cos(yaw / 2.0)));
            spb_put_varint(&o, 2, 3);
            qlen = build_qmi_report(qmi, 22, &rotv_suid, SSC_MSG_SENSOR_EVENT, 4000, pl, spb_done(&o));
            sns_core_receive(core, 1u + PaganiSensorRotv, 7u, 42u, qmi, qlen);
        }
        CHECK(m.compass_count == 1u);
        verify_locationd_report(m.last_report, 90.0, flat_grav, 0.2);
        free(core);
    }

    /* 9. 8-sample magnetometer moving average + gyro-integrated complementary fallback */
    {
        SnsCompass comp;
        sns_compass_init(&comp);
        int32_t acc[3] = {0, 0, -65536};
        int32_t bias[3] = {200 * 65536, 40 * 65536, 450 * 65536};
        sns_compass_set_accel(&comp, acc);
        sns_compass_set_mag_cal(&comp, bias, 3);

        /* Feed 8 alternating noisy mag samples around North (0, 35, -25) uT: +-10 uT on X. */
        for (unsigned step = 0; step < 8u; ++step) {
            int32_t dx = (step & 1u) ? (10 * 65536) : (-10 * 65536);
            int32_t raw_m[3] = {bias[0] + dx, bias[1] + 35 * 65536, bias[2] - 25 * 65536};
            sns_compass_set_mag(&comp, raw_m, 3);
        }
        CHECK(comp.mag_hist_count == 8u);
        CHECK(comp.mag_raw[0] == bias[0]);
        CHECK(comp.mag_raw[1] == bias[1] + 35 * 65536);
        CHECK(comp.mag_raw[2] == bias[2] - 25 * 65536);

        /* Initialize fallback at heading 0 deg, then inject noisy +-12 uT mag at rest with zero gyro. */
        uint8_t rep[SNS_COMPASS_REPORT_BYTES];
        int32_t hdg = -1;
        uint64_t ts = 19200000ull;
        int32_t zero_gyro[3] = {0, 0, 0};
        sns_compass_set_gyro_ts(&comp, zero_gyro, ts);
        CHECK(sns_compass_build_report(&comp, rep, &hdg) == 0);
        verify_locationd_report(rep, 0.0, flat_grav, 0.5);
        for (unsigned step = 0; step < 30u; ++step) {
            ts += 384000ull; /* 20 ms at 19.2 MHz */
            sns_compass_set_gyro_ts(&comp, zero_gyro, ts);
            int32_t dx = (step & 1u) ? (12 * 65536) : (-12 * 65536);
            int32_t raw_m[3] = {bias[0] + dx, bias[1] + 35 * 65536, bias[2] - 25 * 65536};
            sns_compass_set_mag(&comp, raw_m, 3);
            CHECK(sns_compass_build_report(&comp, rep, &hdg) == 0);
            double hdeg = (double)hdg / 1000.0;
            check_angle_close(hdeg, 0.0, 1.5);
        }

        /* Rotate 90 deg CW around +Z (-pi/2 rad/s for 1.0 s = 50 steps of 20 ms) using gyro integration. */
        SnsCompass rotc;
        sns_compass_init(&rotc);
        sns_compass_set_accel(&rotc, acc);
        sns_compass_set_mag_cal(&rotc, bias, 3);
        int32_t north_m[3] = {bias[0], bias[1] + 35 * 65536, bias[2] - 25 * 65536};
        sns_compass_set_mag(&rotc, north_m, 3);
        ts = 19200000ull;
        int32_t cw_gyro[3] = {0, 0, (int32_t)llround((-3.14159265358979323846 / 2.0) * 65536.0)};
        sns_compass_set_gyro_ts(&rotc, cw_gyro, ts);
        CHECK(sns_compass_build_report(&rotc, rep, &hdg) == 0);
        check_angle_close((double)hdg / 1000.0, 0.0, 0.5);
        for (unsigned step = 0; step < 50u; ++step) {
            ts += 384000ull;
            sns_compass_set_gyro_ts(&rotc, cw_gyro, ts);
        }
        /* Pure gyro-integrated heading after 90 deg CW rotation (before mag pull) should be ~90 deg. */
        double rad90 = 3.14159265358979323846 / 2.0;
        int32_t east_m[3] = {
            bias[0] + (int32_t)llround(-35.0 * sin(rad90) * 65536.0),
            bias[1] + (int32_t)llround(35.0 * cos(rad90) * 65536.0),
            bias[2] - 25 * 65536,
        };
        for (unsigned fill = 0; fill < 8u; ++fill) {
            sns_compass_set_mag(&rotc, east_m, 3);
        }
        CHECK(sns_compass_build_report(&rotc, rep, &hdg) == 0);
        verify_locationd_report(rep, 90.0, flat_grav, 1.5);
    }

    /* 10. Stuck-rotv watchdog unit tests: zero false positives at rest, gyro & gravity triggers, yaw-preserving fallback, recovery */
    {
        SnsCompass comp;
        sns_compass_init(&comp);
        sns_compass_set_mode(&comp, SNS_COMPASS_MODE_ROTV);
        int32_t flat_acc[3] = {0, 0, -65536};
        int32_t zero_gyro[3] = {0, 0, 0};
        sns_compass_set_accel(&comp, flat_acc);
        sns_compass_set_gyro_ts(&comp, zero_gyro, 19200000ull);
        /* Stationary phone on desk: 60 identical rotv samples must NEVER trigger stuck. */
        uint32_t q_flat[4] = {f2bits(0.0f), f2bits(0.0f), f2bits(0.590f), f2bits(0.807f)};
        for (unsigned step = 0; step < 60u; ++step) {
            CHECK(sns_compass_set_rotv(&comp, q_flat, 1) == 0);
        }
        CHECK(comp.rotv_stuck == 0 && comp.rotv_valid == 1 && comp.rotv_stuck_events == 0u);

        /* Trigger A: sustained gyro rotation while rotv stays frozen -> quarantine + yaw-preserving fallback even with mcal=0. */
        int32_t cw_gyro[3] = {0, 0, (int32_t)llround((-3.14159265358979323846 / 2.0) * 65536.0)};
        uint64_t ts = 19200000ull;
        sns_compass_set_gyro_ts(&comp, cw_gyro, ts);
        uint8_t rep[SNS_COMPASS_REPORT_BYTES];
        int32_t hdg_before = -1;
        CHECK(sns_compass_build_report(&comp, rep, &hdg_before) == 0);
        for (unsigned step = 0; step < 26u; ++step) {
            ts += 384000ull;
            sns_compass_set_gyro_ts(&comp, cw_gyro, ts);
            CHECK(sns_compass_set_rotv(&comp, q_flat, 1) == 0);
        }
        CHECK(comp.rotv_stuck == 1 && comp.rotv_valid == 0 && comp.rotv_stuck_events == 1u);
        int32_t hdg_after_trip = -1;
        CHECK(sns_compass_build_report(&comp, rep, &hdg_after_trip) == 0);
        /* Integrate 0.5 s of CW rotation (-pi/2 rad/s => +45 deg) with NO mag_cal: yaw must track +45 deg, NOT snap to 0! */
        for (unsigned step = 0; step < 25u; ++step) {
            ts += 384000ull;
            sns_compass_set_gyro_ts(&comp, cw_gyro, ts);
            CHECK(sns_compass_build_report(&comp, rep, &hdg_after_trip) == 0);
        }
        double expected_hdg = fmod((double)hdg_before / 1000.0 + 45.0, 360.0);
        check_angle_close((double)hdg_after_trip / 1000.0, expected_hdg, 4.0);

        /* Trigger B (exact 081533 bug): phone tilted to landscape while rotv stays frozen flat (0,0,0.590,0.807). */
        SnsCompass tilt;
        sns_compass_init(&tilt);
        sns_compass_set_mode(&tilt, SNS_COMPASS_MODE_ROTV);
        int32_t land_acc[3] = {-65536, 0, 0};
        sns_compass_set_accel(&tilt, land_acc);
        sns_compass_set_gyro(&tilt, zero_gyro);
        for (unsigned step = 0; step < 27u; ++step) {
            CHECK(sns_compass_set_rotv(&tilt, q_flat, 1) == 0);
        }
        CHECK(tilt.rotv_stuck == 1 && tilt.rotv_valid == 0 && tilt.rotv_stuck_events == 1u);
        int32_t land_hdg = -1;
        CHECK(sns_compass_build_report(&tilt, rep, &land_hdg) == 0);
        const double land_grav[3] = {-1.0, 0.0, 0.0};
        verify_locationd_report(rep, (double)land_hdg / 1000.0, land_grav, 2.0);

        /* Bounce recovery: dummy init samples + repeated frozen quaternion stay quarantined; real landscape quat unsticks. */
        sns_compass_rotv_bounced(&tilt);
        uint32_t q_dummy[4] = {f2bits(0.5f), f2bits(-0.5f), f2bits(-0.5f), f2bits(0.5f)};
        CHECK(sns_compass_set_rotv(&tilt, q_dummy, 1) == 0);
        CHECK(sns_compass_set_rotv(&tilt, q_dummy, 1) == 0);
        CHECK(tilt.rotv_stuck == 1 && tilt.rotv_valid == 0);
        for (unsigned step = 0; step < 5u; ++step) {
            CHECK(sns_compass_set_rotv(&tilt, q_flat, 1) == 0);
        }
        CHECK(tilt.rotv_stuck == 1 && tilt.rotv_valid == 0);
        /* Real landscape ENU quat: pitched/rolled so body +X points Up -> q_y(-90 deg) = (0, -sqrt(0.5), 0, sqrt(0.5)). */
        uint32_t q_land[4] = {f2bits(0.0f), f2bits((float)(-sqrt(0.5))), f2bits(0.0f), f2bits((float)sqrt(0.5))};
        for (unsigned step = 0; step < 3u; ++step) {
            CHECK(sns_compass_set_rotv(&tilt, q_land, 2) == 0);
        }
        CHECK(tilt.rotv_stuck == 0 && tilt.rotv_valid == 1);
        CHECK(sns_compass_build_report(&tilt, rep, &land_hdg) == 0);
        verify_locationd_report(rep, (double)land_hdg / 1000.0, land_grav, 1.0);
    }

    /* 11. End-to-end SnsCore: 100 Hz accel+gyro lock, rotv sequenced start, 200 Hz cap, and automatic stuck-rotv bounce */
    {
        SnsCore *core = (SnsCore *)calloc(1, sizeof(SnsCore));
        CHECK(core != NULL);
        struct compass_core_mock m;
        memset(&m, 0, sizeof(m));
        m.now_ms = 1000;
        m.core = core;
        SnsOps ops;
        memset(&ops, 0, sizeof(ops));
        ops.context = &m;
        ops.now_ms = cc_now;
        ops.send = cc_send;
        ops.trace = cc_trace;
        ops.compass = cc_compass;

        SnsConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.mode = 3u;
        cfg.orient = 1u;
        cfg.compass = 1u;
        cfg.compass_mode = SNS_COMPASS_MODE_ROTV;
        cfg.motion_hz = 50u;
        cfg.log = 2u;
        strcpy(cfg.type[PaganiSensorAccel], "accel");
        strcpy(cfg.type[PaganiSensorGyro], "gyro");
        strcpy(cfg.type[PaganiSensorMag], "mag");
        strcpy(cfg.type[PaganiSensorRotv], "akm_rotv");
        strcpy(cfg.type[PaganiSensorMagCal], "mag_cal");
        sns_core_init(core, &ops, &cfg);
        sns_core_server(core, 1, 7u, 42u, 1u, 1u);
        cc_flush(&m);

        const SscSuid suids[5] = {
            {0x1001ull, 0x2001ull}, {0x1002ull, 0x2002ull}, {0x1003ull, 0x2003ull},
            {0x1004ull, 0x2004ull}, {0x1005ull, 0x2005ull},
        };
        const char *names[5] = {"accel", "gyro", "mag", "akm_rotv", "mag_cal"};
        const unsigned kinds[5] = {
            PaganiSensorAccel, PaganiSensorGyro, PaganiSensorMag, PaganiSensorRotv, PaganiSensorMagCal,
        };
        for (unsigned idx = 0; idx < 5u; ++idx) {
            uint8_t sb[32], pl[96], qmi[384];
            SpbOut o;
            spb_out(&o, sb, sizeof(sb));
            spb_put_fixed64(&o, 1, suids[idx].low);
            spb_put_fixed64(&o, 2, suids[idx].high);
            uint32_t sbl = spb_done(&o);
            spb_out(&o, pl, sizeof(pl));
            spb_put_string(&o, 1, names[idx]);
            spb_put_bytes(&o, 2, sb, sbl);
            uint32_t qlen = build_qmi_report(qmi, (uint16_t)(10 + idx), &ssc_suid_lookup,
                SSC_MSG_SUID_EVENT, 1000, pl, spb_done(&o));
            sns_core_receive(core, SNS_CONN_LOOKUP, 7u, 42u, qmi, qlen);
            cc_flush(&m);
        }

        /* Inject bmi26x [25,50,100,200,400], ak0991x [5,10,20,50,100], and rotv [1,5,10,25,50,100] attrs. */
        const float bmi_rates[5] = {25.0f, 50.0f, 100.0f, 200.0f, 400.0f};
        const float mag_rates[5] = {5.0f, 10.0f, 20.0f, 50.0f, 100.0f};
        const float rotv_rates[6] = {1.0f, 5.0f, 10.0f, 25.0f, 50.0f, 100.0f};
        for (unsigned idx = 0; idx < 4u; ++idx) {
            const float *rlist = (idx < 2u) ? bmi_rates : (idx == 2u) ? mag_rates : rotv_rates;
            const unsigned rcnt = (idx == 3u) ? 6u : 5u;
            uint8_t av[128], a_rates[144], st_d[16], st_v[24], a_st[32], pl[220], qmi[384];
            SpbOut o;
            spb_out(&o, av, sizeof(av));
            for (unsigned r = 0; r < rcnt; ++r) {
                uint8_t d[16];
                SpbOut od;
                spb_out(&od, d, sizeof(d));
                spb_put_fixed32(&od, 3, f2bits(rlist[r]));
                spb_put_bytes(&o, 1, d, spb_done(&od));
            }
            uint32_t avl = spb_done(&o);
            spb_out(&o, a_rates, sizeof(a_rates));
            spb_put_varint(&o, 1, SSC_ATTR_RATES);
            spb_put_bytes(&o, 2, av, avl);
            uint32_t arl = spb_done(&o);
            spb_out(&o, st_d, sizeof(st_d));
            spb_put_fixed64(&o, 4, SSC_STREAM_STREAMING);
            uint32_t stdl = spb_done(&o);
            spb_out(&o, st_v, sizeof(st_v));
            spb_put_bytes(&o, 1, st_d, stdl);
            uint32_t stvl = spb_done(&o);
            spb_out(&o, a_st, sizeof(a_st));
            spb_put_varint(&o, 1, SSC_ATTR_STREAM_TYPE);
            spb_put_bytes(&o, 2, st_v, stvl);
            uint32_t astl = spb_done(&o);
            spb_out(&o, pl, sizeof(pl));
            spb_put_bytes(&o, 1, a_rates, arl);
            spb_put_bytes(&o, 1, a_st, astl);
            uint32_t qlen = build_qmi_report(qmi, (uint16_t)(20 + idx), &suids[idx],
                SSC_MSG_ATTR_EVENT, 2000, pl, spb_done(&o));
            sns_core_receive(core, 1u + kinds[idx], 7u, 42u, qmi, qlen);
            cc_flush(&m);
        }

        /* Accel and gyro must BOTH request 100 Hz; mag 50 Hz; rotv MUST wait until accel+gyro+mag deliver samples! */
        CHECK(core->sensor[PaganiSensorAccel].active == SNS_STREAMING);
        CHECK(core->sensor[PaganiSensorGyro].active == SNS_STREAMING);
        CHECK(core->sensor[PaganiSensorMag].active == SNS_STREAMING);
        CHECK(fabs(bits2f(m.last_rate_bits[PaganiSensorAccel]) - 100.0f) < 0.1f);
        CHECK(fabs(bits2f(m.last_rate_bits[PaganiSensorGyro]) - 100.0f) < 0.1f);
        CHECK(fabs(bits2f(m.last_rate_bits[PaganiSensorMag]) - 50.0f) < 0.1f);
        CHECK(core->sensor[PaganiSensorRotv].active == SNS_OFF);
        CHECK(m.config_count[PaganiSensorRotv] == 0u);

        /* backboardd 200 Hz gyro demand (5000 us) must stay capped at 100 Hz without extra reconfig. */
        const unsigned gyro_cfgs_before = m.config_count[PaganiSensorGyro];
        sns_core_demand(core, PaganiSensorGyro, 5000u);
        cc_flush(&m);
        CHECK(m.config_count[PaganiSensorGyro] == gyro_cfgs_before);
        CHECK(fabs(bits2f(m.last_rate_bits[PaganiSensorGyro]) - 100.0f) < 0.1f);

        /* Inject physical config events to verify hw_rate tracking. */
        for (unsigned idx = 0; idx < 2u; ++idx) {
            uint8_t pl[32], qmi[384];
            SpbOut o;
            spb_out(&o, pl, sizeof(pl));
            spb_put_fixed32(&o, 1, f2bits(100.0f));
            uint32_t qlen = build_qmi_report(qmi, (uint16_t)(30 + idx), &suids[idx],
                SSC_MSG_PHYSICAL_CONFIG_EVENT, 2500, pl, spb_done(&o));
            sns_core_receive(core, 1u + kinds[idx], 7u, 42u, qmi, qlen);
        }
        CHECK(sfx_milli(core->sensor[PaganiSensorAccel].hw_rate) / 1000 == 100);
        CHECK(sfx_milli(core->sensor[PaganiSensorGyro].hw_rate) / 1000 == 100);

        /* Deliver 1 sample each of accel (landscape -9.80665 m/s^2 on X -> +9.80665 SSC X), gyro, mag -> rotv starts! */
        {
            uint8_t pl[64], qmi[384];
            SpbOut o;
            /* SSC accel +9.80665 on X -> sfx_accel_ios flips sign -> -1 g on X (landscape) */
            spb_out(&o, pl, sizeof(pl));
            spb_put_fixed32(&o, 1, f2bits(9.80665f));
            spb_put_fixed32(&o, 1, f2bits(0.0f));
            spb_put_fixed32(&o, 1, f2bits(0.0f));
            spb_put_varint(&o, 2, 3);
            uint32_t qlen = build_qmi_report(qmi, 40, &suids[0], SSC_MSG_SENSOR_EVENT, 3000, pl, spb_done(&o));
            sns_core_receive(core, 1u + PaganiSensorAccel, 7u, 42u, qmi, qlen);
            cc_flush(&m);
            CHECK(core->sensor[PaganiSensorRotv].active == SNS_OFF);

            spb_out(&o, pl, sizeof(pl));
            spb_put_fixed32(&o, 1, f2bits(0.0f));
            spb_put_fixed32(&o, 1, f2bits(0.0f));
            spb_put_fixed32(&o, 1, f2bits(0.0f));
            spb_put_varint(&o, 2, 3);
            qlen = build_qmi_report(qmi, 41, &suids[1], SSC_MSG_SENSOR_EVENT, 3010, pl, spb_done(&o));
            sns_core_receive(core, 1u + PaganiSensorGyro, 7u, 42u, qmi, qlen);
            cc_flush(&m);
            CHECK(core->sensor[PaganiSensorRotv].active == SNS_OFF);

            spb_out(&o, pl, sizeof(pl));
            spb_put_fixed32(&o, 1, f2bits(220.0f));
            spb_put_fixed32(&o, 1, f2bits(60.0f));
            spb_put_fixed32(&o, 1, f2bits(420.0f));
            spb_put_varint(&o, 2, 3);
            qlen = build_qmi_report(qmi, 42, &suids[2], SSC_MSG_SENSOR_EVENT, 3020, pl, spb_done(&o));
            sns_core_receive(core, 1u + PaganiSensorMag, 7u, 42u, qmi, qlen);
            cc_flush(&m);
            CHECK(core->sensor[PaganiSensorRotv].active == SNS_STREAMING);
            CHECK(fabs(bits2f(m.last_rotv_rate_bits) - 50.0f) < 0.1f);
        }

        /* Inject 27 frozen flat rotv samples while accel is landscape -> automatic DISABLE_REQ + live mag fallback! */
        m.now_ms = 2000;
        for (unsigned step = 0; step < 27u; ++step) {
            uint8_t pl[64], qmi[384];
            SpbOut o;
            spb_out(&o, pl, sizeof(pl));
            spb_put_fixed32(&o, 1, f2bits(0.0f));
            spb_put_fixed32(&o, 1, f2bits(0.0f));
            spb_put_fixed32(&o, 1, f2bits(0.590f));
            spb_put_fixed32(&o, 1, f2bits(0.807f));
            spb_put_varint(&o, 2, 1);
            uint32_t qlen = build_qmi_report(qmi, (uint16_t)(50 + step), &suids[3],
                SSC_MSG_SENSOR_EVENT, 4000ull + step * 384000ull, pl, spb_done(&o));
            sns_core_receive(core, 1u + PaganiSensorRotv, 7u, 42u, qmi, qlen);
            cc_flush(&m);
        }
        CHECK(core->compass.rotv_stuck == 1);
        CHECK(core->compass.rotv_valid == 0);
        CHECK(m.disable_count[PaganiSensorRotv] == 1u);
        CHECK(core->sensor[PaganiSensorRotv].active == SNS_OFF);
        CHECK(core->rotv_bounce_phase == 2);

        /* During the 200 ms bounce pause, mag samples continue driving live landscape fallback reports. */
        const unsigned compass_before_mag = m.compass_count;
        {
            uint8_t pl[64], qmi[384];
            SpbOut o;
            spb_out(&o, pl, sizeof(pl));
            spb_put_fixed32(&o, 1, f2bits(220.0f));
            spb_put_fixed32(&o, 1, f2bits(60.0f));
            spb_put_fixed32(&o, 1, f2bits(420.0f));
            spb_put_varint(&o, 2, 3);
            uint32_t qlen = build_qmi_report(qmi, 90, &suids[2], SSC_MSG_SENSOR_EVENT, 20000000ull, pl, spb_done(&o));
            sns_core_receive(core, 1u + PaganiSensorMag, 7u, 42u, qmi, qlen);
        }
        CHECK(m.compass_count == compass_before_mag + 1u);
        const double land_grav[3] = {-1.0, 0.0, 0.0};
        verify_locationd_report(m.last_report, (double)m.last_heading_mdeg / 1000.0, land_grav, 2.0);

        /* Advance 200 ms and tick -> rotv automatically restarts! */
        m.now_ms = 2250;
        sns_core_tick(core);
        cc_flush(&m);
        CHECK(core->rotv_bounces == 1u);
        CHECK(core->rotv_bounce_phase == 0);
        CHECK(core->sensor[PaganiSensorRotv].active == SNS_STREAMING);
        CHECK(m.config_count[PaganiSensorRotv] == 2u);

        /* Post-bounce: 2 dummy samples ignored, then 3 valid landscape samples un-quarantine rotv. */
        for (unsigned step = 0; step < 5u; ++step) {
            uint8_t pl[64], qmi[384];
            SpbOut o;
            spb_out(&o, pl, sizeof(pl));
            if (step < 2u) {
                spb_put_fixed32(&o, 1, f2bits(0.5f));
                spb_put_fixed32(&o, 1, f2bits(-0.5f));
                spb_put_fixed32(&o, 1, f2bits(-0.5f));
                spb_put_fixed32(&o, 1, f2bits(0.5f));
            } else {
                spb_put_fixed32(&o, 1, f2bits(0.0f));
                spb_put_fixed32(&o, 1, f2bits((float)(-sqrt(0.5))));
                spb_put_fixed32(&o, 1, f2bits(0.0f));
                spb_put_fixed32(&o, 1, f2bits((float)sqrt(0.5)));
            }
            spb_put_varint(&o, 2, 2);
            uint32_t qlen = build_qmi_report(qmi, (uint16_t)(100 + step), &suids[3],
                SSC_MSG_SENSOR_EVENT, 25000000ull + step * 384000ull, pl, spb_done(&o));
            sns_core_receive(core, 1u + PaganiSensorRotv, 7u, 42u, qmi, qlen);
        }
        CHECK(core->compass.rotv_stuck == 0 && core->compass.rotv_valid == 1);
        verify_locationd_report(m.last_report, (double)m.last_heading_mdeg / 1000.0, land_grav, 1.0);
        free(core);
    }

    /* 12. sns_compass_parse_mag_bias validation */
    {
        int32_t q[3] = {0, 0, 0};
        CHECK(sns_compass_parse_mag_bias("231720,25980,451080", q) == 1);
        CHECK(fabs((double)q[0] / 65536.0 - 231.720) < 0.002);
        CHECK(fabs((double)q[1] / 65536.0 - 25.980) < 0.002);
        CHECK(fabs((double)q[2] / 65536.0 - 451.080) < 0.002);
        CHECK(sns_compass_parse_mag_bias(" -12500 , +3400 , 0 ", q) == 1);
        CHECK(fabs((double)q[0] / 65536.0 - (-12.500)) < 0.002);
        CHECK(fabs((double)q[1] / 65536.0 - 3.400) < 0.002);
        CHECK(q[2] == 0);
        CHECK(sns_compass_parse_mag_bias("0,0,0", q) == 0);
        CHECK(q[0] == 0 && q[1] == 0 && q[2] == 0);
        CHECK(sns_compass_parse_mag_bias("", q) == -1);
        CHECK(sns_compass_parse_mag_bias("1,2", q) == -1);
        CHECK(sns_compass_parse_mag_bias("1,2,3,4", q) == -1);
        CHECK(sns_compass_parse_mag_bias("abc,0,0", q) == -1);
        CHECK(sns_compass_parse_mag_bias("99999999,0,0", q) == -1);
    }

    /* 13. Default SNS_COMPASS_MODE_FUSION: 8 flat headings + upright + tilted + face-down with Xiaomi 13 PCB bias
     *     while mag_cal=(0,0,0) acc=0 and bogus akm_rotv is locked at 286 deg! */
    {
        int32_t pcb_bias[3];
        CHECK(sns_compass_parse_mag_bias("231720,25980,451080", pcb_bias) == 1);
        const double H = 36.5;   /* Taipei horizontal field uT */
        const double V = 25.56;  /* Taipei downward dip ~35 deg */
        const uint32_t bogus_rotv[4] = {f2bits(0.0f), f2bits(0.0f), f2bits(0.602f), f2bits(0.799f)}; /* ~286 deg */
        const double flat_hdgs[] = {0.0, 45.0, 90.0, 135.0, 180.0, 225.0, 270.0, 315.0};
        for (unsigned idx = 0; idx < sizeof(flat_hdgs) / sizeof(flat_hdgs[0]); ++idx) {
            const double rad = flat_hdgs[idx] * 3.14159265358979323846 / 180.0;
            const double nx = -sin(rad), ny = cos(rad);
            const double bx = H * nx, by = H * ny, bz = -V;
            SnsCompass comp;
            sns_compass_init(&comp);
            CHECK(comp.compass_mode == SNS_COMPASS_MODE_FUSION);
            sns_compass_set_default_mag_bias(&comp, pcb_bias);
            int32_t acc[3] = {0, 0, -65536};
            int32_t zero_bias[3] = {0, 0, 0};
            int32_t raw_mag[3] = {
                pcb_bias[0] + (int32_t)llround(bx * 65536.0),
                pcb_bias[1] + (int32_t)llround(by * 65536.0),
                pcb_bias[2] + (int32_t)llround(bz * 65536.0),
            };
            sns_compass_set_accel(&comp, acc);
            sns_compass_set_mag_cal(&comp, zero_bias, 0);
            sns_compass_set_mag(&comp, raw_mag, 0);
            CHECK(sns_compass_set_rotv(&comp, bogus_rotv, 1) == 0);
            CHECK(comp.active_bias_src == SNS_COMPASS_BIAS_DEFAULT);
            int32_t cal_out[3];
            CHECK(sns_compass_apply_mag_bias(&comp, raw_mag, cal_out) == 1);
            CHECK(fabs((double)cal_out[0] / 65536.0 - bx) < 0.01);
            CHECK(fabs((double)cal_out[1] / 65536.0 - by) < 0.01);
            CHECK(fabs((double)cal_out[2] / 65536.0 - bz) < 0.01);
            uint8_t rep[SNS_COMPASS_REPORT_BYTES];
            int32_t hdg = -1;
            CHECK(sns_compass_build_report(&comp, rep, &hdg) == 0);
            verify_locationd_report(rep, flat_hdgs[idx], flat_grav, 0.5);
            CHECK(comp.last_mag_heading_mdeg >= 0);
            check_angle_close((double)comp.last_mag_heading_mdeg / 1000.0, flat_hdgs[idx], 0.5);
            CHECK(comp.last_rotv_heading_mdeg >= 0);
            check_angle_close((double)comp.last_rotv_heading_mdeg / 1000.0, 286.0, 2.0);
        }

        /* Upright portrait (+90 deg pitch) across N/E/S/W */
        for (unsigned idx = 0; idx < 4u; ++idx) {
            const double hdeg = idx * 90.0;
            const double rad = hdeg * 3.14159265358979323846 / 180.0;
            const double bx = -H * sin(rad), by = -V, bz = -H * cos(rad);
            SnsCompass comp;
            sns_compass_init(&comp);
            sns_compass_set_default_mag_bias(&comp, pcb_bias);
            int32_t acc[3] = {0, -65536, 0};
            int32_t raw_mag[3] = {
                pcb_bias[0] + (int32_t)llround(bx * 65536.0),
                pcb_bias[1] + (int32_t)llround(by * 65536.0),
                pcb_bias[2] + (int32_t)llround(bz * 65536.0),
            };
            sns_compass_set_accel(&comp, acc);
            sns_compass_set_mag(&comp, raw_mag, 0);
            uint8_t rep[SNS_COMPASS_REPORT_BYTES];
            int32_t hdg = -1;
            CHECK(sns_compass_build_report(&comp, rep, &hdg) == 0);
            verify_locationd_report(rep, hdeg, upright_grav, 0.5);
        }

        /* Face-down 180 deg flip (up = (0,0,-1), like try21b L4618) across N/E/S/W */
        const double facedown_grav[3] = {0.0, 0.0, 1.0};
        for (unsigned idx = 0; idx < 4u; ++idx) {
            const double hdeg = idx * 90.0;
            const double rad = hdeg * 3.14159265358979323846 / 180.0;
            const double bx = H * sin(rad), by = H * cos(rad), bz = +V;
            SnsCompass comp;
            sns_compass_init(&comp);
            sns_compass_set_default_mag_bias(&comp, pcb_bias);
            int32_t acc[3] = {0, 0, 65536};
            int32_t raw_mag[3] = {
                pcb_bias[0] + (int32_t)llround(bx * 65536.0),
                pcb_bias[1] + (int32_t)llround(by * 65536.0),
                pcb_bias[2] + (int32_t)llround(bz * 65536.0),
            };
            sns_compass_set_accel(&comp, acc);
            sns_compass_set_mag(&comp, raw_mag, 0);
            uint8_t rep[SNS_COMPASS_REPORT_BYTES];
            int32_t hdg = -1;
            CHECK(sns_compass_build_report(&comp, rep, &hdg) == 0);
            verify_locationd_report(rep, hdeg, facedown_grav, 0.5);
        }

        /* Real Xiaomi 13 tilted pose (080616 L5251: up = normalize(0.21, 0.44, 0.88)) */
        {
            double u[3] = {0.21, 0.44, 0.88};
            const double un = sqrt(u[0]*u[0] + u[1]*u[1] + u[2]*u[2]);
            u[0] /= un; u[1] /= un; u[2] /= un;
            const double rad = 6.3 * 3.14159265358979323846 / 180.0;
            double top_proj[3] = {-u[0]*u[1], 1.0 - u[1]*u[1], -u[2]*u[1]};
            const double tn = sqrt(top_proj[0]*top_proj[0] + top_proj[1]*top_proj[1] + top_proj[2]*top_proj[2]);
            top_proj[0] /= tn; top_proj[1] /= tn; top_proj[2] /= tn;
            double wt[3] = {
                u[1]*top_proj[2] - u[2]*top_proj[1],
                u[2]*top_proj[0] - u[0]*top_proj[2],
                u[0]*top_proj[1] - u[1]*top_proj[0],
            };
            double north[3] = {
                cos(rad)*top_proj[0] + sin(rad)*wt[0],
                cos(rad)*top_proj[1] + sin(rad)*wt[1],
                cos(rad)*top_proj[2] + sin(rad)*wt[2],
            };
            SnsCompass comp;
            sns_compass_init(&comp);
            sns_compass_set_default_mag_bias(&comp, pcb_bias);
            int32_t acc[3] = {
                (int32_t)llround(-u[0] * 65536.0),
                (int32_t)llround(-u[1] * 65536.0),
                (int32_t)llround(-u[2] * 65536.0),
            };
            int32_t raw_mag[3] = {
                pcb_bias[0] + (int32_t)llround((H * north[0] - V * u[0]) * 65536.0),
                pcb_bias[1] + (int32_t)llround((H * north[1] - V * u[1]) * 65536.0),
                pcb_bias[2] + (int32_t)llround((H * north[2] - V * u[2]) * 65536.0),
            };
            sns_compass_set_accel(&comp, acc);
            sns_compass_set_mag(&comp, raw_mag, 0);
            uint8_t rep[SNS_COMPASS_REPORT_BYTES];
            int32_t hdg = -1;
            CHECK(sns_compass_build_report(&comp, rep, &hdg) == 0);
            const double want_grav[3] = {-u[0], -u[1], -u[2]};
            verify_locationd_report(rep, 6.3, want_grav, 0.8);
        }
    }

    /* 14. Fusion CW/CCW 100 Hz gyro integration (ignoring frozen rotv), stationary noise rejection,
     *     linear accel shock gating, magnetic interference hold, and mag_cal acc=2 handoff */
    {
        int32_t pcb_bias[3];
        CHECK(sns_compass_parse_mag_bias("231720,25980,451080", pcb_bias) == 1);
        const double H = 36.5, V = 25.56;
        const uint32_t frozen_rotv[4] = {f2bits(0.0f), f2bits(0.0f), f2bits(0.602f), f2bits(0.799f)};
        int32_t flat_acc[3] = {0, 0, -65536};
        int32_t zero_gyro[3] = {0, 0, 0};
        uint8_t rep[SNS_COMPASS_REPORT_BYTES];
        int32_t hdg = -1;

        /* 14a: CW (-pi/2 rad/s) 0 -> 90 deg, then CCW (+pi/2 rad/s) 90 -> 0 deg while rotv stays frozen */
        SnsCompass comp;
        sns_compass_init(&comp);
        sns_compass_set_default_mag_bias(&comp, pcb_bias);
        sns_compass_set_accel(&comp, flat_acc);
        int32_t north_raw[3] = {pcb_bias[0], pcb_bias[1] + (int32_t)llround(H * 65536.0), pcb_bias[2] - (int32_t)llround(V * 65536.0)};
        sns_compass_set_mag(&comp, north_raw, 0);
        uint64_t ts = 19200000ull;
        sns_compass_set_gyro_ts(&comp, zero_gyro, ts);
        CHECK(sns_compass_set_rotv(&comp, frozen_rotv, 2) == 0);
        CHECK(sns_compass_build_report(&comp, rep, &hdg) == 0);
        check_angle_close((double)hdg / 1000.0, 0.0, 0.5);

        const int32_t cw_gyro[3] = {0, 0, (int32_t)llround((-3.14159265358979323846 / 2.0) * 65536.0)};
        for (unsigned step = 1; step <= 100u; ++step) {
            ts += 192000ull; /* 10 ms (100 Hz) */
            sns_compass_set_gyro_ts(&comp, cw_gyro, ts);
            if ((step % 2u) == 0u) {
                const double deg = (double)step * 0.9;
                const double rad = deg * 3.14159265358979323846 / 180.0;
                int32_t mraw[3] = {
                    pcb_bias[0] + (int32_t)llround(-H * sin(rad) * 65536.0),
                    pcb_bias[1] + (int32_t)llround(H * cos(rad) * 65536.0),
                    pcb_bias[2] - (int32_t)llround(V * 65536.0),
                };
                sns_compass_set_mag(&comp, mraw, 0);
                CHECK(sns_compass_set_rotv(&comp, frozen_rotv, 2) == 0);
                CHECK(sns_compass_build_report(&comp, rep, &hdg) == 0);
            }
        }
        CHECK(comp.fallback_gyro_steps == 100u);
        check_angle_close((double)hdg / 1000.0, 90.0, 2.5);

        const int32_t ccw_gyro[3] = {0, 0, (int32_t)llround((3.14159265358979323846 / 2.0) * 65536.0)};
        for (unsigned step = 1; step <= 100u; ++step) {
            ts += 192000ull;
            sns_compass_set_gyro_ts(&comp, ccw_gyro, ts);
            if ((step % 2u) == 0u) {
                const double deg = 90.0 - (double)step * 0.9;
                const double rad = deg * 3.14159265358979323846 / 180.0;
                int32_t mraw[3] = {
                    pcb_bias[0] + (int32_t)llround(-H * sin(rad) * 65536.0),
                    pcb_bias[1] + (int32_t)llround(H * cos(rad) * 65536.0),
                    pcb_bias[2] - (int32_t)llround(V * 65536.0),
                };
                sns_compass_set_mag(&comp, mraw, 0);
                CHECK(sns_compass_build_report(&comp, rep, &hdg) == 0);
            }
        }
        check_angle_close((double)hdg / 1000.0, 0.0, 2.5);

        /* 14b: Stationary noise rejection (+/- 2.5 uT oscillating mag noise -> < 0.5 deg jitter) */
        SnsCompass noisy;
        sns_compass_init(&noisy);
        sns_compass_set_default_mag_bias(&noisy, pcb_bias);
        sns_compass_set_accel(&noisy, flat_acc);
        const double base_rad = 35.0 * 3.14159265358979323846 / 180.0;
        int32_t base_m[3] = {
            pcb_bias[0] + (int32_t)llround(-H * sin(base_rad) * 65536.0),
            pcb_bias[1] + (int32_t)llround(H * cos(base_rad) * 65536.0),
            pcb_bias[2] - (int32_t)llround(V * 65536.0),
        };
        sns_compass_set_mag(&noisy, base_m, 0);
        uint64_t nts = 19200000ull;
        sns_compass_set_gyro_ts(&noisy, zero_gyro, nts);
        CHECK(sns_compass_build_report(&noisy, rep, &hdg) == 0);
        double min_h = 360.0, max_h = 0.0;
        for (unsigned step = 0; step < 200u; ++step) {
            nts += 384000ull;
            sns_compass_set_gyro_ts(&noisy, zero_gyro, nts);
            const double n = ((step & 1u) ? 2.5 : -2.5);
            int32_t jm[3] = {
                base_m[0] + (int32_t)llround(n * 65536.0),
                base_m[1] - (int32_t)llround(n * 65536.0),
                base_m[2],
            };
            sns_compass_set_mag(&noisy, jm, 0);
            CHECK(sns_compass_build_report(&noisy, rep, &hdg) == 0);
            const double hd = (double)hdg / 1000.0;
            if (step >= 20u) {
                if (hd < min_h) min_h = hd;
                if (hd > max_h) max_h = hd;
            }
        }
        CHECK((max_h - min_h) < 0.5);
        check_angle_close((min_h + max_h) * 0.5, 35.0, 0.8);

        /* 14c: Linear accel shock (2.0 g sideways) must be gated out without wobbling tilt/heading */
        int32_t shock_acc[3] = {2 * 65536, 0, -65536};
        sns_compass_set_accel(&noisy, shock_acc);
        for (unsigned step = 0; step < 10u; ++step) {
            nts += 384000ull;
            sns_compass_set_gyro_ts(&noisy, zero_gyro, nts);
            sns_compass_set_mag(&noisy, base_m, 0);
            CHECK(sns_compass_build_report(&noisy, rep, &hdg) == 0);
        }
        verify_locationd_report(rep, 35.0, flat_grav, 1.0);
        sns_compass_set_accel(&noisy, flat_acc);

        /* 14d: Magnetic interference (+300 uT speaker magnet) while rotating CW 45 deg -> holds gyro yaw! */
        int32_t magnet_m[3] = {base_m[0] + 300 * 65536, base_m[1], base_m[2]};
        for (unsigned step = 0; step < 8u; ++step) {
            sns_compass_set_mag(&noisy, magnet_m, 0);
        }
        CHECK(noisy.active_bias_src == SNS_COMPASS_BIAS_NONE);
        for (unsigned step = 1; step <= 50u; ++step) {
            nts += 192000ull;
            sns_compass_set_gyro_ts(&noisy, cw_gyro, nts);
            if ((step % 2u) == 0u) {
                sns_compass_set_mag(&noisy, magnet_m, 0);
                CHECK(sns_compass_build_report(&noisy, rep, &hdg) == 0);
            }
        }
        CHECK(noisy.active_bias_src == SNS_COMPASS_BIAS_NONE);
        check_angle_close((double)hdg / 1000.0, 80.0, 3.0);

        /* 14e: Priority & handoff: acc=1 stays PCB while PCB valid; acc=2 switches to MAGCAL smoothly */
        SnsCompass handoff;
        sns_compass_init(&handoff);
        sns_compass_set_default_mag_bias(&handoff, pcb_bias);
        sns_compass_set_accel(&handoff, flat_acc);
        const double r45 = 45.0 * 3.14159265358979323846 / 180.0;
        int32_t shifted_bias[3] = {pcb_bias[0] + 5 * 65536, pcb_bias[1] - 4 * 65536, pcb_bias[2]};
        int32_t m_shifted[3] = {
            shifted_bias[0] + (int32_t)llround(-H * sin(r45) * 65536.0),
            shifted_bias[1] + (int32_t)llround(H * cos(r45) * 65536.0),
            shifted_bias[2] - (int32_t)llround(V * 65536.0),
        };
        for (unsigned i = 0; i < 8u; ++i) sns_compass_set_mag(&handoff, m_shifted, 0);
        uint64_t hts = 19200000ull;
        sns_compass_set_gyro_ts(&handoff, zero_gyro, hts);
        CHECK(sns_compass_build_report(&handoff, rep, &hdg) == 0);
        CHECK(handoff.active_bias_src == SNS_COMPASS_BIAS_DEFAULT);
        sns_compass_set_mag_cal(&handoff, shifted_bias, 1);
        CHECK(handoff.active_bias_src == SNS_COMPASS_BIAS_DEFAULT);
        sns_compass_set_mag_cal(&handoff, shifted_bias, 2);
        CHECK(handoff.active_bias_src == SNS_COMPASS_BIAS_MAGCAL);
        for (unsigned step = 0; step < 60u; ++step) {
            hts += 384000ull;
            sns_compass_set_gyro_ts(&handoff, zero_gyro, hts);
            sns_compass_set_mag(&handoff, m_shifted, 2);
            CHECK(sns_compass_build_report(&handoff, rep, &hdg) == 0);
        }
        check_angle_close((double)hdg / 1000.0, 45.0, 1.5);

        /* 14f: Nearby iPhone MagSafe ramp (+15..+217 uT) + poisoned acc=3 mag_cal (255.6, 48.4, 440.6) -> zero drag & clean recovery */
        SnsCompass immune;
        sns_compass_init(&immune);
        sns_compass_set_default_mag_bias(&immune, pcb_bias);
        sns_compass_set_accel(&immune, flat_acc);
        uint64_t its = 19200000ull;
        sns_compass_set_gyro_ts(&immune, zero_gyro, its);
        for (unsigned step = 0; step < 30u; ++step) {
            its += 384000ull;
            sns_compass_set_gyro_ts(&immune, zero_gyro, its);
            sns_compass_set_mag(&immune, base_m, 0);
            CHECK(sns_compass_build_report(&immune, rep, &hdg) == 0);
        }
        CHECK(immune.active_bias_src == SNS_COMPASS_BIAS_DEFAULT);
        check_angle_close((double)hdg / 1000.0, 35.0, 0.5);
        /* Slow approach (0..40 uT over 50 steps = 1 s), then close MagSafe spike (-143, -163, 0 uT) */
        for (unsigned step = 1; step <= 100u; ++step) {
            its += 384000ull;
            sns_compass_set_gyro_ts(&immune, zero_gyro, its);
            int32_t dist_m[3];
            if (step <= 50u) {
                const double s = (double)step / 50.0;
                dist_m[0] = base_m[0] - (int32_t)llround(30.0 * s * 65536.0);
                dist_m[1] = base_m[1] - (int32_t)llround(25.0 * s * 65536.0);
                dist_m[2] = base_m[2];
            } else {
                dist_m[0] = base_m[0] - 143 * 65536;
                dist_m[1] = base_m[1] - 163 * 65536;
                dist_m[2] = base_m[2];
            }
            sns_compass_set_mag(&immune, dist_m, 0);
            CHECK(sns_compass_build_report(&immune, rep, &hdg) == 0);
            check_angle_close((double)hdg / 1000.0, 35.0, 2.0);
        }
        CHECK(immune.mag_disturbed == 1);
        CHECK(immune.mag_disturb_events >= 1u);
        /* Poisoned ADSP mag_cal from real compass-fix-v1-user L7578 arrives with acc=3 */
        int32_t poisoned_bias[3];
        CHECK(sns_compass_parse_mag_bias("255600,48420,440580", poisoned_bias) == 1);
        sns_compass_set_mag_cal(&immune, poisoned_bias, 3);
        CHECK(immune.mag_cal_polluted == 1);
        CHECK(immune.mag_cal_rejected >= 1u);
        /* Remove nearby iPhone: after 1.5 s (75 clean samples), clean PCB bias resumes with <1.5 deg error */
        for (unsigned step = 0; step < 90u; ++step) {
            its += 384000ull;
            sns_compass_set_gyro_ts(&immune, zero_gyro, its);
            sns_compass_set_mag(&immune, base_m, 0);
            CHECK(sns_compass_build_report(&immune, rep, &hdg) == 0);
            check_angle_close((double)hdg / 1000.0, 35.0, 2.0);
        }
        CHECK(immune.mag_disturbed == 0);
        CHECK(immune.active_bias_src == SNS_COMPASS_BIAS_DEFAULT);
        verify_locationd_report(rep, 35.0, flat_grav, 1.5);

        /* 14g: Constant-norm slow lateral magnetic pull (35 -> 80 deg over 2 s, |B| and B_up unchanged)
         *      -> 3D world-frame vec_err gate catches it on sample #1 and holds yaw with <0.5 deg error */
        SnsCompass lat;
        sns_compass_init(&lat);
        sns_compass_set_default_mag_bias(&lat, pcb_bias);
        sns_compass_set_accel(&lat, flat_acc);
        uint64_t lts = 19200000ull;
        sns_compass_set_gyro_ts(&lat, zero_gyro, lts);
        for (unsigned step = 0; step < 30u; ++step) {
            lts += 384000ull;
            sns_compass_set_gyro_ts(&lat, zero_gyro, lts);
            sns_compass_set_mag(&lat, base_m, 0);
            CHECK(sns_compass_build_report(&lat, rep, &hdg) == 0);
        }
        for (unsigned step = 1; step <= 200u; ++step) {
            lts += 384000ull;
            sns_compass_set_gyro_ts(&lat, zero_gyro, lts);
            const double frac = (step <= 100u) ? ((double)step / 100.0) : 1.0;
            const double ang = (35.0 + 45.0 * frac) * 3.14159265358979323846 / 180.0;
            int32_t lm[3] = {
                pcb_bias[0] + (int32_t)llround(-H * sin(ang) * 65536.0),
                pcb_bias[1] + (int32_t)llround(H * cos(ang) * 65536.0),
                pcb_bias[2] - (int32_t)llround(V * 65536.0),
            };
            sns_compass_set_mag(&lat, lm, 0);
            CHECK(sns_compass_build_report(&lat, rep, &hdg) == 0);
            check_angle_close((double)hdg / 1000.0, 35.0, 1.5);
        }
        CHECK(lat.mag_disturbed == 1);
        CHECK((lat.mag_disturb_reason & 0x08u) != 0u);
        verify_locationd_report(rep, 35.0, flat_grav, 1.5);
        /* Remove lateral magnet: clears hold cleanly after 1.5 s (75 samples) */
        for (unsigned step = 0; step < 90u; ++step) {
            lts += 384000ull;
            sns_compass_set_gyro_ts(&lat, zero_gyro, lts);
            sns_compass_set_mag(&lat, base_m, 0);
            CHECK(sns_compass_build_report(&lat, rep, &hdg) == 0);
        }
        CHECK(lat.mag_disturbed == 0);
        check_angle_close((double)hdg / 1000.0, 35.0, 1.5);

        /* 14h: Quarantine blocks even small-drift (<12 uT) mag_cal during post-disturbance cooldown, then allows after clean cooldown */
        int32_t small_drift_bias[3] = {pcb_bias[0] + 6 * 65536, pcb_bias[1] - 5 * 65536, pcb_bias[2]}; /* ~7.8 uT <= 12 uT */
        CHECK(immune.mag_cal_quarantine > 0u);
        sns_compass_set_mag_cal(&immune, small_drift_bias, 3);
        CHECK(immune.mag_cal_polluted == 1);
        CHECK(immune.active_bias_src == SNS_COMPASS_BIAS_DEFAULT);
        for (unsigned step = 0; step < 340u; ++step) {
            its += 384000ull;
            sns_compass_set_gyro_ts(&immune, zero_gyro, its);
            sns_compass_set_mag(&immune, base_m, 0);
        }
        CHECK(immune.mag_cal_quarantine == 0u);
        sns_compass_set_mag_cal(&immune, shifted_bias, 3);
        CHECK(immune.mag_cal_polluted == 0);
        CHECK(immune.active_bias_src == SNS_COMPASS_BIAS_MAGCAL);

        /* 14i: Room-change re-anchor: steady heading-consistent +13.4 uT Earth field shift re-anchors after ~4.5 s */
        SnsCompass room;
        sns_compass_init(&room);
        sns_compass_set_default_mag_bias(&room, pcb_bias);
        sns_compass_set_accel(&room, flat_acc);
        uint64_t rts = 19200000ull;
        sns_compass_set_gyro_ts(&room, zero_gyro, rts);
        for (unsigned step = 0; step < 30u; ++step) {
            rts += 384000ull;
            sns_compass_set_gyro_ts(&room, zero_gyro, rts);
            sns_compass_set_mag(&room, base_m, 0);
            CHECK(sns_compass_build_report(&room, rep, &hdg) == 0);
        }
        /* Enter a room with |B| = 58.0 uT (H=47.5, V=33.3 -> +13.4 uT norm, +7.7 uT up) at same heading 35 deg */
        const double H2 = 47.5, V2 = 33.3;
        int32_t room_m[3] = {
            pcb_bias[0] + (int32_t)llround(-H2 * sin(base_rad) * 65536.0),
            pcb_bias[1] + (int32_t)llround(H2 * cos(base_rad) * 65536.0),
            pcb_bias[2] - (int32_t)llround(V2 * 65536.0),
        };
        for (unsigned step = 0; step < 250u; ++step) {
            rts += 384000ull;
            sns_compass_set_gyro_ts(&room, zero_gyro, rts);
            sns_compass_set_mag(&room, room_m, 0);
            CHECK(sns_compass_build_report(&room, rep, &hdg) == 0);
        }
        CHECK(room.mag_disturbed == 0);
        CHECK(room.active_bias_src == SNS_COMPASS_BIAS_DEFAULT);
        check_angle_close((double)hdg / 1000.0, 35.0, 1.5);

        /* 14j: Anti-frog-boiling + 20-second side-by-side iPhone hold + clean FuxiSensorMag & CMDeviceMotion outputs */
        SnsCompass hold;
        sns_compass_init(&hold);
        sns_compass_set_default_mag_bias(&hold, pcb_bias);
        sns_compass_set_accel(&hold, flat_acc);
        uint64_t fts = 19200000ull;
        sns_compass_set_gyro_ts(&hold, zero_gyro, fts);
        for (unsigned step = 0; step < 120u; ++step) {
            fts += 192000ull; /* 100 Hz gyro */
            sns_compass_set_gyro_ts(&hold, zero_gyro, fts);
            if ((step & 1u) == 0u) {
                sns_compass_set_mag(&hold, base_m, 0);
                CHECK(sns_compass_build_report(&hold, rep, &hdg) == 0);
            }
        }
        CHECK(hold.fallback_gyro_steps >= 100u);
        check_angle_close((double)hdg / 1000.0, 35.0, 0.5);
        /* Slowly creep lateral field 35 -> 95 deg over 6 s (300 mag steps), then hold 20 s (1000 mag steps)! */
        for (unsigned step = 1; step <= 1300u; ++step) {
            fts += 384000ull;
            sns_compass_set_gyro_ts(&hold, zero_gyro, fts);
            const double frac = (step <= 300u) ? ((double)step / 300.0) : 1.0;
            const double ang = (35.0 + 60.0 * frac) * 3.14159265358979323846 / 180.0;
            const double boost = (step > 500u && step <= 900u) ? 180.0 : 0.0;
            int32_t dm[3] = {
                pcb_bias[0] + (int32_t)llround((-H * sin(ang) + boost) * 65536.0),
                pcb_bias[1] + (int32_t)llround((H * cos(ang) - boost * 0.5) * 65536.0),
                pcb_bias[2] - (int32_t)llround(V * 65536.0),
            };
            sns_compass_set_mag(&hold, dm, 0);
            /* FuxiSensorMag path: MUST NEVER jump by +507 uT PCB bias or leak +180 uT magnet spike! */
            int32_t out_mag[3];
            CHECK(sns_compass_apply_mag_bias(&hold, dm, out_mag) == 1);
            const double mx = (double)out_mag[0] / 65536.0;
            const double my = (double)out_mag[1] / 65536.0;
            const double mz = (double)out_mag[2] / 65536.0;
            const double mn = sqrt(mx * mx + my * my + mz * mz);
            CHECK(mn >= 35.0 && mn <= 55.0);
            CHECK(sns_compass_build_report(&hold, rep, &hdg) == 0);
            check_angle_close((double)hdg / 1000.0, 35.0, 0.5);
            /* CMDeviceMotionReport uncal (0x10) and bias (0x4c) must also stay spike-free! */
            const double b0 = (double)bits2f(rd32(rep + 0x4c));
            const double b1 = (double)bits2f(rd32(rep + 0x50));
            const double b2 = (double)bits2f(rd32(rep + 0x54));
            CHECK(fabs(b0 - 231.720) < 0.2);
            CHECK(fabs(b1 - 25.980) < 0.2);
            CHECK(fabs(b2 - 451.080) < 0.2);
            const double cx = (double)bits2f(rd32(rep + 0x10)) - b0;
            const double cy = (double)bits2f(rd32(rep + 0x14)) - b1;
            const double cz = (double)bits2f(rd32(rep + 0x18)) - b2;
            const double cn = sqrt(cx * cx + cy * cy + cz * cz);
            CHECK(cn >= 35.0 && cn <= 55.0);
        }
        CHECK(hold.mag_disturbed == 1);
        /* Remove iPhone -> clean recovery within 1.5 s with <0.5 deg error */
        for (unsigned step = 0; step < 90u; ++step) {
            fts += 384000ull;
            sns_compass_set_gyro_ts(&hold, zero_gyro, fts);
            sns_compass_set_mag(&hold, base_m, 0);
            CHECK(sns_compass_build_report(&hold, rep, &hdg) == 0);
            check_angle_close((double)hdg / 1000.0, 35.0, 0.5);
        }
        CHECK(hold.mag_disturbed == 0);
        CHECK(hold.active_bias_src == SNS_COMPASS_BIAS_DEFAULT);

        /* 14k: Rotate 90 deg CW while nearby iPhone injects +200 uT spike -> gyro tracks 35->125 deg,
         *      synthetic FuxiSensorMag rotates cleanly with gyro, and removing iPhone resumes smoothly */
        for (unsigned step = 1; step <= 100u; ++step) {
            fts += 192000ull; /* 10 ms */
            sns_compass_set_gyro_ts(&hold, cw_gyro, fts);
            if ((step % 2u) == 0u) {
                int32_t spike_m[3] = {base_m[0] + 200 * 65536, base_m[1] - 150 * 65536, base_m[2]};
                sns_compass_set_mag(&hold, spike_m, 0);
                int32_t out_mag[3];
                CHECK(sns_compass_apply_mag_bias(&hold, spike_m, out_mag) == 1);
                const double mn = sqrt(
                    pow((double)out_mag[0] / 65536.0, 2.0) +
                    pow((double)out_mag[1] / 65536.0, 2.0) +
                    pow((double)out_mag[2] / 65536.0, 2.0));
                CHECK(mn >= 40.0 && mn <= 50.0);
                CHECK(sns_compass_build_report(&hold, rep, &hdg) == 0);
            }
        }
        CHECK(hold.mag_disturbed == 1);
        check_angle_close((double)hdg / 1000.0, 125.0, 2.5);
        const double rad125 = 125.0 * 3.14159265358979323846 / 180.0;
        int32_t m125[3] = {
            pcb_bias[0] + (int32_t)llround(-H * sin(rad125) * 65536.0),
            pcb_bias[1] + (int32_t)llround(H * cos(rad125) * 65536.0),
            pcb_bias[2] - (int32_t)llround(V * 65536.0),
        };
        for (unsigned step = 0; step < 90u; ++step) {
            fts += 384000ull;
            sns_compass_set_gyro_ts(&hold, zero_gyro, fts);
            sns_compass_set_mag(&hold, m125, 0);
            CHECK(sns_compass_build_report(&hold, rep, &hdg) == 0);
        }
        CHECK(hold.mag_disturbed == 0);
        check_angle_close((double)hdg / 1000.0, 125.0, 2.5);

        /* 14l: Booted 40 deg off near iron, then walked into clean uniform Earth field and rotated >=90 deg
         *      -> uniform-field rotation-verified relock recovers true North cleanly */
        SnsCompass relock;
        sns_compass_init(&relock);
        sns_compass_set_default_mag_bias(&relock, pcb_bias);
        sns_compass_set_accel(&relock, flat_acc);
        uint64_t qts = 19200000ull;
        sns_compass_set_gyro_ts(&relock, zero_gyro, qts);
        /* Boot with iron deflecting heading to 40 deg while true physical orientation is 0 deg */
        const double rad40 = 40.0 * 3.14159265358979323846 / 180.0;
        int32_t m_boot_iron[3] = {
            pcb_bias[0] + (int32_t)llround(-H * sin(rad40) * 65536.0),
            pcb_bias[1] + (int32_t)llround(H * cos(rad40) * 65536.0),
            pcb_bias[2] - (int32_t)llround(V * 65536.0),
        };
        for (unsigned step = 0; step < 30u; ++step) {
            qts += 384000ull;
            sns_compass_set_gyro_ts(&relock, zero_gyro, qts);
            sns_compass_set_mag(&relock, m_boot_iron, 0);
            CHECK(sns_compass_build_report(&relock, rep, &hdg) == 0);
        }
        check_angle_close((double)hdg / 1000.0, 40.0, 1.0);
        /* Move to clean Earth field (true heading 0 deg) and rotate 120 deg CW over 6 s (20 deg/s) */
        const double rate20 = -20.0 * 3.14159265358979323846 / 180.0;
        int32_t slow_cw_gyro[3] = {0, 0, (int32_t)llround(rate20 * 65536.0)};
        for (unsigned step = 1; step <= 360u; ++step) {
            qts += 384000ull; /* 20 ms -> 0.4 deg/step -> 144 deg total over 7.2 s */
            sns_compass_set_gyro_ts(&relock, slow_cw_gyro, qts);
            const double true_deg = (double)step * 0.4;
            const double trad = true_deg * 3.14159265358979323846 / 180.0;
            int32_t m_clean[3] = {
                pcb_bias[0] + (int32_t)llround(-H * sin(trad) * 65536.0),
                pcb_bias[1] + (int32_t)llround(H * cos(trad) * 65536.0),
                pcb_bias[2] - (int32_t)llround(V * 65536.0),
            };
            sns_compass_set_mag(&relock, m_clean, 0);
            CHECK(sns_compass_build_report(&relock, rep, &hdg) == 0);
        }
        CHECK(relock.mag_disturbed == 0);
        check_angle_close((double)hdg / 1000.0, 144.0, 5.0);

        /* 14m: Handheld jittering phone (gyro = +-0.08 rad/s, NOT stationary!) + slow 4 s iso-norm approaching iPhone
         *      + 15 s hold -> 3D world-frame vec_err (6.5 uT) + 8 deg yaw gate keep error < 2.5 deg! */
        SnsCompass hand;
        sns_compass_init(&hand);
        sns_compass_set_default_mag_bias(&hand, pcb_bias);
        sns_compass_set_accel(&hand, flat_acc);
        uint64_t jts = 19200000ull;
        sns_compass_set_gyro_ts(&hand, zero_gyro, jts);
        for (unsigned step = 0; step < 30u; ++step) {
            jts += 384000ull;
            sns_compass_set_gyro_ts(&hand, zero_gyro, jts);
            sns_compass_set_mag(&hand, base_m, 0);
            CHECK(sns_compass_build_report(&hand, rep, &hdg) == 0);
        }
        /* Handheld tremor oscillates +-0.08 rad/s (exceeds kGyroStillL1Q16=0.06 rad/s) while magnet creeps 35 -> 85 deg */
        for (unsigned step = 1; step <= 950u; ++step) {
            jts += 384000ull;
            const double w = (step & 1u) ? 0.08 : -0.08;
            int32_t jgyro[3] = {0, 0, (int32_t)llround(w * 65536.0)};
            sns_compass_set_gyro_ts(&hand, jgyro, jts);
            const double frac = (step <= 200u) ? ((double)step / 200.0) : 1.0;
            const double ang = (35.0 + 50.0 * frac) * 3.14159265358979323846 / 180.0;
            int32_t hm[3] = {
                pcb_bias[0] + (int32_t)llround(-H * sin(ang) * 65536.0),
                pcb_bias[1] + (int32_t)llround(H * cos(ang) * 65536.0),
                pcb_bias[2] - (int32_t)llround(V * 65536.0),
            };
            sns_compass_set_mag(&hand, hm, 0);
            CHECK(sns_compass_build_report(&hand, rep, &hdg) == 0);
            check_angle_close((double)hdg / 1000.0, 35.0, 2.5);
        }
        CHECK(hand.mag_disturbed == 1);

        /* 14n: Stale remagnetized PCB bias (+48 uT Y shift, exact compass-fix-v3-user values) ->
         *      verified acc=3 mag_cal promotes cleanly, recovers true 55 deg heading, and survives transient acc=0 drop */
        SnsCompass remag;
        sns_compass_init(&remag);
        sns_compass_set_default_mag_bias(&remag, pcb_bias);
        sns_compass_set_accel(&remag, flat_acc);
        int32_t true_remag_bias[3];
        CHECK(sns_compass_parse_mag_bias("240660,73260,447600", true_remag_bias) == 1);
        int32_t early_remag_bias[3];
        CHECK(sns_compass_parse_mag_bias("240300,71460,447480", early_remag_bias) == 1);
        const double H_desk = 22.0, V_desk = 26.0;
        const double rad55 = 55.0 * 3.14159265358979323846 / 180.0;
        int32_t desk_m[3] = {
            true_remag_bias[0] + (int32_t)llround(-H_desk * sin(rad55) * 65536.0),
            true_remag_bias[1] + (int32_t)llround(H_desk * cos(rad55) * 65536.0),
            true_remag_bias[2] - (int32_t)llround(V_desk * 65536.0),
        };
        uint64_t rts2 = 19200000ull;
        sns_compass_set_gyro_ts(&remag, zero_gyro, rts2);
        for (unsigned step = 0; step < 30u; ++step) {
            rts2 += 384000ull;
            sns_compass_set_gyro_ts(&remag, zero_gyro, rts2);
            sns_compass_set_mag(&remag, desk_m, 0);
            CHECK(sns_compass_build_report(&remag, rep, &hdg) == 0);
        }
        /* Stale pcb_bias produces H ~ 60.6 uT > 58 uT => rejected by in_earth, earth_ref never bootstrapped */
        CHECK(remag.earth_ref_count == 0u);
        CHECK(remag.mag_disturbed == 1);
        /* ADSP mag_cal converges to acc=3 (240.30, 71.46, 447.48) -> (240.66, 73.26, 447.60) */
        sns_compass_set_mag_cal(&remag, early_remag_bias, 3);
        for (unsigned step = 0; step < 20u; ++step) {
            rts2 += 384000ull;
            sns_compass_set_gyro_ts(&remag, zero_gyro, rts2);
            sns_compass_set_mag(&remag, desk_m, 3);
            CHECK(sns_compass_build_report(&remag, rep, &hdg) == 0);
        }
        sns_compass_set_mag_cal(&remag, true_remag_bias, 3);
        for (unsigned step = 0; step < 120u; ++step) {
            rts2 += 384000ull;
            sns_compass_set_gyro_ts(&remag, zero_gyro, rts2);
            sns_compass_set_mag(&remag, desk_m, 3);
            CHECK(sns_compass_build_report(&remag, rep, &hdg) == 0);
        }
        CHECK(remag.has_trusted_mag_cal == 1);
        CHECK(remag.mag_disturbed == 0);
        CHECK(remag.active_bias_src == SNS_COMPASS_BIAS_MAGCAL);
        check_angle_close((double)hdg / 1000.0, 55.0, 2.0);
        /* Nearby MagSafe hits and ADSP drops mag_cal to acc=0 -> trusted_mag_cal_bias MUST be preserved! */
        for (unsigned step = 0; step < 50u; ++step) {
            rts2 += 384000ull;
            sns_compass_set_gyro_ts(&remag, zero_gyro, rts2);
            int32_t spike[3] = {desk_m[0] + 150 * 65536, desk_m[1] - 120 * 65536, desk_m[2]};
            sns_compass_set_mag(&remag, spike, 0);
            if (step == 10u) {
                sns_compass_set_mag_cal(&remag, true_remag_bias, 0);
            }
            CHECK(sns_compass_build_report(&remag, rep, &hdg) == 0);
            check_angle_close((double)hdg / 1000.0, 55.0, 2.0);
        }
        CHECK(remag.has_trusted_mag_cal == 1);
        for (unsigned step = 0; step < 90u; ++step) {
            rts2 += 384000ull;
            sns_compass_set_gyro_ts(&remag, zero_gyro, rts2);
            sns_compass_set_mag(&remag, desk_m, 3);
            CHECK(sns_compass_build_report(&remag, rep, &hdg) == 0);
        }
        CHECK(remag.mag_disturbed == 0);
        CHECK(remag.active_bias_src == SNS_COMPASS_BIAS_MAGCAL);
        check_angle_close((double)hdg / 1000.0, 55.0, 2.0);

        /* 14o: Mid-session permanent chassis remagnetization (+48 uT AFTER clean baseline) ->
         *      rejected while stationary, then mathematically proven and promoted during >=60 deg clean rotation */
        SnsCompass midremag;
        sns_compass_init(&midremag);
        sns_compass_set_default_mag_bias(&midremag, pcb_bias);
        sns_compass_set_accel(&midremag, flat_acc);
        uint64_t mts2 = 19200000ull;
        sns_compass_set_gyro_ts(&midremag, zero_gyro, mts2);
        for (unsigned step = 0; step < 30u; ++step) {
            mts2 += 384000ull;
            sns_compass_set_gyro_ts(&midremag, zero_gyro, mts2);
            sns_compass_set_mag(&midremag, base_m, 0);
            CHECK(sns_compass_build_report(&midremag, rep, &hdg) == 0);
        }
        CHECK(midremag.earth_ref_count >= 24u);
        check_angle_close((double)hdg / 1000.0, 35.0, 0.5);
        /* Chassis gets remagnetized to true_remag_bias while phone is at 35 deg; ADSP emits acc=3 */
        int32_t remag_35[3] = {
            true_remag_bias[0] + (int32_t)llround(-H * sin(base_rad) * 65536.0),
            true_remag_bias[1] + (int32_t)llround(H * cos(base_rad) * 65536.0),
            true_remag_bias[2] - (int32_t)llround(V * 65536.0),
        };
        sns_compass_set_mag(&midremag, remag_35, 0);
        CHECK(midremag.mag_disturbed == 1);
        sns_compass_set_mag_cal(&midremag, true_remag_bias, 3);
        /* Stationary for 2 s: MUST NOT promote while stationary (protects against stationary MagSafe) */
        for (unsigned step = 0; step < 100u; ++step) {
            mts2 += 384000ull;
            sns_compass_set_gyro_ts(&midremag, zero_gyro, mts2);
            sns_compass_set_mag(&midremag, remag_35, 3);
            CHECK(sns_compass_build_report(&midremag, rep, &hdg) == 0);
        }
        CHECK(midremag.has_trusted_mag_cal == 0);
        /* Rotate 90 deg CW over 3 s (30 deg/s) -> 3D B_world invariance proves true_remag_bias is body-fixed */
        const double rate30 = -30.0 * 3.14159265358979323846 / 180.0;
        int32_t gyro30[3] = {0, 0, (int32_t)llround(rate30 * 65536.0)};
        for (unsigned step = 1; step <= 180u; ++step) {
            mts2 += 384000ull;
            const int turning = (step <= 150u);
            sns_compass_set_gyro_ts(&midremag, turning ? gyro30 : zero_gyro, mts2);
            const double cur_deg = 35.0 + (turning ? ((double)step * 0.6) : 90.0);
            const double crad = cur_deg * 3.14159265358979323846 / 180.0;
            int32_t rm[3] = {
                true_remag_bias[0] + (int32_t)llround(-H * sin(crad) * 65536.0),
                true_remag_bias[1] + (int32_t)llround(H * cos(crad) * 65536.0),
                true_remag_bias[2] - (int32_t)llround(V * 65536.0),
            };
            sns_compass_set_mag(&midremag, rm, 3);
            CHECK(sns_compass_build_report(&midremag, rep, &hdg) == 0);
        }
        CHECK(midremag.has_trusted_mag_cal == 1);
        CHECK(midremag.mag_disturbed == 0);
        CHECK(midremag.active_bias_src == SNS_COMPASS_BIAS_MAGCAL);
        check_angle_close((double)hdg / 1000.0, 125.0, 3.5);

        /* 14p: 5-minute stationary BMI260 gyro bias ZUPT (+0.008, +0.003, -0.003 rad/s from v3 L3469) +
         *      +-2.5 uT AK0991x noise + single-sample 6.5 uT glitches -> <0.5 deg drift, 0 false dist trips,
         *      and immediate unlock for slow 3 deg/s + brisk 90 deg/s rotation */
        SnsCompass zupt;
        sns_compass_init(&zupt);
        sns_compass_set_default_mag_bias(&zupt, pcb_bias);
        sns_compass_set_accel(&zupt, flat_acc);
        int32_t biased_still_gyro[3] = {
            (int32_t)llround(0.008 * 65536.0),
            (int32_t)llround(0.003 * 65536.0),
            (int32_t)llround(-0.003 * 65536.0),
        };
        uint64_t zts = 19200000ull;
        sns_compass_set_gyro_ts(&zupt, biased_still_gyro, zts);
        for (unsigned step = 0; step < 30000u; ++step) {
            zts += 192000ull; /* 100 Hz gyro -> 300 s = 5 min */
            int32_t g_noisy[3] = {
                biased_still_gyro[0] + (((int)(step % 3u) - 1) * 32),
                biased_still_gyro[1] + (((int)((step + 1u) % 3u) - 1) * 32),
                biased_still_gyro[2] + (((int)((step + 2u) % 3u) - 1) * 32),
            };
            sns_compass_set_gyro_ts(&zupt, g_noisy, zts);
            if ((step & 1u) == 0u) {
                const unsigned mstep = step >> 1;
                const int glitch = (mstep >= 100u && (mstep % 200u) == 0u);
                const double nx = glitch ? 4.5 : (((int)(mstep % 5u) - 2) * 0.8);
                const double ny = glitch ? 4.5 : (((int)((mstep * 3u) % 5u) - 2) * 0.8);
                const double nz = ((int)((mstep * 7u) % 5u) - 2) * 0.6;
                int32_t nm[3] = {
                    base_m[0] + (int32_t)llround(nx * 65536.0),
                    base_m[1] + (int32_t)llround(ny * 65536.0),
                    base_m[2] + (int32_t)llround(nz * 65536.0),
                };
                sns_compass_set_mag(&zupt, nm, 3);
                CHECK(sns_compass_build_report(&zupt, rep, &hdg) == 0);
            }
        }
        CHECK(zupt.zupt_learn_cnt > 100u);
        CHECK(zupt.mag_disturb_events == 0u);
        CHECK(zupt.mag_disturbed == 0);
        check_angle_close((double)hdg / 1000.0, 35.0, 0.5);
        CHECK(fabs((double)bits2f(rd32(rep + 0x2c))) < 0.002);
        CHECK(fabs((double)bits2f(rd32(rep + 0x30))) < 0.002);
        CHECK(fabs((double)bits2f(rd32(rep + 0x34))) < 0.002);
        /* Slow 3 deg/s turn for 10 s (+30 deg) + brisk 90 deg/s turn for 1 s (+90 deg) -> 155 deg */
        const double rate3 = -3.0 * 3.14159265358979323846 / 180.0;
        const double rate90 = -90.0 * 3.14159265358979323846 / 180.0;
        for (unsigned step = 1; step <= 1100u; ++step) {
            zts += 192000ull; /* 10 ms */
            const double w = (step <= 1000u) ? rate3 : rate90;
            int32_t g_turn[3] = {
                biased_still_gyro[0],
                biased_still_gyro[1],
                biased_still_gyro[2] + (int32_t)llround(w * 65536.0),
            };
            sns_compass_set_gyro_ts(&zupt, g_turn, zts);
            if ((step & 1u) == 0u) {
                const double cur_deg = 35.0 + ((step <= 1000u)
                    ? ((double)step * 0.03)
                    : (30.0 + (double)(step - 1000u) * 0.9));
                const double crad = cur_deg * 3.14159265358979323846 / 180.0;
                int32_t tm[3] = {
                    pcb_bias[0] + (int32_t)llround(-H * sin(crad) * 65536.0),
                    pcb_bias[1] + (int32_t)llround(H * cos(crad) * 65536.0),
                    pcb_bias[2] - (int32_t)llround(V * 65536.0),
                };
                sns_compass_set_mag(&zupt, tm, 3);
                CHECK(sns_compass_build_report(&zupt, rep, &hdg) == 0);
            }
        }
        CHECK(zupt.mag_disturbed == 0);
        check_angle_close((double)hdg / 1000.0, 155.0, 3.0);

        /* 14q: Retroactive pose_hist 3D proof: user boots with mild 25 uT stale b_pcb (passes in_earth at boot),
         *      rotates 90 deg CW to calibrate, STOPS moving to read the screen, and ADSP emits acc=3 AFTER stopping ->
         *      retroactive 3D pose_hist immediately proves and promotes the true bias in 0.2 s without needing more rotation! */
        SnsCompass retro;
        sns_compass_init(&retro);
        sns_compass_set_default_mag_bias(&retro, pcb_bias);
        sns_compass_set_accel(&retro, flat_acc);
        int32_t mild_remag[3] = {pcb_bias[0] + 22 * 65536, pcb_bias[1] + 12 * 65536, pcb_bias[2]}; /* ~25 uT drift */
        uint64_t rpts = 19200000ull;
        sns_compass_set_gyro_ts(&retro, zero_gyro, rpts);
        /* At heading 20 deg, mild_remag still yields valid Earth norm/H against stale pcb_bias -> earth_ref bootstraps! */
        const double r20 = 20.0 * 3.14159265358979323846 / 180.0;
        int32_t m_r20[3] = {
            mild_remag[0] + (int32_t)llround(-H_desk * sin(r20) * 65536.0),
            mild_remag[1] + (int32_t)llround(H_desk * cos(r20) * 65536.0),
            mild_remag[2] - (int32_t)llround(V_desk * 65536.0),
        };
        for (unsigned step = 0; step < 30u; ++step) {
            rpts += 384000ull;
            sns_compass_set_gyro_ts(&retro, zero_gyro, rpts);
            sns_compass_set_mag(&retro, m_r20, 0);
            CHECK(sns_compass_build_report(&retro, rep, &hdg) == 0);
        }
        CHECK(retro.earth_ref_count >= 24u);
        /* User rotates 90 deg CW (20 -> 110 deg) while ADSP has NOT yet emitted acc=3 */
        for (unsigned step = 1; step <= 150u; ++step) {
            rpts += 384000ull;
            sns_compass_set_gyro_ts(&retro, gyro30, rpts);
            const double cur_deg = 20.0 + (double)step * 0.6;
            const double crad = cur_deg * 3.14159265358979323846 / 180.0;
            int32_t rm[3] = {
                mild_remag[0] + (int32_t)llround(-H_desk * sin(crad) * 65536.0),
                mild_remag[1] + (int32_t)llround(H_desk * cos(crad) * 65536.0),
                mild_remag[2] - (int32_t)llround(V_desk * 65536.0),
            };
            sns_compass_set_mag(&retro, rm, 0);
            CHECK(sns_compass_build_report(&retro, rep, &hdg) == 0);
        }
        CHECK(retro.pose_hist_count >= 4u);
        /* Now user STOPS moving to look at Compass, and ADSP emits acc=3! */
        const double r110 = 110.0 * 3.14159265358979323846 / 180.0;
        int32_t m_r110[3] = {
            mild_remag[0] + (int32_t)llround(-H_desk * sin(r110) * 65536.0),
            mild_remag[1] + (int32_t)llround(H_desk * cos(r110) * 65536.0),
            mild_remag[2] - (int32_t)llround(V_desk * 65536.0),
        };
        sns_compass_set_mag_cal(&retro, mild_remag, 3);
        for (unsigned step = 0; step < 100u; ++step) {
            rpts += 384000ull;
            sns_compass_set_gyro_ts(&retro, zero_gyro, rpts);
            sns_compass_set_mag(&retro, m_r110, 3);
            CHECK(sns_compass_build_report(&retro, rep, &hdg) == 0);
        }
        CHECK(retro.has_trusted_mag_cal == 1);
        CHECK(retro.mag_disturbed == 0);
        CHECK(retro.active_bias_src == SNS_COMPASS_BIAS_MAGCAL);
        check_angle_close((double)hdg / 1000.0, 110.0, 2.5);

        /* 14r: Gyro stream pause (>1 s gap / reopen facing 180 deg opposite) resets stale yaw without false r=0x8,
         *      while preserving learned trusted_mag_cal_bias and ZUPT gyro_bias; plus sns_compass_apply_gyro_bias */
        int32_t g_cal_out[3];
        CHECK(sns_compass_apply_gyro_bias(&zupt, biased_still_gyro, g_cal_out) == 1);
        CHECK(abs(g_cal_out[0]) < 128 && abs(g_cal_out[1]) < 128 && abs(g_cal_out[2]) < 128);
        /* Pause gyro 5 seconds (96,000,000 ticks) and face South (215 deg = opposite of 35 deg) */
        const double r215 = 215.0 * 3.14159265358979323846 / 180.0;
        int32_t m_215[3] = {
            pcb_bias[0] + (int32_t)llround(-H * sin(r215) * 65536.0),
            pcb_bias[1] + (int32_t)llround(H * cos(r215) * 65536.0),
            pcb_bias[2] - (int32_t)llround(V * 65536.0),
        };
        zts += 96000000ull;
        sns_compass_set_gyro_ts(&zupt, biased_still_gyro, zts);
        CHECK(zupt.fallback_valid == 0);
        CHECK(zupt.zupt_learn_cnt > 100u); /* learned gyro bias preserved across pause */
        for (unsigned step = 0; step < 30u; ++step) {
            zts += 384000ull;
            sns_compass_set_gyro_ts(&zupt, biased_still_gyro, zts);
            sns_compass_set_mag(&zupt, m_215, 3);
            CHECK(sns_compass_build_report(&zupt, rep, &hdg) == 0);
        }
        CHECK(zupt.mag_disturbed == 0);
        check_angle_close((double)hdg / 1000.0, 215.0, 1.5);

        /* 14s: Exact compass-fix-v5-auto indoor desk replay (H=46..50.6 uT with +-2.5 uT AK0991x noise) +
         *      marginal earth-range 3-sample debounce vs instant hard trip + H=47 uT mag_cal candidate promotion */
        {
            SnsCompass desk;
            sns_compass_init(&desk);
            sns_compass_set_default_mag_bias(&desk, true_remag_bias);
            sns_compass_set_accel(&desk, flat_acc);
            uint64_t dts = 19200000ull;
            sns_compass_set_gyro_ts(&desk, zero_gyro, dts);
            int32_t first_hdg = -1;
            for (unsigned step = 0; step < 3000u; ++step) {
                dts += 384000ull; /* 50 Hz = 60 s */
                sns_compass_set_gyro_ts(&desk, zero_gyro, dts);
                /* Alternates around raw (201,49,420) <-> (198,46,419), H = 46.5..50.6 uT */
                const double dx = ((step % 5u) == 0u) ? -2.5 : (((int)(step % 3u) - 1) * 1.0);
                const double dy = ((step % 5u) == 0u) ? -2.0 : (((int)((step + 1u) % 3u) - 1) * 1.0);
                const double dz = ((int)(step % 3u) - 1) * 1.0;
                int32_t rm[3] = {
                    (int32_t)llround((200.5 + dx) * 65536.0),
                    (int32_t)llround((48.0 + dy) * 65536.0),
                    (int32_t)llround((420.0 + dz) * 65536.0),
                };
                sns_compass_set_mag(&desk, rm, 3);
                CHECK(sns_compass_build_report(&desk, rep, &hdg) == 0);
                if (step == 30u) {
                    first_hdg = hdg;
                }
            }
            CHECK(desk.mag_disturb_events == 0u);
            CHECK(desk.mag_disturbed == 0);
            CHECK(desk.active_bias_src == SNS_COMPASS_BIAS_DEFAULT);
            CHECK(first_hdg >= 0);
            check_angle_close((double)hdg / 1000.0, (double)first_hdg / 1000.0, 0.5);

            /* Marginal earth-range debounce: steady H=56 uT, 2 samples at H=60 uT (58..64 soft band) must NOT trip */
            SnsCompass edge;
            sns_compass_init(&edge);
            sns_compass_set_default_mag_bias(&edge, pcb_bias);
            sns_compass_set_accel(&edge, flat_acc);
            uint64_t ets = 19200000ull;
            sns_compass_set_gyro_ts(&edge, zero_gyro, ets);
            int32_t m56[3] = {pcb_bias[0] - 56 * 65536, pcb_bias[1], pcb_bias[2] - 24 * 65536};
            int32_t m60[3] = {pcb_bias[0] - 60 * 65536, pcb_bias[1], pcb_bias[2] - 24 * 65536};
            int32_t m68[3] = {pcb_bias[0] - 68 * 65536, pcb_bias[1], pcb_bias[2] - 24 * 65536};
            for (unsigned step = 0; step < 30u; ++step) {
                ets += 384000ull;
                sns_compass_set_gyro_ts(&edge, zero_gyro, ets);
                sns_compass_set_mag(&edge, m56, 3);
                CHECK(sns_compass_build_report(&edge, rep, &hdg) == 0);
            }
            CHECK(edge.mag_disturbed == 0 && edge.active_bias_src == SNS_COMPASS_BIAS_DEFAULT);
            /* 2 marginal samples -> soft-skipped, zero disturbance events */
            for (unsigned step = 0; step < 2u; ++step) {
                ets += 384000ull;
                sns_compass_set_gyro_ts(&edge, zero_gyro, ets);
                sns_compass_set_mag(&edge, m60, 3);
            }
            CHECK(edge.mag_disturbed == 0 && edge.mag_disturb_events == 0u);
            CHECK(edge.active_bias_src == SNS_COMPASS_BIAS_DEFAULT);
            /* Clean sample resets debounce counter */
            ets += 384000ull;
            sns_compass_set_gyro_ts(&edge, zero_gyro, ets);
            sns_compass_set_mag(&edge, m56, 3);
            CHECK(edge.earth_anom_cnt == 0u);
            /* 3 consecutive marginal samples -> trips r=0x1 on sample #3 */
            for (unsigned step = 0; step < 3u; ++step) {
                ets += 384000ull;
                sns_compass_set_gyro_ts(&edge, zero_gyro, ets);
                sns_compass_set_mag(&edge, m60, 3);
            }
            CHECK(edge.mag_disturbed == 1 && (edge.mag_disturb_reason & 0x01u) != 0u);
            /* Recover clean */
            for (unsigned step = 0; step < 90u; ++step) {
                ets += 384000ull;
                sns_compass_set_gyro_ts(&edge, zero_gyro, ets);
                sns_compass_set_mag(&edge, m56, 3);
            }
            CHECK(edge.mag_disturbed == 0);
            /* Single hard out-of-range sample (H=68 uT > 64 uT) -> trips immediately on sample #1 */
            ets += 384000ull;
            sns_compass_set_gyro_ts(&edge, zero_gyro, ets);
            sns_compass_set_mag(&edge, m68, 3);
            CHECK(edge.mag_disturbed == 1 && (edge.mag_disturb_reason & 0x01u) != 0u);

            /* Indoor H=47 uT mag_cal acc=3 candidate promotion via rotation */
            SnsCompass cand47;
            sns_compass_init(&cand47);
            sns_compass_set_default_mag_bias(&cand47, pcb_bias);
            sns_compass_set_accel(&cand47, flat_acc);
            uint64_t cts = 19200000ull;
            sns_compass_set_gyro_ts(&cand47, zero_gyro, cts);
            const double H47 = 47.0, V25 = 25.0;
            for (unsigned step = 0; step < 30u; ++step) {
                cts += 384000ull;
                sns_compass_set_gyro_ts(&cand47, zero_gyro, cts);
                int32_t m0[3] = {
                    pcb_bias[0] + (int32_t)llround(-H47 * sin(base_rad) * 65536.0),
                    pcb_bias[1] + (int32_t)llround(H47 * cos(base_rad) * 65536.0),
                    pcb_bias[2] - (int32_t)llround(V25 * 65536.0),
                };
                sns_compass_set_mag(&cand47, m0, 0);
                CHECK(sns_compass_build_report(&cand47, rep, &hdg) == 0);
            }
            sns_compass_set_mag_cal(&cand47, true_remag_bias, 3);
            for (unsigned step = 1; step <= 150u; ++step) {
                cts += 384000ull;
                sns_compass_set_gyro_ts(&cand47, gyro30, cts);
                const double crad = (35.0 + (double)step * 0.6) * 3.14159265358979323846 / 180.0;
                int32_t rm[3] = {
                    true_remag_bias[0] + (int32_t)llround(-H47 * sin(crad) * 65536.0),
                    true_remag_bias[1] + (int32_t)llround(H47 * cos(crad) * 65536.0),
                    true_remag_bias[2] - (int32_t)llround(V25 * 65536.0),
                };
                sns_compass_set_mag(&cand47, rm, 3);
                CHECK(sns_compass_build_report(&cand47, rep, &hdg) == 0);
            }
            CHECK(cand47.has_trusted_mag_cal == 1);
            CHECK(cand47.mag_disturbed == 0);
            CHECK(cand47.active_bias_src == SNS_COMPASS_BIAS_MAGCAL);
            check_angle_close((double)hdg / 1000.0, 125.0, 3.5);
        }

        /* 14t: Persisted hard-iron bias codec (magic/version/FNV-1a/bounds) + cold-boot stale-ADT override */
        {
            int32_t saved_b[3];
            CHECK(sns_compass_parse_mag_bias("225540,55800,451080", saved_b) == 1);
            int32_t stale_b[3];
            CHECK(sns_compass_parse_mag_bias("240660,73260,447600", stale_b) == 1);
            uint8_t blob[SNS_COMPASS_PERSIST_BYTES];
            CHECK(sns_compass_encode_persisted_bias(saved_b, blob) == 0);
            int32_t decoded[3] = {0, 0, 0};
            CHECK(sns_compass_decode_persisted_bias(blob, decoded) == 0);
            CHECK(decoded[0] == saved_b[0] && decoded[1] == saved_b[1] && decoded[2] == saved_b[2]);
            /* Reject zero, out-of-range, bad magic, bad version, corrupted checksum */
            int32_t zero_b[3] = {0, 0, 0};
            int32_t huge_b[3] = {2500 * 65536, 0, 0};
            CHECK(sns_compass_encode_persisted_bias(zero_b, blob) == -1);
            CHECK(sns_compass_encode_persisted_bias(huge_b, blob) == -1);
            CHECK(sns_compass_encode_persisted_bias(saved_b, blob) == 0);
            uint8_t bad_blob[SNS_COMPASS_PERSIST_BYTES];
            memcpy(bad_blob, blob, sizeof(blob));
            bad_blob[0] ^= 0x01u;
            CHECK(sns_compass_decode_persisted_bias(bad_blob, decoded) == -1);
            memcpy(bad_blob, blob, sizeof(blob));
            bad_blob[4] = 1u; /* stale v1 record rejected by v2 decoder */
            CHECK(sns_compass_decode_persisted_bias(bad_blob, decoded) == -1);
            memcpy(bad_blob, blob, sizeof(blob));
            bad_blob[12] ^= 0x80u;
            CHECK(sns_compass_decode_persisted_bias(bad_blob, decoded) == -1);
            CHECK(sns_compass_bias_drift_mut(saved_b, stale_b) > 20000u);

            /* Boot with stale ADT bias on real v6-user desk raw=(201,68,433) -> wrong ~99.5 deg;
             * loading persisted bias immediately corrects to true ~63.5 deg with zero disturbance! */
            SnsCompass pb;
            sns_compass_init(&pb);
            sns_compass_set_default_mag_bias(&pb, stale_b);
            sns_compass_set_accel(&pb, flat_acc);
            int32_t raw_v6_desk[3] = {201 * 65536, 68 * 65536, 433 * 65536};
            uint64_t pts = 19200000ull;
            for (unsigned step = 0; step < 30u; ++step) {
                pts += 384000ull;
                sns_compass_set_gyro_ts(&pb, zero_gyro, pts);
                sns_compass_set_mag(&pb, raw_v6_desk, 0);
                CHECK(sns_compass_build_report(&pb, rep, &hdg) == 0);
            }
            CHECK(hdg > 90000 && hdg < 110000); /* stale ADT bias gave wrong ~98 deg */
            int32_t p_out[3];
            CHECK(sns_compass_get_persistable_bias(&pb, p_out) == 0); /* not persistable before acc=3 */
            CHECK(sns_compass_load_persisted_mag_bias(&pb, decoded) == 1);
            for (unsigned step = 0; step < 30u; ++step) {
                pts += 384000ull;
                sns_compass_set_gyro_ts(&pb, zero_gyro, pts);
                sns_compass_set_mag(&pb, raw_v6_desk, 0);
                CHECK(sns_compass_build_report(&pb, rep, &hdg) == 0);
            }
            CHECK(pb.mag_disturbed == 0);
            CHECK(pb.active_bias_src == SNS_COMPASS_BIAS_DEFAULT);
            check_angle_close((double)hdg / 1000.0, 63.5, 2.5);
            /* After clean acc=3 adoption, persist is gated until handoff & post-promotion earth_ref complete */
            sns_compass_set_mag_cal(&pb, saved_b, 3);
            CHECK(sns_compass_get_persistable_bias(&pb, p_out) == 0);
            for (unsigned step = 0; step < 80u; ++step) {
                pts += 384000ull;
                sns_compass_set_gyro_ts(&pb, zero_gyro, pts);
                sns_compass_set_mag(&pb, raw_v6_desk, 3);
                CHECK(sns_compass_build_report(&pb, rep, &hdg) == 0);
            }
            CHECK(sns_compass_get_persistable_bias(&pb, p_out) == 1);
            CHECK(p_out[0] == saved_b[0] && p_out[1] == saved_b[1] && p_out[2] == saved_b[2]);
            CHECK(sns_compass_load_persisted_mag_bias(&pb, stale_b) == 0);
        }

        /* 14u: Cold-boot pure yaw-only 42 deg turn MUST NOT promote a Z-shifted bias (boot #342),
         *      while a 3D turn+tilt (>=35 deg rot & >=25 deg tilt) rapidly replaces stale default via
         *      cold_boot_proof, and post-trust 42 deg spoof remains blocked! */
        {
            int32_t stale_b[3];
            int32_t true_b[3];
            CHECK(sns_compass_parse_mag_bias("240660,73260,447600", stale_b) == 1);
            CHECK(sns_compass_parse_mag_bias("225540,55800,451080", true_b) == 1);
            int32_t z_bad_b[3] = {true_b[0], true_b[1], true_b[2] - 12 * 65536};
            SnsCompass cb;
            sns_compass_init(&cb);
            sns_compass_set_default_mag_bias(&cb, stale_b);
            sns_compass_set_accel(&cb, flat_acc);
            uint64_t cts = 19200000ull;
            sns_compass_set_gyro_ts(&cb, zero_gyro, cts);
            const double H_c = 28.0;
            const double V_c = 18.0;
            const double a0 = 60.0 * 3.14159265358979323846 / 180.0;
            int32_t m_a0[3] = {
                true_b[0] + (int32_t)llround(-H_c * sin(a0) * 65536.0),
                true_b[1] + (int32_t)llround(H_c * cos(a0) * 65536.0),
                true_b[2] - (int32_t)llround(V_c * 65536.0),
            };
            for (unsigned step = 0; step < 30u; ++step) {
                cts += 384000ull;
                sns_compass_set_gyro_ts(&cb, zero_gyro, cts);
                sns_compass_set_mag(&cb, m_a0, 0);
                CHECK(sns_compass_build_report(&cb, rep, &hdg) == 0);
            }
            CHECK(cb.earth_ref_count >= 24u);
            /* Turn 42 deg CW flat (zero tilt): z_bad_b MUST NOT promote via cold_boot_proof! */
            for (unsigned step = 1; step <= 70u; ++step) {
                cts += 384000ull;
                sns_compass_set_gyro_ts(&cb, gyro30, cts);
                const double crad = (60.0 + (double)step * 0.6) * 3.14159265358979323846 / 180.0;
                int32_t rm[3] = {
                    true_b[0] + (int32_t)llround(-H_c * sin(crad) * 65536.0),
                    true_b[1] + (int32_t)llround(H_c * cos(crad) * 65536.0),
                    true_b[2] - (int32_t)llround(V_c * 65536.0),
                };
                sns_compass_set_mag(&cb, rm, 0);
                CHECK(sns_compass_build_report(&cb, rep, &hdg) == 0);
            }
            CHECK(cb.pose_hist_count >= 3u);
            const double a42 = 102.0 * 3.14159265358979323846 / 180.0;
            const double bx42 = -H_c * sin(a42);
            const double by42 = H_c * cos(a42);
            const double bz42 = -V_c;
            int32_t m_a42[3] = {
                true_b[0] + (int32_t)llround(bx42 * 65536.0),
                true_b[1] + (int32_t)llround(by42 * 65536.0),
                true_b[2] + (int32_t)llround(bz42 * 65536.0),
            };
            sns_compass_set_mag_cal(&cb, z_bad_b, 3);
            for (unsigned step = 0; step < 25u; ++step) {
                cts += 384000ull;
                sns_compass_set_gyro_ts(&cb, zero_gyro, cts);
                sns_compass_set_mag(&cb, m_a42, 3);
                CHECK(sns_compass_build_report(&cb, rep, &hdg) == 0);
            }
            CHECK(cb.has_trusted_mag_cal == 0); /* yaw-only CANNOT promote unconstrained Z bias! */

            /* Now tilt +36 deg around body +X (picking phone up), giving >=25 deg 3D tilt diversity:
             * true_b passes 3D cold_boot_proof and promotes cleanly! */
            int32_t gyro_pitch[3] = {(int32_t)llround(0.5235987756 * 65536.0), 0, 0};
            for (unsigned step = 1; step <= 60u; ++step) {
                cts += 384000ull;
                const double prad = ((double)step * 0.6) * 3.14159265358979323846 / 180.0;
                int32_t p_acc[3] = {
                    0,
                    (int32_t)llround(-sin(prad) * 65536.0),
                    (int32_t)llround(-cos(prad) * 65536.0),
                };
                sns_compass_set_accel(&cb, p_acc);
                sns_compass_set_gyro_ts(&cb, gyro_pitch, cts);
                int32_t pm[3] = {
                    true_b[0] + (int32_t)llround(bx42 * 65536.0),
                    true_b[1] + (int32_t)llround((by42 * cos(prad) + bz42 * sin(prad)) * 65536.0),
                    true_b[2] + (int32_t)llround((-by42 * sin(prad) + bz42 * cos(prad)) * 65536.0),
                };
                sns_compass_set_mag(&cb, pm, 0);
                CHECK(sns_compass_build_report(&cb, rep, &hdg) == 0);
            }
            const double p36 = 36.0 * 3.14159265358979323846 / 180.0;
            int32_t acc36[3] = {
                0,
                (int32_t)llround(-sin(p36) * 65536.0),
                (int32_t)llround(-cos(p36) * 65536.0),
            };
            int32_t m_a42_p36[3] = {
                true_b[0] + (int32_t)llround(bx42 * 65536.0),
                true_b[1] + (int32_t)llround((by42 * cos(p36) + bz42 * sin(p36)) * 65536.0),
                true_b[2] + (int32_t)llround((-by42 * sin(p36) + bz42 * cos(p36)) * 65536.0),
            };
            sns_compass_set_accel(&cb, acc36);
            sns_compass_set_mag_cal(&cb, true_b, 3);
            for (unsigned step = 0; step < 90u; ++step) {
                cts += 384000ull;
                sns_compass_set_gyro_ts(&cb, zero_gyro, cts);
                sns_compass_set_mag(&cb, m_a42_p36, 3);
                CHECK(sns_compass_build_report(&cb, rep, &hdg) == 0);
            }
            CHECK(cb.has_trusted_mag_cal == 1);
            CHECK(cb.mag_disturbed == 0);
            CHECK(cb.active_bias_src == SNS_COMPASS_BIAS_MAGCAL);
            check_angle_close((double)hdg / 1000.0, 102.0, 2.5);
            sns_compass_set_accel(&cb, flat_acc);

            /* Post-trust negative check: after trusted_mag_cal is established, a 42 deg turn with a
             * disturbed 25 uT spoofed candidate MUST NOT promote via cold_boot_proof (requires 60 deg)! */
            int32_t spoof_b[3] = {true_b[0] + 25 * 65536, true_b[1], true_b[2]};
            int32_t dist_mag[3] = {m_a42[0] + 40 * 65536, m_a42[1], m_a42[2]};
            cts += 384000ull;
            sns_compass_set_gyro_ts(&cb, zero_gyro, cts);
            sns_compass_set_mag(&cb, dist_mag, 0);
            CHECK(cb.mag_disturbed == 1);
            cb.pose_hist_count = 0u;
            cb.pose_hist_pos = 0u;
            for (unsigned step = 1; step <= 70u; ++step) {
                cts += 384000ull;
                sns_compass_set_gyro_ts(&cb, gyro30, cts);
                const double crad = (102.0 + (double)step * 0.6) * 3.14159265358979323846 / 180.0;
                int32_t rm[3] = {
                    spoof_b[0] + (int32_t)llround(-H_c * sin(crad) * 65536.0),
                    spoof_b[1] + (int32_t)llround(H_c * cos(crad) * 65536.0),
                    spoof_b[2] - (int32_t)llround(V_c * 65536.0),
                };
                sns_compass_set_mag(&cb, rm, 0);
                CHECK(sns_compass_build_report(&cb, rep, &hdg) == 0);
            }
            const double a84 = 144.0 * 3.14159265358979323846 / 180.0;
            int32_t m_a84[3] = {
                spoof_b[0] + (int32_t)llround(-H_c * sin(a84) * 65536.0),
                spoof_b[1] + (int32_t)llround(H_c * cos(a84) * 65536.0),
                spoof_b[2] - (int32_t)llround(V_c * 65536.0),
            };
            sns_compass_set_mag_cal(&cb, spoof_b, 3);
            for (unsigned step = 0; step < 30u; ++step) {
                cts += 384000ull;
                sns_compass_set_gyro_ts(&cb, zero_gyro, cts);
                sns_compass_set_mag(&cb, m_a84, 3);
                CHECK(sns_compass_build_report(&cb, rep, &hdg) == 0);
            }
            CHECK(cb.trusted_mag_cal_bias[0] == true_b[0]); /* spoof blocked! */
        }

        /* 14v: Hand-to-desk 5.6 uT stationary field delta produces ZERO r=0x8 chatter (v6-user L7822/L8000),
         *      while a real 10.5 uT magnet spike still trips r=0x8 immediately on sample #1. */
        {
            SnsCompass h2d;
            sns_compass_init(&h2d);
            sns_compass_set_default_mag_bias(&h2d, pcb_bias);
            sns_compass_set_accel(&h2d, flat_acc);
            uint64_t hts = 19200000ull;
            sns_compass_set_gyro_ts(&h2d, zero_gyro, hts);
            const double h_hand = 30.7;
            const double v_hand = 18.4;
            const double rad65 = 65.0 * 3.14159265358979323846 / 180.0;
            int32_t m_hand[3] = {
                pcb_bias[0] + (int32_t)llround(-h_hand * sin(rad65) * 65536.0),
                pcb_bias[1] + (int32_t)llround(h_hand * cos(rad65) * 65536.0),
                pcb_bias[2] - (int32_t)llround(v_hand * 65536.0),
            };
            for (unsigned step = 0; step < 35u; ++step) {
                hts += 384000ull;
                sns_compass_set_gyro_ts(&h2d, zero_gyro, hts);
                sns_compass_set_mag(&h2d, m_hand, 3);
                CHECK(sns_compass_build_report(&h2d, rep, &hdg) == 0);
            }
            CHECK(h2d.earth_ref_count >= 24u && h2d.mag_disturb_events == 0u);
            /* Set down on desk: ~5.6 uT 3D vector delta (H=35.5 uT, V=15.5 uT, heading=67 deg) */
            const double rad67 = 67.0 * 3.14159265358979323846 / 180.0;
            int32_t m_desk[3] = {
                pcb_bias[0] + (int32_t)llround(-35.5 * sin(rad67) * 65536.0),
                pcb_bias[1] + (int32_t)llround(35.5 * cos(rad67) * 65536.0),
                pcb_bias[2] - (int32_t)llround(15.5 * 65536.0),
            };
            for (unsigned step = 0; step < 100u; ++step) {
                hts += 384000ull;
                sns_compass_set_gyro_ts(&h2d, zero_gyro, hts);
                sns_compass_set_mag(&h2d, m_desk, 3);
                CHECK(sns_compass_build_report(&h2d, rep, &hdg) == 0);
            }
            CHECK(h2d.mag_disturbed == 0);
            CHECK(h2d.mag_disturb_events == 0u);
            /* Real 10.5 uT magnet spike -> still trips r=0x8 on sample #1! */
            int32_t m_spike[3] = {m_hand[0] + 10 * 65536 + 32768, m_hand[1], m_hand[2]};
            hts += 384000ull;
            sns_compass_set_gyro_ts(&h2d, zero_gyro, hts);
            sns_compass_set_mag(&h2d, m_spike, 3);
            CHECK(h2d.mag_disturbed == 1 && (h2d.mag_disturb_reason & 0x08u) != 0u);
        }

        /* 14w: Replay boot #342 regressions:
         *  (a) acc=3 with small drift (<=15 uT) arriving during transient disturbance enters candidate
         *      queue instead of falling into the drift>15 uT black hole;
         *  (b) violent hand-swing gyro rate (>0.55 rad/s) cannot prematurely lock bogus earth_ref_up_mut;
         *  (c) fast initial handoff converges >95% of a 50 deg jump within 16 steps (0.32 s). */
        {
            SnsCompass r342;
            sns_compass_init(&r342);
            sns_compass_set_default_mag_bias(&r342, pcb_bias);
            sns_compass_set_accel(&r342, flat_acc);
            uint64_t rts = 19200000ull;
            sns_compass_set_gyro_ts(&r342, zero_gyro, rts);
            const double rad50 = 50.0 * 3.14159265358979323846 / 180.0;
            int32_t m_clean[3] = {
                pcb_bias[0] + (int32_t)llround(-30.0 * sin(rad50) * 65536.0),
                pcb_bias[1] + (int32_t)llround(30.0 * cos(rad50) * 65536.0),
                pcb_bias[2] - (int32_t)llround(22.0 * 65536.0),
            };
            /* Violent swing (1.8 rad/s) MUST NOT advance earth_ref_count during bootstrap */
            int32_t fast_swing[3] = {0, (int32_t)llround(1.8 * 65536.0), 0};
            for (unsigned step = 0; step < 30u; ++step) {
                rts += 384000ull;
                sns_compass_set_gyro_ts(&r342, fast_swing, rts);
                sns_compass_set_mag(&r342, m_clean, 0);
                CHECK(sns_compass_build_report(&r342, rep, &hdg) == 0);
            }
            CHECK(r342.earth_ref_count == 0u);
            /* Calm bootstrap locks earth_ref cleanly */
            for (unsigned step = 0; step < 30u; ++step) {
                rts += 384000ull;
                sns_compass_set_gyro_ts(&r342, zero_gyro, rts);
                sns_compass_set_mag(&r342, m_clean, 0);
                CHECK(sns_compass_build_report(&r342, rep, &hdg) == 0);
            }
            CHECK(r342.earth_ref_count >= 24u && r342.earth_ref_has_up == 1);
            /* Trip disturbance, then feed acc=3 with 10 uT drift (<=15 uT): MUST enter mag_cal_cand! */
            int32_t m_bump[3] = {m_clean[0] + 25 * 65536, m_clean[1], m_clean[2]};
            rts += 384000ull;
            sns_compass_set_gyro_ts(&r342, zero_gyro, rts);
            sns_compass_set_mag(&r342, m_bump, 0);
            CHECK(r342.mag_disturbed == 1);
            int32_t near_b[3] = {pcb_bias[0] + 8 * 65536, pcb_bias[1] - 6 * 65536, pcb_bias[2]}; /* 10 uT drift */
            sns_compass_set_mag_cal(&r342, near_b, 3);
            CHECK(r342.mag_cal_cand_valid == 1);
            CHECK(r342.mag_cal_cand_acc == 3);
            /* Verify fast initial handoff snaps a ~25 deg bias shift to within 1.5 deg in 16 steps (0.32 s)
             * (previous gain_den=16 left ~36% / ~9 deg residual error after 16 steps). */
            SnsCompass snap;
            sns_compass_init(&snap);
            int32_t shifted_def[3] = {
                pcb_bias[0] + 10 * 65536,
                pcb_bias[1] + 8 * 65536,
                pcb_bias[2],
            }; /* ~12.8 uT drift -> ~25 deg heading offset, within <=15 uT fast-adopt band */
            sns_compass_set_default_mag_bias(&snap, shifted_def);
            sns_compass_set_accel(&snap, flat_acc);
            uint64_t sts = 19200000ull;
            sns_compass_set_gyro_ts(&snap, zero_gyro, sts);
            for (unsigned step = 0; step < 30u; ++step) {
                sts += 384000ull;
                sns_compass_set_gyro_ts(&snap, zero_gyro, sts);
                sns_compass_set_mag(&snap, m_clean, 0);
                CHECK(sns_compass_build_report(&snap, rep, &hdg) == 0);
            }
            CHECK(hdg > 68000 && hdg < 88000); /* shifted default gives ~78 deg instead of true 50 deg */
            /* Promote true pcb_bias via clean acc=3 (triggers bias_handoff_steps on first report) with constant m_clean */
            sns_compass_set_mag_cal(&snap, pcb_bias, 3);
            for (unsigned step = 0; step < 16u; ++step) {
                sts += 384000ull;
                sns_compass_set_gyro_ts(&snap, zero_gyro, sts);
                sns_compass_set_mag(&snap, m_clean, 3);
                CHECK(sns_compass_build_report(&snap, rep, &hdg) == 0);
                if (step == 0u) {
                    CHECK(snap.bias_handoff_steps == 74u);
                }
            }
            check_angle_close((double)hdg / 1000.0, 50.0, 1.5);
        }
    }

    /* 15. End-to-end SnsCore default SNS_COMPASS_MODE_FUSION: calibrated FuxiSensorMag + 50 Hz mag-driven reports + zero rotv bounces */
    {
        SnsCore *core = (SnsCore *)calloc(1, sizeof(SnsCore));
        CHECK(core != NULL);
        struct compass_core_mock m;
        memset(&m, 0, sizeof(m));
        m.now_ms = 1000;
        m.core = core;
        SnsOps ops;
        memset(&ops, 0, sizeof(ops));
        ops.context = &m;
        ops.now_ms = cc_now;
        ops.send = cc_send;
        ops.trace = cc_trace;
        ops.sample = cc_sample;
        ops.compass = cc_compass;

        SnsConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.mode = 3u;
        cfg.orient = 1u;
        cfg.compass = 1u;
        cfg.compass_mode = SNS_COMPASS_MODE_FUSION;
        CHECK(sns_compass_parse_mag_bias("231720,25980,451080", cfg.mag_bias_q16) == 1);
        cfg.has_mag_bias = 1u;
        cfg.motion_hz = 50u;
        cfg.log = 2u;
        strcpy(cfg.type[PaganiSensorAccel], "accel");
        strcpy(cfg.type[PaganiSensorGyro], "gyro");
        strcpy(cfg.type[PaganiSensorMag], "mag");
        strcpy(cfg.type[PaganiSensorRotv], "akm_rotv");
        strcpy(cfg.type[PaganiSensorMagCal], "mag_cal");
        sns_core_init(core, &ops, &cfg);
        sns_core_server(core, 1, 7u, 42u, 1u, 1u);
        cc_flush(&m);

        const SscSuid suids[5] = {
            {0x3001ull, 0x4001ull}, {0x3002ull, 0x4002ull}, {0x3003ull, 0x4003ull},
            {0x3004ull, 0x4004ull}, {0x3005ull, 0x4005ull},
        };
        const char *names[5] = {"accel", "gyro", "mag", "akm_rotv", "mag_cal"};
        const unsigned kinds[5] = {
            PaganiSensorAccel, PaganiSensorGyro, PaganiSensorMag, PaganiSensorRotv, PaganiSensorMagCal,
        };
        for (unsigned idx = 0; idx < 5u; ++idx) {
            uint8_t sb[32], pl[96], qmi[384];
            SpbOut o;
            spb_out(&o, sb, sizeof(sb));
            spb_put_fixed64(&o, 1, suids[idx].low);
            spb_put_fixed64(&o, 2, suids[idx].high);
            uint32_t sblen = spb_done(&o);
            spb_out(&o, pl, sizeof(pl));
            spb_put_string(&o, 1, names[idx]);
            spb_put_bytes(&o, 2, sb, sblen);
            uint32_t qlen = build_qmi_report(qmi, (uint16_t)(10 + idx), &ssc_suid_lookup,
                SSC_MSG_SUID_EVENT, 1000, pl, spb_done(&o));
            sns_core_receive(core, SNS_CONN_LOOKUP, 7u, 42u, qmi, qlen);
            cc_flush(&m);
        }
        const float fusion_rates[2] = {50.0f, 100.0f};
        for (unsigned idx = 0; idx < 4u; ++idx) {
            uint8_t av[64], a_rates[80], st_d[16], st_v[24], a_st[32], pl[160], qmi[384];
            SpbOut o;
            spb_out(&o, av, sizeof(av));
            for (unsigned r = 0; r < 2u; ++r) {
                uint8_t d[16];
                SpbOut od;
                spb_out(&od, d, sizeof(d));
                spb_put_fixed32(&od, 3, f2bits(fusion_rates[r]));
                spb_put_bytes(&o, 1, d, spb_done(&od));
            }
            uint32_t avl = spb_done(&o);
            spb_out(&o, a_rates, sizeof(a_rates));
            spb_put_varint(&o, 1, SSC_ATTR_RATES);
            spb_put_bytes(&o, 2, av, avl);
            uint32_t arl = spb_done(&o);
            spb_out(&o, st_d, sizeof(st_d));
            spb_put_fixed64(&o, 4, SSC_STREAM_STREAMING);
            uint32_t stdl = spb_done(&o);
            spb_out(&o, st_v, sizeof(st_v));
            spb_put_bytes(&o, 1, st_d, stdl);
            uint32_t stvl = spb_done(&o);
            spb_out(&o, a_st, sizeof(a_st));
            spb_put_varint(&o, 1, SSC_ATTR_STREAM_TYPE);
            spb_put_bytes(&o, 2, st_v, stvl);
            uint32_t astl = spb_done(&o);
            spb_out(&o, pl, sizeof(pl));
            spb_put_bytes(&o, 1, a_rates, arl);
            spb_put_bytes(&o, 1, a_st, astl);
            uint32_t qlen = build_qmi_report(qmi, (uint16_t)(20 + idx), &suids[idx],
                SSC_MSG_ATTR_EVENT, 2000, pl, spb_done(&o));
            sns_core_receive(core, 1u + kinds[idx], 7u, 42u, qmi, qlen);
            cc_flush(&m);
        }

        uint8_t pl[64], qmi[384];
        SpbOut o;
        spb_out(&o, pl, sizeof(pl));
        spb_put_fixed32(&o, 1, f2bits(0.0f));
        spb_put_fixed32(&o, 1, f2bits(0.0f));
        spb_put_fixed32(&o, 1, f2bits(9.80665f));
        spb_put_varint(&o, 2, 3);
        uint32_t qlen = build_qmi_report(qmi, 40, &suids[0], SSC_MSG_SENSOR_EVENT, 3000, pl, spb_done(&o));
        sns_core_receive(core, 1u + PaganiSensorAccel, 7u, 42u, qmi, qlen);

        spb_out(&o, pl, sizeof(pl));
        spb_put_fixed32(&o, 1, f2bits(0.0f));
        spb_put_fixed32(&o, 1, f2bits(0.0f));
        spb_put_fixed32(&o, 1, f2bits(0.0f));
        spb_put_varint(&o, 2, 3);
        qlen = build_qmi_report(qmi, 41, &suids[1], SSC_MSG_SENSOR_EVENT, 3010, pl, spb_done(&o));
        sns_core_receive(core, 1u + PaganiSensorGyro, 7u, 42u, qmi, qlen);

        spb_out(&o, pl, sizeof(pl));
        spb_put_fixed32(&o, 1, f2bits(195.22f));
        spb_put_fixed32(&o, 1, f2bits(25.98f));
        spb_put_fixed32(&o, 1, f2bits(425.52f));
        spb_put_varint(&o, 2, 0);
        qlen = build_qmi_report(qmi, 42, &suids[2], SSC_MSG_SENSOR_EVENT, 3020, pl, spb_done(&o));
        sns_core_receive(core, 1u + PaganiSensorMag, 7u, 42u, qmi, qlen);
        cc_flush(&m);

        CHECK(m.mag_sample_count == 1u);
        CHECK(fabs((double)m.last_mag_value[0] / 65536.0 - (-36.5)) < 0.05);
        CHECK(fabs((double)m.last_mag_value[1] / 65536.0 - 0.0) < 0.05);
        CHECK(fabs((double)m.last_mag_value[2] / 65536.0 - (-25.56)) < 0.05);
        CHECK(m.compass_count >= 1u);
        verify_locationd_report(m.last_report, 90.0, flat_grav, 0.5);

        const unsigned ccount_before = m.compass_count;
        for (unsigned step = 0; step < 30u; ++step) {
            spb_out(&o, pl, sizeof(pl));
            spb_put_fixed32(&o, 1, f2bits(0.0f));
            spb_put_fixed32(&o, 1, f2bits(0.0f));
            spb_put_fixed32(&o, 1, f2bits(0.590f));
            spb_put_fixed32(&o, 1, f2bits(0.807f));
            spb_put_varint(&o, 2, 1);
            qlen = build_qmi_report(qmi, (uint16_t)(50 + step), &suids[3],
                SSC_MSG_SENSOR_EVENT, 4000ull + step * 384000ull, pl, spb_done(&o));
            sns_core_receive(core, 1u + PaganiSensorRotv, 7u, 42u, qmi, qlen);
            cc_flush(&m);
        }
        CHECK(m.compass_count == ccount_before);
        CHECK(m.disable_count[PaganiSensorRotv] == 0u);
        CHECK(core->rotv_bounces == 0u);
        check_angle_close((double)m.last_heading_mdeg / 1000.0, 90.0, 0.5);
        free(core);
    }
}

int main(int argc, char **argv)
{
    CHECK(argc >= 2);
    test_qrtr();
    test_smmu();
    test_fixed_and_orient();
    test_ssp_path_dotdot();
    test_real_pack_and_rpc(argv[1]);
    test_compass_and_core();
    printf("PASS fuxi-sensors: %u checks\n", gChecks);
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sanitize", action="store_true")
    arguments = parser.parse_args()
    assert PACK_BIN.is_file(), f"missing real sensor pack: {PACK_BIN}"
    host_tools = runpy.run_path(str(ROOT / "tools/test-fuxi-audio-prep.py"))
    compiler, environment = host_tools["compiler_environment"]()
    msvc = Path(compiler[0].strip('"')).name.lower() in ("cl", "cl.exe")
    with tempfile.TemporaryDirectory(prefix="fuxi-sensors-host-") as directory:
        temporary = Path(directory)
        harness = temporary / "test_sensors.c"
        harness.write_text(HARNESS, encoding="utf-8", newline="\n")
        executable = temporary / ("sensors-test.exe" if os.name == "nt" else "sensors-test")
        sources = [
            ADSP_DIR / "QcomQrtr.c",
            SNS_DIR / "SensorsPb.c",
            SNS_DIR / "SensorsSsc.c",
            SNS_DIR / "SensorsFixed.c",
            SNS_DIR / "SensorsOrient.c",
            SNS_DIR / "SensorsCompass.c",
            SNS_DIR / "SensorsPack.c",
            SNS_DIR / "SensorsRpc.c",
            SNS_DIR / "SensorsCore.c",
            SNS_DIR / "SensorsHid.c",
            harness,
        ]
        if msvc:
            compat = temporary / "msvc_atomic_compat.h"
            compat.write_text(
                "#ifndef MSVC_ATOMIC_COMPAT_H\n"
                "#define MSVC_ATOMIC_COMPAT_H\n"
                "#define __ATOMIC_RELAXED 0\n"
                "#define __ATOMIC_ACQUIRE 2\n"
                "#define __ATOMIC_RELEASE 3\n"
                "#define __ATOMIC_ACQ_REL 4\n"
                "#define __atomic_load_n(p, m) (*(p))\n"
                "#define __atomic_store_n(p, v, m) (*(p) = (v))\n"
                "#define __atomic_fetch_add(p, v, m) ((*(p) += (v)) - (v))\n"
                "#endif\n",
                encoding="utf-8", newline="\n")
            flags = ["/nologo", "/TC", "/std:c11", "/W3", "/Od", "/Zi", "/MD", "/D_CRT_SECURE_NO_WARNINGS", f"/FI{compat}"]
            if arguments.sanitize:
                flags += ["/fsanitize=address"]
            command = compiler + flags + [f"/I{ADSP_DIR}", f"/I{SNS_DIR}"] + [str(s) for s in sources] + [f"/Fe:{executable}"]
        else:
            flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g"]
            if arguments.sanitize:
                flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            command = compiler + flags + ["-I", str(ADSP_DIR), "-I", str(SNS_DIR)] + [str(s) for s in sources] + ["-lm", "-o", str(executable)]
        subprocess.run(command, cwd=directory, env=environment, check=True, timeout=120)
        result = subprocess.run([str(executable), str(PACK_BIN)], cwd=directory, env=environment,
            capture_output=True, text=True, timeout=60)
        lines = [line.strip() for line in result.stdout.splitlines() if line.strip()]
        if result.returncode or not any(line.startswith("PASS fuxi-sensors:") for line in lines):
            raise SystemExit(result.stdout + result.stderr)
        print(result.stdout.strip())
        print(f"compiler={Path(compiler[0]).name}, sanitize={arguments.sanitize}")


if __name__ == "__main__":
    main()
