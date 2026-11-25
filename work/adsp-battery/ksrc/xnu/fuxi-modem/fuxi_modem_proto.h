/* SPDX-License-Identifier: MIT */
/* 基頻（MPSS）擁有者 fuxi_modem.c 的純邏輯部分：不碰硬體、不配置記憶體，核心與主機測試共用同一份。
 *
 *   fm_assign_page   TrustZone MP_ASSIGN（SIP svc 0xc cmd 0x16，arginfo 0x1117）的參數頁：+0 記憶體清單、+64 來源 VMID、
 *                    +128 目的地（vmid、權限、context）、+240 暫存器放不下的第 4–7 個參數。版面對照 Linux
 *                    drivers/firmware/qcom/qcom_scm.c qcom_scm_assign_mem() 與 HakiOS hakios.mpss.c rmtfs_share()（APSL 2.0，
 *                    只取版面事實，程式為重寫）。
 *   fm_rmtfs_answer  QMI 服務 14（RMTFS v1）：modem 以路徑開檔（/boot/modem_fs1 …）、要一塊共用緩衝區，再以 512 位元組磁區
 *                    經緩衝區讀寫。檔案內容是開機時整份讀進記憶體的 EFS 分區；寫入只留在記憶體，不寫回儲存裝置。
 *                    訊息格式對照 linux-msm/rmtfs（qmi_rmtfs.qmi、rmtfs.c）；caller id 用檔案序號加一（HakiOS 的做法）。
 *   fm_qrtr_ctrl     QRTR v1 控制封包（NEW_SERVER 等）。modem 向應用處理器的名稱服務查詢（NEW_LOOKUP）時，回覆符合的本地
 *                    服務，再以全零 NEW_SERVER 結束（Linux net/qrtr/ns.c ctrl_cmd_new_lookup 的做法）。
 */
#ifndef FUXI_MODEM_PROTO_H
#define FUXI_MODEM_PROTO_H
#include <stddef.h>
#include <stdint.h>

#define FM_VMID_HLOS 3u
#define FM_VMID_MSS_MSA 15u
#define FM_PERM_RW 6u
#define FM_ASSIGN_ARGINFO 0x1117u
#define FM_ASSIGN_PAGE 4096u

#define FM_RMTFS_SERVICE 14u
#define FM_RMTFS_INSTANCE 1u /* 公告值：版本 1 | 執行個體 0 << 8 */
#define FM_RMTFS_SECTOR 512u
#define FM_RMTFS_FILES 4u
#define FM_RMTFS_OPEN 1u
#define FM_RMTFS_CLOSE 2u
#define FM_RMTFS_RW_IOVEC 3u
#define FM_RMTFS_ALLOC_BUFF 4u
#define FM_RMTFS_GET_DEV_ERROR 5u
#define FM_QMI_RESULT_FAILURE 1u
#define FM_QMI_ERR_MALFORMED 1u
#define FM_QMI_ERR_INTERNAL 3u

#define FM_QRTR_HEADER 32u
#define FM_QRTR_CTRL_PACKET (FM_QRTR_HEADER + 20u)
#define FM_QRTR_PORT_CTRL 0xfffffffeu
#define FM_QRTR_NEW_SERVER 4u
#define FM_QRTR_DEL_SERVER 5u
#define FM_QRTR_DEL_CLIENT 6u
#define FM_QRTR_RESUME_TX 7u
#define FM_QRTR_NEW_LOOKUP 10u
#define FM_QRTR_DATA 1u

#define FM_TQFTP_SERVICE 4096u
#define FM_TQFTP_INSTANCE 1u
#define FM_TFTP_RRQ 1u
#define FM_TFTP_WRQ 2u
#define FM_TFTP_DATA 3u
#define FM_TFTP_ACK 4u
#define FM_TFTP_ERROR 5u
#define FM_TFTP_OACK 6u
#define FM_TFTP_ENOENT 1u
#define FM_TFTP_EACCESS 2u
#define FM_TFTP_ENOSPC 3u
#define FM_TFTP_EBADOP 4u
#define FM_TFTP_END_OF_TRANSFER 9u
#define FM_TFTP_MAX_BLOCK 2048u
#define FM_TFTP_MAX_WINDOW 4u

static inline uint32_t fm_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static inline void fm_put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void fm_put32(uint8_t *p, uint32_t v) { fm_put16(p, v); fm_put16(p + 2, v >> 16); }
static inline void fm_put64(uint8_t *p, uint64_t v) { fm_put32(p, (uint32_t)v); fm_put32(p + 4, (uint32_t)(v >> 32)); }

/* ---- MP_ASSIGN ---- */
/* 參數頁（page 是 4096 位元組、實體位址 page_pa）。args 收 x2..x5。失敗（數量不合）回 -1。 */
static inline int fm_assign_page(uint8_t *page, uint64_t page_pa, uint64_t mem_pa, uint64_t mem_size, const uint32_t *sources,
    unsigned source_count, const uint32_t *vmids, const uint32_t *perms, unsigned destination_count, uint64_t args[4])
{
    if (!page || !sources || !vmids || !perms || !args || !source_count || source_count > 3 || !destination_count ||
        destination_count > 3 || !mem_size || (page_pa & 0xfffu))
        return -1;
    for (unsigned i = 0; i < FM_ASSIGN_PAGE; ++i) page[i] = 0;
    fm_put64(page, mem_pa);
    fm_put64(page + 8, mem_size);
    for (unsigned i = 0; i < source_count; ++i) fm_put32(page + 64 + 4 * i, sources[i]);
    for (unsigned i = 0; i < destination_count; ++i) {
        uint8_t *d = page + 128 + 24 * i; /* vmid、權限、u64 context、u32 context 大小、保留 */
        fm_put32(d, vmids[i]);
        fm_put32(d + 4, perms[i]);
    }
    fm_put64(page + 240, (uint64_t)source_count * 4u);       /* 第 4 個參數：來源清單大小 */
    fm_put64(page + 248, page_pa + 128);                      /* 第 5 個：目的地清單 */
    fm_put64(page + 256, (uint64_t)destination_count * 24u); /* 第 6 個：其大小 */
    fm_put64(page + 264, 0);                                  /* 第 7 個：保留 */
    args[0] = page_pa;      /* x2：記憶體清單 */
    args[1] = 16;           /* x3：其大小 */
    args[2] = page_pa + 64; /* x4：來源清單 */
    args[3] = page_pa + 240; /* x5：其餘參數 */
    return 0;
}

/* ---- QMI ---- */
/* TLV 的值（長度在 *size），找不到回 NULL。m 是整份 QMI 訊息（7 位元組標頭加 TLV）。 */
static inline const uint8_t *fm_qmi_tlv(const uint8_t *m, uint32_t length, uint8_t type, uint32_t *size)
{
    if (length < 7) return NULL;
    uint32_t payload = (uint32_t)m[5] | (uint32_t)m[6] << 8;
    if (payload > length - 7) payload = length - 7;
    for (uint32_t at = 7; at + 3 <= 7 + payload;) {
        const uint32_t n = (uint32_t)m[at + 1] | (uint32_t)m[at + 2] << 8;
        if (at + 3 + n > 7 + payload) return NULL;
        if (m[at] == type) {
            if (size) *size = n;
            return m + at + 3;
        }
        at += 3 + n;
    }
    return NULL;
}

/* ---- RMTFS ---- */
typedef struct {
    const char *path, *partition;
    uint8_t *data; /* 整份分區的副本；NULL 表示沒讀到 */
    uint64_t size;
    uint8_t open;
} FmRmtfsFile;
typedef struct {
    FmRmtfsFile file[FM_RMTFS_FILES];
    uint8_t *window;  /* 共用緩衝區（核心：WCOMB 對映） */
    uint64_t pa;      /* 其實體位址，告訴 modem */
    uint32_t size;
    uint8_t shared;   /* TrustZone 已讓 modem 進來 */
    /* 計數 */
    uint32_t requests, refused, opens, reads, writes, allocs;
    uint64_t sectors_read, sectors_written;
    uint16_t last_message, last_error;
} FmRmtfs;

static inline void fm_rmtfs_init(FmRmtfs *r)
{
    static const char *const paths[FM_RMTFS_FILES] = {"/boot/modem_fs1", "/boot/modem_fs2", "/boot/modem_fsc", "/boot/modem_fsg"};
    static const char *const parts[FM_RMTFS_FILES] = {"modemst1", "modemst2", "fsc", "fsg"};
    uint8_t *raw = (uint8_t *)r;
    for (size_t i = 0; i < sizeof(*r); ++i) raw[i] = 0;
    for (unsigned i = 0; i < FM_RMTFS_FILES; ++i) {
        r->file[i].path = paths[i];
        r->file[i].partition = parts[i];
    }
}

static inline unsigned fm_rmtfs_caller(const uint8_t *m, uint32_t length)
{
    uint32_t size = 0;
    const uint8_t *v = fm_qmi_tlv(m, length, 1, &size);
    if (!v || size != 4) return FM_RMTFS_FILES;
    const uint32_t id = fm_le32(v);
    return id >= 1 && id <= FM_RMTFS_FILES ? id - 1 : FM_RMTFS_FILES;
}

/* 一則 modem 的請求（整份 QMI 訊息）→ 回應寫進 out，回傳長度；不是請求或 out 太小回 0。 */
static inline uint32_t fm_rmtfs_answer(FmRmtfs *r, const uint8_t *m, uint32_t length, uint8_t *out, uint32_t capacity)
{
    if (!r || !m || length < 7 || m[0] != 0 || !out || capacity < 32) return 0;
    const uint16_t message = (uint16_t)(m[3] | m[4] << 8);
    uint16_t result = 0, error = 0;
    uint32_t extra = 0, size = 0;
    uint8_t *tlv = out + 7 + 7; /* 結果 TLV（7 位元組）在前 */
    unsigned f = FM_RMTFS_FILES;
    r->requests++;
    r->last_message = message;
    switch (message) {
    case FM_RMTFS_OPEN: {
        const uint8_t *path = fm_qmi_tlv(m, length, 1, &size);
        for (unsigned i = 0; path && i < FM_RMTFS_FILES; ++i) {
            const char *p = r->file[i].path;
            uint32_t n = 0;
            while (p[n]) ++n;
            uint32_t k = 0;
            if (n == size) while (k < n && (uint8_t)p[k] == path[k]) ++k;
            if (n == size && k == n) f = i;
        }
        if (f == FM_RMTFS_FILES || !r->file[f].data) { result = FM_QMI_RESULT_FAILURE; error = FM_QMI_ERR_INTERNAL; break; }
        r->file[f].open = 1;
        r->opens++;
        tlv[0] = 0x10; fm_put16(tlv + 1, 4); fm_put32(tlv + 3, f + 1);
        extra = 7;
        break;
    }
    case FM_RMTFS_CLOSE:
        f = fm_rmtfs_caller(m, length);
        if (f < FM_RMTFS_FILES) r->file[f].open = 0;
        else { result = FM_QMI_RESULT_FAILURE; error = FM_QMI_ERR_MALFORMED; }
        break;
    case FM_RMTFS_ALLOC_BUFF: {
        const uint8_t *v = fm_qmi_tlv(m, length, 2, &size);
        if (!v || size != 4 || fm_le32(v) > r->size || !r->window || !r->shared) {
            result = FM_QMI_RESULT_FAILURE; error = FM_QMI_ERR_INTERNAL; break;
        }
        r->allocs++;
        tlv[0] = 0x10; fm_put16(tlv + 1, 8); fm_put64(tlv + 3, r->pa);
        extra = 11;
        break;
    }
    case FM_RMTFS_RW_IOVEC: {
        uint32_t dsize = 0, esize = 0;
        const uint8_t *direction = fm_qmi_tlv(m, length, 2, &dsize), *entries = fm_qmi_tlv(m, length, 3, &esize);
        f = fm_rmtfs_caller(m, length);
        if (f == FM_RMTFS_FILES || !r->file[f].open || !r->file[f].data || !direction || dsize != 1 || !entries || esize < 1 ||
            esize != 1u + 12u * entries[0] || !r->window || !r->shared) {
            result = FM_QMI_RESULT_FAILURE; error = FM_QMI_ERR_MALFORMED; break;
        }
        const int write = direction[0] != 0;
        const unsigned count = entries[0];
        /* 先全部檢查邊界，再搬資料：一個請求不會只做一半 */
        for (unsigned e = 0; e < count; ++e) {
            const uint64_t sector = fm_le32(entries + 1 + 12 * e), physical = fm_le32(entries + 5 + 12 * e),
                sectors = fm_le32(entries + 9 + 12 * e), bytes = sectors * FM_RMTFS_SECTOR;
            if (physical < r->pa || physical - r->pa > r->size || bytes > r->size - (physical - r->pa) ||
                sector * FM_RMTFS_SECTOR > r->file[f].size || bytes > r->file[f].size - sector * FM_RMTFS_SECTOR) {
                result = FM_QMI_RESULT_FAILURE; error = FM_QMI_ERR_MALFORMED; break;
            }
        }
        for (unsigned e = 0; !result && e < count; ++e) {
            const uint64_t sector = fm_le32(entries + 1 + 12 * e), physical = fm_le32(entries + 5 + 12 * e),
                sectors = fm_le32(entries + 9 + 12 * e), bytes = sectors * FM_RMTFS_SECTOR;
            uint8_t *file = r->file[f].data + sector * FM_RMTFS_SECTOR, *window = r->window + (physical - r->pa);
            if (write) { for (uint64_t i = 0; i < bytes; ++i) file[i] = window[i]; r->sectors_written += sectors; }
            else { for (uint64_t i = 0; i < bytes; ++i) window[i] = file[i]; r->sectors_read += sectors; }
        }
        if (!result) { if (write) r->writes++; else r->reads++; }
        break;
    }
    case FM_RMTFS_GET_DEV_ERROR:
        tlv[0] = 0x10; fm_put16(tlv + 1, 1); tlv[3] = 0;
        extra = 4;
        break;
    default:
        result = FM_QMI_RESULT_FAILURE; error = FM_QMI_ERR_MALFORMED;
        break;
    }
    if (result) { r->refused++; r->last_error = error; extra = 0; }
    out[0] = 2; /* 回應 */
    out[1] = m[1]; out[2] = m[2]; /* 交易號 */
    out[3] = m[3]; out[4] = m[4]; /* 訊息號 */
    fm_put16(out + 5, 7 + extra);
    out[7] = 0x02; fm_put16(out + 8, 4); fm_put16(out + 10, result); fm_put16(out + 12, error);
    return 14 + extra;
}

