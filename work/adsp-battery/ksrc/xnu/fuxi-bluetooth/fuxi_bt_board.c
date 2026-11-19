#include "fuxi_bt_board.h"
#include <string.h>

/* TLMM：每支腳 0x1000；CTL 的 pull [1:0]、func [5:2]、drive [8:6]、OE bit 9；IO 的 IN bit 0、OUT bit 1。 */
#define FUXI_BT_TLMM_CTL(pin) (FUXI_BT_TLMM_BASE + (uint64_t)(pin) * 0x1000u)
#define FUXI_BT_TLMM_IO(pin) (FUXI_BT_TLMM_CTL(pin) + 4u)
#define FUXI_BT_TLMM_FIELDS 0x3ffu
#define FUXI_BT_PULL_DOWN 1u
#define FUXI_BT_PULL_UP 3u
#define FUXI_BT_TLMM_OE (1u << 9)
#define FUXI_BT_TLMM_OUT 2u
#define FUXI_BT_TLMM_IN 1u

#define FUXI_BT_BRANCH_OFF (1u << 31)
#define FUXI_BT_BRANCH_HWCG (1u << 1)
#define FUXI_BT_RCG_UPDATE 1u
#define FUXI_BT_RCG_ROOT_OFF (1u << 31)
/* hid 除頻 [4:0]、來源 [10:8]、MND 模式 [13:12]；bit 20（HW_CLK_CTRL）保留 XBL 的設定。 */
#define FUXI_BT_RCG_CFG_FIELDS 0x371fu
/* F(hz, P_GCC_GPLL0_OUT_EVEN, 1, m, n)：pre_div 2*1-1、parent_map_8 來源 6、雙沿 MND（速率表每項都是這組）。 */
#define FUXI_BT_RCG_CFG_VALUE (1u | (6u << 8) | (2u << 12))
#define FUXI_BT_MND_MASK 0xffffu

static const uint32_t fuxi_bt_board_branches[] = {0x233d4, 0x233d0, 0x233cc, 0x23004, 0x1e754};

const char *fuxi_bt_board_stage_name(enum fuxi_bt_board_stage stage)
{
    static const char *const names[] = {
        "idle", "wlan", "gpll0", "vote", "branch", "rcg", "wrapper",
        "engine", "pins", "bt-en", "ready"
    };
    if ((unsigned)stage >= sizeof(names) / sizeof(names[0])) {
        return "unknown";
    }
    return names[stage];
}

static bool fuxi_bt_board_read(struct fuxi_bt_board *board, uint64_t pa, uint32_t *value)
{
    if (board->io.read(board->io.context, pa, value) == FUXI_BT_OK) {
        return true;
    }
    if (board->error == FUXI_BT_OK) {
        board->error = FUXI_BT_IO;
        board->failed_value = (uint32_t)pa;
    }
    return false;
}

static bool fuxi_bt_board_write(struct fuxi_bt_board *board, uint64_t pa, uint32_t value)
{
    if (board->io.write(board->io.context, pa, value) == FUXI_BT_OK) {
        return true;
    }
    if (board->error == FUXI_BT_OK) {
        board->error = FUXI_BT_IO;
        board->failed_value = (uint32_t)pa;
    }
    return false;
}

static enum fuxi_bt_status fuxi_bt_board_fail(struct fuxi_bt_board *board,
    enum fuxi_bt_status status, uint32_t value)
{
    if (board->error == FUXI_BT_OK) {
        board->error = status;
        board->failed_value = value;
    }
    return board->error;
}

enum fuxi_bt_status fuxi_bt_board_wlan_ready(const struct fuxi_bt_board_io *io, bool *ready)
{
    uint32_t ctl, value;
    enum fuxi_bt_status status;
    if (!io || !io->read || !ready) {
        return FUXI_BT_ARGUMENT;
    }
    *ready = false;
    status = io->read(io->context, FUXI_BT_TLMM_CTL(FUXI_BT_GPIO_WLAN_EN), &ctl);
    if (status != FUXI_BT_OK) {
        return status;
    }
    status = io->read(io->context, FUXI_BT_TLMM_IO(FUXI_BT_GPIO_WLAN_EN), &value);
    if (status != FUXI_BT_OK) {
        return status;
    }
    *ready = !((ctl >> 2) & 0xfu) && (ctl & FUXI_BT_TLMM_OE) && (value & FUXI_BT_TLMM_OUT);
    return FUXI_BT_OK;
}

