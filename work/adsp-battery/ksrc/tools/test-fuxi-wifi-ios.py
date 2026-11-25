#!/usr/bin/env python3
"""P-WIFI production-C/ABI/WPA and mounted-root fixtures. No phone or network."""
from pathlib import Path
import hashlib
import hmac
import importlib.util
import os
import plistlib
import re
import subprocess
import tempfile
from cryptography.hazmat.primitives.keywrap import aes_key_wrap

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'xnu/fuxi-wifi-ios'

MODEL = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <stdatomic.h>
#define FUXI 1
#define KERNEL 1
#define __unused __attribute__((unused))
#define EAUTH 80
#define Z_WAITOK 1
#define Z_ZERO 2
#define KERN_SUCCESS 0
#define THREAD_UNINT 0
#define THREAD_ABORTSAFE 2
#define THREAD_AWAKENED 0
#define THREAD_TIMED_OUT 1
#define THREAD_INTERRUPTED 2
#define LCK_SLEEP_DEFAULT 0
#define USER_ADDR_NULL 0
#define KEV_VENDOR_APPLE 1
#define KEV_IEEE80211_CLASS 6
#define __DECONST(t,p) ((t)(uintptr_t)(p))
#define LCK_GRP_DECLARE(n,s) int n __attribute__((unused))
#define LCK_MTX_DECLARE(n,g) pthread_mutex_t n = PTHREAD_MUTEX_INITIALIZER
#define lck_mtx_lock pthread_mutex_lock
#define lck_mtx_unlock pthread_mutex_unlock
#define kalloc_data(n,f) calloc(1,n)
#define kfree_data(p,n) free(p)
typedef uintptr_t user_addr_t;
typedef unsigned long u_long;
typedef int errno_t;
typedef void *event_t;
typedef int wait_result_t;
typedef pthread_t thread_t;
typedef const void *mbuf_t;
static int opt, trace_opt, superuser = 1, fail_copy;
static const char *caller = "fixture";
static unsigned user_traces;
static unsigned registry, attached;
static atomic_uint events[64];
static bool link_up;
static unsigned received;
static pthread_cond_t wake_cond = PTHREAD_COND_INITIALIZER;
/* hold_wakeups（在 sta_lock 下讀寫）：station 執行緒被叫醒也繼續睡，用來看「要求還在排隊」時的回報 */
static bool hold_wakeups; static unsigned sleeping;
static void lck_mtx_sleep(pthread_mutex_t *m, int flags, event_t e, int t) {
 (void)flags; (void)e; (void)t; sleeping++;
 do pthread_cond_wait(&wake_cond,m); while (hold_wakeups);
 sleeping--;
}
static wait_result_t lck_mtx_sleep_deadline(pthread_mutex_t *m, int flags, event_t e, int t, uint64_t deadline) {
 (void)flags; (void)e; (void)t;
 struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
 uint64_t ns = (uint64_t)ts.tv_nsec + deadline;
 ts.tv_sec += (time_t)(ns / 1000000000ull);
 ts.tv_nsec = (long)(ns % 1000000000ull);
 int rc = pthread_cond_timedwait(&wake_cond, m, &ts);
 return rc == ETIMEDOUT ? THREAD_TIMED_OUT : THREAD_AWAKENED;
}
static void thread_wakeup(event_t e) { (void)e; pthread_cond_broadcast(&wake_cond); }
static void thread_set_thread_name(thread_t t, const char *n) { (void)t; (void)n; }
static void thread_deallocate(thread_t t) { pthread_detach(t); }
static void (*worker)(void *,wait_result_t);
static void *run(void *p) { worker(p,0); return NULL; }
static int kernel_thread_start(void (*f)(void *,wait_result_t),void *p,thread_t *t) {
 worker=f; return pthread_create(t,NULL,run,p);
}
static int memset_s(void *p,size_t n,int c,size_t count) { assert(count<=n); memset(p,c,count); return 0; }
static int copyin(user_addr_t p,void *out,size_t n) {
 if (!p || fail_copy) return EFAULT; memcpy(out,(void *)p,n); return 0;
}
static int copyout(const void *in,user_addr_t p,size_t n) {
 if (!p || fail_copy) return EFAULT; memcpy((void *)p,in,n); return 0;
}
static void proc_selfname(char *p,int n) { snprintf(p,n,"%s",caller); }
static void *current_proc(void) { return NULL; }
static int proc_suser(void *p) { (void)p; return !superuser; }
struct kev_msg { unsigned vendor_code,kev_class,kev_subclass,event_code;
 struct { unsigned data_length; void *data_ptr; } dv[2]; };