/* ---- QRTR 控制封包 ---- */
/* 從本地名稱服務（local_node:CTRL）寄到 node:port 的控制封包；回傳長度（FM_QRTR_CTRL_PACKET）。 */
static inline uint32_t fm_qrtr_ctrl(uint8_t out[FM_QRTR_CTRL_PACKET], uint32_t type, uint32_t local_node, uint32_t node, uint32_t port,
    const uint32_t words[4])
{
    fm_put32(out, 1);
    fm_put32(out + 4, type);
    fm_put32(out + 8, local_node);
    fm_put32(out + 12, FM_QRTR_PORT_CTRL);
    fm_put32(out + 16, 0);
    fm_put32(out + 20, 20);
    fm_put32(out + 24, node);
    fm_put32(out + 28, port);
    fm_put32(out + 32, type);
    for (unsigned i = 0; i < 4; ++i) fm_put32(out + 36 + 4 * i, words ? words[i] : 0);
    return FM_QRTR_CTRL_PACKET;
}
/* 查詢 (service, instance) 是否對上本地服務：版本（低 8 位）與執行個體號（高 24 位）各自 0 皆表不限。 */
static inline int fm_lookup_matches(uint32_t service, uint32_t instance, uint32_t ours_service, uint32_t ours_instance)
{
    if (service != ours_service) return 0;
    const uint32_t want_ver = instance & 0xffu, ours_ver = ours_instance & 0xffu;
    const uint32_t want_inst = instance >> 8, ours_inst = ours_instance >> 8;
    return (!want_ver || want_ver == ours_ver) && (!want_inst || want_inst == ours_inst);
}
/* 一個 NEW_LOOKUP 封包：取出查詢者與 (service, instance)；不是就回 -1。 */
static inline int fm_qrtr_lookup(const uint8_t *packet, uint32_t length, uint32_t *node, uint32_t *port, uint32_t *service,
    uint32_t *instance)
{
    if (length < FM_QRTR_HEADER + 12 || fm_le32(packet) != 1 || fm_le32(packet + 4) != FM_QRTR_NEW_LOOKUP) return -1;
    const uint32_t size = fm_le32(packet + 20);
    if (size < 12 || size > length - FM_QRTR_HEADER) return -1;
    *node = fm_le32(packet + 8);
    *port = fm_le32(packet + 12);
    *service = fm_le32(packet + FM_QRTR_HEADER + 4);
    *instance = fm_le32(packet + FM_QRTR_HEADER + 8);
    return 0;
}

/* 封裝一包 QRTR v1 DATA 封包（不受 QcomQrtr.c 512 位元組上限限制，供 TFTP 大區塊使用）。容不下回 0。 */
static inline uint32_t fm_qrtr_data(uint8_t *packet, uint32_t capacity, uint32_t local_node, uint32_t from_port, uint32_t dst_node,
    uint32_t dst_port, const uint8_t *payload, uint32_t length)
{
    if (!packet || (!payload && length)) return 0;
    const uint32_t padded = (length + 3u) & ~3u;
    if (padded < length || padded > capacity || FM_QRTR_HEADER > capacity - padded) return 0;
    fm_put32(packet, 1u);
    fm_put32(packet + 4, FM_QRTR_DATA);
    fm_put32(packet + 8, local_node);
    fm_put32(packet + 12, from_port);
    fm_put32(packet + 16, 0u);
    fm_put32(packet + 20, length);
    fm_put32(packet + 24, dst_node);
    fm_put32(packet + 28, dst_port);
    for (uint32_t idx = 0; idx < padded; ++idx) packet[FM_QRTR_HEADER + idx] = idx < length ? payload[idx] : 0u;
    return FM_QRTR_HEADER + padded;
}

/* QRTR v1 RESUME_TX（流控恢復，對照 Linux net/qrtr/af_qrtr.c qrtr_send_resume_tx 與 HakiOS hakios.mpss.c）：
 * 標頭 from=(local_node, dst_port)、dst=(src_node, src_port)，負載 {cmd=7, client.node=local_node, client.port=dst_port, 0, 0}。 */
static inline uint32_t fm_qrtr_resume_tx(uint8_t out[FM_QRTR_CTRL_PACKET], uint32_t local_node, uint32_t dst_port,
    uint32_t src_node, uint32_t src_port)
{
    fm_put32(out, 1u);
    fm_put32(out + 4, FM_QRTR_RESUME_TX);
    fm_put32(out + 8, local_node);
    fm_put32(out + 12, dst_port);
    fm_put32(out + 16, 0u);
    fm_put32(out + 20, 20u);
    fm_put32(out + 24, src_node);
    fm_put32(out + 28, src_port);
    fm_put32(out + 32, FM_QRTR_RESUME_TX);
    fm_put32(out + 36, local_node);
    fm_put32(out + 40, dst_port);
    fm_put32(out + 44, 0u);
    fm_put32(out + 48, 0u);
    return FM_QRTR_CTRL_PACKET;
}

/* ---- TFTP over QRTR（tqftpserv，服務 4096：對照 linux-msm/tqftpserv 與 HakiOS hakios.mpss.c，純邏輯重寫） ---- */
typedef struct {
    uint16_t opcode;
    uint8_t has_options;
    uint8_t has_blksize, has_timeoutms, has_timeout, has_tsize, has_wsize, has_rsize, has_seek, has_append, has_unlink;
    uint8_t unknown_opts;
    uint32_t blksize, timeoutms, timeout, tsize, wsize, rsize, seek;
    char path[128];
    char unknown_name[32];
} FmTftpRequest;

static inline char fm_ascii_lower(char ch)
{
    return (ch >= 'A' && ch <= 'Z') ? (char)(ch + ('a' - 'A')) : ch;
}

static inline int fm_ascii_ieq(const char *left, const char *right)
{
    size_t idx = 0;
    while (left[idx] && right[idx] && fm_ascii_lower(left[idx]) == fm_ascii_lower(right[idx])) ++idx;
    return left[idx] == 0 && right[idx] == 0;
}

static inline int fm_parse_u32(const char *text, uint32_t *out)
{
    if (!text || !text[0]) return -1;
    uint64_t acc = 0;
    for (size_t idx = 0; text[idx]; ++idx) {
        if (text[idx] < '0' || text[idx] > '9') return -1;
        acc = acc * 10u + (uint64_t)(text[idx] - '0');
        if (acc > 0xffffffffull) return -1;
    }
    *out = (uint32_t)acc;
    return 0;
}

/* 解析一則 TFTP RRQ／WRQ（RFC 1350／2347 與 Qualcomm wsize／rsize／seek）。非法或路徑過長回 -1。 */
static inline int fm_tftp_parse_request(const uint8_t *data, uint32_t length, FmTftpRequest *req)
{
    if (!data || !req || length < 4) return -1;
    uint8_t *raw = (uint8_t *)req;
    for (size_t idx = 0; idx < sizeof(*req); ++idx) raw[idx] = 0;
    const uint16_t opcode = (uint16_t)((uint16_t)data[0] << 8 | data[1]);
    if (opcode != FM_TFTP_RRQ && opcode != FM_TFTP_WRQ) return -1;
    req->opcode = opcode;
    req->blksize = 512u;
    req->timeoutms = 1000u;
    req->timeout = 1u;
    req->wsize = 1u;
    uint32_t pos = 2u, path_start = 2u;
    while (pos < length && data[pos] != 0u) ++pos;
    if (pos >= length || pos == path_start || pos - path_start >= sizeof(req->path)) return -1;
    for (uint32_t idx = 0; idx < pos - path_start; ++idx) {
        const uint8_t ch = data[path_start + idx];
        if (ch < 0x20u || ch >= 0x7fu) return -1;
        req->path[idx] = (char)ch;
    }
    req->path[pos - path_start] = 0;
    ++pos;
    /* mode 字串（octet 等） */
    const uint32_t mode_start = pos;
    while (pos < length && data[pos] != 0u) ++pos;
    if (pos >= length || pos == mode_start) return -1;
    ++pos;
    while (pos < length) {
        const uint32_t opt_start = pos;
        while (pos < length && data[pos] != 0u) ++pos;
        if (pos >= length) break;
        const char *opt = (const char *)(data + opt_start);
        ++pos;
        const uint32_t val_start = pos;
        while (pos < length && data[pos] != 0u) ++pos;
        if (pos >= length) break;
        const char *val = (const char *)(data + val_start);
        ++pos;
        req->has_options = 1u;
        uint32_t parsed = 0;
        if (fm_ascii_ieq(opt, "blksize")) {
            if (fm_parse_u32(val, &parsed)) return -1;
            req->has_blksize = 1u;
            req->blksize = parsed;
        } else if (fm_ascii_ieq(opt, "timeoutms")) {
            if (fm_parse_u32(val, &parsed)) return -1;
            req->has_timeoutms = 1u;
            req->timeoutms = parsed ? parsed : 1000u;
        } else if (fm_ascii_ieq(opt, "timeout")) {
            if (fm_parse_u32(val, &parsed)) return -1;
            req->has_timeout = 1u;
            req->timeout = parsed ? parsed : 1u;
        } else if (fm_ascii_ieq(opt, "tsize")) {
            if (fm_parse_u32(val, &parsed)) return -1;
            req->has_tsize = 1u;
            req->tsize = parsed;
        } else if (fm_ascii_ieq(opt, "wsize")) {
            if (fm_parse_u32(val, &parsed)) return -1;
            req->has_wsize = 1u;
            req->wsize = parsed;
        } else if (fm_ascii_ieq(opt, "rsize")) {
            if (fm_parse_u32(val, &parsed)) return -1;
            req->has_rsize = 1u;
            req->rsize = parsed;
        } else if (fm_ascii_ieq(opt, "seek")) {
            if (fm_parse_u32(val, &parsed)) return -1;
            req->has_seek = 1u;
            req->seek = parsed;
        } else if (fm_ascii_ieq(opt, "append")) {
            req->has_append = 1u;
        } else if (fm_ascii_ieq(opt, "unlink")) {
            req->has_unlink = 1u;
        } else {
            if (!req->unknown_opts) {
                size_t copy_idx = 0;
                while (opt[copy_idx] && copy_idx + 1u < sizeof(req->unknown_name)) {
                    const uint8_t ch = (uint8_t)opt[copy_idx];
                    req->unknown_name[copy_idx] = (ch >= 0x20u && ch < 0x7fu) ? (char)ch : '?';
                    ++copy_idx;
                }
                req->unknown_name[copy_idx] = 0;
            }
            if (req->unknown_opts < 255u) req->unknown_opts++;
        }
    }
    if (req->blksize < 8u) req->blksize = 8u;
    if (req->blksize > FM_TFTP_MAX_BLOCK) req->blksize = FM_TFTP_MAX_BLOCK;
    if (req->wsize < 1u) req->wsize = 1u;
    if (req->wsize > FM_TFTP_MAX_WINDOW) req->wsize = FM_TFTP_MAX_WINDOW;
    return 0;
}

/* 將 modem TFTP 唯讀路徑轉成 modem_a FAT 卷上的相對路徑 "image/..."；拒絕任何目錄穿越（..）或非唯讀前綴。 */
static inline int fm_tftp_fw_relpath(const char *path, char *out, size_t capacity)
{
    static const char *const prefixes[] = {
        "/readonly/firmware/image/",
        "/readonly/vendor/firmware_mnt/image/",
        "/readonly/vendor/firmware/image/",
        "/readonly/vendor/firmware/",
        "/readonly/firmware/",
    };
    if (!path || !out || capacity < 8u) return -1;
    const char *suffix = NULL;
    for (unsigned idx = 0; idx < sizeof(prefixes) / sizeof(prefixes[0]); ++idx) {
        size_t plen = 0;
        while (prefixes[idx][plen]) ++plen;
        size_t matched = 0;
        while (matched < plen && path[matched] == prefixes[idx][matched]) ++matched;
        if (matched == plen) { suffix = path + plen; break; }
    }
    if (!suffix || !suffix[0]) return -1;
    for (size_t idx = 0; suffix[idx]; ++idx) {
        if (suffix[idx] == '\\' || (uint8_t)suffix[idx] < 0x20u || (uint8_t)suffix[idx] >= 0x7fu) return -1;
        if (suffix[idx] == '.' && suffix[idx + 1] == '.') return -1;
    }
    static const char kHead[] = "image/";
    size_t pos = 0;
    for (size_t idx = 0; kHead[idx]; ++idx) {
        if (pos + 1 >= capacity) return -1;
        out[pos++] = kHead[idx];
    }
    for (size_t idx = 0; suffix[idx]; ++idx) {
        if (pos + 1 >= capacity) return -1;
        out[pos++] = suffix[idx];
    }
    out[pos] = 0;
    return 0;
}

static inline uint32_t fm_tftp_append_str(uint8_t *out, uint32_t pos, uint32_t capacity, const char *text)
{
    size_t len = 0;
    while (text[len]) ++len;
    if (pos >= capacity || len >= capacity - pos) return 0;
    for (size_t idx = 0; idx < len; ++idx) out[pos++] = (uint8_t)text[idx];
    out[pos++] = 0u;
    return pos;
}

static inline uint32_t fm_tftp_append_u64(uint8_t *out, uint32_t pos, uint32_t capacity, uint64_t value)
{
    char digits[24];
    unsigned count = 0;
    do { digits[count++] = (char)('0' + (value % 10u)); value /= 10u; } while (value && count < sizeof(digits));
    if (pos >= capacity || count >= capacity - pos) return 0;
    while (count) out[pos++] = (uint8_t)digits[--count];
    out[pos++] = 0u;
    return pos;
}

