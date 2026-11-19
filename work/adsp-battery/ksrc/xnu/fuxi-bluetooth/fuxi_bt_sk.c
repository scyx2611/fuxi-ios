/* SPDX-License-Identifier: APSL-2.0
 * Copyright (c) 2026 daimile2 and contributors.
 * 2026-11-18 依 UniXNU/QcaBluetooth src/qbt_transport.c 適配 fuxi 與 XNU 8796。
 * 本檔受 Apple Public Source License 2.0 約束：https://opensource.apple.com/apsl/
 * 散佈時依該授權以現狀提供，詳見授權條款。
 */
#include <pexpert/arm64/board_config.h>
#if defined(FUXI)
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/codesign.h>
#include <sys/kauth.h>
#include <sys/kern_control.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/termios.h>
#include <kern/locks.h>
#include <skywalk/os_skywalk.h>
#include <uuid/uuid.h>
#include "fuxi_bt.h"
#include "fuxi_bt_sk.h"

#define BT_SK_NAME "com.apple.uart.sk.bluetooth"
#define BT_SK_FIFO 8192u
#define BT_SK_SET_SPEC 0x800c5414u
#define BT_SK_GET_SPEC 0x400c5413u
#define BT_SK_GET_NEXUS 0x40105412u
#define BT_SK_SET_TERMIOS 0x80487414u
#define BT_SK_GET_TERMIOS 0x40487413u

struct bt_sk_termios64 {
    uint64_t iflag, oflag, cflag, lflag;
    uint8_t cc[20];
    uint32_t pad;
    uint64_t ispeed, ospeed;
};
_Static_assert(sizeof(struct bt_sk_termios64) == 72, "bluetoothd termios ABI");

struct bt_sk_fifo {
    uint8_t data[BT_SK_FIFO];
    uint32_t head;
    uint32_t len;
};

struct bt_sk_instance {
    nexus_controller_t controller;
    uuid_t provider;
    uuid_t uuid;
    kern_pbufpool_t pool;
};

static LCK_GRP_DECLARE(bt_sk_group, "fuxi.bluetooth.sk");
static LCK_MTX_DECLARE(bt_sk_lock, &bt_sk_group);
static struct {
    kern_ctl_ref ctl;
    struct bt_sk_instance instance;
    struct bt_sk_termios64 termios;
    struct bt_sk_fifo rx, tx;
    struct fuxi_bt_sk_stats stats;
    kern_channel_t channel;
    kern_channel_ring_t rx_ring, tx_ring;
    uint32_t spec[3];
    uint32_t unit;
    pid_t pid;
    uint64_t proc_id;
    uintptr_t generation;
    unsigned notifying;
    bool creating, closing, probe;
    bool rx_accepting;   /* 目前這個 H4 封包交給 RX 環；只在封包開頭改變 */
    void (*wake)(void);
    char firmware[64];
} bt_sk = {.stats.boot = ENXIO};

static void
bt_sk_fifo_put(struct bt_sk_fifo *fifo, const uint8_t *bytes, uint32_t count)
{
    uint32_t at = (fifo->head + fifo->len) % BT_SK_FIFO;
    uint32_t first = MIN(count, BT_SK_FIFO - at);
    memcpy(fifo->data + at, bytes, first);
    memcpy(fifo->data, bytes + first, count - first);
    fifo->len += count;
}

static void
bt_sk_fifo_peek(const struct bt_sk_fifo *fifo, uint8_t *bytes, uint32_t count)
{
    uint32_t first = MIN(count, BT_SK_FIFO - fifo->head);
    memcpy(bytes, fifo->data + fifo->head, first);
    memcpy(bytes + first, fifo->data, count - first);
}

static void
bt_sk_fifo_advance(struct bt_sk_fifo *fifo, uint32_t count)
{
    fifo->head = (fifo->head + count) % BT_SK_FIFO;
    fifo->len -= count;
}

/* 解鎖後通知 ring，避免與 Skywalk 的回呼鎖反序。關閉端先停止新通知再等既有通知完成。 */
static void
bt_sk_notify_and_unlock(kern_channel_ring_t ring)
{
    if (!ring) {
        lck_mtx_unlock(&bt_sk_lock);
        return;
    }
    ++bt_sk.notifying;
    lck_mtx_unlock(&bt_sk_lock);
    (void)kern_channel_notify(ring, 0);
    lck_mtx_lock(&bt_sk_lock);
    if (--bt_sk.notifying == 0) {
        wakeup(&bt_sk.notifying);
    }
    lck_mtx_unlock(&bt_sk_lock);
}

