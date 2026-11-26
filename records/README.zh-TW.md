# 修復紀錄庫

小米 13（fuxi，SM8550）跑 iOS 16 的所有修復、實驗、開關、結論、工具與備份，統一記在這裡。每一項都有固定編號，內容寫到別人照著做能復現：改了什麼、怎麼開、怎麼建置、怎麼上機、看哪一行算成功、在哪一趟驗過。

## 怎麼找

- 想知道某個功能怎麼做的：看下面的總索引，點編號進 `items/`。
- 手上有提交、補丁、開機參數、ADT 屬性、測試或追蹤檔案時，可依五份對照表查到項目。 [提交](catalog/commits.zh-TW.md) · [補丁](catalog/patches.zh-TW.md) · [開關](catalog/switches.zh-TW.md) · [主機測試](catalog/tests.zh-TW.md) · [檔案](catalog/files.zh-TW.md)
- 想看當時的完整過程：`journal/` 收的是依時間寫的原始工作紀錄（逐趟上機、逐版數字），紀錄項目的「出處」會指到其中的段落。

| 位置 | 內容 |
|---|---|
| `items/S-upstream.zh-TW.md` | 從 ios-sweet、ios-fuxi 繼承的核心補丁（0001–0116），每份補丁一列 |
| `items/E-early.zh-TW.md` | 早期開機與環境（2026-11-07～11-14）：fastboot loader、root 映像、開機失敗與修法 |
| `items/G-gpu.zh-TW.md` | GPU／顯示流暢度線（2026-11-08～11-15）：NativeMetal、native-layout 各版、混合式驅動 |
| `items/K-kernel.zh-TW.md` | 核心硬體適配（000 線）：ADSP 電池、背光、手電筒、震動、RAM、RTC、Wi-Fi、藍牙、音訊 |
| `items/U-userland.zh-TW.md` | iOS root 與使用者空間：`libfuxi-sb.dylib`、狀態欄、App 安裝、launchd 調整 |
| `items/B-boot.zh-TW.md` | 開機鏈、部署、雙系統選單、loader 安全核對 |
| `items/R-backups.zh-TW.md` | 備份與還原點：分區 dump、iOS root 還原映像、歸檔位置與還原步驟 |
| `items/T-tools.zh-TW.md` | 工具、建置、CI 與流程規則 |
| `catalog/` | 產生的對照表：提交、補丁、開關、主機測試、檔案 |
| [journal/](journal/README.zh-TW.md) | 依時間查工作紀錄、去識別化副本與原始附件位置 |
| 工作區 `map.tsv` | 「物件 → 紀錄編號」的對應規則，產生器用 |
| 工作區 `tools/build-catalog.py` | 產生 `catalog/` 與本頁總索引，`--check` 唯讀檢查覆蓋與日誌副本 |

## 編號規則

- `<系列>-<三位數>`，系列見上表。編號一經使用不改、不重用；項目被取代時把狀態改成「取代（見 X-NNN）」，不刪除。
- 每一項是 `items/` 裡的一個 `### <編號> <名稱>` 小節，欄位依序是：
  - **日期**：開始與結論時間（台北時間）。
  - **狀態**：採用／實驗／停損／取代／未完成／上游。
  - **開關**：開機參數或 ADT 屬性與預設值；沒有開關寫「無（常駐）」。
  - **問題／現象**、**根因**、**做法**（檔案、補丁、函式、提交）。
  - **復現**：建置、產生 loader、上機的完整指令與預期 console 行。
  - **主機測試**、**驗證紀錄**（趟次、loader 名、CI run、紀錄目錄）。
  - **參考來源**、**結論／未完成**、**出處**（`journal/` 或原始檔的段落）。
- 紀錄裡不寫 adb 序號、Wi-Fi 名稱或密碼、使用者目錄、區網 IP；分別寫成 `<序號>`、`<SSID>`、`%USERPROFILE%`、`<區網IP>`。

## 共用步驟

各項的「復現」只寫和這裡不同的部分。「見 README 共用步驟 1–3」就是下面 1–3：取得 ksrc、依序套補丁、建置核心。步驟 1–3 由 `build-kernel.ps1` 一次做完，分開寫是為了說清楚每份補丁在哪一層生效。

1. **取得核心原始碼（ksrc）**：工作區的 `work\adsp-battery\ksrc` 是 zgl97/ios-fuxi 的 worktree，基底 `979e2f08c704677bb75934ae96d4686ba9d1692b`（2026-11-06），其後是本機提交。從零取得與 CI 相同：
   ```bash
   git init source
   git -C source remote add origin https://github.com/zgl97/ios-fuxi.git
   git -C source fetch --no-tags --depth=1 origin 979e2f08c704677bb75934ae96d4686ba9d1692b
   git -C source checkout --detach FETCH_HEAD
   ```
   公開倉庫 fuxi-ios 的 clone 依 `upstream\EXPORT-SOURCES.txt` 的 `kernel-base` 從倉庫歷史取，不連上游。
2. **依序套補丁**：分兩層。
   - **harness 層（改 ios-fuxi 樹）**：依 harness `.github\workflows\kernel-adsp-battery.yml` 的 `paths` 順序，每份先 `git -C source apply --check` 再 `git -C source apply`，共 18 份：`kernel-surface-stride`、`bo-slots`、`surfrow-mode2`、`perf-governor`、`ib-profile`、`perf-wide`、`prof-live`、`prof-inclusive`、`swipe-pacing`、`prof-inclusive-wait`、`prof-edges`、`taps-presets`、`input-fixes`、`taps-page2`、`ibprof-draws`、`taps-page1`、`backlight-dsi`、`adsp-battery`（皆在 `harness\patches\kernel-*.patch`）。前 17 份在 ksrc 裡就是 `979e2f08..eafc778` 這 4 個提交（`75148d2` 一次併入 harness `2aae087` 當時的 14 份，`3277d2c`、`6fc377f`、`eafc778` 依序補 `ibprof-draws`、`taps-page1`、`backlight-dsi`）；`kernel-adsp-battery.patch` 是 `eafc778..HEAD` 的差分，重產指令 `git -c core.autocrlf=false -C work\adsp-battery\ksrc diff --no-color eafc778 HEAD > work\adsp-battery\harness\patches\kernel-adsp-battery.patch`。改 canonical 原始碼後先在 WSL 跑 `tools/sync-fuxi-*.py`（Windows Python 會寫成 CRLF），再提交、重產。
   - **XNU 層（改 Apple XNU 原始碼）**：`tools/ci/build.sh` 取 blacktop/darwin-xnu-build `663906528d1cd5ea8fde7de6f16de73db9511549`（釘選在 `tools/ci/pins.env`），`tools/ci/prepare-kernel.py` 把 `xnu/patches/` 的 `*.patch` 依檔名排序複製進 builder 的 `patches/`，每份旁邊放 `.fuxi-required`（套不上就停）。`FUXI_RAM_LAYOUT=9g` 時再把 `xnu/variants/ram9g.patch` 放成 `0119-fuxi-memory-9g.patch`；設 `FUXI_ATH12K_OBJECT` 時把 `xnu/variants/ath12k-link.patch` 放成 `0120-fuxi-ath12k-link.patch`。S-001～S-116 與 K 系列 0117 以後的補丁都在這一層生效。
