#!/usr/bin/env python3
"""Host ASan/UBSan/LSan tests for M6 GNSS pure-C modules (fuxi_gnss_loc.h, fuxi_gnss_pds_bridge.h, coachz_bb_qmux.h)."""
import argparse
import os
from pathlib import Path
import re
import runpy
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parents[1]
KSRC = HERE / "ksrc" if (HERE / "ksrc/xnu/fuxi-modem").is_dir() else HERE
MODEM_DIR = KSRC / "xnu/fuxi-modem"

C_TEST_SOURCE = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "coachz_bb_qmux.h"
#include "fuxi_gnss_loc.h"
#include "fuxi_gnss_pds_bridge.h"

static unsigned g_checks;
#define CHECK(cond) do { \
    g_checks++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); \
        exit(1); \
    } \
} while (0)

static uint32_t f32_bits(float f) {
    uint32_t u = 0;
    memcpy(&u, &f, 4);
    return u;
}
static uint64_t f64_bits(double d) {
    uint64_t u = 0;
    memcpy(&u, &d, 8);
    return u;
}
static uint16_t rd16(const uint8_t *p) { return fgl_rd16(p); }
static uint32_t rd32(const uint8_t *p) { return fgl_rd32(p); }
static uint64_t rd64(const uint8_t *p) { return fgl_rd64(p); }

typedef struct {
    uint8_t b[2048];
    uint32_t n;
} SduBuf;

static void sdu_begin(SduBuf *m, uint8_t type, uint16_t txn, uint16_t msg_id) {
    memset(m, 0, sizeof *m);
    m->b[0] = type;
    fgl_wr16(m->b + 1, txn);
    fgl_wr16(m->b + 3, msg_id);
    m->n = 7u;
}
static void sdu_tlv(SduBuf *m, uint8_t tag, const void *val, uint16_t len) {
    CHECK(m->n + 3u + len <= sizeof m->b);
    m->b[m->n] = tag;
    fgl_wr16(m->b + m->n + 1, len);
    if (len && val) memcpy(m->b + m->n + 3, val, len);
    m->n += 3u + len;
    fgl_wr16(m->b + 5, m->n - 7u);
}
static void sdu_u8(SduBuf *m, uint8_t tag, uint8_t v) { sdu_tlv(m, tag, &v, 1u); }
static void sdu_u32(SduBuf *m, uint8_t tag, uint32_t v) {
    uint8_t b[4]; fgl_wr32(b, v); sdu_tlv(m, tag, b, 4u);
}
static void sdu_u64(SduBuf *m, uint8_t tag, uint64_t v) {
    uint8_t b[8]; fgl_wr64(b, v); sdu_tlv(m, tag, b, 8u);
}
static void sdu_result(SduBuf *m, uint16_t err) {
    uint8_t b[4];
    fgl_wr16(b, err ? 1u : 0u);
    fgl_wr16(b + 2, err);
    sdu_tlv(m, 0x02u, b, 4u);
}

static uint32_t make_qmux_req(uint8_t *frame, uint32_t room, uint8_t svc, uint8_t cid, const SduBuf *sdu) {
    const uint32_t total = CBQ_HDR + sdu->n;
    CHECK(total <= room);
    frame[0] = CBQ_IF_TYPE;
    cbq_wr16(frame + 1, CBQ_HDR - 1u + sdu->n);
    frame[3] = 0u;
    frame[4] = svc;
    frame[5] = cid;
    memcpy(frame + CBQ_HDR, sdu->b, sdu->n);
    return total;
}

static uint32_t make_ctl_get_cid(uint8_t *frame, uint32_t room, uint8_t txn, uint8_t svc) {
    const uint32_t tlvs = 4u;
    const uint32_t total = CBQ_HDR + CBQ_CTL_HDR + tlvs;
    CHECK(total <= room);
    frame[0] = CBQ_IF_TYPE;
    cbq_wr16(frame + 1, CBQ_HDR - 1u + CBQ_CTL_HDR + tlvs);
    frame[3] = 0u; frame[4] = 0u; frame[5] = 0u;
    frame[6] = 0u; frame[7] = txn;
    cbq_wr16(frame + 8, CBQ_CTL_GET_CLIENT_ID);
    cbq_wr16(frame + 10, tlvs);
    frame[12] = 0x01u; cbq_wr16(frame + 13, 1u); frame[15] = svc;
    return total;
}

static uint32_t make_ctl_rel_cid(uint8_t *frame, uint32_t room, uint8_t txn, uint8_t svc, uint8_t cid) {
    const uint32_t tlvs = 5u;
    const uint32_t total = CBQ_HDR + CBQ_CTL_HDR + tlvs;
    CHECK(total <= room);
    frame[0] = CBQ_IF_TYPE;
    cbq_wr16(frame + 1, CBQ_HDR - 1u + CBQ_CTL_HDR + tlvs);
    frame[3] = 0u; frame[4] = 0u; frame[5] = 0u;
    frame[6] = 0u; frame[7] = txn;
    cbq_wr16(frame + 8, CBQ_CTL_RELEASE_CLIENT_ID);
    cbq_wr16(frame + 10, tlvs);
    frame[12] = 0x01u; cbq_wr16(frame + 13, 2u); frame[15] = svc; frame[16] = cid;
    return total;
}

static uint32_t make_ctl_sync(uint8_t *frame, uint32_t room, uint8_t txn) {
    const uint32_t total = CBQ_HDR + CBQ_CTL_HDR;
    CHECK(total <= room);
    frame[0] = CBQ_IF_TYPE;
    cbq_wr16(frame + 1, CBQ_HDR - 1u + CBQ_CTL_HDR);
    frame[3] = 0u; frame[4] = 0u; frame[5] = 0u;
    frame[6] = 0u; frame[7] = txn;
    cbq_wr16(frame + 8, CBQ_CTL_SYNC);
    cbq_wr16(frame + 10, 0u);
    return total;
}

static void check_pds_resp(const struct fgp_host_msg *h, uint16_t txn, uint16_t msg_id, uint16_t expected_err) {
    CHECK(h->len >= CBQ_HDR + 14u);
    CHECK(h->frame[0] == CBQ_IF_TYPE && h->frame[3] == CBQ_FLAG_SERVICE && h->frame[4] == FGP_SVC_PDS);
    const uint8_t *s = h->frame + CBQ_HDR;
    const uint32_t sn = h->len - CBQ_HDR;
    CHECK(s[0] == CBQ_QMI_RESPONSE);
    CHECK(rd16(s + 1) == txn && rd16(s + 3) == msg_id);
    CHECK(fgl_result_error(s, sn) == expected_err);
}

/* G1: QMI_LOC 0x10 builders, parsers, and two-stage tracking */
static void test_g1_loc_codec(void) {
    uint8_t sdu[256];
    uint32_t n, len = 0;

    /* Too-small room returns 0 cleanly */
    CHECK(fgl_build_reg_events(sdu, 35u, 1u, FGL_EVT_DEFAULT_MASK) == 0u);
    CHECK(fgl_build_start(sdu, 10u, 1u, 1u, 1u, 1u, 1000u) == 0u);
    CHECK(fgl_build_stop(sdu, 8u, 1u, 1u) == 0u);

    /* REG_EVENTS: verify NMEA & FIX_SESSION_STATE bits stripped, and MHAL/0x11/0x12 wired (36 B SDU) */
    CHECK(FGL_EVT_MIN_MASK == 0x03ULL);
    CHECK(FGL_EVT_DEFAULT_MASK == 0x83ULL && FGL_EVT_BRIDGE_MASK == 0xFBULL);
    CHECK((FGL_EVT_DEFAULT_MASK & (FGL_EVT_NMEA | FGL_EVT_FIX_SESSION_STATE)) == 0ULL);
    CHECK((FGL_EVT_BRIDGE_MASK & (FGL_EVT_NMEA | FGL_EVT_FIX_SESSION_STATE)) == 0ULL);
    n = fgl_build_reg_events(sdu, sizeof sdu, 7u, FGL_EVT_BRIDGE_MASK);
    CHECK(n == 7u + 29u && sdu[0] == 0u && rd16(sdu + 1) == 7u && rd16(sdu + 3) == FGL_LOC_REG_EVENTS);
    const uint8_t *v = fgl_find_tlv(sdu, n, 0x01u, &len);
    CHECK(v && len == 8u && rd64(v) == FGL_EVT_BRIDGE_MASK);
    v = fgl_find_tlv(sdu, n, 0x10u, &len);
    CHECK(v && len == 4u && memcmp(v, "MHAL", 4u) == 0);
    v = fgl_find_tlv(sdu, n, 0x11u, &len);
    CHECK(v && len == 4u && rd32(v) == 1u);
    v = fgl_find_tlv(sdu, n, 0x12u, &len);
    CHECK(v && len == 1u && v[0] == 0u);

    /* START & STOP */
    n = fgl_build_start(sdu, sizeof sdu, 8u, 1u, FGL_RECURRENCE_PERIODIC, FGL_INTERMEDIATE_ENABLE, 1000u);
    CHECK(n == 7u + 4u + 7u * 3u && rd16(sdu + 3) == FGL_LOC_START);
    v = fgl_find_tlv(sdu, n, 0x01u, &len); CHECK(v && len == 1u && v[0] == 1u);
    v = fgl_find_tlv(sdu, n, 0x10u, &len); CHECK(v && len == 4u && rd32(v) == FGL_RECURRENCE_PERIODIC);
    v = fgl_find_tlv(sdu, n, 0x12u, &len); CHECK(v && len == 4u && rd32(v) == FGL_INTERMEDIATE_ENABLE);
    v = fgl_find_tlv(sdu, n, 0x13u, &len); CHECK(v && len == 4u && rd32(v) == 1000u);

    n = fgl_build_stop(sdu, sizeof sdu, 9u, 1u);
    CHECK(n == 11u && rd16(sdu + 3) == FGL_LOC_STOP);
    v = fgl_find_tlv(sdu, n, 0x01u, &len); CHECK(v && len == 1u && v[0] == 1u);

    /* INJECT_UTC_TIME, SET_NMEA_TYPES, DELETE_ASSIST_DATA, SET_OP_MODE, INJECT_POSITION */
    n = fgl_build_inject_utc_time(sdu, sizeof sdu, 10u, 1795523696789ULL, 2500u, FGL_TIME_SRC_AP);
    CHECK(n > 7u && rd16(sdu + 3) == FGL_LOC_INJECT_UTC_TIME);
    v = fgl_find_tlv(sdu, n, 0x01u, &len); CHECK(v && len == 8u && rd64(v) == 1795523696789ULL);
    v = fgl_find_tlv(sdu, n, 0x02u, &len); CHECK(v && len == 4u && rd32(v) == 2500u);
    v = fgl_find_tlv(sdu, n, 0x10u, &len); CHECK(v && len == 4u && rd32(v) == 1u);

    n = fgl_build_set_nmea_types(sdu, sizeof sdu, 11u, FGL_NMEA_TYPES_ALL_BASIC);
    CHECK(n == 14u && rd16(sdu + 3) == FGL_LOC_SET_NMEA_TYPES);
    n = fgl_build_delete_assist_data(sdu, sizeof sdu, 12u, 1u, 0ULL, 0u, 0u);
    CHECK(n == 11u && rd16(sdu + 3) == FGL_LOC_DELETE_ASSIST_DATA);
    n = fgl_build_set_op_mode(sdu, sizeof sdu, 13u, FGL_OP_MODE_STANDALONE);
    CHECK(n == 14u && rd16(sdu + 3) == FGL_LOC_SET_OP_MODE);
    n = fgl_build_get_op_mode(sdu, sizeof sdu, 14u);
    CHECK(n == 7u && rd16(sdu + 3) == FGL_LOC_GET_OP_MODE);

    /* REG_MASTER_CLIENT (0x00BA), GET_ENGINE_LOCK (0x003B) & full setGpsLockSync SET_ENGINE_LOCK (0x003A) */
    CHECK(fgl_build_reg_master_client(sdu, 10u, 1u, FGL_MASTER_KEY_DEFAULT) == 0u);
    CHECK(fgl_build_get_engine_lock(sdu, 13u, 1u) == 0u);
    CHECK(fgl_build_set_engine_lock(sdu, 31u, 1u, FGL_LOCK_NONE) == 0u);
    n = fgl_build_reg_master_client(sdu, sizeof sdu, 16u, FGL_MASTER_KEY_DEFAULT);
    CHECK(n == 14u && rd16(sdu + 3) == FGL_LOC_REG_MASTER_CLIENT);
    v = fgl_find_tlv(sdu, n, 0x01u, &len); CHECK(v && len == 4u && rd32(v) == FGL_MASTER_KEY_DEFAULT);
    n = fgl_build_get_engine_lock(sdu, sizeof sdu, 18u);
    CHECK(n == 14u && rd16(sdu + 3) == FGL_LOC_GET_ENGINE_LOCK);
    v = fgl_find_tlv(sdu, n, 0x10u, &len); CHECK(v && len == 4u && rd32(v) == 3u);
    n = fgl_build_set_engine_lock(sdu, sizeof sdu, 17u, FGL_LOCK_NONE);
    CHECK(n == 32u && rd16(sdu + 3) == FGL_LOC_SET_ENGINE_LOCK);
    v = fgl_find_tlv(sdu, n, 0x01u, &len); CHECK(v && len == 4u && rd32(v) == FGL_LOCK_NONE);
    v = fgl_find_tlv(sdu, n, 0x10u, &len); CHECK(v && len == 4u && rd32(v) == 3u);
    v = fgl_find_tlv(sdu, n, 0x12u, &len); CHECK(v && len == 8u && rd64(v) == 0ULL);
    n = fgl_build_set_engine_lock(sdu, sizeof sdu, 19u, FGL_LOCK_ALL);
    CHECK(n == 32u);
    v = fgl_find_tlv(sdu, n, 0x01u, &len); CHECK(v && len == 4u && rd32(v) == FGL_LOCK_ALL);
    v = fgl_find_tlv(sdu, n, 0x10u, &len); CHECK(v && len == 4u && rd32(v) == 3u);
    v = fgl_find_tlv(sdu, n, 0x12u, &len); CHECK(v && len == 8u && rd64(v) == 3ULL);

    struct fgl_inject_pos_req preq;
    memset(&preq, 0, sizeof preq);
    preq.lat_bits = f64_bits(25.0330); preq.has_lat = 1u;
    preq.lon_bits = f64_bits(121.5654); preq.has_lon = 1u;
    preq.hunc_bits = f32_bits(50.0f); preq.has_hunc = 1u;
    preq.utc_ms = 1795523696000ULL; preq.has_utc = 1u;
    n = fgl_build_inject_position(sdu, sizeof sdu, 15u, &preq);
    CHECK(n > 7u && rd16(sdu + 3) == FGL_LOC_INJECT_POSITION);

    /* Two-stage RESP + IND tracker across all 9 two-stage msg_ids */
    static const uint16_t ts_msgs[9] = {
        FGL_LOC_INJECT_UTC_TIME, FGL_LOC_INJECT_POSITION, FGL_LOC_SET_NMEA_TYPES,
        FGL_LOC_DELETE_ASSIST_DATA, FGL_LOC_SET_OP_MODE, FGL_LOC_GET_OP_MODE,
        FGL_LOC_SET_ENGINE_LOCK, FGL_LOC_REG_MASTER_CLIENT, FGL_LOC_GET_ENGINE_LOCK
    };
    struct fgl_two_stage_tracker tr;
    fgl_tracker_init(&tr);
    for (unsigned i = 0; i < 9u; i++) {
        fgl_tracker_note_tx(&tr, ts_msgs[i], (uint16_t)(100u + i));
        CHECK(tr.slot[i].awaiting_resp && tr.slot[i].awaiting_ind);
        SduBuf resp, ind, bad_resp;
        /* Mismatched txn RESP MUST be ignored and leave awaiting_resp=1 */
        sdu_begin(&bad_resp, CBQ_QMI_RESPONSE, (uint16_t)(900u + i), ts_msgs[i]);
        sdu_result(&bad_resp, 0u);
        CHECK(fgl_tracker_handle_rx(&tr, bad_resp.b, bad_resp.n) == 0);
        CHECK(tr.slot[i].awaiting_resp == 1u && tr.resp_count == i);

        sdu_begin(&resp, CBQ_QMI_RESPONSE, (uint16_t)(100u + i), ts_msgs[i]);
        sdu_result(&resp, 0u);
        CHECK(fgl_tracker_handle_rx(&tr, resp.b, resp.n) == 1);
        CHECK(!tr.slot[i].awaiting_resp && tr.slot[i].awaiting_ind && tr.slot[i].last_resp_err == 0u);

        sdu_begin(&ind, CBQ_QMI_INDICATION, 0u, ts_msgs[i]);
        sdu_u32(&ind, 0x01u, FGL_IND_SUCCESS);
        if (ts_msgs[i] == FGL_LOC_GET_OP_MODE) sdu_u32(&ind, 0x10u, FGL_OP_MODE_STANDALONE);
        if (ts_msgs[i] == FGL_LOC_GET_ENGINE_LOCK) sdu_u32(&ind, 0x10u, FGL_LOCK_NONE);
        CHECK(fgl_tracker_handle_rx(&tr, ind.b, ind.n) == 1);
        CHECK(!tr.slot[i].awaiting_ind && tr.slot[i].last_ind_status == FGL_IND_SUCCESS);
        if (ts_msgs[i] == FGL_LOC_GET_OP_MODE) CHECK(tr.slot[i].last_extra_u32 == FGL_OP_MODE_STANDALONE);
        if (ts_msgs[i] == FGL_LOC_GET_ENGINE_LOCK) {
            uint32_t gst = 99u, glock = 99u;
            CHECK(tr.slot[i].last_extra_u32 == FGL_LOCK_NONE);
            CHECK(fgl_parse_get_engine_lock_ind(ind.b, ind.n, &gst, &glock) && gst == 0u && glock == FGL_LOCK_NONE);
        }
    }
    CHECK(tr.resp_count == 9u && tr.ind_count == 9u && tr.ind_failures == 0u);
    CHECK(fgp_is_benign_success_msg(FGP_PDS_EXT_SET_IMD_CONFIG) == 1);

    /* fgl_f32_scale_u32 overflow guard */
    CHECK(fgl_f32_scale_u32(0x7f7fffffu, 65535u, 360u, 65535u) == 65535u);
    CHECK(fgl_f32_scale_u32(f32_bits(1e18f), 1u, 3u, 999u) == 999u);

    /* ENGINE_STATE & FIX_SESSION_STATE */
    SduBuf ind;
    int32_t eng = 0;
    uint32_t sess = 0;
    sdu_begin(&ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_ENGINE_STATE);
    sdu_u32(&ind, 0x01u, FGL_ENGINE_ON);
    CHECK(fgl_parse_engine_state(ind.b, ind.n, &eng) && eng == FGL_ENGINE_ON);
    sdu_begin(&ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_FIX_SESSION_STATE);
    sdu_u32(&ind, 0x01u, FGL_FIX_SESSION_STARTED);
    CHECK(fgl_parse_fix_session_state(ind.b, ind.n, &sess) && sess == FGL_FIX_SESSION_STARTED);
    sdu_begin(&ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_FIX_SESSION_STATE);
    sdu_u32(&ind, 0x01u, FGL_FIX_SESSION_FINISHED);
    CHECK(fgl_parse_fix_session_state(ind.b, ind.n, &sess) && sess == FGL_FIX_SESSION_FINISHED);
}

