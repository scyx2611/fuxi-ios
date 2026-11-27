#ifndef FUXI_APPSTORE_PROXY_H
#define FUXI_APPSTORE_PROXY_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FUXI_APPSTORE_FIX_MEDIA_PROXY 1u
#define FUXI_APPSTORE_SELF_DYLIB_PATH "/usr/local/lib/fuxi/libfuxi-appstore.dylib"

#define FUXI_APPSTORE_PROMISE_CAP 240u
#define FUXI_APPSTORE_PROMISE_204_CAP 32u
#define FUXI_APPSTORE_MESCAL_CAP 240u
#define FUXI_APPSTORE_MESCAL_SIGN_REPEAT_BURST 24u
#define FUXI_APPSTORE_MESCAL_SIGN_STRIDE 8u
#define FUXI_APPSTORE_MAX_HDR_SCAN 64u
#define FUXI_APPSTORE_ERR_MAX_DEPTH 4
#define FUXI_APPSTORE_ERR_MAX_ARRAY 4
#define FUXI_APPSTORE_ERR_MAX_SCAN_KEYS 64u
#define FUXI_APPSTORE_ERR_NODE_MAGIC 0x46584552u

#define FUXI_APPSTORE_SYNTH_JWT \
    "eyJhbGciOiJFUzI1NiIsImtpZCI6IkZVWEkwMDAwMDEifQ." \
    "eyJpc3MiOiJGVVhJIiwiaWF0IjoxNzAwMDAwMDAwLCJleHAiOjIwMDAwMDAwMDAsInN1YiI6ImFwcHN0b3JlIn0." \
    "AAAA"

struct fuxi_appstore_gate_decision {
    bool enabled;
    bool proxy_mode;
    bool hybrid_diag;
};

enum fuxi_appstore_err_key_kind {
    FUXI_ERR_KEY_REJECT = 0,
    FUXI_ERR_KEY_NUM_HTTP = 1,
    FUXI_ERR_KEY_NUM_AMS_EC = 2,
    FUXI_ERR_KEY_NUM_AK_ANISETTE = 3,
    FUXI_ERR_KEY_NUM_OR_ARR_ADI = 4,
    FUXI_ERR_KEY_UNDERLYING = 5,
    FUXI_ERR_KEY_MULTI_UNDERLYING = 6,
};

static inline char fuxi_as_ascii_lower(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (char)(c + ('a' - 'A'));
    }
    return c;
}

static inline bool fuxi_as_eq_ci_n(const char *a, size_t alen, const char *b)
{
    size_t blen = strlen(b);
    if (alen != blen) {
        return false;
    }
    for (size_t i = 0; i < alen; i++) {
        if (fuxi_as_ascii_lower(a[i]) != fuxi_as_ascii_lower(b[i])) {
            return false;
        }
    }
    return true;
}

static inline bool fuxi_as_prefix_ci(const char *s, const char *prefix)
{
    for (size_t i = 0; prefix[i] != '\0'; i++) {
        if (s[i] == '\0' || fuxi_as_ascii_lower(s[i]) != fuxi_as_ascii_lower(prefix[i])) {
            return false;
        }
    }
    return true;
}

static inline bool fuxi_as_parse_https_authority(const char *in_url, const char **host_out,
    size_t *host_len_out, const char **slash_out)
{
    if (in_url == NULL || !fuxi_as_prefix_ci(in_url, "https://")) {
        return false;
    }

    const char *host_start = in_url + 8;
    const char *slash = NULL;
    const char *colon = NULL;
    for (const char *p = host_start; *p != '\0'; p++) {
        if (*p == '/') {
            slash = p;
            break;
        }
        if (*p == '@' || *p == '?' || *p == '#') {
            return false;
        }
        if (*p == ':') {
            if (colon != NULL) {
                return false;
            }
            colon = p;
        }
    }
    if (slash == NULL || slash == host_start) {
        return false;
    }

    size_t host_len = (size_t)(slash - host_start);
    if (colon != NULL) {
        size_t port_len = (size_t)(slash - (colon + 1));
        if (port_len != 3u || memcmp(colon + 1, "443", 3u) != 0) {
            return false;
        }
        host_len = (size_t)(colon - host_start);
        if (host_len == 0u) {
            return false;
        }
    }

    *host_out = host_start;
    *host_len_out = host_len;
    *slash_out = slash;
    return true;
}

