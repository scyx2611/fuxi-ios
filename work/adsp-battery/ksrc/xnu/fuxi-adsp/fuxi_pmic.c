#include <pexpert/arm64/board_config.h>
#if defined(FUXI)
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/kauth.h>
#include <sys/sysctl.h>
#include <sys/proc.h>
#include <sys/codesign.h>
#include <kern/clock.h>
#include <kern/locks.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include <pexpert/device_tree.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>
#include "fuxi_torch_pulse.h"
#include "fuxi_torch_runtime.h"
#include "fuxi_torch_audit.h"
#include "fuxi_torch_steady.h"
#include "fuxi_haptic_pulse.h"
#include "fuxi_haptic_runtime.h"
#include "fuxi_haptic_rtp.h"
#include "sweet_spmi_bus.h"

extern kern_return_t pmap_enter_options_addr(pmap_t, vm_map_offset_t, uint64_t, vm_prot_t, vm_prot_t, unsigned, boolean_t,
    unsigned, void *);
void fuxi_pmic_init(void);
extern kern_return_t kernel_thread_start(thread_continue_t, void *, thread_t *);
extern void thread_deallocate(thread_t);
extern void IOSleep(unsigned milliseconds);

struct fp_window {
    uint64_t physical;
    vm_size_t size;
    vm_offset_t address;
};

static struct fp_window fp_core = {0x0c400000ull, 0x4000, 0};
static struct fp_window fp_config = {0x0c42c000ull, 0x8000, 0};
static struct fp_window fp_observer = {0x0c440000ull, 0x8000, 0};
static LCK_GRP_DECLARE(fp_group, "fuxi-pmic");
static LCK_MTX_DECLARE(fp_lock, &fp_group);
static unsigned fp_apids;
static uint8_t fp_poisoned[1024];
static unsigned fp_started;
static unsigned fp_rejected_logged;
static struct fp_window fp_flash_channel;
static LCK_MTX_DECLARE(fp_torch_lock, &fp_group);
static FuxiTorchRuntime fp_torch_state;
static unsigned fp_torch_ready;
static unsigned fp_torch_audit_enabled;
static struct fp_window fp_haptic_channels[2];
/* Linux spmi-pmic-arb v5/v7 retains the PPID to APID mapping. Enable the
 * fast path only for the two verified EE0 haptic channels on a test boot. */
static unsigned fp_haptic_apid_plus_one[2];
static FuxiHapticState fp_haptic_state;
static LCK_MTX_DECLARE(fp_vibrate_lock, &fp_group);
static FuxiVibrateRuntime fp_vibrate_state;
static unsigned fp_vibrate_ready;
static unsigned fp_vibrate_kick;
static unsigned fp_vibrate_selftest;
static unsigned fp_vibrate_transient, fp_vibrate_brake;
static const uint8_t *fp_rtp_wave;
static uint32_t fp_rtp_vmax;
static unsigned fp_rtp_pcm_enabled;
static unsigned fp_rtp_ready;
static uint64_t fp_rtp_deadline;
static struct {
    uint8_t samples[FUXI_RTP_BYTES];
    unsigned length;
    uint64_t deadline;
} fp_rtp_pending[8];
static unsigned fp_rtp_head, fp_rtp_count;
#define FP_HAPTIC_ATTACK_NS 5710000ull /* Fuxi DT 的 LRA 週期 */

static kern_return_t fp_map(struct fp_window *window)
{
    vm_offset_t address = 0;
    kern_return_t result = kmem_alloc(kernel_map, &address, window->size,
        KMA_VAONLY | KMA_KOBJECT | KMA_NOPAGEWAIT, VM_KERN_MEMORY_IOKIT);
    if (result != KERN_SUCCESS) return result;
    for (vm_size_t offset = 0; offset < window->size; offset += PAGE_SIZE) {
        result = pmap_enter_options_addr(kernel_pmap, address + offset, window->physical + offset,
            VM_PROT_READ | VM_PROT_WRITE, VM_PROT_NONE, VM_WIMG_IO, TRUE, PMAP_OPTIONS_NOWAIT, NULL);
        if (result != KERN_SUCCESS) {
            pmap_remove(kernel_pmap, address, address + window->size);
            return result;
        }
    }
    window->address = address;
    return KERN_SUCCESS;
}

static uint32_t fp_read32(const struct fp_window *window, unsigned offset)
{
    uint32_t value = *(volatile uint32_t *)(window->address + offset);
    __asm__ volatile("dmb oshld" ::: "memory");
    return value;
}

static void fp_write32(const struct fp_window *window, unsigned offset, uint32_t value)
{
    *(volatile uint32_t *)(window->address + offset) = value;
    __asm__ volatile("dsb osh" ::: "memory");
}

static int fp_find(unsigned sid, unsigned peripheral, unsigned *owner)
{
    int selected = -1;
    const unsigned ppid = sid << 8 | peripheral;
    if (sid == 7 && (peripheral == 0xf0 || peripheral == 0xf1)) {
        unsigned cached = __atomic_load_n(&fp_haptic_apid_plus_one[peripheral-0xf0], __ATOMIC_ACQUIRE);
        if (cached) {
            unsigned apid = cached - 1;
            if (apid >= fp_apids) return -1;
            uint32_t map = fp_read32(&fp_core, 0x2000 + 4 * apid);
            if (((map >> 8) & 0xfff) != ppid) return -1;
            *owner = fp_read32(&fp_config, 0x1000 + 4 * apid) & 7u;
            return *owner == 0 ? (int)apid : -1;
        }
    }
    for (unsigned apid = 0; apid < fp_apids; apid++) {
        uint32_t map = fp_read32(&fp_core, 0x2000 + 4 * apid);
        if (!map || ((map >> 8) & 0xfff) != ppid) continue;
        unsigned candidate_owner = fp_read32(&fp_config, 0x1000 + 4 * apid) & 7u;
        if (selected < 0 || candidate_owner == 0) {
            selected = (int)apid;
            *owner = candidate_owner;
        }
    }
    return selected;
}

static int fp_read_registers(unsigned sid, unsigned address, uint8_t *values, unsigned count)
{
    unsigned owner = 0;
    int result = -1;
    lck_mtx_lock(&fp_lock);
    int apid = fp_find(sid, address >> 8, &owner);
    if (apid < 0 || fp_poisoned[apid]) goto done;
    unsigned channel = (unsigned)apid * 0x20;
    /* 同一個 arbiter 一次只服務一筆 EE0 交易；按鍵輪詢（20 ms）、RTC 與 WLAN P17 都用 sweet_spmi_bus_lock，
     * 這裡在 fp_lock 之內再取它（順序固定 fp_lock -> bus），從命令寫入持有到結果讀完。 */
    sweet_spmi_bus_lock();
    fp_write32(&fp_observer, channel, 1u << 27 | (address & 0xffu) << 4 | (count - 1u));
    for (unsigned poll = 0; poll < 1000; poll++) {
        uint32_t status = fp_read32(&fp_observer, channel + 8);
        if (status & 1u) {
            if (!(status & 0x0eu)) {
                uint32_t data = fp_read32(&fp_observer, channel + 0x18);
                for (unsigned i = 0; i < count; i++)
                    values[i] = (uint8_t)(data >> (8 * i));
                result = 0;
            } else if (fp_rejected_logged < 8) {
                fp_rejected_logged++;
                printf("FUXI PMIC: read rejected sid=%u addr=%#x APID=%d owner=%u status=%#x failure=%u denied=%u dropped=%u\n",
                    sid, address, apid, owner, status, !!(status & 2u), !!(status & 4u), !!(status & 8u));
            }
            goto bus_done;
        }
        delay(1);
    }
    fp_poisoned[apid] = 1;
    printf("FUXI PMIC: timeout sid=%u addr=%#x APID=%d; channel disabled\n", sid, address, apid);
bus_done:
    sweet_spmi_bus_unlock();
done:
    lck_mtx_unlock(&fp_lock);
    return result;
}

