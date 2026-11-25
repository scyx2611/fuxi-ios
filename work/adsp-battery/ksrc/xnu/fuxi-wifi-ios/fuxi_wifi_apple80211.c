/*
 * Ported from HackiOS xnu/files/bsd/dev/redwood_apple80211.c (APSL-2.0)
 * Fuxi adaptations, 2026-10-23: opt-in iOS shim; see docs-lanes/wifi-ios.md.
 * Original file commentary is retained below. See LICENSE.HackiOS.
 */
/*
 * Darwin-on-redwood: Apple80211, answered in the kernel.
 *
 * iOS's Wi-Fi side -- wifid, and through it Settings and the status bar -- talks to a Wi-Fi
 * driver in requests that were once ioctls and are, where the driver offers no user client,
 * ioctls still: SIOCGA80211 and SIOCSA80211 on any socket, with a struct apple80211req that
 * names the interface, a request type, a value, and a buffer in the caller's memory
 * (IO80211.framework: Apple80211Get/Set and their kin all end there). XNU hands an interface
 * ioctl it does not know to the interface, so they arrive in bsd/dev/fuxi_wifi_net.c, which
 * passes them here when its link is a radio.
 *
 * What is asked is answered from the station boundary (sys/fuxi_wifi_sta.h): the radio's power,
 * its channels, a scan and what it heard, the network joined. What happens there by itself is
 * told to iOS the way a driver tells it: kernel events of class 6 (IEEE 802.11), subclass 1,
 * whose code is Apple80211's own event number and whose data begins with the interface's name
 * in 16 bytes (IO80211.framework reads them from a PF_SYSTEM socket and compares the name).
 *
 * Every request is still written to the log -- who asks, get or set, the type, the value, the
 * length -- because which of them wifid needs is found that way. Never what a request carries:
 * a network's name or its key may be in there.
 *
 * The request types and the layouts are Apple's. The old ones are as the headers of its open
 * IO80211 sources once described them (apple80211_ioctl.h, apple80211_var.h); where iOS 16.5
 * differs the layout was read out of IO80211.framework of 16.5, which fills and reads the same
 * structures on the other side (Apple80211GetWithIOCTL, _getScanData, _addScanResultToList,
 * _performAssociation, _dispatchAsyncAssocEvent, _dispatchLinkChangedEvent).
 *
 * A join: ASSOCIATE (20) carries the network's name, perhaps its access point, and the
 * passphrase as typed (the framework does not make the key of it), and is answered at once;
 * when the station has joined or failed, event 9 says so, and ASSOCIATE_RESULT (21) gives the
 * outcome as one of Apple80211's result numbers, which the framework turns into its errors:
 * 1 success, 21 "invalid PMK" (a wrong passphrase), 9 a timeout, 2 anything else.
 */
#if defined(FUXI)
#include <kern/clock.h>
#include <kern/kalloc.h>
#include <kern/locks.h>
#include <kern/sched_prim.h>
#include <kern/thread.h>
#include <kern/thread_call.h>
#include <libkern/libkern.h>
#include <pexpert/device_tree.h>
#include <pexpert/pexpert.h>
#include <sys/errno.h>
#include <sys/kern_event.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/fuxi_wifi_net.h>
#include <sys/fuxi_wifi_sta.h>
#include <sys/fuxi_wifi_ios.h>
#include <sys/sockio.h>
#include <sys/systm.h>

#include <sys/fuxi_wifi_apple80211.h>

/* (as far as they have come by: the names help reading the log) */
static const char *
type_name(int32_t type)
{
	static const struct {
		int32_t type;
		const char *name;
	} names[] = {
		{ 1, "SSID" }, { 2, "AUTH_TYPE" }, { 3, "CIPHER_KEY" }, { 4, "CHANNEL" }, { 5, "POWERSAVE" }, { 6, "PROTMODE" },
		{ 7, "TXPOWER" }, { 8, "RATE" }, { 9, "BSSID" }, { 10, "SCAN_REQ" }, { 11, "SCAN_RESULT" }, { 12, "CARD_CAPABILITIES" },
		{ 13, "STATE" }, { 14, "PHY_MODE" }, { 15, "OP_MODE" }, { 16, "RSSI" }, { 17, "NOISE" }, { 18, "INT_MIT" }, { 19, "POWER" },
		{ 20, "ASSOCIATE" }, { 21, "ASSOCIATE_RESULT" }, { 22, "DISASSOCIATE" }, { 23, "STATUS_DEV_NAME" }, { 24, "IBSS_MODE" },
		{ 25, "HOST_AP_MODE" }, { 26, "AP_MODE" }, { 27, "SUPPORTED_CHANNELS" }, { 28, "LOCALE" }, { 29, "DEAUTH" },
		{ 30, "COUNTERMEASURES" }, { 32, "RATE_SET" }, { 42, "STATION_LIST" }, { 43, "DRIVER_VERSION" },
		{ 44, "HARDWARE_VERSION" }, { 46, "RSN_IE" }, { 47, "BACKGROUND_SCAN" }, { 48, "AP_IE_LIST" }, { 49, "STATS" },
		{ 50, "ASSOCIATION_STATUS" }, { 51, "COUNTRY_CODE" }, { 54, "RADIO_INFO" }, { 57, "MCS" }, { 66, "MCS_INDEX_SET" },
		{ 73, "STA_IE_LIST" }, { 74, "STA_AUTHORIZE" }, { 75, "STA_DISASSOCIATE" }, { 76, "STA_DEAUTH" },
		{ 77, "RSN_CONF" }, { 78, "KEY_RSC" },
		{ 79, "STA_STATS" }, { 80, "ROAM_THRESH" }, { 85, "IE" }, { 86, "SCAN_REQ_MULTIPLE" }, { 87, "BTCOEX_MODE" },
		{ 90, "SCANCACHE_CLEAR" }, { 94, "VIRTUAL_IF_CREATE" }, { 95, "VIRTUAL_IF_DELETE" },
		{ 96, "VIRTUAL_IF_ROLE" }, { 97, "VIRTUAL_IF_PARENT" }, { 103, "CURRENT_NETWORK" },
		{ 106, "RSSI_BOUNDS" }, { 107, "ROAM" }, { 207, "CHANNELS_INFO" }, { 254, "HW_SUPPORTED_CHANNELS" },
	};

	for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		if (names[i].type == type) {
			return names[i].name;
		}
	}
	return "?";
}

/* Apple80211's events (APPLE80211_M_...) */
enum { EVENT_POWER_CHANGED = 1, EVENT_SSID_CHANGED = 2, EVENT_BSSID_CHANGED = 3, EVENT_LINK_CHANGED = 4,
       EVENT_MODE_CHANGED = 8, EVENT_ASSOC_DONE = 9, EVENT_SCAN_DONE = 10,
       EVENT_STA_ARRIVE = 12, EVENT_STA_LEAVE = 13, EVENT_HOST_AP_MODE_START = 30, EVENT_RSSI_CHANGED = 39 };

/*
 * The radio does not measure its noise floor for us; ath12k's own default stands in for it, so that
 * a signal-to-noise ratio can be given wherever iOS asks for one.
 */
#define NOISE_FLOOR             (-95)
#define RSSI_EVERY_SECONDS      5       /* how often the network joined is said to be heard so strongly */

static const char interface_name[16] = "en0";
static const char ap_interface_name[16] = "ap1";

static LCK_GRP_DECLARE(apple80211_lock_group, "fuxi-wifi-apple80211");
static LCK_MTX_DECLARE(apple80211_lock, &apple80211_lock_group);
static unsigned result_next;                    /* which of what the last scan heard a SCAN_RESULT request gets next */
static uint32_t join_result;                    /* of the last join asked for: 0 while it runs, then Apple80211's number */
/* fuxi,wifi-ux bit 3：wifid 設過的國家碼（ISO 兩字母＋第三字元），只記不用 */
static uint8_t country_code[3];
static bool country_known;

/* ---- channels ---------------------------------------------------------------------------------------- */

static unsigned
channel_number(unsigned frequency)
{
	return frequency == 2484 ? 14 : frequency < 4000 ? (frequency - 2407) / 5 : (frequency - 5000) / 5;
}

static unsigned
channel_frequency(const struct apple80211_channel *channel)
{
	bool low = channel->flags & CHANNEL_2GHZ ? true : channel->flags & CHANNEL_5GHZ ? false : channel->channel <= 14;

	if (channel->channel == 0 || channel->channel > 196 || (channel->flags & 0x2000)) {     /* (0x2000: 6 GHz) */
		return 0;
	}
	return low ? (channel->channel == 14 ? 2484 : 2407 + 5 * channel->channel) : 5000 + 5 * channel->channel;
}

static void
channel_fill(struct apple80211_channel *channel, unsigned frequency, unsigned flags)
{
	channel->version = 1;
	channel->channel = channel_number(frequency);
	channel->flags = CHANNEL_20MHZ | (frequency < 4000 ? CHANNEL_2GHZ : CHANNEL_5GHZ) | (flags & FUXI_WIFI_STA_CHANNEL_PASSIVE ?
	    0 : CHANNEL_ACTIVE) | (flags & FUXI_WIFI_STA_CHANNEL_RADAR ? CHANNEL_DFS : 0);
}

/* ---- what the caller is given back ------------------------------------------------------------------- */

/* At most as many bytes as it has room for. */
static int
give(const struct apple80211req *request, const void *data, size_t length)
{
	return request->req_data == USER_ADDR_NULL ? EFAULT : copyout(data, request->req_data, length < request->req_len ? length :
	       request->req_len);
}

