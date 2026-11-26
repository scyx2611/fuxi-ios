#!/usr/bin/env python3
"""Host tests for SweetKeyStore.cpp + 0131-fuxi-aks-selectors.patch (ASan/UBSan)."""
import argparse
import hashlib
import hmac
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
PATCH_0038 = PATCH_DIR / '0038-sweet-applekeystore.patch'
PATCH_0043 = PATCH_DIR / '0043-sweet-console-crash-log.patch'
PATCH_0055 = PATCH_DIR / '0055-sweet-aks-ext-state-sampler-depth.patch'
PATCH_0131 = PATCH_DIR / '0131-fuxi-aks-selectors.patch'

HARNESS_HEADER = r'''
#include <assert.h>
#include <initializer_list>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SWEET 1
#define APPLE_KEXT_OVERRIDE override
#define __unused __attribute__((unused))

typedef uint32_t IOReturn;
enum : IOReturn {
    kIOReturnSuccess      = 0x00000000u,
    kIOReturnError        = 0xe00002bcu,
    kIOReturnBadArgument  = 0xe00002c2u,
    kIOReturnUnsupported  = 0xe00002c7u,
    kIOReturnNoSpace      = 0xe00002dbu,
    kIOReturnNotPermitted = 0xe00002e2u,
};

typedef int DTEntry;
#define kSuccess 0
#define kIOUserClientClassKey "IOUserClientClass"

static char last_iolog[512];
static void IOLog(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(last_iolog, sizeof(last_iolog), fmt, ap);
    va_end(ap);
}

static const char *test_proc_name = "SpringBoard";
static void proc_selfname(char *buf, int len) {
    snprintf(buf, (size_t)len, "%s", test_proc_name);
}

/* Mock DeviceTree (/chosen/unique-chip-id) */
static int dt_has_chosen = 1;
static int dt_has_chip_id = 1;
static uint8_t dt_chip_id[64];
static unsigned int dt_chip_id_len = 8;

extern "C" int SecureDTLookupEntry(const DTEntry *searchPoint, const char *pathName, DTEntry *foundEntry) {
    (void)searchPoint;
    if (dt_has_chosen && pathName && strcmp(pathName, "/chosen") == 0) {
        *foundEntry = 1;
        return kSuccess;
    }
    return -1;
}

extern "C" int SecureDTGetProperty(DTEntry entry, const char *propertyName, void const **propertyValue, unsigned int *propertySize) {
    if (entry == 1 && dt_has_chip_id && propertyName && strcmp(propertyName, "unique-chip-id") == 0) {
        *propertyValue = dt_chip_id;
        *propertySize = dt_chip_id_len;
        return kSuccess;
    }
    return -1;
}

/* Minimal IOKit C++ stubs */
class OSObject {
public:
    virtual ~OSObject() = default;
};

#define OSDeclareDefaultStructors(cls) \
    static void *_os_meta_##cls()
#define OSDefineMetaClassAndStructors(cls, sup) \
    void *cls::_os_meta_##cls() { return nullptr; }

class IOService : public OSObject {
public:
    virtual bool start(IOService *provider) { (void)provider; return true; }
    void setProperty(const char *k, const char *v) { (void)k; (void)v; }
    void registerService() {}
};

class IOMemoryDescriptor {
public:
    uint8_t *buf;
    uint64_t len;
    uint64_t getLength() const { return len; }
    uint64_t readBytes(uint64_t offset, void *bytes, uint64_t length) const {
        if (!buf || offset > len || length > len - offset) return 0;
        memcpy(bytes, buf + (size_t)offset, (size_t)length);
        return length;
    }
    uint64_t writeBytes(uint64_t offset, const void *bytes, uint64_t length) {
        if (!buf || offset > len || length > len - offset) return 0;
        memcpy(buf + (size_t)offset, bytes, (size_t)length);
        return length;
    }
};

struct IOExternalMethodDispatch {};
struct IOExternalMethodArguments {
    uint32_t version = 0;
    uint32_t selector = 0;
    const uint64_t *scalarInput = nullptr;
    uint32_t scalarInputCount = 0;
    const void *structureInput = nullptr;
    uint32_t structureInputSize = 0;
    IOMemoryDescriptor *structureInputDescriptor = nullptr;
    uint64_t *scalarOutput = nullptr;
    uint32_t scalarOutputCount = 0;
    void *structureOutput = nullptr;
    uint32_t structureOutputSize = 0;
    IOMemoryDescriptor *structureOutputDescriptor = nullptr;
    uint32_t structureOutputDescriptorSize = 0;
};

class IOUserClient : public IOService {
public:
    void terminate() {}
    virtual IOReturn clientClose(void) { return kIOReturnSuccess; }
    virtual IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args,
        IOExternalMethodDispatch *dispatch, OSObject *target, void *reference) {
        (void)selector; (void)args; (void)dispatch; (void)target; (void)reference;
        return kIOReturnUnsupported;
    }
};

/* Minimal RFC 6234 SHA-256 + RFC 2104 HMAC-SHA256 for corecrypto stubs */
#define CCSHA256_OUTPUT_SIZE 32
struct ccdigest_info {
    size_t output_size;
    size_t block_size;
};
static const struct ccdigest_info sha256_info = { 32, 64 };
extern "C" const struct ccdigest_info *ccsha256_di(void) { return &sha256_info; }

extern "C" void cc_clear(size_t len, void *dst) {
    volatile uint8_t *p = (volatile uint8_t *)dst;
    while (len--) *p++ = 0;
}

extern "C" int cc_cmp_safe(size_t num, const void *ptr1, const void *ptr2) {
    const uint8_t *a = (const uint8_t *)ptr1;
    const uint8_t *b = (const uint8_t *)ptr2;
    uint8_t diff = 0;
    for (size_t i = 0; i < num; i++) diff |= (uint8_t)(a[i] ^ b[i]);
    return diff != 0;
}

static inline uint32_t rotr32(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

static void sha256_compress(uint32_t state[8], const uint8_t block[64]) {
    static const uint32_t K[64] = {
        0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
        0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
        0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
        0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
        0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
        0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
        0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
        0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u,
    };
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i*4] << 24) | ((uint32_t)block[i*4+1] << 16) |
               ((uint32_t)block[i*4+2] << 8) | (uint32_t)block[i*4+3];
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr32(w[i-15], 7) ^ rotr32(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = rotr32(w[i-2], 17) ^ rotr32(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a=state[0], b=state[1], c=state[2], d=state[3];
    uint32_t e=state[4], f=state[5], g=state[6], h=state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + S1 + ch + K[i] + w[i];
        uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }
    state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d;
    state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
}

static void sha256_raw(const uint8_t *data, size_t len, uint8_t out[32]) {
    uint32_t state[8] = {
        0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
        0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u
    };
    uint8_t buf[64];
    size_t i = 0;
    while (i + 64 <= len) {
        sha256_compress(state, data + i);
        i += 64;
    }
    size_t rem = len - i;
    memset(buf, 0, sizeof(buf));
    if (rem) memcpy(buf, data + i, rem);
    buf[rem] = 0x80;
    if (rem >= 56) {
        sha256_compress(state, buf);
        memset(buf, 0, sizeof(buf));
    }
    uint64_t bits = (uint64_t)len * 8ULL;
    for (int j = 0; j < 8; j++) buf[56 + j] = (uint8_t)(bits >> (56 - j * 8));
    sha256_compress(state, buf);
    for (int j = 0; j < 8; j++) {
        out[j*4+0] = (uint8_t)(state[j] >> 24);
        out[j*4+1] = (uint8_t)(state[j] >> 16);
        out[j*4+2] = (uint8_t)(state[j] >> 8);
        out[j*4+3] = (uint8_t)(state[j]);
    }
}

extern "C" void cchmac(const struct ccdigest_info *di, size_t key_len, const void *key,
                       size_t data_len, const void *data, uint8_t *mac_out) {
    (void)di;
    uint8_t k0[64];
    memset(k0, 0, sizeof(k0));
    if (key_len > 64) {
        sha256_raw((const uint8_t *)key, key_len, k0);
    } else if (key_len > 0) {
        memcpy(k0, key, key_len);
    }
    uint8_t *inner = (uint8_t *)malloc(64 + data_len);
    assert(inner != nullptr);
    for (int i = 0; i < 64; i++) inner[i] = k0[i] ^ 0x36u;
    if (data_len > 0) memcpy(inner + 64, data, data_len);
    uint8_t ihash[32];
    sha256_raw(inner, 64 + data_len, ihash);
    free(inner);

    uint8_t outer[64 + 32];
    for (int i = 0; i < 64; i++) outer[i] = k0[i] ^ 0x5cu;
    memcpy(outer + 64, ihash, 32);
    sha256_raw(outer, sizeof(outer), mac_out);
}

#include "iokit/Kernel/arm/SweetKeyStore.cpp"
'''

