// SPDX-License-Identifier: BSD-2-Clause
// Adapted from UniXNU/QcomBaseband (commit dd0199cf3300, baseband/coachz_baseband.cpp,
// HoshinoYumeka @chiba233) for Xiaomi 13 (fuxi / SM8550 IPA v5.5). Opt-in via ADT /chosen "fuxi,modem-bb".
#include <pexpert/arm64/board_config.h>
#if defined(FUXI)
// The cellular bridge: the IOKit side of Apple's PCIe baseband (AppleBaseband, AppleBasebandPCIControl and one
// AppleBasebandPCIInterface per channel, with the user-client classes the sandbox knows them by) over this SoC's modem. ABM and
// BBUpdater talk to these as to an MHI modem; the QMUX channels go to the modem's QMI services over QRTR (coachz_bb_qmux.h), the
// sahara channel is answered by a device end that reads and drops the firmware it is offered (coachz_bb_sahara.h), and the board
// methods are answered by coachz_bb_control.h. Started only with /chosen/coachz-baseband (tools/xnu_screen.py --baseband).
//
// Threads: the Wi-Fi thread owns QRTR (bsd/dev/coachz_wifi.c): it hands modem messages to coachz_baseband_qrtr and sends what
// coachz_baseband_poll gives it. User threads only queue. One lock covers the bridge state and the queues; user memory is copied
// outside it, and asynchronous reads complete on a thread call.
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <libkern/c++/OSData.h>
#include <kern/thread_call.h>
#include <kern/clock.h>

#include "coachz_bb_qmux.h"
#include "coachz_bb_sahara.h"
#include "coachz_bb_control.h"
#include "coachz_bb_channels.h"
#include "coachz_bb_data.h"
#include "coachz_bb_pdp.h"
#include "coachz_bb_esim.h"
#include "fuxi_gnss_loc.h"
#include "fuxi_gnss_pds_bridge.h"

#define BB_FRAME_MAX (64u * 1024u)       // one read or write: a QMUX frame, a SAHARA packet or a READ_DATA answe
// Frames waiting for ABM, per channel. At boot the 53 clients' registrations and indications arrive faster than ABM reads; with 64 the
// queue overflowed every boot (5 to 22 frames), a lost VOICE answer made CommCenter reset the baseband (ATCS_TIMEOUT) and lost UIM and
// NAS frames left the SIM and registration state stale until the SIM was reinserted. A dropped frame is a lost answer: keep room.
#define BB_QUEUE 1024u
#define BB_READS 8u                      // asynchronous reads waiting for a frame, per channel
#define BB_TX 64u                        // QMI messages waiting for the Wi-Fi thread
#define BB_SYNC_READ_SECONDS 30u

// Which SAHARA images the device end asks for. SBL1 (Qualcomm image id 0x15) is the first image a boot loader requests; the rest of
// a Mav16 boot (rpm, tz, apps, acdb, mba, qdsp6sw in the bbfw) follow once BBUpdater's log shows how it takes this one.
static const uint32_t bb_sahara_images[] = { 0x15 };

namespace {
struct bb_frame { uint32_t len; uint8_t *data; };
struct bb_read { OSAsyncReference64 reference; IOMemoryDescriptor *memory; uint32_t len; void *owner; };
struct bb_channel {
  bb_frame frames[BB_QUEUE];
  uint32_t head, count;
  bb_read reads[BB_READS];
  uint32_t reads_waiting;
  void *owner;
  uint32_t opens, frames_in, frames_out, frames_dropped, bytes_in, bytes_out;
};
struct bb_tx { uint32_t local_port, node, port, len; uint8_t *data; };
}

static IOLock *bb_lock;
static thread_call_t bb_complete_call;
static struct cbq_bridge bb_qmux;
static struct cbs_device bb_sahara;
static struct cbc_state bb_control;
static bb_channel bb_ch[CBH_PUBLISHED];
static bb_tx bb_txq[BB_TX];
static uint32_t bb_tx_head, bb_tx_count, bb_tx_dropped;
static void (*bb_send)(uint32_t local_port, uint32_t node, uint32_t port, const void *message, uint32_t length);
static void (*bb_del_client)(uint32_t local_port);
static int bb_published;
static uint32_t bb_private_seen[64], bb_private_count, bb_ctl_logged, bb_ctl_refused;
static uint8_t bb_out[BB_FRAME_MAX];     // answers built under the lock
static struct cbp_client bb_pdp_clients[CBQ_MAX_CLIENTS];
static uint32_t bb_pdp_owners[CBQ_MAX_CLIENTS];
static struct cbd_setup bb_data;         // the data port setup ModemManager runs for IPA (coachz_bb_data.h)
static int bb_data_slot[2] = { -1, -1 }; // the bridge's own DPM and WDA clients
static uint8_t bb_data_wanted, bb_data_logged, bb_data_wait_logged;
static uint32_t bb_endpoint_fixes, bb_activations;
static uint8_t bb_fix[2048];
static struct cbe bb_esim;
static int bb_esim_slot = -1;
static bb_tx bb_uim_held[BB_TX];
static uint32_t bb_uim_head, bb_uim_count;
static uint8_t bb_uim_cid[BB_TX];
static uint8_t *bb_esim_in, *bb_esim_resp;
#define BB_ESIM_BYTES (1024u * 1024u)
static struct fgp_bridge bb_gnss;
static struct fgp_actions bb_gnss_act;
static struct fgp_scratch bb_gnss_scratch;
static int bb_loc_slot = -1;
static uint8_t bb_gnss_probe_step, bb_gnss_reg_fallback;
static struct fgl_unlock_fsm bb_gnss_unlock;
static uint32_t bb_gnss_reg_txn;
static uint64_t bb_gnss_reg_mask;
static uint32_t bb_gnss_probe_reg_txn, bb_gnss_probe_op_txn, bb_gnss_probe_utc_txn, bb_gnss_probe_start_txn;
static uint32_t bb_gnss_probe_logs, bb_gnss_diag_logs, bb_gnss_nmea_logs, bb_gnss_mock_logs;
static uint32_t bb_gnss_last_sv, bb_gnss_last_cn0_max, bb_gnss_last_utc_inject_ms;
static struct fgp_mock_cfg bb_gnss_mock_cfg;
#define BB_GNSS_PENDING 16u
static struct fgp_loc_msg bb_gnss_pending[BB_GNSS_PENDING];
static uint32_t bb_gnss_pending_head, bb_gnss_pending_count;

static uint32_t bb_now_ms(void)
{
  uint64_t ticks, ns;
  clock_get_uptime(&ticks);
  absolutetime_to_nanoseconds(ticks, &ns);
  return (uint32_t)(ns / 1000000u);
}

extern "C" {
int coachz_baseband_enabled(void);
void coachz_baseband_configure(void (*send)(uint32_t, uint32_t, uint32_t, const void *, uint32_t), void (*del_client)(uint32_t));
void coachz_baseband_new_server(uint32_t service, uint32_t version_instance, uint32_t node, uint32_t port);
void coachz_baseband_del_server(uint32_t service, uint32_t node, uint32_t port);
int coachz_baseband_qrtr(uint32_t dst_port, uint32_t src_node, uint32_t src_port, const void *body, uint32_t size);
void coachz_baseband_poll(void);
void coachz_baseband_report(unsigned second);
}

// ------------------------------------------------------------------ device tree
static OSData *bb_chosen(const char *path, const char *key)
{
  IORegistryEntry *entry = IORegistryEntry::fromPath(path, gIODTPlane);
  if (!entry) return nullptr;
  OSData *value = OSDynamicCast(OSData, entry->getProperty(key));
  if (value) value->retain();
  entry->release();
  return value;
}

// The raw /chosen "fuxi,modem-bb" word. Bit 0x10 (2026-11-22): VOICE call-state and modem IMS diagnostics (enum fields only).
// Bit 0x20: hand out all CBQ_MAX_CLIENTS client slots instead of the original 64.
static uint32_t bb_modem_bits(void)
{
  static int cached;
  static uint32_t bits;
  if (!cached) {
    OSData *value = bb_chosen("/chosen", "fuxi,modem-bb");
    if (value && value->getLength() == 4) memcpy(&bits, value->getBytesNoCopy(), 4);
    OSSafeReleaseNULL(value);
    cached = 1;
  }
  return bits;
}
static inline bool bb_diag(void) { return (bb_modem_bits() & 0x10u) != 0; }
static inline bool bb_ims_probe(void) { return (bb_modem_bits() & 0x200u) != 0; }
static inline bool bb_ims_disable_all(void) { return (bb_modem_bits() & 0x400u) != 0; }
static inline bool bb_ims_disable_volte(void) { return (bb_modem_bits() & 0x800u) != 0; }

// /chosen "fuxi,modem-gnss" (default 0 = disabled, 100% identical to mainline):
//   bit 0 (0x01): CBQ_INTERNAL QMI_LOC (0x10) standalone probe
//   bit 1 (0x02): QMI_PDS (0x06) <-> QMI_LOC (0x10) bidirectional bridge for locationd
//   bit 2 (0x04): safe GNSS diagnostics (never logs exact coordinates)
//   bit 3 (0x08): indoor mock_fix fallback (injects compliant 0x0024 -> PDS TLV 0x13 when no real fix)
static uint32_t bb_gnss_bits(void)
{
  static int cached;
  static uint32_t bits;
  if (!cached) {
    OSData *value = bb_chosen("/chosen", "fuxi,modem-gnss");
    if (value && value->getLength() == 4) memcpy(&bits, value->getBytesNoCopy(), 4);
    OSSafeReleaseNULL(value);
    cached = 1;
  }
  return bits;
}
static inline bool bb_gnss_probe(void) { return (bb_gnss_bits() & 0x01u) != 0; }
static inline bool bb_gnss_bridge(void) { return (bb_gnss_bits() & 0x02u) != 0; }
static inline bool bb_gnss_diag(void) { return (bb_gnss_bits() & 0x04u) != 0; }
static inline bool bb_gnss_mock(void) { return (bb_gnss_bits() & 0x08u) != 0; }

extern "C" int coachz_baseband_enabled(void)
{
  return bb_modem_bits() != 0;
}

// ------------------------------------------------------------------ queues (bb_lock held)
static void bb_complete(thread_call_param_t, thread_call_param_t);

static void bb_init(void)
{
  if (bb_lock) return;
  bb_lock = IOLockAlloc();
  bb_complete_call = thread_call_allocate(bb_complete, nullptr);
  cbq_init(&bb_qmux);
  bb_qmux.client_limit = (bb_modem_bits() & 0x20u) ? CBQ_MAX_CLIENTS : CBQ_DEFAULT_CLIENTS;
  bb_qmux.pds_bridge = bb_gnss_bridge() ? 1u : 0u;
  fgp_init(&bb_gnss);
  fgl_unlock_init(&bb_gnss_unlock);
  bb_gnss.mock_enabled = bb_gnss_mock() ? 1u : 0u;
  if (bb_gnss_mock()) {
    OSData *mval = bb_chosen("/chosen", "fuxi,gnss-mock");
    if (mval && mval->getLength() >= 16) {
      const uint8_t *mb = static_cast<const uint8_t *>(mval->getBytesNoCopy());
      bb_gnss_mock_cfg.lat_bits = fgl_rd64(mb);
      bb_gnss_mock_cfg.lon_bits = fgl_rd64(mb + 8);
    }
    OSSafeReleaseNULL(mval);
  }
  printf("COACHZ baseband: QMI client table %u of %u slots (fuxi,modem-bb 0x%x)\n", bb_qmux.client_limit, CBQ_MAX_CLIENTS, bb_modem_bits());
  if (bb_gnss_bits())
    printf("COACHZ gnss: mode 0x%x (probe=%u bridge=%u diag=%u mock=%u)\n", bb_gnss_bits(),
           bb_gnss_probe() ? 1u : 0u, bb_gnss_bridge() ? 1u : 0u,
           bb_gnss_diag() ? 1u : 0u, bb_gnss_mock() ? 1u : 0u);
  bb_esim_in = static_cast<uint8_t *>(IOMallocData(BB_ESIM_BYTES));
  bb_esim_resp = static_cast<uint8_t *>(IOMallocData(BB_ESIM_BYTES));
  cbe_init(&bb_esim, bb_esim_in, bb_esim_in ? BB_ESIM_BYTES : 0,
           bb_esim_resp, bb_esim_resp ? BB_ESIM_BYTES : 0);
  // Read-only eUICC answers (coachz_bb_esim.h): preserve 16 KiB per entry.
  const uint32_t cache_bytes = CBE_CACHE_ENTRIES * 16384u;
  uint8_t *cache = static_cast<uint8_t *>(IOMallocData(cache_bytes));
  cbe_set_cache(&bb_esim, cache, cache ? cache_bytes : 0);
  cbc_init(&bb_control);
  cbs_init(&bb_sahara, bb_sahara_images, sizeof bb_sahara_images / sizeof bb_sahara_images[0], 0x10000);
}

// Probe: QMI header fields of each message (service, type, message, transaction, length, QMI error), never TLV contents, so no APN,
// ICCID, APDU or address reaches the log. UIM, VINYL and WDS print every message; other services the first 6 of a kind, then each
// power of two. A budget keeps the log ring from filling; the periodic report says how many lines it held back.
#define BB_TRACE_KINDS 512u
#define BB_TRACE_BUDGET 6000u
static uint64_t bb_trace_key[BB_TRACE_KINDS];
static uint32_t bb_trace_count[BB_TRACE_KINDS], bb_trace_lines, bb_trace_held;
static uint32_t bb_state_trace_lines;

// 2026-11-22 bit 0x10: why a dialled call never leaves "dialling" (run native-hw000-b017-cell-vmd-1122: DIAL_CALL answered
// err 0, two ALL_CALL_STATUS, then nothing until the user's END_CALL 34 s later). Enum fields only: never a number, name, URI o
// error text. Layouts from libqmi data/qmi-service-voice.json, qmi-service-imsa.json, qmi-service-ims.json and
// qmi-enums-voice.h / qmi-enums-imsa.h (GitHub mirror linux-mobile-broadband/libqmi, read 2026-11-22).
#define BB_DIAG_LINES 600u
static uint32_t bb_diag_lines;

static const char *bb_safe_apn_tag(const uint8_t *v, uint32_t len, char *buf, uint32_t room)
{
  if (!v || !len) return "none";
  static const char *const known[] = { "ims", "internet", "hos", "mms", "xcap", "sos", "em", "default" };
  for (uint32_t k = 0; k < sizeof(known) / sizeof(known[0]); k++) {
    uint32_t n = 0;
    while (known[k][n]) n++;
    if (n == len) {
      bool eq = true;
      for (uint32_t i = 0; i < n; i++) {
        uint8_t c = v[i];
        if (c >= 'A' && c <= 'Z') c = (uint8_t)(c + ('a' - 'A'));
        if (c != (uint8_t)known[k][i]) { eq = false; break; }
      }
      if (eq) return known[k];
    }
  }
  snprintf(buf, room, "other(%u)", len);
  return buf;
}

// TLV ids of an SDU, e.g. "01 10 12". Sizes are left out: a number TLV's size is the number's length.
static void bb_tlv_ids(const uint8_t *sdu, uint32_t n, char *out, uint32_t room)
{
  uint32_t w = 0;
  out[0] = 0;
  if (!cbd_sdu_ok(sdu, n)) return;
  for (uint32_t at = 7u, end = 7u + cbd_rd16(sdu + 5); at < end && w + 4u < room; at += 3u + cbd_rd16(sdu + at + 1))
    w += (uint32_t)snprintf(out + w, room - w, "%s%02x", w ? " " : "", sdu[at]);
}

// For IMSA / IMS settings answers: TLVs of at most 4 bytes as " id=value" (little endian enums, flags, codes); longer ones
// (error text, URIs, APN lists) as " id:size" only. The result TLV 0x02 is left out.
static void bb_tlv_small(const uint8_t *sdu, uint32_t n, char *out, uint32_t room)
{
  uint32_t w = 0;
  out[0] = 0;
  if (!cbd_sdu_ok(sdu, n)) return;
  for (uint32_t at = 7u, end = 7u + cbd_rd16(sdu + 5); at < end && w + 24u < room; at += 3u + cbd_rd16(sdu + at + 1)) {
    const uint32_t t = sdu[at], len = cbd_rd16(sdu + at + 1);
    if (t == 0x02u) continue;
    uint32_t v = 0;
    for (uint32_t i = 0; i < len && i < 4u; i++) v |= (uint32_t)sdu[at + 3u + i] << (8u * i);
    w += (uint32_t)(len <= 4u ? snprintf(out + w, room - w, " %02x=%u", t, v) : snprintf(out + w, room - w, " %02x:%u", t, len));
  }
}

