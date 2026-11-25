/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Pure C11 QMI_LOC (service 0x10) packet builders and indication/response parsers
 * for SM8550 MPSS GNSS over QRTR (10:2@81).
 *
 * Kernel-safe (-mgeneral-regs-only): uses zero floating-point types or instructions;
 * IEEE-754 float/double values are transported and validated via uint32_t / uint64_t
 * raw bit patterns. Portable and host-tested with ASan/UBSan/LSan.
 */
#ifndef FUXI_GNSS_LOC_H
#define FUXI_GNSS_LOC_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FGL_SVC_LOC 0x10u
#define FGL_SDU_HDR 7u
#define FGL_PROBE_SID 2u

/* QMI_LOC 0x10 request / response / indication message IDs (libqmi qmi-service-loc.json) */
#define FGL_LOC_REG_EVENTS                  0x0021u
#define FGL_LOC_START                       0x0022u
#define FGL_LOC_STOP                        0x0023u
#define FGL_LOC_EVENT_POSITION_REPORT       0x0024u
#define FGL_LOC_EVENT_GNSS_SV_INFO          0x0025u
#define FGL_LOC_EVENT_NMEA                  0x0026u
#define FGL_LOC_EVENT_NI_NOTIFY_VERIFY_REQ  0x0027u
#define FGL_LOC_EVENT_INJECT_TIME_REQ       0x0028u
#define FGL_LOC_EVENT_INJECT_ORBITS_REQ     0x0029u
#define FGL_LOC_EVENT_INJECT_POSITION_REQ   0x002Au
#define FGL_LOC_EVENT_ENGINE_STATE          0x002Bu
#define FGL_LOC_EVENT_FIX_SESSION_STATE     0x002Cu
#define FGL_LOC_INJECT_UTC_TIME             0x0038u
#define FGL_LOC_INJECT_POSITION             0x0039u
#define FGL_LOC_SET_ENGINE_LOCK             0x003Au
#define FGL_LOC_GET_ENGINE_LOCK             0x003Bu
#define FGL_LOC_SET_NMEA_TYPES              0x003Eu
#define FGL_LOC_DELETE_ASSIST_DATA          0x0044u
#define FGL_LOC_SET_OP_MODE                 0x004Au
#define FGL_LOC_GET_OP_MODE                 0x004Bu
#define FGL_LOC_REG_MASTER_CLIENT           0x00BAu

/* QMI_LOC Master Client key & Engine Lock types */
#define FGL_MASTER_KEY_DEFAULT 0xBAABCDEFu
#define FGL_LOCK_NONE          1u
#define FGL_LOCK_MI            2u
#define FGL_LOCK_MT            3u
#define FGL_LOCK_ALL           4u

/* QMI_LOC event registration bitmask (TLV 0x01, uint64_t) */
#define FGL_EVT_POSITION_REPORT     (1ULL << 0)
#define FGL_EVT_GNSS_SV_INFO        (1ULL << 1)
#define FGL_EVT_NMEA                (1ULL << 2)
#define FGL_EVT_NI_NOTIFY           (1ULL << 3)
#define FGL_EVT_INJECT_TIME_REQ     (1ULL << 4)
#define FGL_EVT_INJECT_ORBITS_REQ   (1ULL << 5)
#define FGL_EVT_INJECT_POS_REQ      (1ULL << 6)
#define FGL_EVT_ENGINE_STATE        (1ULL << 7)
#define FGL_EVT_FIX_SESSION_STATE   (1ULL << 8)
/* Xiaomi gps.conf disables NMEA_PROVIDER & libloc_api_v02 skips bit 8 (FIX_SESSION_STATE) on SM8550 MPSS */
#define FGL_EVT_MIN_MASK            (FGL_EVT_POSITION_REPORT | FGL_EVT_GNSS_SV_INFO)
#define FGL_EVT_DEFAULT_MASK        (FGL_EVT_MIN_MASK | FGL_EVT_ENGINE_STATE)
#define FGL_EVT_BRIDGE_MASK         (FGL_EVT_DEFAULT_MASK | FGL_EVT_NI_NOTIFY | \
                                     FGL_EVT_INJECT_TIME_REQ | FGL_EVT_INJECT_ORBITS_REQ | \
                                     FGL_EVT_INJECT_POS_REQ)

/* QmiLocOperationMode */
#define FGL_OP_MODE_DEFAULT    1u
#define FGL_OP_MODE_MSB        2u
#define FGL_OP_MODE_MSA        3u
#define FGL_OP_MODE_STANDALONE 4u
#define FGL_OP_MODE_CELLID     5u
#define FGL_OP_MODE_WWAN       6u

/* QmiLocFixRecurrenceType / IntermediateReportState / TimeSource */
#define FGL_RECURRENCE_PERIODIC   1u
#define FGL_RECURRENCE_SINGLE     2u
#define FGL_INTERMEDIATE_ENABLE   1u
#define FGL_INTERMEDIATE_DISABLE  2u
#define FGL_TIME_SRC_UNKNOWN      0u
#define FGL_TIME_SRC_AP           1u

/* QmiLocSessionStatus */
#define FGL_SESS_SUCCESS         0u
#define FGL_SESS_IN_PROGRESS     1u
#define FGL_SESS_GENERAL_FAILURE 2u
#define FGL_SESS_TIMEOUT         3u
#define FGL_SESS_USER_ENDED      4u
#define FGL_SESS_BAD_PARAMETER   5u
#define FGL_SESS_PHONE_OFFLINE   6u

/* QmiLocEngineState */
#define FGL_ENGINE_ON   1
#define FGL_ENGINE_OFF  2

/* QmiLocFixSessionState (0x002C TLV 0x01) */
#define FGL_FIX_SESSION_STARTED   1u
#define FGL_FIX_SESSION_FINISHED  2u

/* QmiLocNmeaType default mask (GGA|RMC|GSV|GSA|VTG + multi-constellation) */
#define FGL_NMEA_TYPES_ALL_BASIC 0x003FFFFFu

/* Two-stage indication status */
#define FGL_IND_SUCCESS          0u
#define FGL_IND_GENERAL_FAILURE  1u
#define FGL_IND_UNSUPPORTED      2u
#define FGL_IND_INVALID_PARAM    3u
#define FGL_IND_ENGINE_BUSY      4u
#define FGL_IND_PHONE_OFFLINE    5u
#define FGL_IND_TIMEOUT          6u

/* fgl_pos_report.valid_bits */
#define FGL_POS_V_LAT          (1u << 0)
#define FGL_POS_V_LON          (1u << 1)
#define FGL_POS_V_HUNC         (1u << 2)
#define FGL_POS_V_MIN_UNC      (1u << 3)
#define FGL_POS_V_MAJ_UNC      (1u << 4)
#define FGL_POS_V_AZ_UNC       (1u << 5)
#define FGL_POS_V_HCONF        (1u << 6)
#define FGL_POS_V_HREL         (1u << 7)
#define FGL_POS_V_HSPD         (1u << 8)
#define FGL_POS_V_SPD_UNC      (1u << 9)
#define FGL_POS_V_ALT_ELL      (1u << 10)
#define FGL_POS_V_ALT_MSL      (1u << 11)
#define FGL_POS_V_VUNC         (1u << 12)
#define FGL_POS_V_VCONF        (1u << 13)
#define FGL_POS_V_VREL         (1u << 14)
#define FGL_POS_V_VSPD         (1u << 15)
#define FGL_POS_V_HDING        (1u << 16)
#define FGL_POS_V_HDING_UNC    (1u << 17)
#define FGL_POS_V_TECH         (1u << 18)
#define FGL_POS_V_DOP          (1u << 19)
#define FGL_POS_V_UTC          (1u << 20)
#define FGL_POS_V_LEAP         (1u << 21)
#define FGL_POS_V_GPS_TIME     (1u << 22)
#define FGL_POS_V_TUNC         (1u << 23)
#define FGL_POS_V_SENSOR_USAGE (1u << 24)
#define FGL_POS_V_FIX_COUNT    (1u << 25)

#define FGL_MAX_SV            64u
#define FGL_SV_WIRE_BYTES     28u
#define FGL_SV_EXT_WIRE_BYTES 29u /* TLV 0x11 gnssSvList: struct[8] (28B) + 1B ext (libloc_api_v02 0x1acf6) */
#define FGL_MAX_NMEA          511u

