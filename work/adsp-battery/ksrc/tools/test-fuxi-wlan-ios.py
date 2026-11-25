import hashlib
import hmac
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'xnu/fuxi-wlan'
IOS = ROOT / 'xnu/fuxi-wifi-ios'
# RFC 3394 AES key wrap of the group key KDE (dd 16 00 0f ac 01 01 00 + bytes 0..15) under the KEK of
# the vectors below, made once by an independent implementation (python cryptography, as in
# tools/test-fuxi-wifi-ios.py). The bridge's WPA code has to unwrap it and pass the integrity check.
WRAPPED = bytes.fromhex('0e6dd8c03700d3cc86d5782a84027cba9ac4a2a20343701481c88897eb4e2eaa')

MODEL = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#define KERNEL 1
#define EAUTH 80
#define THREAD_UNINT 0
#define THREAD_TIMED_OUT 1
#define LCK_SLEEP_DEFAULT 0
#define NSEC_PER_MSEC 1000000
#define MIN(left,right) ((left)<(right)?(left):(right))
#define LCK_GRP_DECLARE(name,text) int name __attribute__((unused))
#define LCK_MTX_DECLARE(name,group) pthread_mutex_t name = PTHREAD_MUTEX_INITIALIZER
#define lck_mtx_lock pthread_mutex_lock
#define lck_mtx_unlock pthread_mutex_unlock
typedef void *event_t;
typedef void *thread_t;
typedef int wait_result_t;
#define KERN_SUCCESS 0
#define THREAD_CONTINUE_NULL NULL
static uint64_t fake_now = 1;
static uint64_t wla_now_ms(void) { return fake_now; }
static void wl_sleep(unsigned milliseconds) { (void)milliseconds; }
static int thread_terminate(thread_t thread) { (void)thread; return 0; }
static thread_t current_thread(void) { return NULL; }
static void thread_block(void *continuation) { (void)continuation; }
static void thread_deallocate(thread_t thread) { (void)thread; }
static unsigned threads_started;
static int kernel_thread_start(void (*entry)(void *,int),void *context,thread_t *thread) {
    (void)entry; (void)context; *thread=NULL; threads_started++; return 0;
}
static int test_mode;
static int test_dwell;
static int test_cache;
static int test_batch;
static uint32_t offered_features;
static char test_ssid[33];
static int PE_parse_boot_argn(const char *name,void *value,int length) {
    if (!strcmp(name,"fuxiwlandwell")) { assert(length==4); *(int *)value=test_dwell; return test_dwell!=0; }
    if (!strcmp(name,"fuxiwlancache")) { assert(length==4); *(int *)value=test_cache; return test_cache!=0; }
    if (!strcmp(name,"fuxiwlanbatch")) { assert(length==4); *(int *)value=test_batch; return test_batch!=0; }
    if (!strcmp(name,"fuxiwifitest")) { assert(length==4); *(int *)value=test_mode; return 1; }
    assert(!strcmp(name,"fuxiwifitestssid") && length==32);
    memcpy(value,test_ssid,32); return test_ssid[0]!=0;
}
static void read_random(void *buffer,unsigned length) {
    for (unsigned index=0;index<length;index++) ((uint8_t *)buffer)[index]=(uint8_t)index;
}
bool fuxi_wifi_sta_busy(void) { return false; }
static bool test_powered = true;
static bool test_off_pending;
bool fuxi_wifi_sta_powered(void) { return test_powered; }
bool fuxi_wifi_sta_power_off_pending(void) { return test_off_pending; }
static unsigned test_ux;                        /* ADT fuxi,wifi-ux；bootstrap() 時讀進 wli.ux */
unsigned fuxi_wifi_ux(void) { return test_ux; }
static bool test_ap_enabled;
bool fuxi_wifi_ap_enabled(void) { return test_ap_enabled; }
int fuxi_wifi_sta_ask_power(bool on) { (void)on; return 0; }
struct fuxi_wifi_sta_scan;
struct fuxi_wifi_sta_bss;
struct fuxi_wifi_sta_join;
int fuxi_wifi_sta_ask_scan(const struct fuxi_wifi_sta_scan *scan) { (void)scan; return 0; }
int fuxi_wifi_sta_ask_join(const struct fuxi_wifi_sta_join *join) { (void)join; return 0; }
int fuxi_wifi_sta_ask_leave(void) { return 0; }
bool fuxi_wifi_sta_heard(unsigned index,struct fuxi_wifi_sta_bss *bss) {
    (void)index; (void)bss; return false;
}
static unsigned lost_calls;
static int lost_error;
void fuxi_wifi_sta_lost(int error) { lost_calls++; lost_error=error; }
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
static bool timeout_once;
static void thread_wakeup(event_t event) { (void)event; pthread_cond_broadcast(&condition); }
static void clock_interval_to_deadline(unsigned interval, unsigned scale, uint64_t *deadline) {
    *deadline = (uint64_t)interval * scale;
}
static int lck_mtx_sleep_deadline(pthread_mutex_t *mutex, int flags, event_t event,
                                 int interruptible, uint64_t deadline) {
    (void)flags; (void)event; (void)interruptible; (void)deadline;
    if (timeout_once) { timeout_once = false; return THREAD_TIMED_OUT; }
    struct timespec until;
    clock_gettime(CLOCK_REALTIME, &until); until.tv_sec += 3;
    return pthread_cond_timedwait(&condition, mutex, &until) ? THREAD_TIMED_OUT : 0;
}
#include "fuxi_wlan_ath12k.h"
#include "fuxi_wlan_control.h"
static uint8_t captured[2048];
static size_t captured_length;
static unsigned attachments, power_calls, requested_power;
static bool ready = true;
static int wla_control_read(void *context, void *destination, size_t length) {
    memcpy(destination, context, length); return 0;
}
int nw_wifi_control_receive(uint64_t session, size_t length,
                           int (*reader)(void *,void *,size_t),void *context) {
    assert(session == 9 && length <= sizeof(captured));
    captured_length = length; return reader(context,captured,length);
}
void nw_wifi_power(int on) { power_calls++; requested_power = on; }
int nw_wifi_bss_count(void) { return 3; }
int nw_wifi_bss_get(int index, struct nw_bss_info *out) {
    memset(out,0,sizeof(*out)); out->freq_mhz = index ? 5180 : 2412;
    out->ssid_len = index == 2 ? 33 : 4; memcpy(out->ssid,"real",4);
    out->signal_dbm = -56; out->ie_len = 4; out->ie[0] = 1; out->ie[1] = 2;
    out->caps = 0x11; out->beacon_int = 100; return 0;
}
static const struct nw_wifi_events *events;
void nw_wifi_set_events(const struct nw_wifi_events *installed) { events = installed; }
static unsigned tx_calls;
static int tx_status;
int nw_wifi_tx(const uint8_t *frame, size_t length) {
    assert(frame && length >= 14 && length <= 2000); tx_calls++; return tx_status;
}
static unsigned phy_reads;
void nw_wifi_link_state(char *text,int size) { phy_reads++; snprintf(text,(size_t)size,"fixture rate"); }
bool fuxi_wifi_ios_native_ready(void) { return ready; }
static unsigned ap_start_calls, ap_stop_calls;
static int ap_start_status;
void nw_wifi_ap_set_events(const struct nw_ap_events *ev) { (void)ev; }
int nw_wifi_ap_start(const struct nw_ap_config *cfg, uint32_t *freq) {
    (void)cfg; ap_start_calls++; if (freq) *freq = 2437; return ap_start_status;
}
int nw_wifi_ap_stop(void) { ap_stop_calls++; return 0; }
int nw_wifi_ap_set_key(const uint8_t *mac, uint8_t idx, uint32_t cipher,
    const uint8_t *key, size_t klen, const uint8_t *seq, size_t slen, int del) {
    (void)mac; (void)idx; (void)cipher; (void)key; (void)klen; (void)seq; (void)slen; (void)del; return 0;
}
int nw_wifi_ap_get_key_seq(const uint8_t *mac, uint8_t idx, uint8_t out[8]) {
    (void)mac; (void)idx; memset(out, 0, 8); return 0;
}
int nw_wifi_ap_sta_authorize(const uint8_t *mac, int auth) { (void)mac; (void)auth; return 0; }
int nw_wifi_ap_sta_deauth(const uint8_t *mac, uint16_t reason, int disassoc) {
    (void)mac; (void)reason; (void)disassoc; return 0;
}
int nw_wifi_ap_tx(const uint8_t *eth, size_t len) { (void)eth; (void)len; return 0; }
'''

TEST = r'''
enum { MESSAGE = 2048 };
static const uint8_t station[6]={2,0x46,0x55,0x58,0x49,1};
static const uint8_t ap[6]={2,0x46,0x55,0x58,0x49,2}, open_ap[6]={2,0x46,0x55,0x58,0x49,3};
static const uint8_t sae_ap[6]={2,0x46,0x55,0x58,0x49,4}, mfp_ap[6]={2,0x46,0x55,0x58,0x49,5};
static const uint8_t far_ap[6]={2,0x46,0x55,0x58,0x49,6}, weak_ap[6]={2,0x46,0x55,0x58,0x49,7};
static const uint8_t own_rsn[22]={48,20,1,0,0,0x0f,0xac,4,1,0,0,0x0f,0xac,4,1,0,0,0x0f,0xac,2,0,0};
static unsigned rx_frames, link_ups, link_downs;
static unsigned rx_packets,rx_flushes;
static uint64_t rx_octets,tx_octets;
static void input_rx(const uint8_t *frame,size_t length) { assert(frame && length>=14); rx_frames++; }
static void input_link(bool up) { if (up) link_ups++; else link_downs++; }
static void input_rx_packet(void *source,size_t length,int (*read)(void *,size_t,void *,size_t)) {
    uint8_t bytes[60]; assert(length==sizeof(bytes) && !read(source,0,bytes,length) && bytes[0]==0xa5);
    rx_packets++;
}
static void input_rx_flush(void) { rx_flushes++; }
static bool input_byte_counts(uint64_t *rx,uint64_t *tx) { *rx=rx_octets; *tx=tx_octets; return true; }
static const struct fuxi_wifi_input input={
    .rx_frame=input_rx,.link_state=input_link,.rx_packet=input_rx_packet,.rx_flush=input_rx_flush,
    .byte_counts=input_byte_counts,
};
bool fuxi_wifi_ios_attach(const struct fuxi_wifi_backend *backend) {
    assert(backend == &wli_backend && !memcmp(backend->station.address,station,6));
    assert(wli.info && wli.channels && !wli_state.last_error && backend->station.join==wli_join);
    attachments++; backend->start(&input); return true;
}
static void reset_peer(void) {
    wli_forget();
    memset(&wli,0,sizeof(wli)); memset(&wli_state,0,sizeof(wli_state));
    memset(&wli_backend,0,sizeof(wli_backend)); memset(&wli_target,0,sizeof(wli_target));
    wli.session = 9; wli_bss_count = wli_channel_count = 0; wli_input = NULL; events = NULL;
    captured_length = 0; power_calls = attachments = 0; fake_now = 1;
    ap_start_calls = ap_stop_calls = 0; ap_start_status = 0;
}
static size_t header(uint8_t *bytes, unsigned kind, unsigned operation, uint64_t generation) {
    memset(bytes,0,MESSAGE);
    fwc_write(bytes,0x3143574e,4); fwc_write(bytes+4,1,2);
    fwc_write(bytes+6,kind,2); fwc_write(bytes+8,operation,2);
    fwc_write(bytes+12,kind==2?wli.pending_sequence:0,4);
    fwc_write(bytes+24,9,8); fwc_write(bytes+32,generation,8); return 40;
}
static int deliver(uint8_t *bytes,size_t length) {
    fwc_write(bytes+16,length,4); return wli_receive(NULL,9,bytes,length);
}
static size_t put32(uint8_t *bytes,size_t offset,unsigned attribute,uint32_t value) {
    uint8_t encoded[4]; fwc_write(encoded,value,4); return wli_put(bytes,offset,attribute,encoded,4);
}
static int last_heard_rssi;
static void count_heard(const struct fuxi_wifi_sta_bss *bss,void *context) {
    ++*(unsigned *)context; last_heard_rssi=bss->rssi;
}
static void bootstrap(void) {
    uint8_t bytes[MESSAGE], power[4]={1};
    reset_peer();
    assert(!wli_step(1) && fwc_read16(captured+8)==1);
    size_t length=header(bytes,2,1,2);
    length=wli_put(bytes,length,2,station,6); length=wli_put(bytes,length,32,power,4);
    if(offered_features) length=put32(bytes,length,28,offered_features);
    assert(!deliver(bytes,length) && wli.info && wli.generation==2);
    assert(!wli_step(2) && fwc_read16(captured+8)==11);
    uint8_t channels[64]={0};
    fwc_write(channels,2412,4); fwc_write(channels+4,2,4);
    fwc_write(channels+16,5180,4); fwc_write(channels+20,8,4);
    fwc_write(channels+28,1,4);
    fwc_write(channels+32,2417,4); fwc_write(channels+36,1,4);
    fwc_write(channels+48,5955,4); fwc_write(channels+60,3,4);
    length=header(bytes,2,11,2); length=wli_put(bytes,length,31,channels,sizeof(channels));
    assert(!deliver(bytes,length) && wli_channel_count==2);
    assert(wli_channels[0].flags==FUXI_WIFI_STA_CHANNEL_PASSIVE);
    assert(wli_channels[1].flags==FUXI_WIFI_STA_CHANNEL_RADAR);
    ready=false; assert(!wli_step(3) && !attachments);
    ready=true; assert(!wli_step(4) && attachments==1);
    assert(events && wli_input==&input);
    if (test_ux & FUXI_WIFI_UX_RX_BATCH) {
        assert(events->rx_packet && events->rx_flush && !events->rx);
    } else {
        assert(events->rx && !events->rx_packet);
    }
}
static atomic_bool caller_done;
static int caller_result;
static unsigned caller_operation;
static bool caller_on;
static struct fuxi_wifi_sta_scan caller_scan;
static struct fuxi_wifi_sta_join caller_join;
static void *caller(void *context) {
    (void)context;
    switch (caller_operation) {
    case 1: caller_result=wli_power(caller_on); break;
    case 2: caller_result=wli_scan(&caller_scan); break;
    case 3: caller_result=wli_join(&caller_join); break;
    default: caller_result=wli_leave(); break;
    }
    atomic_store(&caller_done,true); return NULL;
}
static void wait_busy(unsigned operation) {
    for(unsigned attempt=0;attempt<5000;attempt++) {
        pthread_mutex_lock(&wli_lock);
        bool busy=wli.busy && wli.operation==operation && !wli.submitted;
        pthread_mutex_unlock(&wli_lock);
        if(busy) return;
        struct timespec pause={.tv_nsec=1000000}; nanosleep(&pause,NULL);
    }
    assert(!"request not queued");
}
static pthread_t start_caller(unsigned operation,bool on) {
    caller_operation=operation; caller_on=on; atomic_store(&caller_done,false);
    pthread_t thread; assert(!pthread_create(&thread,NULL,caller,NULL)); return thread;
}
static pthread_t begin(unsigned operation,bool on) {
    pthread_t thread=start_caller(operation,on); wait_busy(operation); return thread;
}
static uint64_t now=100;
static void step(void) { assert(!wli_step(++now)); }
static void expect_request(unsigned operation) {
    assert(wli.pending_operation==operation && fwc_read16(captured+8)==operation);
}
static const uint8_t *request_attribute(unsigned attribute,size_t *size) {
    return fwc_attribute(captured,captured_length,attribute,size);
}
static uint32_t request_u32(unsigned attribute) {
    size_t size; const uint8_t *value=request_attribute(attribute,&size);
    assert(value && size==4); return fwc_read32(value);
}
static void request_bytes(unsigned attribute,const void *expected,size_t length) {
    size_t size; const uint8_t *value=request_attribute(attribute,&size);
    assert(value && size==length && !memcmp(value,expected,length));
}
static void respond(unsigned operation,unsigned status) {
    uint8_t bytes[MESSAGE]; size_t length=header(bytes,2,operation,wli.generation);
    fwc_write(bytes+20,status,4); assert(!deliver(bytes,length));
}
static void power_test(bool on,uint64_t generation) {
    uint8_t bytes[MESSAGE], value[4]={0}; pthread_t thread=begin(1,on);
    step(); assert(requested_power==on && !atomic_load(&caller_done));
    size_t length=header(bytes,3,0x10a,generation); fwc_write(value,on,4);
    length=wli_put(bytes,length,32,value,4);
    assert(!deliver(bytes,length)); assert(!pthread_join(thread,NULL));
    assert(!caller_result && wli.generation==generation);
    assert(wli_state.phase==(on?FUXI_WIFI_STA_IDLE:FUXI_WIFI_STA_OFF));
}