/* G2: 8 strict init gates, 0x0020 trackingState consistency, calendar math, POS/SV/NMEA translation */
static void test_g2_bridge_and_translation(void) {
    struct fgp_bridge b;
    struct fgp_actions act;
    struct fgp_scratch *sc = (struct fgp_scratch *)malloc(sizeof *sc);
    CHECK(sc != NULL);
    fgp_init(&b);
    CHECK(b.enabled == 0u && b.tracking == FGP_TRACK_INACTIVE);

    /* 1. RESET -> RESP + 0x0060 pdsReady IND */
    SduBuf req;
    sdu_begin(&req, CBQ_QMI_REQUEST, 1u, FGP_PDS_RESET);
    fgp_handle_pds_request(&b, 0u, 7u, req.b, req.n, &act);
    CHECK(act.handled && act.host_count == 2u);
    check_pds_resp(&act.host[0], 1u, FGP_PDS_RESET, 0u);
    CHECK(act.host[1].frame[CBQ_HDR] == CBQ_QMI_INDICATION);
    CHECK(rd16(act.host[1].frame + CBQ_HDR + 3) == FGP_PDS_READY_IND);

    /* 2. All 8 strict init gates (CLQmiPdsController::init) */
    sdu_begin(&req, CBQ_QMI_REQUEST, 2u, FGP_PDS_GET_GPS_SERVICE_STATE);
    fgp_handle_pds_request(&b, 0u, 7u, req.b, req.n, &act);
    CHECK(act.host_count == 1u);
    check_pds_resp(&act.host[0], 2u, FGP_PDS_GET_GPS_SERVICE_STATE, 0u);
    uint32_t tlen = 0;
    const uint8_t *t1 = fgl_find_tlv(act.host[0].frame + CBQ_HDR, act.host[0].len - CBQ_HDR, 0x01u, &tlen);
    CHECK(t1 && tlen == 2u && t1[0] == 0u && t1[1] == FGP_TRACK_INACTIVE);

    sdu_begin(&req, CBQ_QMI_REQUEST, 3u, FGP_PDS_SET_FORCE_RECEIVER_OFF);
    sdu_u8(&req, 0x01u, 1u);
    fgp_handle_pds_request(&b, 0u, 7u, req.b, req.n, &act);
    check_pds_resp(&act.host[0], 3u, FGP_PDS_SET_FORCE_RECEIVER_OFF, 0u);

    sdu_begin(&req, CBQ_QMI_REQUEST, 4u, FGP_PDS_SET_EVENT_REPORT);
    sdu_u8(&req, 0x10u, 1u); sdu_u8(&req, 0x12u, 1u); sdu_u8(&req, 0x16u, 1u); sdu_u8(&req, 0x14u, 1u);
    fgp_handle_pds_request(&b, 0u, 7u, req.b, req.n, &act);
    check_pds_resp(&act.host[0], 4u, FGP_PDS_SET_EVENT_REPORT, 0u);
    CHECK(act.loc_count == 1u && rd16(act.loc[0].sdu + 3) == FGL_LOC_REG_EVENTS);

    sdu_begin(&req, CBQ_QMI_REQUEST, 5u, FGP_PDS_SET_LPP_CONFIG);
    sdu_u32(&req, 0x01u, 0x0bu);
    fgp_handle_pds_request(&b, 0u, 7u, req.b, req.n, &act);
    check_pds_resp(&act.host[0], 5u, FGP_PDS_SET_LPP_CONFIG, 0u);

    sdu_begin(&req, CBQ_QMI_REQUEST, 6u, FGP_PDS_SET_GPS_SERVICE_STATE);
    sdu_u8(&req, 0x01u, 1u); sdu_u64(&req, 0x10u, 0x1ULL);
    fgp_handle_pds_request(&b, 0u, 7u, req.b, req.n, &act);
    check_pds_resp(&act.host[0], 6u, FGP_PDS_SET_GPS_SERVICE_STATE, 0u);

    sdu_begin(&req, CBQ_QMI_REQUEST, 7u, FGP_PDS_SET_AUTO_TRACKING_STATE);
    sdu_u8(&req, 0x01u, 0u);
    fgp_handle_pds_request(&b, 0u, 7u, req.b, req.n, &act);
    check_pds_resp(&act.host[0], 7u, FGP_PDS_SET_AUTO_TRACKING_STATE, 0u);

    sdu_begin(&req, CBQ_QMI_REQUEST, 8u, FGP_PDS_EXT_SET_SV_IN_FIX_REPORT);
    sdu_u8(&req, 0x01u, 1u);
    fgp_handle_pds_request(&b, 0u, 7u, req.b, req.n, &act);
    check_pds_resp(&act.host[0], 8u, FGP_PDS_EXT_SET_SV_IN_FIX_REPORT, 0u);

    sdu_begin(&req, CBQ_QMI_REQUEST, 9u, FGP_PDS_SET_SPOOFING_REPORT_CONFIG);
    sdu_u8(&req, 0x01u, 1u);
    fgp_handle_pds_request(&b, 0u, 7u, req.b, req.n, &act);
    check_pds_resp(&act.host[0], 9u, FGP_PDS_SET_SPOOFING_REPORT_CONFIG, 0u);

    sdu_begin(&req, CBQ_QMI_REQUEST, 10u, FGP_PDS_SET_GPS_SERVICE_STATE);
    sdu_u8(&req, 0x01u, 0u);
    fgp_handle_pds_request(&b, 0u, 7u, req.b, req.n, &act);
    check_pds_resp(&act.host[0], 10u, FGP_PDS_SET_GPS_SERVICE_STATE, 0u);

    /* 3. TimeTransfer returns benign NOT_SUPPORTED (94); DPO/NMEA/RF/Sensor benign success */
    sdu_begin(&req, CBQ_QMI_REQUEST, 11u, FGP_PDS_SET_TIME_TRANSFER_CONFIG);
    sdu_u8(&req, 0x01u, 1u);
    fgp_handle_pds_request(&b, 0u, 7u, req.b, req.n, &act);
    check_pds_resp(&act.host[0], 11u, FGP_PDS_SET_TIME_TRANSFER_CONFIG, CBQ_ERR_NOT_SUPPORTED);

    sdu_begin(&req, CBQ_QMI_REQUEST, 12u, FGP_PDS_INJECT_CLIENT_TIME_NS_REQ);
    sdu_u64(&req, 0x01u, 123456789ULL);
    fgp_handle_pds_request(&b, 0u, 7u, req.b, req.n, &act);
    check_pds_resp(&act.host[0], 12u, FGP_PDS_INJECT_CLIENT_TIME_NS_REQ, CBQ_ERR_NOT_SUPPORTED);

    sdu_begin(&req, CBQ_QMI_REQUEST, 13u, FGP_PDS_SET_DPO_CONFIG);
    sdu_u8(&req, 0x10u, 1u);
    fgp_handle_pds_request(&b, 0u, 7u, req.b, req.n, &act);
    check_pds_resp(&act.host[0], 13u, FGP_PDS_SET_DPO_CONFIG, 0u);

    /* 0x003F GET_DPO_CONFIG -> local TLV 0x10 = 0 (DPO disabled); 0x003E SET_NMEA_CONFIG swallowed locally */
    sdu_begin(&req, CBQ_QMI_REQUEST, 131u, FGP_PDS_GET_DPO_CONFIG);
    fgp_handle_pds_request(&b, 0u, 7u, req.b, req.n, &act);
    CHECK(act.handled && !act.unknown && act.loc_count == 0u && act.host_count == 1u);
    check_pds_resp(&act.host[0], 131u, FGP_PDS_GET_DPO_CONFIG, 0u);
    const uint8_t *dpo10 = fgl_find_tlv(act.host[0].frame + CBQ_HDR, act.host[0].len - CBQ_HDR, 0x10u, &tlen);
    CHECK(dpo10 && tlen == 1u && dpo10[0] == 0u);
    sdu_begin(&req, CBQ_QMI_REQUEST, 132u, FGP_PDS_SET_NMEA_CONFIG);
    sdu_u8(&req, 0x01u, 1u);
    fgp_handle_pds_request(&b, 0u, 7u, req.b, req.n, &act);
    CHECK(act.handled && !act.unknown && act.loc_count == 0u && act.host_count == 1u);
    check_pds_resp(&act.host[0], 132u, FGP_PDS_SET_NMEA_CONFIG, 0u);

    /* 4. Start tracking: 0x002A (12B Messenger format!) + 0x0031(1) -> 0x0020 returns {1, 2} */
    uint8_t sess12[12];
    memset(sess12, 0, sizeof sess12);
    sess12[0] = FGP_OP_STANDALONE; sess12[1] = 0xffu;
    fgl_wr32(sess12 + 4, 2u);    /* interval = 2s -> 2000ms */
    fgl_wr32(sess12 + 8, 400u);  /* accuracy = 400m */
    sdu_begin(&req, CBQ_QMI_REQUEST, 14u, FGP_PDS_SET_DEFAULT_TRACKING_SESSION);
    sdu_tlv(&req, 0x01u, sess12, 12u);
    fgp_handle_pds_request(&b, 0u, 7u, req.b, req.n, &act);
    check_pds_resp(&act.host[0], 14u, FGP_PDS_SET_DEFAULT_TRACKING_SESSION, 0u);
    CHECK(act.loc_count == 1u && rd16(act.loc[0].sdu + 3) == FGL_LOC_SET_OP_MODE);
    const uint8_t *opm = fgl_find_tlv(act.loc[0].sdu, act.loc[0].len, 0x01u, &tlen);
    CHECK(opm && tlen == 4u && rd32(opm) == FGL_OP_MODE_STANDALONE);
    CHECK(b.interval_ms == 2000u);

    sdu_begin(&req, CBQ_QMI_REQUEST, 15u, FGP_PDS_SET_AUTO_TRACKING_STATE);
    sdu_u8(&req, 0x01u, 1u);
    fgp_handle_pds_request(&b, 0u, 7u, req.b, req.n, &act);
    check_pds_resp(&act.host[0], 15u, FGP_PDS_SET_AUTO_TRACKING_STATE, 0u);
    CHECK(act.loc_count == 1u && rd16(act.loc[0].sdu + 3) == FGL_LOC_START);
    const uint8_t *iv = fgl_find_tlv(act.loc[0].sdu, act.loc[0].len, 0x13u, &tlen);
    CHECK(iv && tlen == 4u && rd32(iv) == 2000u);

    /* Verify 0x0020 returns enabled=1, tracking=2 */
    sdu_begin(&req, CBQ_QMI_REQUEST, 16u, FGP_PDS_GET_GPS_SERVICE_STATE);
    fgp_handle_pds_request(&b, 0u, 7u, req.b, req.n, &act);
    t1 = fgl_find_tlv(act.host[0].frame + CBQ_HDR, act.host[0].len - CBQ_HDR, 0x01u, &tlen);
    CHECK(t1 && tlen == 2u && t1[0] == 1u && t1[1] == FGP_TRACK_ACTIVE);

    /* Also test legacy 10B 0x002A format compatibility */
    uint8_t sess10[10];
    sess10[0] = FGP_OP_MS_BASED; sess10[1] = 30u;
    fgl_wr32(sess10 + 2, 5u); fgl_wr32(sess10 + 6, 50u);
    sdu_begin(&req, CBQ_QMI_REQUEST, 17u, FGP_PDS_SET_DEFAULT_TRACKING_SESSION);
    sdu_tlv(&req, 0x01u, sess10, 10u);
    fgp_handle_pds_request(&b, 0u, 7u, req.b, req.n, &act);
    CHECK(b.interval_ms == 5000u && b.op_mode == FGP_OP_MS_BASED);
    opm = fgl_find_tlv(act.loc[0].sdu, act.loc[0].len, 0x01u, &tlen);
    CHECK(opm && rd32(opm) == FGL_OP_MODE_MSB);
    b.op_mode = FGP_OP_STANDALONE;

    /* 5. INJECT_ABSOLUTE_TIME_REF (0x003D) -> LOC 0x0038 */
    uint8_t tref[14];
    fgl_wr64(tref + 0, 1795523696789ULL);
    fgl_wr32(tref + 8, 1500u);
    tref[12] = 1u; tref[13] = 0u;
    sdu_begin(&req, CBQ_QMI_REQUEST, 18u, FGP_PDS_INJECT_ABSOLUTE_TIME_REF);
    sdu_tlv(&req, 0x01u, tref, 14u);
    fgp_handle_pds_request(&b, 0u, 7u, req.b, req.n, &act);
    check_pds_resp(&act.host[0], 18u, FGP_PDS_INJECT_ABSOLUTE_TIME_REF, 0u);
    CHECK(act.loc_count == 1u && rd16(act.loc[0].sdu + 3) == FGL_LOC_INJECT_UTC_TIME);

    /* 6. IN_PROGRESS without LAT/LON -> MUST NOT emit TLV 0x13 (prevents (0,0) fix!) */
    SduBuf loc_ind;
    sdu_begin(&loc_ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_POSITION_REPORT);
    sdu_u32(&loc_ind, 0x01u, FGL_SESS_IN_PROGRESS);
    sdu_u8(&loc_ind, 0x02u, 1u);
    fgp_handle_loc_rx(&b, loc_ind.b, loc_ind.n, &act, sc);
    CHECK(act.handled && act.host_count == 1u);
    const uint8_t *psdu = act.host[0].frame + CBQ_HDR;
    uint32_t plen = act.host[0].len - CBQ_HDR;
    CHECK(fgl_find_tlv(psdu, plen, 0x12u, &tlen) && tlen == 1u &&
          fgl_find_tlv(psdu, plen, 0x12u, &tlen)[0] == 1u);
    CHECK(fgl_find_tlv(psdu, plen, 0x13u, &tlen) == NULL);

    /* IN_PROGRESS with explicit (0.0, 0.0) -> ALSO filtered! */
    sdu_begin(&loc_ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_POSITION_REPORT);
    sdu_u32(&loc_ind, 0x01u, FGL_SESS_IN_PROGRESS);
    sdu_u64(&loc_ind, 0x10u, 0ULL);
    sdu_u64(&loc_ind, 0x11u, 0ULL);
    fgp_handle_loc_rx(&b, loc_ind.b, loc_ind.n, &act, sc);
    psdu = act.host[0].frame + CBQ_HDR; plen = act.host[0].len - CBQ_HDR;
    CHECK(fgl_find_tlv(psdu, plen, 0x13u, &tlen) == NULL);

    /* 7. Full SUCCESS POSITION_REPORT -> strict 105B TLV 0x13, calendar, heading bounds */
    /* 2026-11-24T12:34:56.789Z = 1795523696789 ms, Tuesday (wday=2) */
    sdu_begin(&loc_ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_POSITION_REPORT);
    sdu_u32(&loc_ind, 0x01u, FGL_SESS_SUCCESS);
    sdu_u8(&loc_ind, 0x02u, 1u);
    sdu_u64(&loc_ind, 0x10u, f64_bits(25.033964));
    sdu_u64(&loc_ind, 0x11u, f64_bits(121.564468));
    sdu_u32(&loc_ind, 0x12u, f32_bits(4.5f));
    sdu_u32(&loc_ind, 0x1Au, f32_bits(35.2f));
    sdu_u32(&loc_ind, 0x1Bu, f32_bits(18.0f));
    sdu_u32(&loc_ind, 0x18u, f32_bits(1.25f));
    sdu_u32(&loc_ind, 0x1Cu, f32_bits(6.0f));
    sdu_u32(&loc_ind, 0x20u, f32_bits(359.995f)); /* (359.99, 360.0] -> clamps to 0.0f, keeps HDING bit */
    sdu_u32(&loc_ind, 0x21u, f32_bits(3.5f));
    sdu_u32(&loc_ind, 0x23u, 1u);
    uint8_t dop12[12];
    fgl_wr32(dop12 + 0, f32_bits(1.8f));
    fgl_wr32(dop12 + 4, f32_bits(1.1f));
    fgl_wr32(dop12 + 8, f32_bits(1.4f));
    sdu_tlv(&loc_ind, 0x24u, dop12, 12u);
    sdu_u64(&loc_ind, 0x25u, 1795523696789ULL);
    sdu_u8(&loc_ind, 0x26u, 18u);
    sdu_u32(&loc_ind, 0x28u, f32_bits(12.6f)); /* rounds to 13 */
    sdu_u8(&loc_ind, 0x16u, 68u);
    sdu_u32(&loc_ind, 0x17u, 4u);
    sdu_u32(&loc_ind, 0x1Eu, 3u);

    fgp_handle_loc_rx(&b, loc_ind.b, loc_ind.n, &act, sc);
    CHECK(act.handled && act.host_count == 1u);
    psdu = act.host[0].frame + CBQ_HDR; plen = act.host[0].len - CBQ_HDR;
    const uint8_t *t13 = fgl_find_tlv(psdu, plen, 0x13u, &tlen);
    CHECK(t13 != NULL && tlen == 105u);
    const uint32_t val = rd32(t13 + 0x00);
    CHECK((val & (FGP_VAL_CAL | FGP_VAL_UTC | FGP_VAL_SECS)) == 0x7u);
    CHECK((val & (FGP_VAL_LAT | FGP_VAL_LON | FGP_VAL_HDING | FGP_VAL_TUNC)) ==
          (FGP_VAL_LAT | FGP_VAL_LON | FGP_VAL_HDING | FGP_VAL_TUNC));
    CHECK(rd16(t13 + 0x04) == 2026u && t13[0x06] == 11u && t13[0x07] == 2u && t13[0x08] == 24u);
    CHECK(t13[0x09] == 12u && t13[0x0a] == 34u && t13[0x0b] == 56u && rd16(t13 + 0x0c) == 789u);
    CHECK(t13[0x0e] == 18u && rd64(t13 + 0x0f) == 1795523696789ULL && rd32(t13 + 0x17) == 13u);
    CHECK(rd64(t13 + 0x1b) == f64_bits(25.033964) && rd64(t13 + 0x23) == f64_bits(121.564468));
    CHECK(rd32(t13 + 0x3b) == 0u); /* clamped 359.995 -> 0.0 */
    CHECK(t13[0x5b] == 68u && t13[0x68] == FGP_OP_STANDALONE);

    /* Negative or >360 or NaN heading clears HDING bit */
    uint32_t h_out = 123u;
    CHECK(!fgp_sanitize_heading_bits(f32_bits(-0.5f), &h_out));
    CHECK(!fgp_sanitize_heading_bits(f32_bits(360.5f), &h_out));
    CHECK(!fgp_sanitize_heading_bits(0x7fc00000u, &h_out));
    CHECK(fgp_sanitize_heading_bits(f32_bits(180.25f), &h_out) && h_out == f32_bits(180.25f));

    /* Calendar leap day check: 2000-02-29T23:59:59.001Z = 951868799001 ms (Tuesday = 2) */
    struct fgp_cal_time cal;
    fgp_utc_ms_to_cal(951868799001ULL, &cal);
    CHECK(cal.year == 2000u && cal.month == 2u && cal.day == 29u && cal.wday == 2u &&
          cal.hour == 23u && cal.minute == 59u && cal.second == 59u && cal.millisec == 1u);
    fgp_utc_ms_to_cal(0ULL, &cal);
    CHECK(cal.year == 1970u && cal.month == 1u && cal.day == 1u && cal.wday == 4u);

    /* 8. SV_INFO 0x0025 (28 B packed wire) -> PDS TLV 0x17 (CLEurekaController layout, QZSS=6 & Galileo PRN) */
    CHECK(FGL_SV_WIRE_BYTES == 28u);
    /* Verify exact 28 B vs 27 B boundary */
    uint8_t sv_short[1u + 27u];
    memset(sv_short, 0, sizeof sv_short);
    sv_short[0] = 1u;
    sdu_begin(&loc_ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_GNSS_SV_INFO);
    sdu_tlv(&loc_ind, 0x10u, sv_short, sizeof sv_short);
    CHECK(fgl_parse_sv_info(loc_ind.b, loc_ind.n, &sc->sv) == 0);

    uint8_t sv10[1u + FGL_SV_WIRE_BYTES * 4u];
    memset(sv10, 0, sizeof sv10);
    sv10[0] = 4u;
    /* SV 0: GPS prn=12, el=45.0, az=180.0, snr=38.5 */
    uint8_t *e0 = sv10 + 1u + FGL_SV_WIRE_BYTES * 0u;
    fgl_wr32(e0 + 0x00, 0xFFu); fgl_wr32(e0 + 0x04, 1u); fgl_wr16(e0 + 0x08, 12u);
    e0[0x0a] = 1u; fgl_wr32(e0 + 0x0b, 2u); e0[0x0f] = 3u;
    fgl_wr32(e0 + 0x10, f32_bits(45.0f)); fgl_wr32(e0 + 0x14, f32_bits(180.0f)); fgl_wr32(e0 + 0x18, f32_bits(38.5f));
    /* SV 1: GLONASS svId=72 */
    uint8_t *e1 = sv10 + 1u + FGL_SV_WIRE_BYTES * 1u;
    fgl_wr32(e1 + 0x00, 0xFFu); fgl_wr32(e1 + 0x04, 5u); fgl_wr16(e1 + 0x08, 72u);
    e1[0x0a] = 1u; fgl_wr32(e1 + 0x0b, 2u); e1[0x0f] = 1u;
    fgl_wr32(e1 + 0x10, f32_bits(30.0f)); fgl_wr32(e1 + 0x14, f32_bits(90.0f)); fgl_wr32(e1 + 0x18, f32_bits(31.0f));
    /* SV 2: Galileo svId=316 (must NOT collide with GLONASS 65..96!) */
    uint8_t *e2 = sv10 + 1u + FGL_SV_WIRE_BYTES * 2u;
    fgl_wr32(e2 + 0x00, 0xFFu); fgl_wr32(e2 + 0x04, 2u); fgl_wr16(e2 + 0x08, 316u);
    e2[0x0a] = 1u; fgl_wr32(e2 + 0x0b, 2u); e2[0x0f] = 2u;
    fgl_wr32(e2 + 0x10, f32_bits(60.0f)); fgl_wr32(e2 + 0x14, f32_bits(270.0f)); fgl_wr32(e2 + 0x18, f32_bits(42.0f));
    /* SV 3: QZSS system=6, svId=193, el=52.25, az=135.0, snr=44.0 */
    uint8_t *e3 = sv10 + 1u + FGL_SV_WIRE_BYTES * 3u;
    fgl_wr32(e3 + 0x00, 0xFFu); fgl_wr32(e3 + 0x04, 6u); fgl_wr16(e3 + 0x08, 193u);
    e3[0x0a] = 1u; fgl_wr32(e3 + 0x0b, 2u); e3[0x0f] = 3u;
    fgl_wr32(e3 + 0x10, f32_bits(52.25f)); fgl_wr32(e3 + 0x14, f32_bits(135.0f)); fgl_wr32(e3 + 0x18, f32_bits(44.0f));

    sdu_begin(&loc_ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_GNSS_SV_INFO);
    sdu_u8(&loc_ind, 0x01u, 0u);
    sdu_tlv(&loc_ind, 0x10u, sv10, sizeof sv10);
    fgp_handle_loc_rx(&b, loc_ind.b, loc_ind.n, &act, sc);
    CHECK(act.handled && act.host_count == 1u);
    psdu = act.host[0].frame + CBQ_HDR; plen = act.host[0].len - CBQ_HDR;
    const uint8_t *t17 = fgl_find_tlv(psdu, plen, 0x17u, &tlen);
    CHECK(t17 && tlen == 6u + 18u * 4u && (rd32(t17) & 2u) && t17[5] == 4u);
    /* Check SV0: system=1, prn=12, health=1, proc=2, eph=1, alm=1, +0x0a=float elev(45.0), +0x0e=u16 az(180), +0x10=u16 cno(39) */
    const uint8_t *p0 = t17 + 6u;
    CHECK((rd32(p0) & 0x1FFu) == 0x1FFu);
    CHECK(p0[4] == 1u && p0[5] == 12u && p0[6] == 1u && p0[7] == 2u && p0[8] == 1u && p0[9] == 1u);
    CHECK(rd32(p0 + 0x0a) == f32_bits(45.0f));
    CHECK(rd16(p0 + 0x0e) == 180u);
    CHECK(rd16(p0 + 0x10) == 39u);
    /* Check SV1 vs SV2 PRN disjointness, and SV3 QZSS system=6 preserved */
    const uint8_t *p1 = t17 + 6u + 18u;
    const uint8_t *p2 = t17 + 6u + 36u;
    const uint8_t *p3 = t17 + 6u + 54u;
    CHECK(p1[4] == 5u && p1[5] == 72u);
    CHECK(p2[4] == 2u && p2[5] == 116u && p2[5] != p1[5] && (p2[5] < 65u || p2[5] > 96u));
    CHECK(p3[4] == 6u && p3[5] == 193u);
    CHECK(rd32(p3 + 0x0a) == f32_bits(52.25f) && rd16(p3 + 0x0e) == 135u && rd16(p3 + 0x10) == 44u);

    /* 8b. SM8550 TLV 0x11 gnssSvList (29 B/entry): 720B (17 SVs + TLV 0x12/0x13), priority over TLV 0x10, bounds */
    CHECK(FGL_SV_EXT_WIRE_BYTES == 29u);
    uint8_t sv11_short[1u + 28u];
    memset(sv11_short, 0, sizeof sv11_short);
    sv11_short[0] = 1u;
    sdu_begin(&loc_ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_GNSS_SV_INFO);
    sdu_tlv(&loc_ind, 0x11u, sv11_short, sizeof sv11_short);
    CHECK(fgl_parse_sv_info(loc_ind.b, loc_ind.n, &sc->sv) == 0);

    uint8_t sv11_17[1u + FGL_SV_EXT_WIRE_BYTES * 17u];
    memset(sv11_17, 0, sizeof sv11_17);
    sv11_17[0] = 17u;
    for (unsigned i = 0; i < 17u; i++) {
        uint8_t *ep = sv11_17 + 1u + FGL_SV_EXT_WIRE_BYTES * i;
        fgl_wr32(ep + 0x00, 0xFFu);
        fgl_wr32(ep + 0x04, (i == 2u) ? 2u : 1u);
        fgl_wr16(ep + 0x08, (i == 2u) ? 316u : (uint16_t)(1u + i));
        ep[0x0a] = 1u;
        fgl_wr32(ep + 0x0b, 2u);
        ep[0x0f] = 3u;
        fgl_wr32(ep + 0x10, f32_bits(20.0f + (float)i));
        fgl_wr32(ep + 0x14, f32_bits(10.0f * (float)i));
        fgl_wr32(ep + 0x18, f32_bits(30.0f + (float)i));
        ep[0x1c] = (uint8_t)(0xA0u + i); /* 29th ext byte */
    }
    uint8_t sv_t12[1u + 4u * 17u];
    uint8_t sv_t13[1u + 8u * 17u];
    memset(sv_t12, 0, sizeof sv_t12); sv_t12[0] = 17u;
    memset(sv_t13, 0, sizeof sv_t13); sv_t13[0] = 17u;
    sdu_begin(&loc_ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_GNSS_SV_INFO);
    sdu_u8(&loc_ind, 0x01u, 1u);
    sdu_tlv(&loc_ind, 0x11u, sv11_17, sizeof sv11_17);
    sdu_tlv(&loc_ind, 0x12u, sv_t12, sizeof sv_t12);
    sdu_tlv(&loc_ind, 0x13u, sv_t13, sizeof sv_t13);
    CHECK(loc_ind.n == 720u); /* Matches exact SM8550 boot.log len 720 for N=17 */
    fgp_handle_loc_rx(&b, loc_ind.b, loc_ind.n, &act, sc);
    CHECK(act.handled && sc->sv.alt_assumed == 1u && sc->sv.count == 17u && act.host_count == 1u);
    psdu = act.host[0].frame + CBQ_HDR; plen = act.host[0].len - CBQ_HDR;
    t17 = fgl_find_tlv(psdu, plen, 0x17u, &tlen);
    CHECK(t17 && tlen == 6u + 18u * 17u && t17[5] == 17u);
    CHECK(t17[6u + 18u * 2u + 4u] == 2u && t17[6u + 18u * 2u + 5u] == 116u);
    CHECK(rd16(t17 + 6u + 18u * 16u + 0x10) == 46u);

    /* When both TLV 0x10 and TLV 0x11 are present, TLV 0x11 takes priority (matching reportSv) */
    uint8_t sv10_two[1u + FGL_SV_WIRE_BYTES * 2u];
    memset(sv10_two, 0, sizeof sv10_two);
    sv10_two[0] = 2u;
    sdu_begin(&loc_ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_GNSS_SV_INFO);
    sdu_u8(&loc_ind, 0x01u, 0u);
    sdu_tlv(&loc_ind, 0x10u, sv10_two, sizeof sv10_two);
    sdu_tlv(&loc_ind, 0x11u, sv11_17, sizeof sv11_17);
    CHECK(fgl_parse_sv_info(loc_ind.b, loc_ind.n, &sc->sv) == 1 && sc->sv.count == 17u);

    /* Clamping > FGL_MAX_SV (70 -> 64) on TLV 0x11 without buffer overrun */
    uint8_t *sv11_70 = (uint8_t *)calloc(1u, 1u + FGL_SV_EXT_WIRE_BYTES * 70u);
    CHECK(sv11_70 != NULL);
    sv11_70[0] = 70u;
    /* Note: SduBuf has 2048 B limit, 1 + 29*70 = 2031 -> fits in 2048 with header+alt */
    SduBuf *big_ind = (SduBuf *)malloc(sizeof *big_ind);
    CHECK(big_ind != NULL);
    sdu_begin(big_ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_GNSS_SV_INFO);
    sdu_tlv(big_ind, 0x11u, sv11_70, 1u + FGL_SV_EXT_WIRE_BYTES * 70u);
    CHECK(fgl_parse_sv_info(big_ind->b, big_ind->n, &sc->sv) == 1 && sc->sv.count == FGL_MAX_SV);
    free(big_ind);
    free(sv11_70);

    /* 9. NMEA 0x0026 -> PDS TLV 0x10 raw ASCII (no length prefix) */
    static const char sample_nmea[] = "$GPGGA,123456.00,2502.037,N,12133.868,E,1,08,0.9,18.0,M,,,,*00\r\n";
    sdu_begin(&loc_ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_NMEA);
    sdu_tlv(&loc_ind, 0x01u, sample_nmea, (uint16_t)strlen(sample_nmea));
    fgp_handle_loc_rx(&b, loc_ind.b, loc_ind.n, &act, sc);
    CHECK(act.handled && act.host_count == 1u);
    psdu = act.host[0].frame + CBQ_HDR; plen = act.host[0].len - CBQ_HDR;
    const uint8_t *t10 = fgl_find_tlv(psdu, plen, 0x10u, &tlen);
    CHECK(t10 && tlen == strlen(sample_nmea) && memcmp(t10, sample_nmea, tlen) == 0);

    /* 10. 0x002C FIX_SESSION_STATE STARTED(1) / FINISHED(2) & Stop/Cancel -> stays INACTIVE (1) */
    sdu_begin(&loc_ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_FIX_SESSION_STATE);
    sdu_u32(&loc_ind, 0x01u, FGL_FIX_SESSION_STARTED);
    fgp_handle_loc_rx(&b, loc_ind.b, loc_ind.n, &act, sc);
    CHECK(act.handled && b.enabled == 1u && b.tracking == FGP_TRACK_ACTIVE && act.host_count == 1u);
    psdu = act.host[0].frame + CBQ_HDR; plen = act.host[0].len - CBQ_HDR;
    t1 = fgl_find_tlv(psdu, plen, 0x01u, &tlen);
    CHECK(t1 && tlen == 2u && t1[0] == 1u && t1[1] == FGP_TRACK_ACTIVE);

    sdu_begin(&req, CBQ_QMI_REQUEST, 19u, FGP_PDS_SET_AUTO_TRACKING_STATE);
    sdu_u8(&req, 0x01u, 0u);
    fgp_handle_pds_request(&b, 0u, 7u, req.b, req.n, &act);
    CHECK(b.tracking == FGP_TRACK_INACTIVE && act.loc_count == 1u && rd16(act.loc[0].sdu + 3) == FGL_LOC_STOP);

    /* Late/finished 0x002C(2 = FINISHED) MUST keep tracking INACTIVE (1), never flip back to ACTIVE! */
    sdu_begin(&loc_ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_FIX_SESSION_STATE);
    sdu_u32(&loc_ind, 0x01u, FGL_FIX_SESSION_FINISHED);
    fgp_handle_loc_rx(&b, loc_ind.b, loc_ind.n, &act, sc);
    CHECK(act.handled && b.tracking == FGP_TRACK_INACTIVE && act.host_count == 1u);
    psdu = act.host[0].frame + CBQ_HDR; plen = act.host[0].len - CBQ_HDR;
    CHECK(rd16(psdu + 3) == FGP_PDS_GET_GPS_SERVICE_STATE);
    t1 = fgl_find_tlv(psdu, plen, 0x01u, &tlen);
    CHECK(t1 && tlen == 2u && t1[0] == b.enabled && t1[1] == FGP_TRACK_INACTIVE);

    free(sc);
}

