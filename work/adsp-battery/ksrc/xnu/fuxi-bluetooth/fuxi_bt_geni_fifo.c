#include "fuxi_bt_geni_fifo.h"
#include <string.h>

#define FUXI_BT_FIFO_MASTER_ACTIVE 1u
#define FUXI_BT_FIFO_SECONDARY_ACTIVE (1u << 12)
#define FUXI_BT_FIFO_COMMAND_DONE 1u
#define FUXI_BT_FIFO_COMMAND_CANCEL (1u << 4)
#define FUXI_BT_FIFO_COMMAND_ABORT (1u << 5)
#define FUXI_BT_FIFO_CONTROL_CANCEL 4u
#define FUXI_BT_FIFO_CONTROL_ABORT 2u
#define FUXI_BT_FIFO_MASTER_ERRORS (0xeu | (3u << 24) | (3u << 28))
#define FUXI_BT_FIFO_SECONDARY_ERRORS (0xeu | (15u << 9) | (3u << 24))
#define FUXI_BT_FIFO_RX_EVENTS ((1u << 26) | (1u << 27))

static const uint32_t fuxi_bt_fifo_saved_registers[] = {
    FUXI_BT_FIFO_IRQ_ENABLE,
    FUXI_BT_FIFO_MASTER_IRQ_ENABLE,
    FUXI_BT_FIFO_SECONDARY_IRQ_ENABLE,
    FUXI_BT_FIFO_CLOCK_GATING,
    FUXI_BT_FIFO_OUTPUT_CTRL,
    FUXI_BT_FIFO_MASTER_CLOCK,
    FUXI_BT_FIFO_SECONDARY_CLOCK,
    FUXI_BT_FIFO_CLOCK_SELECT,
    FUXI_BT_FIFO_LOOPBACK,
    FUXI_BT_FIFO_BYTE_GRANULARITY,
    FUXI_BT_FIFO_TX_CONFIG,
    FUXI_BT_FIFO_TX_PACKING0,
    FUXI_BT_FIFO_TX_PACKING1,
    FUXI_BT_FIFO_TX_WORD_LENGTH,
    FUXI_BT_FIFO_TX_STOP_LENGTH,
    FUXI_BT_FIFO_RX_CONFIG,
    FUXI_BT_FIFO_RX_PACKING0,
    FUXI_BT_FIFO_RX_PACKING1,
    FUXI_BT_FIFO_RX_WORD_LENGTH,
    FUXI_BT_FIFO_RX_STALE,
    FUXI_BT_FIFO_MANUAL_RFR,
    FUXI_BT_FIFO_TX_PARITY,
    FUXI_BT_FIFO_RX_PARITY,
    FUXI_BT_FIFO_TX_WATERMARK,
    FUXI_BT_FIFO_RX_WATERMARK,
    FUXI_BT_FIFO_RX_RFR_WATERMARK,
    FUXI_BT_FIFO_TX_LENGTH
};

_Static_assert(sizeof(fuxi_bt_fifo_saved_registers) / sizeof(fuxi_bt_fifo_saved_registers[0]) ==
    FUXI_BT_FIFO_SAVED_REGISTERS, "FIFO saved-register count");

static enum fuxi_bt_status fuxi_bt_fifo_fail(struct fuxi_bt_geni_fifo *fifo,
    enum fuxi_bt_status status)
{
    if (fifo->error == FUXI_BT_OK) {
        fifo->error = status;
    }
    fifo->state = FUXI_BT_FIFO_FAULT;
    return fifo->error;
}

static bool fuxi_bt_fifo_read32(struct fuxi_bt_geni_fifo *fifo,
    uint32_t reg, uint32_t *value)
{
    if (fifo->io.read(fifo->io.context, reg, value) == FUXI_BT_OK) {
        return true;
    }
    fifo->io_fault = true;
    fifo->failed_register = reg;
    fifo->failed_write = false;
    return false;
}

static bool fuxi_bt_fifo_write32(struct fuxi_bt_geni_fifo *fifo,
    uint32_t reg, uint32_t value)
{
    if (fifo->io.write(fifo->io.context, reg, value) == FUXI_BT_OK) {
        return true;
    }
    fifo->io_fault = true;
    fifo->failed_register = reg;
    fifo->failed_write = true;
    return false;
}

