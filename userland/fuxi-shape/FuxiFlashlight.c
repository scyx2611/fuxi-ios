#include <CoreFoundation/CoreFoundation.h>
#include <dispatch/dispatch.h>
#include <mach/mach.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "FuxiTorchLevels.h"
#include "FuxiFlashlightLogic.h"

typedef mach_port_t fx_torch_io_object_t;
extern fx_torch_io_object_t IORegistryEntryFromPath(mach_port_t main_port, const char *path);
extern CFTypeRef IORegistryEntryCreateCFProperty(fx_torch_io_object_t entry, CFStringRef key,
    CFAllocatorRef allocator, uint32_t options);
extern kern_return_t IOObjectRelease(fx_torch_io_object_t object);

#define FUXI_CC_USER_DISABLED_MARKER "/private/var/mobile/Library/ControlCenter/.fuxi-flashlight-user-disabled"

static unsigned torch_hooked;
static unsigned torch_sb_hooked;
static unsigned torch_sb_refused;
static unsigned torch_sb_abi_logged;
static unsigned torch_metadata_hooked;
static unsigned torch_metadata_logged;
static unsigned torch_settings_hooked;
static unsigned torch_settings_refused;
static unsigned torch_settings_abi_logged;
static unsigned torch_cc_gate_logged;
static unsigned torch_cc_vis_warned;
static unsigned torch_cc_marker_warned;
static int torch_cc_fix_adt_state; /* 0 unread, -1 disabled (=0), 1 enabled (default/non-zero) */
static unsigned torch_level;
static unsigned torch_kvo_level;
static uint64_t torch_hw_gen;
static IMP torch_metadata_original;
static IMP torch_settings_orig_load;
static IMP torch_settings_orig_save;
static Ivar torch_sb_ivar_flashlight;
static dispatch_queue_t torch_ui_queue;
static id torch_sb_controller;
static pthread_mutex_t torch_coalesce_lock = PTHREAD_MUTEX_INITIALIZER;
static struct fuxi_torch_coalesce_state torch_coalesce;
static Ivar torch_ivar_queue;
static Ivar torch_ivar_fixed;
static Ivar torch_ivar_enabled;
static Ivar torch_ivar_enabled_fixed;
static Ivar torch_ivar_disabled;
static dispatch_source_t torch_timer;
static pthread_mutex_t torch_control_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t torch_metadata_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t torch_settings_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t torch_sb_lock = PTHREAD_MUTEX_INITIALIZER;

static const char *torch_prog_name(void)
{
    const char *name = getprogname();
    return (name != NULL && name[0] != '\0') ? name : "unknown";
}

static void torch_log(const char *message)
{
    (void)write(2, message, strlen(message));
}

static void torch_say(const char *format, ...)
{
    char line[320];
    va_list args;
    int length;

    va_start(args, format);
    length = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (length > (int)sizeof(line) - 1) {
        length = (int)sizeof(line) - 1;
    }
    if (length > 0) {
        (void)write(2, line, (size_t)length);
    }
}

static bool torch_cc_fix_enabled(void)
{
    int state = __atomic_load_n(&torch_cc_fix_adt_state, __ATOMIC_ACQUIRE);
    fx_torch_io_object_t chosen;
    uint32_t value = 1u;
    bool got = false;

    if (state != 0) {
        return state > 0;
    }
    chosen = IORegistryEntryFromPath(MACH_PORT_NULL, "IODeviceTree:/chosen");
    if (chosen != MACH_PORT_NULL) {
        CFTypeRef prop = IORegistryEntryCreateCFProperty(chosen, CFSTR("fuxi,cc-torch-fix"),
            kCFAllocatorDefault, 0);
        (void)IOObjectRelease(chosen);
        if (prop != NULL) {
            if (CFGetTypeID(prop) == CFDataGetTypeID() &&
                CFDataGetLength((CFDataRef)prop) == (CFIndex)sizeof(value)) {
                memcpy(&value, CFDataGetBytePtr((CFDataRef)prop), sizeof(value));
                got = true;
            }
            CFRelease(prop);
        }
    }
    if (got && value == 0u) {
        __atomic_store_n(&torch_cc_fix_adt_state, -1, __ATOMIC_RELEASE);
        return false;
    }
    __atomic_store_n(&torch_cc_fix_adt_state, 1, __ATOMIC_RELEASE);
    return true;
}

static bool torch_ready(void)
{
    int level = 0;
    size_t size = sizeof(level);
    return sysctlbyname("kern.fuxitorch", &level, &size, NULL, 0) == 0 &&
        size == sizeof(level) && level >= 0 && level <= 4;
}

static bool torch_write(unsigned level)
{
    int value = (int)level;
    return sysctlbyname("kern.fuxitorch", NULL, NULL, &value, sizeof(value)) == 0;
}

static bool torch_hw_apply(unsigned requested, uint64_t expected_gen, bool update_gen,
    bool immediate_log, int *failure_out, unsigned *prev_out)
{
    (void)pthread_mutex_lock(&torch_control_lock);
    unsigned prev = __atomic_load_n(&torch_level, __ATOMIC_ACQUIRE);
    if (!update_gen && __atomic_load_n(&torch_hw_gen, __ATOMIC_ACQUIRE) != expected_gen) {
        (void)pthread_mutex_unlock(&torch_control_lock);
        if (prev_out) *prev_out = prev;
        if (failure_out) *failure_out = 0;
        return true;
    }
    if (update_gen) {
        (void)__atomic_add_fetch(&torch_hw_gen, 1u, __ATOMIC_ACQ_REL);
    }
    bool accepted = torch_write(requested);
    int failure = accepted ? 0 : errno;
    if (accepted) {
        __atomic_store_n(&torch_level, requested, __ATOMIC_RELEASE);
    }
    (void)pthread_mutex_unlock(&torch_control_lock);
    if (prev_out) *prev_out = prev;
    if (failure_out) *failure_out = failure;
    if (accepted && prev != requested) {
        torch_say("fuxi: SpringBoard: native flashlight level=%u current=%umA%s\n",
            requested, requested * 20u, immediate_log ? " (immediate)" : "");
    }
    return accepted;
}