/* M6 Iter 2/3: fuxi,modem-gnss = 3 Probe->Bridge handover + Scenario C2/C3 + 0x0021(0) power-off */
static void test_mode3_probe_bridge_handover(void) {
    struct fgp_bridge b;
    struct fgp_actions act;
    struct fgp_scratch *sc = (struct fgp_scratch *)malloc(sizeof *sc);
    CHECK(sc != NULL);
    fgp_init(&b);

    /* Simulate Probe active (fuxi,modem-gnss = 3, FGL_PROBE_SID = 2) before locationd opens 0x06 */
    b.probe_active = 1u;
    CHECK(b.loc_registered == 0u);

    unsigned early_ind_before_event_reg = 0u;
    unsigned loc_stop_count = 0u;
    unsigned loc_start_count = 0u;

    /* Probe receives POS/SV/NMEA/0x002B/0x002C before locationd connects -> 0 host indications */
    SduBuf loc_ind;
    sdu_begin(&loc_ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_POSITION_REPORT);
    sdu_u32(&loc_ind, 0x01u, FGL_SESS_SUCCESS);
    sdu_u8(&loc_ind, 0x02u, FGL_PROBE_SID);
    sdu_u64(&loc_ind, 0x10u, f64_bits(25.0330));
    sdu_u64(&loc_ind, 0x11u, f64_bits(121.5654));
    fgp_handle_loc_rx(&b, loc_ind.b, loc_ind.n, &act, sc);
    early_ind_before_event_reg += act.host_count;

    sdu_begin(&loc_ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_FIX_SESSION_STATE);
    sdu_u32(&loc_ind, 0x01u, FGL_FIX_SESSION_STARTED);
    fgp_handle_loc_rx(&b, loc_ind.b, loc_ind.n, &act, sc);
    early_ind_before_event_reg += act.host_count;
    CHECK(b.enabled == 0u && b.tracking == FGP_TRACK_INACTIVE);

    /* locationd opens PDS 0x06 and sends FGP_PDS_RESET -> MUST emit FGL_LOC_STOP(FGL_PROBE_SID=2) */
    SduBuf req;
    sdu_begin(&req, CBQ_QMI_REQUEST, 1u, FGP_PDS_RESET);
    fgp_handle_pds_request(&b, 0u, 9u, req.b, req.n, &act);
    CHECK(act.handled && b.probe_active == 0u && b.probe_handover_stop == 1u && b.probe_handover_off == 1u);
    CHECK(act.loc_count == 1u && rd16(act.loc[0].sdu + 3) == FGL_LOC_STOP);
    uint32_t tlen = 0;
    const uint8_t *sid_v = fgl_find_tlv(act.loc[0].sdu, act.loc[0].len, 0x01u, &tlen);
    CHECK(sid_v && tlen == 1u && sid_v[0] == FGL_PROBE_SID);
    loc_stop_count += act.loc_count;

    /* Before 0x0001 SET_EVENT_REPORT, pds_client_active=1 but loc_registered=0 -> 0x0024/25/26 MUST NOT forward! */
    static const char nmea[] = "$GPGGA,000000.00,,,,,0,00,,,M,,M,,*66\r\n";
    sdu_begin(&loc_ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_NMEA);
    sdu_tlv(&loc_ind, 0x01u, nmea, (uint16_t)strlen(nmea));
    fgp_handle_loc_rx(&b, loc_ind.b, loc_ind.n, &act, sc);
    early_ind_before_event_reg += act.host_count;

    sdu_begin(&loc_ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_POSITION_REPORT);
    sdu_u32(&loc_ind, 0x01u, FGL_SESS_IN_PROGRESS);
    fgp_handle_loc_rx(&b, loc_ind.b, loc_ind.n, &act, sc);
    early_ind_before_event_reg += act.host_count;
    CHECK(early_ind_before_event_reg == 0u);

    /* During init, 0x0049(1) does not emit duplicate STOP; probe teardown 0x002B/0x002C does not pollute state */
    sdu_begin(&req, CBQ_QMI_REQUEST, 2u, FGP_PDS_SET_FORCE_RECEIVER_OFF);
    sdu_u8(&req, 0x01u, 1u);
    fgp_handle_pds_request(&b, 0u, 9u, req.b, req.n, &act);
    CHECK(act.loc_count == 0u && b.probe_handover_stop == 1u && b.probe_handover_off == 1u);

    sdu_begin(&loc_ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_ENGINE_STATE);
    sdu_u32(&loc_ind, 0x01u, FGL_ENGINE_ON);
    fgp_handle_loc_rx(&b, loc_ind.b, loc_ind.n, &act, sc);
    CHECK(act.host_count == 0u && b.enabled == 0u && b.tracking == FGP_TRACK_INACTIVE && b.probe_handover_off == 1u);

    /* locationd sends 0x0001 SET_EVENT_REPORT -> loc_registered becomes 1 */
    sdu_begin(&req, CBQ_QMI_REQUEST, 3u, FGP_PDS_SET_EVENT_REPORT);
    sdu_u8(&req, 0x10u, 1u);
    fgp_handle_pds_request(&b, 0u, 9u, req.b, req.n, &act);
    CHECK(b.loc_registered == 1u);

    /* Late 0x0024 tagged with FGL_PROBE_SID(2) MUST NOT forward to locationd even after loc_registered=1 */
    sdu_begin(&loc_ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_POSITION_REPORT);
    sdu_u32(&loc_ind, 0x01u, FGL_SESS_SUCCESS);
    sdu_u8(&loc_ind, 0x02u, FGL_PROBE_SID);
    sdu_u64(&loc_ind, 0x10u, f64_bits(25.0330));
    sdu_u64(&loc_ind, 0x11u, f64_bits(121.5654));
    fgp_handle_loc_rx(&b, loc_ind.b, loc_ind.n, &act, sc);
    CHECK(act.host_count == 0u && b.fixes_ok == 0u);

    /* Probe teardown finishes with 0x002C(FINISHED) after 0x0021(1) -> must NOT clobber enabled=1 or emit 0x0020 IND */
    sdu_begin(&req, CBQ_QMI_REQUEST, 4u, FGP_PDS_SET_GPS_SERVICE_STATE);
    sdu_u8(&req, 0x01u, 1u);
    fgp_handle_pds_request(&b, 0u, 9u, req.b, req.n, &act);
    CHECK(b.enabled == 1u && b.tracking == FGP_TRACK_INACTIVE);

    sdu_begin(&loc_ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_FIX_SESSION_STATE);
    sdu_u32(&loc_ind, 0x01u, FGL_FIX_SESSION_FINISHED);
    fgp_handle_loc_rx(&b, loc_ind.b, loc_ind.n, &act, sc);
    CHECK(b.probe_handover_stop == 0u && b.probe_handover_off == 1u &&
          b.enabled == 1u && b.tracking == FGP_TRACK_INACTIVE && act.host_count == 0u);

    /* Now Bridge starts tracking via 0x0031(1) -> single clean LOC_START(1) */
    sdu_begin(&req, CBQ_QMI_REQUEST, 5u, FGP_PDS_SET_AUTO_TRACKING_STATE);
    sdu_u8(&req, 0x01u, 1u);
    fgp_handle_pds_request(&b, 0u, 9u, req.b, req.n, &act);
    CHECK(act.loc_count == 1u && rd16(act.loc[0].sdu + 3) == FGL_LOC_START);
    sid_v = fgl_find_tlv(act.loc[0].sdu, act.loc[0].len, 0x01u, &tlen);
    CHECK(sid_v && tlen == 1u && sid_v[0] == 1u);
    loc_start_count += act.loc_count;
    CHECK(loc_stop_count == 1u && loc_start_count == 1u);

    /* Subsequent NMEA and Bridge sid=1 position report forward normally */
    sdu_begin(&loc_ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_NMEA);
    sdu_tlv(&loc_ind, 0x01u, nmea, (uint16_t)strlen(nmea));
    fgp_handle_loc_rx(&b, loc_ind.b, loc_ind.n, &act, sc);
    CHECK(act.host_count == 1u);

    sdu_begin(&loc_ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_POSITION_REPORT);
    sdu_u32(&loc_ind, 0x01u, FGL_SESS_SUCCESS);
    sdu_u8(&loc_ind, 0x02u, 1u);
    sdu_u64(&loc_ind, 0x10u, f64_bits(25.0330));
    sdu_u64(&loc_ind, 0x11u, f64_bits(121.5654));
    fgp_handle_loc_rx(&b, loc_ind.b, loc_ind.n, &act, sc);
    CHECK(act.host_count == 1u && b.fixes_ok == 1u);

    /* Direct 0x0049(1) handover path (without prior RESET) stops active probe (FGL_PROBE_SID=2) */
    fgp_init(&b);
    b.probe_active = 1u;
    sdu_begin(&req, CBQ_QMI_REQUEST, 6u, FGP_PDS_SET_FORCE_RECEIVER_OFF);
    sdu_u8(&req, 0x01u, 1u);
    fgp_handle_pds_request(&b, 0u, 9u, req.b, req.n, &act);
    CHECK(b.probe_active == 0u && b.probe_handover_stop == 1u && b.probe_handover_off == 1u);
    CHECK(act.loc_count == 1u && rd16(act.loc[0].sdu + 3) == FGL_LOC_STOP);
    sid_v = fgl_find_tlv(act.loc[0].sdu, act.loc[0].len, 0x01u, &tlen);
    CHECK(sid_v && tlen == 1u && sid_v[0] == FGL_PROBE_SID);

    /* Scenario C2: probe_active=1 -> RESET -> 0x0001 -> 0x0021(1) -> 0x0031(1) -> delayed 0x002C(FINISHED) */
    fgp_init(&b);
    b.probe_active = 1u;
    sdu_begin(&req, CBQ_QMI_REQUEST, 10u, FGP_PDS_RESET);
    fgp_handle_pds_request(&b, 0u, 9u, req.b, req.n, &act);
    CHECK(b.probe_handover_stop == 1u && b.probe_handover_off == 1u);

    sdu_begin(&req, CBQ_QMI_REQUEST, 11u, FGP_PDS_SET_EVENT_REPORT);
    sdu_u8(&req, 0x10u, 1u);
    fgp_handle_pds_request(&b, 0u, 9u, req.b, req.n, &act);

    sdu_begin(&req, CBQ_QMI_REQUEST, 12u, FGP_PDS_SET_GPS_SERVICE_STATE);
    sdu_u8(&req, 0x01u, 1u);
    fgp_handle_pds_request(&b, 0u, 9u, req.b, req.n, &act);
    CHECK(b.enabled == 1u);

    sdu_begin(&req, CBQ_QMI_REQUEST, 13u, FGP_PDS_SET_AUTO_TRACKING_STATE);
    sdu_u8(&req, 0x01u, 1u);
    fgp_handle_pds_request(&b, 0u, 9u, req.b, req.n, &act);
    CHECK(b.tracking == FGP_TRACK_ACTIVE && b.probe_handover_stop == 1u && b.probe_handover_off == 1u);

    /* Delayed probe 0x002C(FINISHED) arrives AFTER 0x0031(1): MUST be absorbed cleanly! */
    sdu_begin(&loc_ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_FIX_SESSION_STATE);
    sdu_u32(&loc_ind, 0x01u, FGL_FIX_SESSION_FINISHED);
    fgp_handle_loc_rx(&b, loc_ind.b, loc_ind.n, &act, sc);
    CHECK(act.handled && b.tracking == FGP_TRACK_ACTIVE && b.enabled == 1u);
    CHECK(b.probe_handover_stop == 0u && b.probe_handover_off == 1u && act.host_count == 0u);

    /* Path 1A: Bridge's own 0x002C(STARTED) preserves probe_handover_off; delayed 0x002B(ENGINE_OFF) is absorbed, then ENGINE_ON keeps ACTIVE */
    sdu_begin(&loc_ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_FIX_SESSION_STATE);
    sdu_u32(&loc_ind, 0x01u, FGL_FIX_SESSION_STARTED);
    fgp_handle_loc_rx(&b, loc_ind.b, loc_ind.n, &act, sc);
    CHECK(b.enabled == 1u && b.tracking == FGP_TRACK_ACTIVE && b.probe_handover_off == 1u && act.host_count == 1u);

    sdu_begin(&loc_ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_ENGINE_STATE);
    sdu_u32(&loc_ind, 0x01u, FGL_ENGINE_OFF);
    fgp_handle_loc_rx(&b, loc_ind.b, loc_ind.n, &act, sc);
    CHECK(act.handled && b.enabled == 1u && b.tracking == FGP_TRACK_ACTIVE &&
          b.probe_handover_off == 0u && act.host_count == 0u);

    sdu_begin(&loc_ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_ENGINE_STATE);
    sdu_u32(&loc_ind, 0x01u, FGL_ENGINE_ON);
    fgp_handle_loc_rx(&b, loc_ind.b, loc_ind.n, &act, sc);
    CHECK(act.handled && b.enabled == 1u && b.tracking == FGP_TRACK_ACTIVE && act.host_count == 1u);
    const uint8_t *psdu1a = act.host[0].frame + CBQ_HDR;
    const uint32_t plen1a = act.host[0].len - CBQ_HDR;
    const uint8_t *t1a = fgl_find_tlv(psdu1a, plen1a, 0x01u, &tlen);
    CHECK(t1a && tlen == 2u && t1a[0] == 1u && t1a[1] == FGP_TRACK_ACTIVE);

    /* Path 1B (Scenario C2 edge): MPSS skips FINISHED and sends STARTED directly, then delayed ENGINE_OFF -> ENGINE_ON */
    fgp_init(&b);
    b.probe_active = 1u;
    sdu_begin(&req, CBQ_QMI_REQUEST, 20u, FGP_PDS_RESET);
    fgp_handle_pds_request(&b, 0u, 9u, req.b, req.n, &act);
    sdu_begin(&req, CBQ_QMI_REQUEST, 21u, FGP_PDS_SET_AUTO_TRACKING_STATE);
    sdu_u8(&req, 0x01u, 1u);
    fgp_handle_pds_request(&b, 0u, 9u, req.b, req.n, &act);
    CHECK(b.tracking == FGP_TRACK_ACTIVE && b.probe_handover_stop == 1u && b.probe_handover_off == 1u);
    sdu_begin(&loc_ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_FIX_SESSION_STATE);
    sdu_u32(&loc_ind, 0x01u, FGL_FIX_SESSION_STARTED);
    fgp_handle_loc_rx(&b, loc_ind.b, loc_ind.n, &act, sc);
    CHECK(b.enabled == 1u && b.tracking == FGP_TRACK_ACTIVE && b.probe_handover_stop == 0u &&
          b.probe_handover_off == 1u && act.host_count == 1u);

    sdu_begin(&loc_ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_ENGINE_STATE);
    sdu_u32(&loc_ind, 0x01u, FGL_ENGINE_OFF);
    fgp_handle_loc_rx(&b, loc_ind.b, loc_ind.n, &act, sc);
    CHECK(act.handled && b.enabled == 1u && b.tracking == FGP_TRACK_ACTIVE &&
          b.probe_handover_off == 0u && act.host_count == 0u);

    sdu_begin(&loc_ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_ENGINE_STATE);
    sdu_u32(&loc_ind, 0x01u, FGL_ENGINE_ON);
    fgp_handle_loc_rx(&b, loc_ind.b, loc_ind.n, &act, sc);
    CHECK(act.handled && b.enabled == 1u && b.tracking == FGP_TRACK_ACTIVE && act.host_count == 1u);
    const uint8_t *psdu1b = act.host[0].frame + CBQ_HDR;
    const uint32_t plen1b = act.host[0].len - CBQ_HDR;
    const uint8_t *t1b = fgl_find_tlv(psdu1b, plen1b, 0x01u, &tlen);
    CHECK(t1b && tlen == 2u && t1b[0] == 1u && t1b[1] == FGP_TRACK_ACTIVE);

    /* Path 1A/1B + 0x0021(0) without Probe ENGINE_OFF: STARTED leaves probe_handover_off=1, 0x0021(0) clears it so real ENGINE_OFF forwards */
    fgp_init(&b);
    b.probe_active = 1u;
    sdu_begin(&req, CBQ_QMI_REQUEST, 25u, FGP_PDS_RESET);
    fgp_handle_pds_request(&b, 0u, 9u, req.b, req.n, &act);
    sdu_begin(&req, CBQ_QMI_REQUEST, 26u, FGP_PDS_SET_AUTO_TRACKING_STATE);
    sdu_u8(&req, 0x01u, 1u);
    fgp_handle_pds_request(&b, 0u, 9u, req.b, req.n, &act);
    sdu_begin(&loc_ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_FIX_SESSION_STATE);
    sdu_u32(&loc_ind, 0x01u, FGL_FIX_SESSION_STARTED);
    fgp_handle_loc_rx(&b, loc_ind.b, loc_ind.n, &act, sc);
    CHECK(b.probe_handover_off == 1u && b.enabled == 1u && b.tracking == FGP_TRACK_ACTIVE);
    sdu_begin(&req, CBQ_QMI_REQUEST, 27u, FGP_PDS_SET_GPS_SERVICE_STATE);
    sdu_u8(&req, 0x01u, 0u);
    fgp_handle_pds_request(&b, 0u, 9u, req.b, req.n, &act);
    CHECK(b.enabled == 0u && b.tracking == FGP_TRACK_INACTIVE && b.probe_handover_off == 0u);
    sdu_begin(&loc_ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_ENGINE_STATE);
    sdu_u32(&loc_ind, 0x01u, FGL_ENGINE_OFF);
    fgp_handle_loc_rx(&b, loc_ind.b, loc_ind.n, &act, sc);
    CHECK(act.handled && b.enabled == 0u && b.tracking == FGP_TRACK_INACTIVE && act.host_count == 1u);

    /* Path 1A/1B + 0x0031(0) + 0x0049(1) without Probe ENGINE_OFF: 0x0049(1) clears probe_handover_off when was_enabled */
    fgp_init(&b);
    b.probe_active = 1u;
    sdu_begin(&req, CBQ_QMI_REQUEST, 28u, FGP_PDS_RESET);
    fgp_handle_pds_request(&b, 0u, 9u, req.b, req.n, &act);
    sdu_begin(&req, CBQ_QMI_REQUEST, 29u, FGP_PDS_SET_AUTO_TRACKING_STATE);
    sdu_u8(&req, 0x01u, 1u);
    fgp_handle_pds_request(&b, 0u, 9u, req.b, req.n, &act);
    sdu_begin(&loc_ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_FIX_SESSION_STATE);
    sdu_u32(&loc_ind, 0x01u, FGL_FIX_SESSION_STARTED);
    fgp_handle_loc_rx(&b, loc_ind.b, loc_ind.n, &act, sc);
    CHECK(b.probe_handover_off == 1u);
    sdu_begin(&req, CBQ_QMI_REQUEST, 291u, FGP_PDS_SET_AUTO_TRACKING_STATE);
    sdu_u8(&req, 0x01u, 0u);
    fgp_handle_pds_request(&b, 0u, 9u, req.b, req.n, &act);
    sdu_begin(&req, CBQ_QMI_REQUEST, 292u, FGP_PDS_SET_FORCE_RECEIVER_OFF);
    sdu_u8(&req, 0x01u, 1u);
    fgp_handle_pds_request(&b, 0u, 9u, req.b, req.n, &act);
    CHECK(b.enabled == 0u && b.tracking == FGP_TRACK_INACTIVE && b.probe_handover_off == 0u);
    sdu_begin(&loc_ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_ENGINE_STATE);
    sdu_u32(&loc_ind, 0x01u, FGL_ENGINE_OFF);
    fgp_handle_loc_rx(&b, loc_ind.b, loc_ind.n, &act, sc);
    CHECK(act.handled && b.enabled == 0u && b.tracking == FGP_TRACK_INACTIVE && act.host_count == 1u);

    /* Scenario C3: probe_active=1 -> RESET -> 0x0001 -> 0x0021(1) -> 0x002C(FINISHED) -> delayed 0x002B(ENGINE_OFF) */
    fgp_init(&b);
    b.probe_active = 1u;
    sdu_begin(&req, CBQ_QMI_REQUEST, 30u, FGP_PDS_RESET);
    fgp_handle_pds_request(&b, 0u, 9u, req.b, req.n, &act);
    sdu_begin(&req, CBQ_QMI_REQUEST, 31u, FGP_PDS_SET_EVENT_REPORT);
    sdu_u8(&req, 0x10u, 1u);
    fgp_handle_pds_request(&b, 0u, 9u, req.b, req.n, &act);
    sdu_begin(&req, CBQ_QMI_REQUEST, 32u, FGP_PDS_SET_GPS_SERVICE_STATE);
    sdu_u8(&req, 0x01u, 1u);
    fgp_handle_pds_request(&b, 0u, 9u, req.b, req.n, &act);
    CHECK(b.enabled == 1u);

    /* 0x002C(FINISHED) clears probe_handover_stop ONLY; probe_handover_off stays 1 */
    sdu_begin(&loc_ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_FIX_SESSION_STATE);
    sdu_u32(&loc_ind, 0x01u, FGL_FIX_SESSION_FINISHED);
    fgp_handle_loc_rx(&b, loc_ind.b, loc_ind.n, &act, sc);
    CHECK(b.probe_handover_stop == 0u && b.probe_handover_off == 1u && act.host_count == 0u);

    /* Delayed Probe 0x002B(ENGINE_OFF) arrives AFTER 0x002C(FINISHED): MUST NOT clobber enabled=1! */
    sdu_begin(&loc_ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_ENGINE_STATE);
    sdu_u32(&loc_ind, 0x01u, FGL_ENGINE_OFF);
    fgp_handle_loc_rx(&b, loc_ind.b, loc_ind.n, &act, sc);
    CHECK(act.handled && b.enabled == 1u && b.probe_handover_off == 0u && act.host_count == 0u);

    /* Real 0x0021(0) power-off clears probe_handover_off and forwards subsequent 0x002B(ENGINE_OFF) */
    fgp_init(&b);
    b.probe_active = 1u;
    sdu_begin(&req, CBQ_QMI_REQUEST, 40u, FGP_PDS_RESET);
    fgp_handle_pds_request(&b, 0u, 9u, req.b, req.n, &act);
    sdu_begin(&req, CBQ_QMI_REQUEST, 41u, FGP_PDS_SET_GPS_SERVICE_STATE);
    sdu_u8(&req, 0x01u, 1u);
    fgp_handle_pds_request(&b, 0u, 9u, req.b, req.n, &act);
    sdu_begin(&req, CBQ_QMI_REQUEST, 42u, FGP_PDS_SET_AUTO_TRACKING_STATE);
    sdu_u8(&req, 0x01u, 1u);
    fgp_handle_pds_request(&b, 0u, 9u, req.b, req.n, &act);
    CHECK(b.enabled == 1u && b.tracking == FGP_TRACK_ACTIVE);

    sdu_begin(&req, CBQ_QMI_REQUEST, 43u, FGP_PDS_SET_GPS_SERVICE_STATE);
    sdu_u8(&req, 0x01u, 0u);
    fgp_handle_pds_request(&b, 0u, 9u, req.b, req.n, &act);
    CHECK(b.enabled == 0u && b.tracking == FGP_TRACK_INACTIVE &&
          b.probe_handover_stop == 0u && b.probe_handover_off == 0u);
    CHECK(act.loc_count == 1u && rd16(act.loc[0].sdu + 3) == FGL_LOC_STOP);

    sdu_begin(&loc_ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_ENGINE_STATE);
    sdu_u32(&loc_ind, 0x01u, FGL_ENGINE_OFF);
    fgp_handle_loc_rx(&b, loc_ind.b, loc_ind.n, &act, sc);
    CHECK(act.handled && b.enabled == 0u && b.tracking == FGP_TRACK_INACTIVE && act.host_count == 1u);
    const uint8_t *psdu = act.host[0].frame + CBQ_HDR;
    uint32_t plen = act.host[0].len - CBQ_HDR;
    CHECK(rd16(psdu + 3) == FGP_PDS_GET_GPS_SERVICE_STATE);
    const uint8_t *t1 = fgl_find_tlv(psdu, plen, 0x01u, &tlen);
    CHECK(t1 && tlen == 2u && t1[0] == 0u && t1[1] == FGP_TRACK_INACTIVE);

    free(sc);
}

