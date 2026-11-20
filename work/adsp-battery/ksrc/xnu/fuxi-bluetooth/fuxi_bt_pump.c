#include "fuxi_bt_pump.h"
#include <string.h>

/* 常駐收送泵與 H4 追蹤器，說明見 fuxi_bt_pump.h。 */

const char *fuxi_bt_h4_type_name(uint8_t type)
{
    switch (type) {
    case FUXI_BT_H4_COMMAND:
        return "cmd";
    case FUXI_BT_H4_ACL:
        return "acl";
    case FUXI_BT_H4_SCO:
        return "sco";
    case FUXI_BT_H4_EVENT:
        return "evt";
    case FUXI_BT_H4_ISO:
        return "iso";
    default:
        return "?";
    }
}

static unsigned fuxi_bt_h4_header_size(uint8_t type)
{
    switch (type) {
    case FUXI_BT_H4_COMMAND:
    case FUXI_BT_H4_SCO:
        return 3;
    case FUXI_BT_H4_ACL:
    case FUXI_BT_H4_ISO:
        return 4;
    case FUXI_BT_H4_EVENT:
        return 2;
    default:
        return 0;
    }
}

void fuxi_bt_h4_parser_init(struct fuxi_bt_h4_parser *parser, uint8_t direction)
{
    if (!parser) {
        return;
    }
    memset(parser, 0, sizeof(*parser));
    parser->direction = direction;
}

static void fuxi_bt_h4_begin(struct fuxi_bt_h4_parser *parser)
{
    struct fuxi_bt_h4_record *record = &parser->record;
    const uint8_t *header = parser->header;
    memset(record, 0, sizeof(*record));
    record->direction = parser->direction;
    record->type = parser->type;
    switch (parser->type) {
    case FUXI_BT_H4_COMMAND:
        record->opcode = (uint16_t)(header[0] | (header[1] << 8));
        record->has_opcode = true;
        record->length = header[2];
        break;
    case FUXI_BT_H4_ACL:
        record->handle = (uint16_t)((header[0] | (header[1] << 8)) & 0xfff);
        record->length = (uint16_t)(header[2] | (header[3] << 8));
        break;
    case FUXI_BT_H4_SCO:
        record->handle = (uint16_t)((header[0] | (header[1] << 8)) & 0xfff);
        record->length = header[2];
        break;
    case FUXI_BT_H4_EVENT:
        record->event = header[0];
        record->length = header[1];
        break;
    case FUXI_BT_H4_ISO:
        record->handle = (uint16_t)((header[0] | (header[1] << 8)) & 0xfff);
        record->length = (uint16_t)((header[2] | (header[3] << 8)) & 0x3fff);
        break;
    default:
        break;
    }
    parser->remaining = record->length;
}

bool fuxi_bt_link_is_inquiry(const struct fuxi_bt_h4_record *record)
{
    if (record->has_opcode) {
        return record->opcode == 0x0401u || record->opcode == 0x0402u;
    }
    if (record->direction != FUXI_BT_H4_TO_HOST || record->type != FUXI_BT_H4_EVENT) {
        return false;
    }
    return record->event == 0x01u || record->event == 0x02u ||
           record->event == 0x22u || record->event == 0x2fu;
}

bool fuxi_bt_link_detail(const struct fuxi_bt_h4_record *record, int *value)
{
    *value = -1;
    if (record->has_opcode) {
        switch (record->opcode) {
        case 0x0401: case 0x0402: /* inquiry / cancel */
        case 0x0405: case 0x0406: case 0x0408: /* create / disconnect / cancel */
        case 0x0409: case 0x040a: /* accept / reject incoming connection */
        case 0x040b: case 0x040c: /* link key reply / negative reply */
        case 0x0411: case 0x0413: case 0x0419: /* authenticate / encrypt / remote name */
        case 0x042b: case 0x042c: case 0x042d: /* IO cap / confirmation replies */
        case 0x042e: case 0x042f: case 0x0430: /* passkey / OOB replies */
        case 0x0433: case 0x0434: /* OOB / IO cap negative replies */
        case 0x0c1a: /* Write Scan Enable */
        case 0x0c55: case 0x0c56: /* Read / Write Simple Pairing Mode */
            if (record->direction == FUXI_BT_H4_TO_HOST && record->has_status) {
                *value = record->status;
            } else if (record->direction == FUXI_BT_H4_TO_CONTROLLER && record->opcode == 0x0c1a &&
                       record->length == 1 && record->keep_size == 1 && record->keep[0] <= 3) {
                *value = record->keep[0];
            } else if (record->direction == FUXI_BT_H4_TO_CONTROLLER && record->opcode == 0x0c56 &&
                       record->length == 1 && record->keep_size == 1 && record->keep[0] <= 1) {
                *value = record->keep[0];
            } else if (record->direction == FUXI_BT_H4_TO_CONTROLLER && record->opcode == 0x0413 &&
                       record->length == 3 && record->keep_size >= 3 && record->keep[2] <= 1) {
                *value = record->keep[2];
            }
            return true;
        default:
            return false;
        }
    }
    if (record->direction != FUXI_BT_H4_TO_HOST || record->type != FUXI_BT_H4_EVENT) return false;
    switch (record->event) {
    case 0x01: case 0x03: case 0x06: case 0x07: case 0x10: case 0x30: case 0x36:
        if (record->keep_size) *value = record->keep[0]; /* HCI status or hw error at byte 0 */
        return true;
    case 0x05: case 0x08: /* Disconnection Complete (reason) / Encryption Change (enabled) */
        if (record->keep_size >= 4) {
            *value = ((int)record->keep[0] << 8) | record->keep[3];
        }
        return true;
    case 0x02: case 0x22: case 0x2f: /* inquiry results */
    case 0x04: case 0x16: case 0x17: case 0x18: /* incoming link, PIN or link key */
    case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: /* pairing stages */
        return true; /* never expose a remote address, key or displayed code */
    default:
        return false;
    }
}

