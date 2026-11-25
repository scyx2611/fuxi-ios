/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <stddef.h>
#include "SensorsCompass.h"
#include "SensorsFixed.h"

static const uint16_t kAtanTab[65] = {
        0,   895,  1790,  2684,  3576,  4467,  5356,  6242,
     7125,  8005,  8881,  9752, 10620, 11482, 12339, 13191,
    14036, 14876, 15709, 16535, 17354, 18166, 18970, 19767,
    20556, 21337, 22109, 22874, 23629, 24376, 25115, 25844,
    26565, 27277, 27979, 28673, 29358, 30033, 30700, 31357,
    32005, 32645, 33275, 33896, 34509, 35112, 35707, 36293,
    36870, 37439, 37999, 38550, 39094, 39629, 40156, 40675,
    41186, 41689, 42184, 42672, 43152, 43625, 44091, 44549,
    45000,
};

enum {
    kQ15One = 32768,
    kQ16One = 65536,
    kQ30One = 1073741824,
    kFloatOneBits = 0x3f800000u,
    kCompassVersion = 2u,
    kCompassCal3D = 5u,
    kCompassFlagGravity = 0x0004u,
    kCompassFlagYaw = 0x0008u,
    kCompassFlagBias = 0x0010u,
    kCompassFlagInit = 0x0100u,
    kCompassFlagGyroCal = 0x0200u,
    kCompassFlagCompassCal = 0x0400u,
    kCompassFlagNorth = 0x2000u,
    kQTimerTicksPer10Us = 192,
    kGyroDtMinUs = 1000,
    kGyroDtMaxUs = 100000,
    kGyroMaxQ16 = 64 * 65536,
    kRotvSameEpsQ30 = 200000,
    kRotvSlowDeltaQ30 = 2000000,
    kRotvUnstickDeltaQ30 = 4000000,
    kRotvStuckThreshold = 25u,
    kRotvResumeThreshold = 3u,
    kRotvGyroMovingQ16 = 9830,
    kGyroStillL1Q16 = 3932,          /* ~0.06 rad/s (~3.4 deg/s) L1 stationary threshold */
    kRotvGravCosMinQ15 = 29698,
    kRotvGravAccelMinQ16 = 49152,
    kRotvGravAccelMaxQ16 = 81920,
    kFusionAccelMinQ16 = 55706,      /* 0.85 g */
    kFusionAccelMaxQ16 = 75366,      /* 1.15 g */
    kFusionAccelValidMinQ16 = 16384, /* 0.25 g */
    kBiasHandoffSteps = 75u,         /* ~1.5 s faster blend at 50 Hz after bias source handoff */
    kStationaryYawLockSteps = 100u,  /* ~1 s at 100 Hz gyro before locking stationary yaw */
    kZuptAccelMinQ16 = 58982,        /* 0.90 g */
    kZuptAccelMaxQ16 = 72090,        /* 1.10 g */
    kZuptAccelTolQ16 = 2621,         /* 0.04 g stationary accel stability tolerance */
    kZuptMaxBiasQ16 = 1311,          /* 0.020 rad/s (~1.15 deg/s) max learnable gyro zero-rate bias */
    kZuptJitterQ16 = 524,            /* 0.008 rad/s (~0.46 deg/s) stationary gyro jitter limit */
    kZuptDeadbandQ16 = 262,          /* 0.004 rad/s (~0.23 deg/s) stationary integration deadband */
    kZuptStillSamples = 50u,         /* ~0.5 s at 100 Hz before ZUPT bias tracking & deadband */
    kZuptFastLearnSamples = 32u,     /* fast initial bias convergence window (div=8 -> div=64) */
    kEarthNormMinMut = 22000u,       /* 22.0 uT global geomagnetic floor */
    kEarthNormMaxMut = 68000u,       /* 68.0 uT global geomagnetic ceiling */
    kEarthHorizMinMut = 12000u,      /* 12.0 uT minimum horizontal geomagnetic field */
    kEarthHorizMaxMut = 58000u,      /* 58.0 uT max horizontal field (covers indoor soft-iron + margin while catching severe stale bias) */
    kEarthHardNormMinMut = 18000u,   /* 18.0 uT instant hard floor */
    kEarthHardNormMaxMut = 74000u,   /* 74.0 uT instant hard ceiling */
    kEarthHardHorizMinMut = 9000u,   /* 9.0 uT instant hard horizontal floor */
    kEarthHardHorizMaxMut = 64000u,  /* 64.0 uT instant hard horizontal ceiling */
    kEarthRangeDebounce = 3u,        /* 3 consecutive samples (60 ms) for marginal earth-range noise immunity */
    kMagCalFastDriftMut = 15000u,    /* 15.0 uT immediate clean adoption band for live mag_cal */
    kMagCalRemagMaxDriftMut = 120000u,/* 120.0 uT max chassis remagnetization eligible for verified acc=3 */
    kMagCalCandStableMut = 4000u,    /* 4.0 uT max jitter between consecutive acc=3 updates */
    kMagCalCandNormMinMut = 24000u,  /* 24.0 uT strict candidate |B| floor */
    kMagCalCandNormMaxMut = 62000u,  /* 62.0 uT strict candidate |B| ceiling */
    kMagCalCandHorizMinMut = 12000u, /* 12.0 uT strict candidate H floor */
    kMagCalCandHorizMaxMut = 52000u, /* 52.0 uT candidate H ceiling (covers indoor desk soft-iron; 3D rotation invariance proves bias) */
    kMagCalCandTolNormMut = 4500u,   /* 4.5 uT candidate |B| stability band */
    kMagCalCandTolUpMut = 5500u,     /* 5.5 uT candidate B_up stability band */
    kMagCalCandTolWorldMut = 5000u,  /* 5.0 uT candidate 3D B_world invariance band */
    kMagCalCandBootSamples = 40u,    /* 0.8 s clean proof when escaping invalid boot PCB bias */
    kMagCalCandCleanSamples = 50u,   /* 1.0 s clean proof in undisturbed environment */
    kMagCalCandRotSamples = 75u,     /* 1.5 s proof required for rotation-verified remagnetization recovery */
    kMagCalCandRotTurnMdeg = 60000u, /* 60.0 deg real gyro turn proving candidate bias is body-fixed */
    kMagCalCandRetroSamples = 10u,   /* 0.2 s live confirmation for retroactive pose_hist 3D proof */
    kPoseHistMaxAgeSamples = 1500u,  /* 30 s window at 50 Hz for retroactive figure-8 snapshots */
    kPoseHistMinSnaps = 4u,          /* >= 4 distinct recent poses required for retroactive proof */
    kPoseHistTolWorldMut = 6000u,    /* 6.0 uT 3D B_world invariance across retroactive snapshots */
    kPoseHistColdMinSnaps = 3u,      /* >= 3 distinct poses for cold-boot stale-default replacement */
    kPoseHistColdTolWorldMut = 5500u,/* 5.5 uT strict candidate 3D B_world invariance at cold boot */
    kPoseHistColdOldSpreadMut = 9000u,/* 9.0 uT old-default world spread proving stale cold-boot bias */
    kQuatCos7p5Q30 = 1064556000,     /* cos(7.5 deg) in Q30 => >= 15 deg rotation between snapshots */
    kQuatCos17p5Q30 = 1024055000,    /* cos(17.5 deg) in Q30 => >= 35 deg pairwise spread for cold boot */
    kQuatCos30Q30 = 929888000,       /* cos(30 deg) in Q30 => >= 60 deg pairwise spread proof */
    kPoseHistTiltCosMinQ15 = 29698,  /* cos(25 deg) in Q15 => >= 25 deg 3D tilt diversity constrains b_z */
    kEarthBootGyroMaxQ16 = 36045,    /* ~0.55 rad/s (~31.5 deg/s) max gyro L1 during earth_ref bootstrap */
    kMagBiasPersistMagic = 0x46584d42u, /* "FXMB" persisted hard-iron bias record magic */
    kMagBiasPersistVersion = 2u,     /* v2: requires post-handoff verified earth_ref + 3D tilt cold-boot proof */
    kMagBiasPersistMaxQ16 = 2000 * 65536, /* +/- 2000 uT physical sanity bound */
    kGyroGapResetTicks = 19200000u,  /* 1.0 s QTimer gap resets stale yaw while keeping learned biases */
    kMagStillJumpMut = 10000u,       /* 10.0 uT instantaneous stationary mag jump threshold */
    kEarthRefMinSamples = 24u,       /* ~0.48 s at 50 Hz before enforcing local ref anomaly gate */
    kEarthNormDevMut = 6500u,        /* 6.5 uT deviation from learned local |B| */
    kEarthUpDevMut = 7500u,          /* 7.5 uT deviation from learned local B_up */
    kEarthVecStillDevMut = 6800u,    /* 6.8 uT 3D world-frame vector deviation while stationary (debounced; absorbs hand-to-desk delta) */
    kEarthVecMoveDevMut = 7500u,     /* 7.5 uT 3D world-frame vector deviation while rotating (debounced) */
    kEarthVecInstantDevMut = 9500u,  /* 9.5 uT instant sample-#1 3D world-frame disturbance trip */
    kEarthVecDebounce = 3u,          /* 3 consecutive samples (60 ms) for moderate vec_err noise immunity */
    kEarthRefTrackMaxVecMut = 4000u, /* 4.0 uT max 3D vec_err eligible for slow earth_ref tracking */
    kEarthReanchorMaxDevMut = 25000u,/* 25.0 uT max room-change shift eligible for re-anchor */
    kEarthReanchorMaxWestMut = 5000u,/* 5.0 uT max |B_west| so room re-anchor never accepts deflected heading */
    kEarthReanchorTolNormMut = 3000u,/* 3.0 uT stability band during room-change re-anchor */
    kEarthReanchorTolUpMut = 4000u,  /* 4.0 uT B_up stability band during room-change re-anchor */
    kEarthReanchorSamples = 150u,    /* 3.0 s steady heading-consistent clean field to re-anchor local ref */
    kRelockTolVecMut = 4500u,        /* 4.5 uT 3D B_world invariance band for rotation-verified uniform-field relock */
    kRelockSteadySamples = 250u,     /* 5.0 s steady clean field required for rotation-verified relock */
    kRelockMinTurnMdeg = 90000u,     /* 90.0 deg real gyro turn required to prove field is spatially uniform */
    kMagDisturbHoldSamples = 75u,    /* 1.5 s clean hold at 50 Hz after disturbance */
    kMagCalQuarantineSamples = 400u, /* 8.0 s clean quarantine at 50 Hz against poisoned mag_cal */
    kMagYawInnovGateMdeg = 8000,     /* 8.0 deg gyro-vs-mag yaw innovation gate */
    kMagYawDisturbTrip = 3u,         /* 3 consecutive stationary yaw rejections trip disturbance hold */
    kMagInnovQuarantineTrip = 5u,    /* 5 consecutive yaw rejections quarantine mag_cal */
    kMagDistReasonEarth = 0x01u,
    kMagDistReasonNorm = 0x02u,
    kMagDistReasonUp = 0x04u,
    kMagDistReasonVec = 0x08u,
    kMagDistReasonJump = 0x10u,
    kMagDistReasonYaw = 0x20u,
};

static uint64_t
isqrt64(uint64_t value)
{
    uint64_t root = 0;
    uint64_t bit = 1ull << 62;
    while (bit > value) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (value >= root + bit) {
            value -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}

static uint32_t
abs_i32_to_u32(int32_t val)
{
    if (val >= 0) {
        return (uint32_t)val;
    }
    return (uint32_t)(-(int64_t)val);
}

static int32_t
atan_ratio_mdeg(uint32_t num, uint32_t den)
{
    if (den == 0 || num == 0) {
        return 0;
    }
    if (num >= den) {
        return 45000;
    }
    const uint64_t scaled = (uint64_t)num * 64ull;
    const uint32_t index = (uint32_t)(scaled / (uint64_t)den);
    if (index >= 64u) {
        return 45000;
    }
    const uint64_t rem = scaled - (uint64_t)index * (uint64_t)den;
    const uint32_t low = kAtanTab[index];
    const uint32_t high = kAtanTab[index + 1u];
    return (int32_t)(low + ((rem * (uint64_t)(high - low) + ((uint64_t)den / 2ull)) / (uint64_t)den));
}

int32_t
sns_compass_atan2_mdeg(int32_t y_val, int32_t x_val)
{
    const uint32_t abs_x = abs_i32_to_u32(x_val);
    const uint32_t abs_y = abs_i32_to_u32(y_val);
    if (abs_x == 0 && abs_y == 0) {
        return 0;
    }
    int32_t angle;
    if (abs_y <= abs_x) {
        angle = atan_ratio_mdeg(abs_y, abs_x);
    } else {
        angle = 90000 - atan_ratio_mdeg(abs_x, abs_y);
    }
    if (x_val >= 0) {
        return (y_val >= 0) ? angle : -angle;
    }
    return (y_val >= 0) ? (180000 - angle) : -(180000 - angle);
}

uint32_t
sns_compass_float_signed(int64_t numerator, uint64_t denominator)
{
    if (numerator == 0 || denominator == 0) {
        return 0u;
    }
    const uint32_t sign = (numerator < 0) ? 0x80000000u : 0u;
    uint64_t mag = (numerator < 0)
        ? ((uint64_t)(-(numerator + 1ll)) + 1ull)
        : (uint64_t)numerator;
    uint64_t den = denominator;
    while ((mag >> 32) != 0 || (den >> 32) != 0) {
        mag >>= 1;
        den >>= 1;
    }
    if (mag == 0 || den == 0) {
        return 0u;
    }
    return sign | sfx_float_from_ratio(mag, den);
}

static void
write_le16(uint8_t *dst, uint16_t val)
{
    dst[0] = (uint8_t)(val & 0xffu);
    dst[1] = (uint8_t)((val >> 8) & 0xffu);
}

static void
write_le32(uint8_t *dst, uint32_t val)
{
    dst[0] = (uint8_t)(val & 0xffu);
    dst[1] = (uint8_t)((val >> 8) & 0xffu);
    dst[2] = (uint8_t)((val >> 16) & 0xffu);
    dst[3] = (uint8_t)((val >> 24) & 0xffu);
}

static uint32_t
read_le32(const uint8_t *src)
{
    return (uint32_t)src[0] |
           ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) |
           ((uint32_t)src[3] << 24);
}

static uint32_t
fnv1a_32(const uint8_t *data, unsigned len)
{
    uint32_t h = 2166136261u;
    for (unsigned idx = 0; idx < len; ++idx) {
        h ^= (uint32_t)data[idx];
        h *= 16777619u;
    }
    return h;
}

static void
write_le64(uint8_t *dst, uint64_t val)
{
    write_le32(dst, (uint32_t)(val & 0xffffffffull));
    write_le32(dst + 4, (uint32_t)(val >> 32));
}

static int
is_finite_float_bits(uint32_t bits)
{
    return ((bits >> 23) & 0xffu) != 0xffu;
}