/* CBQ_MAX_CLIENTS=128 slot leak, private_service isolation, and VoLTE/data coexistence */
static void test_qmux_slot_coexistence(void) {
    struct cbq_bridge q;
    uint8_t frame[256], out[512];
    cbq_init(&q);
    q.client_limit = CBQ_MAX_CLIENTS;
    CHECK(CBQ_MAX_CLIENTS == 128u);

    /* Populate modem servers: WDS(1), DMS(2), NAS(3), VOICE(9), LOC(10), IMSS(12), IMSA(21) */
    static const uint8_t svcs[7] = { 0x01u, 0x02u, 0x03u, 0x09u, 0x10u, 0x12u, 0x21u };
    for (unsigned i = 0; i < 7u; i++) {
        CHECK(cbq_server_new(&q, svcs[i], 1u, 0u, 0x10u + i) == 1);
    }

    /* With pds_bridge == 0 (default), service 0x06 MUST be refused with error 31 */
    uint32_t flen = make_ctl_get_cid(frame, sizeof frame, 1u, 0x06u);
    struct cbq_result r = cbq_from_host(&q, 0u, frame, flen, out, sizeof out);
    CHECK(r.kind == CBQ_TO_HOST && rd16(out + CBQ_HDR + CBQ_CTL_HDR + 5u) == CBQ_ERR_INVALID_SERVICE_TYPE);

    /* Allocate 70 cellular/VoLTE slots (simulating active data + VoLTE baseline) */
    uint8_t cellular_cids[70];
    for (unsigned i = 0; i < 70u; i++) {
        const uint8_t svc = svcs[i % 7u];
        flen = make_ctl_get_cid(frame, sizeof frame, (uint8_t)(i + 2u), svc);
        r = cbq_from_host(&q, 0u, frame, flen, out, sizeof out);
        CHECK(r.kind == CBQ_TO_HOST && rd16(out + CBQ_HDR + CBQ_CTL_HDR + 5u) == 0u);
        cellular_cids[i] = r.cid;
    }

    /* Enable pds_bridge = 1 and repeatedly open/request/release/sync 0x06 on qmux1 (200 cycles) */
    q.pds_bridge = 1u;
    for (unsigned iter = 0; iter < 200u; iter++) {
        flen = make_ctl_get_cid(frame, sizeof frame, (uint8_t)(iter + 1u), 0x06u);
        r = cbq_from_host(&q, 0u, frame, flen, out, sizeof out);
        CHECK(r.kind == CBQ_TO_HOST && rd16(out + CBQ_HDR + CBQ_CTL_HDR + 5u) == 0u);
        const uint8_t pds_cid = r.cid;

        SduBuf sdu;
        sdu_begin(&sdu, CBQ_QMI_REQUEST, 1u, FGP_PDS_GET_GPS_SERVICE_STATE);
        flen = make_qmux_req(frame, sizeof frame, 0x06u, pds_cid, &sdu);
        r = cbq_from_host(&q, 0u, frame, flen, out, sizeof out);
        CHECK(r.kind == CBQ_PRIVATE && r.service == 0x06u && r.cid == pds_cid);

        if ((iter & 1u) == 0u) {
            flen = make_ctl_rel_cid(frame, sizeof frame, 99u, 0x06u, pds_cid);
            r = cbq_from_host(&q, 0u, frame, flen, out, sizeof out);
            CHECK(r.kind == CBQ_TO_HOST && rd16(out + CBQ_HDR + CBQ_CTL_HDR + 5u) == 0u);
        } else {
            /* CTL_SYNC on channel 1 (qmux2) to test channel isolation, then release on qmux1 */
            flen = make_ctl_sync(frame, sizeof frame, 100u);
            r = cbq_from_host(&q, 1u, frame, flen, out, sizeof out);
            CHECK(r.kind == CBQ_TO_HOST);
            flen = make_ctl_rel_cid(frame, sizeof frame, 101u, 0x06u, pds_cid);
            r = cbq_from_host(&q, 0u, frame, flen, out, sizeof out);
            CHECK(r.kind == CBQ_TO_HOST);
        }
        /* NEVER emit QRTR DEL_CLIENT for virtual PDS 0x06! */
        CHECK(cbq_next_release(&q) == 0u);
    }

    /* Verify all 70 cellular/VoLTE slots remain untouched */
    unsigned used = 0;
    for (unsigned i = 0; i < CBQ_MAX_CLIENTS; i++) used += q.client[i].used;
    CHECK(used == 70u);
    for (unsigned i = 0; i < 70u; i++) {
        CHECK(cbq_client_find(&q, 0u, svcs[i % 7u], cellular_cids[i]) >= 0);
    }
}