HARNESS_TESTS_TEMPLATE = r'''
static void set_chip_id(const uint8_t *data, unsigned int len) {
    dt_has_chosen = 1;
    dt_has_chip_id = 1;
    dt_chip_id_len = len;
    if (len > 0 && len <= sizeof(dt_chip_id)) {
        memcpy(dt_chip_id, data, len);
    }
}

int main(void) {
    AppleKeyStore aks;
    IOService provider;
    assert(aks.start(&provider));
    AppleKeyStoreUserClient uc;

    /* 1. Verify constants match IOReturn.h */
    assert(kIOReturnError == 0xe00002bcu);
    assert(kIOReturnNoSpace == 0xe00002dbu);
    assert(kIOReturnBadArgument == 0xe00002c2u);
    assert(kIOReturnUnsupported == 0xe00002c7u);

    /* 2. Regressions: no-op selectors */
    const uint32_t noop_sels[] = {0, 4, 5, 9, 12, 13, 26, 27, 55, 101, 102};
    for (uint32_t sel : noop_sels) {
        IOExternalMethodArguments a;
        uint64_t out_s[1] = {99};
        a.scalarOutput = out_s;
        a.scalarOutputCount = 1;
        assert(uc.externalMethod(sel, &a, nullptr, nullptr, nullptr) == kIOReturnSuccess);
        assert(a.scalarOutputCount == 0);
    }

    /* 3. Regressions: sel 14, 7, 103 */
    {
        IOExternalMethodArguments a;
        uint64_t out_s[1] = {999};
        a.scalarOutput = out_s;
        a.scalarOutputCount = 1;
        assert(uc.externalMethod(14, &a, nullptr, nullptr, nullptr) == kIOReturnSuccess);
        assert(a.scalarOutputCount == 1 && out_s[0] == 0);

        out_s[0] = 0;
        a.scalarOutputCount = 1;
        assert(uc.externalMethod(7, &a, nullptr, nullptr, nullptr) == kIOReturnSuccess);
        assert(a.scalarOutputCount == 1 && out_s[0] == 0x4);

        out_s[0] = 99;
        a.scalarOutputCount = 1;
        assert(uc.externalMethod(103, &a, nullptr, nullptr, nullptr) == kIOReturnSuccess);
        assert(a.scalarOutputCount == 1 && out_s[0] == 0);
    }

    /* 4. Regressions: sel 17 & 35 device state (inband and descriptor) */
    for (uint32_t sel : {17u, 35u}) {
        uint8_t buf[128] = {0};
        IOExternalMethodArguments a;
        a.structureOutput = buf;
        a.structureOutputSize = sizeof(buf);
        assert(uc.externalMethod(sel, &a, nullptr, nullptr, nullptr) == kIOReturnSuccess);
        assert(buf[0] == 0x31 && a.structureOutputSize > 2);

        uint8_t dbuf[128] = {0};
        IOMemoryDescriptor md{dbuf, sizeof(dbuf)};
        IOExternalMethodArguments b;
        b.structureOutputDescriptor = &md;
        assert(uc.externalMethod(sel, &b, nullptr, nullptr, nullptr) == kIOReturnSuccess);
        assert(b.structureOutputDescriptorSize == a.structureOutputSize);
        assert(memcmp(buf, dbuf, a.structureOutputSize) == 0);
    }

    /* 5. Regressions: sel 10 & 11 wrap/unwrap round-trip + tamper rejection */
    {
        uint8_t key[32];
        for (int i = 0; i < 32; i++) key[i] = (uint8_t)(i + 1);
        uint8_t blob[40] = {0};
        uint64_t in_s[2] = {0, 3};
        uint64_t out_s[1] = {0};
        IOExternalMethodArguments w;
        w.scalarInput = in_s;
        w.scalarInputCount = 2;
        w.structureInput = key;
        w.structureInputSize = sizeof(key);
        w.scalarOutput = out_s;
        w.scalarOutputCount = 1;
        w.structureOutput = blob;
        w.structureOutputSize = sizeof(blob);
        assert(uc.externalMethod(10, &w, nullptr, nullptr, nullptr) == kIOReturnSuccess);
        assert(w.scalarOutputCount == 1 && out_s[0] == 3 && w.structureOutputSize == 40);

        uint8_t unwrapped[32] = {0};
        out_s[0] = 0;
        IOExternalMethodArguments u;
        u.scalarInput = in_s;
        u.scalarInputCount = 2;
        u.structureInput = blob;
        u.structureInputSize = sizeof(blob);
        u.scalarOutput = out_s;
        u.scalarOutputCount = 1;
        u.structureOutput = unwrapped;
        u.structureOutputSize = sizeof(unwrapped);
        assert(uc.externalMethod(11, &u, nullptr, nullptr, nullptr) == kIOReturnSuccess);
        assert(memcmp(key, unwrapped, 32) == 0);

        blob[0] ^= 1u;
        u.structureOutputSize = sizeof(unwrapped);
        assert(uc.externalMethod(11, &u, nullptr, nullptr, nullptr) == kIOReturnNotPermitted);

        /* scalarOutput == NULL safety on sel 10 */
        IOExternalMethodArguments w_null_out;
        w_null_out.scalarInput = in_s;
        w_null_out.scalarInputCount = 2;
        w_null_out.structureInput = key;
        w_null_out.structureInputSize = sizeof(key);
        w_null_out.scalarOutput = nullptr;
        w_null_out.scalarOutputCount = 1;
        w_null_out.structureOutput = blob;
        w_null_out.structureOutputSize = sizeof(blob);
        assert(uc.externalMethod(10, &w_null_out, nullptr, nullptr, nullptr) == kIOReturnSuccess);
        assert(w_null_out.scalarOutputCount == 0);
    }

    /* 6. sel 8 (kAKSLockDevice) & sel 32 (kAKSLockDeviceForce) */
    test_proc_name = "SpringBoard";
    for (uint32_t sel : {8u, 32u}) {
        uint64_t in_s[2] = {0, 0};
        uint64_t out_s[1] = {0};
        uint8_t dummy[8] = {0};
        IOExternalMethodArguments ok;
        ok.scalarInput = in_s;
        ok.scalarInputCount = 1;
        assert(uc.externalMethod(sel, &ok, nullptr, nullptr, nullptr) == kIOReturnSuccess);
        assert(ok.scalarOutputCount == 0);

        /* Lock state remains 0x4 (unlocked) */
        IOExternalMethodArguments ls;
        ls.scalarOutput = out_s;
        ls.scalarOutputCount = 1;
        assert(uc.externalMethod(7, &ls, nullptr, nullptr, nullptr) == kIOReturnSuccess);
        assert(out_s[0] == 0x4);

        /* Bad argument cases */
        IOExternalMethodArguments bad0;
        bad0.scalarInputCount = 0;
        assert(uc.externalMethod(sel, &bad0, nullptr, nullptr, nullptr) == kIOReturnBadArgument);

        IOExternalMethodArguments bad2;
        bad2.scalarInput = in_s;
        bad2.scalarInputCount = 2;
        assert(uc.externalMethod(sel, &bad2, nullptr, nullptr, nullptr) == kIOReturnBadArgument);

        IOExternalMethodArguments bad_out;
        bad_out.scalarInput = in_s;
        bad_out.scalarInputCount = 1;
        bad_out.scalarOutput = out_s;
        bad_out.scalarOutputCount = 1;
        assert(uc.externalMethod(sel, &bad_out, nullptr, nullptr, nullptr) == kIOReturnBadArgument);

        IOExternalMethodArguments bad_in_st;
        bad_in_st.scalarInput = in_s;
        bad_in_st.scalarInputCount = 1;
        bad_in_st.structureInput = dummy;
        bad_in_st.structureInputSize = 4;
        assert(uc.externalMethod(sel, &bad_in_st, nullptr, nullptr, nullptr) == kIOReturnBadArgument);

        IOExternalMethodArguments bad_out_st;
        bad_out_st.scalarInput = in_s;
        bad_out_st.scalarInputCount = 1;
        bad_out_st.structureOutput = dummy;
        bad_out_st.structureOutputSize = 4;
        assert(uc.externalMethod(sel, &bad_out_st, nullptr, nullptr, nullptr) == kIOReturnBadArgument);
    }

    /* 7. sel 23 (kAKSBagUUID) */
    {
        const uint8_t chip1[8] = {0x01, 0x00, 0x00, 0x00, 0x49, 0x58, 0x55, 0x46}; /* 0x4655584900000001 LE */
        const uint8_t expected1[16] = { EXPECTED_UUID_1 };
        set_chip_id(chip1, 8);

        uint64_t in_s[1] = {0};
        uint8_t out_uuid[16] = {0};
        uint8_t out_uuid2[16] = {0};
        IOExternalMethodArguments a;
        a.scalarInput = in_s;
        a.scalarInputCount = 1;
        a.structureOutput = out_uuid;
        a.structureOutputSize = sizeof(out_uuid);
        assert(uc.externalMethod(23, &a, nullptr, nullptr, nullptr) == kIOReturnSuccess);
        assert(a.scalarOutputCount == 0 && a.structureOutputSize == 16);
        assert(memcmp(out_uuid, expected1, 16) == 0);

        /* Deterministic across calls */
        a.structureOutput = out_uuid2;
        a.structureOutputSize = sizeof(out_uuid2);
        assert(uc.externalMethod(23, &a, nullptr, nullptr, nullptr) == kIOReturnSuccess);
        assert(memcmp(out_uuid, out_uuid2, 16) == 0);

        /* IOMemoryDescriptor output path */
        uint8_t desc_uuid[16] = {0};
        IOMemoryDescriptor md{desc_uuid, sizeof(desc_uuid)};
        IOExternalMethodArguments d;
        d.scalarInput = in_s;
        d.scalarInputCount = 1;
        d.structureOutputDescriptor = &md;
        assert(uc.externalMethod(23, &d, nullptr, nullptr, nullptr) == kIOReturnSuccess);
        assert(d.structureOutputDescriptorSize == 16);
        assert(memcmp(desc_uuid, expected1, 16) == 0);

        /* Different chip id produces different UUID */
        const uint8_t chip2[8] = {0x02, 0x00, 0x00, 0x00, 0x49, 0x58, 0x55, 0x46};
        const uint8_t expected2[16] = { EXPECTED_UUID_2 };
        set_chip_id(chip2, 8);
        a.structureOutput = out_uuid2;
        a.structureOutputSize = sizeof(out_uuid2);
        assert(uc.externalMethod(23, &a, nullptr, nullptr, nullptr) == kIOReturnSuccess);
        assert(memcmp(out_uuid2, expected2, 16) == 0);
        assert(memcmp(out_uuid, out_uuid2, 16) != 0);

        /* >32 bytes truncated safely to 32 bytes */
        uint8_t chip40[40];
        for (int i = 0; i < 40; i++) chip40[i] = (uint8_t)(0xa0 + i);
        const uint8_t expected40[16] = { EXPECTED_UUID_40 };
        set_chip_id(chip40, 40);
        a.structureOutput = out_uuid2;
        a.structureOutputSize = sizeof(out_uuid2);
        assert(uc.externalMethod(23, &a, nullptr, nullptr, nullptr) == kIOReturnSuccess);
        assert(memcmp(out_uuid2, expected40, 16) == 0);

        /* Missing /chosen, missing unique-chip-id, or 0 bytes -> kIOReturnError */
        dt_has_chosen = 0;
        a.structureOutputSize = 16;
        assert(uc.externalMethod(23, &a, nullptr, nullptr, nullptr) == kIOReturnError);

        dt_has_chosen = 1;
        dt_has_chip_id = 0;
        a.structureOutputSize = 16;
        assert(uc.externalMethod(23, &a, nullptr, nullptr, nullptr) == kIOReturnError);

        dt_has_chip_id = 1;
        dt_chip_id_len = 0;
        a.structureOutputSize = 16;
        assert(uc.externalMethod(23, &a, nullptr, nullptr, nullptr) == kIOReturnError);

        /* Output buffer too small / NULL -> kIOReturnNoSpace */
        set_chip_id(chip1, 8);
        a.structureOutput = nullptr;
        a.structureOutputSize = 16;
        assert(uc.externalMethod(23, &a, nullptr, nullptr, nullptr) == kIOReturnNoSpace);

        a.structureOutput = out_uuid;
        a.structureOutputSize = 15;
        assert(uc.externalMethod(23, &a, nullptr, nullptr, nullptr) == kIOReturnNoSpace);

        IOMemoryDescriptor md_small{desc_uuid, 15};
        IOExternalMethodArguments d_small;
        d_small.scalarInput = in_s;
        d_small.scalarInputCount = 1;
        d_small.structureOutputDescriptor = &md_small;
        assert(uc.externalMethod(23, &d_small, nullptr, nullptr, nullptr) == kIOReturnNoSpace);

        /* Invalid argument shapes -> kIOReturnBadArgument */
        IOExternalMethodArguments b23;
        b23.scalarInput = in_s;
        b23.scalarInputCount = 0;
        b23.structureOutput = out_uuid;
        b23.structureOutputSize = 16;
        assert(uc.externalMethod(23, &b23, nullptr, nullptr, nullptr) == kIOReturnBadArgument);

        uint64_t dummy_out = 0;
        b23.scalarInputCount = 1;
        b23.scalarOutput = &dummy_out;
        b23.scalarOutputCount = 1;
        assert(uc.externalMethod(23, &b23, nullptr, nullptr, nullptr) == kIOReturnBadArgument);

        uint8_t dummy_in[4] = {0};
        b23.scalarOutput = nullptr;
        b23.scalarOutputCount = 0;
        b23.structureInput = dummy_in;
        b23.structureInputSize = 4;
        assert(uc.externalMethod(23, &b23, nullptr, nullptr, nullptr) == kIOReturnBadArgument);
    }

    /* 8. sel 40 (kAKSGeneration) */
    {
        uint64_t in_s[3] = {0, 1, 2};
        uint64_t out_s[2] = {123, 456};
        uint8_t dummy[4] = {0};

        IOExternalMethodArguments a;
        a.scalarInput = in_s;
        a.scalarInputCount = 2;
        a.scalarOutput = out_s;
        a.scalarOutputCount = 1;
        assert(uc.externalMethod(40, &a, nullptr, nullptr, nullptr) == kIOReturnSuccess);
        assert(a.scalarOutputCount == 1 && out_s[0] == 0);

        IOExternalMethodArguments bad;
        bad = a; bad.scalarInputCount = 1;
        assert(uc.externalMethod(40, &bad, nullptr, nullptr, nullptr) == kIOReturnBadArgument);
        bad = a; bad.scalarInputCount = 3;
        assert(uc.externalMethod(40, &bad, nullptr, nullptr, nullptr) == kIOReturnBadArgument);
        bad = a; bad.scalarOutputCount = 0;
        assert(uc.externalMethod(40, &bad, nullptr, nullptr, nullptr) == kIOReturnBadArgument);
        bad = a; bad.scalarOutputCount = 2;
        assert(uc.externalMethod(40, &bad, nullptr, nullptr, nullptr) == kIOReturnBadArgument);
        bad = a; bad.scalarOutput = nullptr;
        assert(uc.externalMethod(40, &bad, nullptr, nullptr, nullptr) == kIOReturnBadArgument);
        bad = a; bad.structureInput = dummy; bad.structureInputSize = 4;
        assert(uc.externalMethod(40, &bad, nullptr, nullptr, nullptr) == kIOReturnBadArgument);
        bad = a; bad.structureOutput = dummy; bad.structureOutputSize = 4;
        assert(uc.externalMethod(40, &bad, nullptr, nullptr, nullptr) == kIOReturnBadArgument);
    }

    /* 9. sel 160 (kAKSEnableCacheFlow) */
    {
        uint64_t in_s[2] = {0, 1};
        uint64_t out_s[1] = {0};
        uint8_t dummy[4] = {0};

        IOExternalMethodArguments a0;
        assert(uc.externalMethod(160, &a0, nullptr, nullptr, nullptr) == kIOReturnSuccess);
        assert(a0.scalarOutputCount == 0);

        IOExternalMethodArguments a1;
        a1.scalarInput = in_s;
        a1.scalarInputCount = 1;
        assert(uc.externalMethod(160, &a1, nullptr, nullptr, nullptr) == kIOReturnSuccess);
        assert(a1.scalarOutputCount == 0);

        IOExternalMethodArguments bad;
        bad.scalarInput = in_s; bad.scalarInputCount = 2;
        assert(uc.externalMethod(160, &bad, nullptr, nullptr, nullptr) == kIOReturnBadArgument);
        bad = a1; bad.scalarOutput = out_s; bad.scalarOutputCount = 1;
        assert(uc.externalMethod(160, &bad, nullptr, nullptr, nullptr) == kIOReturnBadArgument);
        bad = a1; bad.structureInput = dummy; bad.structureInputSize = 4;
        assert(uc.externalMethod(160, &bad, nullptr, nullptr, nullptr) == kIOReturnBadArgument);
        bad = a1; bad.structureOutput = dummy; bad.structureOutputSize = 4;
        assert(uc.externalMethod(160, &bad, nullptr, nullptr, nullptr) == kIOReturnBadArgument);
    }

    /* 10. Unsupported selectors (including 31 and 57) & NULL pointer defenses */
    for (uint32_t sel : {31u, 57u, 43u, 80u, 81u, 104u, 9999u}) {
        IOExternalMethodArguments a;
        assert(uc.externalMethod(sel, &a, nullptr, nullptr, nullptr) == kIOReturnUnsupported);
    }
    assert(uc.externalMethod(0, nullptr, nullptr, nullptr, nullptr) == kIOReturnBadArgument);
    for (uint32_t sel : {7u, 14u, 103u}) {
        IOExternalMethodArguments a;
        a.scalarOutput = nullptr;
        a.scalarOutputCount = 1;
        assert(uc.externalMethod(sel, &a, nullptr, nullptr, nullptr) == kIOReturnBadArgument);
    }

    puts("aks selectors model ok");
    return 0;
}
'''


