/*
 * MIT License
 *
 * Copyright (c) 2026 OOSEX GANG
 * Copyright (c) 2026 ios-fuxi contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
/*
 * Ported from HackiOS hackios-xnu-userland userland/src/audio_plugin.c (MIT).
 *
 * FuxiAudio.driver: the audio devices iOS expects of an iPhone X, for mediaserverd's HAL.
 *
 * The iPhone's own speaker, codec and microphones reach the HAL through IOAudio2 kernel drivers,
 * none of which exist here; a HAL plug-in in /System/Library/Audio/Plug-Ins/HAL (an
 * AudioServerPlugIn: one COM-style table of functions; the iOS SDK has no header for it, so what is
 * needed of it is declared below) can stand in for them. The virtual audio device
 * (/Library/Audio/Plug-Ins/HAL/VirtualAudio.plugin), through which everything in iOS is routed,
 * knows an iPhone's devices by their UIDs and builds its ports from them. Without them it finds
 * "no speaker device", and here it crashed mediaserverd (ApplyBeamFormerPreference) until it was
 * disabled (tools/sweet-root-tweaks.py). This presents the four devices HackiOS found it needs:
 *
 *   "Speaker"   a stereo output and a stereo input that is always silent
 *   "Hawking"   one silent input and nothing else: the virtual audio device will not build the
 *               speaker route without its mandatory companion of type 'vhaw' (the iPhone X's
 *               routing tables), whose only port is this input; without it everything stays on
 *               the null port
 *   "Actuator"  one output of one channel: the Taptic Engine, on which iOS plays haptics as
 *               waveforms (only with /product/haptics closed-loop)
 *   "Codec"     one input of one channel with the data source 'imic': the built-in microphone
 *
 * The Actuator can drive HAP525 with fuxihalhaptic=1 and the existing fuxi,haptic runtime.
 * When the boot-args lack fuxihalhaptic, ADT /chosen fuxi,hal-haptic (u32 1 or 2) selects the
 * same mode, so a full 607-byte command line can still turn it on. Speaker output is counted and inputs supply silence while the AudioReach path is being built
 * (docs-lanes/audio.md). The device's clock is the system's: it claims
 * to play exactly FUXI_AUDIO_RATE frames a second of mach_absolute_time(), and the HAL's I/O thread
 * paces itself by that. Installed only by tools/fuxi-sys-root.py --audio-plugin (off by default).
 */
#include <CoreFoundation/CoreFoundation.h>
#include <CoreFoundation/CFPlugInCOM.h>
#include <CoreAudioTypes/CoreAudioTypes.h>
#include <errno.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>
#include <time.h>
#include <unistd.h>

/* The one format: 16-bit signed samples at 48 kHz; the speaker is stereo. */
#define FUXI_AUDIO_RATE         48000
#define FUXI_AUDIO_CHANNELS     2

/* ---- the AudioServerPlugIn interface (macOS: CoreAudio/AudioServerPlugIn.h, AudioHardwareBase.h) ---- */

typedef UInt32 AudioObjectID;
typedef struct {
	UInt32 mSelector, mScope, mElement;
} AudioObjectPropertyAddress;
typedef struct {
	AudioStreamBasicDescription mFormat;
	AudioValueRange mSampleRateRange;
} AudioStreamRangedDescription;
typedef struct {
	UInt32 mClientID;
	pid_t mProcessID;
	Boolean mIsNativeEndian;
	CFStringRef mBundleID;
} AudioServerPlugInClientInfo;
typedef struct {
	UInt64 mIOCycleCounter;
	UInt32 mNominalIOBufferFrameSize;
	AudioTimeStamp mCurrentTime, mInputTime, mOutputTime;
	Float64 mMasterHostTicksPerFrame, mDeviceHostTicksPerFrame;
} AudioServerPlugInIOCycleInfo;

typedef struct AudioServerPlugInHostInterface AudioServerPlugInHostInterface;
typedef const AudioServerPlugInHostInterface *AudioServerPlugInHostRef;
struct AudioServerPlugInHostInterface {
	OSStatus (*PropertiesChanged)(AudioServerPlugInHostRef, AudioObjectID, UInt32, const AudioObjectPropertyAddress *);
	OSStatus (*CopyFromStorage)(AudioServerPlugInHostRef, CFStringRef, CFPropertyListRef *);
	OSStatus (*WriteToStorage)(AudioServerPlugInHostRef, CFStringRef, CFPropertyListRef);
	OSStatus (*DeleteFromStorage)(AudioServerPlugInHostRef, CFStringRef);
	OSStatus (*RequestDeviceConfigurationChange)(AudioServerPlugInHostRef, AudioObjectID, UInt64, void *);
};

typedef struct AudioServerPlugInDriverInterface AudioServerPlugInDriverInterface;
typedef AudioServerPlugInDriverInterface **AudioServerPlugInDriverRef;
struct AudioServerPlugInDriverInterface {
	void *_reserved;
	HRESULT (*QueryInterface)(void *, REFIID, LPVOID *);
	ULONG (*AddRef)(void *);
	ULONG (*Release)(void *);
	OSStatus (*Initialize)(AudioServerPlugInDriverRef, AudioServerPlugInHostRef);
	OSStatus (*CreateDevice)(AudioServerPlugInDriverRef, CFDictionaryRef, const AudioServerPlugInClientInfo *, AudioObjectID *);
	OSStatus (*DestroyDevice)(AudioServerPlugInDriverRef, AudioObjectID);
	OSStatus (*AddDeviceClient)(AudioServerPlugInDriverRef, AudioObjectID, const AudioServerPlugInClientInfo *);
	OSStatus (*RemoveDeviceClient)(AudioServerPlugInDriverRef, AudioObjectID, const AudioServerPlugInClientInfo *);
	OSStatus (*PerformDeviceConfigurationChange)(AudioServerPlugInDriverRef, AudioObjectID, UInt64, void *);
	OSStatus (*AbortDeviceConfigurationChange)(AudioServerPlugInDriverRef, AudioObjectID, UInt64, void *);
	Boolean (*HasProperty)(AudioServerPlugInDriverRef, AudioObjectID, pid_t, const AudioObjectPropertyAddress *);
	OSStatus (*IsPropertySettable)(AudioServerPlugInDriverRef, AudioObjectID, pid_t, const AudioObjectPropertyAddress *, Boolean *);
	OSStatus (*GetPropertyDataSize)(AudioServerPlugInDriverRef, AudioObjectID, pid_t, const AudioObjectPropertyAddress *, UInt32,
	    const void *, UInt32 *);
	OSStatus (*GetPropertyData)(AudioServerPlugInDriverRef, AudioObjectID, pid_t, const AudioObjectPropertyAddress *, UInt32,
	    const void *, UInt32, UInt32 *, void *);
	OSStatus (*SetPropertyData)(AudioServerPlugInDriverRef, AudioObjectID, pid_t, const AudioObjectPropertyAddress *, UInt32,
	    const void *, UInt32, const void *);
	OSStatus (*StartIO)(AudioServerPlugInDriverRef, AudioObjectID, UInt32);
	OSStatus (*StopIO)(AudioServerPlugInDriverRef, AudioObjectID, UInt32);
	OSStatus (*GetZeroTimeStamp)(AudioServerPlugInDriverRef, AudioObjectID, UInt32, Float64 *, UInt64 *, UInt64 *);
	OSStatus (*WillDoIOOperation)(AudioServerPlugInDriverRef, AudioObjectID, UInt32, UInt32, Boolean *, Boolean *);
	OSStatus (*BeginIOOperation)(AudioServerPlugInDriverRef, AudioObjectID, UInt32, UInt32, UInt32,
	    const AudioServerPlugInIOCycleInfo *);
	OSStatus (*DoIOOperation)(AudioServerPlugInDriverRef, AudioObjectID, AudioObjectID, UInt32, UInt32, UInt32,
	    const AudioServerPlugInIOCycleInfo *, void *, void *);
	OSStatus (*EndIOOperation)(AudioServerPlugInDriverRef, AudioObjectID, UInt32, UInt32, UInt32,
	    const AudioServerPlugInIOCycleInfo *);
};

#define kAudioServerPlugInTypeUUID CFUUIDGetConstantUUIDWithBytes(NULL, 0x44, 0x3A, 0xBA, 0xB8, 0xE7, 0xB3, 0x49, 0x1A, \
	    0xB9, 0x85, 0xBE, 0xB9, 0x18, 0x70, 0x30, 0xDB)
#define kAudioServerPlugInDriverInterfaceUUID CFUUIDGetConstantUUIDWithBytes(NULL, 0xEE, 0xA5, 0x77, 0x3D, 0xCC, 0x43, \
	    0x49, 0xF1, 0x8E, 0x00, 0x8F, 0x96, 0xE7, 0xD2, 0x3B, 0x17)

enum {
	kAudioHardwareUnknownPropertyError = 'who?', kAudioHardwareBadPropertySizeError = '!siz',
	kAudioHardwareIllegalOperationError = 'nope', kAudioHardwareBadObjectError = '!obj',
	kAudioHardwareUnsupportedOperationError = 'unop', kAudioDeviceUnsupportedFormatError = '!dat',

	kAudioObjectPropertyScopeGlobal = 'glob', kAudioObjectPropertyScopeInput = 'inpt', kAudioObjectPropertyScopeOutput = 'outp',

	kAudioObjectClassID = 'aobj', kAudioPlugInClassID = 'aplg', kAudioDeviceClassID = 'adev', kAudioStreamClassID = 'astr',

	kAudioObjectPropertyBaseClass = 'bcls', kAudioObjectPropertyClass = 'clas', kAudioObjectPropertyOwner = 'stdv',
	kAudioObjectPropertyName = 'lnam', kAudioObjectPropertyManufacturer = 'lmak', kAudioObjectPropertyOwnedObjects = 'ownd',

	kAudioPlugInPropertyBoxList = 'box#', kAudioPlugInPropertyTranslateUIDToBox = 'uidb',
	kAudioPlugInPropertyDeviceList = 'dev#', kAudioPlugInPropertyTranslateUIDToDevice = 'uidd',
	kAudioPlugInPropertyResourceBundle = 'rsrc',

	kAudioDevicePropertyDeviceUID = 'uid ', kAudioDevicePropertyModelUID = 'muid', kAudioDevicePropertyTransportType = 'tran',
	kAudioDevicePropertyRelatedDevices = 'akin', kAudioDevicePropertyClockDomain = 'clkd',
	kAudioDevicePropertyDeviceIsAlive = 'livn', kAudioDevicePropertyDeviceIsRunning = 'goin',
	kAudioDevicePropertyDeviceCanBeDefaultDevice = 'dflt', kAudioDevicePropertyDeviceCanBeDefaultSystemDevice = 'sflt',
	kAudioDevicePropertyLatency = 'ltnc', kAudioDevicePropertyStreams = 'stm#', kAudioObjectPropertyControlList = 'ctrl',
	kAudioDevicePropertySafetyOffset = 'saft', kAudioDevicePropertyNominalSampleRate = 'nsrt',
	kAudioDevicePropertyAvailableNominalSampleRates = 'nsr#', kAudioDevicePropertyIsHidden = 'hidn',
	kAudioDevicePropertyPreferredChannelsForStereo = 'dch2', kAudioDevicePropertyZeroTimeStampPeriod = 'ring',
	kAudioDevicePropertyDataSource = 'ssrc',
	kAudioSelectorControlClassID = 'slct', kAudioDataSourceControlClassID = 'dsrc',
	kAudioSelectorControlPropertyCurrentItem = 'scci', kAudioSelectorControlPropertyAvailableItems = 'scai',
	kAudioSelectorControlPropertyItemName = 'scin', kAudioSelectorControlPropertyItemKind = 'clkk',

	kAudioStreamPropertyIsActive = 'sact', kAudioStreamPropertyDirection = 'sdir', kAudioStreamPropertyTerminalType = 'term',
	kAudioStreamPropertyStartingChannel = 'schn', kAudioStreamPropertyLatency = 'ltnc',
	kAudioStreamPropertyVirtualFormat = 'sfmt', kAudioStreamPropertyAvailableVirtualFormats = 'sfma',
	kAudioStreamPropertyPhysicalFormat = 'pft ', kAudioStreamPropertyAvailablePhysicalFormats = 'pfta',

	kAudioStreamTerminalTypeSpeaker = 'spkr', kAudioStreamTerminalTypeMicrophone = 'micr',

	kAudioServerPlugInIOOperationReadInput = 'read', kAudioServerPlugInIOOperationWriteMix = 'rite',

	kAudioLevelControlClassID = 'levl', kAudioVolumeControlClassID = 'vlme',
	kAudioControlPropertyScope = 'cscp', kAudioControlPropertyElement = 'celm',
	kAudioLevelControlPropertyScalarValue = 'lcsv', kAudioLevelControlPropertyDecibelValue = 'lcdv',
	kAudioLevelControlPropertyDecibelRange = 'lcdr', kAudioLevelControlPropertyConvertScalarToDecibels = 'lcsd',
	kAudioLevelControlPropertyConvertDecibelsToScalar = 'lcds',
};

/* ---- the devices ---------------------------------------------------------------------------------- */

/*
 * Object numbers are ours to choose. A device's streams follow it: its output (if it has one),
 * then its input.
 */
enum {
	OBJECT_PLUGIN = 1,
	OBJECT_SPEAKER, OBJECT_SPEAKER_OUTPUT, OBJECT_SPEAKER_INPUT,
	OBJECT_HAWKING, OBJECT_HAWKING_INPUT,
	OBJECT_ACTUATOR, OBJECT_ACTUATOR_OUTPUT,
	OBJECT_CODEC, OBJECT_CODEC_INPUT,
	OBJECT_VOICE, OBJECT_VOICE_INPUT,
	OBJECT_SPEAKER_VOLUME,          /* the speaker output's volume control */
	OBJECT_CODEC_SOURCE,            /* the codec input's data source control: its microphones */
	OBJECT_VOICE_SOURCE,            /* the voice input's data source control: multi-mic array for VoiceChat/VoLTE */
	OBJECT_LAST = OBJECT_VOICE_SOURCE
};

#define VOLUME_RANGE_DB         48.0    /* 3 dB for each of iOS's sixteen steps */

struct device {
	AudioObjectID object;
	CFStringRef uid;                /* the name the virtual audio device knows it by */
	AudioObjectID output, input;    /* its streams; 0 = none */
	UInt32 output_channels, input_channels;
	/* its clock, running while anybody does I/O */
	unsigned running;               /* how many clients have started I/O */
	UInt64 anchor;                  /* mach_absolute_time() when the first of them did */
	UInt64 periods;                 /* whole timestamp periods since */
};

static struct device devices[] = {
	{ .object = OBJECT_SPEAKER, .uid = CFSTR("Speaker"), .output = OBJECT_SPEAKER_OUTPUT, .input = OBJECT_SPEAKER_INPUT,
	  .output_channels = FUXI_AUDIO_CHANNELS, .input_channels = 2 },
	{ .object = OBJECT_HAWKING, .uid = CFSTR("Hawking"), .input = OBJECT_HAWKING_INPUT, .input_channels = 1 },
	{ .object = OBJECT_ACTUATOR, .uid = CFSTR("Actuator"), .output = OBJECT_ACTUATOR_OUTPUT, .output_channels = 1 },
	{ .object = OBJECT_CODEC, .uid = CFSTR("Codec"), .input = OBJECT_CODEC_INPUT, .input_channels = 1 },
	{ .object = OBJECT_VOICE, .uid = CFSTR("Voice"), .input = OBJECT_VOICE_INPUT, .input_channels = 4 },
};
#define DEVICES                 (sizeof(devices) / sizeof(devices[0]))
#define VOICE_DEVICE_INDEX      (DEVICES - 1)
#define VOICE_MAX_SOURCES       4