size_t
fuxi_bt_sk_receive(const uint8_t *data, size_t size, bool start)
{
    lck_mtx_lock(&bt_sk_lock);
    if (start) {
        bt_sk.rx_accepting = bt_sk.stats.rings && !bt_sk.closing && !bt_sk.stats.boot;
    }
    if (!bt_sk.rx_accepting || !bt_sk.stats.rings || bt_sk.closing || bt_sk.stats.boot) {
        bt_sk.stats.rx_dropped += size;
        lck_mtx_unlock(&bt_sk_lock);
        return size;
    }
    uint32_t count = (uint32_t)MIN(size, BT_SK_FIFO - bt_sk.rx.len);
    if (count != 0) {
        bt_sk_fifo_put(&bt_sk.rx, data, count);
        bt_sk.stats.rx_bytes += count;
    }
    bt_sk_notify_and_unlock(count ? bt_sk.rx_ring : NULL);
    return count;
}

size_t
fuxi_bt_sk_tx_take(uint8_t *data, size_t capacity)
{
    lck_mtx_lock(&bt_sk_lock);
    uint32_t count = (uint32_t)MIN(capacity, bt_sk.tx.len);
    if (!bt_sk.stats.rings || bt_sk.closing || bt_sk.stats.boot || count == 0) {
        lck_mtx_unlock(&bt_sk_lock);
        return 0;
    }
    bt_sk_fifo_peek(&bt_sk.tx, data, count);
    bt_sk_fifo_advance(&bt_sk.tx, count);
    bt_sk.stats.tx_bytes += count;
    bt_sk_notify_and_unlock(bt_sk.tx_ring);
    return count;
}

void
fuxi_bt_sk_get_stats(struct fuxi_bt_sk_stats *stats)
{
    lck_mtx_lock(&bt_sk_lock);
    *stats = bt_sk.stats;
    stats->rx_len = bt_sk.rx.len;
    stats->tx_len = bt_sk.tx.len;
    lck_mtx_unlock(&bt_sk_lock);
}

uint32_t
fuxi_bt_sk_connects(void)
{
    /* 常駐迴圈每步都要看，不拿鎖：connects 只遞增，讀到舊值下一步就會補上 */
    return __atomic_load_n(&bt_sk.stats.connects, __ATOMIC_RELAXED);
}

static errno_t
bt_sk_pre_connect(kern_nexus_provider_t provider __unused, proc_t proc,
    kern_nexus_t nexus, nexus_port_t port, kern_channel_t channel, void **ctx __unused)
{
    lck_mtx_lock(&bt_sk_lock);
    bool owns_instance = (uintptr_t)kern_nexus_get_context(nexus) == bt_sk.generation;
    errno_t error = EACCES;
    if (bt_sk.stats.connected && !bt_sk.closing && bt_sk.instance.controller && owns_instance &&
        port == NEXUS_PORT_KERNEL_PIPE_CLIENT && proc_uniqueid(proc) == bt_sk.proc_id) {
        error = bt_sk.channel ? EBUSY : bt_sk.stats.boot;
        if (!error) {
            bt_sk.channel = channel;
        }
    }
    lck_mtx_unlock(&bt_sk_lock);
    return error;
}

static errno_t
bt_sk_connected(kern_nexus_provider_t provider __unused, kern_nexus_t nexus __unused,
    kern_channel_t channel)
{
    lck_mtx_lock(&bt_sk_lock);
    bool ready = bt_sk.channel == channel && !bt_sk.closing && bt_sk.rx_ring && bt_sk.tx_ring;
    if (ready) {
        /* 新通道從下一個 H4 封包開頭開始收 */
        bt_sk.rx_accepting = false;
        bt_sk.stats.rings = true;
        ++bt_sk.stats.channels;
    }
    lck_mtx_unlock(&bt_sk_lock);
    if (ready) {
        printf("FUXI BT SK: channel rings ready\n");
        bt_sk.wake();
    }
    return ready ? 0 : ENXIO;
}