/* 依 RFC 2347 與 Qualcomm tftp_server 規則：只回送 client 請求且伺服器接受的選項（含 seek=0／append／unlink）。 */
static inline uint32_t fm_tftp_build_oack(uint8_t *out, uint32_t capacity, const FmTftpRequest *req, uint64_t tsize_value)
{
    if (!out || !req || capacity < 16u) return 0;
    out[0] = 0u;
    out[1] = FM_TFTP_OACK;
    uint32_t pos = 2u;
    if (req->has_blksize) {
        if (!(pos = fm_tftp_append_str(out, pos, capacity, "blksize"))) return 0;
        if (!(pos = fm_tftp_append_u64(out, pos, capacity, req->blksize))) return 0;
    }
    if (req->has_timeoutms) {
        if (!(pos = fm_tftp_append_str(out, pos, capacity, "timeoutms"))) return 0;
        if (!(pos = fm_tftp_append_u64(out, pos, capacity, req->timeoutms))) return 0;
    }
    if (req->has_timeout) {
        if (!(pos = fm_tftp_append_str(out, pos, capacity, "timeout"))) return 0;
        if (!(pos = fm_tftp_append_u64(out, pos, capacity, req->timeout))) return 0;
    }
    if (req->has_tsize) {
        if (!(pos = fm_tftp_append_str(out, pos, capacity, "tsize"))) return 0;
        if (!(pos = fm_tftp_append_u64(out, pos, capacity, tsize_value))) return 0;
    }
    if (req->has_wsize) {
        if (!(pos = fm_tftp_append_str(out, pos, capacity, "wsize"))) return 0;
        if (!(pos = fm_tftp_append_u64(out, pos, capacity, req->wsize))) return 0;
    }
    if (req->has_rsize) {
        if (!(pos = fm_tftp_append_str(out, pos, capacity, "rsize"))) return 0;
        if (!(pos = fm_tftp_append_u64(out, pos, capacity, req->rsize))) return 0;
    }
    if (req->has_seek) {
        if (!(pos = fm_tftp_append_str(out, pos, capacity, "seek"))) return 0;
        if (!(pos = fm_tftp_append_u64(out, pos, capacity, req->seek))) return 0;
    }
    if (req->has_append) {
        if (!(pos = fm_tftp_append_str(out, pos, capacity, "append"))) return 0;
        if (!(pos = fm_tftp_append_str(out, pos, capacity, "0"))) return 0;
    }
    if (req->has_unlink) {
        if (!(pos = fm_tftp_append_str(out, pos, capacity, "unlink"))) return 0;
        if (!(pos = fm_tftp_append_str(out, pos, capacity, "0"))) return 0;
    }
    return pos > 2u ? pos : 0u;
}

static inline uint32_t fm_tftp_build_error(uint8_t *out, uint32_t capacity, uint16_t code, const char *text)
{
    if (!out || !text || capacity < 6u) return 0;
    out[0] = 0u;
    out[1] = FM_TFTP_ERROR;
    out[2] = (uint8_t)(code >> 8);
    out[3] = (uint8_t)code;
    return fm_tftp_append_str(out, 4u, capacity, text);
}

static inline void fm_tftp_build_ack(uint8_t out[4], uint16_t block)
{
    out[0] = 0u;
    out[1] = FM_TFTP_ACK;
    out[2] = (uint8_t)(block >> 8);
    out[3] = (uint8_t)block;
}

/* 計算 TFTP RRQ 單一區塊應送出的檔案偏移與長度（對照小米 13 原廠 vendor-fuxi tftp_server 0x185b8／0xee0c）：
 * 有效傳輸上限為 min(file_size - seek, rsize ? rsize : inf)。只要區塊起點 <= 上限就送包（包含起點 == 上限時
 * 應送出的 0 位元組 DATA 結尾包），並只在 take_len < blksize 時標記 EOF；超過上限才回 0。 */
static inline int fm_tftp_rrq_block(uint64_t file_size, uint32_t seek, uint32_t rsize, uint32_t blksize, uint32_t block,
    uint64_t *file_offset, uint32_t *take_len, int *is_eof)
{
    if (!blksize || !block || !file_offset || !take_len || !is_eof) return 0;
    const uint64_t avail = (uint64_t)seek < file_size ? file_size - (uint64_t)seek : 0u;
    const uint64_t limit = (rsize != 0u && (uint64_t)rsize < avail) ? (uint64_t)rsize : avail;
    const uint64_t rel = (uint64_t)(block - 1u) * (uint64_t)blksize;
    if (rel > limit) return 0;
    const uint64_t rem = limit - rel;
    const uint32_t take = rem < (uint64_t)blksize ? (uint32_t)rem : blksize;
    *file_offset = (uint64_t)seek + rel;
    *take_len = take;
    *is_eof = take < blksize ? 1 : 0;
    return 1;
}

/* ---- QMI 用戶端探測與資料設定（fuxi,modem-qmi）：DMS／NAS／WMS／UIM／DPM／WDA／WDS／IPA ----
 * 訊息號與 TLV 版面對照 libqmi data/qmi-service-{dms,nas,wms,uim,dpm,wda,wds}.json（LGPL，只取格式事實）、
 * HakiOS hakios.mpss.c（APSL 2.0）與 ios-redfin cell.c（只取流程與 TLV 事實，程式為重寫）。刻意不解碼也不保存
 * IMEI／ICCID／IMSI／PLMN、電信商名稱與簡訊門號／內容；UIM AID 與 WDS IPv4 位址僅於記憶體內供啟動會話與 en1 DHCP 使用，
 * 嚴禁輸出至日誌（紀錄庫公開）。 */
#define FM_QMI_WDS 0x01u
#define FM_QMI_DMS 0x02u
#define FM_QMI_NAS 0x03u
#define FM_QMI_WMS 0x05u
#define FM_QMI_UIM 0x0bu
#define FM_QMI_WDA 0x1au
#define FM_QMI_DPM 0x2fu
#define FM_QMI_IPA 0x31u
#define FM_QMI_TYPE_REQUEST 0u
#define FM_QMI_TYPE_RESPONSE 2u
#define FM_QMI_TYPE_INDICATION 4u
#define FM_DMS_GET_REVISION 0x0023u
#define FM_DMS_GET_OPERATING_MODE 0x002du
#define FM_DMS_SET_OPERATING_MODE 0x002eu
#define FM_NAS_GET_SIGNAL_INFO 0x004fu
#define FM_NAS_GET_SERVING_SYSTEM 0x0024u
#define FM_UIM_GET_CARD_STATUS 0x002fu
#define FM_UIM_CHANGE_PROVISIONING 0x0038u
#define FM_UIM_SWITCH_SLOT 0x0046u
#define FM_DPM_OPEN_PORT 0x0020u
#define FM_WDA_SET_DATA_FORMAT 0x0020u
#define FM_WDS_START_NETWORK 0x0020u
#define FM_WDS_GET_CURRENT_SETTINGS 0x002du
#define FM_WDS_SET_IP_FAMILY 0x004du
#define FM_WDS_BIND_MUX_DATA_PORT 0x00a2u
#define FM_WMS_SET_EVENT_REPORT 0x0001u
#define FM_WMS_EVENT_REPORT_IND 0x0001u
#define FM_WMS_RAW_SEND 0x0020u
#define FM_WMS_RAW_READ 0x0022u
#define FM_WMS_DELETE 0x0024u
#define FM_WMS_GET_ROUTES 0x0027u
#define FM_WMS_LIST_MESSAGES 0x0031u
#define FM_WMS_GET_STORE_MAX_SIZE 0x0033u
#define FM_WMS_STORAGE_UIM 0u
#define FM_WMS_STORAGE_NV 1u
#define FM_WMS_MODE_GW 1u
#define FM_WMS_FORMAT_3GPP 6u
#define FM_DMS_MODE_ONLINE 0u
#define FM_DMS_MODE_LOW_POWER 1u
#define FM_DMS_MODE_OFFLINE 3u
#define FM_QMI_ERR_OP_MODE_UNSUPPORTED 0x34u
#define FM_DATA_EP_EMBEDDED 4u
#define FM_DATA_EP_IFACE 1u
#define FM_DATA_MUX_ID 1u
#define FM_IPA5_EP_TO_MODEM 2u    /* SM8550 IPA v5.5 client 0x22 APPS_WAN_PROD (pipe 2, GSI ch 11), ipam.ko ipa3_ep_mapping[21] */
#define FM_IPA5_EP_Q6_WAN_CONS 22u /* SM8550 IPA v5.5 client 0x35 Q6_WAN_CONS (pipe 22, GSI ch 7), ipam.ko ipa3_ep_mapping[21] */
#define FM_IPA5_EP_FROM_MODEM 24u /* SM8550 IPA v5.5 client 0x23 APPS_WAN_CONS (pipe 24, GSI ch 1), ipam.ko ipa3_ep_mapping[21] */
#define FM_WDA_LINK_RAW_IP 2u
#define FM_WDA_AGGR_QMAP 5u
#define FM_WDA_DL_MAX_DGRAMS 16u
#define FM_WDA_DL_MAX_SIZE 0x3800u
#define FM_WDA_DL_MAX_SIZE_M7 0x1800u /* M7 配合 8 KiB RX 緩衝與 5 KiB IPA 聚合上限 */
#define FM_WDS_SETTINGS_MASK 0x0000a310u /* DNS | IPv4 addr | GW | MTU | IP family */
#define FM_UIM_MAX_SLOTS 4u
#define FM_UIM_MAX_AID 32u

static inline uint16_t fm_le16(const uint8_t *ptr)
{
    return (uint16_t)((uint16_t)ptr[0] | ((uint16_t)ptr[1] << 8));
}

/* 一則請求：標頭（type 0）加可選的一個 1 位元組 TLV（tlv_type 0 表示不帶）。out 太小回 0。 */
static inline uint32_t fm_qmi_request(uint8_t *out, uint32_t capacity, uint16_t transaction, uint16_t message, uint8_t tlv_type,
    uint8_t value)
{
    const uint32_t n = tlv_type ? 11u : 7u;
    if (!out || capacity < n) return 0;
    out[0] = FM_QMI_TYPE_REQUEST;
    fm_put16(out + 1, transaction);
    fm_put16(out + 3, message);
    fm_put16(out + 5, n - 7u);
    if (tlv_type) { out[7] = tlv_type; fm_put16(out + 8, 1); out[10] = value; }
    return n;
}

static inline uint32_t fm_qmi_uim_switch_slot(uint8_t *out, uint32_t capacity, uint16_t transaction, uint8_t logical_slot,
    uint32_t physical_slot)
{
    const uint32_t total = 7u + 4u + 7u;
    if (!out || capacity < total || !logical_slot || !physical_slot) return 0;
    out[0] = FM_QMI_TYPE_REQUEST;
    fm_put16(out + 1, transaction);
    fm_put16(out + 3, FM_UIM_SWITCH_SLOT);
    fm_put16(out + 5, total - 7u);
    out[7] = 0x01u; fm_put16(out + 8, 1u); out[10] = logical_slot;
    out[11] = 0x02u; fm_put16(out + 12, 4u); fm_put32(out + 14, physical_slot);
    return total;
}

static inline uint32_t fm_qmi_uim_change_provisioning(uint8_t *out, uint32_t capacity, uint16_t transaction, uint8_t session_type,
    uint8_t activate, uint8_t slot, const uint8_t *aid, uint8_t aid_len)
{
    if (!out || !slot || !aid || !aid_len || aid_len > FM_UIM_MAX_AID) return 0;
    const uint32_t total = 7u + 5u + 5u + (uint32_t)aid_len;
    if (capacity < total) return 0;
    out[0] = FM_QMI_TYPE_REQUEST;
    fm_put16(out + 1, transaction);
    fm_put16(out + 3, FM_UIM_CHANGE_PROVISIONING);
    fm_put16(out + 5, total - 7u);
    out[7] = 0x01u; fm_put16(out + 8, 2u); out[10] = session_type; out[11] = activate;
    out[12] = 0x10u; fm_put16(out + 13, (uint32_t)(2u + aid_len)); out[15] = slot; out[16] = aid_len;
    for (uint8_t idx = 0; idx < aid_len; ++idx) out[17u + idx] = aid[idx];
    return total;
}

static inline uint32_t fm_qmi_dpm_open_port(uint8_t *out, uint32_t capacity, uint16_t transaction, uint32_t ep_type, uint32_t iface,
    uint32_t to_modem, uint32_t from_modem)
{
    const uint32_t total = 7u + 3u + 1u + 16u;
    if (!out || capacity < total) return 0;
    out[0] = FM_QMI_TYPE_REQUEST;
    fm_put16(out + 1, transaction);
    fm_put16(out + 3, FM_DPM_OPEN_PORT);
    fm_put16(out + 5, total - 7u);
    out[7] = 0x11u; fm_put16(out + 8, 17u); out[10] = 1u;
    fm_put32(out + 11, ep_type);
    fm_put32(out + 15, iface);
    fm_put32(out + 19, to_modem);
    fm_put32(out + 23, from_modem);
    return total;
}

static inline uint32_t fm_qmi_wda_set_data_format(uint8_t *out, uint32_t capacity, uint16_t transaction, uint32_t ep_type,
    uint32_t iface, uint32_t dl_max_dgrams, uint32_t dl_max_size)
{
    const uint32_t total = 7u + 7u * 5u + 11u;
    if (!out || capacity < total) return 0;
    out[0] = FM_QMI_TYPE_REQUEST;
    fm_put16(out + 1, transaction);
    fm_put16(out + 3, FM_WDA_SET_DATA_FORMAT);
    fm_put16(out + 5, total - 7u);
    out[7] = 0x11u; fm_put16(out + 8, 4u); fm_put32(out + 10, FM_WDA_LINK_RAW_IP);
    out[14] = 0x12u; fm_put16(out + 15, 4u); fm_put32(out + 17, FM_WDA_AGGR_QMAP);
    out[21] = 0x13u; fm_put16(out + 22, 4u); fm_put32(out + 24, FM_WDA_AGGR_QMAP);
    out[28] = 0x15u; fm_put16(out + 29, 4u); fm_put32(out + 31, dl_max_dgrams);
    out[35] = 0x16u; fm_put16(out + 36, 4u); fm_put32(out + 38, dl_max_size);
    out[42] = 0x17u; fm_put16(out + 43, 8u); fm_put32(out + 45, ep_type); fm_put32(out + 49, iface);
    return total;
}

