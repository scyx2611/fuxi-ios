#include <sys/fuxi_wifi_ios.h>
#include <sys/fuxi_wifi_wpa.h>
#include <sys/errno.h>
#include <sys/random.h>
#include <kern/locks.h>

/*
 * The iOS station over the pinned ath12k module's cfg80211 control channel.
 *
 * Every control request, reply and event is handled on the WLAN worker thread (wl_ath_run): the
 * replies and events reach wli_receive() from inside nw_wifi_step()/nw_wifi_control_receive().
 * Callers on the station thread hand one operation at a time to the worker through wli_submit()
 * and sleep until it is over. The join state (wlj) and the request buffer are the worker's alone.
 *
 * Joining: AUTHENTICATE, ASSOCIATE with our own RSN element, the four-way handshake in
 * fuxi_wifi_wpa.c over RX_EAPOL/TX_EAPOL, the pairwise and group keys, then the port. Only
 * WPA2-Personal (AKM 00-0F-AC:2, pairwise CCMP) and open networks; a network that requires
 * management frame protection or offers only SAE (WPA3) is refused with EOPNOTSUPP.
 * Neither keys, passphrases nor EAPOL payloads are ever logged.
 */

#define WLI_BSS_MAX 128
#define WLI_CHANNEL_MAX 96
#define WLI_REQUEST_MOST 1536
#define WLI_EAPOL_MOST 1024
#define WLI_JOIN_MS 20000
#define WLI_FRESH_MS 20000
#define WLI_CACHE_MS 5000
#define WLI_ACTIVE_REUSE_MS 3000u
#define WLI_DEFAULT_DWELL_MS 110u
#define WLI_SCAN_PART_MS 19000
#define WLI_SCAN_BUDGET_MS 25000
#define WLI_DIAG_SCANS 2
#define WLI_DIAG_LINES 48

enum { WLI_POWER = 1, WLI_SCAN, WLI_JOIN, WLI_LEAVE };
enum { WLI_RESET_NONE, WLI_RESET_OFF_WAIT, WLI_RESET_ON_SEND, WLI_RESET_ON_WAIT, WLI_RESET_DONE };
enum {
	WLJ_NONE, WLJ_AUTH, WLJ_AUTH_WAIT, WLJ_ASSOC, WLJ_ASSOC_WAIT, WLJ_KEYS_WAIT,
	WLJ_PAIRWISE, WLJ_GROUP, WLJ_PORT, WLJ_OPERSTATE, WLJ_UP, WLJ_JOINED, WLJ_REKEY,
	WLJ_ABORT, WLJ_FAILED, WLJ_LEAVE_WAIT
};
#define WLI_SUITE(type) (0x000fac00u | (type))

struct wli_target {
	struct fuxi_wifi_sta_bss bss;
	uint8_t pmk[32];
	int security;
	uint32_t group;
};

struct wli_rsn {
	unsigned present, complete, group, pairwise, akm, caps;
};

static LCK_GRP_DECLARE(wli_group, "fuxi-wlan-ios");
static LCK_MTX_DECLARE(wli_lock, &wli_group);
static struct fuxi_wifi_backend wli_backend;
static const struct fuxi_wifi_input *wli_input;
static struct fuxi_wifi_sta_state wli_state;
static struct fuxi_wifi_sta_bss wli_bss[WLI_BSS_MAX];
static struct fuxi_wifi_sta_channel wli_channels[WLI_CHANNEL_MAX];
static unsigned wli_bss_count, wli_channel_count;
static struct {
	uint64_t session, generation, deadline, scanned_at;
	uint64_t begun_at, fetch_at;            /* 診斷：工作開始、開始取回掃描結果的時間（ms） */
	unsigned scan_parts;
	uint32_t sequence, pending_sequence, features;
	unsigned pending_operation, info, channels, published, busy, submitted;
	unsigned operation, power_on, power_waiting, scan_accepted, scan_done;
	bool scan_draining;                      /* under wli_lock: wait for the abandoned scan's terminal event */
	int scan_error;                          /* worker only: status of the current SCAN_DONE */
	unsigned fetching, fetch_index, scan_reports, diag_lines, dwell_enabled;
	unsigned cache_enabled, cache_valid, cache_hits, scan_rejections;
	unsigned batch_enabled, scan_count, scan_limit, scan_offset;
	unsigned ux;                            /* fuxi,wifi-ux（FUXI_WIFI_UX_*），發布時讀一次 */
	unsigned ap_active;                     /* under wli_lock: SoftAP interface currently active */
	unsigned ap_started;                    /* under wli_lock: SoftAP was actually started since boot/last stop */
	unsigned sta_reset_needed;              /* under wli_lock: recycle primary netdev before next STA scan/join */
	unsigned reset_stage;                   /* under wli_lock: internal WLI_RESET_* state while busy */
	unsigned handshake_timeouts;            /* worker only: consecutive Reason 15 4-way handshake timeouts */
	int result, fatal;
	struct fuxi_wifi_sta_scan scan;
} wli;
/* What a join asks for: written by wli_submit() under wli_lock, taken by the worker when it begins. */
static struct wli_target wli_target;
/* The station thread's own copy while it prepares a join (join() calls never overlap). */
static struct wli_target wli_asked;
/* Frames may pass: set by the worker, read by the transmit path. */
static unsigned wli_link;
/* The join and the link: the worker's alone. */
static struct {
	struct fuxi_wifi_sta_bss bss;
	struct fuxi_wifi_wpa wpa;
	uint8_t rsn[22];
	uint64_t deadline;
	uint32_t group;
	unsigned step, security, joined, leaving, answered, auth_seen, assoc_seen, timed_out;
	unsigned lost, lost_reason, eapol_ready, rekeys, eapol_tx_ack, eapol_tx_nack;
	int auth_status, assoc_status, error;
	size_t eapol_length;
	uint8_t eapol[WLI_EAPOL_MOST];
	uint8_t reply[FUXI_WIFI_WPA_REPLY_MOST];
} wlj;
static struct fuxi_wifi_sta_bss wli_fetched;
static uint8_t wli_out[WLI_REQUEST_MOST];
static size_t wli_out_length;
static unsigned wli_out_overflow;

static int
wli_status(unsigned status)
{
	static const int errors[] = { 0, EINVAL, EBUSY, ENXIO, EAUTH, ETIMEDOUT,
		EOPNOTSUPP, ENOMEM, ENOENT, EIO, ESTALE };
	return status < sizeof(errors) / sizeof(errors[0]) ? errors[status] : EPROTO;
}

static void
wli_complete(int error)
{
	lck_mtx_lock(&wli_lock);
	wli.result = error;
	wli_state.last_error = error;
	if (wli_state.phase == FUXI_WIFI_STA_SCANNING) { wli_state.phase = FUXI_WIFI_STA_IDLE; }
	wli.busy = wli.submitted = wli.power_waiting = wli.fetching = 0;
	wli.reset_stage = WLI_RESET_NONE;
	lck_mtx_unlock(&wli_lock);
	thread_wakeup((event_t)&wli.busy);
}

/* Forget the join and the keys; the radio side is not told (callers do that first, if needed). */
static void
wli_forget(void)
{
	__atomic_store_n(&wli_link, 0, __ATOMIC_RELEASE);
	memset(&wlj, 0, sizeof(wlj));
}

static void
wli_fail(int error)
{
	bool linked = wlj.joined;
	wli_forget();
	lck_mtx_lock(&wli_lock);
	wli.fatal = error;
	wli_state.phase = FUXI_WIFI_STA_OFF;
	wli_bss_count = 0;
	wli.cache_valid = 0;
	lck_mtx_unlock(&wli_lock);
	wli_complete(error);
	if (wli_input) { wli_input->link_state(false); }
	if (linked) { fuxi_wifi_sta_lost(error); }
}

static int
wli_scan_plan(const struct fuxi_wifi_sta_scan *scan, unsigned *count, unsigned *limit)
{
	*count = *limit = scan->frequency_count;
	if (!wli.batch_enabled || !scan->dwell_ms) { return 0; }
	if (scan->dwell_ms > 1000) { return EINVAL; }
	if (!wli.dwell_enabled || !(wli.features & FWC_FEATURE_SCAN_DWELL_MS)) { return EOPNOTSUPP; }
	*count = scan->frequency_count ? scan->frequency_count : wli_channel_count;
	if (!*count || *count > WLI_CHANNEL_MAX) { return EINVAL; }
	unsigned duration_tu = (scan->dwell_ms * 1000u + 1023u) / 1024u;
	unsigned per_channel_ms = (duration_tu * 1024u + 999u) / 1000u + 500u;
	if (per_channel_ms * *count > WLI_SCAN_BUDGET_MS) { return EINVAL; }
	*limit = MIN(*count, WLI_SCAN_PART_MS / per_channel_ms);
	return *limit ? 0 : EINVAL;
}

static int
wli_submit(unsigned operation, bool on, const struct fuxi_wifi_sta_scan *scan, const struct wli_target *target)
{
	if ((operation == WLI_SCAN || operation == WLI_JOIN) && fuxi_wifi_sta_power_off_pending()) {
		return ECANCELED;
	}
	unsigned scan_count = 0, scan_limit = 0;
	if (scan) {
		int error = wli_scan_plan(scan, &scan_count, &scan_limit);
		if (error) { return error; }
	}
	lck_mtx_lock(&wli_lock);
	bool draining = wli.scan_draining && (operation == WLI_SCAN || operation == WLI_JOIN);
	if (wli.fatal || !wli.published || wli.busy || draining) {
		int error = wli.fatal ? wli.fatal : wli.busy || draining ? EBUSY : ENXIO;
		lck_mtx_unlock(&wli_lock);
		return error;
	}
	wli.operation = operation;
	wli.power_on = on;
	if (scan) {
		wli.scan = *scan;
		wli.scan_count = scan_count;
		wli.scan_limit = scan_limit;
		wli.scan_offset = 0;
	}
	if (target) { wli_target = *target; }
	wli.busy = 1;
	wli.submitted = 0;
	wli.result = EINPROGRESS;
	uint64_t deadline, asked_at = wla_now_ms();
	clock_interval_to_deadline(40000, NSEC_PER_MSEC, &deadline);
	while (wli.busy && !wli.fatal) {
		if (lck_mtx_sleep_deadline(&wli_lock, LCK_SLEEP_DEFAULT, (event_t)&wli.busy,
		    THREAD_UNINT, deadline) == THREAD_TIMED_OUT && wli.busy) {
			wli.fatal = ETIMEDOUT;
			wli.result = ETIMEDOUT;
			wli_state.phase = FUXI_WIFI_STA_OFF;
			break;
		}
	}
	int error = wli.fatal ? wli.fatal : wli.result;
	bool report_scan = error && operation == WLI_SCAN && scan && wli.scan_rejections < 16;
	if (report_scan) { wli.scan_rejections++; }
	lck_mtx_unlock(&wli_lock);
	printf("FUXI WIFI IOS: native operation %u on %u completed %d in %llu ms\n", operation, on, error,
	    (unsigned long long)(wla_now_ms() - asked_at));
	if (report_scan) {
		printf("FUXI WIFI IOS: native scan rejected error %d passive %u dwell %u ms channels %u first %u MHz\n",
		    error, scan->passive, scan->dwell_ms, scan->frequency_count,
		    scan->frequency_count ? scan->frequencies[0] : 0);
	}
	return error;
}

static int
wli_power(bool on)
{
	return wli_submit(WLI_POWER, on, NULL, NULL);
}

static int
wli_scan_covers(const struct fuxi_wifi_sta_scan *previous, const struct fuxi_wifi_sta_scan *asked)
{
	if (previous->passive != asked->passive || previous->dwell_ms != asked->dwell_ms ||
	    (previous->ssid_length && (previous->ssid_length != asked->ssid_length ||
	    memcmp(previous->ssid, asked->ssid, previous->ssid_length)))) { return 0; }
	if (!previous->frequency_count) { return 1; }
	if (!asked->frequency_count) { return 0; }
	for (unsigned index = 0; index < asked->frequency_count; index++) {
		bool present = false;
		for (unsigned prior = 0; prior < previous->frequency_count; prior++) {
			present |= previous->frequencies[prior] == asked->frequencies[index];
		}
		if (!present) { return 0; }
	}
	return 1;
}