/* An access point in the form a scan's result has; the caller has said how much room for elements there is. */
static int
give_network(const struct apple80211req *request, const struct fuxi_wifi_sta_bss *bss)
{
	struct apple80211_scan_result *result = kalloc_data(sizeof(*result), Z_WAITOK | Z_ZERO);
	unsigned room = sizeof(result->elements);
	int error;

	if (bss->ssid_length > sizeof(bss->ssid) || bss->elements_length > sizeof(bss->elements)) {
		if (result != NULL) {
			kfree_data(result, sizeof(*result));
		}
		return EINVAL;
	}
	if (result == NULL) {
		return ENOMEM;
	}
	if (request->req_len >= sizeof(*result) && copyin(request->req_data, result, offsetof(struct apple80211_scan_result,
	    elements)) == 0 && result->elements_length < room) {
		room = result->elements_length;
	}
	bzero(result, sizeof(*result));
	result->version = 1;
	channel_fill(&result->channel, bss->frequency, 0);
	result->rssi = bss->rssi;
	result->noise_known = 1;
	result->noise = NOISE_FLOOR;
	result->snr = (int16_t)(bss->rssi - NOISE_FLOOR);
	result->beacon_interval = bss->beacon_interval;
	result->capability = bss->capability;
	memcpy(result->bssid, bss->bssid, sizeof(result->bssid));
	result->ssid_length = bss->ssid_length;
	memcpy(result->ssid, bss->ssid, bss->ssid_length);
	result->age_ms = bss->age_ms;
	result->elements_length = (uint16_t)(bss->elements_length <= room ? bss->elements_length : 0);
	memcpy(result->elements, bss->elements, result->elements_length);
	/* the rates it says it has (elements 1 and 50: units of 500 kbit/s, the top bit marks the ones everybody must have) */
	for (unsigned at = 0; at + 2 <= bss->elements_length && at + 2 + bss->elements[at + 1] <= bss->elements_length;
	    at += 2 + bss->elements[at + 1]) {
		for (unsigned i = 0; (bss->elements[at] == 1 || bss->elements[at] == 50) && i < bss->elements[at + 1]; i++) {
			unsigned rate = (bss->elements[at + 2 + i] & 0x7f) / 2;

			if (rate != 0 && rate <= 54 && result->rate_count < 15) {
				result->rates[result->rate_count++] = rate;
			}
		}
	}
	error = give(request, result, sizeof(*result));
	kfree_data(result, sizeof(*result));
	return error;
}

/* ---- get ---------------------------------------------------------------------------------------------- */

/*
 * RSSI_BOUNDS (106), opt-in with ADT /chosen u32 "fuxi,wifi-rssi" = 1 (the command line is full). IO80211 of 16.5
 * asks it with 16 bytes, version 1 first, and turns the three int32 after it into RSSI_MIN, RSSI_MAX and
 * RSSI_THRESHOLD; wifid keeps RSSI_THRESHOLD as WiFiDeviceManager's legacy join floor (+0x80, compared against
 * every candidate before a join). Refused, that floor stays 0 and every negative-dBm network is "Below minimum
 * RSSI threshold": no automatic join, ever. ath12k reports the signal as a signed 8-bit dBm, so the bounds are
 * the limits of that format and the floor is the lowest of them: wifid's own quality policy (its -80/-90 dBm
 * auto-join passes, the moving-network floors) is left as it is. The same fix as UniXNU's zizhan port (its
 * device record: g331 automatic association with stock wifid); the layout was read from IO80211 20H392.
 */
static bool
rssi_bounds_enabled(void)
{
	DTEntry chosen;
	void const *prop;
	unsigned int bytes;
	uint32_t value = 0;

	if (SecureDTLookupEntry(NULL, "/chosen", &chosen) == kSuccess &&
	    SecureDTGetProperty(chosen, "fuxi,wifi-rssi", &prop, &bytes) == kSuccess && bytes == sizeof(value)) {
		memcpy(&value, prop, sizeof(value));
	}
	return value == 1;
}

/* fuxi,wifi-ux 讀過之後的值；bit 31 表示已讀過（測試可清 0 重讀） */
static uint32_t wifi_ux_cached;

unsigned
fuxi_wifi_ux(void)
{
	uint32_t value = __atomic_load_n(&wifi_ux_cached, __ATOMIC_ACQUIRE);

	if (!(value & 0x80000000u)) {
		DTEntry chosen;
		void const *prop;
		unsigned int bytes;
		uint32_t read = 0;

		if (SecureDTLookupEntry(NULL, "/chosen", &chosen) == kSuccess &&
		    SecureDTGetProperty(chosen, "fuxi,wifi-ux", &prop, &bytes) == kSuccess && bytes == sizeof(read)) {
			memcpy(&read, prop, sizeof(read));
		}
		value = (read & FUXI_WIFI_UX_ALL) | 0x80000000u;
		__atomic_store_n(&wifi_ux_cached, value, __ATOMIC_RELEASE);
		printf("FUXI WIFI IOS: ux options 0x%x (power-asked %u, pick-rate %u, keep-link %u, country %u, power-same %u)\n",
		    value & FUXI_WIFI_UX_ALL, !!(value & FUXI_WIFI_UX_POWER_ASKED), !!(value & FUXI_WIFI_UX_PICK_RATE),
		    !!(value & FUXI_WIFI_UX_KEEP_LINK), !!(value & FUXI_WIFI_UX_COUNTRY), !!(value & FUXI_WIFI_UX_POWER_SAME));
		printf("FUXI WIFI IOS: ux scan options (reuse %u, dwell110 %u)\n",
		    !!(value & FUXI_WIFI_UX_SCAN_REUSE), !!(value & FUXI_WIFI_UX_SCAN_DWELL_110));
	}
	return value & FUXI_WIFI_UX_ALL;
}

/* fuxi,wifi-ap ADT /chosen u32 opt-in bitmask (0 = disabled by default). */
static uint32_t wifi_ap_cached;

unsigned
fuxi_wifi_ap_options(void)
{
	uint32_t value = __atomic_load_n(&wifi_ap_cached, __ATOMIC_ACQUIRE);

	if (!(value & 0x80000000u)) {
		DTEntry chosen;
		void const *prop;
		unsigned int bytes;
		uint32_t read = 0;

		if (SecureDTLookupEntry(NULL, "/chosen", &chosen) == kSuccess &&
		    SecureDTGetProperty(chosen, "fuxi,wifi-ap", &prop, &bytes) == kSuccess && bytes == sizeof(read)) {
			memcpy(&read, prop, sizeof(read));
		}
		value = (read & FUXI_WIFI_AP_ALL) | 0x80000000u;
		__atomic_store_n(&wifi_ap_cached, value, __ATOMIC_RELEASE);
		if (value & FUXI_WIFI_AP_ALL) {
			printf("FUXI WIFI IOS: ap options 0x%x (enable %u, verbose %u, he %u, prefer-5g %u)\n",
			    value & FUXI_WIFI_AP_ALL, !!(value & FUXI_WIFI_AP_ENABLE), !!(value & FUXI_WIFI_AP_VERBOSE),
			    !!(value & FUXI_WIFI_AP_HE), !!(value & FUXI_WIFI_AP_PREFER_5GHZ));
		}
	}
	return value & FUXI_WIFI_AP_ALL;
}

bool
fuxi_wifi_ap_enabled(void)
{
	return (fuxi_wifi_ap_options() & FUXI_WIFI_AP_ENABLE) != 0;
}

/* ---- SoftAP (ap1) state & client table --------------------------------------------------------------- */

#define FUXI_AP_MAX_STAS 16

struct fuxi_ap_sta_rec {
	bool     used;
	bool     authorized;
	bool     joined_before_bridge;
	uint8_t  mac[6];
	uint16_t rsn_ie_len;
	uint8_t  rsn_ie[258];
};

static const struct fuxi_wifi_ap_ops *ap_ops;
static bool     ap_active;
static uint8_t  ap_mac[6];
static uint8_t  ap_ssid[32];
static uint8_t  ap_ssid_len;
static uint32_t ap_freq_mhz;
static uint16_t ap_mode;
static uint16_t ap_auth_lower;
static uint16_t ap_auth_upper;
static struct fuxi_ap_sta_rec ap_stas[FUXI_AP_MAX_STAS];
static bool ap_bridge_ready;
static bool ap_bridge_pending;
static thread_call_t ap_sta_replay_call;
static bool ap_hostapd_active;
static uint64_t ap_hostapd_teardown_ns;
#define AP_STA_REPLAY_MS            350u
#define AP_HOSTAPD_EXIT_WAIT_MS     600u
#define AP_HOSTAPD_EXIT_SETTLE_MS   120u

static int post_if(const char *ifname, uint32_t code, const void *data, uint32_t length);
static int post(uint32_t code, const void *data, uint32_t length);

static uint8_t
ap_authorized_count_locked(void)
{
	uint8_t n = 0;
	for (unsigned i = 0; i < FUXI_AP_MAX_STAS; i++) {
		if (ap_stas[i].used && ap_stas[i].authorized) {
			n++;
		}
	}
	return n;
}

static uint8_t
ap_used_count_locked(void)
{
	uint8_t n = 0;
	for (unsigned i = 0; i < FUXI_AP_MAX_STAS; i++) {
		if (ap_stas[i].used) {
			n++;
		}
	}
	return n;
}

/*
 * Notify wifid on en0 about SoftAP client arrival/departure (iOS 16.7.16 20H392 wifid
 * __WiFiDeviceProcessSTAArriveEvent 0x10005cb64 / __WiFiDeviceProcessSTALeaveEvent 0x10005cc38):
 * byte +0..+5 = client MAC, byte +8 = associated client count (staNum), byte +12..+15 = flags.
 * wifid forwards staNum to misd via NETRBClientSetHostCount to drive the hotspot status bar pill.
 */
static void
ap_post_en0_sta_event(uint32_t code, const uint8_t *mac, uint8_t sta_num)
{
	uint8_t payload[32] = { 0 };
	uint32_t len = code == EVENT_STA_LEAVE ? 16u : 32u;

	if (mac != NULL) {
		memcpy(payload, mac, 6);
	}
	payload[8] = sta_num;
	int err = post(code, payload, len);
	printf("FUXI WIFI IOS: en0 %s %02x:%02x:%02x:%02x:%02x:%02x staNum=%u posted=%d\n",
	    code == EVENT_STA_ARRIVE ? "STA_ARRIVE" : "STA_LEAVE",
	    payload[0], payload[1], payload[2], payload[3], payload[4], payload[5], sta_num, err);
}

void
fuxi_wifi_apple80211_ap_attach(const struct fuxi_wifi_ap_ops *ops)
{
	lck_mtx_lock(&apple80211_lock);
	ap_ops = ops;
	if (ops != NULL) {
		memcpy(ap_mac, ops->address, sizeof(ap_mac));
	}
	ap_active = false;
	ap_bridge_ready = false;
	ap_bridge_pending = false;
	ap_hostapd_active = false;
	ap_hostapd_teardown_ns = 0;
	ap_ssid_len = 0;
	ap_freq_mhz = 0;
	bzero(ap_stas, sizeof(ap_stas));
	lck_mtx_unlock(&apple80211_lock);
}

