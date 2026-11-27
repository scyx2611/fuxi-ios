#include "FuxiAppStoreProxy.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_rewrite_accepts(const char *in_url, const char *expected)
{
    char out[2048];
    memset(out, 0x5a, sizeof(out));
    assert(fuxi_appstore_rewrite_amp_url(in_url, out, sizeof(out)));
    assert(strcmp(out, expected) == 0);

    char out2[2048];
    memset(out2, 0x5a, sizeof(out2));
    assert(!fuxi_appstore_rewrite_amp_url(out, out2, sizeof(out2)));
    assert(out2[0] == '\0');

    size_t need = strlen(expected) + 1u;
    char *exact = malloc(need);
    assert(exact != NULL);
    memset(exact, 0x5a, need);
    assert(fuxi_appstore_rewrite_amp_url(in_url, exact, need));
    assert(strcmp(exact, expected) == 0);
    free(exact);

    char *short_buf = malloc(need - 1u);
    assert(short_buf != NULL);
    memset(short_buf, 0x5a, need - 1u);
    assert(!fuxi_appstore_rewrite_amp_url(in_url, short_buf, need - 1u));
    assert(short_buf[0] == '\0');
    free(short_buf);
}

static void test_rewrite_rejects(const char *in_url)
{
    char out[512];
    memset(out, 0x5a, sizeof(out));
    assert(!fuxi_appstore_rewrite_amp_url(in_url, out, sizeof(out)));
    assert(out[0] == '\0');
}

static void test_heic_accepts(const char *in_url, const char *expected)
{
    char out[2048];
    memset(out, 0x5a, sizeof(out));
    assert(fuxi_appstore_rewrite_heic_url(in_url, out, sizeof(out)));
    assert(strcmp(out, expected) == 0);

    char out2[2048];
    memset(out2, 0x5a, sizeof(out2));
    assert(!fuxi_appstore_rewrite_heic_url(out, out2, sizeof(out2)));
    assert(out2[0] == '\0');

    size_t need = strlen(expected) + 1u;
    char *exact = malloc(need);
    assert(exact != NULL);
    memset(exact, 0x5a, need);
    assert(fuxi_appstore_rewrite_heic_url(in_url, exact, need));
    assert(strcmp(exact, expected) == 0);
    free(exact);

    char *short_buf = malloc(need - 1u);
    assert(short_buf != NULL);
    memset(short_buf, 0x5a, need - 1u);
    assert(!fuxi_appstore_rewrite_heic_url(in_url, short_buf, need - 1u));
    assert(short_buf[0] == '\0');
    free(short_buf);
}

static void test_heic_rejects(const char *in_url)
{
    char out[512];
    memset(out, 0x5a, sizeof(out));
    assert(!fuxi_appstore_rewrite_heic_url(in_url, out, sizeof(out)));
    assert(out[0] == '\0');
}

static void test_mask_url(const char *in_url, const char *expected)
{
    char out[512];
    memset(out, 0x5a, sizeof(out));
    assert(fuxi_appstore_mask_url_host_path(in_url, out, sizeof(out)));
    assert(strcmp(out, expected) == 0);
    assert(strchr(out, '?') == NULL);
    assert(strchr(out, '#') == NULL);
    assert(strchr(out, '@') == NULL);
}

