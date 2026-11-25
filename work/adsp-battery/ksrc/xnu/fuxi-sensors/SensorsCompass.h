/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Pure-integer CMDeviceMotionReport::Compass v2 (88-byte) builder for iOS 16.7.16 locationd CLCompass7 ({0xff0c, 8}).
 *
 * Reference:
 *   - iOS 16.7.16 (20H392) locationd CLCompass7::onDeviceMotionServiceIohidEvent (0x1006bd9cc)
 *     -> CMDeviceMotionReport unpack (0x1002152fc / 0x1006be77c, version=2 => 88 bytes)
 *     -> CLCompass7::populateMotionType (0x1006bdc60), attitude-to-gravity (0x1007b41e4),
 *        and attitude-to-north row0 (0x1007b4274) proving Apple NWU (+X=North, +Y=West, +Z=Up).
 *   - Qualcomm ADSP SSC akm_rotv (9-axis ENU quaternion qx,qy,qz,qw via msg 1025, converted
 *     to Apple NWU via q_z(-90 deg) * q_enu), sns_mag_cal (hard-iron bias uT via msg 1022),
 *     and 8-sample magnetometer moving average matching Xiaomi sensors.ssc.so.
 *   - Built under XNU -mgeneral-regs-only: zero hardware float or SIMD operations.
 */
#ifndef SENSORS_COMPASS_H
#define SENSORS_COMPASS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define SNS_COMPASS_REPORT_BYTES 88u
#define SNS_COMPASS_PERSIST_BYTES 24u
#define SNS_COMPASS_MAG_AVG_WIN 8u
#define SNS_COMPASS_POSE_HIST_WIN 16u

typedef struct {
    int32_t q[4];               /* fallback_q NWU quaternion in Q30 */
    int32_t mag[3];             /* instantaneous raw mag uT in Q16 */
    uint32_t seq;               /* mag_sample_seq when captured */
} SnsCompassPoseSnap;

enum {
    SNS_COMPASS_MODE_FUSION = 0, /* 9-axis calibrated accel + gyro + (mag - active_bias) */
    SNS_COMPASS_MODE_ROTV = 1,   /* legacy ADSP akm_rotv primary with stuck quarantine */
};

enum {
    SNS_COMPASS_BIAS_NONE = 0,
    SNS_COMPASS_BIAS_DEFAULT = 1, /* configured PCB hard-iron bias */
    SNS_COMPASS_BIAS_MAGCAL = 2,  /* live ADSP sns_mag_cal bias */
};