// VOICE (0x09). Call info {id, state, type, direction, mode, multiparty, als}: state 1 origination, 2 incoming, 3 conversation,
// 4 cc in progress, 5 alerting, 8 disconnecting, 9 end, 0x0a setup, 0x0b pre-alerting; type 0 voice, 2 voice over IP, 9 emergency;
// direction 1 MO, 2 MT; mode 2 GSM, 3 UMTS, 4 LTE (Qualcomm firmware adds more, e.g. NR5G).
static void bb_trace_voice(uint32_t type, uint32_t message, const uint8_t *sdu, uint32_t n)
{
  if (bb_diag_lines >= BB_DIAG_LINES || !cbd_sdu_ok(sdu, n)) return;
  char ids[64];
  uint32_t len = 0;
  const uint8_t *v;
  if (message == 0x20u && type == 0u) {
    // DIAL_CALL request: TLV 0x01 is the number (never printed); 0x10 the call type.
    bb_tlv_ids(sdu, n, ids, sizeof ids);
    v = cbd_find(sdu, n, 0x10u, &len);
    bb_diag_lines++;
    printf("COACHZ voice: dial request tlv [%s] call_type=%d t=%u\n", ids, v && len == 1u ? (int)v[0] : -1, bb_now_ms());
  } else if ((message == 0x20u || message == 0x21u) && type == 2u) {
    bb_tlv_ids(sdu, n, ids, sizeof ids);
    v = cbd_find(sdu, n, 0x10u, &len);
    bb_diag_lines++;
    printf("COACHZ voice: %s answer err 0x%x tlv [%s] call_id=%d t=%u\n", message == 0x20u ? "dial" : "end",
           cbd_error(sdu, n), ids, v && len == 1u ? (int)v[0] : -1, bb_now_ms());
  } else if ((message == 0x2eu && type == 4u) || (message == 0x2fu && type == 2u)) {
    // ALL_CALL_STATUS indication (info 0x01, alerting 0x12) or GET_ALL_CALL_INFO answer (info 0x10, alerting 0x13).
    const bool ind = message == 0x2eu;
    bb_tlv_ids(sdu, n, ids, sizeof ids);
    v = cbd_find(sdu, n, ind ? 0x01u : 0x10u, &len);
    const uint32_t count = v && len >= 1u && len == 1u + 7u * v[0] ? v[0] : 0u;
    bb_diag_lines++;
    printf("COACHZ voice: %s tlv [%s] calls=%u t=%u\n", ind ? "status" : "info", ids, count, bb_now_ms());
    for (uint32_t i = 0; i < count && i < 4u && bb_diag_lines < BB_DIAG_LINES; i++) {
      const uint8_t *c = v + 1u + 7u * i;
      bb_diag_lines++;
      printf("COACHZ voice:   call id=%u state=%u type=%u dir=%u mode=%u mpty=%u als=%u\n", c[0], c[1], c[2], c[3], c[4], c[5], c[6]);
    }
    const uint8_t *a = cbd_find(sdu, n, ind ? 0x12u : 0x13u, &len);
    if (a && len >= 1u && len == 1u + 2u * a[0])
      for (uint32_t i = 0; i < a[0] && i < 4u && bb_diag_lines < BB_DIAG_LINES; i++, bb_diag_lines++)
        printf("COACHZ voice:   alerting id=%u type=%u\n", a[1u + 2u * i], a[2u + 2u * i]);
    // Qualcomm's indication also carries the call end reasons {id, u16 reason} (libqmi does not name that TLV): 0x14 is
    // taken only when its size fits that array exactly.
    const uint8_t *e = ind ? cbd_find(sdu, n, 0x14u, &len) : nullptr;
    if (e && len >= 1u && len == 1u + 3u * e[0])
      for (uint32_t i = 0; i < e[0] && i < 4u && bb_diag_lines < BB_DIAG_LINES; i++, bb_diag_lines++)
        printf("COACHZ voice:   end id=%u reason=%u\n", e[1u + 3u * i], cbd_rd16(e + 2u + 3u * i));
  }
}

static void bb_trace_volte_qmi(uint32_t service, uint32_t type, uint32_t message, const uint8_t *sdu, uint32_t n)
{
  if (bb_diag_lines >= BB_DIAG_LINES || !cbd_sdu_ok(sdu, n)) return;
  char ids[96];
  char apn_buf[24];
  uint32_t len = 0;
  if (service == 0x03u && ((message == 0x33u && (type == 0u || type == 2u)) || (message == 0x34u && type == 2u))) {
    bb_tlv_ids(sdu, n, ids, sizeof ids);
    const uint8_t *vd = cbd_find(sdu, n, 0x21u, &len);
    const int vdom = (vd && len == 4u) ? (int)(vd[0] | ((uint32_t)vd[1] << 8)) : -1;
    const uint8_t *mp = cbd_find(sdu, n, 0x11u, &len);
    const uint32_t mode = (mp && len >= 2u) ? cbd_rd16(mp) : 0u;
    const uint8_t *sd = cbd_find(sdu, n, 0x1cu, &len);
    const int sdom = (sd && len == 4u) ? (int)sd[0] : -1;
    bb_diag_lines++;
    printf("COACHZ nas: ssp type=%u msg=0x%x err=0x%x tlv=[%s] vdom=%d mode=0x%x sdom=%d t=%u\n",
           type, message, type == 2u ? cbd_error(sdu, n) : 0u, ids, vdom, mode, sdom, bb_now_ms());
  } else if (service == CBD_SVC_WDS && message == 0x20u && type == 0u) {
    bb_tlv_ids(sdu, n, ids, sizeof ids);
    const uint8_t *apn = cbd_find(sdu, n, 0x14u, &len);
    const char *apn_tag = bb_safe_apn_tag(apn, len, apn_buf, sizeof apn_buf);
    const uint8_t *prof = cbd_find(sdu, n, 0x31u, &len);
    const int pidx = (prof && len == 1u) ? (int)prof[0] : -1;
    const uint8_t *fam = cbd_find(sdu, n, 0x19u, &len);
    const int ipf = (fam && len == 1u) ? (int)fam[0] : -1;
    bb_diag_lines++;
    printf("COACHZ wds: start req tlv=[%s] apn=%s prof3gpp=%d ipfam=%d t=%u\n",
           ids, apn_tag, pidx, ipf, bb_now_ms());
  } else if (service == CBD_SVC_WDS && message == 0x27u && (type == 0u || type == 2u)) {
    bb_tlv_ids(sdu, n, ids, sizeof ids);
    if (type == 0u) {
      const uint8_t *id = cbd_find(sdu, n, 0x01u, &len);
      bb_diag_lines++;
      printf("COACHZ wds: get_prof req type=%d idx=%d t=%u\n",
             id && len >= 2u ? (int)id[0] : -1, id && len >= 2u ? (int)id[1] : -1, bb_now_ms());
    } else {
      const uint8_t *apn = cbd_find(sdu, n, 0x14u, &len);
      const char *apn_tag = bb_safe_apn_tag(apn, len, apn_buf, sizeof apn_buf);
      const uint8_t *pdp = cbd_find(sdu, n, 0x11u, &len);
      const int pdpt = (pdp && len == 1u) ? (int)pdp[0] : -1;
      bb_diag_lines++;
      printf("COACHZ wds: get_prof resp err=0x%x tlv=[%s] apn=%s pdp=%d t=%u\n",
             cbd_error(sdu, n), ids, apn_tag, pdpt, bb_now_ms());
    }
  } else if (service == CBD_SVC_WDS && message == 0x2du && type == 2u) {
    bb_tlv_ids(sdu, n, ids, sizeof ids);
    const uint8_t *apn = cbd_find(sdu, n, 0x14u, &len);
    const char *apn_tag = bb_safe_apn_tag(apn, len, apn_buf, sizeof apn_buf);
    uint32_t l2 = 0;
    const uint32_t ip4 = cbd_find(sdu, n, 0x1eu, &l2) != nullptr;
    const uint32_t ip6 = cbd_find(sdu, n, 0x25u, &l2) != nullptr;
    const uint32_t dns4 = cbd_find(sdu, n, 0x15u, &l2) != nullptr;
    const uint32_t dns6 = cbd_find(sdu, n, 0x27u, &l2) != nullptr;
    const uint32_t pcscf4 = cbd_find(sdu, n, 0x22u, &l2) != nullptr;
    const uint32_t pcscf6 = cbd_find(sdu, n, 0x23u, &l2) != nullptr;
    const uint32_t pcscfd = cbd_find(sdu, n, 0x21u, &l2) != nullptr;
    bb_diag_lines++;
    printf("COACHZ wds: settings resp err=0x%x apn=%s ip4=%u ip6=%u dns4=%u dns6=%u pcscf4=%u pcscf6=%u pcscfd=%u tlv=[%s] t=%u\n",
           cbd_error(sdu, n), apn_tag, ip4, ip6, dns4, dns6, pcscf4, pcscf6, pcscfd, ids, bb_now_ms());
  }
}

static void bb_trace_state(uint32_t service, uint32_t type, uint32_t message, const uint8_t *sdu, uint32_t n)
{
  if (service == 0x09u && bb_diag()) bb_trace_voice(type, message, sdu, n);
  if (bb_diag()) bb_trace_volte_qmi(service, type, message, sdu, n);
  const uint32_t state_cap = bb_diag() ? 160u : 64u;
  if (bb_state_trace_lines >= state_cap || !cbd_sdu_ok(sdu, n)) return;
  uint32_t len = 0;
  // Privacy-safe enum-only diagnostics: never log PLMN, cell ID, ICCID, IMSI, IP or APN.
  if (service == 0x02u && ((type == 0u && message == 0x2eu) || (type == 2u && message == 0x2du))) {
    const uint8_t *v = cbd_find(sdu, n, 0x01u, &len);
    if (v && len >= 1u) {
      bb_state_trace_lines++;
      printf("COACHZ baseband: DMS opmode %s=%u t=%u\n",
             message == 0x2eu ? "set" : "get", (unsigned)v[0], bb_now_ms());
    }
  } else if (service == 0x03u && type == 4u && message == 0x24u) {
    const uint8_t *v = cbd_find(sdu, n, 0x01u, &len);
    if (v && len >= 5u) {
      const uint32_t nrat = v[4];
      const uint32_t rat0 = (nrat > 0u && len >= 6u) ? (uint32_t)v[5] : 0u;
      bb_state_trace_lines++;
      printf("COACHZ baseband: NAS serving reg=%u cs=%u ps=%u nrat=%u rat0=%u t=%u\n",
             (unsigned)v[0], (unsigned)v[1], (unsigned)v[2], nrat, rat0, bb_now_ms());
    }
  } else if (service == 0x03u && ((type == 2u && message == 0x4du) || (type == 4u && message == 0x4eu))) {
    uint32_t lte_valid = 0u, lte_srv = 0u, nr_mask = 0u;
    const uint8_t *v = cbd_find(sdu, n, 0x14u, &len);
    if (v && len >= 2u) { lte_valid = 1u; lte_srv = v[0]; }
    if (cbd_find(sdu, n, 0x28u, &len)) nr_mask |= 1u << 0;
    if (cbd_find(sdu, n, 0x29u, &len)) nr_mask |= 1u << 1;
    if (cbd_find(sdu, n, 0x31u, &len)) nr_mask |= 1u << 2;
    if (cbd_find(sdu, n, 0x32u, &len)) nr_mask |= 1u << 3;
    const uint8_t *vp = cbd_find(sdu, n, 0x21u, &len);
    const int lte_vops = (vp && len == 1u) ? (int)vp[0] : -1;
    const uint8_t *rd = cbd_find(sdu, n, 0x25u, &len);
    const int lte_rdom = (rd && len == 4u) ? (int)rd[0] : -1;
    const uint8_t *li = cbd_find(sdu, n, 0x19u, &len);
    const int lte_dom = (li && len >= 4u && li[0]) ? (int)li[1] : -1;
    const int lte_cap = (li && len >= 4u && li[2]) ? (int)li[3] : -1;
    bb_state_trace_lines++;
    if (bb_diag()) {
      char ids[96];
      bb_tlv_ids(sdu, n, ids, sizeof ids);
      printf("COACHZ baseband: NAS sysinfo msg=0x%x lte_valid=%u lte_srv=%u lte_vops=%d lte_rdom=%d lte_dom=%d lte_cap=%d nr_mask=0x%x tlv=[%s] t=%u\n",
             message, lte_valid, lte_srv, lte_vops, lte_rdom, lte_dom, lte_cap, nr_mask, ids, bb_now_ms());
    } else {
      printf("COACHZ baseband: NAS sysinfo msg=0x%x lte_valid=%u lte_srv=%u nr_mask=0x%x t=%u\n",
             message, lte_valid, lte_srv, nr_mask, bb_now_ms());
    }
  }
}

static void bb_trace(const char *dir, uint32_t channel, uint32_t service, uint32_t cid, const uint8_t *sdu, uint32_t n)
{
  if (!service || n < 7u) return;   // CTL has its own SDU layout and its own lines
  const uint32_t type = sdu[0], txn = cbd_rd16(sdu + 1), message = cbd_rd16(sdu + 3);
  bb_trace_state(service, type, message, sdu, n);
  const uint32_t error = type == 2u ? (cbd_sdu_ok(sdu, n) ? cbd_error(sdu, n) : 0xfffeu) : 0u;
  const uint64_t key = 1u + ((uint64_t)((uint32_t)dir[0] << 8 | (uint32_t)dir[4]) << 48 |(uint64_t)service << 40 | (uint64_t)type << 32 |
    (uint64_t)message << 16 | error);
  uint32_t count = 0;
  for (uint32_t i = 0, at = (uint32_t)(key * 0x9e3779b97f4a7c15ull >> 55) % BB_TRACE_KINDS; i < BB_TRACE_KINDS; i++, at = (at + 1) % BB_TRACE_KINDS) {
    if (bb_trace_key[at] != key && bb_trace_key[at]) continue;
    bb_trace_key[at] = key;
    count = ++bb_trace_count[at];
    break;
  }
  const bool always = service == CBE_SVC_UIM || service == CBE_SVC_VINYL || service == CBD_SVC_WDS || service == 0x05u ||
    (service == 0x09u && bb_diag());
  if (!always && count > 6u && (count & (count - 1u))) return;
  if (bb_trace_lines >= BB_TRACE_BUDGET) { bb_trace_held++; return; }
  bb_trace_lines++;
  printf("COACHZ qmi: %s ch %u svc 0x%x cid %u type %u msg 0x%x txn %u len %u err 0x%x n %u t %u\n",
         dir, channel, service, cid, type, message, txn, n, error, count, bb_now_ms());
}
// A QMUX frame (I/F type 1, length, flags, service, client id, then the SDU).
static void bb_trace_frame(const char *dir, uint32_t channel, const uint8_t *frame, uint32_t len)
{
  if (len >= CBQ_HDR + 7u && frame[0] == 1u) bb_trace(dir, channel, frame[4], frame[5], frame + CBQ_HDR, len - CBQ_HDR);
}

static void bb_push(uint32_t channel, const uint8_t *data, uint32_t len)
{
  if (channel == CBH_QMUX1 || channel == CBH_QMUX2) bb_trace_frame("to-host", channel == CBH_QMUX1 ? 1u : 2u, data, len);
  bb_channel *c = &bb_ch[channel];
  uint8_t *copy = len && len <= BB_FRAME_MAX ? static_cast<uint8_t *>(IOMallocData(len)) : nullptr;
  if (!copy || c->count == BB_QUEUE) {
    if (copy) IOFreeData(copy, len);
    // Probe: which answer was lost (header fields only).
    if (c->frames_dropped < 32u)
      printf("COACHZ baseband: dropped frame for ABM on channel %u (%s, %u queued, %u bytes) svc 0x%x type %u msg 0x%x\n", channel,
             copy ? "queue full" : "no memory", c->count, len, len > 4 ? data[4] : 0u, len > CBQ_HDR ? data[CBQ_HDR] : 0u,
             len > CBQ_HDR + 4 ? (uint32_t)(data[CBQ_HDR + 3] | data[CBQ_HDR + 4] << 8) : 0u);
    c->frames_dropped++;
    return;
  }
  memcpy(copy, data, len);
  c->frames[(c->head + c->count++) % BB_QUEUE] = { len, copy };
  c->frames_out++;
  c->bytes_out += len;
  IOLockWakeup(bb_lock, c, false);
  if (c->reads_waiting && bb_complete_call) thread_call_enter(bb_complete_call);
}

static bb_frame bb_pop(uint32_t channel)
{
  bb_channel *c = &bb_ch[channel];
  bb_frame f = { 0, nullptr };
  if (!c->count) return f;
  f = c->frames[c->head];
  c->head = (c->head + 1) % BB_QUEUE;
  c->count--;
  return f;
}