static void
ap_wait_prev_hostapd_exit(void)
{
	uint64_t start_ns = 0, now_ns = 0;
	absolutetime_to_nanoseconds(mach_absolute_time(), &start_ns);
	bool waited = false;

	lck_mtx_lock(&apple80211_lock);
	if (ap_hostapd_active) {
		waited = true;
		uint64_t deadline = 0;
		clock_interval_to_deadline(AP_HOSTAPD_EXIT_WAIT_MS, 1000000u, &deadline);
		while (ap_hostapd_active) {
			wait_result_t wr = lck_mtx_sleep_deadline(&apple80211_lock, LCK_SLEEP_DEFAULT,
			    (event_t)&ap_hostapd_active, THREAD_ABORTSAFE, deadline);
			if (wr == THREAD_TIMED_OUT || wr == THREAD_INTERRUPTED) {
				ap_hostapd_active = false;
				break;
			}
		}
	}
	uint64_t td_ns = ap_hostapd_teardown_ns;
	ap_hostapd_teardown_ns = 0;
	if (td_ns != 0) {
		absolutetime_to_nanoseconds(mach_absolute_time(), &now_ns);
		uint64_t elapsed_ms = now_ns > td_ns ? (now_ns - td_ns) / 1000000ull : 0;
		if (elapsed_ms < AP_HOSTAPD_EXIT_SETTLE_MS) {
			uint32_t rem_ms = (uint32_t)(AP_HOSTAPD_EXIT_SETTLE_MS - elapsed_ms);
			uint64_t settle_dl = 0;
			clock_interval_to_deadline(rem_ms, 1000000u, &settle_dl);
			waited = true;
			(void)lck_mtx_sleep_deadline(&apple80211_lock, LCK_SLEEP_DEFAULT,
			    (event_t)&ap_hostapd_teardown_ns, THREAD_ABORTSAFE, settle_dl);
		}
	}
	lck_mtx_unlock(&apple80211_lock);

	if (waited) {
		absolutetime_to_nanoseconds(mach_absolute_time(), &now_ns);
		uint64_t total_ms = now_ns > start_ns ? (now_ns - start_ns) / 1000000ull : 0;
		printf("FUXI WIFI IOS: ap1 waited %llu ms for previous hostapd exit before SoftAP start\n",
		    (unsigned long long)total_ms);
	}
}

static void
ap_sta_replay_tick(thread_call_param_t param0 __unused, thread_call_param_t param1 __unused)
{
	uint8_t macs[FUXI_AP_MAX_STAS][6];
	uint8_t count = 0;
	const struct fuxi_wifi_ap_ops *ops = NULL;
	lck_mtx_lock(&apple80211_lock);
	if (ap_active && ap_bridge_pending) {
		ap_bridge_pending = false;
		ap_bridge_ready = true;
		ops = ap_ops;
		for (unsigned i = 0; i < FUXI_AP_MAX_STAS; i++) {
			if (ap_stas[i].used && ap_stas[i].joined_before_bridge) {
				ap_stas[i].joined_before_bridge = false;
				memcpy(macs[count++], ap_stas[i].mac, 6);
			}
		}
	}
	lck_mtx_unlock(&apple80211_lock);
	for (uint8_t i = 0; i < count; i++) {
		printf("FUXI WIFI IOS: ap1 disassoc early STA %02x:%02x:%02x:%02x:%02x:%02x after bridge ready to trigger DHCP\n",
		    macs[i][0], macs[i][1], macs[i][2], macs[i][3], macs[i][4], macs[i][5]);
		if (ops != NULL && ops->sta_deauth != NULL) {
			(void)ops->sta_deauth(macs[i], 4u, 1);
		}
		fuxi_wifi_apple80211_ap_sta_leave(macs[i], 4u);
	}
}

void
fuxi_wifi_apple80211_ap_bridge_ready(bool ready)
{
	thread_call_t call = NULL;
	thread_call_t cancel_call = NULL;
	lck_mtx_lock(&apple80211_lock);
	if (ready && ap_active) {
		ap_bridge_pending = true;
		if (ap_sta_replay_call == NULL) {
			ap_sta_replay_call = thread_call_allocate(ap_sta_replay_tick, NULL);
		}
		call = ap_sta_replay_call;
	} else if (!ready) {
		ap_bridge_ready = false;
		ap_bridge_pending = false;
		cancel_call = ap_sta_replay_call;
	}
	lck_mtx_unlock(&apple80211_lock);
	if (cancel_call != NULL) {
		thread_call_cancel(cancel_call);
	}
	if (call != NULL) {
		uint64_t deadline = 0;
		clock_interval_to_deadline(AP_STA_REPLAY_MS, 1000000u, &deadline);
		thread_call_enter_delayed(call, deadline);
	}
}

void
fuxi_wifi_apple80211_ap_sta_arrive(const uint8_t *mac, const uint8_t *rsn_ie, size_t rsn_ie_len)
{
	uint8_t ev_payload[32] = { 0 };
	uint8_t used_cnt = 0;
	uint8_t auth_cnt = 0;
	bool open_auto_auth = false;

	if (mac == NULL) {
		return;
	}
	lck_mtx_lock(&apple80211_lock);
	struct fuxi_ap_sta_rec *slot = NULL;
	for (unsigned i = 0; i < FUXI_AP_MAX_STAS; i++) {
		if (ap_stas[i].used && memcmp(ap_stas[i].mac, mac, 6) == 0) {
			slot = &ap_stas[i];
			break;
		}
	}
	if (slot == NULL) {
		for (unsigned i = 0; i < FUXI_AP_MAX_STAS; i++) {
			if (!ap_stas[i].used) {
				slot = &ap_stas[i];
				break;
			}
		}
	}
	if (slot != NULL) {
		bzero(slot, sizeof(*slot));
		slot->used = true;
		slot->authorized = (ap_auth_upper == 0);
		slot->joined_before_bridge = !ap_bridge_ready;
		open_auto_auth = slot->authorized;
		memcpy(slot->mac, mac, 6);
		if (rsn_ie != NULL && rsn_ie_len >= 2 && rsn_ie_len <= sizeof(slot->rsn_ie)) {
			memcpy(slot->rsn_ie, rsn_ie, rsn_ie_len);
			slot->rsn_ie_len = (uint16_t)rsn_ie_len;
		}
	}
	used_cnt = ap_used_count_locked();
	auth_cnt = ap_authorized_count_locked();
	lck_mtx_unlock(&apple80211_lock);

	/* hostapd reads RSNXE bytes at +0x10 inside STA_ARRIVE payload; always send 32 zero-padded bytes. */
	memcpy(ev_payload, mac, 6);
	ev_payload[8] = used_cnt;
	int err = post_if(ap_interface_name, EVENT_STA_ARRIVE, ev_payload, sizeof(ev_payload));
	printf("FUXI WIFI IOS: ap1 STA_ARRIVE %02x:%02x:%02x:%02x:%02x:%02x rsn=%zu posted=%d\n",
	    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], rsn_ie_len, err);
	if (open_auto_auth) {
		ap_post_en0_sta_event(EVENT_STA_ARRIVE, mac, auth_cnt);
	}
}

void
fuxi_wifi_apple80211_ap_sta_leave(const uint8_t *mac, uint16_t reason)
{
	uint8_t ev_payload[16] = { 0 };
	uint8_t used_cnt = 0;
	uint8_t auth_cnt = 0;
	bool was_auth = false;

	if (mac == NULL) {
		return;
	}
	lck_mtx_lock(&apple80211_lock);
	for (unsigned i = 0; i < FUXI_AP_MAX_STAS; i++) {
		if (ap_stas[i].used && memcmp(ap_stas[i].mac, mac, 6) == 0) {
			was_auth = ap_stas[i].authorized;
			bzero(&ap_stas[i], sizeof(ap_stas[i]));
			break;
		}
	}
	used_cnt = ap_used_count_locked();
	auth_cnt = ap_authorized_count_locked();
	lck_mtx_unlock(&apple80211_lock);

	memcpy(ev_payload, mac, 6);
	ev_payload[8] = used_cnt;
	int err = post_if(ap_interface_name, EVENT_STA_LEAVE, ev_payload, sizeof(ev_payload));
	printf("FUXI WIFI IOS: ap1 STA_LEAVE %02x:%02x:%02x:%02x:%02x:%02x reason=%u posted=%d\n",
	    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], reason, err);
	if (was_auth) {
		ap_post_en0_sta_event(EVENT_STA_LEAVE, mac, auth_cnt);
	}
}