static void fuxi_bt_h4_finish(struct fuxi_bt_h4_parser *parser,
    fuxi_bt_h4_trace_fn trace, void *context)
{
    struct fuxi_bt_h4_record *record = &parser->record;
    if (record->type == FUXI_BT_H4_EVENT) {
        if (record->event == 0x0e && record->keep_size >= 3) {
            /* Command Complete：num_cmds、opcode、（有回傳參數時）status */
            record->opcode = (uint16_t)(record->keep[1] | (record->keep[2] << 8));
            record->has_opcode = true;
            if (record->keep_size >= 4) {
                record->status = record->keep[3];
                record->has_status = true;
            }
        } else if (record->event == 0x0f && record->keep_size >= 4) {
            /* Command Status：status、num_cmds、opcode */
            record->status = record->keep[0];
            record->has_status = true;
            record->opcode = (uint16_t)(record->keep[2] | (record->keep[3] << 8));
            record->has_opcode = true;
        } else if (record->event == 0x3e && record->keep_size >= 1) {
            record->subevent = record->keep[0];
            if (record->subevent == 0x02 || record->subevent == 0x0b ||
                record->subevent == 0x0d) {
                parser->le_adv_reports++;
            }
        }
    }
    parser->packets[record->type]++;
    if (trace) {
        trace(context, record);
    }
    parser->type = 0;
    parser->header_have = 0;
    parser->header_need = 0;
    parser->remaining = 0;
}

void fuxi_bt_h4_feed(struct fuxi_bt_h4_parser *parser, const uint8_t *bytes, size_t size,
    fuxi_bt_h4_trace_fn trace, void *context)
{
    size_t index = 0;
    if (!parser || (!bytes && size)) {
        return;
    }
    parser->bytes += size;
    while (index < size) {
        if (!parser->type) {
            const uint8_t type = bytes[index++];
            /* 控制器不會送 Command：RX 方向的 0x01 是殘留資料，不當封包開頭 */
            const unsigned need = parser->direction == FUXI_BT_H4_TO_HOST && type == FUXI_BT_H4_COMMAND ?
                0 : fuxi_bt_h4_header_size(type);
            if (!need) {
                parser->desync_bytes++;
                continue;
            }
            parser->type = type;
            parser->header_need = need;
            parser->header_have = 0;
            continue;
        }
        if (parser->header_have < parser->header_need) {
            if (parser->direction == FUXI_BT_H4_TO_HOST && parser->type == FUXI_BT_H4_EVENT &&
                !parser->header_have && !bytes[index]) {
                /* event code 0 保留不用：與分框器相同，類型 byte 算 desync，這個 byte 重新當類型看 */
                parser->desync_bytes++;
                parser->type = 0;
                continue;
            }
            parser->header[parser->header_have++] = bytes[index++];
            if (parser->header_have == parser->header_need) {
                fuxi_bt_h4_begin(parser);
                if (!parser->remaining) {
                    fuxi_bt_h4_finish(parser, trace, context);
                }
            }
            continue;
        }
        {
            struct fuxi_bt_h4_record *record = &parser->record;
            size_t chunk = size - index;
            size_t kept = 0;
            if (chunk > parser->remaining) {
                chunk = parser->remaining;
            }
            while (kept < chunk && record->keep_size < FUXI_BT_H4_KEEP) {
                record->keep[record->keep_size++] = bytes[index + kept++];
            }
            index += chunk;
            parser->remaining -= (uint32_t)chunk;
            if (!parser->remaining) {
                fuxi_bt_h4_finish(parser, trace, context);
            }
        }
    }
}