static inline bool fuxi_appstore_rewrite_amp_url(const char *in_url, char *out_url, size_t out_cap)
{
    static const char proxy_prefix[] = "https://apps.apple.com/api/apps";
    static const char *const amp_hosts[] = {
        "amp-api.apps.apple.com",
        "amp-api-edge.apps.apple.com",
        "amp-api-search-edge.apps.apple.com",
    };

    if (out_url == NULL || out_cap == 0u) {
        return false;
    }
    if (in_url == NULL) {
        out_url[0] = '\0';
        return false;
    }

    const char *host_start = NULL;
    size_t host_len = 0u;
    const char *slash = NULL;
    if (!fuxi_as_parse_https_authority(in_url, &host_start, &host_len, &slash)) {
        out_url[0] = '\0';
        return false;
    }

    bool host_matched = false;
    for (size_t i = 0; i < sizeof(amp_hosts) / sizeof(amp_hosts[0]); i++) {
        if (fuxi_as_eq_ci_n(host_start, host_len, amp_hosts[i])) {
            host_matched = true;
            break;
        }
    }
    if (!host_matched) {
        out_url[0] = '\0';
        return false;
    }

    if (fuxi_as_prefix_ci(slash, "/api/apps/")) {
        out_url[0] = '\0';
        return false;
    }

    const size_t prefix_len = sizeof(proxy_prefix) - 1u;
    const size_t path_len = strlen(slash);
    if (prefix_len > out_cap - 1u || path_len > (out_cap - 1u) - prefix_len) {
        out_url[0] = '\0';
        return false;
    }

    memmove(out_url + prefix_len, slash, path_len + 1u);
    memcpy(out_url, proxy_prefix, prefix_len);
    return true;
}

static inline bool fuxi_appstore_rewrite_heic_url(const char *in_url, char *out_url, size_t out_cap)
{
    static const char mz_suffix[] = ".mzstatic.com";
    const size_t mz_len = sizeof(mz_suffix) - 1u;

    if (out_url == NULL || out_cap == 0u) {
        return false;
    }
    if (in_url == NULL) {
        out_url[0] = '\0';
        return false;
    }

    const char *host_start = NULL;
    size_t host_len = 0u;
    const char *slash = NULL;
    if (!fuxi_as_parse_https_authority(in_url, &host_start, &host_len, &slash)) {
        out_url[0] = '\0';
        return false;
    }

    if (host_len <= mz_len || !fuxi_as_eq_ci_n(host_start + (host_len - mz_len), mz_len, mz_suffix)) {
        out_url[0] = '\0';
        return false;
    }

    const char *path_end = slash;
    while (*path_end != '\0' && *path_end != '?' && *path_end != '#') {
        path_end++;
    }
    size_t path_len = (size_t)(path_end - slash);
    if (path_len < 6u) {
        out_url[0] = '\0';
        return false;
    }
    if (!fuxi_as_eq_ci_n(path_end - 5, 5u, ".heic") && !fuxi_as_eq_ci_n(path_end - 5, 5u, ".heif")) {
        out_url[0] = '\0';
        return false;
    }

    size_t total_len = strlen(in_url);
    if (total_len >= out_cap) {
        out_url[0] = '\0';
        return false;
    }

    size_t suffix_off = (size_t)(path_end - in_url) - 5u;
    memmove(out_url, in_url, total_len + 1u);
    memcpy(out_url + suffix_off, ".webp", 5u);
    return true;
}

static inline bool fuxi_appstore_should_strip_header(const char *name)
{
    static const char *const exact_headers[] = {
        "Authorization",
        "Cookie",
        "Cookie2",
        "X-Apple-Store-Front",
        "X-Apple-ActionSignature",
        "X-Apple-ADSID",
        "X-Dsid",
        "X-Token",
    };
    static const char *const prefix_headers[] = {
        "X-Apple-AMD",
        "X-Apple-MD",
        "X-Apple-I-MD",
    };

    if (name == NULL || name[0] == '\0') {
        return false;
    }

    size_t len = strlen(name);
    for (size_t i = 0; i < sizeof(exact_headers) / sizeof(exact_headers[0]); i++) {
        if (fuxi_as_eq_ci_n(name, len, exact_headers[i])) {
            return true;
        }
    }
    for (size_t i = 0; i < sizeof(prefix_headers) / sizeof(prefix_headers[0]); i++) {
        if (fuxi_as_prefix_ci(name, prefix_headers[i])) {
            return true;
        }
    }
    return false;
}

