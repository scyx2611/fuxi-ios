#include "fuxi_bt_setup.h"
#include <string.h>

/*
 * WCN7850 控制器初始化。命令與回應格式參考 Linux btqca.c（qca_read_soc_version、qca_tlv_check_data、
 * qca_tlv_send_segment、qca_read_fw_board_id、qca_read_fw_build_info、qca_uart_setup），程式自寫：
 *  - vendor 命令 opcode 0xfc00，第一個位元組是子命令（0x19 版本、0x1e TLV 段、0x23 board ID、0x20 build info）；
 *  - WCN3991 以後回 Command Complete，回傳參數 [cresp][rtype][data…]，cresp=0 成功、rtype=子命令；
 *  - rampatch/NVM 整個檔案依序切 243 位元組段送出；patch 標頭的 download_mode 非 0 時控制器只回最後一段；
 *  - NVM tag 17（HCI）：data[0] bit 7＝軟體 IBS、data[1]＝baud（WCN3991 以後）；tag 27 bit 0＝deep sleep。
 *    Linux 會打開 IBS 並設成工作 baud；這裡關掉 IBS／deep sleep，baud 設成實際使用的速率
 *    （預設 115200，列舉值 0）。
 *  - 換速（qca_set_speed／qca_set_baudrate）：0xfc48 參數是 enum qca_baudrate，送出後不等回應，
 *    送完等 1–10 ms 再改 host 速率；這裡改 host 速率＝關 FIFO（RFR 先設未就緒）、改 RCG、以新速率重開。
 *    Linux 不另外確認，這裡以新速率重查版本，確認不到就退回 115200。
 *  - WCN7850 在 NVM 之後送 0xfc17 {0x14, 0x00} 關控制器日誌（qca_disable_soc_logging），選用。
 */

struct fuxi_bt_link {
    const struct fuxi_bt_uart_ops *ops;
    struct fuxi_bt_setup_result *result;
    uint64_t start;
    uint8_t rx[64];
    unsigned rx_offset;
    unsigned rx_size;
    uint8_t header[5];
    unsigned header_size;
    unsigned header_need;
    unsigned body_size;
    unsigned body_need;
    uint8_t event[258];
    unsigned event_size;
};

static uint16_t fuxi_bt_setup_le16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | (uint16_t)bytes[1] << 8);
}

static uint32_t fuxi_bt_setup_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
        (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

const char *fuxi_bt_setup_stage_name(enum fuxi_bt_setup_stage stage)
{
    static const char *const names[] = {
        "start", "version", "baud", "patch-load", "patch-parse", "patch-send", "board-id",
        "nvm-load", "nvm-parse", "nvm-send", "nolog", "build-info", "reset", "local-version",
        "bdaddr", "done"
    };
    if ((unsigned)stage >= sizeof(names) / sizeof(names[0])) {
        return "unknown";
    }
    return names[stage];
}

bool fuxi_bt_setup_name_valid(const char *name)
{
    if (!name || !name[0] || name[0] == '.') {
        return false;
    }
    for (unsigned index = 0; index < FUXI_BT_SETUP_NAME; index++) {
        const char c = name[index];
        if (!c) {
            return true;
        }
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '_' || c == '-')) {
            return false;
        }
    }
    return false;
}

/* 檔名組字：stem＋兩位十六進位 rom＋尾碼；bid_digits=0 表示不帶 board ID。 */
static void fuxi_bt_setup_name(char out[FUXI_BT_SETUP_NAME], const char *stem, unsigned rom,
    const char *middle, unsigned bid, unsigned bid_digits, const char *tail)
{
    static const char digits[] = "0123456789abcdef";
    unsigned at = 0;
    char hex[8];
    unsigned count = 0;
    memset(out, 0, FUXI_BT_SETUP_NAME);
    for (const char *p = stem; *p && at + 1 < FUXI_BT_SETUP_NAME; p++) {
        out[at++] = *p;
    }
    if (at + 3 < FUXI_BT_SETUP_NAME) {
        out[at++] = digits[(rom >> 4) & 15];
        out[at++] = digits[rom & 15];
    }
    for (const char *p = middle; p && *p && at + 1 < FUXI_BT_SETUP_NAME; p++) {
        out[at++] = *p;
    }
    if (bid_digits) {
        unsigned value = bid;
        do {
            hex[count++] = digits[value & 15];
            value >>= 4;
        } while (value && count < sizeof(hex));
        while (count < bid_digits && count < sizeof(hex)) {
            hex[count++] = '0';
        }
        while (count && at + 1 < FUXI_BT_SETUP_NAME) {
            out[at++] = hex[--count];
        }
    }
    for (const char *p = tail; p && *p && at + 1 < FUXI_BT_SETUP_NAME; p++) {
        out[at++] = *p;
    }
    out[FUXI_BT_SETUP_NAME - 1] = 0;
}

static enum fuxi_bt_status fuxi_bt_link_time(struct fuxi_bt_link *link)
{
    const uint64_t now = link->ops->now_us(link->ops->context);
    if (now < link->start || now - link->start < link->result->elapsed_us) {
        return FUXI_BT_IO;
    }
    link->result->elapsed_us = now - link->start;
    return link->result->elapsed_us >= FUXI_BT_SETUP_TIMEOUT_US ? FUXI_BT_TIMEOUT : FUXI_BT_OK;
}