/* Access points as GET_BSS describes them: every element the radio heard. */
struct fixture { const uint8_t *bssid; uint32_t frequency; int32_t signal; uint32_t caps; uint8_t elements[1600]; size_t length; };
static struct fixture fixtures[6];
static size_t element(uint8_t *out,size_t at,unsigned id,const void *body,size_t length) {
    out[at]=(uint8_t)id; out[at+1]=(uint8_t)length; memcpy(out+at+2,body,length); return at+2+length;
}
static void make_fixture(struct fixture *fixture,const uint8_t *bssid,const char *ssid,uint32_t frequency,
                         int32_t signal,unsigned caps,int akm,unsigned rsn_caps,bool crowded) {
    memset(fixture,0,sizeof(*fixture));
    fixture->bssid=bssid; fixture->frequency=frequency; fixture->signal=signal; fixture->caps=caps;
    uint8_t *out=fixture->elements;
    size_t at=element(out,0,0,ssid,strlen(ssid));
    static const uint8_t rates[8]={0x8c,0x12,0x98,0x24,0xb0,0x48,0x60,0x6c};
    at=element(out,at,1,rates,8);
    uint8_t channel=36; at=element(out,at,3,&channel,1);
    if (crowded) {
        uint8_t vendor[250]; memset(vendor,0x5a,sizeof(vendor));
        vendor[0]=0x00; vendor[1]=0x10; vendor[2]=0x18; vendor[3]=0x02;
        for (unsigned index=0;index<4;index++) at=element(out,at,221,vendor,sizeof(vendor));
    }
    if (akm>=0) {
        uint8_t rsn[20]={1,0,0,0x0f,0xac,4,1,0,0,0x0f,0xac,4,1,0,0,0x0f,0xac,(uint8_t)akm,
                         (uint8_t)rsn_caps,(uint8_t)(rsn_caps>>8)};
        at=element(out,at,48,rsn,sizeof(rsn));
    }
    uint8_t ht[26]={0}; at=element(out,at,45,ht,sizeof(ht));
    static const uint8_t wmm[7]={0x00,0x50,0xf2,0x02,0x01,0x01,0x00}; at=element(out,at,221,wmm,7);
    uint8_t he[21]={35}; at=element(out,at,255,he,sizeof(he));
    if (crowded) { out[at]=221; out[at+1]=40; at+=10; }       /* a last element cut short */
    fixture->length=at;
}
static void make_fixtures(void) {
    make_fixture(&fixtures[0],ap,"IEEE",5200,-60,0x11,2,0,true);
    make_fixture(&fixtures[1],open_ap,"open",2412,-45,0x01,-1,0,false);
    make_fixture(&fixtures[2],sae_ap,"sae",5180,-50,0x11,8,0,false);
    make_fixture(&fixtures[3],mfp_ap,"mfp",5180,-50,0x11,2,0xc0,false);
    make_fixture(&fixtures[4],far_ap,"far",5955,-40,0x11,2,0,false);
    make_fixture(&fixtures[5],weak_ap,"IEEE",2437,-70,0x11,2,0,false);
}
/* The scan request has been sent: its reply and SCAN_DONE, in either order. */
static void scan_accept(bool event_first) {
    uint8_t reply[MESSAGE],event[MESSAGE];
    size_t length=header(reply,2,2,wli.generation);
    size_t event_length=header(event,3,0x102,wli.generation);
    if(event_first) assert(!deliver(event,event_length)); else assert(!deliver(reply,length));
    step(); assert(!atomic_load(&caller_done) && !wli.fetching);
    if(event_first) assert(!deliver(reply,length)); else assert(!deliver(event,event_length));
}
static void serve_bss(unsigned count,unsigned refuse) {
    for (unsigned index=0;;index++) {
        step(); expect_request(3); assert(request_u32(21)==index);
        uint8_t bytes[MESSAGE]; size_t length=header(bytes,2,3,wli.generation);
        if (refuse || index==count) {
            fwc_write(bytes+20,refuse?refuse:8,4); assert(!deliver(bytes,length)); return;
        }
        const struct fixture *fixture=&fixtures[index];
        length=wli_put(bytes,length,3,fixture->bssid,6); length=put32(bytes,length,5,fixture->frequency);
        length=put32(bytes,length,22,(uint32_t)fixture->signal); length=put32(bytes,length,23,fixture->caps);
        length=put32(bytes,length,24,100); length=put32(bytes,length,25,0);
        length=wli_put(bytes,length,6,fixture->elements,fixture->length);
        assert(!deliver(bytes,length));
    }
}
static void scan_test(bool event_first) {
    pthread_t thread=begin(2,false);
    step(); expect_request(2);
    assert(captured_length==340 && captured[44]==4);
    scan_accept(event_first);
    serve_bss(6,0); assert(!pthread_join(thread,NULL));
    assert(!caller_result && wli_bss_count==5 && wli_state.phase==FUXI_WIFI_STA_IDLE && wli.scanned_at==fake_now);
    const struct fuxi_wifi_sta_bss *bss=&wli_bss[0];
    assert(bss->ssid_length==4 && !memcmp(bss->ssid,"IEEE",4) && !memcmp(bss->bssid,ap,6));
    assert(bss->frequency==5200 && bss->rssi==-60 && bss->capability==0x11 && bss->beacon_interval==100);
    /* 1119 bytes heard: one vendor element and the cut-off tail dropped, essentials and order kept */
    assert(bss->elements_length==857 && bss->elements[0]==0 && bss->elements[bss->elements_length-23]==255);
    struct wli_rsn rsn; wli_rsn_parse(bss->elements,bss->elements_length,&rsn);
    assert(rsn.complete && rsn.akm==(1u<<2) && rsn.pairwise==(1u<<4) && rsn.group==4 && !rsn.caps);
    for (unsigned index=0;index<wli_bss_count;index++) assert(wli_bss[index].frequency!=5955);
}
static void fallback_test(void) {
    pthread_t thread=begin(2,false);
    step(); expect_request(2); scan_accept(true);
    serve_bss(6,6); assert(!pthread_join(thread,NULL));
    assert(!caller_result && wli_bss_count==2 && wli_bss[0].rssi==-56 && wli_bss[0].ssid_length==4);
    assert(!memcmp(wli_bss[0].ssid,"real",4) && wli_bss[0].elements_length==4 && !wli.fatal);
}

static void dwell_tests(void) {
    memset(&caller_scan,0,sizeof(caller_scan));
    caller_scan.dwell_ms=110;
    bootstrap(); assert(wli_scan(&caller_scan)==EOPNOTSUPP && !wli.busy);
    test_dwell=1; bootstrap(); assert(wli.dwell_enabled && !wli.features);
    assert(wli_scan(&caller_scan)==EOPNOTSUPP && !wli.busy);
    offered_features=FWC_FEATURE_SCAN_DWELL_MS;
    test_dwell=0; bootstrap(); assert(!wli.dwell_enabled && wli.features==offered_features);
    assert(wli_scan(&caller_scan)==EOPNOTSUPP && !wli.busy);
    test_dwell=2; bootstrap(); assert(!wli.dwell_enabled);
    test_dwell=1; bootstrap(); assert(wli.dwell_enabled);
    caller_scan.dwell_ms=1001; assert(wli_scan(&caller_scan)==EINVAL && !wli.busy);
    caller_scan.dwell_ms=110; caller_scan.frequency_count=1; caller_scan.frequencies[0]=2412;
    pthread_t thread=begin(2,false); step(); expect_request(2);
    assert(request_u32(FWC_ATTR_SCAN_DWELL_MS)==110 && !atomic_load(&caller_done));
    scan_accept(true); serve_bss(6,0);
    assert(!pthread_join(thread,NULL) && !caller_result && !wli.fatal);
    thread=begin(2,false); step(); expect_request(2); respond(2,6);
    assert(!pthread_join(thread,NULL) && caller_result==EOPNOTSUPP && !wli.fatal);
    test_dwell=0; offered_features=0;
    memset(&caller_scan,0,sizeof(caller_scan));
}