static void torch_renew(void *context)
{
    (void)context;
    (void)pthread_mutex_lock(&torch_control_lock);
    unsigned level = __atomic_load_n(&torch_level, __ATOMIC_ACQUIRE);
    if (level && !torch_write(level)) {
        __atomic_store_n(&torch_level, 0u, __ATOMIC_RELEASE);
        torch_log("fuxi: SpringBoard: torch lease renewal failed; output will expire\n");
    }
    (void)pthread_mutex_unlock(&torch_control_lock);
}

static void torch_error(id *error, int code)
{
    if (error == NULL) return;
    CFErrorRef value = CFErrorCreate(kCFAllocatorDefault, kCFErrorDomainPOSIX, code, NULL);
    *error = ((id (*)(id, SEL))objc_msgSend)((id)value, sel_registerName("autorelease"));
}

static bool torch_set(id self, SEL selector, float level, id *error)
{
    (void)selector;
    unsigned requested;
    if (!fuxi_torch_ui_decode(level, &requested)) {
        torch_error(error, EINVAL);
        return false;
    }
    unsigned prev_kvo = __atomic_load_n(&torch_kvo_level, __ATOMIC_ACQUIRE);
    if (prev_kvo != requested) {
        ((void (*)(id, SEL, id))objc_msgSend)(self, sel_registerName("willChangeValueForKey:"),
            (id)CFSTR("flashlightLevel"));
    }
    int failure = 0;
    bool accepted = torch_hw_apply(requested, 0u, true, false, &failure, NULL);
    if (accepted) {
        __atomic_store_n(&torch_kvo_level, requested, __ATOMIC_RELEASE);
    }
    if (prev_kvo != requested) {
        ((void (*)(id, SEL, id))objc_msgSend)(self, sel_registerName("didChangeValueForKey:"),
            (id)CFSTR("flashlightLevel"));
    }
    if (!accepted) {
        torch_error(error, failure);
        return false;
    }
    return true;
}

static bool torch_set_kvo_from_ui(id av_fl, unsigned requested, uint64_t expected_gen)
{
    unsigned prev_kvo = __atomic_load_n(&torch_kvo_level, __ATOMIC_ACQUIRE);
    if (prev_kvo != requested) {
        ((void (*)(id, SEL, id))objc_msgSend)(av_fl, sel_registerName("willChangeValueForKey:"),
            (id)CFSTR("flashlightLevel"));
    }
    int failure = 0;
    bool accepted = torch_hw_apply(requested, expected_gen, false, false, &failure, NULL);
    if (accepted) {
        __atomic_store_n(&torch_kvo_level, requested, __ATOMIC_RELEASE);
    }
    if (prev_kvo != requested) {
        ((void (*)(id, SEL, id))objc_msgSend)(av_fl, sel_registerName("didChangeValueForKey:"),
            (id)CFSTR("flashlightLevel"));
    }
    return accepted;
}

static id torch_init(id self, SEL selector)
{
    struct objc_super parent = {self, class_getSuperclass(objc_getClass("AVFlashlight"))};
    return ((id (*)(struct objc_super *, SEL))objc_msgSendSuper)(&parent, selector);
}

static bool torch_available(id self, SEL selector)
{
    (void)self;
    (void)selector;
    return torch_ready();
}

static bool torch_overheated(id self, SEL selector)
{
    (void)self;
    (void)selector;
    return false;
}

static float torch_get(id self, SEL selector)
{
    (void)self;
    (void)selector;
    int level = 0;
    size_t size = sizeof(level);
    if (sysctlbyname("kern.fuxitorch", &level, &size, NULL, 0) ||
        size != sizeof(level) || level < 0 || level > 4) return 0.0f;
    return fuxi_torch_ui_encode((unsigned)level);
}

static bool torch_power_on(id self, SEL selector, id *error)
{
    (void)self;
    (void)selector;
    if (torch_ready()) return true;
    torch_error(error, EIO);
    return false;
}

static void torch_power_off(id self, SEL selector)
{
    (void)torch_set(self, selector, 0.0f, NULL);
}

static bool torch_signature(Method method, char result, unsigned count, char third)
{
    char encoding[16];
    if (method == NULL || method_getNumberOfArguments(method) != count) return false;
    method_getReturnType(method, encoding, sizeof(encoding));
    if (result == 'B') {
        if (encoding[0] != 'B' && encoding[0] != 'c') return false;
    } else if (encoding[0] != result) {
        return false;
    }
    if (third) {
        method_getArgumentType(method, 2, encoding, sizeof(encoding));
        if (encoding[0] != third) return false;
    }
    if (count == 4) {
        method_getArgumentType(method, 3, encoding, sizeof(encoding));
        if (encoding[0] != '^') return false;
    }
    return true;
}