static void fuxi_bt_link_note(uint8_t *destination, unsigned *destination_size,
    const uint8_t *event, unsigned size)
{
    const unsigned count = size < 24u ? size : 24u;
    memcpy(destination, event, count);
    *destination_size = count;
}

/* 換速後從頭分框：丟掉舊速率留下的半個封包。 */
static void fuxi_bt_link_reset(struct fuxi_bt_link *link)
{
    link->rx_offset = link->rx_size = 0;
    link->header_size = link->header_need = 0;
    link->body_size = link->body_need = 0;
    link->event_size = 0;
}

/* 開始前把 UART 裡的殘留位元組讀掉（不解析，避免從半個封包開始）。 */
static enum fuxi_bt_status fuxi_bt_link_drain(struct fuxi_bt_link *link)
{
    unsigned drained = 0;
    for (;;) {
        size_t count = 0;
        enum fuxi_bt_status status = fuxi_bt_link_time(link);
        if (status != FUXI_BT_OK) {
            return status;
        }
        status = link->ops->read(link->ops->context, link->rx, sizeof(link->rx), &count);
        if (count > sizeof(link->rx) || (status != FUXI_BT_OK && count)) {
            return FUXI_BT_IO;
        }
        if (status == FUXI_BT_AGAIN || (status == FUXI_BT_OK && !count)) {
            return FUXI_BT_OK;
        }
        if (status != FUXI_BT_OK) {
            return status;
        }
        drained += (unsigned)count;
        if (drained > FUXI_BT_MAX_DRAIN) {
            return FUXI_BT_BUDGET;
        }
    }
}

/* 取下一個完整 HCI 事件放進 link->event（[0]=0x04）。暫時沒有資料回 AGAIN，不等待。 */
static enum fuxi_bt_status fuxi_bt_link_event(struct fuxi_bt_link *link)
{
    struct fuxi_bt_setup_result *result = link->result;
    for (;;) {
        if (link->rx_offset == link->rx_size) {
            size_t count = 0;
            enum fuxi_bt_status status = link->ops->read(link->ops->context, link->rx,
                sizeof(link->rx), &count);
            if (count > sizeof(link->rx) || (status != FUXI_BT_OK && count)) {
                return FUXI_BT_IO;
            }
            if (status == FUXI_BT_AGAIN || (status == FUXI_BT_OK && !count)) {
                return FUXI_BT_AGAIN;
            }
            if (status != FUXI_BT_OK) {
                return status;
            }
            result->rx_bytes += (unsigned)count;
            link->rx_offset = 0;
            link->rx_size = (unsigned)count;
        }
        while (link->rx_offset < link->rx_size) {
            const uint8_t byte = link->rx[link->rx_offset++];
            if (!link->header_size) {
                switch (byte) {
                    case 2:
                    case 5:
                        link->header_need = 5;
                        break;
                    case 3:
                        link->header_need = 4;
                        break;
                    case 4:
                        link->header_need = 3;
                        break;
                    case 0xfc:
                    case 0xfd:
                    case 0xfe:
                        return FUXI_BT_IBS;
                    default:
                        return FUXI_BT_PROTOCOL;
                }
            }
            if (link->header_size < link->header_need) {
                link->header[link->header_size++] = byte;
                if (link->header_size != link->header_need) {
                    continue;
                }
                if (link->header[0] == 4) {
                    link->body_need = link->header[2];
                    memcpy(link->event, link->header, link->header_need);
                } else if (link->header[0] == 3) {
                    link->body_need = link->header[3];
                } else {
                    link->body_need = fuxi_bt_setup_le16(link->header + 3);
                    if (link->header[0] == 5) {
                        link->body_need &= 0x3fffu;
                    }
                }
                if (link->body_need > FUXI_BT_MAX_RX) {
                    return FUXI_BT_BUDGET;
                }
            } else {
                if (link->header[0] == 4) {
                    link->event[link->header_need + link->body_size] = byte;
                }
                link->body_size++;
            }
            if (link->body_size == link->body_need) {
                const bool event = link->header[0] == 4;
                const unsigned size = link->header_need + link->body_need;
                link->header_size = link->header_need = 0;
                link->body_size = link->body_need = 0;
                if (++result->frames > FUXI_BT_SETUP_MAX_FRAMES) {
                    return FUXI_BT_BUDGET;
                }
                if (event) {
                    link->event_size = size;
                    fuxi_bt_link_note(result->last_event, &result->last_event_size, link->event, size);
                    return FUXI_BT_OK;
                }
                result->ignored_frames++;
            }
        }
    }
}

