/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Native iOS 16.7.16 IOHIDEventService sensors for Xiaomi 13 (fuxi).
 * Subclasses XNU's built-in IOHIDEventService (SweetHID.cpp) directly instead of patching an external
 * IOHIDEventDriver vtable, sets Transport="ADSP" (preventing AppleSPU plugins from hijacking raw sensors),
 * and attaches FuxiSensorOrientation under FuxiSensorSpuDevice -> FuxiSensorSpuInterface advertising
 * AppleSPULib.plugin with an 'ashd' IOUserClient so backboardd/locationd CLOrientationNotifier succeeds.
 */
#include <pexpert/arm64/board_config.h>
#if defined(FUXI)
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSSet.h>
#include <libkern/c++/OSString.h>
#include <libkern/c++/OSCollectionIterator.h>

#include "IOHIDEventService.h"
#include "IOHIDEvent.h"
#include "IOHIDKeys.h"
#include "IOHIDEventTypes.h"

extern "C" uint64_t mach_absolute_time(void);
extern "C" void proc_selfname(char *buf, int size);
extern "C" int sns_hid_spu_method(uint32_t selector, uint32_t scalars, uint64_t first_scalar, uint32_t *command);

extern "C" {
enum {
    FUXI_SENSOR_ACCEL = 0,
    FUXI_SENSOR_GYRO = 1,
    FUXI_SENSOR_MAG = 2,
    FUXI_SENSOR_LIGHT = 3,
    FUXI_SENSOR_PROXIMITY = 4,
    FUXI_SENSOR_PRESSURE = 5,
    FUXI_SENSOR_ORIENT = 6,
    FUXI_SENSOR_DEVMOTION = 7,
    FUXI_SENSOR_HID_KINDS = 8,
};
typedef void (*fuxi_sensors_demand_fn)(unsigned kind, uint32_t demand);
int fuxi_sensors_hid_setup(fuxi_sensors_demand_fn demand);
int fuxi_sensors_hid_publish(unsigned kind);
int fuxi_sensors_hid_node(const char *name);
void fuxi_sensors_hid_dispatch_sample(unsigned kind, uint64_t timestamp,
    int32_t value0, int32_t value1, int32_t value2);
void fuxi_sensors_hid_orientation(int state);
void fuxi_sensors_hid_orientation_replay(void);
void fuxi_sensors_hid_dispatch_compass(uint64_t timestamp, const uint8_t report[88]);
void fuxi_sensors_hid_counts(uint32_t *dispatched, uint32_t *unbound, uint32_t *restamped);
}