static int
wli_scan_covers_channels(const struct fuxi_wifi_sta_scan *previous, const struct fuxi_wifi_sta_scan *asked)
{
	if (previous->passive || asked->passive || previous->ssid_length || asked->ssid_length) {
		return 0;
	}
	if (previous->dwell_ms && previous->dwell_ms < 40) { return 0; }
	if (!previous->frequency_count) { return 1; }
	if (!asked->frequency_count) { return 0; }
	for (unsigned index = 0; index < asked->frequency_count; index++) {
		bool present = false;
		for (unsigned prior = 0; prior < previous->frequency_count; prior++) {
			present |= previous->frequencies[prior] == asked->frequencies[index];
		}
		if (!present) { return 0; }
	}
	return 1;
}

/* Under wli_lock: bound default dwell_ms=0 -> 110 ms only when budget and passive savings hold. */
static bool
wli_should_default_dwell(const struct fuxi_wifi_sta_scan *scan)
{
	if (scan->dwell_ms || !(wli.ux & FUXI_WIFI_UX_SCAN_DWELL_110) || !wli.dwell_enabled ||
	    !(wli.features & FWC_FEATURE_SCAN_DWELL_MS) || !wli.published || !wli_channel_count) {
		return false;
	}
	unsigned count = scan->frequency_count ? scan->frequency_count : wli_channel_count;
	if (!count || count > WLI_CHANNEL_MAX) { return false; }
	unsigned duration_tu = (WLI_DEFAULT_DWELL_MS * 1000u + 1023u) / 1024u;
	unsigned per_channel_ms = (duration_tu * 1024u + 999u) / 1000u + 500u;
	unsigned budget = wli.batch_enabled ? WLI_SCAN_BUDGET_MS : WLI_SCAN_PART_MS;
	if (per_channel_ms * count > budget) { return false; }
	unsigned passive_ch = 0, active_ch = 0;
	if (!scan->frequency_count) {
		for (unsigned index = 0; index < wli_channel_count; index++) {
			if (wli_channels[index].flags & FUXI_WIFI_STA_CHANNEL_PASSIVE) { passive_ch++; }
			else { active_ch++; }
		}
	} else {
		for (unsigned index = 0; index < scan->frequency_count; index++) {
			bool passive = false;
			for (unsigned prior = 0; prior < wli_channel_count; prior++) {
				if (wli_channels[prior].frequency == scan->frequencies[index]) {
					passive = (wli_channels[prior].flags & FUXI_WIFI_STA_CHANNEL_PASSIVE) != 0;
					break;
				}
			}
			if (passive) { passive_ch++; }
			else { active_ch++; }
		}
	}
	return passive_ch * 44u > active_ch * 56u;
}

static int
wli_scan(const struct fuxi_wifi_sta_scan *scan)
{
	if (!scan || scan->ssid_length > 32 || scan->frequency_count > 64 ||
	    (scan->passive && scan->ssid_length)) { return EINVAL; }
	if (scan->dwell_ms > 1000) { return EINVAL; }
	if (scan->dwell_ms && (!wli.dwell_enabled || !(wli.features & FWC_FEATURE_SCAN_DWELL_MS))) {
		return EOPNOTSUPP;
	}
	if (fuxi_wifi_sta_power_off_pending()) { return ECANCELED; }
	struct fuxi_wifi_sta_scan scoped;
	bool expanded_channels = false, defaulted_dwell = false;
	lck_mtx_lock(&wli_lock);
	/* Ath12kWifi's nw_sta.c scans the bands that the OS can use. Keep the
	 * effective channel list in the request for cache coverage checks. */
	if ((wli.ux & FUXI_WIFI_UX_SCAN_CHANNELS) && wli.published && !scan->frequency_count &&
	    wli_channel_count && wli_channel_count <= sizeof(scoped.frequencies) / sizeof(scoped.frequencies[0])) {
		scoped = *scan;
		scoped.frequency_count = wli_channel_count;
		for (unsigned index = 0; index < wli_channel_count; index++) {
			scoped.frequencies[index] = wli_channels[index].frequency;
		}
		scan = &scoped;
		expanded_channels = true;
	}
	uint64_t now = wla_now_ms();
	bool can_cache = wli.cache_valid && wli_bss_count && !wli.fatal && wli.published &&
	    !wli.busy && !wli.scan_draining && !wli.ap_active && !wli.sta_reset_needed &&
	    wli_state.phase != FUXI_WIFI_STA_OFF && now >= wli.scanned_at;
	bool type3_hit = can_cache && scan->cache_allowed && wli.cache_enabled &&
	    now - wli.scanned_at <= WLI_CACHE_MS && wli_scan_covers(&wli.scan, scan);
	bool reuse_hit = can_cache && (wli.ux & FUXI_WIFI_UX_SCAN_REUSE) &&
	    !scan->cache_allowed && !scan->passive && !scan->ssid_length &&
	    now - wli.scanned_at <= WLI_ACTIVE_REUSE_MS &&
	    wli_scan_covers_channels(&wli.scan, scan);
	bool cached = type3_hit || reuse_hit;
	unsigned hits = cached ? ++wli.cache_hits : 0;
	uint64_t cache_age = cached ? now - wli.scanned_at : 0;
	if (!cached && wli_should_default_dwell(scan)) {
		if (scan != &scoped) {
			scoped = *scan;
			scan = &scoped;
		}
		scoped.dwell_ms = WLI_DEFAULT_DWELL_MS;
		defaulted_dwell = true;
	}
	lck_mtx_unlock(&wli_lock);
	if (cached) {
		if (hits <= 8 || !(hits % 64)) {
			if (reuse_hit && !type3_hit) {
				printf("FUXI WIFI IOS: FAST scan uses fresh completed cache %u (active reuse, %u ch, age %llu ms)\n",
				    hits, scan->frequency_count, (unsigned long long)cache_age);
			} else {
				printf("FUXI WIFI IOS: FAST scan uses fresh completed cache %u\n", hits);
			}
		}
		return 0;
	}
	if (expanded_channels) {
		printf("FUXI WIFI IOS: native scan uses %u advertised channels\n", scan->frequency_count);
	}
	if (defaulted_dwell) {
		printf("FUXI WIFI IOS: native scan applies bounded default dwell %u ms\n", scan->dwell_ms);
	}
	return wli_submit(WLI_SCAN, false, scan, NULL);
}

/*
 * fuxiwifitest=3 (UI tests only): what wifid hears is cut down to the network named by
 * fuxiwifitestssid, so a scripted tap on the first row of the list lands on it whatever else is
 * on the air. Off (empty) by default; the radio still scans everything.
 */
static uint8_t wli_ui_only[32];
static uint8_t wli_ui_only_length;

static void
wli_heard(void (*each)(const struct fuxi_wifi_sta_bss *, void *), void *context)
{
	struct fuxi_wifi_sta_bss bss;
	for (unsigned index = 0; index < WLI_BSS_MAX; index++) {
		lck_mtx_lock(&wli_lock);
		bool present = index < wli_bss_count;
		if (present) {
			bss = wli_bss[index];
			uint64_t elapsed = wla_now_ms() >= wli.scanned_at ? wla_now_ms() - wli.scanned_at : 0;
			uint64_t age = (uint64_t)bss.age_ms + elapsed;
			bss.age_ms = age > (uint32_t)-1 ? (uint32_t)-1 : (uint32_t)age;
		}
		lck_mtx_unlock(&wli_lock);
		if (!present) { break; }
		if (wli_ui_only_length && (bss.ssid_length != wli_ui_only_length ||
		    memcmp(bss.ssid, wli_ui_only, wli_ui_only_length) != 0)) {
			continue;
		}
		each(&bss, context);
	}
}

/* The RSN element of a list of elements: what it offers, as bit masks of 00-0F-AC suite types. */
static void
wli_rsn_parse(const uint8_t *elements, size_t length, struct wli_rsn *rsn)
{
	memset(rsn, 0, sizeof(*rsn));
	for (size_t at = 0; at + 2 <= length && at + 2 + elements[at + 1] <= length; at += 2 + elements[at + 1]) {
		if (elements[at] != 48) { continue; }
		const uint8_t *body = elements + at + 2;
		size_t size = elements[at + 1], position = 6;
		rsn->present = 1;
		if (size < 6 || body[0] != 1 || body[1] != 0) { return; }
		rsn->group = body[2] == 0 && body[3] == 0x0f && body[4] == 0xac ? body[5] : 0xff;
		for (unsigned list = 0; list < 2; list++) {
			if (position + 2 > size) { return; }
			unsigned count = body[position] | (unsigned)body[position + 1] << 8;
			position += 2;
			if (count > 16 || position + 4 * count > size) { return; }
			for (unsigned index = 0; index < count; index++, position += 4) {
				const uint8_t *suite = body + position;
				if (suite[0] || suite[1] != 0x0f || suite[2] != 0xac || suite[3] >= 32) { continue; }
				if (list) { rsn->akm |= 1u << suite[3]; } else { rsn->pairwise |= 1u << suite[3]; }
			}
		}
		if (position + 2 <= size) { rsn->caps = body[position] | (unsigned)body[position + 1] << 8; }
		rsn->complete = 1;
		return;
	}
}

/* Whether a join of this kind can be made to this network; its group cipher on success. */
static int
wli_security(const struct fuxi_wifi_sta_bss *bss, int security, uint32_t *group)
{
	struct wli_rsn rsn;
	wli_rsn_parse(bss->elements, bss->elements_length, &rsn);
	*group = 0;
	if (security == FUXI_WIFI_STA_OPEN) {
		return rsn.present || (bss->capability & 0x10) ? EINVAL : 0;
	}
	if (!rsn.present) { return (bss->capability & 0x10) ? EOPNOTSUPP : EINVAL; }
	if (!rsn.complete || !(rsn.pairwise & (1u << 4)) || !(rsn.akm & (1u << 2)) ||
	    (rsn.group != 4 && rsn.group != 2) || (rsn.caps & 0x40)) { return EOPNOTSUPP; }
	*group = WLI_SUITE(rsn.group);
	return 0;
}

/*
 * fuxi,wifi-ux bit 1：估計一台 AP 能跑多快（kbps），給 wli_compare() 用。
 * 參考 wpa_supplicant 的 wpas_get_est_tpt() 與 wpa_scan_result_compar()（wpa_supplicant/scan.c，BSD 授權）：
 * 雜訊沒量到時 2.4 GHz 當 -89 dBm、5 GHz 當 -92 dBm，每個 MCS 要的 SNR 在 20 MHz 的門檻上每加倍頻寬加 3 dB，
 * 速率 = 20 MHz 單串流速率 × 頻寬倍數 × 串流數。這裡只做本機用得到的：HT／VHT／HE，20/40/80 MHz
 * （160 MHz 保守當 80），最多 2 條串流（WCN7850 是 2x2），2.4 GHz 不算 VHT；速率表是 802.11n/ac/ax 的
 * 長保護間隔（HE 0.8 µs）數值。只用來排先後，不是真的連線速率。
 */
#define WLI_NOISE_24 (-89)
#define WLI_NOISE_5 (-92)
#define WLI_GREAT_SNR 25

static const struct { int8_t snr; uint16_t ht, he; } wli_mcs[12] = {     /* 20 MHz、單串流：Mbps×10 */
	{ 2, 65, 86 }, { 5, 130, 172 }, { 9, 195, 258 }, { 11, 260, 344 }, { 15, 390, 516 }, { 18, 520, 688 },
	{ 20, 585, 774 }, { 25, 650, 860 }, { 29, 780, 1032 }, { 31, 867, 1147 }, { 34, 0, 1290 }, { 36, 0, 1434 },
};

/* 第一個這種元素的內容（id 255 時再比 ext 編號，回傳的內容不含 ext 編號）；沒有就 NULL */
static const uint8_t *
wli_element(const struct fuxi_wifi_sta_bss *bss, unsigned id, int ext, size_t *size)
{
	const uint8_t *elements = bss->elements;
	size_t length = MIN((size_t)bss->elements_length, sizeof(bss->elements));
	for (size_t at = 0; at + 2 <= length && at + 2 + elements[at + 1] <= length; at += 2 + elements[at + 1]) {
		const uint8_t *body = elements + at + 2;
		size_t body_size = elements[at + 1];
		if (elements[at] != id) { continue; }
		if (ext >= 0) {
			if (!body_size || body[0] != ext) { continue; }
			body++;
			body_size--;
		}
		*size = body_size;
		return body;
	}
	return NULL;
}

