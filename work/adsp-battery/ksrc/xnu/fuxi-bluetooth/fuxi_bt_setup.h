#ifndef FUXI_BT_SETUP_H
#define FUXI_BT_SETUP_H

/*
 * WCN7850 藍牙控制器初始化（fuxi,bt=4）：版本查詢 → rampatch → board ID → NVM → build info → HCI reset
 * → 讀本機版本與 BD 位址。流程對照 Linux drivers/bluetooth/btqca.c qca_uart_setup（只作流程參考，
 * 程式自寫）。預設全程 115200 baud；options.baud（fuxi,bt-baud）要求其他速率時，照 Linux qca_setup：
 * 以 115200 查版本後送 0xfc48、host 換速、再查一次版本確認，確認不到就退回 115200 繼續。
 * NVM 的 HCI tag 把 IBS 位元清掉、baud 欄位設成實際速率，deep sleep 位元清掉。
 * 可攜程式，主機測試用假 UART 與假檔案。
 */

#include "fuxi_bt.h"

#define FUXI_BT_TLV_SEGMENT 243u
/* 115200 baud 約 11.5 KB/s：1 MiB 上限的 rampatch 要約 91 秒，加上每段等 ack，總上限 240 秒 */
#define FUXI_BT_SETUP_TIMEOUT_US 240000000u
#define FUXI_BT_COMMAND_TIMEOUT_US 2000000u
#define FUXI_BT_PATCH_MAX (1024u * 1024u)
#define FUXI_BT_NVM_MAX 65536u
#define FUXI_BT_SETUP_MAX_FRAMES 16384u
#define FUXI_BT_SETUP_NAME 24u
#define FUXI_BT_BAUD_VERIFY_ATTEMPTS 3u
#define FUXI_BT_BAUD_SETTLE_US 10000u /* Linux WCN7850：送完 0xfc48 後 usleep_range(1000, 10000) */

enum fuxi_bt_setup_stage {
    FUXI_BT_SETUP_START = 0,
    FUXI_BT_SETUP_VERSION,
    FUXI_BT_SETUP_BAUD,
    FUXI_BT_SETUP_PATCH_LOAD,
    FUXI_BT_SETUP_PATCH_PARSE,
    FUXI_BT_SETUP_PATCH_SEND,
    FUXI_BT_SETUP_BOARD_ID,
    FUXI_BT_SETUP_NVM_LOAD,
    FUXI_BT_SETUP_NVM_PARSE,
    FUXI_BT_SETUP_NVM_SEND,
    FUXI_BT_SETUP_NOLOG,
    FUXI_BT_SETUP_BUILD_INFO,
    FUXI_BT_SETUP_RESET,
    FUXI_BT_SETUP_LOCAL_VERSION,
    FUXI_BT_SETUP_BDADDR,
    FUXI_BT_SETUP_DONE
};

/* 讀檔回呼：name 是基本檔名；成功時 data 指向可寫緩衝區（NVM 會就地修改），直到 release。 */
struct fuxi_bt_blob {
    uint8_t *data;
    size_t size;
};

struct fuxi_bt_files {
    void *context;
    enum fuxi_bt_status (*load)(void *, const char *, size_t, struct fuxi_bt_blob *);
    void (*release)(void *, struct fuxi_bt_blob *);
};

/* 預設全關（fuxi_bt_setup_run 用的就是全 0）。 */
struct fuxi_bt_setup_options {
    uint32_t baud; /* 0 或 115200＝不換速；其他值須在速率表內（fuxi,bt-baud） */
    bool disable_logging; /* Linux qca_disable_soc_logging：0xfc17 {0x14, 0x00}（fuxi,bt-nolog） */
};

struct fuxi_bt_tlv_info {
    uint32_t file_bytes;
    uint32_t tlv_bytes;
    uint32_t total_size;
    uint8_t type;
    uint8_t download_mode;
    uint16_t product;
    uint16_t rom;
    uint16_t patch;
    uint16_t tags;
    bool hci_tag;
    uint8_t hci_before[3];
    uint8_t hci_after[3];
    bool sleep_tag;
    uint8_t sleep_before;
    uint8_t sleep_after;
    uint32_t segments;
    uint32_t acks;
    bool final_ack;
};

struct fuxi_bt_setup_result {
    enum fuxi_bt_status status;
    enum fuxi_bt_status cleanup;
    enum fuxi_bt_setup_stage stage;
    struct fuxi_bt_version version;
    uint8_t rom_ver;
    char patch_name[FUXI_BT_SETUP_NAME];
    char nvm_name[FUXI_BT_SETUP_NAME];
    struct fuxi_bt_tlv_info patch;
    struct fuxi_bt_tlv_info nvm;
    uint16_t board_id;
    bool board_id_valid;
    enum fuxi_bt_status board_id_status;
    char build_info[96];
    enum fuxi_bt_status build_info_status;
    uint8_t controller_status;
    uint8_t hci_version;
    uint16_t hci_revision;
    uint8_t lmp_version;
    uint16_t manufacturer;
    uint16_t lmp_subversion;
    bool local_version_valid;
    uint8_t bdaddr[6];
    bool bdaddr_valid;
    uint8_t last_event[24];
    unsigned last_event_size;
    uint8_t last_vendor[24];
    unsigned last_vendor_size;
    unsigned tx_bytes;
    unsigned rx_bytes;
    unsigned frames;
    unsigned ignored_frames;
    unsigned vendor_events;
    unsigned stale_acks;
    uint64_t elapsed_us;
    uint64_t patch_us;
    uint32_t baud_requested;
    uint32_t baud_active;
    enum fuxi_bt_status baud_status; /* off＝沒換速；ok＝已換；其他＝新速率確認失敗的原因 */
    enum fuxi_bt_status baud_fallback; /* off＝沒退回；ok＝已退回 115200 並確認 */
    unsigned baud_attempts;
    unsigned baud_early_bytes; /* 送 0xfc48 後、host 換速前以舊速率收到的位元組數 */
    uint8_t baud_early[8];
    unsigned baud_early_size;
    uint64_t baud_us;
    enum fuxi_bt_status nolog_status; /* off＝沒要求 */
};

const char *fuxi_bt_setup_stage_name(enum fuxi_bt_setup_stage stage);
bool fuxi_bt_setup_name_valid(const char *name);
enum fuxi_bt_status fuxi_bt_setup_run(const struct fuxi_bt_uart_ops *ops,
    const struct fuxi_bt_files *files, struct fuxi_bt_setup_result *result);
enum fuxi_bt_status fuxi_bt_setup_run_options(const struct fuxi_bt_uart_ops *ops,
    const struct fuxi_bt_files *files, const struct fuxi_bt_setup_options *options,
    struct fuxi_bt_setup_result *result);

#endif
