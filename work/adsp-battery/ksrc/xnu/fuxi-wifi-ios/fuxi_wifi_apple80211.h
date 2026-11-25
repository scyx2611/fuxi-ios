/*
 * Ported from HackiOS xnu/files/bsd/dev/redwood_apple80211.c (APSL-2.0)
 * Fuxi, 2026-10-23: shared diagnostic ABI; 16.7 payloads UNVERIFIED.
 * See LICENSE.HackiOS and docs-lanes/wifi-ios.md for provenance/confidence.
 */
#ifndef FUXI_WIFI_APPLE80211_H
#define FUXI_WIFI_APPLE80211_H
#include <stdint.h>
#include <stddef.h>
#define SIOCSA80211             0x802869c8u     /* _IOW('i', 200, struct apple80211req) */
#define SIOCGA80211             0xc02869c9u     /* _IOWR('i', 201, struct apple80211req) */

struct apple80211req {
	char req_if_name[16];
	int32_t req_type;
	int32_t req_val;
	uint32_t req_len;
	uint64_t req_data __attribute__((aligned(8)));
};

_Static_assert(sizeof(struct apple80211req) == 40 && offsetof(struct apple80211req, req_data) == 32,
    "arm64 Apple80211 request envelope (16.7 consumer unverified)");

/* ---- the layouts ------------------------------------------------------------------------------------------ */

#define CHANNEL_20MHZ           0x2u
#define CHANNEL_2GHZ            0x8u
#define CHANNEL_5GHZ            0x10u
#define CHANNEL_ACTIVE          0x80u           /* the radio may send there */
#define CHANNEL_DFS             0x100u
#define CHANNEL_80MHZ           0x400u

struct apple80211_channel {
	uint32_t version, channel, flags;
};

/* SUPPORTED_CHANNELS (27) and HW_SUPPORTED_CHANNELS (254): 4824 bytes */
struct apple80211_channels {
	uint32_t version, count;
	struct apple80211_channel channels[401];
	uint32_t unused;
};

/* CHANNELS_INFO (207): 10016 bytes; of each channel a byte in each of several rows, and a word of flags */
struct apple80211_channels_info {
	uint8_t unknown_0[12];
	uint16_t count;
	uint8_t unknown_1[800];
	uint8_t number[400], indoor[400], dfs[400], passive[400], wide_40[400], wide_80[400];
	uint8_t unknown_2[1602];
	uint32_t flags[400];
	uint8_t unknown_3[3600];
} __attribute__((packed));

/* SCAN_REQ (10): 5456 bytes */
struct apple80211_scan_request {
	uint32_t version, bss_type;
	uint8_t bssid[6], unused_0[2];
	uint32_t ssid_length;
	uint8_t ssid[32];
	uint32_t scan_type;                     /* 1: asking, 2: listening only */
	uint32_t phy_mode;
	uint16_t dwell_ms, unused_1;
	uint32_t rest_ms;
	uint32_t channel_count;
	struct apple80211_channel channels[400];
	uint64_t flags;
	uint32_t unused_2;
	uint32_t name_count;                    /* networks asked for by name, each in 56 bytes */
	uint8_t names[10][56];
	uint32_t short_ssid, unused_3;
};

/* SCAN_RESULT (11) and CURRENT_NETWORK (103): 1164 bytes */
struct apple80211_scan_result {
	uint32_t version;
	struct apple80211_channel channel;
	uint16_t noise_known;
	int16_t noise, snr, rssi;
	uint16_t beacon_interval, capability;
	uint8_t bssid[6];
	uint8_t rate_count, unused_0;
	uint32_t rates[15];                     /* Mbit/s */
	uint8_t ssid_length, ssid[32], unused_1[3];
	uint32_t age_ms;
	uint8_t flags, unused_2;
	uint16_t elements_length;               /* comes in as the room there is */
	uint8_t elements[1024];
};

/* ASSOCIATE (20): 592 bytes; of what follows the key (the RSN element wifid built, flags) nothing is used */
struct apple80211_assoc {
	uint32_t version;
	uint16_t mode, auth_lower, auth_upper, unused_0;
	uint32_t ssid_length;
	uint8_t ssid[32];
	uint8_t bssid[6], unused_1[2];
	uint32_t key_version, key_length, key_cipher;   /* cipher 10: a passphrase as typed; 6: the key (from 64 hex digits) */
	uint16_t key_flags, key_index;
	uint8_t key[64];
	uint8_t rest[0x250 - 0x88];
};