namespace {

constexpr uint32_t kTicksPerSecond = 19200000u;
constexpr uint32_t kSpuClientType = 0x61736864u; /* 'ashd' */
constexpr uint32_t kProxMaskNear = 0x40u;        /* kIOHIDProximityDetectionReceiver */
constexpr const char *kSpuPluginType = "7ACF5332-1A35-4893-87CB-BA64E1887FAE";
constexpr const char *kSpuPluginPath = "AppleSPU.kext/PlugIns/AppleSPULib.plugin";

enum {
    kSvcDevMotion9Axis = FUXI_SENSOR_HID_KINDS,
    kSvcDevMotion6Axis = FUXI_SENSOR_HID_KINDS + 1,
    kSvcDevMotion3Axis = FUXI_SENSOR_HID_KINDS + 2,
    kTotalServices = FUXI_SENSOR_HID_KINDS + 3,
};

struct SensorSpec {
    uint16_t page;
    uint16_t usage;
    uint32_t placement;
    const char *name;
    const char *product;
};

static const SensorSpec kSpecs[kTotalServices] = {
    {0xff00u, 0x0003u, 0u, "FuxiSensorAccel",            "Fuxi Accelerometer"},
    {0xff00u, 0x0009u, 0u, "FuxiSensorGyro",             "Fuxi Gyroscope"},
    {0xff00u, 0x000au, 0u, "FuxiSensorMag",              "Fuxi Magnetometer"},
    {0xff00u, 0x0004u, 0u, "FuxiSensorLight",            "Fuxi Ambient Light"},
    {0xff00u, 0x0008u, 1u, "FuxiSensorProximity",        "Fuxi Proximity"},
    {0x0020u, 0x0031u, 0u, "FuxiSensorPressure",         "Fuxi Barometer"},
    {0x0020u, 0x008au, 0u, "FuxiSensorOrientation",      "Fuxi Orientation"},
    {0xff0cu, 0x0008u, 0u, "FuxiSensorDeviceMotion",     "Fuxi Device Motion"},
    {0xff0cu, 0x0004u, 0u, "FuxiSensorDeviceMotion9Axis","Fuxi Device Motion 9Axis"},
    {0xff0cu, 0x0005u, 0u, "FuxiSensorDeviceMotion6Axis","Fuxi Device Motion 6Axis"},
    {0xff0cu, 0x0007u, 0u, "FuxiSensorDeviceMotion3Axis","Fuxi Device Motion 3Axis"},
};

static fuxi_sensors_demand_fn gDemandFn;
static uint32_t gSequence[kTotalServices];
static uint64_t gLastSampleTs[kTotalServices];
static volatile uint32_t gDispatched;
static volatile uint32_t gUnbound;
static volatile uint32_t gRestamped;
static volatile uint32_t gSpuLogCount;
static volatile uint32_t gDemandLogCount;
static volatile uint32_t gOrientLogCount;
static volatile uint32_t gCompassLogCount;
static volatile uint32_t gCompassMaskLogged;
static volatile uint32_t gDevMotionDemand[4];
static volatile int32_t gCachedLux = -1;
static volatile int32_t gCachedProxMask = -1;
static volatile uint32_t gCachedProxLevel;
static volatile int32_t gCachedOrient = -1;
static uint64_t gCachedOrientTs;
static volatile uint32_t gCompassCacheSeq;
static uint8_t gCachedCompass[88];
static uint64_t gCachedCompassTs;

static bool
isDevMotionService(unsigned svcKind)
{
    return svcKind == FUXI_SENSOR_DEVMOTION ||
        svcKind == kSvcDevMotion9Axis ||
        svcKind == kSvcDevMotion6Axis ||
        svcKind == kSvcDevMotion3Axis;
}

static unsigned
devMotionSlot(unsigned svcKind)
{
    if (svcKind == FUXI_SENSOR_DEVMOTION) return 0u;
    if (svcKind == kSvcDevMotion9Axis) return 1u;
    if (svcKind == kSvcDevMotion6Axis) return 2u;
    return 3u;
}

static uint64_t
resolveStamp(uint64_t ticks)
{
    const uint64_t now = mach_absolute_time();
    if (ticks && ticks <= now && (now - ticks) < kTicksPerSecond) {
        return ticks;
    }
    if (ticks > now && (ticks - now) <= (kTicksPerSecond / 20u)) {
        return now;
    }
    __atomic_fetch_add(&gRestamped, 1u, __ATOMIC_RELAXED);
    return now;
}

static uint64_t
nextSensorStamp(unsigned kind, uint64_t raw)
{
    uint64_t ts = raw ? raw : mach_absolute_time();
    if (!ts) ts = 1u;
    if (kind >= kTotalServices) return ts;
    uint64_t prev = __atomic_load_n(&gLastSampleTs[kind], __ATOMIC_ACQUIRE);
    if (ts <= prev) ts = prev + 1u;
    __atomic_store_n(&gLastSampleTs[kind], ts, __ATOMIC_RELEASE);
    return ts;
}

static void
appendSensorTimeChild(IOHIDEvent *ev, uint64_t ts)
{
    if (!ev) return;
    uint64_t data = ts;
    IOHIDEvent *child = IOHIDEvent::vendorDefinedEvent(ts, 0xff15u, 2u, 0u,
        (UInt8 *)&data, (UInt32)sizeof(data), 0);
    if (child) {
        ev->appendChild(child);
        child->release();
    }
}

static IOHIDEvent *
buildDevMotionEvent(unsigned svcKind, uint64_t ts, const uint8_t report[88])
{
    if (!isDevMotionService(svcKind) || !report) return NULL;
    const uint16_t usage = kSpecs[svcKind].usage;
    if (svcKind == FUXI_SENSOR_DEVMOTION || svcKind == kSvcDevMotion9Axis) {
        return IOHIDEvent::vendorDefinedEvent(ts, 0xff0cu, usage, 0u,
            (UInt8 *)report, 88u, 0);
    }
    uint8_t pkt[52];
    bzero(pkt, sizeof(pkt));
    if (svcKind == kSvcDevMotion6Axis) {
        pkt[0] = 3u;
        const uint16_t rawFlags = (uint16_t)report[2] | ((uint16_t)report[3] << 8);
        const uint16_t flags6 = (uint16_t)(rawFlags & (uint16_t)~0x2408u);
        pkt[1] = (uint8_t)(flags6 & 0xffu);
        pkt[2] = (uint8_t)(flags6 >> 8);
        memcpy(pkt + 0x04, report + 0x04, 8u);
    } else {
        pkt[0] = 4u;
    }
    memcpy(pkt + 0x0c, report + 0x1c, 16u);
    memcpy(pkt + 0x1c, report + 0x2c, 12u);
    memcpy(pkt + 0x28, report + 0x38, 12u);
    return IOHIDEvent::vendorDefinedEvent(ts, 0xff0cu, usage, 0u,
        pkt, (UInt32)sizeof(pkt), 0);
}

} /* namespace */

