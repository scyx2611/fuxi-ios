#ifndef FUXI_WLAN_ATH12K_H
#define FUXI_WLAN_ATH12K_H
#include <stdint.h>
#include <stddef.h>
struct lx_platform {
	uint64_t (*now_ms)(void);
	void (*sleep_ms)(unsigned ms);
	void (*poll)(void);
	void (*log)(int level, const char *line);
	void (*halt)(void);
	void *(*alloc)(size_t bytes);
	void (*free)(void *memory);
	void *(*dma_alloc)(size_t bytes, uint64_t *bus);
	void (*dma_free)(size_t bytes, void *memory, uint64_t bus);
	uint64_t (*dma_map)(void *memory, size_t bytes, int direction);
	void (*dma_sync)(uint64_t bus, size_t bytes, int direction);
	int (*fw_get)(const char *name, const uint8_t **data, size_t *bytes);
	uint64_t (*now_ns)(void);
	uint64_t (*boottime_ns)(void);
	void (*busy_delay_us)(unsigned us);
	void (*preempt_disable)(void);
	void (*preempt_enable)(void);
};
struct nw_board {
	void *bar;
	size_t bar_len;
	uint16_t device_id, msi_cap;
	int irq_base;
	int (*cfg_read)(int offset, int size, uint32_t *value);
	int (*cfg_write)(int offset, int size, uint32_t value);
	int (*msi_target)(uint32_t *low, uint32_t *high, uint32_t *data);
	void *(*map_phys)(uint64_t address, size_t bytes);
	unsigned diagnostics;
};
struct nw_wifi_stats {
	uint32_t stage, scans, frames, beacons, bss_seen;
	int last_err;
};
struct nw_bss_info {
	uint8_t bssid[6], ssid_len, ssid[32];
	uint32_t freq_mhz;
	int8_t signal_dbm;
	uint16_t caps, beacon_int;
	uint32_t age_ms;
	uint16_t ie_len;
	uint8_t ie[384];
};
struct nw_control_transport {
	void *context;
	int (*send)(void *context, uint64_t session, const void *bytes, size_t length);
	void (*wake)(void *context);
};
struct nw_wifi_status;
/* Frames for the host stack: with only rx set, the module hands over each frame whole, on its worker. */
struct nw_wifi_events {
	void (*scan_done)(void);
	void (*connect_done)(int result);
	void (*link_lost)(int reason);
	void (*rx)(const uint8_t *eth, size_t length);
	void (*rx_flush)(void);
	void (*rx_packet)(void *context, size_t length, int (*reader)(void *, size_t, void *, size_t));
	void (*link_update)(const struct nw_wifi_status *status);
};
extern void nw_wifi_set_platform(const struct lx_platform *);
extern void nw_wifi_set_events(const struct nw_wifi_events *);
/* 0: queued (copied); only while the port opened by SET_PORT is up. 14..2000 bytes, Ethernet framed. */
extern int nw_wifi_tx(const uint8_t *, size_t);
extern int nw_wifi_control_transport_set(const struct nw_control_transport *);
extern int nw_wifi_control_attach(uint64_t *);
extern int nw_wifi_control_receive(uint64_t, size_t, int (*)(void *, void *, size_t), void *);
extern int nw_wifi_start(const struct lx_platform *, const struct nw_board *);
extern int nw_wifi_step(void);
extern void nw_wifi_link_state(char *, int);
extern void nw_wifi_stop(void);
extern void nw_wifi_poll(void);
extern void nw_wifi_irq_mode(int);
extern void nw_wifi_power(int);
extern int nw_wifi_scan(const uint8_t *, size_t);
extern int nw_wifi_bss_count(void);
extern int nw_wifi_bss_get(int, struct nw_bss_info *);
extern struct nw_wifi_stats nw_wifi_stats;

/* SoftAP (NL80211_IFTYPE_AP) entry points from fuxi_ath12k.o (must match include/nw_ap.h). */
struct nw_ap_config {
	uint8_t  mac[6];
	uint8_t  ssid[32];
	uint8_t  ssid_len;
	uint8_t  hidden;
	uint8_t  wpa2;
	uint8_t  flags;
	char     country[2];
	uint8_t  reserved[2];
	uint32_t freq_mhz;
	uint16_t extra_ie_len;
	uint8_t  extra_ie[1024];
};
struct nw_ap_events {
	void (*rx)(const uint8_t *eth, size_t len);
	void (*sta_arrive)(const uint8_t *mac, const uint8_t *rsn_ie, size_t rsn_ie_len);
	void (*sta_leave)(const uint8_t *mac, uint16_t reason);
};
extern void nw_wifi_ap_set_events(const struct nw_ap_events *);
extern int  nw_wifi_ap_start(const struct nw_ap_config *, uint32_t *);
extern int  nw_wifi_ap_stop(void);
extern int  nw_wifi_ap_set_key(const uint8_t *, uint8_t, uint32_t, const uint8_t *, size_t, const uint8_t *, size_t, int);
extern int  nw_wifi_ap_get_key_seq(const uint8_t *, uint8_t, uint8_t[8]);
extern int  nw_wifi_ap_sta_authorize(const uint8_t *, int);
extern int  nw_wifi_ap_sta_deauth(const uint8_t *, uint16_t, int);
extern int  nw_wifi_ap_tx(const uint8_t *, size_t);

static inline int
fwa_mac_text(const void *property, size_t length, char text[13])
{
	if (!property || !text || length != 6) { return 1; }
	const uint8_t *address = property;
	if ((address[0] & 1) ||
	    !(address[0] | address[1] | address[2] | address[3] | address[4] | address[5])) { return 1; }
	static const char hex[] = "0123456789abcdef";
	for (unsigned index = 0; index < 6; index++) {
		text[index * 2] = hex[address[index] >> 4];
		text[index * 2 + 1] = hex[address[index] & 15];
	}
	text[12] = 0;
	return 0;
}

static int
fwa_config_offset(uint32_t encoded, unsigned size, unsigned express, unsigned *offset)
{
	if (!offset || (size != 1 && size != 2 && size != 4)) { return 1; }
	uint32_t relative = encoded & 0x7fffffffu;
	if ((encoded & 0x80000000u) && (!express || relative > 0xfffu - express)) { return 1; }
	uint32_t absolute = relative + ((encoded & 0x80000000u) ? express : 0);
	if (absolute > 0x1000u - size || (absolute & (size - 1))) { return 1; }
	*offset = absolute;
	return 0;
}
#endif
