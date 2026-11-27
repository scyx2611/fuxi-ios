/*
 * Route 2 AppStore.app media proxy + T6 hybrid read-only diagnostic probes.
 *
 * Design & references (AGENTS.md section 0):
 *  - Reuses the 4-library link allowlist (libSystem.B.dylib, libobjc.A.dylib,
 *    CoreFoundation, IOKit) and IODeviceTree:/chosen u32 gate pattern from
 *    FuxiAtxFix.c / FuxiRtcPersist.c (no Foundation dependency, 0 __interpose).
 *  - Reuses pure-C URL/header/gate helpers from FuxiAppStoreProxy.h and safe
 *    objc_lookUpClass + _dyld_register_func_for_add_image swizzling pattern
 *    from FuxiAppStoreDiag.h.
 *  - Privacy-hardened for T6 login/purchase flows: URLs are always truncated at
 *    '?' and '#' via apsd_cfurl_host_path(), Cookie/auth headers are stripped on
 *    rewritten public web endpoints, header presence probes emit only 0/1 flags,
 *    and CFError formatting only emits domain:code, whitelisted numeric status
 *    codes, and recursive NSUnderlyingError/NSMultipleUnderlyingErrorsKey chains
 *    (never reading NSLocalizedDescription or NSErrorFailingURLStringKey).
 */
#include <CoreFoundation/CoreFoundation.h>
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <objc/runtime.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "FuxiAppStoreProxy.h"

typedef mach_port_t fx_as_io_object_t;
extern fx_as_io_object_t IORegistryEntryFromPath(mach_port_t main_port, const char *path);
extern CFTypeRef IORegistryEntryCreateCFProperty(fx_as_io_object_t entry, CFStringRef key,
    CFAllocatorRef allocator, uint32_t options);
extern kern_return_t IOObjectRelease(fx_as_io_object_t object);

#define APSD_CHUNK 210u
#define APSD_TEXT_MAX 880u
#define APSD_MAX_SEEN_METHODS 64u

static bool apsd_is_appstore;
static unsigned apsd_log_mode;
static unsigned apsd_fix_mode;
static unsigned apsd_hybrid_mode;
static id apsd_cached_synth_token;
static unsigned apsd_seq;
static __thread unsigned apsd_busy;
static pthread_mutex_t apsd_lock = PTHREAD_MUTEX_INITIALIZER;

static Method apsd_seen_methods[APSD_MAX_SEEN_METHODS];
static unsigned apsd_seen_count;

/* Per-category log caps */
static unsigned apsd_proxy_tok_calls;
static unsigned apsd_proxy_rw_calls;
static unsigned apsd_proxy_img_calls;
static unsigned apsd_url_done_calls;
static unsigned apsd_url_key_done_calls;
static unsigned apsd_nsurl_req_calls;
static unsigned apsd_mescal_calls;
static unsigned apsd_mescal_req_calls;
static struct fuxi_appstore_mescal_sign_state apsd_mescal_fp_sign_state;
static struct fuxi_appstore_mescal_sign_state apsd_mescal_sess_sign_state;
static unsigned apsd_sap_calls;
static unsigned apsd_bag_calls;
static unsigned apsd_promise_calls;
static unsigned apsd_promise_204_calls;
static unsigned apsd_auth_calls;
static unsigned apsd_purchase_calls;

/* Install flags */
static bool apsd_inst_token_fetch;
static bool apsd_inst_token_cached;
static bool apsd_inst_nsurl_task[2];
static bool apsd_inst_nsurl_task_ch[2];
static bool apsd_inst_ams_url_done;
static bool apsd_inst_sap_exch;
static bool apsd_inst_sap_sign;
static bool apsd_inst_mescal_init;
static bool apsd_inst_mescal_exch;
static bool apsd_inst_mescal_sign;
static bool apsd_inst_mescal_prime;
static bool apsd_inst_mescal_sig_req;
static bool apsd_inst_msess_est_b;
static bool apsd_inst_msess_est_id;
static bool apsd_inst_msess_cert;
static bool apsd_inst_msess_post;
static bool apsd_inst_msess_sign;
static bool apsd_inst_bag_fetch_v;
static bool apsd_inst_bag_fetch_id;
static bool apsd_inst_bag_fail;
static bool apsd_inst_promise_b;
static bool apsd_inst_promise_v;
static bool apsd_inst_promise_res_b;
static bool apsd_inst_promise_res_v;
static bool apsd_inst_auth_perform;
static bool apsd_inst_purchase_perform;

/* Original IMP slots */
static IMP apsd_orig_token_fetch;
static IMP apsd_orig_token_cached;
static IMP apsd_orig_nsurl_task[2];
static IMP apsd_orig_nsurl_task_ch[2];
static IMP apsd_orig_ams_url_done;
static IMP apsd_orig_sap_exch;
static IMP apsd_orig_sap_sign;
static IMP apsd_orig_mescal_init;
static IMP apsd_orig_mescal_exch;
static IMP apsd_orig_mescal_sign;
static IMP apsd_orig_mescal_prime;
static IMP apsd_orig_mescal_sig_req;
static IMP apsd_orig_msess_est_b;
static IMP apsd_orig_msess_est_id;
static IMP apsd_orig_msess_cert;
static IMP apsd_orig_msess_post;
static IMP apsd_orig_msess_sign;
static IMP apsd_orig_bag_fetch_v;
static IMP apsd_orig_bag_fetch_id;
static IMP apsd_orig_bag_fail;
static IMP apsd_orig_promise_b;
static IMP apsd_orig_promise_v;
static IMP apsd_orig_promise_res_b;
static IMP apsd_orig_promise_res_v;
static IMP apsd_orig_auth_perform;
static IMP apsd_orig_purchase_perform;

static void apsd_sanitize(char *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (c < 0x20u || c == 0x7fu) {
            buf[i] = ' ';
        }
    }
}

static void apsd_write(const char *kind, char *text, size_t len)
{
    if (__atomic_load_n(&apsd_log_mode, __ATOMIC_RELAXED) == 0u || len == 0u) {
        return;
    }
    if (len >= APSD_TEXT_MAX) {
        len = APSD_TEXT_MAX - 1u;
    }
    apsd_sanitize(text, len);
    const unsigned seq = __atomic_add_fetch(&apsd_seq, 1u, __ATOMIC_RELAXED);
    char line[256];
    size_t offset = 0;
    unsigned part = 0;
    do {
        size_t chunk = len - offset;
        if (chunk > APSD_CHUNK) {
            chunk = APSD_CHUNK;
        }
        int used = snprintf(line, sizeof(line), "%s %u %s %.*s\n",
            part ? "APSD+" : "APSD", seq, kind, (int)chunk, text + offset);
        if (used > 0) {
            size_t n = (size_t)used < sizeof(line) ? (size_t)used : sizeof(line) - 1u;
            (void)write(2, line, n);
        }
        offset += chunk;
        part++;
    } while (offset < len);
}

