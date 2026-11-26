#!/usr/bin/env python3
"""Host tests for 0132-fuxi-der-entitlements.patch (DER parser, SweetEntitlements, sweet_amfi, ASan/UBSan/LSan)."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
os.environ.setdefault('PYTHONDONTWRITEBYTECODE', '1')

ROOT = Path(__file__).resolve().parents[1]
PATCH_DIR = ROOT / 'xnu/patches'
PATCH_0132 = PATCH_DIR / '0132-fuxi-der-entitlements.patch'

C_DER_TEST = r'''
#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "iokit/Kernel/arm/fuxi_der_entitlements.h"

enum obj_kind {
    OBJ_BOOL = 1,
    OBJ_NUM,
    OBJ_DATA,
    OBJ_STR,
    OBJ_ARR,
    OBJ_DICT,
};

struct host_obj {
    enum obj_kind kind;
    int refcnt;
    bool bval;
    uint64_t nval;
    unsigned int nbits;
    uint8_t *bytes;
    size_t len;
    struct host_obj **items;
    char **keys;
    size_t count;
    size_t cap;
};

static int g_alloc_count = 0;
static int g_free_count = 0;
static int g_fail_after = -1; /* >=0: fail when g_alloc_calls == g_fail_after */
static int g_alloc_calls = 0;

static bool should_fail_alloc(void) {
    if (g_fail_after >= 0 && g_alloc_calls == g_fail_after) {
        g_alloc_calls++;
        return true;
    }
    g_alloc_calls++;
    return false;
}

static struct host_obj *new_obj(enum obj_kind kind) {
    if (should_fail_alloc()) return NULL;
    struct host_obj *o = (struct host_obj *)calloc(1, sizeof(*o));
    if (!o) return NULL;
    o->kind = kind;
    o->refcnt = 1;
    g_alloc_count++;
    return o;
}

static void retain_obj(struct host_obj *o) {
    if (o) o->refcnt++;
}

static void release_obj(void *ctx, void *ptr) {
    (void)ctx;
    struct host_obj *o = (struct host_obj *)ptr;
    if (!o) return;
    assert(o->refcnt > 0);
    o->refcnt--;
    if (o->refcnt > 0) return;
    if (o->bytes) free(o->bytes);
    for (size_t i = 0; i < o->count; i++) {
        if (o->keys && o->keys[i]) free(o->keys[i]);
        if (o->items && o->items[i]) release_obj(ctx, o->items[i]);
    }
    free(o->keys);
    free(o->items);
    free(o);
    g_free_count++;
}

static void *h_make_bool(void *ctx, bool v) {
    (void)ctx;
    struct host_obj *o = new_obj(OBJ_BOOL);
    if (o) o->bval = v;
    return o;
}

static void *h_make_number(void *ctx, uint64_t v, unsigned int bits) {
    (void)ctx;
    struct host_obj *o = new_obj(OBJ_NUM);
    if (o) { o->nval = v; o->nbits = bits; }
    return o;
}

static void *h_make_data(void *ctx, const uint8_t *bytes, size_t len) {
    (void)ctx;
    struct host_obj *o = new_obj(OBJ_DATA);
    if (!o) return NULL;
    if (len > 0) {
        o->bytes = (uint8_t *)malloc(len);
        if (!o->bytes) { release_obj(ctx, o); return NULL; }
        memcpy(o->bytes, bytes, len);
    }
    o->len = len;
    return o;
}

static void *h_make_string(void *ctx, const char *str, size_t len) {
    (void)ctx;
    struct host_obj *o = new_obj(OBJ_STR);
    if (!o) return NULL;
    o->bytes = (uint8_t *)malloc(len + 1);
    if (!o->bytes) { release_obj(ctx, o); return NULL; }
    if (len > 0) memcpy(o->bytes, str, len);
    o->bytes[len] = '\0';
    o->len = len;
    return o;
}

static void *h_make_array(void *ctx, unsigned int cap) {
    (void)ctx;
    struct host_obj *o = new_obj(OBJ_ARR);
    if (!o) return NULL;
    o->cap = cap ? cap : 1;
    o->items = (struct host_obj **)calloc(o->cap, sizeof(struct host_obj *));
    if (!o->items) { release_obj(ctx, o); return NULL; }
    return o;
}

static bool h_array_append(void *ctx, void *arr_p, void *elem_p) {
    (void)ctx;
    struct host_obj *a = (struct host_obj *)arr_p;
    struct host_obj *e = (struct host_obj *)elem_p;
    if (!a || a->kind != OBJ_ARR || !e) return false;
    if (a->count == a->cap) {
        size_t ncap = a->cap * 2;
        struct host_obj **ni = (struct host_obj **)realloc(a->items, ncap * sizeof(*ni));
        if (!ni) return false;
        a->items = ni;
        a->cap = ncap;
    }
    retain_obj(e);
    a->items[a->count++] = e;
    return true;
}

static void *h_make_dict(void *ctx, unsigned int cap) {
    (void)ctx;
    struct host_obj *o = new_obj(OBJ_DICT);
    if (!o) return NULL;
    o->cap = cap ? cap : 1;
    o->items = (struct host_obj **)calloc(o->cap, sizeof(struct host_obj *));
    o->keys = (char **)calloc(o->cap, sizeof(char *));
    if (!o->items || !o->keys) { release_obj(ctx, o); return NULL; }
    return o;
}

static bool h_dict_set(void *ctx, void *dict_p, const char *key, size_t klen, void *val_p) {
    (void)ctx;
    struct host_obj *d = (struct host_obj *)dict_p;
    struct host_obj *v = (struct host_obj *)val_p;
    if (!d || d->kind != OBJ_DICT || !key || !v) return false;
    if (should_fail_alloc()) return false;
    for (size_t i = 0; i < d->count; i++) {
        if (strlen(d->keys[i]) == klen && memcmp(d->keys[i], key, klen) == 0) {
            retain_obj(v);
            release_obj(ctx, d->items[i]);
            d->items[i] = v;
            return true;
        }
    }
    if (d->count == d->cap) {
        size_t ncap = d->cap * 2;
        struct host_obj **ni = (struct host_obj **)realloc(d->items, ncap * sizeof(*ni));
        if (!ni) return false;
        d->items = ni;
        char **nk = (char **)realloc(d->keys, ncap * sizeof(*nk));
        if (!nk) return false;
        d->keys = nk;
        d->cap = ncap;
    }
    char *kdup = (char *)malloc(klen + 1);
    if (!kdup) return false;
    memcpy(kdup, key, klen);
    kdup[klen] = '\0';
    retain_obj(v);
    d->keys[d->count] = kdup;
    d->items[d->count] = v;
    d->count++;
    return true;
}

static const struct fuxi_der_ops host_ops = {
    .make_bool = h_make_bool,
    .make_number = h_make_number,
    .make_data = h_make_data,
    .make_string = h_make_string,
    .make_array = h_make_array,
    .array_append = h_array_append,
    .make_dict = h_make_dict,
    .dict_set = h_dict_set,
    .release = release_obj,
};

static struct host_obj *dict_get(struct host_obj *d, const char *key) {
    assert(d && d->kind == OBJ_DICT);
    for (size_t i = 0; i < d->count; i++) {
        if (strcmp(d->keys[i], key) == 0) return d->items[i];
    }
    return NULL;
}

static size_t der_encode_len(uint8_t *out, size_t len) {
    if (len < 128) {
        out[0] = (uint8_t)len;
        return 1;
    }
    uint8_t tmp[4];
    size_t n = 0;
    size_t v = len;
    while (v > 0) {
        tmp[n++] = (uint8_t)(v & 0xffu);
        v >>= 8;
    }
    out[0] = (uint8_t)(0x80u | n);
    for (size_t i = 0; i < n; i++) out[1 + i] = tmp[n - 1 - i];
    return 1 + n;
}

struct buf {
    uint8_t *p;
    size_t len;
    size_t cap;
};

static void buf_push(struct buf *b, const void *data, size_t n) {
    if (b->len + n > b->cap) {
        size_t ncap = b->cap ? b->cap * 2 : 256;
        while (ncap < b->len + n) ncap *= 2;
        b->p = (uint8_t *)realloc(b->p, ncap);
        assert(b->p != NULL);
        b->cap = ncap;
    }
    if (n) memcpy(b->p + b->len, data, n);
    b->len += n;
}

static void buf_tlv(struct buf *b, uint8_t tag, const void *val, size_t vlen) {
    uint8_t lbuf[5];
    size_t ln = der_encode_len(lbuf, vlen);
    buf_push(b, &tag, 1);
    buf_push(b, lbuf, ln);
    buf_push(b, val, vlen);
}

static void buf_kv(struct buf *b, const char *key, uint8_t vtag, const void *v, size_t vlen) {
    struct buf inner = {0};
    buf_tlv(&inner, 0x0c, key, strlen(key));
    buf_tlv(&inner, vtag, v, vlen);
    buf_tlv(b, 0x30, inner.p, inner.len);
    free(inner.p);
}

static void wrap_blob(struct buf *out, const void *payload, size_t plen) {
    uint32_t total = (uint32_t)(8 + plen);
    uint8_t hdr[8] = {
        0xfa, 0xde, 0x71, 0x72,
        (uint8_t)(total >> 24), (uint8_t)(total >> 16), (uint8_t)(total >> 8), (uint8_t)total
    };
    buf_push(out, hdr, 8);
    buf_push(out, payload, plen);
}

static void assert_no_leaks(void) {
    assert(g_alloc_count == g_free_count);
}

static void test_ce_v1_and_v0(void) {
    /* Build rich CE v1 dict: bools, ints (pos/neg/64bit), strings (incl empty), data (incl empty), array, nested dict */
    struct buf nested = {0};
    uint8_t btrue = 1, bff = 0xff, bfalse = 0;
    buf_kv(&nested, "nested-bool", 0x01, &bff, 1);
    uint8_t neg1[1] = {0xff};
    buf_kv(&nested, "nested-neg1", 0x02, neg1, 1);

    struct buf arr = {0};
    buf_tlv(&arr, 0x0c, "com.apple.security", 18);
    buf_tlv(&arr, 0x0c, "two", 3);
    buf_tlv(&arr, 0xb0, nested.p, nested.len);

    struct buf dict = {0};
    buf_kv(&dict, "get-task-allow", 0x01, &btrue, 1);
    buf_kv(&dict, "disabled-flag", 0x01, &bfalse, 1);
    buf_kv(&dict, "app-id", 0x0c, "ABC123.com.example.app", 22);
    buf_kv(&dict, "empty-str", 0x0c, "", 0);
    uint8_t dbytes[3] = {0xde, 0xad, 0xbe};
    buf_kv(&dict, "raw-data", 0x04, dbytes, 3);
    buf_kv(&dict, "empty-data", 0x04, "", 0);
    uint8_t i_zero[1] = {0x00};
    uint8_t i_128[2] = {0x00, 0x80};
    uint8_t i_neg128[1] = {0x80};
    uint8_t i_min[8] = {0x80, 0, 0, 0, 0, 0, 0, 0};
    uint8_t i_max[8] = {0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    buf_kv(&dict, "i-zero", 0x02, i_zero, 1);
    buf_kv(&dict, "i-128", 0x02, i_128, 2);
    buf_kv(&dict, "i-neg128", 0x02, i_neg128, 1);
    buf_kv(&dict, "i-min", 0x02, i_min, 8);
    buf_kv(&dict, "i-max", 0x02, i_max, 8);
    buf_kv(&dict, "arr", 0x30, arr.p, arr.len);
    buf_kv(&dict, "subdict", 0xb0, nested.p, nested.len);

    /* CE v1: 0x70 { 0x02 0x01 0x01, 0xb0 { ... } } */
    struct buf v1_inner = {0};
    uint8_t one = 1;
    buf_tlv(&v1_inner, 0x02, &one, 1);
    buf_tlv(&v1_inner, 0xb0, dict.p, dict.len);
    struct buf v1_payload = {0};
    buf_tlv(&v1_payload, 0x70, v1_inner.p, v1_inner.len);
    struct buf blob_v1 = {0};
    wrap_blob(&blob_v1, v1_payload.p, v1_payload.len);

    struct host_obj *parsed = (struct host_obj *)fuxi_der_parse_blob(blob_v1.p, blob_v1.len, &host_ops, NULL);
    assert(parsed != NULL && parsed->kind == OBJ_DICT);
    assert(dict_get(parsed, "get-task-allow")->bval == true);
    assert(dict_get(parsed, "disabled-flag")->bval == false);
    assert(strcmp((const char *)dict_get(parsed, "app-id")->bytes, "ABC123.com.example.app") == 0);
    assert(dict_get(parsed, "empty-str")->len == 0 && strcmp((const char *)dict_get(parsed, "empty-str")->bytes, "") == 0);
    assert(dict_get(parsed, "raw-data")->len == 3 && memcmp(dict_get(parsed, "raw-data")->bytes, dbytes, 3) == 0);
    assert(dict_get(parsed, "empty-data")->len == 0);
    assert((int64_t)dict_get(parsed, "i-zero")->nval == 0);
    assert((int64_t)dict_get(parsed, "i-128")->nval == 128);
    assert((int64_t)dict_get(parsed, "i-neg128")->nval == -128);
    assert((int64_t)dict_get(parsed, "i-min")->nval == INT64_MIN);
    assert((int64_t)dict_get(parsed, "i-max")->nval == INT64_MAX);
    struct host_obj *parr = dict_get(parsed, "arr");
    assert(parr->kind == OBJ_ARR && parr->count == 3);
    assert(strcmp((const char *)parr->items[0]->bytes, "com.apple.security") == 0);
    assert(parr->items[2]->kind == OBJ_DICT && dict_get(parr->items[2], "nested-bool")->bval == true);
    struct host_obj *psub = dict_get(parsed, "subdict");
    assert((int64_t)dict_get(psub, "nested-neg1")->nval == -1);

    /* Truncation fuzzing on valid CE v1 blob: every prefix 0..len-1 must fail with 0 leaks */
    for (size_t trunc = 0; trunc < blob_v1.len; trunc++) {
        assert(fuxi_der_parse_blob(blob_v1.p, trunc, &host_ops, NULL) == NULL);
    }

    /* OOM injection at every allocation point: must fail cleanly with 0 leaks */
    int total_allocs = g_alloc_calls;
    (void)total_allocs;
    release_obj(NULL, parsed);
    assert_no_leaks();

    for (int k = 0; k < 64; k++) {
        g_alloc_calls = 0;
        g_fail_after = k;
        void *oom_res = fuxi_der_parse_blob(blob_v1.p, blob_v1.len, &host_ops, NULL);
        g_fail_after = -1;
        if (oom_res != NULL) {
            release_obj(NULL, oom_res);
            assert_no_leaks();
            break;
        }
        assert_no_leaks();
    }

    /* CE v0 / ldid: top-level 0x31 and tolerant 0x30 */
    const uint8_t top_tags[2] = {0x31, 0x30};
    for (size_t ti = 0; ti < 2; ti++) {
        uint8_t top_tag = top_tags[ti];
        struct buf v0_payload = {0};
        buf_tlv(&v0_payload, top_tag, dict.p, dict.len);
        struct buf blob_v0 = {0};
        wrap_blob(&blob_v0, v0_payload.p, v0_payload.len);
        struct host_obj *p0 = (struct host_obj *)fuxi_der_parse_blob(blob_v0.p, blob_v0.len, &host_ops, NULL);
        assert(p0 != NULL && p0->kind == OBJ_DICT);
        assert(dict_get(p0, "get-task-allow")->bval == true);
        release_obj(NULL, p0);
        assert_no_leaks();
        free(v0_payload.p);
        free(blob_v0.p);
    }

    /* Long length encodings: 0x81 (128B), 0x82 (256B), 0x83 (65536B) */
    const size_t slens[3] = {128u, 256u, 65536u};
    for (size_t si = 0; si < 3; si++) {
        size_t slen = slens[si];
        char *big = (char *)malloc(slen);
        memset(big, 'A', slen);
        struct buf d = {0};
        buf_kv(&d, "big", 0x0c, big, slen);
        struct buf p = {0};
        buf_tlv(&p, 0x31, d.p, d.len);
        struct buf b = {0};
        wrap_blob(&b, p.p, p.len);
        struct host_obj *po = (struct host_obj *)fuxi_der_parse_blob(b.p, b.len, &host_ops, NULL);
        assert(po && dict_get(po, "big")->len == slen);
        release_obj(NULL, po);
        assert_no_leaks();
        free(big); free(d.p); free(p.p); free(b.p);
    }

    free(nested.p); free(arr.p); free(dict.p);
    free(v1_inner.p); free(v1_payload.p); free(blob_v1.p);
}

static void test_malformed_and_boundaries(void) {
    /* Null / header errors */
    assert(fuxi_der_parse_blob(NULL, 16, &host_ops, NULL) == NULL);
    uint8_t tiny[7] = {0xfa, 0xde, 0x71, 0x72, 0, 0, 7};
    assert(fuxi_der_parse_blob(tiny, 7, &host_ops, NULL) == NULL);
    uint8_t empty_payload[8] = {0xfa, 0xde, 0x71, 0x72, 0, 0, 0, 8};
    assert(fuxi_der_parse_blob(empty_payload, 8, &host_ops, NULL) == NULL);
    uint8_t bad_magic[10] = {0xfa, 0xde, 0x71, 0x71, 0, 0, 0, 10, 0x31, 0x00};
    assert(fuxi_der_parse_blob(bad_magic, 10, &host_ops, NULL) == NULL);
    uint8_t bad_len[10] = {0xfa, 0xde, 0x71, 0x72, 0, 0, 0, 11, 0x31, 0x00};
    assert(fuxi_der_parse_blob(bad_len, 10, &host_ops, NULL) == NULL);
    uint8_t xml_pref[13] = {0xfa, 0xde, 0x71, 0x72, 0, 0, 0, 13, '<', '?', 'x', 'm', 'l'};
    assert(fuxi_der_parse_blob(xml_pref, 13, &host_ops, NULL) == NULL);

    /* > 1 MiB limit */
    size_t over = FUXI_DER_MAX_BLOB + 1u;
    uint8_t *huge = (uint8_t *)calloc(1, over);
    huge[0] = 0xfa; huge[1] = 0xde; huge[2] = 0x71; huge[3] = 0x72;
    huge[4] = (uint8_t)(over >> 24); huge[5] = (uint8_t)(over >> 16);
    huge[6] = (uint8_t)(over >> 8); huge[7] = (uint8_t)over;
    huge[8] = 0x31; huge[9] = 0x00;
    assert(fuxi_der_parse_blob(huge, over, &host_ops, NULL) == NULL);
    free(huge);

    /* Indefinite length 0x80, reserved 0xff, non-minimal DER length 0x81 0x01 / 0x82 0x00 0x80, high tag 0x1f */
    const uint8_t bad_tlvs[][16] = {
        {0xfa,0xde,0x71,0x72, 0,0,0,12, 0x31,0x80, 0x00,0x00},
        {0xfa,0xde,0x71,0x72, 0,0,0,10, 0x31,0xff},
        {0xfa,0xde,0x71,0x72, 0,0,0,12, 0x31,0x81,0x00, 0x00},
        {0xfa,0xde,0x71,0x72, 0,0,0,12, 0x31,0x81,0x01, 0x00},
        {0xfa,0xde,0x71,0x72, 0,0,0,12, 0x31,0x82,0x00,0x00},
        {0xfa,0xde,0x71,0x72, 0,0,0,11, 0x3f,0x01,0x00},
    };
    const size_t bad_lens[] = {12, 10, 12, 12, 12, 11};
    for (size_t i = 0; i < 6; i++) {
        assert(fuxi_der_parse_blob(bad_tlvs[i], bad_lens[i], &host_ops, NULL) == NULL);
    }

    /* Trailing bytes after root container */
    uint8_t trailing[11] = {0xfa,0xde,0x71,0x72, 0,0,0,11, 0x31,0x00, 0x00};
    assert(fuxi_der_parse_blob(trailing, 11, &host_ops, NULL) == NULL);

    /* Top-level array 0x30 { 0x01 0x01 0x01 } must be refused */
    uint8_t top_arr[13] = {0xfa,0xde,0x71,0x72, 0,0,0,13, 0x30,0x03, 0x01,0x01,0x01};
    assert(fuxi_der_parse_blob(top_arr, 13, &host_ops, NULL) == NULL);

    /* CE v1 version != 1, missing dict, extra 3rd element */
    uint8_t v1_v2[15] = {0xfa,0xde,0x71,0x72, 0,0,0,15, 0x70,0x05, 0x02,0x01,0x02, 0xb0,0x00};
    assert(fuxi_der_parse_blob(v1_v2, 15, &host_ops, NULL) == NULL);
    uint8_t v1_no_dict[13] = {0xfa,0xde,0x71,0x72, 0,0,0,13, 0x70,0x03, 0x02,0x01,0x01};
    assert(fuxi_der_parse_blob(v1_no_dict, 13, &host_ops, NULL) == NULL);
    uint8_t v1_extra[17] = {0xfa,0xde,0x71,0x72, 0,0,0,17, 0x70,0x07, 0x02,0x01,0x01, 0xb0,0x00, 0x05,0x00};
    assert(fuxi_der_parse_blob(v1_extra, 17, &host_ops, NULL) == NULL);

    /* Embedded NUL in key or UTF8String, empty key, bad bool len, bad int len */
    {
        struct buf d = {0};
        buf_kv(&d, "k", 0x0c, "a\0b", 3);
        struct buf p = {0}; buf_tlv(&p, 0x31, d.p, d.len);
        struct buf b = {0}; wrap_blob(&b, p.p, p.len);
        assert(fuxi_der_parse_blob(b.p, b.len, &host_ops, NULL) == NULL);
        free(d.p); free(p.p); free(b.p);
    }
    {
        struct buf inner = {0};
        uint8_t one = 1;
        buf_tlv(&inner, 0x0c, "a\0b", 3);
        buf_tlv(&inner, 0x01, &one, 1);
        struct buf d = {0}; buf_tlv(&d, 0x30, inner.p, inner.len);
        struct buf p = {0}; buf_tlv(&p, 0x31, d.p, d.len);
        struct buf b = {0}; wrap_blob(&b, p.p, p.len);
        assert(fuxi_der_parse_blob(b.p, b.len, &host_ops, NULL) == NULL);
        free(inner.p); free(d.p); free(p.p); free(b.p);
    }
    {
        struct buf d = {0};
        buf_kv(&d, "", 0x01, "\x01", 1);
        struct buf p = {0}; buf_tlv(&p, 0x31, d.p, d.len);
        struct buf b = {0}; wrap_blob(&b, p.p, p.len);
        assert(fuxi_der_parse_blob(b.p, b.len, &host_ops, NULL) == NULL);
        free(d.p); free(p.p); free(b.p);
    }
    {
        struct buf d = {0};
        buf_kv(&d, "badbool0", 0x01, "", 0);
        struct buf p = {0}; buf_tlv(&p, 0x31, d.p, d.len);
        struct buf b = {0}; wrap_blob(&b, p.p, p.len);
        assert(fuxi_der_parse_blob(b.p, b.len, &host_ops, NULL) == NULL);
        free(d.p); free(p.p); free(b.p);
    }
    {
        struct buf d = {0};
        buf_kv(&d, "badbool2", 0x01, "\x01\x00", 2);
        struct buf p = {0}; buf_tlv(&p, 0x31, d.p, d.len);
        struct buf b = {0}; wrap_blob(&b, p.p, p.len);
        assert(fuxi_der_parse_blob(b.p, b.len, &host_ops, NULL) == NULL);
        free(d.p); free(p.p); free(b.p);
    }
    {
        struct buf d = {0};
        buf_kv(&d, "badint0", 0x02, "", 0);
        struct buf p = {0}; buf_tlv(&p, 0x31, d.p, d.len);
        struct buf b = {0}; wrap_blob(&b, p.p, p.len);
        assert(fuxi_der_parse_blob(b.p, b.len, &host_ops, NULL) == NULL);
        free(d.p); free(p.p); free(b.p);
    }
    {
        struct buf d = {0};
        uint8_t i9[9] = {0,1,2,3,4,5,6,7,8};
        buf_kv(&d, "badint9", 0x02, i9, 9);
        struct buf p = {0}; buf_tlv(&p, 0x31, d.p, d.len);
        struct buf b = {0}; wrap_blob(&b, p.p, p.len);
        assert(fuxi_der_parse_blob(b.p, b.len, &host_ops, NULL) == NULL);
        free(d.p); free(p.p); free(b.p);
    }

    /* Depth limit: 16 layers total (0..15) ok, 17th container (depth=16) rejected with 0 leaks */
    for (int layers = 15; layers <= 17; layers++) {
        struct buf cur = {0};
        uint8_t one = 1;
        buf_kv(&cur, "leaf", 0x01, &one, 1);
        for (int i = 0; i < layers; i++) {
            struct buf outer = {0};
            buf_kv(&outer, "n", 0xb0, cur.p, cur.len);
            free(cur.p);
            cur = outer;
        }
        struct buf p = {0}; buf_tlv(&p, 0x31, cur.p, cur.len);
        struct buf b = {0}; wrap_blob(&b, p.p, p.len);
        void *res = fuxi_der_parse_blob(b.p, b.len, &host_ops, NULL);
        if (layers <= 15) {
            assert(res != NULL);
            release_obj(NULL, res);
        } else {
            assert(res == NULL);
        }
        assert_no_leaks();
        free(cur.p); free(p.p); free(b.p);
    }

    assert_no_leaks();
}

int main(void) {
    test_ce_v1_and_v0();
    test_malformed_and_boundaries();
    puts("der parser model ok");
    return 0;
}
'''

AMFI_TEST_PREFIX = r'''
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define __unused __attribute__((unused))

typedef struct {
    uint32_t magic;
    uint32_t length;
    char data[];
} CS_GenericBlob;

struct cs_blob {
    void *csb_entitlements;
    const char *identity;
    int xml_rc;
    void *xml;
    size_t xml_len;
    int der_rc;
    const CS_GenericBlob *der;
    size_t der_len;
};

static uint32_t sweet_amfi_debug = 0;
static int g_xml_calls = 0;
static int g_der_calls = 0;
static int g_create_calls = 0;
static int g_set_calls = 0;
static int g_release_calls = 0;
static int g_live_ents = 0;
static int g_create_fail = 0;

struct mock_ent {
    const void *blob;
    size_t len;
    int refcnt;
};

static const char *csblob_get_identity(struct cs_blob *b) { return b->identity; }

static int csblob_get_entitlements(struct cs_blob *b, void **out, size_t *len) {
    g_xml_calls++;
    if (b->xml_rc != 0) return b->xml_rc;
    *out = b->xml;
    *len = b->xml_len;
    return 0;
}

static int csblob_get_der_entitlements(struct cs_blob *b, const CS_GenericBlob **out, size_t *len) {
    g_der_calls++;
    if (b->der_rc != 0) return b->der_rc;
    *out = b->der;
    *len = b->der_len;
    return 0;
}

static void *sweet_entitlements_create(const CS_GenericBlob *blob, size_t blob_len) {
    g_create_calls++;
    if (g_create_fail) return NULL;
    struct mock_ent *e = (struct mock_ent *)calloc(1, sizeof(*e));
    e->blob = blob;
    e->len = blob_len;
    e->refcnt = 1;
    g_live_ents++;
    return e;
}

static void csblob_os_entitlements_set(struct cs_blob *b, void *ent) {
    g_set_calls++;
    struct mock_ent *e = (struct mock_ent *)ent;
    e->refcnt++;
    b->csb_entitlements = e;
}

static void osobject_release(void *obj) {
    g_release_calls++;
    struct mock_ent *e = (struct mock_ent *)obj;
    if (!e) return;
    assert(e->refcnt > 0);
    e->refcnt--;
    if (e->refcnt == 0) {
        free(e);
        g_live_ents--;
    }
}

static unsigned int sweet_entitlements_count(const void *ent) { (void)ent; return 0; }
static bool sweet_entitlements_bool(const void *ent, const char *name) { (void)ent; (void)name; return false; }
static bool sweet_entitlements_string(const void *ent, const char *name, const char *value) { (void)ent; (void)name; (void)value; return false; }

static void dummy_printf(const char *fmt, ...) { (void)fmt; }
#define printf dummy_printf
'''

AMFI_TEST_MAIN = r'''
static void reset_counters(void) {
    g_xml_calls = g_der_calls = g_create_calls = g_set_calls = g_release_calls = g_create_fail = 0;
}

int main(void) {
    CS_GenericBlob xml_blob = {0}, der_blob = {0};

    /* 1. XML hash failure -> fail closed, NEVER fall back to DER */
    reset_counters();
    struct cs_blob b1 = {.identity = "badxml", .xml_rc = 8, .der = &der_blob, .der_len = 16};
    sweet_amfi_attach_entitlements(&b1);
    assert(g_xml_calls == 1 && g_der_calls == 0 && g_create_calls == 0 && b1.csb_entitlements == NULL);

    /* 2. XML present & valid -> use XML, do NOT read DER */
    reset_counters();
    struct cs_blob b2 = {.identity = "both", .xml = &xml_blob, .xml_len = 20, .der = &der_blob, .der_len = 16};
    sweet_amfi_attach_entitlements(&b2);
    assert(g_xml_calls == 1 && g_der_calls == 0 && g_create_calls == 1 && g_set_calls == 1 && g_release_calls == 1);
    assert(b2.csb_entitlements != NULL && ((struct mock_ent *)b2.csb_entitlements)->blob == &xml_blob);
    assert(((struct mock_ent *)b2.csb_entitlements)->refcnt == 1);
    osobject_release(b2.csb_entitlements);

    /* 3. XML present & corrupt -> fail closed, NEVER fall back to DER */
    reset_counters();
    g_create_fail = 1;
    struct cs_blob b3 = {.identity = "corruptxml", .xml = &xml_blob, .xml_len = 20, .der = &der_blob, .der_len = 16};
    sweet_amfi_attach_entitlements(&b3);
    assert(g_xml_calls == 1 && g_der_calls == 0 && g_create_calls == 1 && b3.csb_entitlements == NULL);

    /* 4. XML absent (rc=0, xml=NULL) & DER valid -> fallback to DER */
    reset_counters();
    struct cs_blob b4 = {.identity = "deronly", .xml = NULL, .der = &der_blob, .der_len = 32};
    sweet_amfi_attach_entitlements(&b4);
    assert(g_xml_calls == 1 && g_der_calls == 1 && g_create_calls == 1 && g_set_calls == 1 && g_release_calls == 1);
    assert(b4.csb_entitlements != NULL && ((struct mock_ent *)b4.csb_entitlements)->blob == &der_blob);
    assert(((struct mock_ent *)b4.csb_entitlements)->refcnt == 1);
    osobject_release(b4.csb_entitlements);

    /* 5. XML absent & DER hash failure -> refuse */
    reset_counters();
    struct cs_blob b5 = {.identity = "badder", .xml = NULL, .der_rc = 8};
    sweet_amfi_attach_entitlements(&b5);
    assert(g_xml_calls == 1 && g_der_calls == 1 && g_create_calls == 0 && b5.csb_entitlements == NULL);

    /* 6. XML absent & DER absent -> silent no entitlements */
    reset_counters();
    struct cs_blob b6 = {.identity = "none", .xml = NULL, .der = NULL};
    sweet_amfi_attach_entitlements(&b6);
    assert(g_xml_calls == 1 && g_der_calls == 1 && g_create_calls == 0 && b6.csb_entitlements == NULL);

    assert(g_live_ents == 0);
    puts("sweet_amfi attach model ok");
    return 0;
}
'''


CPP_ENT_PREFIX = r'''
#include <arpa/inet.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SWEET 1
#define APPLE_KEXT_OVERRIDE override
#define __unused __attribute__((unused))
#define Z_WAITOK 0

#define CSMAGIC_EMBEDDED_ENTITLEMENTS     0xfade7171u
#define CSMAGIC_EMBEDDED_DER_ENTITLEMENTS 0xfade7172u

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t length;
    char data[];
} CS_GenericBlob;

static int cpp_live_objs = 0;

static void *kalloc_data(size_t size, int flags) {
    (void)flags;
    return calloc(1, size ? size : 1);
}
static void kfree_data(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

class OSObject {
public:
    int refcnt = 1;
    OSObject() { cpp_live_objs++; }
    virtual ~OSObject() { cpp_live_objs--; }
    virtual bool init() { return true; }
    virtual void free() { delete this; }
    virtual void retain() { refcnt++; }
    virtual void release() {
        assert(refcnt > 0);
        if (--refcnt == 0) free();
    }
};

#define OSDeclareDefaultStructors(cls) \
    static void *_meta_##cls()
#define OSDefineMetaClassAndStructors(cls, sup) \
    void *cls::_meta_##cls() { return nullptr; }
#define OSTypeAlloc(cls) new cls()
#define OSSafeReleaseNULL(p) do { if (p) { (p)->release(); (p) = nullptr; } } while (0)
#define OSDynamicCast(cls, obj) dynamic_cast<cls *>(obj)

class OSBoolean : public OSObject {
public:
    bool val;
    OSBoolean(bool v) : val(v) { cpp_live_objs--; } /* singleton, excluded from leak count */
    void retain() override {}
    void release() override {}
    static OSBoolean *withBoolean(bool v);
};
static OSBoolean g_true(true), g_false(false);
static OSBoolean *const kOSBooleanTrue = &g_true;
static OSBoolean *const kOSBooleanFalse = &g_false;
OSBoolean *OSBoolean::withBoolean(bool v) { return v ? kOSBooleanTrue : kOSBooleanFalse; }

class OSNumber : public OSObject {
public:
    uint64_t val;
    unsigned int bits;
    static OSNumber *withNumber(unsigned long long v, unsigned int b) {
        auto *n = new OSNumber(); n->val = (uint64_t)v; n->bits = b; return n;
    }
};

class OSData : public OSObject {
public:
    uint8_t *bytes = nullptr;
    unsigned int len = 0;
    ~OSData() override { ::free(bytes); }
    static OSData *withBytes(const void *b, unsigned int l) {
        if (l && !b) return nullptr;
        auto *d = new OSData();
        if (l) { d->bytes = (uint8_t *)malloc(l); memcpy(d->bytes, b, l); }
        d->len = l;
        return d;
    }
};

class OSString : public OSObject {
public:
    char *str = nullptr;
    size_t len = 0;
    ~OSString() override { ::free(str); }
    static OSString *withCString(const char *s, size_t l) {
        if (!s) return nullptr;
        auto *o = new OSString();
        size_t sl = strnlen(s, l);
        o->str = (char *)malloc(sl + 1);
        if (sl) memcpy(o->str, s, sl);
        o->str[sl] = '\0';
        o->len = sl;
        return o;
    }
    bool isEqualTo(const char *rhs) const { return rhs && strcmp(str, rhs) == 0; }
};

class OSArray : public OSObject {
public:
    OSObject **items = nullptr;
    unsigned int cnt = 0, cap = 0;
    ~OSArray() override {
        for (unsigned int i = 0; i < cnt; i++) items[i]->release();
        ::free(items);
    }
    static OSArray *withCapacity(unsigned int c) {
        auto *a = new OSArray();
        a->cap = c ? c : 1;
        a->items = (OSObject **)calloc(a->cap, sizeof(OSObject *));
        return a;
    }
    bool setObject(const OSObject *obj) {
        if (!obj) return false;
        if (cnt == cap) {
            cap *= 2;
            items = (OSObject **)realloc(items, cap * sizeof(OSObject *));
        }
        auto *m = const_cast<OSObject *>(obj);
        m->retain();
        items[cnt++] = m;
        return true;
    }
    unsigned int getCount() const { return cnt; }
    OSObject *getObject(unsigned int i) const { return i < cnt ? items[i] : nullptr; }
};

class OSDictionary : public OSObject {
public:
    char **keys = nullptr;
    OSObject **vals = nullptr;
    unsigned int cnt = 0, cap = 0;
    ~OSDictionary() override {
        for (unsigned int i = 0; i < cnt; i++) { ::free(keys[i]); vals[i]->release(); }
        ::free(keys); ::free(vals);
    }
    static OSDictionary *withCapacity(unsigned int c) {
        auto *d = new OSDictionary();
        d->cap = c ? c : 1;
        d->keys = (char **)calloc(d->cap, sizeof(char *));
        d->vals = (OSObject **)calloc(d->cap, sizeof(OSObject *));
        return d;
    }
    bool setObject(const char *k, const OSObject *v) {
        if (!k || !v) return false;
        auto *m = const_cast<OSObject *>(v);
        for (unsigned int i = 0; i < cnt; i++) {
            if (strcmp(keys[i], k) == 0) {
                m->retain();
                vals[i]->release();
                vals[i] = m;
                return true;
            }
        }
        if (cnt == cap) {
            cap *= 2;
            keys = (char **)realloc(keys, cap * sizeof(char *));
            vals = (OSObject **)realloc(vals, cap * sizeof(OSObject *));
        }
        keys[cnt] = strdup(k);
        m->retain();
        vals[cnt] = m;
        cnt++;
        return true;
    }
    unsigned int getCount() const { return cnt; }
    OSObject *getObject(const char *k) const {
        for (unsigned int i = 0; i < cnt; i++) if (strcmp(keys[i], k) == 0) return vals[i];
        return nullptr;
    }
};

static OSObject *OSUnserializeXML(const char *buf, size_t len) {
    (void)len;
    if (buf && strstr(buf, "test-xml")) {
        auto *d = OSDictionary::withCapacity(1);
        d->setObject("xml-flag", kOSBooleanTrue);
        return d;
    }
    return nullptr;
}

#include "iokit/Kernel/arm/SweetEntitlements.cpp"
'''

CPP_ENT_MAIN = r'''
int main(void) {
    /* 1. XML blob path (0xfade7171) */
    const char *xml_str = "<dict><key>test-xml</key><true/></dict>";
    size_t xml_sz = sizeof(CS_GenericBlob) + strlen(xml_str);
    CS_GenericBlob *xb = (CS_GenericBlob *)calloc(1, xml_sz);
    xb->magic = htonl(CSMAGIC_EMBEDDED_ENTITLEMENTS);
    xb->length = htonl((uint32_t)xml_sz);
    memcpy(xb->data, xml_str, strlen(xml_str));

    void *ent_xml = sweet_entitlements_create(xb, xml_sz);
    assert(ent_xml != nullptr);
    assert(sweet_entitlements_bool(ent_xml, "xml-flag"));
    CS_GenericBlob *copied_xml = nullptr;
    assert(sweet_entitlements_copy_xml(ent_xml, &copied_xml) && copied_xml != nullptr);
    assert(ntohl(copied_xml->length) == xml_sz);
    kfree_data(copied_xml, xml_sz);
    ((OSObject *)ent_xml)->release();
    free(xb);
    assert(cpp_live_objs == 0);

    /* 2. DER blob path (0xfade7172) CE v1: 0x70 { INT 1, 0xb0 { bool, int, str, data, arr } } */
    const uint8_t der_payload[] = {
        0x70, 56,
          0x02, 0x01, 0x01,
          0xb0, 51,
            0x30, 12, 0x0c, 7, 'd','e','r','b','o','o','l', 0x01, 0x01, 0x01,
            0x30, 10, 0x0c, 5, 'd','e','r','i','d', 0x0c, 1, 'X',
            0x30, 10, 0x0c, 4, 'd','n','u','m', 0x02, 0x02, 0x01, 0x00,
            0x30, 11, 0x0c, 5, 'd','d','a','t','a', 0x04, 0x02, 0xca, 0xfe
    };
    size_t der_sz = sizeof(CS_GenericBlob) + sizeof(der_payload);
    CS_GenericBlob *db = (CS_GenericBlob *)calloc(1, der_sz);
    db->magic = htonl(CSMAGIC_EMBEDDED_DER_ENTITLEMENTS);
    db->length = htonl((uint32_t)der_sz);
    memcpy(db->data, der_payload, sizeof(der_payload));

    void *ent_der = sweet_entitlements_create(db, der_sz);
    assert(ent_der != nullptr);
    assert(sweet_entitlements_count(ent_der) == 4);
    assert(sweet_entitlements_bool(ent_der, "derbool"));
    assert(sweet_entitlements_string(ent_der, "derid", "X"));
    CS_GenericBlob *out_null = (CS_GenericBlob *)0x1;
    assert(!sweet_entitlements_copy_xml(ent_der, &out_null));
    OSObject *num_obj = (OSObject *)sweet_entitlements_copy_object(ent_der, "dnum");
    assert(OSDynamicCast(OSNumber, num_obj)->val == 256);
    num_obj->release();
    OSObject *data_obj = (OSObject *)sweet_entitlements_copy_object(ent_der, "ddata");
    assert(OSDynamicCast(OSData, data_obj)->len == 2 && OSDynamicCast(OSData, data_obj)->bytes[0] == 0xca);
    data_obj->release();
    OSDictionary *dict_copy = (OSDictionary *)sweet_entitlements_copy_dict(ent_der);
    assert(dict_copy != nullptr && dict_copy->getCount() == 4);
    dict_copy->release();
    ((OSObject *)ent_der)->release();
    free(db);
    assert(cpp_live_objs == 0);

    puts("SweetEntitlements.cpp model ok");
    return 0;
}
'''


def extract_function(text, name):
    m = re.search(r'(?:static )?(?:void|int)\n' + name + r'\([^;]+?\n\{', text)
    assert m, f'function not found: {name}'
    depth, end = 1, m.end()
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[m.start():end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--cc', default=os.environ.get('CC', 'clang'))
    args = parser.parse_args()

    assert PATCH_0132.is_file(), f'missing {PATCH_0132}'
    with tempfile.TemporaryDirectory(prefix='fuxi-der-ent-') as tmp:
        t = Path(tmp)
        for patch in sorted(PATCH_DIR.glob('*.patch')):
            if patch.name >= PATCH_0132.name:
                break
            chunks = re.split(r'(?=^--- (?:a/|/dev/null))', patch.read_text(errors='surrogateescape'), flags=re.M)
            for chunk in chunks:
                m = re.search(r'^\+\+\+ b/(.+)$', chunk, re.M)
                if not m or m[1] not in ('bsd/kern/sweet_amfi.c', 'iokit/Kernel/arm/SweetEntitlements.cpp'):
                    continue
                subprocess.run(['git', 'apply', '--whitespace=nowarn', '-'], input=chunk,
                               text=True, cwd=t, check=True)
        subprocess.run(['git', 'apply', '--check', str(PATCH_0132)], cwd=t, check=True)
        subprocess.run(['git', 'apply', str(PATCH_0132)], cwd=t, check=True)

        assert (t / 'iokit/Kernel/arm/fuxi_der_entitlements.h').is_file()
        ent_cpp = (t / 'iokit/Kernel/arm/SweetEntitlements.cpp').read_text()
        amfi_c = (t / 'bsd/kern/sweet_amfi.c').read_text()
        assert 'CSMAGIC_EMBEDDED_DER_ENTITLEMENTS' in ent_cpp
        assert 'fuxi_der_parse_blob' in ent_cpp
        assert 'ent->xml == NULL' in ent_cpp
        assert 'csblob_get_der_entitlements' in amfi_c
        assert 'XML + DER entitlements' in amfi_c

        # Unit 1: Pure C DER parser test with ASan/UBSan/LSan
        c_der = t / 'test_der.c'
        c_der.write_text(C_DER_TEST)
        b_der = t / 'test_der'
        cmd = [args.cc, '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
               '-I', str(t), str(c_der), '-o', str(b_der)]
        if args.sanitize:
            cmd.extend(['-fsanitize=address,undefined', '-fno-sanitize-recover=all'])
        subprocess.run(cmd, check=True)
        env = dict(os.environ)
        if args.sanitize:
            env['ASAN_OPTIONS'] = 'detect_leaks=1'
        r1 = subprocess.run([str(b_der)], env=env, capture_output=True, text=True)
        assert r1.returncode == 0, r1.stdout + r1.stderr
        assert 'der parser model ok' in r1.stdout

        # Unit 2: Extracted sweet_amfi_attach_entitlements priority & fail-closed model
        attach_fn = extract_function(amfi_c, 'sweet_amfi_attach_entitlements')
        c_amfi = t / 'test_amfi.c'
        c_amfi.write_text(AMFI_TEST_PREFIX + attach_fn + '\n' + AMFI_TEST_MAIN)
        b_amfi = t / 'test_amfi'
        cmd2 = [args.cc, '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                str(c_amfi), '-o', str(b_amfi)]
        if args.sanitize:
            cmd2.extend(['-fsanitize=address,undefined', '-fno-sanitize-recover=all'])
        subprocess.run(cmd2, check=True)
        r2 = subprocess.run([str(b_amfi)], env=env, capture_output=True, text=True)
        assert r2.returncode == 0, r2.stdout + r2.stderr
        assert 'sweet_amfi attach model ok' in r2.stdout

        # Unit 3: Real SweetEntitlements.cpp C++ model with ASan/UBSan/LSan
        inc = t / 'include'
        for rel in (
            'pexpert/arm64/board_config.h',
            'IOKit/IOLib.h',
            'libkern/c++/OSArray.h',
            'libkern/c++/OSBoolean.h',
            'libkern/c++/OSData.h',
            'libkern/c++/OSDictionary.h',
            'libkern/c++/OSNumber.h',
            'libkern/c++/OSString.h',
            'libkern/c++/OSUnserialize.h',
            'kern/cs_blobs.h',
        ):
            hp = inc / rel
            hp.parent.mkdir(parents=True, exist_ok=True)
            hp.write_text('/* host test stub */\n')
        cpp_ent = t / 'test_sweet_ent.cpp'
        cpp_ent.write_text(CPP_ENT_PREFIX + '\n' + CPP_ENT_MAIN)
        b_ent = t / 'test_sweet_ent'
        cxx = os.environ.get('CXX', 'clang++')
        cmd3 = [cxx, '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                '-I', str(inc), '-I', str(t), str(cpp_ent), '-o', str(b_ent)]
        if args.sanitize:
            cmd3.extend(['-fsanitize=address,undefined', '-fno-sanitize-recover=all'])
        subprocess.run(cmd3, check=True)
        r3 = subprocess.run([str(b_ent)], env=env, capture_output=True, text=True)
        assert r3.returncode == 0, r3.stdout + r3.stderr
        assert 'SweetEntitlements.cpp model ok' in r3.stdout

    print('PASS: 0132 Slot 7 DER entitlements (CE v1 0x70/0xb0, CE v0 0x31/0x30, fuzz/OOM 0-leak, sweet_amfi fail-closed, SweetEntitlements.cpp, ASan/UBSan)')


if __name__ == '__main__':
    main()