static int fp_read(unsigned sid, unsigned address, uint8_t *value)
{
    return fp_read_registers(sid, address, value, 1);
}

static void fp_survey(unsigned sid, unsigned base, const char *name)
{
    static const uint8_t offsets[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x10, 0x26, 0x41, 0x46, 0x47, 0x48, 0x49,
        0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x50, 0x5c, 0x5d, 0x60, 0x63, 0x66, 0x67,
        0x68, 0x70, 0x71, 0x72};
    unsigned owner = 7;
    int apid = fp_find(sid, base >> 8, &owner);
    printf("FUXI PMIC: %s sid=%u base=%#x APID=%d owner=%u survey-only\n", name, sid, base, apid, owner);
    if (apid < 0) return;
    for (unsigned index = 0; index < sizeof(offsets); index++) {
        uint8_t value = 0;
        int result = fp_read(sid, base + offsets[index], &value);
        printf("FUXI PMIC: %s +%02x=%02x rc=%d\n", name, offsets[index], value, result);
        if (fp_poisoned[apid]) break;
    }
}

static int fp_flash_read(void *context, unsigned offset, uint8_t *value)
{
    (void)context;
    if (offset > 0xff) return -1;
    return fp_read(1, 0xee00 + offset, value);
}

static int fp_flash_write(void *context, unsigned offset, uint8_t value)
{
    (void)context;
    if (offset != 0x3e && offset != 0x42 && offset != 0x46 &&
        offset != 0x49 && offset != 0x4a && offset != 0x4e) return -1;
    unsigned owner = 7;
    int result = -1;
    lck_mtx_lock(&fp_lock);
    int apid = fp_find(1, 0xee, &owner);
    if (apid < 0 || owner != 0 || fp_poisoned[apid] || !fp_flash_channel.address) goto done;
    uint64_t physical = 0x0c500000ull + (unsigned)apid * 0x1000u;
    if ((physical & ~(uint64_t)PAGE_MASK) != fp_flash_channel.physical) goto done;
    unsigned channel = physical & PAGE_MASK;
    sweet_spmi_bus_lock();
    fp_write32(&fp_flash_channel, channel + 0x10, value);
    fp_write32(&fp_flash_channel, channel, offset << 4);
    for (unsigned poll = 0; poll < 1000; poll++) {
        uint32_t status = fp_read32(&fp_flash_channel, channel + 8);
        if (status & 1u) {
            result = status & 0x0eu ? -1 : 0;
            if (result && fp_rejected_logged < 8) {
                fp_rejected_logged++;
                printf("FUXI PMIC: flash write rejected +%02x APID=%d owner=%u status=%#x failure=%u denied=%u dropped=%u\n",
                    offset, apid, owner, status, !!(status & 2u), !!(status & 4u), !!(status & 8u));
            }
            goto bus_done;
        }
        delay(1);
    }
    fp_poisoned[apid] = 1;
    printf("FUXI PMIC: flash write timeout +%02x APID=%d; channel disabled\n", offset, apid);
bus_done:
    sweet_spmi_bus_unlock();
done:
    lck_mtx_unlock(&fp_lock);
    return result;
}

static void fp_flash_wait(void *context, unsigned microseconds)
{
    (void)context;
    delay(microseconds);
}

static int fp_flash_prepare(void)
{
    unsigned owner = 7;
    int apid = fp_find(1, 0xee, &owner);
    if (apid < 0 || owner != 0) {
        printf("FUXI PMIC: flash pulse refused APID=%d owner=%u\n", apid, owner);
        return -1;
    }
    uint64_t channel = 0x0c500000ull + (unsigned)apid * 0x1000u;
    fp_flash_channel.physical = channel & ~(uint64_t)PAGE_MASK;
    fp_flash_channel.size = PAGE_SIZE;
    if (fp_map(&fp_flash_channel) != KERN_SUCCESS) {
        printf("FUXI PMIC: flash channel mapping failed\n");
        return -1;
    }
    return 0;
}

static void fp_flash_pulse(void)
{
    if (fp_flash_prepare()) return;
    FuxiTorchIo io = {NULL, fp_flash_read, fp_flash_write, fp_flash_wait};
    printf("FUXI PMIC: flash pulse begin channel=0 current=40mA duration=80ms hardware-timer=100ms\n");
    int result = fuxi_torch_pulse(&io);
    printf("FUXI PMIC: flash pulse result=%d%s\n", result,
        result == 0 ? " registers restored; visible light needs observation" : " failed; not accepted");
}

static uint64_t fp_torch_now(void)
{
    uint64_t absolute, nanoseconds;
    clock_get_uptime(&absolute);
    absolutetime_to_nanoseconds(absolute, &nanoseconds);
    return nanoseconds;
}

static int fp_torch_sysctl SYSCTL_HANDLER_ARGS
{
    (void)oidp;
    (void)arg1;
    (void)arg2;
    if (!__atomic_load_n(&fp_torch_ready, __ATOMIC_ACQUIRE)) return ENOTSUP;
    if (req->newptr) {
        char name[32] = {0};
        proc_selfname(name, sizeof(name));
        proc_t process = proc_self();
        int platform = process && csproc_get_platform_binary(process) != 0;
        if (process) proc_rele(process);
        if (!fuxi_torch_caller_allowed(kauth_cred_issuser(kauth_cred_get()), platform, name)) return EPERM;
    }
    lck_mtx_lock(&fp_torch_lock);
    int value = (int)fuxi_torch_requested(&fp_torch_state, fp_torch_now());
    int failed = fp_torch_state.failed;
    lck_mtx_unlock(&fp_torch_lock);
    if (failed && !req->newptr) return EIO;
    int changed = 0;
    int error = sysctl_io_number(req, value, sizeof(value), &value, &changed);
    if (error || !changed) return error;
    if (value < 0 || value > 4) return EINVAL;
    lck_mtx_lock(&fp_torch_lock);
    error = fuxi_torch_request(&fp_torch_state, (unsigned)value, fp_torch_now()) ? EIO : 0;
    lck_mtx_unlock(&fp_torch_lock);
    return error;
}

SYSCTL_PROC(_kern, OID_AUTO, fuxitorch, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_ANYBODY | CTLFLAG_LOCKED,
    0, 0, fp_torch_sysctl, "I", "Fuxi torch level 0..4, platform SpringBoard or root, two second lease");