/* VHT／HE 的 MCS map：每條串流 2 bits，3 = 不支援；回傳支援的串流數 */
static unsigned
wli_streams(unsigned map)
{
	unsigned streams = 0;
	while (streams < 8 && ((map >> (2 * streams)) & 3) != 3) { streams++; }
	return streams;
}

static int
wli_snr(const struct fuxi_wifi_sta_bss *bss)
{
	return bss->rssi - (bss->frequency >= 5000 ? WLI_NOISE_5 : WLI_NOISE_24);
}

static uint32_t
wli_rate_kbps(const struct fuxi_wifi_sta_bss *bss)
{
	bool five = bss->frequency >= 5000;
	int snr = wli_snr(bss);
	size_t size;
	const uint8_t *ht = wli_element(bss, 45, -1, &size);
	if (!ht || size < 26) {
		/* 沒有 HT：只有 802.11a/g 的 OFDM（6–54 Mbps） */
		static const struct { int8_t snr; uint16_t rate; } legacy[8] = {
			{ 4, 60 }, { 5, 90 }, { 7, 120 }, { 9, 180 }, { 12, 240 }, { 16, 360 }, { 20, 480 }, { 21, 540 },
		};
		uint32_t rate = 0;
		for (unsigned index = 0; index < 8; index++) {
			if (snr >= legacy[index].snr) { rate = legacy[index].rate * 100u; }
		}
		return rate;
	}
	/* HT 能力的 Rx MCS bitmask：第 n 條串流的 MCS 8n..8n+7 在 byte 3+n */
	unsigned streams = 0, top = 7, width = 20;
	while (streams < 4 && ht[3 + streams]) { streams++; }
	/* HT 操作：次通道偏移（1 上、3 下）而且 STA 頻寬位元打開 = 40 MHz */
	const uint8_t *op = wli_element(bss, 61, -1, &size);
	if (op && size >= 22 && (op[1] & 4) && ((op[1] & 3) == 1 || (op[1] & 3) == 3)) { width = 40; }
	const uint8_t *cap = five ? wli_element(bss, 191, -1, &size) : NULL;
	if (cap && size >= 12) {
		unsigned map = fwc_read16(cap + 4), count = wli_streams(map);
		if (count) {
			streams = count;
			top = 7 + MIN(map & 3, 2u);             /* 0：MCS 0–7，1：0–8，2：0–9 */
		}
		const uint8_t *vop = wli_element(bss, 192, -1, &size);
		if (vop && size >= 5 && vop[0] >= 1) { width = 80; }  /* 1：80，2／3：160 或 80+80，這裡當 80 */
	}
	/* HE 能力（ext 35）：MAC 6 bytes、PHY 11 bytes 之後是 80 MHz 以下的 Rx HE-MCS map */
	bool he = false;
	cap = wli_element(bss, 255, 35, &size);
	if (cap && size >= 19) {
		unsigned map = fwc_read16(cap + 17), count = wli_streams(map);
		he = true;
		if (count) {
			streams = count;
			top = 7 + 2 * MIN(map & 3, 2u);         /* 0：MCS 0–7，1：0–9，2：0–11 */
		}
	}
	streams = streams ? MIN(streams, 2u) : 1u;
	top = MIN(top, he ? 11u : 9u);
	int extra = width == 80 ? 6 : width == 40 ? 3 : 0;
	unsigned factor = he ? (width == 80 ? 419 : width == 40 ? 200 : 100) : (width == 80 ? 450 : width == 40 ? 208 : 100);
	uint32_t rate = 0;
	for (unsigned index = 0; index <= top; index++) {
		unsigned base = he ? wli_mcs[index].he : wli_mcs[index].ht;
		if (base && snr >= wli_mcs[index].snr + extra) { rate = (uint32_t)base * factor * streams; }
	}
	return rate;
}

/*
 * wpa_scan_result_compar() 的順序，>0：a 比 b 好。SNR 封頂 25 dB 之後差不到 7 dB 先比估計速率，
 * 差不到 5 dB 再選 5 GHz，其餘比完整 SNR。
 */
static int
wli_compare(const struct fuxi_wifi_sta_bss *a, uint32_t rate_a, const struct fuxi_wifi_sta_bss *b, uint32_t rate_b)
{
	int full_a = wli_snr(a), full_b = wli_snr(b);
	int snr_a = MIN(full_a, WLI_GREAT_SNR), snr_b = MIN(full_b, WLI_GREAT_SNR);
	int gap = snr_a > snr_b ? snr_a - snr_b : snr_b - snr_a;
	bool five_a = a->frequency >= 5000, five_b = b->frequency >= 5000;
	if (snr_a > 0 && snr_b > 0 && gap < 7 && rate_a != rate_b) { return rate_a > rate_b ? 1 : -1; }
	if (snr_a > 0 && snr_b > 0 && gap < 5 && five_a != five_b) { return five_a ? 1 : -1; }
	return full_a - full_b;
}

struct wli_pick_note {
	unsigned candidates, fresh;
	uint32_t best_rate, strongest_rate;
	uint16_t strongest_frequency;
	int16_t strongest_rssi;
};

/*
 * bit 1：能用這種保護加入的那幾台裡挑；新鮮的（WLI_FRESH_MS 內聽到）優先，再照 wli_compare()。
 * 一台都不能加入就回 -1，照舊挑訊號最強的，讓 wli_security() 說為什麼不行。呼叫者持有 wli_lock。
 */
static int
wli_pick_rate(const struct fuxi_wifi_sta_join *join, bool given, uint64_t age, struct wli_pick_note *note)
{
	int best = -1, strongest = -1;
	bool best_fresh = false;
	uint32_t best_rate = 0;
	for (unsigned index = 0; index < wli_bss_count; index++) {
		const struct fuxi_wifi_sta_bss *bss = &wli_bss[index];
		uint32_t group;
		if (bss->ssid_length != join->ssid_length || memcmp(bss->ssid, join->ssid, join->ssid_length) ||
		    (given && memcmp(bss->bssid, join->bssid, 6)) || wli_security(bss, join->security, &group)) { continue; }
		bool fresh = age + bss->age_ms <= WLI_FRESH_MS;
		uint32_t rate = wli_rate_kbps(bss);
		note->candidates++;
		note->fresh += fresh;
		if (strongest < 0 || bss->rssi > wli_bss[strongest].rssi) {
			strongest = (int)index;
			note->strongest_rate = rate;
			note->strongest_frequency = bss->frequency;
			note->strongest_rssi = bss->rssi;
		}
		if (best < 0 || (fresh && !best_fresh) ||
		    (fresh == best_fresh && wli_compare(bss, rate, &wli_bss[best], best_rate) > 0)) {
			best = (int)index;
			best_fresh = fresh;
			best_rate = rate;
		}
	}
	note->best_rate = best_rate;
	return best;
}

/* The access point to join: the given one, or the one of that name heard best. */
static int
wli_pick(const struct fuxi_wifi_sta_join *join, struct wli_target *target)
{
	static const uint8_t none[6];
	bool given = memcmp(join->bssid, none, 6) != 0;
	struct wli_pick_note note;
	memset(&note, 0, sizeof(note));
	int best = -1;
	lck_mtx_lock(&wli_lock);
	uint64_t age = wla_now_ms() - wli.scanned_at;
	bool rated = (wli.ux & FUXI_WIFI_UX_PICK_RATE) != 0;
	if (rated) { best = wli_pick_rate(join, given, age, &note); }
	if (best < 0) {
		rated = false;
		for (unsigned index = 0; index < wli_bss_count; index++) {
			const struct fuxi_wifi_sta_bss *bss = &wli_bss[index];
			if (bss->ssid_length != join->ssid_length || memcmp(bss->ssid, join->ssid, join->ssid_length) ||
			    (given && memcmp(bss->bssid, join->bssid, 6))) { continue; }
			if (best < 0 || bss->rssi > wli_bss[best].rssi) { best = (int)index; }
		}
	}
	int error = best < 0 ? ENOENT : age + wli_bss[best].age_ms > WLI_FRESH_MS ? ESTALE : 0;
	/* (a stale one is kept too, for its channel: the rescan listens there first) */
	if (best >= 0) { target->bss = wli_bss[best]; }
	lck_mtx_unlock(&wli_lock);
	if (rated && note.candidates > 1) {
		printf("FUXI WIFI IOS: pick among %u (%u fresh): %u MHz %d dBm ~%u Mbps; strongest %u MHz %d dBm ~%u Mbps\n",
		    note.candidates, note.fresh, target->bss.frequency, target->bss.rssi, note.best_rate / 1000,
		    note.strongest_frequency, note.strongest_rssi, note.strongest_rate / 1000);
	}
	return error;
}

static void
wli_note_error(int error)
{
	lck_mtx_lock(&wli_lock);
	wli_state.last_error = error;
	lck_mtx_unlock(&wli_lock);
}

static int
wli_leave(void)
{
	lck_mtx_lock(&wli_lock);
	bool off = wli_state.phase == FUXI_WIFI_STA_OFF;
	lck_mtx_unlock(&wli_lock);
	int error = off ? 0 : wli_submit(WLI_LEAVE, false, NULL, NULL);
	if (wli_input) { wli_input->link_state(false); }
	return error;
}

/*
 * fuxi,wifi-ux bit 2：已連著的時候又要求加入，選好之後決定要不要真的換。
 * 0：留著現在的連線、回成功（選中的就是這台 AP；或是同一個網路名稱、但這次選不出能換過去的 AP：
 * 沒再聽到、太舊、重掃失敗或保護方式不支援）。-1：要先斷線再加入（換 AP，或換到別的網路）。
 */
static int
wli_kept(const struct fuxi_wifi_sta_join *join, const struct wli_target *target, int error)
{
	lck_mtx_lock(&wli_lock);
	bool joined = wli_state.phase == FUXI_WIFI_STA_JOINED;
	bool same_name = joined && wli_state.bss.ssid_length == join->ssid_length &&
	    !memcmp(wli_state.bss.ssid, join->ssid, join->ssid_length);
	bool same_ap = same_name && !error && !memcmp(wli_state.bss.bssid, target->bss.bssid, 6) &&
	    wli_state.security == join->security;
	lck_mtx_unlock(&wli_lock);
	if (same_ap) {
		printf("FUXI WIFI IOS: join asks for the access point already joined: link kept\n");
		return 0;
	}
	if (same_name && error) {
		printf("FUXI WIFI IOS: join of the network joined, no other access point to move to (%d): link kept\n", error);
		return 0;
	}
	return -1;
}

static int
wli_join(const struct fuxi_wifi_sta_join *join)
{
	if (!join || !join->ssid_length || join->ssid_length > 32 ||
	    (join->security != FUXI_WIFI_STA_OPEN && join->security != FUXI_WIFI_STA_WPA2_PSK) ||
	    (join->passphrase_length && (join->passphrase_length < 8 || join->passphrase_length > 63))) {
		wli_note_error(EINVAL);
		return EINVAL;
	}
	lck_mtx_lock(&wli_lock);
	enum fuxi_wifi_sta_phase phase = wli_state.phase;
	lck_mtx_unlock(&wli_lock);
	if (phase == FUXI_WIFI_STA_OFF) {
		wli_note_error(ENETDOWN);
		return ENETDOWN;
	}
	/* bit 2：已連著就先別斷，選好（必要時邊連邊掃）再說 */
	bool keep = (wli.ux & FUXI_WIFI_UX_KEEP_LINK) && phase == FUXI_WIFI_STA_JOINED;
	if ((phase == FUXI_WIFI_STA_JOINED || phase == FUXI_WIFI_STA_JOINING) && !keep) { (void)wli_leave(); }
	struct wli_target *target = &wli_asked;
	memset(target, 0, sizeof(*target));
	target->security = join->security;
	if (join->security == FUXI_WIFI_STA_WPA2_PSK) {
		if (join->passphrase_length) {
			fuxi_wifi_wpa_pmk(join->passphrase, join->passphrase_length, join->ssid, join->ssid_length,
			    target->pmk);
		} else {
			memcpy(target->pmk, join->pmk, sizeof(target->pmk));
		}
	}
	int error = wli_pick(join, target);
	if (error == ENOENT || error == ESTALE) {
		struct fuxi_wifi_sta_scan scan;
		memset(&scan, 0, sizeof(scan));
		memcpy(scan.ssid, join->ssid, join->ssid_length);
		scan.ssid_length = join->ssid_length;
		/* heard before but too long ago: its own channel first, a moment rather than every channel's seconds */
		if (error == ESTALE && target->bss.frequency) {
			scan.frequencies[0] = target->bss.frequency;
			scan.frequency_count = 1;
		}
		printf("FUXI WIFI IOS: join needs a fresh scan (%s%s)\n", error == ENOENT ? "not heard" : "too old",
		    scan.frequency_count ? ", its channel first" : "");
		error = wli_scan(&scan);
		if (!error) { error = wli_pick(join, target); }
		if (scan.frequency_count && (error == ENOENT || error == ESTALE)) {
			scan.frequency_count = 0;
			printf("FUXI WIFI IOS: not on its channel; wider scan\n");
			error = wli_scan(&scan);
			if (!error) { error = wli_pick(join, target); }
		}
	}
	if (error == ECANCELED) {
		memset(target, 0, sizeof(*target));
		wli_note_error(error);
		return error;
	}
	if (!error) { error = wli_security(&target->bss, join->security, &target->group); }
	if (keep) {
		if (wli_kept(join, target, error) == 0) {
			memset(target, 0, sizeof(*target));
			printf("FUXI WIFI IOS: join finished 0\n");
			return 0;
		}
		(void)wli_leave();
	}
	if (!error) {
		printf("FUXI WIFI IOS: join %s at %u MHz %d dBm\n",
		    join->security == FUXI_WIFI_STA_OPEN ? "open" : "WPA2-PSK", target->bss.frequency, target->bss.rssi);
		error = wli_submit(WLI_JOIN, false, NULL, target);
	}
	memset(target, 0, sizeof(*target));
	if (error) { wli_note_error(error); }
	printf("FUXI WIFI IOS: join finished %d\n", error);
	return error;
}