static bool fuxi_bt_fifo_now(struct fuxi_bt_geni_fifo *fifo, uint64_t *now)
{
    *now = fifo->io.now_us(fifo->io.context);
    if (*now < fifo->last_us) {
        return false;
    }
    fifo->last_us = *now;
    return true;
}

static bool fuxi_bt_fifo_irqs(struct fuxi_bt_geni_fifo *fifo)
{
    return fuxi_bt_fifo_read32(fifo, FUXI_BT_FIFO_MASTER_IRQ, &fifo->master_irq) &&
        fuxi_bt_fifo_read32(fifo, FUXI_BT_FIFO_SECONDARY_IRQ, &fifo->secondary_irq);
}

enum fuxi_bt_status fuxi_bt_geni_fifo_open(struct fuxi_bt_geni_fifo *fifo,
    const struct fuxi_bt_geni_fifo_io *io, const struct fuxi_bt_geni_fifo_config *config)
{
    uint32_t status, tx_status, rx_status, events, hw_tx, hw_rx;
    uint32_t master_control, secondary_control;
    uint32_t divider, sampling, depth_mask, baud;
    unsigned major, minor;
    if (!fifo || !config) {
        return FUXI_BT_ARGUMENT;
    }
    if (!config->opt_in) {
        return FUXI_BT_OFF;
    }
    if (config->opt_in != 1 || !config->resources_owned ||
        !config->qup_version || !io || !io->read || !io->write || !io->now_us) {
        return FUXI_BT_ARGUMENT;
    }
    if (fifo->modified) {
        return FUXI_BT_BUSY;
    }
    sampling = config->qup_version >= 0x20050000u ? 16u : 32u;
    baud = config->baud ? config->baud : FUXI_BT_BOOT_BAUD;
    /* 只接受整數除頻（Linux get_clk_div_rate 找的也是剛好整除的來源頻率） */
    if (baud > FUXI_BT_MAX_BAUD || !config->source_hz || config->source_hz % (baud * sampling)) {
        return FUXI_BT_ARGUMENT;
    }
    divider = config->source_hz / (baud * sampling);
    if (!divider || divider > 4095) {
        return FUXI_BT_ARGUMENT;
    }
    memset(fifo, 0, sizeof(*fifo));
    fifo->io = *io;
    fifo->baud = baud;
    fifo->divider = divider;
    fifo->last_us = io->now_us(io->context);
    if (!fuxi_bt_fifo_read32(fifo, FUXI_BT_FIFO_IF_DISABLE, &fifo->interface_disable)) {
        return fuxi_bt_fifo_fail(fifo, FUXI_BT_IO);
    }
    if (fifo->interface_disable & 1u) {
        return fuxi_bt_fifo_fail(fifo, FUXI_BT_NO_UART);
    }
    if (!fuxi_bt_fifo_read32(fifo, FUXI_BT_FIFO_FIRMWARE, &fifo->firmware) ||
        !fuxi_bt_fifo_read32(fifo, FUXI_BT_FIFO_DMA_MODE, &fifo->dma_mode) ||
        !fuxi_bt_fifo_read32(fifo, FUXI_BT_FIFO_GSI_EVENTS, &events) ||
        !fuxi_bt_fifo_read32(fifo, FUXI_BT_FIFO_STATUS, &status) ||
        !fuxi_bt_fifo_read32(fifo, FUXI_BT_FIFO_TX_STATUS, &tx_status) ||
        !fuxi_bt_fifo_read32(fifo, FUXI_BT_FIFO_RX_STATUS, &rx_status) ||
        !fuxi_bt_fifo_read32(fifo, FUXI_BT_FIFO_HW_TX, &hw_tx) ||
        !fuxi_bt_fifo_read32(fifo, FUXI_BT_FIFO_HW_RX, &hw_rx) ||
        !fuxi_bt_fifo_read32(fifo, FUXI_BT_FIFO_MASTER_CONTROL, &master_control) ||
        !fuxi_bt_fifo_read32(fifo, FUXI_BT_FIFO_SECONDARY_CONTROL, &secondary_control) ||
        !fuxi_bt_fifo_irqs(fifo)) {
        return fuxi_bt_fifo_fail(fifo, FUXI_BT_IO);
    }
    if (((fifo->firmware >> 8) & 0xffu) != 2) {
        return fuxi_bt_fifo_fail(fifo, FUXI_BT_NO_UART);
    }
    if ((fifo->dma_mode & 1u) || events ||
        (status & (FUXI_BT_FIFO_MASTER_ACTIVE | FUXI_BT_FIFO_SECONDARY_ACTIVE)) ||
        (tx_status & 0xfffffffu) || (rx_status & 0x1ffffffu) ||
        ((master_control | secondary_control) & 7u)) {
        return fuxi_bt_fifo_fail(fifo, FUXI_BT_BUSY);
    }
    major = (config->qup_version >> 28) & 0xfu;
    minor = (config->qup_version >> 16) & 0xfffu;
    depth_mask = (major > 3 || (major == 3 && minor >= 10)) ? 0xffu : 0x3fu;
    fifo->tx_depth = (hw_tx >> 16) & depth_mask;
    fifo->rx_depth = (hw_rx >> 16) & depth_mask;
    if (((hw_tx >> 24) & 0x3fu) != 32 || ((hw_rx >> 24) & 0x3fu) != 32 ||
        !fifo->tx_depth || fifo->rx_depth < 3) {
        return fuxi_bt_fifo_fail(fifo, FUXI_BT_NO_UART);
    }
    for (unsigned index = 0; index < FUXI_BT_FIFO_SAVED_REGISTERS; index++) {
        if (!fuxi_bt_fifo_read32(fifo, fuxi_bt_fifo_saved_registers[index], &fifo->saved[index])) {
            return fuxi_bt_fifo_fail(fifo, FUXI_BT_IO);
        }
    }
    fifo->startup_master_irq = fifo->master_irq;
    fifo->startup_secondary_irq = fifo->secondary_irq;
    fifo->modified = true;
    if (!fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_IRQ_ENABLE, 0) ||
        !fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_MANUAL_RFR, 0x80000002u) ||
        !fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_MASTER_IRQ_CLEAR, 0xffffffffu) ||
        !fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_SECONDARY_IRQ_CLEAR, 0xffffffffu) ||
        !fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_CLOCK_GATING, fifo->saved[3] | 0x7fu) ||
        !fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_OUTPUT_CTRL, 0x7f) ||
        !fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_CLOCK_SELECT, fifo->saved[7] & ~7u) ||
        !fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_MASTER_CLOCK, (divider << 4) | 1u) ||
        !fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_SECONDARY_CLOCK, (divider << 4) | 1u) ||
        !fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_LOOPBACK, 0) ||
        !fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_BYTE_GRANULARITY, 0) ||
        !fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_TX_PACKING0, 0x4380e) ||
        !fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_TX_PACKING1, 0xc3e0e) ||
        !fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_RX_PACKING0, 0x4380e) ||
        !fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_RX_PACKING1, 0xc3e0e) ||
        !fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_TX_WORD_LENGTH, 8) ||
        !fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_RX_WORD_LENGTH, 8) ||
        !fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_TX_STOP_LENGTH, 0) ||
        !fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_TX_PARITY, fifo->saved[21] & ~1u) ||
        !fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_RX_PARITY, fifo->saved[22] & ~1u) ||
        !fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_TX_CONFIG, fifo->saved[10] & ~3u) ||
        !fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_RX_CONFIG, fifo->saved[15] & ~(1u << 4)) ||
        !fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_RX_STALE, 160) ||
        !fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_TX_WATERMARK,
            fifo->tx_depth > 2 ? 2 : fifo->tx_depth - 1) ||
        !fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_RX_WATERMARK, 2) ||
        !fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_RX_RFR_WATERMARK, fifo->rx_depth - 2) ||
        !fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_MASTER_IRQ_ENABLE,
            FUXI_BT_FIFO_MASTER_ERRORS | 0x31u | (1u << 30)) ||
        !fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_SECONDARY_IRQ_ENABLE,
            FUXI_BT_FIFO_SECONDARY_ERRORS | 0x31u | FUXI_BT_FIFO_RX_EVENTS)) {
        return fuxi_bt_fifo_fail(fifo, FUXI_BT_IO);
    }
    fifo->owns_secondary = true;
    if (!fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_SECONDARY_COMMAND, 1u << 27) ||
        !fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_MANUAL_RFR, 0)) {
        return fuxi_bt_fifo_fail(fifo, FUXI_BT_IO);
    }
    fifo->state = FUXI_BT_FIFO_STARTING;
    fifo->start_us = fifo->last_us;
    return fuxi_bt_geni_fifo_poll(fifo);
}

