#ifndef FUXI_BT_PUMP_H
#define FUXI_BT_PUMP_H

/*
 * WCN7850 藍牙常駐收送（fuxi,bt=5）：把 GENI UART（fuxi_bt_uart_ops）接到上層傳輸
 * （XNU 端是 UART.SK＋Skywalk，見 fuxi_bt_sk.c），並附一個雙向 H4 追蹤器。
 * 可攜 C，主機測試用假 UART 與假傳輸（tools/test-fuxi-bluetooth-pump.py）。
 *
 * - TX：每次向上層取最多 FUXI_BT_PUMP_TX_CHUNK bytes 當一筆 GENI 傳送。上一筆還在線上時，
 *   FIFO 對新傳送回 AGAIN，新的一塊先留在泵內；位元組順序不變。
 * - RX：上層收不下時保留未送達的位元組並暫停讀 UART；GENI 的 RFR 水位讓控制器停送，不丟資料。
 *   交付前依 H4 分框（fuxi_bt_h4_framer）：上層只在封包開頭決定收或丟，通道在封包中間接上或斷開
 *   都不會讓上層收到半個封包；封包邊界上的非 H4 byte 與 QCA 除錯 ACL 不交出。
 * - 追蹤器只看封包邊界與前幾個 payload byte，不改資料；未知封包類型逐 byte 跳過並計數。
 *
 * 傳輸層設計對照 QcaBluetooth src/qbt_transport.c（APSL-2.0，daimile2 and contributors）的
 * rx_put／tx_take 介面；本檔是自寫的可攜實作。
 */
#include "fuxi_bt.h"

#define FUXI_BT_PUMP_TX_CHUNK 256u
#define FUXI_BT_PUMP_RX_CHUNK 256u
#define FUXI_BT_H4_KEEP 6u
/* QCA 控制器用這個 ACL handle（含 PB 旗標的 16 位原值）送韌體除錯日誌；Linux btqca／hci_qca 的
 * QCA_DEBUG_HANDLE 把它轉成診斷封包，不交給 HCI 層。 */
#define FUXI_BT_QCA_DEBUG_HANDLE 0x2edcu
/* QCA 軟體 IBS（Linux hci_qca.c HCI_IBS_*）：單一 byte，出現在封包之間。 */
#define FUXI_BT_IBS_WAKE_ACK 0xfcu
#define FUXI_BT_IBS_WAKE_IND 0xfdu
#define FUXI_BT_IBS_SLEEP_IND 0xfeu
/* 主機送 WAKE_IND 後最多等幾步 WAKE_ACK；每 FUXI_BT_IBS_RETRY_STEPS 步重送一次，等滿就照送。 */
#define FUXI_BT_IBS_WAIT_STEPS 160u
#define FUXI_BT_IBS_RETRY_STEPS 16u
/* fuxi,bt-optional：最多幾個「失敗也當成功」的命令 opcode（只放回傳只有 status 的寫入命令） */
#define FUXI_BT_OPTIONAL_MAX 8u
/* 清單裡放 0xfc00（OGF 0x3f、OCF 0）表示所有 vendor 命令都算可選：bluetoothd 送 Apple／Broadcom
 * VSC（aci_vsc.c、bcm_vsc.c），QCA 控制器回 Unknown HCI Command。 */
#define FUXI_BT_OPTIONAL_VENDOR 0xfc00u
#define FUXI_BT_EVENT_COMMAND_COMPLETE 0x0eu
/* fuxi,bt-pad：最多幾個要補回傳資料的 opcode（同樣接受 0xfc00）。bluetoothd 的 Broadcom ByteStream
 * 解析器（例如 BD_VSC_OLYMPIC_ANTENNA_PREF_B，0xfce2 子命令 0x15）在 status 後少於 2 bytes 時只印
 * 「Invalid data size」不呼叫完成回呼，15 秒後判定 VSC stall 重啟。只有 status 的 Command Complete
 * 在交給上層前補「子命令回聲（命令第一個參數 byte）＋0」兩個 byte。 */