static int
ap_set_host_ap_mode(const struct apple80211req *request, char *said, size_t said_size)
{
	if (!fuxi_wifi_ap_enabled() || ap_ops == NULL) {
		return EOPNOTSUPP;
	}
	if (request->req_val == 2 || (request->req_len == 0 && request->req_val != 1)) {
		int err = ap_ops->ap_stop ? ap_ops->ap_stop() : 0;
		uint8_t left_macs[FUXI_AP_MAX_STAS][6];
		uint8_t left_count = 0;
		thread_call_t cancel_call = NULL;
		lck_mtx_lock(&apple80211_lock);
		for (unsigned i = 0; i < FUXI_AP_MAX_STAS; i++) {
			if (ap_stas[i].used && ap_stas[i].authorized) {
				memcpy(left_macs[left_count++], ap_stas[i].mac, 6);
			}
		}
		ap_active = false;
		ap_bridge_ready = false;
		ap_bridge_pending = false;
		cancel_call = ap_sta_replay_call;
		ap_ssid_len = 0;
		ap_freq_mhz = 0;
		bzero(ap_stas, sizeof(ap_stas));
		lck_mtx_unlock(&apple80211_lock);
		if (cancel_call != NULL) {
			thread_call_cancel(cancel_call);
		}
		for (uint8_t i = 0; i < left_count; i++) {
			ap_post_en0_sta_event(EVENT_STA_LEAVE, left_macs[i], (uint8_t)(left_count - 1u - i));
		}
		fuxi_wifi_ap_net_link_state(false);
		struct apple80211_link_changed lc = { .down = 1 };
		post_if(ap_interface_name, EVENT_MODE_CHANGED, NULL, 0);
		post_if(ap_interface_name, EVENT_LINK_CHANGED, &lc, sizeof(lc));
		/*
		 * wifid StopWiFiDirectGroup (cmd 6) waits on en0 for EVENT_MODE_CHANGED (8), then checks
		 * en0 OP_MODE (15) bit 3 == 0 before invoking __WiFiDeviceManagerHostApStateChangedEventCallback(..., 0).
		 */
		int en0_ev = post(EVENT_MODE_CHANGED, NULL, 0);
		if (fuxi_wifi_ap_options() & FUXI_WIFI_AP_VERBOSE) {
			printf("FUXI WIFI IOS: en0 MODE_CHANGED (SoftAP stop) posted=%d\n", en0_ev);
		}
		snprintf(said, said_size, " (SoftAP stop -> %d)", err);
		return err;
	}
	if (request->req_val != 1 || request->req_data == USER_ADDR_NULL ||
	    request->req_len < offsetof(struct apple80211_apmode_data, ie)) {
		return EINVAL;
	}

	struct apple80211_apmode_data *asked = kalloc_data(sizeof(*asked), Z_WAITOK | Z_ZERO);
	struct fuxi_wifi_ap_config *cfg = kalloc_data(sizeof(*cfg), Z_WAITOK | Z_ZERO);
	int error = 0;
	if (asked == NULL || cfg == NULL) {
		error = ENOMEM;
		goto out;
	}
	size_t copy_len = request->req_len < sizeof(*asked) ? request->req_len : sizeof(*asked);
	if ((error = copyin(request->req_data, asked, copy_len)) != 0) {
		goto out;
	}
	if (asked->ssid_len == 0 || asked->ssid_len > sizeof(cfg->ssid) ||
	    asked->ie_len > sizeof(cfg->extra_ie)) {
		error = EINVAL;
		goto out;
	}

	/* If STA is currently joined/joining, tear it down cleanly before bringing up SoftAP. */
	struct fuxi_wifi_sta_state *sta_st = kalloc_data(sizeof(*sta_st), Z_WAITOK | Z_ZERO);
	if (sta_st != NULL) {
		fuxi_wifi_sta_state(sta_st);
		bool sta_busy = (sta_st->phase == FUXI_WIFI_STA_JOINED || sta_st->phase == FUXI_WIFI_STA_JOINING);
		kfree_data(sta_st, sizeof(*sta_st));
		if (sta_busy) {
			printf("FUXI WIFI IOS: ap1 leaving active STA session before SoftAP start\n");
			(void)fuxi_wifi_sta_ask_leave();
		}
	}

	unsigned ap_opts = fuxi_wifi_ap_options();
	memcpy(cfg->mac, ap_ops->address, 6);
	memcpy(cfg->ssid, asked->ssid, asked->ssid_len);
	cfg->ssid_len = (uint8_t)asked->ssid_len;
	cfg->hidden = (asked->mode & 0x200u) != 0 ? 1 : 0;
	cfg->wpa2 = (asked->auth_upper != 0 || asked->key_len != 0) ? 1 : 0;
	if (ap_opts & FUXI_WIFI_AP_HE) {
		cfg->flags |= FUXI_WIFI_AP_CFG_HE;
	}
	if (ap_opts & FUXI_WIFI_AP_PREFER_5GHZ) {
		cfg->flags |= FUXI_WIFI_AP_CFG_PREFER_5GHZ;
	}
	lck_mtx_lock(&apple80211_lock);
	if (country_known) {
		cfg->country[0] = (char)country_code[0];
		cfg->country[1] = (char)country_code[1];
	}
	lck_mtx_unlock(&apple80211_lock);
	cfg->freq_mhz = channel_frequency(&asked->channel);
	if (asked->ie_len > 0) {
		memcpy(cfg->extra_ie, asked->ie, asked->ie_len);
		cfg->extra_ie_len = (uint16_t)asked->ie_len;
	}

	ap_wait_prev_hostapd_exit();

	uint32_t actual_freq = 0;
	error = ap_ops->ap_start(cfg, &actual_freq);
	if (error == 0) {
		thread_call_t cancel_call = NULL;
		lck_mtx_lock(&apple80211_lock);
		ap_active = true;
		ap_bridge_ready = false;
		ap_bridge_pending = false;
		cancel_call = ap_sta_replay_call;
		memcpy(ap_mac, cfg->mac, 6);
		memcpy(ap_ssid, cfg->ssid, cfg->ssid_len);
		ap_ssid_len = cfg->ssid_len;
		ap_freq_mhz = actual_freq ? actual_freq : (cfg->freq_mhz ? cfg->freq_mhz : 2437);
		ap_mode = asked->mode;
		ap_auth_lower = asked->auth_lower;
		ap_auth_upper = asked->auth_upper;
		bzero(ap_stas, sizeof(ap_stas));
		lck_mtx_unlock(&apple80211_lock);
		if (cancel_call != NULL) {
			thread_call_cancel(cancel_call);
		}

		fuxi_wifi_ap_net_link_state(true);
		struct apple80211_link_changed lc = { .down = 0 };
		post_if(ap_interface_name, EVENT_MODE_CHANGED, NULL, 0);
		post_if(ap_interface_name, EVENT_SSID_CHANGED, NULL, 0);
		post_if(ap_interface_name, EVENT_BSSID_CHANGED, NULL, 0);
		post_if(ap_interface_name, EVENT_LINK_CHANGED, &lc, sizeof(lc));
		/*
		 * wifid StartWiFiDirectGroup (cmd 5, 0x100046c78..0x100046e80) stores its completion callback
		 * before Apple80211Set(en0, HOST_AP_MODE) and waits on en0 for EVENT_HOST_AP_MODE_START (30 / 0x1e)
		 * to call 0x100049dd4 -> __WiFiDeviceManagerStartMISSessionCallback -> NETRBClientStartService("ap1")
		 * -> misd (bridge100 + bootpd + pf NAT).
		 */
		int en0_ev = post(EVENT_HOST_AP_MODE_START, NULL, 0);
		printf("FUXI WIFI IOS: en0 HOST_AP_MODE_START posted=%d\n", en0_ev);
	}
	snprintf(said, said_size, " (SoftAP start mode=0x%x auth=0x%x/0x%x ch=%u->%uMHz flags=0x%x cc=%c%c ssid_len=%u ie_len=%u -> %d)",
	    asked->mode, asked->auth_lower, asked->auth_upper, asked->channel.channel,
	    actual_freq, cfg->flags, cfg->country[0] ? cfg->country[0] : '-',
	    cfg->country[1] ? cfg->country[1] : '-', asked->ssid_len, asked->ie_len, error);

out:
	if (asked != NULL) {
		memset_s(asked, sizeof(*asked), 0, sizeof(*asked));
		kfree_data(asked, sizeof(*asked));
	}
	if (cfg != NULL) {
		memset_s(cfg, sizeof(*cfg), 0, sizeof(*cfg));
		kfree_data(cfg, sizeof(*cfg));
	}
	return error;
}

static int
ap_give_station_list(const struct apple80211req *request)
{
	struct apple80211_station_list *list = kalloc_data(sizeof(*list), Z_WAITOK | Z_ZERO);
	if (list == NULL) {
		return ENOMEM;
	}
	list->version = 1;
	lck_mtx_lock(&apple80211_lock);
	for (unsigned i = 0; i < FUXI_AP_MAX_STAS && list->count < 127; i++) {
		if (!ap_stas[i].used) {
			continue;
		}
		struct apple80211_station_entry *e = &list->stations[list->count++];
		e->version = 1;
		memcpy(e->mac, ap_stas[i].mac, 6);
		e->flags = ap_stas[i].authorized ? 1u : 0u;
	}
	lck_mtx_unlock(&apple80211_lock);
	int err = give(request, list, sizeof(*list));
	kfree_data(list, sizeof(*list));
	return err;
}