/* 投票後等五個分支開：hwcg 開著的分支（s_ahb、m_ahb）閒置時可能仍回報 off，比照 Linux 跳過。 */
static enum fuxi_bt_status fuxi_bt_board_branches_wait(struct fuxi_bt_board *board)
{
    for (unsigned attempt = 0; attempt <= FUXI_BT_BRANCH_TIMEOUT_US / 10u; attempt++) {
        uint32_t pending = 0, value = 0;
        for (unsigned index = 0; index < 5; index++) {
            if (!fuxi_bt_board_read(board, FUXI_BT_GCC_BASE + fuxi_bt_board_branches[index], &value)) {
                return board->error;
            }
            if ((value & FUXI_BT_BRANCH_OFF) && !(value & FUXI_BT_BRANCH_HWCG)) {
                pending = value;
            }
        }
        if (!pending) {
            return FUXI_BT_OK;
        }
        if (attempt == FUXI_BT_BRANCH_TIMEOUT_US / 10u) {
            return fuxi_bt_board_fail(board, FUXI_BT_TIMEOUT, pending);
        }
        board->io.wait_us(board->io.context, 10);
    }
    return fuxi_bt_board_fail(board, FUXI_BT_TIMEOUT, 0);
}

static enum fuxi_bt_status fuxi_bt_board_rcg_update(struct fuxi_bt_board *board)
{
    uint32_t command = 0;
    if (!fuxi_bt_board_read(board, FUXI_BT_GCC_BASE + FUXI_BT_RCG_CMD, &command) ||
        !fuxi_bt_board_write(board, FUXI_BT_GCC_BASE + FUXI_BT_RCG_CMD, command | FUXI_BT_RCG_UPDATE)) {
        return board->error;
    }
    for (unsigned attempt = 0; attempt <= FUXI_BT_RCG_TIMEOUT_US / 10u; attempt++) {
        if (!fuxi_bt_board_read(board, FUXI_BT_GCC_BASE + FUXI_BT_RCG_CMD, &command)) {
            return board->error;
        }
        if (!(command & FUXI_BT_RCG_UPDATE)) {
            return FUXI_BT_OK;
        }
        if (attempt < FUXI_BT_RCG_TIMEOUT_US / 10u) {
            board->io.wait_us(board->io.context, 10);
        }
    }
    return fuxi_bt_board_fail(board, FUXI_BT_TIMEOUT, command);
}