/* QMI_LOC response errors & unlock FSM bounds (libloc_api_v02 locClientSendReq 0x6911c) */
#define FGL_QMI_ERR_INTERNAL          3u
#define FGL_QMI_ERR_SESSION_OWNERSHIP 0x43u
#define FGL_UNLOCK_TIMEOUT_MS         3000u
#define FGL_UNLOCK_RETRY_DELAY_MS     50u
#define FGL_UNLOCK_MAX_RETRIES        10u

/* ------------------------------------------------------------------ Little-endian & IEEE-754 integer helpers */
static inline uint16_t fgl_rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static inline uint32_t fgl_rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline uint64_t fgl_rd64(const uint8_t *p)
{
    return (uint64_t)fgl_rd32(p) | ((uint64_t)fgl_rd32(p + 4) << 32);
}
static inline void fgl_wr16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
}
static inline void fgl_wr32(uint8_t *p, uint32_t v)
{
    fgl_wr16(p, v);
    fgl_wr16(p + 2, v >> 16);
}
static inline void fgl_wr64(uint8_t *p, uint64_t v)
{
    fgl_wr32(p, (uint32_t)(v & 0xffffffffu));
    fgl_wr32(p + 4, (uint32_t)(v >> 32));
}

static inline int fgl_f32_finite(uint32_t bits)
{
    return ((bits >> 23) & 0xffu) != 0xffu;
}
static inline int fgl_f64_finite(uint64_t bits)
{
    return ((bits >> 52) & 0x7ffu) != 0x7ffu;
}
static inline int fgl_f64_abs_le(uint64_t bits, uint64_t pos_limit_bits)
{
    return fgl_f64_finite(bits) && (bits & 0x7fffffffffffffffULL) <= pos_limit_bits;
}

/* Convert non-negative finite IEEE-754 float bits to rounded integer: round(val * num / den), clamped [0, max_out]. */
static inline uint32_t fgl_f32_scale_u32(uint32_t bits, uint64_t num, uint64_t den, uint32_t max_out)
{
    if (!fgl_f32_finite(bits) || (bits >> 31) || den == 0 || num == 0) return 0;
    const uint32_t exp = (bits >> 23) & 0xffu;
    const uint32_t frac = bits & 0x7fffffu;
    uint64_t mant = exp ? (uint64_t)(frac | 0x800000u) : (uint64_t)frac;
    if (!mant) return 0;
    int power = exp ? (int)exp - 150 : -149;
    /* num fits <= 2^20 in all GNSS usages (1, 10, 65535); guard overflow anyway */
    if (num > 0xffffffffULL) return max_out;
    uint64_t prod = mant * num;
    uint64_t res = 0;
    if (power >= 0) {
        if (power > 39) return max_out;
        while (power > 0) {
            if (prod > 0x7fffffffffffffffULL) return max_out;
            prod <<= 1;
            power--;
        }
        if (prod > UINT64_MAX - den / 2u) return max_out;
        res = (prod + den / 2u) / den;
    } else {
        int k = -power;
        if (k > 62) return 0;
        if (k > 31) {
            const int drop = k - 31;
            prod = (prod + (1ULL << (drop - 1))) >> drop;
            k = 31;
        }
        if (den > (UINT64_MAX >> k)) return 0;
        const uint64_t div = den << k;
        if (prod > UINT64_MAX - div / 2u) return max_out;
        res = (prod + div / 2u) / div;
    }
    return res > max_out ? max_out : (uint32_t)res;
}

/* ------------------------------------------------------------------ SDU / TLV framing */
static inline uint32_t fgl_tlv(uint8_t *p, uint32_t room, uint32_t type, const void *val, uint32_t len)
{
    if (room < 3u + len || len > 0xffffu) return 0;
    p[0] = (uint8_t)type;
    fgl_wr16(p + 1, len);
    if (len && val) memcpy(p + 3, val, len);
    return 3u + len;
}
static inline uint32_t fgl_tlv_u8(uint8_t *p, uint32_t room, uint32_t type, uint8_t v)
{
    return fgl_tlv(p, room, type, &v, 1u);
}
static inline uint32_t fgl_tlv_u32(uint8_t *p, uint32_t room, uint32_t type, uint32_t v)
{
    uint8_t b[4];
    fgl_wr32(b, v);
    return fgl_tlv(p, room, type, b, 4u);
}
static inline uint32_t fgl_tlv_u64(uint8_t *p, uint32_t room, uint32_t type, uint64_t v)
{
    uint8_t b[8];
    fgl_wr64(b, v);
    return fgl_tlv(p, room, type, b, 8u);
}

static inline uint32_t fgl_request(uint8_t *sdu, uint32_t txn, uint32_t message, uint32_t tlv_len)
{
    sdu[0] = 0x00u; /* QMI_REQUEST */
    fgl_wr16(sdu + 1, txn);
    fgl_wr16(sdu + 3, message);
    fgl_wr16(sdu + 5, tlv_len);
    return FGL_SDU_HDR + tlv_len;
}

static inline int fgl_sdu_ok(const uint8_t *sdu, uint32_t n)
{
    if (!sdu || n < FGL_SDU_HDR) return 0;
    const uint32_t tlen = fgl_rd16(sdu + 5);
    if (FGL_SDU_HDR + tlen > n) return 0;
    uint32_t at = FGL_SDU_HDR;
    const uint32_t end = FGL_SDU_HDR + tlen;
    while (at < end) {
        if (end - at < 3u) return 0;
        const uint32_t l = fgl_rd16(sdu + at + 1);
        if (3u + l > end - at) return 0;
        at += 3u + l;
    }
    return 1;
}

static inline const uint8_t *fgl_find_tlv(const uint8_t *sdu, uint32_t n, uint32_t type, uint32_t *len_out)
{
    if (!fgl_sdu_ok(sdu, n)) return NULL;
    const uint32_t end = FGL_SDU_HDR + fgl_rd16(sdu + 5);
    for (uint32_t at = FGL_SDU_HDR; at < end; at += 3u + fgl_rd16(sdu + at + 1)) {
        if (sdu[at] == type) {
            if (len_out) *len_out = fgl_rd16(sdu + at + 1);
            return sdu + at + 3;
        }
    }
    return NULL;
}

/* Return QMI error from TLV 0x02, or 0xffffu if absent/malformed */
static inline uint32_t fgl_result_error(const uint8_t *sdu, uint32_t n)
{
    uint32_t len = 0;
    const uint8_t *v = fgl_find_tlv(sdu, n, 0x02u, &len);
    if (!v || len < 4u) return 0xffffu;
    return fgl_rd16(v) ? fgl_rd16(v + 2) : 0u;
}

/* ------------------------------------------------------------------ QMI_LOC 0x10 request builders */
static inline uint32_t fgl_build_reg_events(uint8_t *sdu, uint32_t room, uint32_t txn, uint64_t event_mask)
{
    /* Match libloc_api_v02 locClientRegisterEventMask (TLV 0x01 + 0x10 "MHAL" + 0x11 u32=1 + 0x12 u8=0, 29 B) */
    if (!sdu || room < FGL_SDU_HDR + 29u) return 0;
    uint8_t *t = sdu + FGL_SDU_HDR;
    const uint32_t r = room - FGL_SDU_HDR;
    uint32_t w = 0, a;
    a = fgl_tlv_u64(t + w, r - w, 0x01u, event_mask);
    if (!a) return 0;
    w += a;
    a = fgl_tlv(t + w, r - w, 0x10u, "MHAL", 4u);
    if (!a) return 0;
    w += a;
    a = fgl_tlv_u32(t + w, r - w, 0x11u, 1u);
    if (!a) return 0;
    w += a;
    a = fgl_tlv_u8(t + w, r - w, 0x12u, 0u);
    if (!a) return 0;
    w += a;
    return fgl_request(sdu, txn, FGL_LOC_REG_EVENTS, w);
}