static void batch_tests(void) {
    unsigned count, limit;
    memset(&caller_scan,0,sizeof(caller_scan));
    caller_scan.dwell_ms=110; caller_scan.frequency_count=40; caller_scan.passive=true;
    for(unsigned index=0;index<40;index++)
        caller_scan.frequencies[index]=index<13 ? 2412+index*5 : 5180+(index-13)*20;
    test_dwell=1; offered_features=FWC_FEATURE_SCAN_DWELL_MS;
    test_batch=2; bootstrap(); assert(!wli.batch_enabled);
    assert(!wli_scan_plan(&caller_scan,&count,&limit) && count==40 && limit==40);
    test_batch=1; bootstrap(); assert(wli.batch_enabled);
    assert(!wli_scan_plan(&caller_scan,&count,&limit) && count==40 && limit==31);
    pthread_t thread=begin(2,false); step(); expect_request(2);
    assert(wli.scan_count==40 && wli.scan_limit==31 && !wli.scan_offset && !wli.cache_valid);
    uint64_t deadline=wli.deadline;
    size_t size; const uint8_t *frequencies=request_attribute(20,&size);
    assert(frequencies && size==31*4 && request_u32(FWC_ATTR_SCAN_DWELL_MS)==110);
    assert(!request_attribute(19,&size));
    for(unsigned index=0;index<31;index++) assert(fwc_read32(frequencies+index*4)==caller_scan.frequencies[index]);
    scan_accept(false); step(); expect_request(2);
    assert(wli.scan_offset==31 && !wli.fetching && !wli.cache_valid && !atomic_load(&caller_done));
    assert(wli.deadline==deadline);
    frequencies=request_attribute(20,&size); assert(frequencies && size==9*4);
    for(unsigned index=0;index<9;index++) assert(fwc_read32(frequencies+index*4)==caller_scan.frequencies[31+index]);
    scan_accept(true); serve_bss(6,0);
    assert(!pthread_join(thread,NULL) && !caller_result && wli.cache_valid && wli_bss_count==5);
    assert(wli.scan.frequency_count==40 && wli.scan.dwell_ms==110);
    bootstrap(); thread=begin(2,false); step(); scan_accept(true); step(); expect_request(2);
    respond(2,1);
    assert(!pthread_join(thread,NULL) && caller_result==EINVAL && !wli.cache_valid && !wli.fetching && !wli_bss_count);
    bootstrap(); thread=begin(2,false); step(); scan_accept(false); step(); expect_request(2);
    assert(wli_step(wli.deadline)==ETIMEDOUT); wli_fail(ETIMEDOUT);
    assert(!pthread_join(thread,NULL) && caller_result==ETIMEDOUT && !wli.cache_valid);
    bootstrap(); caller_scan.frequency_count=50; caller_scan.dwell_ms=1;
    assert(wli_scan_plan(&caller_scan,&count,&limit)==EINVAL);
    assert(wli_scan(&caller_scan)==EINVAL && !wli.busy);
    caller_scan.frequency_count=49;
    assert(!wli_scan_plan(&caller_scan,&count,&limit) && count==49 && limit==37);
    caller_scan.dwell_ms=1000; caller_scan.frequency_count=17;
    assert(wli_scan_plan(&caller_scan,&count,&limit)==EINVAL);
    caller_scan.frequency_count=16;
    assert(!wli_scan_plan(&caller_scan,&count,&limit) && limit==12);
    caller_scan.dwell_ms=0; caller_scan.frequency_count=64;
    assert(!wli_scan_plan(&caller_scan,&count,&limit) && count==64 && limit==64);
    caller_scan.dwell_ms=110; caller_scan.frequency_count=0;
    thread=begin(2,false); step(); expect_request(2);
    frequencies=request_attribute(20,&size);
    assert(frequencies && size==8 && fwc_read32(frequencies)==2412 && fwc_read32(frequencies+4)==5180);
    scan_accept(false); serve_bss(6,0); assert(!pthread_join(thread,NULL) && !caller_result);
    test_batch=test_dwell=0; offered_features=0;
    memset(&caller_scan,0,sizeof(caller_scan));
}

static void cache_tests(void) {
    struct fuxi_wifi_sta_scan previous={0}, asked={0};
    assert(wli_scan_covers(&previous,&asked));
    asked.frequency_count=1; asked.frequencies[0]=2412;
    assert(wli_scan_covers(&previous,&asked));
    previous.frequency_count=1; previous.frequencies[0]=5200;
    assert(!wli_scan_covers(&previous,&asked));
    previous.frequencies[0]=2412;
    assert(wli_scan_covers(&previous,&asked));
    asked.frequency_count=0; assert(!wli_scan_covers(&previous,&asked));
    previous.frequency_count=0; previous.ssid_length=4; memcpy(previous.ssid,"IEEE",4);
    assert(!wli_scan_covers(&previous,&asked));
    asked.ssid_length=4; memcpy(asked.ssid,"IEEE",4);
    assert(wli_scan_covers(&previous,&asked));
    asked.ssid[3]='X'; assert(!wli_scan_covers(&previous,&asked));
    previous.ssid_length=0; asked.passive=true; assert(!wli_scan_covers(&previous,&asked));
    asked.passive=false; asked.dwell_ms=110; assert(!wli_scan_covers(&previous,&asked));
    test_cache=2; bootstrap(); assert(!wli.cache_enabled);
    test_cache=1; bootstrap(); assert(wli.cache_enabled && !wli.cache_valid);
    memset(&caller_scan,0,sizeof(caller_scan)); caller_scan.cache_allowed=true;
    pthread_t thread=begin(2,false); step(); expect_request(2);
    scan_accept(false); serve_bss(6,0);
    assert(!pthread_join(thread,NULL) && !caller_result && wli.cache_valid);
    unsigned sequence=wli.sequence;
    assert(!wli_scan(&caller_scan) && wli.sequence==sequence && wli.cache_hits==1);
    fake_now+=WLI_CACHE_MS;
    assert(!wli_scan(&caller_scan) && wli.sequence==sequence && wli.cache_hits==2);
    unsigned heard_seen=0; wli_heard(count_heard,&heard_seen); assert(heard_seen==5);
    fake_now++;
    thread=begin(2,false); step(); expect_request(2); assert(!wli.cache_valid);
    scan_accept(true); serve_bss(6,0);
    assert(!pthread_join(thread,NULL) && !caller_result);
    caller_scan.cache_allowed=false;
    thread=begin(2,false); step(); expect_request(2);
    scan_accept(false); serve_bss(6,0);
    assert(!pthread_join(thread,NULL) && !caller_result);
    caller_scan.cache_allowed=true; wli.cache_enabled=0;
    thread=begin(2,false); step(); expect_request(2);
    scan_accept(false); serve_bss(6,0);
    assert(!pthread_join(thread,NULL) && !caller_result);
    wli.cache_enabled=1; power_test(false,3);
    assert(!wli.cache_valid && !wli_bss_count);
    test_cache=0; memset(&caller_scan,0,sizeof(caller_scan));
}

static void scan_done(unsigned status,uint64_t generation) {
    uint8_t bytes[MESSAGE]; size_t length=header(bytes,3,0x102,generation);
    fwc_write(bytes+20,status,4); assert(!deliver(bytes,length));
}

static void scan_recovery_tests(void) {
    memset(&caller_scan,0,sizeof(caller_scan));
    test_ux=FUXI_WIFI_UX_SCAN_RECOVER|FUXI_WIFI_UX_POWER_ASKED;
    for (unsigned event_first=0;event_first<2;event_first++) {
        bootstrap(); pthread_t thread=begin(2,false); step(); expect_request(2);
        if (event_first) scan_done(9,wli.generation); else respond(2,0);
        step(); assert(!atomic_load(&caller_done) && !wli.fetching);
        if (event_first) respond(2,0); else scan_done(9,wli.generation);
        step(); assert(!pthread_join(thread,NULL) && caller_result==EIO);
        assert(!wli.fatal && !wli.scan_draining && !wli.cache_valid && !wli_bss_count);
    }

    bootstrap(); pthread_t thread=begin(2,false); step(); respond(2,0);
    now=wli.deadline-2; step();
    assert(!atomic_load(&caller_done));
    step(); assert(!pthread_join(thread,NULL) && caller_result==ETIMEDOUT);
    assert(!wli.fatal && wli.scan_draining && wli_scan(&caller_scan)==EBUSY);
    power_test(true,2); assert(wli.scan_draining);
    scan_done(9,2);
    assert(!wli.scan_draining);
    thread=begin(2,false); step(); scan_accept(false); serve_bss(6,0);
    assert(!pthread_join(thread,NULL) && !caller_result && wli_bss_count==5);

    bootstrap(); wli_state.phase=FUXI_WIFI_STA_JOINED;
    unsigned downs=link_downs;
    thread=begin(2,false); step(); respond(2,0);
    test_powered=false; step(); test_powered=true;
    assert(!pthread_join(thread,NULL) && caller_result==ECANCELED);
    assert(wli.scan_draining && wli_state.phase==FUXI_WIFI_STA_JOINED && link_downs==downs);
    power_test(false,3);
    assert(!wli.scan_draining);
    power_test(true,4);
    thread=begin(2,false); step(); scan_done(9,2);
    assert(!wli.scan_done && !wli.fatal);
    scan_accept(true); serve_bss(6,0);
    assert(!pthread_join(thread,NULL) && !caller_result);

    /* An already expired aggregate scan must not enqueue another dwell batch. */
    test_dwell=test_batch=1; offered_features=FWC_FEATURE_SCAN_DWELL_MS;
    caller_scan.dwell_ms=110; caller_scan.frequency_count=40;
    for (unsigned index=0;index<40;index++) caller_scan.frequencies[index]=2412+index*5;
    bootstrap(); thread=begin(2,false); step(); scan_accept(false);
    unsigned sequence=wli.sequence;
    now=wli.deadline-1; step();
    assert(!pthread_join(thread,NULL) && caller_result==ETIMEDOUT);
    assert(wli.sequence==sequence && !wli.scan_draining && !wli.fatal);

    test_ux=test_dwell=test_batch=offered_features=0;
    memset(&caller_scan,0,sizeof(caller_scan));
    puts("PASS scan recovery: abort, deadline, power cancellation, late completion and a subsequent scan");
}

static int read_test_packet(void *source,size_t offset,void *dest,size_t length) {
    assert(offset<=60 && length<=60-offset); memcpy(dest,(uint8_t *)source+offset,length); return 0;
}

static void rx_batch_tests(void) {
    test_ux=FUXI_WIFI_UX_RX_BATCH|FUXI_WIFI_UX_LINK_STATS;
    bootstrap();
    uint8_t frame[60]={0xa5};
    events->rx_packet(frame,sizeof(frame),read_test_packet);
    assert(!rx_packets);
    __atomic_store_n(&wli_link,1,__ATOMIC_RELEASE);
    events->rx_packet(frame,sizeof(frame),read_test_packet);
    events->rx_flush(); assert(rx_packets==1 && rx_flushes==1);
    assert(!wli_step(100) && !phy_reads);
    rx_octets=700;tx_octets=80;
    assert(!wli_step(5099) && !phy_reads);
    assert(!wli_step(5100) && phy_reads==1);
    __atomic_store_n(&wli_link,0,__ATOMIC_RELEASE);
    assert(!wli_step(5101));
    test_ux=0;
    puts("PASS native RX batch callbacks and paced PHY counters");
}