static id torch_module_capabilities(id self, SEL selector, id information)
{
    IMP orig = __atomic_load_n(&torch_metadata_original, __ATOMIC_ACQUIRE);
    if (orig == NULL || orig == (IMP)torch_module_capabilities) {
        return NULL;
    }
    if (information && torch_ready() &&
        CFGetTypeID((CFTypeRef)information) == CFDictionaryGetTypeID()) {
        CFDictionaryRef dictionary = (CFDictionaryRef)information;
        CFTypeRef identifier = CFDictionaryGetValue(dictionary, CFSTR("CFBundleIdentifier"));
        CFTypeRef capabilities = CFDictionaryGetValue(dictionary, CFSTR("UIRequiredDeviceCapabilities"));
        char bundle_id[FUXI_CC_MAX_ID_LEN];
        if (identifier && CFGetTypeID(identifier) == CFStringGetTypeID() &&
            CFStringGetCString((CFStringRef)identifier, bundle_id, (CFIndex)sizeof(bundle_id),
                kCFStringEncodingUTF8) &&
            fuxi_flashlight_is_target_module(bundle_id) &&
            capabilities && CFGetTypeID(capabilities) == CFArrayGetTypeID()) {
            CFArrayRef caps = (CFArrayRef)capabilities;
            CFIndex cap_count = CFArrayGetCount(caps);
            CFMutableArrayRef filtered = CFArrayCreateMutableCopy(kCFAllocatorDefault, 0, caps);
            CFMutableDictionaryRef updated = CFDictionaryCreateMutableCopy(kCFAllocatorDefault, 0, dictionary);
            if (filtered && updated && cap_count > 0) {
                bool removed = false;
                bool planned = false;
                if ((size_t)cap_count <= FUXI_FLASHLIGHT_MAX_CAPS) {
                    char cap_buf[FUXI_FLASHLIGHT_MAX_CAPS][64];
                    const char *cap_ptrs[FUXI_FLASHLIGHT_MAX_CAPS];
                    bool keep_flags[FUXI_FLASHLIGHT_MAX_CAPS];
                    size_t dropped = 0u;
                    bool all_strings = true;
                    for (CFIndex i = 0; i < cap_count; i++) {
                        CFTypeRef item = CFArrayGetValueAtIndex(caps, i);
                        if (item == NULL || CFGetTypeID(item) != CFStringGetTypeID() ||
                            !CFStringGetCString((CFStringRef)item, cap_buf[i], (CFIndex)sizeof(cap_buf[i]),
                                kCFStringEncodingUTF8)) {
                            all_strings = false;
                            break;
                        }
                        cap_ptrs[i] = cap_buf[i];
                    }
                    if (all_strings) {
                        planned = true;
                        if (fuxi_flashlight_plan_cap_filter(bundle_id, true, cap_ptrs, (size_t)cap_count,
                                keep_flags, &dropped)) {
                            for (CFIndex i = cap_count; i > 0; i--) {
                                if (!keep_flags[i - 1]) {
                                    CFArrayRemoveValueAtIndex(filtered, i - 1);
                                    removed = true;
                                }
                            }
                        }
                    }
                }
                if (!planned) {
                    for (CFIndex index = cap_count; index > 0; index--) {
                        CFTypeRef item = CFArrayGetValueAtIndex(filtered, index - 1);
                        char item_buf[64];
                        if (item != NULL && CFGetTypeID(item) == CFStringGetTypeID() &&
                            CFStringGetCString((CFStringRef)item, item_buf, (CFIndex)sizeof(item_buf),
                                kCFStringEncodingUTF8) &&
                            fuxi_flashlight_capability_should_drop(item_buf)) {
                            CFArrayRemoveValueAtIndex(filtered, index - 1);
                            removed = true;
                        }
                    }
                }
                if (removed) {
                    CFDictionarySetValue(updated, CFSTR("UIRequiredDeviceCapabilities"), filtered);
                    id result = ((id (*)(id, SEL, id))orig)(self, selector, (id)updated);
                    CFRelease(updated);
                    CFRelease(filtered);
                    if (!__atomic_exchange_n(&torch_metadata_logged, 1u, __ATOMIC_ACQ_REL)) {
                        torch_say("fuxi: %s: native FlashlightModule capability admitted\n", torch_prog_name());
                    }
                    return result;
                }
            }
            if (updated) CFRelease(updated);
            if (filtered) CFRelease(filtered);
        }
    }
    return ((id (*)(id, SEL, id))orig)(self, selector, information);
}

bool fuxi_flashlight_metadata_hook(void);
bool fuxi_flashlight_metadata_hook(void)
{
    if (__atomic_load_n(&torch_metadata_hooked, __ATOMIC_ACQUIRE) != 0u) {
        return true;
    }
    Class target = objc_getClass("CCSModuleMetadata");
    if (target == NULL) {
        return false;
    }
    if (!torch_ready()) {
        return false;
    }
    Method method = class_getClassMethod(target, sel_registerName("_requiredCapabilitiesForInfoDictionary:"));
    bool sig_valid = torch_signature(method, '@', 3, '@');
    bool installed_now = false;
    bool result = false;

    (void)pthread_mutex_lock(&torch_metadata_lock);
    bool already = (__atomic_load_n(&torch_metadata_hooked, __ATOMIC_ACQUIRE) != 0u);
    IMP current = method != NULL ? method_getImplementation(method) : NULL;
    enum fuxi_flashlight_meta_step_result step = fuxi_flashlight_meta_decide_install(
        already, true, true, sig_valid, (const void *)current, (const void *)torch_module_capabilities);
    switch (step) {
    case FUXI_FLASHLIGHT_META_ALREADY_HOOKED:
        result = true;
        break;
    case FUXI_FLASHLIGHT_META_SELF_IMP_DETECTED:
        __atomic_store_n(&torch_metadata_hooked, 1u, __ATOMIC_RELEASE);
        result = true;
        break;
    case FUXI_FLASHLIGHT_META_INSTALL_NOW:
        __atomic_store_n(&torch_metadata_original, current, __ATOMIC_RELEASE);
        (void)method_setImplementation(method, (IMP)torch_module_capabilities);
        __atomic_store_n(&torch_metadata_hooked, 1u, __ATOMIC_RELEASE);
        installed_now = true;
        result = true;
        break;
    default:
        result = false;
        break;
    }
    (void)pthread_mutex_unlock(&torch_metadata_lock);

    if (installed_now) {
        torch_say("fuxi: %s: native FlashlightModule metadata hook ready\n", torch_prog_name());
    }
    return result;
}