/*
 * Multi-mic Voice device (UID "Voice") required by VirtualAudio.plugin
 * (Device_Voice_Aspen / RoutingHandler_PlaybackAndRecord_GenericConfig1.cpp:173)
 * whenever VoiceChat / VoLTE (Category='cpar', Mode='rmvc') needs >2 microphones
 * (Device_Codec_Aspen::mMaxCodecMics == 2). Gated by boot-arg fuxiaudiovoice=1..5
 * or ADT /chosen fuxi,audio-voice (u32 1..5), default 0 (off) so baseline 4-device
 * boots stay unchanged.
 *
 * Start at 4 channels ({'imic', 'smic', 'fmic', 'lmic'}) from boot so CoreAudio's
 * HALS_Device / HALS_IOContext allocates a native 4-channel input stream buffer
 * without requiring a dynamic 1ch->4ch structural reconfiguration on first VoLTE route.
 *
 * Fill modes when expanding the single live ADSP bottom mic into N active channels:
 *   1 = full live mic on all active channels
 *   2 = full live mic on 'imic' only (fallback ch 0), silence on others
 *   3 = full live mic on 'fmic' only (fallback ch 0), silence on others
 *   4 = full live mic on 'imic' (fallback ch 0), attenuated (>>2, -12 dB) on others
 *   5 = full live mic on 'imic' and 'fmic' (fallback ch 0), silence on 'smic'/'lmic'
 */
static const UInt32 voice_avail_sources[VOICE_MAX_SOURCES] = { 'imic', 'smic', 'fmic', 'lmic' };
static UInt32 voice_sources[VOICE_MAX_SOURCES] = { 'imic', 'smic', 'fmic', 'lmic' };
static unsigned voice_source_count = VOICE_MAX_SOURCES;
static unsigned voice_mode;
static uint32_t voice_rt_cfg = VOICE_MAX_SOURCES | (0u << 8) | (2u << 16); /* ch=4 imic=0 fmic=2 */
static UInt64 voice_frames;
static UInt64 voice_io_calls;
static UInt64 voice_out_nz;
static UInt32 voice_first_frames;
static unsigned voice_out_peak;

static inline unsigned
active_devices(void)
{
	return voice_mode ? (unsigned)DEVICES : (unsigned)(DEVICES - 1);
}

static inline bool
voice_object_hidden(AudioObjectID object)
{
	return !voice_mode &&
	    (object == OBJECT_VOICE || object == OBJECT_VOICE_INPUT || object == OBJECT_VOICE_SOURCE);
}

static uint32_t
voice_pack_rt_cfg(const UInt32 *srcs, unsigned count)
{
	unsigned imic_idx = 0xffu, fmic_idx = 0xffu;
	for (unsigned i = 0; i < count && i < VOICE_MAX_SOURCES; i++) {
		if (srcs[i] == 'imic' && imic_idx == 0xffu) {
			imic_idx = i;
		}
		if (srcs[i] == 'fmic' && fmic_idx == 0xffu) {
			fmic_idx = i;
		}
	}
	return (count & 0xffu) | ((imic_idx & 0xffu) << 8) | ((fmic_idx & 0xffu) << 16);
}

#define TRANSPORT_BUILT_IN      'bltn'
/*
 * The codec's input has data sources, one for each microphone, and the virtual audio device will not
 * take a codec without them (VirtualAudio asks for kAudioDevicePropertyDataSources on its input, and
 * keeps those it knows: 'imic', 'smic', 'fmic', 'lmic', 'tmic', 'umic', 'imi1' to 'imi4'). A plug-in
 * has them as a data source control on the input, which the HAL answers those properties from. There
 * is one microphone here, at the bottom: 'imic', the iPhone's internal (bottom) one.
 */
#define MIC_SOURCE              'imic'
#define MIC_SOURCE_BACK         'smic'
#define MIC_SOURCE_FRONT        'fmic'
#define MIC_SOURCE_COMPAT       'bmic'
#define MANUFACTURER            CFSTR("fuxi")
/* how often a device says where its clock is, in frames: also the largest I/O buffer the HAL will use */
#define TIMESTAMP_PERIOD        16384

static AudioServerPlugInHostRef host;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static Float64 ticks_per_frame;
static UInt64 written;                  /* frames since the speaker's I/O started (all discarded) */
static Boolean inactive[OBJECT_LAST + 1];       /* streams the HAL has switched off */
static Float32 volume = 1.0f;           /* the speaker's volume control, 0 to 1 (iOS sets it once it routes there) */
static UInt32 codec_sources[4] = { MIC_SOURCE };
static unsigned codec_source_count = 1;
static unsigned codec_source_said;

static void say(const char *format, ...);

/* HAP525 的 DIRECT_PLAY 以晶片共振頻率工作，依 10 ms PCM 視窗的振幅調整馬達輸出。 */
enum {
	ACTUATOR_WINDOW = FUXI_AUDIO_RATE / 100,
	ACTUATOR_QUEUE_SIZE = 64,
	ACTUATOR_MAX_LEVEL = 128,
	ACTUATOR_SLACK_MS = 5,
	ACTUATOR_PEAK_FLOOR = 32,
	ACTUATOR_WRITE_LATE_GRACE_MS = 8,
	ACTUATOR_STREAM_WRITE_LATE_GRACE_MS = 25,
	ACTUATOR_SUBMIT_LATE_GRACE_MS = 40,
	ACTUATOR_LOW_ONSET_LEVEL = 16,
	ACTUATOR_ONSET_HOLD_MS = 6,
	ACTUATOR_PCM_BYTES = 376,
	ACTUATOR_PCM_LIMIT = 96,
	ACTUATOR_STREAM_PCM_LIMIT = 126,
	ACTUATOR_STREAM_ENV_RESET_MS = 18,
	ACTUATOR_STREAM_LEAD_MS = 35,
	ACTUATOR_STREAM_PRELOAD_MS = 2,
	ACTUATOR_STREAM5_COALESCE_MARGIN_MS = 2,
	ACTUATOR_STREAM5_COALESCE_MIN_MS = 7,
	ACTUATOR_STREAM5_COALESCE_ARRIVAL_MS = 8,
	ACTUATOR_STREAM6_COALESCE_MARGIN_MS = 2,
	ACTUATOR_STREAM6_COALESCE_MIN_MS = 7,
	ACTUATOR_STREAM6_COALESCE_ARRIVAL_MS = 13,
	ACTUATOR_STREAM6_COALESCE_CHUNK3_ARRIVAL_MS = 12,
	ACTUATOR_STREAM6_TAIL_ACTIVE_SAMPLES = 16,
	ACTUATOR_STREAM_MAX_QUEUE_MS = 85,
	ACTUATOR_LINEAR_GAIN_NUM = 176,
	ACTUATOR_LINEAR_FRESH_MIN_PEAK = 3,
	ACTUATOR_LINEAR_CONT_MIN_PEAK = 2,
};

struct actuator_pulse {
	UInt64 start, end, enqueued;
	unsigned level;
	unsigned pcm_length;
	unsigned raw24_len;
	uint8_t pcm[ACTUATOR_PCM_BYTES];
	int16_t raw24[ACTUATOR_PCM_BYTES];
};

static struct {
	pthread_mutex_t lock;
	pthread_cond_t ready;
	struct actuator_pulse queue[ACTUATOR_QUEUE_SIZE];
	UInt64 accepted_until, playing_until, burst_end;
	uint64_t accepted_sample_until;
	UInt64 stream_last_end, stream_playing_until, burst_last_end;
	unsigned head, count, submitted, errors, stream_env, diag_logged;
	unsigned drops_lock, drops_dup, drops_late, drops_full;
	unsigned burst_scale_num, burst_scale_den;
	bool enabled, active, rtp, pcm, stream, linear, perceptual;
	bool accepted_sample_valid;
} actuator = {.lock = PTHREAD_MUTEX_INITIALIZER, .ready = PTHREAD_COND_INITIALIZER};

enum {
	/* Sample-time additive mixer (reference: UniXNU-sheng hal/ShengAudioPlugIn.c):
	 * iOS mediaserverd can run multiple concurrent IO contexts on Speaker for the same
	 * mSampleTime; summing into a sample-time-indexed ring and draining at 1x device clock
	 * prevents 2x frame duplication and silent-context interleaving. */
	SPEAKER_RING_FRAMES = 32768,
	SPEAKER_RING_MASK = SPEAKER_RING_FRAMES - 1,
	SPEAKER_CHUNK_FRAMES = 960,       /* 20 ms, matches kernel TDM period (960 frames) */
	SPEAKER_MIN_CHUNK_FRAMES = 960,   /* pace active stream in 20 ms chunks; avoid tiny syscall spin */
	SPEAKER_MIX_HOLD_FRAMES = 480,    /* 10 ms mix-assembly window for concurrent contexts */
	SPEAKER_MIX_LEAD_CAP = 24576,     /* 512 ms cap (> TIMESTAMP_PERIOD 341 ms) before shedding excess */
};

static struct {
	pthread_mutex_t lock;
	pthread_cond_t ready;
	int16_t ring[SPEAKER_RING_FRAMES * 2];
	uint64_t next_send_sample, max_written_sample;
	uint64_t origin_sample, origin_host;
	uint64_t sent_frames;
	unsigned writes, errors, overlaps, late, lead_dumps;
	bool enabled, active, logged_first, timeline_valid, paced;
} speaker = {.lock = PTHREAD_MUTEX_INITIALIZER, .ready = PTHREAD_COND_INITIALIZER};

static void
speaker_push_volume(Float32 vol, bool active)
{
	if (!speaker.enabled) {
		return;
	}
	uint32_t milli = vol <= 0.0f ? 0u : vol >= 1.0f ? 1000u : (uint32_t)(vol * 1000.0f + 0.5f);
	int packed = (int)(milli | (active ? 0x80000000u : 0u));
	(void)sysctlbyname("kern.fuxiaudiovol", NULL, NULL, &packed, sizeof(packed));
}

static int16_t
speaker_sat16(int32_t value)
{
	return value > 32767 ? 32767 : value < -32768 ? -32768 : (int16_t)value;
}

static bool
speaker_drain_once(void)
{
	int16_t chunk[SPEAKER_CHUNK_FRAMES * 2];
	UInt32 frames = 0;
	uint64_t now = mach_absolute_time();

	pthread_mutex_lock(&speaker.lock);
	if (!speaker.enabled || !speaker.timeline_valid) {
		pthread_mutex_unlock(&speaker.lock);
		return false;
	}
	uint64_t ready_until = speaker.max_written_sample;
	if (speaker.active && speaker.paced && ticks_per_frame > 0.0) {
		uint64_t elapsed = now > speaker.origin_host
		    ? (uint64_t)((Float64)(now - speaker.origin_host) / ticks_per_frame) : 0;
		uint64_t clock_ready = elapsed > SPEAKER_MIX_HOLD_FRAMES
		    ? speaker.origin_sample + (elapsed - SPEAKER_MIX_HOLD_FRAMES) : speaker.origin_sample;
		uint64_t backlog = speaker.max_written_sample > speaker.next_send_sample
		    ? speaker.max_written_sample - speaker.next_send_sample : 0;
		if (backlog > SPEAKER_MIX_LEAD_CAP) {
			uint64_t lead_ready = speaker.max_written_sample - SPEAKER_MIX_LEAD_CAP;
			if (lead_ready > clock_ready) {
				clock_ready = lead_ready;
				speaker.lead_dumps++;
			}
		}
		if (clock_ready < ready_until) {
			ready_until = clock_ready;
		}
	}
	if (ready_until > speaker.next_send_sample) {
		uint64_t avail = ready_until - speaker.next_send_sample;
		if (speaker.active && speaker.paced && avail < SPEAKER_MIN_CHUNK_FRAMES) {
			pthread_mutex_unlock(&speaker.lock);
			return false;
		}
		frames = avail > SPEAKER_CHUNK_FRAMES ? SPEAKER_CHUNK_FRAMES : (UInt32)avail;
		uint64_t pos = speaker.next_send_sample;
		for (UInt32 i = 0; i < frames; i++) {
			UInt32 slot = (UInt32)((pos + i) & SPEAKER_RING_MASK);
			chunk[i * 2] = speaker.ring[slot * 2];
			chunk[i * 2 + 1] = speaker.ring[slot * 2 + 1];
			speaker.ring[slot * 2] = 0;
			speaker.ring[slot * 2 + 1] = 0;
		}
		speaker.next_send_sample = pos + frames;
		speaker.sent_frames += frames;
	}
	pthread_mutex_unlock(&speaker.lock);
	if (!frames) {
		return false;
	}
	if (sysctlbyname("kern.fuxiaudiopcm", NULL, NULL, chunk, (size_t)frames * 4u) == 0) {
		speaker.writes++;
		if (!speaker.logged_first) {
			speaker.logged_first = true;
			say("fuxi: audio: Speaker live PCM active (first %u frames)\n", (unsigned)frames);
		}
		return true;
	}
	if (errno == ENOTSUP || errno == ENOENT) {
		pthread_mutex_lock(&speaker.lock);
		speaker.enabled = false;
		pthread_mutex_unlock(&speaker.lock);
		say("fuxi: audio: Speaker live PCM unavailable (errno=%d); discarding\n", errno);
		return false;
	}
	if (errno == EAGAIN) {
		usleep(20000);
		return false;
	}
	speaker.errors++;
	return false;
}

static void *
speaker_worker(__unused void *context)
{
	for (;;) {
		pthread_mutex_lock(&speaker.lock);
		while (speaker.enabled &&
		    (!speaker.timeline_valid || speaker.next_send_sample >= speaker.max_written_sample)) {
			(void)pthread_cond_wait(&speaker.ready, &speaker.lock);
		}
		bool alive = speaker.enabled;
		bool active = speaker.active;
		pthread_mutex_unlock(&speaker.lock);
		if (!alive) {
			return NULL;
		}
		if (!speaker_drain_once() && active) {
			usleep(4000);
		}
	}
}

static void
speaker_initialize(void)
{
	int probe = 0;
	size_t size = sizeof(probe);
	if (speaker.enabled || sysctlbyname("kern.fuxiaudiovol", &probe, &size, NULL, 0) != 0) {
		return;
	}
	pthread_t worker;
	speaker.enabled = pthread_create(&worker, NULL, speaker_worker, NULL) == 0;
	if (speaker.enabled) {
		(void)pthread_detach(worker);
		speaker_push_volume(volume, false);
	}
	say("fuxi: audio: Speaker live PCM bridge %s\n", speaker.enabled ? "enabled" : "failed");
}

static void
speaker_write(const int16_t *samples, UInt32 frames, const AudioServerPlugInIOCycleInfo *cycle)
{
	if (!speaker.enabled || !samples || !frames) {
		return;
	}
	if (pthread_mutex_trylock(&speaker.lock)) {
		return;
	}
	uint64_t now = mach_absolute_time();
	if (frames > SPEAKER_RING_FRAMES) {
		samples += (frames - SPEAKER_RING_FRAMES) * 2u;
		frames = SPEAKER_RING_FRAMES;
	}
	bool has_sample_time = cycle != NULL &&
	    (cycle->mOutputTime.mFlags & kAudioTimeStampSampleTimeValid) != 0 &&
	    cycle->mOutputTime.mSampleTime >= 0.0;
	uint64_t sample_time = has_sample_time
	    ? (uint64_t)(cycle->mOutputTime.mSampleTime + 0.5)
	    : speaker.max_written_sample;
	if (!speaker.timeline_valid) {
		memset(speaker.ring, 0, sizeof(speaker.ring));
		speaker.next_send_sample = sample_time;
		speaker.max_written_sample = sample_time;
		speaker.origin_sample = sample_time;
		speaker.origin_host = now;
		speaker.paced = has_sample_time;
		speaker.timeline_valid = true;
	}
	if (sample_time < speaker.next_send_sample) {
		uint64_t skip = speaker.next_send_sample - sample_time;
		speaker.late++;
		if (skip >= frames) {
			pthread_mutex_unlock(&speaker.lock);
			return;
		}
		samples += skip * 2u;
		sample_time += skip;
		frames -= (UInt32)skip;
	}
	if (sample_time + frames > speaker.next_send_sample + SPEAKER_RING_FRAMES) {
		memset(speaker.ring, 0, sizeof(speaker.ring));
		speaker.next_send_sample = sample_time;
		speaker.max_written_sample = sample_time;
		speaker.origin_sample = sample_time;
		speaker.origin_host = now;
		speaker.paced = has_sample_time;
	}
	if (has_sample_time && sample_time < speaker.max_written_sample) {
		speaker.overlaps++;
	}
	for (UInt32 i = 0; i < frames; i++) {
		UInt32 slot = (UInt32)((sample_time + i) & SPEAKER_RING_MASK);
		speaker.ring[slot * 2] = speaker_sat16((int32_t)speaker.ring[slot * 2] + (int32_t)samples[i * 2]);
		speaker.ring[slot * 2 + 1] = speaker_sat16((int32_t)speaker.ring[slot * 2 + 1] + (int32_t)samples[i * 2 + 1]);
	}
	if (sample_time + frames > speaker.max_written_sample) {
		speaker.max_written_sample = sample_time + frames;
	}
	pthread_cond_signal(&speaker.ready);
	pthread_mutex_unlock(&speaker.lock);
}