static void bb_flush(uint32_t channel)
{
  for (bb_frame f = bb_pop(channel); f.data; f = bb_pop(channel)) IOFreeData(f.data, f.len);
}

// A QMI message for the modem, sent by the Wi-Fi thread from coachz_baseband_poll.
static void bb_queue_tx(uint32_t local_port, uint32_t node, uint32_t port, const uint8_t *data, uint32_t len)
{
  uint8_t *copy = len ? static_cast<uint8_t *>(IOMallocData(len)) : nullptr;
  if (!copy || bb_tx_count == BB_TX) {
    if (copy) IOFreeData(copy, len);
    bb_tx_dropped++;
    return;
  }
  memcpy(copy, data, len);
  bb_txq[(bb_tx_head + bb_tx_count++) % BB_TX] = { local_port, node, port, len, copy };
  const uint32_t slot = local_port - CBQ_PORT_BASE;
  if (slot < CBQ_MAX_CLIENTS && bb_qmux.client[slot].used && bb_qmux.client[slot].channel == CBQ_INTERNAL)
    bb_trace("int-tx", 0, bb_qmux.client[slot].service, bb_qmux.client[slot].cid, data, len);
}

// BBUEUR13Loader pushes the SBL with SendImage on pbl, frees that transport, opens sahara and waits 40 s for the loaded SBL's HELLO there. So the
// HELLO goes out when sahara opens after a SendImage.
static bool bb_boot_image_sent;

static void bb_sahara_restart(void)
{
  struct cbs_out o;
  bb_flush(CBH_SAHARA);
  cbs_init(&bb_sahara, bb_sahara_images, sizeof bb_sahara_images / sizeof bb_sahara_images[0], 0x10000);
  cbs_start(&bb_sahara, &o);
  for (uint32_t i = 0; i < o.count; i++) bb_push(CBH_SAHARA, o.data[i], o.len[i]);
  printf("COACHZ baseband: sahara hello\n");
}

static void bb_private_note(uint32_t service, uint32_t message, const uint8_t *sdu, uint32_t len)
{
  const uint32_t key = service << 16 | (message & 0xffff);
  for (uint32_t i = 0; i < bb_private_count; i++)
    if (bb_private_seen[i] == key) return;
  if (bb_private_count < 64) bb_private_seen[bb_private_count++] = key;
  if (bb_diag() && sdu) {
    char ids[64];
    bb_tlv_ids(sdu, len, ids, sizeof ids);
    printf("COACHZ baseband: private QMI service 0x%x message 0x%x tlv=[%s] answered not supported\n", service, message, ids);
  } else {
    printf("COACHZ baseband: private QMI service 0x%x message 0x%x answered not supported\n", service, message);
  }
}

// ------------------------------------------------------------------ data port setup (bb_lock held)
static void bb_data_log_done(void)
{
  if (bb_data_logged) return;
  bb_data_logged = 1;
  printf("COACHZ baseband: data port: DPM open %s 0x%x; WDA set data format error 0x%x, link %d, uplink %d, downlink %d (asked %u), %d datagrams, %d bytes\n",
         (bb_data.has_dpm || bb_data.dpm_error != 0xffffu) ? "error" : "skipped, no service,", bb_data.dpm_error, bb_data.wda_error,
         (int)bb_data.llp, (int)bb_data.ul_dap, (int)bb_data.dl_dap, cbd_daps[bb_data.dap_index], (int)bb_data.dl_datagrams, (int)bb_data.dl_size);
}

static void bb_data_try(void)
{
  if (!bb_data_wanted || bb_data.step == CBD_DONE || bb_data.step == CBD_DPM || bb_data.step == CBD_WDA)
    return;
  bb_data.has_dpm = cbq_server_find(&bb_qmux, CBD_SVC_DPM) != nullptr;
  bb_data.has_wda = cbq_server_find(&bb_qmux, CBD_SVC_WDA) != nullptr;
  if (bb_data.step == CBD_IDLE && !bb_data.has_dpm) {
    if (!(bb_data_wait_logged & 1u)) {
      bb_data_wait_logged |= 1u;
      printf("COACHZ baseband: data port: waiting for DPM service\n");
    }
    return;
  }
  if ((bb_data.step == CBD_WAIT_WDA || bb_data.step == CBD_WDA_NEXT) && !bb_data.has_wda) {
    if (!(bb_data_wait_logged & 2u)) {
      bb_data_wait_logged |= 2u;
      printf("COACHZ baseband: data port: DPM open error 0x%x; waiting for WDA service\n", bb_data.dpm_error);
    }
    return;
  }
  if (bb_tx_count >= BB_TX) return;
  const uint32_t next_svc = bb_data.step == CBD_IDLE ? CBD_SVC_DPM : CBD_SVC_WDA;
  const struct cbq_server *server = cbq_server_find(&bb_qmux, next_svc);
  if (!server) return;
  int *slot = &bb_data_slot[next_svc == CBD_SVC_DPM ? 0 : 1];
  if (*slot < 0) *slot = cbq_client_alloc(&bb_qmux, CBQ_INTERNAL, next_svc);
  if (*slot < 0) {
    static uint32_t noslot_logged;
    if (noslot_logged++ < 4u)
      printf("COACHZ baseband: data port: no client for service %u (retrying)\n", next_svc);
    return;
  }
  uint8_t sdu[128];
  uint32_t service = 0;
  const struct cbd_setup saved = bb_data;
  const uint32_t n = cbd_next(&bb_data, sdu, sizeof sdu, &service);
  if (!n || service != next_svc) {
    if (bb_data.step == CBD_DONE) bb_data_log_done();
    return;
  }
  const uint32_t queued = bb_tx_count;
  bb_queue_tx(CBQ_PORT_BASE + (uint32_t)*slot, server->node, server->port, sdu, n);
  if (bb_tx_count == queued) bb_data = saved;
}

// 2026-11-22 bit 0x10: the bridge's own read-only IMSA (0x21) and IMS settings (0x12) clients. Asked at every report (each 30 s
// for the first 5 minutes, then each 10 minutes): is the modem's IMS stack registered, which services it can carry, which ones are
// enabled. CommCenter itself opened neither service in the runs so far. IMSA 0x22 only subscribes this client to the two status
// indications; 0x20, 0x21 and IMS settings 0x90 are reads.
#define BB_SVC_IMSS 0x12u
#define BB_SVC_IMSA 0x21u
static int bb_ims_slot[2] = { -1, -1 };   // IMSA, IMS settings
static uint32_t bb_ims_txn, bb_ims_polls, bb_ims_noslot;

static void bb_ims_send(uint32_t which, uint32_t message, const uint8_t *tlvs, uint32_t tlv_len)
{
  const uint32_t service = which ? BB_SVC_IMSS : BB_SVC_IMSA;
  const struct cbq_server *server = cbq_server_find(&bb_qmux, service);
  uint8_t sdu[64];
  if (!server || tlv_len > sizeof sdu - 7u) return;
  int *slot = &bb_ims_slot[which];
  if (*slot < 0) *slot = cbq_client_alloc(&bb_qmux, CBQ_INTERNAL, service);
  if (*slot < 0) {
    if (bb_ims_noslot++ < 4u) printf("COACHZ ims: no client slot for service 0x%x\n", service);
    return;
  }
  if (tlv_len) memcpy(sdu + 7, tlvs, tlv_len);
  bb_ims_txn = bb_ims_txn % 0xfff0u + 1u;
  bb_queue_tx(CBQ_PORT_BASE + (uint32_t)*slot, server->node, server->port, sdu, cbd_request(sdu, bb_ims_txn, message, tlv_len));
}

// 2026-11-22 bits 0x200 / 0x400 / 0x800:
// - 0x200: bind IMSA (0x33) & IMSS (0x98) to subscription 0 (fallback 1), read IMSS 0x90, and run IMSA 0x20 liveness pings
// - 0x400: after IMSS bind+read, send IMSS 0x8f disabling all on-modem IMS services (RAM-only rmtfs; AP libIPTelephony owns VoLTE)
// - 0x800: after IMSS bind+read, send IMSS 0x8f disabling only on-modem VoLTE/VT/VoWiFi (0x10/0x11/0x14)
static uint32_t bb_imsa_step, bb_imss_step;
static uint32_t bb_ims_probe_sent, bb_ims_probe_events, bb_ims_probe_next_ms;

static void bb_ims_send_bind(uint32_t which, uint32_t sub)
{
  const uint8_t tag = which ? 0x01u : 0x10u;
  const uint8_t tlv[7] = { tag, 4u, 0u, (uint8_t)(sub & 0xffu), 0u, 0u, 0u };
  bb_ims_send(which, which ? 0x98u : 0x33u, tlv, sizeof tlv);
  printf("COACHZ ims-cfg: %s bind(%u) tx txn=%u t=%u\n", which ? "imss" : "imsa", sub, bb_ims_txn, bb_now_ms());
}

static void bb_ims_send_disable(bool all_services)
{
  static const uint8_t tlvs_all[] = {
    0x10u, 1u, 0u, 0u,  // VoLTE = 0
    0x11u, 1u, 0u, 0u,  // VT = 0
    0x14u, 1u, 0u, 0u,  // VoWiFi = 0
    0x18u, 1u, 0u, 0u,  // IMS Service Enabled = 0
    0x19u, 1u, 0u, 0u,  // UT = 0
    0x1au, 1u, 0u, 0u,  // SMS = 0
    0x1cu, 1u, 0u, 0u,  // USSD = 0
    0x1eu, 1u, 0u, 0u,  // Presence = 0
  };
  static const uint8_t tlvs_volte[] = {
    0x10u, 1u, 0u, 0u,  // VoLTE = 0
    0x11u, 1u, 0u, 0u,  // VT = 0
    0x14u, 1u, 0u, 0u,  // VoWiFi = 0
  };
  const uint8_t *p = all_services ? tlvs_all : tlvs_volte;
  const uint32_t len = all_services ? (uint32_t)sizeof tlvs_all : (uint32_t)sizeof tlvs_volte;
  bb_ims_send(1, 0x8fu, p, len);
  printf("COACHZ ims-cfg: imss set 0x8f (%s) tx txn=%u t=%u\n", all_services ? "all=0" : "volte=0", bb_ims_txn, bb_now_ms());
}

static void bb_ims_cfg_poll(void)
{
  const bool want_imsa = bb_ims_probe();
  const bool want_imss = bb_ims_probe() || bb_ims_disable_all() || bb_ims_disable_volte();
  if (want_imsa && bb_imsa_step == 0u && cbq_server_find(&bb_qmux, BB_SVC_IMSA)) {
    bb_imsa_step = 1u;
    bb_ims_send_bind(0, 0u);
  }
  if (want_imss && bb_imss_step == 0u && cbq_server_find(&bb_qmux, BB_SVC_IMSS)) {
    bb_imss_step = 1u;
    bb_ims_send_bind(1, 0u);
  }
}

static void bb_ims_poll(void)
{
  if (!bb_ims_polls++) {
    const uint8_t subscribe[] = { 0x10, 1, 0, 1, 0x11, 1, 0, 1 };
    bb_ims_send(0, 0x22u, subscribe, sizeof subscribe);
  }
  bb_ims_send(0, 0x20u, nullptr, 0);   // registration: 0x12 status 0 not, 1 registering, 2 registered, 3 limited; 0x11 error code
  bb_ims_send(0, 0x21u, nullptr, 0);   // services: 0x10 SMS, 0x11 voice, 0x12 video; 0 unavailable, 1 limited, 2 available
  bb_ims_send(1, 0x90u, nullptr, 0);   // enabled: 0x11 voice, 0x12 video, 0x15 Wi-Fi voice, 0x19 registration, 0x1b SMS
}

static void bb_ims_answer(uint32_t which, const uint8_t *sdu, uint32_t n)
{
  if (!cbd_sdu_ok(sdu, n)) return;
  char vals[200];
  bb_tlv_small(sdu, n, vals, sizeof vals);
  const uint32_t msg = cbd_rd16(sdu + 3);
  const uint32_t err = sdu[0] == 2u ? cbd_error(sdu, n) : 0u;
  if (which == 0u && msg == 0x33u && sdu[0] == 2u) {
    printf("COACHZ ims-cfg: imsa bind rsp step=%u err=0x%x%s t=%u\n", bb_imsa_step, err, vals, bb_now_ms());
    if (err && bb_imsa_step == 1u) {
      bb_imsa_step = 2u;
      bb_ims_send_bind(0, 1u);
    } else {
      bb_imsa_step = 3u;
    }
  } else if (which == 1u && (bb_ims_probe() || bb_ims_disable_all() || bb_ims_disable_volte())) {
    printf("COACHZ ims-cfg: imss rx type=%u msg=0x%x step=%u err=0x%x%s t=%u\n",
           sdu[0], msg, bb_imss_step, err, vals, bb_now_ms());
    if (sdu[0] == 2u && msg == 0x98u) {
      if (err && bb_imss_step == 1u) {
        bb_imss_step = 2u;
        bb_ims_send_bind(1, 1u);
      } else {
        bb_imss_step = 3u;
        bb_ims_send(1, 0x90u, nullptr, 0);
      }
    } else if (sdu[0] == 2u && msg == 0x90u && bb_imss_step == 3u) {
      if (bb_ims_disable_all() || bb_ims_disable_volte()) {
        bb_imss_step = 4u;
        bb_ims_send_disable(bb_ims_disable_all());
      } else {
        bb_imss_step = 5u;
      }
    } else if (sdu[0] == 2u && msg == 0x8fu && bb_imss_step == 4u) {
      bb_imss_step = 5u;
      bb_ims_send(1, 0x90u, nullptr, 0);
    }
  } else if (bb_ims_probe() && which == 0u) {
    static uint32_t bb_ims_probe_rx;
    if (bb_ims_probe_rx++ < 80u)
      printf("COACHZ ims-probe: rx txn=%u msg=0x%x err=0x%x%s t=%u\n",
             cbd_rd16(sdu + 1), msg, err, vals, bb_now_ms());
  }
  if (!bb_diag() || bb_diag_lines >= BB_DIAG_LINES) return;
  bb_diag_lines++;
  printf("COACHZ ims: %s type %u msg 0x%x err 0x%x%s t=%u\n", which ? "imss" : "imsa", sdu[0], msg,
         err, vals, bb_now_ms());
}

static void bb_ims_probe_ping(const char *tag)
{
  if (!bb_ims_probe() || bb_ims_probe_sent >= 64u || bb_tx_count + 4u >= BB_TX) return;
  if (!cbq_server_find(&bb_qmux, BB_SVC_IMSA) || bb_imsa_step < 3u) return;
  bb_ims_probe_sent++;
  bb_ims_send(0, 0x20u, nullptr, 0);
  printf("COACHZ ims-probe: tx #%u txn=%u tag=%s t=%u\n", bb_ims_probe_sent, bb_ims_txn, tag, bb_now_ms());
}

static void bb_ims_probe_event(const char *tag, const uint8_t *sdu, uint32_t n)
{
  if (!bb_ims_probe() || bb_ims_probe_events >= 24u || !cbd_sdu_ok(sdu, n)) return;
  char vals[200];
  bb_tlv_small(sdu, n, vals, sizeof vals);
  bb_ims_probe_events++;
  printf("COACHZ ims-probe: ev #%u %s type=%u msg=0x%x len=%u%s t=%u\n",
         bb_ims_probe_events, tag, sdu[0], cbd_rd16(sdu + 3), n, vals, bb_now_ms());
  bb_ims_probe_ping(tag);
}

static void bb_data_start(void)
{
  bb_data_wanted = 1;
  bb_data_try();
}

