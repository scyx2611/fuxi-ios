# K 系列：核心硬體適配（000 線）

2026-11-14 起在 `work\adsp-battery\ksrc`（zgl97/ios-fuxi `979e2f08` 加本機 88 個提交）做的硬體適配。核心原始碼在 `ksrc\xnu\fuxi-*\`，CI 實際套用的是 harness 的 `xnu\patches\01xx-*.patch`（由 `ksrc\tools\sync-fuxi-*.py` 從原始碼重產）。

共通事項（各項不再重複）：

- **改原始碼後重產補丁**：在 WSL 跑 `python3 tools/sync-fuxi-adsp.py`（ADSP／PMIC／藍牙／音訊都在 0117）、`sync-fuxi-wlan.py`（0104）、`sync-fuxi-h1.py`（0103）、`sync-fuxi-backlight.py`（0109）；Windows 的 Python 會把補丁寫成 CRLF。產生的補丁複製到 harness 的 `xnu\patches\` 再建置。
- **開關優先序**：開機參數（`PE_parse_boot_argn`）先，沒有才讀 ADT `/chosen` 的 u32 屬性。日常命令列 607／608 bytes 已滿，所以新開關都同時提供 ADT 名稱，由 loader 產生器寫進 ADT。
- **預設全部關閉**：開關不給就不碰硬體；日常 loader 只打開已驗收的項目（現況見 B 系列的開機鏈項目）。
- **建置與上機**：照 `records\README.zh-TW.md` 的「共用的復現步驟」。下面的「復現」只寫這一項特有的旗標與要看的 console 行。
- **紀錄出處**：`journal\hw000-adaptation-status.zh-TW.md`（原 `work\hw000-adaptation-status.zh-TW.md`）的段落名，以及 ksrc `docs-lanes\*.md`。

## 電池與 ADSP

### K-001 ADSP 開機與 GLINK 電池讀值
- **日期**：2026-11-15 00:02（d5c44c3）～11-16 02:14 首次讀到正確電量
- **狀態**：採用
- **開關**：`fuxiadsp=<0..4>`，或 ADT `/chosen fuxi,adsp`（u32）。0／不給＝不碰 ADSP；1＝只讀盤點（SMEM 分區與項目、ADSP 的 SMP2P 狀態、AOSS 信箱、RSC、兩條 SPI 的 GIC enable），不寫任何東西；2＝啟動 ADSP 並建立 SMP2P、GLINK、QRTR service locator 與電池管理客戶端，只記錄讀值；3＝另外發布給 iOS（K-002）；4＝測試用假讀值（K-005）。與 `fuxiwlan` 同時設定時自動降為 1，除非 ADT `fuxi,adsp-wlan=1`（K-012）。
- **問題／現象**：iOS 固定顯示 SweetBattery 的預設值（100%、接電、未充電），沒有真實電量。高通平台的電量計、充電器都由音訊 DSP（ADSP／LPASS）上的 battery manager 管，AP 端只能透過 PMIC GLINK 通道問。
- **根因**：XNU 這邊沒有 ADSP 遠端處理器的啟動流程（PAS 認證、carveout 載入、AOSS load_state、RPMh 代投票）和 GLINK／QRTR 協定。
- **做法**：
  - 從 UniXNU-sheng `package/AdspProbe.cpp`／`ShengBattery.cpp`（commit `6880068`，BSD-2-Clause-Patent，授權檔 `xnu\fuxi-adsp\LICENSE.UniXNU-sheng`）移植；協定模組 `QcomGlink.c`、`QcomMdt.c`、`QcomQmp.c`、`QcomQrtr.c`、`QcomRpmh.c`、`QcomScm.h`、`QcomSmem.c`、`QcomSmp2p.c`、`FatVolume.c`、`ShengBattmgr.c`、`ShengQrtr.c` 原樣使用。
  - 本機新寫 `fuxi_adsp.c`（單一核心執行緒擁有全部步驟與協定狀態）、`fuxi_adsp_fw.c`（韌體從 `modem_a` 分區的 FAT 檔案系統原地讀，不另放檔案）、`fuxi_adsp_battery.c`（電池客戶端，通道 `PMIC_RTR_ADSP_APPS`）。
  - 與 sheng 不同處：這顆核心的 GIC 只有 GPU fence 一個 SPI handler，所以不用中斷，改輪詢 IPCC 接收暫存器（啟動中 1 ms，有流量後 500 ms 內每 2 ms，其餘 200 ms）與 RSC 完成狀態；先在 distributor 把 IPCC（SPI 229）與 RSC（SPI 5）遮罩，避免開機階段留下的 level 中斷打爆無主 EOI 路徑。MMIO 與 carveout 用 `kernel_map` VA＋pmap 映射，不用 `ml_io_map`（8 MiB 子映射是 KMA_NOFAIL）。
  - 裝置樹事實（fuxi live DTB，與 sheng 的 SM8550 相同）：`remoteproc-adsp`（`qcom,kalama-adsp-pas`），adsp PAS id 1 在 adspslpi `0x9ea00000`+`0x4680000`，adsp_dtb PAS id 36 在 `0x9e980000`+`0x80000`；SMEM `0x81d00000`+`0x200000`，TCSR mutex 3；smp2p-adsp 項目 443 進／429 出，remote pid 2；IPCC `0x408000`；GLINK 項目 478／479／480；AOSS `0xc300000`+`0x400`；apps RSC DRV 2 在 `0x17a20000`（TCS +`0xd00`）；cmd-db `0x81c60000`。全部在 XNU RAM（`0x8c0000000` 起）之外，`tools\mkadt-fuxi.py` 會拒絕和 reserved-memory 重疊的配置。
  - 韌體分區讀取靠 0106（`sweet_ufs.c`）新增的韌體分區讀取介面。
  - 補丁：harness `xnu\patches\0117-fuxi-adsp.patch`（本機新增）；`tools\sync-fuxi-adsp.py` 由原始碼重產。
- **復現**：
  1. 本機建核心（9g 或 5g 都可，ADSP 與 RAM 配置無關）。
  2. loader：`python -X utf8 work\adsp-battery\prepare-ram9g-loader.py <新 loader 名> <來源 loader> <核心產物>`，命令列加 `fuxiadsp=2`（命令列滿時改由產生器寫 ADT `fuxi,adsp`）。
  3. 上機 `-Seconds 180`。
  4. 預期 console：`FUXI ADSP: firmware adsp … KiB, adsp_dtb … KiB` → `FUXI ADSP: adsp_dtb authenticated and started` → `FUXI ADSP: adsp authenticated and reset; waiting for ready` → `FUXI ADSP: ready after … ms` → `FUXI ADSP: glink version agreed` → `FUXI BATTERY: channel up; notifications requested, first refresh` → `FUXI BATTERY: percent=… mV=… mA=…`。失敗行：`FUXI ADSP: failed at stage %d error %d (scm x0=… x1=…)`、`FUXI ADSP: crash reason …`。
- **主機測試**：`tools\test-fuxi-battery-prep.py`（harness `hw000-preparation.yml` 執行）。
- **驗證紀錄**：11-16 USB 測試開機 `adsp2`：`channel IPCRTR up`、GLINK 通；`adsp2b` 02:14（核心 ksrc 040332a，含 K-003 修正）`FUXI BATTERY: percent=100`、f2=10000，與 Android 同時讀數一致（GATE `percent=100 mV=4443 android_level=100`）：4388–4443 mV、滿充 4268 mAh／設計 4500 mAh、152 次循環。
- **參考來源**：UniXNU-sheng AdspProbe／ShengBattery（`6880068`）；Linux `drivers/remoteproc/qcom_q6v5_pas.c`、`drivers/soc/qcom/pmic_glink.c`、`drivers/power/supply/qcom_battmgr.c`（欄位意義對照）。
- **結論／未完成**：ADSP 啟動與電池讀值穩定。充電控制仍完全由 DSP 韌體負責，核心只送通知啟用請求，不寫其他東西。
- **出處**：journal「Claude 接續（19:10 起）」與「總結（03:40）」段；ksrc `docs-lanes\adsp.md`；記憶 `xiaomi13-adsp-battery`。

### K-002 SweetBattery 發布真實電量
- **日期**：2026-11-15 00:16（478bf32）；2026-11-16 02:22 上機驗證
- **狀態**：採用
- **開關**：`fuxiadsp=3`（或 ADT `fuxi,adsp=3`）。另有舊開關 `sweetbattery=1`（PM6150 電壓估算，只對 sweet 機型有意義，fuxi 不用）。
- **問題／現象**：K-001 只把讀值寫進 log，SpringBoard 的電池圖示、設定頁的電池百分比都還是預設值。
- **根因**：iOS 透過 `IOPMPowerSource` 類別（`AppleSmartBattery` 相容屬性）讀電量；SweetBattery（ios-sweet 繼承的內建服務）只有固定值或 PM6150 估算兩種來源。
- **做法**：`xnu\fuxi-adsp\SweetBattery.cpp` 新增 ADSP 來源：`fuxi_adsp_battery.c` 每輪刷新後呼叫 SweetBattery 更新 `CurrentCapacity`、`Voltage`、`Amperage`、`ExternalConnected`、`IsCharging`、`FullyCharged` 等屬性並送變更通知；讀不到前保留預設值。
- **復現**：loader 用 `fuxiadsp=3`；上機 `-Seconds 180`。預期 console：`SWEET BATTERY: ADSP battery manager, <百分比>% <mV>mV <mA>mA external=<0|1> charging=<0|1>`，之後 `SWEET BATTERY: published #<n> …`。畫面證據：狀態欄電量與設定 → 電池百分比相符。
- **主機測試**：`tools\test-fuxi-battery-publication.py`。
- **驗證紀錄**：11-16 02:22 `adsp3t`（第二輪鏈 bl4→amp2→adsp3t→amp3→bl5t，03:37 跑完）：`SWEET BATTERY: ADSP battery manager, 100% 4367mV -609mA external=1 charging=1`，SpringBoard 的 `IOPMPowerSource` 配對到 SweetBattery，截圖狀態欄有充電符號；巡覽 14 張截圖在 `work\statusbar-fix\sb-compare-adsp3t.png`。
- **參考來源**：ios-sweet SweetBattery 原始實作；Apple `IOPMPowerSource.h` 屬性鍵。
- **結論／未完成**：USB 測試開機已驗；使用者回報日常獨立開機曾仍顯示 100% 充電中（拔線後），待 mtdblk 紀錄（K-019）查。後續修正見 K-006～K-009。
- **出處**：journal「總結（03:40）」段；記憶 `xiaomi13-adsp-battery`。

### K-003 單電芯電量換算
- **日期**：2026-11-16 01:32（040332a）；02:14 上機驗證
- **狀態**：採用
- **開關**：無（隨 `fuxiadsp>=2` 生效）
- **問題／現象**：照 sheng 的算法讀到 48%，同時 Android 顯示 97%。
- **根因**：sheng 的機型是雙電芯，把兩顆電量計的 mAh 相加後再算百分比；小米 13 是單電芯，第二組欄位是 0 或重複值，相加後分母錯誤。battery manager 回報的 percent 欄位是 0.01% 單位（9600＝96%）。
- **做法**：`fuxi_adsp_battery.c` 直接用 battery manager 的 percent 欄位（除以 100）；mAh 原始值只取電量計 1。
- **復現**：同 K-001，比對 `FUXI BATTERY: percent=` 與 Android 設定頁（或 `adb shell dumpsys battery`）同時刻的數值，差距應在 1% 內。
- **主機測試**：`tools\test-fuxi-battery-prep.py` 內的換算案例。
- **驗證紀錄**：`adsp2b` 02:14 讀 100% 對 Android 100%。
- **參考來源**：Linux `qcom_battmgr.c` 的 `BATTMGR_BAT_STATUS` 欄位單位。
- **結論／未完成**：完成。
- **出處**：journal「總結（03:40）」段。

### K-004 充電器種類、真實充電資訊與 10 秒刷新
- **日期**：2026-11-16 02:48（26a4114）～10:55（1b4f5f8）
- **狀態**：採用
- **開關**：無（隨 `fuxiadsp=3`）
- **問題／現象**：充電時 iOS 只知道「有接電」，不知道是 USB SDP 還是 PD 快充；電流數字 60 秒才更新一次，插拔反應慢。
- **根因**：log 的充電器類型原本取 f8（一直是 SDP）；真正的充電器類型在 usb 屬性 f20（`BmUsbAdapter`）。SweetBattery 也沒把充電器類型發佈給 iOS，iOS 只知道「外接電源、充電中」。刷新週期沿用 sheng 的 60 秒。
- **做法**：26a4114 依 f20 決定 adapter family（SDP f20=1 → 500 mA；PD f20=6 → 1.2 A 等）寫進 SweetBattery 的 `AdapterDetails`；1b4f5f8 改用真實的充電器電壓、電流上限，並把刷新週期改為 10 秒（通知到達時仍立即刷新）。
- **復現**：`fuxiadsp=3` 上機，分別接 USB 電腦埠與 PD 充電器。預期 console：`SWEET BATTERY: published #… external=1 … charging=1 … <mA>mA`，`FUXI BATTERY: raw valid=…` 列出 f20 值。
- **主機測試**：`tools\test-fuxi-battery-publication.py`。
- **驗證紀錄**：USB 測試開機中途電源類型從電腦口 SDP（f20=1，輸入上限 500 mA）換成 PD（f20=6，上限 1.2 A），之後電池電流 +289～343 mA、charging=1，f9／f10 4.62–4.65 V、781–831 mA、f21=1200000、f4 為負（充電）；Android 開回來顯示 AC powered。amp3 那趟驗證 26a4114：adapter=`0xe000400a`（PD）。
- **參考來源**：Linux `qcom_battmgr.c` 的 `usb_type` 對應表。
- **結論／未完成**：完成。
- **出處**：journal「Claude 接續（19:10 起）」與「總結（03:40）」段。

### K-005 拔線測試讀值（fuxi,adsp=4）
- **日期**：2026-11-16 07:02（ba65d38）
- **狀態**：實驗（只供測試，不進日常）
- **開關**：`fuxi,adsp=4`（ADT）或 `fuxiadsp=4`。實際執行模式是 3，另外讓 SweetBattery 先收到一筆假讀值。
- **問題／現象**：USB 測試開機時手機一定接著電，無法驗證「拔線、未充電、電量下降」這條顯示路徑。
- **根因**：測試流程本身需要 USB。
- **做法**：`fuxi_adsp_battery.c` 在模式 4 時送一筆固定讀值 57%、未接電、未充電給 SweetBattery，之後真實讀值照常覆蓋。tour 腳本拖動亮度滑桿 8 次並截圖。
- **復現**：loader 寫 ADT `fuxi,adsp=4`、帶 tour 截圖；預期 console：`FUXI BATTERY: TEST (fuxi,adsp=4) SweetBattery now gets a staged reading: 57%, unplugged`。畫面：狀態欄顯示 57%、無閃電符號。
- **主機測試**：`tools\test-fuxi-battery-publication.py`。
- **驗證紀錄**：bl6t 那趟（11-16 07:19–07:39）截圖 `framebuffer-shot19-battery-staged-cc.png` 顯示 57% 未充電。
- **參考來源**：無。
- **結論／未完成**：顯示路徑正確。真正拔線的行為要靠獨立開機＋mtdblk 紀錄（K-019）驗。
- **出處**：journal「Claude 接續（19:10 起）」段。

### K-006 SweetBattery 讀取者紀錄
- **日期**：2026-11-16 07:08（bad6adf）
- **狀態**：採用（診斷）
- **開關**：無（隨 `fuxiadsp=3`）
- **問題／現象**：獨立開機時狀態欄仍顯示「100% 充電中」，不知道是 SweetBattery 沒更新還是 iOS 的讀取者讀到舊值。
- **根因**：查證中（見結論）。
- **做法**：SweetBattery 每次被讀時記錄讀取的行程名：開機後前 16 次全記，之後每次值改變後再記 4 次。
- **復現**：`fuxiadsp=3` 上機，預期 console：`SWEET BATTERY: read #<n> by <行程名> (published #<m>: <百分比>% external=… charging=…)`；若仍是預設值則是 `(still the fallback)`。
- **主機測試**：無（只是 log）。
- **驗證紀錄**：之後各趟 `fuxiadsp=3` 上機的 console 都有這幾行（讀取者行程名照實記錄在各趟 `console.txt`）。
- **參考來源**：無。
- **結論／未完成**：USB 開機時讀取者拿到的是新值；獨立開機的「100% 充電中」要靠 mtdblk 紀錄比對，仍未結案。
- **出處**：journal「更正（07:50）」段。

### K-007 電池欄位逐項過期（TTL）
- **日期**：2026-11-16 14:45（b6ce046）
- **狀態**：採用
- **開關**：`fuxibatttl=<秒>`（預設 90）
- **問題／現象**：ADSP 某一欄位暫時讀不到時，SweetBattery 會一直沿用很久以前的值，例如拔線後仍顯示接電。
- **根因**：原本整筆讀值只要有一欄成功就整筆視為新鮮。
- **做法**：`fuxi_adsp_battery.c` 為每個欄位記錄最後成功時間，超過 TTL 的欄位標為過期；過期的接電／充電狀態不發布，改回保守值。同提交加入 `fuxi-battery-prep` 的新鮮度模型與 `docs-lanes\battery-publication-checkpoint.md`、`battery-freshness.md`。
- **復現**：`fuxiadsp=3`，可縮短 TTL 觀察（`fuxibatttl=30`）。預期 console：`FUXI BATTERY: percent=… (stale)`、`FUXI BATTERY: rejected field <n> value <v>`。
- **主機測試**：`tools\test-fuxi-battery-prep.py`（新鮮度案例）、`tools\test-fuxi-battery-publication.py`。
- **驗證紀錄**：Run 23（`native-hw000rtc-shortpause-boot-1`，11-16 18:11:20）在第 50 次取樣（距上次成功 96.7 s）觸發過期，第 80 次取樣恢復。
- **參考來源**：無。
- **結論／未完成**：完成。
- **出處**：journal「Codex 接續（11-16 10:04 起）」段；ksrc `docs-lanes\battery-freshness.md`、`battery-publication-checkpoint.md`。

### K-008 電池查詢暫停（GET pause）實驗
- **日期**：2026-11-16 16:09（b6be873）～16:36（acb3fcd）；17:21、18:11 上機
- **狀態**：實驗（已驗證，不進日常）
- **開關**：`fuxibatpause=<毫秒>`（0 或 130000）、`fuxibatpausedelay=<毫秒>`；loader 產生器 `prepare-ram9g-loader.py --battery-get-pause-ms 130000 --battery-get-pause-delay-ms <ms> --battery-audit`。
- **問題／現象**：要驗證 K-007 的過期機制在真實「讀不到」時的行為，需要可控地讓 ADSP 查詢停一段時間。
- **根因**：（實驗工具）
- **做法**：`fuxi_adsp_battery.c` 在指定延遲後暫停送 GET 請求指定毫秒數，期間紀錄每次 SweetBattery 的讀取；`--battery-audit` 加讀 HAP525 唯讀證據（同提交）。
- **復現**：`python -X utf8 work\adsp-battery\prepare-ram9g-loader.py <名> <來源> <核心> --battery-get-pause-ms 130000 --battery-audit`，上機 `-Seconds 300`。預期 console：`FUXI BATTERY: poll-pause begin …` 與 `poll-pause end …`。分析：`python -X utf8 work\statusbar-fix\analyze-battery-pause.py <run 資料夾>`。
- **主機測試**：`tools\test-fuxi-battery-pause.py`。
- **驗證紀錄**：Run 20（`native-hw000batpause-hap-boot-1`，17:21:45）暫停 42569→172570 ms；Run 23（`native-hw000rtc-shortpause-boot-1`，18:11:20）暫停 42192→172193 ms，120 次取樣、6 份快照、192 列、0 解析錯誤，報告 `work\statusbar-fix\battery-pause-short-20261116-1817.json`。
- **參考來源**：無。
- **結論／未完成**：過期與恢復都照設計。
- **出處**：journal「Codex 接續（11-16 10:04 起）」段；ksrc `docs-lanes\battery-poll-pause-checkpoint.md`。

### K-009 SweetBattery 改走 IOKit work loop 發布
- **日期**：2026-11-16 21:45（5a8b7e9）
- **狀態**：採用
- **開關**：無
- **問題／現象**：電池客戶端的 thread call 直接改 SweetBattery 屬性，和 iOS 讀屬性的路徑沒有同步，有競態風險。
- **根因**：IOService 屬性更新應在該服務的 work loop 上做。
- **做法**：SweetBattery 加 `IOInterruptEventSource`，電池客戶端只排一個事件，實際更新在 work loop 執行。
- **復現**：同 K-002；console 與先前相同，差別在不再出現交錯的發布序號。
- **主機測試**：`tools\test-fuxi-battery-publication.py`。
- **驗證紀錄**：CI fast 43878944772、9g 43878944603、5g 43878944613（核心 `e59e5cb8…`）。11-17 07:38 聯合趟 `native-hw000wlan-joint1-boot-1`：接電狀態 0↔1 切換正確，140 次查詢 0 逾時。
- **參考來源**：Apple IOKit `IOInterruptEventSource` 用法。
- **結論／未完成**：完成。
- **出處**：journal「Codex 再接續（11-16 21:31 起）」段。

### K-010 充電證據工具
- **日期**：2026-11-16 15:24（a948742）
- **狀態**：採用（工具）
- **開關**：無
- **問題／現象**：充電驗收需要把 console、Android 讀數、時間軸對齊，每次手算容易出錯。
- **根因**：（工具）
- **做法**：`ksrc\tools\fuxi-charge-evidence.py`：讀上機紀錄中的 `FUXI BATTERY:`／`SWEET BATTERY:` 行與 ADT `fuxi,adsp`，輸出插拔事件、電流變化、讀取者的表格與判定。
- **復現**：`python3 tools/fuxi-charge-evidence.py <run 資料夾>/console.txt`（WSL 或 Windows `python -X utf8`）。
- **主機測試**：`tools\test-fuxi-charge-evidence.py`。
- **驗證紀錄**：11-17 聯合 ADSP／WLAN 拔插趟（07:38）、akswrap 趟（電量 4234–4248／4249 mAh）都用它整理。
- **參考來源**：無。
- **結論／未完成**：完成。
- **出處**：ksrc `docs-lanes\charging-acceptance.md`。

## 共用電源與音訊

### K-011 ADSP 與 WLAN 共用 RPMh（序列化與隔離）
- **日期**：2026-11-16 11:26（93201ca）
- **狀態**：採用
- **開關**：無（ADSP 或 WLAN 任一啟用時生效）
- **問題／現象**：ADSP（K-001）和 WLAN（K-041 起）都要對 apps RSC 送 RPMh 投票（lcx／lmx、WLAN 電源軌）。兩邊各自寫 TCS，同時進行時可能互相覆蓋，且某一方失敗後 RSC 狀態不明。
- **根因**：RSC 的 TCS 是共用硬體；沒有互斥鎖，也沒有「狀態不明就停用」的規則。
- **做法**：新增 `xnu\fuxi-adsp\fuxi_rpmh_shared.c`／`.h`：全域 mutex 串行化所有 RPMh 交易；ADSP 用 active TCS 1、WLAN 用 TCS 0；任一交易逾時或完成狀態不符時把共用 RSC 標為隔離（quarantined），之後雙方都不再送。
- **復現**：任一 ADSP 或 WLAN 上機都會用到。異常時預期 console：`FUXI ADSP: rpmh cleanup failed; shared RSC quarantined`；正常時 `FUXI ADSP: rpmh vote lcx=… lmx=… done=1`。
- **主機測試**：`ksrc\tools\test-fuxi-rpmh-shared.py`（`hw000-preparation.yml`）。
- **驗證紀錄**：之後所有 ADSP、WLAN 及聯合趟（K-012）都未觸發隔離。
- **參考來源**：Linux `drivers/soc/qcom/rpmh-rsc.c` 的 TCS 分配與 `tcs_is_free` 規則。
- **結論／未完成**：完成。
- **出處**：journal「Codex 接續（11-16 10:04 起）」段。

### K-012 ADSP 與 WLAN 聯合測試開關
- **日期**：2026-11-17 07:35（72a31b8）；07:38 上機
- **狀態**：實驗（已驗，日常未開）
- **開關**：ADT `fuxi,adsp-wlan=1`。沒有它時，命令列同時有 `fuxiwlan` 與 `fuxiadsp>=2` 會把 ADSP 降為 1（只讀盤點）並記 `FUXI ADSP: fuxiadsp=… refused with fuxiwlan (combined hardware validation pending); surveying only`。
- **問題／現象**：ADSP 與 WLAN 同時啟用還沒驗過共用 RPMh、SMEM、IPCC 的行為，先以預設拒絕保護日常。
- **根因**：（安全閘門）
- **做法**：`fuxi_adsp.c` 的 `fuxi_adsp_mode()` 只有在 ADT `fuxi,adsp-wlan=1` 時才允許和 WLAN 並行。loader 產生器 `prepare-wlan-w18-loader.py --with-adsp` 會同時寫 ADT `fuxi,adsp=3`、`fuxi,pmic=3`、`fuxi,adsp-wlan=1`，並檢查核心含這個開關的字串、命令列沒有 `fuxiadsp=`／`fuxipmic=`。
- **復現**：`python -X utf8 work\adsp-battery\prepare-wlan-w18-loader.py build --like <已驗 native Wi-Fi loader> --artifact <核心> --name <新名> --with-adsp`；上機時間依要觀察的拔插次數決定，中途手動拔插 USB。預期 console：`FUXI ADSP: fuxiadsp=3 alongside fuxiwlan (fuxi,adsp-wlan=1: combined test, shared RSC mutex)`，之後 `FUXI BATTERY:` 與 `FUXI WIFI IOS:` 兩邊都持續輸出。
- **主機測試**：`ksrc\tools\test-fuxi-rpmh-shared.py`。
- **驗證紀錄**：11-17 07:38 `native-hw000wlan-joint1-boot-1`（本機 9g `kernel-local-joint1-9g-artifact`，ADT `fuxi,adsp=3`、`fuxi,pmic=3`、`fuxi,adsp-wlan=1`）：ADSP 與 WLAN 同時起來；拔線時 `FUXI BATTERY` 報 external=0 charging=0，插回 external=1 charging=1，140 輪 0 逾時。同趟使用者手動加入 `<SSID>` 成功，但連上那一刻 securityd 的 AKS sel 10 被拒 `0xe00002c7`（K-059 修）；「日期與時間」卡死（U 系列 fairplayd 項）；mediaserverd 4 次 SIGABRT（同前幾趟）。
- **參考來源**：無。
- **結論／未完成**：聯合運作正常；日常 loader 仍只開 Wi-Fi，ADSP 要等獨立開機的電量顯示問題（K-002 結論）查清再合併。
- **出處**：journal「Codex 再接續（11-16 21:31 起）」之後的 11-17 段落；記憶 `xiaomi13-wlan-state`。

### K-013 CS35L41 喇叭功放盤點
- **日期**：2026-11-15 00:37（417dcf3）；2026-11-16 02:48（26a4114）
- **狀態**：實驗（只讀；未出聲）
- **開關**：`fuxiamp=<1|2>`，或 ADT `fuxi,amp`。1＝只讀盤點：GCC 投票 `0x52008`／`0x52010`／`0x52018`、QUP2 五個 branch（core `0x233d4`、s_ahb `0x233d0`、m_ahb `0x233cc`、core_2x `0x23004`、s0 `0x1e004`）、SE0 時脈源 `0x1e010`、四支腳位；branch 全開時再讀 engine 狀態。2＝照 UniXNU-sheng `AudioProbe.cpp` 的 setupClocks／setupPins／setupBus 把 QUP2 SE0 I2C 帶起來（400 kHz、FIFO 模式），兩顆功放各自解除 reset（低 2 ms、拉高、等 10 ms OTP 開機），讀 DEVID `0x0`、REVID `0x4`、IRQ1_STATUS3 `0x10018`、IRQ1_STATUS4 `0x1001c`，不寫功放；最後重新 reset，腳位與投票還原。
- **問題／現象**：要讓喇叭出聲，得先確認兩顆 CS35L41（`cs35l41@40` 「B」、`@42`「T」）在 I2C 上可存取。
- **根因**：mode 2 第一版（amp2）讀不到功放：reset 腳 gpio182／183 是 eGPIO，開機時 ctl=`0x801`＝歸 LPASS 音訊島，AP 端直接驅動無效；I2C 匯流排、時鐘、engine 本身正常。
- **做法**：`xnu\fuxi-adsp\fuxi_amp.c`、`GeniI2c.c`（sheng 原樣）；26a4114 驅動 reset 前先把 eGPIO 182／183 的 `egpio_enable`（TLMM ctl bit 12）設起來，把腳位從 LPASS 拿回 AP。I2C 腳 gpio0 SDA／gpio1 SCL、function 2（qup2_se0_l0／l1_mirb）、2 mA、上拉；engine SPI 373 先遮罩，全部輪詢。窗口：QUP2 SE0 `0x880000`+`0x4000`、GCC `0x100000`、TLMM `0xf100000`、GICD `0x17100000`。
- **復現**：命令列加 `fuxiamp=2`（或 ADT `fuxi,amp=2`），上機 `-Seconds 120`。預期 console：`FUXI AMP: started mode 2` → `FUXI AMP: survey vote 0x52008=…` → `FUXI AMP: amp B@0x40 … devid=0x35a40 … revid=0xb2 …` 與 `amp T@0x42 …`；失敗行 `FUXI AMP: bring-up stopped at step <n> (6 = both read; …)`。
- **主機測試**：無專用測試（只讀、全還原）。
- **驗證紀錄**：11-16 第二輪鏈 `amp2`（26a4114 前，reset 放不開，讀不到）→`amp3`（26a4114 後）：兩顆 CS35L41 都回應，DEVID `0x35a40`、REVID `0xB2`，IRQ1_STATUS4=`0x3`（OTP 開機完成）、STATUS3 無 OTP 錯誤；reset 腳位這次真的放開（ctl `0x1bc0`、輸入 0→1）；讀完放回 reset，腳位與時鐘票還原。證據：amp3 `boot.log`。
- **參考來源**：UniXNU-sheng `AudioProbe.cpp`（`6880068`）；Linux `sound/soc/codecs/cs35l41*.c`；fuxi live DTB `i2c@880000`。
- **結論／未完成**：功放可存取。出聲還缺：ADSP 音訊圖（K-014、K-015）、功放韌體與調校（`cs35l41-dsp1-spk-prot` 系列）、iOS 音訊裝置。
- **出處**：journal「總結（03:40）」段；記憶 `xiaomi13-adsp-battery`、`xiaomi13-audio-spf`。

