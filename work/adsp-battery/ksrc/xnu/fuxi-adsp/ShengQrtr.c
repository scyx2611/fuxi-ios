/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* The IPC router link to the audio DSP (GLINK channel "IPCRTR", which the DSP opens) and the service locator on it. The DSP's protection
 * domains (audio, charger, sensors) ask the application processor's locator which domains provide a service before they start; the
 * battery manager runs in the charger domain. Started with adsp=2. Everything runs in the DSP thread's callbacks: each packet is
 * handled at once and any answer is queued with sheng_adsp_send, so nothing here waits. */
#include <stddef.h>
#include <stdint.h>
#include <kern/locks.h>
#include "QcomQrtr.h"
#include "ShengAdsp.h"
#include "ShengQrtr.h"

/* [0] stage (1 channel registered, 2 link up, 3 the DSP's hello, negative: registration failed), [1] packets in, [2] data packets in,
 * [3] bad packets, [4] packets out, [5] sends refused, [6] locator requests, [7] locator answers, [8] the DSP's node, [9] services the
 * DSP announced, [10] link ups, [11] link downs, [12] data for ports we do not have, [13] the last locator request's service name
 * length | its first byte << 8 */
volatile int32_t native_qrtr_stats[16];
/* the services the DSP announced, as service:instance in hex */
char native_qrtr_text[256];

enum {kLocalNode = 1, kLocatorPort = 1, kClientPortBase = 0x100, kMaxClients = 12, kMaxWatchers = 4};

static const char *const kAudio[] = {"avs/audio", NULL};
static const char *const kGps[] = {"gps/gps_service", NULL};
static const QcomPdDomain kDomains[] = {
    {"msm/adsp/audio_pd", 74, kAudio},   {"msm/adsp/root_pd", 74, NULL}, {"msm/adsp/charger_pd", 74, NULL},
    {"msm/adsp/sensor_pd", 74, NULL},    {"msm/cdsp/root_pd", 76, NULL}, {"msm/modem/root_pd", 180, kGps},
};

static QcomQrtr gQrtr;
static ShengAdspChannel *gChannel;
static int gQrtrReady;
static LCK_GRP_DECLARE(gQrtrGrp, "fuxi_qrtr");
static LCK_MTX_DECLARE(gQrtrLock, &gQrtrGrp);

struct ShengQrtrPort {
    uint32_t port;
    ShengQrtrReceive receive;
    void *context;
};
static struct ShengQrtrPort gClientPorts[kMaxClients];
static uint32_t gClientCount;
struct ShengQrtrWatcher {
    ShengQrtrServerNotify notify;
    void *context;
};
static struct ShengQrtrWatcher gWatchers[kMaxWatchers];
static uint32_t gWatcherCount;
static int gServerDirty;

static void notifyWatchers(void)
{
    struct ShengQrtrWatcher snapshot[kMaxWatchers];
    uint32_t count = 0;
    lck_mtx_lock(&gQrtrLock);
    count = gWatcherCount;
    for (uint32_t i = 0; i < count; ++i) snapshot[i] = gWatchers[i];
    lck_mtx_unlock(&gQrtrLock);
    for (uint32_t i = 0; i < count; ++i) {
        if (snapshot[i].notify) snapshot[i].notify(snapshot[i].context);
    }
}

static void onServerChange(void *context)
{
    (void)context;
    gServerDirty = 1;
}

static void clientReceiveAdapter(void *context, QcomQrtr *q, uint32_t port, uint32_t node, uint32_t from,
                                 const uint8_t *data, uint32_t length)
{
    (void)q;
    (void)port;
    struct ShengQrtrPort *client = (struct ShengQrtrPort *)context;
    if (client && client->receive) client->receive(client->context, node, from, data, length);
}

static int send(void *context, const uint8_t *packet, uint32_t length)
{
    (void)context;
    return sheng_adsp_send(gChannel, packet, length) ? -1 : 0;
}

static void hex(char *out, unsigned *at, unsigned cap, uint32_t value)
{
    char digits[8];
    unsigned n = 0;
    do {digits[n++] = "0123456789abcdef"[value & 15]; value >>= 4;} while (value && n < 8);
    while (n && *at + 1 < cap) out[(*at)++] = digits[--n];
}

static void counters(void)
{
    native_qrtr_stats[1] = (int32_t)gQrtr.rx_packets;
    native_qrtr_stats[2] = (int32_t)gQrtr.rx_data;
    native_qrtr_stats[3] = (int32_t)gQrtr.rx_bad;
    native_qrtr_stats[4] = (int32_t)gQrtr.tx_packets;
    native_qrtr_stats[5] = (int32_t)gQrtr.tx_refused;
    native_qrtr_stats[8] = gQrtr.hello_received ? (int32_t)gQrtr.remote_node : -1;
    native_qrtr_stats[9] = (int32_t)gQrtr.remote_count;
    native_qrtr_stats[12] = (int32_t)gQrtr.rx_unrouted;
    if (gQrtr.hello_received && native_qrtr_stats[0] == 2) native_qrtr_stats[0] = 3;
    unsigned at = 0;
    const unsigned cap = sizeof native_qrtr_text;
    for (uint32_t i = 0; i < gQrtr.remote_count && at + 4 < cap; ++i) {
        if (at) native_qrtr_text[at++] = ' ';
        hex(native_qrtr_text, &at, cap, gQrtr.remote[i].service);
        if (at + 1 < cap) native_qrtr_text[at++] = ':';
        hex(native_qrtr_text, &at, cap, gQrtr.remote[i].instance);
    }
    native_qrtr_text[at] = 0;
}