// WDS is still entirely owned by CommCenter. Observe only wire lifecycle facts, never credentials or addresses.
static void bb_pdp_note(uint32_t local_port, uint32_t cid, uint32_t channel, const uint8_t *sdu, uint32_t n)
{
  const uint32_t slot = local_port - CBQ_PORT_BASE;
  if (slot >= CBQ_MAX_CLIENTS || !cbd_sdu_ok(sdu, n)) return;
  const uint32_t owner = 1u + cid + (channel << 8);
  if (bb_pdp_owners[slot] != owner) {
    bzero(&bb_pdp_clients[slot], sizeof bb_pdp_clients[slot]);
    bb_pdp_clients[slot].family = 4; // WDS default family, overridden by Set IP Family or Start Network
    bb_pdp_owners[slot] = owner;
  }
  cbp_client &c = bb_pdp_clients[slot];
  if (cbp_observe(&c, sdu, n))
    printf("COACHZ pdp: WDS type %u message 0x%x txn %u mux %u family %u active %u error %u address-fields %u\n",
      sdu[0], cbd_rd16(sdu + 3), cbd_rd16(sdu + 1), c.mux, c.family, c.active,
      sdu[0] == 2 ? cbd_error(sdu, n) : 0, c.settings);
  const bool start_failed = sdu[0] == 2 && cbd_rd16(sdu + 3) == CBP_START && cbd_error(sdu, n);
  if (start_failed || (sdu[0] == 4 && cbd_rd16(sdu + 3) == CBP_STATUS && !c.active)) {
    // libqmi WDS 0x20 responses and 0x22 indications use the same call-end TLVs.
    uint32_t reason_size = 0, verbose_size = 0;
    const uint8_t *reason = cbd_find(sdu, n, 0x10, &reason_size);
    const uint8_t *verbose = cbd_find(sdu, n, 0x11, &verbose_size);
    printf("COACHZ pdp: %s cid %u channel %u mux %u family %u reason-present %u reason %u verbose-present %u type %u code %d\n",
      start_failed ? "start failed" : "disconnected", cid, channel, c.mux, c.family, reason && reason_size == 2u,
      reason && reason_size == 2u ? cbd_rd16(reason) : 0u, verbose && verbose_size == 4u,
      verbose && verbose_size == 4u ? cbd_rd16(verbose) : 0u,
      verbose && verbose_size == 4u ? static_cast<int16_t>(cbd_rd16(verbose + 2)) : 0);
  }
}

static void bb_sim_request(const struct cbq_result &r, const uint8_t *sdu);

// A request for the modem, with the data port named as this modem has it.
static void bb_to_modem(const struct cbq_result &r, const uint8_t *sdu)
{
  if (r.service == CBD_SVC_WDS) {
    if (!bb_data_wanted) bb_data_start();
    bb_pdp_note(r.local_port, r.cid, r.channel, sdu, r.len);
  }
  bb_sim_request(r, sdu);
  if (r.service == CBE_SVC_UIM && bb_esim.route_failed) {
    const uint32_t n = cbq_private_answer(sdu, r.service, r.cid, CBE_ERR_INTERNAL, nullptr, 0, bb_out, sizeof bb_out);
    if (n) bb_push(r.channel ? CBH_QMUX2 : CBH_QMUX1, bb_out, n);
    return;
  }
  if (r.service == CBE_SVC_UIM && bb_esim_in && bb_esim_resp &&
      (bb_esim.freeze || (!bb_esim.slots.known && r.len >= 7 && cbd_rd16(sdu + 3) == CBE_UIM_GET_CARD_STATUS))) {
    uint8_t *copy = bb_uim_count < BB_TX ? static_cast<uint8_t *>(IOMallocData(r.len)) : nullptr;
    if (copy) {
      memcpy(copy, sdu, r.len);
      const uint32_t slot = (bb_uim_head + bb_uim_count++) % BB_TX;
      bb_uim_held[slot] = { r.local_port, r.node, r.port, r.len, copy };
      bb_uim_cid[slot] = r.cid;
    } else {
      const uint32_t n = cbq_private_answer(sdu, r.service, r.cid, CBE_ERR_DEVICE_IN_USE, nullptr, 0, bb_out, sizeof bb_out);
      if (n) bb_push(r.channel ? CBH_QMUX2 : CBH_QMUX1, bb_out, n);
    }
    return;
  }
  const struct cbd_fix f = cbd_endpoint_fix(r.service, sdu, r.len, bb_fix, sizeof bb_fix);
  if (!f.len) {
    bb_queue_tx(r.local_port, r.node, r.port, sdu, r.len);
  } else {
    if (bb_endpoint_fixes++ < 8)
      printf("COACHZ baseband: data port: service %u message 0x%x endpoint %u/%u %s %u/%u\n", r.service, (uint32_t)(sdu[3] | sdu[4] << 8),
             f.before_type, f.before_interface, f.inserted ? "added as" : "now", CBD_EP_EMBEDDED, CBD_EP_INTERFACE);
    bb_queue_tx(r.local_port, r.node, r.port, bb_fix, f.len);
  }
  if (bb_ims_probe() && r.len >= 7u) {
    const uint32_t mid = cbd_rd16(sdu + 3);
    if (r.service == 0x03u && mid == 0xa1u) bb_ims_probe_event("nas-a1", sdu, r.len);
    else if (r.service == 0x09u && mid == 0x40u) bb_ims_probe_event("voice-40", sdu, r.len);
    else if (r.service == 0x0au && mid == 0x25u) bb_ims_probe_event("cat-25", sdu, r.len);
  }
}

// eSIM control runs on the same QRTR owner as the other QMI traffic (bb_lock held).
// Only explicit VINYL commands may switch slots or send an APDU. Idle discovery reads slot status.
static void bb_esim_step(void)
{
  if (!bb_esim_in || !bb_esim_resp) return;
  const struct cbq_server *server = cbq_server_find(&bb_qmux, CBE_SVC_UIM);
  if (!server) return;
  if (bb_esim_slot < 0) bb_esim_slot = cbq_client_alloc(&bb_qmux, CBQ_INTERNAL, CBE_SVC_UIM);
  if (bb_esim_slot < 0) return;
  const uint32_t now = bb_now_ms();
  while (!bb_esim.freeze && !bb_esim.route_failed && (bb_esim.slots.known || (bb_esim.step == CBE_S_IDLE && bb_esim.txn)) &&
         bb_uim_count && bb_tx_count < BB_TX) {
    bb_tx &held = bb_uim_held[bb_uim_head];
    const uint32_t index = held.local_port - CBQ_PORT_BASE;
    if (index < CBQ_MAX_CLIENTS && bb_qmux.client[index].used && bb_qmux.client[index].service == CBE_SVC_UIM &&
        bb_qmux.client[index].cid == bb_uim_cid[bb_uim_head]) {
      bb_txq[(bb_tx_head + bb_tx_count++) % BB_TX] = held;
    } else IOFreeData(held.data, held.len);
    bb_uim_head = (bb_uim_head + 1) % BB_TX; bb_uim_count--;
  }
  for (uint32_t i = 0; i < CBE_JOBS; i++) {
    struct cbe_job *j = &bb_esim.job[i];
    if (j->state && cbq_client_find(&bb_qmux, j->channel, CBE_SVC_VINYL, j->cid) < 0) {
      if ((int)i == bb_esim.active) cbe_finish(&bb_esim, 1, CBE_R_INACTIVE, 0, now);
      else j->state = CBE_J_FREE;
    }
  }
  if (bb_esim.session && bb_esim.active < 0 &&
      cbq_client_find(&bb_qmux, bb_esim.session_channel, CBE_SVC_VINYL, bb_esim.session_cid) < 0)
    cbe_session_release(&bb_esim, now, 2);
  cbe_tick(&bb_esim, now);
  if (bb_esim.req_len) {
    const uint32_t queued = bb_tx_count;
    bb_queue_tx(CBQ_PORT_BASE + (uint32_t)bb_esim_slot, server->node, server->port, bb_esim.req, bb_esim.req_len);
    if (bb_tx_count != queued) bb_esim.req_len = 0;
  }
  for (uint32_t i = 0; i < 4 && bb_esim.done_pending; i++) {
    const struct cbe_job *done = &bb_esim.done;
    if (cbq_client_find(&bb_qmux, done->channel, CBE_SVC_VINYL, done->cid) < 0) {
      if (bb_esim.step == CBE_S_IDLE) bb_esim.done_pending = 0;
      break;
    }
    const uint32_t channel = done->channel ? CBH_QMUX2 : CBH_QMUX1;
    if (bb_ch[channel].count == BB_QUEUE) break;
    const uint32_t chunk = bb_esim.done_chunk, pending = bb_esim.done_pending, queued = bb_ch[channel].count;
    uint8_t qmux = 0;
    const uint32_t n = cbe_output(&bb_esim, &qmux, bb_out, sizeof bb_out);
    if (!n) break;
    bb_push(channel, bb_out, n);
    if (bb_ch[channel].count == queued) { bb_esim.done_chunk = chunk; bb_esim.done_pending = (uint8_t)pending; break; }
  }
  struct cbe_event event;
  while (cbe_take_event(&bb_esim, &event))
    printf("COACHZ baseband: esim event %u a%u b%u c%u d%u e%u\n", event.kind, event.a, event.b, event.c, event.d, event.e);
}

// The activation indication (coachz_bb_data.h) to the BSP client that registered, with slot one's ICCID and IMSI once the card gave them.
static uint8_t bb_iccid[CBD_ICCID_BYTES], bb_imsi[CBD_IMSI_BYTES], bb_iccid_known, bb_imsi_known, bb_act_known, bb_act_channel, bb_act_cid;
static void bb_activation_indicate(void)
{
  if (!bb_act_known) return;
  uint8_t tlvs[CBD_ACTIVATION_SIM_TLVS];
  const uint32_t t = cbd_activation_state(tlvs, sizeof tlvs, bb_iccid_known ? bb_iccid : nullptr, bb_imsi_known ? bb_imsi : nullptr);
  const uint32_t n = cbq_private_indication(CBD_BSP_ACTIVATION_REGISTER, CBD_SVC_BSP, bb_act_cid, tlvs, t, bb_out, sizeof bb_out);
  if (n) bb_push(bb_act_channel, bb_out, n);
}

// CommCenter's own EF_ICCID and EF_IMSI reads: the request's client, transaction and file, so the answer can be recognized. Never logged.
struct bb_sim_read { uint8_t used, channel, cid; uint16_t txn, file; };
static bb_sim_read bb_sim_reads[8];
static uint32_t bb_sim_next;
static void bb_sim_request(const struct cbq_result &r, const uint8_t *sdu)
{
  if (r.service != CBD_SVC_UIM) return;
  const uint32_t file = cbd_uim_read_file(sdu, r.len);
  if (file != CBD_EF_ICCID && file != CBD_EF_IMSI) return;
  bb_sim_reads[bb_sim_next++ % 8] = { 1, r.channel, r.cid, cbd_rd16(sdu + 1), (uint16_t)file };
}
static void bb_sim_answer(const struct cbq_result &r, const uint8_t *sdu, uint32_t n)
{
  if (r.service != CBD_SVC_UIM || n < 7u || sdu[0] != 2u || cbd_rd16(sdu + 3) != CBD_UIM_READ_TRANSPARENT) return;
  for (auto &q : bb_sim_reads) {
    if (!q.used || q.channel != r.channel || q.cid != r.cid || q.txn != cbd_rd16(sdu + 1)) continue;
    q.used = 0;
    uint8_t ef[16], value[CBD_ICCID_BYTES];
    const uint32_t len = cbd_uim_read_content(sdu, n, ef, sizeof ef);
    const bool iccid = q.file == CBD_EF_ICCID;
    if (iccid ? !cbd_iccid_from_ef(ef, len, value) : !cbd_imsi_from_ef(ef, len, value)) return;
    uint8_t *have = iccid ? bb_iccid : bb_imsi, *known = iccid ? &bb_iccid_known : &bb_imsi_known;
    const uint32_t size = iccid ? CBD_ICCID_BYTES : CBD_IMSI_BYTES;
    if (*known && !memcmp(value, have, size)) return;
    memcpy(have, value, size); *known = 1;
    printf("COACHZ baseband: slot one %s read from the card, activation indication %s\n", iccid ? "ICCID" : "IMSI",
           bb_act_known ? "sent again with it" : "waits for registration");
    bb_activation_indicate();
    return;
  }
}

// ------------------------------------------------------------------ GNSS (fuxi,modem-gnss, bb_lock held)
static void bb_gnss_queue_pending(const uint8_t *sdu, uint32_t len)
{
  if (!len || len > sizeof bb_gnss_pending[0].sdu) return;
  /* Coalesce queued INJECT_POSITION (0x0039) so unlock flush does not burst same-ms err=1 */
  if (fgl_sdu_ok(sdu, len) && fgl_rd16(sdu + 3) == FGL_LOC_INJECT_POSITION) {
    for (uint32_t i = 0u; i < bb_gnss_pending_count; i++) {
      struct fgp_loc_msg *m = &bb_gnss_pending[(bb_gnss_pending_head + i) % BB_GNSS_PENDING];
      if (m->len >= FGL_SDU_HDR && fgl_rd16(m->sdu + 3) == FGL_LOC_INJECT_POSITION) {
        memcpy(m->sdu, sdu, len);
        m->len = len;
        return;
      }
    }
  }
  if (bb_gnss_pending_count >= BB_GNSS_PENDING) return;
  struct fgp_loc_msg *m = &bb_gnss_pending[(bb_gnss_pending_head + bb_gnss_pending_count++) % BB_GNSS_PENDING];
  memcpy(m->sdu, sdu, len);
  m->len = len;
}

static bool bb_gnss_try_tx_loc(const uint8_t *sdu, uint32_t len)
{
  const struct cbq_server *server = cbq_server_find(&bb_qmux, FGL_SVC_LOC);
  if (!server || bb_tx_count >= BB_TX) return false;
  if (bb_loc_slot < 0) bb_loc_slot = cbq_client_alloc(&bb_qmux, CBQ_INTERNAL, FGL_SVC_LOC);
  if (bb_loc_slot < 0) return false;
  const uint32_t before = bb_tx_count;
  bb_queue_tx(CBQ_PORT_BASE + (uint32_t)bb_loc_slot, server->node, server->port, sdu, len);
  if (bb_tx_count > before) {
    /* Only track post-unlock REG_EVENTS so pre-unlock mask=0 (MHAL session) never triggers fallback */
    if (fgl_unlock_is_done(&bb_gnss_unlock) && fgl_sdu_ok(sdu, len) && fgl_rd16(sdu + 3) == FGL_LOC_REG_EVENTS) {
      uint32_t mlen = 0;
      const uint8_t *mv = fgl_find_tlv(sdu, len, 0x01u, &mlen);
      bb_gnss_reg_txn = fgl_rd16(sdu + 1);
      bb_gnss_reg_mask = (mv && mlen >= 8u) ? fgl_rd64(mv) : 0ULL;
    }
    return true;
  }
  return false;
}

static void bb_gnss_send_loc(const uint8_t *sdu, uint32_t len)
{
  if (!len) return;
  if (fgl_unlock_is_done(&bb_gnss_unlock) && bb_gnss_pending_count == 0u && bb_gnss_try_tx_loc(sdu, len)) return;
  bb_gnss_queue_pending(sdu, len);
}

static void bb_gnss_flush_pending(void)
{
  while (bb_gnss_pending_count > 0u) {
    struct fgp_loc_msg *m = &bb_gnss_pending[bb_gnss_pending_head];
    if (!bb_gnss_try_tx_loc(m->sdu, m->len)) break;
    bb_gnss_pending_head = (bb_gnss_pending_head + 1u) % BB_GNSS_PENDING;
    bb_gnss_pending_count--;
  }
}

static void bb_gnss_try_inject_utc_now(uint32_t now)
{
  if (!bb_gnss.pending_time_inject) return;
  if (bb_gnss_last_utc_inject_ms && (uint32_t)(now - bb_gnss_last_utc_inject_ms) < 5000u) return;
  clock_sec_t sec = 0;
  clock_usec_t usec = 0;
  clock_get_calendar_microtime(&sec, &usec);
  if ((uint64_t)sec < 1700000000ULL) return;
  const uint64_t utc_ms = (uint64_t)sec * 1000ULL + (uint64_t)usec / 1000ULL;
  fgp_set_utc_hint(&bb_gnss, utc_ms);
  uint8_t tsdu[64];
  const uint16_t txn = fgp_next_txn(&bb_gnss);
  const uint32_t tlen = fgl_build_inject_utc_time(tsdu, sizeof tsdu, txn, utc_ms, 2000u, FGL_TIME_SRC_AP);
  if (tlen) {
    bb_gnss_send_loc(tsdu, tlen);
    fgl_tracker_note_tx(&bb_gnss.two_stage, FGL_LOC_INJECT_UTC_TIME, txn);
    bb_gnss.pending_time_inject = 0u;
    bb_gnss_last_utc_inject_ms = now;
    if (bb_gnss_diag() && bb_gnss_diag_logs < 64u) {
      bb_gnss_diag_logs++;
      printf("COACHZ gnss-diag: injected utc on 0x0028 req sec=%llu t=%u\n", (unsigned long long)sec, now);
    }
  }
}

