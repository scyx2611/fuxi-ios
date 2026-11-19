"""Run fuxi_amp.c mode 7 (bottom CS35L41 B2 -30 dB tone stage) against a simulated amplifier and PCM handshake.

Verifies that ampTone() follows the early Fuxi au_amp_on.h register sequence, keeps 0x6c04 at 0 and 0x6000 strictly
in {0xa678 (mute), 0xb885 (-30 dB)}, configures boost peak current before AMP_EN/GLOBAL_EN, unmutes only after PUP_DONE,
PLL lock, 200 ms settling and a clean status check (and before CLOCKED->ON), checks faults every 100 ms during ON,
and tears down safely on every fault, timeout and injected I2C bus failure.
"""
import argparse
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / "ksrc/xnu/fuxi-adsp/fuxi_amp.c"
INCLUDE = ROOT / "ksrc/xnu/fuxi-adsp"

MODEL = r"""
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fuxi_amp_tone.h"

uint32_t fuxi_amp_tone;
static struct { uint8_t address; unsigned error; } gBus;

#define SLOTS 1024u
static uint32_t keys[SLOTS], vals[SLOTS];
static uint8_t used[SLOTS];
static unsigned slot(uint32_t reg)
{
    unsigned h = (uint32_t)(reg * 2654435761u) >> 22;
    while (used[h] && keys[h] != reg) h = (h + 1) & (SLOTS - 1);
    return h;
}
static uint32_t load(uint32_t reg) { unsigned h = slot(reg); return used[h] ? vals[h] : 0; }
static void store(uint32_t reg, uint32_t v) { unsigned h = slot(reg); used[h] = 1; keys[h] = reg; vals[h] = v; }

#define MAX_LOG 256u
static struct { uint32_t reg, val; } write_log[MAX_LOG];
static unsigned write_log_n;

static unsigned ops, fail_at, fail_in_down, in_down, sim_ms;
static uint32_t failed_reg, readback_corrupt_reg;
static unsigned key_step, unlocked, boost_mask, pll_steps;
static unsigned pup_req_ms, pdn_req_ms, pup_delay, pdn_delay, pup_done_ms;
static bool pup_active, pdn_active, pup_seen, pdn_seen, pll_checked, unmuted, down_called;
static bool no_pup, no_pdn;
static uint32_t pup_fault_bits, pll_status3, check_fault_bits;
static unsigned check_reads, check_fault_at;

/* 模擬 PCM 端在 IOSleep 內推進 fuxi_amp_tone 狀態。 */
static bool pcm_clock_enabled, pcm_drain_enabled, pcm_fail_in_ready, pcm_fail_in_on;
static unsigned pcm_clock_at, pcm_drain_delay, pcm_on_seen_ms, pcm_fail_at;
static bool pcm_on_seen;

static unsigned msg_ready, msg_on, msg_done, msg_refused;

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static int step(uint32_t reg)
{
    if (++ops != fail_at) return 1;
    failed_reg = reg;
    fail_in_down = in_down;
    return 0;
}
static int ampTransferDone(void) { return 1; }

static void tick_pcm(void)
{
    const uint32_t s = fuxi_amp_tone_get();
    if (s == FUXI_TONE_READY) {
        if (pcm_fail_in_ready && sim_ms >= pcm_fail_at) {
            fuxi_amp_tone_move(FUXI_TONE_READY, FUXI_TONE_FAILED);
        } else if (pcm_clock_enabled && sim_ms >= pcm_clock_at) {
            fuxi_amp_tone_move(FUXI_TONE_READY, FUXI_TONE_CLOCKED);
        }
    } else if (s == FUXI_TONE_ON) {
        if (!pcm_on_seen) {
            pcm_on_seen = true;
            pcm_on_seen_ms = sim_ms;
        }
        if (pcm_fail_in_on && sim_ms - pcm_on_seen_ms >= pcm_fail_at) {
            fuxi_amp_tone_move(FUXI_TONE_ON, FUXI_TONE_FAILED);
        } else if (pcm_drain_enabled && sim_ms - pcm_on_seen_ms >= pcm_drain_delay) {
            fuxi_amp_tone_move(FUXI_TONE_ON, FUXI_TONE_DRAIN);
        }
    }
}

static void IOSleep(unsigned ms)
{
    sim_ms += ms;
    tick_pcm();
}

static void hardware_tick(void)
{
    if (pup_active && !no_pup && sim_ms - pup_req_ms >= pup_delay) {
        store(FM_IRQ1_STATUS1, load(FM_IRQ1_STATUS1) | FM_TONE_PUP_DONE);
        store(FM_IRQ1_STATUS3, pll_status3);
        pup_active = false;
        pup_done_ms = sim_ms;
    }
    if (pup_active && pup_fault_bits) {
        store(FM_IRQ1_STATUS1, load(FM_IRQ1_STATUS1) | pup_fault_bits);
    }
    if (pdn_active && !no_pdn && sim_ms - pdn_req_ms >= pdn_delay) {
        store(FM_IRQ1_STATUS1, load(FM_IRQ1_STATUS1) | FM_TONE_PDN_DONE);
        pdn_active = false;
    }
}

static int ampRead(uint8_t address, uint32_t reg, uint32_t *out)
{
    assert(address == 0x40);
    if (!step(reg)) return 0;
    hardware_tick();
    uint32_t v = load(reg);
    if (reg == FM_IRQ1_STATUS1 && pup_seen && !in_down) {
        check_reads++;
        if (check_fault_at && check_reads == check_fault_at) {
            v |= check_fault_bits;
            store(FM_IRQ1_STATUS1, v);
        }
    }
    if (reg == FM_IRQ1_STATUS3 && pup_seen && !pll_checked) {
        pll_checked = true;
    }
    if (readback_corrupt_reg && reg == readback_corrupt_reg) {
        v ^= 1u;
    }
    *out = v;
    return 1;
}

static void reg_write(uint32_t reg, uint32_t v)
{
    if (write_log_n < MAX_LOG) {
        write_log[write_log_n].reg = reg;
        write_log[write_log_n].val = v;
        write_log_n++;
    }
    /* 硬性安全不變量：0x6c04 永遠最低（0），0x6000 永遠只能是靜音或 -30 dB（0xb885）。 */
    if (reg == FM_AMP_GAIN) {
        assert(v == 0);
        if (pup_seen || (load(FM_PWR_CTRL1) & 1u) || pll_steps == 6) {
            in_down = 1;
            down_called = true;
        }
    }
    if (reg == FM_DIG_VOL) {
        assert(v == FM_DIG_VOL_MUTE || v == FM_DIG_VOL_LEVEL);
        if (v == FM_DIG_VOL_LEVEL) {
            /* 必須在 PUP_DONE、PLL 鎖定、等滿 200 ms 且做過狀態檢查後才解除靜音，且在 CLOCKED→ON 之前。 */
            assert(pup_seen && pll_checked && check_reads >= 1 && sim_ms >= pup_done_ms + 200);
            assert(fuxi_amp_tone_get() == FUXI_TONE_CLOCKED);
            assert(!unlocked && load(FM_AMP_GAIN) == 0 && (load(FM_PWR_CTRL1) & 1u));
            unmuted = true;
        }
    }
    if (reg == FM_TEST_KEY) {
        if (v == 0x55) key_step = 1;
        else if (v == 0xaa && key_step == 1) { unlocked = 1; key_step = 0; }
        else if (v == 0xcc) key_step = 2;
        else if (v == 0x33 && key_step == 2) { unlocked = 0; key_step = 0; }
        else assert(0);
        return;
    }
    if (reg == FM_TONE_TEST) {
        assert(unlocked == 1);
        assert(v == 0x002f1aa0u || v == 0x002f1aa3u);
        if (v == 0x002f1aa3u) {
            in_down = 1;
            down_called = true;
        }
        store(reg, v);
        return;
    }
    if (reg == 0x3810 || reg == 0x3814 || reg == 0x3808) {
        /* 升壓參數必須先於 AMP_EN 與 GLOBAL_EN。 */
        assert(!(load(FM_PWR_CTRL2) & 1u) && !(load(FM_PWR_CTRL1) & 1u));
        boost_mask |= reg == 0x3810 ? 1u : reg == 0x3814 ? 2u : 4u;
    }
    if (reg == FM_PLL_CLK_CTRL) {
        pll_steps++;
    }
    if (reg == FM_PWR_CTRL2 && (v & 1u)) {
        assert(boost_mask == 7u && pll_steps == 6 && load(FM_DIG_VOL) == FM_DIG_VOL_MUTE && load(FM_AMP_GAIN) == 0);
        assert(!(load(FM_PWR_CTRL1) & 1u));
    }
    if (reg == FM_PWR_CTRL1) {
        if (v & 1u) {
            assert(!unlocked);
            assert(boost_mask == 7u && pll_steps == 6 && (load(FM_PWR_CTRL2) & 1u));
            assert(load(FM_DIG_VOL) == FM_DIG_VOL_MUTE && load(FM_AMP_GAIN) == 0 && load(FM_TONE_TEST) == 0x002f1aa0u);
            pup_active = true;
            pup_req_ms = sim_ms;
        } else {
            assert(!unlocked || failed_reg == FM_TEST_KEY);
            assert((load(FM_DIG_VOL) == FM_DIG_VOL_MUTE || (fail_in_down && failed_reg == FM_DIG_VOL)) &&
                (load(FM_AMP_GAIN) == 0 || (fail_in_down && failed_reg == FM_AMP_GAIN)));
            in_down = 1;
            down_called = true;
            pdn_active = true;
            pdn_req_ms = sim_ms;
        }
    }
    if (reg >= FM_IRQ1_STATUS1 && reg <= FM_IRQ1_STATUS1 + 12u) {
        if (reg == FM_IRQ1_STATUS1 && (v & FM_TONE_PUP_DONE) && (load(reg) & FM_TONE_PUP_DONE)) pup_seen = true;
        if (reg == FM_IRQ1_STATUS1 && (v & FM_TONE_PDN_DONE) && (load(reg) & FM_TONE_PDN_DONE)) pdn_seen = true;
        store(reg, load(reg) & ~v);
        return;
    }
    store(reg, v);
}

static int geni_i2c_start_write_bytes(void *bus, const uint8_t *p, unsigned n)
{
    assert(bus == &gBus && gBus.address == 0x40 && n == 8);
    const uint32_t reg = be32(p), v = be32(p + 4);
    if (reg == FM_AMP_GAIN && (pup_seen || (load(FM_PWR_CTRL1) & 1u) || pll_steps == 6)) {
        in_down = 1;
        down_called = true;
    }
    if (!step(reg)) return -1;
    reg_write(reg, v);
    return 0;
}

static int record(const char *fmt, ...)
{
    char text[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    if (strstr(text, "tone ready, bottom amplifier muted")) msg_ready++;
    if (strstr(text, "tone on -30 dB")) msg_on++;
    if (strstr(text, "tone done stage=")) msg_done++;
    if (strstr(text, "tone refused")) msg_refused++;
    return 0;
}
#define printf record
"""