static int
normalize_quat_q30(const int64_t raw[4], int32_t out_q30[4])
{
    int64_t scaled[4];
    uint64_t norm2 = 0;
    for (unsigned idx = 0; idx < 4u; ++idx) {
        int64_t val = raw[idx];
        if (val > kQ30One) {
            val = kQ30One;
        } else if (val < -kQ30One) {
            val = -kQ30One;
        }
        scaled[idx] = val;
        const uint64_t mag = (val < 0) ? (uint64_t)(-val) : (uint64_t)val;
        norm2 += mag * mag;
    }
    const uint64_t norm = isqrt64(norm2);
    if (norm < (1ull << 24)) {
        return -1;
    }
    const int64_t sign_flip = (scaled[3] < 0) ? -1ll : 1ll;
    for (unsigned idx = 0; idx < 4u; ++idx) {
        const int64_t num = scaled[idx] * sign_flip;
        int64_t comp;
        if (num >= 0) {
            comp = (int64_t)((((uint64_t)num << 30) + (norm / 2ull)) / norm);
        } else {
            comp = -(int64_t)((((uint64_t)(-num) << 30) + (norm / 2ull)) / norm);
        }
        if (comp > kQ30One) {
            comp = kQ30One;
        } else if (comp < -kQ30One) {
            comp = -kQ30One;
        }
        out_q30[idx] = (int32_t)comp;
    }
    return 0;
}

static int
normalize_vec3_q15(const int64_t vec[3], int32_t out_q15[3])
{
    int64_t work[3] = {vec[0], vec[1], vec[2]};
    while ((work[0] > 0x7fffffffll || work[0] < -0x7fffffffll) ||
           (work[1] > 0x7fffffffll || work[1] < -0x7fffffffll) ||
           (work[2] > 0x7fffffffll || work[2] < -0x7fffffffll)) {
        work[0] >>= 1;
        work[1] >>= 1;
        work[2] >>= 1;
    }
    uint64_t norm2 = 0;
    for (unsigned idx = 0; idx < 3u; ++idx) {
        const uint64_t mag = (work[idx] < 0) ? (uint64_t)(-work[idx]) : (uint64_t)work[idx];
        norm2 += mag * mag;
    }
    const uint64_t norm = isqrt64(norm2);
    if (norm == 0) {
        return -1;
    }
    for (unsigned idx = 0; idx < 3u; ++idx) {
        const int64_t comp = (work[idx] * (int64_t)kQ15One) / (int64_t)norm;
        out_q15[idx] = (comp > kQ15One) ? kQ15One : (comp < -kQ15One) ? -kQ15One : (int32_t)comp;
    }
    return 0;
}

static void
cross_q15(const int32_t lhs[3], const int32_t rhs[3], int64_t out[3])
{
    out[0] = (int64_t)lhs[1] * (int64_t)rhs[2] - (int64_t)lhs[2] * (int64_t)rhs[1];
    out[1] = (int64_t)lhs[2] * (int64_t)rhs[0] - (int64_t)lhs[0] * (int64_t)rhs[2];
    out[2] = (int64_t)lhs[0] * (int64_t)rhs[1] - (int64_t)lhs[1] * (int64_t)rhs[0];
}

static int
mat3_q15_to_quat_q30(const int32_t row0[3], const int32_t row1[3], const int32_t row2[3],
    int32_t quat_q30[4])
{
    const int64_t r00 = row0[0], r01 = row0[1], r02 = row0[2];
    const int64_t r10 = row1[0], r11 = row1[1], r12 = row1[2];
    const int64_t r20 = row2[0], r21 = row2[1], r22 = row2[2];
    const int64_t trace = r00 + r11 + r22;
    int64_t raw[4];

    if (trace > 0) {
        const uint64_t diag = (uint64_t)(kQ15One + trace);
        const int64_t root = (int64_t)isqrt64(diag << 45);
        if (root <= 0) {
            return -1;
        }
        raw[3] = root >> 1;
        raw[0] = ((r21 - r12) * (1ll << 44)) / root;
        raw[1] = ((r02 - r20) * (1ll << 44)) / root;
        raw[2] = ((r10 - r01) * (1ll << 44)) / root;
    } else if (r00 >= r11 && r00 >= r22) {
        const int64_t term = (int64_t)kQ15One + r00 - r11 - r22;
        const int64_t root = (term > 0) ? (int64_t)isqrt64((uint64_t)term << 45) : 0;
        if (root <= 0) {
            return -1;
        }
        raw[0] = root >> 1;
        raw[3] = ((r21 - r12) * (1ll << 44)) / root;
        raw[1] = ((r01 + r10) * (1ll << 44)) / root;
        raw[2] = ((r02 + r20) * (1ll << 44)) / root;
    } else if (r11 >= r22) {
        const int64_t term = (int64_t)kQ15One + r11 - r00 - r22;
        const int64_t root = (term > 0) ? (int64_t)isqrt64((uint64_t)term << 45) : 0;
        if (root <= 0) {
            return -1;
        }
        raw[1] = root >> 1;
        raw[3] = ((r02 - r20) * (1ll << 44)) / root;
        raw[0] = ((r01 + r10) * (1ll << 44)) / root;
        raw[2] = ((r12 + r21) * (1ll << 44)) / root;
    } else {
        const int64_t term = (int64_t)kQ15One + r22 - r00 - r11;
        const int64_t root = (term > 0) ? (int64_t)isqrt64((uint64_t)term << 45) : 0;
        if (root <= 0) {
            return -1;
        }
        raw[2] = root >> 1;
        raw[3] = ((r10 - r01) * (1ll << 44)) / root;
        raw[0] = ((r02 + r20) * (1ll << 44)) / root;
        raw[1] = ((r12 + r21) * (1ll << 44)) / root;
    }
    return normalize_quat_q30(raw, quat_q30);
}

static int32_t
heading_from_quat_q30(const int32_t quat_q30[4])
{
    const int64_t qx = quat_q30[0] >> 15;
    const int64_t qy = quat_q30[1] >> 15;
    const int64_t qz = quat_q30[2] >> 15;
    const int64_t qw = quat_q30[3] >> 15;
    const int32_t r01 = (int32_t)((2ll * (qx * qy - qw * qz)) >> 15);
    const int32_t r02 = (int32_t)((2ll * (qx * qz + qw * qy)) >> 15);
    const int32_t r11 = (int32_t)(kQ15One - ((2ll * (qx * qx + qz * qz)) >> 15));
    const int32_t r12 = (int32_t)((2ll * (qy * qz - qw * qx)) >> 15);
    const uint32_t y_horiz = abs_i32_to_u32(r01) + abs_i32_to_u32(r11);
    const uint32_t z_horiz = abs_i32_to_u32(r02) + abs_i32_to_u32(r12);
    int32_t heading_mdeg;
    if (y_horiz >= z_horiz) {
        heading_mdeg = sns_compass_atan2_mdeg(-r11, r01);
    } else {
        heading_mdeg = sns_compass_atan2_mdeg(r12, -r02);
    }
    if (heading_mdeg < 0) {
        heading_mdeg += 360000;
    }
    if (heading_mdeg >= 360000) {
        heading_mdeg -= 360000;
    }
    return heading_mdeg;
}

static int
accel_up_and_norm(const SnsCompass *state, int32_t up_vec[3], uint64_t *norm_q16_out)
{
    if (!state->accel_valid) {
        return -1;
    }
    int64_t up_raw[3];
    uint64_t norm2 = 0;
    for (unsigned idx = 0; idx < 3u; ++idx) {
        const int64_t val = -(int64_t)state->accel_ios[idx];
        up_raw[idx] = val;
        const uint64_t mag = (val < 0) ? (uint64_t)(-val) : (uint64_t)val;
        if (mag > 0x1000000ull) {
            return -1;
        }
        norm2 += mag * mag;
    }
    const uint64_t norm_q16 = isqrt64(norm2);
    if (norm_q16 < (uint64_t)kFusionAccelValidMinQ16) {
        return -1;
    }
    if (norm_q16_out) {
        *norm_q16_out = norm_q16;
    }
    return normalize_vec3_q15(up_raw, up_vec);
}

static void
gyro_corrected_q16(const SnsCompass *state, const int32_t raw_q16[3], int32_t out_q16[3])
{
    for (unsigned idx = 0; idx < 3u; ++idx) {
        int64_t val = (int64_t)raw_q16[idx] - (int64_t)state->gyro_bias[idx];
        if (val > 0x7fffffffll) {
            val = 0x7fffffffll;
        } else if (val < -0x7fffffffll) {
            val = -0x7fffffffll;
        }
        out_q16[idx] = (int32_t)val;
    }
}

static uint64_t
gyro_l1_rate_q16(const SnsCompass *state)
{
    if (!state->gyro_valid) {
        return 0ull;
    }
    int32_t corr[3];
    gyro_corrected_q16(state, state->gyro, corr);
    return (uint64_t)abs_i32_to_u32(corr[0]) +
           (uint64_t)abs_i32_to_u32(corr[1]) +
           (uint64_t)abs_i32_to_u32(corr[2]);
}

static int
gyro_is_rotating(const SnsCompass *state)
{
    return state->gyro_valid && gyro_l1_rate_q16(state) >= (uint64_t)kRotvGyroMovingQ16;
}

static int
gyro_is_still(const SnsCompass *state)
{
    return state->gyro_valid && gyro_l1_rate_q16(state) < (uint64_t)kGyroStillL1Q16;
}

static void
quat_to_axes_q15(const int32_t quat_q30[4], int32_t north_q15[3], int32_t west_q15[3], int32_t up_q15[3])
{
    const int64_t qx = quat_q30[0] >> 15;
    const int64_t qy = quat_q30[1] >> 15;
    const int64_t qz = quat_q30[2] >> 15;
    const int64_t qw = quat_q30[3] >> 15;

    if (north_q15) {
        north_q15[0] = (int32_t)(kQ15One - ((2ll * (qy * qy + qz * qz)) >> 15));
        north_q15[1] = (int32_t)((2ll * (qx * qy - qw * qz)) >> 15);
        north_q15[2] = (int32_t)((2ll * (qx * qz + qw * qy)) >> 15);
    }
    if (west_q15) {
        west_q15[0] = (int32_t)((2ll * (qx * qy + qw * qz)) >> 15);
        west_q15[1] = (int32_t)(kQ15One - ((2ll * (qx * qx + qz * qz)) >> 15));
        west_q15[2] = (int32_t)((2ll * (qy * qz - qw * qx)) >> 15);
    }
    if (up_q15) {
        up_q15[0] = (int32_t)((2ll * (qx * qz - qw * qy)) >> 15);
        up_q15[1] = (int32_t)((2ll * (qy * qz + qw * qx)) >> 15);
        up_q15[2] = (int32_t)(kQ15One - ((2ll * (qx * qx + qy * qy)) >> 15));
    }
}

static void
compute_synth_mag_q16(const SnsCompass *state, const int32_t quat_q30[4], int32_t synth_q16[3])
{
    int32_t north_q15[3];
    int32_t up_q15[3];
    quat_to_axes_q15(quat_q30, north_q15, NULL, up_q15);
    const int64_t horiz_mut =
        (state->earth_ref_count >= (uint32_t)kEarthRefMinSamples &&
         state->earth_ref_horiz_mut >= (uint32_t)kEarthHorizMinMut)
        ? (int64_t)state->earth_ref_horiz_mut : 35000ll;
    const int64_t up_mut =
        (state->earth_ref_count >= (uint32_t)kEarthRefMinSamples && state->earth_ref_has_up)
        ? (int64_t)state->earth_ref_up_mut : -25000ll;
    for (unsigned idx = 0; idx < 3u; ++idx) {
        const int64_t b_mut = ((int64_t)north_q15[idx] * horiz_mut + (int64_t)up_q15[idx] * up_mut) >> 15;
        synth_q16[idx] = (int32_t)((b_mut * (int64_t)kQ16One) / 1000ll);
    }
}

static uint32_t
bias_drift_mut(const int32_t lhs_q16[3], const int32_t rhs_q16[3])
{
    uint64_t sum_sq = 0;
    for (unsigned idx = 0; idx < 3u; ++idx) {
        const int64_t diff_q16 = (int64_t)lhs_q16[idx] - (int64_t)rhs_q16[idx];
        const int64_t dm = (diff_q16 * 1000ll) >> 16;
        const uint64_t abs_m = (dm < 0) ? (uint64_t)(-dm) : (uint64_t)dm;
        if (abs_m > 1000000ull) {
            return 1000000u;
        }
        sum_sq += abs_m * abs_m;
    }
    return (uint32_t)isqrt64(sum_sq);
}

static int
mag_cal_is_trusted(const SnsCompass *state)
{
    return state->has_trusted_mag_cal ? 1 : 0;
}

static const int32_t *
select_instant_ref_bias(const SnsCompass *state)
{
    if (state->has_trusted_mag_cal && state->trusted_mag_cal_acc >= 2) {
        return state->trusted_mag_cal_bias;
    }
    if (state->has_default_mag_bias) {
        return state->default_mag_bias;
    }
    if (state->has_trusted_mag_cal && state->trusted_mag_cal_acc >= 1) {
        return state->trusted_mag_cal_bias;
    }
    return NULL;
}

static int
bias_eval_earth(const int32_t mag_raw_q16[3], const int32_t bias_q16[3], const int32_t up_vec_q15[3],
    uint32_t *norm_mut_out, uint32_t *horiz_mut_out, int32_t *up_mut_out, int32_t cal_mut_out[3])
{
    int64_t cal_mut[3];
    uint64_t total_sq = 0;
    int64_t dot_up = 0;
    for (unsigned idx = 0; idx < 3u; ++idx) {
        const int64_t diff_q16 = (int64_t)mag_raw_q16[idx] - (int64_t)bias_q16[idx];
        cal_mut[idx] = (diff_q16 * 1000ll) >> 16;
        if (cal_mut_out) {
            cal_mut_out[idx] = (cal_mut[idx] > 2000000ll) ? 2000000
                : (cal_mut[idx] < -2000000ll) ? -2000000 : (int32_t)cal_mut[idx];
        }
        const uint64_t abs_m = (cal_mut[idx] < 0) ? (uint64_t)(-cal_mut[idx]) : (uint64_t)cal_mut[idx];
        if (abs_m > 200000ull) {
            return 0;
        }
        total_sq += abs_m * abs_m;
        if (up_vec_q15) {
            dot_up += cal_mut[idx] * (int64_t)up_vec_q15[idx];
        }
    }
    const uint64_t mag_norm = isqrt64(total_sq);
    if (norm_mut_out) {
        *norm_mut_out = (uint32_t)mag_norm;
    }
    uint64_t horiz_norm = mag_norm;
    if (up_vec_q15) {
        const int64_t par = dot_up >> 15;
        if (up_mut_out) {
            *up_mut_out = (int32_t)par;
        }
        const uint64_t par_abs = (par < 0) ? (uint64_t)(-par) : (uint64_t)par;
        const uint64_t par_sq = par_abs * par_abs;
        const uint64_t horiz_sq = (total_sq > par_sq) ? (total_sq - par_sq) : 0ull;
        horiz_norm = isqrt64(horiz_sq);
    }
    if (horiz_mut_out) {
        *horiz_mut_out = (uint32_t)horiz_norm;
    }
    if (mag_norm < (uint64_t)kEarthNormMinMut || mag_norm > (uint64_t)kEarthNormMaxMut) {
        return 0;
    }
    if (up_vec_q15 &&
        (horiz_norm < (uint64_t)kEarthHorizMinMut || horiz_norm > (uint64_t)kEarthHorizMaxMut)) {
        return 0;
    }
    return 1;
}

static int
bias_is_earth_valid(const int32_t mag_raw_q16[3], const int32_t bias_q16[3], const int32_t up_vec_q15[3])
{
    return bias_eval_earth(mag_raw_q16, bias_q16, up_vec_q15, NULL, NULL, NULL, NULL);
}