/* 依速率表設 RCG；rcg_saved 必須已存好 XBL 的值（上半部保留位元照抄）。 */
static enum fuxi_bt_status fuxi_bt_board_rcg_program(struct fuxi_bt_board *board,
    const struct fuxi_bt_rate *rate)
{
    static const uint32_t offsets[] = {4, 8, 12, 16};
    uint32_t command = 0, values[4], d;
    enum fuxi_bt_status status;
    if (!rate || !rate->m || rate->m >= rate->n) {
        return FUXI_BT_ARGUMENT;
    }
    if (!fuxi_bt_board_read(board, FUXI_BT_GCC_BASE + FUXI_BT_RCG_CMD, &command)) {
        return board->error;
    }
    if (command & FUXI_BT_RCG_UPDATE) {
        return fuxi_bt_board_fail(board, FUXI_BT_BUSY, command);
    }
    /* clk-rcg2 __clk_rcg2_configure_mnd：2d 值＝n，夾在 [m, 2(n-m)]；D 暫存器寫 ~2d */
    d = rate->n;
    if (d < rate->m) {
        d = rate->m;
    }
    if (d > 2u * (uint32_t)(rate->n - rate->m)) {
        d = 2u * (uint32_t)(rate->n - rate->m);
    }
    values[0] = (board->rcg_saved[0] & ~FUXI_BT_RCG_CFG_FIELDS) | FUXI_BT_RCG_CFG_VALUE;
    values[1] = (board->rcg_saved[1] & ~FUXI_BT_MND_MASK) | rate->m;
    values[2] = (board->rcg_saved[2] & ~FUXI_BT_MND_MASK) |
        (~(uint32_t)(rate->n - rate->m) & FUXI_BT_MND_MASK);
    values[3] = (board->rcg_saved[3] & ~FUXI_BT_MND_MASK) | (~d & FUXI_BT_MND_MASK);
    board->rcg_changed = true;
    board->rcg_hz = 0;
    /* 和 Linux 一樣先寫 M/N/D 再寫 CFG，最後 CMD_UPDATE。 */
    for (unsigned index = 1; index < 4; index++) {
        if (!fuxi_bt_board_write(board, FUXI_BT_GCC_BASE + FUXI_BT_RCG_CMD + offsets[index], values[index])) {
            return board->error;
        }
    }
    if (!fuxi_bt_board_write(board, FUXI_BT_GCC_BASE + FUXI_BT_RCG_CMD + 4, values[0])) {
        return board->error;
    }
    status = fuxi_bt_board_rcg_update(board);
    if (status != FUXI_BT_OK) {
        return status;
    }
    if (!fuxi_bt_board_read(board, FUXI_BT_GCC_BASE + FUXI_BT_RCG_CMD + 4, &board->rcg_cfg) ||
        !fuxi_bt_board_read(board, FUXI_BT_GCC_BASE + FUXI_BT_RCG_CMD, &command)) {
        return board->error;
    }
    if ((board->rcg_cfg & FUXI_BT_RCG_CFG_FIELDS) != FUXI_BT_RCG_CFG_VALUE) {
        return fuxi_bt_board_fail(board, FUXI_BT_IO, board->rcg_cfg);
    }
    if (command & FUXI_BT_RCG_ROOT_OFF) {
        return fuxi_bt_board_fail(board, FUXI_BT_OFF, command);
    }
    board->rcg_hz = rate->source_hz;
    return FUXI_BT_OK;
}

static enum fuxi_bt_status fuxi_bt_board_rcg(struct fuxi_bt_board *board)
{
    static const uint32_t offsets[] = {4, 8, 12, 16};
    uint32_t command = 0;
    if (!fuxi_bt_board_read(board, FUXI_BT_GCC_BASE + FUXI_BT_RCG_CMD, &command)) {
        return board->error;
    }
    if (command & FUXI_BT_RCG_UPDATE) {
        return fuxi_bt_board_fail(board, FUXI_BT_BUSY, command);
    }
    for (unsigned index = 0; index < 4; index++) {
        if (!fuxi_bt_board_read(board, FUXI_BT_GCC_BASE + FUXI_BT_RCG_CMD + offsets[index],
            &board->rcg_saved[index])) {
            return board->error;
        }
    }
    return fuxi_bt_board_rcg_program(board, fuxi_bt_rate_find(FUXI_BT_BOOT_BAUD));
}