#define CIPHER_PMK              6
#define CIPHER_PASSPHRASE       10

/* the link event's data (at least 32 bytes, or the framework drops the event) */
struct apple80211_link_changed {
	uint8_t down, unused_0[3];
	int32_t rssi;
	uint16_t snr, noise;
	uint8_t unused_1[4];
	uint8_t involuntary, unused_2[3];
	uint32_t reason, subreason;
	uint8_t unused_3[4];
};

/*
 * RSSI_CHANGED's data (event 39): IO80211.framework of 16.5 drops one shorter than 180 bytes and
 * makes a dictionary of the parts whose flag is set ("RSSI", "PER_CORE_RSSI", "SNR", "NOISE", "CCA");
 * wifid computes the status bar's strength from "RSSI" and ignores one that comes within a second.
 */
struct apple80211_rssi_changed {
	uint8_t rssi_known, unused_0[3];
	int32_t rssi;                           /* dBm */
	uint8_t per_core_known;
	int8_t per_core[2];                     /* dBm, -128: no such chain */
	uint8_t snr_known;
	int16_t snr;                            /* dB */
	uint8_t noise_known, unused_1;
	int16_t noise;                          /* dBm */
	uint8_t cca_known;
	int8_t cca;
	uint8_t unused_2[180 - 0x14];           /* what was sent and how fast: not said */
};

_Static_assert(sizeof(struct apple80211_rssi_changed) == 180 && offsetof(struct apple80211_rssi_changed, snr) == 0xc &&
    offsetof(struct apple80211_rssi_changed, noise) == 0x10 && offsetof(struct apple80211_rssi_changed, cca) == 0x13,
    "the RSSI event as iOS 16.5 reads it");
_Static_assert(sizeof(struct apple80211_assoc) == 592 && offsetof(struct apple80211_assoc, key) == 0x48,
    "the association request as iOS 16.5 has it");
_Static_assert(sizeof(struct apple80211_link_changed) == 32 && offsetof(struct apple80211_link_changed, reason) == 0x14,
    "the link event as iOS 16.5 reads it");
_Static_assert(sizeof(struct apple80211_channels) == 4824, "the channel list as iOS 16.5 has it");
_Static_assert(sizeof(struct apple80211_channels_info) == 10016 && offsetof(struct apple80211_channels_info, number) == 0x32e &&
    offsetof(struct apple80211_channels_info, flags) == 0x12d0, "the channels' description as iOS 16.5 has it");
_Static_assert(sizeof(struct apple80211_scan_request) == 5456 && offsetof(struct apple80211_scan_request, channels) == 0x48 &&
    offsetof(struct apple80211_scan_request, flags) == 0x1308 && offsetof(struct apple80211_scan_request, names) == 0x1318,
    "the scan request as iOS 16.5 has it");
_Static_assert(sizeof(struct apple80211_scan_result) == 1164 && offsetof(struct apple80211_scan_result, ssid_length) == 0x60 &&
    offsetof(struct apple80211_scan_result, age_ms) == 0x84 && offsetof(struct apple80211_scan_result, elements) == 0x8c,
    "a scan's result as iOS 16.5 has it");

/* ---- SoftAP (Personal Hotspot / ap1) layouts verified against iOS 16.7.16 20H392 IO80211/wifid/hostapd ---- */

/* HOST_AP_MODE (25): 1500 bytes (0x5dc) */
struct apple80211_apmode_data {
	uint32_t version;
	uint16_t mode;                  /* 2: SoftAP, 0x200: hidden SSID */
	uint16_t auth_lower;
	uint16_t auth_upper;            /* 0x8: WPA2-PSK */
	uint16_t unused_0;
	struct apple80211_channel channel;
	uint32_t ssid_len;
	uint8_t  ssid[32];
	uint32_t key_version;
	uint32_t key_len;
	uint32_t key_cipher;
	uint16_t key_flags;
	uint16_t key_index;
	uint8_t  key[64];
	uint8_t  unused_1[0xd0 - 0x8c];
	uint32_t phy_mode;
	uint8_t  unused_2[0x1d8 - 0xd4];
	uint32_t ie_len;
	uint8_t  ie[1024];
};