/* 不是正在等的事件：vendor 事件計數，沒等的 TLV 段回應若帶錯誤就中止，硬體錯誤事件中止。 */
static enum fuxi_bt_status fuxi_bt_link_other(struct fuxi_bt_link *link)
{
    struct fuxi_bt_setup_result *result = link->result;
    const uint8_t *event = link->event;
    if (event[1] == 0xff) {
        result->vendor_events++;
        fuxi_bt_link_note(result->last_vendor, &result->last_vendor_size, event, link->event_size);
        return FUXI_BT_OK;
    }
    if (event[1] == 0x0e && event[2] >= 5 && fuxi_bt_setup_le16(event + 4) == 0xfc00 &&
        event[7] == 0x1e) {
        if (event[6]) {
            result->controller_status = event[6];
            return FUXI_BT_CONTROLLER;
        }
        result->stale_acks++;
        return FUXI_BT_OK;
    }
    if (event[1] == 0x10) {
        result->controller_status = event[2] ? event[3] : 0;
        return FUXI_BT_CONTROLLER;
    }
    result->ignored_frames++;
    return FUXI_BT_OK;
}

static enum fuxi_bt_status fuxi_bt_link_poll(struct fuxi_bt_link *link)
{
    for (;;) {
        enum fuxi_bt_status status = fuxi_bt_link_event(link);
        if (status == FUXI_BT_AGAIN) {
            return FUXI_BT_OK;
        }
        if (status != FUXI_BT_OK) {
            return status;
        }
        status = fuxi_bt_link_other(link);
        if (status != FUXI_BT_OK) {
            return status;
        }
    }
}

static enum fuxi_bt_status fuxi_bt_link_write(struct fuxi_bt_link *link,
    const uint8_t *bytes, size_t size)
{
    size_t sent = 0;
    enum fuxi_bt_status status = fuxi_bt_link_time(link);
    uint64_t begin;
    if (status != FUXI_BT_OK) {
        return status;
    }
    begin = link->result->elapsed_us;
    while (sent < size) {
        size_t count = 0;
        status = link->ops->write(link->ops->context, bytes + sent, size - sent, &count);
        if (count > size - sent || (status != FUXI_BT_OK && count)) {
            return FUXI_BT_IO;
        }
        if (status != FUXI_BT_OK && status != FUXI_BT_AGAIN) {
            return status;
        }
        sent += count;
        link->result->tx_bytes += (unsigned)count;
        if (sent < size && !count) {
            /* TX FIFO 滿：先收 RX，免得控制器因我方 RFR 停住而互等 */
            status = fuxi_bt_link_poll(link);
            if (status != FUXI_BT_OK) {
                return status;
            }
            link->ops->wait_us(link->ops->context, 100);
        }
        status = fuxi_bt_link_time(link);
        if (status != FUXI_BT_OK) {
            return status;
        }
        if (sent < size && link->result->elapsed_us - begin >= FUXI_BT_COMMAND_TIMEOUT_US) {
            return FUXI_BT_TIMEOUT;
        }
    }
    return FUXI_BT_OK;
}

/*
 * 等 opcode 的 Command Complete。vendor 命令（0xfc00）回傳參數是 [cresp][rtype][data…]：rtype 不符當成
 * 其他事件處理，cresp≠0 失敗；一般命令是 [status][…]，status≠0 失敗。成功時 *params 指向回傳參數。
 */
static enum fuxi_bt_status fuxi_bt_link_wait(struct fuxi_bt_link *link, uint16_t opcode,
    uint8_t rtype, const uint8_t **params, unsigned *size)
{
    struct fuxi_bt_setup_result *result = link->result;
    enum fuxi_bt_status status = fuxi_bt_link_time(link);
    uint64_t begin;
    if (status != FUXI_BT_OK) {
        return status;
    }
    begin = result->elapsed_us;
    for (;;) {
        status = fuxi_bt_link_event(link);
        if (status == FUXI_BT_OK) {
            const uint8_t *event = link->event;
            if (event[1] == 0x0e && event[2] >= 3 && fuxi_bt_setup_le16(event + 4) == opcode) {
                const unsigned count = event[2] - 3u;
                if (!count || (opcode == 0xfc00 && count < 2)) {
                    return FUXI_BT_PROTOCOL;
                }
                if (opcode != 0xfc00 || event[7] == rtype) {
                    if (event[6]) {
                        result->controller_status = event[6];
                        return FUXI_BT_CONTROLLER;
                    }
                    *params = event + 6;
                    *size = count;
                    return FUXI_BT_OK;
                }
            }
            if (event[1] == 0x0f && event[2] == 4 && fuxi_bt_setup_le16(event + 5) == opcode &&
                event[3]) {
                result->controller_status = event[3];
                return FUXI_BT_CONTROLLER;
            }
            status = fuxi_bt_link_other(link);
            if (status != FUXI_BT_OK) {
                return status;
            }
            continue;
        }
        if (status != FUXI_BT_AGAIN) {
            return status;
        }
        status = fuxi_bt_link_time(link);
        if (status != FUXI_BT_OK) {
            return status;
        }
        if (result->elapsed_us - begin >= FUXI_BT_COMMAND_TIMEOUT_US) {
            return FUXI_BT_TIMEOUT;
        }
        link->ops->wait_us(link->ops->context, 200);
    }
}

