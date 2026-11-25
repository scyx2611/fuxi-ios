/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Fuxi (SM8550) ADSP SSC sensors manager glue.
 * Adapted from PaganiSensors/PaganiSensors.cpp (BSD-2-Clause-Patent, Copyright (c) 2026 UniXNU contributors)
 * to Fuxi's XNU BSD + ShengQrtr + native IOHIDEventService (FuxiSensorsHid.cpp) stack.
 *
 * Opt-in boot-arg `fuxisensors` or ADT `/chosen u32 "fuxi,sensors"` (default 0 = completely off):
 *   bits 0..1 : mode (0=off, 1=HID services only, 2=SUID lookup + attrs, 3=full on-demand streams + DT nodes)
 *   bit 2 (4) : start FastRPC reverse file service (fastrpcglink-apps-dsp)
 *   bit 3 (8) : publish Orientation service (0x0020/0x8a) + AppleSPULib 'ashd' user client
 *   bit 4 (16): also request SUID list of all sensor data types (diagnostic)
 *   bit 5 (32): force 50 Hz motion streaming for bring-up (overridable via fuxisensorshz / fuxi,sensors-hz)
 *   bit 6 (64): publish DeviceMotion compass service (0xff0c/8) backed by ADSP rotv + mag_cal
 */
#include <pexpert/arm64/board_config.h>
#if defined(FUXI)
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/uio.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <kern/kalloc.h>
#include <pexpert/pexpert.h>
#include <pexpert/device_tree.h>
#include "ShengQrtr.h"
#include "SensorsCore.h"
#include "SensorsHid.h"
#include "SensorsRpc.h"
#include "SensorsText.h"
#include "fuxi_sensors_hid.h"

extern kern_return_t kernel_thread_start(thread_continue_t, void *, thread_t *);
extern void thread_deallocate(thread_t);
extern kern_return_t clear_wait(thread_t, wait_result_t);
extern wait_result_t thread_block(thread_continue_t);
extern int fuxi_sensors_rpc_status(SsrStatus *out);

void fuxi_sensors_start(void);

enum {
    kRingSlots = 64u,
    kSlotBytes = 1088u,
    kEvData = 1u << 0,
    kEvServer = 1u << 1,
    kEvDemand = 1u << 2,
};

struct fs_slot {
    uint32_t conn, node, port, length;
    uint8_t data[kSlotBytes];
};

static volatile uint32_t gStarted, gPending, gDemandDirty;
static volatile uint32_t gDemand[SNS_HID_KINDS];
static struct fs_slot *gRing;
static uint32_t gHead, gTail;
static volatile uint32_t gDropped, gTooLong;
static ShengQrtrPort *gPort[SNS_CONNS];
static SnsCore *gCore;
static SnsConfig gConfig;
static uint32_t gPublished;
static uint32_t gMode;
static int gRpcEnabled;
static int gMagPersistEnabled;
static int gMagBiasLoadDone;
static int gHasSavedMagBias;
static int32_t gSavedMagBias[3];
static uint64_t gStartMs;
static uint64_t gLastMagBiasSaveMs;
static uint32_t gTestRotDelaySec;
static uint32_t gTestRotStage;
static int gTestRotActive;

static const char kMagBiasDir[] = "/private/var/db";
static const char kMagBiasPath[] = "/private/var/db/fuxi-mag-bias.bin";

static uint64_t
fs_now_ms(void *context)
{
    (void)context;
    uint64_t ns = 0;
    absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
    return ns / 1000000ull;
}

static void
fs_trace(void *context, const char *line)
{
    (void)context;
    if (line) printf("%s\n", line);
}

static void
fs_wake(uint32_t bits)
{
    __atomic_fetch_or(&gPending, bits, __ATOMIC_RELEASE);
    thread_wakeup((event_t)&gPending);
}

static uint32_t
fs_wait(uint32_t ms)
{
    uint32_t bits = __atomic_exchange_n(&gPending, 0u, __ATOMIC_ACQ_REL);
    if (bits) return bits;
    assert_wait_timeout((event_t)&gPending, THREAD_UNINT, ms ? ms : 1u, 1000000u);
    bits = __atomic_exchange_n(&gPending, 0u, __ATOMIC_ACQ_REL);
    if (bits) {
        clear_wait(current_thread(), THREAD_AWAKENED);
        return bits;
    }
    thread_block(THREAD_CONTINUE_NULL);
    return __atomic_exchange_n(&gPending, 0u, __ATOMIC_ACQ_REL);
}