static inline bool fuxi_as_is_alpha(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static inline bool fuxi_as_is_scheme_cont(char c)
{
    return fuxi_as_is_alpha(c) || (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.';
}

static inline const char *fuxi_as_find_rfc3986_scheme_sep(const char *in_url)
{
    if (in_url == NULL || !fuxi_as_is_alpha(in_url[0])) {
        return NULL;
    }
    const char *p = in_url + 1;
    while (fuxi_as_is_scheme_cont(*p)) {
        p++;
    }
    if (p[0] == ':' && p[1] == '/' && p[2] == '/') {
        return p;
    }
    return NULL;
}

static inline bool fuxi_appstore_mask_url_host_path(const char *in_url, char *out, size_t cap)
{
    if (out == NULL || cap == 0u) {
        return false;
    }
    if (in_url == NULL || in_url[0] == '\0') {
        out[0] = '\0';
        return false;
    }

    const char *scheme_sep = fuxi_as_find_rfc3986_scheme_sep(in_url);
    size_t wpos = 0u;
    const char *cursor = in_url;

    if (scheme_sep != NULL) {
        size_t scheme_prefix_len = (size_t)(scheme_sep - in_url) + 3u;
        for (size_t i = 0; i < scheme_prefix_len && wpos + 1u < cap; i++) {
            out[wpos++] = in_url[i];
        }
        cursor = scheme_sep + 3;

        /* Strip any user:pass@ before host inside authority */
        const char *auth_end = cursor;
        const char *at_sign = NULL;
        while (*auth_end != '\0' && *auth_end != '/' && *auth_end != '?' && *auth_end != '#') {
            if (*auth_end == '@') {
                at_sign = auth_end;
            }
            auth_end++;
        }
        if (at_sign != NULL) {
            cursor = at_sign + 1;
        }
    }

    while (*cursor != '\0' && *cursor != '?' && *cursor != '#' && wpos + 1u < cap) {
        unsigned char c = (unsigned char)*cursor;
        out[wpos++] = (c < 0x20u || c == 0x7fu) ? ' ' : (char)c;
        cursor++;
    }
    out[wpos] = '\0';
    return wpos > 0u;
}

static inline bool fuxi_appstore_mask_url_host_path_bounded(
    const char *in_url, bool input_truncated, char *out, size_t cap)
{
    static const char redacted[] = "[url_oversize_redacted]";
    if (out == NULL || cap == 0u) {
        return false;
    }
    if (input_truncated) {
        size_t rlen = sizeof(redacted) - 1u;
        if (rlen >= cap) {
            out[0] = '\0';
            return false;
        }
        memcpy(out, redacted, rlen + 1u);
        return true;
    }
    return fuxi_appstore_mask_url_host_path(in_url, out, cap);
}

static inline bool fuxi_appstore_is_key_auth_buy_url(const char *in_url)
{
    static const char *const key_hosts[] = {
        "se-edge.itunes.apple.com",
        "auth.itunes.apple.com",
        "buy.itunes.apple.com",
        "idmsa.apple.com",
        "gsa.apple.com",
        "fpinit.itunes.apple.com",
        "sf-api-token-service.itunes.apple.com",
        "bag.itunes.apple.com",
        "init.itunes.apple.com",
    };

    const char *host_start = NULL;
    size_t host_len = 0u;
    const char *slash = NULL;
    if (!fuxi_as_parse_https_authority(in_url, &host_start, &host_len, &slash)) {
        return false;
    }
    for (size_t i = 0; i < sizeof(key_hosts) / sizeof(key_hosts[0]); i++) {
        if (fuxi_as_eq_ci_n(host_start, host_len, key_hosts[i])) {
            return true;
        }
    }
    /* Match pXX-buy.itunes.apple.com (e.g. p25-buy.itunes.apple.com) */
    static const char pbuy_suffix[] = "-buy.itunes.apple.com";
    const size_t pbuy_len = sizeof(pbuy_suffix) - 1u;
    if (host_len > pbuy_len + 1u &&
        (host_start[0] == 'p' || host_start[0] == 'P') &&
        fuxi_as_eq_ci_n(host_start + (host_len - pbuy_len), pbuy_len, pbuy_suffix)) {
        bool digits = true;
        for (size_t i = 1u; i < host_len - pbuy_len; i++) {
            if (host_start[i] < '0' || host_start[i] > '9') {
                digits = false;
                break;
            }
        }
        if (digits) {
            return true;
        }
    }
    return false;
}

/* Short-circuit evaluation prevents dereferencing uninitialized *ep when ok==true */
#define FUXI_APPSTORE_ERR_OUT(ok, ep) ((!(ok) && (ep) != NULL) ? *(ep) : NULL)

static inline const void *fuxi_appstore_select_err_ptr(bool succeeded, const void *err_val)
{
    return succeeded ? NULL : err_val;
}

enum fuxi_appstore_diag_hdr_kind {
    FUXI_DIAG_HDR_NONE = 0,
    FUXI_DIAG_HDR_ACTION_SIG = 1,
    FUXI_DIAG_HDR_I_MD = 2,
    FUXI_DIAG_HDR_I_MD_M = 3,
};

struct fuxi_appstore_hdr_entry {
    const char *name;
    size_t name_len;
    bool nonempty;
};

struct fuxi_appstore_hdr_flags {
    unsigned has_sig;
    unsigned has_md;
    unsigned has_mdm;
};

static inline enum fuxi_appstore_diag_hdr_kind fuxi_appstore_classify_diag_hdr(
    const char *name, size_t len)
{
    if (name == NULL || len == 0u) {
        return FUXI_DIAG_HDR_NONE;
    }
    if (fuxi_as_eq_ci_n(name, len, "X-Apple-ActionSignature")) {
        return FUXI_DIAG_HDR_ACTION_SIG;
    }
    if (fuxi_as_eq_ci_n(name, len, "X-Apple-I-MD")) {
        return FUXI_DIAG_HDR_I_MD;
    }
    if (fuxi_as_eq_ci_n(name, len, "X-Apple-I-MD-M")) {
        return FUXI_DIAG_HDR_I_MD_M;
    }
    return FUXI_DIAG_HDR_NONE;
}

static inline bool fuxi_appstore_scan_hdr_flags(
    const struct fuxi_appstore_hdr_entry *entries, size_t cnt,
    struct fuxi_appstore_hdr_flags *out_flags)
{
    if (out_flags == NULL) {
        return false;
    }
    out_flags->has_sig = 0u;
    out_flags->has_md = 0u;
    out_flags->has_mdm = 0u;
    if (entries == NULL || cnt == 0u || cnt > FUXI_APPSTORE_MAX_HDR_SCAN) {
        return false;
    }
    for (size_t i = 0u; i < cnt; i++) {
        if (!entries[i].nonempty) {
            continue;
        }
        enum fuxi_appstore_diag_hdr_kind kind =
            fuxi_appstore_classify_diag_hdr(entries[i].name, entries[i].name_len);
        if (kind == FUXI_DIAG_HDR_ACTION_SIG) {
            out_flags->has_sig = 1u;
        } else if (kind == FUXI_DIAG_HDR_I_MD) {
            out_flags->has_md = 1u;
        } else if (kind == FUXI_DIAG_HDR_I_MD_M) {
            out_flags->has_mdm = 1u;
        }
    }
    return true;
}

static inline bool fuxi_appstore_err_is_bag_missing(const char *domain, long code)
{
    return (domain != NULL && strcmp(domain, "AMSErrorDomain") == 0 && code == 204L);
}

static inline bool fuxi_appstore_promise_should_log(
    bool is_bag_missing, unsigned *bag_missing_count, unsigned *other_count)
{
    if (bag_missing_count == NULL || other_count == NULL) {
        return false;
    }
    if (is_bag_missing) {
        if (*bag_missing_count >= FUXI_APPSTORE_PROMISE_204_CAP) {
            return false;
        }
        (*bag_missing_count)++;
        return true;
    }
    if (*other_count >= FUXI_APPSTORE_PROMISE_CAP) {
        return false;
    }
    (*other_count)++;
    return true;
}

struct fuxi_appstore_mescal_sign_state {
    unsigned repeat_304;
};

static inline bool fuxi_appstore_should_log_mescal_sign(
    bool ok, const char *err_dom, long err_code, unsigned total_logged,
    unsigned *repeat_304_count)
{
    if (total_logged >= FUXI_APPSTORE_MESCAL_CAP) {
        return false;
    }
    if (ok || err_dom == NULL || strcmp(err_dom, "AMSErrorDomain") != 0 || err_code != 304L) {
        if (repeat_304_count != NULL) {
            *repeat_304_count = 0u;
        }
        return true;
    }
    if (repeat_304_count == NULL) {
        return true;
    }
    unsigned n = ++(*repeat_304_count);
    return (n <= FUXI_APPSTORE_MESCAL_SIGN_REPEAT_BURST) ||
        ((n % FUXI_APPSTORE_MESCAL_SIGN_STRIDE) == 0u);
}

static inline bool fuxi_appstore_should_log_mescal_sign_state(
    bool ok, const char *err_dom, long err_code, unsigned total_logged,
    struct fuxi_appstore_mescal_sign_state *state)
{
    return fuxi_appstore_should_log_mescal_sign(
        ok, err_dom, err_code, total_logged,
        state != NULL ? &state->repeat_304 : NULL);
}

static inline enum fuxi_appstore_err_key_kind fuxi_appstore_classify_err_key(const char *key)
{
    if (key == NULL || key[0] == '\0') {
        return FUXI_ERR_KEY_REJECT;
    }
    if (strcmp(key, "AMSStatusCode") == 0) {
        return FUXI_ERR_KEY_NUM_HTTP;
    }
    if (strcmp(key, "AMSServerErrorCode") == 0) {
        return FUXI_ERR_KEY_NUM_AMS_EC;
    }
    if (strcmp(key, "AKAnisetteError") == 0) {
        return FUXI_ERR_KEY_NUM_AK_ANISETTE;
    }
    if (strcmp(key, "AKUnderlyingADIErrors") == 0) {
        return FUXI_ERR_KEY_NUM_OR_ARR_ADI;
    }
    if (strcmp(key, "NSUnderlyingError") == 0) {
        return FUXI_ERR_KEY_UNDERLYING;
    }
    if (strcmp(key, "NSMultipleUnderlyingErrorsKey") == 0) {
        return FUXI_ERR_KEY_MULTI_UNDERLYING;
    }
    return FUXI_ERR_KEY_REJECT;
}

static inline void fuxi_appstore_append_err_fmt(char *buf, size_t cap, size_t *pos, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));
static inline void fuxi_appstore_append_err_fmt(char *buf, size_t cap, size_t *pos, const char *fmt, ...)
{
    if (buf == NULL || pos == NULL || cap == 0u || *pos >= cap - 1u) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *pos, cap - *pos, fmt, ap);
    va_end(ap);
    if (n > 0) {
        size_t add = (size_t)n;
        size_t avail = (cap - 1u) - *pos;
        *pos += (add < avail) ? add : avail;
    }
}

