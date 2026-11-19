import os
from pathlib import Path
import subprocess
import tempfile


root = Path(__file__).resolve().parents[1]
source = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "fuxi_haptic_rtp.h"

/* HAP525 假模型：Android 開機後留下的狀態，PLAY_EN 會讓 HPWR 變 READY，關掉後延遲幾次輪詢才回 DISABLED。 */
typedef struct {
    uint8_t reg[0x300];
    unsigned owners[3];
    unsigned disable_delay;   /* 關掉 PLAY 後還要幾次 wait 才回 0 */
    unsigned pending;
    int stuck;                /* 1：PLAY 關掉也不回 0；2：連關開模組都不回 0 */
    int write_fail, verify_lie, fault_sticky;
    unsigned writes, waits;
    unsigned written[64];
} Model;

static int owner(void *context, unsigned peripheral, unsigned *value)
{
    Model *model = context;
    if (peripheral < 0xf0 || peripheral > 0xf2) return -1;
    *value = model->owners[peripheral - 0xf0];
    return 0;
}

static int rd(void *context, unsigned address, uint8_t *value)
{
    Model *model = context;
    if (address < 0xf000 || address > 0xf2ff) return -1;
    *value = model->reg[address - 0xf000];
    return 0;
}

static int wr(void *context, unsigned address, uint8_t value)
{
    Model *model = context;
    assert(fuxi_haptic_writable(address));
    assert(address == 0xf04c || address == 0xf046 || address == 0xf126 || address == 0xf066);
    if (model->writes < 64) model->written[model->writes] = address;
    model->writes++;
    if (model->write_fail) return -1;
    if (address == 0xf066) {
        /* FAULT_CLR 寫一清除，讀回永遠 0 */
        if (!model->fault_sticky) model->reg[0x0c] &= (uint8_t)~value;
        return 0;
    }
    model->reg[address - 0xf000] = model->verify_lie ? (uint8_t)(value ^ 0x40) : value;
    if (address == 0xf04c) {
        if (value & 0x80u) {
            model->reg[0x0b] = 3;
            model->pending = 0;
        } else {
            model->pending = model->stuck ? 1000 : model->disable_delay;
            if (!model->pending) model->reg[0x0b] = 0;
        }
    } else if (address == 0xf046 && !(value & 0x80u) && model->stuck < 2) {
        model->reg[0x0b] = 0;
        model->pending = 0;
        model->reg[0x4c] &= 0x7fu;
    }
    return 0;
}

static void wait_us(void *context, unsigned microseconds)
{
    Model *model = context;
    assert(microseconds == 1000);
    model->waits++;
    if (model->pending && !--model->pending) model->reg[0x0b] = 0;
}

static void android(Model *model)
{
    memset(model, 0, sizeof(*model));
    model->owners[2] = 3;
    static const unsigned identities[] = {0xf001, 0xf004, 0xf005, 0xf101, 0xf104, 0xf105, 0xf200, 0xf201, 0xf204, 0xf205};
    static const uint8_t expected[] = {4, 0x15, 0x0a, 4, 0x15, 0x0b, 2, 0, 0x31, 1};
    for (unsigned index = 0; index < 10; index++) model->reg[identities[index] - 0xf000] = expected[index];
    model->reg[0x46] = 0x80;  /* 模組開 */
    model->reg[0x48] = 0x1c;  /* VMAX 1400 mV */
    model->reg[0x4c] = 0x01;  /* DIRECT_PLAY，PLAY_EN 關 */
    model->reg[0x63] = 0x89;  /* 自動共振開 */
    model->reg[0x68] = 0x54;
    model->reg[0x126] = 0xff; /* DIRECT_PLAY 振幅 */
    model->reg[0x241] = 0xe0; /* boost 跟隨硬體 */
    model->disable_delay = 3;
}

static FuxiHapticIo io_for(Model *model)
{
    FuxiHapticIo io = {model, owner, rd, wr, wait_us, 0};
    return io;
}

static Model rtp_model;
static unsigned rtp_fill, rtp_writes, rtp_fail, rtp_played, rtp_received;
static unsigned rtp_expected_prefill;
static uint8_t rtp_data[FUXI_RTP_BYTES];