static uint32_t
read_u32_cfg(const char *boot_arg, const char *dt_prop, uint32_t fallback)
{
    uint32_t val = fallback;
    if (boot_arg && PE_parse_boot_argn(boot_arg, &val, sizeof(val))) return val;
    if (dt_prop) {
        DTEntry chosen;
        const void *prop = NULL;
        unsigned bytes = 0;
        if (SecureDTLookupEntry(NULL, "/chosen", &chosen) == kSuccess &&
            SecureDTGetProperty(chosen, dt_prop, &prop, &bytes) == kSuccess &&
            bytes == sizeof(uint32_t)) {
            memcpy(&val, prop, sizeof(val));
        }
    }
    return val;
}

static int
read_str_cfg(const char *boot_arg, const char *dt_prop, char *out, unsigned cap)
{
    if (!out || !cap) return 0;
    out[0] = 0;
    if (boot_arg && PE_parse_boot_argn(boot_arg, out, (int)cap)) return 1;
    if (dt_prop) {
        DTEntry chosen;
        const void *prop = NULL;
        unsigned bytes = 0;
        if (SecureDTLookupEntry(NULL, "/chosen", &chosen) == kSuccess &&
            SecureDTGetProperty(chosen, dt_prop, &prop, &bytes) == kSuccess && bytes > 0) {
            unsigned n = bytes < cap - 1 ? bytes : cap - 1;
            memcpy(out, prop, n);
            out[n] = 0;
            return 1;
        }
    }
    return 0;
}

static void
on_qrtr_receive(void *context, uint32_t node, uint32_t from_port, const uint8_t *data, uint32_t length)
{
    const uint32_t conn = (uint32_t)(uintptr_t)context;
    if (!gRing) return;
    if (length > kSlotBytes) {
        __atomic_fetch_add(&gTooLong, 1u, __ATOMIC_RELAXED);
        return;
    }
    const uint32_t head = __atomic_load_n(&gHead, __ATOMIC_RELAXED);
    const uint32_t tail = __atomic_load_n(&gTail, __ATOMIC_ACQUIRE);
    if (head - tail >= kRingSlots) {
        __atomic_fetch_add(&gDropped, 1u, __ATOMIC_RELAXED);
        return;
    }
    struct fs_slot *slot = &gRing[head % kRingSlots];
    slot->conn = conn;
    slot->node = node;
    slot->port = from_port;
    slot->length = length;
    memcpy(slot->data, data, length);
    __atomic_store_n(&gHead, head + 1u, __ATOMIC_RELEASE);
    fs_wake(kEvData);
}

static void
on_qrtr_server_change(void *context)
{
    (void)context;
    fs_wake(kEvServer);
}

static void
on_hid_demand(unsigned kind, uint32_t demand)
{
    if (kind >= SNS_HID_KINDS) return;
    __atomic_store_n(&gDemand[kind], demand, __ATOMIC_RELEASE);
    __atomic_fetch_or(&gDemandDirty, 1u << kind, __ATOMIC_RELEASE);
    fs_wake(kEvDemand);
}

static int
op_send(void *context, unsigned conn, uint32_t node, uint32_t port, const uint8_t *data, uint32_t length)
{
    (void)context;
    if (conn >= SNS_CONNS || !gPort[conn]) return -1;
    return sheng_qrtr_send(gPort[conn], node, port, data, length);
}

static void
op_sample(void *context, const SnsSample *s)
{
    (void)context;
    if (!s) return;
    fuxi_sensors_hid_dispatch_sample(s->kind, s->timestamp, s->value[0], s->value[1], s->value[2]);
}

static void
op_found(void *context, unsigned kind)
{
    (void)context;
    if (kind == PaganiSensorPressure && !(gPublished & (1u << kind))) {
        if (fuxi_sensors_hid_publish(kind) == 0) {
            gPublished |= 1u << kind;
            printf("[sensors] published dynamic service kind=%u (%s)\n", kind, sns_kind_name(kind));
        }
    }
}

static void
op_orientation(void *context, int state)
{
    (void)context;
    if (gTestRotActive) return;
    fuxi_sensors_hid_orientation(state);
}