static void
bt_sk_pre_disconnect(kern_nexus_provider_t provider __unused, kern_nexus_t nexus __unused,
    kern_channel_t channel)
{
    lck_mtx_lock(&bt_sk_lock);
    if (bt_sk.channel == channel) {
        bt_sk.stats.rings = false;
        bt_sk.rx_accepting = false;
        while (bt_sk.notifying) {
            (void)msleep(&bt_sk.notifying, &bt_sk_lock, PZERO, "fuxibtring", NULL);
        }
    }
    lck_mtx_unlock(&bt_sk_lock);
}

static void
bt_sk_disconnected(kern_nexus_provider_t provider __unused, kern_nexus_t nexus __unused,
    kern_channel_t channel)
{
    lck_mtx_lock(&bt_sk_lock);
    if (bt_sk.channel == channel) {
        bt_sk.channel = NULL;
        bt_sk.rx.head = bt_sk.rx.len = 0;
        bt_sk.tx.head = bt_sk.tx.len = 0;
    }
    lck_mtx_unlock(&bt_sk_lock);
}

static errno_t
bt_sk_ring_init(kern_nexus_provider_t provider __unused, kern_nexus_t nexus __unused,
    kern_channel_t channel, kern_channel_ring_t ring, boolean_t is_tx, void **ctx __unused)
{
    lck_mtx_lock(&bt_sk_lock);
    kern_channel_ring_t *target = is_tx ? &bt_sk.tx_ring : &bt_sk.rx_ring;
    bool available = bt_sk.channel == channel && !bt_sk.closing && !*target;
    if (available) {
        *target = ring;
    }
    lck_mtx_unlock(&bt_sk_lock);
    return available ? 0 : EBUSY;
}

static void
bt_sk_ring_fini(kern_nexus_provider_t provider __unused, kern_nexus_t nexus __unused,
    kern_channel_ring_t ring)
{
    lck_mtx_lock(&bt_sk_lock);
    if (bt_sk.tx_ring == ring) {
        bt_sk.tx_ring = NULL;
    }
    if (bt_sk.rx_ring == ring) {
        bt_sk.rx_ring = NULL;
    }
    lck_mtx_unlock(&bt_sk_lock);
}

static errno_t
bt_sk_sync_tx(kern_nexus_provider_t provider __unused, kern_nexus_t nexus __unused,
    kern_channel_ring_t ring, uint32_t flags __unused)
{
    kern_channel_slot_t last = NULL;
    errno_t error = 0;
    lck_mtx_lock(&bt_sk_lock);
    if (!bt_sk.stats.rings || bt_sk.closing || bt_sk.tx_ring != ring || bt_sk.stats.boot) {
        lck_mtx_unlock(&bt_sk_lock);
        return ENXIO;
    }
    for (kern_channel_slot_t slot = kern_channel_get_next_slot(ring, NULL, NULL); slot;
        slot = kern_channel_get_next_slot(ring, slot, NULL)) {
        kern_packet_t packet = kern_channel_slot_get_packet(ring, slot);
        if (packet) {
            kern_buflet_t buf = kern_packet_get_next_buflet(packet, NULL);
            uint32_t count = kern_packet_get_data_length(packet);
            const uint8_t *bytes = buf ? kern_buflet_get_data_address(buf) : NULL;
            uint32_t offset = buf ? kern_buflet_get_data_offset(buf) : 0;
            uint32_t limit = buf ? kern_buflet_get_data_limit(buf) : 0;
            if (!bytes || count > bt_sk.spec[0] || offset > limit || count > limit - offset ||
                count > kern_buflet_get_data_length(buf)) {
                error = EPROTO;
                break;
            }
            if (count > BT_SK_FIFO - bt_sk.tx.len) {
                break;
            }
            bt_sk_fifo_put(&bt_sk.tx, bytes + offset, count);
        }
        last = slot;
        ++bt_sk.stats.tx_syncs;
    }
    if (last) {
        kern_channel_advance_slot(ring, last);
        (void)kern_channel_reclaim(ring);
    }
    lck_mtx_unlock(&bt_sk_lock);
    if (last) {
        bt_sk.wake();
    }
    return error;
}