static void scan_scope_tests(void) {
    size_t size;
    memset(&caller_scan,0,sizeof(caller_scan)); caller_scan.cache_allowed=true;
    bootstrap(); pthread_t thread=begin(2,false); step();
    assert(!request_attribute(20,&size));
    scan_accept(false); serve_bss(6,0); assert(!pthread_join(thread,NULL) && !caller_result);

    test_ux=FUXI_WIFI_UX_SCAN_CHANNELS; test_cache=1;
    bootstrap(); thread=begin(2,false); step();
    const uint8_t *frequencies=request_attribute(20,&size);
    assert(frequencies && size==8 && fwc_read32(frequencies)==2412 && fwc_read32(frequencies+4)==5180);
    assert(!request_attribute(FWC_ATTR_SCAN_DWELL_MS,&size));
    scan_accept(false); serve_bss(6,0); assert(!pthread_join(thread,NULL) && !caller_result);
    unsigned sequence=wli.sequence;
    assert(!wli_scan(&caller_scan) && wli.sequence==sequence && wli.cache_hits==1);

    caller_scan.frequency_count=1; caller_scan.frequencies[0]=5200;
    thread=begin(2,false); step();
    frequencies=request_attribute(20,&size);
    assert(frequencies && size==4 && fwc_read32(frequencies)==5200);
    scan_accept(true); serve_bss(6,0); assert(!pthread_join(thread,NULL) && !caller_result);
    test_ux=test_cache=0;
    memset(&caller_scan,0,sizeof(caller_scan));
    puts("PASS implicit scan uses advertised channels in one request; explicit channels and cache coverage preserved");
}