/* Kernel VFS hard-iron bias persistence (follows fuxi_sensors_rpc.c + bsd/kern/kern_shutdown.c). */
static void
mag_bias_try_load(void)
{
    if (!gCore || !gMagPersistEnabled || gMagBiasLoadDone) return;
    if (gCore->compass.has_trusted_mag_cal) {
        gMagBiasLoadDone = 1;
        return;
    }
    vfs_context_t ctx = vfs_context_kernel();
    vnode_t dir = NULL;
    int err = vnode_open(kMagBiasDir, FREAD | O_NOFOLLOW | O_NONBLOCK, 0, 0, &dir, ctx);
    if (err) {
        if (err != ENOENT) gMagBiasLoadDone = 1;
        return;
    }
    vnode_close(dir, FREAD, ctx);

    vnode_t vp = NULL;
    err = vnode_open(kMagBiasPath, FREAD | O_NOFOLLOW | O_NONBLOCK, 0, 0, &vp, ctx);
    if (err) {
        gMagBiasLoadDone = 1;
        if (err == ENOENT) {
            printf("[sensors] mag_bias persist absent (using default)\n");
        } else {
            printf("[sensors] mag_bias persist open err=%d\n", err);
        }
        return;
    }
    uint8_t buf[SNS_COMPASS_PERSIST_BYTES];
    int resid = 0;
    err = vn_rdwr(UIO_READ, vp, (caddr_t)buf, (int)sizeof(buf), (off_t)0,
        UIO_SYSSPACE, IO_NODELOCKED, vfs_context_ucred(ctx), &resid, vfs_context_proc(ctx));
    vnode_close(vp, FREAD, ctx);
    gMagBiasLoadDone = 1;
    int32_t bias[3];
    if (!err && resid == 0 && sns_compass_decode_persisted_bias(buf, bias) == 0) {
        const int applied = sns_compass_load_persisted_mag_bias(&gCore->compass, bias);
        for (unsigned i = 0; i < 3u; ++i) gSavedMagBias[i] = bias[i];
        gHasSavedMagBias = 1;
        printf("[sensors] mag_bias loaded persisted=%d,%d,%d applied=%d\n",
            bias[0] >> 16, bias[1] >> 16, bias[2] >> 16, applied);
    } else {
        printf("[sensors] mag_bias persist invalid err=%d resid=%d (ignored)\n", err, resid);
    }
}

static void
mag_bias_try_save(uint64_t now_ms)
{
    if (!gCore || !gMagPersistEnabled || !gMagBiasLoadDone) return;
    int32_t bias[3];
    if (!sns_compass_get_persistable_bias(&gCore->compass, bias)) return;
    if (gHasSavedMagBias && sns_compass_bias_drift_mut(bias, gSavedMagBias) < 2000u) return;
    if (gLastMagBiasSaveMs != 0 && now_ms > gLastMagBiasSaveMs &&
        (now_ms - gLastMagBiasSaveMs) < 10000ull) {
        return;
    }
    uint8_t buf[SNS_COMPASS_PERSIST_BYTES];
    if (sns_compass_encode_persisted_bias(bias, buf) != 0) return;

    vfs_context_t ctx = vfs_context_kernel();
    vnode_t vp = NULL;
    int err = vnode_open(kMagBiasPath, (O_CREAT | O_TRUNC | FWRITE | O_NOFOLLOW), 0644, 0, &vp, ctx);
    if (err) {
        gLastMagBiasSaveMs = now_ms;
        printf("[sensors] mag_bias save open err=%d\n", err);
        return;
    }
    int resid = 0;
    err = vn_rdwr(UIO_WRITE, vp, (caddr_t)buf, (int)sizeof(buf), (off_t)0,
        UIO_SYSSPACE, IO_UNIT | IO_NOAUTH, vfs_context_ucred(ctx), &resid, vfs_context_proc(ctx));
    if (!err && resid == 0) {
        (void)VNOP_FSYNC(vp, MNT_WAIT, ctx);
    }
    int cerr = vnode_close(vp, FWRITE, ctx);
    gLastMagBiasSaveMs = now_ms;
    if (!err && resid == 0 && !cerr) {
        for (unsigned i = 0; i < 3u; ++i) gSavedMagBias[i] = bias[i];
        gHasSavedMagBias = 1;
        printf("[sensors] mag_bias saved=%d,%d,%d\n",
            bias[0] >> 16, bias[1] >> 16, bias[2] >> 16);
    } else {
        printf("[sensors] mag_bias save err=%d resid=%d cerr=%d\n", err, resid, cerr);
    }
}

