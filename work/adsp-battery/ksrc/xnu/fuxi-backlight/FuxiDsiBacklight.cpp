/*
 * FUXI (fuxibacklight=4): panel brightness for iOS 16's CoreBrightness.
 * CoreBrightness takes a backlight for the built-in display only when the service has backlight-control = true and conforms to
 * AppleARMBacklight (-[CBDisplayContaineriOS findBacklight]); FuxiBacklight (modes 1-3) does not conform, so it never received a
 * level. This kernel has no Apple AppleARMBacklight class (no AppleARMPlatform kext), so the service is that class itself, as
 * AppleCLCD is. It publishes the panel's normal DBV range 0..2047 in IODisplayParameters brightness, accepts {brightness,
 * FadePeriod (16.16 s)} through setProperties (flat, {value = n}, or wrapped in IODisplayParameters), fades on its own work loop in
 * 16 ms steps and hands each level to the display owner (fuxi_gpu_scanout_backlight), which sends DCS 0x51 between transfers.
 * setProperties never waits for the work loop: the newest request replaces one not yet taken. No MMIO in this file.
 */
#if defined(FUXI)
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <libkern/OSAtomic.h>
#include <libkern/c++/OSBoolean.h>
#include <libkern/c++/OSCollectionIterator.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSString.h>
#include <pexpert/pexpert.h>
#include <sys/proc.h>
#include <kern/clock.h>
#include "fuxi_dsi_backlight_logic.h"

extern "C" void fuxi_gpu_scanout_backlight(unsigned level);

static volatile SInt32 dbl_events;

/* Bounded description of a request: top-level keys with numbers or types, one nested level. */
static void dbl_describe(uint32_t event, const char *prefix, OSDictionary *dict, unsigned depth)
{
	OSCollectionIterator *it = OSCollectionIterator::withCollection(dict);
	if (!it) return;
	unsigned shown = 0;
	OSString *key;
	while (shown < 16 && (key = OSDynamicCast(OSString, it->getNextObject()))) {
		OSObject *value = dict->getObject(key);
		OSNumber *number = OSDynamicCast(OSNumber, value);
		OSDictionary *child = OSDynamicCast(OSDictionary, value);
		shown++;
		if (number)
			IOLog("FUXI BACKLIGHT DSI: setProperties #%u %s%.64s number=%llu bits=%u\n", event, prefix,
			    key->getCStringNoCopy(), number->unsigned64BitValue(), number->numberOfBits());
		else
			IOLog("FUXI BACKLIGHT DSI: setProperties #%u %s%.64s type=%s\n", event, prefix, key->getCStringNoCopy(),
			    value ? value->getMetaClass()->getClassName() : "missing");
		if (child && depth == 0) dbl_describe(event, "  ", child, 1);
	}
	it->release();
}

class AppleARMBacklight : public IOService {
	OSDeclareDefaultStructors(AppleARMBacklight);
	IOLock *fLock;
	IOWorkLoop *fLoop;
	IOTimerEventSource *fFade, *fRequest;
	bool fFadeAdded, fRequestAdded, fFirst;
	volatile uint64_t fPending;   /* fdbl_request(), taken by the work loop */
	uint32_t fLevel, fFrom, fTo;  /* work loop only */
	uint32_t fPublished;          /* protected by fLock */
	uint64_t fStart, fPeriod;     /* absolute time */
	bool publishBrightness(uint32_t value);
	bool publishCalibration(void);
	void setLevel(uint32_t level);
	void fadeStep(void);
	static void requestFired(OSObject *owner, IOTimerEventSource *sender);
	static void fadeFired(OSObject *owner, IOTimerEventSource *sender);
public:
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void free(void) APPLE_KEXT_OVERRIDE;
	IOReturn setProperties(OSObject *properties) APPLE_KEXT_OVERRIDE;
};
OSDefineMetaClassAndStructors(AppleARMBacklight, IOService);

bool AppleARMBacklight::publishBrightness(uint32_t value)
{
	OSDictionary *params = OSDictionary::withCapacity(1);
	OSDictionary *brightness = OSDictionary::withCapacity(3);
	OSNumber *minimum = OSNumber::withNumber(0ULL, 32);
	OSNumber *maximum = OSNumber::withNumber(FDBL_MAX, 32);
	OSNumber *current = OSNumber::withNumber(value, 32);
	bool ok = params && brightness && minimum && maximum && current;
	if (ok) ok = brightness->setObject("min", minimum) &&
	    brightness->setObject("max", maximum) && brightness->setObject("value", current) &&
	    params->setObject("brightness", brightness) && setProperty("IODisplayParameters", params);
	OSSafeReleaseNULL(current);
	OSSafeReleaseNULL(maximum);
	OSSafeReleaseNULL(minimum);
	OSSafeReleaseNULL(brightness);
	OSSafeReleaseNULL(params);
	return ok;
}

