#ifndef FUXI_BT_SK_H
#define FUXI_BT_SK_H

/*
 * iOS bluetoothd 的私有傳輸（fuxi,bt=5）：kernel control「com.apple.uart.sk.bluetooth」＋kernel pipe Skywalk
 * nexus，H4 位元組流經 TX／RX 兩個 8 KB FIFO 與 UART 擁有者（fuxi_bt_xnu.c 的常駐泵）交換。
 * 改寫自 QcaBluetooth src/qbt_transport.c（APSL-2.0，Copyright (c) 2026 daimile2 and contributors）；
 * sysctl 名稱 kern.qbt_boot／qbt_power_on／qbt_firmware 保持相同，讓它的 BlueTool 替身可直接沿用。
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct fuxi_bt_sk_stats {
    uint64_t rx_bytes;      /* 交給 RX FIFO 的 bytes */
    uint64_t rx_dropped;    /* 沒有 RX 環時丟掉的 bytes（以 H4 封包為單位） */
    uint64_t tx_bytes;      /* UART 擁有者取走的 bytes */
    uint64_t rx_syncs;      /* sync_rx 送出的槽 */
    uint64_t tx_syncs;      /* sync_tx 收下的槽 */
    uint32_t rx_len;
    uint32_t tx_len;
    uint32_t connects;
    uint32_t channels;
    bool connected;
    bool rings;
    int boot;
};

#ifdef __cplusplus
extern "C" {
#endif

void fuxi_bt_sk_start(void (*wake)(void), bool probe);
/* UART 擁有者回報上電結果。成功時註冊 kernel control，失敗時回傳 errno。 */
int fuxi_bt_sk_boot_result(int error, const char *firmware);
/* 控制器 → 主機：回傳收下的 bytes。start 表示 data 從 H4 封包開頭起算：此時沒有 RX 環就丟掉整個封包，
 * 通道在封包中間接上也要等下一個封包開頭才開始收。丟棄時回 size。 */
size_t fuxi_bt_sk_receive(const uint8_t *data, size_t size, bool start);
/* 主機 → 控制器：最多取 capacity bytes。 */
size_t fuxi_bt_sk_tx_take(uint8_t *data, size_t capacity);
void fuxi_bt_sk_get_stats(struct fuxi_bt_sk_stats *stats);
/* 已接受的連線次數（無鎖讀取），常駐迴圈用來偵測 bluetoothd 重新連線。 */
uint32_t fuxi_bt_sk_connects(void);

#ifdef __cplusplus
}
#endif

#endif