3. **建置核心**：
   - 本機（WSL）：`work\localbuild\build-kernel.ps1 -Ram 9g -Name kernel-local-<名稱>-9g-artifact`（`-Ram full` 是 5g 配置；`-Tests` 依 workflow 順序穿插跑主機測試；`-Ath12k <資料夾>` 連結 ath12k 物件；`-Clean` 從零重編）。產物在 `work\Codex-Xiaomi13-GPU-20261109\<名稱>\`，`BUILD.txt` 第一行 `result ok` 才能用，失敗先看 `errors.txt`。
   - CI：harness 的 `kernel-adsp-battery.yml`（5g）與 `kernel-adsp-ram9g.yml`（9g），建置步驟是 `bash tools/ci/build.sh kernel-metal4`。主機測試命令在同一份 workflow 的「Fetch pinned public kernel source and apply patches」步驟。
   - 本機與 CI 產物不逐位元相同（本機上游 clang、CI Apple clang）；紀錄裡寫明是哪一種。
4. **建 `libfuxi-sb.dylib`（本機 WSL）**：`work\localbuild\build-sb-shape.ps1 -Name sb-local-<名稱>-artifact`。
5. **產生 loader**：`python -X utf8 work\adsp-battery\prepare-wlan-w18-loader.py build --like <已驗 loader> --artifact <核心產物> --name <新 loader> <要改的旗標>`；只改 RAM／核心時用 `prepare-ram9g-loader.py`。校時加 `--rtc-calibration <rtc-calibration.py capture 的 json>`（一小時內有效）。
6. **上機**：`work\statusbar-fix\launch-loader-check.ps1 -Loader <loader 名> -Kernel <核心產物名> -Seconds <預期時間+30> -Reset <分鐘> -Out <趟次名>`。`-Loader`／`-Kernel` 都是 `work\Codex-Xiaomi13-GPU-20261109\` 底下的資料夾名；`-Reset`（分鐘）要比 `-Seconds` 長，預設 6。腳本先檢查三個鎖檔（`work\PHONE-LOCK-000`、`work\hybrid-driver\HOLD-PHONE`、`work\adsp-battery\hw000\HOLD`）、拉回上一趟 mtd 紀錄，跑 `verify-loader-safety.py` 驗 loader，再 fastboot 暫時開機（`boot-exp-v.ps1`→`run-monitor.ps1`），結束後回 Android。要放 dylib 時加 `-Dylib <檔> -DylibSha <SHA256>`；要最後一張畫面加 `-FinalFramebuffer -FramebufferHeight 1200`。不要加 `*>`／`2>&1` 轉向（PowerShell 5.1 會把 adb 進度當致命錯誤）。
7. **看結果**：只查該項「預期 console 行」寫的幾行；原始 console 與 mtd 紀錄在 `-Out` 指定的資料夾。

## 總索引

<!-- INDEX:BEGIN -->

共 443 項紀錄；目錄涵蓋 408 筆提交、267 份補丁、286 個開關、226 份測試、744 個檔案。

### S：上游繼承補丁（iOS 16、XNU 27 與 GPU 建置）（117 項）

| 編號 | 項目 | 狀態 | 開關 |
|---|---|---|---|
| [S-001](items/S-upstream.zh-TW.md#s-001-移除-xcode-16-已刪除的-clang-自動變數初始化旗標) | 移除 Xcode 16 已刪除的 clang 自動變數初始化旗標 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-002](items/S-upstream.zh-TW.md#s-002-為新版-clang-回移植-uio_stackbuf-巨集) | 為新版 clang 回移植 UIO_STACKBUF 巨集 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-003](items/S-upstream.zh-TW.md#s-003-新增-sweet-fbcon-幀緩衝區序列主控台) | 新增 sweet-fbcon 幀緩衝區序列主控台 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-004](items/S-upstream.zh-TW.md#s-004-新增-sweet-機型組態與-gicv3-group-1-中斷路徑) | 新增 SWEET 機型組態與 GICv3 Group 1 中斷路徑 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-005](items/S-upstream.zh-TW.md#s-005-修正無-pac-且無-rosetta-組態下的-pmap-成員重複定義) | 修正無 PAC 且無 Rosetta 組態下的 pmap 成員重複定義 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-006](items/S-upstream.zh-TW.md#s-006-新增使用開源-arm-asmpmap-的-sweet-master-組態檔) | 新增使用開源 arm asm／pmap 的 SWEET MASTER 組態檔 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-007](items/S-upstream.zh-TW.md#s-007-為-apple-內部標頭檔tunablessartuat提供空實作佔位並停用-kpckperf) | 為 Apple 內部標頭檔（tunables／SART／UAT）提供空實作佔位並停用 kpc／kperf | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-008](items/S-upstream.zh-TW.md#s-008-補齊-sweet-閉源程式庫缺口與-kpc-空後端) | 補齊 SWEET 閉源程式庫缺口與 kpc 空後端 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-009](items/S-upstream.zh-TW.md#s-009-移除僅限-apple-soc-的-kpi-匯出符號) | 移除僅限 Apple SoC 的 KPI 匯出符號 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-010](items/S-upstream.zh-TW.md#s-010-新增開機早期幀緩衝區除錯標記與早期-panic-主控台) | 新增開機早期幀緩衝區除錯標記與早期 panic 主控台 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-011](items/S-upstream.zh-TW.md#s-011-為大記憶體非-ppl-核心預留-physmap-區塊對齊填充空間) | 為大記憶體非 PPL 核心預留 physmap 區塊對齊填充空間 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-012](items/S-upstream.zh-TW.md#s-012-在-ttbr0-撤除後保留幀緩衝區除錯主控台) | 在 TTBR0 撤除後保留幀緩衝區除錯主控台 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-013](items/S-upstream.zh-TW.md#s-013-在無-corecrypto-下為-kmem-配置器提供每-cpu-亂數產生器) | 在無 corecrypto 下為 kmem 配置器提供每 CPU 亂數產生器 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-014](items/S-upstream.zh-TW.md#s-014-註冊-sweet-內建-hmac-sha256-核心-prng) | 註冊 SWEET 內建 HMAC-SHA256 核心 PRNG | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-015](items/S-upstream.zh-TW.md#s-015-明確呈現-sweet-缺少-trust-cache-服務的狀態) | 明確呈現 SWEET 缺少 trust-cache 服務的狀態 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-016](items/S-upstream.zh-TW.md#s-016-新增-sweet-內建裝置樹-platform-expert) | 新增 SWEET 內建裝置樹 Platform Expert | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-017](items/S-upstream.zh-TW.md#s-017-從裝置樹選擇虛擬計時器-ppi-中斷編號支援-qemu) | 從裝置樹選擇虛擬計時器 PPI 中斷編號（支援 QEMU） | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無（透過 ADT 既有節點屬性 `virtual-timer-intid` 控制，非新增開機參數） |
| [S-018](items/S-upstream.zh-TW.md#s-018-註冊-sweet-開機-cpu-並驗證-gic-sgi-傳遞) | 註冊 SWEET 開機 CPU 並驗證 GIC SGI 傳遞 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-019](items/S-upstream.zh-TW.md#s-019-整合相符的開源-libpthread-核心提供者) | 整合相符的開源 libpthread 核心提供者 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-020](items/S-upstream.zh-TW.md#s-020-整合-bearssl-可攜式常數時間-aes-cbcecb-至傳統加密-api) | 整合 BearSSL 可攜式常數時間 AES CBC/ECB 至傳統加密 API | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-021](items/S-upstream.zh-TW.md#s-021-透過-mockfs-啟動-ram-disk-根檔案系統) | 透過 mockfs 啟動 RAM disk 根檔案系統 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-022](items/S-upstream.zh-TW.md#s-022-允許在無-iostoragefamily--iomedia-下執行-publishhiddenmedia) | 允許在無 IOStorageFamily / IOMedia 下執行 publishHiddenMedia | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-023](items/S-upstream.zh-TW.md#s-023-將-sweet-的-libkern-sha-256-導向內建實作) | 將 SWEET 的 libkern SHA-256 導向內建實作 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-024](items/S-upstream.zh-TW.md#s-024-修正-mockfs-透過裝置-io-讀取虛擬-ram-disk) | 修正 mockfs 透過裝置 I/O 讀取虛擬 RAM disk | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-025](items/S-upstream.zh-TW.md#s-025-註冊-sweet-內建無-entitlements-的-amfi-介面) | 註冊 SWEET 內建無 entitlements 的 AMFI 介面 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-026](items/S-upstream.zh-TW.md#s-026-將-apple-開源-hfs-內建至-sweet-核心) | 將 Apple 開源 HFS 內建至 SWEET 核心 | 上游繼承（ios-sweet）；ksrc：上游原樣 | `hfs_corruption_panics`：開機參數（於 HFS 原始碼／設定中使用；卷宗未記預設值，細節見補丁） |
| [S-027](items/S-upstream.zh-TW.md#s-027-使用-bearssl-提供-md5-以支援-hfs-卷宗-uuid) | 使用 BearSSL 提供 MD5 以支援 HFS 卷宗 UUID | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-028](items/S-upstream.zh-TW.md#s-028-新增-sweet-內部-ufs-唯讀根裝置驅動ufsroot0) | 新增 SWEET 內部 UFS 唯讀根裝置驅動（ufsroot0） | 上游繼承（ios-sweet）；ksrc：上游原樣 | `ufsroot=<分區名>`：開機參數，指定要掛載的 GPT 分區名稱，預設為 `userdata`。 |
| [S-029](items/S-upstream.zh-TW.md#s-029-允許-sweet-在未驗證未密封的根卷宗下繼續開機) | 允許 SWEET 在未驗證／未密封的根卷宗下繼續開機 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-030](items/S-upstream.zh-TW.md#s-030-新增由開機參數-ufsrw1-控制的-ufs-寫入支援) | 新增由開機參數 ufsrw=1 控制的 UFS 寫入支援 | 上游繼承（ios-sweet）；ksrc：上游原樣 | `ufsrw=1`：開機參數，啟用 `sweet_ufs` 寫入功能；預設關閉（未設時拒絕所有寫入）。 |
| [S-031](items/S-upstream.zh-TW.md#s-031-新增開機參數-rootrw1-並允許掛載非日誌髒-hfs-根卷宗) | 新增開機參數 rootrw=1 並允許掛載非日誌髒 HFS 根卷宗 | 上游繼承（ios-sweet）；ksrc：上游原樣 | `rootrw=1`：開機參數，從開機初始即以讀寫模式掛載根檔案系統；預設維持原唯讀流程。 |
| [S-032](items/S-upstream.zh-TW.md#s-032-使用-bearssl-提供傳統-sha1-api-以修復-ipv6-lo0-掛載崩潰) | 使用 BearSSL 提供傳統 SHA1 API 以修復 IPv6 lo0 掛載崩潰 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-033](items/S-upstream.zh-TW.md#s-033-使用-bearssl-提供-sha-384sha-512-api) | 使用 BearSSL 提供 SHA-384／SHA-512 API | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-034](items/S-upstream.zh-TW.md#s-034-保護-ufs-dma-在高記憶體4g下檢查-64-bit-定址能力) | 保護 UFS DMA 在高記憶體（>4G）下檢查 64-bit 定址能力 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-035](items/S-upstream.zh-TW.md#s-035-在-sweet-內建-amfi-中加入-xml-entitlements-剖析與-platform-binary-標記) | 在 SWEET 內建 AMFI 中加入 XML entitlements 剖析與 platform binary 標記 | 上游繼承（ios-sweet）；ksrc：上游原樣 | `sweet_amfi_debug=1`：開機參數（註解於 `sweet_amfi.c` 記載，記錄每個簽章附加的 entitlements 摘要；預設關閉）。 |
| [S-036](items/S-upstream.zh-TW.md#s-036-sweet-amfi-為通過驗證的簽章標記-cs_signed) | SWEET AMFI 為通過驗證的簽章標記 CS_SIGNED | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-037](items/S-upstream.zh-TW.md#s-037-在-sweet-核心組態啟用-bindfs) | 在 SWEET 核心組態啟用 bindfs | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-038](items/S-upstream.zh-TW.md#s-038-新增-sweet-內建-applekeystore無密碼已解鎖與鑰匙圈金鑰封裝) | 新增 SWEET 內建 AppleKeyStore（無密碼已解鎖與鑰匙圈金鑰封裝） | 上游繼承（ios-sweet）；ksrc：本機修改 +100／-1（本機提交 `31241b2`、`7294068`）；本機修改見 K-059 | 無 |
| [S-039](items/S-upstream.zh-TW.md#s-039-將-sweet-的-coredump-預設設為關閉) | 將 SWEET 的 coredump 預設設為關閉 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無（沿用既有 sysctl `kern.coredump`，SWEET 預設改為 `0`，設為 `1` 可重新開啟） |
| [S-040](items/S-upstream.zh-TW.md#s-040-新增-sweet-內建-iowatchdog-服務並上限化-hfs-連結修復紀錄) | 新增 SWEET 內建 IOWatchdog 服務並上限化 HFS 連結修復紀錄 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-041](items/S-upstream.zh-TW.md#s-041-限制-hfs-硬連結修復訊息輸出次數) | 限制 HFS 硬連結修復訊息輸出次數 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-042](items/S-upstream.zh-TW.md#s-042-精簡-sweet-上每次-ios-exec-的-console-紀錄為單行) | 精簡 SWEET 上每次 iOS exec 的 console 紀錄為單行 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-043](items/S-upstream.zh-TW.md#s-043-在-console-印出-sweet-行程崩潰報告並實作-aks-selector-103) | 在 console 印出 SWEET 行程崩潰報告並實作 AKS selector 103 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-044](items/S-upstream.zh-TW.md#s-044-在-console-崩潰報告印出-exit-reason-文字) | 在 console 崩潰報告印出 exit reason 文字 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-045](items/S-upstream.zh-TW.md#s-045-在-iowatchdog-上標記-iopmuprimary-以通過-watchdogd-的-pmu-階段檢查) | 在 IOWatchdog 上標記 IOPMUPrimary 以通過 watchdogd 的 PMU 階段檢查 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-046](items/S-upstream.zh-TW.md#s-046-實作-sweet-內建-iosurface-與-iomobileframebuffercpu-顯示路徑) | 實作 SWEET 內建 IOSurface 與 IOMobileFramebuffer（CPU 顯示路徑） | 上游繼承（ios-sweet）；ksrc：本機修改 +4／-1（本機提交 `75148d2`）；本機修改見 G-019、K-072 | `sweetfb_noconsole=1`：開機參數，於首次 swap 時暫停螢幕文字主控台（改走序列）；預設為 `0`（保留核心文字疊加，以便 panic 仍可在螢幕看到）。 |
| [S-047](items/S-upstream.zh-TW.md#s-047-新增-userdata-尾端持續性-console-紀錄環sweetlog) | 新增 userdata 尾端持續性 console 紀錄環（sweetlog） | 上游繼承（ios-sweet）；ksrc：上游原樣 | `sweetlog=0`：開機參數，停用持續性 console 紀錄寫入；預設啟用。 |
| [S-048](items/S-upstream.zh-TW.md#s-048-將指定-daemon-寫入-devnull-的輸出側錄至-consolesweetnull) | 將指定 daemon 寫入 /dev/null 的輸出側錄至 console（sweetnull） | 上游繼承（ios-sweet）；ksrc：上游原樣 | `sweetnull=<名單>`：開機參數，逗號分隔要側錄 `/dev/null` 輸出的行程名稱；預設 `"backboardd,SpringBoard"`，設為 `"-"` 關閉。 |
| [S-049](items/S-upstream.zh-TW.md#s-049-追蹤指定行程的-iokit-查詢與配對呼叫sweetiolog) | 追蹤指定行程的 IOKit 查詢與配對呼叫（sweetiolog） | 上游繼承（ios-sweet）；ksrc：上游原樣 | `sweetiolog=<名單>`：開機參數，逗號分隔要追蹤 IOKit 查詢的行程名稱；預設 `"backboardd"`（`"-"` 關閉）。 |
| [S-050](items/S-upstream.zh-TW.md#s-050-新增寬鬆的-sweet-內建-sandbox-mac-政策) | 新增寬鬆的 SWEET 內建 Sandbox MAC 政策 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-051](items/S-upstream.zh-TW.md#s-051-行程-sigabrt-崩潰時掃描並印出-malloc-heap-可列印字串sweetscan) | 行程 SIGABRT 崩潰時掃描並印出 malloc heap 可列印字串（sweetscan） | 上游繼承（ios-sweet）；ksrc：上游原樣 | `sweetscan=<名單>`：開機參數，逗號分隔於 `SIGABRT` 時掃描 heap 字串的行程名稱；預設 `"backboardd"`（`"-"` 關閉）。 |
| [S-052](items/S-upstream.zh-TW.md#s-052-在-hfs-上以-vnop_exchange-模擬-rename_swap) | 在 HFS 上以 VNOP_EXCHANGE 模擬 RENAME_SWAP | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-053](items/S-upstream.zh-TW.md#s-053-將-heap-字串掃描擴充至-springboard-與-sigtrap) | 將 heap 字串掃描擴充至 SpringBoard 與 SIGTRAP | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無（修改 `0051` 既有的 `sweetscan` 預設值為 `"backboardd,SpringBoard"`） |
| [S-054](items/S-upstream.zh-TW.md#s-054-新增使用者空間行程抽樣器swap-計數器並追蹤-springboard-iokit) | 新增使用者空間行程抽樣器、swap 計數器並追蹤 SpringBoard IOKit | 上游繼承（ios-sweet）；ksrc：上游原樣 | `sweetsample=<名單>`：開機參數，逗號分隔要定期抽樣各執行緒 user state 的行程名稱；預設 `"SpringBoard,backboardd"`（`"-"` 關閉）。 |
| [S-055](items/S-upstream.zh-TW.md#s-055-實作-aks-selector-35101-並加深行程抽樣器深度與次數) | 實作 AKS selector 35／101 並加深行程抽樣器深度與次數 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-056](items/S-upstream.zh-TW.md#s-056-將-sweetframebuffer-swap-改由-vsync-計時器以-10-hz-節流呈現) | 將 SweetFramebuffer swap 改由 vsync 計時器以 10 Hz 節流呈現 | 上游繼承（ios-sweet）；ksrc：上游原樣 | `sweetfb_hz=<頻率>`：開機參數，設定 vsync 計時器將最新畫面複製到面板的頻率（Hz）；預設 `10` Hz。 |
| [S-057](items/S-upstream.zh-TW.md#s-057-停用外部身分解析器config_ext_resolver使-ios-acl-生效) | 停用外部身分解析器（config_ext_resolver）使 iOS ACL 生效 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-058](items/S-upstream.zh-TW.md#s-058-新增-iohidfamily-19151403-hid-事件服務按鍵與觸控) | 新增 IOHIDFamily-1915.140.3 HID 事件服務（按鍵與觸控） | 上游繼承（ios-sweet）；ksrc：上游原樣 |  |
| [S-059](items/S-upstream.zh-TW.md#s-059-從-adt-讀取-hwmachinehwmodel-以呈現-iphone-x-裝置身分) | 從 ADT 讀取 hw.machine／hw.model 以呈現 iPhone X 裝置身分 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-060](items/S-upstream.zh-TW.md#s-060-新增輪詢式電源音量鍵spmi與觸控geni-i2c驅動) | 新增輪詢式電源／音量鍵（SPMI）與觸控（GENI I2C）驅動 | 上游繼承（ios-sweet）；ksrc：上游原樣 |  |
| [S-061](items/S-upstream.zh-TW.md#s-061-將顯示畫面與觸控座標呈現為-iphone-x1125x2436) | 將顯示畫面與觸控座標呈現為 iPhone X（1125x2436） | 上游繼承（ios-sweet）；ksrc：上游原樣 | `sweetfb_native=1`：開機參數，恢復回報實體面板解析度（1080x2400）；預設關閉（呈現為 1125x2436）。 |
| [S-062](items/S-upstream.zh-TW.md#s-062-新增觸控電源軌-rpmh-投票l11c與-iopmpowersource-電池服務) | 新增觸控電源軌 RPMh 投票（L11C）與 IOPMPowerSource 電池服務 | 上游繼承（ios-sweet）；ksrc：上游原樣 | `sweetbattery=1`：開機參數，每 30 秒透過 `sweet_spmi.c` 唯讀輪詢 PM6150 以電壓估算未校正電量（SOC）；預設關閉（回傳靜態 100% / 外接電源）。 |
| [S-063](items/S-upstream.zh-TW.md#s-063-修正-geni-i2c-fifo-packing-與-gt9896yellowstone協定) | 修正 GENI I2C FIFO packing 與 GT9896（Yellowstone）協定 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-064](items/S-upstream.zh-TW.md#s-064-在-sweet-sandbox-政策中授予-priv_work_interval-權限) | 在 SWEET Sandbox 政策中授予 PRIV_WORK_INTERVAL 權限 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-065](items/S-upstream.zh-TW.md#s-065-以-psci-cpu_on-啟動-a55-次要核心cpus26) | 以 PSCI CPU_ON 啟動 A55 次要核心（cpus=2..6） | 上游繼承（ios-sweet）；ksrc：上游原樣 | `cpus=<N>`：開機參數，啟用最多 `N` 顆 A55 核心（`2..6`）；預設為 `1`（單核）。 |
| [S-066](items/S-upstream.zh-TW.md#s-066-在崩潰報告印出錯誤位址所屬的-vm-映射區段) | 在崩潰報告印出錯誤位址所屬的 VM 映射區段 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-067](items/S-upstream.zh-TW.md#s-067-將-sweetfb_noconsole-改為靜音螢幕主控台旗標而非切換到序列埠) | 將 sweetfb_noconsole 改為靜音螢幕主控台旗標而非切換到序列埠 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無（修改 `0046` 既有的 `sweetfb_noconsole=1` 行為） |
| [S-068](items/S-upstream.zh-TW.md#s-068-重複查詢已映射的-iosurface-時回傳基底位址--1) | 重複查詢已映射的 IOSurface 時回傳基底位址 -1 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-069](items/S-upstream.zh-TW.md#s-069-將-sweetlog-刷寫週期改為-10-秒並新增選配-ufs-快取與等待調校) | 將 sweetlog 刷寫週期改為 10 秒並新增選配 UFS 快取與等待調校 | 上游繼承（ios-sweet）；ksrc：上游原樣 |  |
| [S-070](items/S-upstream.zh-TW.md#s-070-在-sweet-啟用-pid_hibernate-系統呼叫修復-springboard-退回桌面崩潰) | 在 SWEET 啟用 pid_hibernate 系統呼叫（修復 SpringBoard 退回桌面崩潰） | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-071](items/S-upstream.zh-TW.md#s-071-為螢幕邊緣起始的觸控標記-fromedgetip-位元) | 為螢幕邊緣起始的觸控標記 FromEdgeTip 位元 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-072](items/S-upstream.zh-TW.md#s-072-新增唯讀-usb-狀態探測dwc3--qusb2--gcc) | 新增唯讀 USB 狀態探測（DWC3 / QUSB2 / GCC） | 上游繼承（ios-sweet）；ksrc：上游原樣 | `sweetusb=0`：開機參數，停用 USB 探測；預設啟用唯讀探測（`1`）。 |
| [S-073](items/S-upstream.zh-TW.md#s-073-新增-usb-裝置模式與-endpoint-0-列舉sweetusb2) | 新增 USB 裝置模式與 Endpoint 0 列舉（sweetusb=2） | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無（擴充 `0072` 既有 `sweetusb` 開機參數：`0` 關閉、`1` 預設僅探測、`2` 啟動 USB 裝置模式） |
| [S-074](items/S-upstream.zh-TW.md#s-074-在-qualcomm-usb-wrapper-強制啟用-vbus-session-valid) | 在 Qualcomm USB wrapper 強制啟用 VBUS session valid | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-075](items/S-upstream.zh-TW.md#s-075-新增-usb-乙太網路cdc-ecm與-ue0-網路介面1921687224) | 新增 USB 乙太網路（CDC-ECM）與 ue0 網路介面（192.168.7.2/24） | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-076](items/S-upstream.zh-TW.md#s-076-修正-usb-乙太網路收包使用單一-2-kib-cluster-mbuf) | 修正 USB 乙太網路收包使用單一 2 KiB cluster mbuf | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-077](items/S-upstream.zh-TW.md#s-077-透過-usb-乙太網路即時串流-console-logudp-至-192168715140) | 透過 USB 乙太網路即時串流 console log（UDP 至 192.168.7.1:5140） | 上游繼承（ios-sweet）；ksrc：上游原樣 | `sweetusblog=0`：開機參數，停用 USB 即時 console log 串流；預設啟用（搭配 `sweetusb=2`）。 |
| [S-078](items/S-upstream.zh-TW.md#s-078-啟用全部八顆核心六顆-a55--兩顆-a76cpus8) | 啟用全部八顆核心（六顆 A55 ＋ 兩顆 A76，cpus=8） | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無（將 `0065` 既有 `cpus` 參數上限從 6 提高到 8） |
| [S-079](items/S-upstream.zh-TW.md#s-079-實作-iomfb-requestpowerchange0-漸暗息屏與恢復顯示) | 實作 IOMFB RequestPowerChange(0) 漸暗息屏與恢復顯示 | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-080](items/S-upstream.zh-TW.md#s-080-記錄-ipv4-路由與位址變更sweet-net-追蹤) | 記錄 IPv4 路由與位址變更（SWEET NET 追蹤） | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-081](items/S-upstream.zh-TW.md#s-081-讓-ue0-回報作用中的乙太網路連結狀態siocgifmedia-與-kev_dl_link_on) | 讓 ue0 回報作用中的乙太網路連結狀態（SIOCGIFMEDIA 與 KEV_DL_LINK_ON） | 上游繼承（ios-sweet）；ksrc：上游原樣 | 無 |
| [S-082](items/S-upstream.zh-TW.md#s-082-建立小米-13fuxiram-only-開機與顯示移植目標) | 建立小米 13（fuxi）RAM-only 開機與顯示移植目標 | 上游繼承（ios-fuxi）；ksrc：上游原樣 | 無 |
| [S-083](items/S-upstream.zh-TW.md#s-083-獨立原生-usb-監看器診斷通道啟動至-ram-root-pid-1) | 獨立原生 USB 監看器診斷通道（啟動至 RAM-root PID 1） | 上游繼承（ios-fuxi）；ksrc：上游原樣 | 無 |
| [S-084](items/S-upstream.zh-TW.md#s-084-fuxi-唯讀-ufs-交接探測與時脈ahb-驗證fuxi_ufs_probe) | Fuxi 唯讀 UFS 交接探測與時脈／AHB 驗證（fuxi_ufs_probe） | 上游繼承（ios-fuxi）；ksrc：上游原樣 | `fuxi_ufs_probe`：開機參數，啟用 fuxi 時脈驗證的唯讀 UFS 交接測試（`TEST UNIT READY` 與 `READ(10)`，確認 GPT 邊界）；預設狀態見補丁。 |
| [S-085](items/S-upstream.zh-TW.md#s-085-診斷-fuxi-ufs-check-condition-原始回應封包) | 診斷 Fuxi UFS CHECK CONDITION 原始回應封包 | 上游繼承（ios-fuxi）；ksrc：上游原樣 | 無 |
| [S-086](items/S-upstream.zh-TW.md#s-086-在唯讀-io-前發送精確的-ufs-device-wlun-active-電源指令) | 在唯讀 I/O 前發送精確的 UFS device-WLUN ACTIVE 電源指令 | 上游繼承（ios-fuxi）；ksrc：上游原樣 | 無 |
| [S-087](items/S-upstream.zh-TW.md#s-087-當-abl-autorefresh-開啟時略過手動-ctl0-start-以避免-dsc-損毀) | 當 ABL autorefresh 開啟時略過手動 CTL0 START 以避免 DSC 損毀 | 上游繼承（ios-fuxi）；ksrc：上游原樣 | 無 |
| [S-088](items/S-upstream.zh-TW.md#s-088-將-fuxi-ufs-根裝置限制在-userdata-內預先釘選的單一-data-檔案extent-mapped) | 將 Fuxi UFS 根裝置限制在 userdata 內預先釘選的單一 /data 檔案（extent-mapped） | 上游繼承（ios-fuxi）；ksrc：上游原樣 | 無（讀取 ADT `/chosen` 既有屬性 `fuxi-root-extents` 與 `fuxi-root-blocks`） |
| [S-089](items/S-upstream.zh-TW.md#s-089-在-fuxi-上於指定-sweetsample-時啟動使用者空間堆疊抽樣器) | 在 Fuxi 上於指定 sweetsample= 時啟動使用者空間堆疊抽樣器 | 上游繼承（ios-fuxi）；ksrc：上游原樣 | 無（沿用 `0054` 的 `sweetsample=` 開機參數，僅在明確指定時於 Fuxi 啟動） |
| [S-090](items/S-upstream.zh-TW.md#s-090-將-fuxi-smp-上限設為-7-核保留-cpu100-給-usb-監看器) | 將 Fuxi SMP 上限設為 7 核（保留 cpu@100 給 USB 監看器） | 上游繼承（ios-fuxi）；ksrc：上游原樣 | 無（將 fuxi 的 `cpus=` 上限夾在 7 核） |
| [S-091](items/S-upstream.zh-TW.md#s-091-新增-fuxi-st-ftsftm5spi-觸控驅動透過-qup1-se4-gpi-dma) | 新增 Fuxi ST FTS（FTM5）SPI 觸控驅動（透過 QUP1 SE4 GPI DMA） | 上游繼承（ios-fuxi）；ksrc：本機修改 +1／-1（本機提交 `75148d2`）；本機修改見 E-037、G-025、K-072 |  |
| [S-092](items/S-upstream.zh-TW.md#s-092-新增-fuxi-電源音量鍵驅動spmi-arbiter-v7-唯讀輪詢) | 新增 Fuxi 電源／音量鍵驅動（SPMI arbiter v7 唯讀輪詢） | 上游繼承（ios-fuxi）；ksrc：上游原樣 |  |
| [S-093](items/S-upstream.zh-TW.md#s-093-優化-fuxi-60-hz-顯示呈現與-4-ms-自適應觸控輪詢) | 優化 Fuxi 60 Hz 顯示呈現與 4 ms 自適應觸控輪詢 | 上游繼承（ios-fuxi）；ksrc：本機修改 +1／-1（本機提交 `75148d2`）；本機修改見 E-037、G-025、K-072 |  |
| [S-095](items/S-upstream.zh-TW.md#s-095-新增-sweetnoexec-開機參數以拒絕重試循環-daemon-的-exec) | 新增 sweetnoexec= 開機參數以拒絕重試循環 daemon 的 exec | 上游繼承（ios-sweet）；ksrc：上游原樣 | `sweetnoexec=<名單>`：開機參數，逗號分隔要在 `exec` 階段直接拒絕執行的二進位檔名稱（例如 `bluetoothd,fairplayd.H2`）；預設無。 |
| [S-096](items/S-upstream.zh-TW.md#s-096-整合-adreno-a740-gpu-冷啟動pas13-韌體載入與非同步-2d-呈現fuxigpu) | 整合 Adreno A740 GPU 冷啟動、PAS13 韌體載入與非同步 2D 呈現（fuxigpu） | 上游繼承（ios-fuxi）；ksrc：上游原樣 |  |
| [S-097](items/S-upstream.zh-TW.md#s-097-實作-fuxi-觸控編譯-cpu-加速推薦核心遮罩與熱壓力橋接fuxiperffuxitpfuxicores) | 實作 Fuxi 觸控／編譯 CPU 加速、推薦核心遮罩與熱壓力橋接（fuxiperf／fuxitp／fuxicores） | 上游繼承（ios-fuxi）；ksrc：本機修改 +44／-11（本機提交 `75148d2`）；本機修改見 G-018、G-047、K-072 |  |
| [S-098](items/S-upstream.zh-TW.md#s-098-實作-nativeadreno-v2-使用者空間-gpu-abi時脈ddr-dcvs-與掃描輸出最佳化) | 實作 NativeAdreno v2 使用者空間 GPU ABI、時脈／DDR DCVS 與掃描輸出最佳化 | 上游繼承（ios-fuxi）；ksrc：本機修改 +515／-6（本機提交 `75148d2`、`3277d2c`、`eafc778`、`ba65d38`、`7af418a`）；本機修改見 G-014、G-019、G-027、G-048、K-005、K-017、K-018、K-019、K-072 |  |
| [S-099](items/S-upstream.zh-TW.md#s-099-實作-wcn7850-pcie0mhibhi-探索qmi-wlfw-帶起與原生-ath12k-wi-fi-ios-站台橋接fuxiwlan) | 實作 WCN7850 PCIe0/MHI/BHI 探索、QMI WLFW 帶起與原生 ath12k Wi-Fi iOS 站台橋接（fuxiwlan） | 上游繼承（ios-fuxi）；ksrc：本機修改 +2816／-14（本機提交 `93201ca`、`51e7ca9`、`a066a05`、`723e740`、`e82f103`、`86cfb9d`、`42df429`、`15e6d0a`、`62dd9b0`、`d24fc49`、`e575a32`、`4b52021`、`d020ece`、`70470b9`、`54beb9b`、`d3f7631`、`21e2a91`、`8741795`、`69ea46c`、`aa89a3e`、`497a117`、`a23e701`、`490764d`、`a41069f`、`af3c264`）；本機修改見 K-011、K-015、K-024、K-028、K-032、K-037、K-041、K-042、K-043、K-044、K-045、K-049、K-050、K-051、K-052、K-053、K-054、K-055、K-056、K-057、K-058、K-060、K-061、K-062、K-066、K-067、K-068、K-069、K-073 |  |
| [S-100](items/S-upstream.zh-TW.md#s-100-實作受閘控執行路徑代換測試路由與-nativeadreno-abi-效能除錯追蹤sweetexecswap) | 實作受閘控執行路徑代換測試路由與 NativeAdreno ABI 效能／除錯追蹤（sweetexecswap） | 上游繼承（ios-fuxi）；ksrc：上游原樣 |  |
| [S-101](items/S-upstream.zh-TW.md#s-101-修復-4-gib-以上實體位址進行-gpu-bo-快取同步時的截斷問題) | 修復 4 GiB 以上實體位址進行 GPU BO 快取同步時的截斷問題 | 上游繼承（ios-fuxi）；ksrc：上游原樣 | 無 |
| [S-102](items/S-upstream.zh-TW.md#s-102-允許-backboardd-在指定開機參數下開啟-nativeadreno-user-clientfuxibbgpu) | 允許 backboardd 在指定開機參數下開啟 NativeAdreno User Client（fuxibbgpu） | 上游繼承（ios-fuxi）；ksrc：上游原樣 |  |
| [S-103](items/S-upstream.zh-TW.md#s-103-實作-fuxi-h1-amfi-相容層按鍵仲裁注入自動鎖屏ui-腳本導覽與-pmk8550-rtc-校時) | 實作 Fuxi H1 AMFI 相容層、按鍵仲裁／注入／自動鎖屏、UI 腳本導覽與 PMK8550 RTC 校時 | 上游繼承（ios-fuxi）；ksrc：本機修改 +109／-30（本機提交 `75148d2`、`6fc377f`、`7928af8`、`ba65d38`、`aab893e`、`1836f06`、`406f096`）；本機修改見 G-027、G-028、G-048、K-005、K-017、K-018、K-035、K-036、K-072 |  |
| [S-104](items/S-upstream.zh-TW.md#s-104-實作-ios-apple80211ionetworkstack-wi-fi-模擬與原生-wcn7850-站台對接層fuxiwifiios) | 實作 iOS Apple80211／IONetworkStack Wi-Fi 模擬與原生 WCN7850 站台對接層（fuxiwifiios） | 上游繼承（ios-fuxi）；ksrc：本機修改 +557／-37（本機提交 `e575a32`、`54beb9b`、`31241b2`、`21e2a91`、`8741795`、`05ed0fa`、`aa89a3e`、`497a117`、`a23e701`、`490764d`、`a41069f`、`af3c264`）；本機修改見 K-033、K-053、K-057、K-059、K-060、K-061、K-064、K-065、K-066、K-067、K-068、K-069、K-073 |  |
| [S-105](items/S-upstream.zh-TW.md#s-105-實作-cpu-負載ipiidle-l1-快取清理診斷禁用系統睡眠與-xpcproxy-身份保留fuxicpuloadfuxinosleepfuxixpcidfuxinoidleflush) | 實作 CPU 負載／IPI／Idle L1 快取清理診斷、禁用系統睡眠與 xpcproxy 身份保留（fuxicpuload／fuxinosleep／fuxixpcid／fuxinoidleflush） | 上游繼承（ios-fuxi）；ksrc：上游原樣 |  |
| [S-106](items/S-upstream.zh-TW.md#s-106-整合-unixnu-顯示ufs-安全策略120-hz-te-同步掃描輸出追蹤與-mtdblk-獨立紀錄fx_log) | 整合 UniXNU 顯示／UFS 安全策略、120 Hz TE 同步、掃描輸出追蹤與 mtdblk 獨立紀錄（fx_log） | 上游繼承（ios-fuxi）；ksrc：本機修改 +396／-42（本機提交 `d5c44c3`、`7af418a`、`d96adda`、`05ed0fa`）；本機修改見 G-048、K-001、K-018、K-019、K-033、K-064、K-065 |  |
| [S-107](items/S-upstream.zh-TW.md#s-107-實作依-entitlement-授權特權與無容器程序-sandbox-路徑回應fuxiprivsfuxisbpath) | 實作依 Entitlement 授權特權與無容器程序 Sandbox 路徑回應（fuxiprivs／fuxisbpath） | 上游繼承（ios-fuxi）；ksrc：上游原樣 |  |
| [S-108](items/S-upstream.zh-TW.md#s-108-實作-sweetsurface-scanout-租約管理列對齊zero-copy-調查與-dpu-stage-3-直接掃描sweetsurfrow) | 實作 SweetSurface Scanout 租約管理、列對齊、Zero-Copy 調查與 DPU Stage 3 直接掃描（sweetsurfrow） | 上游繼承（ios-fuxi）；ksrc：本機修改 +10／-10（本機提交 `75148d2`）；本機修改見 G-016、K-072 |  |
| [S-109](items/S-upstream.zh-TW.md#s-109-實作-applearmbacklight-服務與-dcs-0x51-螢幕亮度控制fuxibacklight) | 實作 AppleARMBacklight 服務與 DCS 0x51 螢幕亮度控制（fuxibacklight） | 上游繼承（ios-fuxi）；ksrc：本機修改 +295／-2（本機提交 `eafc778`）；本機修改見 K-017 |  |
| [S-110](items/S-upstream.zh-TW.md#s-110-移植-unixnu-union-lower-directory-查找資格修正fuxiuniondir) | 移植 UniXNU Union Lower-Directory 查找資格修正（fuxiuniondir） | 上游繼承（ios-fuxi）；ksrc：上游原樣 |  |
| [S-111](items/S-upstream.zh-TW.md#s-111-實作可選使用者空間程序堆疊ipi-即時取樣剖析器sweetprof) | 實作可選使用者空間程序堆疊／IPI 即時取樣剖析器（sweetprof） | 上游繼承（ios-fuxi）；ksrc：本機修改 +198／-4（本機提交 `75148d2`、`f1a0846`、`7af418a`、`d96adda`、`d10a41d`）；本機修改見 G-021、G-027、G-048、K-018、K-019、K-071、K-072 |  |
| [S-112](items/S-upstream.zh-TW.md#s-112-實作-backboardd-ioregistry-查詢與-work-interval-耗時追蹤fuxiregtrace) | 實作 backboardd IORegistry 查詢與 Work-Interval 耗時追蹤（fuxiregtrace） | 上游繼承（ios-fuxi）；ksrc：上游原樣 |  |
| [S-113](items/S-upstream.zh-TW.md#s-113-移植音訊-gpraudioreach-封包編碼與共用-rsc-active-tcs-仲裁器基礎p-au1) | 移植音訊 GPR／AudioReach 封包編碼與共用 RSC ACTIVE TCS 仲裁器基礎（P-AU1） | 上游繼承（ios-fuxi）；ksrc：上游原樣 | 無 |
| [S-114](items/S-upstream.zh-TW.md#s-114-實作全核心-el1-pac-指針驗證硬體自我測試fuxigpuncimport-bit-25) | 實作全核心 EL1 PAC 指針驗證硬體自我測試（fuxigpuncimport bit 25） | 上游繼承（ios-fuxi）；ksrc：上游原樣 |  |
| [S-115](items/S-upstream.zh-TW.md#s-115-實作實驗性原生-arm64e-核心建置與開機金鑰初始化fuxi_arm64e1) | 實作實驗性原生 arm64e 核心建置與開機金鑰初始化（FUXI_ARM64E=1） | 上游繼承（ios-fuxi）；ksrc：上游原樣 | 無（建置時期以 `FUXI_ARM64E=1` 切換目標 `kernel=metal4e`，非一般開機參數） |
| [S-116](items/S-upstream.zh-TW.md#s-116-調整-arm64e-核心下-fat-binary-評分以停用-user-jop-避免-fpac-panicfuxiarm64e) | 調整 arm64e 核心下 Fat Binary 評分以停用 User JOP 避免 FPAC Panic（fuxiarm64e） | 上游繼承（ios-fuxi）；ksrc：上游原樣 |  |
| [S-117](items/S-upstream.zh-TW.md#s-117-xnu-27-與-kernel-libpthread-的各層補丁) | XNU 27 與 kernel libpthread 的各層補丁 | 保留早期 iOS 27 路線，相關實機紀錄見 E-001～E-029。 | 上游工具的 kernel-ios27 目標。FUXI_XNU27_CHAINED 與 FUXI_XNU27_RELEASE_REF 控制附加建置。 |
| [S-118](items/S-upstream.zh-TW.md#s-118-上游-nativemetalmetal2vulkan-與-mesa-建置補丁) | 上游 NativeMetal、metal2vulkan 與 Mesa 建置補丁 | 保留上游建置依存與每份補丁索引。 | FUXI_NATIVE_M2V_DRAW=0 跳過上游 Metal 轉譯 draw 補丁。完整建置選項見各工具。 |

### E：早期開機與環境（2026-11-07～11-14）（49 項）

| 編號 | 項目 | 狀態 | 開關 |
|---|---|---|---|
| [E-001](items/E-early.zh-TW.md#e-001-boot-1bootseed_init-256-位元組隨機種子不足) | Boot 1：`bootseed_init` 256 位元組隨機種子不足 | 修復：改用 `tools/mkadt-fuxi27.py` 產生 256 位元組隨機種子，通過 `bootseed_init` | `fuxi` |
| [E-002](items/E-early.zh-TW.md#e-002-boot-2kern_trustcache-缺少-appleimage4-介面) | Boot 2：`kern_trustcache` 缺少 `AppleImage4` 介面 | 修復：編寫 `tools/ios27-prelink.py` 預連結 39 個官方 kext，通過 `kern_trustcache` | 無 |
| [E-003](items/E-early.zh-TW.md#e-003-boot-3fuxiroot27-遭-android-dm-default-key-加密) | Boot 3：`FuxiRoot27` 遭 Android `dm-default-key` 加密 | 修復：改用 `FIEMAP` 實體 extent 寫入工具 `scratch/stream_extents_dev` 寫入明文 | 無 |
| [E-004](items/E-early.zh-TW.md#e-004-boot-45twrp-區塊快取內聯加密導致磁區雜湊不符) | Boot 4–5：TWRP 區塊快取／內聯加密導致磁區雜湊不符 | 修復：改用 TCP 9999 原始 extent 串流寫入與逐塊 SHA-256 驗證，extent 0 100% 吻合 | 無 |
| [E-005](items/E-early.zh-TW.md#e-005-boot-6bsd_init-根卷未認證-panic) | Boot 6：`bsd_init` 根卷未認證 panic | 修復：加入開機參數 `fuxi_root27_norootauth=1` 略過根卷認證 | `fuxi_root27_norootauth=1` |
| [E-006](items/E-early.zh-TW.md#e-006-boot-7閉源-apfs-kext-struct-upl-abi-不相容) | Boot 7：閉源 APFS kext `struct upl` ABI 不相容 | 修復：以 `--drop-apfs` 移除閉源 APFS，改用內建唯讀 Dada-APFS（Patch 0008） | 無 |
| [E-007](items/E-early.zh-TW.md#e-007-boot-8iofindbsdroot-根裝置-uuid-不匹配) | Boot 8：`IOFindBSDRoot` 根裝置 UUID 不匹配 | 修復：`fuxi27-root.adt` 改用 `--root-uuid 2ABE836E-EB6D-4C24-B639-FDEDDD5154B2` | 無 |
| [E-008](items/E-early.zh-TW.md#e-008-boot-9amfi-coretrust-拒絕未註冊之-sbinlaunchd) | Boot 9：AMFI CoreTrust 拒絕未註冊之 `/sbin/launchd` | 修復：合併 3 個官方 TrustCache（2,851 筆）至 `ios27.tc` 並以 `--trustcache` 打包 | 無 |
| [E-009](items/E-early.zh-TW.md#e-009-boot-10進入-launchd-使用者態後因無-rdmd0-觸發-exc_guard) | Boot 10：進入 `launchd` 使用者態後因無 `rd=md0` 觸發 `EXC_GUARD` | 修復：加入 `fuxi_launchd_ramdisk=1` 與 `fuxi_proc_log=1` | `rd=md0`、`fuxi_launchd_ramdisk=1`、`fuxi_proc_log=1` |
| [E-010](items/E-early.zh-TW.md#e-010-boot-1143-個-dyld-快取掛載完成後遇嚴格回覆埠例外) | Boot 11：43 個 dyld 快取掛載完成後遇嚴格回覆埠例外 | 修復：加入 `task_exc_guard_default=0` | 無 |
| [E-011](items/E-early.zh-TW.md#e-011-boot-12cpus7-下跨核心-sigp-斷言-panic) | Boot 12：`cpus=7` 下跨核心 `SIGP` 斷言 panic | 修復：改為單核 `cpus=1`（ADT `--cpus 1`），核心 0 跑 XNU、`cpu@100` 跑 USB 探針 | `cpus=7`、`cpus=1` |
| [E-012](items/E-early.zh-TW.md#e-012-boot-13mach-埠安全策略-policy-7-殺死-launchd) | Boot 13：Mach 埠安全策略 Policy 7 殺死 `launchd` | 修復：`patch_kernel_guard.py` 關閉 `_ool_port_array_enforced` 並放行 `_mach_port_guard_ast` | 無 |
| [E-013](items/E-early.zh-TW.md#e-013-boot-14could-not-set-up-inheritance-portnaked-send-檢查) | Boot 14：`could not set up inheritance port`（naked-send 檢查） | 修復：`patch_kernel_inheritance.py` 放行 `ipc_can_stash_naked_send`（Boot 15 發現仍缺 MIG 檢查） | 無 |
| [E-014](items/E-early.zh-TW.md#e-014-boot-15mig-demux-mig_bad_arguments--304) | Boot 15：MIG demux `MIG_BAD_ARGUMENTS` (-304) | 修復：繞過 `__X_kernelrpc_mach_ports_register3` demux 檢查，強制回傳 `KERN_SUCCESS` | 無 |
| [E-015](items/E-early.zh-TW.md#e-015-boot-16launchd-進入-dispatch_main子進程-xpcproxy-遇-sigtrap) | Boot 16：`launchd` 進入 `dispatch_main`，子進程 `xpcproxy` 遇 `SIGTRAP` | 修復：於 `psignal_internal` 注入使用者態回溯列印以定位子進程崩潰點 | `fuxi_user_backtrace` |
| [E-016](items/E-early.zh-TW.md#e-016-boot-17mach_ports_lookup-ool-回覆長度不匹配) | Boot 17：`mach_ports_lookup()` OOL 回覆長度不匹配 | 修復：`patch_kernel_lookup.py` 重寫 `_X_kernelrpc_mach_ports_lookup3` 輸出 OOL 埠陣列 | 無 |
| [E-017](items/E-early.zh-TW.md#e-017-boot-18核心堆積隔離混淆-panic-kfree_heap_confusion_panic) | Boot 18：核心堆積隔離混淆 panic (`kfree_heap_confusion_panic`) | 修復：改用 `kalloc_ext` 配對 `kalloc_type_view_3960` 並跳過 `kfree_zone` 檢查 | 無 |
| [E-018](items/E-early.zh-TW.md#e-018-boot-19子進程因-invalid-bootstrap-port-遇-sigtrap) | Boot 19：子進程因 `Invalid bootstrap port` 遇 `SIGTRAP` | 修復：`patch_kernel_register.py` 完整解包並呼叫 `_kernelrpc_mach_ports_register3` | 無 |
| [E-019](items/E-early.zh-TW.md#e-019-boot-20patch_kernel_registerpy-硬編碼-count--3-導致-launchd-失敗) | Boot 20：`patch_kernel_register.py` 硬編碼 `count == 3` 導致 `launchd` 失敗 | 修復：改為接受動態計數 `0 <= count <= 3` | 無 |
| [E-020](items/E-early.zh-TW.md#e-020-boot-21springboard-缺-usrliblibobjc-trampolinesdylib-崩潰) | Boot 21：`SpringBoard` 缺 `/usr/lib/libobjc-trampolines.dylib` 崩潰 | 修復：`patch_kernel_trampoline.py` 攔截 `lookup_check_for_resolve_prefix` 導向 Cryptex | 無 |
| [E-021](items/E-early.zh-TW.md#e-021-boot-22springboard-首次穩定啟動約-90-秒觸發-watchdog-重開) | Boot 22：`SpringBoard` 首次穩定啟動，約 90 秒觸發 watchdog 重開 | 實驗：定位 90 秒重開來自未傳 `--reset-minutes 30`；分類出 `logd`/`backboardd` 等崩潰 | 無 |
| [E-022](items/E-early.zh-TW.md#e-022-boot-23突破-90-秒限制跑滿-300-秒產生-431-進程與-6-代-springboard) | Boot 23：突破 90 秒限制跑滿 300 秒，產生 431 進程與 6 代 `SpringBoard` | 實驗：無重開完成 300 秒並擷取 `1080x2400` 主控台畫面；統計 10 種 daemon 崩潰 | 無 |
| [E-023](items/E-early.zh-TW.md#e-023-boot-24runningboardd-存活-300-秒總退出下降-51) | Boot 24：`runningboardd` 存活 300 秒，總退出下降 51% | 修復：`patch_kernel_boot24.py` 抑制 `EXC_GUARD`（`logd`/`backboardd` 尚待續修） | 無 |
| [E-024](items/E-early.zh-TW.md#e-024-boot-25logd-存活-285-秒零崩潰定位-backboardd-真正退出路徑) | Boot 25：`logd` 存活 285 秒零崩潰，定位 `backboardd` 真正退出路徑 | 修復：`_hook_namei`＋`_hook_vn_stat` 修好 `logd`；定位 `_exit_with_reason` 尾呼叫 | 無 |
| [E-025](items/E-early.zh-TW.md#e-025-boot-26_hook_exit_with_reason-誤呼叫-proc_unlock-導致-mutex-panic) | Boot 26：`_hook_exit_with_reason` 誤呼叫 `proc_unlock` 導致 Mutex panic | 修復：移除誤呼叫之 `proc_unlock`，改為無鎖呼叫 `_thread_terminate_self()` | 無 |
| [E-026](items/E-early.zh-TW.md#e-026-boot-27_hook_lookup_prefix-誤將-flags-0x100-當指標解參考) | Boot 27：`_hook_lookup_prefix` 誤將 `flags` (`0x100`) 當指標解參考 | 修復：`patch_kernel_boot28.py` 恢復經驗證之 `_hook_lookup_prefix` 實作 | 無 |
| [E-027](items/E-early.zh-TW.md#e-027-boot-28300-秒零-panicspringboardbackboardd-存活診斷無畫面原因) | Boot 28：300 秒零 panic、`SpringBoard`/`backboardd` 存活，診斷無畫面原因 | 結論：確認因 ADT 缺 `/arm-io/disp0` 強制 `BKDisplayIsHeadless=true` | 無 |
| [E-028](items/E-early.zh-TW.md#e-028-boot-29adt-注入-disp0wdtbuttons靜態-trustcache-與靜音主控台) | Boot 29：ADT 注入 `disp0`/`wdt`/`buttons`、靜態 TrustCache 與靜音主控台 | 未解決：`headless=false` 且 68 服務常駐 300s 零 panic，但實體螢幕仍需 K5 `AppleCLCD` 管線 | `sweetfb_noconsole=1` |
| [E-029](items/E-early.zh-TW.md#e-029-xnu-12377-顯示管線缺口盤點與假-apple-logo-移除) | `xnu-12377` 顯示管線缺口盤點與假 Apple Logo 移除 | 結論：確認 `loader.c` 已無假 logo 繪製，並盤點 `SweetFramebuffer` 無法直接複製之原因 | 無 |
| [E-030](items/E-early.zh-TW.md#e-030-評估原生-ios-16-核心路徑xnu-8796-ram-診斷開機-fuxi-diag-ramimg) | 評估原生 iOS 16 核心路徑：`xnu-8796` RAM 診斷開機 `fuxi-diag-ram.img` | 實驗：驗證 `IOCoreSurfaceRoot` 與 `AppleCLCD` 成功註冊（靜態 `init` PID 1） | `cpus=1`、`rd=md0`、`sweetlog=0` |
| [E-031](items/E-early.zh-TW.md#e-031-讀回-ios27-rootimg-與隔離-qemu-alpine-建置-ios-16716-hfs-rootfs) | 讀回 `ios27-root.img` 與隔離 QEMU Alpine 建置 iOS 16.7.16 HFS+ RootFS | 實驗：備份 `ios27-root.img` 並產出 20 GiB `ios16-root-build.img`（`fsck.hfsplus` 通過） | 無 |
| [E-032](items/E-early.zh-TW.md#e-032-f2fs_iors-pin-誤判與釐清-fiemap-0x1000-merged-旗標) | `f2fs_io.rs` pin 誤判與釐清 `FIEMAP` `0x1000` (`MERGED`) 旗標 | 修復：確認 `/data/ios16-codex-b.img`（160 extents）無 `UNWRITTEN` 等異常旗標 | 無 |
| [E-033](items/E-early.zh-TW.md#e-033-排除-windows-adb-0x1a-eof-問題改-tcp-29865-串流寫入與讀回驗證) | 排除 Windows `adb` `0x1a` EOF 問題，改 TCP 29865 串流寫入與讀回驗證 | 修復：全檔 20 GiB 逐位元讀回比對 SHA-256 100% 吻合 | 無 |
| [E-034](items/E-early.zh-TW.md#e-034-ios-16716-xnu-8796-首次實機開機鎖屏截圖與主畫面手勢確認) | iOS 16.7.16 (`xnu-8796`) 首次實機開機、鎖屏截圖與主畫面手勢確認 | 實驗：顯示 iOS 鎖屏且上滑進主畫面可行，但 CPU 渲染極卡頓且 30 分鐘後觸發自動重置 | 無 |
| [E-035](items/E-early.zh-TW.md#e-035-建立效能-ab-候選-ios16-perf-loader-與修正-monitor---kernel-slide) | 建立效能 A/B 候選 `ios16-perf-loader` 與修正 monitor `--kernel-slide` | 工具：產出含 `fuxiperf=1 fuxicpuload=10` 之開機映像（尚未上機）並修正 slide 誤判 | `fuxiperf=1`、`fuxicpuload=10` |
| [E-036](items/E-early.zh-TW.md#e-036-ksu-雙系統開機模組-fuxi_ios_bootboot_b-分流器-chain-v1v9) | KSU 雙系統開機模組 `fuxi_ios_boot`（`boot_b` 分流器 `chain-v1`～`v9`） | 結論：實作 `boot_b` 免電腦開 iOS，已於 2026-11-18 改用 `boot_a` 常駐選單並封存移除 | 無 |
| [E-037](items/E-early.zh-TW.md#e-037-開機觸控延遲-30-秒成因與改預設為-10-秒) | 開機觸控延遲 30 秒成因與改預設為 10 秒 | 修復：`fuxi_touch.c` `fuxitouchdelay` 預設 30 秒，先以 loader 調 10 秒再合入核心補丁 | `fuxitouchdelay`、`fuxitouchdelay=10` |
| [E-038](items/E-early.zh-TW.md#e-038-開機真-apple-logo-疊加開機跑碼待辦低優先) | 開機真 Apple Logo 疊加開機跑碼待辦（低優先） | 未解決：採樣 USB 抓圖需 16s 無法抓 2s logo，待從 rootfs 取原圖並改 console 背景重畫 | 無 |
| [E-039](items/E-early.zh-TW.md#e-039-小米-13-硬體適配現成方案總調查) | 小米 13 硬體適配現成方案總調查 | 結論：確定優先移植順序：亮度 → ADSP+GLINK 基礎層 → 電池 → 聲音 → Wi-Fi/藍牙 | `fuxi` |
| [E-040](items/E-early.zh-TW.md#e-040-亮度-dsi0-applearmbacklight-離線補丁準備0117) | 亮度 DSI0 `AppleARMBacklight` 離線補丁準備（`0117`） | 結論：完成 `fuxibacklight=4/5` 補丁與主機測試，但調查建議改由掃描輸出代送不忙等 | `fuxibacklight=1\|2\|3`、`fuxibacklight=4`、`fuxibacklight=5` |
| [E-041](items/E-early.zh-TW.md#e-041-音訊-p-au0p-au2-離線準備0118-rpmh-鎖與-cs35l41-dt) | 音訊 `P-AU0/P-AU2` 離線準備（`0118` RPMH 鎖與 `CS35L41` DT） | 結論：完成跨驅動 TCS 鎖補丁與 `CS35L41` 規格整理，仍缺 ADSP PAS/GLINK 與韌體 | 無 |
| [E-042](items/E-early.zh-TW.md#e-042-wi-fi-fuxiwifiios1p17-診斷與-608-位元組開機參數組) | Wi-Fi `fuxiwifiios=1`、`P17` 診斷與 608 位元組開機參數組 | 結論：產生 9 組 `<608B` 開機參數與 WCN7850 韌體 SHA256 清單；802.11 資料面待開發 | `fuxiwifiios=1`、`sweetsample`、`sweetscan`、`sweetexecswap`、`sweetnull`、`fuxiwlanmlo=0x17d`、`fuxiwlanrails=1` |
| [E-043](items/E-early.zh-TW.md#e-043-電池-spmi-v7--pmk8550-vadc9000--pmic_rtr_adsp_apps-規格分析) | 電池 SPMI v7 / `PMK8550 vadc@9000` / `PMIC_RTR_ADSP_APPS` 規格分析 | 結論：確認 `SweetBattery` 因 SPMI v7 退回靜態 100%，規劃先唯讀 VADC 探針再接 ADSP | 無 |
| [E-044](items/E-early.zh-TW.md#e-044-藍牙-qup2-se6-規格h4-framer-與-sweetnoexec-解除前置清單) | 藍牙 QUP2 SE6 規格、H4 framer 與 `sweetnoexec` 解除前置清單 | 結論：備妥 H4 狀態機與接腳規格；未有核心驅動前嚴禁移除 `sweetnoexec=bluetoothd` | `sweetnoexec=bluetoothd,fairplayd.H2`、`sweetnoexec=bluetoothd` |
| [E-045](items/E-early.zh-TW.md#e-045-參考專案-unixnu-sheng-與-unixnu-sm8550-common-技術盤點) | 參考專案 `UniXNU-sheng` 與 `UniXNU-sm8550-common` 技術盤點 | 結論：記錄 SM8550/sheng 在 GPU、觸控、AppSync、電源與記憶體壓縮之實證做法 | `fuxi` |
| [E-046](items/E-early.zh-TW.md#e-046-參考專案-unixnuath12kwifi-技術盤點) | 參考專案 `UniXNU/Ath12kWifi` 技術盤點 | 結論：Linux v7.0.14 `ath12k`＋`wpa_supplicant 2.12`＋`org.unixnu.wifi.control` 架構 | `fuxi` |
| [E-047](items/E-early.zh-TW.md#e-047-參考專案-unixnuqcabluetooth-技術盤點) | 參考專案 `UniXNU/QcaBluetooth` 技術盤點 | 結論：`btqca.c`＋GENI UART DMA＋`UART.SK`＋`bluetoothd` native adapter 架構 | `fuxi` |
| [E-048](items/E-early.zh-TW.md#e-048-參考方案-raphael-wifi-port-planmdsm8150-wi-fi-移植紀律) | 參考方案 `RAPHAEL-WIFI-PORT-PLAN.md`（SM8150 Wi-Fi 移植紀律） | 結論：記錄 R35–R38 單變數輪次、BDF 選型與 15 項主機回歸檢查紀律 | `fuxi` |
| [E-049](items/E-early.zh-TW.md#e-049-wcn7850-原廠韌體路線的-m5w17-主機模型) | WCN7850 原廠韌體路線的 M5–W17 主機模型 | 上游主機測試保留，後續實機延伸見 K-041～K-048，日常 ath12k 路線見 K-049 起。 | 早期模型依對應 docs-lanes 文件使用 WLAN 分階段實驗值。W17／P17 記於 wlan-p17.md 的 fuxiwlanmlo 選項。 |

### G：GPU／顯示流暢度線（2026-11-08～11-15）（52 項）

| 編號 | 項目 | 狀態 | 開關 |
|---|---|---|---|
| [G-001](items/G-gpu.zh-TW.md#g-001-managed-root-與首顆-gpu-測試環境建立) | Managed root 與首顆 GPU 測試環境建立 | 實驗：建立 `codexnative_root_1108b` 與 ODM zap 驗證，但 Home 合成因缺失 Metal 功能回退 CPU | 無 |
| [G-002](items/G-gpu.zh-TW.md#g-002-nativemetal-補丁堆疊與裝置冒煙測試stages-07) | NativeMetal 補丁堆疊與裝置冒煙測試（stages 0–7） | 修復：通過 GPU 裝置探索、三角形與明確 ASTC 像素測試，證實底層渲染可行 | 無 |
| [G-003](items/G-gpu.zh-TW.md#g-003-nativemetal-補丁-00040006-與優先權診斷) | NativeMetal 補丁 0004–0006 與優先權診斷 | 修復：修復可選回答故障誤判、completion handler 與 `uchar3` 常數；CA 離屏 stage 5 達 60 幀 PASS | 無 |
| [G-004](items/G-gpu.zh-TW.md#g-004-唯讀-host-import-調查與-fuxigpuncimport1024) | 唯讀 Host Import 調查與 `fuxigpuncimport=1024` | 實驗：啟用 bit 10 唯讀匯入通過 CA 圖片階段，但揭露計時器與排程異常 | `fuxigpuncimport`、`fuxigpuncimport=1024` |
| [G-005](items/G-gpu.zh-TW.md#g-005-nocopy-診斷補丁-000700080010-與-atx-來源調查) | `nocopy` 診斷補丁 0007/0008/0010 與 ATX 來源調查 | 實驗：鎖定阻礙為 QuartzCore 傳入 1125×2436 `pixelFormat=204`（2,752,512 bytes）之無 deallocator 請求 | 無 |
| [G-006](items/G-gpu.zh-TW.md#g-006-copy-span-邊界校驗補丁m2v-0001--nativemetal-0009) | Copy-span 邊界校驗補丁（m2v 0001 / nativemetal 0009） | 修復：為 `HostLayout` 與 bridge 上傳／讀取加入 checked arithmetic 防止越界 | 無 |
| [G-007](items/G-gpu.zh-TW.md#g-007-astc-layout-最小回移植補丁-00110012) | ASTC layout 最小回移植（補丁 0011/0012） | 修復：完成同行程 producer/consumer 配對與跨行程 ATX row-major 快照匯入（`FUXI_NATIVE_FOREIGN_LINEAR`） | `FUXI_NATIVE_LINEAR_LAYOUT=0`、`FUXI_NATIVE_FOREIGN_LINEAR=0` |
| [G-008](items/G-gpu.zh-TW.md#g-008-native-layout0011-部署與實機驗證) | `native-layout0011` 部署與實機驗證 | 實驗：部署補丁 0011，但實機拒絕未變，證明 1125×2436 請求來自跨行程 ATX 映像而非同行程 producer | 無 |
| [G-009](items/G-gpu.zh-TW.md#g-009-native-layout0012-部署與首次持續-gpu-桌面合成) | `native-layout0012` 部署與首次持續 GPU 桌面合成 | 修復：部署補丁 0012，首次達成持續桌面 GPU 合成，但受限於未調頻與同步提交僅約 16–21 Hz | `fuxigpuncimport=1024` |
| [G-010](items/G-gpu.zh-TW.md#g-010-實驗-1fuxigpulevel4401-mhz--ddr-投票) | 實驗 1：`fuxigpulevel=4`（401 MHz + DDR 投票） | 實驗：升頻後觀察幀率變化與溫度，確認單純升頻無法突破 ~30 Hz 瓶頸 | `fuxigpulevel=4` |
| [G-011](items/G-gpu.zh-TW.md#g-011-實驗-2fuxigpuncimport1536queue-模式與閃爍) | 實驗 2：`fuxigpuncimport=1536`（queue 模式）與閃爍 | 實驗：開啟 bit 9 queue 模式消除 noq 重試睡眠，但滑動時螢幕出現嚴重閃爍 | `fuxigpuncimport`、`fuxigpuncimport=1536` |
| [G-012](items/G-gpu.zh-TW.md#g-012-native-layout0013-部署nativemetal-補丁-0013-swap-wait) | `native-layout0013` 部署（NativeMetal 補丁 0013 swap wait） | 修復：實作 `fuxiswapwait` 等待 GPU 完成才回傳 swap，實機目視確認滑動閃爍完全消除 | `fuxigpuncimport=1536`、`FUXI_NATIVE_SWAP_WAIT=0`、`fuxiswapwait=0` |
| [G-013](items/G-gpu.zh-TW.md#g-013-實驗-3queuelevel4與實驗-4scanout120-120-hz-面板接管) | 實驗 3（queue+level4）與實驗 4（`scanout120` 120 Hz 面板接管） | 實驗：啟用 `fuxipanel120=1` 與 `fuxivsyncte=1` 解除 60 Hz 面板鎖定，vsync 達 120.9 Hz | `fuxigpulevel=4`、`fuxigpuncimport=1536`、`fuxigpuscanout=1`、`fuxiscandouble=1`、`fuxiscantriple=1`、`fuxipanel120=1`、`fuxivsyncte=1` |
| [G-014](items/G-gpu.zh-TW.md#g-014-實驗-5kernel-bo-slots-4096--9728prefdiag-與實驗-6dirty19) | 實驗 5（`kernel-bo-slots` 4096 + `9728`）、`prefdiag` 與實驗 6（dirty19） | 實驗：解除 BO slot 上限與 START floor 達 52.5 Hz（使用者「有變順」）；回報「關於本機」卡死與多工馬賽克；bit 19 自測拒測 | `fuxigpuncimport=9728`、`fuxigpuncimport` |
| [G-015](items/G-gpu.zh-TW.md#g-015-實驗-79792-bit-6-coherent-self-test與實驗-8level-8-680-mhz) | 實驗 7（`9792` bit 6 coherent self-test）與實驗 8（level 8 680 MHz） | 實驗：`wb-ish` 通過 8/8，消除每幀 1.1–1.9 ms cache 清理，跳升至 75.4 Hz；level 8 升溫至 73.8 °C 不適合常駐 | `fuxigpuncimport`、`fuxigpuncimport=9792`、`fuxigpulevel=8` |
| [G-016](items/G-gpu.zh-TW.md#g-016-實驗-9kernel-surfrow-mode2-sweetsurfrow2與實驗-10te-spin) | 實驗 9（`kernel-surfrow-mode2` `sweetsurfrow=2`）與實驗 10（TE spin） | 修復：修復 app-switcher 卡片馬賽克（best 97.6 Hz）；實驗 10（`304704`）確認 `swap wait` 為下一段串行瓶頸 | `fuxigpuncimport=304704`、`sweetsurfrow=1`、`sweetsurfrow`、`sweetsurfrow=2` |
| [G-017](items/G-gpu.zh-TW.md#g-017-native-layout0014-部署m2v-補丁-0002-fresh-zero與實驗-11關-swap-wait) | `native-layout0014` 部署（m2v 補丁 0002 `fresh-zero`）與實驗 11（關 swap wait） | 修復：新建未寫材質改為歸零（除紫色背景），`aboutdiag` 定位 `InterfaceNamer` 卡死；bit 12 關 swap wait（`308800`）無閃爍 | `fuxiswapwait=0`、`fuxigpuncimport=308800` |
| [G-018](items/G-gpu.zh-TW.md#g-018-native-layout0015-部署與實驗-1213cpu-調頻器) | `native-layout0015` 部署與實驗 12/13（CPU 調頻器） | 修復：修改 `InterfaceNamer` 逾時為 15 s 解決「關於本機」卡死並清舊模糊快取；實驗 12/13 揭露 62 °C / 200 ms 限頻 | `fuxiperfbase`、`fuxiperfmax`、`fuxiperfbudget`、`fuxicores`、`fuxiperf` |
| [G-019](items/G-gpu.zh-TW.md#g-019-native-layout0016-部署實驗-1516-與首輪-ib-profile) | `native-layout0016` 部署、實驗 15/16 與首輪 IB profile | 實驗：加入 `kernel-surface-stride` 與 `novmd`，`nowait` 達 86.0 fps / 94.98 Hz；`kernel-ib-profile` 定位 GPU exec 7.5–9.4 ms | `fuxiearly`、`fuxigpulevel=6` |
| [G-020](items/G-gpu.zh-TW.md#g-020-native-layout0017-部署m2v-補丁-0003-單獨測試) | `native-layout0017` 部署（m2v 補丁 0003 單獨測試） | 實驗：僅套用 0003（`FLUSH_PER_OVERLAP`），`s25-ibp` 測得 34.0 Hz / `gpu_ms` 8.62 | 無 |
| [G-021](items/G-gpu.zh-TW.md#g-021-native-layout0018-部署m2v-00030004與-sweetprof-剖析) | `native-layout0018` 部署（m2v 0003+0004）與 `sweetprof` 剖析 | 實驗：`gpu_ms` 降至 6.64 ms，但 `sweetprof` 發現顯示執行緒 20% CPU 耗在 `supportsFamily:` 稽核 | `sweetprof` |
| [G-022](items/G-gpu.zh-TW.md#g-022-native-layout0019-部署m2v-補丁-0005-family-cache與退回-0003) | `native-layout0019` 部署（m2v 補丁 0005 `family-cache`）與退回 0003 | 修復/停損：消除 `supportsFamily:` 開銷，推升至 83.4 Hz（dragreg 86.2 Hz）；但視覺檢查發現材質／毛玻璃毀損，決定移除 0003 | `fuxiearly=16`、`fuxiperf=2`、`fuxiregtrace=1` |
| [G-023](items/G-gpu.zh-TW.md#g-023-native-layout0020-部署移除-0003) | `native-layout0020` 部署（移除 0003） | 修復：僅保留 0004+0005，兩輪使用者目視確認材質／毛玻璃恢復正常 | 無 |
| [G-024](items/G-gpu.zh-TW.md#g-024-native-layout0021-部署m2v-補丁-00060008) | `native-layout0021` 部署（m2v 補丁 0006–0008） | 實驗：加入 m2v 逐 draw 與 shadow copy 診斷計數，確認大量多餘 shadow copy | 無 |
| [G-025](items/G-gpu.zh-TW.md#g-025-native-layout0022-部署m2v-0009與-gmem-重試touch10) | `native-layout0022` 部署（m2v 0009）與 GMEM 重試／`touch10` | 修復：消除 shadow copies，sysmem 達 90.1 Hz / 71% 單週期；GMEM 達 101.9 Hz / 89%；`fuxitouchdelay=10` 提前觸控；大檔集中工作區 | `fuxitouchdelay`、`fuxitouchdelay=10` |
| [G-026](items/G-gpu.zh-TW.md#g-026-native-layout0023-部署fuxitudebug與-gmem-二分法) | `native-layout0023` 部署（`fuxitudebug=`）與 GMEM 二分法 | 實驗：以 `fuxitudebug=` 二分排除 `flushall`/`syncdraw`/`nobin`，當時判為 `noconcurrentresolves` 修復條紋 | `fuxitudebug=` |
| [G-027](items/G-gpu.zh-TW.md#g-027-native-layout0024-部署假象平反boot_b-開機鏈與-vt6-突破) | `native-layout0024` 部署、假象平反、`boot_b` 開機鏈與 `vt6` 突破 | 修復/結論：`--tour-verify` 證實 GMEM 條紋為擷取假象；`PROFINC`/`PROFEDGE` 鎖定 m2v 逐 draw 為 CPU 瓶頸；建立 `boot_b` `chain-v1`；`fuxivsynctoff=6000` 讓捲動達 ~119 Hz | `fuxigpugmem=1`、`fuxitudebug=nocb`、`fuxitapcmd=@1`、`fuxigpuncimport`、`fuxigpulevel=24`、`fuxiperfidle=1000`、`fuxivsynctoff=6000` |
| [G-028](items/G-gpu.zh-TW.md#g-028-native-layout0025-部署bridge-lto--fuxitcache1與-3-基準) | `native-layout0025` 部署（Bridge LTO + `fuxitcache=1`）與 `@3` 基準 | 修復：Bridge 改 fat LTO + tcache，每 draw 由 ~72 µs 降至 57.7 µs；`@3` 達 81.7 Hz / 5.73 ms CPU | `fuxitcache=1`、`fuxitapcmd=@3` |
| [G-029](items/G-gpu.zh-TW.md#g-029-native-layout0026-部署hybrid-p1-普查-fuxihyb1與-fuxibacklight4) | `native-layout0026` 部署（Hybrid P1 普查 `fuxihyb=1`）與 `fuxibacklight=4` | 實驗：79.0% draw 符合 L1 資格（唯一排除為 fetch 21%），每 draw m2v 37.4 µs；離線完成 `kernel-backlight-dsi.patch` | `fuxihyb=1`、`fuxibacklight=4` |
| [G-030](items/G-gpu.zh-TW.md#g-030-native-layout0027-部署m2v-補丁-0011-細分-rest) | `native-layout0027` 部署（m2v 補丁 0011 細分 `rest`） | 實驗：細分 `rest` 與修復日誌截斷；86.6% 合格、29.6 µs/draw，`sweetprof` 指明 `Recording::record` 與每 encoder 成本 | `sweetprof` |
| [G-031](items/G-gpu.zh-TW.md#g-031-native-layout0028-部署m2v-補丁-0012-細分-presettlerec) | `native-layout0028` 部署（m2v 補丁 0012 細分 `pre`/`settle`/`rec`） | 實驗：細分三大階段比例，`rec` 佔 11.9 µs（`record` 6.8 µs），CA 平均每個 encoder 僅 ~3 筆 draw | 無 |
| [G-032](items/G-gpu.zh-TW.md#g-032-native-layout0029-部署m2v-0013-fuxihyb2-sampler-快取) | `native-layout0029` 部署（m2v 0013 `fuxihyb=2` sampler 快取） | 實驗：快取 argument sampler 清單與位址掃描；但每 draw 僅 2.0 sampler 且開機漂移大於微調收益 | `fuxihyb=2`、`fuxihyb=3` |
| [G-033](items/G-gpu.zh-TW.md#g-033-native-layout0030-部署m2v-0014-set-0-重用--fuxihyb3) | `native-layout0030` 部署（m2v 0014 set-0 重用 + `fuxihyb=3`） | 實驗/停損：`fuxihyb=3` 單次開機 A/B 實測 set-0 重用淨損失 +0.59 µs (+1.8%)，決定移除 | `fuxihyb=3`、`sweetprof` |
| [G-034](items/G-gpu.zh-TW.md#g-034-native-layout0031-部署m2v-0015-移除-set-0--calls-普查) | `native-layout0031` 部署（m2v 0015 移除 set-0 + `calls` 普查） | 實驗：發現 100% encoder 都在記錄 rebind call log（107,077 次），而 NativeMetal 前端從不 rebind | 無 |
| [G-035](items/G-gpu.zh-TW.md#g-035-native-layout0032-部署m2v-0016-關閉-call-logrebind) | `native-layout0032` 部署（m2v 0016 關閉 call log/rebind） | 修復：lane 下關閉 call logging 與拒絕 rebind 並延後重設 pool，同 pipeline 淨省 -4.49 µs/draw (-13.8%) | `fuxihyb=2\|3`、`fuxihyb=3` |
| [G-036](items/G-gpu.zh-TW.md#g-036-native-layout0033-部署m2v-0017-fuxihyb4-日常靜音) | `native-layout0033` 部署（m2v 0017 `fuxihyb=4` 日常靜音） | 修復：無普查 A/B 確認 `hyb4` 達 87.1 Hz / 5.58 ms CPU（vs 基線 81.2 Hz / 6.68 ms，-16.5% CPU） | `fuxihyb=4`、`sweetprof` |
| [G-037](items/G-gpu.zh-TW.md#g-037-native-layout0034-部署m2v-0018-延後釋放-leftover與-vt-ab) | `native-layout0034` 部署（m2v 0018 延後釋放 leftover）與 `vt` A/B | 修復：延後 25 個欄位釋放省 ~1.7 µs/encoder；tour 10 張截圖正常；vsync offset 4000 中性、2000 掉 DeviceLost | `fuxivsynctoff` |
| [G-038](items/G-gpu.zh-TW.md#g-038-native-layout0035-部署nativemetal-0016-fuxisplit1500) | `native-layout0035` 部署（nativemetal 0016 `fuxisplit=1500`） | 實驗：分段 commit 降低顯示延遲 ~2.4 ms 且渲染逐像素相同，但平均 Hz 中性（84.7 Hz），留作選項 | `fuxisplit=<us>`、`fuxisplit=1500`、`fuxisplit` |
| [G-039](items/G-gpu.zh-TW.md#g-039-native-layout0036-部署fuxi-frame--passes-普查) | `native-layout0036` 部署（`FUXI FRAME` + `passes` 普查） | 實驗：確認重幀（132.7 draws / 15.1 passes / 8.0 ms encode）超越 8.33 ms 預算；SpringBoard 93% 閒置 | `sweetprof=10090` |
| [G-040](items/G-gpu.zh-TW.md#g-040-native-layout0037-部署m2v-0020-fuxihyb56-邊條-reach-clear) | `native-layout0037` 部署（m2v 0020 `fuxihyb=5`/`6` 邊條 reach clear） | 實驗/停損：邊條 clear 全數命中但表面極小（~10K px），Hz 與 `gpu_ms` 中性（86.1 vs 86.9 Hz），日常維持 `hyb4` | `fuxihyb=5`、`fuxihyb=4` |
| [G-041](items/G-gpu.zh-TW.md#g-041-native-layout0038-部署nativemetal-0018-fuxihyb7-延遲編碼-worker) | `native-layout0038` 部署（nativemetal 0018 `fuxihyb=7` 延遲編碼 worker） | 實驗：`hyb7` 達 87.2/87.8 vs `hyb4` 85.3 Hz（中性偏正），因 CA commit 仍需 drain 等待 80% m2v 時間而無跨幀重疊 | `fuxihyb=7`、`sweetprof`、`fuxihyb=8` |
| [G-042](items/G-gpu.zh-TW.md#g-042-native-layout0039-部署fuxi-bridge-lse-builder-lse) | `native-layout0039` 部署（`fuxi-bridge-lse-builder` `+lse`） | 實驗：導入 ARMv8.1 LSE 原子指令後整體 Hz 中性（84.4/86.3 vs 86.4 Hz），`hyb7` 首度出現 1/5 偶發 DeviceLost | `fuxihyb=8` |
| [G-043](items/G-gpu.zh-TW.md#g-043-native-layout0040-部署hybrid-turnip-hyb4t--nativemetal-0019) | `native-layout0040` 部署（Hybrid Turnip `hyb4t` + nativemetal 0019） | 實驗：消除每次 `vkCmdBindPipeline` 10096 bytes `memmove`，`memmove` 佔比降但 Hz 中性（85.2 vs 85.4 Hz） | `fuxitudebug=` |
| [G-044](items/G-gpu.zh-TW.md#g-044-native-layout0041-部署nativemetal-0020-fuxihyb8--0021-錯誤名) | `native-layout0041` 部署（nativemetal 0020 `fuxihyb=8` + 0021 錯誤名） | 實驗：實作重疊 commit + pending writer lease，但因 QuartzCore `addScheduledHandler:` 閘門僅 3.4% 重疊 | `fuxihyb=8`、`fuxiswapwait=0` |
| [G-045](items/G-gpu.zh-TW.md#g-045-native-layout0042-部署nativemetal-0022-預熱--iomfb-analytics) | `native-layout0042` 部署（nativemetal 0022 預熱 + IOMFB analytics） | 修復：冷啟動首次開啟 late 幀大減 ~90%（29 vs 279）；`hyb8k` 消除每幀 3 次失敗 analytics IPC，兩者均 Hz 中性 | `fuxitudebug=` |
| [G-046](items/G-gpu.zh-TW.md#g-046-native-layout0043-部署hyb8shyb8itourl與-0043-停損結論) | `native-layout0043` 部署（`hyb8s`/`hyb8i`/`tourl`）與 **0043 停損結論** | 停損/結論：`hyb8s` 重疊 97%、`hyb8i` 中斷睡眠皆 88.1–88.5 Hz；`4+` 空檔屬渲染上游限制（腳本上限 ~90.5），建議不做 P3 | `fuxitudebug=...,ovsh`、`fuxigpuncimport=69514824` |
| [G-047](items/G-gpu.zh-TW.md#g-047-kernel-perf-widepatchfuxiperf2-寬域調頻) | `kernel-perf-wide.patch`（`fuxiperf=2` 寬域調頻） | 工具/實驗：將 `fuxiperfmax=720`、`fuxiperfbudget=1000` 整合為單一 `fuxiperf=2` 節省 boot-args 長度；提出 CPU 時脈重測假說 | `fuxiperf=2`、`fuxiperfmax=720`、`fuxiperfbudget=1000`、`fuxiperfmax`、`fuxiperfbudget`、`fuxiperfbase=2400` |
| [G-048](items/G-gpu.zh-TW.md#g-048-雙系統開機鏈-chain-v2-與-chain-v3-寫入-boot_b) | 雙系統開機鏈 `chain-v2` 與 `chain-v3` 寫入 `boot_b` | 修復：將 `fuxibacklight=4`、`fuxi,adsp=3` 與 `fuxi,log=1` 寫入 `boot_b`（修正亮度連滑停止並加入 `mtdblk` 獨立開機日誌） | `fuxibacklight=4`、`fuxi,adsp=3`、`fuxi,log=1` |
| [G-049](items/G-gpu.zh-TW.md#g-049-未解決項目鎖定自訂-devicelost4-空檔重幀材質重用等) | 未解決項目（鎖定自訂 DeviceLost、4+ 空檔、重幀材質重用等） | 未完成。已錄到一次 `DeviceLost` 後的顯示程序重啟與保護性停用，首次錯誤的原因待查。 | 無 |
| [G-050](items/G-gpu.zh-TW.md#g-050-在-1080-寬面板顯示完整-1125-寬畫面並對齊觸控) | 在 1080 寬面板顯示完整 1125 寬畫面並對齊觸控 | 兩種模式皆通過主機驗證；實機結果見下方。 | loader 的 `--display-fit uniform\|fill` 設定 ADT `/chosen fuxi,fit` 為 1 或 2。缺省值 0 沿用置中裁切。 |
| [G-051](items/G-gpu.zh-TW.md#g-051-gpu-class-0-1280-mib-配額120-hz-te-鎖定backboardd-恢復與多用戶端閘門) | GPU Class 0 1280 MiB 配額、120 Hz TE 鎖定、backboardd 恢復與多用戶端閘門 | Class 0（1280 MiB）＋120 Hz TE 鎖定＋相機重進防掉 GPU 已通過並納入 B-021；Class 1（Apple Maps）因仍觸發 `ERROR_DEVICE_LOST`，依指示暫緩排除於 B-021 | ADT `/chosen fuxi,gpu-bb-recover=3`、`fuxi,gpu-ncimport=606385736`、`fuxi,gpu-clients=none`（B-021 日常版安全關閉 Class 1 Maps；實驗開放時另有其他 client 位元） |
| [G-052](items/G-gpu.zh-TW.md#g-052-wave-2-120hz-純化整合m4-2-probe_0x4eposter-硬防護冷機-120hz-promotion-定論與-maps-phase-111212b469-隔離實驗實證) | Wave 2 120Hz 純化整合、M4-2 `probe_0x4e`、`*poster*` 硬防護、冷機 120Hz ProMotion 定論與 Maps Phase 11/12/12B／`#469` 隔離實驗實證 | **(A) 已併入 `B-022`**：Wave 2 三開關、`M4-2 probe_0x4e`、`stage-fuxi-metal.py` `*poster*` 硬防護、`Maps Class 1 quantum=100` 架構（日常預設 `--gpu-clients none` 關閉）、`#454` vs `#456` 冷機 120Hz 定論與 `Measure (P2-AR)`（`#457`）；**(B) 未併入 `B-022` 隔離實驗**：`#461` (`M4-3 v3`)、Phase 11 (`#464`/`#465`)、Phase 12 (`#466`)、Phase 12B (`#467`/`#468`) 與 `#469` 手動驗收暴露 `backboardd` 4096 BO 洩漏並經 `stage-rollback-stick14-b022-1126` 回退 `Stick-14`，交由 `B-023` 處理 | 開機參數 `fuxigpuquantum`（預設 `40`，實驗設 `100`）、`fuxigpuirqslice`（預設 `8000`）、`fuxigpulocksplit`（`1`）、`fuxinopoll`（`1`）；ADT `/chosen` 屬性 `fuxi,gpu-irqslice`、`fuxi,gpu-locksplit`、`fuxi,nopoll`、`fuxi,gpu-clients`（`none` / `minimal` / `core` / `maps-extended`，`B-022` 日常維持 `none`）、沿用 `fuxi,atx-fix=1`（`U-026`）；*[註：隔離實驗開關 `fuxi,gpu-c1-bos`（`B-022` `prepare-aio-loader.py` 僅以預設關閉 opt-in `0\|1024..4096` 支援）與 `fuxi,fb-blend`（屬獨立實驗線）未納入 `B-022` 日常開啟]* |