static atomic_uint ap_events[64];
static uint8_t en0_last_sta_num;
static int kev_post_msg(struct kev_msg *m) {
 assert(m->kev_class==6 && m->kev_subclass==1 && m->dv[0].data_length==16);
 bool is_ap = !strcmp(m->dv[0].data_ptr,"ap1");
 assert(is_ap || !strcmp(m->dv[0].data_ptr,"en0"));
 if (m->event_code==4) assert(m->dv[1].data_length==32);
 if (is_ap) {
  if (m->event_code==12) assert(m->dv[1].data_length==32);
  if (m->event_code==13) assert(m->dv[1].data_length==16);
  assert(m->event_code<64); atomic_fetch_add(&ap_events[m->event_code],1); return 0;
 }
 if (m->event_code==12) {
  const uint8_t *d=m->dv[1].data_ptr;
  assert(m->dv[1].data_length==32); en0_last_sta_num=d[8];
 }
 if (m->event_code==13) {
  const uint8_t *d=m->dv[1].data_ptr;
  assert(m->dv[1].data_length==16); en0_last_sta_num=d[8];
 }
 if (m->event_code==39) {
  const uint8_t *d=m->dv[1].data_ptr; int32_t rssi; int16_t snr,noise;
  assert(m->dv[1].data_length==180 && d[0]==1 && d[0xb]==1 && d[0xe]==1 && !d[0x12]);
  memcpy(&rssi,d+4,4); memcpy(&snr,d+0xc,2); memcpy(&noise,d+0x10,2);
  assert(rssi==-42 && noise==-95 && snr==53 && (int8_t)d[9]==-42);
 }
 assert(m->event_code<64); atomic_fetch_add(&events[m->event_code],1); return 0;
}
typedef void *thread_call_param_t;
typedef void (*thread_call_func_t)(thread_call_param_t,thread_call_param_t);
typedef thread_call_func_t *thread_call_t;
#define NSEC_PER_SEC 1000000000u
static thread_call_func_t timer_fn, timer_slots[4]; static unsigned timer_armed, timer_slot_count, ap_replay_armed;
static thread_call_t thread_call_allocate(thread_call_func_t f,thread_call_param_t p) {
 (void)p;
 if (!timer_fn) { timer_fn=f; return &timer_fn; }
 assert(timer_slot_count < 4); timer_slots[timer_slot_count]=f; return &timer_slots[timer_slot_count++];
}
static void clock_interval_to_deadline(uint32_t i,uint32_t s,uint64_t *d) { *d=(uint64_t)i*s; }
/* 開關耗時診斷用的時鐘（替身：單調時鐘的奈秒值） */
static uint64_t mach_absolute_time(void) {
 struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (uint64_t)t.tv_sec*1000000000ull+(uint64_t)t.tv_nsec;
}
static void absolutetime_to_nanoseconds(uint64_t a,uint64_t *n) { *n=a; }
static int thread_call_enter_delayed(thread_call_t c,uint64_t d) {
 if (c==&timer_fn) { assert(d==5ull*NSEC_PER_SEC); timer_armed++; return 0; }
 assert(c >= &timer_slots[0] && c < &timer_slots[timer_slot_count] && d==350000000ull);
 ap_replay_armed++; return 0;
}
static int thread_call_cancel(thread_call_t c) { (void)c; return 0; }
static int PE_parse_boot_argn(const char *key,void *p,int n) {
 if (!strcmp(key,"fuxiwifitrace")) { assert(n==4); memcpy(p,&trace_opt,4); return trace_opt!=0; }
 assert(!strcmp(key,"fuxiwifiios") && n==4); memcpy(p,&opt,4); return opt!=0;
}
/* ADT /chosen：fuxi,wifi-rssi 與 fuxi,wifi-ux 兩個屬性（值 0 表示沒有），長度可改來測錯誤長度 */
typedef const void *DTEntry;
#define kSuccess 0
#define kError (-1)
static uint32_t adt_rssi, adt_ux, adt_ap; static unsigned adt_rssi_bytes=4, adt_ux_bytes=4, adt_lookups;
static int SecureDTLookupEntry(const void *root,const char *path,DTEntry *entry) {
 assert(!root && !strcmp(path,"/chosen")); adt_lookups++; *entry=&adt_rssi; return kSuccess;
}
static int SecureDTGetProperty(DTEntry entry,const char *name,void const **prop,unsigned int *bytes) {
 assert(entry==&adt_rssi);
 if (!strcmp(name,"fuxi,wifi-ap")) {
  if (!adt_ap) return kError;
  *prop=&adt_ap; *bytes=4; return kSuccess;
 }
 if (!strcmp(name,"fuxi,wifi-ux")) {
  if (!adt_ux) return kError;
  *prop=&adt_ux; *bytes=adt_ux_bytes; return kSuccess;
 }
 assert(!strcmp(name,"fuxi,wifi-rssi"));
 if (!adt_rssi) return kError;
 *prop=&adt_rssi; *bytes=adt_rssi_bytes; return kSuccess;
}
static int mbuf_copydata(mbuf_t p,size_t off,size_t n,void *out) { memcpy(out,(const char *)p+off,n);return 0; }
static void FuxiNetworkStackStart(void) { registry++; }
static bool ap_attached, ap_link_state;
static void fuxi_wifi_ap_net_input(const uint8_t *p,size_t n) { (void)p;(void)n; }
static void fuxi_wifi_ap_net_link_state(bool up) { ap_link_state=up; }
static void fuxi_wifi_net_input(const uint8_t *p,size_t n) { assert(p[12]==0x88 && n>=14); received++; }
static void fuxi_wifi_net_input_packet(void *source,size_t length,int (*read)(void *,size_t,void *,size_t)) {
 (void)source;(void)length;(void)read;assert(!"synthetic backend must not use the native packet reader");
}
static void fuxi_wifi_net_flush_rx(void) { assert(!"synthetic backend must not batch"); }
static bool fuxi_wifi_net_byte_counts(uint64_t *rx,uint64_t *tx) { (void)rx;(void)tx;return false; }
/* link_hold：把 station 執行緒停在後端的電源切換裡（假後端切電源時會先報 link down） */
static atomic_int link_hold, link_held;
static void fuxi_wifi_net_link_state(bool up) {
 link_up=up;
 if (atomic_load(&link_hold)) {
  struct timespec pause={.tv_nsec=1000000};
  atomic_store(&link_held,1);
  while (atomic_load(&link_hold)) nanosleep(&pause,NULL);
  atomic_store(&link_held,0);
 }
}
'''

TEST = r'''
void sweet_prof_user_bt(const char *tag) {
 assert(!strcmp(tag,"FUXI WIFI IOC383"));
 assert(!pthread_mutex_trylock(&apple80211_lock)); pthread_mutex_unlock(&apple80211_lock);
 assert(!pthread_mutex_trylock(&ios_lock)); pthread_mutex_unlock(&ios_lock);
 user_traces++;
}
bool fuxi_wifi_net_attach(const struct fuxi_wifi_net_link *p) {
 assert(p->wireless && !p->up && p->address[0]==2); attached++; return true;
}
bool fuxi_wifi_ap_net_attach(const struct fuxi_wifi_net_link *p) { (void)p; ap_attached=true; return true; }
static uint32_t ap_mock_cipher; static unsigned ap_mock_starts, ap_mock_stops, ap_mock_keys, ap_mock_auths, ap_mock_deauths;
static uint8_t ap_mock_flags; static char ap_mock_cc[2];
static int ap_m_start(const struct fuxi_wifi_ap_config *c, uint32_t *f) { assert(c->ssid_len==7 && c->wpa2); ap_mock_flags=c->flags; ap_mock_cc[0]=c->country[0]; ap_mock_cc[1]=c->country[1]; ap_mock_starts++; *f=(c->flags & FUXI_WIFI_AP_CFG_PREFER_5GHZ)?5745:2437; return 0; }
static int ap_m_stop(void) { ap_mock_stops++; return 0; }
static int ap_m_set_key(const uint8_t *mac,uint8_t idx,uint32_t cipher,const uint8_t *k,size_t kl,const uint8_t *s,size_t sl,int del) {
 (void)mac;(void)idx;(void)k;(void)kl;(void)s;(void)sl;(void)del; ap_mock_cipher=cipher; ap_mock_keys++; return 0;
}
static int ap_m_get_seq(const uint8_t *mac,uint8_t idx,uint8_t out[8]) { (void)mac;(void)idx; out[0]=0x11; out[1]=0x22; return 0; }
static int ap_m_auth(const uint8_t *mac,int a) { (void)mac; assert(a==1); ap_mock_auths++; return 0; }
static int ap_m_deauth(const uint8_t *mac,uint16_t r,int d) { (void)mac;(void)r;(void)d; ap_mock_deauths++; return 0; }
static bool ap_m_tx(const uint8_t *f,size_t n) { (void)f;(void)n; return true; }
static void ap_m_scb(const struct fuxi_wifi_ap_input *in) { (void)in; }
static const struct fuxi_wifi_ap_ops ap_mock_ops = {
 .address={0x02,0x46,0x55,0x58,0x49,0x00}, .start_cb=ap_m_scb, .ap_start=ap_m_start, .ap_stop=ap_m_stop,
 .set_key=ap_m_set_key, .get_key_seq=ap_m_get_seq, .sta_authorize=ap_m_auth, .sta_deauth=ap_m_deauth, .tx_frame=ap_m_tx
};
static void settle(void) {
 struct timespec pause = { .tv_nsec=1000000 };
 for (unsigned i=0;i<5000;i++) {
  if (!fuxi_wifi_sta_busy()) return;
  nanosleep(&pause,NULL);
 }
 assert(!"station timeout");
}
static int request(bool set,int type,void *p,unsigned n,int *val) {
 struct apple80211req req = { .req_if_name="en0",.req_type=type,.req_len=n,.req_data=(uintptr_t)p };
 int error=fuxi_wifi_apple80211_ioctl(set?SIOCSA80211:SIOCGA80211,&req);
 if(val) *val=req.req_val; return error;
}
/* station 執行緒睡著之後才擋住它，免得它還沒睡就把要求拿走 */
static void hold_station(void) {
 struct timespec pause = { .tv_nsec=1000000 };
 for (unsigned i=0;i<5000;i++) {
  pthread_mutex_lock(&sta_lock);
  if (sleeping) { hold_wakeups=true; pthread_mutex_unlock(&sta_lock); return; }
  pthread_mutex_unlock(&sta_lock);
  nanosleep(&pause,NULL);
 }
 assert(!"station never slept");
}
static void release_station(void) {
 pthread_mutex_lock(&sta_lock); hold_wakeups=false; pthread_mutex_unlock(&sta_lock);
 thread_wakeup(NULL); settle();
}
static void wait_link_held(void) {
 struct timespec pause = { .tv_nsec=1000000 };
 for (unsigned i=0;i<5000 && !atomic_load(&link_held);i++) nanosleep(&pause,NULL);
 assert(atomic_load(&link_held));
}
static bool radio_on(void) {
 pthread_mutex_lock(&sta_lock); bool on=sta_on; pthread_mutex_unlock(&sta_lock); return on;
}
static void set_ux(uint32_t value,unsigned bytes) { adt_ux=value; adt_ux_bytes=bytes; wifi_ux_cached=0; }
static uint32_t said_power(void) {
 struct { uint32_t version,radios,state[4]; } answer={0};
 assert(!request(false,19,&answer,sizeof(answer),NULL) && answer.version==1 && answer.radios==1);
 return answer.state[0];
}
static atomic_uint hostapd_exit_deauth_sent;
static void *delayed_hostapd_exit_thread(void *arg) {
 (void)arg;
 struct timespec pause = { .tv_nsec = 30000000 }; /* 30 ms slow hostapd teardown */
 nanosleep(&pause, NULL);
 struct apple80211_sta_deauth sd = { .version = 1, .reason = 2 };
 memset(sd.mac, 0xff, 6);
 struct apple80211req req = { .req_if_name = "ap1", .req_type = 76, .req_len = sizeof(sd), .req_data = (uintptr_t)&sd };
 assert(fuxi_wifi_apple80211_ioctl(SIOCSA80211, &req) == 0);
 atomic_store(&hostapd_exit_deauth_sent, 1);
 return NULL;
}
int main(int argc,char **argv) {
 (void)argv;
 if(argc==2) {
  opt=2; fuxi_wifi_ios_init();
  assert(!attached && !registry && !fuxi_wifi_sta_present() && fuxi_wifi_ios_native_ready());
  assert(request(true,383,NULL,4104,NULL)==EOPNOTSUPP && !user_traces);
  trace_opt=1;
  assert(request(true,383,NULL,4104,NULL)==EOPNOTSUPP && !user_traces);
  caller="wifid-client";
  assert(request(true,383,NULL,4104,NULL)==EOPNOTSUPP && !user_traces);
  caller="wifid";
  for(trace_opt=-1;trace_opt<=2;trace_opt++) {
   if(trace_opt==1) continue;
   assert(request(true,383,NULL,4104,NULL)==EOPNOTSUPP && !user_traces);
  }
  trace_opt=1;
  assert(request(false,383,NULL,4104,NULL)==EOPNOTSUPP && !user_traces);
  assert(request(true,382,NULL,4104,NULL)==EOPNOTSUPP && !user_traces);
  trace_unsupported_request(383,true,EINVAL,caller); assert(!user_traces);
  superuser=0; assert(request(true,383,NULL,4104,NULL)==EPERM && !user_traces); superuser=1;
  for(unsigned index=0;index<4;index++) {
   assert(request(true,383,NULL,4104,NULL)==EOPNOTSUPP);
   assert(user_traces==(index ? 2u : 1u));
  }
  puts("PASS: native opt-in waits for real backend without publishing synthetic interface"); return 0;
 }
 assert(sizeof(struct apple80211req)==40 && offsetof(struct apple80211req,req_data)==32);
 fuxi_wifi_ios_init(); assert(!attached && !registry && !fuxi_wifi_sta_present());
 opt=3; fuxi_wifi_ios_init(); assert(!attached);
 opt=1; fuxi_wifi_ios_init(); assert(attached==1 && registry==1);
 trace_opt=1; caller="wifid";
 assert(request(true,383,NULL,4104,NULL)==EOPNOTSUPP && !user_traces);
 caller="fixture"; trace_opt=0;
 fuxi_wifi_ios_init(); assert(attached==1);
 assert(!fuxi_wifi_ios_attach(&fake_backend));
 assert(fuxi_wifi_sta_ask_scan(NULL)==EINVAL && fuxi_wifi_sta_ask_join(NULL)==EINVAL);
 struct fuxi_wifi_sta_scan bad_scan={.frequency_count=65};
 assert(fuxi_wifi_sta_ask_scan(&bad_scan)==EINVAL);
 struct apple80211req invalid={.req_if_name="ue0"};
 assert(fuxi_wifi_apple80211_ioctl(SIOCGA80211,&invalid)==ENXIO);
 assert(request(false,999,NULL,0,NULL)==EOPNOTSUPP);
 /* RSSI_BOUNDS (106)：預設照舊拒絕；ADT fuxi,wifi-rssi=1（4 bytes）才回 {1,-128,127,-128}，不必先加入網路 */
 struct { uint32_t version; int32_t minimum,maximum,threshold; } bounds={0};
 assert(request(false,106,&bounds,sizeof(bounds),NULL)==EOPNOTSUPP && !bounds.version);
 adt_rssi=2; assert(request(false,106,&bounds,sizeof(bounds),NULL)==EOPNOTSUPP && !bounds.version);
 adt_rssi=1; adt_rssi_bytes=8; assert(request(false,106,&bounds,sizeof(bounds),NULL)==EOPNOTSUPP && !bounds.version);
 adt_rssi_bytes=4; assert(!request(false,106,&bounds,sizeof(bounds),NULL));
 assert(bounds.version==1 && bounds.minimum==-128 && bounds.maximum==127 && bounds.threshold==-128);
 assert(request(false,106,NULL,sizeof(bounds),NULL)==EFAULT);
 struct { uint32_t version; int32_t minimum; } shorter={0};
 assert(!request(false,106,&shorter,sizeof(shorter),NULL) && shorter.version==1 && shorter.minimum==-128);
 assert(adt_lookups>=6); adt_rssi=0;
 struct apple80211_scan_request scan={.version=1};
 assert(request(true,10,&scan,sizeof(scan)-1,NULL)==EINVAL);
 scan.ssid_length=33; assert(request(true,10,&scan,sizeof(scan),NULL)==EINVAL); scan.ssid_length=0;
 scan.channel_count=401; assert(request(true,10,&scan,sizeof(scan),NULL)==EINVAL); scan.channel_count=0;
 superuser=0; assert(request(true,10,&scan,sizeof(scan),NULL)==EPERM); superuser=1;
 fail_copy=1; assert(request(true,10,&scan,sizeof(scan),NULL)==EFAULT); fail_copy=0;
 assert(request(true,10,&scan,sizeof(scan),NULL)==0); settle();
 assert(atomic_load(&events[10])==1);
 struct apple80211_scan_result result={.elements_length=1024};
 assert(request(false,11,&result,sizeof(result),NULL)==0);
 assert(result.ssid_length==13 && !memcmp(result.ssid,"Fuxi Loopback",13));
 assert(result.rssi==-42 && result.channel.channel==1 && result.elements_length==4);
 assert(result.noise_known==1 && result.noise==-95 && result.snr==53);
 assert(request(false,11,&result,sizeof(result),NULL)==EIO);
 struct apple80211_channels channels;
 assert(!request(false,27,&channels,sizeof(channels),NULL) && channels.count==1);
 assert(request(false,27,NULL,sizeof(channels),NULL)==EFAULT);
 struct apple80211_assoc join={.version=1,.ssid_length=13}; memcpy(join.ssid,"Fuxi Loopback",13);
 assert(request(true,20,&join,sizeof(join)-1,NULL)==EINVAL);
 join.ssid_length=33; assert(request(true,20,&join,sizeof(join),NULL)==EINVAL); join.ssid_length=13;
 join.key_length=65; join.key_cipher=10; assert(request(true,20,&join,sizeof(join),NULL)==EINVAL);
 join.key_length=0; assert(!request(true,20,&join,sizeof(join),NULL)); settle();
 int val; assert(!request(false,21,NULL,0,&val) && val==1); assert(link_up);
 /* joined: the strength is said at once and every 5 s after; the noise is asked for */
 assert(atomic_load(&events[39])==1 && timer_armed==1 && timer_fn);
 timer_fn(NULL,NULL); assert(atomic_load(&events[39])==2 && timer_armed==2);
 struct { uint32_t version,radios,unit; int32_t each[4],all,each_ext[4],all_ext; } noise;
 assert(!request(false,17,&noise,sizeof(noise),NULL) && noise.all==-95 && noise.each[0]==-95);
 uint8_t frame[1514]={0}; frame[12]=0x88;frame[13]=0xb5;
 assert(transmit(frame,60) && received==1);
 assert(!transmit(frame,1515)); assert(!transmit(frame,13));
 frame[13]=0x8e; assert(!transmit(frame,60));
 assert(!request(true,22,NULL,0,NULL));settle();assert(!link_up);
 /* left: the pending one finds no network, says nothing and stops */
 timer_fn(NULL,NULL); assert(atomic_load(&events[39])==2 && timer_armed==2);
 assert(request(false,17,&noise,sizeof(noise),NULL)==ENXIO);
 join.key_length=8;memcpy(join.key,"password",8);join.key_cipher=10;
 assert(!request(true,20,&join,sizeof(join),NULL));settle();
 assert(!request(false,21,NULL,0,&val) && val==2 && !link_up);
 struct { uint32_t version,radios,state[4]; } power={.version=1,.radios=1};
 assert(!request(true,19,&power,sizeof(power),NULL));settle();
 assert(!fuxi_wifi_sta_powered() && !link_up);
 assert(request(true,10,&scan,sizeof(scan),NULL)==ENETDOWN);
 power.state[0]=1;assert(!request(true,19,&power,sizeof(power),NULL));settle();
 assert(fuxi_wifi_sta_powered());
 /* fuxi,wifi-ux：讀一次就記住；只留已知位元；長度不對、沒有這個屬性都當 0 */
 assert(!fuxi_wifi_ux());
 adt_ux=0xffffffffu; assert(!fuxi_wifi_ux());
 set_ux(0xffffffffu,4); assert(fuxi_wifi_ux()==FUXI_WIFI_UX_ALL);
 set_ux(1,8); assert(!fuxi_wifi_ux());
 set_ux(6,4); assert(fuxi_wifi_ux()==(FUXI_WIFI_UX_PICK_RATE|FUXI_WIFI_UX_KEEP_LINK));
 set_ux(FUXI_WIFI_UX_SCAN_REUSE|FUXI_WIFI_UX_SCAN_DWELL_110,4);
 assert(fuxi_wifi_ux()==(FUXI_WIFI_UX_SCAN_REUSE|FUXI_WIFI_UX_SCAN_DWELL_110));
 assert(FUXI_WIFI_UX_ALL==8191u);
 /* 預設（bit 0 關）：關閉還在排隊時，GET POWER 照舊回報「開」，也照收掃描（舊行為：開關會彈回約兩秒） */
 set_ux(0,4); hold_station();
 power.state[0]=0; assert(!request(true,19,&power,sizeof(power),NULL));
 assert(fuxi_wifi_sta_powered() && said_power()==1);
 assert(!request(true,10,&scan,sizeof(scan),NULL));
 release_station(); assert(!fuxi_wifi_sta_powered() && said_power()==0);
 /* bit 0：還在排隊的要求立刻算數；正要關就不再收掃描或加入 */
 set_ux(1,4); hold_station();
 power.state[0]=1; assert(!request(true,19,&power,sizeof(power),NULL));
 assert(fuxi_wifi_sta_powered() && said_power()==1 && !radio_on());
 assert(!request(true,10,&scan,sizeof(scan),NULL));
 power.state[0]=0; assert(!request(true,19,&power,sizeof(power),NULL));
 assert(!fuxi_wifi_sta_powered() && said_power()==0);
 assert(request(true,10,&scan,sizeof(scan),NULL)==ENETDOWN);
 join.key_length=0; join.key_cipher=0;
 assert(request(true,20,&join,sizeof(join),NULL)==ENETDOWN);
 release_station(); assert(!fuxi_wifi_sta_powered() && said_power()==0);
 /* bit 0：已從佇列取出、正在切換的電源也算數；之後再要求的方向優先 */
 atomic_store(&link_hold,1);
 power.state[0]=1; assert(!request(true,19,&power,sizeof(power),NULL)); wait_link_held();
 assert(fuxi_wifi_sta_powered() && said_power()==1 && !radio_on());
 assert(!request(true,10,&scan,sizeof(scan),NULL));
 power.state[0]=0; assert(!request(true,19,&power,sizeof(power),NULL));
 assert(!fuxi_wifi_sta_powered() && said_power()==0);
 assert(request(true,10,&scan,sizeof(scan),NULL)==ENETDOWN);
 atomic_store(&link_hold,0); settle(); assert(!fuxi_wifi_sta_powered() && !radio_on());
 power.state[0]=1; assert(!request(true,19,&power,sizeof(power),NULL)); settle();
 assert(fuxi_wifi_sta_powered() && radio_on());
 /* 開著的時候正在關：bit 0 回報「關」，預設回報「開」（同一個狀態，只差這個開關） */
 atomic_store(&link_hold,1);
 power.state[0]=0; assert(!request(true,19,&power,sizeof(power),NULL)); wait_link_held();
 assert(!fuxi_wifi_sta_powered() && said_power()==0 && radio_on());
 assert(request(true,20,&join,sizeof(join),NULL)==ENETDOWN);
 set_ux(0,4); assert(fuxi_wifi_sta_powered() && said_power()==1);
 atomic_store(&link_hold,0); settle(); assert(!fuxi_wifi_sta_powered());
 power.state[0]=1; assert(!request(true,19,&power,sizeof(power),NULL)); settle();
 assert(fuxi_wifi_sta_powered());
 /* bit 4：要的電源和現在一樣（關被後到的開覆蓋）：不碰底層、不清連線 */
 struct fuxi_wifi_sta_state st;
 set_ux(FUXI_WIFI_UX_POWER_SAME,4);
 assert(!request(true,20,&join,sizeof(join),NULL)); settle(); assert(link_up);
 fuxi_wifi_sta_state(&st); assert(st.phase==FUXI_WIFI_STA_JOINED);
 power.state[0]=1; assert(!request(true,19,&power,sizeof(power),NULL)); settle();
 assert(link_up && fuxi_wifi_sta_powered());
 fuxi_wifi_sta_state(&st); assert(st.phase==FUXI_WIFI_STA_JOINED);
 /* 沒開 bit 4：同樣的要求照舊重開底層，連線被清掉 */
 set_ux(0,4);
 assert(!request(true,19,&power,sizeof(power),NULL)); settle();
 assert(!link_up && fuxi_wifi_sta_powered());
 fuxi_wifi_sta_state(&st); assert(st.phase==FUXI_WIFI_STA_IDLE);
 /* bit 4 下真的要關仍然關，再開也照常 */
 set_ux(FUXI_WIFI_UX_POWER_SAME,4);
 power.state[0]=0; assert(!request(true,19,&power,sizeof(power),NULL)); settle();
 assert(!fuxi_wifi_sta_powered() && !radio_on());
 power.state[0]=1; assert(!request(true,19,&power,sizeof(power),NULL)); settle();
 assert(fuxi_wifi_sta_powered() && radio_on());
 /* A later on request must wait for the queued off to reach the backend. */
 set_ux(FUXI_WIFI_UX_POWER_ASKED|FUXI_WIFI_UX_POWER_SAME|FUXI_WIFI_UX_POWER_OFF_FIRST,4);
 hold_station();
 unsigned power_events=atomic_load(&events[1]);
 power.state[0]=0; assert(!request(true,19,&power,sizeof(power),NULL));
 power.state[0]=1; assert(!request(true,19,&power,sizeof(power),NULL));
 assert(fuxi_wifi_sta_power_off_pending() && !said_power() && radio_on());
 assert(request(true,10,&scan,sizeof(scan),NULL)==ENETDOWN);
 release_station();
 assert(!fuxi_wifi_sta_power_off_pending() && said_power() && radio_on());
 assert(atomic_load(&events[1])==power_events+2);
 puts("PASS: queued Wi-Fi off is applied before a later on");
 /* bit 3：國家碼。沒開時 SET／GET 照舊拒絕 */
 uint8_t cc8[8]={1,0,0,0,'T','W',' ',0}, got[8], bad[8]={1,0,0,0,'t','w',0,0};
 set_ux(0,4);
 assert(request(true,51,cc8,8,NULL)==EOPNOTSUPP && request(false,51,got,8,NULL)==EOPNOTSUPP);
 set_ux(FUXI_WIFI_UX_COUNTRY,4);
 assert(request(false,51,got,8,NULL)==EOPNOTSUPP);            /* 還沒設過 */
 assert(request(true,51,cc8,2,NULL)==EINVAL && request(true,51,NULL,8,NULL)==EINVAL);
 assert(request(true,51,bad,8,NULL)==EINVAL);                  /* 小寫 */
 bad[4]='T'; bad[5]='W'; bad[6]=0x7f; assert(request(true,51,bad,8,NULL)==EINVAL);
 assert(request(false,51,got,8,NULL)==EOPNOTSUPP);            /* 不合法的不記 */
 fail_copy=1; assert(request(true,51,cc8,8,NULL)==EFAULT); fail_copy=0;
 assert(!request(true,51,cc8,8,NULL));                         /* version＋cc[3] */
 memset(got,0,8); assert(!request(false,51,got,8,NULL) && got[0]==1 && !memcmp(got+4,"TW ",3));
 memset(got,0,8); assert(!request(false,51,got,3,NULL) && !memcmp(got,"TW ",3) && !got[3]);
 assert(!request(true,51,(void *)"US",3,NULL));               /* 只有 cc[3] */
 memset(got,0,8); assert(!request(false,51,got,8,NULL) && got[0]==1 && !memcmp(got+4,"US",3));
 assert(!request(true,51,(void *)"US",3,NULL));               /* 同一個碼再設一次 */
 assert(request(false,51,NULL,8,NULL)==EFAULT);
 puts("PASS: production station, ioctl layouts/events, fake frame callbacks and gates");
 puts("PASS: fuxi,wifi-ux bit 0 power report follows the queued and running request");
 puts("PASS: fuxi,wifi-ux bit 3 country code kept, bit 4 same power keeps the link");
 /* SoftAP (ap1) opt-in lifecycle, layouts, hostapd key/STA/IE contract and isolation */
 {
  uint8_t caps[13]={0};
  assert(!request(false,12,caps,sizeof(caps),NULL) && !(caps[0]&0x80)); /* disabled by default */
  struct apple80211req apreq = { .req_if_name="ap1", .req_type=96 };
  assert(fuxi_wifi_apple80211_ioctl(SIOCGA80211,&apreq)==ENXIO);
  adt_ap=FUXI_WIFI_AP_ALL; wifi_ap_cached=0;
  fuxi_wifi_apple80211_ap_attach(&ap_mock_ops);
  assert(!request(false,12,caps,sizeof(caps),NULL) && (caps[0]&0x80)); /* HOSTAP bit 7 advertised */
  assert(fuxi_wifi_apple80211_ioctl(SIOCGA80211,&apreq)==0 && apreq.req_val==7);
  char parent[16]={0};
  apreq = (struct apple80211req){ .req_if_name="ap1", .req_type=97, .req_len=16, .req_data=(uintptr_t)parent };
  assert(fuxi_wifi_apple80211_ioctl(SIOCGA80211,&apreq)==0 && !strcmp(parent,"en0"));
  unsigned en0_mode_ev = atomic_load(&events[8]);
  unsigned en0_apstart_ev = atomic_load(&events[30]);
  unsigned en0_arrive_ev = atomic_load(&events[12]);
  unsigned en0_leave_ev = atomic_load(&events[13]);
  int en0_opmode = 0;
  assert(!request(false,15,NULL,0,&en0_opmode) && en0_opmode==1);
  struct apple80211_station_list sl; memset(&sl,0,sizeof(sl));
  assert(request(false,42,&sl,sizeof(sl),NULL)==EOPNOTSUPP); /* en0 STATION_LIST refused when AP inactive */
  struct apple80211_apmode_data apd; memset(&apd,0,sizeof(apd));
  apd.version=1; apd.mode=2; apd.auth_lower=1; apd.auth_upper=8; apd.ssid_len=7; memcpy(apd.ssid,"FuxiAP1",7);
  apreq = (struct apple80211req){ .req_if_name="en0", .req_type=25, .req_val=1, .req_len=sizeof(apd), .req_data=(uintptr_t)&apd };
  assert(fuxi_wifi_apple80211_ioctl(SIOCSA80211,&apreq)==0 && ap_mock_starts==1 && ap_link_state);
  assert(ap_mock_flags==(FUXI_WIFI_AP_CFG_HE|FUXI_WIFI_AP_CFG_PREFER_5GHZ) && ap_mock_cc[0]=='U' && ap_mock_cc[1]=='S');
  /* Start posts EVENT_HOST_AP_MODE_START (30) on en0 for wifid -> misd, and MODE_CHANGED (8) on ap1 for hostapd. */
  assert(atomic_load(&events[30])==en0_apstart_ev+1 && atomic_load(&events[8])==en0_mode_ev && atomic_load(&ap_events[8])==1);
  assert(!request(false,15,NULL,0,&en0_opmode) && en0_opmode==(1|8));
  struct { uint32_t v; struct apple80211_channel c; } apch = {0};
  apreq = (struct apple80211req){ .req_if_name="ap1", .req_type=4, .req_len=sizeof(apch), .req_data=(uintptr_t)&apch };
  assert(fuxi_wifi_apple80211_ioctl(SIOCGA80211,&apreq)==0 && apch.c.channel==149 && (apch.c.flags & (CHANNEL_5GHZ|CHANNEL_80MHZ|CHANNEL_ACTIVE))==(CHANNEL_5GHZ|CHANNEL_80MHZ|CHANNEL_ACTIVE));
  apreq = (struct apple80211req){ .req_if_name="ap1", .req_type=15 };
  assert(fuxi_wifi_apple80211_ioctl(SIOCGA80211,&apreq)==0 && apreq.req_val==8);
  const uint8_t sta_mac[6]={0xaa,0xbb,0xcc,0xdd,0xee,0x01};
  const uint8_t rsn_ie[22]={0x30,20,1,0,0,0x0f,0xac,4,1,0,0,0x0f,0xac,4,1,0,0,0x0f,0xac,2,0,0};
  fuxi_wifi_apple80211_ap_sta_arrive(sta_mac,rsn_ie,sizeof(rsn_ie));
  assert(atomic_load(&ap_events[12])==1 && atomic_load(&events[12])==en0_arrive_ev); /* WPA2: en0 arrive waits for STA_AUTHORIZE */
  struct apple80211_sta_ie_list iel; memset(&iel,0,sizeof(iel));
  iel.version=1; memcpy(iel.mac,sta_mac,6); iel.ie_len=1024;
  apreq = (struct apple80211req){ .req_if_name="ap1", .req_type=73, .req_len=sizeof(iel), .req_data=(uintptr_t)&iel };
  assert(fuxi_wifi_apple80211_ioctl(SIOCGA80211,&apreq)==0 && iel.ie_len==22 && iel.ie[0]==0x30 && iel.ie[1]==20);
  struct apple80211_key k; memset(&k,0,sizeof(k));
  k.version=1; k.key_len=16; k.key_cipher_type=5; k.key_index=1; memset(k.key_ea,0xff,6);
  apreq = (struct apple80211req){ .req_if_name="ap1", .req_type=3, .req_len=sizeof(k), .req_data=(uintptr_t)&k };
  assert(fuxi_wifi_apple80211_ioctl(SIOCSA80211,&apreq)==0 && ap_mock_keys==1 && ap_mock_cipher==0x000fac04u);
  apreq = (struct apple80211req){ .req_if_name="ap1", .req_type=78, .req_len=sizeof(k), .req_data=(uintptr_t)&k };
  assert(fuxi_wifi_apple80211_ioctl(SIOCGA80211,&apreq)==0 && k.key_rsc[0]==0x11 && k.key_rsc[1]==0x22);
  struct apple80211_sta_authorize sa = { .version=1, .authorize=1 }; memcpy(sa.mac,sta_mac,6);
  apreq = (struct apple80211req){ .req_if_name="ap1", .req_type=74, .req_len=sizeof(sa), .req_data=(uintptr_t)&sa };
  assert(fuxi_wifi_apple80211_ioctl(SIOCSA80211,&apreq)==0 && ap_mock_auths==1);
  assert(atomic_load(&events[12])==en0_arrive_ev+1 && en0_last_sta_num==1); /* en0 STA_ARRIVE with staNum=1 */
  /* Early STA before bridge ready: bridge_ready(true) arms 350ms tick -> disassoc early STA to trigger fresh DHCP/bootpd */
  unsigned prev_rep = ap_replay_armed, prev_deauth = ap_mock_deauths;
  unsigned prev_ap_leave = atomic_load(&ap_events[13]), prev_en0_leave = atomic_load(&events[13]);
  fuxi_wifi_apple80211_ap_bridge_ready(true);
  assert(ap_replay_armed == prev_rep + 1 && timer_slot_count >= 1);
  timer_slots[0](NULL, NULL);
  assert(ap_mock_deauths == prev_deauth + 1);
  assert(atomic_load(&ap_events[13]) == prev_ap_leave + 1);
  assert(atomic_load(&events[13]) == prev_en0_leave + 1 && en0_last_sta_num == 0);
  /* Client reconnects AFTER bridge is ready -> joined_before_bridge==false, must NOT be disassoc'd again */
  fuxi_wifi_apple80211_ap_sta_arrive(sta_mac,rsn_ie,sizeof(rsn_ie));
  apreq = (struct apple80211req){ .req_if_name="ap1", .req_type=74, .req_len=sizeof(sa), .req_data=(uintptr_t)&sa };
  assert(fuxi_wifi_apple80211_ioctl(SIOCSA80211,&apreq)==0 && ap_mock_auths==2);
  assert(atomic_load(&events[12])==en0_arrive_ev+2 && en0_last_sta_num==1);
  fuxi_wifi_apple80211_ap_bridge_ready(true);
  timer_slots[0](NULL, NULL);
  assert(ap_mock_deauths == prev_deauth + 1); /* unchanged: post-bridge STA not kicked */
  memset(&sl,0,sizeof(sl));
  apreq = (struct apple80211req){ .req_if_name="ap1", .req_type=42, .req_len=sizeof(sl), .req_data=(uintptr_t)&sl };
  assert(fuxi_wifi_apple80211_ioctl(SIOCGA80211,&apreq)==0 && sl.count==1 && !memcmp(sl.stations[0].mac,sta_mac,6) && sl.stations[0].flags==1);
  memset(&sl,0,sizeof(sl));
  assert(request(false,42,&sl,sizeof(sl),NULL)==0 && sl.count==1 && !memcmp(sl.stations[0].mac,sta_mac,6) && sl.stations[0].flags==1); /* en0 STATION_LIST */
  struct apple80211_sta_deauth sd = { .version=1, .reason=3 }; memcpy(sd.mac,sta_mac,6);
  apreq = (struct apple80211req){ .req_if_name="ap1", .req_type=76, .req_len=sizeof(sd), .req_data=(uintptr_t)&sd };
  assert(fuxi_wifi_apple80211_ioctl(SIOCSA80211,&apreq)==0 && ap_mock_deauths==prev_deauth+2);
  assert(atomic_load(&events[13])==en0_leave_ev+2 && en0_last_sta_num==0); /* en0 STA_LEAVE with staNum=0 */
  /* Also verify SoftAP stop posts en0 STA_LEAVE if an authorized STA is still present */
  fuxi_wifi_apple80211_ap_sta_arrive(sta_mac,rsn_ie,sizeof(rsn_ie));
  sa.authorize=1;
  apreq = (struct apple80211req){ .req_if_name="ap1", .req_type=74, .req_len=sizeof(sa), .req_data=(uintptr_t)&sa };
  assert(fuxi_wifi_apple80211_ioctl(SIOCSA80211,&apreq)==0);
  unsigned leave_before_stop = atomic_load(&events[13]);
  apreq = (struct apple80211req){ .req_if_name="en0", .req_type=25, .req_val=2, .req_len=0 };
  assert(fuxi_wifi_apple80211_ioctl(SIOCSA80211,&apreq)==0 && ap_mock_stops==1 && !ap_link_state);
  assert(atomic_load(&events[13])==leave_before_stop+1 && en0_last_sta_num==0);
  assert(atomic_load(&events[8])==en0_mode_ev+1); /* Stop posts EVENT_MODE_CHANGED (8) on en0 */
  assert(!request(false,15,NULL,0,&en0_opmode) && en0_opmode==1);
  assert(request(false,42,&sl,sizeof(sl),NULL)==EOPNOTSUPP);
  /* Verify SoftAP restart waits for slow previous hostapd teardown (RSN_CONF -> stop -> delayed broadcast STA_DEAUTH) */
  apreq = (struct apple80211req){ .req_if_name="en0", .req_type=25, .req_val=1, .req_len=sizeof(apd), .req_data=(uintptr_t)&apd };
  assert(fuxi_wifi_apple80211_ioctl(SIOCSA80211,&apreq)==0 && ap_mock_starts==2);
  uint8_t rsn_conf[164] = {0};
  apreq = (struct apple80211req){ .req_if_name="ap1", .req_type=77, .req_len=sizeof(rsn_conf), .req_data=(uintptr_t)rsn_conf };
  assert(fuxi_wifi_apple80211_ioctl(SIOCSA80211,&apreq)==0);
  apreq = (struct apple80211req){ .req_if_name="en0", .req_type=25, .req_val=2, .req_len=0 };
  assert(fuxi_wifi_apple80211_ioctl(SIOCSA80211,&apreq)==0 && ap_mock_stops==2);
  atomic_store(&hostapd_exit_deauth_sent, 0);
  pthread_t htid;
  assert(pthread_create(&htid, NULL, delayed_hostapd_exit_thread, NULL) == 0);
  uint64_t t_before = mach_absolute_time();
  apreq = (struct apple80211req){ .req_if_name="en0", .req_type=25, .req_val=1, .req_len=sizeof(apd), .req_data=(uintptr_t)&apd };
  assert(fuxi_wifi_apple80211_ioctl(SIOCSA80211,&apreq)==0 && ap_mock_starts==3);
  uint64_t t_after = mach_absolute_time();
  assert(pthread_join(htid, NULL) == 0);
  assert(atomic_load(&hostapd_exit_deauth_sent) == 1);
  assert((t_after - t_before) >= 120000000ull);
  apreq = (struct apple80211req){ .req_if_name="en0", .req_type=25, .req_val=2, .req_len=0 };
  assert(fuxi_wifi_apple80211_ioctl(SIOCSA80211,&apreq)==0 && ap_mock_stops==3);
  puts("PASS: SoftAP ap1 opt-in gate, Wi-Fi 6/5G/CC flags, HOST_AP_MODE, wifid en0 events (30/8/12/13), hostapd exit sync & STATION_LIST");
 }
 return 0;
}
'''


def stripped(name):
    return re.sub(r'^#include[^\n]*\n', '', (SRC / name).read_text(), flags=re.M)


def compile_run(source, directory, name):
    path = directory / (name + '.c')
    path.write_text(source)
    binary = directory / name
    subprocess.run([os.environ.get('CC', 'clang'), '-std=gnu11', '-g', '-O1', '-Wall', '-Wextra',
                    '-Wno-unused-function', '-Wno-unused-parameter', '-fsanitize=address,undefined',
                    '-pthread', '-I', str(SRC), str(path), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=30)
    if name == 'ios':
        subprocess.run([str(binary), 'native'], check=True, timeout=30)


def wpa_test(directory):
    # Independent IEEE PRF/HMAC/AES-wrap peer. No reference checkout code is run.
    pmk = hashlib.pbkdf2_hmac('sha1', b'password', b'IEEE', 4096, 32)
    own, peer = bytes.fromhex('024655584901'), bytes.fromhex('024655584902')
    snonce, anonce = bytes(range(32)), bytes(range(32, 64))
    data = min(own, peer) + max(own, peer) + min(snonce, anonce) + max(snonce, anonce)
    ptk = b''.join(hmac.digest(pmk, b'Pairwise key expansion\0' + data + bytes([i]), 'sha1')
                   for i in range(3))[:48]
    gtk = bytes(range(16))
    wrapped = aes_key_wrap(ptk[16:32], b'\xdd\x16\x00\x0f\xac\x01\x01\x00' + gtk)

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

    vectors = ''.join(array(n, v) for n, v in [('pmk', pmk), ('own', own), ('peer', peer),
        ('snonce', snonce), ('tk', ptk[32:]), ('gtk', gtk), ('m1', message(0x8a, 1)),
        ('m3', message(0x13ca, 2, wrapped, True)), ('g1', message(0x1382, 3, wrapped, True))])
    source = '#include <assert.h>\n#include <stdio.h>\n' + (SRC / 'fuxi_wifi_wpa.c').read_text() + vectors + r'''