/* ---- AppleSPULib 'ashd' UserClient for CLOrientationNotifier ---- */
class FuxiSensorSpuClient : public IOUserClient {
    OSDeclareDefaultStructors(FuxiSensorSpuClient);
public:
    virtual IOReturn clientClose(void) APPLE_KEXT_OVERRIDE;
    virtual IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *arguments,
        IOExternalMethodDispatch *dispatch, OSObject *target, void *reference) APPLE_KEXT_OVERRIDE;
};
OSDefineMetaClassAndStructors(FuxiSensorSpuClient, IOUserClient);

IOReturn
FuxiSensorSpuClient::clientClose(void)
{
    terminate();
    return kIOReturnSuccess;
}

IOReturn
FuxiSensorSpuClient::externalMethod(uint32_t selector, IOExternalMethodArguments *arguments,
    IOExternalMethodDispatch *dispatch __unused, OSObject *target __unused, void *reference __unused)
{
    const uint32_t scIn = arguments ? arguments->scalarInputCount : 0u;
    const uint64_t s0 = (arguments && scIn > 0u) ? arguments->scalarInput[0] : 0ull;
    const uint32_t stIn = arguments ? arguments->structureInputSize : 0u;
    const uint32_t stOut = arguments ? arguments->structureOutputSize : 0u;
    const uint8_t *buf = (arguments && arguments->structureInput)
        ? (const uint8_t *)arguments->structureInput : NULL;
    uint32_t w0 = 0, w1 = 0;
    if (buf && stIn >= 4u) memcpy(&w0, buf, 4u);
    if (buf && stIn >= 8u) memcpy(&w1, buf + 4u, 4u);

    uint32_t command = 0;
    const bool accepted = (sns_hid_spu_method(selector, scIn, s0, &command) == 0) ||
        (selector == 6u && stIn > 0u);
    if (selector == 6u && scIn == 0u && stIn >= 4u) {
        command = w0;
    }
    const IOReturn rc = accepted ? kIOReturnSuccess : kIOReturnUnsupported;

    if (__atomic_fetch_add(&gSpuLogCount, 1u, __ATOMIC_RELAXED) < 64u) {
        char pname[32];
        pname[0] = '\0';
        proc_selfname(pname, (int)sizeof(pname));
        IOLog("FUXI SENSORS SPU: proc=%s sel=%u cmd=%#x scIn=%u s0=%#llx stIn=%u (%#x,%#x) stOut=%u -> %#x\n",
            pname, selector, command, scIn, (unsigned long long)s0, stIn, w0, w1, stOut, (unsigned)rc);
    }

    if (arguments) {
        arguments->scalarOutputCount = 0u;
        if (arguments->structureOutput && stOut > 0u) {
            bzero(arguments->structureOutput, stOut);
        }
        arguments->structureOutputSize = 0u;
    }

    if (rc != kIOReturnSuccess) {
        return rc;
    }
    if (gDemandFn && selector != 5u) {
        gDemandFn(FUXI_SENSOR_ORIENT, 1u);
    }
    return kIOReturnSuccess;
}

/* Parent / grandparent node carrying AppleSPULib IOCFPlugInTypes and 'ashd' user client. */
class FuxiSensorSpuNode : public IOService {
    OSDeclareDefaultStructors(FuxiSensorSpuNode);
public:
    virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
    virtual IOReturn newUserClient(task_t owningTask, void *securityID, UInt32 type,
        OSDictionary *properties, IOUserClient **handler) APPLE_KEXT_OVERRIDE;
};
OSDefineMetaClassAndStructors(FuxiSensorSpuNode, IOService);

bool
FuxiSensorSpuNode::start(IOService *provider)
{
    if (!IOService::start(provider)) return false;
    OSDictionary *types = OSDictionary::withCapacity(1);
    OSString *path = OSString::withCString(kSpuPluginPath);
    const bool ok = types && path && types->setObject(kSpuPluginType, path) && setProperty("IOCFPlugInTypes", types);
    OSSafeReleaseNULL(types);
    OSSafeReleaseNULL(path);
    if (!ok) return false;
    setProperty("Built-In", kOSBooleanTrue);
    return true;
}

IOReturn
FuxiSensorSpuNode::newUserClient(task_t owningTask, void *securityID, UInt32 type,
    OSDictionary *properties, IOUserClient **handler)
{
    if (type != kSpuClientType) {
        return IOService::newUserClient(owningTask, securityID, type, properties, handler);
    }
    FuxiSensorSpuClient *client = OSTypeAlloc(FuxiSensorSpuClient);
    if (!client) return kIOReturnNoMemory;
    if (!client->initWithTask(owningTask, securityID, type, properties) || !client->attach(this)) {
        client->release();
        return kIOReturnError;
    }
    if (!client->start(this)) {
        client->detach(this);
        client->release();
        return kIOReturnError;
    }
    *handler = client;
    return kIOReturnSuccess;
}

