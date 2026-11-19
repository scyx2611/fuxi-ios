#ifndef FUXI_HAPTIC_PULSE_H
#define FUXI_HAPTIC_PULSE_H
#include <stdint.h>

enum {
    FUXI_HAPTIC_CONFIG = 0xf000,
    FUXI_HAPTIC_PATTERN = 0xf100,
    FUXI_HAPTIC_BOOST = 0xf200,
    FUXI_HAPTIC_VMAX_CODE = 24,
    FUXI_HAPTIC_AMPLITUDE = 64,
    FUXI_HAPTIC_SAMPLES = 8,
    FUXI_HAPTIC_STOP_POLLS = 20,
    FUXI_HAPTIC_OK = 0,
    FUXI_HAPTIC_INVALID = -1,
    FUXI_HAPTIC_HARDWARE = -2,
    FUXI_HAPTIC_BUSY = -3,
    FUXI_HAPTIC_IO = -4,
    FUXI_HAPTIC_VERIFY = -5,
    FUXI_HAPTIC_TIMEOUT = -6,
    FUXI_HAPTIC_RESTORE = -7,
    FUXI_HAPTIC_LOCKED = -8,
    FUXI_HAPTIC_ONCE = -9,
    FUXI_HAPTIC_FAULT = -10,
    FUXI_HAPTIC_FINITE_UNVERIFIED = -11
};

typedef struct {
    void *context;
    int (*owner)(void *, unsigned, unsigned *);
    int (*read)(void *, unsigned, uint8_t *);
    int (*write)(void *, unsigned, uint8_t);
    void (*wait_us)(void *, unsigned);
    unsigned pattern1_finite_verified;
} FuxiHapticIo;

typedef struct {
    unsigned attempted;
    unsigned start_attempted;
    unsigned started;
    unsigned stopped;
    unsigned restored;
    unsigned period_us;
    unsigned probed;
    int failed;
} FuxiHapticState;

static inline int fuxi_haptic_writable(unsigned address)
{
    return address == 0xf046 || address == 0xf048 || address == 0xf04b ||
        address == 0xf04c || address == 0xf04e || address == 0xf063 || address == 0xf066 ||
        address == 0xf126 || address == 0xf130 || address == 0xf131 ||
        (address >= 0xf134 && address <= 0xf14b);
}

static inline int fuxi_haptic_write_checked(const FuxiHapticIo *io, unsigned address, uint8_t value)
{
    unsigned owner = 7;
    uint8_t actual = 0;
    if (!fuxi_haptic_writable(address) ||
        io->owner(io->context, address >> 8, &owner) || owner != 0)
        return FUXI_HAPTIC_HARDWARE;
    if (io->write(io->context, address, value) || io->read(io->context, address, &actual))
        return FUXI_HAPTIC_IO;
    return actual == value ? FUXI_HAPTIC_OK : FUXI_HAPTIC_VERIFY;
}

static inline int fuxi_haptic_read_stable(const FuxiHapticIo *io, unsigned address, uint8_t *value)
{
    uint8_t repeated = 0;
    if (io->read(io->context, address, value) || io->read(io->context, address, &repeated))
        return FUXI_HAPTIC_IO;
    return *value == repeated ? FUXI_HAPTIC_OK : FUXI_HAPTIC_VERIFY;
}

static inline int fuxi_haptic_stop(const FuxiHapticIo *io, FuxiHapticState *state, uint8_t module_off)
{
    state->stopped = 0;
    int result = fuxi_haptic_write_checked(io, 0xf04c, 2);
    int disabled = fuxi_haptic_write_checked(io, 0xf046, module_off);
    if (!result) result = disabled;
    for (unsigned poll = 0; poll < FUXI_HAPTIC_STOP_POLLS; poll++) {
        uint8_t play = 0, module = 0, hpwr = 0;
        io->wait_us(io->context, 1000);
        if (io->read(io->context, 0xf04c, &play) ||
            io->read(io->context, 0xf046, &module) ||
            io->read(io->context, 0xf00b, &hpwr)) {
            if (!result) result = FUXI_HAPTIC_IO;
            continue;
        }
        if (play == 2 && module == module_off && !(hpwr & 3u)) {
            state->stopped = 1;
            return result;
        }
        if ((play != 2 || module != module_off) && !result) result = FUXI_HAPTIC_VERIFY;
    }
    return FUXI_HAPTIC_TIMEOUT;
}

