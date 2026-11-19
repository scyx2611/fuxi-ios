#ifndef FUXI_BT_H
#define FUXI_BT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FUXI_BT_UART_BASE UINT64_C(0x898000)
#define FUXI_BT_WRAPPER_BASE UINT64_C(0x8c0000)
#define FUXI_BT_GCC_BASE UINT64_C(0x100000)
#define FUXI_BT_TLMM_BASE UINT64_C(0xf100000)
#define FUXI_BT_UART_INTID 493u
#define FUXI_BT_BOOT_BAUD 115200u
#define FUXI_BT_MAX_BAUD 3200000u /* Linux qca_set_baudrate 拒絕 3.2 Mbaud 以上 */
#define FUXI_BT_TIMEOUT_US 2000000u
#define FUXI_BT_MAX_POLLS 2000u
#define FUXI_BT_MAX_RX 4096u
#define FUXI_BT_MAX_FRAMES 64u
#define FUXI_BT_MAX_DRAIN 1024u

enum fuxi_bt_status {
    FUXI_BT_OK = 0,
    FUXI_BT_OFF,
    FUXI_BT_ARGUMENT,
    FUXI_BT_NO_UART,
    FUXI_BT_BUSY,
    FUXI_BT_AGAIN,
    FUXI_BT_IO,
    FUXI_BT_TIMEOUT,
    FUXI_BT_BUDGET,
    FUXI_BT_PROTOCOL,
    FUXI_BT_CONTROLLER,
    FUXI_BT_LEGACY,
    FUXI_BT_IBS,
    FUXI_BT_VERSION,
    FUXI_BT_CLEANUP
};

struct fuxi_bt_version {
    uint32_t product;
    uint32_t soc;
    uint16_t patch;
    uint16_t rom;
};

struct fuxi_bt_result {
    enum fuxi_bt_status status;
    enum fuxi_bt_status cleanup;
    struct fuxi_bt_version version;
    bool version_valid;
    uint8_t controller_status;
    uint8_t reply[21];
    unsigned reply_size;
    unsigned tx_bytes;
    unsigned rx_bytes;
    unsigned drained_bytes;
    unsigned frames;
    unsigned ignored_frames;
    unsigned polls;
    uint64_t elapsed_us;
};

struct fuxi_bt_uart_ops {
    void *context;
    enum fuxi_bt_status (*acquire)(void *, unsigned);
    enum fuxi_bt_status (*write)(void *, const uint8_t *, size_t, size_t *);
    enum fuxi_bt_status (*read)(void *, uint8_t *, size_t, size_t *);
    enum fuxi_bt_status (*release)(void *);
    uint64_t (*now_us)(void *);
    void (*wait_us)(void *, unsigned);
};

struct fuxi_bt_snapshot {
    uint32_t vote;
    uint32_t gpll0_vote;
    uint32_t branches[5];
    uint32_t rcg[6];
    uint32_t pin_ctl[8];
    uint32_t pin_io[8];
    uint32_t off_mask;
    bool engine_read;
    uint32_t wrapper_version;
    uint32_t firmware;
    uint32_t interface_disable;
    uint32_t engine_status;
    uint32_t master_irq;
    uint32_t secondary_irq;
};

/*
 * UART 速率：SE6 RCG 來源（GPLL0_OUT_EVEN 300 MHz × m/n）、GENI 除頻後的 baud，
 * 以及 QCA 0xfc48 的速率列舉值（Linux btqca.h enum qca_baudrate）。
 */
struct fuxi_bt_rate {
    uint32_t baud;
    uint32_t source_hz;
    uint16_t m;
    uint16_t n;
    uint8_t qca;
};

typedef enum fuxi_bt_status (*fuxi_bt_read32_fn)(void *, uint64_t, uint32_t *);

const char *fuxi_bt_status_name(enum fuxi_bt_status status);
bool fuxi_bt_uart_ops_valid(const struct fuxi_bt_uart_ops *ops);
const struct fuxi_bt_rate *fuxi_bt_rate_find(uint32_t baud);
enum fuxi_bt_status fuxi_bt_probe_run(unsigned opt_in,
    const struct fuxi_bt_uart_ops *ops, struct fuxi_bt_result *result);
enum fuxi_bt_status fuxi_bt_snapshot_read(fuxi_bt_read32_fn read32,
    void *context, struct fuxi_bt_snapshot *snapshot);
enum fuxi_bt_status fuxi_bt_register_uart(const struct fuxi_bt_uart_ops *ops);
void fuxi_bt_init(void);

#endif
