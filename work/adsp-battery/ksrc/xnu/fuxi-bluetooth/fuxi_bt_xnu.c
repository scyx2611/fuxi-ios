#include <pexpert/arm64/board_config.h>
#if defined(FUXI)
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/vnode.h>
#include <sys/uio.h>
#include <kern/thread.h>
#include <kern/clock.h>
#include <kern/sched_prim.h>
#include <pexpert/pexpert.h>
#include <pexpert/device_tree.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>
#include <string.h>
#include "fuxi_bt.h"
#include "fuxi_bt_board.h"
#include "fuxi_bt_pump.h"
#include "fuxi_bt_setup.h"
#include "fuxi_bt_sk.h"

extern kern_return_t pmap_enter_options_addr(pmap_t, vm_map_offset_t,
    uint64_t, vm_prot_t, vm_prot_t, unsigned, boolean_t, unsigned, void *);
extern unsigned fuxi_gpu_access(uintptr_t, uint64_t *, unsigned, unsigned);
extern kern_return_t kernel_thread_start(thread_continue_t, void *, thread_t *);
extern kern_return_t thread_terminate(thread_t);
extern void thread_deallocate(thread_t);
extern wait_result_t thread_block(thread_continue_t);

struct fuxi_bt_window {
    uint64_t pa;
    vm_size_t size;
    vm_offset_t va;
};

static struct fuxi_bt_window fuxi_bt_windows[] = {
    {0x11c000, 0x4000, 0},
    {0x120000, 0x4000, 0},
    {0x150000, 0x4000, 0},
    {0xf14c000, 0x8000, 0},
    {FUXI_BT_UART_BASE, 0x4000, 0},
    {FUXI_BT_WRAPPER_BASE, 0x4000, 0},
    {FUXI_BT_GCC_BASE, 0x4000, 0} /* GPLL0 MODE／USER_CTL（fuxi,bt=3） */
};
static struct fuxi_bt_board fuxi_bt_board_state;
static struct fuxi_bt_uart_ops fuxi_bt_uart;
static unsigned fuxi_bt_registration;
static unsigned fuxi_bt_started;
static unsigned fuxi_bt_mode;
static bool fuxi_bt_probe;