bool AppleARMBacklight::publishCalibration(void)
{
	static const struct {
		const char *key;
		uint32_t value;
	} kCalParams[9] = {
		{"current-for-max-backlight",  FDBL_CURR_MAX_FIXED},
		{"current-for-mid-backlight",  FDBL_CURR_MID_FIXED},
		{"hardware-max-current-limit", FDBL_CURR_MAX_FIXED},
		{"lMaxProduct",                FDBL_MAX_NITS_FIXED},
		{"lMidProduct",                FDBL_MID_NITS_FIXED},
		{"lMinProduct",                FDBL_MIN_NITS_FIXED},
		{"lMaxPanel",                  FDBL_MAX_NITS_FIXED},
		{"lMidPanel",                  FDBL_MID_NITS_FIXED},
		{"lMinPanel",                  FDBL_MIN_NITS_FIXED},
	};
	static const uint32_t kMarketingTable[11] = {
		2u << 16,   4u << 16,   10u << 16,  24u << 16,  52u << 16,
		100u << 16, 160u << 16, 230u << 16, 310u << 16, 400u << 16,
		500u << 16,
	};
	OSDictionary *cal = OSDictionary::withCapacity(9);
	OSData *table = OSData::withBytes(kMarketingTable, sizeof(kMarketingTable));
	bool ok = cal && table;
	for (unsigned idx = 0; ok && idx < 9u; ++idx) {
		OSNumber *num = OSNumber::withNumber((uint64_t)kCalParams[idx].value, 32);
		ok = num && cal->setObject(kCalParams[idx].key, num);
		OSSafeReleaseNULL(num);
	}
	if (ok) ok = setProperty("new-backlight-architecture", kOSBooleanTrue) &&
	    setProperty("backlight-calibration-parameters", cal) &&
	    setProperty("backlight-marketing-table", table);
	OSSafeReleaseNULL(table);
	OSSafeReleaseNULL(cal);
	return ok;
}

/* Work loop. */
void AppleARMBacklight::setLevel(uint32_t level)
{
	if (level == fLevel) return;
	fLevel = level;
	fuxi_gpu_scanout_backlight(fdbl_level(level));
}

/* Work loop. */
void AppleARMBacklight::fadeStep(void)
{
	uint64_t now = mach_absolute_time();
	uint32_t next = fdbl_fade_at(fFrom, fTo, now - fStart, fPeriod);
	setLevel(next);
	if (next != fTo) fFade->setTimeoutMS(FDBL_STEP_MS);
}

void AppleARMBacklight::fadeFired(OSObject *owner, IOTimerEventSource *)
{
	AppleARMBacklight *self = OSDynamicCast(AppleARMBacklight, owner);
	if (self) self->fadeStep();
}

void AppleARMBacklight::requestFired(OSObject *owner, IOTimerEventSource *)
{
	AppleARMBacklight *self = OSDynamicCast(AppleARMBacklight, owner);
	if (!self) return;
	uint64_t request = __atomic_exchange_n(&self->fPending, 0ull, __ATOMIC_ACQ_REL);
	if (!(request & FDBL_PENDING)) return;
	uint32_t goal = fdbl_request_goal(request), ms = fdbl_request_ms(request);
	self->fFade->cancelTimeout();
	/* The boot loader's level is unknown: the first request is applied at once instead of fading from a guess. */
	if (self->fFirst) { self->fFirst = false; ms = 0; self->fLevel = ~0u; }
	self->fFrom = self->fLevel == ~0u ? goal : self->fLevel;
	self->fTo = goal;
	self->fStart = mach_absolute_time();
	nanoseconds_to_absolutetime((uint64_t)ms * 1000000ull, &self->fPeriod);
	self->fadeStep();
}

bool AppleARMBacklight::start(IOService *provider)
{
	int enable = 0;
	bool present = PE_parse_boot_argn("fuxibacklight", &enable, sizeof(enable));
	if (!fdbl_enabled(present, enable)) return false;
	if (!IOService::start(provider)) return false;
	fLock = IOLockAlloc();
	fLoop = IOWorkLoop::workLoop();
	fFade = IOTimerEventSource::timerEventSource(this, &AppleARMBacklight::fadeFired);
	fRequest = IOTimerEventSource::timerEventSource(this, &AppleARMBacklight::requestFired);
	if (!fLock || !fLoop || !fFade || !fRequest) return false;
	fFadeAdded = fLoop->addEventSource(fFade) == kIOReturnSuccess;
	fRequestAdded = fLoop->addEventSource(fRequest) == kIOReturnSuccess;
	if (!fFadeAdded || !fRequestAdded) return false;
	fLevel = fFrom = fTo = FDBL_INITIAL;
	fPublished = FDBL_INITIAL;
	fFirst = true;
	if (!setProperty("backlight-control", kOSBooleanTrue) ||
	    !publishCalibration() || !publishBrightness(FDBL_INITIAL)) return false;
	registerService();
	IOLog("FUXI BACKLIGHT DSI: published AppleARMBacklight min=0 max=%u value=%u nits=%u..%u new-arch=1; levels go to the display owner (DCS 0x51)\n",
	    FDBL_MAX, FDBL_INITIAL, FDBL_MIN_NITS_FIXED >> 16, FDBL_MAX_NITS_FIXED >> 16);
	return true;
}

