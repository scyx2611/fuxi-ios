#ifndef FUXI_BT_BOARD_H
#define FUXI_BT_BOARD_H

/*
 * WCN7850 藍牙 UART 的板級準備（fuxi,bt=3）：
 * QUP2 SE6 時脈投票、SE6 RCG 設成 7.3728 MHz（GPLL0_OUT_EVEN 300 MHz × 384/15625，與 Linux gcc-sm8550 相同）、
 * 之後 acquire 要求其他 baud 時，在 FIFO 關閉期間把 RCG 改成速率表的來源頻率（power_off 還原 XBL 的值）、
 * gpio76–79 切成 qup2_se6、BT_EN（gpio81）拉高。前提是 WLAN 已把 WCN 的電源軌和 WLAN_EN（gpio80）打開。
 * 純 C；MMIO 經由呼叫端提供的 read/write（XNU 走 fault-safe 存取，主機測試走模擬模型）。
 * power_off 依旗標逆序還原 power_on 改過的東西，不論 power_on 成功或失敗都要呼叫。
 */
#include "fuxi_bt.h"
#include "fuxi_bt_geni_fifo.h"

#define FUXI_BT_SE_HZ 7372800u
#define FUXI_BT_GPIO_UART_FIRST 76u /* 76 CTS、77 RFR、78 TX、79 RX */
#define FUXI_BT_GPIO_WLAN_EN 80u
#define FUXI_BT_GPIO_BT_EN 81u
#define FUXI_BT_GPIO_SW_CTRL 82u
#define FUXI_BT_VOTE_REG 0x52010u
#define FUXI_BT_VOTE_BITS 0x40fu /* core、s_ahb、m_ahb、core_2x、se6 */
#define FUXI_BT_RCG_CMD 0x1e760u
#define FUXI_BT_GPLL0_MODE 0x0u
#define FUXI_BT_GPLL0_USER 0x18u
#define FUXI_BT_GPLL0_VOTE 0x52018u
#define FUXI_BT_BRANCH_TIMEOUT_US 2000u
#define FUXI_BT_RCG_TIMEOUT_US 2000u
#define FUXI_BT_BT_EN_LOW_US 50000u
#define FUXI_BT_POWERUP_US 100000u /* pwrseq-qcom-wcn：WCN7850 pwup_delay_ms 100 */
#define FUXI_BT_ACQUIRE_POLLS 2000u
#define FUXI_BT_RELEASE_POLLS 20000u

enum fuxi_bt_board_stage {
    FUXI_BT_STAGE_IDLE = 0,
    FUXI_BT_STAGE_WLAN,
    FUXI_BT_STAGE_GPLL0,
    FUXI_BT_STAGE_VOTE,
    FUXI_BT_STAGE_BRANCH,
    FUXI_BT_STAGE_RCG,
    FUXI_BT_STAGE_WRAPPER,
    FUXI_BT_STAGE_ENGINE,
    FUXI_BT_STAGE_PINS,
    FUXI_BT_STAGE_BT_EN,
    FUXI_BT_STAGE_READY
};

struct fuxi_bt_board_io {
    void *context;
    enum fuxi_bt_status (*read)(void *, uint64_t, uint32_t *);
    enum fuxi_bt_status (*write)(void *, uint64_t, uint32_t);
    uint64_t (*now_us)(void *);
    void (*wait_us)(void *, unsigned);
};

struct fuxi_bt_board {
    struct fuxi_bt_board_io io;
    enum fuxi_bt_board_stage stage;
    enum fuxi_bt_status error;
    uint32_t failed_value;
    uint32_t gpll0_mode;
    uint32_t gpll0_user;
    uint32_t vote;
    uint32_t added_votes;
    uint32_t rcg_saved[4]; /* cfg、m、n、d */
    uint32_t rcg_cfg;
    uint32_t rcg_hz; /* 目前 RCG 設定的來源頻率；0＝未設或設定中途失敗 */
    uint32_t baud; /* 最近一次 acquire 的 baud */
    unsigned rcg_switches;
    uint32_t qup_version;
    uint32_t firmware;
    uint32_t pin_ctl[4];
    uint32_t bt_en_ctl;
    uint32_t bt_en_io;
    uint32_t sw_ctrl_before;
    uint32_t sw_ctrl_after;
    bool votes_changed;
    bool rcg_changed;
    bool pins_changed;
    bool bt_en_changed;
    bool fifo_used;
    struct fuxi_bt_geni_fifo fifo;
};

#ifdef __cplusplus
extern "C" {
#endif

const char *fuxi_bt_board_stage_name(enum fuxi_bt_board_stage stage);
enum fuxi_bt_status fuxi_bt_board_wlan_ready(const struct fuxi_bt_board_io *io, bool *ready);
enum fuxi_bt_status fuxi_bt_board_power_on(struct fuxi_bt_board *board,
    const struct fuxi_bt_board_io *io);
enum fuxi_bt_status fuxi_bt_board_power_off(struct fuxi_bt_board *board);
void fuxi_bt_board_uart_ops(struct fuxi_bt_board *board, struct fuxi_bt_uart_ops *ops);

#ifdef __cplusplus
}
#endif

#endif