typedef void (*fuxi_appstore_kv_cb)(const char *key, const void *val, void *ctx);

struct fuxi_appstore_err_ops {
    bool (*is_err)(const void *obj);
    void (*get_dom_code)(const void *err_obj, char *dom_out, size_t dom_cap, long *code_out);
    const void *(*copy_userinfo)(const void *err_obj);
    void (*release_userinfo)(const void *info_obj);
    bool (*for_each_kv)(const void *info_obj, fuxi_appstore_kv_cb cb, void *ctx);
    bool (*lookup_key)(const void *info_obj, const char *key, const void **val_out);
    bool (*read_int64)(const void *val_obj, int64_t *out_val);
    bool (*is_array)(const void *val_obj);
    size_t (*array_count)(const void *arr_obj);
    const void *(*array_item)(const void *arr_obj, size_t index);
};

struct fuxi_appstore_err_slots {
    const void *http_val;
    const void *ams_ec_val;
    const void *ak_an_val;
    const void *adi_val;
    const void *under_val;
    const void *multi_val;
};

static inline void fuxi_as_err_slot_cb(const char *key, const void *val, void *ctx)
{
    struct fuxi_appstore_err_slots *slots = (struct fuxi_appstore_err_slots *)ctx;
    if (slots == NULL || val == NULL) {
        return;
    }
    switch (fuxi_appstore_classify_err_key(key)) {
    case FUXI_ERR_KEY_NUM_HTTP:
        if (slots->http_val == NULL) {
            slots->http_val = val;
        }
        break;
    case FUXI_ERR_KEY_NUM_AMS_EC:
        if (slots->ams_ec_val == NULL) {
            slots->ams_ec_val = val;
        }
        break;
    case FUXI_ERR_KEY_NUM_AK_ANISETTE:
        if (slots->ak_an_val == NULL) {
            slots->ak_an_val = val;
        }
        break;
    case FUXI_ERR_KEY_NUM_OR_ARR_ADI:
        if (slots->adi_val == NULL) {
            slots->adi_val = val;
        }
        break;
    case FUXI_ERR_KEY_UNDERLYING:
        if (slots->under_val == NULL) {
            slots->under_val = val;
        }
        break;
    case FUXI_ERR_KEY_MULTI_UNDERLYING:
        if (slots->multi_val == NULL) {
            slots->multi_val = val;
        }
        break;
    case FUXI_ERR_KEY_REJECT:
    default:
        break;
    }
}