static UInt64
actuator_ticks_ms(void)
{
	return (UInt64)(ticks_per_frame * (FUXI_AUDIO_RATE / 1000));
}

static void
actuator_wait(UInt64 ticks)
{
	UInt64 ns = (UInt64)((Float64)ticks / ticks_per_frame * (1e9 / FUXI_AUDIO_RATE));
	const struct timespec delay = {(time_t)(ns / 1000000000u), (long)(ns % 1000000000u)};
	(void)pthread_cond_timedwait_relative_np(&actuator.ready, &actuator.lock, &delay);
}

/*
 * 連續串流模式 6（搭配核心 fuxi,haptic=9、fuxi,haptic-damp=0x005/0x006）的感知增益曲線：
 * 先在 16-bit 24 kHz 域合併相鄰週期並裁除前導靜音，再以單次突發一致增益線性壓縮至 ±126，
 * 拉開滾輪≈1100->30、鍵盤≈6400->58、音量≈8000->64、開關／切換器≈10500->75、
 * 控制中心≈16384->98、桌面長按≈23000->119 的五段動態層次，避免輕重回饋擠在同檔。
 */
static unsigned
actuator_perceptual_target(unsigned peak16)
{
	static const struct { unsigned in, out; } curve[] = {
		{ACTUATOR_PEAK_FLOOR, 16},
		{512,   24},
		{1536,  34},
		{3584,  46},
		{6400,  58},
		{8400,  66},
		{11500, 80},
		{16384, 98},
		{22528, 118},
		{26624, ACTUATOR_STREAM_PCM_LIMIT},
		{32768, ACTUATOR_STREAM_PCM_LIMIT},
	};
	if (peak16 < ACTUATOR_PEAK_FLOOR) {
		return 0;
	}
	if (peak16 >= 26624u) {
		return ACTUATOR_STREAM_PCM_LIMIT;
	}
	for (unsigned i = 1; i < sizeof(curve) / sizeof(curve[0]); i++) {
		if (peak16 <= curve[i].in) {
			unsigned x0 = curve[i - 1].in, x1 = curve[i].in;
			unsigned y0 = curve[i - 1].out, y1 = curve[i].out;
			return y0 + ((peak16 - x0) * (y1 - y0) + (x1 - x0) / 2u) / (x1 - x0);
		}
	}
	return ACTUATOR_STREAM_PCM_LIMIT;
}

/* actuator.lock 已持有，且目前視窗已到播放時間。 */
static void
actuator_submit(UInt64 now, UInt64 ticks_ms)
{
	struct actuator_pulse pulse = actuator.queue[actuator.head];
	actuator.head = (actuator.head + 1) % ACTUATOR_QUEUE_SIZE;
	actuator.count--;
	unsigned merged_chunks = 1, diag_raw_len = 0, diag_pk16 = 0, diag_pk8 = 0;
	unsigned diag_wait_ms = now > pulse.enqueued && ticks_ms ? (unsigned)((now - pulse.enqueued) / ticks_ms) : 0u;
	if (actuator.perceptual && pulse.raw24_len) {
		bool fresh = !actuator.burst_last_end ||
		    pulse.start > actuator.burst_last_end + ACTUATOR_STREAM_ENV_RESET_MS * ticks_ms;
		bool trimmed_first = false;
		if (fresh) {
			unsigned lead_idx = 0;
			while (lead_idx < pulse.raw24_len) {
				int val = pulse.raw24[lead_idx];
				unsigned mag = (unsigned)(val < 0 ? -val : val);
				if (mag >= ACTUATOR_PEAK_FLOOR) {
					break;
				}
				lead_idx++;
			}
			if (lead_idx < pulse.raw24_len) {
				if (lead_idx > 2u) {
					unsigned trim = lead_idx - 2u;
					memmove(pulse.raw24, pulse.raw24 + trim,
					    (pulse.raw24_len - trim) * sizeof(pulse.raw24[0]));
					pulse.raw24_len -= trim;
				}
				trimmed_first = true;
			}
		}
		while (actuator.count) {
			const struct actuator_pulse *next = &actuator.queue[actuator.head];
			if (!next->raw24_len || next->start > pulse.end + 2 * ticks_ms ||
			    pulse.raw24_len + next->raw24_len > ACTUATOR_PCM_BYTES) {
				break;
			}
			memcpy(pulse.raw24 + pulse.raw24_len, next->raw24,
			    next->raw24_len * sizeof(pulse.raw24[0]));
			pulse.raw24_len += next->raw24_len;
			if (next->end > pulse.end) {
				pulse.end = next->end;
			}
			if (next->level > pulse.level) {
				pulse.level = next->level;
			}
			actuator.head = (actuator.head + 1) % ACTUATOR_QUEUE_SIZE;
			actuator.count--;
			merged_chunks++;
		}
		unsigned start_idx = 0;
		if (fresh && !trimmed_first) {
			while (start_idx < pulse.raw24_len) {
				int val = pulse.raw24[start_idx];
				unsigned mag = (unsigned)(val < 0 ? -val : val);
				if (mag >= ACTUATOR_PEAK_FLOOR) {
					break;
				}
				start_idx++;
			}
			if (start_idx >= pulse.raw24_len) {
				return;
			}
			if (start_idx > 2u) {
				start_idx -= 2u;
			} else {
				start_idx = 0;
			}
		}
		unsigned len = pulse.raw24_len - start_idx;
		unsigned peak16 = 0;
		for (unsigned i = 0; i < len; i++) {
			int val = pulse.raw24[start_idx + i];
			unsigned mag = (unsigned)(val < 0 ? -val : val);
			if (mag > peak16) peak16 = mag;
		}
		if (fresh) {
			unsigned target = actuator_perceptual_target(peak16);
			if (!target || !peak16) {
				return;
			}
			actuator.burst_scale_num = target;
			actuator.burst_scale_den = peak16;
		} else if (!actuator.burst_scale_den || peak16 > actuator.burst_scale_den) {
			unsigned target = actuator_perceptual_target(peak16);
			if (!target || !peak16) {
				return;
			}
			actuator.burst_scale_num = target;
			actuator.burst_scale_den = peak16;
		}
		unsigned out_peak = 0;
		int64_t num = (int64_t)actuator.burst_scale_num;
		int64_t den = (int64_t)actuator.burst_scale_den;
		for (unsigned i = 0; i < len; i++) {
			int64_t prod = (int64_t)pulse.raw24[start_idx + i] * num;
			int sample = (int)((prod + (prod >= 0 ? den / 2 : -den / 2)) / den);
			if (sample > ACTUATOR_STREAM_PCM_LIMIT) sample = ACTUATOR_STREAM_PCM_LIMIT;
			if (sample < -ACTUATOR_STREAM_PCM_LIMIT) sample = -ACTUATOR_STREAM_PCM_LIMIT;
			pulse.pcm[i] = (uint8_t)(int8_t)sample;
			unsigned mag = (unsigned)(sample < 0 ? -sample : sample);
			if (mag > out_peak) out_peak = mag;
		}
		if (!out_peak) {
			return;
		}
		diag_raw_len = len;
		diag_pk16 = peak16;
		diag_pk8 = out_peak;
		unsigned padded = (len + 3u) & ~3u;
		if (padded > ACTUATOR_PCM_BYTES) padded = ACTUATOR_PCM_BYTES;
		if (padded > len) {
			memset(pulse.pcm + len, 0, padded - len);
		}
		pulse.pcm_length = padded;
		actuator.burst_last_end = pulse.end;
	}
	while (actuator.linear && pulse.pcm_length && actuator.count) {
		const struct actuator_pulse *next = &actuator.queue[actuator.head];
		if (!next->pcm_length || next->start > pulse.end + 2 * ticks_ms ||
		    pulse.pcm_length + next->pcm_length > ACTUATOR_PCM_BYTES) {
			break;
		}
		memcpy(pulse.pcm + pulse.pcm_length, next->pcm, next->pcm_length);
		pulse.pcm_length += next->pcm_length;
		if (next->end > pulse.end) {
			pulse.end = next->end;
		}
		if (next->level > pulse.level) {
			pulse.level = next->level;
		}
		actuator.head = (actuator.head + 1) % ACTUATOR_QUEUE_SIZE;
		actuator.count--;
	}
	unsigned ms;
	if (now >= pulse.end) {
		if (now - pulse.end > ACTUATOR_SUBMIT_LATE_GRACE_MS * ticks_ms) {
			actuator.drops_late++;
			return;
		}
		UInt64 dur = pulse.end > pulse.start ? pulse.end - pulse.start : ticks_ms;
		ms = (unsigned)((dur + ticks_ms - 1) / ticks_ms);
		if (ms > 20) {
			ms = 20;
		}
	} else {
		ms = (unsigned)((pulse.end - now + ticks_ms - 1) / ticks_ms);
	}
	if (actuator.count && actuator.queue[actuator.head].start == pulse.end) {
		ms += ACTUATOR_SLACK_MS;
	}
	if (!actuator.pcm && !actuator.rtp && actuator.count) {
		const struct actuator_pulse *next = &actuator.queue[actuator.head];
		if (next->start <= pulse.end + 2 * ticks_ms && next->level > pulse.level &&
		    (!actuator.playing_until || now >= actuator.playing_until)) {
			pulse.level = next->level;
		}
	}
	int request = (int)((pulse.level << 16) | ms);
	bool do_pcm = actuator.pcm && pulse.pcm_length;
	bool do_stream = actuator.stream;
	bool do_rtp = !do_pcm && actuator.rtp;
	if (do_rtp) {
		/* Multiple mix windows can describe one short haptic. Use host time so
		 * scheduler jitter cannot turn contiguous windows into duplicate taps. */
		bool onset = !actuator.burst_end || pulse.start > actuator.burst_end + 10 * ticks_ms;
		actuator.burst_end = pulse.end;
		if (!onset) return;
	}
	/* sysctl 期間放開 actuator.lock，避免 CoreAudio 即時 I/O 執行緒 trylock 失敗漏掉震動。 */
	pthread_mutex_unlock(&actuator.lock);
	bool pcm_ok = false, pcm_enotsup = false, pcm_err = false;
	bool used_rtp = false, rtp_disable = false, rtp_err = false;
	bool timed_tried = false, timed_ok = false;
	if (do_pcm) {
		if (sysctlbyname("kern.fuxihapticpcm", NULL, NULL, pulse.pcm, pulse.pcm_length) == 0) {
			pcm_ok = true;
		} else if (errno == ENOTSUP) {
			pcm_enotsup = true;
		} else {
			pcm_err = true;
		}
	}
	if (!pcm_ok && !pcm_err) {
		if (do_rtp) {
			int click = 0x01000000;
			if (sysctlbyname("kern.fuxivibrate", NULL, NULL, &click, sizeof(click)) == 0) {
				used_rtp = true;
			} else if (errno == ENOTSUP || errno == EINVAL) {
				rtp_disable = true;
			} else {
				rtp_err = true;
			}
		}
		if (!used_rtp && !rtp_err) {
			timed_tried = true;
			timed_ok = sysctlbyname("kern.fuxivibrate", NULL, NULL, &request, sizeof(request)) == 0;
		}
	}
	pthread_mutex_lock(&actuator.lock);
	if (pcm_ok) {
		if (do_stream && actuator.active) {
			UInt64 span = ((UInt64)pulse.pcm_length * ticks_ms + 23u) / 24u;
			UInt64 play_base = actuator.stream_playing_until > now ? actuator.stream_playing_until : now;
			if (play_base > now + ACTUATOR_STREAM_MAX_QUEUE_MS * ticks_ms) {
				play_base = now + ACTUATOR_STREAM_MAX_QUEUE_MS * ticks_ms;
			}
			UInt64 play_end = play_base + span;
			if (pulse.end > play_end) {
				play_end = pulse.end;
			}
			if (play_end > actuator.stream_playing_until) {
				actuator.stream_playing_until = play_end;
			}
		}
		unsigned prev_submitted = actuator.submitted++;
		unsigned log_kind = 0;
		if (prev_submitted == 0) {
			if (actuator.perceptual) {
				if (actuator.diag_logged < 32u) actuator.diag_logged++;
				log_kind = 1;
			} else {
				log_kind = 2;
			}
		} else if (actuator.perceptual && actuator.diag_logged < 32u) {
			actuator.diag_logged++;
			log_kind = 3;
		}
		unsigned d_lock = __atomic_load_n(&actuator.drops_lock, __ATOMIC_RELAXED);
		unsigned d_dup = actuator.drops_dup, d_late = actuator.drops_late, d_full = actuator.drops_full;
		if (log_kind) {
			pthread_mutex_unlock(&actuator.lock);
			if (log_kind == 1) {
				say("fuxi: audio: Actuator HAP525 first PCM packet bytes=%u stream=%u merged=%u raw=%u pk16=%u pk8=%u wait_ms=%u drops=%u/%u/%u/%u\n",
				    pulse.pcm_length, (unsigned)do_stream, merged_chunks, diag_raw_len, diag_pk16, diag_pk8, diag_wait_ms,
				    d_lock, d_dup, d_late, d_full);
			} else if (log_kind == 2) {
				say("fuxi: audio: Actuator HAP525 first PCM packet bytes=%u stream=%u\n",
				    pulse.pcm_length, (unsigned)do_stream);
			} else {
				say("fuxi: audio: Actuator HAP525 PCM bytes=%u stream=%u merged=%u raw=%u pk16=%u pk8=%u wait_ms=%u drops=%u/%u/%u/%u\n",
				    pulse.pcm_length, (unsigned)do_stream, merged_chunks, diag_raw_len, diag_pk16, diag_pk8, diag_wait_ms,
				    d_lock, d_dup, d_late, d_full);
			}
			pthread_mutex_lock(&actuator.lock);
		}
		return;
	}
	if (pcm_err) {
		actuator.stream_playing_until = 0;
		actuator.errors++;
		return;
	}
	if (pcm_enotsup) {
		actuator.pcm = false;
		actuator.stream = false;
		actuator.stream_playing_until = 0;
		say("fuxi: audio: Actuator PCM unavailable; using timed pulses\n");
	}
	if (rtp_disable) {
		actuator.rtp = false;
		say("fuxi: audio: Actuator RTP unavailable; using timed pulses\n");
	}
	if (rtp_err) {
		actuator.errors++;
		return;
	}
	if (used_rtp || (timed_tried && timed_ok)) {
		if (actuator.active) {
			actuator.playing_until = used_rtp ? 0 : now + ms * ticks_ms;
		}
		if (actuator.submitted++ == 0) {
			say("fuxi: audio: Actuator HAP525 first pulse ms=%u level=%u RTP=%u\n", ms, pulse.level, used_rtp);
		}
	} else if (timed_tried) {
		actuator.errors++;
	}
}

