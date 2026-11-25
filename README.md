<div align="center">

# Xiaomi 13 (`fuxi` / SM8550) iOS / XNU Bring-up

**小米 13 的 iOS／XNU 實驗移植專案，涵蓋核心適配、驅動、iOS userland 相容層、引導工具、雙系統選單與建置驗證流程。**

[![Device](https://img.shields.io/badge/Device-Xiaomi%2013%20(fuxi)-orange?style=flat-square)](#裝置與操作原則)
[![SoC](https://img.shields.io/badge/SoC-Snapdragon%208%20Gen%202%20(SM8550)-blue?style=flat-square)](#裝置與操作原則)
[![iOS](https://img.shields.io/badge/iOS-16.7.16%20(20H392)-blue?style=flat-square)](#目前進度)
[![Status](https://img.shields.io/badge/Status-B--021%20Experimental-yellow?style=flat-square)](#倉庫狀態)

[倉庫狀態](#倉庫狀態) · [目前進度](#目前進度) · [倉庫內容](#倉庫內容) · [裝置與操作原則](#裝置與操作原則) · [外部資產與授權](#外部資產與授權)

</div>

---

目前主要開發路線為 **iOS 16.7.16（20H392）／XNU 8796.141.3**。

> 本專案仍在實驗階段，尚不適合作為日常手機系統，也不是可直接刷入的完整 ROM。

## 倉庫狀態

**2026-11-25（`B-021` 常駐里程碑）：25 份補丁主分支堆疊已同步並收錄。**

核心與測試已同步至 `ksrc`（`db86c4e`）與 `harness`（`541b10b`）的 **25 份補丁堆疊**（**56/56** 主機測試命令 PASS、ISA 檢查 `2,703,368` 條指令／`forbidden 0`），並以 WSL 本機工具鏈編譯為 **`local-wsl (not a CI artifact)`** 升級至實機常駐 `boot_a` 分區（`INSTALLED_ANDROID_VERIFIED`，`menu-daily-b021-20261125` 通過 454 項斷言、保存 B-020 v2 回退映像與 5 個保護分區完好）。

> **重要邊界**：**Apple 地圖（Class 1 Metal 用戶端）已依決策在 `B-021` 暫緩排除（`--gpu-clients none`）**；其餘明確未納入或待續驗項目見下方「暫緩與尚未驗收項目」。

核心基底（`zgl97/ios-fuxi`）、loader 原始碼樹（`bangprovn/ios-fuxi`）與 `mkbootimg.py` 都已收進本倉庫，本機建置不必再向上游下載；Apple IPSW／RootFS、高通韌體、iOS SDK 與工具鏈仍須自行準備。快速導覽：[來源與目錄說明](docs/source-layout.zh-TW.md) · [紀錄庫索引](records/README.zh-TW.md) · [2026-11-25 功能查驗](docs/status-20261125.zh-TW.md)。

## 目前進度

以下進度嚴格依據 [2026-11-25 功能查驗](docs/status-20261125.zh-TW.md)（接續 [2026-11-18 查驗](docs/status-20261118.zh-TW.md) 與 [CPU／GPU 配置](docs/cpu-gpu-final-20261118.zh-TW.md)）及 `records/items/*` 實測紀錄整理；「已驗證」僅限於記載的版本與情境，未獨立驗收情境不作推定。

### 顯示、GPU 與輸入

| 項目 | 現狀 | 查驗數據與限制 |
| --- | --- | --- |
| 系統啟動與雙系統選單 | 已常駐 `boot_a`（`B-021`） | iOS 16.7.16／XNU 8796.141.3；`menu-daily-b021-20261125`（預設 10 秒進 Android、音量鍵選 iOS，454 項斷言、整區讀回 `INSTALLED_ANDROID_VERIFIED`）。b 槽選 Android 循環與冷開機全選項未逐一驗收。[E27](docs/status-20261125.zh-TW.md#e27) · [B-021](records/items/B-boot.zh-TW.md#b-021-雙系統日常常駐-boot_a-升級25-補丁主分支imx800-四鏡頭-c-af閃光燈gnsswi-fi-加速gpu-class-0-1280-mib120-hz-te手電筒控制中心修復暫緩-maps) |
| 螢幕顯示與去紫 | `1125×2500` 等比全寬＋系統模糊去紫 | `--display-fit aspect` 縮放填滿 `1080×2400` 面板；`--atx-fix 1` 修復單手模式、通話背景、Safari 分頁總覽與多工切換器發紫（洋紅像素 `0 / 1,296,000`）。須維持不改寫磁碟 `.atx` 為大寫 `'ASTC'`。[G-050](records/items/G-gpu.zh-TW.md#g-050-在-1080-寬面板顯示完整-1125-寬畫面並對齊觸控) · [E19](docs/status-20261125.zh-TW.md#e19) · [U-026](records/items/U-userland.zh-TW.md#u-026-springboardpreferences-偽-pre-twiddled-astc-atx-cpu-解碼改走線性解碼修復系統模糊背景區域發紫fuxiatx-fix) |
| 狀態欄與右側音量條 | 已依前鏡頭與側面按鍵校準 | 狀態欄保留 `360×86` 避讓區；右側音量條 `volhud=238128`（`y=238 h=128`）已併入日常版。[B-015](records/items/B-boot.zh-TW.md#b-015-以面板比例排版的日常版) · [E18](docs/status-20261125.zh-TW.md#e18) |
| Metal GPU（Class 0） | `1280 MiB` 桌面與相機穩定 | `FUXI GPU: through stage 16 PASS`、GPU fault／lost 為 0（日誌見 `failures=0`、`gfsr=0`）、`fuxi,gpu-bb-recover=3`、二次進出相機不掉 GPU；**Class 1（Apple Maps）已暫緩關閉（`--gpu-clients none`）**。[E25](docs/status-20261125.zh-TW.md#e25) · [G-051](records/items/G-gpu.zh-TW.md#g-051-gpu-class-0-1280-mib-配額120-hz-te-鎖定backboardd-恢復與多用戶端閘門) |
| 螢幕更新時序 | `120 Hz` TE 鎖定 | `SWEET FB: vsync te-locked=600 fallback=0 period_us 8253`（8249..8256 us）；此為面板 TE 鎖定統計，不等於全系統與應用層 120 fps 流暢度已完成。[E25](docs/status-20261125.zh-TW.md#e25) |
| 觸控、按鍵與背光 | 已驗證可用 | SweetHID 單指典型回報約 `120 Hz`（中位間隔 `8.333 ms`）、電源／音量鍵 `20 ms` 輪詢、DSI 背光控制。[E1](docs/status-20261118.zh-TW.md#e1) · [E2](docs/status-20261118.zh-TW.md#e2) · [E10](docs/status-20261118.zh-TW.md#e10) |

### CPU 調度與頻率

沿用 9g 7 核 XNU 調度架構（第 8 顆供 loader USB monitor）：推薦核心 `0x7c`（Gold/Prime）、`fuxiperf=2`（boost hold `1500 ms`、1 秒閒置 LUT 307.2／499.2／595.2 MHz、熱控 hot 66°C / limit 72°C）與 `fuxitcache=1`。詳見 [CPU／GPU 最終配置](docs/cpu-gpu-final-20261118.zh-TW.md)。

### 網路、通訊與定位

| 項目 | 現狀 | 查驗數據與限制 |
| --- | --- | --- |
| Wi-Fi `2.4 / 5 GHz` 與加速 | 已驗證連線與上網 | WCN7850 + `ath12k` 掃描快取與連線加速（`fuxiwlancache=1`、`fuxi,wifi-ux=8191`、`join finished 0`）；非 DRM 網頁影片可播，YouTube 受 FairPlay 裝置憑證限制無法播。[E24](docs/status-20261125.zh-TW.md#e24) · [K-083](records/items/K-kernel.zh-TW.md#k-083-wi-fi-掃描快取與連線加速ath12k-join-speed) |
| 個人熱點（SoftAP） | `5 GHz 80 MHz` Wi-Fi 6 已驗證 | `fuxi,wifi-ap=15`（`ap1` `ch=149` + Apple 雙圓圈 IE + `hostapd` 退場同步 + `bootpd` 綠標恢復）：`4/4 started ap1 / hostapd / bootpd`、雙向零丟包；關熱點後 STA 回收與 DMA 對齊已併入日常版。多裝置同時連入未測。[E20](docs/status-20261125.zh-TW.md#e20) · [K-079](records/items/K-kernel.zh-TW.md#k-079-wcn7850-原生-softap-個人熱點ap15-ghz-80-mhz-wi-fi-6apple-熱點-iehostapd-退場同步與橋接就緒-dhcp綠標恢復) |
| 藍牙 | 常駐 HCI 就緒 | 原廠 `bluetoothd` 驅動 QCA `0x40170200`（`baud=3200000`、`ibs=1`、`desync=0`），Apple 耳機 A2DP 可播與關閉藍牙不再閃退。[K-077](records/items/K-kernel.zh-TW.md#k-077-wcn7850-uartsk-與-skywalk-常駐收送) · [U-023](records/items/U-userland.zh-TW.md#u-023-bluetoothd-相容修補關藍牙閃退與單實例-ble-廣播fuxibt-fix) |
| 基頻行動數據與 IMS | 數據介面與 IMS 註冊就緒 | `CBD_WAIT_WDA` 等候修復後，`WDA error 0x0`、`pdp_ip0..2`（`qmap_bad=0`）與 `IMS Voice -> true` 已驗證；**真實可用 SIM 卡 VoLTE 通話實撥尚未測試**。[E20](docs/status-20261125.zh-TW.md#e20) · [B-020](records/items/B-boot.zh-TW.md#b-020-大一統整合日常版震動交響樂斷音修復錄音聽寫原生相機感測器指南針wda-時序與關熱點-wi-fi-穩定性修復) |
| GPS／GNSS 定位 | `QMI_LOC` + PDS 通道就緒 | `fuxi,modem-gnss=3` 實測 `COACHZ gnss: mode=0x3`（`pds` 與 `loc` 雙向收發計數正常）。實測均為室內，**戶外實際鎖星與地圖聯調尚未驗收**。[E24](docs/status-20261125.zh-TW.md#e24) · [K-082](records/items/K-kernel.zh-TW.md#k-082-gpsgnss-定位通道qmi_loc-與-pds-橋接) |

### 相機、音訊、周邊與開發工具

| 項目 | 現狀 | 查驗數據與限制 |
| --- | --- | --- |
| 相機（IMX800 四鏡頭） | 0.5x／1x／3.2x 零發紫＋閃光燈＋C-AF | `IMX800` 主攝、`OV13B10` 超廣角、`S5K3K1` 3.2x 長焦零發紫、`OV32C` 前攝、拍照閃光燈補光、持續自動對焦 C-AF、`16 B` VFE 步幅對齊與 `256 KiB` DMA 護欄（`guard_dirty=0`）。高負載多攝長時錄影仍待持續觀察。[E22](docs/status-20261125.zh-TW.md#e22) · [E23](docs/status-20261125.zh-TW.md#e23) · [K-080](records/items/K-kernel.zh-TW.md#k-080-sm8550-titan-gen3-原生相機核心驅動devfuxi-camera前-ov32c後廣角-ov13b10長焦-s5k3k1-三組-i2c-tab-與-16-byte-vfe-wm0-步幅256-kib-dma-guard) · [K-081](records/items/K-kernel.zh-TW.md#k-081-imx800-四鏡頭05x1x32x-零發紫閃光燈補光與持續自動對焦-c-af) |
| 雙揚聲器與麥克風／聽寫 | 出聲、錄音與 `en-US` 聽寫可用 | CS35L41 雙揚聲器（`gain=16`、`max_db=-4`、`idle-pdn=1500`、`bst_off=1`）；麥克風 DSP 旁路＋1440 預緩衝＋4 Hz HPF 錄音 `clip=0 / steps8k=0 / underruns=0`，`en-US` 離線聽寫啟動正常；`zh-TW` 因缺離線包且無 FairPlay 硬體憑證（`-42184`）無法使用雲端聽寫。[E18](docs/status-20261125.zh-TW.md#e18) · [E21](docs/status-20261125.zh-TW.md#e21) · [U-027](records/items/U-userland.zh-TW.md#u-027-麥克風錄音品質與鍵盤聽寫修復fuxiaudio-mic-dsp-旁路fuxiaudio-啟動預緩衝4-hz-hpf0x0c08-增益與-fuxibluetoothlog-防崩潰) |
| 觸覺震動（HAP525） | Mode 9 v2.2 併入日常版 | `haptic=9 / damp=0x9`（`manual-b020-v2-accept-1124` 117 次 burst `clp=0 / underruns=0`，交響樂／斷音爆震消除）＋手電筒零轉子；手感屬於主觀回饋。[E18](docs/status-20261125.zh-TW.md#e18) · [B-020](records/items/B-boot.zh-TW.md#b-020-大一統整合日常版震動交響樂斷音修復錄音聽寫原生相機感測器指南針wda-時序與關熱點-wi-fi-穩定性修復) |
| 感測器、電池與 RTC | 多感測器 v8、ADSP 電量與動態校時 | `.sensor_pack` v8（`440,332 B` 置 `0xE8800000`，`drop=0/0`）；ADSP 電量與插拔偵測；動態持久化 RTC（`--rtc-persist --rtc-user 1`：`persist:/time/ats_2`＋`timed` 持久化）。完整電池續航未測。[E4](docs/status-20261118.zh-TW.md#e4) · [E17](docs/status-20261125.zh-TW.md#e17) · [B-016](records/items/B-boot.zh-TW.md#b-016-動態-rtc-校時日常版persisttimeats_2-與-timed-持久化) |
| 手電筒與控制中心 | 五檔調光＋權限過濾修復 | 0 關閉＋1–4 亮度不閃；`libfuxi-sb.dylib`（`sb-local-daily-b021-clean-artifact`）修復 `FlashlightModule` 權限准入與清單保護（`native FlashlightModule capability admitted`）。[E5](docs/status-20261118.zh-TW.md#e5) · [E26](docs/status-20261125.zh-TW.md#e26) · [U-029](records/items/U-userland.zh-TW.md#u-029-控制中心手電筒模組權限過濾與設定清單保護) |
| 本機建置工具（WSL） | 9g 核心＋`libfuxi-sb.dylib` 可編可驗 | `B-021` 乾淨重編 24 s（make 4 s）、ISA 0 禁止指令、56 項主機測試命令；`libfuxi-sb.dylib` 8 s（57 碼頁簽署校驗）。屬 **`local-wsl (not a CI artifact)`**，不宣稱與 macOS CI 逐位元相同。[E27](docs/status-20261125.zh-TW.md#e27) |

### 暫緩與尚未驗收項目

1. **Apple 地圖（Class 1 GPU）**：已依決策在 `B-021` 暫緩排除（`--gpu-clients none`）；開放 Class 1 仍會觸發 GPU hang／SMMU fault（`ERROR_DEVICE_LOST`）。[E28](docs/status-20261125.zh-TW.md#e28)
2. **App Store 原生安裝**：目前由獨立分支線持續驗證，尚未併入 `B-021` 主幹。[E28](docs/status-20261125.zh-TW.md#e28)
3. **GPU 120 FPS 全鏈路優化**：目前僅確認面板 TE 鎖定在 `120 Hz`，系統與應用層 120 fps 流暢度優化仍列後續專項。[E28](docs/status-20261125.zh-TW.md#e28)
4. **5 GiB 核心配置與 macOS CI 一致性比對**：`B-021` 主分支與常駐版皆為 9g WSL 本機建置，5g 配置與正式 macOS CI 核心對照仍待另驗。[E28](docs/status-20261125.zh-TW.md#e28)
5. **戶外 GNSS 實地鎖星與地圖聯調**：室內雖已通過 `QMI_LOC`＋PDS 通訊，尚缺戶外強衛星訊號實地鎖星與地圖導航聯調。[E28](docs/status-20261125.zh-TW.md#e28)
6. **真實可用 SIM 卡 VoLTE 通話實撥**：無卡或未撥話條件下已見 `pdp_ip0..2` 與 `IMS Voice -> true`，插入真實可用 SIM 卡之實際通話撥測待補。[E28](docs/status-20261125.zh-TW.md#e28)

## 倉庫內容

| 路徑 | 內容 |
| --- | --- |
| `patches/` | 核心、Metal、Turnip、相機、GNSS 等補丁共 39 份。本機與 CI 建置依 `.github/workflows/kernel-adsp-battery.yml` 的順序，將其中 **25 份**套用到核心基底上 |
| `tests/`、`tools/`、`userland/` | 主機測試、ath12k 與震動／手電筒等工具，以及 iOS 端 `libfuxi-sb.dylib`（`userland/fuxi-shape/`） |
| `.github/workflows/` | 既有 CI 工作流程，未改觸發條件（見[來源與目錄說明](docs/source-layout.zh-TW.md#ci)） |
| `work/adsp-battery/ksrc/` | 核心原始碼樹：上游 `zgl97/ios-fuxi` `979e2f08` 加本機修改（XNU 驅動、內嵌只讀 `/Developer` 相機/基帶 HFS+ 映像、loader、ADT 產生器） |
| `work/adsp-battery/prepare-aio-loader.py` | `B-021` 全功能日常 loader 建置腳本（128 KiB ADT、`.sensor_pack` `0xE8800000`、4 顆鏡頭（6 份 `.tab`）、`--gpu-clients none`） |
| `ios-fuxi/` | loader 與工具原始碼樹：上游 `bangprovn/ios-fuxi` `27f6a31` 加本機修改（14 個檔案） |
| `dualboot/` | 雙系統圖形選單、安裝與救援工具（說明見 [`dualboot/README.zh-TW.md`](dualboot/README.zh-TW.md)） |
| `records/` | 研發紀錄庫索引、分類條目（`items/`）、去識別化日誌與對照表（`catalog/`），見 [`records/README.zh-TW.md`](records/README.zh-TW.md) |
| `work/localbuild/` | Windows＋WSL 本機核心與 `libfuxi-sb.dylib` 編譯腳本 |
| `work/`（其他） | loader 產生器、引導鏈、手機啟動器、iOS 根映像整備與檢查工具 |
| `uefi-fuxi/Resources/Scripts/mkbootimg.py` | AOSP `mkbootimg.py`（Apache-2.0，未修改） |
| `upstream/EXPORT-SOURCES.txt` | 各上游的釘選版本、tree id、本機修改清單與去識別化紀錄 |
| `docs/` | 功能查驗（2026-11-18 / 2026-11-25）、CPU／GPU 配置與[來源與目錄說明](docs/source-layout.zh-TW.md) |

本機建置 9g 核心與 `libfuxi-sb.dylib`（需先在 WSL `Ubuntu-26.04` 裝好工具鏈與 iPhoneOS 16.5 SDK，見[來源與目錄說明](docs/source-layout.zh-TW.md)）：

```powershell
# 建置 9g 核心並穿插執行 56 項主機測試
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