static bool torch_cc_marker_exists(void)
{
    return access(FUXI_CC_USER_DISABLED_MARKER, F_OK) == 0;
}

static bool torch_cc_marker_write(void)
{
    int fd = open(FUXI_CC_USER_DISABLED_MARKER, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        ssize_t wr = write(fd, "1\n", 2);
        int wr_err = errno;
        if (close(fd) == 0 && wr == 2) {
            return true;
        }
        if (!__atomic_exchange_n(&torch_cc_marker_warned, 1u, __ATOMIC_ACQ_REL)) {
            torch_say("fuxi: %s: warning: cannot write user-disabled marker (%d); continuing\n",
                torch_prog_name(), wr < 0 ? wr_err : errno);
        }
        return false;
    }
    if (!__atomic_exchange_n(&torch_cc_marker_warned, 1u, __ATOMIC_ACQ_REL)) {
        torch_say("fuxi: %s: warning: cannot write user-disabled marker (%d); continuing\n",
            torch_prog_name(), errno);
    }
    return false;
}

static void torch_cc_marker_clear(void)
{
    if (unlink(FUXI_CC_USER_DISABLED_MARKER) == 0 || errno == ENOENT) {
        return;
    }
    if (!__atomic_exchange_n(&torch_cc_marker_warned, 1u, __ATOMIC_ACQ_REL)) {
        torch_say("fuxi: %s: warning: cannot remove user-disabled marker (%d); continuing\n",
            torch_prog_name(), errno);
    }
}

static bool torch_cc_list_from_cf(CFTypeRef obj, struct fuxi_cc_id_list *out)
{
    fuxi_cc_list_init(out);
    if (obj == NULL) {
        return true;
    }
    if (CFGetTypeID(obj) != CFArrayGetTypeID()) {
        return false;
    }
    CFArrayRef arr = (CFArrayRef)obj;
    CFIndex n = CFArrayGetCount(arr);
    if (n < 0 || (size_t)n > FUXI_CC_MAX_MODULES) {
        out->overflow = true;
        return false;
    }
    for (CFIndex i = 0; i < n; i++) {
        CFTypeRef item = CFArrayGetValueAtIndex(arr, i);
        char buf[FUXI_CC_MAX_ID_LEN];
        if (item == NULL || CFGetTypeID(item) != CFStringGetTypeID() ||
            !CFStringGetCString((CFStringRef)item, buf, (CFIndex)sizeof(buf), kCFStringEncodingUTF8) ||
            !fuxi_cc_list_append(out, buf)) {
            out->overflow = true;
            return false;
        }
    }
    return true;
}

static id torch_cf_array_from_cc_list(const struct fuxi_cc_id_list *list)
{
    if (list == NULL) {
        return NULL;
    }
    CFMutableArrayRef mut = CFArrayCreateMutable(kCFAllocatorDefault,
        (CFIndex)list->count, &kCFTypeArrayCallBacks);
    if (mut == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < list->count && i < FUXI_CC_MAX_MODULES; i++) {
        CFStringRef str = CFStringCreateWithCString(kCFAllocatorDefault,
            list->items[i], kCFStringEncodingUTF8);
        if (str == NULL) {
            CFRelease(mut);
            return NULL;
        }
        CFArrayAppendValue(mut, str);
        CFRelease(str);
    }
    CFArrayRef imm = CFArrayCreateCopy(kCFAllocatorDefault, mut);
    CFRelease(mut);
    return (id)imm;
}

static bool torch_cc_lists_equal(const struct fuxi_cc_id_list *a, const struct fuxi_cc_id_list *b)
{
    if (a->count != b->count) {
        return false;
    }
    for (size_t i = 0; i < a->count; i++) {
        if (strcmp(a->items[i], b->items[i]) != 0) {
            return false;
        }
    }
    return true;
}

struct torch_cc_load_buf {
    struct fuxi_cc_id_list fixed;
    struct fuxi_cc_id_list enabled_fixed;
    struct fuxi_cc_id_list enabled;
    struct fuxi_cc_id_list disabled;
    struct fuxi_cc_id_list orig_enabled;
    struct fuxi_cc_id_list orig_disabled;
};