static unsigned
actuator_raw24_active_len(const struct actuator_pulse *pulse)
{
	for (unsigned idx = 0; idx < pulse->raw24_len; idx++) {
		int val = pulse->raw24[idx];
		unsigned mag = (unsigned)(val < 0 ? -val : val);
		if (mag >= ACTUATOR_PEAK_FLOOR) {
			unsigned trim = idx > 2u ? idx - 2u : 0u;
			return pulse->raw24_len - trim;
		}
	}
	return 0;
}

static bool
actuator_raw24_tail_active(const struct actuator_pulse *pulse)
{
	unsigned start_idx = pulse->raw24_len > ACTUATOR_STREAM6_TAIL_ACTIVE_SAMPLES
	    ? pulse->raw24_len - ACTUATOR_STREAM6_TAIL_ACTIVE_SAMPLES : 0u;
	for (unsigned idx = start_idx; idx < pulse->raw24_len; idx++) {
		int val = pulse->raw24[idx];
		unsigned mag = (unsigned)(val < 0 ? -val : val);
		if (mag >= ACTUATOR_PEAK_FLOOR) {
			return true;
		}
	}
	return false;
}

static UInt64
actuator_fire_at(const struct actuator_pulse *head, bool stream_cont, UInt64 ticks_ms)
{
	if (!stream_cont && actuator.perceptual &&
	    head->raw24_len > 0 && head->raw24_len <= 128) {
		unsigned head_active = actuator_raw24_active_len(head);
		unsigned eff_head_len = head_active ? head_active : head->raw24_len;
		if (actuator.count == 1) {
			if (actuator_raw24_tail_active(head) &&
			    eff_head_len + 128u <= ACTUATOR_PCM_BYTES) {
				UInt64 coalesce = head->end + ACTUATOR_STREAM6_COALESCE_MARGIN_MS * ticks_ms;
				UInt64 floor = head->start + ACTUATOR_STREAM6_COALESCE_MIN_MS * ticks_ms;
				UInt64 arrival = head->enqueued + ACTUATOR_STREAM6_COALESCE_ARRIVAL_MS * ticks_ms;
				if (floor > coalesce) {
					coalesce = floor;
				}
				return arrival > coalesce ? arrival : coalesce;
			}
		} else if (actuator.count == 2) {
			const struct actuator_pulse *second =
			    &actuator.queue[(actuator.head + 1) % ACTUATOR_QUEUE_SIZE];
			if (second->raw24_len > 0 && second->raw24_len <= 128 &&
			    second->start <= head->end + 2 * ticks_ms &&
			    eff_head_len + second->raw24_len + 128u <= ACTUATOR_PCM_BYTES &&
			    actuator_raw24_tail_active(second)) {
				UInt64 coalesce = second->end + ACTUATOR_STREAM6_COALESCE_MARGIN_MS * ticks_ms;
				UInt64 arrival = second->enqueued + ACTUATOR_STREAM6_COALESCE_CHUNK3_ARRIVAL_MS * ticks_ms;
				return arrival > coalesce ? arrival : coalesce;
			}
		}
	}
	if (!stream_cont && actuator.linear && actuator.count == 1 &&
	    head->pcm_length <= 128) {
		UInt64 coalesce = head->end + ACTUATOR_STREAM5_COALESCE_MARGIN_MS * ticks_ms;
		UInt64 floor = head->start + ACTUATOR_STREAM5_COALESCE_MIN_MS * ticks_ms;
		UInt64 arrival = head->enqueued + ACTUATOR_STREAM5_COALESCE_ARRIVAL_MS * ticks_ms;
		if (floor > coalesce) {
			coalesce = floor;
		}
		return arrival > coalesce ? arrival : coalesce;
	}
	UInt64 lead = actuator.stream ? ACTUATOR_STREAM_PRELOAD_MS * ticks_ms : 0;
	return head->start > lead ? head->start - lead : 0;
}

/* 以核心既有的有限播放要求傳輸。HAL 的 I/O callback 只填排程。 */
static void *
actuator_worker(__unused void *context)
{
#ifdef __APPLE__
	(void)pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
	const UInt64 ticks_ms = actuator_ticks_ms();
	pthread_mutex_lock(&actuator.lock);
	for (;;) {
		if (!actuator.active || !actuator.count) {
			pthread_cond_wait(&actuator.ready, &actuator.lock);
			continue;
		}
		const struct actuator_pulse *head = &actuator.queue[actuator.head];
		UInt64 start = head->start;
		UInt64 now = mach_absolute_time();
		bool stream_cont = actuator.stream && actuator.stream_playing_until &&
		    start <= actuator.stream_playing_until + 4 * ticks_ms &&
		    start <= now + ACTUATOR_STREAM_LEAD_MS * ticks_ms;
		UInt64 fire_at = actuator_fire_at(head, stream_cont, ticks_ms);
		if (!stream_cont && now < fire_at) {
			actuator_wait(fire_at - now);
			continue;
		}
		if (!actuator.pcm && !actuator.rtp && actuator.count == 1 &&
		    actuator.queue[actuator.head].level < ACTUATOR_LOW_ONSET_LEVEL &&
		    (!actuator.playing_until || now >= actuator.playing_until)) {
			UInt64 settle = start + ACTUATOR_ONSET_HOLD_MS * ticks_ms;
			if (now < settle) {
				actuator_wait(settle - now);
				continue;
			}
		}
		actuator_submit(now, ticks_ms);
	}
}

/*
 * iOS SDK 沒有 IOKit 標頭；照 harness userland/fuxi-shape/FuxiShape.c 讀 ADT 的作法，
 * 只宣告用到的三個函式（連結 IOKit.framework）。
 */
typedef mach_port_t fx_io_object_t;
extern fx_io_object_t IORegistryEntryFromPath(mach_port_t main_port, const char *path);
extern CFTypeRef IORegistryEntryCreateCFProperty(fx_io_object_t entry, CFStringRef key, CFAllocatorRef allocator,
    uint32_t options);
extern kern_return_t IOObjectRelease(fx_io_object_t object);

/* 命令列的 fuxihalhaptic=1..6；只認完整 token，沒有就回 0。args 會被 strtok_r 切開。 */
static unsigned
actuator_bootargs_mode(char *args)
{
	char *save;
	for (char *arg = strtok_r(args, " ", &save); arg; arg = strtok_r(NULL, " ", &save)) {
		if (strcmp(arg, "fuxihalhaptic=1") == 0) return 1;
		if (strcmp(arg, "fuxihalhaptic=2") == 0) return 2;
		if (strcmp(arg, "fuxihalhaptic=3") == 0) return 3;
		if (strcmp(arg, "fuxihalhaptic=4") == 0) return 4;
		if (strcmp(arg, "fuxihalhaptic=5") == 0) return 5;
		if (strcmp(arg, "fuxihalhaptic=6") == 0) return 6;
	}
	return 0;
}

/*
 * ADT /chosen fuxi,hal-haptic：4-byte 1..6，由 loader 產生器 --hal-haptics-adt 寫入。
 * 1=DIRECT_PLAY、2=RTP click、3=單段 PCM、4=即時包絡串流、5=線性串流、6=合併＋感知增益串流。
 * 長度或值不對都當沒有。
 */
static unsigned
actuator_adt_mode(void)
{
	unsigned mode = 0;
	fx_io_object_t chosen = IORegistryEntryFromPath(MACH_PORT_NULL, "IODeviceTree:/chosen");
	if (chosen == MACH_PORT_NULL) {
		return 0;
	}
	CFTypeRef value = IORegistryEntryCreateCFProperty(chosen, CFSTR("fuxi,hal-haptic"), kCFAllocatorDefault, 0);
	if (value) {
		if (CFGetTypeID(value) == CFDataGetTypeID() && CFDataGetLength((CFDataRef)value) == 4) {
			uint32_t raw;
			memcpy(&raw, CFDataGetBytePtr((CFDataRef)value), sizeof(raw));
			mode = raw >= 1 && raw <= 6 ? raw : 0;
		}
		CFRelease(value);
	}
	(void)IOObjectRelease(chosen);
	return mode;
}

/* 命令列優先，與補丁 0121 的 fuxi,args 相同；讀不到命令列也照樣看 ADT。 */
static unsigned
actuator_select_mode(const char **source)
{
	char args[1024] = {0};
	size_t size = sizeof(args) - 1;
	unsigned mode = sysctlbyname("kern.bootargs", args, &size, NULL, 0) == 0 ? actuator_bootargs_mode(args) : 0;
	*source = "boot-args";
	if (!mode) {
		mode = actuator_adt_mode();
		*source = "ADT";
	}
	return mode;
}

static void
actuator_initialize(void)
{
	if (actuator.enabled || !actuator_ticks_ms()) {
		return;
	}
	const char *source;
	unsigned mode = actuator_select_mode(&source);
	if (!mode) {
		return;
	}
	pthread_t worker;
	actuator.rtp = mode == 2;
	actuator.pcm = mode >= 3 && mode <= 6;
	actuator.stream = mode >= 4 && mode <= 6;
	actuator.linear = mode == 5;
	actuator.perceptual = mode == 6;
	actuator.enabled = pthread_create(&worker, NULL, actuator_worker, NULL) == 0;
	if (actuator.enabled) {
		(void)pthread_detach(worker);
	}
	say("fuxi: audio: Actuator HAP525 mode %u from %s%s\n", mode, source, actuator.enabled ? "" : " (no worker)");
}

static unsigned
voice_bootargs_mode(char *args)
{
	char *save;
	for (char *arg = strtok_r(args, " ", &save); arg; arg = strtok_r(NULL, " ", &save)) {
		if (strcmp(arg, "fuxiaudiovoice=1") == 0) return 1;
		if (strcmp(arg, "fuxiaudiovoice=2") == 0) return 2;
		if (strcmp(arg, "fuxiaudiovoice=3") == 0) return 3;
		if (strcmp(arg, "fuxiaudiovoice=4") == 0) return 4;
		if (strcmp(arg, "fuxiaudiovoice=5") == 0) return 5;
	}
	return 0;
}

static unsigned
voice_adt_mode(void)
{
	unsigned mode = 0;
	fx_io_object_t chosen = IORegistryEntryFromPath(MACH_PORT_NULL, "IODeviceTree:/chosen");
	if (chosen == MACH_PORT_NULL) {
		return 0;
	}
	CFTypeRef value = IORegistryEntryCreateCFProperty(chosen, CFSTR("fuxi,audio-voice"), kCFAllocatorDefault, 0);
	if (value) {
		if (CFGetTypeID(value) == CFDataGetTypeID() && CFDataGetLength((CFDataRef)value) == 4) {
			uint32_t raw;
			memcpy(&raw, CFDataGetBytePtr((CFDataRef)value), sizeof(raw));
			mode = raw >= 1 && raw <= 5 ? raw : 0;
		}
		CFRelease(value);
	}
	(void)IOObjectRelease(chosen);
	return mode;
}

static unsigned
voice_select_mode(const char **source)
{
	char args[1024] = {0};
	size_t size = sizeof(args) - 1;
	unsigned mode = sysctlbyname("kern.bootargs", args, &size, NULL, 0) == 0 ? voice_bootargs_mode(args) : 0;
	*source = "boot-args";
	if (!mode) {
		mode = voice_adt_mode();
		*source = "ADT";
	}
	return mode;
}

static void
voice_initialize(void)
{
	const char *source = NULL;
	unsigned mode = voice_select_mode(&source);
	voice_mode = mode;
	memcpy(voice_sources, voice_avail_sources, sizeof(voice_sources));
	voice_source_count = VOICE_MAX_SOURCES;
	devices[VOICE_DEVICE_INDEX].input_channels = VOICE_MAX_SOURCES;
	__atomic_store_n(&voice_rt_cfg, voice_pack_rt_cfg(voice_sources, voice_source_count), __ATOMIC_RELEASE);
	if (mode) {
		say("fuxi: audio: Voice multi-mic device enabled mode %u from %s\n", mode, source);
	}
}

static void
say(const char *format, ...)
{
	char line[256];
	va_list ap;
	int n;

	va_start(ap, format);
	n = vsnprintf(line, sizeof(line), format, ap);
	va_end(ap);
	if (n > (int)sizeof(line) - 1) {
		n = sizeof(line) - 1;
	}
	if (n > 0) {
		(void)write(2, line, (size_t)n);
	}
}

/* The volume control's scale: a straight line of decibels over VOLUME_RANGE_DB. */
static Float32
scalar_to_db(Float32 scalar)
{
	scalar = scalar < 0 ? 0 : scalar > 1 ? 1 : scalar;
	return (Float32)(-VOLUME_RANGE_DB * (1.0 - scalar));
}

static Float32
db_to_scalar(Float32 db)
{
	Float32 scalar = (Float32)(1.0 + db / VOLUME_RANGE_DB);

	return scalar < 0 ? 0 : scalar > 1 ? 1 : scalar;
}

/* The volume: remembered and said; there is no amplifier to give it to yet. With the lock held. */
static void
volume_said(void)
{
	say("fuxi: audio: the volume is %.3f (%.1f dB)%s\n", volume, scalar_to_db(volume), volume <= 0 ? ", silence" : "");
}

/* The device with this object number, or the one this stream belongs to; NULL = neither. */
static struct device *
device_of(AudioObjectID object)
{
	unsigned count = active_devices();
	for (unsigned i = 0; i < count; i++) {
		if (object == devices[i].object || (object != 0 && (object == devices[i].output || object == devices[i].input))) {
			return &devices[i];
		}
	}
	return NULL;
}

/* A 16-bit signed packed PCM format at FUXI_AUDIO_RATE with `channels` interleaved channels. */
static AudioStreamBasicDescription
format_channels(UInt32 channels)
{
	return (AudioStreamBasicDescription) {
		       .mSampleRate = FUXI_AUDIO_RATE, .mFormatID = kAudioFormatLinearPCM,
		       .mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked,
		       .mBytesPerPacket = 2 * channels, .mFramesPerPacket = 1, .mBytesPerFrame = 2 * channels,
		       .mChannelsPerFrame = channels, .mBitsPerChannel = 16,
	};
}

/* The one format of a stream: 16-bit samples at FUXI_AUDIO_RATE, so many channels. */
static AudioStreamBasicDescription
format_of(AudioObjectID stream)
{
	const struct device *d = device_of(stream);
	UInt32 channels = stream == d->output ? d->output_channels : d->input_channels;

	return format_channels(channels);
}

static HRESULT
query_interface(void *driver, REFIID uuid, LPVOID *interface)
{
	CFUUIDRef requested = CFUUIDCreateFromUUIDBytes(NULL, uuid);
	Boolean ours = CFEqual(requested, IUnknownUUID) || CFEqual(requested, kAudioServerPlugInDriverInterfaceUUID);

	CFRelease(requested);
	if (interface == NULL || !ours) {
		return E_NOINTERFACE;
	}
	*interface = driver;            /* one driver, never freed: no counting */
	return S_OK;
}

static ULONG
add_ref(__unused void *driver)
{
	return 1;
}

static ULONG
release(__unused void *driver)
{
	return 1;
}

static void mic_initialize(void);

static OSStatus
initialize(__unused AudioServerPlugInDriverRef driver, AudioServerPlugInHostRef the_host)
{
	mach_timebase_info_data_t timebase;

	host = the_host;
	mach_timebase_info(&timebase);
	ticks_per_frame = 1e9 * timebase.denom / timebase.numer / FUXI_AUDIO_RATE;
	actuator_initialize();
	speaker_initialize();
	mic_initialize();
	voice_initialize();
	say("fuxi: audio: Speaker, Hawking, Actuator and Codec are with the HAL (HAP525=%u Voice=%u)\n",
	    (unsigned)actuator.enabled, voice_mode);
	return 0;
}