static void
test_rot_tick(uint64_t now_ms)
{
    if (!gTestRotDelaySec || !gConfig.orient || gTestRotStage >= 4u) return;
    if (now_ms < gStartMs) return;
    const uint64_t elapsed_sec = (now_ms - gStartMs) / 1000ull;
    const uint64_t base = (uint64_t)gTestRotDelaySec;
    if (gTestRotStage == 0u && elapsed_sec >= base) {
        gTestRotActive = 1;
        gTestRotStage = 1u;
        printf("[sensors] test-rot inject state=3 (landscape-left) at %llus\n", (unsigned long long)elapsed_sec);
        fuxi_sensors_hid_orientation(3);
    } else if (gTestRotStage == 1u && elapsed_sec >= base + 12ull) {
        gTestRotStage = 2u;
        printf("[sensors] test-rot inject state=4 (landscape-right) at %llus\n", (unsigned long long)elapsed_sec);
        fuxi_sensors_hid_orientation(4);
    } else if (gTestRotStage == 2u && elapsed_sec >= base + 24ull) {
        gTestRotStage = 3u;
        printf("[sensors] test-rot inject state=1 (portrait) at %llus\n", (unsigned long long)elapsed_sec);
        fuxi_sensors_hid_orientation(1);
    } else if (gTestRotStage == 3u && elapsed_sec >= base + 30ull) {
        gTestRotActive = 0;
        gTestRotStage = 4u;
        printf("[sensors] test-rot done at %llus\n", (unsigned long long)elapsed_sec);
    }
}

static int gLastMagDist = 0;
static uint32_t gMagDistLogCount = 0;

static void
op_compass(void *context, uint64_t timestamp, const uint8_t report[SNS_COMPASS_REPORT_BYTES], int32_t heading_mdeg)
{
    (void)context;
    fuxi_sensors_hid_dispatch_compass(timestamp, report);
    if (gCore) {
        const int cur_dist = gCore->compass.mag_disturbed ? 1 : 0;
        if (cur_dist != gLastMagDist) {
            if (gMagDistLogCount < 48u) {
                const int32_t mhdg = (gCore->compass.last_mag_heading_mdeg >= 0)
                    ? (gCore->compass.last_mag_heading_mdeg / 1000) : -1;
                printf("[sensors] mag_dist %d->%d r=%#x hdg=%d.%03d mhdg=%d verr=%u ref=%u,%u,%d raw=%d,%d,%d\n",
                    gLastMagDist, cur_dist, gCore->compass.mag_disturb_reason,
                    heading_mdeg / 1000,
                    (heading_mdeg >= 0 ? heading_mdeg : -heading_mdeg) % 1000,
                    mhdg, gCore->compass.last_vec_err_mut,
                    gCore->compass.earth_ref_norm_mut, gCore->compass.earth_ref_horiz_mut,
                    gCore->compass.earth_ref_up_mut,
                    gCore->compass.mag_raw[0] >> 16, gCore->compass.mag_raw[1] >> 16,
                    gCore->compass.mag_raw[2] >> 16);
                gMagDistLogCount++;
            }
            gLastMagDist = cur_dist;
        }
    }
}