static enum fuxi_bt_status fuxi_bt_link_command(struct fuxi_bt_link *link, uint16_t opcode,
    const uint8_t *payload, uint8_t payload_size, uint8_t rtype,
    const uint8_t **params, unsigned *size)
{
    uint8_t packet[4 + 255];
    enum fuxi_bt_status status;
    packet[0] = 1;
    packet[1] = (uint8_t)(opcode & 0xff);
    packet[2] = (uint8_t)(opcode >> 8);
    packet[3] = payload_size;
    if (payload_size) {
        memcpy(packet + 4, payload, payload_size);
    }
    status = fuxi_bt_link_write(link, packet, 4u + payload_size);
    if (status != FUXI_BT_OK) {
        return status;
    }
    return fuxi_bt_link_wait(link, opcode, rtype, params, size);
}

/* rampatch 標頭：[type=1|len<<8] 後接 total_size、data_length、format、signature、download_mode、
 * reserved、product_id、rom_build、patch_version、reserved、entry（共 24 位元組）。ELF 格式不支援。 */
static enum fuxi_bt_status fuxi_bt_tlv_patch(const struct fuxi_bt_blob *blob,
    struct fuxi_bt_tlv_info *info)
{
    const uint8_t *data = blob->data;
    uint32_t type_len;
    info->file_bytes = (uint32_t)blob->size;
    if (blob->size < 28 || blob->size > FUXI_BT_PATCH_MAX) {
        return FUXI_BT_PROTOCOL;
    }
    if (!memcmp(data, "\177ELF", 4)) {
        return FUXI_BT_PROTOCOL;
    }
    type_len = fuxi_bt_setup_le32(data);
    info->type = (uint8_t)(type_len & 0xff);
    info->tlv_bytes = type_len >> 8;
    info->total_size = fuxi_bt_setup_le32(data + 4);
    info->download_mode = data[4 + 10];
    info->product = fuxi_bt_setup_le16(data + 4 + 12);
    info->rom = fuxi_bt_setup_le16(data + 4 + 14);
    info->patch = fuxi_bt_setup_le16(data + 4 + 16);
    if (info->type != 1 || info->tlv_bytes < 24 || info->tlv_bytes > blob->size - 4 ||
        info->download_mode > 3) {
        return FUXI_BT_PROTOCOL;
    }
    return FUXI_BT_OK;
}

/* NVM：[type=2|len<<8] 後接 tag（id u16、len u16、8 位元組保留、資料）。就地改 HCI 與 deep sleep tag。 */
static enum fuxi_bt_status fuxi_bt_tlv_nvm(struct fuxi_bt_blob *blob, struct fuxi_bt_tlv_info *info,
    uint8_t baud)
{
    uint8_t *data = blob->data;
    uint32_t type_len;
    size_t length, index = 0;
    info->file_bytes = (uint32_t)blob->size;
    if (blob->size < 16 || blob->size > FUXI_BT_NVM_MAX) {
        return FUXI_BT_PROTOCOL;
    }
    type_len = fuxi_bt_setup_le32(data);
    info->type = (uint8_t)(type_len & 0xff);
    info->tlv_bytes = type_len >> 8;
    length = info->tlv_bytes;
    if (info->type != 2 || length < 12 || length > blob->size - 4) {
        return FUXI_BT_PROTOCOL;
    }
    data += 4;
    /* Linux：while (idx < length - sizeof(struct tlv_type_nvm))；這裡另外檢查 tag 資料不超出範圍 */
    while (index + 12 < length) {
        const uint16_t tag = fuxi_bt_setup_le16(data + index);
        const uint16_t tag_length = fuxi_bt_setup_le16(data + index + 2);
        uint8_t *value = data + index + 12;
        if (tag_length > length - index - 12) {
            return FUXI_BT_PROTOCOL;
        }
        if (tag == 17 && tag_length >= 3 && !info->hci_tag) {
            info->hci_tag = true;
            memcpy(info->hci_before, value, 3);
            value[0] &= 0x7f;
            value[1] = baud;
            memcpy(info->hci_after, value, 3);
        } else if (tag == 27 && tag_length >= 1 && !info->sleep_tag) {
            info->sleep_tag = true;
            info->sleep_before = value[0];
            value[0] &= 0xfe;
            info->sleep_after = value[0];
        }
        if (++info->tags > 4096) {
            return FUXI_BT_PROTOCOL;
        }
        index += 12u + tag_length;
    }
    /* 沒有 HCI tag 就無法確定控制器下載後的 baud／IBS，寧可不送。 */
    return info->hci_tag ? FUXI_BT_OK : FUXI_BT_PROTOCOL;
}