static void rtp_status(void)
{
    rtp_model.reg[9] = (rtp_fill >> 8) | (rtp_played ? 0x40 : 0);
    rtp_model.reg[10] = (uint8_t)rtp_fill;
}
static int rtp_read_status(uint8_t status[2])
{
    status[0] = rtp_model.reg[9];
    status[1] = rtp_model.reg[10];
    return 0;
}
static int rtp_write(unsigned addr, const uint8_t *data, unsigned n)
{
    if (++rtp_writes == rtp_fail) return -1;
    if (addr == 0xf120) {
        assert(n == 4 && rtp_received + n <= sizeof(rtp_data));
        memcpy(rtp_data + rtp_received, data, n);
        rtp_received += n;
        rtp_fill += n;
    } else {
        assert(n == 1 && addr >= 0xf000 && addr < 0xf200);
        rtp_model.reg[addr-0xf000] = *data;
        if (addr == 0xf12d && *data == 1) rtp_fill = 0;
        if (addr == 0xf04c) {
            rtp_model.reg[11] = (*data & 0x80) ? 3 : 0;
            if (*data & 0x80) {
                assert(rtp_fill == rtp_expected_prefill);
                rtp_played++;
            }
        }
    }
    rtp_status();
    return 0;
}
static void rtp_wait(void *context, unsigned us)
{
    (void)context;
    assert(us == 1000);
    if (rtp_model.reg[0x4c] == 0x80) rtp_fill = rtp_fill < 24 ? 0 : rtp_fill - 24;
    rtp_status();
}
static void test_rtp(void)
{
    uint8_t waveform[FUXI_RTP_BYTES] = {0};
    for (unsigned i = 0; i < sizeof(waveform)-4; i++) waveform[i] = (uint8_t)(i * 3);
    rtp_expected_prefill = FUXI_RTP_PREFILL;
    for (unsigned fail = 0; fail < 108; fail++) {
        android(&rtp_model);
        rtp_model.reg[0x1a0] = 15;
        rtp_writes = rtp_fill = rtp_played = rtp_received = 0;
        rtp_fail = fail;
        rtp_status();
        FuxiHapticIo io = io_for(&rtp_model);
        io.wait_us = rtp_wait;
        FuxiRtpStats stats;
        int result = fuxi_rtp_click(&io, rtp_write, rtp_read_status, waveform, FUXI_RTP_BYTES, 0x30, &stats);
        if (!fail) {
            assert(!result && stats.sent == 376 && stats.polls == 16 && rtp_played == 1);
            assert(stats.before_start == 320 && stats.before_refill == 296 && !stats.last_fill);
            assert(!memcmp(rtp_data, waveform, sizeof(waveform)));
        } else assert(result);
        assert(rtp_model.reg[0x4c] != 0x80 && !rtp_fill);
        assert(rtp_model.reg[0x48] == 0x1c);
    }
    android(&rtp_model);
    rtp_model.reg[0x1a0] = 15;
    rtp_played = 1;
    rtp_fill = 284;
    rtp_fail = 0;
    rtp_status();
    FuxiHapticIo io = io_for(&rtp_model);
    FuxiRtpStats stats;
    assert(fuxi_rtp_click(&io, rtp_write, rtp_read_status, waveform, FUXI_RTP_BYTES, 0, &stats) && !stats.sent);
    assert(rtp_fill == 284 && rtp_model.reg[0x12d] == 0);
    rtp_fill = rtp_played = rtp_received = 0;
    rtp_status();
    io.wait_us = rtp_wait;
    static const uint8_t tick[] = {12, (uint8_t)-12, 24, (uint8_t)-24, 0, 0, 0, 0};
    rtp_expected_prefill = sizeof(tick);
    assert(!fuxi_rtp_click(&io, rtp_write, rtp_read_status, tick, sizeof(tick), 0, &stats));
    assert(stats.sent == sizeof(tick) && stats.before_start == sizeof(tick) && stats.last_fill == 0);
    assert(!memcmp(rtp_data, tick, sizeof(tick)) && rtp_played == 1);
    uint8_t quiet[] = {4, (uint8_t)-4, 2, 0};
    uint8_t strong[] = {96, (uint8_t)-96, 48, 0};
    assert(fuxi_rtp_pcm_gain(quiet, sizeof(quiet)) == 8);
    assert(quiet[0] == 8 && (int8_t)quiet[1] == -8 && quiet[2] == 4 && !quiet[3]);
    assert(fuxi_rtp_pcm_gain(strong, sizeof(strong)) == 96);
    assert(strong[0] == 96 && (int8_t)strong[1] == -96 && strong[2] == 48 && !strong[3]);
}