enum fuxi_bt_status fuxi_bt_geni_fifo_poll(struct fuxi_bt_geni_fifo *fifo)
{
    uint32_t status, tx_status;
    uint64_t now;
    if (!fifo) {
        return FUXI_BT_ARGUMENT;
    }
    if (fifo->state == FUXI_BT_FIFO_FAULT) {
        return fifo->error;
    }
    if (fifo->state != FUXI_BT_FIFO_OPEN && fifo->state != FUXI_BT_FIFO_STARTING) {
        return FUXI_BT_OFF;
    }
    if (!fuxi_bt_fifo_now(fifo, &now)) {
        return fuxi_bt_fifo_fail(fifo, FUXI_BT_IO);
    }
    if (!fuxi_bt_fifo_irqs(fifo) ||
        !fuxi_bt_fifo_read32(fifo, FUXI_BT_FIFO_STATUS, &status)) {
        return fuxi_bt_fifo_fail(fifo, FUXI_BT_IO);
    }
    if (!fuxi_bt_fifo_now(fifo, &now)) {
        return fuxi_bt_fifo_fail(fifo, FUXI_BT_IO);
    }
    if ((fifo->master_irq & FUXI_BT_FIFO_MASTER_ERRORS) ||
        (fifo->secondary_irq & FUXI_BT_FIFO_SECONDARY_ERRORS)) {
        return fuxi_bt_fifo_fail(fifo, FUXI_BT_IO);
    }
    if ((fifo->master_irq | fifo->secondary_irq) &
        (FUXI_BT_FIFO_COMMAND_CANCEL | FUXI_BT_FIFO_COMMAND_ABORT)) {
        return fuxi_bt_fifo_fail(fifo, FUXI_BT_PROTOCOL);
    }
    if (fifo->state == FUXI_BT_FIFO_STARTING) {
        if (++fifo->start_polls >= FUXI_BT_FIFO_MAX_POLLS ||
            now - fifo->start_us >= FUXI_BT_FIFO_STOP_TIMEOUT_US) {
            return fuxi_bt_fifo_fail(fifo, FUXI_BT_TIMEOUT);
        }
        if (!(status & FUXI_BT_FIFO_SECONDARY_ACTIVE)) {
            return FUXI_BT_AGAIN;
        }
        fifo->state = FUXI_BT_FIFO_OPEN;
    } else if (!(status & FUXI_BT_FIFO_SECONDARY_ACTIVE)) {
        return fuxi_bt_fifo_fail(fifo, FUXI_BT_PROTOCOL);
    }
    if (fifo->owns_master) {
        /*
         * 先看傳送是否已完成，再判逾時：常駐收送（fuxi,bt=5）的輪詢間隔受排程影響，
         * 已經送完的傳送晚一點被看到不算故障；只有仍在進行的傳送才套用時間與次數上限。
         */
        const bool polls_spent = ++fifo->tx_polls >= FUXI_BT_FIFO_MAX_POLLS;
        if (!fuxi_bt_fifo_read32(fifo, FUXI_BT_FIFO_TX_STATUS, &tx_status)) {
            return fuxi_bt_fifo_fail(fifo, FUXI_BT_IO);
        }
        if ((tx_status & 0xfffffffu) > fifo->tx_depth) {
            return fuxi_bt_fifo_fail(fifo, FUXI_BT_PROTOCOL);
        }
        if (!fuxi_bt_fifo_now(fifo, &now)) {
            return fuxi_bt_fifo_fail(fifo, FUXI_BT_IO);
        }
        if (!(status & FUXI_BT_FIFO_MASTER_ACTIVE) &&
            (fifo->master_irq & FUXI_BT_FIFO_COMMAND_DONE)) {
            if (fifo->tx_remaining || (tx_status & 0xfffffffu)) {
                return fuxi_bt_fifo_fail(fifo, FUXI_BT_PROTOCOL);
            }
            if (!fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_MASTER_IRQ_CLEAR,
                FUXI_BT_FIFO_COMMAND_DONE)) {
                return fuxi_bt_fifo_fail(fifo, FUXI_BT_IO);
            }
            fifo->tx_completed += fifo->tx_size;
            fifo->owns_master = false;
            return FUXI_BT_OK;
        }
        if (polls_spent || now - fifo->tx_started_us >= FUXI_BT_FIFO_TX_TIMEOUT_US) {
            return fuxi_bt_fifo_fail(fifo, FUXI_BT_TIMEOUT);
        }
        if (!(status & FUXI_BT_FIFO_MASTER_ACTIVE)) {
            return FUXI_BT_AGAIN;
        }
    } else if (status & FUXI_BT_FIFO_MASTER_ACTIVE) {
        return fuxi_bt_fifo_fail(fifo, FUXI_BT_BUSY);
    }
    return FUXI_BT_OK;
}