static void
wli_snapshot(struct fuxi_wifi_sta_state *state)
{
	lck_mtx_lock(&wli_lock);
	*state = wli_state;
	lck_mtx_unlock(&wli_lock);
}

static unsigned
wli_channel_snapshot(struct fuxi_wifi_sta_channel *channels, unsigned most)
{
	lck_mtx_lock(&wli_lock);
	unsigned count = MIN(most, wli_channel_count);
	memcpy(channels, wli_channels, count * sizeof(*channels));
	lck_mtx_unlock(&wli_lock);
	return count;
}

static bool
wli_tx(const uint8_t *frame, size_t length)
{
	return frame && length >= 14 && length <= 2000 && __atomic_load_n(&wli_link, __ATOMIC_ACQUIRE) &&
	       nw_wifi_tx(frame, length) == 0;
}

/* A frame from the radio for the host stack, on the worker thread: Ethernet header and payload. */
static void
wli_rx(const uint8_t *frame, size_t length)
{
	if (frame && length >= 14 && __atomic_load_n(&wli_link, __ATOMIC_ACQUIRE) && wli_input) {
		wli_input->rx_frame(frame, length);
	}
}

static const struct nw_wifi_events wli_events = { .rx = wli_rx };

static void
wli_rx_packet(void *source, size_t length, int (*read)(void *, size_t, void *, size_t))
{
	if (__atomic_load_n(&wli_link, __ATOMIC_ACQUIRE)) {
		wli_input->rx_packet(source, length, read);
	}
}

static void
wli_rx_flush(void)
{
	wli_input->rx_flush();
}

static const struct nw_wifi_events wli_batch_events = {
	.rx_packet = wli_rx_packet, .rx_flush = wli_rx_flush,
};

static void
wli_link_stats(uint64_t now)
{
	static uint64_t last_time, last_rx, last_tx;
	if (!__atomic_load_n(&wli_link, __ATOMIC_ACQUIRE)) {
		last_time = 0;
		return;
	}
	if (last_time && now - last_time < 5000) { return; }
	uint64_t rx, tx;
	if (!wli_input || !wli_input->byte_counts || !wli_input->byte_counts(&rx, &tx)) { return; }
	if (last_time && (rx != last_rx || tx != last_tx)) {
		char phy[128];
		nw_wifi_link_state(phy, sizeof(phy));
		printf("FUXI WIFI IOS: link traffic rx %llu tx %llu bytes in %llu ms; PHY %s\n",
		    (unsigned long long)(rx - last_rx), (unsigned long long)(tx - last_tx),
		    (unsigned long long)(now - last_time), phy[0] ? phy : "unavailable");
	}
	last_rx = rx;
	last_tx = tx;
	last_time = now;
}

static void
wli_start(const struct fuxi_wifi_input *input)
{
	wli_input = input;
}

/* ---- SoftAP (ap1) bridge to ath12k nw_wifi_ap_* --------------------------------------------------- */

static const struct fuxi_wifi_ap_input *wli_ap_input;
static struct fuxi_wifi_ap_ops wli_ap_ops;

static int
wli_ap_errno(int r)
{
	if (r >= 0) { return r; }
	switch (-r) {
	case 1: return EPERM;
	case 2: return ENOENT;
	case 5: return EIO;
	case 12: return ENOMEM;
	case 16: return EBUSY;
	case 19: return ENODEV;
	case 22: return EINVAL;
	case 95: return EOPNOTSUPP;
	case 100: return ENETDOWN;
	case 110: return ETIMEDOUT;
	default: return EIO;
	}
}

static void
wli_ap_start_cb(const struct fuxi_wifi_ap_input *input)
{
	wli_ap_input = input;
}

static void
wli_ap_rx_cb(const uint8_t *eth, size_t len)
{
	if (wli_ap_input && wli_ap_input->rx_frame && eth && len >= 14) {
		wli_ap_input->rx_frame(eth, len);
	}
}

static void
wli_ap_sta_arrive_cb(const uint8_t *mac, const uint8_t *rsn_ie, size_t rsn_ie_len)
{
	if (wli_ap_input && wli_ap_input->sta_arrive && mac) {
		wli_ap_input->sta_arrive(mac, rsn_ie, rsn_ie_len);
	}
}

static void
wli_ap_sta_leave_cb(const uint8_t *mac, uint16_t reason)
{
	if (wli_ap_input && wli_ap_input->sta_leave && mac) {
		wli_ap_input->sta_leave(mac, reason);
	}
}

static const struct nw_ap_events wli_ap_events = {
	.rx = wli_ap_rx_cb,
	.sta_arrive = wli_ap_sta_arrive_cb,
	.sta_leave = wli_ap_sta_leave_cb,
};

static int
wli_ap_start_op(const struct fuxi_wifi_ap_config *cfg, uint32_t *actual_freq_mhz)
{
	if (!cfg || cfg->ssid_len > 32 || cfg->extra_ie_len > 1024) { return EINVAL; }
	struct nw_ap_config nc;
	memset(&nc, 0, sizeof(nc));
	memcpy(nc.mac, cfg->mac, 6);
	memcpy(nc.ssid, cfg->ssid, cfg->ssid_len);
	nc.ssid_len = cfg->ssid_len;
	nc.hidden = cfg->hidden;
	nc.wpa2 = cfg->wpa2;
	nc.flags = cfg->flags;
	nc.country[0] = cfg->country[0];
	nc.country[1] = cfg->country[1];
	nc.freq_mhz = cfg->freq_mhz;
	nc.extra_ie_len = cfg->extra_ie_len;
	if (cfg->extra_ie_len) { memcpy(nc.extra_ie, cfg->extra_ie, cfg->extra_ie_len); }
	/* Wait for any in-flight STA leave or primary recycle to finish before starting SoftAP
	 * so ap1 creation does not race with wlan0 deauthentication or dev_close/dev_open. */
	lck_mtx_lock(&wli_lock);
	if (wli.busy && (wli.operation == WLI_LEAVE || wli.reset_stage != WLI_RESET_NONE) && !wli.fatal) {
		uint64_t wait_deadline;
		clock_interval_to_deadline(2000, NSEC_PER_MSEC, &wait_deadline);
		while (wli.busy && (wli.operation == WLI_LEAVE || wli.reset_stage != WLI_RESET_NONE) && !wli.fatal) {
			if (lck_mtx_sleep_deadline(&wli_lock, LCK_SLEEP_DEFAULT, (event_t)&wli.busy,
			    THREAD_UNINT, wait_deadline) == THREAD_TIMED_OUT) {
				break;
			}
		}
	}
	wli.ap_active = 1;
	wli.ap_started = 1;
	wli.sta_reset_needed = 1;
	wli.cache_valid = 0;
	lck_mtx_unlock(&wli_lock);
	int error = wli_ap_errno(nw_wifi_ap_start(&nc, actual_freq_mhz));
	if (error) {
		lck_mtx_lock(&wli_lock);
		wli.ap_active = 0;
		lck_mtx_unlock(&wli_lock);
	}
	return error;
}

static int
wli_ap_stop_op(void)
{
	int error = wli_ap_errno(nw_wifi_ap_stop());
	lck_mtx_lock(&wli_lock);
	bool need_reset = wli.ap_started || wli.ap_active;
	wli.ap_active = 0;
	wli.ap_started = 0;
	if (need_reset) {
		wli.sta_reset_needed = 1;
		wli.cache_valid = 0;
	}
	lck_mtx_unlock(&wli_lock);
	return error;
}

static int
wli_ap_set_key_op(const uint8_t *mac, uint8_t key_idx, uint32_t cipher,
    const uint8_t *key, size_t key_len, const uint8_t *seq, size_t seq_len, int is_delete)
{
	return wli_ap_errno(nw_wifi_ap_set_key(mac, key_idx, cipher, key, key_len, seq, seq_len, is_delete));
}

static int
wli_ap_get_key_seq_op(const uint8_t *mac, uint8_t key_idx, uint8_t seq_out[8])
{
	return wli_ap_errno(nw_wifi_ap_get_key_seq(mac, key_idx, seq_out));
}

static int
wli_ap_sta_authorize_op(const uint8_t *mac, int authorize)
{
	return wli_ap_errno(nw_wifi_ap_sta_authorize(mac, authorize));
}

static int
wli_ap_sta_deauth_op(const uint8_t *mac, uint16_t reason, int disassoc)
{
	return wli_ap_errno(nw_wifi_ap_sta_deauth(mac, reason, disassoc));
}

static bool
wli_ap_tx_op(const uint8_t *frame, size_t length)
{
	return frame && length >= 14 && length <= 2000 && nw_wifi_ap_tx(frame, length) == 0;
}

/* Elements an iOS scan record cannot do without; the rest is kept while there is room. */
static bool
wli_essential(const uint8_t *element)
{
	switch (element[0]) {
	case 0: case 1: case 3: case 5: case 7: case 11: case 45: case 48: case 50: case 61: case 127:
	case 191: case 192:
		return true;
	case 221:
		return element[1] >= 4 && element[2] == 0x00 && element[3] == 0x50 && element[4] == 0xf2 &&
		       (element[5] == 1 || element[5] == 2);
	case 255:
		return element[1] >= 1 && (element[2] == 35 || element[2] == 36);
	default:
		return false;
	}
}

/* Copy whole elements only, in their order, at most FUXI_WIFI_STA_ELEMENTS_MOST bytes. */
static unsigned
wli_trim(const uint8_t *elements, size_t length, struct fuxi_wifi_sta_bss *bss)
{
	size_t whole = 0, needed = 0, out = 0;
	unsigned dropped = 0;
	for (size_t at = 0; at + 2 <= length && at + 2 + elements[at + 1] <= length; at += 2 + elements[at + 1]) {
		whole = at + 2 + elements[at + 1];
		if (wli_essential(elements + at)) { needed += 2 + elements[at + 1]; }
	}
	size_t spare = needed < FUXI_WIFI_STA_ELEMENTS_MOST ? FUXI_WIFI_STA_ELEMENTS_MOST - needed : 0;
	for (size_t at = 0; at < whole; at += 2 + elements[at + 1]) {
		size_t size = 2 + elements[at + 1];
		if (!wli_essential(elements + at)) {
			if (size > spare) { dropped++; continue; }
			spare -= size;
		}
		if (out + size > FUXI_WIFI_STA_ELEMENTS_MOST) { dropped++; continue; }
		memcpy(bss->elements + out, elements + at, size);
		out += size;
		if (elements[at] == 0 && elements[at + 1] <= 32 && !bss->ssid_length) {
			memcpy(bss->ssid, elements + at + 2, elements[at + 1]);
			bss->ssid_length = elements[at + 1];
		}
	}
	bss->elements_length = (uint16_t)out;
	return dropped + (whole < length);
}