static errno_t
bt_sk_sync_rx(kern_nexus_provider_t provider __unused, kern_nexus_t nexus,
    kern_channel_ring_t ring, uint32_t flags __unused)
{
    kern_pbufpool_t pool = NULL;
    errno_t error = kern_nexus_get_pbufpool(nexus, NULL, &pool);
    if (error || !pool) {
        return error ? error : ENXIO;
    }
    kern_channel_slot_t last = NULL;
    lck_mtx_lock(&bt_sk_lock);
    if (!bt_sk.stats.rings || bt_sk.closing || bt_sk.rx_ring != ring || bt_sk.stats.boot) {
        lck_mtx_unlock(&bt_sk_lock);
        return ENXIO;
    }
    (void)kern_channel_reclaim(ring);
    for (kern_channel_slot_t slot = kern_channel_get_next_slot(ring, NULL, NULL);
        slot && bt_sk.rx.len; slot = kern_channel_get_next_slot(ring, slot, NULL)) {
        kern_packet_t packet = 0;
        if (kern_pbufpool_alloc_nosleep(pool, 1, &packet)) {
            break;
        }
        kern_buflet_t buf = kern_packet_get_next_buflet(packet, NULL);
        uint32_t count = MIN(bt_sk.rx.len, bt_sk.spec[0]);
        uint8_t *bytes = buf ? kern_buflet_get_data_address(buf) : NULL;
        if (!bytes || count > kern_buflet_get_data_limit(buf)) {
            kern_pbufpool_free(pool, packet);
            error = EIO;
            break;
        }
        bt_sk_fifo_peek(&bt_sk.rx, bytes, count);
        kern_packet_clear_flow_uuid(packet);
        error = kern_buflet_set_data_offset(buf, 0);
        if (!error) {
            error = kern_buflet_set_data_length(buf, (uint16_t)count);
        }
        if (!error) {
            error = kern_packet_finalize(packet);
        }
        if (!error) {
            error = kern_channel_slot_attach_packet(ring, slot, packet);
        }
        if (error) {
            kern_pbufpool_free(pool, packet);
            break;
        }
        bt_sk_fifo_advance(&bt_sk.rx, count);
        last = slot;
        ++bt_sk.stats.rx_syncs;
    }
    if (last) {
        kern_channel_advance_slot(ring, last);
    }
    lck_mtx_unlock(&bt_sk_lock);
    if (last) {
        bt_sk.wake();
    }
    return error;
}

static void
bt_sk_destroy(const struct bt_sk_instance *instance)
{
    if (!uuid_is_null(instance->uuid)) {
        (void)kern_nexus_controller_free_provider_instance(instance->controller, instance->uuid);
    }
    if (instance->pool) {
        kern_pbufpool_destroy(instance->pool);
    }
    if (!uuid_is_null(instance->provider)) {
        (void)kern_nexus_controller_deregister_provider(instance->controller, instance->provider);
    }
    if (instance->controller) {
        kern_nexus_controller_destroy(instance->controller);
    }
}

static errno_t
bt_sk_create(const uint32_t spec[3], pid_t pid, uintptr_t generation, struct bt_sk_instance *instance)
{
    nexus_attr_t attr = NULL;
    uuid_t domain;
    errno_t error = kern_nexus_controller_create(&instance->controller);
    if (!error) {
        error = kern_nexus_get_default_domain_provider(NEXUS_TYPE_KERNEL_PIPE, &domain);
    }
    if (!error) {
        error = kern_nexus_attr_create(&attr);
    }
    if (!error) {
        error = kern_nexus_attr_set(attr, NEXUS_ATTR_SLOT_BUF_SIZE, spec[0]);
    }
    if (!error) {
        error = kern_nexus_attr_set(attr, NEXUS_ATTR_TX_SLOTS, spec[1]);
    }
    if (!error) {
        error = kern_nexus_attr_set(attr, NEXUS_ATTR_RX_SLOTS, spec[2]);
    }
    if (!error) {
        const struct kern_nexus_provider_init init = {
            .nxpi_version = KERN_NEXUS_PROVIDER_CURRENT_VERSION,
            .nxpi_flags = NXPIF_VIRTUAL_DEVICE,
            .nxpi_pre_connect = bt_sk_pre_connect,
            .nxpi_connected = bt_sk_connected,
            .nxpi_pre_disconnect = bt_sk_pre_disconnect,
            .nxpi_disconnected = bt_sk_disconnected,
            .nxpi_ring_init = bt_sk_ring_init,
            .nxpi_ring_fini = bt_sk_ring_fini,
            .nxpi_sync_tx = bt_sk_sync_tx,
            .nxpi_sync_rx = bt_sk_sync_rx,
        };
        error = kern_nexus_controller_register_provider(instance->controller, domain,
            (const uint8_t *)BT_SK_NAME, &init, sizeof(init), attr, &instance->provider);
    }
    if (attr) {
        kern_nexus_attr_destroy(attr);
    }
    if (!error) {
        const struct kern_pbufpool_init pool_init = {
            .kbi_name = "com.apple.kpipe.uart.bluetooth",
            .kbi_version = KERN_PBUFPOOL_CURRENT_VERSION,
            .kbi_flags = KBIF_VIRTUAL_DEVICE | KBIF_QUANTUM,
            .kbi_packets = (spec[1] + spec[2]) * 2,
            .kbi_bufsize = spec[0],
            .kbi_max_frags = 1,
        };
        error = kern_pbufpool_create(&pool_init, &instance->pool, NULL);
    }
    if (!error) {
        const struct kern_nexus_init init = {
            .nxi_version = KERN_NEXUS_CURRENT_VERSION,
            .nxi_tx_pbufpool = instance->pool,
        };
        error = kern_nexus_controller_alloc_provider_instance(instance->controller,
            instance->provider, (void *)generation, NULL, &instance->uuid, &init);
    }
    if (!error) {
        nexus_port_t port = NEXUS_PORT_KERNEL_PIPE_CLIENT;
        error = kern_nexus_controller_bind_provider_instance(instance->controller,
            instance->uuid, &port, pid, NULL, NULL, 0, NEXUS_BIND_PID);
    }
    if (error) {
        bt_sk_destroy(instance);
    }
    return error;
}