enum fuxi_bt_status fuxi_bt_geni_fifo_write(struct fuxi_bt_geni_fifo *fifo,
    const uint8_t *bytes, size_t size, size_t *accepted)
{
    uint32_t status;
    unsigned available;
    enum fuxi_bt_status result;
    if (!accepted) {
        return FUXI_BT_ARGUMENT;
    }
    *accepted = 0;
    if (!fifo || (!bytes && size) || size > FUXI_BT_FIFO_MAX_TX) {
        return FUXI_BT_ARGUMENT;
    }
    result = fuxi_bt_geni_fifo_poll(fifo);
    if (result != FUXI_BT_OK || !size) {
        return result;
    }
    if (fifo->owns_master && !fifo->tx_remaining) {
        return FUXI_BT_AGAIN;
    }
    if (fifo->owns_master && size != fifo->tx_remaining) {
        return FUXI_BT_ARGUMENT;
    }
    if (!fuxi_bt_fifo_read32(fifo, FUXI_BT_FIFO_TX_STATUS, &status)) {
        return fuxi_bt_fifo_fail(fifo, FUXI_BT_IO);
    }
    status &= 0xfffffffu;
    if (status > fifo->tx_depth) {
        return fuxi_bt_fifo_fail(fifo, FUXI_BT_PROTOCOL);
    }
    available = fifo->tx_depth - status;
    if (!available) {
        return FUXI_BT_AGAIN;
    }
    if (!fifo->owns_master) {
        if (status) {
            return fuxi_bt_fifo_fail(fifo, FUXI_BT_PROTOCOL);
        }
        fifo->tx_size = (unsigned)size;
        fifo->tx_remaining = (unsigned)size;
        fifo->tx_polls = 0;
        fifo->tx_started_us = fifo->last_us;
        if (!fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_MASTER_IRQ_CLEAR, 0x31u) ||
            !fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_TX_LENGTH, (uint32_t)size)) {
            return fuxi_bt_fifo_fail(fifo, FUXI_BT_IO);
        }
        fifo->owns_master = true;
        if (!fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_MASTER_COMMAND, 1u << 27)) {
            return fuxi_bt_fifo_fail(fifo, FUXI_BT_IO);
        }
    }
    for (unsigned index = 0; index < available && index < FUXI_BT_FIFO_WORD_BUDGET; index++) {
        uint32_t word = 0;
        const unsigned count = fifo->tx_remaining < 4 ? fifo->tx_remaining : 4;
        for (unsigned byte = 0; byte < count; byte++) {
            word |= (uint32_t)bytes[*accepted + byte] << (8 * byte);
        }
        if (!fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_TX, word)) {
            (void)fuxi_bt_fifo_fail(fifo, FUXI_BT_IO);
            return *accepted ? FUXI_BT_OK : fifo->error;
        }
        *accepted += count;
        fifo->tx_remaining -= count;
        fifo->tx_accepted += count;
        if (!fifo->tx_remaining) {
            break;
        }
    }
    if (!fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_MASTER_IRQ_CLEAR, 1u << 30)) {
        (void)fuxi_bt_fifo_fail(fifo, FUXI_BT_IO);
    }
    return *accepted ? FUXI_BT_OK : fifo->error;
}