int main(void)
{
    Model model;
    FuxiHapticIo io;
    unsigned polls = 0, toggled = 0;
    uint8_t fault = 0, hpwr = 0;
    unsigned cleared = 0;

    /* 呼叫者 */
    assert(fuxi_vibrate_caller_allowed(1, 0, NULL));
    assert(fuxi_vibrate_caller_allowed(0, 1, "SpringBoard"));
    assert(fuxi_vibrate_caller_allowed(0, 1, "mediaserverd"));
    assert(!fuxi_vibrate_caller_allowed(0, 0, "mediaserverd"));
    assert(!fuxi_vibrate_caller_allowed(0, 1, "mediaserver"));
    assert(!fuxi_vibrate_caller_allowed(0, 1, "mediaserverdX"));
    assert(!fuxi_vibrate_caller_allowed(0, 1, "SpringBoardX"));
    assert(!fuxi_vibrate_caller_allowed(0, 1, ""));
    assert(!fuxi_vibrate_caller_allowed(0, 1, NULL));

    /* 請求：期限、覆蓋、停止、上限、溢位、鎖死 */
    FuxiVibrateRuntime state = {0}, saved;
    assert(!fuxi_vibrate_wanted(&state, 0));
    assert(!fuxi_vibrate_request(&state, 15, 1000));
    assert(fuxi_vibrate_wanted(&state, 1000));
    assert(fuxi_vibrate_remaining_ms(&state, 1000) == 15);
    assert(fuxi_vibrate_remaining_ms(&state, 1000 + 14000001ull) == 1);
    assert(fuxi_vibrate_wanted(&state, 1000 + 14999999ull));
    assert(!fuxi_vibrate_wanted(&state, 1000 + 15000000ull));
    assert(!fuxi_vibrate_remaining_ms(&state, 1000 + 15000000ull));
    assert(!fuxi_vibrate_request(&state, 2000, 5));
    assert(!fuxi_vibrate_request(&state, 30, 6));
    assert(fuxi_vibrate_remaining_ms(&state, 6) == 30);
    assert(!fuxi_vibrate_request(&state, 0, 7));
    assert(!fuxi_vibrate_wanted(&state, 7));
    saved = state;
    assert(fuxi_vibrate_request(&state, 2001, 8) < 0);
    assert(!memcmp(&state, &saved, sizeof(state)));
    assert(fuxi_vibrate_request(&state, 1, UINT64_MAX - 5) < 0);
    assert(!memcmp(&state, &saved, sizeof(state)));
    assert(!fuxi_vibrate_request(&state, 100, 9));
    state.failed = 1;
    assert(!fuxi_vibrate_wanted(&state, 10));
    assert(!fuxi_vibrate_remaining_ms(&state, 10));
    assert(fuxi_vibrate_request(&state, 10, 11) < 0);
    assert(!fuxi_vibrate_request(&state, 0, 12));
    assert(fuxi_vibrate_request(NULL, 0, 0) < 0);

    /* sysctl 值：bit 0..15 毫秒、bit 16..23 振幅；舊使用端只送毫秒＝振幅 0（沿用） */
    unsigned ms = 7, level = 7;
    assert(!fuxi_vibrate_decode(15, &ms, &level) && ms == 15 && level == 0);
    assert(!fuxi_vibrate_decode(15 | (0x60 << 16), &ms, &level) && ms == 15 && level == 0x60);
    assert(!fuxi_vibrate_decode(0xff << 16, &ms, &level) && ms == 0 && level == 0xff);
    assert(!fuxi_vibrate_decode(2000 | (0xff << 16), &ms, &level) && ms == 2000 && level == 0xff);
    assert(fuxi_vibrate_decode(2001, &ms, &level) < 0);
    assert(fuxi_vibrate_decode(2001 | (0x60 << 16), &ms, &level) < 0);
    assert(fuxi_vibrate_decode(1 << 24, &ms, &level) < 0);
    assert(fuxi_vibrate_decode(-1, &ms, &level) < 0);
    FuxiVibrateRuntime leveled = {0};
    assert(!fuxi_vibrate_request_level(&leveled, 20, 0x60, 1) && leveled.level == 0x60);
    assert(!fuxi_vibrate_request_level(&leveled, 0, 0, 2) && leveled.level == 0x60 && !fuxi_vibrate_wanted(&leveled, 2));
    assert(!fuxi_vibrate_request_level(&leveled, 20, 0, 3) && leveled.level == 0);
    saved = leveled;
    assert(fuxi_vibrate_request_level(&leveled, 20, 0x100, 4) < 0 && !memcmp(&leveled, &saved, sizeof(leveled)));
    assert(fuxi_vibrate_request_level(&leveled, 2001, 0x60, 4) < 0 && !memcmp(&leveled, &saved, sizeof(leveled)));
    leveled.failed = 1;
    assert(fuxi_vibrate_request_level(&leveled, 20, 0x60, 5) < 0 && leveled.level == 0);

    /* Android 留下的狀態通過檢查，且檢查不寫任何暫存器 */
    android(&model);
    io = io_for(&model);
    assert(fuxi_vibrate_check(&io) == FUXI_HAPTIC_OK);
    assert(model.writes == 0);

    /* 檢查拒絕：owner、身分、模組關、PLAY 不是閒置 DIRECT_PLAY、VMAX 過高、振幅 0、自動共振關、fault、忙碌 */
    android(&model); model.owners[0] = 1; assert(fuxi_vibrate_check(&io) == FUXI_HAPTIC_HARDWARE);
    android(&model); model.owners[2] = 0; assert(fuxi_vibrate_check(&io) == FUXI_HAPTIC_HARDWARE);
    android(&model); model.reg[0x205] = 2; assert(fuxi_vibrate_check(&io) == FUXI_HAPTIC_HARDWARE);
    android(&model); model.reg[0x46] = 0; assert(fuxi_vibrate_check(&io) == FUXI_HAPTIC_HARDWARE);
    android(&model); model.reg[0x4c] = 0x02; assert(fuxi_vibrate_check(&io) == FUXI_HAPTIC_HARDWARE);
    android(&model); model.reg[0x4c] = 0x81; assert(fuxi_vibrate_check(&io) == FUXI_HAPTIC_HARDWARE);
    android(&model); model.reg[0x48] = 0x1d; assert(fuxi_vibrate_check(&io) == FUXI_HAPTIC_HARDWARE);
    android(&model); model.reg[0x48] = 0; assert(fuxi_vibrate_check(&io) == FUXI_HAPTIC_HARDWARE);
    android(&model); model.reg[0x126] = 0; assert(fuxi_vibrate_check(&io) == FUXI_HAPTIC_HARDWARE);
    android(&model); model.reg[0x63] = 0x09; assert(fuxi_vibrate_check(&io) == FUXI_HAPTIC_HARDWARE);
    android(&model); model.reg[0x0c] = 0x04; assert(fuxi_vibrate_check(&io) == FUXI_HAPTIC_FAULT);
    android(&model); model.reg[0x0b] = 3; assert(fuxi_vibrate_check(&io) == FUXI_HAPTIC_BUSY);
    android(&model); model.reg[0x241] = 0x60; assert(fuxi_vibrate_check(&io) == FUXI_HAPTIC_HARDWARE);
    android(&model); assert(model.writes == 0);

    /* 正常一下：0x81 開、READY、0x01 關、三次輪詢後 DISABLED，不動模組 */
    android(&model);
    assert(fuxi_vibrate_start(&io, 0, &cleared) == FUXI_HAPTIC_OK);
    assert(model.reg[0x4c] == 0x81 && model.reg[0x0b] == 3);
    assert(fuxi_vibrate_health(&io, &fault, &hpwr) == FUXI_HAPTIC_OK && hpwr == 3 && !fault);
    assert(fuxi_vibrate_stop(&io, 0, &polls, &toggled) == FUXI_HAPTIC_OK);
    assert(polls == 3 && !toggled && model.reg[0x4c] == 0x01 && model.reg[0x46] == 0x80 && !model.reg[0x0b]);
    assert(model.writes == 2 && model.written[0] == 0xf04c && model.written[1] == 0xf04c);
    assert(fuxi_vibrate_check(&io) == FUXI_HAPTIC_OK);

    /* 立刻 DISABLED：只輪詢一次 */
    android(&model); model.disable_delay = 0;
    assert(!fuxi_vibrate_start(&io, 0, &cleared));
    assert(!fuxi_vibrate_stop(&io, 0, &polls, &toggled) && polls == 1 && !toggled);

    /* 播放中 AUTO_RES_ERROR（低振幅實機 0x2）：照常播放，只回報；SC、HPWR_RDY、未知位元才是 fault */
    android(&model);
    assert(!fuxi_vibrate_start(&io, 0, &cleared) && !cleared);
    model.reg[0x0c] = 0x02;
    assert(fuxi_vibrate_health(&io, &fault, &hpwr) == FUXI_HAPTIC_OK && fault == 2 && hpwr == 3);
    model.reg[0x0c] = 0x04;
    assert(fuxi_vibrate_health(&io, &fault, &hpwr) == FUXI_HAPTIC_FAULT && fault == 4);
    model.reg[0x0c] = 0x06;
    assert(fuxi_vibrate_health(&io, &fault, &hpwr) == FUXI_HAPTIC_FAULT);
    model.reg[0x0c] = 0x01;
    assert(fuxi_vibrate_health(&io, &fault, &hpwr) == FUXI_HAPTIC_FAULT);
    model.reg[0x0c] = 0x08;
    assert(fuxi_vibrate_health(&io, &fault, &hpwr) == FUXI_HAPTIC_FAULT);

    /* 上一次留下 AUTO_RES_ERROR：開播前寫 FAULT_CLR=0x02、確認歸零才寫 PLAY */
    android(&model); model.reg[0x0c] = 0x02; cleared = 0;
    assert(fuxi_vibrate_start(&io, 0, &cleared) == FUXI_HAPTIC_OK && cleared == 1);
    assert(model.writes == 2 && model.written[0] == 0xf066 && model.written[1] == 0xf04c);
    assert(!model.reg[0x0c] && model.reg[0x4c] == 0x81);
    /* 清不掉：不開 */
    android(&model); model.reg[0x0c] = 0x02; model.fault_sticky = 1; cleared = 0;
    assert(fuxi_vibrate_start(&io, 0, &cleared) == FUXI_HAPTIC_FAULT && cleared == 1);
    assert(model.writes == 1 && model.reg[0x4c] == 0x01);
    /* SC 跟 AUTO_RES 一起：不清、不開 */
    android(&model); model.reg[0x0c] = 0x06; cleared = 0;
    assert(fuxi_vibrate_start(&io, 0, &cleared) == FUXI_HAPTIC_FAULT && !cleared && !model.writes);
    /* owner 換人：不清、不開 */
    android(&model); model.reg[0x0c] = 0x02; model.owners[0] = 2; cleared = 0;
    assert(fuxi_vibrate_start(&io, 0, &cleared) == FUXI_HAPTIC_HARDWARE && !cleared && !model.writes);
    /* 清除寫入失敗 */
    android(&model); model.reg[0x0c] = 0x02; model.write_fail = 1; cleared = 0;
    assert(fuxi_vibrate_start(&io, 0, &cleared) == FUXI_HAPTIC_IO && !cleared && model.reg[0x4c] == 0x01);

    /* 有 fault 就不開 */
    android(&model); model.reg[0x0c] = 0x01;
    assert(fuxi_vibrate_start(&io, 0, &cleared) == FUXI_HAPTIC_FAULT && model.writes == 0 && model.reg[0x4c] == 0x01);

    /* HPWR 卡住：照 Linux 關開模組一次，之後回 DISABLED，模組留在開 */
    android(&model); model.stuck = 1;
    assert(!fuxi_vibrate_start(&io, 0, &cleared));
    assert(fuxi_vibrate_stop(&io, 0, &polls, &toggled) == FUXI_HAPTIC_OK);
    assert(toggled && polls == FUXI_VIBRATE_STOP_POLLS + 1 && model.reg[0x46] == 0x80 && !model.reg[0x0b]);
    assert(model.writes == 4 && model.written[2] == 0xf046 && model.written[3] == 0xf046);

    /* 連關開模組都停不下來：回逾時，呼叫端鎖死 */
    android(&model); model.stuck = 2;
    assert(!fuxi_vibrate_start(&io, 0, &cleared));
    assert(fuxi_vibrate_stop(&io, 0, &polls, &toggled) == FUXI_HAPTIC_TIMEOUT);
    assert(toggled && polls == 2 * FUXI_VIBRATE_STOP_POLLS);

    /* 寫入讀回不符 */
    android(&model); model.verify_lie = 1;
    assert(fuxi_vibrate_start(&io, 0, &cleared) == FUXI_HAPTIC_VERIFY);

    /* 寫入失敗時停止仍會輪詢、走退路，結果不得是 OK */
    android(&model);
    assert(!fuxi_vibrate_start(&io, 0, &cleared));
    model.write_fail = 1;
    assert(fuxi_vibrate_stop(&io, 0, &polls, &toggled) != FUXI_HAPTIC_OK);

    /* owner 改變時停止直接回硬體錯誤（不寫） */
    android(&model);
    assert(!fuxi_vibrate_start(&io, 0, &cleared));
    model.owners[0] = 2;
    unsigned before = model.writes;
    assert(fuxi_vibrate_stop(&io, 0, &polls, &toggled) == FUXI_HAPTIC_HARDWARE && model.writes == before);

    /* 振幅：0 或相同不寫；不同就寫 0xf126 並讀回；播放中可改；讀回不符、owner 換人不更新現值 */
    uint8_t current = 0;
    unsigned amplitude_writes = 0;
    android(&model);
    current = model.reg[0x126];
    assert(!fuxi_vibrate_amplitude(&io, 0, &current, &amplitude_writes) && !amplitude_writes && !model.writes);
    assert(!fuxi_vibrate_amplitude(&io, 0xff, &current, &amplitude_writes) && !amplitude_writes && !model.writes);
    assert(!fuxi_vibrate_amplitude(&io, 0x60, &current, &amplitude_writes));
    assert(current == 0x60 && model.reg[0x126] == 0x60 && amplitude_writes == 1 && model.written[0] == 0xf126);
    assert(!fuxi_vibrate_start(&io, 0, &cleared));
    assert(!fuxi_vibrate_amplitude(&io, 0x30, &current, &amplitude_writes) && current == 0x30 && model.reg[0x4c] == 0x81);
    assert(!fuxi_vibrate_stop(&io, 0, &polls, &toggled) && model.reg[0x126] == 0x30);
    assert(fuxi_vibrate_amplitude(&io, 0x100, &current, &amplitude_writes) == FUXI_HAPTIC_INVALID && current == 0x30);
    android(&model); current = 0xff; model.verify_lie = 1;
    assert(fuxi_vibrate_amplitude(&io, 0x60, &current, &amplitude_writes) == FUXI_HAPTIC_VERIFY && current == 0xff);
    android(&model); current = 0xff; model.owners[1] = 2;
    assert(fuxi_vibrate_amplitude(&io, 0x60, &current, &amplitude_writes) == FUXI_HAPTIC_HARDWARE);
    assert(current == 0xff && !model.writes && model.reg[0x126] == 0xff);

    android(&model);
    assert(!fuxi_vibrate_stock_brake(&io));
    model.reg[0x50] = 0x41;
    model.reg[0x170] = 0xff; model.reg[0x171] = 0x3f; model.reg[0x172] = 0x1f;
    assert(fuxi_vibrate_stock_brake(&io));
    model.reg[0x177] = 1;
    assert(!fuxi_vibrate_stock_brake(&io));
    assert(!fuxi_vibrate_start(&io, 8, &cleared) && model.reg[0x4c] == 0x89);
    assert(!fuxi_vibrate_stop(&io, 8, &polls, &toggled) && !toggled && model.reg[0x4c] == 1);
    assert(!fuxi_vibrate_attack_level(0) && fuxi_vibrate_attack_level(4) == 66);
    for (unsigned level = 1; level <= 255; level++) {
        assert(fuxi_vibrate_attack_level(level) >= level && fuxi_vibrate_attack_level(level) <= 255);
    }
    test_rtp();
    puts("PASS: HAP525 DIRECT_PLAY runtime caller policy, lease, amplitude field, Android-state check, start/stop/HPWR poll, module toggle fallback, AUTO_RES clear/tolerate, fault and verify paths");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='fuxi-haptic-runtime-') as temporary:
    directory = Path(temporary)
    cfile, binary = directory / 'runtime.c', directory / 'runtime'
    cfile.write_text(source, encoding='utf-8')
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-g', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I' + str(root / 'xnu/fuxi-adsp'), str(cfile), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