enum fuxi_bt_status fuxi_bt_board_power_on(struct fuxi_bt_board *board,
    const struct fuxi_bt_board_io *io)
{
    uint32_t value = 0;
    bool ready = false;
    enum fuxi_bt_status status;
    if (!board || !io || !io->read || !io->write || !io->now_us || !io->wait_us) {
        return FUXI_BT_ARGUMENT;
    }
    memset(board, 0, sizeof(*board));
    board->io = *io;

    board->stage = FUXI_BT_STAGE_WLAN;
    status = fuxi_bt_board_wlan_ready(io, &ready);
    if (status != FUXI_BT_OK) {
        return fuxi_bt_board_fail(board, status, 0);
    }
    if (!ready) {
        return fuxi_bt_board_fail(board, FUXI_BT_OFF, 0);
    }

    board->stage = FUXI_BT_STAGE_GPLL0;
    if (!fuxi_bt_board_read(board, FUXI_BT_GCC_BASE + FUXI_BT_GPLL0_VOTE, &value) ||
        !fuxi_bt_board_read(board, FUXI_BT_GCC_BASE + FUXI_BT_GPLL0_MODE, &board->gpll0_mode) ||
        !fuxi_bt_board_read(board, FUXI_BT_GCC_BASE + FUXI_BT_GPLL0_USER, &board->gpll0_user)) {
        return board->error;
    }
    /* GPLL0 已投票、已鎖定，EVEN 輸出開著且後除 2（300 MHz）；不是就不動它。 */
    if (!(value & 1u) || !(board->gpll0_mode & (1u << 31)) ||
        !(board->gpll0_user & 2u) || ((board->gpll0_user >> 10) & 0xfu) != 1u) {
        return fuxi_bt_board_fail(board, FUXI_BT_NO_UART, board->gpll0_user);
    }

    board->stage = FUXI_BT_STAGE_VOTE;
    if (!fuxi_bt_board_read(board, FUXI_BT_GCC_BASE + FUXI_BT_VOTE_REG, &board->vote)) {
        return board->error;
    }
    board->added_votes = FUXI_BT_VOTE_BITS & ~board->vote;
    if (board->added_votes) {
        board->votes_changed = true;
        if (!fuxi_bt_board_write(board, FUXI_BT_GCC_BASE + FUXI_BT_VOTE_REG,
            board->vote | FUXI_BT_VOTE_BITS)) {
            return board->error;
        }
    }

    board->stage = FUXI_BT_STAGE_BRANCH;
    status = fuxi_bt_board_branches_wait(board);
    if (status != FUXI_BT_OK) {
        return status;
    }

    board->stage = FUXI_BT_STAGE_RCG;
    status = fuxi_bt_board_rcg(board);
    if (status != FUXI_BT_OK) {
        return status;
    }

    board->stage = FUXI_BT_STAGE_WRAPPER;
    if (!fuxi_bt_board_read(board, FUXI_BT_WRAPPER_BASE + 4, &board->qup_version)) {
        return board->error;
    }
    if (!board->qup_version || board->qup_version == 0xffffffffu) {
        return fuxi_bt_board_fail(board, FUXI_BT_NO_UART, board->qup_version);
    }

    board->stage = FUXI_BT_STAGE_ENGINE;
    if (!fuxi_bt_board_read(board, FUXI_BT_UART_BASE + FUXI_BT_FIFO_FIRMWARE, &board->firmware) ||
        !fuxi_bt_board_read(board, FUXI_BT_UART_BASE + FUXI_BT_FIFO_IF_DISABLE, &value)) {
        return board->error;
    }
    if (((board->firmware >> 8) & 0xffu) != 2u) {
        return fuxi_bt_board_fail(board, FUXI_BT_NO_UART, board->firmware);
    }
    if (value & 1u) {
        return fuxi_bt_board_fail(board, FUXI_BT_NO_UART, value);
    }

    /* sm8550.dtsi qup_uart14：CTS/RTS 下拉、TX/RX 上拉，2 mA，function qup2_se6（1）。 */
    board->stage = FUXI_BT_STAGE_PINS;
    for (unsigned index = 0; index < 4; index++) {
        const unsigned pin = FUXI_BT_GPIO_UART_FIRST + index;
        if (!fuxi_bt_board_read(board, FUXI_BT_TLMM_CTL(pin), &board->pin_ctl[index])) {
            return board->error;
        }
    }
    board->pins_changed = true;
    for (unsigned index = 0; index < 4; index++) {
        const unsigned pin = FUXI_BT_GPIO_UART_FIRST + index;
        const uint32_t wanted = (index < 2 ? FUXI_BT_PULL_DOWN : FUXI_BT_PULL_UP) | (1u << 2);
        uint32_t readback = 0;
        if (!fuxi_bt_board_write(board, FUXI_BT_TLMM_CTL(pin),
                (board->pin_ctl[index] & ~FUXI_BT_TLMM_FIELDS) | wanted) ||
            !fuxi_bt_board_read(board, FUXI_BT_TLMM_CTL(pin), &readback)) {
            return board->error;
        }
        if ((readback & FUXI_BT_TLMM_FIELDS) != wanted) {
            return fuxi_bt_board_fail(board, FUXI_BT_IO, readback);
        }
    }

    board->stage = FUXI_BT_STAGE_BT_EN;
    if (!fuxi_bt_board_read(board, FUXI_BT_TLMM_IO(FUXI_BT_GPIO_SW_CTRL), &value) ||
        !fuxi_bt_board_read(board, FUXI_BT_TLMM_CTL(FUXI_BT_GPIO_BT_EN), &board->bt_en_ctl) ||
        !fuxi_bt_board_read(board, FUXI_BT_TLMM_IO(FUXI_BT_GPIO_BT_EN), &board->bt_en_io)) {
        return board->error;
    }
    board->sw_ctrl_before = value & FUXI_BT_TLMM_IN;
    board->bt_en_changed = true;
    if (!fuxi_bt_board_write(board, FUXI_BT_TLMM_IO(FUXI_BT_GPIO_BT_EN), 0) ||
        !fuxi_bt_board_write(board, FUXI_BT_TLMM_CTL(FUXI_BT_GPIO_BT_EN),
            (board->bt_en_ctl & ~FUXI_BT_TLMM_FIELDS) | FUXI_BT_PULL_DOWN | FUXI_BT_TLMM_OE)) {
        return board->error;
    }
    board->io.wait_us(board->io.context, FUXI_BT_BT_EN_LOW_US);
    if (!fuxi_bt_board_write(board, FUXI_BT_TLMM_IO(FUXI_BT_GPIO_BT_EN), FUXI_BT_TLMM_OUT)) {
        return board->error;
    }
    board->io.wait_us(board->io.context, FUXI_BT_POWERUP_US);
    if (!fuxi_bt_board_read(board, FUXI_BT_TLMM_IO(FUXI_BT_GPIO_BT_EN), &value)) {
        return board->error;
    }
    if (!(value & FUXI_BT_TLMM_IN)) {
        return fuxi_bt_board_fail(board, FUXI_BT_IO, value);
    }
    if (!fuxi_bt_board_read(board, FUXI_BT_TLMM_IO(FUXI_BT_GPIO_SW_CTRL), &value)) {
        return board->error;
    }
    board->sw_ctrl_after = value & FUXI_BT_TLMM_IN;
    board->stage = FUXI_BT_STAGE_READY;
    return FUXI_BT_OK;
}