#define FUXI_BT_PAD_MAX 8u
#define FUXI_BT_PAD_BYTES 2u
/* 記住最近幾筆清單內命令的 opcode 與第一個參數 byte（bluetoothd 一次只等一個命令） */
#define FUXI_BT_PAD_TX_SLOTS 4u
/* fuxi,bt-mask：最多幾條「Command Complete 回傳參數清位元」規則。每條 u32＝opcode<<16 |
 * offset<<8 | clear；offset 從 status（0）起算。用途：0x2003（LE Read Local Supported Features）
 * 清功能位元 12／13（Extended／Periodic Advertising），bluetoothd 就不送 0x204a（LE Read Periodic
 * Advertiser List Size）。QCA 控制器依規範在收過延伸廣播命令後，舊式廣播／掃描命令（0x2007、0x200b）
 * 一律回 Command Disallowed（0x0c），bluetoothd 掃描起不來以 reason 712 重啟。 */
#define FUXI_BT_MASK_MAX 8u
/* fuxi,bt-txmask：最多幾條「送出命令參數清位元」規則，格式同 fuxi,bt-mask，offset 從第一個參數 byte（0）
 * 起算。用途：bluetoothd 在 Broadcom 平台由控制器輪替位址（BTLOG「supports address rotation while scanning
 * or connecting」），LE 掃描／連線的 Own_Address_Type 填 1（隨機），從不送 LE Set Random Address（0x2005）。
 * QCA 控制器依 Core spec Vol 4 Part E 7.8.11 對 LE Set Scan Enable（0x200c）回 Invalid HCI Command Parameters
 * （0x12），bluetoothd 以 reason 718 重啟（os_log 趟 4）。清 bit 0 讓 1→0（公開位址）、3→2。QcaBluetooth
 * （native-ios27.md「Scanning and addresses」）看到同一個 0x12，改為先送 0x2005。 */
#define FUXI_BT_TXMASK_MAX 8u

enum fuxi_bt_h4_direction {
    FUXI_BT_H4_TO_CONTROLLER = 0,
    FUXI_BT_H4_TO_HOST = 1
};

enum fuxi_bt_h4_type {
    FUXI_BT_H4_COMMAND = 1,
    FUXI_BT_H4_ACL = 2,
    FUXI_BT_H4_SCO = 3,
    FUXI_BT_H4_EVENT = 4,
    FUXI_BT_H4_ISO = 5
};

struct fuxi_bt_h4_record {
    uint8_t direction;
    uint8_t type;
    uint16_t length;      /* header 宣告的 payload 長度 */
    uint16_t opcode;      /* 命令的 opcode；Command Complete／Status 時是被完成的 opcode */
    uint16_t handle;      /* ACL／SCO／ISO 的 12-bit handle */
    uint8_t event;        /* 事件碼 */
    uint8_t subevent;     /* LE Meta 子事件 */
    uint8_t status;       /* Command Complete／Status 的狀態 */
    bool has_opcode;
    bool has_status;
    uint8_t keep[FUXI_BT_H4_KEEP];
    unsigned keep_size;
};

typedef void (*fuxi_bt_h4_trace_fn)(void *, const struct fuxi_bt_h4_record *);
/* Classic link milestones. Only status, disconnect reason, encryption or mode flags are returned. */
bool fuxi_bt_link_detail(const struct fuxi_bt_h4_record *record, int *value);
bool fuxi_bt_link_is_inquiry(const struct fuxi_bt_h4_record *record);

struct fuxi_bt_h4_parser {
    uint8_t direction;
    uint8_t type;
    uint8_t header[4];
    unsigned header_have;
    unsigned header_need;
    uint32_t remaining;
    struct fuxi_bt_h4_record record;
    uint64_t packets[6];      /* 依 H4 類型 1..5 計數，[0] 不用 */
    uint64_t bytes;
    uint64_t desync_bytes;
    uint64_t le_adv_reports;  /* LE Meta 廣播報告事件數（0x02／0x0b／0x0d） */
};