/* Events from the module about the join. */
static void event_auth(const uint8_t *bssid,unsigned status) {
    uint8_t bytes[MESSAGE], frame[30]={0xb0};
    memcpy(frame+16,bssid,6); fwc_write(frame+26,2,2); fwc_write(frame+28,status,2);
    size_t length=header(bytes,3,0x103,wli.generation);
    length=wli_put(bytes,length,3,bssid,6); length=wli_put(bytes,length,26,frame,sizeof(frame));
    assert(!deliver(bytes,length));
}
static void event_assoc(const uint8_t *bssid,unsigned status) {
    uint8_t bytes[MESSAGE]; size_t length=header(bytes,3,0x104,wli.generation);
    length=wli_put(bytes,length,3,bssid,6); length=put32(bytes,length,18,status);
    assert(!deliver(bytes,length));
}
static void event_eapol(const uint8_t *bssid,const uint8_t *message,size_t size) {
    uint8_t bytes[MESSAGE]; size_t length=header(bytes,3,0x106,wli.generation);
    length=wli_put(bytes,length,3,bssid,6); length=put32(bytes,length,16,1);
    length=wli_put(bytes,length,15,message,size); assert(!deliver(bytes,length));
}
static void event_lost(const uint8_t *bssid,unsigned reason,unsigned local) {
    uint8_t bytes[MESSAGE]; size_t length=header(bytes,3,0x105,wli.generation);
    length=wli_put(bytes,length,3,bssid,6); length=put32(bytes,length,8,reason);
    length=put32(bytes,length,39,local); assert(!deliver(bytes,length));
}
static void event_eapol_status(unsigned acked) {
    uint8_t bytes[MESSAGE], frame[14]={0};
    size_t length=header(bytes,3,0x107,wli.generation);
    length=wli_put(bytes,length,26,frame,sizeof(frame));
    length=put32(bytes,length,41,acked);
    assert(!deliver(bytes,length));
}
static void event_power(bool on,uint64_t generation) {
    uint8_t bytes[MESSAGE], value[4]={0};
    size_t length=header(bytes,3,0x10a,generation);
    fwc_write(value,on,4);
    length=wli_put(bytes,length,32,value,4);
    assert(!deliver(bytes,length));
}
static void ask_join(const char *ssid,const char *passphrase,int security) {
    memset(&caller_join,0,sizeof(caller_join));
    caller_join.ssid_length=(uint8_t)strlen(ssid); memcpy(caller_join.ssid,ssid,caller_join.ssid_length);
    caller_join.security=security;
    if (passphrase) {
        caller_join.passphrase_length=(uint8_t)strlen(passphrase);
        memcpy(caller_join.passphrase,passphrase,caller_join.passphrase_length);
    }
}
/* The access point's side of the handshake, from the same inputs: what the bridge has to send. */
static struct fuxi_wifi_wpa model;
static void model_reset(void) {
    memset(&model,0,sizeof(model));
    memcpy(model.pmk,pmk,32); memcpy(model.own,station,6); memcpy(model.peer,ap,6);
    memcpy(model.own_nonce,snonce,32); model.rsn=own_rsn; model.rsn_length=sizeof(own_rsn);
}
static unsigned model_reply(const uint8_t *message,size_t length,int kind,uint8_t *reply) {
    const char *why=""; unsigned reply_length=0;
    assert(fuxi_wifi_wpa_message(&model,message,(unsigned)length,reply,&reply_length,&why)==kind);
    return reply_length;
}
static void through_association(void) {
    step(); expect_request(4);
    request_bytes(4,"IEEE",4); request_bytes(3,ap,6); assert(request_u32(5)==5200 && request_u32(7)==0);
    assert(wli_state.phase==FUXI_WIFI_STA_JOINING);
    respond(4,0); step(); assert(wlj.step==WLJ_AUTH_WAIT);
    event_auth(weak_ap,0); event_auth(ap,0); step(); assert(wlj.step==WLJ_ASSOC);
    step(); expect_request(5); request_bytes(6,own_rsn,sizeof(own_rsn)); request_bytes(3,ap,6);
    assert(request_u32(34)==0x000fac04 && request_u32(35)==0x000fac04 && request_u32(36)==0x000fac02);
    assert(request_u32(37)==0);
    respond(5,0); event_assoc(ap,0);
}
static void message_two(bool right_key) {
    uint8_t reply[256];
    model_reset();
    unsigned length=model_reply(m1,sizeof(m1),1,reply);
    event_eapol(ap,m1,sizeof(m1)); step(); expect_request(9);
    if (right_key) request_bytes(15,reply,length);
    else { size_t size; assert(request_attribute(15,&size) && size==length); }
    request_bytes(3,ap,6); assert(request_u32(40)==0x888e && request_u32(16)==1);
    assert(wlj.step==WLJ_KEYS_WAIT && wlj.answered==1); respond(9,0);
}
static void keys_and_port(void) {
    uint8_t reply[256];
    unsigned length=model_reply(m3,sizeof(m3),2,reply);
    event_eapol(ap,m3,sizeof(m3)); step(); expect_request(9);
    request_bytes(15,reply,length); assert(request_u32(16)==1); respond(9,0);
    step(); expect_request(7); request_bytes(3,ap,6); request_bytes(12,tk,16);
    assert(request_u32(10)==0 && request_u32(11)==1 && request_u32(9)==0x000fac04); respond(7,0);
    step(); expect_request(7); request_bytes(12,gtk,16);
    assert(request_u32(10)==1 && request_u32(11)==0 && request_u32(9)==0x000fac04);
    size_t size; assert(!request_attribute(3,&size) && request_attribute(13,&size) && size==6); respond(7,0);
    step(); expect_request(8); assert(request_u32(14)==1); respond(8,0);
    step(); expect_request(10); assert(request_u32(32)==1); respond(10,0);
    assert(!__atomic_load_n(&wli_link,__ATOMIC_ACQUIRE));
    step();
}
static void full_join(void) {
    unsigned ups=link_ups;
    ask_join("IEEE","password",FUXI_WIFI_STA_WPA2_PSK); pthread_t thread=begin(3,false);
    through_association(); message_two(true); keys_and_port();
    assert(!pthread_join(thread,NULL) && !caller_result);
    assert(wli_state.phase==FUXI_WIFI_STA_JOINED && link_ups==ups+1 && __atomic_load_n(&wli_link,__ATOMIC_ACQUIRE));
    assert(wli_state.security==FUXI_WIFI_STA_WPA2_PSK && !memcmp(wli_state.bss.bssid,ap,6));
    for (unsigned index=0;index<32;index++) assert(!wlj.wpa.pmk[index] && !wli_target.pmk[index] && !wli_asked.pmk[index]);
}
static int failed_join(pthread_t thread) {
    assert(!pthread_join(thread,NULL));
    assert(wli_state.phase==FUXI_WIFI_STA_IDLE && wlj.step==WLJ_NONE && !wli.fatal);
    assert(wli_state.last_error==caller_result); return caller_result;
}
static void power_off_join_test(void) {
    test_ux=FUXI_WIFI_UX_POWER_OFF_FIRST;
    bootstrap();
    caller_scan.frequency_count=0;
    pthread_t thread=begin(2,false); step(); scan_accept(false); serve_bss(6,0);
    assert(!pthread_join(thread,NULL) && !caller_result);
    ask_join("IEEE","password",FUXI_WIFI_STA_WPA2_PSK);
    thread=begin(3,false); step(); expect_request(4);
    respond(4,0);
    test_off_pending=true;
    step(); expect_request(6); respond(6,0); step();
    assert(failed_join(thread)==ECANCELED);
    assert(wli_scan(&caller_scan)==ECANCELED && !wli.fatal);
    power_test(false,3);
    test_off_pending=false;
    power_test(true,4);
    test_ux=0;
    puts("PASS power off cancels an active join and leaves radio control available");
}
static void join_tests(void) {
    uint8_t frame[60]={0}, reply[256];
    pthread_t thread;
    /* what cannot be joined is turned away before the radio is asked */
    ask_join("sae","password",FUXI_WIFI_STA_WPA2_PSK); assert(wli_join(&caller_join)==EOPNOTSUPP);
    ask_join("mfp","password",FUXI_WIFI_STA_WPA2_PSK); assert(wli_join(&caller_join)==EOPNOTSUPP);
    ask_join("open","password",FUXI_WIFI_STA_WPA2_PSK); assert(wli_join(&caller_join)==EINVAL);
    ask_join("IEEE",NULL,FUXI_WIFI_STA_OPEN); assert(wli_join(&caller_join)==EINVAL);
    ask_join("IEEE","short",FUXI_WIFI_STA_WPA2_PSK); assert(wli_join(&caller_join)==EINVAL);
    assert(wli_join(NULL)==EINVAL && wli_state.last_error==EINVAL && !wli_tx(frame,60));
    assert(captured_length && fwc_read16(captured+8)==3);
    /* WPA2-PSK: authentication, association, four-way handshake, keys, port, frames, group rekey */
    full_join();
    assert(wli_tx(frame,60) && tx_calls==1);
    assert(!wli_tx(frame,13) && !wli_tx(frame,2001) && !wli_tx(NULL,60) && tx_calls==1);
    tx_status=1; assert(!wli_tx(frame,60)); tx_status=0;
    events->rx(frame,60); events->rx(frame,13); assert(rx_frames==1);
    unsigned length=model_reply(g1,sizeof(g1),3,reply);
    event_eapol(ap,g1,sizeof(g1)); step(); expect_request(9);
    request_bytes(15,reply,length); assert(request_u32(16)==0); respond(9,0);
    step(); expect_request(7); request_bytes(12,gtk,16); respond(7,0);
    step(); assert(wlj.rekeys==1 && wlj.step==WLJ_JOINED && wli_state.phase==FUXI_WIFI_STA_JOINED);
    /* the access point lets go */
    unsigned downs=link_downs;
    event_lost(weak_ap,7,0); step(); assert(wli_state.phase==FUXI_WIFI_STA_JOINED);
    event_lost(ap,7,0); step();
    assert(lost_calls==1 && lost_error==ECONNRESET && wli_state.phase==FUXI_WIFI_STA_IDLE);
    assert(link_downs==downs+1 && !wli_tx(frame,60) && wlj.step==WLJ_NONE);
    events->rx(frame,60); assert(rx_frames==1);
    /* leaving when asked: DEAUTHENTICATE, the module's own note of it ignored */
    full_join();
    thread=begin(4,false); step(); expect_request(6); request_bytes(3,ap,6); assert(request_u32(8)==3);
    assert(!__atomic_load_n(&wli_link,__ATOMIC_ACQUIRE));
    event_lost(ap,3,1); respond(6,0); step();
    assert(!pthread_join(thread,NULL) && !caller_result && wli_state.phase==FUXI_WIFI_STA_IDLE && lost_calls==1);
    /* a wrong passphrase: message 2 goes out, the access point leaves */
    ask_join("IEEE","not-the-passphrase",FUXI_WIFI_STA_WPA2_PSK); thread=begin(3,false);
    through_association(); message_two(false);
    event_lost(ap,15,0); step(); step();
    assert(failed_join(thread)==EAUTH && link_ups==2);
    /* authentication refused */
    ask_join("IEEE","password",FUXI_WIFI_STA_WPA2_PSK); thread=begin(3,false);
    step(); expect_request(4); respond(4,0); event_auth(ap,17); step();
    step(); expect_request(6); request_bytes(3,ap,6); respond(6,0); step();
    assert(failed_join(thread)==ECONNREFUSED);
    /* association refused */
    ask_join("IEEE","password",FUXI_WIFI_STA_WPA2_PSK); thread=begin(3,false);
    step(); respond(4,0); event_auth(ap,0); step(); step(); expect_request(5); respond(5,0);
    event_assoc(ap,17); step(); step(); expect_request(6); respond(6,0); step();
    assert(failed_join(thread)==ECONNREFUSED);
    /* the module turns a request down: not fatal */
    ask_join("IEEE","password",FUXI_WIFI_STA_WPA2_PSK); thread=begin(3,false);
    step(); expect_request(4); respond(4,8); step(); expect_request(6); respond(6,0); step();
    assert(failed_join(thread)==ENOENT);
    /* nothing heard in time */
    ask_join("IEEE","password",FUXI_WIFI_STA_WPA2_PSK); thread=begin(3,false);
    step(); expect_request(4); respond(4,0); now+=20001; step(); expect_request(6); respond(6,0); step();
    assert(failed_join(thread)==ETIMEDOUT);
    /* message 1 answered but nothing more */
    ask_join("IEEE","password",FUXI_WIFI_STA_WPA2_PSK); thread=begin(3,false);
    through_association(); message_two(true); now+=20001; step(); expect_request(6); respond(6,0); step();
    assert(failed_join(thread)==EAUTH);
    /* an open network */
    ask_join("open",NULL,FUXI_WIFI_STA_OPEN); thread=begin(3,false);
    step(); expect_request(4); request_bytes(4,"open",4); respond(4,0); event_auth(open_ap,0); step();
    step(); expect_request(5); size_t size; assert(!request_attribute(6,&size));
    assert(request_u32(34)==0 && request_u32(35)==0 && request_u32(36)==0); respond(5,0);
    event_assoc(open_ap,0); step(); assert(wlj.step==WLJ_PORT);
    step(); expect_request(8); respond(8,0); step(); expect_request(10); respond(10,0); step();
    assert(!pthread_join(thread,NULL) && !caller_result && wli_state.phase==FUXI_WIFI_STA_JOINED);
    assert(wli_tx(frame,60));
    thread=begin(4,false); step(); expect_request(6); respond(6,0); step();
    assert(!pthread_join(thread,NULL) && !caller_result && wli_state.phase==FUXI_WIFI_STA_IDLE);
    /* not heard: a directed scan first, then the answer */
    ask_join("gone","password",FUXI_WIFI_STA_WPA2_PSK); thread=start_caller(3,false); wait_busy(2);
    step(); expect_request(2); assert(captured[44]==4 && !memcmp(captured+45,"gone",4));
    scan_accept(true); serve_bss(6,0);
    assert(!pthread_join(thread,NULL) && caller_result==ENOENT && wli_state.last_error==ENOENT);
    /* heard too long ago: its own channel listened to first; not there now, so every channel; then joined */
    fake_now+=WLI_FRESH_MS+1;
    ask_join("IEEE","password",FUXI_WIFI_STA_WPA2_PSK); thread=start_caller(3,false); wait_busy(2);
    step(); expect_request(2); assert(captured_length==88 && request_u32(20)==5200);
    scan_accept(false); serve_bss(0,0);
    wait_busy(2); step(); expect_request(2); assert(captured_length==80 && !request_attribute(20,&(size_t){0}));
    scan_accept(false); serve_bss(6,0);
    wait_busy(3); step(); expect_request(4); respond(4,8); step(); expect_request(6); respond(6,0); step();
    assert(failed_join(thread)==ENOENT);
    /* joined, then the radio is switched off: the join is forgotten */
    full_join();
    power_test(false,wli.generation+1);
    assert(!__atomic_load_n(&wli_link,__ATOMIC_ACQUIRE) && wlj.step==WLJ_NONE && !wli_tx(frame,60));
    ask_join("IEEE","password",FUXI_WIFI_STA_WPA2_PSK); assert(wli_join(&caller_join)==ENETDOWN);
    power_test(true,wli.generation+1);
}
/* fuxi,wifi-ux bit 1：照估計速率選 AP。元素照 802.11 的格式直接寫進 wli_bss。 */
static const uint8_t ap2[6]={2,0x46,0x55,0x58,0x49,8};
static const uint8_t ht_cap[28]={45,26,0,0,0,0xff,0xff};                /* Rx MCS 0–15：2 條串流 */
static const uint8_t ht_op40[24]={61,22,1,0x05};                         /* 次通道在上、STA 40 MHz */
static const uint8_t vht_cap[14]={191,12,0,0,0,0,0xfa,0xff};             /* 2 條串流、MCS 0–9 */
static const uint8_t vht_op80[7]={192,5,1,42};                           /* 80 MHz */
static const uint8_t he_cap[24]={255,22,35,[20]=0xfa,[21]=0xff,[22]=0xfa,[23]=0xff};  /* 2 條、MCS 0–11 */
static size_t elements_of(uint8_t *out,const uint8_t *const *parts,const size_t *sizes,unsigned count) {
    size_t at=0;
    for (unsigned index=0;index<count;index++) { memcpy(out+at,parts[index],sizes[index]); at+=sizes[index]; }
    return at;
}
enum { HT=1, HT40=2, VHT80=4, HE=8 };
static void put_bss(unsigned index,const uint8_t *bssid,const char *ssid,uint32_t frequency,int rssi,int akm,
                    unsigned kinds,uint32_t age_ms) {
    struct fuxi_wifi_sta_bss *bss=&wli_bss[index]; memset(bss,0,sizeof(*bss));
    memcpy(bss->bssid,bssid,6); bss->ssid_length=(uint8_t)strlen(ssid); memcpy(bss->ssid,ssid,bss->ssid_length);
    bss->frequency=(uint16_t)frequency; bss->rssi=(int16_t)rssi; bss->capability=akm>=0?0x11:0x01;
    bss->beacon_interval=100; bss->age_ms=age_ms;
    size_t at=element(bss->elements,0,0,ssid,strlen(ssid));
    if (akm>=0) {
        uint8_t rsn[20]={1,0,0,0x0f,0xac,4,1,0,0,0x0f,0xac,4,1,0,0,0x0f,0xac,(uint8_t)akm,0,0};
        at=element(bss->elements,at,48,rsn,sizeof(rsn));
    }
    const uint8_t *parts[5]; size_t sizes[5]; unsigned count=0;
    if (kinds&(HT|HT40)) { parts[count]=ht_cap; sizes[count++]=sizeof(ht_cap); }
    if (kinds&HT40) { parts[count]=ht_op40; sizes[count++]=sizeof(ht_op40); }
    if (kinds&(VHT80|HE)) { parts[count]=vht_cap; sizes[count++]=sizeof(vht_cap); }
    if (kinds&(VHT80|HE)) { parts[count]=vht_op80; sizes[count++]=sizeof(vht_op80); }
    if (kinds&HE) { parts[count]=he_cap; sizes[count++]=sizeof(he_cap); }
    bss->elements_length=(uint16_t)(at+elements_of(bss->elements+at,parts,sizes,count));
}
static void heard_now(unsigned count) { wli_bss_count=count; wli.scanned_at=fake_now; }
static int pick(unsigned ux,const char *ssid,const uint8_t *given,struct wli_target *target) {
    wli.ux=ux; ask_join(ssid,"password",FUXI_WIFI_STA_WPA2_PSK);
    if (given) memcpy(caller_join.bssid,given,6);
    memset(target,0,sizeof(*target)); return wli_pick(&caller_join,target);
}
static void pick_tests(void) {
    struct wli_target target;
    /* 估計速率：HT20 2 條 SNR 43 → MCS7；HT40；VHT80 SNR 32 → MCS7、SNR 47 → MCS9；HE80 SNR 42 → MCS11；沒有 HT → 54 */
    put_bss(0,weak_ap,"IEEE",2412,-46,2,HT,0);
    put_bss(1,ap,"IEEE",5200,-60,2,HT|VHT80,0);
    put_bss(2,open_ap,"IEEE",2412,-46,2,HT40,0);
    put_bss(3,ap2,"IEEE",5200,-45,2,HT|VHT80,0);
    put_bss(4,far_ap,"IEEE",5200,-50,2,HT|HE,0);
    put_bss(5,mfp_ap,"IEEE",2412,-60,2,0,0);
    assert(wli_rate_kbps(&wli_bss[0])==130000 && wli_rate_kbps(&wli_bss[1])==585000);
    assert(wli_rate_kbps(&wli_bss[2])==270400 && wli_rate_kbps(&wli_bss[3])==780300);
    assert(wli_rate_kbps(&wli_bss[4])==1201692 && wli_rate_kbps(&wli_bss[5])==54000);
    /* 實機那一次：2.4 GHz -46 dBm 對 5 GHz VHT80 -60 dBm。舊選法挑訊號強的 2.4，bit 1 挑快的 5 GHz */
    heard_now(2);
    assert(!pick(0,"IEEE",NULL,&target) && !memcmp(target.bss.bssid,weak_ap,6));
    assert(!pick(FUXI_WIFI_UX_PICK_RATE,"IEEE",NULL,&target) && !memcmp(target.bss.bssid,ap,6));
    /* HE80 比 VHT80 強 5 dB 的那台還快：SNR 都超過 25，差距不到 7 就比速率 */
    put_bss(0,ap2,"IEEE",5200,-45,2,HT|VHT80,0); put_bss(1,far_ap,"IEEE",5200,-50,2,HT|HE,0); heard_now(2);
    assert(!pick(FUXI_WIFI_UX_PICK_RATE,"IEEE",NULL,&target) && !memcmp(target.bss.bssid,far_ap,6));
    /* SNR 差 7 dB 以上就比完整 SNR，不看速率：2.4 GHz -40（49）對 5 GHz VHT80 -75（17） */
    put_bss(0,weak_ap,"IEEE",2412,-40,2,HT,0); put_bss(1,ap,"IEEE",5200,-75,2,HT|VHT80,0); heard_now(2);
    assert(wli_rate_kbps(&wli_bss[1])==234000 && wli_rate_kbps(&wli_bss[0])==130000);
    assert(!pick(FUXI_WIFI_UX_PICK_RATE,"IEEE",NULL,&target) && !memcmp(target.bss.bssid,weak_ap,6));
    /* 速率一樣（都沒有 HT、都 54）而 SNR 差不到 5：選 5 GHz；舊選法選強 2 dB 的 2.4 GHz */
    put_bss(0,weak_ap,"IEEE",2412,-60,2,0,0); put_bss(1,ap,"IEEE",5200,-62,2,0,0); heard_now(2);
    assert(wli_rate_kbps(&wli_bss[0])==54000 && wli_rate_kbps(&wli_bss[1])==54000);
    assert(!pick(FUXI_WIFI_UX_PICK_RATE,"IEEE",NULL,&target) && !memcmp(target.bss.bssid,ap,6));
    assert(!pick(0,"IEEE",NULL,&target) && !memcmp(target.bss.bssid,weak_ap,6));
    /* 太久沒聽到的不先選：bit 1 選新鮮的 2.4 GHz；舊選法選最強的那台再回 ESTALE 去重掃 */
    put_bss(0,ap,"IEEE",5200,-40,2,HT|VHT80,WLI_FRESH_MS+1); put_bss(1,weak_ap,"IEEE",2412,-46,2,HT,0); heard_now(2);
    assert(!pick(FUXI_WIFI_UX_PICK_RATE,"IEEE",NULL,&target) && !memcmp(target.bss.bssid,weak_ap,6));
    assert(pick(0,"IEEE",NULL,&target)==ESTALE && !memcmp(target.bss.bssid,ap,6));
    /* 全都太舊：仍回 ESTALE，保留最好那台的頻道給重掃 */
    put_bss(1,weak_ap,"IEEE",2412,-46,2,HT,WLI_FRESH_MS+1);
    assert(pick(FUXI_WIFI_UX_PICK_RATE,"IEEE",NULL,&target)==ESTALE && target.bss.frequency==5200);
    /* 同名但只能 WPA3（SAE）的那台跳過；舊選法選它，之後 wli_security() 才拒絕 */
    put_bss(0,sae_ap,"IEEE",5200,-30,8,HT|VHT80,0); put_bss(1,weak_ap,"IEEE",2412,-46,2,HT,0); heard_now(2);
    assert(!pick(FUXI_WIFI_UX_PICK_RATE,"IEEE",NULL,&target) && !memcmp(target.bss.bssid,weak_ap,6));
    assert(!pick(0,"IEEE",NULL,&target) && !memcmp(target.bss.bssid,sae_ap,6));
    /* 全部都不能用這種保護加入：照舊挑最強的，讓加入時說明原因 */
    put_bss(1,ap,"IEEE",5200,-50,8,HT|VHT80,0);
    assert(!pick(FUXI_WIFI_UX_PICK_RATE,"IEEE",NULL,&target) && !memcmp(target.bss.bssid,sae_ap,6));
    /* 指定 BSSID：只看那一台；名稱不對、沒聽到 → ENOENT */
    put_bss(0,weak_ap,"IEEE",2412,-46,2,HT,0); put_bss(1,ap,"IEEE",5200,-60,2,HT|VHT80,0); heard_now(2);
    assert(!pick(FUXI_WIFI_UX_PICK_RATE,"IEEE",weak_ap,&target) && !memcmp(target.bss.bssid,weak_ap,6));
    assert(pick(FUXI_WIFI_UX_PICK_RATE,"IEEE",ap2,&target)==ENOENT);
    assert(pick(FUXI_WIFI_UX_PICK_RATE,"none",NULL,&target)==ENOENT);
    wli_bss_count=0; wli.ux=0;
    puts("PASS fuxi,wifi-ux bit 1: rate estimates, wpa_supplicant order, freshness, protection, given BSSID");
}
/* fuxi,wifi-ux bit 2：已連著又要求加入時，選中的是同一台就不斷線；要換才先斷再加入 */
static void keep_tests(void) {
    uint8_t frame[60]={0};
    pthread_t thread;
    test_ux=FUXI_WIFI_UX_PICK_RATE|FUXI_WIFI_UX_KEEP_LINK;
    bootstrap(); assert(wli.ux==test_ux);
    scan_test(true); full_join();
    /* 同一台：不送任何要求，連線不動 */
    unsigned downs=link_downs, lost=lost_calls; uint8_t last[MESSAGE]; size_t sent=captured_length;
    memcpy(last,captured,sent);
    ask_join("IEEE","password",FUXI_WIFI_STA_WPA2_PSK); assert(!wli_join(&caller_join));
    assert(wli_state.phase==FUXI_WIFI_STA_JOINED && link_downs==downs && lost_calls==lost && wli_tx(frame,60));
    assert(captured_length==sent && !memcmp(captured,last,sent) && !wli.busy);
    for (unsigned index=0;index<32;index++) assert(!wli_asked.pmk[index]);
    /* 太舊要重掃，連著也照掃（自己頻道、再全部）；同名卻沒再聽到：留著連線，不斷 */
    fake_now+=WLI_FRESH_MS+1;
    ask_join("IEEE","password",FUXI_WIFI_STA_WPA2_PSK); thread=start_caller(3,false); wait_busy(2);
    step(); expect_request(2); assert(request_u32(20)==5200);
    scan_accept(false); serve_bss(0,0);
    wait_busy(2); step(); expect_request(2); assert(!request_attribute(20,&(size_t){0}));
    scan_accept(false); serve_bss(0,0);
    assert(!pthread_join(thread,NULL) && !caller_result);
    assert(wli_state.phase==FUXI_WIFI_STA_JOINED && link_downs==downs && wli_tx(frame,60));
    /* 同名、有更快的另一台：先斷，再加入那一台 */
    put_bss(0,ap,"IEEE",5200,-60,2,0,0); put_bss(1,ap2,"IEEE",5200,-45,2,HT|VHT80,0); heard_now(2);
    ask_join("IEEE","password",FUXI_WIFI_STA_WPA2_PSK); thread=start_caller(3,false); wait_busy(4);
    step(); expect_request(6); request_bytes(3,ap,6); respond(6,0); step();
    wait_busy(3); step(); expect_request(4); request_bytes(3,ap2,6);
    respond(4,8); step(); expect_request(6); respond(6,0); step();
    assert(failed_join(thread)==ENOENT && link_downs>downs);
    /* bit 2 沒開：同一台也先斷再加入（舊行為） */
    test_ux=FUXI_WIFI_UX_PICK_RATE;
    bootstrap(); scan_test(true); full_join(); downs=link_downs;
    ask_join("IEEE","password",FUXI_WIFI_STA_WPA2_PSK); thread=start_caller(3,false); wait_busy(4);
    step(); expect_request(6); request_bytes(3,ap,6); respond(6,0); step();
    wait_busy(3); step(); expect_request(4); request_bytes(3,ap,6);
    respond(4,8); step(); expect_request(6); respond(6,0); step();
    assert(failed_join(thread)==ENOENT && link_downs>downs);
    test_ux=0; wli_bss_count=0;
    puts("PASS fuxi,wifi-ux bit 2: same access point kept, unheard rescan kept, better one switched, default leaves");
}
static atomic_bool ap_starter_done;
static int ap_starter_result;
static void *ap_starter_thread(void *context) {
    (void)context;
    struct fuxi_wifi_ap_config cfg={0};
    memcpy(cfg.ssid,"Hotspot",7); cfg.ssid_len=7; cfg.wpa2=1;
    uint32_t freq=0;
    ap_starter_result=wli_ap_start_op(&cfg,&freq);
    atomic_store(&ap_starter_done,true);
    return NULL;
}
static void softap_recovery_tests(void) {
    struct fuxi_wifi_ap_config cfg={0};
    memcpy(cfg.ssid,"Hotspot",7); cfg.ssid_len=7; cfg.wpa2=1;
    uint32_t freq=0;
    test_ap_enabled=true;
    bootstrap();
    assert(wli_backend.ap == &wli_ap_ops);
    memset(&caller_scan,0,sizeof(caller_scan));
    pthread_t thread=begin(2,false); step(); expect_request(2); scan_accept(true); serve_bss(6,0);
    assert(!pthread_join(thread,NULL) && !caller_result && wli_bss_count==5);

    /* 1. SoftAP start/stop does not call nw_wifi_power on stop (fast band switch safe),
     *    but transparently recycles primary netdev on next direct STA join without clearing BSS or flashing OFF. */
    unsigned pcalls=power_calls, downs=link_downs;
    assert(!wli_ap_start_op(&cfg,&freq) && ap_start_calls==1 && wli.ap_active && wli.sta_reset_needed);
    assert(!wli_ap_stop_op() && ap_stop_calls==1 && !wli.ap_active && wli.sta_reset_needed);
    assert(power_calls==pcalls);
    ask_join("IEEE","password",FUXI_WIFI_STA_WPA2_PSK);
    thread=begin(3,false);
    step();
    assert(power_calls==pcalls+1 && requested_power==0 && wli.reset_stage==WLI_RESET_OFF_WAIT);
    assert(wli_state.phase==FUXI_WIFI_STA_JOINING && wli_bss_count==5 && link_downs==downs);
    event_power(false,wli.generation+1);
    assert(wli.reset_stage==WLI_RESET_ON_SEND && wli_state.phase==FUXI_WIFI_STA_JOINING && wli_bss_count==5);
    step();
    assert(power_calls==pcalls+2 && requested_power==1 && wli.reset_stage==WLI_RESET_ON_WAIT);
    event_power(true,wli.generation+1);
    assert(wli.reset_stage==WLI_RESET_DONE && !wli.sta_reset_needed && link_downs==downs);
    /* Next step finishes recycle and immediately sends AUTHENTICATE (through_association). */
    through_association(); message_two(true); keys_and_port();
    assert(!pthread_join(thread,NULL) && !caller_result && wli_state.phase==FUXI_WIFI_STA_JOINED);
    assert(link_downs==downs);

    /* 2. ap_start waits for an in-flight WLI_LEAVE before starting SoftAP. */
    unsigned starts_before=ap_start_calls;
    thread=begin(4,false);
    atomic_store(&ap_starter_done,false);
    pthread_t ap_thread;
    assert(!pthread_create(&ap_thread,NULL,ap_starter_thread,NULL));
    struct timespec pause={.tv_nsec=20000000}; nanosleep(&pause,NULL);
    assert(!atomic_load(&ap_starter_done) && ap_start_calls==starts_before);
    step(); expect_request(6); respond(6,0); step();
    assert(!pthread_join(thread,NULL) && !caller_result);
    assert(!pthread_join(ap_thread,NULL) && !ap_starter_result && ap_start_calls==starts_before+1);
    assert(wli.ap_active && wli.sta_reset_needed);

    /* 3. Fast ap_stop -> ap_start (Maximize Compatibility toggle) triggers zero power recycles,
     *    and the first subsequent WLI_SCAN performs one transparent recycle before scanning. */
    pcalls=power_calls;
    assert(!wli_ap_stop_op() && !wli.ap_active);
    assert(!wli_ap_start_op(&cfg,&freq) && wli.ap_active);
    assert(!wli_ap_stop_op() && !wli.ap_active);
    assert(power_calls==pcalls);
    memset(&caller_scan,0,sizeof(caller_scan));
    thread=begin(2,false);
    step(); assert(power_calls==pcalls+1 && requested_power==0 && wli.reset_stage==WLI_RESET_OFF_WAIT);
    assert(wli_state.phase==FUXI_WIFI_STA_SCANNING);
    event_power(false,wli.generation+1);
    step(); assert(power_calls==pcalls+2 && requested_power==1 && wli.reset_stage==WLI_RESET_ON_WAIT);
    event_power(true,wli.generation+1);
    step(); expect_request(2);
    scan_accept(false); serve_bss(6,0);
    assert(!pthread_join(thread,NULL) && !caller_result && wli_bss_count==5 && !wli.sta_reset_needed);

    /* 4. Manual radio off clears sta_reset_needed so no redundant recycle happens afterwards. */
    assert(!wli_ap_start_op(&cfg,&freq) && !wli_ap_stop_op() && wli.sta_reset_needed);
    power_test(false,wli.generation+1);
    assert(!wli.sta_reset_needed);
    power_test(true,wli.generation+1);
    pcalls=power_calls;
    memset(&caller_scan,0,sizeof(caller_scan));
    thread=begin(2,false); step(); expect_request(2); scan_accept(true); serve_bss(6,0);
    assert(!pthread_join(thread,NULL) && !caller_result && wli_bss_count==5);
    assert(power_calls==pcalls);

    /* 5. 0x107 EAPOL TX status telemetry + 2 consecutive Reason 15 timeouts trigger recycle & recovery. */
    pcalls=power_calls;
    ask_join("IEEE","password",FUXI_WIFI_STA_WPA2_PSK); thread=begin(3,false);
    through_association(); message_two(true);
    event_eapol_status(0); event_eapol_status(1);
    assert(wlj.eapol_tx_nack==1 && wlj.eapol_tx_ack==1);
    event_lost(ap,15,0); step(); step();
    assert(failed_join(thread)==EAUTH && wli.handshake_timeouts==1 && !wli.sta_reset_needed);
    /* Second consecutive Reason 15 (0x107 silent drop) schedules primary interface recycle. */
    ask_join("IEEE","password",FUXI_WIFI_STA_WPA2_PSK); thread=begin(3,false);
    through_association(); message_two(true);
    assert(wlj.eapol_tx_nack==0 && wlj.eapol_tx_ack==0);
    event_lost(ap,15,0); step(); step();
    assert(failed_join(thread)==EAUTH && wli.handshake_timeouts==0 && wli.sta_reset_needed);
    /* Third attempt transparently recycles primary netdev and succeeds cleanly. */
    ask_join("IEEE","password",FUXI_WIFI_STA_WPA2_PSK); thread=begin(3,false);
    step(); assert(power_calls==pcalls+1 && requested_power==0);
    event_power(false,wli.generation+1);
    step(); assert(power_calls==pcalls+2 && requested_power==1);
    event_power(true,wli.generation+1);
    through_association(); message_two(true); keys_and_port();
    assert(!pthread_join(thread,NULL) && !caller_result && wli_state.phase==FUXI_WIFI_STA_JOINED);
    assert(!wli.sta_reset_needed && !wli.handshake_timeouts);

    test_ap_enabled=false;
    puts("PASS SoftAP recovery: ap_start waits for leave, fast toggle untouched, post-AP recycle on scan/join, 0x107 telemetry & repeated Reason 15 recovery");
}