static inline uint32_t fm_qmi_wds_bind_mux(uint8_t *out, uint32_t capacity, uint16_t transaction, uint32_t ep_type, uint32_t iface,
    uint8_t mux_id)
{
    const uint32_t total = 7u + 11u + 4u;
    if (!out || capacity < total || !mux_id) return 0;
    out[0] = FM_QMI_TYPE_REQUEST;
    fm_put16(out + 1, transaction);
    fm_put16(out + 3, FM_WDS_BIND_MUX_DATA_PORT);
    fm_put16(out + 5, total - 7u);
    out[7] = 0x10u; fm_put16(out + 8, 8u); fm_put32(out + 10, ep_type); fm_put32(out + 14, iface);
    out[18] = 0x11u; fm_put16(out + 19, 1u); out[21] = mux_id;
    return total;
}

static inline uint32_t fm_qmi_wds_set_ip_family(uint8_t *out, uint32_t capacity, uint16_t transaction, uint8_t ip_family)
{
    return fm_qmi_request(out, capacity, transaction, FM_WDS_SET_IP_FAMILY, 0x01u, ip_family);
}

static inline uint32_t fm_qmi_wds_start_network(uint8_t *out, uint32_t capacity, uint16_t transaction, uint8_t ip_family)
{
    return fm_qmi_request(out, capacity, transaction, FM_WDS_START_NETWORK, 0x19u, ip_family);
}

static inline uint32_t fm_qmi_wds_get_current_settings(uint8_t *out, uint32_t capacity, uint16_t transaction, uint32_t mask)
{
    const uint32_t total = 7u + 7u;
    if (!out || capacity < total) return 0;
    out[0] = FM_QMI_TYPE_REQUEST;
    fm_put16(out + 1, transaction);
    fm_put16(out + 3, FM_WDS_GET_CURRENT_SETTINGS);
    fm_put16(out + 5, total - 7u);
    out[7] = 0x10u; fm_put16(out + 8, 4u); fm_put32(out + 10, mask);
    return total;
}

/* ---- QMI WMS (0x05) 請求編碼（對照 libqmi qmi-service-wms.json 與 ios-redfin cell.c） ---- */
static inline uint32_t fm_qmi_wms_set_event_report(uint8_t *out, uint32_t capacity, uint16_t transaction, uint8_t report_mt)
{
    return fm_qmi_request(out, capacity, transaction, FM_WMS_SET_EVENT_REPORT, 0x10u, report_mt ? 1u : 0u);
}

static inline uint32_t fm_qmi_wms_raw_send(uint8_t *out, uint32_t capacity, uint16_t transaction, uint8_t format,
    const uint8_t *tpdu, uint16_t tpdu_len)
{
    if (!out || !tpdu || !tpdu_len || tpdu_len > 255u) return 0u;
    const uint32_t tlv_len = 3u + (uint32_t)tpdu_len;
    const uint32_t total = 7u + 3u + tlv_len;
    if (capacity < total) return 0u;
    out[0] = FM_QMI_TYPE_REQUEST;
    fm_put16(out + 1, transaction);
    fm_put16(out + 3, FM_WMS_RAW_SEND);
    fm_put16(out + 5, (uint16_t)(total - 7u));
    out[7] = 0x01u;
    fm_put16(out + 8, (uint16_t)tlv_len);
    out[10] = format;
    fm_put16(out + 11, tpdu_len);
    for (uint32_t idx = 0u; idx < tpdu_len; ++idx) out[13u + idx] = tpdu[idx];
    return total;
}

static inline uint32_t fm_qmi_wms_raw_read(uint8_t *out, uint32_t capacity, uint16_t transaction, uint8_t storage,
    uint32_t index, uint8_t mode)
{
    const uint32_t total = 7u + 8u + 4u;
    if (!out || capacity < total) return 0u;
    out[0] = FM_QMI_TYPE_REQUEST;
    fm_put16(out + 1, transaction);
    fm_put16(out + 3, FM_WMS_RAW_READ);
    fm_put16(out + 5, (uint16_t)(total - 7u));
    out[7] = 0x01u; fm_put16(out + 8, 5u); out[10] = storage; fm_put32(out + 11, index);
    out[15] = 0x10u; fm_put16(out + 16, 1u); out[18] = mode;
    return total;
}

static inline uint32_t fm_qmi_wms_delete(uint8_t *out, uint32_t capacity, uint16_t transaction, uint8_t storage,
    uint32_t index, uint8_t mode)
{
    const uint32_t total = 7u + 4u + 7u + 4u;
    if (!out || capacity < total) return 0u;
    out[0] = FM_QMI_TYPE_REQUEST;
    fm_put16(out + 1, transaction);
    fm_put16(out + 3, FM_WMS_DELETE);
    fm_put16(out + 5, (uint16_t)(total - 7u));
    out[7] = 0x01u; fm_put16(out + 8, 1u); out[10] = storage;
    out[11] = 0x10u; fm_put16(out + 12, 4u); fm_put32(out + 14, index);
    out[18] = 0x12u; fm_put16(out + 19, 1u); out[21] = mode;
    return total;
}

static inline uint32_t fm_qmi_wms_get_routes(uint8_t *out, uint32_t capacity, uint16_t transaction)
{
    return fm_qmi_request(out, capacity, transaction, FM_WMS_GET_ROUTES, 0u, 0u);
}

static inline uint32_t fm_qmi_wms_list_messages(uint8_t *out, uint32_t capacity, uint16_t transaction, uint8_t storage,
    uint8_t mode)
{
    const uint32_t total = 7u + 4u + 4u;
    if (!out || capacity < total) return 0u;
    out[0] = FM_QMI_TYPE_REQUEST;
    fm_put16(out + 1, transaction);
    fm_put16(out + 3, FM_WMS_LIST_MESSAGES);
    fm_put16(out + 5, (uint16_t)(total - 7u));
    out[7] = 0x01u; fm_put16(out + 8, 1u); out[10] = storage;
    out[11] = 0x11u; fm_put16(out + 12, 1u); out[14] = mode;
    return total;
}

static inline uint32_t fm_qmi_wms_get_store_max_size(uint8_t *out, uint32_t capacity, uint16_t transaction, uint8_t storage,
    uint8_t mode)
{
    const uint32_t total = 7u + 4u + 4u;
    if (!out || capacity < total) return 0u;
    out[0] = FM_QMI_TYPE_REQUEST;
    fm_put16(out + 1, transaction);
    fm_put16(out + 3, FM_WMS_GET_STORE_MAX_SIZE);
    fm_put16(out + 5, (uint16_t)(total - 7u));
    out[7] = 0x01u; fm_put16(out + 8, 1u); out[10] = storage;
    out[11] = 0x10u; fm_put16(out + 12, 1u); out[14] = mode;
    return total;
}

typedef struct {
    uint8_t card_state, upin_state, card_error, apps;
    uint8_t app_type, app_state, perso_state, pin1_state, pin1_retries, puk1_retries;
    uint8_t has_gw_app, gw_app_type, gw_app_state, gw_aid_len;
    uint8_t gw_aid[FM_UIM_MAX_AID];
} FmUimSlot;

typedef struct {
    uint16_t transaction, message, result, error;
    uint8_t has_result, is_indication;
    /* DMS */
    uint8_t has_mode, mode, has_offline_reason, has_hw_restricted, hw_restricted;
    uint16_t offline_reason;
    char revision[64];
    /* UIM：向下相容保留 slot 0 欄位，另存 primary_gw 與全部最多 4 槽 */
    uint8_t has_card, slots, card_state, upin_state, card_error, apps, app_type, app_state, perso_state, pin1_state, pin1_retries,
        puk1_retries;
    uint16_t primary_gw;
    uint8_t slot_count;
    FmUimSlot slot[FM_UIM_MAX_SLOTS];
    /* NAS 服務系統 */
    uint8_t has_serving, reg_state, cs_attach, ps_attach, network, radio_count, radio[8];
    /* NAS 訊號（GetSignalInfo）：各制式的 TLV 有沒有出現 */
    uint8_t has_gsm, has_wcdma, has_lte, has_nr;
    int8_t gsm_rssi, wcdma_rssi, lte_rssi, lte_rsrq;
    int16_t wcdma_ecio, lte_rsrp, lte_snr, nr_rsrp, nr_snr;
    /* WDA Set Data Format */
    uint8_t has_wda_format;
    uint32_t wda_link_layer, wda_ul_aggr, wda_dl_aggr, wda_dl_max_dgrams, wda_dl_max_size;
    /* WDS Start Network / Current Settings（IPv4 數值僅存記憶體供 en1 DHCP，嚴禁輸出日誌） */
    uint8_t has_wds_handle, has_call_end_reason, has_verbose_reason;
    uint32_t wds_handle;
    uint16_t call_end_reason, verbose_reason_type, verbose_reason;
    uint8_t has_wds_settings, wds_has_ip, wds_has_gw, wds_has_mask, wds_has_dns1, wds_has_dns2, wds_ip_family;
    uint32_t wds_ip, wds_gw, wds_mask, wds_dns1, wds_dns2, wds_mtu;
    /* WMS 簡訊中繼資料（隱私安全：不保存任何門號或簡訊內容） */
    uint8_t wms_has_msg_id, wms_has_cause, wms_has_read, wms_read_tag, wms_read_format;
    uint8_t wms_has_routes, wms_has_list, wms_has_store_size, wms_has_store_free;
    uint16_t wms_msg_id, wms_cause, wms_read_len, wms_route_count;
    uint32_t wms_list_count, wms_store_max, wms_store_free;
    uint8_t wms_ind_has_mt, wms_ind_store, wms_ind_has_route, wms_ind_ack, wms_ind_format;
    uint16_t wms_ind_data_len;
    uint32_t wms_ind_index;
} FmQmiAnswer;

static inline int16_t fm_s16(const uint8_t *p) { return (int16_t)(uint16_t)(p[0] | p[1] << 8); }

/* UIM Card Status（TLV 0x10）：四個 u16 索引、卡片陣列（每張：狀態、UPIN 狀態／次數、UPUK 次數、錯誤碼、應用陣列）。
 * 完整走訪全部槽位與應用（最多保存 FM_UIM_MAX_SLOTS 槽），並回填 slot 0 舊欄位；版面不合就回 -1。 */
static inline int fm_uim_card(const uint8_t *val, uint32_t len, FmQmiAnswer *ans)
{
    if (len < 9u) return -1;
    ans->primary_gw = fm_le16(val);
    ans->slots = val[8];
    ans->slot_count = ans->slots < FM_UIM_MAX_SLOTS ? ans->slots : (uint8_t)FM_UIM_MAX_SLOTS;
    uint32_t pos = 9u;
    for (uint32_t slot_idx = 0; slot_idx < ans->slots; ++slot_idx) {
        if (pos + 6u > len) return -1;
        const uint8_t card_state = val[pos];
        const uint8_t upin_state = val[pos + 1u];
        const uint8_t card_error = val[pos + 4u];
        const uint8_t num_apps = val[pos + 5u];
        pos += 6u;
        FmUimSlot *slot = slot_idx < FM_UIM_MAX_SLOTS ? &ans->slot[slot_idx] : NULL;
        if (slot) {
            slot->card_state = card_state;
            slot->upin_state = upin_state;
            slot->card_error = card_error;
            slot->apps = num_apps;
        }
        for (uint32_t app_idx = 0; app_idx < num_apps; ++app_idx) {
            if (pos + 7u > len) return -1;
            const uint8_t app_type = val[pos];
            const uint8_t app_state = val[pos + 1u];
            const uint8_t perso_state = val[pos + 2u];
            const uint32_t aid_len = val[pos + 6u];
            const uint32_t aid_pos = pos + 7u;
            if (aid_len > len - aid_pos || len - (aid_pos + aid_len) < 7u) return -1;
            const uint32_t pin_pos = aid_pos + aid_len;
            const uint8_t pin1_state = val[pin_pos + 1u];
            const uint8_t pin1_retries = val[pin_pos + 2u];
            const uint8_t puk1_retries = val[pin_pos + 3u];
            if (slot) {
                if (app_idx == 0u) {
                    slot->app_type = app_type;
                    slot->app_state = app_state;
                    slot->perso_state = perso_state;
                    slot->pin1_state = pin1_state;
                    slot->pin1_retries = pin1_retries;
                    slot->puk1_retries = puk1_retries;
                }
                if ((app_type == 2u || app_type == 1u) && aid_len > 0u && aid_len <= FM_UIM_MAX_AID) {
                    const int better = !slot->has_gw_app ||
                        (app_type == 2u && slot->gw_app_type != 2u) ||
                        (app_type == slot->gw_app_type && app_state == 7u && slot->gw_app_state != 7u);
                    if (better) {
                        slot->has_gw_app = 1u;
                        slot->gw_app_type = app_type;
                        slot->gw_app_state = app_state;
                        slot->gw_aid_len = (uint8_t)aid_len;
                        for (uint32_t byte_idx = 0; byte_idx < aid_len; ++byte_idx)
                            slot->gw_aid[byte_idx] = val[aid_pos + byte_idx];
                    }
                }
            }
            pos = pin_pos + 7u;
        }
    }
    if (ans->slot_count) {
        const FmUimSlot *first = &ans->slot[0];
        ans->card_state = first->card_state;
        ans->upin_state = first->upin_state;
        ans->card_error = first->card_error;
        ans->apps = first->apps;
        ans->app_type = first->app_type;
        ans->app_state = first->app_state;
        ans->perso_state = first->perso_state;
        ans->pin1_state = first->pin1_state;
        ans->pin1_retries = first->pin1_retries;
        ans->puk1_retries = first->puk1_retries;
    }
    ans->has_card = 1;
    return 0;
}