static int
ap_get(struct apple80211req *request)
{
	switch (request->req_type) {
	case 96:                                /* VIRTUAL_IF_ROLE: 7 = SoftAP */
		request->req_val = 7;
		return 0;
	case 97: {                              /* VIRTUAL_IF_PARENT: "en0" */
		char parent[16] = "en0";
		int err = give(request, parent, sizeof(parent));
		if (err == 0) {
			request->req_len = sizeof(parent);
		}
		return err;
	}
	case 15:                                /* OP_MODE: 8 (APPLE80211_M_HOSTAP) when active, else 0 */
		lck_mtx_lock(&apple80211_lock);
		request->req_val = ap_active ? 8 : 0;
		lck_mtx_unlock(&apple80211_lock);
		return 0;
	case 25:                                /* HOST_AP_MODE */
		lck_mtx_lock(&apple80211_lock);
		request->req_val = ap_active ? 1 : 2;
		lck_mtx_unlock(&apple80211_lock);
		return 0;
	case 13:                                /* STATE: 4 (RUN) when active, 0 (INIT) when stopped */
		lck_mtx_lock(&apple80211_lock);
		request->req_val = ap_active ? 4 : 0;
		lck_mtx_unlock(&apple80211_lock);
		return 0;
	case 1: {                               /* SSID */
		uint8_t ssid[32];
		uint8_t slen = 0;
		bool act;
		lck_mtx_lock(&apple80211_lock);
		act = ap_active;
		slen = ap_ssid_len;
		if (slen > 0) {
			memcpy(ssid, ap_ssid, slen);
		}
		lck_mtx_unlock(&apple80211_lock);
		if (!act || slen == 0) {
			return ENXIO;
		}
		int err = give(request, ssid, slen);
		if (err == 0) {
			request->req_len = slen;
		}
		return err;
	}
	case 9: {                               /* BSSID: ap1 own MAC */
		uint8_t bssid[6];
		lck_mtx_lock(&apple80211_lock);
		memcpy(bssid, ap_mac, 6);
		lck_mtx_unlock(&apple80211_lock);
		return give(request, bssid, sizeof(bssid));
	}
	case 4: {                               /* CHANNEL */
		struct {
			uint32_t version;
			struct apple80211_channel channel;
		} answer = { .version = 1 };
		uint32_t freq;
		bool act;
		lck_mtx_lock(&apple80211_lock);
		act = ap_active;
		freq = ap_freq_mhz;
		lck_mtx_unlock(&apple80211_lock);
		if (!act || freq == 0) {
			return ENXIO;
		}
		channel_fill(&answer.channel, freq, 0);
		if (freq >= 5000u && (fuxi_wifi_ap_options() & FUXI_WIFI_AP_PREFER_5GHZ)) {
			answer.channel.flags = (answer.channel.flags & ~CHANNEL_20MHZ) | CHANNEL_80MHZ;
		}
		return give(request, &answer, sizeof(answer));
	}
	case 2: {                               /* AUTH_TYPE */
		struct {
			uint32_t version, lower, upper, extended;
		} answer = { .version = 1 };
		lck_mtx_lock(&apple80211_lock);
		answer.lower = ap_auth_lower ? ap_auth_lower : 1;
		answer.upper = ap_auth_upper;
		lck_mtx_unlock(&apple80211_lock);
		return give(request, &answer, sizeof(answer));
	}
	case 19: {                              /* POWER */
		struct {
			uint32_t version, radios, state[4];
		} answer = { .version = 1, .radios = 1, .state = { 1 } };
		return give(request, &answer, sizeof(answer));
	}
	case 42:                                /* STATION_LIST (2056 B) */
		return ap_give_station_list(request);
	case 73: {                              /* STA_IE_LIST (1040 B): hostapd reads RSN IE directly at +0x10 */
		if (request->req_data == USER_ADDR_NULL || request->req_len < 16) {
			return EINVAL;
		}
		struct apple80211_sta_ie_list *io = kalloc_data(sizeof(*io), Z_WAITOK | Z_ZERO);
		if (io == NULL) {
			return ENOMEM;
		}
		int err = copyin(request->req_data, io, 16);
		if (err != 0) {
			kfree_data(io, sizeof(*io));
			return err;
		}
		uint32_t room = io->ie_len;
		if (room == 0 || room > sizeof(io->ie)) {
			room = sizeof(io->ie);
		}
		bool found = false;
		lck_mtx_lock(&apple80211_lock);
		for (unsigned i = 0; i < FUXI_AP_MAX_STAS; i++) {
			if (ap_stas[i].used && memcmp(ap_stas[i].mac, io->mac, 6) == 0) {
				found = true;
				uint32_t n = ap_stas[i].rsn_ie_len;
				if (n > room) {
					n = room;
				}
				io->version = 1;
				io->ie_len = n;
				if (n > 0) {
					memcpy(io->ie, ap_stas[i].rsn_ie, n);
				}
				break;
			}
		}
		lck_mtx_unlock(&apple80211_lock);
		err = found ? give(request, io, sizeof(*io)) : ENOENT;
		kfree_data(io, sizeof(*io));
		return err;
	}
	case 78: {                              /* KEY_RSC (148 B): return TX seq in key_rsc[8] at +0x54 */
		struct apple80211_key key;
		if (request->req_data == USER_ADDR_NULL || request->req_len < sizeof(key)) {
			return EINVAL;
		}
		int err = copyin(request->req_data, &key, sizeof(key));
		if (err != 0) {
			return err;
		}
		bzero(key.key_rsc, sizeof(key.key_rsc));
		if (ap_ops != NULL && ap_ops->get_key_seq != NULL) {
			err = ap_ops->get_key_seq(key.key_ea, (uint8_t)key.key_index, key.key_rsc);
			if (err == ENOENT) {
				err = 0; /* zero seq before key is installed */
			}
		}
		key.key_rsc_len = 8;
		if (err == 0) {
			err = give(request, &key, sizeof(key));
		}
		memset_s(&key, sizeof(key), 0, sizeof(key));
		return err;
	}
	case 12:
	case 27:
	case 43:
	case 44:
	case 51:
	case 106:
	case 207:
	case 254:
		return -1; /* delegate to shared read-only radio queries in get() */
	default:
		return EOPNOTSUPP;
	}
}

static int
ap_set(const struct apple80211req *request, char *said, size_t said_size)
{
	switch (request->req_type) {
	case 25:                                /* HOST_AP_MODE */
		return ap_set_host_ap_mode(request, said, said_size);
	case 1: {                               /* SSID (hostapd set_ssid) */
		uint8_t ssid[32];
		if (request->req_len == 0 || request->req_len > sizeof(ssid) || request->req_data == USER_ADDR_NULL) {
			return EINVAL;
		}
		int err = copyin(request->req_data, ssid, request->req_len);
		if (err != 0) {
			return err;
		}
		lck_mtx_lock(&apple80211_lock);
		memcpy(ap_ssid, ssid, request->req_len);
		ap_ssid_len = (uint8_t)request->req_len;
		lck_mtx_unlock(&apple80211_lock);
		snprintf(said, said_size, " (ap1 ssid_len=%u)", request->req_len);
		return 0;
	}
	case 3: {                               /* CIPHER_KEY (148 B) */
		struct apple80211_key key;
		if (ap_ops == NULL || ap_ops->set_key == NULL) {
			return EOPNOTSUPP;
		}
		if (request->req_data == USER_ADDR_NULL || request->req_len < sizeof(key)) {
			return EINVAL;
		}
		int err = copyin(request->req_data, &key, sizeof(key));
		if (err != 0) {
			return err;
		}
		int is_del = (key.key_len == 0 || key.key_cipher_type == 0) ? 1 : 0;
		uint32_t suite = 0;
		if (!is_del) {
			if (key.key_len > sizeof(key.key)) {
				memset_s(&key, sizeof(key), 0, sizeof(key));
				return EINVAL;
			}
			switch (key.key_cipher_type) {
			case 3: suite = 0x000fac02u; break; /* TKIP */
			case 4: suite = 0x000fac04u; break; /* CCMP fallback */
			case 5: suite = key.key_index >= 4 ? 0x000fac06u : 0x000fac04u; break; /* iOS 16.7 hostapd: 5=AES-CCM/IGTK */
			default:
				snprintf(said, said_size, " (ap1 unsupported cipher=%u idx=%u)", key.key_cipher_type, key.key_index);
				memset_s(&key, sizeof(key), 0, sizeof(key));
				return EOPNOTSUPP;
			}
		}
		size_t seq_len = key.key_rsc_len <= sizeof(key.key_rsc) ? key.key_rsc_len : 0;
		err = ap_ops->set_key(key.key_ea, (uint8_t)key.key_index, suite,
		    key.key, key.key_len, key.key_rsc, seq_len, is_del);
		if (is_del && err == ENOENT) {
			err = 0; /* hostapd clears unused slots on startup/join */
		}
		snprintf(said, said_size, " (ap1 key idx=%u type=%u len=%u del=%d ea=%02x:%02x:%02x:%02x:%02x:%02x -> %d)",
		    key.key_index, key.key_cipher_type, key.key_len, is_del,
		    key.key_ea[0], key.key_ea[1], key.key_ea[2], key.key_ea[3], key.key_ea[4], key.key_ea[5], err);
		memset_s(&key, sizeof(key), 0, sizeof(key));
		return err;
	}
	case 74: {                              /* STA_AUTHORIZE (20 B) */
		struct apple80211_sta_authorize auth;
		if (ap_ops == NULL || ap_ops->sta_authorize == NULL) {
			return EOPNOTSUPP;
		}
		if (request->req_data == USER_ADDR_NULL || request->req_len < sizeof(auth)) {
			return EINVAL;
		}
		int err = copyin(request->req_data, &auth, sizeof(auth));
		if (err != 0) {
			return err;
		}
		err = ap_ops->sta_authorize(auth.mac, auth.authorize ? 1 : 0);
		bool became_auth = false;
		bool lost_auth = false;
		uint8_t auth_cnt = 0;
		if (err == 0) {
			lck_mtx_lock(&apple80211_lock);
			for (unsigned i = 0; i < FUXI_AP_MAX_STAS; i++) {
				if (ap_stas[i].used && memcmp(ap_stas[i].mac, auth.mac, 6) == 0) {
					bool next = auth.authorize != 0;
					became_auth = (!ap_stas[i].authorized && next);
					lost_auth = (ap_stas[i].authorized && !next);
					ap_stas[i].authorized = next;
					break;
				}
			}
			auth_cnt = ap_authorized_count_locked();
			lck_mtx_unlock(&apple80211_lock);
		}
		if (became_auth) {
			ap_post_en0_sta_event(EVENT_STA_ARRIVE, auth.mac, auth_cnt);
		} else if (lost_auth) {
			ap_post_en0_sta_event(EVENT_STA_LEAVE, auth.mac, auth_cnt);
		}
		snprintf(said, said_size, " (ap1 auth=%u mac=%02x:%02x:%02x:%02x:%02x:%02x -> %d)",
		    auth.authorize, auth.mac[0], auth.mac[1], auth.mac[2], auth.mac[3], auth.mac[4], auth.mac[5], err);
		return err;
	}
	case 75:                                /* STA_DISASSOCIATE (16 B) */
	case 76: {                              /* STA_DEAUTH (16 B) */
		struct apple80211_sta_deauth da;
		if (ap_ops == NULL || ap_ops->sta_deauth == NULL) {
			return EOPNOTSUPP;
		}
		if (request->req_data == USER_ADDR_NULL || request->req_len < sizeof(da)) {
			return EINVAL;
		}
		int err = copyin(request->req_data, &da, sizeof(da));
		if (err != 0) {
			return err;
		}
		int disassoc = request->req_type == 75 ? 1 : 0;
		err = ap_ops->sta_deauth(da.mac, (uint16_t)da.reason, disassoc);
		bool was_auth = false;
		bool wake_hostapd = false;
		uint8_t auth_cnt = 0;
		static const uint8_t bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
		lck_mtx_lock(&apple80211_lock);
		for (unsigned i = 0; i < FUXI_AP_MAX_STAS; i++) {
			if (ap_stas[i].used && memcmp(ap_stas[i].mac, da.mac, 6) == 0) {
				was_auth = ap_stas[i].authorized;
				bzero(&ap_stas[i], sizeof(ap_stas[i]));
				break;
			}
		}
		if (!ap_active && ap_hostapd_active && memcmp(da.mac, bcast, 6) == 0) {
			uint64_t now_ns = 0;
			absolutetime_to_nanoseconds(mach_absolute_time(), &now_ns);
			ap_hostapd_active = false;
			ap_hostapd_teardown_ns = now_ns;
			wake_hostapd = true;
		}
		auth_cnt = ap_authorized_count_locked();
		lck_mtx_unlock(&apple80211_lock);
		if (wake_hostapd) {
			thread_wakeup((event_t)&ap_hostapd_active);
		}
		if (was_auth) {
			ap_post_en0_sta_event(EVENT_STA_LEAVE, da.mac, auth_cnt);
		}
		snprintf(said, said_size, " (ap1 %s reason=%u mac=%02x:%02x:%02x:%02x:%02x:%02x -> %d)",
		    disassoc ? "disassoc" : "deauth", da.reason,
		    da.mac[0], da.mac[1], da.mac[2], da.mac[3], da.mac[4], da.mac[5], err);
		return err;
	}
	case 77: {                              /* RSN_CONF (164 B): beacon already carries WPA2-PSK CCMP RSN IE */
		lck_mtx_lock(&apple80211_lock);
		if (ap_active) {
			ap_hostapd_active = true;
			ap_hostapd_teardown_ns = 0;
		}
		lck_mtx_unlock(&apple80211_lock);
		snprintf(said, said_size, " (ap1 RSN_CONF accepted)");
		return 0;
	}
	case 19:                                /* POWER on ap1: keep radio up */
	case 4:                                 /* CHANNEL on ap1 */
		return 0;
	case 22:                                /* DISASSOCIATE on ap1 */
		return 0;
	default:
		return EOPNOTSUPP;
	}
}