enum fuxi_bt_status fuxi_bt_register_uart(const struct fuxi_bt_uart_ops *ops)
{
    unsigned expected = 0;
    if (!fuxi_bt_uart_ops_valid(ops)) {
        return FUXI_BT_ARGUMENT;
    }
    if (!__atomic_compare_exchange_n(&fuxi_bt_registration, &expected, 1,
        false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        return FUXI_BT_BUSY;
    }
    fuxi_bt_uart = *ops;
    __atomic_store_n(&fuxi_bt_registration, 2, __ATOMIC_RELEASE);
    return FUXI_BT_OK;
}

static enum fuxi_bt_status fuxi_bt_map(struct fuxi_bt_window *window)
{
    vm_offset_t address = 0;
    kern_return_t status;
    if (window->va) {
        return FUXI_BT_OK;
    }
    if ((window->pa & PAGE_MASK) || (window->size & PAGE_MASK)) {
        return FUXI_BT_ARGUMENT;
    }
    status = kmem_alloc(kernel_map, &address, window->size,
        KMA_VAONLY | KMA_KOBJECT | KMA_NOPAGEWAIT, VM_KERN_MEMORY_IOKIT);
    if (status != KERN_SUCCESS) {
        return FUXI_BT_IO;
    }
    for (vm_size_t offset = 0; offset < window->size; offset += PAGE_SIZE) {
        status = pmap_enter_options_addr(kernel_pmap, address + offset,
            window->pa + offset, VM_PROT_READ | VM_PROT_WRITE, VM_PROT_NONE,
            VM_WIMG_IO, TRUE, PMAP_OPTIONS_NOWAIT, NULL);
        if (status != KERN_SUCCESS) {
            pmap_remove(kernel_pmap, address, address + window->size);
            kmem_free(kernel_map, address, window->size);
            return FUXI_BT_IO;
        }
    }
    window->va = address;
    return FUXI_BT_OK;
}

static void fuxi_bt_unmap(void)
{
    for (unsigned index = 0; index < sizeof(fuxi_bt_windows) / sizeof(fuxi_bt_windows[0]); index++) {
        struct fuxi_bt_window *window = &fuxi_bt_windows[index];
        if (window->va) {
            pmap_remove(kernel_pmap, window->va, window->va + window->size);
            kmem_free(kernel_map, window->va, window->size);
            window->va = 0;
        }
    }
}

static enum fuxi_bt_status fuxi_bt_read32(void *context,
    uint64_t pa, uint32_t *value)
{
    (void)context;
    if (pa & 3) {
        return FUXI_BT_ARGUMENT;
    }
    for (unsigned index = 0; index < sizeof(fuxi_bt_windows) / sizeof(fuxi_bt_windows[0]); index++) {
        struct fuxi_bt_window *window = &fuxi_bt_windows[index];
        if (pa >= window->pa && pa - window->pa <= window->size - 4) {
            uint64_t raw = 0;
            enum fuxi_bt_status status = fuxi_bt_map(window);
            if (status != FUXI_BT_OK) {
                return status;
            }
            if (fuxi_gpu_access(window->va + (uintptr_t)(pa - window->pa), &raw, 0, 0)) {
                printf("FUXI BT: read fault pa=%#llx\n", pa);
                return FUXI_BT_IO;
            }
            *value = (uint32_t)raw;
            return FUXI_BT_OK;
        }
    }
    return FUXI_BT_ARGUMENT;
}

static enum fuxi_bt_status fuxi_bt_write32(void *context,
    uint64_t pa, uint32_t value)
{
    (void)context;
    if (pa & 3) {
        return FUXI_BT_ARGUMENT;
    }
    for (unsigned index = 0; index < sizeof(fuxi_bt_windows) / sizeof(fuxi_bt_windows[0]); index++) {
        struct fuxi_bt_window *window = &fuxi_bt_windows[index];
        if (pa >= window->pa && pa - window->pa <= window->size - 4) {
            uint64_t raw = value;
            enum fuxi_bt_status status = fuxi_bt_map(window);
            if (status != FUXI_BT_OK) {
                return status;
            }
            if (fuxi_gpu_access(window->va + (uintptr_t)(pa - window->pa), &raw, 1, 0)) {
                printf("FUXI BT: write fault pa=%#llx\n", pa);
                return FUXI_BT_IO;
            }
            return FUXI_BT_OK;
        }
    }
    return FUXI_BT_ARGUMENT;
}

static uint64_t fuxi_bt_now_us(void *context)
{
    uint64_t absolute, nanoseconds;
    (void)context;
    clock_get_uptime(&absolute);
    absolutetime_to_nanoseconds(absolute, &nanoseconds);
    return nanoseconds / 1000u;
}

static void fuxi_bt_wait_us(void *context, unsigned microseconds)
{
    (void)context;
    delay((int)microseconds);
}

static void fuxi_bt_print_probe(enum fuxi_bt_status status, const struct fuxi_bt_result *result)
{
    printf("FUXI BT: probe status=%s cleanup=%s tx=%u rx=%u drained=%u frames=%u ignored=%u polls=%u elapsed_us=%llu controller_status=%#x valid=%u\n",
        fuxi_bt_status_name(status), fuxi_bt_status_name(result->cleanup),
        result->tx_bytes, result->rx_bytes, result->drained_bytes, result->frames,
        result->ignored_frames, result->polls, result->elapsed_us,
        (unsigned)result->controller_status, (unsigned)result->version_valid);
    if (result->reply_size == sizeof(result->reply)) {
        /* 原始版本回應（H4 0x04 起 21 位元組），解析有疑問時不必再跑一趟就能對照。 */
        static const char digits[] = "0123456789abcdef";
        char hex[sizeof(result->reply) * 2 + 1];
        for (unsigned index = 0; index < sizeof(result->reply); index++) {
            hex[index * 2] = digits[result->reply[index] >> 4];
            hex[index * 2 + 1] = digits[result->reply[index] & 15];
        }
        hex[sizeof(hex) - 1] = 0;
        printf("FUXI BT: reply %s\n", hex);
    }
    if (result->version_valid) {
        printf("FUXI BT: QCA version product=%#x soc=%#x rom=%#x patch=%#x\n",
            result->version.product, result->version.soc,
            (unsigned)result->version.rom, (unsigned)result->version.patch);
    }
}

/*
 * fuxi,bt=4 的韌體檔：只讀 /usr/local/share/fuxi/bt/<基本檔名>（rampatch hmtbtfw20.tlv、NVM hmtnv20.*，
 * 從手機 Android vendor 分區複製，不編進核心）。讀法同 fuxi_wlan.c wl14_read；不存在回 OFF 讓引擎試下一個名字。
 */
static enum fuxi_bt_status fuxi_bt_file_load(void *context, const char *name, size_t limit,
    struct fuxi_bt_blob *blob)
{
    char path[96];
    vnode_t file = NULL;
    vfs_context_t vfs = vfs_context_kernel();
    struct vnode_attr attributes;
    vm_offset_t memory = 0;
    enum fuxi_bt_status status = FUXI_BT_OK;
    int residual = 0, error;
    (void)context;
    blob->data = NULL;
    blob->size = 0;
    if (!fuxi_bt_setup_name_valid(name)) {
        return FUXI_BT_ARGUMENT;
    }
    snprintf(path, sizeof(path), "/usr/local/share/fuxi/bt/%s", name);
    error = vnode_open(path, FREAD | O_NOFOLLOW | O_NONBLOCK, 0, 0, &file, vfs);
    if (error) {
        if (error != ENOENT) {
            printf("FUXI BT: setup file %s open errno=%d\n", path, error);
        }
        return error == ENOENT ? FUXI_BT_OFF : FUXI_BT_IO;
    }
    VATTR_INIT(&attributes);
    VATTR_WANTED(&attributes, va_data_size);
    error = vnode_getattr(file, &attributes, vfs);
    if (error || vnode_vtype(file) != VREG || !VATTR_IS_SUPPORTED(&attributes, va_data_size) ||
        !attributes.va_data_size || attributes.va_data_size > limit) {
        printf("FUXI BT: setup file %s errno=%d size=%llu limit=%lu rejected\n", path, error,
            (unsigned long long)attributes.va_data_size, (unsigned long)limit);
        status = FUXI_BT_IO;
    } else if (kmem_alloc(kernel_map, &memory, round_page((vm_size_t)attributes.va_data_size),
        KMA_DATA | KMA_ZERO | KMA_NOPAGEWAIT, VM_KERN_MEMORY_IOKIT) != KERN_SUCCESS) {
        status = FUXI_BT_IO;
        memory = 0;
    } else {
        error = vn_rdwr(UIO_READ, file, (caddr_t)memory, (int)attributes.va_data_size, 0,
            UIO_SYSSPACE, 0, vfs_context_ucred(vfs), &residual, vfs_context_proc(vfs));
        if (error || residual) {
            printf("FUXI BT: setup file %s read errno=%d residual=%d\n", path, error, residual);
            status = FUXI_BT_IO;
        }
    }
    if (vnode_close(file, FREAD, vfs) && status == FUXI_BT_OK) {
        status = FUXI_BT_IO;
    }
    if (status != FUXI_BT_OK) {
        if (memory) {
            kmem_free(kernel_map, memory, round_page((vm_size_t)attributes.va_data_size));
        }
        return status;
    }
    blob->data = (uint8_t *)memory;
    blob->size = (size_t)attributes.va_data_size;
    printf("FUXI BT: setup file %s bytes=%lu\n", path, (unsigned long)blob->size);
    return FUXI_BT_OK;
}

static void fuxi_bt_file_release(void *context, struct fuxi_bt_blob *blob)
{
    (void)context;
    if (blob->data) {
        kmem_free(kernel_map, (vm_offset_t)blob->data, round_page((vm_size_t)blob->size));
    }
    blob->data = NULL;
    blob->size = 0;
}

static void fuxi_bt_hex(const uint8_t *bytes, unsigned size, char *out, unsigned capacity)
{
    static const char digits[] = "0123456789abcdef";
    unsigned index = 0;
    for (; index < size && index * 2 + 2 < capacity; index++) {
        out[index * 2] = digits[bytes[index] >> 4];
        out[index * 2 + 1] = digits[bytes[index] & 15];
    }
    out[index * 2] = 0;
}

static void fuxi_bt_print_tlv(const char *what, const char *name, const struct fuxi_bt_tlv_info *info)
{
    printf("FUXI BT: setup %s %s file=%u tlv=%u type=%u mode=%u product=%#x rom=%#x patch=%#x tags=%u segments=%u acks=%u final_ack=%u\n",
        what, name, info->file_bytes, info->tlv_bytes, (unsigned)info->type, (unsigned)info->download_mode,
        (unsigned)info->product, (unsigned)info->rom, (unsigned)info->patch, (unsigned)info->tags,
        info->segments, info->acks, (unsigned)info->final_ack);
}

static void fuxi_bt_print_setup(enum fuxi_bt_status status, const struct fuxi_bt_setup_result *result)
{
    char hex[sizeof(result->last_event) * 2 + 1];
    printf("FUXI BT: setup status=%s stage=%s cleanup=%s controller_status=%#x elapsed_ms=%llu patch_ms=%llu tx=%u rx=%u frames=%u ignored=%u vendor=%u stale=%u\n",
        fuxi_bt_status_name(status), fuxi_bt_setup_stage_name(result->stage),
        fuxi_bt_status_name(result->cleanup), (unsigned)result->controller_status,
        result->elapsed_us / 1000u, result->patch_us / 1000u, result->tx_bytes, result->rx_bytes,
        result->frames, result->ignored_frames, result->vendor_events, result->stale_acks);
    printf("FUXI BT: setup version product=%#x soc=%#x rom=%#x patch=%#x rom_ver=%#x\n",
        result->version.product, result->version.soc, (unsigned)result->version.rom,
        (unsigned)result->version.patch, (unsigned)result->rom_ver);
    if (result->patch.file_bytes) {
        fuxi_bt_print_tlv("rampatch", result->patch_name, &result->patch);
    }
    printf("FUXI BT: setup board_id=%#x valid=%u status=%s\n", (unsigned)result->board_id,
        (unsigned)result->board_id_valid, fuxi_bt_status_name(result->board_id_status));
    if (result->nvm.file_bytes) {
        fuxi_bt_print_tlv("nvm", result->nvm_name, &result->nvm);
        printf("FUXI BT: setup nvm hci_tag=%u %02x%02x%02x->%02x%02x%02x sleep_tag=%u %#x->%#x\n",
            (unsigned)result->nvm.hci_tag, result->nvm.hci_before[0], result->nvm.hci_before[1],
            result->nvm.hci_before[2], result->nvm.hci_after[0], result->nvm.hci_after[1],
            result->nvm.hci_after[2], (unsigned)result->nvm.sleep_tag,
            (unsigned)result->nvm.sleep_before, (unsigned)result->nvm.sleep_after);
    }
    printf("FUXI BT: setup build_info status=%s \"%s\"\n", fuxi_bt_status_name(result->build_info_status),
        result->build_info);
    if (result->local_version_valid) {
        printf("FUXI BT: setup local hci=%#x rev=%#x lmp=%#x manufacturer=%#x subver=%#x\n",
            (unsigned)result->hci_version, (unsigned)result->hci_revision, (unsigned)result->lmp_version,
            (unsigned)result->manufacturer, (unsigned)result->lmp_subversion);
    }
    if (result->bdaddr_valid) {
        printf("FUXI BT: setup bdaddr valid (not logged)\n");
    }
    if (result->baud_requested != FUXI_BT_BOOT_BAUD || result->baud_status != FUXI_BT_OFF) {
        fuxi_bt_hex(result->baud_early, result->baud_early_size, hex, sizeof(hex));
        printf("FUXI BT: setup baud requested=%u active=%u status=%s fallback=%s attempts=%u early=%u %s ms=%llu\n",
            result->baud_requested, result->baud_active, fuxi_bt_status_name(result->baud_status),
            fuxi_bt_status_name(result->baud_fallback), result->baud_attempts, result->baud_early_bytes,
            hex, result->baud_us / 1000u);
    }
    if (result->nolog_status != FUXI_BT_OFF) {
        printf("FUXI BT: setup nolog status=%s\n", fuxi_bt_status_name(result->nolog_status));
    }
    fuxi_bt_hex(result->last_event, result->last_event_size, hex, sizeof(hex));
    printf("FUXI BT: setup last_event %s\n", hex);
    if (result->last_vendor_size) {
        fuxi_bt_hex(result->last_vendor, result->last_vendor_size, hex, sizeof(hex));
        printf("FUXI BT: setup last_vendor %s\n", hex);
    }
}

static unsigned fuxi_bt_wait_event;

static void fuxi_bt_wake(void)
{
    thread_wakeup((event_t)&fuxi_bt_wait_event);
}

static void fuxi_bt_sleep(unsigned us)
{
    if (assert_wait_timeout((event_t)&fuxi_bt_wait_event, THREAD_UNINT, us, NSEC_PER_USEC)
        == THREAD_WAITING) {
        (void)thread_block(THREAD_CONTINUE_NULL);
    }
}

static size_t fuxi_bt_deliver(void *context, const uint8_t *data, size_t size, bool start)
{
    (void)context;
    return fuxi_bt_sk_receive(data, size, start);
}

static size_t fuxi_bt_take(void *context, uint8_t *data, size_t size)
{
    (void)context;
    return fuxi_bt_sk_tx_take(data, size);
}

/* H4 追蹤：每次 UART.SK 連線重新計數（第六趟 200 筆總上限在 bluetoothd 斷線前就用完）；
 * 記錄還沒得到 Command Complete／Status 的命令與最近送出的 opcode。 */
#define FUXI_BT_TRACE_PER_LINK 400u
#define FUXI_BT_TRACE_RECENT 8u
#define FUXI_BT_LINK_DETAIL_LIMIT 192u
#define FUXI_BT_LINK_INQUIRY_LIMIT 64u
struct fuxi_bt_trace_state {
    unsigned count;
    unsigned link_detail_count;
    unsigned link_inquiry_count;
    bool link_detail_enabled;
    bool pending;
    bool pending_reported;
    uint16_t pending_opcode;
    uint64_t pending_since_us;
    uint16_t recent[FUXI_BT_TRACE_RECENT];
    unsigned recent_pos;
    uint64_t slowest_us;
    uint16_t slowest_opcode;
};

static void fuxi_bt_trace(void *context, const struct fuxi_bt_h4_record *record)
{
    struct fuxi_bt_trace_state *state = context;
    const uint64_t now = fuxi_bt_now_us(NULL);
    if (state->link_detail_enabled) {
        int value;
        if (fuxi_bt_link_detail(record, &value)) {
            const bool inquiry = fuxi_bt_link_is_inquiry(record);
            unsigned *counter = inquiry ? &state->link_inquiry_count : &state->link_detail_count;
            const unsigned limit = inquiry ? FUXI_BT_LINK_INQUIRY_LIMIT : FUXI_BT_LINK_DETAIL_LIMIT;
            if (*counter < limit) {
                printf("FUXI BT LINK: ms=%llu dir=%s op=%#x evt=%#x value=%d seq=%u%s\n",
                    now / 1000, record->direction == FUXI_BT_H4_TO_CONTROLLER ? "tx" : "rx",
                    record->has_opcode ? record->opcode : 0, record->event, value,
                    ++(*counter), inquiry ? " inq" : "");
                if (*counter == limit) {
                    printf("FUXI BT LINK: %s limit reached\n", inquiry ? "inquiry" : "detail");
                }
            }
        }
    }
    if (record->direction == FUXI_BT_H4_TO_CONTROLLER && record->type == FUXI_BT_H4_COMMAND &&
        record->has_opcode) {
        state->recent[state->recent_pos++ % FUXI_BT_TRACE_RECENT] = record->opcode;
        state->pending = true;
        state->pending_reported = false;
        state->pending_opcode = record->opcode;
        state->pending_since_us = now;
    } else if (record->direction == FUXI_BT_H4_TO_HOST && record->has_opcode && state->pending &&
        record->opcode == state->pending_opcode) {
        const uint64_t waited = now - state->pending_since_us;
        if (state->pending_reported) {
            printf("FUXI BT H4: late reply opcode=%#x after %llums\n", record->opcode, waited / 1000u);
        }
        if (waited > state->slowest_us) {
            state->slowest_us = waited;
            state->slowest_opcode = record->opcode;
        }
        state->pending = false;
    }
    if (state->count >= FUXI_BT_TRACE_PER_LINK) {
        return;
    }
    if (record->has_opcode) {
        state->count++;
        printf("FUXI BT H4: %s opcode=%#x event=%#x status=%d bytes=%u\n",
            record->direction == FUXI_BT_H4_TO_CONTROLLER ? "tx" : "rx",
            record->opcode, record->event, record->has_status ? record->status : -1, record->length);
    } else if (record->direction == FUXI_BT_H4_TO_HOST && record->type == FUXI_BT_H4_EVENT &&
        record->event != 0x13 && !(record->event == 0x3e &&
        (record->subevent == 0x02 || record->subevent == 0x0d))) {
        /* 沒有 opcode 的事件（硬體錯誤、vendor 事件、連線事件…）；略過已完成封包數與廣播報告 */
        char hex[2 * FUXI_BT_H4_KEEP + 1];
        fuxi_bt_hex(record->keep, record->keep_size, hex, sizeof(hex));
        state->count++;
        printf("FUXI BT H4: rx event=%#x sub=%#x bytes=%u keep=%s\n", record->event,
            record->subevent, record->length, hex);
    }
}

static void fuxi_bt_trace_recent(const struct fuxi_bt_trace_state *state, char *out, unsigned capacity)
{
    const unsigned count = state->recent_pos < FUXI_BT_TRACE_RECENT ? state->recent_pos :
        FUXI_BT_TRACE_RECENT;
    unsigned used = 0;
    out[0] = 0;
    for (unsigned index = 0; index < count && used + 8 < capacity; index++) {
        const uint16_t opcode = state->recent[(state->recent_pos - count + index) % FUXI_BT_TRACE_RECENT];
        static const char digits[] = "0123456789abcdef";
        if (index) {
            out[used++] = ',';
        }
        for (int shift = 12; shift >= 0; shift -= 4) {
            out[used++] = digits[(opcode >> shift) & 15];
        }
        out[used] = 0;
    }
}

static int fuxi_bt_adt_u32(const char *name);
static unsigned fuxi_bt_adt_opcodes(const char *name, uint16_t *opcodes, unsigned capacity);
static unsigned fuxi_bt_adt_masks(const char *name, uint32_t *rules, unsigned capacity);

/* 常駐診斷：bluetoothd 連上後的原始 RX／TX（第四趟 HCI Reset 沒有回應、desync 每秒約 100 byte） */
static void fuxi_bt_raw(const char *what, const uint8_t *bytes, unsigned size, uint64_t since_tx_us)
{
    char hex[2 * 32 + 1];
    fuxi_bt_hex(bytes, size < 32 ? size : 32, hex, sizeof(hex));
    printf("FUXI BT %s: +%llums n=%u %s\n", what, since_tx_us / 1000u, size, hex);
}

static void fuxi_bt_rx_diag(const struct fuxi_bt_pump *pump, uint32_t m_irq, uint32_t s_irq)
{
    const struct fuxi_bt_h4_framer *framer = &pump->host;
    const unsigned ring = sizeof(framer->desync_recent);
    const unsigned count = framer->desync_recent_pos < ring ? framer->desync_recent_pos : ring;
    uint8_t recent[sizeof(framer->desync_recent)];
    char hex[2 * sizeof(framer->desync_recent) + 1];
    for (unsigned index = 0; index < count; index++) {
        recent[index] = framer->desync_recent[(framer->desync_recent_pos - count + index) % ring];
    }
    fuxi_bt_hex(recent, count, hex, sizeof(hex));
    printf("FUXI BT RXDIAG: desync=%llu ibs=%llu/%llu/%llu recent=%s m_irq=%#x s_irq=%#x uart_rx=%llu steps=%llu stalls=%llu ibs_on=%u acks=%llu wakes=%llu wake_timeouts=%llu invalid=%llu resyncs=%llu\n",
        framer->desync_bytes, framer->ibs_bytes[0], framer->ibs_bytes[1], framer->ibs_bytes[2], hex,
        m_irq, s_irq, pump->rx_bytes, pump->steps, pump->rx_stalls, (unsigned)pump->ibs,
        pump->ibs_acks_sent, pump->ibs_wakes_sent, pump->ibs_wake_timeouts, framer->invalid_headers,
        pump->host_resyncs);
}

/* setup_run 已歸還 UART；板級電源仍在。重取同一個 UART，再把 H4 交給 Skywalk。 */
static enum fuxi_bt_status fuxi_bt_resident(struct fuxi_bt_board *board,
    const struct fuxi_bt_uart_ops *ops, const struct fuxi_bt_setup_result *setup)
{
    static struct fuxi_bt_pump pump;
    static struct fuxi_bt_trace_state traces;
    const struct fuxi_bt_pump_sink sink = {NULL, fuxi_bt_deliver, fuxi_bt_take};
    /* setup 換速成功時控制器已在新速率（NVM HCI tag 也是），這裡用同一個速率 */
    enum fuxi_bt_status status = ops->acquire(ops->context, setup->baud_active);
    if (status == FUXI_BT_OK) {
        memset(&traces, 0, sizeof(traces));
        traces.link_detail_enabled = fuxi_bt_adt_u32("fuxi,bt-link-log") == 1;
        status = fuxi_bt_pump_init(&pump, ops, &sink, fuxi_bt_trace, &traces);
        /* fuxi,bt-ibs=1：回應 QCA IBS（預設關，NVM 已關 IBS；只在診斷看到 0xfd 時打開） */
        pump.ibs = fuxi_bt_adt_u32("fuxi,bt-ibs") == 1;
        /* fuxi,bt-optional：u32 opcode 陣列（預設無）；清單內命令的 Command Complete 失敗改回成功 */
        pump.optional_count = fuxi_bt_adt_opcodes("fuxi,bt-optional", pump.optional,
            FUXI_BT_OPTIONAL_MAX);
        /* fuxi,bt-pad：u32 opcode 陣列（預設無）；清單內命令只有 status 的 Command Complete 補兩個 byte */
        pump.pad_count = fuxi_bt_adt_opcodes("fuxi,bt-pad", pump.pad, FUXI_BT_PAD_MAX);
        /* fuxi,bt-mask：u32 規則陣列（預設無）；opcode<<16 | offset<<8 | clear，清 Command Complete 回傳參數位元 */
        pump.mask_count = fuxi_bt_adt_masks("fuxi,bt-mask", pump.mask, FUXI_BT_MASK_MAX);
        /* fuxi,bt-txmask：同格式（預設無），offset 從第一個命令參數起算，清送出命令的參數位元 */
        pump.txmask_count = fuxi_bt_adt_masks("fuxi,bt-txmask", pump.txmask, FUXI_BT_TXMASK_MAX);
    }
    if (status == FUXI_BT_OK && fuxi_bt_sk_boot_result(0, setup->build_info)) {
        status = FUXI_BT_IO;
    }
    if (status != FUXI_BT_OK) {
        return status;
    }
    bool optional_vendor = false;
    for (unsigned index = 0; index < pump.optional_count; index++) {
        optional_vendor |= pump.optional[index] == FUXI_BT_OPTIONAL_VENDOR;
    }
    printf("FUXI BT: resident ready baud=%u ibs=%u optional=%u optional_vendor=%u pad=%u mask=%u txmask=%u\n",
        setup->baud_active, (unsigned)pump.ibs, pump.optional_count, (unsigned)optional_vendor,
        pump.pad_count, pump.mask_count, pump.txmask_count);
    for (unsigned index = 0; index < pump.optional_count; index++) {
        printf("FUXI BT: optional opcode=%#x\n", pump.optional[index]);
    }
    for (unsigned index = 0; index < pump.pad_count; index++) {
        printf("FUXI BT: pad opcode=%#x\n", pump.pad[index]);
    }
    for (unsigned index = 0; index < pump.mask_count; index++) {
        printf("FUXI BT: mask opcode=%#x offset=%u clear=%#x\n", pump.mask[index] >> 16,
            (pump.mask[index] >> 8) & 0xffu, pump.mask[index] & 0xffu);
    }
    for (unsigned index = 0; index < pump.txmask_count; index++) {
        printf("FUXI BT: txmask opcode=%#x offset=%u clear=%#x\n", pump.txmask[index] >> 16,
            (pump.txmask[index] >> 8) & 0xffu, pump.txmask[index] & 0xffu);
    }
    uint64_t next_wlan_check = 0, next_stats = 0, last_tx_us = 0, optional_seen = 0, next_link = 0;
    uint64_t padded_seen = 0, masked_seen = 0, txmasked_seen = 0;
    uint32_t link_connects = 0, resync_connects = fuxi_bt_sk_connects();
    bool link_rings = false;
    char recent[FUXI_BT_TRACE_RECENT * 5 + 1];
    unsigned rx_dumps = 0, tx_dumps = 0, rx_armed = 8;
    uint32_t m_irq_seen = 0, s_irq_seen = 0;
    for (;;) {
        uint64_t now = fuxi_bt_now_us(NULL);
        if (now >= next_wlan_check) {
            bool ready = false;
            status = fuxi_bt_board_wlan_ready(&board->io, &ready);
            if (status != FUXI_BT_OK || !ready) {
                printf("FUXI BT: resident WLAN_EN lost status=%s\n", fuxi_bt_status_name(status));
                return status == FUXI_BT_OK ? FUXI_BT_OFF : status;
            }
            next_wlan_check = now + 250000u;
        }
        uint32_t connects = fuxi_bt_sk_connects();
        if (connects != resync_connects) {
            /* 新的 bluetoothd 連上：丟掉上一個連線留下的半個封包與 RX 殘餘，從下一個封包開頭重新對齊 */
            resync_connects = connects;
            uint64_t dropped = pump.resync_dropped, partial = pump.resync_partial;
            fuxi_bt_pump_resync_host(&pump);
            printf("FUXI BT: link %u resync dropped=%llu partial=%llu\n", connects,
                pump.resync_dropped - dropped, pump.resync_partial - partial);
        }
        struct fuxi_bt_pump_activity activity;
        status = fuxi_bt_pump_step(&pump, &activity);
        if (status != FUXI_BT_OK) {
            return status;
        }
        m_irq_seen |= board->fifo.master_irq;
        s_irq_seen |= board->fifo.secondary_irq;
        /* pump_step 內的 TX trace 以當下時間記錄 pending_since_us。 */
        now = fuxi_bt_now_us(NULL);
        if (now >= next_link) {
            /* 每 100 ms：新連線重設追蹤與原始 dump 配額；環收掉時印最後狀態；命令 2 秒沒回應就報 */
            struct fuxi_bt_sk_stats link;
            fuxi_bt_sk_get_stats(&link);
            if (link.connects != link_connects) {
                link_connects = link.connects;
                traces.count = 0;
                traces.link_detail_count = 0;
                traces.link_inquiry_count = 0;
                tx_dumps = rx_dumps = 0;
                rx_armed = 8;
                printf("FUXI BT: link %u trace reset\n", link.connects);
            }
            if (link.rings != link_rings) {
                link_rings = link.rings;
                if (!link.rings) {
                    fuxi_bt_trace_recent(&traces, recent, sizeof(recent));
                    printf("FUXI BT: link %u down pending=%u opcode=%#x waited=%llums recent=%s slowest=%#x/%llums\n",
                        link.connects, (unsigned)traces.pending, traces.pending_opcode,
                        traces.pending ? (now - traces.pending_since_us) / 1000u : 0, recent,
                        traces.slowest_opcode, traces.slowest_us / 1000u);
                    fuxi_bt_rx_diag(&pump, m_irq_seen, s_irq_seen);
                }
            }
            if (traces.pending && !traces.pending_reported && now - traces.pending_since_us > 2000000u) {
                traces.pending_reported = true;
                fuxi_bt_trace_recent(&traces, recent, sizeof(recent));
                printf("FUXI BT H4: no reply opcode=%#x after 2000ms recent=%s\n", traces.pending_opcode,
                    recent);
                fuxi_bt_rx_diag(&pump, m_irq_seen, s_irq_seen);
            }
            next_link = now + 100000u;
        }
        if (pump.optional_patched != optional_seen) {
            optional_seen = pump.optional_patched;
            printf("FUXI BT OPTIONAL: opcode=%#x status=%#x -> 0 count=%llu\n",
                pump.optional_last_opcode, pump.optional_last_status, optional_seen);
        }
        if (pump.padded != padded_seen) {
            padded_seen = pump.padded;
            printf("FUXI BT PAD: opcode=%#x subop=%#x count=%llu\n", pump.pad_last_opcode,
                pump.pad_last_subop, padded_seen);
        }
        if (pump.masked != masked_seen) {
            masked_seen = pump.masked;
            printf("FUXI BT MASK: opcode=%#x offset=%u %#x -> %#x count=%llu\n", pump.mask_last_opcode,
                pump.mask_last_offset, pump.mask_last_before, pump.mask_last_after, masked_seen);
        }
        if (pump.txmasked != txmasked_seen) {
            /* 掃描參數每次重設都會改寫：前 32 次逐筆印，之後只在次數是 2 的冪時印 */
            txmasked_seen = pump.txmasked;
            if (txmasked_seen <= 32 || !(txmasked_seen & (txmasked_seen - 1))) {
                printf("FUXI BT TXMASK: opcode=%#x offset=%u %#x -> %#x count=%llu\n", pump.txmask_last_opcode,
                    pump.txmask_last_offset, pump.txmask_last_before, pump.txmask_last_after, txmasked_seen);
            }
        }
        if (activity.tx_taken && tx_dumps < 80) {
            /* 這一步從上層取的塊在 tx[0..taken) */
            tx_dumps++;
            rx_armed = 12;
            last_tx_us = now;
            fuxi_bt_raw("TXRAW", pump.tx, activity.tx_taken, 0);
        }
        if (activity.rx_received && rx_armed && rx_dumps < 160) {
            /* 這一步讀到的塊在 rx[0..received)（讀只在 rx 清空後從頭寫） */
            rx_armed--;
            rx_dumps++;
            fuxi_bt_raw("RXRAW", pump.rx, activity.rx_received, now - last_tx_us);
        }
        if (now >= next_stats) {
            struct fuxi_bt_sk_stats stats;
            fuxi_bt_sk_get_stats(&stats);
            printf("FUXI BT SK: stats links=%u channels=%u rings=%u tx=%llu/%llu rx=%llu/%llu dropped=%llu acl=%llu/%llu le=%llu framed=%llu debug=%llu/%llu desync=%llu\n",
                stats.connects, stats.channels, (unsigned)stats.rings, stats.tx_bytes, stats.tx_syncs,
                stats.rx_bytes, stats.rx_syncs, stats.rx_dropped, pump.to_controller.packets[FUXI_BT_H4_ACL],
                pump.to_host.packets[FUXI_BT_H4_ACL], pump.to_host.le_adv_reports, pump.host.packets,
                pump.host.debug_packets, pump.host.debug_bytes, pump.host.desync_bytes);
            fuxi_bt_rx_diag(&pump, m_irq_seen, s_irq_seen);
            m_irq_seen = s_irq_seen = 0;
            next_stats = now + 10000000u;
        }
        unsigned cadence = 5000;
        if (board->fifo.tx_size || activity.tx_pending) {
            cadence = 250;
        } else if (activity.rx_received || activity.rx_delivered || activity.tx_accepted) {
            cadence = 1000;
        }
        fuxi_bt_sleep(cadence);
    }
}

/*
 * fuxi,bt=3：等 WLAN_EN 穩定拉高（WCN 電源軌由 WLAN 開好）連續 15 秒，最多等 300 秒；
 * 之後開 SE6 時脈與腳位、拉 BT_EN、以 115200 送 QCA 版本查詢，結束後全部還原。
 * fuxi,bt=4：同樣上電，改跑 fuxi_bt_setup_run（rampatch→NVM→HCI reset），結束後同樣還原。
 * fuxi,bt=5：初始化成功後保留板級電源，直到常駐收送發生錯誤才還原。
 */
#define FUXI_BT_WLAN_STEADY_US 15000000ull
#define FUXI_BT_WLAN_DEADLINE_US 300000000ull
#define FUXI_BT_WLAN_POLL_US 250000u

static enum fuxi_bt_status fuxi_bt_board_session(void)
{
    const struct fuxi_bt_board_io io = {
        NULL, fuxi_bt_read32, fuxi_bt_write32, fuxi_bt_now_us, fuxi_bt_wait_us
    };
    struct fuxi_bt_board *board = &fuxi_bt_board_state;
    struct fuxi_bt_uart_ops ops;
    struct fuxi_bt_result result;
    enum fuxi_bt_status status, cleanup;
    const uint64_t start = fuxi_bt_now_us(NULL);
    uint64_t steady = 0, now = start;
    bool steady_seen = false;
    for (;;) {
        bool ready = false;
        status = fuxi_bt_board_wlan_ready(&io, &ready);
        now = fuxi_bt_now_us(NULL);
        if (status != FUXI_BT_OK) {
            printf("FUXI BT: board WLAN_EN read status=%s; BT_EN untouched\n",
                fuxi_bt_status_name(status));
            return status;
        }
        if (!ready) {
            steady_seen = false;
        } else if (!steady_seen) {
            steady_seen = true;
            steady = now;
        }
        if (steady_seen && now - steady >= FUXI_BT_WLAN_STEADY_US) {
            break;
        }
        if (now - start >= FUXI_BT_WLAN_DEADLINE_US) {
            printf("FUXI BT: board WLAN_EN not steady high for 15 s within 300 s; BT_EN untouched\n");
            return FUXI_BT_TIMEOUT;
        }
        if (fuxi_bt_mode == 5) {
            fuxi_bt_sleep(FUXI_BT_WLAN_POLL_US);
        } else {
            fuxi_bt_wait_us(NULL, FUXI_BT_WLAN_POLL_US);
        }
    }
    printf("FUXI BT: board WLAN_EN steady after %llu ms\n", (now - start) / 1000u);
    status = fuxi_bt_board_power_on(board, &io);
    printf("FUXI BT: board power status=%s stage=%s value=%#x gpll0_mode=%#x gpll0_user=%#x vote=%#x added=%#x rcg_cfg=%#x->%#x qup=%#x fw=%#x sw_ctrl=%u->%u\n",
        fuxi_bt_status_name(status), fuxi_bt_board_stage_name(board->stage),
        board->failed_value, board->gpll0_mode, board->gpll0_user, board->vote,
        board->added_votes, board->rcg_saved[0], board->rcg_cfg, board->qup_version,
        board->firmware, board->sw_ctrl_before, board->sw_ctrl_after);
    if (status == FUXI_BT_OK) {
        fuxi_bt_board_uart_ops(board, &ops);
        if (fuxi_bt_mode >= 4) {
            static struct fuxi_bt_setup_result setup; /* 約 0.5 KB，不放核心堆疊 */
            const struct fuxi_bt_files files = {NULL, fuxi_bt_file_load, fuxi_bt_file_release};
            struct fuxi_bt_setup_options options = {0, false};
            /* fuxi,bt-baud：u32 工作速率（預設無＝115200）；必須在速率表內，否則忽略 */
            const int baud = fuxi_bt_adt_u32("fuxi,bt-baud");
            if (baud > 0 && fuxi_bt_rate_find((uint32_t)baud)) {
                options.baud = (uint32_t)baud;
            } else if (baud != 0) {
                printf("FUXI BT: fuxi,bt-baud %d ignored (not in rate table)\n", baud);
            }
            /* fuxi,bt-nolog=1：NVM 後送 0xfc17 關控制器日誌（預設關） */
            options.disable_logging = fuxi_bt_adt_u32("fuxi,bt-nolog") == 1;
            status = fuxi_bt_setup_run_options(&ops, &files, &options, &setup);
            fuxi_bt_print_setup(status, &setup);
            printf("FUXI BT: uart baud=%u div=%u rcg_hz=%u rcg_switches=%u nolog=%u\n",
                board->baud, board->fifo.divider, board->rcg_hz, board->rcg_switches,
                (unsigned)options.disable_logging);
            if (status == FUXI_BT_OK && fuxi_bt_mode == 5) {
                status = fuxi_bt_resident(board, &ops, &setup);
                printf("FUXI BT: resident stopped status=%s\n", fuxi_bt_status_name(status));
            }
        } else {
            status = fuxi_bt_probe_run(1, &ops, &result);
            fuxi_bt_print_probe(status, &result);
        }
        printf("FUXI BT: fifo state=%u error=%s cleanup=%s fault=%u reg=%#x%s tx=%llu/%llu rx=%llu discarded=%llu m_irq=%#x s_irq=%#x depth=%u/%u\n",
            (unsigned)board->fifo.state, fuxi_bt_status_name(board->fifo.error),
            fuxi_bt_status_name(board->fifo.cleanup), (unsigned)board->fifo.io_fault,
            board->fifo.failed_register, board->fifo.failed_write ? " write" : "",
            board->fifo.tx_accepted, board->fifo.tx_completed, board->fifo.rx_delivered,
            board->fifo.rx_discarded, board->fifo.master_irq, board->fifo.secondary_irq,
            board->fifo.tx_depth, board->fifo.rx_depth);
    }
    cleanup = fuxi_bt_board_power_off(board);
    printf("FUXI BT: board restore status=%s\n", fuxi_bt_status_name(cleanup));
    return status == FUXI_BT_OK ? cleanup : status;
}

static void fuxi_bt_worker(void *argument, wait_result_t wait_result)
{
    struct fuxi_bt_snapshot snapshot;
    enum fuxi_bt_status status;
    (void)argument;
    (void)wait_result;
    if (fuxi_bt_mode == 5) {
        fuxi_bt_sk_start(fuxi_bt_wake, fuxi_bt_probe);
    }
    status = fuxi_bt_snapshot_read(fuxi_bt_read32, NULL, &snapshot);
    printf("FUXI BT: survey status=%s vote=%#x gpll0_vote=%#x off_mask=%#x engine_read=%u\n",
        fuxi_bt_status_name(status), snapshot.vote, snapshot.gpll0_vote,
        snapshot.off_mask, (unsigned)snapshot.engine_read);
    if (status == FUXI_BT_OK) {
        printf("FUXI BT: branches core=%#x s_ahb=%#x m_ahb=%#x core2x=%#x se6=%#x\n",
            snapshot.branches[0], snapshot.branches[1], snapshot.branches[2],
            snapshot.branches[3], snapshot.branches[4]);
        printf("FUXI BT: rcg cmd=%#x cfg=%#x m=%#x n=%#x d=%#x dfs=%#x\n",
            snapshot.rcg[0], snapshot.rcg[1], snapshot.rcg[2], snapshot.rcg[3],
            snapshot.rcg[4], snapshot.rcg[5]);
        for (unsigned index = 0; index < 8; index++) {
            printf("FUXI BT: gpio%u ctl=%#x io=%#x\n", 76 + index,
                snapshot.pin_ctl[index], snapshot.pin_io[index]);
        }
        if (snapshot.engine_read) {
            printf("FUXI BT: engine wrapper=%#x fw=%#x protocol=%u if_disable=%#x status=%#x m_irq=%#x s_irq=%#x\n",
                snapshot.wrapper_version, snapshot.firmware,
                (snapshot.firmware >> 8) & 0xff, snapshot.interface_disable,
                snapshot.engine_status, snapshot.master_irq, snapshot.secondary_irq);
        }
    }
    fuxi_bt_unmap();
    if (status == FUXI_BT_OK && fuxi_bt_mode == 2) {
        struct fuxi_bt_result result;
        const struct fuxi_bt_uart_ops *ops = NULL;
        if (__atomic_load_n(&fuxi_bt_registration, __ATOMIC_ACQUIRE) == 2) {
            ops = &fuxi_bt_uart;
        }
        status = fuxi_bt_probe_run(1, ops, &result);
        fuxi_bt_print_probe(status, &result);
    } else if (status == FUXI_BT_OK && fuxi_bt_mode >= 3) {
        status = fuxi_bt_board_session();
        fuxi_bt_unmap();
    }
    if (fuxi_bt_mode == 5) {
        int error = status == FUXI_BT_TIMEOUT ? ETIMEDOUT : EIO;
        (void)fuxi_bt_sk_boot_result(error, NULL);
    }
    thread_terminate(current_thread());
}

/* 命令列已滿：沒有 fuxibt 時讀 ADT /chosen 的 u32 "fuxi,bt"。 */
/* /chosen 的 u32 陣列 → opcode；長度不是 4 的倍數、超過 capacity 或有值大於 0xffff 就整個不用 */
static unsigned fuxi_bt_adt_opcodes(const char *name, uint16_t *opcodes, unsigned capacity)
{
    DTEntry chosen;
    const void *property = NULL;
    unsigned int bytes = 0;
    if (SecureDTLookupEntry(NULL, "/chosen", &chosen) != kSuccess ||
        SecureDTGetProperty(chosen, name, &property, &bytes) != kSuccess || !property ||
        !bytes || bytes % sizeof(uint32_t) || bytes / sizeof(uint32_t) > capacity) {
        return 0;
    }
    for (unsigned index = 0; index < bytes / sizeof(uint32_t); index++) {
        uint32_t value;
        memcpy(&value, (const uint8_t *)property + index * sizeof(value), sizeof(value));
        if (!value || value > 0xffffu) {
            return 0;
        }
        opcodes[index] = (uint16_t)value;
    }
    return bytes / sizeof(uint32_t);
}

/* /chosen 的 u32 規則陣列（opcode<<16 | offset<<8 | clear）；opcode 或 clear 為 0、長度不對就整個不用 */
static unsigned fuxi_bt_adt_masks(const char *name, uint32_t *rules, unsigned capacity)
{
    DTEntry chosen;
    const void *property = NULL;
    unsigned int bytes = 0;
    if (SecureDTLookupEntry(NULL, "/chosen", &chosen) != kSuccess ||
        SecureDTGetProperty(chosen, name, &property, &bytes) != kSuccess || !property ||
        !bytes || bytes % sizeof(uint32_t) || bytes / sizeof(uint32_t) > capacity) {
        return 0;
    }
    for (unsigned index = 0; index < bytes / sizeof(uint32_t); index++) {
        uint32_t value;
        memcpy(&value, (const uint8_t *)property + index * sizeof(value), sizeof(value));
        if (!(value >> 16) || !(value & 0xffu)) {
            return 0;
        }
        rules[index] = value;
    }
    return bytes / sizeof(uint32_t);
}

static int fuxi_bt_adt_u32(const char *name)
{
    DTEntry chosen;
    const void *property = NULL;
    unsigned int bytes = 0;
    uint32_t value = 0;
    if (SecureDTLookupEntry(NULL, "/chosen", &chosen) == kSuccess &&
        SecureDTGetProperty(chosen, name, &property, &bytes) == kSuccess &&
        property && bytes == sizeof(value)) {
        memcpy(&value, property, sizeof(value));
    }
    return value > 0x7fffffffu ? -1 : (int)value;
}

void fuxi_bt_init(void)
{
    int mode = 0;
    unsigned expected = 0;
    thread_t worker;
    if (!PE_parse_boot_argn("fuxibt", &mode, sizeof(mode))) {
        mode = fuxi_bt_adt_u32("fuxi,bt");
    }
    if (mode == 0) {
        return;
    }
    if (mode < 1 || mode > 5) {
        printf("FUXI BT: mode %d rejected (fuxibt / fuxi,bt)\n", mode);
        return;
    }
    if (!__atomic_compare_exchange_n(&fuxi_bt_started, &expected, 1,
        false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        return;
    }
    fuxi_bt_mode = (unsigned)mode;
    fuxi_bt_probe = mode == 5 && fuxi_bt_adt_u32("fuxi,bt-probe") == 1;
    if (kernel_thread_start(fuxi_bt_worker, NULL, &worker) != KERN_SUCCESS) {
        printf("FUXI BT: worker start failed\n");
        return;
    }
    thread_deallocate(worker);
}
#endif