typedef struct {
    uint32_t compass_mode;      /* SNS_COMPASS_MODE_FUSION (0) or SNS_COMPASS_MODE_ROTV (1) */
    int32_t default_mag_bias[3];/* configured PCB hard-iron bias uT in 16.16 fixed point */
    int has_default_mag_bias;

    int32_t rotv_q[4];          /* normalized NWU quaternion (qx, qy, qz, qw) in Q30 */
    int32_t rotv_raw_enu_milli[4]; /* last raw ENU quaternion (qx, qy, qz, qw) in milli-units */
    int32_t rotv_acc;           /* SSC accuracy 0..3, -1 unknown */
    int rotv_valid;
    int32_t rotv_prev_q[4];     /* previous normalized NWU quaternion for stuck detection */
    int32_t rotv_stuck_q[4];    /* frozen quaternion captured when quarantine tripped */
    int rotv_has_prev;
    int rotv_stuck;             /* 1 while akm_rotv is frozen and quarantined */
    uint32_t rotv_gyro_stuck_cnt;
    uint32_t rotv_grav_stuck_cnt;
    uint32_t rotv_stuck_events; /* total times stuck watchdog tripped */
    uint32_t rotv_resume_moved_cnt;
    uint32_t rotv_post_bounce_skip;

    int32_t mag_cal_bias[3];    /* latest raw ADSP sns_mag_cal uT in 16.16 fixed point */
    int32_t mag_cal_acc;
    int mag_cal_valid;
    int mag_cal_polluted;       /* 1 if latest mag_cal was rejected or held pending verification */
    uint32_t mag_cal_quarantine;/* clean mag samples required after disturbance before trusting new mag_cal */
    uint32_t mag_cal_rejected;  /* total rejected/held mag_cal updates */

    int32_t trusted_mag_cal_bias[3]; /* verified ADSP mag_cal bias preserved across transient acc=0 drops */
    int32_t trusted_mag_cal_acc;
    int has_trusted_mag_cal;

    int32_t mag_cal_cand_bias[3];    /* large-shift (remagnetization) acc=3 candidate awaiting physical proof */
    int32_t mag_cal_cand_acc;
    int mag_cal_cand_valid;
    int mag_cal_cand_from_disturb;
    uint32_t mag_cal_cand_good;
    uint32_t mag_cal_cand_turn_mdeg;
    uint32_t mag_cal_cand_norm_mut;
    int32_t mag_cal_cand_up_mut;
    int mag_cal_cand_has_up;
    int32_t mag_cal_cand_world_mut[3];
    int mag_cal_cand_has_world;

    SnsCompassPoseSnap pose_hist[SNS_COMPASS_POSE_HIST_WIN];
    uint32_t pose_hist_count;
    uint32_t pose_hist_pos;
    uint32_t mag_sample_seq;

    int32_t active_bias[3];     /* currently validated hard-iron bias uT in 16.16 fixed point */
    int active_bias_src;        /* SNS_COMPASS_BIAS_NONE / _DEFAULT / _MAGCAL */
    int prev_bias_src;          /* previous bias source applied to fusion */
    uint32_t bias_handoff_steps;/* faster blend countdown after bias source handoff */

    int32_t mag_raw[3];         /* 8-sample moving-average uT in 16.16 fixed point */
    int32_t mag_hist[SNS_COMPASS_MAG_AVG_WIN][3];
    int64_t mag_sum[3];
    uint32_t mag_hist_count;
    uint32_t mag_hist_pos;
    int32_t mag_acc;
    int mag_valid;
    int32_t mag_prev_inst[3];   /* previous instantaneous raw mag uT in Q16 for stationary jump detection */
    int has_mag_prev_inst;
    int mag_disturbed;          /* 1 while magnetic disturbance is active or holding */
    uint32_t mag_disturb_hold;  /* clean mag samples required before resuming mag fusion */
    uint32_t mag_disturb_events;/* total times magnetic disturbance hold tripped */
    uint32_t mag_disturb_reason;/* bitmask: 1=earth 2=norm 4=up 8=vec 16=jump 32=yaw */
    uint32_t last_vec_err_mut;  /* last 3D world-frame |B_world - B_world_ref| in milli-uT */
    uint32_t vec_anom_cnt;      /* consecutive moderate vec_err samples for noise debouncing */
    uint32_t earth_anom_cnt;    /* consecutive marginal earth-range samples for noise debouncing */
    int last_yaw_rejected;      /* 1 if last build_report rejected mag yaw */

    uint32_t earth_ref_norm_mut;/* learned local Earth field |B| in milli-uT */
    uint32_t earth_ref_horiz_mut;/* learned local Earth horizontal field H in milli-uT */
    int32_t earth_ref_up_mut;   /* learned local Earth field dot(B, up) in milli-uT */
    int earth_ref_has_up;       /* 1 once earth_ref_up_mut is initialized from steady accel */
    uint32_t earth_ref_count;   /* clean samples accumulated into earth_ref */
    uint32_t earth_reanchor_norm_mut;
    uint32_t earth_reanchor_horiz_mut;
    int32_t earth_reanchor_up_mut;
    uint32_t earth_reanchor_count;

    int32_t relock_world_mut[3];/* candidate constant 3D B_world (N,W,U) during rotation-verified relock */
    uint32_t relock_steady_cnt; /* consecutive steady clean samples matching candidate */
    uint32_t gyro_turn_acc_mdeg;/* accumulated gyro turn (mdeg) while relock candidate stays invariant */

    uint32_t mag_innov_reject_cnt;  /* consecutive samples rejected by gyro-vs-mag yaw innovation gate */
    uint32_t mag_innov_confirm_cnt; /* consecutive clean steady samples at a shifted yaw before relock */
    int32_t mag_innov_cand_hdg_mdeg;

    int32_t accel_ios[3];       /* g in 16.16 fixed point (face-up z = -1 g) */
    int accel_valid;

    int32_t gyro[3];            /* raw rad/s in 16.16 fixed point */
    int32_t gyro_bias[3];       /* learned stationary ZUPT gyro zero-rate bias rad/s in 16.16 fixed point */
    int32_t zupt_gyro_ref[3];   /* reference raw gyro during stationary stability check */
    int32_t zupt_accel_ref[3];  /* reference accel during stationary stability check */
    int zupt_has_ref;
    uint32_t zupt_still_cnt;    /* consecutive stationary gyro+accel samples */
    uint32_t zupt_learn_cnt;    /* total stationary bias learning updates applied */
    int gyro_valid;

    int32_t fallback_q[4];      /* gyro+accel+mag complementary NWU quaternion in Q30 */
    int fallback_valid;
    int fallback_used_mag;
    int fallback_seeded_from_rotv;
    uint32_t fallback_gyro_steps;
    uint64_t last_gyro_ts;      /* SSC 19.2 MHz QTimer ticks */

    int32_t last_heading_mdeg;  /* 0..359999 millidegrees */
    int32_t last_mag_heading_mdeg;  /* instantaneous tilt-compensated mag heading, or -1 */
    int32_t last_rotv_heading_mdeg; /* last akm_rotv heading, or -1 */
    uint32_t emitted;
} SnsCompass;