TEST = r"""
#undef printf
static void fixture(void)
{
    memset(used, 0, sizeof(used));
    memset(write_log, 0, sizeof(write_log));
    write_log_n = 0;
    ops = fail_at = fail_in_down = in_down = 0;
    sim_ms = 1000;
    failed_reg = readback_corrupt_reg = 0;
    key_step = unlocked = boost_mask = pll_steps = 0;
    pup_req_ms = pdn_req_ms = pup_done_ms = 0;
    pup_delay = 5;
    pdn_delay = 4;
    pup_active = pdn_active = pup_seen = pdn_seen = pll_checked = unmuted = down_called = false;
    no_pup = no_pdn = false;
    pup_fault_bits = 0;
    pll_status3 = FM_TONE_PLL_LOCK;
    check_fault_bits = 0;
    check_reads = check_fault_at = 0;
    pcm_clock_enabled = true;
    pcm_drain_enabled = true;
    pcm_fail_in_ready = false;
    pcm_fail_in_on = false;
    pcm_clock_at = 1030;
    pcm_drain_delay = 1100;
    pcm_on_seen_ms = pcm_fail_at = 0;
    pcm_on_seen = false;
    msg_ready = msg_on = msg_done = msg_refused = 0;
    __atomic_store_n(&fuxi_amp_tone, FUXI_TONE_PENDING, __ATOMIC_RELEASE);
    /* 模擬 mode 6 停機後的狀態：輸出關閉、有些舊的 sticky IRQ 旗標。 */
    store(FM_PWR_CTRL1, 0);
    store(FM_PWR_CTRL2, 0);
    store(FM_PLL_CLK_CTRL, 0x10);
    store(0x3800, 0);
    store(0x3808, 0x7f);
    store(0x3810, 0x1111);
    store(0x3814, 0x00ff);
    store(FM_IRQ1_STATUS1, 0);
    store(FM_IRQ1_STATUS2, 0x100002u);
    store(FM_IRQ1_STATUS3, 0x20000000u);
    store(0x1001cu, 0x3u);
}

int main(void)
{
    /* 1. 完整成功路徑：核對逐筆暫存器寫入順序與值（對應早期 au_amp_on.h）。 */
    fixture();
    assert(ampTone(0x40) == 1);
    assert(fuxi_amp_tone_get() == FUXI_TONE_OFF);
    assert(msg_ready == 1 && msg_on == 1 && msg_done == 1 && !msg_refused);
    assert(pup_seen && pdn_seen && pll_checked && unmuted && down_called && !unlocked);
    assert(load(FM_DIG_VOL) == FM_DIG_VOL_MUTE && load(FM_AMP_GAIN) == 0);
    assert(!(load(FM_PWR_CTRL1) & 1u) && !(load(FM_PWR_CTRL2) & 1u) && load(0x4800) == 0);
    assert(load(FM_TONE_TEST) == 0x002f1aa3u && check_reads == 13); /* 1 pre-unmute + 11 in ON + 1 in DRAIN */

    static const struct { uint32_t reg, val; } expected_writes[] = {
        /* step 12: clear IRQ1_STATUS1..4 */
        {0x10010, 0x0}, {0x10014, 0x100002u}, {0x10018, 0x20000000u}, {0x1001c, 0x3u},
        /* step 13: boost */
        {0x3810, 0x2424u}, {0x3814, 0x75fcu}, {0x3808, 0x40u},
        /* steps 15-24 */
        {0x2018, 0x20u}, {0x6c04, 0u}, {0x6000, 0xa678u},
        {0x2c0c, 3u}, {0x4804, 0x28u}, {0x4808, 0x20200000u}, {0x4840, 0x18u}, {0x4830, 0x18u},
        {0x4820, 0x401u}, {0x480c, 3u}, {0x4c00, 8u},
        {0x4c20, 0u}, {0x4c24, 0u}, {0x4c28, 0u}, {0x4c2c, 0u},
        {0x2d10, 0x18010u}, {0x4800, 0x10000u},
        /* step 25: PLL 6 RMWs from 0x10 */
        {0x2c04, 0x810u}, {0x2c04, 0xd10u}, {0x2c04, 0xd00u}, {0x2c04, 0xd00u}, {0x2c04, 0x500u}, {0x2c04, 0x510u},
        /* step 26 */
        {0x2018, 0x21u},
        /* steps 28-30: power up */
        {0x40, 0x55u}, {0x40, 0xaau}, {0x2084, 0x002f1aa0u}, {0x40, 0xccu}, {0x40, 0x33u},
        {0x2014, 1u}, {0x10010, 1u << 24},
        /* unmute -30 dB */
        {0x6000, 0xb885u},
        /* down */
        {0x6c04, 0u}, {0x6000, 0xa678u}, {0x2014, 0u}, {0x10010, 1u << 23},
        {0x40, 0x55u}, {0x40, 0xaau}, {0x2084, 0x002f1aa3u}, {0x40, 0xccu}, {0x40, 0x33u},
        {0x2018, 0x20u}, {0x4800, 0u},
    };
    const unsigned expected_n = sizeof(expected_writes) / sizeof(expected_writes[0]);
    assert(write_log_n == expected_n);
    for (unsigned i = 0; i < expected_n; i++) {
        assert(write_log[i].reg == expected_writes[i].reg && write_log[i].val == expected_writes[i].val);
    }
    const unsigned operations = ops;

    /* 2. 逐筆注入 I2C 失敗：每一點都不能成功，一律落到 FAILED，已動過功放就一定走下電流程。 */
    for (unsigned i = 1; i <= operations; i++) {
        fixture();
        fail_at = i;
        assert(ampTone(0x40) == 0);
        assert(fuxi_amp_tone_get() == FUXI_TONE_FAILED);
        assert(!msg_done && msg_refused >= 1);
        assert(!unlocked || failed_reg == FM_TEST_KEY);
        assert(down_called);
        if (!fail_in_down) {
            assert(load(FM_DIG_VOL) == FM_DIG_VOL_MUTE && load(FM_AMP_GAIN) == 0);
            assert(!(load(FM_PWR_CTRL1) & 1u) && !(load(FM_PWR_CTRL2) & 1u) && load(0x4800) == 0);
        }
    }

    /* 3. PCM 端先放棄，或 READY 等 15 秒逾時：絕不動功放任何暫存器。 */
    fixture();
    __atomic_store_n(&fuxi_amp_tone, FUXI_TONE_FAILED, __ATOMIC_RELEASE);
    assert(ampTone(0x40) == 0 && ops == 0 && !down_called);

    fixture();
    pcm_clock_enabled = false;
    assert(ampTone(0x40) == 0 && fuxi_amp_tone_get() == FUXI_TONE_FAILED && ops == 0 && !down_called && sim_ms >= 16000);

    fixture();
    pcm_clock_enabled = false;
    pcm_fail_in_ready = true;
    pcm_fail_at = 1025;
    assert(ampTone(0x40) == 0 && fuxi_amp_tone_get() == FUXI_TONE_FAILED && ops == 0 && !down_called);

    /* 4. 進入 prepare 前的守衛：輸出位元未清、0x3800 非零、寫後讀回不符。 */
    fixture();
    store(FM_PWR_CTRL1, 1);
    assert(ampTone(0x40) == 0 && fuxi_amp_tone_get() == FUXI_TONE_FAILED && !unmuted && down_called);

    fixture();
    store(0x3800, 1);
    assert(ampTone(0x40) == 0 && fuxi_amp_tone_get() == FUXI_TONE_FAILED && !unmuted && !pup_seen && down_called);

    fixture();
    readback_corrupt_reg = 0x4820;
    assert(ampTone(0x40) == 0 && fuxi_amp_tone_get() == FUXI_TONE_FAILED && !unmuted && !pup_seen && down_called);

    /* 5. 上電故障：六個 FM_TONE_ERRORS 位元逐一測、PUP 逾時、PLL 未鎖／失鎖／OTP 錯。 */
    static const uint32_t fault_bits[6] = {0x40u, 0x80u, 0x100u, 0x8000u, 0x20000u, 0x80000000u};
    for (unsigned i = 0; i < 6; i++) {
        fixture();
        pup_fault_bits = fault_bits[i];
        assert(ampTone(0x40) == 0 && fuxi_amp_tone_get() == FUXI_TONE_FAILED && !unmuted && down_called);
    }

    fixture();
    no_pup = true;
    assert(ampTone(0x40) == 0 && fuxi_amp_tone_get() == FUXI_TONE_FAILED && !unmuted && down_called);

    static const uint32_t bad_pll[3] = {0u, FM_TONE_PLL_LOCK | FM_TONE_PLL_UNLOCK, FM_TONE_PLL_LOCK | FM_OTP_BOOT_ERR};
    for (unsigned i = 0; i < 3; i++) {
        fixture();
        pll_status3 = bad_pll[i];
        assert(ampTone(0x40) == 0 && fuxi_amp_tone_get() == FUXI_TONE_FAILED && !unmuted && down_called);
    }

    /* 6. 200 ms 穩定後檢查故障（解除靜音前擋下）。 */
    fixture();
    check_fault_at = 1;
    check_fault_bits = 0x80u;
    assert(ampTone(0x40) == 0 && fuxi_amp_tone_get() == FUXI_TONE_FAILED && !unmuted && down_called);

    /* 7. ON 期間每 100 ms 檢查故障、PCM 中途放棄、ON 等 5 秒逾時。 */
    fixture();
    check_fault_at = 4; /* pre-unmute 1 次 + 第 300 ms 檢查 */
    check_fault_bits = 0x40u;
    assert(ampTone(0x40) == 0 && fuxi_amp_tone_get() == FUXI_TONE_FAILED && unmuted && down_called && check_reads == 4);

    fixture();
    pcm_drain_enabled = false;
    pcm_fail_in_on = true;
    pcm_fail_at = 250;
    assert(ampTone(0x40) == 0 && fuxi_amp_tone_get() == FUXI_TONE_FAILED && unmuted && down_called);

    fixture();
    pcm_drain_enabled = false;
    assert(ampTone(0x40) == 0 && fuxi_amp_tone_get() == FUXI_TONE_FAILED && unmuted && down_called && check_reads == 51);

    /* 8. DRAIN 最後一次檢查故障，以及下電沒有 PDN_DONE（必須標 FAILED 而不是 OFF）。 */
    fixture();
    check_fault_at = 13; /* DRAIN 最終檢查 */
    check_fault_bits = 0x20000u;
    assert(ampTone(0x40) == 0 && fuxi_amp_tone_get() == FUXI_TONE_FAILED && down_called);

    fixture();
    no_pdn = true;
    assert(ampTone(0x40) == 0 && fuxi_amp_tone_get() == FUXI_TONE_FAILED && down_called && !pdn_seen);

    printf("PASS CS35L41 B2 mode 7 tone: %u exact register writes, %u injected bus failures, all fault/timeout guards\n",
        expected_n, operations);
    return 0;
}
"""


def extract(text):
    constants = "\n".join(line for line in text.splitlines() if line.startswith("#define FM_"))
    rw = text[text.index("static int ampWrite32("):text.index("/* Linux v6.12 cs35l41-lib.c")]
    no_out = text[text.index("/* GLOBAL_EN (PWR_CTRL1 bit 0)"):text.index("/* Xiaomi cs35l41.c")]
    tone = text[text.index("/* ---- mode 7"):text.index("/* Mode 2.")]
    return constants + "\n" + MODEL + rw + "\n" + no_out + "\n" + tone + "\n" + TEST


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="clang")
    parser.add_argument("--source", type=Path, default=SOURCE, help="fuxi_amp.c to test (mutation checks)")
    args = parser.parse_args()
    source = args.source.read_text(encoding="utf-8")
    with tempfile.TemporaryDirectory(prefix="fuxi-amp-tone-") as tmp:
        directory = Path(tmp)
        path = directory / "tone.c"
        path.write_text(extract(source), encoding="utf-8")
        exe = directory / "tone"
        subprocess.run([args.cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
                        "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                        "-I", str(INCLUDE), str(path), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=60)


if __name__ == "__main__":
    main()