static int
select_active_mag_bias(const SnsCompass *state, const int32_t up_vec_q15[3], int32_t bias_out_q16[3])
{
    if (!state->mag_valid || state->mag_disturbed) {
        return SNS_COMPASS_BIAS_NONE;
    }
    if (state->has_trusted_mag_cal && state->trusted_mag_cal_acc >= 2 &&
        bias_is_earth_valid(state->mag_raw, state->trusted_mag_cal_bias, up_vec_q15)) {
        for (unsigned idx = 0; idx < 3u; ++idx) {
            bias_out_q16[idx] = state->trusted_mag_cal_bias[idx];
        }
        return SNS_COMPASS_BIAS_MAGCAL;
    }
    if (state->has_default_mag_bias &&
        bias_is_earth_valid(state->mag_raw, state->default_mag_bias, up_vec_q15)) {
        for (unsigned idx = 0; idx < 3u; ++idx) {
            bias_out_q16[idx] = state->default_mag_bias[idx];
        }
        return SNS_COMPASS_BIAS_DEFAULT;
    }
    if (state->has_trusted_mag_cal && state->trusted_mag_cal_acc >= 1 &&
        bias_is_earth_valid(state->mag_raw, state->trusted_mag_cal_bias, up_vec_q15)) {
        for (unsigned idx = 0; idx < 3u; ++idx) {
            bias_out_q16[idx] = state->trusted_mag_cal_bias[idx];
        }
        return SNS_COMPASS_BIAS_MAGCAL;
    }
    return SNS_COMPASS_BIAS_NONE;
}

static void
refresh_active_bias(SnsCompass *state)
{
    int32_t up_vec[3];
    const int32_t *up_ptr = (accel_up_and_norm(state, up_vec, NULL) == 0) ? up_vec : NULL;
    int32_t chosen[3] = {0, 0, 0};
    const int src = select_active_mag_bias(state, up_ptr, chosen);
    state->active_bias_src = src;
    for (unsigned idx = 0; idx < 3u; ++idx) {
        state->active_bias[idx] = (src != SNS_COMPASS_BIAS_NONE) ? chosen[idx] : 0;
    }
}

static int32_t
quat_yaw_diff_mdeg(const int32_t cur_q30[4], const int32_t meas_q30[4])
{
    const int64_t cx = cur_q30[0] >> 15;
    const int64_t cy = cur_q30[1] >> 15;
    const int64_t cz = cur_q30[2] >> 15;
    const int64_t cw = cur_q30[3] >> 15;
    const int64_t mx = meas_q30[0] >> 15;
    const int64_t my = meas_q30[1] >> 15;
    const int64_t mz = meas_q30[2] >> 15;
    const int64_t mw = meas_q30[3] >> 15;
    const int64_t cn0 = kQ15One - ((2ll * (cy * cy + cz * cz)) >> 15);
    const int64_t cn1 = (2ll * (cx * cy - cw * cz)) >> 15;
    const int64_t cn2 = (2ll * (cx * cz + cw * cy)) >> 15;
    const int64_t cw0 = (2ll * (cx * cy + cw * cz)) >> 15;
    const int64_t cw1 = kQ15One - ((2ll * (cx * cx + cz * cz)) >> 15);
    const int64_t cw2 = (2ll * (cy * cz - cw * cx)) >> 15;
    const int64_t mn0 = kQ15One - ((2ll * (my * my + mz * mz)) >> 15);
    const int64_t mn1 = (2ll * (mx * my - mw * mz)) >> 15;
    const int64_t mn2 = (2ll * (mx * mz + mw * my)) >> 15;
    const int32_t cos_q15 = (int32_t)((cn0 * mn0 + cn1 * mn1 + cn2 * mn2) >> 15);
    const int32_t sin_q15 = (int32_t)((cw0 * mn0 + cw1 * mn1 + cw2 * mn2) >> 15);
    return sns_compass_atan2_mdeg(sin_q15, cos_q15);
}

static int
fallback_quat_accel_only(const SnsCompass *state, int32_t quat_q30[4])
{
    int32_t up_vec[3];
    if (!state->fallback_valid || accel_up_and_norm(state, up_vec, NULL) != 0) {
        return -1;
    }
    const int64_t qx = state->fallback_q[0] >> 15;
    const int64_t qy = state->fallback_q[1] >> 15;
    const int64_t qz = state->fallback_q[2] >> 15;
    const int64_t qw = state->fallback_q[3] >> 15;
    const int32_t cur_north[3] = {
        (int32_t)(kQ15One - ((2ll * (qy * qy + qz * qz)) >> 15)),
        (int32_t)((2ll * (qx * qy - qw * qz)) >> 15),
        (int32_t)((2ll * (qx * qz + qw * qy)) >> 15),
    };
    int64_t west_raw[3];
    int32_t west_vec[3] = {0, 0, 0};
    int have_west = 0;
    cross_q15(up_vec, cur_north, west_raw);
    const uint64_t west_norm2 =
        (uint64_t)(west_raw[0] * west_raw[0]) +
        (uint64_t)(west_raw[1] * west_raw[1]) +
        (uint64_t)(west_raw[2] * west_raw[2]);
    if (west_norm2 >= (1ull << 24) && normalize_vec3_q15(west_raw, west_vec) == 0) {
        have_west = 1;
    } else {
        const int32_t cur_west[3] = {
            (int32_t)((2ll * (qx * qy + qw * qz)) >> 15),
            (int32_t)(kQ15One - ((2ll * (qx * qx + qz * qz)) >> 15)),
            (int32_t)((2ll * (qy * qz - qw * qx)) >> 15),
        };
        int64_t north_tmp_raw[3];
        int32_t north_tmp[3];
        cross_q15(cur_west, up_vec, north_tmp_raw);
        if (normalize_vec3_q15(north_tmp_raw, north_tmp) == 0) {
            cross_q15(up_vec, north_tmp, west_raw);
            if (normalize_vec3_q15(west_raw, west_vec) == 0) {
                have_west = 1;
            }
        }
    }
    if (!have_west) {
        return -1;
    }
    int64_t north_raw[3];
    int32_t north_vec[3];
    cross_q15(west_vec, up_vec, north_raw);
    if (normalize_vec3_q15(north_raw, north_vec) != 0) {
        return -1;
    }
    return mat3_q15_to_quat_q30(north_vec, west_vec, up_vec, quat_q30);
}

static int
fallback_quat_from_accel_mag(SnsCompass *state, int32_t quat_q30[4], int *used_mag_out, int *accel_steady_out)
{
    *used_mag_out = 0;
    if (accel_steady_out) {
        *accel_steady_out = 0;
    }
    int32_t up_vec[3];
    uint64_t norm_q16 = 0;
    if (accel_up_and_norm(state, up_vec, &norm_q16) != 0) {
        return -1;
    }
    if (accel_steady_out) {
        *accel_steady_out = (norm_q16 >= (uint64_t)kFusionAccelMinQ16 &&
                             norm_q16 <= (uint64_t)kFusionAccelMaxQ16) ? 1 : 0;
    }
    int32_t chosen_bias[3] = {0, 0, 0};
    const int bias_src = select_active_mag_bias(state, up_vec, chosen_bias);
    state->active_bias_src = bias_src;
    for (unsigned idx = 0; idx < 3u; ++idx) {
        state->active_bias[idx] = (bias_src != SNS_COMPASS_BIAS_NONE) ? chosen_bias[idx] : 0;
    }
    int32_t west_vec[3] = {0, 0, 0};
    int have_west = 0;
    if (bias_src != SNS_COMPASS_BIAS_NONE) {
        const int64_t cal_mag[3] = {
            (int64_t)state->mag_raw[0] - (int64_t)chosen_bias[0],
            (int64_t)state->mag_raw[1] - (int64_t)chosen_bias[1],
            (int64_t)state->mag_raw[2] - (int64_t)chosen_bias[2],
        };
        int32_t mag_unit[3];
        if (normalize_vec3_q15(cal_mag, mag_unit) == 0) {
            int64_t west_raw[3];
            cross_q15(up_vec, mag_unit, west_raw);
            const uint64_t west_norm2 =
                (uint64_t)(west_raw[0] * west_raw[0]) +
                (uint64_t)(west_raw[1] * west_raw[1]) +
                (uint64_t)(west_raw[2] * west_raw[2]);
            if (west_norm2 >= (1ull << 24) && normalize_vec3_q15(west_raw, west_vec) == 0) {
                have_west = 1;
                *used_mag_out = 1;
            }
        }
    }
    if (!have_west && state->fallback_valid) {
        const int64_t qx = state->fallback_q[0] >> 15;
        const int64_t qy = state->fallback_q[1] >> 15;
        const int64_t qz = state->fallback_q[2] >> 15;
        const int64_t qw = state->fallback_q[3] >> 15;
        const int32_t cur_north[3] = {
            (int32_t)(kQ15One - ((2ll * (qy * qy + qz * qz)) >> 15)),
            (int32_t)((2ll * (qx * qy - qw * qz)) >> 15),
            (int32_t)((2ll * (qx * qz + qw * qy)) >> 15),
        };
        int64_t west_raw[3];
        cross_q15(up_vec, cur_north, west_raw);
        const uint64_t west_norm2 =
            (uint64_t)(west_raw[0] * west_raw[0]) +
            (uint64_t)(west_raw[1] * west_raw[1]) +
            (uint64_t)(west_raw[2] * west_raw[2]);
        if (west_norm2 >= (1ull << 24) && normalize_vec3_q15(west_raw, west_vec) == 0) {
            have_west = 1;
        } else {
            const int32_t cur_west[3] = {
                (int32_t)((2ll * (qx * qy + qw * qz)) >> 15),
                (int32_t)(kQ15One - ((2ll * (qx * qx + qz * qz)) >> 15)),
                (int32_t)((2ll * (qy * qz - qw * qx)) >> 15),
            };
            int64_t north_tmp_raw[3];
            int32_t north_tmp[3];
            cross_q15(cur_west, up_vec, north_tmp_raw);
            if (normalize_vec3_q15(north_tmp_raw, north_tmp) == 0) {
                cross_q15(up_vec, north_tmp, west_raw);
                if (normalize_vec3_q15(west_raw, west_vec) == 0) {
                    have_west = 1;
                }
            }
        }
    }
    if (!have_west) {
        const int32_t ref_north[3] = {
            (abs_i32_to_u32(up_vec[1]) < 29000u) ? 0 : kQ15One,
            (abs_i32_to_u32(up_vec[1]) < 29000u) ? kQ15One : 0,
            0,
        };
        int64_t west_raw[3];
        cross_q15(up_vec, ref_north, west_raw);
        if (normalize_vec3_q15(west_raw, west_vec) != 0) {
            return -1;
        }
    }
    int64_t north_raw[3];
    int32_t north_vec[3];
    cross_q15(west_vec, up_vec, north_raw);
    if (normalize_vec3_q15(north_raw, north_vec) != 0) {
        return -1;
    }
    const int rc = mat3_q15_to_quat_q30(north_vec, west_vec, up_vec, quat_q30);
    if (rc == 0 && *used_mag_out) {
        state->last_mag_heading_mdeg = heading_from_quat_q30(quat_q30);
    }
    return rc;
}

static void
integrate_quat_gyro(int32_t quat_q30[4], const int32_t gyro_q16[3], uint64_t dt_us)
{
    int64_t half_angle[3];
    for (unsigned idx = 0; idx < 3u; ++idx) {
        int64_t rate = gyro_q16[idx];
        if (rate > kGyroMaxQ16) {
            rate = kGyroMaxQ16;
        } else if (rate < -kGyroMaxQ16) {
            rate = -kGyroMaxQ16;
        }
        int64_t hval = (rate * (int64_t)dt_us * 8192ll) / 1000000ll;
        if (hval > kQ30One) {
            hval = kQ30One;
        } else if (hval < -kQ30One) {
            hval = -kQ30One;
        }
        half_angle[idx] = hval;
    }
    const int64_t qx = (int64_t)quat_q30[0] >> 2;
    const int64_t qy = (int64_t)quat_q30[1] >> 2;
    const int64_t qz = (int64_t)quat_q30[2] >> 2;
    const int64_t qw = (int64_t)quat_q30[3] >> 2;
    const int64_t hx = half_angle[0];
    const int64_t hy = half_angle[1];
    const int64_t hz = half_angle[2];

    const int64_t next_raw[4] = {
        ((int64_t)quat_q30[0] + ((qw * hx + qy * hz - qz * hy) >> 28)) >> 1,
        ((int64_t)quat_q30[1] + ((qw * hy - qx * hz + qz * hx) >> 28)) >> 1,
        ((int64_t)quat_q30[2] + ((qw * hz + qx * hy - qy * hx) >> 28)) >> 1,
        ((int64_t)quat_q30[3] + ((-qx * hx - qy * hy - qz * hz) >> 28)) >> 1,
    };
    int32_t updated[4];
    if (normalize_quat_q30(next_raw, updated) == 0) {
        for (unsigned idx = 0; idx < 4u; ++idx) {
            quat_q30[idx] = updated[idx];
        }
    }
}

static void
blend_quat_nlerp(int32_t cur_q30[4], const int32_t target_q30[4], int64_t gain_num, int64_t gain_den)
{
    int64_t dot = 0;
    for (unsigned idx = 0; idx < 4u; ++idx) {
        dot += ((int64_t)cur_q30[idx] >> 15) * ((int64_t)target_q30[idx] >> 15);
    }
    const int64_t sign = (dot < 0) ? -1ll : 1ll;
    const int64_t keep = gain_den - gain_num;
    int64_t blended[4];
    for (unsigned idx = 0; idx < 4u; ++idx) {
        blended[idx] = ((int64_t)cur_q30[idx] * keep + sign * (int64_t)target_q30[idx] * gain_num) / gain_den;
    }
    int32_t updated[4];
    if (normalize_quat_q30(blended, updated) == 0) {
        for (unsigned idx = 0; idx < 4u; ++idx) {
            cur_q30[idx] = updated[idx];
        }
    }
}

void
sns_compass_init(SnsCompass *state)
{
    uint8_t *bytes = (uint8_t *)state;
    for (unsigned idx = 0; idx < sizeof(*state); ++idx) {
        bytes[idx] = 0;
    }
    state->compass_mode = SNS_COMPASS_MODE_FUSION;
    state->rotv_acc = -1;
    state->mag_cal_acc = -1;
    state->trusted_mag_cal_acc = -1;
    state->mag_cal_cand_acc = -1;
    state->mag_acc = -1;
    state->last_mag_heading_mdeg = -1;
    state->last_rotv_heading_mdeg = -1;
}

void
sns_compass_set_mode(SnsCompass *state, uint32_t mode)
{
    state->compass_mode = (mode == SNS_COMPASS_MODE_ROTV)
        ? SNS_COMPASS_MODE_ROTV : SNS_COMPASS_MODE_FUSION;
}

void
sns_compass_set_default_mag_bias(SnsCompass *state, const int32_t bias_q16[3])
{
    for (unsigned idx = 0; idx < 3u; ++idx) {
        state->default_mag_bias[idx] = bias_q16[idx];
    }
    state->has_default_mag_bias = 1;
    refresh_active_bias(state);
}