/* 控制器 → 主機的交付分框。header 跨 RX 塊時先存在這裡；其餘情形 header 與 payload 一起從 RX 塊交出。 */
struct fuxi_bt_h4_framer {
    uint8_t header[5];       /* 類型 byte＋header */
    unsigned header_have;
    unsigned header_need;    /* 1＋header 長度；0 表示在封包邊界 */
    unsigned header_sent;    /* header[] 已交出的 bytes */
    uint32_t remaining;      /* 還要從 RX 塊交出（或略過）的 bytes */
    bool start;              /* 下一次交付從封包開頭起算 */
    bool skip;               /* 這個封包不交給上層 */
    uint64_t packets;        /* 交給上層決定的封包 */
    uint64_t debug_packets;  /* 略過的 QCA 除錯 ACL */
    uint64_t debug_bytes;
    uint64_t desync_bytes;   /* 封包邊界上不是 H4 類型的 byte（例如 QCA IBS 0xfc–0xfe） */
    uint64_t ibs_bytes[3];   /* desync 中的 0xfc WAKE_ACK、0xfd WAKE_IND、0xfe SLEEP_IND */
    uint8_t desync_recent[16]; /* 最近 16 個 desync byte（環狀，診斷用） */
    unsigned desync_recent_pos;
    uint64_t invalid_headers; /* 跨 RX 塊才看出不合格的 header（例如 event code 0） */
    uint8_t cc_opcode[2];    /* 目前 Command Complete 的 opcode（payload 1..2） */
    bool cc_patched;         /* 目前封包的 status 已改寫 */
    /* fuxi,bt-pad 開啟時，只有 status 的 Command Complete 整包先收進 hold[]，改寫／補資料後再交出 */
    bool hold_active;
    uint8_t hold[16];
    unsigned hold_size;
    unsigned hold_sent;
};

struct fuxi_bt_pump_sink {
    void *context;
    /* 控制器 → 主機：回傳上層收下的 byte 數（0..size）。start 表示 data 從一個 H4 封包的第一個 byte 起算：
     * 上層在 start 時決定收下或丟棄整個封包（丟棄時回 size），start 為 false 的資料沿用同一個決定。
     * 回 0 時同一段資料之後以相同 start 重送。 */
    size_t (*deliver)(void *, const uint8_t *, size_t, bool);
    /* 主機 → 控制器：最多取 capacity bytes，回傳實際取得數。 */
    size_t (*take)(void *, uint8_t *, size_t);
};

struct fuxi_bt_pump_activity {
    unsigned rx_received;   /* 這一步從 UART 讀到的 byte 數 */
    unsigned rx_delivered;  /* 這一步交給上層的 byte 數 */
    unsigned tx_taken;      /* 這一步從上層取的 byte 數 */
    unsigned tx_accepted;   /* 這一步寫進 GENI FIFO 的 byte 數 */
    bool rx_blocked;        /* 上層收不下，UART 暫停讀 */
    bool tx_pending;        /* 泵內還有沒寫進 FIFO 的 TX */
};