static errno_t
bt_sk_connect(kern_ctl_ref ref __unused, struct sockaddr_ctl *addr, void **unitinfo)
{
    proc_t proc = current_proc();
    if (!kauth_cred_issuser(kauth_cred_get())) {
        /*
         * 20H392 的 bluetoothd 不是以 root 執行；註冊時若帶 CTL_FLAG_PRIVILEGED，kern_control 會在
         * 呼叫本函式之前就回 EPERM（第三趟沒有任何 connect 紀錄，堆疊初始化以 reason 1201 中止）。
         * 權限改在這裡判斷：root、原生模式的平台 bluetoothd，或探測模式的平台 SpringBoard。
         */
        char name[32] = {0};
        proc_selfname(name, sizeof(name));
        const char *allowed = bt_sk.probe ? "SpringBoard" : "bluetoothd";
        if (!csproc_get_platform_binary(proc) || strcmp(name, allowed)) {
            return EPERM;
        }
    }
    lck_mtx_lock(&bt_sk_lock);
    errno_t error = bt_sk.stats.connected ? EBUSY : bt_sk.stats.boot;
    if (!error) {
        bt_sk.stats.connected = true;
        ++bt_sk.stats.connects;
        ++bt_sk.generation;
        bt_sk.unit = addr->sc_unit;
        bt_sk.pid = proc_pid(proc);
        bt_sk.proc_id = proc_uniqueid(proc);
        bt_sk.termios = (struct bt_sk_termios64){
            .cflag = CS8 | CREAD | CLOCAL | CRTSCTS,
            .ispeed = FUXI_BT_BOOT_BAUD, .ospeed = FUXI_BT_BOOT_BAUD,
        };
        bt_sk.spec[0] = 1024;
        bt_sk.spec[1] = bt_sk.spec[2] = 63;
        *unitinfo = &bt_sk;
    }
    lck_mtx_unlock(&bt_sk_lock);
    if (!error) {
        printf("FUXI BT SK: connect unit=%u pid=%d\n", addr->sc_unit, proc_pid(proc));
    }
    return error;
}