enum fuxi_bt_status fuxi_bt_pump_init(struct fuxi_bt_pump *pump,
    const struct fuxi_bt_uart_ops *ops, const struct fuxi_bt_pump_sink *sink,
    fuxi_bt_h4_trace_fn trace, void *trace_context)
{
    if (!pump) {
        return FUXI_BT_ARGUMENT;
    }
    memset(pump, 0, sizeof(*pump));
    if (!ops || !ops->read || !ops->write || !sink || !sink->deliver || !sink->take) {
        pump->error = FUXI_BT_ARGUMENT;
        return FUXI_BT_ARGUMENT;
    }
    pump->ops = *ops;
    pump->sink = *sink;
    pump->trace = trace;
    pump->trace_context = trace_context;
    fuxi_bt_h4_parser_init(&pump->to_controller, FUXI_BT_H4_TO_CONTROLLER);
    fuxi_bt_h4_parser_init(&pump->to_host, FUXI_BT_H4_TO_HOST);
    return FUXI_BT_OK;
}

static enum fuxi_bt_status fuxi_bt_pump_fail(struct fuxi_bt_pump *pump, enum fuxi_bt_status status)
{
    if (pump->error == FUXI_BT_OK) {
        pump->error = status;
    }
    return pump->error;
}

/* packet[0] 是類型 byte，header 已齊 */
static uint32_t fuxi_bt_h4_payload(const uint8_t *packet)
{
    switch (packet[0]) {
    case FUXI_BT_H4_COMMAND:
    case FUXI_BT_H4_SCO:
        return packet[3];
    case FUXI_BT_H4_ACL:
        return (uint32_t)(packet[3] | (packet[4] << 8));
    case FUXI_BT_H4_EVENT:
        return packet[2];
    case FUXI_BT_H4_ISO:
        return (uint32_t)((packet[3] | (packet[4] << 8)) & 0x3fff);
    default:
        return 0;
    }
}

static void fuxi_bt_framer_classify(struct fuxi_bt_h4_framer *framer)
{
    const uint8_t *header = framer->header;
    framer->skip = header[0] == FUXI_BT_H4_ACL &&
        (uint16_t)(header[1] | (header[2] << 8)) == FUXI_BT_QCA_DEBUG_HANDLE;
    if (framer->skip) {
        framer->debug_packets++;
    } else {
        framer->packets++;
    }
}

static void fuxi_bt_pump_queue_ctl(struct fuxi_bt_pump *pump, uint8_t byte)
{
    if (pump->ctl_size < sizeof(pump->ctl)) {
        pump->ctl[pump->ctl_size++] = byte;
    }
}

/* 封包邊界上的非 H4 byte：IBS 開啟時處理控制器的 WAKE_IND／WAKE_ACK／SLEEP_IND（不計入 desync）；
 * 其餘非 H4 byte 或 IBS 關閉時的 0xfc–0xfe 計入 desync 並留樣本 */
static void fuxi_bt_pump_desync(struct fuxi_bt_pump *pump, uint8_t byte)
{
    struct fuxi_bt_h4_framer *framer = &pump->host;
    if (byte >= FUXI_BT_IBS_WAKE_ACK && byte <= FUXI_BT_IBS_SLEEP_IND) {
        framer->ibs_bytes[byte - FUXI_BT_IBS_WAKE_ACK]++;
        if (pump->ibs) {
            if (byte == FUXI_BT_IBS_WAKE_IND) {
                /* 控制器要送資料：回 WAKE_ACK（hci_qca device_want_to_wakeup） */
                fuxi_bt_pump_queue_ctl(pump, FUXI_BT_IBS_WAKE_ACK);
            } else if (byte == FUXI_BT_IBS_WAKE_ACK) {
                /* 主機的 WAKE_IND 得到回應 */
                pump->ibs_tx_awake = true;
            }
            return;
        }
    }
    framer->desync_bytes++;
    framer->desync_recent[framer->desync_recent_pos++ % sizeof(framer->desync_recent)] = byte;
}

/* opcode 清單比對；0xfc00 代表所有 vendor（OGF 0x3f）命令 */
static bool fuxi_bt_opcode_listed(const uint16_t *list, unsigned count, unsigned cap, uint16_t opcode)
{
    for (unsigned index = 0; index < count && index < cap; index++) {
        if (list[index] == opcode ||
            (list[index] == FUXI_BT_OPTIONAL_VENDOR && (opcode >> 10) == 0x3f)) {
            return true;
        }
    }
    return false;
}