static enum fuxi_bt_status fuxi_bt_board_cleanup_write(struct fuxi_bt_board *board,
    uint64_t pa, uint32_t value, enum fuxi_bt_status *cleanup)
{
    if (board->io.write(board->io.context, pa, value) != FUXI_BT_OK && *cleanup == FUXI_BT_OK) {
        *cleanup = FUXI_BT_IO;
    }
    return *cleanup;
}

static enum fuxi_bt_status fuxi_bt_board_release(void *context);

enum fuxi_bt_status fuxi_bt_board_power_off(struct fuxi_bt_board *board)
{
    enum fuxi_bt_status cleanup = FUXI_BT_OK;
    if (!board || !board->io.read || !board->io.write || !board->io.wait_us) {
        return FUXI_BT_ARGUMENT;
    }
    if (board->fifo_used) {
        cleanup = fuxi_bt_board_release(board);
    }
    if (board->bt_en_changed) {
        (void)fuxi_bt_board_cleanup_write(board, FUXI_BT_TLMM_IO(FUXI_BT_GPIO_BT_EN), 0, &cleanup);
        (void)fuxi_bt_board_cleanup_write(board, FUXI_BT_TLMM_CTL(FUXI_BT_GPIO_BT_EN), board->bt_en_ctl, &cleanup);
        (void)fuxi_bt_board_cleanup_write(board, FUXI_BT_TLMM_IO(FUXI_BT_GPIO_BT_EN),
            board->bt_en_io & FUXI_BT_TLMM_OUT, &cleanup);
        board->bt_en_changed = false;
    }
    if (board->pins_changed) {
        for (unsigned index = 0; index < 4; index++) {
            (void)fuxi_bt_board_cleanup_write(board, FUXI_BT_TLMM_CTL(FUXI_BT_GPIO_UART_FIRST + index),
                board->pin_ctl[index], &cleanup);
        }
        board->pins_changed = false;
    }
    if (board->rcg_changed) {
        static const uint32_t offsets[] = {4, 8, 12, 16};
        uint32_t command = 0;
        bool updated = false;
        for (unsigned index = 1; index < 4; index++) {
            (void)fuxi_bt_board_cleanup_write(board, FUXI_BT_GCC_BASE + FUXI_BT_RCG_CMD + offsets[index],
                board->rcg_saved[index], &cleanup);
        }
        (void)fuxi_bt_board_cleanup_write(board, FUXI_BT_GCC_BASE + FUXI_BT_RCG_CMD + 4,
            board->rcg_saved[0], &cleanup);
        if (board->io.read(board->io.context, FUXI_BT_GCC_BASE + FUXI_BT_RCG_CMD, &command) == FUXI_BT_OK &&
            board->io.write(board->io.context, FUXI_BT_GCC_BASE + FUXI_BT_RCG_CMD,
                command | FUXI_BT_RCG_UPDATE) == FUXI_BT_OK) {
            for (unsigned attempt = 0; attempt <= FUXI_BT_RCG_TIMEOUT_US / 10u; attempt++) {
                if (board->io.read(board->io.context, FUXI_BT_GCC_BASE + FUXI_BT_RCG_CMD, &command) != FUXI_BT_OK) {
                    break;
                }
                if (!(command & FUXI_BT_RCG_UPDATE)) {
                    updated = true;
                    break;
                }
                board->io.wait_us(board->io.context, 10);
            }
        }
        if (!updated && cleanup == FUXI_BT_OK) {
            cleanup = FUXI_BT_TIMEOUT;
        }
        board->rcg_changed = false;
        board->rcg_hz = 0;
    }
    if (board->votes_changed) {
        uint32_t vote = 0;
        if (board->io.read(board->io.context, FUXI_BT_GCC_BASE + FUXI_BT_VOTE_REG, &vote) != FUXI_BT_OK) {
            if (cleanup == FUXI_BT_OK) {
                cleanup = FUXI_BT_IO;
            }
        } else {
            (void)fuxi_bt_board_cleanup_write(board, FUXI_BT_GCC_BASE + FUXI_BT_VOTE_REG,
                vote & ~board->added_votes, &cleanup);
        }
        board->votes_changed = false;
    }
    return cleanup;
}

