#ifndef FUXI_HAPTIC_RUNTIME_H
#define FUXI_HAPTIC_RUNTIME_H
/*
 * HAP525 DIRECT_PLAY 執行期（fuxi,haptic=2 才啟用）。
 * 做法與 Android qcom-hv-haptics 的 constant effect 相同：
 * 開：SPMI_PLAY(0xf04c) 寫 0x81（PLAY_EN | 來源 DIRECT_PLAY，不開 brake）；
 * 關：寫 0x01，再輪詢 HPWR_INTF(0xf00b) 低兩位回到 0（HPWR_DISABLED）；
 * 逾時就照 Linux 的 haptics_toggle_module_enable 關開模組一次再等。
 * VMAX、自動共振沿用 Android 開機前留下的設定，這裡只檢查不改寫。
 * 振幅（2026-11-17）：sysctl 值的 bit 16..23 是 DIRECT_PLAY 振幅，寫進 PTN 0x26（0xf126；Linux
 * qcom-hv-haptics 的 HAP_PTN_DIRECT_PLAY_REG，0..0xff 線性乘上 VMAX，haptics_set_direct_play 播放中也照寫）。
 * 0 表示沿用目前的值：只送毫秒的舊使用端完全不變。VMAX 不改，所以任何級數都不超過 Android 自己會用的強度。
 */
#include <stdint.h>
#include "fuxi_haptic_pulse.h"

enum {
    FUXI_VIBRATE_MAX_MS = 2000,   /* 單次請求上限；鈴聲 pattern 由使用端分段送 */
    FUXI_VIBRATE_POLL_MS = 5,     /* 播放中檢查 fault 的間隔 */
    FUXI_VIBRATE_STOP_POLLS = 30, /* 1 ms 一次；TLRA 約 5.7 ms，Linux 約等 brake 長度加一週期 */
    FUXI_VIBRATE_PLAY = 0x81,
    FUXI_VIBRATE_IDLE = 0x01,
    FUXI_VIBRATE_MODULE_ON = 0x80,
    FUXI_VIBRATE_VMAX_LIMIT = 0x1c, /* 50 mV/碼，1400 mV：Android 留下的值，不接受更高 */
    FUXI_VIBRATE_LEVEL_SHIFT = 16,  /* sysctl 值：bit 0..15 毫秒，bit 16..23 振幅，其餘須為 0 */
    FUXI_VIBRATE_AMPLITUDE = 0xf126,
    /*
     * FAULT_STATUS(0xf00c)：bit 2 SC、bit 1 AUTO_RES_ERROR、bit 0 HPWR_RDY_FAULT（Linux qcom-hv-haptics
     * HAP_CFG_FAULT_STATUS_REG）。低振幅時反電動勢太弱，自動共振鎖不住會設 bit 1；Linux 播放中從不檢查它，
     * 每次開播前 haptics_clear_fault 寫 FAULT_CLR(0xf066)。2026-11-18 實機：振幅 0x08..0x30 播放約 46 ms 就設 0x2，
     * 舊版把它當致命錯誤，整個執行期鎖到重開機。現在只有 SC／HPWR_RDY／未知位元才停機鎖死；
     * AUTO_RES_ERROR 只記錄，下次開播前清掉（只清這一位，SC 仍要人看）。
     */
    FUXI_VIBRATE_FAULT_CLEAR = 0xf066,
    FUXI_VIBRATE_FAULT_AUTORES = 0x02
};

typedef struct {
    uint64_t deadline;  /* 奈秒；now < deadline 期間要震 */
    uint64_t requests;
    uint64_t plays;
    unsigned failed;
    unsigned playing;
    unsigned level;     /* 目前請求的振幅，0＝沿用暫存器現值 */
    int last_error;
} FuxiVibrateRuntime;

/* sysctl 寫入值拆成毫秒與振幅；格式不對（負數、高位元、超過上限）回 -1。 */
static inline int fuxi_vibrate_decode(int value, unsigned *ms, unsigned *level)
{
    if (value < 0 || ((unsigned)value >> 24)) return -1;
    *ms = (unsigned)value & 0xffffu;
    *level = ((unsigned)value >> FUXI_VIBRATE_LEVEL_SHIFT) & 0xffu;
    return *ms > FUXI_VIBRATE_MAX_MS ? -1 : 0;
}

/* 允許的呼叫者：root，或平台二進位檔名為 SpringBoard／mediaserverd。 */
static inline int fuxi_vibrate_caller_allowed(unsigned root, unsigned platform, const char *name)
{
    static const char *const allowed[] = {"SpringBoard", "mediaserverd"};
    if (root) return 1;
    if (!platform || !name) return 0;
    for (unsigned entry = 0; entry < sizeof(allowed) / sizeof(allowed[0]); entry++) {
        const char *expected = allowed[entry];
        unsigned index = 0;
        while (expected[index] && name[index] == expected[index]) index++;
        if (!expected[index] && !name[index]) return 1;
    }
    return 0;
}