static void torch_settings_load(id self, SEL _cmd)
{
    IMP orig = __atomic_load_n(&torch_settings_orig_load, __ATOMIC_ACQUIRE);
    if (orig != NULL && orig != (IMP)torch_settings_load) {
        ((void (*)(id, SEL))orig)(self, _cmd);
    }
    bool ready = torch_ready();
    bool adt_on = torch_cc_fix_enabled();
    if (!ready || !adt_on) {
        if (!__atomic_exchange_n(&torch_cc_gate_logged, 1u, __ATOMIC_ACQ_REL)) {
            torch_say("fuxi: %s: plist guard inactive (torch_ready=%d cc_torch_fix=%d)\n",
                torch_prog_name(), ready ? 1 : 0, adt_on ? 1 : 0);
        }
        return;
    }

    CFTypeRef vis = CFPreferencesCopyAppValue(CFSTR("SBIconVisibility"), CFSTR(FUXI_FLASHLIGHT_MODULE_ID));
    if (vis != NULL) {
        if (CFGetTypeID(vis) == CFBooleanGetTypeID() && !CFBooleanGetValue((CFBooleanRef)vis)) {
            if (!__atomic_exchange_n(&torch_cc_vis_warned, 1u, __ATOMIC_ACQ_REL)) {
                torch_say("fuxi: %s: warning: SBIconVisibility is false for FlashlightModule\n",
                    torch_prog_name());
            }
        }
        CFRelease(vis);
    }

    struct torch_cc_load_buf *st = (struct torch_cc_load_buf *)calloc(1, sizeof(*st));
    if (st == NULL) {
        return;
    }

    Ivar ivar_fixed = __atomic_load_n(&torch_ivar_fixed, __ATOMIC_ACQUIRE);
    Ivar ivar_enabled_fixed = __atomic_load_n(&torch_ivar_enabled_fixed, __ATOMIC_ACQUIRE);
    Ivar ivar_enabled = __atomic_load_n(&torch_ivar_enabled, __ATOMIC_ACQUIRE);
    Ivar ivar_disabled = __atomic_load_n(&torch_ivar_disabled, __ATOMIC_ACQUIRE);
    if (ivar_fixed == NULL || ivar_enabled_fixed == NULL ||
        ivar_enabled == NULL || ivar_disabled == NULL) {
        free(st);
        return;
    }

    id old_fixed = object_getIvar(self, ivar_fixed);
    id old_enabled_fixed = object_getIvar(self, ivar_enabled_fixed);
    id old_enabled = object_getIvar(self, ivar_enabled);
    id old_disabled = object_getIvar(self, ivar_disabled);

    if (!torch_cc_list_from_cf((CFTypeRef)old_fixed, &st->fixed) ||
        !torch_cc_list_from_cf((CFTypeRef)old_enabled_fixed, &st->enabled_fixed) ||
        !torch_cc_list_from_cf((CFTypeRef)old_enabled, &st->enabled) ||
        !torch_cc_list_from_cf((CFTypeRef)old_disabled, &st->disabled)) {
        free(st);
        return;
    }

    st->orig_enabled = st->enabled;
    st->orig_disabled = st->disabled;

    enum fuxi_cc_repair_action act = fuxi_cc_repair_module_lists(
        true, true, torch_cc_marker_exists(),
        &st->fixed, &st->enabled_fixed, &st->enabled, &st->disabled);

    if (act == FUXI_CC_REPAIR_RESTORED || act == FUXI_CC_REPAIR_CLEARED_DISABLED) {
        bool enabled_changed = !torch_cc_lists_equal(&st->enabled, &st->orig_enabled);
        bool disabled_changed = !torch_cc_lists_equal(&st->disabled, &st->orig_disabled);
        id new_enabled = enabled_changed ? torch_cf_array_from_cc_list(&st->enabled) : NULL;
        id new_disabled = disabled_changed ? torch_cf_array_from_cc_list(&st->disabled) : NULL;
        if ((enabled_changed && new_enabled == NULL) ||
            (disabled_changed && new_disabled == NULL)) {
            if (new_enabled != NULL) CFRelease((CFTypeRef)new_enabled);
            if (new_disabled != NULL) CFRelease((CFTypeRef)new_disabled);
            free(st);
            return;
        }
        /*
         * CCSModuleSettingsProvider is RO_IS_ARC with strong ivars:
         * object_setIvar calls objc_storeStrong (retains new_*, releases old_*).
         * Never CFRelease(old_*); release our +1 from CFArrayCreateCopy instead.
         */
        if (enabled_changed) {
            object_setIvar(self, ivar_enabled, new_enabled);
            CFRelease((CFTypeRef)new_enabled);
        }
        if (disabled_changed) {
            object_setIvar(self, ivar_disabled, new_disabled);
            CFRelease((CFTypeRef)new_disabled);
        }
        if (enabled_changed || disabled_changed) {
            ((void (*)(id, SEL))objc_msgSend)(self, sel_registerName("_queue_saveSettings"));
            if (act == FUXI_CC_REPAIR_RESTORED) {
                torch_say("fuxi: %s: restored FlashlightModule in ModuleConfiguration.plist\n",
                    torch_prog_name());
            } else if (enabled_changed && disabled_changed) {
                torch_say("fuxi: %s: deduped enabled and cleared stale FlashlightModule from disabled-module-identifiers\n",
                    torch_prog_name());
            } else if (disabled_changed) {
                torch_say("fuxi: %s: cleared stale FlashlightModule from disabled-module-identifiers\n",
                    torch_prog_name());
            } else {
                torch_say("fuxi: %s: deduped FlashlightModule in ModuleConfiguration.plist\n",
                    torch_prog_name());
            }
        }
    }
    free(st);
}

struct torch_cc_read_ctx {
    id self_obj;
    Ivar ivar_enabled;
    CFArrayRef snapshot;
};

static void torch_cc_read_old_cb(void *arg)
{
    struct torch_cc_read_ctx *ctx = (struct torch_cc_read_ctx *)arg;
    id cur = object_getIvar(ctx->self_obj, ctx->ivar_enabled);
    if (cur != NULL && CFGetTypeID((CFTypeRef)cur) == CFArrayGetTypeID()) {
        ctx->snapshot = CFArrayCreateCopy(kCFAllocatorDefault, (CFArrayRef)cur);
    }
}

struct torch_cc_save_buf {
    struct fuxi_cc_id_list old_list;
    struct fuxi_cc_id_list new_list;
    struct fuxi_cc_id_list patched_list;
};