int
sns_compass_apply_mag_bias(const SnsCompass *state, const int32_t raw_q16[3], int32_t out_q16[3])
{
    if (!state->mag_disturbed && !state->last_yaw_rejected &&
        state->active_bias_src != SNS_COMPASS_BIAS_NONE) {
        for (unsigned idx = 0; idx < 3u; ++idx) {
            int64_t diff = (int64_t)raw_q16[idx] - (int64_t)state->active_bias[idx];
            if (diff > 0x7fffffffll) {
                diff = 0x7fffffffll;
            } else if (diff < -0x7fffffffll) {
                diff = -0x7fffffffll;
            }
            out_q16[idx] = (int32_t)diff;
        }
        return 1;
    }
    /* During magnetic disturbance or yaw rejection, emit the gyro-aligned clean Earth vector
     * instead of leaking unsubtracted PCB bias (~507 uT) or nearby-magnet spikes to FuxiSensorMag. */
    if (state->compass_mode == SNS_COMPASS_MODE_ROTV && state->rotv_valid) {
        compute_synth_mag_q16(state, state->rotv_q, out_q16);
        return 1;
    }
    if (state->fallback_valid) {
        compute_synth_mag_q16(state, state->fallback_q, out_q16);
        return 1;
    }
    const int32_t *ref_bias = select_instant_ref_bias(state);
    if (ref_bias) {
        for (unsigned idx = 0; idx < 3u; ++idx) {
            int64_t diff = (int64_t)raw_q16[idx] - (int64_t)ref_bias[idx];
            if (diff > 0x7fffffffll) {
                diff = 0x7fffffffll;
            } else if (diff < -0x7fffffffll) {
                diff = -0x7fffffffll;
            }
            out_q16[idx] = (int32_t)diff;
        }
        return 1;
    }
    for (unsigned idx = 0; idx < 3u; ++idx) {
        out_q16[idx] = raw_q16[idx];
    }
    return 0;
}

int
sns_compass_apply_gyro_bias(const SnsCompass *state, const int32_t raw_q16[3], int32_t out_q16[3])
{
    int32_t corr[3];
    gyro_corrected_q16(state, raw_q16, corr);
    int in_deadband = (state->zupt_still_cnt >= (uint32_t)kZuptStillSamples);
    if (in_deadband) {
        for (unsigned idx = 0; idx < 3u; ++idx) {
            if (abs_i32_to_u32(corr[idx]) >= (uint32_t)kZuptDeadbandQ16) {
                in_deadband = 0;
                break;
            }
        }
    }
    for (unsigned idx = 0; idx < 3u; ++idx) {
        out_q16[idx] = in_deadband ? 0 : corr[idx];
    }
    return state->zupt_learn_cnt > 0u ? 1 : 0;
}

static void mag_hist_reset(SnsCompass *state);

void
sns_compass_reset_session(SnsCompass *state)
{
    state->fallback_valid = 0;
    state->fallback_used_mag = 0;
    state->fallback_seeded_from_rotv = 0;
    state->fallback_gyro_steps = 0u;
    state->bias_handoff_steps = 0u;
    state->mag_disturbed = 0;
    state->mag_disturb_hold = 0u;
    state->mag_disturb_reason = 0u;
    state->vec_anom_cnt = 0u;
    state->earth_anom_cnt = 0u;
    state->last_yaw_rejected = 0;
    state->mag_innov_reject_cnt = 0u;
    state->mag_innov_confirm_cnt = 0u;
    state->earth_ref_count = 0u;
    state->earth_ref_horiz_mut = 0u;
    state->earth_ref_has_up = 0;
    state->earth_reanchor_count = 0u;
    state->relock_steady_cnt = 0u;
    state->gyro_turn_acc_mdeg = 0u;
    state->has_mag_prev_inst = 0;
    state->zupt_still_cnt = 0u;
    state->zupt_has_ref = 0;
    state->pose_hist_count = 0u;
    state->pose_hist_pos = 0u;
    mag_hist_reset(state);
}

int
sns_compass_parse_mag_bias(const char *spec, int32_t out_q16[3])
{
    if (!spec || !spec[0] || !out_q16) {
        return -1;
    }
    int64_t mut[3] = {0, 0, 0};
    const char *cursor = spec;
    for (unsigned idx = 0; idx < 3u; ++idx) {
        while (*cursor == ' ' || *cursor == '\t') {
            cursor++;
        }
        int64_t sign = 1;
        if (*cursor == '-') {
            sign = -1;
            cursor++;
        } else if (*cursor == '+') {
            cursor++;
        }
        if (*cursor < '0' || *cursor > '9') {
            return -1;
        }
        int64_t acc = 0;
        while (*cursor >= '0' && *cursor <= '9') {
            acc = acc * 10ll + (int64_t)(*cursor - '0');
            if (acc > 10000000ll) {
                return -1;
            }
            cursor++;
        }
        mut[idx] = sign * acc;
        while (*cursor == ' ' || *cursor == '\t') {
            cursor++;
        }
        if (idx < 2u) {
            if (*cursor != ',') {
                return -1;
            }
            cursor++;
        }
    }
    if (*cursor != '\0') {
        return -1;
    }
    int nonzero = 0;
    for (unsigned idx = 0; idx < 3u; ++idx) {
        if (mut[idx] != 0) {
            nonzero = 1;
        }
        int64_t q16;
        if (mut[idx] >= 0) {
            q16 = (mut[idx] * (int64_t)kQ16One + 500ll) / 1000ll;
        } else {
            q16 = -(((-mut[idx]) * (int64_t)kQ16One + 500ll) / 1000ll);
        }
        out_q16[idx] = (int32_t)q16;
    }
    return nonzero;
}

static int
bias_triplet_sane(const int32_t bias_q16[3])
{
    if (!bias_q16) {
        return 0;
    }
    int nonzero = 0;
    for (unsigned idx = 0; idx < 3u; ++idx) {
        if (bias_q16[idx] > kMagBiasPersistMaxQ16 || bias_q16[idx] < -kMagBiasPersistMaxQ16) {
            return 0;
        }
        if (bias_q16[idx] != 0) {
            nonzero = 1;
        }
    }
    return nonzero;
}

uint32_t
sns_compass_bias_drift_mut(const int32_t lhs_q16[3], const int32_t rhs_q16[3])
{
    if (!lhs_q16 || !rhs_q16) {
        return 1000000u;
    }
    return bias_drift_mut(lhs_q16, rhs_q16);
}

int
sns_compass_encode_persisted_bias(const int32_t bias_q16[3], uint8_t out[SNS_COMPASS_PERSIST_BYTES])
{
    if (!out || !bias_triplet_sane(bias_q16)) {
        return -1;
    }
    write_le32(out + 0u, (uint32_t)kMagBiasPersistMagic);
    write_le32(out + 4u, (uint32_t)kMagBiasPersistVersion);
    for (unsigned idx = 0; idx < 3u; ++idx) {
        write_le32(out + 8u + idx * 4u, (uint32_t)bias_q16[idx]);
    }
    write_le32(out + 20u, fnv1a_32(out, 20u));
    return 0;
}

int
sns_compass_decode_persisted_bias(const uint8_t in[SNS_COMPASS_PERSIST_BYTES], int32_t bias_q16[3])
{
    if (!in || !bias_q16) {
        return -1;
    }
    if (read_le32(in + 0u) != (uint32_t)kMagBiasPersistMagic ||
        read_le32(in + 4u) != (uint32_t)kMagBiasPersistVersion) {
        return -1;
    }
    if (read_le32(in + 20u) != fnv1a_32(in, 20u)) {
        return -1;
    }
    int32_t tmp[3];
    for (unsigned idx = 0; idx < 3u; ++idx) {
        tmp[idx] = (int32_t)read_le32(in + 8u + idx * 4u);
    }
    if (!bias_triplet_sane(tmp)) {
        return -1;
    }
    for (unsigned idx = 0; idx < 3u; ++idx) {
        bias_q16[idx] = tmp[idx];
    }
    return 0;
}

int
sns_compass_load_persisted_mag_bias(SnsCompass *state, const int32_t bias_q16[3])
{
    if (!state || !bias_triplet_sane(bias_q16)) {
        return -1;
    }
    if (state->has_trusted_mag_cal) {
        return 0;
    }
    for (unsigned idx = 0; idx < 3u; ++idx) {
        state->default_mag_bias[idx] = bias_q16[idx];
    }
    state->has_default_mag_bias = 1;
    sns_compass_reset_session(state);
    refresh_active_bias(state);
    return 1;
}

int
sns_compass_get_persistable_bias(const SnsCompass *state, int32_t bias_q16[3])
{
    if (!state || !bias_q16) {
        return 0;
    }
    if (!state->has_trusted_mag_cal || state->trusted_mag_cal_acc < 3 ||
        state->bias_handoff_steps > 0u ||
        state->earth_ref_count < (uint32_t)kEarthRefMinSamples ||
        !state->earth_ref_has_up ||
        state->mag_disturbed || state->mag_disturb_hold > 0u ||
        state->mag_cal_quarantine > 0u || state->last_yaw_rejected) {
        return 0;
    }
    if (!bias_triplet_sane(state->trusted_mag_cal_bias)) {
        return 0;
    }
    for (unsigned idx = 0; idx < 3u; ++idx) {
        bias_q16[idx] = state->trusted_mag_cal_bias[idx];
    }
    return 1;
}

void
sns_compass_set_accel(SnsCompass *state, const int32_t accel_q16[3])
{
    for (unsigned idx = 0; idx < 3u; ++idx) {
        state->accel_ios[idx] = accel_q16[idx];
    }
    state->accel_valid = 1;
}

static void
update_zupt_gyro_bias(SnsCompass *state, const int32_t gyro_q16[3])
{
    int32_t up_vec[3];
    uint64_t accel_norm_q16 = 0;
    if (!state->accel_valid || accel_up_and_norm(state, up_vec, &accel_norm_q16) != 0 ||
        accel_norm_q16 < (uint64_t)kZuptAccelMinQ16 || accel_norm_q16 > (uint64_t)kZuptAccelMaxQ16) {
        state->zupt_still_cnt = 0u;
        state->zupt_has_ref = 0;
        return;
    }
    if (!state->zupt_has_ref) {
        for (unsigned idx = 0; idx < 3u; ++idx) {
            state->zupt_gyro_ref[idx] = gyro_q16[idx];
            state->zupt_accel_ref[idx] = state->accel_ios[idx];
        }
        state->zupt_has_ref = 1;
        state->zupt_still_cnt = 0u;
        return;
    }
    int still = 1;
    for (unsigned idx = 0; idx < 3u; ++idx) {
        if (abs_i32_to_u32(gyro_q16[idx]) > (uint32_t)kZuptMaxBiasQ16) {
            still = 0;
            break;
        }
        const int64_t dg = (int64_t)gyro_q16[idx] - (int64_t)state->zupt_gyro_ref[idx];
        const uint64_t adg = (dg < 0) ? (uint64_t)(-dg) : (uint64_t)dg;
        if (adg > (uint64_t)kZuptJitterQ16) {
            still = 0;
            break;
        }
        if (state->zupt_learn_cnt > 0u) {
            const int64_t db = (int64_t)gyro_q16[idx] - (int64_t)state->gyro_bias[idx];
            const uint64_t adb = (db < 0) ? (uint64_t)(-db) : (uint64_t)db;
            if (adb > (uint64_t)kZuptJitterQ16) {
                still = 0;
                break;
            }
        }
        const int64_t da = (int64_t)state->accel_ios[idx] - (int64_t)state->zupt_accel_ref[idx];
        const uint64_t ada = (da < 0) ? (uint64_t)(-da) : (uint64_t)da;
        if (ada > (uint64_t)kZuptAccelTolQ16) {
            still = 0;
            break;
        }
    }
    if (!still) {
        state->zupt_still_cnt = 0u;
        for (unsigned idx = 0; idx < 3u; ++idx) {
            state->zupt_gyro_ref[idx] = gyro_q16[idx];
            state->zupt_accel_ref[idx] = state->accel_ios[idx];
        }
        return;
    }
    if (state->zupt_still_cnt < 0xffffffffu) {
        state->zupt_still_cnt++;
    }
    for (unsigned idx = 0; idx < 3u; ++idx) {
        state->zupt_gyro_ref[idx] = (int32_t)((int64_t)state->zupt_gyro_ref[idx] +
            (((int64_t)gyro_q16[idx] - (int64_t)state->zupt_gyro_ref[idx]) / 16ll));
        state->zupt_accel_ref[idx] = (int32_t)((int64_t)state->zupt_accel_ref[idx] +
            (((int64_t)state->accel_ios[idx] - (int64_t)state->zupt_accel_ref[idx]) / 16ll));
    }
    if (state->zupt_still_cnt >= (uint32_t)kZuptStillSamples) {
        for (unsigned idx = 0; idx < 3u; ++idx) {
            int64_t next;
            if (state->zupt_learn_cnt == 0u) {
                next = (int64_t)state->zupt_gyro_ref[idx];
            } else {
                const int64_t div = (state->zupt_learn_cnt < (uint32_t)kZuptFastLearnSamples) ? 8ll : 64ll;
                next = (int64_t)state->gyro_bias[idx] +
                    (((int64_t)gyro_q16[idx] - (int64_t)state->gyro_bias[idx]) / div);
            }
            if (next > (int64_t)kZuptMaxBiasQ16) {
                next = (int64_t)kZuptMaxBiasQ16;
            } else if (next < -(int64_t)kZuptMaxBiasQ16) {
                next = -(int64_t)kZuptMaxBiasQ16;
            }
            state->gyro_bias[idx] = (int32_t)next;
        }
        if (state->zupt_learn_cnt < 0xffffffffu) {
            state->zupt_learn_cnt++;
        }
    }
}

void
sns_compass_set_gyro_ts(SnsCompass *state, const int32_t gyro_q16[3], uint64_t timestamp_ticks)
{
    for (unsigned idx = 0; idx < 3u; ++idx) {
        state->gyro[idx] = gyro_q16[idx];
    }
    state->gyro_valid = 1;
    update_zupt_gyro_bias(state, gyro_q16);
    int32_t gyro_corr[3];
    gyro_corrected_q16(state, gyro_q16, gyro_corr);
    int32_t gyro_eff[3];
    int in_deadband = (state->zupt_still_cnt >= (uint32_t)kZuptStillSamples);
    if (in_deadband) {
        for (unsigned idx = 0; idx < 3u; ++idx) {
            if (abs_i32_to_u32(gyro_corr[idx]) >= (uint32_t)kZuptDeadbandQ16) {
                in_deadband = 0;
                break;
            }
        }
    }
    for (unsigned idx = 0; idx < 3u; ++idx) {
        gyro_eff[idx] = in_deadband ? 0 : gyro_corr[idx];
    }
    const int integrate = (state->compass_mode == SNS_COMPASS_MODE_FUSION) || !state->rotv_valid;
    if (state->compass_mode == SNS_COMPASS_MODE_FUSION && state->fallback_valid &&
        timestamp_ticks != 0 && state->last_gyro_ts != 0 &&
        timestamp_ticks > state->last_gyro_ts &&
        (timestamp_ticks - state->last_gyro_ts) > (uint64_t)kGyroGapResetTicks) {
        sns_compass_reset_session(state);
    }
    if (integrate && state->fallback_valid && timestamp_ticks != 0 &&
        state->last_gyro_ts != 0 && timestamp_ticks > state->last_gyro_ts) {
        const uint64_t dt_ticks = timestamp_ticks - state->last_gyro_ts;
        if (dt_ticks <= (uint64_t)kGyroDtMaxUs * 20ull) {
            const uint64_t dt_us = (dt_ticks * 10ull) / (uint64_t)kQTimerTicksPer10Us;
            if (dt_us >= (uint64_t)kGyroDtMinUs && dt_us <= (uint64_t)kGyroDtMaxUs) {
                if (!in_deadband) {
                    integrate_quat_gyro(state->fallback_q, gyro_eff, dt_us);
                }
                state->fallback_gyro_steps++;
                if ((state->relock_steady_cnt > 0u ||
                     (state->mag_cal_cand_valid && state->mag_cal_cand_good > 0u)) &&
                    !gyro_is_still(state)) {
                    uint64_t l1 = gyro_l1_rate_q16(state);
                    const uint64_t cap = (uint64_t)kGyroMaxQ16 * 3ull;
                    if (l1 > cap) {
                        l1 = cap;
                    }
                    const uint64_t dmdeg = (l1 * dt_us * 57296ull) / 65536000000ull;
                    if (state->relock_steady_cnt > 0u && state->gyro_turn_acc_mdeg < 3600000u) {
                        const uint64_t next = (uint64_t)state->gyro_turn_acc_mdeg + dmdeg;
                        state->gyro_turn_acc_mdeg = (next > 3600000ull) ? 3600000u : (uint32_t)next;
                    }
                    if (state->mag_cal_cand_valid && state->mag_cal_cand_good > 0u &&
                        state->mag_cal_cand_turn_mdeg < 3600000u) {
                        const uint64_t next = (uint64_t)state->mag_cal_cand_turn_mdeg + dmdeg;
                        state->mag_cal_cand_turn_mdeg = (next > 3600000ull) ? 3600000u : (uint32_t)next;
                    }
                }
            }
        }
    }
    if (timestamp_ticks != 0) {
        state->last_gyro_ts = timestamp_ticks;
    }
}