static void fp_torch_wait(void *context, unsigned microseconds)
{
    if (context) fuxi_torch_audit_record(context, 'S', microseconds, 0, 0, fp_torch_now());
    IOSleep((microseconds + 999u) / 1000u);
    if (context) fuxi_torch_audit_record(context, 'E', microseconds, 0, 0, fp_torch_now());
}

static int fp_torch_audit_read(void *context, unsigned offset, uint8_t *value)
{
    int result = fp_flash_read(NULL, offset, value);
    fuxi_torch_audit_record(context, 'R', offset, result ? 0 : *value, result, fp_torch_now());
    return result;
}

static int fp_torch_audit_write(void *context, unsigned offset, uint8_t value)
{
    int result = fp_flash_write(NULL, offset, value);
    fuxi_torch_audit_record(context, 'W', offset, value, result, fp_torch_now());
    return result;
}

static void fp_torch_worker(void *argument, wait_result_t wait_result)
{
    (void)argument;
    (void)wait_result;
    FuxiTorchAudit audit;
    FuxiTorchSteady steady = {0};
    FuxiTorchIo io = {NULL, fp_flash_read, fp_flash_write, fp_torch_wait};
    if (fp_torch_audit_enabled) {
        io.context = &audit;
        io.read = fp_torch_audit_read;
        io.write = fp_torch_audit_write;
    }
    unsigned previous = 0;
    for (;;) {
        lck_mtx_lock(&fp_torch_lock);
        unsigned level = fuxi_torch_requested(&fp_torch_state, fp_torch_now());
        unsigned failed = fp_torch_state.failed;
        lck_mtx_unlock(&fp_torch_lock);
        if (failed) {
            IOSleep(20);
            continue;
        }
        if (level != previous) {
            printf("FUXI PMIC: torch request=%u current=%umA lease=2000ms\n", level, level * 20);
            previous = level;
        }
        if (level || steady.prepared) {
            uint64_t check_started = fp_torch_now();
            fuxi_torch_audit_begin(io.context, check_started);
            unsigned changed = steady.level != level;
            int result = fuxi_torch_steady_step(&io, &steady, level);
            uint64_t check_finished = fp_torch_now();
            lck_mtx_lock(&fp_torch_lock);
            if (result) {
                fp_torch_state.failed = 1;
                fp_torch_state.level = 0;
                printf("FUXI PMIC: torch steady runtime stopped rc=%d; reboot required\n", result);
                static const unsigned offsets[] = {0x46, 0x4e, 6, 7, 9};
                uint8_t status[5] = {0};
                int status_result = 0;
                for (unsigned index = 0; index < 5; index++)
                    status_result |= fp_flash_read(NULL, offsets[index], &status[index]);
                printf("FUXI PMIC: torch stopped transitions=%llu module=%#x channels=%#x status=%#x/%#x/%#x read-rc=%d\n",
                    fp_torch_state.pulses, status[0], status[1], status[2], status[3], status[4], status_result);
                printf("FUXI PMIC: torch stopped elapsed-us=%llu audit=%u\n",
                    check_finished >= check_started ? (check_finished - check_started) / 1000 : UINT64_MAX,
                    fp_torch_audit_enabled);
                if (fp_torch_audit_enabled) {
                    for (unsigned index = 0; index < audit.count; index++) {
                        const FuxiTorchAuditEvent *event = &audit.events[index];
                        printf("FUXI PMIC: torch audit=%u op=%c offset=%#x value=%#x rc=%d elapsed-us=%llu\n",
                            index, event->operation, event->offset, event->value, event->result, event->elapsed_us);
                    }
                    printf("FUXI PMIC: torch audit count=%u dropped=%u\n", audit.count, audit.dropped);
                }
            } else if (changed) {
                fp_torch_state.pulses++;
                printf("FUXI PMIC: torch steady transition=%llu level=%u continuous=%u timer=off\n",
                    fp_torch_state.pulses, level, !!level);
            }
            lck_mtx_unlock(&fp_torch_lock);
        }
        IOSleep(20);
    }
}

static void fp_torch_start(void)
{
    if (fp_flash_prepare()) return;
    thread_t thread = NULL;
    if (kernel_thread_start(fp_torch_worker, NULL, &thread) != KERN_SUCCESS) {
        printf("FUXI PMIC: torch runtime worker failed\n");
        return;
    }
    thread_deallocate(thread);
    __atomic_store_n(&fp_torch_ready, 1u, __ATOMIC_RELEASE);
    printf("FUXI PMIC: torch steady runtime ready; admitted sysctl kern.fuxitorch, 20..80mA, output off\n");
    if (fp_torch_audit_enabled) printf("FUXI PMIC: torch bounded I/O audit enabled; continuous output, 20ms health checks\n");
}

static int fp_haptic_owner(void *context, unsigned peripheral, unsigned *owner)
{
    (void)context;
    if (peripheral < 0xf0 || peripheral > 0xf2) return -1;
    lck_mtx_lock(&fp_lock);
    int apid = fp_find(7, peripheral, owner);
    int result = apid < 0 || fp_poisoned[apid] ? -1 : 0;
    lck_mtx_unlock(&fp_lock);
    return result;
}

static int fp_haptic_read(void *context, unsigned address, uint8_t *value)
{
    (void)context;
    if (address < 0xf000 || address > 0xf2ff) return -1;
    return fp_read(7, address, value);
}

static int fp_rtp_read_status(uint8_t status[2])
{
    return fp_read_registers(7, 0xf009, status, 2);
}

static int fp_haptic_transfer(unsigned address, uint32_t payload, unsigned bytes)
{
    unsigned owner = 7;
    int result = -1;
    lck_mtx_lock(&fp_lock);
    int apid = fp_find(7, address >> 8, &owner);
    struct fp_window *window = &fp_haptic_channels[(address >> 8) - 0xf0];
    if (apid < 0 || owner != 0 || fp_poisoned[apid] || !window->address) goto done;
    uint64_t physical = 0x0c500000ull + (unsigned)apid * 0x1000u;
    if ((physical & ~(uint64_t)PAGE_MASK) != window->physical) goto done;
    unsigned channel = physical & PAGE_MASK;
    sweet_spmi_bus_lock();
    fp_write32(window, channel + 0x10, payload);
    fp_write32(window, channel, ((address & 0xffu) << 4) | (bytes - 1u));
    for (unsigned poll = 0; poll < 1000; poll++) {
        uint32_t status = fp_read32(window, channel + 8);
        if (status & 1u) {
            result = status & 0x0eu ? -1 : 0;
            goto bus_done;
        }
        delay(1);
    }
    fp_poisoned[apid] = 1;
    printf("FUXI PMIC: haptic write timeout addr=%#x APID=%d; channel disabled\n", address, apid);
bus_done:
    sweet_spmi_bus_unlock();
done:
    lck_mtx_unlock(&fp_lock);
    return result;
}

