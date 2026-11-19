#ifndef FUXI_HAPTIC_RTP_H
#define FUXI_HAPTIC_RTP_H
#include "fuxi_haptic_runtime.h"

/* HAP525，依同機 Android 的 spmi_write_begin 及 qcom_haptics_play。
 * 先送原機的 320 bytes，再由 worker 輪詢補上 56 bytes 的尾段。 */
enum { FUXI_RTP_BYTES = 376, FUXI_RTP_PREFILL = 320, FUXI_RTP_TEST_VMAX_LIMIT = 0x38 };
enum { FUXI_RTP_PCM_LIMIT = 96 };
static inline unsigned fuxi_rtp_pcm_gain(uint8_t *samples, unsigned length)
{
    unsigned peak = 0;
    for (unsigned i = 0; i < length; i++) {
        int sample = (int8_t)samples[i];
        unsigned magnitude = (unsigned)(sample < 0 ? -sample : sample);
        if (magnitude > peak) peak = magnitude;
    }
    if (!peak || peak > FUXI_RTP_PCM_LIMIT) return peak;

    /* Lift quiet packets by at most 6 dB while retaining relative peak
     * differences. Apply one gain to the signed waveform within the packet. */
    unsigned boosted = 2 * peak;
    unsigned ceiling = (peak + FUXI_RTP_PCM_LIMIT) / 2;
    if (boosted > ceiling) boosted = ceiling;
    for (unsigned i = 0; i < length; i++) {
        int sample = (int8_t)samples[i];
        samples[i] = (uint8_t)(sample * (int)boosted / (int)peak);
    }
    return boosted;
}

typedef struct {
    unsigned sent, polls, before_start, before_refill, last_fill;
} FuxiRtpStats;
typedef int (*FuxiRtpWrite)(unsigned address, const uint8_t *data, unsigned size);
typedef int (*FuxiRtpReadStatus)(uint8_t status[2]);

static inline int fuxi_rtp_register(FuxiRtpWrite write, const FuxiHapticIo *io,
    unsigned address, uint8_t value)
{
    uint8_t observed;
    return write(address, &value, 1) || io->read(io->context, address, &observed) || observed != value;
}

static inline int fuxi_rtp_fill(FuxiRtpWrite write, FuxiRtpReadStatus read_status, unsigned *fill)
{
    uint8_t status[2];
    /* Linux haptics_get_status_data reselects FIFO_REAL_TIME_STS on every read. */
    uint8_t xt = 0x80, selector = 5;
    if (write(0xf071, &xt, 1) || write(0xf070, &selector, 1)) return -1;
    if (read_status(status)) return -1;
    *fill = ((status[0] & 15u) << 8) | status[1];
    /* Android 實測 0x41/0x1c 表示還有 284 bytes；bit 6 不是筆數。 */
    return (status[0] & 0x20u) || *fill > 2048 ? -1 : 0;
}