void
sns_compass_set_gyro(SnsCompass *state, const int32_t gyro_q16[3])
{
    sns_compass_set_gyro_ts(state, gyro_q16, 0ull);
}

static void
mag_hist_reset(SnsCompass *state)
{
    state->mag_hist_count = 0u;
    state->mag_hist_pos = 0u;
    for (unsigned idx = 0; idx < 3u; ++idx) {
        state->mag_sum[idx] = 0ll;
    }
}

static void
mag_hist_push(SnsCompass *state, const int32_t mag_q16[3])
{
    const uint32_t slot = state->mag_hist_pos;
    if (state->mag_hist_count < SNS_COMPASS_MAG_AVG_WIN) {
        state->mag_hist_count++;
    } else {
        for (unsigned idx = 0; idx < 3u; ++idx) {
            state->mag_sum[idx] -= (int64_t)state->mag_hist[slot][idx];
        }
    }
    for (unsigned idx = 0; idx < 3u; ++idx) {
        state->mag_hist[slot][idx] = mag_q16[idx];
        state->mag_sum[idx] += (int64_t)mag_q16[idx];
        state->mag_raw[idx] = (int32_t)(state->mag_sum[idx] / (int64_t)state->mag_hist_count);
    }
    state->mag_hist_pos = (slot + 1u) % SNS_COMPASS_MAG_AVG_WIN;
}

static void
promote_trusted_mag_cal(SnsCompass *state, const int32_t bias_q16[3], int32_t accuracy, int full_reset)
{
    for (unsigned idx = 0; idx < 3u; ++idx) {
        state->trusted_mag_cal_bias[idx] = bias_q16[idx];
    }
    state->trusted_mag_cal_acc = accuracy;
    state->has_trusted_mag_cal = 1;
    state->mag_cal_polluted = 0;
    state->mag_cal_cand_valid = 0;
    state->mag_cal_cand_from_disturb = 0;
    state->mag_cal_cand_good = 0u;
    state->mag_cal_cand_turn_mdeg = 0u;
    state->mag_cal_cand_has_up = 0;
    state->mag_cal_cand_has_world = 0;
    if (full_reset) {
        state->mag_disturbed = 0;
        state->mag_disturb_hold = 0u;
        state->mag_disturb_reason = 0u;
        state->mag_cal_quarantine = 0u;
        state->vec_anom_cnt = 0u;
        state->earth_anom_cnt = 0u;
        state->mag_innov_reject_cnt = 0u;
        state->mag_innov_confirm_cnt = 0u;
        state->earth_ref_count = 0u;
        state->earth_ref_horiz_mut = 0u;
        state->earth_ref_has_up = 0;
        state->earth_reanchor_count = 0u;
        state->relock_steady_cnt = 0u;
        state->gyro_turn_acc_mdeg = 0u;
        if (state->fallback_valid && state->fallback_used_mag) {
            state->bias_handoff_steps = (uint32_t)kBiasHandoffSteps;
        }
        mag_hist_reset(state);
    }
}

static uint32_t
quat_abs_dot_q30(const int32_t lhs[4], const int32_t rhs[4])
{
    int64_t dot = 0;
    for (unsigned idx = 0; idx < 4u; ++idx) {
        dot += ((int64_t)lhs[idx] >> 15) * ((int64_t)rhs[idx] >> 15);
    }
    if (dot < 0) {
        dot = -dot;
    }
    return (dot > (int64_t)kQ30One) ? (uint32_t)kQ30One : (uint32_t)dot;
}

static void
pose_hist_record(SnsCompass *state, const int32_t mag_q16[3])
{
    if (!state->fallback_valid) {
        return;
    }
    if (state->pose_hist_count > 0u) {
        const uint32_t prev_slot = (state->pose_hist_pos + SNS_COMPASS_POSE_HIST_WIN - 1u) % SNS_COMPASS_POSE_HIST_WIN;
        if (quat_abs_dot_q30(state->fallback_q, state->pose_hist[prev_slot].q) > (uint32_t)kQuatCos7p5Q30) {
            return;
        }
    }
    SnsCompassPoseSnap *snap = &state->pose_hist[state->pose_hist_pos];
    for (unsigned idx = 0; idx < 4u; ++idx) {
        snap->q[idx] = state->fallback_q[idx];
    }
    for (unsigned idx = 0; idx < 3u; ++idx) {
        snap->mag[idx] = mag_q16[idx];
    }
    snap->seq = state->mag_sample_seq;
    state->pose_hist_pos = (state->pose_hist_pos + 1u) % SNS_COMPASS_POSE_HIST_WIN;
    if (state->pose_hist_count < SNS_COMPASS_POSE_HIST_WIN) {
        state->pose_hist_count++;
    }
}

static int
pose_hist_proves_candidate(const SnsCompass *state, const int32_t live_cw[3])
{
    if (state->pose_hist_count < (uint32_t)kPoseHistMinSnaps) {
        return 0;
    }
    uint32_t matched_idx[SNS_COMPASS_POSE_HIST_WIN];
    uint32_t matched = 0u;
    for (uint32_t back = 0u; back < state->pose_hist_count; ++back) {
        const uint32_t slot = (state->pose_hist_pos + SNS_COMPASS_POSE_HIST_WIN - 1u - back) % SNS_COMPASS_POSE_HIST_WIN;
        const SnsCompassPoseSnap *snap = &state->pose_hist[slot];
        if ((state->mag_sample_seq - snap->seq) > (uint32_t)kPoseHistMaxAgeSamples) {
            break;
        }
        uint32_t snorm = 0u;
        int32_t scal[3] = {0, 0, 0};
        (void)bias_eval_earth(snap->mag, state->mag_cal_cand_bias, NULL, &snorm, NULL, NULL, scal);
        if (snorm < (uint32_t)kMagCalCandNormMinMut || snorm > (uint32_t)kMagCalCandNormMaxMut) {
            break;
        }
        int32_t nq[3], wq[3], uq[3];
        quat_to_axes_q15(snap->q, nq, wq, uq);
        const int32_t sw[3] = {
            (int32_t)(((int64_t)nq[0] * (int64_t)scal[0] + (int64_t)nq[1] * (int64_t)scal[1] + (int64_t)nq[2] * (int64_t)scal[2]) >> 15),
            (int32_t)(((int64_t)wq[0] * (int64_t)scal[0] + (int64_t)wq[1] * (int64_t)scal[1] + (int64_t)wq[2] * (int64_t)scal[2]) >> 15),
            (int32_t)(((int64_t)uq[0] * (int64_t)scal[0] + (int64_t)uq[1] * (int64_t)scal[1] + (int64_t)uq[2] * (int64_t)scal[2]) >> 15),
        };
        uint64_t dsq = 0ull;
        for (unsigned idx = 0; idx < 3u; ++idx) {
            const int64_t d = (int64_t)sw[idx] - (int64_t)live_cw[idx];
            const uint64_t ad = (d < 0) ? (uint64_t)(-d) : (uint64_t)d;
            dsq += ad * ad;
        }
        if (isqrt64(dsq) > (uint64_t)kPoseHistTolWorldMut) {
            break;
        }
        matched_idx[matched++] = slot;
    }
    if (matched < (uint32_t)kPoseHistMinSnaps) {
        return 0;
    }
    for (uint32_t i = 0u; i < matched; ++i) {
        for (uint32_t j = i + 1u; j < matched; ++j) {
            if (quat_abs_dot_q30(state->pose_hist[matched_idx[i]].q,
                                 state->pose_hist[matched_idx[j]].q) <= (uint32_t)kQuatCos30Q30) {
                return 1;
            }
        }
    }
    return 0;
}

static int
pose_hist_proves_cold_boot(const SnsCompass *state, const int32_t live_cw[3], const int32_t live_mag_q16[3])
{
    if (state->has_trusted_mag_cal || !state->has_default_mag_bias || !state->fallback_valid ||
        state->pose_hist_count < (uint32_t)kPoseHistColdMinSnaps) {
        return 0;
    }
    int32_t live_old_cal[3] = {0, 0, 0};
    (void)bias_eval_earth(live_mag_q16, state->default_mag_bias, NULL, NULL, NULL, NULL, live_old_cal);
    int32_t lnq[3], lwq[3], luq[3];
    quat_to_axes_q15(state->fallback_q, lnq, lwq, luq);
    const int32_t live_old_w[3] = {
        (int32_t)(((int64_t)lnq[0] * (int64_t)live_old_cal[0] + (int64_t)lnq[1] * (int64_t)live_old_cal[1] + (int64_t)lnq[2] * (int64_t)live_old_cal[2]) >> 15),
        (int32_t)(((int64_t)lwq[0] * (int64_t)live_old_cal[0] + (int64_t)lwq[1] * (int64_t)live_old_cal[1] + (int64_t)lwq[2] * (int64_t)live_old_cal[2]) >> 15),
        (int32_t)(((int64_t)luq[0] * (int64_t)live_old_cal[0] + (int64_t)luq[1] * (int64_t)live_old_cal[1] + (int64_t)luq[2] * (int64_t)live_old_cal[2]) >> 15),
    };

    uint32_t matched_idx[SNS_COMPASS_POSE_HIST_WIN];
    int32_t matched_up[SNS_COMPASS_POSE_HIST_WIN][3];
    uint32_t matched = 0u;
    uint32_t max_old_err = 0u;
    for (uint32_t back = 0u; back < state->pose_hist_count; ++back) {
        const uint32_t slot = (state->pose_hist_pos + SNS_COMPASS_POSE_HIST_WIN - 1u - back) % SNS_COMPASS_POSE_HIST_WIN;
        const SnsCompassPoseSnap *snap = &state->pose_hist[slot];
        if ((state->mag_sample_seq - snap->seq) > (uint32_t)kPoseHistMaxAgeSamples) {
            break;
        }
        uint32_t snorm = 0u;
        int32_t scal[3] = {0, 0, 0};
        (void)bias_eval_earth(snap->mag, state->mag_cal_cand_bias, NULL, &snorm, NULL, NULL, scal);
        if (snorm < (uint32_t)kMagCalCandNormMinMut || snorm > (uint32_t)kMagCalCandNormMaxMut) {
            break;
        }
        int32_t nq[3], wq[3], uq[3];
        quat_to_axes_q15(snap->q, nq, wq, uq);
        const int32_t sw[3] = {
            (int32_t)(((int64_t)nq[0] * (int64_t)scal[0] + (int64_t)nq[1] * (int64_t)scal[1] + (int64_t)nq[2] * (int64_t)scal[2]) >> 15),
            (int32_t)(((int64_t)wq[0] * (int64_t)scal[0] + (int64_t)wq[1] * (int64_t)scal[1] + (int64_t)wq[2] * (int64_t)scal[2]) >> 15),
            (int32_t)(((int64_t)uq[0] * (int64_t)scal[0] + (int64_t)uq[1] * (int64_t)scal[1] + (int64_t)uq[2] * (int64_t)scal[2]) >> 15),
        };
        uint64_t dsq = 0ull;
        for (unsigned idx = 0; idx < 3u; ++idx) {
            const int64_t d = (int64_t)sw[idx] - (int64_t)live_cw[idx];
            const uint64_t ad = (d < 0) ? (uint64_t)(-d) : (uint64_t)d;
            dsq += ad * ad;
        }
        if (isqrt64(dsq) > (uint64_t)kPoseHistColdTolWorldMut) {
            break;
        }
        int32_t old_cal[3] = {0, 0, 0};
        (void)bias_eval_earth(snap->mag, state->default_mag_bias, NULL, NULL, NULL, NULL, old_cal);
        const int32_t old_w[3] = {
            (int32_t)(((int64_t)nq[0] * (int64_t)old_cal[0] + (int64_t)nq[1] * (int64_t)old_cal[1] + (int64_t)nq[2] * (int64_t)old_cal[2]) >> 15),
            (int32_t)(((int64_t)wq[0] * (int64_t)old_cal[0] + (int64_t)wq[1] * (int64_t)old_cal[1] + (int64_t)wq[2] * (int64_t)old_cal[2]) >> 15),
            (int32_t)(((int64_t)uq[0] * (int64_t)old_cal[0] + (int64_t)uq[1] * (int64_t)old_cal[1] + (int64_t)uq[2] * (int64_t)old_cal[2]) >> 15),
        };
        uint64_t old_dsq = 0ull;
        for (unsigned idx = 0; idx < 3u; ++idx) {
            const int64_t d = (int64_t)old_w[idx] - (int64_t)live_old_w[idx];
            const uint64_t ad = (d < 0) ? (uint64_t)(-d) : (uint64_t)d;
            old_dsq += ad * ad;
        }
        const uint32_t old_err = (uint32_t)isqrt64(old_dsq);
        if (old_err > max_old_err) {
            max_old_err = old_err;
        }
        matched_up[matched][0] = uq[0];
        matched_up[matched][1] = uq[1];
        matched_up[matched][2] = uq[2];
        matched_idx[matched++] = slot;
    }
    if (matched < (uint32_t)kPoseHistColdMinSnaps) {
        return 0;
    }
    if (!state->mag_disturbed && max_old_err < (uint32_t)kPoseHistColdOldSpreadMut) {
        return 0;
    }
    /* Require both >=35 deg total rotation AND >=25 deg 3D gravity-axis tilt diversity:
     * pure yaw around Z cannot constrain b_z and would promote Z-shifted biases (boot #342). */
    int have_rot_spread = 0;
    int have_tilt_spread = 0;
    for (uint32_t i = 0u; i < matched; ++i) {
        if (quat_abs_dot_q30(state->pose_hist[matched_idx[i]].q,
                             state->fallback_q) <= (uint32_t)kQuatCos17p5Q30) {
            have_rot_spread = 1;
        }
        const int64_t live_up_dot =
            ((int64_t)matched_up[i][0] * (int64_t)luq[0] +
             (int64_t)matched_up[i][1] * (int64_t)luq[1] +
             (int64_t)matched_up[i][2] * (int64_t)luq[2]) >> 15;
        if (live_up_dot <= (int64_t)kPoseHistTiltCosMinQ15) {
            have_tilt_spread = 1;
        }
        for (uint32_t j = i + 1u; j < matched; ++j) {
            if (quat_abs_dot_q30(state->pose_hist[matched_idx[i]].q,
                                 state->pose_hist[matched_idx[j]].q) <= (uint32_t)kQuatCos17p5Q30) {
                have_rot_spread = 1;
            }
            const int64_t pair_up_dot =
                ((int64_t)matched_up[i][0] * (int64_t)matched_up[j][0] +
                 (int64_t)matched_up[i][1] * (int64_t)matched_up[j][1] +
                 (int64_t)matched_up[i][2] * (int64_t)matched_up[j][2]) >> 15;
            if (pair_up_dot <= (int64_t)kPoseHistTiltCosMinQ15) {
                have_tilt_spread = 1;
            }
        }
        if (have_rot_spread && have_tilt_spread) {
            return 1;
        }
    }
    return 0;
}