static void torch_settings_set_and_save(id self, SEL _cmd, id identifiers)
{
    IMP orig = __atomic_load_n(&torch_settings_orig_save, __ATOMIC_ACQUIRE);
    if (orig == NULL || orig == (IMP)torch_settings_set_and_save) {
        return;
    }
    if (!torch_ready() || !torch_cc_fix_enabled() || identifiers == NULL ||
        CFGetTypeID((CFTypeRef)identifiers) != CFArrayGetTypeID()) {
        ((void (*)(id, SEL, id))orig)(self, _cmd, identifiers);
        return;
    }

    Ivar ivar_queue = __atomic_load_n(&torch_ivar_queue, __ATOMIC_ACQUIRE);
    Ivar ivar_enabled = __atomic_load_n(&torch_ivar_enabled, __ATOMIC_ACQUIRE);
    if (ivar_queue == NULL || ivar_enabled == NULL) {
        ((void (*)(id, SEL, id))orig)(self, _cmd, identifiers);
        return;
    }

    id q_obj = object_getIvar(self, ivar_queue);
    if (q_obj == NULL) {
        ((void (*)(id, SEL, id))orig)(self, _cmd, identifiers);
        return;
    }

    struct torch_cc_read_ctx ctx;
    ctx.self_obj = self;
    ctx.ivar_enabled = ivar_enabled;
    ctx.snapshot = NULL;
    dispatch_sync_f((dispatch_queue_t)q_obj, &ctx, torch_cc_read_old_cb);

    struct torch_cc_save_buf *buf = (struct torch_cc_save_buf *)calloc(1, sizeof(*buf));
    if (buf == NULL) {
        if (ctx.snapshot != NULL) CFRelease(ctx.snapshot);
        ((void (*)(id, SEL, id))orig)(self, _cmd, identifiers);
        return;
    }

    bool ok_old = torch_cc_list_from_cf((CFTypeRef)ctx.snapshot, &buf->old_list);
    if (ctx.snapshot != NULL) {
        CFRelease(ctx.snapshot);
        ctx.snapshot = NULL;
    }
    bool ok_new = torch_cc_list_from_cf((CFTypeRef)identifiers, &buf->new_list);
    if (!ok_old || !ok_new) {
        free(buf);
        ((void (*)(id, SEL, id))orig)(self, _cmd, identifiers);
        return;
    }

    enum fuxi_flashlight_process_mode proc_mode = fuxi_flashlight_process_mode_for_name(getprogname());
    bool meta_active = (__atomic_load_n(&torch_metadata_hooked, __ATOMIC_ACQUIRE) != 0u);
    enum fuxi_cc_save_policy policy = fuxi_cc_guard_save_identifiers(
        proc_mode, meta_active, true, true,
        &buf->old_list, &buf->new_list, &buf->patched_list);

    switch (policy) {
    case FUXI_CC_SAVE_ALLOW_MARK_USER_DISABLED:
        if (torch_cc_marker_write()) {
            torch_say("fuxi: %s: user disabled FlashlightModule; marker recorded\n", torch_prog_name());
        } else {
            torch_say("fuxi: %s: warning: user disabled FlashlightModule; marker write failed\n",
                torch_prog_name());
        }
        ((void (*)(id, SEL, id))orig)(self, _cmd, identifiers);
        break;
    case FUXI_CC_SAVE_ALLOW_CLEAR_USER_DISABLED:
        torch_cc_marker_clear();
        torch_say("fuxi: %s: user enabled FlashlightModule; marker cleared\n", torch_prog_name());
        if (!torch_cc_lists_equal(&buf->patched_list, &buf->new_list)) {
            id patched = torch_cf_array_from_cc_list(&buf->patched_list);
            if (patched != NULL) {
                ((void (*)(id, SEL, id))orig)(self, _cmd, patched);
                CFRelease((CFTypeRef)patched);
                break;
            }
        }
        ((void (*)(id, SEL, id))orig)(self, _cmd, identifiers);
        break;
    case FUXI_CC_SAVE_GUARDED_RESTORE: {
        id patched = torch_cf_array_from_cc_list(&buf->patched_list);
        if (patched != NULL) {
            torch_say("fuxi: %s: guarded ModuleConfiguration.plist against removing FlashlightModule\n",
                torch_prog_name());
            ((void (*)(id, SEL, id))orig)(self, _cmd, patched);
            CFRelease((CFTypeRef)patched);
        } else {
            ((void (*)(id, SEL, id))orig)(self, _cmd, identifiers);
        }
        break;
    }
    default:
        ((void (*)(id, SEL, id))orig)(self, _cmd, identifiers);
        break;
    }
    free(buf);
}

