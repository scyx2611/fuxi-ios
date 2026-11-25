/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Pure C11 bidirectional QMI_PDS (0x06) <-> QMI_LOC (0x10) protocol bridge fo
 * iOS 16.5 (20H392) locationd (CLEurekaController / CLQmiPdsController / CLQmiPdsMessenger)
 * running over SM8550 MPSS QMI_LOC v2 (10:2@81).
 *
 * Kernel-safe (-mgeneral-regs-only): zero floating-point types or instructions.
 * Portable and host-tested with ASan/UBSan/LSan.
 */
#ifndef FUXI_GNSS_PDS_BRIDGE_H
#define FUXI_GNSS_PDS_BRIDGE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "coachz_bb_qmux.h"
#include "fuxi_gnss_loc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FGP_SVC_PDS 0x06u

/* QMI_PDS 0x06 message IDs (CLQmiPdsController / CLQmiPdsMessenger) */
#define FGP_PDS_RESET                          0x0000u
#define FGP_PDS_SET_EVENT_REPORT               0x0001u /* REQ & IND */
#define FGP_PDS_GET_GPS_SERVICE_STATE          0x0020u /* REQ & IND */
#define FGP_PDS_SET_GPS_SERVICE_STATE          0x0021u
#define FGP_PDS_SET_NMEA_CONFIG                0x0027u
#define FGP_PDS_SET_DEFAULT_TRACKING_SESSION   0x002Au
#define FGP_PDS_FORCE_XTRA_DOWNLOAD            0x002Du
#define FGP_PDS_SET_AUTO_TRACKING_STATE        0x0031u
#define FGP_PDS_RESET_GPS_SERVICE_DATA         0x0034u
#define FGP_PDS_GET_GPS_SERVICE_VERSION        0x0036u
#define FGP_PDS_INJECT_XTRA_DATA               0x0037u
#define FGP_PDS_INJECT_POSITION_DATA           0x0038u
#define FGP_PDS_SEND_NI_RESPONSE               0x003Cu
#define FGP_PDS_INJECT_ABSOLUTE_TIME_REF       0x003Du
#define FGP_PDS_GET_DPO_CONFIG                 0x003Fu
#define FGP_PDS_SET_DPO_CONFIG                 0x0040u
#define FGP_PDS_SET_SPI_STREAMING_REPORT       0x0046u
#define FGP_PDS_SET_SPI_STATUS                 0x0047u
#define FGP_PDS_SET_FORCE_RECEIVER_OFF         0x0049u
#define FGP_PDS_INJECT_SENSOR_DATA             0x0052u
#define FGP_PDS_INJECT_TIME_SYNC_DATA          0x0053u
#define FGP_PDS_SET_SENSOR_CONFIG              0x0055u
#define FGP_PDS_SET_NAVIGATION_CONFIG          0x0057u
#define FGP_PDS_EXT_SET_CELLDB_CONTROL         0x005Fu
#define FGP_PDS_READY_IND                      0x0060u
#define FGP_PDS_INJECT_MOTION_DATA             0x0061u
#define FGP_PDS_SET_GNSS_RF_CONFIG             0x0065u
#define FGP_PDS_INJECT_COURSE_OVER_GROUND      0x0066u
#define FGP_PDS_INJECT_SUPL_CERTIFICATE        0x0067u
#define FGP_PDS_DELETE_SUPL_CERTIFICATE        0x0068u
#define FGP_PDS_SET_AGNSS_POSITIONING_MODES    0x0074u
#define FGP_PDS_SET_APN_PROFILES               0x0078u
#define FGP_PDS_INJECT_VEHICLE_DATA            0x007Bu
#define FGP_PDS_GET_LPP_CONFIG                 0x007Du
#define FGP_PDS_SET_LPP_CONFIG                 0x007Eu
#define FGP_PDS_SET_SUPL_UDP_CONFIG            0x0083u
#define FGP_PDS_CANCEL_ONGOING_SESSION         0x0084u
#define FGP_PDS_SET_TIME_TRANSFER_CONFIG       0x0085u
#define FGP_PDS_INJECT_CLIENT_TIME_NS_REQ      0x0087u
#define FGP_PDS_SET_EMERGENCY_SUPPORT_CONFIG   0x008Au
#define FGP_PDS_SET_BLOCKLIST_SV               0x008Bu
#define FGP_PDS_INJECT_SPEED_DATA              0x008Eu
#define FGP_PDS_SET_OTDOA_CONFIG               0x0093u
#define FGP_PDS_SET_GNSS_SIGNAL_ENVIRONMENT    0x0096u
#define FGP_PDS_INJECT_SPEED_INEQUALITY        0x0097u
#define FGP_PDS_SET_CALIBRATION_STATE_CONFIG   0x0098u
#define FGP_PDS_SET_SPOOFING_REPORT_CONFIG     0x009Fu
#define FGP_PDS_SET_GNSS_ENGINE_MONITOR_CFG    0x00A1u
#define FGP_PDS_SET_GNSS_REPORTING_CONFIG      0x00A3u
#define FGP_PDS_SET_E911_OVERRIDE_CONFIG       0x00A9u
#define FGP_PDS_SET_MULTIBAND_STATE            0x00ADu
#define FGP_PDS_SET_WWAN_L5_BLANKING_CONTROL   0x00B7u
#define FGP_PDS_GET_TIMING_ADVANCE_REPORT      0x00E0u
#define FGP_PDS_EXT_SET_SV_IN_FIX_REPORT       0x5557u
#define FGP_PDS_EXT_SET_DPO_STATUS_REPORT      0x5558u
#define FGP_PDS_EXT_SET_IMD_CONFIG             0x5559u

/* PDS Tracking Session State (TLV 0x01 byte[1] in 0x0020) */
#define FGP_TRACK_UNKNOWN   0u
#define FGP_TRACK_INACTIVE  1u
#define FGP_TRACK_ACTIVE    2u

/* PDS Operating Mode */
#define FGP_OP_STANDALONE   0u
#define FGP_OP_MS_BASED     1u
#define FGP_OP_MS_ASSISTED  2u

/* PDS TLV 0x13 (105 bytes) validity bits (bits 0..22) */
#define FGP_VAL_CAL       (1u << 0)
#define FGP_VAL_UTC       (1u << 1)
#define FGP_VAL_SECS      (1u << 2)
#define FGP_VAL_TUNC      (1u << 3)
#define FGP_VAL_LAT       (1u << 4)
#define FGP_VAL_LON       (1u << 5)
#define FGP_VAL_ALTE      (1u << 6)
#define FGP_VAL_ALTS      (1u << 7)
#define FGP_VAL_HSPD      (1u << 8)
#define FGP_VAL_VSPD      (1u << 9)
#define FGP_VAL_HDING     (1u << 10)
#define FGP_VAL_HUNC      (1u << 11)
#define FGP_VAL_H_SMAJ    (1u << 12)
#define FGP_VAL_H_SMIN    (1u << 13)
#define FGP_VAL_H_AZ      (1u << 14)
#define FGP_VAL_VUNC      (1u << 15)
#define FGP_VAL_HVEL_UNC  (1u << 16)
#define FGP_VAL_VVEL_UNC  (1u << 17)
#define FGP_VAL_HCONF     (1u << 18)
#define FGP_VAL_PDOP      (1u << 19)
#define FGP_VAL_HDOP      (1u << 20)
#define FGP_VAL_VDOP      (1u << 21)
#define FGP_VAL_MODE      (1u << 22)

#define FGP_TLV13_LEN        105u
#define FGP_MAX_SV           63u
#define FGP_HOST_FRAME_MAX   1536u
#define FGP_MAX_HOST_OUT     2u
#define FGP_MAX_LOC_OUT      6u