static int
eval_mag_cal_candidate(SnsCompass *state, const int32_t mag_q16[3], const int32_t *up_ptr, int accel_steady)
{
    if (!state->mag_cal_cand_valid || state->mag_cal_cand_acc < 3) {
        return 0;
    }
    uint32_t c_norm = 0u;
    uint32_t c_horiz = 0u;
    int32_t c_up = 0;
    int32_t c_cal[3] = {0, 0, 0};
    (void)bias_eval_earth(mag_q16, state->mag_cal_cand_bias, up_ptr, &c_norm, &c_horiz, &c_up, c_cal);
    const int strict_earth =
        c_norm >= (uint32_t)kMagCalCandNormMinMut && c_norm <= (uint32_t)kMagCalCandNormMaxMut &&
        (!up_ptr || (c_horiz >= (uint32_t)kMagCalCandHorizMinMut && c_horiz <= (uint32_t)kMagCalCandHorizMaxMut));
    if (!strict_earth) {
        state->mag_cal_cand_good = 0u;
        state->mag_cal_cand_turn_mdeg = 0u;
        state->mag_cal_cand_has_up = 0;
        state->mag_cal_cand_has_world = 0;
        return 0;
    }

    int have_cw = 0;
    int32_t cw[3] = {0, 0, 0};
    if (state->fallback_valid) {
        int32_t north_q15[3];
        int32_t west_q15[3];
        int32_t up_q15[3];
        quat_to_axes_q15(state->fallback_q, north_q15, west_q15, up_q15);
        cw[0] = (int32_t)(((int64_t)north_q15[0] * (int64_t)c_cal[0] +
                           (int64_t)north_q15[1] * (int64_t)c_cal[1] +
                           (int64_t)north_q15[2] * (int64_t)c_cal[2]) >> 15);
        cw[1] = (int32_t)(((int64_t)west_q15[0] * (int64_t)c_cal[0] +
                           (int64_t)west_q15[1] * (int64_t)c_cal[1] +
                           (int64_t)west_q15[2] * (int64_t)c_cal[2]) >> 15);
        cw[2] = (int32_t)(((int64_t)up_q15[0] * (int64_t)c_cal[0] +
                           (int64_t)up_q15[1] * (int64_t)c_cal[1] +
                           (int64_t)up_q15[2] * (int64_t)c_cal[2]) >> 15);
        have_cw = 1;
    }

    int steady_ok = 1;
    if (state->mag_cal_cand_good > 0u) {
        const uint32_t dn = (c_norm >= state->mag_cal_cand_norm_mut)
            ? (c_norm - state->mag_cal_cand_norm_mut)
            : (state->mag_cal_cand_norm_mut - c_norm);
        if (dn > (uint32_t)kMagCalCandTolNormMut) {
            steady_ok = 0;
        }
        if (steady_ok && accel_steady && state->mag_cal_cand_has_up) {
            const int64_t du = (int64_t)c_up - (int64_t)state->mag_cal_cand_up_mut;
            const uint32_t adu = (du < 0) ? (uint32_t)(-du) : (uint32_t)du;
            if (adu > (uint32_t)kMagCalCandTolUpMut) {
                steady_ok = 0;
            }
        }
        if (steady_ok && have_cw && state->mag_cal_cand_has_world) {
            uint64_t dsq = 0ull;
            for (unsigned idx = 0; idx < 3u; ++idx) {
                const int64_t d = (int64_t)cw[idx] - (int64_t)state->mag_cal_cand_world_mut[idx];
                const uint64_t ad = (d < 0) ? (uint64_t)(-d) : (uint64_t)d;
                dsq += ad * ad;
            }
            if (isqrt64(dsq) > (uint64_t)kMagCalCandTolWorldMut) {
                steady_ok = 0;
            }
        }
    }

    if (!steady_ok || state->mag_cal_cand_good == 0u) {
        state->mag_cal_cand_norm_mut = c_norm;
        state->mag_cal_cand_up_mut = c_up;
        state->mag_cal_cand_has_up = accel_steady ? 1 : 0;
        if (have_cw) {
            for (unsigned idx = 0; idx < 3u; ++idx) {
                state->mag_cal_cand_world_mut[idx] = cw[idx];
            }
            state->mag_cal_cand_has_world = 1;
        } else {
            state->mag_cal_cand_has_world = 0;
        }
        state->mag_cal_cand_good = 1u;
        state->mag_cal_cand_turn_mdeg = 0u;
        return 0;
    }

    if (state->mag_cal_cand_good < 0xffffffffu) {
        state->mag_cal_cand_good++;
    }
    if (accel_steady && !state->mag_cal_cand_has_up) {
        state->mag_cal_cand_up_mut = c_up;
        state->mag_cal_cand_has_up = 1;
    }
    if (have_cw && !state->mag_cal_cand_has_world) {
        for (unsigned idx = 0; idx < 3u; ++idx) {
            state->mag_cal_cand_world_mut[idx] = cw[idx];
        }
        state->mag_cal_cand_has_world = 1;
    }

    const int boot_escape = !state->has_trusted_mag_cal &&
        state->earth_ref_count < (uint32_t)kEarthRefMinSamples &&
        state->mag_cal_cand_good >= (uint32_t)kMagCalCandBootSamples;
    const int clean_ok = !state->mag_cal_cand_from_disturb &&
        !state->mag_disturbed && state->mag_disturb_hold == 0u &&
        state->mag_cal_quarantine == 0u &&
        state->mag_cal_cand_good >= (uint32_t)kMagCalCandCleanSamples;
    const int rot_proof = state->mag_cal_cand_has_world &&
        state->mag_cal_cand_good >= (uint32_t)kMagCalCandRotSamples &&
        state->mag_cal_cand_turn_mdeg >= (uint32_t)kMagCalCandRotTurnMdeg;
    const int retro_proof = have_cw &&
        state->mag_cal_cand_good >= (uint32_t)kMagCalCandRetroSamples &&
        pose_hist_proves_candidate(state, cw);
    const int cold_boot_proof = !state->has_trusted_mag_cal && have_cw &&
        state->mag_cal_cand_good >= (uint32_t)kMagCalCandRetroSamples &&
        pose_hist_proves_cold_boot(state, cw, mag_q16);

    if (boot_escape || clean_ok || rot_proof || retro_proof || cold_boot_proof) {
        int32_t promoted[3];
        for (unsigned idx = 0; idx < 3u; ++idx) {
            promoted[idx] = state->mag_cal_cand_bias[idx];
        }
        promote_trusted_mag_cal(state, promoted, state->mag_cal_cand_acc, 1);
        return 1;
    }
    return 0;
}

