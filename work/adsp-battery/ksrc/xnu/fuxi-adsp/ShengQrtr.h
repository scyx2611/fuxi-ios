/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Client ports and remote service lookup on the ADSP IPC router (ShengQrtr.c). */
#ifndef SHENG_QRTR_H
#define SHENG_QRTR_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct ShengQrtrPort ShengQrtrPort;
typedef void (*ShengQrtrReceive)(void *context, uint32_t node, uint32_t from_port, const uint8_t *data, uint32_t length);
typedef void (*ShengQrtrServerNotify)(void *context);

/* Opens a local client-only QRTR port. Safe to call after sheng_qrtr_start(). Returns NULL if full or not started. */
ShengQrtrPort *sheng_qrtr_open_client(ShengQrtrReceive receive, void *context);
/* Returns the allocated local port number (0 on NULL). */
uint32_t sheng_qrtr_port_number(const ShengQrtrPort *port);
/* Sends a QRTR DATA packet from this client port to node:dst_port. Returns 0 when queued. */
int sheng_qrtr_send(ShengQrtrPort *port, uint32_t node, uint32_t dst_port, const void *data, uint32_t length);
/* Queries any announced remote instance of service. Always outputs current generation if non-NULL.
 * Returns 1 when found (node/port/instance populated), 0 when absent or router not started. */
int sheng_qrtr_find_service(uint32_t service, uint32_t *node, uint32_t *port, uint32_t *instance, uint32_t *generation);
/* Registers a server-table change watcher (called from the DSP thread without holding the QRTR lock). Returns 0 or -1. */
int sheng_qrtr_watch(ShengQrtrServerNotify notify, void *context);

#ifdef __cplusplus
}
#endif
#endif