struct fgp_host_msg {
    uint32_t len;
    uint8_t frame[FGP_HOST_FRAME_MAX];
};

struct fgp_loc_msg {
    uint32_t len;
    uint8_t sdu[256];
};

#define FGP_MOCK_REAL_HOLDOFF_MS  5000u
#define FGP_MOCK_DEFAULT_UTC_MS   1795523696000ULL
#define FGP_MOCK_DEFAULT_LAT_BITS 0x403908b1dd5d3dc9ULL /* 25.033964 deg */
#define FGP_MOCK_DEFAULT_LON_BITS 0x405e64203e63e8deULL /* 121.564468 deg */
#define FGP_MOCK_DEFAULT_HUNC_BITS 0x40a00000u          /* 5.0 m */
#define FGP_MOCK_DEFAULT_ALTE_BITS 0x420c0000u          /* 35.0 m */
#define FGP_MOCK_DEFAULT_ALTS_BITS 0x41900000u          /* 18.0 m */
#define FGP_MOCK_DEFAULT_VUNC_BITS 0x40c00000u          /* 6.0 m */
#define FGP_MOCK_DEFAULT_TUNC_BITS 0x41200000u          /* 10.0 ms */

struct fgp_mock_cfg {
    uint64_t lat_bits;
    uint64_t lon_bits;
    uint32_t hunc_bits;
    uint32_t alt_ell_bits;
    uint32_t alt_msl_bits;
    uint32_t vunc_bits;
    uint32_t tunc_ms_bits;
    uint32_t interval_ms;
};

struct fgp_actions {
    uint32_t host_count;
    struct fgp_host_msg host[FGP_MAX_HOST_OUT];
    uint32_t loc_count;
    struct fgp_loc_msg loc[FGP_MAX_LOC_OUT];
    uint8_t handled;
    uint8_t unknown;
    uint8_t time_inject_requested;
    uint8_t orbits_inject_requested;
};

struct fgp_scratch {
    struct fgl_sv_info sv;
    struct fgl_pos_report pos;
    uint8_t tlvs[3u + 6u + 18u * FGP_MAX_SV];
    uint8_t t13[FGP_TLV13_LEN];
    char nmea[FGL_MAX_NMEA + 1u];
};

struct fgp_bridge {
    uint8_t enabled;            /* 0=disabled, 1=enabled */
    uint8_t tracking;           /* FGP_TRACK_INACTIVE (1) or FGP_TRACK_ACTIVE (2) */
    uint8_t op_mode;            /* FGP_OP_STANDALONE / MS_BASED / MS_ASSISTED */
    uint8_t sid;                /* QMI_LOC session ID (1) */
    uint32_t interval_ms;       /* [1000, 600000] */
    uint32_t accuracy_m;
    uint32_t timeout_s;
    uint64_t allowed_services;
    uint64_t loc_event_mask;
    uint8_t loc_registered;
    uint8_t pds_client_active;
    uint8_t pds_channel;
    uint8_t pds_cid;
    uint16_t loc_txn;
    struct fgl_two_stage_tracker two_stage;
    uint32_t pds_rx;
    uint32_t pds_tx;
    uint32_t loc_tx;
    uint32_t loc_rx;
    uint32_t fixes_ok;
    uint32_t fixes_in_prog;
    uint32_t sv_reports;
    uint32_t nmea_reports;
    uint32_t unknown_reqs;
    uint16_t last_unknown_msg;
    uint8_t loc_engine_state;   /* 0=unknown, 1=on, 2=off */
    uint8_t probe_active;       /* 1 if standalone probe started FGL_PROBE_SID (2) */
    uint8_t probe_handover_stop;/* 1 while awaiting probe STOP(2) 0x002C(FINISHED) */
    uint8_t probe_handover_off; /* 1 while awaiting probe STOP(2) 0x002B(ENGINE_OFF) */
    uint32_t inject_time_reqs;
    uint32_t inject_orbits_reqs;
    uint32_t xtra_max_file_bytes;
    uint32_t xtra_max_part_bytes;
    uint8_t pending_time_inject;
    uint8_t pending_orbits_notice;
    uint64_t cached_utc_ms;
    uint8_t mock_enabled;
    uint8_t in_mock_inject;
    uint8_t real_fix_seen;
    uint32_t last_rx_ms;
    uint32_t last_real_fix_ms;
    uint32_t last_mock_ms;
    uint32_t mock_fixes;
};

static inline void fgp_init(struct fgp_bridge *b)
{
    if (!b) return;
    memset(b, 0, sizeof *b);
    b->tracking = FGP_TRACK_INACTIVE;
    b->sid = 1u;
    b->interval_ms = 1000u;
    b->loc_event_mask = FGL_EVT_BRIDGE_MASK;
}

static inline uint16_t fgp_next_txn(struct fgp_bridge *b)
{
    uint16_t t = (uint16_t)(b->loc_txn + 1u);
    if (t == 0 || t >= 0xfff0u) t = 1u;
    b->loc_txn = t;
    return t;
}

static inline void fgp_set_utc_hint(struct fgp_bridge *b, uint64_t utc_ms)
{
    if (b && utc_ms >= 1700000000000ULL) b->cached_utc_ms = utc_ms;
}

static inline void fgp_note_now_ms(struct fgp_bridge *b, uint32_t now_ms)
{
    if (b) b->last_rx_ms = now_ms;
}

static inline void fgp_set_pds_client(struct fgp_bridge *b, uint8_t active, uint8_t channel, uint8_t cid)
{
    if (!b) return;
    b->pds_client_active = active ? 1u : 0u;
    b->pds_channel = channel;
    b->pds_cid = cid;
}

static inline void fgp_push_host(struct fgp_actions *act, uint32_t len)
{
    if (len && act->host_count < FGP_MAX_HOST_OUT) act->host_count++;
}

static inline void fgp_emit_loc(struct fgp_bridge *b, struct fgp_actions *act, const uint8_t *sdu, uint32_t len)
{
    if (!len || len > sizeof act->loc[0].sdu || act->loc_count >= FGP_MAX_LOC_OUT) return;
    struct fgp_loc_msg *m = &act->loc[act->loc_count++];
    memcpy(m->sdu, sdu, len);
    m->len = len;
    b->loc_tx++;
    if (fgl_is_two_stage_msg(fgl_rd16(sdu + 3))) {
        fgl_tracker_note_tx(&b->two_stage, fgl_rd16(sdu + 3), fgl_rd16(sdu + 1));
    }
}

static inline void fgp_emit_pds_resp(struct fgp_bridge *b, struct fgp_actions *act, const uint8_t *req_sdu,
                                     uint8_t cid, uint32_t err, const uint8_t *tlvs, uint32_t tlv_len)
{
    if (act->host_count >= FGP_MAX_HOST_OUT) return;
    struct fgp_host_msg *h = &act->host[act->host_count];
    h->len = cbq_private_answer(req_sdu, FGP_SVC_PDS, cid, err, tlvs, tlv_len, h->frame, sizeof h->frame);
    if (h->len) {
        fgp_push_host(act, h->len);
        b->pds_tx++;
    }
}

static inline void fgp_emit_pds_ind(struct fgp_bridge *b, struct fgp_actions *act, uint32_t msg_id,
                                    uint8_t cid, const uint8_t *tlvs, uint32_t tlv_len)
{
    if (act->host_count >= FGP_MAX_HOST_OUT) return;
    struct fgp_host_msg *h = &act->host[act->host_count];
    h->len = cbq_private_indication(msg_id, FGP_SVC_PDS, cid, tlvs, tlv_len, h->frame, sizeof h->frame);
    if (h->len) {
        fgp_push_host(act, h->len);
        b->pds_tx++;
    }
}