static inline void fuxi_appstore_format_err_depth(
    const void *err_obj, const struct fuxi_appstore_err_ops *ops,
    char *out, size_t cap, int depth);

static inline void fuxi_appstore_format_adi_val(
    const void *val, const struct fuxi_appstore_err_ops *ops,
    char *out, size_t cap, int depth)
{
    if (out == NULL || cap == 0u) {
        return;
    }
    out[0] = '\0';
    if (val == NULL || ops == NULL || depth >= FUXI_APPSTORE_ERR_MAX_DEPTH) {
        return;
    }
    int64_t ival = 0;
    if (ops->read_int64 != NULL && ops->read_int64(val, &ival)) {
        snprintf(out, cap, "%lld", (long long)ival);
        return;
    }
    if (ops->is_array != NULL && ops->array_count != NULL && ops->array_item != NULL &&
        ops->is_array(val)) {
        size_t cnt = ops->array_count(val);
        size_t limit = cnt > FUXI_APPSTORE_ERR_MAX_ARRAY ? FUXI_APPSTORE_ERR_MAX_ARRAY : cnt;
        size_t pos = 0u;
        for (size_t i = 0u; i < limit; i++) {
            const void *item = ops->array_item(val, i);
            if (item == NULL) {
                continue;
            }
            if (ops->read_int64 != NULL && ops->read_int64(item, &ival)) {
                fuxi_appstore_append_err_fmt(out, cap, &pos, "%s%lld", pos ? "," : "", (long long)ival);
            } else if (ops->is_err != NULL && ops->is_err(item)) {
                char eb[128];
                fuxi_appstore_format_err_depth(item, ops, eb, sizeof(eb), depth + 1);
                fuxi_appstore_append_err_fmt(out, cap, &pos, "%s%s", pos ? "," : "", eb);
            }
        }
    }
}