static void
wli_diag(const struct fuxi_wifi_sta_bss *bss, size_t heard, unsigned dropped)
{
	if (wli.scan_reports >= WLI_DIAG_SCANS || wli.diag_lines >= WLI_DIAG_LINES) { return; }
	wli.diag_lines++;
	struct wli_rsn rsn;
	wli_rsn_parse(bss->elements, bss->elements_length, &rsn);
	char name[33];
	for (unsigned index = 0; index < bss->ssid_length; index++) {
		uint8_t character = bss->ssid[index];
		name[index] = character >= 0x20 && character < 0x7f && character != '"' ? (char)character : '?';
	}
	name[bss->ssid_length] = 0;
	printf("FUXI WIFI IOS: heard %u MHz %d dBm cap %#x IE %u kept %u dropped %u RSN %u akm %#x pairwise %#x "
	    "group %u caps %#x \"%s\"\n", bss->frequency, bss->rssi, bss->capability, (unsigned)heard,
	    bss->elements_length, dropped, rsn.present, rsn.akm, rsn.pairwise, rsn.group, rsn.caps, name);
}

/* A scan heard the access point joined again: how strongly is what the status bar shows (caller holds wli_lock). */
static void
wli_heard_joined(const struct fuxi_wifi_sta_bss *bss)
{
	if (wli_state.phase == FUXI_WIFI_STA_JOINED && !memcmp(wli_state.bss.bssid, bss->bssid, 6) && bss->rssi < 0) {
		wli_state.bss.rssi = bss->rssi;
	}
}

/* The module's own (truncated) records: only when GET_BSS fails. */
static void
wli_cache_scan(void)
{
	int count = nw_wifi_bss_count();
	lck_mtx_lock(&wli_lock);
	wli_bss_count = 0;
	lck_mtx_unlock(&wli_lock);
	for (int index = 0; index < count && index < WLI_BSS_MAX; index++) {
		struct nw_bss_info source;
		if (nw_wifi_bss_get(index, &source) || source.ssid_len > 32 || source.ie_len > sizeof(source.ie) ||
		    source.freq_mhz < 2412 || source.freq_mhz > 5900) { continue; }
		struct fuxi_wifi_sta_bss *bss = &wli_fetched;
		memset(bss, 0, sizeof(*bss));
		memcpy(bss->bssid, source.bssid, 6);
		bss->frequency = (uint16_t)source.freq_mhz;
		bss->rssi = source.signal_dbm;
		bss->beacon_interval = source.beacon_int;
		bss->capability = source.caps;
		bss->age_ms = source.age_ms;
		unsigned dropped = wli_trim(source.ie, source.ie_len, bss);
		if (!bss->ssid_length) {
			memcpy(bss->ssid, source.ssid, source.ssid_len);
			bss->ssid_length = source.ssid_len;
		}
		wli_diag(bss, source.ie_len, dropped);
		lck_mtx_lock(&wli_lock);
		wli_bss[wli_bss_count++] = *bss;
		wli_heard_joined(bss);
		lck_mtx_unlock(&wli_lock);
	}
	lck_mtx_lock(&wli_lock);
	wli.scanned_at = wla_now_ms();
	wli.cache_valid = 0;
	lck_mtx_unlock(&wli_lock);
	wli.scan_reports++;
	printf("FUXI WIFI IOS: native scan complete BSS %u generation %llu (module records)\n", wli_bss_count,
	    (unsigned long long)wli.generation);
	wli_complete(0);
}

static void
wli_scan_finish(void)
{
	lck_mtx_lock(&wli_lock);
	wli.scanned_at = wla_now_ms();
	wli.cache_valid = 1;
	lck_mtx_unlock(&wli_lock);
	wli.scan_reports++;
	uint64_t done_at = wla_now_ms(), fetch_at = wli.fetch_at ? wli.fetch_at : done_at;
	printf("FUXI WIFI IOS: native scan complete BSS %u generation %llu (radio %llu ms in %u parts, fetch %llu ms)\n",
	    wli_bss_count, (unsigned long long)wli.generation,
	    (unsigned long long)(fetch_at >= wli.begun_at ? fetch_at - wli.begun_at : 0), wli.scan_parts,
	    (unsigned long long)(done_at >= fetch_at ? done_at - fetch_at : 0));
	wli_complete(0);
}

/* One GET_BSS reply: the access point with every element it sent. */
static void
wli_bss_reply(unsigned status, const uint8_t *bytes, size_t length)
{
	if (!wli.fetching) { return; }
	if (status == 8) {
		wli_scan_finish();
		return;
	}
	if (status) {
		printf("FUXI WIFI IOS: GET_BSS %u refused %u; module records instead\n", wli.fetch_index, status);
		wli.fetching = 0;
		wli_cache_scan();
		return;
	}
	size_t size, ie_size;
	const uint8_t *value, *bssid = fwc_attribute(bytes, length, 3, &size);
	const uint8_t *elements = fwc_attribute(bytes, length, 6, &ie_size);
	struct fuxi_wifi_sta_bss *bss = &wli_fetched;
	memset(bss, 0, sizeof(*bss));
	value = fwc_attribute(bytes, length, 5, &size);
	uint32_t frequency = value && size == 4 ? fwc_read32(value) : 0;
	if (bssid && elements && frequency >= 2412 && frequency <= 5900) {
		memcpy(bss->bssid, bssid, 6);
		bss->frequency = (uint16_t)frequency;
		value = fwc_attribute(bytes, length, 22, &size);
		int32_t signal = value && size == 4 ? (int32_t)fwc_read32(value) : -100;
		bss->rssi = (int16_t)(signal < -127 ? -127 : signal > 0 ? 0 : signal);
		value = fwc_attribute(bytes, length, 23, &size);
		bss->capability = value && size == 4 ? (uint16_t)fwc_read32(value) : 0;
		value = fwc_attribute(bytes, length, 24, &size);
		bss->beacon_interval = value && size == 4 ? (uint16_t)fwc_read32(value) : 100;
		value = fwc_attribute(bytes, length, 25, &size);
		bss->age_ms = value && size == 4 ? fwc_read32(value) : 0;
		unsigned dropped = wli_trim(elements, ie_size, bss);
		wli_diag(bss, ie_size, dropped);
		lck_mtx_lock(&wli_lock);
		if (wli_bss_count < WLI_BSS_MAX) { wli_bss[wli_bss_count++] = *bss; }
		wli_heard_joined(bss);
		lck_mtx_unlock(&wli_lock);
	}
	if (++wli.fetch_index >= 256 || wli_bss_count >= WLI_BSS_MAX) { wli_scan_finish(); }
}

static size_t
wli_put(uint8_t *bytes, size_t offset, unsigned attribute, const void *value, size_t size)
{
	fwc_write(bytes + offset, attribute, 2);
	fwc_write(bytes + offset + 2, size + 4, 2);
	if (size) { memcpy(bytes + offset + 4, value, size); }
	return offset + ((size + 7u) & ~(size_t)3u);
}

static void
wli_add(unsigned attribute, const void *value, size_t size)
{
	if (size > 0xfff0 || wli_out_length + ((size + 7u) & ~(size_t)3u) > sizeof(wli_out)) {
		wli_out_overflow = 1;
		return;
	}
	wli_out_length = wli_put(wli_out, wli_out_length, attribute, value, size);
}

static void
wli_put32(unsigned attribute, uint32_t value)
{
	uint8_t bytes[4];
	fwc_write(bytes, value, 4);
	wli_add(attribute, bytes, sizeof(bytes));
}

static void
wli_begin(void)
{
	memset(wli_out, 0, sizeof(wli_out));
	wli_out_length = 40;
	wli_out_overflow = 0;
}

static int
wli_send(unsigned operation)
{
	if (wli_out_overflow) { return EMSGSIZE; }
	if (wli.sequence + 1u == 0) { return EOVERFLOW; }
	uint8_t *bytes = wli_out;
	size_t length = wli_out_length;
	fwc_write(bytes, 0x3143574e, 4);
	fwc_write(bytes + 4, 1, 2);
	fwc_write(bytes + 6, 1, 2);
	fwc_write(bytes + 8, operation, 2);
	fwc_write(bytes + 12, ++wli.sequence, 4);
	fwc_write(bytes + 16, length, 4);
	fwc_write(bytes + 24, wli.session, 8);
	fwc_write(bytes + 32, wli.generation, 8);
	wli.pending_sequence = wli.sequence;
	wli.pending_operation = operation;
	int status = nw_wifi_control_receive(wli.session, length, wla_control_read, bytes);
	/* The module has its copy: keys and EAPOL do not stay here. */
	memset(wli_out, 0, length);
	if (status) { wli.pending_operation = 0; }
	return wli_status(status);
}

static int
wli_request(unsigned operation)
{
	wli_begin();
	if (operation == 2) {
		if (!wli.scan.passive) {
			uint8_t ssid[33] = {0};
			ssid[0] = wli.scan.ssid_length;
			memcpy(ssid + 1, wli.scan.ssid, ssid[0]);
			wli_add(19, ssid, sizeof(ssid));
		}
		if (wli.scan_count) {
			uint8_t frequencies[256];
			unsigned count = MIN(wli.scan_limit, wli.scan_count - wli.scan_offset);
			if (count > sizeof(frequencies) / 4) { return EINVAL; }
			for (unsigned index = 0; index < count; index++) {
				unsigned source = wli.scan_offset + index;
				unsigned frequency = wli.scan.frequency_count ? wli.scan.frequencies[source] :
				    wli_channels[source].frequency;
				fwc_write(frequencies + index * 4, frequency, 4);
			}
			wli_add(20, frequencies, count * 4);
			if (wli.scan_limit < wli.scan_count) {
				printf("FUXI WIFI IOS: native scan batch channels %u offset %u total %u\n",
				    count, wli.scan_offset, wli.scan_count);
			}
		}
		if (wli.scan.dwell_ms) { wli_put32(FWC_ATTR_SCAN_DWELL_MS, wli.scan.dwell_ms); }
		if (wli.ux & FUXI_WIFI_UX_SCAN_TRACE) {
			size_t size;
			const uint8_t *frequencies = fwc_attribute(wli_out, wli_out_length, 20, &size);
			unsigned count = frequencies ? (unsigned)(size / 4) : 0;
			printf("FUXI WIFI IOS: %llu scan submit generation %llu channels %u first %u/%u/%u MHz passive %u dwell %u ms\n",
			    (unsigned long long)wla_now_ms(), (unsigned long long)wli.generation, count,
			    count ? fwc_read32(frequencies) : 0, count > 1 ? fwc_read32(frequencies + 4) : 0,
			    count > 2 ? fwc_read32(frequencies + 8) : 0, wli.scan.passive, wli.scan.dwell_ms);
		}
	} else if (operation == 3) {
		wli_put32(21, wli.fetch_index);
	}
	return wli_send(operation);
}

/* An event of the join or the link: noted here, acted on by wli_join_step(). */
static void
wli_join_event(unsigned operation, const uint8_t *bytes, size_t length)
{
	size_t size;
	const uint8_t *value, *bssid = fwc_attribute(bytes, length, 3, &size);
	bool ours = bssid && size == 6 && !memcmp(bssid, wlj.bss.bssid, 6);
	if (wlj.step == WLJ_NONE) { return; }
	switch (operation) {
	case 0x103:
		value = fwc_attribute(bytes, length, 26, &size);
		if (wlj.step != WLJ_AUTH_WAIT || !value || size < 30 || memcmp(value + 16, wlj.bss.bssid, 6) ||
		    fwc_read16(value + 24) != 0 || fwc_read16(value + 26) != 2) { return; }
		wlj.auth_status = fwc_read16(value + 28);
		wlj.auth_seen = 1;
		return;
	case 0x104:
		if (wlj.step != WLJ_ASSOC_WAIT || (bssid && !ours)) { return; }
		value = fwc_attribute(bytes, length, 18, &size);
		wlj.assoc_status = value && size == 4 ? (int)fwc_read32(value) : 0xffff;
		wlj.assoc_seen = 1;
		return;
	case 0x105:
		if (bssid && !ours) { return; }
		value = fwc_attribute(bytes, length, 8, &size);
		wlj.lost_reason = value && size == 4 ? fwc_read32(value) : 0;
		wlj.lost = 1;
		return;
	case 0x106:
		value = fwc_attribute(bytes, length, 15, &size);
		if (wlj.step < WLJ_ASSOC_WAIT || wlj.step >= WLJ_ABORT || !ours || !value || size < 4 ||
		    size > sizeof(wlj.eapol)) { return; }
		memcpy(wlj.eapol, value, size);
		wlj.eapol_length = size;
		wlj.eapol_ready = 1;
		return;
	case 0x107:
		value = fwc_attribute(bytes, length, 41, &size);
		if (value && size == 4) {
			if (fwc_read32(value)) { wlj.eapol_tx_ack++; }
			else { wlj.eapol_tx_nack++; }
		}
		return;
	case 0x10c:
	case 0x10d:
		if ((operation == 0x10c && wlj.step == WLJ_AUTH_WAIT) ||
		    (operation == 0x10d && wlj.step == WLJ_ASSOC_WAIT)) { wlj.timed_out = 1; }
		return;
	default:
		return;
	}
}