static int
get(struct apple80211req *request)
{
	struct fuxi_wifi_sta_state *state;
	int error = 0;

	switch (request->req_type) {
	case 43:
	case 44: {                              /* the driver's and the hardware's name: the text alone, in 256 bytes */
		static const char name[256] = "Fuxi Wi-Fi iOS shim (synthetic backend)";
		static const char native_name[256] = "Fuxi WCN7850 ath12k (native power/scan)";

		return give(request, fuxi_wifi_ios_native_ready() ? native_name : name, sizeof(name));
	}
	case 12: {
		/*
		 * What the card can do, a bit each (apple80211_var.h: APPLE80211_CAP_*): the 13 bytes are the
		 * bits alone, from the first (the framework hands in a buffer that begins after the old
		 * header's version word). AES, AES-CCM, power management, short slot and preamble, WPA2, WME,
		 * short guard interval at 20 MHz. Read wrong, the framework sees no WPA2 and builds no join
		 * for a WPA2 network ("Unable to join").
		 */
		uint8_t bits[13] = { 0 };
		static const uint8_t has[] = { 2, 3, 6, 9, 10, 14, 17, 19 };

		for (unsigned i = 0; i < sizeof(has); i++) {
			bits[has[i] / 8] |= (uint8_t)(1u << (has[i] % 8));
		}
		if (fuxi_wifi_ap_enabled()) {
			bits[7 / 8] |= (uint8_t)(1u << (7 % 8)); /* APPLE80211_CAP_HOSTAP = 7 */
		}
		return give(request, bits, sizeof(bits));
	}
	case 25:                                /* HOST_AP_MODE query on en0 */
		if (!fuxi_wifi_ap_enabled()) {
			break;
		}
		lck_mtx_lock(&apple80211_lock);
		request->req_val = ap_active ? 1 : 2;
		lck_mtx_unlock(&apple80211_lock);
		return 0;
	case 42: {                              /* STATION_LIST query on en0 while SoftAP active */
		if (!fuxi_wifi_ap_enabled()) {
			break;
		}
		lck_mtx_lock(&apple80211_lock);
		bool act = ap_active;
		lck_mtx_unlock(&apple80211_lock);
		if (!act) {
			break;
		}
		return ap_give_station_list(request);
	}
	case 19: {                              /* the radio's power: one radio. Until the driver is up it counts as on: it */
		struct {                        /* is being brought up, and iOS asks this once, early */
			uint32_t version, radios, state[4];
		} answer = { .version = 1, .radios = 1, .state = { !fuxi_wifi_sta_present() || fuxi_wifi_sta_powered() } };

		return give(request, &answer, sizeof(answer));
	}
	case 15:                                /* the mode: STA (1), plus HOSTAP bit (8) while SoftAP is active */
		if (fuxi_wifi_ap_enabled()) {
			lck_mtx_lock(&apple80211_lock);
			request->req_val = ap_active ? (1 | 8) : 1;
			lck_mtx_unlock(&apple80211_lock);
		} else {
			request->req_val = 1;
		}
		return 0;
	case 106: {                             /* the strengths the radio can report, and the floor to join at */
		struct {
			uint32_t version;
			int32_t minimum, maximum, threshold;
		} answer = { .version = 1, .minimum = INT8_MIN, .maximum = INT8_MAX, .threshold = INT8_MIN };

		if (!rssi_bounds_enabled()) {
			break;                  /* refused below, as before */
		}
		return give(request, &answer, sizeof(answer));
	}
	case 27:
	case 254: {                             /* the channels the radio may tune to */
		struct apple80211_channels *answer;
		struct fuxi_wifi_sta_channel channels[96];
		unsigned count = fuxi_wifi_sta_channels(channels, 96);

		if (count == 0 || (answer = kalloc_data(sizeof(*answer), Z_WAITOK | Z_ZERO)) == NULL) {
			return count == 0 ? ENXIO : ENOMEM;
		}
		answer->version = 1;
		answer->count = count;
		for (unsigned i = 0; i < count; i++) {
			channel_fill(&answer->channels[i], channels[i].frequency, channels[i].flags);
		}
		error = give(request, answer, sizeof(*answer));
		kfree_data(answer, sizeof(*answer));
		return error;
	}
	case 207: {                             /* ... and more of each */
		struct apple80211_channels_info *answer;
		struct fuxi_wifi_sta_channel channels[96];
		unsigned count = fuxi_wifi_sta_channels(channels, 96);

		if (count == 0 || (answer = kalloc_data(sizeof(*answer), Z_WAITOK | Z_ZERO)) == NULL) {
			return count == 0 ? ENXIO : ENOMEM;
		}
		answer->count = (uint16_t)count;
		for (unsigned i = 0; i < count; i++) {
			struct apple80211_channel channel;

			channel_fill(&channel, channels[i].frequency, channels[i].flags);
			answer->number[i] = (uint8_t)channel.channel;
			answer->dfs[i] = (channels[i].flags & FUXI_WIFI_STA_CHANNEL_RADAR) != 0;
			answer->passive[i] = (channels[i].flags & FUXI_WIFI_STA_CHANNEL_PASSIVE) != 0;
			answer->flags[i] = channel.flags;
		}
		error = give(request, answer, sizeof(*answer));
		kfree_data(answer, sizeof(*answer));
		return error;
	}
	case 51: {                              /* the country code wifid set (fuxi,wifi-ux bit 3) */
		struct {
			uint32_t version;
			uint8_t code[4];
		} answer = { .version = 1 };

		if (!(fuxi_wifi_ux() & FUXI_WIFI_UX_COUNTRY)) {
			break;                  /* refused below, as before */
		}
		lck_mtx_lock(&apple80211_lock);
		bool known = country_known;
		memcpy(answer.code, country_code, sizeof(country_code));
		lck_mtx_unlock(&apple80211_lock);
		if (!known) {
			break;                  /* not set yet: refused below, as before */
		}
		/* 長度不到 8 的呼叫者只要三個字元；8 以上是 version＋cc[3] */
		return request->req_len < sizeof(answer) ? give(request, answer.code, 3) : give(request, &answer, sizeof(answer));
	}
	case 21:                                /* how the last join went (0: not over yet) */
		lck_mtx_lock(&apple80211_lock);
		request->req_val = (int32_t)join_result;
		lck_mtx_unlock(&apple80211_lock);
		return 0;
	case 11: {                              /* what the last scan heard, one access point a request; EIO: that was all */
		struct fuxi_wifi_sta_bss *bss = kalloc_data(sizeof(*bss), Z_WAITOK);
		unsigned index;
		bool there;

		if (bss == NULL) {
			return ENOMEM;
		}
		lck_mtx_lock(&apple80211_lock);
		index = result_next++;
		lck_mtx_unlock(&apple80211_lock);
		there = fuxi_wifi_sta_heard(index, bss);
		error = there ? give_network(request, bss) : EIO;
		kfree_data(bss, sizeof(*bss));
		return error;
	}
	default:
		break;
	}

	/* the rest is about the network joined, and there is none unless the station says so */
	if ((state = kalloc_data(sizeof(*state), Z_WAITOK)) == NULL) {
		return ENOMEM;
	}
	fuxi_wifi_sta_state(state);
	switch (request->req_type) {
	case 13:                                /* the state: APPLE80211_S_INIT 0, _SCAN 1, _AUTH 2, _ASSOC 3, _RUN 4 */
		request->req_val = state->phase == FUXI_WIFI_STA_JOINED ? 4 : state->phase == FUXI_WIFI_STA_JOINING ? 2 :
		    state->phase == FUXI_WIFI_STA_SCANNING ? 1 : 0;
		break;
	case 1:                                 /* its name; the length goes back in the request */
		if (state->phase != FUXI_WIFI_STA_JOINED) {
			error = ENXIO;
		} else if ((error = give(request, state->bss.ssid, state->bss.ssid_length)) == 0) {
			request->req_len = state->bss.ssid_length;
		}
		break;
	case 9:                                 /* its access point's address */
		error = state->phase != FUXI_WIFI_STA_JOINED ? ENXIO : give(request, state->bss.bssid, 6);
		break;
	case 4: {                               /* its channel */
		struct {
			uint32_t version;
			struct apple80211_channel channel;
		} answer = { .version = 1 };

		if (state->phase != FUXI_WIFI_STA_JOINED) {
			error = ENXIO;
		} else {
			channel_fill(&answer.channel, state->bss.frequency, 0);
			error = give(request, &answer, sizeof(answer));
		}
		break;
	}
	case 16: {                              /* how strongly it is heard: dBm, per radio and all together, twice over */
		struct {
			uint32_t version, radios, unit;
			int32_t each[4], all, each_ext[4], all_ext;
		} answer = { .version = 1, .radios = 1, .each = { state->bss.rssi }, .all = state->bss.rssi,
			     .each_ext = { state->bss.rssi }, .all_ext = state->bss.rssi };

		error = state->phase != FUXI_WIFI_STA_JOINED ? ENXIO : give(request, &answer, sizeof(answer));
		break;
	}
	case 17: {                              /* the noise under it, laid out as the strength is */
		struct {
			uint32_t version, radios, unit;
			int32_t each[4], all, each_ext[4], all_ext;
		} answer = { .version = 1, .radios = 1, .each = { NOISE_FLOOR }, .all = NOISE_FLOOR,
			     .each_ext = { NOISE_FLOOR }, .all_ext = NOISE_FLOOR };

		error = state->phase != FUXI_WIFI_STA_JOINED ? ENXIO : give(request, &answer, sizeof(answer));
		break;
	}
	case 103:                             /* all of it, as a scan's result */
		error = state->phase != FUXI_WIFI_STA_JOINED ? ENXIO : give_network(request, &state->bss);
		break;
	case 2: {                               /* how it was joined: open system, then WPA2 with a passphrase */
		struct {
			uint32_t version, lower, upper, extended;
		} answer = { .version = 1, .lower = 1, .upper = state->security == FUXI_WIFI_STA_WPA2_PSK ? 8 : 0 };

		error = state->phase != FUXI_WIFI_STA_JOINED ? ENXIO : give(request, &answer, sizeof(answer));
		break;
	}
	default:
		error = EOPNOTSUPP;
		break;
	}
	kfree_data(state, sizeof(*state));
	return error;
}