/* 一則回應或指示（整份 QMI 訊息，service 是它來自的 QMI 服務）→ a。非回應／非指示或版面不對回 -1。 */
static inline int fm_qmi_decode(uint32_t service, const uint8_t *m, uint32_t length, FmQmiAnswer *a)
{
    uint8_t *raw = (uint8_t *)a;
    for (size_t i = 0; i < sizeof(*a); ++i) raw[i] = 0;
    if (!m || length < 7u) return -1;
    if (m[0] == FM_QMI_TYPE_INDICATION) {
        const uint32_t payload = (uint32_t)fm_le16(m + 5);
        if (payload > length - 7u) return -1;
        a->is_indication = 1u;
        a->transaction = fm_le16(m + 1);
        a->message = fm_le16(m + 3);
        if (service == FM_QMI_WMS && a->message == FM_WMS_EVENT_REPORT_IND) {
            uint32_t ind_len = 0u;
            const uint8_t *ind_val = fm_qmi_tlv(m, length, 0x11u, &ind_len);
            if (ind_val && ind_len >= 5u) {
                a->wms_ind_has_mt = 1u;
                a->wms_ind_store = ind_val[0];
                a->wms_ind_index = fm_le32(ind_val + 1);
            }
            ind_val = fm_qmi_tlv(m, length, 0x10u, &ind_len);
            if (ind_val && ind_len >= 4u && 4u + (uint32_t)fm_le16(ind_val + 2) <= ind_len) {
                a->wms_ind_has_route = 1u;
                a->wms_ind_ack = ind_val[0];
                a->wms_ind_format = ind_val[1];
                a->wms_ind_data_len = fm_le16(ind_val + 2);
            }
        }
        return 0;
    }
    if (m[0] != FM_QMI_TYPE_RESPONSE) return -1;
    a->transaction = (uint16_t)(m[1] | m[2] << 8);
    a->message = (uint16_t)(m[3] | m[4] << 8);
    uint32_t n = 0;
    const uint8_t *v = fm_qmi_tlv(m, length, 0x02, &n);
    if (!v || n != 4) return -1;
    a->has_result = 1;
    a->result = (uint16_t)(v[0] | v[1] << 8);
    a->error = (uint16_t)(v[2] | v[3] << 8);
    if (service == FM_QMI_WDS && a->message == FM_WDS_START_NETWORK) {
        if ((v = fm_qmi_tlv(m, length, 0x10u, &n)) && n >= 2u) {
            a->has_call_end_reason = 1u;
            a->call_end_reason = fm_le16(v);
        }
        if ((v = fm_qmi_tlv(m, length, 0x11u, &n)) && n >= 4u) {
            a->has_verbose_reason = 1u;
            a->verbose_reason_type = fm_le16(v);
            a->verbose_reason = fm_le16(v + 2);
        }
    }
    if (service == FM_QMI_WMS && a->message == FM_WMS_RAW_SEND) {
        if ((v = fm_qmi_tlv(m, length, 0x10u, &n)) && n >= 2u) {
            a->wms_has_cause = 1u;
            a->wms_cause = fm_le16(v);
        }
    }
    if (a->result) return 0;
    if (service == FM_QMI_DMS && a->message == FM_DMS_GET_OPERATING_MODE) {
        if ((v = fm_qmi_tlv(m, length, 0x01, &n)) && n == 1) { a->has_mode = 1; a->mode = v[0]; }
        if ((v = fm_qmi_tlv(m, length, 0x10, &n)) && n == 2) {
            a->has_offline_reason = 1;
            a->offline_reason = (uint16_t)(v[0] | (uint16_t)v[1] << 8);
        }
        if ((v = fm_qmi_tlv(m, length, 0x11, &n)) && n == 1) {
            a->has_hw_restricted = 1;
            a->hw_restricted = v[0];
        }
    } else if (service == FM_QMI_DMS && a->message == FM_DMS_GET_REVISION) {
        if ((v = fm_qmi_tlv(m, length, 0x01, &n))) {
            uint32_t k = 0;
            for (; k < n && k + 1 < sizeof a->revision; ++k) a->revision[k] = (v[k] >= 0x20 && v[k] < 0x7f) ? (char)v[k] : '?';
            a->revision[k] = 0;
        }
    } else if (service == FM_QMI_UIM && a->message == FM_UIM_GET_CARD_STATUS) {
        if ((v = fm_qmi_tlv(m, length, 0x10, &n)) && fm_uim_card(v, n, a)) return -1;
    } else if (service == FM_QMI_NAS && a->message == FM_NAS_GET_SERVING_SYSTEM) {
        if ((v = fm_qmi_tlv(m, length, 0x01, &n))) {
            if (n < 5 || n != 5u + v[4]) return -1;
            a->has_serving = 1;
            a->reg_state = v[0]; a->cs_attach = v[1]; a->ps_attach = v[2]; a->network = v[3];
            a->radio_count = v[4] < sizeof a->radio ? v[4] : (uint8_t)sizeof a->radio;
            for (unsigned i = 0; i < a->radio_count; ++i) a->radio[i] = v[5 + i];
        }
    } else if (service == FM_QMI_NAS && a->message == FM_NAS_GET_SIGNAL_INFO) {
        if ((v = fm_qmi_tlv(m, length, 0x12, &n)) && n == 1) { a->has_gsm = 1; a->gsm_rssi = (int8_t)v[0]; }
        if ((v = fm_qmi_tlv(m, length, 0x13, &n)) && n == 3) { a->has_wcdma = 1; a->wcdma_rssi = (int8_t)v[0]; a->wcdma_ecio = fm_s16(v + 1); }
        if ((v = fm_qmi_tlv(m, length, 0x14, &n)) && n == 6) {
            a->has_lte = 1; a->lte_rssi = (int8_t)v[0]; a->lte_rsrq = (int8_t)v[1]; a->lte_rsrp = fm_s16(v + 2); a->lte_snr = fm_s16(v + 4);
        }
        if ((v = fm_qmi_tlv(m, length, 0x17, &n)) && n == 4) { a->has_nr = 1; a->nr_rsrp = fm_s16(v); a->nr_snr = fm_s16(v + 2); }
    } else if (service == FM_QMI_WDA && a->message == FM_WDA_SET_DATA_FORMAT) {
        a->has_wda_format = 1u;
        if ((v = fm_qmi_tlv(m, length, 0x11u, &n)) && n == 4u) a->wda_link_layer = fm_le32(v);
        if ((v = fm_qmi_tlv(m, length, 0x12u, &n)) && n == 4u) a->wda_ul_aggr = fm_le32(v);
        if ((v = fm_qmi_tlv(m, length, 0x13u, &n)) && n == 4u) a->wda_dl_aggr = fm_le32(v);
        if ((v = fm_qmi_tlv(m, length, 0x15u, &n)) && n == 4u) a->wda_dl_max_dgrams = fm_le32(v);
        if ((v = fm_qmi_tlv(m, length, 0x16u, &n)) && n == 4u) a->wda_dl_max_size = fm_le32(v);
    } else if (service == FM_QMI_WDS && a->message == FM_WDS_START_NETWORK) {
        if ((v = fm_qmi_tlv(m, length, 0x01u, &n)) && n == 4u) {
            a->has_wds_handle = 1u;
            a->wds_handle = fm_le32(v);
        }
    } else if (service == FM_QMI_WDS && a->message == FM_WDS_GET_CURRENT_SETTINGS) {
        a->has_wds_settings = 1u;
        if ((v = fm_qmi_tlv(m, length, 0x1eu, &n)) && n == 4u && fm_le32(v) != 0u) { a->wds_has_ip = 1u; a->wds_ip = fm_le32(v); }
        if ((v = fm_qmi_tlv(m, length, 0x20u, &n)) && n == 4u && fm_le32(v) != 0u) { a->wds_has_gw = 1u; a->wds_gw = fm_le32(v); }
        if ((v = fm_qmi_tlv(m, length, 0x21u, &n)) && n == 4u && fm_le32(v) != 0u) { a->wds_has_mask = 1u; a->wds_mask = fm_le32(v); }
        if ((v = fm_qmi_tlv(m, length, 0x15u, &n)) && n == 4u && fm_le32(v) != 0u) { a->wds_has_dns1 = 1u; a->wds_dns1 = fm_le32(v); }
        if ((v = fm_qmi_tlv(m, length, 0x16u, &n)) && n == 4u && fm_le32(v) != 0u) { a->wds_has_dns2 = 1u; a->wds_dns2 = fm_le32(v); }
        if ((v = fm_qmi_tlv(m, length, 0x29u, &n)) && n == 4u) a->wds_mtu = fm_le32(v);
        if ((v = fm_qmi_tlv(m, length, 0x2bu, &n)) && n == 1u) a->wds_ip_family = v[0];
    } else if (service == FM_QMI_WMS && a->message == FM_WMS_RAW_SEND) {
        if ((v = fm_qmi_tlv(m, length, 0x01u, &n)) && n >= 2u) {
            a->wms_has_msg_id = 1u;
            a->wms_msg_id = fm_le16(v);
        }
    } else if (service == FM_QMI_WMS && a->message == FM_WMS_RAW_READ) {
        if ((v = fm_qmi_tlv(m, length, 0x01u, &n)) && n >= 4u) {
            const uint16_t raw_len = fm_le16(v + 2);
            if (4u + (uint32_t)raw_len > n) return -1;
            a->wms_has_read = 1u;
            a->wms_read_tag = v[0];
            a->wms_read_format = v[1];
            a->wms_read_len = raw_len;
        }
    } else if (service == FM_QMI_WMS && a->message == FM_WMS_GET_ROUTES) {
        if ((v = fm_qmi_tlv(m, length, 0x01u, &n)) && n >= 2u) {
            const uint16_t rcnt = fm_le16(v);
            if (2u + 4u * (uint32_t)rcnt != n) return -1;
            a->wms_has_routes = 1u;
            a->wms_route_count = rcnt;
        }
    } else if (service == FM_QMI_WMS && a->message == FM_WMS_LIST_MESSAGES) {
        if ((v = fm_qmi_tlv(m, length, 0x01u, &n)) && n >= 4u) {
            const uint32_t mcnt = fm_le32(v);
            if (mcnt > (n - 4u) / 5u || 4u + 5u * mcnt != n) return -1;
            a->wms_has_list = 1u;
            a->wms_list_count = mcnt;
        }
    } else if (service == FM_QMI_WMS && a->message == FM_WMS_GET_STORE_MAX_SIZE) {
        if ((v = fm_qmi_tlv(m, length, 0x01u, &n)) && n == 4u) {
            a->wms_has_store_size = 1u;
            a->wms_store_max = fm_le32(v);
        }
        if ((v = fm_qmi_tlv(m, length, 0x10u, &n)) && n == 4u) {
            a->wms_has_store_free = 1u;
            a->wms_store_free = fm_le32(v);
        }
    }
    return 0;
}

/* ---- SM8550 IPA v5.5、GSI v5.0、QMI IPA 0x31、RPMh BCM 與 apps_smmu 純邏輯 ----
 * 暫存器與區段表對照 Linux drivers/net/ipa/{reg/ipa_reg-v5.5.c,data/ipa_data-v5.5.c,reg/gsi_reg-v5.0.c,gsi.c,gsi_trans.c,
 * ipa_cmd.c,ipa_table.c,ipa_endpoint.c,ipa_qmi.c} 與 sm8550.dtsi（GPL，只取位址與欄位事實）；
 * QMI 0x23 filter rule 回覆版面另對照 hakios.mpss.c（APSL 2.0，只取協定事實）。 */
#define FM_CMD_DB_SLAVE_BCM 5u
#define FM_IPA5_REG_PA 0x3f40000ull
#define FM_IPA5_REG_SIZE 0x10000u
#define FM_IPA5_SRAM_PA 0x3f50000ull
#define FM_IPA5_SRAM_SIZE 0x5000u
#define FM_IPA5_SRAM_MAP_SIZE 0x8000u
#define FM_IPA5_CANARY_VAL 0xdeadbeefu

#define FM_IPA5_FLAVOR_0 0x00u
#define FM_IPA5_COMP_CFG 0x48u
#define FM_IPA5_ROUTE 0x54u
#define FM_IPA5_SHARED_MEM_SIZE 0x5cu
#define FM_IPA5_QSB_MAX_WRITES 0x70u
#define FM_IPA5_QSB_MAX_READS 0x74u
#define FM_IPA5_FILT_ROUT_CACHE_FLUSH 0x404u
#define FM_IPA5_LOCAL_PKT_PROC_CNTXT 0x478u
#define FM_IPA5_IDLE_INDICATION_CFG 0x4a8u
#define FM_IPA5_QTIME_TIMESTAMP_CFG 0x4acu
#define FM_IPA5_TIMERS_XO_CLK_DIV_CFG 0x4b0u
#define FM_IPA5_TIMERS_PULSE_GRAN_CFG 0x4b4u
#define FM_IPA5_SRC_RSRC_GRP_01(type) (0x500u + 0x20u * (type))
#define FM_IPA5_SRC_RSRC_GRP_23(type) (0x504u + 0x20u * (type))
#define FM_IPA5_SRC_RSRC_GRP_45(type) (0x508u + 0x20u * (type))
#define FM_IPA5_SRC_RSRC_GRP_67(type) (0x50cu + 0x20u * (type))
#define FM_IPA5_DST_RSRC_GRP_01(type) (0x600u + 0x20u * (type))
#define FM_IPA5_DST_RSRC_GRP_23(type) (0x604u + 0x20u * (type))
#define FM_IPA5_DST_RSRC_GRP_45(type) (0x608u + 0x20u * (type))
#define FM_IPA5_DST_RSRC_GRP_67(type) (0x60cu + 0x20u * (type))
#define FM_IPA5_ENDP_INIT_CFG(ep) (0x1008u + 0x80u * (ep))
#define FM_IPA5_ENDP_INIT_NAT(ep) (0x100cu + 0x80u * (ep))
#define FM_IPA5_ENDP_INIT_HDR(ep) (0x1010u + 0x80u * (ep))
#define FM_IPA5_ENDP_INIT_HDR_EXT(ep) (0x1014u + 0x80u * (ep))
#define FM_IPA5_ENDP_INIT_HDR_METADATA_MASK(ep) (0x1018u + 0x80u * (ep))
#define FM_IPA5_ENDP_INIT_MODE(ep) (0x1020u + 0x80u * (ep))
#define FM_IPA5_ENDP_INIT_AGGR(ep) (0x1024u + 0x80u * (ep))
#define FM_IPA5_ENDP_INIT_HOL_BLOCK_EN(ep) (0x102cu + 0x80u * (ep))
#define FM_IPA5_ENDP_INIT_HOL_BLOCK_TIMER(ep) (0x1030u + 0x80u * (ep))
#define FM_IPA5_ENDP_INIT_DEAGGR(ep) (0x1034u + 0x80u * (ep))
#define FM_IPA5_ENDP_INIT_RSRC_GRP(ep) (0x1038u + 0x80u * (ep))
#define FM_IPA5_ENDP_INIT_SEQ(ep) (0x103cu + 0x80u * (ep))
#define FM_IPA5_ENDP_STATUS(ep) (0x1040u + 0x80u * (ep))
#define FM_IPA5_ENDP_FILTER_CACHE_CFG(ep) (0x105cu + 0x80u * (ep))
#define FM_IPA5_ENDP_ROUTER_CACHE_CFG(route) (0x1060u + 0x80u * (route))
#define FM_IPA5_IRQ_STTS 0xc008u
#define FM_IPA5_IRQ_EN 0xc00cu
#define FM_IPA5_IRQ_CLR 0xc010u