static OSStatus
create_device(__unused AudioServerPlugInDriverRef driver, __unused CFDictionaryRef description,
    __unused const AudioServerPlugInClientInfo *client, __unused AudioObjectID *object)
{
	return kAudioHardwareUnsupportedOperationError;
}

static OSStatus
destroy_device(__unused AudioServerPlugInDriverRef driver, __unused AudioObjectID object)
{
	return kAudioHardwareUnsupportedOperationError;
}

static OSStatus
device_client(__unused AudioServerPlugInDriverRef driver, __unused AudioObjectID object,
    __unused const AudioServerPlugInClientInfo *client)
{
	return 0;
}

static OSStatus
configuration_change(__unused AudioServerPlugInDriverRef driver, __unused AudioObjectID object, __unused UInt64 action,
    __unused void *info)
{
	return 0;
}

/* A property's value: `size` bytes of it, or a list with nothing in it, or (neither) no such property. */
struct value {
	UInt32 size;
	bool empty;
	bool string;                    /* a CFString: whoever is given it gets a reference of their own */
	union {
		UInt32 words[16];
		Float64 number;
		Float32 decimal;
		CFStringRef string;
		AudioValueRange range;
		AudioStreamBasicDescription format;
		AudioStreamRangedDescription ranged[VOICE_MAX_SOURCES];
	} u;
};

static struct value
property(AudioObjectID object, const AudioObjectPropertyAddress *address, UInt32 qualifier_size, const void *qualifier)
{
	UInt32 selector = address->mSelector, scope = address->mScope;
	const struct device *d = device_of(object);
	struct value v;

	memset(&v, 0, sizeof(v));
#define WORD(x)         (v.u.words[0] = (x), v.size = sizeof(UInt32))
#define LIST(x)         (v.u.words[v.size / sizeof(UInt32)] = (x), v.size += sizeof(UInt32))
#define STRING(x)       (v.u.string = (x), v.string = true, v.size = sizeof(CFStringRef))
#define FLOAT(x)        (v.u.decimal = (x), v.size = sizeof(Float32))
	if (object == OBJECT_SPEAKER_VOLUME) {
		switch (selector) {
		case kAudioObjectPropertyBaseClass:
			WORD(kAudioLevelControlClassID);
			break;
		case kAudioObjectPropertyClass:
			WORD(kAudioVolumeControlClassID);
			break;
		case kAudioObjectPropertyOwner:
			WORD(OBJECT_SPEAKER);
			break;
		case kAudioObjectPropertyOwnedObjects:
			v.empty = true;
			break;
		case kAudioControlPropertyScope:
			WORD(kAudioObjectPropertyScopeOutput);
			break;
		case kAudioControlPropertyElement:
			WORD(0);
			break;
		case kAudioLevelControlPropertyScalarValue:
			FLOAT(volume);
			break;
		case kAudioLevelControlPropertyDecibelValue:
			FLOAT(scalar_to_db(volume));
			break;
		case kAudioLevelControlPropertyDecibelRange:
			v.u.range.mMinimum = -VOLUME_RANGE_DB;
			v.u.range.mMaximum = 0;
			v.size = sizeof(AudioValueRange);
			break;
		case kAudioLevelControlPropertyConvertScalarToDecibels:
		case kAudioLevelControlPropertyConvertDecibelsToScalar:
			FLOAT(0);       /* (converts what it is given: get_property_data()) */
			break;
		}
		return v;
	}
	if (object == OBJECT_CODEC_SOURCE) {
		switch (selector) {
		case kAudioObjectPropertyBaseClass:
			WORD(kAudioSelectorControlClassID);
			break;
		case kAudioObjectPropertyClass:
			WORD(kAudioDataSourceControlClassID);
			break;
		case kAudioObjectPropertyOwner:
			WORD(OBJECT_CODEC);
			break;
		case kAudioObjectPropertyOwnedObjects:
			v.empty = true;
			break;
		case kAudioControlPropertyScope:
			WORD(kAudioObjectPropertyScopeInput);
			break;
		case kAudioControlPropertyElement:
			WORD(0);
			break;
		case kAudioSelectorControlPropertyCurrentItem:
			for (unsigned i = 0; i < codec_source_count && i < 4; i++) {
				LIST(codec_sources[i]);
			}
			break;
		case kAudioSelectorControlPropertyAvailableItems:
			LIST(MIC_SOURCE);
			LIST(MIC_SOURCE_BACK);
			LIST(MIC_SOURCE_FRONT);
			break;
		case kAudioSelectorControlPropertyItemName:
			if (qualifier_size == sizeof(UInt32) && qualifier != NULL) {
				UInt32 src = *(const UInt32 *)qualifier;
				if (src == MIC_SOURCE || src == MIC_SOURCE_COMPAT) {
					STRING(CFSTR("Bottom"));
				} else if (src == MIC_SOURCE_BACK) {
					STRING(CFSTR("Back"));
				} else if (src == MIC_SOURCE_FRONT) {
					STRING(CFSTR("Front"));
				} else {
					STRING(CFSTR(""));
				}
			} else {
				STRING(CFSTR(""));
			}
			break;
		}
		return v;
	}
	if (object == OBJECT_VOICE_SOURCE) {
		if (!voice_mode) {
			return v;
		}
		switch (selector) {
		case kAudioObjectPropertyBaseClass:
			WORD(kAudioSelectorControlClassID);
			break;
		case kAudioObjectPropertyClass:
			WORD(kAudioDataSourceControlClassID);
			break;
		case kAudioObjectPropertyOwner:
			WORD(OBJECT_VOICE);
			break;
		case kAudioObjectPropertyOwnedObjects:
			v.empty = true;
			break;
		case kAudioControlPropertyScope:
			WORD(kAudioObjectPropertyScopeInput);
			break;
		case kAudioControlPropertyElement:
			WORD(0);
			break;
		case kAudioSelectorControlPropertyCurrentItem:
			for (unsigned i = 0; i < voice_source_count && i < VOICE_MAX_SOURCES; i++) {
				LIST(voice_sources[i]);
			}
			break;
		case kAudioSelectorControlPropertyAvailableItems:
			for (unsigned i = 0; i < VOICE_MAX_SOURCES; i++) {
				LIST(voice_avail_sources[i]);
			}
			break;
		case kAudioSelectorControlPropertyItemName:
			if (qualifier_size == sizeof(UInt32) && qualifier != NULL) {
				switch (*(const UInt32 *)qualifier) {
				case 'imic': STRING(CFSTR("Bottom")); break;
				case 'smic': STRING(CFSTR("Back")); break;
				case 'fmic': STRING(CFSTR("Front")); break;
				case 'lmic': STRING(CFSTR("Bottom 2")); break;
				default: STRING(CFSTR("")); break;
				}
			} else {
				STRING(CFSTR(""));
			}
			break;
		}
		return v;
	}
	if (object != OBJECT_PLUGIN && d == NULL) {
		return v;
	}
	switch (selector) {
	case kAudioObjectPropertyBaseClass:
		WORD(kAudioObjectClassID);
		return v;
	case kAudioObjectPropertyClass:
		WORD(object == OBJECT_PLUGIN ? kAudioPlugInClassID : object == d->object ? kAudioDeviceClassID : kAudioStreamClassID);
		return v;
	case kAudioObjectPropertyOwner:
		WORD(object == OBJECT_PLUGIN ? 0 : object == d->object ? OBJECT_PLUGIN : d->object);
		return v;
	}
	if (object == OBJECT_PLUGIN) {
		unsigned count = active_devices();
		switch (selector) {
		case kAudioObjectPropertyManufacturer:
			STRING(MANUFACTURER);
			break;
		case kAudioObjectPropertyOwnedObjects:
		case kAudioPlugInPropertyDeviceList:
			for (unsigned i = 0; i < count; i++) {
				LIST(devices[i].object);
			}
			break;
		case kAudioPlugInPropertyBoxList:
			v.empty = true;
			break;
		case kAudioPlugInPropertyTranslateUIDToBox:
			WORD(0);
			break;
		case kAudioPlugInPropertyTranslateUIDToDevice:
			WORD(0);
			for (unsigned i = 0; i < count; i++) {
				if (qualifier_size == sizeof(CFStringRef) && qualifier != NULL && *(const CFStringRef *)qualifier != NULL &&
				    CFEqual(*(const CFStringRef *)qualifier, devices[i].uid)) {
					WORD(devices[i].object);
				}
			}
			break;
		case kAudioPlugInPropertyResourceBundle:
			STRING(CFSTR(""));
			break;
		}
	} else if (object == d->object) {
		switch (selector) {
		case kAudioObjectPropertyManufacturer:
			STRING(MANUFACTURER);
			break;
		case kAudioObjectPropertyName:
		case kAudioDevicePropertyDeviceUID:
		case kAudioDevicePropertyModelUID:
			STRING(d->uid);
			break;
		case kAudioDevicePropertyTransportType:
			WORD(TRANSPORT_BUILT_IN);
			break;
		case kAudioDevicePropertyRelatedDevices:
			WORD(d->object);
			break;
		case kAudioDevicePropertyClockDomain:
		case kAudioDevicePropertyLatency:
		case kAudioDevicePropertySafetyOffset:
		case kAudioDevicePropertyIsHidden:
			WORD(0);
			break;
		case kAudioDevicePropertyDeviceIsAlive:
			WORD(1);
			break;
		case kAudioDevicePropertyDeviceCanBeDefaultDevice:
			WORD(scope == kAudioObjectPropertyScopeInput ? (d->object == OBJECT_CODEC) :
			    scope == kAudioObjectPropertyScopeOutput ? (d->output != 0) :
			    (d->output != 0 || d->object == OBJECT_CODEC));
			break;
		case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
			WORD(scope != kAudioObjectPropertyScopeInput && d->output != 0);
			break;
		case kAudioDevicePropertyDeviceIsRunning:
			WORD(d->running != 0);
			break;
		case kAudioObjectPropertyOwnedObjects:
		case kAudioDevicePropertyStreams:
			/* the streams of one direction, or of both (and the speaker's volume control, which it owns) */
			if (scope != kAudioObjectPropertyScopeInput && d->output != 0) {
				LIST(d->output);
			}
			if (scope != kAudioObjectPropertyScopeOutput && d->input != 0) {
				LIST(d->input);
			}
			if (selector == kAudioObjectPropertyOwnedObjects && d->object == OBJECT_SPEAKER) {
				LIST(OBJECT_SPEAKER_VOLUME);
			}
			if (selector == kAudioObjectPropertyOwnedObjects && d->object == OBJECT_CODEC && scope != kAudioObjectPropertyScopeOutput) {
				LIST(OBJECT_CODEC_SOURCE);
			}
			if (selector == kAudioObjectPropertyOwnedObjects && d->object == OBJECT_VOICE && scope != kAudioObjectPropertyScopeOutput) {
				LIST(OBJECT_VOICE_SOURCE);
			}
			v.empty = v.size == 0;
			break;
		case kAudioObjectPropertyControlList:
			if (d->object == OBJECT_SPEAKER) {
				LIST(OBJECT_SPEAKER_VOLUME);
			}
			if (d->object == OBJECT_CODEC) {
				LIST(OBJECT_CODEC_SOURCE);
			}
			if (d->object == OBJECT_VOICE) {
				LIST(OBJECT_VOICE_SOURCE);
			}
			v.empty = v.size == 0;
			break;
		case kAudioDevicePropertyNominalSampleRate:
			v.u.number = FUXI_AUDIO_RATE;
			v.size = sizeof(Float64);
			break;
		case kAudioDevicePropertyAvailableNominalSampleRates:
			v.u.range.mMinimum = v.u.range.mMaximum = FUXI_AUDIO_RATE;
			v.size = sizeof(AudioValueRange);
			break;
		case kAudioDevicePropertyPreferredChannelsForStereo:
			v.u.words[0] = 1;
			v.u.words[1] = (scope == kAudioObjectPropertyScopeInput ? d->input_channels : d->output_channels) >= 2 ? 2 : 1;
			v.size = 2 * sizeof(UInt32);
			break;
		case kAudioDevicePropertyZeroTimeStampPeriod:
			WORD(TIMESTAMP_PERIOD);
			break;
		}
	} else {
		switch (selector) {
		case kAudioStreamPropertyIsActive:
			WORD(!inactive[object]);
			break;
		case kAudioStreamPropertyDirection:
			WORD(object == d->input);
			break;
		case kAudioStreamPropertyTerminalType:
			WORD(object == d->input ? kAudioStreamTerminalTypeMicrophone : kAudioStreamTerminalTypeSpeaker);
			break;
		case kAudioStreamPropertyStartingChannel:
			WORD(1);
			break;
		case kAudioStreamPropertyLatency:
			WORD(0);
			break;
		case kAudioStreamPropertyVirtualFormat:
		case kAudioStreamPropertyPhysicalFormat:
			v.u.format = format_of(object);
			v.size = sizeof(AudioStreamBasicDescription);
			break;
		case kAudioStreamPropertyAvailableVirtualFormats:
		case kAudioStreamPropertyAvailablePhysicalFormats:
			/*
			 * CoreAudio HALS_Stream caches pfta/sfma at device creation time, while
			 * VirtualStream::EnsureValidPhysicalStreamFormat verifies current pft against
			 * that cached list after SubPortPolicy switches 'scci' to 1..4 mics. Advertise
			 * all 1..4 channel counts from boot so 1..4-mic transitions always match pfta.
			 */
			if (object == OBJECT_VOICE_INPUT) {
				for (UInt32 ch = 1; ch <= VOICE_MAX_SOURCES; ch++) {
					v.u.ranged[ch - 1].mFormat = format_channels(ch);
					v.u.ranged[ch - 1].mSampleRateRange.mMinimum = FUXI_AUDIO_RATE;
					v.u.ranged[ch - 1].mSampleRateRange.mMaximum = FUXI_AUDIO_RATE;
				}
				v.size = VOICE_MAX_SOURCES * sizeof(AudioStreamRangedDescription);
			} else {
				v.u.ranged[0].mFormat = format_of(object);
				v.u.ranged[0].mSampleRateRange.mMinimum = v.u.ranged[0].mSampleRateRange.mMaximum = FUXI_AUDIO_RATE;
				v.size = sizeof(AudioStreamRangedDescription);
			}
			break;
		}
	}
#undef WORD
#undef LIST
#undef STRING
#undef FLOAT
	return v;
}

/*
 * What the HAL (or the virtual audio device through it) asks of an object that it does not have,
 * the first times for each object, selector and scope: what a device it will not take may lack.
 */
#define UNKNOWN_TOLD            64
static UInt32 unknown_told[UNKNOWN_TOLD][3];
static unsigned unknown_count;

static void
unknown(AudioObjectID object, const AudioObjectPropertyAddress *address)
{
	UInt32 s = address->mSelector, c = address->mScope;

	for (unsigned i = 0; i < unknown_count; i++) {
		if (unknown_told[i][0] == object && unknown_told[i][1] == s && unknown_told[i][2] == c) {
			return;
		}
	}
	if (unknown_count >= UNKNOWN_TOLD) {
		return;
	}
	unknown_told[unknown_count][0] = object;
	unknown_told[unknown_count][1] = s;
	unknown_told[unknown_count][2] = c;
	unknown_count++;
	say("fuxi: audio: asked of object %u (%s) what it does not have: '%c%c%c%c' (%#x), scope '%c%c%c%c', element %u\n",
	    (unsigned)object, device_of(object) ? CFStringGetCStringPtr(device_of(object)->uid, kCFStringEncodingUTF8) : "-",
	    (int)(s >> 24 & 0xff), (int)(s >> 16 & 0xff), (int)(s >> 8 & 0xff), (int)(s & 0xff), (unsigned)s,
	    (int)(c >> 24 & 0xff), (int)(c >> 16 & 0xff), (int)(c >> 8 & 0xff), (int)(c & 0xff), (unsigned)address->mElement);
}