static inline uint32_t fgl_build_start(uint8_t *sdu, uint32_t room, uint32_t txn, uint8_t sid,
                                       uint32_t recurrence, uint32_t intermediate, uint32_t min_interval_ms)
{
    if (!sdu || room < FGL_SDU_HDR + 4u + 7u * 3u) return 0;
    uint8_t *t = sdu + FGL_SDU_HDR;
    const uint32_t r = room - FGL_SDU_HDR;
    uint32_t w = 0, a;
    a = fgl_tlv_u8(t + w, r - w, 0x01u, sid);
    if (!a) return 0;
    w += a;
    if (recurrence) {
        a = fgl_tlv_u32(t + w, r - w, 0x10u, recurrence);
        if (!a) return 0;
        w += a;
    }
    if (intermediate) {
        a = fgl_tlv_u32(t + w, r - w, 0x12u, intermediate);
        if (!a) return 0;
        w += a;
    }
    if (min_interval_ms) {
        a = fgl_tlv_u32(t + w, r - w, 0x13u, min_interval_ms);
        if (!a) return 0;
        w += a;
    }
    return fgl_request(sdu, txn, FGL_LOC_START, w);
}

static inline uint32_t fgl_build_stop(uint8_t *sdu, uint32_t room, uint32_t txn, uint8_t sid)
{
    if (!sdu || room < FGL_SDU_HDR + 4u) return 0;
    const uint32_t w = fgl_tlv_u8(sdu + FGL_SDU_HDR, room - FGL_SDU_HDR, 0x01u, sid);
    if (!w) return 0;
    return fgl_request(sdu, txn, FGL_LOC_STOP, w);
}

static inline uint32_t fgl_build_inject_utc_time(uint8_t *sdu, uint32_t room, uint32_t txn,
                                                 uint64_t utc_ms, uint32_t tunc_ms, int32_t time_src)
{
    if (!sdu || room < FGL_SDU_HDR + 11u + 7u + 7u) return 0;
    uint8_t *t = sdu + FGL_SDU_HDR;
    const uint32_t r = room - FGL_SDU_HDR;
    uint32_t w = 0, a;
    a = fgl_tlv_u64(t + w, r - w, 0x01u, utc_ms);
    if (!a) return 0;
    w += a;
    a = fgl_tlv_u32(t + w, r - w, 0x02u, tunc_ms);
    if (!a) return 0;
    w += a;
    if (time_src >= 0) {
        a = fgl_tlv_u32(t + w, r - w, 0x10u, (uint32_t)time_src);
        if (!a) return 0;
        w += a;
    }
    return fgl_request(sdu, txn, FGL_LOC_INJECT_UTC_TIME, w);
}

struct fgl_inject_pos_req {
    uint64_t lat_bits;        /* IEEE-754 double bits */
    uint64_t lon_bits;        /* IEEE-754 double bits */
    uint64_t utc_ms;
    uint32_t hunc_bits;       /* float bits */
    uint32_t alt_ell_bits;    /* float bits */
    uint32_t alt_sl_bits;     /* float bits */
    uint32_t vunc_bits;       /* float bits */
    uint32_t tunc_bits;       /* float bits */
    uint32_t pos_src;
    uint8_t hconf;
    uint8_t vconf;
    uint8_t has_lat, has_lon, has_hunc, has_hconf, has_alt_ell, has_alt_sl;
    uint8_t has_vunc, has_vconf, has_utc, has_pos_src, has_tunc;
};

static inline uint32_t fgl_build_inject_position(uint8_t *sdu, uint32_t room, uint32_t txn,
                                                 const struct fgl_inject_pos_req *req)
{
    if (!sdu || !req || room < FGL_SDU_HDR) return 0;
    uint8_t *t = sdu + FGL_SDU_HDR;
    const uint32_t r = room - FGL_SDU_HDR;
    uint32_t w = 0, a;
    if (req->has_lat) { a = fgl_tlv_u64(t + w, r - w, 0x10u, req->lat_bits); if (!a) return 0; w += a; }
    if (req->has_lon) { a = fgl_tlv_u64(t + w, r - w, 0x11u, req->lon_bits); if (!a) return 0; w += a; }
    if (req->has_hunc) { a = fgl_tlv_u32(t + w, r - w, 0x12u, req->hunc_bits); if (!a) return 0; w += a; }
    if (req->has_hconf) { a = fgl_tlv_u8(t + w, r - w, 0x13u, req->hconf); if (!a) return 0; w += a; }
    if (req->has_alt_ell) { a = fgl_tlv_u32(t + w, r - w, 0x15u, req->alt_ell_bits); if (!a) return 0; w += a; }
    if (req->has_alt_sl) { a = fgl_tlv_u32(t + w, r - w, 0x16u, req->alt_sl_bits); if (!a) return 0; w += a; }
    if (req->has_vunc) { a = fgl_tlv_u32(t + w, r - w, 0x17u, req->vunc_bits); if (!a) return 0; w += a; }
    if (req->has_vconf) { a = fgl_tlv_u8(t + w, r - w, 0x18u, req->vconf); if (!a) return 0; w += a; }
    if (req->has_utc) { a = fgl_tlv_u64(t + w, r - w, 0x1Bu, req->utc_ms); if (!a) return 0; w += a; }
    if (req->has_pos_src) { a = fgl_tlv_u32(t + w, r - w, 0x1Du, req->pos_src); if (!a) return 0; w += a; }
    if (req->has_tunc) { a = fgl_tlv_u32(t + w, r - w, 0x23u, req->tunc_bits); if (!a) return 0; w += a; }
    return fgl_request(sdu, txn, FGL_LOC_INJECT_POSITION, w);
}

static inline uint32_t fgl_build_set_nmea_types(uint8_t *sdu, uint32_t room, uint32_t txn, uint32_t nmea_mask)
{
    if (!sdu || room < FGL_SDU_HDR + 7u) return 0;
    const uint32_t w = fgl_tlv_u32(sdu + FGL_SDU_HDR, room - FGL_SDU_HDR, 0x01u, nmea_mask);
    if (!w) return 0;
    return fgl_request(sdu, txn, FGL_LOC_SET_NMEA_TYPES, w);
}

static inline uint32_t fgl_build_delete_assist_data(uint8_t *sdu, uint32_t room, uint32_t txn,
                                                    uint8_t delete_all, uint64_t gnss_mask,
                                                    uint32_t cell_mask, uint32_t clock_mask)
{
    if (!sdu || room < FGL_SDU_HDR + 4u) return 0;
    uint8_t *t = sdu + FGL_SDU_HDR;
    const uint32_t r = room - FGL_SDU_HDR;
    uint32_t w = 0, a;
    a = fgl_tlv_u8(t + w, r - w, 0x01u, delete_all ? 1u : 0u);
    if (!a) return 0;
    w += a;
    if (!delete_all) {
        if (gnss_mask) { a = fgl_tlv_u64(t + w, r - w, 0x11u, gnss_mask); if (!a) return 0; w += a; }
        if (cell_mask) { a = fgl_tlv_u32(t + w, r - w, 0x12u, cell_mask); if (!a) return 0; w += a; }
        if (clock_mask) { a = fgl_tlv_u32(t + w, r - w, 0x13u, clock_mask); if (!a) return 0; w += a; }
    }
    return fgl_request(sdu, txn, FGL_LOC_DELETE_ASSIST_DATA, w);
}

static inline uint32_t fgl_build_set_op_mode(uint8_t *sdu, uint32_t room, uint32_t txn, uint32_t op_mode)
{
    if (!sdu || room < FGL_SDU_HDR + 7u) return 0;
    const uint32_t w = fgl_tlv_u32(sdu + FGL_SDU_HDR, room - FGL_SDU_HDR, 0x01u, op_mode);
    if (!w) return 0;
    return fgl_request(sdu, txn, FGL_LOC_SET_OP_MODE, w);
}

static inline uint32_t fgl_build_get_op_mode(uint8_t *sdu, uint32_t room, uint32_t txn)
{
    if (!sdu || room < FGL_SDU_HDR) return 0;
    return fgl_request(sdu, txn, FGL_LOC_GET_OP_MODE, 0u);
}

static inline uint32_t fgl_build_reg_master_client(uint8_t *sdu, uint32_t room, uint32_t txn, uint32_t key)
{
    if (!sdu || room < FGL_SDU_HDR + 7u) return 0;
    const uint32_t w = fgl_tlv_u32(sdu + FGL_SDU_HDR, room - FGL_SDU_HDR, 0x01u, key);
    if (!w) return 0;
    return fgl_request(sdu, txn, FGL_LOC_REG_MASTER_CLIENT, w);
}