static enum fuxi_bt_status fuxi_bt_send_tlv(struct fuxi_bt_link *link,
    const struct fuxi_bt_blob *blob, uint8_t mode, struct fuxi_bt_tlv_info *info)
{
    uint8_t payload[2 + FUXI_BT_TLV_SEGMENT];
    for (size_t offset = 0; offset < blob->size;) {
        const size_t remaining = blob->size - offset;
        const unsigned segment = remaining < FUXI_BT_TLV_SEGMENT ?
            (unsigned)remaining : FUXI_BT_TLV_SEGMENT;
        const bool last = segment == remaining;
        uint8_t packet[4 + sizeof(payload)];
        enum fuxi_bt_status status;
        payload[0] = 0x1e;
        payload[1] = (uint8_t)segment;
        memcpy(payload + 2, blob->data + offset, segment);
        packet[0] = 1;
        packet[1] = 0x00;
        packet[2] = 0xfc;
        packet[3] = (uint8_t)(segment + 2);
        memcpy(packet + 4, payload, segment + 2);
        status = fuxi_bt_link_write(link, packet, 6u + segment);
        if (status != FUXI_BT_OK) {
            return status;
        }
        info->segments++;
        if (last || mode == 0) {
            const uint8_t *params = NULL;
            unsigned size = 0;
            status = fuxi_bt_link_wait(link, 0xfc00, 0x1e, &params, &size);
            if (status == FUXI_BT_TIMEOUT && last && mode != 0 &&
                fuxi_bt_link_time(link) == FUXI_BT_OK) {
                /* 省略回應的模式下最後一段也可能不回（Linux 自己補 Command Complete）；
                 * 之後的 board ID／HCI reset 會證明控制器是否正常。 */
                info->final_ack = false;
            } else if (status != FUXI_BT_OK) {
                return status;
            } else {
                info->acks++;
                info->final_ack = last;
            }
        } else {
            status = fuxi_bt_link_poll(link);
            if (status != FUXI_BT_OK) {
                return status;
            }
        }
        offset += segment;
    }
    return FUXI_BT_OK;
}

static bool fuxi_bt_soft_failure(enum fuxi_bt_status status)
{
    return status == FUXI_BT_CONTROLLER || status == FUXI_BT_TIMEOUT || status == FUXI_BT_PROTOCOL;
}

/* 版本查詢：回傳參數 [cresp][0x19][長度 12][product 4][patch 2][rom 2][soc 4] */
static enum fuxi_bt_status fuxi_bt_setup_version(struct fuxi_bt_link *link,
    struct fuxi_bt_version *version)
{
    static const uint8_t request[] = {0x19};
    const uint8_t *params = NULL;
    unsigned size = 0;
    enum fuxi_bt_status status = fuxi_bt_link_command(link, 0xfc00, request, 1, 0x19, &params, &size);
    if (status != FUXI_BT_OK) {
        return status;
    }
    if (size < 15) {
        return FUXI_BT_PROTOCOL;
    }
    version->product = fuxi_bt_setup_le32(params + 3);
    version->patch = fuxi_bt_setup_le16(params + 7);
    version->rom = fuxi_bt_setup_le16(params + 9);
    version->soc = fuxi_bt_setup_le32(params + 11);
    return FUXI_BT_OK;
}

/* 換速前後以舊速率收到的原始位元組：不解析，只記數量與前 8 個（看控制器有沒有先回 Command Complete）。 */
static enum fuxi_bt_status fuxi_bt_link_collect(struct fuxi_bt_link *link)
{
    struct fuxi_bt_setup_result *result = link->result;
    uint8_t bytes[64];
    for (unsigned round = 0; round < 16; round++) {
        size_t count = 0;
        enum fuxi_bt_status status = link->ops->read(link->ops->context, bytes, sizeof(bytes), &count);
        if (count > sizeof(bytes) || (status != FUXI_BT_OK && count)) {
            return FUXI_BT_IO;
        }
        if (status == FUXI_BT_AGAIN || (status == FUXI_BT_OK && !count)) {
            return FUXI_BT_OK;
        }
        if (status != FUXI_BT_OK) {
            return status;
        }
        for (size_t index = 0; index < count; index++) {
            if (result->baud_early_size < sizeof(result->baud_early)) {
                result->baud_early[result->baud_early_size++] = bytes[index];
            }
        }
        result->baud_early_bytes += (unsigned)count;
        result->rx_bytes += (unsigned)count;
    }
    return FUXI_BT_OK;
}

/* Linux qca_set_baudrate：01 48 fc 01 <enum>，不等回應；送完後等 BAUD_SETTLE_US 才換 host 速率。 */
static enum fuxi_bt_status fuxi_bt_setup_rate_command(struct fuxi_bt_link *link, uint8_t qca)
{
    const uint8_t packet[] = {1, 0x48, 0xfc, 1, qca};
    enum fuxi_bt_status status = fuxi_bt_link_write(link, packet, sizeof(packet));
    if (status != FUXI_BT_OK) {
        return status;
    }
    for (unsigned step = 0; step < FUXI_BT_BAUD_SETTLE_US / 1000u; step++) {
        link->ops->wait_us(link->ops->context, 1000);
        status = fuxi_bt_link_collect(link);
        if (status != FUXI_BT_OK) {
            return status;
        }
    }
    return FUXI_BT_OK;
}