static int fp_haptic_write(void *context, unsigned address, uint8_t value)
{
    (void)context;
    if (!fuxi_haptic_writable(address)) return -1;
    return fp_haptic_transfer(address, value, 1);
}

/* 0xf120 的四個樣點必須在同一筆 SPMI 寫交易送出。 */
static int fp_rtp_write(unsigned address, const uint8_t *data, unsigned size)
{
    if (address == 0xf120 && size == 4) {
        return fp_haptic_transfer(address, (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
            ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24), size);
    }
    if (size != 1) return -1;
    switch (address) {
    case 0xf048: case 0xf04a: case 0xf04c: case 0xf05e: case 0xf05f: case 0xf070: case 0xf071:
    case 0xf072: case 0xf124: case 0xf12a: case 0xf12d:
        return fp_haptic_transfer(address, data[0], 1);
    default:
        return -1;
    }
}

static int fp_haptic_prepare(void)
{
    unsigned found[2];
    for (unsigned index = 0; index < 2; index++) {
        unsigned owner = 7;
        int apid = fp_find(7, 0xf0 + index, &owner);
        if (apid < 0 || owner != 0 || fp_poisoned[apid]) {
            fp_haptic_state.failed = 1;
            printf("FUXI PMIC: haptic pulse refused peripheral=%#x APID=%d owner=%u\n",
                0xf0 + index, apid, owner);
            return -1;
        }
        uint64_t channel = 0x0c500000ull + (unsigned)apid * 0x1000u;
        fp_haptic_channels[index].physical = channel & ~(uint64_t)PAGE_MASK;
        fp_haptic_channels[index].size = PAGE_SIZE;
        if (fp_map(&fp_haptic_channels[index]) != KERN_SUCCESS) {
            fp_haptic_state.failed = 1;
            printf("FUXI PMIC: haptic channel mapping failed peripheral=%#x\n", 0xf0 + index);
            return -1;
        }
        found[index] = (unsigned)apid;
    }
    DTEntry chosen;
    const void *prop;
    unsigned bytes;
    uint32_t opt = 0;
    if (fp_rtp_pcm_enabled && SecureDTLookupEntry(NULL, "/chosen", &chosen) == kSuccess &&
        SecureDTGetProperty(chosen, "fuxi,haptic-fast-apid", &prop, &bytes) == kSuccess && bytes == sizeof(opt))
        memcpy(&opt, prop, sizeof(opt));
    if (opt == 1) {
        for (unsigned i = 0; i < 2; i++)
            __atomic_store_n(&fp_haptic_apid_plus_one[i], found[i] + 1, __ATOMIC_RELEASE);
        printf("FUXI PMIC: haptic PCM cached two EE0 APIDs with live map and owner checks\n");
    }
    return 0;
}

/* ---- HAP525 DIRECT_PLAY 執行期（fuxi,haptic=2；3 另加開機自測三下） ---- */
static void fp_vibrate_kick_worker(void)
{
    __atomic_fetch_add(&fp_vibrate_kick, 1u, __ATOMIC_RELEASE);
    thread_wakeup((event_t)&fp_vibrate_kick);
}

static int fp_vibrate_write_allowed(void)
{
    char name[32] = {0};
    proc_selfname(name, sizeof(name));
    proc_t process = proc_self();
    int platform = process && csproc_get_platform_binary(process) != 0;
    if (process) proc_rele(process);
    return fuxi_vibrate_caller_allowed(kauth_cred_issuser(kauth_cred_get()), platform, name);
}

static int fp_vibrate_pcm_sysctl SYSCTL_HANDLER_ARGS
{
    (void)oidp;
    (void)arg1;
    (void)arg2;
    if (!fp_rtp_pcm_enabled || !__atomic_load_n(&fp_rtp_ready, __ATOMIC_ACQUIRE)) return ENOTSUP;
    if (!req->newptr || req->oldptr) return EINVAL;
    if (!fp_vibrate_write_allowed()) return EPERM;
    size_t length = req->newlen;
    if (length < 8 || length > FUXI_RTP_BYTES || (length & 3u)) return EINVAL;
    uint8_t samples[FUXI_RTP_BYTES];
    int error = SYSCTL_IN(req, samples, length);
    if (error) return error;
    for (size_t i = 0; i < length; i++) {
        int sample = (int8_t)samples[i];
        if (sample < -FUXI_RTP_PCM_LIMIT || sample > FUXI_RTP_PCM_LIMIT ||
            (i >= length - 4 && sample != 0)) return EINVAL;
    }
    lck_mtx_lock(&fp_vibrate_lock);
    if (fp_vibrate_state.failed || !__atomic_load_n(&fp_rtp_ready, __ATOMIC_RELAXED)) {
        error = EIO;
    } else if (fp_rtp_count == sizeof(fp_rtp_pending) / sizeof(fp_rtp_pending[0])) {
        error = EAGAIN;
    } else {
        unsigned slot = (fp_rtp_head + fp_rtp_count) % (sizeof(fp_rtp_pending) / sizeof(fp_rtp_pending[0]));
        memcpy(fp_rtp_pending[slot].samples, samples, length);
        fp_rtp_pending[slot].length = (unsigned)length;
        fp_rtp_pending[slot].deadline = fp_torch_now() + 40000000ull;
        fp_rtp_count++;
    }
    lck_mtx_unlock(&fp_vibrate_lock);
    if (!error) fp_vibrate_kick_worker();
    return error;
}

SYSCTL_PROC(_kern, OID_AUTO, fuxihapticpcm, CTLTYPE_OPAQUE | CTLFLAG_WR | CTLFLAG_ANYBODY | CTLFLAG_LOCKED,
    0, 0, fp_vibrate_pcm_sysctl, "S,fuxihapticpcm", "Bounded signed 24 kHz HAP525 FIFO samples");

static int fp_vibrate_sysctl SYSCTL_HANDLER_ARGS
{
    (void)oidp;
    (void)arg1;
    (void)arg2;
    if (!__atomic_load_n(&fp_vibrate_ready, __ATOMIC_ACQUIRE)) return ENOTSUP;
    if (req->newptr && !fp_vibrate_write_allowed()) return EPERM;
    lck_mtx_lock(&fp_vibrate_lock);
    int value = (int)fuxi_vibrate_remaining_ms(&fp_vibrate_state, fp_torch_now());
    int failed = fp_vibrate_state.failed;
    lck_mtx_unlock(&fp_vibrate_lock);
    if (failed && !req->newptr) return EIO;
    int changed = 0;
    int error = sysctl_io_number(req, value, sizeof(value), &value, &changed);
    if (error || !changed) return error;
    unsigned ms = 0, level = 0;
    /* HAL emits this once per contiguous tactile gesture, never per PCM window. */
    int click = value == 0x01000000;
    if (click && !__atomic_load_n(&fp_rtp_ready, __ATOMIC_ACQUIRE)) {
        /* The first RTP HAL build cannot fall back itself. Keep basic UI
         * feedback available with a single bounded pulse. */
        if (!fp_rtp_wave) return ENOTSUP;
        click = 0;
        value = (96 << FUXI_VIBRATE_LEVEL_SHIFT) | 15;
    }
    if (!click && fuxi_vibrate_decode(value, &ms, &level)) return EINVAL;
    lck_mtx_lock(&fp_vibrate_lock);
    uint64_t now = fp_torch_now();
    if (click && fp_vibrate_state.failed) {
        error = EIO;
    } else {
        error = fuxi_vibrate_request_level(&fp_vibrate_state, ms, level, now) ? EIO : 0;
        if (!error) fp_rtp_deadline = click ? now + 40000000ull : 0;
    }
    lck_mtx_unlock(&fp_vibrate_lock);
    if (!error) fp_vibrate_kick_worker();
    return error;
}