/* M6 Iter 4: QMI_LOC startup unlock FSM (pre-reg 0x0021(0) -> 0x00BA -> 0x003B w/ 0x43+err3 backoff -> optional 0x003A) */
static void test_g3_unlock_fsm(void) {
    struct fgl_unlock_fsm fsm;
    struct fgl_unlock_event ev;
    uint8_t sdu[128];
    uint16_t msg_id = 0u;
    uint32_t len = 0u, tlen = 0u;
    SduBuf rx;

    /* Case A: REG_EVENTS(0) -> REG_MASTER -> GET_ENGINE_LOCK hits 0x43 + err=3 twin, retries after 50ms, returns lock=NONE (skips 0x003A) */
    fgl_unlock_init(&fsm);
    CHECK(!fgl_unlock_is_done(&fsm) && fgl_unlock_is_send_step(&fsm) && fsm.step == FGL_ULK_PRE_REG_SEND);
    len = fgl_unlock_prepare_tx(&fsm, 1u, sdu, sizeof sdu, &msg_id);
    CHECK(len == 36u && msg_id == FGL_LOC_REG_EVENTS);
    const uint8_t *v = fgl_find_tlv(sdu, len, 0x01u, &tlen);
    CHECK(v && tlen == 8u && rd64(v) == 0ULL);
    v = fgl_find_tlv(sdu, len, 0x10u, &tlen);
    CHECK(v && tlen == 4u && memcmp(v, "MHAL", 4u) == 0);
    v = fgl_find_tlv(sdu, len, 0x11u, &tlen);
    CHECK(v && tlen == 4u && rd32(v) == 1u);
    v = fgl_find_tlv(sdu, len, 0x12u, &tlen);
    CHECK(v && tlen == 1u && v[0] == 0u);

    fgl_unlock_commit_tx(&fsm, 1u, 100u);
    CHECK(fsm.step == FGL_ULK_PRE_REG_WAIT && fgl_unlock_prepare_tx(&fsm, 2u, sdu, sizeof sdu, &msg_id) == 0u);
    sdu_begin(&rx, CBQ_QMI_RESPONSE, 1u, FGL_LOC_REG_EVENTS);
    sdu_result(&rx, 0u);
    fgl_unlock_handle_rx(&fsm, rx.b, rx.n, 102u, &ev);
    CHECK(ev.kind == FGL_ULK_EV_PRE_REG_RESP && ev.err == 0u && fsm.step == FGL_ULK_MASTER_SEND);

    len = fgl_unlock_prepare_tx(&fsm, 2u, sdu, sizeof sdu, &msg_id);
    CHECK(len == 14u && msg_id == FGL_LOC_REG_MASTER_CLIENT);
    fgl_unlock_commit_tx(&fsm, 2u, 102u);
    sdu_begin(&rx, CBQ_QMI_RESPONSE, 2u, FGL_LOC_REG_MASTER_CLIENT);
    sdu_result(&rx, 0u);
    fgl_unlock_handle_rx(&fsm, rx.b, rx.n, 104u, &ev);
    CHECK(ev.kind == FGL_ULK_EV_MASTER_RESP && ev.err == 0u && fsm.step == FGL_ULK_MASTER_WAIT);
    sdu_begin(&rx, CBQ_QMI_INDICATION, 0u, FGL_LOC_REG_MASTER_CLIENT);
    sdu_u32(&rx, 0x01u, FGL_IND_SUCCESS);
    fgl_unlock_handle_rx(&fsm, rx.b, rx.n, 105u, &ev);
    CHECK(ev.kind == FGL_ULK_EV_MASTER_IND && ev.ind_status == FGL_IND_SUCCESS && fsm.step == FGL_ULK_GET_LOCK_SEND);

    len = fgl_unlock_prepare_tx(&fsm, 3u, sdu, sizeof sdu, &msg_id);
    CHECK(len == 14u && msg_id == FGL_LOC_GET_ENGINE_LOCK);
    fgl_unlock_commit_tx(&fsm, 3u, 105u);
    sdu_begin(&rx, CBQ_QMI_RESPONSE, 3u, FGL_LOC_GET_ENGINE_LOCK);
    sdu_result(&rx, FGL_QMI_ERR_SESSION_OWNERSHIP);
    fgl_unlock_handle_rx(&fsm, rx.b, rx.n, 107u, &ev);
    CHECK(ev.kind == FGL_ULK_EV_GET_LOCK_RETRY && ev.err == 0x43u && ev.retry == 1u);
    CHECK(fsm.step == FGL_ULK_GET_LOCK_BACKOFF && fsm.t0_ms == 107u);

    /* Twin err=3 on same txn MUST be absorbed cleanly without touching retry count or t0_ms */
    sdu_begin(&rx, CBQ_QMI_RESPONSE, 3u, FGL_LOC_GET_ENGINE_LOCK);
    sdu_result(&rx, FGL_QMI_ERR_INTERNAL);
    fgl_unlock_handle_rx(&fsm, rx.b, rx.n, 110u, &ev);
    CHECK(ev.kind == FGL_ULK_EV_ABSORBED_ERR3 && fsm.absorbed_err3_count == 1u);
    CHECK(fsm.step == FGL_ULK_GET_LOCK_BACKOFF && fsm.get_lock_retries == 1u && fsm.t0_ms == 107u);

    /* 49 ms elapsed: still in backoff */
    fgl_unlock_tick(&fsm, 156u, &ev);
    CHECK(fsm.step == FGL_ULK_GET_LOCK_BACKOFF && fgl_unlock_prepare_tx(&fsm, 4u, sdu, sizeof sdu, &msg_id) == 0u);
    /* 50 ms elapsed: ready to retry */
    fgl_unlock_tick(&fsm, 157u, &ev);
    CHECK(fsm.step == FGL_ULK_GET_LOCK_SEND);
    len = fgl_unlock_prepare_tx(&fsm, 4u, sdu, sizeof sdu, &msg_id);
    CHECK(len == 14u && msg_id == FGL_LOC_GET_ENGINE_LOCK);
    fgl_unlock_commit_tx(&fsm, 4u, 157u);
    sdu_begin(&rx, CBQ_QMI_RESPONSE, 4u, FGL_LOC_GET_ENGINE_LOCK);
    sdu_result(&rx, 0u);
    fgl_unlock_handle_rx(&fsm, rx.b, rx.n, 159u, &ev);
    CHECK(ev.kind == FGL_ULK_EV_GET_LOCK_RESP && ev.err == 0u);
    sdu_begin(&rx, CBQ_QMI_INDICATION, 0u, FGL_LOC_GET_ENGINE_LOCK);
    sdu_u32(&rx, 0x01u, FGL_IND_SUCCESS);
    sdu_u32(&rx, 0x10u, FGL_LOCK_NONE);
    fgl_unlock_handle_rx(&fsm, rx.b, rx.n, 160u, &ev);
    CHECK(ev.kind == FGL_ULK_EV_GET_LOCK_IND && ev.lock_type == FGL_LOCK_NONE && fgl_unlock_is_done(&fsm));

    /* Case B: GET_ENGINE_LOCK reports locked (MT) -> SET_ENGINE_LOCK (32B) w/ 0x43+err=3 retry -> success */
    fgl_unlock_init(&fsm);
    fsm.step = FGL_ULK_GET_LOCK_WAIT; fsm.cur_txn = 10u; fsm.t0_ms = 200u;
    sdu_begin(&rx, CBQ_QMI_INDICATION, 0u, FGL_LOC_GET_ENGINE_LOCK);
    sdu_u32(&rx, 0x01u, FGL_IND_SUCCESS);
    sdu_u32(&rx, 0x10u, FGL_LOCK_MT);
    fgl_unlock_handle_rx(&fsm, rx.b, rx.n, 205u, &ev);
    CHECK(ev.kind == FGL_ULK_EV_GET_LOCK_IND && fsm.step == FGL_ULK_SET_LOCK_SEND);

    len = fgl_unlock_prepare_tx(&fsm, 11u, sdu, sizeof sdu, &msg_id);
    CHECK(len == 32u && msg_id == FGL_LOC_SET_ENGINE_LOCK);
    v = fgl_find_tlv(sdu, len, 0x01u, &tlen); CHECK(v && tlen == 4u && rd32(v) == FGL_LOCK_NONE);
    v = fgl_find_tlv(sdu, len, 0x10u, &tlen); CHECK(v && tlen == 4u && rd32(v) == 3u);
    v = fgl_find_tlv(sdu, len, 0x12u, &tlen); CHECK(v && tlen == 8u && rd64(v) == 0ULL);
    fgl_unlock_commit_tx(&fsm, 11u, 205u);
    sdu_begin(&rx, CBQ_QMI_RESPONSE, 11u, FGL_LOC_SET_ENGINE_LOCK);
    sdu_result(&rx, FGL_QMI_ERR_SESSION_OWNERSHIP);
    fgl_unlock_handle_rx(&fsm, rx.b, rx.n, 207u, &ev);
    CHECK(ev.kind == FGL_ULK_EV_SET_LOCK_RETRY && ev.retry == 1u && fsm.step == FGL_ULK_SET_LOCK_BACKOFF);
    sdu_begin(&rx, CBQ_QMI_RESPONSE, 11u, FGL_LOC_SET_ENGINE_LOCK);
    sdu_result(&rx, FGL_QMI_ERR_INTERNAL);
    fgl_unlock_handle_rx(&fsm, rx.b, rx.n, 207u, &ev);
    CHECK(ev.kind == FGL_ULK_EV_ABSORBED_ERR3 && fsm.step == FGL_ULK_SET_LOCK_BACKOFF);
    fgl_unlock_tick(&fsm, 257u, &ev);
    CHECK(fsm.step == FGL_ULK_SET_LOCK_SEND);
    len = fgl_unlock_prepare_tx(&fsm, 12u, sdu, sizeof sdu, &msg_id);
    CHECK(len == 32u && msg_id == FGL_LOC_SET_ENGINE_LOCK);
    fgl_unlock_commit_tx(&fsm, 12u, 257u);
    sdu_begin(&rx, CBQ_QMI_RESPONSE, 12u, FGL_LOC_SET_ENGINE_LOCK);
    sdu_result(&rx, 0u);
    fgl_unlock_handle_rx(&fsm, rx.b, rx.n, 259u, &ev);
    CHECK(ev.kind == FGL_ULK_EV_SET_LOCK_RESP && ev.err == 0u);
    sdu_begin(&rx, CBQ_QMI_INDICATION, 0u, FGL_LOC_SET_ENGINE_LOCK);
    sdu_u32(&rx, 0x01u, FGL_IND_SUCCESS);
    fgl_unlock_handle_rx(&fsm, rx.b, rx.n, 260u, &ev);
    CHECK(ev.kind == FGL_ULK_EV_SET_LOCK_IND && fgl_unlock_is_done(&fsm));

    /* Case C1: Permanent 0x43 + err=3 exhaustion on GET_ENGINE_LOCK gracefully degrades to DONE */
    fgl_unlock_init(&fsm);
    fsm.step = FGL_ULK_GET_LOCK_SEND;
    uint32_t now = 1000u;
    for (uint32_t attempt = 0u; attempt <= FGL_UNLOCK_MAX_RETRIES; attempt++) {
        uint16_t t = (uint16_t)(20u + attempt);
        len = fgl_unlock_prepare_tx(&fsm, t, sdu, sizeof sdu, &msg_id);
        CHECK(len == 14u && msg_id == FGL_LOC_GET_ENGINE_LOCK);
        fgl_unlock_commit_tx(&fsm, t, now);
        sdu_begin(&rx, CBQ_QMI_RESPONSE, t, FGL_LOC_GET_ENGINE_LOCK);
        sdu_result(&rx, FGL_QMI_ERR_SESSION_OWNERSHIP);
        fgl_unlock_handle_rx(&fsm, rx.b, rx.n, now + 2u, &ev);
        if (attempt < FGL_UNLOCK_MAX_RETRIES) {
            CHECK(ev.kind == FGL_ULK_EV_GET_LOCK_RETRY && fsm.step == FGL_ULK_GET_LOCK_BACKOFF);
        } else {
            CHECK(ev.kind == FGL_ULK_EV_GET_LOCK_EXHAUSTED && fgl_unlock_is_done(&fsm));
        }
        /* Companion err=3 absorbed even after transitioning to DONE on final attempt */
        sdu_begin(&rx, CBQ_QMI_RESPONSE, t, FGL_LOC_GET_ENGINE_LOCK);
        sdu_result(&rx, FGL_QMI_ERR_INTERNAL);
        fgl_unlock_handle_rx(&fsm, rx.b, rx.n, now + 2u, &ev);
        CHECK(ev.kind == FGL_ULK_EV_ABSORBED_ERR3);
        if (attempt < FGL_UNLOCK_MAX_RETRIES) {
            now += 2u + FGL_UNLOCK_RETRY_DELAY_MS;
            fgl_unlock_tick(&fsm, now, &ev);
            CHECK(fsm.step == FGL_ULK_GET_LOCK_SEND);
        }
    }
    CHECK(fsm.absorbed_err3_count == FGL_UNLOCK_MAX_RETRIES + 1u && fgl_unlock_is_done(&fsm));

    /* Case C2: Permanent 0x43 exhaustion on SET_ENGINE_LOCK gracefully degrades to DONE */
    fgl_unlock_init(&fsm);
    fsm.step = FGL_ULK_SET_LOCK_SEND;
    now = 2000u;
    for (uint32_t attempt = 0u; attempt <= FGL_UNLOCK_MAX_RETRIES; attempt++) {
        uint16_t t = (uint16_t)(40u + attempt);
        len = fgl_unlock_prepare_tx(&fsm, t, sdu, sizeof sdu, &msg_id);
        CHECK(len == 32u && msg_id == FGL_LOC_SET_ENGINE_LOCK);
        fgl_unlock_commit_tx(&fsm, t, now);
        sdu_begin(&rx, CBQ_QMI_RESPONSE, t, FGL_LOC_SET_ENGINE_LOCK);
        sdu_result(&rx, FGL_QMI_ERR_SESSION_OWNERSHIP);
        fgl_unlock_handle_rx(&fsm, rx.b, rx.n, now + 1u, &ev);
        if (attempt < FGL_UNLOCK_MAX_RETRIES) {
            CHECK(ev.kind == FGL_ULK_EV_SET_LOCK_RETRY && fsm.step == FGL_ULK_SET_LOCK_BACKOFF);
            now += 1u + FGL_UNLOCK_RETRY_DELAY_MS;
            fgl_unlock_tick(&fsm, now, &ev);
            CHECK(fsm.step == FGL_ULK_SET_LOCK_SEND);
        } else {
            CHECK(ev.kind == FGL_ULK_EV_SET_LOCK_EXHAUSTED && fgl_unlock_is_done(&fsm));
        }
    }

    /* Case D: 3000ms timeouts across all WAIT states + PRE_REG error + twin err=3 never block */
    fgl_unlock_init(&fsm);
    fgl_unlock_commit_tx(&fsm, 50u, 100u);
    fgl_unlock_tick(&fsm, 3100u, &ev);
    CHECK(ev.kind == FGL_ULK_EV_TIMEOUT && fsm.step == FGL_ULK_MASTER_SEND);
    fgl_unlock_commit_tx(&fsm, 51u, 3100u);
    fgl_unlock_tick(&fsm, 6100u, &ev);
    CHECK(ev.kind == FGL_ULK_EV_TIMEOUT && fsm.step == FGL_ULK_GET_LOCK_SEND);
    fgl_unlock_commit_tx(&fsm, 52u, 6100u);
    fgl_unlock_tick(&fsm, 9100u, &ev);
    CHECK(ev.kind == FGL_ULK_EV_TIMEOUT && fgl_unlock_is_done(&fsm));

    fgl_unlock_init(&fsm);
    fgl_unlock_commit_tx(&fsm, 60u, 0u);
    sdu_begin(&rx, CBQ_QMI_RESPONSE, 60u, FGL_LOC_REG_EVENTS);
    sdu_result(&rx, 0x30u);
    fgl_unlock_handle_rx(&fsm, rx.b, rx.n, 5u, &ev);
    CHECK(ev.kind == FGL_ULK_EV_PRE_REG_RESP && ev.err == 0x30u && fsm.step == FGL_ULK_MASTER_SEND);
    sdu_begin(&rx, CBQ_QMI_RESPONSE, 60u, FGL_LOC_REG_EVENTS);
    sdu_result(&rx, FGL_QMI_ERR_INTERNAL);
    fgl_unlock_handle_rx(&fsm, rx.b, rx.n, 6u, &ev);
    CHECK(ev.kind == FGL_ULK_EV_ABSORBED_ERR3 && fsm.step == FGL_ULK_MASTER_SEND);
}