static bool fuxi_bt_pump_optional(const struct fuxi_bt_pump *pump, uint16_t opcode)
{
    return fuxi_bt_opcode_listed(pump->optional, pump->optional_count, FUXI_BT_OPTIONAL_MAX, opcode);
}

/* TX 追蹤器每完成一個封包呼叫一次：記下 fuxi,bt-pad 清單內命令的第一個參數 byte，再轉給外部 trace */
static void fuxi_bt_pump_trace_tx(void *context, const struct fuxi_bt_h4_record *record)
{
    struct fuxi_bt_pump *pump = context;
    if (pump->pad_count && record->type == FUXI_BT_H4_COMMAND && record->has_opcode &&
        fuxi_bt_opcode_listed(pump->pad, pump->pad_count, FUXI_BT_PAD_MAX, record->opcode)) {
        const unsigned slot = pump->pad_tx_pos++ % FUXI_BT_PAD_TX_SLOTS;
        pump->pad_tx_opcode[slot] = record->opcode;
        pump->pad_tx_subop[slot] = record->keep_size ? record->keep[0] : 0;
    }
    if (pump->trace) {
        pump->trace(pump->trace_context, record);
    }
}

/* 最近一筆同 opcode 命令的第一個參數 byte；找不到回 0 */
static uint8_t fuxi_bt_pump_pad_subop(const struct fuxi_bt_pump *pump, uint16_t opcode)
{
    for (unsigned back = 1; back <= FUXI_BT_PAD_TX_SLOTS && back <= pump->pad_tx_pos; back++) {
        const unsigned slot = (pump->pad_tx_pos - back) % FUXI_BT_PAD_TX_SLOTS;
        if (pump->pad_tx_opcode[slot] == opcode) {
            return pump->pad_tx_subop[slot];
        }
    }
    return 0;
}

/* header 剛收齊：fuxi,bt-pad 開啟且這是只有 status 的 Command Complete 時，改為整包收進 hold[]。
 * 呼叫後 header 已在 hold[]，header 不再另外交出，remaining 只剩 payload。 */
static bool fuxi_bt_pump_hold_begin(struct fuxi_bt_pump *pump)
{
    struct fuxi_bt_h4_framer *framer = &pump->host;
    const uint8_t *header = framer->header;
    if (!pump->pad_count || framer->skip || header[0] != FUXI_BT_H4_EVENT ||
        header[1] != FUXI_BT_EVENT_COMMAND_COMPLETE || fuxi_bt_h4_payload(header) != 4) {
        return false;
    }
    memcpy(framer->hold, header, framer->header_need);
    framer->hold_size = framer->header_need;
    framer->hold_sent = 0;
    framer->hold_active = true;
    framer->header_sent = framer->header_need;
    framer->remaining = 4;
    return true;
}

/* hold[] 收齊（04 0e 04 ncmd op_lo op_hi status）：先照 fuxi,bt-optional 改 status，再照 fuxi,bt-pad 補資料 */
static void fuxi_bt_pump_hold_finish(struct fuxi_bt_pump *pump)
{
    struct fuxi_bt_h4_framer *framer = &pump->host;
    uint8_t *payload = framer->hold + framer->header_need;
    const uint16_t opcode = (uint16_t)(payload[1] | (payload[2] << 8));
    if (payload[3] && fuxi_bt_pump_optional(pump, opcode)) {
        pump->optional_patched++;
        pump->optional_last_opcode = opcode;
        pump->optional_last_status = payload[3];
        payload[3] = 0;
    }
    if (fuxi_bt_opcode_listed(pump->pad, pump->pad_count, FUXI_BT_PAD_MAX, opcode) &&
        framer->hold_size + FUXI_BT_PAD_BYTES <= sizeof(framer->hold)) {
        const uint8_t subop = fuxi_bt_pump_pad_subop(pump, opcode);
        framer->hold[framer->header_need - 1] = 4 + FUXI_BT_PAD_BYTES;
        framer->hold[framer->hold_size++] = subop;
        framer->hold[framer->hold_size++] = 0;
        pump->padded++;
        pump->pad_last_opcode = opcode;
        pump->pad_last_subop = subop;
    }
}

/* fuxi,bt-mask：回傳參數第 param 個 byte（status＝0）照規則清位元 */
static void fuxi_bt_pump_mask(struct fuxi_bt_pump *pump, uint16_t opcode, uint32_t param, uint8_t *byte)
{
    for (unsigned index = 0; index < pump->mask_count && index < FUXI_BT_MASK_MAX; index++) {
        const uint32_t rule = pump->mask[index];
        const uint8_t clear = (uint8_t)rule;
        if ((uint16_t)(rule >> 16) != opcode || ((rule >> 8) & 0xffu) != param || !(*byte & clear)) {
            continue;
        }
        pump->masked++;
        pump->mask_last_opcode = opcode;
        pump->mask_last_offset = (uint8_t)param;
        pump->mask_last_before = *byte;
        *byte = (uint8_t)(*byte & ~clear);
        pump->mask_last_after = *byte;
    }
}

