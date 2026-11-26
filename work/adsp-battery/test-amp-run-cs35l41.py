"""Run fuxi_amp.c mode 6 (muted CS35L41 B2 protection run) against a simulated amplifier.

The model keeps a register file, the test key, the HALO core and the CSPL mailbox. It checks that the B2 errata and OTP trims
are written only under the test key and before any DSP memory, that the trims equal Linux v6.12 cs35l41_otp_unpack with
otp_map_1 (parsed from ref-linux/), that the core starts only after the rates, MPU and MAX_LRCLK_DELAY, that no output,
boost, clock or volume register is ever written, and that every bus failure leaves the key locked and the core stopped.
"""
import argparse
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / "ksrc/xnu/fuxi-adsp/fuxi_amp.c"
LINUX_LIB = ROOT / "ref-linux/cs35l41-lib.c"
LINUX_H = ROOT / "ref-linux/cs35l41.h"


def linux_otp_map():
    defs = {m.group(1): int(m.group(2), 16)
            for m in re.finditer(r"#define\s+(CS35L41_\w+)\s+0x([0-9A-Fa-f]+)", LINUX_H.read_text(encoding="utf-8"))}
    text = LINUX_LIB.read_text(encoding="utf-8")
    block = text[text.index("otp_map_1[] = {"):]
    block = block[:block.index("};")]
    entries = []
    for m in re.finditer(r"\{\s*(\w+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}", block):
        name = m.group(1)
        reg = defs[name] if name in defs else int(name, 0)
        entries.append((reg, int(m.group(2)), int(m.group(3))))
    assert len(entries) == 99, len(entries)
    return ",".join("{%#x,%d,%d}" % e for e in entries)


