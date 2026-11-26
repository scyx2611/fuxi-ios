<div align="center">

# Xiaomi 13 (`fuxi` / SM8550) iOS / XNU Bring-up

**小米 13 的 iOS／XNU 實驗移植專案，涵蓋核心適配、驅動、iOS userland 相容層、引導工具、雙系統選單與建置驗證流程。**

[![Device](https://img.shields.io/badge/Device-Xiaomi%2013%20(fuxi)-orange?style=flat-square)](#裝置與操作原則)
[![SoC](https://img.shields.io/badge/SoC-Snapdragon%208%20Gen%202%20(SM8550)-blue?style=flat-square)](#裝置與操作原則)
[![iOS](https://img.shields.io/badge/iOS-16.7.16%20(20H392)-blue?style=flat-square)](#目前進度)
[![Status](https://img.shields.io/badge/Status-B--022%20Experimental-yellow?style=flat-square)](#倉庫狀態)
[![Apps](https://img.shields.io/badge/Apps-Option%20B%20(cryptid%3D0)%20Only-lightgrey?style=flat-square)](#目前進度)

[倉庫狀態](#倉庫狀態) · [目前進度](#目前進度) · [倉庫內容](#倉庫內容) · [裝置與操作原則](#裝置與操作原則) · [外部資產與授權](#外部資產與授權)

</div>

---

目前主要開發路線為 **iOS 16.7.16（20H392）／XNU 8796.141.3**。

> 本專案仍在實驗階段，尚不適合作為日常手機系統，也不是可直接刷入的完整 ROM。

## 倉庫狀態

**2026-11-26（Scheme A `B-022` 常駐里程碑）：29 份補丁主分支堆疊已同步並完成離線建置與實機升級驗證。**

核心與測試已同步至 `ksrc`（`a00205b`）與 `harness`（`2ce4028`）的 **29 份補丁堆疊**（目錄共 43 份補丁、依 workflow 套用 29 份；**63/63** 主機測試命令 PASS、ISA 檢查 `2,706,839` 條指令／`forbidden 0`），並以 WSL 本機工具鏈編譯為 **`local-wsl (not a CI artifact)`**（`kernel-local-b022-daily-9g-artifact`，`cd4bf328...`），搭配 `native-b022-daily-1126-loader`（`payload 43eff0a6...`／`boot.img 00c4e7f7...`、`private.adt 71,764 B`、`cmdline 602 B`）、沿用 `sb-local-daily-b021-clean-artifact`（`e8cfaa01...`）與 `menu-daily-b022-20261126`（PIN `3f021363...`，`68,423,680 B`、通過 `454` 項斷言、`SAVED_FDT_REPLAY_OK`、`BASE_RELINK_IDENTICAL`）。

- **Stage E-2 實機煙霧與常駐 `boot_a` 狀態**：實機煙霧 `#470`（`2026-11-26 19:04–19:07 CST`，`boot.log` `9,062` 行 `cfe2698e024b...`、`panic=0`、`REAL_FAULTS=0`、`DEV_LOST=0`、`BoAlloc slots refused=0`、`backboardd`/`SpringBoard` `CRASH=0`、`dropped=0`、`mtl_failures_last=0`、`thermal_cuts=0`、`SWEET FB: vsync te-locked=600 fallback=0 period_us 8251`（`121.2 Hz`、`p95=4.754 ms`、`gpu_ms=3.041`、`max_temp_dc=521`、`host_tsens_max=50.9°C`）、`atx-fix` 已安裝、`ath12k` stage 4（11 scans / 15 BSS / err 0）、BT／ADSP（`100%`、`4419 mV`）／RTC `persist-ats_2`／FlashlightModule／GNSS `QMI_LOC`／CS35L41／HAP525 全數正常）、RAM 選單交接（`daily-b022-ram-android-20261126`：`result="ok"`、`ANDROID_CHAIN_HANDOFF_OK 0xa9410000`、`candidate_sha256=3f021363...`、`boot_partition_writes=0`、`slot_changes=0`）、常駐 `boot_a` 升級（`2026-11-26 19:13–19:21 CST`，`boot_a` SHA-256 `0e1e496b6e92b83160e8c5d7b6604589f270c6060ed5b687e64b2579922ba9d9`；首趟 `persistent-slot-a-upgrade-daily-b022-20261126` 遇本機 `adb` TCP 5037 daemon 瞬斷安全回退 `B-021` `6b88c3e5...`，於 `dualboot/phone-check.py` 補上狹義重試並登記 `-r2` 後，`-r2` 達成 `INSTALLED_ANDROID_VERIFIED`，槽位 `_a`、`B-021` 回退映像已保存、5 個保護分區 SHA-256 零異動、`lock_state="released"`）。

> **重要邊界與未併入隔離實驗區隔**：
> 1. 日常版沿用 `Stick-14` `FuxiNativeMetal` 並**維持排除 Apple 地圖（`--gpu-clients none`）**；**商業 App Store（`cryptid=1`／R4）受非 Apple SoC 硬體信任根限制明確不支援（`SAPSign -42036 -> http=401`）**，專案轉向 **Option B（`cryptid=0`）** 無 DRM 原生生態。
> 2. Maps 隔離實驗（`M4-3 v3` `#461`、平行 Phase 11 `#464`/`#465`、Phase 12 `#466`、Phase 12B `#467`/`#468` 回退，以及 `#469` 暴露 `M4-3 v3` `backboardd` 4096 BO 洩漏並已回退至 `Stick-14`）與進行中的 `B-023` 藍圖**均未併入 `B-022`**。

核心基底（`zgl97/ios-fuxi`）、loader 原始碼樹（`bangprovn/ios-fuxi`）與 `mkbootimg.py` 都已收進本倉庫，本機建置不必再向上游下載；Apple IPSW／RootFS、高通韌體、iOS SDK 與工具鏈仍須自行準備。快速導覽：[來源與目錄說明](docs/source-layout.zh-TW.md) · [紀錄庫索引](records/README.zh-TW.md) · [2026-11-26 功能查驗](docs/status-20261126.zh-TW.md) · [2026-11-25 功能查驗](docs/status-20261125.zh-TW.md)。

## 目前進度

以下進度嚴格依據 [2026-11-26 功能查驗](docs/status-20261126.zh-TW.md) 與 [2026-11-25 功能查驗](docs/status-20261125.zh-TW.md)（接續 [2026-11-18 查驗](docs/status-20261118.zh-TW.md) 與 [CPU／GPU 配置](docs/cpu-gpu-final-20261118.zh-TW.md)）及 `records/items/*` 實測紀錄整理；「已驗證」僅限於記載的版本與情境，未獨立驗收情境不作推定。

### 顯示、GPU 與輸入

| 項目 | 現狀 | 查驗數據與限制 |
| --- | --- | --- |
| 系統啟動與雙系統選單 | 已升級常駐 `boot_a`（`B-022`） | iOS 16.7.16／XNU 8796.141.3；`menu-daily-b022-20261126`（PIN `3f021363...`、預設 10 秒進 Android、音量鍵選 iOS，454 項斷言、12 GiB 機型使用 9 GiB RAM 佈局）；Stage E-2 `boot_a`：`0e1e496b6e92b83160e8c5d7b6604589f270c6060ed5b687e64b2579922ba9d9`（首趟遇本機 `adb` 5037 瞬斷安全回退，補重試後 `-r2` 達成 `INSTALLED_ANDROID_VERIFIED`，`B-021` 回退映像已保存、5 個保護分區 SHA-256 零異動、`lock_state="released"`）。[E34](docs/status-20261126.zh-TW.md#e34) · [E35](docs/status-20261126.zh-TW.md#e35) |
| 螢幕顯示與去紫 | `1125×2500` 等比全寬＋系統模糊去紫 | `--display-fit aspect` 縮放填滿 `1080×2400` 面板；狀態欄 `360×86`、`volhud=238128`；`--atx-fix 1` 與 `--display-gamut sRGB` 修復系統模糊發紫（`0 / 1,296,000` 洋紅像素）。不可改寫磁碟 `.atx` 為大寫 `'ASTC'`。[E19](docs/status-20261125.zh-TW.md#e19) · [G-050](records/items/G-gpu.zh-TW.md#g-050-在-1080-寬面板顯示完整-1125-寬畫面並對齊觸控) · [U-026](records/items/U-userland.zh-TW.md#u-026-springboardpreferences-偽-pre-twiddled-astc-atx-cpu-解碼改走線性解碼修復系統模糊背景區域發紫fuxiatx-fix) |
| Metal GPU 與系統 App 分級 | Class 0（`1280 MiB`）＋系統 App 分級驗證 | 沿用 `Stick-14` `FuxiNativeMetal`、`fuxi,gpu-bb-recover=3`、`probe_0x4e`（`a8d64c47...` 消除截圖 USB 重連）；`#449–#453`/`#457`/`#458`（`Notes`、`Photos`、`Camera`、`Safari WebKit.GPU`、`Music`、`Weather`、`Stocks`、`MobileTimer` 過關，`Measure` 進介面 0 crash／黑背景 AR）。**Class 1（Apple Maps）續排除（`--gpu-clients none`）**。[E31](docs/status-20261126.zh-TW.md#e31) · [E33](docs/status-20261126.zh-TW.md#e33) |
| 120 Hz Wave 2 時序 | 底層時序解鎖（TE `~121 Hz`，局部 `120 Hz`） | Wave 2（`gpu-locksplit=1`/`nopoll=1`/`gpu-irqslice=8000`）將 ABI `lock_` 最大等待由 `76,600 us` 壓至 `0–300 us`；局部滑動可達 `120.13–120.21 Hz`（`#442`/`#447`），`B-022` 全棧實測（`#454`/`#456`，冷機 `33.4°C` `Stick-14`）受 ProMotion VRR 降至 `29.90–30.20 Hz` 與重幀編碼長尾影響，尚未達成全場景穩 120 FPS。[E29](docs/status-20261126.zh-TW.md#e29) |
| 觸控、按鍵與背光 | 已驗證可用 | SweetHID 單指典型回報約 `120 Hz`（中位間隔 `8.333 ms`）、電源／音量鍵 `20 ms` 輪詢、DSI 背光控制。[E1](docs/status-20261118.zh-TW.md#e1) |

### CPU 調度與頻率

沿用 9g 7 核 XNU 調度架構（第 8 顆供 loader USB monitor）：推薦核心 `0x7c`（Gold/Prime）、`fuxiperf=2`（boost hold `1500 ms`、1 秒閒置 LUT `307.2／499.2／595.2 MHz`、熱控 hot 66°C / limit 72°C）與 `fuxitcache=1`。詳見 [CPU／GPU 最終配置](docs/cpu-gpu-final-20261118.zh-TW.md)。

### 網路、通訊、定位與 Option B 無 DRM

| 項目 | 現狀 | 查驗數據與限制 |
| --- | --- | --- |
| Wi-Fi `2.4 / 5 GHz` 與加速 | 已驗證連線與上網 | WCN7850 + `ath12k` 掃描快取與連線加速（`fuxiwlancache=1`、`fuxi,wifi-ux=8191`、`join finished 0`）；非 DRM 網頁影片可播，FairPlay 受限。[E24](docs/status-20261125.zh-TW.md#e24) · [K-083](records/items/K-kernel.zh-TW.md#k-083-wi-fi-掃描快取與連線加速ath12k-join-speed) |
| 個人熱點（SoftAP） | `5 GHz 80 MHz` Wi-Fi 6 已驗證 | `fuxi,wifi-ap=15`（`ap1` `ch=149` + Apple 雙圓圈 IE + `hostapd` 退場同步 + `bootpd` 綠標恢復）：`4/4 started ap1 / hostapd / bootpd`、雙向零丟包。[E20](docs/status-20261125.zh-TW.md#e20) · [K-079](records/items/K-kernel.zh-TW.md#k-079-wcn7850-原生-softap-個人熱點ap15-ghz-80-mhz-wi-fi-6apple-熱點-iehostapd-退場同步與橋接就緒-dhcp綠標恢復) |
| 藍牙 | 常駐 HCI 就緒 | 原廠 `bluetoothd` 驅動 QCA `0x40170200`（`baud=3200000`、`ibs=1`、`desync=0`），A2DP 可播與關藍牙防閃退。[K-077](records/items/K-kernel.zh-TW.md#k-077-wcn7850-uartsk-與-skywalk-常駐收送) · [U-023](records/items/U-userland.zh-TW.md#u-023-bluetoothd-相容修補關藍牙閃退與單實例-ble-廣播fuxibt-fix) |
| 基頻行動數據與 IMS | 數據介面與 IMS 註冊就緒 | `CBD_WAIT_WDA` 等候修復後，`WDA error 0x0`、`pdp_ip0..2`（`qmap_bad=0`）與 `IMS Voice -> true` 已驗證；**真實可用 SIM 卡 VoLTE 通話實撥尚未測試**。[E20](docs/status-20261125.zh-TW.md#e20) |
| GPS／GNSS 定位 | `QMI_LOC` + PDS 通道就緒 | `fuxi,modem-gnss=3` 實測 `COACHZ gnss: mode=0x3`（`pds`/`loc` 雙向計數正常）。實測均為室內，**戶外實際鎖星與地圖聯調尚未驗收**。[E24](docs/status-20261125.zh-TW.md#e24) · [K-082](records/items/K-kernel.zh-TW.md#k-082-gpsgnss-定位通道qmi_loc-與-pds-橋接) |
| Option B（`cryptid=0`）無 DRM | 核心 `0131`–`0133` ＋ `0xB0` 雙端實機通過 | 商業 App Store（`cryptid=1`/R4）遇 `SAPSign -42036 -> http=401` 硬體邊界；`0131`–`0133` + `0080`（日常預設 opt-out）於 `#463` 消除 `Code=33 (EDOM)` 讀回 4 項 Entitlements 並完成卸載閉環（`176->175`）。[E32](docs/status-20261126.zh-TW.md#e32) |

### 相機、音訊、周邊與開發工具

| 項目 | 現狀 | 查驗數據與限制 |
| --- | --- | --- |
| 相機（IMX800 四鏡頭） | 0.5x／1x／3.2x 零發紫＋Tap-to-Focus＋`30 fps` 錄影 | `P0-Cam #450` 實證 `IMX800` 1x、`OV13B10` 0.5x、`S5K3K1` 3.2x（零發紫二次切換）、Tap-to-Focus（`sharpness 0.030`）、`30 fps` 錄影（`ok=1 err=0`）與 `Photos` 相簿閉環；`OV32C` 前鏡 Standby/DMA 檢驗（曾於 `U-028` 支線完測拍照）。4K 60fps 壓測待續。[E30](docs/status-20261126.zh-TW.md#e30) · [K-081](records/items/K-kernel.zh-TW.md#k-081-imx800-四鏡頭05x1x32x-零發紫閃光燈補光與持續自動對焦-c-af) |
| 雙揚聲器與麥克風／聽寫 | 出聲、錄音與 `en-US` 聽寫可用 | CS35L41 雙揚聲器（`gain=16`、`max_db=-4`、`idle-pdn=1500`、`bst_off=1`）；麥克風 DSP 旁路＋1440 預緩衝＋4 Hz HPF；`zh-TW` 雲端聽寫受 FairPlay 憑證（`-42184`）限制。[E21](docs/status-20261125.zh-TW.md#e21) · [U-027](records/items/U-userland.zh-TW.md#u-027-麥克風錄音品質與鍵盤聽寫修復fuxiaudio-mic-dsp-旁路fuxiaudio-啟動預緩衝4-hz-hpf0x0c08-增益與-fuxibluetoothlog-防崩潰) |
| 觸覺震動（HAP525） | Mode 9 v2.2 併入日常版 | `haptic=9 / damp=0x9`（117 次 burst `clp=0 / underruns=0`）＋手電筒零轉子；手感屬於主觀回饋。[E18](docs/status-20261125.zh-TW.md#e18) |
| 感測器、電池與 RTC | 多感測器 v8、ADSP 電量與動態校時 | `.sensor_pack` v8（`440,332 B` 置 `0xE8800000`）；ADSP 電量與插拔偵測；動態持久化 RTC（`--rtc-persist --rtc-user 1`）。[E17](docs/status-20261125.zh-TW.md#e17) |
| 手電筒與控制中心 | 五檔調光＋沿用 `B-021` `libfuxi-sb.dylib` | `fuxi-shape` 與 `B-021` 零差異，直接沿用 `sb-local-daily-b021-clean-artifact`（`e8cfaa01...`）。[E26](docs/status-20261125.zh-TW.md#e26) · [U-029](records/items/U-userland.zh-TW.md#u-029-控制中心手電筒模組權限過濾與設定清單保護) |
| 本機建置工具（WSL） | 9g 核心＋`libfuxi-sb.dylib` 可編可驗 | `B-022` 核心 `135 s`（`make 8 s`）、ISA `2,706,839` 條／`forbidden 0`、63/63 主機測試。屬 **`local-wsl (not a CI artifact)`**，不宣稱與 macOS CI 逐位元相同。[E34](docs/status-20261126.zh-TW.md#e34) |

### 暫緩與尚未驗收項目

1. **Apple 地圖（Class 1 GPU）與隔離實驗**：`B-022` 維持 `--gpu-clients none` 與 `Stick-14`。未併入 `B-022` 之隔離實驗（Phase 11 `#464`/`#465`、Phase 12 `#466`、Phase 12B `#467`/`#468` 及 `#469` 暴露 `M4-3 v3` `backboardd` 4096 BO 洩漏並回退至 `Stick-14`）轉入 `B-023` 藍圖（Phase 12D-A、Turnip BO 洩漏根治）後續處理。[E33](docs/status-20261126.zh-TW.md#e33) · [E36](docs/status-20261126.zh-TW.md#e36)
2. **商業 App Store（`cryptid=1` / R4）**：已實證硬體信任根邊界（`SAPSign -42036 -> http=401`），明確不支援；`B-023` 藍圖另規劃原生 AppStore UI opt-in 評估與 Option B 沙盒容器路徑 B2。[E32](docs/status-20261126.zh-TW.md#e32) · [E36](docs/status-20261126.zh-TW.md#e36)
3. **全場景 120 FPS 與 4K 60fps 錄影**：底層 Wave 2 時序已解鎖，惟全場景穩 120 FPS 與 4K 60fps 長時錄影壓測仍待優化與驗證。[E29](docs/status-20261126.zh-TW.md#e29) · [E30](docs/status-20261126.zh-TW.md#e30)
4. **5 GiB 核心配置與 macOS CI 一致性比對**：`B-022` 為 9g WSL 本機建置，5g 配置與正式 macOS CI 對照仍待另驗。[E36](docs/status-20261126.zh-TW.md#e36)
5. **戶外 GNSS 實地鎖星與真實 SIM 卡 VoLTE 通話實撥**：室內 `QMI_LOC`＋PDS 及無卡 `IMS Voice -> true` 已通，戶外實地鎖星與實際通話撥測待補。[E36](docs/status-20261126.zh-TW.md#e36)

## 倉庫內容

| 路徑 | 內容 |
| --- | --- |
| `patches/` | 核心、Metal、Turnip、相機、GNSS、Wave 2 與 Option B 等補丁共 43 份。依 `.github/workflows/kernel-adsp-battery.yml` 順序套用其中 **29 份** |
| `tests/`、`tools/`、`userland/` | 主機測試、ath12k 與震動／手電筒等工具，以及 iOS 端 `libfuxi-sb.dylib`（`userland/fuxi-shape/`） |
| `.github/workflows/` | 既有 CI 工作流程，未改觸發條件（見[來源與目錄說明](docs/source-layout.zh-TW.md#ci)） |
| `work/adsp-battery/ksrc/` | 核心原始碼樹：`zgl97/ios-fuxi` `979e2f08` 加本機修改（`a00205b`，131 份 `.patch`、內嵌只讀 `/Developer` 映像） |
| `work/adsp-battery/prepare-aio-loader.py` | `B-022` 全功能日常 loader 建置腳本（`9eca8290...`，128 KiB ADT、`.sensor_pack` `0xE8800000`、4 鏡頭 / 6 份 `.tab`、Wave 2、`--gpu-clients none`） |
| `ios-fuxi/` | loader 與工具原始碼樹：`bangprovn/ios-fuxi` `27f6a31` 加本機修改（14 個檔案，含 `probe_0x4e` `a8d64c47...`） |
| `dualboot/` | 雙系統圖形選單、安裝與救援工具（說明見 [`dualboot/README.zh-TW.md`](dualboot/README.zh-TW.md)） |
| `records/` | 研發紀錄庫索引、分類條目（`items/`）、去識別化日誌與對照表（`catalog/`），見 [`records/README.zh-TW.md`](records/README.zh-TW.md) |
| `work/localbuild/` | Windows＋WSL 本機核心與 `libfuxi-sb.dylib` 編譯腳本 |
| `work/`（其他） | loader 產生器、引導鏈、手機啟動器、iOS 根映像整備與檢查工具 |
| `uefi-fuxi/Resources/Scripts/mkbootimg.py` | AOSP `mkbootimg.py`（Apache-2.0，未修改） |
| `upstream/EXPORT-SOURCES.txt` | 各上游的釘選版本、tree id、本機修改清單與去識別化紀錄 |
| `docs/` | 功能查驗（2026-11-18 / 2026-11-25 / 2026-11-26）、CPU／GPU 配置與[來源與目錄說明](docs/source-layout.zh-TW.md) |

本機建置 9g 核心與 `libfuxi-sb.dylib`（需先在 WSL `Ubuntu-26.04` 裝好工具鏈與 iPhoneOS 16.5 SDK，見[來源與目錄說明](docs/source-layout.zh-TW.md)）：

```powershell
# 建置 9 GiB 核心並穿插執行 63 項主機測試
work\localbuild\build-kernel.ps1 -Ram 9g -Name kernel-local-<實驗名>-9g-artifact -Tests

# 建置 SpringBoard / 控制中心輔助函式庫 libfuxi-sb.dylib + ASan/UBSan + 碼頁簽署校驗
work\localbuild\build-sb-shape.ps1 -Name sb-local-<實驗名>-artifact
```

目前不提供一般使用者的刷機或部署步驟。

## 裝置與操作原則

目前開發及驗證以小米 13 `fuxi` 的 **12 GB RAM 實機**為主，不推定適用其他記憶體、儲存容量或裝置型號。

- 手機操作統一經由具備鎖檔檢查、紀錄保存與回復流程的啟動器（`work/statusbar-fix/launch-loader-check.ps1`）。
- 寫入前核對手機現況，寫入後讀回驗證，保留使用者資料與 5 個保護分區。
- 暫時開機與持久安裝分開說明；個別測試成功不等於可直接安裝。
- 整理、建置與主機測試流程不應自動操作手機。

## 外部資產與授權

本倉庫不提供 Apple IPSW、DMG、RootFS、專有系統二進位、高通韌體、iOS SDK、個人手機備份、金鑰或 Wi-Fi 憑證。需要的外部資產須由使用者自行合法取得或從自己的裝置擷取。

本專案自己撰寫的程式碼以 [MIT](LICENSE) 授權。對 Apple XNU 原始檔的修改依 APSL 2.0；其他上游元件維持各自的授權，原有的 LICENSE、NOTICE 與檔頭都保留。

收錄的 `bangprovn/ios-fuxi` 與 `zgl97/ios-fuxi` 上游沒有宣告授權，其作者保有原有權利；MIT 只涵蓋本專案新增的部分。權利人如要求移除，會照辦。完整清單見 [THIRD_PARTY.md](THIRD_PARTY.md)。