static enum fuxi_bt_status fuxi_bt_board_se_read(void *context, uint32_t reg, uint32_t *value)
{
    struct fuxi_bt_board *board = context;
    if ((reg & 3u) || reg >= 0x4000u) {
        return FUXI_BT_ARGUMENT;
    }
    return board->io.read(board->io.context, FUXI_BT_UART_BASE + reg, value);
}

static enum fuxi_bt_status fuxi_bt_board_se_write(void *context, uint32_t reg, uint32_t value)
{
    struct fuxi_bt_board *board = context;
    if ((reg & 3u) || reg >= 0x4000u) {
        return FUXI_BT_ARGUMENT;
    }
    return board->io.write(board->io.context, FUXI_BT_UART_BASE + reg, value);
}

static uint64_t fuxi_bt_board_now(void *context)
{
    struct fuxi_bt_board *board = context;
    return board->io.now_us(board->io.context);
}

static void fuxi_bt_board_wait(void *context, unsigned microseconds)
{
    struct fuxi_bt_board *board = context;
    board->io.wait_us(board->io.context, microseconds);
}

/*
 * baud 必須在速率表內。RCG 來源頻率不同時，趁 FIFO 關著（GENI 引擎閒置、RFR 已還原）先改 RCG，
 * 再用新的來源頻率與 baud 開 FIFO；對照 Linux qcom_geni_serial_set_termios 的 dev_pm_opp_set_rate＋
 * M/S_CLK_CFG。
 */