/* CIPHER_KEY (3) and KEY_RSC (78): 148 bytes (0x94); cipher 0=NONE, 3=TKIP, 5=AES_CCM/IGTK */
struct apple80211_key {
	uint32_t version;
	uint32_t key_len;
	uint32_t key_cipher_type;
	uint16_t key_flags;
	uint16_t key_index;
	uint8_t  key[64];
	uint32_t key_rsc_len;
	uint8_t  key_rsc[8];
	uint8_t  key_ea[6];
	uint8_t  unused[0x94 - 0x62];
};

/* STA_IE_LIST (73): 1040 bytes (0x410); hostapd expects RSN IE (0x30, len, ...) directly at +0x10 */
struct apple80211_sta_ie_list {
	uint32_t version;
	uint8_t  mac[6];
	uint8_t  unused[2];
	uint32_t ie_len;
	uint8_t  ie[1024];
};

/* STA_AUTHORIZE (74): 20 bytes (0x14) */
struct apple80211_sta_authorize {
	uint32_t version;
	uint32_t authorize;
	uint8_t  mac[6];
	uint8_t  unused[6];
};

/* STA_DISASSOCIATE (75) and STA_DEAUTH (76): 16 bytes (0x10) */
struct apple80211_sta_deauth {
	uint32_t version;
	uint32_t reason;
	uint8_t  mac[6];
	uint8_t  unused[2];
};

/* STATION_LIST (42): 2056 bytes (0x808) */
struct apple80211_station_entry {
	uint32_t version;
	uint8_t  mac[6];
	uint8_t  unused[2];
	uint32_t flags;
};

struct apple80211_station_list {
	uint32_t version;
	uint32_t count;
	struct apple80211_station_entry stations[128];
};

_Static_assert(sizeof(struct apple80211_apmode_data) == 1500 &&
    offsetof(struct apple80211_apmode_data, channel) == 0x0c &&
    offsetof(struct apple80211_apmode_data, ssid_len) == 0x18 &&
    offsetof(struct apple80211_apmode_data, key_len) == 0x40 &&
    offsetof(struct apple80211_apmode_data, key) == 0x4c &&
    offsetof(struct apple80211_apmode_data, phy_mode) == 0xd0 &&
    offsetof(struct apple80211_apmode_data, ie_len) == 0x1d8 &&
    offsetof(struct apple80211_apmode_data, ie) == 0x1dc,
    "HOST_AP_MODE payload as iOS 16.7.16 IO80211/wifid sends it");
_Static_assert(sizeof(struct apple80211_key) == 148 &&
    offsetof(struct apple80211_key, key_index) == 0x0e &&
    offsetof(struct apple80211_key, key) == 0x10 &&
    offsetof(struct apple80211_key, key_rsc) == 0x54 &&
    offsetof(struct apple80211_key, key_ea) == 0x5c,
    "CIPHER_KEY/KEY_RSC payload as iOS 16.7.16 hostapd uses it");
_Static_assert(sizeof(struct apple80211_sta_ie_list) == 1040 &&
    offsetof(struct apple80211_sta_ie_list, mac) == 0x04 &&
    offsetof(struct apple80211_sta_ie_list, ie_len) == 0x0c &&
    offsetof(struct apple80211_sta_ie_list, ie) == 0x10,
    "STA_IE_LIST payload as iOS 16.7.16 hostapd reads it");
_Static_assert(sizeof(struct apple80211_sta_authorize) == 20 &&
    offsetof(struct apple80211_sta_authorize, mac) == 0x08,
    "STA_AUTHORIZE payload as iOS 16.7.16 hostapd sends it");
_Static_assert(sizeof(struct apple80211_sta_deauth) == 16 &&
    offsetof(struct apple80211_sta_deauth, mac) == 0x08,
    "STA_DEAUTH/STA_DISASSOCIATE payload as iOS 16.7.16 hostapd sends it");
_Static_assert(sizeof(struct apple80211_station_list) == 2056 &&
    offsetof(struct apple80211_station_list, stations) == 0x08 &&
    offsetof(struct apple80211_station_entry, mac) == 0x04 &&
    offsetof(struct apple80211_station_entry, flags) == 0x0c,
    "STATION_LIST payload as iOS 16.7.16 IO80211 reads it");


#endif