/* ---- Native IOHIDEventService per sensor kind ---- */
class FuxiSensorService : public IOHIDEventService {
    OSDeclareDefaultStructors(FuxiSensorService);
public:
    void configure(unsigned kind);
    virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
    virtual void free(void) APPLE_KEXT_OVERRIDE;
    virtual IOReturn setProperties(OSObject *properties) APPLE_KEXT_OVERRIDE;
    virtual IOHIDEvent *copyEvent(IOHIDEventType type, IOHIDEvent *matching = 0,
        IOOptionBits options = 0) APPLE_KEXT_OVERRIDE;
    virtual IOHIDEvent *copyMatchingEvent(OSDictionary *matching) APPLE_KEXT_OVERRIDE;
    void emitEvent(IOHIDEvent *event);
protected:
    virtual OSString *getTransport(void) APPLE_KEXT_OVERRIDE { return OSString::withCString("ADSP"); }
    virtual OSString *getProduct(void) APPLE_KEXT_OVERRIDE {
        return OSString::withCString(fKind < kTotalServices ? kSpecs[fKind].product : "Fuxi Sensor");
    }
    virtual UInt32 getPrimaryUsagePage(void) APPLE_KEXT_OVERRIDE {
        return fKind < kTotalServices ? kSpecs[fKind].page : 0u;
    }
    virtual UInt32 getPrimaryUsage(void) APPLE_KEXT_OVERRIDE {
        return fKind < kTotalServices ? kSpecs[fKind].usage : 0u;
    }
    virtual UInt32 getProductID(void) APPLE_KEXT_OVERRIDE { return 0x100u + fKind; }
    virtual UInt32 getReportInterval(void) APPLE_KEXT_OVERRIDE { return 10000u; }
private:
    unsigned fKind;
    OSSet *fSeenKeys;
    bool fHasReportInterval;
    uint64_t fLastReportInterval;
    bool fHasDetectionMode;
    uint64_t fLastDetectionMode;
};
OSDefineMetaClassAndStructors(FuxiSensorService, IOHIDEventService);

static FuxiSensorService *gServices[kTotalServices];

void
FuxiSensorService::configure(unsigned kind)
{
    fKind = kind;
    fHasReportInterval = false;
    fLastReportInterval = 0;
    fHasDetectionMode = false;
    fLastDetectionMode = 0;
}

bool
FuxiSensorService::start(IOService *provider)
{
    if (fKind >= kTotalServices) return false;
    if (!IOHIDEventService::start(provider)) return false;
    fSeenKeys = OSSet::withCapacity(16);
    setProperty("HIDDefaultBehavior", kOSBooleanTrue);
    setProperty("Built-In", kOSBooleanTrue);
    if (kSpecs[fKind].placement) {
        setProperty("Placement", kSpecs[fKind].placement, 32);
    }
    return true;
}

void
FuxiSensorService::free(void)
{
    OSSafeReleaseNULL(fSeenKeys);
    IOHIDEventService::free();
}