/* ---- set ---------------------------------------------------------------------------------------------- */

static int
set_scan(const struct apple80211req *request, char *said, size_t said_size)
{
	struct apple80211_scan_request *asked;
	struct fuxi_wifi_sta_scan *what;
	unsigned unknown = 0;
	int error;

	if (request->req_len != sizeof(*asked)) {
		return EINVAL;
	}
	if (!fuxi_wifi_sta_present()) {
		return EBUSY;                   /* (the radio is still being brought up: the framework tries again) */
	}
	asked = kalloc_data(sizeof(*asked), Z_WAITOK);
	what = kalloc_data(sizeof(*what), Z_WAITOK | Z_ZERO);
	if (asked == NULL || what == NULL || (error = copyin(request->req_data, asked, sizeof(*asked))) != 0) {
		error = asked == NULL || what == NULL ? ENOMEM : error;
		goto out;
	}
	if (asked->ssid_length > sizeof(what->ssid) || asked->channel_count > 400 ||
	    asked->name_count > 10) {
		error = EINVAL;
		goto out;
	}
	if (asked->ssid_length <= sizeof(what->ssid)) {
		what->ssid_length = (uint8_t)asked->ssid_length;
		memcpy(what->ssid, asked->ssid, asked->ssid_length);
	}
	what->passive = asked->scan_type == 2;
	what->cache_allowed = asked->scan_type == 3;
	what->dwell_ms = asked->dwell_ms;
	/* (more channels than there is room to name: every channel) */
	for (unsigned i = 0; i < asked->channel_count && i < 400 && asked->channel_count <= 64; i++) {
		unsigned frequency = channel_frequency(&asked->channels[i]);

		if (frequency != 0) {
			what->frequencies[what->frequency_count++] = (uint16_t)frequency;
		} else {
			unknown++;
		}
	}
	snprintf(said, said_size, " (a scan: type %u, %u channels of which %u are not the radio's, %u ms on each, a name of %u bytes, "
	    "%u more names, flags %#llx)", asked->scan_type, asked->channel_count, unknown, asked->dwell_ms, asked->ssid_length,
	    asked->name_count, (unsigned long long)asked->flags);
	if (asked->channel_count != 0 && asked->channel_count <= 64 && what->frequency_count == 0) {
		error = EINVAL;                 /* (nothing of what is asked for is a channel of this radio's) */
	} else {
		lck_mtx_lock(&apple80211_lock);
		result_next = 0;
		lck_mtx_unlock(&apple80211_lock);
		error = fuxi_wifi_sta_ask_scan(what);
	}
out:
	if (asked != NULL) {
		kfree_data(asked, sizeof(*asked));
	}
	if (what != NULL) {
		kfree_data(what, sizeof(*what));
	}
	return error;
}

static int
set_join(const struct apple80211req *request, char *said, size_t said_size)
{
	struct apple80211_assoc *asked;
	struct fuxi_wifi_sta_join *what;
	int error;

	if (request->req_len != sizeof(struct apple80211_assoc)) {
		return EINVAL;
	}
	asked = kalloc_data(sizeof(*asked), Z_WAITOK | Z_ZERO);
	what = kalloc_data(sizeof(*what), Z_WAITOK | Z_ZERO);
	if (asked == NULL || what == NULL || (error = copyin(request->req_data, asked, offsetof(struct apple80211_assoc, rest))) != 0) {
		error = asked == NULL || what == NULL ? ENOMEM : error;
		goto out;
	}
	/* (lengths and kinds only: the name, the access point and the key stay out of the log) */
	snprintf(said, said_size, " (a join: a name of %u bytes, %s access point, authentication %#x/%#x, a key of %u bytes of kind %u)",
	    asked->ssid_length, asked->bssid[0] | asked->bssid[1] | asked->bssid[2] | asked->bssid[3] | asked->bssid[4] |
	    asked->bssid[5] ? "a given" : "any", asked->auth_lower, asked->auth_upper, asked->key_length, asked->key_cipher);
	if (asked->ssid_length == 0 || asked->ssid_length > sizeof(what->ssid)) {
		error = EINVAL;
		goto out;
	}
	what->ssid_length = (uint8_t)asked->ssid_length;
	memcpy(what->ssid, asked->ssid, asked->ssid_length);
	memcpy(what->bssid, asked->bssid, sizeof(what->bssid));
	if (asked->key_length == 0) {
		what->security = FUXI_WIFI_STA_OPEN;
	} else if (asked->key_cipher == CIPHER_PASSPHRASE && asked->key_length >= 8 && asked->key_length <= 63) {
		what->security = FUXI_WIFI_STA_WPA2_PSK;
		what->passphrase_length = (uint8_t)asked->key_length;
		memcpy(what->passphrase, asked->key, asked->key_length);
	} else if (asked->key_cipher == CIPHER_PMK && asked->key_length == sizeof(what->pmk)) {
		what->security = FUXI_WIFI_STA_WPA2_PSK;
		memcpy(what->pmk, asked->key, sizeof(what->pmk));
	} else {
		error = EINVAL;
		goto out;
	}
	lck_mtx_lock(&apple80211_lock);
	join_result = 0;
	lck_mtx_unlock(&apple80211_lock);
	error = fuxi_wifi_sta_ask_join(what);
out:
	if (asked != NULL) {
		memset_s(asked, sizeof(*asked), 0, sizeof(*asked));
		kfree_data(asked, sizeof(*asked));
	}
	if (what != NULL) {
		memset_s(what, sizeof(*what), 0, sizeof(*what));
		kfree_data(what, sizeof(*what));
	}
	return error;
}

static int
set(const struct apple80211req *request, char *said, size_t said_size)
{
	switch (request->req_type) {
	case 20:
		return set_join(request, said, said_size);
	case 22:                                /* leave the network */
		return fuxi_wifi_sta_ask_leave();
	case 19: {                              /* the radio's power */
		struct {
			uint32_t version, radios, state[4];
		} asked;
		int error;

		if (request->req_len < sizeof(asked) || (error = copyin(request->req_data, &asked, sizeof(asked))) != 0) {
			return request->req_len < sizeof(asked) ? EINVAL : error;
		}
		snprintf(said, said_size, " (power: %s)", asked.state[0] ? "on" : "off");
		return fuxi_wifi_sta_ask_power(asked.state[0] != 0);
	}
	case 51: {                              /* the country code (fuxi,wifi-ux bit 3): kept, not applied */
		/*
		 * IO80211 框架把 wifid 的 CFString 轉成 version(u32)＋cc[3]（Apple80211Set 長度 8）；
		 * 只有 3 bytes 時當成 cc[3] 本身。兩字母要是大寫英文或數字，第三字元可為空白、NUL 或可印字元。
		 */
		uint8_t bytes[8] = { 0 }, *code;
		const char *layout;
		int error;

		if (!(fuxi_wifi_ux() & FUXI_WIFI_UX_COUNTRY)) {
			return EOPNOTSUPP;
		}
		if (request->req_len < 3 || request->req_data == USER_ADDR_NULL) {
			return EINVAL;
		}
		if ((error = copyin(request->req_data, bytes, request->req_len < sizeof(bytes) ? request->req_len :
		    sizeof(bytes))) != 0) {
			return error;
		}
		if (request->req_len < 7) {
			code = bytes;
			layout = "cc";
		} else {
			code = bytes + 4;
			layout = "version+cc";
		}
		for (unsigned i = 0; i < 2; i++) {
			if (!((code[i] >= 'A' && code[i] <= 'Z') || (code[i] >= '0' && code[i] <= '9'))) {
				snprintf(said, said_size, " (country: not a code, %s)", layout);
				return EINVAL;
			}
		}
		if (code[2] != 0 && (code[2] < 0x20 || code[2] > 0x7e)) {
			snprintf(said, said_size, " (country: not a code, %s)", layout);
			return EINVAL;
		}
		lck_mtx_lock(&apple80211_lock);
		bool changed = !country_known || memcmp(country_code, code, sizeof(country_code)) != 0;
		memcpy(country_code, code, sizeof(country_code));
		country_known = true;
		lck_mtx_unlock(&apple80211_lock);
		snprintf(said, said_size, " (country: %c%c, %s, %s)", code[0], code[1], layout, changed ? "kept" : "same");
		return 0;
	}
	case 10:
		return set_scan(request, said, said_size);
	case 25:
		return ap_set_host_ap_mode(request, said, said_size);
	default:
		return EOPNOTSUPP;
	}
}

/* ---- events -------------------------------------------------------------------------------------------- */

static int
post_if(const char *ifname, uint32_t code, const void *data, uint32_t length)
{
	struct kev_msg message = { .vendor_code = KEV_VENDOR_APPLE, .kev_class = KEV_IEEE80211_CLASS, .kev_subclass = 1,
		                   .event_code = code };
	static unsigned failed;
	char name[16] = { 0 };
	int error;

	strlcpy(name, ifname, sizeof(name));
	message.dv[0].data_length = sizeof(name);
	message.dv[0].data_ptr = name;
	if (length != 0) {
		message.dv[1].data_length = length;
		message.dv[1].data_ptr = __DECONST(void *, data);
	}
	error = kev_post_msg(&message);
	/* (an event that cannot be posted is never seen by wifid: say so, a few times) */
	if (error != 0 && failed < 20) {
		failed++;
		printf("FUXI WIFI IOS: apple80211: %s event %u of %u bytes not posted: error %d\n", name, code, length, error);
	}
	return error;
}