/* ms=0 立即停；1..FUXI_VIBRATE_MAX_MS 從現在起震這麼久（覆蓋前一個請求）。 */
static inline int fuxi_vibrate_request(FuxiVibrateRuntime *state, unsigned ms, uint64_t now)
{
    if (!state || ms > FUXI_VIBRATE_MAX_MS) return -1;
    if (ms && (state->failed || now > UINT64_MAX - (uint64_t)FUXI_VIBRATE_MAX_MS * 1000000ull)) return -1;
    state->deadline = ms ? now + (uint64_t)ms * 1000000ull : 0;
    state->requests++;
    return 0;
}

/* 同上，另帶振幅（0..0xff）；停止（ms=0）不改記下的振幅。 */
static inline int fuxi_vibrate_request_level(FuxiVibrateRuntime *state, unsigned ms, unsigned level, uint64_t now)
{
    if (level > 0xffu || fuxi_vibrate_request(state, ms, now)) return -1;
    if (ms) state->level = level;
    return 0;
}

/* 播放前或播放中套用振幅：0 或與現值相同就不寫；寫入讀回相符才更新 *current。 */
static inline int fuxi_vibrate_amplitude(const FuxiHapticIo *io, unsigned level, uint8_t *current, unsigned *writes)
{
    if (!level || level == *current) return FUXI_HAPTIC_OK;
    if (level > 0xffu) return FUXI_HAPTIC_INVALID;
    int result = fuxi_haptic_write_checked(io, FUXI_VIBRATE_AMPLITUDE, (uint8_t)level);
    (*writes)++;
    if (!result) *current = (uint8_t)level;
    return result;
}

static inline unsigned fuxi_vibrate_wanted(FuxiVibrateRuntime *state, uint64_t now)
{
    if (state->failed) state->deadline = 0;
    return state->deadline && now < state->deadline;
}

/* 剩下的毫秒數（無條件進位），供 sysctl 讀取。 */
static inline unsigned fuxi_vibrate_remaining_ms(const FuxiVibrateRuntime *state, uint64_t now)
{
    if (state->failed || !state->deadline || now >= state->deadline) return 0;
    return (unsigned)((state->deadline - now + 999999ull) / 1000000ull);
}

/* 開機後第一次使用前的完整檢查：身分、owner、Android 留下的 DIRECT_PLAY 設定、閒置。 */
static inline int fuxi_vibrate_check(const FuxiHapticIo *io)
{
    static const unsigned identities[] = {
        0xf001, 0xf004, 0xf005, 0xf101, 0xf104, 0xf105,
        0xf200, 0xf201, 0xf204, 0xf205
    };
    static const uint8_t expected[] = {4, 0x15, 0x0a, 4, 0x15, 0x0b, 2, 0, 0x31, 1};
    uint8_t value = 0, module = 0, play = 0, vmax = 0, amplitude = 0, autores = 0;
    if (!io || !io->owner || !io->read || !io->write || !io->wait_us) return FUXI_HAPTIC_INVALID;
    for (unsigned index = 0; index < 3; index++) {
        unsigned owner = 7;
        if (io->owner(io->context, 0xf0 + index, &owner) || owner != (index == 2 ? 3u : 0u))
            return FUXI_HAPTIC_HARDWARE;
    }
    for (unsigned index = 0; index < sizeof(identities) / sizeof(identities[0]); index++) {
        if (fuxi_haptic_read_stable(io, identities[index], &value)) return FUXI_HAPTIC_IO;
        if (value != expected[index]) return FUXI_HAPTIC_HARDWARE;
    }
    if (fuxi_haptic_read_stable(io, 0xf046, &module) || fuxi_haptic_read_stable(io, 0xf04c, &play) ||
        fuxi_haptic_read_stable(io, 0xf048, &vmax) || fuxi_haptic_read_stable(io, 0xf126, &amplitude) ||
        fuxi_haptic_read_stable(io, 0xf063, &autores))
        return FUXI_HAPTIC_IO;
    if (!(module & 0x80u) || play != FUXI_VIBRATE_IDLE || !amplitude || !vmax ||
        vmax > FUXI_VIBRATE_VMAX_LIMIT || !(autores & 0x80u))
        return FUXI_HAPTIC_HARDWARE;
    return fuxi_haptic_idle(io);
}