static void
log_summary(void)
{
    uint32_t dispatched = 0, unbound = 0, restamped = 0;
    fuxi_sensors_hid_counts(&dispatched, &unbound, &restamped);
    if (gCore) {
        PaganiSensorsStatus st;
        sns_core_status(gCore, &st);
        uint32_t total_ev = 0;
        for (unsigned k = 0; k < PaganiSensorKinds; ++k) total_ev += st.events[k];
        const int using_rotv = (gCore->compass.compass_mode == SNS_COMPASS_MODE_ROTV && gCore->compass.rotv_valid);
        const int32_t *qsrc = using_rotv ? gCore->compass.rotv_q : gCore->compass.fallback_q;
        int32_t nq[4] = {0, 0, 0, 0};
        if (gCore->compass.rotv_valid || gCore->compass.fallback_valid) {
            for (unsigned i = 0; i < 4u; ++i) {
                nq[i] = (int32_t)(((int64_t)qsrc[i] * 1000ll) >> 30);
            }
        }
        const char *bsrc = (gCore->compass.active_bias_src == SNS_COMPASS_BIAS_MAGCAL) ? "mcal"
            : (gCore->compass.active_bias_src == SNS_COMPASS_BIAS_DEFAULT) ? "pcb" : "none";
        const int32_t mhdg = (gCore->compass.last_mag_heading_mdeg >= 0) ? (gCore->compass.last_mag_heading_mdeg / 1000) : -1;
        const int32_t rhdg = (gCore->compass.last_rotv_heading_mdeg >= 0) ? (gCore->compass.last_rotv_heading_mdeg / 1000) : -1;
        printf("[sensors] status st=%d err=%d found=%#x desc=%#x stream=%#x rep=%u ev=%u(%u,%u,%u,%u,%u,%u,%u) orient=%d hw=%d/%d hid=%u/%u/%u drop=%u/%u\n",
            st.stage, st.error, st.found, st.described, st.streaming, st.reports, total_ev,
            st.events[0], st.events[1], st.events[2], st.events[3], st.events[4],
            st.events[PaganiSensorRotv], st.events[PaganiSensorMagCal],
            gCore->orient.state,
            sfx_milli(gCore->sensor[PaganiSensorAccel].hw_rate) / 1000,
            sfx_milli(gCore->sensor[PaganiSensorGyro].hw_rate) / 1000,
            dispatched, unbound, restamped, gDropped, gTooLong);
        printf("[sensors] compass=%u(%d.%03d) src=%s bsrc=%s mb=%d,%d,%d mhdg=%d rhdg=%d racc=%d mcal=%d dist=%u/%u(r=%#x) mrej=%u yrej=%u verr=%u ref=%u,%u,%d\n",
            gCore->compass_events,
            gCore->compass.last_heading_mdeg / 1000,
            (gCore->compass.last_heading_mdeg >= 0 ? gCore->compass.last_heading_mdeg : -gCore->compass.last_heading_mdeg) % 1000,
            using_rotv ? "rotv" : "fus", bsrc,
            gCore->compass.active_bias[0] >> 16, gCore->compass.active_bias[1] >> 16, gCore->compass.active_bias[2] >> 16,
            mhdg, rhdg,
            gCore->compass.rotv_acc, gCore->compass.mag_cal_acc,
            (unsigned)gCore->compass.mag_disturbed, gCore->compass.mag_disturb_events,
            gCore->compass.mag_disturb_reason, gCore->compass.mag_cal_rejected,
            gCore->compass.mag_innov_reject_cnt, gCore->compass.last_vec_err_mut,
            gCore->compass.earth_ref_norm_mut, gCore->compass.earth_ref_horiz_mut,
            gCore->compass.earth_ref_up_mut);
        printf("[sensors] pose rq=%d,%d,%d,%d nq=%d,%d,%d,%d mag=%d,%d,%d zb=%d,%d,%d tcal=%d stuck=%u/%u/%u\n",
            gCore->compass.rotv_raw_enu_milli[0], gCore->compass.rotv_raw_enu_milli[1],
            gCore->compass.rotv_raw_enu_milli[2], gCore->compass.rotv_raw_enu_milli[3],
            nq[0], nq[1], nq[2], nq[3],
            gCore->compass.mag_raw[0] >> 16, gCore->compass.mag_raw[1] >> 16, gCore->compass.mag_raw[2] >> 16,
            (int)(((int64_t)gCore->compass.gyro_bias[0] * 1000ll) >> 16),
            (int)(((int64_t)gCore->compass.gyro_bias[1] * 1000ll) >> 16),
            (int)(((int64_t)gCore->compass.gyro_bias[2] * 1000ll) >> 16),
            gCore->compass.has_trusted_mag_cal ? gCore->compass.trusted_mag_cal_acc : -1,
            (unsigned)gCore->compass.rotv_stuck, gCore->compass.rotv_stuck_events, gCore->rotv_bounces);
    }
    if (gRpcEnabled) {
        SsrStatus rst;
        if (fuxi_sensors_rpc_status(&rst) == 0) {
            printf("[sensors] rpc status st=%d err=%d att=%u calls=%u open=%u miss=%u rd=%u wr=%u ref=%u bytes=%llu heap=%u note=%s\n",
                rst.stage, rst.error, rst.attaches, rst.calls, rst.opens, rst.missing,
                rst.reads, rst.writes, rst.refused, rst.bytes, rst.heap_used, rst.note);
        }
    }
}