static void bb_gnss_mock_poll(uint32_t now)
{
  if (!bb_gnss_mock() || !bb_gnss_bridge()) return;
  clock_sec_t sec = 0;
  clock_usec_t usec = 0;
  clock_get_calendar_microtime(&sec, &usec);
  const uint64_t utc_ms = ((uint64_t)sec >= 1700000000ULL) ?
      ((uint64_t)sec * 1000ULL + (uint64_t)usec / 1000ULL) : 0ULL;
  if (utc_ms) fgp_set_utc_hint(&bb_gnss, utc_ms);
  if (fgp_mock_step(&bb_gnss, &bb_gnss_mock_cfg, now, utc_ms, &bb_gnss_act, &bb_gnss_scratch)) {
    const uint32_t ch = bb_gnss.pds_channel ? CBH_QMUX2 : CBH_QMUX1;
    for (uint32_t i = 0; i < bb_gnss_act.host_count; i++)
      bb_push(ch, bb_gnss_act.host[i].frame, bb_gnss_act.host[i].len);
    if (bb_gnss_mock_logs < 64u) {
      bb_gnss_mock_logs++;
      printf("COACHZ gnss-mock: injected mock fix seq=%u hacc=5m t=%u\n", bb_gnss.mock_fixes, now);
    }
  }
}

static void bb_gnss_probe_poll(void)
{
  if (!bb_gnss_probe() && !bb_gnss_bridge()) return;
  const uint32_t now = bb_now_ms();
  bb_gnss_mock_poll(now);
  if (!cbq_server_find(&bb_qmux, FGL_SVC_LOC)) return;
  uint8_t sdu[128];
  /* Align QMI_LOC startup with Xiaomi 13 libloc_api_v02 LocApiV02::open():
   * 0x0021(mask=0, MHAL) -> 0x00BA -> 0x003B (bounded 0x43 backoff) -> optional 0x003A -> flush. */
  if (!fgl_unlock_is_done(&bb_gnss_unlock)) {
    struct fgl_unlock_event uev;
    fgl_unlock_tick(&bb_gnss_unlock, now, &uev);
    if (uev.kind == FGL_ULK_EV_TIMEOUT) {
      const char *name = (uev.msg == FGL_LOC_REG_EVENTS) ? "REG_EVENTS mask=0x0" :
                         (uev.msg == FGL_LOC_REG_MASTER_CLIENT) ? "REG_MASTER ind" :
                         (uev.msg == FGL_LOC_GET_ENGINE_LOCK) ? "GET_ENGINE_LOCK ind" : "SET_ENGINE_LOCK ind";
      printf("COACHZ gnss: QMI_LOC %s timeout t=%u\n", name, now);
    }
    while (fgl_unlock_is_send_step(&bb_gnss_unlock) && bb_tx_count < BB_TX) {
      const uint16_t txn = fgp_next_txn(&bb_gnss);
      uint16_t msg_id = 0u;
      const uint32_t len = fgl_unlock_prepare_tx(&bb_gnss_unlock, txn, sdu, sizeof sdu, &msg_id);
      if (!len || !bb_gnss_try_tx_loc(sdu, len)) break;
      fgl_unlock_commit_tx(&bb_gnss_unlock, txn, now);
      if (fgl_is_two_stage_msg(msg_id)) fgl_tracker_note_tx(&bb_gnss.two_stage, msg_id, txn);
    }
  }
  if (!fgl_unlock_is_done(&bb_gnss_unlock)) return;
  bb_gnss_try_inject_utc_now(now);
  bb_gnss_flush_pending();
  if (!bb_gnss_probe()) return;
  if (bb_gnss.pds_client_active) {
    if (bb_gnss_probe_step < 5u) bb_gnss_probe_step = 5u;
    return;
  }
  while (bb_gnss_probe_step < 4u && bb_tx_count < BB_TX) {
    uint32_t len = 0;
    const uint16_t txn = fgp_next_txn(&bb_gnss);
    if (bb_gnss_probe_step == 0u) {
      len = fgl_build_reg_events(sdu, sizeof sdu, txn, FGL_EVT_BRIDGE_MASK);
      if (!len || !bb_gnss_try_tx_loc(sdu, len)) break;
      bb_gnss_probe_reg_txn = txn;
      bb_gnss_probe_step = 1u;
    } else if (bb_gnss_probe_step == 1u) {
      len = fgl_build_set_op_mode(sdu, sizeof sdu, txn, FGL_OP_MODE_STANDALONE);
      if (!len || !bb_gnss_try_tx_loc(sdu, len)) break;
      fgl_tracker_note_tx(&bb_gnss.two_stage, FGL_LOC_SET_OP_MODE, txn);
      bb_gnss_probe_op_txn = txn;
      bb_gnss_probe_step = 2u;
    } else if (bb_gnss_probe_step == 2u) {
      clock_sec_t sec = 0;
      clock_usec_t usec = 0;
      clock_get_calendar_microtime(&sec, &usec);
      if ((uint64_t)sec >= 1700000000ULL) {
        const uint64_t utc_ms = (uint64_t)sec * 1000ULL + (uint64_t)usec / 1000ULL;
        len = fgl_build_inject_utc_time(sdu, sizeof sdu, txn, utc_ms, 2000u, FGL_TIME_SRC_AP);
        if (!len || !bb_gnss_try_tx_loc(sdu, len)) break;
        fgl_tracker_note_tx(&bb_gnss.two_stage, FGL_LOC_INJECT_UTC_TIME, txn);
        bb_gnss_probe_utc_txn = txn;
        printf("COACHZ gnss-probe: inject utc sec=%llu t=%u\n", (unsigned long long)sec, bb_now_ms());
      }
      bb_gnss_probe_step = 3u;
    } else if (bb_gnss_probe_step == 3u) {
      len = fgl_build_start(sdu, sizeof sdu, txn, FGL_PROBE_SID, FGL_RECURRENCE_PERIODIC, FGL_INTERMEDIATE_ENABLE, 1000u);
      if (!len || !bb_gnss_try_tx_loc(sdu, len)) break;
      bb_gnss_probe_start_txn = txn;
      bb_gnss.probe_active = 1u;
      bb_gnss_probe_step = 4u;
    }
  }
}

static void bb_gnss_loc_answer(const uint8_t *sdu, uint32_t size)
{
  if (!fgl_sdu_ok(sdu, size)) return;
  const uint32_t msg = fgl_rd16(sdu + 3);
  const uint32_t txn = fgl_rd16(sdu + 1);
  const uint32_t now = bb_now_ms();
  const uint8_t prev_unlock = bb_gnss_unlock.step;
  struct fgl_unlock_event uev;
  fgl_unlock_handle_rx(&bb_gnss_unlock, sdu, size, now, &uev);
  if (uev.kind == FGL_ULK_EV_ABSORBED_ERR3) {
    return;
  } else if (uev.kind == FGL_ULK_EV_PRE_REG_RESP) {
    printf("COACHZ gnss: QMI_LOC REG_EVENTS mask=0x0 err=%u t=%u\n", uev.err, now);
  } else if (uev.kind == FGL_ULK_EV_MASTER_RESP) {
    printf("COACHZ gnss: QMI_LOC REG_MASTER err=%u t=%u\n", uev.err, now);
  } else if (uev.kind == FGL_ULK_EV_MASTER_IND) {
    printf("COACHZ gnss: QMI_LOC REG_MASTER ind=%u t=%u\n", uev.ind_status, now);
  } else if (uev.kind == FGL_ULK_EV_GET_LOCK_RESP) {
    printf("COACHZ gnss: QMI_LOC GET_ENGINE_LOCK err=%u t=%u\n", uev.err, now);
  } else if (uev.kind == FGL_ULK_EV_GET_LOCK_RETRY) {
    printf("COACHZ gnss: QMI_LOC GET_ENGINE_LOCK err=%u retry=%u/%u t=%u\n",
           uev.err, uev.retry, FGL_UNLOCK_MAX_RETRIES, now);
  } else if (uev.kind == FGL_ULK_EV_GET_LOCK_EXHAUSTED) {
    printf("COACHZ gnss: QMI_LOC GET_ENGINE_LOCK err=%u exhausted=%u t=%u\n",
           uev.err, uev.retry, now);
  } else if (uev.kind == FGL_ULK_EV_GET_LOCK_IND) {
    printf("COACHZ gnss: QMI_LOC GET_ENGINE_LOCK ind=%u lock=%u t=%u\n",
           uev.ind_status, uev.lock_type, now);
  } else if (uev.kind == FGL_ULK_EV_SET_LOCK_RESP) {
    printf("COACHZ gnss: QMI_LOC SET_ENGINE_LOCK err=%u t=%u\n", uev.err, now);
  } else if (uev.kind == FGL_ULK_EV_SET_LOCK_RETRY) {
    printf("COACHZ gnss: QMI_LOC SET_ENGINE_LOCK err=%u retry=%u/%u t=%u\n",
           uev.err, uev.retry, FGL_UNLOCK_MAX_RETRIES, now);
  } else if (uev.kind == FGL_ULK_EV_SET_LOCK_EXHAUSTED) {
    printf("COACHZ gnss: QMI_LOC SET_ENGINE_LOCK err=%u exhausted=%u t=%u\n",
           uev.err, uev.retry, now);
  } else if (uev.kind == FGL_ULK_EV_SET_LOCK_IND) {
    printf("COACHZ gnss: QMI_LOC SET_ENGINE_LOCK ind=%u t=%u\n", uev.ind_status, now);
  } else if (sdu[0] == CBQ_QMI_RESPONSE) {
    const uint32_t err = fgl_result_error(sdu, size);
    if (msg == FGL_LOC_REG_EVENTS && txn == bb_gnss_reg_txn) {
      printf("COACHZ gnss: QMI_LOC REG_EVENTS mask=0x%llx err=%u fb=%u t=%u\n",
             (unsigned long long)bb_gnss_reg_mask, err, bb_gnss_reg_fallback, bb_now_ms());
      if (err == 0u) {
        bb_gnss_reg_fallback = 0u;
      } else if (bb_gnss_reg_fallback < 2u) {
        uint64_t next_mask = 0ULL;
        if (bb_gnss_reg_fallback == 0u && (bb_gnss_reg_mask & ~FGL_EVT_DEFAULT_MASK) != 0ULL) {
          next_mask = FGL_EVT_DEFAULT_MASK;
          bb_gnss_reg_fallback = 1u;
        } else if ((bb_gnss_reg_mask & ~FGL_EVT_MIN_MASK) != 0ULL) {
          next_mask = FGL_EVT_MIN_MASK;
          bb_gnss_reg_fallback = 2u;
        } else {
          bb_gnss_reg_fallback = 2u;
        }
        if (next_mask != 0ULL) {
          uint8_t rsdu[64];
          const uint32_t rlen = fgl_build_reg_events(rsdu, sizeof rsdu, fgp_next_txn(&bb_gnss), next_mask);
          if (rlen) {
            bb_gnss.loc_event_mask = next_mask;
            printf("COACHZ gnss: QMI_LOC REG_EVENTS fallback step=%u mask=0x%llx t=%u\n",
                   bb_gnss_reg_fallback, (unsigned long long)next_mask, bb_now_ms());
            bb_gnss_send_loc(rsdu, rlen);
          }
        }
      }
    } else if (bb_gnss_probe()) {
      if (msg == FGL_LOC_REG_EVENTS && txn == bb_gnss_probe_reg_txn)
        printf("COACHZ gnss-probe: QMI_LOC REG_EVENTS err=%u t=%u\n", err, bb_now_ms());
      else if (msg == FGL_LOC_SET_OP_MODE && txn == bb_gnss_probe_op_txn)
        printf("COACHZ gnss-probe: QMI_LOC SET_OP_MODE err=%u t=%u\n", err, bb_now_ms());
      else if (msg == FGL_LOC_INJECT_UTC_TIME && txn == bb_gnss_probe_utc_txn)
        printf("COACHZ gnss-probe: QMI_LOC INJECT_UTC_TIME err=%u t=%u\n", err, bb_now_ms());
      else if (msg == FGL_LOC_START && txn == bb_gnss_probe_start_txn)
        printf("COACHZ gnss-probe: QMI_LOC START err=%u t=%u\n", err, bb_now_ms());
    }
  }

  if (bb_gnss_probe() && sdu[0] == CBQ_QMI_INDICATION) {
    if (msg == FGL_LOC_EVENT_GNSS_SV_INFO) {
      if (fgl_parse_sv_info(sdu, size, &bb_gnss_scratch.sv)) {
        uint32_t cn0_max = 0u;
        for (uint32_t i = 0; i < bb_gnss_scratch.sv.count; i++) {
          const uint32_t c = fgl_f32_scale_u32(bb_gnss_scratch.sv.sv[i].snr_bits, 1u, 1u, 99u);
          if (c > cn0_max) cn0_max = c;
        }
        bb_gnss_last_sv = bb_gnss_scratch.sv.count;
        bb_gnss_last_cn0_max = cn0_max;
      }
    } else if (msg == FGL_LOC_EVENT_POSITION_REPORT) {
      if (fgl_parse_pos_report(sdu, size, &bb_gnss_scratch.pos) && bb_gnss_probe_logs < 120u) {
        const struct fgl_pos_report *p = &bb_gnss_scratch.pos;
        const uint32_t has_ll = ((p->valid_bits & (FGL_POS_V_LAT | FGL_POS_V_LON)) == (FGL_POS_V_LAT | FGL_POS_V_LON)) ? 1u : 0u;
        const uint32_t valid = (p->session_status == FGL_SESS_SUCCESS && has_ll) ? 1u : 0u;
        const uint32_t hacc_m = (p->valid_bits & FGL_POS_V_HUNC) ? fgl_f32_scale_u32(p->hunc_bits, 1u, 1u, 99999u) : 0u;
        bb_gnss_probe_logs++;
        printf("COACHZ gnss-probe: loc fix valid=%u status=%u sv=%u cn0_max=%u hacc=%um t=%u\n",
               valid, p->session_status, bb_gnss_last_sv, bb_gnss_last_cn0_max, hacc_m, bb_now_ms());
      }
    } else if (msg == FGL_LOC_EVENT_NMEA && (bb_gnss_diag() || bb_gnss_nmea_logs < 8u)) {
      char nmea[64];
      uint32_t nlen = 0;
      if (fgl_parse_nmea(sdu, size, nmea, sizeof nmea, &nlen) && bb_gnss_nmea_logs < 64u) {
        char tag[12];
        uint32_t k = 0;
        while (k < 10u && nmea[k] && nmea[k] != ',' && nmea[k] != '*') { tag[k] = nmea[k]; k++; }
        tag[k] = '\0';
        bb_gnss_nmea_logs++;
        printf("COACHZ gnss-probe: nmea tag=%s len=%u t=%u\n", tag[0] ? tag : "?", nlen, bb_now_ms());
      }
    }
  }
  if (bb_gnss_bridge()) {
    fgp_note_now_ms(&bb_gnss, now);
    fgp_handle_loc_rx(&bb_gnss, sdu, size, &bb_gnss_act, &bb_gnss_scratch);
    const uint32_t ch = bb_gnss.pds_channel ? CBH_QMUX2 : CBH_QMUX1;
    for (uint32_t i = 0; i < bb_gnss_act.host_count; i++)
      bb_push(ch, bb_gnss_act.host[i].frame, bb_gnss_act.host[i].len);
    for (uint32_t i = 0; i < bb_gnss_act.loc_count; i++)
      bb_gnss_send_loc(bb_gnss_act.loc[i].sdu, bb_gnss_act.loc[i].len);
    if (bb_gnss_act.time_inject_requested || bb_gnss.pending_time_inject) {
      if (bb_gnss_diag() && bb_gnss_act.time_inject_requested && bb_gnss_diag_logs < 64u) {
        bb_gnss_diag_logs++;
        printf("COACHZ gnss-diag: loc inject_time_req count=%u t=%u\n", bb_gnss.inject_time_reqs, now);
      }
      bb_gnss_try_inject_utc_now(now);
    }
    if (bb_gnss_diag() && bb_gnss_act.orbits_inject_requested && bb_gnss_diag_logs < 64u) {
      bb_gnss_diag_logs++;
      printf("COACHZ gnss-diag: loc inject_orbits_req count=%u max_file=%u max_part=%u t=%u\n",
             bb_gnss.inject_orbits_reqs, bb_gnss.xtra_max_file_bytes, bb_gnss.xtra_max_part_bytes, now);
    }
    if (bb_gnss_diag() && bb_gnss_act.unknown && bb_gnss_diag_logs < 64u) {
      bb_gnss_diag_logs++;
      printf("COACHZ gnss-diag: unhandled loc type=%u msg=0x%x t=%u\n", sdu[0], msg, bb_now_ms());
    }
  }
  if (bb_gnss_unlock.step != prev_unlock &&
      (fgl_unlock_is_send_step(&bb_gnss_unlock) || fgl_unlock_is_done(&bb_gnss_unlock)))
    bb_gnss_probe_poll();
}