static inline void fuxi_appstore_format_userinfo(
    const void *info, const struct fuxi_appstore_err_ops *ops,
    char *out, size_t cap, int depth)
{
    if (out == NULL || cap == 0u) {
        return;
    }
    out[0] = '\0';
    if (info == NULL || ops == NULL || cap < 2u) {
        return;
    }

    struct fuxi_appstore_err_slots slots;
    memset(&slots, 0, sizeof(slots));
    bool enumerated = (ops->for_each_kv != NULL) &&
        ops->for_each_kv(info, fuxi_as_err_slot_cb, &slots);
    if (!enumerated && ops->lookup_key != NULL) {
        static const char *const wl_keys[] = {
            "AMSStatusCode",
            "AMSServerErrorCode",
            "AKAnisetteError",
            "AKUnderlyingADIErrors",
            "NSUnderlyingError",
            "NSMultipleUnderlyingErrorsKey",
        };
        for (size_t i = 0u; i < sizeof(wl_keys) / sizeof(wl_keys[0]); i++) {
            const void *val = NULL;
            if (ops->lookup_key(info, wl_keys[i], &val) && val != NULL) {
                fuxi_as_err_slot_cb(wl_keys[i], val, &slots);
            }
        }
    }

    size_t pos = 0u;
    int64_t ival = 0;
    if (slots.http_val != NULL && ops->read_int64 != NULL && ops->read_int64(slots.http_val, &ival)) {
        fuxi_appstore_append_err_fmt(out, cap, &pos, " http=%lld", (long long)ival);
    }
    if (slots.ams_ec_val != NULL && ops->read_int64 != NULL && ops->read_int64(slots.ams_ec_val, &ival)) {
        fuxi_appstore_append_err_fmt(out, cap, &pos, " ams_ec=%lld", (long long)ival);
    }
    if (slots.ak_an_val != NULL && ops->read_int64 != NULL && ops->read_int64(slots.ak_an_val, &ival)) {
        fuxi_appstore_append_err_fmt(out, cap, &pos, " ak_an=%lld", (long long)ival);
    }

    if (depth < FUXI_APPSTORE_ERR_MAX_DEPTH) {
        if (slots.adi_val != NULL) {
            char adi[160];
            fuxi_appstore_format_adi_val(slots.adi_val, ops, adi, sizeof(adi), depth);
            if (adi[0] != '\0') {
                fuxi_appstore_append_err_fmt(out, cap, &pos, " adi=[%s]", adi);
            }
        }
        if (slots.under_val != NULL && ops->is_err != NULL && ops->is_err(slots.under_val)) {
            char under[280];
            fuxi_appstore_format_err_depth(slots.under_val, ops, under, sizeof(under), depth + 1);
            if (under[0] != '\0') {
                fuxi_appstore_append_err_fmt(out, cap, &pos, " under=[%s]", under);
            }
        }
        if (slots.multi_val != NULL && ops->is_array != NULL && ops->array_count != NULL &&
            ops->array_item != NULL && ops->is_array(slots.multi_val)) {
            size_t cnt = ops->array_count(slots.multi_val);
            if (cnt > 0u) {
                char msub[300];
                msub[0] = '\0';
                size_t mpos = 0u;
                size_t limit = cnt > FUXI_APPSTORE_ERR_MAX_ARRAY ? FUXI_APPSTORE_ERR_MAX_ARRAY : cnt;
                for (size_t i = 0u; i < limit; i++) {
                    const void *item = ops->array_item(slots.multi_val, i);
                    if (item != NULL && ops->is_err != NULL && ops->is_err(item)) {
                        char item_buf[180];
                        fuxi_appstore_format_err_depth(item, ops, item_buf, sizeof(item_buf), depth + 1);
                        if (item_buf[0] != '\0') {
                            fuxi_appstore_append_err_fmt(msub, sizeof(msub), &mpos, "%s%s", mpos ? "; " : "", item_buf);
                        }
                    }
                }
                if (msub[0] != '\0') {
                    fuxi_appstore_append_err_fmt(out, cap, &pos, " multi=[%s]", msub);
                }
            }
        }
    }
}