SYSCTL_PROC(_kern, OID_AUTO, fuxivibrate, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_ANYBODY | CTLFLAG_LOCKED,
    0, 0, fp_vibrate_sysctl, "I",
    "Fuxi HAP525 direct play ms 0..2000 | amplitude<<16 (0 keeps), platform SpringBoard/mediaserverd or root");

static uint8_t fp_vibrate_android_amplitude, fp_vibrate_android_vmax; /* 開機時 Android 留下的值，只給紀錄 */

/* 等到 deadline_ns（0 表示不限）或有新請求。 */
static void fp_vibrate_sleep(unsigned seen, uint64_t deadline_ns)
{
    if (deadline_ns) {
        uint64_t deadline = 0;
        nanoseconds_to_absolutetime(deadline_ns, &deadline);
        assert_wait_deadline((event_t)&fp_vibrate_kick, THREAD_UNINT, deadline);
    } else {
        assert_wait((event_t)&fp_vibrate_kick, THREAD_UNINT);
    }
    if (__atomic_load_n(&fp_vibrate_kick, __ATOMIC_ACQUIRE) != seen) {
        clear_wait(current_thread(), THREAD_AWAKENED);
        return;
    }
    thread_block(THREAD_CONTINUE_NULL);
}

static void fp_vibrate_fail(const FuxiHapticIo *io, const char *where, int result)
{
    uint8_t fault = 0, hpwr = 0, play = 0, module = 0;
    int read_result = io->read(io->context, 0xf00c, &fault) | io->read(io->context, 0xf00b, &hpwr) |
        io->read(io->context, 0xf04c, &play) | io->read(io->context, 0xf046, &module);
    lck_mtx_lock(&fp_vibrate_lock);
    fp_vibrate_state.failed = 1;
    fp_vibrate_state.deadline = 0;
    fp_vibrate_state.last_error = result;
    lck_mtx_unlock(&fp_vibrate_lock);
    printf("FUXI PMIC: vibrate %s rc=%d fault=%#x hpwr=%#x play=%#x module=%#x read-rc=%d; runtime locked until reboot\n",
        where, result, fault, hpwr, play, module, read_result);
}