// A private request the bridge answers itself: 1 when it did.
static int bb_private_answer(uint32_t channel, const struct cbq_result &r, const uint8_t *sdu)
{
  const uint32_t message = (uint32_t)(sdu[3] | sdu[4] << 8);
  if (r.service == FGP_SVC_PDS && bb_gnss_bridge()) {
    fgp_handle_pds_request(&bb_gnss, r.channel, r.cid, sdu, r.len, &bb_gnss_act);
    if (bb_gnss.pds_client_active && bb_gnss_probe_step < 5u) bb_gnss_probe_step = 5u;
    for (uint32_t i = 0; i < bb_gnss_act.host_count; i++)
      bb_push(channel, bb_gnss_act.host[i].frame, bb_gnss_act.host[i].len);
    for (uint32_t i = 0; i < bb_gnss_act.loc_count; i++)
      bb_gnss_send_loc(bb_gnss_act.loc[i].sdu, bb_gnss_act.loc[i].len);
    if (bb_gnss_diag() && bb_gnss_diag_logs < 64u) {
      char ids[64];
      bb_tlv_ids(sdu, r.len, ids, sizeof ids);
      bb_gnss_diag_logs++;
      printf("COACHZ gnss-diag: pds msg=0x%x tlv=[%s] unk=%u loc_out=%u track=%u t=%u\n",
             message, ids, bb_gnss_act.unknown, bb_gnss_act.loc_count, bb_gnss.tracking, bb_now_ms());
    }
    return 1;
  }
  if (r.service == CBE_SVC_VINYL) {
    const uint32_t n = bb_esim_in && bb_esim_resp ?
      cbe_host(&bb_esim, r.channel, r.cid, sdu, r.len, bb_out, sizeof bb_out, bb_now_ms()) :
      cbq_private_answer(sdu, r.service, r.cid, CBE_ERR_NO_MEMORY, nullptr, 0, bb_out, sizeof bb_out);
    if (!n) return 0;
    if (n != CBE_DEFERRED) bb_push(channel, bb_out, n);
    return 1;
  }
  // Carrier bundle files (coachz_bb_data.h): accepted and dropped, each segment acknowledged with the next segment number.
  if (r.service == CBD_SVC_BSP && message == CBD_BSP_SEND_FILE) {
    struct cbd_file_segment f;
    if (!cbd_bsp_file_segment(sdu, r.len, &f)) return 0;
    uint8_t tlvs[8];
    const uint32_t t = cbd_bsp_file_answer(&f, tlvs, sizeof tlvs);
    const uint32_t n = cbq_private_answer(sdu, r.service, r.cid, 0, tlvs, t, bb_out, sizeof bb_out);
    if (n) bb_push(channel, bb_out, n);
    if (f.segment + 1u == f.count) {
      // The file is complete: report it received as an Apple modem does (FileTransferStatus), or CommCenter times out after 22 s.
      uint8_t done[12];
      const uint32_t d = cbd_bsp_file_done(&f, done, sizeof done);
      const uint32_t m = cbq_private_indication(CBD_BSP_FILE_TRANSFER_STATUS, r.service, r.cid, done, d, bb_out, sizeof bb_out);
      if (m) bb_push(channel, bb_out, m);
      printf("COACHZ baseband: carrier file type %u taken (%u segments, last %u bytes), dropped, transfer status sent\n", f.type, f.count, f.bytes);
    }
    return 1;
  }
  if (r.service == CBD_SVC_BSP && message == CBD_BSP_CHIP_INFO) {
    uint8_t tlvs[12];
    const uint32_t t = cbd_bsp_chip_info(CBS_CHIP_ID, CBS_SERIAL, tlvs, sizeof tlvs);
    const uint32_t n = cbq_private_answer(sdu, r.service, r.cid, 0, tlvs, t, bb_out, sizeof bb_out);
    if (n) bb_push(channel, bb_out, n);
    return 1;
  }
  if (r.service == CBD_SVC_BSP && message == CBD_BSP_SET_CONFIG) {
    const uint32_t n = cbq_private_answer(sdu, r.service, r.cid, 0, nullptr, 0, bb_out, sizeof bb_out);
    if (n) bb_push(channel, bb_out, n);
    printf("COACHZ baseband: baseband config (SetConfig) taken\n");
    return 1;
  }
  if (r.service == CBD_SVC_BSP && message == CBD_BSP_ACTIVATION_REGISTER) {
    const uint32_t n = cbq_private_answer(sdu, r.service, r.cid, 0, 0, 0, bb_out, sizeof bb_out);
    if (n) bb_push(channel, bb_out, n);
    bb_act_known = 1; bb_act_channel = (uint8_t)channel; bb_act_cid = r.cid;
    bb_activation_indicate();
    if (bb_activations++ < 4)
      printf("COACHZ baseband: activation register answered, state activated, slot one ICCID %s, IMSI %s\n", bb_iccid_known ? "included" : "not read yet",
             bb_imsi_known ? "included" : "not read yet");
    return 1;
  }
  return 0;
}

// One write from ABM on a channel, after its bytes were copied in.
static void bb_host_write(uint32_t channel, const uint8_t *data, uint32_t len)
{
  bb_channel *c = &bb_ch[channel];
  c->frames_in++;
  c->bytes_in += len;
  if (channel == CBH_SAHARA) {
    const enum cbs_state before = bb_sahara.state;
    for (uint32_t at = 0; at < len && bb_sahara.state != CBS_FAILED;) {
      struct cbs_out o;
      const uint32_t used = cbs_receive(&bb_sahara, data + at, len - at, &o);
      for (uint32_t i = 0; i < o.count; i++) bb_push(CBH_SAHARA, o.data[i], o.len[i]);
      if (!used) break;
      at += used;
    }
    if (bb_sahara.state != before && bb_sahara.state == CBS_COMMAND)
      printf("COACHZ baseband: sahara command mode\n");
    if (bb_sahara.state != before && (bb_sahara.state == CBS_FINISHED || bb_sahara.state == CBS_FAILED || bb_sahara.state == CBS_WAIT_DONE))
      printf("COACHZ baseband: sahara %s, image %u of %u (id 0x%x), host mode %u status %u, last command %u, %u requests, %llu bytes\n",
             bb_sahara.state == CBS_FINISHED ? "finished" : bb_sahara.state == CBS_FAILED ? "failed" : "image read",
             bb_sahara.image_index + 1, bb_sahara.image_count, bb_sahara.images[bb_sahara.image_index], bb_sahara.host_mode,
             bb_sahara.host_status, bb_sahara.last_command, bb_sahara.requests, bb_sahara.bytes);
    return;
  }
  if (channel != CBH_QMUX1 && channel != CBH_QMUX2) return;   // counted above, nothing behind it yet
  const uint32_t qmux = channel == CBH_QMUX1 ? 0 : 1;
  bb_trace_frame("host", qmux + 1u, data, len);
  const struct cbq_result r = cbq_from_host(&bb_qmux, qmux, data, len, bb_out, sizeof bb_out);
  switch (r.kind) {
  case CBQ_TO_HOST:
    bb_push(channel, bb_out, r.len);
    // 2026-11-22: a GET_CLIENT_ID refused because every slot is in use was silent; name the service it was for.
    if (data[4] == 0u && len >= CBQ_HDR + CBQ_CTL_HDR + 4u && cbq_rd16(data + CBQ_HDR + 2) == CBQ_CTL_GET_CLIENT_ID &&
        r.len >= CBQ_HDR + CBQ_CTL_HDR + 7u && cbq_rd16(bb_out + CBQ_HDR + CBQ_CTL_HDR + 5) != 0u && bb_ctl_refused++ < 32u) {
      uint32_t used = 0;
      for (uint32_t i = 0; i < CBQ_MAX_CLIENTS; i++) used += bb_qmux.client[i].used;
      printf("COACHZ baseband: GET_CLIENT_ID for service 0x%x on qmux%u refused, error %u (%u slots in use, limit %u)\n",
             data[CBQ_HDR + CBQ_CTL_HDR] == 0x01u ? data[CBQ_HDR + CBQ_CTL_HDR + 3] : 0u, qmux + 1u,
             cbq_rd16(bb_out + CBQ_HDR + CBQ_CTL_HDR + 5), used, bb_qmux.client_limit);
    }
    // The first WDS client ABM takes: set the data port up as ModemManager does before its first bind.
    if (r.service == CBD_SVC_WDS && r.local_port && cbq_rd16(data + CBQ_HDR + 2) == CBQ_CTL_GET_CLIENT_ID) bb_data_start();
    if (bb_gnss_bridge() && bb_gnss.pds_client_active &&
        cbq_client_find(&bb_qmux, bb_gnss.pds_channel, FGP_SVC_PDS, bb_gnss.pds_cid) < 0) {
      fgp_set_pds_client(&bb_gnss, 0u, 0u, 0u);
      if (bb_gnss.tracking == FGP_TRACK_ACTIVE) {
        bb_gnss.tracking = FGP_TRACK_INACTIVE;
        uint8_t stop_sdu[32];
        const uint32_t sn = fgl_build_stop(stop_sdu, sizeof stop_sdu, fgp_next_txn(&bb_gnss), bb_gnss.sid ? bb_gnss.sid : 1u);
        bb_gnss_send_loc(stop_sdu, sn);
      }
    }
    if (bb_qmux.ctl_other != bb_ctl_logged) {
      bb_ctl_logged = bb_qmux.ctl_other;
      printf("COACHZ baseband: CTL message 0x%x accepted without action\n", (uint32_t)(data[CBQ_HDR + 2] | data[CBQ_HDR + 3] << 8));
    }
    break;
  case CBQ_TO_MODEM:
    // Apple's report configuration this modem's firmware does not know (coachz_bb_data.h): accepted here, not forwarded.
    if (cbd_vendor_reporting(r.service, data + CBQ_HDR, r.len)) {
      const uint32_t n = cbq_private_answer(data + CBQ_HDR, r.service, r.cid, 0, nullptr, 0, bb_out, sizeof bb_out);
      if (n) bb_push(channel, bb_out, n);
      break;
    }
    bb_to_modem(r, data + CBQ_HDR);
    break;
  case CBQ_PRIVATE: {
    const uint8_t *sdu = data + CBQ_HDR;
    if (bb_private_answer(channel, r, sdu)) break;
    bb_private_note(r.service, (uint32_t)(sdu[3] | sdu[4] << 8), sdu, r.len);
    const uint32_t n = cbq_private_unsupported(sdu, r.service, r.cid, bb_out, sizeof bb_out);
    if (n) bb_push(channel, bb_out, n);
    break;
  }
  case CBQ_DROP:
    break;
  }
}

// ------------------------------------------------------------------ asynchronous completions
// IOUserClient::sendAsyncResult64 is a protected static; this never-instantiated subclass only forwards to it.
class CoachzBasebandAsync : public IOUserClient {
public:
  static IOReturn send(OSAsyncReference64 reference, IOReturn result, io_user_reference_t *args, UInt32 count)
  {
    return sendAsyncResult64(reference, result, args, count);
  }
};

static void bb_finish_read(bb_read *read, IOReturn result, const uint8_t *data, uint32_t len)
{
  io_user_reference_t args[1] = { 0 };
  if (data && read->memory) {
    const uint32_t n = len < read->len ? len : read->len;
    args[0] = read->memory->writeBytes(0, data, n);
    if (len > read->len) result = kIOReturnOverrun;
  }
  if (read->memory) {
    read->memory->complete();
    read->memory->release();
    read->memory = nullptr;
  }
  CoachzBasebandAsync::send(read->reference, result, args, 1);
}

static void bb_complete(thread_call_param_t, thread_call_param_t)
{
  for (;;) {
    bb_read read;
    bb_frame frame = { 0, nullptr };
    bool found = false;
    IOLockLock(bb_lock);
    for (uint32_t c = 0; c < CBH_PUBLISHED && !found; c++) {
      bb_channel *ch = &bb_ch[c];
      if (!ch->reads_waiting || !ch->count) continue;
      read = ch->reads[0];
      memmove(&ch->reads[0], &ch->reads[1], (--ch->reads_waiting) * sizeof ch->reads[0]);
      frame = bb_pop(c);
      found = true;
    }
    IOLockUnlock(bb_lock);
    if (!found) return;
    bb_finish_read(&read, kIOReturnSuccess, frame.data, frame.len);
    IOFreeData(frame.data, frame.len);
  }
}

// Every read a client has waiting ends with `result` (close, abort).
static void bb_abort_reads(uint32_t channel, void *owner, IOReturn result)
{
  bb_read ended[BB_READS];
  uint32_t n = 0;
  IOLockLock(bb_lock);
  bb_channel *ch = &bb_ch[channel];
  for (uint32_t i = 0; i < ch->reads_waiting;) {
    if (!owner || ch->reads[i].owner == owner) {
      ended[n++] = ch->reads[i];
      memmove(&ch->reads[i], &ch->reads[i + 1], (--ch->reads_waiting - i) * sizeof ch->reads[0]);
    } else {
      i++;
    }
  }
  IOLockWakeup(bb_lock, ch, false);
  IOLockUnlock(bb_lock);
  for (uint32_t i = 0; i < n; i++) bb_finish_read(&ended[i], result, nullptr, 0);
}

// ------------------------------------------------------------------ user memory
static uint8_t *bb_copy_in(task_t task, mach_vm_address_t address, uint32_t len)
{
  if (!len || len > BB_FRAME_MAX) return nullptr;
  IOMemoryDescriptor *memory = IOMemoryDescriptor::withAddressRange(address, len, kIODirectionOut, task);
  uint8_t *copy = static_cast<uint8_t *>(IOMallocData(len));
  bool ok = memory && copy && memory->prepare() == kIOReturnSuccess;
  if (ok) {
    ok = memory->readBytes(0, copy, len) == len;
    memory->complete();
  }
  OSSafeReleaseNULL(memory);
  if (!ok && copy) {
    IOFreeData(copy, len);
    copy = nullptr;
  }
  return copy;
}

static bool bb_async(IOExternalMethodArguments *args) { return args->asyncWakePort != MACH_PORT_NULL && args->asyncReference; }

static void bb_reply(IOExternalMethodArguments *args, IOReturn result, uint32_t count, uint64_t a = 0, uint64_t b = 0)
{
  io_user_reference_t values[2] = { a, b };
  OSAsyncReference64 reference;
  memset(reference, 0, sizeof reference);
  memcpy(reference, args->asyncReference, sizeof(io_user_reference_t) *
         (args->asyncReferenceCount < kOSAsyncRef64Count ? args->asyncReferenceCount : kOSAsyncRef64Count));
  CoachzBasebandAsync::send(reference, result, values, count);
}

// ------------------------------------------------------------------ AppleBasebandPCIInterface
class AppleBasebandPCIUserClient : public IOUserClient {
  OSDeclareDefaultStructors(AppleBasebandPCIUserClient)
  task_t fTask;
  uint32_t fChannel;
public:
  bool initWithTask(task_t task, void *security, UInt32 type, OSDictionary *properties) override
  {
    fTask = task;
    return IOUserClient::initWithTask(task, security, type, properties);
  }
  void setChannel(uint32_t channel) { fChannel = channel; }
  IOReturn clientClose() override
  {
    closeChannel();
    terminate();
    return kIOReturnSuccess;
  }
  void closeChannel()
  {
    bb_abort_reads(fChannel, this, kIOReturnAborted);
    IOLockLock(bb_lock);
    if (bb_ch[fChannel].owner == this) bb_ch[fChannel].owner = nullptr;
    IOLockUnlock(bb_lock);
  }
  IOReturn write(IOExternalMethodArguments *args);
  IOReturn read(IOExternalMethodArguments *args);
  IOReturn readRegister(IOExternalMethodArguments *args);
  IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args, IOExternalMethodDispatch *, OSObject *, void *) override;
};
OSDefineMetaClassAndStructors(AppleBasebandPCIUserClient, IOUserClient)

IOReturn AppleBasebandPCIUserClient::write(IOExternalMethodArguments *args)
{
  uint8_t *copy = nullptr;
  uint32_t len = 0;
  if (bb_async(args)) {
    if (args->scalarInputCount < 2) return kIOReturnBadArgument;
    len = (uint32_t)args->scalarInput[1];
    copy = bb_copy_in(fTask, args->scalarInput[0], len);
  } else if (args->structureInputDescriptor) {
    IOMemoryDescriptor *memory = args->structureInputDescriptor;
    len = (uint32_t)memory->getLength();
    copy = len && len <= BB_FRAME_MAX ? static_cast<uint8_t *>(IOMallocData(len)) : nullptr;
    bool ok = copy && memory->prepare() == kIOReturnSuccess;
    if (ok) {
      ok = memory->readBytes(0, copy, len) == len;
      memory->complete();
    }
    if (!ok && copy) {
      IOFreeData(copy, len);
      copy = nullptr;
    }
  } else if (args->structureInput && args->structureInputSize) {
    len = args->structureInputSize;
    copy = len <= BB_FRAME_MAX ? static_cast<uint8_t *>(IOMallocData(len)) : nullptr;
    if (copy) memcpy(copy, args->structureInput, len);
  }
  if (!copy) return kIOReturnBadArgument;
  IOLockLock(bb_lock);
  bb_host_write(fChannel, copy, len);
  IOLockUnlock(bb_lock);
  IOFreeData(copy, len);
  if (bb_async(args)) bb_reply(args, kIOReturnSuccess, 1, len);
  return kIOReturnSuccess;
}