int main(void) {
 unsigned char key[32],reply[256],bad[sizeof(m3)];unsigned n;const char *why;
 fuxi_wifi_wpa_pmk((const unsigned char *)"password",8,(const unsigned char *)"IEEE",4,key);
 assert(!memcmp(key,pmk,32));
 struct fuxi_wifi_wpa w={0};memcpy(w.pmk,pmk,32);memcpy(w.own,own,6);memcpy(w.peer,peer,6);memcpy(w.own_nonce,snonce,32);
 assert(fuxi_wifi_wpa_message(&w,m3,sizeof(m3),reply,&n,&why)==-1);
 assert(fuxi_wifi_wpa_message(&w,m1,sizeof(m1),reply,&n,&why)==1 && n==99);
 assert(!memcmp(w.pairwise,tk,16));
 memcpy(bad,m3,sizeof(bad));bad[81]^=1;
 assert(fuxi_wifi_wpa_message(&w,bad,sizeof(bad),reply,&n,&why)==-1);
 assert(fuxi_wifi_wpa_message(&w,m3,sizeof(m3),reply,&n,&why)==2 && n==99);
 assert(w.group_length==16 && !memcmp(w.group,gtk,16));
 assert(fuxi_wifi_wpa_message(&w,m3,sizeof(m3),reply,&n,&why)==-1);
 assert(fuxi_wifi_wpa_message(&w,m1,sizeof(m1),reply,&n,&why)==-1);
 assert(fuxi_wifi_wpa_message(&w,g1,sizeof(g1),reply,&n,&why)==3);
 w.rsn_length=256; assert(fuxi_wifi_wpa_message(&w,m1,sizeof(m1),reply,&n,&why)==-1);
 for(unsigned i=0;i<sizeof(m3);i++) {
  struct fuxi_wifi_wpa fresh={0};
  assert(fuxi_wifi_wpa_message(&fresh,m3,i,reply,&n,&why)<=0);
 }
 puts("PASS: WPA2 PBKDF2, independent M1/M3/group peer, MIC, replay and lengths");
}
'''
    compile_run(source, directory, 'wpa')



def net_test(directory):
    # Production TX/RX with a modeled mbuf KPI. In particular, mbuf_adj must
    # reduce packet length exactly once and keep the Ethernet header available.
    code = stripped('fuxi_wifi_net.c')
    code = code[code.index('static LCK_GRP_DECLARE'):code.index('static unsigned net_ioctls_said;')]
    prelude = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#define KERNEL 1
#define LCK_GRP_DECLARE(n,s) int n __attribute__((unused))
#define LCK_MTX_DECLARE(n,g) pthread_mutex_t n = PTHREAD_MUTEX_INITIALIZER
#define lck_mtx_lock pthread_mutex_lock
#define lck_mtx_unlock pthread_mutex_unlock
#define ETHER_HDR_LEN 14
#define DLT_EN10MB 1
#define MBUF_DONTWAIT 0
typedef int errno_t;
typedef void *ifnet_t;
typedef struct packet {
 uint8_t bytes[1516]; size_t offset,len,total;void *header;struct packet *nextpkt;
} *mbuf_t;
struct ifnet_stat_increment_param { unsigned packets_in,bytes_in; };
struct ifnet_stats_param { uint64_t bytes_in,bytes_out; };
static unsigned freed,inputs,input_calls,out_packets,out_errors;
static struct ifnet_stats_param interface_stats;
static bool fail_alloc,fail_copy,accept;
static size_t expected_length,expected_padding;
static size_t mbuf_pkthdr_len(mbuf_t m) { return m->total; }
static void mbuf_freem(mbuf_t m) { freed++;free(m); }
static void ifnet_stat_increment_out(ifnet_t p,unsigned n,unsigned bytes,unsigned errors) {
 assert(p);interface_stats.bytes_out+=bytes;out_packets+=n;out_errors+=errors;
}
static int ifnet_stat(ifnet_t p,struct ifnet_stats_param *s) { assert(p);*s=interface_stats;return 0; }
static int mbuf_allocpacket(int flags,size_t n,unsigned *chunks,mbuf_t *out) {
 (void)flags;(void)chunks;if(fail_alloc)return 1;
 *out=calloc(1,sizeof(**out));(*out)->len=(*out)->total=n;return 0;
}
static int mbuf_copyback(mbuf_t m,size_t off,size_t n,const void *p,int flags) {
 (void)flags;if(fail_copy)return 1;assert(off+n<=sizeof(m->bytes));memcpy(m->bytes+off,p,n);return 0;
}
static void *mbuf_data(mbuf_t m) { return m->bytes+m->offset; }
static int mbuf_setdata(mbuf_t m,void *data,size_t len) {
 size_t offset=(uint8_t *)data-m->bytes;
 if (offset>sizeof(m->bytes) || len>sizeof(m->bytes)-offset) return 1;
 m->offset=offset;m->len=len;return 0;
}
static void mbuf_pkthdr_setlen(mbuf_t m,size_t len) { m->total=len; }
static void mbuf_setnextpkt(mbuf_t m,mbuf_t next) { m->nextpkt=next; }
static int mbuf_freem_list(mbuf_t m) {
 int count=0;
 while(m) { mbuf_t next=m->nextpkt;mbuf_freem(m);m=next;count++; }
 return count;
}
static void mbuf_pkthdr_setrcvif(mbuf_t m,ifnet_t p) { (void)m;assert(p); }
static void mbuf_pkthdr_setheader(mbuf_t m,void *p) { m->header=p; }
static void mbuf_adj(mbuf_t m,int n) { m->offset+=n;m->len-=n;m->total-=n; }
static void bpf_tap_in(ifnet_t p,int type,mbuf_t m,void *header,int n) {
 assert(p && type==1 && n==14 && header==m->bytes+expected_padding && m->header==header);
 assert(m->offset==expected_padding+14 && m->total==expected_length-14 && m->len==m->total);
}
static void bpf_tap_out(ifnet_t p,int type,mbuf_t m,void *h,int n) {
 assert(p && type==1 && !h && !n && m->offset==0);
}
static int ifnet_input(ifnet_t p,mbuf_t m,struct ifnet_stat_increment_param *stats) {
 assert(p && stats->packets_in && stats->bytes_in==stats->packets_in*expected_length);
 for (mbuf_t packet=m;packet;packet=packet->nextpkt) {
  assert(packet->bytes[12+expected_padding]==0x88 && packet->bytes[13+expected_padding]==0xb5);
 }
 input_calls++;interface_stats.bytes_in+=stats->bytes_in;
 assert((unsigned)mbuf_freem_list(m)==stats->packets_in);inputs+=stats->packets_in;return 0;
}
'''
    test = r'''
static bool send_frame(mbuf_t m,size_t n) { assert(m->total==n);return accept; }
static int read_frame(void *source,size_t offset,void *dest,size_t n) {
 assert(!offset && n==expected_length);
 if (fail_copy) return 1;
 memcpy(dest,source,n);return 0;
}
int main(void) {
 static const struct fuxi_wifi_net_link driver={.transmit=send_frame};
 net_link=&driver;net_ifp=(void *)1;
 uint8_t frame[1514]={0};frame[12]=0x88;frame[13]=0xb5;
 fuxi_wifi_net_input(frame,60);assert(inputs==0);
 net_link_up=true;
 fuxi_wifi_net_input(frame,13);fuxi_wifi_net_input(frame,1515);assert(inputs==0);
 fail_alloc=true;fuxi_wifi_net_input(frame,60);assert(inputs==0);fail_alloc=false;
 fail_copy=true;fuxi_wifi_net_input(frame,60);assert(inputs==0 && freed==1);fail_copy=false;
 expected_length=60;fuxi_wifi_net_input(frame,60);assert(inputs==1);
 expected_length=1514;fuxi_wifi_net_input(frame,1514);assert(inputs==2);
 mbuf_t packet=calloc(1,sizeof(*packet));packet->len=packet->total=60;
 accept=true;assert(net_output(net_ifp,packet)==0 && out_packets==1);
 packet=calloc(1,sizeof(*packet));packet->len=packet->total=1515;
 assert(net_output(net_ifp,packet)==0 && out_errors==1);
 packet=calloc(1,sizeof(*packet));packet->len=packet->total=60;
 net_link_up=false;assert(net_output(net_ifp,packet)==0 && out_errors==2);
 assert(freed==6);
 expected_padding=2; expected_length=1514;
 fuxi_wifi_net_input_packet(frame,1514,read_frame); assert(!rx_head);
 net_link_up=true;
 fuxi_wifi_net_input_packet(frame,1515,read_frame); assert(!rx_head);
 fail_copy=true; fuxi_wifi_net_input_packet(frame,1514,read_frame);
 assert(!rx_head && freed==7); fail_copy=false;
 for (unsigned i=0;i<63;i++) fuxi_wifi_net_input_packet(frame,1514,read_frame);
 assert(input_calls==2 && rx_pending.packets_in==63);
 fuxi_wifi_net_input_packet(frame,1514,read_frame);
 assert(input_calls==3 && inputs==66 && !rx_head && !rx_tail);
 fuxi_wifi_net_input_packet(frame,1514,read_frame);
 fuxi_wifi_net_flush_rx(); fuxi_wifi_net_flush_rx();
 assert(input_calls==4 && inputs==67);
 fuxi_wifi_net_input_packet(frame,1514,read_frame);
 net_link_up=false; fuxi_wifi_net_flush_rx();
 assert(input_calls==4 && !rx_head && !rx_pending.bytes_in && freed==73);
 uint64_t rx=0,tx=0;
 assert(fuxi_wifi_net_byte_counts(&rx,&tx) && rx==60+66*1514 && tx==60);
 puts("PASS: aligned borrowed RX, bounded batches, error and link-down ownership");
 puts("PASS: production ifnet TX/RX bounds, mbuf ownership, header adjustment and failures");
}
'''
    compile_run(prelude + stripped('fuxi_wifi_net.h') + code + test, directory, 'net')