static void set_channels_40(unsigned count) {
    assert(count <= WLI_CHANNEL_MAX);
    wli_channel_count = 0;
    for (unsigned i = 0; i < count; i++) {
        uint16_t freq = i < 13 ? (uint16_t)(2412 + i * 5) : (uint16_t)(5180 + (i - 13) * 20);
        uint16_t flags = i < 11 ? 0 : FUXI_WIFI_STA_CHANNEL_PASSIVE;
        wli_channels[wli_channel_count++] = (struct fuxi_wifi_sta_channel){ .frequency = freq, .flags = flags };
    }
}

static void join_speed_tests(void) {
    struct fuxi_wifi_ap_config cfg = {0};
    memcpy(cfg.ssid, "Hotspot", 7); cfg.ssid_len = 7; cfg.wpa2 = 1;
    uint32_t freq = 0;
    size_t size = 0;

    /* T1: Short-window active reuse (type=1), channel subset check, SSID/passive exclusion, timeout */
    test_ux = FUXI_WIFI_UX_SCAN_REUSE | FUXI_WIFI_UX_SCAN_CHANNELS;
    test_cache = 0;
    bootstrap();
    set_channels_40(40);
    /* Scan A: 13ch 2.4G active */
    memset(&caller_scan, 0, sizeof(caller_scan));
    caller_scan.frequency_count = 13;
    for (unsigned i = 0; i < 13; i++) caller_scan.frequencies[i] = wli_channels[i].frequency;
    pthread_t thread = begin(2, false); step(); expect_request(2);
    scan_accept(false); serve_bss(6, 0);
    assert(!pthread_join(thread, NULL) && !caller_result && wli.cache_valid);
    unsigned seq = wli.sequence;
    /* Scan B: 0ch expanded to 40ch -> not covered by 13ch, must perform real scan */
    memset(&caller_scan, 0, sizeof(caller_scan));
    thread = begin(2, false); step(); expect_request(2);
    assert(wli.sequence == seq + 1);
    scan_accept(false); serve_bss(6, 0);
    assert(!pthread_join(thread, NULL) && !caller_result && wli.cache_valid && wli.scan.frequency_count == 40);
    seq = wli.sequence;
    unsigned hits = wli.cache_hits;
    /* Scan C: 27ch 5G type=1 within 100 ms -> hits 1A short-window reuse without WLI_SCAN */
    memset(&caller_scan, 0, sizeof(caller_scan));
    caller_scan.frequency_count = 27;
    for (unsigned i = 0; i < 27; i++) caller_scan.frequencies[i] = wli_channels[13 + i].frequency;
    fake_now += 100;
    assert(!wli_scan(&caller_scan) && wli.sequence == seq && wli.cache_hits == hits + 1 && !wli.busy);
    /* Exactly at WLI_ACTIVE_REUSE_MS boundary still hits */
    fake_now = wli.scanned_at + WLI_ACTIVE_REUSE_MS;
    assert(!wli_scan(&caller_scan) && wli.sequence == seq && wli.cache_hits == hits + 2);
    /* Past WLI_ACTIVE_REUSE_MS -> must re-scan */
    fake_now = wli.scanned_at + WLI_ACTIVE_REUSE_MS + 1;
    thread = begin(2, false); step(); expect_request(2);
    assert(wli.sequence == seq + 1);
    scan_accept(true); serve_bss(6, 0);
    assert(!pthread_join(thread, NULL) && !caller_result);
    /* Re-establish 40ch active scan, then test passive and SSID exclusions */
    memset(&caller_scan, 0, sizeof(caller_scan));
    thread = begin(2, false); step(); scan_accept(false); serve_bss(6, 0);
    assert(!pthread_join(thread, NULL) && !caller_result);
    seq = wli.sequence;
    memset(&caller_scan, 0, sizeof(caller_scan));
    caller_scan.frequency_count = 27;
    for (unsigned i = 0; i < 27; i++) caller_scan.frequencies[i] = wli_channels[13 + i].frequency;
    caller_scan.passive = true;
    fake_now += 50;
    thread = begin(2, false); step(); expect_request(2);
    assert(wli.sequence == seq + 1);
    scan_accept(false); serve_bss(6, 0);
    assert(!pthread_join(thread, NULL) && !caller_result);
    /* Active Scan C must NOT reuse previous passive scan */
    caller_scan.passive = false;
    fake_now += 50;
    thread = begin(2, false); step(); expect_request(2);
    scan_accept(false); serve_bss(6, 0);
    assert(!pthread_join(thread, NULL) && !caller_result);
    /* Directed SSID scan must NOT reuse and must NOT be reused by non-SSID scan */
    memset(&caller_scan, 0, sizeof(caller_scan));
    thread = begin(2, false); step(); scan_accept(false); serve_bss(6, 0);
    assert(!pthread_join(thread, NULL) && !caller_result);
    caller_scan.frequency_count = 1; caller_scan.frequencies[0] = 5180;
    caller_scan.ssid_length = 4; memcpy(caller_scan.ssid, "IEEE", 4);
    fake_now += 50;
    thread = begin(2, false); step(); expect_request(2);
    scan_accept(false); serve_bss(6, 0);
    assert(!pthread_join(thread, NULL) && !caller_result);
    caller_scan.ssid_length = 0;
    fake_now += 50;
    thread = begin(2, false); step(); expect_request(2);
    scan_accept(false); serve_bss(6, 0);
    assert(!pthread_join(thread, NULL) && !caller_result);

    /* T2: Zero-Reason-15 guard (ap_active / sta_reset_needed blocks both type=1 and type=3 even if cache_valid==1) */
    test_cache = 1; test_ap_enabled = true;
    bootstrap();
    set_channels_40(40);
    memset(&caller_scan, 0, sizeof(caller_scan));
    thread = begin(2, false); step(); scan_accept(false); serve_bss(6, 0);
    assert(!pthread_join(thread, NULL) && !caller_result && wli.cache_valid);
    fake_now += 50;
    /* A. ap_active == 1 with cache_valid == 1 still forces real scan (blocks 1A and type=3) */
    lck_mtx_lock(&wli_lock); wli.ap_active = 1; lck_mtx_unlock(&wli_lock);
    caller_scan.cache_allowed = false;
    thread = begin(2, false); step(); expect_request(2);
    lck_mtx_lock(&wli_lock); wli.ap_active = 0; lck_mtx_unlock(&wli_lock);
    scan_accept(false); serve_bss(6, 0);
    assert(!pthread_join(thread, NULL) && !caller_result && wli.cache_valid);
    fake_now += 50;
    lck_mtx_lock(&wli_lock); wli.ap_active = 1; lck_mtx_unlock(&wli_lock);
    caller_scan.cache_allowed = true;
    thread = begin(2, false); step(); expect_request(2);
    lck_mtx_lock(&wli_lock); wli.ap_active = 0; lck_mtx_unlock(&wli_lock);
    scan_accept(false); serve_bss(6, 0);
    assert(!pthread_join(thread, NULL) && !caller_result && wli.cache_valid);
    /* B. sta_reset_needed == 1 with cache_valid == 1 blocks type=1 and type=3 and triggers power recycle */
    for (int mode = 0; mode < 2; mode++) {
        fake_now += 50;
        lck_mtx_lock(&wli_lock); wli.sta_reset_needed = 1; assert(wli.cache_valid); lck_mtx_unlock(&wli_lock);
        unsigned pcalls = power_calls;
        caller_scan.cache_allowed = (mode == 1);
        thread = begin(2, false);
        step(); assert(power_calls == pcalls + 1 && requested_power == 0 && wli.reset_stage == WLI_RESET_OFF_WAIT);
        event_power(false, wli.generation + 1);
        step(); assert(power_calls == pcalls + 2 && requested_power == 1 && wli.reset_stage == WLI_RESET_ON_WAIT);
        event_power(true, wli.generation + 1);
        step(); expect_request(2);
        scan_accept(false); serve_bss(6, 0);
        assert(!pthread_join(thread, NULL) && !caller_result && !wli.sta_reset_needed && wli.cache_valid);
    }
    /* C. Two consecutive Reason 15 timeouts clear cache_valid and set sta_reset_needed */
    for (int r = 0; r < 2; r++) {
        ask_join("IEEE", "password", FUXI_WIFI_STA_WPA2_PSK);
        thread = begin(3, false);
        through_association(); message_two(true);
        event_lost(ap, 15, 0); step(); step();
        assert(failed_join(thread) == EAUTH);
    }
    assert(wli.sta_reset_needed && !wli.cache_valid);
    unsigned pcalls = power_calls;
    caller_scan.cache_allowed = false;
    thread = begin(2, false);
    step(); assert(power_calls == pcalls + 1 && requested_power == 0);
    event_power(false, wli.generation + 1);
    step(); assert(power_calls == pcalls + 2 && requested_power == 1);
    event_power(true, wli.generation + 1);
    step(); expect_request(2);
    scan_accept(false); serve_bss(6, 0);
    assert(!pthread_join(thread, NULL) && !caller_result && !wli.sta_reset_needed);

    /* T3: 1C boot-time ap_stop does not spuriously set sta_reset_needed; real ap_start -> ap_stop does */
    test_cache = 0; test_ux = 0; test_ap_enabled = true;
    bootstrap();
    pcalls = power_calls;
    assert(!wli_ap_stop_op() && !wli.ap_active && !wli.ap_started && !wli.sta_reset_needed);
    memset(&caller_scan, 0, sizeof(caller_scan));
    thread = begin(2, false); step(); expect_request(2);
    assert(power_calls == pcalls);
    scan_accept(false); serve_bss(6, 0);
    assert(!pthread_join(thread, NULL) && !caller_result);
    assert(!wli_ap_start_op(&cfg, &freq) && wli.ap_active && wli.ap_started && wli.sta_reset_needed);
    assert(!wli_ap_stop_op() && !wli.ap_active && !wli.ap_started && wli.sta_reset_needed);
    /* Complete recycle via scan, then a redundant ap_stop must not set sta_reset_needed again */
    thread = begin(2, false);
    step(); assert(power_calls == pcalls + 1 && requested_power == 0);
    event_power(false, wli.generation + 1);
    step(); assert(power_calls == pcalls + 2 && requested_power == 1);
    event_power(true, wli.generation + 1);
    step(); expect_request(2); scan_accept(false); serve_bss(6, 0);
    assert(!pthread_join(thread, NULL) && !caller_result && !wli.sta_reset_needed);
    assert(!wli_ap_stop_op() && !wli.sta_reset_needed);
    test_ap_enabled = false;

    /* T4: 1B bounded default dwell_ms=110 with 19000 ms batching (31+9), 13ch no regression, 41ch/batch=0 fallback */
    test_ux = FUXI_WIFI_UX_SCAN_DWELL_110 | FUXI_WIFI_UX_SCAN_CHANNELS;
    test_dwell = 1; test_batch = 1; offered_features = FWC_FEATURE_SCAN_DWELL_MS;
    bootstrap();
    set_channels_40(40);
    /* 40ch (11 active + 29 passive), dwell_ms=0 -> defaults to 110 ms and batches 31 + 9 */
    memset(&caller_scan, 0, sizeof(caller_scan));
    thread = begin(2, false); step(); expect_request(2);
    assert(wli.scan_count == 40 && wli.scan_limit == 31 && request_u32(FWC_ATTR_SCAN_DWELL_MS) == 110);
    const uint8_t *freqs = request_attribute(20, &size);
    assert(freqs && size == 31 * 4);
    scan_accept(false); step(); expect_request(2);
    assert(wli.scan_offset == 31 && request_u32(FWC_ATTR_SCAN_DWELL_MS) == 110);
    freqs = request_attribute(20, &size);
    assert(freqs && size == 9 * 4);
    scan_accept(true); serve_bss(6, 0);
    assert(!pthread_join(thread, NULL) && !caller_result);
    /* 13ch 2.4G (11 active + 2 passive): passive*44=88 <= active*56=616 -> keeps dwell_ms=0 (no regression) */
    memset(&caller_scan, 0, sizeof(caller_scan));
    caller_scan.frequency_count = 13;
    for (unsigned i = 0; i < 13; i++) caller_scan.frequencies[i] = wli_channels[i].frequency;
    thread = begin(2, false); step(); expect_request(2);
    assert(!request_attribute(FWC_ATTR_SCAN_DWELL_MS, &size));
    scan_accept(false); serve_bss(6, 0);
    assert(!pthread_join(thread, NULL) && !caller_result);
    /* 27ch 5G (all passive): applies 110 ms in 1 batch */
    memset(&caller_scan, 0, sizeof(caller_scan));
    caller_scan.frequency_count = 27;
    for (unsigned i = 0; i < 27; i++) caller_scan.frequencies[i] = wli_channels[13 + i].frequency;
    thread = begin(2, false); step(); expect_request(2);
    assert(wli.scan_count == 27 && wli.scan_limit == 27 && request_u32(FWC_ATTR_SCAN_DWELL_MS) == 110);
    scan_accept(false); serve_bss(6, 0);
    assert(!pthread_join(thread, NULL) && !caller_result);
    /* 41ch (>25000 ms budget) safely falls back to dwell_ms=0 without EINVAL */
    set_channels_40(41);
    memset(&caller_scan, 0, sizeof(caller_scan));
    thread = begin(2, false); step(); expect_request(2);
    assert(!request_attribute(FWC_ATTR_SCAN_DWELL_MS, &size));
    scan_accept(false); serve_bss(6, 0);
    assert(!pthread_join(thread, NULL) && !caller_result);
    /* batch=0 with 40ch (>19000 ms single-batch budget) safely falls back to dwell_ms=0 */
    test_batch = 0; bootstrap(); set_channels_40(40);
    memset(&caller_scan, 0, sizeof(caller_scan));
    thread = begin(2, false); step(); expect_request(2);
    assert(!request_attribute(FWC_ATTR_SCAN_DWELL_MS, &size));
    scan_accept(false); serve_bss(6, 0);
    assert(!pthread_join(thread, NULL) && !caller_result);
    /* No module feature or test_dwell=0 -> dwell_ms=0 still succeeds without FWC_ATTR_SCAN_DWELL_MS */
    test_batch = 1; offered_features = 0; bootstrap(); set_channels_40(40);
    thread = begin(2, false); step(); expect_request(2);
    assert(!request_attribute(FWC_ATTR_SCAN_DWELL_MS, &size));
    scan_accept(false); serve_bss(6, 0);
    assert(!pthread_join(thread, NULL) && !caller_result);

    test_ux = test_dwell = test_cache = test_batch = offered_features = 0;
    memset(&caller_scan, 0, sizeof(caller_scan));
    puts("PASS join speed: short-window reuse, Reason-15/SoftAP guards, boot ap_stop no-op, bounded 110ms dwell & batching");
}
int main(void) {
    unsigned config_offset;
    assert(!fwa_config_offset(0,4,0,&config_offset));
    uint8_t address[6]={2,0x53,0x57,0x45,0x45,1}; char text[13];
    assert(!fwa_mac_text(address,6,text) && !strcmp(text,"025357454501"));
    assert(fwa_mac_text(address,5,text) && fwa_mac_text(NULL,6,text));
    address[0]=1; assert(fwa_mac_text(address,6,text));
    memset(address,0,6); assert(fwa_mac_text(address,6,text));
    make_fixtures();
    dwell_tests();
    batch_tests();
    cache_tests();
    scan_recovery_tests();
    scan_scope_tests();
    rx_batch_tests();
    power_off_join_test();
    bootstrap(); power_test(false,3); power_test(true,4); power_test(true,4);
    memset(&caller_scan,0,sizeof(caller_scan)); caller_scan.ssid_length=4;
    memcpy(caller_scan.ssid,"real",4); caller_scan.frequency_count=64;
    for(unsigned index=0;index<64;index++) caller_scan.frequencies[index]=2412;
    fallback_test();
    scan_test(true); scan_test(false);
    join_tests();
    pick_tests();
    keep_tests();
    softap_recovery_tests();
    join_speed_tests();
    bootstrap();
    uint8_t bytes[MESSAGE]; size_t length=header(bytes,3,0x102,2);
    assert(deliver(bytes,length)==2 && wli.fatal==EPROTO);
    bootstrap(); length=header(bytes,3,0x104,1); length=put32(bytes,length,18,0);
    assert(!deliver(bytes,length) && !wli.fatal);
    bootstrap(); pthread_t thread=begin(1,false); assert(!wli_step(20));
    length=header(bytes,3,0x10a,3); uint8_t wrong[4]={1};
    length=wli_put(bytes,length,32,wrong,4);
    assert(deliver(bytes,length)==2); assert(!pthread_join(thread,NULL) && caller_result==EPROTO);
    bootstrap(); thread=begin(2,false); assert(!wli_step(1));
    length=header(bytes,2,2,2); fwc_write(bytes+12,wli.pending_sequence+1,4);
    assert(deliver(bytes,length)==2); assert(!pthread_join(thread,NULL) && caller_result==EPROTO);
    bootstrap(); thread=begin(1,false); assert(!wli_step(1));
    assert(wli_step(30001)==ETIMEDOUT); wli_fail(ETIMEDOUT);
    assert(!pthread_join(thread,NULL) && caller_result==ETIMEDOUT);
    bootstrap(); timeout_once=true;
    assert(wli_power(false)==ETIMEDOUT && wli_step(99)==ETIMEDOUT);
    bootstrap(); length=header(bytes,3,0x100,2);
    fwc_write(bytes+16,length,4); assert(fwc_message_valid(9,bytes,length));
    for(size_t short_length=0;short_length<40;short_length++)
        assert(!fwc_message_valid(9,bytes,short_length));
    assert(!fwc_message_valid(10,bytes,length));
    fwc_write(bytes+16,44,4); assert(!fwc_message_valid(9,bytes,length));
    test_mode=2; memcpy(test_ssid,"fcb",4); unsigned started=threads_started; bootstrap();
    assert(threads_started==started+1 && wli_test_join.ssid_length==3 && !memcmp(wli_test_join.ssid,"fcb",3));
    test_ssid[0]=0; bootstrap(); assert(threads_started==started+1); test_mode=0;
    /* fuxiwifitest=3: wifid hears only the named network; no test thread, nothing else changes. */
    test_mode=3; memcpy(test_ssid,"fcb",4); started=threads_started; bootstrap();
    assert(threads_started==started && wli_ui_only_length==3 && !memcmp(wli_ui_only,"fcb",3));
    memset(wli_bss,0,3*sizeof(wli_bss[0])); wli_bss_count=3;
    wli_bss[0].ssid_length=4; memcpy(wli_bss[0].ssid,"fcbx",4);
    wli_bss[1].ssid_length=3; memcpy(wli_bss[1].ssid,"fcb",3); wli_bss[1].rssi=-40;
    wli_bss[2].ssid_length=2; memcpy(wli_bss[2].ssid,"fc",2);
    unsigned heard_seen=0; wli_heard(count_heard,&heard_seen); assert(heard_seen==1 && last_heard_rssi==-40);
    wli_ui_only_length=0; heard_seen=0; wli_heard(count_heard,&heard_seen); assert(heard_seen==3);
    test_ssid[0]=0; bootstrap(); assert(wli_ui_only_length==0); test_mode=0; wli_bss_count=0;
    puts("PASS native bridge: power acknowledgements, generations, directed scans with every element "
         "(trimmed whole), module-record fallback, WPA2-PSK and open joins with the four-way and group "
         "handshakes, keys, port, frames both ways, refusals, timeouts, loss, leave and stale rejection");
}
'''


def vectors():
    # The same independent IEEE PRF/HMAC peer as tools/test-fuxi-wifi-ios.py: station 02:46:55:58:49:01,
    # access point ...:02, station nonce 0..31 (the model's read_random), access point nonce 32..63.
    pmk = hashlib.pbkdf2_hmac('sha1', b'password', b'IEEE', 4096, 32)
    own, peer = bytes.fromhex('024655584901'), bytes.fromhex('024655584902')
    snonce, anonce = bytes(range(32)), bytes(range(32, 64))
    data = min(own, peer) + max(own, peer) + min(snonce, anonce) + max(snonce, anonce)
    ptk = b''.join(hmac.digest(pmk, b'Pairwise key expansion\0' + data + bytes([i]), 'sha1')
                   for i in range(3))[:48]

    def message(info, counter, payload=b'', signed=False):
        out = bytearray(99 + len(payload))
        out[:4] = bytes([2, 3]) + (len(out) - 4).to_bytes(2, 'big')
        out[4] = 2
        out[5:7] = info.to_bytes(2, 'big')
        out[9:17] = counter.to_bytes(8, 'big')
        out[17:49] = anonce
        out[97:99] = len(payload).to_bytes(2, 'big')
        out[99:] = payload
        if signed:
            out[81:97] = hmac.digest(ptk[:16], out, 'sha1')[:16]
        return bytes(out)

    def array(name, value):
        return f'static const unsigned char {name}[]={{' + ','.join(map(str, value)) + '};\n'

    return ''.join(array(name, value) for name, value in [
        ('pmk', pmk), ('snonce', snonce), ('tk', ptk[32:]), ('gtk', bytes(range(16))),
        ('m1', message(0x8a, 1)), ('m3', message(0x13ca, 2, WRAPPED, True)),
        ('g1', message(0x1382, 3, WRAPPED, True))])


def stripped(path):
    return re.sub(r'^#include[^\n]*\n', '', path.read_text(), flags=re.M)


def main():
    code = MODEL + stripped(IOS / 'fuxi_wifi_sta.h') + stripped(IOS / 'fuxi_wifi_ios.h')
    code += stripped(IOS / 'fuxi_wifi_wpa.h')
    code += stripped(SOURCE / 'fuxi_wlan_ios.inc') + stripped(IOS / 'fuxi_wifi_wpa.c') + vectors() + TEST
    with tempfile.TemporaryDirectory(prefix='fuxi-wlan-ios-') as directory:
        source = Path(directory) / 'bridge.c'
        binary = Path(directory) / 'bridge'
        source.write_text(code)
        subprocess.run([os.environ.get('CC', 'clang'), '-std=gnu11', '-O1', '-g',
                        '-Wall', '-Wextra', '-Werror', '-pthread', '-fsanitize=address,undefined',
                        '-I', str(SOURCE), str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=60)


if __name__ == '__main__':
    main()