IOReturn AppleBasebandPCIUserClient::read(IOExternalMethodArguments *args)
{
  if (bb_async(args)) {
    if (args->scalarInputCount < 2 || !args->scalarInput[1]) return kIOReturnBadArgument;
    bb_read r;
    memset(&r, 0, sizeof r);
    memcpy(r.reference, args->asyncReference, sizeof(io_user_reference_t) *
           (args->asyncReferenceCount < kOSAsyncRef64Count ? args->asyncReferenceCount : kOSAsyncRef64Count));
    r.len = (uint32_t)args->scalarInput[1];
    r.owner = this;
    r.memory = IOMemoryDescriptor::withAddressRange(args->scalarInput[0], r.len, kIODirectionIn, fTask);
    if (!r.memory || r.memory->prepare() != kIOReturnSuccess) {
      OSSafeReleaseNULL(r.memory);
      return kIOReturnVMError;
    }
    IOLockLock(bb_lock);
    bb_channel *ch = &bb_ch[fChannel];
    const bool queued = ch->reads_waiting < BB_READS;
    if (queued) ch->reads[ch->reads_waiting++] = r;
    if (queued && ch->count && bb_complete_call) thread_call_enter(bb_complete_call);
    IOLockUnlock(bb_lock);
    if (!queued) {
      r.memory->complete();
      r.memory->release();
      return kIOReturnNoResources;
    }
    return kIOReturnSuccess;
  }
  // synchronous: the next frame, waiting up to BB_SYNC_READ_SECONDS
  uint64_t deadline = 0;
  clock_interval_to_deadline(BB_SYNC_READ_SECONDS, kSecondScale, &deadline);
  IOLockLock(bb_lock);
  bb_channel *ch = &bb_ch[fChannel];
  int waited = THREAD_AWAKENED;
  while (!ch->count && ch->owner == this && waited == THREAD_AWAKENED)
    waited = IOLockSleepDeadline(bb_lock, ch, deadline, THREAD_INTERRUPTIBLE);
  bb_frame frame = bb_pop(fChannel);
  IOLockUnlock(bb_lock);
  if (!frame.data) return waited == THREAD_TIMED_OUT ? kIOReturnTimeout : kIOReturnAborted;
  IOReturn result = kIOReturnSuccess;
  if (args->structureOutputDescriptor) {
    IOMemoryDescriptor *memory = args->structureOutputDescriptor;
    const uint32_t room = (uint32_t)memory->getLength(), n = frame.len < room ? frame.len : room;
    if (memory->prepare() == kIOReturnSuccess) {
      memory->writeBytes(0, frame.data, n);
      memory->complete();
      args->structureOutputDescriptorSize = n;
    } else {
      result = kIOReturnVMError;
    }
    if (frame.len > room) result = kIOReturnOverrun;
  } else if (args->structureOutput) {
    const uint32_t n = frame.len < args->structureOutputSize ? frame.len : args->structureOutputSize;
    memcpy(args->structureOutput, frame.data, n);
    if (frame.len > args->structureOutputSize) result = kIOReturnOverrun;
    args->structureOutputSize = n;
  } else {
    result = kIOReturnBadArgument;
  }
  IOFreeData(frame.data, frame.len);
  return result;
}

// IOABPClass::readRegister: one scalar, the register, and a structure output of the size asked for.
IOReturn AppleBasebandPCIUserClient::readRegister(IOExternalMethodArguments *args)
{
  if (args->scalarInputCount < 1) return kIOReturnBadArgument;
  const uint32_t reg = (uint32_t)args->scalarInput[0];   // pbl
  static uint32_t seen;
  if (reg < 32 && !(__atomic_fetch_or(&seen, 1u << reg, __ATOMIC_RELAXED) & (1u << reg)))
    printf("COACHZ baseband: %s register %u read, %u bytes\n", cbh_published[fChannel], reg,
           args->structureOutputDescriptor ? (uint32_t)args->structureOutputDescriptor->getLength() : args->structureOutputSize);
  if (args->structureOutput && args->structureOutputSize) {
    cbs_read_register(reg, static_cast<uint8_t *>(args->structureOutput), args->structureOutputSize);
    return kIOReturnSuccess;
  }
  if (IOMemoryDescriptor *memory = args->structureOutputDescriptor) {
    const uint32_t size = (uint32_t)memory->getLength();
    if (!size || size > BB_FRAME_MAX) return kIOReturnBadArgument;
    uint8_t *data = static_cast<uint8_t *>(IOMallocData(size));
    if (!data) return kIOReturnNoMemory;
    cbs_read_register(reg, data, size);
    IOReturn result = memory->prepare();
    if (result == kIOReturnSuccess) {
      memory->writeBytes(0, data, size);
      memory->complete();
      args->structureOutputDescriptorSize = size;
    }
    IOFreeData(data, size);
    return result;
  }
  return kIOReturnBadArgument;
}

// Selectors and shapes: coachz_bb_channels.h (libIOABP's IOConnectCall* sites).
IOReturn AppleBasebandPCIUserClient::externalMethod(uint32_t selector, IOExternalMethodArguments *args, IOExternalMethodDispatch *,
                                                    OSObject *, void *)
{
  if (!args) return kIOReturnBadArgument;
  switch (selector) {
  case 0: {   // open
    IOLockLock(bb_lock);
    bb_channel *ch = &bb_ch[fChannel];
    const bool free_channel = !ch->owner || ch->owner == this;
    if (free_channel) {
      ch->owner = this;
      if (!ch->opens++) printf("COACHZ baseband: %s opened\n", cbh_published[fChannel]);
      if (fChannel == CBH_SAHARA && bb_boot_image_sent) {
        bb_boot_image_sent = false;
        bb_sahara_restart();
      }
    }
    IOLockUnlock(bb_lock);
    return free_channel ? kIOReturnSuccess : kIOReturnExclusiveAccess;
  }
  case 1:     // close
    closeChannel();
    return kIOReturnSuccess;
  case 2:
    return write(args);
  case 3:
    return read(args);
  case 4:     // sendImage (BHI): the modem is already running; the image is accepted and dropped
    printf("COACHZ baseband: %s sendImage %llu bytes dropped\n", cbh_published[fChannel], args->scalarInputCount > 1 ? args->scalarInput[1] : 0ull);
    IOLockLock(bb_lock);
    if (fChannel == CBH_PBL) bb_boot_image_sent = true;
    IOLockUnlock(bb_lock);
    if (bb_async(args)) bb_reply(args, kIOReturnSuccess, 2, args->scalarInputCount > 1 ? args->scalarInput[1] : 0, 0);
    return kIOReturnSuccess;
  case 5:
    return readRegister(args);
  case 6:     // abort channel: pending reads end
    bb_abort_reads(fChannel, this, kIOReturnAborted);
    if (bb_async(args)) bb_reply(args, kIOReturnSuccess, 0);
    return kIOReturnSuccess;
  case 7:     // start channel
  case 9:     // map shared memory: nothing here reads it
  case 10:    // unmap
  case 12:    // deregister time event
    if (bb_async(args)) bb_reply(args, kIOReturnSuccess, 0);
    return kIOReturnSuccess;
  case 11:    // register time event: none is ever sent
    return kIOReturnSuccess;
  default:    // 8 time sync: no time base behind this channel
    printf("COACHZ baseband: channel %s selector %u unsupported\n", cbh_published[fChannel], selector);
    return kIOReturnUnsupported;
  }
}

class AppleBasebandPCIInterface : public IOService {
  OSDeclareDefaultStructors(AppleBasebandPCIInterface)
  uint32_t fChannel;
public:
  bool setup(uint32_t channel)
  {
    fChannel = channel;
    return setProperty(CBH_PROTOCOL_KEY, cbh_published[channel]);
  }
  // IOABPClass::start_nl puts ABPInterfaceProtocol straight in the IOServiceMatching dictionary. IOKit does not compare a key it does not know: the
  // family's matchPropertyTable does, and without it every client gets whichever channel comes first.
  bool matchPropertyTable(OSDictionary *table, SInt32 *score) override
  {
    return IOService::matchPropertyTable(table, score) && matchProtocol(table);
  }
  bool matchPropertyTable(OSDictionary *table) override
  {
    return IOService::matchPropertyTable(table) && matchProtocol(table);
  }
  bool matchProtocol(OSDictionary *table) const
  {
    OSString *wanted = OSDynamicCast(OSString, table->getObject(CBH_PROTOCOL_KEY));
    bool matched = !wanted || wanted->isEqualTo(cbh_published[fChannel]);
    if (wanted) {
      static uint32_t match_seen;
      if (matched) {
        if (!(__atomic_fetch_or(&match_seen, 1u << fChannel, __ATOMIC_RELAXED) & (1u << fChannel)))
          printf("COACHZ baseband: pci match hit %s\n", cbh_published[fChannel]);
      } else {
        static uint32_t miss_logged;
        if (__atomic_fetch_add(&miss_logged, 1u, __ATOMIC_RELAXED) < 4u)
          printf("COACHZ baseband: pci match query ch=%s wanted=%s\n",
              cbh_published[fChannel], wanted->getCStringNoCopy());
      }
    }
    return matched;
  }
  IOReturn newUserClient(task_t task, void *security, UInt32 type, OSDictionary *properties, IOUserClient **out) override
  {
    if (!out) return kIOReturnBadArgument;
    *out = nullptr;
    static uint32_t seen;
    if (!(__atomic_fetch_or(&seen, 1u << fChannel, __ATOMIC_RELAXED) & (1u << fChannel)))
      printf("COACHZ baseband: first user client on %s\n", cbh_published[fChannel]);
    AppleBasebandPCIUserClient *client = OSTypeAlloc(AppleBasebandPCIUserClient);
    if (!client) return kIOReturnNoMemory;
    client->setChannel(fChannel);
    if (!client->initWithTask(task, security, type, properties) || !client->attach(this)) {
      client->release();
      return kIOReturnError;
    }
    if (!client->start(this)) {
      client->detach(this);
      client->release();
      return kIOReturnError;
    }
    *out = client;
    return kIOReturnSuccess;
  }
  IOReturn newUserClient(task_t task, void *security, UInt32 type, IOUserClient **out) override
  {
    return newUserClient(task, security, type, nullptr, out);
  }
};
OSDefineMetaClassAndStructors(AppleBasebandPCIInterface, IOService)

// ------------------------------------------------------------------ AppleBasebandPCIControl
class AppleBasebandPCIControlUserClient : public IOUserClient {
  OSDeclareDefaultStructors(AppleBasebandPCIControlUserClient)
  OSAsyncReference64 fLogs;
  bool fLogsPending;
public:
  void endLogs()
  {
    if (!fLogsPending) return;
    fLogsPending = false;
    io_user_reference_t none[1] = { 0 };
    CoachzBasebandAsync::send(fLogs, kIOReturnAborted, none, 1);
  }
  IOReturn clientClose() override
  {
    endLogs();
    terminate();
    return kIOReturnSuccess;
  }
  IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args, IOExternalMethodDispatch *, OSObject *, void *) override
  {
    if (!args) return kIOReturnBadArgument;
    switch (selector) {
    case 0:   // open
    case 3:   // logger tune
      return kIOReturnSuccess;
    case 1:   // close
      endLogs();
      return kIOReturnSuccess;
    case 2:   // read logs: there are none; the request waits until close
      if (!bb_async(args) || fLogsPending) return kIOReturnBadArgument;
      memset(fLogs, 0, sizeof fLogs);
      memcpy(fLogs, args->asyncReference, sizeof(io_user_reference_t) *
             (args->asyncReferenceCount < kOSAsyncRef64Count ? args->asyncReferenceCount : kOSAsyncRef64Count));
      fLogsPending = true;
      return kIOReturnSuccess;
    default:
      return kIOReturnUnsupported;
    }
  }
};
OSDefineMetaClassAndStructors(AppleBasebandPCIControlUserClient, IOUserClient)

template <class Client>
static IOReturn bb_open(IOService *service, task_t task, void *security, UInt32 type, OSDictionary *properties, IOUserClient **out)
{
  if (!out) return kIOReturnBadArgument;
  *out = nullptr;
  Client *client = OSTypeAlloc(Client);
  if (!client) return kIOReturnNoMemory;
  if (!client->initWithTask(task, security, type, properties) || !client->attach(service)) {
    client->release();
    return kIOReturnError;
  }
  if (!client->start(service)) {
    client->detach(service);
    client->release();
    return kIOReturnError;
  }
  *out = client;
  return kIOReturnSuccess;
}

class AppleBasebandPCIControl : public IOService {
  OSDeclareDefaultStructors(AppleBasebandPCIControl)
public:
  IOReturn newUserClient(task_t t, void *s, UInt32 type, OSDictionary *p, IOUserClient **out) override
  {
    static uint32_t seen;
    if (!__atomic_exchange_n(&seen, 1, __ATOMIC_RELAXED)) printf("COACHZ baseband: first user client on AppleBasebandPCIControl\n");
    return bb_open<AppleBasebandPCIControlUserClient>(this, t, s, type, p, out);
  }
  IOReturn newUserClient(task_t t, void *s, UInt32 type, IOUserClient **out) override { return newUserClient(t, s, type, nullptr, out); }
};
OSDefineMetaClassAndStructors(AppleBasebandPCIControl, IOService)

// ------------------------------------------------------------------ AppleBaseband
class AppleBasebandUserClient : public IOUserClient {
  OSDeclareDefaultStructors(AppleBasebandUserClient)
public:
  IOReturn clientClose() override
  {
    terminate();
    return kIOReturnSuccess;
  }
  IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args, IOExternalMethodDispatch *, OSObject *, void *) override
  {
    if (!args) return kIOReturnBadArgument;
    uint64_t out[4] = { 0 };
    uint32_t nout = args->scalarOutputCount < 4 ? args->scalarOutputCount : 4;
    IOLockLock(bb_lock);
    const int result = cbc_call(&bb_control, selector, args->scalarInput, args->scalarInputCount, out, &nout);
    // A reset or power-on goes back to before SendImage: nothing greets until the next one.
    if (!result && (selector == CBC_RESET_MODEM || selector == CBC_POWERCYCLE || (selector == CBC_POWER && args->scalarInputCount && args->scalarInput[0]))) {
      bb_boot_image_sent = false;
      bb_flush(CBH_SAHARA);
    }
    IOLockUnlock(bb_lock);
    static uint64_t ctrl_seen;
    if (selector < 64 && !(__atomic_fetch_or(&ctrl_seen, 1ull << selector, __ATOMIC_RELAXED) & (1ull << selector)))
      printf("COACHZ baseband: AppleBaseband selector %u first call (in=%u out=%u rc=%#x)\n",
             selector, args->scalarInputCount, nout, result);
    if (result == CBC_UNSUPPORTED) printf("COACHZ baseband: AppleBaseband selector %u unsupported\n", selector);
    for (uint32_t i = 0; i < nout; i++) args->scalarOutput[i] = out[i];
    args->scalarOutputCount = nout;
    return result;
  }
};
OSDefineMetaClassAndStructors(AppleBasebandUserClient, IOUserClient)

class AppleBaseband : public IOService {
  OSDeclareDefaultStructors(AppleBaseband)
public:
  IOReturn newUserClient(task_t t, void *s, UInt32 type, OSDictionary *p, IOUserClient **out) override
  {
    return bb_open<AppleBasebandUserClient>(this, t, s, type, p, out);
  }
  IOReturn newUserClient(task_t t, void *s, UInt32 type, IOUserClient **out) override { return newUserClient(t, s, type, nullptr, out); }
};
OSDefineMetaClassAndStructors(AppleBaseband, IOService)

#include "coachz_bb_pdp.cpp"