static unsigned fuxi_bt_fifo_pending(struct fuxi_bt_geni_fifo *fifo,
    uint8_t *bytes, size_t capacity)
{
    unsigned delivered = 0;
    while (fifo->rx_pending_offset < fifo->rx_pending_size && delivered < capacity) {
        bytes[delivered++] = fifo->rx_pending[fifo->rx_pending_offset++];
    }
    if (fifo->rx_pending_offset == fifo->rx_pending_size) {
        fifo->rx_pending_offset = fifo->rx_pending_size = 0;
    }
    return delivered;
}

static enum fuxi_bt_status fuxi_bt_fifo_rx_count(struct fuxi_bt_geni_fifo *fifo,
    uint32_t status, unsigned *words, unsigned *last_bytes)
{
    *words = status & 0x1ffffffu;
    *last_bytes = 4;
    if (*words > fifo->rx_depth) {
        return FUXI_BT_PROTOCOL;
    }
    if (*words && (status & (1u << 31))) {
        const unsigned valid = (status >> 28) & 7u;
        if (valid > 4) {
            return FUXI_BT_PROTOCOL;
        }
        if (valid) {
            *last_bytes = valid;
        }
    }
    return FUXI_BT_OK;
}

enum fuxi_bt_status fuxi_bt_geni_fifo_read(struct fuxi_bt_geni_fifo *fifo,
    uint8_t *bytes, size_t capacity, size_t *received)
{
    uint32_t status, word;
    unsigned words, last_bytes;
    enum fuxi_bt_status result;
    if (!received) {
        return FUXI_BT_ARGUMENT;
    }
    *received = 0;
    if (!fifo || (!bytes && capacity)) {
        return FUXI_BT_ARGUMENT;
    }
    result = fuxi_bt_geni_fifo_poll(fifo);
    if (result != FUXI_BT_OK || !capacity) {
        return result;
    }
    *received = fuxi_bt_fifo_pending(fifo, bytes, capacity);
    if (*received == capacity) {
        fifo->rx_delivered += *received;
        return FUXI_BT_OK;
    }
    if (!fuxi_bt_fifo_read32(fifo, FUXI_BT_FIFO_RX_STATUS, &status)) {
        result = FUXI_BT_IO;
        goto failed;
    }
    result = fuxi_bt_fifo_rx_count(fifo, status, &words, &last_bytes);
    if (result != FUXI_BT_OK) {
        goto failed;
    }
    for (unsigned index = 0; index < words && index < FUXI_BT_FIFO_WORD_BUDGET; index++) {
        const unsigned valid = index + 1 == words ? last_bytes : 4;
        if (!fuxi_bt_fifo_read32(fifo, FUXI_BT_FIFO_RX, &word)) {
            result = FUXI_BT_IO;
            goto failed;
        }
        for (unsigned byte = 0; byte < valid; byte++) {
            fifo->rx_pending[byte] = (uint8_t)(word >> (8 * byte));
        }
        fifo->rx_pending_size = valid;
        *received += fuxi_bt_fifo_pending(fifo, bytes + *received, capacity - *received);
        if (index + 1 == words) {
            if (!fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_SECONDARY_IRQ_CLEAR,
                FUXI_BT_FIFO_RX_EVENTS)) {
                result = FUXI_BT_IO;
                goto failed;
            }
        }
        if (*received == capacity) {
            break;
        }
    }
    fifo->rx_delivered += *received;
    return *received ? FUXI_BT_OK : FUXI_BT_AGAIN;