static void locator(void *context, QcomQrtr *q, uint32_t port, uint32_t node, uint32_t from, const uint8_t *data, uint32_t length)
{
    (void)context;
    uint8_t answer[480];
    QcomQmiMessage m;
    uint32_t size = 0;
    native_qrtr_stats[6]++;
    if (!qcom_qmi_parse(data, length, &m)) {
        const uint8_t *name = qcom_qmi_tlv(&m, 0x01, &size);
        if (name) native_qrtr_stats[13] = (int32_t)(size | (size ? (uint32_t)name[0] << 8 : 0));
    }
    const uint32_t n = qcom_servloc_answer(kDomains, sizeof kDomains / sizeof kDomains[0], data, length, answer, sizeof answer);
    if (n && !qcom_qrtr_send_data(q, port, node, from, answer, n)) native_qrtr_stats[7]++;
}

static void linkState(void *context, int up)
{
    (void)context;
    int changed = 0;
    lck_mtx_lock(&gQrtrLock);
    if (up) {
        native_qrtr_stats[10]++;
        /* a fresh link: forget the old one's hello and services, keep ours */
        gQrtr.hello_sent = gQrtr.hello_received = 0;
        gQrtr.remote_count = 0;
        gQrtr.remote_node = QCOM_QRTR_NODE_BROADCAST;
        gQrtr.remote_generation++;
        native_qrtr_stats[0] = 2;
        qcom_qrtr_start(&gQrtr);
        changed = 1;
    } else {
        native_qrtr_stats[11]++;
        gQrtr.remote_count = 0;
        gQrtr.hello_received = 0;
        gQrtr.remote_generation++;
        native_qrtr_stats[0] = 1;
        changed = 1;
    }
    counters();
    lck_mtx_unlock(&gQrtrLock);
    if (changed) notifyWatchers();
}

static void linkReceive(void *context, const uint8_t *data, uint32_t length)
{
    (void)context;
    lck_mtx_lock(&gQrtrLock);
    gServerDirty = 0;
    qcom_qrtr_receive(&gQrtr, data, length);
    counters();
    const int dirty = gServerDirty;
    lck_mtx_unlock(&gQrtrLock);
    if (dirty) notifyWatchers();
}

ShengQrtrPort *sheng_qrtr_open_client(ShengQrtrReceive receive, void *context)
{
    if (!receive) return NULL;
    lck_mtx_lock(&gQrtrLock);
    if (!gQrtrReady || gClientCount >= kMaxClients) {
        lck_mtx_unlock(&gQrtrLock);
        return NULL;
    }
    struct ShengQrtrPort *slot = &gClientPorts[gClientCount];
    const uint32_t port = kClientPortBase + gClientCount;
    slot->port = port;
    slot->receive = receive;
    slot->context = context;
    if (qcom_qrtr_add_client(&gQrtr, port, clientReceiveAdapter, slot) != 0) {
        lck_mtx_unlock(&gQrtrLock);
        return NULL;
    }
    gClientCount++;
    lck_mtx_unlock(&gQrtrLock);
    return slot;
}

uint32_t sheng_qrtr_port_number(const ShengQrtrPort *port)
{
    return port ? port->port : 0;
}

int sheng_qrtr_send(ShengQrtrPort *port, uint32_t node, uint32_t dst_port, const void *data, uint32_t length)
{
    if (!port || !data || !length) return -1;
    lck_mtx_lock(&gQrtrLock);
    if (!gQrtrReady || !gChannel) {
        lck_mtx_unlock(&gQrtrLock);
        return -1;
    }
    const int rc = qcom_qrtr_send_data(&gQrtr, port->port, node, dst_port, (const uint8_t *)data, length);
    counters();
    lck_mtx_unlock(&gQrtrLock);
    return rc;
}

int sheng_qrtr_find_service(uint32_t service, uint32_t *node, uint32_t *port, uint32_t *instance, uint32_t *generation)
{
    lck_mtx_lock(&gQrtrLock);
    if (!gQrtrReady) {
        if (generation) *generation = 0;
        lck_mtx_unlock(&gQrtrLock);
        return 0;
    }
    if (generation) *generation = gQrtr.remote_generation;
    const int found = qcom_qrtr_find_remote_any(&gQrtr, service, node, port, instance) == 0;
    lck_mtx_unlock(&gQrtrLock);
    return found;
}

int sheng_qrtr_watch(ShengQrtrServerNotify notify, void *context)
{
    if (!notify) return -1;
    lck_mtx_lock(&gQrtrLock);
    if (gWatcherCount >= kMaxWatchers) {
        lck_mtx_unlock(&gQrtrLock);
        return -1;
    }
    gWatchers[gWatcherCount].notify = notify;
    gWatchers[gWatcherCount].context = context;
    gWatcherCount++;
    lck_mtx_unlock(&gQrtrLock);
    return 0;
}

void sheng_qrtr_start(void)
{
    static const ShengAdspClient client = {linkState, linkReceive};
    lck_mtx_lock(&gQrtrLock);
    qcom_qrtr_init(&gQrtr, kLocalNode, send, NULL);
    qcom_qrtr_add_service(&gQrtr, QCOM_SERVLOC_SERVICE, QCOM_SERVLOC_INSTANCE, kLocatorPort, locator, NULL);
    qcom_qrtr_set_server_notify(&gQrtr, onServerChange, NULL);
    gChannel = sheng_adsp_channel("IPCRTR", 2048, 12, 0, &client, NULL);
    gQrtrReady = gChannel != NULL;
    native_qrtr_stats[0] = gChannel ? 1 : -1;
    lck_mtx_unlock(&gQrtrLock);
}
