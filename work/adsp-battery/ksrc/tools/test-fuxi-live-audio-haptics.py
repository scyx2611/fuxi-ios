#!/usr/bin/env python3
"""0124 fuxi-live-audio-haptics 主機測試（ASan/UBSan）。

從 0123 與 0124 補丁抽出 fuxi_audio_stream.h、fuxi_haptic_shape.h 與 fuxi_haptic_shape_v2.h，
測試即時喇叭環形緩衝、TDM 轉換、安全音量／增益上限、開機驗證音、觸感整形 v2 快速滾輪與連續手感，
以及 0124 核心閘門條件。
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
P0123 = ROOT / 'xnu/patches/0123-fuxi-haptic-shape.patch'
P0124 = ROOT / 'xnu/patches/0124-fuxi-live-audio-haptics.patch'

TEST_C = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fuxi_audio_stream.h"
#include "fuxi_haptic_shape_v2.h"
#include "fuxi_haptic_shape_v3.h"
#include "fuxi_haptic_rtp.h"
#include "fuxi_haptic_stream.h"
#include "fuxi_audio_mic.h"
#include <math.h>

#define MS(x) ((uint64_t)(x) * 1000000ull)

static void test_audio_volume_and_gain(void) {
    assert(fuxi_audio_vol_pack(500, 0) == 500u);
    assert(fuxi_audio_vol_pack(500, 1) == (500u | FUXI_AUDIO_VOL_ACTIVE_BIT));
    assert(fuxi_audio_vol_milli(fuxi_audio_vol_pack(5000, 1)) == FUXI_AUDIO_VOL_MAX_MILLI);
    assert(fuxi_audio_vol_active(fuxi_audio_vol_pack(0, 1)) == 1);
    assert(fuxi_audio_vol_active(fuxi_audio_vol_pack(1000, 0)) == 0);

    assert(fuxi_audio_clamp_gain(0) == 0);
    assert(fuxi_audio_clamp_gain(12) == 12);
    assert(fuxi_audio_clamp_gain(18) == 18);
    assert(fuxi_audio_clamp_gain(25) == 18);

    /* 0 音量一律靜音且啟用 2 ms 軟斜坡 (VOL_RAMP=3) */
    assert(fuxi_audio_volume_to_reg(0, -12 * 8) == FUXI_AUDIO_DIG_VOL_MUTE);
    assert(FUXI_AUDIO_DIG_VOL_MUTE == 0xa67bu && (FUXI_AUDIO_DIG_VOL_MUTE & 0x8007u) == 0x8003u);
    /* max_db_x8 > 0 強制夾回 0 dB；HPF=1、soft ramp=3 */
    uint32_t at_zero = fuxi_audio_volume_to_reg(1000, 80);
    assert(at_zero == (0x8000u | (0u << 3) | 3u));
    uint32_t at_m12 = fuxi_audio_volume_to_reg(1000, -12 * 8);
    assert((at_m12 & 0x8007u) == 0x8003u);
    int signed_code = (int)((at_m12 >> 3) & 0x7ffu);
    if (signed_code >= 1024) signed_code -= 2048;
    assert(signed_code == -96);

    /* 單調遞增（二補數轉回有號值比較） */
    int prev = -1000;
    for (uint32_t v = 1; v <= 1000; v += 25) {
        uint32_t reg = fuxi_audio_volume_to_reg(v, -12 * 8);
        assert((reg & 0x8007u) == 0x8003u);
        int cur = (int)((reg >> 3) & 0x7ffu);
        if (cur >= 1024) cur -= 2048;
        assert(cur >= prev && cur <= -96);
        prev = cur;
    }
}

static void test_audio_ring_and_chime(void) {
    static FuxiAudioRing ring;
    memset(&ring, 0, sizeof(ring));
    static int16_t chunk512[512 * 2];
    for (int i = 0; i < 512; i++) {
        chunk512[i * 2] = 1000;
        chunk512[i * 2 + 1] = 2000;
    }
    static volatile uint32_t tdm[960 * 4];

    /* 預緩衝水位：active=1 且未滿 2880 frames 時不提前抽乾、不計 underrun */
    assert(fuxi_audio_ring_write(&ring, chunk512, 512) == 1);
    assert(fuxi_audio_ring_read_tdm(&ring, tdm, 960, 0, 1) == 0);
    assert(ring.underruns == 0 && ring.primed == 0);
    assert(fuxi_audio_ring_used(&ring) == 512);

    /* 湊滿 >= 2880 frames 後開始穩態播放，首樣點淡入自 0 起避免爆音 */
    for (int k = 0; k < 5; k++) {
        assert(fuxi_audio_ring_write(&ring, chunk512, 512) == 1);
    }
    uint32_t nz = fuxi_audio_ring_read_tdm(&ring, tdm, 960, 0, 1);
    assert(ring.primed == 1 && ring.underruns == 0);
    assert(nz == 959); /* index 0 淡入為 0，其餘 959 筆非零 */
    assert(tdm[0] == 0u);
    /* 淡入結束後為 mono downmix (1000+2000)/2 = 1500 -> << 16 */
    assert(tdm[FUXI_AUDIO_FADE_FRAMES * 4u] == ((uint32_t)1500 << 16));
    assert(tdm[FUXI_AUDIO_FADE_FRAMES * 4u + 1u] == tdm[FUXI_AUDIO_FADE_FRAMES * 4u]);

    /* 穩態節拍模擬：CoreAudio 1024/2048 幀批次 vs TDM 960 幀 3 週期提前抽乾 -> 0 underruns, 0 overflows */
    static int16_t chunk2048[2048 * 2];
    for (int i = 0; i < 2048; i++) {
        chunk2048[i * 2] = 1000;
        chunk2048[i * 2 + 1] = 2000;
    }
    uint32_t produced = 3072;
    uint32_t consumed = 960;
    for (int step = 0; step < 120; step++) {
        if (step % 5 == 0) {
            while (produced - consumed < 2880) {
                fuxi_audio_ring_write(&ring, chunk2048, 2048);
                produced += 2048;
            }
        } else {
            while (produced - consumed < 2880) {
                fuxi_audio_ring_write(&ring, chunk2048, 1024);
                produced += 1024;
            }
        }
        nz = fuxi_audio_ring_read_tdm(&ring, tdm, 960, 0, 1);
        consumed += 960;
        assert(nz == 960);
    }
    assert(ring.underruns == 0 && ring.overflows == 0);
    assert(ring.max_used >= 2880 && ring.max_used <= FUXI_AUDIO_MAX_LATENCY_FRAMES);

    /* 欠載淡出並重設 primed=0 */
    ring.read_pos = ring.write_pos - 100u;
    nz = fuxi_audio_ring_read_tdm(&ring, tdm, 960, 0, 1);
    assert(ring.primed == 0 && ring.underruns == 1);
    assert(tdm[99 * 4] == 0u && tdm[100 * 4] == 0u);

    /* 短音效 active=0 即使未滿 1920 也立即播出不卡死 */
    memset(&ring, 0, sizeof(ring));
    assert(fuxi_audio_ring_write(&ring, chunk512, 64) == 1);
    nz = fuxi_audio_ring_read_tdm(&ring, tdm, 64, 1, 0);
    assert(nz > 0 && ring.underruns == 0);
    assert(tdm[40 * 4] == ((uint32_t)1000 << 16) && tdm[40 * 4 + 1] == ((uint32_t)2000 << 16));

    /* 200 ms 延遲上限裁切回 100 ms（防時脈漂移累積延遲） */
    memset(&ring, 0, sizeof(ring));
    for (int k = 0; k < 6; k++) {
        fuxi_audio_ring_write(&ring, chunk2048, 2048);
    }
    assert(ring.overflows >= 1);
    assert(fuxi_audio_ring_used(&ring) <= FUXI_AUDIO_MAX_LATENCY_FRAMES);
    assert(ring.max_used > FUXI_AUDIO_MAX_LATENCY_FRAMES);

    /* chime fade-in / mid / end */
    assert(fuxi_audio_chime_sample(0) == 0);
    assert(fuxi_audio_chime_sample(FUXI_AUDIO_CHIME_FRAMES) == 0);
    int32_t peak = 0;
    for (uint32_t f = 0; f < FUXI_AUDIO_CHIME_FRAMES; f++) {
        int32_t s = fuxi_audio_chime_sample(f);
        if (s < 0) s = -s;
        if (s > peak) peak = s;
    }
    assert(peak > 200000000 && peak < 280000000);
}

static void test_haptic_shape_v2(void) {
    FuxiShapeBurst b = {0};
#define AT(t, m) fuxi_shape2_classify(&b, MS(t) + MS(5000), (m))
    /* 輕觸：單一 6 ms 視窗 -> CLICK */
    assert(AT(0, 6) == FUXI_SHAPE_CLICK);
    /* 2 視窗中按：15 ms + 10 ms -> 只點一下、吞第二窗 */
    assert(AT(100, 15) == FUXI_SHAPE_CLICK);
    assert(AT(110, 10) == FUXI_SHAPE_SWALLOW);
    /* 連續觸感：0 ms 點擊、10 ms 吞掉（讓 15 ms RTP 播完）、20 ms 無縫交回 DIRECT */
    assert(AT(300, 15) == FUXI_SHAPE_CLICK);
    assert(AT(310, 15) == FUXI_SHAPE_SWALLOW);
    assert(AT(320, 15) == FUXI_SHAPE_PASS);
    assert(AT(330, 15) == FUXI_SHAPE_PASS);
    assert(AT(340, 0) == FUXI_SHAPE_PASS);

    /* 快速計時器滾輪：每 22 ms 一筆 15 ms 刻度，每一格都要觸發（mode 6 會吞掉） */
    b.active = 0;
    for (unsigned i = 0; i < 20; i++) {
        assert(AT(1000 + 22 * i, 15) == FUXI_SHAPE_CLICK);
    }
    /* 每 20 ms 一筆 6 ms 刻度，每一格都要觸發 */
    b.active = 0;
    for (unsigned i = 0; i < 20; i++) {
        assert(AT(2000 + 20 * i, 6) == FUXI_SHAPE_CLICK);
    }
    /* 長震動不改寫 */
    b.active = 0;
    assert(AT(3000, 400) == FUXI_SHAPE_PASS);
    assert(AT(3300, 400) == FUXI_SHAPE_PASS);
#undef AT

    /* VMAX 分級：單調、不超過 base、輕按與重按分明 */
    uint32_t base = 0x1c;
    uint32_t light = fuxi_shape2_vmax(12, base);
    uint32_t mid = fuxi_shape2_vmax(55, base);
    uint32_t heavy = fuxi_shape2_vmax(128, base);
    assert(light >= 10 && light < mid && mid < heavy && heavy == base);
    uint32_t prev = 0;
    for (unsigned lvl = 1; lvl <= 128; lvl++) {
        uint32_t v = fuxi_shape2_vmax(lvl, base);
        assert(v >= prev && v <= base);
        prev = v;
    }
}

static void test_haptic_shape_v3(void) {
    FuxiShapeBurst b = {0};
#define AT3(t, ms) fuxi_shape3_classify(&b, MS(t), (ms))
    /* 鍵盤／切換器短點按：6 ms -> CLICK */
    assert(AT3(0, 6) == FUXI_SHAPE_CLICK);
    /* 控制中心／桌面圖示長按（3 個連續視窗共 30 ms）：只擊發一次，不雙擊成彈簧感 */
    b.active = 0;
    assert(AT3(100, 15) == FUXI_SHAPE_CLICK);
    assert(AT3(110, 15) == FUXI_SHAPE_SWALLOW);
    assert(AT3(120, 10) == FUXI_SHAPE_SWALLOW);
    assert(AT3(130, 0) == FUXI_SHAPE_SWALLOW);
    /* 連續震動超過 38 ms 後交回 DIRECT_PLAY */
    b.active = 0;
    assert(AT3(300, 15) == FUXI_SHAPE_CLICK);
    assert(AT3(310, 15) == FUXI_SHAPE_SWALLOW);
    assert(AT3(320, 15) == FUXI_SHAPE_SWALLOW);
    assert(AT3(330, 15) == FUXI_SHAPE_SWALLOW);
    assert(AT3(340, 15) == FUXI_SHAPE_PASS);
    assert(AT3(350, 0) == FUXI_SHAPE_PASS);
    /* 快速計時器滾輪：每 22 ms 一筆 15 ms 刻度，每一格都要觸發 */
    b.active = 0;
    for (unsigned i = 0; i < 20; i++) {
        assert(AT3(1000 + 22 * i, 15) == FUXI_SHAPE_CLICK);
    }
    /* 每 20 ms 一筆 6 ms 刻度，每一格都要觸發 */
    b.active = 0;
    for (unsigned i = 0; i < 20; i++) {
        assert(AT3(2000 + 20 * i, 6) == FUXI_SHAPE_CLICK);
    }
    /* 長震動不改寫 */
    b.active = 0;
    assert(AT3(3000, 400) == FUXI_SHAPE_PASS);
#undef AT3

    /* 高壓 base=0x60 (4.80V)：輕點清晰可感 (>=0x34)、重按接近 base、單調遞增 */
    uint32_t base = 0x60;
    uint32_t light = fuxi_shape3_vmax(4, base);
    uint32_t mid = fuxi_shape3_vmax(55, base);
    uint32_t heavy = fuxi_shape3_vmax(128, base);
    assert(light >= 0x34 && light < mid && mid < heavy && heavy == base);
    uint32_t prev = 0;
    for (unsigned lvl = 1; lvl <= 128; lvl++) {
        uint32_t v = fuxi_shape3_vmax(lvl, base);
        assert(v >= prev && v <= base);
        prev = v;
    }
}

typedef struct {
    uint8_t regs[0x300];
    unsigned fifo_fill;
    unsigned hpwr_busy_reads;
    unsigned hpwr_reads;
    unsigned vmax_writes;
    unsigned rc_cal_bursts;
    unsigned rc_cal_1b_writes;
    unsigned fail_read_addr;
    uint8_t fault_reg;
    unsigned fault_clr_writes;
    uint8_t last_fault_clr;
    uint8_t play_hist[16];
    unsigned play_hist_n;
    uint8_t fifo_log[1024];
    unsigned fifo_log_n;
    unsigned write_seq_addr[64];
    uint8_t write_seq_val[64];
    unsigned write_seq_n;
} MockRtp;

static MockRtp *g_mock_rtp;

static int mock_owner(void *ctx, unsigned base, unsigned *owner) {
    (void)ctx; (void)base; *owner = 0; return 0;
}
static int mock_read(void *ctx, unsigned addr, uint8_t *val) {
    MockRtp *m = (MockRtp *)ctx;
    if (m->fail_read_addr && addr == m->fail_read_addr) return -1;
    if (addr == 0xf00b) {
        m->hpwr_reads++;
        if (m->hpwr_busy_reads) {
            m->hpwr_busy_reads--;
            *val = 1; /* BUSY */
            return 0;
        }
        *val = 0; /* DISABLED */
        return 0;
    }
    if (addr == 0xf00c) { *val = m->fault_reg; return 0; }
    if (addr < 0xf000 || addr >= 0xf300) return -1;
    *val = m->regs[addr - 0xf000];
    return 0;
}
static int mock_write_reg(void *ctx, unsigned addr, uint8_t val) {
    MockRtp *m = (MockRtp *)ctx;
    if (addr < 0xf000 || addr >= 0xf300) return -1;
    if (addr == 0xf066) {
        m->fault_clr_writes++;
        m->last_fault_clr = val;
        return 0;
    }
    m->regs[addr - 0xf000] = val;
    return 0;
}
static void mock_wait_us(void *ctx, unsigned us) {
    MockRtp *m = (MockRtp *)ctx;
    (void)us;
    if ((m->regs[0x04c] & 0x80) && m->fifo_fill) {
        m->fifo_fill = m->fifo_fill > 120 ? m->fifo_fill - 120 : 0;
    }
}
static int mock_rtp_write(unsigned addr, const uint8_t *data, unsigned size) {
    MockRtp *m = g_mock_rtp;
    if (addr == 0xf120) {
        for (unsigned i = 0; i < size && m->fifo_log_n < sizeof(m->fifo_log); i++) {
            m->fifo_log[m->fifo_log_n++] = data[i];
        }
        m->fifo_fill += size;
        return 0;
    }
    if (addr == 0xf05e && size == 2) {
        m->regs[0x05e] = data[0];
        m->regs[0x05f] = data[1];
        m->rc_cal_bursts++;
        if (m->write_seq_n < 64) {
            m->write_seq_addr[m->write_seq_n] = addr;
            m->write_seq_val[m->write_seq_n++] = data[0];
        }
        return 0;
    }
    if (addr == 0xf12d && size == 1 && data[0] == 1) { m->fifo_fill = 0; m->regs[0x12d] = 0; return 0; }
    if (size != 1 || addr < 0xf000 || addr >= 0xf300) return -1;
    if (addr == 0xf05e || addr == 0xf05f) m->rc_cal_1b_writes++;
    m->regs[addr - 0xf000] = data[0];
    if (m->write_seq_n < 64) {
        m->write_seq_addr[m->write_seq_n] = addr;
        m->write_seq_val[m->write_seq_n++] = data[0];
    }
    if (addr == 0xf04c && m->play_hist_n < 16) m->play_hist[m->play_hist_n++] = data[0];
    if (addr == 0xf048) { m->hpwr_busy_reads = 2; m->vmax_writes++; }
    return 0;
}
static int mock_rtp_status(uint8_t status[2]) {
    MockRtp *m = g_mock_rtp;
    status[0] = (uint8_t)((m->fifo_fill >> 8) & 0x0f);
    status[1] = (uint8_t)(m->fifo_fill & 0xff);
    return 0;
}

static void test_rtp_hv_short_wave(void) {
    assert(FUXI_RTP_TEST_VMAX_LIMIT == 0xa3);
    MockRtp m = {0};
    m.regs[0x046] = 0x80;
    m.regs[0x048] = 0x1c;
    m.regs[0x04c] = FUXI_VIBRATE_IDLE;
    m.regs[0x05e] = 0x01;
    m.regs[0x05f] = 0x22;
    m.regs[0x063] = 0x89;
    m.regs[0x072] = 0x04;
    m.regs[0x1a0] = 0x0f;
    m.regs[0x241] = 0xe0;
    g_mock_rtp = &m;
    FuxiHapticIo io = {&m, mock_owner, mock_read, mock_write_reg, mock_wait_us, 0};
    uint8_t wave[196];
    memset(wave, 0x40, 192);
    memset(wave + 192, 0, 4);
    FuxiRtpStats stats = {0};
    assert(fuxi_rtp_click(&io, mock_rtp_write, mock_rtp_status, wave, sizeof(wave), 0xa3, &stats) == 0);
    assert(stats.sent == 196 && stats.before_start == 196 && stats.last_fill == 0);
    assert(m.regs[0x048] == 0x1c && m.regs[0x063] == 0x89); /* restored */
    assert(m.regs[0x05e] == 0x01 && m.regs[0x05f] == 0x22 && m.regs[0x072] == 0x04);
    assert(m.rc_cal_bursts == 2 && m.rc_cal_1b_writes == 0);
    assert(m.hpwr_reads >= 6);     /* waited for HPWR settling on raise & restore */
    /* Verify order: 0xf063=0x09 -> 0xf05e 2B -> 0xf072=0x08 -> 0xf04c=0x80 -> ... -> 0xf072=0x04 -> 0xf05e 2B -> 0xf063=0x89 */
    int idx_ar_off = -1, idx_rc_set = -1, idx_cal_man = -1, idx_play = -1;
    int idx_cal_rst = -1, idx_rc_rst = -1, idx_ar_on = -1;
    for (unsigned i = 0; i < m.write_seq_n; i++) {
        unsigned a = m.write_seq_addr[i];
        uint8_t v = m.write_seq_val[i];
        if (a == 0xf063 && v == 0x09 && idx_ar_off < 0) idx_ar_off = (int)i;
        if (a == 0xf05e && v == 0x02 && idx_rc_set < 0) idx_rc_set = (int)i;
        if (a == 0xf072 && v == 0x08 && idx_cal_man < 0) idx_cal_man = (int)i;
        if (a == 0xf04c && v == 0x80 && idx_play < 0) idx_play = (int)i;
        if (a == 0xf072 && v == 0x04 && idx_play >= 0 && idx_cal_rst < 0) idx_cal_rst = (int)i;
        if (a == 0xf05e && v == 0x01 && idx_play >= 0 && idx_rc_rst < 0) idx_rc_rst = (int)i;
        if (a == 0xf063 && v == 0x89 && idx_play >= 0 && idx_ar_on < 0) idx_ar_on = (int)i;
    }
    assert(idx_ar_off >= 0 && idx_ar_off < idx_rc_set && idx_rc_set < idx_cal_man && idx_cal_man < idx_play);
    assert(idx_play < idx_cal_rst && idx_cal_rst < idx_rc_rst && idx_rc_rst < idx_ar_on);
    /* Over-limit VMAX rejected */
    assert(fuxi_rtp_click(&io, mock_rtp_write, mock_rtp_status, wave, sizeof(wave), 0xa4, &stats) == -1);
    /* Partial read failure on 0xf05f must roll back 0xf063 cleanly without writing 0xf05e/0xf05f */
    m.rc_cal_bursts = 0;
    m.fail_read_addr = 0xf05f;
    assert(fuxi_rtp_click(&io, mock_rtp_write, mock_rtp_status, wave, sizeof(wave), 0xa3, &stats) == -1);
    m.fail_read_addr = 0;
    assert(m.regs[0x063] == 0x89 && m.regs[0x05e] == 0x01 && m.regs[0x05f] == 0x22);
    assert(m.regs[0x072] == 0x04 && m.rc_cal_bursts == 0 && m.rc_cal_1b_writes == 0);
}

static void test_haptic_stream_v4(void) {
    uint8_t chunk[128];
    for (unsigned i = 0; i < sizeof(chunk); i++) chunk[i] = (uint8_t)(int8_t)((i & 1) ? 126 : -126);
    unsigned peak = 0;
    assert(fuxi_stream_validate_pcm(chunk, sizeof(chunk), &peak) == 0 && peak == 126);
    uint8_t bad[4] = {127, 0, 0, 0};
    assert(fuxi_stream_validate_pcm(bad, 4, &peak) == -1);
    assert(fuxi_stream_validate_pcm(chunk, 6, &peak) == -1);
    assert(fuxi_stream_validate_pcm(chunk, 0, &peak) == -1);

    MockRtp m = {0};
    m.regs[0x046] = 0x80;
    m.regs[0x048] = 0x1c;
    m.regs[0x04c] = FUXI_VIBRATE_IDLE;
    m.regs[0x05e] = 0x01;
    m.regs[0x05f] = 0x22;
    m.regs[0x063] = 0x89;
    m.regs[0x072] = 0x04;
    m.regs[0x124] = 0x05;
    m.regs[0x1a0] = 0x0f;
    m.regs[0x241] = 0xe0;
    g_mock_rtp = &m;
    FuxiHapticIo io = {&m, mock_owner, mock_read, mock_write_reg, mock_wait_us, 0};
    FuxiHapticStream st = {0};
    unsigned autores = 0;

    /* Cold open + seamless mid-burst refill (0 underruns, 1 refill) */
    assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, chunk, 128, 0xa3, 126, MS(0), &autores) == 0);
    assert(m.fault_clr_writes == 1 && m.last_fault_clr == 0x07);
    assert(st.configured && st.playing && m.regs[0x048] == 0xa3 && m.regs[0x04c] == 0x80);
    assert(m.regs[0x063] == 0x09 && m.regs[0x05e] == 0x02 && m.regs[0x05f] == 0x44 && m.regs[0x072] == 0x08);
    assert(m.rc_cal_bursts == 1 && m.rc_cal_1b_writes == 0);
    assert(m.fifo_fill == 128 && st.burst_chunks == 1 && st.burst_refills == 0);
    assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, chunk, 128, 0xa3, 120, MS(5), &autores) == 0);
    assert(m.fifo_fill == 256 && st.burst_chunks == 2 && st.burst_refills == 1 && st.burst_underruns == 0);
    assert(m.vmax_writes == 1);

    /* Drain polling until empty -> stays warm, but restores 0xf124/0xf072 per stock Android click.trace:144-145 */
    assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, MS(10)) == 1);
    m.fifo_fill = 0;
    assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, MS(20)) == 0);
    assert(st.configured && !st.playing && !st.play_armed && st.idle_since_ns == MS(20));
    assert(m.regs[0x048] == 0xa3 && m.regs[0x124] == 0x05 && m.regs[0x072] == 0x04);

    /* Warm burst simulates VMAX drift + transient BUSY: must wait cleanly, log vpre, and re-arm 0xf048/0xf124/0xf05e-5f/0xf072 */
    m.regs[0x048] = 0x1c;
    m.hpwr_busy_reads = 2;
    assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, chunk, 64, 0xa3, 64, MS(22), &autores) == 0);
    assert(st.playing && st.play_armed && st.last_vmax_pre == 0x1c);
    assert(m.regs[0x048] == 0xa3 && m.regs[0x124] == 0x0a && m.regs[0x05e] == 0x02 && m.regs[0x05f] == 0x44 && m.regs[0x072] == 0x08);
    assert(m.vmax_writes == 2 && m.rc_cal_bursts == 2 && m.rc_cal_1b_writes == 0 && m.fifo_fill == 64);

    /* Underrun recovery while playing: empty FIFO before next chunk -> stops cleanly & re-arms warm */
    m.fifo_fill = 0;
    assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, chunk, 64, 0xa3, 64, MS(25), &autores) == 0);
    assert(st.playing && st.play_armed && st.burst_underruns == 1 && m.fifo_fill == 64);
    assert(m.vmax_writes == 3 && m.rc_cal_bursts == 3);

    /* Fault during drain closes & restores all registers, VMAX and 0xf04c=IDLE */
    m.fault_reg = 0x04;
    assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, MS(30)) == -1);
    assert(!st.configured && !st.playing);
    assert(m.regs[0x048] == 0x1c && m.regs[0x04c] == FUXI_VIBRATE_IDLE);
    assert(m.regs[0x063] == 0x89 && m.regs[0x072] == 0x04);
    assert(m.regs[0x05e] == 0x01 && m.regs[0x05f] == 0x22 && m.regs[0x124] == 0x05);
    assert(m.rc_cal_bursts == 4 && m.rc_cal_1b_writes == 0);

    /* Clean idle close also restores everything */
    m.fault_reg = 0;
    assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, chunk, 64, 0xa3, 64, MS(35), &autores) == 0);
    assert(m.regs[0x063] == 0x09 && m.rc_cal_bursts == 5);
    m.fifo_fill = 0;
    assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, MS(40)) == 0);
    assert(m.regs[0x124] == 0x05 && m.regs[0x072] == 0x04);
    assert(fuxi_stream_close(&io, mock_rtp_write, &st) == 0);
    assert(!st.configured && !st.playing && m.regs[0x048] == 0x1c && m.regs[0x04c] == FUXI_VIBRATE_IDLE);
    assert(m.regs[0x063] == 0x89 && m.regs[0x05e] == 0x01 && m.regs[0x05f] == 0x22 && m.regs[0x072] == 0x04);
    assert(m.rc_cal_bursts == 6 && m.rc_cal_1b_writes == 0);
}

static double sim_lra_ring_energy(const uint8_t *drive, unsigned drive_len, unsigned settle_samples) {
    /* Real Xiaomi 13 AAC SLA0809 LRA: f0=175.1Hz (tlra=5710us), z0=0.033, fs=24kHz */
    const double alpha = 2.0 * 3.14159265358979323846 / (24000.0 * 0.005710);
    const double two_z0 = 2.0 * 0.033;
    double x = 0.0, y = 0.0, ring_peak = 0.0;
    for (unsigned i = 0; i < drive_len + settle_samples; i++) {
        double u = i < drive_len ? (double)(int8_t)drive[i] : 0.0;
        y += alpha * (u - x - two_z0 * y);
        x += alpha * y;
        if (i >= drive_len + 120u) { /* > 5 ms after drive ends */
            double ax = x < 0 ? -x : x;
            if (ax > ring_peak) ring_peak = ax;
        }
    }
    return ring_peak;
}

static void test_haptic_damp_and_tail(void) {
    /* Parameter decode & TLRA clamping */
    FuxiHapticDamp d;
    fuxi_damp_init(&d, 0, 5710);
    assert(d.mode == 0 && !d.no_tail && !d.hw_brake_req);
    fuxi_damp_init(&d, 0x42, 5710);
    assert(d.mode == 2 && d.gain_q8 == (8 + 4) * 32 && d.tlra_us == 5710 && d.alpha_q15 == 1502);
    fuxi_damp_init(&d, 0x384, 100);
    assert(d.mode == 4 && d.tlra_us == 5710 && d.hw_brake_req && d.no_tail && d.kick_q8 == 90 && d.gain_q8 == 512);

    /* TLRA register read (0xf05c/0xf05d -> 1142 * 5 = 5710 us) */
    MockRtp m = {0};
    m.regs[0x05c] = (uint8_t)((1142u >> 8) & 0x0fu);
    m.regs[0x05d] = (uint8_t)(1142u & 0xffu);
    FuxiHapticIo io = {&m, mock_owner, mock_read, mock_write_reg, mock_wait_us, 0};
    assert(fuxi_damp_read_tlra(&io) == 5710u);
    m.regs[0x05c] = 0; m.regs[0x05d] = 1;
    assert(fuxi_damp_read_tlra(&io) == 5710u);

    /* 1-cycle 175 Hz tap (137 samples -> 140B): compare raw LRA ringing vs damp=0x42 + brake tail */
    uint8_t raw_tap[140] = {0};
    for (unsigned i = 0; i < 137; i++) {
        double ph = 2.0 * 3.14159265358979323846 * i / 137.0;
        int v = (int)lrint(80.0 * sin(ph));
        raw_tap[i] = (uint8_t)(int8_t)v;
    }
    double raw_ring = sim_lra_ring_energy(raw_tap, sizeof(raw_tap), 1200);

    fuxi_damp_init(&d, 0x42, 5710);
    uint8_t damped_full[332] = {0};
    unsigned out_pk = 0, zc = 0, tail_pk = 0;
    fuxi_damp_process(&d, raw_tap, damped_full, sizeof(raw_tap), &out_pk, &zc);
    unsigned tail_len = fuxi_damp_tail(&d, damped_full + sizeof(raw_tap), FUXI_DAMP_TAIL_MAX, &tail_pk, &zc);
    assert(out_pk > 60 && out_pk <= 126 && tail_len >= 8 && (tail_len & 3u) == 0 && tail_pk > 0);
    double damped_ring = sim_lra_ring_energy(damped_full, sizeof(raw_tap) + tail_len, 1200);
    assert(damped_ring < raw_ring * 0.35);

    /* Anti-windup saturation check: 1000 samples at +126 must stay bounded */
    fuxi_damp_init(&d, 0x83, 5710);
    for (unsigned i = 0; i < 1000; i++) {
        int8_t s = fuxi_damp_step(&d, 126, NULL);
        assert(s >= -126 && s <= 126);
        assert(d.x >= -FUXI_DAMP_STATE_LIMIT && d.x <= FUXI_DAMP_STATE_LIMIT);
        assert(d.y >= -FUXI_DAMP_STATE_LIMIT && d.y <= FUXI_DAMP_STATE_LIMIT);
    }

    /* Stream integration: fast-poll return=2 at fill<=96, tail injection at fill<=14 (not 20) + HW brake */
    assert(FUXI_STREAM_TAIL_THRESHOLD == 14 && FUXI_STREAM_FAST_POLL_WATERMARK == 96);
    m = (MockRtp){0};
    m.regs[0x046] = 0x80; m.regs[0x048] = 0x1c; m.regs[0x04c] = FUXI_VIBRATE_IDLE;
    m.regs[0x063] = 0x89; m.regs[0x072] = 0x04; m.regs[0x1a0] = 0x0f; m.regs[0x241] = 0xe0;
    g_mock_rtp = &m;
    FuxiHapticStream st = {0};
    fuxi_damp_init(&st.damp, 0x142, 5710);
    st.hw_brake = 0x08;
    unsigned autores = 0;
    assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, raw_tap, sizeof(raw_tap), 0xa3, 80, MS(0), &autores) == 0);
    assert(m.regs[0x04c] == 0x88 && st.burst_raw_peak == 80 && st.burst_out_peak > 0);
    m.fifo_fill = 104;
    assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, MS(10)) == 1);
    assert(!st.tail_emitted && st.burst_tail_bytes == 0 && st.burst_tails == 0);
    m.fifo_fill = 60;
    assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, MS(12)) == 2);
    assert(!st.tail_emitted && st.burst_tail_bytes == 0 && st.burst_tails == 0);
    m.fifo_fill = 20;
    assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, MS(13)) == 2);
    assert(!st.tail_emitted && st.burst_tail_bytes == 0 && st.burst_tails == 0);
    m.fifo_fill = 14;
    assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, MS(14)) == 1);
    assert(st.tail_emitted && st.burst_tails == 1 && st.burst_tail_bytes >= 8 && m.fifo_fill == 14 + st.burst_tail_bytes);
    m.fifo_fill = 0;
    assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, MS(20)) == 0);
    assert(!st.playing && m.regs[0x04c] == 0x00);
    assert(m.play_hist_n >= 3 && m.play_hist[0] == 0x88 && m.play_hist[1] == 0x08 && m.play_hist[2] == 0x00);

    /* Multi-chunk 128B+128B regression: fill=12 at +4 ms (< 6 ms quiet) must NOT inject mid-tap tail; single tail after quiet */
    st.burst_chunks = st.burst_refills = st.burst_underruns = st.burst_bytes = 0;
    st.burst_raw_peak = st.burst_out_peak = st.burst_peak = st.burst_zc = st.burst_tail_bytes = st.burst_tails = 0;
    assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, raw_tap, 128, 0xa3, 80, MS(21), &autores) == 0);
    m.fifo_fill = 12;
    assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, MS(25)) == 2);
    assert(!st.tail_emitted && st.burst_tails == 0 && st.burst_tail_bytes == 0);
    assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, raw_tap, 128, 0xa3, 80, MS(26), &autores) == 0);
    assert(st.burst_chunks == 2 && st.burst_refills == 1 && st.burst_underruns == 0 && m.fifo_fill == 140);
    m.fifo_fill = 14;
    assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, MS(33)) == 1);
    assert(st.tail_emitted && st.burst_tails == 1 && st.burst_tail_bytes >= 8 && st.burst_tail_bytes <= FUXI_DAMP_TAIL_MAX);
    m.fifo_fill = 0;
    assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, MS(38)) == 0);
    assert(!st.playing && st.burst_tails == 1 && st.burst_tail_bytes <= FUXI_DAMP_TAIL_MAX);
    assert(fuxi_stream_close(&io, mock_rtp_write, &st) == 0);
    assert(m.regs[0x063] == 0x89 && m.regs[0x072] == 0x04);
}

static void sim_lra_metrics(const uint8_t *drive, unsigned drive_len, double f0_hz,
    double *pk_out, double *post_pct_out, double *re_pct_out) {
    const double w0 = 2.0 * 3.14159265358979323846 * f0_hz;
    const double dt = 1.0 / 24000.0;
    const double z0 = 0.033;
    double x = 0.0, v = 0.0, pk = 0.0, post = 0.0, min_after = 0.0, re = 0.0;
    unsigned tot = drive_len + 1200u;
    double env[2048];
    assert(tot <= 2048u);
    unsigned pk_idx = 0;
    for (unsigned i = 0; i < tot; i++) {
        double u = i < drive_len ? ((double)(int8_t)drive[i] / 126.0) : 0.0;
        double a = w0 * w0 * (u - x) - 2.0 * z0 * w0 * v;
        v += a * dt;
        x += v * dt;
        double e = hypot(x, v / w0);
        env[i] = e;
        if (e > pk) { pk = e; pk_idx = i; }
        if (i >= drive_len && e > post) post = e;
    }
    min_after = pk;
    for (unsigned i = pk_idx; i < tot; i++) {
        if (env[i] < min_after) min_after = env[i];
        else {
            double rise = (env[i] - min_after) / pk * 100.0;
            if (rise > re) re = rise;
        }
    }
    *pk_out = pk;
    *post_pct_out = pk > 0.0 ? (post / pk * 100.0) : 0.0;
    *re_pct_out = re;
}

static void test_haptic_mode5_and_mode6(void) {
    /* Mode 5 init & gain nibble */
    FuxiHapticDamp d;
    fuxi_damp_init(&d, 0x005, 5710);
    assert(d.mode == 5 && d.gain_q8 == 256 && d.pitch_step_q16 == 79155);
    fuxi_damp_init(&d, 0x0c5, 5710);
    assert(d.mode == 5 && d.gain_q8 == 320); /* 1.25x */
    fuxi_damp_init(&d, 0x106, 5710);
    assert(d.mode == 6 && d.gain_q8 == 288 && d.hw_brake_req);

    /* Construct realistic Apple D22-style Micro / Mini / Full taps */
    uint8_t micro_in[140] = {0}, mini_in[240] = {0}, full_in[320] = {0};
    for (unsigned i = 0; i < 120; i++) {
        micro_in[i] = (uint8_t)(int8_t)lrint(58.0 * sin(3.14159265358979323846 * i / 120.0));
    }
    for (unsigned i = 0; i < 220; i++) {
        mini_in[i] = (uint8_t)(int8_t)lrint(-92.0 * sin(2.0 * 3.14159265358979323846 * i / 220.0));
    }
    for (unsigned i = 0; i < 300; i++) {
        full_in[i] = (uint8_t)(int8_t)lrint(122.0 * sin(4.0 * 3.14159265358979323846 * i / 300.0));
    }

    uint8_t out[FUXI_DAMP_SYNTH_MAX];
    unsigned out_pk = 0, zc = 0, prof = 0;
    double pk = 0, post = 0, re = 0, pk3 = 0, post3 = 0, re3 = 0;

    /* Micro-2L: 140B, prof=1, positive polarity, crisp single-hump (<2% ring, 0% double-hump) */
    fuxi_damp_init(&d, 0x005, 5710);
    unsigned n_micro = fuxi_damp_synth_transient(&d, micro_in, sizeof(micro_in), out, sizeof(out), &out_pk, &zc, &prof);
    assert(prof == 1 && n_micro == 140 && out_pk >= 90 && out_pk <= 126 && (int8_t)out[30] > 0);
    sim_lra_metrics(out, n_micro, 175.13, &pk, &post, &re);
    sim_lra_metrics(out, n_micro, 178.13, &pk3, &post3, &re3);
    assert(pk > 1.0 && post < 2.0 && post3 < 7.0 && re < 0.5);

    /* Mini-4L: 276B, prof=2, preserves negative initial polarity, <5% ring, 0% double-hump */
    fuxi_damp_init(&d, 0x005, 5710);
    unsigned n_mini = fuxi_damp_synth_transient(&d, mini_in, sizeof(mini_in), out, sizeof(out), &out_pk, &zc, &prof);
    assert(prof == 2 && n_mini == 276 && out_pk >= 90 && out_pk <= 126 && (int8_t)out[30] < 0);
    sim_lra_metrics(out, n_mini, 175.13, &pk, &post, &re);
    sim_lra_metrics(out, n_mini, 172.13, &pk3, &post3, &re3);
    assert(pk > 2.0 && post < 5.0 && post3 < 12.0 && re < 0.5);

    /* Full-5L: 344B, prof=3, strong single-hump (>3.0), <5% ring, 0% double-hump */
    fuxi_damp_init(&d, 0x005, 5710);
    unsigned n_full = fuxi_damp_synth_transient(&d, full_in, sizeof(full_in), out, sizeof(out), &out_pk, &zc, &prof);
    assert(prof == 3 && n_full == 344 && out_pk >= 115 && out_pk <= 126 && (int8_t)out[30] > 0);
    sim_lra_metrics(out, n_full, 175.13, &pk, &post, &re);
    assert(pk > 3.1 && post < 5.0 && re < 0.5);

    /* Real iOS scroll tick (raw_peak=45, len=140) MUST hit light prof=1 (Micro-2L 140B, out_pk~92) */
    uint8_t scroll_in[140] = {0};
    for (unsigned i = 0; i < 120; i++) {
        scroll_in[i] = (uint8_t)(int8_t)lrint(45.0 * sin(2.0 * 3.14159265358979323846 * i / 120.0));
    }
    fuxi_damp_init(&d, 0x005, 5710);
    unsigned n_scr = fuxi_damp_synth_transient(&d, scroll_in, sizeof(scroll_in), out, sizeof(out), &out_pk, &zc, &prof);
    assert(prof == 1 && n_scr == 140 && out_pk >= 85 && out_pk <= 98);

    /* Real iOS Spotlight keyboard tap (new perceptual raw_peak=58, len=252) MUST hit crisp prof=1 (Micro-2L 140B) */
    uint8_t kbd_in[252] = {0};
    for (unsigned i = 0; i < 240; i++) {
        kbd_in[i] = (uint8_t)(int8_t)lrint(58.0 * sin(3.0 * 3.14159265358979323846 * i / 240.0));
    }
    fuxi_damp_init(&d, 0x005, 5710);
    unsigned n_kbd = fuxi_damp_synth_transient(&d, kbd_in, sizeof(kbd_in), out, sizeof(out), &out_pk, &zc, &prof);
    assert(prof == 1 && n_kbd == 140 && out_pk >= 96 && out_pk <= 106);

    /* Real iOS desktop long-press (raw_peak=119, zc=2, len=210) MUST hit prof=3 (Full-5L 344B) */
    uint8_t lp_in[220] = {0};
    for (unsigned i = 0; i < 210; i++) {
        lp_in[i] = (uint8_t)(int8_t)lrint(119.0 * sin(2.5 * 3.14159265358979323846 * i / 210.0));
    }
    fuxi_damp_init(&d, 0x005, 5710);
    unsigned n_lp = fuxi_damp_synth_transient(&d, lp_in, sizeof(lp_in), out, sizeof(out), &out_pk, &zc, &prof);
    assert(prof == 3 && n_lp == 344 && out_pk >= 115 && out_pk <= 126);

    /* Mode 5 strict monotonicity regression: Micro(34..71) < Mini(72..103) < Full(104..126) in physical LRA displacement */
    static const unsigned mono_peaks[] = { 34, 46, 58, 66, 71, 72, 80, 98, 103, 104, 118, 126 };
    static const unsigned mono_profs[] = {  1,  1,  1,  1,  1,  2,  2,  2,   2,   3,   3,   3 };
    double prev_pk = 0.0;
    for (unsigned idx = 0; idx < sizeof(mono_peaks) / sizeof(mono_peaks[0]); idx++) {
        uint8_t mono_in[200] = {0};
        for (unsigned s = 0; s < 180; s++) {
            mono_in[s] = (uint8_t)(int8_t)lrint((double)mono_peaks[idx] * sin(2.0 * 3.14159265358979323846 * s / 180.0));
        }
        fuxi_damp_init(&d, 0x005, 5710);
        unsigned nm = fuxi_damp_synth_transient(&d, mono_in, sizeof(mono_in), out, sizeof(out), &out_pk, &zc, &prof);
        assert(prof == mono_profs[idx] && nm >= 140);
        sim_lra_metrics(out, nm, 175.13, &pk, &post, &re);
        assert(pk > prev_pk && post < 5.0 && re < 0.5);
        prev_pk = pk;
    }

    /* Mode 5 stream integration: double-tap guard + zero extra tail + fresh tap + 5..6-chunk discrete absorb + >36 ms continuous handoff */
    MockRtp m = {0};
    m.regs[0x046] = 0x80; m.regs[0x048] = 0x1c; m.regs[0x04c] = FUXI_VIBRATE_IDLE;
    m.regs[0x063] = 0x89; m.regs[0x072] = 0x04; m.regs[0x1a0] = 0x0f; m.regs[0x241] = 0xe0;
    g_mock_rtp = &m;
    FuxiHapticIo io = {&m, mock_owner, mock_read, mock_write_reg, mock_wait_us, 0};
    FuxiHapticStream st = {0};
    fuxi_damp_init(&st.damp, 0x005, 5710);
    unsigned autores = 0;
    /* Chunk 1 (256B = 10.67 ms FullTap) plays Full-5L (344B) */
    assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, full_in, 256, 0xa3, 122, MS(0), &autores) == 0);
    assert(st.burst_profile == 3 && st.burst_bytes == 344 && st.synth_active && m.fifo_fill == 344);
    assert(m.fault_clr_writes == 1 && m.last_fault_clr == 0x07);
    /* Chunk 2 (128B tail arriving at +11 ms <= 10.67+8 ms) MUST be absorbed (rc=1, zero FIFO writes!) */
    assert(fuxi_stream_will_absorb(&st, 128, MS(11)) == 1);
    assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, full_in, 128, 0xa3, 40, MS(11), &autores) == 1);
    assert(st.burst_absorbed == 1 && m.fifo_fill == 344 && st.burst_chunks == 1);
    /* Poll drain near empty MUST NOT inject extra tail in Mode 5 synth */
    m.fifo_fill = 8;
    assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, MS(20)) == 2);
    assert(!st.tail_emitted && st.burst_tails == 0 && m.fifo_fill == 8);
    m.fifo_fill = 0;
    assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, MS(22)) == 0);
    assert(!st.playing && st.burst_tails == 0);

    /* Fast next tap at t=40 ms (> input_end+8 ms) triggers fresh Micro-2L tap immediately */
    assert(fuxi_stream_will_absorb(&st, 128, MS(40)) == 0);
    assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, micro_in, 128, 0xa3, 58, MS(40), &autores) == 0);
    assert(st.burst_profile == 1 && m.fifo_fill == 140 && st.burst_absorbed == 0);
    /* 5-chunk (640 = 26.67 ms) and 6-chunk (768 = 32 ms) discrete feedback stay absorbed; 7th (896 > 864) hands off to continuous! */
    assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, mini_in, 128, 0xa3, 92, MS(45), &autores) == 1);
    assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, mini_in, 128, 0xa3, 92, MS(50), &autores) == 1);
    assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, mini_in, 128, 0xa3, 92, MS(56), &autores) == 1);
    assert(st.synth_active && st.synth_input_samples == 512);
    assert(fuxi_stream_will_absorb(&st, 128, MS(61)) == 1);
    assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, mini_in, 128, 0xa3, 92, MS(61), &autores) == 1);
    assert(st.synth_active && st.synth_input_samples == 640 && m.fifo_fill == 140);
    assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, mini_in, 128, 0xa3, 92, MS(66), &autores) == 1);
    assert(st.synth_active && st.synth_input_samples == 768 && m.fifo_fill == 140);
    assert(fuxi_stream_will_absorb(&st, 128, MS(72)) == 0);
    assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, mini_in, 128, 0xa3, 92, MS(72), &autores) == 0);
    assert(!st.synth_active && st.synth_continuous && m.fifo_fill > 140);
    /* End-only deadbeat brake tail fires after >= 6 ms quiet in continuous mode */
    m.fifo_fill = 10;
    assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, MS(75)) == 2);
    assert(!st.tail_emitted);
    assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, MS(79)) == 1);
    assert(st.tail_emitted && st.burst_tails == 1 && st.burst_tail_bytes >= 8);
    m.fifo_fill = 0;
    assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, MS(86)) == 0);
    assert(fuxi_stream_close(&io, mock_rtp_write, &st) == 0);

    /* Mode 6: pitch-matched 145->175 Hz + proportional deadbeat brake suppresses ringing without slamming light taps to ±126 */
    fuxi_damp_init(&d, 0x006, 5710);
    uint8_t mode6_full[512] = {0};
    unsigned n6 = fuxi_damp_process_pitch(&d, mini_in, sizeof(mini_in), mode6_full, sizeof(mode6_full), &out_pk, &zc);
    assert(n6 > 0 && n6 < sizeof(mini_in) && (n6 & 3u) == 0 && out_pk >= 90);
    double pk_nb = 0, post_nb = 0, re_nb = 0;
    sim_lra_metrics(mode6_full, n6, 175.13, &pk_nb, &post_nb, &re_nb);
    unsigned tail6_lim = (out_pk * 9u) / 8u;
    if (tail6_lim > FUXI_DAMP_PCM_LIMIT) tail6_lim = FUXI_DAMP_PCM_LIMIT;
    unsigned tail6_pk = 0;
    unsigned tail6 = fuxi_damp_tail_limited(&d, mode6_full + n6, FUXI_DAMP_TAIL_MAX, tail6_lim, &tail6_pk, &zc);
    assert(tail6 >= 8 && (tail6 & 3u) == 0 && tail6_pk <= tail6_lim);
    sim_lra_metrics(mode6_full, n6 + tail6, 175.13, &pk, &post, &re);
    assert(pk > 2.0 && post < post_nb * 0.05 && post < 2.0);

    /* Light keyboard tap in Mode 6: brake tail MUST stay proportional (<= 74), never slamming ±126 */
    fuxi_damp_init(&d, 0x006, 5710);
    unsigned light_drv_pk = 0, light_tail_pk = 0;
    unsigned n6_light = fuxi_damp_process_pitch(&d, micro_in, sizeof(micro_in), mode6_full, sizeof(mode6_full), &light_drv_pk, &zc);
    unsigned light_lim = (light_drv_pk * 9u) / 8u;
    unsigned tail6_light = fuxi_damp_tail_limited(&d, mode6_full + n6_light, FUXI_DAMP_TAIL_MAX, light_lim, &light_tail_pk, &zc);
    assert(n6_light > 0 && tail6_light >= 8 && light_tail_pk <= light_lim && light_tail_pk <= 76);
    sim_lra_metrics(mode6_full, n6_light + tail6_light, 175.13, &pk, &post, &re);
    assert(pk > 0.7 && pk < 1.5 && post < 2.0);

    /* Mode 6 stream integration:
     * 1) Finished short 1-chunk tap (ends in silence -> last_input_quiet=1) fires single proportional tail at fill<=28.
     * 2) Run-79 regression: mid-burst 36B sliver + 128B active chunk MUST NOT emit mid-burst tails (tails=0 until final quiet chunk -> tails=1). */
    m = (MockRtp){0};
    m.regs[0x046] = 0x80; m.regs[0x048] = 0x1c; m.regs[0x04c] = FUXI_VIBRATE_IDLE;
    m.regs[0x063] = 0x89; m.regs[0x072] = 0x04; m.regs[0x1a0] = 0x0f; m.regs[0x241] = 0xe0;
    g_mock_rtp = &m;
    st = (FuxiHapticStream){0};
    fuxi_damp_init(&st.damp, 0x006, 5710);
    uint8_t short_done84[84] = {0}, short_done64[64] = {0}, sliver36[36] = {0}, active128[128] = {0};
    for (unsigned i = 0; i < 68; i++) short_done84[i] = (uint8_t)(int8_t)lrint(58.0 * sin(3.14159265358979323846 * i / 68.0));
    for (unsigned i = 0; i < 48; i++) short_done64[i] = (uint8_t)(int8_t)lrint(64.0 * sin(3.14159265358979323846 * i / 48.0));
    for (unsigned i = 0; i < 36; i++) sliver36[i] = (uint8_t)(int8_t)lrint(58.0 * sin(3.14159265358979323846 * (i + 10) / 70.0));
    for (unsigned i = 0; i < 128; i++) active128[i] = (uint8_t)(int8_t)lrint(66.0 * sin(3.14159265358979323846 * (i + 20) / 70.0));

    /* 84B finished keyboard tap (last 16B silent -> last_input_quiet=1): adaptive quiet = 0.5 ms, fires single proportional tail at fill<=48 */
    assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, short_done84, 84, 0xa3, 58, MS(0), &autores) == 0);
    assert(m.fifo_fill == 72 && st.last_push_fill == 72 && st.last_input_quiet == 1);
    m.fifo_fill = 52;
    assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, MS(1)) == 2);
    assert(!st.tail_emitted && st.burst_tails == 0);
    m.fifo_fill = 44;
    assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, 1200000ull) >= 1);
    assert(st.tail_emitted && st.burst_tails == 1 && st.burst_tail_bytes >= 8 && m.fifo_fill == 44 + st.burst_tail_bytes);
    assert(st.burst_out_peak <= (st.burst_drive_peak * 9u) / 8u && st.burst_out_peak < 90);
    m.fifo_fill = 0;
    assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, MS(8)) == 0);
    assert(!st.playing && st.burst_tails == 1 && st.burst_underruns == 0);

    /* 64B finished tap -> 56B (2.33 ms); adaptive quiet = 0 ms, fires cleanly at fill=44 (t=0.5 ms) */
    st.burst_chunks = st.burst_refills = st.burst_underruns = st.burst_bytes = st.burst_drive_peak = st.burst_out_peak = 0;
    st.burst_tail_bytes = st.burst_tails = 0;
    assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, short_done64, 64, 0xa3, 64, MS(20), &autores) == 0);
    assert(m.fifo_fill == 56 && st.last_push_fill == 56 && st.last_input_quiet == 1);
    m.fifo_fill = 44;
    assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, 20500000ull) >= 1);
    assert(st.tail_emitted && st.burst_tails == 1 && st.burst_tail_bytes >= 8 && m.fifo_fill == 44 + st.burst_tail_bytes);
    m.fifo_fill = 0;
    assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, MS(28)) == 0);
    assert(!st.playing && st.burst_tails == 1 && st.burst_underruns == 0);

    /* Run-79 regression: active 36B sliver -> active 128B chunk -> quiet 84B final chunk MUST emit ZERO mid-burst tails and 1 end tail */
    st.burst_chunks = st.burst_refills = st.burst_underruns = st.burst_bytes = st.burst_drive_peak = st.burst_out_peak = 0;
    st.burst_tail_bytes = st.burst_tails = 0;
    assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, sliver36, 36, 0xa3, 58, MS(40), &autores) == 0);
    assert(st.last_input_quiet == 0 && m.fifo_fill == 32);
    m.fifo_fill = 20;
    assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, MS(41)) == 2);
    assert(!st.tail_emitted && st.burst_tails == 0);
    assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, active128, 128, 0xa3, 66, MS(42), &autores) == 0);
    assert(st.last_input_quiet == 0 && st.burst_refills == 1);
    m.fifo_fill = 24;
    assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, MS(47)) == 2);
    assert(!st.tail_emitted && st.burst_tails == 0);
    assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, short_done84, 84, 0xa3, 58, MS(47), &autores) == 0);
    assert(st.last_input_quiet == 1 && st.burst_refills == 2);
    m.fifo_fill = 24;
    assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, MS(50)) >= 1);
    assert(st.tail_emitted && st.burst_tails == 1 && st.burst_tail_bytes >= 8 && st.burst_out_peak < 96);
    m.fifo_fill = 0;
    assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, MS(58)) == 0);
    assert(!st.playing && st.burst_tails == 1 && st.burst_underruns == 0);
    assert(fuxi_stream_close(&io, mock_rtp_write, &st) == 0);
}

static void sim_lra_split_metrics(const uint8_t *pcm, unsigned drv_len, unsigned total_len,
    double f0_hz, double *drv_pk, double *tail_pk, double *post_pk) {
    const double alpha = 2.0 * 3.14159265358979323846 * f0_hz / 24000.0;
    const double two_z0 = 2.0 * 0.033;
    double x = 0.0, y = 0.0;
    double dpk = 0.0, tpk = 0.0, ppk = 0.0;
    for (unsigned i = 0; i < total_len + 480u; i++) {
        double q = i < total_len ? (double)(int8_t)pcm[i] : 0.0;
        double a = q - x - two_z0 * y;
        y += alpha * a;
        x += alpha * y;
        double aa = a < 0 ? -a : a;
        if (i < drv_len) {
            if (aa > dpk) dpk = aa;
        } else if (i < total_len) {
            if (aa > tpk) tpk = aa;
        } else {
            if (aa > ppk) ppk = aa;
        }
    }
    *drv_pk = dpk;
    *tail_pk = tpk;
    *post_pk = ppk;
}

static void test_haptic_mode7(void) {
    FuxiHapticDamp d;
    fuxi_damp_init(&d, 0x0007u, 5710);
    assert(d.mode == 7 && !d.no_pitch && !d.no_tail && d.gain_q8 == 608);
    assert(d.two_z0_q15 == 2162 && d.c_r_q8 == 874 && d.c_x_q15 == -14347 && d.c_y_q15 == -32416);

    /* Bit 15 (crisp_high) and bit 14 (no_pitch) must NOT bleed into z0_field (bits 10..13) */
    fuxi_damp_init(&d, 0x8007u, 5710);
    assert(d.mode == 7 && !d.no_pitch && d.gain_q8 == 592 && d.two_z0_q15 == 2162);
    assert(d.c_r_q8 == 977 && d.c_x_q15 == -21319 && d.c_y_q15 == -39938);
    fuxi_damp_init(&d, 0x4007u, 5710);
    assert(d.mode == 7 && d.no_pitch && d.gain_q8 == 608 && d.two_z0_q15 == 2162);
    fuxi_damp_init(&d, 0x0047u, 5710);
    assert(d.mode == 7 && d.gain_q8 == 320 + 4 * 48);
    fuxi_damp_init(&d, 0x0c07u, 5710);
    assert(d.mode == 7 && d.two_z0_q15 == 2 * 3 * 328);

    /* Build Apple-like Micro (200 Hz, pk=57), Mini (150 Hz, pk=75), Full (150 Hz, pk=120) waveforms */
    uint8_t micro[160] = {0}, mini[236] = {0}, full[396] = {0};
    for (unsigned i = 0; i < 120; i++) {
        double t = (double)i / 24000.0;
        double env = 0.5 - 0.5 * cos(2.0 * 3.14159265358979323846 * i / 120.0);
        int v = (int)lrint(57.0 * env * sin(2.0 * 3.14159265358979323846 * 200.0 * t));
        micro[i] = (uint8_t)(int8_t)v;
    }
    for (unsigned i = 0; i < 160; i++) {
        double t = (double)i / 24000.0;
        double env = 0.5 - 0.5 * cos(2.0 * 3.14159265358979323846 * i / 160.0);
        int v = (int)lrint(75.0 * env * sin(2.0 * 3.14159265358979323846 * 150.0 * t));
        mini[i] = (uint8_t)(int8_t)v;
    }
    for (unsigned i = 0; i < 320; i++) {
        double t = (double)i / 24000.0;
        double env = 0.5 - 0.5 * cos(2.0 * 3.14159265358979323846 * i / 320.0);
        int v = (int)lrint(120.0 * env * sin(2.0 * 3.14159265358979323846 * 150.0 * t));
        full[i] = (uint8_t)(int8_t)v;
    }

    const uint32_t cfgs[2] = {0x0007u, 0x8007u};
    for (unsigned ci = 0; ci < 2; ci++) {
        double pks[3] = {0};
        const uint8_t *ins[3] = {micro, mini, full};
        const unsigned lens[3] = {sizeof(micro), sizeof(mini), sizeof(full)};
        for (unsigned k = 0; k < 3; k++) {
            uint8_t buf[640] = {0};
            unsigned out_pk = 0, tail_pk = 0, zc = 0;
            fuxi_damp_init(&d, cfgs[ci], 5710);
            unsigned n = fuxi_damp_process_mode7(&d, ins[k], lens[k], buf, sizeof(buf) - FUXI_DAMP_TAIL_MAX, &out_pk, &zc);
            assert(n >= 4 && (n & 3u) == 0 && out_pk > 0 && out_pk <= FUXI_DAMP_PCM_LIMIT);
            if (k < 2) assert(out_pk < FUXI_DAMP_PCM_LIMIT);
            unsigned tl = fuxi_damp_tail(&d, buf + n, FUXI_DAMP_TAIL_MAX, &tail_pk, &zc);
            double dpk = 0, tpk = 0, ppk = 0;
            sim_lra_split_metrics(buf, n, n + tl, 175.13, &dpk, &tpk, &ppk);
            /* Mode 7 must peak during the drive (center-peaked), not slam harder in the tail, and kill post-ringing < 2% */
            assert(dpk > tpk);
            assert(ppk < dpk * 0.02);
            pks[k] = dpk;
        }
        /* Preserve clear tactile hierarchy: Micro < Mini < Full */
        assert(pks[0] < pks[1] && pks[1] < pks[2]);
    }

    /* Stream integration with mode 7 + widened 48B tail threshold & 96B fast-poll watermark */
    MockRtp m = {0};
    m.regs[0x046] = 0x80; m.regs[0x048] = 0x1c; m.regs[0x04c] = FUXI_VIBRATE_IDLE;
    m.regs[0x063] = 0x89; m.regs[0x072] = 0x04; m.regs[0x1a0] = 0x0f; m.regs[0x241] = 0xe0;
    g_mock_rtp = &m;
    FuxiHapticIo io = {&m, mock_owner, mock_read, mock_write_reg, mock_wait_us, 0};
    FuxiHapticStream st = {0};
    fuxi_damp_init(&st.damp, 0x0007u, 5710);
    unsigned autores = 0;
    assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, full, 256, 0xa3, 120, MS(10), &autores) == 0);
    assert(st.playing && st.burst_chunks == 1 && !st.tail_emitted && !st.last_input_quiet);
    m.fifo_fill = 80;
    assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, MS(15)) == 2);
    assert(!st.tail_emitted && st.burst_tails == 0);
    assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, full + 256, sizeof(full) - 256, 0xa3, 120, MS(16), &autores) == 0);
    assert(st.playing && st.burst_chunks == 2 && st.burst_refills == 1 && st.last_input_quiet && !st.tail_emitted);
    /* At fill=44 (<= 48B pitch threshold) after adaptive quiet window, tail fires cleanly and returns fast-poll if <= 96B */
    m.fifo_fill = 44;
    int drain_rc = fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, MS(24));
    assert(drain_rc >= 1 && st.tail_emitted && st.burst_tails == 1);
    m.fifo_fill = 0;
    assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, MS(32)) == 0);
    assert(!st.playing && st.burst_underruns == 0);
    assert(fuxi_stream_close(&io, mock_rtp_write, &st) == 0);
}

static void test_haptic_mode8(void) {
    FuxiHapticDamp d;
    /* Default 0x0008: native 1:1 timing, ft=150 Hz, zd=0.28 -> zb=0.56, gain=512 */
    fuxi_damp_init(&d, 0x0008u, 5710);
    assert(d.mode == 8 && d.no_pitch && !d.no_tail && d.gain_q8 == 512);
    assert(d.two_z0_q15 == 2162 && d.c_r_q8 == 376 && d.c_x_q15 == 8729 && d.c_y_q15 == -13554);
    assert(d.c_x_tl_q15 == 8729 && d.c_y_tl_q15 == -29272);

    /* Crisp 0x8008: ft=160 Hz, zd=0.28 -> zb=0.58, gain=528 */
    fuxi_damp_init(&d, 0x8008u, 5710);
    assert(d.mode == 8 && d.no_pitch && d.gain_q8 == 528 && d.two_z0_q15 == 2162);
    assert(d.c_r_q8 == 441 && d.c_x_q15 == 5417 && d.c_y_q15 == -14602);
    assert(d.c_x_tl_q15 == 5417 && d.c_y_tl_q15 == -32564);

    /* Bit 14 (0x4008): native f0 braking tail (c_x_tl=0) while keeping 150 Hz drive */
    fuxi_damp_init(&d, 0x4008u, 5710);
    assert(d.mode == 8 && d.no_pitch && d.c_x_q15 == 8729 && d.c_x_tl_q15 == 0 && d.c_y_tl_q15 == -34538);

    /* Gain override (bits 4..7): 320 + n * 32 */
    fuxi_damp_init(&d, 0x0078u, 5710);
    assert(d.mode == 8 && d.gain_q8 == 320 + 7 * 32);

    /* Build native 1:1 Apple waveforms: Micro 200Hz, Mini 100Hz, Full 150Hz */
    uint8_t micro[160] = {0}, mini100[260] = {0}, full150[396] = {0};
    for (unsigned i = 0; i < 120; i++) {
        double t = (double)i / 24000.0;
        double env = 0.5 - 0.5 * cos(2.0 * 3.14159265358979323846 * i / 120.0);
        micro[i] = (uint8_t)(int8_t)(int)lrint(57.0 * env * sin(2.0 * 3.14159265358979323846 * 200.0 * t));
    }
    for (unsigned i = 0; i < 240; i++) {
        double t = (double)i / 24000.0;
        double env = 0.5 - 0.5 * cos(2.0 * 3.14159265358979323846 * i / 240.0);
        mini100[i] = (uint8_t)(int8_t)(int)lrint(69.0 * env * sin(2.0 * 3.14159265358979323846 * 100.0 * t));
    }
    for (unsigned i = 0; i < 320; i++) {
        double t = (double)i / 24000.0;
        double env = 0.5 - 0.5 * cos(2.0 * 3.14159265358979323846 * i / 320.0);
        full150[i] = (uint8_t)(int8_t)(int)lrint(120.0 * env * sin(2.0 * 3.14159265358979323846 * 150.0 * t));
    }

    const uint32_t cfgs[3] = {0x0008u, 0x8008u, 0x4008u};
    for (unsigned ci = 0; ci < 3; ci++) {
        double pks[3] = {0};
        const uint8_t *ins[3] = {micro, mini100, full150};
        const unsigned lens[3] = {sizeof(micro), sizeof(mini100), sizeof(full150)};
        for (unsigned k = 0; k < 3; k++) {
            uint8_t buf[640] = {0};
            unsigned out_pk = 0, tail_pk = 0, zc = 0;
            fuxi_damp_init(&d, cfgs[ci], 5710);
            unsigned n = fuxi_damp_process_mode7(&d, ins[k], lens[k], buf, sizeof(buf) - FUXI_DAMP_TAIL_MAX, &out_pk, &zc);
            /* Mode 8 must preserve exact 1:1 native length without pitch compression */
            assert(n == lens[k] && out_pk > 0 && out_pk <= FUXI_DAMP_PCM_LIMIT);
            unsigned tl = fuxi_damp_tail(&d, buf + n, FUXI_DAMP_TAIL_MAX, &tail_pk, &zc);
            assert(tl >= 4 && (tl & 3u) == 0);
            /* Smooth 24-sample tail ramp must prevent step spike at drive->tail seam */
            int seam_diff = (int)(int8_t)buf[n] - (int)(int8_t)buf[n - 1];
            int seam_jump = seam_diff < 0 ? -seam_diff : seam_diff;
            assert(seam_jump <= 10);
            double dpk = 0, tpk = 0, ppk = 0;
            sim_lra_split_metrics(buf, n, n + tl, 175.13, &dpk, &tpk, &ppk);
            assert(dpk > tpk);
            assert(ppk < dpk * 0.025);
            pks[k] = dpk;
        }
        assert(pks[0] < pks[2] && pks[1] < pks[2]);
    }

    /* Stream integration with mode 8 multi-chunk push + smooth ramped braking tail */
    MockRtp m = {0};
    m.regs[0x046] = 0x80; m.regs[0x048] = 0x1c; m.regs[0x04c] = FUXI_VIBRATE_IDLE;
    m.regs[0x063] = 0x89; m.regs[0x072] = 0x04; m.regs[0x1a0] = 0x0f; m.regs[0x241] = 0xe0;
    g_mock_rtp = &m;
    FuxiHapticIo io = {&m, mock_owner, mock_read, mock_write_reg, mock_wait_us, 0};
    FuxiHapticStream st = {0};
    fuxi_damp_init(&st.damp, 0x0008u, 5710);
    unsigned autores = 0;
    assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, full150, 256, 0xa3, 120, MS(10), &autores) == 0);
    assert(st.playing && st.burst_chunks == 1 && st.burst_bytes == 256 && !st.tail_emitted);
    m.fifo_fill = 96;
    assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, MS(16)) == 2);
    assert(!st.tail_emitted && st.burst_tails == 0);
    assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, full150 + 256, sizeof(full150) - 256, 0xa3, 120, MS(17), &autores) == 0);
    assert(st.playing && st.burst_chunks == 2 && st.burst_refills == 1 && st.burst_bytes == sizeof(full150));
    m.fifo_fill = 44;
    int drain_rc = fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, MS(26));
    assert(drain_rc >= 1 && st.tail_emitted && st.burst_tails == 1);
    m.fifo_fill = 0;
    assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, MS(35)) == 0);
    assert(!st.playing && st.burst_underruns == 0);
    assert(fuxi_stream_close(&io, mock_rtp_write, &st) == 0);
}

static void test_haptic_mode9(void) {
    FuxiHapticDamp d;
    /* Default 0x0009: 1.208x (145->f_LRA), ft=210 Hz, zd=0.38 -> zb=0.58 (f0), gain=456, soft-knee */
    fuxi_damp_init(&d, 0x0009u, 5710);
    assert(d.mode == 9 && !d.no_pitch && d.soft_knee && d.gain_q8 == 456);
    assert(d.two_z0_q15 == 2162 && d.c_x_tl_q15 == 0 && d.c_y_tl_q15 == -35848);
    assert(d.pitch_step_q16 > 78000u && d.pitch_step_q16 < 80000u);

    /* Warm 0x8009: 1.10x (159->f_LRA), ft=195 Hz, zd=0.34 -> zb=0.58 (f0), gain=430 */
    fuxi_damp_init(&d, 0x8009u, 5710);
    assert(d.mode == 9 && !d.no_pitch && d.soft_knee && d.gain_q8 == 430);
    assert(d.c_x_tl_q15 == 0 && d.c_y_tl_q15 == -35848);
    assert(d.pitch_step_q16 > 71500u && d.pitch_step_q16 < 73000u);

    /* Native 1:1 0x4009: no_pitch=1, ft=205 Hz, zd=0.36 -> zb=0.58 (f0), gain=422 */
    fuxi_damp_init(&d, 0x4009u, 5710);
    assert(d.mode == 9 && d.no_pitch && d.soft_knee && d.gain_q8 == 422 && d.pitch_step_q16 == 65536u);

    /* Gain override (bits 4..7): 320 + n * 24 */
    fuxi_damp_init(&d, 0x0059u, 5710);
    assert(d.mode == 9 && d.gain_q8 == 320 + 5 * 24);

    /* Soft-knee limiter: linear <= 108, smooth monotonic compression 109..126, symmetric */
    assert(fuxi_damp_soft_clamp(0, 126) == 0);
    assert(fuxi_damp_soft_clamp(108, 126) == 108 && fuxi_damp_soft_clamp(-108, 126) == -108);
    int32_t sk126 = fuxi_damp_soft_clamp(126, 126);
    int32_t sk180 = fuxi_damp_soft_clamp(180, 126);
    int32_t sk500 = fuxi_damp_soft_clamp(500, 126);
    assert(sk126 == 117 && sk180 > sk126 && sk180 < 126 && sk500 <= 126 && sk500 >= 124);
    assert(fuxi_damp_soft_clamp(-180, 126) == -sk180);

    /* Cliff zero-pad trimmer: strips 1..3 trailing zeros after |prev|>=8, keeps smooth endings & silence */
    uint8_t pad1[8] = {10, 20, 30, 40, 50, 58, 59, 0};
    uint8_t pad3[8] = {10, 20, 30, 40, 58, 0, 0, 0};
    uint8_t pad_edge8[8] = {10, 20, 18, 15, 12, 10, 8, 0};
    uint8_t pad_edge7[8] = {10, 20, 18, 15, 12, 10, 7, 0};
    uint8_t smooth_end[8] = {20, 15, 10, 6, 3, 1, 0, 0};
    uint8_t all_zero[8] = {0};
    assert(fuxi_damp_trim_chunk_pad(pad1, 8) == 7);
    assert(fuxi_damp_trim_chunk_pad(pad3, 8) == 5);
    assert(fuxi_damp_trim_chunk_pad(pad_edge8, 8) == 7);
    assert(fuxi_damp_trim_chunk_pad(pad_edge7, 8) == 8);
    assert(fuxi_damp_trim_chunk_pad(smooth_end, 8) == 8);
    assert(fuxi_damp_trim_chunk_pad(all_zero, 8) == 8);

    /* Multi-chunk App Switcher regression (MiniTap80Hz-like 80 Hz bipolar cycle, pk=75, 3 chunks [295+1pad, 128, 108]) */
    uint8_t sw_full[532] = {0};
    for (unsigned i = 0; i < 528; i++) {
        double t = (double)i / 24000.0;
        double env = 0.5 - 0.5 * cos(2.0 * 3.14159265358979323846 * i / 528.0);
        int v = (int)lrint(75.0 * env * cos(2.0 * 3.14159265358979323846 * 80.0 * t));
        sw_full[i] = (uint8_t)(int8_t)v;
    }
    /* Split right inside active peak at sample 295 with 1 trailing FuxiAudio zero-pad byte */
    uint8_t sw_c1[296] = {0}, sw_c2[128] = {0}, sw_c3[108] = {0};
    memcpy(sw_c1, sw_full, 295);
    sw_c1[295] = 0; /* simulated FuxiAudio.c 4B alignment zero-pad at mid-wave! */
    int s294 = (int)(int8_t)sw_c1[294];
    assert(s294 >= 40 || s294 <= -40);
    memcpy(sw_c2, sw_full + 295, 128);
    memcpy(sw_c3, sw_full + 423, 105);

    /* Also test scroll(24, 270Hz), kbd(57, 220Hz), long(120, 200Hz) across all 3 profiles */
    uint8_t scroll[144] = {0}, kbd[208] = {0}, lng[236] = {0};
    for (unsigned i = 0; i < 120; i++) {
        double env = 0.5 - 0.5 * cos(2.0 * 3.14159265358979323846 * i / 120.0);
        scroll[i] = (uint8_t)(int8_t)(int)lrint(24.0 * env);
    }
    for (unsigned i = 0; i < 180; i++) {
        double t = (double)i / 24000.0;
        double env = 0.5 - 0.5 * cos(2.0 * 3.14159265358979323846 * i / 180.0);
        kbd[i] = (uint8_t)(int8_t)(int)lrint(57.0 * env * sin(2.0 * 3.14159265358979323846 * 220.0 * t));
    }
    for (unsigned i = 0; i < 208; i++) {
        double t = (double)i / 24000.0;
        double env = 0.5 - 0.5 * cos(2.0 * 3.14159265358979323846 * i / 208.0);
        lng[i] = (uint8_t)(int8_t)(int)lrint(120.0 * env * sin(2.0 * 3.14159265358979323846 * 200.0 * t));
    }

    const uint32_t cfgs[3] = {0x0009u, 0x8009u, 0x4009u};
    for (unsigned ci = 0; ci < 3; ci++) {
        /* 1) 3-chunk switcher with mid-wave zero pad: ZERO hard clip, ZERO step spike (<=10), post < 1.5% */
        uint8_t out_sw[800] = {0};
        unsigned pk1 = 0, pk2 = 0, pk3 = 0, tpk_u = 0, zc = 0;
        fuxi_damp_init(&d, cfgs[ci], 5710);
        unsigned n1 = fuxi_damp_process_mode9(&d, sw_c1, sizeof(sw_c1), out_sw, 512, 1, &pk1, &zc);
        unsigned n2 = fuxi_damp_process_mode9(&d, sw_c2, sizeof(sw_c2), out_sw + n1, 512, 0, &pk2, &zc);
        unsigned n3 = fuxi_damp_process_mode9(&d, sw_c3, sizeof(sw_c3), out_sw + n1 + n2, 512, 0, &pk3, &zc);
        unsigned nd = n1 + n2 + n3;
        assert(nd > 300 && (n1 & 3u) == 0 && (n2 & 3u) == 0 && (n3 & 3u) == 0);
        unsigned tl = fuxi_damp_tail(&d, out_sw + nd, FUXI_DAMP_TAIL_MAX, &tpk_u, &zc);
        assert(tl >= 4 && (tl & 3u) == 0 && d.out_rem_len == 0);
        unsigned max_step = 0, hard_clips = 0;
        for (unsigned i = 0; i < nd + tl; i++) {
            int v = (int)(int8_t)out_sw[i];
            if (v >= 126 || v <= -126) hard_clips++;
            if (i > 0) {
                int diff = v - (int)(int8_t)out_sw[i - 1];
                unsigned ad = (unsigned)(diff < 0 ? -diff : diff);
                if (ad > max_step) max_step = ad;
            }
        }
        assert(hard_clips == 0 && max_step <= 10);
        double dpk = 0, tpk = 0, ppk = 0;
        sim_lra_split_metrics(out_sw, nd, nd + tl, 175.13, &dpk, &tpk, &ppk);
        assert(dpk > tpk * 2.0 && ppk < dpk * 0.02);

        /* 2) Single-chunk archetypes: scroll < kbd < long, zero hard clip, dpk > tpk, post < 2.0% */
        const uint8_t *ins[3] = {scroll, kbd, lng};
        const unsigned lens[3] = {sizeof(scroll), sizeof(kbd), sizeof(lng)};
        double pks[3] = {0};
        for (unsigned k = 0; k < 3; k++) {
            uint8_t buf[640] = {0};
            unsigned opk = 0, tlpk = 0;
            fuxi_damp_init(&d, cfgs[ci], 5710);
            unsigned n = fuxi_damp_process_mode9(&d, ins[k], lens[k], buf, 512, 1, &opk, &zc);
            unsigned t = fuxi_damp_tail(&d, buf + n, FUXI_DAMP_TAIL_MAX, &tlpk, &zc);
            assert(n >= 4 && (n & 3u) == 0 && t >= 4 && (t & 3u) == 0 && opk < 126);
            double dk = 0, tk = 0, pk_post = 0;
            sim_lra_split_metrics(buf, n, n + t, 175.13, &dk, &tk, &pk_post);
            assert(dk > tk && pk_post < dk * 0.02);
            pks[k] = dk;
        }
        assert(pks[0] < pks[1] && pks[1] < pks[2]);
    }

    /* Stream integration with mode 9 3-chunk push + out_rem flush + single tail + 0 underruns + telemetry */
    MockRtp m = {0};
    m.regs[0x046] = 0x80; m.regs[0x048] = 0x1c; m.regs[0x04c] = FUXI_VIBRATE_IDLE;
    m.regs[0x063] = 0x89; m.regs[0x072] = 0x04; m.regs[0x1a0] = 0x0f; m.regs[0x241] = 0xe0;
    g_mock_rtp = &m;
    FuxiHapticIo io = {&m, mock_owner, mock_read, mock_write_reg, mock_wait_us, 0};
    FuxiHapticStream st = {0};
    fuxi_damp_init(&st.damp, 0x0009u, 5710);
    unsigned autores = 0;
    assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, sw_c1, sizeof(sw_c1), 0xa3, 74, MS(10), &autores) == 0);
    assert(st.playing && st.burst_chunks == 1 && !st.tail_emitted && !st.last_input_quiet);
    assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, sw_c2, sizeof(sw_c2), 0xa3, 75, MS(18), &autores) == 0);
    assert(st.burst_chunks == 2 && st.burst_refills == 1 && !st.tail_emitted);
    assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, sw_c3, sizeof(sw_c3), 0xa3, 18, MS(23), &autores) == 0);
    assert(st.burst_chunks == 3 && st.burst_refills == 2 && st.last_input_quiet && !st.tail_emitted);
    assert(st.burst_drive_peak < 126);
    m.fifo_fill = 44;
    int drain_rc = fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, MS(32));
    assert(drain_rc >= 1 && st.tail_emitted && st.burst_tails == 1 && st.damp.out_rem_len == 0);
    assert(st.burst_clips == 0 && st.burst_max_step > 0 && st.burst_max_step <= 10);

    /* Back-to-back edge case: 4B sliver with trailing cliff zero (eff_len=3 -> 3 samples held in out_rem, emit_n=0)
     * arrives while previous tail is still draining (tail_emitted==1, fill>0) -> MUST reset tail_emitted & flush via next chunk! */
    m.fifo_fill = 36;
    uint8_t tiny4[4] = {2, 4, 8, 0};
    assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, tiny4, sizeof(tiny4), 0xa3, 8, MS(33), &autores) == 0);
    assert(!st.tail_emitted && st.damp.out_rem_len == 3);
    assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, kbd, sizeof(kbd), 0xa3, 57, MS(34), &autores) == 0);
    assert(!st.tail_emitted && st.last_input_quiet);
    m.fifo_fill = 40;
    assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, MS(42)) >= 1);
    assert(st.tail_emitted && st.burst_tails == 2 && st.damp.out_rem_len == 0 && st.burst_clips == 0);
    m.fifo_fill = 0;
    assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, MS(50)) == 0);
    assert(!st.playing && st.burst_underruns == 0 && !st.burst_has_prev);
    assert(fuxi_stream_close(&io, mock_rtp_write, &st) == 0);

    /* Control negative test: same buggy 3-chunk switcher under mode 8 MUST register clips>0 & cliff step>=40 in telemetry */
    st = (FuxiHapticStream){0};
    fuxi_damp_init(&st.damp, 0x0008u, 5710);
    assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, sw_c1, sizeof(sw_c1), 0xa3, 74, MS(60), &autores) == 0);
    assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, sw_c2, sizeof(sw_c2), 0xa3, 75, MS(68), &autores) == 0);
    assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, sw_c3, sizeof(sw_c3), 0xa3, 18, MS(73), &autores) == 0);
    m.fifo_fill = 44;
    assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, MS(84)) >= 1);
    assert(st.burst_clips > 0 && st.burst_max_step >= 40);
    m.fifo_fill = 0;
    assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, MS(92)) == 0);
    assert(fuxi_stream_close(&io, mock_rtp_write, &st) == 0);

    /* FullTap80Hz dynamic-gain seam regression (observed on real silicon burst=3/5/6:
     * FuxiAudio drops scale 52/5059 -> 65/8091 across chunk1->chunk2, creating a -52 -> -42 raw step).
     * Mode 9 first-order seam de-clicker MUST suppress output step from 24 down to <= 8 with 0 clips! */
    uint8_t vol_c1[292] = {0}, vol_c2[128] = {0};
    for (unsigned i = 0; i < 290; i++) {
        double t = (double)i / 24000.0;
        double v = -52.0 * sin(3.14159265358979323846 * i / 580.0);
        vol_c1[i] = (uint8_t)(int8_t)(int)lrint(v);
        (void)t;
    }
    /* vol_c1[289] == -52, trailing [290..291] == 0 (2B cliff pad); vol_c2 starts at -42 (22% gain drop) */
    for (unsigned i = 0; i < 128; i++) {
        double v = -42.0 * cos(3.14159265358979323846 * i / 256.0);
        vol_c2[i] = (uint8_t)(int8_t)(int)lrint(v);
    }
    st = (FuxiHapticStream){0};
    fuxi_damp_init(&st.damp, 0x0009u, 5710);
    assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, vol_c1, sizeof(vol_c1), 0xa3, 52, MS(110), &autores) == 0);
    assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, vol_c2, sizeof(vol_c2), 0xa3, 65, MS(118), &autores) == 0);
    m.fifo_fill = 44;
    assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, MS(126)) >= 1);
    assert(st.burst_clips == 0 && st.burst_max_step <= 8 && st.burst_tails == 1);
    m.fifo_fill = 0;
    assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, MS(134)) == 0);
    assert(fuxi_stream_close(&io, mock_rtp_write, &st) == 0);

    /* Mode 9 v2 four-profile verification:
     * 1) UISwitch (MicroTap100Hz unipolar pk=96, 232 active samples ~100 Hz) -> prof=1 (UNI), softer than AppSwitch, 0 clips */
    uint8_t ui_sw[316] = {0};
    for (unsigned i = 0; i < 232; i++) {
        double env = 0.5 - 0.5 * cos(2.0 * 3.14159265358979323846 * i / 232.0);
        ui_sw[i] = (uint8_t)(int8_t)(int)lrint(96.0 * env);
    }
    {
        uint8_t buf[640] = {0};
        unsigned opk = 0, tlpk = 0, zc = 0;
        fuxi_damp_init(&d, 0x0009u, 5710);
        unsigned n = fuxi_damp_process_mode9(&d, ui_sw, sizeof(ui_sw), buf, 512, 1, &opk, &zc);
        unsigned t = fuxi_damp_tail(&d, buf + n, FUXI_DAMP_TAIL_MAX, &tlpk, &zc);
        assert(d.m9_prof == FUXI_DAMP_M9_PROF_UNI);
        assert(n >= 260 && (n & 3u) == 0 && t >= 4 && opk >= 110 && opk <= 122 && opk < FUXI_DAMP_PCM_LIMIT);
        double dk = 0, tk = 0, pk_post = 0;
        sim_lra_split_metrics(buf, n, n + t, 175.13, &dk, &tk, &pk_post);
        assert(dk > tk * 2.5 && pk_post < dk * 0.02);
        /* Light unipolar Scroll(24) MUST stay prof=0 (STD) */
        fuxi_damp_init(&d, 0x0009u, 5710);
        (void)fuxi_damp_process_mode9(&d, scroll, sizeof(scroll), buf, 512, 1, &opk, &zc);
        assert(d.m9_prof == FUXI_DAMP_M9_PROF_STD);
    }

    /* 2) LongPress (MiniTap200Hz bipolar pk=120 quiet-ending) -> prof=2 (LP), crisp deadbeat tail ratio < 0.35 */
    {
        uint8_t buf[640] = {0};
        unsigned opk = 0, tlpk = 0, zc = 0;
        fuxi_damp_init(&d, 0x0009u, 5710);
        unsigned n = fuxi_damp_process_mode9(&d, lng, sizeof(lng), buf, 512, 1, &opk, &zc);
        unsigned t = fuxi_damp_tail(&d, buf + n, FUXI_DAMP_TAIL_MAX, &tlpk, &zc);
        assert(d.m9_prof == FUXI_DAMP_M9_PROF_LP);
        assert(n >= 160 && t >= 4 && t <= 148 && opk < 126);
        double dk = 0, tk = 0, pk_post = 0;
        sim_lra_split_metrics(buf, n, n + t, 175.13, &dk, &tk, &pk_post);
        assert(tk < dk * 0.35 && pk_post < dk * 0.02);
    }

    /* 3) Incoming Call continuous ringtone (166 chunks @ 141 Hz & on-resonance 170.6 Hz, pk=126, abrupt peak cut):
     *    MUST lock prof=3 (SUS), 1:1 rate -> 0 underruns across 166 chunks, thermal cap <= 43, safe x_pk <= 250,
     *    abrupt mid-peak answer tail max_step <= 10! */
    const double ring_freqs[2] = {141.0, 170.6};
    for (unsigned fi = 0; fi < 2; fi++) {
        const double freq = ring_freqs[fi];
        st = (FuxiHapticStream){0};
        fuxi_damp_init(&st.damp, 0x0009u, 5710);
        double lx = 0.0, ly = 0.0, x_pk_real = 0.0;
        const double w0 = 2.0 * 3.14159265358979323846 * 170.6;
        const double dt = 1.0 / 24000.0;
        unsigned sample_idx = 0;
        uint64_t t_ns = MS(200);
        uint8_t c0[348];
        for (unsigned i = 0; i < 348; i++, sample_idx++) {
            double env = sample_idx < 48 ? (double)sample_idx / 48.0 : 1.0;
            c0[i] = (uint8_t)(int8_t)(int)lrint(126.0 * env * sin(2.0 * 3.14159265358979323846 * freq * sample_idx / 24000.0));
        }
        assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, c0, sizeof(c0), 0xa3, 126, t_ns, &autores) == 0);
        assert(st.burst_profile == FUXI_DAMP_M9_PROF_SUS && st.burst_bytes == 348 && m.fifo_fill == 348);
        assert(st.burst_drive_peak <= (unsigned)FUXI_DAMP_M9_SUS_LIMIT);
        /* Advance real-plant observer over chunk 0 output via independent copy */
        FuxiHapticDamp d_check;
        fuxi_damp_init(&d_check, 0x0009u, 5710);
        uint8_t chk_out[512];
        unsigned chk_pk = 0, chk_zc = 0;
        unsigned chk_n = fuxi_damp_process_mode9(&d_check, c0, sizeof(c0), chk_out, sizeof(chk_out), 1, &chk_pk, &chk_zc);
        for (unsigned j = 0; j < chk_n; j++) {
            double a = (double)(int8_t)chk_out[j] - lx - 2.0 * 0.033 * ly;
            ly += w0 * dt * a; lx += w0 * dt * ly;
            if (fabs(lx) > x_pk_real) x_pk_real = fabs(lx);
        }
        for (unsigned ci = 1; ci < 166; ci++) {
            t_ns += 5333333ull;
            assert(m.fifo_fill >= 128);
            m.fifo_fill -= 128; /* exact 24 kHz drain over 5.333 ms */
            assert(m.fifo_fill >= 200); /* zero starvation risk */
            uint8_t ck[128];
            for (unsigned i = 0; i < 128; i++, sample_idx++) {
                ck[i] = (uint8_t)(int8_t)(int)lrint(126.0 * sin(2.0 * 3.14159265358979323846 * freq * sample_idx / 24000.0));
            }
            unsigned prev_bytes = st.burst_bytes;
            assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, ck, sizeof(ck), 0xa3, 126, t_ns, &autores) == 0);
            assert(st.burst_bytes - prev_bytes == 128u); /* exact 1:1 lock */
            assert(st.burst_underruns == 0 && !st.tail_emitted);
            chk_n = fuxi_damp_process_mode9(&d_check, ck, sizeof(ck), chk_out, sizeof(chk_out), 0, &chk_pk, &chk_zc);
            for (unsigned j = 0; j < chk_n; j++) {
                double a = (double)(int8_t)chk_out[j] - lx - 2.0 * 0.033 * ly;
                ly += w0 * dt * a; lx += w0 * dt * ly;
                if (fabs(lx) > x_pk_real) x_pk_real = fabs(lx);
            }
        }
        /* Abrupt call answer stop: wait quiet_ns and drain low watermark -> single smooth tail, step <= 10! */
        m.fifo_fill = 44;
        assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, t_ns + MS(8)) >= 1);
        assert(st.tail_emitted && st.burst_tails == 1 && st.burst_clips == 0);
        assert(st.burst_drive_peak <= (unsigned)FUXI_DAMP_M9_SUS_LIMIT);
        assert(st.burst_max_step <= 10u);
        assert(x_pk_real >= 100.0 && x_pk_real <= 250.0); /* full buzz, strictly zero end-stop banging! */
        /* Back-to-back STD keyboard click arriving right after SUS tail MUST reset profile back to 0 (STD)! */
        m.fifo_fill = 36;
        assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, kbd, sizeof(kbd), 0xa3, 57, t_ns + MS(10), &autores) == 0);
        assert(st.burst_profile == FUXI_DAMP_M9_PROF_STD && !st.damp.m9_sus_active && st.damp.m9_sus_prog == 0);
        m.fifo_fill = 0;
        assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, t_ns + MS(25)) == 0);
        assert(fuxi_stream_close(&io, mock_rtp_write, &st) == 0);
    }

    /* 4) Mid-amplitude continuous fallback (pk=80 > 36 ms = 864 samples) -> smooth crossfade into SUS 1:1, 0 underruns */
    {
        st = (FuxiHapticStream){0};
        fuxi_damp_init(&st.damp, 0x0009u, 5710);
        unsigned sidx = 0;
        uint64_t t_ns = MS(400);
        uint8_t c0[348];
        for (unsigned i = 0; i < 348; i++, sidx++) {
            c0[i] = (uint8_t)(int8_t)(int)lrint(80.0 * sin(2.0 * 3.14159265358979323846 * 141.0 * sidx / 24000.0));
        }
        assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, c0, sizeof(c0), 0xa3, 80, t_ns, &autores) == 0);
        assert(st.burst_profile == FUXI_DAMP_M9_PROF_STD);
        for (unsigned ci = 1; ci < 20; ci++) {
            t_ns += 5333333ull;
            assert(m.fifo_fill > 128);
            m.fifo_fill -= 128;
            uint8_t ck[128];
            for (unsigned i = 0; i < 128; i++, sidx++) {
                ck[i] = (uint8_t)(int8_t)(int)lrint(80.0 * sin(2.0 * 3.14159265358979323846 * 141.0 * sidx / 24000.0));
            }
            assert(fuxi_stream_push(&io, mock_rtp_write, mock_rtp_status, &st, ck, sizeof(ck), 0xa3, 80, t_ns, &autores) == 0);
            assert(st.burst_underruns == 0);
        }
        assert(st.burst_profile == FUXI_DAMP_M9_PROF_SUS && st.damp.m9_sus_prog == FUXI_DAMP_M9_SUS_RAMP);
        m.fifo_fill = 44;
        assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, t_ns + MS(8)) >= 1);
        assert(st.burst_tails == 1 && st.burst_clips == 0 && st.burst_max_step <= 14u);
        m.fifo_fill = 0;
        assert(fuxi_stream_poll_drain(&io, mock_rtp_write, mock_rtp_status, &st, t_ns + MS(20)) == 0);
        assert(fuxi_stream_close(&io, mock_rtp_write, &st) == 0);
    }
}

typedef struct {
    double x175, y175, xpk175;
    double x170, y170, xpk170;
} TestLraPlant;

static void test_lra_step(TestLraPlant *p, int8_t u) {
    const double dt = 1.0 / 24000.0;
    const double z0 = 0.033;
    const double w175 = 2.0 * 3.14159265358979323846 * 175.13;
    const double w170 = 2.0 * 3.14159265358979323846 * 170.6;
    double a1 = (double)u - p->x175 - 2.0 * z0 * p->y175;
    p->y175 += w175 * dt * a1;
    p->x175 += w175 * dt * p->y175;
    if (fabs(p->x175) > p->xpk175) p->xpk175 = fabs(p->x175);
    double a2 = (double)u - p->x170 - 2.0 * z0 * p->y170;
    p->y170 += w170 * dt * a2;
    p->x170 += w170 * dt * p->y170;
    if (fabs(p->x170) > p->xpk170) p->xpk170 = fabs(p->x170);
}

static void test_lra_consume_fifo_log(MockRtp *m, TestLraPlant *p) {
    for (unsigned i = 0; i < m->fifo_log_n; i++) {
        test_lra_step(p, (int8_t)m->fifo_log[i]);
    }
    m->fifo_log_n = 0;
}

static void test_reset_stream_burst_stats(FuxiHapticStream *st) {
    st->burst_chunks = st->burst_bytes = st->burst_peak = 0;
    st->burst_raw_peak = st->burst_drive_peak = st->burst_out_peak = st->burst_zc = 0;
    st->burst_tail_bytes = st->burst_tails = st->burst_refills = st->burst_underruns = 0;
    st->burst_profile = st->burst_repeat_level = st->burst_gap_ms = st->burst_absorbed = 0;
    st->burst_clips = st->burst_max_step = 0;
    st->burst_has_prev = 0;
    st->burst_prev_sample = 0;
}

static void test_make_rampsine_pcm8(uint8_t *buf, unsigned n24, double freq, double att_ms, double rel_ms, double pk8) {
    unsigned att = (unsigned)lrint(att_ms * 24.0);
    unsigned rel = (unsigned)lrint(rel_ms * 24.0);
    for (unsigned i = 0; i < n24; i++) {
        double env = 1.0;
        if (att && i < att) env = (double)i / (double)att;
        if (rel && i >= n24 - rel) {
            double r = (double)(n24 - i) / (double)rel;
            if (r < env) env = r;
        }
        int s = (int)lrint(pk8 * env * sin(2.0 * 3.14159265358979323846 * freq * (double)i / 24000.0));
        if (s > 126) s = 126;
        if (s < -126) s = -126;
        buf[i] = (uint8_t)(int8_t)s;
    }
}

static void test_push_pcm8_burst(const FuxiHapticIo *io, MockRtp *m, FuxiHapticStream *st,
                                 TestLraPlant *pl, const uint8_t *pcm, unsigned n24,
                                 unsigned first_len, uint64_t *t_ns, int drain_to_stop,
                                 unsigned *autores) {
    if (!st->playing) test_reset_stream_burst_stats(st);
    unsigned pos = 0, ci = 0;
    while (pos < n24) {
        unsigned clen = (ci == 0) ? first_len : 128u;
        if (pos + clen > n24) clen = n24 - pos;
        uint8_t ck[376] = {0};
        unsigned pk = 0;
        for (unsigned i = 0; i < clen; i++) {
            ck[i] = pcm[pos + i];
            int v = (int)(int8_t)ck[i];
            unsigned mag = (unsigned)(v < 0 ? -v : v);
            if (mag > pk) pk = mag;
        }
        unsigned pad = (clen + 3u) & ~3u;
        if (ci > 0) {
            *t_ns += 5333333ull;
            m->fifo_fill = m->fifo_fill > 128u ? (m->fifo_fill - 128u) : 16u;
        }
        assert(fuxi_stream_push(io, mock_rtp_write, mock_rtp_status, st, ck, pad, 0xa3, pk, *t_ns, autores) == 0);
        test_lra_consume_fifo_log(m, pl);
        pos += clen;
        ci++;
    }
    m->fifo_fill = 44u;
    *t_ns += MS(8);
    assert(fuxi_stream_poll_drain(io, mock_rtp_write, mock_rtp_status, st, *t_ns) >= 1);
    test_lra_consume_fifo_log(m, pl);
    if (drain_to_stop) {
        m->fifo_fill = 0u;
        *t_ns += MS(8);
        assert(fuxi_stream_poll_drain(io, mock_rtp_write, mock_rtp_status, st, *t_ns) == 0);
        assert(!st->playing);
    }
}

/* Mode 9 v2.2 回歸：消除交響樂／斷音起震共振撞底與連發強拍爆震，同時嚴格保留既有單發／雙擊 UI 手感。 */
static void test_haptic_mode9_symphony_staccato_and_repeat(void) {
    MockRtp m = {0};
    m.regs[0x046] = 0x80; m.regs[0x048] = 0x1c; m.regs[0x04c] = FUXI_VIBRATE_IDLE;
    m.regs[0x063] = 0x89; m.regs[0x072] = 0x04; m.regs[0x1a0] = 0x0f; m.regs[0x241] = 0xe0;
    g_mock_rtp = &m;
    FuxiHapticIo io = {&m, mock_owner, mock_read, mock_write_reg, mock_wait_us, 0};
    unsigned autores = 0;

    /* 基礎測試波形：LP 短強拍 (MiniTap200Hz raw_pk=120)、ResCalibration (145Hz 13ms raw_pk=120)、
     * 上滑後台 (raw_pk=75)、設定開關 UNI (raw_pk=96)、鍵盤 (raw_pk=57) */
    uint8_t tap_lp[236] = {0}, res_cal[320] = {0}, ui_sw[316] = {0}, kbd[208] = {0}, app_sw[528] = {0};
    for (unsigned i = 0; i < 208; i++) {
        double env = 0.5 - 0.5 * cos(2.0 * 3.14159265358979323846 * i / 208.0);
        tap_lp[i] = (uint8_t)(int8_t)(int)lrint(120.0 * env * sin(2.0 * 3.14159265358979323846 * 200.0 * i / 24000.0));
    }
    for (unsigned i = 0; i < 280; i++) {
        double env = 0.5 - 0.5 * cos(2.0 * 3.14159265358979323846 * i / 280.0);
        res_cal[i] = (uint8_t)(int8_t)(int)lrint(120.0 * env * sin(2.0 * 3.14159265358979323846 * 145.0 * i / 24000.0));
    }
    for (unsigned i = 0; i < 232; i++) {
        double env = 0.5 - 0.5 * cos(2.0 * 3.14159265358979323846 * i / 232.0);
        ui_sw[i] = (uint8_t)(int8_t)(int)lrint(96.0 * env);
    }
    for (unsigned i = 0; i < 180; i++) {
        double env = 0.5 - 0.5 * cos(2.0 * 3.14159265358979323846 * i / 180.0);
        kbd[i] = (uint8_t)(int8_t)(int)lrint(57.0 * env * sin(2.0 * 3.14159265358979323846 * 220.0 * i / 24000.0));
    }
    for (unsigned i = 0; i < 528; i++) {
        double env = 0.5 - 0.5 * cos(2.0 * 3.14159265358979323846 * i / 528.0);
        app_sw[i] = (uint8_t)(int8_t)(int)lrint(75.0 * env * sin(2.0 * 3.14159265358979323846 * 80.0 * i / 24000.0));
    }

    /* 1) 高頻連發短脈衝（6x raw_pk=120 @ 82ms，斷音短拍／Radar 連擊）：
     *    首發全幅，第 2 發降 rep=2 (<=72)，第 3..6 發收斂至 rep=3 (<=56, x_pk<=160)，靜置 >1s 恢復全幅。 */
    {
        FuxiHapticStream st = {0};
        fuxi_damp_init(&st.damp, 0x0009u, 5710);
        uint64_t t_ns = MS(1000);
        for (unsigned k = 0; k < 6; k++) {
            if (k > 0) t_ns += MS(82);
            TestLraPlant pl = {0};
            test_push_pcm8_burst(&io, &m, &st, &pl, tap_lp, sizeof(tap_lp), sizeof(tap_lp), &t_ns, 1, &autores);
            assert(st.burst_profile == FUXI_DAMP_M9_PROF_LP && st.burst_clips == 0 && st.burst_underruns == 0);
            if (k == 0) {
                assert(st.burst_repeat_level == 0 && st.burst_drive_peak >= 118u);
            } else if (k == 1) {
                assert(st.burst_repeat_level == 2 && st.burst_drive_peak <= (unsigned)FUXI_DAMP_M9_LP_REP2_LIMIT);
                assert(pl.xpk175 <= 185.0 && pl.xpk170 <= 185.0);
            } else {
                assert(st.burst_repeat_level == 3 && st.burst_drive_peak <= (unsigned)FUXI_DAMP_M9_LP_REP3_LIMIT);
                assert(st.burst_max_step <= 12u && pl.xpk175 <= 160.0 && pl.xpk170 <= 160.0);
            }
        }
        /* 靜置 1100 ms 後再單次長按：連發記憶自動清零，恢復單發滿幅手感 */
        t_ns += MS(1100);
        TestLraPlant pl_rec = {0};
        test_push_pcm8_burst(&io, &m, &st, &pl_rec, tap_lp, sizeof(tap_lp), sizeof(tap_lp), &t_ns, 1, &autores);
        assert(st.burst_profile == FUXI_DAMP_M9_PROF_LP && st.burst_repeat_level == 0 && st.burst_drive_peak >= 118u);
        assert(fuxi_stream_close(&io, mock_rtp_write, &st) == 0);
    }

    /* 2) 「交響樂 (Beethoven - Symphony No. 5)」完整序列：
     *    3x RampSineORBrake(100ms, 145Hz, 20ms attack, pk8=102, m=2=256B, gap=100ms)
     *    + ResCalibration(13ms, pk8=120, gap=80ms) + RampSineORBrake(400ms, gap=20ms)。 */
    {
        static uint8_t note100[2400], note400[9600];
        test_make_rampsine_pcm8(note100, sizeof(note100), 145.0, 20.0, 20.0, 102.0);
        test_make_rampsine_pcm8(note400, sizeof(note400), 145.0, 20.0, 20.0, 102.0);
        FuxiHapticStream st = {0};
        fuxi_damp_init(&st.damp, 0x0009u, 5710);
        TestLraPlant pl = {0};
        uint64_t t_ns = MS(2000);
        const unsigned gaps[5] = {0, 100, 100, 80, 20};
        for (unsigned k = 0; k < 5; k++) {
            t_ns += MS(gaps[k]);
            pl.xpk175 = pl.xpk170 = 0.0;
            if (k < 3) {
                test_push_pcm8_burst(&io, &m, &st, &pl, note100, sizeof(note100), 256u, &t_ns, 1, &autores);
                assert(st.burst_profile == FUXI_DAMP_M9_PROF_SUS && st.burst_clips == 0 && st.burst_underruns == 0);
                assert(st.burst_max_step <= 10u && pl.xpk175 <= 190.0 && pl.xpk170 <= 190.0);
                if (k >= 1) {
                    assert(st.burst_repeat_level == 3 && st.burst_drive_peak <= (unsigned)FUXI_DAMP_M9_SUS_LIMIT);
                }
            } else if (k == 3) {
                test_push_pcm8_burst(&io, &m, &st, &pl, res_cal, sizeof(res_cal), sizeof(res_cal), &t_ns, 1, &autores);
                assert(st.burst_profile == FUXI_DAMP_M9_PROF_LP && st.burst_repeat_level == 3);
                assert(st.burst_drive_peak <= (unsigned)FUXI_DAMP_M9_LP_REP3_LIMIT && st.burst_clips == 0);
                assert(pl.xpk175 <= 165.0 && pl.xpk170 <= 165.0);
            } else {
                test_push_pcm8_burst(&io, &m, &st, &pl, note400, sizeof(note400), 256u, &t_ns, 1, &autores);
                assert(st.burst_profile == FUXI_DAMP_M9_PROF_SUS && st.burst_repeat_level == 3);
                assert(st.burst_drive_peak <= (unsigned)FUXI_DAMP_M9_SUS_LIMIT && st.burst_clips == 0);
                assert(st.burst_underruns == 0 && st.burst_max_step <= 10u && pl.xpk175 <= 165.0 && pl.xpk170 <= 165.0);
            }
            for (unsigned z = 0; z < 480; z++) test_lra_step(&pl, 0);
        }
        assert(fuxi_stream_close(&io, mock_rtp_write, &st) == 0);
    }

    /* 3) 「斷音 (Staccato)」完整序列 + 背靠背未排空尾段切換：
     *    Stac1(200ms, pk8=118) -> ResCal1(@55ms, 留 tail 未排空) -> 背靠背 Stac2(400ms, @15ms) -> Loop1(200ms, @75ms)。 */
    {
        static uint8_t stac200[4800], stac400[9600];
        test_make_rampsine_pcm8(stac200, sizeof(stac200), 145.0, 20.0, 20.0, 118.0);
        test_make_rampsine_pcm8(stac400, sizeof(stac400), 145.0, 20.0, 20.0, 118.0);
        FuxiHapticStream st = {0};
        fuxi_damp_init(&st.damp, 0x0009u, 5710);
        TestLraPlant pl = {0};
        uint64_t t_ns = MS(5000);
        /* Stac1 */
        test_push_pcm8_burst(&io, &m, &st, &pl, stac200, sizeof(stac200), 256u, &t_ns, 1, &autores);
        assert(st.burst_profile == FUXI_DAMP_M9_PROF_SUS && st.burst_clips == 0 && st.burst_underruns == 0);
        assert(st.burst_max_step <= 10u && pl.xpk175 <= 195.0 && pl.xpk170 <= 195.0);
        /* ResCal1 (不排空至 0，驗證 tail_emitted==1 背靠背接 Stac2！) */
        t_ns += MS(55);
        pl.xpk175 = pl.xpk170 = 0.0;
        test_push_pcm8_burst(&io, &m, &st, &pl, res_cal, sizeof(res_cal), sizeof(res_cal), &t_ns, 0, &autores);
        assert(st.playing && st.tail_emitted && st.burst_profile == FUXI_DAMP_M9_PROF_LP && st.burst_repeat_level == 3);
        assert(st.burst_drive_peak <= (unsigned)FUXI_DAMP_M9_LP_REP3_LIMIT && pl.xpk175 <= 165.0);
        /* 背靠背 Stac2（ResCal1 尾段尚未排空即到達） */
        m.fifo_fill = 32u;
        t_ns += MS(15);
        pl.xpk175 = pl.xpk170 = 0.0;
        test_push_pcm8_burst(&io, &m, &st, &pl, stac400, sizeof(stac400), 256u, &t_ns, 1, &autores);
        assert(st.burst_profile == FUXI_DAMP_M9_PROF_SUS && st.burst_repeat_level == 3);
        assert(st.burst_clips == 0 && st.burst_underruns == 0 && st.burst_max_step <= 10u);
        assert(pl.xpk175 <= 170.0 && pl.xpk170 <= 170.0);
        /* Loop1 @75ms */
        t_ns += MS(75);
        pl.xpk175 = pl.xpk170 = 0.0;
        test_push_pcm8_burst(&io, &m, &st, &pl, stac200, sizeof(stac200), 256u, &t_ns, 1, &autores);
        assert(st.burst_profile == FUXI_DAMP_M9_PROF_SUS && st.burst_repeat_level == 3);
        assert(st.burst_drive_peak <= (unsigned)FUXI_DAMP_M9_SUS_LIMIT && pl.xpk175 <= 170.0 && pl.xpk170 <= 170.0);
        assert(fuxi_stream_close(&io, mock_rtp_write, &st) == 0);
    }

    /* 4) 0 dB (pk8=126) 與 5 ms 快起振 RampSineORBrake 邊界：首段即鎖 SUS，零撞底零毛刺 */
    {
        static uint8_t fast_sine[2400];
        const double amps[2] = {126.0, 102.0};
        const double atts[2] = {20.0, 5.0};
        for (unsigned ci = 0; ci < 2; ci++) {
            test_make_rampsine_pcm8(fast_sine, sizeof(fast_sine), 145.0, atts[ci], 20.0, amps[ci]);
            FuxiHapticStream st = {0};
            fuxi_damp_init(&st.damp, 0x0009u, 5710);
            TestLraPlant pl = {0};
            uint64_t t_ns = MS(9000 + ci * 1000u);
            test_push_pcm8_burst(&io, &m, &st, &pl, fast_sine, sizeof(fast_sine), 256u, &t_ns, 1, &autores);
            assert(st.burst_profile == FUXI_DAMP_M9_PROF_SUS && st.burst_clips == 0 && st.burst_underruns == 0);
            assert(st.burst_drive_peak <= (unsigned)FUXI_DAMP_M9_SUS_LIMIT && st.burst_max_step <= 8u);
            assert(pl.xpk175 <= 165.0 && pl.xpk170 <= 165.0);
            assert(fuxi_stream_close(&io, mock_rtp_write, &st) == 0);
        }
    }

    /* 5) 既有好評 UI 手感保護：手電筒按放雙擊 (199ms) 全幅、第 3+ 次連按 (250ms) 退讓、
     *    上滑後台 / 設定開關 / 鍵盤連打永遠不受連發限幅影響。 */
    {
        FuxiHapticStream st = {0};
        fuxi_damp_init(&st.damp, 0x0009u, 5710);
        uint64_t t_ns = MS(12000);
        /* 鎖屏手電筒按放雙擊 (199ms) + 連續第 3/4 次 (250ms) */
        const unsigned torch_gaps[4] = {0, 199, 250, 250};
        for (unsigned k = 0; k < 4; k++) {
            t_ns += MS(torch_gaps[k]);
            TestLraPlant pl = {0};
            test_push_pcm8_burst(&io, &m, &st, &pl, tap_lp, sizeof(tap_lp), sizeof(tap_lp), &t_ns, 1, &autores);
            assert(st.burst_profile == FUXI_DAMP_M9_PROF_LP && st.burst_clips == 0);
            if (k < 2) {
                assert(st.burst_repeat_level == 0 && st.burst_drive_peak >= 118u);
            } else if (k == 2) {
                assert(st.burst_repeat_level == 2 && st.burst_drive_peak <= (unsigned)FUXI_DAMP_M9_LP_REP2_LIMIT);
            } else {
                assert(st.burst_repeat_level == 3 && st.burst_drive_peak <= (unsigned)FUXI_DAMP_M9_LP_REP3_LIMIT);
            }
        }
        /* 設定開關 (UNI) 連切 4 次 @ 120ms：始終 prof=1、不降幅且清零 lp_streak */
        for (unsigned k = 0; k < 4; k++) {
            t_ns += MS(120);
            TestLraPlant pl = {0};
            test_push_pcm8_burst(&io, &m, &st, &pl, ui_sw, sizeof(ui_sw), sizeof(ui_sw), &t_ns, 1, &autores);
            assert(st.burst_profile == FUXI_DAMP_M9_PROF_UNI && st.burst_drive_peak >= 110u && st.burst_clips == 0);
        }
        /* 緊接設定開關後的單次長按：因 UNI 已清零 lp_streak，單次長按保持滿幅 */
        t_ns += MS(120);
        {
            TestLraPlant pl = {0};
            test_push_pcm8_burst(&io, &m, &st, &pl, tap_lp, sizeof(tap_lp), sizeof(tap_lp), &t_ns, 1, &autores);
            assert(st.burst_profile == FUXI_DAMP_M9_PROF_LP && st.burst_repeat_level == 0 && st.burst_drive_peak >= 118u);
        }
        /* 上滑後台 (prof=0, raw_pk=75) 連續 3 次 @ 100ms：始終 STD 原味 */
        unsigned sw_pk0 = 0;
        for (unsigned k = 0; k < 3; k++) {
            t_ns += MS(100);
            TestLraPlant pl = {0};
            test_push_pcm8_burst(&io, &m, &st, &pl, app_sw, sizeof(app_sw), 296u, &t_ns, 1, &autores);
            assert(st.burst_profile == FUXI_DAMP_M9_PROF_STD && st.burst_clips == 0);
            if (k == 0) sw_pk0 = st.burst_drive_peak;
            else assert(st.burst_drive_peak == sw_pk0);
        }
        /* 鍵盤連打 8 次 @ 65ms：每次 STD 輸出完全一致 */
        unsigned kbd_pk0 = 0;
        for (unsigned k = 0; k < 8; k++) {
            t_ns += MS(65);
            TestLraPlant pl = {0};
            test_push_pcm8_burst(&io, &m, &st, &pl, kbd, sizeof(kbd), sizeof(kbd), &t_ns, 1, &autores);
            assert(st.burst_profile == FUXI_DAMP_M9_PROF_STD && st.burst_repeat_level == 0 && st.burst_clips == 0);
            if (k == 0) kbd_pk0 = st.burst_drive_peak;
            else assert(st.burst_drive_peak == kbd_pk0);
        }
        assert(fuxi_stream_close(&io, mock_rtp_write, &st) == 0);
    }
}

static void test_amp_wake_hold_and_peaks(void) {
    uint32_t hold = 0;
    /* Awake or empty ring never holds and clears hold counter */
    assert(fuxi_audio_should_hold_for_wake(1u, 960u, &hold) == 0 && hold == 0u);
    assert(fuxi_audio_should_hold_for_wake(0u, 0u, &hold) == 0 && hold == 0u);
    /* Sleeping amp with pending PCM holds for up to FUXI_AUDIO_WAKE_HOLD_PERIODS (2) periods */
    assert(fuxi_audio_should_hold_for_wake(0u, 206u, &hold) == 1 && hold == 1u);
    assert(fuxi_audio_should_hold_for_wake(0u, 206u, &hold) == 1 && hold == 2u);
    /* 3rd period releases even if amp failed to wake, preventing infinite ring stall */
    assert(fuxi_audio_should_hold_for_wake(0u, 206u, &hold) == 0 && hold == 2u);
    /* Once amp wakes, hold counter resets for next standby cycle */
    assert(fuxi_audio_should_hold_for_wake(1u, 206u, &hold) == 0 && hold == 0u);
    assert(fuxi_audio_should_hold_for_wake(0u, 206u, &hold) == 1 && hold == 1u);

    const int16_t pcm[6] = { -32768, 1200, 4500, -18000, 0, 500 };
    uint32_t lpk = 0, rpk = 0;
    fuxi_audio_pcm_peaks(pcm, 3u, &lpk, &rpk);
    assert(lpk == 32768u && rpk == 18000u);

    /* Silent-only ring writes while amp is asleep must NOT report nonzero or trigger wake-hold */
    static FuxiAudioRing r;
    static uint32_t tdm[960 * 4];
    static int16_t zeros[960 * 2];
    static int16_t click[960 * 2];
    memset(&r, 0, sizeof(r));
    memset(zeros, 0, sizeof(zeros));
    memset(click, 0, sizeof(click));
    click[40] = 6000;
    click[41] = -6000;
    assert(fuxi_audio_ring_has_nonzero(&r) == 0u);
    assert(fuxi_audio_ring_write(&r, zeros, 960u) == 0);
    assert(fuxi_audio_ring_used(&r) == 960u && fuxi_audio_ring_has_nonzero(&r) == 0u);
    hold = 0u;
    assert(fuxi_audio_should_hold_for_wake(0u, fuxi_audio_ring_has_nonzero(&r), &hold) == 0 && hold == 0u);
    /* Non-zero click sets has_nonzero and triggers wake-hold until amp wakes and drains past click */
    assert(fuxi_audio_ring_write(&r, click, 960u) == 1);
    assert(fuxi_audio_ring_has_nonzero(&r) == 1u);
    assert(fuxi_audio_should_hold_for_wake(0u, fuxi_audio_ring_has_nonzero(&r), &hold) == 1 && hold == 1u);
    assert(fuxi_audio_ring_write(&r, zeros, 960u) == 0);
    assert(fuxi_audio_ring_has_nonzero(&r) == 1u);
    /* Drain first silent chunk (still before click), then click chunk, leaving trailing silence */
    assert(fuxi_audio_ring_read_tdm(&r, tdm, 960u, 0, 1) == 0u);
    assert(fuxi_audio_ring_has_nonzero(&r) == 1u);
    assert(fuxi_audio_ring_read_tdm(&r, tdm, 960u, 0, 1) > 0u);
    assert(fuxi_audio_ring_has_nonzero(&r) == 0u);
    /* Trailing silence in ring after click was played must NOT hold while amp sleeps */
    hold = 0u;
    assert(fuxi_audio_ring_used(&r) == 960u);
    assert(fuxi_audio_should_hold_for_wake(0u, fuxi_audio_ring_has_nonzero(&r), &hold) == 0 && hold == 0u);
}

static void test_amp_idle_standby(void) {
    assert(fuxi_audio_clamp_idle_pdn_ms(0) == 0);
    assert(fuxi_audio_clamp_idle_pdn_ms(1) == FUXI_AUDIO_IDLE_PDN_DEFAULT_MS);
    assert(fuxi_audio_clamp_idle_pdn_ms(100) == FUXI_AUDIO_IDLE_PDN_MIN_MS && FUXI_AUDIO_IDLE_PDN_MIN_MS == 200u);
    assert(fuxi_audio_clamp_idle_pdn_ms(200) == 200);
    assert(fuxi_audio_clamp_idle_pdn_ms(500) == 500);
    assert(fuxi_audio_clamp_idle_pdn_ms(2500) == 2500);
    assert(fuxi_audio_clamp_idle_pdn_ms(999999) == FUXI_AUDIO_IDLE_PDN_MAX_MS);

    /* idle_pdn_ms == 0 preserves legacy digital-mute-only behavior */
    FuxiAmpPowerState pwr;
    int want_unmute = 0;
    fuxi_amp_power_init(&pwr, MS(0), 0, 0, 0);
    assert(fuxi_amp_power_step(&pwr, MS(100), 1, 0, 0, 0, &want_unmute) == FUXI_AMP_ACT_NONE && want_unmute == 1);
    assert(fuxi_amp_power_step(&pwr, MS(10000), 0, 0, 0, 0, &want_unmute) == FUXI_AMP_ACT_NONE && want_unmute == 0);
    assert(fuxi_amp_power_step(&pwr, MS(10500), 0, 1, 0, 0, &want_unmute) == FUXI_AMP_ACT_NONE && want_unmute == 1);

    /* active == 0 (post-chime or StopIO): fast 200 ms standby (160 ms drain + 40 ms mute ramp) even when idle_pdn_ms == 2500 */
    fuxi_amp_power_init(&pwr, MS(0), 0, 0, 0);
    assert(fuxi_amp_power_step(&pwr, MS(600), 1, 0, 0, 2500, &want_unmute) == FUXI_AMP_ACT_NONE && want_unmute == 1);
    assert(fuxi_amp_power_step(&pwr, MS(759), 0, 0, 0, 2500, &want_unmute) == FUXI_AMP_ACT_NONE && want_unmute == 1);
    assert(fuxi_amp_power_step(&pwr, MS(760), 0, 0, 0, 2500, &want_unmute) == FUXI_AMP_ACT_NONE && want_unmute == 0);
    assert(pwr.muted && pwr.muted_since_ns == MS(760));
    assert(fuxi_amp_power_step(&pwr, MS(799), 0, 0, 0, 2500, &want_unmute) == FUXI_AMP_ACT_NONE && want_unmute == 0);
    assert(fuxi_amp_power_step(&pwr, MS(800), 0, 0, 0, 2500, &want_unmute) == FUXI_AMP_ACT_STANDBY && want_unmute == 0);
    pwr.powered = 0;
    assert(fuxi_amp_power_step(&pwr, MS(4000), 0, 0, 0, 2500, &want_unmute) == FUXI_AMP_ACT_NONE && want_unmute == 0);
    /* New nonzero PCM wakes amp */
    assert(fuxi_amp_power_step(&pwr, MS(5000), 0, 1, MS(4995), 2500, &want_unmute) == FUXI_AMP_ACT_WAKE && want_unmute == 1);
    pwr.powered = 1;
    /* Active stream goes silent -> respects exact idle_pdn_ms=2500 without +2000ms double-count: mute at +2000ms, standby at +2500ms */
    assert(fuxi_amp_power_step(&pwr, MS(6999), 0, 1, MS(4995), 2500, &want_unmute) == FUXI_AMP_ACT_NONE && want_unmute == 1);
    assert(fuxi_amp_power_step(&pwr, MS(7000), 0, 1, MS(4995), 2500, &want_unmute) == FUXI_AMP_ACT_NONE && want_unmute == 0);
    assert(fuxi_amp_power_step(&pwr, MS(7499), 0, 1, MS(4995), 2500, &want_unmute) == FUXI_AMP_ACT_NONE && want_unmute == 0);
    assert(fuxi_amp_power_step(&pwr, MS(7500), 0, 1, MS(4995), 2500, &want_unmute) == FUXI_AMP_ACT_STANDBY && want_unmute == 0);
    pwr.powered = 0;
    /* Falling & rising active edge wakes amp even before first nonzero sample */
    assert(fuxi_amp_power_step(&pwr, MS(9600), 0, 0, MS(4995), 2500, &want_unmute) == FUXI_AMP_ACT_NONE && want_unmute == 0);
    assert(fuxi_amp_power_step(&pwr, MS(9700), 0, 1, MS(4995), 2500, &want_unmute) == FUXI_AMP_ACT_WAKE && want_unmute == 1);

    /* Short active idle_pdn_ms == 500: mutes at 460 ms, enforces >= 40 ms soft-ramp before STANDBY at 500 ms */
    fuxi_amp_power_init(&pwr, MS(0), 1, 0, 0);
    assert(fuxi_amp_power_step(&pwr, MS(459), 0, 1, 0, 500, &want_unmute) == FUXI_AMP_ACT_NONE && want_unmute == 1);
    assert(fuxi_amp_power_step(&pwr, MS(460), 0, 1, 0, 500, &want_unmute) == FUXI_AMP_ACT_NONE && want_unmute == 0);
    assert(fuxi_amp_power_step(&pwr, MS(499), 0, 1, 0, 500, &want_unmute) == FUXI_AMP_ACT_NONE && want_unmute == 0);
    assert(fuxi_amp_power_step(&pwr, MS(500), 0, 1, 0, 500, &want_unmute) == FUXI_AMP_ACT_STANDBY && want_unmute == 0);
}

typedef struct {
    uint32_t va[0x40];
    uint32_t tx[0x120];
    uint32_t swr[0x1500];
    uint32_t lpi[16];
    uint32_t slew;
    uint32_t reset[2];
    uint8_t codec[0x3600];
    uint32_t rd_resp[32];
    unsigned rd_head, rd_tail;
    uint64_t fault_pa;
} MockMic;

static uint32_t mock_mic_rd(void *ctx, uint64_t pa, uint32_t *faulted) {
    MockMic *m = (MockMic *)ctx;
    if (m->fault_pa && pa == m->fault_pa) { if (faulted) *faulted = 1; return 0; }
    if (faulted) *faulted = 0;
    if (pa >= FUXI_MIC_VA && pa < FUXI_MIC_VA + sizeof(m->va)) return m->va[(pa - FUXI_MIC_VA) / 4];
    if (pa >= FUXI_MIC_TX && pa < FUXI_MIC_TX + sizeof(m->tx)) return m->tx[(pa - FUXI_MIC_TX) / 4];
    if (pa >= FUXI_MIC_SWR && pa < FUXI_MIC_SWR + sizeof(m->swr)) {
        uint32_t off = (uint32_t)(pa - FUXI_MIC_SWR);
        if (off == 0x5050u) return ((uint32_t)(m->rd_head - m->rd_tail) & 0x1fu) << 16;
        if (off == 0x5040u) {
            if (m->rd_tail != m->rd_head) return m->rd_resp[(m->rd_tail++) & 31u];
            return 0;
        }
        return m->swr[off / 4];
    }
    if (pa >= FUXI_MIC_LPI && pa < FUXI_MIC_LPI + 0x10000u && ((pa - FUXI_MIC_LPI) & 0xfffu) == 0) {
        return m->lpi[(pa - FUXI_MIC_LPI) >> 12];
    }
    if (pa == FUXI_MIC_SLEW) return m->slew;
    if (pa == FUXI_MIC_RESET) return m->reset[0];
    if (pa == FUXI_MIC_RESET + 4u) return m->reset[1];
    if (faulted) *faulted = 1;
    return 0;
}

static unsigned mock_mic_wr(void *ctx, uint64_t pa, uint32_t value) {
    MockMic *m = (MockMic *)ctx;
    if (m->fault_pa && pa == m->fault_pa) return 1;
    if (pa >= FUXI_MIC_VA && pa < FUXI_MIC_VA + sizeof(m->va)) { m->va[(pa - FUXI_MIC_VA) / 4] = value; return 0; }
    if (pa >= FUXI_MIC_TX && pa < FUXI_MIC_TX + sizeof(m->tx)) { m->tx[(pa - FUXI_MIC_TX) / 4] = value; return 0; }
    if (pa >= FUXI_MIC_SWR && pa < FUXI_MIC_SWR + sizeof(m->swr)) {
        uint32_t off = (uint32_t)(pa - FUXI_MIC_SWR);
        m->swr[off / 4] = value;
        if (off == 0x004u && value == 3u) {
            m->swr[0x5064u / 4] = 1u;
            m->swr[0x1090u / 4] = 1u << 2;
        } else if (off == 0x308u && value == 1u) {
            m->rd_head = m->rd_tail = 0;
        } else if (off == 0x5020u) {
            uint32_t reg = value & 0xffffu;
            uint8_t data = (uint8_t)(value >> 24);
            if (reg < sizeof(m->codec)) m->codec[reg] = data;
        } else if (off == 0x5024u) {
            uint32_t reg = value & 0xffffu;
            uint32_t id = (value >> 16) & 0xfu;
            uint8_t data = reg < sizeof(m->codec) ? m->codec[reg] : 0;
            m->rd_resp[(m->rd_head++) & 31u] = (id << 8) | data;
        }
        return 0;
    }
    if (pa >= FUXI_MIC_LPI && pa < FUXI_MIC_LPI + 0x10000u && ((pa - FUXI_MIC_LPI) & 0xfffu) == 0) {
        m->lpi[(pa - FUXI_MIC_LPI) >> 12] = value;
        return 0;
    }
    if (pa == FUXI_MIC_SLEW) { m->slew = value; return 0; }
    if (pa == FUXI_MIC_RESET) { m->reset[0] = value; return 0; }
    if (pa == FUXI_MIC_RESET + 4u) { m->reset[1] = value; return 0; }
    return 1;
}

static void mock_mic_delay(void *ctx, uint32_t us) { (void)ctx; (void)us; }

static void init_mock_mic(MockMic *m) {
    memset(m, 0, sizeof(*m));
    m->swr[0] = 0x02000000u;
    m->reset[0] = 0x100u; m->reset[1] = 0u;
    m->lpi[0] = 0x11; m->lpi[1] = 0x22; m->lpi[2] = 0x33; m->lpi[14] = 0x44;
    m->slew = 0xaa;
    m->va[0xd0 / 4] = 0x11; m->va[0xd4 / 4] = 0x22; m->va[0xd8 / 4] = 0x33;
    m->codec[0x51] = 0x02; m->codec[0x52] = 0x17; m->codec[0x53] = 0x01; m->codec[0x54] = 0x0d;
    m->codec[0x3403] = 0x0d; m->codec[0x3404] = 0x01;
}

static void test_audio_mic_hw_and_ring(void) {
    uint8_t adc1 = 0, dec0 = 0;
    fuxi_mic_decode_gain(0, &adc1, &dec0);
    assert(adc1 == 8 && dec0 == 0x12);
    fuxi_mic_decode_gain(0x9999u, &adc1, &dec0);
    assert(adc1 == FUXI_MIC_ADC1_MAX && dec0 == FUXI_MIC_DEC0_MAX);
    fuxi_mic_decode_gain((0x18u << 8) | 12u, &adc1, &dec0);
    assert(adc1 == 12 && dec0 == 0x18);

    static FuxiAudioMicRing ring;
    fuxi_mic_ring_reset(&ring);
    int16_t buf[480], out[480];
    for (unsigned i = 0; i < 480; i++) buf[i] = (int16_t)(i - 200);
    assert(fuxi_mic_ring_push(&ring, buf, 480) == 480);
    assert(fuxi_mic_ring_pop(&ring, out, 200) == 200 && out[0] == -200 && out[199] == -1);
    assert(fuxi_mic_ring_pop(&ring, out, 480) == 280 && out[0] == 0 && out[279] == 279);
    for (unsigned k = 0; k < 40; k++) fuxi_mic_ring_push(&ring, buf, 480);
    assert(ring.drops == 40u * 480u - FUXI_MIC_RING_CAP);

    static MockMic mm;
    init_mock_mic(&mm);
    FuxiMicHw hw = {0};
    hw.io = (FuxiMicIo){&mm, mock_mic_rd, mock_mic_wr, mock_mic_delay};
    hw.dev = 1; hw.port = 3; hw.lane = 1; hw.mic = 5; hw.adc1_gain = 10; hw.dec0_gain = 0x16;
    assert(!fuxi_mic_codec_write(&hw, 0x3500u, 0x01u));
    assert(!fuxi_mic_codec_write(&hw, FUXI_MIC_MICB1, 0x40u | 0x24u));
    assert(fuxi_mic_codec_write(&hw, FUXI_MIC_MICB1, 0x40u | 0x23u));

    init_mock_mic(&mm);
    hw = (FuxiMicHw){0};
    hw.io = (FuxiMicIo){&mm, mock_mic_rd, mock_mic_wr, mock_mic_delay};
    hw.port = 3; hw.lane = 1; hw.mic = 5; hw.adc1_gain = 10; hw.dec0_gain = 0x16;
    assert(fuxi_mic_hw_bringup(&hw));
    assert(hw.passed == 7 && hw.held && hw.readback_ok == 25);
    assert(hw.chip_id[2] == 0x0d && hw.chip_id[3] == 0x01);
    assert((mm.codec[0x300e] & 0x1fu) == 10u);
    assert((mm.tx[0x400 / 4] & 0x10u) != 0u);
    assert(((mm.tx[0x404 / 4] >> 5) & 3u) == 2u);
    mm.tx[0x418 / 4] = 0x5au;
    assert(fuxi_mic_hw_unmute_tx(&hw));
    assert((mm.tx[0x400 / 4] & 0x10u) == 0u && (mm.tx[0x40c / 4] & 0xffu) == 0x16u);
    assert(((mm.tx[0x404 / 4] >> 5) & 3u) == 2u);
    assert(mm.tx[0x418 / 4] == 0x5au);
    hw.hpf_4hz = true;
    assert(fuxi_mic_hw_unmute_tx(&hw));
    assert(((mm.tx[0x404 / 4] >> 5) & 3u) == 0u);
    assert(mm.tx[0x418 / 4] == 0x59u);
    fuxi_mic_hw_down(&hw);
    assert(!hw.held && mm.slew == 0xaa && mm.lpi[14] == 0x44 && mm.reset[0] == 0x100u);
    assert((mm.codec[FUXI_MIC_MICB1] & 0xc0u) == 0u);

    init_mock_mic(&mm);
    mm.codec[0x3403] = 0x99;
    hw = (FuxiMicHw){0};
    hw.io = (FuxiMicIo){&mm, mock_mic_rd, mock_mic_wr, mock_mic_delay};
    hw.port = 3; hw.lane = 1; hw.mic = 5; hw.adc1_gain = 8; hw.dec0_gain = 0x12;
    assert(!fuxi_mic_hw_bringup(&hw));
    assert(hw.stop_step == 6 && hw.why == FUXI_MIC_WHY_CHIP && !hw.held);
    assert(mm.slew == 0xaa && mm.reset[0] == 0x100u);
}

int main(void) {
    test_audio_volume_and_gain();
    test_audio_ring_and_chime();
    test_haptic_shape_v2();
    test_haptic_shape_v3();
    test_rtp_hv_short_wave();
    test_haptic_stream_v4();
    test_haptic_damp_and_tail();
    test_haptic_mode5_and_mode6();
    test_haptic_mode7();
    test_haptic_mode8();
    test_haptic_mode9();
    test_haptic_mode9_symphony_staccato_and_repeat();
    test_amp_idle_standby();
    test_amp_wake_hold_and_peaks();
    test_audio_mic_hw_and_ring();
    printf("live audio + haptic shape v2 host tests ok\n");
    return 0;
}
'''


def extract_added_file(patch_path: Path, rel: str) -> str:
    text = patch_path.read_text(encoding='utf-8')
    m = re.search(
        r'^--- /dev/null\n\+\+\+ b/' + re.escape(rel) + r'\n@@[^\n]*\n((?:\+[^\n]*\n)+)',
        text,
        re.M,
    )
    assert m, f'{rel} not found in {patch_path.name}'
    return ''.join(line[1:] + '\n' for line in m.group(1).splitlines())


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--sanitize', action='store_true')
    args = ap.parse_args()

    p124 = P0124.read_text(encoding='utf-8')
    assert 'if (max_db > 0) max_db = 0;' in p124
    assert 'fuxi_audio_clamp_gain(' in p124
    assert 'FUXI_AUDIO_PRIME_FRAMES' in p124
    assert 'FM_TONE_PLL_UNLOCK' in p124
    assert 'if (!__atomic_load_n(&gAudioLiveReady, __ATOMIC_ACQUIRE)) return EAGAIN;' in p124
    assert 'if (!__atomic_load_n(&gAudioLiveEnabled, __ATOMIC_ACQUIRE)) return ENOTSUP;' in p124
    assert 'fuxiaudiopcm' in p124 and 'fuxiaudiovol' in p124
    assert 'fp_vibrate_shape = haptic == 6 ? 1u : haptic == 7 ? 2u : haptic == 8 ? 3u : 0u;' in p124
    assert 'fuxi_rtp_wait_hpwr' in p124
    assert 'FUXI_RTP_TEST_VMAX_LIMIT = 0xa3' in p124
    assert 'fuxi_haptic_stream.h' in p124
    assert 'fp_rtp_stream_enabled = haptic == 9;' in p124
    assert 'if (mode > 3 || haptic > 9)' in p124
    assert 'fp_haptic_apid_plus_one[3]' in p124
    assert 'peripheral == 0xf2' in p124
    assert '#define FUXI_STREAM_IDLE_CLOSE_NS 4000000000ull' in p124
    assert 'fuxi_haptic_damp.h' in p124
    assert 'fuxi,haptic-damp' in p124
    assert 'FUXI_STREAM_TAIL_THRESHOLD = 14' in p124
    assert 'FUXI_STREAM_FAST_POLL_WATERMARK = 96' in p124
    assert 'drain > 1 ? 250000ull : 500000ull' in p124
    assert 'fuxi_rtp_write_rc_cal' in p124
    assert 'address == 0xf05e && size == 2' in p124
    assert 'case 0xf063:' in p124
    assert '{0xf063, 0x80, 0}' in p124
    assert 'fuxi_audio_mic.h' in p124
    assert 'fuxi,audio-mic' in p124
    assert 'fuxiaudiomicctl' in p124 and 'fuxiaudiomic' in p124
    assert 'FUXI_MIC_DMA_OFFSET' in p124
    assert 'fuxi,audio-idle-pdn' in p124
    assert 'fuxi_amp_power_step' in p124 and 'ampLiveWake' in p124
    assert 'bsd/kern/kern_shutdown.c' in p124 and 'fuxi_amp_shutdown();' in p124
    assert 'FM_AMP_GAIN_ZC' in p124 and 'bst_off=1' in p124
    # Verify exact Xiaomi CS35L41 power-down sequence inside ampToneDownEx
    pdn_raw = p124[p124.index('+static int ampToneDownEx('):p124.index('+static int ampToneDown(')]
    pdn_block = '\n'.join(l for l in pdn_raw.splitlines() if not l.startswith('-') and not l.startswith('@@'))
    pdn_steps = [
        'FM_DIG_VOL_MUTE', 'FM_PWR_CTRL1', '0x4800, 0', '0x002f1aa3u',
        'FM_TONE_PDN_DONE', '0x31u', 'FM_AMP_GAIN, 0',
    ]
    positions = [pdn_block.index(s) for s in pdn_steps]
    assert positions == sorted(positions), f'CS35L41 power-down order broken: {positions}'
    assert 'fuxi_damp_synth_transient' in p124 and 'fuxi_damp_process_pitch' in p124
    assert 'fuxi_damp_deadbeat_step' in p124 and 'FUXI_STREAM_TAIL_QUIET_NS' in p124
    assert '0xf066, 0x07' in p124 and 'fuxi_stream_will_absorb' in p124
    assert 'FUXI_AUDIO_DIG_VOL_MUTE = 0xa67bu' in p124
    assert 'FUXI_STREAM_SYNTH_MAX_IN = 864u' in p124
    assert 'FUXI_STREAM_TAIL_THRESHOLD_PITCH = 48' in p124
    assert 'if (mode == 8) IOSleep(2);' in p124
    assert 'fp_flash_apid_plus_one' in p124 and 'sid == 1 && peripheral == 0xee' in p124
    assert 'fuxi_amp_live_awake' in p124 and 'fuxi_amp_live_kick' in p124
    assert 'fuxi_audio_should_hold_for_wake' in p124 and 'fuxi_audio_ring_has_nonzero' in p124
    assert 'fuxi_audio_pcm_peaks' in p124
    assert 'fuxi_damp_process_mode7' in p124 and 'FUXI_DAMP_FLAG_CRISP_HIGH' in p124
    assert 'fuxi_damp_mode8_tail_step' in p124 and 'FUXI_DAMP_TAIL_RAMP' in p124
    assert 'fuxi_damp_process_mode9' in p124 and 'fuxi_damp_soft_clamp' in p124
    assert 'fuxi_damp_trim_chunk_pad' in p124 and 'FUXI_DAMP_TAIL_STOP_TIGHT' in p124
    assert 'FUXI_DAMP_M9_PROF_SUS' in p124 and 'FUXI_DAMP_M9_SUS_LIMIT' in p124
    assert 'fuxi_damp_mode9_classify_chunk' in p124 and 'fuxi_damp_mode9_restart_burst' in p124
    assert 'fuxi_damp_mode9_on_burst_start' in p124 and 'fuxi_damp_mode9_on_burst_end' in p124
    assert 'FUXI_DAMP_M9_LP_REP2_LIMIT' in p124 and 'FUXI_DAMP_M9_LP_REP3_LIMIT' in p124
    assert 'FUXI_DAMP_M9_ENV_KNEE_NORM_S' in p124 and 'm9_repeat_level' in p124
    assert 'burst_repeat_level' in p124 and 'stream_notable' in p124

    with tempfile.TemporaryDirectory(prefix='fuxi-live-audio-') as td:
        tmp = Path(td)
        bd = tmp / 'bsd/dev'
        bd.mkdir(parents=True)
        for name in ('fuxi_amp.c', 'fuxi_audio_spf.c', 'fuxi_pmic.c', 'fuxi_haptic_rtp.h',
                     'fuxi_haptic_runtime.h', 'fuxi_haptic_pulse.h'):
            shutil.copy2(ROOT / 'xnu/fuxi-adsp' / name, bd / name)
        subprocess.run(['git', 'init', '-q'], cwd=tmp, check=True)
        subprocess.run(['git', 'apply', str(P0123)], cwd=tmp, check=True)
        subprocess.run(['git', 'apply', '--exclude=bsd/kern/kern_shutdown.c', str(P0124)], cwd=tmp, check=True)
        src = tmp / 'test.c'
        src.write_text(TEST_C, encoding='utf-8', newline='\n')
        exe = tmp / 'test'
        cmd = ['clang', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-I', str(bd), str(src), '-o', str(exe)]
        if args.sanitize:
            cmd.insert(1, '-fsanitize=address,undefined')
        if os.name != 'nt':
            cmd.append('-lm')
        subprocess.run(cmd, check=True)
        out = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
        assert 'live audio + haptic shape v2 host tests ok' in out.stdout
    print('PASS: 0124 live audio stream + haptic shape v2/v3 + HV RTP (host model + patch gates)')


if __name__ == '__main__':
    main()