/* fuxi,bt-txmask：剛從上層取到的 TX bytes 寫出前，依 H4 封包邊界找出命令參數 byte，照規則就地清位元。
 * 每個 byte 只經過這裡一次（取到時），所以 FIFO 回 AGAIN 重寫不會重算；非命令封包整段略過。 */
static void fuxi_bt_pump_txmask(struct fuxi_bt_pump *pump, uint8_t *bytes, size_t size)
{
    size_t index = 0;
    while (index < size) {
        if (!pump->txw_type) {
            const uint8_t type = bytes[index++];
            pump->txw_header_need = fuxi_bt_h4_header_size(type);
            pump->txw_header_have = 0;
            pump->txw_type = pump->txw_header_need ? type : 0;
            continue;
        }
        if (pump->txw_header_have < pump->txw_header_need) {
            pump->txw_header[pump->txw_header_have++] = bytes[index++];
            if (pump->txw_header_have == pump->txw_header_need) {
                uint8_t packet[5];
                packet[0] = pump->txw_type;
                memcpy(packet + 1, pump->txw_header, pump->txw_header_need);
                pump->txw_remaining = fuxi_bt_h4_payload(packet);
                pump->txw_opcode = (uint16_t)(pump->txw_header[0] | (pump->txw_header[1] << 8));
                pump->txw_param = 0;
                if (!pump->txw_remaining) {
                    pump->txw_type = 0;
                }
            }
            continue;
        }
        if (pump->txw_type == FUXI_BT_H4_COMMAND) {
            uint8_t *byte = &bytes[index++];
            for (unsigned rule_index = 0; rule_index < pump->txmask_count && rule_index < FUXI_BT_TXMASK_MAX;
                rule_index++) {
                const uint32_t rule = pump->txmask[rule_index];
                const uint8_t clear = (uint8_t)rule;
                if ((uint16_t)(rule >> 16) != pump->txw_opcode || ((rule >> 8) & 0xffu) != pump->txw_param ||
                    !(*byte & clear)) {
                    continue;
                }
                pump->txmasked++;
                pump->txmask_last_opcode = pump->txw_opcode;
                pump->txmask_last_offset = (uint8_t)pump->txw_param;
                pump->txmask_last_before = *byte;
                *byte = (uint8_t)(*byte & ~clear);
                pump->txmask_last_after = *byte;
            }
            pump->txw_param++;
            pump->txw_remaining--;
        } else {
            size_t chunk = size - index;
            if (chunk > pump->txw_remaining) {
                chunk = pump->txw_remaining;
            }
            index += chunk;
            pump->txw_remaining -= (uint32_t)chunk;
        }
        if (!pump->txw_remaining) {
            pump->txw_type = 0;
        }
    }
}

/* 即將從 rx[] 交出的 bytes[0..size)：Command Complete（04 0e len ncmd op_lo op_hi status ...）的
 * opcode 在 fuxi,bt-optional 清單內且 status 非 0 時就地改成 0；fuxi,bt-mask 規則清回傳參數位元。
 * 封包可能分好幾塊交出，opcode 先記在 framer；交不出去的 bytes 下一輪會再經過這裡，改寫結果不變。 */
static void fuxi_bt_pump_patch(struct fuxi_bt_pump *pump, uint8_t *bytes, size_t size)
{
    struct fuxi_bt_h4_framer *framer = &pump->host;
    const uint8_t *header = framer->header;
    uint32_t payload, offset;
    if ((!pump->optional_count && !pump->mask_count) || framer->skip ||
        header[0] != FUXI_BT_H4_EVENT || header[1] != FUXI_BT_EVENT_COMMAND_COMPLETE) {
        return;
    }
    payload = fuxi_bt_h4_payload(header);
    if (payload < 4 || framer->remaining > framer->header_need + payload) {
        return;
    }
    /* 封包內位置：header_need（類型＋header）之後是 payload */
    offset = framer->header_need + payload - framer->remaining;
    for (size_t index = 0; index < size; index++, offset++) {
        const uint16_t opcode = (uint16_t)(framer->cc_opcode[0] | (framer->cc_opcode[1] << 8));
        if (offset == framer->header_need + 1 || offset == framer->header_need + 2) {
            framer->cc_opcode[offset - framer->header_need - 1] = bytes[index];
        } else if (offset == framer->header_need + 3) {
            if (bytes[index] && !framer->cc_patched && fuxi_bt_pump_optional(pump, opcode)) {
                pump->optional_patched++;
                pump->optional_last_opcode = opcode;
                pump->optional_last_status = bytes[index];
                framer->cc_patched = true;
                bytes[index] = 0;
            }
            if (!pump->mask_count) {
                break;
            }
        } else if (offset > framer->header_need + 3) {
            fuxi_bt_pump_mask(pump, opcode, offset - framer->header_need - 3, &bytes[index]);
        }
    }
}