IOReturn
FuxiSensorService::setProperties(OSObject *properties)
{
    OSDictionary *dict = OSDynamicCast(OSDictionary, properties);
    if (!dict) return kIOReturnBadArgument;

    OSCollectionIterator *it = OSCollectionIterator::withCollection(dict);
    if (it) {
        const OSSymbol *key = NULL;
        while ((key = OSDynamicCast(OSSymbol, it->getNextObject())) != NULL) {
            OSObject *val = dict->getObject(key);
            OSNumber *num = OSDynamicCast(OSNumber, val);
            if (fSeenKeys && fSeenKeys->getCount() < 24 && !fSeenKeys->containsObject(key)) {
                fSeenKeys->setObject(key);
                if (num) {
                    IOLog("FUXI SENSORS HID: %s prop %s=%llu\n",
                        getName(), key->getCStringNoCopy(), (unsigned long long)num->unsigned64BitValue());
                } else {
                    IOLog("FUXI SENSORS HID: %s prop %s (class %s)\n",
                        getName(), key->getCStringNoCopy(), val ? val->getMetaClass()->getClassName() : "null");
                }
            }
        }
        it->release();
    }

    OSNumber *ri = OSDynamicCast(OSNumber, dict->getObject("ReportInterval"));
    if (ri) {
        uint64_t val = ri->unsigned64BitValue();
        uint32_t demand = val > 0xffffffffull ? 0xffffffffu : (uint32_t)val;
        if (!fHasReportInterval || fLastReportInterval != val) {
            if (__atomic_fetch_add(&gDemandLogCount, 1u, __ATOMIC_RELAXED) < 192u) {
                char pname[32];
                pname[0] = '\0';
                proc_selfname(pname, (int)sizeof(pname));
                IOLog("FUXI SENSORS HID: %s ReportInterval %llu->%llu (proc=%s)\n",
                    getName(), (unsigned long long)(fHasReportInterval ? fLastReportInterval : 0ull),
                    (unsigned long long)val, pname);
            }
            fHasReportInterval = true;
            fLastReportInterval = val;
        }
        setProperty(kIOHIDReportIntervalKey, demand, 32);
        if (gDemandFn) {
            if (fKind == FUXI_SENSOR_ACCEL || fKind == FUXI_SENSOR_GYRO ||
                fKind == FUXI_SENSOR_MAG || fKind == FUXI_SENSOR_PRESSURE) {
                gDemandFn(fKind, demand);
            } else if (isDevMotionService(fKind)) {
                __atomic_store_n(&gDevMotionDemand[devMotionSlot(fKind)], demand, __ATOMIC_RELEASE);
                uint32_t agg = 0u;
                for (unsigned slot = 0u; slot < 4u; ++slot) {
                    const uint32_t d = __atomic_load_n(&gDevMotionDemand[slot], __ATOMIC_ACQUIRE);
                    if (d > 0u && (agg == 0u || d < agg)) {
                        agg = d;
                    }
                }
                gDemandFn(FUXI_SENSOR_DEVMOTION, agg);
            } else if (fKind == FUXI_SENSOR_ORIENT && demand > 0u) {
                gDemandFn(FUXI_SENSOR_ORIENT, 1u);
            }
        }
    }
    OSNumber *dm = OSDynamicCast(OSNumber, dict->getObject("DetectionMode"));
    if (dm && fKind == FUXI_SENSOR_PROXIMITY) {
        uint64_t dmVal = dm->unsigned64BitValue();
        if (!fHasDetectionMode || fLastDetectionMode != dmVal) {
            if (__atomic_fetch_add(&gDemandLogCount, 1u, __ATOMIC_RELAXED) < 192u) {
                char pname[32];
                pname[0] = '\0';
                proc_selfname(pname, (int)sizeof(pname));
                IOLog("FUXI SENSORS HID: %s DetectionMode %llu->%llu (proc=%s)\n",
                    getName(), (unsigned long long)(fHasDetectionMode ? fLastDetectionMode : 0ull),
                    (unsigned long long)dmVal, pname);
            }
            fHasDetectionMode = true;
            fLastDetectionMode = dmVal;
        }
        if (gDemandFn) {
            gDemandFn(fKind, dmVal ? 1u : 0u);
        }
        if (dmVal != 0u) {
            const int32_t mask = __atomic_load_n(&gCachedProxMask, __ATOMIC_ACQUIRE);
            if (mask >= 0) {
                const uint32_t level = __atomic_load_n(&gCachedProxLevel, __ATOMIC_RELAXED);
                IOHIDEvent *ev = IOHIDEvent::proximityEvent(mach_absolute_time(),
                    (IOHIDProximityDetectionMask)mask, level, 0);
                if (ev) {
                    emitEvent(ev);
                    ev->release();
                }
            }
        }
    }
    return kIOReturnSuccess;
}

IOHIDEvent *
FuxiSensorService::copyEvent(IOHIDEventType type, IOHIDEvent *matching __unused, IOOptionBits options __unused)
{
    const uint64_t now = mach_absolute_time();
    if (fKind == FUXI_SENSOR_LIGHT && type == kIOHIDEventTypeAmbientLightSensor) {
        const int32_t lux = __atomic_load_n(&gCachedLux, __ATOMIC_ACQUIRE);
        if (lux >= 0) {
            return IOHIDEvent::ambientLightSensorEvent(now, (UInt32)lux);
        }
    }
    if (fKind == FUXI_SENSOR_PROXIMITY && type == kIOHIDEventTypeProximity) {
        const int32_t mask = __atomic_load_n(&gCachedProxMask, __ATOMIC_ACQUIRE);
        if (mask >= 0) {
            const uint32_t level = __atomic_load_n(&gCachedProxLevel, __ATOMIC_RELAXED);
            return IOHIDEvent::proximityEvent(now, (IOHIDProximityDetectionMask)mask, level, 0);
        }
    }
    if (fKind == FUXI_SENSOR_ORIENT && type == kIOHIDEventTypeOrientation) {
        const int32_t st = __atomic_load_n(&gCachedOrient, __ATOMIC_ACQUIRE);
        if (st >= 0 && st <= 6) {
            uint64_t ts = __atomic_load_n(&gCachedOrientTs, __ATOMIC_ACQUIRE);
            if (!ts) {
                ts = nextSensorStamp(FUXI_SENSOR_ORIENT, now);
                __atomic_store_n(&gCachedOrientTs, ts, __ATOMIC_RELEASE);
            }
            IOHIDEvent *ev = IOHIDEvent::orientationEvent(ts, kIOHIDOrientationTypeCMUsage);
            if (ev) {
                ev->setIntegerValue(kIOHIDEventFieldOrientationDeviceOrientationUsage, (SInt32)(0x64u + (uint32_t)st));
                appendSensorTimeChild(ev, ts);
            }
            return ev;
        }
    }
    if (isDevMotionService(fKind) && type == kIOHIDEventTypeVendorDefined) {
        uint8_t localReport[88];
        uint64_t localTs = 0;
        for (unsigned attempt = 0; attempt < 4u; ++attempt) {
            const uint32_t seq1 = __atomic_load_n(&gCompassCacheSeq, __ATOMIC_ACQUIRE);
            if (!seq1 || (seq1 & 1u)) continue;
            memcpy(localReport, gCachedCompass, sizeof(localReport));
            localTs = gCachedCompassTs;
            __atomic_thread_fence(__ATOMIC_ACQUIRE);
            const uint32_t seq2 = __atomic_load_n(&gCompassCacheSeq, __ATOMIC_ACQUIRE);
            if (seq1 == seq2) {
                return buildDevMotionEvent(fKind, localTs ? localTs : now, localReport);
            }
        }
    }
    return NULL;
}