failed:
    (void)fuxi_bt_fifo_fail(fifo, result);
    fifo->rx_delivered += *received;
    return *received ? FUXI_BT_OK : fifo->error;
}

static enum fuxi_bt_status fuxi_bt_fifo_close_fail(struct fuxi_bt_geni_fifo *fifo,
    enum fuxi_bt_status status)
{
    fifo->cleanup = status;
    fifo->state = FUXI_BT_FIFO_CLOSE_FAILED;
    return status;
}

static bool fuxi_bt_fifo_stop_request(struct fuxi_bt_geni_fifo *fifo,
    uint32_t status, uint32_t control)
{
    if (fifo->owns_master && (status & FUXI_BT_FIFO_MASTER_ACTIVE)) {
        if (!fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_MASTER_IRQ_CLEAR, 0x31u) ||
            !fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_MASTER_CONTROL, control)) {
            return false;
        }
    }
    if (fifo->owns_secondary && (status & FUXI_BT_FIFO_SECONDARY_ACTIVE)) {
        if (!fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_SECONDARY_IRQ_CLEAR, 0x31u) ||
            !fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_SECONDARY_CONTROL, control)) {
            return false;
        }
    }
    return true;
}

enum fuxi_bt_status fuxi_bt_geni_fifo_close_poll(struct fuxi_bt_geni_fifo *fifo)
{
    uint32_t status, master_control, secondary_control, tx_status, rx_status, word;
    uint64_t now;
    unsigned words, last_bytes;
    enum fuxi_bt_status result;
    if (!fifo) {
        return FUXI_BT_ARGUMENT;
    }
    if (fifo->state == FUXI_BT_FIFO_CLOSE_FAILED) {
        return fifo->cleanup;
    }
    if (!fifo->modified || fifo->state == FUXI_BT_FIFO_CLOSED) {
        return FUXI_BT_OK;
    }
    if (fifo->io_fault || !fuxi_bt_fifo_now(fifo, &now)) {
        return fuxi_bt_fifo_close_fail(fifo, FUXI_BT_IO);
    }
    if (++fifo->stop_polls >= FUXI_BT_FIFO_MAX_POLLS) {
        return fuxi_bt_fifo_close_fail(fifo, FUXI_BT_TIMEOUT);
    }
    if (!fuxi_bt_fifo_read32(fifo, FUXI_BT_FIFO_STATUS, &status)) {
        return fuxi_bt_fifo_close_fail(fifo, FUXI_BT_IO);
    }
    if (!fuxi_bt_fifo_now(fifo, &now)) {
        return fuxi_bt_fifo_close_fail(fifo, FUXI_BT_IO);
    }
    if ((!fifo->owns_master && (status & FUXI_BT_FIFO_MASTER_ACTIVE)) ||
        (!fifo->owns_secondary && (status & FUXI_BT_FIFO_SECONDARY_ACTIVE))) {
        return fuxi_bt_fifo_close_fail(fifo, FUXI_BT_BUSY);
    }
    if (fifo->state == FUXI_BT_FIFO_OPEN || fifo->state == FUXI_BT_FIFO_STARTING ||
        fifo->state == FUXI_BT_FIFO_FAULT) {
        if (!fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_MANUAL_RFR, 0x80000002u) ||
            !fuxi_bt_fifo_stop_request(fifo, status, FUXI_BT_FIFO_CONTROL_CANCEL)) {
            return fuxi_bt_fifo_close_fail(fifo, FUXI_BT_IO);
        }
        fifo->state = FUXI_BT_FIFO_CANCEL;
        fifo->stop_started_us = now;
        return FUXI_BT_AGAIN;
    }
    if (now - fifo->stop_started_us >= FUXI_BT_FIFO_STOP_TIMEOUT_US) {
        if (fifo->state == FUXI_BT_FIFO_CANCEL) {
            if (!fuxi_bt_fifo_stop_request(fifo, status, FUXI_BT_FIFO_CONTROL_ABORT)) {
                return fuxi_bt_fifo_close_fail(fifo, FUXI_BT_IO);
            }
            fifo->state = FUXI_BT_FIFO_ABORT;
            fifo->stop_started_us = now;
            return FUXI_BT_AGAIN;
        }
        return fuxi_bt_fifo_close_fail(fifo, FUXI_BT_TIMEOUT);
    }
    if (!fuxi_bt_fifo_read32(fifo, FUXI_BT_FIFO_MASTER_CONTROL, &master_control) ||
        !fuxi_bt_fifo_read32(fifo, FUXI_BT_FIFO_SECONDARY_CONTROL, &secondary_control) ||
        !fuxi_bt_fifo_irqs(fifo)) {
        return fuxi_bt_fifo_close_fail(fifo, FUXI_BT_IO);
    }
    if ((status & (FUXI_BT_FIFO_MASTER_ACTIVE | FUXI_BT_FIFO_SECONDARY_ACTIVE)) ||
        ((master_control | secondary_control) & 6u)) {
        return FUXI_BT_AGAIN;
    }
    if ((fifo->owns_master && !(fifo->master_irq & 0x31u)) ||
        (fifo->owns_secondary && !(fifo->secondary_irq & 0x31u))) {
        return FUXI_BT_AGAIN;
    }
    if (!fuxi_bt_fifo_read32(fifo, FUXI_BT_FIFO_TX_STATUS, &tx_status) ||
        !fuxi_bt_fifo_read32(fifo, FUXI_BT_FIFO_RX_STATUS, &rx_status)) {
        return fuxi_bt_fifo_close_fail(fifo, FUXI_BT_IO);
    }
    if (tx_status & 0xfffffffu) {
        return fuxi_bt_fifo_close_fail(fifo, FUXI_BT_CLEANUP);
    }
    result = fuxi_bt_fifo_rx_count(fifo, rx_status, &words, &last_bytes);
    if (result != FUXI_BT_OK) {
        return fuxi_bt_fifo_close_fail(fifo, result);
    }
    fifo->rx_discarded += fifo->rx_pending_size - fifo->rx_pending_offset;
    fifo->rx_pending_size = fifo->rx_pending_offset = 0;
    fifo->state = FUXI_BT_FIFO_DRAIN;
    for (unsigned index = 0; index < words && index < FUXI_BT_FIFO_WORD_BUDGET; index++) {
        if (!fuxi_bt_fifo_read32(fifo, FUXI_BT_FIFO_RX, &word)) {
            return fuxi_bt_fifo_close_fail(fifo, FUXI_BT_IO);
        }
        fifo->rx_discarded += index + 1 == words ? last_bytes : 4;
    }
    if (words) {
        return FUXI_BT_AGAIN;
    }
    fifo->quiescent = true;
    if (!fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_MASTER_IRQ_CLEAR, fifo->master_irq) ||
        !fuxi_bt_fifo_write32(fifo, FUXI_BT_FIFO_SECONDARY_IRQ_CLEAR, fifo->secondary_irq)) {
        return fuxi_bt_fifo_close_fail(fifo, FUXI_BT_IO);
    }
    for (unsigned index = FUXI_BT_FIFO_SAVED_REGISTERS; index > 0; index--) {
        if (!fuxi_bt_fifo_write32(fifo, fuxi_bt_fifo_saved_registers[index - 1], fifo->saved[index - 1])) {
            return fuxi_bt_fifo_close_fail(fifo, FUXI_BT_IO);
        }
    }
    for (unsigned index = 0; index < FUXI_BT_FIFO_SAVED_REGISTERS; index++) {
        uint32_t value;
        if (!fuxi_bt_fifo_read32(fifo, fuxi_bt_fifo_saved_registers[index], &value)) {
            return fuxi_bt_fifo_close_fail(fifo, FUXI_BT_IO);
        }
        if (value != fifo->saved[index]) {
            return fuxi_bt_fifo_close_fail(fifo, FUXI_BT_CLEANUP);
        }
    }
    if (!fuxi_bt_fifo_now(fifo, &now)) {
        return fuxi_bt_fifo_close_fail(fifo, FUXI_BT_IO);
    }
    if (now - fifo->stop_started_us >= FUXI_BT_FIFO_STOP_TIMEOUT_US) {
        return fuxi_bt_fifo_close_fail(fifo, FUXI_BT_TIMEOUT);
    }
    fifo->restored = true;
    fifo->modified = false;
    fifo->owns_master = fifo->owns_secondary = false;
    fifo->state = FUXI_BT_FIFO_CLOSED;
    fifo->cleanup = FUXI_BT_OK;
    return FUXI_BT_OK;
}