static size_t fuxi_bt_pump_offer(struct fuxi_bt_pump *pump, const uint8_t *bytes, size_t size,
    struct fuxi_bt_pump_activity *activity)
{
    struct fuxi_bt_h4_framer *framer = &pump->host;
    size_t taken;
    if (framer->skip) {
        framer->debug_bytes += size;
        return size;
    }
    taken = pump->sink.deliver(pump->sink.context, bytes, size, framer->start);
    if (taken > size) {
        taken = size;
    }
    if (taken) {
        framer->start = false;
        pump->rx_delivered += taken;
        activity->rx_delivered += (unsigned)taken;
    }
    return taken;
}

/* 分框器回到封包邊界（封包結束、recover、新連線共用） */
static void fuxi_bt_framer_reset(struct fuxi_bt_h4_framer *framer)
{
    framer->header_need = 0;
    framer->header_have = 0;
    framer->header_sent = 0;
    framer->remaining = 0;
    framer->skip = false;
    framer->start = false;
    framer->cc_patched = false;
    framer->hold_active = false;
    framer->hold_size = 0;
    framer->hold_sent = 0;
}

/* 控制器 → 主機只會有 ACL／SCO／Event／ISO；event code 0 保留不用。不合格的 header 當 desync。 */
static bool fuxi_bt_framer_header_valid(const uint8_t *header)
{
    return header[0] != FUXI_BT_H4_EVENT || header[1] != 0;
}

static void fuxi_bt_pump_deliver(struct fuxi_bt_pump *pump, struct fuxi_bt_pump_activity *activity)
{
    struct fuxi_bt_h4_framer *framer = &pump->host;
    for (;;) {
        const uint8_t *bytes = pump->rx + pump->rx_offset;
        const size_t available = pump->rx_size - pump->rx_offset;
        size_t taken;
        if (!framer->header_need) {
            /* 封包邊界 */
            unsigned need;
            if (!available) {
                break;
            }
            need = bytes[0] == FUXI_BT_H4_COMMAND ? 0 : fuxi_bt_h4_header_size(bytes[0]);
            if (!need || (available > need && !fuxi_bt_framer_header_valid(bytes))) {
                fuxi_bt_pump_desync(pump, bytes[0]);
                pump->rx_offset++;
                continue;
            }
            framer->header_need = need + 1;
            framer->start = true;
            if (available >= framer->header_need) {
                /* header 整段在這一塊：與 payload 一起從 RX 塊交出 */
                memcpy(framer->header, bytes, framer->header_need);
                framer->header_have = framer->header_sent = framer->header_need;
                framer->remaining = framer->header_need + fuxi_bt_h4_payload(framer->header);
                fuxi_bt_framer_classify(framer);
                if (fuxi_bt_pump_hold_begin(pump)) {
                    pump->rx_offset += framer->header_need;
                }
            } else {
                memcpy(framer->header, bytes, available);
                framer->header_have = (unsigned)available;
                framer->header_sent = 0;
                pump->rx_offset += (unsigned)available;
            }
            continue;
        }
        if (framer->header_have < framer->header_need) {
            /* header 跨塊：收齊後才決定這個封包 */
            size_t copy = framer->header_need - framer->header_have;
            if (!available) {
                break;
            }
            if (copy > available) {
                copy = available;
            }
            memcpy(framer->header + framer->header_have, bytes, copy);
            framer->header_have += (unsigned)copy;
            pump->rx_offset += (unsigned)copy;
            if (framer->header_have == framer->header_need) {
                if (!fuxi_bt_framer_header_valid(framer->header)) {
                    /* 跨塊才看得到的不合格 header：類型 byte 算 desync；這一塊裡的 header bytes（不含類型
                     * byte）退回重掃，裡面可能是 IBS 或下一個封包開頭；上一塊的已無法退回，直接丟掉。 */
                    const size_t rescan = copy < framer->header_need - 1u ? copy : framer->header_need - 1u;
                    fuxi_bt_pump_desync(pump, framer->header[0]);
                    framer->invalid_headers++;
                    fuxi_bt_framer_reset(framer);
                    pump->rx_offset -= (unsigned)rescan;
                    continue;
                }
                framer->remaining = fuxi_bt_h4_payload(framer->header);
                fuxi_bt_framer_classify(framer);
                fuxi_bt_pump_hold_begin(pump);
            }
            continue;
        }
        if (framer->header_sent < framer->header_need) {
            taken = fuxi_bt_pump_offer(pump, framer->header + framer->header_sent,
                framer->header_need - framer->header_sent, activity);
            if (!taken) {
                break;
            }
            framer->header_sent += (unsigned)taken;
            continue;
        }
        if (framer->remaining) {
            size_t want = framer->remaining;
            if (!available) {
                break;
            }
            if (want > available) {
                want = available;
            }
            if (framer->hold_active) {
                /* 整包收進 hold[]，收齊才改寫並交出 */
                memcpy(framer->hold + framer->hold_size, bytes, want);
                framer->hold_size += (unsigned)want;
                framer->remaining -= (uint32_t)want;
                pump->rx_offset += (unsigned)want;
                if (!framer->remaining) {
                    fuxi_bt_pump_hold_finish(pump);
                }
                continue;
            }
            fuxi_bt_pump_patch(pump, pump->rx + pump->rx_offset, want);
            taken = fuxi_bt_pump_offer(pump, bytes, want, activity);
            if (!taken) {
                break;
            }
            framer->remaining -= (uint32_t)taken;
            pump->rx_offset += (unsigned)taken;
            continue;
        }
        if (framer->hold_active && framer->hold_sent < framer->hold_size) {
            taken = fuxi_bt_pump_offer(pump, framer->hold + framer->hold_sent,
                framer->hold_size - framer->hold_sent, activity);
            if (!taken) {
                break;
            }
            framer->hold_sent += (unsigned)taken;
            continue;
        }
        /* 封包結束 */
        fuxi_bt_framer_reset(framer);
    }
    if (pump->rx_offset == pump->rx_size) {
        pump->rx_offset = 0;
        pump->rx_size = 0;
    }
}