struct fuxi_bt_pump {
    struct fuxi_bt_uart_ops ops;
    struct fuxi_bt_pump_sink sink;
    fuxi_bt_h4_trace_fn trace;
    void *trace_context;
    enum fuxi_bt_status error;
    uint8_t tx[FUXI_BT_PUMP_TX_CHUNK];
    unsigned tx_size;
    unsigned tx_offset;
    uint8_t rx[FUXI_BT_PUMP_RX_CHUNK];
    unsigned rx_size;
    unsigned rx_offset;
    struct fuxi_bt_h4_parser to_controller;
    struct fuxi_bt_h4_parser to_host;
    struct fuxi_bt_h4_framer host;
    uint64_t steps;
    uint64_t tx_bytes;
    uint64_t tx_chunks;
    uint64_t tx_again;
    uint64_t tx_dropped;
    uint64_t rx_bytes;
    uint64_t rx_delivered;
    uint64_t rx_stalls;
    uint64_t recoveries;
    /* 新的上層連線前 RX 重新同步（fuxi_bt_pump_resync_host）：次數、丟掉的未交出 bytes、
     * 當下卡在封包中間的次數 */
    uint64_t host_resyncs;
    uint64_t resync_dropped;
    uint64_t resync_partial;
    /* QCA IBS 應答（fuxi,bt-ibs，預設關）：控制器送 WAKE_IND 就回 WAKE_ACK；主機第一次送資料前
     * 先送 WAKE_IND 等 WAKE_ACK。控制 byte 只在 TX 封包之間寫出。 */
    bool ibs;
    bool ibs_tx_awake;
    bool tx_head_boundary;   /* tx[] 這一塊從 H4 封包開頭起算 */
    unsigned ibs_wait_steps;
    uint8_t ctl[8];
    unsigned ctl_size;
    uint64_t ibs_acks_sent;
    uint64_t ibs_wakes_sent;
    uint64_t ibs_wake_timeouts;
    /* 可選命令（fuxi,bt-optional，預設空）：控制器對清單內 opcode 回 Command Complete 失敗時，
     * 交給上層前把 status 改成 0。WCN7850 不支援 0x0c5b，bluetoothd 收到失敗就重啟。 */
    uint16_t optional[FUXI_BT_OPTIONAL_MAX];
    unsigned optional_count;
    uint64_t optional_patched;
    uint16_t optional_last_opcode;
    uint8_t optional_last_status;
    /* 補資料命令（fuxi,bt-pad，預設空）：清單內 opcode 的 Command Complete 只有 status 時，
     * 補 FUXI_BT_PAD_BYTES bytes（子命令回聲＋0）並把事件長度 4 改成 6。 */
    uint16_t pad[FUXI_BT_PAD_MAX];
    unsigned pad_count;
    uint64_t padded;
    uint16_t pad_last_opcode;
    uint8_t pad_last_subop;
    uint16_t pad_tx_opcode[FUXI_BT_PAD_TX_SLOTS];
    uint8_t pad_tx_subop[FUXI_BT_PAD_TX_SLOTS];
    unsigned pad_tx_pos;
    /* 回傳參數清位元（fuxi,bt-mask，預設空）：清單內 opcode 的 Command Complete 在指定 offset 清掉
     * 指定位元，交出前就地改寫（同一 byte 重算也只會得到同樣結果）。 */
    uint32_t mask[FUXI_BT_MASK_MAX];
    unsigned mask_count;
    uint64_t masked;
    uint16_t mask_last_opcode;
    uint8_t mask_last_offset;
    uint8_t mask_last_before;
    uint8_t mask_last_after;
    /* 送出命令參數清位元（fuxi,bt-txmask，預設空）：從上層取到的 TX 塊寫進 FIFO 前就地改寫。
     * txw_* 是獨立的 H4 命令邊界走訪狀態（TX 追蹤器在改寫後才看到資料），recover 時一起歸零。 */
    uint32_t txmask[FUXI_BT_TXMASK_MAX];
    unsigned txmask_count;
    uint64_t txmasked;
    uint16_t txmask_last_opcode;
    uint8_t txmask_last_offset;
    uint8_t txmask_last_before;
    uint8_t txmask_last_after;
    uint8_t txw_type;
    uint8_t txw_header[4];
    unsigned txw_header_have;
    unsigned txw_header_need;
    uint32_t txw_remaining;
    uint32_t txw_param;
    uint16_t txw_opcode;
};

#ifdef __cplusplus
extern "C" {
#endif

const char *fuxi_bt_h4_type_name(uint8_t type);
void fuxi_bt_h4_parser_init(struct fuxi_bt_h4_parser *parser, uint8_t direction);
void fuxi_bt_h4_feed(struct fuxi_bt_h4_parser *parser, const uint8_t *bytes, size_t size,
    fuxi_bt_h4_trace_fn trace, void *context);

enum fuxi_bt_status fuxi_bt_pump_init(struct fuxi_bt_pump *pump,
    const struct fuxi_bt_uart_ops *ops, const struct fuxi_bt_pump_sink *sink,
    fuxi_bt_h4_trace_fn trace, void *trace_context);
/* 一次收送：先把 RX 交給上層（或從 UART 讀新的一塊），再從上層取 TX 寫進 FIFO。
 * UART 的 AGAIN 不算錯；其他錯誤記在 pump->error，之後每一步都回同一個錯直到 recover。 */
enum fuxi_bt_status fuxi_bt_pump_step(struct fuxi_bt_pump *pump,
    struct fuxi_bt_pump_activity *activity);
/* UART 重新 acquire 之後呼叫：丟掉送到一半的 TX 塊（計入 tx_dropped），清錯誤，TX 追蹤器重新同步；
 * RX 分框從下一個類型 byte 重新開始。 */
void fuxi_bt_pump_recover(struct fuxi_bt_pump *pump);
/* 新的上層連線（bluetoothd 開 UART.SK）時呼叫：丟掉還沒交出的 RX 與半個封包，分框器回到封包邊界。
 * 連線前 UART 殘留的破碎資料會被當成假封包（例如 01 da 01 ff…），之後控制器的 IBS WAKE_IND 被當成
 * payload 吃掉不回 WAKE_ACK，HCI Reset 回應永遠等不到（os_log 趟 3 前兩次啟動 reason 108／635）。 */
void fuxi_bt_pump_resync_host(struct fuxi_bt_pump *pump);

#ifdef __cplusplus
}
#endif

#endif