/* GSI v5.0（EE0 AP，基底 0x3e04000 已 16 KiB 對齊，大小 0x28000 涵蓋全部通道／事件／門鈴／控制暫存器） */
#define FM_GSI5_MAP_PA 0x3e04000ull
#define FM_GSI5_MAP_SIZE 0x28000u
#define FM_GSI5_INTER_EE_CH_IRQ_MSK 0xc01cu
#define FM_GSI5_INTER_EE_EV_IRQ_MSK 0xc028u
#define FM_GSI5_CH_CNTXT_0(ch) (0x14000u + 0x80u * (ch))
#define FM_GSI5_CH_CNTXT_1(ch) (0x14004u + 0x80u * (ch))
#define FM_GSI5_CH_CNTXT_2(ch) (0x14008u + 0x80u * (ch))
#define FM_GSI5_CH_CNTXT_3(ch) (0x1400cu + 0x80u * (ch))
#define FM_GSI5_CH_QOS(ch) (0x14048u + 0x80u * (ch))
#define FM_GSI5_CH_SCRATCH_0(ch) (0x1404cu + 0x80u * (ch))
#define FM_GSI5_CH_SCRATCH_1(ch) (0x14050u + 0x80u * (ch))
#define FM_GSI5_CH_SCRATCH_2(ch) (0x14054u + 0x80u * (ch))
#define FM_GSI5_CH_SCRATCH_3(ch) (0x14058u + 0x80u * (ch))
#define FM_GSI5_EV_CNTXT_0(ev) (0x1c000u + 0x80u * (ev))
#define FM_GSI5_EV_CNTXT_1(ev) (0x1c004u + 0x80u * (ev))
#define FM_GSI5_EV_CNTXT_2(ev) (0x1c008u + 0x80u * (ev))
#define FM_GSI5_EV_CNTXT_3(ev) (0x1c00cu + 0x80u * (ev))
#define FM_GSI5_EV_CNTXT_4(ev) (0x1c010u + 0x80u * (ev))
#define FM_GSI5_EV_CNTXT_8(ev) (0x1c020u + 0x80u * (ev))
#define FM_GSI5_EV_CNTXT_9(ev) (0x1c024u + 0x80u * (ev))
#define FM_GSI5_EV_CNTXT_10(ev) (0x1c028u + 0x80u * (ev))
#define FM_GSI5_EV_CNTXT_11(ev) (0x1c02cu + 0x80u * (ev))
#define FM_GSI5_EV_CNTXT_12(ev) (0x1c030u + 0x80u * (ev))
#define FM_GSI5_EV_CNTXT_13(ev) (0x1c034u + 0x80u * (ev))
#define FM_GSI5_EV_SCRATCH_0(ev) (0x1c048u + 0x80u * (ev))
#define FM_GSI5_EV_SCRATCH_1(ev) (0x1c04cu + 0x80u * (ev))
#define FM_GSI5_CH_DB_0(ch) (0x24000u + 0x08u * (ch))
#define FM_GSI5_EV_DB_0(ev) (0x24800u + 0x08u * (ev))
#define FM_GSI5_STATUS 0x25000u
#define FM_GSI5_CH_CMD 0x25008u
#define FM_GSI5_EV_CMD 0x25010u
#define FM_GSI5_HW_PARAM_2 0x25040u
#define FM_GSI5_HW_PARAM_4 0x25050u
#define FM_GSI5_CNTXT_TYPE_IRQ_MSK 0x25088u
#define FM_GSI5_CNTXT_SRC_CH_IRQ_MSK 0x25094u
#define FM_GSI5_CNTXT_SRC_CH_IRQ_CLR 0x25098u
#define FM_GSI5_CNTXT_SRC_EV_IRQ_MSK 0x250a0u
#define FM_GSI5_CNTXT_SRC_EV_IRQ_CLR 0x250a4u
#define FM_GSI5_CNTXT_SRC_IEOB_IRQ_MSK 0x250acu
#define FM_GSI5_CNTXT_SRC_IEOB_IRQ_CLR 0x250b0u
#define FM_GSI5_CNTXT_GLOB_IRQ_EN 0x25204u
#define FM_GSI5_CNTXT_GLOB_IRQ_CLR 0x25208u
#define FM_GSI5_CNTXT_GSI_IRQ_EN 0x25210u
#define FM_GSI5_CNTXT_GSI_IRQ_CLR 0x25214u
#define FM_GSI5_CNTXT_INTSET 0x25220u
#define FM_GSI5_ERROR_LOG 0x25240u
#define FM_GSI5_ERROR_LOG_CLR 0x25244u
#define FM_GSI5_SCRATCH_0 0x25400u

#define FM_GSI_STATE_NOT_ALLOCATED 0u
#define FM_GSI_STATE_ALLOCATED 1u
#define FM_GSI_STATE_STARTED 2u
#define FM_GSI_STATE_ERROR 15u
#define FM_GSI_CMD_ALLOCATE 0x0u
#define FM_GSI_CMD_START 0x1u
#define FM_GSI_CMD_STOP 0x2u
#define FM_GSI_CMD_RESET 0x9u
#define FM_GSI_CMD_DE_ALLOC 0xau
#define FM_GSI_RE_XFER 0x2u
#define FM_GSI_RE_IMMD_CMD 0x3u
#define FM_GSI_RING_ELEM_SIZE 16u

#define FM_IPA_CMD_V4_FILTER_INIT 0x3u
#define FM_IPA_CMD_V6_FILTER_INIT 0x4u
#define FM_IPA_CMD_V4_ROUTING_INIT 0x7u
#define FM_IPA_CMD_V6_ROUTING_INIT 0x8u
#define FM_IPA_CMD_HDR_INIT_LOCAL 0x9u
#define FM_IPA_CMD_DMA_SHARED_MEM 0x13u

/* IPA v5.5 SRAM 區段與表常數（linux-ipa_data-v5.5.c） */
#define FM_IPA5_MEM_V4_FLT_HASH_OFF 0x1288u
#define FM_IPA5_MEM_V4_FLT_OFF 0x1308u
#define FM_IPA5_MEM_V6_FLT_HASH_OFF 0x1388u
#define FM_IPA5_MEM_V6_FLT_OFF 0x1408u
#define FM_IPA5_MEM_FLT_SIZE 0x0078u
#define FM_IPA5_MEM_V4_RT_HASH_OFF 0x1488u
#define FM_IPA5_MEM_V4_RT_OFF 0x1528u
#define FM_IPA5_MEM_V6_RT_HASH_OFF 0x15c8u
#define FM_IPA5_MEM_V6_RT_OFF 0x1668u
#define FM_IPA5_MEM_RT_SIZE 0x0098u
#define FM_IPA5_MEM_MODEM_HDR_OFF 0x1708u
#define FM_IPA5_MEM_MODEM_HDR_SIZE 0x0240u
#define FM_IPA5_MEM_AP_HDR_SIZE 0x01e0u
#define FM_IPA5_MEM_MODEM_CTX_OFF 0x1b40u
#define FM_IPA5_MEM_MODEM_CTX_SIZE 0x0b20u
#define FM_IPA5_MEM_AP_CTX_OFF 0x2660u
#define FM_IPA5_MEM_AP_CTX_SIZE 0x0200u
#define FM_IPA5_MEM_QUOTA_MODEM_OFF 0x2868u
#define FM_IPA5_MEM_QUOTA_MODEM_SIZE 0x0060u
#define FM_IPA5_MEM_HW_FLT_STATS_OFF 0x2cd0u /* ipam.ko hw_ver=0x18 mem_map+0x140 */
#define FM_IPA5_HW_FLT_STATS_SIZE 0x0340u    /* ipam.ko ipa3_q6_clnt_svc_arrive TLV 0x23 */
#define FM_IPA5_HW_FLT_STATS_START_IDX 0x80u
#define FM_IPA5_HW_FLT_STATS_END_IDX 0xb3u
#define FM_IPA5_PER_STATS_SMEM_SIZE 0x0800u  /* ipam.ko ipa3_q6_clnt_svc_arrive TLV 0x25 */
#define FM_IPA5_PER_STATS_ENTRIES 20u        /* ipam.ko ipa3_peripheral_stats_init MSM path */
#define FM_IPA5_PER_STATS_WORDS 43u          /* 0x00..0xab (172 B) */
#define FM_IPA5_MEM_STATS_DROP_OFF 0x3870u
#define FM_IPA5_MEM_STATS_DROP_SIZE 0x0020u
#define FM_IPA5_MEM_MODEM_OFF 0x3898u
#define FM_IPA5_MEM_MODEM_SIZE 0x0d48u
#define FM_IPA5_MODEM_ROUTE_COUNT 11u
#define FM_IPA5_ROUTE_ENTRIES 19u /* 0x98 / 8 */
#define FM_IPA5_FILTERED_MASK ((1ull << 2) | (1ull << 12) | (1ull << 15)) /* 0x9004，v5.0+ 不左移 */
#define FM_IPA5_FILTER_INIT_SIZE 0x20u /* (1 bitmap + 3 endpoints) * 8 */
#define FM_IPA5_FILTER_TAIL_SIZE (FM_IPA5_MEM_FLT_SIZE - FM_IPA5_FILTER_INIT_SIZE) /* 0x58 */

/* AP 四條 GSI 通道與對應 IPA 端點（linux-ipa_data-v5.5.c） */
#define FM_IPA5_CH_CMD_TX 12u
#define FM_IPA5_EP_CMD_TX 14u
#define FM_IPA5_CH_LAN_RX 13u
#define FM_IPA5_EP_LAN_RX 16u
#define FM_IPA5_CH_MODEM_TX 11u
#define FM_IPA5_CH_MODEM_RX 1u

/* QMI IPA 服務 0x31 雙向交握（AP 掛 0x101，modem 掛 0x201） */
#define FM_IPA_HOST_INSTANCE 0x101u
#define FM_IPA_MODEM_INSTANCE 0x201u
#define FM_IPA_QMI_IND_REGISTER 0x20u
#define FM_IPA_QMI_INIT_DRIVER 0x21u
#define FM_IPA_QMI_INIT_COMPLETE 0x22u
#define FM_IPA_QMI_INSTALL_FILTER 0x23u
#define FM_IPA_QMI_FILTER_NOTIF 0x24u
#define FM_IPA_QMI_CONFIG 0x27u
#define FM_IPA_QMI_DRIVER_INIT_COMPLETE 0x35u
#define FM_IPA_QMI_FILTER_SPEC_SIZE 173u
#define FM_IPA_QMI_FILTER_ID_OFF 168u
#define FM_IPA_QMI_FILTER_MAX 64u

#define FM_SMMU_PA 0x15000000ull
#define FM_SMMU_SIZE 0x100000u
#define FM_SMMU_BYPASS_BANK 4u
#define FM_SMMU_FAULT_S2CR (2u << 16)
#define FM_SMMU_IPA_AP_SID 0x4a0u
#define FM_SMMU_IPA_UC_SID 0x4a2u

static inline int fm_is_bcm(uint32_t address)
{
    return address != 0u && ((address >> 16) & 0xfu) == FM_CMD_DB_SLAVE_BCM;
}
static inline uint32_t fm_bcm_vote(int enable, uint32_t vote)
{
    return (1u << 30) | (enable ? (1u << 29) : 0u) | (vote & 0x3fffu);
}
static inline uint32_t fm_ipa_shared_mem_offset(uint32_t reg) { return 8u * (reg >> 16); }
static inline uint32_t fm_ipa_shared_mem_size(uint32_t reg) { return 8u * (reg & 0xffffu); }
static inline uint32_t fm_ipa_rsrc_pair(uint32_t x_min, uint32_t x_max, uint32_t y_min, uint32_t y_max)
{
    return (x_min & 0x3fu) | ((x_max & 0x3fu) << 8) | ((y_min & 0x3fu) << 16) | ((y_max & 0x3fu) << 24);
}
static inline uint32_t fm_ipa_route_reg(uint32_t endpoint_id)
{
    return (endpoint_id & 0xffu) | ((endpoint_id & 0xffu) << 8) | (1u << 26) | (1u << 27);
}
static inline uint32_t fm_gsi_ev_cntxt0(void)
{
    return 2u | (1u << 7) | (FM_GSI_RING_ELEM_SIZE << 24);
}
static inline uint32_t fm_gsi_ch_cntxt0(int toward_ipa)
{
    return 2u | (toward_ipa ? (1u << 7) : 0u) | (FM_GSI_RING_ELEM_SIZE << 24);
}
static inline uint32_t fm_gsi_ch_cntxt1(uint32_t ring_bytes, uint32_t ev_id)
{
    return (ring_bytes & 0xffffffu) | ((ev_id & 0xffu) << 24);
}
static inline uint32_t fm_gsi_ch_qos(int is_cmd)
{
    return (is_cmd ? 0xfu : (1u << 10)) | (1u << 24);
}
static inline uint32_t fm_gsi_cmd_word(uint32_t id, uint32_t opcode)
{
    return (id & 0xffu) | ((opcode & 0xffu) << 24);
}
static inline void fm_gsi_tre_fill(uint8_t out[16], uint64_t addr, uint16_t len_or_opcode, int is_cmd, int last_tre)
{
    uint32_t flags = (uint32_t)(is_cmd ? FM_GSI_RE_IMMD_CMD : FM_GSI_RE_XFER) << 16;
    flags |= last_tre ? (1u << 9) : 1u; /* IEOT or CHAIN */
    fm_put64(out, addr);
    fm_put16(out + 8, len_or_opcode);
    fm_put16(out + 10, 0u);
    fm_put32(out + 12, flags);
}
static inline void fm_ipa_cmd_hdr_init_local(uint8_t out[16], uint64_t dma_addr, uint32_t local_off, uint16_t size)
{
    const uint32_t flags = ((uint32_t)size & 0xfffu) | ((local_off & 0xffffu) << 12);
    fm_put64(out, dma_addr);
    fm_put32(out + 8, flags);
    fm_put32(out + 12, 0u);
}
static inline void fm_ipa_cmd_dma_shared_mem(uint8_t out[16], uint64_t dma_addr, uint32_t local_off, uint16_t size, int toward_ipa)
{
    fm_put16(out, 0u);
    fm_put16(out + 2, size);
    fm_put16(out + 4, (uint16_t)local_off);
    fm_put16(out + 6, toward_ipa ? 0u : 1u);
    fm_put64(out + 8, dma_addr);
}
static inline void fm_ipa_cmd_table_init(uint8_t out[24], uint64_t nhash_addr, uint32_t nhash_off, uint16_t nhash_size,
    uint64_t hash_addr, uint32_t hash_off, uint16_t hash_size)
{
    uint64_t flags = (((uint64_t)nhash_size & 0xfffu) << 28) | (((uint64_t)nhash_off & 0xffffu) << 40);
    if (hash_size) {
        flags |= ((uint64_t)hash_size & 0xfffu) | (((uint64_t)hash_off & 0xffffu) << 12);
    }
    fm_put64(out, hash_size ? hash_addr : 0ull);
    fm_put64(out + 8, flags);
    fm_put64(out + 16, nhash_addr);
}

