/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* FUXI 下方喇叭出聲短測（fuxiamp／ADT fuxi,amp = 7，搭配 fuxi,audio-spf = 5）的雙方交握。
 * fuxi_amp.c 的功放執行緒和 fuxi_audio_spf.c 的 ADSP 擁有者執行緒只透過 fuxi_amp_tone 這個狀態字溝通；
 * 每次轉換都用 CAS，任何一方出錯就改成 FAILED，另一方看到後自行收尾（功放：靜音、下電、回 reset；PCM：停送正弦、關圖）。
 *
 *   IDLE     不是 mode 7，PCM 圖照 mode 5 只送靜音
 *   PENDING  mode 7 已啟動，功放還在載入與核對保護韌體（PCM 圖停在 GATE，不開 TDM）
 *   READY    保護韌體試跑通過且 CSPL_STATE／ERRORNO／CAL_STATUS 正常，TDM 腳位已切換，輸出仍關，等 TDM 時脈
 *   CLOCKED  PCM 圖已 START 並播完 100 ms 靜音，功放可設旁路與上電
 *   ON       功放上電、-30 dB；PCM 開始送 1 秒正弦
 *   DRAIN    正弦的每個週期都已回 WRITE_DONE，PCM 改送靜音，功放做最後檢查與下電
 *   OFF      功放已下電，PCM 收尾關圖
 *   FAILED   任一方放棄
 *
 * 正弦表、長度與淡入淡出逐項沿用早期 Fuxi shim/fuxi-probe/audio.c au_sine／au_tone_fill（2 kHz、-20 dBFS、1 秒、
 * 各 40 ms 淡入淡出，四個槽同值），2026-11-19 對照。 */
#ifndef FUXI_AMP_TONE_H
#define FUXI_AMP_TONE_H

#include <stdint.h>

enum {
    FUXI_TONE_IDLE = 0,
    FUXI_TONE_PENDING = 1,
    FUXI_TONE_READY = 2,
    FUXI_TONE_CLOCKED = 3,
    FUXI_TONE_ON = 4,
    FUXI_TONE_DRAIN = 5,
    FUXI_TONE_OFF = 6,
    FUXI_TONE_FAILED = 7,
};

#define FUXI_TONE_FRAMES 48000u /* 一秒 */
#define FUXI_TONE_FADE 1920u    /* 40 ms */

/* fuxi_amp.c 定義。 */
extern uint32_t fuxi_amp_tone;

static inline uint32_t fuxi_amp_tone_get(void)
{
    return __atomic_load_n(&fuxi_amp_tone, __ATOMIC_ACQUIRE);
}

/* from → to；狀態已被另一方改掉就回 0。 */
static inline int fuxi_amp_tone_move(uint32_t from, uint32_t to)
{
    return __atomic_compare_exchange_n(&fuxi_amp_tone, &from, to, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

/* PENDING 到 ON 之間的任何狀態改成 FAILED，回傳原本的狀態。DRAIN 由功放自己收尾，OFF／FAILED／IDLE 不動。 */
static inline uint32_t fuxi_amp_tone_abandon(void)
{
    uint32_t state = fuxi_amp_tone_get();
    while (state >= FUXI_TONE_PENDING && state <= FUXI_TONE_ON &&
        !__atomic_compare_exchange_n(&fuxi_amp_tone, &state, FUXI_TONE_FAILED, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
    }
    return state;
}

/* 第 frame 個取樣（32 位元，四個槽同值）；超過一秒回 0。 */
static inline int32_t fuxi_amp_tone_sample(uint32_t frame)
{
    static const int32_t sine[24] = {0, 55580967, 107374182, 151850025, 185977539, 207430992, 214748365, 207430992,
        185977539, 151850025, 107374182, 55580967, 0, -55580967, -107374182, -151850025, -185977539, -207430992,
        -214748365, -207430992, -185977539, -151850025, -107374182, -55580967};
    if (frame >= FUXI_TONE_FRAMES) {
        return 0;
    }
    const uint32_t left = FUXI_TONE_FRAMES - frame;
    const uint32_t gain = frame < FUXI_TONE_FADE ? frame : left < FUXI_TONE_FADE ? left : FUXI_TONE_FADE;
    return (int32_t)((int64_t)sine[frame % 24u] * gain / FUXI_TONE_FADE);
}

#endif /* FUXI_AMP_TONE_H */