void sns_compass_init(SnsCompass *state);
void sns_compass_set_mode(SnsCompass *state, uint32_t mode);
void sns_compass_set_default_mag_bias(SnsCompass *state, const int32_t bias_q16[3]);
int sns_compass_apply_mag_bias(const SnsCompass *state, const int32_t raw_q16[3], int32_t out_q16[3]);
int sns_compass_apply_gyro_bias(const SnsCompass *state, const int32_t raw_q16[3], int32_t out_q16[3]);
void sns_compass_reset_session(SnsCompass *state);
/* Parses "X,Y,Z" signed milli-uT triplet into Q16 uT. Returns 1 if nonzero, 0 if all zero, -1 on error. */
int sns_compass_parse_mag_bias(const char *spec, int32_t out_q16[3]);
/* Binary persistence codec for verified hard-iron bias (/private/var/db/fuxi-mag-bias.bin). */
int sns_compass_encode_persisted_bias(const int32_t bias_q16[3], uint8_t out[SNS_COMPASS_PERSIST_BYTES]);
int sns_compass_decode_persisted_bias(const uint8_t in[SNS_COMPASS_PERSIST_BYTES], int32_t bias_q16[3]);
int sns_compass_load_persisted_mag_bias(SnsCompass *state, const int32_t bias_q16[3]);
int sns_compass_get_persistable_bias(const SnsCompass *state, int32_t bias_q16[3]);
uint32_t sns_compass_bias_drift_mut(const int32_t lhs_q16[3], const int32_t rhs_q16[3]);
void sns_compass_set_accel(SnsCompass *state, const int32_t accel_q16[3]);
void sns_compass_set_gyro(SnsCompass *state, const int32_t gyro_q16[3]);
void sns_compass_set_gyro_ts(SnsCompass *state, const int32_t gyro_q16[3], uint64_t timestamp_ticks);
void sns_compass_set_mag(SnsCompass *state, const int32_t mag_q16[3], int32_t accuracy);
void sns_compass_set_mag_cal(SnsCompass *state, const int32_t bias_q16[3], int32_t accuracy);
int sns_compass_set_rotv(SnsCompass *state, const uint32_t quat_bits[4], int32_t accuracy);
void sns_compass_rotv_bounced(SnsCompass *state);

/* Builds an 88-byte CMDeviceMotionReport::Compass v2 payload. Returns 0 on success, -1 if no attitude source yet. */
int sns_compass_build_report(SnsCompass *state, uint8_t out[SNS_COMPASS_REPORT_BYTES], int32_t *heading_mdeg_out);

/* Pure-integer math helpers exposed for host verification. */
int32_t sns_compass_atan2_mdeg(int32_t y_val, int32_t x_val);
uint32_t sns_compass_float_signed(int64_t numerator, uint64_t denominator);

#ifdef __cplusplus
}
#endif
#endif