static inline uint32_t fm_qmi_put_u32(uint8_t *out, uint8_t tlv_type, uint32_t value)
{
    out[0] = tlv_type;
    fm_put16(out + 1, 4u);
    fm_put32(out + 3, value);
    return 7u;
}
static inline uint32_t fm_qmi_put_pair(uint8_t *out, uint8_t tlv_type, uint32_t first, uint32_t second)
{
    out[0] = tlv_type;
    fm_put16(out + 1, 8u);
    fm_put32(out + 3, first);
    fm_put32(out + 7, second);
    return 11u;
}
/* 組 QMI 回覆或通知（只帶 TLV 0x02 result/error，共 14 bytes）。 */
static inline uint32_t fm_ipa_qmi_result_msg(uint8_t *out, uint32_t capacity, uint8_t msg_type, uint16_t transaction,
    uint16_t message, uint16_t result, uint16_t error)
{
    if (!out || capacity < 14u) return 0u;
    out[0] = msg_type;
    fm_put16(out + 1, transaction);
    fm_put16(out + 3, message);
    fm_put16(out + 5, 7u);
    out[7] = 0x02u;
    fm_put16(out + 8, 4u);
    fm_put16(out + 10, result);
    fm_put16(out + 12, error);
    return 14u;
}
/* 建 SMEM 497 開頭的 peripheral stats 標頭（172 B = 43 個 u32；對照 SM8550 原廠 ipam.ko ipa3_peripheral_stats_init）。 */
static inline uint32_t fm_ipa_init_per_stats_words(uint32_t *out_words, uint32_t word_capacity)
{
    if (!out_words || word_capacity < FM_IPA5_PER_STATS_WORDS) return 0u;
    for (uint32_t idx = 0u; idx < FM_IPA5_PER_STATS_WORDS; ++idx) out_words[idx] = 0u;
    out_words[1] = FM_IPA5_PER_STATS_ENTRIES;
    uint32_t pos = 2u;
    for (uint32_t stat_id = 0u; stat_id < FM_IPA5_PER_STATS_ENTRIES; ++stat_id) {
        const uint32_t val_bytes = (stat_id == 6u) ? 8u : 4u;
        out_words[pos++] = (val_bytes << 16) | stat_id;
        pos += val_bytes / 4u;
    }
    return pos;
}
/* 組 QMI IPA 0x21 INIT_DRIVER 請求（對照 SM8550 原廠 ipam.ko ipa3_q6_clnt_svc_arrive 與 hw_ver=0x18 mem_map；含 TLV 0x23/0x24/0x25）。 */
static inline uint32_t fm_ipa_qmi_init_driver_req(uint8_t *out, uint32_t capacity, uint16_t transaction, uint32_t mem_offset,
    uint32_t modem_rx_ep, uint32_t smem_size, uint32_t smem_phys)
{
    const uint32_t total = 7u + 7u * 10u + 11u * 7u + 13u + (smem_size ? 11u : 0u) + (smem_phys ? 11u : 0u);
    if (!out || capacity < total) return 0u;
    uint32_t pos = 7u;
    const uint32_t hdr_start = mem_offset + FM_IPA5_MEM_MODEM_HDR_OFF;
    const uint32_t ctx_start = mem_offset + FM_IPA5_MEM_MODEM_CTX_OFF;
    const uint32_t route_end = FM_IPA5_MODEM_ROUTE_COUNT - 1u;
    pos += fm_qmi_put_u32(out + pos, 0x10u, 3u); /* MSM Android */
    pos += fm_qmi_put_pair(out + pos, 0x11u, hdr_start, hdr_start + FM_IPA5_MEM_MODEM_HDR_SIZE - 1u);
    pos += fm_qmi_put_pair(out + pos, 0x12u, mem_offset + FM_IPA5_MEM_V4_RT_OFF, route_end);
    pos += fm_qmi_put_pair(out + pos, 0x13u, mem_offset + FM_IPA5_MEM_V6_RT_OFF, route_end);
    pos += fm_qmi_put_u32(out + pos, 0x14u, mem_offset + FM_IPA5_MEM_V4_FLT_OFF);
    pos += fm_qmi_put_u32(out + pos, 0x15u, mem_offset + FM_IPA5_MEM_V6_FLT_OFF);
    pos += fm_qmi_put_pair(out + pos, 0x16u, mem_offset + FM_IPA5_MEM_MODEM_OFF, FM_IPA5_MEM_MODEM_SIZE);
    pos += fm_qmi_put_u32(out + pos, 0x17u, modem_rx_ep);
    pos += fm_qmi_put_pair(out + pos, 0x19u, ctx_start, ctx_start + FM_IPA5_MEM_MODEM_CTX_SIZE - 1u);
    pos += fm_qmi_put_pair(out + pos, 0x1bu, mem_offset + FM_IPA5_MEM_V4_RT_HASH_OFF, route_end);
    pos += fm_qmi_put_pair(out + pos, 0x1cu, mem_offset + FM_IPA5_MEM_V6_RT_HASH_OFF, route_end);
    pos += fm_qmi_put_u32(out + pos, 0x1du, mem_offset + FM_IPA5_MEM_V4_FLT_HASH_OFF);
    pos += fm_qmi_put_u32(out + pos, 0x1eu, mem_offset + FM_IPA5_MEM_V6_FLT_HASH_OFF);
    pos += fm_qmi_put_u32(out + pos, 0x1fu, mem_offset + FM_IPA5_MEM_QUOTA_MODEM_OFF);
    pos += fm_qmi_put_u32(out + pos, 0x20u, FM_IPA5_MEM_QUOTA_MODEM_SIZE);
    pos += fm_qmi_put_u32(out + pos, 0x21u, mem_offset + FM_IPA5_MEM_STATS_DROP_OFF);
    pos += fm_qmi_put_u32(out + pos, 0x22u, FM_IPA5_MEM_STATS_DROP_SIZE);
    out[pos++] = 0x23u;
    fm_put16(out + pos, 10u);
    pos += 2u;
    fm_put32(out + pos, FM_IPA5_MEM_HW_FLT_STATS_OFF);
    pos += 4u;
    fm_put32(out + pos, FM_IPA5_HW_FLT_STATS_SIZE);
    pos += 4u;
    out[pos++] = FM_IPA5_HW_FLT_STATS_START_IDX;
    out[pos++] = FM_IPA5_HW_FLT_STATS_END_IDX;
    if (smem_size) pos += fm_qmi_put_pair(out + pos, 0x24u, 0u, smem_size);
    if (smem_phys) pos += fm_qmi_put_pair(out + pos, 0x25u, smem_phys, FM_IPA5_PER_STATS_SMEM_SIZE);
    out[0] = FM_QMI_TYPE_REQUEST;
    fm_put16(out + 1, transaction);
    fm_put16(out + 3, FM_IPA_QMI_INIT_DRIVER);
    fm_put16(out + 5, (uint16_t)(pos - 7u));
    return pos;
}
/* 組 QMI IPA 0x23 INSTALL_FILTER_RULE 回覆：解析 TLV 0x15（count + count*173 B）並於 TLV 0x11 回聲每筆 offset 168 的 4-byte rule id。 */
static inline uint32_t fm_ipa_qmi_filter_resp(uint8_t *out, uint32_t capacity, uint16_t transaction, const uint8_t *req,
    uint32_t req_len, uint32_t *out_count)
{
    if (out_count) *out_count = 0u;
    if (!out || capacity < 14u) return 0u;
    uint32_t list_len = 0u;
    const uint8_t *list = (req && req_len >= 7u) ? fm_qmi_tlv(req, req_len, 0x15u, &list_len) : NULL;
    const uint32_t count = (list && list_len >= 1u) ? list[0] : 0u;
    if (!list || count > FM_IPA_QMI_FILTER_MAX || list_len != 1u + count * FM_IPA_QMI_FILTER_SPEC_SIZE) {
        return fm_ipa_qmi_result_msg(out, capacity, FM_QMI_TYPE_RESPONSE, transaction, FM_IPA_QMI_INSTALL_FILTER, 0u, 0u);
    }
    const uint32_t tlv11_len = 1u + 4u * count;
    const uint32_t total = 14u + 3u + tlv11_len;
    if (capacity < total) return 0u;
    (void)fm_ipa_qmi_result_msg(out, capacity, FM_QMI_TYPE_RESPONSE, transaction, FM_IPA_QMI_INSTALL_FILTER, 0u, 0u);
    uint32_t pos = 14u;
    out[pos++] = 0x11u;
    fm_put16(out + pos, (uint16_t)tlv11_len);
    pos += 2u;
    out[pos++] = (uint8_t)count;
    for (uint32_t idx = 0u; idx < count; ++idx) {
        const uint8_t *id_src = list + 1u + idx * FM_IPA_QMI_FILTER_SPEC_SIZE + FM_IPA_QMI_FILTER_ID_OFF;
        for (uint32_t byte_idx = 0u; byte_idx < 4u; ++byte_idx) out[pos++] = id_src[byte_idx];
    }
    fm_put16(out + 5, (uint16_t)(pos - 7u));
    if (out_count) *out_count = count;
    return pos;
}

static inline int fm_smmu_overlap(uint32_t smr, uint32_t sid, uint32_t mask)
{
    if (!(smr & (1u << 31))) return 0;
    const uint32_t smr_mask = (smr >> 16) & 0x7fffu;
    const uint32_t smr_sid = smr & 0x7fffu;
    return ((smr_sid ^ sid) & ~(smr_mask | mask) & 0x7fffu) == 0u;
}
static inline int fm_smmu_pick_high_slot(const uint32_t *smrs, const uint32_t *s2crs, unsigned slots, uint32_t sid, uint32_t mask)
{
    if (!smrs || !s2crs || !slots) return -1;
    for (unsigned idx = 0; idx < slots; ++idx) {
        if (fm_smmu_overlap(smrs[idx], sid, mask)) return -2;
    }
    for (unsigned rev = slots; rev > 0u; --rev) {
        const unsigned slot = rev - 1u;
        if (!(smrs[slot] & (1u << 31)) && s2crs[slot] == FM_SMMU_FAULT_S2CR) return (int)slot;
    }
    return -1;
}

/* ---- M7 資料面：IPA v5.5 ENDP_INIT_AGGR、QMAP v1/v5 與 en1 合成路由器純邏輯 ----
 * ENDP_INIT_AGGR 位元版面對照 Linux drivers/net/ipa/reg/ipa_reg-v5.5.c（AGGR_EN=[1:0]、AGGR_TYPE=[4:2]、
 * BYTE_LIMIT=[10:5]、TIME_LIMIT=[16:12]、PKT_LIMIT=[22:17]、SW_EOF_ACTIVE=BIT(23)）。
 * QMAP v1/v5 與指令 ACK 對照 Linux drivers/net/ethernet/qualcomm/rmnet/rmnet_map.h 與 HakiOS hakios.ipa.c；
 * 合成 ARP／DHCPv4 路由器對照 HakiOS hakios.cell.c（APSL 2.0，只取協定事實，程式為重寫；嚴禁紀錄輸出 IP）。 */
static inline uint32_t fm_ipa5_endp_status(int enable, uint32_t status_ep, int pkt_suppress)
{
    /* IPA v5.0+/v5.5 ENDP_STATUS（ipam.ko ipareg_construct_endp_status_n_v5_0 0xd8040、linux-ipa_reg-v5.5.c）：
     * bit 0 STATUS_EN、bits [8:1] STATUS_ENDP、bit 9 STATUS_PKT_SUPPRESS。 */
    return (enable ? 1u : 0u) | ((status_ep & 0xffu) << 1) | (pkt_suppress ? (1u << 9) : 0u);
}
static inline uint32_t fm_ipa5_endp_init_aggr(uint32_t aggr_en, uint32_t aggr_type, uint32_t byte_limit_kb,
    uint32_t time_limit, uint32_t pkt_limit, int sw_eof)
{
    return (aggr_en & 0x3u) | ((aggr_type & 0x7u) << 2) | ((byte_limit_kb & 0x3fu) << 5) |
        ((time_limit & 0x1fu) << 12) | ((pkt_limit & 0x3fu) << 17) | (sw_eof ? (1u << 23) : 0u);
}
#define FM_IPA5_AGGR_M7 0x008050b1u /* en=1 type=4(GENERIC) byte_limit=5KB time_limit=5 sw_eof=1 */

#define FM_QMAP_CMD_FLOW_DISABLE 1u
#define FM_QMAP_CMD_FLOW_ENABLE 2u
#define FM_QMAP_CMD_TYPE_REQUEST 0u
#define FM_QMAP_CMD_TYPE_ACK 1u

typedef struct {
    uint8_t is_cmd, has_v5_ext, mux_id, pad_len;
    uint32_t payload_off, payload_len, next_off;
} FmQmapPacket;

typedef struct {
    uint8_t cmd_name, cmd_type;
    uint32_t transaction_id;
} FmQmapCmd;