IOHIDEvent *
FuxiSensorService::copyMatchingEvent(OSDictionary *matching)
{
    if (!matching) return NULL;
    OSNumber *et = OSDynamicCast(OSNumber, matching->getObject("EventType"));
    if (!et) return NULL;
    return copyEvent((IOHIDEventType)et->unsigned32BitValue(), NULL, 0);
}

void
FuxiSensorService::emitEvent(IOHIDEvent *event)
{
    if (event) dispatchEvent(event);
}

static IOService *
attachAndRegister(IOService *service, IOService *provider, const char *name)
{
    if (!service || !provider || !service->attach(provider)) {
        OSSafeReleaseNULL(service);
        return NULL;
    }
    service->setName(name);
    if (!service->start(provider)) {
        service->detach(provider);
        service->release();
        return NULL;
    }
    service->registerService();
    return service;
}

extern "C" int
fuxi_sensors_hid_setup(fuxi_sensors_demand_fn demand)
{
    gDemandFn = demand;
    return 0;
}

static int
publishOne(unsigned kind)
{
    if (kind >= kTotalServices) return -22;
    if (__atomic_load_n(&gServices[kind], __ATOMIC_ACQUIRE) != NULL) return 0;
    IOService *platform = IOService::getPlatform();
    if (!platform) return -19;

    IOService *provider = platform;
    FuxiSensorSpuNode *spuDev = NULL;
    FuxiSensorSpuNode *spuIface = NULL;

    if (kind == FUXI_SENSOR_ORIENT) {
        spuDev = OSTypeAlloc(FuxiSensorSpuNode);
        if (!spuDev || !spuDev->init()) {
            OSSafeReleaseNULL(spuDev);
            return -12;
        }
        if (!attachAndRegister(spuDev, platform, "FuxiSensorSpuDevice")) {
            return -5;
        }
        spuIface = OSTypeAlloc(FuxiSensorSpuNode);
        if (!spuIface || !spuIface->init()) {
            OSSafeReleaseNULL(spuIface);
            spuDev->release();
            return -12;
        }
        if (!attachAndRegister(spuIface, spuDev, "FuxiSensorSpuInterface")) {
            spuDev->release();
            return -5;
        }
        provider = spuIface;
    }

    FuxiSensorService *svc = OSTypeAlloc(FuxiSensorService);
    if (!svc || !svc->init()) {
        OSSafeReleaseNULL(svc);
        OSSafeReleaseNULL(spuIface);
        OSSafeReleaseNULL(spuDev);
        return -12;
    }
    svc->configure(kind);
    if (!attachAndRegister(svc, provider, kSpecs[kind].name)) {
        OSSafeReleaseNULL(spuIface);
        OSSafeReleaseNULL(spuDev);
        return -5;
    }
    __atomic_store_n(&gServices[kind], svc, __ATOMIC_RELEASE);
    OSSafeReleaseNULL(spuIface);
    OSSafeReleaseNULL(spuDev);
    IOLog("FUXI SENSORS HID: registered %s (%#x/%#x)\n",
        kSpecs[kind].name, kSpecs[kind].page, kSpecs[kind].usage);
    return 0;
}