static Boolean
has_property(__unused AudioServerPlugInDriverRef driver, AudioObjectID object, __unused pid_t client,
    const AudioObjectPropertyAddress *address)
{
	struct value v;

	if (address == NULL || voice_object_hidden(object)) {
		return false;
	}
	pthread_mutex_lock(&lock);
	v = property(object, address, 0, NULL);
	pthread_mutex_unlock(&lock);
	if (v.size == 0 && !v.empty) {
		unknown(object, address);
	}
	return v.size != 0 || v.empty;
}

static OSStatus
is_property_settable(AudioServerPlugInDriverRef driver, AudioObjectID object, pid_t client,
    const AudioObjectPropertyAddress *address, Boolean *settable)
{
	const struct device *d = device_of(object);

	if (address == NULL || settable == NULL || !has_property(driver, object, client, address)) {
		return kAudioHardwareUnknownPropertyError;
	}
	/* the rate and the formats can be "set", to the ones there are; the volume can be set */
	if (object == OBJECT_SPEAKER_VOLUME) {
		*settable = address->mSelector == kAudioLevelControlPropertyScalarValue ||
		    address->mSelector == kAudioLevelControlPropertyDecibelValue;
	} else if (object == OBJECT_CODEC_SOURCE || object == OBJECT_VOICE_SOURCE) {
		*settable = address->mSelector == kAudioSelectorControlPropertyCurrentItem;
	} else if (object == OBJECT_PLUGIN) {
		*settable = false;
	} else if (object == d->object) {
		*settable = address->mSelector == kAudioDevicePropertyNominalSampleRate;
	} else {
		*settable = address->mSelector == kAudioStreamPropertyIsActive ||
		    address->mSelector == kAudioStreamPropertyVirtualFormat || address->mSelector == kAudioStreamPropertyPhysicalFormat;
	}
	return 0;
}

static OSStatus
get_property_data_size(AudioServerPlugInDriverRef driver, AudioObjectID object, pid_t client,
    const AudioObjectPropertyAddress *address, UInt32 qualifier_size, const void *qualifier, UInt32 *size)
{
	if (address == NULL || size == NULL || !has_property(driver, object, client, address)) {
		return object < OBJECT_PLUGIN || object > OBJECT_LAST || voice_object_hidden(object) ?
		    kAudioHardwareBadObjectError : kAudioHardwareUnknownPropertyError;
	}
	pthread_mutex_lock(&lock);
	*size = property(object, address, qualifier_size, qualifier).size;
	pthread_mutex_unlock(&lock);
	return 0;
}

static OSStatus
get_property_data(AudioServerPlugInDriverRef driver, AudioObjectID object, pid_t client,
    const AudioObjectPropertyAddress *address, UInt32 qualifier_size, const void *qualifier, UInt32 room, UInt32 *size,
    void *data)
{
	struct value v;

	if (address == NULL || size == NULL || data == NULL || !has_property(driver, object, client, address)) {
		return object < OBJECT_PLUGIN || object > OBJECT_LAST || voice_object_hidden(object) ?
		    kAudioHardwareBadObjectError : kAudioHardwareUnknownPropertyError;
	}
	if (object == OBJECT_SPEAKER_VOLUME && (address->mSelector == kAudioLevelControlPropertyConvertScalarToDecibels ||
	    address->mSelector == kAudioLevelControlPropertyConvertDecibelsToScalar)) {
		/* what is in the buffer, converted where it is */
		if (room < sizeof(Float32)) {
			return kAudioHardwareBadPropertySizeError;
		}
		*(Float32 *)data = address->mSelector == kAudioLevelControlPropertyConvertScalarToDecibels ?
		    scalar_to_db(*(Float32 *)data) : db_to_scalar(*(Float32 *)data);
		*size = sizeof(Float32);
		return 0;
	}
	pthread_mutex_lock(&lock);
	v = property(object, address, qualifier_size, qualifier);
	pthread_mutex_unlock(&lock);
	if (v.size > room) {
		/* a list may be asked for in part; anything else must fit */
		if (address->mSelector == kAudioStreamPropertyAvailableVirtualFormats ||
		    address->mSelector == kAudioStreamPropertyAvailablePhysicalFormats) {
			v.size = room - room % (UInt32)sizeof(AudioStreamRangedDescription);
		} else if (address->mSelector != kAudioObjectPropertyOwnedObjects && address->mSelector != kAudioDevicePropertyStreams &&
		    address->mSelector != kAudioPlugInPropertyDeviceList &&
		    address->mSelector != kAudioSelectorControlPropertyAvailableItems &&
		    address->mSelector != kAudioSelectorControlPropertyCurrentItem) {
			return kAudioHardwareBadPropertySizeError;
		} else {
			v.size = room - room % sizeof(UInt32);
		}
	}
	memcpy(data, &v.u, v.size);
	if (v.string) {
		CFRetain(v.u.string);
	}
	*size = v.size;
	return 0;
}

static bool
voice_source_valid(UInt32 id)
{
	for (unsigned i = 0; i < VOICE_MAX_SOURCES; i++) {
		if (voice_avail_sources[i] == id) {
			return true;
		}
	}
	return false;
}

static OSStatus
set_property_data(AudioServerPlugInDriverRef driver, AudioObjectID object, pid_t client,
    const AudioObjectPropertyAddress *address, __unused UInt32 qualifier_size, __unused const void *qualifier, UInt32 size,
    const void *data)
{
	Boolean settable = false;

	if (address == NULL || data == NULL || is_property_settable(driver, object, client, address, &settable) != 0) {
		return kAudioHardwareUnknownPropertyError;
	}
	if (!settable) {
		return kAudioHardwareIllegalOperationError;
	}
	if (object == OBJECT_SPEAKER_VOLUME) {
		AudioObjectPropertyAddress changed[2] = {
			{ kAudioLevelControlPropertyScalarValue, kAudioObjectPropertyScopeGlobal, 0 },
			{ kAudioLevelControlPropertyDecibelValue, kAudioObjectPropertyScopeGlobal, 0 },
		};
		Float32 wanted;

		if (size != sizeof(Float32)) {
			return kAudioHardwareBadPropertySizeError;
		}
		wanted = *(const Float32 *)data;
		wanted = address->mSelector == kAudioLevelControlPropertyDecibelValue ? db_to_scalar(wanted) :
		    wanted < 0 ? 0 : wanted > 1 ? 1 : wanted;
		pthread_mutex_lock(&lock);
		volume = wanted;
		volume_said();
		bool speaker_active = speaker.active;
		pthread_mutex_unlock(&lock);
		if (speaker.enabled) {
			speaker_push_volume(wanted, speaker_active);
		}
		if (host != NULL) {
			host->PropertiesChanged(host, OBJECT_SPEAKER_VOLUME, 2, changed);
		}
		return 0;
	}
	if (object == OBJECT_CODEC_SOURCE) {
		if (size == 0 || (size % sizeof(UInt32)) != 0 || size > sizeof(codec_sources)) {
			return kAudioHardwareBadPropertySizeError;
		}
		unsigned count = size / (UInt32)sizeof(UInt32);
		const UInt32 *items = (const UInt32 *)data;
		for (unsigned i = 0; i < count; i++) {
			UInt32 src = items[i];
			if (src != MIC_SOURCE && src != MIC_SOURCE_BACK && src != MIC_SOURCE_FRONT && src != MIC_SOURCE_COMPAT) {
				return kAudioHardwareIllegalOperationError;
			}
		}
		pthread_mutex_lock(&lock);
		for (unsigned i = 0; i < count; i++) {
			codec_sources[i] = items[i];
		}
		codec_source_count = count;
		bool log_now = codec_source_said < 8;
		if (log_now) {
			codec_source_said++;
		}
		pthread_mutex_unlock(&lock);
		if (log_now) {
			say("fuxi: audio: Codec data source set count=%u first='%c%c%c%c'\n", count,
			    (int)(items[0] >> 24 & 0xff), (int)(items[0] >> 16 & 0xff),
			    (int)(items[0] >> 8 & 0xff), (int)(items[0] & 0xff));
		}
		return 0;
	}
	if (object == OBJECT_VOICE_SOURCE) {
		if (size < sizeof(UInt32) || (size % sizeof(UInt32)) != 0 || size > sizeof(voice_sources)) {
			return kAudioHardwareBadPropertySizeError;
		}
		const UInt32 *wanted = (const UInt32 *)data;
		unsigned count = size / (UInt32)sizeof(UInt32);
		for (unsigned i = 0; i < count; i++) {
			if (!voice_source_valid(wanted[i])) {
				return kAudioHardwareIllegalOperationError;
			}
			for (unsigned j = 0; j < i; j++) {
				if (wanted[j] == wanted[i]) {
					return kAudioHardwareIllegalOperationError;
				}
			}
		}
		char desc[48] = {0};
		size_t dpos = 0;
		for (unsigned i = 0; i < count && dpos + 6 < sizeof(desc); i++) {
			if (i > 0) desc[dpos++] = ' ';
			UInt32 id = wanted[i];
			desc[dpos++] = (char)((id >> 24) & 0xff);
			desc[dpos++] = (char)((id >> 16) & 0xff);
			desc[dpos++] = (char)((id >> 8) & 0xff);
			desc[dpos++] = (char)(id & 0xff);
		}
		pthread_mutex_lock(&lock);
		unsigned old_count = voice_source_count;
		memcpy(voice_sources, wanted, count * sizeof(UInt32));
		voice_source_count = count;
		devices[VOICE_DEVICE_INDEX].input_channels = count;
		__atomic_store_n(&voice_rt_cfg, voice_pack_rt_cfg(voice_sources, count), __ATOMIC_RELEASE);
		pthread_mutex_unlock(&lock);
		say("fuxi: audio: Voice sources -> [%s] (ch=%u)\n", desc, count);
		if (host != NULL) {
			AudioObjectPropertyAddress ctl_changed = {
				kAudioSelectorControlPropertyCurrentItem, kAudioObjectPropertyScopeGlobal, 0
			};
			AudioObjectPropertyAddress dev_src_changed = {
				kAudioDevicePropertyDataSource, kAudioObjectPropertyScopeInput, 0
			};
			host->PropertiesChanged(host, OBJECT_VOICE_SOURCE, 1, &ctl_changed);
			host->PropertiesChanged(host, OBJECT_VOICE, 1, &dev_src_changed);
			if (count != old_count) {
				AudioObjectPropertyAddress stream_changed[4] = {
					{ kAudioStreamPropertyAvailableVirtualFormats, kAudioObjectPropertyScopeGlobal, 0 },
					{ kAudioStreamPropertyAvailablePhysicalFormats, kAudioObjectPropertyScopeGlobal, 0 },
					{ kAudioStreamPropertyPhysicalFormat, kAudioObjectPropertyScopeGlobal, 0 },
					{ kAudioStreamPropertyVirtualFormat, kAudioObjectPropertyScopeGlobal, 0 },
				};
				AudioObjectPropertyAddress ch2_changed = {
					kAudioDevicePropertyPreferredChannelsForStereo, kAudioObjectPropertyScopeInput, 0
				};
				host->PropertiesChanged(host, OBJECT_VOICE_INPUT, 4, stream_changed);
				host->PropertiesChanged(host, OBJECT_VOICE, 1, &ch2_changed);
			}
		}
		return 0;
	}
	if (address->mSelector == kAudioStreamPropertyIsActive) {
		if (size != sizeof(UInt32)) {
			return kAudioHardwareBadPropertySizeError;
		}
		pthread_mutex_lock(&lock);
		inactive[object] = *(const UInt32 *)data == 0;
		pthread_mutex_unlock(&lock);
		return 0;
	}
	if (address->mSelector == kAudioDevicePropertyNominalSampleRate) {
		return size == sizeof(Float64) && *(const Float64 *)data == FUXI_AUDIO_RATE ? 0 : kAudioDeviceUnsupportedFormatError;
	}
	/* a format: the one we have, or nothing */
	if (size != sizeof(AudioStreamBasicDescription)) {
		return kAudioHardwareBadPropertySizeError;
	} else {
		const AudioStreamBasicDescription *wanted = data;
		pthread_mutex_lock(&lock);
		AudioStreamBasicDescription format = format_of(object);
		pthread_mutex_unlock(&lock);

		return wanted->mSampleRate == format.mSampleRate && wanted->mFormatID == format.mFormatID &&
		       wanted->mChannelsPerFrame == format.mChannelsPerFrame && wanted->mBitsPerChannel == format.mBitsPerChannel &&
		       (wanted->mFormatFlags & kAudioFormatFlagIsSignedInteger) ? 0 : kAudioDeviceUnsupportedFormatError;
	}
}

/*
 * The microphone (the device "Codec"): there is no capture path on this phone yet, so its input
 * is silence. Said when it starts and stops, with how long it ran.
 */
static UInt64 mic_frames;

enum {
	MIC_RING_FRAMES = 16384,
	MIC_RING_MASK = MIC_RING_FRAMES - 1,
	MIC_POLL_SAMPLES = 2048,
	MIC_POLL_US = 2000,
	MIC_PRIME_FRAMES = 1440,
	MIC_PRIME_WAIT_MS = 120,
};

static struct {
	pthread_mutex_t lock;
	pthread_cond_t ready;
	pthread_cond_t primed;
	int16_t ring[MIC_RING_FRAMES];
	uint32_t head, tail;
	UInt64 live_frames, underruns, errors;
	unsigned peak, users, startup_waited_ms;
	bool enabled, active, worker_running;
} mic = {
	.lock = PTHREAD_MUTEX_INITIALIZER,
	.ready = PTHREAD_COND_INITIALIZER,
	.primed = PTHREAD_COND_INITIALIZER,
};

static uint32_t
mic_poll_once(void)
{
	int16_t tmp[MIC_POLL_SAMPLES];
	size_t bytes = sizeof(tmp);
	if (sysctlbyname("kern.fuxiaudiomic", tmp, &bytes, NULL, 0) == 0) {
		uint32_t got = (uint32_t)(bytes / sizeof(int16_t));
		if (got > 0) {
			pthread_mutex_lock(&mic.lock);
			if (mic.active) {
				for (uint32_t i = 0; i < got; i++) {
					if ((uint32_t)(mic.head - mic.tail) >= MIC_RING_FRAMES) {
						mic.tail++;
					}
					int16_t s = tmp[i];
					unsigned mag = (unsigned)(s < 0 ? -(int)s : (int)s);
					if (mag > mic.peak) mic.peak = mag;
					mic.ring[mic.head & MIC_RING_MASK] = s;
					mic.head++;
				}
				mic.live_frames += got;
				if ((uint32_t)(mic.head - mic.tail) >= MIC_PRIME_FRAMES) {
					(void)pthread_cond_broadcast(&mic.primed);
				}
			}
			pthread_mutex_unlock(&mic.lock);
		}
		return got;
	}
	pthread_mutex_lock(&mic.lock);
	mic.errors++;
	pthread_mutex_unlock(&mic.lock);
	return 0;
}

static void *
mic_worker(__unused void *context)
{
#ifdef __APPLE__
	(void)pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
	for (;;) {
		pthread_mutex_lock(&mic.lock);
		while (mic.enabled && !mic.active) {
			(void)pthread_cond_wait(&mic.ready, &mic.lock);
		}
		bool alive = mic.enabled;
		if (!alive) {
			mic.worker_running = false;
		}
		pthread_mutex_unlock(&mic.lock);
		if (!alive) {
			return NULL;
		}
		if (mic_poll_once() >= MIC_POLL_SAMPLES) {
			continue;
		}
		usleep(MIC_POLL_US);
	}
}