static errno_t
bt_sk_disconnect(kern_ctl_ref ref __unused, u_int32_t unit, void *unitinfo __unused)
{
    lck_mtx_lock(&bt_sk_lock);
    if (!bt_sk.stats.connected || bt_sk.unit != unit) {
        lck_mtx_unlock(&bt_sk_lock);
        return 0;
    }
    bt_sk.closing = true;
    bt_sk.stats.rings = false;
    bt_sk.rx_accepting = false;
    while (bt_sk.creating) {
        (void)msleep(&bt_sk.creating, &bt_sk_lock, PZERO, "fuxibtcreate", NULL);
    }
    while (bt_sk.notifying) {
        (void)msleep(&bt_sk.notifying, &bt_sk_lock, PZERO, "fuxibtring", NULL);
    }
    struct bt_sk_instance instance = bt_sk.instance;
    const pid_t owner_pid = bt_sk.pid;
    const uint64_t owner_id = bt_sk.proc_id;
    memset(&bt_sk.instance, 0, sizeof(bt_sk.instance));
    lck_mtx_unlock(&bt_sk_lock);
    /* XNU 8796 的 exit_with_reason 先保存 wait status，再關閉檔案描述符。
     * 呼叫者可能是核心的清理執行緒，因此只採用已核對身分的 owner 狀態。 */
    proc_t caller = current_proc();
    const bool owner_exiting = proc_uniqueid(caller) == owner_id && proc_exiting(caller);
    const int wait_status = owner_exiting ? proc_exitstatus(caller) : -1;
    bt_sk_destroy(&instance);

    lck_mtx_lock(&bt_sk_lock);
    bt_sk.channel = NULL;
    bt_sk.rx_ring = bt_sk.tx_ring = NULL;
    bt_sk.rx.head = bt_sk.rx.len = 0;
    bt_sk.tx.head = bt_sk.tx.len = 0;
    bt_sk.stats.connected = bt_sk.closing = false;
    lck_mtx_unlock(&bt_sk_lock);
    printf("FUXI BT SK: disconnect unit=%u owner=%d caller=%d owner_exiting=%u wait_status=%d\n",
        unit, owner_pid, proc_pid(caller), (unsigned)owner_exiting, wait_status);
    return 0;
}

/* 以下兩個 socket options 會在可睡眠的 syscall context 操作 Skywalk。 */
static errno_t
bt_sk_setopt(kern_ctl_ref ref __unused, u_int32_t unit, void *unitinfo __unused,
    int option, void *data, size_t len)
{
    lck_mtx_lock(&bt_sk_lock);
    if (!bt_sk.stats.connected || bt_sk.closing || bt_sk.unit != unit ||
        proc_uniqueid(current_proc()) != bt_sk.proc_id) {
        lck_mtx_unlock(&bt_sk_lock);
        return ENOTCONN;
    }
    if ((uint32_t)option == BT_SK_SET_TERMIOS) {
        if (len != sizeof(bt_sk.termios)) {
            lck_mtx_unlock(&bt_sk_lock);
            return EINVAL;
        }
        struct bt_sk_termios64 termios;
        memcpy(&termios, data, sizeof(termios));
        if ((termios.cflag & (CSIZE | CSTOPB | PARENB)) != CS8) {
            lck_mtx_unlock(&bt_sk_lock);
            return EINVAL;
        }
        /* bluetoothd 設定的是虛擬端。實體 UART 速率由已驗證的控制器初始化設定。 */
        bt_sk.termios = termios;
        lck_mtx_unlock(&bt_sk_lock);
        printf("FUXI BT SK: termios speed=%llu/%llu\n", termios.ispeed, termios.ospeed);
        return 0;
    }
    if ((uint32_t)option != BT_SK_SET_SPEC) {
        lck_mtx_unlock(&bt_sk_lock);
        return ENOPROTOOPT;
    }
    if (len != sizeof(bt_sk.spec)) {
        lck_mtx_unlock(&bt_sk_lock);
        return EINVAL;
    }
    uint32_t spec[3];
    memcpy(spec, data, sizeof(spec));
    if ((spec[0] & (spec[0] - 1)) || spec[0] < 1024 || spec[0] > 4096 ||
        spec[1] < 2 || spec[1] > 127 || spec[2] < 2 || spec[2] > 127) {
        lck_mtx_unlock(&bt_sk_lock);
        return EINVAL;
    }
    if (bt_sk.instance.controller || bt_sk.creating || bt_sk.stats.boot) {
        errno_t error = EBUSY;
        if (bt_sk.instance.controller && memcmp(spec, bt_sk.spec, sizeof(spec)) == 0) {
            error = 0;
        }
        lck_mtx_unlock(&bt_sk_lock);
        return error;
    }
    bt_sk.creating = true;
    pid_t pid = bt_sk.pid;
    uintptr_t generation = bt_sk.generation;
    lck_mtx_unlock(&bt_sk_lock);

    struct bt_sk_instance instance = {0};
    errno_t error = bt_sk_create(spec, pid, generation, &instance);
    lck_mtx_lock(&bt_sk_lock);
    if (!error) {
        bt_sk.instance = instance;
        memcpy(bt_sk.spec, spec, sizeof(spec));
    }
    bt_sk.creating = false;
    wakeup(&bt_sk.creating);
    lck_mtx_unlock(&bt_sk_lock);
    printf("FUXI BT SK: channel spec buf=%u slots=%u/%u pid=%d error=%d\n",
        spec[0], spec[1], spec[2], pid, error);
    return error;
}