static inline uint32_t fgl_build_get_engine_lock(uint8_t *sdu, uint32_t room, uint32_t txn)
{
    if (!sdu || room < FGL_SDU_HDR + 7u) return 0;
    const uint32_t w = fgl_tlv_u32(sdu + FGL_SDU_HDR, room - FGL_SDU_HDR, 0x10u, 3u);
    if (!w) return 0;
    return fgl_request(sdu, txn, FGL_LOC_GET_ENGINE_LOCK, w);
}

static inline uint32_t fgl_build_set_engine_lock(uint8_t *sdu, uint32_t room, uint32_t txn, uint32_t lock_type)
{
    /* Match Xiaomi 13 libloc_api_v02 LocApiV02::setGpsLockSync (0x29388):
     * TLV 0x01 (u32 lockType) + TLV 0x10 (u32 nfwControlBits=3) + TLV 0x12 (u64 clientsConfig). */
    if (!sdu || room < FGL_SDU_HDR + 25u) return 0;
    uint8_t *t = sdu + FGL_SDU_HDR;
    const uint32_t r = room - FGL_SDU_HDR;
    const uint64_t clients_cfg = (lock_type > FGL_LOCK_NONE) ? (uint64_t)(lock_type - 1u) : 0ULL;
    uint32_t w = 0, a;
    a = fgl_tlv_u32(t + w, r - w, 0x01u, lock_type);
    if (!a) return 0;
    w += a;
    a = fgl_tlv_u32(t + w, r - w, 0x10u, 3u);
    if (!a) return 0;
    w += a;
    a = fgl_tlv_u64(t + w, r - w, 0x12u, clients_cfg);
    if (!a) return 0;
    w += a;
    return fgl_request(sdu, txn, FGL_LOC_SET_ENGINE_LOCK, w);
}

/* ------------------------------------------------------------------ QMI_LOC 0x10 indication parsers */
struct fgl_pos_report {
    uint32_t session_status;
    uint8_t session_id;
    uint8_t has_status, has_sid;
    uint32_t valid_bits;
    uint64_t lat_bits, lon_bits;
    uint32_t hunc_bits, min_unc_bits, maj_unc_bits, az_unc_bits;
    uint32_t hspd_bits, spd_unc_bits, alt_ell_bits, alt_msl_bits;
    uint32_t vunc_bits, vspd_bits, hding_bits, hding_unc_bits;
    uint32_t pdop_bits, hdop_bits, vdop_bits, tunc_ms_bits;
    uint64_t utc_ms, sensor_usage;
    uint32_t tech_used, hrel, vrel, fix_count, gps_tow_ms;
    uint16_t gps_weeks;
    uint8_t hconf, vconf, leap_seconds, alt_assumed;
};

static inline int fgl_parse_pos_report(const uint8_t *sdu, uint32_t n, struct fgl_pos_report *out)
{
    if (!sdu || !out || !fgl_sdu_ok(sdu, n)) return 0;
    if (sdu[0] != 0x04u || fgl_rd16(sdu + 3) != FGL_LOC_EVENT_POSITION_REPORT) return 0;
    memset(out, 0, sizeof *out);
    uint32_t len = 0;
    const uint8_t *v = fgl_find_tlv(sdu, n, 0x01u, &len);
    if (!v || len < 4u) return 0;
    out->session_status = fgl_rd32(v);
    out->has_status = 1u;

    v = fgl_find_tlv(sdu, n, 0x02u, &len);
    if (v && len >= 1u) { out->session_id = v[0]; out->has_sid = 1u; }

    v = fgl_find_tlv(sdu, n, 0x10u, &len);
    if (v && len >= 8u) { out->lat_bits = fgl_rd64(v); if (fgl_f64_abs_le(out->lat_bits, 0x4056800000000000ULL)) out->valid_bits |= FGL_POS_V_LAT; }
    v = fgl_find_tlv(sdu, n, 0x11u, &len);
    if (v && len >= 8u) { out->lon_bits = fgl_rd64(v); if (fgl_f64_abs_le(out->lon_bits, 0x4066800000000000ULL)) out->valid_bits |= FGL_POS_V_LON; }
    v = fgl_find_tlv(sdu, n, 0x12u, &len);
    if (v && len >= 4u) { out->hunc_bits = fgl_rd32(v); if (fgl_f32_finite(out->hunc_bits)) out->valid_bits |= FGL_POS_V_HUNC; }
    v = fgl_find_tlv(sdu, n, 0x13u, &len);
    if (v && len >= 4u) { out->min_unc_bits = fgl_rd32(v); if (fgl_f32_finite(out->min_unc_bits)) out->valid_bits |= FGL_POS_V_MIN_UNC; }
    v = fgl_find_tlv(sdu, n, 0x14u, &len);
    if (v && len >= 4u) { out->maj_unc_bits = fgl_rd32(v); if (fgl_f32_finite(out->maj_unc_bits)) out->valid_bits |= FGL_POS_V_MAJ_UNC; }
    v = fgl_find_tlv(sdu, n, 0x15u, &len);
    if (v && len >= 4u) { out->az_unc_bits = fgl_rd32(v); if (fgl_f32_finite(out->az_unc_bits)) out->valid_bits |= FGL_POS_V_AZ_UNC; }
    v = fgl_find_tlv(sdu, n, 0x16u, &len);
    if (v && len >= 1u) { out->hconf = v[0]; out->valid_bits |= FGL_POS_V_HCONF; }
    v = fgl_find_tlv(sdu, n, 0x17u, &len);
    if (v && len >= 4u) { out->hrel = fgl_rd32(v); out->valid_bits |= FGL_POS_V_HREL; }
    v = fgl_find_tlv(sdu, n, 0x18u, &len);
    if (v && len >= 4u) { out->hspd_bits = fgl_rd32(v); if (fgl_f32_finite(out->hspd_bits)) out->valid_bits |= FGL_POS_V_HSPD; }
    v = fgl_find_tlv(sdu, n, 0x19u, &len);
    if (v && len >= 4u) { out->spd_unc_bits = fgl_rd32(v); if (fgl_f32_finite(out->spd_unc_bits)) out->valid_bits |= FGL_POS_V_SPD_UNC; }
    v = fgl_find_tlv(sdu, n, 0x1Au, &len);
    if (v && len >= 4u) { out->alt_ell_bits = fgl_rd32(v); if (fgl_f32_finite(out->alt_ell_bits)) out->valid_bits |= FGL_POS_V_ALT_ELL; }
    v = fgl_find_tlv(sdu, n, 0x1Bu, &len);
    if (v && len >= 4u) { out->alt_msl_bits = fgl_rd32(v); if (fgl_f32_finite(out->alt_msl_bits)) out->valid_bits |= FGL_POS_V_ALT_MSL; }
    v = fgl_find_tlv(sdu, n, 0x1Cu, &len);
    if (v && len >= 4u) { out->vunc_bits = fgl_rd32(v); if (fgl_f32_finite(out->vunc_bits)) out->valid_bits |= FGL_POS_V_VUNC; }
    v = fgl_find_tlv(sdu, n, 0x1Du, &len);
    if (v && len >= 1u) { out->vconf = v[0]; out->valid_bits |= FGL_POS_V_VCONF; }
    v = fgl_find_tlv(sdu, n, 0x1Eu, &len);
    if (v && len >= 4u) { out->vrel = fgl_rd32(v); out->valid_bits |= FGL_POS_V_VREL; }
    v = fgl_find_tlv(sdu, n, 0x1Fu, &len);
    if (v && len >= 4u) { out->vspd_bits = fgl_rd32(v); if (fgl_f32_finite(out->vspd_bits)) out->valid_bits |= FGL_POS_V_VSPD; }
    v = fgl_find_tlv(sdu, n, 0x20u, &len);
    if (v && len >= 4u) { out->hding_bits = fgl_rd32(v); if (fgl_f32_finite(out->hding_bits)) out->valid_bits |= FGL_POS_V_HDING; }
    v = fgl_find_tlv(sdu, n, 0x21u, &len);
    if (v && len >= 4u) { out->hding_unc_bits = fgl_rd32(v); if (fgl_f32_finite(out->hding_unc_bits)) out->valid_bits |= FGL_POS_V_HDING_UNC; }
    v = fgl_find_tlv(sdu, n, 0x23u, &len);
    if (v && len >= 4u) { out->tech_used = fgl_rd32(v); out->valid_bits |= FGL_POS_V_TECH; }
    v = fgl_find_tlv(sdu, n, 0x24u, &len);
    if (v && len >= 12u) {
        out->pdop_bits = fgl_rd32(v);
        out->hdop_bits = fgl_rd32(v + 4);
        out->vdop_bits = fgl_rd32(v + 8);
        if (fgl_f32_finite(out->pdop_bits) && fgl_f32_finite(out->hdop_bits) && fgl_f32_finite(out->vdop_bits))
            out->valid_bits |= FGL_POS_V_DOP;
    }
    v = fgl_find_tlv(sdu, n, 0x25u, &len);
    if (v && len >= 8u) { out->utc_ms = fgl_rd64(v); out->valid_bits |= FGL_POS_V_UTC; }
    v = fgl_find_tlv(sdu, n, 0x26u, &len);
    if (v && len >= 1u) { out->leap_seconds = v[0]; out->valid_bits |= FGL_POS_V_LEAP; }
    v = fgl_find_tlv(sdu, n, 0x27u, &len);
    if (v && len >= 6u) { out->gps_weeks = fgl_rd16(v); out->gps_tow_ms = fgl_rd32(v + 2); out->valid_bits |= FGL_POS_V_GPS_TIME; }
    v = fgl_find_tlv(sdu, n, 0x28u, &len);
    if (v && len >= 4u) { out->tunc_ms_bits = fgl_rd32(v); if (fgl_f32_finite(out->tunc_ms_bits)) out->valid_bits |= FGL_POS_V_TUNC; }
    v = fgl_find_tlv(sdu, n, 0x2Au, &len);
    if (v && len >= 8u) { out->sensor_usage = fgl_rd64(v); out->valid_bits |= FGL_POS_V_SENSOR_USAGE; }
    v = fgl_find_tlv(sdu, n, 0x2Bu, &len);
    if (v && len >= 4u) { out->fix_count = fgl_rd32(v); out->valid_bits |= FGL_POS_V_FIX_COUNT; }
    v = fgl_find_tlv(sdu, n, 0x2Du, &len);
    if (v && len >= 1u) { out->alt_assumed = v[0]; }
    return 1;
}