static int
post(uint32_t code, const void *data, uint32_t length)
{
	return post_if(interface_name, code, data, length);
}

/*
 * How strongly the network joined is heard, as a driver says it of itself every few seconds: wifid
 * reads no strength of its own accord, and without it the status bar shows a joined network with
 * no bars at all. True while there is a network to speak of.
 */
static bool
rssi_tell(void)
{
	struct fuxi_wifi_sta_state *state = kalloc_data(sizeof(*state), Z_WAITOK | Z_ZERO);
	bool joined;

	if (state == NULL) {
		return true;                    /* (try again next time) */
	}
	fuxi_wifi_sta_state(state);
	joined = state->phase == FUXI_WIFI_STA_JOINED;
	if (joined && state->bss.rssi < 0) {
		struct apple80211_rssi_changed rssi = { .rssi_known = 1, .rssi = state->bss.rssi, .per_core_known = 1,
			                                .per_core = { (int8_t)state->bss.rssi, (int8_t)state->bss.rssi }, .snr_known = 1,
			                                .snr = (int16_t)(state->bss.rssi - NOISE_FLOOR), .noise_known = 1,
			                                .noise = NOISE_FLOOR };
		static unsigned told;
		int error = post(EVENT_RSSI_CHANGED, &rssi, sizeof(rssi));

		/* (the first few are said, so a run shows that wifid was told at all) */
		if (told < 3) {
			told++;
			printf("FUXI WIFI IOS: apple80211: RSSI_CHANGED %d dBm (noise %d) posted: error %d\n", state->bss.rssi,
			    NOISE_FLOOR, error);
		}
	}
	kfree_data(state, sizeof(*state));
	return joined;
}

static thread_call_t rssi_call;
static bool rssi_running;

static void
rssi_again(void)
{
	uint64_t deadline;

	clock_interval_to_deadline(RSSI_EVERY_SECONDS, NSEC_PER_SEC, &deadline);
	thread_call_enter_delayed(rssi_call, deadline);
}

static void
rssi_tick(thread_call_param_t param0 __unused, thread_call_param_t param1 __unused)
{
	bool joined = rssi_tell();

	lck_mtx_lock(&apple80211_lock);
	if (joined && rssi_running) {
		rssi_again();
	} else {
		rssi_running = false;
	}
	lck_mtx_unlock(&apple80211_lock);
}

/* A network is joined: say how well it is heard now, and from now on until it is left. */
static void
rssi_start(void)
{
	(void)rssi_tell();
	lck_mtx_lock(&apple80211_lock);
	if (rssi_call == NULL) {
		rssi_call = thread_call_allocate(rssi_tick, NULL);
	}
	if (rssi_call != NULL && !rssi_running) {
		rssi_running = true;
		rssi_again();
	}
	lck_mtx_unlock(&apple80211_lock);
}

/* What the station side says has happened (bsd/dev/fuxi_wifi_sta.c calls this, on its thread or the station's). */
void
fuxi_wifi_apple80211_event(enum fuxi_wifi_sta_event event, int status)
{
	static unsigned said;

	if (said < 200) {
		said++;
		printf("FUXI WIFI IOS: apple80211: the station says: %s, %d\n", event == FUXI_WIFI_STA_POWER ? "power" : event == FUXI_WIFI_STA_SCAN_DONE ?
		    "a scan is over" : event == FUXI_WIFI_STA_JOIN_DONE ? "a join is over" : "the link", status);
	}
	switch (event) {
	case FUXI_WIFI_STA_POWER:
		post(EVENT_POWER_CHANGED, NULL, 0);
		break;
	case FUXI_WIFI_STA_SCAN_DONE:
		lck_mtx_lock(&apple80211_lock);
		result_next = 0;
		lck_mtx_unlock(&apple80211_lock);
		post(EVENT_SCAN_DONE, NULL, 0);
		break;
	case FUXI_WIFI_STA_JOIN_DONE:
		lck_mtx_lock(&apple80211_lock);
		join_result = status == 0 ? 1 : status == EAUTH ? 21 : status == ETIMEDOUT ? 9 : 2;
		lck_mtx_unlock(&apple80211_lock);
		post(EVENT_ASSOC_DONE, NULL, 0);
		break;
	case FUXI_WIFI_STA_LINK: {
		struct apple80211_link_changed link = { .down = status != 1, .involuntary = status != 1 && status != 0,
			                                .reason = status != 1 && status != 0 ? 4 : 0 };      /* (4: gone, not asked) */
		struct fuxi_wifi_sta_state *state = kalloc_data(sizeof(*state), Z_WAITOK | Z_ZERO);

		if (state != NULL) {
			fuxi_wifi_sta_state(state);
			link.rssi = status == 1 ? state->bss.rssi : 0;
			link.snr = status == 1 ? (uint16_t)(state->bss.rssi - NOISE_FLOOR) : 0;
			link.noise = status == 1 ? (uint16_t)NOISE_FLOOR : 0;
			kfree_data(state, sizeof(*state));
		}
		post(EVENT_SSID_CHANGED, NULL, 0);
		post(EVENT_BSSID_CHANGED, NULL, 0);
		post(EVENT_LINK_CHANGED, &link, sizeof(link));
		if (status == 1) {
			rssi_start();
		} else {
			lck_mtx_lock(&apple80211_lock);
			rssi_running = false;           /* (the one pending finds no network and stops) */
			lck_mtx_unlock(&apple80211_lock);
		}
		break;
	}
	}
}

/* ---- the requests as they arrive --------------------------------------------------------------------- */

/*
 * How often each kind of request came and how it fared: said when the run ends, because the
 * lines said along the way can be lost with the middle of a long log.
 */
static struct {
	int32_t type;
	bool set;
	unsigned times, answered;
	int last_error;
} kinds[96];
static unsigned kind_count;

static void
note(const struct apple80211req *request, bool is_set, int error)
{
	unsigned i;

	lck_mtx_lock(&apple80211_lock);
	for (i = 0; i < kind_count && !(kinds[i].type == request->req_type && kinds[i].set == is_set); i++) {
	}
	if (i < sizeof(kinds) / sizeof(kinds[0])) {
		if (i == kind_count) {
			kinds[kind_count++] = (typeof(kinds[0])) { .type = request->req_type, .set = is_set };
		}
		kinds[i].times++;
		kinds[i].answered += error == 0;
		kinds[i].last_error = error != 0 ? error : kinds[i].last_error;
	}
	lck_mtx_unlock(&apple80211_lock);
}

void
fuxi_wifi_apple80211_summary(void)
{
	static bool said;

	if (said) {
		return;
	}
	said = true;
	for (unsigned i = 0; i < kind_count; i++) {
		printf("FUXI WIFI IOS: apple80211: in this run: %s %d (%s) %u times, answered %u times%s", kinds[i].set ? "set" : "get", kinds[i].type,
		    type_name(kinds[i].type), kinds[i].times, kinds[i].answered, kinds[i].last_error == 0 ? "\n" : "");
		if (kinds[i].last_error != 0) {
			printf("; the last refusal: %d%s\n", kinds[i].last_error, kinds[i].last_error == EOPNOTSUPP ? " (not built)" : "");
		}
	}
}

extern void sweet_prof_user_bt(const char *tag);

static void
trace_unsupported_request(int32_t type, bool is_set, int error, const char *caller)
{
	static unsigned traces;
	int enabled = 0;

	if (!is_set || type != 383 || error != EOPNOTSUPP || strcmp(caller, "wifid") ||
	    !fuxi_wifi_ios_native_ready()) {
		return;
	}
	PE_parse_boot_argn("fuxiwifitrace", &enabled, sizeof(enabled));
	if (enabled != 1) {
		return;
	}
	lck_mtx_lock(&apple80211_lock);
	bool allowed = traces < 2;
	if (allowed) {
		traces++;
	}
	lck_mtx_unlock(&apple80211_lock);
	if (allowed) {
		sweet_prof_user_bt("FUXI WIFI IOC383");
	}
}

/* An Apple80211 request for the radio's interface, from the process that is running now. */
errno_t
fuxi_wifi_apple80211_ioctl(u_long command, void *data)
{
	static unsigned said_gets, said_sets;
	struct apple80211req *request = data;
	char who[32] = "?", what[200] = "";
	bool is_set = command == SIOCSA80211;
	int error;

	if (command != SIOCSA80211 && command != SIOCGA80211) {
		return EOPNOTSUPP;
	}
	bool is_ap = fuxi_wifi_ap_enabled() &&
	    strncmp(request->req_if_name, ap_interface_name, sizeof(ap_interface_name)) == 0;
	if (!is_ap && strncmp(request->req_if_name, interface_name, sizeof(interface_name)) != 0) {
		return ENXIO;
	}
	if (is_set && proc_suser(current_proc()) != 0) {
		return EPERM;
	}
	proc_selfname(who, sizeof(who));
	if (is_ap) {
		error = is_set ? ap_set(request, what, sizeof(what)) : ap_get(request);
		if (!is_set && error == -1) {
			error = get(request);
		}
	} else {
		error = is_set ? set(request, what, sizeof(what)) : get(request);
	}
	note(request, is_set, error);
	/*
	 * (a request that is repeated for ever is said a few hundred times; sets — scans, power, joins,
	 * the country — come far less often and matter late in a run too, so they get their own allowance)
	 */
	unsigned *said = is_set ? &said_sets : &said_gets;
	bool force_ap_log = is_ap && (fuxi_wifi_ap_options() & FUXI_WIFI_AP_VERBOSE);
	if (force_ap_log || *said < (is_set ? 3000u : 800u)) {
		if (!force_ap_log) {
			(*said)++;
		}
		printf("FUXI WIFI IOS: apple80211%s: %s %s %d (%s), value %d, %u bytes%s: %s",
		    is_ap ? "[ap1]" : "", who, is_set ? "sets" : "gets", request->req_type,
		    type_name(request->req_type), request->req_val, request->req_len, what, error == 0 ? "answered\n" : error ==
		    EOPNOTSUPP ? "REFUSED\n" : "");
		if (error != 0 && error != EOPNOTSUPP) {
			printf("error %d\n", error);
		}
	}
	trace_unsupported_request(request->req_type, is_set, error, who);
	return error;
}

#endif /* FUXI */
