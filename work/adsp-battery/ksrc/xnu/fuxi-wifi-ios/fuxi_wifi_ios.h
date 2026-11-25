/* Fuxi Wi-Fi iOS shim: boot-lifetime backend boundary. */
#ifndef FUXI_WIFI_IOS_H
#define FUXI_WIFI_IOS_H
#include <stddef.h>
#include <sys/fuxi_wifi_sta.h>

#define FUXI_WIFI_FRAME_MAX 1514

/* No mbuf/IOKit objects cross this boundary. RX is copied before return.
 * link_state is called after association/key installation or on link loss.
 * Callbacks run in thread context; no callback may retain a frame pointer.
 */
struct fuxi_wifi_input {
	void (*rx_frame)(const uint8_t *frame, size_t length);
	void (*link_state)(bool up);
	/* A synchronous reader for borrowed fragments. End each worker batch with rx_flush. */
	void (*rx_packet)(void *packet, size_t length,
	    int (*read)(void *, size_t, void *, size_t));
	void (*rx_flush)(void);
	bool (*byte_counts)(uint64_t *received, uint64_t *sent);
};

/* Optional SoftAP (Personal Hotspot / ap1) boundary, opt-in via ADT /chosen fuxi,wifi-ap. */
#define FUXI_WIFI_AP_ENABLE      (1u << 0)
#define FUXI_WIFI_AP_VERBOSE     (1u << 1)
#define FUXI_WIFI_AP_HE          (1u << 2)
#define FUXI_WIFI_AP_PREFER_5GHZ (1u << 3)
#define FUXI_WIFI_AP_ALL         (FUXI_WIFI_AP_ENABLE | FUXI_WIFI_AP_VERBOSE | FUXI_WIFI_AP_HE | FUXI_WIFI_AP_PREFER_5GHZ)
#define FUXI_WIFI_AP_CFG_HE          0x01u
#define FUXI_WIFI_AP_CFG_PREFER_5GHZ 0x02u
#define FUXI_WIFI_AP_EXTRA_IE_MAX 1024

struct fuxi_wifi_ap_config {
	uint8_t  mac[6];
	uint8_t  ssid[32];
	uint8_t  ssid_len;
	uint8_t  hidden;
	uint8_t  wpa2;
	uint8_t  flags;      /* FUXI_WIFI_AP_CFG_* bitmask */
	char     country[2]; /* optional 2-char ISO country code (e.g. "TW"), or {0,0} */
	uint8_t  reserved[2];
	uint32_t freq_mhz;
	uint16_t extra_ie_len;
	uint8_t  extra_ie[FUXI_WIFI_AP_EXTRA_IE_MAX];
};

struct fuxi_wifi_ap_input {
	void (*rx_frame)(const uint8_t *frame, size_t length);
	void (*sta_arrive)(const uint8_t *mac, const uint8_t *rsn_ie, size_t rsn_ie_len);
	void (*sta_leave)(const uint8_t *mac, uint16_t reason);
};

struct fuxi_wifi_ap_ops {
	uint8_t address[6];
	void (*start_cb)(const struct fuxi_wifi_ap_input *input);
	int  (*ap_start)(const struct fuxi_wifi_ap_config *cfg, uint32_t *actual_freq_mhz);
	int  (*ap_stop)(void);
	int  (*set_key)(const uint8_t *mac, uint8_t key_idx, uint32_t cipher,
	                const uint8_t *key, size_t key_len,
	                const uint8_t *seq, size_t seq_len, int is_delete);
	int  (*get_key_seq)(const uint8_t *mac, uint8_t key_idx, uint8_t seq_out[8]);
	int  (*sta_authorize)(const uint8_t *mac, int authorize);
	int  (*sta_deauth)(const uint8_t *mac, uint16_t reason, int disassoc);
	bool (*tx_frame)(const uint8_t *frame, size_t length);
};

/* One immutable backend for the boot. No detach/hot replacement. Station
 * callbacks are serialized by fuxi_wifi_sta; state() must synchronize its snapshot.
 * tx_frame can run concurrently with station callbacks and must copy before return.
 * start() only installs the input callbacks; it must not send events yet.
 * join() returns only after keys are installed; asynchronous loss also calls
 * fuxi_wifi_sta_lost(errno). A future WCN7850 backend owns its own EAPOL/key path,
 * using fuxi_wifi_wpa.h, and Ethernet decap/encap. No hardware is accessed here.
 */
struct fuxi_wifi_backend {
	struct fuxi_wifi_sta_ops station; /* power, scan request/results, connect/disconnect, state */
	bool (*tx_frame)(const uint8_t *frame, size_t length);
	void (*start)(const struct fuxi_wifi_input *input);
	const struct fuxi_wifi_ap_ops *ap; /* optional SoftAP ops; NULL when unused */
};

#ifdef KERNEL
bool fuxi_wifi_ios_attach(const struct fuxi_wifi_backend *backend);
void fuxi_wifi_ios_init(void);
bool fuxi_wifi_ios_native_ready(void);
unsigned fuxi_wifi_ap_options(void);
bool fuxi_wifi_ap_enabled(void);
void fuxi_wifi_apple80211_ap_attach(const struct fuxi_wifi_ap_ops *ops);
void fuxi_wifi_apple80211_ap_sta_arrive(const uint8_t *mac, const uint8_t *rsn_ie, size_t rsn_ie_len);
void fuxi_wifi_apple80211_ap_sta_leave(const uint8_t *mac, uint16_t reason);
#endif
#endif
