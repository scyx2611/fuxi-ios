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

    /* Process / ADT gate decision matrix (Route 2 vs T5 hybrid vs disabled) */
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

    printf("test-appstore-proxy PASS\n");
    return 0;
}