/* host 換速：歸還 UART（FIFO 關閉、暫存器還原）再以新速率取回。歸還失敗表示 FIFO 沒還原，不能繼續。 */
static enum fuxi_bt_status fuxi_bt_setup_reopen(struct fuxi_bt_link *link, uint32_t baud,
    bool *acquired)
{
    const struct fuxi_bt_uart_ops *ops = link->ops;
    enum fuxi_bt_status status;
    *acquired = false;
    status = ops->release(ops->context);
    if (status != FUXI_BT_OK) {
        link->result->cleanup = status == FUXI_BT_AGAIN ? FUXI_BT_TIMEOUT : status;
        return FUXI_BT_CLEANUP;
    }
    fuxi_bt_link_reset(link);
    status = ops->acquire(ops->context, baud);
    if (status != FUXI_BT_OK) {
        return status == FUXI_BT_AGAIN ? FUXI_BT_TIMEOUT : status;
    }
    *acquired = true;
    return FUXI_BT_OK;
}

static bool fuxi_bt_setup_noise(enum fuxi_bt_status status)
{
    return fuxi_bt_soft_failure(status) || status == FUXI_BT_IBS || status == FUXI_BT_BUDGET ||
        status == FUXI_BT_VERSION;
}

/* 以目前速率重查版本，必須和第一次相同；分框錯、IBS 位元組、超量與逾時都當成雜訊重試。 */
static enum fuxi_bt_status fuxi_bt_setup_verify(struct fuxi_bt_link *link,
    const struct fuxi_bt_version *expected)
{
    enum fuxi_bt_status status = FUXI_BT_TIMEOUT;
    for (unsigned attempt = 0; attempt < FUXI_BT_BAUD_VERIFY_ATTEMPTS; attempt++) {
        struct fuxi_bt_version version;
        link->result->baud_attempts++;
        if (attempt) {
            link->ops->wait_us(link->ops->context, FUXI_BT_BAUD_SETTLE_US);
        }
        fuxi_bt_link_reset(link);
        status = fuxi_bt_link_drain(link);
        if (status == FUXI_BT_OK) {
            memset(&version, 0, sizeof(version));
            status = fuxi_bt_setup_version(link, &version);
        }
        if (status == FUXI_BT_OK) {
            if (version.product == expected->product && version.soc == expected->soc &&
                version.rom == expected->rom && version.patch == expected->patch) {
                return FUXI_BT_OK;
            }
            status = FUXI_BT_VERSION;
        }
        if (!fuxi_bt_setup_noise(status) || fuxi_bt_link_time(link) != FUXI_BT_OK) {
            return status;
        }
    }
    return status;
}

/*
 * 換到 rate；確認不到就（盡力）以新速率送 0xfc48 回 115200、host 也回 115200 再確認一次，
 * 成功就以 115200 繼續（baud_status 記失敗原因）。退回也確認不到才算 setup 失敗。
 */
static enum fuxi_bt_status fuxi_bt_setup_baud(struct fuxi_bt_link *link,
    const struct fuxi_bt_rate *rate, bool *acquired)
{
    struct fuxi_bt_setup_result *result = link->result;
    const struct fuxi_bt_version expected = result->version;
    const struct fuxi_bt_rate *boot = fuxi_bt_rate_find(FUXI_BT_BOOT_BAUD);
    enum fuxi_bt_status status, failure;
    if (!boot) {
        return FUXI_BT_ARGUMENT;
    }
    status = fuxi_bt_setup_rate_command(link, rate->qca);
    if (status != FUXI_BT_OK) {
        return status;
    }
    status = fuxi_bt_setup_reopen(link, rate->baud, acquired);
    if (status == FUXI_BT_CLEANUP) {
        return status;
    }
    if (status == FUXI_BT_OK) {
        status = fuxi_bt_setup_verify(link, &expected);
        if (status == FUXI_BT_OK) {
            result->baud_status = FUXI_BT_OK;
            result->baud_active = rate->baud;
            return FUXI_BT_OK;
        }
        if (!fuxi_bt_setup_noise(status)) {
            return status;
        }
    }
    failure = status;
    if (*acquired) {
        /* 只有我方收不到時有用；控制器根本沒換速時，這幾個位元組對它只是雜訊 */
        status = fuxi_bt_setup_rate_command(link, boot->qca);
        if (status != FUXI_BT_OK && !fuxi_bt_setup_noise(status)) {
            return status;
        }
    }
    status = fuxi_bt_setup_reopen(link, boot->baud, acquired);
    if (status == FUXI_BT_OK) {
        status = fuxi_bt_setup_verify(link, &expected);
    }
    result->baud_fallback = status;
    result->baud_status = failure;
    if (status != FUXI_BT_OK) {
        return status;
    }
    result->baud_active = boot->baud;
    return FUXI_BT_OK;
}

enum fuxi_bt_status fuxi_bt_setup_run(const struct fuxi_bt_uart_ops *ops,
    const struct fuxi_bt_files *files, struct fuxi_bt_setup_result *result)
{
    const struct fuxi_bt_setup_options options = {0, false};
    return fuxi_bt_setup_run_options(ops, files, &options, result);
}