static inline void fgp_emit_service_state_ind(struct fgp_bridge *b, struct fgp_actions *act, uint8_t cid)
{
    uint8_t tlv[5];
    const uint8_t st[2] = {
        (uint8_t)(b->enabled ? 1u : 0u),
        (uint8_t)(b->tracking ? b->tracking : FGP_TRACK_INACTIVE)
    };
    const uint32_t tl = fgl_tlv(tlv, sizeof tlv, 0x01u, st, 2u);
    fgp_emit_pds_ind(b, act, FGP_PDS_GET_GPS_SERVICE_STATE, cid, tlv, tl);
}

/* Howard Hinnant civil-from-days UTC ms -> calendar components (pure integer) */
struct fgp_cal_time {
    uint16_t year;
    uint8_t month;      /* 1..12 */
    uint8_t wday;       /* 0=Sun..6=Sat */
    uint8_t day;        /* 1..31 */
    uint8_t hour;       /* 0..23 */
    uint8_t minute;     /* 0..59 */
    uint8_t second;     /* 0..59 */
    uint16_t millisec;  /* 0..999 */
};

static inline void fgp_utc_ms_to_cal(uint64_t utc_ms, struct fgp_cal_time *out)
{
    const uint64_t sec_total = utc_ms / 1000ULL;
    out->millisec = (uint16_t)(utc_ms % 1000ULL);
    int64_t z = (int64_t)(sec_total / 86400ULL);
    uint32_t rem = (uint32_t)(sec_total % 86400ULL);
    out->hour = (uint8_t)(rem / 3600u);
    rem %= 3600u;
    out->minute = (uint8_t)(rem / 60u);
    out->second = (uint8_t)(rem % 60u);
    out->wday = (uint8_t)((z + 4) % 7); /* 1970-01-01 = Thursday (4) */

    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const uint64_t doe = (uint64_t)(z - era * 146097);
    const uint64_t yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    int64_t y = (int64_t)yoe + era * 400;
    const uint64_t doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const uint64_t mp = (5u * doy + 2u) / 153u;
    out->day = (uint8_t)(doy - (153u * mp + 2u) / 5u + 1u);
    const int m_adj = mp < 10u ? 3 : -9;
    out->month = (uint8_t)((int)mp + m_adj);
    y += (out->month <= 2u) ? 1 : 0;
    out->year = y < 0 ? 0u : (y > 65535 ? 65535u : (uint16_t)y);
}

static inline uint32_t fgp_loc_op_mode_from_pds(uint8_t pds_mode)
{
    switch (pds_mode) {
    case FGP_OP_MS_BASED:   return FGL_OP_MODE_MSB;
    case FGP_OP_MS_ASSISTED: return FGL_OP_MODE_MSA;
    case FGP_OP_STANDALONE:
    default:                return FGL_OP_MODE_STANDALONE;
    }
}

/* Heading range check matching locationd headingRangeCheck (0x1004ff28c):
 * [0.0, 359.99] -> valid; (359.99, 360.0] -> clamp to +0.0f; <0 or >360 or NaN -> invalid. */
static inline int fgp_sanitize_heading_bits(uint32_t bits, uint32_t *out_bits)
{
    if (!fgl_f32_finite(bits)) return 0;
    if ((bits & 0x7fffffffu) == 0u) { *out_bits = 0u; return 1; } /* normalize -0.0f */
    if (bits & 0x80000000u) return 0; /* negative */
    if (bits > 0x43b40000u) return 0; /* > 360.0f */
    if (bits > 0x43b3feb8u) { *out_bits = 0u; return 1; } /* (359.99f, 360.0f] -> 0.0f */
    *out_bits = bits;
    return 1;
}

/* Avoid uint8_t truncation collision with GLONASS (65..96) for Galileo 301..336 / >255 svId */
static inline uint8_t fgp_map_prn_u8(uint16_t sv_id)
{
    if (sv_id <= 255u) return (uint8_t)sv_id;
    if (sv_id >= 301u && sv_id <= 336u) return (uint8_t)(101u + (sv_id - 301u));
    return (uint8_t)(140u + ((sv_id - 256u) % 50u));
}