static void apsd_logf(const char *kind, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void apsd_logf(const char *kind, const char *fmt, ...)
{
    if (__atomic_load_n(&apsd_log_mode, __ATOMIC_RELAXED) == 0u) {
        return;
    }
    char text[APSD_TEXT_MAX];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    if (n > 0) {
        size_t len = (size_t)n < sizeof(text) ? (size_t)n : sizeof(text) - 1u;
        apsd_write(kind, text, len);
    }
}

static bool apsd_type_matches(const char *enc, char expected)
{
    if (enc == NULL) {
        return false;
    }
    while (*enc && strchr("rnNoORV", *enc) != NULL) {
        enc++;
    }
    if (expected == 'b') {
        return *enc == 'B' || *enc == 'c';
    }
    if (expected == 'i') {
        return *enc == 'q' || *enc == 'Q' || *enc == 'l' || *enc == 'L' || *enc == 'i' || *enc == 'I';
    }
    if (expected == 'p') {
        return *enc == '^';
    }
    return *enc == expected;
}

static bool apsd_method_already_seen(Method m)
{
    for (unsigned i = 0; i < apsd_seen_count; i++) {
        if (apsd_seen_methods[i] == m) {
            return true;
        }
    }
    if (apsd_seen_count < APSD_MAX_SEEN_METHODS) {
        apsd_seen_methods[apsd_seen_count++] = m;
    }
    return false;
}

static id apsd_objc0(id obj, const char *sel_name)
{
    if (obj == NULL) {
        return NULL;
    }
    SEL sel = sel_registerName(sel_name);
    Method m = class_getInstanceMethod(object_getClass(obj), sel);
    if (m == NULL || method_getNumberOfArguments(m) != 2) {
        return NULL;
    }
    char rtype[32];
    method_getReturnType(m, rtype, sizeof(rtype));
    if (!apsd_type_matches(rtype, '@')) {
        return NULL;
    }
    return ((id (*)(id, SEL))method_getImplementation(m))(obj, sel);
}

static void apsd_objc_void0(id obj, const char *sel_name)
{
    if (obj == NULL) {
        return;
    }
    SEL sel = sel_registerName(sel_name);
    Method m = class_getInstanceMethod(object_getClass(obj), sel);
    if (m == NULL || method_getNumberOfArguments(m) != 2) {
        return;
    }
    char rtype[32];
    method_getReturnType(m, rtype, sizeof(rtype));
    if (!apsd_type_matches(rtype, 'v')) {
        return;
    }
    ((void (*)(id, SEL))method_getImplementation(m))(obj, sel);
}

static void apsd_objc_void1(id obj, const char *sel_name, id a2)
{
    if (obj == NULL) {
        return;
    }
    SEL sel = sel_registerName(sel_name);
    Method m = class_getInstanceMethod(object_getClass(obj), sel);
    if (m == NULL || method_getNumberOfArguments(m) != 3) {
        return;
    }
    char rtype[32], t2[32];
    method_getReturnType(m, rtype, sizeof(rtype));
    method_getArgumentType(m, 2u, t2, sizeof(t2));
    if (!apsd_type_matches(rtype, 'v') || !apsd_type_matches(t2, '@')) {
        return;
    }
    ((void (*)(id, SEL, id))method_getImplementation(m))(obj, sel, a2);
}

static void apsd_objc_void2(id obj, const char *sel_name, id a2, id a3)
{
    if (obj == NULL) {
        return;
    }
    SEL sel = sel_registerName(sel_name);
    Method m = class_getInstanceMethod(object_getClass(obj), sel);
    if (m == NULL || method_getNumberOfArguments(m) != 4) {
        return;
    }
    char rtype[32], t2[32], t3[32];
    method_getReturnType(m, rtype, sizeof(rtype));
    method_getArgumentType(m, 2u, t2, sizeof(t2));
    method_getArgumentType(m, 3u, t3, sizeof(t3));
    if (!apsd_type_matches(rtype, 'v') || !apsd_type_matches(t2, '@') || !apsd_type_matches(t3, '@')) {
        return;
    }
    ((void (*)(id, SEL, id, id))method_getImplementation(m))(obj, sel, a2, a3);
}

static id apsd_objc_cls0(Class cls, const char *sel_name)
{
    if (cls == NULL) {
        return NULL;
    }
    SEL sel = sel_registerName(sel_name);
    Method m = class_getClassMethod(cls, sel);
    if (m == NULL || method_getNumberOfArguments(m) != 2) {
        return NULL;
    }
    char rtype[32];
    method_getReturnType(m, rtype, sizeof(rtype));
    if (!apsd_type_matches(rtype, '@')) {
        return NULL;
    }
    return ((id (*)(id, SEL))method_getImplementation(m))((id)cls, sel);
}

static id apsd_objc_cls1(Class cls, const char *sel_name, id a2)
{
    if (cls == NULL) {
        return NULL;
    }
    SEL sel = sel_registerName(sel_name);
    Method m = class_getClassMethod(cls, sel);
    if (m == NULL || method_getNumberOfArguments(m) != 3) {
        return NULL;
    }
    char rtype[32], t2[32];
    method_getReturnType(m, rtype, sizeof(rtype));
    method_getArgumentType(m, 2u, t2, sizeof(t2));
    if (!apsd_type_matches(rtype, '@') || !apsd_type_matches(t2, '@')) {
        return NULL;
    }
    return ((id (*)(id, SEL, id))method_getImplementation(m))((id)cls, sel, a2);
}

static intptr_t apsd_int0(id obj, const char *sel_name, bool *ok_out)
{
    if (ok_out) {
        *ok_out = false;
    }
    if (obj == NULL) {
        return -1;
    }
    SEL sel = sel_registerName(sel_name);
    Method m = class_getInstanceMethod(object_getClass(obj), sel);
    if (m == NULL || method_getNumberOfArguments(m) != 2) {
        return -1;
    }
    char rtype[32];
    method_getReturnType(m, rtype, sizeof(rtype));
    const char *p = rtype;
    while (*p && strchr("rnNoORV", *p) != NULL) {
        p++;
    }
    IMP imp = method_getImplementation(m);
    if (ok_out) {
        *ok_out = true;
    }
    switch (*p) {
    case 'c': return ((int8_t (*)(id, SEL))imp)(obj, sel);
    case 'C': return ((uint8_t (*)(id, SEL))imp)(obj, sel);
    case 's': return ((int16_t (*)(id, SEL))imp)(obj, sel);
    case 'S': return ((uint16_t (*)(id, SEL))imp)(obj, sel);
    case 'i': return ((int32_t (*)(id, SEL))imp)(obj, sel);
    case 'I': return (intptr_t)((uint32_t (*)(id, SEL))imp)(obj, sel);
    case 'l':
    case 'q': return ((intptr_t (*)(id, SEL))imp)(obj, sel);
    case 'L':
    case 'Q': return (intptr_t)((uintptr_t (*)(id, SEL))imp)(obj, sel);
    default:
        if (ok_out) {
            *ok_out = false;
        }
        return -1;
    }
}

static size_t apsd_cfstr(const void *obj, char *out, size_t cap)
{
    if (out == NULL || cap < 2u) {
        return 0;
    }
    out[0] = 0;
    if (obj == NULL || CFGetTypeID((CFTypeRef)obj) != CFStringGetTypeID()) {
        return 0;
    }
    CFIndex used = 0;
    CFStringGetBytes((CFStringRef)obj, CFRangeMake(0, CFStringGetLength((CFStringRef)obj)),
        kCFStringEncodingUTF8, '?', false, (UInt8 *)out, (CFIndex)cap - 1, &used);
    if (used < 0) {
        used = 0;
    }
    out[used] = 0;
    return (size_t)used;
}

static long apsd_cfdata_len(const void *obj)
{
    if (obj == NULL) {
        return -1;
    }
    if (CFGetTypeID((CFTypeRef)obj) != CFDataGetTypeID()) {
        return -2;
    }
    return (long)CFDataGetLength((CFDataRef)obj);
}

static long apsd_cfstr_or_data_len(const void *obj)
{
    if (obj == NULL) {
        return -1;
    }
    CFTypeID tid = CFGetTypeID((CFTypeRef)obj);
    if (tid == CFStringGetTypeID()) {
        return (long)CFStringGetLength((CFStringRef)obj);
    }
    if (tid == CFDataGetTypeID()) {
        return (long)CFDataGetLength((CFDataRef)obj);
    }
    return -2;
}

static bool apsd_cfstr_exact(const void *obj, char *out, size_t cap)
{
    if (out == NULL || cap < 2u) {
        return false;
    }
    out[0] = 0;
    if (obj == NULL || CFGetTypeID((CFTypeRef)obj) != CFStringGetTypeID()) {
        return false;
    }
    CFStringRef s = (CFStringRef)obj;
    CFIndex len = CFStringGetLength(s);
    if (len < 0 || (size_t)len >= cap) {
        return false;
    }
    if (!CFStringGetCString(s, out, (CFIndex)cap, kCFStringEncodingUTF8)) {
        out[0] = 0;
        return false;
    }
    return true;
}

/* Always truncates '?' and '#' and strips userinfo before outputting logs */
static void apsd_cfurl_host_path(const void *url_obj, char *out, size_t cap)
{
    if (out == NULL || cap < 2u) {
        return;
    }
    out[0] = 0;
    if (url_obj == NULL) {
        snprintf(out, cap, "(null)");
        return;
    }
    CFTypeID tid = CFGetTypeID((CFTypeRef)url_obj);
    CFStringRef raw_str = NULL;
    if (tid == CFURLGetTypeID()) {
        raw_str = CFURLGetString((CFURLRef)url_obj);
    } else if (tid == CFStringGetTypeID()) {
        raw_str = (CFStringRef)url_obj;
    } else {
        snprintf(out, cap, "<url>");
        return;
    }
    if (raw_str == NULL) {
        snprintf(out, cap, "<url>");
        return;
    }

    char raw[4096];
    raw[0] = 0;
    if (apsd_cfstr_exact(raw_str, raw, sizeof(raw))) {
        if (!fuxi_appstore_mask_url_host_path_bounded(raw, false, out, cap)) {
            snprintf(out, cap, "<url>");
        }
        return;
    }

    /* Oversize URL (>4095B): never pass truncated raw string into masker.
     * For absolute CFURLRef without baseURL, extract only Scheme/HostName/Path
     * (never UserName/Password/Query/Fragment); otherwise emit redacted marker. */
    if (tid == CFURLGetTypeID() && CFURLGetBaseURL((CFURLRef)url_obj) == NULL) {
        CFStringRef cf_scheme = CFURLCopyScheme((CFURLRef)url_obj);
        CFStringRef cf_host = CFURLCopyHostName((CFURLRef)url_obj);
        CFStringRef cf_path = CFURLCopyPath((CFURLRef)url_obj);
        char s_buf[32];
        char h_buf[256];
        char p_buf[1024];
        s_buf[0] = 0;
        h_buf[0] = 0;
        p_buf[0] = 0;
        bool parts_ok = (cf_scheme != NULL && cf_host != NULL &&
            apsd_cfstr_exact(cf_scheme, s_buf, sizeof(s_buf)) &&
            apsd_cfstr_exact(cf_host, h_buf, sizeof(h_buf)) &&
            (cf_path == NULL || apsd_cfstr_exact(cf_path, p_buf, sizeof(p_buf))));
        if (cf_scheme != NULL) {
            CFRelease(cf_scheme);
        }
        if (cf_host != NULL) {
            CFRelease(cf_host);
        }
        if (cf_path != NULL) {
            CFRelease(cf_path);
        }
        if (parts_ok && s_buf[0] != '\0' && h_buf[0] != '\0' &&
            strchr(h_buf, '@') == NULL && strchr(h_buf, '/') == NULL &&
            strchr(h_buf, '?') == NULL && strchr(h_buf, '#') == NULL) {
            char composed[1400];
            int n = snprintf(composed, sizeof(composed), "%s://%s%s", s_buf, h_buf, p_buf);
            if (n > 0 && (size_t)n < sizeof(composed) &&
                fuxi_appstore_mask_url_host_path_bounded(composed, false, out, cap)) {
                return;
            }
        }
    }

    if (!fuxi_appstore_mask_url_host_path_bounded(NULL, true, out, cap)) {
        snprintf(out, cap, "<url>");
    }
}

static bool apsd_req_url_raw(id req, char *out, size_t cap, bool *oversize_out)
{
    if (oversize_out != NULL) {
        *oversize_out = false;
    }
    if (out == NULL || cap < 2u) {
        return false;
    }
    out[0] = 0;
    if (req == NULL) {
        return false;
    }
    id url = apsd_objc0(req, "URL");
    if (url == NULL) {
        return false;
    }
    CFTypeID tid = CFGetTypeID((CFTypeRef)url);
    CFStringRef ustr = NULL;
    if (tid == CFURLGetTypeID()) {
        ustr = CFURLGetString((CFURLRef)url);
    } else if (tid == CFStringGetTypeID()) {
        ustr = (CFStringRef)url;
    } else {
        return false;
    }
    if (ustr == NULL) {
        return false;
    }
    if (!apsd_cfstr_exact(ustr, out, cap)) {
        if (oversize_out != NULL) {
            *oversize_out = true;
        }
        out[0] = 0;
        return false;
    }
    return out[0] != 0;
}

/* Emits strictly 0/1 presence booleans for sensitive signature/Anisette headers; never reads values */
static void apsd_req_hdr_flags(id req, unsigned *has_sig, unsigned *has_md, unsigned *has_mdm)
{
    *has_sig = 0u;
    *has_md = 0u;
    *has_mdm = 0u;
    if (req == NULL) {
        return;
    }
    id hdrs = apsd_objc0(req, "allHTTPHeaderFields");
    if (hdrs == NULL || CFGetTypeID((CFTypeRef)hdrs) != CFDictionaryGetTypeID()) {
        return;
    }
    CFIndex cnt = CFDictionaryGetCount((CFDictionaryRef)hdrs);
    if (cnt <= 0 || cnt > (CFIndex)FUXI_APPSTORE_MAX_HDR_SCAN) {
        return;
    }
    const void *keys[FUXI_APPSTORE_MAX_HDR_SCAN];
    const void *vals[FUXI_APPSTORE_MAX_HDR_SCAN];
    char kbufs[FUXI_APPSTORE_MAX_HDR_SCAN][64];
    struct fuxi_appstore_hdr_entry entries[FUXI_APPSTORE_MAX_HDR_SCAN];
    CFDictionaryGetKeysAndValues((CFDictionaryRef)hdrs, keys, vals);
    for (CFIndex i = 0; i < cnt; i++) {
        kbufs[i][0] = 0;
        size_t klen = apsd_cfstr(keys[i], kbufs[i], sizeof(kbufs[i]));
        bool nonempty = false;
        if (klen > 0u && vals[i] != NULL) {
            nonempty = true;
            if (CFGetTypeID((CFTypeRef)vals[i]) == CFStringGetTypeID()) {
                nonempty = (CFStringGetLength((CFStringRef)vals[i]) > 0);
            }
        }
        entries[i].name = kbufs[i];
        entries[i].name_len = klen;
        entries[i].nonempty = nonempty;
    }
    struct fuxi_appstore_hdr_flags flags;
    if (fuxi_appstore_scan_hdr_flags(entries, (size_t)cnt, &flags)) {
        *has_sig = flags.has_sig;
        *has_md = flags.has_md;
        *has_mdm = flags.has_mdm;
    }
}

static void apsd_task_url_status_hdrs(id task, char *url_out, size_t url_cap, long *status_out,
    unsigned *has_sig, unsigned *has_md, unsigned *has_mdm)
{
    *status_out = -1;
    *has_sig = 0u;
    *has_md = 0u;
    *has_mdm = 0u;
    if (task == NULL) {
        snprintf(url_out, url_cap, "(nil-task)");
        return;
    }
    id cur_req = apsd_objc0(task, "currentRequest");
    id orig_req = apsd_objc0(task, "originalRequest");
    id url_req = orig_req != NULL ? orig_req : cur_req;
    id hdr_req = cur_req != NULL ? cur_req : orig_req;
    if (url_req == NULL) {
        snprintf(url_out, url_cap, "(nil-req)");
    } else {
        id url = apsd_objc0(url_req, "URL");
        apsd_cfurl_host_path((const void *)url, url_out, url_cap);
    }
    if (hdr_req != NULL) {
        apsd_req_hdr_flags(hdr_req, has_sig, has_md, has_mdm);
    }
    id resp = apsd_objc0(task, "response");
    if (resp != NULL) {
        bool ok = false;
        intptr_t sc = apsd_int0(resp, "statusCode", &ok);
        if (ok) {
            *status_out = (long)sc;
        }
    }
}

static bool apsd_err_dom_code(const void *err_obj, char *dom_out, size_t dom_cap, long *code_out)
{
    if (dom_out != NULL && dom_cap > 0u) {
        dom_out[0] = 0;
    }
    if (code_out != NULL) {
        *code_out = 0L;
    }
    if (err_obj == NULL || CFGetTypeID((CFTypeRef)err_obj) != CFErrorGetTypeID()) {
        return false;
    }
    CFErrorRef err = (CFErrorRef)err_obj;
    if (dom_out != NULL && dom_cap > 1u) {
        apsd_cfstr(CFErrorGetDomain(err), dom_out, dom_cap);
    }
    if (code_out != NULL) {
        *code_out = (long)CFErrorGetCode(err);
    }
    return true;
}

static bool apsd_cf_is_err(const void *obj)
{
    return obj != NULL && CFGetTypeID((CFTypeRef)obj) == CFErrorGetTypeID();
}

static void apsd_cf_get_dom_code(const void *err_obj, char *dom_out, size_t dom_cap, long *code_out)
{
    (void)apsd_err_dom_code(err_obj, dom_out, dom_cap, code_out);
}

static const void *apsd_cf_copy_userinfo(const void *err_obj)
{
    return (const void *)CFErrorCopyUserInfo((CFErrorRef)err_obj);
}

static void apsd_cf_release_userinfo(const void *info_obj)
{
    if (info_obj != NULL) {
        CFRelease((CFTypeRef)info_obj);
    }
}

static bool apsd_cf_for_each_kv(const void *info_obj, fuxi_appstore_kv_cb cb, void *ctx)
{
    if (info_obj == NULL || CFGetTypeID((CFTypeRef)info_obj) != CFDictionaryGetTypeID()) {
        return true;
    }
    CFDictionaryRef dict = (CFDictionaryRef)info_obj;
    CFIndex cnt = CFDictionaryGetCount(dict);
    if (cnt <= 0) {
        return true;
    }
    if (cnt > (CFIndex)FUXI_APPSTORE_ERR_MAX_SCAN_KEYS) {
        return false;
    }
    const void *keys[FUXI_APPSTORE_ERR_MAX_SCAN_KEYS];
    const void *vals[FUXI_APPSTORE_ERR_MAX_SCAN_KEYS];
    CFDictionaryGetKeysAndValues(dict, keys, vals);
    for (CFIndex i = 0; i < cnt; i++) {
        char kbuf[64];
        kbuf[0] = 0;
        if (apsd_cfstr(keys[i], kbuf, sizeof(kbuf)) > 0u) {
            cb(kbuf, vals[i], ctx);
        }
    }
    return true;
}

static bool apsd_cf_lookup_key(const void *info_obj, const char *key, const void **val_out)
{
    if (info_obj == NULL || key == NULL || val_out == NULL ||
        CFGetTypeID((CFTypeRef)info_obj) != CFDictionaryGetTypeID()) {
        return false;
    }
    CFStringRef cf_key = NULL;
    if (strcmp(key, "AMSStatusCode") == 0) {
        cf_key = CFSTR("AMSStatusCode");
    } else if (strcmp(key, "AMSServerErrorCode") == 0) {
        cf_key = CFSTR("AMSServerErrorCode");
    } else if (strcmp(key, "AKAnisetteError") == 0) {
        cf_key = CFSTR("AKAnisetteError");
    } else if (strcmp(key, "AKUnderlyingADIErrors") == 0) {
        cf_key = CFSTR("AKUnderlyingADIErrors");
    } else if (strcmp(key, "NSUnderlyingError") == 0) {
        cf_key = CFSTR("NSUnderlyingError");
    } else if (strcmp(key, "NSMultipleUnderlyingErrorsKey") == 0) {
        cf_key = CFSTR("NSMultipleUnderlyingErrorsKey");
    }
    if (cf_key == NULL) {
        return false;
    }
    *val_out = CFDictionaryGetValue((CFDictionaryRef)info_obj, cf_key);
    return *val_out != NULL;
}

static bool apsd_cf_read_int64(const void *val_obj, int64_t *out_val)
{
    if (val_obj == NULL || out_val == NULL || CFGetTypeID((CFTypeRef)val_obj) != CFNumberGetTypeID()) {
        return false;
    }
    return CFNumberGetValue((CFNumberRef)val_obj, kCFNumberSInt64Type, out_val) != false;
}

static bool apsd_cf_is_array(const void *val_obj)
{
    return val_obj != NULL && CFGetTypeID((CFTypeRef)val_obj) == CFArrayGetTypeID();
}

static size_t apsd_cf_array_count(const void *arr_obj)
{
    if (arr_obj == NULL || CFGetTypeID((CFTypeRef)arr_obj) != CFArrayGetTypeID()) {
        return 0u;
    }
    CFIndex cnt = CFArrayGetCount((CFArrayRef)arr_obj);
    return cnt > 0 ? (size_t)cnt : 0u;
}

static const void *apsd_cf_array_item(const void *arr_obj, size_t index)
{
    if (arr_obj == NULL || CFGetTypeID((CFTypeRef)arr_obj) != CFArrayGetTypeID()) {
        return NULL;
    }
    CFIndex cnt = CFArrayGetCount((CFArrayRef)arr_obj);
    if (cnt <= 0 || index >= (size_t)cnt) {
        return NULL;
    }
    return CFArrayGetValueAtIndex((CFArrayRef)arr_obj, (CFIndex)index);
}

/* Strictly emits domain:code, whitelisted numeric status codes, and recursive CFError chains only. */
static void apsd_fmt_err(const void *err_obj, char *out, size_t cap)
{
    const struct fuxi_appstore_err_ops ops = {
        .is_err = apsd_cf_is_err,
        .get_dom_code = apsd_cf_get_dom_code,
        .copy_userinfo = apsd_cf_copy_userinfo,
        .release_userinfo = apsd_cf_release_userinfo,
        .for_each_kv = apsd_cf_for_each_kv,
        .lookup_key = apsd_cf_lookup_key,
        .read_int64 = apsd_cf_read_int64,
        .is_array = apsd_cf_is_array,
        .array_count = apsd_cf_array_count,
        .array_item = apsd_cf_array_item,
    };
    fuxi_appstore_format_err_depth(err_obj, &ops, out, cap, 0);
}

/* ---- Route 2 UI browsing hooks ---- */

static id apsd_get_synth_media_token(void)
{
    id tok = __atomic_load_n(&apsd_cached_synth_token, __ATOMIC_ACQUIRE);
    if (tok != NULL) {
        return tok;
    }
    pthread_mutex_lock(&apsd_lock);
    tok = __atomic_load_n(&apsd_cached_synth_token, __ATOMIC_RELAXED);
    if (tok == NULL) {
        Class cls = objc_lookUpClass("AMSMediaToken");
        SEL init_sel = sel_registerName("initWithString:expirationDate:lifetime:valid:");
        Method m = cls ? class_getInstanceMethod(cls, init_sel) : NULL;
        if (cls != NULL && m != NULL && method_getNumberOfArguments(m) == 6) {
            char rtype[32], a2[32], a3[32], a4[32], a5[32];
            method_getReturnType(m, rtype, sizeof(rtype));
            method_getArgumentType(m, 2u, a2, sizeof(a2));
            method_getArgumentType(m, 3u, a3, sizeof(a3));
            method_getArgumentType(m, 4u, a4, sizeof(a4));
            method_getArgumentType(m, 5u, a5, sizeof(a5));
            if (apsd_type_matches(rtype, '@') && apsd_type_matches(a2, '@') &&
                apsd_type_matches(a3, '@') && apsd_type_matches(a4, 'd') &&
                apsd_type_matches(a5, 'b')) {
                id raw = apsd_objc_cls0(cls, "alloc");
                if (raw != NULL) {
                    CFDateRef exp = CFDateCreate(kCFAllocatorDefault, CFAbsoluteTimeGetCurrent() + 2592000.0);
                    if (exp != NULL) {
                        typedef id (*apsd_tok_init_fn)(id, SEL, id, id, double, bool);
                        apsd_tok_init_fn init_fn = (apsd_tok_init_fn)method_getImplementation(m);
                        tok = init_fn(raw, init_sel, (id)CFSTR(FUXI_APPSTORE_SYNTH_JWT),
                            (id)exp, 2592000.0, true);
                        CFRelease(exp);
                        if (tok != NULL) {
                            __atomic_store_n(&apsd_cached_synth_token, tok, __ATOMIC_RELEASE);
                        }
                    } else {
                        apsd_objc_void0(raw, "release");
                    }
                }
            }
        }
    }
    pthread_mutex_unlock(&apsd_lock);
    return tok;
}

static id apsd_hook_token_fetch(id self, SEL cmd)
{
    id (*orig)(id, SEL) = (id (*)(id, SEL))__atomic_load_n(&apsd_orig_token_fetch, __ATOMIC_ACQUIRE);
    if (apsd_is_appstore && (__atomic_load_n(&apsd_fix_mode, __ATOMIC_RELAXED) & FUXI_APPSTORE_FIX_MEDIA_PROXY)) {
        apsd_busy++;
        id tok = apsd_get_synth_media_token();
        Class prom_cls = objc_lookUpClass("AMSPromise");
        id prom = (tok != NULL && prom_cls != NULL)
            ? apsd_objc_cls1(prom_cls, "promiseWithResult:", tok)
            : NULL;
        if (prom != NULL) {
            if (__atomic_load_n(&apsd_log_mode, __ATOMIC_RELAXED) != 0u &&
                __atomic_add_fetch(&apsd_proxy_tok_calls, 1u, __ATOMIC_RELAXED) <= 40u) {
                char cid[96];
                cid[0] = 0;
                (void)apsd_cfstr(apsd_objc0(self, "clientIdentifier"), cid, sizeof(cid));
                apsd_logf("PROXY_TOK", "fetchMediaToken client=%s", cid[0] ? cid : "?");
            }
            apsd_busy--;
            return prom;
        }
        apsd_busy--;
    }
    return orig(self, cmd);
}

static id apsd_hook_token_cached(id self, SEL cmd)
{
    id (*orig)(id, SEL) = (id (*)(id, SEL))__atomic_load_n(&apsd_orig_token_cached, __ATOMIC_ACQUIRE);
    if (apsd_is_appstore && (__atomic_load_n(&apsd_fix_mode, __ATOMIC_RELAXED) & FUXI_APPSTORE_FIX_MEDIA_PROXY)) {
        apsd_busy++;
        id tok = apsd_get_synth_media_token();
        apsd_busy--;
        if (tok != NULL) {
            return tok;
        }
    }
    return orig(self, cmd);
}

static id apsd_maybe_rewrite_req(id req, bool *owned_out)
{
    *owned_out = false;
    if (!apsd_is_appstore ||
        !(__atomic_load_n(&apsd_fix_mode, __ATOMIC_RELAXED) & FUXI_APPSTORE_FIX_MEDIA_PROXY) ||
        req == NULL) {
        return req;
    }
    apsd_busy++;
    char orig_url[2048];
    orig_url[0] = 0;
    bool oversize = false;
    if (!apsd_req_url_raw(req, orig_url, sizeof(orig_url), &oversize) || oversize) {
        apsd_busy--;
        return req;
    }
    char new_url[2048];
    const bool is_amp = fuxi_appstore_rewrite_amp_url(orig_url, new_url, sizeof(new_url));
    const bool is_img = !is_amp && fuxi_appstore_rewrite_heic_url(orig_url, new_url, sizeof(new_url));
    if (!is_amp && !is_img) {
        apsd_busy--;
        return req;
    }
    id mreq = apsd_objc0(req, "mutableCopy");
    if (mreq == NULL) {
        apsd_busy--;
        return req;
    }
    CFStringRef ustr = CFStringCreateWithCString(kCFAllocatorDefault, new_url, kCFStringEncodingUTF8);
    CFURLRef cfurl = ustr ? CFURLCreateWithString(kCFAllocatorDefault, ustr, NULL) : NULL;
    if (ustr != NULL) {
        CFRelease(ustr);
    }
    if (cfurl == NULL) {
        apsd_objc_void0(mreq, "release");
        apsd_busy--;
        return req;
    }
    apsd_objc_void1(mreq, "setURL:", (id)cfurl);
    CFRelease(cfurl);

    char safe_url[512];
    if (!fuxi_appstore_mask_url_host_path(new_url, safe_url, sizeof(safe_url))) {
        snprintf(safe_url, sizeof(safe_url), "<url>");
    }

    if (is_amp) {
        char hdr_names[384];
        hdr_names[0] = 0;
        size_t hpos = 0;
        unsigned stripped = 0;
        id hdrs = apsd_objc0(mreq, "allHTTPHeaderFields");
        if (hdrs != NULL && CFGetTypeID((CFTypeRef)hdrs) == CFDictionaryGetTypeID()) {
            CFIndex cnt = CFDictionaryGetCount((CFDictionaryRef)hdrs);
            if (cnt > 0 && cnt <= 64) {
                const void *keys[64];
                CFDictionaryGetKeysAndValues((CFDictionaryRef)hdrs, keys, NULL);
                for (CFIndex i = 0; i < cnt; i++) {
                    char kbuf[96];
                    kbuf[0] = 0;
                    if (apsd_cfstr(keys[i], kbuf, sizeof(kbuf)) > 0u) {
                        if (hpos < sizeof(hdr_names) - 2u) {
                            int n = snprintf(hdr_names + hpos, sizeof(hdr_names) - hpos, "%s%s",
                                hpos ? "," : "", kbuf);
                            if (n > 0) {
                                size_t add = (size_t)n;
                                hpos += (add < sizeof(hdr_names) - hpos) ? add : (sizeof(hdr_names) - hpos - 1u);
                            }
                        }
                        if (fuxi_appstore_should_strip_header(kbuf)) {
                            apsd_objc_void2(mreq, "setValue:forHTTPHeaderField:", NULL, (id)keys[i]);
                            stripped++;
                        }
                    }
                }
            }
        }

        const CFStringRef core_strip[] = {
            CFSTR("Authorization"),
            CFSTR("Cookie"),
            CFSTR("Cookie2"),
            CFSTR("X-Apple-Store-Front"),
            CFSTR("X-Apple-ActionSignature"),
            CFSTR("X-Apple-ADSID"),
            CFSTR("X-Dsid"),
            CFSTR("X-Token"),
            CFSTR("X-Apple-AMD"),
            CFSTR("X-Apple-AMD-M"),
            CFSTR("X-Apple-MD"),
            CFSTR("X-Apple-MD-M"),
            CFSTR("X-Apple-I-MD"),
            CFSTR("X-Apple-I-MD-M"),
            CFSTR("X-Apple-I-MD-LU"),
            CFSTR("X-Apple-I-MD-RINFO"),
        };
        for (size_t i = 0; i < sizeof(core_strip) / sizeof(core_strip[0]); i++) {
            apsd_objc_void2(mreq, "setValue:forHTTPHeaderField:", NULL, (id)core_strip[i]);
        }
        apsd_objc_void2(mreq, "setValue:forHTTPHeaderField:", (id)CFSTR("https://apps.apple.com"), (id)CFSTR("Origin"));
        apsd_objc_void2(mreq, "setValue:forHTTPHeaderField:", (id)CFSTR("https://apps.apple.com/"), (id)CFSTR("Referer"));

        if (__atomic_load_n(&apsd_log_mode, __ATOMIC_RELAXED) != 0u &&
            __atomic_add_fetch(&apsd_proxy_rw_calls, 1u, __ATOMIC_RELAXED) <= 60u) {
            apsd_logf("PROXY_RW", "stripped=%u hdrs=[%s] url=%s",
                stripped, hdr_names[0] ? hdr_names : "-", safe_url);
        }
    } else if (__atomic_load_n(&apsd_log_mode, __ATOMIC_RELAXED) != 0u &&
        __atomic_add_fetch(&apsd_proxy_img_calls, 1u, __ATOMIC_RELAXED) <= 60u) {
        apsd_logf("PROXY_IMG", "url=%s", safe_url);
    }
    *owned_out = true;
    apsd_busy--;
    return mreq;
}

static void apsd_log_nsurl_req(id req)
{
    if (req == NULL || apsd_busy ||
        __atomic_load_n(&apsd_log_mode, __ATOMIC_RELAXED) == 0u ||
        __atomic_load_n(&apsd_hybrid_mode, __ATOMIC_RELAXED) == 0u) {
        return;
    }
    apsd_busy++;
    char raw_url[2048];
    raw_url[0] = 0;
    bool oversize = false;
    bool has_raw = apsd_req_url_raw(req, raw_url, sizeof(raw_url), &oversize);
    if (oversize || (has_raw && fuxi_appstore_is_key_auth_buy_url(raw_url))) {
        if (__atomic_add_fetch(&apsd_nsurl_req_calls, 1u, __ATOMIC_RELAXED) <= 160u) {
            char safe_url[512];
            unsigned has_sig = 0u, has_md = 0u, has_mdm = 0u;
            if (oversize) {
                if (!fuxi_appstore_mask_url_host_path_bounded(NULL, true, safe_url, sizeof(safe_url))) {
                    snprintf(safe_url, sizeof(safe_url), "[url_oversize_redacted]");
                }
            } else {
                apsd_cfurl_host_path((const void *)apsd_objc0(req, "URL"), safe_url, sizeof(safe_url));
            }
            apsd_req_hdr_flags(req, &has_sig, &has_md, &has_mdm);
            apsd_logf("NSURL_REQ", "url=%s has_sig=%u has_md=%u has_mdm=%u",
                safe_url, has_sig, has_md, has_mdm);
        }
    }
    apsd_busy--;
}

static id apsd_hook_nsurl_task0(id self, SEL cmd, id req)
{
    id (*orig)(id, SEL, id) = (id (*)(id, SEL, id))__atomic_load_n(&apsd_orig_nsurl_task[0], __ATOMIC_ACQUIRE);
    bool owned = false;
    id eff_req = apsd_maybe_rewrite_req(req, &owned);
    apsd_log_nsurl_req(eff_req);
    id task = orig(self, cmd, eff_req);
    if (owned) {
        apsd_busy++;
        apsd_objc_void0(eff_req, "release");
        apsd_busy--;
    }
    return task;
}

static id apsd_hook_nsurl_task1(id self, SEL cmd, id req)
{
    id (*orig)(id, SEL, id) = (id (*)(id, SEL, id))__atomic_load_n(&apsd_orig_nsurl_task[1], __ATOMIC_ACQUIRE);
    bool owned = false;
    id eff_req = apsd_maybe_rewrite_req(req, &owned);
    apsd_log_nsurl_req(eff_req);
    id task = orig(self, cmd, eff_req);
    if (owned) {
        apsd_busy++;
        apsd_objc_void0(eff_req, "release");
        apsd_busy--;
    }
    return task;
}

static id apsd_hook_nsurl_task_ch0(id self, SEL cmd, id req, id handler)
{
    id (*orig)(id, SEL, id, id) = (id (*)(id, SEL, id, id))__atomic_load_n(&apsd_orig_nsurl_task_ch[0], __ATOMIC_ACQUIRE);
    bool owned = false;
    id eff_req = apsd_maybe_rewrite_req(req, &owned);
    apsd_log_nsurl_req(eff_req);
    id task = orig(self, cmd, eff_req, handler);
    if (owned) {
        apsd_busy++;
        apsd_objc_void0(eff_req, "release");
        apsd_busy--;
    }
    return task;
}

static id apsd_hook_nsurl_task_ch1(id self, SEL cmd, id req, id handler)
{
    id (*orig)(id, SEL, id, id) = (id (*)(id, SEL, id, id))__atomic_load_n(&apsd_orig_nsurl_task_ch[1], __ATOMIC_ACQUIRE);
    bool owned = false;
    id eff_req = apsd_maybe_rewrite_req(req, &owned);
    apsd_log_nsurl_req(eff_req);
    id task = orig(self, cmd, eff_req, handler);
    if (owned) {
        apsd_busy++;
        apsd_objc_void0(eff_req, "release");
        apsd_busy--;
    }
    return task;
}

static void apsd_hook_ams_url_done(id self, SEL cmd, id sess, id task, id err)
{
    void (*orig)(id, SEL, id, id, id) = (void (*)(id, SEL, id, id, id))__atomic_load_n(&apsd_orig_ams_url_done, __ATOMIC_ACQUIRE);
    if (__atomic_load_n(&apsd_log_mode, __ATOMIC_RELAXED) != 0u) {
        apsd_busy++;
        char url[512];
        long status = -1;
        unsigned has_sig = 0u, has_md = 0u, has_mdm = 0u;
        apsd_task_url_status_hdrs(task, url, sizeof(url), &status, &has_sig, &has_md, &has_mdm);
        const bool is_key_or_err = (err != NULL || status != 200L || fuxi_appstore_is_key_auth_buy_url(url));
        bool should_log = false;
        if (is_key_or_err) {
            should_log = (__atomic_add_fetch(&apsd_url_key_done_calls, 1u, __ATOMIC_RELAXED) <= 160u);
        } else {
            should_log = (__atomic_add_fetch(&apsd_url_done_calls, 1u, __ATOMIC_RELAXED) <= 200u);
        }
        if (should_log) {
            if (err != NULL) {
                char eb[460];
                apsd_fmt_err(err, eb, sizeof(eb));
                apsd_logf("AMS_DONE", "http=%ld has_sig=%u has_md=%u has_mdm=%u url=%s err=%s",
                    status, has_sig, has_md, has_mdm, url, eb);
            } else {
                apsd_logf("AMS_DONE", "http=%ld has_sig=%u has_md=%u has_mdm=%u url=%s ok",
                    status, has_sig, has_md, has_mdm, url);
            }
        }
        apsd_busy--;
    }
    orig(self, cmd, sess, task, err);
}

/* ---- T6 Read-only diagnostic probes (non-blocking, non-ARC *ep safe, privacy-safe) ---- */

static id apsd_hook_sap_exch(id self, SEL cmd, id data, id *err_out)
{
    id (*orig)(id, SEL, id, id *) = (id (*)(id, SEL, id, id *))__atomic_load_n(&apsd_orig_sap_exch, __ATOMIC_ACQUIRE);
    id local_err = NULL;
    id *ep = err_out ? err_out : &local_err;
    id res = orig(self, cmd, data, ep);
    if (__atomic_add_fetch(&apsd_sap_calls, 1u, __ATOMIC_RELAXED) <= 80u) {
        apsd_busy++;
        const void *err_obj = FUXI_APPSTORE_ERR_OUT(res != NULL, ep);
        char eb[460];
        apsd_fmt_err(err_obj, eb, sizeof(eb));
        apsd_logf("SAP", "stage=exchangeData in_len=%ld out_len=%ld ok=%u err=%s",
            apsd_cfdata_len(data), apsd_cfdata_len(res), res != NULL ? 1u : 0u, eb);
        apsd_busy--;
    }
    return res;
}

static id apsd_hook_sap_sign(id self, SEL cmd, id data, id *err_out)
{
    id (*orig)(id, SEL, id, id *) = (id (*)(id, SEL, id, id *))__atomic_load_n(&apsd_orig_sap_sign, __ATOMIC_ACQUIRE);
    id local_err = NULL;
    id *ep = err_out ? err_out : &local_err;
    id res = orig(self, cmd, data, ep);
    if (__atomic_add_fetch(&apsd_sap_calls, 1u, __ATOMIC_RELAXED) <= 80u) {
        apsd_busy++;
        const void *err_obj = FUXI_APPSTORE_ERR_OUT(res != NULL, ep);
        char eb[460];
        apsd_fmt_err(err_obj, eb, sizeof(eb));
        apsd_logf("SAP", "stage=signData in_len=%ld out_len=%ld ok=%u err=%s",
            apsd_cfdata_len(data), apsd_cfdata_len(res), res != NULL ? 1u : 0u, eb);
        apsd_busy--;
    }
    return res;
}

static bool apsd_mescal_admit_other(void)
{
    pthread_mutex_lock(&apsd_lock);
    unsigned total = __atomic_load_n(&apsd_mescal_calls, __ATOMIC_RELAXED);
    bool admit = (total < FUXI_APPSTORE_MESCAL_CAP);
    if (admit) {
        __atomic_store_n(&apsd_mescal_calls, total + 1u, __ATOMIC_RELAXED);
    }
    pthread_mutex_unlock(&apsd_lock);
    return admit;
}

static id apsd_hook_mescal_init(id self, SEL cmd, intptr_t mtype, id *err_out)
{
    id (*orig)(id, SEL, intptr_t, id *) = (id (*)(id, SEL, intptr_t, id *))__atomic_load_n(&apsd_orig_mescal_init, __ATOMIC_ACQUIRE);
    id local_err = NULL;
    id *ep = err_out ? err_out : &local_err;
    id res = orig(self, cmd, mtype, ep);
    if (apsd_mescal_admit_other()) {
        apsd_busy++;
        const void *err_obj = FUXI_APPSTORE_ERR_OUT(res != NULL, ep);
        char eb[460];
        apsd_fmt_err(err_obj, eb, sizeof(eb));
        apsd_logf("MESCAL", "stage=initWithMescalType mescalType=%ld ok=%u err=%s",
            (long)mtype, res != NULL ? 1u : 0u, eb);
        apsd_busy--;
    }
    return res;
}

static id apsd_hook_mescal_exch(id self, SEL cmd, id data, id *err_out)
{
    id (*orig)(id, SEL, id, id *) = (id (*)(id, SEL, id, id *))__atomic_load_n(&apsd_orig_mescal_exch, __ATOMIC_ACQUIRE);
    id local_err = NULL;
    id *ep = err_out ? err_out : &local_err;
    id res = orig(self, cmd, data, ep);
    if (apsd_mescal_admit_other()) {
        apsd_busy++;
        const void *err_obj = FUXI_APPSTORE_ERR_OUT(res != NULL, ep);
        char eb[460];
        apsd_fmt_err(err_obj, eb, sizeof(eb));
        apsd_logf("MESCAL", "stage=fairplay.exchangeData in_len=%ld out_len=%ld ok=%u err=%s",
            apsd_cfdata_len(data), apsd_cfdata_len(res), res != NULL ? 1u : 0u, eb);
        apsd_busy--;
    }
    return res;
}

static void apsd_log_mescal_sign_event(
    const char *stage, struct fuxi_appstore_mescal_sign_state *sign_state,
    id data, id res, const void *err_obj)
{
    apsd_busy++;
    char dom[96];
    long code = 0L;
    (void)apsd_err_dom_code(err_obj, dom, sizeof(dom), &code);

    pthread_mutex_lock(&apsd_lock);
    unsigned total = __atomic_load_n(&apsd_mescal_calls, __ATOMIC_RELAXED);
    bool admit = fuxi_appstore_should_log_mescal_sign_state(
        res != NULL, dom, code, total, sign_state);
    unsigned repeat_n = sign_state->repeat_304;
    if (admit) {
        __atomic_store_n(&apsd_mescal_calls, total + 1u, __ATOMIC_RELAXED);
    }
    pthread_mutex_unlock(&apsd_lock);

    if (admit) {
        char eb[460];
        apsd_fmt_err(err_obj, eb, sizeof(eb));
        apsd_logf("MESCAL", "stage=%s in_len=%ld out_len=%ld ok=%u r304=%u err=%s",
            stage, apsd_cfdata_len(data), apsd_cfdata_len(res),
            res != NULL ? 1u : 0u, repeat_n, eb);
    }
    apsd_busy--;
}

static id apsd_hook_mescal_sign(id self, SEL cmd, id data, id *err_out)
{
    id (*orig)(id, SEL, id, id *) = (id (*)(id, SEL, id, id *))__atomic_load_n(&apsd_orig_mescal_sign, __ATOMIC_ACQUIRE);
    id local_err = NULL;
    id *ep = err_out ? err_out : &local_err;
    id res = orig(self, cmd, data, ep);
    const void *err_obj = FUXI_APPSTORE_ERR_OUT(res != NULL, ep);
    apsd_log_mescal_sign_event("fairplay.signData", &apsd_mescal_fp_sign_state, data, res, err_obj);
    return res;
}

static id apsd_hook_mescal_prime(id self, SEL cmd, id data, id *err_out)
{
    id (*orig)(id, SEL, id, id *) = (id (*)(id, SEL, id, id *))__atomic_load_n(&apsd_orig_mescal_prime, __ATOMIC_ACQUIRE);
    id local_err = NULL;
    id *ep = err_out ? err_out : &local_err;
    id res = orig(self, cmd, data, ep);
    if (apsd_mescal_admit_other()) {
        apsd_busy++;
        const void *err_obj = FUXI_APPSTORE_ERR_OUT(res != NULL, ep);
        char eb[460];
        apsd_fmt_err(err_obj, eb, sizeof(eb));
        apsd_logf("MESCAL", "stage=fairplay.primingSignature in_len=%ld out_len=%ld ok=%u err=%s",
            apsd_cfdata_len(data), apsd_cfdata_len(res), res != NULL ? 1u : 0u, eb);
        apsd_busy--;
    }
    return res;
}

/* AMSMescalType is NSInteger ('q' on arm64 LP64); long matches 64-bit signed 'q' and %ld */
static id apsd_hook_mescal_sig_req(id self, SEL cmd, id req, long mtype, id bag, id *err_out)
{
    id (*orig)(id, SEL, id, long, id, id *) =
        (id (*)(id, SEL, id, long, id, id *))__atomic_load_n(&apsd_orig_mescal_sig_req, __ATOMIC_ACQUIRE);
    char url[512];
    unsigned has_sig_pre = 0u, has_md = 0u, has_mdm = 0u;
    url[0] = 0;
    if (!apsd_busy) {
        apsd_busy++;
        if (req != NULL) {
            apsd_cfurl_host_path((const void *)apsd_objc0(req, "URL"), url, sizeof(url));
            apsd_req_hdr_flags(req, &has_sig_pre, &has_md, &has_mdm);
        } else {
            snprintf(url, sizeof(url), "(nil-req)");
        }
        apsd_busy--;
    }
    id local_err = NULL;
    id *ep = err_out ? err_out : &local_err;
    id res = orig(self, cmd, req, mtype, bag, ep);
    if (!apsd_busy && __atomic_add_fetch(&apsd_mescal_req_calls, 1u, __ATOMIC_RELAXED) <= 160u) {
        apsd_busy++;
        const void *err_obj = FUXI_APPSTORE_ERR_OUT(res != NULL, ep);
        char eb[460];
        apsd_fmt_err(err_obj, eb, sizeof(eb));
        apsd_logf("MESCAL_REQ",
            "type=%ld url=%s has_md=%u has_mdm=%u pre_sig=%u ok=%u sig_len=%ld err=%s",
            mtype, url[0] ? url : "(unknown)", has_md, has_mdm, has_sig_pre,
            res != NULL ? 1u : 0u, apsd_cfstr_or_data_len(res), eb);
        apsd_busy--;
    }
    return res;
}

static bool apsd_hook_msess_est_b(id self, SEL cmd, id bag, id *err_out)
{
    bool (*orig)(id, SEL, id, id *) = (bool (*)(id, SEL, id, id *))__atomic_load_n(&apsd_orig_msess_est_b, __ATOMIC_ACQUIRE);
    id local_err = NULL;
    id *ep = err_out ? err_out : &local_err;
    bool ok = orig(self, cmd, bag, ep);
    if (apsd_mescal_admit_other()) {
        apsd_busy++;
        const void *err_obj = FUXI_APPSTORE_ERR_OUT(ok, ep);
        char eb[460];
        apsd_fmt_err(err_obj, eb, sizeof(eb));
        apsd_logf("MESCAL", "stage=session._establishContext ok=%u err=%s", ok ? 1u : 0u, eb);
        apsd_busy--;
    }
    return ok;
}

static id apsd_hook_msess_est_id(id self, SEL cmd, id bag, id *err_out)
{
    id (*orig)(id, SEL, id, id *) = (id (*)(id, SEL, id, id *))__atomic_load_n(&apsd_orig_msess_est_id, __ATOMIC_ACQUIRE);
    id local_err = NULL;
    id *ep = err_out ? err_out : &local_err;
    id res = orig(self, cmd, bag, ep);
    if (apsd_mescal_admit_other()) {
        apsd_busy++;
        const void *err_obj = FUXI_APPSTORE_ERR_OUT(res != NULL, ep);
        char eb[460];
        apsd_fmt_err(err_obj, eb, sizeof(eb));
        apsd_logf("MESCAL", "stage=session._establishContext ok=%u err=%s",
            res != NULL ? 1u : 0u, eb);
        apsd_busy--;
    }
    return res;
}

static id apsd_hook_msess_cert(id self, SEL cmd, id bag, id *err_out)
{
    id (*orig)(id, SEL, id, id *) = (id (*)(id, SEL, id, id *))__atomic_load_n(&apsd_orig_msess_cert, __ATOMIC_ACQUIRE);
    id local_err = NULL;
    id *ep = err_out ? err_out : &local_err;
    id res = orig(self, cmd, bag, ep);
    if (apsd_mescal_admit_other()) {
        apsd_busy++;
        const void *err_obj = FUXI_APPSTORE_ERR_OUT(res != NULL, ep);
        char eb[460];
        apsd_fmt_err(err_obj, eb, sizeof(eb));
        apsd_logf("MESCAL", "stage=session._loadCertificateData out_len=%ld ok=%u err=%s",
            apsd_cfdata_len(res), res != NULL ? 1u : 0u, eb);
        apsd_busy--;
    }
    return res;
}

static id apsd_hook_msess_post(id self, SEL cmd, id data, id bag, id *err_out)
{
    id (*orig)(id, SEL, id, id, id *) = (id (*)(id, SEL, id, id, id *))__atomic_load_n(&apsd_orig_msess_post, __ATOMIC_ACQUIRE);
    id local_err = NULL;
    id *ep = err_out ? err_out : &local_err;
    id res = orig(self, cmd, data, bag, ep);
    if (apsd_mescal_admit_other()) {
        apsd_busy++;
        const void *err_obj = FUXI_APPSTORE_ERR_OUT(res != NULL, ep);
        char eb[460];
        apsd_fmt_err(err_obj, eb, sizeof(eb));
        apsd_logf("MESCAL", "stage=session._postExchangeData in_len=%ld out_len=%ld ok=%u err=%s",
            apsd_cfdata_len(data), apsd_cfdata_len(res), res != NULL ? 1u : 0u, eb);
        apsd_busy--;
    }
    return res;
}

static id apsd_hook_msess_sign(id self, SEL cmd, id data, id bag, id *err_out)
{
    id (*orig)(id, SEL, id, id, id *) = (id (*)(id, SEL, id, id, id *))__atomic_load_n(&apsd_orig_msess_sign, __ATOMIC_ACQUIRE);
    id local_err = NULL;
    id *ep = err_out ? err_out : &local_err;
    id res = orig(self, cmd, data, bag, ep);
    const void *err_obj = FUXI_APPSTORE_ERR_OUT(res != NULL, ep);
    apsd_log_mescal_sign_event("session.signData", &apsd_mescal_sess_sign_state, data, res, err_obj);
    return res;
}

static void apsd_log_bag_info(id self)
{
    if (__atomic_add_fetch(&apsd_bag_calls, 1u, __ATOMIC_RELAXED) > 80u) {
        return;
    }
    apsd_busy++;
    char prof[96];
    char ver[48];
    prof[0] = 0;
    ver[0] = 0;
    (void)apsd_cfstr(apsd_objc0(self, "profile"), prof, sizeof(prof));
    (void)apsd_cfstr(apsd_objc0(self, "profileVersion"), ver, sizeof(ver));
    apsd_logf("BAG", "_fetchBag profile=%s ver=%s", prof[0] ? prof : "?", ver[0] ? ver : "?");
    apsd_busy--;
}

static void apsd_hook_bag_fetch_v(id self, SEL cmd)
{
    void (*orig)(id, SEL) = (void (*)(id, SEL))__atomic_load_n(&apsd_orig_bag_fetch_v, __ATOMIC_ACQUIRE);
    apsd_log_bag_info(self);
    orig(self, cmd);
}

static id apsd_hook_bag_fetch_id(id self, SEL cmd)
{
    id (*orig)(id, SEL) = (id (*)(id, SEL))__atomic_load_n(&apsd_orig_bag_fetch_id, __ATOMIC_ACQUIRE);
    apsd_log_bag_info(self);
    return orig(self, cmd);
}

static void apsd_hook_bag_fail(id self, SEL cmd, id failure)
{
    void (*orig)(id, SEL, id) = (void (*)(id, SEL, id))__atomic_load_n(&apsd_orig_bag_fail, __ATOMIC_ACQUIRE);
    if (failure != NULL && __atomic_add_fetch(&apsd_bag_calls, 1u, __ATOMIC_RELAXED) <= 80u) {
        apsd_busy++;
        char prof[64];
        char eb[460];
        prof[0] = 0;
        (void)apsd_cfstr(apsd_objc0(self, "profile"), prof, sizeof(prof));
        apsd_fmt_err(failure, eb, sizeof(eb));
        apsd_logf("BAG_ERR", "profile=%s err=%s", prof[0] ? prof : "?", eb);
        apsd_busy--;
    }
    orig(self, cmd, failure);
}

static void apsd_log_promise_err(id err)
{
    if (err == NULL || apsd_busy) {
        return;
    }
    apsd_busy++;
    char dom[96];
    long code = 0L;
    bool is_204 = false;
    if (apsd_err_dom_code(err, dom, sizeof(dom), &code)) {
        is_204 = fuxi_appstore_err_is_bag_missing(dom, code);
    }
    pthread_mutex_lock(&apsd_lock);
    bool should_log = fuxi_appstore_promise_should_log(
        is_204, &apsd_promise_204_calls, &apsd_promise_calls);
    pthread_mutex_unlock(&apsd_lock);
    if (should_log) {
        char eb[640];
        apsd_fmt_err(err, eb, sizeof(eb));
        apsd_logf("PROMISE_ERR", "err=%s", eb);
    }
    apsd_busy--;
}

static bool apsd_hook_promise_b(id self, SEL cmd, id err)
{
    bool (*orig)(id, SEL, id) = (bool (*)(id, SEL, id))__atomic_load_n(&apsd_orig_promise_b, __ATOMIC_ACQUIRE);
    apsd_log_promise_err(err);
    return orig(self, cmd, err);
}

static void apsd_hook_promise_v(id self, SEL cmd, id err)
{
    void (*orig)(id, SEL, id) = (void (*)(id, SEL, id))__atomic_load_n(&apsd_orig_promise_v, __ATOMIC_ACQUIRE);
    apsd_log_promise_err(err);
    orig(self, cmd, err);
}

static bool apsd_hook_promise_res_b(id self, SEL cmd, id result, id err)
{
    bool (*orig)(id, SEL, id, id) = (bool (*)(id, SEL, id, id))__atomic_load_n(&apsd_orig_promise_res_b, __ATOMIC_ACQUIRE);
    if (err != NULL) {
        apsd_log_promise_err(err);
    }
    return orig(self, cmd, result, err);
}

static void apsd_hook_promise_res_v(id self, SEL cmd, id result, id err)
{
    void (*orig)(id, SEL, id, id) = (void (*)(id, SEL, id, id))__atomic_load_n(&apsd_orig_promise_res_v, __ATOMIC_ACQUIRE);
    if (err != NULL) {
        apsd_log_promise_err(err);
    }
    orig(self, cmd, result, err);
}

static id apsd_hook_auth_perform(id self, SEL cmd)
{
    id (*orig)(id, SEL) = (id (*)(id, SEL))__atomic_load_n(&apsd_orig_auth_perform, __ATOMIC_ACQUIRE);
    bool should_log = (__atomic_add_fetch(&apsd_auth_calls, 1u, __ATOMIC_RELAXED) <= 40u);
    if (should_log) {
        apsd_busy++;
        apsd_logf("AUTH", "stage=performAuthentication enter");
        apsd_busy--;
    }
    id res = orig(self, cmd);
    if (should_log) {
        apsd_busy++;
        apsd_logf("AUTH", "stage=performAuthentication returned prom_nonnull=%u", res != NULL ? 1u : 0u);
        apsd_busy--;
    }
    return res;
}

static id apsd_hook_purchase_perform(id self, SEL cmd)
{
    id (*orig)(id, SEL) = (id (*)(id, SEL))__atomic_load_n(&apsd_orig_purchase_perform, __ATOMIC_ACQUIRE);
    bool should_log = (__atomic_add_fetch(&apsd_purchase_calls, 1u, __ATOMIC_RELAXED) <= 40u);
    if (should_log) {
        apsd_busy++;
        apsd_logf("PURCHASE", "stage=performPurchase enter");
        apsd_busy--;
    }
    id res = orig(self, cmd);
    if (should_log) {
        apsd_busy++;
        apsd_logf("PURCHASE", "stage=performPurchase returned prom_nonnull=%u", res != NULL ? 1u : 0u);
        apsd_busy--;
    }
    return res;
}

/* ---- Safe method swizzling (objc_lookUpClass + add_image) ---- */

static bool apsd_try_install_one_ex(const char *cls_name, const char *sel_name,
    unsigned nargs, char ret_exp, char a2_exp, char a3_exp, char a4_exp, char a5_exp,
    bool is_class_method, IMP replacement, IMP *orig_slot, bool *done_flag)
{
    if (*done_flag) {
        return false;
    }
    Class cls = objc_lookUpClass(cls_name);
    if (cls == NULL) {
        return false;
    }
    SEL sel = sel_registerName(sel_name);
    Method m = is_class_method ? class_getClassMethod(cls, sel) : class_getInstanceMethod(cls, sel);
    if (m == NULL) {
        return false;
    }
    if (method_getNumberOfArguments(m) != nargs) {
        return false;
    }
    char rtype[32];
    method_getReturnType(m, rtype, sizeof(rtype));
    if (!apsd_type_matches(rtype, ret_exp)) {
        return false;
    }
    const char exps[4] = { a2_exp, a3_exp, a4_exp, a5_exp };
    for (unsigned idx = 2; idx < nargs && idx < 6u; idx++) {
        char atype[32];
        method_getArgumentType(m, idx, atype, sizeof(atype));
        if (!apsd_type_matches(atype, exps[idx - 2u])) {
            return false;
        }
    }
    if (apsd_method_already_seen(m)) {
        *done_flag = true;
        return false;
    }
    IMP cur = method_getImplementation(m);
    __atomic_store_n(orig_slot, cur, __ATOMIC_RELEASE);
    (void)method_setImplementation(m, replacement);
    *done_flag = true;
    apsd_logf("INST", "%s%s.%s ret=%s", is_class_method ? "+" : "-", cls_name, sel_name, rtype);
    return true;
}

static bool apsd_try_install_one(const char *cls_name, const char *sel_name,
    unsigned nargs, char ret_exp, char a2_exp, char a3_exp, char a4_exp,
    IMP replacement, IMP *orig_slot, bool *done_flag)
{
    return apsd_try_install_one_ex(cls_name, sel_name, nargs, ret_exp, a2_exp, a3_exp, a4_exp, 0,
        false, replacement, orig_slot, done_flag);
}

static void apsd_install_all(void)
{
    const unsigned fix_mode = __atomic_load_n(&apsd_fix_mode, __ATOMIC_RELAXED);
    const unsigned log_mode = __atomic_load_n(&apsd_log_mode, __ATOMIC_RELAXED);
    const unsigned hybrid_mode = __atomic_load_n(&apsd_hybrid_mode, __ATOMIC_RELAXED);
    const bool fix_media = apsd_is_appstore && ((fix_mode & FUXI_APPSTORE_FIX_MEDIA_PROXY) != 0u);

    if (fix_media) {
        (void)apsd_try_install_one("AMSMediaTokenService", "fetchMediaToken", 2u, '@', 0, 0, 0,
            (IMP)apsd_hook_token_fetch, &apsd_orig_token_fetch, &apsd_inst_token_fetch);
        (void)apsd_try_install_one("AMSMediaTokenService", "cachedMediaToken", 2u, '@', 0, 0, 0,
            (IMP)apsd_hook_token_cached, &apsd_orig_token_cached, &apsd_inst_token_cached);
    }

    if (fix_media || log_mode != 0u) {
        static const char *const url_classes[2] = { "__NSURLSessionLocal", "NSURLSession" };
        const IMP task_hooks[2] = { (IMP)apsd_hook_nsurl_task0, (IMP)apsd_hook_nsurl_task1 };
        const IMP task_ch_hooks[2] = { (IMP)apsd_hook_nsurl_task_ch0, (IMP)apsd_hook_nsurl_task_ch1 };
        for (unsigned idx = 0; idx < 2u; idx++) {
            (void)apsd_try_install_one(url_classes[idx], "dataTaskWithRequest:", 3u, '@', '@', 0, 0,
                task_hooks[idx], &apsd_orig_nsurl_task[idx], &apsd_inst_nsurl_task[idx]);
            (void)apsd_try_install_one(url_classes[idx], "dataTaskWithRequest:completionHandler:", 4u, '@', '@', '@', 0,
                task_ch_hooks[idx], &apsd_orig_nsurl_task_ch[idx], &apsd_inst_nsurl_task_ch[idx]);
        }
    }

    if (log_mode != 0u) {
        (void)apsd_try_install_one("AMSURLSession", "URLSession:task:didCompleteWithError:", 5u, 'v', '@', '@', '@',
            (IMP)apsd_hook_ams_url_done, &apsd_orig_ams_url_done, &apsd_inst_ams_url_done);
    }

    if (hybrid_mode == 0u) {
        return;
    }

    /* T6 hybrid read-only diagnostic probes */
    (void)apsd_try_install_one("SSVFairPlaySAPContext", "exchangeData:error:", 4u, '@', '@', 'p', 0,
        (IMP)apsd_hook_sap_exch, &apsd_orig_sap_exch, &apsd_inst_sap_exch);
    (void)apsd_try_install_one("SSVFairPlaySAPContext", "signData:error:", 4u, '@', '@', 'p', 0,
        (IMP)apsd_hook_sap_sign, &apsd_orig_sap_sign, &apsd_inst_sap_sign);

    (void)apsd_try_install_one("AMSMescalFairPlay", "initWithMescalType:error:", 4u, '@', 'i', 'p', 0,
        (IMP)apsd_hook_mescal_init, &apsd_orig_mescal_init, &apsd_inst_mescal_init);
    (void)apsd_try_install_one("AMSMescalFairPlay", "exchangeData:error:", 4u, '@', '@', 'p', 0,
        (IMP)apsd_hook_mescal_exch, &apsd_orig_mescal_exch, &apsd_inst_mescal_exch);
    (void)apsd_try_install_one("AMSMescalFairPlay", "signData:error:", 4u, '@', '@', 'p', 0,
        (IMP)apsd_hook_mescal_sign, &apsd_orig_mescal_sign, &apsd_inst_mescal_sign);
    (void)apsd_try_install_one("AMSMescalFairPlay", "primingSignatureForData:error:", 4u, '@', '@', 'p', 0,
        (IMP)apsd_hook_mescal_prime, &apsd_orig_mescal_prime, &apsd_inst_mescal_prime);
    (void)apsd_try_install_one_ex("AMSMescal", "signatureUsingRequest:type:bag:error:", 6u, '@', '@', 'q', '@', 'p',
        true, (IMP)apsd_hook_mescal_sig_req, &apsd_orig_mescal_sig_req, &apsd_inst_mescal_sig_req);

    if (!apsd_inst_msess_est_b && !apsd_inst_msess_est_id) {
        if (!apsd_try_install_one("AMSMescalSession", "_establishContextWithBag:error:", 4u, 'b', '@', 'p', 0,
                (IMP)apsd_hook_msess_est_b, &apsd_orig_msess_est_b, &apsd_inst_msess_est_b)) {
            (void)apsd_try_install_one("AMSMescalSession", "_establishContextWithBag:error:", 4u, '@', '@', 'p', 0,
                (IMP)apsd_hook_msess_est_id, &apsd_orig_msess_est_id, &apsd_inst_msess_est_id);
        }
    }
    (void)apsd_try_install_one("AMSMescalSession", "_loadCertificateDataWithBag:error:", 4u, '@', '@', 'p', 0,
        (IMP)apsd_hook_msess_cert, &apsd_orig_msess_cert, &apsd_inst_msess_cert);
    (void)apsd_try_install_one("AMSMescalSession", "_postExchangeData:bag:error:", 5u, '@', '@', '@', 'p',
        (IMP)apsd_hook_msess_post, &apsd_orig_msess_post, &apsd_inst_msess_post);
    (void)apsd_try_install_one("AMSMescalSession", "signData:bag:error:", 5u, '@', '@', '@', 'p',
        (IMP)apsd_hook_msess_sign, &apsd_orig_msess_sign, &apsd_inst_msess_sign);

    if (!apsd_inst_bag_fetch_v && !apsd_inst_bag_fetch_id) {
        if (!apsd_try_install_one("AMSBagNetworkDataSource", "_fetchBag", 2u, 'v', 0, 0, 0,
                (IMP)apsd_hook_bag_fetch_v, &apsd_orig_bag_fetch_v, &apsd_inst_bag_fetch_v)) {
            (void)apsd_try_install_one("AMSBagNetworkDataSource", "_fetchBag", 2u, '@', 0, 0, 0,
                (IMP)apsd_hook_bag_fetch_id, &apsd_orig_bag_fetch_id, &apsd_inst_bag_fetch_id);
        }
    }
    (void)apsd_try_install_one("AMSBagNetworkDataSource", "setActiveFailure:", 3u, 'v', '@', 0, 0,
        (IMP)apsd_hook_bag_fail, &apsd_orig_bag_fail, &apsd_inst_bag_fail);

    if (!apsd_inst_promise_b && !apsd_inst_promise_v) {
        if (!apsd_try_install_one("AMSPromise", "finishWithError:", 3u, 'b', '@', 0, 0,
                (IMP)apsd_hook_promise_b, &apsd_orig_promise_b, &apsd_inst_promise_b)) {
            (void)apsd_try_install_one("AMSPromise", "finishWithError:", 3u, 'v', '@', 0, 0,
                (IMP)apsd_hook_promise_v, &apsd_orig_promise_v, &apsd_inst_promise_v);
        }
    }
    if (!apsd_inst_promise_res_b && !apsd_inst_promise_res_v) {
        if (!apsd_try_install_one("AMSPromise", "finishWithResult:error:", 4u, 'b', '@', '@', 0,
                (IMP)apsd_hook_promise_res_b, &apsd_orig_promise_res_b, &apsd_inst_promise_res_b)) {
            (void)apsd_try_install_one("AMSPromise", "finishWithResult:error:", 4u, 'v', '@', '@', 0,
                (IMP)apsd_hook_promise_res_v, &apsd_orig_promise_res_v, &apsd_inst_promise_res_v);
        }
    }

    (void)apsd_try_install_one("AMSAuthenticateTask", "performAuthentication", 2u, '@', 0, 0, 0,
        (IMP)apsd_hook_auth_perform, &apsd_orig_auth_perform, &apsd_inst_auth_perform);
    (void)apsd_try_install_one("AMSPurchaseTask", "performPurchase", 2u, '@', 0, 0, 0,
        (IMP)apsd_hook_purchase_perform, &apsd_orig_purchase_perform, &apsd_inst_purchase_perform);
}

static void apsd_add_image(const struct mach_header *header, intptr_t slide)
{
    (void)header;
    (void)slide;
    pthread_mutex_lock(&apsd_lock);
    apsd_install_all();
    pthread_mutex_unlock(&apsd_lock);
}

static uint32_t apsd_read_adt_u32(CFStringRef key)
{
    fx_as_io_object_t chosen = IORegistryEntryFromPath(MACH_PORT_NULL, "IODeviceTree:/chosen");
    uint32_t val = 0u;
    if (chosen == MACH_PORT_NULL) {
        return 0u;
    }
    CFTypeRef prop = IORegistryEntryCreateCFProperty(chosen, key, kCFAllocatorDefault, 0);
    (void)IOObjectRelease(chosen);
    if (prop != NULL) {
        if (CFGetTypeID(prop) == CFDataGetTypeID() &&
            CFDataGetLength((CFDataRef)prop) == (CFIndex)sizeof(val)) {
            memcpy(&val, CFDataGetBytePtr((CFDataRef)prop), sizeof(val));
        }
        CFRelease(prop);
    }
    return val;
}

__attribute__((constructor)) static void fuxi_appstore_proxy_init(void)
{
    const char *inserted = getenv("DYLD_INSERT_LIBRARIES");
    if (fuxi_appstore_should_unset_dyld_insert(inserted, FUXI_APPSTORE_SELF_DYLIB_PATH)) {
        (void)unsetenv("DYLD_INSERT_LIBRARIES");
    }

    const char *prog = getprogname();
    uint32_t fix_val = apsd_read_adt_u32(CFSTR("fuxi,appstore-fix"));
    uint32_t hybrid_val = apsd_read_adt_u32(CFSTR("fuxi,appstore-hybrid"));
    struct fuxi_appstore_gate_decision gate = fuxi_appstore_eval_gate(prog, fix_val, hybrid_val);
    if (!gate.enabled) {
        return;
    }

    apsd_is_appstore = true;
    __atomic_store_n(&apsd_fix_mode, fix_val, __ATOMIC_RELEASE);
    __atomic_store_n(&apsd_hybrid_mode, gate.hybrid_diag ? 1u : 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&apsd_log_mode, 1u, __ATOMIC_RELEASE);

    apsd_logf("INIT", "proc=%s fix=%u hybrid=%u", prog, fix_val, gate.hybrid_diag ? 1u : 0u);
    pthread_mutex_lock(&apsd_lock);
    apsd_install_all();
    pthread_mutex_unlock(&apsd_lock);
    _dyld_register_func_for_add_image(apsd_add_image);
}