void
sns_compass_set_mag(SnsCompass *state, const int32_t mag_q16[3], int32_t accuracy)
{
    state->mag_sample_seq++;
    int still_jump = 0;
    if (state->has_mag_prev_inst && state->gyro_valid && !gyro_is_rotating(state)) {
        if (bias_drift_mut(mag_q16, state->mag_prev_inst) > (uint32_t)kMagStillJumpMut) {
            still_jump = 1;
        }
    }
    for (unsigned idx = 0; idx < 3u; ++idx) {
        state->mag_prev_inst[idx] = mag_q16[idx];
    }
    state->has_mag_prev_inst = 1;

    if (!still_jump) {
        pose_hist_record(state, mag_q16);
    }

    int32_t up_vec[3];
    uint64_t accel_norm_q16 = 0;
    const int have_up = (accel_up_and_norm(state, up_vec, &accel_norm_q16) == 0);
    const int accel_steady = have_up &&
        accel_norm_q16 >= (uint64_t)kFusionAccelMinQ16 &&
        accel_norm_q16 <= (uint64_t)kFusionAccelMaxQ16;
    const int32_t *up_ptr = have_up ? up_vec : NULL;

    if (state->mag_cal_cand_valid && !still_jump) {
        (void)eval_mag_cal_candidate(state, mag_q16, up_ptr, accel_steady);
    }

    const int32_t *ref_bias = select_instant_ref_bias(state);
    if (!ref_bias) {
        mag_hist_push(state, mag_q16);
        state->mag_acc = accuracy;
        state->mag_valid = 1;
        refresh_active_bias(state);
        return;
    }

    uint32_t norm_mut = 0u;
    uint32_t horiz_mut = 0u;
    int32_t up_mut = 0;
    int32_t cal_mut[3] = {0, 0, 0};
    const int in_earth = bias_eval_earth(mag_q16, ref_bias, up_ptr, &norm_mut, &horiz_mut, &up_mut, cal_mut);

    uint32_t reason = 0u;
    int earth_anomaly = 0;
    int earth_soft_skip = 0;
    if (!in_earth) {
        const int hard_out =
            norm_mut < (uint32_t)kEarthHardNormMinMut || norm_mut > (uint32_t)kEarthHardNormMaxMut ||
            (up_ptr && (horiz_mut < (uint32_t)kEarthHardHorizMinMut || horiz_mut > (uint32_t)kEarthHardHorizMaxMut));
        if (hard_out) {
            state->earth_anom_cnt = (uint32_t)kEarthRangeDebounce;
            earth_anomaly = 1;
            reason |= kMagDistReasonEarth;
        } else {
            if (state->earth_anom_cnt < (uint32_t)kEarthRangeDebounce) {
                state->earth_anom_cnt++;
            }
            if (state->earth_anom_cnt >= (uint32_t)kEarthRangeDebounce) {
                earth_anomaly = 1;
                reason |= kMagDistReasonEarth;
            } else {
                earth_soft_skip = 1;
            }
        }
    } else {
        state->earth_anom_cnt = 0u;
    }
    if (still_jump) {
        reason |= kMagDistReasonJump;
    }

    int ref_anomaly = 0;
    int vec_anomaly = 0;
    int vec_soft_skip = 0;
    int have_world = 0;
    int64_t b_north = 0;
    int64_t b_west = 0;
    int64_t b_up_q = 0;
    uint32_t vec_err_mut = 0u;

    if (in_earth && state->earth_ref_count >= (uint32_t)kEarthRefMinSamples && state->bias_handoff_steps == 0u) {
        const uint32_t norm_diff = (norm_mut >= state->earth_ref_norm_mut)
            ? (norm_mut - state->earth_ref_norm_mut)
            : (state->earth_ref_norm_mut - norm_mut);
        uint32_t up_diff = 0u;
        if (accel_steady && state->earth_ref_has_up) {
            const int64_t du = (int64_t)up_mut - (int64_t)state->earth_ref_up_mut;
            up_diff = (du < 0) ? (uint32_t)(-du) : (uint32_t)du;
        }
        if (norm_diff > (uint32_t)kEarthNormDevMut) {
            ref_anomaly = 1;
            reason |= kMagDistReasonNorm;
        }
        if (up_diff > (uint32_t)kEarthUpDevMut) {
            ref_anomaly = 1;
            reason |= kMagDistReasonUp;
        }

        /* 3D inertial world-frame vector consistency check (inspired by Apple CLAttitudeDependentKFCalibrator
         * dAngle/magDiff/incDiff): trips immediately on >9.5 uT spikes and debounces moderate 5.5-9.5 uT
         * deviations across 3 samples (60 ms) to ignore isolated raw AK0991x noise without leaking heading. */
        if (state->compass_mode == SNS_COMPASS_MODE_FUSION &&
            state->fallback_valid && state->fallback_used_mag &&
            state->earth_ref_horiz_mut >= (uint32_t)kEarthHorizMinMut) {
            int32_t north_q15[3];
            int32_t west_q15[3];
            int32_t up_q15[3];
            quat_to_axes_q15(state->fallback_q, north_q15, west_q15, up_q15);
            b_north = ((int64_t)north_q15[0] * (int64_t)cal_mut[0] +
                       (int64_t)north_q15[1] * (int64_t)cal_mut[1] +
                       (int64_t)north_q15[2] * (int64_t)cal_mut[2]) >> 15;
            b_west = ((int64_t)west_q15[0] * (int64_t)cal_mut[0] +
                      (int64_t)west_q15[1] * (int64_t)cal_mut[1] +
                      (int64_t)west_q15[2] * (int64_t)cal_mut[2]) >> 15;
            b_up_q = ((int64_t)up_q15[0] * (int64_t)cal_mut[0] +
                      (int64_t)up_q15[1] * (int64_t)cal_mut[1] +
                      (int64_t)up_q15[2] * (int64_t)cal_mut[2]) >> 15;
            const int64_t dn = b_north - (int64_t)state->earth_ref_horiz_mut;
            const int64_t dw = b_west;
            const int64_t du_w = state->earth_ref_has_up ? (b_up_q - (int64_t)state->earth_ref_up_mut) : 0ll;
            const uint64_t an = (dn < 0) ? (uint64_t)(-dn) : (uint64_t)dn;
            const uint64_t aw = (dw < 0) ? (uint64_t)(-dw) : (uint64_t)dw;
            const uint64_t au = (du_w < 0) ? (uint64_t)(-du_w) : (uint64_t)du_w;
            vec_err_mut = (uint32_t)isqrt64(an * an + aw * aw + au * au);
            state->last_vec_err_mut = vec_err_mut;
            have_world = 1;
            const uint32_t vec_lim = gyro_is_still(state)
                ? (uint32_t)kEarthVecStillDevMut : (uint32_t)kEarthVecMoveDevMut;
            if (vec_err_mut > (uint32_t)kEarthVecInstantDevMut) {
                state->vec_anom_cnt = (uint32_t)kEarthVecDebounce;
                vec_anomaly = 1;
                reason |= kMagDistReasonVec;
            } else if (vec_err_mut > vec_lim) {
                if (state->vec_anom_cnt < (uint32_t)kEarthVecDebounce) {
                    state->vec_anom_cnt++;
                }
                if (state->vec_anom_cnt >= (uint32_t)kEarthVecDebounce) {
                    vec_anomaly = 1;
                    reason |= kMagDistReasonVec;
                } else {
                    vec_soft_skip = 1;
                }
            } else {
                state->vec_anom_cnt = 0u;
            }
        } else {
            state->vec_anom_cnt = 0u;
        }

        /* Heading-preserving room-change re-anchor: only re-anchor local |B|/H/B_up when heading
         * STILL agrees with gyro (|B_west| <= 5 uT, B_north >= 12 uT), so a nearby magnet cannot hijack it. */
        const int heading_consistent = !have_world ||
            (b_north >= (int64_t)kEarthHorizMinMut &&
             ((b_west < 0 ? -b_west : b_west) <= (int64_t)kEarthReanchorMaxWestMut));
        if ((ref_anomaly || vec_anomaly) && !still_jump && heading_consistent &&
            norm_diff <= (uint32_t)kEarthReanchorMaxDevMut &&
            up_diff <= (uint32_t)kEarthReanchorMaxDevMut) {
            int match_cand = 0;
            if (state->earth_reanchor_count > 0u) {
                const uint32_t cn = (norm_mut >= state->earth_reanchor_norm_mut)
                    ? (norm_mut - state->earth_reanchor_norm_mut)
                    : (state->earth_reanchor_norm_mut - norm_mut);
                uint32_t cu = 0u;
                if (accel_steady && state->earth_ref_has_up) {
                    const int64_t du2 = (int64_t)up_mut - (int64_t)state->earth_reanchor_up_mut;
                    cu = (du2 < 0) ? (uint32_t)(-du2) : (uint32_t)du2;
                }
                match_cand = (cn <= (uint32_t)kEarthReanchorTolNormMut &&
                              cu <= (uint32_t)kEarthReanchorTolUpMut);
            }
            if (match_cand) {
                state->earth_reanchor_count++;
            } else {
                state->earth_reanchor_norm_mut = norm_mut;
                state->earth_reanchor_horiz_mut = horiz_mut;
                state->earth_reanchor_up_mut = up_mut;
                state->earth_reanchor_count = 1u;
            }
            if (state->earth_reanchor_count >= (uint32_t)kEarthReanchorSamples) {
                state->earth_ref_norm_mut = norm_mut;
                state->earth_ref_horiz_mut = horiz_mut;
                if (accel_steady) {
                    state->earth_ref_up_mut = up_mut;
                    state->earth_ref_has_up = 1;
                }
                state->earth_reanchor_count = 0u;
                state->vec_anom_cnt = 0u;
                ref_anomaly = 0;
                vec_anomaly = 0;
                reason &= ~(kMagDistReasonNorm | kMagDistReasonUp | kMagDistReasonVec);
            }
        } else {
            state->earth_reanchor_count = 0u;
        }

        /* Rotation-verified uniform-field relock: if phone booted near localized iron and later moved to a
         * clean uniform Earth field, B_world = R(q) * B_cal stays invariant in 3D WHILE turning >=90 deg by gyro. */
        if ((ref_anomaly || vec_anomaly || state->mag_disturbed) && !still_jump && accel_steady && have_world) {
            const int32_t bw[3] = {(int32_t)b_north, (int32_t)b_west, (int32_t)b_up_q};
            int match_relock = 0;
            if (state->relock_steady_cnt > 0u) {
                uint64_t dsq = 0ull;
                for (unsigned idx = 0; idx < 3u; ++idx) {
                    const int64_t d = (int64_t)bw[idx] - (int64_t)state->relock_world_mut[idx];
                    const uint64_t ad = (d < 0) ? (uint64_t)(-d) : (uint64_t)d;
                    dsq += ad * ad;
                }
                match_relock = (isqrt64(dsq) <= (uint64_t)kRelockTolVecMut);
            }
            if (match_relock) {
                state->relock_steady_cnt++;
            } else {
                for (unsigned idx = 0; idx < 3u; ++idx) {
                    state->relock_world_mut[idx] = bw[idx];
                }
                state->relock_steady_cnt = 1u;
                state->gyro_turn_acc_mdeg = 0u;
            }
            if (state->relock_steady_cnt >= (uint32_t)kRelockSteadySamples &&
                state->gyro_turn_acc_mdeg >= (uint32_t)kRelockMinTurnMdeg) {
                state->earth_ref_norm_mut = norm_mut;
                state->earth_ref_horiz_mut = horiz_mut;
                state->earth_ref_up_mut = up_mut;
                state->earth_ref_has_up = 1;
                state->mag_disturbed = 0;
                state->mag_disturb_hold = 0u;
                state->mag_cal_quarantine = 0u;
                state->vec_anom_cnt = 0u;
                state->earth_anom_cnt = 0u;
                state->bias_handoff_steps = (uint32_t)kBiasHandoffSteps;
                state->relock_steady_cnt = 0u;
                state->gyro_turn_acc_mdeg = 0u;
                state->earth_reanchor_count = 0u;
                ref_anomaly = 0;
                vec_anomaly = 0;
                reason = 0u;
            }
        } else {
            state->relock_steady_cnt = 0u;
            state->gyro_turn_acc_mdeg = 0u;
        }
    } else {
        state->vec_anom_cnt = 0u;
        state->earth_reanchor_count = 0u;
        state->relock_steady_cnt = 0u;
        state->gyro_turn_acc_mdeg = 0u;
    }

    if (earth_anomaly || ref_anomaly || vec_anomaly || still_jump) {
        if (!state->mag_disturbed) {
            state->mag_disturb_events++;
        }
        state->mag_disturbed = 1;
        state->mag_disturb_reason = reason;
        state->mag_disturb_hold = (uint32_t)kMagDisturbHoldSamples;
        state->mag_cal_quarantine = (uint32_t)kMagCalQuarantineSamples;
        state->mag_innov_reject_cnt = 0u;
        state->mag_innov_confirm_cnt = 0u;
        mag_hist_reset(state);
        for (unsigned idx = 0; idx < 3u; ++idx) {
            state->mag_raw[idx] = mag_q16[idx];
        }
        state->mag_acc = accuracy;
        state->mag_valid = 1;
        refresh_active_bias(state);
        return;
    }

    if (earth_soft_skip || vec_soft_skip) {
        state->mag_acc = accuracy;
        state->mag_valid = 1;
        refresh_active_bias(state);
        return;
    }

    mag_hist_push(state, mag_q16);
    if (state->mag_disturb_hold > 0u) {
        state->mag_disturb_hold--;
        if (state->mag_disturb_hold == 0u) {
            state->mag_disturbed = 0;
            state->mag_disturb_reason = 0u;
        }
    }
    if (state->mag_cal_quarantine > 0u) {
        state->mag_cal_quarantine--;
    }

    /* Bootstrap local Earth ref quickly (~0.5 s of calm gravity), then freeze it while stationary
     * and only track very slowly (div=512) during clean rotation so a slowly-approaching magnet
     * or violent mid-swing linear acceleration can never corrupt earth_ref. */
    const int calm_boot_gyro = (!state->gyro_valid || gyro_l1_rate_q16(state) <= (uint64_t)kEarthBootGyroMaxQ16);
    const int bootstrapping = (state->earth_ref_count < (uint32_t)kEarthRefMinSamples);
    const int allow_boot_step = bootstrapping && calm_boot_gyro && (!have_up || accel_steady);
    const int allow_slow_track = !bootstrapping &&
        !state->mag_disturbed && state->mag_cal_quarantine == 0u && !state->last_yaw_rejected &&
        !gyro_is_still(state) && (!have_world || vec_err_mut <= (uint32_t)kEarthRefTrackMaxVecMut);
    if (allow_boot_step || allow_slow_track) {
        if (state->earth_ref_count == 0u) {
            state->earth_ref_norm_mut = norm_mut;
            if (have_up) {
                state->earth_ref_horiz_mut = horiz_mut;
            }
        } else {
            const int64_t div = bootstrapping ? 8ll : 512ll;
            const int64_t delta = (int64_t)norm_mut - (int64_t)state->earth_ref_norm_mut;
            state->earth_ref_norm_mut = (uint32_t)((int64_t)state->earth_ref_norm_mut + (delta / div));
            if (have_up) {
                if (state->earth_ref_horiz_mut == 0u) {
                    state->earth_ref_horiz_mut = horiz_mut;
                } else {
                    const int64_t hdelta = (int64_t)horiz_mut - (int64_t)state->earth_ref_horiz_mut;
                    state->earth_ref_horiz_mut = (uint32_t)((int64_t)state->earth_ref_horiz_mut + (hdelta / div));
                }
            }
        }
        if (accel_steady) {
            if (!state->earth_ref_has_up) {
                state->earth_ref_up_mut = up_mut;
                state->earth_ref_has_up = 1;
            } else {
                const int64_t div = bootstrapping ? 8ll : 512ll;
                const int64_t delta = (int64_t)up_mut - (int64_t)state->earth_ref_up_mut;
                state->earth_ref_up_mut = (int32_t)((int64_t)state->earth_ref_up_mut + (delta / div));
            }
        }
        if (state->earth_ref_count < 0xffffffffu) {
            state->earth_ref_count++;
        }
    }

    state->mag_acc = accuracy;
    state->mag_valid = 1;
    refresh_active_bias(state);
}

void
sns_compass_set_mag_cal(SnsCompass *state, const int32_t bias_q16[3], int32_t accuracy)
{
    const int was_trusted = mag_cal_is_trusted(state);
    for (unsigned idx = 0; idx < 3u; ++idx) {
        state->mag_cal_bias[idx] = bias_q16[idx];
    }
    state->mag_cal_acc = accuracy;
    state->mag_cal_valid = 1;

    if (accuracy <= 0) {
        /* Transient acc=0 during anomalies clears pending candidates but preserves verified trusted_mag_cal_bias. */
        state->mag_cal_polluted = 0;
        state->mag_cal_cand_valid = 0;
        state->mag_cal_cand_good = 0u;
        state->mag_cal_cand_turn_mdeg = 0u;
        refresh_active_bias(state);
        return;
    }

    const int in_disturb = (state->mag_disturbed || state->mag_disturb_hold > 0u || state->mag_cal_quarantine > 0u);
    const int32_t *drift_ref = state->has_trusted_mag_cal ? state->trusted_mag_cal_bias
        : (state->has_default_mag_bias ? state->default_mag_bias : NULL);
    const uint32_t drift = drift_ref ? bias_drift_mut(bias_q16, drift_ref) : 0u;

    if (!in_disturb && (!drift_ref || drift <= (uint32_t)kMagCalFastDriftMut)) {
        const int allow_update = !state->has_trusted_mag_cal ||
            accuracy >= state->trusted_mag_cal_acc || accuracy >= 2;
        if (allow_update) {
            const int need_ref_reset = (!was_trusted && accuracy >= 2);
            promote_trusted_mag_cal(state, bias_q16, accuracy, 0);
            if (need_ref_reset) {
                state->earth_ref_count = 0u;
                state->earth_ref_horiz_mut = 0u;
                state->earth_ref_has_up = 0;
                state->earth_reanchor_count = 0u;
                state->relock_steady_cnt = 0u;
                state->gyro_turn_acc_mdeg = 0u;
            }
        } else {
            state->mag_cal_polluted = 0;
        }
        refresh_active_bias(state);
        return;
    }

    state->mag_cal_polluted = 1;
    state->mag_cal_rejected++;

    /* Allow acc=3 with drift <= 15 uT that arrived during transient disturbance to enter candidate
     * verification instead of being silently dropped (boot #342 25 s mrej black hole). */
    if (accuracy >= 3 && drift <= (uint32_t)kMagCalRemagMaxDriftMut) {
        if (state->mag_cal_cand_valid &&
            bias_drift_mut(bias_q16, state->mag_cal_cand_bias) <= (uint32_t)kMagCalCandStableMut) {
            for (unsigned idx = 0; idx < 3u; ++idx) {
                state->mag_cal_cand_bias[idx] = bias_q16[idx];
            }
            state->mag_cal_cand_acc = accuracy;
            if (in_disturb) {
                state->mag_cal_cand_from_disturb = 1;
            }
        } else {
            for (unsigned idx = 0; idx < 3u; ++idx) {
                state->mag_cal_cand_bias[idx] = bias_q16[idx];
            }
            state->mag_cal_cand_acc = accuracy;
            state->mag_cal_cand_valid = 1;
            state->mag_cal_cand_from_disturb = in_disturb ? 1 : 0;
            state->mag_cal_cand_good = 0u;
            state->mag_cal_cand_turn_mdeg = 0u;
            state->mag_cal_cand_has_up = 0;
            state->mag_cal_cand_has_world = 0;
        }
    } else {
        state->mag_cal_cand_valid = 0;
        state->mag_cal_cand_good = 0u;
        state->mag_cal_cand_turn_mdeg = 0u;
    }
    refresh_active_bias(state);
}

static uint64_t
quat_l1_dist_q30(const int32_t lhs[4], const int32_t rhs[4])
{
    uint64_t sum_pos = 0;
    uint64_t sum_neg = 0;
    for (unsigned idx = 0; idx < 4u; ++idx) {
        const int64_t diff_pos = (int64_t)lhs[idx] - (int64_t)rhs[idx];
        const int64_t diff_neg = (int64_t)lhs[idx] + (int64_t)rhs[idx];
        sum_pos += (diff_pos < 0) ? (uint64_t)(-diff_pos) : (uint64_t)diff_pos;
        sum_neg += (diff_neg < 0) ? (uint64_t)(-diff_neg) : (uint64_t)diff_neg;
    }
    return (sum_pos < sum_neg) ? sum_pos : sum_neg;
}

static int
rotv_gravity_agrees(const SnsCompass *state, const int32_t quat_q30[4])
{
    if (!state->accel_valid) {
        return 1;
    }
    int64_t up_raw[3];
    uint64_t norm2 = 0;
    for (unsigned idx = 0; idx < 3u; ++idx) {
        const int64_t val = -(int64_t)state->accel_ios[idx];
        up_raw[idx] = val;
        const uint64_t mag = (val < 0) ? (uint64_t)(-val) : (uint64_t)val;
        if (mag > 0x1000000ull) {
            return 1;
        }
        norm2 += mag * mag;
    }
    const uint64_t norm_q16 = isqrt64(norm2);
    if (norm_q16 < (uint64_t)kRotvGravAccelMinQ16 || norm_q16 > (uint64_t)kRotvGravAccelMaxQ16) {
        return 1;
    }
    int32_t accel_up[3];
    if (normalize_vec3_q15(up_raw, accel_up) != 0) {
        return 1;
    }
    const int64_t qx = quat_q30[0] >> 15;
    const int64_t qy = quat_q30[1] >> 15;
    const int64_t qz = quat_q30[2] >> 15;
    const int64_t qw = quat_q30[3] >> 15;
    const int32_t rotv_up[3] = {
        (int32_t)((2ll * (qx * qz - qw * qy)) >> 15),
        (int32_t)((2ll * (qy * qz + qw * qx)) >> 15),
        (int32_t)(kQ15One - ((2ll * (qx * qx + qy * qy)) >> 15)),
    };
    const int64_t dot_q15 =
        ((int64_t)rotv_up[0] * (int64_t)accel_up[0] +
         (int64_t)rotv_up[1] * (int64_t)accel_up[1] +
         (int64_t)rotv_up[2] * (int64_t)accel_up[2]) >> 15;
    return dot_q15 >= (int64_t)kRotvGravCosMinQ15;
}

void
sns_compass_rotv_bounced(SnsCompass *state)
{
    state->rotv_has_prev = 0;
    state->rotv_gyro_stuck_cnt = 0;
    state->rotv_grav_stuck_cnt = 0;
    state->rotv_resume_moved_cnt = 0;
    state->rotv_post_bounce_skip = 2u;
}