static inline int fuxi_rtp_click(const FuxiHapticIo *io, FuxiRtpWrite write,
    FuxiRtpReadStatus read_status, const uint8_t *wave, unsigned length, unsigned vmax, FuxiRtpStats *stats)
{
    /* 兩個時脈計數 byte 取自此機的 Android 記錄，原值保存在 saved。
     * 樣點與 Android 相同。第一版沿用進入 iOS 時的 VMAX。 */
    static const struct { unsigned addr; uint8_t mask, value; } config[] = {
        {0xf071, 0xff, 0x80}, {0xf070, 0xff, 5}, {0xf124, 15, 10},
        {0xf05e, 0xff, 2}, {0xf05f, 0xff, 0x44}, {0xf072, 0x0c, 8},
        {0xf12a, 15, 0}, {0xf04a, 4, 0}
    };
    uint8_t saved[sizeof(config) / sizeof(config[0])], size_code, memop, fault, saved_vmax;
    unsigned prepared = 0, fill = 0;
    int result = -1, attempted_play = 0, owns_fifo = 0, changed_vmax = 0, restore_error = 0;
    uint8_t flush = 1, zero = 0;
    *stats = (FuxiRtpStats){.before_start = ~0u, .before_refill = ~0u, .last_fill = ~0u};
    if (vmax > FUXI_RTP_TEST_VMAX_LIMIT || !wave || length < 8 || length > FUXI_RTP_BYTES ||
        (length & 3u) || wave[length - 1] || wave[length - 2] || wave[length - 3] ||
        wave[length - 4] || fuxi_haptic_idle(io) ||
        fuxi_haptic_read_stable(io, 0xf1a0, &size_code) || (size_code & 15) != 15 ||
        fuxi_haptic_read_stable(io, 0xf12d, &memop) || memop) return -1;
    for (unsigned i = 0; i < sizeof(saved); i++) {
        if (fuxi_haptic_read_stable(io, config[i].addr, &saved[i])) goto cleanup;
        uint8_t value = (saved[i] & ~config[i].mask) | config[i].value;
        prepared++;
        if (value != saved[i] && fuxi_rtp_register(write, io, config[i].addr, value)) goto cleanup;
    }
    if (fuxi_rtp_fill(write, read_status, &fill) || fill) goto cleanup;
    if (vmax) {
        if (fuxi_haptic_read_stable(io, 0xf048, &saved_vmax) ||
            !saved_vmax || saved_vmax > FUXI_VIBRATE_VMAX_LIMIT) goto cleanup;
        changed_vmax = 1;
        if (fuxi_rtp_register(write, io, 0xf048, (uint8_t)vmax) || fuxi_haptic_idle(io)) goto cleanup;
    }
    owns_fifo = 1;
    if (write(0xf12d, &flush, 1) || fuxi_rtp_register(write, io, 0xf12d, 0)) goto cleanup;
    unsigned prefill = length < FUXI_RTP_PREFILL ? length : FUXI_RTP_PREFILL;
    for (; stats->sent < prefill; stats->sent += 4) {
        if (write(0xf120, wave + stats->sent, 4)) goto cleanup;
    }
    if (fuxi_rtp_fill(write, read_status, &fill)) goto cleanup;
    stats->before_start = fill;
    if (fill != prefill) goto cleanup;
    attempted_play = 1;
    if (fuxi_rtp_register(write, io, 0xf04c, 0x80)) goto cleanup;
    for (stats->polls = 1; stats->polls <= 32; ++stats->polls) {
        io->wait_us(io->context, 1000);
        if (io->read(io->context, 0xf00c, &fault) || fault) break;
        if (fuxi_rtp_fill(write, read_status, &fill)) goto cleanup;
        stats->last_fill = fill;
        if (stats->sent < length) {
            stats->before_refill = fill;
            if (fill > FUXI_RTP_PREFILL || !fill) break;
            for (; stats->sent < length; stats->sent += 4) {
                if (write(0xf120, wave + stats->sent, 4)) goto cleanup;
            }
        } else if (!fill) { result = 0; break; }
    }
cleanup:
    if (attempted_play) {
        unsigned stop_polls = 0;
        if (fuxi_rtp_register(write, io, 0xf04c, 0)) restore_error = 1;
        if (fuxi_vibrate_wait_disabled(io, &stop_polls)) restore_error = 1;
    }
    if (owns_fifo) {
        /* 任一個失敗的寫交易仍可能已進硬體；沖掉未播完的波形。 */
        uint8_t reset_fifo = 1;
        if (write(0xf12d, &reset_fifo, 1)) restore_error = 1;
        if (fuxi_rtp_register(write, io, 0xf12d, zero)) restore_error = 1;
    }
    while (prepared) {
        unsigned i = --prepared;
        if (((saved[i] ^ config[i].value) & config[i].mask) &&
            fuxi_rtp_register(write, io, config[i].addr, saved[i])) restore_error = 1;
    }
    if (changed_vmax && fuxi_rtp_register(write, io, 0xf048, saved_vmax)) restore_error = 1;
    if (attempted_play && fuxi_rtp_register(write, io, 0xf04c, FUXI_VIBRATE_IDLE)) restore_error = 1;
    return restore_error ? -2 : result;
}
#endif