extern "C" int
fuxi_sensors_hid_publish(unsigned kind)
{
    if (kind >= FUXI_SENSOR_HID_KINDS) return -22;
    if (kind == FUXI_SENSOR_DEVMOTION) {
        static const unsigned kDevMotionKinds[4] = {
            FUXI_SENSOR_DEVMOTION,
            kSvcDevMotion9Axis,
            kSvcDevMotion6Axis,
            kSvcDevMotion3Axis,
        };
        int firstErr = 0;
        for (unsigned idx = 0u; idx < 4u; ++idx) {
            const int err = publishOne(kDevMotionKinds[idx]);
            if (err != 0 && firstErr == 0) {
                firstErr = err;
            }
        }
        return firstErr;
    }
    return publishOne(kind);
}

extern "C" int
fuxi_sensors_hid_node(const char *name)
{
    if (!name || !name[0]) return -22;
    IORegistryEntry *product = IORegistryEntry::fromPath("/product", gIODTPlane);
    if (!product) return -2;
    IORegistryEntry *existing = product->childFromPath(name, gIODTPlane);
    if (existing) {
        existing->release();
        product->release();
        return 1;
    }
    IOService *node = new IOService;
    if (node && !node->init()) {
        node->release();
        node = NULL;
    }
    if (!node) {
        product->release();
        return -12;
    }
    uint32_t len = 0;
    while (name[len]) len++;
    OSData *bytes = OSData::withBytes(name, len + 1u);
    bool ok = bytes && node->setProperty("name", bytes);
    OSSafeReleaseNULL(bytes);
    node->setName(name);
    ok = ok && node->attachToParent(product, gIODTPlane);
    node->release();
    product->release();
    return ok ? 0 : -5;
}

extern "C" void
fuxi_sensors_hid_dispatch_sample(unsigned kind, uint64_t timestamp,
    int32_t value0, int32_t value1, int32_t value2)
{
    if (kind >= FUXI_SENSOR_ORIENT) return;
    if (kind == FUXI_SENSOR_LIGHT) {
        __atomic_store_n(&gCachedLux, value0 > 0 ? value0 : 0, __ATOMIC_RELEASE);
    } else if (kind == FUXI_SENSOR_PROXIMITY) {
        __atomic_store_n(&gCachedProxLevel, value1 > 0 ? (uint32_t)value1 : 0u, __ATOMIC_RELAXED);
        __atomic_store_n(&gCachedProxMask, value0 ? (int32_t)kProxMaskNear : 0, __ATOMIC_RELEASE);
    }
    FuxiSensorService *svc = __atomic_load_n(&gServices[kind], __ATOMIC_ACQUIRE);
    if (!svc) {
        __atomic_fetch_add(&gUnbound, 1u, __ATOMIC_RELAXED);
        return;
    }
    const uint64_t ts = nextSensorStamp(kind, resolveStamp(timestamp));
    const uint32_t seq = ++gSequence[kind];
    IOHIDEvent *ev = NULL;
    switch (kind) {
    case FUXI_SENSOR_ACCEL:
        ev = IOHIDEvent::accelerometerEvent(ts, (IOFixed)value0, (IOFixed)value1, (IOFixed)value2, 0, 0, seq, 0);
        appendSensorTimeChild(ev, ts);
        break;
    case FUXI_SENSOR_GYRO:
        ev = IOHIDEvent::gyroEvent(ts, (IOFixed)value0, (IOFixed)value1, (IOFixed)value2, 0, 0, seq, 0);
        appendSensorTimeChild(ev, ts);
        break;
    case FUXI_SENSOR_MAG:
        ev = IOHIDEvent::compassEvent(ts, (IOFixed)value0, (IOFixed)value1, (IOFixed)value2, 0, 0, seq, 0);
        appendSensorTimeChild(ev, ts);
        break;
    case FUXI_SENSOR_LIGHT:
        ev = IOHIDEvent::ambientLightSensorEvent(ts, value0 > 0 ? (UInt32)value0 : 0u);
        break;
    case FUXI_SENSOR_PROXIMITY:
        ev = IOHIDEvent::proximityEvent(ts, (IOHIDProximityDetectionMask)(value0 ? kProxMaskNear : 0u),
            value1 > 0 ? (UInt32)value1 : 0u, 0);
        break;
    case FUXI_SENSOR_PRESSURE:
        ev = IOHIDEvent::atmosphericPressureEvent(ts, (IOFixed)value0, seq, 0);
        break;
    default:
        break;
    }
    if (!ev) return;
    svc->emitEvent(ev);
    ev->release();
    __atomic_fetch_add(&gDispatched, 1u, __ATOMIC_RELAXED);
}