// ------------------------------------------------------------------ the data plane's kernel services
// ABM's data::TransportService calls ApplePDPHelperInterface::getInstance()->enableMetadataTrailer() without a null check, and getInstance
// is null without an AppleIPAppender service to open. ApplePDPHelper picks its variant by which PDP manager is registered; this is the Mav
// one. Nothing is behind them until pdp_ip exists: every AppleIPAppender method (0 addFlow ... 20 registerFilterRuleCallBack, libApplePDPHelper)
// succeeds with zeroed outputs, and the first use of each selector is logged.
class AppleIPAppenderUserClient : public IOUserClient {
  OSDeclareDefaultStructors(AppleIPAppenderUserClient)
public:
  IOReturn clientClose() override
  {
    terminate();
    return kIOReturnSuccess;
  }
  IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args, IOExternalMethodDispatch *, OSObject *, void *) override
  {
    if (!args) return kIOReturnBadArgument;
    // Capture the first call shape for each helper operation, including the traffic gate.
    // Payloads can contain network identities. Record only sizes and counts.
    static uint32_t shapes_seen;
    if (selector < 32 && !(__atomic_fetch_or(&shapes_seen, 1u << selector, __ATOMIC_RELAXED) & (1u << selector)))
      printf("COACHZ pdp: appender selector %u scalar-in %u scalar-out %u struct-in %llu struct-out %llu descriptor-in %llu descriptor-out %llu\n",
        selector, args->scalarInputCount, args->scalarOutputCount,
        static_cast<unsigned long long>(args->structureInputSize), static_cast<unsigned long long>(args->structureOutputSize),
        static_cast<unsigned long long>(args->structureInputDescriptor ? args->structureInputDescriptor->getLength() : 0),
        static_cast<unsigned long long>(args->structureOutputDescriptor ? args->structureOutputDescriptor->getLength() : 0));
    if (selector == 16) {
      if (args->scalarInputCount != 2) return kIOReturnBadArgument;
      static uint32_t block_logs;
      if (block_logs++ < 32u)
        printf("COACHZ pdp: appender block unit %u blocked %u t=%u\n",
               static_cast<uint32_t>(args->scalarInput[0]),
               static_cast<uint32_t>(args->scalarInput[1]), bb_now_ms());
      return bb_pdp_block(static_cast<uint32_t>(args->scalarInput[0]), static_cast<uint32_t>(args->scalarInput[1]));
    }
    static uint32_t seen;
    if (selector < 32 && !(__atomic_fetch_or(&seen, 1u << selector, __ATOMIC_RELAXED) & (1u << selector)))
      printf("COACHZ baseband: AppleIPAppender selector %u answered with zeros\n", selector);
    for (uint32_t i = 0; i < args->scalarOutputCount; i++) args->scalarOutput[i] = 0;
    if (args->structureOutput && args->structureOutputSize) bzero(args->structureOutput, args->structureOutputSize);
    if (args->structureOutputDescriptor) args->structureOutputDescriptorSize = 0;
    return kIOReturnSuccess;
  }
};
OSDefineMetaClassAndStructors(AppleIPAppenderUserClient, IOUserClient)

class AppleIPAppender : public IOService {
  OSDeclareDefaultStructors(AppleIPAppender)
public:
  IOReturn newUserClient(task_t t, void *s, UInt32 type, OSDictionary *p, IOUserClient **out) override
  {
    return bb_open<AppleIPAppenderUserClient>(this, t, s, type, p, out);
  }
  IOReturn newUserClient(task_t t, void *s, UInt32 type, IOUserClient **out) override { return newUserClient(t, s, type, nullptr, out); }
};
OSDefineMetaClassAndStructors(AppleIPAppender, IOService)

// libPCITransport's pci::event::PDPListener matches the base class, ApplePDPHelper the leaf and AppleBasebandPCIPDPManager.
class AppleBasebandPCIPDPManagerBase : public IOService {
  OSDeclareDefaultStructors(AppleBasebandPCIPDPManagerBase)
};
OSDefineMetaClassAndStructors(AppleBasebandPCIPDPManagerBase, IOService)

class AppleBasebandPCIPDPManager : public AppleBasebandPCIPDPManagerBase {
  OSDeclareDefaultStructors(AppleBasebandPCIPDPManager)
};
OSDefineMetaClassAndStructors(AppleBasebandPCIPDPManager, AppleBasebandPCIPDPManagerBase)

class AppleBasebandPCIMAVPDPManager : public AppleBasebandPCIPDPManager {
  OSDeclareDefaultStructors(AppleBasebandPCIMAVPDPManager)
};
OSDefineMetaClassAndStructors(AppleBasebandPCIMAVPDPManager, AppleBasebandPCIPDPManager)

// ------------------------------------------------------------------ publication and the Wi-Fi thread's entry points
template <class Service>
static Service *bb_publish(IOService *provider, uint32_t channel = ~0u)
{
  Service *service = OSTypeAlloc(Service);
  bool ok = service && service->init();
  if (ok && channel != ~0u) {
    AppleBasebandPCIInterface *interface = OSDynamicCast(AppleBasebandPCIInterface, service);
    ok = interface && interface->setup(channel);
  }
  ok = ok && service->attach(provider);
  if (ok && !service->start(provider)) {
    service->detach(provider);
    ok = false;
  }
  if (!ok) {
    OSSafeReleaseNULL(service);
    return nullptr;
  }
  service->registerService();
  return service;
}

// The modem's QRTR is up (GLINK started): publish the services ABM looks for. `send` and `del_client` run on the Wi-Fi thread only.
extern "C" void coachz_baseband_configure(void (*send)(uint32_t, uint32_t, uint32_t, const void *, uint32_t), void (*del_client)(uint32_t))
{
  if (!coachz_baseband_enabled() || bb_published) return;
  bb_init();
  bb_pdp_init();
  bb_send = send;
  bb_del_client = del_client;
  IOService *provider = IOService::getResourceService();
  int made = 0;
  made += provider && bb_publish<AppleBaseband>(provider);
  made += provider && bb_publish<AppleBasebandPCIControl>(provider);
  for (uint32_t c = 0; c < CBH_PUBLISHED; c++) made += provider && bb_publish<AppleBasebandPCIInterface>(provider, c);
  made += provider && bb_publish<AppleBasebandPCIMAVPDPManager>(provider);
  made += provider && bb_publish<AppleIPAppender>(provider);
  bb_published = 1;
  printf("COACHZ baseband: %d of %u services published (AppleBaseband, AppleBasebandPCIControl, %u channels, the PDP manager, AppleIPAppender)\n", made, 4u + CBH_PUBLISHED, CBH_PUBLISHED);
}

extern "C" void coachz_baseband_new_server(uint32_t service, uint32_t version_instance, uint32_t node, uint32_t port)
{
  if (!coachz_baseband_enabled()) return;
  bb_init();
  IOLockLock(bb_lock);
  cbq_server_new(&bb_qmux, service, version_instance, node, port);
  if (service == CBD_SVC_DPM || service == CBD_SVC_WDA) bb_data_try();
  if (service == FGL_SVC_LOC) bb_gnss_probe_poll();
  IOLockUnlock(bb_lock);
}

extern "C" void coachz_baseband_del_server(uint32_t service, uint32_t node, uint32_t port)
{
  if (!bb_lock) return;
  IOLockLock(bb_lock);
  cbq_server_del(&bb_qmux, service, node, port);
  if (service == CBD_SVC_WDS) bzero(bb_pdp_clients, sizeof bb_pdp_clients);
  if ((service == CBD_SVC_DPM || service == CBD_SVC_WDA) && !cbq_server_find(&bb_qmux, service))
    cbd_server_lost(&bb_data, service);
  if (service == FGL_SVC_LOC && !cbq_server_find(&bb_qmux, FGL_SVC_LOC)) {
    if (bb_loc_slot >= 0) { cbq_client_free(&bb_qmux, (unsigned)bb_loc_slot); bb_loc_slot = -1; }
    bb_gnss.loc_registered = 0u;
    bb_gnss.probe_active = 0u;
    bb_gnss.probe_handover_stop = 0u;
    bb_gnss.probe_handover_off = 0u;
    bb_gnss_probe_step = 0u;
    bb_gnss_reg_fallback = 0u;
    fgl_unlock_init(&bb_gnss_unlock);
    bb_gnss_reg_txn = 0u;
    bb_gnss_reg_mask = 0ULL;
    bb_gnss_pending_head = 0u;
    bb_gnss_pending_count = 0u;
  }
  IOLockUnlock(bb_lock);
}

// QRTR data for one of the bridge's client ports: 1 when the port is the bridge's.
extern "C" int coachz_baseband_qrtr(uint32_t dst_port, uint32_t src_node, uint32_t, const void *body, uint32_t size)
{
  if (dst_port < CBQ_PORT_BASE || dst_port >= CBQ_PORT_BASE + CBQ_MAX_CLIENTS) return 0;
  if (!bb_lock) return 1;
  IOLockLock(bb_lock);
  const struct cbq_result r = cbq_from_modem(&bb_qmux, dst_port, src_node, static_cast<const uint8_t *>(body), size, bb_out, sizeof bb_out);
  if (r.kind == CBQ_TO_HOST && r.channel == CBQ_INTERNAL) {
    bb_trace("int-rx", 0, r.service, r.cid, static_cast<const uint8_t *>(body), size);
    if (bb_esim_slot >= 0 && dst_port == CBQ_PORT_BASE + (uint32_t)bb_esim_slot) {
      cbe_answer(&bb_esim, static_cast<const uint8_t *>(body), size, bb_now_ms());
    } else if (bb_ims_slot[0] >= 0 && dst_port == CBQ_PORT_BASE + (uint32_t)bb_ims_slot[0]) {
      bb_ims_answer(0, static_cast<const uint8_t *>(body), size);
    } else if (bb_ims_slot[1] >= 0 && dst_port == CBQ_PORT_BASE + (uint32_t)bb_ims_slot[1]) {
      bb_ims_answer(1, static_cast<const uint8_t *>(body), size);
    } else if (bb_loc_slot >= 0 && dst_port == CBQ_PORT_BASE + (uint32_t)bb_loc_slot) {
      bb_gnss_loc_answer(static_cast<const uint8_t *>(body), size);
    } else if (cbd_answer(&bb_data, r.service, static_cast<const uint8_t *>(body), size)) {
      if (bb_data.step == CBD_DONE) bb_data_log_done();
      else bb_data_try();
    }
  } else if (r.kind == CBQ_TO_HOST) {
    if (r.service == CBD_SVC_WDS) bb_pdp_note(dst_port, r.cid, r.channel, static_cast<const uint8_t *>(body), size);
    // A temporary eUICC card-status indication is not the restored logical SIM.
    const uint8_t *sdu = static_cast<const uint8_t *>(body);
    if (bb_esim.freeze && r.service == CBE_SVC_UIM && size >= 7 &&
        sdu[0] == CBQ_QMI_INDICATION && cbd_rd16(sdu + 3) == 0x32u) {
      IOLockUnlock(bb_lock);
      return 1;
    }
    const uint32_t fixed = cbe_card_status_fix(&bb_esim, r.service,
      static_cast<const uint8_t *>(body), size, bb_fix, sizeof bb_fix);
    if (fixed) {
      cbq_frame_header(bb_out, fixed, r.service, r.cid);
      memcpy(bb_out + CBQ_HDR, bb_fix, fixed);
      bb_push(r.channel == 0 ? CBH_QMUX1 : CBH_QMUX2, bb_out, CBQ_HDR + fixed);
    } else bb_push(r.channel == 0 ? CBH_QMUX1 : CBH_QMUX2, bb_out, r.len);
    bb_sim_answer(r, sdu, size);   // after the answer itself: bb_activation_indicate reuses bb_out
    if (bb_ims_probe() && r.service == CBD_SVC_QOS && size >= 7u &&
        sdu[0] == CBQ_QMI_INDICATION && cbd_rd16(sdu + 3) == 0x01u)
      bb_ims_probe_event("qos-ind1", sdu, size);
  }
  IOLockUnlock(bb_lock);
  return 1;
}

// On the Wi-Fi thread: QMI requests for the modem, then DEL_CLIENT for released clients.
extern "C" void coachz_baseband_poll(void)
{
  if (!bb_lock || !bb_send) return;
  bb_tx batch[16];
  uint32_t n = 0, released[16], r = 0, port;
  IOLockLock(bb_lock);
  bb_data_try();
  bb_esim_step();
  bb_ims_cfg_poll();
  bb_gnss_probe_poll();
  if (bb_ims_probe() && bb_imsa_step >= 3u && cbq_server_find(&bb_qmux, BB_SVC_IMSA)) {
    const uint32_t now = bb_now_ms();
    if (!bb_ims_probe_next_ms) bb_ims_probe_next_ms = now + 5000u;
    else if ((int32_t)(now - bb_ims_probe_next_ms) >= 0) {
      bb_ims_probe_next_ms = now + 5000u;
      bb_ims_probe_ping("tick");
    }
  }
  while (bb_tx_count && n < 16) {
    batch[n++] = bb_txq[bb_tx_head];
    bb_tx_head = (bb_tx_head + 1) % BB_TX;
    bb_tx_count--;
  }
  while (r < 16 && (port = cbq_next_release(&bb_qmux))) released[r++] = port;
  IOLockUnlock(bb_lock);
  for (uint32_t i = 0; i < n; i++) {
    bb_send(batch[i].local_port, batch[i].node, batch[i].port, batch[i].data, batch[i].len);
    IOFreeData(batch[i].data, batch[i].len);
  }
  for (uint32_t i = 0; i < r; i++)
    if (bb_del_client) bb_del_client(released[i]);
  bb_pdp_poll();
}

extern "C" void coachz_baseband_report(unsigned second)
{
  if (!bb_lock || !bb_published) return;
  bb_pdp_report(second);
  IOLockLock(bb_lock);
  uint32_t clients = 0, servers = 0;
  for (uint32_t i = 0; i < CBQ_MAX_CLIENTS; i++) clients += bb_qmux.client[i].used;
  for (uint32_t i = 0; i < CBQ_MAX_SERVERS; i++) servers += bb_qmux.server[i].present;
  printf("COACHZ baseband: %u s: %u QMI services, %u clients, dropped %u/%u; sahara state %u, %u executes, %u requests; board calls %u (%u unsupported); data port step %u, %u endpoints named, %u activations\n",
         second, servers, clients, bb_qmux.dropped, bb_tx_dropped, bb_sahara.state, bb_sahara.executes, bb_sahara.requests, bb_control.calls, bb_control.unsupported,
         bb_data.step, bb_endpoint_fixes, bb_activations);
  printf("COACHZ qmi: %u s: %u trace lines, %u held back by the budget\n", second, bb_trace_lines, bb_trace_held);
  printf("COACHZ baseband: %u s: client slots %u in use, peak %u, limit %u, %u refused\n", second, clients, bb_qmux.peak,
         bb_qmux.client_limit, bb_qmux.exhausted);
  if (bb_gnss_bits())
    printf("COACHZ gnss: %u s: mode=0x%x pds(rx=%u tx=%u track=%u) loc(tx=%u rx=%u fix=%u/%u sv=%u nmea=%u unk=%u)\n",
           second, bb_gnss_bits(), bb_gnss.pds_rx, bb_gnss.pds_tx, bb_gnss.tracking,
           bb_gnss.loc_tx, bb_gnss.loc_rx, bb_gnss.fixes_ok, bb_gnss.fixes_in_prog,
           bb_gnss.sv_reports, bb_gnss.nmea_reports, bb_gnss.unknown_reqs);
  if (bb_diag()) {
    static uint8_t per[256];
    char tally[400];
    uint32_t w = 0;
    bzero(per, sizeof per);
    for (uint32_t i = 0; i < CBQ_MAX_CLIENTS; i++)
      if (bb_qmux.client[i].used) per[bb_qmux.client[i].service]++;
    tally[0] = 0;
    for (uint32_t s = 0; s < 256u && w + 12u < sizeof tally; s++)
      if (per[s]) w += (uint32_t)snprintf(tally + w, sizeof tally - w, " %02x:%u", s, per[s]);
    printf("COACHZ baseband: %u s: clients by service%s\n", second, tally);
    bb_ims_poll();
  }
  for (uint32_t c = 0; c < CBH_PUBLISHED; c++) {
    const bb_channel *ch = &bb_ch[c];
    if (ch->opens)
      printf("COACHZ baseband: %s: opens %u, in %u frames %u bytes, out %u frames %u bytes, dropped %u, queued %u, reads waiting %u\n",
             cbh_published[c], ch->opens, ch->frames_in, ch->bytes_in, ch->frames_out, ch->bytes_out, ch->frames_dropped, ch->count,
             ch->reads_waiting);
  }
  IOLockUnlock(bb_lock);
}
#else
#include <stdint.h>
extern "C" {
int coachz_baseband_enabled(void) { return 0; }
void coachz_baseband_configure(void (*)(uint32_t, uint32_t, uint32_t, const void *, uint32_t), void (*)(uint32_t)) {}
void coachz_baseband_new_server(uint32_t, uint32_t, uint32_t, uint32_t) {}
void coachz_baseband_del_server(uint32_t, uint32_t, uint32_t) {}
int coachz_baseband_qrtr(uint32_t, uint32_t, uint32_t, const void *, uint32_t) { return 0; }
void coachz_baseband_poll(void) {}
void coachz_baseband_report(unsigned) {}
}
#endif /* FUXI */