MODEL = r"""
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { kSuccess = 0 };
typedef unsigned DTEntry;
static const uint8_t kAddr[] = {0x40, 0x42};
static struct { uint8_t address; unsigned error; } gBus;
static const struct { uint32_t reg; unsigned shift, size; } linux_map[99] = {@LINUX_MAP@};

#define SLOTS 32768u
static uint32_t keys[SLOTS], vals[SLOTS];
static uint8_t used[SLOTS];
static unsigned slot(uint32_t reg)
{
    unsigned h = (uint32_t)(reg * 2654435761u) >> 17;
    while (used[h] && keys[h] != reg) h = (h + 1) & (SLOTS - 1);
    return h;
}
static uint32_t load(uint32_t reg) { unsigned h = slot(reg); return used[h] ? vals[h] : 0; }
static void store(uint32_t reg, uint32_t v) { unsigned h = slot(reg); used[h] = 1; keys[h] = reg; vals[h] = v; }

static uint8_t plan[29932], cal[4216];
static uint32_t otp_words[32], otp_id;
static unsigned mode, ops, fail_at, fail_stopping, stopping, writes_total, dsp_writes;
static uint32_t failed_reg;
static unsigned key_step, unlocked, b2_writes, mpu_step, mpu_count, mpu_done, rates_mask;
static unsigned mbox_cmds, sim_ms, core_en, core_start_ms, run_delay, never_run, bad_mbox, no_ack, pause_due, soft_reset, ever_started;
static uint32_t mbox_sts;
static unsigned b2_applied, verified, tuned, confirmed, stopped_msg, refused_run, tone_gate_refused;

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static int step(uint32_t reg)
{
    if (++ops != fail_at) return 1;
    failed_reg = reg;
    fail_stopping = stopping;
    return 0;
}
static void IOSleep(unsigned ms) { sim_ms += ms; }
static int ampTransferDone(void) { return 1; }

static int is_trim(uint32_t reg)
{
    for (unsigned i = 0; i < 99; i++) if (linux_map[i].reg && linux_map[i].reg == reg) return 1;
    return 0;
}
static int is_errata(uint32_t reg)
{
    static const uint32_t r[] = {0x4100, 0x4310, 0x4400, 0x381c, 0x02bc20e0, 0x02bc2020, 0x6c04, 0x4c28, 0x4c2c};
    for (unsigned i = 0; i < sizeof(r) / sizeof(r[0]); i++) if (r[i] == reg) return 1;
    return 0;
}
static uint32_t halo_state(void)
{
    if (!core_en) return 0;
    return !never_run && sim_ms - core_start_ms >= run_delay ? 2 : 1;
}
static void mailbox_tick(void)
{
    if (pause_due && sim_ms >= pause_due) {
        mbox_sts = 1;
        store(FM_IRQ1_STATUS2, load(FM_IRQ1_STATUS2) | FM_MBOX_FW);
        pause_due = 0;
    }
}
static int ampRead(uint8_t address, uint32_t reg, uint32_t *out)
{
    assert(address == 0x40);
    if (reg == FM_HALO_WDT) stopping = 1;
    if (!step(reg)) return 0;
    mailbox_tick();
    if (reg == FM_HALO_STATE) *out = halo_state();
    else if (reg == FM_HALO_HEARTBEAT) *out = halo_state() == 2 ? sim_ms - core_start_ms - run_delay + 7 : 0;
    else if (reg == FM_MBOX_STS) *out = mbox_sts;
    else if (reg == FM_OTPID) *out = otp_id;
    else if (reg >= FM_OTP_MEM0 && reg < FM_OTP_MEM0 + 128) *out = otp_words[(reg - FM_OTP_MEM0) / 4];
    else *out = load(reg);
    return 1;
}
static void reg_write(uint32_t reg, uint32_t v)
{
    static const uint32_t forbidden[] = {FM_PWR_CTRL1, 0x6000, 0x4800, FM_PLL_CLK_CTRL, 0x2c0c, 0x2084, 0x3800, 0x3808,
        0x3810, 0x3814, 0x4c00, FM_DSP_CLK_CTRL};
    for (unsigned i = 0; i < sizeof(forbidden) / sizeof(forbidden[0]); i++) assert(reg != forbidden[i]);
    ++writes_total;
    if (reg == FM_PWR_CTRL2) assert(v == 0);
    if (reg == FM_TEST_KEY) {
        if (v == 0x55) key_step = 1;
        else if (v == 0xaa && key_step == 1) { unlocked = 1; key_step = 0; }
        else if (v == 0xcc) key_step = 2;
        else if (v == 0x33 && key_step == 2) { unlocked = 0; key_step = 0; }
        else assert(0);
        return;
    }
    if (is_errata(reg) || is_trim(reg) || (reg == FM_PWR_CTRL2 && mode >= 6)) {
        assert(unlocked && !dsp_writes && !core_en);
        ++b2_writes;
    }
    if (reg == FM_CCM_CORE_CTRL) {
        assert(!(v & ~(FM_HALO_CORE_EN | FM_HALO_CORE_RESET)));
        if ((v & FM_HALO_CORE_EN) && !(v & FM_HALO_CORE_RESET)) {
            assert(mpu_done && rates_mask == 0xffff && load(FM_MAX_LRCLK_DELAY) == 0x20 && !core_en);
            assert(!unlocked && load(FM_PWR_CTRL2) == 0);
            core_en = 1;
            ever_started = 1;
            core_start_ms = sim_ms;
            mbox_sts = bad_mbox ? 3 : 0; /* 3: not RUNNING or PAUSED */
        } else if (!(v & FM_HALO_CORE_EN)) {
            core_en = 0;
        }
    }
    if (reg == FM_IRQ1_STATUS2 || reg == FM_IRQ2_STATUS2) { store(reg, load(reg) & ~v); return; }
    if (reg == FM_MBOX_CMD) {
        assert(v == 1 && halo_state() == 2);
        ++mbox_cmds;
        if (!(load(FM_IRQ2_MASK2) & FM_MBOX_DRV) && !no_ack) pause_due = sim_ms + 1;
        return;
    }
    if (reg == FM_HALO_MPU_LOCK) {
        if (v == 0x5555) mpu_step = 1;
        else if (v == 0xaaaa && mpu_step == 1) mpu_step = 2;
        else if (v == 0) { assert(mpu_step == 2 && mpu_count == 20); mpu_done = 1; mpu_step = 0; }
        else assert(0);
    }
    if (reg >= FM_HALO_MPU && reg < FM_HALO_MPU + 0x60) { assert(mpu_step == 2 && v == 0xffffffffu); ++mpu_count; }
    for (unsigned i = 0; i < 16; i++) {
        if (reg == (i < 8 ? FM_HALO_RX1_RATE : FM_HALO_TX1_RATE) + 8u * (i % 8u)) { assert(v == 1); rates_mask |= 1u << i; }
    }
    if (reg == FM_HALO_SOFT_RESET && (v & 1)) soft_reset = 1;
    store(reg, v);
}
static int geni_i2c_start_write_bytes(void *bus, const uint8_t *p, unsigned n)
{
    assert(bus == &gBus && gBus.address == 0x40 && n == 8);
    if (!step(be32(p))) return -1;
    reg_write(be32(p), be32(p + 4));
    return 0;
}
static int geni_i2c_start_write_buffer(void *bus, const uint8_t *p, unsigned length)
{
    const uint32_t reg = be32(p);
    assert(bus == &gBus && gBus.address == 0x40 && length >= 8 && !((length - 4) % 4));
    assert(reg >= 0x02000000 && reg < 0x04000000 && !core_en && !unlocked);
    if (mode >= 6) assert(b2_writes == 9 + 98 + 1);
    if (!step(reg)) return -1;
    ++dsp_writes;
    for (unsigned k = 0; k < (length - 4) / 4; k++) store(reg + 4 * k, be32(p + 4 + 4 * k));
    return 0;
}
static int geni_i2c_start_read_buffer(void *bus, const uint8_t *p, unsigned addr_len, uint8_t *out, unsigned n)
{
    const uint32_t reg = be32(p);
    assert(bus == &gBus && addr_len == 4 && !(n % 4));
    if (!step(reg)) return -1;
    for (unsigned k = 0; k < n / 4; k++) {
        const uint32_t v = load(reg + 4 * k);
        out[4 * k] = (uint8_t)(v >> 24); out[4 * k + 1] = (uint8_t)(v >> 16);
        out[4 * k + 2] = (uint8_t)(v >> 8); out[4 * k + 3] = (uint8_t)v;
    }
    return 0;
}
static int SecureDTLookupEntry(void *parent, const char *name, DTEntry *entry)
{
    assert(!parent && !strcmp(name, "/chosen/fuxi-speaker")); *entry = 1; return 0;
}
static int SecureDTGetProperty(DTEntry entry, const char *key, const void **p, unsigned *length)
{
    assert(entry == 1);
    if (!strcmp(key, "fuxi,cs35l41-cal")) { *p = cal; *length = sizeof(cal); return 0; }
    assert(!strcmp(key, "fuxi,cs35l41-prot")); *p = plan; *length = sizeof(plan); return 0;
}
static int record(const char *fmt, ...)
{
    char text[400]; va_list ap;
    va_start(ap, fmt); vsnprintf(text, sizeof(text), fmt, ap); va_end(ap);
    if (strstr(text, "B2 errata applied otpid=1 trims=98 test key locked")) b2_applied++;
    if (strstr(text, "full muted HALO verified bytes=29876 sections=6")) verified++;
    if (strstr(text, "muted bottom tuning verified bytes=4204")) tuned++;
    if (strstr(text, "protection running confirmed halo_state=2")) confirmed++;
    if (strstr(text, "protection core stopped")) stopped_msg++;
    if (strstr(text, "protection run refused")) refused_run++;
    if (strstr(text, "tone refused cspl_state=")) tone_gate_refused++;
    return 0;
}
#define printf record
"""

