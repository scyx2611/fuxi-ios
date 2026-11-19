#ifndef FUXI_BT_GENI_FIFO_H
#define FUXI_BT_GENI_FIFO_H

#include "fuxi_bt.h"

#define FUXI_BT_FIFO_MAX_TX 1024u
#define FUXI_BT_FIFO_WORD_BUDGET 16u
#define FUXI_BT_FIFO_TX_TIMEOUT_US 200000u
#define FUXI_BT_FIFO_STOP_TIMEOUT_US 20000u
#define FUXI_BT_FIFO_MAX_POLLS 2000u
#define FUXI_BT_FIFO_SAVED_REGISTERS 27u

enum fuxi_bt_geni_fifo_register {
    FUXI_BT_FIFO_OUTPUT_CTRL = 0x24,
    FUXI_BT_FIFO_CLOCK_GATING = 0x28,
    FUXI_BT_FIFO_STATUS = 0x40,
    FUXI_BT_FIFO_MASTER_CLOCK = 0x48,
    FUXI_BT_FIFO_SECONDARY_CLOCK = 0x4c,
    FUXI_BT_FIFO_IF_DISABLE = 0x64,
    FUXI_BT_FIFO_FIRMWARE = 0x68,
    FUXI_BT_FIFO_CLOCK_SELECT = 0x7c,
    FUXI_BT_FIFO_LOOPBACK = 0x22c,
    FUXI_BT_FIFO_BYTE_GRANULARITY = 0x254,
    FUXI_BT_FIFO_DMA_MODE = 0x258,
    FUXI_BT_FIFO_TX_CONFIG = 0x25c,
    FUXI_BT_FIFO_TX_PACKING0 = 0x260,
    FUXI_BT_FIFO_TX_PACKING1 = 0x264,
    FUXI_BT_FIFO_TX_WORD_LENGTH = 0x268,
    FUXI_BT_FIFO_TX_STOP_LENGTH = 0x26c,
    FUXI_BT_FIFO_TX_LENGTH = 0x270,
    FUXI_BT_FIFO_RX_CONFIG = 0x280,
    FUXI_BT_FIFO_RX_PACKING0 = 0x284,
    FUXI_BT_FIFO_RX_PACKING1 = 0x288,
    FUXI_BT_FIFO_RX_WORD_LENGTH = 0x28c,
    FUXI_BT_FIFO_RX_STALE = 0x294,
    FUXI_BT_FIFO_TX_PARITY = 0x2a4,
    FUXI_BT_FIFO_RX_PARITY = 0x2a8,
    FUXI_BT_FIFO_MANUAL_RFR = 0x2ac,
    FUXI_BT_FIFO_MASTER_COMMAND = 0x600,
    FUXI_BT_FIFO_MASTER_CONTROL = 0x604,
    FUXI_BT_FIFO_MASTER_IRQ = 0x610,
    FUXI_BT_FIFO_MASTER_IRQ_ENABLE = 0x614,
    FUXI_BT_FIFO_MASTER_IRQ_CLEAR = 0x618,
    FUXI_BT_FIFO_SECONDARY_COMMAND = 0x630,
    FUXI_BT_FIFO_SECONDARY_CONTROL = 0x634,
    FUXI_BT_FIFO_SECONDARY_IRQ = 0x640,
    FUXI_BT_FIFO_SECONDARY_IRQ_ENABLE = 0x644,
    FUXI_BT_FIFO_SECONDARY_IRQ_CLEAR = 0x648,
    FUXI_BT_FIFO_TX = 0x700,
    FUXI_BT_FIFO_RX = 0x780,
    FUXI_BT_FIFO_TX_STATUS = 0x800,
    FUXI_BT_FIFO_RX_STATUS = 0x804,
    FUXI_BT_FIFO_TX_WATERMARK = 0x80c,
    FUXI_BT_FIFO_RX_WATERMARK = 0x810,
    FUXI_BT_FIFO_RX_RFR_WATERMARK = 0x814,
    FUXI_BT_FIFO_GSI_EVENTS = 0xe18,
    FUXI_BT_FIFO_IRQ_ENABLE = 0xe1c,
    FUXI_BT_FIFO_HW_TX = 0xe24,
    FUXI_BT_FIFO_HW_RX = 0xe28
};

enum fuxi_bt_geni_fifo_state {
    FUXI_BT_FIFO_ZERO = 0,
    FUXI_BT_FIFO_STARTING,
    FUXI_BT_FIFO_OPEN,
    FUXI_BT_FIFO_FAULT,
    FUXI_BT_FIFO_CANCEL,
    FUXI_BT_FIFO_ABORT,
    FUXI_BT_FIFO_DRAIN,
    FUXI_BT_FIFO_CLOSED,
    FUXI_BT_FIFO_CLOSE_FAILED
};

struct fuxi_bt_geni_fifo_io {
    void *context;
    enum fuxi_bt_status (*read)(void *, uint32_t, uint32_t *);
    enum fuxi_bt_status (*write)(void *, uint32_t, uint32_t);
    uint64_t (*now_us)(void *);
};

struct fuxi_bt_geni_fifo_config {
    unsigned opt_in;
    bool resources_owned;
    uint32_t qup_version;
    uint32_t source_hz;
    uint32_t baud; /* 0＝FUXI_BT_BOOT_BAUD */
};

struct fuxi_bt_geni_fifo {
    struct fuxi_bt_geni_fifo_io io;
    enum fuxi_bt_geni_fifo_state state;
    enum fuxi_bt_status error;
    enum fuxi_bt_status cleanup;
    bool modified;
    bool owns_master;
    bool owns_secondary;
    bool io_fault;
    bool quiescent;
    bool restored;
    uint32_t failed_register;
    bool failed_write;
    uint32_t firmware;
    uint32_t interface_disable;
    uint32_t dma_mode;
    uint32_t master_irq;
    uint32_t secondary_irq;
    uint32_t startup_master_irq;
    uint32_t startup_secondary_irq;
    unsigned tx_depth;
    unsigned rx_depth;
    uint32_t baud;
    uint32_t divider;
    uint32_t saved[FUXI_BT_FIFO_SAVED_REGISTERS];
    uint64_t start_us;
    unsigned start_polls;
    unsigned tx_size;
    unsigned tx_remaining;
    unsigned tx_polls;
    uint64_t tx_started_us;
    uint64_t tx_accepted;
    uint64_t tx_completed;
    uint8_t rx_pending[4];
    unsigned rx_pending_size;
    unsigned rx_pending_offset;
    uint64_t rx_delivered;
    uint64_t rx_discarded;
    uint64_t last_us;
    uint64_t stop_started_us;
    unsigned stop_polls;
};

#ifdef __cplusplus
extern "C" {
#endif

enum fuxi_bt_status fuxi_bt_geni_fifo_open(struct fuxi_bt_geni_fifo *fifo,
    const struct fuxi_bt_geni_fifo_io *io, const struct fuxi_bt_geni_fifo_config *config);
enum fuxi_bt_status fuxi_bt_geni_fifo_poll(struct fuxi_bt_geni_fifo *fifo);
enum fuxi_bt_status fuxi_bt_geni_fifo_write(struct fuxi_bt_geni_fifo *fifo,
    const uint8_t *bytes, size_t size, size_t *accepted);
enum fuxi_bt_status fuxi_bt_geni_fifo_read(struct fuxi_bt_geni_fifo *fifo,
    uint8_t *bytes, size_t capacity, size_t *received);
enum fuxi_bt_status fuxi_bt_geni_fifo_close_poll(struct fuxi_bt_geni_fifo *fifo);

#ifdef __cplusplus
}
#endif

#endif