/* Strictly emits domain:code, whitelisted numeric status codes, and recursive error chains only. */
static inline void fuxi_appstore_format_err_depth(
    const void *err_obj, const struct fuxi_appstore_err_ops *ops,
    char *out, size_t cap, int depth)
{
    if (out == NULL || cap == 0u) {
        return;
    }
    if (cap == 1u) {
        out[0] = '\0';
        return;
    }
    if (err_obj == NULL) {
        snprintf(out, cap, "none");
        return;
    }
    if (ops == NULL || ops->is_err == NULL || !ops->is_err(err_obj)) {
        snprintf(out, cap, "<non-err>");
        return;
    }

    char dom[96];
    long code = 0L;
    dom[0] = '\0';
    if (ops->get_dom_code != NULL) {
        ops->get_dom_code(err_obj, dom, sizeof(dom), &code);
    }

    char extra[460];
    extra[0] = '\0';
    if (ops->copy_userinfo != NULL) {
        const void *info = ops->copy_userinfo(err_obj);
        if (info != NULL) {
            fuxi_appstore_format_userinfo(info, ops, extra, sizeof(extra), depth);
            if (ops->release_userinfo != NULL) {
                ops->release_userinfo(info);
            }
        }
    }
    snprintf(out, cap, "%s:%ld%s", dom[0] ? dom : "?", code, extra);
}

/* Pure-C mockable error tree representation sharing the exact core formatter above */
typedef struct FuxiAppStoreErrNode FuxiAppStoreErrNode;

enum fuxi_appstore_err_val_kind {
    FUXI_ERR_VAL_NONE = 0,
    FUXI_ERR_VAL_INT64 = 1,
    FUXI_ERR_VAL_STR = 2,
    FUXI_ERR_VAL_ERR = 3,
    FUXI_ERR_VAL_ARR = 4,
};

struct fuxi_appstore_err_kv {
    const char *key;
    const FuxiAppStoreErrNode *val;
};

struct FuxiAppStoreErrNode {
    uint32_t magic;
    enum fuxi_appstore_err_val_kind kind;
    const char *domain;
    long code;
    int64_t int_val;
    const char *str_val;
    const struct fuxi_appstore_err_kv *kvs;
    size_t kv_count;
    const FuxiAppStoreErrNode *const *arr_items;
    size_t arr_count;
};

static inline bool fuxi_as_node_is_err(const void *obj)
{
    if (obj == NULL) {
        return false;
    }
    const FuxiAppStoreErrNode *node = (const FuxiAppStoreErrNode *)obj;
    return node->magic == FUXI_APPSTORE_ERR_NODE_MAGIC && node->kind == FUXI_ERR_VAL_ERR;
}

static inline void fuxi_as_node_get_dom_code(
    const void *err_obj, char *dom_out, size_t dom_cap, long *code_out)
{
    const FuxiAppStoreErrNode *node = (const FuxiAppStoreErrNode *)err_obj;
    if (dom_out != NULL && dom_cap > 0u) {
        if (node->domain != NULL) {
            snprintf(dom_out, dom_cap, "%s", node->domain);
        } else {
            dom_out[0] = '\0';
        }
    }
    if (code_out != NULL) {
        *code_out = node->code;
    }
}

static inline const void *fuxi_as_node_copy_userinfo(const void *err_obj)
{
    const FuxiAppStoreErrNode *node = (const FuxiAppStoreErrNode *)err_obj;
    if (node->kvs == NULL || node->kv_count == 0u) {
        return NULL;
    }
    return node;
}