def root_test(directory):
    spec = importlib.util.spec_from_file_location('wifi_edit', ROOT / 'tools/enable-wifi-ios.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    root = directory / 'root'
    job = {'Label': 'com.apple.wifid', 'ProgramArguments': ['/usr/sbin/wifid'],
           '_LimitLoadToDeviceTree': ['wlan'], 'Disabled': True, 'Other': 42}
    cache = root / module.CACHE
    cache.parent.mkdir(parents=True)
    original = {'LaunchDaemons': {'com.apple.wifid': job, 'other': {'Program': '/usr/sbin/other'}}}
    cache.write_bytes(plistlib.dumps(original, fmt=plistlib.FMT_BINARY))
    standalone = root / module.JOB
    standalone.parent.mkdir(parents=True)
    standalone.write_bytes(plistlib.dumps(job))
    command = ['python3', str(ROOT / 'tools/enable-wifi-ios.py'), str(root)]
    before = cache.read_bytes()
    subprocess.run(command, check=True)
    assert cache.read_bytes() == before and not list(root.rglob('*backup*'))
    subprocess.run(command + ['--enable-wifid'], check=True)
    updated = plistlib.loads(cache.read_bytes())
    assert updated['LaunchDaemons']['other'] == original['LaunchDaemons']['other']
    enabled = updated['LaunchDaemons']['com.apple.wifid']
    assert enabled['Disabled'] is False and '_LimitLoadToDeviceTree' not in enabled
    assert enabled['Other'] == 42
    subprocess.run(command + ['--enable-wifid'], check=True)
    # Restoration changes only the saved keys; later unrelated edits survive.
    updated['LaunchDaemons']['com.apple.wifid']['Other'] = 43
    cache.write_bytes(plistlib.dumps(updated))
    subprocess.run(command + ['--restore-wifid'], check=True)
    restored = plistlib.loads(cache.read_bytes())['LaunchDaemons']['com.apple.wifid']
    assert restored['_LimitLoadToDeviceTree'] == ['wlan'] and restored['Disabled'] is True
    assert restored['Other'] == 43 and plistlib.loads(standalone.read_bytes()) == job
    # Validate all inputs before changes.
    standalone.write_bytes(b'bad plist')
    before = cache.read_bytes()
    assert subprocess.run(command + ['--enable-wifid'], capture_output=True).returncode != 0
    assert cache.read_bytes() == before
    standalone.unlink()
    standalone.symlink_to(cache)
    assert subprocess.run(command + ['--enable-wifid'], capture_output=True).returncode != 0
    assert cache.read_bytes() == before
    print('PASS: root dry-run, opt-in, repeat, key-only undo, invalid input and symlink refusal')


def main():
    subprocess.run(['python3', str(ROOT / 'tools/sync-fuxi-wifi-ios.py'), '--check'], check=True)
    subprocess.run([os.environ.get('CC', 'clang'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-fsyntax-only', str(ROOT / 'tools/fuxi-wifi-ios-test.c')], check=True)
    with tempfile.TemporaryDirectory(prefix='fuxi-wifi-test-') as tmp:
        directory = Path(tmp)
        source = MODEL + stripped('fuxi_wifi_sta.h') + stripped('fuxi_wifi_net.h')
        source += stripped('fuxi_wifi_ios.h') + stripped('fuxi_wifi_apple80211.h')
        source += 'bool fuxi_wifi_net_attach(const struct fuxi_wifi_net_link *);\n'
        source += 'bool fuxi_wifi_ap_net_attach(const struct fuxi_wifi_net_link *);\n'
        source += stripped('fuxi_wifi_sta.c') + stripped('fuxi_wifi_apple80211.c')
        source += stripped('fuxi_wifi_ios.c') + TEST
        compile_run(source, directory, 'ios')
        wpa_test(directory)
        net_test(directory)
        root_test(directory)


if __name__ == '__main__':
    main()