static void
sensors_thread(void *arg, wait_result_t wr)
{
    (void)arg;
    (void)wr;
    uint64_t next_status = fs_now_ms(NULL) + 15000ull;
    uint64_t next_persist = fs_now_ms(NULL) + 2000ull;
    unsigned status_count = 0;
    uint32_t last_gen = 0xffffffffu;
    int last_up = -1;
    int smmu_checked = 0;

    for (;;) {
        if (gCore) {
            /* Drain received QMI packets. */
            for (;;) {
                const uint32_t tail = __atomic_load_n(&gTail, __ATOMIC_RELAXED);
                const uint32_t head = __atomic_load_n(&gHead, __ATOMIC_ACQUIRE);
                if (tail == head) break;
                const struct fs_slot *slot = &gRing[tail % kRingSlots];
                sns_core_receive(gCore, slot->conn, slot->node, slot->port, slot->data, slot->length);
                __atomic_store_n(&gTail, tail + 1u, __ATOMIC_RELEASE);
            }

            /* Poll QRTR service 0x190 (sns_client). */
            uint32_t node = 0, port = 0, inst = 0, gen = 0;
            const int up = sheng_qrtr_find_service(SSC_QMI_SERVICE, &node, &port, &inst, &gen);
            if (up != last_up || gen != last_gen) {
                last_up = up;
                last_gen = gen;
                sns_core_server(gCore, up, node, port, inst, gen);
            }

            /* Apply iOS HID demand changes. */
            uint32_t dirty = __atomic_exchange_n(&gDemandDirty, 0u, __ATOMIC_ACQ_REL);
            for (unsigned k = 0; dirty && k < SNS_HID_KINDS; ++k) {
                if (!(dirty & (1u << k))) continue;
                const uint32_t val = __atomic_load_n(&gDemand[k], __ATOMIC_ACQUIRE);
                if (k == SNS_HID_ORIENT) {
                    fuxi_sensors_hid_orientation_replay();
                } else if (k == SNS_HID_DEVMOTION) {
                    sns_core_compass_demand(gCore, val);
                } else if (k <= PaganiSensorPressure) {
                    sns_core_demand(gCore, k, val);
                }
            }

            sns_core_tick(gCore);
        }

        const uint64_t now = fs_now_ms(NULL);
        if (gCore && gMagPersistEnabled && now >= next_persist) {
            if (!gMagBiasLoadDone) {
                mag_bias_try_load();
                if (!gMagBiasLoadDone && now >= gStartMs + 60000ull) {
                    gMagBiasLoadDone = 1;
                    printf("[sensors] mag_bias persist wait timeout (using default)\n");
                }
                next_persist = now + 1000ull;
            } else {
                mag_bias_try_save(now);
                next_persist = now + 2000ull;
            }
        }
        test_rot_tick(now);
        if (now >= next_status) {
            log_summary();
            if (gRpcEnabled && !smmu_checked && status_count >= 1) {
                fuxi_sensors_rpc_log_smmu("30s");
                smmu_checked = 1;
            }
            status_count++;
            next_status = now + (status_count < 6 ? 15000ull : 60000ull);
        }

        const uint32_t sleep_ms = gCore ? sns_core_idle_ms(gCore) : 5000u;
        const uint32_t bounded = sleep_ms > 2000u ? 2000u : (sleep_ms ? sleep_ms : 1u);
        (void)fs_wait(bounded);
    }
}

static void
copy_sensor_type(char *dst, const char *boot_arg, const char *dt_prop, const char *fallback)
{
    char tmp[SSC_TYPE_MAX];
    const char *src = (read_str_cfg(boot_arg, dt_prop, tmp, sizeof(tmp)) && tmp[0]) ? tmp : fallback;
    unsigned i = 0;
    for (; src[i] && i + 1u < SSC_TYPE_MAX; ++i) dst[i] = src[i];
    dst[i] = 0;
}