void AppleARMBacklight::free(void)
{
	if (fLoop) {
		if (fFade) { fFade->cancelTimeout(); if (fFadeAdded) fLoop->removeEventSource(fFade); }
		if (fRequest) { fRequest->cancelTimeout(); if (fRequestAdded) fLoop->removeEventSource(fRequest); }
	}
	OSSafeReleaseNULL(fFade);
	OSSafeReleaseNULL(fRequest);
	OSSafeReleaseNULL(fLoop);
	if (fLock) { IOLockFree(fLock); fLock = NULL; }
	IOService::free();
}

IOReturn AppleARMBacklight::setProperties(OSObject *properties)
{
	OSDictionary *dict = OSDynamicCast(OSDictionary, properties);
	if (!dict) return kIOReturnBadArgument;
	if (!fRequest || !fLock) return kIOReturnNotReady;
	uint32_t event = (uint32_t)OSIncrementAtomic(&dbl_events) + 1;
	bool log = fdbl_log_this(event);
	if (log) {
		char name[33] = {};
		proc_selfname(name, sizeof(name));
		IOLog("FUXI BACKLIGHT DSI: setProperties #%u process=%.32s keys=%u\n", event, name, dict->getCount());
		dbl_describe(event, "", dict, 0);
	}
	OSDictionary *params = OSDynamicCast(OSDictionary, dict->getObject("IODisplayParameters"));
	OSDictionary *source = params ? params : dict;
	OSObject *value = source->getObject("brightness");
	OSDictionary *range = OSDynamicCast(OSDictionary, value);
	if (range) value = range->getObject("value");
	OSNumber *brightness = OSDynamicCast(OSNumber, value);
	OSObject *nitsValue = source->getObject("brightness-nits");
	if (!nitsValue && params) nitsValue = dict->getObject("brightness-nits");
	OSDictionary *nitsRange = OSDynamicCast(OSDictionary, nitsValue);
	if (nitsRange) nitsValue = nitsRange->getObject("value");
	OSNumber *nits = OSDynamicCast(OSNumber, nitsValue);
	OSNumber *fade = OSDynamicCast(OSNumber, source->getObject("FadePeriod"));
	if (!fade && params) fade = OSDynamicCast(OSNumber, dict->getObject("FadePeriod"));
	/* Other keys are accepted (and logged above): a refusal could stop CoreBrightness's whole update. */
	if (!brightness && !nits) return kIOReturnSuccess;
	bool fromNits = (brightness == NULL);
	uint64_t raw = fromNits ? nits->unsigned64BitValue() : brightness->unsigned64BitValue();
	unsigned bits = fromNits ? nits->numberOfBits() : brightness->numberOfBits();
	uint32_t goal = fromNits ? fdbl_nits_goal(raw, bits) : fdbl_goal(raw, bits);
	uint32_t ms = fdbl_fade_ms(fade ? fade->unsigned64BitValue() : 0);
	__atomic_store_n(&fPending, fdbl_request(goal, ms), __ATOMIC_RELEASE);
	fRequest->setTimeoutUS(1);
	IOLockLock(fLock);
	/* Replace the immutable registry dictionary; never mutate a reader's snapshot. */
	bool ok = (goal == fPublished) ? true : publishBrightness(goal);
	if (ok) fPublished = goal;
	IOLockUnlock(fLock);
	if (log) {
		if (fromNits)
			IOLog("FUXI BACKLIGHT DSI: nits #%u raw=%llu goal=%u sent=%u fade_ms=%u published=%u\n",
			    event, (unsigned long long)raw, goal, fdbl_level(goal), ms, ok);
		else
			IOLog("FUXI BACKLIGHT DSI: brightness #%u goal=%u sent=%u fade_ms=%u published=%u\n",
			    event, goal, fdbl_level(goal), ms, ok);
	}
	return kIOReturnSuccess;
}

/* Called from fuxi_backlight_publish (FuxiBacklight.cpp) under AppleCLCD before its registerService(). */
void fuxi_dsi_backlight_publish(IOService *provider);
void fuxi_dsi_backlight_publish(IOService *provider)
{
	AppleARMBacklight *service = new AppleARMBacklight;
	if (!service) { IOLog("FUXI BACKLIGHT DSI: allocation failed\n"); return; }
	if (!service->init()) {
		IOLog("FUXI BACKLIGHT DSI: init failed\n");
	} else if (!service->attach(provider)) {
		IOLog("FUXI BACKLIGHT DSI: attach failed\n");
	} else if (!service->start(provider)) {
		IOLog("FUXI BACKLIGHT DSI: start failed\n");
		service->detach(provider);
	}
	service->release(); /* registry attachment owns the successful service */
}
#endif /* FUXI */