TEST = r"""
#undef printf
#define GENMASK(h, l) ((~0u >> (31u - (h))) & (~0u << (l)))
static uint32_t shadow_reg[128], shadow_val[128];
static unsigned shadow_n;
static uint32_t *shadow(uint32_t reg)
{
    for (unsigned i = 0; i < shadow_n; i++) if (shadow_reg[i] == reg) return &shadow_val[i];
    assert(shadow_n < 128);
    shadow_reg[shadow_n] = reg; shadow_val[shadow_n] = load(reg);
    return &shadow_val[shadow_n++];
}
/* Linux v6.12 cs35l41_otp_unpack, word by word with GENMASK, independent of the kernel's bit loop. */
static void linux_unpack(void)
{
    int bit_offset = 16, word_offset = 2;
    shadow_n = 0;
    for (unsigned i = 0; i < 99; i++) {
        const int size = (int)linux_map[i].size;
        uint32_t val;
        if (bit_offset + size - 1 >= 32) {
            val = (otp_words[word_offset] & GENMASK(31u, (unsigned)bit_offset)) >> bit_offset;
            val |= (otp_words[++word_offset] & GENMASK((unsigned)(bit_offset + size - 33), 0u)) << (32 - bit_offset);
            bit_offset += size - 32;
        } else {
            val = (otp_words[word_offset] & GENMASK((unsigned)(bit_offset + size - 1), (unsigned)bit_offset)) >> bit_offset;
            bit_offset += size;
        }
        if (bit_offset == 32) { bit_offset = 0; word_offset++; }
        if (linux_map[i].reg) {
            const uint32_t mask = GENMASK(linux_map[i].shift + linux_map[i].size - 1, linux_map[i].shift);
            uint32_t *v = shadow(linux_map[i].reg);
            *v = (*v & ~mask) | ((val << linux_map[i].shift) & mask);
        }
    }
}
static void fixture(unsigned m)
{
    static const uint32_t directory[] = {0x40000, 0x30000, 2, 0x400a4, 0x2b01, 0x1f4, 0xe, 0, 0, 2,
        0xcd, 0x1d3f01, 0x94, 0x50, 0xe, 0x7bc, 0xf20b, 0x100, 0x176, 0x4c, 0, 0, 0xbedead};
    static const uint32_t regions[6][2] = {{0x02000000, 72}, {0x0200009c, 528}, {0x020003cc, 1596},
        {0x02000a14, 1260}, {0x02c00000, 6000}, {0x03800000, 20420}};
    memset(used, 0, sizeof(used));
    mode = m; ops = fail_at = fail_stopping = stopping = writes_total = dsp_writes = 0; failed_reg = 0;
    key_step = unlocked = b2_writes = mpu_step = mpu_count = mpu_done = rates_mask = 0;
    mbox_cmds = 0; sim_ms = 1000; core_en = core_start_ms = run_delay = never_run = bad_mbox = no_ack = pause_due = soft_reset = 0;
    ever_started = 0; mbox_sts = 0xffffffffu; otp_id = 1;
    b2_applied = verified = tuned = confirmed = stopped_msg = refused_run = tone_gate_refused = 0;
    const uint32_t head[2] = {0x32505343, 6};
    memcpy(plan, head, 8);
    unsigned off = 8;
    for (unsigned i = 0; i < 6; i++) {
        memcpy(plan + off, regions[i], 8); off += 8;
        for (unsigned j = 0; j < regions[i][1]; j++) plan[off + j] = (uint8_t)(3 * j + 7 * i + 1);
        off += regions[i][1];
    }
    assert(off == sizeof(plan));
    memcpy(cal, "CSB1", 4);
    memset(cal + 4, 0, 8); cal[6] = 0x21; cal[7] = 0x65; cal[10] = 0x21; cal[11] = 0x66;
    for (unsigned i = 12; i < sizeof(cal); i++) cal[i] = (uint8_t)(i * 5);
    uint32_t seed = 0x13579bdfu;
    for (unsigned i = 0; i < 32; i++) { seed = seed * 1103515245u + 12345u; otp_words[i] = seed ^ (seed >> 13); }
    for (unsigned i = 0; i < 99; i++) if (linux_map[i].reg) store(linux_map[i].reg, 0xa5a5a5a5u ^ linux_map[i].reg);
    for (unsigned i = 0; i < 23; i++) store(0x02800000u + 4 * i, directory[i]);
    store(FM_PWR_CTRL1, 0); store(FM_PWR_CTRL2, 0x3320); store(FM_CCM_CORE_CTRL, FM_HALO_CORE_RESET);
    store(FM_IRQ2_MASK2, 0xffffffffu); store(FM_PLL_CLK_CTRL, 0x10); store(FM_DSP_CLK_CTRL, 3);
    store(0x4100, 1); store(0x381c, 0x50); store(FM_HALO_WDT, 1);
    store(FM_CSPL_STATE, 0x10); store(FM_CSPL_TEMPERATURE, 0x1900);
}
static void check_trims(void)
{
    for (unsigned i = 0; i < shadow_n; i++) assert(load(shadow_reg[i]) == shadow_val[i]);
}
int main(void)
{
    /* the whole run */
    fixture(6); linux_unpack(); ampLoadMutedProtection(0x40, 6);
    assert(b2_applied == 1 && verified == 1 && tuned == 1 && confirmed == 1 && stopped_msg == 1 && !refused_run);
    check_trims();
    assert(!unlocked && !core_en && soft_reset && !(load(FM_HALO_WDT) & 1) && mbox_sts == 1);
    assert(load(FM_IRQ2_MASK2) == 0xffffffffu && !(load(FM_IRQ1_STATUS2) & FM_MBOX_FW));
    assert(load(FM_PWR_CTRL2) == 0 && load(FM_PWR_CTRL1) == 0 && load(0x381c) == 0x51 && load(0x4100) == 0);
    const unsigned operations = ops, written = writes_total;
    /* the core takes 7 ms to RUN: still within the 100 ms wait */
    fixture(6); run_delay = 7; ampLoadMutedProtection(0x40, 6); assert(confirmed == 1 && !core_en);
    /* every single bus failure: no confirmation, key locked unless a lock write failed, core stopped unless a stop op failed */
    for (unsigned i = 1; i <= operations; i++) {
        fixture(6); fail_at = i; ampLoadMutedProtection(0x40, 6);
        assert(!confirmed && ops >= i);
        assert(!unlocked || failed_reg == FM_TEST_KEY);
        assert(!core_en || fail_stopping);
        if (ever_started) assert(stopped_msg + refused_run >= 1);
    }
    /* firmware states that must stop it */
    fixture(6); never_run = 1; ampLoadMutedProtection(0x40, 6);
    assert(!confirmed && refused_run == 1 && stopped_msg == 1 && !core_en && soft_reset && sim_ms >= 1100);
    fixture(6); bad_mbox = 1; ampLoadMutedProtection(0x40, 6);
    assert(!confirmed && refused_run == 1 && stopped_msg == 1 && !core_en && !mbox_cmds);
    fixture(6); no_ack = 1; ampLoadMutedProtection(0x40, 6);
    assert(!confirmed && refused_run == 1 && !core_en && load(FM_IRQ2_MASK2) == 0xffffffffu);
    /* identity and output guards refuse before any write */
    fixture(6); otp_id = 2; ampLoadMutedProtection(0x40, 6); assert(!writes_total && !dsp_writes && !b2_applied);
    fixture(6); store(FM_PWR_CTRL1, 1); ampLoadMutedProtection(0x40, 6); assert(!writes_total && !dsp_writes);
    /* mode 5 stays as it was: no errata, no trims, no run */
    fixture(5); ampLoadMutedProtection(0x40, 5);
    assert(tuned == 1 && !b2_applied && !b2_writes && !ever_started && !confirmed);
    /* mode 7 閘門：cspl_state=0x10 或 errorno!=0 都拒絕，0/0/1 才通過，且在 ampLoadMutedProtection 內絕不寫出聲暫存器。 */
    fixture(7); assert(!ampLoadMutedProtection(0x40, 7));
    assert(confirmed == 1 && tone_gate_refused == 1 && !core_en && !unlocked);
    fixture(7); store(FM_CSPL_STATE, 0); store(FM_CSPL_ERRORNO, 1); assert(!ampLoadMutedProtection(0x40, 7));
    assert(confirmed == 1 && tone_gate_refused == 1 && !core_en && !unlocked);
    fixture(7); store(FM_CSPL_STATE, 0); store(FM_CSPL_ERRORNO, 0); assert(ampLoadMutedProtection(0x40, 7) == 1);
    assert(confirmed == 1 && !tone_gate_refused && !core_en && !unlocked);
    /* mode 8 尚未定義。 */
    fixture(6); assert(!ampLoadMutedProtection(0x40, 8)); assert(!ops);
    printf("PASS muted CS35L41 B2 protection run: %u operations, %u register writes, %u injected bus failures\n",
        operations, written, operations);
    return 0;
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="clang")
    parser.add_argument("--source", type=Path, default=SOURCE, help="fuxi_amp.c to test (mutation checks)")
    args = parser.parse_args()
    source = args.source.read_text(encoding="utf-8")
    constants = "\n".join(line for line in source.splitlines() if line.startswith("#define FM_"))
    probe = source[source.index("static int ampWriteZero("):source.index("/* The survey reads")]
    model = MODEL.replace("@LINUX_MAP@", linux_otp_map())
    with tempfile.TemporaryDirectory() as tmp:
        directory = Path(tmp)
        path = directory / "run.c"
        path.write_text(constants + "\n" + model + probe + TEST, encoding="utf-8")
        exe = directory / "run"
        subprocess.run([args.cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
                        "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                        str(path), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=120)


if __name__ == "__main__":
    main()