int
sns_compass_set_rotv(SnsCompass *state, const uint32_t quat_bits[4], int32_t accuracy)
{
    int64_t enu[4];
    for (unsigned idx = 0; idx < 4u; ++idx) {
        if (!is_finite_float_bits(quat_bits[idx])) {
            return -1;
        }
        enu[idx] = sfx_scale(quat_bits[idx], 1, 1, 30, -(int64_t)kQ30One, (int64_t)kQ30One);
    }
    /* Convert Android ENU (+X=E, +Y=N, +Z=U) to Apple NWU (+X=N, +Y=W, +Z=U): q_nwu = q_z(-90 deg) * q_enu. */
    const int64_t raw_nwu[4] = {
        (enu[0] + enu[1]) >> 1,
        (enu[1] - enu[0]) >> 1,
        (enu[2] - enu[3]) >> 1,
        (enu[3] + enu[2]) >> 1,
    };
    int32_t normalized[4];
    if (normalize_quat_q30(raw_nwu, normalized) != 0) {
        return -1;
    }
    for (unsigned idx = 0; idx < 4u; ++idx) {
        state->rotv_raw_enu_milli[idx] = sfx_milli(quat_bits[idx]);
    }
    state->rotv_acc = accuracy;
    state->last_rotv_heading_mdeg = heading_from_quat_q30(normalized);

    if (state->compass_mode != SNS_COMPASS_MODE_ROTV) {
        for (unsigned idx = 0; idx < 4u; ++idx) {
            state->rotv_q[idx] = normalized[idx];
            state->rotv_prev_q[idx] = normalized[idx];
        }
        state->rotv_has_prev = 1;
        state->rotv_valid = 1;
        return 0;
    }

    if (state->rotv_stuck) {
        if (state->rotv_post_bounce_skip > 0u) {
            state->rotv_post_bounce_skip--;
            return 0;
        }
        const int grav_ok = rotv_gravity_agrees(state, normalized);
        const uint64_t dist_stuck = quat_l1_dist_q30(normalized, state->rotv_stuck_q);
        const uint64_t delta_prev = state->rotv_has_prev ? quat_l1_dist_q30(normalized, state->rotv_prev_q) : 0ull;
        if (grav_ok && (dist_stuck > (uint64_t)kRotvUnstickDeltaQ30 ||
                        (state->rotv_has_prev && delta_prev > (uint64_t)kRotvSameEpsQ30))) {
            state->rotv_resume_moved_cnt++;
        } else {
            state->rotv_resume_moved_cnt = 0;
        }
        for (unsigned idx = 0; idx < 4u; ++idx) {
            state->rotv_prev_q[idx] = normalized[idx];
        }
        state->rotv_has_prev = 1;
        if (state->rotv_resume_moved_cnt >= kRotvResumeThreshold) {
            for (unsigned idx = 0; idx < 4u; ++idx) {
                state->rotv_q[idx] = normalized[idx];
            }
            state->rotv_stuck = 0;
            state->rotv_valid = 1;
            state->rotv_gyro_stuck_cnt = 0;
            state->rotv_grav_stuck_cnt = 0;
            state->rotv_resume_moved_cnt = 0;
        }
        return 0;
    }

    const int grav_ok = rotv_gravity_agrees(state, normalized);
    if (state->rotv_has_prev) {
        const uint64_t delta_prev = quat_l1_dist_q30(normalized, state->rotv_prev_q);
        if (delta_prev <= (uint64_t)kRotvSameEpsQ30) {
            if (gyro_is_rotating(state)) {
                state->rotv_gyro_stuck_cnt++;
            } else if (state->rotv_gyro_stuck_cnt > 0u) {
                state->rotv_gyro_stuck_cnt--;
            }
        } else {
            state->rotv_gyro_stuck_cnt = 0;
        }
        if (!grav_ok && delta_prev <= (uint64_t)kRotvSlowDeltaQ30) {
            state->rotv_grav_stuck_cnt++;
        } else {
            state->rotv_grav_stuck_cnt = 0;
        }
        if (state->rotv_gyro_stuck_cnt >= kRotvStuckThreshold ||
            state->rotv_grav_stuck_cnt >= kRotvStuckThreshold) {
            state->rotv_stuck = 1;
            state->rotv_valid = 0;
            state->rotv_stuck_events++;
            state->rotv_resume_moved_cnt = 0;
            for (unsigned idx = 0; idx < 4u; ++idx) {
                state->rotv_stuck_q[idx] = normalized[idx];
                state->rotv_prev_q[idx] = normalized[idx];
                state->fallback_q[idx] = normalized[idx];
            }
            state->fallback_valid = 1;
            state->fallback_seeded_from_rotv = 1;
            state->fallback_used_mag = (accuracy >= 1) ? 1 : 0;
            if (!grav_ok) {
                int32_t aligned_q30[4];
                int aligned_mag = 0;
                if (fallback_quat_from_accel_mag(state, aligned_q30, &aligned_mag, NULL) == 0) {
                    for (unsigned idx = 0; idx < 4u; ++idx) {
                        state->fallback_q[idx] = aligned_q30[idx];
                    }
                    if (aligned_mag) {
                        state->fallback_used_mag = 1;
                    }
                }
            }
            return 0;
        }
    }

    for (unsigned idx = 0; idx < 4u; ++idx) {
        state->rotv_q[idx] = normalized[idx];
        state->rotv_prev_q[idx] = normalized[idx];
    }
    state->rotv_has_prev = 1;
    state->rotv_valid = 1;
    return 0;
}

int
sns_compass_build_report(SnsCompass *state, uint8_t out[SNS_COMPASS_REPORT_BYTES], int32_t *heading_mdeg_out)
{
    int32_t quat_q30[4];
    int used_rotv = 0;
    int used_fallback_mag = 0;
    int yaw_innov_rejected = 0;

    if (state->compass_mode == SNS_COMPASS_MODE_ROTV && state->rotv_valid) {
        for (unsigned idx = 0; idx < 4u; ++idx) {
            quat_q30[idx] = state->rotv_q[idx];
        }
        used_rotv = 1;
        refresh_active_bias(state);
    } else {
        int32_t meas_q30[4];
        int accel_steady = 0;
        const int meas_rc = fallback_quat_from_accel_mag(state, meas_q30, &used_fallback_mag, &accel_steady);
        if (meas_rc != 0) {
            if (!state->fallback_valid) {
                return -1;
            }
        } else {
            if (!state->fallback_valid || (used_fallback_mag && !state->fallback_used_mag)) {
                for (unsigned idx = 0; idx < 4u; ++idx) {
                    state->fallback_q[idx] = meas_q30[idx];
                }
                state->fallback_valid = 1;
                state->fallback_used_mag = used_fallback_mag;
                state->prev_bias_src = state->active_bias_src;
                state->bias_handoff_steps = 0u;
                state->mag_innov_reject_cnt = 0u;
                state->mag_innov_confirm_cnt = 0u;
            } else {
                if (state->active_bias_src != state->prev_bias_src) {
                    if (state->prev_bias_src == SNS_COMPASS_BIAS_DEFAULT &&
                        state->active_bias_src == SNS_COMPASS_BIAS_MAGCAL &&
                        used_fallback_mag && state->fallback_used_mag) {
                        state->bias_handoff_steps = kBiasHandoffSteps;
                        state->mag_innov_reject_cnt = 0u;
                        state->mag_innov_confirm_cnt = 0u;
                    }
                    state->prev_bias_src = state->active_bias_src;
                }
                if (accel_steady) {
                    int hold_accel_only = 0;
                    if (state->compass_mode == SNS_COMPASS_MODE_FUSION &&
                        used_fallback_mag && state->fallback_used_mag &&
                        state->fallback_gyro_steps > 0u && state->bias_handoff_steps == 0u) {
                        const uint32_t abs_yaw_err = abs_i32_to_u32(quat_yaw_diff_mdeg(state->fallback_q, meas_q30));
                        const int still_now = gyro_is_still(state);
                        if (abs_yaw_err > (uint32_t)kMagYawInnovGateMdeg) {
                            yaw_innov_rejected = 1;
                            hold_accel_only = 1;
                            state->mag_innov_reject_cnt++;
                            if (state->mag_innov_reject_cnt == (uint32_t)kMagInnovQuarantineTrip &&
                                state->mag_cal_quarantine < (uint32_t)kMagCalQuarantineSamples) {
                                state->mag_cal_quarantine = (uint32_t)kMagCalQuarantineSamples;
                            }
                            if ((still_now && state->mag_innov_reject_cnt >= (uint32_t)kMagYawDisturbTrip) ||
                                state->mag_innov_reject_cnt >= (uint32_t)kMagInnovQuarantineTrip) {
                                if (!state->mag_disturbed) {
                                    state->mag_disturb_events++;
                                }
                                state->mag_disturbed = 1;
                                state->mag_disturb_reason |= kMagDistReasonYaw;
                                state->mag_disturb_hold = (uint32_t)kMagDisturbHoldSamples;
                                state->mag_cal_quarantine = (uint32_t)kMagCalQuarantineSamples;
                                refresh_active_bias(state);
                            }
                        } else {
                            state->mag_innov_reject_cnt = 0u;
                            /* Stationary yaw lock: once initial gyro convergence completes, do not let
                             * subtle (<8 deg) slowly-approaching magnetic perturbations creep yaw at rest. */
                            if (still_now && state->fallback_gyro_steps >= (uint32_t)kStationaryYawLockSteps) {
                                hold_accel_only = 1;
                            }
                        }
                    }
                    if (hold_accel_only) {
                        int32_t accel_only_q30[4];
                        if (fallback_quat_accel_only(state, accel_only_q30) == 0) {
                            for (unsigned idx = 0; idx < 4u; ++idx) {
                                meas_q30[idx] = accel_only_q30[idx];
                            }
                        }
                    }
                    int64_t gain_den;
                    if (state->fallback_gyro_steps == 0u) {
                        gain_den = 8ll;
                    } else if (state->bias_handoff_steps > 0u) {
                        /* Fast initial convergence (~0.3 s at 50 Hz -> 99% lock) before smooth tail,
                         * so yaw aligns before post-handoff anomaly gates engage. */
                        gain_den = (state->bias_handoff_steps > (uint32_t)kBiasHandoffSteps - 16u) ? 4ll : 16ll;
                        state->bias_handoff_steps--;
                    } else if (state->compass_mode == SNS_COMPASS_MODE_FUSION) {
                        if (hold_accel_only) {
                            gain_den = 128ll;
                        } else if (used_fallback_mag &&
                                   state->earth_ref_count < (uint32_t)kEarthRefMinSamples &&
                                   gyro_is_still(state)) {
                            /* Initial ~0.5 s stationary earth_ref bootstrap: settle single-sample startup
                             * mag noise before anomaly gates & stationary yaw lock engage. */
                            gain_den = 8ll;
                        } else {
                            gain_den = 384ll;
                        }
                    } else {
                        gain_den = 32ll;
                    }
                    blend_quat_nlerp(state->fallback_q, meas_q30, 1ll, gain_den);
                    if (used_fallback_mag && !yaw_innov_rejected) {
                        state->fallback_used_mag = 1;
                    }
                }
            }
        }
        used_fallback_mag = state->fallback_used_mag;
        for (unsigned idx = 0; idx < 4u; ++idx) {
            quat_q30[idx] = state->fallback_q[idx];
        }
    }
    state->last_yaw_rejected = yaw_innov_rejected;

    int32_t up_q15[3];
    quat_to_axes_q15(quat_q30, NULL, NULL, up_q15);
    const int32_t grav_q15[3] = {-up_q15[0], -up_q15[1], -up_q15[2]};
    const int32_t heading_mdeg = heading_from_quat_q30(quat_q30);

    int64_t uncal_q16[3] = {0, 0, 0};
    int64_t bias_q16[3] = {0, 0, 0};
    int use_real_mag_cal = 0;

    if (state->mag_valid && !state->mag_disturbed && !yaw_innov_rejected &&
        state->active_bias_src != SNS_COMPASS_BIAS_NONE &&
        bias_is_earth_valid(state->mag_raw, state->active_bias, up_q15)) {
        use_real_mag_cal = 1;
        for (unsigned idx = 0; idx < 3u; ++idx) {
            uncal_q16[idx] = state->mag_raw[idx];
            bias_q16[idx] = state->active_bias[idx];
        }
    }
    if (!use_real_mag_cal) {
        int32_t synth_q16[3];
        compute_synth_mag_q16(state, quat_q30, synth_q16);
        const int32_t *ref_bias = select_instant_ref_bias(state);
        for (unsigned idx = 0; idx < 3u; ++idx) {
            const int64_t b = ref_bias ? (int64_t)ref_bias[idx] : 0ll;
            bias_q16[idx] = b;
            uncal_q16[idx] = b + (int64_t)synth_q16[idx];
        }
    }

    int64_t user_accel_q16[3] = {0, 0, 0};
    if (state->accel_valid) {
        for (unsigned idx = 0; idx < 3u; ++idx) {
            user_accel_q16[idx] = (int64_t)state->accel_ios[idx] - ((int64_t)grav_q15[idx] * 2ll);
        }
    }

    int64_t acc_deg = 25;
    if (used_rotv) {
        acc_deg = (state->rotv_acc >= 2) ? 15 : (state->rotv_acc == 1) ? 20 : 25;
    } else if (state->active_bias_src == SNS_COMPASS_BIAS_MAGCAL) {
        acc_deg = (state->trusted_mag_cal_acc >= 2) ? 15 : 22;
    } else if (state->active_bias_src == SNS_COMPASS_BIAS_DEFAULT) {
        acc_deg = 18;
    } else if (used_fallback_mag) {
        acc_deg = 28;
    } else {
        acc_deg = 35;
    }

    for (unsigned idx = 0; idx < SNS_COMPASS_REPORT_BYTES; ++idx) {
        out[idx] = 0;
    }
    uint16_t flags = (uint16_t)(kCompassFlagInit | kCompassFlagGravity | kCompassFlagBias);
    if (state->gyro_valid) {
        flags |= (uint16_t)kCompassFlagGyroCal;
    }
    if (used_rotv || used_fallback_mag || state->fallback_seeded_from_rotv) {
        flags |= (uint16_t)(kCompassFlagYaw | kCompassFlagCompassCal | kCompassFlagNorth);
    }
    out[0] = (uint8_t)kCompassVersion;
    out[1] = (uint8_t)kCompassCal3D;
    write_le16(out + 0x02, flags);
    write_le64(out + 0x04, 0ull);
    write_le32(out + 0x0c, kFloatOneBits);

    for (unsigned idx = 0; idx < 3u; ++idx) {
        write_le32(out + 0x10 + idx * 4u, sns_compass_float_signed(uncal_q16[idx], kQ16One));
    }
    for (unsigned idx = 0; idx < 4u; ++idx) {
        write_le32(out + 0x1c + idx * 4u, sns_compass_float_signed(quat_q30[idx], kQ30One));
    }
    int32_t gyro_corr[3] = {0, 0, 0};
    if (state->gyro_valid) {
        gyro_corrected_q16(state, state->gyro, gyro_corr);
    }
    for (unsigned idx = 0; idx < 3u; ++idx) {
        const int64_t gyro_val = state->gyro_valid ? (int64_t)gyro_corr[idx] : 0ll;
        write_le32(out + 0x2c + idx * 4u, sns_compass_float_signed(gyro_val, kQ16One));
    }
    for (unsigned idx = 0; idx < 3u; ++idx) {
        write_le32(out + 0x38 + idx * 4u, sns_compass_float_signed(user_accel_q16[idx], kQ16One));
    }
    write_le32(out + 0x44, sns_compass_float_signed(heading_mdeg, 1000ull));
    write_le32(out + 0x48, sns_compass_float_signed(acc_deg, 1ull));
    for (unsigned idx = 0; idx < 3u; ++idx) {
        write_le32(out + 0x4c + idx * 4u, sns_compass_float_signed(bias_q16[idx], kQ16One));
    }

    state->last_heading_mdeg = heading_mdeg;
    state->emitted++;
    if (heading_mdeg_out) {
        *heading_mdeg_out = heading_mdeg;
    }
    return 0;
}