### K：核心硬體適配（000 線，2026-11-14 起）（84 項）

| 編號 | 項目 | 狀態 | 開關 |
|---|---|---|---|
| [K-001](items/K-kernel.zh-TW.md#k-001-adsp-開機與-glink-電池讀值) | ADSP 開機與 GLINK 電池讀值 | 採用 | `fuxiadsp=<0..4>`，或 ADT `/chosen fuxi,adsp`（u32）。0／不給＝不碰 ADSP；1＝只讀盤點（SMEM 分區與項目、ADSP 的 SMP2P 狀態、AOSS 信箱、RSC、兩條 SPI 的 GIC enable），不寫任何東西；2＝啟動 ADSP 並建立 SMP2P、GLINK、QRTR service locator 與電池管理客戶端，只記錄讀值；3＝另外發布給 iOS（K-002）；4＝測試用假讀值（K-005）。與 `fuxiwlan` 同時設定時自動降為 1，除非 ADT `fuxi,adsp-wlan=1`（K-012）。 |
| [K-002](items/K-kernel.zh-TW.md#k-002-sweetbattery-發布真實電量) | SweetBattery 發布真實電量 | 採用 | `fuxiadsp=3`（或 ADT `fuxi,adsp=3`）。另有舊開關 `sweetbattery=1`（PM6150 電壓估算，只對 sweet 機型有意義，fuxi 不用）。 |
| [K-003](items/K-kernel.zh-TW.md#k-003-單電芯電量換算) | 單電芯電量換算 | 採用 | 無（隨 `fuxiadsp>=2` 生效） |
| [K-004](items/K-kernel.zh-TW.md#k-004-充電器種類真實充電資訊與-10-秒刷新) | 充電器種類、真實充電資訊與 10 秒刷新 | 採用 | 無（隨 `fuxiadsp=3`） |
| [K-005](items/K-kernel.zh-TW.md#k-005-拔線測試讀值fuxiadsp4) | 拔線測試讀值（fuxi,adsp=4） | 實驗（只供測試，不進日常） | `fuxi,adsp=4`（ADT）或 `fuxiadsp=4`。實際執行模式是 3，另外讓 SweetBattery 先收到一筆假讀值。 |
| [K-006](items/K-kernel.zh-TW.md#k-006-sweetbattery-讀取者紀錄) | SweetBattery 讀取者紀錄 | 採用（診斷） | 無（隨 `fuxiadsp=3`） |
| [K-007](items/K-kernel.zh-TW.md#k-007-電池欄位逐項過期ttl) | 電池欄位逐項過期（TTL） | 採用 | `fuxibatttl=<秒>`（預設 90） |
| [K-008](items/K-kernel.zh-TW.md#k-008-電池查詢暫停get-pause實驗) | 電池查詢暫停（GET pause）實驗 | 實驗（已驗證，不進日常） | `fuxibatpause=<毫秒>`（0 或 130000）、`fuxibatpausedelay=<毫秒>`；loader 產生器 `prepare-ram9g-loader.py --battery-get-pause-ms 130000 --battery-get-pause-delay-ms <ms> --battery-audit`。 |
| [K-009](items/K-kernel.zh-TW.md#k-009-sweetbattery-改走-iokit-work-loop-發布) | SweetBattery 改走 IOKit work loop 發布 | 採用 | 無 |
| [K-010](items/K-kernel.zh-TW.md#k-010-充電證據工具) | 充電證據工具 | 採用（工具） | 無 |
| [K-011](items/K-kernel.zh-TW.md#k-011-adsp-與-wlan-共用-rpmh序列化與隔離) | ADSP 與 WLAN 共用 RPMh（序列化與隔離） | 採用 | 無（ADSP 或 WLAN 任一啟用時生效） |
| [K-012](items/K-kernel.zh-TW.md#k-012-adsp-與-wlan-聯合測試開關) | ADSP 與 WLAN 聯合測試開關 | 實驗（已驗，日常未開） | ADT `fuxi,adsp-wlan=1`。沒有它時，命令列同時有 `fuxiwlan` 與 `fuxiadsp>=2` 會把 ADSP 降為 1（只讀盤點）並記 `FUXI ADSP: fuxiadsp=… refused with fuxiwlan (combined hardware validation pending); surveying only`。 |
| [K-013](items/K-kernel.zh-TW.md#k-013-cs35l41-喇叭功放盤點) | CS35L41 喇叭功放盤點 | 實驗（只讀；未出聲） | `fuxiamp=<1\|2>`，或 ADT `fuxi,amp`。1＝只讀盤點：GCC 投票 `0x52008`／`0x52010`／`0x52018`、QUP2 五個 branch（core `0x233d4`、s_ahb `0x233d0`、m_ahb `0x233cc`、core_2x `0x23004`、s0 `0x1e004`）、SE0 時脈源 `0x1e010`、四支腳位；branch 全開時再讀 engine 狀態。2＝照 UniXNU-sheng `AudioProbe.cpp` 的 setupClocks／setupPins／setupBus 把 QUP2 SE0 I2C 帶起來（400 kHz、FIFO 模式），兩顆功放各自解除 reset（低 2 ms、拉高、等 10 ms OTP 開機），讀 DEVID `0x0`、REVID `0x4`、IRQ1_STATUS3 `0x10018`、IRQ1_STATUS4 `0x1001c`，不寫功放；最後重新 reset，腳位與投票還原。 |
| [K-014](items/K-kernel.zh-TW.md#k-014-音訊-spf-狀態查詢) | 音訊 SPF 狀態查詢 | 實驗（已通，下一步未做） | `fuxiaudiospf=<1\|2>`，或 ADT `fuxi,audio-spf`。1＝APM 格式查詢（40 bytes）；2＝裸 GPR 狀態查詢（24 bytes）。loader：`prepare-wlan-w18-loader.py --audio-spf [--audio-spf-format apm\|bare]`。 |
| [K-015](items/K-kernel.zh-TW.md#k-015-gpr-音訊準備主機模型) | GPR 音訊準備（主機模型） | 準備（主機模型，未上機） | 無 |
| [K-016](items/K-kernel.zh-TW.md#k-016-原廠音訊設定擷取) | 原廠音訊設定擷取 | 採用（資料） | 無 |
| [K-017](items/K-kernel.zh-TW.md#k-017-背光模式-4applearmbacklightdcs-0x51) | 背光模式 4（AppleARMBacklight＋DCS 0x51） | 採用（日常開啟） | `fuxibacklight=<0..4>`（4＝本項；日常命令列含 `fuxibacklight=4`）；`fuxibacklighttrace=1` 記錄每個 IOKit 呼叫。 |
| [K-018](items/K-kernel.zh-TW.md#k-018-dsi-錯誤容忍與等滿一幀) | DSI 錯誤容忍與等滿一幀 | 採用 | 無（隨 `fuxibacklight=4`） |
| [K-019](items/K-kernel.zh-TW.md#k-019-獨立開機的-console-紀錄寫入-mtdblkfx_log) | 獨立開機的 console 紀錄寫入 mtdblk（fx_log） | 採用（日常開啟） | ADT `fuxi,log=1` 或 `fuxilog=1`，且分區已蓋章；沒有章就不寫任何東西。 |
| [K-020](items/K-kernel.zh-TW.md#k-020-核心在-exec-時注入函式庫fxinj補丁-0118) | 核心在 exec 時注入函式庫（fxinj，補丁 0118） | 採用（日常 `fuxi,inject=2`） | `fxinj=<位元>` 或 ADT `fuxi,inject`（u32，開機時讀一次）。bit 0（1）＝App 主程式（`.../NAME.app/EXECUTABLE`，SpringBoard 除外）注入 `/usr/local/lib/fuxi/libfuxi-app.dylib`；bit 1（2）＝SpringBoard 注入 `/usr/local/lib/fuxi/libfuxi-sb.dylib`；bit 2（4）＝`/usr/sbin/mediaserverd` 注入同一份 `libfuxi-sb.dylib`（震動，K-033）；bit 3（8）＝wifid 依完整路徑注入（Wi-Fi 診斷，K-063）。 |
| [K-021](items/K-kernel.zh-TW.md#k-021-9-gib-ram-配置ram9g-變體) | 9 GiB RAM 配置（ram9g 變體） | 採用（日常） | 建置期變體，不是開機參數：harness `xnu\variants\ram9g.patch`、workflow `kernel-adsp-ram9g.yml`（Intel runner 版 `kernel-adsp-ram9g-intel.yml`）；本機 `build-kernel.ps1 -Ram 9g`。loader：`prepare-ram9g-loader.py --ram-layout 9g`（預設）。 |
| [K-022](items/K-kernel.zh-TW.md#k-022-ram-sku-與實際配置分開) | RAM SKU 與實際配置分開 | 採用 | loader `--ram-sku auto\|8g\|12g`（預設 auto）→ ADT `fuxi,ram-sku`；ios-fuxi 入口的 incoming-FDT guard。 |
| [K-023](items/K-kernel.zh-TW.md#k-023-高位址-ram-樣式測試ramtest) | 高位址 RAM 樣式測試（ramtest） | 實驗（已驗，按需開） | ADT `fuxi,ramtest`；loader `prepare-ram9g-loader.py --ramtest [--ramtest-mib 128\|512]`。 |
| [K-024](items/K-kernel.zh-TW.md#k-024-多段-ram-admissionbank-shadow-與記憶體盤點) | 多段 RAM admission、bank shadow 與記憶體盤點 | 準備（主機模型與資料，核心未用） | 無 |
| [K-025](items/K-kernel.zh-TW.md#k-025-pmic-唯讀盤點flashhaptics) | PMIC 唯讀盤點（flash／haptics） | 採用（診斷） | `fuxipmic=1` 或 ADT `fuxi,pmic=1`；loader `prepare-ram9g-loader.py --pmic-survey`。值大於 3 時拒絕並記 `unsupported mode`。 |
| [K-026](items/K-kernel.zh-TW.md#k-026-手電筒有界脈衝診斷mode-2) | 手電筒有界脈衝診斷（mode 2） | 實驗（保留作診斷） | `fuxi,pmic=2`；loader `--pmic-torch-pulse`。 |
| [K-027](items/K-kernel.zh-TW.md#k-027-手電筒租約控制mode-3與-springboard-放行) | 手電筒租約控制（mode 3）與 SpringBoard 放行 | 採用（日常 ADT `fuxi,pmic=3`，chain-v7 起） | `fuxi,pmic=3`；loader `--pmic-torch-runtime`。 |
| [K-028](items/K-kernel.zh-TW.md#k-028-手電筒失敗追查關閉穩定計時器audit原始拒絕) | 手電筒失敗追查（關閉穩定、計時器、audit、原始拒絕） | 已結案（根因見 K-029） | ADT `fuxi,torchaudit=1`（只在 `fuxi,pmic=3` 時有效）；loader `--torch-audit`。 |
| [K-029](items/K-kernel.zh-TW.md#k-029-pmic-spmi-交易共用仲裁鎖) | PMIC SPMI 交易共用仲裁鎖 | 採用 | 無（PMIC 任一模式） |
| [K-030](items/K-kernel.zh-TW.md#k-030-手電筒常亮) | 手電筒常亮 | 採用 | 無（`fuxi,pmic=3` 的行為） |
| [K-031](items/K-kernel.zh-TW.md#k-031-手電筒調光不重開輸出與五檔) | 手電筒調光不重開輸出與五檔 | 採用（日常 chain-v7 起） | 無 |
| [K-032](items/K-kernel.zh-TW.md#k-032-hap525-震動盤點與有界脈衝) | HAP525 震動盤點與有界脈衝 | 實驗（只讀；已被 K-033 取代為實際震動） | `fuxihaptic=1` 或 ADT `fuxi,haptic=1`；loader `prepare-ram9g-loader.py --haptic-probe`。 |
| [K-033](items/K-kernel.zh-TW.md#k-033-hap525-direct_play-震動執行期) | HAP525 DIRECT_PLAY 震動執行期 | 採用（日常 ADT `fuxi,haptic=2`） | ADT `fuxi,haptic=2`（3＝另加開機自測三下）；`fuxi,haptic-amp=<0..255>`（dylib 讀，見 U 系列）；ADT `fuxi,inject` bit 2 讓 mediaserverd 載入 dylib。loader：`prepare-wlan-w18-loader.py --haptic 2 --inject-media [--haptic-amp 128]`。 |
| [K-034](items/K-kernel.zh-TW.md#k-034-震動-fifo-準備主機模型) | 震動 FIFO 準備（主機模型） | 準備（未接上核心） | 無 |
| [K-035](items/K-kernel.zh-TW.md#k-035-裝置端-ui-測試指令tapstourtapcmd) | 裝置端 UI 測試指令（taps、tour、tapcmd） | 採用（測試工具） | `fuxitaps=<延遲秒>`、`fuxitour=<…>`（`fuxitoura`、`fuxitourg`、`fuxitourhold`）、`fuxitapcmd=<指令>` 或 ADT `fuxi,tapcmd`（字串）；loader `--tap-script <指令> [--tap-script-adt]`、`--tap-delay`。 |
| [K-036](items/K-kernel.zh-TW.md#k-036-rtc-校時adt-fuxirtc-offset) | RTC 校時（ADT fuxi,rtc-offset） | 採用（日常） | `fuxirtcoffset=<秒>`（優先）或 ADT `/chosen fuxi,rtc-offset`（4 bytes LE）；`fuxirtc` 控制 RTC 讀取。loader：`--rtc-calibration <json> [--rtc-adt]`。 |
| [K-037](items/K-kernel.zh-TW.md#k-037-藍牙-geni-fifo-與-ios-16-藍牙-abi-準備) | 藍牙 GENI FIFO 與 iOS 16 藍牙 ABI 準備 | 準備（唯讀盤點已上機） | `fuxibt=1` 或 ADT `fuxi,bt=1`（唯讀盤點）；loader `prepare-ram9g-loader.py --bluetooth-survey` 或 `prepare-wlan-w18-loader.py --bt 1`。 |
| [K-038](items/K-kernel.zh-TW.md#k-038-藍牙五條時脈租約準備) | 藍牙五條時脈租約準備 | 準備（延後） | 無 |
| [K-039](items/K-kernel.zh-TW.md#k-039-藍牙板級上電與-qca-版本查詢fuxibt3) | 藍牙板級上電與 QCA 版本查詢（fuxi,bt=3） | 實驗（已通；韌體下載未做） | ADT `fuxi,bt=3`（或 `fuxibt=3`）；loader `prepare-wlan-w18-loader.py --bt 3`（會檢查核心含 `board WLAN_EN steady after` 字串、命令列沒有 `fuxibt=`）。 |
| [K-040](items/K-kernel.zh-TW.md#k-040-wlan-htc-協定準備主機模型) | WLAN HTC 協定準備（主機模型） | 準備（未接上核心） | 無（`FUXI_HTC_DEFAULT_ACTIVE` 恆為 0） |
| [K-041](items/K-kernel.zh-TW.md#k-041-w18-追蹤與原廠韌體-fw_ready-停損) | W18 追蹤與原廠韌體 FW_READY 停損 | 結論（原廠韌體路線擱置） | `fuxiwlanw18=1`（W18 有界追蹤）；`fuxiwlanmlo=<值>`（P17 選擇器，見 K-044）。loader 產生器 `--mlo`。 |
| [K-042](items/K-kernel.zh-TW.md#k-042-secondary-host_cap) | SECONDARY HOST_CAP | 結論（排除） | `fuxiwlancap=1`；loader 產生器 `--host-cap-secondary`。 |
| [K-043](items/K-kernel.zh-TW.md#k-043-省略-nm_modem-能力) | 省略 nm_modem 能力 | 結論（排除） | `fuxiwlancap=2`；loader 產生器 `--omit-nm-modem`。payload 和 `fuxiwlancap=1` 只差一個字元。 |
| [K-044](items/K-kernel.zh-TW.md#k-044-pmk8550-睡眠時脈與大塊記憶體p17) | PMK8550 睡眠時脈與大塊記憶體（P17） | 結論（睡眠時脈是必要條件，見 K-047；大塊記憶體排除） |  |
| [K-045](items/K-kernel.zh-TW.md#k-045-errdecwlfw-逾時後統計韌體寫過的主機記憶體) | ERRDEC：WLFW 逾時後統計韌體寫過的主機記憶體 | 結論（診斷） | `fuxiwlanmlo` 位元 `0x100`；loader 產生器 `--errdec` 產生 `fuxiwlanmlo=960`（相對第27趟只多診斷輸出）。 |
| [K-046](items/K-kernel.zh-TW.md#k-046-aop-pdc-設定) | AOP PDC 設定 | 結論（排除） | `fuxiwlancnss=pdc`（M6n 階段已有，預設關）；loader 產生器 `--cnss-pdc`。 |
| [K-047](items/K-kernel.zh-TW.md#k-047-上游韌體睡眠時脈wlan_mode-通過) | 上游韌體＋睡眠時脈：WLAN_MODE 通過 | 已驗證（QMI 啟動流程全部完成；資料通路改走 K-049 起的 native ath12k） | `fuxiwlanbdf=bd_up.elf fuxiwlanmlo=192`（不開 SECONDARY、大塊、PDC）；loader 產生器 `--bdf bd_up.elf --sleep-clock on`。 |
| [K-048](items/K-kernel.zh-TW.md#k-048-wlan-bdf-來源盤點) | WLAN BDF 來源盤點 | 準備（工具） | 無 |
| [K-049](items/K-kernel.zh-TW.md#k-049-ath12k-板級轉接層) | ath12k 板級轉接層 | 已驗證（K-052 起實機掃描） | `fuxiwlanath=1`（預設關） |
| [K-050](items/K-kernel.zh-TW.md#k-050-ath12k-模組連結與-ci) | ath12k 模組連結與 CI | 已驗證 | 只在要求時連結（核心 CI 的 opt-in 9g，或本機 `-Ath12k`）。 |
| [K-051](items/K-kernel.zh-TW.md#k-051-隔離-native-imports-與控制通道) | 隔離 native imports 與控制通道 | 已驗證 | 同 K-049 |
| [K-052](items/K-kernel.zh-TW.md#k-052-遵守模組-dma-kfree-擁有權) | 遵守模組 DMA kfree 擁有權 | 已驗證（第一次掃到 BSS） | 同 K-049 |
| [K-053](items/K-kernel.zh-TW.md#k-053-ios-station-後端電源與掃描) | iOS station 後端（電源與掃描） | 已驗證 | `fuxiwifiios=2`；loader 產生器 `--wifi-ios`。 |
| [K-054](items/K-kernel.zh-TW.md#k-054-native-wlan-身分對齊-ios-裝置樹) | native WLAN 身分對齊 iOS 裝置樹 | 已驗證（併入 K-053 那趟） | 同 K-053 |
| [K-055](items/K-kernel.zh-TW.md#k-055-加入開放與-wpa2-psk-網路) | 加入開放與 WPA2-PSK 網路 | 已驗證（11-17 使用者手動加入 WPA2 成功、DHCP 成功） | 加入流程本身沒有開關（跟著 `fuxiwifiios=2`）。自測用 `fuxiwifitest=2 fuxiwifitestssid=<SSID>`（loader 產生器 `--wifi-join-test <SSID>`）。 |
| [K-056](items/K-kernel.zh-TW.md#k-056-ui-測試只列指定網路fuxiwifitest3) | UI 測試只列指定網路（fuxiwifitest=3） | 已驗證（開關生效；腳本點擊仍不可靠） | `fuxiwifitest=3 fuxiwifitestssid=<SSID>`；loader 產生器 `--wifi-ui-only <SSID>`。只用在測試 loader，日常不帶。 |
| [K-057](items/K-kernel.zh-TW.md#k-057-定期送-rssi_changed狀態列訊號格) | 定期送 RSSI_CHANGED（狀態列訊號格） | 已驗證（wifid 的 RSSI／NOISE 查詢有回答） | 無（跟著 `fuxiwifiios=2`） |
| [K-058](items/K-kernel.zh-TW.md#k-058-舊掃描結果加入前只重掃上次頻道) | 舊掃描結果加入前只重掃上次頻道 | 已合入（沒有單獨上機） | 無 |
| [K-059](items/K-kernel.zh-TW.md#k-059-aks-金鑰-wrapunwrap鑰匙圈可存-wi-fi-密碼) | AKS 金鑰 wrap／unwrap（鑰匙圈可存 Wi-Fi 密碼） | 已驗證 | 無（AKS 替身固定提供） |
| [K-060](items/K-kernel.zh-TW.md#k-060-協商掃描停留時間與-fast-掃描快取) | 協商掃描停留時間與 FAST 掃描快取 | 已驗證（110 ms 單頻道掃描成功；快取命中） |  |
| [K-061](items/K-kernel.zh-TW.md#k-061-wifid-呼叫追蹤與被拒掃描參數) | wifid 呼叫追蹤與被拒掃描參數 | 已驗證（診斷） | `fuxiwifitrace=1`（loader 產生器 `--wifi-trace`） |
| [K-062](items/K-kernel.zh-TW.md#k-062-dwell-掃描分批) | dwell 掃描分批 | 已驗證（模組 NULL 合約修正後） | `fuxiwlanbatch=1`（loader 產生器 `--scan-batch`） |
| [K-063](items/K-kernel.zh-TW.md#k-063-wifid-精確路徑注入) | wifid 精確路徑注入 | 已驗證（診斷用） | fxinj bit 8（ADT `fuxi,inject` 的 8），只比對 wifid 的精確路徑。 |
| [K-064](items/K-kernel.zh-TW.md#k-064-回答-rssi_bounds自動加入修好) | 回答 RSSI_BOUNDS（自動加入修好） | 已驗證（使用者確認「wifi 終於會自己連線了」） | ADT `fuxi,wifi-rssi=1`；loader 產生器 `--wifi-rssi-bounds`。 |
| [K-065](items/K-kernel.zh-TW.md#k-065-安靜開機不畫-console-跑馬燈) | 安靜開機（不畫 console 跑馬燈） | 已驗證 | `sweetfb_noconsole=2`；loader 產生器 `--quiet-boot`。 |
| [K-066](items/K-kernel.zh-TW.md#k-066-fuxiwifi-ux電源回報依速率選-ap同台不斷線bit-02) | fuxi,wifi-ux：電源回報、依速率選 AP、同台不斷線（bit 0～2） | 已驗證 | ADT `fuxi,wifi-ux`（位元遮罩，預設 0）；loader 產生器 `--wifi-ux <值>`（會檢查核心有沒有 `FUXI WIFI IOS: ux options` 標記和各位元的字串）。 |
| [K-067](items/K-kernel.zh-TW.md#k-067-國家碼相同電源狀態不重做計時adt-fuxiargsbit-34補丁-0121) | 國家碼、相同電源狀態不重做、計時、ADT fuxi,args（bit 3～4，補丁 0121） | 已驗證（日常 chain-v8 用 ux=31） |  |
| [K-068](items/K-kernel.zh-TW.md#k-068-限制隱含掃描範圍分組停留失敗已回退) | 限制隱含掃描範圍＋分組停留（失敗、已回退） | 放棄 | 當時 `fuxi,wifi-ux` bit 6（ux=127） |
| [K-069](items/K-kernel.zh-TW.md#k-069-掃描中止逾時恢復與公布頻道掃描bit-56) | 掃描中止／逾時恢復與公布頻道掃描（bit 5～6） | 已驗證（實機兩趟；日常還沒合入） |  |
| [K-070](items/K-kernel.zh-TW.md#k-070-nativeadreno-host-import-配額失敗回報-vulkan-oom) | NativeAdreno host-import 配額失敗回報 Vulkan OOM | 已驗證 | 無（Turnip／mesa 補丁） |
| [K-071](items/K-kernel.zh-TW.md#k-071-指定行程的有界取樣sweetprofname) | 指定行程的有界取樣（sweetprofname） | 已驗證（診斷工具） |  |
| [K-072](items/K-kernel.zh-TW.md#k-072-ksrc-基底與維護) | ksrc 基底與維護 | 維護 | 無 |
| [K-073](items/K-kernel.zh-TW.md#k-073-優先處理-wi-fi-關閉批次接收與連線統計bit-79) | 優先處理 Wi-Fi 關閉、批次接收與連線統計（bit 7～9） | 實驗（ux1023 上機一趟；ux767 對照未跑） |  |
| [K-074](items/K-kernel.zh-TW.md#k-074-ath12k-接受韌體配置的-peer-id-0) | ath12k 接受韌體配置的 peer ID 0 | 修正版完成一趟暫時開機驗證，重連後即有資料流量 | 使用含 peer ID 0 修補的 ath12k 物件。對照組與修正版都開啟接收批次（ADT fuxi,wifi-ux=1023），沿用相同命令列與 root extents。 |
| [K-075](items/K-kernel.zh-TW.md#k-075-ath12k-wmi-掃描優先級與首輪掃描時間) | ath12k WMI 掃描優先級與首輪掃描時間 | 修正版暫時開機三輪設定頁關開通過 | 核心按需 Wi-Fi 追蹤 fuxiwifitrace。A/B 使用相同 ADT、命令列與 root extents，變更 ath12k 物件的 WMI priority 欄位。 |
| [K-076](items/K-kernel.zh-TW.md#k-076-wcn7850-藍牙韌體下載與-hci-resetfuxibt4) | WCN7850 藍牙韌體下載與 HCI reset（fuxi,bt=4） | 實驗（已通：韌體下載、HCI reset、讀版本；iOS bluetoothd 傳輸未做） | ADT `fuxi,bt=4`（或 `fuxibt=4`）；loader `prepare-wlan-w18-loader.py --bt 4`（另檢查核心含 `FUXI BT: setup status=` 與 `/usr/local/share/fuxi/bt/` 字串）。韌體要先用 `stage-wlan-assets.py` kind=bt 放進 iOS root（U-020）。 |
| [K-077](items/K-kernel.zh-TW.md#k-077-wcn7850-uartsk-與-skywalk-常駐收送) | WCN7850 UART.SK 與 Skywalk 常駐收送 | 11-19 設定頁關閉再開啟後，Windows 重新連線並即時讀回 5 項 RFCOMM 服務。iOS 另建立過對外 BLE 連線。使用者已配對 Apple 耳機（趟 17），趟 20 換到 3.2 Mbaud 後確認 A2DP 聲音正常。關藍牙閃退與長時間穩定性待驗。 | ADT `fuxi,bt=5`，自測另加 `fuxi,bt-probe=1`；原生 bluetoothd 用 `--bt-native`（另開 `fuxi,inject` 位元 32）。UART 換速 `fuxi,bt-baud`（u32，loader `--bt-baud`）與控制器日誌關閉 `fuxi,bt-nolog`（loader `--bt-nolog`），預設都關（趟 20）。 |
| [K-078](items/K-kernel.zh-TW.md#k-078-用目前-adsp-驗證-fuxi-喇叭與播放圖) | 用目前 ADSP 驗證 Fuxi 喇叭與播放圖 | 21:25 實機建圖六步全部 `status=0`；22:02 PCM（模式 5）實機通過；11-19 mode 6 靜音保護兩趟通過、mode 7 `-30 dB` 1 秒 2 kHz 短測通過。11-20 趟 48–53 已打通 mode 8 下方喇叭與 `FuxiAudio.driver`（v2）即時 PCM 串流（0 功放故障／0 PLL unlock、`sent=4.40/4.43 s`、`writes=220`、顯示／Wi-Fi／BT 無回歸），根映像已就緒於 v2；**使用者實機耳聽驗收（音量、音質、無爆音）與正式 CI 建置待完成**。 | `--audio-tdm-graph`。寫入 ADT `fuxi,audio-spf=4`，預設停用；`--audio-tdm-pcm` 為模式 5。 |
| [K-079](items/K-kernel.zh-TW.md#k-079-wcn7850-原生-softap-個人熱點ap15-ghz-80-mhz-wi-fi-6apple-熱點-iehostapd-退場同步與橋接就緒-dhcp綠標恢復) | WCN7850 原生 SoftAP 個人熱點（ap1、5 GHz 80 MHz Wi-Fi 6、Apple 熱點 IE、hostapd 退場同步與橋接就緒 DHCP／綠標恢復） | 實機驗收通過（Run 4 5 GHz 80 MHz Wi-Fi 6 與 4G 熱點上網通過；Run 6 確認個人熱點雙圓圈圖標生效；Run 7 `ap6` 驗證 `hostapd` 退場同步 6/6 拉起；Run 10 `ap7` 實測 4/4 `exec hostapd`＋4/4 `exec bootpd`、3/3 次「關閉 → 開啟」自動重連喚醒 `bootpd` 並重新亮回狀態欄綠標、3/3 次「開啟 → 關閉」綠標長亮不閃、全程網路通暢且零丟包） | ADT `/chosen fuxi,wifi-ap` 位元遮罩（`1`=啟用 `ap1` SoftAP、`2`=詳細日誌、`4`=啟用 802.11ax HE、`8`=優先 5 GHz 80 MHz 自動回退 2.4 GHz；完整功能 `15`，預設 `0` 關閉）；loader 由 `work/localbuild/hotspot-ap-1122/prepare-hotspot-loader.py --wifi-ap 15` 產生，沿用 `--rtc-persist --rtc-user 1` 與行動數據基底。 |
| [K-080](items/K-kernel.zh-TW.md#k-080-sm8550-titan-gen3-原生相機核心驅動devfuxi-camera前-ov32c後廣角-ov13b10長焦-s5k3k1-三組-i2c-tab-與-16-byte-vfe-wm0-步幅256-kib-dma-guard) | SM8550 Titan Gen3 原生相機核心驅動（`/dev/fuxi-camera`、前 `OV32C`／後廣角 `OV13B10`／長焦 `S5K3K1` 三組 I2C tab 與 16-byte VFE WM0 步幅＋256 KiB DMA guard） | 自動化雙攝 DMA／穩定性與切換實機驗證通過（`Auto-Run 1/3/4/5d`，目前由 `kernel-local-cam1122-iq8b-ath12k-9g-artifact` **`local-wsl (not a CI artifact)`** 承載；僅餘使用者實景人工驗收與後續正式 CI 核心對照，常駐 `boot_a`／`boot_b` 保持未動） | ADT `/chosen` 屬性：`fuxi,camera=7`（預設關閉 `0`）、`fuxi,camera-pll=1`、`fuxi,camera-back-sensor=3`（後攝映射至核心 `sensor 3 ultra-ov13b10`）、`fuxi,camera-tab`（前置 `ov32c-front-m1.tab`，`3264x2448`，6,768 B）、`fuxi,camera-tab-ultra`（超廣／後廣角 `ov13b10-ultra.tab`，`2000x1500`，756 B，182 regs）、`fuxi,camera-tab-tele`（長焦 `s5k3k1-tele.tab`，`3264x2448`，3,664 B，454 regs）；選用除錯／多攝參數 `fuxi,camera-multicam`、`fuxi,camera-mmnoc` 預設均不開啟。核心端實作含 VFE WM0 16-byte 步幅對齊與 256 KiB DMA guard 掃描（`f1..f3 dma_check`）。 |
| [K-081](items/K-kernel.zh-TW.md#k-081-imx800-四鏡頭05x1x32x-零發紫閃光燈補光與持續自動對焦-c-af) | IMX800 四鏡頭（0.5x／1x／3.2x 零發紫）、閃光燈補光與持續自動對焦 C-AF | 實機驗收通過，已合併主分支並納入日常版 B-021（本機 WSL 9g 建置，非 CI 產物） | ADT `/chosen fuxi,camera=7`、`fuxi,camera-pll=1`、`fuxi,camera-back-sensor=1`、`fuxi,camera-multicam=3`、`fuxi,camera-dylib=1`、`fuxi,camera-diag`（預設 0），搭配 4 顆 `.tab` 感測器暫存器表（wide / wide-4k60 / ultra / tele / tele-60 / front）；loader 由 `work/adsp-battery/prepare-aio-loader.py` 組裝 |
| [K-082](items/K-kernel.zh-TW.md#k-082-gpsgnss-定位通道qmi_loc-與-pds-橋接) | GPS／GNSS 定位通道（QMI_LOC 與 PDS 橋接） | 核心 QMI_LOC 與 PDS 通訊已實機驗證並納入日常版 B-021；戶外實際鎖星與 Maps 整合仍待續驗 | ADT `/chosen fuxi,modem-gnss=3`（預設 0） |
| [K-083](items/K-kernel.zh-TW.md#k-083-wi-fi-掃描快取與連線加速ath12k-join-speed) | Wi-Fi 掃描快取與連線加速（ath12k join speed） | 實機掃描與快速連線驗證通過，已合併主分支並納入日常版 B-021 | ADT `/chosen fuxi,wifi-ux=8191`、開機參數 `fuxiwlancache=1` |
| [K-084](items/K-kernel.zh-TW.md#k-084-option-b-核心基礎aks-sel-8233240160slot-7-der-entitlements容器去平台化與網路追蹤過濾0080) | Option B 核心基礎（AKS sel 8/23/32/40/160、Slot 7 DER Entitlements、容器去平台化）與網路追蹤過濾（0080） | 已正式合併至主分支（`ksrc 251339c`/`a00205b`、`harness 2ce4028`，納入 Scheme A `B-022` 29 補丁／63 主機測試）；`test-fuxi-net-trace.py` 測試位址已改為 RFC 5737 `192.0.2.10`，Option B `B-1` 合規 `0xB0` 純 Slot 7 DER 雙端實機驗證（`#462`/`#463`）與對稱安全卸載閉環（`stage-b1-remove-slot7der-b0-1126`）全數通過 | 開機參數 `fuxiuserappnoplat=0\|1`（預設 `0`）、`sweetnettrace=0\|1\|2`（預設 `0`）、`sweet_amfi_debug=0\|1`（預設 `0`，非 `PE_parse_boot_arg`）；ADT `/chosen` 屬性 `fuxi,user-app-noplatform`（u32，預設 `0`）、`fuxi,net-trace-filter`（u32，預設 `0`） |

### U：iOS root 與使用者空間（30 項）

| 編號 | 項目 | 狀態 | 開關 |
|---|---|---|---|
| [U-001](items/U-userland.zh-TW.md#u-001-springboard-形狀函式庫-libfuxi-sbdylib狀態欄打孔區) | SpringBoard 形狀函式庫 libfuxi-sb.dylib（狀態欄打孔區） | 日常使用中（隨 `fuxi,inject=2` 注入 SpringBoard，持續承接手電筒、震動、電池與 Wi-Fi 稽核） |  |
| [U-002](items/U-userland.zh-TW.md#u-002-狀態欄位置uikit-偏好鍵split1080provider與只寫少數區塊的部署) | 狀態欄位置：UIKit 偏好鍵（Split1080／provider）與只寫少數區塊的部署 | 已採用並部分演進（UIKit provider 覆寫改回清除，`FuxiShape.c` 接手 `-notchRect`，其建立的「只寫少數改動區塊」部署鏈沿用至今） |  |
| [U-003](items/U-userland.zh-TW.md#u-003-ios-手電筒控制接上核心租約與五檔亮度) | iOS 手電筒控制接上核心租約與五檔亮度 | 已採用並進入日常（`chain-v7`～`v9` 啟用，控制中心五檔實測通過） |  |
| [U-004](items/U-userland.zh-TW.md#u-004-ios-震動交給核心-hap525-執行期mediaserverdfigvibratormediaexperience) | iOS 震動交給核心 HAP525 執行期（mediaserverd／FigVibrator／MediaExperience） | 部分可用（來電／通知／充電與系統聲音 `VibePattern`／`Atoms` 震動及包絡振幅調整實測通過；UIKit CoreHaptics 互動震動仍因 `-4810` 未解決） |  |
| [U-005](items/U-userland.zh-TW.md#u-005-uikit-觸覺回饋與-corehaptics-初始化診斷) | UIKit 觸覺回饋與 CoreHaptics 初始化診斷 | SpringBoard 回退實機通過。11 月 18 日試裝 U-021 的 HAL 後，SpringBoard 已能建立原生 CoreHaptics 引擎並經 Actuator 播放。Preferences 的個別操作仍待複驗。 |  |
| [U-006](items/U-userland.zh-TW.md#u-006-電池讀取者稽核函式庫) | 電池讀取者稽核函式庫 | 已採用（診斷工具，需時透過 ADT 開關啟用） |  |
| [U-007](items/U-userland.zh-TW.md#u-007-wi-fi-診斷函式庫corewifi-掃描結果過濾候選比對autojoin-決策) | Wi-Fi 診斷函式庫（CoreWiFi 掃描結果、過濾、候選比對、autojoin 決策） | 已採用（診斷工具，成功將無法自動加入縮小至原生資格檢查與密碼查詢） |  |
| [U-008](items/U-userland.zh-TW.md#u-008-原生-wi-fi-決策日誌開關唯讀擷取離線重播) | 原生 Wi-Fi 決策日誌：開關、唯讀擷取、離線重播 | 已採用（提供零風險唯讀分析與離線重播工具鏈） |  |
| [U-009](items/U-userland.zh-TW.md#u-009-ios-時區) | iOS 時區 | 已採用（iOS root 已設定為 `Asia/Taipei`） | `phone-readthrough.py --timezone <AREA/CITY>`（透過 `zone.txt` 傳給 `stage-timezone.sh`） |
| [U-010](items/U-userland.zh-TW.md#u-010-停用-launchd-jobfairplayd-擋-exec-導致日期與時間卡死) | 停用 launchd job（fairplayd 擋 exec 導致日期與時間卡死） | 已採用（已於手機 root 停用 `fairplayd.H2`） | `phone-readthrough.py --disable-job <com.apple....>`（傳入 `job.txt` 給 `stage-launchd-disable.sh`） |
| [U-011](items/U-userland.zh-TW.md#u-011-安裝-32-個原廠內建-app) | 安裝 32 個原廠內建 App | 已採用（32 個原廠 App 已寫入手機 `/Applications`） | `phone-readthrough.py --install-staged-apps` |
| [U-012](items/U-userland.zh-TW.md#u-012-第三方-app-重簽與安裝read-through-stage) | 第三方 App 重簽與安裝（read-through stage） | 已採用（支援將已砸殼 IPA 重簽並安全寫入手機 `/Applications`） |  |
| [U-013](items/U-userland.zh-TW.md#u-013-wlan-韌體與-bdf-放進-ios-root) | WLAN 韌體與 BDF 放進 iOS root | 已採用（WCN7850／`ath12k` 所需資產已放進 `/usr/local/share/fuxi/wlan/`） | `stage-wlan-assets.py` 接受 JSON manifest 與 `--live`（或測試模擬）寫入手機 |
| [U-014](items/U-userland.zh-TW.md#u-014-ios-root-疊加層快照底檔hfs-journal-重播與差分寫入) | iOS root 疊加層、快照底檔、HFS+ journal 重播與差分寫入 | 日常使用中（全專案所有 iOS root 讀寫的基礎儲存層） | `hfs-journal-block-probe.py <IMAGE> [--apply \| --mark-unmounted \| --mark-journaled]` |
| [U-015](items/U-userland.zh-TW.md#u-015-root-資產與-powerlog-唯讀檢查) | root 資產與 powerlog 唯讀檢查 | 日常使用中（所有唯讀狀態／資產／電源暫停診斷的共用工具） |  |
| [U-016](items/U-userland.zh-TW.md#u-016-混合式-turnip-驅動上機stage-turnip-hybrid) | 混合式 Turnip 驅動上機（stage-turnip-hybrid） | 已採用（提供受保護的單檔替換流程，將混合式 Turnip 函式庫部署至手機） | 由 `launch-loader-check.ps1` 與 `phone-readthrough.py` 搭配 `stage-turnip-hybrid.sh` 執行（並於 `prepare-wlan-w18-loader.py` 暴露獨立混合驅動選擇器） |
| [U-017](items/U-userland.zh-TW.md#u-017-safari-網路觸覺與-wi-fi-自動加入的使用者回報對齊) | Safari 網路、觸覺與 Wi-Fi 自動加入的使用者回報對齊 | 部分通過／持續追蹤（Safari 連網與充電震動已由使用者確認；互動震動與當日階段性 Wi-Fi 自動加入對齊結果作為後續修正依據） | 無獨立開關（綜合各診斷趟次實機回饋對齊狀態紀錄） |
| [U-018](items/U-userland.zh-TW.md#u-018-手動加入-wi-fi-時的密碼框等待) | 手動加入 Wi-Fi 時的密碼框等待 | 採用。使用者已在暫時開機的測試版確認密碼框卡頓消失。 | loader 的 --wifi-password-prompt 設定 fuxi,inject bit 4（16），限制到 /Applications/Preferences.app/Preferences。預設關閉。 |
| [U-019](items/U-userland.zh-TW.md#u-019-個人熱點切換後恢復狀態欄圖示) | 個人熱點切換後恢復狀態欄圖示 | 14:32 啟用補查通過單趟驗證。14:53 關閉補查的對照版再現 0 格。15:34 開機的日常合併候選版已通過使用者核對。 | `--wifi-sb-refresh` 設定 ADT `fuxi,wifi-sb` bit 1，預設關閉。可搭配 bit 0 的 `--wifi-sb-audit` 觀察原生旗標。 |
| [U-020](items/U-userland.zh-TW.md#u-020-wcn7850-藍牙韌體放進-ios-rootstage-wlan-assets-kindbt) | WCN7850 藍牙韌體放進 iOS root（stage-wlan-assets kind=bt） | 已部署（`/usr/local/share/fuxi/bt/` 50 檔，寫前比對、寫後讀回通過） | manifest 的 `"kind": "bt"`；輸出目錄名必須是 `run-bt-…`（`run-wlan-…` 給 WLAN），名稱與 kind 不符就拒絕。 |
| [U-021](items/U-userland.zh-TW.md#u-021-從-ios-hal-的-actuator-輸出驅動-hap525) | 從 iOS HAL 的 Actuator 輸出驅動 HAP525 | WSL 建置、主機模型、HFS+ 離線試裝／還原通過。11 月 18 日實機已驗到原生 CoreHaptics、Actuator 與 HAP525 播放。 | loader `--hal-haptics` 寫入 `fuxihalhaptic=1`，搭配 `--haptic 3 --product-haptics`。根映像試驗透過 `launch-loader-check.ps1 -AudioHal enable\|disable -AudioBundle <FuxiAudio.driver> -AudioSha <FuxiAudio SHA-256> -Out <紀錄名>`。 |
| [U-022](items/U-userland.zh-TW.md#u-022-bluetoothd-os_log-轉錄到-consolefuxibt-oslog) | bluetoothd os_log 轉錄到 console（fuxi,bt-oslog） | 已實機驗證（os_log 趟 2 無 crash，轉錄出 VSC stall 的原因） | ADT `/chosen fuxi,bt-oslog`（u32，預設 0）：bit 0 default／error／fault（每程序最多 4000 行），bit 1 info、bit 2 debug（合計最多 1500 行）。需要核心 0118 的注入位元 64（bluetoothd）及 `sweetnull` 含 bluetoothd；loader 產生器 `--bt-oslog <1..7>`，要求 `--bt-native` 與 `--stderr-log` 含 bluetoothd。 |
| [U-023](items/U-userland.zh-TW.md#u-023-bluetoothd-相容修補關藍牙閃退與單實例-ble-廣播fuxibt-fix) | bluetoothd 相容修補：關藍牙閃退與單實例 BLE 廣播（fuxi,bt-fix） | 已實機驗證（趟 32／33：設定頁關→開兩次無閃退，電源循環後 Windows SDP 成功）。11-20 10:01 已併入日常 boot_a（B-013，`--bt-fix 3`，不帶 `--bt-oslog`）。 | ADT `/chosen fuxi,bt-fix`（u32，預設 0）：bit 0 補 H4 傳輸表的 PrepareTerminate；bit 1 讓 bluetoothd 讀到 `SupportsMultipleAdvertising=false`。需要核心 0118 的注入位元 64（bluetoothd）。loader 產生器 `--bt-fix <1..3>`，要求 `--bt-native` 與含 ` bluetoothd gets ` 的核心，並把 `fuxi,inject` 加上 64；不綁 `--bt-oslog`／`--stderr-log`。 |
| [U-024](items/U-userland.zh-TW.md#u-024-mediaserverd-錄影-xpc-服務空回應存根解決錄音後-springboard-主執行緒卡死fuxicapsrc-stub) | mediaserverd 錄影 XPC 服務空回應存根：解決錄音後 SpringBoard 主執行緒卡死（fuxi,capsrc-stub） | 已實機自動驗證（趟 73 `native-hw000-capsrc-autotest-1121` 通過，錄音結束後 SpringBoard 主執行緒觸感與音量條皆正常回應，零回歸）；待使用者實機驗收確認。 | ADT `/chosen fuxi,capsrc-stub`（u32 位元遮罩，預設 0 關閉，有效範圍 `1..15`）：bit 0 `com.apple.coremedia.capturesource`、bit 1 `com.apple.coremedia.capturesession`、bit 2 `com.apple.coremedia.capture.deferred.containermanager.xpc`、bit 3 `com.apple.coremedia.flashlight`。loader 產生器 `--capsrc-stub [MASK]`（預設 `7`，會同時確保 `fuxi,inject` 開啟 bit 2 `mediaserverd` 注入）。 |
| [U-025](items/U-userland.zh-TW.md#u-025-timed-執行期動態-rtc-偏移持久化fuxirtc-user) | timed 執行期動態 RTC 偏移持久化（fuxi,rtc-user） | 已實機驗證（A+B 執行期自測、純 B 從 1970 還原、使用者互動現場捕捉 `calendar_set` 落盤 `gen=4`），已併入日常 `boot_a`（B-016）。 | ADT `/chosen fuxi,rtc-user`（u32，預設 0；`1`=正式持久化與開機還原，`3`=附帶一次性非破壞執行期自測），搭配核心 `fuxi,inject` bit 256（`/usr/libexec/timed`）。loader：`--rtc-user [1..3]`。 |
| [U-026](items/U-userland.zh-TW.md#u-026-springboardpreferences-偽-pre-twiddled-astc-atx-cpu-解碼改走線性解碼修復系統模糊背景區域發紫fuxiatx-fix) | SpringBoard／Preferences 偽 pre-twiddled ASTC `.atx` CPU 解碼改走線性解碼，修復系統模糊背景區域發紫（`fuxi,atx-fix`） | 已由使用者實機目視驗收通過 4 項場景並併入主分支與日常 `boot_a`（B-019）。 | ADT `/chosen fuxi,atx-fix`（u32，預設 0 關閉；`1`=於 `SpringBoard` 與 `Preferences` 啟用），需核心 `fuxi,inject` 開啟 bit 2（`SpringBoard`）。loader 產生器：`--atx-fix 1`。 |
| [U-027](items/U-userland.zh-TW.md#u-027-麥克風錄音品質與鍵盤聽寫修復fuxiaudio-mic-dsp-旁路fuxiaudio-啟動預緩衝4-hz-hpf0x0c08-增益與-fuxibluetoothlog-防崩潰) | 麥克風錄音品質與鍵盤聽寫修復（`fuxi,audio-mic-dsp` 旁路、`FuxiAudio` 啟動預緩衝、4 Hz HPF／`0x0c08` 增益與 `FuxiBluetoothLog` 防崩潰） | 自動化修復、主機測試與實機量化驗證已通過（`boot #363`）；裝置目前保留 `audio-local-dict-v2-artifact`（`3278da66…`）供人工開口實錄／聽寫／無發紫驗收，尚未併入日常常駐 `boot_a`（B-019）。 | ADT `/chosen fuxi,audio-mic-dsp`（u32，預設 0 關閉；`1`=於 `mediaserverd` 將 `d22_bottom_mic_general.austrip` 導向 `d22_bottom_mic_measurement.austrip` 並對 `agc2`/`mstk`/`cfrk` 注入 `bypass=1`）、`fuxi,audio-mic=2`、`fuxi,audio-mic-gain=0x0c08`（`3080`，`adc1=8` +12 dB 類比、`dec0=0x0c` +12 dB 數位）、`fuxi,audio-mic-cfg=1`（`lpass-tx-macro` TX0 4 Hz HPF）。loader 產生器：`--audio-mic-dsp 1 --audio-mic 2 --audio-mic-gain 0x0c08 --audio-mic-cfg 1`。 |
| [U-028](items/U-userland.zh-TW.md#u-028-libfuxi-cameradylib-使用者態原生相機-isp2-stream-前後雙攝與自動化-e2e-交付iq8k-080-iq8b-核心) | `libfuxi-camera.dylib` 使用者態原生相機 ISP、2-stream 前後雙攝與自動化 E2E 交付（`iq8`＋`K-080` `iq8b` 核心） | 自動化主機測試與前後雙攝 E2E 實機驗證已通過（`Auto-Run 2`＋`Auto-Run 5d` `l5`/`auto_e2e` 均 PASS）；已備妥乾淨交付版 `native-cam1122-iq8b-user-loader`（基於 **`local-wsl (not a CI artifact)`** 核心 `K-080` 與 `iq8` HFS dylib 映像，未改動常駐 `boot_a`／`boot_b`），僅留必須人工實景驗收項目。 | ADT `/chosen` 屬性：`fuxi,camera=7`、`fuxi,camera-pll=1`、`fuxi,camera-dylib=1`、`fuxi,camera-back-sensor=3`、`fuxi,sb-ext=1`、`fuxi,atx-fix=1`（系統模糊防發紫）、`fuxi,rtc-persist=1`、`fuxi,rtc-user=1`、`fuxi,camera-tab`／`-ultra`／`-tele`；使用者交付 loader 命令列共 **605 B**（含 `fuxiperfidle=1000`、`fuxiwlan=1`，不含 `fuxitaps=`／`fuxi,tapcmd`／`fuxi,camera-multicam`／`fuxi,camera-mmnoc`）。 |
| [U-029](items/U-userland.zh-TW.md#u-029-控制中心手電筒模組權限過濾與設定清單保護) | 控制中心手電筒模組權限過濾與設定清單保護 | 主機測試與實機驗證通過，已合併主分支並納入日常版 B-021（乾淨版 `sb-local-daily-b021-clean-artifact`） | 併入 `libfuxi-sb.dylib` 常駐載入（隨 SpringBoard 啟動生效） |
| [U-030](items/U-userland.zh-TW.md#u-030-系統應用-class-1-相容矩陣實證與座標定址mobilenoteswebkitgpu三攝拍照錄影photosp1v2-與-measure-進介面全數實機驗證) | 系統應用 Class 1 相容矩陣實證與座標定址（`MobileNotes`、`WebKit.GPU`、三攝拍照錄影、`Photos`、`P1v2` 與 `Measure` 進介面全數實機驗證） | `P0-Core`（`#449` `MobileNotes`）、`P0-Cam`（`#450` 後置三攝＋錄影＋`Photos`）、`B0` / `B0-Folder`（`#451`/`#453` 座標定址）、`B-022 P1`（`#452` `WebKit.GPU`）、`P1v2`（`#458` `Stocks`/`Weather`/`MobileTimer` 純 UIKit 0 crash，不含背景既有系統／extension 項目）與 `Measure (P2-AR)`（`#457` 進介面 `admit(Class1)`×3 / `Measure` 0 crash，不含背景既有系統／extension 項目，取景黑背景受 Metal 特性限制）實機驗證全部結案；註：`#469` 暴露之 `M4-3 v3` `backboardd` 4096 BO 洩漏未入 `B-022` 日常配置 | `--gpu-clients core`（`105 B` 含 8 token：`Maps,com.apple.MapKit.SnapshotService,Weather,Stocks,MobileNotes,MobileTimer,Measure,com.apple.WebKit.GPU`）、`--gpu-quantum 100`、`--display-gamut sRGB`、`--atx-fix 1`、`--display-fit aspect`（`1080×2400` 1:1 座標對應）；`U-026` 永久封鎖 `SpringBoard`、`mediaserverd`、`backboardd` 與 `*poster*`（註：`B-022` 日常配置維持 `--gpu-clients none` 與 `Stick-14`） |

### B：開機鏈、部署與雙系統選單（22 項）

| 編號 | 項目 | 狀態 | 開關 |
|---|---|---|---|
| [B-001](items/B-boot.zh-TW.md#b-001-b-槽開機分流器-chain-與版本史chain-v1v9) | b 槽開機分流器 chain 與版本史（chain-v1～v9） | 備援使用中（2026-11-18 起改由 `boot_a` 常駐 dualboot 選單，b 槽 `chain-v9-dualboot-launching` 僅作備援；切到 b 槽選 Android 會循環） | `build-chain.py` 參數：`--force-linux`（測試用，跳轉內嵌 Android 核心）、`--auto-launch-s <5..60>`（loader 等電腦秒數，預設 5；v8 起 30）、`--payload <native-*-loader>`、`--ram-sku <auto\|8g\|12g>`；編譯巨集 `-DFUXI_CHAIN=1`、`-DFUXI_CHAIN_FORCE_LINUX=1`、`-DFUXI_AUTO_LAUNCH_S=<N>`、`-DFUXI_RAM_9G=1` |
| [B-002](items/B-boot.zh-TW.md#b-002-已驗證部署與還原deploy-verifiedprepare-test) | 已驗證部署與還原（deploy-verified、prepare-test） | 已採用（v5–v9 `boot_b` 部署與失敗自動還原皆由此腳本執行） | `deploy-verified.py <previous> <next> --version <N>=5> [--menu-entry] [--skip-full-boot-backup]`；`prepare-test.py <chain> <native-*-loader>` |
| [B-003](items/B-boot.zh-TW.md#b-003-loader-的-ram-sku-預檢與-ram-入口防護) | loader 的 RAM SKU 預檢與 RAM 入口防護 | 已採用（主機端 preflight 與 XNU loader 入口 `loader_check_ram` 皆已啟用） | `fuxi,ram-sku=<12>`（ADT `/chosen` u32）；`--ram-sku <auto\|8g\|12g>`（`build-chain.py`）；`FUXI_RAM_9G=1`（區分 5g／9g 配置）；`loader_check_ram(fdt)` 回傳碼 `FuxiRamProfileResult`（0=`StaticOk12G`, 7=`SkuMissing`, 8=`SkuUnsupported`, 9=`Conflict` 等） |
| [B-004](items/B-boot.zh-TW.md#b-004-雙系統開機選單boot_a-常駐) | 雙系統開機選單（boot_a 常駐） | 日常使用中（`boot_a` 已常駐 `menu-launching-20261118`，Android、iOS、Recovery 三條路徑皆已在安裝後實機驗收） | `dualboot/build.py`：`--output <work/dualboot/...>`、`--host-only`、`--base-chain <目錄>`（預設 `chain-v7-ram9g-rtctorch`）、`--fdt <dtb>`、`--default <android\|ios>`（編譯 `MENU_DEFAULT_IOS`）、`--timeout <秒>`（編譯 `MENU_TIMEOUT_MS`，歷史版預設 0，現行版預設 10，見 B-011）；`launch-loader-check.ps1`：`-DualBootCheck`、`-InstallPersistent`、`-UpgradePersistent`、`-ValidatePersistent` |
| [B-005](items/B-boot.zh-TW.md#b-005-移除-ksu-模組檢查移到主機) | 移除 KSU 模組、檢查移到主機 | 已採用（手機上 `/data/adb/modules/fuxi_ios_boot` 已封存並移除，檢查改由電腦端 `dualboot/phone-check.py` 執行） | `phone-check.py` 函式 `verify_ios_root()` 與 `remove_ksu_module()` |
| [B-006](items/B-boot.zh-TW.md#b-006-ksu-開機模組-fuxi_ios_boot已移除) | KSU 開機模組 fuxi_ios_boot（已移除） | 已停用／已從手機移除（封存於 `D:\xiaomi13\fuxi_Backup\ksu-module-remove-ksu-module-20261118\`；主機端保留參考 binary 與 `expected-fiemap.txt`） | `build-module.py <chain-vN>`；手機端點擊 KSU「執行」（`action.sh`） |
| [B-007](items/B-boot.zh-TW.md#b-007-日常鏈合併chain-v7chain-v8chain-v9) | 日常鏈合併（chain-v7、chain-v8、chain-v9） | 已採用（日常已合併至 `boot_a` 常駐 `menu-launching-20261118`＋`boot_b` `chain-v9` 備援） | 依 `comparison.json` 記錄之 `chain-v8` 差異：命令列移除 `debug=12`、新增 `fuxiwlan=1`、`sweetfb_noconsole` `1->2`、`sweetnull` 改為 `backboardd,SpringBoard,wifid`；ADT 新增 `fuxi,adsp-wlan=1`、`fuxi,args="fuxiwlanath=1 fuxiwlanmlo=192 fuxiwifiios=2 fuxiwlandwell=1 fuxiwlanbatch=1"`、`fuxi,audio-spf=1`、`fuxi,haptic=2`、`fuxi,haptic-amp=128`、`fuxi,haptic-env=1`、`fuxi,haptic-ui=15`、`fuxi,inject` `2->14`、`fuxi,wifi-rssi=1`、`fuxi,wifi-sb=1`、`fuxi,wifi-ux=31`、`/product/artwork-display-gamut` `P3->sRGB`、`/product/haptics` 與 `/wlan` 節點。 |
| [B-008](items/B-boot.zh-TW.md#b-008-獨立開機-console-紀錄的主機端mtdblk-擷取與解析) | 獨立開機 console 紀錄的主機端（mtdblk 擷取與解析） | 日常使用中（每次 `launch-loader-check.ps1` 或 `start-daily.py` 進 iOS 前皆自動拉回上一趟紀錄） | `fuxi-log.sh <survey\|stamp\|pull>`；`fuxi-log-parse.py <area.bin> <out.txt>`；ADT `fuxi,log=1`（核心端見 K-019） |
| [B-009](items/B-boot.zh-TW.md#b-009-上機前差分備份工具) | 上機前差分備份工具 | 已採用（用於建立 `ios-root-hw000-*` 等多層與單層差分還原點） | `hw000-delta-backup.py --backup-base <raw> --backup-base-sha <sha256> --name <暫存名> --keep-delta <新備份目錄> [--parent-snapshot <父還原點>] [--ntfs-compress] [--resume-download]` |
| [B-010](items/B-boot.zh-TW.md#b-010-loader-安全核對) | loader 安全核對 | 日常使用中（每次上機前強制核對 iOS 映像 FIEMAP、pin 狀態與 loader ADT extents） | `verify-native-map.py`（無參數）；`verify-loader-safety.py <loader 目錄名>`；`verify-args-on-loader-payload.py <loader 目錄名>`；`check-pin-readonly.c`（`check-pin-readonly <檔案...>`） |
| [B-011](items/B-boot.zh-TW.md#b-011-a-槽選單顯示倒數10-秒後自動進-android) | a 槽選單顯示倒數，10 秒後自動進 Android | 已寫入 a 槽，Android 自動交接與讀回通過 | dualboot/build.py 的 --timeout 預設 10 秒、0 等待手動選擇、1–60 秒自動倒數。--default 改初始焦點，逾時固定選 Android。 |
| [B-012](items/B-boot.zh-TW.md#b-012-日常-a-槽整合-wi-fi-修正與前置鏡頭對齊) | 日常 a 槽整合 Wi-Fi 修正與前置鏡頭對齊 | 日常 a 槽已寫入並讀回。10 秒自動進 Android 已在常駐版驗證。 | 從 B-011 保留 10 秒倒數。ADT 採用 `fuxi,sbshape=300103`、`fuxi,wifi-ux=2047`、`fuxi,inject=30`、`fuxi,wifi-sb=3`。 |
| [B-013](items/B-boot.zh-TW.md#b-013-日常-a-槽整合原生藍牙關藍牙修補與-safari-影片前置) | 日常 a 槽整合原生藍牙、關藍牙修補與 Safari 影片前置 | 日常 a 槽已寫入並讀回，10 秒自動進 Android 在常駐版通過。10:47 確認使用者從常駐選單進 iOS（第 174 次開機）、在設定頁關→開藍牙 5 輪、回 Android，都通過。 | 保留 B-012 的倒數、狀態欄高度 103 與 Wi-Fi 開關。新增原生藍牙：`fuxi,bt=5`、`--bt-native`（`sweetnoexec` 不再擋 bluetoothd）、IBS、`bt-optional`、`bt-pad`、`bt-mask`、`bt-txmask`、3.2 Mbaud、`bt-nolog`、控制器身分節點，以及 U-023 的 `fuxi,bt-fix=3`、`fuxiprivs=1`、disp0 `fuxi,mfb-align=1`。`fuxi,inject` 從 30 改為 126。不帶 `fuxi,bt-oslog`、`fuxi,bt-link-log`、點擊腳本或 bluetoothd stderr。 |
| [B-014](items/B-boot.zh-TW.md#b-014-日常版全寬填滿與前鏡頭狀態欄間距) | 日常版全寬填滿與前鏡頭狀態欄間距 | 日常 iOS payload 已暫時開機；校正版已寫入常駐 boot_a，寫後整區讀回及選單倒數回 Android 通過。 | B-013 的完整指令加 `--display-fit fill --statusbar-width 360`，並將 `--statusbar-height 103` 改為 `86`。日常維持 mode 2 震動，命令列仍為原來的 604 bytes。 |
| [B-015](items/B-boot.zh-TW.md#b-015-以面板比例排版的日常版) | 以面板比例排版的日常版 | 日常 payload 暫時開機通過，鍵盤、狀態欄與滑動已實機驗收。已安裝至 boot_a，整區讀回與常駐選單回 Android 均通過。 | `--display-fit aspect` 設定 ADT `/chosen fuxi,fit=3` 及 disp0 的 `canvas-height=2500`。保留 B-014 的狀態欄 360×86、震動 mode 2、Wi-Fi、藍牙與原 604-byte 命令列。 |
| [B-016](items/B-boot.zh-TW.md#b-016-動態-rtc-校時日常版persisttimeats_2-與-timed-持久化) | 動態 RTC 校時日常版（persist:/time/ats_2 與 timed 持久化） | 方案 A、A+B 自測、純 B 獨立還原、使用者互動檢視與日常 payload 暫時開機全數通過；已安裝至 `boot_a`，整區讀回與常駐選單倒數回 Android 均通過。 | `--rtc-persist --rtc-user 1`（ADT `/chosen fuxi,rtc-persist=1`、`fuxi,rtc-user=1`、`fuxi,inject=382`），移除靜態 `--rtc-calibration`／`fuxi,rtc-offset`。完整保留 B-015 的等比 `1125×2500`（`fuxi,fit=3`）、狀態欄 `360×86`、震動 mode 2、Wi-Fi、原生藍牙（`bt-fix=3`）、ADSP 與 604-byte 命令列。 |
| [B-017](items/B-boot.zh-TW.md#b-017-音訊麥克風震動右側音量條整合日常版合併-b-016-與-hap6f-m6-原點) | 音訊／麥克風／震動／右側音量條整合日常版（合併 B-016 與 hap6f-m6 原點） | 已建立乾淨主分支與子樹基準點（`checkpoint-hap6f-m6-20261122`），並完成合併日常 payload 暫時開機、雙系統選單 RAM Android 交接與 `boot_a` 常駐升級（整區讀回與 10 秒倒數回 Android 全過）。 | 完整保留 B-016 全部參數（`--rtc-persist --rtc-user 1`、`fuxi,inject=382`、`fuxi,sbshape=360086`、`fuxi,fit=3`、背光 `fuxibacklight=4`、手電筒 `fuxi,pmic=3`、Wi-Fi、原生藍牙 `bt-fix=3`＋真實 MAC、ADSP、604-byte 靜音命令列），疊加音訊／麥克風／震動／音量條參數：`--audio-tdm-live --amp 8 --audio-gain 14 --audio-max-db -8 --audio-chime 0 --audio-idle-pdn 600 --audio-mic 2 --haptic 9 --haptic-rtp-vmax 163 --haptic-damp 6 --hal-haptics-adt 6 --haptic-fast-apid --product-haptics-3p --volhud 238128 --sb-ext --capsrc-stub 7`。 |
| [B-018](items/B-boot.zh-TW.md#b-018-手電筒零轉子喇叭音量防靜音誤喚醒與-hap8-內核日常原點日常維持-damp6) | 手電筒零轉子、喇叭音量／防靜音誤喚醒與 `hap8` 內核日常原點（日常維持 `damp=6`） | 已建立乾淨三倉庫基準點（`checkpoint-hap8-m6-20261122`），並完成合併日常 payload 暫時開機、雙系統選單 RAM Android 交接與常駐 `boot_a` 升級（整區讀回與 10 秒倒數回 Android 全過）。 | 完整保留 B-017 全功能（`--rtc-persist --rtc-user 1`、`fuxi,inject=382`、`fuxi,sbshape=360086`、`fuxi,fit=3`、背光 `fuxibacklight=4`、手電筒 `fuxi,pmic=3`、Wi-Fi、原生藍牙 `bt-fix=3`＋真實 MAC、ADSP、右側音量條 `volhud=238128`、`sb-ext`、`capsrc-stub=7`、604-byte 靜音命令列與好評的 `--haptic-damp 6`），調整音訊參數為 `--audio-gain 16 --audio-max-db -4 --audio-chime 0 --audio-idle-pdn 1500 --audio-mic 1`；核心內建 `mode 6/7/8` 可由後續 loader 免重編切換。 |
| [B-019](items/B-boot.zh-TW.md#b-019-系統去紫u-0264gvolte-行動數據與個人熱點k-079-ap7整合日常版) | 系統去紫（U-026）、4G／VoLTE 行動數據與個人熱點（K-079 `ap7`）整合日常版 | 已於乾淨隔離整合工作樹建立並通過 48 項核心主機測試＋454 項雙系統選單斷言，完成全子系統 RAM 共存試開、雙系統選單 RAM Android 交接與常駐 `boot_a` 升級（全區讀回與 10 秒倒數回 Android 全過）。 | 以 `verify-daily-loader.py` 逐位元核對完整保留 B-018 及 U-026 去紫全部 44 項 ADT `/chosen` 屬性（含 `--atx-fix 1`、`--rtc-persist --rtc-user 1`、`fuxi,inject=382`、`fuxi,sbshape=360086`、`fuxi,fit=3`、背光 `fuxibacklight=4`、手電筒 `fuxi,pmic=3`、Wi-Fi、原生藍牙 `bt-fix=3`＋真實 MAC、ADSP、CS35L41 `--audio-tdm-live --amp 8 --audio-gain 16 --audio-max-db -4 --audio-chime 0 --audio-idle-pdn 1500 --audio-mic 1`、HAP525 `--haptic 9 --haptic-rtp-vmax 163 --haptic-damp 6 --hal-haptics-adt 6 --haptic-fast-apid --product-haptics-3p`、右側音量條 `volhud=238128`、`sb-ext`、`capsrc-stub=7`），精準疊加 6 項行動數據／VoLTE／熱點開關：`fuxi,modem=3`、`fuxi,modem-ipa=6131`、`fuxi,modem-bb=0x7eb (2027)`、`fuxi,modem-tftp=1`、`fuxi,audio-voice=4`、`fuxi,wifi-ap=15`（另注入 `/baseband` 節點，命令列放行 `vmd` 維持 600 bytes ≤ 607）。 |
| [B-020](items/B-boot.zh-TW.md#b-020-大一統整合日常版震動交響樂斷音修復錄音聽寫原生相機感測器指南針wda-時序與關熱點-wi-fi-穩定性修復) | 大一統整合日常版（震動交響樂／斷音修復、錄音聽寫、原生相機、感測器／指南針、WDA 時序與關熱點 Wi-Fi 穩定性修復） | 已於乾淨隔離樹完成 `v1 → v2` 實機 60 分鐘看門狗人工驗收並回合併入主分支 `ksrc`（`7f7b2857`）、`harness`（`686f486a`）與工作區 `main`，支援 `.sensor_pack` 雙系統選單連結並通過 454 項斷言與 RAM 試開，已正式寫入常駐 `boot_a`（整區讀回與 10 秒倒數回 Android 全過，保留 B-019 完整回退映像）。 | 完整保留 B-019 全部 50 項 ADT `/chosen` 參數（含 U-026 `--atx-fix 1`、`--rtc-persist --rtc-user 1`、`fuxi,fit=3`、背光 `fuxibacklight=4`、`fuxi,pmic=3`、Wi-Fi、原生藍牙 `bt-fix=3`＋真實 MAC、ADSP、CS35L41、VolHUD `volhud=238128`、`sb-ext`、4G＋VoLTE `fuxi,modem=3`/`fuxi,modem-ipa=6131`/`fuxi,modem-bb=2027`/`fuxi,modem-tftp=1`/`fuxi,audio-voice=4`、K-079 `fuxi,wifi-ap=15`），並整合收斂：(1) `hap9e` Mode 9 v2.2 震動（`fuxi,haptic=9`、`fuxi,haptic-rtp-vmax=163`、`fuxi,haptic-damp=9`、`fuxi,hal-haptic=6`、`fuxi,haptic-ui=15`、`fuxi,haptic-amp=128`）；(2) U-027 麥克風 DSP bypass 與增益（`fuxi,audio-mic=2`、`fuxi,audio-mic-gain=3080 (0x0c08)`、`fuxi,audio-mic-cfg=1`、`fuxi,audio-mic-dsp=1`、`FuxiAudio.driver` `MIC_PRIME_FRAMES=1440`／4ch VoLTE）；(3) K-080＋U-028 原生相機（`fuxi,camera=7`、`fuxi,camera-pll=1`、`fuxi,camera-dylib=1`、`fuxi,camera-back-sensor=3`、`fuxi,camera-tab`／`-ultra`／`-tele`，並於啟用 `--camera-dylib` 時自動移除 `fuxi,capsrc-stub` 以退讓偽造相機且維持 `fuxi,inject=382`）；(4) 感測器／指南針 v8（`fuxi,sensors=255`、`fuxi,sensors-compass-mode=0`、`fuxi,sensors-mag-bias` 與 `.sensor_pack` `0xe8800000`）；(5) Adreno 500 ms inline present 與 `0109` 背光校準。不含 `fuxi,tapcmd`、`fuxi,sensors-test-rot`、`fuxi,camera-multicam`、`fuxi,camera-mmnoc` 除錯旗標；命令列維持 600 bytes（≤ 607）。 |
| [B-021](items/B-boot.zh-TW.md#b-021-雙系統日常常駐-boot_a-升級25-補丁主分支imx800-四鏡頭-c-af閃光燈gnsswi-fi-加速gpu-class-0-1280-mib120-hz-te手電筒控制中心修復暫緩-maps) | 雙系統日常常駐 boot_a 升級（25 補丁主分支：IMX800 四鏡頭 C-AF／閃光燈、GNSS、Wi-Fi 加速、GPU Class 0 1280 MiB／120 Hz TE、手電筒控制中心修復；暫緩 Maps） | 已安全升級至日常常駐雙系統 `boot_a`（槽位 `_a`、`UPGRADED_ANDROID_VERIFIED`；Android 10 秒倒數、回退映像與 5 個保護分區完好；日常短測通過；Maps 依使用者指示暫緩排除於本版） | 常駐雙系統選單 `--default android --timeout 10`（iOS `--auto-launch-s 30`）；iOS loader 由 `work/adsp-battery/prepare-aio-loader.py` 組裝，啟用 `4` 顆鏡頭 `.tab`、`fuxi,camera=7`、`fuxi,camera-back-sensor=1`、`fuxi,camera-multicam=3`、`fuxi,camera-dylib=1`、`fuxi,modem-gnss=3`、`fuxi,wifi-ux=8191`、`fuxiwlancache=1`、`fuxi,gpu-bb-recover=3`、`--gpu-clients none`（關閉 Class 1 Maps）、`--atx-fix 1`、`--statusbar-width 360 --statusbar-height 86`（`360086`）、`--display-fit aspect`、`--rtc-persist --rtc-user 1`、命令列 `602 B <= 607 B`（無 `--tap-script`、無 `fuxi,capsrc-stub`） |
| [B-022](items/B-boot.zh-TW.md#b-022-雙系統日常常駐-boot_a-升級scheme-a-29-補丁主分支wave-2-pure-120hzm4-2-probe_0x4eoption-b-013101330080poster-硬防護與系統-app-矩陣maps-class-1-預設關閉並排除隔離實驗) | 雙系統日常常駐 boot_a 升級（Scheme A 29 補丁主分支：Wave 2 Pure 120Hz、M4-2 probe_0x4e、Option B 0131–0133/0080、*poster* 硬防護與系統 App 矩陣；Maps Class 1 預設關閉並排除隔離實驗） | Scheme A（29 補丁／63 主機測試）已正式合併主分支雙倉（`ksrc a00205b`、`harness 2ce4028`、`dirty=0`）並重建乾淨 9g 核心、日常 Loader（`--gpu-clients none`）與雙系統選單（`454 assertions`）；實機煙霧（`#470`）、RAM Android 交接與常駐 `boot_a` 升級：`-r2` 達成 `INSTALLED_ANDROID_VERIFIED`（槽位 `_a`、整區 `boot_a` `0e1e496b6e92b83160e8c5d7b6604589f270c6060ed5b687e64b2579922ba9d9`、`B-021` 回退映像 `6b88c3e5…` 已保存、5 個保護分區 SHA-256 零異動、`lock_state="released"`；首趟因本機 `adb` TCP 5037 daemon 瞬斷安全回退並補齊 `phone-check.py` 重試，隔離實驗 `#464`–`#469` 未併入本版，留待後續 `B-023`） | 沿用 `B-021` 全套日常開關並整合純 Wave 2（`fuxi,gpu-locksplit=1`、`fuxi,nopoll=1`、`fuxi,gpu-irqslice=8000`）及 Option B 預設關閉開關（`fuxi,user-app-noplatform=0`、`fuxi,net-trace-filter=0`）；日常 Loader 由 `work/adsp-battery/prepare-aio-loader.py` 組裝，設定 `--gpu-clients none`（關閉 Class 1 Maps，亦未啟用 `--gpu-c1-bos`）、`--atx-fix 1`、`--display-gamut sRGB`、`--rtc-persist --rtc-user 1`、`--statusbar-width 360 --statusbar-height 86`（`360086`）、`--display-fit aspect`、`--gpu-bb-recover 3`、`--modem-gnss 3`、`--wifi-ux 8191`，無 `--tap-script` / 無 `fuxitaps`，命令列 `602 B <= 607 B`、`private.adt` `71,764 B <= 131,072 B` |

### T：工具、建置與流程（61 項）

| 編號 | 項目 | 狀態 | 開關 |
|---|---|---|---|
| [T-001](items/T-tools.zh-TW.md#t-001-工具-scriptsfuxi_envpy) | 工具 `scripts/fuxi_env.py` | 工具：供多數 `scripts/` 與根目錄 Python 腳本匯入使用 | `FUXI_WORKSPACE`、`FUXI_DEVICE_SERIAL`、`FUXI_NDK_BIN`、`FUXI_ADB`、`FUXI_FASTBOOT`、`fuxi_env` |
| [T-002](items/T-tools.zh-TW.md#t-002-工具-scriptssetup_upstreampy) | 工具 `scripts/setup_upstream.py` | 工具：寫進 `README.md` Step 1 作為初始建置入口 | 無 |
| [T-003](items/T-tools.zh-TW.md#t-003-工具-scriptsbuild_diag_loaderpy) | 工具 `scripts/build_diag_loader.py` | 工具：產出 `fuxi-diag-ram.img` 或 `fuxi-ios16-root.img`（Track 1 仍引用） | `rd=md0` |
| [T-004](items/T-tools.zh-TW.md#t-004-工具-scriptsbuild_fuxi_loader_winpy) | 工具 `scripts/build_fuxi_loader_win.py` | 工具：編譯 `fuxi-loader` 並與 `payload27-dada.bin` 打包成 `fuxi-xnu-boot.img` | 無 |
| [T-005](items/T-tools.zh-TW.md#t-005-工具-scriptsbuild_ios16_root_guestsh) | 工具 `scripts/build_ios16_root_guest.sh` | 工具：從只讀 `vda/vdb/vdc` 建置 `vdd` 20 GiB iOS 16 root 映像 | 無 |
| [T-006](items/T-tools.zh-TW.md#t-006-工具-scriptsprepare_ios16_perf_payloadpy) | 工具 `scripts/prepare_ios16_perf_payload.py` | 工具：由 `ios16-production-payload.bin` 附加 `fuxiperf=1 fuxicpuload=10` | `fuxiperf=1`、`fuxicpuload=10` |
| [T-007](items/T-tools.zh-TW.md#t-007-工具-scriptsstream_ios16_candidatepy) | 工具 `scripts/stream_ios16_candidate.py` | 工具：經 ADB 轉發寫入 `write-pinned-extents` 並記錄寫入日誌 | 無 |
| [T-008](items/T-tools.zh-TW.md#t-008-工具-scriptsverify_ios16_candidatepy) | 工具 `scripts/verify_ios16_candidate.py` | 工具：寫入後逐 extent `dd` 讀回比對並輸出 `ios16-candidate-readback.json` | 無 |
| [T-009](items/T-tools.zh-TW.md#t-009-工具-scriptsread_root27_backuppy) | 工具 `scripts/read_root27_backup.py` | 工具：依 `f2fs_io-codex` extent 表讀回 8 GB 映像至 `candidates/recovered-root27/` | 無 |
| [T-010](items/T-tools.zh-TW.md#t-010-工具-scriptscapture_probe_framebufferpy) | 工具 `scripts/capture_probe_framebuffer.py` | 工具：不重開機自 USB `0x4e` 讀取 `1080x2400` BGRA 並存成 PNG | 無 |
| [T-011](items/T-tools.zh-TW.md#t-011-工具-scriptsinspect_devicepy) | 工具 `scripts/inspect_device.py` | 工具：印出 `monitor_info` 11 欄位與 `kernel_stage` | 無 |
| [T-012](items/T-tools.zh-TW.md#t-012-工具-scriptsstream_hostpy) | 工具 `scripts/stream_host.py` | 工具：早期 iOS 27 root 映像串流工具，已被 Track 1 之 `stream_ios16_candidate.py` 取代 | 無 |
| [T-013](items/T-tools.zh-TW.md#t-013-工具-scriptspatch_kernel_guardpy) | 工具 `scripts/patch_kernel_guard.py` | 工具：早期 `xnu-12377` 靜態二進位補丁腳本（歷史工具） | 無 |
| [T-014](items/T-tools.zh-TW.md#t-014-工具-scriptspatch_kernel_guard_astpy) | 工具 `scripts/patch_kernel_guard_ast.py` | 工具：早期 `xnu-12377` 靜態二進位補丁腳本（歷史工具） | 無 |
| [T-015](items/T-tools.zh-TW.md#t-015-工具-scriptspatch_kernel_inheritancepy) | 工具 `scripts/patch_kernel_inheritance.py` | 工具：早期 `xnu-12377` 靜態二進位補丁腳本（歷史工具） | 無 |
| [T-016](items/T-tools.zh-TW.md#t-016-工具-scriptspatch_kernel_lookuppy) | 工具 `scripts/patch_kernel_lookup.py` | 工具：早期 `xnu-12377` 靜態二進位補丁腳本（歷史工具） | 無 |
| [T-017](items/T-tools.zh-TW.md#t-017-工具-scriptspatch_kernel_registerpy) | 工具 `scripts/patch_kernel_register.py` | 工具：早期 `xnu-12377` 靜態二進位補丁腳本（歷史工具） | 無 |
| [T-018](items/T-tools.zh-TW.md#t-018-工具-scriptspatch_kernel_boot17py) | 工具 `scripts/patch_kernel_boot17.py` | 工具：早期 `xnu-12377` 靜態二進位補丁腳本（歷史工具） | 無 |
| [T-019](items/T-tools.zh-TW.md#t-019-工具-scriptspatch_kernel_trampolinepy) | 工具 `scripts/patch_kernel_trampoline.py` | 工具：早期 `xnu-12377` 靜態二進位補丁腳本（歷史工具） | 無 |
| [T-020](items/T-tools.zh-TW.md#t-020-工具-scriptspatch_kernel_boot24py) | 工具 `scripts/patch_kernel_boot24.py` | 工具：早期 `xnu-12377` 靜態二進位補丁腳本（歷史工具） | 無 |
| [T-021](items/T-tools.zh-TW.md#t-021-工具-scriptspatch_kernel_boot25py) | 工具 `scripts/patch_kernel_boot25.py` | 工具：早期 `xnu-12377` 靜態二進位補丁腳本（歷史工具） | 無 |
| [T-022](items/T-tools.zh-TW.md#t-022-工具-scriptspatch_kernel_boot26py) | 工具 `scripts/patch_kernel_boot26.py` | 工具：早期 `xnu-12377` 靜態二進位補丁腳本（歷史工具） | 無 |
| [T-023](items/T-tools.zh-TW.md#t-023-工具-scriptspatch_kernel_boot27py) | 工具 `scripts/patch_kernel_boot27.py` | 工具：含 `far: 0x100` 錯誤之歷史補丁腳本，已被 `patch_kernel_boot28.py` 取代 | 無 |
| [T-024](items/T-tools.zh-TW.md#t-024-工具-scriptspatch_kernel_boot28py) | 工具 `scripts/patch_kernel_boot28.py` | 工具：早期 `xnu-12377` 靜態二進位補丁腳本（歷史工具） | 無 |
| [T-025](items/T-tools.zh-TW.md#t-025-工具-scriptspatch_loader_cpy) | 工具 `scripts/patch_loader_c.py` | 工具：已由 `remove_fake_logo.py` 撤銷之歷史腳本 | 無 |
| [T-026](items/T-tools.zh-TW.md#t-026-工具-scriptsupdate_loader_flawlesspy) | 工具 `scripts/update_loader_flawless.py` | 工具：已由 `remove_fake_logo.py` 撤銷之歷史腳本 | 無 |
| [T-027](items/T-tools.zh-TW.md#t-027-工具-scriptsremove_fake_logopy) | 工具 `scripts/remove_fake_logo.py` | 工具：執行後清除 `loader.c` 內 `draw_apple_logo` 與 `apple_boot_asset.h` | 無 |
| [T-028](items/T-tools.zh-TW.md#t-028-工具-scriptsupdate_logpy) | 工具 `scripts/update_log.py` | 工具：一次性文件更新腳本（含舊使用者路徑），已停用 | 無 |
| [T-029](items/T-tools.zh-TW.md#t-029-工具-scriptsdo_bootpy) | 工具 `scripts/do_boot.py` | 工具：早期 `xnu-12377` 開機腳本（已被 `do_boot29_win.py` 等取代） | 無 |
| [T-030](items/T-tools.zh-TW.md#t-030-工具-scriptsdo_boot29py) | 工具 `scripts/do_boot29.py` | 工具：早期 `xnu-12377` 開機腳本 | 無 |
| [T-031](items/T-tools.zh-TW.md#t-031-工具-scriptsdo_boot29_winpy) | 工具 `scripts/do_boot29_win.py` | 工具：被 `boot_ios_monitor.bat` 呼叫 | 無 |
| [T-032](items/T-tools.zh-TW.md#t-032-工具-scriptsdo_boot30_winpy) | 工具 `scripts/do_boot30_win.py` | 工具：早期 `xnu-12377` Windows 開機測試腳本（不確定是否仍在用） | 無 |
| [T-033](items/T-tools.zh-TW.md#t-033-工具-scriptsauto_stagepy) | 工具 `scripts/auto_stage.py` | 工具：早期自動化輪詢腳本（不確定是否仍在用） | `FUXI_DEVICE_SERIAL` |
| [T-034](items/T-tools.zh-TW.md#t-034-工具-scriptswait_for_fastboot_and_bootpy) | 工具 `scripts/wait_for_fastboot_and_boot.py` | 工具：早期 Boot 26 專用調度腳本 | 無 |
| [T-035](items/T-tools.zh-TW.md#t-035-工具-scriptswatch_and_boot27py) | 工具 `scripts/watch_and_boot27.py` | 工具：早期 Boot 27 專用調度腳本 | 無 |
| [T-036](items/T-tools.zh-TW.md#t-036-工具-scriptswatch_and_boot28py) | 工具 `scripts/watch_and_boot28.py` | 工具：早期 Boot 28 專用調度腳本 | 無 |
| [T-037](items/T-tools.zh-TW.md#t-037-工具-scriptswatch_and_boot29py) | 工具 `scripts/watch_and_boot29.py` | 工具：早期 Boot 29 專用調度腳本 | 無 |
| [T-038](items/T-tools.zh-TW.md#t-038-工具-scriptsboot_and_drive_userlandpy) | 工具 `scripts/boot_and_drive_userland.py` | 工具：早期 `xnu-12377` 使用者態圖形驅動測試腳本 | 無 |
| [T-039](items/T-tools.zh-TW.md#t-039-工具-scriptsboot_and_verify_logopy) | 工具 `scripts/boot_and_verify_logo.py` | 工具：已過時（假 Apple Logo 已移除） | 無 |
| [T-040](items/T-tools.zh-TW.md#t-040-工具-scriptsboot_clean_verifypy) | 工具 `scripts/boot_clean_verify.py` | 工具：早期驗證腳本（不確定是否仍在用） | 無 |
| [T-041](items/T-tools.zh-TW.md#t-041-工具-根目錄-boot_iosps1boot_iosbat) | 工具 根目錄 `boot_ios.ps1`／`boot_ios.bat` | 工具：Track 2 交接快速開機腳本，載入 `README.md` | 無 |
| [T-042](items/T-tools.zh-TW.md#t-042-工具-根目錄-2_boot_iosbatboot_ios_monitorbatreboot_to_fastbootbat) | 工具 根目錄 `2_boot_ios.bat`／`boot_ios_monitor.bat`／`reboot_to_fastboot.bat` | 工具：Track 2 一鍵開機＋監看／重開進 Fastboot 批次檔 | 無 |
| [T-043](items/T-tools.zh-TW.md#t-043-工具-根目錄-auto_boot_orchestratorpyrun_complete_ios_bootpywait_for_adb_and_bootpy) | 工具 根目錄 `auto_boot_orchestrator.py`／`run_complete_ios_boot.py`／`wait_for_adb_and_boot.py` | 工具：Track 2 Windows 全自動開機調度腳本 | 無 |
| [T-044](items/T-tools.zh-TW.md#t-044-工具-根目錄-fuxi_monitorpy) | 工具 根目錄 `fuxi_monitor.py` | 工具：輕量讀取器（完整日誌串流改用 `ios-fuxi/tools/fuxi-xnu-monitor.py`） | 無 |
| [T-045](items/T-tools.zh-TW.md#t-045-工具-根目錄-build-iosroot-macshfinish-iosroot-macsh) | 工具 根目錄 `build-iosroot-mac.sh`／`finish-iosroot-mac.sh` | 工具：macOS `hdiutil`＋`rsync` 建置 16 GB HFS+ RootFS 腳本 | `FUXI_WORKSPACE` |
| [T-046](items/T-tools.zh-TW.md#t-046-工具-根目錄-fuxi_probecfgfuxi_winusbinfzadigini) | 工具 根目錄 `fuxi_probe.cfg`／`fuxi_winusb.inf`／`zadig.ini` | 工具：為 `VID 0x1209 / PID 0x0001` 探針裝置安裝 WinUSB 驅動 | 無 |
| [T-047](items/T-tools.zh-TW.md#t-047-工具-workksu-ios-entrybuild-modulepyactionshcustomizesh) | 工具 `work/ksu-ios-entry/build-module.py`、`action.sh`、`customize.sh` | 工具：打包與執行 KSU 模組 `fuxi_ios_boot`（已於 2026-11-18 停用並封存） | `fuxi_ios_boot` |
| [T-048](items/T-tools.zh-TW.md#t-048-手機啟動器-launch-loader-checkps1) | 手機啟動器 launch-loader-check.ps1 | 日常使用中（AGENTS.md 第 7 節規定所有手機操作一律經此腳本） |  |
| [T-049](items/T-tools.zh-TW.md#t-049-loader-產生器-prepare-wlan-w18-loaderpy) | loader 產生器 prepare-wlan-w18-loader.py | 日常使用中（所有 W18／Wi-Fi／9g 實驗與日常 loader 的標準封裝工具） |  |
| [T-050](items/T-tools.zh-TW.md#t-050-loader-產生器-prepare-ram9g-loaderpy) | loader 產生器 prepare-ram9g-loader.py | 已採用（針對 9g RAM 配置與 ADSP／PMIC／手電筒／狀態欄／藍牙初期測試） |  |
| [T-051](items/T-tools.zh-TW.md#t-051-本機核心建置wsl) | 本機核心建置（WSL） | 日常使用中（9g 本機核心與 ath12k 連結均已實機驗證） |  |
| [T-052](items/T-tools.zh-TW.md#t-052-本機-ios-函式庫建置與簽署校驗wsl) | 本機 iOS 函式庫建置與簽署校驗（WSL） | 日常使用中（已通過主機測試、Mach-O 簽署校驗與實機載入） |  |
| [T-053](items/T-tools.zh-TW.md#t-053-harness-的硬體線-ci) | harness 的硬體線 CI | 已採用（作為正式可追溯產物與對照基準，日常迭代已轉由本機 WSL 建置分擔） | 各 workflow 皆支援 `workflow_dispatch` 與 `push` 到 `codex/adsp-battery` 分支（依 `paths` 過濾觸發）；`kernel-adsp-battery.yml` 另提供 `workflow_call` 參數 `ram_layout`（`full`/`9g`）、`artifact_name`、`runner_label`、`ath12k`（布林值） |
| [T-054](items/T-tools.zh-TW.md#t-054-wi-fi-使用者自測流程) | Wi-Fi 使用者自測流程 | 已採用（供使用者親自輸入真實 Wi-Fi 密碼測試連線） |  |
| [T-055](items/T-tools.zh-TW.md#t-055-公開倉庫匯出export-fuxi-iospy) | 公開倉庫匯出（export-fuxi-ios.py） | 已採用（2026-11-18 起用於同步公開倉庫 `scyx2611/fuxi-ios`） |  |
| [T-056](items/T-tools.zh-TW.md#t-056-工作區規範與文件) | 工作區規範與文件 | 日常使用中（全專案規範、授權與對外說明入口） | 無 |
| [T-057](items/T-tools.zh-TW.md#t-057-主機驗證資料hostcheck-wlan-標記規範) | 主機驗證資料（hostcheck WLAN 標記規範） | 已採用（作為本機 `ath12k` 實機驗收的結構化判準資料） | 無 |
| [T-058](items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) | ksrc 繼承的 GPU、建置與其他主機測試 | 繼承、原樣保留；本專案的 CI 與本機建置（`build-kernel.ps1 -Tests`）都不執行，只有 `tools/test-fuxi-nativemetal.py` 由 harness `native-metal-host-tests.yml` 呼叫（見 T-059） | 測試本身不讀開機參數；被測的上游功能用到的環境變數與開機參數：`FUXI_MTL_OCCLUDE`、`FUXI_MTL_NOFETCH_RP`、`FUXI_MTL_DRAW_AREA`、`FUXI_MTL_SPIRV_UNROLL`、`FUXI_MTL_HALF_ROUND`（`test-fuxi-metal-occlude.py`）、`fuxigpucache`（`test-fuxi-metal-cache.py`） |
| [T-059](items/T-tools.zh-TW.md#t-059-harness-gpu-時期的-ci-與主機測試2026-11-0811-14) | harness GPU 時期的 CI 與主機測試（2026-11-08～11-14） | 已完成、留作紀錄；日常 CI 已改用 `kernel-adsp-battery.yml` 系列（T-053），GPU 時期的 17 份核心補丁也已併進 ksrc `979e2f08..eafc778`（`75148d2` 一次併入 harness `2aae087` 當時的補丁，`3277d2c`、`6fc377f`、`eafc778` 再依序補 `kernel-ibprof-draws`、`kernel-taps-page1`、`kernel-backlight-dsi`；README 共用步驟 2） | workflow 都支援 `workflow_dispatch`；`kernel-bo-slots-check.yml` 另在 push 到 `codex/kernel-bo-slots`、`kernel-backlight-dsi.yml` 在 push 到 `codex/backlight-dsi` 時觸發（依 `paths` 過濾）。被測補丁的開機參數見各 G 項目（如 `fuxiperf=2`、`sweetsurfrow`、`sweetprof`、`fuxitapcmd`、`fuxibacklight=4`） |
| [T-060](items/T-tools.zh-TW.md#t-060-修復紀錄庫的維護驗證與匯出) | 修復紀錄庫的維護、驗證與匯出 | 工作區使用，公開版由匯出工具產生。 | `records/tools/build-catalog.py --check` 唯讀檢查。`work/repo-export/export-fuxi-ios.py --verify` 比對已提交的匯出，`--check` 掃描 clone。 |
| [T-061](items/T-tools.zh-TW.md#t-061-option-b-無-drmcryptid0201-靜態預檢側載指南對稱卸載工具鏈launch-loader-checkps1-擴充prepare-aio-loaderpy-升級與磁碟空間歸檔評估) | Option B 無 DRM（`cryptid=0`）20+1 靜態預檢／側載指南／對稱卸載工具鏈、`launch-loader-check.ps1` 擴充、`prepare-aio-loader.py` 升級與磁碟空間歸檔評估 | Option B 20+1 靜態預檢／側載指南／對稱卸載工具鏈與 `out-slot7der-b0` 合規 `0xB0` 雙端實機驗證閉環（`#462`/`#463`＋`stage-b1-remove-slot7der-b0-1126`）、`launch-loader-check.ps1` `-InstallApp`/`-RemoveApp` 擴充、`prepare-aio-loader.py` 正式升級（`79,889 B`, `9eca8290...`）、`LEAKS` 清障（`verify_b022_loaders.py:24`、`appstore-e2e-1123\`、`test-fuxi-net-trace.py`）與 `DISK_RECLAIM_CANDIDATES_1126.md` / `DISK_RECLAIM_ASSESSMENT_1126.md` 均已就緒；B2 沙盒容器實作待續 | `check-nodrm-ipa.py [--json] [--strict-webkit] [--strict-b1-naming] [--fail-on-der-only] [--self-test]`；`resign-app-adhoc.py build\|verify`；`launch-loader-check.ps1 -InstallAppDir <DIR> -Out <^[a-z0-9-]+$>` 與 `-RemoveAppName <APP> -RemoveAppBundleId <BUNDLE_ID> -Out <^[a-z0-9-]+$>`（對應 `phone-readthrough.py`）；`work/adsp-battery/prepare-aio-loader.py`（淘汰 `--gpu-hyb8-ovsh`、預設 `--probe-source None`、新增 `--user-app-noplatform 0\|1` 與 opt-in `--gpu-c1-bos 0\|1024..4096`）；`verify_b022_loaders.py [--loaders-only \| --negative-only]`；`remove-app-guest.py self-test` |

### R：備份與還原點（28 項）

| 編號 | 項目 | 狀態 | 開關 |
|---|---|---|---|
| [R-001](items/R-backups.zh-TW.md#r-001-android-partitions-20261107) | android-partitions-20261107 | 仍在 D:（`D:\xiaomi13\fuxi_Backup\android-partitions-20261107\`） | 無 |
| [R-002](items/R-backups.zh-TW.md#r-002-ios-root-images) | ios-root-images | 部分仍在 D:（`native-layout0025-root.img` 及各版 `.img.sha256` 仍在 `D:\xiaomi13\fuxi_Backup\ios-root-images\`），0022／0023 qcow2 與 0024 raw 已於 2026-11-16 23:00 移到 `E:\fuxi_Backup-archive\ios-root-images\` | 無 |
| [R-003](items/R-backups.zh-TW.md#r-003-ios27-root-recovered-20261108) | ios27-root-recovered-20261108 | 已移到 E:（`E:\fuxi_Backup-archive\ios27-root-recovered-20261108\`） | 無 |
| [R-004](items/R-backups.zh-TW.md#r-004-ios-root-phone-20261115q) | ios-root-phone-20261115q | 仍在 D:（`D:\xiaomi13\fuxi_Backup\ios-root-phone-20261115q\`） | `delta-pull-inplace.py --name phone1115q` |
| [R-005](items/R-backups.zh-TW.md#r-005-android-partitions-20261114) | android-partitions-20261114 | 仍在 D:（`D:\xiaomi13\fuxi_Backup\android-partitions-20261114\`） | 無 |
| [R-006](items/R-backups.zh-TW.md#r-006-android-partitions-hw000-20261116) | android-partitions-hw000-20261116 | 仍在 D:（`D:\xiaomi13\fuxi_Backup\android-partitions-hw000-20261116\`） | 無 |
| [R-007](items/R-backups.zh-TW.md#r-007-ios-root-hw000-20261116) | ios-root-hw000-20261116 | 仍在 D:（`D:\xiaomi13\fuxi_Backup\ios-root-hw000-20261116\`） | `hw000-delta-backup.py --name hw000-20261116` |
| [R-008](items/R-backups.zh-TW.md#r-008-ios-root-hw000sb-20261116) | ios-root-hw000sb-20261116 | 仍在 D:（`D:\xiaomi13\fuxi_Backup\ios-root-hw000sb-20261116\`） | `hw000-delta-backup.py --name hw000sb-20261116` |
| [R-009](items/R-backups.zh-TW.md#r-009-ios-root-hw000shape-20261116) | ios-root-hw000shape-20261116 | 仍在 D:（`D:\xiaomi13\fuxi_Backup\ios-root-hw000shape-20261116\`） | `hw000-delta-backup.py --name hw000shape-20261116` |
| [R-010](items/R-backups.zh-TW.md#r-010-boot_b-chain-v1-20261116) | boot_b-chain-v1-20261116 | 仍在 D:（`D:\xiaomi13\fuxi_Backup\boot_b-chain-v1-20261116\`） | 無 |
| [R-011](items/R-backups.zh-TW.md#r-011-boot_b-chain-v2-20261116) | boot_b-chain-v2-20261116 | 仍在 D:（`D:\xiaomi13\fuxi_Backup\boot_b-chain-v2-20261116\`） | 無 |
| [R-012](items/R-backups.zh-TW.md#r-012-mtdblk-20261116) | mtdblk-20261116 | 仍在 D:（`D:\xiaomi13\fuxi_Backup\mtdblk-20261116\`） | 無 |
| [R-013](items/R-backups.zh-TW.md#r-013-boot_b-chain-v3-before-chain-v4-20261116-121904) | boot_b-chain-v3-before-chain-v4-20261116-121904 | 仍在 D:（`D:\xiaomi13\fuxi_Backup\boot_b-chain-v3-before-chain-v4-20261116-121904\`） | 無 |
| [R-014](items/R-backups.zh-TW.md#r-014-boot_b-chain-v3-before-chain-v4-20261116-122005) | boot_b-chain-v3-before-chain-v4-20261116-122005 | 仍在 D:（`D:\xiaomi13\fuxi_Backup\boot_b-chain-v3-before-chain-v4-20261116-122005\`） | 無 |
| [R-015](items/R-backups.zh-TW.md#r-015-boot_b-chain-v3-before-chain-v4-20261116-122108) | boot_b-chain-v3-before-chain-v4-20261116-122108 | 仍在 D:（`D:\xiaomi13\fuxi_Backup\boot_b-chain-v3-before-chain-v4-20261116-122108\`） | 無 |
| [R-016](items/R-backups.zh-TW.md#r-016-boot_b-chain-v4-before-chain-v6-ram9g-batfresh-20261116-161041) | boot_b-chain-v4-before-chain-v6-ram9g-batfresh-20261116-161041 | 仍在 D:（`D:\xiaomi13\fuxi_Backup\boot_b-chain-v4-before-chain-v6-ram9g-batfresh-20261116-161041\`） | 無 |
| [R-017](items/R-backups.zh-TW.md#r-017-boot_b-chain-v6-ram9g-batfresh-before-chain-v7-ram9g-rtctorch-20261117-024231) | boot_b-chain-v6-ram9g-batfresh-before-chain-v7-ram9g-rtctorch-20261117-024231 | 仍在 D:（`D:\xiaomi13\fuxi_Backup\boot_b-chain-v6-ram9g-batfresh-before-chain-v7-ram9g-rtctorch-20261117-024231\`） | 無 |
| [R-018](items/R-backups.zh-TW.md#r-018-boot_b-chain-v7-ram9g-rtctorch-before-chain-v8-dualboot-wifi-20261118-031406) | boot_b-chain-v7-ram9g-rtctorch-before-chain-v8-dualboot-wifi-20261118-031406 | 仍在 D:（`D:\xiaomi13\fuxi_Backup\boot_b-chain-v7-ram9g-rtctorch-before-chain-v8-dualboot-wifi-20261118-031406\`） | 無 |
| [R-019](items/R-backups.zh-TW.md#r-019-boot_b-chain-v8-dualboot-wifi-before-chain-v9-dualboot-launching-20261118-042629) | boot_b-chain-v8-dualboot-wifi-before-chain-v9-dualboot-launching-20261118-042629 | 仍在 D:（`D:\xiaomi13\fuxi_Backup\boot_b-chain-v8-dualboot-wifi-before-chain-v9-dualboot-launching-20261118-042629\`） | 無 |
| [R-020](items/R-backups.zh-TW.md#r-020-dualboot-before-validation-20261118-014941) | dualboot-before-validation-20261118-014941 | 仍在 D:（`D:\xiaomi13\fuxi_Backup\dualboot-before-validation-20261118-014941\`） | `hw000-delta-backup.py --name dualboot1118` |
| [R-021](items/R-backups.zh-TW.md#r-021-ios-root-hw000provider-20261116) | ios-root-hw000provider-20261116 | 仍在 D:（`D:\xiaomi13\fuxi_Backup\ios-root-hw000provider-20261116\`） | `hw000-delta-backup.py --name hw000provider-20261116` |
| [R-022](items/R-backups.zh-TW.md#r-022-ios-root-hw000shape2-20261116) | ios-root-hw000shape2-20261116 | 仍在 D:（`D:\xiaomi13\fuxi_Backup\ios-root-hw000shape2-20261116\`） | `hw000-delta-backup.py --name hw000shape2-20261116` |
| [R-023](items/R-backups.zh-TW.md#r-023-ios-root-hw000torch-cap-20261116) | ios-root-hw000torch-cap-20261116 | 已中止／不存在於 D:（資料夾未建立完成） | 無 |
| [R-024](items/R-backups.zh-TW.md#r-024-ios-root-hw000torch-runtime-20261116) | ios-root-hw000torch-runtime-20261116 | 仍在 D:（`D:\xiaomi13\fuxi_Backup\ios-root-hw000torch-runtime-20261116\`） | `hw000-delta-backup.py --name hw000torch-runtime-20261116`（中斷後用 `--resume-download`） |
| [R-025](items/R-backups.zh-TW.md#r-025-ksu-module-remove-ksu-module-20261118) | ksu-module-remove-ksu-module-20261118 | 仍在 D:（`D:\xiaomi13\fuxi_Backup\ksu-module-remove-ksu-module-20261118\`） | 無 |
| [R-026](items/R-backups.zh-TW.md#r-026-efuxi_backup-archiveworkspace-20261118) | E:\fuxi_Backup-archive\workspace-20261118 | 已移到 E:（`E:\fuxi_Backup-archive\workspace-20261118\`） | 無 |
| [R-027](items/R-backups.zh-TW.md#r-027-已刪除的舊手機差異-ios-root-phone-2026111420261115h) | 已刪除的舊手機差異 ios-root-phone-20261114～20261115h | 已刪除 | 無 |
| [R-028](items/R-backups.zh-TW.md#r-028-a-槽倒數版升級前的整分區回退映像) | a 槽倒數版升級前的整分區回退映像 | 回退映像在工作區，201,326,592 bytes，已核對 SHA-256。 | 由 launch-loader-check.ps1 的 -UpgradePersistent 流程產生。 |

<!-- INDEX:END -->