enum fuxi_bt_status fuxi_bt_setup_run_options(const struct fuxi_bt_uart_ops *ops,
    const struct fuxi_bt_files *files, const struct fuxi_bt_setup_options *options,
    struct fuxi_bt_setup_result *result)
{
    struct fuxi_bt_link link;
    struct fuxi_bt_blob patch = {NULL, 0}, nvm = {NULL, 0};
    const struct fuxi_bt_rate *rate, *active;
    const uint8_t *params = NULL;
    unsigned size = 0;
    bool acquired = false;
    enum fuxi_bt_status status;
    uint64_t mark;
    if (!result) {
        return FUXI_BT_ARGUMENT;
    }
    memset(result, 0, sizeof(*result));
    result->board_id_status = FUXI_BT_OFF;
    result->build_info_status = FUXI_BT_OFF;
    result->baud_status = FUXI_BT_OFF;
    result->baud_fallback = FUXI_BT_OFF;
    result->nolog_status = FUXI_BT_OFF;
    result->baud_active = FUXI_BT_BOOT_BAUD;
    if (!fuxi_bt_uart_ops_valid(ops)) {
        result->status = FUXI_BT_NO_UART;
        return result->status;
    }
    if (!files || !files->load || !files->release || !options) {
        result->status = FUXI_BT_ARGUMENT;
        return result->status;
    }
    result->baud_requested = options->baud ? options->baud : FUXI_BT_BOOT_BAUD;
    rate = fuxi_bt_rate_find(result->baud_requested);
    if (!rate) {
        result->status = FUXI_BT_ARGUMENT;
        return result->status;
    }
    memset(&link, 0, sizeof(link));
    link.ops = ops;
    link.result = result;
    link.start = ops->now_us(ops->context);
    status = ops->acquire(ops->context, FUXI_BT_BOOT_BAUD);
    if (status != FUXI_BT_OK) {
        goto finish;
    }
    acquired = true;
    status = fuxi_bt_link_drain(&link);
    if (status != FUXI_BT_OK) {
        goto finish;
    }

    result->stage = FUXI_BT_SETUP_VERSION;
    status = fuxi_bt_setup_version(&link, &result->version);
    if (status != FUXI_BT_OK) {
        goto finish;
    }
    if (!result->version.soc || !result->version.rom) {
        status = FUXI_BT_VERSION;
        goto finish;
    }
    result->rom_ver = (uint8_t)(((result->version.soc & 0xf00u) >> 4) | (result->version.soc & 0xfu));

    if (rate->baud != FUXI_BT_BOOT_BAUD) {
        result->stage = FUXI_BT_SETUP_BAUD;
        mark = result->elapsed_us;
        status = fuxi_bt_setup_baud(&link, rate, &acquired);
        (void)fuxi_bt_link_time(&link);
        result->baud_us = result->elapsed_us - mark;
        if (status != FUXI_BT_OK) {
            goto finish;
        }
    }
    active = fuxi_bt_rate_find(result->baud_active);
    if (!active) {
        status = FUXI_BT_ARGUMENT;
        goto finish;
    }

    result->stage = FUXI_BT_SETUP_PATCH_LOAD;
    fuxi_bt_setup_name(result->patch_name, "hmtbtfw", result->rom_ver, NULL, 0, 0, ".tlv");
    status = files->load(files->context, result->patch_name, FUXI_BT_PATCH_MAX, &patch);
    if (status != FUXI_BT_OK) {
        patch.data = NULL;
        goto finish;
    }
    if (!patch.data || !patch.size || patch.size > FUXI_BT_PATCH_MAX) {
        status = FUXI_BT_IO;
        goto finish;
    }
    result->stage = FUXI_BT_SETUP_PATCH_PARSE;
    status = fuxi_bt_tlv_patch(&patch, &result->patch);
    if (status != FUXI_BT_OK) {
        goto finish;
    }
    result->stage = FUXI_BT_SETUP_PATCH_SEND;
    mark = result->elapsed_us;
    status = fuxi_bt_send_tlv(&link, &patch, result->patch.download_mode, &result->patch);
    result->patch_us = result->elapsed_us - mark;
    files->release(files->context, &patch);
    patch.data = NULL;
    if (status != FUXI_BT_OK) {
        goto finish;
    }
    /* Linux：給控制器 10 ms 準備接 NVM */
    ops->wait_us(ops->context, 10000);

    result->stage = FUXI_BT_SETUP_BOARD_ID;
    {
        static const uint8_t request[] = {0x23};
        status = fuxi_bt_link_command(&link, 0xfc00, request, 1, 0x23, &params, &size);
    }
    result->board_id_status = status;
    if (status == FUXI_BT_OK && size >= 5) {
        result->board_id = (uint16_t)((uint16_t)params[3] << 8 | params[4]);
        result->board_id_valid = true;
    } else if (status != FUXI_BT_OK && !fuxi_bt_soft_failure(status)) {
        goto finish;
    }