/* Fuzz malformed/truncated TLVs (>= 12,000 iterations) */
static void test_fuzz_malformed(void) {
    uint32_t rng = 0x13572468u;
    struct fgp_bridge b;
    struct fgp_actions act;
    struct fgp_scratch *sc = (struct fgp_scratch *)malloc(sizeof *sc);
    CHECK(sc != NULL);
    fgp_init(&b);
    fgp_set_pds_client(&b, 1u, 0u, 9u);

    uint8_t *buf = (uint8_t *)malloc(512u);
    CHECK(buf != NULL);

    for (unsigned iter = 0; iter < 12000u; iter++) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        const uint32_t len = (rng % 320u);
        for (uint32_t j = 0; j < len; j++) {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            buf[j] = (uint8_t)(rng & 0xffu);
        }
        /* Force interesting headers on half the iterations */
        if ((iter & 1u) == 0u && len >= 7u) {
            buf[0] = (iter & 2u) ? CBQ_QMI_REQUEST : CBQ_QMI_INDICATION;
            static const uint16_t interesting[] = {
                0x0000u, 0x0001u, 0x0020u, 0x0021u, 0x0024u, 0x0025u, 0x0026u,
                0x0028u, 0x0029u, 0x002Au, 0x002Bu, 0x002Cu, 0x0031u, 0x0038u, 0x003Du, 0x004Au
            };
            fgl_wr16(buf + 3, interesting[iter % (sizeof interesting / sizeof interesting[0])]);
            if ((iter & 4u) == 0u) fgl_wr16(buf + 5, len - 7u);
        }
        struct fgl_pos_report pos;
        struct fgl_sv_info sv;
        char nmea[128];
        uint32_t nlen = 0, st32 = 0, extra = 0;
        int32_t est = 0;
        uint16_t mid = 0;
        (void)fgl_parse_pos_report(buf, len, &pos);
        (void)fgl_parse_sv_info(buf, len, &sv);
        (void)fgl_parse_nmea(buf, len, nmea, sizeof nmea, &nlen);
        (void)fgl_parse_engine_state(buf, len, &est);
        (void)fgl_parse_fix_session_state(buf, len, &st32);
        (void)fgl_parse_inject_time_req(buf, len);
        (void)fgl_parse_inject_orbits_req(buf, len, &st32, &extra);
        (void)fgl_parse_two_stage_ind(buf, len, &mid, &st32, &extra);
        struct fgl_unlock_fsm u_fuzz;
        struct fgl_unlock_event u_ev;
        fgl_unlock_init(&u_fuzz);
        u_fuzz.step = (uint8_t)(iter % (FGL_ULK_DONE + 1u));
        u_fuzz.cur_txn = (len >= 3u) ? rd16(buf + 1) : 1u;
        fgl_unlock_handle_rx(&u_fuzz, buf, len, iter, &u_ev);
        fgp_handle_pds_request(&b, 0u, 9u, buf, len, &act);
        fgp_handle_loc_rx(&b, buf, len, &act, sc);
    }
    free(buf);
    free(sc);
}