def hmac_uuid_bytes(chip_bytes: bytes) -> str:
    digest = hmac.new(b'SWEET AKS fuxi bag uuid, v1', chip_bytes[:32], hashlib.sha256).digest()[:16]
    return ', '.join(f'0x{b:02x}' for b in digest)


def extract_and_apply_sweetkeystore(dst_dir: Path):
    for patch in (PATCH_0038, PATCH_0043, PATCH_0055, PATCH_0131):
        assert patch.is_file(), f'missing {patch}'
        chunks = re.split(r'(?=^--- (?:a/|/dev/null))', patch.read_text(errors='surrogateescape'), flags=re.M)
        for chunk in chunks:
            m = re.search(r'^\+\+\+ b/(.+)$', chunk, re.M)
            if not m or m[1] != 'iokit/Kernel/arm/SweetKeyStore.cpp':
                continue
            subprocess.run(['git', 'apply', '--whitespace=nowarn', '-'], input=chunk,
                           text=True, cwd=dst_dir, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'clang++'))
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix='fuxi-aks-test-') as tmp:
        tmp_path = Path(tmp)
        extract_and_apply_sweetkeystore(tmp_path)

        inc = tmp_path / 'include'
        for rel in (
            'pexpert/arm64/board_config.h',
            'pexpert/device_tree.h',
            'IOKit/IOService.h',
            'IOKit/IOUserClient.h',
            'IOKit/IOKitKeys.h',
            'IOKit/IOLib.h',
            'IOKit/IOMemoryDescriptor.h',
            'libkern/c++/OSString.h',
            'sys/proc.h',
            'corecrypto/cc.h',
            'corecrypto/cchmac.h',
            'corecrypto/ccsha2.h',
        ):
            hp = inc / rel
            hp.parent.mkdir(parents=True, exist_ok=True)
            hp.write_text('/* host test stub */\n')

        aks_cpp = (tmp_path / 'iokit/Kernel/arm/SweetKeyStore.cpp').read_text()
        assert '#include <pexpert/device_tree.h>' in aks_cpp
        assert 'kAKSLockDevice' in aks_cpp and 'kAKSBagUUID' in aks_cpp
        assert 'kAKSLockDeviceForce' in aks_cpp and 'kAKSGeneration' in aks_cpp
        assert 'kAKSEnableCacheFlow' in aks_cpp

        tests = (HARNESS_TESTS_TEMPLATE
                 .replace('EXPECTED_UUID_1', hmac_uuid_bytes(bytes([0x01, 0x00, 0x00, 0x00, 0x49, 0x58, 0x55, 0x46])))
                 .replace('EXPECTED_UUID_2', hmac_uuid_bytes(bytes([0x02, 0x00, 0x00, 0x00, 0x49, 0x58, 0x55, 0x46])))
                 .replace('EXPECTED_UUID_40', hmac_uuid_bytes(bytes(0xa0 + i for i in range(40)))))

        src = tmp_path / 'test_aks.cpp'
        src.write_text(HARNESS_HEADER + tests)
        binary = tmp_path / 'test_aks'
        cmd = [args.cxx, '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
               '-I', str(inc), '-I', str(tmp_path), str(src), '-o', str(binary)]
        if args.sanitize:
            cmd.extend(['-fsanitize=address,undefined', '-fno-sanitize-recover=all'])
        subprocess.run(cmd, check=True)
        run = subprocess.run([str(binary)], capture_output=True, text=True)
        assert run.returncode == 0, run.stdout + run.stderr
        assert 'aks selectors model ok' in run.stdout
    print('PASS: 0131 SweetKeyStore selectors (sel 8, 32, 23, 40, 160, unsupported 31/57, NULL checks, ASan/UBSan)')


if __name__ == '__main__':
    main()