enum fuxi_bt_status fuxi_bt_pump_step(struct fuxi_bt_pump *pump,
    struct fuxi_bt_pump_activity *activity)
{
    struct fuxi_bt_pump_activity scratch;
    enum fuxi_bt_status status;
    if (!activity) {
        activity = &scratch;
    }
    memset(activity, 0, sizeof(*activity));
    if (!pump) {
        return FUXI_BT_ARGUMENT;
    }
    if (pump->error != FUXI_BT_OK) {
        return pump->error;
    }
    pump->steps++;

    /* 控制器 → 主機 */
    fuxi_bt_pump_deliver(pump, activity);
    if (pump->rx_size) {
        pump->rx_stalls++;
        activity->rx_blocked = true;
    } else {
        size_t received = 0;
        status = pump->ops.read(pump->ops.context, pump->rx, sizeof(pump->rx), &received);
        if (status == FUXI_BT_OK) {
            if (received > sizeof(pump->rx)) {
                return fuxi_bt_pump_fail(pump, FUXI_BT_PROTOCOL);
            }
            if (received) {
                pump->rx_size = (unsigned)received;
                pump->rx_bytes += received;
                activity->rx_received = (unsigned)received;
                fuxi_bt_h4_feed(&pump->to_host, pump->rx, received, pump->trace,
                    pump->trace_context);
                fuxi_bt_pump_deliver(pump, activity);
                activity->rx_blocked = pump->rx_size != 0;
            }
        } else if (status != FUXI_BT_AGAIN) {
            return fuxi_bt_pump_fail(pump, status);
        }
    }