static void fp_vibrate_worker(void *argument, wait_result_t wait_result)
{
    (void)argument;
    (void)wait_result;
    const FuxiHapticIo io = {NULL, fp_haptic_owner, fp_haptic_read, fp_haptic_write, fp_flash_wait, 0};
    unsigned playing = 0, logged = 0, health_checks = 0, amplitude_writes = 0, amplitude_logged = 0;
    unsigned autores_cleared = 0, autores_plays = 0, autores_logged = 0;
    unsigned pcm_played = 0, pcm_expired = 0;
    uint8_t mid_hpwr = 0, mid_fault = 0, amplitude = fp_vibrate_android_amplitude, low = 0, high = 0;
    uint64_t started_ns = 0;
    if (fp_rtp_wave) {
        FuxiRtpStats stats;
        int result = fuxi_rtp_click(&io, fp_rtp_write, fp_rtp_read_status, fp_rtp_wave, FUXI_RTP_BYTES, 0, &stats);
        printf("FUXI PMIC: haptic RTP FIFO boot click bytes=%u polls=%u rc=%d pre=%u refill=%u last=%u\n",
            stats.sent, stats.polls, result, stats.before_start, stats.before_refill, stats.last_fill);
        if (result == -2) {
            fp_vibrate_fail(&io, "RTP restore", result);
            return;
        }
        if (!result) __atomic_store_n(&fp_rtp_ready, 1u, __ATOMIC_RELEASE);
    }
    for (;;) {
        unsigned seen = __atomic_load_n(&fp_vibrate_kick, __ATOMIC_ACQUIRE);
        uint64_t now = fp_torch_now();
        uint8_t pcm[FUXI_RTP_BYTES];
        unsigned pcm_length = 0;
        uint64_t queued_ns = 0;
        lck_mtx_lock(&fp_vibrate_lock);
        unsigned failed = fp_vibrate_state.failed;
        unsigned want = fuxi_vibrate_wanted(&fp_vibrate_state, now);
        unsigned level = fp_vibrate_state.level;
        uint64_t deadline = fp_vibrate_state.deadline;
        int click = !playing && !failed && now < fp_rtp_deadline;
        if (!playing) fp_rtp_deadline = 0;
        if (!click && !want && !playing && !failed && fp_rtp_count) {
            unsigned slot = fp_rtp_head;
            queued_ns = fp_rtp_pending[slot].deadline - 40000000ull;
            if (now < fp_rtp_pending[slot].deadline) {
                pcm_length = fp_rtp_pending[slot].length;
                memcpy(pcm, fp_rtp_pending[slot].samples, pcm_length);
            }
            fp_rtp_head = (fp_rtp_head + 1) % (sizeof(fp_rtp_pending) / sizeof(fp_rtp_pending[0]));
            fp_rtp_count--;
            if (!pcm_length) {
                lck_mtx_unlock(&fp_vibrate_lock);
                if (++pcm_expired <= 8)
                    printf("FUXI PMIC: haptic RTP FIFO UI PCM expired=%u played=%u queue_us=%llu\n",
                        pcm_expired, pcm_played, (now - queued_ns) / 1000ull);
                continue;
            }
        }
        lck_mtx_unlock(&fp_vibrate_lock);
        if (click || pcm_length) {
            FuxiRtpStats stats = {0};
            uint8_t fault = 0;
            int result = io.read(io.context, 0xf00c, &fault);
            if (!result) result = fuxi_vibrate_clear_autores(&io, &fault, &autores_cleared);
            const uint8_t *wave = pcm_length ? pcm : fp_rtp_wave;
            unsigned length = pcm_length ? pcm_length : FUXI_RTP_BYTES;
            unsigned boosted_peak = pcm_length ? fuxi_rtp_pcm_gain(pcm, pcm_length) : 0;
            if (!result) result = fuxi_rtp_click(&io, fp_rtp_write, fp_rtp_read_status, wave, length, fp_rtp_vmax, &stats);
            if (pcm_length && !result) pcm_played++;
            if (logged < 64 || result) {
                logged++;
                if (pcm_length) printf("FUXI PMIC: haptic RTP FIFO UI PCM bytes=%u polls=%u rc=%d peak=%u played=%u expired=%u queue_us=%llu service_us=%llu\n",
                    stats.sent, stats.polls, result, boosted_peak, pcm_played, pcm_expired,
                    (now >= queued_ns ? now - queued_ns : 0) / 1000ull, (fp_torch_now() - now) / 1000ull);
                else printf("FUXI PMIC: haptic RTP FIFO UI click bytes=%u polls=%u rc=%d pre=%u refill=%u last=%u vmax=%#x\n",
                    stats.sent, stats.polls, result, stats.before_start, stats.before_refill, stats.last_fill,
                    fp_rtp_vmax ? fp_rtp_vmax : fp_vibrate_android_vmax);
            }
            if (result == -2) {
                fp_vibrate_fail(&io, "RTP UI restore", result);
            } else if (result) {
                __atomic_store_n(&fp_rtp_ready, 0u, __ATOMIC_RELEASE);
                lck_mtx_lock(&fp_vibrate_lock);
                fp_rtp_count = 0;
                if (seen == __atomic_load_n(&fp_vibrate_kick, __ATOMIC_ACQUIRE) &&
                    !fp_rtp_deadline && !fp_vibrate_state.deadline)
                    (void)fuxi_vibrate_request_level(&fp_vibrate_state, 15, 96, fp_torch_now());
                lck_mtx_unlock(&fp_vibrate_lock);
            }
            continue;
        }
        if (failed && !playing) {
            fp_vibrate_sleep(seen, 0);
            continue;
        }
        if (want && !failed) {
            /* 振幅先於開始寫；播放中改了也照寫（Linux haptics_set_gain 同樣在播放中改 DIRECT_PLAY） */
            if (fp_vibrate_transient && (!playing || now - started_ns < FP_HAPTIC_ATTACK_NS))
                level = fuxi_vibrate_attack_level(level);
            unsigned before = amplitude_writes;
            int result = fuxi_vibrate_amplitude(&io, level, &amplitude, &amplitude_writes);
            if (result) {
                unsigned polls = 0, toggled = 0;
                int stop_result = playing ? fuxi_vibrate_stop(&io, fp_vibrate_brake, &polls, &toggled) : 0;
                playing = 0;
                lck_mtx_lock(&fp_vibrate_lock);
                fp_vibrate_state.playing = 0;
                lck_mtx_unlock(&fp_vibrate_lock);
                fp_vibrate_fail(&io, "amplitude refused", result);
                printf("FUXI PMIC: vibrate amplitude level=%#x kept=%#x stop-rc=%d polls=%u toggled=%u\n",
                    level, amplitude, stop_result, polls, toggled);
                continue;
            }
            if (amplitude_writes != before && amplitude_logged < 8) {
                amplitude_logged++;
                printf("FUXI PMIC: vibrate amplitude %#x (Android left %#x, VMAX %#x kept) writes=%u%s\n",
                    amplitude, fp_vibrate_android_amplitude, fp_vibrate_android_vmax, amplitude_writes,
                    playing ? " while playing" : "");
            }
            if (playing) {
                if (amplitude < low) low = amplitude;
                if (amplitude > high) high = amplitude;
            }
        }
        if (want && !playing) {
            low = high = amplitude;
            int result = fuxi_vibrate_start(&io, fp_vibrate_brake, &autores_cleared);
            if (result) {
                unsigned polls = 0, toggled = 0;
                int stop_result = fuxi_vibrate_stop(&io, fp_vibrate_brake, &polls, &toggled);
                fp_vibrate_fail(&io, "start refused", result);
                printf("FUXI PMIC: vibrate start cleanup rc=%d polls=%u toggled=%u\n", stop_result, polls, toggled);
                continue;
            }
            playing = 1;
            health_checks = 0;
            mid_hpwr = mid_fault = 0;
            started_ns = now;
            lck_mtx_lock(&fp_vibrate_lock);
            fp_vibrate_state.playing = 1;
            fp_vibrate_state.plays++;
            lck_mtx_unlock(&fp_vibrate_lock);
            continue;
        }
        if (playing && (!want || failed)) {
            unsigned polls = 0, toggled = 0;
            uint64_t stop_ns = fp_torch_now();
            int result = fuxi_vibrate_stop(&io, fp_vibrate_brake, &polls, &toggled);
            uint64_t stopped_ns = fp_torch_now();
            playing = 0;
            lck_mtx_lock(&fp_vibrate_lock);
            fp_vibrate_state.playing = 0;
            uint64_t plays = fp_vibrate_state.plays;
            lck_mtx_unlock(&fp_vibrate_lock);
            if (logged < 16 || result || toggled) {
                logged++;
                printf("FUXI PMIC: vibrate play=%llu on-us=%llu checks=%u hpwr-mid=%#x fault-mid=%#x stop-rc=%d stop-polls=%u stop-us=%llu toggled=%u amp=%#x..%#x autores-plays=%u cleared=%u\n",
                    plays, (stop_ns - started_ns) / 1000, health_checks, mid_hpwr, mid_fault, result, polls,
                    (stopped_ns - stop_ns) / 1000, toggled, low, high, autores_plays, autores_cleared);
            }
            if (result) fp_vibrate_fail(&io, "stop unverified", result);
            continue;
        }
        if (playing) {
            uint8_t fault = 0, hpwr = 0;
            int result = fuxi_vibrate_health(&io, &fault, &hpwr);
            health_checks++;
            mid_hpwr = hpwr; /* 停止前最後一次看到的 HPWR；播放中應為 3（READY） */
            if (!result && (fault & FUXI_VIBRATE_FAULT_AUTORES) && !(mid_fault & FUXI_VIBRATE_FAULT_AUTORES)) {
                /* Linux 播放中不看這一位；照常播完，下次開播前清 */
                autores_plays++;
                if (autores_logged < 4) {
                    autores_logged++;
                    printf("FUXI PMIC: vibrate auto-resonance error while playing amp=%#x on-us=%llu hpwr=%#x; continuing, cleared before next play (count=%u)\n",
                        amplitude, (fp_torch_now() - started_ns) / 1000, hpwr, autores_plays);
                }
            }
            mid_fault |= fault;
            if (result) {
                unsigned polls = 0, toggled = 0;
                int stop_result = fuxi_vibrate_stop(&io, fp_vibrate_brake, &polls, &toggled);
                playing = 0;
                lck_mtx_lock(&fp_vibrate_lock);
                fp_vibrate_state.playing = 0;
                lck_mtx_unlock(&fp_vibrate_lock);
                fp_vibrate_fail(&io, "fault while playing", result);
                printf("FUXI PMIC: vibrate fault stop rc=%d polls=%u toggled=%u on-us=%llu\n", stop_result, polls, toggled,
                    (fp_torch_now() - started_ns) / 1000);
                continue;
            }
            uint64_t next = fp_torch_now() + FUXI_VIBRATE_POLL_MS * 1000000ull;
            if (fp_vibrate_transient && now - started_ns < FP_HAPTIC_ATTACK_NS && started_ns + FP_HAPTIC_ATTACK_NS < next)
                next = started_ns + FP_HAPTIC_ATTACK_NS;
            fp_vibrate_sleep(seen, deadline && deadline < next ? deadline : next);
            continue;
        }
        fp_vibrate_sleep(seen, 0);
    }
}