/* A reply to a request of the join or the link (the reply's status is not OK). */
static void
wli_join_refused(unsigned operation, unsigned status)
{
	printf("FUXI WIFI IOS: request %u of join step %u refused %u\n", operation, wlj.step, status);
	if (operation == 6 || wlj.step == WLJ_NONE || wlj.step >= WLJ_ABORT) { return; }
	if (wlj.step >= WLJ_JOINED) {
		wlj.lost = 1;
		return;
	}
	if (!wlj.error) { wlj.error = status == 4 ? EAUTH : status == 8 ? ENOENT : wli_status(status); }
	wlj.step = WLJ_ABORT;
}

static int
wli_receive(void *context, uint64_t session, const void *message, size_t length)
{
	(void)context;
	if (session != wli.session || !fwc_message_valid(session, message, length)) { goto invalid; }
	const uint8_t *bytes = message;
	unsigned kind = fwc_read16(bytes + 6), operation = fwc_read16(bytes + 8);
	unsigned status = fwc_read32(bytes + 20);
	uint64_t generation = fwc_read64(bytes + 32);
	size_t size;
	const uint8_t *value;
	if (kind == 3) {
		if (fwc_read32(bytes + 12)) { goto invalid; }
		bool recover = (wli.ux & (FUXI_WIFI_UX_SCAN_RECOVER | FUXI_WIFI_UX_POWER_OFF_FIRST)) != 0;
		if (operation == 0x10a) {
			value = fwc_attribute(bytes, length, 32, &size);
			lck_mtx_lock(&wli_lock);
			unsigned reset_stage = wli.reset_stage;
			lck_mtx_unlock(&wli_lock);
			if (reset_stage == WLI_RESET_OFF_WAIT || reset_stage == WLI_RESET_ON_WAIT) {
				unsigned expected = reset_stage == WLI_RESET_ON_WAIT ? 1u : 0u;
				if (status || !value || size != 4 || fwc_read32(value) != expected ||
				    generation < wli.generation || generation - wli.generation > 1) { goto invalid; }
				bool power_changed = generation != wli.generation;
				wli.generation = generation;
				lck_mtx_lock(&wli_lock);
				if (power_changed) { wli.scan_draining = false; }
				if (reset_stage == WLI_RESET_OFF_WAIT) {
					wli.reset_stage = WLI_RESET_ON_SEND;
				} else {
					wli.reset_stage = WLI_RESET_DONE;
					wli.sta_reset_needed = 0;
				}
				lck_mtx_unlock(&wli_lock);
				return 0;
			}
			if (!wli.power_waiting || status || !value || size != 4 ||
			    fwc_read32(value) != wli.power_on || generation < wli.generation ||
			    generation - wli.generation > 1) { goto invalid; }
			bool power_changed = generation != wli.generation;
			wli.generation = generation;
			wli.handshake_timeouts = 0;
			wli_forget();
			lck_mtx_lock(&wli_lock);
			if (power_changed) { wli.scan_draining = false; }
			if (!wli.power_on) { wli.sta_reset_needed = 0; }
			wli_state.phase = wli.power_on ? FUXI_WIFI_STA_IDLE : FUXI_WIFI_STA_OFF;
			wli_bss_count = 0;
			wli.cache_valid = 0;
			lck_mtx_unlock(&wli_lock);
			if (wli_input) { wli_input->link_state(false); }
			wli_complete(0);
		} else if (operation == 0x101 || operation == 0x102) {
			if (recover && generation < wli.generation) { return 0; }
			if (generation != wli.generation) { goto invalid; }
			lck_mtx_lock(&wli_lock);
			bool draining = wli.scan_draining;
			if (draining && operation == 0x102) { wli.scan_draining = false; }
			bool scanning = wli.busy && wli.submitted && wli.operation == WLI_SCAN &&
			    !wli.fetching && wli.reset_stage == WLI_RESET_NONE;
			lck_mtx_unlock(&wli_lock);
			if (draining) { return 0; }
			if (!scanning) { goto invalid; }
			if (status && (!recover || operation == 0x101)) { wli_fail(wli_status(status)); }
			if (operation == 0x102) {
				wli.scan_done = 1;
				wli.scan_error = wli_status(status);
			}
		} else if (generation == wli.generation) {
			wli_join_event(operation, bytes, length);
		}
		return 0;
	}
	if (!wli.pending_operation || operation != wli.pending_operation ||
	    fwc_read32(bytes + 12) != wli.pending_sequence ||
	    (operation != 1 && generation != wli.generation)) { goto invalid; }
	wli.pending_operation = 0;
	if (operation == 3) {
		wli_bss_reply(status, bytes, length);
		return 0;
	}
	if (operation >= 4 && operation != 11) {
		if (status) { wli_join_refused(operation, status); }
		return 0;
	}
	if (status) {
		if (operation == 2) { wli_complete(wli_status(status)); }
		else { wli_fail(wli_status(status)); }
		return 0;
	}
	if (operation == 1) {
		value = fwc_attribute(bytes, length, 2, &size);
		if (!value || size != 6 || (value[0] & 1) ||
		    !(value[0] | value[1] | value[2] | value[3] | value[4] | value[5])) { goto invalid; }
		memcpy(wli_backend.station.address, value, 6);
		value = fwc_attribute(bytes, length, 32, &size);
		if (!value || size != 4 || fwc_read32(value) > 1) { goto invalid; }
		wli_state.phase = fwc_read32(value) ? FUXI_WIFI_STA_IDLE : FUXI_WIFI_STA_OFF;
		value = fwc_attribute(bytes, length, 28, &size);
		if (value && size != 4) { goto invalid; }
		wli.features = value ? fwc_read32(value) : 0;
		wli.generation = generation;
		wli.info = 1;
	} else if (operation == 11) {
		value = fwc_attribute(bytes, length, 31, &size);
		if (!value || !size || size % 16) { goto invalid; }
		for (size_t offset = 0; offset < size; offset += 16) {
			unsigned frequency = fwc_read32(value + offset), flags = fwc_read32(value + offset + 4);
			unsigned band = fwc_read32(value + offset + 12);
			if (band > 1 || frequency < 2412 || frequency > 5900 || (flags & 1)) { continue; }
			if (wli_channel_count >= WLI_CHANNEL_MAX) { goto invalid; }
			wli_channels[wli_channel_count++] = (struct fuxi_wifi_sta_channel){
				.frequency = (uint16_t)frequency,
				.flags = (flags & 2 ? FUXI_WIFI_STA_CHANNEL_PASSIVE : 0) |
				         (flags & 8 ? FUXI_WIFI_STA_CHANNEL_RADAR : 0)
			};
		}
		if (!wli_channel_count) { goto invalid; }
		wli.channels = 1;
	} else if (operation == 2) {
		wli.scan_accepted = 1;
	}
	return 0;
invalid:
	wli_fail(EPROTO);
	return 2;
}

static void
wli_join_begin(uint64_t now)
{
	static const uint8_t rsn[] = {
		48, 20, 1, 0, 0x00, 0x0f, 0xac, 4, 1, 0, 0x00, 0x0f, 0xac, 4, 1, 0, 0x00, 0x0f, 0xac, 2, 0, 0
	};
	memcpy(wlj.rsn, rsn, sizeof(rsn));
	wlj.rsn[7] = (uint8_t)(wlj.group & 0xff);
	memcpy(wlj.wpa.own, wli_backend.station.address, 6);
	memcpy(wlj.wpa.peer, wlj.bss.bssid, 6);
	read_random(wlj.wpa.own_nonce, sizeof(wlj.wpa.own_nonce));
	wlj.wpa.rsn = wlj.rsn;
	wlj.wpa.rsn_length = sizeof(wlj.rsn);
	wlj.deadline = now + WLI_JOIN_MS;
	wlj.step = WLJ_AUTH;
}

static void
wli_join_stop(int error, const char *why)
{
	if (!wlj.error) { wlj.error = error; }
	if (wlj.answered) {
		printf("FUXI WIFI IOS: join stopped at step %u: %s, error %d (eapol_tx ack %u nack %u)\n",
		    wlj.step, why, wlj.error, wlj.eapol_tx_ack, wlj.eapol_tx_nack);
	} else {
		printf("FUXI WIFI IOS: join stopped at step %u: %s, error %d\n", wlj.step, why, wlj.error);
	}
	if (wlj.step == WLJ_KEYS_WAIT && wlj.lost && wlj.lost_reason == 15 && wlj.answered) {
		if (++wli.handshake_timeouts >= 2) {
			wli.handshake_timeouts = 0;
			lck_mtx_lock(&wli_lock);
			wli.sta_reset_needed = 1;
			wli.cache_valid = 0;
			lck_mtx_unlock(&wli_lock);
			printf("FUXI WIFI IOS: repeated 4-way handshake timeout; scheduling primary interface recycle\n");
		}
	} else {
		wli.handshake_timeouts = 0;
	}
	wlj.step = wlj.lost ? WLJ_FAILED : WLJ_ABORT;
}

static int
wli_join_send(unsigned operation)
{
	int error = wli_send(operation);
	if (error) {
		printf("FUXI WIFI IOS: request %u of join step %u not sent, error %d\n", operation, wlj.step, error);
	}
	return error;
}

static void
wli_tx_eapol(unsigned length, bool clear)
{
	wli_begin();
	wli_add(3, wlj.bss.bssid, 6);
	wli_add(15, wlj.reply, length);
	wli_put32(40, 0x888e);
	wli_put32(16, clear);
	memset(wlj.reply, 0, sizeof(wlj.reply));
	if (wli_join_send(9) && wlj.step < WLJ_JOINED) { wli_join_stop(EIO, "EAPOL not sent"); }
}

static void
wli_set_key(bool pairwise)
{
	uint8_t sequence[6] = {0};
	wli_begin();
	if (pairwise) {
		wli_add(3, wlj.bss.bssid, 6);
		wli_put32(10, 0);
		wli_put32(11, 1);
		wli_put32(9, WLI_SUITE(4));
		wli_add(12, wlj.wpa.pairwise, 16);
	} else {
		memcpy(sequence, wlj.wpa.group_counter, sizeof(sequence));
		wli_put32(10, wlj.wpa.group_index);
		wli_put32(11, 0);
		wli_put32(9, wlj.group);
		wli_add(12, wlj.wpa.group, wlj.wpa.group_length);
	}
	wli_add(13, sequence, sizeof(sequence));
	if (wli_join_send(7)) {
		if (wlj.step < WLJ_JOINED) { wli_join_stop(EIO, "key not installed"); } else { wlj.lost = 1; }
	}
}

/* A message of the four-way or group handshake. */
static void
wli_eapol(void)
{
	unsigned length = 0;
	const char *why = "";
	wlj.eapol_ready = 0;
	int kind = fuxi_wifi_wpa_message(&wlj.wpa, wlj.eapol, (unsigned)wlj.eapol_length, wlj.reply, &length, &why);
	memset(wlj.eapol, 0, sizeof(wlj.eapol));
	if (kind == FUXI_WIFI_WPA_BAD) {
		printf("FUXI WIFI IOS: EAPOL message at step %u not answered: %s\n", wlj.step, why);
		return;
	}
	if (kind == FUXI_WIFI_WPA_NOTHING) { return; }
	if (kind == FUXI_WIFI_WPA_ANSWER && wlj.step == WLJ_KEYS_WAIT) {
		wlj.answered++;
		printf("FUXI WIFI IOS: handshake message 1 (%u), answering\n", wlj.answered);
		wli_tx_eapol(length, true);
	} else if (kind == FUXI_WIFI_WPA_KEYS && wlj.step == WLJ_KEYS_WAIT) {
		printf("FUXI WIFI IOS: handshake message 3 verified, group key %u of %u bytes\n",
		    wlj.wpa.group_index, wlj.wpa.group_length);
		wlj.step = WLJ_PAIRWISE;
		wli_tx_eapol(length, true);
	} else if (kind == FUXI_WIFI_WPA_GROUP && wlj.step == WLJ_JOINED) {
		wlj.rekeys++;
		printf("FUXI WIFI IOS: group key renewed (%u), index %u\n", wlj.rekeys, wlj.wpa.group_index);
		wlj.step = WLJ_REKEY;
		wli_tx_eapol(length, false);
	} else {
		printf("FUXI WIFI IOS: EAPOL kind %d at step %u ignored\n", kind, wlj.step);
		memset(wlj.reply, 0, sizeof(wlj.reply));
	}
}