static inline uint16_t fm_be16(const uint8_t *ptr)
{
    return (uint16_t)(((uint16_t)ptr[0] << 8) | ptr[1]);
}
static inline uint32_t fm_be32(const uint8_t *ptr)
{
    return ((uint32_t)ptr[0] << 24) | ((uint32_t)ptr[1] << 16) | ((uint32_t)ptr[2] << 8) | ptr[3];
}
static inline void fm_put_be16(uint8_t *ptr, uint32_t val)
{
    ptr[0] = (uint8_t)(val >> 8);
    ptr[1] = (uint8_t)val;
}
static inline void fm_put_be32(uint8_t *ptr, uint32_t val)
{
    ptr[0] = (uint8_t)(val >> 24);
    ptr[1] = (uint8_t)(val >> 16);
    ptr[2] = (uint8_t)(val >> 8);
    ptr[3] = (uint8_t)val;
}

static inline int fm_qmap_parse(const uint8_t *buf, uint32_t len, uint32_t pos, FmQmapPacket *out)
{
    if (!buf || !out || pos > len || len - pos < 4u) return -1;
    const uint8_t flags = buf[pos];
    const uint8_t is_cmd = (uint8_t)((flags >> 7) & 1u);
    const uint8_t has_v5 = (uint8_t)((flags >> 6) & 1u);
    const uint8_t pad_len = (uint8_t)(flags & 0x3fu);
    const uint8_t mux_id = buf[pos + 1u];
    const uint32_t pkt_len = fm_be16(buf + pos + 2u);
    const uint32_t hdr_len = 4u + ((!is_cmd && has_v5) ? 8u : 0u);
    if (!pkt_len || pad_len > pkt_len || hdr_len > len - pos || pkt_len > len - pos - hdr_len) return -1;
    out->is_cmd = is_cmd;
    out->has_v5_ext = has_v5;
    out->mux_id = mux_id;
    out->pad_len = pad_len;
    out->payload_off = pos + hdr_len;
    out->payload_len = pkt_len - pad_len;
    out->next_off = pos + hdr_len + pkt_len;
    return 0;
}

static inline uint32_t fm_qmap_wrap(uint8_t *out, uint32_t capacity, uint8_t mux_id, const uint8_t *payload,
    uint32_t payload_len, int is_cmd)
{
    if (!out || !payload || !payload_len || payload_len > 0xffffu || capacity < 4u + payload_len) return 0u;
    out[0] = is_cmd ? 0x80u : 0x00u;
    out[1] = mux_id;
    fm_put_be16(out + 2, payload_len);
    for (uint32_t idx = 0u; idx < payload_len; ++idx) out[4u + idx] = payload[idx];
    return 4u + payload_len;
}

static inline int fm_qmap_cmd_parse(const uint8_t *payload, uint32_t payload_len, FmQmapCmd *out)
{
    if (!payload || !out || payload_len < 8u) return -1;
    out->cmd_name = payload[0];
    out->cmd_type = (uint8_t)(payload[1] & 0x03u);
    out->transaction_id = fm_be32(payload + 4);
    return 0;
}

static inline uint32_t fm_qmap_cmd_ack(uint8_t *out, uint32_t capacity, uint8_t mux_id, const FmQmapCmd *req)
{
    if (!out || !req || capacity < 12u || req->cmd_type != FM_QMAP_CMD_TYPE_REQUEST) return 0u;
    uint8_t body[8];
    body[0] = req->cmd_name;
    body[1] = FM_QMAP_CMD_TYPE_ACK;
    body[2] = 0u;
    body[3] = 0u;
    fm_put_be32(body + 4, req->transaction_id);
    return fm_qmap_wrap(out, capacity, mux_id, body, 8u, 1);
}

typedef struct {
    uint8_t up;
    uint32_t address, mask, router, dns1, dns2, mtu;
} FmCellSession;

static inline uint16_t fm_ipv4_checksum(const uint8_t *data, uint32_t len)
{
    uint32_t sum = 0u;
    for (uint32_t idx = 0u; idx + 1u < len; idx += 2u) {
        sum += ((uint32_t)data[idx] << 8) | data[idx + 1u];
    }
    if (len & 1u) sum += (uint32_t)data[len - 1u] << 8;
    while (sum >> 16) sum = (sum & 0xffffu) + (sum >> 16);
    return (uint16_t)(~sum);
}

static inline void fm_cell_session_config(FmCellSession *sess, int up, uint32_t address, uint32_t dns1, uint32_t dns2,
    uint32_t mtu)
{
    if (!sess) return;
    const uint32_t mask = 0xffffff00u;
    uint32_t router = (address & mask) | 1u;
    if (router == address) router = (address & mask) | 2u;
    sess->address = address;
    sess->mask = mask;
    sess->router = router;
    sess->dns1 = dns1;
    sess->dns2 = dns2;
    sess->mtu = mtu;
    sess->up = (up && address != 0u) ? 1u : 0u;
}

static inline uint32_t fm_cell_arp_reply(const FmCellSession *sess, const uint8_t router_mac[6], const uint8_t *frame,
    uint32_t len, uint8_t *out, uint32_t capacity)
{
    if (!sess || !sess->up || !router_mac || !frame || !out || len < 42u || capacity < 42u) return 0u;
    if (fm_be16(frame + 12) != 0x0806u) return 0u;
    const uint8_t *arp = frame + 14;
    if (fm_be32(arp) != 0x00010800u || arp[4] != 6u || arp[5] != 4u || fm_be16(arp + 6) != 1u) return 0u;
    if (fm_be32(arp + 24) == sess->address) return 0u;
    for (uint32_t idx = 0u; idx < 6u; ++idx) {
        out[idx] = frame[6u + idx];
        out[6u + idx] = router_mac[idx];
        out[22u + idx] = router_mac[idx];
    }
    fm_put_be16(out + 12, 0x0806u);
    fm_put_be32(out + 14, 0x00010800u);
    out[18] = 6u;
    out[19] = 4u;
    fm_put_be16(out + 20, 2u);
    for (uint32_t idx = 0u; idx < 4u; ++idx) out[28u + idx] = arp[24u + idx];
    for (uint32_t idx = 0u; idx < 10u; ++idx) out[32u + idx] = arp[8u + idx];
    return 42u;
}

static inline int fm_cell_is_dhcp_request(const uint8_t *frame, uint32_t len, uint32_t *udp_off_out)
{
    if (!frame || len < 14u + 20u + 8u + 240u || fm_be16(frame + 12) != 0x0800u) return 0;
    const uint8_t *ip = frame + 14;
    if ((ip[0] >> 4) != 4u || ip[9] != 17u) return 0;
    const uint32_t ihl = 4u * (uint32_t)(ip[0] & 0x0fu);
    if (ihl < 20u || 14u + ihl + 8u + 240u > len) return 0;
    const uint8_t *udp = ip + ihl;
    if (fm_be16(udp + 2) != 67u) return 0;
    if (udp_off_out) *udp_off_out = 14u + ihl;
    return 1;
}

#define FM_DHCP_DISCOVER 1u
#define FM_DHCP_OFFER 2u
#define FM_DHCP_REQUEST 3u
#define FM_DHCP_ACK 5u
#define FM_DHCP_NAK 6u

static inline uint32_t fm_cell_dhcp_reply(const FmCellSession *sess, const uint8_t router_mac[6], const uint8_t *frame,
    uint32_t len, uint32_t udp_off, uint8_t *out, uint32_t capacity, uint8_t *kind_out)
{
    if (kind_out) *kind_out = 0u;
    if (!sess || !sess->up || !router_mac || !frame || !out) return 0u;
    if (udp_off > len || len - udp_off < 8u + 240u) return 0u;
    if (capacity < 14u + 20u + 8u + 320u) return 0u;
    const uint8_t *req = frame + udp_off + 8u;
    const uint32_t req_len = len - (udp_off + 8u);
    if (req[0] != 1u || req[1] != 1u || req[2] != 6u || fm_be32(req + 236) != 0x63825363u) return 0u;
    uint8_t msg_type = 0u;
    uint32_t wanted = 0u;
    for (uint32_t pos = 240u; pos < req_len && req[pos] != 255u;) {
        if (req[pos] == 0u) { pos += 1u; continue; }
        if (pos + 2u > req_len) break;
        const uint8_t opt = req[pos];
        const uint8_t olen = req[pos + 1u];
        if (olen > req_len - pos - 2u) break;
        if (opt == 53u && olen == 1u) msg_type = req[pos + 2u];
        else if (opt == 50u && olen == 4u) wanted = fm_be32(req + pos + 2u);
        pos += 2u + (uint32_t)olen;
    }
    if (msg_type != FM_DHCP_DISCOVER && msg_type != FM_DHCP_REQUEST) return 0u;
    const uint8_t kind = (msg_type == FM_DHCP_DISCOVER) ? FM_DHCP_OFFER :
        ((wanted != 0u && wanted != sess->address) ? FM_DHCP_NAK : FM_DHCP_ACK);
    for (uint32_t idx = 0u; idx < 14u + 20u + 8u + 320u; ++idx) out[idx] = 0u;
    uint8_t *ip = out + 14u;
    uint8_t *rudp = ip + 20u;
    uint8_t *bootp = rudp + 8u;
    bootp[0] = 2u; bootp[1] = 1u; bootp[2] = 6u;
    for (uint32_t idx = 0u; idx < 4u; ++idx) bootp[4u + idx] = req[4u + idx];
    bootp[10] = req[10]; bootp[11] = req[11];
    if (kind != FM_DHCP_NAK) {
        fm_put_be32(bootp + 16, sess->address);
        fm_put_be32(bootp + 20, sess->router);
    }
    for (uint32_t idx = 0u; idx < 16u; ++idx) bootp[28u + idx] = req[28u + idx];
    fm_put_be32(bootp + 236, 0x63825363u);
    uint32_t opt_pos = 240u;
    bootp[opt_pos++] = 53u; bootp[opt_pos++] = 1u; bootp[opt_pos++] = kind;
    bootp[opt_pos++] = 54u; bootp[opt_pos++] = 4u; fm_put_be32(bootp + opt_pos, sess->router); opt_pos += 4u;
    if (kind != FM_DHCP_NAK) {
        bootp[opt_pos++] = 51u; bootp[opt_pos++] = 4u; fm_put_be32(bootp + opt_pos, 86400u); opt_pos += 4u;
        bootp[opt_pos++] = 1u; bootp[opt_pos++] = 4u; fm_put_be32(bootp + opt_pos, sess->mask); opt_pos += 4u;
        bootp[opt_pos++] = 3u; bootp[opt_pos++] = 4u; fm_put_be32(bootp + opt_pos, sess->router); opt_pos += 4u;
        if (sess->dns1 != 0u || sess->dns2 != 0u) {
            bootp[opt_pos++] = 6u;
            bootp[opt_pos++] = (uint8_t)(4u * ((sess->dns1 != 0u ? 1u : 0u) + (sess->dns2 != 0u ? 1u : 0u)));
            if (sess->dns1 != 0u) { fm_put_be32(bootp + opt_pos, sess->dns1); opt_pos += 4u; }
            if (sess->dns2 != 0u) { fm_put_be32(bootp + opt_pos, sess->dns2); opt_pos += 4u; }
        }
        if (sess->mtu >= 576u && sess->mtu <= 1500u) {
            bootp[opt_pos++] = 26u; bootp[opt_pos++] = 2u; fm_put_be16(bootp + opt_pos, sess->mtu); opt_pos += 2u;
        }
    }
    bootp[opt_pos++] = 255u;
    const uint32_t bootp_size = opt_pos < 300u ? 300u : opt_pos;
    fm_put_be16(rudp, 67u);
    fm_put_be16(rudp + 2, 68u);
    fm_put_be16(rudp + 4, 8u + bootp_size);
    fm_put_be16(rudp + 6, 0u);
    ip[0] = 0x45u; ip[1] = 0u;
    fm_put_be16(ip + 2, 20u + 8u + bootp_size);
    ip[8] = 64u; ip[9] = 17u;
    fm_put_be32(ip + 12, sess->router);
    fm_put_be32(ip + 16, 0xffffffffu);
    fm_put_be16(ip + 10, fm_ipv4_checksum(ip, 20u));
    for (uint32_t idx = 0u; idx < 6u; ++idx) {
        out[idx] = req[28u + idx];
        out[6u + idx] = router_mac[idx];
    }
    fm_put_be16(out + 12, 0x0800u);
    if (kind_out) *kind_out = kind;
    return 14u + 20u + 8u + bootp_size;
}

/*
 * iOS 16.7.16 libsystem_sandbox.dylib _sandbox_check_common (__mac_syscall("Sandbox", 2, arg))
 * leading 32 bytes (verified against D22AP dyld_shared_cache 0x1d1af4bf8..0x1d1af4e88):
 *   +0x00 out_result_ptr (points to caller's uint64_t result word; non-zero -> return 1)
 *   +0x08 pid            (sign-extended int64_t)
 *   +0x10 operation      (NULL when querying "is this process sandboxed?")
 *   +0x18 filter_type    (0 = SANDBOX_FILTER_NONE)
 * MessagesBlastDoorService / MessagesAirlockService assert sandbox_check(getpid(), NULL, 0) == 1
 * in BlastDoor/Sandbox.swift (_bd_sandbox_check) on every incoming SMS and MobileSMS open.
 */
typedef struct {
    uint64_t out_result_ptr;
    int64_t  pid;
    uint64_t operation;
    uint64_t filter_type;
} FmSandboxCheckArgs;
_Static_assert(sizeof(FmSandboxCheckArgs) == 32u, "Sandbox op 2 prefix ABI size");

static inline int fm_cstr_has_prefix(const char *text, const char *prefix)
{
    if (!text || !prefix) return 0;
    for (size_t idx = 0u; prefix[idx] != '\0'; ++idx) {
        if (text[idx] != prefix[idx]) return 0;
    }
    return 1;
}

static inline int fm_sandbox_check_wants_sandboxed(const char *proc_name, const FmSandboxCheckArgs *args)
{
    if (!proc_name || !args) return 0;
    if (args->out_result_ptr == 0u || args->operation != 0u || args->filter_type != 0u) return 0;
    return fm_cstr_has_prefix(proc_name, "MessagesBlastDoo") ||
        fm_cstr_has_prefix(proc_name, "MessagesAirlock");
}
#endif
