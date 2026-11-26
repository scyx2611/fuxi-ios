#ifndef FUXI_APPSTORE_PROXY_H
#define FUXI_APPSTORE_PROXY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define FUXI_APPSTORE_FIX_MEDIA_PROXY 1u
#define FUXI_APPSTORE_SELF_DYLIB_PATH "/usr/local/lib/fuxi/libfuxi-appstore.dylib"

#define FUXI_APPSTORE_SYNTH_JWT \
    "eyJhbGciOiJFUzI1NiIsImtpZCI6IkZVWEkwMDAwMDEifQ." \
    "eyJpc3MiOiJGVVhJIiwiaWF0IjoxNzAwMDAwMDAwLCJleHAiOjIwMDAwMDAwMDAsInN1YiI6ImFwcHN0b3JlIn0." \
    "AAAA"

struct fuxi_appstore_gate_decision {
    bool enabled;
    bool proxy_mode;
    bool hybrid_diag;
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