struct fgl_sv_entry {
    uint32_t valid_info;
    uint32_t system;
    uint16_t sv_id;
    uint8_t health;
    uint8_t nav_data;
    uint32_t proc_status;
    uint32_t elev_bits;
    uint32_t azim_bits;
    uint32_t snr_bits;
};

struct fgl_sv_info {
    uint8_t alt_assumed;
    uint8_t count;
    struct fgl_sv_entry sv[FGL_MAX_SV];
};

static inline int fgl_parse_sv_info(const uint8_t *sdu, uint32_t n, struct fgl_sv_info *out)
{
    if (!sdu || !out || !fgl_sdu_ok(sdu, n)) return 0;
    if (sdu[0] != 0x04u || fgl_rd16(sdu + 3) != FGL_LOC_EVENT_GNSS_SV_INFO) return 0;
    memset(out, 0, sizeof *out);
    uint32_t len = 0;
    const uint8_t *v = fgl_find_tlv(sdu, n, 0x01u, &len);
    if (v && len >= 1u) out->alt_assumed = v[0];
    /* Prefer TLV 0x11 (gnssSvList, 29B/entry on SM8550) over TLV 0x10 (svList, 28B/entry);
     * matches Xiaomi 13 libloc_api_v02 LocApiV02::reportSv (0x35610) & IDL 0x1b2b8. */
    uint32_t stride = FGL_SV_EXT_WIRE_BYTES;
    v = fgl_find_tlv(sdu, n, 0x11u, &len);
    if (!v) {
        stride = FGL_SV_WIRE_BYTES;
        v = fgl_find_tlv(sdu, n, 0x10u, &len);
    }
    if (!v) return 1; /* empty SV list is valid */
    if (len < 1u) return 0;
    const uint32_t raw_cnt = v[0];
    if (raw_cnt > (len - 1u) / stride) return 0;
    const uint32_t take = raw_cnt > FGL_MAX_SV ? FGL_MAX_SV : raw_cnt;
    out->count = (uint8_t)take;
    for (uint32_t i = 0; i < take; i++) {
        const uint8_t *e = v + 1u + stride * i;
        struct fgl_sv_entry *d = &out->sv[i];
        d->valid_info = fgl_rd32(e + 0x00);
        d->system = fgl_rd32(e + 0x04);
        d->sv_id = fgl_rd16(e + 0x08);
        d->health = e[0x0a];
        d->proc_status = fgl_rd32(e + 0x0b);
        d->nav_data = e[0x0f];
        d->elev_bits = fgl_rd32(e + 0x10);
        d->azim_bits = fgl_rd32(e + 0x14);
        d->snr_bits = fgl_rd32(e + 0x18);
    }
    return 1;
}

static inline int fgl_parse_nmea(const uint8_t *sdu, uint32_t n, char *buf, uint32_t room, uint32_t *out_len)
{
    if (!sdu || !buf || room == 0 || !fgl_sdu_ok(sdu, n)) return 0;
    if (sdu[0] != 0x04u || fgl_rd16(sdu + 3) != FGL_LOC_EVENT_NMEA) return 0;
    uint32_t len = 0;
    const uint8_t *v = fgl_find_tlv(sdu, n, 0x01u, &len);
    if (!v || len == 0) return 0;
    uint32_t copy = len < (room - 1u) ? len : (room - 1u);
    /* Trim trailing NULs if modem included them */
    while (copy > 0 && v[copy - 1u] == 0) copy--;
    if (copy) memcpy(buf, v, copy);
    buf[copy] = '\0';
    if (out_len) *out_len = copy;
    return 1;
}

static inline int fgl_parse_engine_state(const uint8_t *sdu, uint32_t n, int32_t *state_out)
{
    if (!sdu || !state_out || !fgl_sdu_ok(sdu, n)) return 0;
    if (sdu[0] != 0x04u || fgl_rd16(sdu + 3) != FGL_LOC_EVENT_ENGINE_STATE) return 0;
    uint32_t len = 0;
    const uint8_t *v = fgl_find_tlv(sdu, n, 0x01u, &len);
    if (!v || len < 4u) return 0;
    *state_out = (int32_t)fgl_rd32(v);
    return 1;
}

static inline int fgl_parse_fix_session_state(const uint8_t *sdu, uint32_t n, uint32_t *state_out)
{
    if (!sdu || !state_out || !fgl_sdu_ok(sdu, n)) return 0;
    if (sdu[0] != 0x04u || fgl_rd16(sdu + 3) != FGL_LOC_EVENT_FIX_SESSION_STATE) return 0;
    uint32_t len = 0;
    const uint8_t *v = fgl_find_tlv(sdu, n, 0x01u, &len);
    if (!v || len < 4u) return 0;
    *state_out = fgl_rd32(v);
    return 1;
}

static inline int fgl_parse_inject_time_req(const uint8_t *sdu, uint32_t n)
{
    if (!sdu || !fgl_sdu_ok(sdu, n)) return 0;
    if (sdu[0] != 0x04u || fgl_rd16(sdu + 3) != FGL_LOC_EVENT_INJECT_TIME_REQ) return 0;
    return 1;
}

static inline int fgl_parse_inject_orbits_req(const uint8_t *sdu, uint32_t n,
                                              uint32_t *max_file_bytes_out, uint32_t *max_part_bytes_out)
{
    if (!sdu || !fgl_sdu_ok(sdu, n)) return 0;
    if (sdu[0] != 0x04u || fgl_rd16(sdu + 3) != FGL_LOC_EVENT_INJECT_ORBITS_REQ) return 0;
    if (max_file_bytes_out) *max_file_bytes_out = 0u;
    if (max_part_bytes_out) *max_part_bytes_out = 0u;
    uint32_t len = 0;
    const uint8_t *v = fgl_find_tlv(sdu, n, 0x01u, &len);
    if (v && len >= 8u) {
        if (max_file_bytes_out) *max_file_bytes_out = fgl_rd32(v);
        if (max_part_bytes_out) *max_part_bytes_out = fgl_rd32(v + 4);
    }
    return 1;
}