static void
wli_link_lost(void)
{
	unsigned reason = wlj.lost_reason;
	wli_forget();
	lck_mtx_lock(&wli_lock);
	if (wli_state.phase == FUXI_WIFI_STA_JOINED) { wli_state.phase = FUXI_WIFI_STA_IDLE; }
	wli_state.last_error = ECONNRESET;
	lck_mtx_unlock(&wli_lock);
	printf("FUXI WIFI IOS: the network was lost, reason %u\n", reason);
	if (wli_input) { wli_input->link_state(false); }
	fuxi_wifi_sta_lost(ECONNRESET);
}

/* The join and the link, one request at a time; called while no request is pending. */
static void
wli_join_step(uint64_t now)
{
	if (wlj.step > WLJ_NONE && wlj.step < WLJ_UP && now >= wlj.deadline) {
		wli_join_stop(wlj.answered ? EAUTH : ETIMEDOUT, "no answer in time");
	}
	switch (wlj.step) {
	case WLJ_AUTH:
		wli_begin();
		wli_add(4, wlj.bss.ssid, wlj.bss.ssid_length);
		wli_add(3, wlj.bss.bssid, 6);
		wli_put32(5, wlj.bss.frequency);
		wli_put32(7, 0);
		wlj.step = WLJ_AUTH_WAIT;
		if (wli_join_send(4)) { wli_join_stop(EIO, "authentication not sent"); }
		return;
	case WLJ_AUTH_WAIT:
		if (wlj.auth_seen) {
			if (wlj.auth_status) {
				printf("FUXI WIFI IOS: authentication status %d\n", wlj.auth_status);
				wli_join_stop(ECONNREFUSED, "authentication refused");
			} else {
				wlj.step = WLJ_ASSOC;
			}
		} else if (wlj.timed_out || wlj.lost) {
			wli_join_stop(wlj.timed_out ? ETIMEDOUT : ECONNREFUSED, "authentication failed");
		}
		return;
	case WLJ_ASSOC: {
		bool secure = wlj.security == FUXI_WIFI_STA_WPA2_PSK;
		wli_begin();
		wli_add(4, wlj.bss.ssid, wlj.bss.ssid_length);
		wli_add(3, wlj.bss.bssid, 6);
		wli_put32(5, wlj.bss.frequency);
		if (secure) { wli_add(6, wlj.rsn, sizeof(wlj.rsn)); }
		wli_put32(34, secure ? WLI_SUITE(4) : 0);
		wli_put32(35, secure ? wlj.group : 0);
		wli_put32(36, secure ? WLI_SUITE(2) : 0);
		wli_put32(37, 0);
		wlj.step = WLJ_ASSOC_WAIT;
		if (wli_join_send(5)) { wli_join_stop(EIO, "association not sent"); }
		return;
	}
	case WLJ_ASSOC_WAIT:
		if (wlj.assoc_seen) {
			if (wlj.assoc_status) {
				printf("FUXI WIFI IOS: association status %d\n", wlj.assoc_status);
				wli_join_stop(ECONNREFUSED, "association refused");
			} else {
				printf("FUXI WIFI IOS: associated\n");
				wlj.step = wlj.security == FUXI_WIFI_STA_WPA2_PSK ? WLJ_KEYS_WAIT : WLJ_PORT;
				if (wlj.eapol_ready && wlj.step == WLJ_KEYS_WAIT) { wli_eapol(); }
			}
		} else if (wlj.timed_out || wlj.lost) {
			wli_join_stop(wlj.timed_out ? ETIMEDOUT : ECONNREFUSED, "association failed");
		}
		return;
	case WLJ_KEYS_WAIT:
		if (wlj.eapol_ready) {
			wli_eapol();
		} else if (wlj.lost) {
			printf("FUXI WIFI IOS: access point left during the handshake, reason %u\n", wlj.lost_reason);
			wli_join_stop(wlj.answered ? EAUTH : ECONNRESET, "handshake failed");
		}
		return;
	case WLJ_PAIRWISE:
		if (wlj.lost) { wli_join_stop(EAUTH, "lost before the keys"); return; }
		wlj.step = WLJ_GROUP;
		wli_set_key(true);
		return;
	case WLJ_GROUP:
		if (wlj.lost) { wli_join_stop(ECONNRESET, "lost before the group key"); return; }
		wlj.step = WLJ_PORT;
		wli_set_key(false);
		return;
	case WLJ_PORT:
		if (wlj.lost) { wli_join_stop(ECONNRESET, "lost before the port"); return; }
		wli_begin();
		wli_put32(14, 1);
		wlj.step = WLJ_OPERSTATE;
		if (wli_join_send(8)) { wli_join_stop(EIO, "port not opened"); }
		return;
	case WLJ_OPERSTATE:
		if (wlj.lost) { wli_join_stop(ECONNRESET, "lost before the link"); return; }
		wli_begin();
		wli_put32(32, 1);
		wlj.step = WLJ_UP;
		if (wli_join_send(10)) { wli_join_stop(EIO, "link not up"); }
		return;
	case WLJ_UP:
		if (wlj.lost) { wli_join_stop(ECONNRESET, "lost as the link came up"); return; }
		wlj.step = WLJ_JOINED;
		wlj.joined = 1;
		wli.handshake_timeouts = 0;
		memset(wlj.wpa.pmk, 0, sizeof(wlj.wpa.pmk));
		__atomic_store_n(&wli_link, 1, __ATOMIC_RELEASE);
		lck_mtx_lock(&wli_lock);
		wli_state.phase = FUXI_WIFI_STA_JOINED;
		wli_state.bss = wlj.bss;
		wli_state.security = wlj.security;
		lck_mtx_unlock(&wli_lock);
		printf("FUXI WIFI IOS: joined at %u MHz\n", wlj.bss.frequency);
		if (wli_input) { wli_input->link_state(true); }
		wli_complete(0);
		return;
	case WLJ_JOINED:
		if (wlj.leaving) {
			wli_begin();
			wli_add(3, wlj.bss.bssid, 6);
			wli_put32(8, 3);
			wlj.step = WLJ_LEAVE_WAIT;
			__atomic_store_n(&wli_link, 0, __ATOMIC_RELEASE);
			(void)wli_join_send(6);
		} else if (wlj.lost) {
			wli_link_lost();
		} else if (wlj.eapol_ready) {
			wli_eapol();
		}
		return;
	case WLJ_REKEY:
		wlj.step = WLJ_JOINED;
		wli_set_key(false);
		return;
	case WLJ_ABORT:
		wli_begin();
		wli_add(3, wlj.bss.bssid, 6);
		wli_put32(8, 3);
		wlj.step = WLJ_FAILED;
		(void)wli_join_send(6);
		return;
	case WLJ_FAILED: {
		int error = wlj.error ? wlj.error : EIO;
		wli_forget();
		lck_mtx_lock(&wli_lock);
		if (wli_state.phase == FUXI_WIFI_STA_JOINING) { wli_state.phase = FUXI_WIFI_STA_IDLE; }
		lck_mtx_unlock(&wli_lock);
		wli_complete(error);
		return;
	}
	case WLJ_LEAVE_WAIT:
		wli_forget();
		lck_mtx_lock(&wli_lock);
		if (wli_state.phase == FUXI_WIFI_STA_JOINED) { wli_state.phase = FUXI_WIFI_STA_IDLE; }
		lck_mtx_unlock(&wli_lock);
		printf("FUXI WIFI IOS: left the network\n");
		if (wli_input) { wli_input->link_state(false); }
		wli_complete(0);
		return;
	default:
		return;
	}
}

static int
wli_test_wait(void)
{
	uint64_t until = wla_now_ms() + 45000;
	while (fuxi_wifi_sta_busy() && wla_now_ms() < until) { wl_sleep(20); }
	return fuxi_wifi_sta_busy() ? ETIMEDOUT : 0;
}

static void
wli_test_thread(void *context, wait_result_t result)
{
	(void)context;
	(void)result;
	struct fuxi_wifi_sta_scan scan = {0};
	for (unsigned round = 0; round < 2; round++) {
		int error = fuxi_wifi_sta_ask_power(false);
		if (!error) { error = wli_test_wait(); }
		if (!error && fuxi_wifi_sta_powered()) { error = EIO; }
		printf("FUXI WIFI IOS: selftest round %u power off result %d\n", round + 1, error);
		if (error) { break; }
		error = fuxi_wifi_sta_ask_power(true);
		if (!error) { error = wli_test_wait(); }
		if (!error && !fuxi_wifi_sta_powered()) { error = EIO; }
		printf("FUXI WIFI IOS: selftest round %u power on result %d\n", round + 1, error);
		if (error) { break; }
		error = fuxi_wifi_sta_ask_scan(&scan);
		if (!error) { error = wli_test_wait(); }
		struct fuxi_wifi_sta_bss bss;
		unsigned count = 0;
		while (!error && count < WLI_BSS_MAX && fuxi_wifi_sta_heard(count, &bss)) { count++; }
		if (!count && !error) { error = EIO; }
		printf("FUXI WIFI IOS: selftest round %u station scan BSS %u result %d\n", round + 1, count, error);
		if (error) { break; }
	}
	(void)thread_terminate(current_thread());
	thread_block(THREAD_CONTINUE_NULL);
}

/*
 * fuxiwifitest=2: join the network named by fuxiwifitestssid with a made-up passphrase. The
 * radio path is proved when authentication and association succeed, message 1 is answered,
 * and the access point turns the made-up key away (EAUTH). No real key is ever used here.
 */
static struct fuxi_wifi_sta_join wli_test_join;

static void
wli_test_join_thread(void *context, wait_result_t result)
{
	(void)context;
	(void)result;
	static const char dummy[] = "fuxi-selftest-not-a-key";
	wl_sleep(15000);
	int error = wli_test_wait();
	if (!error && !fuxi_wifi_sta_powered()) {
		error = fuxi_wifi_sta_ask_power(true);
		if (!error) { error = wli_test_wait(); }
	}
	wli_test_join.security = FUXI_WIFI_STA_WPA2_PSK;
	wli_test_join.passphrase_length = sizeof(dummy) - 1;
	memcpy(wli_test_join.passphrase, dummy, sizeof(dummy) - 1);
	if (!error) { error = fuxi_wifi_sta_ask_join(&wli_test_join); }
	if (!error) { error = wli_test_wait(); }
	lck_mtx_lock(&wli_lock);
	int last = wli_state.last_error;
	unsigned phase = wli_state.phase;
	lck_mtx_unlock(&wli_lock);
	printf("FUXI WIFI IOS: selftest join with a made-up key: wait %d, join error %d, phase %u (%s)\n",
	    error, last, phase, !error && last == EAUTH ? "PASS: key refused by the access point" : "see above");
	if (phase == FUXI_WIFI_STA_JOINED) { (void)fuxi_wifi_sta_ask_leave(); }
	memset(&wli_test_join, 0, sizeof(wli_test_join));
	(void)thread_terminate(current_thread());
	thread_block(THREAD_CONTINUE_NULL);
}