/* 播放中檢查：AUTO_RES_ERROR 只回報在 *fault，不算失敗；其他位元回 FAULT。 */
static inline int fuxi_vibrate_health(const FuxiHapticIo *io, uint8_t *fault, uint8_t *hpwr)
{
    if (io->read(io->context, 0xf00c, fault) || io->read(io->context, 0xf00b, hpwr)) return FUXI_HAPTIC_IO;
    return *fault & (uint8_t)~FUXI_VIBRATE_FAULT_AUTORES ? FUXI_HAPTIC_FAULT : FUXI_HAPTIC_OK;
}

/* 只清 AUTO_RES_ERROR：FAULT_CLR 是寫一清除，不讀回比對，改讀 FAULT_STATUS 確認已歸零。 */
static inline int fuxi_vibrate_clear_autores(const FuxiHapticIo *io, uint8_t *fault, unsigned *cleared)
{
    unsigned owner = 7;
    if (*fault != FUXI_VIBRATE_FAULT_AUTORES) return *fault ? FUXI_HAPTIC_FAULT : FUXI_HAPTIC_OK;
    if (io->owner(io->context, FUXI_VIBRATE_FAULT_CLEAR >> 8, &owner) || owner != 0) return FUXI_HAPTIC_HARDWARE;
    if (io->write(io->context, FUXI_VIBRATE_FAULT_CLEAR, FUXI_VIBRATE_FAULT_AUTORES) ||
        io->read(io->context, 0xf00c, fault))
        return FUXI_HAPTIC_IO;
    (*cleared)++;
    return *fault ? FUXI_HAPTIC_FAULT : FUXI_HAPTIC_OK;
}

/* Fuxi 原機 DT 的閉迴路 brake-mode=1、brake-pattern=ff 3f 1f。
 * qcom-hv-haptics 的 haptics_enable_play 在開與關都保留 BRAKE_EN。 */
static inline int fuxi_vibrate_stock_brake(const FuxiHapticIo *io)
{
    static const uint8_t samples[] = {0xff, 0x3f, 0x1f, 0, 0, 0, 0, 0};
    uint8_t value = 0;
    if (fuxi_haptic_read_stable(io, 0xf050, &value) || (value >> 6) != 1) return 0;
    for (unsigned i = 0; i < sizeof(samples); i++) {
        if (fuxi_haptic_read_stable(io, 0xf170 + i, &value) || value != samples[i]) return 0;
    }
    return 1;
}

/* 第一次起振的約一個 LRA 週期提高振幅，後續視窗依原 envelope。 */
static inline unsigned fuxi_vibrate_attack_level(unsigned level)
{
    return level ? level + (255u - level) / 4u : 0;
}

static inline int fuxi_vibrate_start(const FuxiHapticIo *io, unsigned brake, unsigned *cleared)
{
    uint8_t fault = 0;
    if (io->read(io->context, 0xf00c, &fault)) return FUXI_HAPTIC_IO;
    int result = fuxi_vibrate_clear_autores(io, &fault, cleared);
    if (result) return result;
    return fuxi_haptic_write_checked(io, 0xf04c, FUXI_VIBRATE_PLAY | brake);
}

static inline int fuxi_vibrate_wait_disabled(const FuxiHapticIo *io, unsigned *polls)
{
    for (unsigned poll = 0; poll < FUXI_VIBRATE_STOP_POLLS; poll++) {
        uint8_t hpwr = 0;
        io->wait_us(io->context, 1000);
        (*polls)++;
        if (!io->read(io->context, 0xf00b, &hpwr) && !(hpwr & 3u)) return FUXI_HAPTIC_OK;
    }
    return FUXI_HAPTIC_TIMEOUT;
}

/* 停止；*toggled 表示有沒有走關開模組的退路。回傳非 0 就要鎖死執行期。 */
static inline int fuxi_vibrate_stop(const FuxiHapticIo *io, unsigned brake, unsigned *polls, unsigned *toggled)
{
    int result = fuxi_haptic_write_checked(io, 0xf04c, FUXI_VIBRATE_IDLE | brake);
    *polls = 0;
    *toggled = 0;
    if (result == FUXI_HAPTIC_HARDWARE) return result;
    if (!fuxi_vibrate_wait_disabled(io, polls)) {
        if (!result && brake) result = fuxi_haptic_write_checked(io, 0xf04c, FUXI_VIBRATE_IDLE);
        return result;
    }
    *toggled = 1;
    int off = fuxi_haptic_write_checked(io, 0xf046, 0);
    int on = fuxi_haptic_write_checked(io, 0xf046, FUXI_VIBRATE_MODULE_ON);
    int waited = fuxi_vibrate_wait_disabled(io, polls);
    if (waited) return waited;
    if (off) return off;
    if (on) return on;
    if (!result && brake) result = fuxi_haptic_write_checked(io, 0xf04c, FUXI_VIBRATE_IDLE);
    return result;
}
#endif