static inline bool fuxi_as_node_for_each_kv(const void *info_obj, fuxi_appstore_kv_cb cb, void *ctx)
{
    const FuxiAppStoreErrNode *node = (const FuxiAppStoreErrNode *)info_obj;
    if (node->kv_count > FUXI_APPSTORE_ERR_MAX_SCAN_KEYS) {
        return false;
    }
    for (size_t i = 0u; i < node->kv_count; i++) {
        cb(node->kvs[i].key, node->kvs[i].val, ctx);
    }
    return true;
}

static inline bool fuxi_as_node_lookup_key(const void *info_obj, const char *key, const void **val_out)
{
    const FuxiAppStoreErrNode *node = (const FuxiAppStoreErrNode *)info_obj;
    if (val_out == NULL || key == NULL) {
        return false;
    }
    for (size_t i = 0u; i < node->kv_count; i++) {
        if (node->kvs[i].key != NULL && strcmp(node->kvs[i].key, key) == 0) {
            *val_out = node->kvs[i].val;
            return true;
        }
    }
    return false;
}

static inline bool fuxi_as_node_read_int64(const void *val_obj, int64_t *out_val)
{
    const FuxiAppStoreErrNode *node = (const FuxiAppStoreErrNode *)val_obj;
    if (node == NULL || out_val == NULL ||
        node->magic != FUXI_APPSTORE_ERR_NODE_MAGIC || node->kind != FUXI_ERR_VAL_INT64) {
        return false;
    }
    *out_val = node->int_val;
    return true;
}

static inline bool fuxi_as_node_is_array(const void *val_obj)
{
    const FuxiAppStoreErrNode *node = (const FuxiAppStoreErrNode *)val_obj;
    return node != NULL && node->magic == FUXI_APPSTORE_ERR_NODE_MAGIC &&
        node->kind == FUXI_ERR_VAL_ARR;
}

static inline size_t fuxi_as_node_array_count(const void *arr_obj)
{
    const FuxiAppStoreErrNode *node = (const FuxiAppStoreErrNode *)arr_obj;
    if (node == NULL || node->magic != FUXI_APPSTORE_ERR_NODE_MAGIC ||
        node->kind != FUXI_ERR_VAL_ARR || node->arr_items == NULL) {
        return 0u;
    }
    return node->arr_count;
}

static inline const void *fuxi_as_node_array_item(const void *arr_obj, size_t index)
{
    const FuxiAppStoreErrNode *node = (const FuxiAppStoreErrNode *)arr_obj;
    if (node == NULL || node->magic != FUXI_APPSTORE_ERR_NODE_MAGIC ||
        node->kind != FUXI_ERR_VAL_ARR || node->arr_items == NULL || index >= node->arr_count) {
        return NULL;
    }
    return node->arr_items[index];
}

static inline void fuxi_appstore_format_err_tree(const void *root_obj, char *out, size_t cap)
{
    struct fuxi_appstore_err_ops ops;
    ops.is_err = fuxi_as_node_is_err;
    ops.get_dom_code = fuxi_as_node_get_dom_code;
    ops.copy_userinfo = fuxi_as_node_copy_userinfo;
    ops.release_userinfo = NULL;
    ops.for_each_kv = fuxi_as_node_for_each_kv;
    ops.lookup_key = fuxi_as_node_lookup_key;
    ops.read_int64 = fuxi_as_node_read_int64;
    ops.is_array = fuxi_as_node_is_array;
    ops.array_count = fuxi_as_node_array_count;
    ops.array_item = fuxi_as_node_array_item;
    fuxi_appstore_format_err_depth(root_obj, &ops, out, cap, 0);
}

static inline bool fuxi_appstore_should_unset_dyld_insert(const char *env_val, const char *self_path)
{
    if (env_val == NULL || self_path == NULL || env_val[0] == '\0' || self_path[0] == '\0') {
        return false;
    }
    return strcmp(env_val, self_path) == 0;
}

static inline struct fuxi_appstore_gate_decision fuxi_appstore_eval_gate(
    const char *progname, uint32_t appstore_fix, uint32_t appstore_hybrid)
{
    struct fuxi_appstore_gate_decision dec;
    dec.enabled = false;
    dec.proxy_mode = false;
    dec.hybrid_diag = false;

    if (progname == NULL || strcmp(progname, "AppStore") != 0) {
        return dec;
    }
    if (appstore_fix == 0u) {
        return dec;
    }
    dec.enabled = true;
    dec.proxy_mode = ((appstore_fix & FUXI_APPSTORE_FIX_MEDIA_PROXY) != 0u);
    dec.hybrid_diag = (appstore_hybrid != 0u);
    return dec;
}

#endif