    /* 主機 → 控制器 */
    if (pump->tx_offset == pump->tx_size) {
        size_t taken;
        pump->tx_offset = 0;
        pump->tx_size = 0;
        taken = pump->sink.take(pump->sink.context, pump->tx, sizeof(pump->tx));
        if (taken > sizeof(pump->tx)) {
            return fuxi_bt_pump_fail(pump, FUXI_BT_PROTOCOL);
        }
        if (taken) {
            pump->tx_size = (unsigned)taken;
            pump->tx_head_boundary = pump->to_controller.type == 0;
            activity->tx_taken = (unsigned)taken;
            if (pump->txmask_count) {
                fuxi_bt_pump_txmask(pump, pump->tx, taken);
            }
            fuxi_bt_h4_feed(&pump->to_controller, pump->tx, taken, fuxi_bt_pump_trace_tx, pump);
        }
    }
    if (pump->ibs && !pump->ibs_tx_awake && pump->tx_size && !pump->tx_offset &&
        pump->tx_head_boundary) {
        /* 主機要送：先 WAKE_IND，等 WAKE_ACK（hci_qca HCI_IBS_TX_WAKING），等滿就照送 */
        if (pump->ibs_wait_steps >= FUXI_BT_IBS_WAIT_STEPS) {
            pump->ibs_tx_awake = true;
            pump->ibs_wake_timeouts++;
        } else {
            if (pump->ibs_wait_steps % FUXI_BT_IBS_RETRY_STEPS == 0) {
                fuxi_bt_pump_queue_ctl(pump, FUXI_BT_IBS_WAKE_IND);
                pump->ibs_wakes_sent++;
            }
            pump->ibs_wait_steps++;
        }
    }
    if (pump->ctl_size && (pump->tx_offset == pump->tx_size ?
        pump->to_controller.type == 0 : !pump->tx_offset && pump->tx_head_boundary)) {
        /* IBS 控制 byte：只在 TX 封包之間寫出 */
        size_t accepted = 0;
        status = pump->ops.write(pump->ops.context, pump->ctl, pump->ctl_size, &accepted);
        if (status == FUXI_BT_OK) {
            if (accepted > pump->ctl_size) {
                return fuxi_bt_pump_fail(pump, FUXI_BT_PROTOCOL);
            }
            for (size_t index = 0; index < accepted; index++) {
                if (pump->ctl[index] == FUXI_BT_IBS_WAKE_ACK) {
                    pump->ibs_acks_sent++;
                }
            }
            memmove(pump->ctl, pump->ctl + accepted, pump->ctl_size - accepted);
            pump->ctl_size -= (unsigned)accepted;
            pump->tx_bytes += accepted;
            activity->tx_accepted += (unsigned)accepted;
        } else if (status == FUXI_BT_AGAIN) {
            pump->tx_again++;
        } else {
            return fuxi_bt_pump_fail(pump, status);
        }
        if (pump->ctl_size) {
            activity->tx_pending = true;
            return FUXI_BT_OK;
        }
    }
    if (pump->ibs && !pump->ibs_tx_awake && pump->tx_size && !pump->tx_offset &&
        pump->tx_head_boundary) {
        activity->tx_pending = true;
        return FUXI_BT_OK;
    }
    if (pump->tx_offset < pump->tx_size) {
        const size_t remaining = pump->tx_size - pump->tx_offset;
        size_t accepted = 0;
        status = pump->ops.write(pump->ops.context, pump->tx + pump->tx_offset, remaining,
            &accepted);
        if (status == FUXI_BT_OK) {
            if (accepted > remaining) {
                return fuxi_bt_pump_fail(pump, FUXI_BT_PROTOCOL);
            }
            if (!pump->tx_offset && accepted) {
                pump->tx_chunks++;
            }
            pump->tx_offset += (unsigned)accepted;
            pump->tx_bytes += accepted;
            activity->tx_accepted += (unsigned)accepted;
        } else if (status == FUXI_BT_AGAIN) {
            pump->tx_again++;
        } else {
            return fuxi_bt_pump_fail(pump, status);
        }
        activity->tx_pending = pump->tx_offset < pump->tx_size;
    }
    return FUXI_BT_OK;
}

void fuxi_bt_pump_recover(struct fuxi_bt_pump *pump)
{
    if (!pump) {
        return;
    }
    if (pump->tx_offset < pump->tx_size) {
        pump->tx_dropped += pump->tx_size - pump->tx_offset;
    }
    pump->tx_offset = 0;
    pump->tx_size = 0;
    pump->error = FUXI_BT_OK;
    pump->recoveries++;
    /* 丟掉的塊可能切在封包中間：TX 追蹤器從下一個類型 byte 重新同步 */
    pump->to_controller.type = 0;
    pump->to_controller.header_have = 0;
    pump->to_controller.header_need = 0;
    pump->to_controller.remaining = 0;
    /* fuxi,bt-txmask 的走訪狀態同樣從下一個類型 byte 重新開始 */
    pump->txw_type = 0;
    pump->txw_header_have = 0;
    pump->txw_header_need = 0;
    pump->txw_remaining = 0;
    fuxi_bt_framer_reset(&pump->host);
}

void fuxi_bt_pump_resync_host(struct fuxi_bt_pump *pump)
{
    if (!pump) {
        return;
    }
    pump->host_resyncs++;
    pump->resync_dropped += pump->rx_size - pump->rx_offset;
    if (pump->host.header_need) {
        pump->resync_partial++;
    }
    pump->rx_offset = 0;
    pump->rx_size = 0;
    fuxi_bt_framer_reset(&pump->host);
    /* RX 追蹤器也從下一個類型 byte 重新開始 */
    pump->to_host.type = 0;
    pump->to_host.header_have = 0;
    pump->to_host.header_need = 0;
    pump->to_host.remaining = 0;
}