/* Build a compliant QMI_LOC 0x0024 POSITION_REPORT indication using raw IEEE-754 bit patterns (zero FP). */
static inline uint32_t fgl_build_mock_pos_report(
    uint8_t *sdu, uint32_t room, uint8_t sid,
    uint64_t lat_bits, uint64_t lon_bits, uint64_t utc_ms,
    uint32_t hunc_bits, uint32_t alt_ell_bits, uint32_t alt_msl_bits,
    uint32_t vunc_bits, uint32_t tunc_ms_bits, uint32_t fix_count)
{
    if (!sdu || room < FGL_SDU_HDR + 128u) return 0u;
    if (!fgl_f64_abs_le(lat_bits, 0x4056800000000000ULL)) return 0u;
    if (!fgl_f64_abs_le(lon_bits, 0x4066800000000000ULL)) return 0u;
    if (!fgl_f32_finite(hunc_bits) || !fgl_f32_finite(alt_ell_bits) ||
        !fgl_f32_finite(alt_msl_bits) || !fgl_f32_finite(vunc_bits) || !fgl_f32_finite(tunc_ms_bits)) return 0u;
    if ((hunc_bits >> 31) || (vunc_bits >> 31) || (tunc_ms_bits >> 31)) return 0u;

    uint8_t *t = sdu + FGL_SDU_HDR;
    const uint32_t r = room - FGL_SDU_HDR;
    uint32_t w = 0u, a = 0u;

    a = fgl_tlv_u32(t + w, r - w, 0x01u, FGL_SESS_SUCCESS); if (!a) return 0u; w += a;
    a = fgl_tlv_u8(t + w, r - w, 0x02u, sid ? sid : 1u); if (!a) return 0u; w += a;
    a = fgl_tlv_u64(t + w, r - w, 0x10u, lat_bits); if (!a) return 0u; w += a;
    a = fgl_tlv_u64(t + w, r - w, 0x11u, lon_bits); if (!a) return 0u; w += a;
    a = fgl_tlv_u32(t + w, r - w, 0x12u, hunc_bits); if (!a) return 0u; w += a;
    a = fgl_tlv_u8(t + w, r - w, 0x16u, 68u); if (!a) return 0u; w += a;
    a = fgl_tlv_u32(t + w, r - w, 0x17u, 3u); if (!a) return 0u; w += a;
    a = fgl_tlv_u32(t + w, r - w, 0x1Au, alt_ell_bits); if (!a) return 0u; w += a;
    a = fgl_tlv_u32(t + w, r - w, 0x1Bu, alt_msl_bits); if (!a) return 0u; w += a;
    a = fgl_tlv_u32(t + w, r - w, 0x1Cu, vunc_bits); if (!a) return 0u; w += a;
    a = fgl_tlv_u32(t + w, r - w, 0x1Eu, 3u); if (!a) return 0u; w += a;
    a = fgl_tlv_u32(t + w, r - w, 0x23u, 1u); if (!a) return 0u; w += a;
    if (utc_ms) {
        a = fgl_tlv_u64(t + w, r - w, 0x25u, utc_ms); if (!a) return 0u; w += a;
        a = fgl_tlv_u8(t + w, r - w, 0x26u, 18u); if (!a) return 0u; w += a;
    }
    a = fgl_tlv_u32(t + w, r - w, 0x28u, tunc_ms_bits); if (!a) return 0u; w += a;
    a = fgl_tlv_u32(t + w, r - w, 0x2Bu, fix_count); if (!a) return 0u; w += a;

    sdu[0] = 0x04u; /* QMI_INDICATION */
    fgl_wr16(sdu + 1, 0u);
    fgl_wr16(sdu + 3, FGL_LOC_EVENT_POSITION_REPORT);
    fgl_wr16(sdu + 5, w);
    return FGL_SDU_HDR + w;
}

static inline int fgl_is_two_stage_msg(uint32_t msg_id)
{
    return msg_id == FGL_LOC_INJECT_UTC_TIME ||
           msg_id == FGL_LOC_INJECT_POSITION ||
           msg_id == FGL_LOC_SET_NMEA_TYPES ||
           msg_id == FGL_LOC_DELETE_ASSIST_DATA ||
           msg_id == FGL_LOC_SET_OP_MODE ||
           msg_id == FGL_LOC_GET_OP_MODE ||
           msg_id == FGL_LOC_SET_ENGINE_LOCK ||
           msg_id == FGL_LOC_REG_MASTER_CLIENT ||
           msg_id == FGL_LOC_GET_ENGINE_LOCK;
}

static inline int fgl_two_stage_index(uint32_t msg_id)
{
    switch (msg_id) {
    case FGL_LOC_INJECT_UTC_TIME:    return 0;
    case FGL_LOC_INJECT_POSITION:    return 1;
    case FGL_LOC_SET_NMEA_TYPES:     return 2;
    case FGL_LOC_DELETE_ASSIST_DATA: return 3;
    case FGL_LOC_SET_OP_MODE:        return 4;
    case FGL_LOC_GET_OP_MODE:        return 5;
    case FGL_LOC_SET_ENGINE_LOCK:    return 6;
    case FGL_LOC_REG_MASTER_CLIENT:  return 7;
    case FGL_LOC_GET_ENGINE_LOCK:    return 8;
    default:                         return -1;
    }
}

struct fgl_two_stage_slot {
    uint16_t pending_txn;
    uint16_t last_resp_err;
    uint32_t last_ind_status;
    uint32_t last_extra_u32;
    uint8_t awaiting_resp;
    uint8_t awaiting_ind;
    uint8_t resp_seen;
    uint8_t ind_seen;
};

struct fgl_two_stage_tracker {
    struct fgl_two_stage_slot slot[9];
    uint32_t resp_count;
    uint32_t ind_count;
    uint32_t ind_failures;
};

static inline void fgl_tracker_init(struct fgl_two_stage_tracker *t)
{
    if (t) memset(t, 0, sizeof *t);
}

static inline void fgl_tracker_note_tx(struct fgl_two_stage_tracker *t, uint32_t msg_id, uint16_t txn)
{
    const int idx = fgl_two_stage_index(msg_id);
    if (!t || idx < 0) return;
    t->slot[idx].pending_txn = txn;
    t->slot[idx].awaiting_resp = 1u;
    t->slot[idx].awaiting_ind = 1u;
}

static inline int fgl_parse_two_stage_resp(const uint8_t *sdu, uint32_t n, uint16_t *msg_id_out, uint32_t *err_out)
{
    if (!sdu || !fgl_sdu_ok(sdu, n) || sdu[0] != 0x02u) return 0;
    const uint16_t msg = fgl_rd16(sdu + 3);
    if (!fgl_is_two_stage_msg(msg)) return 0;
    const uint32_t err = fgl_result_error(sdu, n);
    if (err == 0xffffu) return 0;
    if (msg_id_out) *msg_id_out = msg;
    if (err_out) *err_out = err;
    return 1;
}

static inline int fgl_parse_two_stage_ind(const uint8_t *sdu, uint32_t n, uint16_t *msg_id_out,
                                          uint32_t *ind_status_out, uint32_t *extra_u32_out)
{
    if (!sdu || !fgl_sdu_ok(sdu, n) || sdu[0] != 0x04u) return 0;
    const uint16_t msg = fgl_rd16(sdu + 3);
    if (!fgl_is_two_stage_msg(msg)) return 0;
    uint32_t len = 0;
    const uint8_t *v = fgl_find_tlv(sdu, n, 0x01u, &len);
    if (!v || len < 4u) return 0;
    if (msg_id_out) *msg_id_out = msg;
    if (ind_status_out) *ind_status_out = fgl_rd32(v);
    if (extra_u32_out) {
        *extra_u32_out = 0u;
        if (msg == FGL_LOC_GET_OP_MODE || msg == FGL_LOC_GET_ENGINE_LOCK) {
            const uint8_t *m = fgl_find_tlv(sdu, n, 0x10u, &len);
            if (m && len >= 4u) *extra_u32_out = fgl_rd32(m);
        }
    }
    return 1;
}

static inline int fgl_parse_get_engine_lock_ind(const uint8_t *sdu, uint32_t n,
                                              uint32_t *status_out, uint32_t *lock_type_out)
{
    uint16_t msg = 0;
    uint32_t st = 0, lock = 0;
    if (!fgl_parse_two_stage_ind(sdu, n, &msg, &st, &lock) || msg != FGL_LOC_GET_ENGINE_LOCK) return 0;
    if (status_out) *status_out = st;
    if (lock_type_out) *lock_type_out = lock;
    return 1;
}