static void
mic_initialize(void)
{
	int state = 0;
	size_t size = sizeof(state);
	if (mic.enabled || sysctlbyname("kern.fuxiaudiomicctl", &state, &size, NULL, 0) != 0 || !(state & 1)) {
		return;
	}
	pthread_t worker;
	pthread_mutex_lock(&mic.lock);
	mic.enabled = true;
	if (pthread_create(&worker, NULL, mic_worker, NULL) != 0) {
		mic.enabled = false;
	} else {
		mic.worker_running = true;
	}
	bool ok = mic.enabled;
	pthread_mutex_unlock(&mic.lock);
	if (ok) {
		(void)pthread_detach(worker);
	}
	say("fuxi: audio: Codec live mic bridge %s (state=%#x)\n", ok ? "enabled" : "failed",
	    (unsigned)state);
}

static bool
mic_acquire(void)
{
	if (!mic.enabled) {
		return false;
	}
	bool start_hw = false;
	pthread_mutex_lock(&mic.lock);
	if (mic.users++ == 0) {
		mic.head = mic.tail = 0;
		mic.live_frames = mic.underruns = mic.errors = 0;
		mic.peak = 0;
		mic.startup_waited_ms = 0;
		mic.active = true;
		pthread_cond_signal(&mic.ready);
		start_hw = true;
	}
	pthread_mutex_unlock(&mic.lock);
	if (start_hw) {
		int on = 1;
		(void)sysctlbyname("kern.fuxiaudiomicctl", NULL, NULL, &on, sizeof(on));
	}
	return true;
}

static bool
mic_release(UInt64 *live_out, UInt64 *underruns_out, unsigned *peak_out, UInt64 *errs_out)
{
	if (!mic.enabled) {
		return false;
	}
	bool stop_hw = false;
	pthread_mutex_lock(&mic.lock);
	if (mic.users > 0 && --mic.users == 0) {
		mic.active = false;
		stop_hw = true;
	}
	*live_out = mic.live_frames;
	*underruns_out = mic.underruns;
	*peak_out = mic.peak;
	*errs_out = mic.errors;
	pthread_mutex_unlock(&mic.lock);
	if (stop_hw) {
		int off = 0;
		(void)sysctlbyname("kern.fuxiaudiomicctl", NULL, NULL, &off, sizeof(off));
	}
	return true;
}

static void
codec_open(void)
{
	mic_frames = 0;
	if (mic_acquire()) {
		say("fuxi: audio: the microphone starts (live ADSP capture)\n");
		return;
	}
	say("fuxi: audio: the microphone is listened to (silence: no capture path)\n");
}

static void
codec_close(void)
{
	UInt64 live = 0, underruns = 0, errs = 0;
	unsigned peak = 0;
	if (mic_release(&live, &underruns, &peak, &errs)) {
		say("fuxi: audio: the microphone stops: %llu.%02llu s (live=%llu underruns=%llu peak=%u errs=%llu)\n",
		    mic_frames / FUXI_AUDIO_RATE, mic_frames % FUXI_AUDIO_RATE * 100 / FUXI_AUDIO_RATE,
		    live, underruns, peak, errs);
		return;
	}
	say("fuxi: audio: the microphone stops: %llu.%02llu s of silence given\n", mic_frames / FUXI_AUDIO_RATE,
	    mic_frames % FUXI_AUDIO_RATE * 100 / FUXI_AUDIO_RATE);
}

static void
voice_open(void)
{
	voice_frames = 0;
	voice_io_calls = 0;
	voice_out_nz = 0;
	voice_first_frames = 0;
	voice_out_peak = 0;
	unsigned ch = devices[VOICE_DEVICE_INDEX].input_channels;
	if (mic_acquire()) {
		say("fuxi: audio: Voice starts (live ADSP capture, ch=%u mode=%u)\n", ch, voice_mode);
		return;
	}
	say("fuxi: audio: Voice starts (silence: no capture path, ch=%u mode=%u)\n", ch, voice_mode);
}

static void
voice_close(void)
{
	UInt64 live = 0, underruns = 0, errs = 0;
	unsigned peak = 0;
	unsigned ch = devices[VOICE_DEVICE_INDEX].input_channels;
	if (mic_release(&live, &underruns, &peak, &errs)) {
		say("fuxi: audio: Voice stops: %llu.%02llu s (ch=%u live=%llu underruns=%llu peak=%u out_pk=%u out_nz=%llu calls=%llu f0=%u errs=%llu)\n",
		    voice_frames / FUXI_AUDIO_RATE, voice_frames % FUXI_AUDIO_RATE * 100 / FUXI_AUDIO_RATE,
		    ch, live, underruns, peak, voice_out_peak, voice_out_nz, voice_io_calls, (unsigned)voice_first_frames, errs);
		return;
	}
	say("fuxi: audio: Voice stops: %llu.%02llu s of silence given (ch=%u)\n",
	    voice_frames / FUXI_AUDIO_RATE, voice_frames % FUXI_AUDIO_RATE * 100 / FUXI_AUDIO_RATE, ch);
}

static void
mic_wait_primed(const char *who)
{
	if (!mic.enabled) {
		return;
	}
	struct timespec start_ts, deadline;
	(void)clock_gettime(CLOCK_REALTIME, &start_ts);
	deadline = start_ts;
	deadline.tv_nsec += (long)MIC_PRIME_WAIT_MS * 1000000L;
	if (deadline.tv_nsec >= 1000000000L) {
		deadline.tv_sec += deadline.tv_nsec / 1000000000L;
		deadline.tv_nsec %= 1000000000L;
	}
	pthread_mutex_lock(&mic.lock);
	while (mic.enabled && mic.active && mic.worker_running &&
	       (uint32_t)(mic.head - mic.tail) < MIC_PRIME_FRAMES) {
		if (pthread_cond_timedwait(&mic.primed, &mic.lock, &deadline) == ETIMEDOUT) {
			break;
		}
	}
	uint32_t avail = (uint32_t)(mic.head - mic.tail);
	bool waited = mic.worker_running;
	pthread_mutex_unlock(&mic.lock);
	if (waited) {
		struct timespec end_ts;
		(void)clock_gettime(CLOCK_REALTIME, &end_ts);
		long ms = (end_ts.tv_sec - start_ts.tv_sec) * 1000L +
		    (end_ts.tv_nsec - start_ts.tv_nsec) / 1000000L;
		if (ms < 0) {
			ms = 0;
		}
		pthread_mutex_lock(&mic.lock);
		mic.startup_waited_ms = (unsigned)ms;
		pthread_mutex_unlock(&mic.lock);
		say("fuxi: audio: %s primed (avail=%u waited=%u ms)\n", who, avail, (unsigned)ms);
	}
}

static void
mic_read_mono(int16_t *samples, UInt32 frames)
{
	if (!mic.enabled) {
		memset(samples, 0, (size_t)frames * 2);
		return;
	}
	pthread_mutex_lock(&mic.lock);
	uint32_t avail = (uint32_t)(mic.head - mic.tail);
	uint32_t take = avail < frames ? avail : frames;
	for (uint32_t i = 0; i < take; i++) {
		samples[i] = mic.ring[mic.tail & MIC_RING_MASK];
		mic.tail++;
	}
	if (take < frames) {
		memset(samples + take, 0, (size_t)(frames - take) * 2);
		mic.underruns++;
	}
	pthread_mutex_unlock(&mic.lock);
}

static void
codec_read(int16_t *samples, UInt32 frames)
{
	mic_frames += frames;
	mic_read_mono(samples, frames);
}

static void
voice_read(int16_t *samples, UInt32 frames)
{
	uint32_t cfg = __atomic_load_n(&voice_rt_cfg, __ATOMIC_ACQUIRE);
	unsigned ch = cfg & 0xffu;
	unsigned imic_idx = (cfg >> 8) & 0xffu;
	unsigned fmic_idx = (cfg >> 16) & 0xffu;
	if (ch < 1 || ch > VOICE_MAX_SOURCES) {
		ch = 1;
	}
	if (voice_io_calls == 0) {
		voice_first_frames = frames;
	}
	voice_io_calls++;
	voice_frames += frames;
	if (!mic.enabled) {
		memset(samples, 0, (size_t)frames * 2 * ch);
		return;
	}
	unsigned mode = voice_mode;
	unsigned primary_imic = imic_idx < ch ? imic_idx : 0u;
	unsigned primary_fmic = fmic_idx < ch ? fmic_idx : 0u;
	int16_t mono[512];
	UInt32 done = 0;
	UInt64 local_nz = 0;
	unsigned local_pk = voice_out_peak;
	while (done < frames) {
		UInt32 chunk = frames - done;
		if (chunk > 512u) {
			chunk = 512u;
		}
		mic_read_mono(mono, chunk);
		int16_t *dst = samples + (size_t)done * ch;
		for (UInt32 f = 0; f < chunk; f++) {
			int16_t s = mono[f];
			int16_t atten = (int16_t)(s / 4);
			for (unsigned c = 0; c < ch; c++) {
				int16_t out = 0;
				switch (mode) {
				case 1:
					out = s;
					break;
				case 2:
					out = (c == primary_imic) ? s : 0;
					break;
				case 3:
					out = (c == primary_fmic) ? s : 0;
					break;
				case 4:
					out = (c == primary_imic) ? s : atten;
					break;
				case 5:
					if (imic_idx < ch || fmic_idx < ch) {
						out = (c == imic_idx || c == fmic_idx) ? s : 0;
					} else {
						out = (c == 0) ? s : 0;
					}
					break;
				default:
					out = (c == primary_imic) ? s : 0;
					break;
				}
				dst[(size_t)f * ch + c] = out;
				if (out != 0) {
					local_nz++;
					unsigned mag = (unsigned)(out < 0 ? -(int)out : (int)out);
					if (mag > local_pk) {
						local_pk = mag;
					}
				}
			}
		}
		done += chunk;
	}
	voice_out_nz += local_nz;
	voice_out_peak = local_pk;
}

static OSStatus
start_io(__unused AudioServerPlugInDriverRef driver, AudioObjectID object, __unused UInt32 client)
{
	struct device *d = device_of(object);
	const char *primed_who = NULL;

	if (d == NULL || object != d->object) {
		return kAudioHardwareBadObjectError;
	}
	pthread_mutex_lock(&lock);
	if (d->running++ == 0) {
		d->anchor = mach_absolute_time();
		d->periods = 0;
		if (d->output != 0) {
			written = 0;
			say("fuxi: audio: %s playing\n", CFStringGetCStringPtr(d->uid, kCFStringEncodingUTF8));
		}
		if (d->object == OBJECT_CODEC) {
			codec_open();
			primed_who = "the microphone";
		} else if (d->object == OBJECT_VOICE) {
			voice_open();
			primed_who = "Voice";
		} else if (d->object == OBJECT_ACTUATOR && actuator.enabled) {
			pthread_mutex_lock(&actuator.lock);
			actuator.active = true;
			actuator.stream_env = 0;
			actuator.stream_last_end = 0;
			actuator.stream_playing_until = 0;
			pthread_mutex_unlock(&actuator.lock);
		} else if (d->object == OBJECT_SPEAKER && speaker.enabled) {
			pthread_mutex_lock(&speaker.lock);
			memset(speaker.ring, 0, sizeof(speaker.ring));
			speaker.next_send_sample = speaker.max_written_sample = 0;
			speaker.origin_sample = speaker.origin_host = 0;
			speaker.sent_frames = 0;
			speaker.overlaps = speaker.late = speaker.lead_dumps = 0;
			speaker.timeline_valid = false;
			speaker.paced = false;
			speaker.active = true;
			pthread_mutex_unlock(&speaker.lock);
			speaker_push_volume(volume, true);
		}
	}
	pthread_mutex_unlock(&lock);
	if (primed_who != NULL) {
		mic_wait_primed(primed_who);
	}
	return 0;
}

static OSStatus
stop_io(__unused AudioServerPlugInDriverRef driver, AudioObjectID object, __unused UInt32 client)
{
	struct device *d = device_of(object);

	if (d == NULL || object != d->object) {
		return kAudioHardwareBadObjectError;
	}
	pthread_mutex_lock(&lock);
	if (d->running != 0 && --d->running == 0) {
		if (d->object == OBJECT_CODEC) {
			codec_close();
		} else if (d->object == OBJECT_VOICE) {
			voice_close();
		} else if (d->object == OBJECT_ACTUATOR && actuator.enabled) {
			pthread_mutex_lock(&actuator.lock);
			actuator.active = false;
			actuator.head = actuator.count = 0;
			actuator.accepted_until = 0;
			actuator.accepted_sample_until = 0;
			actuator.accepted_sample_valid = false;
			actuator.burst_end = 0;
			actuator.stream_env = 0;
			actuator.stream_last_end = 0;
			actuator.stream_playing_until = 0;
			actuator.burst_last_end = 0;
			actuator.burst_scale_num = 0;
			actuator.burst_scale_den = 0;
			/* StopIO also follows normal completion. Already submitted PCM
			 * may finish its bounded hardware queue after HAL I/O stops. */
			if (mach_absolute_time() < actuator.playing_until) {
				int stop = 0;
				(void)sysctlbyname("kern.fuxivibrate", NULL, NULL, &stop, sizeof(stop));
			}
			actuator.playing_until = 0;
			pthread_cond_signal(&actuator.ready);
			unsigned submitted = actuator.submitted, errors = actuator.errors;
			unsigned d_lock = __atomic_exchange_n(&actuator.drops_lock, 0u, __ATOMIC_RELAXED);
			unsigned d_dup = actuator.drops_dup, d_late = actuator.drops_late, d_full = actuator.drops_full;
			actuator.submitted = actuator.errors = 0;
			actuator.drops_dup = actuator.drops_late = actuator.drops_full = 0;
			pthread_mutex_unlock(&actuator.lock);
			say("fuxi: audio: Actuator stopped (HAP525 requests=%u errors=%u drops=%u/%u/%u/%u)\n",
			    submitted, errors, d_lock, d_dup, d_late, d_full);
		} else if (d->object == OBJECT_SPEAKER && speaker.enabled) {
			speaker_push_volume(volume, false);
			pthread_mutex_lock(&speaker.lock);
			speaker.active = false;
			pthread_cond_signal(&speaker.ready);
			unsigned swrites = speaker.writes, serrors = speaker.errors;
			unsigned soverlaps = speaker.overlaps, slate = speaker.late, slead = speaker.lead_dumps;
			uint64_t ssent = speaker.sent_frames;
			speaker.writes = speaker.errors = 0;
			speaker.overlaps = speaker.late = speaker.lead_dumps = 0;
			pthread_mutex_unlock(&speaker.lock);
			say("fuxi: audio: Speaker stopped after %llu.%02llu s (sent=%llu.%02llu s writes=%u errors=%u overlaps=%u late=%u lead=%u)\n",
			    written / FUXI_AUDIO_RATE, written % FUXI_AUDIO_RATE * 100 / FUXI_AUDIO_RATE,
			    ssent / FUXI_AUDIO_RATE, ssent % FUXI_AUDIO_RATE * 100 / FUXI_AUDIO_RATE,
			    swrites, serrors, soverlaps, slate, slead);
		} else if (d->output != 0) {
			say("fuxi: audio: %s stopped after %llu.%02llu s (discarded)\n", CFStringGetCStringPtr(d->uid,
			    kCFStringEncodingUTF8), written / FUXI_AUDIO_RATE, written % FUXI_AUDIO_RATE * 100 / FUXI_AUDIO_RATE);
		}
	}
	pthread_mutex_unlock(&lock);
	return 0;
}