/* XTRA 0x0028/0x0029 consumption + indoor mock_fix fallback & real-fix suppression */
static void test_xtra_and_mock_fix(void) {
    struct fgp_bridge b;
    struct fgp_actions act;
    struct fgp_scratch *sc = (struct fgp_scratch *)malloc(sizeof *sc);
    CHECK(sc != NULL);
    fgp_init(&b);
    fgp_set_pds_client(&b, 1u, 0u, 9u);
    b.loc_registered = 1u;
    b.enabled = 1u;
    b.tracking = FGP_TRACK_ACTIVE;

    /* 1. 0x0028 INJECT_TIME_REQ without cached_utc_ms -> handled=1, unknown=0, pending_time_inject=1 */
    SduBuf ind;
    sdu_begin(&ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_INJECT_TIME_REQ);
    fgp_handle_loc_rx(&b, ind.b, ind.n, &act, sc);
    CHECK(act.handled == 1u && act.unknown == 0u && b.unknown_reqs == 0u);
    CHECK(act.time_inject_requested == 1u && b.pending_time_inject == 1u && b.inject_time_reqs == 1u);
    CHECK(act.loc_count == 0u);

    /* With valid cached_utc_ms -> emits QMI_LOC 0x0038 INJECT_UTC_TIME immediately and clears pending */
    fgp_set_utc_hint(&b, 1795523696789ULL);
    fgp_handle_loc_rx(&b, ind.b, ind.n, &act, sc);
    CHECK(act.handled == 1u && act.unknown == 0u && b.unknown_reqs == 0u);
    CHECK(act.time_inject_requested == 1u && b.pending_time_inject == 0u && b.inject_time_reqs == 2u);
    CHECK(act.loc_count == 1u && rd16(act.loc[0].sdu + 3) == FGL_LOC_INJECT_UTC_TIME);
    uint32_t tlen = 0u;
    const uint8_t *utc_v = fgl_find_tlv(act.loc[0].sdu, act.loc[0].len, 0x01u, &tlen);
    CHECK(utc_v && tlen == 8u && rd64(utc_v) == 1795523696789ULL);

    /* 2. 0x0029 INJECT_ORBITS_REQ (empty & with allowedSizes TLV 0x01) -> handled=1, unknown=0 */
    sdu_begin(&ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_INJECT_ORBITS_REQ);
    fgp_handle_loc_rx(&b, ind.b, ind.n, &act, sc);
    CHECK(act.handled == 1u && act.unknown == 0u && b.unknown_reqs == 0u);
    CHECK(act.orbits_inject_requested == 1u && b.inject_orbits_reqs == 1u);

    uint8_t orb_sz[8];
    fgl_wr32(orb_sz + 0, 65536u);
    fgl_wr32(orb_sz + 4, 1024u);
    sdu_tlv(&ind, 0x01u, orb_sz, sizeof orb_sz);
    fgp_handle_loc_rx(&b, ind.b, ind.n, &act, sc);
    CHECK(act.handled == 1u && act.unknown == 0u && b.unknown_reqs == 0u);
    CHECK(b.inject_orbits_reqs == 2u && b.xtra_max_file_bytes == 65536u && b.xtra_max_part_bytes == 1024u);

    /* 3. mock_fix disabled by default: never emits even when tracking is active */
    struct fgp_mock_cfg mcfg;
    memset(&mcfg, 0, sizeof mcfg);
    CHECK(b.mock_enabled == 0u);
    CHECK(fgp_mock_step(&b, &mcfg, 1000u, 1795523696789ULL, &act, sc) == 0);
    CHECK(act.host_count == 0u && b.mock_fixes == 0u);

    /* Enable mock_fix, verify guard when tracking is INACTIVE */
    b.mock_enabled = 1u;
    b.tracking = FGP_TRACK_INACTIVE;
    CHECK(fgp_mock_step(&b, &mcfg, 1000u, 1795523696789ULL, &act, sc) == 0);

    /* Tracking ACTIVE, no real fix -> injects compliant 0x0024 -> QMI_PDS TLV 0x13 (105B) */
    b.tracking = FGP_TRACK_ACTIVE;
    CHECK(fgp_mock_step(&b, &mcfg, 1000u, 1795523696789ULL, &act, sc) == 1);
    CHECK(act.host_count == 1u && b.mock_fixes == 1u && b.fixes_ok == 1u);
    const uint8_t *psdu = act.host[0].frame + CBQ_HDR;
    uint32_t plen = act.host[0].len - CBQ_HDR;
    const uint8_t *t12 = fgl_find_tlv(psdu, plen, 0x12u, &tlen);
    CHECK(t12 && tlen == 1u && t12[0] == FGL_SESS_SUCCESS);
    const uint8_t *t13 = fgl_find_tlv(psdu, plen, 0x13u, &tlen);
    CHECK(t13 && tlen == FGP_TLV13_LEN);
    CHECK(rd64(t13 + 0x1b) == FGP_MOCK_DEFAULT_LAT_BITS && rd64(t13 + 0x23) == FGP_MOCK_DEFAULT_LON_BITS);
    CHECK(rd64(t13 + 0x0f) == 1795523696789ULL && rd32(t13 + 0x3f) == FGP_MOCK_DEFAULT_HUNC_BITS);

    /* Pacing guard: < 1000ms does not re-inject; >= 1000ms injects with custom coords */
    CHECK(fgp_mock_step(&b, &mcfg, 1500u, 1795523697289ULL, &act, sc) == 0);
    mcfg.lat_bits = f64_bits(35.6762);
    mcfg.lon_bits = f64_bits(139.6503);
    CHECK(fgp_mock_step(&b, &mcfg, 2000u, 1795523697789ULL, &act, sc) == 1);
    CHECK(b.mock_fixes == 2u);
    psdu = act.host[0].frame + CBQ_HDR; plen = act.host[0].len - CBQ_HDR;
    t13 = fgl_find_tlv(psdu, plen, 0x13u, &tlen);
    CHECK(t13 && tlen == FGP_TLV13_LEN);
    CHECK(rd64(t13 + 0x1b) == f64_bits(35.6762) && rd64(t13 + 0x23) == f64_bits(139.6503));

    /* Real satellite fix suppresses mock_fix for FGP_MOCK_REAL_HOLDOFF_MS (5000ms) */
    sdu_begin(&ind, CBQ_QMI_INDICATION, 0u, FGL_LOC_EVENT_POSITION_REPORT);
    sdu_u32(&ind, 0x01u, FGL_SESS_SUCCESS);
    sdu_u8(&ind, 0x02u, 1u);
    sdu_u64(&ind, 0x10u, f64_bits(25.0400));
    sdu_u64(&ind, 0x11u, f64_bits(121.5700));
    fgp_note_now_ms(&b, 5000u);
    fgp_handle_loc_rx(&b, ind.b, ind.n, &act, sc);
    CHECK(b.real_fix_seen == 1u && b.last_real_fix_ms == 5000u);
    CHECK(fgp_mock_step(&b, &mcfg, 6000u, 1795523701000ULL, &act, sc) == 0);
    CHECK(fgp_mock_step(&b, &mcfg, 9999u, 1795523704999ULL, &act, sc) == 0);
    CHECK(fgp_mock_step(&b, &mcfg, 10000u, 1795523705000ULL, &act, sc) == 1);
    CHECK(b.mock_fixes == 3u);

    /* Invalid / NaN / out-of-range mock coordinates rejected safely */
    uint8_t bad_sdu[256];
    CHECK(fgl_build_mock_pos_report(bad_sdu, sizeof bad_sdu, 1u, f64_bits(90.5), f64_bits(121.0),
                                    1795523696000ULL, FGP_MOCK_DEFAULT_HUNC_BITS, FGP_MOCK_DEFAULT_ALTE_BITS,
                                    FGP_MOCK_DEFAULT_ALTS_BITS, FGP_MOCK_DEFAULT_VUNC_BITS, FGP_MOCK_DEFAULT_TUNC_BITS, 1u) == 0u);
    CHECK(fgl_build_mock_pos_report(bad_sdu, sizeof bad_sdu, 1u, f64_bits(25.0), f64_bits(-181.0),
                                    1795523696000ULL, FGP_MOCK_DEFAULT_HUNC_BITS, FGP_MOCK_DEFAULT_ALTE_BITS,
                                    FGP_MOCK_DEFAULT_ALTS_BITS, FGP_MOCK_DEFAULT_VUNC_BITS, FGP_MOCK_DEFAULT_TUNC_BITS, 1u) == 0u);
    CHECK(fgl_build_mock_pos_report(bad_sdu, sizeof bad_sdu, 1u, 0x7ff8000000000000ULL, f64_bits(121.0),
                                    1795523696000ULL, FGP_MOCK_DEFAULT_HUNC_BITS, FGP_MOCK_DEFAULT_ALTE_BITS,
                                    FGP_MOCK_DEFAULT_ALTS_BITS, FGP_MOCK_DEFAULT_VUNC_BITS, FGP_MOCK_DEFAULT_TUNC_BITS, 1u) == 0u);
    CHECK(fgl_build_mock_pos_report(bad_sdu, sizeof bad_sdu, 1u, f64_bits(25.0), f64_bits(121.0),
                                    1795523696000ULL, f32_bits(-1.0f), FGP_MOCK_DEFAULT_ALTE_BITS,
                                    FGP_MOCK_DEFAULT_ALTS_BITS, FGP_MOCK_DEFAULT_VUNC_BITS, FGP_MOCK_DEFAULT_TUNC_BITS, 1u) == 0u);

    free(sc);
}

