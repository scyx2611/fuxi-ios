<div align="center">

# Xiaomi 13 (`fuxi` / SM8550) iOS / XNU Bring-up

**小米 13 的 iOS／XNU 實驗移植專案，涵蓋核心適配、驅動、iOS userland 相容層、引導工具、雙系統選單與建置驗證流程。**

[![Device](https://img.shields.io/badge/Device-Xiaomi%2013%20(fuxi)-orange?style=flat-square)](#裝置與操作原則)
[![SoC](https://img.shields.io/badge/SoC-Snapdragon%208%20Gen%202%20(SM8550)-blue?style=flat-square)](#裝置與操作原則)
[![iOS](https://img.shields.io/badge/iOS-16.7.16%20(20H392)-blue?style=flat-square)](#目前進度)
[![Status](https://img.shields.io/badge/Status-Experimental-yellow?style=flat-square)](#倉庫狀態)

[倉庫狀態](#倉庫狀態) · [目前進度](#目前進度) · [倉庫內容](#倉庫內容) · [裝置與操作原則](#裝置與操作原則) · [外部資產與授權](#外部資產與授權)

</div>

---

目前主要開發路線為 **iOS 16.7.16（20H392）／XNU 8796.141.3**。

> 本專案仍在實驗階段，尚不適合作為日常手機系統，也不是可直接刷入的完整 ROM。

## 倉庫狀態

**2026-11-18：原始碼、補丁、測試、建置腳本與引導工具已收錄。**

核心基底（zgl97/ios-fuxi）、loader 原始碼樹（bangprovn/ios-fuxi）與 `mkbootimg.py` 都已收進本倉庫，本機建置不必再向上游下載。Apple 的 IPSW／RootFS、高通韌體、iOS SDK 與工具鏈仍須自行準備，詳見[來源與目錄說明](docs/source-layout.zh-TW.md)。

2026-11-18 已從本倉庫的 clone 在 WSL 重編 9g 核心（38 項主機測試通過）與 `libfuxi-sb.dylib`；這兩份產物沒有另外上機。下表是開發工作區的實機驗證進度。

## 目前進度

### 顯示與輸入

| 項目 | 現狀 | 查驗數據與限制 |
| --- | --- | --- |
| 系統啟動 | 可進入 iOS 桌面 | iOS 16.7.16／XNU 8796.141.3；近期本機 9g 核心可進 SpringBoard。[E1](docs/status-20261118.zh-TW.md#e1) |
| 螢幕顯示 | 可顯示桌面與 App | 實體 framebuffer **1080×2400**，stride **4320 bytes**。[E1](docs/status-20261118.zh-TW.md#e1) |
| 螢幕更新時序 | 120 Hz 模式已啟用 | 最新已查驗開機的 TE 為 **120.7–120.9 Hz**；面板時序與實際 shown-FPS 分開記錄。[查驗結果](docs/cpu-gpu-final-20261118.zh-TW.md) |
| 觸控 | 可點擊、滑動 | 同趟記錄 **245 次 down／245 次 up、241 段完成接觸**；這些是驅動紀錄，不是每次 UIKit 操作成功率。[E1](docs/status-20261118.zh-TW.md#e1) |
| 觸控採樣／回報率 | **實測典型回報約 120 Hz** | 99 段較長單指接觸的回報間隔中位數 **8.333 ms**；驅動接觸時輪詢 **4 ms＝250 Hz**、閒置時 **8 ms＝125 Hz**。硬體內部掃描率尚無獨立量測。[E1](docs/status-20261118.zh-TW.md#e1) |
| 多點觸控 | 已有雙指回報 | 同趟最大同時接觸數 **2**；程式上限 10 指，尚未據此宣稱十指手勢通過。[E1](docs/status-20261118.zh-TW.md#e1) |
| 實體按鍵 | 電源鍵事件可讀 | 電源／音量鍵驅動輪詢 **20 ms＝50 Hz**；最新回退趟可見電源按下／放開及螢幕背光切換，音量完整操作另待驗收。[E2](docs/status-20261118.zh-TW.md#e2) |
| 螢幕亮度 | 背光控制路徑已接通 | 連拉 8 次滑桿的歷史測試中，brightness 請求序號達 **3584**，該 console 未見 no clean completion；持續使用時的實際面板亮度仍需人眼驗收。[E10](docs/status-20261118.zh-TW.md#e10) |
| 狀態欄 | 已調整位置 | 既有量測時間／電池中心約 **87.5 px**；實體鏡頭孔精確對齊尚待確認。[E16](docs/status-20261118.zh-TW.md#e16) |

### CPU 調度與頻率

以下為最後已保存並查驗的本機 `kernel-local-wifiux2-9g-artifact` 暫時開機結果；日常 `boot_b` 目前內容未重新讀取。

| 項目 | 最終現狀 | 數據與限制 |
| --- | --- | --- |
| CPU 啟用 | XNU 使用 **7 顆** | 另 1 顆 A510 供 loader USB monitor；Prime 已啟動，邏輯 CPU 6＝MPIDR 0x700。 |
| 執行緒調度 | **推薦核心 0x7c 已生效** | 同質 clutch＋邏輯 **2–6（Gold／Prime）**推薦集合；小核仍處理中斷與綁定執行緒，完整 AMP／Edge 調度尚未移植。 |
| CPU 動態調頻 | **`fuxiperf=2` 已生效** | 有實際 boosted／cool／idle 紀錄；輸入 boost hold **1500 ms**，受熱條件與守門約束。 |
| CPU 狀態回讀 | Gold 最高 **2.323 GHz**／Prime **2.592 GHz** | 為 EPSS 狀態暫存器換算的窗口峰值，不是持續頻率；高檔 LUT 投票另為 **2.3232／2.3424 GHz**，兩者差異原因待確認。 |
| 閒置降頻 | **1 秒閒置策略已生效** | Silver／Gold／Prime 的 idle LUT 為 **307.2／499.2／595.2 MHz**；已有 Gold 499／Prime 595 MHz 回讀。 |
| CPU 熱控制 | 已啟用 | hot **66°C**、limit **72°C**、resume **62°C**；最新 CPU governor 統計最高 **70.6°C**，策略門檻不等於機身溫度保證。 |
| 繪圖小物件快取 | **`fuxitcache=1` 已生效** | 明確記錄 `FUXI TCACHE: on`；16–512 bytes，每個尺寸級別每執行緒 64 blocks。 |

### GPU 與顯示管線

| 項目 | 最終現狀 | 數據與限制 |
| --- | --- | --- |
| GPU／Metal | **Adreno 740 GPU 桌面可用** | NativeMetal／bridge → metal2vulkan → Turnip → NativeAdreno ABI；Vulkan 為內部後端，不推定所有 Metal 功能相容。 |
| GPU 資源匯入 | ABI v2／HostImport 16K 已開啟 | backboardd 已開啟 NativeAdrenoGpuPublished。 |
| GPU 頻率策略 | **680／220 MHz 投票往返已觀察** | `fuxigpulevel=24`＝cap 8＋governor；無新 doorbell **100 ms** 後降到 floor。最新一筆 DCVS 為 **64 次升／64 次降、fails=0**，不是整趟總數。 |
| GPU 實際時脈 | 尚無直接量測值 | console 為 `core_measured_khz=unavailable`；680／220 MHz 是 HFI 投票值。 |
| 記憶體頻寬 | 高低檔頻寬請求已配置 | **12449218／2136718 kB/s**，不是實測 DDR 吞吐。 |
| Turnip 設定 | **`fuxitudebug=nocb` 生效** | 關閉 concurrent binning；目前未啟用 `fuxihyb=8` 或獨立 Hybrid Turnip 選擇。 |
| swap／VSync | **swap wait=0／TE offset=6000 us** | 額外 swap hook 等待關閉，核心確認 6 ms offset；保留 fence、writer 與 scanout 保護。 |
| 顯示緩衝 | double／triple buffering 啟用 | 實機有三緩衝 flips；面板 TE **120.7–120.9 Hz**。 |
| GPU 出幀 | 最新摘要 **9600 幀** | **9594 GPU＋6 CPU**；15 筆 GPU presents 的 failed 均為 0。 |
| scanout 複製 | 摘要平均 **4.581 ms**／最大 **29.433 ms** | 這是複製路徑耗時，並非整幀端到端延遲。 |
| 持續畫面 FPS | 尚未完成統一負載驗收 | 不以 120 Hz 面板時序代替 shown-FPS；紫色顯示異常與部分網站／App 相容性仍待改善。 |

[CPU／GPU 最終配置、有效參數與查驗來源](docs/cpu-gpu-final-20261118.zh-TW.md)。

### 網路與通訊

| 項目 | 現狀 | 查驗數據與限制 |
| --- | --- | --- |
| Wi-Fi 2.4 GHz | 已驗證連線 | 已手動完成 **WPA2-PSK／2412 MHz** 加入、四向握手及 DHCP。[E3](docs/status-20261118.zh-TW.md#e3) |
| Wi-Fi 5 GHz | **已驗證連線** | 使用者確認，原始 console 亦記錄 **joined at 5200 MHz、join finished 0**。[E1](docs/status-20261118.zh-TW.md#e1) [E14](docs/status-20261118.zh-TW.md#e14) |
| 實際上網／Safari | **已驗證上網** | 使用者已實際開啟網頁；不再列為「僅 DHCP 通過」。這不代表所有網站繪圖及長時間網頁壓力測試通過。非 DRM 網頁影片可播放（apple.com 首頁影片）；YouTube 需要 FairPlay 授權（`lskdd` 回 `-42660`），這個移植無法取得裝置憑證，無法播放。[E14](docs/status-20261118.zh-TW.md#e14) · [K-077](records/items/K-kernel.zh-TW.md) |
| Wi-Fi 自動加入 | 已成功實測 | Claude 最新對話記錄單次開機自動加入約 **0.44 秒**，使用者也曾確認自動連線；跨多次重啟的成功率未統計。[E14](docs/status-20261118.zh-TW.md#e14) |
| Wi-Fi 掃描 | 可掃描，仍偏慢 | 00:31 那趟：2.4 GHz 13 頻道約 **0.94 秒**、5 GHz 27 頻道約 **7.20 秒**、全頻約 **16.11 秒**；結果取回約 1–7 ms。不同掃描範圍分開比較。[E1](docs/status-20261118.zh-TW.md#e1) [E14](docs/status-20261118.zh-TW.md#e14) |
| Wi-Fi 吞吐量 | 可傳輸，效能未達目標 | 使用者先前實測約 **100 Mbps**，後續確認有提升但未提供新數值，仍未達對照手機約 300 Mbps；AP 選擇器的 648–720 Mbps 是估計值，不能當實際網速。[E14](docs/status-20261118.zh-TW.md#e14) |
| Wi-Fi 開關／重連 | 可切換，仍有偶發失敗 | 最新回退趟底層關閉 **2 ms**、開啟 **18 ms**；先出現一次 join=80，後續成功加入 5200 MHz。底層耗時不等於 UI 操作延遲。[E2](docs/status-20261118.zh-TW.md#e2) |
| 藍牙 | 耳機可用；關閉藍牙不再閃退；已進日常開機 | 原廠 `bluetoothd` 直接驅動 QCA 控制器（SoC **0x40170200**）。UART 升到 **3.2 Mbaud** 後韌體載入約 **1.4 秒**，Apple 耳機 A2DP（AAC-ELD 48 kHz）播放正常，使用者已確認；耳機出盒會自動回連並完成加密。從設定頁配對 iPhone 不成功：對端沒開個人熱點時沒有可連的傳統藍牙服務（NAP `ServiceAvailability=0`），這是原生行為。關閉藍牙的閃退已修正：`bluetoothd` 的 H4 傳輸表少一個終止函式，由注入的 `libfuxi-sb.dylib` 在 ADT `fuxi,bt-fix` 開啟時補上；設定頁關→開兩趟皆未閃退，關開後電腦端 SDP 仍查得到 5 項服務。2026-11-20 起日常 boot_a 已帶原生藍牙與這個修補（[B-013](records/items/B-boot.zh-TW.md)）；同一份 iOS payload 暫時開機時藍牙就緒、`bluetoothd` 未閃退，常駐選單進 iOS 後的日常實測待做。[U-023](records/items/U-userland.zh-TW.md) · [2026-11-19／20 趟次](records/journal/hw000-runs.zh-TW.md) |
| 行動網路 | 尚未驗收 | 沒有通話、SMS 或 4G／5G 數據連線通過紀錄。[E15](docs/status-20261118.zh-TW.md#e15) |
| 個人熱點 | 尚未驗收 | Wi-Fi station 連線成功不代表 AP／網路分享功能可用。[E15](docs/status-20261118.zh-TW.md#e15) |

### 電源、周邊與系統功能

| 項目 | 現狀 | 查驗數據與限制 |
| --- | --- | --- |
| 可用記憶體 | 12 GB 實機的 9g 配置已上機 | 已核對 XNU usable **9184 MiB**；5g／9g 是配置名稱，不是不同實體 RAM SKU。[E11](docs/status-20261118.zh-TW.md#e11) |
| 電量讀取 | ADSP 電量路徑可用 | ADSP＋WLAN 聯合測試 **140 輪、0 timeout**，可讀 SOC、電壓與電流；此趟長時間接近滿電，不當作全電量範圍準確度驗收。[E4](docs/status-20261118.zh-TW.md#e4) |
| 充電狀態 | 拔插線切換已驗證 | 原始紀錄可見拔線 external=0／charging=0，插回 external=1／charging=1；快充最高功率與充滿曲線尚未量測。[E4](docs/status-20261118.zh-TW.md#e4) |
| 電池歷史／續航 | 尚未完成驗收 | 即時電量可讀不代表設定中的歷史圖表可用；尚無完整放電續航測試。[E15](docs/status-20261118.zh-TW.md#e15) |
| 手電筒 | 五檔操作已確認 | **0 關閉＋1–4 亮度**；手測紀錄有 **113 次 level transition**，使用者確認常亮及調光不閃。[E5](docs/status-20261118.zh-TW.md#e5) |
| 充電震動 | 已確認有震動 | 兩次原生充電事件的馬達啟動時間為 **400.089／402.395 ms**，fault=0、stop-rc=0；使用者已確認，手感仍需調整。[E7](docs/status-20261118.zh-TW.md#e7) |
| 短震動／取消 | 核心時序測試通過 | 15 ms Minitap 實測 **15.530 ms**；2000 ms 請求提前取消，實際啟動 **120.850 ms**。不代表一般 UI 觸感已可用。[E6](docs/status-20261118.zh-TW.md#e6) |
| 介面交互震動 | 尚未通過 | 最新 v7 console 中 **11 筆 CoreHaptics error=-4810**；不能用充電震動成功替代此項。[E1](docs/status-20261118.zh-TW.md#e1) |
| 喇叭／音訊 | DSP 框架就緒，出聲未驗收 | **第 2 次查詢取得 SPF state=1**；尚無完整 PCM 播放、功放播放圖與 iOS 音訊裝置驗收。[E9](docs/status-20261118.zh-TW.md#e9) |
| 麥克風 | 尚未驗收 | 尚無錄音、輸入電平或錄放音通過紀錄。[E15](docs/status-20261118.zh-TW.md#e15) |
| 相機 | 尚未驗收 | 尚無預覽、拍照或錄影通過紀錄。[E15](docs/status-20261118.zh-TW.md#e15) |
| 感測器 | 尚未驗收 | 尚無自動旋轉、指南針或自動亮度的完整功能驗收。[E15](docs/status-20261118.zh-TW.md#e15) |
| 影片硬體解碼 | 尚未驗收 | 尚無 Iris VPU／VideoToolbox 硬解播放通過紀錄。[E15](docs/status-20261118.zh-TW.md#e15) |
| 日期與時間 | 校時與台北時區已有驗證 | ADT RTC offset 已生效，UI 曾確認顯示台北時間；完全斷電或 RTC 重設後需重新校準，長期走時未驗收。[E11](docs/status-20261118.zh-TW.md#e11) |
| 第三方 App | 已有安裝及啟動案例 | 酷安 **16.2.5** 重簽 **28 個 Mach-O** 後可執行；登入與所有 app group 功能未驗收，不能推定所有 IPA 相容。[E14](docs/status-20261118.zh-TW.md#e14) |
| 雙系統圖形選單 | 已常駐 boot_a | 2026-11-20 起 boot_a 為 `menu-daily-bt-20261120`（10 秒無按鍵自動進 Android，按音量鍵可選 iOS），寫入讀回與 Android 回程已驗，這版的 iOS 路徑待手動驗證。更早的 `menu-launching-20261118` 已驗過 Android、iOS、Recovery 三條路徑；開 iOS＝重開後在選單選 iOS。其他選項的 Fastboot／重新啟動／關機、完全關機後冷開機、ABL 重試與回退**尚未驗收**；b 槽選 Android 仍會循環。[E13](docs/status-20261118.zh-TW.md#e13) |

### 開發工具

| 項目 | 現狀 | 查驗數據與限制 |
| --- | --- | --- |
| 本機核心建置 | Windows＋WSL 可建置 | 11-16 保留樣本：快取來源下完整重編 **70 秒**、無修改增量 **21 秒**；該輪 **33 個 workflow 主機命令**通過。不同補丁版本耗時另計。[E12](docs/status-20261118.zh-TW.md#e12) |
| 本機 iOS 函式庫建置 | 可建置且已有載入實證 | 首份本機 dylib 腳本內 **3 秒**，**28 頁**簽署雜湊校驗；11-17 20:44 那趟已有 runtime 載入紀錄。[E12](docs/status-20261118.zh-TW.md#e12) |

[查驗方法、原始紀錄索引與限制說明](docs/status-20261118.zh-TW.md)。數值屬指定測試樣本，不是所有版本、所有情境的保證。

## 倉庫內容

| 路徑 | 內容 |
| --- | --- |
| `patches/` | 核心、Metal、Turnip 等補丁。本機與 CI 建置依 `.github/workflows/kernel-adsp-battery.yml` 的順序，把其中 18 份套到核心基底上 |
| `tests/`、`tools/`、`userland/` | 主機測試、ath12k 與震動／手電筒等工具，以及 iOS 端 `libfuxi-sb.dylib`（`userland/fuxi-shape/`） |
| `.github/workflows/` | 既有 CI 工作流程，未改觸發條件（見[來源與目錄說明](docs/source-layout.zh-TW.md#ci)） |
| `work/adsp-battery/ksrc/` | 核心原始碼樹：上游 zgl97/ios-fuxi `979e2f08` 加本機修改（XNU 驅動、loader、ADT 產生器） |
| `ios-fuxi/` | loader 與工具原始碼樹：上游 bangprovn/ios-fuxi `27f6a31` 加本機修改 |
| `dualboot/` | 雙系統圖形選單、安裝與救援工具（說明見 `dualboot/README.zh-TW.md`） |
| `work/localbuild/` | Windows＋WSL 本機核心與 dylib 建置腳本 |
| `work/`（其他） | loader 產生器、引導鏈、手機啟動器、iOS 根映像整備與檢查工具 |
| `uefi-fuxi/Resources/Scripts/mkbootimg.py` | AOSP `mkbootimg.py`（Apache-2.0，未修改） |
| `upstream/EXPORT-SOURCES.txt` | 各上游的釘選版本、tree id、本機修改清單與去識別化紀錄 |
| `docs/` | 功能查驗、CPU／GPU 配置與[來源與目錄說明](docs/source-layout.zh-TW.md) |

本機建置 9g 核心（需先在 WSL 裝好工具鏈，見來源與目錄說明）：

```powershell
work\localbuild\build-kernel.ps1 -Ram 9g -Name kernel-local-<實驗名>-9g-artifact -Tests
```

目前不提供一般使用者的刷機或部署步驟。

## 裝置與操作原則

目前開發及驗證以小米 13 `fuxi` 的 **12 GB RAM 實機**為主，不推定適用其他記憶體、儲存容量或裝置型號。

- 手機操作統一經由具備鎖檔檢查、紀錄保存與回復流程的啟動器。
- 寫入前核對手機現況，寫入後讀回驗證，保留使用者資料。
- 暫時開機與持久安裝分開說明；個別測試成功不等於可直接安裝。
- 整理、建置與主機測試流程不應自動操作手機。

## 外部資產與授權

本倉庫不提供 Apple IPSW、DMG、RootFS、專有系統二進位、高通韌體、iOS SDK、個人手機備份、金鑰或 Wi-Fi 憑證。需要的外部資產須由使用者自行合法取得或從自己的裝置擷取。

本專案自己撰寫的程式碼以 [MIT](LICENSE) 授權。對 Apple XNU 原始檔的修改依 APSL 2.0；其他上游元件維持各自的授權，原有的 LICENSE、NOTICE 與檔頭都保留。

收錄的 bangprovn/ios-fuxi 與 zgl97/ios-fuxi 上游沒有宣告授權，其作者保有原有權利；MIT 只涵蓋本專案新增的部分。權利人如要求移除，會照辦。完整清單見 [THIRD_PARTY.md](THIRD_PARTY.md)。