static errno_t
bt_sk_getopt(kern_ctl_ref ref __unused, u_int32_t unit, void *unitinfo __unused,
    int option, void *data, size_t *len)
{
    lck_mtx_lock(&bt_sk_lock);
    const void *value = NULL;
    size_t size = 0;
    errno_t error = 0;
    if (!bt_sk.stats.connected || bt_sk.closing || bt_sk.unit != unit ||
        proc_uniqueid(current_proc()) != bt_sk.proc_id) {
        error = ENOTCONN;
    } else {
        switch ((uint32_t)option) {
        case BT_SK_GET_SPEC:
            value = bt_sk.spec;
            size = sizeof(bt_sk.spec);
            break;
        case BT_SK_GET_NEXUS:
            if (!bt_sk.instance.controller) {
                error = ENXIO;
                break;
            }
            value = bt_sk.instance.uuid;
            size = sizeof(uuid_t);
            break;
        case BT_SK_GET_TERMIOS:
            value = &bt_sk.termios;
            size = sizeof(bt_sk.termios);
            break;
        default:
            error = ENOPROTOOPT;
            break;
        }
    }
    if (!error && data && *len < size) {
        error = EINVAL;
    }
    if (!error) {
        if (data) {
            memcpy(data, value, size);
        }
        *len = size;
    }
    lck_mtx_unlock(&bt_sk_lock);
    return error;
}

/* BlueTool 讀取既有 qbt sysctl ABI。常駐 worker 是唯一能改變控制器狀態的執行緒。 */
static int
bt_sk_boot_sysctl SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
    const struct timespec limit = {30, 0};
    int error = 0, value;
    lck_mtx_lock(&bt_sk_lock);
    if (req->oldptr && bt_sk.stats.boot == EINPROGRESS) {
        struct timespec timeout = limit;
        error = msleep(&bt_sk.stats.boot, &bt_sk_lock, PZERO | PCATCH, "fuxibtboot", &timeout);
    }
    value = bt_sk.stats.boot;
    lck_mtx_unlock(&bt_sk_lock);
    return error ? error : SYSCTL_OUT(req, &value, sizeof(value));
}
SYSCTL_PROC(_kern, OID_AUTO, qbt_boot, CTLTYPE_INT | CTLFLAG_RD | CTLFLAG_LOCKED,
    NULL, 0, bt_sk_boot_sysctl, "I", "WCN7850 bring-up result");
SYSCTL_PROC(_kern, OID_AUTO, qbt_power_on, CTLTYPE_INT | CTLFLAG_RD | CTLFLAG_LOCKED,
    NULL, 0, bt_sk_boot_sysctl, "I", "WCN7850 running state");
SYSCTL_STRING(_kern, OID_AUTO, qbt_firmware, CTLFLAG_RD, bt_sk.firmware,
    sizeof(bt_sk.firmware), "WCN7850 firmware build label");

void
fuxi_bt_sk_start(void (*wake)(void), bool probe)
{
    lck_mtx_lock(&bt_sk_lock);
    bt_sk.wake = wake;
    bt_sk.probe = probe;
    bt_sk.stats.boot = EINPROGRESS;
    lck_mtx_unlock(&bt_sk_lock);
}

int
fuxi_bt_sk_boot_result(int error, const char *firmware)
{
    if (!error) {
        struct kern_ctl_reg reg = {
            .ctl_name = BT_SK_NAME,
            /* 不帶 CTL_FLAG_PRIVILEGED：連線權限由 bt_sk_connect 依行程判斷。 */
            .ctl_flags = CTL_FLAG_REG_SOCK_STREAM,
            .ctl_connect = bt_sk_connect,
            .ctl_disconnect = bt_sk_disconnect,
            .ctl_setopt = bt_sk_setopt,
            .ctl_getopt = bt_sk_getopt,
        };
        error = ctl_register(&reg, &bt_sk.ctl);
        printf("FUXI BT SK: control register error=%d\n", error);
    }
    lck_mtx_lock(&bt_sk_lock);
    if (!error && firmware) {
        strlcpy(bt_sk.firmware, firmware, sizeof(bt_sk.firmware));
    }
    bt_sk.stats.boot = error;
    wakeup(&bt_sk.stats.boot);
    lck_mtx_unlock(&bt_sk_lock);
    return error;
}
#endif