bool fuxi_flashlight_settings_hook(void);
bool fuxi_flashlight_settings_hook(void)
{
    if (__atomic_load_n(&torch_settings_hooked, __ATOMIC_ACQUIRE) != 0u) {
        return true;
    }
    if (__atomic_load_n(&torch_settings_refused, __ATOMIC_ACQUIRE) != 0u) {
        return false;
    }
    Class cls = objc_getClass("CCSModuleSettingsProvider");
    if (cls == NULL) {
        return false;
    }

    bool installed_now = false;
    bool refused_now = false;
    bool result = false;

    (void)pthread_mutex_lock(&torch_settings_lock);
    if (__atomic_load_n(&torch_settings_hooked, __ATOMIC_ACQUIRE) != 0u) {
        result = true;
    } else if (__atomic_load_n(&torch_settings_refused, __ATOMIC_ACQUIRE) != 0u) {
        result = false;
    } else {
        Method m_load = class_getInstanceMethod(cls, sel_registerName("_queue_loadSettings"));
        Method m_save = class_getInstanceMethod(cls,
            sel_registerName("setAndSaveOrderedUserEnabledModuleIdentifiers:"));
        Method m_queue_save = class_getInstanceMethod(cls, sel_registerName("_queue_saveSettings"));
        Ivar iv_queue = class_getInstanceVariable(cls, "_queue");
        Ivar iv_fixed = class_getInstanceVariable(cls, "_orderedFixedModuleIdentifiers");
        Ivar iv_enabled = class_getInstanceVariable(cls, "_orderedUserEnabledModuleIdentifiers");
        Ivar iv_enabled_fixed = class_getInstanceVariable(cls, "_orderedUserEnabledFixedModuleIdentifiers");
        Ivar iv_disabled = class_getInstanceVariable(cls, "_userDisabledModuleIdentifiers");

        if (!torch_signature(m_load, 'v', 2, 0) ||
            !torch_signature(m_save, 'v', 3, '@') ||
            !torch_signature(m_queue_save, 'v', 2, 0) ||
            iv_queue == NULL || iv_fixed == NULL || iv_enabled == NULL ||
            iv_enabled_fixed == NULL || iv_disabled == NULL ||
            class_getIvarLayout(cls) == NULL) {
            __atomic_store_n(&torch_settings_refused, 1u, __ATOMIC_RELEASE);
            refused_now = !__atomic_exchange_n(&torch_settings_abi_logged, 1u, __ATOMIC_ACQ_REL);
            result = false;
        } else {
            IMP orig_load = method_getImplementation(m_load);
            IMP orig_save = method_getImplementation(m_save);
            if (orig_load == (IMP)torch_settings_load || orig_save == (IMP)torch_settings_set_and_save) {
                __atomic_store_n(&torch_settings_hooked, 1u, __ATOMIC_RELEASE);
                result = true;
            } else {
                __atomic_store_n(&torch_ivar_queue, iv_queue, __ATOMIC_RELEASE);
                __atomic_store_n(&torch_ivar_fixed, iv_fixed, __ATOMIC_RELEASE);
                __atomic_store_n(&torch_ivar_enabled, iv_enabled, __ATOMIC_RELEASE);
                __atomic_store_n(&torch_ivar_enabled_fixed, iv_enabled_fixed, __ATOMIC_RELEASE);
                __atomic_store_n(&torch_ivar_disabled, iv_disabled, __ATOMIC_RELEASE);
                __atomic_store_n(&torch_settings_orig_load, orig_load, __ATOMIC_RELEASE);
                __atomic_store_n(&torch_settings_orig_save, orig_save, __ATOMIC_RELEASE);
                (void)method_setImplementation(m_load, (IMP)torch_settings_load);
                (void)method_setImplementation(m_save, (IMP)torch_settings_set_and_save);
                __atomic_store_n(&torch_settings_hooked, 1u, __ATOMIC_RELEASE);
                installed_now = true;
                result = true;
            }
        }
    }
    (void)pthread_mutex_unlock(&torch_settings_lock);

    if (refused_now) {
        torch_say("fuxi: %s: ABI mismatch; plist guard refused\n", torch_prog_name());
    }
    if (installed_now) {
        torch_say("fuxi: %s: native CCSModuleSettingsProvider guard ready\n", torch_prog_name());
    }
    return result;
}

static void torch_sb_ui_drain(void *context)
{
    (void)context;
    for (;;) {
        unsigned target_level = 0u;
        id ctrl = NULL;
        (void)pthread_mutex_lock(&torch_coalesce_lock);
        bool has_work = fuxi_torch_coalesce_take(&torch_coalesce, &target_level);
        ctrl = __atomic_load_n(&torch_sb_controller, __ATOMIC_ACQUIRE);
        (void)pthread_mutex_unlock(&torch_coalesce_lock);
        if (!has_work) {
            break;
        }
        Ivar iv_fl = __atomic_load_n(&torch_sb_ivar_flashlight, __ATOMIC_ACQUIRE);
        if (ctrl == NULL || iv_fl == NULL) {
            continue;
        }
        id av_fl = object_getIvar(ctrl, iv_fl);
        if (av_fl == NULL) {
            continue;
        }
        uint64_t gen_snap = __atomic_load_n(&torch_hw_gen, __ATOMIC_ACQUIRE);
        (void)torch_set_kvo_from_ui(av_fl, target_level, gen_snap);
    }
}

static void torch_sb_dispatch_level(id self, unsigned requested, bool is_power_off)
{
    (void)torch_hw_apply(requested, 0u, true, true, NULL, NULL);
    (void)pthread_mutex_lock(&torch_coalesce_lock);
    __atomic_store_n(&torch_sb_controller, self, __ATOMIC_RELEASE);
    bool schedule = fuxi_torch_coalesce_submit(&torch_coalesce, requested, is_power_off);
    dispatch_queue_t q = torch_ui_queue;
    (void)pthread_mutex_unlock(&torch_coalesce_lock);
    if (schedule && q != NULL) {
        dispatch_async_f(q, NULL, torch_sb_ui_drain);
    }
}

static void torch_sb_set_level(id self, SEL _cmd, float level)
{
    (void)_cmd;
    unsigned requested = 0u;
    if (!fuxi_torch_ui_decode(level, &requested)) {
        return;
    }
    torch_sb_dispatch_level(self, requested, requested == 0u);
}

static void torch_sb_turn_power_on(id self, SEL _cmd)
{
    (void)self;
    (void)_cmd;
    /* Wait for the immediately following _setFlashlightLevel: so level=0 is never turned on early. */
}

static void torch_sb_turn_power_off(id self, SEL _cmd)
{
    (void)_cmd;
    torch_sb_dispatch_level(self, 0u, true);
}

