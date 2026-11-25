#!/usr/bin/env python3
"""P-SYS host tests; no device, Apple binaries, firmware or network.

Compiles the production 0107 kernel header and SWEET policy functions, the
FuxiAudio HAL plug-in and the SpringBoard snapshot library natively against
small stand-in headers, and exercises mkadt-fuxi.py's /product options and
fuxi-sys-root.py on synthetic inputs.
"""
import argparse
import importlib.util
import os
from pathlib import Path
import plistlib
import re
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / 'tools'
sys.path.insert(0, str(TOOLS))
import adt  # noqa: E402
import mkadt  # noqa: E402

CC = ['clang', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined']


def function(text, name):
    match = re.search(r'(?:static )?(?:void|int)\n' + name + r'\([^;]+?\n\{', text)
    assert match, name
    depth, end = 1, match.end()
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[match.start():end]


def build_and_run(tmp, name, source, extra=()):
    path = Path(tmp) / (name + '.c')
    path.write_text(source)
    binary = Path(tmp) / name
    subprocess.run(CC + list(extra) + [str(path), '-o', str(binary)], check=True)
    out = subprocess.run([str(binary)], capture_output=True, text=True)
    assert out.returncode == 0, out.stdout + out.stderr
    return out.stdout + out.stderr


# ---- kernel: 0107 fuxi_sys.h + the SWEET Sandbox policy functions -------------------------

KERNEL_MODEL = r'''
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define KERN_SUCCESS 0
#define KERN_DENIED 53
typedef int kern_return_t;
typedef uintptr_t user_addr_t;
struct proc { int pid; };
typedef struct proc *proc_t;
typedef struct ucred { int uid; } *kauth_cred_t;
#define TUNABLE(t,n,k,v) t n = v
#define os_atomic_or_orig(p,v,m) __atomic_fetch_or((p),(v),__ATOMIC_RELAXED)
#define PRIV_WORK_INTERVAL 1009
#define PRIV_PROC_UUID_POLICY 1001
#define PRIV_GLOBAL_PROC_INFO 1002
#define PRIV_SYSTEM_OVERRIDE 1003
#define PRIV_HW_DEBUG_DATA 1004
#define PRIV_SELECTIVE_FORCED_IDLE 1005
#define PRIV_DARKBOOT 1007
#define PRIV_AUDIO_LATENCY 1010
#define PRIV_KTRACE_BACKGROUND 1011
#define PRIV_SETPRIORITY_DARWIN_ROLE 1012
#define PRIV_PACKAGE_EXTENSIONS 1013
#define PRIV_TRIM_ACTIVE_FILE 1014
#define PRIV_PROC_CPUMON_OVERRIDE 1015
#define PRIV_VM_PRESSURE 6000
#define PRIV_VM_JETSAM 6001
#define PRIV_NET_PRIVILEGED_TRAFFIC_CLASS 10000
#define PRIV_NET_PRIVILEGED_SOCKET_DELEGATE 10001
#define PRIV_NET_INTERFACE_CONTROL 10002
#define PRIV_NET_PRIVILEGED_NETWORK_STATISTICS 10003
#define PRIV_NET_PRIVILEGED_NECP_POLICIES 10004
#define PRIV_NET_RESTRICTED_AWDL 10005
#define PRIV_NET_QOSMARKING_POLICY_OVERRIDE 10007
#define PRIV_NET_RESTRICTED_MULTIPATH_EXTENDED 10010
#define PRIV_NET_RESTRICTED_ROUTE_NC_READ 10011
#define PRIV_NET_VALIDATED_RESOLVER 10014
#define PRIV_NET_CUSTOM_PROTOCOL 10015
#define PRIV_NET_PRIVILEGED_NECP_DROP_ALL_BYPASS 10016
#define PRIV_NET_PRIVILEGED_IPSEC_WAKE_PACKET 10017
#define PRIV_NETINET_RESERVEDPORT 11000
#define PRIV_NETINET_TCP_KA_OFFLOAD 11001
#define PRIV_SKYWALK_REGISTER_USER_PIPE 12000
#define PRIV_SKYWALK_REGISTER_KERNEL_PIPE 12001
#define PRIV_SKYWALK_REGISTER_NET_IF 12002
#define PRIV_SKYWALK_REGISTER_FLOW_SWITCH 12003
#define PRIV_SKYWALK_OBSERVE_ALL 12010
#define PRIV_SKYWALK_OBSERVE_STATS 12011
#define PRIV_VFS_OPEN_BY_ID 14000
#define PRIV_VFS_MOVE_DATA_EXTENTS 14001
#define PRIV_VFS_SNAPSHOT 14002
#define PRIV_VFS_DATALESS_RESOLVER 14004
#define PRIV_VFS_SETSIZE 14006
static struct ucred self_cred, other_cred;
static struct proc self_proc = { 77 };
static const char *carried[8];
static char last_log[256];
static unsigned logs, queries;
static int container_error;
static unsigned container_calls;
static uint32_t fuxi_amfi_enabled = 1;
static int model_printf(const char *fmt, ...) {
 va_list ap; va_start(ap, fmt); vsnprintf(last_log, sizeof(last_log), fmt, ap); va_end(ap);
 logs++; return 0;
}
#define printf model_printf
static kauth_cred_t kauth_cred_get(void) { return &self_cred; }
static proc_t current_proc(void) { return &self_proc; }
static void proc_selfname(char *b, int n) { snprintf(b, n, "dasd"); }
static void proc_name(int pid, char *b, int n) { (void)pid; snprintf(b, n, "pkd"); }
static int proc_pid(proc_t p) { return p->pid; }
static kern_return_t sweet_amfi_query_boolean_with_proc(const proc_t proc, const char *name) {
 assert(proc == &self_proc); queries++;
 for (unsigned i = 0; i < 8 && carried[i]; i++) if (!strcmp(carried[i], name)) return KERN_SUCCESS;
 return KERN_DENIED;
}
static int fuxi_sandbox_container(user_addr_t arg) { (void)arg; container_calls++; return container_error; }
'''

KERNEL_TEST = r'''
#define SWEET_PRIV_LOG_MAX 64
static struct { int priv; char name[20]; } sweet_priv_logged[SWEET_PRIV_LOG_MAX];
static int sweet_priv_logged_count;
PRIV_FUNCTION
SANDBOX_FUNCTION
int main(void) {
 /* every pair is distinct and fits the log bitmap */
 assert(FUXI_SYS_PRIVS == 40);
 for (unsigned i = 0; i < FUXI_SYS_PRIVS; i++) {
  assert(fuxi_sys_priv_index(fuxi_sys_priv_entitlements[i].priv) == (int)i);
  for (unsigned j = i + 1; j < FUXI_SYS_PRIVS; j++) {
   assert(fuxi_sys_priv_entitlements[i].priv != fuxi_sys_priv_entitlements[j].priv);
   assert(strcmp(fuxi_sys_priv_entitlements[i].entitlement, fuxi_sys_priv_entitlements[j].entitlement));
  }
 }
 assert(fuxi_sys_priv_index(PRIV_WORK_INTERVAL) == -1);
 carried[0] = "vm-pressure-level";
 carried[1] = "com.apple.private.kernel.global-proc-info";
 /* default (fuxiprivs=0): exactly the previous behaviour, no entitlement is even looked at */
 assert(sweet_sandbox_priv_grant(&self_cred, PRIV_WORK_INTERVAL) == 0);
 assert(sweet_sandbox_priv_grant(&self_cred, PRIV_VM_PRESSURE) == EPERM);
 assert(queries == 0 && strstr(last_log, "dasd privilege 6000 denied"));
 assert(sweet_sandbox_priv_grant(&self_cred, PRIV_VM_PRESSURE) == EPERM && logs == 1);
 fuxi_sys_report();
 assert(logs == 1);                              /* nothing said while both are off */
 /* fuxiprivs=1 */
 fuxi_sys_privs = 1;
 fuxi_sys_report();
 assert(logs == 2 && strstr(last_log, "privileges by entitlement on (40 pairs)"));
 assert(sweet_sandbox_priv_grant(&self_cred, PRIV_VM_PRESSURE) == 0);
 assert(logs == 3 && strstr(last_log, "FUXI SYS: dasd privilege 6000 granted by vm-pressure-level"));
 assert(sweet_sandbox_priv_grant(&self_cred, PRIV_VM_PRESSURE) == 0 && logs == 3);   /* logged once */
 assert(sweet_sandbox_priv_grant(&self_cred, PRIV_GLOBAL_PROC_INFO) == 0 && logs == 4);
 /* entitlement missing: refused, and the old one-line denial log is kept */
 assert(sweet_sandbox_priv_grant(&self_cred, PRIV_DARKBOOT) == EPERM);
 assert(logs == 5 && strstr(last_log, "dasd privilege 1007 denied"));
 /* unknown privilege: no query */
 unsigned before = queries;
 assert(sweet_sandbox_priv_grant(&self_cred, 424242) == EPERM && queries == before);
 /* another credential: never answered from the current process's entitlements */
 assert(sweet_sandbox_priv_grant(&other_cred, PRIV_VM_PRESSURE) == EPERM && queries == before);
 assert(sweet_sandbox_priv_grant(&self_cred, PRIV_WORK_INTERVAL) == 0 && queries == before);
 /* Sandbox request 4: H1 result unless fuxisbpath=1 turns "no container" into success */
 container_error = ENOENT;
 assert(sweet_sandbox_syscall(&self_proc, 4, 0) == ENOENT);
 fuxi_sys_sbpath = 1;
 assert(sweet_sandbox_syscall(&self_proc, 4, 0) == 0);
 container_error = ESRCH;
 assert(sweet_sandbox_syscall(&self_proc, 4, 0) == ESRCH);
 container_error = ENAMETOOLONG;
 assert(sweet_sandbox_syscall(&self_proc, 4, 0) == ENAMETOOLONG);
 container_error = 0;
 assert(sweet_sandbox_syscall(&self_proc, 4, 0) == 0);
 /* fuxiamfi=0 keeps the unconditional ENOENT, without asking H1 */
 unsigned calls = container_calls;
 fuxi_amfi_enabled = 0;
 assert(sweet_sandbox_syscall(&self_proc, 4, 0) == ENOENT && container_calls == calls);
 fuxi_amfi_enabled = 1;
 assert(sweet_sandbox_syscall(&self_proc, 5, 0) == ENOENT && sweet_sandbox_syscall(&self_proc, 1, 0) == 0);
 fuxi_sys_privs = 0;
 fuxi_sys_report();
 assert(strstr(last_log, "privileges by entitlement off") && strstr(last_log, "succeeds untouched"));
 puts("kernel model ok");
 return 0;
}
'''


def test_kernel(tmp):
    subprocess.run([sys.executable, str(TOOLS / 'sync-fuxi-sys.py'), '--check'], check=True)
    sys_dir = ROOT / 'xnu/fuxi-sys'
    amfi = (sys_dir / 'sweet_amfi.c').read_text()
    assert '#include "fuxi_sys.h"' in amfi and amfi.index('#include "fuxi_amfi.h"') < amfi.index('#include "fuxi_sys.h"')
    test = KERNEL_TEST.replace('PRIV_FUNCTION', function(amfi, 'sweet_sandbox_priv_grant')) \
        .replace('SANDBOX_FUNCTION', 'static uint64_t sweet_sandbox_seen;\n' + function(amfi, 'sweet_sandbox_syscall'))
    out = build_and_run(tmp, 'kernel', KERNEL_MODEL + (sys_dir / 'fuxi_sys.h').read_text() + test)
    assert 'kernel model ok' in out
    # The default boot is unchanged: both switches default to 0.
    header = (sys_dir / 'fuxi_sys.h').read_text()
    assert 'TUNABLE(uint32_t, fuxi_sys_privs, "fuxiprivs", 0)' in header
    assert 'TUNABLE(uint32_t, fuxi_sys_sbpath, "fuxisbpath", 0)' in header
    assert 'Ported from HackiOS hackios-xnu-kernel' in header and 'APSL' in header
    print('PASS: 0107 privilege-by-entitlement and container-path switches (model + patch sync)')


# ---- FuxiAudio.driver -------------------------------------------------------------------

CF_STUB = r'''
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define __unused __attribute__((unused))
typedef uint8_t Boolean;
typedef uint32_t UInt32;
typedef int32_t SInt32;
typedef uint64_t UInt64;
typedef float Float32;
typedef double Float64;
typedef int32_t OSStatus;
typedef int32_t HRESULT;
typedef uint32_t ULONG;
typedef void *LPVOID;
#define S_OK 0
#define E_NOINTERFACE ((HRESULT)0x80000004)
struct __CFString { int tag; const char *s; };
typedef const struct __CFString *CFStringRef;
#define CFSTR(x) (&(const struct __CFString){ 1, x })
typedef struct { uint8_t b[16]; } CFUUIDBytes;
typedef CFUUIDBytes REFIID;
struct __CFUUID { int tag; CFUUIDBytes bytes; };
typedef const struct __CFUUID *CFUUIDRef;
typedef const void *CFTypeRef, *CFDictionaryRef, *CFAllocatorRef, *CFPropertyListRef;
#define kCFStringEncodingUTF8 0x08000100
static inline const char *CFStringGetCStringPtr(CFStringRef s, int e) { (void)e; return s->s; }
static inline Boolean CFEqual(const void *a, const void *b) {
 const int *ta = a, *tb = b;
 if (*ta != *tb) return 0;
 if (*ta == 1) return !strcmp(((CFStringRef)a)->s, ((CFStringRef)b)->s);
 return !memcmp(&((CFUUIDRef)a)->bytes, &((CFUUIDRef)b)->bytes, 16);
}
static inline CFTypeRef CFRetain(CFTypeRef v) { return v; }
/* tag 3＝CFData、4＝其他型別：模型 ADT 屬性，只數它們的 release。 */
struct __CFData { int tag; long length; const uint8_t *bytes; };
typedef const struct __CFData *CFDataRef;
typedef unsigned long CFTypeID;
typedef long CFIndex;
#define kCFAllocatorDefault ((CFAllocatorRef)0)
static unsigned cf_data_releases;
static inline void CFRelease(CFTypeRef v) { if (*(const int *)v >= 3) cf_data_releases++; }
static inline CFTypeID CFGetTypeID(CFTypeRef v) { return (CFTypeID)*(const int *)v; }
static inline CFTypeID CFDataGetTypeID(void) { return 3; }
static inline CFIndex CFDataGetLength(CFDataRef d) { return d->length; }
static inline const uint8_t *CFDataGetBytePtr(CFDataRef d) { return d->bytes; }
static inline CFUUIDRef CFUUIDCreateFromUUIDBytes(CFAllocatorRef a, CFUUIDBytes b) {
 (void)a; static struct __CFUUID pool[64]; static unsigned n; struct __CFUUID *u = &pool[n++ % 64];
 u->tag = 2; u->bytes = b; return u;
}
#define CFUUIDGetConstantUUIDWithBytes(a, ...) \
 CFUUIDCreateFromUUIDBytes(a, (CFUUIDBytes){ { __VA_ARGS__ } })
#define IUnknownUUID CFUUIDGetConstantUUIDWithBytes(NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46)
'''

AUDIO_TYPES_STUB = r'''
#pragma once
typedef struct { Float64 mMinimum, mMaximum; } AudioValueRange;
typedef struct {
 Float64 mSampleRate; UInt32 mFormatID, mFormatFlags, mBytesPerPacket, mFramesPerPacket, mBytesPerFrame,
 mChannelsPerFrame, mBitsPerChannel, mReserved;
} AudioStreamBasicDescription;
typedef struct { Float64 mSampleTime; UInt64 mHostTime; Float64 mRateScalar; UInt64 mWordClockTime; UInt32 mFlags, mReserved; } AudioTimeStamp;
enum { kAudioFormatLinearPCM = 'lpcm', kAudioFormatFlagIsSignedInteger = 1u << 2, kAudioFormatFlagIsPacked = 1u << 3 };
enum { kAudioTimeStampSampleTimeValid = 1u << 0, kAudioTimeStampHostTimeValid = 1u << 1 };
'''

MACH_STUB = r'''
#pragma once
#include <stdint.h>
typedef uint32_t mach_port_t;
typedef int kern_return_t;
#define MACH_PORT_NULL ((mach_port_t)0)
'''

MACH_TIME_STUB = r'''
#pragma once
#include <stdint.h>
typedef struct { uint32_t numer, denom; } mach_timebase_info_data_t;
extern uint64_t model_now;
static inline uint64_t mach_absolute_time(void) { return model_now; }
static inline int mach_timebase_info(mach_timebase_info_data_t *t) { t->numer = 125; t->denom = 3; return 0; }
'''

AUDIO_SYSCTL_STUB = r'''
#pragma once
#include <assert.h>
#include <pthread.h>
static unsigned actuator_calls;
static int actuator_value;
static bool refuse_rtp;
static bool refuse_pcm;
static unsigned model_haptic_pcm_calls;
static size_t model_haptic_pcm_bytes;
static uint8_t model_haptic_pcm_last[376];
static size_t model_haptic_pcm_last_bytes;
static const char *model_bootargs = "";
static bool model_bootargs_fail;
static bool model_speaker_live;
static int model_speaker_vol;
static unsigned model_speaker_pcm_calls;
static size_t model_speaker_pcm_bytes;
static int16_t model_speaker_pcm_last[2048 * 2];
static size_t model_speaker_pcm_last_bytes;
static bool model_mic_live;
static int model_mic_state = 1;
static int model_mic_wanted;
static unsigned model_mic_ctl_writes;
static int16_t model_mic_pcm[4096];
static size_t model_mic_avail;
static pthread_mutex_t *model_actuator_lock;
static unsigned model_lock_free_pcm, model_lock_free_vib;
static inline int sysctlbyname(const char *name, void *out, size_t *outlen, const void *in, size_t inlen) {
 if (!strcmp(name, "kern.bootargs")) {
  assert(out && outlen && !in);
  if (model_bootargs_fail) {
   errno = ENOENT;
   return -1;
  }
  size_t length = strlen(model_bootargs) + 1;
  assert(length <= *outlen);
  memcpy(out, model_bootargs, length);
  *outlen = length;
  return 0;
 }
 if (!strcmp(name, "kern.fuxiaudiovol")) {
  if (!model_speaker_live) {
   errno = ENOTSUP;
   return -1;
  }
  if (out && outlen && *outlen >= sizeof(int)) {
   memcpy(out, &model_speaker_vol, sizeof(int));
   *outlen = sizeof(int);
  }
  if (in && inlen == sizeof(int)) memcpy(&model_speaker_vol, in, sizeof(int));
  return 0;
 }
 if (!strcmp(name, "kern.fuxiaudiopcm")) {
  if (!model_speaker_live) {
   errno = ENOTSUP;
   return -1;
  }
  assert(!out && in && inlen >= 4 && (inlen & 3u) == 0);
  model_speaker_pcm_calls++;
  model_speaker_pcm_bytes += inlen;
  model_speaker_pcm_last_bytes = inlen <= sizeof(model_speaker_pcm_last) ? inlen : sizeof(model_speaker_pcm_last);
  memcpy(model_speaker_pcm_last, in, model_speaker_pcm_last_bytes);
  return 0;
 }
 if (!strcmp(name, "kern.fuxiaudiomicctl")) {
  if (!model_mic_live) {
   errno = ENOTSUP;
   return -1;
  }
  if (out && outlen && *outlen >= sizeof(int)) {
   memcpy(out, &model_mic_state, sizeof(int));
   *outlen = sizeof(int);
  }
  if (in && inlen == sizeof(int)) {
   memcpy(&model_mic_wanted, in, sizeof(int));
   model_mic_ctl_writes++;
  }
  return 0;
 }
 if (!strcmp(name, "kern.fuxiaudiomic")) {
  if (!model_mic_live) {
   errno = ENOTSUP;
   return -1;
  }
  assert(out && outlen && !in);
  size_t max_samples = *outlen / sizeof(int16_t);
  size_t take = model_mic_avail < max_samples ? model_mic_avail : max_samples;
  if (take) {
   memcpy(out, model_mic_pcm, take * sizeof(int16_t));
   model_mic_avail -= take;
  }
  *outlen = take * sizeof(int16_t);
  return 0;
 }
 if (!strcmp(name, "kern.fuxihapticpcm")) {
  if (refuse_pcm) {
   errno = ENOTSUP;
   return -1;
  }
  assert(!out && in && inlen >= 4 && inlen <= sizeof(model_haptic_pcm_last) && (inlen & 3u) == 0);
 if (model_actuator_lock) {
  assert(pthread_mutex_trylock(model_actuator_lock) == 0);
  pthread_mutex_unlock(model_actuator_lock);
  model_lock_free_pcm++;
 }
  model_haptic_pcm_calls++;
  model_haptic_pcm_bytes += inlen;
  model_haptic_pcm_last_bytes = inlen;
  memcpy(model_haptic_pcm_last, in, inlen);
  return 0;
 }
 assert(!strcmp(name, "kern.fuxivibrate") && !out && in && inlen == sizeof(int));
 memcpy(&actuator_value, in, inlen);
 actuator_calls++;
 if (actuator_value != 0 && model_actuator_lock) {
  assert(pthread_mutex_trylock(model_actuator_lock) == 0);
  pthread_mutex_unlock(model_actuator_lock);
  model_lock_free_vib++;
 }
 if (refuse_rtp && actuator_value == 0x01000000) {
  errno = ENOTSUP;
  return -1;
 }
 return 0;
}
static inline int pthread_cond_timedwait_relative_np(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *delay) {
 (void)cond; (void)mutex; (void)delay;
 assert(0 && "host advances the actuator clock synchronously");
 return -1;
}
'''

AUDIO_TEST = r'''
uint64_t model_now = 1000;
/* IODeviceTree:/chosen 的模型：預設沒有 fuxi,hal-haptic。 */
#define MODEL_CHOSEN ((fx_io_object_t)0x1234)
static bool model_chosen = true;
static CFTypeRef model_hal_haptic;
static CFTypeRef model_audio_voice;
static unsigned io_lookups, io_property_reads, io_releases;
fx_io_object_t IORegistryEntryFromPath(mach_port_t main_port, const char *path) {
 assert(main_port == MACH_PORT_NULL && !strcmp(path, "IODeviceTree:/chosen"));
 io_lookups++;
 return model_chosen ? MODEL_CHOSEN : MACH_PORT_NULL;
}
CFTypeRef IORegistryEntryCreateCFProperty(fx_io_object_t entry, CFStringRef key, CFAllocatorRef allocator, uint32_t options) {
 assert(entry == MODEL_CHOSEN && allocator == kCFAllocatorDefault && !options);
 io_property_reads++;
 if (!strcmp(key->s, "fuxi,hal-haptic")) return model_hal_haptic;
 if (!strcmp(key->s, "fuxi,audio-voice")) return model_audio_voice;
 assert(0 && "unexpected ADT key");
 return NULL;
}
static struct { AudioObjectID obj; UInt32 sel; UInt32 scope; } prop_events[32];
static unsigned prop_event_count;
static OSStatus model_props_changed(AudioServerPlugInHostRef h, AudioObjectID obj, UInt32 n, const AudioObjectPropertyAddress *addrs) {
 (void)h;
 for (UInt32 i = 0; i < n && prop_event_count < 32; i++) {
  prop_events[prop_event_count].obj = obj;
  prop_events[prop_event_count].sel = addrs[i].mSelector;
  prop_events[prop_event_count].scope = addrs[i].mScope;
  prop_event_count++;
 }
 return 0;
}
static const AudioServerPlugInHostInterface model_host_iface = {
 .PropertiesChanged = model_props_changed,
};
kern_return_t IOObjectRelease(fx_io_object_t object) {
 assert(object == MODEL_CHOSEN);
 io_releases++;
 return 0;
}
static unsigned select_case(const char *bootargs, CFTypeRef adt, const char *want_source) {
 const char *source = NULL;
 model_bootargs = bootargs;
 model_hal_haptic = adt;
 unsigned before = io_lookups, released = cf_data_releases;
 unsigned mode = actuator_select_mode(&source);
 assert(source && !strcmp(source, want_source));
 /* 命令列有開關時完全不碰 IOKit；看了 ADT 就一定釋放 chosen 與屬性。 */
 assert(!strcmp(want_source, "boot-args") ? io_lookups == before : io_lookups == before + 1);
 assert(io_releases == io_property_reads);
 assert(cf_data_releases == released + (adt && !strcmp(want_source, "ADT") && model_chosen ? 1 : 0));
 return mode;
}
static AudioServerPlugInDriverRef drv;
static UInt32 get_u32(AudioObjectID o, UInt32 sel, UInt32 scope) {
 AudioObjectPropertyAddress a = { sel, scope, 0 }; UInt32 v = 0xdeadbeef, size = 0;
 assert(interface.GetPropertyData(drv, o, 1, &a, 0, NULL, sizeof(v), &size, &v) == 0 && size == 4);
 return v;
}
static UInt32 list(AudioObjectID o, UInt32 sel, UInt32 scope, UInt32 *out, UInt32 room) {
 AudioObjectPropertyAddress a = { sel, scope, 0 }; UInt32 size = 0;
 assert(interface.HasProperty(drv, o, 1, &a));
 assert(interface.GetPropertyData(drv, o, 1, &a, 0, NULL, room * 4, &size, out) == 0);
 return size / 4;
}
int main(void) {
 void *made = FuxiAudio_Create(NULL, kAudioServerPlugInTypeUUID);
 assert(made != NULL && FuxiAudio_Create(NULL, IUnknownUUID) == NULL);
 drv = made;
 LPVOID got = NULL;
 CFUUIDBytes want = kAudioServerPlugInDriverInterfaceUUID->bytes;
 assert(interface.QueryInterface(made, want, &got) == S_OK && got == made);
 CFUUIDBytes other = { { 1, 2, 3 } };
 assert(interface.QueryInterface(made, other, &got) == E_NOINTERFACE);
 assert(interface.Initialize(drv, NULL) == 0 && ticks_per_frame > 0);
 UInt32 ids[8];
 assert(list(OBJECT_PLUGIN, kAudioPlugInPropertyDeviceList, kAudioObjectPropertyScopeGlobal, ids, 8) == 4);
 const char *uids[] = { "Speaker", "Hawking", "Actuator", "Codec" };
 for (int i = 0; i < 4; i++) {
  CFStringRef uid = &(const struct __CFString){ 1, uids[i] };
  AudioObjectPropertyAddress a = { kAudioPlugInPropertyTranslateUIDToDevice, kAudioObjectPropertyScopeGlobal, 0 };
  UInt32 dev = 0, size = 0;
  assert(interface.GetPropertyData(drv, OBJECT_PLUGIN, 1, &a, sizeof(uid), &uid, 4, &size, &dev) == 0);
  assert(dev == ids[i]);
  CFStringRef name = NULL;
  AudioObjectPropertyAddress n = { kAudioDevicePropertyDeviceUID, kAudioObjectPropertyScopeGlobal, 0 };
  assert(interface.GetPropertyData(drv, dev, 1, &n, 0, NULL, sizeof(name), &size, &name) == 0);
  assert(!strcmp(name->s, uids[i]));
  assert(get_u32(dev, kAudioDevicePropertyTransportType, kAudioObjectPropertyScopeGlobal) == 'bltn');
 }
 /* Hawking: one silent input, no output, never a default device */
 UInt32 s[4];
 assert(list(OBJECT_HAWKING, kAudioDevicePropertyStreams, kAudioObjectPropertyScopeInput, s, 4) == 1 && s[0] == OBJECT_HAWKING_INPUT);
 assert(list(OBJECT_HAWKING, kAudioDevicePropertyStreams, kAudioObjectPropertyScopeOutput, s, 4) == 0);
 assert(get_u32(OBJECT_HAWKING, kAudioDevicePropertyDeviceCanBeDefaultDevice, kAudioObjectPropertyScopeGlobal) == 0);
 assert(get_u32(OBJECT_SPEAKER, kAudioDevicePropertyDeviceCanBeDefaultDevice, kAudioObjectPropertyScopeGlobal) == 1);
 assert(get_u32(OBJECT_SPEAKER, kAudioDevicePropertyDeviceCanBeDefaultDevice, kAudioObjectPropertyScopeInput) == 0);
 assert(get_u32(OBJECT_SPEAKER, kAudioDevicePropertyDeviceCanBeDefaultDevice, kAudioObjectPropertyScopeOutput) == 1);
 assert(get_u32(OBJECT_CODEC, kAudioDevicePropertyDeviceCanBeDefaultDevice, kAudioObjectPropertyScopeInput) == 1);
 assert(get_u32(OBJECT_CODEC, kAudioDevicePropertyDeviceCanBeDefaultDevice, kAudioObjectPropertyScopeOutput) == 0);
 assert(get_u32(OBJECT_CODEC, kAudioDevicePropertyDeviceCanBeDefaultDevice, kAudioObjectPropertyScopeGlobal) == 1);
 assert(get_u32(OBJECT_CODEC, kAudioDevicePropertyDeviceCanBeDefaultSystemDevice, kAudioObjectPropertyScopeGlobal) == 0);
 assert(get_u32(OBJECT_CODEC, kAudioDevicePropertyDeviceCanBeDefaultSystemDevice, kAudioObjectPropertyScopeInput) == 0);
 UInt32 pref[2] = {0, 0};
 UInt32 pref_size = 0;
 AudioObjectPropertyAddress pa_in = { kAudioDevicePropertyPreferredChannelsForStereo, kAudioObjectPropertyScopeInput, 0 };
 AudioObjectPropertyAddress pa_out = { kAudioDevicePropertyPreferredChannelsForStereo, kAudioObjectPropertyScopeOutput, 0 };
 assert(interface.GetPropertyData(drv, OBJECT_CODEC, 1, &pa_in, 0, NULL, sizeof(pref), &pref_size, pref) == 0);
 assert(pref_size == 8 && pref[0] == 1 && pref[1] == 1);
 assert(interface.GetPropertyData(drv, OBJECT_SPEAKER, 1, &pa_out, 0, NULL, sizeof(pref), &pref_size, pref) == 0);
 assert(pref_size == 8 && pref[0] == 1 && pref[1] == 2);
 assert(interface.GetPropertyData(drv, OBJECT_SPEAKER, 1, &pa_in, 0, NULL, sizeof(pref), &pref_size, pref) == 0);
 assert(pref_size == 8 && pref[0] == 1 && pref[1] == 2);
 assert(get_u32(OBJECT_HAWKING, kAudioDevicePropertyDeviceCanBeDefaultDevice, kAudioObjectPropertyScopeInput) == 0);
 assert(get_u32(OBJECT_HAWKING_INPUT, kAudioStreamPropertyDirection, kAudioObjectPropertyScopeGlobal) == 1);
 assert(get_u32(OBJECT_HAWKING_INPUT, kAudioStreamPropertyTerminalType, kAudioObjectPropertyScopeGlobal) == 'micr');
 assert(list(OBJECT_SPEAKER, kAudioObjectPropertyOwnedObjects, kAudioObjectPropertyScopeGlobal, s, 4) == 3 && s[2] == OBJECT_SPEAKER_VOLUME);
 assert(list(OBJECT_ACTUATOR, kAudioDevicePropertyStreams, kAudioObjectPropertyScopeGlobal, s, 4) == 1 && s[0] == OBJECT_ACTUATOR_OUTPUT);
 /* Codec: the data source VirtualAudio needs */
 assert(get_u32(OBJECT_CODEC_SOURCE, kAudioSelectorControlPropertyCurrentItem, kAudioObjectPropertyScopeGlobal) == 'imic');
 UInt32 imic = 'imic', smic = 'smic', bad = 'xxxx';
 AudioObjectPropertyAddress cur = { kAudioSelectorControlPropertyCurrentItem, kAudioObjectPropertyScopeGlobal, 0 };
 assert(interface.SetPropertyData(drv, OBJECT_CODEC_SOURCE, 1, &cur, 0, NULL, 4, &imic) == 0);
 assert(interface.SetPropertyData(drv, OBJECT_CODEC_SOURCE, 1, &cur, 0, NULL, 4, &smic) == 0);
 assert(interface.SetPropertyData(drv, OBJECT_CODEC_SOURCE, 1, &cur, 0, NULL, 4, &imic) == 0);
 assert(interface.SetPropertyData(drv, OBJECT_CODEC_SOURCE, 1, &cur, 0, NULL, 4, &bad) == kAudioHardwareIllegalOperationError);
 /* formats */
 AudioStreamBasicDescription f; UInt32 size = 0;
 AudioObjectPropertyAddress fa = { kAudioStreamPropertyVirtualFormat, kAudioObjectPropertyScopeGlobal, 0 };
 assert(interface.GetPropertyData(drv, OBJECT_SPEAKER_OUTPUT, 1, &fa, 0, NULL, sizeof(f), &size, &f) == 0);
 assert(f.mSampleRate == 48000 && f.mChannelsPerFrame == 2 && f.mBitsPerChannel == 16 && f.mBytesPerFrame == 4);
 assert(interface.GetPropertyData(drv, OBJECT_CODEC_INPUT, 1, &fa, 0, NULL, sizeof(f), &size, &f) == 0 && f.mChannelsPerFrame == 1);
 assert(interface.SetPropertyData(drv, OBJECT_CODEC_INPUT, 1, &fa, 0, NULL, sizeof(f), &f) == 0);
 f.mSampleRate = 44100;
 assert(interface.SetPropertyData(drv, OBJECT_CODEC_INPUT, 1, &fa, 0, NULL, sizeof(f), &f) == kAudioDeviceUnsupportedFormatError);
 /* volume */
 Float32 v = 0.5f, back = 0;
 AudioObjectPropertyAddress va = { kAudioLevelControlPropertyScalarValue, kAudioObjectPropertyScopeGlobal, 0 };
 assert(interface.SetPropertyData(drv, OBJECT_SPEAKER_VOLUME, 1, &va, 0, NULL, 4, &v) == 0);
 assert(interface.GetPropertyData(drv, OBJECT_SPEAKER_VOLUME, 1, &va, 0, NULL, 4, &size, &back) == 0 && back == 0.5f);
 AudioObjectPropertyAddress da = { kAudioLevelControlPropertyDecibelValue, kAudioObjectPropertyScopeGlobal, 0 };
 assert(interface.GetPropertyData(drv, OBJECT_SPEAKER_VOLUME, 1, &da, 0, NULL, 4, &size, &back) == 0 && back == -24.0f);
 /* unknown property and object */
 AudioObjectPropertyAddress u = { 'zzzz', kAudioObjectPropertyScopeGlobal, 0 };
 assert(!interface.HasProperty(drv, OBJECT_HAWKING, 1, &u));
 assert(interface.GetPropertyDataSize(drv, OBJECT_HAWKING, 1, &u, 0, NULL, &size) == kAudioHardwareUnknownPropertyError);
 assert(interface.GetPropertyDataSize(drv, 99, 1, &u, 0, NULL, &size) == kAudioHardwareBadObjectError);
 /* clock: zero timestamps advance by whole periods of 16384 frames at 48 kHz */
 assert(interface.StartIO(drv, OBJECT_HAWKING, 1) == 0);
 assert(interface.StartIO(drv, OBJECT_SPEAKER, 1) == 0 && interface.StartIO(drv, OBJECT_CODEC, 1) == 0);
 Float64 st; UInt64 ht, seed;
 assert(interface.GetZeroTimeStamp(drv, OBJECT_HAWKING, 1, &st, &ht, &seed) == 0 && st == 0 && ht == 1000);
 model_now += (UInt64)(ticks_per_frame * 16384 * 2.5);
 assert(interface.GetZeroTimeStamp(drv, OBJECT_HAWKING, 1, &st, &ht, &seed) == 0 && st == 2 * 16384);
 assert(ht == 1000 + (UInt64)(2.0 * ticks_per_frame * 16384));
 /* I/O: inputs are silence, outputs are taken without waiting */
 int16_t buf[512];
 Boolean will, in_place;
 assert(interface.WillDoIOOperation(drv, OBJECT_HAWKING, 1, kAudioServerPlugInIOOperationReadInput, &will, &in_place) == 0 && will);
 memset(buf, 0x55, sizeof(buf));
 assert(interface.DoIOOperation(drv, OBJECT_HAWKING, OBJECT_HAWKING_INPUT, 1, kAudioServerPlugInIOOperationReadInput, 256, NULL, buf, NULL) == 0);
 for (int i = 0; i < 256; i++) assert(buf[i] == 0);
 assert(buf[256] == 0x5555);
 memset(buf, 0x55, sizeof(buf));
 assert(interface.DoIOOperation(drv, OBJECT_CODEC, OBJECT_CODEC_INPUT, 1, kAudioServerPlugInIOOperationReadInput, 128, NULL, buf, NULL) == 0);
 for (int i = 0; i < 128; i++) assert(buf[i] == 0);
 assert(interface.DoIOOperation(drv, OBJECT_SPEAKER, OBJECT_SPEAKER_OUTPUT, 1, kAudioServerPlugInIOOperationWriteMix, 128, NULL, buf, NULL) == 0);
 assert(written == 128);
 buf[3] = 1000;
 assert(interface.DoIOOperation(drv, OBJECT_ACTUATOR, OBJECT_ACTUATOR_OUTPUT, 1, kAudioServerPlugInIOOperationWriteMix, 64, NULL, buf, NULL) == 0);
 assert(haptic_said == 1);
 assert(interface.StopIO(drv, OBJECT_CODEC, 1) == 0 && interface.StopIO(drv, OBJECT_SPEAKER, 1) == 0);
 assert(interface.StopIO(drv, OBJECT_HAWKING, 1) == 0 && interface.StopIO(drv, OBJECT_HAWKING, 1) == 0);
 assert(interface.StartIO(drv, OBJECT_HAWKING_INPUT, 1) == kAudioHardwareBadObjectError);

 /* 實際的 DoIOOperation 將非零樣本依 host time 排程，I/O callback 內沒有呼叫硬體。 */
 assert(!actuator.enabled && actuator_calls == 0);
 actuator.enabled = true;   /* 以手動推進的時鐘代替正式 worker */
 model_actuator_lock = &actuator.lock;
 assert(interface.StartIO(drv, OBJECT_ACTUATOR, 1) == 0);
 int16_t wave[ACTUATOR_WINDOW * 3] = {INT16_MIN};
 wave[ACTUATOR_WINDOW] = 16384;
 UInt64 ticks_ms = actuator_ticks_ms();
 AudioServerPlugInIOCycleInfo cycle = {.mOutputTime = {
  .mFlags = kAudioTimeStampHostTimeValid, .mHostTime = model_now + ticks_ms * 20
 }};
 assert(interface.DoIOOperation(drv, OBJECT_ACTUATOR, OBJECT_ACTUATOR_OUTPUT, 1,
  kAudioServerPlugInIOOperationWriteMix, ACTUATOR_WINDOW * 3, &cycle, wave, NULL) == 0);
 assert(actuator.count == 2 && actuator_calls == 0);
 assert(actuator.queue[0].start == cycle.mOutputTime.mHostTime && actuator.queue[0].level == 128);
 assert(actuator.queue[1].start == actuator.queue[0].end && actuator.queue[1].level == 64);
 /* 同一時間戳的混音重送不重複排進馬達佇列。 */
 actuator_write(wave, ACTUATOR_WINDOW * 3, &cycle);
 assert(actuator.count == 2);
 pthread_mutex_lock(&actuator.lock);
 model_now = actuator.queue[0].start;
 actuator_submit(model_now, ticks_ms);
 assert(actuator_calls == 1 && actuator_value == ((128 << 16) | (10 + ACTUATOR_SLACK_MS)));
 model_now = actuator.queue[1].start;
 actuator_submit(model_now, ticks_ms);
 assert(actuator_calls == 2 && actuator_value == ((64 << 16) | 10) && !actuator.count);
 pthread_mutex_unlock(&actuator.lock);
 assert(interface.StopIO(drv, OBJECT_ACTUATOR, 1) == 0);
 assert(actuator_calls == 3 && actuator_value == 0 && !actuator.active && !actuator.count);
 assert(interface.StartIO(drv, OBJECT_ACTUATOR, 1) == 0);
 cycle.mOutputTime.mHostTime = model_now - ticks_ms * 40;
 actuator_write(wave, ACTUATOR_WINDOW * 3, &cycle);
 assert(actuator.count == 0 && actuator_calls == 3);
 cycle.mOutputTime.mHostTime = model_now + ticks_ms * 501;
 actuator_write(wave, ACTUATOR_WINDOW, &cycle);
 assert(actuator.count == 0 && actuator_calls == 3);
 assert(interface.StopIO(drv, OBJECT_ACTUATOR, 1) == 0);
 actuator.rtp = true;
 assert(interface.StartIO(drv, OBJECT_ACTUATOR, 1) == 0);
 cycle.mOutputTime.mHostTime = model_now + ticks_ms * 10;
 actuator_write(wave, ACTUATOR_WINDOW * 3, &cycle);
 pthread_mutex_lock(&actuator.lock);
 actuator_submit(actuator.queue[actuator.head].start, ticks_ms);
 actuator_submit(actuator.queue[actuator.head].start, ticks_ms);
 pthread_mutex_unlock(&actuator.lock);
 assert(actuator_calls == 4 && actuator_value == 0x01000000);
 cycle.mOutputTime.mHostTime += 40 * ticks_ms;
 actuator_write(wave, ACTUATOR_WINDOW, &cycle);
 pthread_mutex_lock(&actuator.lock);
 actuator_submit(actuator.queue[actuator.head].start, ticks_ms);
 pthread_mutex_unlock(&actuator.lock);
 assert(actuator_calls == 5 && actuator_value == 0x01000000);
 assert(interface.StopIO(drv, OBJECT_ACTUATOR, 1) == 0 && !actuator.burst_end);
 refuse_rtp = true;
 assert(interface.StartIO(drv, OBJECT_ACTUATOR, 1) == 0);
 cycle.mOutputTime.mHostTime += 40 * ticks_ms;
 actuator_write(wave, ACTUATOR_WINDOW, &cycle);
 pthread_mutex_lock(&actuator.lock);
 actuator_submit(actuator.queue[actuator.head].start, ticks_ms);
 pthread_mutex_unlock(&actuator.lock);
 assert(actuator_calls == 7 && actuator_value == ((128 << 16) | 10) && !actuator.rtp);
 assert(interface.StopIO(drv, OBJECT_ACTUATOR, 1) == 0);
 /* 弱脈衝保底（peak >= 32 -> level=1）、輕微晚醒寬容（<=40 ms）與微晚寫入寬容（<=8 ms）。 */
 assert(interface.StartIO(drv, OBJECT_ACTUATOR, 1) == 0);
 int16_t quiet_tap[ACTUATOR_WINDOW] = {0};
 quiet_tap[0] = 48;
 cycle.mOutputTime.mHostTime = model_now - ticks_ms * 13; /* end = now - 3 ms */
 actuator_write(quiet_tap, ACTUATOR_WINDOW, &cycle);
 assert(actuator.count == 1 && actuator.queue[actuator.head].level == 1);
 unsigned before_grace = actuator_calls;
 pthread_mutex_lock(&actuator.lock);
 actuator_submit(actuator.queue[actuator.head].end + ticks_ms * 6, ticks_ms);
 pthread_mutex_unlock(&actuator.lock);
 assert(actuator_calls == before_grace + 1 && actuator_value == ((1 << 16) | 10));
 cycle.mOutputTime.mHostTime = model_now + ticks_ms * 5;
 actuator_write(quiet_tap, ACTUATOR_WINDOW, &cycle);
 assert(actuator.count == 1);
 pthread_mutex_lock(&actuator.lock);
 actuator_submit(actuator.queue[actuator.head].end + ticks_ms * 30, ticks_ms);
 pthread_mutex_unlock(&actuator.lock);
 assert(actuator_calls == before_grace + 2 && actuator.count == 0);
 cycle.mOutputTime.mHostTime = model_now + ticks_ms * 50;
 actuator_write(quiet_tap, ACTUATOR_WINDOW, &cycle);
 assert(actuator.count == 1);
 unsigned late_before = actuator.drops_late;
 pthread_mutex_lock(&actuator.lock);
 actuator_submit(actuator.queue[actuator.head].end + ticks_ms * (ACTUATOR_SUBMIT_LATE_GRACE_MS + 5), ticks_ms);
 pthread_mutex_unlock(&actuator.lock);
 assert(actuator_calls == before_grace + 2 && actuator.count == 0 && actuator.drops_late == late_before + 1);
 assert(interface.StopIO(drv, OBJECT_ACTUATOR, 1) == 0);
 uint8_t packet[ACTUATOR_PCM_BYTES];
 int16_t bipolar[] = {INT16_MIN, INT16_MIN, INT16_MAX, INT16_MAX, 0};
 assert(actuator_pcm_packet(packet, bipolar, 5) == 8);
 assert((int8_t)packet[0] == -96 && packet[1] == 95);
 for (unsigned i = 2; i < 8; i++) assert(!packet[i]);
 /* 連續串流模式（mode 4）感知曲線單調遞增、無人工尾端 4B 零填補、突發尾音不抬升。 */
 assert(actuator_stream_target_peak(0) == 0);
 assert(actuator_stream_target_peak(ACTUATOR_PEAK_FLOOR - 1) == 0);
 assert(actuator_stream_target_peak(ACTUATOR_PEAK_FLOOR) == 1);
 assert(actuator_stream_target_peak(256) == 8);
 assert(actuator_stream_target_peak(1024) == 32);
 assert(actuator_stream_target_peak(4096) == 55);
 assert(actuator_stream_target_peak(32768) == ACTUATOR_STREAM_PCM_LIMIT);
 unsigned prev_t = 0;
 for (unsigned p = ACTUATOR_PEAK_FLOOR; p <= 32768; p += 128) {
  unsigned cur_t = actuator_stream_target_peak(p);
  assert(cur_t >= prev_t && cur_t <= ACTUATOR_STREAM_PCM_LIMIT);
  prev_t = cur_t;
 }
 assert(actuator_stream_packet(packet, bipolar, 5, 32768) == 4);
 assert((int8_t)packet[0] == -126 && (int8_t)packet[1] == 126 && !packet[2] && !packet[3]);
 int16_t stream_buf[256];
 for (int i = 0; i < 256; i++) stream_buf[i] = (i & 2) ? 1024 : -1024;
 assert(actuator_stream_packet(packet, stream_buf, 256, 1024) == 128);
 assert((int8_t)packet[0] == -32 && (int8_t)packet[1] == 32);
 /* 同一次突發內（包絡 24576）的衰減尾音（peak=2048）按同比例縮放，不被抬高。 */
 int16_t tail_buf[256];
 for (int i = 0; i < 256; i++) tail_buf[i] = 2048;
 assert(actuator_stream_packet(packet, tail_buf, 256, 24576) == 128);
 assert((int8_t)packet[0] == 9);
 /* 端對端 mode 4 串流：連續兩塊 256 幀皆送出 128B 至 kern.fuxihapticpcm 且包絡保持；間隔 >18ms 後重置。 */
 actuator.pcm = true;
 actuator.stream = true;
 actuator.rtp = false;
 assert(interface.StartIO(drv, OBJECT_ACTUATOR, 1) == 0);
 int16_t strong_buf[256];
 for (int i = 0; i < 256; i++) strong_buf[i] = 24576;
 cycle.mOutputTime.mHostTime = model_now;
 actuator_write(strong_buf, 256, &cycle);
 cycle.mOutputTime.mHostTime = model_now + (UInt64)(ticks_per_frame * 256);
 actuator_write(tail_buf, 256, &cycle);
 assert(actuator.count == 2 && actuator.stream_env == 24576);
 pthread_mutex_lock(&actuator.lock);
 actuator_submit(model_now, ticks_ms);
 assert(model_haptic_pcm_calls == 1 && model_haptic_pcm_last_bytes == 128 && (int8_t)model_haptic_pcm_last[0] == 113);
 assert(actuator.stream_playing_until > model_now);
 actuator_submit(model_now + ticks_ms, ticks_ms);
 assert(model_haptic_pcm_calls == 2 && model_haptic_pcm_last_bytes == 128 && (int8_t)model_haptic_pcm_last[0] == 9);
 pthread_mutex_unlock(&actuator.lock);
 /* 靜音間隔 30 ms 後再來輕震（1024）-> 包絡重置並對應到 32 */
 cycle.mOutputTime.mHostTime = model_now + ticks_ms * 45;
 model_now += ticks_ms * 45;
 actuator_write(stream_buf, 256, &cycle);
 assert(actuator.count == 1 && actuator.stream_env == 1024);
 pthread_mutex_lock(&actuator.lock);
 actuator_submit(model_now, ticks_ms);
 assert(model_haptic_pcm_calls == 3 && (int8_t)model_haptic_pcm_last[0] == -32);
 pthread_mutex_unlock(&actuator.lock);
 /* ENOTSUP 退回 timed pulse 並清除 stream 狀態 */
 refuse_pcm = true;
 cycle.mOutputTime.mHostTime = model_now + ticks_ms * 30;
 model_now += ticks_ms * 30;
 actuator_write(stream_buf, 256, &cycle);
 unsigned before_fb = actuator_calls;
 pthread_mutex_lock(&actuator.lock);
 actuator_submit(model_now, ticks_ms);
 pthread_mutex_unlock(&actuator.lock);
 assert(!actuator.pcm && !actuator.stream && actuator_calls == before_fb + 1);
 assert(model_lock_free_pcm == 3 && model_lock_free_vib >= 7);
 refuse_pcm = false;
 assert(interface.StopIO(drv, OBJECT_ACTUATOR, 1) == 0);
 /* mode 5（固定線性增益 + 連續塊合併至 376 B + 雜訊門檻）：新突發 <3 丟棄，續流 >=2 保留。 */
 int16_t dust_buf[256], cont_buf[256], mid_buf[256];
 for (int i = 0; i < 256; i++) {
  dust_buf[i] = 350;   /* sum=700 -> (700*176+32768)/65536 = 2 (<3 fresh) */
  cont_buf[i] = 400;   /* sum=800 -> (800*176+32768)/65536 = 2 (>=2 cont) */
  mid_buf[i] = (i & 2) ? 8192 : -8192; /* sum=+-16384 -> +-44 */
 }
 assert(actuator_linear_packet(packet, dust_buf, 256, ACTUATOR_LINEAR_FRESH_MIN_PEAK) == 0);
 assert(actuator_linear_packet(packet, dust_buf, 256, ACTUATOR_LINEAR_CONT_MIN_PEAK) == 128);
 assert(actuator_linear_packet(packet, mid_buf, 256, ACTUATOR_LINEAR_FRESH_MIN_PEAK) == 128);
 assert((int8_t)packet[0] == -44 && (int8_t)packet[1] == 44);
 actuator.pcm = true;
 actuator.stream = true;
 actuator.linear = true;
 actuator.rtp = false;
 assert(interface.StartIO(drv, OBJECT_ACTUATOR, 1) == 0);
 unsigned pcm_before_m5 = model_haptic_pcm_calls;
 /* 新突發低於門檻（peak8=2）不送硬體 */
 cycle.mOutputTime.mHostTime = model_now;
 actuator_write(dust_buf, 256, &cycle);
 assert(actuator.count == 0);
 /* 三塊連續（128+128+128=384 > 376）：前兩塊合併成 256 B，第三塊獨立成 128 B */
 model_now += ticks_ms * 30;
 UInt64 chunk_ticks = (UInt64)(ticks_per_frame * 256);
 cycle.mOutputTime.mHostTime = model_now;
 actuator_write(mid_buf, 256, &cycle);
	/* 單一新 128 B 塊等過完整一個 5.33 ms CoreAudio 週期（max(end+2ms, start+7ms)）合包；第二塊一到立刻轉為提前 2 ms 發射並合併 */
 pthread_mutex_lock(&actuator.lock);
 assert(actuator.count == 1);
	const struct actuator_pulse *head = &actuator.queue[actuator.head];
	UInt64 expect_coalesce = head->end + ACTUATOR_STREAM5_COALESCE_MARGIN_MS * ticks_ms;
	UInt64 expect_floor = head->start + ACTUATOR_STREAM5_COALESCE_MIN_MS * ticks_ms;
	UInt64 expect_arrival = head->enqueued + ACTUATOR_STREAM5_COALESCE_ARRIVAL_MS * ticks_ms;
	if (expect_floor > expect_coalesce) expect_coalesce = expect_floor;
	if (expect_arrival > expect_coalesce) expect_coalesce = expect_arrival;
	assert(actuator_fire_at(head, false, ticks_ms) == expect_coalesce);
	assert(expect_coalesce > model_now + chunk_ticks);
	assert(actuator_fire_at(head, true, ticks_ms) == model_now - ACTUATOR_STREAM_PRELOAD_MS * ticks_ms);
 pthread_mutex_unlock(&actuator.lock);
 cycle.mOutputTime.mHostTime = model_now + chunk_ticks;
 actuator_write(mid_buf, 256, &cycle);
 pthread_mutex_lock(&actuator.lock);
 assert(actuator.count == 2);
	assert(actuator_fire_at(&actuator.queue[actuator.head], false, ticks_ms) == model_now - ACTUATOR_STREAM_PRELOAD_MS * ticks_ms);
 pthread_mutex_unlock(&actuator.lock);
 cycle.mOutputTime.mHostTime = model_now + chunk_ticks * 2;
 actuator_write(cont_buf, 256, &cycle);
 assert(actuator.count == 3);
 pthread_mutex_lock(&actuator.lock);
 actuator_submit(model_now, ticks_ms);
 assert(model_haptic_pcm_calls == pcm_before_m5 + 1 && model_haptic_pcm_last_bytes == 256);
 assert((int8_t)model_haptic_pcm_last[0] == -44 && (int8_t)model_haptic_pcm_last[128] == -44);
 assert(actuator.count == 1);
 actuator_submit(model_now + ticks_ms * 5, ticks_ms);
 assert(model_haptic_pcm_calls == pcm_before_m5 + 2 && model_haptic_pcm_last_bytes == 128);
 assert((int8_t)model_haptic_pcm_last[0] == 2);
 pthread_mutex_unlock(&actuator.lock);
 /* 間隔 >2 ms 的兩塊不得合併 */
 model_now += ticks_ms * 30;
 cycle.mOutputTime.mHostTime = model_now;
 actuator_write(mid_buf, 256, &cycle);
 cycle.mOutputTime.mHostTime = model_now + chunk_ticks + ticks_ms * 4;
 actuator_write(mid_buf, 256, &cycle);
 assert(actuator.count == 2);
 pthread_mutex_lock(&actuator.lock);
 actuator_submit(model_now, ticks_ms);
 assert(model_haptic_pcm_calls == pcm_before_m5 + 3 && model_haptic_pcm_last_bytes == 128);
 assert(actuator.count == 1);
 pthread_mutex_unlock(&actuator.lock);
 assert(interface.StopIO(drv, OBJECT_ACTUATOR, 1) == 0);
 actuator.linear = false;
 /* mode 6（24 kHz 原始樣點合併＋前導靜音裁切＋單突發感知增益映射）：鍵盤微震可辨且不破音，重按滿幅。 */
 actuator.perceptual = true;
 actuator.stream = true;
 actuator.pcm = true;
 assert(interface.StartIO(drv, OBJECT_ACTUATOR, 1) == 0);
 unsigned pcm_before_m6 = model_haptic_pcm_calls;
 int16_t kbd_lead[256] = {0}, kbd_main[256] = {0}, heavy_buf[256] = {0}, cont_tail_buf[256] = {0};
 for (int i = 0; i < 256; i++) {
  kbd_lead[i] = i < 40 ? 0 : (int16_t)((i & 2) ? 200 : -200);
  kbd_main[i] = i < 216 ? (int16_t)((i & 2) ? 600 : -600) : 0;
  heavy_buf[i] = (int16_t)((i & 2) ? 26000 : -26000);
  cont_tail_buf[i] = (int16_t)((i & 2) ? 150 : -150);
 }
 model_now += ticks_ms * 40;
 cycle.mOutputTime.mHostTime = model_now;
 actuator_write(kbd_lead, 256, &cycle);
 pthread_mutex_lock(&actuator.lock);
 assert(actuator.count == 1);
 /* 單一短塊等滿 5.33 ms 週期＋2 ms 餘裕（下限 7 ms）確保第二塊先到合併；第二塊一到立刻提前 2 ms 發射 */
 {
  UInt64 c6 = actuator.queue[actuator.head].end + ACTUATOR_STREAM6_COALESCE_MARGIN_MS * ticks_ms;
  UInt64 f6 = actuator.queue[actuator.head].start + ACTUATOR_STREAM6_COALESCE_MIN_MS * ticks_ms;
		UInt64 a6 = actuator.queue[actuator.head].enqueued + ACTUATOR_STREAM6_COALESCE_ARRIVAL_MS * ticks_ms;
		UInt64 e6 = c6 > f6 ? c6 : f6;
		if (a6 > e6) e6 = a6;
		assert(actuator_fire_at(&actuator.queue[actuator.head], false, ticks_ms) == e6);
 }
 pthread_mutex_unlock(&actuator.lock);
 cycle.mOutputTime.mHostTime = model_now + chunk_ticks;
 actuator_write(kbd_main, 256, &cycle);
 pthread_mutex_lock(&actuator.lock);
 assert(actuator.count == 2);
 assert(actuator_fire_at(&actuator.queue[actuator.head], false, ticks_ms) ==
        model_now - ACTUATOR_STREAM_PRELOAD_MS * ticks_ms);
 actuator_submit(model_now, ticks_ms);
 /* 兩塊合併後裁除前導 18 個零點（保留 2 點過零前緣），peak16=600 -> target≈25 */
 assert(model_haptic_pcm_calls == pcm_before_m6 + 1);
 assert(model_haptic_pcm_last_bytes > 200 && model_haptic_pcm_last_bytes < 256);
 unsigned kbd_pk = 0;
 for (size_t i = 0; i < model_haptic_pcm_last_bytes; i++) {
  int v = (int8_t)model_haptic_pcm_last[i];
  unsigned m = (unsigned)(v < 0 ? -v : v);
  if (m > kbd_pk) kbd_pk = m;
 }
 assert(kbd_pk >= 23 && kbd_pk <= 28);
 /* 續段小尾音（peak16=150）沿用同一突發比例（~6），不得被重新拉大到 24 */
 pthread_mutex_unlock(&actuator.lock);
 cycle.mOutputTime.mHostTime = model_now + chunk_ticks * 2;
 actuator_write(cont_tail_buf, 256, &cycle);
 pthread_mutex_lock(&actuator.lock);
 actuator_submit(model_now + chunk_ticks * 2, ticks_ms);
 assert(model_haptic_pcm_calls == pcm_before_m6 + 2 && model_haptic_pcm_last_bytes == 128);
 unsigned tail_pk = 0;
 for (size_t i = 0; i < model_haptic_pcm_last_bytes; i++) {
  int v = (int8_t)model_haptic_pcm_last[i];
  unsigned m = (unsigned)(v < 0 ? -v : v);
  if (m > tail_pk) tail_pk = m;
 }
 assert(tail_pk >= 4 && tail_pk <= 9);
 pthread_mutex_unlock(&actuator.lock);
 /* 隔 40 ms 實機 Spotlight 鍵盤振幅（peak16=6400）落在 55..62（Micro-2L 輕脆檔），與開關／長按明確拉開 */
 int16_t real_kbd_buf[256] = {0};
 for (int i = 0; i < 256; i++) {
  real_kbd_buf[i] = i < 200 ? (int16_t)((i & 2) ? 6400 : -6400) : 0;
 }
 model_now += ticks_ms * 40;
 cycle.mOutputTime.mHostTime = model_now;
 actuator_write(real_kbd_buf, 256, &cycle);
 pthread_mutex_lock(&actuator.lock);
 /* 單塊且尾端已靜音的極短鍵盤點按應零額外等待，立即提前 2 ms 發射 */
 assert(actuator.count == 1);
 assert(actuator_fire_at(&actuator.queue[actuator.head], false, ticks_ms) ==
        model_now - ACTUATOR_STREAM_PRELOAD_MS * ticks_ms);
 actuator_submit(model_now, ticks_ms);
 assert(model_haptic_pcm_calls == pcm_before_m6 + 3 && model_haptic_pcm_last_bytes == 128);
 unsigned real_kbd_pk = 0;
 for (size_t i = 0; i < model_haptic_pcm_last_bytes; i++) {
  int v = (int8_t)model_haptic_pcm_last[i];
  unsigned m = (unsigned)(v < 0 ? -v : v);
  if (m > real_kbd_pk) real_kbd_pk = m;
 }
 assert(real_kbd_pk >= 55 && real_kbd_pk <= 62);
 /* 驗證動態層次：滾輪(1100) < 鍵盤(6400) < 音量(8000) < 切換器(10500) < 控制中心(16384) < 長按(23000) */
 assert(actuator_perceptual_target(1100) <= 34);
 assert(actuator_perceptual_target(6400) == 58);
 assert(actuator_perceptual_target(8000) >= 62 && actuator_perceptual_target(8000) <= 66);
 assert(actuator_perceptual_target(10500) >= 73 && actuator_perceptual_target(10500) <= 78);
 assert(actuator_perceptual_target(16384) == 98);
 assert(actuator_perceptual_target(23000) >= 118 && actuator_perceptual_target(23000) <= 122);
 pthread_mutex_unlock(&actuator.lock);
 /* 隔 40 ms 新重按（peak16=26624+）應打滿 126（peak16=26000 亦達 125） */
 model_now += ticks_ms * 40;
 cycle.mOutputTime.mHostTime = model_now;
 actuator_write(heavy_buf, 256, &cycle);
 pthread_mutex_lock(&actuator.lock);
 actuator_submit(model_now, ticks_ms);
 assert(model_haptic_pcm_calls == pcm_before_m6 + 4 && model_haptic_pcm_last_bytes == 128);
 unsigned heavy_pk = 0;
 for (size_t i = 0; i < model_haptic_pcm_last_bytes; i++) {
  int v = (int8_t)model_haptic_pcm_last[i];
  unsigned m = (unsigned)(v < 0 ? -v : v);
  if (m > heavy_pk) heavy_pk = m;
 }
 assert(heavy_pk >= 124 && heavy_pk <= ACTUATOR_STREAM_PCM_LIMIT);
 pthread_mutex_unlock(&actuator.lock);
	/* 回歸測試 1：CoreAudio WriteMix 晚於 mOutputTime（4 ms）時，仍以到達時間＋8 ms 等第二塊合併 */
	int16_t late_sliver[256] = {0}, late_mid_active[256] = {0}, late_mid_silent[256] = {0}, late_chunk3[256] = {0};
	for (int idx = 0; idx < 256; idx++) {
		if (idx >= 176) late_sliver[idx] = (int16_t)((idx & 2) ? 6000 : -6000);
		late_mid_active[idx] = (int16_t)((idx & 2) ? 16000 : -16000);
		if (idx < 216) late_mid_silent[idx] = (int16_t)((idx & 2) ? 16000 : -16000);
		if (idx < 192) late_chunk3[idx] = (int16_t)((idx & 2) ? 12000 : -12000);
	}
	model_now += ticks_ms * 40;
	cycle.mOutputTime.mHostTime = model_now - ticks_ms * 4;
	actuator_write(late_sliver, 256, &cycle);
	pthread_mutex_lock(&actuator.lock);
	assert(actuator.count == 1);
	assert(actuator_fire_at(&actuator.queue[actuator.head], false, ticks_ms) ==
	       model_now + ACTUATOR_STREAM6_COALESCE_ARRIVAL_MS * ticks_ms);
	pthread_mutex_unlock(&actuator.lock);
	/* 回歸測試 2：前導 88 點靜音的 2 塊短按，若第 2 塊尾端已靜音則立刻發射並合併成單包（~168 B） */
	model_now += chunk_ticks;
	cycle.mOutputTime.mHostTime = model_now - ticks_ms * 4;
	actuator_write(late_mid_silent, 256, &cycle);
	pthread_mutex_lock(&actuator.lock);
	assert(actuator.count == 2);
	assert(actuator_fire_at(&actuator.queue[actuator.head], false, ticks_ms) ==
	       actuator.queue[actuator.head].start - ACTUATOR_STREAM_PRELOAD_MS * ticks_ms);
	actuator_submit(model_now, ticks_ms);
	assert(model_haptic_pcm_calls == pcm_before_m6 + 5 &&
	       model_haptic_pcm_last_bytes >= 164 && model_haptic_pcm_last_bytes <= 176);
	assert(actuator.count == 0);
	pthread_mutex_unlock(&actuator.lock);
	/* 回歸測試 3：前導靜音使第 1 塊有效長 <= 120（含 110 與 42）時，第 2 塊尾端仍帶波形應續等第 3 塊（12 ms）一次合併成單包 */
	model_now += ticks_ms * 40;
	cycle.mOutputTime.mHostTime = model_now - ticks_ms * 3;
	actuator_write(late_sliver, 256, &cycle);
	model_now += chunk_ticks;
	cycle.mOutputTime.mHostTime = model_now - ticks_ms * 3;
	actuator_write(late_mid_active, 256, &cycle);
	pthread_mutex_lock(&actuator.lock);
	assert(actuator.count == 2);
	assert(actuator_fire_at(&actuator.queue[actuator.head], false, ticks_ms) ==
	       model_now + ACTUATOR_STREAM6_COALESCE_CHUNK3_ARRIVAL_MS * ticks_ms);
	pthread_mutex_unlock(&actuator.lock);
	model_now += chunk_ticks;
	cycle.mOutputTime.mHostTime = model_now - ticks_ms * 3;
	actuator_write(late_chunk3, 256, &cycle);
	pthread_mutex_lock(&actuator.lock);
	assert(actuator.count == 3);
	assert(actuator_fire_at(&actuator.queue[actuator.head], false, ticks_ms) ==
	       actuator.queue[actuator.head].start - ACTUATOR_STREAM_PRELOAD_MS * ticks_ms);
	actuator_submit(model_now, ticks_ms);
	assert(model_haptic_pcm_calls == pcm_before_m6 + 6 &&
	       model_haptic_pcm_last_bytes >= 296 && model_haptic_pcm_last_bytes <= 304);
	assert(actuator.count == 0);
	pthread_mutex_unlock(&actuator.lock);
	/* 回歸測試 4：有效長 110（舊門檻 80 會誤拆）＋第 2 塊尾端活躍 -> 110+128+128=366<=376，應等第 3 塊一次合併 */
	model_now += ticks_ms * 40;
	cycle.mOutputTime.mHostTime = model_now - ticks_ms * 2;
	actuator_write(kbd_lead, 256, &cycle);
	model_now += chunk_ticks;
	cycle.mOutputTime.mHostTime = model_now - ticks_ms * 2;
	actuator_write(late_mid_active, 256, &cycle);
	pthread_mutex_lock(&actuator.lock);
	assert(actuator.count == 2);
	assert(actuator_fire_at(&actuator.queue[actuator.head], false, ticks_ms) ==
	       model_now + ACTUATOR_STREAM6_COALESCE_CHUNK3_ARRIVAL_MS * ticks_ms);
	pthread_mutex_unlock(&actuator.lock);
	model_now += chunk_ticks;
	cycle.mOutputTime.mHostTime = model_now - ticks_ms * 2;
	actuator_write(late_chunk3, 256, &cycle);
	pthread_mutex_lock(&actuator.lock);
	assert(actuator.count == 3);
	actuator_submit(model_now, ticks_ms);
	assert(model_haptic_pcm_calls == pcm_before_m6 + 7 &&
	       model_haptic_pcm_last_bytes >= 364 && model_haptic_pcm_last_bytes <= ACTUATOR_PCM_BYTES);
	assert(actuator.count == 0);
	pthread_mutex_unlock(&actuator.lock);
	/* 回歸測試 5：CoreAudio mRateScalar > 1 造成下一週期 mHostTime 提早 1..20 ticks 時，不得丟棄第 2 塊（含 mSampleTime 與純 mHostTime 雙路徑），但同週期重送仍應擋掉 */
	model_now += ticks_ms * 40;
	AudioServerPlugInIOCycleInfo st_cycle = {.mOutputTime = {
	 .mFlags = kAudioTimeStampSampleTimeValid | kAudioTimeStampHostTimeValid,
	 .mSampleTime = 10240.0, .mHostTime = model_now
	}};
	actuator.drops_dup = 0;
	actuator_write(kbd_lead, 256, &st_cycle);
	/* 下一週期 mSampleTime 連續 +256，但 mHostTime 提早 20 ticks（模擬 PLL 抖動） */
	st_cycle.mOutputTime.mSampleTime = 10496.0;
	st_cycle.mOutputTime.mHostTime = model_now + chunk_ticks - 20;
	actuator_write(late_mid_silent, 256, &st_cycle);
	/* 重送同一個 mSampleTime=10496 必須仍被去重擋掉 */
	actuator_write(late_mid_silent, 256, &st_cycle);
	pthread_mutex_lock(&actuator.lock);
	assert(actuator.count == 2 && actuator.drops_dup == 1);
	actuator_submit(model_now + chunk_ticks, ticks_ms);
	assert(model_haptic_pcm_calls == pcm_before_m6 + 8 &&
	       model_haptic_pcm_last_bytes >= 232 && model_haptic_pcm_last_bytes <= 244);
	assert(actuator.count == 0);
	pthread_mutex_unlock(&actuator.lock);
	/* 回歸測試 6：三段連波每段 mHostTime 提早 1 tick（無 mSampleTime 備援路徑）應全部收齊並一次合併 */
	assert(interface.StopIO(drv, OBJECT_ACTUATOR, 1) == 0);
	assert(interface.StartIO(drv, OBJECT_ACTUATOR, 1) == 0);
	model_now += ticks_ms * 40;
	cycle.mOutputTime.mFlags = kAudioTimeStampHostTimeValid;
	cycle.mOutputTime.mHostTime = model_now;
	actuator_write(late_sliver, 256, &cycle);
	cycle.mOutputTime.mHostTime = model_now + chunk_ticks - 1;
	actuator_write(late_mid_active, 256, &cycle);
	/* 重送同 mHostTime 仍須被擋掉 */
	actuator_write(late_mid_active, 256, &cycle);
	cycle.mOutputTime.mHostTime = model_now + chunk_ticks * 2 - 2;
	actuator_write(late_chunk3, 256, &cycle);
	pthread_mutex_lock(&actuator.lock);
	assert(actuator.count == 3 && actuator.drops_dup == 1);
	actuator_submit(model_now + chunk_ticks * 2, ticks_ms);
	assert(model_haptic_pcm_calls == pcm_before_m6 + 9 &&
	       model_haptic_pcm_last_bytes >= 296 && model_haptic_pcm_last_bytes <= 304);
	assert(actuator.count == 0);
	pthread_mutex_unlock(&actuator.lock);
	/* 回歸測試 7：串流模式下 15 ms 寫入延遲仍被保留（<=25 ms），且逾時（>25 ms）才記 drops_late */
	model_now += ticks_ms * 60;
	unsigned late_stream_before = actuator.drops_late;
	cycle.mOutputTime.mFlags = kAudioTimeStampHostTimeValid;
	cycle.mOutputTime.mHostTime = model_now - ticks_ms * 20; /* end ≈ now - 14.7 ms */
	actuator_write(kbd_lead, 256, &cycle);
	assert(actuator.count == 1 && actuator.drops_late == late_stream_before);
	pthread_mutex_lock(&actuator.lock);
	actuator_submit(model_now, ticks_ms);
	pthread_mutex_unlock(&actuator.lock);
	assert(actuator.count == 0);
	cycle.mOutputTime.mHostTime = model_now - ticks_ms * 35; /* end ≈ now - 29.7 ms */
	actuator_write(kbd_lead, 256, &cycle);
	assert(actuator.count == 0 && actuator.drops_late == late_stream_before + 1);
 assert(interface.StopIO(drv, OBJECT_ACTUATOR, 1) == 0);
 assert(actuator.burst_scale_num == 0 && actuator.burst_scale_den == 0 && actuator.burst_last_end == 0);
 actuator.perceptual = false;
 actuator.stream = false;
 actuator.pcm = false;
 /* fuxihalhaptic：命令列優先；沒有時讀 ADT /chosen fuxi,hal-haptic（收 4-byte 的 1..6）。 */
 static const uint32_t one = 1, two = 2, three = 3, four = 4, five = 5, six = 6, seven = 7, zero = 0;
 static const uint64_t wide = 1;
 const struct __CFData adt1 = { 3, 4, (const uint8_t *)&one }, adt2 = { 3, 4, (const uint8_t *)&two };
 const struct __CFData adt3 = { 3, 4, (const uint8_t *)&three }, adt4 = { 3, 4, (const uint8_t *)&four };
 const struct __CFData adt5 = { 3, 4, (const uint8_t *)&five }, adt6 = { 3, 4, (const uint8_t *)&six };
 const struct __CFData adt7 = { 3, 4, (const uint8_t *)&seven }, adt0 = { 3, 4, (const uint8_t *)&zero };
 const struct __CFData adt_short = { 3, 2, (const uint8_t *)&one }, adt_wide = { 3, 8, (const uint8_t *)&wide };
 const struct __CFData adt_type = { 4, 4, (const uint8_t *)&one };
 assert(io_lookups == 2 && io_property_reads == 2 && io_releases == 2);   /* Initialize：fuxi,hal-haptic 與 fuxi,audio-voice */
 assert(select_case("", NULL, "ADT") == 0);
 assert(select_case("serial=3 fuxihalhaptic=1", NULL, "boot-args") == 1);
 assert(select_case("fuxihalhaptic=2 serial=3", &adt1, "boot-args") == 2);
 assert(select_case("fuxihalhaptic=3", &adt1, "boot-args") == 3);
 assert(select_case("fuxihalhaptic=4", &adt1, "boot-args") == 4);
 assert(select_case("fuxihalhaptic=5", &adt1, "boot-args") == 5);
 assert(select_case("fuxihalhaptic=6", &adt1, "boot-args") == 6);
 assert(select_case("serial=3", &adt1, "ADT") == 1);
 assert(select_case("serial=3", &adt2, "ADT") == 2);
 assert(select_case("serial=3", &adt3, "ADT") == 3);
 assert(select_case("serial=3", &adt4, "ADT") == 4);
 assert(select_case("serial=3", &adt5, "ADT") == 5);
 assert(select_case("serial=3", &adt6, "ADT") == 6);
 assert(select_case("serial=3", &adt7, "ADT") == 0);
 assert(select_case("serial=3", &adt0, "ADT") == 0);
 assert(select_case("serial=3", &adt_short, "ADT") == 0);
 assert(select_case("serial=3", &adt_wide, "ADT") == 0);
 assert(select_case("serial=3", &adt_type, "ADT") == 0);
 assert(select_case("xfuxihalhaptic=1 fuxihalhaptic=10 fuxihalhaptic=", &adt2, "ADT") == 2);
 model_bootargs_fail = true;
 assert(select_case("fuxihalhaptic=1", &adt2, "ADT") == 2);    /* 讀不到命令列也看 ADT */
 model_bootargs_fail = false;
 model_chosen = false;
 assert(select_case("serial=3", &adt1, "ADT") == 0);
 model_chosen = true;
 /* 即時喇叭串流模型：在未啟用時不動；啟用後送出音量與 PCM，不阻塞 IO 執行緒。 */
 assert(!speaker.enabled);
 model_speaker_live = true;
 speaker.enabled = true;
 assert(interface.StartIO(drv, OBJECT_SPEAKER, 1) == 0);
 assert((model_speaker_vol & 0x80000000) != 0);
 int16_t stereo[128 * 2];
 for (int i = 0; i < 128; i++) { stereo[i * 2] = 1200; stereo[i * 2 + 1] = -1200; }
 assert(interface.DoIOOperation(drv, OBJECT_SPEAKER, OBJECT_SPEAKER_OUTPUT, 1,
        kAudioServerPlugInIOOperationWriteMix, 128, &cycle, stereo, NULL) == 0);
 assert(speaker_drain_once());
 assert(model_speaker_pcm_calls == 1 && model_speaker_pcm_bytes == 128 * 4);
 assert(interface.StopIO(drv, OBJECT_SPEAKER, 1) == 0);
 assert((model_speaker_vol & 0x80000000) == 0);
 /* 多重 IO context 同時間軸混音：兩個 context 同寫 mSampleTime=1000，應疊加混音而非 2 倍速串接。 */
 assert(interface.StartIO(drv, OBJECT_SPEAKER, 1) == 0);
 int16_t toneA[1024 * 2], toneB[1024 * 2], silence[1024 * 2];
 for (int i = 0; i < 1024; i++) {
  toneA[i * 2] = 1200; toneA[i * 2 + 1] = -1200;
  toneB[i * 2] = 300; toneB[i * 2 + 1] = -300;
  silence[i * 2] = 0; silence[i * 2 + 1] = 0;
 }
 AudioServerPlugInIOCycleInfo sc = {.mOutputTime = {
  .mFlags = kAudioTimeStampSampleTimeValid | kAudioTimeStampHostTimeValid,
  .mSampleTime = 1000.0, .mHostTime = model_now
 }};
 assert(interface.DoIOOperation(drv, OBJECT_SPEAKER, OBJECT_SPEAKER_OUTPUT, 1,
        kAudioServerPlugInIOOperationWriteMix, 1024, &sc, toneA, NULL) == 0);
 assert(interface.DoIOOperation(drv, OBJECT_SPEAKER, OBJECT_SPEAKER_OUTPUT, 2,
        kAudioServerPlugInIOOperationWriteMix, 1024, &sc, silence, NULL) == 0);
 assert(interface.DoIOOperation(drv, OBJECT_SPEAKER, OBJECT_SPEAKER_OUTPUT, 3,
        kAudioServerPlugInIOOperationWriteMix, 1024, &sc, toneB, NULL) == 0);
 assert(speaker.overlaps == 2);
 /* 10 ms 預留窗及不足 960 幀（20 ms）時不提前拆成碎塊送出 */
 assert(!speaker_drain_once());
 model_now += ticks_ms * 20;
 assert(!speaker_drain_once());
 /* 再前進 10 ms（共 30 ms = 1440 frames -> clock_ready 960 frames）-> 送出整批 960 frames 並確認混音值 */
 model_now += ticks_ms * 10;
 size_t bytes_before = model_speaker_pcm_bytes;
 unsigned calls_before = model_speaker_pcm_calls;
 assert(speaker_drain_once());
 assert(model_speaker_pcm_calls - calls_before == 1);
 assert(model_speaker_pcm_bytes - bytes_before == 960 * 4);
 assert(model_speaker_pcm_last[0] == 1500 && model_speaker_pcm_last[1] == -1500);
 /* 過期寫入（sampleTime < next_send_sample）安全略過不破壞緩衝 */
 assert(interface.DoIOOperation(drv, OBJECT_SPEAKER, OBJECT_SPEAKER_OUTPUT, 4,
        kAudioServerPlugInIOOperationWriteMix, 256, &sc, toneB, NULL) == 0);
 assert(speaker.late == 1);
 /* StopIO 後沖出剩餘 64 frames，總輸出嚴格等於 1024 frames（1 倍速，非 3 倍速） */
 assert(interface.StopIO(drv, OBJECT_SPEAKER, 1) == 0);
 assert(speaker_drain_once());
 assert(model_speaker_pcm_bytes - bytes_before == 1024 * 4);
 assert(model_speaker_pcm_last[0] == 1500 && model_speaker_pcm_last[1] == -1500);
 assert(!speaker_drain_once());
 /* CoreAudio 預先緩衝 8192 幀（~170 ms）不得觸發 lead_dumps 或提前傾倒進核心 */
 assert(interface.StartIO(drv, OBJECT_SPEAKER, 1) == 0);
 for (int b = 0; b < 8; b++) {
  AudioServerPlugInIOCycleInfo pre = {.mOutputTime = {
   .mFlags = kAudioTimeStampSampleTimeValid | kAudioTimeStampHostTimeValid,
   .mSampleTime = 2000.0 + (double)(b * 1024), .mHostTime = model_now
  }};
  assert(interface.DoIOOperation(drv, OBJECT_SPEAKER, OBJECT_SPEAKER_OUTPUT, 1,
         kAudioServerPlugInIOOperationWriteMix, 1024, &pre, toneA, NULL) == 0);
 }
 assert(!speaker_drain_once());
 assert(speaker.lead_dumps == 0);
 assert(interface.StopIO(drv, OBJECT_SPEAKER, 1) == 0);
 speaker.enabled = false;
 /* 即時麥克風橋接模型：未啟用時維持靜音；啟用後 StartIO/StopIO 切換 kern.fuxiaudiomicctl 1/0 並回傳實音＋缺口補零。 */
 assert(!mic.enabled && model_mic_ctl_writes == 0);
 model_mic_live = true;
 mic.enabled = true; /* 手動驅動 mic_poll_once 避免測試競態 */
 assert(interface.StartIO(drv, OBJECT_CODEC, 1) == 0);
 assert(model_mic_wanted == 1 && model_mic_ctl_writes == 1 && mic.active);
 for (size_t i = 0; i < 96; i++) model_mic_pcm[i] = (int16_t)((i & 1) ? -1234 : 1234);
 model_mic_avail = 96;
 assert(mic_poll_once() == 96);
 assert(mic.live_frames == 96 && mic.peak == 1234);
 int16_t cap[128];
 memset(cap, 0x77, sizeof(cap));
 assert(interface.DoIOOperation(drv, OBJECT_CODEC, OBJECT_CODEC_INPUT, 1,
        kAudioServerPlugInIOOperationReadInput, 128, NULL, cap, NULL) == 0);
 for (int i = 0; i < 96; i++) assert(cap[i] == ((i & 1) ? -1234 : 1234));
 for (int i = 96; i < 128; i++) assert(cap[i] == 0);
 assert(mic.underruns == 1);
 assert(interface.StopIO(drv, OBJECT_CODEC, 1) == 0);
 assert(model_mic_wanted == 0 && model_mic_ctl_writes == 2 && !mic.active);
 mic.enabled = false;
 /* 停用狀態退回純靜音且不再碰 kern.fuxiaudiomicctl */
 assert(interface.StartIO(drv, OBJECT_CODEC, 1) == 0);
 assert(model_mic_ctl_writes == 2);
 memset(cap, 0x33, sizeof(cap));
 assert(interface.DoIOOperation(drv, OBJECT_CODEC, OBJECT_CODEC_INPUT, 1,
        kAudioServerPlugInIOOperationReadInput, 64, NULL, cap, NULL) == 0);
 for (int i = 0; i < 64; i++) assert(cap[i] == 0);
 assert(interface.StopIO(drv, OBJECT_CODEC, 1) == 0);
 /* 驗證 mic_initialize() 與啟動預緩衝等待：worker 在 StartIO 後即刻補滿 >= 1440 samples，首次 ReadInput 零 underrun、零開頭靜音 */
 model_mic_state = 1;
 model_mic_live = true;
 for (size_t i = 0; i < 1600; i++) model_mic_pcm[i] = (int16_t)(i + 100);
 model_mic_avail = 1600;
 mic_initialize();
 assert(mic.enabled && mic.worker_running);
 assert(interface.StartIO(drv, OBJECT_CODEC, 1) == 0);
 int16_t primed_cap[1024];
 memset(primed_cap, 0, sizeof(primed_cap));
 assert(interface.DoIOOperation(drv, OBJECT_CODEC, OBJECT_CODEC_INPUT, 1,
        kAudioServerPlugInIOOperationReadInput, 1024, NULL, primed_cap, NULL) == 0);
 assert(primed_cap[0] == 100 && primed_cap[1023] == 1123 && mic.underruns == 0);
 assert(interface.StopIO(drv, OBJECT_CODEC, 1) == 0);
 assert(mic.users == 0 && !mic.active);
 pthread_mutex_lock(&mic.lock);
 mic.enabled = false;
 pthread_cond_signal(&mic.ready);
 pthread_mutex_unlock(&mic.lock);
 usleep(10000);
 assert(!mic.worker_running);
 model_mic_live = false;

 /* ---- Voice multi-mic device (fuxi,audio-voice / fuxiaudiovoice=1..5) ---- */
 /* 預設（mode=0）時 Voice 相關物件對 HAL 完全不可見 */
 {
  AudioObjectPropertyAddress va_uid = { kAudioDevicePropertyDeviceUID, kAudioObjectPropertyScopeGlobal, 0 };
  UInt32 sz = 0;
  assert(!interface.HasProperty(drv, OBJECT_VOICE, 1, &va_uid));
  assert(interface.GetPropertyDataSize(drv, OBJECT_VOICE, 1, &va_uid, 0, NULL, &sz) == kAudioHardwareBadObjectError);
  assert(interface.GetPropertyDataSize(drv, OBJECT_VOICE_INPUT, 1, &fa, 0, NULL, &sz) == kAudioHardwareBadObjectError);
  assert(interface.GetPropertyDataSize(drv, OBJECT_VOICE_SOURCE, 1, &cur, 0, NULL, &sz) == kAudioHardwareBadObjectError);
 }
 /* voice_select_mode：boot-args 優先，其次 ADT fuxi,audio-voice（1..5） */
 {
  const char *vsrc = NULL;
  model_bootargs = "serial=3 fuxiaudiovoice=4"; model_audio_voice = &adt2;
  assert(voice_select_mode(&vsrc) == 4 && !strcmp(vsrc, "boot-args"));
  model_bootargs = "serial=3"; model_audio_voice = &adt5;
  assert(voice_select_mode(&vsrc) == 5 && !strcmp(vsrc, "ADT"));
  model_audio_voice = &adt6;
  assert(voice_select_mode(&vsrc) == 0);
  model_audio_voice = &adt0;
  assert(voice_select_mode(&vsrc) == 0);
  model_audio_voice = &adt_short;
  assert(voice_select_mode(&vsrc) == 0);
  model_audio_voice = &adt4;
  model_bootargs = "";
  assert(interface.Initialize(drv, &model_host_iface) == 0);
  assert(voice_mode == 4);
 }
 /* 啟用後回報 5 個裝置，UID "Voice" 對應 OBJECT_VOICE，且不當 default device */
 assert(list(OBJECT_PLUGIN, kAudioPlugInPropertyDeviceList, kAudioObjectPropertyScopeGlobal, ids, 8) == 5);
 assert(ids[4] == OBJECT_VOICE);
 {
  CFStringRef vuid = CFSTR("Voice");
  AudioObjectPropertyAddress ta = { kAudioPlugInPropertyTranslateUIDToDevice, kAudioObjectPropertyScopeGlobal, 0 };
  UInt32 dev = 0, sz = 0;
  assert(interface.GetPropertyData(drv, OBJECT_PLUGIN, 1, &ta, sizeof(vuid), &vuid, 4, &sz, &dev) == 0 && dev == OBJECT_VOICE);
  assert(get_u32(OBJECT_VOICE, kAudioDevicePropertyDeviceCanBeDefaultDevice, kAudioObjectPropertyScopeGlobal) == 0);
  assert(get_u32(OBJECT_VOICE, kAudioDevicePropertyDeviceCanBeDefaultDevice, kAudioObjectPropertyScopeInput) == 0);
  assert(list(OBJECT_VOICE, kAudioObjectPropertyOwnedObjects, kAudioObjectPropertyScopeGlobal, s, 4) == 2);
  assert(s[0] == OBJECT_VOICE_INPUT && s[1] == OBJECT_VOICE_SOURCE);
  UInt32 avail[8] = {0};
  assert(list(OBJECT_VOICE_SOURCE, kAudioSelectorControlPropertyAvailableItems, kAudioObjectPropertyScopeGlobal, avail, 8) == 4);
  assert(avail[0] == 'imic' && avail[1] == 'smic' && avail[2] == 'fmic' && avail[3] == 'lmic');
  UInt32 cur_items[8] = {0};
  assert(list(OBJECT_VOICE_SOURCE, kAudioSelectorControlPropertyCurrentItem, kAudioObjectPropertyScopeGlobal, cur_items, 8) == 4 &&
         cur_items[0] == 'imic' && cur_items[1] == 'smic' && cur_items[2] == 'fmic' && cur_items[3] == 'lmic');
  /* 寫入無效／重複／長度錯誤的資料來源必須拒絕 */
  UInt32 bad_id = 'tmic';
  UInt32 dup_ids[2] = { 'imic', 'imic' };
  UInt32 too_many[5] = { 'imic', 'smic', 'fmic', 'lmic', 'imic' };
  assert(interface.SetPropertyData(drv, OBJECT_VOICE_SOURCE, 1, &cur, 0, NULL, 4, &bad_id) == kAudioHardwareIllegalOperationError);
  assert(interface.SetPropertyData(drv, OBJECT_VOICE_SOURCE, 1, &cur, 0, NULL, 8, dup_ids) == kAudioHardwareIllegalOperationError);
  assert(interface.SetPropertyData(drv, OBJECT_VOICE_SOURCE, 1, &cur, 0, NULL, 3, &imic) == kAudioHardwareBadPropertySizeError);
  assert(interface.SetPropertyData(drv, OBJECT_VOICE_SOURCE, 1, &cur, 0, NULL, 20, too_many) == kAudioHardwareBadPropertySizeError);
  /* 切換成 3 麥克風陣列 {'smic', 'fmic', 'imic'}：格式同步變更為 3ch 並觸發 PropertiesChanged */
  AudioObjectPropertyAddress pfta_a = { kAudioStreamPropertyAvailablePhysicalFormats, kAudioObjectPropertyScopeGlobal, 0 };
  AudioObjectPropertyAddress sfma_a = { kAudioStreamPropertyAvailableVirtualFormats, kAudioObjectPropertyScopeGlobal, 0 };
  UInt32 pfta_sz = 0;
  assert(interface.GetPropertyDataSize(drv, OBJECT_VOICE_INPUT, 1, &pfta_a, 0, NULL, &pfta_sz) == 0 &&
         pfta_sz == 4 * sizeof(AudioStreamRangedDescription));
  AudioStreamRangedDescription avail_fmts[4];
  memset(avail_fmts, 0, sizeof(avail_fmts));
  assert(interface.GetPropertyData(drv, OBJECT_VOICE_INPUT, 1, &pfta_a, 0, NULL, sizeof(avail_fmts), &sz, avail_fmts) == 0 &&
         sz == sizeof(avail_fmts));
  for (UInt32 ch = 1; ch <= 4; ch++) {
   assert(avail_fmts[ch - 1].mFormat.mSampleRate == 48000 && avail_fmts[ch - 1].mFormat.mChannelsPerFrame == ch &&
          avail_fmts[ch - 1].mFormat.mBytesPerFrame == 2 * ch && avail_fmts[ch - 1].mFormat.mBitsPerChannel == 16);
  }
  /* 部分讀取 sfma 只取完整結構個數 */
  AudioStreamRangedDescription part_fmts[2];
  assert(interface.GetPropertyData(drv, OBJECT_VOICE_INPUT, 1, &sfma_a, 0, NULL, sizeof(part_fmts) + 8, &sz, part_fmts) == 0 &&
         sz == sizeof(part_fmts) && part_fmts[0].mFormat.mChannelsPerFrame == 1 && part_fmts[1].mFormat.mChannelsPerFrame == 2);
  prop_event_count = 0;
  UInt32 three_mics[3] = { 'smic', 'fmic', 'imic' };
  assert(interface.SetPropertyData(drv, OBJECT_VOICE_SOURCE, 1, &cur, 0, NULL, sizeof(three_mics), three_mics) == 0);
  assert(list(OBJECT_VOICE_SOURCE, kAudioSelectorControlPropertyCurrentItem, kAudioObjectPropertyScopeGlobal, cur_items, 8) == 3);
  assert(cur_items[0] == 'smic' && cur_items[1] == 'fmic' && cur_items[2] == 'imic');
  AudioStreamBasicDescription vf = {0};
  assert(interface.GetPropertyData(drv, OBJECT_VOICE_INPUT, 1, &fa, 0, NULL, sizeof(vf), &sz, &vf) == 0);
  assert(vf.mSampleRate == 48000 && vf.mChannelsPerFrame == 3 && vf.mBytesPerFrame == 6 && vf.mBitsPerChannel == 16);
  assert(interface.SetPropertyData(drv, OBJECT_VOICE_INPUT, 1, &fa, 0, NULL, sizeof(vf), &vf) == 0);
  bool saw_scci = false, saw_sfmt = false, saw_sfma = false;
  for (unsigned i = 0; i < prop_event_count; i++) {
   if (prop_events[i].obj == OBJECT_VOICE_SOURCE && prop_events[i].sel == kAudioSelectorControlPropertyCurrentItem) saw_scci = true;
   if (prop_events[i].obj == OBJECT_VOICE_INPUT && prop_events[i].sel == kAudioStreamPropertyVirtualFormat) saw_sfmt = true;
   if (prop_events[i].obj == OBJECT_VOICE_INPUT && prop_events[i].sel == kAudioStreamPropertyAvailableVirtualFormats) saw_sfma = true;
  }
  assert(saw_scci && saw_sfmt && saw_sfma);
  /* 同聲道數重排時不重發串流格式變更事件 */
  prop_event_count = 0;
  UInt32 three_mics2[3] = { 'smic', 'fmic', 'imic' };
  assert(interface.SetPropertyData(drv, OBJECT_VOICE_SOURCE, 1, &cur, 0, NULL, sizeof(three_mics2), three_mics2) == 0);
  for (unsigned i = 0; i < prop_event_count; i++) assert(prop_events[i].obj != OBJECT_VOICE_INPUT);
  /* 測試 5 種多聲道填入模式與 ADSP mic 共享參考計數（{'smic', 'fmic', 'imic'} -> imic=ch2, fmic=ch1） */
  model_mic_live = true;
  mic.enabled = true;
  unsigned ctl_before = model_mic_ctl_writes;
  assert(interface.StartIO(drv, OBJECT_VOICE, 1) == 0);
  assert(model_mic_wanted == 1 && model_mic_ctl_writes == ctl_before + 1 && mic.users == 1);
  assert(interface.StartIO(drv, OBJECT_CODEC, 1) == 0);
  assert(model_mic_ctl_writes == ctl_before + 1 && mic.users == 2);
  assert(interface.StopIO(drv, OBJECT_CODEC, 1) == 0);
  assert(model_mic_wanted == 1 && model_mic_ctl_writes == ctl_before + 1 && mic.users == 1);
  int16_t vout[16 * 3];
  for (unsigned m = 1; m <= 5; m++) {
   voice_mode = m;
   for (size_t i = 0; i < 8; i++) model_mic_pcm[i] = 4000;
   model_mic_avail = 8;
   assert(mic_poll_once() == 8);
   memset(vout, 0x55, sizeof(vout));
   assert(interface.DoIOOperation(drv, OBJECT_VOICE, OBJECT_VOICE_INPUT, 1,
          kAudioServerPlugInIOOperationReadInput, 8, NULL, vout, NULL) == 0);
   if (m == 1) assert(vout[0] == 4000 && vout[1] == 4000 && vout[2] == 4000);
   if (m == 2) assert(vout[0] == 0 && vout[1] == 0 && vout[2] == 4000);
   if (m == 3) assert(vout[0] == 0 && vout[1] == 4000 && vout[2] == 0);
   if (m == 4) assert(vout[0] == 1000 && vout[1] == 1000 && vout[2] == 4000);
   if (m == 5) assert(vout[0] == 0 && vout[1] == 4000 && vout[2] == 4000);
  }
  assert(interface.StopIO(drv, OBJECT_VOICE, 1) == 0);
  assert(model_mic_wanted == 0 && model_mic_ctl_writes == ctl_before + 2 && mic.users == 0 && !mic.active);
  mic.enabled = false;
  model_mic_live = false;
 }
 puts("audio model ok");
 return 0;
}
'''


def test_audio(tmp):
    inc = Path(tmp) / 'audio-include'
    for rel, text in (('CoreFoundation/CoreFoundation.h', CF_STUB), ('CoreFoundation/CFPlugInCOM.h', '#pragma once\n'),
                      ('CoreAudioTypes/CoreAudioTypes.h', AUDIO_TYPES_STUB), ('mach/mach_time.h', MACH_TIME_STUB),
                      ('mach/mach.h', MACH_STUB), ('sys/sysctl.h', AUDIO_SYSCTL_STUB)):
        (inc / rel).parent.mkdir(parents=True, exist_ok=True)
        (inc / rel).write_text(text)
    source = (TOOLS / 'fuxi-audio/FuxiAudio.c').read_text()
    assert source.startswith('/*\n * MIT License') and 'Ported from HackiOS hackios-xnu-userland userland/src/audio_plugin.c (MIT)' in source
    assert not re.search(r'(?<![A-Za-z_])(open|ioctl)\(', source), 'the plug-in must not touch device nodes'
    out = build_and_run(tmp, 'audio', '#include <assert.h>\n#include <stdio.h>\n' + source + AUDIO_TEST,
                        ['-I', str(inc), '-Wno-multichar', '-Wno-four-char-constants'])
    assert 'audio model ok' in out
    assert 'fuxi: audio: Speaker, Hawking, Actuator and Codec are with the HAL' in out
    assert 'fuxi: audio: the actuator plays: 64 frames, peak 1000' in out
    with open(TOOLS / 'fuxi-audio/Info.plist', 'rb') as f:
        info = plistlib.load(f)
    factory = info['CFPlugInTypes']['443ABAB8-E7B3-491A-B985-BEB9187030DB'][0]
    assert info['CFPlugInFactories'][factory] == 'FuxiAudio_Create' and info['CFBundleExecutable'] == 'FuxiAudio'
    print('PASS: FuxiAudio devices, formats, clock, HAP525 timing, scaling, silence and cancellation (host model)')


# ---- libfuxi-sb.dylib -------------------------------------------------------------------

OBJC_STUB = r'''
#pragma once
#include <stdbool.h>
typedef signed char BOOL;
typedef struct objc_object { struct objc_class *isa; BOOL opaque; } *id;
typedef struct objc_class *Class;
typedef const struct objc_selector *SEL;
typedef struct objc_method *Method;
typedef void (*IMP)(void);
Class objc_getClass(const char *name);
Method class_getClassMethod(Class cls, SEL sel);
Method class_getInstanceMethod(Class cls, SEL sel);
Class object_getClass(id object);
SEL sel_registerName(const char *name);
IMP method_setImplementation(Method method, IMP imp);
'''

SB_TEST = r'''
#include <assert.h>
struct objc_selector { const char *name; };
struct objc_method { SEL sel; IMP imp; };
struct objc_class { const char *name; struct objc_method cmethod, imethod; bool has_cmethod, has_imethod; };
static struct objc_selector sels[4] = { { "_outputFormatForSnapshot:" }, { "isImageOpaque" } };
static struct objc_class manifest = { .name = "XBApplicationSnapshotManifestImpl", .has_cmethod = true };
static struct objc_class snapshot_cls = { .name = "XBApplicationSnapshot", .has_imethod = true };
static struct objc_class plain_cls = { .name = "NSObject" };
static bool manifest_loaded;
static char bootargs[256];
static void (*add_image)(const struct mach_header *, intptr_t);
static long original(id self, SEL sel, id snap) { (void)self; (void)sel; (void)snap; return 1; }
Class objc_getClass(const char *name) { return manifest_loaded && !strcmp(name, manifest.name) ? &manifest : NULL; }
SEL sel_registerName(const char *name) { for (int i = 0; i < 2; i++) if (!strcmp(sels[i].name, name)) return &sels[i]; assert(0); }
Method class_getClassMethod(Class c, SEL s) { return c->has_cmethod && s == &sels[0] ? &c->cmethod : NULL; }
Method class_getInstanceMethod(Class c, SEL s) { return c->has_imethod && s == &sels[1] ? &c->imethod : NULL; }
Class object_getClass(id o) { return o->isa; }
IMP method_setImplementation(Method m, IMP imp) { IMP old = m->imp; m->imp = imp; return old; }
BOOL objc_msgSend(id self, SEL sel) { assert(sel == &sels[1]); return self->opaque; }
void _dyld_register_func_for_add_image(void (*f)(const struct mach_header *, intptr_t)) { add_image = f; f(NULL, 0); }
int sysctlbyname(const char *n, void *o, size_t *len, void *ni, size_t nl) {
 (void)ni; (void)nl; assert(!strcmp(n, "kern.bootargs"));
 size_t l = strlen(bootargs) + 1; if (l > *len) l = *len; memcpy(o, bootargs, l); *len = l; return 0;
}
static const char *progname = "SpringBoard";
const char *getprogname(void) { return progname; }
static long call(id snap) {
 return ((long (*)(id, SEL, id))manifest.cmethod.imp)(NULL, &sels[0], snap);
}
int main(void) {
 manifest.cmethod.imp = (IMP)original;
 /* another program: nothing touched */
 progname = "backboardd"; manifest_loaded = true;
 fuxi_sb_init();
 assert(manifest.cmethod.imp == (IMP)original && add_image == NULL);
 /* opt-out boot-arg */
 progname = "SpringBoard";
 strcpy(bootargs, "-v fuxisbjpeg=0 serial=3");
 fuxi_sb_init();
 assert(manifest.cmethod.imp == (IMP)original);
 strcpy(bootargs, "-v xfuxisbjpeg=0 fuxisbjpeg=01");     /* neither is the switch */
 assert(!boot_arg_off("fuxisbjpeg"));
 /* SplashBoard not loaded yet: hooked from the image callback */
 manifest_loaded = false;
 fuxi_sb_init();
 assert(add_image != NULL && manifest.cmethod.imp == (IMP)original && hooked == 0);
 manifest_loaded = true;
 add_image(NULL, 0);
 assert(hooked == 1 && manifest.cmethod.imp == (IMP)snapshot_output_format && original_output_format == (IMP)original);
 add_image(NULL, 0);                                     /* idempotent */
 assert(original_output_format == (IMP)original);
 struct objc_object opaque = { &snapshot_cls, 1 }, clear = { &snapshot_cls, 0 }, odd = { &plain_cls, 1 };
 assert(call(&opaque) == 3 && call(&clear) == 0 && call(&odd) == 0 && call(NULL) == 0);
 /* a build without the selector: left alone, said once */
 hooked = 0; manifest.has_cmethod = false;
 assert(try_hook() && hooked == -1);
 puts("sb model ok");
 return 0;
}
'''


def test_sb(tmp):
    inc = Path(tmp) / 'sb-include'
    stubs = {
        'objc/runtime.h': OBJC_STUB,
        'objc/message.h': '#pragma once\n#include <objc/runtime.h>\nBOOL objc_msgSend(id self, SEL sel);\n',
        'mach-o/dyld.h': '#pragma once\n#include <stdint.h>\nstruct mach_header;\n'
                         'void _dyld_register_func_for_add_image(void (*)(const struct mach_header *, intptr_t));\n',
        'sys/sysctl.h': '#pragma once\n#include <stddef.h>\nint sysctlbyname(const char *, void *, size_t *, void *, size_t);\n'
                        'const char *getprogname(void);\n#define __unused __attribute__((unused))\n',
    }
    for rel, text in stubs.items():
        (inc / rel).parent.mkdir(parents=True, exist_ok=True)
        (inc / rel).write_text(text)
    source = (TOOLS / 'fuxi-sb/FuxiSnapshots.c').read_text()
    assert 'Ported from HackiOS hackios-xnu-userland userland/src/inject_sb.c (MIT)' in source
    # The library's sysctl.h must come first so __unused is defined before use.
    source = source.replace('#include <mach-o/dyld.h>', '#include <sys/sysctl.h>\n#include <mach-o/dyld.h>', 1)
    # The model calls the constructor itself, once per scenario.
    assert source.count('__attribute__((constructor)) static void') == 1
    source = source.replace('__attribute__((constructor)) static void', 'static void')
    out = build_and_run(tmp, 'sb', source + SB_TEST, ['-I', str(inc)])
    assert 'sb model ok' in out
    assert 'fuxi: SpringBoard: application snapshots as JPEG (opaque) or PNG, not ATX' in out
    assert 'fuxi: SpringBoard: fuxisbjpeg=0, application snapshots unchanged' in out
    assert 'fuxi: SpringBoard: application snapshot as JPEG' in out and 'NOT changed' in out
    print('PASS: SpringBoard snapshot format hook, late SplashBoard, opt-out boot-arg (host model)')


# ---- mkadt-fuxi.py /product options -----------------------------------------------------

def synthetic_d22ap(path):
    def node(name, props, children=()):
        return mkadt.node(name, props, children)
    haptics = node('haptics', [('AAPL,phandle', struct.pack('<I', 0xd9)), ('closed-loop', struct.pack('<I', 1)),
                               ('supports-3rd-party-haptics', struct.pack('<I', 1))])
    haptics.props.append(('syscfg-placeholder', 1, b'syscfg/XXXX'))
    camera = node('camera', [('AAPL,phandle', struct.pack('<I', 0xd6)), ('flash', struct.pack('<I', 1))])
    product = node('product', [('AAPL,phandle', struct.pack('<I', 5)), ('product-name', b'iPhone X\0')],
                   [camera, haptics])
    root = node('device-tree', [('AAPL,phandle', struct.pack('<I', 1))], [product])
    path.write_bytes(adt.serialize(root))


def test_adt(tmp):
    d22ap = Path(tmp) / 'd22ap.adt'
    synthetic_d22ap(d22ap)
    spec = importlib.util.spec_from_file_location('fuxi_adt', TOOLS / 'mkadt-fuxi.py')
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    saved = mkadt.d22ap_product.__defaults__
    mkadt.d22ap_product.__defaults__ = (str(d22ap),)
    try:
        plain = m.build()
        product = next(c for c in plain.children if c.name == 'product')
        assert product.children == [] and product.prop('product-name') == b'iPhone X\0'
        before = adt.serialize(plain)
        assert adt.serialize(m.add_product_children(plain, False, False, d22ap)) == before   # default unchanged
        root = m.add_product_children(m.build(), True, True, d22ap)
        root, _ = adt.parse(adt.serialize(root))
        product = next(c for c in root.children if c.name == 'product')
        kids = {c.name: c for c in product.children}
        assert sorted(kids) == ['camera', 'haptics']
        assert kids['haptics'].prop('closed-loop') == struct.pack('<I', 1)
        assert kids['haptics'].prop('supports-3rd-party-haptics') == struct.pack('<I', 1)
        assert kids['haptics'].prop('syscfg-placeholder') is None
        assert [k for k, _, _ in kids['camera'].props] == ['name', 'AAPL,phandle']   # empty: no flash
        phandles = []

        def walk(n):
            value = n.prop('AAPL,phandle')
            if value:
                phandles.append(struct.unpack('<I', value)[0])
            for c in n.children:
                walk(c)
        walk(root)
        assert len(phandles) == len(set(phandles))
        # A template run (existing ADT) replaces rather than duplicates.
        again = m.add_product_children(root, True, False, d22ap)
        assert [c.name for c in next(c for c in again.children if c.name == 'product').children].count('haptics') == 1
        try:
            m.add_product_children(m.build(), True, False, Path(tmp) / 'missing.adt')
            raise AssertionError('missing D22AP tree accepted')
        except OSError:
            pass
        # disp0 display options: absent by default, replaced (not duplicated) on rebuild.
        plain = m.build(); before = adt.serialize(plain)
        assert adt.serialize(m.set_display_options(plain)) == before
        root = m.set_display_options(m.set_display_options(m.build(), 120000, False), 120800, True)
        root, _ = adt.parse(adt.serialize(root))
        disp0 = next(c for c in next(c for c in root.children if c.name == 'arm-io').children if c.name == 'disp0')
        assert disp0.prop('fuxi,refresh-mhz') == struct.pack('<I', 120800) and disp0.prop('fuxi,swap-hist') == struct.pack('<I', 1)
        assert [k for k, _, _ in disp0.props].count('fuxi,refresh-mhz') == 1 and disp0.prop('canvas-width') == struct.pack('<I', 1125)
        for bad in (60000, 240001):
            try:
                m.set_display_options(m.build(), bad)
                raise AssertionError('bad refresh accepted')
            except ValueError:
                pass
        mkadt.d22ap_product.__defaults__ = (str(Path(tmp) / 'absent.adt'),)
        try:
            m.add_product_children(m.build(), False, True, d22ap)
            raise AssertionError('ADT without /product accepted')
        except ValueError:
            pass
    finally:
        mkadt.d22ap_product.__defaults__ = saved
    print('PASS: mkadt-fuxi /product/haptics, /product/camera and disp0 display options (off by default)')


# ---- fuxi-sys-root.py -------------------------------------------------------------------

def synthetic_root(base):
    jobs = {
        '/System/Library/LaunchDaemons/com.apple.SpringBoard.plist': {
            'Label': 'com.apple.SpringBoard', 'EnvironmentVariables': {'CA_ASSERT_MAIN_THREAD_TRANSACTIONS': '1'}},
        '/System/Library/LaunchDaemons/com.apple.nanotimekitcompaniond.plist': {'Label': 'com.apple.nanotimekitcompaniond'},
        '/System/Library/LaunchDaemons/com.apple.homed.plist': {'Label': 'com.apple.homed'},
        '/System/Library/LaunchDaemons/com.apple.dmd.plist': {'Label': 'com.apple.dmd', 'Disabled': True},
        '/System/Library/LaunchDaemons/com.apple.mediaserverd.plist': {'Label': 'com.apple.mediaserverd'},
    }
    cache = base / 'System/Library/xpc/launchd.plist'
    cache.parent.mkdir(parents=True)
    cache.write_bytes(plistlib.dumps({'LaunchDaemons': jobs, 'VersionNumber': 1}, fmt=plistlib.FMT_BINARY))
    va = base / 'Library/Audio/Plug-Ins/HAL.disabled/VirtualAudio.plugin'
    va.mkdir(parents=True)
    (va / 'VirtualAudio').write_bytes(b'x')
    (base / 'usr/local').mkdir(parents=True)
    bundle = base.parent / 'FuxiAudio.driver'
    bundle.mkdir()
    (bundle / 'FuxiAudio').write_bytes(b'\xcf\xfa\xed\xfe')
    os.chmod(bundle / 'FuxiAudio', 0o755)
    (bundle / '_CodeSignature').mkdir()
    (bundle / '_CodeSignature/CodeResources').write_bytes(b'<plist/>')
    with open(TOOLS / 'fuxi-audio/Info.plist', 'rb') as f:
        (bundle / 'Info.plist').write_bytes(f.read())
    dylib = base.parent / 'libfuxi-sb.dylib'
    dylib.write_bytes(b'\xcf\xfa\xed\xfe')
    return cache, bundle, dylib


def run_root(*args):
    return subprocess.run([sys.executable, str(TOOLS / 'fuxi-sys-root.py'), *map(str, args)],
                          check=True, capture_output=True, text=True).stdout


def load(path):
    with open(path, 'rb') as f:
        return plistlib.load(f)


def test_root(tmp):
    base = Path(tmp) / 'root' / 'mnt'
    base.mkdir(parents=True)
    cache, bundle, dylib = synthetic_root(base)
    original = cache.read_bytes()
    # dry run writes nothing
    out = run_root('--dry-run', '--audio-plugin', bundle, '--restore-virtualaudio', '--sb-snapshots', dylib,
                   '--quiet-jobs', 'observed,hackios', base)
    assert 'would: install FuxiAudio.driver' in out and 'would: com.apple.homed: Disabled=true' in out
    assert cache.read_bytes() == original and not (base / 'System/Library/Audio').exists()
    assert not (base / 'usr/local/lib').exists()
    # apply
    out = run_root('--audio-plugin', bundle, '--restore-virtualaudio', '--sb-snapshots', dylib,
                   '--quiet-jobs', 'observed,hackios', base)
    driver = base / 'System/Library/Audio/Plug-Ins/HAL/FuxiAudio.driver'
    assert (driver / 'FuxiAudio').stat().st_mode & 0o777 == 0o755
    assert (driver / 'Info.plist').stat().st_mode & 0o777 == 0o644
    assert (driver / '_CodeSignature/CodeResources').is_file()
    assert (base / 'Library/Audio/Plug-Ins/HAL/VirtualAudio.plugin/VirtualAudio').is_file()
    assert (base / 'usr/local/lib/fuxi/libfuxi-sb.dylib').stat().st_mode & 0o777 == 0o755
    jobs = {j['Label']: j for j in load(cache)['LaunchDaemons'].values()}
    env = jobs['com.apple.SpringBoard']['EnvironmentVariables']
    assert env == {'CA_ASSERT_MAIN_THREAD_TRANSACTIONS': '1', 'DYLD_INSERT_LIBRARIES': '/usr/local/lib/fuxi/libfuxi-sb.dylib'}
    assert jobs['com.apple.nanotimekitcompaniond']['Disabled'] and jobs['com.apple.nanotimekitcompaniond']['_FuxiSysDisabled']
    assert jobs['com.apple.homed']['_FuxiSysDisabled'] and '_FuxiSysDisabled' not in jobs['com.apple.dmd']
    assert 'Disabled' not in jobs['com.apple.mediaserverd']
    assert 'com.apple.routined: no such job, skipped' in out and 'com.apple.dmd: already disabled, left alone' in out
    # idempotent
    snapshot = cache.read_bytes()
    out = run_root('--sb-snapshots', dylib, '--quiet-jobs', 'observed', '--restore-virtualaudio', base)
    assert 'already has' in out and 'already disabled by this tool' in out and 'already moved' in out
    assert cache.read_bytes() == snapshot
    # another library already inserted is kept on undo
    data = load(cache)
    data['LaunchDaemons']['/System/Library/LaunchDaemons/com.apple.SpringBoard.plist']['EnvironmentVariables'][
        'DYLD_INSERT_LIBRARIES'] = '/other.dylib:/usr/local/lib/fuxi/libfuxi-sb.dylib'
    cache.write_bytes(plistlib.dumps(data, fmt=plistlib.FMT_BINARY))
    run_root('--no-sb-snapshots', '--unquiet-jobs', '--remove-audio-plugin', '--disable-virtualaudio', base)
    jobs = {j['Label']: j for j in load(cache)['LaunchDaemons'].values()}
    assert jobs['com.apple.SpringBoard']['EnvironmentVariables']['DYLD_INSERT_LIBRARIES'] == '/other.dylib'
    assert 'Disabled' not in jobs['com.apple.nanotimekitcompaniond'] and 'Disabled' not in jobs['com.apple.homed']
    assert jobs['com.apple.dmd']['Disabled'] is True
    assert not any('_FuxiSysDisabled' in j for j in jobs.values())
    assert not driver.exists() and not (base / 'usr/local/lib/fuxi/libfuxi-sb.dylib').exists()
    assert (base / 'Library/Audio/Plug-Ins/HAL.disabled/VirtualAudio.plugin/VirtualAudio').is_file()
    # a full undo of an untouched job list restores the original job contents
    run_root('--no-sb-snapshots', base)
    jobs = {j['Label']: j for j in load(cache)['LaunchDaemons'].values()}
    assert 'DYLD_INSERT_LIBRARIES' in jobs['com.apple.SpringBoard']['EnvironmentVariables']   # /other.dylib stays
    bad = subprocess.run([sys.executable, str(TOOLS / 'fuxi-sys-root.py'), '--quiet-jobs', 'nope', str(base)],
                         capture_output=True, text=True)
    assert bad.returncode != 0 and 'unknown preset' in bad.stderr
    print('PASS: fuxi-sys-root.py install/undo, dry run, idempotence, job presets (synthetic root)')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--audio-only', action='store_true', help='run the HAL implementation and root bundle tests')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='fuxi-sys-test-') as tmp:
        if not args.audio_only:
            test_kernel(tmp)
        test_audio(tmp)
        if not args.audio_only:
            test_sb(tmp)
            test_adt(tmp)
        test_root(tmp)


if __name__ == '__main__':
    main()