/* Where the device's clock was last at a multiple of TIMESTAMP_PERIOD frames, and when. */
static OSStatus
get_zero_time_stamp(__unused AudioServerPlugInDriverRef driver, AudioObjectID object, __unused UInt32 client,
    Float64 *sample_time, UInt64 *host_time, UInt64 *seed)
{
	Float64 ticks_per_period = ticks_per_frame * TIMESTAMP_PERIOD;
	UInt64 now = mach_absolute_time();
	struct device *d = device_of(object);

	if (d == NULL) {
		return kAudioHardwareBadObjectError;
	}
	pthread_mutex_lock(&lock);
	while (now >= d->anchor + (UInt64)((Float64)(d->periods + 1) * ticks_per_period)) {
		d->periods++;
	}
	*sample_time = (Float64)d->periods * TIMESTAMP_PERIOD;
	*host_time = d->anchor + (UInt64)((Float64)d->periods * ticks_per_period);
	*seed = 1;
	pthread_mutex_unlock(&lock);
	return 0;
}

static OSStatus
will_do_io_operation(__unused AudioServerPlugInDriverRef driver, __unused AudioObjectID object, __unused UInt32 client,
    UInt32 operation, Boolean *will_do, Boolean *in_place)
{
	*will_do = operation == kAudioServerPlugInIOOperationReadInput || operation == kAudioServerPlugInIOOperationWriteMix;
	*in_place = true;
	return 0;
}

static OSStatus
begin_end_io_operation(__unused AudioServerPlugInDriverRef driver, __unused AudioObjectID object, __unused UInt32 client,
    __unused UInt32 operation, __unused UInt32 frames, __unused const AudioServerPlugInIOCycleInfo *cycle)
{
	return 0;
}

/*
 * What iOS plays on the actuator -- the haptics: the system's feedbacks and the ringer's vibration,
 * as waveforms of one channel (ADT /product/haptics closed-loop). 在混音執行緒計算振幅並排進時間軸，
 * 由另一條執行緒驅動 HAP525。過期的視窗與純靜音不送出播放要求。
 */
static unsigned haptic_said;

static unsigned
actuator_peak(const int16_t *samples, UInt32 frames)
{
	unsigned peak = 0;

	for (UInt32 i = 0; i < frames; i++) {
		int sample = samples[i];
		unsigned magnitude = (unsigned)(sample < 0 ? -sample : sample);
		peak = magnitude > peak ? magnitude : peak;
	}
	return peak;
}

/* Average adjacent 48 kHz frames before quantizing to the signed 24 kHz FIFO.
 * A packet's zero padding stays inside the hardware-mandated word alignment. */
static unsigned
actuator_pcm_packet(uint8_t *output, const int16_t *input, UInt32 frames)
{
	unsigned samples = (frames + 1) / 2;
	bool active = false;
	for (unsigned i = 0; i < samples; i++) {
		unsigned j = i * 2;
		int first = input[j];
		int second = j + 1 < frames ? input[j + 1] : first;
		int sample = (first + second) * ACTUATOR_PCM_LIMIT / 65536;
		output[i] = (uint8_t)sample;
		active |= sample != 0;
	}
	unsigned length = (samples + 7) & ~3u;
	memset(output + samples, 0, length - samples);
	return active ? length : 0;
}

/*
 * 連續串流模式（mode 4，搭配核心 fuxi,haptic=9）的感知增益曲線：
 * 將 iOS CoreHaptics 輸出峰值（實測微震 level=4 -> 1024、鍵盤 level=17 -> 4352、重按 level=90 -> 23040）
 * 依近似 (peak / 32768)^0.40 對應到小米 13 HAP525（VMAX=0xa3）±126 有效手感區間。
 * 單次突發內以相同峰值包絡線性縮放，保留原始波形頻率與主動煞車過零點、不抬升尾音。
 */
static unsigned
actuator_stream_target_peak(unsigned peak)
{
	static const struct { unsigned in, out; } curve[] = {
		{ACTUATOR_PEAK_FLOOR, 1},
		{256, 8},
		{1024, 32},
		{2048, 42},
		{4096, 55},
		{8192, 72},
		{12288, 85},
		{16384, 96},
		{24576, 113},
		{32768, ACTUATOR_STREAM_PCM_LIMIT},
	};
	if (peak < ACTUATOR_PEAK_FLOOR) {
		return 0;
	}
	if (peak >= 32768u) {
		return ACTUATOR_STREAM_PCM_LIMIT;
	}
	for (unsigned i = 1; i < sizeof(curve) / sizeof(curve[0]); i++) {
		if (peak <= curve[i].in) {
			unsigned x0 = curve[i - 1].in, x1 = curve[i].in;
			unsigned y0 = curve[i - 1].out, y1 = curve[i].out;
			return y0 + ((peak - x0) * (y1 - y0) + (x1 - x0) / 2u) / (x1 - x0);
		}
	}
	return ACTUATOR_STREAM_PCM_LIMIT;
}

static unsigned
actuator_stream_packet(uint8_t *output, const int16_t *input, UInt32 frames, unsigned env_peak)
{
	unsigned target = actuator_stream_target_peak(env_peak);
	if (!target || !env_peak) {
		return 0;
	}
	unsigned samples = (frames + 1) / 2;
	bool active = false;
	int64_t den = (int64_t)env_peak * 2;
	for (unsigned i = 0; i < samples; i++) {
		unsigned j = i * 2;
		int sum = (int)input[j] + (int)(j + 1 < frames ? input[j + 1] : input[j]);
		int64_t num = (int64_t)sum * (int64_t)target;
		int sample = (int)((num + (num >= 0 ? den / 2 : -den / 2)) / den);
		if (sample > ACTUATOR_STREAM_PCM_LIMIT) sample = ACTUATOR_STREAM_PCM_LIMIT;
		if (sample < -ACTUATOR_STREAM_PCM_LIMIT) sample = -ACTUATOR_STREAM_PCM_LIMIT;
		output[i] = (uint8_t)(int8_t)sample;
		active |= sample != 0;
	}
	unsigned length = (samples + 3u) & ~3u;
	if (length > samples) {
		memset(output + samples, 0, length - samples);
	}
	return active ? length : 0;
}

static unsigned
actuator_linear_packet(uint8_t *output, const int16_t *input, UInt32 frames, unsigned min_peak8)
{
	unsigned samples = (frames + 1) / 2;
	unsigned peak8 = 0;
	for (unsigned i = 0; i < samples; i++) {
		unsigned j = i * 2;
		int sum = (int)input[j] + (int)(j + 1 < frames ? input[j + 1] : input[j]);
		int64_t num = (int64_t)sum * (int64_t)ACTUATOR_LINEAR_GAIN_NUM;
		int sample = (int)((num + (num >= 0 ? 32768 : -32768)) / 65536);
		if (sample > ACTUATOR_STREAM_PCM_LIMIT) sample = ACTUATOR_STREAM_PCM_LIMIT;
		if (sample < -ACTUATOR_STREAM_PCM_LIMIT) sample = -ACTUATOR_STREAM_PCM_LIMIT;
		output[i] = (uint8_t)(int8_t)sample;
		unsigned mag = (unsigned)(sample < 0 ? -sample : sample);
		if (mag > peak8) peak8 = mag;
	}
	if (peak8 < min_peak8) {
		return 0;
	}
	unsigned length = (samples + 3u) & ~3u;
	if (length > samples) {
		memset(output + samples, 0, length - samples);
	}
	return length;
}

static unsigned
actuator_raw24_packet(int16_t *output, const int16_t *input, UInt32 frames)
{
	unsigned samples = (frames + 1) / 2;
	if (samples > ACTUATOR_PCM_BYTES) {
		samples = ACTUATOR_PCM_BYTES;
	}
	for (unsigned i = 0; i < samples; i++) {
		unsigned j = i * 2;
		int first = input[j];
		int second = j + 1 < frames ? input[j + 1] : first;
		int sum = first + second;
		output[i] = (int16_t)((sum + (sum >= 0 ? 1 : -1)) / 2);
	}
	return samples;
}

static void
actuator_write(const int16_t *samples, UInt32 frames, const AudioServerPlugInIOCycleInfo *cycle)
{
	if (!samples || !frames || frames > TIMESTAMP_PERIOD) {
		return;
	}
	if (!actuator.enabled) {
		unsigned peak = actuator_peak(samples, frames);
		if (!peak || haptic_said >= 12) {
			return;
		}
		haptic_said++;
		say("fuxi: audio: the actuator plays: %u frames, peak %u of 32768 (HAP525 inactive)\n",
		    (unsigned)frames, peak);
		return;
	}
	if (pthread_mutex_trylock(&actuator.lock)) {
		__atomic_fetch_add(&actuator.drops_lock, 1u, __ATOMIC_RELAXED);
		return;
	}
	UInt64 now = mach_absolute_time();
	UInt64 start = cycle && (cycle->mOutputTime.mFlags & kAudioTimeStampHostTimeValid)
	    ? cycle->mOutputTime.mHostTime : now;
	bool has_sample_time = cycle != NULL &&
	    (cycle->mOutputTime.mFlags & kAudioTimeStampSampleTimeValid) != 0 &&
	    cycle->mOutputTime.mSampleTime >= 0.0;
	uint64_t base_sample = has_sample_time
	    ? (uint64_t)(cycle->mOutputTime.mSampleTime + 0.5) : 0;
	UInt64 span = (UInt64)(ticks_per_frame * frames);
	const UInt64 ticks_ms = actuator_ticks_ms();
	if (actuator.active && start <= UINT64_MAX - span &&
	    (start <= now || start - now <= ticks_ms * 500u)) {
		for (UInt32 offset = 0; offset < frames;) {
			UInt32 count = frames - offset;
			UInt32 window = actuator.stream ? 2 * ACTUATOR_PCM_BYTES :
			    actuator.pcm ? 2 * (ACTUATOR_PCM_BYTES - 4) : ACTUATOR_WINDOW;
			if (count > window) {
				count = window;
			}
			UInt64 begin = start + (UInt64)(ticks_per_frame * offset);
			UInt64 end = start + (UInt64)(ticks_per_frame * (offset + count));
			unsigned peak = actuator_peak(samples + offset, count);
			unsigned level = (peak * ACTUATOR_MAX_LEVEL + 16384u) / 32768u;
			if (!level && peak >= ACTUATOR_PEAK_FLOOR) {
				level = 1;
			}
			bool cont_tail = actuator.perceptual && !level && peak >= 8u &&
			    actuator.stream_last_end &&
			    begin <= actuator.stream_last_end + ACTUATOR_STREAM_ENV_RESET_MS * ticks_ms;
			if (level || cont_tail) {
				if (actuator.count >= ACTUATOR_QUEUE_SIZE) {
					actuator.drops_full++;
					break;
				}
				unsigned write_grace = actuator.stream
				    ? ACTUATOR_STREAM_WRITE_LATE_GRACE_MS : ACTUATOR_WRITE_LATE_GRACE_MS;
				if (end + write_grace * ticks_ms <= now) {
					actuator.drops_late++;
					offset += count;
					continue;
				}
				uint64_t sample_begin = base_sample + offset;
				uint64_t sample_end = sample_begin + count;
				bool not_dup;
				bool sample_reset = has_sample_time && actuator.accepted_sample_valid &&
				    sample_begin + FUXI_AUDIO_RATE <= actuator.accepted_sample_until;
				if (has_sample_time && actuator.accepted_sample_valid && !sample_reset) {
					not_dup = sample_begin >= actuator.accepted_sample_until;
				} else {
					UInt64 count_span = end > begin ? end - begin : 0;
					UInt64 jitter_slack = count_span / 4u;
					if (jitter_slack > ticks_ms / 2u) {
						jitter_slack = ticks_ms / 2u;
					}
					not_dup = begin + jitter_slack >= actuator.accepted_until;
				}
				if (!not_dup) {
					actuator.drops_dup++;
					offset += count;
					continue;
				}
				unsigned index = (actuator.head + actuator.count) % ACTUATOR_QUEUE_SIZE;
				struct actuator_pulse *pulse = &actuator.queue[index];
				pulse->start = begin;
				pulse->end = end;
				pulse->enqueued = now;
				pulse->level = level ? level : 1u;
				pulse->raw24_len = 0;
				pulse->pcm_length = 0;
				if (actuator.stream) {
					bool fresh = !actuator.stream_last_end ||
					    begin > actuator.stream_last_end + ACTUATOR_STREAM_ENV_RESET_MS * ticks_ms;
					if (actuator.perceptual) {
						pulse->raw24_len = actuator_raw24_packet(pulse->raw24,
						    samples + offset, count);
						if (pulse->raw24_len) {
							actuator.stream_last_end = end;
							actuator.count++;
						}
					} else if (actuator.linear) {
						unsigned min_pk = fresh ? ACTUATOR_LINEAR_FRESH_MIN_PEAK :
						    ACTUATOR_LINEAR_CONT_MIN_PEAK;
						pulse->pcm_length = actuator_linear_packet(pulse->pcm, samples + offset,
						    count, min_pk);
					} else {
						if (fresh) {
							actuator.stream_env = 0;
						}
						if (peak > actuator.stream_env) {
							actuator.stream_env = peak;
						}
						pulse->pcm_length = actuator_stream_packet(pulse->pcm, samples + offset,
						    count, actuator.stream_env);
					}
					if (!actuator.perceptual && pulse->pcm_length) {
						actuator.stream_last_end = end;
						actuator.count++;
					}
				} else {
					pulse->pcm_length = actuator.pcm
					    ? actuator_pcm_packet(pulse->pcm, samples + offset, count) : 0;
					if (!actuator.pcm || pulse->pcm_length) actuator.count++;
				}
				if (end > actuator.accepted_until) {
					actuator.accepted_until = end;
				}
				if (has_sample_time && (!actuator.accepted_sample_valid || sample_reset ||
				    sample_end > actuator.accepted_sample_until)) {
					actuator.accepted_sample_until = sample_end;
					actuator.accepted_sample_valid = true;
				}
			}
			offset += count;
		}
		pthread_cond_signal(&actuator.ready);
	}
	pthread_mutex_unlock(&actuator.lock);
}

static OSStatus
do_io_operation(__unused AudioServerPlugInDriverRef driver, __unused AudioObjectID object, AudioObjectID stream,
    __unused UInt32 client, UInt32 operation, UInt32 frames, const AudioServerPlugInIOCycleInfo *cycle, void *buffer,
    __unused void *secondary)
{
	const struct device *d = device_of(stream);

	if (d == NULL) {
		return kAudioHardwareBadObjectError;
	}
	if (operation == kAudioServerPlugInIOOperationReadInput && stream == OBJECT_CODEC_INPUT) {
		codec_read(buffer, frames);
	} else if (operation == kAudioServerPlugInIOOperationReadInput && stream == OBJECT_VOICE_INPUT) {
		voice_read(buffer, frames);
	} else if (operation == kAudioServerPlugInIOOperationReadInput) {
		/* the speaker's and Hawking's inputs: silence */
		memset(buffer, 0, (size_t)frames * 2 * d->input_channels);
	} else if (operation == kAudioServerPlugInIOOperationWriteMix && stream == OBJECT_ACTUATOR_OUTPUT) {
		actuator_write(buffer, frames, cycle);
	} else if (operation == kAudioServerPlugInIOOperationWriteMix && stream == d->output) {
		written += frames;
		if (stream == OBJECT_SPEAKER_OUTPUT && speaker.enabled) {
			speaker_write((const int16_t *)buffer, frames, cycle);
		}
	}
	return 0;
}

static AudioServerPlugInDriverInterface interface = {
	NULL, query_interface, add_ref, release, initialize, create_device, destroy_device, device_client, device_client,
	configuration_change, configuration_change, has_property, is_property_settable, get_property_data_size,
	get_property_data, set_property_data, start_io, stop_io, get_zero_time_stamp, will_do_io_operation,
	begin_end_io_operation, do_io_operation, begin_end_io_operation,
};
static AudioServerPlugInDriverInterface *driver = &interface;

/* The factory named in the bundle's Info.plist. */
void *FuxiAudio_Create(CFAllocatorRef allocator, CFUUIDRef type);

void *
FuxiAudio_Create(__unused CFAllocatorRef allocator, CFUUIDRef type)
{
	return CFEqual(type, kAudioServerPlugInTypeUUID) ? &driver : NULL;
}