static void
wli_test_start(void)
{
	int test = 0;
	PE_parse_boot_argn("fuxiwifitest", &test, sizeof(test));
	thread_t thread;
	if (test == 1) {
		if (kernel_thread_start(wli_test_thread, NULL, &thread) == KERN_SUCCESS) {
			thread_deallocate(thread);
		}
	} else if (test == 2) {
		char name[33];
		memset(name, 0, sizeof(name));
		if (!PE_parse_boot_argn("fuxiwifitestssid", name, sizeof(name) - 1) || !name[0]) {
			printf("FUXI WIFI IOS: selftest join needs fuxiwifitestssid\n");
			return;
		}
		memset(&wli_test_join, 0, sizeof(wli_test_join));
		wli_test_join.ssid_length = (uint8_t)strlen(name);
		memcpy(wli_test_join.ssid, name, wli_test_join.ssid_length);
		if (kernel_thread_start(wli_test_join_thread, NULL, &thread) == KERN_SUCCESS) {
			thread_deallocate(thread);
		}
	} else if (test == 3) {
		char name[33];
		memset(name, 0, sizeof(name));
		if (!PE_parse_boot_argn("fuxiwifitestssid", name, sizeof(name) - 1) || !name[0]) {
			printf("FUXI WIFI IOS: UI list filter needs fuxiwifitestssid\n");
			return;
		}
		wli_ui_only_length = (uint8_t)strlen(name);
		memcpy(wli_ui_only, name, wli_ui_only_length);
		printf("FUXI WIFI IOS: UI test: wifid hears only the network named by fuxiwifitestssid (%u bytes)\n",
		    wli_ui_only_length);
	}
}

static int
wli_step(uint64_t now)
{
	lck_mtx_lock(&wli_lock);
	int fatal = wli.fatal;
	lck_mtx_unlock(&wli_lock);
	if (fatal) { return fatal; }
	if (!wli.info || !wli.channels) {
		if (!wli.pending_operation) {
			wli.deadline = now + 10000;
			int error = wli_request(wli.info ? 11 : 1);
			if (error) { return error; }
		}
		return now >= wli.deadline ? ETIMEDOUT : 0;
	}
	if (!wli.published) {
		if (!fuxi_wifi_ios_native_ready()) { return 0; }
		int dwell = 0;
		PE_parse_boot_argn("fuxiwlandwell", &dwell, sizeof(dwell));
		wli.dwell_enabled = dwell == 1;
		printf("FUXI WIFI IOS: scan dwell opt-in %u module support %u\n", wli.dwell_enabled,
		    !!(wli.features & FWC_FEATURE_SCAN_DWELL_MS));
		int cache = 0;
		PE_parse_boot_argn("fuxiwlancache", &cache, sizeof(cache));
		wli.cache_enabled = cache == 1;
		printf("FUXI WIFI IOS: scan cache opt-in %u max age %u ms\n", wli.cache_enabled, WLI_CACHE_MS);
		int batch = 0;
		PE_parse_boot_argn("fuxiwlanbatch", &batch, sizeof(batch));
		wli.batch_enabled = batch == 1;
		printf("FUXI WIFI IOS: scan batch opt-in %u budget %u ms\n", wli.batch_enabled, WLI_SCAN_BUDGET_MS);
		wli.ux = fuxi_wifi_ux();
		if (wli.ux & FUXI_WIFI_UX_SCAN_RECOVER) {
			printf("FUXI WIFI IOS: native scan recovery enabled\n");
		}
		if (wli.ux & FUXI_WIFI_UX_POWER_OFF_FIRST) {
			printf("FUXI WIFI IOS: queued power off takes priority\n");
		}
		wli_backend.station.power = wli_power;
		wli_backend.station.scan = wli_scan;
		wli_backend.station.heard = wli_heard;
		wli_backend.station.join = wli_join;
		wli_backend.station.leave = wli_leave;
		wli_backend.station.state = wli_snapshot;
		wli_backend.station.channels = wli_channel_snapshot;
		wli_backend.tx_frame = wli_tx;
		wli_backend.start = wli_start;
		if (fuxi_wifi_ap_enabled()) {
			memset(&wli_ap_ops, 0, sizeof(wli_ap_ops));
			memcpy(wli_ap_ops.address, wli_backend.station.address, 6);
			wli_ap_ops.address[0] = (uint8_t)((wli_ap_ops.address[0] & 0xfeu) | 0x02u);
			wli_ap_ops.address[5] ^= 0x01u;
			wli_ap_ops.start_cb = wli_ap_start_cb;
			wli_ap_ops.ap_start = wli_ap_start_op;
			wli_ap_ops.ap_stop = wli_ap_stop_op;
			wli_ap_ops.set_key = wli_ap_set_key_op;
			wli_ap_ops.get_key_seq = wli_ap_get_key_seq_op;
			wli_ap_ops.sta_authorize = wli_ap_sta_authorize_op;
			wli_ap_ops.sta_deauth = wli_ap_sta_deauth_op;
			wli_ap_ops.tx_frame = wli_ap_tx_op;
			wli_backend.ap = &wli_ap_ops;
		} else {
			wli_backend.ap = NULL;
		}
		lck_mtx_lock(&wli_lock);
		wli.published = 1;
		lck_mtx_unlock(&wli_lock);
		if (!fuxi_wifi_ios_attach(&wli_backend)) { return ENXIO; }
		if (wli_backend.ap != NULL) { nw_wifi_ap_set_events(&wli_ap_events); }
		bool batch_rx = (wli.ux & FUXI_WIFI_UX_RX_BATCH) && wli_input &&
		    wli_input->rx_packet && wli_input->rx_flush;
		if (batch_rx) { printf("FUXI WIFI IOS: batched ath12k receive enabled\n"); }
		nw_wifi_set_events(batch_rx ? &wli_batch_events : &wli_events);
		printf("FUXI WIFI IOS: native backend published channels %u; power/scan/join ready "
		    "(open, WPA2-PSK CCMP; full scan elements)\n", wli_channel_count);
		wli_test_start();
	}
	if (wli.ux & FUXI_WIFI_UX_LINK_STATS) { wli_link_stats(now); }
	lck_mtx_lock(&wli_lock);
	bool begin = wli.busy && !wli.submitted;
	unsigned operation = wli.operation, power_on = wli.power_on;
	bool leave_now = false;
	bool start_reset = false;
	if (begin) {
		wli.submitted = 1;
		wli.deadline = now + 30000;
		wli.begun_at = now;
		wli.fetch_at = 0;
		wli.scan_parts = operation == WLI_SCAN;
		wli.scan_accepted = wli.scan_done = wli.fetching = 0;
		wli.scan_error = 0;
		if (operation == WLI_SCAN) {
			start_reset = !wli.ap_active && wli.sta_reset_needed &&
			    wli_state.phase != FUXI_WIFI_STA_OFF && wlj.step == WLJ_NONE;
			if (wli_state.phase == FUXI_WIFI_STA_IDLE) { wli_state.phase = FUXI_WIFI_STA_SCANNING; }
			wli_bss_count = 0;
			wli.cache_valid = 0;
		} else if (operation == WLI_JOIN) {
			start_reset = !wli.ap_active && wli.sta_reset_needed &&
			    wli_state.phase != FUXI_WIFI_STA_OFF;
			memset(&wlj, 0, sizeof(wlj));
			wlj.bss = wli_target.bss;
			memcpy(wlj.wpa.pmk, wli_target.pmk, sizeof(wlj.wpa.pmk));
			wlj.security = (unsigned)wli_target.security;
			wlj.group = wli_target.group;
			memset(&wli_target, 0, sizeof(wli_target));
			wli_state.phase = FUXI_WIFI_STA_JOINING;
			wli_state.bss = wlj.bss;
			wli_state.security = (int)wlj.security;
			wli_state.last_error = 0;
		} else if (operation == WLI_LEAVE) {
			leave_now = wlj.step != WLJ_JOINED && wlj.step != WLJ_REKEY;
			wlj.leaving = 1;
		}
		if (start_reset) { wli.reset_stage = WLI_RESET_OFF_WAIT; }
	}
	unsigned reset_stage = wli.reset_stage;
	bool busy = wli.busy;
	lck_mtx_unlock(&wli_lock);
	if (begin) {
		if (start_reset) {
			printf("FUXI WIFI IOS: recycling primary STA interface before op %u\n", operation);
			nw_wifi_power(0);
			return now >= wli.deadline ? ETIMEDOUT : 0;
		} else if (operation == WLI_POWER) {
			wli.power_waiting = 1;
			nw_wifi_power(power_on);
		} else if (operation == WLI_SCAN) {
			int error = wli_request(2);
			if (error) { return error; }
		} else if (operation == WLI_JOIN) {
			wli_join_begin(now);
		} else if (leave_now) {
			wli_forget();
			wli_complete(0);
			busy = false;
		}
	} else if (reset_stage != WLI_RESET_NONE) {
		if (reset_stage == WLI_RESET_ON_SEND) {
			lck_mtx_lock(&wli_lock);
			wli.reset_stage = WLI_RESET_ON_WAIT;
			lck_mtx_unlock(&wli_lock);
			nw_wifi_power(1);
			return now >= wli.deadline ? ETIMEDOUT : 0;
		}
		if (reset_stage == WLI_RESET_DONE) {
			lck_mtx_lock(&wli_lock);
			wli.reset_stage = WLI_RESET_NONE;
			lck_mtx_unlock(&wli_lock);
			thread_wakeup((event_t)&wli.busy);
			printf("FUXI WIFI IOS: primary STA interface recycled (generation %llu)\n",
			    (unsigned long long)wli.generation);
			if (operation == WLI_SCAN) {
				if (fuxi_wifi_sta_power_off_pending()) {
					wli_complete(ECANCELED);
					return 0;
				}
				int error = wli_request(2);
				if (error) { return error; }
			} else if (operation == WLI_JOIN) {
				wli_join_begin(now);
			}
		} else {
			return now >= wli.deadline ? ETIMEDOUT : 0;
		}
	}
	if (busy && operation == WLI_SCAN && wli.scan_accepted && !wli.fetching &&
	    (wli.ux & (FUXI_WIFI_UX_SCAN_RECOVER | FUXI_WIFI_UX_POWER_OFF_FIRST))) {
		int scan_error = wli.scan_error;
		if (!scan_error && (fuxi_wifi_sta_power_off_pending() ||
		    ((wli.ux & FUXI_WIFI_UX_POWER_ASKED) && !fuxi_wifi_sta_powered()))) {
			scan_error = ECANCELED;
		}
		if (!scan_error && now >= wli.deadline) { scan_error = ETIMEDOUT; }
		if (scan_error) {
			/* SCAN_DONE has no request ID. Until it arrives or power changes the
			 * generation, a new scan could mistake that event for its own. */
			lck_mtx_lock(&wli_lock);
			wli.scan_draining = !wli.scan_done;
			lck_mtx_unlock(&wli_lock);
			printf("FUXI WIFI IOS: native scan ended %d after %llu ms in part %u, waiting for completion %u\n",
			    scan_error, (unsigned long long)(now - wli.begun_at), wli.scan_parts, !wli.scan_done);
			wli_complete(scan_error);
			return 0;
		}
	}
	if (busy && operation == WLI_SCAN && wli.scan_accepted && wli.scan_done && !wli.fetching) {
		if (wli.scan_offset + wli.scan_limit < wli.scan_count) {
			wli.scan_offset += wli.scan_limit;
			wli.scan_accepted = wli.scan_done = 0;
			wli.scan_error = 0;
			wli.scan_parts++;
			int error = wli_request(2);
			if (error) { return error; }
			return now >= wli.deadline ? ETIMEDOUT : 0;
		}
		wli.fetching = 1;
		wli.fetch_index = 0;
		wli.fetch_at = now;
		wli.diag_lines = 0;
	}
	if (!wli.pending_operation) {
		if (wli.fetching) {
			if (fuxi_wifi_sta_power_off_pending()) {
				lck_mtx_lock(&wli_lock);
				wli_bss_count = 0;
				lck_mtx_unlock(&wli_lock);
				wli_complete(ECANCELED);
				return 0;
			}
			int error = wli_request(3);
			if (error) {
				printf("FUXI WIFI IOS: GET_BSS not sent, error %d; module records instead\n", error);
				wli.fetching = 0;
				wli_cache_scan();
			}
		} else if (wlj.step != WLJ_NONE) {
			if (busy && operation == WLI_JOIN && wlj.step < WLJ_JOINED &&
			    fuxi_wifi_sta_power_off_pending()) {
				wli_join_stop(ECANCELED, "power off queued");
			}
			wli_join_step(now);
		}
	}
	lck_mtx_lock(&wli_lock);
	busy = wli.busy;
	lck_mtx_unlock(&wli_lock);
	if (busy && now >= wli.deadline) { return ETIMEDOUT; }
	return 0;
}