int main(void) {
    test_g1_loc_codec();
    test_g2_bridge_and_translation();
    test_mode3_probe_bridge_handover();
    test_g3_unlock_fsm();
    test_qmux_slot_coexistence();
    test_xtra_and_mock_fix();
    test_fuzz_malformed();
    printf("PASS fuxi gnss proto & bridge: %u checks (G1/G2/G3-unlock-fsm/TLV11-29B/mode3-handover/8-init-gates/xtra-0x28-0x29/mock-fix/12000-fuzz/128-slot-coexistence)\n", g_checks);
    return 0;
}
'''


def verify_no_fp_tokens():
    for hdr_name in ("fuxi_gnss_loc.h", "fuxi_gnss_pds_bridge.h"):
        text = (MODEM_DIR / hdr_name).read_text(encoding="utf-8")
        # Strip C comments before scanning for float/double keywords
        stripped = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
        stripped = re.sub(r"//[^\n]*", "", stripped)
        hits = re.findall(r"\b(?:float|double)\b", stripped)
        if hits:
            raise AssertionError(f"{hdr_name} contains forbidden kernel FP types: {hits}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()

    verify_no_fp_tokens()
    tools = runpy.run_path(str(KSRC / "tools/test-fuxi-audio-prep.py"))
    compiler, environment = tools["compiler_environment"]()
    microsoft = Path(compiler[0].strip('"')).name.lower() in ("cl", "cl.exe")

    with tempfile.TemporaryDirectory(prefix="fuxi-gnss-") as tmpdir:
        d = Path(tmpdir)
        src = d / "gnss_test.c"
        src.write_text(C_TEST_SOURCE, encoding="utf-8", newline="\n")
        binary = d / ("gnss_test.exe" if os.name == "nt" else "gnss_test")
        if microsoft:
            flags = ["/nologo", "/std:c11", "/W4", "/WX", "/Zi", "/Od", "/MD", "/utf-8"]
            if args.sanitize:
                flags.append("/fsanitize=address")
            cmd = compiler + flags + [f"/I{MODEM_DIR}", str(src), f"/Fe:{binary}"]
        else:
            flags = ["-std=c11", "-g", "-Wall", "-Wextra", "-Werror", "-O1"]
            if args.sanitize:
                san = "-fsanitize=address,undefined,leak" if sys.platform.startswith("linux") else "-fsanitize=address,undefined"
                flags += [san, "-fno-omit-frame-pointer"]
                if sys.platform.startswith("linux"):
                    environment = dict(environment)
                    environment["ASAN_OPTIONS"] = "detect_leaks=1"
            cmd = compiler + flags + ["-I", str(MODEM_DIR), str(src), "-o", str(binary)]
        subprocess.run(cmd, cwd=d, env=environment, check=True, timeout=120)
        subprocess.run([str(binary)], cwd=d, env=environment, check=True, timeout=120)


if __name__ == "__main__":
    main()