static void fp_vibrate_selftest_worker(void *argument, wait_result_t wait_result)
{
    (void)argument;
    (void)wait_result;
    static const unsigned pulses[] = {30, 100, 300};
    IOSleep(2000);
    for (unsigned index = 0; index < 3; index++) {
        lck_mtx_lock(&fp_vibrate_lock);
        int result = fuxi_vibrate_request(&fp_vibrate_state, pulses[index], fp_torch_now());
        lck_mtx_unlock(&fp_vibrate_lock);
        printf("FUXI PMIC: vibrate selftest pulse=%u ms=%u rc=%d\n", index + 1, pulses[index], result);
        if (result) return;
        fp_vibrate_kick_worker();
        IOSleep(pulses[index] + 400);
    }
}

static void fp_vibrate_runtime_start(void)
{
    if (fp_haptic_prepare()) return;
    const FuxiHapticIo io = {NULL, fp_haptic_owner, fp_haptic_read, fp_haptic_write, fp_flash_wait, 0};
    int result = fuxi_vibrate_check(&io);
    if (!result && (fuxi_haptic_read_stable(&io, FUXI_VIBRATE_AMPLITUDE, &fp_vibrate_android_amplitude) ||
        fuxi_haptic_read_stable(&io, 0xf048, &fp_vibrate_android_vmax)))
        result = FUXI_HAPTIC_IO;
    if (result) {
        printf("FUXI PMIC: vibrate runtime refused check rc=%d; writes=0\n", result);
        return;
    }
    if (fp_vibrate_transient) {
        fp_vibrate_brake = fuxi_vibrate_stock_brake(&io) ? 0x08u : 0;
        printf("FUXI PMIC: haptic transient attack-us=5710 stock-brake=%u\n", !!fp_vibrate_brake);
    }
    thread_t thread = NULL;
    if (kernel_thread_start(fp_vibrate_worker, NULL, &thread) != KERN_SUCCESS) {
        printf("FUXI PMIC: vibrate runtime worker failed\n");
        return;
    }
    thread_deallocate(thread);
    __atomic_store_n(&fp_vibrate_ready, 1u, __ATOMIC_RELEASE);
    printf("FUXI PMIC: vibrate runtime ready; DIRECT_PLAY 0x81/0x01, sysctl kern.fuxivibrate ms<=%u | amplitude<<16, Android VMAX %#x kept, amplitude %#x until asked\n",
        (unsigned)FUXI_VIBRATE_MAX_MS, fp_vibrate_android_vmax, fp_vibrate_android_amplitude);
    if (fp_vibrate_selftest && kernel_thread_start(fp_vibrate_selftest_worker, NULL, &thread) == KERN_SUCCESS)
        thread_deallocate(thread);
}

static int fp_haptic_fifo_observe(unsigned round, unsigned address, uint8_t *value)
{
    if (address < 0xf000 || address >= 0xf200) return -1;
    unsigned owner_before = 7, owner_after = 7;
    lck_mtx_lock(&fp_lock);
    int apid_before = fp_find(7, address >> 8, &owner_before);
    int available = apid_before >= 0 && owner_before == 0 && !fp_poisoned[apid_before];
    lck_mtx_unlock(&fp_lock);
    uint8_t observed = 0;
    int result = available ? fp_read(7, address, &observed) : -1;
    lck_mtx_lock(&fp_lock);
    int apid_after = fp_find(7, address >> 8, &owner_after);
    unsigned valid = !result && apid_before == apid_after && apid_after >= 0 &&
        owner_before == 0 && owner_after == 0 && !fp_poisoned[apid_after];
    lck_mtx_unlock(&fp_lock);
    *value = observed;
    printf("FUXI PMIC: haptic-fifo-ro round=%u addr=%#x APID=%d/%d owner=%u/%u value=%02x rc=%d valid=%u\n",
        round, address, apid_before, apid_after, owner_before, owner_after, observed, result, valid);
    return valid ? 0 : -1;
}

static void fp_haptic_fifo_survey(unsigned mode)
{
    if (mode != 1) return;
    enum { FIELD_COUNT = 27, TLRA_HIGH = 12, TLRA_LOW = 13, FIFO_MMAP = 20 };
    static const unsigned addresses[FIELD_COUNT] = {
        0xf070, 0xf071, 0xf00b, 0xf00c, 0xf046, 0xf048, 0xf049,
        0xf04a, 0xf04b, 0xf04c, 0xf04d, 0xf04e, 0xf05c, 0xf05d,
        0xf063, 0xf068, 0xf072, 0xf124, 0xf12a, 0xf12d, 0xf1a0,
        0xf001, 0xf004, 0xf005, 0xf101, 0xf104, 0xf105
    };
    uint8_t values[2][FIELD_COUNT] = {{0}};
    uint8_t status[2][2] = {{0}};
    unsigned round_valid[2] = {1, 1}, fifo_valid[2] = {0, 0}, identity_valid[2] = {0, 0}, attempts = 0;
    printf("FUXI PMIC: haptic-fifo-ro begin mode=1 rounds=2 active-gate=0 writes=0\n");
    for (unsigned round = 0; round < 2; round++) {
        unsigned field_valid[FIELD_COUNT] = {0};
        for (unsigned index = 0; index < FIELD_COUNT; index++) {
            attempts++;
            field_valid[index] = !fp_haptic_fifo_observe(round + 1, addresses[index], &values[round][index]);
            if (!field_valid[index]) round_valid[round] = 0;
        }
        identity_valid[round] = field_valid[21] && field_valid[22] && field_valid[23] &&
            field_valid[24] && field_valid[25] && field_valid[26] &&
            values[round][21] == 4 && values[round][22] == 0x15 && values[round][23] == 0x0a &&
            values[round][24] == 4 && values[round][25] == 0x15 && values[round][26] == 0x0b;
        if (identity_valid[round] && field_valid[0] && field_valid[1] &&
            values[round][0] == 5 && values[round][1] == 0x80) {
            fifo_valid[round] = 1;
            for (unsigned index = 0; index < 2; index++) {
                attempts++;
                if (fp_haptic_fifo_observe(round + 1, 0xf009 + index, &status[round][index])) {
                    fifo_valid[round] = 0;
                    round_valid[round] = 0;
                }
            }
        }
        uint8_t selector = 0, selector_xt = 0;
        attempts += 2;
        int selector_result = fp_haptic_fifo_observe(round + 1, 0xf070, &selector);
        int selector_xt_result = fp_haptic_fifo_observe(round + 1, 0xf071, &selector_xt);
        unsigned selector_stable = field_valid[0] && field_valid[1] && !selector_result &&
            !selector_xt_result && selector == values[round][0] && selector_xt == values[round][1];
        if (!selector_stable) round_valid[round] = fifo_valid[round] = 0;
        unsigned tlra_valid = identity_valid[round] && field_valid[TLRA_HIGH] && field_valid[TLRA_LOW];
        unsigned period_us = tlra_valid ? (((unsigned)values[round][TLRA_HIGH] & 15u) * 256u +
            values[round][TLRA_LOW]) * 5u : 0;
        unsigned capacity = 0, fill = 0, empty = 0;
        const char *fifo_status = "unavailable";
        fifo_valid[round] = fifo_valid[round] && round_valid[round] &&
            field_valid[FIFO_MMAP] && (values[round][FIFO_MMAP] & 0x80u);
        if (fifo_valid[round]) {
            capacity = ((values[round][FIFO_MMAP] & 15u) + 1u) * 128u;
            fill = ((status[round][0] & 15u) << 8) | status[round][1];
            empty = !!(status[round][0] & 0x40u);
            unsigned full = !!(status[round][0] & 0x20u);
            if (fill > capacity || (empty && fill) || (full && fill != capacity) || (empty && full)) {
                fifo_valid[round] = round_valid[round] = 0;
                fifo_status = "contradictory";
            } else fifo_status = "observed";
        }
        printf("FUXI PMIC: haptic-fifo-ro round=%u valid=%u selector-stable=%u identity-valid=%u TLRA-valid=%u TLRA=%uus FIFO-status=%s fill=%u empty=%u capacity=%u writes=0 cutoff-verified=0\n",
            round + 1, round_valid[round], selector_stable, identity_valid[round], tlra_valid,
            period_us, fifo_status, fill, empty, capacity);
    }
    unsigned stable = round_valid[0] && round_valid[1];
    for (unsigned index = 0; index < FIELD_COUNT; index++)
        if (values[0][index] != values[1][index]) stable = 0;
    if (fifo_valid[0] != fifo_valid[1] || (fifo_valid[0] &&
        (status[0][0] != status[1][0] || status[0][1] != status[1][1]))) stable = 0;
    printf("FUXI PMIC: haptic-fifo-ro end rounds=2 attempts=%u valid=%u stable=%u identity-valid=%u active-gate=0 start-attempted=0 writes=0 cutoff-verified=0 route-verified=0\n",
        attempts, round_valid[0] && round_valid[1], stable, identity_valid[0] && identity_valid[1]);
}