static inline int fgp_is_benign_success_msg(uint16_t msg_id)
{
    switch (msg_id) {
    case FGP_PDS_FORCE_XTRA_DOWNLOAD:
    case FGP_PDS_INJECT_XTRA_DATA:
    case FGP_PDS_SEND_NI_RESPONSE:
    case FGP_PDS_SET_DPO_CONFIG:
    case FGP_PDS_SET_SPI_STREAMING_REPORT:
    case FGP_PDS_SET_SPI_STATUS:
    case FGP_PDS_SET_FORCE_RECEIVER_OFF:
    case FGP_PDS_INJECT_SENSOR_DATA:
    case FGP_PDS_INJECT_TIME_SYNC_DATA:
    case FGP_PDS_SET_SENSOR_CONFIG:
    case FGP_PDS_SET_NAVIGATION_CONFIG:
    case FGP_PDS_EXT_SET_CELLDB_CONTROL:
    case FGP_PDS_INJECT_MOTION_DATA:
    case FGP_PDS_SET_GNSS_RF_CONFIG:
    case FGP_PDS_INJECT_COURSE_OVER_GROUND:
    case FGP_PDS_INJECT_SUPL_CERTIFICATE:
    case FGP_PDS_DELETE_SUPL_CERTIFICATE:
    case FGP_PDS_SET_AGNSS_POSITIONING_MODES:
    case FGP_PDS_SET_APN_PROFILES:
    case FGP_PDS_INJECT_VEHICLE_DATA:
    case FGP_PDS_SET_LPP_CONFIG:
    case FGP_PDS_SET_SUPL_UDP_CONFIG:
    case FGP_PDS_SET_EMERGENCY_SUPPORT_CONFIG:
    case FGP_PDS_SET_BLOCKLIST_SV:
    case FGP_PDS_INJECT_SPEED_DATA:
    case FGP_PDS_SET_OTDOA_CONFIG:
    case FGP_PDS_SET_GNSS_SIGNAL_ENVIRONMENT:
    case FGP_PDS_INJECT_SPEED_INEQUALITY:
    case FGP_PDS_SET_CALIBRATION_STATE_CONFIG:
    case FGP_PDS_SET_SPOOFING_REPORT_CONFIG:
    case FGP_PDS_SET_GNSS_ENGINE_MONITOR_CFG:
    case FGP_PDS_SET_GNSS_REPORTING_CONFIG:
    case FGP_PDS_SET_E911_OVERRIDE_CONFIG:
    case FGP_PDS_SET_MULTIBAND_STATE:
    case FGP_PDS_SET_WWAN_L5_BLANKING_CONTROL:
    case FGP_PDS_GET_TIMING_ADVANCE_REPORT:
    case FGP_PDS_EXT_SET_SV_IN_FIX_REPORT:
    case FGP_PDS_EXT_SET_DPO_STATUS_REPORT:
    case FGP_PDS_EXT_SET_IMD_CONFIG:
        return 1;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------ PDS 0x06 -> LOC 0x10 */
static inline void fgp_handle_pds_request(struct fgp_bridge *b, uint8_t channel, uint8_t cid,
                                          const uint8_t *sdu, uint32_t len, struct fgp_actions *act)
{
    if (!b || !act) return;
    memset(act, 0, sizeof *act);
    if (!sdu || !fgl_sdu_ok(sdu, len) || sdu[0] != CBQ_QMI_REQUEST) return;

    b->pds_rx++;
    fgp_set_pds_client(b, 1u, channel, cid);
    act->handled = 1u;

    const uint16_t msg_id = fgl_rd16(sdu + 3);
    uint8_t loc_sdu[256];
    uint32_t tlen = 0;

    switch (msg_id) {
    case FGP_PDS_RESET: {
        const uint8_t was_tracking = (b->tracking == FGP_TRACK_ACTIVE) ? 1u : 0u;
        b->enabled = 0u;
        b->tracking = FGP_TRACK_INACTIVE;
        b->op_mode = FGP_OP_STANDALONE;
        b->interval_ms = 1000u;
        b->loc_registered = 0u;
        if (was_tracking) {
            b->probe_handover_stop = 0u;
            b->probe_handover_off = 0u;
            const uint32_t ln = fgl_build_stop(loc_sdu, sizeof loc_sdu, fgp_next_txn(b), b->sid ? b->sid : 1u);
            fgp_emit_loc(b, act, loc_sdu, ln);
        }
        if (b->probe_active) {
            b->probe_active = 0u;
            b->probe_handover_stop = 1u;
            b->probe_handover_off = 1u;
            const uint32_t ln = fgl_build_stop(loc_sdu, sizeof loc_sdu, fgp_next_txn(b), FGL_PROBE_SID);
            fgp_emit_loc(b, act, loc_sdu, ln);
        }
        fgp_emit_pds_resp(b, act, sdu, cid, 0u, NULL, 0u);
        /* Cancel CLQmiPdsController 1s init timer immediately via 0x0060 pdsReady */
        fgp_emit_pds_ind(b, act, FGP_PDS_READY_IND, cid, NULL, 0u);
        break;
    }
    case FGP_PDS_GET_GPS_SERVICE_STATE: {
        uint8_t tlv[5];
        const uint8_t st[2] = {
        (uint8_t)(b->enabled ? 1u : 0u),
        (uint8_t)(b->tracking ? b->tracking : FGP_TRACK_INACTIVE)
    };
        const uint32_t tl = fgl_tlv(tlv, sizeof tlv, 0x01u, st, 2u);
        fgp_emit_pds_resp(b, act, sdu, cid, 0u, tlv, tl);
        break;
    }
    case FGP_PDS_GET_GPS_SERVICE_VERSION: {
        uint8_t tlv[5];
        const uint8_t ver[2] = { 2u, 0u };
        const uint32_t tl = fgl_tlv(tlv, sizeof tlv, 0x01u, ver, 2u);
        fgp_emit_pds_resp(b, act, sdu, cid, 0u, tlv, tl);
        break;
    }
    case FGP_PDS_GET_LPP_CONFIG: {
        uint8_t tlv[7];
        const uint32_t tl = fgl_tlv_u32(tlv, sizeof tlv, 0x01u, 0u);
        fgp_emit_pds_resp(b, act, sdu, cid, 0u, tlv, tl);
        break;
    }
    case FGP_PDS_SET_EVENT_REPORT: {
        uint64_t mask = FGL_EVT_DEFAULT_MASK;
        const uint8_t *v;
        if ((v = fgl_find_tlv(sdu, len, 0x13u, &tlen)) && tlen >= 1u && v[0]) mask |= FGL_EVT_INJECT_ORBITS_REQ;
        if ((v = fgl_find_tlv(sdu, len, 0x14u, &tlen)) && tlen >= 1u && v[0]) mask |= FGL_EVT_INJECT_TIME_REQ;
        if ((v = fgl_find_tlv(sdu, len, 0x15u, &tlen)) && tlen >= 1u && v[0]) mask |= FGL_EVT_INJECT_POS_REQ;
        if ((v = fgl_find_tlv(sdu, len, 0x18u, &tlen)) && tlen >= 1u && v[0]) mask |= FGL_EVT_NI_NOTIFY;
        if ((v = fgl_find_tlv(sdu, len, 0x23u, &tlen)) && tlen >= 1u && v[0]) mask |= FGL_EVT_INJECT_ORBITS_REQ;
        b->loc_event_mask = mask;
        b->loc_registered = 1u;
        fgp_emit_pds_resp(b, act, sdu, cid, 0u, NULL, 0u);
        const uint32_t ln = fgl_build_reg_events(loc_sdu, sizeof loc_sdu, fgp_next_txn(b), mask);
        fgp_emit_loc(b, act, loc_sdu, ln);
        break;
    }
    case FGP_PDS_SET_FORCE_RECEIVER_OFF: {
        const uint8_t *v = fgl_find_tlv(sdu, len, 0x01u, &tlen);
        const uint8_t off = (v && tlen >= 1u && v[0]) ? 1u : 0u;
        if (off) {
            const uint8_t was_enabled = b->enabled;
            const uint8_t was_tracking = (b->tracking == FGP_TRACK_ACTIVE) ? 1u : 0u;
            const uint8_t was_probe = b->probe_active ? 1u : 0u;
            b->enabled = 0u;
            b->tracking = FGP_TRACK_INACTIVE;
            if (was_tracking) {
                b->probe_handover_stop = 0u;
                b->probe_handover_off = 0u;
                const uint32_t ln = fgl_build_stop(loc_sdu, sizeof loc_sdu, fgp_next_txn(b), b->sid ? b->sid : 1u);
                fgp_emit_loc(b, act, loc_sdu, ln);
            } else if (was_enabled && !was_probe) {
                b->probe_handover_off = 0u;
            }
            if (was_probe) {
                b->probe_active = 0u;
                b->probe_handover_stop = 1u;
                b->probe_handover_off = 1u;
                const uint32_t ln = fgl_build_stop(loc_sdu, sizeof loc_sdu, fgp_next_txn(b), FGL_PROBE_SID);
                fgp_emit_loc(b, act, loc_sdu, ln);
            }
        }
        fgp_emit_pds_resp(b, act, sdu, cid, 0u, NULL, 0u);
        break;
    }
    case FGP_PDS_SET_GPS_SERVICE_STATE: {
        const uint8_t *v = fgl_find_tlv(sdu, len, 0x01u, &tlen);
        if (!v || tlen < 1u) {
            fgp_emit_pds_resp(b, act, sdu, cid, CBQ_ERR_MISSING_ARGUMENT, NULL, 0u);
            break;
        }
        b->enabled = v[0] ? 1u : 0u;
        const uint8_t *m = fgl_find_tlv(sdu, len, 0x10u, &tlen);
        if (m && tlen >= 8u) b->allowed_services = fgl_rd64(m);
        if (!b->enabled) {
            b->probe_handover_off = 0u;
            if (b->tracking == FGP_TRACK_ACTIVE) {
                b->probe_handover_stop = 0u;
                b->tracking = FGP_TRACK_INACTIVE;
                const uint32_t ln = fgl_build_stop(loc_sdu, sizeof loc_sdu, fgp_next_txn(b), b->sid ? b->sid : 1u);
                fgp_emit_loc(b, act, loc_sdu, ln);
            }
        }
        fgp_emit_pds_resp(b, act, sdu, cid, 0u, NULL, 0u);
        fgp_emit_service_state_ind(b, act, cid);
        break;
    }
    case FGP_PDS_SET_DEFAULT_TRACKING_SESSION: {
        const uint8_t *v = fgl_find_tlv(sdu, len, 0x01u, &tlen);
        if (!v || tlen < 10u) {
            fgp_emit_pds_resp(b, act, sdu, cid, CBQ_ERR_MISSING_ARGUMENT, NULL, 0u);
            break;
        }
        b->op_mode = v[0];
        b->timeout_s = v[1];
        /* Active CLQmiPdsMessenger uses 12B (+4/+8); legacy CLQmiPdsManager uses 10B (+2/+6) */
        const uint32_t interval_s = (tlen >= 12u) ? fgl_rd32(v + 4) : fgl_rd32(v + 2);
        b->accuracy_m = (tlen >= 12u) ? fgl_rd32(v + 8) : fgl_rd32(v + 6);
        uint32_t ms = (interval_s > 600u) ? 600000u : (interval_s * 1000u);
        if (ms < 1000u) ms = 1000u;
        b->interval_ms = ms;
        fgp_emit_pds_resp(b, act, sdu, cid, 0u, NULL, 0u);
        const uint32_t ln = fgl_build_set_op_mode(loc_sdu, sizeof loc_sdu, fgp_next_txn(b),
                                                  fgp_loc_op_mode_from_pds(b->op_mode));
        fgp_emit_loc(b, act, loc_sdu, ln);
        break;
    }
    case FGP_PDS_SET_AUTO_TRACKING_STATE: {
        const uint8_t *v = fgl_find_tlv(sdu, len, 0x01u, &tlen);
        if (!v || tlen < 1u) {
            fgp_emit_pds_resp(b, act, sdu, cid, CBQ_ERR_MISSING_ARGUMENT, NULL, 0u);
            break;
        }
        const uint8_t en = v[0] ? 1u : 0u;
        if (en) {
            if (b->probe_active) {
                b->probe_active = 0u;
                b->probe_handover_stop = 1u;
                b->probe_handover_off = 1u;
                const uint32_t pn = fgl_build_stop(loc_sdu, sizeof loc_sdu, fgp_next_txn(b), FGL_PROBE_SID);
                fgp_emit_loc(b, act, loc_sdu, pn);
            }
            if (!b->loc_registered) {
                b->loc_registered = 1u;
                const uint32_t rn = fgl_build_reg_events(loc_sdu, sizeof loc_sdu, fgp_next_txn(b),
                                                         b->loc_event_mask ? b->loc_event_mask : FGL_EVT_BRIDGE_MASK);
                fgp_emit_loc(b, act, loc_sdu, rn);
            }
            b->enabled = 1u;
            b->tracking = FGP_TRACK_ACTIVE;
            const uint32_t sn = fgl_build_start(loc_sdu, sizeof loc_sdu, fgp_next_txn(b),
                                                b->sid ? b->sid : 1u, FGL_RECURRENCE_PERIODIC,
                                                FGL_INTERMEDIATE_ENABLE,
                                                b->interval_ms ? b->interval_ms : 1000u);
            fgp_emit_loc(b, act, loc_sdu, sn);
        } else {
            if (b->tracking == FGP_TRACK_ACTIVE) {
                b->probe_handover_stop = 0u;
                const uint32_t xn = fgl_build_stop(loc_sdu, sizeof loc_sdu, fgp_next_txn(b), b->sid ? b->sid : 1u);
                fgp_emit_loc(b, act, loc_sdu, xn);
            }
            b->tracking = FGP_TRACK_INACTIVE;
        }
        fgp_emit_pds_resp(b, act, sdu, cid, 0u, NULL, 0u);
        fgp_emit_service_state_ind(b, act, cid);
        break;
    }
    case FGP_PDS_CANCEL_ONGOING_SESSION: {
        if (b->tracking == FGP_TRACK_ACTIVE) {
            b->probe_handover_stop = 0u;
            const uint32_t xn = fgl_build_stop(loc_sdu, sizeof loc_sdu, fgp_next_txn(b), b->sid ? b->sid : 1u);
            fgp_emit_loc(b, act, loc_sdu, xn);
        }
        b->tracking = FGP_TRACK_INACTIVE;
        fgp_emit_pds_resp(b, act, sdu, cid, 0u, NULL, 0u);
        fgp_emit_service_state_ind(b, act, cid);
        break;
    }
    case FGP_PDS_GET_DPO_CONFIG: {
        uint8_t tlv[4];
        const uint32_t tl = fgl_tlv_u8(tlv, sizeof tlv, 0x10u, 0u);
        fgp_emit_pds_resp(b, act, sdu, cid, 0u, tlv, tl);
        break;
    }
    case FGP_PDS_SET_NMEA_CONFIG: {
        fgp_emit_pds_resp(b, act, sdu, cid, 0u, NULL, 0u);
        break;
    }
    case FGP_PDS_RESET_GPS_SERVICE_DATA: {
        fgp_emit_pds_resp(b, act, sdu, cid, 0u, NULL, 0u);
        const uint32_t ln = fgl_build_delete_assist_data(loc_sdu, sizeof loc_sdu, fgp_next_txn(b), 1u, 0ULL, 0u, 0u);
        fgp_emit_loc(b, act, loc_sdu, ln);
        break;
    }
    case FGP_PDS_INJECT_ABSOLUTE_TIME_REF: {
        const uint8_t *v = fgl_find_tlv(sdu, len, 0x01u, &tlen);
        if (!v || tlen < 12u) {
            fgp_emit_pds_resp(b, act, sdu, cid, CBQ_ERR_MISSING_ARGUMENT, NULL, 0u);
            break;
        }
        const uint64_t utc_ms = fgl_rd64(v + 0);
        const uint32_t tunc_ms = fgl_rd32(v + 8);
        fgp_emit_pds_resp(b, act, sdu, cid, 0u, NULL, 0u);
        const uint32_t ln = fgl_build_inject_utc_time(loc_sdu, sizeof loc_sdu, fgp_next_txn(b),
                                                      utc_ms, tunc_ms, FGL_TIME_SRC_AP);
        fgp_emit_loc(b, act, loc_sdu, ln);
        break;
    }
    case FGP_PDS_INJECT_POSITION_DATA: {
        struct fgl_inject_pos_req req;
        memset(&req, 0, sizeof req);
        const uint8_t *v;
        if ((v = fgl_find_tlv(sdu, len, 0x10u, &tlen)) && tlen >= 8u) { req.utc_ms = fgl_rd64(v); req.has_utc = 1u; }
        if ((v = fgl_find_tlv(sdu, len, 0x11u, &tlen)) && tlen >= 8u) { req.lat_bits = fgl_rd64(v); req.has_lat = 1u; }
        if ((v = fgl_find_tlv(sdu, len, 0x12u, &tlen)) && tlen >= 8u) { req.lon_bits = fgl_rd64(v); req.has_lon = 1u; }
        if ((v = fgl_find_tlv(sdu, len, 0x13u, &tlen)) && tlen >= 4u) { req.alt_ell_bits = fgl_rd32(v); req.has_alt_ell = 1u; }
        if ((v = fgl_find_tlv(sdu, len, 0x14u, &tlen)) && tlen >= 4u) { req.alt_sl_bits = fgl_rd32(v); req.has_alt_sl = 1u; }
        if ((v = fgl_find_tlv(sdu, len, 0x15u, &tlen)) && tlen >= 4u) { req.hunc_bits = fgl_rd32(v); req.has_hunc = 1u; }
        if ((v = fgl_find_tlv(sdu, len, 0x16u, &tlen)) && tlen >= 4u) { req.vunc_bits = fgl_rd32(v); req.has_vunc = 1u; }
        if ((v = fgl_find_tlv(sdu, len, 0x17u, &tlen)) && tlen >= 1u) { req.hconf = v[0]; req.has_hconf = 1u; }
        if ((v = fgl_find_tlv(sdu, len, 0x18u, &tlen)) && tlen >= 1u) { req.vconf = v[0]; req.has_vconf = 1u; }
        fgp_emit_pds_resp(b, act, sdu, cid, 0u, NULL, 0u);
        if (req.has_lat && req.has_lon) {
            const uint32_t ln = fgl_build_inject_position(loc_sdu, sizeof loc_sdu, fgp_next_txn(b), &req);
            fgp_emit_loc(b, act, loc_sdu, ln);
        }
        break;
    }
    case FGP_PDS_SET_TIME_TRANSFER_CONFIG:
    case FGP_PDS_INJECT_CLIENT_TIME_NS_REQ:
        fgp_emit_pds_resp(b, act, sdu, cid, CBQ_ERR_NOT_SUPPORTED, NULL, 0u);
        break;
    default:
        if (fgp_is_benign_success_msg(msg_id)) {
            fgp_emit_pds_resp(b, act, sdu, cid, 0u, NULL, 0u);
        } else {
            act->unknown = 1u;
            b->unknown_reqs++;
            b->last_unknown_msg = msg_id;
            fgp_emit_pds_resp(b, act, sdu, cid, CBQ_ERR_NOT_SUPPORTED, NULL, 0u);
        }
        break;
    }
}

/* ------------------------------------------------------------------ LOC 0x10 -> PDS 0x06 */
static inline uint32_t fgp_build_pds_tlv13(const struct fgp_bridge *b, const struct fgl_pos_report *p,
                                           uint8_t out[FGP_TLV13_LEN])
{
    memset(out, 0, FGP_TLV13_LEN);
    uint32_t val = 0u;

    /* Default leap_seconds to -1 (0xff) when not provided, matching CLQmiPdsManager */
    out[0x0e] = 0xffu;

    if (p->valid_bits & FGL_POS_V_UTC) {
        struct fgp_cal_time cal;
        fgp_utc_ms_to_cal(p->utc_ms, &cal);
        fgl_wr16(out + 0x04, cal.year);
        out[0x06] = cal.month;
        out[0x07] = cal.wday;
        out[0x08] = cal.day;
        out[0x09] = cal.hour;
        out[0x0a] = cal.minute;
        out[0x0b] = cal.second;
        fgl_wr16(out + 0x0c, cal.millisec);
        fgl_wr64(out + 0x0f, p->utc_ms);
        val |= FGP_VAL_CAL | FGP_VAL_UTC;
    }
    if (p->valid_bits & FGL_POS_V_LEAP) {
        out[0x0e] = p->leap_seconds;
        val |= FGP_VAL_SECS;
    }
    if (p->valid_bits & FGL_POS_V_TUNC) {
        const uint32_t tunc_u32 = fgl_f32_scale_u32(p->tunc_ms_bits, 1u, 1u, 0xffffffffu);
        fgl_wr32(out + 0x17, tunc_u32);
        val |= FGP_VAL_TUNC;
    }
    if ((p->valid_bits & (FGL_POS_V_LAT | FGL_POS_V_LON)) == (FGL_POS_V_LAT | FGL_POS_V_LON)) {
        fgl_wr64(out + 0x1b, p->lat_bits);
        fgl_wr64(out + 0x23, p->lon_bits);
        val |= FGP_VAL_LAT | FGP_VAL_LON;
    }
    if (p->valid_bits & FGL_POS_V_ALT_ELL) { fgl_wr32(out + 0x2b, p->alt_ell_bits); val |= FGP_VAL_ALTE; }
    if (p->valid_bits & FGL_POS_V_ALT_MSL) { fgl_wr32(out + 0x2f, p->alt_msl_bits); val |= FGP_VAL_ALTS; }
    if (p->valid_bits & FGL_POS_V_HSPD)    { fgl_wr32(out + 0x33, p->hspd_bits); val |= FGP_VAL_HSPD; }
    if (p->valid_bits & FGL_POS_V_VSPD)    { fgl_wr32(out + 0x37, p->vspd_bits); val |= FGP_VAL_VSPD; }
    if (p->valid_bits & FGL_POS_V_HDING) {
        uint32_t h_bits = 0;
        if (fgp_sanitize_heading_bits(p->hding_bits, &h_bits)) {
            fgl_wr32(out + 0x3b, h_bits);
            val |= FGP_VAL_HDING;
        }
    }
    if (p->valid_bits & FGL_POS_V_HUNC)    { fgl_wr32(out + 0x3f, p->hunc_bits); val |= FGP_VAL_HUNC; }
    if (p->valid_bits & FGL_POS_V_MAJ_UNC) { fgl_wr32(out + 0x43, p->maj_unc_bits); val |= FGP_VAL_H_SMAJ; }
    if (p->valid_bits & FGL_POS_V_MIN_UNC) { fgl_wr32(out + 0x47, p->min_unc_bits); val |= FGP_VAL_H_SMIN; }
    if (p->valid_bits & FGL_POS_V_AZ_UNC)  { fgl_wr32(out + 0x4b, p->az_unc_bits); val |= FGP_VAL_H_AZ; }
    if (p->valid_bits & FGL_POS_V_VUNC)    { fgl_wr32(out + 0x4f, p->vunc_bits); val |= FGP_VAL_VUNC; }
    if (p->valid_bits & FGL_POS_V_SPD_UNC) { fgl_wr32(out + 0x53, p->spd_unc_bits); val |= FGP_VAL_HVEL_UNC; }
    if (p->valid_bits & FGL_POS_V_HCONF)   { out[0x5b] = p->hconf; val |= FGP_VAL_HCONF; }
    if (p->valid_bits & FGL_POS_V_DOP) {
        fgl_wr32(out + 0x5c, p->pdop_bits);
        fgl_wr32(out + 0x60, p->hdop_bits);
        fgl_wr32(out + 0x64, p->vdop_bits);
        val |= FGP_VAL_PDOP | FGP_VAL_HDOP | FGP_VAL_VDOP;
    }
    out[0x68] = b->op_mode;
    val |= FGP_VAL_MODE;
    fgl_wr32(out + 0x00, val);
    return val;
}

static inline uint32_t fgp_map_pos_source(uint32_t tech_used)
{
    uint32_t src = 0u;
    if (tech_used & (1u << 0)) src |= (1u << 0); /* SAT -> 'G' */
    if (tech_used & (1u << 1)) src |= (1u << 1); /* CELL -> 'C' */
    if (tech_used & (1u << 2)) src |= (1u << 2); /* WIFI -> 'L' */
    if (tech_used & (1u << 3)) src |= (1u << 4); /* SENSORS -> 'P' */
    if (tech_used & (1u << 4)) src |= (1u << 5); /* REF_LOC -> 'B' */
    if (tech_used & (1u << 6)) src |= (1u << 7); /* AFLT -> 'A' */
    if (!src) src = 1u; /* default GNSS */
    return src;
}

static inline void fgp_handle_loc_rx(struct fgp_bridge *b, const uint8_t *sdu, uint32_t len,
                                     struct fgp_actions *act, struct fgp_scratch *scratch)
{
    if (!b || !act || !scratch) return;
    memset(act, 0, sizeof *act);
    if (!sdu || !fgl_sdu_ok(sdu, len)) return;

    b->loc_rx++;
    const uint16_t msg_id = fgl_rd16(sdu + 3);

    /* Consume two-stage RESP & IND cleanly; never forward to locationd */
    if (fgl_tracker_handle_rx(&b->two_stage, sdu, len)) {
        act->handled = 1u;
        return;
    }
    if (sdu[0] != CBQ_QMI_INDICATION) {
        act->handled = 1u;
        return;
    }

    const uint8_t cid = b->pds_client_active ? b->pds_cid : 0u;

    if (msg_id == FGL_LOC_EVENT_POSITION_REPORT) {
        if (!fgl_parse_pos_report(sdu, len, &scratch->pos)) return;
        const struct fgl_pos_report *p = &scratch->pos;
        act->handled = 1u;
        if (p->has_sid && p->session_id != (b->sid ? b->sid : 1u)) return;
        const int has_ll = ((p->valid_bits & (FGL_POS_V_LAT | FGL_POS_V_LON)) == (FGL_POS_V_LAT | FGL_POS_V_LON));
        const int nz_ll = ((p->lat_bits & 0x7fffffffffffffffULL) != 0ULL) || ((p->lon_bits & 0x7fffffffffffffffULL) != 0ULL);
        if (p->session_status == FGL_SESS_SUCCESS) {
            b->fixes_ok++;
            if (!b->in_mock_inject && has_ll && nz_ll) {
                b->real_fix_seen = 1u;
                b->last_real_fix_ms = b->last_rx_ms;
            }
        } else if (p->session_status == FGL_SESS_IN_PROGRESS) b->fixes_in_prog++;
        if (!b->pds_client_active || !b->loc_registered) return;

        uint8_t *tlvs = scratch->tlvs;
        const uint32_t room = 256u;
        uint32_t w = 0;
        const uint8_t st = (uint8_t)(p->session_status <= 7u ? p->session_status : 2u);
        w += fgl_tlv_u8(tlvs + w, room - w, 0x12u, st);

        const int has_latlon = ((p->valid_bits & (FGL_POS_V_LAT | FGL_POS_V_LON)) == (FGL_POS_V_LAT | FGL_POS_V_LON));
        const int non_zero_ll = ((p->lat_bits & 0x7fffffffffffffffULL) != 0ULL) || ((p->lon_bits & 0x7fffffffffffffffULL) != 0ULL);
        /* Filter IN_PROGRESS without valid non-zero LAT/LON to prevent (0.0, 0.0) Gulf of Guinea fix */
        if (has_latlon && (p->session_status != FGL_SESS_IN_PROGRESS || non_zero_ll)) {
            fgp_build_pds_tlv13(b, p, scratch->t13);
            w += fgl_tlv(tlvs + w, room - w, 0x13u, scratch->t13, FGP_TLV13_LEN);

            const uint32_t psrc = fgp_map_pos_source((p->valid_bits & FGL_POS_V_TECH) ? p->tech_used : 1u);
            w += fgl_tlv_u32(tlvs + w, room - w, 0x1cu, psrc);

            if (p->valid_bits & (FGL_POS_V_HREL | FGL_POS_V_VREL)) {
                const uint8_t rel[2] = {
                    (uint8_t)((p->valid_bits & FGL_POS_V_HREL) && p->hrel <= 4u ? p->hrel : 0u),
                    (uint8_t)((p->valid_bits & FGL_POS_V_VREL) && p->vrel <= 4u ? p->vrel : 0u)
                };
                w += fgl_tlv(tlvs + w, room - w, 0x20u, rel, 2u);
            }
            if (p->valid_bits & FGL_POS_V_HDING_UNC) {
                uint8_t t24[16];
                memset(t24, 0, sizeof t24);
                fgl_wr32(t24, p->hding_unc_bits);
                w += fgl_tlv(tlvs + w, room - w, 0x24u, t24, 16u);
            }
        }
        fgp_emit_pds_ind(b, act, FGP_PDS_SET_EVENT_REPORT, cid, tlvs, w);
        return;
    }

    if (msg_id == FGL_LOC_EVENT_GNSS_SV_INFO) {
        if (!fgl_parse_sv_info(sdu, len, &scratch->sv)) return;
        act->handled = 1u;
        b->sv_reports++;
        if (!b->pds_client_active || !b->loc_registered) return;

        const uint32_t cnt = scratch->sv.count > FGP_MAX_SV ? FGP_MAX_SV : scratch->sv.count;
        uint8_t *tlvs = scratch->tlvs;
        uint8_t *v17 = tlvs + 3u;
        const uint32_t v17_len = 6u + 18u * cnt;
        tlvs[0] = 0x17u;
        fgl_wr16(tlvs + 1, v17_len);
        fgl_wr32(v17 + 0, 0x00000003u); /* validMask bit0|bit1 */
        v17[4] = 0u;                    /* ionoValid */
        v17[5] = (uint8_t)cnt;

        for (uint32_t i = 0; i < cnt; i++) {
            const struct fgl_sv_entry *s = &scratch->sv.sv[i];
            uint8_t *e = v17 + 6u + 18u * i;
            uint32_t sv_mask = 0u;
            if (s->valid_info & (1u << 0)) sv_mask |= (1u << 0); /* SYSTEM -> bit 0 */
            if (s->valid_info & (1u << 1)) sv_mask |= (1u << 1); /* PRN -> bit 1 */
            if (s->valid_info & (1u << 2)) sv_mask |= (1u << 2); /* HEALTH -> bit 2 */
            if (s->valid_info & (1u << 3)) sv_mask |= (1u << 3); /* STATUS -> bit 3 */
            if (s->valid_info & (1u << 4)) {
                if (s->nav_data & 0x01u) sv_mask |= (1u << 4); /* EPH */
                if (s->nav_data & 0x02u) sv_mask |= (1u << 5); /* ALM */
            }
            if ((s->valid_info & (1u << 7)) && fgl_f32_finite(s->snr_bits))  sv_mask |= (1u << 6); /* SNR -> bit 6 */
            if ((s->valid_info & (1u << 6)) && fgl_f32_finite(s->azim_bits)) sv_mask |= (1u << 7); /* AZ  -> bit 7 */
            if ((s->valid_info & (1u << 5)) && fgl_f32_finite(s->elev_bits)) sv_mask |= (1u << 8); /* EL  -> bit 8 */

            /* Active CLEurekaController (0x101170198 + 0x10115b6a4) SV entry layout:
             * +0x00 u32 svValidMask, +0x04 u8 system (1..6 incl QZSS=6), +0x05 u8 prn,
             * +0x06..+0x09 health/procStatus/eph/alm, +0x0a f32 elevation, +0x0e u16 azimuth, +0x10 u16 cno */
            fgl_wr32(e + 0x00, sv_mask);
            e[0x04] = (uint8_t)(s->system <= 6u ? s->system : 0u);
            e[0x05] = fgp_map_prn_u8(s->sv_id);
            e[0x06] = s->health;
            e[0x07] = (uint8_t)(s->proc_status & 0xffu);
            e[0x08] = (uint8_t)(s->nav_data & 0x01u);
            e[0x09] = (uint8_t)((s->nav_data >> 1) & 0x01u);
            const uint32_t elev_bits = ((s->valid_info & (1u << 5)) && fgl_f32_finite(s->elev_bits)) ? s->elev_bits : 0u;
            fgl_wr32(e + 0x0a, elev_bits);
            fgl_wr16(e + 0x0e, fgl_f32_scale_u32(s->azim_bits, 1u, 1u, 360u));
            fgl_wr16(e + 0x10, fgl_f32_scale_u32(s->snr_bits, 1u, 1u, 99u));
        }
        fgp_emit_pds_ind(b, act, FGP_PDS_SET_EVENT_REPORT, cid, tlvs, 3u + v17_len);
        return;
    }

    if (msg_id == FGL_LOC_EVENT_NMEA) {
        uint32_t nlen = 0;
        if (!fgl_parse_nmea(sdu, len, scratch->nmea, (uint32_t)sizeof scratch->nmea, &nlen) || nlen == 0u) return;
        act->handled = 1u;
        b->nmea_reports++;
        if (!b->pds_client_active || !b->loc_registered) return;

        /* Use PDS TLV 0x10 (raw ASCII, no prefix), identical across CLEurekaController and CLQmiPdsManager */
        const uint32_t w = fgl_tlv(scratch->tlvs, (uint32_t)sizeof scratch->tlvs, 0x10u, scratch->nmea, nlen);
        fgp_emit_pds_ind(b, act, FGP_PDS_SET_EVENT_REPORT, cid, scratch->tlvs, w);
        return;
    }

    if (msg_id == FGL_LOC_EVENT_ENGINE_STATE) {
        int32_t st = 0;
        if (!fgl_parse_engine_state(sdu, len, &st)) return;
        act->handled = 1u;
        b->loc_engine_state = (uint8_t)(st == FGL_ENGINE_ON ? 1u : 2u);
        if (b->probe_active) return;
        if (b->probe_handover_off) {
            if (st == FGL_ENGINE_OFF) {
                b->probe_handover_off = 0u;
                return;
            }
            if (st == FGL_ENGINE_ON && !b->probe_handover_stop &&
                (b->enabled || b->tracking == FGP_TRACK_ACTIVE)) {
                b->probe_handover_off = 0u;
            } else {
                return;
            }
        } else if (b->probe_handover_stop) {
            return;
        }
        if (st == FGL_ENGINE_ON) {
            b->enabled = 1u;
        } else if (st == FGL_ENGINE_OFF) {
            b->enabled = 0u;
            b->tracking = FGP_TRACK_INACTIVE;
        }
        if (b->pds_client_active) fgp_emit_service_state_ind(b, act, cid);
        return;
    }

    if (msg_id == FGL_LOC_EVENT_FIX_SESSION_STATE) {
        uint32_t st = 0;
        if (!fgl_parse_fix_session_state(sdu, len, &st)) return;
        act->handled = 1u;
        if (b->probe_active) return;
        if (b->probe_handover_stop) {
            if (st == FGL_FIX_SESSION_FINISHED) {
                b->probe_handover_stop = 0u;
                return;
            }
            if (st == FGL_FIX_SESSION_STARTED && b->tracking == FGP_TRACK_ACTIVE) {
                b->probe_handover_stop = 0u;
            } else {
                return;
            }
        }
        if (st == FGL_FIX_SESSION_STARTED) {
            b->enabled = 1u;
            b->tracking = FGP_TRACK_ACTIVE;
        } else if (st == FGL_FIX_SESSION_FINISHED) {
            b->tracking = FGP_TRACK_INACTIVE;
        }
        if (b->pds_client_active) fgp_emit_service_state_ind(b, act, cid);
        return;
    }

    if (msg_id == FGL_LOC_EVENT_INJECT_TIME_REQ) {
        if (!fgl_parse_inject_time_req(sdu, len)) return;
        act->handled = 1u;
        b->inject_time_reqs++;
        b->pending_time_inject = 1u;
        act->time_inject_requested = 1u;
        if (b->cached_utc_ms >= 1700000000000ULL) {
            uint8_t loc_sdu[64];
            const uint32_t ln = fgl_build_inject_utc_time(
                loc_sdu, sizeof loc_sdu, fgp_next_txn(b), b->cached_utc_ms, 2000u, FGL_TIME_SRC_AP);
            if (ln) {
                fgp_emit_loc(b, act, loc_sdu, ln);
                b->pending_time_inject = 0u;
            }
        }
        return;
    }

    if (msg_id == FGL_LOC_EVENT_INJECT_ORBITS_REQ) {
        uint32_t max_file = 0u, max_part = 0u;
        if (!fgl_parse_inject_orbits_req(sdu, len, &max_file, &max_part)) return;
        act->handled = 1u;
        b->inject_orbits_reqs++;
        if (max_file) b->xtra_max_file_bytes = max_file;
        if (max_part) b->xtra_max_part_bytes = max_part;
        b->pending_orbits_notice = 1u;
        act->orbits_inject_requested = 1u;
        return;
    }

    act->unknown = 1u;
}

/*
 * Development indoor mock_fix injector: when mock_enabled != 0, tracking is active,
 * and no real GNSS fix arrived within FGP_MOCK_REAL_HOLDOFF_MS, synthesize a compliant
 * QMI_LOC 0x0024 indication and pass it through fgp_handle_loc_rx -> QMI_PDS TLV 0x13.
 */
static inline int fgp_mock_step(
    struct fgp_bridge *b, const struct fgp_mock_cfg *cfg,
    uint32_t now_ms, uint64_t utc_ms,
    struct fgp_actions *act, struct fgp_scratch *scratch)
{
    if (!b || !act || !scratch) return 0;
    memset(act, 0, sizeof *act);
    if (!b->mock_enabled || !b->pds_client_active || !b->loc_registered ||
        b->tracking != FGP_TRACK_ACTIVE) {
        return 0;
    }
    if (b->real_fix_seen && (uint32_t)(now_ms - b->last_real_fix_ms) < FGP_MOCK_REAL_HOLDOFF_MS) {
        return 0;
    }
    const uint32_t interval = (cfg && cfg->interval_ms >= 250u) ? cfg->interval_ms :
                              (b->interval_ms >= 250u ? b->interval_ms : 1000u);
    if (b->mock_fixes > 0u && (uint32_t)(now_ms - b->last_mock_ms) < interval) {
        return 0;
    }

    const uint64_t lat = (cfg && (cfg->lat_bits || cfg->lon_bits)) ? cfg->lat_bits : FGP_MOCK_DEFAULT_LAT_BITS;
    const uint64_t lon = (cfg && (cfg->lat_bits || cfg->lon_bits)) ? cfg->lon_bits : FGP_MOCK_DEFAULT_LON_BITS;
    const uint32_t hunc = (cfg && cfg->hunc_bits) ? cfg->hunc_bits : FGP_MOCK_DEFAULT_HUNC_BITS;
    const uint32_t alte = (cfg && cfg->alt_ell_bits) ? cfg->alt_ell_bits : FGP_MOCK_DEFAULT_ALTE_BITS;
    const uint32_t alts = (cfg && cfg->alt_msl_bits) ? cfg->alt_msl_bits : FGP_MOCK_DEFAULT_ALTS_BITS;
    const uint32_t vunc = (cfg && cfg->vunc_bits) ? cfg->vunc_bits : FGP_MOCK_DEFAULT_VUNC_BITS;
    const uint32_t tunc = (cfg && cfg->tunc_ms_bits) ? cfg->tunc_ms_bits : FGP_MOCK_DEFAULT_TUNC_BITS;
    const uint64_t ts = (utc_ms >= 1700000000000ULL) ? utc_ms :
                        (b->cached_utc_ms >= 1700000000000ULL ? b->cached_utc_ms : FGP_MOCK_DEFAULT_UTC_MS);

    uint8_t sdu[256];
    const uint32_t slen = fgl_build_mock_pos_report(
        sdu, sizeof sdu, b->sid ? b->sid : 1u,
        lat, lon, ts, hunc, alte, alts, vunc, tunc, b->mock_fixes + 1u);
    if (!slen) return 0;

    b->in_mock_inject = 1u;
    fgp_handle_loc_rx(b, sdu, slen, act, scratch);
    b->in_mock_inject = 0u;
    b->last_mock_ms = now_ms;
    b->mock_fixes++;
    return act->host_count > 0u ? 1 : 0;
}

#ifdef __cplusplus
}
#endif
#endif /* FUXI_GNSS_PDS_BRIDGE_H */
