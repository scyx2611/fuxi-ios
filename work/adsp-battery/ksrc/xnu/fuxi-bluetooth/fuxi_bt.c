#include "fuxi_bt.h"
#include <string.h>

struct fuxi_bt_parser {
    uint8_t header[5];
    uint8_t event[258];
    unsigned header_size;
    unsigned header_need;
    unsigned body_size;
    unsigned body_need;
};

static uint16_t fuxi_bt_le16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | (uint16_t)bytes[1] << 8);
}

static uint32_t fuxi_bt_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
        (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

const char *fuxi_bt_status_name(enum fuxi_bt_status status)
{
    static const char *const names[] = {
        "ok", "off", "argument", "no-uart", "busy", "again", "io",
        "timeout", "budget", "protocol", "controller", "legacy",
        "ibs", "version", "cleanup"
    };
    if ((unsigned)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown";
    }
    return names[status];
}

bool fuxi_bt_uart_ops_valid(const struct fuxi_bt_uart_ops *ops)
{
    return ops && ops->acquire && ops->write && ops->read &&
        ops->release && ops->now_us && ops->wait_us;
}

/*
 * 來源頻率與 m/n 取自 Linux drivers/clk/qcom/gcc-sm8550.c ftbl_gcc_qupv3_wrap2_s6_clk_src
 * （P_GCC_GPLL0_OUT_EVEN、pre_div 1）；QUP ≥ 2.5 取樣 16，除頻 = 來源 / (baud × 16)：
 * 115200 用 7.3728 MHz 除 4，其餘除 1。qca 值對照 Linux btqca.h enum qca_baudrate；
 * Linux 的 WCN7850 DT 用 max-speed 3200000。
 */
static const struct fuxi_bt_rate fuxi_bt_rates[] = {
    {115200u, 7372800u, 384u, 15625u, 0u},
    {921600u, 14745600u, 768u, 15625u, 10u},
    {2000000u, 32000000u, 8u, 75u, 13u},
    {3000000u, 48000000u, 4u, 25u, 14u},
    {3200000u, 51200000u, 64u, 375u, 17u}
};

const struct fuxi_bt_rate *fuxi_bt_rate_find(uint32_t baud)
{
    for (unsigned index = 0; index < sizeof(fuxi_bt_rates) / sizeof(fuxi_bt_rates[0]); index++) {
        if (fuxi_bt_rates[index].baud == baud) {
            return &fuxi_bt_rates[index];
        }
    }
    return NULL;
}

static enum fuxi_bt_status fuxi_bt_event(const uint8_t *event,
    unsigned size, struct fuxi_bt_result *result)
{
    const uint8_t code = event[1];
    const unsigned payload = event[2];
    if (code == 0xff && payload >= 2 && event[3] == 0 && event[4] == 2) {
        return FUXI_BT_LEGACY;
    }
    if (code == 0x0f) {
        if (payload != 4) {
            return FUXI_BT_PROTOCOL;
        }
        if (fuxi_bt_le16(event + 5) == 0xfc00) {
            result->controller_status = event[3];
            if (event[3]) {
                return FUXI_BT_CONTROLLER;
            }
        }
    }
    if (code == 0x0e) {
        if (payload < 3) {
            return FUXI_BT_PROTOCOL;
        }
        if (fuxi_bt_le16(event + 4) == 0xfc00) {
            if (payload == 4 && event[6]) {
                result->controller_status = event[6];
                return FUXI_BT_CONTROLLER;
            }
            if (payload != 18 || size != sizeof(result->reply)) {
                return FUXI_BT_PROTOCOL;
            }
            memcpy(result->reply, event, size);
            result->reply_size = size;
            if (event[6] != 0 || event[7] != 0x19) {
                return FUXI_BT_PROTOCOL;
            }
            /*
             * WCN3991 以後的版本回應放在 Command Complete：event[6]=cresp(0)、event[7]=rtype(0x19)，
             * event[8] 是多出的一個位元組，版本資料從 event[9] 起。Linux btqca.c qca_read_soc_version
             * 對 soc_type >= QCA_WCN3991 用 memcpy(ver, edl->data + 1, ...) 直接略過它、不當狀態碼；
             * 2026-11-17 WCN7850 實機回 0x0c（等於 12 位元組版本資料長度），之前誤判成 Command Disallowed。
             */
            result->version.product = fuxi_bt_le32(event + 9);
            result->version.patch = fuxi_bt_le16(event + 13);
            result->version.rom = fuxi_bt_le16(event + 15);
            result->version.soc = fuxi_bt_le32(event + 17);
            if (!result->version.soc || !result->version.rom) {
                return FUXI_BT_VERSION;
            }
            result->version_valid = true;
            return FUXI_BT_OK;
        }
    }
    result->ignored_frames++;
    return FUXI_BT_AGAIN;
}

static enum fuxi_bt_status fuxi_bt_receive(struct fuxi_bt_parser *parser,
    const uint8_t *bytes, size_t size, struct fuxi_bt_result *result)
{
    for (size_t index = 0; index < size; index++) {
        const uint8_t byte = bytes[index];
        if (!parser->header_size) {
            switch (byte) {
                case 2:
                case 5:
                    parser->header_need = 5;
                    break;
                case 3:
                    parser->header_need = 4;
                    break;
                case 4:
                    parser->header_need = 3;
                    break;
                case 0xfc:
                case 0xfd:
                case 0xfe:
                    return FUXI_BT_IBS;
                default:
                    return FUXI_BT_PROTOCOL;
            }
        }
        if (parser->header_size < parser->header_need) {
            parser->header[parser->header_size++] = byte;
            if (parser->header_size != parser->header_need) {
                continue;
            }
            if (parser->header[0] == 4) {
                parser->body_need = parser->header[2];
                memcpy(parser->event, parser->header, parser->header_need);
            } else if (parser->header[0] == 3) {
                parser->body_need = parser->header[3];
            } else {
                parser->body_need = fuxi_bt_le16(parser->header + 3);
                if (parser->header[0] == 5) {
                    parser->body_need &= 0x3fffu;
                }
            }
            if (parser->body_need > FUXI_BT_MAX_RX) {
                return FUXI_BT_BUDGET;
            }
        } else {
            if (parser->header[0] == 4) {
                parser->event[parser->header_need + parser->body_size] = byte;
            }
            parser->body_size++;
        }
        if (parser->body_size == parser->body_need) {
            enum fuxi_bt_status status = FUXI_BT_AGAIN;
            if (++result->frames > FUXI_BT_MAX_FRAMES) {
                return FUXI_BT_BUDGET;
            }
            if (parser->header[0] == 4) {
                status = fuxi_bt_event(parser->event,
                    parser->header_need + parser->body_need, result);
            } else {
                result->ignored_frames++;
            }
            memset(parser, 0, sizeof(*parser));
            if (status != FUXI_BT_AGAIN) {
                return status;
            }
        }
    }
    return FUXI_BT_AGAIN;
}

static enum fuxi_bt_status fuxi_bt_time_check(
    const struct fuxi_bt_uart_ops *ops, uint64_t start,
    struct fuxi_bt_result *result)
{
    const uint64_t now = ops->now_us(ops->context);
    if (now < start || now - start < result->elapsed_us) {
        return FUXI_BT_IO;
    }
    result->elapsed_us = now - start;
    if (result->elapsed_us >= FUXI_BT_TIMEOUT_US) {
        return FUXI_BT_TIMEOUT;
    }
    return FUXI_BT_OK;
}

static enum fuxi_bt_status fuxi_bt_drain(
    const struct fuxi_bt_uart_ops *ops, uint64_t start,
    struct fuxi_bt_result *result)
{
    uint8_t bytes[64];
    for (unsigned attempt = 0; attempt < FUXI_BT_MAX_DRAIN; attempt++) {
        size_t count = 0;
        const unsigned remaining = FUXI_BT_MAX_DRAIN - result->drained_bytes;
        const size_t capacity = remaining < sizeof(bytes) ? remaining : sizeof(bytes);
        enum fuxi_bt_status status = fuxi_bt_time_check(ops, start, result);
        if (status != FUXI_BT_OK) {
            return status;
        }
        if (!capacity) {
            return FUXI_BT_BUDGET;
        }
        status = ops->read(ops->context, bytes, capacity, &count);
        if (count > capacity || (status != FUXI_BT_OK && count)) {
            return FUXI_BT_IO;
        }
        if (status == FUXI_BT_AGAIN || (status == FUXI_BT_OK && !count)) {
            return FUXI_BT_OK;
        }
        if (status != FUXI_BT_OK) {
            return status;
        }
        if (count > FUXI_BT_MAX_DRAIN - result->drained_bytes) {
            return FUXI_BT_BUDGET;
        }
        result->drained_bytes += (unsigned)count;
    }
    return FUXI_BT_BUDGET;
}

enum fuxi_bt_status fuxi_bt_probe_run(unsigned opt_in,
    const struct fuxi_bt_uart_ops *ops, struct fuxi_bt_result *result)
{
    static const uint8_t command[] = {1, 0, 0xfc, 1, 0x19};
    struct fuxi_bt_parser parser = {0};
    uint8_t bytes[64];
    uint64_t start;
    enum fuxi_bt_status status;
    if (!result) {
        return FUXI_BT_ARGUMENT;
    }
    memset(result, 0, sizeof(*result));
    result->status = FUXI_BT_OFF;
    if (!opt_in) {
        return result->status;
    }
    if (opt_in != 1) {
        result->status = FUXI_BT_ARGUMENT;
        return result->status;
    }
    if (!fuxi_bt_uart_ops_valid(ops)) {
        result->status = FUXI_BT_NO_UART;
        return result->status;
    }
    start = ops->now_us(ops->context);
    status = ops->acquire(ops->context, FUXI_BT_BOOT_BAUD);
    if (status != FUXI_BT_OK) {
        goto finish;
    }
    status = fuxi_bt_drain(ops, start, result);
    if (status != FUXI_BT_OK) {
        goto finish;
    }
    for (unsigned attempt = 0; attempt < FUXI_BT_MAX_POLLS; attempt++) {
        size_t count = 0;
        result->polls++;
        status = fuxi_bt_time_check(ops, start, result);
        if (status != FUXI_BT_OK) {
            goto finish;
        }
        if (result->tx_bytes < sizeof(command)) {
            const size_t remaining = sizeof(command) - result->tx_bytes;
            status = ops->write(ops->context, command + result->tx_bytes,
                remaining, &count);
            if (count > remaining || (status != FUXI_BT_OK && count)) {
                status = FUXI_BT_IO;
                goto finish;
            }
            if (status != FUXI_BT_OK && status != FUXI_BT_AGAIN) {
                goto finish;
            }
            result->tx_bytes += (unsigned)count;
        }
        if (result->tx_bytes == sizeof(command)) {
            const unsigned remaining = FUXI_BT_MAX_RX - result->rx_bytes;
            const size_t capacity = remaining < sizeof(bytes) ? remaining : sizeof(bytes);
            status = fuxi_bt_time_check(ops, start, result);
            if (status != FUXI_BT_OK) {
                goto finish;
            }
            if (!capacity) {
                status = FUXI_BT_BUDGET;
                goto finish;
            }
            count = 0;
            status = ops->read(ops->context, bytes, capacity, &count);
            if (count > capacity || (status != FUXI_BT_OK && count)) {
                status = FUXI_BT_IO;
                goto finish;
            }
            if (status != FUXI_BT_OK && status != FUXI_BT_AGAIN) {
                goto finish;
            }
            if (count > FUXI_BT_MAX_RX - result->rx_bytes) {
                status = FUXI_BT_BUDGET;
                goto finish;
            }
            result->rx_bytes += (unsigned)count;
            status = fuxi_bt_time_check(ops, start, result);
            if (status != FUXI_BT_OK) {
                goto finish;
            }
            status = fuxi_bt_receive(&parser, bytes, count, result);
            if (status != FUXI_BT_AGAIN) {
                goto finish;
            }
        }
        ops->wait_us(ops->context, 1000);
    }
    status = FUXI_BT_TIMEOUT;

finish:
    if (status == FUXI_BT_AGAIN) {
        status = FUXI_BT_IO;
    }
    {
        const enum fuxi_bt_status timing = fuxi_bt_time_check(ops, start, result);
        if (status == FUXI_BT_OK && timing != FUXI_BT_OK) {
            status = timing;
        }
    }
    result->status = status;
    result->cleanup = ops->release(ops->context);
    if (result->status == FUXI_BT_OK && result->cleanup != FUXI_BT_OK) {
        result->status = FUXI_BT_CLEANUP;
    }
    return result->status;
}

enum fuxi_bt_status fuxi_bt_snapshot_read(fuxi_bt_read32_fn read32,
    void *context, struct fuxi_bt_snapshot *snapshot)
{
    static const uint32_t branches[] = {0x233d4, 0x233d0, 0x233cc, 0x23004, 0x1e754};
    static const uint32_t rcg[] = {0x1e760, 0x1e764, 0x1e768, 0x1e76c, 0x1e770, 0x1e774};
    static const uint32_t engine[] = {0x68, 0x64, 0x40, 0x610, 0x640};
    uint32_t *engine_values[5];
    enum fuxi_bt_status status;
    if (!read32 || !snapshot) {
        return FUXI_BT_ARGUMENT;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    status = read32(context, FUXI_BT_GCC_BASE + 0x52010, &snapshot->vote);
    if (status != FUXI_BT_OK) {
        return status;
    }
    status = read32(context, FUXI_BT_GCC_BASE + 0x52018, &snapshot->gpll0_vote);
    if (status != FUXI_BT_OK) {
        return status;
    }
    for (unsigned index = 0; index < 5; index++) {
        status = read32(context, FUXI_BT_GCC_BASE + branches[index], &snapshot->branches[index]);
        if (status != FUXI_BT_OK) {
            return status;
        }
        if (snapshot->branches[index] & (1u << 31)) {
            snapshot->off_mask |= 1u << index;
        }
    }
    for (unsigned index = 0; index < 6; index++) {
        status = read32(context, FUXI_BT_GCC_BASE + rcg[index], &snapshot->rcg[index]);
        if (status != FUXI_BT_OK) {
            return status;
        }
    }
    for (unsigned index = 0; index < 8; index++) {
        const uint64_t pin = FUXI_BT_TLMM_BASE + (uint64_t)(76 + index) * 0x1000;
        status = read32(context, pin, &snapshot->pin_ctl[index]);
        if (status != FUXI_BT_OK) {
            return status;
        }
        status = read32(context, pin + 4, &snapshot->pin_io[index]);
        if (status != FUXI_BT_OK) {
            return status;
        }
    }
    if (snapshot->off_mask) {
        return FUXI_BT_OK;
    }
    status = read32(context, FUXI_BT_WRAPPER_BASE + 4, &snapshot->wrapper_version);
    if (status != FUXI_BT_OK) {
        return status;
    }
    engine_values[0] = &snapshot->firmware;
    engine_values[1] = &snapshot->interface_disable;
    engine_values[2] = &snapshot->engine_status;
    engine_values[3] = &snapshot->master_irq;
    engine_values[4] = &snapshot->secondary_irq;
    for (unsigned index = 0; index < 5; index++) {
        status = read32(context, FUXI_BT_UART_BASE + engine[index], engine_values[index]);
        if (status != FUXI_BT_OK) {
            return status;
        }
    }
    snapshot->engine_read = true;
    return FUXI_BT_OK;
}