extern "C" void
fuxi_sensors_hid_orientation(int state)
{
    if (state < 0 || state > 6) return;
    const uint64_t ts = nextSensorStamp(FUXI_SENSOR_ORIENT, mach_absolute_time());
    __atomic_store_n(&gCachedOrientTs, ts, __ATOMIC_RELEASE);
    __atomic_store_n(&gCachedOrient, state, __ATOMIC_RELEASE);
    FuxiSensorService *svc = __atomic_load_n(&gServices[FUXI_SENSOR_ORIENT], __ATOMIC_ACQUIRE);
    if (!svc) return;
    IOHIDEvent *ev = IOHIDEvent::orientationEvent(ts, kIOHIDOrientationTypeCMUsage);
    if (!ev) return;
    ev->setIntegerValue(kIOHIDEventFieldOrientationDeviceOrientationUsage, (SInt32)(0x64u + (uint32_t)state));
    appendSensorTimeChild(ev, ts);
    svc->emitEvent(ev);
    ev->release();
    __atomic_fetch_add(&gDispatched, 1u, __ATOMIC_RELAXED);
    if (__atomic_fetch_add(&gOrientLogCount, 1u, __ATOMIC_RELAXED) < 32u) {
        IOLog("FUXI SENSORS HID: orientation state=%d usage=%#x ts=%llu\n",
            state, 0x64u + (uint32_t)state, (unsigned long long)ts);
    }
}

extern "C" void
fuxi_sensors_hid_orientation_replay(void)
{
    const int state = __atomic_load_n(&gCachedOrient, __ATOMIC_ACQUIRE);
    if (state >= 0 && state <= 6) {
        fuxi_sensors_hid_orientation(state);
    }
}

extern "C" void
fuxi_sensors_hid_dispatch_compass(uint64_t timestamp, const uint8_t report[88])
{
    if (!report) return;
    const uint64_t rawTs = resolveStamp(timestamp);
    const uint64_t cacheTs = nextSensorStamp(FUXI_SENSOR_DEVMOTION, rawTs);
    const uint32_t seq = __atomic_load_n(&gCompassCacheSeq, __ATOMIC_RELAXED);
    __atomic_store_n(&gCompassCacheSeq, seq + 1u, __ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    memcpy(gCachedCompass, report, sizeof(gCachedCompass));
    gCachedCompassTs = cacheTs;
    __atomic_store_n(&gCompassCacheSeq, seq + 2u, __ATOMIC_RELEASE);

    static const unsigned kDevMotionKinds[4] = {
        FUXI_SENSOR_DEVMOTION,
        kSvcDevMotion9Axis,
        kSvcDevMotion6Axis,
        kSvcDevMotion3Axis,
    };
    uint32_t activeMask = 0u;
    for (unsigned slot = 0u; slot < 4u; ++slot) {
        if (__atomic_load_n(&gDevMotionDemand[slot], __ATOMIC_ACQUIRE) > 0u) {
            activeMask |= (1u << slot);
        }
    }
    if (activeMask == 0u) {
        activeMask = 1u;
    }

    uint32_t sentMask = 0u;
    for (unsigned slot = 0u; slot < 4u; ++slot) {
        if ((activeMask & (1u << slot)) == 0u) continue;
        const unsigned svcKind = kDevMotionKinds[slot];
        FuxiSensorService *svc = __atomic_load_n(&gServices[svcKind], __ATOMIC_ACQUIRE);
        if (!svc) {
            if (slot == 0u) {
                __atomic_fetch_add(&gUnbound, 1u, __ATOMIC_RELAXED);
            }
            continue;
        }
        const uint64_t evTs = (svcKind == FUXI_SENSOR_DEVMOTION)
            ? cacheTs
            : nextSensorStamp(svcKind, rawTs);
        IOHIDEvent *ev = buildDevMotionEvent(svcKind, evTs, report);
        if (!ev) continue;
        svc->emitEvent(ev);
        ev->release();
        __atomic_fetch_add(&gDispatched, 1u, __ATOMIC_RELAXED);
        sentMask |= (1u << slot);
    }
    if (sentMask != 0u) {
        const uint32_t prevMask = __atomic_fetch_or(&gCompassMaskLogged, sentMask, __ATOMIC_RELAXED);
        if ((sentMask & ~prevMask) != 0u ||
            __atomic_fetch_add(&gCompassLogCount, 1u, __ATOMIC_RELAXED) < 8u) {
            IOLog("FUXI SENSORS HID: compass ts=%llu mask=%#x\n",
                (unsigned long long)cacheTs, sentMask);
        }
    }
}

extern "C" void
fuxi_sensors_hid_counts(uint32_t *dispatched, uint32_t *unbound, uint32_t *restamped)
{
    if (dispatched) *dispatched = __atomic_load_n(&gDispatched, __ATOMIC_RELAXED);
    if (unbound) *unbound = __atomic_load_n(&gUnbound, __ATOMIC_RELAXED);
    if (restamped) *restamped = __atomic_load_n(&gRestamped, __ATOMIC_RELAXED);
}
#endif /* FUXI */