    result->stage = FUXI_BT_SETUP_NVM_LOAD;
    status = FUXI_BT_OFF;
    for (unsigned candidate = 0; candidate < 2 && status == FUXI_BT_OFF; candidate++) {
        if (candidate == 0) {
            if (!result->board_id_valid || result->board_id == 0 || result->board_id == 0xffffu) {
                continue;
            }
            /*
             * Linux qca_get_nvm_name_by_board 的 "%s%02x.b%02x"：bid 至少兩位、超過照實際位數
             * （0x0a→b0a、0x10a→b10a）。2026-11-18 對照手機 /vendor/bt_firmware/image 的
             * hmtnv20.b0a／b10a／b14 等檔名。
             */
            fuxi_bt_setup_name(result->nvm_name, "hmtnv", result->rom_ver, ".b", result->board_id, 2, NULL);
        } else {
            /* 板級檔不存在、board ID 為 0／0xffff 或讀不到：Linux qca_get_alt_nvm_file 改讀 .bin */
            fuxi_bt_setup_name(result->nvm_name, "hmtnv", result->rom_ver, NULL, 0, 0, ".bin");
        }
        status = files->load(files->context, result->nvm_name, FUXI_BT_NVM_MAX, &nvm);
        if (status != FUXI_BT_OK) {
            nvm.data = NULL;
        }
    }
    if (status != FUXI_BT_OK) {
        goto finish;
    }
    if (!nvm.data || !nvm.size || nvm.size > FUXI_BT_NVM_MAX) {
        status = FUXI_BT_IO;
        goto finish;
    }
    result->stage = FUXI_BT_SETUP_NVM_PARSE;
    status = fuxi_bt_tlv_nvm(&nvm, &result->nvm, active->qca);
    if (status != FUXI_BT_OK) {
        goto finish;
    }
    result->stage = FUXI_BT_SETUP_NVM_SEND;
    status = fuxi_bt_send_tlv(&link, &nvm, 0, &result->nvm);
    files->release(files->context, &nvm);
    nvm.data = NULL;
    if (status != FUXI_BT_OK) {
        goto finish;
    }

    if (options->disable_logging) {
        /* Linux qca_uart_setup：WCN7850 在 NVM 之後、HCI reset 之前關控制器日誌；失敗只記錄 */
        static const uint8_t request[] = {0x14, 0x00};
        result->stage = FUXI_BT_SETUP_NOLOG;
        status = fuxi_bt_link_command(&link, 0xfc17, request, sizeof(request), 0, &params, &size);
        result->nolog_status = status;
        if (status != FUXI_BT_OK && !fuxi_bt_soft_failure(status)) {
            goto finish;
        }
    }

    result->stage = FUXI_BT_SETUP_BUILD_INFO;
    {
        static const uint8_t request[] = {0x20};
        status = fuxi_bt_link_command(&link, 0xfc00, request, 1, 0x20, &params, &size);
    }
    result->build_info_status = status;
    if (status == FUXI_BT_OK && size >= 3) {
        unsigned length = params[2];
        unsigned out = 0;
        if (length > size - 3) {
            length = size - 3;
        }
        for (unsigned index = 0; index < length && out + 1 < sizeof(result->build_info); index++) {
            const uint8_t c = params[3 + index];
            if (!c) {
                break;
            }
            result->build_info[out++] = (c >= 0x20 && c < 0x7f) ? (char)c : '?';
        }
    } else if (status != FUXI_BT_OK && !fuxi_bt_soft_failure(status)) {
        goto finish;
    }

    result->stage = FUXI_BT_SETUP_RESET;
    status = fuxi_bt_link_command(&link, 0x0c03, NULL, 0, 0, &params, &size);
    if (status != FUXI_BT_OK) {
        goto finish;
    }

    result->stage = FUXI_BT_SETUP_LOCAL_VERSION;
    status = fuxi_bt_link_command(&link, 0x1001, NULL, 0, 0, &params, &size);
    if (status != FUXI_BT_OK) {
        goto finish;
    }
    if (size < 9) {
        status = FUXI_BT_PROTOCOL;
        goto finish;
    }
    result->hci_version = params[1];
    result->hci_revision = fuxi_bt_setup_le16(params + 2);
    result->lmp_version = params[4];
    result->manufacturer = fuxi_bt_setup_le16(params + 5);
    result->lmp_subversion = fuxi_bt_setup_le16(params + 7);
    result->local_version_valid = true;

    result->stage = FUXI_BT_SETUP_BDADDR;
    status = fuxi_bt_link_command(&link, 0x1009, NULL, 0, 0, &params, &size);
    if (status != FUXI_BT_OK) {
        goto finish;
    }
    if (size < 7) {
        status = FUXI_BT_PROTOCOL;
        goto finish;
    }
    memcpy(result->bdaddr, params + 1, sizeof(result->bdaddr));
    result->bdaddr_valid = true;
    result->stage = FUXI_BT_SETUP_DONE;
    status = FUXI_BT_OK;

finish:
    if (patch.data) {
        files->release(files->context, &patch);
    }
    if (nvm.data) {
        files->release(files->context, &nvm);
    }
    if (status == FUXI_BT_AGAIN) {
        status = FUXI_BT_IO;
    }
    result->status = status;
    if (acquired) {
        result->cleanup = ops->release(ops->context);
        if (result->status == FUXI_BT_OK && result->cleanup != FUXI_BT_OK) {
            result->status = FUXI_BT_CLEANUP;
        }
    }
    return result->status;
}