/* Returns 1 if sdu was a two-stage RESP or IND and updated tracker, 0 otherwise. */
static inline int fgl_tracker_handle_rx(struct fgl_two_stage_tracker *t, const uint8_t *sdu, uint32_t n)
{
    if (!t || !sdu || !fgl_sdu_ok(sdu, n)) return 0;
    const uint16_t msg = fgl_rd16(sdu + 3);
    const int idx = fgl_two_stage_index(msg);
    if (idx < 0) return 0;
    if (sdu[0] == 0x02u) {
        uint32_t err = 0;
        if (!fgl_parse_two_stage_resp(sdu, n, NULL, &err)) return 0;
        const uint16_t txn = fgl_rd16(sdu + 1);
        if (!t->slot[idx].awaiting_resp || txn != t->slot[idx].pending_txn) return 0;
        t->slot[idx].awaiting_resp = 0u;
        t->slot[idx].resp_seen = 1u;
        t->slot[idx].last_resp_err = (uint16_t)err;
        if (err != 0u) t->slot[idx].awaiting_ind = 0u;
        t->resp_count++;
        return 1;
    }
    if (sdu[0] == 0x04u) {
        uint32_t status = 0, extra = 0;
        if (!fgl_parse_two_stage_ind(sdu, n, NULL, &status, &extra)) return 0;
        t->slot[idx].awaiting_ind = 0u;
        t->slot[idx].ind_seen = 1u;
        t->slot[idx].last_ind_status = status;
        t->slot[idx].last_extra_u32 = extra;
        t->ind_count++;
        if (status != FGL_IND_SUCCESS) t->ind_failures++;
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ QMI_LOC 0x10 unlock & session-ownership FSM */
/* Models Xiaomi 13 libloc_api_v02 LocApiV02::open() + locClientOpenInstance() (0x22ca0 / 0x6ac14)
 * and locClientSendReq() 0x43 (QMI_ERR_SESSION_OWNERSHIP_V01) bounded backoff retry (0x6911c),
 * plus SM8550 MPSS twin-response err=3 (QMI_ERR_INTERNAL) absorption. */
enum fgl_unlock_step {
    FGL_ULK_PRE_REG_SEND = 0,
    FGL_ULK_PRE_REG_WAIT = 1,
    FGL_ULK_MASTER_SEND = 2,
    FGL_ULK_MASTER_WAIT = 3,
    FGL_ULK_GET_LOCK_SEND = 4,
    FGL_ULK_GET_LOCK_WAIT = 5,
    FGL_ULK_GET_LOCK_BACKOFF = 6,
    FGL_ULK_SET_LOCK_SEND = 7,
    FGL_ULK_SET_LOCK_WAIT = 8,
    FGL_ULK_SET_LOCK_BACKOFF = 9,
    FGL_ULK_DONE = 10
};

enum fgl_unlock_ev_kind {
    FGL_ULK_EV_NONE = 0,
    FGL_ULK_EV_ABSORBED_ERR3 = 1,
    FGL_ULK_EV_TIMEOUT = 2,
    FGL_ULK_EV_PRE_REG_RESP = 3,
    FGL_ULK_EV_MASTER_RESP = 4,
    FGL_ULK_EV_MASTER_IND = 5,
    FGL_ULK_EV_GET_LOCK_RESP = 6,
    FGL_ULK_EV_GET_LOCK_RETRY = 7,
    FGL_ULK_EV_GET_LOCK_EXHAUSTED = 8,
    FGL_ULK_EV_GET_LOCK_IND = 9,
    FGL_ULK_EV_SET_LOCK_RESP = 10,
    FGL_ULK_EV_SET_LOCK_RETRY = 11,
    FGL_ULK_EV_SET_LOCK_EXHAUSTED = 12,
    FGL_ULK_EV_SET_LOCK_IND = 13
};

struct fgl_unlock_event {
    uint8_t kind;
    uint8_t retry;
    uint16_t msg;
    uint16_t txn;
    uint32_t err;
    uint32_t ind_status;
    uint32_t lock_type;
    uint8_t timed_out_step;
};

struct fgl_unlock_fsm {
    uint8_t step;
    uint8_t get_lock_retries;
    uint8_t set_lock_retries;
    uint8_t resp_ok_seen;
    uint8_t last_err_active;
    uint16_t cur_txn;
    uint16_t last_err_msg;
    uint16_t last_err_txn;
    uint32_t t0_ms;
    uint32_t absorbed_err3_count;
};

static inline void fgl_unlock_init(struct fgl_unlock_fsm *f)
{
    if (f) memset(f, 0, sizeof *f);
}

static inline int fgl_unlock_is_done(const struct fgl_unlock_fsm *f)
{
    return f && f->step >= FGL_ULK_DONE;
}

static inline int fgl_unlock_is_send_step(const struct fgl_unlock_fsm *f)
{
    if (!f) return 0;
    return f->step == FGL_ULK_PRE_REG_SEND ||
           f->step == FGL_ULK_MASTER_SEND ||
           f->step == FGL_ULK_GET_LOCK_SEND ||
           f->step == FGL_ULK_SET_LOCK_SEND;
}

static inline void fgl_unlock_tick(struct fgl_unlock_fsm *f, uint32_t now_ms, struct fgl_unlock_event *ev_out)
{
    if (ev_out) memset(ev_out, 0, sizeof *ev_out);
    if (!f || f->step >= FGL_ULK_DONE) return;
    const uint32_t elapsed = (uint32_t)(now_ms - f->t0_ms);
    if (f->step == FGL_ULK_GET_LOCK_BACKOFF) {
        if (elapsed >= FGL_UNLOCK_RETRY_DELAY_MS) f->step = FGL_ULK_GET_LOCK_SEND;
        return;
    }
    if (f->step == FGL_ULK_SET_LOCK_BACKOFF) {
        if (elapsed >= FGL_UNLOCK_RETRY_DELAY_MS) f->step = FGL_ULK_SET_LOCK_SEND;
        return;
    }
    if (elapsed < FGL_UNLOCK_TIMEOUT_MS) return;
    if (f->step == FGL_ULK_PRE_REG_WAIT) {
        if (ev_out) { ev_out->kind = FGL_ULK_EV_TIMEOUT; ev_out->timed_out_step = f->step; ev_out->msg = FGL_LOC_REG_EVENTS; }
        f->step = FGL_ULK_MASTER_SEND;
    } else if (f->step == FGL_ULK_MASTER_WAIT) {
        if (ev_out) { ev_out->kind = FGL_ULK_EV_TIMEOUT; ev_out->timed_out_step = f->step; ev_out->msg = FGL_LOC_REG_MASTER_CLIENT; }
        f->step = FGL_ULK_GET_LOCK_SEND;
    } else if (f->step == FGL_ULK_GET_LOCK_WAIT) {
        if (ev_out) { ev_out->kind = FGL_ULK_EV_TIMEOUT; ev_out->timed_out_step = f->step; ev_out->msg = FGL_LOC_GET_ENGINE_LOCK; }
        f->step = FGL_ULK_DONE;
    } else if (f->step == FGL_ULK_SET_LOCK_WAIT) {
        if (ev_out) { ev_out->kind = FGL_ULK_EV_TIMEOUT; ev_out->timed_out_step = f->step; ev_out->msg = FGL_LOC_SET_ENGINE_LOCK; }
        f->step = FGL_ULK_DONE;
    }
}

static inline uint32_t fgl_unlock_prepare_tx(const struct fgl_unlock_fsm *f, uint16_t txn,
                                             uint8_t *sdu_out, uint32_t room, uint16_t *msg_id_out)
{
    if (!f || !sdu_out) return 0u;
    if (f->step == FGL_ULK_PRE_REG_SEND) {
        if (msg_id_out) *msg_id_out = FGL_LOC_REG_EVENTS;
        return fgl_build_reg_events(sdu_out, room, txn, 0ULL);
    }
    if (f->step == FGL_ULK_MASTER_SEND) {
        if (msg_id_out) *msg_id_out = FGL_LOC_REG_MASTER_CLIENT;
        return fgl_build_reg_master_client(sdu_out, room, txn, FGL_MASTER_KEY_DEFAULT);
    }
    if (f->step == FGL_ULK_GET_LOCK_SEND) {
        if (msg_id_out) *msg_id_out = FGL_LOC_GET_ENGINE_LOCK;
        return fgl_build_get_engine_lock(sdu_out, room, txn);
    }
    if (f->step == FGL_ULK_SET_LOCK_SEND) {
        if (msg_id_out) *msg_id_out = FGL_LOC_SET_ENGINE_LOCK;
        return fgl_build_set_engine_lock(sdu_out, room, txn, FGL_LOCK_NONE);
    }
    return 0u;
}

static inline void fgl_unlock_commit_tx(struct fgl_unlock_fsm *f, uint16_t txn, uint32_t now_ms)
{
    if (!f) return;
    if (f->last_err_active && f->last_err_txn == txn) f->last_err_active = 0u;
    f->cur_txn = txn;
    f->resp_ok_seen = 0u;
    f->t0_ms = now_ms;
    if (f->step == FGL_ULK_PRE_REG_SEND) f->step = FGL_ULK_PRE_REG_WAIT;
    else if (f->step == FGL_ULK_MASTER_SEND) f->step = FGL_ULK_MASTER_WAIT;
    else if (f->step == FGL_ULK_GET_LOCK_SEND) f->step = FGL_ULK_GET_LOCK_WAIT;
    else if (f->step == FGL_ULK_SET_LOCK_SEND) f->step = FGL_ULK_SET_LOCK_WAIT;
}

static inline void fgl_unlock_handle_rx(struct fgl_unlock_fsm *f, const uint8_t *sdu, uint32_t size,
                                        uint32_t now_ms, struct fgl_unlock_event *ev_out)
{
    if (ev_out) memset(ev_out, 0, sizeof *ev_out);
    if (!f || !sdu || !fgl_sdu_ok(sdu, size)) return;
    const uint16_t msg = fgl_rd16(sdu + 3);
    const uint16_t txn = fgl_rd16(sdu + 1);
    if (sdu[0] == 0x02u) {
        const uint32_t err = fgl_result_error(sdu, size);
        if (err == 0xffffu) return;
        if (f->last_err_active && msg == f->last_err_msg && txn == f->last_err_txn && err == FGL_QMI_ERR_INTERNAL) {
            f->absorbed_err3_count++;
            if (ev_out) { ev_out->kind = FGL_ULK_EV_ABSORBED_ERR3; ev_out->msg = msg; ev_out->txn = txn; ev_out->err = err; }
            return;
        }
        if (f->step == FGL_ULK_PRE_REG_WAIT && msg == FGL_LOC_REG_EVENTS && txn == f->cur_txn) {
            if (err != 0u) { f->last_err_active = 1u; f->last_err_msg = msg; f->last_err_txn = txn; }
            f->step = FGL_ULK_MASTER_SEND;
            if (ev_out) { ev_out->kind = FGL_ULK_EV_PRE_REG_RESP; ev_out->msg = msg; ev_out->txn = txn; ev_out->err = err; }
            return;
        }
        if (f->step == FGL_ULK_MASTER_WAIT && msg == FGL_LOC_REG_MASTER_CLIENT && txn == f->cur_txn) {
            if (err == 0u) {
                f->resp_ok_seen = 1u;
            } else {
                f->last_err_active = 1u; f->last_err_msg = msg; f->last_err_txn = txn;
                f->step = FGL_ULK_GET_LOCK_SEND;
            }
            if (ev_out) { ev_out->kind = FGL_ULK_EV_MASTER_RESP; ev_out->msg = msg; ev_out->txn = txn; ev_out->err = err; }
            return;
        }
        if (f->step == FGL_ULK_GET_LOCK_WAIT && msg == FGL_LOC_GET_ENGINE_LOCK && txn == f->cur_txn) {
            if (err == 0u) {
                f->resp_ok_seen = 1u;
                if (ev_out) { ev_out->kind = FGL_ULK_EV_GET_LOCK_RESP; ev_out->msg = msg; ev_out->txn = txn; ev_out->err = err; }
                return;
            }
            f->last_err_active = 1u; f->last_err_msg = msg; f->last_err_txn = txn;
            if (err == FGL_QMI_ERR_SESSION_OWNERSHIP && f->get_lock_retries < FGL_UNLOCK_MAX_RETRIES) {
                f->get_lock_retries++;
                f->t0_ms = now_ms;
                f->step = FGL_ULK_GET_LOCK_BACKOFF;
                if (ev_out) {
                    ev_out->kind = FGL_ULK_EV_GET_LOCK_RETRY;
                    ev_out->msg = msg; ev_out->txn = txn; ev_out->err = err; ev_out->retry = f->get_lock_retries;
                }
                return;
            }
            f->step = FGL_ULK_DONE;
            if (ev_out) {
                ev_out->kind = (err == FGL_QMI_ERR_SESSION_OWNERSHIP) ? FGL_ULK_EV_GET_LOCK_EXHAUSTED : FGL_ULK_EV_GET_LOCK_RESP;
                ev_out->msg = msg; ev_out->txn = txn; ev_out->err = err; ev_out->retry = f->get_lock_retries;
            }
            return;
        }
        if (f->step == FGL_ULK_SET_LOCK_WAIT && msg == FGL_LOC_SET_ENGINE_LOCK && txn == f->cur_txn) {
            if (err == 0u) {
                f->resp_ok_seen = 1u;
                if (ev_out) { ev_out->kind = FGL_ULK_EV_SET_LOCK_RESP; ev_out->msg = msg; ev_out->txn = txn; ev_out->err = err; }
                return;
            }
            f->last_err_active = 1u; f->last_err_msg = msg; f->last_err_txn = txn;
            if (err == FGL_QMI_ERR_SESSION_OWNERSHIP && f->set_lock_retries < FGL_UNLOCK_MAX_RETRIES) {
                f->set_lock_retries++;
                f->t0_ms = now_ms;
                f->step = FGL_ULK_SET_LOCK_BACKOFF;
                if (ev_out) {
                    ev_out->kind = FGL_ULK_EV_SET_LOCK_RETRY;
                    ev_out->msg = msg; ev_out->txn = txn; ev_out->err = err; ev_out->retry = f->set_lock_retries;
                }
                return;
            }
            f->step = FGL_ULK_DONE;
            if (ev_out) {
                ev_out->kind = (err == FGL_QMI_ERR_SESSION_OWNERSHIP) ? FGL_ULK_EV_SET_LOCK_EXHAUSTED : FGL_ULK_EV_SET_LOCK_RESP;
                ev_out->msg = msg; ev_out->txn = txn; ev_out->err = err; ev_out->retry = f->set_lock_retries;
            }
            return;
        }
    } else if (sdu[0] == 0x04u) {
        uint32_t st = 0u, lock = 0u;
        if (f->step == FGL_ULK_MASTER_WAIT && msg == FGL_LOC_REG_MASTER_CLIENT &&
            fgl_parse_two_stage_ind(sdu, size, NULL, &st, NULL)) {
            f->step = FGL_ULK_GET_LOCK_SEND;
            if (ev_out) { ev_out->kind = FGL_ULK_EV_MASTER_IND; ev_out->msg = msg; ev_out->ind_status = st; }
            return;
        }
        if (f->step == FGL_ULK_GET_LOCK_WAIT && msg == FGL_LOC_GET_ENGINE_LOCK &&
            fgl_parse_get_engine_lock_ind(sdu, size, &st, &lock)) {
            f->step = (st == FGL_IND_SUCCESS && lock == FGL_LOCK_NONE) ? FGL_ULK_DONE : FGL_ULK_SET_LOCK_SEND;
            if (ev_out) {
                ev_out->kind = FGL_ULK_EV_GET_LOCK_IND;
                ev_out->msg = msg; ev_out->ind_status = st; ev_out->lock_type = lock;
            }
            return;
        }
        if (f->step == FGL_ULK_SET_LOCK_WAIT && msg == FGL_LOC_SET_ENGINE_LOCK &&
            fgl_parse_two_stage_ind(sdu, size, NULL, &st, NULL)) {
            f->step = FGL_ULK_DONE;
            if (ev_out) { ev_out->kind = FGL_ULK_EV_SET_LOCK_IND; ev_out->msg = msg; ev_out->ind_status = st; }
            return;
        }
    }
}

#ifdef __cplusplus
}
#endif
#endif /* FUXI_GNSS_LOC_H */