static bool fuxi_sbui_flashlight_hook(void)
{
    if (__atomic_load_n(&torch_sb_hooked, __ATOMIC_ACQUIRE)) {
        return true;
    }
    if (__atomic_load_n(&torch_sb_refused, __ATOMIC_ACQUIRE)) {
        return false;
    }
    if (!torch_ready()) {
        return false;
    }
    Class sb_cls = objc_getClass("SBUIFlashlightController");
    if (sb_cls == NULL) {
        return false;
    }

    bool installed_now = false;
    bool refused_now = false;
    bool result = false;

    (void)pthread_mutex_lock(&torch_sb_lock);
    if (__atomic_load_n(&torch_sb_hooked, __ATOMIC_ACQUIRE)) {
        result = true;
    } else if (!__atomic_load_n(&torch_sb_refused, __ATOMIC_ACQUIRE)) {
        Ivar iv_fl = class_getInstanceVariable(sb_cls, "_flashlight");
        Method m_set = class_getInstanceMethod(sb_cls, sel_registerName("_setFlashlightLevel:"));
        Method m_on = class_getInstanceMethod(sb_cls, sel_registerName("_turnPowerOn"));
        Method m_off = class_getInstanceMethod(sb_cls, sel_registerName("_turnPowerOff"));
        const char *ivar_enc = iv_fl != NULL ? ivar_getTypeEncoding(iv_fl) : NULL;
        bool ivar_ok = (iv_fl != NULL && ivar_enc != NULL && ivar_enc[0] == '@' &&
            class_getIvarLayout(sb_cls) != NULL);

        if (!ivar_ok ||
            !torch_signature(m_set, 'v', 3, 'f') ||
            !torch_signature(m_on, 'v', 2, 0) ||
            !torch_signature(m_off, 'v', 2, 0)) {
            __atomic_store_n(&torch_sb_refused, 1u, __ATOMIC_RELEASE);
            refused_now = !__atomic_exchange_n(&torch_sb_abi_logged, 1u, __ATOMIC_ACQ_REL);
        } else {
            dispatch_queue_attr_t attr = dispatch_queue_attr_make_with_qos_class(
                DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INTERACTIVE, 0);
            dispatch_queue_t q = dispatch_queue_create("com.fuxi.flashlight.ui", attr);
            if (q != NULL) {
                (void)pthread_mutex_lock(&torch_coalesce_lock);
                fuxi_torch_coalesce_init(&torch_coalesce);
                torch_ui_queue = q;
                (void)pthread_mutex_unlock(&torch_coalesce_lock);
                __atomic_store_n(&torch_sb_ivar_flashlight, iv_fl, __ATOMIC_RELEASE);
                (void)method_setImplementation(m_set, (IMP)torch_sb_set_level);
                (void)method_setImplementation(m_on, (IMP)torch_sb_turn_power_on);
                (void)method_setImplementation(m_off, (IMP)torch_sb_turn_power_off);
                __atomic_store_n(&torch_sb_hooked, 1u, __ATOMIC_RELEASE);
                installed_now = true;
                result = true;
            }
        }
    }
    (void)pthread_mutex_unlock(&torch_sb_lock);

    if (refused_now) {
        torch_log("fuxi: SpringBoard: SBUIFlashlightController ABI mismatch; utility queue left as-is\n");
    }
    if (installed_now) {
        torch_log("fuxi: SpringBoard: SBUIFlashlightController immediate dispatch + coalescing ready (USER_INTERACTIVE)\n");
    }
    return result;
}

bool fuxi_flashlight_hook(void);
bool fuxi_flashlight_hook(void)
{
    (void)fuxi_flashlight_metadata_hook();
    (void)fuxi_flashlight_settings_hook();
    if (fuxi_flashlight_process_mode_for_name(getprogname()) != FUXI_FLASHLIGHT_MODE_FULL) {
        return false;
    }
    (void)fuxi_sbui_flashlight_hook();
    if (__atomic_load_n(&torch_hooked, __ATOMIC_ACQUIRE)) return true;
    if (!torch_ready()) return false;
    Class target = objc_getClass("AVFlashlight");
    if (target == NULL) return false;
    const struct {
        const char *name;
        IMP replacement;
        char result;
        unsigned count;
        char third;
    } hooks[] = {
        {"init", (IMP)torch_init, '@', 2, 0},
        {"isAvailable", (IMP)torch_available, 'B', 2, 0},
        {"isOverheated", (IMP)torch_overheated, 'B', 2, 0},
        {"flashlightLevel", (IMP)torch_get, 'f', 2, 0},
        {"setFlashlightLevel:withError:", (IMP)torch_set, 'B', 4, 'f'},
        {"turnPowerOnWithError:", (IMP)torch_power_on, 'B', 3, '^'},
        {"turnPowerOff", (IMP)torch_power_off, 'v', 2, 0},
    };
    Method methods[sizeof(hooks) / sizeof(hooks[0])];
    Method present = class_getClassMethod(target, sel_registerName("hasFlashlight"));
    if (!torch_signature(present, 'B', 2, 0)) return false;
    for (unsigned index = 0; index < sizeof(hooks) / sizeof(hooks[0]); index++) {
        methods[index] = class_getInstanceMethod(target, sel_registerName(hooks[index].name));
        if (!torch_signature(methods[index], hooks[index].result, hooks[index].count, hooks[index].third)) {
            torch_log("fuxi: SpringBoard: AVFlashlight ABI mismatch; torch hooks refused\n");
            return false;
        }
    }
    torch_timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0,
        dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0));
    if (torch_timer == NULL) return false;
    if (__atomic_exchange_n(&torch_hooked, 1u, __ATOMIC_ACQ_REL)) return true;
    (void)method_setImplementation(present, (IMP)torch_available);
    for (unsigned index = 0; index < sizeof(hooks) / sizeof(hooks[0]); index++)
        (void)method_setImplementation(methods[index], hooks[index].replacement);
    dispatch_source_set_event_handler_f(torch_timer, torch_renew);
    dispatch_source_set_timer(torch_timer, dispatch_time(DISPATCH_TIME_NOW, 500 * NSEC_PER_MSEC),
        500 * NSEC_PER_MSEC, 50 * NSEC_PER_MSEC);
    dispatch_resume(torch_timer);
    torch_log("fuxi: SpringBoard: native AVFlashlight hooks ready; 500ms HIGH heartbeat\n");
    return true;
}