static inline int fuxi_haptic_idle(const FuxiHapticIo *io)
{
    uint8_t fault = 0, hpwr = 0, play = 0, follow = 0, boost = 0, status = 0, vset = 0;
    if (io->read(io->context, 0xf00c, &fault) || io->read(io->context, 0xf00b, &hpwr) ||
        io->read(io->context, 0xf04c, &play) || io->read(io->context, 0xf241, &follow) ||
        io->read(io->context, 0xf246, &boost) || io->read(io->context, 0xf20b, &status) ||
        io->read(io->context, 0xf068, &vset)) return FUXI_HAPTIC_IO;
    if (fault) return FUXI_HAPTIC_FAULT;
    if ((follow & 0xe0u) != 0xe0u || (vset & 3u)) return FUXI_HAPTIC_HARDWARE;
    return (hpwr & 3u) || (play & 0x80u) || (boost & 0x80u) || (status & 1u) ?
        FUXI_HAPTIC_BUSY : FUXI_HAPTIC_OK;
}

static inline int fuxi_haptic_pulse(const FuxiHapticIo *io, FuxiHapticState *state)
{
    static const unsigned identities[] = {
        0xf001, 0xf004, 0xf005, 0xf101, 0xf104, 0xf105,
        0xf200, 0xf201, 0xf204, 0xf205
    };
    static const uint8_t expected[] = {4, 0x15, 0x0a, 4, 0x15, 0x0b, 2, 0, 0x31, 1};
    static const unsigned registers[] = {
        0xf046, 0xf048, 0xf04b, 0xf04c, 0xf04e, 0xf063, 0xf130, 0xf131,
        0xf134, 0xf135, 0xf136, 0xf137, 0xf138, 0xf139, 0xf13a, 0xf13b,
        0xf13c, 0xf13d, 0xf13e, 0xf13f, 0xf140, 0xf141, 0xf142, 0xf143,
        0xf144, 0xf145, 0xf146, 0xf147, 0xf148, 0xf149, 0xf14a, 0xf14b
    };
    static const unsigned setup_order[] = {0, 3, 2, 4, 1, 5};
    static const unsigned restore_order[] = {1, 5, 3, 4, 2, 0};
    uint8_t saved[32], staged[32], tlra_high = 0, tlra_low = 0, waveform = 0;
    uint8_t value = 0;
    int result = FUXI_HAPTIC_OK;
    if (!io || !state || !io->owner || !io->read || !io->write || !io->wait_us)
        return FUXI_HAPTIC_INVALID;
    if (state->failed) return FUXI_HAPTIC_LOCKED;
    if (state->attempted) return FUXI_HAPTIC_ONCE;
    state->attempted = 1;
    for (unsigned index = 0; index < 3; index++) {
        unsigned owner = 7;
        if (io->owner(io->context, 0xf0 + index, &owner) || owner != (index == 2 ? 3u : 0u)) {
            result = FUXI_HAPTIC_HARDWARE;
            goto refused;
        }
    }
    for (unsigned index = 0; index < sizeof(expected); index++) {
        result = fuxi_haptic_read_stable(io, identities[index], &value);
        if (result) goto refused;
        if (value != expected[index]) {
            result = FUXI_HAPTIC_HARDWARE;
            goto refused;
        }
    }
    result = fuxi_haptic_idle(io);
    if (result) goto refused;
    result = fuxi_haptic_read_stable(io, 0xf05c, &tlra_high);
    if (result) goto refused;
    result = fuxi_haptic_read_stable(io, 0xf05d, &tlra_low);
    if (result) goto refused;
    result = fuxi_haptic_read_stable(io, 0xf049, &waveform);
    if (result) goto refused;
    state->period_us = (((unsigned)tlra_high & 0x0fu) * 256u + tlra_low) * 5u;
    if (!state->period_us || (waveform & 0x13u) != 1) {
        result = FUXI_HAPTIC_HARDWARE;
        goto refused;
    }
    for (unsigned index = 0; index < 32; index++) {
        result = fuxi_haptic_read_stable(io, registers[index], &saved[index]);
        if (result) goto refused;
        staged[index] = saved[index];
    }
    result = fuxi_haptic_idle(io);
    if (result) goto refused;
    if (saved[3] & 0x80u) {
        result = FUXI_HAPTIC_BUSY;
        goto refused;
    }
    state->probed = 1;
    if (!io->pattern1_finite_verified) {
        result = FUXI_HAPTIC_FINITE_UNVERIFIED;
        goto refused;
    }
    staged[0] &= 0x7f;
    staged[1] = FUXI_HAPTIC_VMAX_CODE;
    staged[2] |= 0x10;
    staged[3] = 2;
    staged[4] &= 0x3f;
    staged[5] &= 0x7f;
    staged[6] = tlra_high & 0x0fu;
    staged[7] = tlra_low;
    for (unsigned index = 0; index < FUXI_HAPTIC_SAMPLES; index++) {
        staged[8 + index * 3] = 0;
        staged[9 + index * 3] = 0;
        staged[10 + index * 3] = index < 4 ? FUXI_HAPTIC_AMPLITUDE : 0;
    }
    for (unsigned index = 0; index < sizeof(setup_order) / sizeof(setup_order[0]); index++) {
        unsigned selected = setup_order[index];
        result = fuxi_haptic_write_checked(io, registers[selected], staged[selected]);
        if (result) goto cleanup;
    }
    result = fuxi_haptic_idle(io);
    if (result) goto cleanup;
    for (unsigned index = 6; index < 32; index++) {
        result = fuxi_haptic_write_checked(io, registers[index], staged[index]);
        if (result) goto cleanup;
    }
    for (unsigned index = 0; index < 32; index++) {
        if (io->read(io->context, registers[index], &value)) {
            result = FUXI_HAPTIC_IO;
            goto cleanup;
        }
        if (value != staged[index]) {
            result = FUXI_HAPTIC_VERIFY;
            goto cleanup;
        }
    }
    if (io->read(io->context, 0xf05c, &value) || value != tlra_high ||
        io->read(io->context, 0xf05d, &value) || value != tlra_low) {
        result = FUXI_HAPTIC_VERIFY;
        goto cleanup;
    }
    result = fuxi_haptic_idle(io);
    if (result) goto cleanup;
    result = fuxi_haptic_write_checked(io, 0xf046, saved[0] | 0x80u);
    if (result) goto cleanup;
    result = fuxi_haptic_idle(io);
    if (result) goto cleanup;
    state->start_attempted = 1;
    result = fuxi_haptic_write_checked(io, 0xf04c, 0x82);
    if (result) goto cleanup;
    state->started = 1;
    io->wait_us(io->context, state->period_us * FUXI_HAPTIC_SAMPLES + 10000u);
    if (io->read(io->context, 0xf00c, &value)) result = FUXI_HAPTIC_IO;
    else if (value) result = FUXI_HAPTIC_FAULT;
cleanup:
    {
        int stop_result = fuxi_haptic_stop(io, state, staged[0]);
        if (!state->stopped) {
            if (state->start_attempted)
                io->wait_us(io->context, state->period_us * FUXI_HAPTIC_SAMPLES + 10000u);
            result = FUXI_HAPTIC_TIMEOUT;
            goto refused;
        }
        if (!result) result = stop_result;
    }
    for (unsigned index = 6; index < 32; index++) {
        if (fuxi_haptic_write_checked(io, registers[index], saved[index])) goto restore_failed;
    }
    for (unsigned index = 0; index < sizeof(restore_order) / sizeof(restore_order[0]); index++) {
        unsigned selected = restore_order[index];
        if (fuxi_haptic_write_checked(io, registers[selected], saved[selected])) goto restore_failed;
    }
    state->restored = 1;
    state->failed = result != FUXI_HAPTIC_OK;
    return result;
restore_failed:
    fuxi_haptic_stop(io, state, staged[0]);
    result = FUXI_HAPTIC_RESTORE;
refused:
    state->failed = 1;
    return result;
}
#endif