### K-014 音訊 SPF 狀態查詢
- **日期**：2026-11-17 11:32（6f28814）～12:01（4b3402d）
- **狀態**：實驗（已通，下一步未做）
- **開關**：`fuxiaudiospf=<1|2>`，或 ADT `fuxi,audio-spf`。1＝APM 格式查詢（40 bytes）；2＝裸 GPR 狀態查詢（24 bytes）。loader：`prepare-wlan-w18-loader.py --audio-spf [--audio-spf-format apm|bare]`。
- **問題／現象**：要在 ADSP 上建音訊圖，第一步要知道 ADSP 的 SPF（Signal Processing Framework，AudioReach）是否就緒。
- **根因**：第一版查詢被拒：GPR 標頭的 client 位元（bits 16–19）不符；首次查詢常回 `AR_EFAILED`，要再問。
- **做法**：`xnu\fuxi-adsp\fuxi_audio_spf.c`：在 GLINK `adsp_apps` 通道（512 bytes × 20 個 intent）註冊只查詢的客戶端，送 APM `GET_SPF_STATE`。fd7ee5e 記錄被拒回覆的標頭；a4e663d 接受 GPR client data 位元並加裸查詢模式 2；4b3402d 每次用新 token 重試，最多 100 次、間隔 100 ms、每次回覆期限 1 s。
- **復現**：`prepare-wlan-w18-loader.py build --like <已驗 loader> --artifact <核心> --name <新名> --with-adsp --audio-spf`，上機 `-Seconds 180`。預期 console：`FUXI AUDIO SPF: registered adsp_apps query-only client` → `FUXI AUDIO SPF: state query queued bytes 40` → `FUXI AUDIO SPF: state 1 after <n> queries (…)`。失敗行：`not ready after … queries`、`rejected reply …`、`channel timeout`。
- **主機測試**：`ksrc\tools\test-fuxi-audio-spf.py`（26106 種 GPR 標頭組合）。
- **驗證紀錄**：11-17 `passivespf` 11:35（被拒，取得標頭）、`spfclient` 11:56（client 位元修正）、`spfretry` 12:04：`state 1 after 2 queries`（第二次查詢就緒）。
- **參考來源**：Linux `sound/soc/qcom/qdsp6/audioreach.c`、`q6apm.c`、`drivers/soc/qcom/apr.c`（GPR）。
- **結論／未完成**：SPF 就緒。之後還缺 PRM（時脈／電源資源）、音訊圖（ACDB，見 K-016）、CS35L41 播放、iOS 音訊裝置。
- **出處**：記憶 `xiaomi13-audio-spf`；ksrc `docs-lanes\audio.md`。
- 2026-11-18 18:10 重驗，第 692 行再次收到 `state 1 after 2 queries`。紀錄為 `work/Codex-Xiaomi13-GPU-20261109/native-hw000haptic-prefs-codex-1118/boot.log`。
- 新增 ADT `fuxi,audio-spf=3`。收到 SPF state 1 後，沿用同一 `adsp_apps` 通道及既有 `fuxi_ar_prm_vote`，向 PRM port 2 申請一次 LPASS macro 資源。收到回覆後釋放，申請逾時也會嘗試釋放。成功判據為同趟 `FUXI AUDIO PRM: LPASS macro request status=0` 和 `LPASS macro release status=0`。使用 K-077 同趟 loader 的 `--audio-spf --audio-prm`，功放保持預設停用。 [Linux q6prm.c](https://github.com/torvalds/linux/blob/v6.12/sound/soc/qcom/qdsp6/q6prm.c)
- 模擬 DSP 的 ASan／UBSan 已通過，包括請求和釋放封包、錯 token、遠端拒絕與逾時後斷線回復。實機 PRM 回覆、TDM、保護設定和音訊播放仍待驗。測試輸出為 K-077 核心產物的 `logs/test-25.log`。

### K-015 GPR 音訊準備（主機模型）
- **日期**：2026-11-16 14:09（51e7ca9）
- **狀態**：準備（主機模型，未上機）
- **開關**：無
- **問題／現象**：K-014 之前需要先確定 GPR 封包格式、port 與 token 規則，避免上機才發現格式錯。
- **根因**：（準備）
- **做法**：`ksrc\xnu\fuxi-audio-prep\fuxi_audio_prep_gpr.c`／`.h`（GPR 標頭編解碼、APM 命令封包、回覆驗證）與 C 主機測試 `test_gpr.c`。
- **復現**：WSL `python3 tools/test-fuxi-audio-prep.py`。
- **主機測試**：`ksrc\tools\test-fuxi-audio-prep.py`。
- **驗證紀錄**：主機測試通過；K-014 的上機格式由這份模型修正而來。
- **參考來源**：Linux `include/linux/soc/qcom/apr.h`、`audioreach.h`。
- **結論／未完成**：已被 K-014 使用。
- **出處**：ksrc `docs-lanes\audio-preparation.md`。

### K-016 原廠音訊設定擷取
- **日期**：2026-11-17 17:44（824e8c2）
- **狀態**：採用（資料）
- **開關**：無
- **問題／現象**：要建音訊圖，需要原廠的 ACDB 檔名、喇叭 TDM 後端格式和功放 mixer 預設值。
- **根因**：（資料蒐集）
- **做法**：`work\statusbar-fix\fetch-android-audio-config.py` 從 Android（adb，唯讀）搜尋 `/vendor/etc`、`/odm/etc`、`/vendor/odm/etc` 的音訊設定，存到 `work\adsp-battery\android-audio-config-1117\`；ksrc 記錄：ACDB `Tutu_acdb_cal.acdb`，喇叭後端 `TDM-LPAIF-RX-SECONDARY` 48 kHz／2 聲道／24 bit，功放 mixer 預設值。
- **復現**：拓撲資料的既有擷取入口是 `fetch-android-audio-config.py --output <新目錄> --serial <序號>`。2026-11-18 新增 `launch-loader-check.ps1 -CaptureAudioFirmware -Out android-cs35l41-<日期>`，在 Android 讀取同一韌體目錄中的 11 份功放韌體、B/T 校正及 fast-switch 檔。比對 `audio-config.json` 與下述參考 manifest 的檔名及 SHA-256。
- **主機測試**：以 11 份參考檔模擬 Android，確認在三鎖範圍才執行讀取、可選完整的次要路徑，且檔案缺漏、傳輸與雜湊不符、Android 未開完時沒有完成 manifest。原有拓撲擷取也通過模擬。
- **驗證紀錄**：拓撲擷取檔保存在 `android-audio-config-1117`。`work/adsp-battery/cs35l41-fuxi-reference-1118/manifest.json` 保存 11 份檔案的來源、容量、Git blob 雜湊和 SHA-256。TheMuppets `24085d5` 與 xiaomi-8550 `05c474d` 的 11 份 Git blobs 逐檔相符。兩個 `.wmfw` 相同，B/T 的保護與校正 `.bin` 各自不同。
- **參考來源**：Android `audio_platform_info`、`mixer_paths_*.xml`、裝置樹的 B/T fast-switch 清單。 [LineageOS 檔案清單](https://github.com/LineageOS/android_device_xiaomi_fuxi/blob/lineage-23.2/proprietary-files.txt) · [Fuxi 韌體快照](https://github.com/TheMuppets/proprietary_vendor_xiaomi_fuxi/tree/24085d5ae330e335996b67867f5498f7b83aa51a/proprietary/odm/firmware)
- **結論／未完成**：23:34 用上述 launcher 唯讀擷取。手機傳輸前後、主機落地檔及參考 manifest 的 11 項 SHA-256 逐檔相符，見 `work/adsp-battery/android-cs35l41-1118-2334/audio-config.json`。這些是機型共用檔案，另依 Android 的 init.cirrus.rc 用 `-CaptureAudioCalibration` 唯讀取回本機 crus_calr.bin 與 .txt，二進位兩個 little-endian u32 逐一吻合文字的 T/B 聲道數值。證據在 `work/adsp-battery/android-cirrus-cal-1118-2348/`。已授權核對保護後僅以最低音量短測下方喇叭，功放端實際套用與發聲仍待驗。
- **出處**：ksrc `docs-lanes\audio.md`；記憶 `xiaomi13-audio-spf`。

## 顯示、紀錄與注入

### K-017 背光模式 4（AppleARMBacklight＋DCS 0x51）
- **日期**：2026-11-14 22:52（eafc778）；2026-11-16 03:02（7928af8 巡覽）、02:51～03:37 上機
- **狀態**：採用（日常開啟）
- **開關**：`fuxibacklight=<0..4>`（4＝本項；日常命令列含 `fuxibacklight=4`）；`fuxibacklighttrace=1` 記錄每個 IOKit 呼叫。
- **問題／現象**：iOS 的亮度滑桿、自動亮度沒有作用，螢幕固定亮度。
- **根因**：iOS 的 backboardd 找的是 `AppleARMBacklight` 類別服務；ios-fuxi 沒有提供。面板亮度要透過 DSI DCS 命令 0x51（Set Display Brightness）設定，而 DSI 傳輸由顯示擁有者（`fuxi_gpu_scanout.inc`）掌握，不能從別的執行緒直接送。
- **做法**：
  - `xnu\fuxi-backlight\FuxiDsiBacklight.cpp`：發佈 `AppleARMBacklight`（min 0、max 面板最大值），接收 `setProperties` 的亮度值，換算後交給顯示擁有者。
  - `fuxi_dsi_backlight_logic.h`：亮度換算、淡入淡出、合併請求的純函式。
  - `xnu\fuxi-adreno\fuxi_gpu_scanout.inc`：在兩次畫面傳輸之間送 DCS 0x51（不和 scanout 搶 DSI）。
  - 補丁 0098（本機修改）、0109（背光）；`tools\sync-fuxi-backlight.py` 重產。
  - 7928af8：`fuxitour` 巡覽腳本把控制中心亮度滑桿往下、往上各拖一次並截圖（`brightness-low`／`brightness-high`）。
- **復現**：命令列 `fuxibacklight=4`，帶 tour；上機 `-Seconds 240`。預期 console：`FUXI BACKLIGHT DSI: published AppleARMBacklight min=0 max=… value=…` → 拖動時 `FUXI BACKLIGHT DSI: brightness #<n> goal=… sent=… fade_ms=… published=…`。畫面：兩張截圖亮度明顯不同（需肉眼或手機前方拍照，framebuffer 截圖看不到背光）。
- **主機測試**：harness `tests\test-backlight-dsi.py`（workflow `kernel-backlight-dsi.yml`）。
- **驗證紀錄**：11-16 bl4 02:51（路徑通）；bl5t 03:16–03:37：亮度值 1135→65→1329，每個 DCS 46–154 µs。
- **參考來源**：Linux `drivers/gpu/drm/msm/dsi/dsi_host.c`、MIPI DCS 規格 0x51；Kerfand fuxi iOS17 的亮度先例（記憶 `xiaomi13-hardware-reference-projects`）。
- **結論／未完成**：完成；日常「滑兩下就停」由 K-018 修。
- **出處**：journal「總結（03:40）」段；ksrc `docs-lanes\backlight.md`；記憶 `xiaomi13-backlight-mode4`。

### K-018 DSI 錯誤容忍與等滿一幀
- **日期**：2026-11-16 07:02（ba65d38）、07:43（7af418a）
- **狀態**：採用
- **開關**：無（隨 `fuxibacklight=4`）
- **問題／現象**：日常使用時亮度「滑兩下就停」：之後再拖滑桿都沒反應，直到重開機。
- **根因**：第一版只要一個 DCS 命令結果不確定（DSI 錯誤旗標）就永久停用背光；而 CMD DMA 完成等待太短，偶發逾時被當成錯誤。
- **做法**：ba65d38 照 Linux `dsi_error` 的方式讀取並清除 DSI 錯誤狀態，只有連續 6 次錯誤才停用；7af418a 等 `CMD_DMA_DONE` 最長 20 ms（滿一幀）。
- **復現**：同 K-017，但讓 tour 連續拖動滑桿 8 次以上（`fuxitour` 預設）；預期 console 不出現停用訊息，`brightness #` 序號持續增加。
- **主機測試**：harness `tests\test-backlight-dsi.py`。
- **驗證紀錄**：bl6t（11-16 07:19–07:39）約 2900 次 DCS 不停；bl7t 約 3600 次請求，4 次 DMA 完成時間 7.9 ms，核心 `8579ba89…`。
- **參考來源**：Linux `drivers/gpu/drm/msm/dsi/dsi_host.c` 的 `dsi_error` 處理。
- **結論／未完成**：完成。
- **出處**：journal「更正（07:50）」段；記憶 `xiaomi13-backlight-mode4`。

### K-019 獨立開機的 console 紀錄寫入 mtdblk（fx_log）
- **日期**：2026-11-16 07:43（7af418a）～07:46（d96adda）
- **狀態**：採用（日常開啟）
- **開關**：ADT `fuxi,log=1` 或 `fuxilog=1`，且分區已蓋章；沒有章就不寫任何東西。
- **問題／現象**：日常是不接電腦的獨立開機（slot b／常駐選單），沒有 USB 就沒有即時 console，獨立開機才出現的問題（如 K-002 的「100% 充電中」）無從查。
- **根因**：小米 13 沒有 `logdump` 分區（7af418a 原本以為有）。
- **做法**：
  - `xnu\fuxi-unixnu\sweet_ufs.c`（補丁 0106）的 fx_log：寫入 GPT 分區 `mtdblk`（LUN 0，32 MiB；11-16 時全為 0，Android、abl、xbl、uefi 都沒有使用）。
  - 只寫分區開頭 2049 個 4 KiB 區塊：0＝`SWEETLOG` 標頭（版本 2）＋章；1..1024＝環狀緩衝（最後 4 MiB console）；1025..2048＝開頭 4 MiB（環狀繞回後凍結）。其他任何分區、區塊都不寫（`fx_log_write_allowed` 檢查）。
  - 章：Android 端在區塊 0 的 byte 2048 寫 `FUXILOG1 STAMPED`（`work\adsp-battery\hw000\fuxi-log.sh stamp`）。
  - 每 10 秒（`ufslogsecs`）寫回一次；在 ADSP 啟動前開始，保留其啟動過程。
- **復現**：
  1. 主機（Git Bash）跑 `work\adsp-battery\hw000\fuxi-log.sh stamp`（一次即可）：先把整個分區 dd 到手機 `/data/local/tmp/fuxilog-backup.img`、拉回主機並比對 SHA-256，不符就停止，相符才寫章。
  2. loader 的 ADT 寫 `fuxi,log=1`。
  3. 進 iOS 使用後回 Android，`fuxi-log.sh pull` 拉回，`python -X utf8 work\adsp-battery\hw000\fuxi-log-parse.py <拉回的檔>` 解析。
  4. **下次進 iOS 前一定先 pull**，否則被覆蓋；`launch-loader-check.ps1` 每趟開機前會自動拉。
- **主機測試**：無專用測試；寫入範圍由 `fx_log_write_allowed` 限制。
- **驗證紀錄**：bl7t 拉回 799006 bytes；分區原始內容備份在 `D:\xiaomi13\fuxi_Backup\mtdblk-20261116`（sda19）。
- **參考來源**：Linux `drivers/mtd/mtdoops.c` 的環狀紀錄設計（只參考概念）。
- **結論／未完成**：完成。
- **出處**：journal「更正（07:50）」段；記憶 `xiaomi13-standalone-log-mtdblk`。

### K-020 核心在 exec 時注入函式庫（fxinj，補丁 0118）
- **日期**：2026-11-16 09:24（9fa23d5）、09:28（a037bd3）；11-17 13:00（48a46c0，bit 2）、19:23（3d8e39e，wifid）
- **狀態**：採用（日常 `fuxi,inject=2`）
- **開關**：`fxinj=<位元>` 或 ADT `fuxi,inject`（u32，開機時讀一次）。bit 0（1）＝App 主程式（`.../NAME.app/EXECUTABLE`，SpringBoard 除外）注入 `/usr/local/lib/fuxi/libfuxi-app.dylib`；bit 1（2）＝SpringBoard 注入 `/usr/local/lib/fuxi/libfuxi-sb.dylib`；bit 2（4）＝`/usr/sbin/mediaserverd` 注入同一份 `libfuxi-sb.dylib`（震動，K-033）；bit 3（8）＝wifid 依完整路徑注入（Wi-Fi 診斷，K-063）。
- **問題／現象**：SpringBoard 的狀態欄、手電筒按鈕等要改 iOS 行為（U 系列），但在 SpringBoard 的 launchd job 設 `DYLD_INSERT_LIBRARIES` 沒有作用。
- **根因**：launchd（20H392）會丟掉 job `EnvironmentVariables` 裡所有 `DYLD_` 變數（`DYLD_PRINT_*` 除外，log：`Disallowing environment variable: %s`），只有 label 不是 `com.apple.` 開頭的 job 才放行。
- **做法**：補丁 `xnu\patches\0118-fuxi-exec-inject.patch`、`xnu\fuxi-inject\fuxi_exec_inject.h`（純函式，不用 `strrchr`）：exec 時依路徑判斷，把變數加進新行程的環境。dyld 會接受，因為 H1 AMFI 的 dyld policy 回答（補丁 0103，請求 0x5a）全部放行（函式庫不存在也不會失敗）。移植自 GPU 線的 0121（去掉啟動加速 hook）。概念同 HackiOS（hakios-xnu-kernel `xnu/hakios.patch`，`kern.exec_inject`，MIT）。
- **復現**：ADT `fuxi,inject=2`，並把 `libfuxi-sb.dylib` 放進 root（`launch-loader-check.ps1 -Dylib <檔> -DylibSha <SHA256>` 會暫存安裝）。預期：SpringBoard 的 log 有函式庫自己的啟動訊息（U 系列各項）；`launchctl` 不需改。
- **主機測試**：`ksrc\tools\test-fuxi-exec-inject.py`。
- **驗證紀錄**：11-16 狀態欄 shape 各趟（U 系列）；11-17 震動（mediaserverd）、Wi-Fi 稽核（wifid）各趟。
- **參考來源**：HackiOS `kern.exec_inject`（MIT）；GPU 線補丁 0121。
- **結論／未完成**：完成；日常 ADT `fuxi,inject=2`。
- **出處**：journal「Codex 接續（11-16 10:04 起）」段。

## RAM

### K-021 9 GiB RAM 配置（ram9g 變體）
- **日期**：2026-11-16 10:19（fee0512）；12:21 起上機；16:10:51 隨 chain-v6 進日常
- **狀態**：採用（日常）
- **開關**：建置期變體，不是開機參數：harness `xnu\variants\ram9g.patch`、workflow `kernel-adsp-ram9g.yml`（Intel runner 版 `kernel-adsp-ram9g-intel.yml`）；本機 `build-kernel.ps1 -Ram 9g`。loader：`prepare-ram9g-loader.py --ram-layout 9g`（預設）。
- **問題／現象**：原配置 XNU 只用 5 GiB（5088 MiB），12 GB 機型大半記憶體閒置，App 容易被 jetsam 收掉。
- **根因**：ios-fuxi 的記憶體窗口是保守的單一 5 GiB 區段；GPU scratch 放在窗口緊接處，擴大窗口會撞上。
- **做法**：`ram9g.patch` 把 XNU 可用區改成相鄰 3+6 GiB（fresh FDT 已驗證不跨保留區）：XNU usable `0x23e000000`（9184 MiB），GPU scratch 移到 `0xafe000000`；USB 讀取改為有界。`tools\mkadt-fuxi.py`、`tools\ci\kernel-inputs.py`、`tools\ci\prepare-kernel.py` 支援兩種配置。
- **復現**：本機 `build-kernel.ps1 -Ram 9g -Name kernel-local-<名>-9g-artifact`；`python -X utf8 work\adsp-battery\prepare-ram9g-loader.py <名> <來源 loader> <核心產物>`。預期：開機進 SpringBoard，console 的記憶體統計（free＋wired 頁數）大於 5g 配置。loader 產生器會核對韌體、payload、核心、root extents 與 RAM 一致性。
- **主機測試**：`ksrc\tools\test-fuxi-ram9g.py`。
- **驗證紀錄**：CI `43791547599`（harness `36c962d`）成功，40 項 manifest 全通過；新 loader `native-ram9g-tour-user-loader` 檢查全過；12:21:48 第八趟 RAM 樣本（K-023）通過；12:32:01 第九趟整顆 9g（`chain-v5-ram9g`，boot SHA-256 `b7a81bd0…`）見 582815 free＋4961 wired 頁、SpringBoard hook 與 ADSP 電量發佈，12:38:51 回 Android、無 panic（當時未部署）。之後所有 9g 趟次與 chain-v6 起的日常都用此配置。
- **參考來源**：fuxi live FDT `/memory` 與 `reserved-memory`。
- **結論／未完成**：9 GiB 是 12 GB 機型已核對的配置，不是硬體上限；更多 RAM 需要多段 admission（K-024）。
- **出處**：journal「Codex 接續（11-16 10:04 起）」段。

### K-022 RAM SKU 與實際配置分開
- **日期**：2026-11-16 16:28（6166ed6）
- **狀態**：採用
- **開關**：loader `--ram-sku auto|8g|12g`（預設 auto）→ ADT `fuxi,ram-sku`；ios-fuxi 入口的 incoming-FDT guard。
- **問題／現象**：「5g／9g」是 XNU 的配置名，不代表手機是 8 GB 還是 12 GB 機型；拿 12 GB 配置開 8 GB 機會壞。
- **根因**：原本把配置名當成硬體規格。
- **做法**：6166ed6 把實體 SKU（live FDT `/memory/ddr_size`，四個 bytes，`0000000c`＝12 GB）與允許的配置分開；loader 產生器寫 ADT `fuxi,ram-sku` 並在 SKU 不符時拒絕。ios-fuxi 本機提交 `5f04f26` 加 incoming-FDT guard：入口核對 SKU 與配置，相符才印 `RAM_PROFILE_OK12_9G`。
- **復現**：loader 用 `--ram-sku auto`；預期 console：`RAM_PROFILE_OK12_9G`。
- **主機測試**：`ksrc\tools\test-fuxi-ram-sku.py`；`work\boot-chain\test-loader-sku-preflight.py`、`test-runtime-ram-profile.py`；private live FDT 重播與 423 cases／2966 assertions（ASan），入口 smoke 5g／9g 各 6 cases／46 assertions。
- **驗證紀錄**：第 20 趟暫時 loader 通過 `RAM_PROFILE_OK12_9G` 並進 SpringBoard。目前實機是 12 GB SKU。
- **參考來源**：無。
- **結論／未完成**：8 GB 實機的拒絕路徑與 ARM fault／reset 行為未上機驗；日常 chain-v6 當時尚未更新此 guard。
- **出處**：journal「歷史驗收摘要」段（L76、L86）。

### K-023 高位址 RAM 樣式測試（ramtest）
- **日期**：2026-11-16 11:57（d4bc0d0）；12:21、第 13 趟上機
- **狀態**：實驗（已驗，按需開）
- **開關**：ADT `fuxi,ramtest`；loader `prepare-ram9g-loader.py --ramtest [--ramtest-mib 128|512]`。
- **問題／現象**：9 GiB 配置要證明新增的高位址記憶體真的可讀寫，不只是開得了機。
- **根因**：（驗證工具）
- **做法**：`xnu\fuxi-adsp\fuxi_ramtest.c`、`fuxi_ramtest_pattern.h`：開機後配置指定大小，只接受舊 5 GiB 窗口以上的頁；寫四組樣式、清 cache 後讀回比對，結束釋放。實體位址落在配置之外或配置被拒就不寫。
- **復現**：`prepare-ram9g-loader.py <名> <來源> <核心> --ramtest --ramtest-mib 512`；上機 `-Seconds 180`。預期 console：`FUXI RAMTEST: allocated bytes=… pages=… high-pages=… physical=…` → 四次 `FUXI RAMTEST: pass=<n> … cache-evicted verified` → `FUXI RAMTEST: released bytes=… completed=4/4 coverage=allocated-sample`。失敗行：`FUXI RAMTEST: FAIL pass=…`、`no pages above old 5GiB aperture`。
- **主機測試**：`ksrc\tools\test-fuxi-ramtest.py`。
- **驗證紀錄**：第八趟 `native-hw000ram9g-pattern-boot-1`（12:21:48–12:28:58）：128 MiB、8192 頁全在高位址 `0xa1f0bc000..0xa27104000`，4/4 通過（`boot.log` 第 2849–2858 行）。之後 512 MiB：32768 頁 `0xa1b21c000..0xa3b690000`，4/4 通過。
- **參考來源**：無。
- **結論／未完成**：只是配置樣本，不能當全 9 GiB 完整性或長時間壓力驗收。
- **出處**：journal「Codex 接續（11-16 10:04 起）」段。

### K-024 多段 RAM admission、bank shadow 與記憶體盤點
- **日期**：2026-11-16 14:09（51e7ca9）、15:24（a948742）、16:17（27bb0cf）；16:05 Android 擷取
- **狀態**：準備（主機模型與資料，核心未用）
- **開關**：無
- **問題／現象**：想超過 9 GiB 要把 Android 保留區之間的零碎區段也交給 XNU，必須先知道哪些區段真的空著、誰擁有。
- **根因**：DT 的 reserved-memory 只列靜態保留；動態配置（`size` 請求）與 live owner 不在 DT 裡。
- **做法**：
  - `ksrc\tools\fuxi-ram-inventory.py`：從 DT 列出候選區段；`fuxi-ram-admission.py`：依規則決定可交給 XNU 的區段。
  - `ksrc\xnu\fuxi-ram-prep\fuxi_ram_bank.c`：bank shadow admission 的純 C 模型（`test_fuxi_ram_bank.c`）。
  - `work\statusbar-fix\inspect-android-memory.py`：Android 下唯讀擷取 `/proc/iomem`、`/proc/meminfo` 等，存 `work\adsp-battery\android-memory-20261116-1606\memory.json`（參數 `--output <資料夾>`）。
- **復現**：`python -X utf8 work\statusbar-fix\inspect-android-memory.py --output work\adsp-battery\android-memory-<日期>`（Android、USB）；WSL `python3 tools/fuxi-ram-inventory.py <dtb>`、`tools/fuxi-ram-admission.py`。
- **主機測試**：`test-fuxi-ram-inventory.py`（20/20）、`test-fuxi-ram-admission.py`、`test-fuxi-ram-bank.py`。
- **驗證紀錄**：inventory：已知排除以外 15 段共 2228.56 MiB 候選，另有 772.00 MiB 動態 size 請求及未知 placement／live owner，不能直接當可用量。Android 擷取（16:05:37）：14 段 System RAM union 11491.55 MiB、MemTotal 11190.55 MiB，都大於目前 XNU 窗口；兩個 memblock 介面不存在，不能解讀為零保留。
- **參考來源**：Linux `mm/memblock.c`、`/proc/iomem` 格式。
- **結論／未完成**：真正的 XNU 多段 admission 未做。
- **出處**：journal「Codex 接續（11-16 10:04 起）」段；ksrc `docs-lanes\ram-multirange-preparation.md`、`ram-handoff-owner-contract.md`、`ram-android-owner-20261116.md`、`ram-admission-checkpoint.md`。

## PMIC：手電筒與震動

### K-025 PMIC 唯讀盤點（flash／haptics）
- **日期**：2026-11-16 10:49（286ddc5）
- **狀態**：採用（診斷）
- **開關**：`fuxipmic=1` 或 ADT `fuxi,pmic=1`；loader `prepare-ram9g-loader.py --pmic-survey`。值大於 3 時拒絕並記 `unsupported mode`。
- **問題／現象**：手電筒（flash LED，SID 1）和震動（HAP525，SID 7）都在 SPMI 上的 PMIC，先要知道 Android 留下的暫存器狀態和 SPMI 通道擁有權。
- **根因**：（盤點）
- **做法**：`xnu\fuxi-adsp\fuxi_pmic.c`：映射 SPMI PMIC arbiter（core `0x0c400000`、config `0x0c42c000`、observer `0x0c440000`），讀 arbiter 版本與 APID 數，盤點 flash（SID 1 `0xee00`）、haptic config／pattern／boost（SID 7 `0xf000`／`0xf100`／`0xf200`）的暫存器與 APID owner；只讀。
- **復現**：`--pmic-survey`，上機 `-Seconds 120`。預期 console：`FUXI PMIC: arbiter=0x7… APIDs=… mode=1 haptic=0`、`FUXI PMIC: flash sid=1 base=0xee00 APID=… owner=… survey-only`、各 `+xx=yy rc=0` 行。
- **主機測試**：`ksrc\tools\test-fuxi-haptic-survey.py`（同時涵蓋 init 分派）。
- **驗證紀錄**：11-16 盤點趟完成；之後 K-026～K-033 都建立在這份資料上。
- **參考來源**：Linux `drivers/spmi/spmi-pmic-arb.c`、`drivers/leds/flash/leds-qcom-flash.c`、`drivers/input/misc/qcom-hv-haptics.c`（downstream）。
- **結論／未完成**：完成。
- **出處**：journal「Codex 接續（11-16 10:04 起）」段；ksrc `docs-lanes\pmic-torch.md`、`pmic-haptics.md`。

### K-026 手電筒有界脈衝診斷（mode 2）
- **日期**：2026-11-16 11:43（816ab9e）
- **狀態**：實驗（保留作診斷）
- **開關**：`fuxi,pmic=2`；loader `--pmic-torch-pulse`。
- **問題／現象**：要先確認 AP 能安全地點亮 flash LED：電流、硬體計時器、關閉都要可控。
- **根因**：（診斷）
- **做法**：`fuxi_torch_pulse.h`：通道 0、40 mA、80 ms 的單次脈衝，依賴 PMIC 硬體計時器自動截止；寫前檢查 APID owner，寫後讀回。
- **復現**：`--pmic-torch-pulse`，上機 `-Seconds 120`。預期 console：`FUXI PMIC: flash pulse begin channel=0 current=40mA duration=80ms hardware-timer=…` → `FUXI PMIC: flash pulse result=0`；被拒時 `flash pulse refused APID=… owner=…`。
- **主機測試**：`ksrc\tools\test-fuxi-torch-pulse.py`。
- **驗證紀錄**：Torch 5g／9g CI `43799939050`／`43799939063` 成功（9g 40 項 manifest 全過）；第六趟 `flash pulse result=0 registers restored`，ADSP RPMh 啟動與降票同時完成。只證明 40 mA／80 ms 的暫存器序列與還原成功，未確認可見亮光。
- **參考來源**：Linux `leds-qcom-flash.c`。
- **結論／未完成**：mode 2 保留有限脈衝，作為硬體檢查。
- **出處**：journal「Codex 接續（11-16 10:04 起）」段。

### K-027 手電筒租約控制（mode 3）與 SpringBoard 放行
- **日期**：2026-11-16 12:30（e390fe4）、12:38（be26a95）；13:19、13:47 上機
- **狀態**：採用（日常 ADT `fuxi,pmic=3`，chain-v7 起）
- **開關**：`fuxi,pmic=3`；loader `--pmic-torch-runtime`。
- **問題／現象**：控制中心的手電筒按鈕要能開關、調亮度，需要 iOS 可呼叫的介面。
- **根因**：iOS 的手電筒走相機驅動堆疊，fuxi 沒有相機驅動；需要另開一個由 SpringBoard dylib 呼叫的核心入口（dylib 見 U 系列）。
- **做法**：`fuxi_torch_runtime.h`：sysctl `kern.fuxitorch`（0..4；0＝關，1..4＝20／40／60／80 mA），每次寫入給 2 秒租約，逾時自動關；失敗後鎖存（failed latch），讀回 EIO。be26a95：呼叫者為 root，或 platform binary 且行程名是 SpringBoard 才可寫（`fuxi_torch_caller_allowed`）。同提交擴充 haptics 盤點。
- **復現**：`--pmic-torch-runtime`，搭配 U 系列的手電筒 dylib；上機 `-Seconds 240`。預期 console：`torch steady runtime ready`（K-030 之後的字樣）；在控制中心開手電筒後 `sysctl kern.fuxitorch` 讀回 1..4。
- **主機測試**：`ksrc\tools\test-fuxi-torch-runtime.py`。
- **驗證紀錄**：第 10 趟（13:19）控制中心沒有手電筒按鈕；第 11 趟（13:47）按鈕出現，ADT tap 後援指令生效（`using ADT command (22 bytes)`）。之後持續亮光的失敗見 K-028。
- **參考來源**：Linux `leds-qcom-flash.c` 的 torch 模式。
- **結論／未完成**：介面可用；持續亮光直到 K-029／K-030 才解決。
- **出處**：journal「Codex 接續（11-16 10:04 起）」段；ksrc `docs-lanes\pmic-torch.md`。

### K-028 手電筒失敗追查（關閉穩定、計時器、audit、原始拒絕）
- **日期**：2026-11-16 13:53～19:02（b6ce046、5b0f00e、b274554、a066a05、272620c）
- **狀態**：已結案（根因見 K-029）
- **開關**：ADT `fuxi,torchaudit=1`（只在 `fuxi,pmic=3` 時有效）；loader `--torch-audit`。
- **問題／現象**：mode 3 下持續請求 80 mA，脈衝跑一陣就鎖停，每趟停的位置和錯誤碼都不同：第 12 趟 64 次後 `rc=-2`；第 13 趟 12 次後 `rc=-6`；第 14 趟 9 次後 `rc=-6`；第 16 趟 8 次＋restore `+0x42 rc=-1`；第 18 趟第一次就 `rc=-5`（post-disable `+0x09=0x10`）；第 24 趟計時器 `0x89` 下 21 次後 observer read 回 `status=0x9`（DONE＋DROPPED，SID1 `0xee46`、APID 591、owner 0）。
- **根因**：最後確定是 SPMI 交易被同時存取的其他 PMIC 使用者打斷（DROPPED），見 K-029。過程中排除了熱保護、計時器值、關閉順序。
- **做法**：
  - b6ce046：有界記錄 torch 失敗。
  - `fuxi_torch_audit.h`：失敗脈衝最多 64 筆 I/O（R/W、sleep 前後、耗時），控制序列與保護不變。
  - 5b0f00e：關閉後的穩定等待有上限，計時器保留。
  - b274554：回報 PMIC 原始交易拒絕的狀態。
  - a066a05：計時器改用較保守的解碼值；272620c：記錄計時器讀回驗證與鎖存的 SPMI dropped read。
  - 計時器值爭議：Linux v6.12 `leds-qcom-flash.c:set_flash_timeout()` 是 `timeout_ms / 10` 加 bit 7（100 ms→`0x8a`）；比對到的 OEM 來源是 `timeout/10-1`（→`0x89`）。目前核心 `fuxi_torch_pulse.h` 用 `0x89`（保守，OEM 解讀 100 ms、mainline 解讀 90 ms），仍保留 80 ms 等待、限流、2 秒租約與失敗鎖存。實際硬體時間未用儀器量。
- **復現**：`--pmic-torch-runtime --torch-audit`；在控制中心開手電筒 120 秒。失敗時 console：`FUXI PMIC: flash write rejected +xx APID=… status=… failure=…`、`flash write timeout`，以及最多 64 行 audit。
- **主機測試**：`test-fuxi-torch-audit.py`、`test-fuxi-torch-runtime.py`。
- **驗證紀錄**：第 12–24 趟如上（各趟紀錄在 `work\Codex-Xiaomi13-GPU-20261109\native-hw000torch*`）；最高溫 48.5°C 上下，不是熱保護。
- **參考來源**：Linux v6.12 `leds-qcom-flash.c`；OEM kernel 對應檔。
- **結論／未完成**：移交 K-029。
- **出處**：journal「Codex 接續（11-16 10:04 起）」段（第 12–24 趟）；ksrc `docs-lanes\torch-timer-source-checkpoint.md`、`torch-timeout-status-research.md`。

### K-029 PMIC SPMI 交易共用仲裁鎖
- **日期**：2026-11-16 19:37（b48ec0f）；20:00、20:21 上機
- **狀態**：採用
- **開關**：無（PMIC 任一模式）
- **問題／現象**：K-028 的隨機鎖停。
- **根因**：手電筒、震動和核心其他 SPMI 使用者（RTC、按鍵等，`sweet_spmi` 匯流排）各自操作 PMIC arbiter，沒有共用鎖；交易互相插隊時 observer 回 DROPPED。
- **做法**：`fuxi_pmic.c` 的 flash／haptic SPMI 交易改用共用的 `sweet_spmi_bus_lock`（`sweet_spmi_bus.h`）。
- **復現**：`--pmic-torch-runtime --torch-audit`，控制中心請求 80 mA 120 秒。預期：脈衝持續到結束，0 次失敗。
- **主機測試**：`test-fuxi-torch-runtime.py`。
- **驗證紀錄**：第 28 趟 `native-hw000torchlock-120s-boot-1`（20:00:41–20:07:32，最高 52.8°C）：脈衝到第 768 次以上、0 失敗；第 30 趟 `native-hw000torchlock-120s-boot-2`（20:21:22–20:28:11，最高 50.9°C）結果相同。
- **參考來源**：Linux `spmi-pmic-arb.c` 的 `pmic_arb->lock`。
- **結論／未完成**：完成；脈衝式在使用者眼中仍會閃，改成常亮見 K-030。
- **出處**：journal「Claude 接續（19:10 起）」段。

### K-030 手電筒常亮
- **日期**：2026-11-16 22:14（3d41b16）；22:00、22:58 上機
- **狀態**：採用
- **開關**：無（`fuxi,pmic=3` 的行為）
- **問題／現象**：22:00 手動測試（`native-ram9g-torchmanual-sb127-user-loader`，無 tour／tap）使用者看到換檔時閃。
- **根因**：mode 3 原本用一連串有硬體計時器的脈衝模擬常亮，脈衝之間有空檔。
- **做法**：`fuxi_torch_steady.h`：輸出持續保持，直到停止請求或 2 秒租約到期（由 dylib 續約）。
- **復現**：loader `native-ram9g-torchsteady-sb127-user-loader` 的作法：`prepare-ram9g-loader.py … --pmic-torch-runtime --rtc-calibration <json> --rtc-adt`，不帶 tour；上機 540 秒、Reset 10 分鐘，使用者親手開關。
- **主機測試**：`ksrc\tools\test-fuxi-torch-steady.py`。
- **驗證紀錄**：22:58:06 `native-hw000torchsteady-user-boot-1`（CI 9g Intel `43886498954`，核心 SHA256 `0513709d…`，boot SHA256 `a141afa5…`）：常亮 OK，但調亮度時閃。
- **參考來源**：Linux `leds-qcom-flash.c` torch 模式（非 flash 模式）。
- **結論／未完成**：調光閃爍由 K-031 修。
- **出處**：journal「Codex 再接續（11-16 21:31 起）」段。

### K-031 手電筒調光不重開輸出與五檔
- **日期**：2026-11-16 23:13（e9a1f57）；23:44 上機；11-17 00:03:58 五檔驗收
- **狀態**：採用（日常 chain-v7 起）
- **開關**：無
- **問題／現象**：K-030 常亮後調整亮度會閃一下；另外 iOS 的 3、4 檔沒有差別。
- **根因**：調光時先關輸出再以新電流打開。iOS 16 對手電筒送的強度值是 0.85／1.0，dylib 舊算法 `ceil(value*4)` 把兩者都算成第 4 檔。
- **做法**：e9a1f57 在輸出開著時直接更新電流暫存器，不切換輸出。檔位對應由 harness `7034f05`（dylib，U 系列）改為 0／0.25／0.5／0.85／1.0 與核心 0–4 雙向對應（20／40／60／80 mA 不變）。
- **復現**：loader `native-ram9g-torchdim-sb127-user-loader`（581 bytes 命令列，手動）。五檔：本機 9g 核心＋`7034f05` dylib，上機時在控制中心逐檔切換。
- **主機測試**：`test-fuxi-torch-steady.py`；harness `tools\test-flashlight-levels.c`（ASan／UBSan）。
- **驗證紀錄**：23:44 `native-ram9g-torchdim-sb127-user-loader`（CI `43896202141`，核心 `adb03512…`，boot `e570b49c…`）調光不閃。11-17 00:03:58–00:08:50 `native-hw000local-torchlevels-user-boot-1`（本機 9g 核心，第一次上機的本機核心；dylib CI `43902171532`）：113 次切換涵蓋五檔，使用者回報「手電筒可以了 沒問題」。
- **參考來源**：iOS 16 AVFlashlight 強度值（反組譯觀察）。
- **結論／未完成**：完成。mode 3 不宣稱在核心或 SPMI 失效時有硬體自主截止。
- **出處**：journal「歷史驗收摘要」段（L79）。

### K-032 HAP525 震動盤點與有界脈衝
- **日期**：2026-11-16 14:09（51e7ca9）、16:36（acb3fcd）；第 13、20 趟上機
- **狀態**：實驗（只讀；已被 K-033 取代為實際震動）
- **開關**：`fuxihaptic=1` 或 ADT `fuxi,haptic=1`；loader `prepare-ram9g-loader.py --haptic-probe`。
- **問題／現象**：要讓 iOS 震動，先確認 PM8550B HAP525 的狀態、共振週期與能否自主停止。
- **根因**：（盤點）
- **做法**：`fuxi_haptic_pulse.h`：檢查 HAP525 identity／owner／fault／idle／boost follow；PATTERN1 自主停止未驗證前不寫（writes=0）。
- **復現**：`--haptic-probe`，上機 `-Seconds 120`。預期 console：`FUXI PMIC: haptic result=… probed=1 TLRA=5710us start-attempted=0 …`。
- **主機測試**：`test-fuxi-haptic-pulse.py`、`test-fuxi-haptic-survey.py`。
- **驗證紀錄**：第 13 趟 `result=-11 probed=1 TLRA=5710us start-attempted=0 writes=0`；第 20 趟兩輪唯讀 identity／selector／TLRA 有效（5710 µs），FIFO 狀態不可用、cutoff 未驗。研究確認 CFG `0x4a` bit 2 為 FIFO hold（當時 `0x84` 已開）。
- **參考來源**：downstream `qcom-hv-haptics.c`。
- **結論／未完成**：由 K-033 的 DIRECT_PLAY 取代。
- **出處**：journal「Codex 接續（11-16 10:04 起）」段；ksrc `docs-lanes\pmic-haptics.md`、`haptic-finite-research.md`。

### K-033 HAP525 DIRECT_PLAY 震動執行期

- 11-19 16:09，HAP525 實機的兩個 EE0 周邊編號為 631 與 632，arbiter 共 768 項。原程式每次讀寫 FIFO 都重新掃遍映射。參考 Linux v6.12 `spmi-pmic-arb-v6.12.c` 的 PPID→APID 快取，以 `--haptic-fast-apid` 開啟，沿用初始完整盤點找到的兩個通道，每筆交易重新核對 PPID、owner，並保留隔離、實體映射及匯流排鎖檢查。以 WSL 執行 `python3 work/adsp-battery/test-haptic-apid.py` 驗正常、改 owner、改 PPID、編號越界、其他周邊與未啟用模式。本機核心 `kernel-local-hap-apid-1119-9g-artifact` 通過 40 項 workflow 測試。
- `native-hw000-hap-apid-1119/boot.log` 第 409、4845–4846 行。長按後兩筆 132 bytes，排隊 10 µs／13,037 µs，處理 17,599／17,450 µs，兩筆 rc=0，累計 played=2、expired=0。前一版同手勢的第二包逾時。腳本送三次長按，只有第一段看到引擎啟動，這是兩包的一次樣本。主觀手感與計時器密集刻度仍待驗收。暫時開機使用 `work/statusbar-fix/launch-loader-check.ps1 -Loader native-hap-apid-1119-loader -Kernel kernel-local-hap-apid-1119-9g-artifact -Out native-hw000-hap-apid-1119 -Seconds 154 -Reset 4`。
- 11-19 17:01–17:06 重驗三次長按（loader `native-amp-run-hap-1119-loader`，本機核心 `kernel-local-amp-run-1119-9g-artifact`，同樣帶 `--haptic-fast-apid`；與 K-078 mode 6 同一趟）。前一趟的關閉點擊落在選單項目上，這趟改用 home 手勢 `s540,2390,540,1700,120` 關選單。
  - 三次長按都走完整條路：SpringBoard `_UIClickPresentationFeedbackGenerator` 請求 → `_UIFeedbackCoreHapticsEngine` 播放 → 核心送出 132 bytes UI PCM → CoreHaptics 結束 `error-present=0`。
  - 三次分別送出 2、1、2 筆，全部 rc=0，累計 played=5、expired=0；處理時間 17,070–17,922 µs。本趟沒有出現 `-4810`。
  - 證據：`work/Codex-Xiaomi13-GPU-20261109/native-hw000-amp-run-hap-1119/boot.log` 第 4954–4972、5057–5065、5155–5165 行。手感仍待使用者驗收。
  - 指令：`launch-loader-check.ps1 -Loader native-amp-run-hap-1119-loader -Kernel kernel-local-amp-run-1119-9g-artifact -Out native-hw000-amp-run-hap-1119 -Seconds 154 -Reset 4`。loader 由 K-078 mode 6 的指令再加 `--haptic-fast-apid --tap-delay 65 --tap-script 's540,2390,540,999,99;w1000;s148,570,148,570,750;w1400;s540,2390,540,1700,120;w1400;s148,570,148,570,750;w1400;s540,2390,540,1700,120;w1400;s148,570,148,570,750;w1400;s540,2390,540,1700,120'` 產生，boot SHA-256 為 `7580e9bcbc59cb75e3931da9f1abbe4641d70a2b0a04b5f7d79057827ec80dec`。
- **日期**：2026-11-17 13:00（48a46c0）、13:01（6206698）、22:17（05ed0fa 強度）
- **狀態**：採用（日常 ADT `fuxi,haptic=2`）
- **開關**：ADT `fuxi,haptic=2`（3＝另加開機自測三下）；`fuxi,haptic-amp=<0..255>`（dylib 讀，見 U 系列）；ADT `fuxi,inject` bit 2 讓 mediaserverd 載入 dylib。loader：`prepare-wlan-w18-loader.py --haptic 2 --inject-media [--haptic-amp 128]`。
- **問題／現象**：iOS 的觸覺回饋、來電與充電震動都沒有。
- **根因**：iOS 的震動走 mediaserverd 的 haptic server 與 AppleHapticsSupport 驅動，fuxi 沒有；需要核心介面加 dylib 橋接。
- **做法**：
  - `fuxi_haptic_runtime.h`：映射 haptic CFG／PTN 通道，檢查 Android 留下的狀態（module 開、SPMI_PLAY `0x01` direct play、振幅與 VMAX ≤ 1400 mV、自動共振開、idle、boost 跟隨硬體）；sysctl `kern.fuxivibrate`（0..2000 ms；root 或 platform 的 SpringBoard／mediaserverd 可寫）。播放寫 `0x81`，停止寫 `0x01` 並輪詢 HPWR_INTF 等關閉（照 Linux 先切一次 module 再鎖存關閉）；播放中每 5 ms 檢查 fault。
  - 05ed0fa：sysctl 值的 bits 16..23 帶 HAP525 DIRECT_PLAY 振幅暫存器（`0xf126`）的值；0 表示不改暫存器，舊呼叫者行為不變。開始前與播放中寫入並讀回；VMAX 永不改動；ready 時記錄 Android 留下的振幅與 VMAX。
  - iOS 端（mediaserverd／SpringBoard 的 dylib）把震動模式轉成 sysctl 呼叫，見 U 系列。
- **復現**：`prepare-wlan-w18-loader.py build --like <已驗> --artifact <核心> --name <新> --haptic 3 --inject-media`；上機 `-Seconds 240`。預期 console：開機自測 3 次 `fault=0 stop-rc=0`；觸發震動時 `kern.fuxivibrate` 寫入紀錄。
- **主機測試**：`test-fuxi-haptic-runtime.py`（含 haptic=4 拒絕）；harness `tools\test-vibe-pattern.c`、`test-vibe-worker.py`。
- **驗證紀錄**：vibe6 14:06:34–14:14:15（`native-vibe5-ram9g-sb127-loader`／`kernel-local-vibrate-9g-artifact`）：核心三次脈衝 fault=0／stop-rc=0，iOS 直接震動入口回 `!itl`、無 AVHapticServer 播放，最高 74.6°C。vibe7 14:31:19–14:38:00（dylib `c4dbd88`，CI `44031165852`）：MediaExperience bridge 安裝，一般震動與自訂節奏到核心，9 次播放停止成功，`!itl` 改回 0。vibe8 14:51:38–14:55:26（dylib `101e39d`，CI `44033584532`）：Atoms Minitap 15 ms 實測 15.530 ms，2000 ms 請求提前取消實測 120.850 ms，10 次播放 fault=0。使用者確認充電震動有感。
- **參考來源**：downstream `qcom-hv-haptics.c`；Linux `drivers/input/misc/` HAP 驅動。
- **2026-11-18 自動共振錯誤鎖死（ksrc a4d9402）**：
  - 現象：hapticfb3 趟（16:44，`native-hapticfb3-1118-loader`，核心 `kernel-local-wifi-password-prompt-1118-9g-artifact`）開機自測三下正常，但第一次低振幅播放（`ms=40 level=88`）約 47 ms 後 `fault=0x2`，核心記 `runtime locked until reboot`，之後 mediaserverd／SpringBoard 每次寫入都 `errno=5`（boot.log 第 3067–4538 行）。
  - 根因：FAULT_STATUS（`0xf00c`）bit 1 是 AUTO_RES_ERROR；低 DIRECT_PLAY 振幅（約 0x08..0x30）會讓自動共振追不到而設這個 bit。原本任何 fault 都當致命錯誤鎖死執行期。
  - 做法：照 Linux `qcom-hv-haptics` 的處理，播放中只出現 AUTO_RES_ERROR 時繼續播，下一次播放前寫 FAULT_CLR（`0xf066`，寫 1 清除）清掉；短路（SC）、HPWR_RDY 與未知 bit 仍停止並鎖死。新增計數 `autores-plays=<n> cleared=<m>`。主機測試 `test-fuxi-haptic-runtime.py` 加了 AUTO_RES 繼續播放、清除與其他 fault 仍鎖死的案例。
  - 驗證：btsetup 趟（17:20，`native-hw000btsetup-1118`，與 K-076 同一趟，核心 ksrc b5b540a 含 a4d9402）boot.log 第 2918–2938 行：振幅 0x8 播放 25 ms 與 76 ms 時各出現一次 `auto-resonance error while playing … continuing, cleared before next play`，計數 `autores-plays=2 cleared=2`；之後 play=6..13（400／200／395／15／15／15／400／122 ms）全部 `fault-mid=0 stop-rc=0`，沒有 `runtime locked until reboot`，mediaserverd／SpringBoard 也沒有 `errno=5`。SpringBoard 的 UIKit 自測（impact／selection generator）各產生一次 15 ms 播放（play=10、11）。
- **結論／未完成**：CoreHaptics `-4810` 仍未解（U 系列）。
- **出處**：journal 11-17 段（L93–L95）；11-18 hapticfb3 趟 `work/Codex-Xiaomi13-GPU-20261109/native-hw000hapticfb3-1118/boot.log`。

### K-034 震動 FIFO 準備（主機模型）
- **日期**：2026-11-16 15:24（a948742）
- **狀態**：準備（未接上核心）
- **開關**：無
- **問題／現象**：複雜震動波形需要 HAP525 的 FIFO 模式；先在主機上建模。
- **根因**：（準備）
- **做法**：`ksrc\xnu\fuxi-haptic-prep\fuxi_haptic_fifo.c`／`.h` 與 `test_fifo.c`：FIFO 填充、水位、停止條件的純 C 模型。
- **復現**：WSL `python3 tools/test-fuxi-haptic-fifo.py`。
- **主機測試**：`test-fuxi-haptic-fifo.py`。
- **驗證紀錄**：主機測試通過。
- **參考來源**：downstream `qcom-hv-haptics.c` FIFO 路徑。
- **結論／未完成**：DIRECT_PLAY（K-033）已滿足目前需求，FIFO 未接。
- **出處**：ksrc `docs-lanes\haptic-fifo-preparation.md`。

## 測試自動化、RTC、藍牙

### K-035 裝置端 UI 測試指令（taps、tour、tapcmd）
- **日期**：2026-11-14 21:29（6fc377f）、11-16 03:02（7928af8）、13:14（aab893e）
- **狀態**：採用（測試工具）
- **開關**：`fuxitaps=<延遲秒>`、`fuxitour=<…>`（`fuxitoura`、`fuxitourg`、`fuxitourhold`）、`fuxitapcmd=<指令>` 或 ADT `fuxi,tapcmd`（字串）；loader `--tap-script <指令> [--tap-script-adt]`、`--tap-delay`。
- **問題／現象**：上機驗證要在 iOS 裡點開設定、拖滑桿、截圖，但沒有 USB 輸入通道。
- **根因**：（測試工具）
- **做法**：`xnu\fuxi-h1\fuxi_keys.c`（補丁 0103）：開機後依預設腳本或指令字串，透過觸控／HID 注入點擊、拖曳、按鍵與截圖標記。6fc377f：預設 @3 從主畫面第一頁開設定；7928af8：tour 拖控制中心亮度滑桿上下各一次（`brightness-low`／`brightness-high`）；aab893e：命令列滿了改從 ADT `fuxi,tapcmd` 讀有界指令。
- **復現**：`prepare-wlan-w18-loader.py … --tap-script "<指令>" --tap-delay 85`；預期 console：`using ADT command (<n> bytes)`、`FUXI KEYS: inject …`。
- **主機測試**：`ksrc\tools\test-fuxi-tap-command.py`；截圖流程 `work\statusbar-fix\test-launcher-framebuffer.ps1`。
- **驗證紀錄**：第 11 趟（13:47）ADT 後援指令生效（22 bytes）；各 tour 截圖趟。
- **參考來源**：無。
- **結論／未完成**：全高 framebuffer 截圖期間 USB 可能短斷（AGENTS §8）。
- **出處**：ksrc `docs-lanes\tap-command.md`。

### K-036 RTC 校時（ADT fuxi,rtc-offset）
- **日期**：2026-11-16 17:58（cac5f98）、18:39（1836f06）；11-17 02:09（406f096）；02:42 隨 chain-v7 進日常
- **狀態**：採用（日常）
- **開關**：`fuxirtcoffset=<秒>`（優先）或 ADT `/chosen fuxi,rtc-offset`（4 bytes LE）；`fuxirtc` 控制 RTC 讀取。loader：`--rtc-calibration <json> [--rtc-adt]`。
- **問題／現象**：iOS 開機時間不對（PMIC RTC 計數器從某個基準起算，不是 Unix 時間），每次開機都要網路校時，且沒網路時時間錯誤。
- **根因**：PM8550 RTC 只是計數器，Android 把 offset 存在使用者空間；iOS 側沒有。
- **做法**：
  - RTC 讀取本身在 `fuxi_keys.c`（補丁 0103）。cac5f98：rollover 驗證的最後一次 LSB 讀取失敗時拒絕這筆樣本（原本仍回成功）；1836f06 同步到 H1 overlay。
  - 406f096：命令列沒有 `fuxirtcoffset` 時改讀 ADT（日常命令列 607 bytes 已滿）。
  - `work\adsp-battery\rtc-calibration.py capture`：Android 下讀 RTC 計數器與系統時間算 offset，輸出 json（一小時內有效）。
- **復現**：Android 開機後 `python -X utf8 work\adsp-battery\rtc-calibration.py capture <json>`（`verify <json>` 檢查是否仍有效）；也可用 `launch-loader-check.ps1 -CalibrateRtc -Out <名>`；`prepare-ram9g-loader.py … --rtc-calibration <json> --rtc-adt`。預期 console：`counter=<n> offset=<秒> (adt) epoch=<秒>`、`clock set`。
- **主機測試**：`work\adsp-battery\test-rtc-calibration.py`；`ksrc\tools\test-fuxi-rtc-read.py`（production extraction 16 cases／364 assertions）。
- **驗證紀錄**：chain-v7 暫時開機實記 `counter=71238 offset=1794782948 (adt) epoch=1794854186`、`clock set`，與台北時間吻合；日常 payload 帶 `1794782948`（`work\adsp-battery\rtc-calibration-daily-v7-20261117.json`）。iOS 時區另改 Asia/Taipei（U 系列）。
- **參考來源**：Linux `drivers/rtc/rtc-pm8xxx.c`（rollover 讀法）。
- **結論／未完成**：offset 依賴 RTC 計數器不歸零；完全斷電或 RTC 被重設後要重跑 capture 並重包部署。長時間走時未確認。
- **出處**：journal「歷史驗收摘要」段（L87）；ksrc `docs-lanes\rtc-time-handoff.md`。

### K-037 藍牙 GENI FIFO 與 iOS 16 藍牙 ABI 準備
- **日期**：2026-11-16 14:09（51e7ca9）、15:24（a948742）
- **狀態**：準備（唯讀盤點已上機）
- **開關**：`fuxibt=1` 或 ADT `fuxi,bt=1`（唯讀盤點）；loader `prepare-ram9g-loader.py --bluetooth-survey` 或 `prepare-wlan-w18-loader.py --bt 1`。
- **問題／現象**：WCN7850 藍牙走 QUP2 SE6 UART（HCI H4），iOS 端則要 `bluetoothd` 看得懂的傳輸；兩端都還沒有。
- **根因**：（準備）
- **做法**：`xnu\fuxi-bluetooth\fuxi_bt.c`／`fuxi_bt_xnu.c`：唯讀快照 SE6 的 GPIO 76–83、clock branch、engine 狀態；GENI FIFO 的純 C 模型；`ksrc\tools\inspect-fuxi-bluetooth-abi.py` 分析 iOS 16 藍牙驅動的 ABI。
- **復現**：`--bluetooth-survey`；預期 console：`FUXI BT: probe status=ok …`、`FUXI BT: gpio<n> ctl=… io=…`、`FUXI BT: branches core=… se6=…`。
- **主機測試**：`test-fuxi-bluetooth.py`、`test-fuxi-bluetooth-xnu.py`、`test-fuxi-bt-geni-fifo.py`。
- **驗證紀錄**：首趟唯讀 `survey status=ok`，GPIO 76–83 八筆完整、無 read fault；`off_mask=0x12 engine_read=0`（正確拒讀冷時鐘的 SE6；s_ahb 與 SE6 branch CLK_OFF 仍設定）。
- **參考來源**：Linux `drivers/tty/serial/qcom_geni_serial.c`、`drivers/bluetooth/hci_qca.c`。
- **結論／未完成**：板級上電見 K-039。
- **出處**：ksrc `docs-lanes\bluetooth-ios16-abi.md`、`bluetooth-fifo-preparation.md`、`bluetooth-checkpoint-01.md`。

### K-038 藍牙五條時脈租約準備
- **日期**：2026-11-16 16:09（b6be873）
- **狀態**：準備（延後）
- **開關**：無
- **問題／現象**：藍牙用的五條共用投票時脈還有其他使用者，要有引用計數才能安全開關。
- **根因**：XNU 這邊沒有涵蓋這五條時脈的 native clock owner。
- **做法**：`ksrc\xnu\fuxi-bt-clock-prep\fuxi_bt_clock_ledger.c`：純值 C 租約帳本與主機測試，不產生 MMIO callback。
- **復現**：WSL `python3 tools/test-fuxi-bt-clock.py`。
- **主機測試**：`test-fuxi-bt-clock.py`。
- **驗證紀錄**：主機測試通過。
- **參考來源**：Linux common clock framework 的 prepare／enable refcount。
- **結論／未完成**：明確延後；K-039 改用「暫時開、結束全部還原」的作法。
- **出處**：`ksrc\xnu\fuxi-bt-clock-prep\README.md`。

### K-039 藍牙板級上電與 QCA 版本查詢（fuxi,bt=3）
- **日期**：2026-11-17 17:20（141bb99）、17:38（7dfdd70）、17:41（161994f）
- **狀態**：實驗（已通；韌體下載未做）
- **開關**：ADT `fuxi,bt=3`（或 `fuxibt=3`）；loader `prepare-wlan-w18-loader.py --bt 3`（會檢查核心含 `board WLAN_EN steady after` 字串、命令列沒有 `fuxibt=`）。
- **問題／現象**：要讓藍牙晶片回應，得先把 SE6 時脈、腳位、BT_EN 拉起來。
- **根因**：開機階段沒有替 iOS 準備藍牙。第一版（bt1）把回覆的 `event[8]=0x0c` 誤判成 Command Disallowed，其實那是 12 bytes 的長度欄位。
- **做法**：`xnu\fuxi-bluetooth\fuxi_bt_board.c`：SE6 RCG 用 GPLL0_OUT_EVEN 設 7.3728 MHz；gpio76–79 切到 qup2_se6；先等 WLAN_EN 穩定為高 15 秒（上限 300 秒，不穩就不碰 BT_EN）；BT_EN 低 50 ms、拉高等 100 ms；送 QCA 版本查詢（vendor `0xfc00`）；結束後全部還原。7dfdd70 依 Linux btqca 跳過回覆多出的一個 byte，並記錄原始回覆；161994f 同步進 0117。
- **復現**：`prepare-wlan-w18-loader.py build --like <已驗> --artifact <核心> --name <新> --bt 3`，上機 `-Seconds 240`。預期 console：`FUXI BT: board WLAN_EN steady after <ms> ms` → `FUXI BT: QCA version product=0x19 soc=0x40170200 rom=0x200 patch=0x43fb` → `FUXI BT: board restore status=ok`。
- **主機測試**：`ksrc\tools\test-fuxi-bt-board.py`。
- **驗證紀錄**：bt1 `native-hw000bt1-boot-1`（17:25–17:32，141bb99）回應正確但誤判；bt2apps `native-hw000bt2apps-boot-1`（17:44，loader `native-bt2apps-ram9g-sb127-loader`，本機核心 `kernel-local-bt2-9g-artifact`＝ksrc `7dfdd70`＋`161994f`、ath12k `b2876c6`）：原始回覆 `040e120100fc00190c19000000fb43000200021740`，`probe status=ok … valid=1`，fifo 錯誤 0。
- **參考來源**：Linux `drivers/bluetooth/btqca.c`、`hci_qca.c`；UniXNU-sheng 藍牙（記憶 `xiaomi13-hardware-reference-projects`）。
- **結論／未完成**：下一步是下載韌體（依 product／soc 應為 `hmtbtfw20.tlv`／`hmtnv20.bin`）與 iOS 端 HCI 傳輸。
- **出處**：journal「歷史驗收摘要」段（L83）；記憶 `xiaomi13-bluetooth`。

### K-040 WLAN HTC 協定準備（主機模型）
- **日期**：2026-11-16 16:17（27bb0cf）
- **狀態**：準備（未接上核心）
- **開關**：無（`FUXI_HTC_DEFAULT_ACTIVE` 恆為 0）
- **問題／現象**：ath12k 韌體與主機之間的 HTC 封包（READY、CONNECT_RESPONSE、credit trailer）要先能正確解析。
- **根因**：（準備）
- **做法**：`ksrc\xnu\fuxi-htc-prep\fuxi_htc_prep.c`：獨立的 C11 解析器與診斷觀察器，不連進 WLAN、核心、CE、WMI 或 loader；不加開機參數、MMIO、DMA。
- **復現**：WSL `python3 tools/test-fuxi-htc-prep.py`。
- **主機測試**：`test-fuxi-htc-prep.py`。
- **驗證紀錄**：主機測試通過。
- **參考來源**：Linux v6.12 `drivers/net/wireless/ath/ath12k/htc.h`（標頭 8 bytes、READY 8／12 bytes、trailer 記錄）。
- **結論／未完成**：native ath12k（K-049 起）直接用 Linux 模組，這份模型保留作對照。
- **出處**：`ksrc\xnu\fuxi-htc-prep\README.md`。


## WLAN：原廠韌體路線（W18／P17，已擱置）

共同前提：

- 驅動在 `ksrc\xnu\fuxi-wlan\fuxi_wlan.c`，以補丁 `0099-fuxi-wlan.patch` 進核心。改原始碼後在 WSL 跑 `tools/sync-fuxi-wlan.py` 重產補丁。
- loader 產生器 `work\adsp-battery\prepare-wlan-w18-loader.py build`。基本命令列是 `WLAN_ARGS`：`fuxiadsp=0 fuxiwlan=1 fuxiwlanclk=1 fuxiwlanprobe=civac fuxiwlanseq=cycle,parf,dbi,eq,hostirq,msi,por,prefix fuxiwlanqrtr=5 fuxiwlanw18=1`。各實驗開關再附加上去，命令列上限 607 bytes。
- 這一段每趟都是暫時開機，沒改日常 boot_b。收尾都完成 bus master off、PERST、MSI／SMMU undo、buffer 釋放。
- 2026-11-14 以前的 WLAN 階段（M5、M6～M6o、W10～W17、try12～try16）在基底 75148d2 之前，屬 harness 歷史，見 S 系列 `0099`／`0104` 與 E 系列。

### K-041 W18 追蹤與原廠韌體 FW_READY 停損
- **日期**：2026-11-16 14:09（51e7ca9）；上機 15:32（第15趟）、16:24（第19趟）
- **狀態**：結論（原廠韌體路線擱置）
- **開關**：`fuxiwlanw18=1`（W18 有界追蹤）；`fuxiwlanmlo=<值>`（P17 選擇器，見 K-044）。loader 產生器 `--mlo`。
- **問題／現象**：用 Android 原廠 WLAN 韌體（amss20、bd_m3、REGDB、M3）啟動 WCN7850。QMI 前段全部成功，但韌體不送 FW_READY（QMI 0x38）。
- **根因**：未解。第32趟（K-047）證明同一套主機流程換上游韌體就能到 WLAN_MODE，所以差在原廠韌體自己要的額外步驟（QDSS 設定、cnss 專屬能力等）。
- **做法**：
  - 51e7ca9 加 W18 有界追蹤（`docs-lanes\wlan-w18.md`、`wlan-w18-profile.md`）。
  - 原廠資產從 Android 唯讀取出（`work\adsp-battery\android-wlan-assets-20261116-1450`），寫進手機 iOS root。寫入收據 `statusbar-fix\run-wlan-oem-assets-crlf-20261116`。
  - 依 TCSR major2 選 amss20。
- **復現**：
  1. 照「寫入手機分區」規則把原廠 WLAN 資產寫進 root：寫前比對、寫後讀回，保留還原清單。
  2. `prepare-wlan-w18-loader.py build --like <已驗 W18 loader> --artifact <含 W18 的核心> --name <新名>`。
  3. `launch-loader-check.ps1 -Seconds 300`。
  4. 預期 console：REGDB、BDF 15 段、M3_INFO 的 QMI result／error 都是 0，之後 `FW_READY 0x38` 等 10000 ms 逾時，停在 step 28（code `0x1c01`）。
- **主機測試**：`ksrc\tools\test-fuxi-wlan-w18.py`、`test-fuxi-wlan-m6.py`。
- **驗證紀錄**：
  - 第15趟 `native-hw000w18-oem-assets-boot-1`，15:32:22–15:38:12：結果如上，主機最高 61.9°C。mtd 存 `adsp-battery\fuxilog\20261116-153920`（boot#17，307228 bytes）。
  - 第19趟 `native-hw000w18-oem-nomlo-boot-1`（loader `native-wlan-w18-oem-nomlo-ram9g-loader`），16:24:13–16:31:04：關掉 MLO 後結果相同，所以 no-MLO 不是解法。最高 51.7°C。
  - 對照 Android 開機紀錄 `work\adsp-battery\android-wlan-init-20261116-1823`：Android 的 M3（`phy_ucode20.elf`，299660 bytes，和 fuxi 的 m3.bin 同大小）送出後 2 ms 下載 `qdss_trace_config_v2.cfg`（4911 bytes），再 11 ms 就收到 FW init done。
  - 288cd1a（18:25）把這份 Android 證據寫進 `docs-lanes\wlan-hostcap-secondary-checkpoint.md`，用來框定下一批假設（K-042～K-046）。
- **參考來源**：Android cnss2 開機紀錄；Linux ath12k `qmi.c`；UniXNU-sheng（同 WCN7850，已通）。
- **結論／未完成**：K-042～K-046 依序排除了五個假設。K-047 換上游韌體才到 MODE，原廠路線擱置。
- **出處**：journal 2026-11-16 第15、19趟；`ksrc\docs-lanes\wlan-w18.md`。

### K-042 SECONDARY HOST_CAP
- **日期**：2026-11-16 17:04（a066a05）、17:19（06c7b64）、17:41（3115b92）；上機 18:04（第22趟）
- **狀態**：結論（排除）
- **開關**：`fuxiwlancap=1`；loader 產生器 `--host-cap-secondary`。
- **問題／現象**：Android 的 HOST_CAP 帶 SECONDARY 能力，fuxi 沒帶。懷疑韌體因此不送 FW_READY。
- **根因**：不是這個。
- **做法**：
  - a066a05 加有界的 HOST_CAP TLV `0x20 SECONDARY=2`，只在 `fuxiwlancap=1` 時送。
  - 06c7b64 讓 M6i 主機模型、3115b92 讓 Rails 主機抽取，都跟正式的 admission guard 對齊。
  - 同一個 commit 也把手電筒的 deadline 降低（K-028）。
- **復現**：在 K-041 的 loader 上加 `--host-cap-secondary`，其他不變。
- **主機測試**：`test-fuxi-wlan-w18.py`、`test-fuxi-wlan-m6i.py`、`test-fuxi-wlan-rails.py`。產生器 self-test 會核對 `fuxiwlancap=1` 只出現一次，而且是唯一差異。
- **驗證紀錄**：第22趟 `native-hw000w18-oem-secondary-boot-1`（loader `native-wlan-w18-oem-secondary-ram9g-sb127-loader`），18:04:50–18:09:41。
  - TLV 確實送出，回覆 `msg0x34 result0/error0`。
  - REGDB、BDF、M3_INFO 都成功，但仍等 FW_READY 10000 ms 後停在 step 28。主機最高 50.5°C。
  - USB log SHA-256 `72c168f66ce8f43da9656c6b9f1b2a6a6416d0f84569885bc96b9b9a4eb8d4fb`。
- **參考來源**：Android cnss2 HOST_CAP 內容；Linux ath12k `qmi.c` 的 host_cap。
- **結論／未完成**：單一 TLV 變更不是解法。
- **出處**：journal 第22趟；`ksrc\docs-lanes\wlan-hostcap-secondary-checkpoint.md`。

### K-043 省略 nm_modem 能力
- **日期**：2026-11-16 18:37（723e740）、19:02（4efd714 紀錄）；上機 18:55（第25趟）
- **狀態**：結論（排除）
- **開關**：`fuxiwlancap=2`；loader 產生器 `--omit-nm-modem`。payload 和 `fuxiwlancap=1` 只差一個字元。
- **問題／現象**：接 K-042，比對 Android 的能力宣告，試試不宣告 nm_modem。
- **根因**：不是這個。
- **做法**：723e740 加一個隔離的「省略 SECONDARY host 能力」選項，用來對照 firmware-ready。
- **復現**：在 K-041 的 loader 上加 `--omit-nm-modem`。核心用 9g CI `43850892720`（kernel SHA-256 `1311058383a2680914b77292e4d7883e72de8bdea5ceda6e16c893c592191da3`）；同批 5g CI 是 `43850892769`。
- **主機測試**：`test-fuxi-wlan-m6i.py`、`test-fuxi-wlan-rails.py`、`test-fuxi-wlan-w18.py`。
- **驗證紀錄**：第25趟 `native-hw000w18-oem-no-nm-modem-boot-1`，18:55:06–18:59:58。
  - 省略標記和 SECONDARY 都出現，回覆 result0。
  - 仍是 step 28 FW_READY 逾時，code `0x1c01`。
  - SMMU CB6 的 FSR `0x400`、FAR、FSYNR、SID `0x139f` 從 step 20 到 step 28 都一樣，所以不算新增 fault，也不改 stream mapping。
  - USB `boot.log` 4114 行，SHA-256 `beb4fb316b641167a6ace5ca5d1eabdb313c880d9d3e22cb465e5c3cfb8e528c`。
  - 持久紀錄 `adsp-battery\fuxilog\20261116-190038\console.txt`（boot#27）。
- **參考來源**：同 K-042。
- **結論／未完成**：排除。
- **出處**：journal「18:54–19:01：第25趟」。

### K-044 PMK8550 睡眠時脈與大塊記憶體（P17）
- **日期**：2026-11-16 19:37（e82f103）；上機 19:20（第26趟，用既有開關）、19:53（第27趟）
- **狀態**：結論（睡眠時脈是必要條件，見 K-047；大塊記憶體排除）
- **開關**：
  - `fuxiwlanmlo` 位元：`0x40` 讀睡眠時脈；`0x80` 開睡眠時脈；`0x200` BIGMEM；`0x100` ERRDEC（K-045）。
  - loader 產生器 `--sleep-clock check|on` 會產生 `fuxiwlanmlo=64` 或 `192`；`--big-mem` 產生 `704`。
- **問題／現象**：
  - 對照 UniXNU-sheng（同晶片、已通）後發現 fuxi 從沒開過 PMK8550 gpio3 的 32 kHz 睡眠時脈。
  - 另外，W13 起 fuxi 一律回零段記憶體，逼韌體改要 31 塊不超過 512 KiB 的小塊。Android cnss2 和 mainline ath12k 都直接給兩大塊（7077888＋8454144 bytes）。
- **根因**：
  - 睡眠時脈：實證 XNU 下是關的。
  - 大塊：try12 時期給過大塊，但 IOVA 只對齊 16 KiB；W13 把「切小塊」和「二的次方對齊」一起改掉，所以「大塊＋正確對齊」從沒測過。
- **做法**：
  - 睡眠時脈的 P17 開關早已存在，只是沒上機過。
  - e82f103 新增 `0x200` BIGMEM：直接給兩大塊，IOVA 對齊，不做零段協商。
  - 為了湊 607 bytes 命令列，拿掉重複的 `fuxiadsp=0`；ADT `fuxi,adsp=0` 仍然關著 ADSP。
- **復現**：
  - 第26趟：`--sleep-clock on`。預期 console 先印 `mode 0 func 0 en 0x80 state 0`（原本關），開啟後讀回 `mode 0x1 func 0x2 en 0x80 state 0xff`。
  - 第27趟：`--big-mem`。預期 `P17 bigmem grants n=2 total=15532032 without zero-segment negotiation; aligned IOVAs`。
- **主機測試**：`test-fuxi-wlan-w13.py`（加 trial 14）、`test-fuxi-wlan-w17.py`（選擇器範圍）。
- **驗證紀錄**：
  - 第26趟 `native-hw000w18-oem-slpclk-boot-1`（loader `native-wlan-w18-oem-slpclk-ram9g-sb127-loader`，boot SHA-256 `04b7f151bd1bb9b8a394a7f65dc54f3bf561b2c394de60efe807ecd3ab835b90`），19:20:14–19:25:02。時脈確實開了，但原廠韌體仍停在 step 28。EE 是 AMSS，ERRCODE／ERRDBG 全是 `0xcafecace`：韌體沒當掉，只是沒往下走。USB SHA-256 `222d0de1ae1477b701971938f82459b667383d172de5166cd74dc36072108241`。
  - 第27趟 `native-hw000w18-oem-bigmem-boot-1`，19:53，`fuxiwlanmlo=704`：走了新路徑，FW_READY 仍逾時，所以小塊協商不是卡點。
  - 核心 `kernel-adsp-bigmem-ram9g-artifact`（kernel SHA-256 `cb527adf…f0d5`，含手電筒共用鎖 b48ec0f）；harness `4c6cc13`；CI `43859436119`／`43859436103`／`43859436037`。
- **參考來源**：UniXNU-sheng 的 WCN7850 開機流程；Android cnss2、Linux ath12k 的 `qmi.c` 記憶體請求處理。
- **結論／未完成**：睡眠時脈對原廠韌體不夠，但對上游韌體是關鍵（K-047）。大塊記憶體排除。
- **出處**：journal「19:10–19:25：第26趟」「19:53–20:16」；`ksrc\docs-lanes\wlan-p17.md`。

### K-045 ERRDEC：WLFW 逾時後統計韌體寫過的主機記憶體
- **日期**：2026-11-16 20:27（86cfb9d）；上機 20:42（第31趟）
- **狀態**：結論（診斷）
- **開關**：`fuxiwlanmlo` 位元 `0x100`；loader 產生器 `--errdec` 產生 `fuxiwlanmlo=960`（相對第27趟只多診斷輸出）。
- **問題／現象**：需要分辨兩種情況：韌體根本沒碰主機記憶體，還是跑了一段才停。
- **根因**：（診斷項）
- **做法**：
  - 分配時清零。
  - WLFW 逾時後逐段統計：非零位元組數和頁數、開頭幾個字、最多 12 段可讀文字。
  - 唯讀，不改流程。
- **復現**：`--errdec`。核心 `kernel-adsp-errdec-ram9g-artifact`：harness `fb74d96`，CI `43866828542`／`43866828266`／`43866828527`。
- **主機測試**：無專用測試，跟著 0099 編譯。
- **驗證紀錄**：第31趟 `native-hw000w18-oem-errdec-boot-1`（loader boot SHA-256 `21ff3a9e…c7fb`），20:42:59–20:50:11，最高 50.1°C。
  - type 1（7077888 bytes）只有開頭 493 個非零位元組（+0～+0x317，一頁），開頭是 `0159aec0 0000012c 00100505 01278c08`，沒有可讀文字。
  - type 4（校正用，8454144 bytes）完全沒被碰過。
  - USB SHA-256 `f82cede2…7371`。
- **參考來源**：無。
- **結論／未完成**：韌體對主機記憶體的 DMA 是通的，SMMU 對應沒問題。它只寫了一小段表頭就停。Android 在同一步，M3 之後 13 ms 就 init done。
- **出處**：journal 第31趟；`docs-lanes\wlan-p17.md`。

### K-046 AOP PDC 設定
- **日期**：2026-11-16 20:10（第29趟）
- **狀態**：結論（排除）
- **開關**：`fuxiwlancnss=pdc`（M6n 階段已有，預設關）；loader 產生器 `--cnss-pdc`。
- **問題／現象**：Android cnss2 在 WLAN 供電前會送 13 筆 `wlan_pdc` 設定給 AOP，fuxi 預設不送。
- **根因**：不是這個。
- **做法**：開既有開關。為了湊 607 bytes 命令列，拿掉 `fuxiperfidle=1000`。
- **復現**：在 K-044 第27趟的 loader 上加 `--cnss-pdc`，即 `fuxiwlanmlo=704 fuxiwlancnss=pdc`。預期 console `M6n PDC consumed 13/13 before WLAN rails`。
- **主機測試**：`test-fuxi-wlan-m6n.py`。
- **驗證紀錄**：
  - 第29趟 `native-hw000w18-oem-pdc-boot-1`，20:10:48 開機：13 筆全部送出，FW_READY 仍逾時。USB SHA-256 `41ae905f…b0d8`；持久紀錄 `fuxilog\20261116-201600`（boot#31）。
  - 這趟啟動器在 20:13 中途結束（主機程序消失）。手機靠核心的 6 分鐘倒數自己回 Android；20:16 確認回程後才移除留下的鎖檔。
- **參考來源**：Android cnss2 開機紀錄。
- **結論／未完成**：
  - 預設表第 11 筆 `bb s2g.v dwnval` 是 800，Android 實際是 512，來源未查。
  - Android 在 CAP 回覆後還依 `ol_cpr_cfg` 再送 7 筆 AOP 電壓（s4e 914、s5g 946/414、s2g 966/790、s4g 1314、s6g 1866），fuxi 只印不送。
  - 兩者都未再追（原廠路線擱置）。
- **出處**：journal「19:53–20:16」。

### K-047 上游韌體＋睡眠時脈：WLAN_MODE 通過
- **日期**：2026-11-16 21:02（第32趟）
- **狀態**：已驗證（QMI 啟動流程全部完成；資料通路改走 K-049 起的 native ath12k）
- **開關**：`fuxiwlanbdf=bd_up.elf fuxiwlanmlo=192`（不開 SECONDARY、大塊、PDC）；loader 產生器 `--bdf bd_up.elf --sleep-clock on`。
- **問題／現象**：11-14 的 try15／16 用上游韌體已到 FW_READY／INI／CFG，只卡 WLAN_MODE 10 秒。
- **根因**：缺 PMK8550 gpio3 的 32 kHz 睡眠時脈（K-044 實證 XNU 下原本是關的）。
- **做法**：
  1. 從 linux-firmware `ath12k/WCN7850/hw2.0` 下載 `amss.bin`／`m3.bin`／`board-2.bin`。SHA-256 依序 `43aadfd3…`／`0e72f44d…`／`6a98237c…`，三個都和 sheng 清單相同。版本 `WLAN.HMT.1.1.c7-00108…UPSTREAM-3`。存 `work\adsp-battery\upstream-wlan-fw-20261116`。
  2. 從 board-2.bin 取 `…subsystem-vendor=17cb,subsystem-device=1107,qmi-chip-id=2,qmi-board-id=255` 項（88760 bytes，`d4f92e9f…`）存成 `bd_up.elf`；regdb 取同檔的 `bus=pci` 項（24310 bytes）。
  3. 清單 `wlan-stage-input-upstream-20261116-2110.json`。20:58–21:02 寫進手機 root：改 8 個 4 MiB 區塊，三次 fsck OK。還原清單與原區塊在 `statusbar-fix\run-wlan-upstream-20261116`。
- **復現**：照上面三步準備、寫入韌體後，`prepare-wlan-w18-loader.py build --like native-wlan-w18-oem-slpclk-ram9g-sb127-loader --bdf bd_up.elf --sleep-clock on --name <新名>`。預期 console：
  - M3_INFO 回覆時收到 FW_READY（0x38）。
  - INI 約 11 ms、CFG 約 35 ms，`W15 wlan mode normal ok; stop before CE/HTC`（約 89 ms，result 0／error 0）。
  - 再觀察 2 秒 ERRCODE 不變，`step 28 ok code 0x101c`，28 steps done。
- **主機測試**：`test-fuxi-wlan-w15.py`、`test-fuxi-wlan-w18.py`。
- **驗證紀錄**：第32趟 `native-hw000w18-up-slpclk-boot-1`（loader `native-wlan-w18-up-slpclk-ram9g-sb127-loader`，boot SHA-256 `8a32fedb…9940`，核心同第31趟），21:02:23–21:09:33，最高 60.7°C。
  - 結果如上預期。
  - USB SHA-256 `f30b9ff1…624a`；持久紀錄 `fuxilog\20261116-210953`。
- **參考來源**：linux-firmware ath12k WCN7850 hw2.0；UniXNU-sheng 韌體清單；Linux ath12k `qmi.c`。
- **結論／未完成**：
  - 當時驅動到 MODE 就主動停機，沒有 CE／HTC／WMI。
  - 原廠韌體（c2-00058.3）停在 M3 之後，擱置不追。
  - 下一段資料通路改用 Linux ath12k 模組（K-049）。
- **出處**：journal「第32趟」。

### K-048 WLAN BDF 來源盤點
- **日期**：2026-11-16 16:17（27bb0cf）
- **狀態**：準備（工具）
- **開關**：無
- **問題／現象**：BDF（board data）有好幾個來源：Android vendor、board-2.bin、sheng 清單。要能核對哪個檔對應哪組 chip／board id。
- **根因**：（準備）
- **做法**：`ksrc\tools\fuxi-wlan-bdf-inventory.py` 解析 board-2.bin／ELF BDF 並列出項目和雜湊。同一個 commit 也加了 RAM bank shadow admission 檢查（K-024）。
- **復現**：WSL `python3 tools/fuxi-wlan-bdf-inventory.py <board-2.bin 或 BDF>`（參數見檔頭 argparse）。
- **主機測試**：`test-fuxi-wlan-bdf-inventory.py`。
- **驗證紀錄**：主機測試通過；K-047 的 `bd_up.elf` 就是依這套規則取出的。
- **參考來源**：Linux ath12k `core.c` 的 board-2.bin 解析（BDF 名稱字串格式）。
- **結論／未完成**：無。
- **出處**：`ksrc\docs-lanes\wlan-bdf-provenance.md`。

## WLAN：native ath12k 與 iOS 介面

共同前提：

- 直接連結 pinned 的 Linux ath12k 模組物件，模組在 macOS CI 編：harness workflow `fuxi-ath12k-objects`。來源鎖是 ath12k `5f7f5a8`、UniXNU-sheng `6880068`、UniXNU `4a654d1`。
- 核心這邊：
  - `xnu\fuxi-wlan\fuxi_wlan_ath12k.{h,inc}`、`fuxi_wlan_arena.h`、`fuxi_wlan_control.h`、`fuxi_wlan_ios.inc`，在補丁 0099。
  - `xnu\fuxi-wifi-ios\`（apple80211、station、net），在補丁 0104。
- 本機建置：`build-kernel.ps1 -Ram 9g -Ath12k <已下載的 fuxi-ath12k-objects 資料夾> -Name <名>`。
- loader：`prepare-wlan-w18-loader.py build --like <已驗 native loader> --artifact <核心> --name <新> --native-ath12k --wifi-ios …`。產生器會檢查核心裡有沒有對應的字串標記，沒有就拒絕。
- 每個 loader 資料夾都有產生器寫的 profile：native 是 `wlan-ath12k-profile.json`，W18 是 `wlan-w18-profile.json`。裡面記錄當次選項（quiet_boot、haptic_amp、wifi_rssi_bounds、inject…）和完整命令列，`--like <loader 名>` 就是讀 native 的這份當範本。要重現某趟，先看這個檔。
- 命令列固定帶 `fuxiwlan=1 fuxiwlanath=1 fuxiwlanmlo=192`，`--wifi-ios` 再加 `fuxiwifiios=2`。11-18 起，可用 `--wifi-args-adt` 把驅動啟動後才讀的開關移到 ADT `fuxi,args`（K-067）。

### K-049 ath12k 板級轉接層
- **日期**：2026-11-16 22:37（42df429）
- **狀態**：已驗證（K-052 起實機掃描）
- **開關**：`fuxiwlanath=1`（預設關）
- **問題／現象**：K-047 之後要 CE／HTC／WMI／掃描。自己移植 ath12k 是大工程，改成直接連 Linux 模組，fuxi 只提供板級介面。
- **根因**：（新功能）
- **做法**：
  - PBL 階段只有單一 MHI owner。
  - 對應完整 2 MiB BAR。
  - 128 MiB Normal-NC 自有 DMA arena（`fuxi_wlan_arena.h`）。
  - 窄範圍的 PCI config W1C 保護。
  - 韌體讀取走白名單。
- **復現**：只做主機驗證；上機見 K-052。
- **主機測試**：`test-fuxi-wlan-ath12k.py`、`test-fuxi-wlan-m6.py`／`m6i`／`m6o`。
  - MSVC ASan 跑 236471 項檢查、30000 次 allocator transition，含 containment 之後 metadata／韌體的釋放。
  - 和 pinned `nw_abi.h` 比對四個 struct 的大小、alignment、所有欄位 offset 和 callback 指派。
  - M6／M6i／M6o／Rails／W18 用 wasm32 ASan／UBSan 重跑。
- **驗證紀錄**：主機全過；當時 board-2.bin 尚未放進 root。
- **參考來源**：pinned 模組 ABI（`nw_abi.h`，在模組來源鎖內）；Linux ath12k `pci.c`／`mhi.c`。
- **結論／未完成**：接 K-050 連結。
- **出處**：journal「Fuxi板級介面新增…」；`ksrc\docs-lanes\wlan-ath12k.md`。

### K-050 ath12k 模組連結與 CI
- **日期**：2026-11-16 22:43（f6a7588）
- **狀態**：已驗證
- **開關**：只在要求時連結（核心 CI 的 opt-in 9g，或本機 `-Ath12k`）。
- **問題／現象**：模組物件在 macOS 上編，核心在 Linux／macOS 交叉編，連結前要先驗證物件。
- **根因**：首次模組 CI 失敗：pinned fetcher 把帶前置斜線的路徑直接交給 cone sparse-checkout。
- **做法**：
  - `ksrc\tools\ci\ath12k-link.py` 核對物件 manifest、imports、entries 後才連結。
  - `variants\ath12k-link.patch`；`tools\ci\build.sh`、`prepare-kernel.py` 接線。
  - harness `41a932d` 只做受控的字串轉換，改用既有去斜線的 `paths` 和 `--skip-checks`。
- **復現**：
  - 推 harness 觸發 `fuxi-ath12k-objects`。
  - 下載 artifact 後 `build-kernel.ps1 -Ram 9g -Ath12k <資料夾>`。腳本從模組 gate 讀 15.5 建置 target，用 Linux `llvm-nm` 檢查 Mach-O。
- **主機測試**：`test-fuxi-ath12k-link.py`（15e6d0a 加）。
- **驗證紀錄**：
  - 模組 CI `43879548709`（harness `41a932d`）成功：約 1.2 MiB，13 項 undefined import 全是 C 字串／記憶體／格式化函式。
  - harness `cb63de2` 重建，CI `43894744787`：125 objects，Mach-O 為 MACOS15.5，13 imports，9 entries。
  - opt-in 9g 核心 CI `43896202460`（harness `c0d0d2d`）：41 項 manifest 全過，kernel SHA-256 `3528e078d9c2c2770f638c361b49d54bf6f…`。
  - 11-17 本機建置通過 47 項 manifest。
- **參考來源**：UniXNU Ath12kWifi 建置方式。
- **結論／未完成**：無。
- **出處**：journal「ath12k objects 首次 CI…」「harness cb63de2…」「ath12k 9g CI…」。

### K-051 隔離 native imports 與控制通道
- **日期**：2026-11-16 23:25（15e6d0a）、2026-11-17 00:19（62dd9b0）
- **狀態**：已驗證
- **開關**：同 K-049
- **問題／現象**：
  - 第一趟 native 實機停在 `start -16`，BSS 0。
  - 第二趟到 AMSS／M0，但 DMA error 1、BSS 0。
- **根因**：
  - 第一趟：控制通道沒接上，start 回 -EBUSY。
  - 另外 imports 要和核心連結平台一致。
- **做法**：
  - 15e6d0a 隔離 native WLAN 的 imports，讓連結平台和核心一致。
  - 62dd9b0 接上 control transport（`fuxi_wlan_control.h`），掃描 peer 核對 session、generation 和 reply。
- **復現**：本機 9g 加 `-Ath12k`，loader 加 `--native-ath12k`。
- **主機測試**：`test-fuxi-wlan-control.c`（C、ASan）、`test-fuxi-wlan-ath12k.py`、`test-fuxi-ath12k-link.py`。
- **驗證紀錄**：
  - `native-hw000ath12k-scan-boot-1`，11-16 23:51:24–23:58:16：start -16，BSS 0，DMA error 0，faults 0。mtd `fuxilog\20261117-000135`。
  - `native-hw000ath12k-control-boot-1`（ksrc 62dd9b0），00:26:28–00:30:18：到 AMSS／M0，DMA error 1 後收尾，BSS 0，最高 49.3°C。mtd `fuxilog\20261117-003351`。
- **參考來源**：pinned 模組的 `nw_*` 控制介面。
- **結論／未完成**：DMA error 由 K-052 修。
- **出處**：journal 對應兩行。

### K-052 遵守模組 DMA kfree 擁有權
- **日期**：2026-11-17 00:31（d24fc49）
- **狀態**：已驗證（第一次掃到 BSS）
- **開關**：同 K-049
- **問題／現象**：K-051 第二趟 DMA error 1。
- **根因**：模組對 DMA buffer 的 kfree 擁有權契約沒照做：該由模組釋放的被 fuxi 處理，或反過來。
- **做法**：arena 依模組契約記錄擁有者，釋放走對應路徑（`fuxi_wlan_arena.h`、`fuxi_wlan_ath12k.inc`）。
- **復現**：loader `native-wlan-dma-ram9g-sb127-loader`，核心本機 9g（ksrc d24fc49）。
- **主機測試**：`test-fuxi-wlan-ath12k.py`。
- **驗證紀錄**：`native-hw000ath12k-dma-boot-1`，00:34:12–00:37:02。
  - 掃到 34 個 BSS；start error 0、DMA error 0、faults 0；最高 61.1°C。
  - 收尾 bus master off、PERST asserted；MHI reset 未確認。
- **參考來源**：pinned ath12k 模組的 DMA API 契約。
- **結論／未完成**：MHI reset 的收尾未確認。
- **出處**：journal `native-hw000ath12k-dma-boot-1`。

### K-053 iOS station 後端（電源與掃描）
- **日期**：2026-11-17 00:56（e575a32）
- **狀態**：已驗證
- **開關**：`fuxiwifiios=2`；loader 產生器 `--wifi-ios`。
- **問題／現象**：要讓 iOS 的 wifid 看到網路介面，能開關電源和掃描。
- **根因**：（新功能）
- **做法**：
  - `fuxi-wifi-ios\`：`fuxi_wifi_apple80211.c` 回答 Apple80211 ioctl；`fuxi_wifi_sta.c` 是 station 狀態；`fuxi_wifi_ios.c` 接 IOKit。
  - `fuxi_wlan_ios.inc` 把 native WCN7850 的電源和掃描接到 station 後端。
  - 啟動時自測兩輪 power off／on 和掃描。
- **復現**：loader `native-wlan-ios-ram9g-sb127-loader`，本機 9g（ksrc 4b52021、harness 1d53fc3）。
- **主機測試**：`test-fuxi-wifi-ios.py`、`test-fuxi-wlan-ios.py`；`ksrc\tools\check-fuxi-wifi-ios.py`。
- **驗證紀錄**：
  - `native-hw000wlan-ios-boot-1`，01:03:11：兩輪 power off／on result 0，station 掃描 BSS 30。
  - `native-hw000wlan-ios-ui-boot-1`（同核心、帶設定頁巡覽）：wifid 取 POWER 有回應。RSSI_BOUNDS、INT_MIT、POWERSAVE 和 ioctl 383 被拒，畫面顯示「association unavailable」，無法加入。
- **參考來源**：`fuxi-wifi-ios` 移植自 HackiOS kernel checkout `8e61187098f2c26c5daee8f9f72b6416aa64b9ab`（APSL-2.0）：`redwood_net.c`→`fuxi_wifi_net.c`、`redwood_sta.c`→`fuxi_wifi_sta.c`、`redwood_apple80211.c`→`fuxi_wifi_apple80211.c`、`RedwoodWLAN.cpp`→`FuxiWLAN.cpp`、`MiniIONetworkStack.cpp`→`FuxiNetworkStack.cpp`、`wifi/lx/src/lx_wpa.c`→`fuxi_wifi_wpa.c`；授權 `LICENSE.HackiOS` 隨 0104 附上。HackiOS userland `906425b7…`（MIT）只讀過 wifid 筆記和 `net_test.m`，沒有複製。沒有用到 UniXNU 程式碼。
- **結論／未完成**：加入流程見 K-055。
- **出處**：journal 兩行；`ksrc\docs-lanes\wifi-ios.md`。

### K-054 native WLAN 身分對齊 iOS 裝置樹
- **日期**：2026-11-17 01:00（4b52021）
- **狀態**：已驗證（併入 K-053 那趟）
- **開關**：同 K-053
- **問題／現象**：wifid 依裝置樹 wlan 節點找介面，native 驅動的身分要和它一致。
- **根因**：native 驅動沒對應 iOS DT 的 wlan 節點。
- **做法**：`fuxi_wlan_ath12k.h`／`.inc` 加身分欄位，讓它與 iOS 裝置樹的 wlan 節點一致。
- **復現**：同 K-053。
- **主機測試**：`test-fuxi-wlan-ios.py`。
- **驗證紀錄**：K-053 那趟 wifid 有找到介面並送 ioctl。
- **參考來源**：iOS 16 原廠 DT 的 wlan 節點。
- **結論／未完成**：無。
- **出處**：ksrc 4b52021；`docs-lanes\wlan-ath12k.md`。

### K-055 加入開放與 WPA2-PSK 網路
- **日期**：2026-11-17 02:00（d020ece）
- **狀態**：已驗證（11-17 使用者手動加入 WPA2 成功、DHCP 成功）
- **開關**：加入流程本身沒有開關（跟著 `fuxiwifiios=2`）。自測用 `fuxiwifitest=2 fuxiwifitestssid=<SSID>`（loader 產生器 `--wifi-join-test <SSID>`）。
- **問題／現象**：wifid 從來沒有匹配到 WPA2 網路。
- **根因**：GET_BSS 只回 384 bytes 的元素，把 RSN 截掉了，wifid 以為網路沒加密。
- **做法**：
  - GET_BSS 回完整元素。
  - worker 透過模組的 rx event 和 `nw_wifi_tx` 跑四向握手。
  - WPA3／SAE 和必要 MFP 的網路回 EOPNOTSUPP（不支援）。
  - `fuxiwifitest=2` 用假密碼試加入一次。
- **復現**：loader `native-wlan-join-ram9g-sb127-loader`（`--like` 產生，`--wifi-join-test <SSID>`），本機 `kernel-local-wifijoin-9g-artifact`。預期 console：associated → step5 handshake error 80（AP 拒絕金鑰＝流程正確）。
- **主機測試**：`test-fuxi-wlan-ios.py`（大幅擴充）、`test-fuxi-wlan-ath12k.py`。當時 33 項 workflow 主機命令通過。
- **驗證紀錄**：`native-hw000wlan-join-boot-1`，02:02:52–02:05:22，最高約 42°C。
  - 掃描 BSS 36；家用網路 2.4 GHz −37 dBm、5.2 GHz −55 dBm。
  - 假密碼結果如預期。
  - mtd `fuxilog\20261117-020754`。
- **參考來源**：四向握手的 WPA 邏輯是 `fuxi_wifi_wpa.c`（移植自 HackiOS `lx_wpa.c`，見 K-053）；Linux mac80211 加入流程。
- **結論／未完成**：
  - 真密碼加入在 K-056（06:50）手動驗過。
  - 純 SAE 和必要 MFP 仍不支援。
- **出處**：journal `native-hw000wlan-join-boot-1`。

### K-056 UI 測試只列指定網路（fuxiwifitest=3）
- **日期**：2026-11-17 02:56（70470b9）
- **狀態**：已驗證（開關生效；腳本點擊仍不可靠）
- **開關**：`fuxiwifitest=3 fuxiwifitestssid=<SSID>`；loader 產生器 `--wifi-ui-only <SSID>`。只用在測試 loader，日常不帶。
- **問題／現象**：點擊腳本要在「其他網路」或清單裡點到目標網路，但清單順序不固定。
- **根因**：（測試輔助）
- **做法**：開關打開時，wifid 只看得到指定名稱的網路，清單第一列就是它。密碼仍由畫面輸入。
- **復現**：`--wifi-ui-only <SSID> --tap-script <腳本>`。
- **主機測試**：`test-fuxi-wlan-ios.py`。
- **驗證紀錄**：
  - `native-hw000wlan-uijoin-boot-1`（02:45，本機 `kernel-local-wifirtc-9g-artifact`，這個開關之前）：設定 App 停在「一般」頁，點擊全部落空。21 個標記只存到 2 張圖（讀取時 USB 斷 16 次）。
    - 同趟確認狀態列 2:45 是台北時間（K-036）。
    - 同趟發現 coreduetd 不停呼叫 AKS sel 17（kAKSGetDeviceState），約 57% CPU（U 系列）。
  - `native-hw000wlan-uijoin2-boot-1`（06:50，本機 `kernel-local-wifiui-9g-artifact`＋`fuxiwifitest=3`）：腳本仍落空，還誤改了「收到新留言」提示聲。
    - 使用者手動加入 `<SSID>` 成功：WPA2-PSK、2412 MHz、−47 dBm，DHCP 拿到 `<區網IP>`。
    - 但狀態列 Wi-Fi 格是空的、Safari 黑屏，點網路後輸入密碼前卡幾秒。
- **參考來源**：無。
- **結論／未完成**：訊號格空白見 K-057。
- **出處**：journal 兩行。

### K-057 定期送 RSSI_CHANGED（狀態列訊號格）
- **日期**：2026-11-17 07:18（54beb9b）
- **狀態**：已驗證（wifid 的 RSSI／NOISE 查詢有回答）
- **開關**：無（跟著 `fuxiwifiios=2`）
- **問題／現象**：連上之後狀態列 Wi-Fi 格是空的。
- **根因**：驅動從不送 RSSI_CHANGED（事件 39），wifid 算不出 ScaledRSSI。
- **做法**：已連線時每 5 秒送一次 RSSI_CHANGED；噪聲底回 −95 dBm（`fuxi_wifi_apple80211.c`／`.h`）。
- **復現**：本機 `kernel-local-wifirssi-9g-artifact`；同趟狀態欄設定 300103（U 系列狀態欄項）。
- **主機測試**：`test-fuxi-wifi-ios.py`。
- **驗證紀錄**：`native-hw000wlan-rssi-sb103-boot-1`，07:20。
  - 使用者再加入 `<SSID>` 成功（2412 MHz），wifid 的 RSSI／NOISE 查詢有回答。
  - 訊號格要等使用者確認：tap `p` 不發 monitor 認得的標記，沒有截圖。
  - 同趟 Safari 開 apple.com：backboardd 的 Metal 後端拒收 WebKit 的 `b3a8`（3001×1500，HostMemory 不支援）和 `w30r`（OutOfMemory(Device)）。gate runtime-failure，20 秒後 backboardd 以 CPU 重生。這個問題交給 K-070／G 系列。
- **參考來源**：iOS 16 wifid 對 RSSI 事件的處理（本機反組譯）。
- **結論／未完成**：自動加入仍不行，根因在 K-064。
- **出處**：journal `native-hw000wlan-rssi-sb103-boot-1`。

### K-058 舊掃描結果加入前只重掃上次頻道
- **日期**：2026-11-17 07:27（d3f7631）
- **狀態**：已合入（沒有單獨上機）
- **開關**：無
- **問題／現象**：wifid 用較舊的掃描結果要求加入時，BSS 可能已不在 mac80211 的快取裡，需要重掃；全頻重掃很慢。
- **根因**：（效能）
- **做法**：先只重掃這個網路上次所在的頻道；不在那裡才全頻掃。
- **復現**：包含在 K-059 之後的所有 Wi-Fi 核心。
- **主機測試**：`test-fuxi-wlan-ios.py`。
- **驗證紀錄**：沒有單獨一趟；之後各趟加入都有經過。
- **參考來源**：Linux mac80211 加入前 probe 的行為。
- **結論／未完成**：無。
- **出處**：ksrc d3f7631。

### K-059 AKS 金鑰 wrap／unwrap（鑰匙圈可存 Wi-Fi 密碼）
- **日期**：2026-11-17 07:56（31241b2）、07:58（7294068）
- **狀態**：已驗證
- **開關**：無（AKS 替身固定提供）
- **問題／現象**：
  - 11-17 07:38 的 ADSP＋WLAN 聯合那趟（K-012）：使用者手動加入成功，但連上的那一刻 securityd 包裝金鑰（AKS sel 10）被拒 `0xe00002c7`。
  - 之後 wifid 不再發任何 apple80211 查詢，訊號格空白，密碼也沒存進鑰匙圈。
  - 原生日誌（15:22 的 `native-hw000wlan-log-boot-1`）也顯示，修正之前一般和 NonSyncable 的密碼查詢各 22 次 `-25300`。
- **根因**：AKS 替身（補丁 0038 `sweet-applekeystore`）沒實作 sel 10（wrap）和 sel 11（unwrap）。
- **做法**：
  - 在 0038 加 wrap／unwrap。用固定種子的 HMAC-SHA256 產生 pad 和 tag，blob 格式是 `tag(8) ∥ key ⊕ pad(32)`。
  - 鑰匙圈因此沒有靜態保護（文件註明）。
  - 31241b2 同時加記錄：RSSI_CHANGED 事件有沒有到 wifid。
  - 7294068 把 wrap 定義移開 0043 依賴的 context。
- **復現**：本機 `kernel-local-akswrap-9g-artifact`；要取樣時加 `--sample Preferences,locationd,wifid`（boot arg `sweetsample=`）。
- **主機測試**：沒有單獨主機測試，以實機 unwrap 計數為準。
- **驗證紀錄**：
  - `native-hw000wlan-akswrap-dt-boot-1`，08:01：securityd 的 sel 10 和 sel 11 全部回 0x0（22 次 unwrap，0 次錯誤），鑰匙圈可寫。
  - 同趟電量計：remaining 4234–4248、full 4249 mAh，4.36–4.44 V，100% 是實值。
  - 同趟用 locationd 取樣找到「日期與時間」卡死的根因：fairplayd MIG，修法在 U／B 系列。
- **參考來源**：AppleKeyStore user client 的 selector 編號（依 securityd 實際呼叫觀察）；0038 原本的 AKS 替身。
- **結論／未完成**：鑰匙圈沒有靜態保護，這是已知取捨。
- **出處**：journal `native-hw000wlan-joint1-boot-1`、`native-hw000wlan-akswrap-dt-boot-1`；`work\localbuild\wifi-autojoin-credential-decision-20261117.zh-TW.md`。

### K-060 協商掃描停留時間與 FAST 掃描快取
- **日期**：2026-11-17 10:31（21e2a91）
- **狀態**：已驗證（110 ms 單頻道掃描成功；快取命中）
- **開關**：
  - `fuxiwlandwell=1`（loader 產生器 `--scan-dwell`）：需要和模組協商。
  - `fuxiwlancache=1`（`--scan-cache`）：只重用 5 秒內、成功完成、覆蓋請求範圍、非空的結果。
- **問題／現象**：wifid 帶非零停留時間的掃描被拒（EOPNOTSUPP 102）。
- **根因**：pinned 模組原本不接受指定 dwell。
- **做法**：
  - 和模組協商 dwell extension，模組端由 harness `9b5cfcd`／`4d87ed9` 修改。
  - 加 FAST 掃描快取。
- **復現**：
  - 模組 CI `44002779624`，正式核心 CI `44003964196`。
  - 本機 `kernel-local-scandwell-9g-artifact`：33 項 workflow 主機命令、81 項 manifest、1012 個 production dwell 案例。
  - loader `native-wlan-scandwell-ram9g-loader`（只開 dwell），以及 `native-wlan-scancache-ram9g-loader`（再開 cache）。
- **主機測試**：`test-fuxi-wlan-ios.py`、`test-fuxi-wlan-control.c`、`test-fuxi-ath12k-link.py`。
- **驗證紀錄**：
  - `native-hw000wlan-scandwell-boot-1`，10:34:45–10:39:35：110 ms 單頻道掃描完成（0），BSS 49，原本的 EOPNOTSUPP 102 消失。持久紀錄 `fuxilog\20261117-103948`（boot#58）。
  - `native-hw000wlan-scancache-boot-1`，10:40:10–10:44:59：FAST 命中一次。持久紀錄 `fuxilog\20261117-104546`（boot#59）。
  - 兩趟都還沒有自動加入，後段各有 4 次 scan EINVAL 22，由 K-062 處理。
  - 10:46 唯讀檢查 `statusbar-fix\inspect-wifi-after-scandwell-20261117-1046`：讀 20 個區塊，overlay 寫入 0。
- **參考來源**：Linux cfg80211 `cfg80211_scan_request` 的 `duration` 欄位。
- **結論／未完成**：無。
- **出處**：journal 兩行與「已修非零停留時間掃描遭拒」段。

### K-061 wifid 呼叫追蹤與被拒掃描參數
- **日期**：2026-11-17 10:56（8741795）
- **狀態**：已驗證（診斷）
- **開關**：`fuxiwifitrace=1`（loader 產生器 `--wifi-trace`）
- **問題／現象**：要知道 wifid 從哪裡呼叫 ioctl 383（掃描），以及哪些掃描參數被拒。
- **根因**：（診斷）
- **做法**：最多記兩次 wifid ioctl 383 的呼叫堆疊，並印出被拒掃描的參數。
- **復現**：loader `native-wlan-wifitrace-ram9g-loader`。
- **主機測試**：`test-fuxi-wifi-ios.py`。
- **驗證紀錄**：`native-hw000wlan-wifitrace-boot-1`，10:59:02–11:03:50：定位到 40 頻道、每頻道 110 ms 的預算被拒；FAST 命中一次；仍沒有加入。最高 66.7°C。呼叫點整理在 `work\localbuild\ioctl383-wifid-calls-20261117.json`。
- **參考來源**：本機同版 wifid 反組譯。
- **結論／未完成**：被拒原因交給 K-062。
- **出處**：journal `native-hw000wlan-wifitrace-boot-1`。

### K-062 dwell 掃描分批
- **日期**：2026-11-17 11:09（69ea46c）
- **狀態**：已驗證（模組 NULL 合約修正後）
- **開關**：`fuxiwlanbatch=1`（loader 產生器 `--scan-batch`）
- **問題／現象**：40 頻道×110 ms 超過模組和 bridge 的逾時預算。
- **根因**：
  - 預算問題。
  - 另外，首測在 pinned Linux 的 `rdev-ops.h:463` 被拒 EINVAL 22：被動掃描時 `n_ssids=0`，`ssids` 卻不是 NULL。
- **做法**：
  - 在現有逾時內把掃描分成 31＋9 兩批。
  - 模組端 harness `b2876c6` 修 NULL 合約，模組 CI `44010197996`。
- **復現**：loader `native-wlan-scanbatch-ram9g-loader`；核心要搭配 `44010197996` 之後的模組。
- **主機測試**：`test-fuxi-wlan-ios.py`。
- **驗證紀錄**：
  - `native-hw000wlan-scanbatch-boot-1`，11:13:29–11:18:18：模組修正前，第一段 31 頻道仍回 EINVAL 22，沒進第二段。
  - `native-hw000wlan-passivespf-boot-1`，11:35:14–11:40:05（本機核心 `6f28814`＋模組 `44010197996`）：31＋9 被動掃描多次成功，BSS 52–54；仍沒有加入。持久紀錄 `fuxilog\20261117-114041`（boot#62）。
- **參考來源**：Linux `net/wireless/rdev-ops.h`。
- **結論／未完成**：無。
- **出處**：journal 兩行。

### K-063 wifid 精確路徑注入
- **日期**：2026-11-17 19:23（3d8e39e）
- **狀態**：已驗證（診斷用）
- **開關**：fxinj bit 8（ADT `fuxi,inject` 的 8），只比對 wifid 的精確路徑。
- **問題／現象**：自動加入的決策在 wifid 內部，需要把診斷 dylib 注入 wifid。
- **根因**：（診斷）
- **做法**：0118 exec 注入加一個位元，只在 exec 路徑完全等於 wifid 時注入。
- **復現**：loader 產生器 `--wifi-audit`：要求 `--wifi-ios`、核心含 `wifid gets` 字串、命令列沒有 `fxinj=`，原 `fuxi,inject` 必須是 2 或 6，再 OR 上 8。診斷 dylib 見 U 系列。
- **主機測試**：`test-fuxi-exec-inject.py`。
- **驗證紀錄**：
  - `native-hw000wifi-filter-feedback-audit-boot-1`，19:28:53–19:34:43（本機 `kernel-local-wifiaudit-9g-artifact`）。
  - `native-hw000wifi-candidate-sound-audit-boot-1`，19:47:37–19:52:26。
  - `native-hw000wifi-decisions-corehaptics-boot-1`，20:06:13–20:11:02。
  - 三趟都成功注入並記到原生決策。
- **參考來源**：K-020 fxinj 0118。
- **結論／未完成**：決策結論見 K-064。
- **出處**：journal 三行與「Wi-Fi 已縮到最後的原生加入資格檢查」段。

### K-064 回答 RSSI_BOUNDS（自動加入修好）
- **日期**：2026-11-17 22:17（05ed0fa）
- **狀態**：已驗證（使用者確認「wifi 終於會自己連線了」）
- **開關**：ADT `fuxi,wifi-rssi=1`；loader 產生器 `--wifi-rssi-bounds`。
- **問題／現象**：
  - 已知網路通過 profile 匹配和排序，但 `__allowJoinCandidate:trigger:defer:error:` 一直回 allowed=0，NSError code=1。
  - 原生 NSError 擷取把原因縮到「Below minimum RSSI threshold」。
- **根因**：wifid 讀 RSSI_BOUNDS（Apple80211 get 106）被拒，拿不到門檻，就把每個候選判成太弱。
- **做法**：
  - `fuxi_wifi_apple80211.c`：開關打開時，get 106 回 version 1、上下限 −128／127、門檻 −128 dBm；關閉時照舊拒絕。
  - UniXNU zizhan 用同樣的值。
  - 同一個 commit 也加了 DIRECT_PLAY 振幅（K-033）和 quiet boot（K-065）。
- **復現**：loader `native-rssiamp-quiet-1117-2225-ram9g-srgb-loader`（`--wifi-rssi-bounds --quiet-boot --haptic-amp 128`），本機 `kernel-local-rssiamp-quiet-9g-artifact`，dylib `sb-local-hapticamp-sbwifi-artifact`。
- **主機測試**：`test-fuxi-wifi-ios.py`。
- **驗證紀錄**：
  - `native-hw000wifi-hapticfb-boot-1`，21:34：定位根因，仍沒有 join finished。console SHA-256 `df846f2f…c3`，mtd `fuxilog\20261117-212954`。
  - `native-hw000wifi-rssiamp-boot-1`，22:25：join finished 10 次、DHCP 5 次，使用者確認自動加入。console 27075 行，SHA-256 `3e5a033b09196d09140abd276b2675c84af60d7f2f7181b115cbb13cd0161249`，mtd `fuxilog\20261117-222609`。
- **參考來源**：UniXNU zizhan 的 RSSI_BOUNDS 回答；本機同版 CoreWiFi 反組譯。
- **結論／未完成**：使用者接著回報四個體驗問題，見 K-066。
- **出處**：journal「最新對齊（2026-11-17 23:30）」段。

### K-065 安靜開機（不畫 console 跑馬燈）
- **日期**：2026-11-17 22:17（05ed0fa，補丁 0119）
- **狀態**：已驗證
- **開關**：`sweetfb_noconsole=2`；loader 產生器 `--quiet-boot`。
- **問題／現象**：開機時螢幕會畫 XNU console 文字。
- **根因**：SweetFramebuffer 預設把 console 畫到螢幕上。
- **做法**：`fuxi-unixnu\SweetFramebuffer.cpp`（補丁 0106）加上 `0119-sweet-quiet-boot.patch`，值 2 時不畫 console 文字。USB console 和 mtdblk 紀錄照常。
- **復現**：`--quiet-boot`。
- **主機測試**：無專用測試。
- **驗證紀錄**：22:25 那趟（K-064）不再出現跑馬燈，USB console 照常完整。
- **參考來源**：0106 的 SweetFramebuffer（v7 以前用 `sweetfb_noconsole=1`）。
- **結論／未完成**：日常 chain-v8 已帶 `sweetfb_noconsole=2`（`work\dualboot\daily-merge-20261118\comparison.json`：v7 為 1、v8 為 2）。
- **出處**：journal 22:25 段。

### K-066 fuxi,wifi-ux：電源回報、依速率選 AP、同台不斷線（bit 0～2）
- **日期**：2026-11-17 23:16（aa89a3e）
- **狀態**：已驗證
- **開關**：ADT `fuxi,wifi-ux`（位元遮罩，預設 0）；loader 產生器 `--wifi-ux <值>`（會檢查核心有沒有 `FUXI WIFI IOS: ux options` 標記和各位元的字串）。
  - bit 0（1，`FUXI_WIFI_UX_POWER_ASKED`）：開關剛按下、電源還沒切完時，GET POWER 回報要求的狀態；關閉進行中拒絕掃描和加入。
  - bit 1（2，`PICK_RATE`）：wifid 沒指定 BSSID 時，照 wpa_supplicant 的比較法用估計速率選 AP。
  - bit 2（4，`KEEP_LINK`）：已連線時又要求加入同一台，就不先斷線。
- **問題／現象**：使用者回報四個問題：
  1. 關 Wi-Fi 時開關閃約 2 秒才關。
  2. 打開後掃描慢。
  3. 速率最多約 100 Mbps（另一支手機 300 Mbps）。
  4. 偶爾斷線又恢復。
- **根因**：
  - 關閉排隊或執行中時，GET POWER 仍回報「開」，開關就彈回去。
  - 選 AP 只比 RSSI，常選到 2.4 GHz。
  - wifid 對已連線的同一台再要求加入時，驅動先斷線再重連。
- **做法**：`fuxi_wifi_sta.c`／`.h`、`fuxi_wlan_ios.inc`、`fuxi_wifi_apple80211.c`；harness `0392fff`；產生器 `--wifi-ux`（主倉庫 `45b700d`）。
- **復現**：本機 `kernel-local-wifiux-9g-artifact`（38 項 PASS），loader `native-wifiux-1117-2320-ram9g-srgb-loader`（同 K-064 再加 `--wifi-ux 7`）。
- **主機測試**：`test-fuxi-wifi-ios.py`、`test-fuxi-wlan-ios.py`。
- **驗證紀錄**：`native-hw000wifi-wifiux-boot-1`，23:20–23:42:46（900 秒），console 36080 行，SHA-256 `2580304451062ab6d2305dd92ae7f3ea05a597d88c7ecddd0dbf114ee9e21652`，最高 75.7°C。
  - ux 0x7 生效：每次都選 5200 MHz（估計 648–720 Mbps），不選較強的 2.4 GHz。join finished 0 四次、16 一次。
  - 「轉圈」那次的成因：
    1. 掃描中切換開關，關閉要求被後到的開啟覆蓋，radio 從沒關過。
    2. iOS 端收到「開」仍清掉連線狀態，但 mac80211 還連著同一台。
    3. 再認證回 -EALREADY（狀態 2 → EBUSY），中止流程送 deauth，下一輪才連上。
  - 這個成因由 K-067 的 bit 4 修。
- **參考來源**：wpa_supplicant `wpa_scan_result_compar`（估計速率比較）。
- **結論／未完成**：使用者回報速率有升，但沒到 300 Mbps；重開後搜尋仍慢。
- **出處**：journal「最新對齊」段與 `native-hw000wifi-wifiux-boot-1`。

### K-067 國家碼、相同電源狀態不重做、計時、ADT fuxi,args（bit 3～4，補丁 0121）
- **日期**：2026-11-18 00:02（497a117）
- **狀態**：已驗證（日常 chain-v8 用 ux=31）
- **開關**：
  - `fuxi,wifi-ux` bit 3（8，`COUNTRY`）：SET 51 收下並記住國家碼。只記住，不改 Linux 法規域，也不發事件。
  - bit 4（16，`POWER_SAME`）：要求的電源狀態和現在一樣（例如關被開覆蓋）時，不呼叫底層、不清連線。
  - ADT `fuxi,args`（補丁 `0121-fuxi-adt-wifi-args.patch`）：驅動啟動後才讀的開關可以放這裡，不佔 607 bytes 命令列。loader 產生器 `--wifi-args-adt`。`fuxiwlan=1` 和測試用的 `fuxiwifitest*` 仍留在命令列。
- **問題／現象**：K-066 的轉圈；命令列已滿，放不下 chain-v7 的調校項目。
- **根因**：同 K-066 的分析。
- **做法**：
  - `fuxi_wifi_apple80211.c`／`fuxi_wifi_sta.c` 實作 bit 3／4，並加開關切換和掃描的耗時紀錄。
  - 0121 讓核心從 ADT `/chosen fuxi,args` 讀參數。
  - 產生器 `--daily-tuning` 把 chain-v7 的 7 項調校放回命令列。
- **復現**：loader `native-v7wifi-1118-0030-ram9g-srgb-loader`（`--daily-tuning --wifi-args-adt --wifi-ux 31`），本機 `kernel-local-wifiux2-9g-artifact`。
- **主機測試**：`test-fuxi-wifi-ios.py`、`ksrc\tools\test-fuxi-adt-args.py`；產生器 self-test 有 `fuxi,args` 的負測試（重複 token、空字串、過長、NUL）。
- **驗證紀錄**：`native-hw000wifi-v7wifi-boot-1`，00:31–00:49:30（900 秒），console 25431 行，SHA-256 `822c283c24fb1a66e4b4e57c0bb515cf179b04840c92f563b9f8018f3c71f83c`，USB 斷 2 次都重連，最高 62.3°C。
  - ux 0x1f 生效，ADT 參數有讀到；開機自動加入 438 ms。
  - 第一次量到掃描耗時：2.4 GHz 13 頻道約 0.94 秒，5 GHz 27 頻道約 7 秒，全頻 16 秒（每個 5 GHz 頻道約 260 ms）。
  - 同趟先用 `phone-readthrough.py --install-app` 裝酷安（T／U 系列）。
- **參考來源**：iOS 16 wifid SET 51（本機反組譯）。
- **結論／未完成**：日常 chain-v8 合入這版（ADT `fuxi,wifi-ux=31`、`fuxi,wifi-rssi=1`，B 系列）；掃描慢留給 K-068／K-069。
- **出處**：journal `native-hw000wifi-v7wifi-boot-1`、「日常 v8 合併」。

### K-068 限制隱含掃描範圍＋分組停留（失敗、已回退）
- **日期**：2026-11-18 01:03（a23e701），01:09 由 490764d 回退
- **狀態**：放棄
- **開關**：當時 `fuxi,wifi-ux` bit 6（ux=127）
- **問題／現象**：想把全頻 16 秒的掃描縮短。
- **根因**：分組 dwell 在真實韌體上分批耗時過長；主機測試沒涵蓋。
- **做法**：（已回退）未指定頻道時改用 iOS 公布的頻道清單，再用模組的 dwell extension 依主動、被動、雷達頻道分組。
- **復現**：不建議重現。回退方式：
  - `git diff 497a117 HEAD` 為空。
  - harness `patches/kernel-adsp-battery.patch` 回到 `05880e1`。
  - loader 產生器回到主倉庫 `dbbb912`。
- **主機測試**：當時兩份 Wi-Fi ASan／UBSan、loader self-test 和 38 項 workflow 命令全過，但都沒涵蓋真實韌體的分批逾時。
- **驗證紀錄**：
  - `native-hw000wifi-scanscope-boot-1`（loader `native-scanscope-1118-0105-ram9g-srgb-loader`），01:05:32：第一輪 15 頻道分五批，30,001 ms 逾時（error 60），native worker 隨後退出。使用者回報掃描沒結果、關不了 Wi-Fi。USB SHA-256 `447410a777ec6d3598d5fa67dc6c38f4915802e9ed7e00c1266020613728eb18`，最高 61.1°C。
  - 回退後 `native-hw000wifi-rollback-boot-1`，01:12:56：重開 `native-v7wifi-1118-0030-ram9g-srgb-loader`（boot SHA-256 `7a66e622c513473d2426827b95b837943c79dbd229f49676a3c49e0f0d84da27`）。POWER off 2 ms／on 18 ms，掃描 7,427 ms、39 BSS；第一次加入在四向握手失敗（error 80），留給使用者檢查。
- **參考來源**：無。
- **結論／未完成**：
  - 這組設定拒用。
  - 國家碼只在 iOS 層保存；底層是不是 world 法規域、舊全頻掃描有沒有包含 6 GHz，都未證實。
- **出處**：`work\localbuild\wifi-scan-validation-20261118.zh-TW.md`「失敗與限制」「回退方式」。

### K-069 掃描中止／逾時恢復與公布頻道掃描（bit 5～6）
- **日期**：2026-11-18 11:11（a41069f）
- **狀態**：已驗證（實機兩趟；日常還沒合入）
- **開關**：
  - `fuxi,wifi-ux` bit 5（32，`SCAN_RECOVER`）：已受理的掃描被中止或逾時後，保留電源控制。掃描沒回完成事件前，先擋下一筆掃描和加入，等原事件到，或切換電源更新 generation。
  - bit 6（64，`SCAN_CHANNELS`）：未帶頻道的掃描改用 iOS 公布的 2.4／5 GHz 清單。指定的頻道和停留時間照傳，預設停留時間的掃描維持一批。
  - 兩個都預設關。
- **問題／現象**：K-068 失敗後，需要先讓「掃描卡住就關不了」不再發生，再安全地縮小範圍。
- **根因**：掃描沒回完成事件時，電源控制被卡住。
- **做法**：沿用既有 ath12k 模組的 SCAN_DONE 和電源確認契約；harness `522e94b`。
- **復現**：
  - 本機 `kernel-local-wifi-recover-1118-9g-artifact`：建置 101 秒，38 項 workflow 主機命令、86 項產物雜湊。
  - loader `native-scan-ux63-1118-ram9g-loader`、`native-scan-ux127-1118-ram9g-loader`：同核心、同 RTC 校時、同命令列，payload 只差 ADT 掃描範圍位元。
- **主機測試**：`test-fuxi-wlan-ios.py`。ASan／UBSan 涵蓋 reply／SCAN_DONE 先後到達、批次逾時、切電源時中止、晚到的完成事件和之後的重掃。
- **驗證紀錄**：
  - `native-hw000wifi-scan-ux63-1118`，11:16:16：兩輪關／開自測通過，各一次 join finished 0。兩次全頻掃描 15,784 和 15,769 ms（各一批）。USB SHA-256 `4b68768dcd506ba2adb71c398ac54716b785d97e6bcbe806b8c00aea3592b673`，最高 65.5°C。11:26:27 等不到 Android ADB（常駐選單要按鍵確認），啟動器自動釋鎖。
  - `native-hw000wifi-scan-ux127-1118`，11:39:51：
    - 40 頻道一批掃描 11,295 ms、27 BSS，比全頻平均 15,776.5 ms 少 28.4%（關開後只量到一次）。
    - 第二輪關閉 104 ms 內完成，但 wifid 在自測讀電源前又打開了，自測回 EIO 5 提前結束。
    - 兩次在 5200 MHz 加入成功。
    - USB SHA-256 `d480190d19ba8be0a39a8280a7e59ea9cb072147b4141ebca4cb9e4d47cb981e`，最高 79.3°C。11:45:10 回 Android。
- **參考來源**：UniXNU Ath12kWifi `src/nw_sta.c`（公布頻道清單）。
- **結論／未完成**：下載吞吐和長時間連線還沒測；日常 chain-v8 仍是 ux=31。
- **出處**：`work\localbuild\wifi-scan-validation-20261118.zh-TW.md`「11:16 起續修」「11:39 的 40 頻道對照」。

### K-073 優先處理 Wi-Fi 關閉、批次接收與連線統計（bit 7～9）
- **日期**：2026-11-18 12:05（af3c264）；上機 12:26（ux1023）
- **狀態**：實驗（ux1023 上機一趟；ux767 對照未跑）
- **開關**：
  - `fuxi,wifi-ux` bit 7（128，`POWER_OFF_FIRST`）：先做完排隊中的關閉，再受理後到的開啟；關閉可中止掃描和進行中的加入。
  - bit 8（256，`RX_BATCH`）：用既有 ath12k 封包讀取回呼，直接填進 mbuf，批次交給 XNU。
  - bit 9（512，`LINK_STATS`）：有流量時每 5 秒印一次 PHY TX 速率和 en0 位元組計數。
  - `FUXI_WIFI_UX_ALL` 是 1023。
- **問題／現象**：使用者回報「設定 → Wi-Fi」關閉不穩，下載最高約 210 Mbps。
- **根因**：
  - 關閉和開啟的要求次序沒有保證。
  - 接收路徑逐封包複製再交給 XNU。
- **做法**：`fuxi_wifi_sta.c`／`.h`、`fuxi_wifi_net.h`、`fuxi_wlan_ios.inc`、`fuxi_wlan_ath12k.h`；harness `7900eac`。
- **復現**：
  - 本機 `kernel-local-wifi-power-rxbatch-1118-9g-artifact`：建置 64 秒，38 項 workflow 主機命令通過。
  - loader `native-wifi-power-ux767-1118-ram9g-loader`（關閉批次接收）和 `native-wifi-power-ux1023-1118-ram9g-loader`（開啟）：逐位元比對只差接收批次開關，兩者都保留實際 Settings 操作。
  - 上機要有 USB 連線和有效的 RTC 校時（一小時內）。
- **主機測試**：`test-fuxi-wlan-ios.py`、`test-fuxi-wifi-ios.py`。
- **驗證紀錄**：
  - 對照組：上一趟日常 ux=31 的持久紀錄 boot#96（`fuxilog61118-122545`），其中一筆關閉排隊 4,786 ms，另一筆在掃描期間被隨後的開啟蓋過。
  - `native-hw000wifi-power-ux1023-1118-1225`（loader `native-wifi-power-ux1023-1118-1225-loader`），12:26:14 RAM 開機：
    - 兩次在設定頁關閉，都在 67–71 ms 完成；第一次中止了未完成的掃描。
    - 第一次重新加入成功後約 50 秒沒有接收，第二次關開後恢復流量。
    - 使用者回報約 220 Mbps；en0 最高的 5 秒接收平均 196.88 Mbps，同區間 TX PHY 採樣 576.4 Mbps。
- **參考來源**：UniXNU-sheng `package/WifiNet.cpp`（commit `68800685763d755c368775b848dd703fb3f8a3ce`）的接收批次作法。
- **結論／未完成**：
  - 關閉優先已見效（4,786 ms → 67–71 ms）。
  - 「加入後約 50 秒無接收」未解。
  - 待同核心不分批（ux767）對照吞吐。
- **出處**：journal「2026-11-18 12:06 Wi-Fi 關閉與下載續修」。

### K-074 ath12k 接受韌體配置的 peer ID 0

- **日期**：2026-11-18 12:25–13:00（台北時間）
- **狀態**：修正版完成一趟暫時開機驗證，重連後即有資料流量
- **開關**：使用含 peer ID 0 修補的 ath12k 物件。對照組與修正版都開啟接收批次（ADT fuxi,wifi-ux=1023），沿用相同命令列與 root extents。
- **問題／現象**：對照趟第一輪重新加入成功後，en0 一段時間收不到資料。TX completion 顯示 peer ID 0 查找錯誤。
- **根因**：釘選的 Linux 7.0.14 ath12k 把韌體可合法配置的 ID 0 當無效值。上游修補保留 ID 上限，移除 ID 0 的特例拒絕。
- **做法**：harness 9ca823f 回移植修補，ksrc 1431ae5 與 harness 91698cf 讓連結檢查接受該份經驗證的物件。工作區 cca035e 登記已驗證的 loader 輸入。CI 使用固定來源的 ASan／UBSan 對照驗證舊版會拒絕 ID 0，修正版可接受。
- **復現**：取 macOS run 44239539371 的 fuxi-ath12k-objects 與 manifest。執行 build-kernel.ps1 -Ram 9g -Name kernel-local-wifi-peer-zero-1118-9g-artifact -Tests -Ath12k <物件目錄>。以接收批次對照趟為 --like 基底，依總索引步驟 5–6 換入該核心。使用歷史 loader native-wifi-peer-zero-1118-1252-loader 時，先依目前裝置核對 root extents、RAM 與 RTC。進設定關開 Wi-Fi 並測速，核對首次連上後第一個五秒 en0 讀值和 TX completion。
- **主機測試**：本機 9g 建置 68 秒、38 項 workflow 主機命令通過。CI 30 項物件檢查通過。連結條件見 tools/test-fuxi-ath12k-link.py。
- **驗證紀錄**：native-hw000wifi-peer-zero-1118-1252 於 12:53:51 開機，第一次重連 5200 MHz 的首個五秒取樣收到 38,461 bytes。峰值五秒平均接收 228.45 Mbps，該區間 TX PHY 864.6 Mbps、RSSI −59 dBm。查找錯誤與 panic 均為零，12:59:45 回 Android。原始 boot.log SHA-256 為 91d073877da8462f8e67af85cfd19e8403b8f9c5b0c8fa8a5b1f8d184a54c40b。
- **參考來源**：[上游 ath12k 修補](https://lkml.iu.edu/2605.1/06622.html) · [macOS CI 44239539371](https://github.com/scyx2611/fuxi-metal-frontend-check-20261108/actions/runs/44239539371)
- **結論／未完成**：此趟驗證首次重連就恢復資料與測速流量。持續使用與跨重開機留待各版本的獨立紀錄。
- **出處**：work/hw000-adaptation-status.zh-TW.md 的 12:06 段落、work/Codex-Xiaomi13-GPU-20261109/native-hw000wifi-peer-zero-1118-1252/boot.log、work/adsp-battery/prepare-wlan-w18-loader.py。

### K-075 ath12k WMI 掃描優先級與首輪掃描時間

- **日期**：2026-11-18 13:13–13:33（台北時間）
- **狀態**：修正版暫時開機三輪設定頁關開通過
- **開關**：核心按需 Wi-Fi 追蹤 fuxiwifitrace。A/B 使用相同 ADT、命令列與 root extents，變更 ath12k 物件的 WMI priority 欄位。
- **問題／現象**：開啟 Wi-Fi 後首輪僅三個頻道，仍等了 7,395 ms。事件顯示 802.11d 啟動後 7,021 ms 才有頻道切換，BSS 回報後 wifid 立即讀取。
- **根因**：ath12k 送 WMI 掃描命令時須輸出已選擇的掃描優先級。補上該欄位後，手機的首輪切頻和 BSS 結果顯著提前。數字用以界定這次變更的實機效果。
- **做法**：ksrc 43b2e2b 補 fuxi_wlan_ath12k.inc、fuxi_wlan_ios.inc 與 fuxi_wifi_sta.h 的時間／韌體事件欄位。harness 7387e88 補 WMI 封包序列化，ksrc 5b0e0b3 與 harness be60a9f 更新物件輸入檢查。韌體仍執行 802.11d 掃描。
- **復現**：以 macOS run 44243587936 的 fuxi-ath12k-objects 和 manifest 編本機 9g 核心。對照 loader native-wifi-scan-trace-1118-loader，修正版 native-wifi-scan-priority-1118-loader。依總索引步驟 5–6 檢查兩份輸入同條件並開機，在設定頁關閉再開啟 Wi-Fi，量首輪 5200／2412／5745 MHz 和「首輪送出到 wifid 掃描完成」兩段時間。每輪再檢查加入後 en0 接收計數。
- **主機測試**：9g 核心 59 秒，38 項 workflow 主機命令、31 項 ath12k 物件檢查通過。
- **驗證紀錄**：對照 native-hw000wifi-scan-trace-1118 的設定頁掃描完成為 9,380 ms。修正版 native-hw000wifi-scan-priority-1118 的三輪為 1,646／1,629／1,723 ms，三頻首輪為 414／408／414 ms。三輪首次自動加入 5200 MHz 均收到資料。13:33:28 回 Android 並釋鎖，使用者確認明顯提速。
- **參考來源**：[ath12k 802.11d offload 變更](https://lists.infradead.org/pipermail/ath12k/2025-March/006346.html) · [macOS CI 44243587936](https://github.com/scyx2611/fuxi-metal-frontend-check-20261108/actions/runs/44243587936)
- **結論／未完成**：確認這組 AP 與設定頁操作的掃描延遲下降。下載 220 Mbps 是 K-073／K-074 所列對照的驗收結果。掃描速度分別以首輪完成時間和設定頁整段時間記錄。
- **出處**：work/hw000-adaptation-status.zh-TW.md 的 13:13 段落、work/Codex-Xiaomi13-GPU-20261109/native-hw000wifi-scan-trace-1118/boot.log 第 6627–6719 行、native-hw000wifi-scan-priority-1118/boot.log 第 5439–8126 行。

### K-076 WCN7850 藍牙韌體下載與 HCI reset（fuxi,bt=4）
- **日期**：2026-11-18 17:03（ksrc a4d9402）、17:06（b5b540a NVM 檔名）
- **狀態**：實驗（已通：韌體下載、HCI reset、讀版本；iOS bluetoothd 傳輸未做）
- **開關**：ADT `fuxi,bt=4`（或 `fuxibt=4`）；loader `prepare-wlan-w18-loader.py --bt 4`（另檢查核心含 `FUXI BT: setup status=` 與 `/usr/local/share/fuxi/bt/` 字串）。韌體要先用 `stage-wlan-assets.py` kind=bt 放進 iOS root（U-020）。
- **問題／現象**：K-039 只做到上電與版本查詢；控制器沒有下載 rampatch／NVM，iOS 的 bluetoothd 不能用。
- **根因**：WCN7850 開機時只跑 ROM，要由主機下載 rampatch（`hmtbtfw20.tlv`）與板級 NVM（`hmtnv20.b<bid>`），之後 HCI reset 才是可用的控制器。
- **做法**：
  - `xnu\fuxi-bluetooth\fuxi_bt_setup.c`／`.h`：可攜的設定引擎，流程照 Linux btqca `qca_uart_setup`：版本查詢 → rampatch（TLV type 1，download mode 3 不等 VSE／CC）→ 讀 board ID → NVM（清掉 HCI tag 的 IBS bit 0x80、baud 欄位設 115200；清 deep sleep tag 27 bit 0）→ build info → HCI reset → local version 與 BD address（位址不記入 log）。全程 115200 baud，結束後照 K-039 還原。
  - 韌體從 `/usr/local/share/fuxi/bt/` 讀，不編進核心。
  - b5b540a：NVM 檔名照 Linux `qca_get_nvm_name_by_board` 的 `"%s%02x.b%02x"`（bid 0x0a → `hmtnv20.b0a`、0x10a → `hmtnv20.b10a`）；bid 為 0、0xffff 或讀不到時，或板級檔不存在時，照 `qca_get_alt_nvm_file` 改讀 `hmtnv20.bin`。原先誤寫成 `hmtnv20.10a`，對照手機 `/vendor/bt_firmware/image` 的實際檔名後修正。
- **復現**：
  1. `stage-wlan-assets.py` 以 `work\adsp-battery\bt-stage-input-20261118.json` 部署（U-020）。
  2. `prepare-wlan-w18-loader.py build --like native-daily-wifi-sb103-1118-loader --wifi-ios --artifact kernel-local-btsetup-autores-9g-artifact --name native-btsetup-1118-loader --rtc-calibration <json> … --bt 4`（其餘參數同 hapticfb3，見 journal）。
  3. `launch-loader-check.ps1 -Loader native-btsetup-1118-loader -Kernel kernel-local-btsetup-autores-9g-artifact -Out native-hw000btsetup-1118 -Seconds 210 -Reset 6`。
  - 成功判據：`FUXI BT: board WLAN_EN steady after` → `setup file hmtbtfw20.tlv bytes=231484` → `setup file hmtnv20.b<xx>`（或 `.bin`）→ `setup status=ok stage=done`。失敗看 `status≠ok` 的 `stage=` 或 `open errno`。
- **主機測試**：`ksrc\tools\test-fuxi-bluetooth-setup.py`（UART 分段收發、三種 download mode、控制器錯誤、壞檔與 framing 模糊測試、bid 0x0a／0x14／0x1234／0／0xffff 與 `.bin` 退回）；`test-fuxi-bluetooth-xnu.py`。另用暫存 harness 把手機實際的 1 份 rampatch 與 48 份 NVM 交給 `fuxi_bt_tlv_patch`／`fuxi_bt_tlv_nvm` 解析，全部通過。WSL 一次跑完：`work\localbuild\run-bt-haptic-tests.sh`。
- **驗證紀錄**：btsetup 趟 2026-11-18 17:20:45 暫時開機、17:27:47 回 Android（`native-hw000btsetup-1118`，loader `native-btsetup-1118-loader`，本機核心 `kernel-local-btsetup-autores-9g-artifact`＝ksrc b5b540a、ath12k `b2876c6`）。boot.log 第 2821–2835、4536–4564 行：
  - `board WLAN_EN steady after 15043 ms` → `setup file …/hmtbtfw20.tlv bytes=231484` → `setup file …/hmtnv20.bin bytes=9036`；
  - `setup status=ok stage=done cleanup=ok controller_status=0 elapsed_ms=23269 patch_ms=21775 tx=246493 rx=1620 frames=54`；
  - rampatch `type=1 mode=3 … segments=953 final_ack=1`；`board_id=0 valid=1`，所以照 Linux 讀 `hmtnv20.bin`（`tags=90 segments=38 acks=38`，HCI tag `151122->150022`、deep sleep `0x1->0`）；
  - `build_info status=ok "BTFW.HAMILTON.2.0.0-00613-PATCHZ-1.35836.5.36233.6"`（與 `hmtbtfw20.ver` 相同）；HCI reset 後 `local hci=0xc rev=0 lmp=0xc manufacturer=0x1d subver=0x5e21`（Bluetooth 5.3、Qualcomm，subver 等於 rampatch 的 patch 版本）；`bdaddr valid`；
  - `fifo … error=ok fault=0 tx=246493/246493`、`board restore status=ok`。主機 tsens 最高 49.3°C。
- **參考來源**：Linux `drivers/bluetooth/btqca.c`（`qca_uart_setup`、`qca_tlv_check_data`、`qca_get_nvm_name_by_board`、`qca_get_alt_nvm_file`）、`hci_qca.c`。
- **結論／未完成**：
  - 控制器已能在 iOS 開機時載入原廠韌體並回應標準 HCI；這顆機器 board ID 為 0，用 `hmtnv20.bin`。
  - 115200 baud 下載要 22 秒；之後要接 bluetoothd 時應照 Linux 先用 QCA vendor 命令升速（sheng 用 51.2 MHz 時脈、3.2 Mbaud）。
  - 下一步是 iOS 端傳輸：bluetoothd 走私有 kernel control `com.apple.uart.sk.bluetooth`＋Skywalk channel（ksrc `docs-lanes\bluetooth-ios16-abi.md`）。可對照本機 `work\ref-sheng-bt\QcaBluetooth`（`src\qbt_transport.c` 的 UART.SK、`native-ios27.md` 的 bluetoothd 改寫點；目標是 iPadOS 27 arm64e，iOS 16 的 bluetoothd 是 arm64，改寫點要重找）。
- **出處**：ksrc a4d9402、b5b540a；harness 8fb7a23（0117 同步）。

### K-077 WCN7850 UART.SK 與 Skywalk 常駐收送

- **日期**：2026-11-18。
- **狀態**：11-19 設定頁關閉再開啟後，Windows 重新連線並即時讀回 5 項 RFCOMM 服務。iOS 另建立過對外 BLE 連線。使用者已配對 Apple 耳機（趟 17），趟 20 換到 3.2 Mbaud 後確認 A2DP 聲音正常。關藍牙閃退與長時間穩定性待驗。
- **開關**：ADT `fuxi,bt=5`，自測另加 `fuxi,bt-probe=1`；原生 bluetoothd 用 `--bt-native`（另開 `fuxi,inject` 位元 32）。UART 換速 `fuxi,bt-baud`（u32，loader `--bt-baud`）與控制器日誌關閉 `fuxi,bt-nolog`（loader `--bt-nolog`），預設都關（趟 20）。
- **問題／現象**：K-076 已取得 WCN7850 韌體和標準 HCI 回覆，需要將初始化完成的控制器交給 iOS。
- **根因**：模式 4 在韌體初始化後關閉控制器，iOS 的 UART.SK 控制和 Skywalk 收送仍缺後端。
- **做法**：模式 5 沿用 K-076 的韌體初始化，重新取得同一個 GENI UART 並透過 `com.apple.uart.sk.bluetooth` 提供 H4 收送。WLAN_EN 掉低或 UART 出錯時停止並還原板級配置。Skywalk 通道綁定控制 socket 的程序，自測模式額外讓平台簽署的 SpringBoard 連線。自測連線兩次，各送一次 HCI Read Local Version，記錄 ring ID、HCI 狀態和廠商。
- **復現**：使用 `work/adsp-battery/ksrc` 的 `tools/sync-fuxi-adsp.py` 產生 0117，再同步到 harness 的 `patches/kernel-adsp-battery.patch`。以同一份本機工作目錄建置核心、函式庫及測試 loader。

  ```powershell
  .\work\localbuild\build-kernel.ps1 -Ram 9g -Name kernel-local-bt5-prm-haptic-r2-1118-9g-artifact -Ath12k work/adsp-battery/ath12k-objects-b2876c6 -Tests
  .\work\localbuild\build-sb-shape.ps1 -Name sb-local-bt5-prefs-codex-r3-1118-artifact
  python -X utf8 work/adsp-battery/prepare-wlan-w18-loader.py build --like native-btsetup-1118-loader --artifact kernel-local-bt5-prm-haptic-r2-1118-9g-artifact --name native-bt5-prm-haptic-codex-1118-loader --wifi-ios --wifi-args-adt --with-adsp --audio-spf --audio-prm --bt 5 --bt-probe --haptic 3 --inject-media --product-haptics --haptic-ui-ms 15 --haptic-envelope --haptic-amp 128 --wifi-audit --wifi-password-prompt --wifi-sb-audit --wifi-sb-refresh --wifi-rssi-bounds --wifi-ux 2047 --stderr-log mediaserverd,Preferences --rtc-calibration <本輪RTC紀錄>
  ```

- **上機配置**　以 `launch-loader-check.ps1` 使用上述 loader、對應核心與 `-Dylib`／`-DylibSha`，`-Seconds 155 -Reset 5`。同趟收 PRM 與 Preferences 證據。這份 loader 採用 SpringBoard HCI 自測，`sweetnoexec` 仍擋 bluetoothd。
- **原生程序試驗**　UART.SK 自測後，以同份核心和 Wi-Fi 配置改用 `--bt 5 --bt-native --stderr-log bluetoothd,BlueTool`，省略 `--bt-probe`，帶本輪 RTC 校時。`--bt-native` 從 `sweetnoexec` 移除 `bluetoothd`，其餘程序名仍由來源 loader 指定。基準 console 已確認此前由 `sweetnoexec` 擋住。離線候選 `native-bluetoothd-smoke-offline-1118-loader` 的 boot SHA-256 為 `a25f84a6c8cbbac2e24b6c6a11f0c94ae68ba3a149362b8d3ed23fa713a63aaf`。首趟看 bluetoothd 啟動、BlueTool 回應與 UART.SK rings。掃描、配對和重連另有各自的實測判據。
  - 已核對本機 20H392 二進位檔的 BlueTool XPC 接口。BlueTool `0x100041e28` 讀 `kMsgId`、`kMsgArgs`、`script`，分派 `RunBuiltinScript`／`RunCommand`。bluetoothd `0x100272034` 寫 `script` 及選用的 `btaddress`，回覆函式 `0x10027249c` 處理 int64 `result`、字串 `firmware` 和 int64 `manufacturer`，`result=0` 分支記錄腳本正常完成。Sheng 的 BlueTool adapter 可對照這套欄位。實際 20H392 的啟動流程、控制器選擇與藍牙位址處理待上機確認。
- **成功判據**　`FUXI BT: setup status=ok stage=done`、`FUXI BT SK: channel rings ready`、HCI opcode `0x1001` 收發、`status=0`、`manufacturer=0x1d`、`Bluetooth HCI probe reconnect error=0`。`stage=… error=…` 標出缺少回覆的階段，`resident WLAN_EN lost` 用於區分共用電源中斷。
- **主機測試**：40 項 workflow 命令通過，包括 `test-fuxi-bluetooth-pump.py` 的 FIFO 壓力、H4 framing 和逾時案例。
- **驗證紀錄**：產物在 `work/Codex-Xiaomi13-GPU-20261109/`。WSL 核心經 81 秒建置，`ISA.txt` 解碼 2,555,547 條、禁止指令零條。核心 SHA-256 `031c73b30a19669e6e672fb2bff8c6376ff29db1f3e075f6359d6c4afadf9453`，測試 dylib `5b15e1c82bce26dd318f56d4b37035a06b7ee2deb9b57fcc6261111d5378e955`。既有校時紀錄產生的離線 boot.img `020c5b08f27e0c316cc7daa8567e1343b5730130109c9bd4a5b2b7e601117181`，重跑前核對校時時效。
- **參考來源**：QcaBluetooth 的 UART.SK 傳輸，以及 XNU 8796 的 kernel pipe 和 libsyscall。換速對照 Linux `hci_qca.c` 的 `qca_set_speed`／`qca_disable_soc_logging`、`btqca.c` 的 NVM tag 17，以及 `gcc-sm8550.c` 的 QUPv3 UART 頻率表。 [QcaBluetooth](https://github.com/UniXNU/QcaBluetooth) · [XNU 8796 kernel pipe](https://github.com/apple-oss-distributions/xnu/blob/xnu-8796.141.3/bsd/skywalk/nexus/kpipe/nx_kernel_pipe.c) · [Linux hci_qca.c](https://github.com/torvalds/linux/blob/master/drivers/bluetooth/hci_qca.c) · [Linux gcc-sm8550.c](https://github.com/torvalds/linux/blob/master/drivers/clk/qcom/gcc-sm8550.c)
- **第一趟實機（21:20–21:31）**：loader `native-bt5-tdm-smmu-1118-2125-loader`，核心 `kernel-local-bt5-tdm-smmu-1118-9g-artifact`（本機編），dylib `sb-local-bt5-prefs-codex-r3-1118-artifact`。`FUXI BT: setup status=ok stage=done`（韌體 21.8 秒）、`setup local hci=0xc lmp=0xc manufacturer=0x1d`、`control register error=0`、`resident ready baud=115200`、`channel rings ready` 皆出現。SpringBoard 自測 `stage=read-version error=100`（EPROTO），之後 stats 為 `rx=12/1 dropped=677 acl=0/8`。
  - 判斷：常駐泵把 UART 收到的位元組原樣交給 RX 環，控制器的 QCA 除錯日誌 ACL（raw handle `0x2EDC`，Linux btqca 的 `QCA_DEBUG_HANDLE`）與 IBS 位元組混在 HCI 回覆前，通道在封包中間接上時第一個位元組也不是 H4 開頭。
- **H4 分框修正（21:40）**：`fuxi_bt_pump.c` 改為依 H4 標頭分框後交付，除錯 ACL 整包丟掉，封包邊界外的位元組計入 desync；sink 的 `deliver` 多一個 `start` 參數，`fuxi_bt_sk_receive` 只在封包開頭決定收或丟，通道接上、斷線前後都等下一個封包開頭。stats 增列 `framed=… debug=…/… desync=…`。SpringBoard 自測改為逐包解析，略過其他封包並記錄前 4 個。
  - 主機測試：`test-fuxi-bluetooth-pump.py` 新增 8 組隨機分框（混入除錯 ACL、`0x0edc`、ISO 旗標、SCO、IBS 雜訊，任意收量與任意開關通道）及中途接上案例，ASan／UBSan 通過。
  - 建置：`kernel-local-bt5-framing-1118-9g-artifact`（本機 WSL，62 秒，40 項 workflow 測試通過，ISA 解碼 2,557,640 條、禁止指令零條），核心 SHA-256 `872abc909c72bf00633835d75288e3a17d04bbc91796a7c93483ff4d64c2fdb0`。
- **BlueTool 替身（xnu 0118 位元 32／64）**：20H392 的 BlueTool 只認 Broadcom，bluetoothd 透過 `com.apple.BlueTool` 送 `RunBuiltinScript`。0118 新增位元 32（`/usr/sbin/BlueTool`）與 64（`/usr/sbin/bluetoothd`），同樣只注入原廠路徑，`test-fuxi-exec-inject.py` 檢查模式 0..127 與錯誤路徑。`FuxiBlueTool.c` 在 BlueTool 的 main 前接手該 Mach 服務並停在 `dispatch_main`：`boot`／`init` 讀 `kern.qbt_power_on`，`deepsleep` 讀 `kern.qbt_boot`，成功時回 `result=0`、`manufacturer=29` 與 `kern.qbt_firmware`。協定改寫自 QcaBluetooth `userland/BlueTool.c`。iPhoneOS16.5 SDK 沒有 xpc／IOKit 標頭，改用自行宣告並連 libSystem 重新匯出的 libxpc 符號。
  - 建置：`build-sb-shape.ps1 -Name sb-local-bluetool-r2-1118-artifact`，dylib SHA-256 `fb94dcbe50de8ad013fa698f3428687e5cc9953a4527d9358ea94a4479cdc4e7`（r1 因 `xpc/xpc.h` 不存在而失敗）。
  - loader：`prepare-wlan-w18-loader.py` 的 `--bt-native` 加開位元 32，要求核心含 `BlueTool gets`。第二趟 loader `native-bt5-native-pcm-1118-2150-loader`（`fuxi,inject=62`），指令同上方範例，改用 `--artifact kernel-local-bt5-framing-1118-9g-artifact --bt 5 --bt-native --audio-tdm-pcm --stderr-log mediaserverd,bluetoothd,BlueTool`，boot SHA-256 `47b2eb5721b974b968ecb9456460c4eb9277041eb2c02eb999172d1bf7b9a941`。
  - 第二趟判據：成功看 `fuxi: BlueTool adapter listening com.apple.BlueTool`、`BlueTool request=… script=boot error=0 result=0`、stats 的 `framed` 與 tx／rx 持續增加且 bluetoothd 沒有反覆重啟；失敗看 `BlueTool adapter off`、`listener unavailable`、BlueTool 完全沒出現，或 bluetoothd 反覆重啟。`Failed to determine ControllerChipset type` 不算失敗：它來自 `0x100099020`（`Init sniff` HCI 記錄器）呼叫 `0x10007e8b4` 時的 dispatch_once 檢查（`0x10007e954`，型別需在 1..0xf9f），只影響封包記錄。
- **第二趟實機（21:59–22:11）**：loader `native-bt5-native-pcm-hal-1118-2201-loader`（boot SHA-256 `6cc8d0835d4de34effe50347e445df411cbc500df9e2fad1974f4d5d443d1fb6`，`fuxi,inject=62`），核心 `kernel-local-bt5-framing-1118-9g-artifact`，dylib `sb-local-bluetool-r2-1118-artifact`。setup ok、`manufacturer=0x1d`，`BlueTool adapter listening com.apple.BlueTool`，`RunBuiltinScript` boot／init 都 `result=0`。bluetoothd 印出 `marconi-bt: Unable to match bluetooth service in device tree`、`Not a valid MAC address (all 00s)`，以 errx「No bluetooth on this device」反覆重啟。
  - 判斷：20H392 bluetoothd `0x1000266bc` 依序讀 `BT_DEVICE_ADDRESS`、IOService `marconi-bt`／`bluetooth` 的 IODeviceTree `local-mac-address`（`0x100027920`）、boot-args；位址全 0，或沒有 `/dev/cu.bluetooth`、`/dev/uart.bluetooth` 也沒有同名服務，就走 `0x10002702c` 結束。
  - 修正：`--bt-native` 在 ADT 根層加 `bluetooth` 節點（`compatible=fuxi,wcn7850-bt`、`device_type=bluetooth`），`local-mac-address` 取既有 iOS 身分 `/chosen mac-address-bluetooth0`，要求非 0、unicast；根層節點由平台建 nub，與 `wlan` 相同。參考 UniXNU-sheng `BluetoothProbe.cpp` 的 bluetooth 服務。位址值不寫進紀錄。
- **第三趟實機（22:17–22:23）**：loader `native-bt5-btnode-pcm-hal-1118-2222-loader`（boot SHA-256 `956b41de378dbeaed52154b0bf80bd88c892f2695b76e3d9721e7408c67af753`），核心與 dylib 同第二趟。bluetoothd 印出 `bluetooth: Got local-mac-address`，MAC／服務閘門通過；BlueTool boot／init `result=0` 後，bluetoothd 以 `Bluetooth fatal error - crashing { build=release, reason=1201 }`（`abort_with_payload` ns 18 code 2）中止並反覆重啟。UART.SK 沒有任何 `FUXI BT SK: connect` 紀錄，stats 一直是 `links=0`、`tx=0/0`。
  - 判斷：返回位址 `0x1000f3888` 在堆疊初始化 `0x1000f3434` 的共同中止點，前面是 Device Manager、傳輸層、`OI_HCIAPI_Init_Vars`、BT VSC 四步；訊息只走 `__os_log_impl`。傳輸層 `0x1000e69f0` 經 `0x100156160` 選 POSIX driver（`0x100844148`），其 open `0x100066bdc` 以 `socket(32,1,2)`、CTLIOCGINFO、connect 連 `com.apple.uart.sk.bluetooth`。launchd 快取（0025 root 映像 `/System/Library/xpc/launchd.plist`）中 `com.apple.bluetoothd` 的 UserName 是 `mobile`；核心註冊帶 `CTL_FLAG_PRIVILEGED`，kern_control 在呼叫 `bt_sk_connect` 前就回 EPERM，所以沒有 connect 紀錄，傳輸層失敗。
  - 修正：`fuxi_bt_sk.c` 註冊時不帶 `CTL_FLAG_PRIVILEGED`，在 `bt_sk_connect` 判斷權限：root、原生模式的平台 `bluetoothd`、探測模式的平台 `SpringBoard`，其餘 EPERM。
  - 同時補 ADT 屬性：bluetoothd `0x1000e7684` 讀 `transport-encoding`（4 bytes），缺少時預設 3＝H4BC（Broadcom）；`0x100156160` 的對照為 0 USB、1 H4、2 H4DS、3 H4BC、4 H5、6 APPLEBT、7 PCIe，改設 1（QcaBluetooth 板級同值）。`0x1000e74e4` 讀 `transport-speed`，低於約 115200 改用預設 2400000；它只設 UART.SK 的虛擬 termios，填實際的 115200。
  - 建置：`kernel-local-bt5-sknopriv-1118-9g-artifact`（本機 WSL，24 秒增量，ISA 解碼 2,557,627 條、禁止指令零條）；loader `native-bt5-sknopriv-pcm-hal-1118-2236-loader`（boot SHA-256 `11bb54055d4668266f105d5e6036009952d1171dbb6a6321ba4a566f1d7d0c1d`），指令同第二趟，換 `--artifact kernel-local-bt5-sknopriv-1118-9g-artifact`。
  - 第四趟判據：成功看 bluetoothd 的 `FUXI BT SK: connect unit=… pid=…`、`channel spec`、`channel rings ready`，stats 的 tx／rx 增加，且不再出現 reason 1201；失敗看仍沒有 connect 或仍以 1201 中止。
- **第四趟實機（22:37–22:43）**：證據 `work/Codex-Xiaomi13-GPU-20261109/native-hw000bt5-sknopriv-1118-2237/boot.log`。bluetoothd 連上 UART.SK（`connect unit=1 pid=85`、`channel spec … error=0`、`channel rings ready`、`termios speed=115200/115200`），不再以 1201 中止；使用者回報藍牙設定頁能進入，但開關一直轉圈。
  - 每次 bluetoothd 只送 HCI Reset（`tx opcode=0xc03`，tx 4 bytes），沒有 Command Complete；約 10 秒後送 Read Local Version（`0x1001`），接著斷線並換 pid 重連（85→314→322→327→332），共五次。
  - 核心收端：`framed=15`、`debug=6/132` 與沒有 client 的第二趟相同（都是 setup 後的第一批），bluetoothd 收到這批的後 179 bytes；之後 desync 每 10 秒穩定多約 1000 byte（989、1988、2988…8983），第二趟沒有 client 時一直是 36。也就是送出 HCI Reset 後，控制器約每 10 ms 送一個非 H4 byte。
  - 判斷：形狀符合 QCA 軟體 IBS——控制器要回事件前先送 WAKE_IND（0xfd），等主機 WAKE_ACK（0xfc），沒有回應就重送；NVM HCI tag `151122->150022` 的 data[0] 原本就沒有 bit 7，但控制器的行為不像關閉。尚未取得原始 byte，第五趟確認。
  - 第五趟準備：`fuxi_bt_pump.c` 加 IBS 應答（ADT `fuxi,bt-ibs=1`，預設關）：封包邊界收到 0xfd 回 0xfc；主機第一次送資料前先送 0xfd，等 0xfc，最多 160 步、每 16 步重送，等滿照送；控制 byte 只在 TX 封包之間寫出。另加診斷：bluetoothd 每次取 TX 後印接著 12 塊原始 RX（`FUXI BT RXRAW`，含距上次 TX 的毫秒），stats 後印 `FUXI BT RXDIAG`（desync 中 0xfc／0xfd／0xfe 計數、最近 16 個 desync byte、GENI M／S IRQ 聯集、ack／wake 次數）。參考 Linux `drivers/bluetooth/hci_qca.c` 的 HCI_IBS 狀態機。主機測試 `tools/test-fuxi-bluetooth-pump.py --sanitize` 新增 `test_ibs`（回 ack、等 ack 才送、逾時照送、關閉只計數）通過。
  - 建置：`kernel-local-bt5-ibsdiag-1118-9g-artifact`（本機 WSL，ISA 解碼 2,558,114 條、禁止指令零條）；loader `native-bt5-ibsdiag-pcm-hal-1118-2251-loader`（boot SHA-256 `c4194e006fce804589572008f426fb29b900f59ddf5845f13b5a533485ec8282`），指令同第四趟，加 `--bt-ibs`。
- **第五趟實機（22:51–22:58）**：loader `native-bt5-ibsdiag-pcm-hal-1118-2251-loader`，核心 `kernel-local-bt5-ibsdiag-1118-9g-artifact`（本機編）；證據 `work/Codex-Xiaomi13-GPU-20261109/native-hw000bt5-ibsdiag-1118-2251/boot.log`（7064 行，SHA-256 `3153faea678bf22a94c5d4316d382c3f067c674992505457e5030c111395b9b6`）。`resident ready baud=115200 ibs=1`；bluetoothd 送 HCI Reset 後 RXRAW 依序是 `fc`、`fd`、`040e0401030c00`，`rx opcode=0xc03 status=0`。IBS 判斷成立：最後一筆 RXDIAG `ibs=1/12/13 acks=12 wakes=1 wake_timeouts=0`，desync 只剩 62 byte，都是 IBS byte。
  - HCI 初始化：bluetoothd 依序送 0x0c03、0x1001、0x1002、0x1009、0x1003、0x1004、0x2003、0x0c01、0x0c63、0x2001、0x0c56、0x0c6d、0x2060、0x202f、0x2031、0x2024、0x201c、0x200f、0x1005、0x204a、0x2074（兩次）、0x207d、0x0c33、0x0c31，全部 status 0。接著 0x0c5b（Write Default Erroneous Data Reporting）回 status 1（Unknown HCI Command），bluetoothd 隨即斷開 UART.SK、由 launchd 重啟，stderr 沒有錯誤訊息。整趟 connect／disconnect／exec 各 13 次；H4 追蹤上限 200 筆，看得到的 3 次都停在 0x0c5b。使用者回報藍牙設定頁進不去。
  - 對照：QcaBluetooth `native-ios27.md` 也把 0x0c5b 當可選命令（依 Supported Commands byte 18 bit 3），host flow control 與 erroneous data reporting 被拒時繼續初始化；那邊改的是 bluetoothd 本身。這裡 0x0c33／0x0c31 已成功，只需處理 0x0c5b。
- 第六趟準備：`fuxi_bt_pump.c` 加可選命令開關（ADT `fuxi,bt-optional`，u32 opcode 陣列，最多 8 個，預設空）。控制器對清單內 opcode 回 Command Complete 且 status 非 0 時，交給 UART.SK 之前就地改成 0。清單只放回傳只有 status 的寫入命令。封包跨塊時先記 opcode；Command Status、其他 opcode 與成功回應不動；每次改寫印 `FUXI BT OPTIONAL: opcode=… status=… -> 0 count=…`。主機測試 `test_optional` 通過（RX 切塊 0–9 × 交付上限 1–3 與不限；清單空時原樣交出）。`prepare-wlan-w18-loader.py --bt-optional 0x0c5b` 需要 `--bt 5`，拒絕 vendor opcode（OGF 0x3f）與重複值。
  - 建置：`kernel-local-bt5-optional-1118-9g-artifact`（本機 WSL，ISA 解碼 2,558,356 條、禁止指令零條）；RTC 校時 `work/adsp-battery/rtc-calibration-bt5opt-1118-2302.json`；loader `native-bt5-optional-pcm-hal-1118-2303-loader`（boot SHA-256 `66396e23dd38ce11ff1d32edee307268c8918465d8026b17ed6e6746c3a9ed2b`，payload `450cf2b48abdca8ad300643834d4d5c923dc05879162af5feee88696557b82cd`），其餘選項同第五趟，另加 `--bt-optional 0x0c5b`。
  - 第六趟判據：成功看 `resident ready … optional=1`、`FUXI BT OPTIONAL: opcode=0xc5b status=0x1 -> 0`，之後 bluetoothd 繼續送命令，且沒有立即 disconnect；失敗看另一個命令 status 非 0 後斷開，或沒有失敗也斷開。
- **第六趟實機（23:04–約 23:12）**：證據 `work/Codex-Xiaomi13-GPU-20261109/native-hw000bt5-optional-1118-2304/boot.log`。`resident ready baud=115200 ibs=1 optional=1`，`FUXI BT OPTIONAL: opcode=0xc5b status=0x1 -> 0 count=1`；之後 bluetoothd 繼續送標準命令 0x0c7a、0x080f、0x0c16、0x0c18、0x0c1c、0x0c1e、0x0c26、0x0c2a、0x0c2c、0x0c2f、0x0c43、0x0c45、0x0c47、0x0c49、0x0c13、0x0c52、0x0c24，全部 status 0。使用者回報藍牙設定頁可以進入，開關仍一直轉圈。
  - 接著是 vendor 命令：0xfd3c（`aci_vsc.c` 的 `_ACI_SetMultipleAdvInstanceParams`）、0xfce9（`bcm_vsc.c`，回應帶 sub-opcode）、0xfce2（兩次 5 byte、最後一次 10 byte）、0xfd48、0xfd47 都回 status 1；0x2007（LE Read Advertising Physical Channel Tx Power）回 status 12（Command Disallowed）。第一次連線 57 個命令共 864 byte，與統計 `tx=864/57` 相符，所以追蹤沒有漏命令：最後一個 0xfce2 失敗後 bluetoothd 不再送任何東西，約 10 秒後斷線重啟。之後每次連線都多 857 byte／56 次同步，行為相同。
  - vendor 命令不在 bluetoothd 的標準命令描述表（`0x1008448e0`，253 筆，24 byte 一筆；0x0c5b 在第 124 筆，bits 0x1208）。QcaBluetooth 的做法是在 bluetoothd 裡把控制器類型設為 0、清掉 vendor 操作表；這裡先不改 bluetoothd。
- 第七趟準備：`fuxi,bt-optional` 加萬用值 0xfc00（所有 OGF 0x3f 命令的 Command Complete 失敗都改成 0），核心 `resident ready` 多印 `optional_vendor=`，loader 用它判斷核心是否支援；主機測試補 vendor 萬用案例。H4 追蹤改成每次 UART.SK 連線重新計數（上限 400），TXRAW／RXRAW 配額 80／160；每 100 ms 檢查連線，環收掉時印 `FUXI BT: link N down pending=… recent=… slowest=…` 與 RXDIAG，命令 2 秒沒有回應印 `FUXI BT H4: no reply opcode=…`；沒有 opcode 的事件（排除已完成封包數與廣播報告）也印出。
- **os_log 趟 1（11-19 10:23）**：loader `native-bt5-oslog-pcm-hal-1119-1021-loader`（boot SHA-256 `d4d3ade16916ea97ec122546b09161645709f8c3675e22abc695f047f0f33344`，`--bt-oslog 3`），核心 `kernel-local-bt5-vendoropt-1118-9g-artifact`（本機編），dylib `sb-local-btoslog-1119-artifact`。bluetoothd 被 `libfuxi-sb.dylib` 的 os_log 轉錄（U-022）弄成 SIGSEGV：格式器把 clang os_log 緩衝區的 mask item（kind 7，`%{public,mask.hash}`）當成參數取走，後面的參數錯位。證據 `work/Codex-Xiaomi13-GPU-20261109/native-hw000bt5-oslog-1119-1023/boot.log`（48614 行，SHA-256 `f9814df5adec226688c0954b6dfe75e48064407f5efe7cdb2996bc1f9bac6e41`）。
- **os_log 趟 2（11-19 10:34）**：同 loader 與核心，dylib 換 `sb-local-btoslog-r2-1119-artifact`（SHA-256 `e5793544ce690d264f82a0335c73ef9f151051308df1e10e701bab52552e12d6`，略過 mask item）；沒有 bluetoothd crash。證據 `native-hw000bt5-oslog2-1119-1034/boot.log`（27121 行，SHA-256 `a24a0bc3227b3a781b98d22fe95a77016045484cfe0cf3d5741f412086bd6f29`）。
  - 轉錄出的流程：productType iPhone10,3 選 `BCM_4357` 的 VSC 表；BlueTool boot／init result=0；StackManager 初始化完成（0x0c5b 照舊 1→0），`STACK_BUSY`→`STACK_READY`、`Stack Did Start`。vendor 命令 0xfce9 LE_META_ENA_FEATURES、0xfce2 CHANGE_LE_SETTINGS、0xfce2 SET_MIN_AFH_LE_CHANNELS、0xfd47 都由泵改成 status 0 並 `completed with status 0`；0xfd48 保留錯誤、0x2007 回 12，兩者都沒有造成重啟。
  - 中止點：`BD_VSC_OLYMPIC_ANTENNA_PREF_B`（0xfce2，參數 `15 00 00 00 ff 0f 02 00 00 00`，子命令 0x15）改成功後印 `Invalid data size 0 (status=65535)`，15 秒後 `timedout waiting for controller`、VSCommandStall ErrorCode 140（VSC 名稱表 `0x100804a20` 第 0x8c 筆），restart reason=3602，`_exit status=1`，核心 `link 1 down pending=0 opcode=0xfce2`。
  - 判斷（20H392 arm64 反組譯）：`_BCM_OlympicAntennaPreference`（`0x1001287fc`）寫子命令 0x15、3 個 0 與 6 個參數 byte，經 `0x100166b80` 送 OCF 0xe2，解析器是 `_BCM_ByteStreamEventParser`（`0x100126134`）。它在 status 之後剩餘 bytes ≤ 1 時只記錄「Invalid data size」，不呼叫完成回呼 `0x1002682b0`（後者忽略資料、轉呼叫通用完成 `0x1003cce8c(status, 0x8c)`），所以等滿 15 秒判 stall。只回 status 的簡單解析器（`0x100123514`）不看資料，前幾個 vendor 命令因此能過。
- **回應補資料（11-19 10:45）**：`fuxi_bt_pump.c` 加 ADT `fuxi,bt-pad`（u32 opcode 陣列，最多 8 個，接受 0xfc00 萬用，預設空）。TX 追蹤器記下清單內命令的第一個參數 byte（最近 4 筆）；RX 端只有 status 的 Command Complete（payload 4）整包先收進 framer 的 `hold[]`，照 `fuxi,bt-optional` 改 status，再補「子命令回聲＋0」兩個 byte、事件長度 4→6 後交出。`resident ready` 多印 `pad=`，每次補資料印 `FUXI BT PAD: opcode=… subop=… count=…`；loader 產生器加 `--bt-pad`，以 `fuxi,bt-pad` 字串判斷核心是否支援。
  - 主機測試：`test-fuxi-bluetooth-pump.py` 加 `test_pad`（0..9 byte 切塊 × 4 種交付上限、ACL 穿插、帶參數的回應不補、Command Status 不動、清單空原樣交出、上層收不下時 hold 留在泵內），ASan／UBSan 通過；`sync-fuxi-adsp.py --check`、`test-fuxi-bluetooth-xnu.py`、`test-fuxi-bt-geni-fifo.py` 通過。
  - 建置：`kernel-local-bt5-pad-1119-9g-artifact`（本機 WSL，24 秒增量，ISA 解碼 2,561,462 條、禁止指令零條），核心 SHA-256 `131967abaace6542bbda382cb9c3a46b4fb50ad9c58712c61fd76c05925fb1f9`。harness 補丁只換藍牙 5 個檔的段落（`fuxi_bt_pump.{c,h}`、`fuxi_bt_xnu.c`、`test-fuxi-bluetooth-pump.py`、0117），不帶入 ksrc 工作目錄裡尚未完成的 FuxiAudio 修改。
  - loader：RTC 校時 `work/adsp-battery/rtc-calibration-rtc-btpad-1119-1051.json`；`native-bt5-pad-1119-1051-loader`（boot SHA-256 `ed17041d05e557ce338a13cdc5a7326d5c3873e1de2109825703b05a010a2807`），指令同 os_log 趟，換 `--artifact kernel-local-bt5-pad-1119-9g-artifact` 並加 `--bt-pad 0xfc00`。
  - 判據：成功看 `resident ready … pad=1`、`FUXI BT PAD: opcode=0xfce2 subop=0x15`，之後沒有 `Invalid data size`、VSC stall 與 reason=3602；失敗看另一個 VSC 仍 stall，或補資料後解析器報其他錯。
- **os_log 趟 3（11-19 10:52）**：loader `native-bt5-pad-1119-1051-loader`，核心 `kernel-local-bt5-pad-1119-9g-artifact`（本機編），dylib 同趟 2。證據 `work/Codex-Xiaomi13-GPU-20261109/native-hw000bt5-pad-1119-1052/boot.log`（55842 行，SHA-256 `3794a32b26652b2120e84db38acf61ed6f7f77e0b7c03f7c460091eabc038b8e`）。`FUXI BT PAD:` 529 筆、`Invalid data size` 0 筆，ANTENNA_PREF_B 的 VSC stall 與 reason 3602 消失，`Stack Did Start` 第一次出現在第 10977 行。使用者回報設定頁能進入，開關仍轉圈。
  - 新中止點：每個 bluetoothd 在 `Stack Did Start` 後約 1 秒以 `Bluetooth error - restarting { reason=712 }` 重啟（第一次第 11989 行）。同趟 0x2007（LE Read Advertising Physical Channel Tx Power）與 0x200b（LE Set Scan Parameters）各 14 次回 status 12（Command Disallowed），LE 掃描無法開始。
  - 判斷：Core spec Vol 4 Part E 3.1.1 規定主機用過延伸廣播命令後，控制器對 legacy 廣播／掃描命令回 Command Disallowed。20H392 bluetoothd 的初始化鏈（送出 `0x10010b590` → 回呼 `0x10010c5d8` → `0x10008fbe4` 記錄 `LE_SetPeriodicAdvListSize`）在 LE 功能 bit 13（Periodic Advertising，結構 `0x1008638c0+0x24`）存在時送 0x204a（LE Read Periodic Advertiser List Size，第 10588 行），之後 QCA 就拒絕 legacy 0x2007／0x200b。0x2003 回傳功能 byte 1 的 0x30 是 bit 12（Extended Advertising）與 bit 13，清掉後 bluetoothd 只走 legacy 路徑。
  - 另一個問題：前兩個 bluetoothd 的 HCI Reset 沒有回應（第 8655 行 `no reply opcode=0xc03`）。第 6400–6850 行顯示常駐開始時 UART 殘留的 byte 被當成 Command（類型 0x01），接著是 event code 0、長度 96 的假事件；後面的 IBS WAKE_IND（0xfd）被吃進假事件 payload，泵沒有回 WAKE_ACK，控制器也就不送 Reset 的回應。
- **os_log 趟 4 準備（11-19 11:20）**：
  - `fuxi,bt-mask`：u32 陣列，每條 `opcode<<16 | offset<<8 | clear`，最多 8 條，預設空。清單內 opcode 的 Command Complete 回傳參數第 offset byte 清掉 clear 位元後才交給 UART.SK，可與 `fuxi,bt-optional`、`fuxi,bt-pad` 同時開。`resident ready` 多印 `mask=`，每條規則印 `FUXI BT: mask opcode=… offset=… clear=…`，每次改寫印 `FUXI BT MASK: opcode=… offset=… 前 -> 後 count=…`。loader 產生器加 `--bt-mask OPCODE:OFFSET:CLEAR[,…]`，需要 `--bt 5`，以 `fuxi,bt-mask` 字串判斷核心是否支援，拒絕 0 值與重複規則。
  - RX 對齊：分框器與 RX 追蹤器不再把類型 0x01 和 event code 0 當成封包開頭。這些 byte 算 desync，後面的 byte 重新掃描，IBS 照常應答；header 跨塊時，本塊內的 header bytes 退回重掃。新的 UART.SK 連線出現時，常駐迴圈呼叫 `fuxi_bt_pump_resync_host`，丟掉上一個連線的半個封包與泵內未交出的 RX，並印 `FUXI BT: link N resync dropped=… partial=…`。RXDIAG 多印 `invalid=` 與 `resyncs=`。`fuxi_bt_sk_connects()` 以無鎖方式讀連線次數。
  - 主機測試：`test-fuxi-bluetooth-pump.py --sanitize` 新增三項：
    - `test_mask`：切塊 0–9 × 4 種交付上限 × optional 開關；清單空時原樣交出。
    - `test_rx_garbage`：殘留 `01 da 01 ff 04 00 60 fd` 之後的 Command Complete 完整交出，WAKE_ACK 恰好一次，切塊 0–9。
    - `test_rx_resync`：涵蓋上層收不下、封包交到一半、在封包邊界三種情形。
  - 隨機分框不再產生 event code 0；追蹤器測試的命令改由 TX 方向驗。以上與 `sync-fuxi-adsp.py --check`、`test-fuxi-bluetooth-xnu.py`、`test-fuxi-bt-geni-fifo.py` 都通過。
  - 建置：harness 補丁 SHA-256 `abf07f2e4673f642c0c584d309b17f3dc07beeb086a682ee8ef167adb277baec`。只換藍牙段落：`fuxi_bt_{pump.c,pump.h,sk.c,sk.h,xnu.c}`、`test-fuxi-bluetooth-{pump,xnu}.py`、0117，0117 內層差異也只有這 5 個 bsd/dev 檔。核心 `kernel-local-bt5-resync-1119-9g-artifact`（本機 WSL，23 秒增量，ISA 解碼 2,561,769 條、禁止指令零條），SHA-256 `e42bbb8fa20329704059749a8f9d0a7387de86a2b522db8cac7c635ca88627c1`。
  - loader：RTC 校時 `work/adsp-battery/rtc-calibration-rtc-btresync-1119-1124.json`；`native-bt5-resync-1119-1124-loader`（boot SHA-256 `13dab41990a599ab543c0a3d549aa44841e63c934cc3a002e32f81de5ff6efde`），指令同趟 3，換 `--artifact kernel-local-bt5-resync-1119-9g-artifact` 並加 `--bt-mask 0x2003:2:0x30`。
  - 判據：
    - 成功：`resident ready … mask=1`、`FUXI BT MASK: opcode=0x2003 offset=2`、沒有 tx 0x204a、0x200b `status=0`、沒有 reason=712，且第一個 bluetoothd 的 HCI Reset 就有回應。
    - 失敗：`status=12`、`restarting`、`_exit`，或 `no reply opcode=0xc03`。
- **os_log 趟 4（11-19 11:30–11:40）**：loader `native-bt5-resync-1119-1124-loader`，核心 `kernel-local-bt5-resync-1119-9g-artifact`（本機編），dylib 同趟 2。證據 `work/Codex-Xiaomi13-GPU-20261109/native-hw000bt5-resync-1119-1130/boot.log`（64494 行，SHA-256 `cd8c9a0dfea0903adbefb9170a5e9c3c54825e0f875dd01c8f7562b74085d952`）。11:26 第一次啟動把 `-Loader`／`-Kernel` 給成完整路徑，fastboot 回 `cannot load`、手機停在 fastboot，沒有進 iOS；手動 `fastboot reboot` 回 Android 後由 launcher 自行釋鎖，改用資料夾名稱重跑。殘留的 `native-hw000bt5-resync-1119-1125-monitor.log` 只有 monitor traceback。
  - 趟 4 目標都達成：`resident ready … optional=5 pad=1 mask=1`；每次連線都印 `FUXI BT MASK: opcode=0x2003 offset=2 0xf9 -> 0xc9`（17 次）；沒有送 0x204a；0x2007、0x200b 各 17 次 status 0，沒有 status 12，也沒有 reason 712。18 次 UART.SK connect 都印 `link N resync`，第一個是 `dropped=0 partial=1`。`no reply opcode=0xc03` 為 0，第一個 bluetoothd 就在第 7243 行 `Stack Did Start`，共 17 次。
  - 新中止點：bluetoothd 依序送 `BT_VSC_OLYMPIC_SET_RETAIN_LE_SCAN_DUPLICATES_ON_START`（0xfce2）、0x200b（被動掃描，interval 0x01e0、window 0x0030、Own_Address_Type 1、filter 0）、0x200c（enable 1、filter_dup 1）。0x200c 17 次都回 status 18（0x12，Invalid HCI Command Parameters），接著 `Reset scan in LE_Scan: STATUS 718 for location 3`、`Scan stopped improperly with result 718 -- restart`、`reason=718`、`_exit status=1`。整趟 17 次重啟都是 718。
  - 判斷：整趟沒有送過 LE Set Random Address（0x2005），自身隨機位址從未設定。Core spec Vol 4 Part E 7.8.11 規定這種情況下 0x200c 要回 0x12。bluetoothd 印出 `This platform supports address rotation while scanning or connecting`，也從 keychain 讀了「Local Device Static Random Address」；VSC 名稱裡沒有設位址的命令，推定 Broadcom 控制器自行產生、輪替隨機位址，QCA 不會。QcaBluetooth `native-ios27.md` 的「Scanning and addresses」記載同一個 0x12，它在 bluetoothd 內先排 0x2005。
- **os_log 趟 5 準備（11-19 11:45）**：
  - `fuxi,bt-txmask`：格式同 `fuxi,bt-mask`（`opcode<<16 | offset<<8 | clear`，最多 8 條，預設空），offset 從第一個命令參數起算（0 起）。`fuxi_bt_pump_txmask` 在從 UART.SK 取到的 TX 塊寫進 FIFO 前，用獨立的 H4 命令邊界走訪狀態找出參數 byte，就地清位元。每個 byte 只在取到時經過一次，FIFO AGAIN 重寫不重算；ACL 等非命令封包整段略過。recover 時走訪狀態歸零，TX 追蹤器看到的是改寫後的值。`resident ready` 多印 `txmask=`，每條規則印 `FUXI BT: txmask opcode=… offset=… clear=…`。改寫時印 `FUXI BT TXMASK: opcode=… offset=… 前 -> 後 count=…`，前 32 次逐筆印，之後只在次數是 2 的冪時印。
  - 規則：`0x200b:5:1`、`0x200d:12:1`、`0x2006:5:1`、`0x2041:0:1`、`0x2043:1:1`，分別是掃描參數、建立連線、廣播參數、延伸掃描參數、延伸建立連線的 Own_Address_Type。清 bit 0 讓 1→0（公開位址）、3→2（RPA，退回公開位址）。代價是主動掃描與連線改用公開位址、隱私降低；被動掃描不發射，不受影響。另一條路是照 QcaBluetooth 先送 0x2005，需要泵自行注入命令並吃掉它的 Command Complete，先不採用。
  - loader 產生器加 `--bt-txmask OPCODE:OFFSET:CLEAR[,…]`：需要 `--bt 5`，以 `fuxi,bt-txmask` 字串判斷核心是否支援，offset 接受 0。
  - 主機測試：`test-fuxi-bluetooth-pump.py --sanitize` 新增 `test_txmask`。0x200b 參數 5 由 1 改 0；0x200d 參數 12 由 1 改 0，同一命令參數 5 的 Peer_Address_Type 不動；0x2041 參數 0 由 3 改 2。ACL payload 裡長得像 0x200b 的 bytes、沒有規則的 0x200c、已經是 0 的位址類型都不動。take 切塊 0–9 × FIFO 隨機 AGAIN／budget 3 結果相同，追蹤器看到改寫後的值。另驗清單空時原樣送出，命令送到一半時 recover 後下一個命令照常改寫。與 `sync-fuxi-adsp.py --check`、`test-fuxi-bluetooth-xnu.py`、`test-fuxi-bt-geni-fifo.py` 都通過。
  - 建置：harness 補丁 SHA-256 `213397668918f9b1a14a9282d96bd9662f82e15428d81718d795eb0c403b2020`，只換藍牙段落：`fuxi_bt_{pump.c,pump.h,xnu.c}`、`test-fuxi-bluetooth-pump.py`、0117，0117 內層差異只有對應的 3 個 bsd/dev 檔。核心 `kernel-local-bt5-txmask-1119-9g-artifact`（本機 WSL，24 秒增量，ISA 解碼 2,562,081 條、禁止指令零條），SHA-256 `5faf531465be774d97926103c0353cb4e2550130eac52f810fd97f733b2c8e47`。
  - loader：RTC 校時 `work/adsp-battery/rtc-calibration-rtc-bttxmask-1119-1150.json`；`native-bt5-txmask-1119-1150-loader`（boot SHA-256 `c89030f599d50d00cbad5c63a1b2cbc5792f3bc451bea0b7694575b7e6d827ff`，payload `e5a74bd99f1e6fed09ab662dd93b08c91f9b36d51dcf2f3ec1b9d31c36034842`），指令同趟 4，換 `--artifact kernel-local-bt5-txmask-1119-9g-artifact` 並加 `--bt-txmask 0x200b:5:1,0x200d:12:1,0x2006:5:1,0x2041:0:1,0x2043:1:1`。
  - 判據：
    - 成功：`resident ready … txmask=5`、`FUXI BT TXMASK: opcode=0x200b offset=5 0x1 -> 0`、0x200c `status=0`、沒有 reason 718，bluetoothd 不再重啟（connect 次數停在 1）。
    - 失敗：0x200c 仍 `status=18`，或出現其他 `reason=`、`_exit`。
- **os_log 趟 5（11-19 上午）**：證據 `work/Codex-Xiaomi13-GPU-20261109/native-hw000bt5-txmask-1119-1152/boot.log`，SHA-256 `617051906913cf11457fff5029a4eea64c498f4da741dd9560e36acb353c2af9`。第 6527 行連上 UART.SK，第 7080 行原生服務記下 `Stack Did Start`，全段各一次。0x200c 的 10 筆 Command Complete 全部 status 0；同一段擷取中 reason 718 與 `_exit` 均為零。32 筆客戶端通知進到 `PowerState:1 managerState:5`。第 9158 行原生服務收到成功的對外 LE 連線完成事件，第 9593 行報告 rapportd 的 BLE 連線就緒。
- **os_log 趟 6（11-19 14:16–14:22）**：沿用趟 5 的核心與手機既有 dylib，以新 RTC 重包，加入 `--quiet-screen`。測試使用 `-Seconds 105 -Reset 4 -FinalFramebuffer -FramebufferHeight 1200`。原生服務到 On，SpringBoard 回報啟動 Preferences；本趟的 `framebuffer.png` 顯示鎖定畫面。證據 `work/Codex-Xiaomi13-GPU-20261109/native-hw000bt5-ui-location-1119/`。launcher 14:22:05 確認 Android 返回並釋鎖。
- **os_log 趟 7（11-19 14:22 開始）**：K-035 的上滑手勢進入設定，舊首頁座標 (148,570) 此時落在登入列。以 `-FramebufferHeight 1450` 擷取到 Apple ID 登入頁，未輸入帳戶資料。第一個 bluetoothd 的 HCI Reset 沒有收到回覆，reason 108 退出一次；第二個成功初始化、送出 On。這證實冷啟動仍偶有初始化重試。證據 `work/Codex-Xiaomi13-GPU-20261109/native-hw000bt5-ui-settings-1119/`。
- 趟 7 在 Android 回來後，從 `work/adsp-battery/fuxilog/20261119-142745/console.txt` 補看 USB 擷取結束後的紀錄。整趟有 6 次 UART.SK 連線、5 次斷線。初次初始化逾時以外，還有 4 次在電源統計資料長度檢查中止（SIGABRT）；都發生在控制器的「不支援」回覆被相容層改報成功後。USB 擷取段不足以判定整趟服務穩定。
- **os_log 趟 8（11-19 14:27 開始）**：保留同一核心、函式庫與趟 5 的藍牙配置。從鎖定頁上滑，在設定首頁將列表捲到最上，點選 (260,1200) 的藍牙列。實際擷取的首頁顯示「藍牙　開啟」，詳情頁右側為綠色開關，下方「裝置」列顯示搜尋進度。證據 `work/Codex-Xiaomi13-GPU-20261109/native-hw000bt5-ui-row-1119/framebuffer-shot5-step.png`。USB 紀錄有一條 UART.SK 連線，0x200c 十一筆成功回覆。
- **os_log 趟 9**：同核心，將 0xfce2 從 `--bt-optional` 清單移除，使晶片能回報不支援；其餘相容選項同趟 8。綠色開關點擊後變灰、背景變暗；第二次點擊時仍處於該畫面。裁切高度 640，未記錄中央提示文字或完成關→開。整趟 mtd 有一次初始 reason 108，此後看到 BLE Ready，沒有電源統計斷言。
- **os_log 趟 10，Windows 對端（11-19 14:45）**：沿用趟 9 的藍牙配置。Windows 針對手機實體控制器做 uncached SDP 查詢，回報 `SUCCESS`、5 項 RFCOMM 服務、`CONNECTED`。這段查詢落在 iOS 實機監控的時間範圍內。Windows 在 7 秒觀察中沒看到該公開位址的 BLE 廣播，GATT 查詢為 `device_unavailable`。同趟 iOS 的 rapportd 另有 BLE Ready，對端身分未核對。全程未呼叫配對 API。
- 11-19 初次 HCI Reset 對照。趟 9、10 的第一個連線都收到十個獨立的 UART `0xfc`（IBS WAKE_ACK），但關閉第一個連線時 RXDIAG 的已解析 ACK 為 0，另有十次喚醒與一次逾時。趟 11 首次 Reset 即完成，RXDIAG 已解析一筆 ACK、無喚醒逾時。這將問題縮小到 UART 原始資料與 H4／IBS 接收狀態的交界；兩端尚需同時序紀錄。持久 console 在 `work/adsp-battery/fuxilog/20261119-144235`、`20261119-145000`、`20261119-150433`。
- **Windows 測法**：工作區 launcher 的 Boot 模式加 `-PeerTestPython <Windows Python 路徑> -PeerIdentityRuns native-hw000bt-early-exit-1119-e,native-hw000bt-fce2-native-status-1119-f`。Python 環境需有 Bleak 與 PyWinRT 的 Bluetooth、Rfcomm 投影。工具在兩份獨立手機紀錄中核對完整 Read BD_ADDR 回覆，只在記憶體保留位址。開機參數須含 K-035 的觸控腳本，測試等該腳本完成才開始。證據 `work/Codex-Xiaomi13-GPU-20261109/native-hw000bt5-ui-pc-1119/windows-peer.json`、同目錄截圖與 `thermal.jsonl`。
- 11-19 15:14 開關循環。趟 12 點擊後的 1640 列截圖辨識到 iOS 原生關閉確認視窗。趟 13 按右側「關閉」，bluetoothd 發送電源 0；再點擊開關後發送電源 1。啟動時為 1，三次事件在同份 boot.log 第 7254、12876、15448 行。關閉時截圖混有 console，重新開啟的截圖可見綠色開關。`work/Codex-Xiaomi13-GPU-20261109/native-hw000bt5-off-marker-1119/framebuffer-shot6-step.png`、`native-hw000bt5-confirm-cycle-1119/framebuffer-shot9-step.png`。
- 趟 14 的混合測試未重現關→開。PCM 震動配置沒有這條自測的 Preferences 啟動紀錄；截圖停在世界時鐘。Windows 傳統藍牙查詢回應 SUCCESS，但服務數 0、DISCONNECTED，因此未建立連線。證據 `native-hw000bt5-cycle-av-otp-1119/windows-peer.json` 與同目錄 shot7。原生服務查詢是否成功須同時看服務數、Windows 連線狀態與手機端時序。為循環測試加 `-PeerAfterPowerCycle`，只在 iOS 電源事件依序為 1、0、1 且觸控腳本結束後開始搜尋。逾時從監控啟動才計，Android 降溫另計。已用趟 10、13、14 的 boot.log 及缺標記負測試重播。
- 趟 15 於 15:31 同一趟驗成關→開後電腦重新連線。`native-bt5-cycle-pc-check-1119-loader` 使用趟 13 的本機 bt5-txmask 9g 核心、75 秒後的設定頁手勢與 `-Seconds 202 -Reset 5 -FramebufferHeight 640`，Windows 端沿用趟 10 的兩個控制器身分來源並加 `-PeerAfterPowerCycle`。手機 `boot.log` 第 7343、12603、15177 行分別送出 1、0、1，Windows 的即時 SDP 回報 5 項服務、CONNECTED。查詢起訖均在 iOS 實機監控期間；同趟 Windows GATT 為 device_unavailable。證據 `work/Codex-Xiaomi13-GPU-20261109/native-hw000bt5-cycle-pc-check-1119/windows-peer.json` 與同目錄 thermal.jsonl。
- 趟 16 於 11-19 20:17–20:23 改用最新的本機 `kernel-local-amp-tone-1119-9g-artifact`（含 mode 7，本趟 `fuxi,amp=0` 功放不動），核心 SHA-256 `c878ca440283407393bad7b69454100f876f4e76980436d663aefe827d5a8e77`。loader `native-bt5-a2dp-src-1119-loader`（boot SHA-256 `c6e0a32612983fefe9ae5ada54576e3e88d2302e09c27730190a77c12748fef4`）以趟 15 的完整產生器參數重包，34 個 `fuxi,*` ADT 屬性與趟 15 相同，只差 RTC 校時 `work/adsp-battery/rtc-calibration-bt-a2dp-1119.json`。launcher 同趟 15 再加 `-PeerAudioSource`。手機 `boot.log` 第 7289、11420、13963 行依序送出電源 1、0、1，`Stack Did Start` 兩次（開機與重新開啟），reason 718／108 為零。Windows uncached SDP 讀回 5 項 RFCOMM 服務（Wireless iAP、HFP AG 0x111F、PBAP 0x112F、MAP 0x1132 及一項自訂 UUID），CONNECTED、未配對；WinSock 依類別 0x110A 查到 1 筆 A2DP Audio Source 紀錄（137 bytes）。GATT 仍為 device_unavailable，觀察期間未見 BLE 廣播。boot.log SHA-256 `ba2447f38bedd48fc5357dbd994de55016796c77f6654d5574746fb7f7893942`。證據 `work/Codex-Xiaomi13-GPU-20261109/native-hw000bt5-a2dp-src-1119/windows-peer.json` 與同目錄 boot.log。
- 趟 17 於 11-19 20:29–20:37 由使用者手動測耳機。loader `native-bt5-headset-user-1119-loader`（boot SHA-256 `afff67da3c9e08846b2348da0cd92a3ad6788cfd0e53ab18bb1e367147f2c416`）沿用趟 16 的產生器參數，去掉 tap 腳本，加 `--quiet-screen --display-gamut sRGB`；核心同趟 16。launcher `-Seconds 900 -Reset 20`。使用者 20:37 長按電源手動回 Android，launcher 偵測到 Android 已開機，跳過 reset，20:37:47 釋鎖。
  - 使用者回報：剛開機時藍牙開關轉圈一陣子才能打開；配對後開關一直轉圈。
  - boot.log：Apple 耳機（遠端 LMP 版本 0x0E、製造商 0x004C）ACL 連線 status 0（第 12414 行），SDP 判定支援 Handsfree／A2DP；隨後 bluetoothd（pid 85）SIGSEGV，far 0（第 12761 行）。
  - 閃退解析：用本機唯讀取出的 20H392 bluetoothd（`work/statusbar-fix/inspect-radio-assets-20261116-1436/files/usr/sbin/bluetoothd`）反組譯。lr `0x1003effb8` 在 `0x1003efef4`（AudioSkywalkPipe 啟動，建立 kqueue 後呼叫 `os_channel_get_fd([x19+0xa8])`），通道是 NULL。上層 `0x100308ebc` 印「Configuring A2DP audio device … MTU=%u, RTP_len=%u」，再往上是 `0x10022c248`。開機時第 1599 行「Creating nexus with NEXUS_ATTR_SLOT_BUF_SIZE: 2048 …」，緊接第 1600 行 `SWEET Sandbox: bluetoothd privilege 12000 denied`。12000 是 `PRIV_SKYWALK_REGISTER_USER_PIPE`（XNU `bsd/sys/priv.h`）：user pipe nexus 沒有註冊成功，A2DP 一設定音訊就閃退。bluetoothd 的簽章帶有 `com.apple.private.skywalk.register-user-pipe`，S-107 的 `fuxiprivs=1`（loader `--privs`）正好依這個 entitlement 放行，待驗；000 線還沒有 `fuxiprivs=1` 的上機紀錄。
  - 閃退之後：新的 bluetoothd（pid 326）連上（第 13084 行，`link 2 resync dropped=0 partial=1`）。HCI Reset 一直沒有回應，`unknown packet type received 253` 出現 639 次，開關轉圈到重開。研判 RX 分框器重新對齊後鎖進一個不存在的長封包：之後每 10 秒交出約 1005 bytes，但 desync 計數不變，RX 追蹤器也沒有完成任何封包。IBS 只在封包邊界處理，所以控制器的 WAKE_IND（0xfd）被當成資料交給 bluetoothd，主機沒有回 WAKE_ACK，控制器的回應就送不出來。修正方向（核心，預設關的 ADT 開關）：header 檢查 ACL handle 與長度上限（對照 Linux `h4_recv_buf` 的 maxlen），以及封包中途閒置逾時就回到封包邊界。
  - 頻寬限制：UART 固定 115200 baud（`fuxi_bt_setup.c` 把 NVM tag 17 的 baud 設 0，SE 時脈 7.3728 MHz），約 11.5 KB/s。A2DP 常見的 SBC／AAC 位元率是 128–345 kbps（16–43 KB/s），超過 UART 容量。就算 Skywalk 修好，要讓耳機出聲還得先拉高 baud，可照 Linux `qca_set_speed`：設 NVM baud、送 EDL 0xfc48，並重設 GENI 時脈。開機時開關轉圈也是同一個限制：K-076 在 115200 下載 rampatch／NVM 約需 22 秒，之後才做 HCI 初始化。
  - boot.log SHA-256 `3c51ebb73cccd91f60cd2e3a57721783819c6255453e55391b5a6568d5dc6eba`（25654 行），證據 `work/Codex-Xiaomi13-GPU-20261109/native-hw000bt5-headset-user-1119/`。
- 趟 18 於 11-19 20:53–21:00 驗 S-107 的 `fuxiprivs=1`。loader `native-bt5-privs-1119-loader`（boot SHA-256 `cb1f552efbb8732f29c88517c3ac6e83c88606d9ef672593d31cd1de6820a259`）以趟 16 的完整產生器參數加 `--privs` 重包，命令列 532 bytes；核心同趟 16。launcher 同趟 16，20:54:04 開機，21:00:04 回 Android 並釋鎖。
  - 權限：bluetoothd 兩個實例（開機的 pid 85 與閃退後的 pid 357）建立 nexus 後都沒有 `privilege 12000 denied`（第 1672、15264 行；趟 16 第 1525 行在同一處被拒）。放行紀錄每種特權全域只印第一次（`fuxi_sys.h` 的 `fuxi_sys_priv_logged` bitmask），本趟 12000 的第一筆是 rapportd（第 1156 行），所以 bluetoothd 的放行不另外印。其他首見放行：rapportd 1002、identityservicesd 12001、trustd 10001、symptomsd 10002／10003、SpringBoard 1007。仍被拒的是 1002（logd、healthd、dasd、cloudd、callservicesd、bluetoothd、accountsd、ReportMemoryException，簽章沒有 `global-proc-info`）與 kernel_task 10001。
  - 藍牙無退步：`Stack Did Start` 兩次（第 7236、17641 行），電源依序上報 1、0、1（第 7411、15305、17769 行），reason 718／108 為零，`unknown packet type received 253` 為零。Windows SDP 5 項服務、CONNECTED、未配對，0x110A Audio Source 1 筆（137 bytes），GATT device_unavailable，與趟 16 相同。耳機 A2DP 路徑要使用者在場再測。
  - 關藍牙時的閃退（既有問題，趟 16 第 11192 行也有，當時漏記）：第 15028 行 bluetoothd SIGSEGV、pc 0，lr 換算 `0x1000f3b50`。終止堆疊的 `0x1000f3b44` 先呼叫 `0x1000e6aac`，它在 HCI driver 表 `[0x100844850]` 啟用時跳到表的 +0x60，而目前這個 driver 的 +0x60 是 NULL。launchd 重啟 bluetoothd 後，開關仍能再打開。修正方向：在注入 bluetoothd 的 dylib 補一個空的終止函式，或查清 +0x60 原本對應的 driver 操作。
  - 非腳本觸控：tap 腳本在第 14139 行才開始，第 6507–13945 行已有多次手動點擊與邊緣滑動。之後 PosterBoard 建了 15 個 surface，AegirPoster 啟動 27 次、SIGABRT 26 次（第一次在第 6972 行，建立 1125x2436 A008 surface 之後）。11-10 至 11-15 沒有 `fuxiprivs` 的 native-layout 多趟（例如 `native-layout0024-base2323-boot-1`）也有同一處 abort（pc／lr 頁內偏移 `0x198`／`0x5f8` 相同），所以這是手動進入鎖定畫面／桌布畫面時的既有問題，不算 `fuxiprivs` 的退步。
  - boot.log SHA-256 `82b69efe138974d6abf7407ce0cafcf92ce7e39a87e03854ca97e73b6b59b0cb`，證據 `work/Codex-Xiaomi13-GPU-20261109/native-hw000bt5-privs-1119/`（含 `windows-peer.json`）。
- 趟 19 於 11-19 21:06–21:13 由使用者手動重測耳機。loader `native-bt5-privs-headset-1119-loader`（boot SHA-256 `24e10b311498b3ff535ec114c9d99a32a63c5bf1b6fc8d11e8b80218e3c2d0e8`，命令列 540 bytes）是趟 17 的產生器參數加 `--privs`；核心同趟 16。launcher `-Seconds 900 -Reset 20`，21:06:00 開機。使用者 21:11:03 從電源選單關機（第 26792 行，`System shutdown initiated by: SpringBoard`），回 Android 後 launcher 21:13:40 釋鎖。
  - 使用者回報：有聲音，但斷斷續續、幾乎不正常。網站影片無法播放，所以改用鍵盤音效測試。
  - 開機：第一個 bluetoothd（pid 85）HCI 初始化失敗（第 7135 行 reason 108），之前 `unknown packet type received 253` 6 次，第 7148 行 RXDIAG desync=70，也就是 0xfd 漏進。第二個 bluetoothd（pid 303）約 21:07:14 `Stack Did Start`（第 9513 行），第 9718 行電源上報 1。開機時開關轉圈就是這段重啟。
  - 耳機：Apple 耳機 ACL status 0（第 11564 行）。第 11772 行「A2DP configured at 48 KHz. Codec: AAC-ELD, VBR max: 256 kbps. 3 frames * (2+320) bytes = 966 per RTP … every 30 ms」，第 11937 行 `Starting AudioSkywalk read loop`，HFP 裝置也建立成功（LC3）。A2DP 設定期間 bluetoothd 沒有閃退，趟 17 的閃退由 `fuxiprivs=1` 修正。
  - 斷續的原因：每 30 ms 966 bytes 約 258 kbps，UART 115200 扣除起止位元約 92 kbps，不到三分之一。收尾時 RXDIAG（第 25867 行）desync=1436、ibs=1/1061/308、acks=628，最近 16 bytes 幾乎全是 0xfd：控制器一直重送 WAKE_IND，主機的 WAKE_ACK 排在已佇列的 A2DP 資料後面，送得太慢。第 26897 行 SK stats：tx 291961 bytes／4798 包、rx 84082、dropped 355，控制器除錯日誌 ACL 638305 bytes（丟棄）。
  - mediaserverd SIGABRT 5 次（第 17127–23283 行，21:08:02–21:08:19），在 MobileSafari 啟動後（第 14620 行）。例外是 `-[__NSCFNumber objectForKey:]: unrecognized selector`。用主機 shared cache（`E:\fuxi_Backup-archive\workspace-20261118\work\Codex-Xiaomi13-GPU-20261109\dyld-cache`，以 `work/localbuild/wifi-password-profile-1118-symbols/prof-sym.py` 的載入程式）解出 VideoToolbox `FigGetAlignmentForIOSurfaceOutputToDisplayService` ← MediaToolbox `FPSupport_CopyDestinationPixelBufferAttributesWithIOSurfaceSupport` ← `itemfig_createRenderTriplesForVideo`：影片播放管線向顯示服務查 IOSurface 對齊，預期字典卻拿到數字。`native-hw000wlan-hybimport-safari-boot-1` 也有同一個例外，所以這是既有的影片播放問題，和藍牙無關，使用者看到的網站影片無法播放就是它。
  - 關機時 bluetoothd（pid 303）第 26827 行 pc 0 閃退，lr 換算 `0x1000f3b50`，與趟 18 的關藍牙閃退相同。
  - boot.log SHA-256 `eb469f41c5b15b7da7ced76ebe29efa319c80788d4e2025e4d76da44dca2c3f8`，證據 `work/Codex-Xiaomi13-GPU-20261109/native-hw000bt5-privs-headset-1119/`。
- 趟 20 於 11-19 21:49–21:55 驗 UART 換速（`fuxi,bt-baud`、`fuxi,bt-nolog`，兩者預設關）。
  - 做法：照 Linux `qca_setup`，以 115200 查版本後送 EDL `01 48 fc 01 <enum>`（不等回覆），重設 UART SE 的 RCG 與 GENI 除頻，再重查版本，最多三次。確認不到時以新速率送 enum 0，退回 115200 後再確認。RCG 來源為 GPLL0_EVEN 300 MHz，M/N 取自 gcc-sm8550 的頻率表：921600＝14.7456 MHz 768/15625（enum 10）、2M＝32 MHz 8/75（13）、3M＝48 MHz 4/25（14）、3.2M＝51.2 MHz 64/375（17）；CFG 都是 0x102601，GENI 除頻 1（暫存器 0x11）。之後下載 rampatch，NVM tag 17 的 baud 位元組寫實際 enum。`fuxi,bt-nolog`＝1 時，NVM 之後送 0xfc17 `{0x14, 0x00}`（Linux `qca_disable_soc_logging`），失敗只記錄。常駐泵依實際速率重新取得 UART（`setup->baud_active`）。
  - 建置：ksrc `1ce05ea`（`fuxi_bt.{c,h}`、`fuxi_bt_board.{c,h}`、`fuxi_bt_geni_fifo.{c,h}`、`fuxi_bt_setup.{c,h}`、`fuxi_bt_xnu.c`、0117，以及 3 個主機測試）。harness 補丁 SHA-256 `a2e490919d0d8205ec478f3ac1f84fdc7dd77781ff166fd939afbf76b8e59c5b`。核心 `kernel-local-btbaud-9g-artifact`（本機 WSL，24 秒增量，ISA 解碼 2,566,337 條、禁止指令零條），SHA-256 `04cbb89cd5816c857e5625c21ca1a2507f594cdf00fc6386ee0185d933e8eded`。loader 產生器加 `--bt-baud {921600,2000000,3000000,3200000}` 與 `--bt-nolog`，兩者都要求 `--bt 5`，並檢查核心含對應的 ADT 字串。
  - 主機測試：7 個藍牙測試以 `--sanitize` 全數通過。`test-fuxi-bluetooth-setup.py` 涵蓋 36 組換速（4 種速率、分段、舊速率回 ACK、雜訊重試、nolog）、開機速率與非法速率、控制器拒絕、RX／TX 不通、acquire 失敗、nolog 錯誤，以及 500 次 fuzz。`test-fuxi-bt-board.py` 驗 4 種速率的 M/N/D/CFG 與 GENI 0x11、同速率不重寫、回 115200、root off 後恢復、3.2M 下 power_off 還原，共 411 個 MMIO 故障點（還原前 393、還原中 18、已換速 274）。`test-fuxi-bt-geni-fifo.py` 驗各速率的除頻與 4 種拒絕。
  - loader：`native-bt5-baud3m2-1119-loader`（boot SHA-256 `6ed956a315670ffa8b60402bf7c6711833903229912628d057cc1b6cd1031fb9`，payload `57f488c637ba17c074625a04d5035fe8d41b752203e4176d32c375ae987a8aab`，命令列 532 bytes）。參數為趟 18 的配置加 `--bt-baud 3200000 --bt-nolog`：

    ```powershell
    python -X utf8 work/adsp-battery/prepare-wlan-w18-loader.py build --like native-btsetup-1118-loader --artifact kernel-local-btbaud-9g-artifact --wifi-ios --wifi-args-adt --with-adsp --audio-spf --audio-tdm-pcm --bt 5 --bt-native --bt-ibs --bt-optional '0x0c5b,0xfd3c,0xfce9,0xfd47' --bt-pad 0xfc00 --bt-mask '0x2003:2:0x30' --bt-txmask '0x200b:5:1,0x200d:12:1,0x2006:5:1,0x2041:0:1,0x2043:1:1' --bt-oslog 3 --bt-baud 3200000 --bt-nolog --haptic 3 --inject-media --product-haptics --haptic-ui-ms 15 --haptic-envelope --haptic-amp 128 --hal-haptics --wifi-audit --wifi-password-prompt --wifi-sb-audit --wifi-sb-refresh --wifi-rssi-bounds --wifi-ux 2047 --stderr-log 'mediaserverd,bluetoothd,BlueTool' --rtc-calibration work/adsp-battery/rtc-calibration-bt-baud-1119.json --privs --tap-delay 75 --tap-script 's540,2390,540,999,99;w2200;t80,175;s560,690,560,1570,340;w1800;t260,1200;w3500;t935,415;w2400;t740,1360;w6000;p200;w9500;t935,415;w12000;p200' --name native-bt5-baud3m2-1119-loader
    work/statusbar-fix/launch-loader-check.ps1 -Loader native-bt5-baud3m2-1119-loader -Kernel kernel-local-btbaud-9g-artifact -Out native-hw000bt5-baud3m2-1119 -Seconds 202 -Reset 5 -FramebufferHeight 640 -PeerTestPython <Windows Python> -PeerIdentityRuns native-hw000bt-early-exit-1119-e,native-hw000bt-fce2-native-status-1119-f -PeerAfterPowerCycle -PeerAudioSource
    ```

    21:49:42 開機，21:55:38 回 Android 並釋鎖。
  - 判據：成功為 `setup baud requested=3200000 active=3200000 status=ok fallback=off`、`uart baud=3200000 div=1 rcg_hz=51200000`、`resident ready baud=3200000`。失敗為 `fallback=on`、`status=` 不是 ok，或常駐泵仍是 `resident ready baud=115200`。
  - 換速：第 5777 行 `setup status=ok stage=done … elapsed_ms=1445 patch_ms=1059`（趟 19 在 115200 下韌體約 23 秒）。第 5786 行 `setup baud requested=3200000 active=3200000 status=ok fallback=off attempts=1`，第 5787 行 `setup nolog status=ok`，第 5789 行 `uart baud=3200000 div=1 rcg_hz=51200000 rcg_switches=1 nolog=1`，第 5791 行 `resident ready baud=3200000`。開機的第一個 bluetoothd 就完成初始化（第 6410 行 `Stack Did Start`，趟 19 第一個先 reason 108 退出）。整趟 reason 718／108 為零，`unknown packet type` 為零。
  - 耳機：Apple 耳機 ACL status 0（第 17024 行），第 17187 行 A2DP 48 kHz AAC-ELD、VBR 256 kbps，第 17246 行 `Starting AudioSkywalk read loop`。**使用者回報耳機聲音正常**（趟 19 斷續失真）。收尾 SK stats（第 28599 行）tx 227439 bytes／1953 包、rx 45036、`dropped=0`（趟 19 為 355）。
  - 電源與閃退：電源依序上報 1、0、1、0、1（第 6877、12598、15300、19658、22307 行），`Stack Did Start` 3 次（第 6410、14886、21884 行）。兩次關閉都讓 bluetoothd pc 0 閃退（第 12308、19283 行，lr 減 mach header 為 `0xf3b50`，即 `0x1000f3b50`），和趟 18 的關藍牙閃退相同；launchd 重啟後都能再開啟。
  - Windows 對端：SDP SUCCESS、5 項服務、CONNECTED、未配對；0x110A Audio Source 1 筆（137 bytes）；GATT device_unavailable，與趟 16、18 相同。
  - RXDIAG：desync 逐步增加到 550，`recent` 全是 `fdfe`。這是控制器在封包邊界送出成對的 IBS WAKE_IND／SLEEP_IND，被計入 desync。acks=274、wake_timeouts=0，resyncs=3（三個連線各一次），資料未遺失。
  - Safari 黑屏（使用者回報）：這份 loader 漏帶 `--display-gamut sRGB`（趟 17、19 有帶），ADT `/product artwork-display-gamut` 回到 D22AP 的 `P3`。MobileGestalt 因此回報深色彩，WebKit GPU 程序的圖層改用 `w30r`（8 個）與雙平面 `b3a8`（19 個），MobileSafari 的 1125x2436 快照也是 `w30r`；趟 19 的 17 個 WebKit 圖層全是 BGRA。backboardd 的 M2V 為它們建立 51 個 BGR10XR shadow，網頁畫不出來。SpringBoard 與設定 app 仍是 BGRA，畫面正常。11-17 的 `native-hw000wlan-hybimport-safari-boot-1` 也是 P3，分頁縮圖同樣空白。所以黑屏來自 loader 參數，與換速核心無關；產生器 `--display-gamut` 的註解早已寫下這個原因。
  - boot.log SHA-256 `b55a5a8cd2b1e9f4b230f54d8266152f2f3f880e0f106399f6b55fe1c3bc4729`（28716 行），證據 `work/Codex-Xiaomi13-GPU-20261109/native-hw000bt5-baud3m2-1119/`（含 `windows-peer.json`）。
- 趟 21（22:05–22:20）以趟 20 的本機 9g 核心進行手動測試。loader 沿用趟 20 的建置命令，移除 `--tap-delay` 和 `--tap-script`，加入 `--quiet-screen --display-gamut sRGB`，輸出 `native-bt5-baud3m2-srgb-1119-loader`。WebKit GPU 建立 8 個 BGRA 表面。使用者仍無法播放 Safari 影片，mediaserverd 共 4 次 SIGABRT（16902、18560、19692、20743 行），例外為 `-[__NSCFNumber objectForKey:]`。藍牙每次重啟均讀回 1 筆配對，關閉時 bluetoothd 閃退 2 次，未觀察到耳機 A2DP 建立。22:20:32 launcher 收尾。證據在 `work/Codex-Xiaomi13-GPU-20261109/native-hw000bt5-baud3m2-srgb-1119/boot.log`，SHA-256 `6859e9babf630891e0c514cd99ceb8cfe8213c61b127d1b003bf0b25cab92065`。
- 趟 22（22:31–22:37）使用 ksrc `3b2d9b6` 和 disp0 `fuxi,mfb-align=1`。SweetFramebuffer 以字典發布 `DisplayPipePlaneBaseAlignment`，鍵 `PlaneBaseAlignmentLinear` 為 32-bit 整數 64。格式取自同版 VideoToolbox 與 QuartzCore，loader 屬性位置沿用 `mkadt-fuxi.py:set_display_options`。核對趟 21/22 兩份 private.adt，僅此一個屬性有差異。以 `python3 tools/test-fuxi-mfb-align.py --sanitize` 驗過發布成功、四個故障點和引用釋放；loader 自測通過。
  - 本機命令 `work/localbuild/build-kernel.ps1 -Ram 9g -Name kernel-local-mfbalign-9g-artifact -Ath12k D:\xiaomi13\fuxi_ios\work\adsp-battery\ath12k-objects-b2876c6`。增量 25 秒、ISA 2,566,410 條、禁止指令 0；核心 SHA-256 `122de00b7b93a306a6202b94482bf1a11f25acad034f50cfbce02d5bfdd9b3c5`。使用趟 21 的 loader 參數，替換 `--artifact kernel-local-mfbalign-9g-artifact --name native-bt5-mfbalign-1119-loader` 並加 `--mfb-align`。boot SHA-256 `10c8ce7638efb29f990adc592ebda6d3cf57ab4dbc4e07131890e6cc7724b657`。
  - `work/statusbar-fix/launch-loader-check.ps1 -Loader native-bt5-mfbalign-1119-loader -Kernel kernel-local-mfbalign-9g-artifact -Out native-hw000bt5-mfbalign-1119 -Seconds 900 -Reset 20`。第 79 行確認顯示字典發布，第 19884 行開啟 Safari。本趟先前的型別例外及 mediaserverd 的 CRASH 行各 0 筆。媒體服務 PID 28、345、383 依次完成顯示列舉，之後又有新實例，最後為 PID 510。使用者確認網頁正常、點影片無法播放；顯示屬性已生效，影片播放仍未通過。
  - 使用者回報重開藍牙後沒有自動回連。1496、13299 行均讀到 1 筆配對；關閉時 bluetoothd 閃退 1 次（13136）。18286 行設定頁送出手動連線，18660 行 ACL 成功，18703 行讀回 link key。開機後 rapportd 另連過未配對的 BLE 周邊。耳機自動連線的觸發點與關藍牙閃退的因果關係仍待驗證。
  - 22:37:33 回 Android，映像已解除映射並釋鎖。觀察檔 `work/Codex-Xiaomi13-GPU-20261109/native-hw000bt5-mfbalign-1119/boot.log`，31,277 行，SHA-256 `eede326e9b250f747d250fba800bdd074d44e4c52aa1497d3bc87bfa090bcf0a`。以 `work/statusbar-fix/launch-loader-check.ps1 -InspectCrash -Out inspect-crash-mfbalign-1119` 唯讀查看最近報告；列出的最新 64 筆中，最近一次 mediaserverd 報告來自趟 21。22:39:39 釋鎖。證據在 `work/statusbar-fix/inspect-crash-mfbalign-1119/provenance.json`（讀 27 塊、overlay 零寫入）。
- 趟 23（11-19 22:54 開始）驗原生回連。趟 22 重啟後，bluetoothd 讀回耳機 IRK；控制器不支援 `BD_VSC_LE_META_ADD_IRK_TO_LIST`（0xfce9），轉接層卻回報成功。趟 23 使用同一顆本機核心、手機現有函式庫，只把趟 22 loader 的 `--bt-optional` 改為 `0x0c5b,0xfd3c,0xfd47`，其餘 ADT 完全相同。未部署 SpringBoard 代連版本。
  - `work/statusbar-fix/launch-loader-check.ps1 -Loader native-bt5-fce9-status-1119-loader -Kernel kernel-local-mfbalign-9g-artifact -Out native-hw000bt5-fce9-status-1119 -Seconds 470 -Reset 11`。boot SHA-256 `5e5b8480247fe4388d7c93f146a04ff853bc7e9352d34307ed9e461e7abe4d2a`。兩次原生 Stack Did Start（6338、17197 行），兩次加入 IRK 都向 daemon 回報 701（6324、17184）。電源 1→0→1，配對筆數均為 1，關藍牙時仍閃退一次。使用者確認重開藍牙後沒有自動回連；耳機入盒至顯示未連線，再取出，仍未回連。只傳回真實錯誤不足以修復。
  - 另測小米與 iPhone 17：使用者回報雙方設定頁互相可見，從兩邊發起都顯示配對失敗。小米原生設定頁有宣告 discoverable（19059 行）。每條 UART.SK 的一般 H4 記錄在第 400 筆截止；第二條於 21490 行達上限，後段無法依缺少事件判斷。四趟（20–23）用 `windows-bluetooth-peer.py:controller_from_console` 離線比較本機識別相同，僅記比較結果。證據 `work/Codex-Xiaomi13-GPU-20261109/native-hw000bt5-fce9-status-1119/boot.log`，SHA-256 `6b39813a1e15fffd149b707936655812ab389d9e5313d98fd170e5a0cb8bdaaa`。
- 趟 24（11-19 23:07 開始）沿用趟 23 全部藍牙設定，另加 `--bt-link-log`。本機 WSL `kernel-local-btlinklog-9g-artifact`（ksrc `cf01413`，harness `f9cd1dd`，ISA 2,566,550 條，禁止指令 0）。新增獨立限額的 HCI 里程碑，僅記方向、命令、事件與狀態或掃描模式。既有 H4 parser 與主機模擬 FIFO 可直接復用；`python3 tools/test-fuxi-bluetooth-pump.py --sanitize` 通過（包括配對碼與金鑰類事件不複製內容，四類相關錯誤／空值測試）。核對 private.adt 僅增加 `fuxi,bt-link-log=1`。
  - 建置重用趟 22 的 `build-kernel.ps1 -Ram 9g -Ath12k` 指令，換上述核心名稱；loader 沿用趟 23，替換 `--artifact kernel-local-btlinklog-9g-artifact --name native-bt5-linklog-1119-loader` 並加 `--bt-link-log`。`work/statusbar-fix/launch-loader-check.ps1 -Loader native-bt5-linklog-1119-loader -Kernel kernel-local-btlinklog-9g-artifact -Out native-hw000bt5-linklog-1119 -Seconds 300 -Reset 7`。boot SHA-256 `20c2b35e57d9d4895482b031c59ef4b8e52bae57ea79655053db0a02cf10a664`。
  - 標記中看見進、出方向皆有 ACL 成功，查詢遠端名稱有一次 HCI 0x04（Page Timeout）。其中一段達獨立限額 128（27477 行），不能用後續無標記排除事件。較晚一次原生設定頁對未配對裝置呼叫 ConnectServices（33445–33447），bluetoothd 確認 iPhone 的 outgoing ACL status 0（33703–33705），之後完成 SDP 資料庫查詢（34210–34211）。使用者確認雙方均未看到六位數配對碼；兩機看得見、ACL 成功與完成配對是不同階段。需進一步確認哪項服務要求認證。
  - 離線核對私有 ADT 與趟 20–23 的 controller Read BD_ADDR。本機控制器跨開機穩定，但 /bluetooth 的 local-mac-address 來自 /chosen 的模板值，和控制器位址（含反序比較）不同。bluetoothd 在趟 24 啟動時讀回的 service 值與 ADT 一致。只記布林比對結果。證據 `work/Codex-Xiaomi13-GPU-20261109/native-hw000bt5-linklog-1119/boot.log`，SHA-256 `1cd1924f103cc9901f8d162e3c5893ae3ccb8e297659c41c929ca0a21a41ef12`。
- 趟 24 尾段由趟 25 啟動器取回。第 33447 行設定頁對未配對裝置請求服務，33705 行去向 ACL 成功，34210–34223 行完成服務資料庫查詢並結束 SDP。34595 行 bluetoothd 發生與此前關閉藍牙相同位置的 SIGSEGV，34605 行重新啟動。無法將閃退判定為配對失敗原因。證據 `work/adsp-battery/fuxilog/20261119-231618/console.txt`，SHA-256 `7af90d41d832b060e4f2d9dbf8627f5a35e763adc41cf02b6ded663450909a91`。
- 趟 25（11-19 23:17–23:25）沿用趟 24 核心、診斷開關及手機現有函式庫。loader 產生器重用 Windows 對端工具的 HCI Read BD_ADDR 解析器，核對兩份不同 SHA 的實機紀錄，僅將私有 ADT 的 `/bluetooth/local-mac-address` 改為控制器位址。趟 24 的建置命令換成 `--name native-bt5-controller-id-1119-loader`，加 `--bt-controller-identity native-hw000bt5-mfbalign-1119 native-hw000bt5-fce9-status-1119`。boot SHA-256 `9b436f6744824029332660b979c5ac95582d2f7968252144d932ede1e10073e5`。
  - `work/statusbar-fix/launch-loader-check.ps1 -Loader native-bt5-controller-id-1119-loader -Kernel kernel-local-btlinklog-9g-artifact -Out native-hw000bt5-controller-id-1119 -Seconds 300 -Reset 7`。第 1296 行確認 bluetoothd 讀到控制器位址。第 24145 行有設定頁手動連線要求，另一次回連在 28675 行電源開啟後，30891 行收到來向連線請求，30904 行為已配對耳機、incoming 1、status 0。31009 行找到原有 link key，31058 行完成加密，31066 行建立 A2DP 訊號通道，31109 行選定 AAC-ELD，31146 行設定成功。這段沒有設定頁手動連線要求；使用者確認出盒後完全沒點即顯示已連線。原生回連至少成功一次。趟 24 已有來向 ACL 及驗證，位址變更的因果關係與反覆回連的成功率仍待驗證。
  - 使用者確認小米與 iPhone 17 從設定頁仍無法配對。關閉藍牙時的 bluetoothd 閃退仍可見於 13853、19199、25920 行。23:17:58 暫時開機，主機量到 TSENS 峰值 85.2°C。23:25:50 啟動器確認 Android 開機並釋鎖。證據 `work/Codex-Xiaomi13-GPU-20261109/native-hw000bt5-controller-id-1119/boot.log`，33,123 行（按 LF 計），SHA-256 `ef4c9ef66b1aec2411b331ed3e1f6969a667b51209b0e67076c8afd5e76a0971`。
- 趟 26（11-20 00:19–00:26）使用本機 WSL 核心 `kernel-local-bt-media-diag-1120-9g-artifact`（ksrc `92a5ecf`、harness `3027c50`，SHA-256 `c4293407e1c9388094937f73327091df942f2b7ea10076f4c64c982d1cc4cd2b`，41 項主機測試通過，`ISA.txt` 解碼 2,566,916 條、禁止指令 0 條）。0117 將 `FUXI BT LINK` 拆為 inquiry（上限 64）與非 inquiry 連線／認證事件（上限 192）並解碼 `evt=0x05`／`0x08` 原因與加密模式；0044 新增預設關閉的 `fuximediaexit`／ADT `fuxi,media-exit-trace=1`（loader `--media-exit-trace`）追蹤 `mediaserverd`、`MobileSafari`、`VTDecoderXPC*`、`com.apple.WebKit*`、`audiomxd` 及有界 `lskdd` 退出。loader `native-bt5-diag-1120-loader`（命令列 540 bytes，boot SHA-256 `2e0af6a231f0f19249ca6e889acfa9eff6a0a7db3a12da24eb89799f51988b1f`）。`work/statusbar-fix/launch-loader-check.ps1 -Loader native-bt5-diag-1120-loader -Kernel kernel-local-bt-media-diag-1120-9g-artifact -Out native-hw000bt5-diag-1120 -Seconds 360 -Reset 8`，後以 `-PullConsole -Out pull-diag-1120` 補回完整 mtdblk 尾段（`boot #164`）。主機 TSENS 峰值 70.2°C（`cpu-1-5`）。
  - 藍牙 HCI 診斷（完整紀錄 `work/adsp-battery/fuxilog/20261120-002736/console.txt`，21,282 行，SHA-256 `5b4fcc9157ae9b69d09dab52592a46965750ffb2a173d8762f4728eda0c037a9`；USB `work/Codex-Xiaomi13-GPU-20261109/native-hw000bt5-diag-1120/boot.log`，18,641 行，SHA-256 `4178e95b471e0735b7497b3a2aa7659489f00e07cee4c5b60f7febd9c9e0198d`）：開機確認 `tx op=0xc56 value=1`（SSP 開啟，status 0）。iPhone 17 來向（ms 214525–216125）與小米去向（ms 223646–228461）兩方向均完成 ACL（`evt=0x3 status=0`），全程均未發出 `0x411` Authentication 或任何 SSP 事件，隨即由對端（`evt=0x5 value=19`、`0x13` Remote User Terminated）或本機（`tx 0x406`、`evt=0x5 value=22`、`0x16` Local Host Terminated）主動中斷 ACL。同趟已配對耳機於 ms 231094 與 254217 兩度完成 `0x411`→`evt=0x17`→`0x40b`→`evt=0x6 status=0`→`0x413 value=1`→`encryptionComplete status:0 mode:2 keySize:16`（WCN7850 以 BT 5.3 `evt=0x59` 回報加密變更，故 `evt=0x08` 未觸發）與 A2DP AAC-ELD 48 kHz 設定（第 20080 行）。切藍牙開關仍見既有 `pc=0` SIGSEGV（第 13502 行）。
  - 離線核對本機 20H392 原廠 `bluetoothd`（`work/statusbar-fix/inspect-radio-assets-20261116-1436/files/usr/sbin/bluetoothd`）與趟 24 SDP 紀錄（第 33905–34211 行）：設定頁連線遮罩 `0xffffd7bf` 在 `HandleQueryComplete`（`0x1003c17d0`）逐一核對 16 項服務。對端 iPhone 17 宣告的 12 筆傳統藍牙 SDP 紀錄屬同角色伺服器／音訊來源（`AudioSource 0x110A`、`HandsfreeAudioGateway 0x111F`、`PhonebookAccessServer 0x112F`），`RemoteService`（`0x1004bb6f0`）遇有 `0x110A` 卻無 `0x110B` 直接標示不支援，`MAP`（`0x200`）於 `0x1003c1bfc` 略過外連，`GATT`/CATT（`0x100000`）於 `0x100369be4` 僅放行特定 Apple 音訊配件 PID（`0x200a..0x200f`、`0x2012..0x2013`，iPhone 17 為 `pid=0x792e`）。唯一匹配的 `NetworkConsumerService`（`0x1000`，對應對端 `NAP 0x1116`）在 `0x100489dac` 檢查 SDP 屬性 `0x0008`（`ServiceAvailability`），對端未開個人熱點時值為 0 而回傳不可用（`0x72`）。因沒有任何可連的傳統藍牙服務，原廠 `bluetoothd` 不會進入需認證的 L2CAP 通道，因此雙方均不觸發 `0x0411` 與六位數 SSP 配對碼，直接中斷 ACL 並在設定頁顯示配對失敗，屬原廠兩支 iPhone 在未開個人熱點時的既定行為。
  - Safari／`mediaserverd`：本趟 6 分鐘窗口未開啟 Safari（`exec MobileSafari: 0`、`exec mediaserverd: 1`、`FUXI EXIT:: 0`），待下趟（趟 27）沿用同一核心與 loader 實測點擊影片觸發 `FUXI EXIT:` 診斷。
- **結論／未完成**：已驗設定頁關→開、對外 BLE 連線、電腦至手機的傳統藍牙服務查詢、A2DP Audio Source SDP 登錄（趟 16）、`fuxiprivs=1` Skywalk 放行（趟 18）、3.2 Mbaud 降初始化至 1.4 秒並確認耳機 A2DP 正常（趟 20）、耳機原生自動回連與加密（趟 25／26），並由實機 HCI 與原廠 `bluetoothd` 二進位確認兩支 iPhone 未開個人熱點時設定頁無可連傳統藍牙服務而不發起 SSP 配對（趟 26）。待做：關藍牙時 `bluetoothd` 因 HCI driver +0x60 為 NULL 閃退、IBS 成對位元組不計入 desync、Windows GATT 及長時間穩定性。Safari 網頁在 sRGB 下正常；趟 22 已消除顯示屬性型別例外，點影片仍無法播放，待趟 27 以 `--media-exit-trace` 抓取 `mediaserverd` 無聲退出原因。耳機測試的 loader 要帶 `--display-gamut sRGB`，否則 Safari 網頁黑屏（趟 20）。
- **出處**：`ksrc/xnu/fuxi-bluetooth/fuxi_bt_{sk,pump,xnu}.c`、`ksrc/xnu/patches/0044-sweet-crash-log-reason.patch`、`ksrc/xnu/patches/0118-fuxi-exec-inject.patch`、`harness/userland/fuxi-shape/FuxiBluetoothProbe.c`、`FuxiBlueTool.c`，以及上述產物的 `BUILD.txt` 和 `logs/tests.txt`；第一趟 console 在 `work/Codex-Xiaomi13-GPU-20261109/native-hw000bt5-tdm-smmu-1118-2125/boot.log`。

### K-078 用目前 ADSP 驗證 Fuxi 喇叭與播放圖

- 11-19 18:04 以 mode 7（loader `--audio-bottom-tone`，ADT `fuxi,amp=7` 搭配 `fuxi,audio-spf=5`）在保護與校正閘門後實機完成下方喇叭 `-30 dB` 1 秒 2 kHz（`-20 dBFS`）短測（使用者 2026-11-18 同意範圍：只限下方喇叭、最低 `-30 dB`、`0x6000=0xb885`、短測、先核對保護與校正）。
  1. 交握（`ksrc/xnu/fuxi-adsp/fuxi_amp_tone.h`，ksrc `4bcbee3`／harness `66f5d72`）：`fuxi_amp_init()` 先設 `PENDING`，讓 `fuxi_audio_spf.c` 的 format 5 在 SPF state 1 與 DMA 開啟後停在 `GATE` 等候，不開 TDM 圖。
  2. 功放端先完整跑完 mode 6；只有在 `CSPL_STATE=0`（`CSPL_ST_RUNNING`）、`ERRORNO=0`、`CAL_STATUS=1` 時才切換 TDM 腳位（gpio121／123／124，function 1、8 mA，eGPIO 取回）並推進 `READY`。
  3. PCM 端見到 `READY` 後開啟 secondary TDM 與 playback 圖，先播滿 5 個週期（100 ms）靜音並確認 `WRITE_DONE` 後推進 `CLOCKED`；功放端沿用早期 Fuxi `au_amp_on.h` 旁路序列（清 IRQ1、升壓 1000 nH／15 uF／4000 mA、`0x3800==0`、TDM／PLL 暫存器、`AMP_EN`、100 ms 靜音、test key `0x2084=0x002f1aa0`、`GLOBAL_EN`、`PUP_DONE`、PLL 鎖定、200 ms 靜音、故障檢查、`-30 dB` 解除靜音）後推進 `ON`。
  4. PCM 端播完 50 個週期（48,000 frames＝1 秒 2 kHz 正弦，首尾各 40 ms 淡入淡出）再補 5 個週期（100 ms）靜音後推進 `DRAIN`；功放端期間每 100 ms 檢查一次 `IRQ1_STATUS1` 故障位元（`0x800281c0`），收到 `DRAIN` 做最後檢查後執行靜音、下電（`PDN_DONE`）、`0x2084=0x002f1aa3`、清 `AMP_EN` 與 `0x4800`，推進 `OFF`，回到 reset 並還原 TDM／I²C 腳位與時脈投票；PCM 端收尾最後在途緩衝並關圖。

  結果（18:04–18:08）：
  - mode 6 閘門：`otpid=8`、`trims=98`、`full muted HALO verified bytes=29876`、`cspl_state=0 errorno=0 cal_r=0x241e cal_status=0x1`、`protection running confirmed halo_state=2 after 2 ms`，核心停妥後進 `tone ready`。
  - 出聲與下電：`TDM clock running, 5 periods of zeros played` → `tone on -30 dB after 225 ms, pup 1 ms, pll 0x2b, irq1=0x400000/0/0x2f/0x80000000`（`PLL_LOCK` 置位、`PLL_UNLOCK`/`OTP_BOOT_ERR` 為 0，`IRQ1_STATUS1` 六個故障位元全 0）→ `tone played, 48000 frames in periods 24-73` → `tone done stage=8 waited=225 ms on=1070 ms checks=10 irq1=0x400000/0/0x2f/0x80000000 down ok state=6` → `TDM pins restored gpio121 0x1 gpio123 0x1 gpio124 0x1`。
  - PCM 與收尾：`FUXI AUDIO TONE: complete periods sent=82 done=82 tone_frames=48000 stray=0 status=0 amp_state=6`，所有關圖與釋放時脈步驟 `status=0`，`FUXI AUDIO PCM: probe complete result=ok`。18:08:23 回 Android 並釋鎖（主機 tsens 最高 74.2°C）。使用者當下不在手機旁，以上為硬體暫存器與 ADSP 控制台實證，尚未有耳聽確認。
  - 證據：`work/Codex-Xiaomi13-GPU-20261109/native-hw000-amp-tone-1119/boot.log`（7,477 行，SHA-256 `49cc2c371d0670e2595454ca651fca24265bd2d55fa62098a0a806ac3adc2239`）第 443–470、703–705、1041–1190、1350–1352、1924–1964 行。
- mode 7 的復現：
  - 主機測試（WSL clang + ASan／UBSan）：
    - `python3 work/adsp-battery/ksrc/tools/test-fuxi-audio-spf.py`：新增 `test_tone()` 驗證 GATE／建圖／PREROLL／CLOCKED／ON／PLAY（逐筆核對 DMA 緩衝區正弦、淡入淡出、靜音與位址輪替）／DRAIN／OFF／FINISH 與 12 條故障／逾時／亂序／斷線路徑（4 項 PASS）。
    - `python3 work/adsp-battery/test-amp-run-cs35l41.py`：擴充 mode 7 閘門測試（`CSPL_STATE=0x10` 或 `ERRORNO=1` 拒絕、`0/0/1` 回傳 1、mode 8 零操作拒絕；4,707 故障點通過）。
    - `python3 work/adsp-battery/test-amp-tone-cs35l41.py`：核對 50 筆精確暫存器寫入順序與值、`0x6000` 只允許 `{0xa678, 0xb885}`、`0x6c04` 恆為 0、162 個 I²C 故障注入與全故障／逾時守衛；測試抓出 `ampTonePowerUp()`／`ampToneDown()` 在測試鑰匙區間匯流排失敗時未上鎖的缺陷並修正；10 項刻意變異全數抓出。
  - 核心以 `work/localbuild/build-kernel.ps1 -Ram 9g -Ath12k work/adsp-battery/ath12k-objects-b2876c6 -Tests -Name kernel-local-amp-tone-1119-9g-artifact` 建置（本機，64 秒、make 4 秒，40 項 workflow 測試通過，ISA 2,565,488 指令、禁止指令 0）。kernel SHA-256 為 `c878ca440283407393bad7b69454100f876f4e76980436d663aefe827d5a8e77`。harness 補丁由 `git diff eafc778 HEAD` 重產，不含 ksrc 未提交的 WIP。
  - loader：`python -X utf8 work/adsp-battery/prepare-wlan-w18-loader.py build --like native-btsetup-1118-loader --artifact kernel-local-amp-tone-1119-9g-artifact --wifi-ios --wifi-args-adt --with-adsp --audio-spf --audio-tdm-pcm --audio-bottom-fw work/adsp-battery/android-cs35l41-1118-2334/odm/firmware/cs35l41-dsp1-spk-prot.wmfw --haptic 5 --haptic-rtp work/adsp-battery/android-haptic-waves-1118-2352/0_click_P_RTP.bin --haptic-pcm --inject-media --product-haptics --haptic-envelope --haptic-amp 128 --hal-haptics --wifi-audit --wifi-password-prompt --wifi-sb-audit --wifi-sb-refresh --wifi-rssi-bounds --wifi-ux 2047 --stderr-log mediaserverd --rtc-calibration work/adsp-battery/rtc-calibration-amp-tone-1119.json --audio-bottom-full --audio-bottom-cal work/adsp-battery/android-cirrus-cal-1118-2348 --audio-bottom-run --audio-bottom-tone --name native-amp-tone-1119-loader`。boot SHA-256 為 `0eaf4d0074d72787411bff045b500a33f2762ec36f83e05446768e334ae1472f`。
  - 上機：`work/statusbar-fix/launch-loader-check.ps1 -Loader native-amp-tone-1119-loader -Kernel kernel-local-amp-tone-1119-9g-artifact -Out native-hw000-amp-tone-1119 -Seconds 90 -Reset 3`。
  - 成功判據：`FUXI AMP: tone on -30 dB`、`FUXI AUDIO TONE: tone played, 48000 frames`、`FUXI AMP: tone done`（`down ok state=6`）、`FUXI AUDIO TONE: complete`（`stray=0 status=0`）、`FUXI AUDIO PCM: probe complete result=ok`、`FUXI AMP: TDM pins restored` 齊備，且無 `tone refused`、`FUXI AUDIO TONE: failed:`、`silent PCM only` 或 `PLL not locked`。

- 11-19 16:57 以 mode 6（loader `--audio-bottom-run`，ADT `fuxi,amp=6`）在靜音下試跑下方保護韌體。校正讀回之後的順序如下：
  1. 先依 Linux B2 errata 與 `otp_map_1` 解 OTP。實機 OTPID 為 8，共 98 個 trim，test key 已重新上鎖。
  2. 再照 Linux v6.12 `cs35l41_hda.c` 的 `cs35l41_smart_amp` 啟動 HALO：寫 16 個 fs rate、解 MPU 鎖，再以 `cs_dsp_halo_start_core` 啟動。依 Xiaomi `wm_adsp.c` 先寫 MAX_LRCLK_DELAY 0x20。
  3. GLOBAL_EN、AMP_EN 全程為 0。PLL、ASP、升壓與音量都不寫，也沒有 I2S 時脈。

  結果：
  - HALO_STATE 2 ms 內進 RUN，DSP_MBOX_2 為 0（RUNNING）。依 Xiaomi `cs35l41_set_csplmboxcmd` 的 sticky／遮罩流程送 PAUSE，第一次輪詢即收到確認，狀態變 1（PAUSED）。
  - 控制項：FIRMWARE_STATE 1、CSPL_STATE 0（`CSPL_ST_RUNNING`）、ERRORNO 0、CAL_R `0x241e`、CAL_STATUS 1。CAL_R_SELECTED 與 CAL_SET_STATUS 為 0。heartbeat 在 20 ms 內未增加，推測沒有音訊時脈時不前進，待有 TDM 時再查。
  - 依 `cs_dsp_stop` 關 watchdog、清 CORE_EN、soft reset 後，CCM 讀回 0，IRQ1 為 `0/0x100002/0x20000000/0x3`。功放回 reset，腳位與投票已還原。
  - 同趟 SPF state 1，TDM／PCM `probe complete result=ok`。17:00:03 回 Android 並釋鎖。
  - 證據：`work/Codex-Xiaomi13-GPU-20261109/native-hw000-amp-run-muted-1119/boot.log` 第 443–462、478–479、1080–1082、1144、1151、1159–1162 行。
- mode 6 的復現：
  - 主機測試在 WSL 執行 `python3 work/adsp-battery/test-amp-run-cs35l41.py`。模型模擬暫存器、test key、HALO 核心與 CSPL 郵箱，另從 `ref-linux/cs35l41-lib.c` 解析 `otp_map_1`，逐字對照 GENMASK 算出獨立期望值。
    - 4,707 個匯流排操作逐一注入失敗：每次都不得確認成功，key 必須上鎖、核心必須已停。
    - 另測 HALO 不進 RUN、啟動時郵箱狀態無效（不得送出命令）、PAUSE 無回應、GLOBAL_EN 已開、OTPID 不符，以及 mode 5 不受影響。
    - 9 個刻意改壞的版本全部被抓到，包括 OTP 起始位元、不上鎖、不解 MPU、不改遮罩、不停核心、不檢查郵箱與不等 RUN。
    - 主機測試期間補上一處：停止階段的匯流排失敗也判為失敗（stage 9）。
    - `test-muted-cs35l41.py` 仍為 215 個故障點通過。
  - 核心以 `work/localbuild/build-kernel.ps1 -Ram 9g -Name kernel-local-amp-run-1119-9g-artifact -Ath12k work/adsp-battery/ath12k-objects-b2876c6 -Tests` 建置（本機，64 秒，40 項通過，ISA 禁止指令 0）。kernel SHA-256 為 `a51acdbaf4335794642eec06080f688ba32e1cca5f3bae718bbc39095b050043`。harness 差分只換入 `fuxi_amp.c` 與重產的 0117；ksrc 其餘未同步的 WIP 未帶入。
  - loader：`python -X utf8 work/adsp-battery/prepare-wlan-w18-loader.py build --like native-btsetup-1118-loader --artifact kernel-local-amp-run-1119-9g-artifact --wifi-ios --wifi-args-adt --with-adsp --audio-spf --audio-tdm-pcm --audio-bottom-fw work/adsp-battery/android-cs35l41-1118-2334/odm/firmware/cs35l41-dsp1-spk-prot.wmfw --haptic 5 --haptic-rtp work/adsp-battery/android-haptic-waves-1118-2352/0_click_P_RTP.bin --haptic-pcm --inject-media --product-haptics --haptic-envelope --haptic-amp 128 --hal-haptics --wifi-audit --wifi-password-prompt --wifi-sb-audit --wifi-sb-refresh --wifi-rssi-bounds --wifi-ux 2047 --stderr-log mediaserverd --rtc-calibration <本輪 RTC json> --audio-bottom-full --audio-bottom-cal work/adsp-battery/android-cirrus-cal-1118-2348 --audio-bottom-run --name native-amp-run-muted-1119-loader`。boot SHA-256 為 `6b6c41f2de97cda776ce4a8b913e64a2c5ccaa64c193915c71300eb4855d9348`。
  - 上機：`work/statusbar-fix/launch-loader-check.ps1 -Loader native-amp-run-muted-1119-loader -Kernel kernel-local-amp-run-1119-9g-artifact -Out native-hw000-amp-run-muted-1119 -Seconds 80 -Reset 2`。
  - 成功判據為 `protection running confirmed halo_state=2` 與 `protection core stopped` 各一行，且沒有 `protection run refused`。
  - 第二個樣本是 17:01 K-033 長按重驗的同一趟（`native-hw000-amp-run-hap-1119/boot.log` 第 474、1131–1140 行）：結果與第一次相同。OTPID 8、98 個 trim，2 ms 進 RUN，PAUSE 確認，CSPL_STATE 0，CAL_R `0x241e`，核心停止後 IRQ1 也相同，PCM 同趟 ok。17:06:39 回 Android 並釋鎖。
- 參考的 Xiaomi 原始碼已從暫存位置複製到 `work/adsp-battery/ref-xiaomi-cirrus-92a9fde479/`，含 `SOURCE.txt` 與雜湊；只供對照，不編譯也不匯出。

- 11-19 16:03 的本機 9g 核心另以 mode 5 驗證 CSPL。先讀實機 UNPACK24 XM 的 23 個目錄字，確認韌體 0x400a4 與演算法 0xcd 版本 0x1d3f01、XM／YM 起點、長度與結尾。依該裝置原廠 B 保護檔寫後讀回 4,200 bytes packed YM 與 4 bytes unpacked YM。另依同機 `crus_calr.bin` 的第二個 u32 與 `.txt` 的 B 聲道比對，以 `CAL_R`、`CAL_CHECKSUM=CAL_R+1`、`CAL_STATUS=1` 順序寫後讀回。GLOBAL、AMP、CORE 的 enable 始終為 0，結束已重置功放，16:06:25 已回 Android。上機證據為 `native-hw000-av-cal-muted-1119/boot.log` 第 1088–1089、1142–1143 行。
- 校正趟使用 `kernel-local-amp-cal-1119-9g-artifact`（本機 40 個 workflow 測試通過），沿用完整韌體的 loader 指令加 `--audio-bottom-cal work/adsp-battery/android-cirrus-cal-1118-2348 --name native-av-cal-muted-1119-loader`。用同一 launcher，`-Kernel kernel-local-amp-cal-1119-9g-artifact -Loader native-av-cal-muted-1119-loader -Out native-hw000-av-cal-muted-1119 -Seconds 80 -Reset 2`。對照 LineageOS/android_kernel_xiaomi_sm8550-modules 提交 `92a9fde479d3262da080cfd28bab0775b0da500a` 內 `cs35l41/wm_adsp.c` 的演算法與係數定位，以及 `cs35l41/cs35l41-cal.c` 的保護校正流程。喇叭校正韌體才寫 AMBIENT。保護程式執行已於 11-19 16:57 以 mode 6 在靜音下驗證（見本條開頭）；B-music 參數與最低音量聲音仍待實測。

- 11-19 15:45，在確認 GLOBAL_EN=0 後，依 Linux CS35L41 B2 errata 與 register_errata_patch 清 PWR_CTRL2 及 CCM_CORE_CTRL，三項讀回皆為零。64-byte XM burst 首次讀回即不符並重置下方功放。15:49 換成每四個 DSP word 一組的 12-byte burst，兩段 XM 的 600 bytes 全部吻合。同 SM8550 的 Xiaomi 模組 `wm_adsp.c` 對 packed XM／YM 使用 12 bytes、PM 使用 20 bytes。I²C 沿用 UniXNU-sheng GeniI2c，sheng 的 CS35L43 暫存器不適用。證據見趟次表的 b2-muted 與 xm12。
- 11-19 15:53，原廠保護韌體的 XM 四段 72、528、1,596、1,260 bytes，YM 6,000 bytes，PM 20,420 bytes，逐組寫後讀回均相符。功放及 DSP 保持停用，結束後復位並還原腳位與時脈。完整紀錄 `work/Codex-Xiaomi13-GPU-20261109/native-hw000-av-full-muted-1119/boot.log` 第 463、1061–1062 行。原廠 WMFW SHA-256 為 `c11cb3e25825c673a955a3519370ad77fbcd8bcf68808e8e34140a70caefcec4`。此趟只驗 RAM 內容。
- 復現使用本機 `work/localbuild/build-kernel.ps1 -Ram 9g -Name kernel-local-amp-full-1119-9g-artifact -Ath12k work/adsp-battery/ath12k-objects-b2876c6 -Tests`。WSL 執行 `python3 work/adsp-battery/test-muted-cs35l41.py` 驗讀回、錯位與 215 個故障點。loader 產生器沿用 PCM 配置，增加 `--audio-bottom-full --audio-bottom-fw work/adsp-battery/android-cs35l41-1118-2334/odm/firmware/cs35l41-dsp1-spk-prot.wmfw`。僅此配置擴大 v1 ADT 上限至 64 KiB，實際 44,524 bytes，保留 v1 的 root extents／608-byte 命令列。上機指令為 `work/statusbar-fix/launch-loader-check.ps1 -Loader native-av-full-muted-1119-loader -Kernel kernel-local-amp-full-1119-9g-artifact -Out native-hw000-av-full-muted-1119 -Seconds 80 -Reset 2`。

- **日期**：2026-11-18。
- **狀態**：21:25 實機建圖六步全部 `status=0`；22:02 PCM（模式 5）實機通過，靜音 80 ms 各步驟 `status=0`、`probe complete result=ok`。11-19 16:57／17:01 mode 6 靜音保護試跑兩趟通過；18:04 mode 7 下方喇叭 `-30 dB` 1 秒 2 kHz 短測實機通過（`pup 1 ms`、PLL 鎖定、`48000 frames`、`stray=0`、`checks=10` 全零故障、`down ok`、腳位還原、`PCM probe complete result=ok`）。耳聽確認、上方喇叭／較高音量（需另獲同意）與 iOS 即時音訊 HAL／`mediaserverd` 輸出仍在後面。
- 11-19 14:50 下方功放靜音測試（本機 bt5-txmask 9g）。CS35L41 B2 回覆，IRQ1_STATUS3 為 `0x20000000`，STATUS4 為 `0x3`；舊驅動要求 STATUS3 全零，因此未進 XM 寫入。腳位和時脈投票已復原，ADSP 靜音 PCM 完成。實機證據在 `work/adsp-battery/fuxilog/20261119-145858/console.txt` 第 465、466、861 行。Linux v6.12 的 cs35l41.c 只檢查 STATUS3 的 OTP_BOOT_ERR 位元；對應更動保留識別、OTP 完成與關閉輸出檢查。
- 11-19 15:21 OTP 條件修正後上機。以 `work/localbuild/build-kernel.ps1 -Ram 9g -Name kernel-local-amp-otp-survey-1119-9g-artifact -Ath12k work/adsp-battery/ath12k-objects-b2876c6 -Tests` 建置，40 個主機命令通過；主機 haptic survey 模型同步到 RTP 模式及現行 SPMI 鎖序，並覆蓋波形缺漏、長度不足、HAL 模式與過高 VMAX。實際 B2 通過 OTP，接著讀到 PWR_CTRL1=0、PWR_CTRL2=0x3321、CCM_CORE_CTRL=0x101，後兩者的 enable 位元仍置位。驅動在第一筆 XM 寫入前停止，verified=0、下方功放回 reset、腳位和投票還原；同趟 ADSP 靜音 PCM 正常。來源 `native-hw000bt5-cycle-av-otp-1119/boot.log` 第 476–479、874 行。Linux 參考 `work/adsp-battery/ref-linux/cs35l41-lib.c` 的 B2 errata 表與 cs35l41_register_errata_patch；下次需先在靜音下逐項驗證初始化及讀回。
- **HALO 韌體盤點（11-19 01:15）**：`inspect-cs35l41-halo.py` 依 Linux v6.12 `wmfw.h`、`cs_dsp.c` 解析實機下方喇叭的保護和校正檔。兩組都含六個記憶體區段、三個演算法和兩個係數區段。保護、校正係數均指向 CSPL `0xcd`。從韌體確認 `HALO_STATE`、`CSPL_STATE`、`CAL_R`、`CAL_STATUS`、`CAL_CHECKSUM` 各為 4 bytes，offset 相對 DSP 上回讀的演算法基址。此項是離線格式驗證。真機 DSP 載入、狀態、校正讀回和最低音量試播均待完成。
- **開關**：`--audio-tdm-graph`。寫入 ADT `fuxi,audio-spf=4`，預設停用；`--audio-tdm-pcm` 為模式 5。
- **問題／現象**：目前 ADSP 已能回報 SPF 就緒，喇叭與播放圖仍待驗證。
- **根因**：現行 owner 只有 SPF 與 PRM 探測。早期 Fuxi 已有適用的 secondary TDM 建圖程式，採 48 kHz、4 個 32-bit slot、SD1、反相 BCLK。原機 PAL 的 2 聲道／24-bit 是音訊後端的媒體格式。
- **做法**：沿用 `288179c` 的建圖封包與 `5efc5e2` 的 TDM 設定。SPF 就緒後依序建立及設定 speaker、playback 子圖，再反序關閉。任一步失敗就進清理，包括 OPEN 排入後遺失回覆。此模式只走 APM 的 OPEN、SET_CFG、CLOSE。
  - SPF 就緒時也收一份 `apps_smmu` 快照。沿用目前 WLAN 驅動的 SMR、context bank 寄存器配置及 XNU 的 MMIO 例外處理。從唯讀映射辨識音訊 SID `0x1001/0x80`、`0x1061/0`、WLAN SID `0x1401`、未用 SMR 和無引用的 bank。列出的 bank 還需在實際配置時重新核對。
- **復現**：在 ksrc 執行 `python3 tools/sync-fuxi-audio.py` 和 `python3 tools/sync-fuxi-adsp.py`，同步 0113 和 0117 到 harness 差分。使用 K-077 的核心建置命令，將 `-Name` 改為 `kernel-local-bt5-tdm-graph-1118-9g-artifact`。其 loader 指令換上述 artifact 與新名稱，將 `--audio-prm` 換成 `--audio-tdm-graph`。上機使用本輪 RTC 校時，沿用 K-077 的 dylib 及驗收時間。成功須有 SPF state 1 和 `FUXI AUDIO TDM:` 六步各一筆 `status=0`，末行 `probe complete result=ok`。
  - 20:06 後使用 `kernel-local-bt5-tdm-smmu-1118-9g-artifact`。同趟收 `FUXI AUDIO DMA: apps_smmu survey=0`、兩個 SID 的 `matches` 與 `WLAN-slot`。對照 WLAN console 的 SMR／bank。送 PCM 前需取得音訊專用 DMA 映射和成功的 PRM 時脈回覆。
- **主機測試**：舊版及新版 encoder 在相同 GPR port/token 下的六筆封包逐位元相同。模擬 DSP 測試涵蓋回覆路由、token、opcode、設定編碼與清理。
  - SMMU 模型覆蓋 mask 交集、已被引用或啟用的 bank、在用 ASID、唯讀失敗及越界 IDR 配置。
- **驗證紀錄**：9g 本機建置 62 秒，40 項 workflow 測試通過，核心 SHA-256 `3078f748fb8d94ea3a92d6afe1d6aea2b8a9b24e664f538d6e87e875fbcb17d2`。證據在 `work/Codex-Xiaomi13-GPU-20261109/kernel-local-bt5-tdm-graph-1118-9g-artifact/` 的 `BUILD.txt`、`SHA256SUMS` 和 `logs/test-25.log`。`native-bt5-tdm-offline-1118-loader` 離線封裝的 boot SHA-256 為 `68cb5775708f1b0a21a737c38852f8420283224fb7debe0cf9890582c456bc5a`。
  - 加入唯讀盤點的候選同樣建置 62 秒、40 項通過。核心 SHA-256 `2a8ed7596cb09c0abae9adc40f1a110d51c779ad54d419f1e28683f981b70f02`。`native-bt5-tdm-smmu-offline-1118-loader` boot SHA-256 `84224c296acfdc090dd630d82fb0835638e1b798601f0dd01cc10d600f7b6e52`。完整主機紀錄在新核心產物的 `logs/test-25.log`。
- **參考來源**：本機 `work/xiaomi13-ios-gpu-20261108/ios-fuxi-native-work` 的程式與 `docs-lanes/audio.md`，其中 `a-play2` 送出 PCM，`a-spk1` 由使用者確認下方喇叭播出 iOS 提示音。原始 console 在當時的研究紀錄。 [AudioReach TDM API](https://github.com/AudioReach/audioreach-graphservices/blob/master/spf/api/modules/pcm_tdm_api.h)
  - 音訊 SID 從 Fuxi 原機 DT 的 `qcom,msm-audio-ion` 取得。現行 9g 由 `0x8c0000000` 開始，歷史 `0xd0000000` 位於這段 RAM 之外。原機配置的 IOVA 範圍為 `0x10000000–0x1fffffff`，可供對照。 [Linux SM8550 裝置樹](https://github.com/torvalds/linux/blob/v6.12/arch/arm64/boot/dts/qcom/sm8550.dtsi#L4421) · [Linux ARM SMMU stream 判重](https://github.com/torvalds/linux/blob/v6.12/drivers/iommu/arm/arm-smmu/arm-smmu.c#L978)
- **第一趟實機（21:20–21:31）**：與 K-077 同趟（loader `native-bt5-tdm-smmu-1118-2125-loader`）。SPF 第一次回 remote error 1、重問後 `state 1 after 2 queries`；`FUXI AUDIO TDM:` open／configure speaker、open／configure playback、close playback／speaker 六步皆 `status=0`，末行 `probe complete result=ok`。SMMU 盤點：`SMRs=105 banks=70 blank=96 first=9/10 WLAN-slot=8 S2CR=0x6`，SID `0x1001/0x80` 與 `0x1061` 都是 `matches=0`（`sCR0` 的 USFCFG 已設，未匹配的 stream 會 fault）。功放維持 `fuxi,amp=0`。
- **PCM 前置核對（21:47）**：`--audio-tdm-pcm` 的 64 KB 緩衝區在 loader BSS，`loader-audio-dma.c` 要求實體位址小於 4 GB，在 9g XNU RAM（`0x8c0000000` 起）之外；loader 在 XNU 啟動前用兩個空 SMR 指到已停用 stage 1 的 bank，核心 `fuxi_audio_dma_open` 再核對路由與 bypass 才映射。WLAN 的 `wl_smmu_probe` 只挑無效 SMR 並避開被引用的 bank，SID `0x1401` 與音訊 SMR 不重疊，兩者可共存。
- **第二趟實機 PCM（21:59–22:11）**：與 K-077 第二趟同趟（loader `native-bt5-native-pcm-hal-1118-2201-loader`，核心 `kernel-local-bt5-framing-1118-9g-artifact`，本機編）。SPF `state 1 after 2 queries` 後出現 `FUXI AUDIO PCM: loader DMA region pa=0xa8078000 bytes=0x10000 stream=1`；LPASS macro、digital codec、TDM clock 啟用，speaker／playback open、configure，shared memory input、map PCM buffer、prepare、start、`write 80 ms of silent PCM`、stop、close、unmap 與三項釋放全部 `status=0`，末行 `FUXI AUDIO PCM: probe complete result=ok`。同趟 WLAN 正常，沒有 SMMU fault 或 ADSP SSR；功放維持 `fuxi,amp=0`。console：`work/Codex-Xiaomi13-GPU-20261109/native-hw000bt5-native-pcm-hal-1118-2202/boot.log` 第 718–866 行。
- **結論／未完成**：建圖與 80 ms 靜音 DMA 已實機通過。目前 HAL 的 Speaker WriteMix 只累加 frame 數，仍需接到 AudioReach。K-016 已逐檔驗過本機功放檔案，開啟喇叭另需保護與個別校正。
- **出處**：`ksrc/xnu/fuxi-audio/fuxi_audioreach.{c,h}`、`ksrc/xnu/fuxi-adsp/fuxi_audio_spf.c`、`fuxi_audio_smmu.h` 和 `ksrc/tools/test-fuxi-audio-spf.py`。

## 其他核心項目

### K-070 NativeAdreno host-import 配額失敗回報 Vulkan OOM
- **日期**：2026-11-17 08:58（566aaa9）
- **狀態**：已驗證
- **開關**：無（Turnip／mesa 補丁）
- **問題／現象**：K-057 那趟，Safari 的 WebKit 大貼圖（`b3a8` 3001×1500、`w30r`）被 backboardd 的 Metal 後端拒收，gate 判成 runtime-failure，backboardd 以 CPU 重生。
- **根因**：host-import 配額不足時，回的錯誤碼不是 Vulkan 的 OOM，上層就當成致命失敗，不會走降級路徑。
- **做法**：`ksrc\tools\patches\mesa-fuxi-hostimport.patch`：BoImport 因配額失敗時回 `VK_ERROR_OUT_OF_DEVICE_MEMORY`，其他失敗仍回 `VK_ERROR_INVALID_EXTERNAL_HANDLE`。
- **復現**：依 G 系列的 Turnip 建置流程套這個補丁；上機用 `launch-loader-check.ps1 -TurnipHybrid <檔> -TurnipSha <SHA256>`。
- **主機測試**：`test-fuxi-turnip-hostimport.py`（核心端 HostImport user client 另有 `test-fuxi-hostimport.py`，屬 G 系列）。
- **驗證紀錄**：`native-hw000wlan-hybimport-safari-boot-1`，09:32–09:43:42：寫 11 個區塊（PRE_OK／WRITE_OK）安裝獨立 hybrid，09:35:24 開機，沿用本機 akswrap 核心。
  - 1200 列裁切圖 5184000 bytes、9.2 秒、0 次重連，親眼看是 Safari 分頁畫面。
  - USB SHA-256 `84e6cac532241cd8c135802644e474c42b40263aa86131dbe9088a5a9cf7139a`；CPU 短峰 83.6°C。
  - boot#56 持久紀錄 `fuxilog\20261117-094627`。
- **參考來源**：Vulkan 規格 `vkAllocateMemory` 的錯誤碼；mesa Turnip。
- **結論／未完成**：G 系列有對應項目（GPU 線）。
- **出處**：journal `native-hw000wlan-hybimport-safari-boot-1`。

### K-071 指定行程的有界取樣（sweetprofname）
- **日期**：2026-11-17 09:53（d10a41d）
- **狀態**：已驗證（診斷工具）
- **開關**：
  - boot arg `sweetprofname=<行程名>`；loader 產生器 `--profile <名> --profile-delay <秒>`。
  - 既有的 `sweetsample=<名,名…>`（`--sample`）做多行程堆疊取樣。
- **問題／現象**：要分析特定 userland 行程的 CPU 熱點，例如 coreduetd 57% CPU、locationd 卡死。
- **根因**：（工具）
- **做法**：`ksrc\xnu\fuxi-prof\sweet_prof.c`（補丁 `0111-fuxi-user-profiler.patch`）只對指定名稱的行程做有界取樣。
- **復現**：在 loader 加 `--profile <行程名>`，上機後在 console 找取樣輸出。
- **主機測試**：`test-fuxi-prof.py`；同步工具 `sync-fuxi-prof.py`。
- **驗證紀錄**：K-059 那趟用 `sweetsample=Preferences,locationd,wifid` 找到 locationd 卡在 FairPlay MIG。
- **參考來源**：0111 原有的 `sweetsample` 取樣器。
- **結論／未完成**：無。
- **出處**：ksrc d10a41d。

### K-072 ksrc 基底與維護
- **日期**：2026-11-14 19:45（75148d2）、2026-11-15 00:06（f1a0846）
- **狀態**：維護
- **開關**：無
- **問題／現象**：ksrc 是讓補丁可以用 git 管理的展開樹。
- **根因**：（維護）
- **做法**：
  - 75148d2：基底 zgl97/ios-fuxi `979e2f08` 加上當時 harness 的全部補丁（harness HEAD `2aae087`）。
  - f1a0846：0106 讀取的韌體分區變多之後，重產 0111（只有 hunk 位移）。
  - 3277d2c 屬 GPU 線，見 G 系列。
  - WLAN M5～W17、ADSP 早期與 UniXNU 移植的上游脈絡見 S／E 系列。
  - 從 ksrc 根目錄執行下表的 `python3 tools/<腳本>`。補丁編號對應 `xnu/patches/` 的檔名前綴。

  | 同步腳本 | 重產或校驗的補丁 | 主要輸入 |
  | --- | --- | --- |
  | `sync-abi-backend.py` | 0098 的 NativeAdreno ABI 後端區塊 | NativeAdreno probe／後端 |
  | `sync-fuxi-adreno-shared.py` | 0096、0098，並核對 0100 的連帶變更 | Adreno 共用 header 與 Metal 同步腳本 |
  | `sync-fuxi-adsp.py` | 0117 | ADSP、SweetBattery、電池 TTL、音訊準備與共用電源原始碼 |
  | `sync-fuxi-audio.py` | 0113 | 音訊 GPR 與 RSC 仲裁器 |
  | `sync-fuxi-backlight.py` | 0109 | 背光服務與 DSI 控制 |
  | `sync-fuxi-h1.py` | 0103 | H1 相容層、按鍵與 RTC |
  | `sync-fuxi-idle.py` | 0105 | Idle／IPI 診斷 |
  | `sync-fuxi-metal.py` | 0096、0098 | GPU probe、NativeAdreno ABI 與 XNU 接線 |
  | `sync-fuxi-metal2.py` | 0100，讀取 0098 以核對相依關係 | execswap、handle index 與 user client |
  | `sync-fuxi-pac.py` | 0114 | PAC 自我測試 |
  | `sync-fuxi-perf.py` | 0097 | 觸控／編譯調頻與排程 |
  | `sync-fuxi-prof.py` | 0111 | 使用者空間剖析器 |
  | `sync-fuxi-regtrace.py` | 0112 | IORegistry 與 Work-Interval 追蹤 |
  | `sync-fuxi-scanout.py` | 0108 | SweetSurface、顯示觸發和 Scanout |
  | `sync-fuxi-sys.py` | 0107 | P-SYS 服務與路徑政策 |
  | `sync-fuxi-unixnu.py` | 0106 | UniXNU 移植、mtdblk 紀錄與顯示 |
  | `sync-fuxi-wifi-ios.py` | 0104 | Apple80211／IONetworkStack 與 station 模型 |
  | `sync-fuxi-wlan.py` | 0099 | WLAN 核心 C／include，含 RPMh 與 ath12k 接線 |

- **復現**：在 WSL 的 ksrc 樹改原始碼，跑對應腳本重產補丁，再加 `--check` 重跑並檢視該份補丁差異。將原始碼與補丁一併提交到 ksrc，再依總索引共用步驟 2，以 `eafc778..HEAD` 重產 harness 的外層 `kernel-adsp-battery.patch`。使用 WSL 可讓未指定 newline 的產生器輸出 LF。
- **主機測試**：同步腳本用 `--check` 核對內容。各硬體主機模型依對應 S／K 項目，workflow 套補丁順序見 T-059。
- **驗證紀錄**：無。
- **參考來源**：無。
- **結論／未完成**：無。
- **出處**：ksrc `git log` 與 `tools/sync-*.py`，以各腳本的 patch 路徑與執行分支核對。