static void fp_haptic_diagnostic(void)
{
    FuxiHapticIo io = {NULL, fp_haptic_owner, fp_haptic_read, fp_haptic_write, fp_flash_wait, 0};
    if (io.pattern1_finite_verified && fp_haptic_prepare()) return;
    printf("FUXI PMIC: haptic diagnostic begin PATTERN1 finite-bound-verified=%u\n", io.pattern1_finite_verified);
    int result = fuxi_haptic_pulse(&io, &fp_haptic_state);
    printf("FUXI PMIC: haptic result=%d probed=%u TLRA=%uus start-attempted=%u started=%u stopped=%u restored=%u locked=%d\n",
        result, fp_haptic_state.probed, fp_haptic_state.period_us, fp_haptic_state.start_attempted, fp_haptic_state.started,
        fp_haptic_state.stopped, fp_haptic_state.restored, fp_haptic_state.failed);
    if (result == FUXI_HAPTIC_FINITE_UNVERIFIED)
        printf("FUXI PMIC: haptic probe accepted HAP525/owner/fault/idle/follow; PATTERN1 autonomous stop unverified; writes=0\n");
}

void fuxi_pmic_init(void)
{
    uint32_t mode = 0;
    uint32_t haptic = 0;
    uint32_t torch_audit = 0;
    DTEntry chosen;
    void *property = NULL;
    unsigned bytes = 0;
    if (!PE_parse_boot_argn("fuxipmic", &mode, sizeof(mode)) &&
        SecureDTLookupEntry(NULL, "/chosen", &chosen) == kSuccess &&
        SecureDTGetProperty(chosen, "fuxi,pmic", &property, &bytes) == kSuccess && bytes == sizeof(mode)) {
        memcpy(&mode, property, sizeof(mode));
    }
    if (!PE_parse_boot_argn("fuxihaptic", &haptic, sizeof(haptic)) &&
        SecureDTLookupEntry(NULL, "/chosen", &chosen) == kSuccess &&
        SecureDTGetProperty(chosen, "fuxi,haptic", &property, &bytes) == kSuccess && bytes == sizeof(haptic)) {
        memcpy(&haptic, property, sizeof(haptic));
    }
    if (SecureDTLookupEntry(NULL, "/chosen", &chosen) == kSuccess &&
        SecureDTGetProperty(chosen, "fuxi,torchaudit", &property, &bytes) == kSuccess && bytes == sizeof(torch_audit)) {
        memcpy(&torch_audit, property, sizeof(torch_audit));
    }
    fp_torch_audit_enabled = torch_audit == 1 && mode == 3;
    if ((!mode && !haptic) || __atomic_exchange_n(&fp_started, 1u, __ATOMIC_ACQ_REL)) return;
    if (mode > 3 || haptic > 5) {
        printf("FUXI PMIC: unsupported mode=%u haptic=%u; hardware untouched\n", mode, haptic);
        return;
    }
    if (fp_map(&fp_core) != KERN_SUCCESS || fp_map(&fp_config) != KERN_SUCCESS ||
        fp_map(&fp_observer) != KERN_SUCCESS) {
        printf("FUXI PMIC: mapping failed\n");
        return;
    }
    uint32_t version = fp_read32(&fp_core, 0);
    fp_apids = fp_read32(&fp_core, 4) & 0x7ffu;
    printf("FUXI PMIC: arbiter=%#x APIDs=%u mode=%u haptic=%u\n", version, fp_apids, mode, haptic);
    if (version < 0x70000000u || version >= 0x80000000u || !fp_apids || fp_apids > 1024) return;
    fp_survey(1, 0xee00, "flash");
    fp_survey(7, 0xf000, "haptic-config");
    fp_survey(7, 0xf100, "haptic-pattern");
    fp_survey(7, 0xf200, "haptic-boost");
    fp_haptic_fifo_survey(mode);
    if (haptic == 1) fp_haptic_diagnostic();
    if (haptic == 5) {
        if (SecureDTLookupEntry(NULL, "/chosen", &chosen) != kSuccess ||
            SecureDTGetProperty(chosen, "fuxi,haptic-rtp", &property, &bytes) != kSuccess ||
            bytes != FUXI_RTP_BYTES) haptic = 0;
        else {
            fp_rtp_wave = property;
            unsigned hal = 0;
            fp_rtp_pcm_enabled = PE_parse_boot_argn("fuxihalhaptic", &hal, sizeof(hal)) && hal == 3;
            if (SecureDTGetProperty(chosen, "fuxi,haptic-rtp-vmax", &property, &bytes) == kSuccess &&
                bytes == sizeof(fp_rtp_vmax)) {
                memcpy(&fp_rtp_vmax, property, sizeof(fp_rtp_vmax));
                if (fp_rtp_vmax > FUXI_RTP_TEST_VMAX_LIMIT) fp_rtp_vmax = 0;
            }
        }
    }
    if (haptic >= 2 && haptic <= 5) {
        fp_vibrate_transient = haptic == 4;
        fp_vibrate_selftest = haptic == 3;
        fp_vibrate_runtime_start();
    }
    if (mode == 2) fp_flash_pulse();
    if (mode == 3) fp_torch_start();
}
#endif