void
fuxi_sensors_start(void)
{
    if (gStarted) return;
    const uint32_t raw = read_u32_cfg("fuxisensors", "fuxi,sensors", 0u);
    const uint32_t mode = raw & 3u;
    const uint32_t rpc = (raw >> 2) & 1u;
    const uint32_t orient = (raw >> 3) & 1u;
    const uint32_t list = (raw >> 4) & 1u;
    const uint32_t force_motion = (raw >> 5) & 1u;
    const uint32_t compass = (raw >> 6) & 1u;
    const uint32_t als = (raw >> 7) & 1u;
    if (!mode && !rpc) return;
    gStarted = 1;
    gStartMs = fs_now_ms(NULL);
    gMode = mode;
    gRpcEnabled = (int)rpc;
    gMagPersistEnabled = (mode == 3u && compass &&
        read_u32_cfg("fuxisensorsmagpersist", "fuxi,sensors-mag-persist", 1u) != 0u) ? 1 : 0;
    gTestRotDelaySec = read_u32_cfg("fuxisensorstestrot", "fuxi,sensors-test-rot", 0u);

    const uint32_t log_level = read_u32_cfg("fuxisensorslog", "fuxi,sensors-log", (list || force_motion) ? 2u : 1u);
    const uint32_t motion_hz = read_u32_cfg("fuxisensorshz", "fuxi,sensors-hz", force_motion ? 50u : 0u);
    const uint32_t want_nodes = read_u32_cfg("fuxisensorsnodes", "fuxi,sensors-nodes", 1u);

    memset(&gConfig, 0, sizeof(gConfig));
    gConfig.mode = mode;
    gConfig.orient = (mode == 3u && orient) ? 1u : 0u;
    gConfig.compass = (mode == 3u && compass) ? 1u : 0u;
    const uint32_t cmode = read_u32_cfg("fuxisensorscompassmode", "fuxi,sensors-compass-mode", SNS_COMPASS_MODE_FUSION);
    gConfig.compass_mode = (cmode == SNS_COMPASS_MODE_ROTV) ? SNS_COMPASS_MODE_ROTV : SNS_COMPASS_MODE_FUSION;
    gConfig.list = list;
    gConfig.log = log_level;
    gConfig.motion_hz = motion_hz;
    gConfig.als_permille = read_u32_cfg("fuxisensorsalsscale", "fuxi,sensors-als-scale", 1000u);
    if (!gConfig.als_permille || gConfig.als_permille > 100000u) gConfig.als_permille = 1000u;
    copy_sensor_type(gConfig.type[PaganiSensorAccel], "fuxisensorsaccel", "fuxi,sensors-accel", "accel");
    copy_sensor_type(gConfig.type[PaganiSensorGyro], "fuxisensorsgyro", "fuxi,sensors-gyro", "gyro");
    copy_sensor_type(gConfig.type[PaganiSensorMag], "fuxisensorsmag", "fuxi,sensors-mag", "mag");
    if (als) {
        copy_sensor_type(gConfig.type[PaganiSensorLight], "fuxisensorsals", "fuxi,sensors-als", "ambient_light");
    }
    copy_sensor_type(gConfig.type[PaganiSensorProximity], "fuxisensorsprox", "fuxi,sensors-prox", "proximity");
    copy_sensor_type(gConfig.type[PaganiSensorPressure], "fuxisensorspressure", "fuxi,sensors-pressure", "pressure");
    if (compass) {
        copy_sensor_type(gConfig.type[PaganiSensorRotv], "fuxisensorsrotv", "fuxi,sensors-rotv", "akm_rotv");
        copy_sensor_type(gConfig.type[PaganiSensorMagCal], "fuxisensorsmagcal", "fuxi,sensors-mag-cal", "mag_cal");
    }
    /* ADSP applies Xiaomi 13 bmi26x_0_platform.orient / ak0991x_0_platform.orient from pack.bin
     * so SSC reports standard Android ENU coordinates; keep identity defaults unless overridden. */
    sfx_axes_identity(&gConfig.axes[0]);
    sfx_axes_identity(&gConfig.axes[1]);
    sfx_axes_identity(&gConfig.axes[2]);
    char axes_buf[64];
    if (read_str_cfg("fuxisensorsaxes", "fuxi,sensors-axes", axes_buf, sizeof(axes_buf)) && axes_buf[0]) {
        if (sfx_axes_parse(axes_buf, &gConfig.axes[0], &gConfig.axes[1], &gConfig.axes[2]) != 0) {
            printf("[sensors] bad axes override: %s\n", axes_buf);
        }
    }
    char bias_buf[64];
    if (read_str_cfg("fuxisensorsmagbias", "fuxi,sensors-mag-bias", bias_buf, sizeof(bias_buf)) && bias_buf[0]) {
        const int brc = sns_compass_parse_mag_bias(bias_buf, gConfig.mag_bias_q16);
        if (brc > 0) {
            gConfig.has_mag_bias = 1u;
        } else if (brc == 0) {
            gConfig.has_mag_bias = 0u;
        } else {
            printf("[sensors] bad mag-bias override: %s\n", bias_buf);
        }
    }

    printf("[sensors] start raw=%#x mode=%u rpc=%u orient=%u compass=%u cmode=%u mbias=%d,%d,%d(%u) mpersist=%d trot=%u als=%u list=%u hz=%u log=%u\n",
        raw, mode, rpc, orient, compass, gConfig.compass_mode,
        gConfig.mag_bias_q16[0] >> 16, gConfig.mag_bias_q16[1] >> 16, gConfig.mag_bias_q16[2] >> 16,
        gConfig.has_mag_bias, gMagPersistEnabled, gTestRotDelaySec, als, list, motion_hz, log_level);

    if (mode >= 1u) {
        int rc = fuxi_sensors_hid_setup(on_hid_demand);
        if (rc != 0) {
            printf("[sensors] hid setup failed rc=%d\n", rc);
            return;
        }
        for (unsigned kind = 0; kind < PaganiSensorPressure; ++kind) {
            if (kind == PaganiSensorLight && !als) continue;
            if (fuxi_sensors_hid_publish(kind) == 0) {
                gPublished |= 1u << kind;
            } else {
                printf("[sensors] failed to publish kind=%u (%s)\n", kind, sns_kind_name(kind));
            }
        }
        if (orient && (gPublished & (1u << PaganiSensorAccel))) {
            if (fuxi_sensors_hid_publish(SNS_HID_ORIENT) == 0) {
                gPublished |= 1u << SNS_HID_ORIENT;
            } else {
                printf("[sensors] failed to publish orientation service\n");
            }
        }
        if (compass && (gPublished & (1u << PaganiSensorAccel))) {
            if (fuxi_sensors_hid_publish(SNS_HID_DEVMOTION) == 0) {
                gPublished |= 1u << SNS_HID_DEVMOTION;
            } else {
                printf("[sensors] failed to publish device motion compass service\n");
            }
        }
        printf("[sensors] hid services published=%#x\n", gPublished);

        const unsigned nodes = sns_hid_nodes(mode, gPublished, want_nodes);
        if (nodes) {
            int ra = (nodes & SNS_HID_NODE_ACCEL) ? fuxi_sensors_hid_node("accel") : -1;
            int rg = (nodes & SNS_HID_NODE_GYRO) ? fuxi_sensors_hid_node("gyro") : -1;
            printf("[sensors] dt nodes /product accel=%d gyro=%d\n", ra, rg);
        }
    }

    if (rpc) {
        int rrc = fuxi_sensors_rpc_start(log_level);
        if (rrc != 0) {
            printf("[sensors] rpc start failed err=%d\n", rrc);
        }
    }

    if (mode >= 2u) {
        gRing = (struct fs_slot *)kalloc_data(sizeof(struct fs_slot) * kRingSlots, Z_WAITOK | Z_ZERO);
        gCore = (SnsCore *)kalloc_data(sizeof(SnsCore), Z_WAITOK | Z_ZERO);
        if (!gRing || !gCore) {
            printf("[sensors] alloc failed\n");
            return;
        }
        const unsigned nports = gConfig.list ? SNS_CONNS : (SNS_CONNS - 1u);
        unsigned opened = 0;
        for (unsigned i = 0; i < nports; ++i) {
            gPort[i] = sheng_qrtr_open_client(on_qrtr_receive, (void *)(uintptr_t)i);
            if (!gPort[i]) break;
            opened++;
        }
        if (opened < nports) {
            printf("[sensors] qrtr ports failed (%u/%u)\n", opened, nports);
            return;
        }
        (void)sheng_qrtr_watch(on_qrtr_server_change, NULL);
        static const SnsOps kOps = {
            .context = NULL,
            .now_ms = fs_now_ms,
            .send = op_send,
            .trace = fs_trace,
            .sample = op_sample,
            .found = op_found,
            .orientation = op_orientation,
            .compass = op_compass,
        };
        sns_core_init(gCore, &kOps, &gConfig);
        printf("[sensors] qrtr ports=%u first=%#x watching service 0x190\n",
            opened, sheng_qrtr_port_number(gPort[0]));
    }

    thread_t thread = NULL;
    if (kernel_thread_start(sensors_thread, NULL, &thread) != KERN_SUCCESS) {
        printf("[sensors] thread start failed\n");
        return;
    }
    thread_deallocate(thread);
}
#endif /* FUXI */