static enum fuxi_bt_status fuxi_bt_board_acquire(void *context, unsigned baud)
{
    struct fuxi_bt_board *board = context;
    const struct fuxi_bt_geni_fifo_io io = {
        board, fuxi_bt_board_se_read, fuxi_bt_board_se_write, fuxi_bt_board_now
    };
    const struct fuxi_bt_rate *rate = fuxi_bt_rate_find(baud);
    struct fuxi_bt_geni_fifo_config config;
    enum fuxi_bt_status status;
    if (!rate) {
        return FUXI_BT_ARGUMENT;
    }
    if (board->fifo_used) {
        return FUXI_BT_BUSY;
    }
    if (board->stage != FUXI_BT_STAGE_READY) {
        return FUXI_BT_NO_UART;
    }
    if (board->rcg_hz != rate->source_hz) {
        status = fuxi_bt_board_rcg_program(board, rate);
        if (status != FUXI_BT_OK) {
            return status;
        }
        board->rcg_switches++;
    }
    config = (struct fuxi_bt_geni_fifo_config){1, true, board->qup_version, rate->source_hz, rate->baud};
    board->baud = rate->baud;
    board->fifo_used = true;
    status = fuxi_bt_geni_fifo_open(&board->fifo, &io, &config);
    for (unsigned poll = 0; status == FUXI_BT_AGAIN && poll < FUXI_BT_ACQUIRE_POLLS; poll++) {
        board->io.wait_us(board->io.context, 10);
        status = fuxi_bt_geni_fifo_poll(&board->fifo);
    }
    return status == FUXI_BT_AGAIN ? FUXI_BT_TIMEOUT : status;
}

static enum fuxi_bt_status fuxi_bt_board_uart_write(void *context, const uint8_t *bytes,
    size_t size, size_t *accepted)
{
    struct fuxi_bt_board *board = context;
    return fuxi_bt_geni_fifo_write(&board->fifo, bytes, size, accepted);
}

static enum fuxi_bt_status fuxi_bt_board_uart_read(void *context, uint8_t *bytes,
    size_t capacity, size_t *received)
{
    struct fuxi_bt_board *board = context;
    return fuxi_bt_geni_fifo_read(&board->fifo, bytes, capacity, received);
}

static enum fuxi_bt_status fuxi_bt_board_release(void *context)
{
    struct fuxi_bt_board *board = context;
    enum fuxi_bt_status status;
    if (!board->fifo_used) {
        return FUXI_BT_OK;
    }
    status = fuxi_bt_geni_fifo_close_poll(&board->fifo);
    for (unsigned poll = 0; status == FUXI_BT_AGAIN && poll < FUXI_BT_RELEASE_POLLS; poll++) {
        board->io.wait_us(board->io.context, 10);
        status = fuxi_bt_geni_fifo_close_poll(&board->fifo);
    }
    board->fifo_used = false;
    return status == FUXI_BT_AGAIN ? FUXI_BT_TIMEOUT : status;
}

void fuxi_bt_board_uart_ops(struct fuxi_bt_board *board, struct fuxi_bt_uart_ops *ops)
{
    if (!ops) {
        return;
    }
    memset(ops, 0, sizeof(*ops));
    if (!board) {
        return;
    }
    ops->context = board;
    ops->acquire = fuxi_bt_board_acquire;
    ops->write = fuxi_bt_board_uart_write;
    ops->read = fuxi_bt_board_uart_read;
    ops->release = fuxi_bt_board_release;
    ops->now_us = fuxi_bt_board_now;
    ops->wait_us = fuxi_bt_board_wait;
}