int main(void)
{
    /* All three AMP hosts, mixed case, optional :443, long query */
    test_rewrite_accepts(
        "https://amp-api.apps.apple.com/v1/editorial/tw/today?platform=iphone&l=zh-Hant",
        "https://apps.apple.com/api/apps/v1/editorial/tw/today?platform=iphone&l=zh-Hant");
    test_rewrite_accepts(
        "HTTPS://AMP-API-EDGE.Apps.Apple.Com/v1/editorial/tw/groupings?platform=iphone&name=games",
        "https://apps.apple.com/api/apps/v1/editorial/tw/groupings?platform=iphone&name=games");
    test_rewrite_accepts(
        "https://amp-api-search-edge.apps.apple.com:443/v1/catalog/tw/search?term=youtube&limit=10",
        "https://apps.apple.com/api/apps/v1/catalog/tw/search?term=youtube&limit=10");

    /* Long query string preservation */
    char long_in[1400];
    char long_expected[1450];
    char filler[1025];
    memset(filler, 'a', 1024);
    filler[1024] = '\0';
    int ni = snprintf(long_in, sizeof(long_in),
        "https://amp-api-edge.apps.apple.com/v1/editorial/tw/today?platform=iphone&extend=%s", filler);
    int ne = snprintf(long_expected, sizeof(long_expected),
        "https://apps.apple.com/api/apps/v1/editorial/tw/today?platform=iphone&extend=%s", filler);
    assert(ni > 1050 && ne > 1050);
    test_rewrite_accepts(long_in, long_expected);

    /* Bag, Auth, Purchase, SAP/FairPlay, and spoofed hosts MUST NOT be rewritten */
    test_rewrite_rejects(NULL);
    test_rewrite_rejects("");
    test_rewrite_rejects("https://bag.itunes.apple.com/bag.xml?profile=AppStore");
    test_rewrite_rejects("https://idmsa.apple.com/appleauth/auth/signin");
    test_rewrite_rejects("https://gsa.apple.com/grandslam/GsService2");
    test_rewrite_rejects("https://buy.itunes.apple.com/WebObjects/MZBuy.woa/wa/buyProduct");
    test_rewrite_rejects("https://sf-api-token-service.itunes.apple.com/apiToken?clientId=appstore");
    test_rewrite_rejects("https://apps.mzstatic.com/content/test.jetpack");
    test_rewrite_rejects("https://apps.apple.com/api/apps/v1/editorial/tw/today");
    test_rewrite_rejects("https://evil-amp-api.apps.apple.com/v1/editorial/tw/today");
    test_rewrite_rejects("https://amp-api.apps.apple.com.evil.com/v1/editorial/tw/today");
    test_rewrite_rejects("http://amp-api.apps.apple.com/v1/editorial/tw/today");
    test_rewrite_rejects("https://user@amp-api.apps.apple.com/v1/editorial/tw/today");
    test_rewrite_rejects("https://amp-api.apps.apple.com:8443/v1/editorial/tw/today");
    test_rewrite_rejects("https://amp-api.apps.apple.com");
    test_rewrite_rejects("https://amp-api.apps.apple.com?a=1");
    test_rewrite_rejects("https://amp-api.apps.apple.com/api/apps/v1/editorial/tw/today");

    /* Null/zero capacity edge cases */
    assert(!fuxi_appstore_rewrite_amp_url("https://amp-api.apps.apple.com/v1/test", NULL, 128));
    char one = 'x';
    assert(!fuxi_appstore_rewrite_amp_url("https://amp-api.apps.apple.com/v1/test", &one, 0));
    assert(one == 'x');
    assert(!fuxi_appstore_rewrite_amp_url("https://amp-api.apps.apple.com/v1/test", &one, 1));
    assert(one == '\0');

    /* Header strip allow/deny matrix (includes Cookie + T5 auth headers) */
    assert(!fuxi_appstore_should_strip_header(NULL));
    assert(!fuxi_appstore_should_strip_header(""));
    assert(fuxi_appstore_should_strip_header("Authorization"));
    assert(fuxi_appstore_should_strip_header("authorization"));
    assert(fuxi_appstore_should_strip_header("Cookie"));
    assert(fuxi_appstore_should_strip_header("cookie"));
    assert(fuxi_appstore_should_strip_header("Cookie2"));
    assert(fuxi_appstore_should_strip_header("X-Apple-Store-Front"));
    assert(fuxi_appstore_should_strip_header("x-apple-store-front"));
    assert(fuxi_appstore_should_strip_header("X-Apple-ActionSignature"));
    assert(fuxi_appstore_should_strip_header("X-Apple-ADSID"));
    assert(fuxi_appstore_should_strip_header("X-Dsid"));
    assert(fuxi_appstore_should_strip_header("X-Token"));
    assert(fuxi_appstore_should_strip_header("X-Apple-AMD"));
    assert(fuxi_appstore_should_strip_header("X-Apple-AMD-M"));
    assert(fuxi_appstore_should_strip_header("X-Apple-MD-Action"));
    assert(fuxi_appstore_should_strip_header("X-Apple-MD-Data"));
    assert(fuxi_appstore_should_strip_header("x-apple-i-md"));
    assert(fuxi_appstore_should_strip_header("X-Apple-I-MD-M"));
    assert(fuxi_appstore_should_strip_header("X-Apple-I-MD-LU"));
    assert(fuxi_appstore_should_strip_header("X-Apple-I-MD-RINFO"));

    assert(!fuxi_appstore_should_strip_header("Accept"));
    assert(!fuxi_appstore_should_strip_header("Accept-Language"));
    assert(!fuxi_appstore_should_strip_header("Accept-Encoding"));
    assert(!fuxi_appstore_should_strip_header("User-Agent"));
    assert(!fuxi_appstore_should_strip_header("X-Apple-Connection-Type"));
    assert(!fuxi_appstore_should_strip_header("X-Apple-Tz"));
    assert(!fuxi_appstore_should_strip_header("X-Apple-Client-Application"));
    assert(!fuxi_appstore_should_strip_header("X-Apple-Jingle-Correlation-Key"));
    assert(!fuxi_appstore_should_strip_header("Origin"));
    assert(!fuxi_appstore_should_strip_header("Referer"));
    assert(!fuxi_appstore_should_strip_header("If-None-Match"));
    assert(!fuxi_appstore_should_strip_header("If-Modified-Since"));

    /* Synth JWT structure */
    const char *jwt = FUXI_APPSTORE_SYNTH_JWT;
    const char *d1 = strchr(jwt, '.');
    assert(d1 != NULL && d1 > jwt);
    const char *d2 = strchr(d1 + 1, '.');
    assert(d2 != NULL && d2 > d1 + 1 && d2[1] != '\0');
    assert(strchr(d2 + 1, '.') == NULL);

    /* mzstatic .heic / .heif -> .webp rewrite tests */
    test_heic_accepts(
        "https://is1-ssl.mzstatic.com/image/thumb/Features/v4/67/22/99/67229943-8d4c-1f0c-d1fb-d0d43408751f/0d743ea5-8268-4d4e-a5e9-b86f80a73bc0.png/2406x1476ss-60.heic",
        "https://is1-ssl.mzstatic.com/image/thumb/Features/v4/67/22/99/67229943-8d4c-1f0c-d1fb-d0d43408751f/0d743ea5-8268-4d4e-a5e9-b86f80a73bc0.png/2406x1476ss-60.webp");
    test_heic_accepts(
        "HTTPS://IS5-SSL.MZSTATIC.COM:443/image/thumb/a.png/100x100.HEIC?v=1",
        "HTTPS://IS5-SSL.MZSTATIC.COM:443/image/thumb/a.png/100x100.webp?v=1");
    test_heic_accepts(
        "https://is2-ssl.mzstatic.com/image/thumb/b.png/200x200.heif#frag",
        "https://is2-ssl.mzstatic.com/image/thumb/b.png/200x200.webp#frag");

    test_heic_rejects(NULL);
    test_heic_rejects("");
    test_heic_rejects("http://is1-ssl.mzstatic.com/image/thumb/a.heic");
    test_heic_rejects("https://mzstatic.com/a.heic");
    test_heic_rejects("https://evilmzstatic.com/a.heic");
    test_heic_rejects("https://is1-ssl.mzstatic.com.evil.com/a.heic");
    test_heic_rejects("https://apps.apple.com/a.heic");
    test_heic_rejects("https://is1-ssl.mzstatic.com/a.png");
    test_heic_rejects("https://is1-ssl.mzstatic.com:8443/a.heic");
    test_heic_rejects("https://user@is1-ssl.mzstatic.com/a.heic");
    test_heic_rejects("https://is1-ssl.mzstatic.com/a.heic.png");

    /* URL privacy masking tests: scheme://host/path only, no ?query / #fragment / userinfo */
    test_mask_url(
        "https://bag.itunes.apple.com/bag.xml?deviceClass=iPhone&dsid=999888&token=SECRET",
        "https://bag.itunes.apple.com/bag.xml");
    test_mask_url(
        "https://buy.itunes.apple.com/WebObjects/MZBuy.woa/wa/buyProduct?guid=ABC&kbsync=XYZ#section",
        "https://buy.itunes.apple.com/WebObjects/MZBuy.woa/wa/buyProduct");
    test_mask_url(
        "https://user:pass@idmsa.apple.com/appleauth/auth/signin?accountName=user@example.com",
        "https://idmsa.apple.com/appleauth/auth/signin");
    test_mask_url(
        "https://apps.apple.com/api/apps/v1/editorial/tw/today#top",
        "https://apps.apple.com/api/apps/v1/editorial/tw/today");
    {
        char mbuf[64];
        assert(fuxi_appstore_mask_url_host_path(
            "https://sf-api-token-service.itunes.apple.com/apiToken?X-Token=TOPSECRET&Cookie=SESS",
            mbuf, sizeof(mbuf)));
        assert(strcmp(mbuf, "https://sf-api-token-service.itunes.apple.com/apiToken") == 0);
        assert(strstr(mbuf, "TOPSECRET") == NULL);
        assert(strstr(mbuf, "SESS") == NULL);
        assert(strstr(mbuf, "X-Token") == NULL);

        assert(!fuxi_appstore_mask_url_host_path(NULL, mbuf, sizeof(mbuf)));
        assert(mbuf[0] == '\0');
        assert(!fuxi_appstore_mask_url_host_path("", mbuf, sizeof(mbuf)));
        assert(mbuf[0] == '\0');
        assert(!fuxi_appstore_mask_url_host_path("https://a.b/c", NULL, 16));
        char small[2] = { 'x', 'y' };
        assert(fuxi_appstore_mask_url_host_path("https://a.b/c?x=1", small, sizeof(small)));
        assert(strcmp(small, "h") == 0);
        char tiny[1] = { 'z' };
        assert(!fuxi_appstore_mask_url_host_path("https://a.b/c", tiny, sizeof(tiny)));
        assert(tiny[0] == '\0');
    }

    /* Adversarial relative URLs containing "://" in query/fragment and invalid schemes */
    test_mask_url(
        "/appleauth/signin?dsid=12345&password=secret&redirect=https://evil.com/cb",
        "/appleauth/signin");
    test_mask_url(
        "/appleauth/signin#password=secret://evil.com",
        "/appleauth/signin");
    test_mask_url(
        "appleauth/signin?next=https://evil.com/cb",
        "appleauth/signin");
    test_mask_url(
        "://evil.com/path?secret=1#frag",
        "://evil.com/path");
    test_mask_url(
        "1bad://evil.com/path?secret=1",
        "1bad://evil.com/path");
    test_mask_url(
        "https+custom-1.0://user:pass@idmsa.apple.com/auth?token=secret#frag",
        "https+custom-1.0://idmsa.apple.com/auth");
    {
        char qbuf[32];
        assert(!fuxi_appstore_mask_url_host_path("?password=secret://evil.com", qbuf, sizeof(qbuf)));
        assert(qbuf[0] == '\0');
        assert(!fuxi_appstore_mask_url_host_path("#password=secret://evil.com", qbuf, sizeof(qbuf)));
        assert(qbuf[0] == '\0');
    }

    /* In-place aliasing (in_url == out_url) tests */
    {
        char abuf[512];
        snprintf(abuf, sizeof(abuf), "https://amp-api.apps.apple.com/v1/editorial/tw/today?l=zh-Hant");
        assert(fuxi_appstore_rewrite_amp_url(abuf, abuf, sizeof(abuf)));
        assert(strcmp(abuf, "https://apps.apple.com/api/apps/v1/editorial/tw/today?l=zh-Hant") == 0);

        snprintf(abuf, sizeof(abuf), "https://amp-api-search-edge.apps.apple.com/v1/catalog/tw/search?term=a");
        assert(fuxi_appstore_rewrite_amp_url(abuf, abuf, sizeof(abuf)));
        assert(strcmp(abuf, "https://apps.apple.com/api/apps/v1/catalog/tw/search?term=a") == 0);

        snprintf(abuf, sizeof(abuf), "https://idmsa.apple.com/appleauth/auth/signin");
        assert(!fuxi_appstore_rewrite_amp_url(abuf, abuf, sizeof(abuf)));
        assert(abuf[0] == '\0');

        snprintf(abuf, sizeof(abuf), "https://is1-ssl.mzstatic.com/image/thumb/a.png/100x100.heic?v=1");
        assert(fuxi_appstore_rewrite_heic_url(abuf, abuf, sizeof(abuf)));
        assert(strcmp(abuf, "https://is1-ssl.mzstatic.com/image/thumb/a.png/100x100.webp?v=1") == 0);

        snprintf(abuf, sizeof(abuf), "https://is1-ssl.mzstatic.com/image/thumb/a.png");
        assert(!fuxi_appstore_rewrite_heic_url(abuf, abuf, sizeof(abuf)));
        assert(abuf[0] == '\0');

        snprintf(abuf, sizeof(abuf), "https://user:secret@idmsa.apple.com/auth?password=123");
        assert(fuxi_appstore_mask_url_host_path(abuf, abuf, sizeof(abuf)));
        assert(strcmp(abuf, "https://idmsa.apple.com/auth") == 0);

        snprintf(abuf, sizeof(abuf), "/appleauth/signin?password=secret&next=https://evil.com");
        assert(fuxi_appstore_mask_url_host_path(abuf, abuf, sizeof(abuf)));
        assert(strcmp(abuf, "/appleauth/signin") == 0);
    }

    /* Oversize userinfo (>2100B) and truncated input protection tests */
    {
        char secret_pad[2201];
        memset(secret_pad, 's', 2200);
        secret_pad[2200] = '\0';
        char huge_url[2560];
        int hn = snprintf(huge_url, sizeof(huge_url),
            "https://user:%s@idmsa.apple.com/auth?password=secret", secret_pad);
        assert(hn > 2240);

        char masked[256];
        assert(fuxi_appstore_mask_url_host_path_bounded(huge_url, false, masked, sizeof(masked)));
        assert(strcmp(masked, "https://idmsa.apple.com/auth") == 0);
        assert(strchr(masked, '@') == NULL);
        assert(strchr(masked, '?') == NULL);

        assert(fuxi_appstore_mask_url_host_path_bounded(huge_url, true, masked, sizeof(masked)));
        assert(strcmp(masked, "[url_oversize_redacted]") == 0);

        char tiny_redacted[8];
        assert(!fuxi_appstore_mask_url_host_path_bounded(huge_url, true, tiny_redacted, sizeof(tiny_redacted)));
        assert(tiny_redacted[0] == '\0');
    }

    /* DYLD_INSERT_LIBRARIES exact unset gate */
    assert(fuxi_appstore_should_unset_dyld_insert(
        "/usr/local/lib/fuxi/libfuxi-appstore.dylib",
        FUXI_APPSTORE_SELF_DYLIB_PATH));
    assert(!fuxi_appstore_should_unset_dyld_insert(
        "/usr/local/lib/fuxi/libfuxi-appstore.dylib:/tmp/other.dylib",
        FUXI_APPSTORE_SELF_DYLIB_PATH));
    assert(!fuxi_appstore_should_unset_dyld_insert(
        "/usr/local/lib/fuxi/libfuxi-sb.dylib",
        FUXI_APPSTORE_SELF_DYLIB_PATH));
    assert(!fuxi_appstore_should_unset_dyld_insert(NULL, FUXI_APPSTORE_SELF_DYLIB_PATH));
    assert(!fuxi_appstore_should_unset_dyld_insert("", FUXI_APPSTORE_SELF_DYLIB_PATH));

    /* Process / ADT gate decision matrix (Route 2 vs T5/T6 hybrid vs disabled) */
    {
        struct fuxi_appstore_gate_decision g;
        g = fuxi_appstore_eval_gate("AppStore", 1u, 0u);
        assert(g.enabled && g.proxy_mode && !g.hybrid_diag);

        g = fuxi_appstore_eval_gate("AppStore", 1u, 1u);
        assert(g.enabled && g.proxy_mode && g.hybrid_diag);

        g = fuxi_appstore_eval_gate("AppStore", 2u, 1u);
        assert(g.enabled && !g.proxy_mode && g.hybrid_diag);

        g = fuxi_appstore_eval_gate("AppStore", 0u, 1u);
        assert(!g.enabled && !g.proxy_mode && !g.hybrid_diag);

        g = fuxi_appstore_eval_gate("AppStore", 0u, 0u);
        assert(!g.enabled && !g.proxy_mode && !g.hybrid_diag);

        g = fuxi_appstore_eval_gate("appstored", 1u, 1u);
        assert(!g.enabled && !g.proxy_mode && !g.hybrid_diag);

        g = fuxi_appstore_eval_gate("SpringBoard", 1u, 1u);
        assert(!g.enabled && !g.proxy_mode && !g.hybrid_diag);

        g = fuxi_appstore_eval_gate(NULL, 1u, 1u);
        assert(!g.enabled && !g.proxy_mode && !g.hybrid_diag);
    }

    /* T6: PROMISE_ERR 204 bucketing ensures 500x Bag Missing never starves 6/100/304/AKAnisetteError */
    {
        assert(fuxi_appstore_err_is_bag_missing("AMSErrorDomain", 204L));
        assert(!fuxi_appstore_err_is_bag_missing("AMSErrorDomain", 100L));
        assert(!fuxi_appstore_err_is_bag_missing("AMSErrorDomain", 6L));
        assert(!fuxi_appstore_err_is_bag_missing("AMSErrorDomain", 304L));
        assert(!fuxi_appstore_err_is_bag_missing("AKAnisetteError", 204L));
        assert(!fuxi_appstore_err_is_bag_missing(NULL, 204L));

        unsigned bag_cnt = 0u;
        unsigned other_cnt = 0u;
        unsigned bag_logged = 0u;
        unsigned other_logged = 0u;
        for (unsigned i = 0; i < 500u; i++) {
            if (fuxi_appstore_promise_should_log(true, &bag_cnt, &other_cnt)) {
                bag_logged++;
            }
        }
        assert(bag_logged == FUXI_APPSTORE_PROMISE_204_CAP);
        assert(other_cnt == 0u);

        for (unsigned i = 0; i < 10u; i++) {
            assert(fuxi_appstore_promise_should_log(false, &bag_cnt, &other_cnt));
            other_logged++;
        }
        assert(other_logged == 10u);
        assert(other_cnt == 10u);
        assert(!fuxi_appstore_promise_should_log(false, NULL, &other_cnt));
        assert(!fuxi_appstore_promise_should_log(false, &bag_cnt, NULL));

        /* Saturation boundary: 240th other error is admitted, 241st is rejected */
        for (unsigned i = 10u; i < FUXI_APPSTORE_PROMISE_CAP; i++) {
            assert(fuxi_appstore_promise_should_log(false, &bag_cnt, &other_cnt));
            other_logged++;
        }
        assert(other_logged == FUXI_APPSTORE_PROMISE_CAP);
        assert(other_cnt == FUXI_APPSTORE_PROMISE_CAP);
        assert(!fuxi_appstore_promise_should_log(false, &bag_cnt, &other_cnt));
        assert(other_cnt == FUXI_APPSTORE_PROMISE_CAP);
    }

    /* T6: Whitelist-only error userInfo key classification (rejects all string/sensitive keys) */
    {
        assert(fuxi_appstore_classify_err_key("AMSStatusCode") == FUXI_ERR_KEY_NUM_HTTP);
        assert(fuxi_appstore_classify_err_key("AMSServerErrorCode") == FUXI_ERR_KEY_NUM_AMS_EC);
        assert(fuxi_appstore_classify_err_key("AKAnisetteError") == FUXI_ERR_KEY_NUM_AK_ANISETTE);
        assert(fuxi_appstore_classify_err_key("AKUnderlyingADIErrors") == FUXI_ERR_KEY_NUM_OR_ARR_ADI);
        assert(fuxi_appstore_classify_err_key("NSUnderlyingError") == FUXI_ERR_KEY_UNDERLYING);
        assert(fuxi_appstore_classify_err_key("NSMultipleUnderlyingErrorsKey") == FUXI_ERR_KEY_MULTI_UNDERLYING);

        assert(fuxi_appstore_classify_err_key(NULL) == FUXI_ERR_KEY_REJECT);
        assert(fuxi_appstore_classify_err_key("") == FUXI_ERR_KEY_REJECT);
        assert(fuxi_appstore_classify_err_key("NSLocalizedDescription") == FUXI_ERR_KEY_REJECT);
        assert(fuxi_appstore_classify_err_key("NSDebugDescription") == FUXI_ERR_KEY_REJECT);
        assert(fuxi_appstore_classify_err_key("NSErrorFailingURLStringKey") == FUXI_ERR_KEY_REJECT);
        assert(fuxi_appstore_classify_err_key("AMSServerErrorMessage") == FUXI_ERR_KEY_REJECT);
        assert(fuxi_appstore_classify_err_key("AKAppleID") == FUXI_ERR_KEY_REJECT);
        assert(fuxi_appstore_classify_err_key("Authorization") == FUXI_ERR_KEY_REJECT);
        assert(fuxi_appstore_classify_err_key("Cookie") == FUXI_ERR_KEY_REJECT);
        assert(fuxi_appstore_classify_err_key("X-Token") == FUXI_ERR_KEY_REJECT);
        assert(fuxi_appstore_classify_err_key("X-Dsid") == FUXI_ERR_KEY_REJECT);
        assert(fuxi_appstore_classify_err_key("password") == FUXI_ERR_KEY_REJECT);
    }

    /* T6: Shared 4-layer error tree formatting, array truncation, small buffer safety, and privacy enforcement */
    {
        char out[1024];
        fuxi_appstore_format_err_tree(NULL, out, sizeof(out));
        assert(strcmp(out, "none") == 0);

        const FuxiAppStoreErrNode str_node = {
            .magic = FUXI_APPSTORE_ERR_NODE_MAGIC,
            .kind = FUXI_ERR_VAL_STR,
            .str_val = "SECRET_SHOULD_NEVER_PRINT",
        };
        fuxi_appstore_format_err_tree(&str_node, out, sizeof(out));
        assert(strcmp(out, "<non-err>") == 0);
        assert(strstr(out, "SECRET_SHOULD_NEVER_PRINT") == NULL);

        const FuxiAppStoreErrNode v_http = { .magic = FUXI_APPSTORE_ERR_NODE_MAGIC, .kind = FUXI_ERR_VAL_INT64, .int_val = 403 };
        const FuxiAppStoreErrNode v_ec = { .magic = FUXI_APPSTORE_ERR_NODE_MAGIC, .kind = FUXI_ERR_VAL_INT64, .int_val = 5001 };
        const FuxiAppStoreErrNode v_an = { .magic = FUXI_APPSTORE_ERR_NODE_MAGIC, .kind = FUXI_ERR_VAL_INT64, .int_val = -8004 };
        const FuxiAppStoreErrNode v_adi_scalar = { .magic = FUXI_APPSTORE_ERR_NODE_MAGIC, .kind = FUXI_ERR_VAL_INT64, .int_val = -45061 };
        const FuxiAppStoreErrNode v_sec_cookie = { .magic = FUXI_APPSTORE_ERR_NODE_MAGIC, .kind = FUXI_ERR_VAL_STR, .str_val = "SECRET_COOKIE_VAL" };
        const FuxiAppStoreErrNode v_sec_tok = { .magic = FUXI_APPSTORE_ERR_NODE_MAGIC, .kind = FUXI_ERR_VAL_STR, .str_val = "SECRET_TOKEN_VAL" };
        const FuxiAppStoreErrNode v_sec_pw = { .magic = FUXI_APPSTORE_ERR_NODE_MAGIC, .kind = FUXI_ERR_VAL_STR, .str_val = "SECRET_PASSWORD_VAL" };
        const FuxiAppStoreErrNode v_sec_desc = { .magic = FUXI_APPSTORE_ERR_NODE_MAGIC, .kind = FUXI_ERR_VAL_STR, .str_val = "SECRET_LOCALIZED_DESC" };

        /* 5-layer nested NSUnderlyingError: L0 -> L1 -> L2 -> L3 -> L4 -> L5 (L5 must be truncated at depth 4) */
        const FuxiAppStoreErrNode l5 = {
            .magic = FUXI_APPSTORE_ERR_NODE_MAGIC, .kind = FUXI_ERR_VAL_ERR,
            .domain = "Layer5Domain", .code = 555,
        };
        const struct fuxi_appstore_err_kv l4_kvs[] = {
            { "AMSStatusCode", &v_http },
            { "AKUnderlyingADIErrors", &v_adi_scalar }, /* depth==4 must NOT expand adi */
            { "NSUnderlyingError", &l5 },              /* depth==4 must NOT expand L5 */
            { "NSLocalizedDescription", &v_sec_desc },
        };
        const FuxiAppStoreErrNode l4 = {
            .magic = FUXI_APPSTORE_ERR_NODE_MAGIC, .kind = FUXI_ERR_VAL_ERR,
            .domain = "Layer4Domain", .code = 444, .kvs = l4_kvs, .kv_count = 4u,
        };
        const struct fuxi_appstore_err_kv l3_kvs[] = {
            { "NSUnderlyingError", &l4 },
            { "Cookie", &v_sec_cookie },
        };
        const FuxiAppStoreErrNode l3 = {
            .magic = FUXI_APPSTORE_ERR_NODE_MAGIC, .kind = FUXI_ERR_VAL_ERR,
            .domain = "Layer3Domain", .code = 333, .kvs = l3_kvs, .kv_count = 2u,
        };
        const struct fuxi_appstore_err_kv l2_kvs[] = {
            { "NSUnderlyingError", &l3 },
            { "X-Token", &v_sec_tok },
        };
        const FuxiAppStoreErrNode l2 = {
            .magic = FUXI_APPSTORE_ERR_NODE_MAGIC, .kind = FUXI_ERR_VAL_ERR,
            .domain = "Layer2Domain", .code = 222, .kvs = l2_kvs, .kv_count = 2u,
        };
        const struct fuxi_appstore_err_kv l1_kvs[] = {
            { "AKAnisetteError", &v_an },
            { "NSUnderlyingError", &l2 },
            { "password", &v_sec_pw },
        };
        const FuxiAppStoreErrNode l1 = {
            .magic = FUXI_APPSTORE_ERR_NODE_MAGIC, .kind = FUXI_ERR_VAL_ERR,
            .domain = "Layer1Domain", .code = 111, .kvs = l1_kvs, .kv_count = 3u,
        };
        const struct fuxi_appstore_err_kv l0_kvs[] = {
            { "AMSStatusCode", &v_http },
            { "AMSServerErrorCode", &v_ec },
            { "Cookie", &v_sec_cookie },
            { "X-Token", &v_sec_tok },
            { "password", &v_sec_pw },
            { "NSLocalizedDescription", &v_sec_desc },
            { "NSUnderlyingError", &l1 },
        };
        const FuxiAppStoreErrNode l0 = {
            .magic = FUXI_APPSTORE_ERR_NODE_MAGIC, .kind = FUXI_ERR_VAL_ERR,
            .domain = "AMSErrorDomain", .code = 100, .kvs = l0_kvs, .kv_count = 7u,
        };
        fuxi_appstore_format_err_tree(&l0, out, sizeof(out));
        assert(strstr(out, "AMSErrorDomain:100 http=403 ams_ec=5001") != NULL);
        assert(strstr(out, "Layer1Domain:111 ak_an=-8004") != NULL);
        assert(strstr(out, "Layer2Domain:222") != NULL);
        assert(strstr(out, "Layer3Domain:333") != NULL);
        assert(strstr(out, "Layer4Domain:444 http=403") != NULL);
        assert(strstr(out, "adi=[") == NULL); /* L4 is at depth==4 so ADI is truncated */
        assert(strstr(out, "Layer5Domain") == NULL); /* L5 is beyond max depth 4 */
        assert(strstr(out, "SECRET_COOKIE_VAL") == NULL);
        assert(strstr(out, "SECRET_TOKEN_VAL") == NULL);
        assert(strstr(out, "SECRET_PASSWORD_VAL") == NULL);
        assert(strstr(out, "SECRET_LOCALIZED_DESC") == NULL);
        assert(strstr(out, "Cookie") == NULL);
        assert(strstr(out, "X-Token") == NULL);
        assert(strstr(out, "password") == NULL);
        assert(strstr(out, "NSLocalizedDescription") == NULL);

        /* NSMultipleUnderlyingErrorsKey (5 elements -> only first 4 emitted) + AKUnderlyingADIErrors scalar & mixed array */
        const FuxiAppStoreErrNode m0 = { .magic = FUXI_APPSTORE_ERR_NODE_MAGIC, .kind = FUXI_ERR_VAL_ERR, .domain = "M0", .code = 10 };
        const FuxiAppStoreErrNode m1 = { .magic = FUXI_APPSTORE_ERR_NODE_MAGIC, .kind = FUXI_ERR_VAL_ERR, .domain = "M1", .code = 20 };
        const FuxiAppStoreErrNode m2 = { .magic = FUXI_APPSTORE_ERR_NODE_MAGIC, .kind = FUXI_ERR_VAL_ERR, .domain = "M2", .code = 30 };
        const FuxiAppStoreErrNode m3 = { .magic = FUXI_APPSTORE_ERR_NODE_MAGIC, .kind = FUXI_ERR_VAL_ERR, .domain = "M3", .code = 40 };
        const FuxiAppStoreErrNode m4 = { .magic = FUXI_APPSTORE_ERR_NODE_MAGIC, .kind = FUXI_ERR_VAL_ERR, .domain = "M4_TRUNC", .code = 50 };
        const FuxiAppStoreErrNode *const multi_items[] = { &m0, &m1, &m2, &m3, &m4 };
        const FuxiAppStoreErrNode multi_arr = {
            .magic = FUXI_APPSTORE_ERR_NODE_MAGIC, .kind = FUXI_ERR_VAL_ARR,
            .arr_items = multi_items, .arr_count = 5u,
        };

        const FuxiAppStoreErrNode adi_n1 = { .magic = FUXI_APPSTORE_ERR_NODE_MAGIC, .kind = FUXI_ERR_VAL_INT64, .int_val = -1 };
        const FuxiAppStoreErrNode adi_e1 = { .magic = FUXI_APPSTORE_ERR_NODE_MAGIC, .kind = FUXI_ERR_VAL_ERR, .domain = "ADErr", .code = -42 };
        const FuxiAppStoreErrNode adi_n2 = { .magic = FUXI_APPSTORE_ERR_NODE_MAGIC, .kind = FUXI_ERR_VAL_INT64, .int_val = 2 };
        const FuxiAppStoreErrNode adi_n3 = { .magic = FUXI_APPSTORE_ERR_NODE_MAGIC, .kind = FUXI_ERR_VAL_INT64, .int_val = -3 };
        const FuxiAppStoreErrNode adi_n4_trunc = { .magic = FUXI_APPSTORE_ERR_NODE_MAGIC, .kind = FUXI_ERR_VAL_INT64, .int_val = 9999 };
        const FuxiAppStoreErrNode *const adi_items[] = { &adi_n1, &adi_e1, &adi_n2, &adi_n3, &adi_n4_trunc, &str_node };
        const FuxiAppStoreErrNode adi_arr = {
            .magic = FUXI_APPSTORE_ERR_NODE_MAGIC, .kind = FUXI_ERR_VAL_ARR,
            .arr_items = adi_items, .arr_count = 6u,
        };

        const struct fuxi_appstore_err_kv mix_kvs[] = {
            { "AKAnisetteError", &v_an },
            { "AKUnderlyingADIErrors", &adi_arr },
            { "NSMultipleUnderlyingErrorsKey", &multi_arr },
            { "Cookie", &v_sec_cookie },
            { "X-Token", &v_sec_tok },
        };
        const FuxiAppStoreErrNode mix_root = {
            .magic = FUXI_APPSTORE_ERR_NODE_MAGIC, .kind = FUXI_ERR_VAL_ERR,
            .domain = "AMSErrorDomain", .code = 100, .kvs = mix_kvs, .kv_count = 5u,
        };
        fuxi_appstore_format_err_tree(&mix_root, out, sizeof(out));
        assert(strstr(out, "ak_an=-8004") != NULL);
        assert(strstr(out, "adi=[-1,ADErr:-42,2,-3]") != NULL);
        assert(strstr(out, "9999") == NULL);
        assert(strstr(out, "multi=[M0:10; M1:20; M2:30; M3:40]") != NULL);
        assert(strstr(out, "M4_TRUNC") == NULL);
        assert(strstr(out, "SECRET_COOKIE_VAL") == NULL);
        assert(strstr(out, "SECRET_TOKEN_VAL") == NULL);

        /* Scalar AKUnderlyingADIErrors at depth 0 */
        const struct fuxi_appstore_err_kv scalar_adi_kvs[] = {
            { "AKUnderlyingADIErrors", &v_adi_scalar },
            { "NSLocalizedDescription", &v_sec_desc },
        };
        const FuxiAppStoreErrNode scalar_adi_root = {
            .magic = FUXI_APPSTORE_ERR_NODE_MAGIC, .kind = FUXI_ERR_VAL_ERR,
            .domain = "AKAuthenticationErrorDomain", .code = -7001, .kvs = scalar_adi_kvs, .kv_count = 2u,
        };
        fuxi_appstore_format_err_tree(&scalar_adi_root, out, sizeof(out));
        assert(strcmp(out, "AKAuthenticationErrorDomain:-7001 adi=[-45061]") == 0);

        /* >64 KVs triggers lookup_key fallback path and still filters out sensitive keys */
        struct fuxi_appstore_err_kv many_kvs[70];
        for (size_t i = 0u; i < 68u; i++) {
            many_kvs[i].key = "X-Token";
            many_kvs[i].val = &v_sec_tok;
        }
        many_kvs[68].key = "AMSStatusCode";
        many_kvs[68].val = &v_http;
        many_kvs[69].key = "AKAnisetteError";
        many_kvs[69].val = &v_an;
        const FuxiAppStoreErrNode many_root = {
            .magic = FUXI_APPSTORE_ERR_NODE_MAGIC, .kind = FUXI_ERR_VAL_ERR,
            .domain = "AMSErrorDomain", .code = 6, .kvs = many_kvs, .kv_count = 70u,
        };
        fuxi_appstore_format_err_tree(&many_root, out, sizeof(out));
        assert(strcmp(out, "AMSErrorDomain:6 http=403 ak_an=-8004") == 0);

        /* Small buffer boundary truncation without out-of-bounds write */
        char canary[32];
        memset(canary, 0x5a, sizeof(canary));
        fuxi_appstore_format_err_tree(&mix_root, canary, 0u);
        assert(canary[0] == 0x5a);
        fuxi_appstore_format_err_tree(&mix_root, canary, 1u);
        assert(canary[0] == '\0' && (unsigned char)canary[1] == 0x5a);
        fuxi_appstore_format_err_tree(&mix_root, canary, 16u);
        assert(strlen(canary) == 15u);
        assert(strncmp(canary, "AMSErrorDomain:", 15u) == 0);
        assert((unsigned char)canary[16] == 0x5a);
    }

    /* T6: Header presence flag classification & scanning (case-insensitive, empty filter, cnt > 64 guard) */
    {
        assert(fuxi_appstore_classify_diag_hdr(NULL, 0u) == FUXI_DIAG_HDR_NONE);
        assert(fuxi_appstore_classify_diag_hdr("", 0u) == FUXI_DIAG_HDR_NONE);
        assert(fuxi_appstore_classify_diag_hdr("X-Apple-ActionSignature", 23u) == FUXI_DIAG_HDR_ACTION_SIG);
        assert(fuxi_appstore_classify_diag_hdr("x-apple-actionsignature", 23u) == FUXI_DIAG_HDR_ACTION_SIG);
        assert(fuxi_appstore_classify_diag_hdr("X-APPLE-I-MD", 12u) == FUXI_DIAG_HDR_I_MD);
        assert(fuxi_appstore_classify_diag_hdr("x-apple-i-md-m", 14u) == FUXI_DIAG_HDR_I_MD_M);
        assert(fuxi_appstore_classify_diag_hdr("X-Apple-I-MD-LU", 15u) == FUXI_DIAG_HDR_NONE);
        assert(fuxi_appstore_classify_diag_hdr("Cookie", 6u) == FUXI_DIAG_HDR_NONE);
        assert(fuxi_appstore_classify_diag_hdr("X-Token", 7u) == FUXI_DIAG_HDR_NONE);
        assert(fuxi_appstore_classify_diag_hdr("Authorization", 13u) == FUXI_DIAG_HDR_NONE);

        struct fuxi_appstore_hdr_flags fl;
        const struct fuxi_appstore_hdr_entry mixed_entries[] = {
            { "Cookie", 6u, true },
            { "x-apple-actionsignature", 23u, false }, /* empty value must NOT trigger has_sig */
            { "X-APPLE-I-MD", 12u, true },
            { "x-Apple-i-Md-M", 14u, true },
            { "X-Token", 7u, true },
        };
        assert(fuxi_appstore_scan_hdr_flags(mixed_entries, 5u, &fl));
        assert(fl.has_sig == 0u && fl.has_md == 1u && fl.has_mdm == 1u);

        assert(!fuxi_appstore_scan_hdr_flags(mixed_entries, 0u, &fl));
        assert(fl.has_sig == 0u && fl.has_md == 0u && fl.has_mdm == 0u);
        assert(!fuxi_appstore_scan_hdr_flags(NULL, 1u, &fl));
        assert(!fuxi_appstore_scan_hdr_flags(mixed_entries, 1u, NULL));

        /* cnt == 64 succeeds (64th slot detected); cnt == 65 trips >64 guard */
        struct fuxi_appstore_hdr_entry arr65[65];
        for (size_t i = 0u; i < 65u; i++) {
            arr65[i].name = "User-Agent";
            arr65[i].name_len = 10u;
            arr65[i].nonempty = true;
        }
        arr65[0].name = "X-Apple-ActionSignature";
        arr65[0].name_len = 23u;
        arr65[63].name = "x-apple-i-md-m";
        arr65[63].name_len = 14u;
        assert(fuxi_appstore_scan_hdr_flags(arr65, 64u, &fl));
        assert(fl.has_sig == 1u && fl.has_md == 0u && fl.has_mdm == 1u);

        assert(!fuxi_appstore_scan_hdr_flags(arr65, 65u, &fl));
        assert(fl.has_sig == 0u && fl.has_md == 0u && fl.has_mdm == 0u);
    }

    /* T6: non-ARC *err_out guard (when succeeded=true, uninitialized/invalid *ep MUST NOT be dereferenced) */
    {
        int sentinel = 0x1234;
        const void *err_ptr = &sentinel;
        const void *const *ep_valid = &err_ptr;
        const void *const *ep_unmapped = (const void *const *)(uintptr_t)0x10;
        assert(FUXI_APPSTORE_ERR_OUT(true, ep_unmapped) == NULL);
        assert(FUXI_APPSTORE_ERR_OUT(true, ep_valid) == NULL);
        assert(FUXI_APPSTORE_ERR_OUT(false, ep_valid) == err_ptr);
        assert(FUXI_APPSTORE_ERR_OUT(false, (const void *const *)NULL) == NULL);
        assert(fuxi_appstore_select_err_ptr(true, err_ptr) == NULL);
        assert(fuxi_appstore_select_err_ptr(false, err_ptr) == err_ptr);
        assert(fuxi_appstore_select_err_ptr(false, NULL) == NULL);
    }

    /* T6: MESCAL signData independent per-stage 304 sampling (paired calls must NOT starve fairplay.signData) */
    {
        struct fuxi_appstore_mescal_sign_state fp_state = { 0u };
        struct fuxi_appstore_mescal_sign_state sess_state = { 0u };
        unsigned fp_logged = 0u;
        unsigned sess_logged = 0u;
        unsigned total_logged = 0u;
        for (unsigned pair = 1u; pair <= 50u; pair++) {
            bool fp_admit = fuxi_appstore_should_log_mescal_sign_state(
                false, "AMSErrorDomain", 304L, total_logged, &fp_state);
            if (fp_admit) {
                fp_logged++;
                total_logged++;
            }
            bool sess_admit = fuxi_appstore_should_log_mescal_sign_state(
                false, "AMSErrorDomain", 304L, total_logged, &sess_state);
            if (sess_admit) {
                sess_logged++;
                total_logged++;
            }
            bool expected_pair_admit = (pair <= FUXI_APPSTORE_MESCAL_SIGN_REPEAT_BURST) ||
                ((pair % FUXI_APPSTORE_MESCAL_SIGN_STRIDE) == 0u);
            assert(fp_admit == expected_pair_admit);
            assert(sess_admit == expected_pair_admit);
        }
        assert(fp_state.repeat_304 == 50u);
        assert(sess_state.repeat_304 == 50u);
        assert(fp_logged == 27u);   /* 24 + {32, 40, 48} */
        assert(sess_logged == 27u); /* 24 + {32, 40, 48} */
        assert(total_logged == 54u);

        /* Non-304 or ok=true resets only that stage's compression counter and logs immediately */
        assert(fuxi_appstore_should_log_mescal_sign_state(false, "AMSErrorDomain", 302L, total_logged, &fp_state));
        assert(fp_state.repeat_304 == 0u);
        assert(sess_state.repeat_304 == 50u);
        assert(fuxi_appstore_should_log_mescal_sign_state(false, "AMSErrorDomain", 304L, total_logged, &fp_state));
        assert(fp_state.repeat_304 == 1u);
        assert(fuxi_appstore_should_log_mescal_sign_state(true, NULL, 0L, total_logged, &sess_state));
        assert(sess_state.repeat_304 == 0u);
        assert(!fuxi_appstore_should_log_mescal_sign_state(true, NULL, 0L, FUXI_APPSTORE_MESCAL_CAP, &fp_state));
    }

    /* T6: Key auth/buy/store endpoint detection */
    {
        assert(fuxi_appstore_is_key_auth_buy_url("https://se-edge.itunes.apple.com/WebObjects/MZStoreElements.woa/wa/buyButtonMetaData"));
        assert(fuxi_appstore_is_key_auth_buy_url("https://auth.itunes.apple.com/auth/v1/authenticate"));
        assert(fuxi_appstore_is_key_auth_buy_url("https://buy.itunes.apple.com/WebObjects/MZBuy.woa/wa/buyProduct"));
        assert(fuxi_appstore_is_key_auth_buy_url("HTTPS://P25-BUY.ITUNES.APPLE.COM:443/WebObjects/MZBuy.woa/wa/buyProduct"));
        assert(fuxi_appstore_is_key_auth_buy_url("https://idmsa.apple.com/appleauth/auth/signin"));
        assert(fuxi_appstore_is_key_auth_buy_url("https://gsa.apple.com/grandslam/GsService2"));
        assert(fuxi_appstore_is_key_auth_buy_url("https://fpinit.itunes.apple.com/v1/signSapSetup/legacy"));
        assert(fuxi_appstore_is_key_auth_buy_url("https://sf-api-token-service.itunes.apple.com/apiToken"));
        assert(fuxi_appstore_is_key_auth_buy_url("https://bag.itunes.apple.com/bag.xml"));

        assert(!fuxi_appstore_is_key_auth_buy_url(NULL));
        assert(!fuxi_appstore_is_key_auth_buy_url(""));
        assert(!fuxi_appstore_is_key_auth_buy_url("http://gsa.apple.com/grandslam/GsService2"));
        assert(!fuxi_appstore_is_key_auth_buy_url("https://user@gsa.apple.com/grandslam/GsService2"));
        assert(!fuxi_appstore_is_key_auth_buy_url("https://evil-gsa.apple.com/grandslam/GsService2"));
        assert(!fuxi_appstore_is_key_auth_buy_url("https://gsa.apple.com.evil.com/grandslam/GsService2"));
        assert(!fuxi_appstore_is_key_auth_buy_url("https://p-buy.itunes.apple.com/WebObjects/MZBuy.woa"));
        assert(!fuxi_appstore_is_key_auth_buy_url("https://px-buy.itunes.apple.com/WebObjects/MZBuy.woa"));
        assert(!fuxi_appstore_is_key_auth_buy_url("https://amp-api.apps.apple.com/v1/editorial/tw/today"));
        assert(!fuxi_appstore_is_key_auth_buy_url("https://is1-ssl.mzstatic.com/image/thumb/a.webp"));
    }

    printf("test-appstore-proxy PASS\n");
    return 0;
}
