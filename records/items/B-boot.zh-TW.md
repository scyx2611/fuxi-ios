# B 系列：開機鏈、部署與雙系統選單

> 從 fastboot 暫時開機到常駐雙系統：`boot_b` 分流器 chain 各版、已驗證部署、RAM SKU 預檢、`boot_a` 雙系統選單（`dualboot/`）、KSU 模組（已移除）、獨立開機 console 紀錄（mtdblk）、上機前差分備份與 loader 安全核對。備份與還原點本身記在 R 系列。

### B-001 b 槽開機分流器 chain 與版本史（chain-v1～v9）

- **日期**：2026-11-14（chain-v1）～2026-11-18 04:26（chain-v9）
- **狀態**：備援使用中（2026-11-18 起改由 `boot_a` 常駐 dualboot 選單，b 槽 `chain-v9-dualboot-launching` 僅作備援；切到 b 槽選 Android 會循環）
- **開關**：`build-chain.py` 參數：`--force-linux`（測試用，跳轉內嵌 Android 核心）、`--auto-launch-s <5..60>`（loader 等電腦秒數，預設 5；v8 起 30）、`--payload <native-*-loader>`、`--ram-sku <auto|8g|12g>`；編譯巨集 `-DFUXI_CHAIN=1`、`-DFUXI_CHAIN_FORCE_LINUX=1`、`-DFUXI_AUTO_LAUNCH_S=<N>`、`-DFUXI_RAM_9G=1`
- **問題／現象**：在不接電腦的日常情況下，需要從 Android `boot_b` 槽位啟動 iOS 16，同時在按住「電源＋音量上」進入 Recovery 時仍能導向內嵌 Android 核心進入 TWRP，避免切槽後卡死回不來。
- **根因**：原廠 `boot_b` 直接載入 Android 核心；XNU loader 須打包為 Android boot v4 image，並於早期入口分流一般開機與 Recovery 開機。
- **做法**：
  - `work/boot-chain/build-chain.py`：從 `D:\xiaomi13\fuxi_Backup\android-partitions-20261107\boot_a.img` 抽出未壓縮 ARM64 `linux-Image`，搭配 `ios-fuxi/shim/fuxi-loader/{start.S,chain.c}`、`fuxi-probe/probe.c`、指定 `payload` 目錄的 `loader.o`/`console.o`/`gpu-fw.o`/`payload.o`，以 `loader-chain.ld` 連結成 `loader.elf` → `Image`，再以 `mkbootimg.py --header_version 4 --kernel Image` 封裝為 `boot.img`（無 ramdisk、`os_version 0`、`<= 201326592` bytes）。
  - **版本史（chain-v1～v9）**：
    - `chain-v1`（2026-11-14 寫入 `boot_b`，2026-11-16 06:33 備份）：payload `native-auto-cool-loader`，`force_linux=False`、`auto_launch_s=5`，SHA-256 `9924fe1c...3f06`。
    - `chain-v2`（2026-11-16 06:33 部署）：換入電量＋亮度（`native-dly1-user-loader`、`auto_launch_s=5`），核心含 `vsync`/`tcache` 調校、移除 `keepsyms` 與 `fuxi-ca-smoke`，prefix SHA-256 `994c95d6b78118ae625bb5919da2c7e416a5644fb403abc655ec4c6a85bcf25d`。因獨立開機實測「亮度滑兩下就停」與電量停在 100% 充電中而替換。
    - `chain-v3`（2026-11-16 08:47 部署）：修復亮度「滑兩下就停」（`ba65d38`＋`7af418a`）並開啟 `mtdblk` 獨立開機紀錄，KSU v4，prefix SHA-256 `2965b2cce5f9c1aef514a454e3299e3343b8637e7ad3a7e307cf38bf8973c27c`。
    - `chain-v4`（2026-11-16 12:21:32 部署）：加入共用 RSC 與 SpringBoard 狀態欄 300×127（`native-dly-sb127-user-loader`、5g、`auto_launch_s=5`、ADT `fuxi,inject=2`、`fuxi,sbshape=300127`），KSU v5，prefix SHA-256 `dc9c0158638b319a10cb38b09531eb1e324b433bf287a7a4e33596058b3ec5e2`。
    - `chain-v5-ram9g`（2026-11-16 12:32 暫時測試，**未部署**）：首次將 9g payload 與同目錄 `loader.o`/`console.o` 及 `-DFUXI_RAM_9G=1` 打包測試，boot SHA-256 `b7a81bd0a9c41d6abacf7e4954be4ce1b5cd222675e79815258dab2882644dbb`。
    - `chain-v6-ram9g-batfresh`（2026-11-16 16:10:51 部署）：升級為 9g（9184 MiB usable）＋電池 freshness 修復＋sb127，KSU v6，prefix SHA-256 `93cb7d5feedabaf24db3d494157dcfd4aa27a8a3b060ce3ca32b420ca4f8d24e`。
    - `chain-v7-ram9g-rtctorch`（2026-11-17 02:42:31 部署）：加入 ADT RTC 校時（`fuxi,rtc-offset`）、手電筒五檔亮度、新 `incoming-FDT` guard 等，payload `native-dly-ram9g-rtctorch-sb127-loader`（`kernel-adsp-rtcadt-ram9g-artifact`），KSU v7，prefix SHA-256 `eb985a344eaa2c57d3ab3005c96f204a5e44fd3df785267eec60875b3bfe3483`。
    - `chain-v8-dualboot-wifi`（2026-11-18 03:14:38 部署）：合併三按鈕 dualboot 選單（`menu-direct-launch-20261118`）、原生 Wi-Fi（`kernel-local-wifiux2-9g-artifact`、`ux=31`）、`haptic=2` 等，KSU v8，prefix SHA-256 `dc7240d7cfdc37141e0430d742ee83a8cb2a25fed3aa9c65e7e6b5cad792d39e`。
    - `chain-v9-dualboot-launching`（2026-11-18 04:26:29 部署）：將選單更新為 `menu-launching-20261118`（「進入系統中」提示），與 `boot_a` 常駐選單同版，KSU v9（後移除模組），prefix SHA-256 `7977fbf0c8813ec16d76608bd24b1a930dda769fc1b2a8ed06c690fe013a2773`。
- **復現**：
  ```powershell
  python -X utf8 work\boot-chain\build-chain.py chain-v4 --payload native-dly-sb127-user-loader
  ```
- **主機測試**：`work/boot-chain/test-loader-sku-preflight.py`（涵蓋 `build-chain.py` 的 SKU 預檢邏輯）。
- **驗證紀錄**：各版皆先以 `prepare-test.py` 打包並經 `fastboot boot` 暫時開機驗證，再以 `deploy-verified.py` 寫入 `boot_b` 並讀回核對（詳見 `hw000-adaptation-status.zh-TW.md` 與 `D:\xiaomi13\fuxi_Backup\boot_b-chain-*`）。
- **參考來源**：`ios-fuxi/shim/fuxi-loader/{start.S,chain.c,loader-chain.ld}`、`uefi-fuxi/Resources/Scripts/mkbootimg.py`。
- **結論／未完成**：2026-11-18 04:45 起日常改由 `boot_a` 常駐 dualboot 選單啟動，`boot_b`（chain-v9）保留作為備援。
- **出處**：`ws:work/boot-chain/build-chain.py`；ws 提交 `b7d80cf`、`c4de3f4`；`work/hw000-adaptation-status.zh-TW.md` L24, L28, L77, L177, L181, L259, L265, L392, L395。（2026-11-18 主代理分配範圍與來源，Gemini 依原始檔撰寫，主代理核對欄位、提交時間與路徑）

### B-002 已驗證部署與還原（deploy-verified、prepare-test）

- **日期**：2026-11-16 16:00（`2018462`）～2026-11-18 04:46（`3583605`）
- **狀態**：已採用（v5–v9 `boot_b` 部署與失敗自動還原皆由此腳本執行）
- **開關**：`deploy-verified.py <previous> <next> --version <N>=5> [--menu-entry] [--skip-full-boot-backup]`；`prepare-test.py <chain> <native-*-loader>`
- **問題／現象**：將新分流器寫入 `boot_b` 與更新 KSU 模組時，若寫入中途出錯、讀回不符或裝置非 12GB SKU，必須能安全中止並自動還原 `boot_b`、手機模組與主機模組目錄。
- **根因**：直接用 `dd` 覆寫分區缺乏寫前鎖定、前像核對、寫後讀回與失敗自動回復機制。
- **做法**：
  - `work/boot-chain/prepare-test.py`：確認 `build.txt` 含 `force_linux=False`、核對 `payload.bin` 與 `boot.img` 雜湊，將 `boot.img` 與對應 loader metadata 複製到 `work/Codex-Xiaomi13-GPU-20261109/native-*-loader` 供 `launch-loader-check.ps1` 暫時開機測試。
  - `work/boot-chain/deploy-verified.py`：
    1. 取得 `WORK/PHONE-LOCK-000`、`WORK/hybrid-driver/HOLD-PHONE`，並檢查 `HOLD` 不存在。
    2. 檢查手機在 Android `_a` 槽、12GB DDR SKU（`[0, 0, 0, 12]`）、`boot_a` 屬於 `allowed_boot_a()`（原廠或已驗證常駐選單）。
    3. 備份現有 `boot_b`（完整或 `--skip-full-boot-backup` 記錄前綴來源）、手機 KSU 模組 tar 與 `host-module-before/` 至 `D:\xiaomi13\fuxi_Backup\boot_b-<prev>-before-<next>-<timestamp>\`。
    4. 寫入新 `boot_b`（`dd bs=4194304 conv=fsync`）、清快取讀回前 `size` bytes 比對 SHA-256，並依 `--menu-entry` 決定打包／驗證 KSU 模組。
    5. 若任何步驟失敗，`except`／`finally` 自動執行 `restore_boot()`、`restore_module()`、`restore_host_module()` 並記錄 JSON event journal；若還原也失敗則保留鎖並印出 `MANUAL_RECOVERY_REQUIRED`。
  - 演進：
    - `2018462` 2026-11-16 16:00：新增 `deploy-verified.py`（含 `boot_b` 與模組原子化部署、失敗還原）。
    - `e77243c` 2026-11-16 16:17：新增 `prepare-test.py`（依記錄補補丁）。
    - `b7d80cf` 2026-11-16 17:09：強化路徑檢查與新增 `test-deploy-recovery.py`。
    - `b37dcde` 2026-11-18 04:21：`allowed_boot_a()` 允許已驗證常駐 `boot_a` 選單雜湊。
    - `3583605` 2026-11-18 04:46：同步更新 `deploy-verified.py` 與 `test-deploy-recovery.py` 以配合 KSU 模組移除。
- **復現**：
  ```powershell
  python -X utf8 work\boot-chain\prepare-test.py chain-v6-ram9g-batfresh native-test-v6-loader
  python -X utf8 work\boot-chain\deploy-verified.py chain-v4 chain-v6-ram9g-batfresh --version 6 --skip-full-boot-backup
  ```
- **主機測試**：`python -X utf8 work\boot-chain\test-deploy-recovery.py`（模擬 `adb`／`rooted`，測試 `dd` 失敗、`tar` 失敗、讀回不符、遠端雜湊不符等故障路徑，32 項測試全數通過）。
- **驗證紀錄**：2026-11-16 12:19–12:21 部署 `chain-v4` 時前兩次因編碼中止，第二次實際觸發自動還原 `chain-v3` 與模組成功，第三次部署成功；2026-11-18 03:14 部署 `chain-v8` 前通過 32 項 `test-deploy-recovery.py` 測試。
- **參考來源**：`dualboot/phone-check.py`（共用 `installed_boot_a_shas()`）。
- **結論／未完成**：完成；KSU 模組移除後僅用於備援 `boot_b` 更新。
- **出處**：`ws:work/boot-chain/deploy-verified.py`、`prepare-test.py`、`test-deploy-recovery.py`；ws 提交 `2018462`、`e77243c`、`b7d80cf`、`b37dcde`、`3583605`、`c75a3b6`。（2026-11-18 主代理分配範圍與來源，Gemini 依原始檔撰寫，主代理核對欄位、提交時間與路徑）

### B-003 loader 的 RAM SKU 預檢與 RAM 入口防護

- **日期**：2026-11-16 17:41（`c4de3f4`）～2026-11-18 05:40（`f401e41`）
- **狀態**：已採用（主機端 preflight 與 XNU loader 入口 `loader_check_ram` 皆已啟用）
- **開關**：`fuxi,ram-sku=<12>`（ADT `/chosen` u32）；`--ram-sku <auto|8g|12g>`（`build-chain.py`）；`FUXI_RAM_9G=1`（區分 5g／9g 配置）；`loader_check_ram(fdt)` 回傳碼 `FuxiRamProfileResult`（0=`StaticOk12G`, 7=`SkuMissing`, 8=`SkuUnsupported`, 9=`Conflict` 等）
- **問題／現象**：小米 13 有 8GB 與 12GB 兩種實體記憶體 SKU；若將 9g（9184 MiB usable，GPU scratch `0xafe000000`）或 5g 的寫死配置誤開在 8GB 機型，或讓 XNU 使用與保留區／loader 自身重疊的 FDT 記憶體區段，會導致記憶體毀損或無法開機。
- **根因**：早期 loader 未在早期入口對 live FDT 的 `/memory/ddr_size`、`/memory/reg`、`model` 與保留區做有界驗證。
- **做法**：
  - `ios-fuxi/shim/fuxi-loader/fuxi-ram-profile.h` 與 `loader.c` 的 `loader_check_ram(uintptr_t fdt)`：透過 `probe_safe_rd32` 安全讀取 FDT 標頭與內容，拒絕結構異常、非 `fuxi` model、`ddr_size != 12`（8GB 直接拒絕）、重複衝突屬性、記憶體洞／溢位或與 loader 自身（`_head.._end`）／full-bank 重疊的配置。
  - `test-runtime-ram-profile.py`：在主機上建構合成 FDT 與重播已存 `fuxi-live-20261116.dtb`，覆蓋 423 cases／2966 assertions（含 5g/9g、zero-slot、overlap、overflow、endianness 等）。
  - `test-loader-entry-ram.py`：從生產碼 `loader.c` 抽取 `loader_check_ram` 函式，以模擬 `probe_safe_rd32` 編譯並執行 5g／9g 各 6 cases／46 assertions 煙霧測試。
  - `test-loader-sku-preflight.py`：用 AST 抽取 `mkadt-fuxi.py`、`prepare-ram9g-loader.py`、`prepare-wlan-w18-loader.py`、`build-chain.py`、`verify-loader-safety.py` 的實際解析與驗證邏輯，在無手機情況下驗證 8GB 拒絕、12GB 通過與選項相容性。
  - 演進：
    - `c4de3f4` 2026-11-16 17:41：新增三個主機測試腳本與更新 `build-chain.py`／`verify-loader-safety.py`。
    - `f401e41` 2026-11-18 05:40：更新 `test-loader-sku-preflight.py`，以 `Options` 類別對未列出的 30 多個新產生器選項回傳 `None`，並支援 `os` 環境變數。
- **復現**：
  ```powershell
  python -X utf8 work\boot-chain\test-runtime-ram-profile.py
  python -X utf8 work\boot-chain\test-loader-entry-ram.py
  python -X utf8 work\boot-chain\test-loader-sku-preflight.py
  ```
- **主機測試**：上述三支腳本即為主機 ASan／UBSan 與 AST 回歸測試。
- **驗證紀錄**：`hw000-adaptation-status.zh-TW.md` L87 記錄第 20 趟暫時 loader 實機通過 `RAM_PROFILE_OK12_9G` 並進入 SpringBoard（`ios-fuxi` 本機提交 `5f04f26`）；主機通過 423 cases／2966 assertions 與 entry smoke 各 6 cases／46 assertions。8GB 實機拒絕行為僅主機測試，未在 8GB 硬體實測。
- **參考來源**：`work/adsp-battery/ksrc/tools/mkadt-fuxi.py`、`ios-fuxi/shim/fuxi-loader/fuxi-ram-profile.h`。
- **結論／未完成**：12GB SKU 預檢與實機 `RAM_PROFILE_OK12_9G` 已驗證；8GB 實機與 ARM fault/reset 未驗。
- **出處**：`ws:work/boot-chain/test-loader-entry-ram.py`、`test-loader-sku-preflight.py`、`test-runtime-ram-profile.py`；ws 提交 `b7d80cf`、`c4de3f4`、`f401e41`；`work/hw000-adaptation-status.zh-TW.md` L87。（2026-11-18 主代理分配範圍與來源，Gemini 依原始檔撰寫，主代理核對欄位、提交時間與路徑）

### B-004 雙系統開機選單（boot_a 常駐）

- **日期**：2026-11-18 04:21（`b37dcde`）～2026-11-18 05:45（`a35a57b`）
- **狀態**：日常使用中（`boot_a` 已常駐 `menu-launching-20261118`，Android、iOS、Recovery 三條路徑皆已在安裝後實機驗收）
- **開關**：`dualboot/build.py`：`--output <work/dualboot/...>`、`--host-only`、`--base-chain <目錄>`（預設 `chain-v7-ram9g-rtctorch`）、`--fdt <dtb>`、`--default <android|ios>`（編譯 `MENU_DEFAULT_IOS`）、`--timeout <秒>`（編譯 `MENU_TIMEOUT_MS`，歷史版預設 0，現行版預設 10，見 B-011）；`launch-loader-check.ps1`：`-DualBootCheck`、`-InstallPersistent`、`-UpgradePersistent`、`-ValidatePersistent`
- **問題／現象**：過去必須先開進 Android 再透過 KSU 模組切換 `boot_b` 才能進 iOS，且 `boot_b` 選 Android 會循環。需要一個常駐於 `boot_a`、開機即可直接用實體按鍵選擇系統的黑底白字雙系統選單。
- **根因**：原 `boot_a` 為純 Android boot image，無早期選單介面與按鍵分流。
- **做法**：
  - 選單核心（`dualboot/`）：
    - `menu.c/h`：首頁三按鈕 **Android**、**iOS**、**其他選項**（返回、Fastboot、Recovery、重新啟動、關機）；40 ms 去彈跳、忽略開機未放開按鍵與組合鍵（避免電源＋音量下誤觸）。
    - `render.c` 與 `font-generated.h`（由 `tools/make-font.py` 從開源 Noto Sans TC 48 px 字重 400 產生的單色字形子集，授權 `licenses/NotoSansTC-OFL.txt`）：黑底白字繪製主標 **dualboot**、副標 **for fuxi**、粗白框焦點與「進入系統中」狀態提示。
    - `spmi.c/h`：SM8550 SPMI v7 唯讀讀取 PON（`0x013`，電源／音量下）與 GPIO6（`0x18d`，音量上）；進入 Fastboot／Recovery 時寫入 SDAM（`0x071`）EE0 並讀回核對後再由 PSCI 重啟。
    - `bootinfo.c/h`：有界 FDT／bootconfig 解析，先於顯示／SPMI 判斷是否為 Recovery（缺 `androidboot.force_normal_boot` 或 initrd 判斷）。
    - `board.c`：1080×2400／4320-byte stride／`0xb8000000` framebuffer 幾何檢查與 `present()` 刷新、12GB RAM profile 檢查、選定系統後顯示「進入系統中」等待 20 ms 再交接內嵌 Android 核心或回到 `loader_main`。
  - 電腦端建置與安裝：
    - `build.py`：先對 `base-chain` 物件重連結驗證逐位元相符，編譯選單物件並檢查無 `R_AARCH64_ABS` 重定位，產出 `boot.img`（66,326,528 bytes，SHA-256 `7977fbf0...13a2773`）。
    - `prepare-phone-loader.py`：打包成 `launch-loader-check.ps1` 可暫時 RAM 載入的 wrapper。
    - `install-persistent.py`：僅允許寫入已通過 RAM Android 試開驗證的 `VERIFIED_IMAGES`，保留 `boot_a` 尾段原廠內容，寫後全分區讀回核對並提供 Fastboot 還原路徑。
    - `phone-check.py`、`start-daily.py`、`rescue-android.py`：提供備份、寫前檢查、重開進選單與 Fastboot `set_active a` 救援。
  - 演進：
    - `b37dcde` 2026-11-18 04:21：加入 `dualboot/` 完整選單、建置、安裝與驗證工具，並將 `menu-launching-20261118` 常駐安裝至 `boot_a`。
    - `a35a57b` 2026-11-18 05:45：補充匯出 `NotoSansTC-OFL.txt`。
- **復現**：
  ```powershell
  # 電腦端建置與主機測試
  python -X utf8 dualboot\build.py --output work\dualboot\menu-local
  # 僅跑主機測試與預覽圖
  python -X utf8 dualboot\build.py --host-only --output work\dualboot\menu-host
  # 首次安裝／升級／驗證常駐 boot_a
  .\work\statusbar-fix\launch-loader-check.ps1 -InstallPersistent
  .\work\statusbar-fix\launch-loader-check.ps1 -UpgradePersistent
  .\work\statusbar-fix\launch-loader-check.ps1 -ValidatePersistent
  ```
- **主機測試**：`dualboot/tests/host.c`（由 `build.py` 經 WSL clang＋ASan／UBSan 自動編譯執行），通過 444 項斷言（涵蓋三按鈕焦點、倒數、去彈跳、FDT／bootconfig 截斷與變異、SPMI 錯誤／讀回、framebuffer 邊界與黑白像素）。
- **驗證紀錄**：
  - 2026-11-18 03:50–04:06：首版 `menu-partial-refresh-20261118`（`05e0318d...`）安裝至 `boot_a`，Android `0xa9210000`、iOS boot #91（exec SpringBoard、無 panic）通過。
  - 2026-11-18 04:13–04:19：升級為 `menu-launching-20261118`（整分區 SHA-256 `275dac0f...b4b1`），安裝後實機驗過 Android（`0xa9210000`）、iOS boot #92（exec SpringBoard、無 panic）、Recovery（TWRP `3.7.1_12-Xiaomi13_v9.0_A15`）及回程，使用者確認全程正常。
- **參考來源**：本機 `work/boot-chain/chain-v7-ram9g-rtctorch`、`ios-fuxi/shim/`、`unixnu-fuxi/package/{BootResetCore.h,FuxiButtons.cpp}`（MIT／BSD-2-Clause-Patent）、`Hercules-Zhaoziyi/mi8-dualboot-lab`（互動參考，未複製程式碼）、Linux v6.12 `spmi-pmic-arb`、Google Fonts `Noto Sans TC`（OFL 1.1）。
- **結論／未完成**：日常 `boot_a` 運作正常；尚未驗收「其他選項」中的 Fastboot／重新啟動／關機、完全關機冷開機、ABL 重試與回退，以及 b 槽選 Android（因 `system_b` 等邏輯分區為空會循環，不修）。
- **出處**：`ws:dualboot/` 全部檔案；ws 提交 `b37dcde`、`3583605`、`a35a57b`；`dualboot/README.zh-TW.md`、`VALIDATION.zh-TW.md`、`THIRD_PARTY.md`、`work/hw000-adaptation-status.zh-TW.md` L6–L8, L23–L27。（2026-11-18 主代理分配範圍與來源，Gemini 依原始檔撰寫，主代理核對欄位、提交時間與路徑）

### B-005 移除 KSU 模組、檢查移到主機

- **日期**：2026-11-18 04:44～04:46（`3583605`）
- **狀態**：已採用（手機上 `/data/adb/modules/fuxi_ios_boot` 已封存並移除，檢查改由電腦端 `dualboot/phone-check.py` 執行）
- **開關**：`phone-check.py` 函式 `verify_ios_root()` 與 `remove_ksu_module()`
- **問題／現象**：`boot_a` 常駐雙系統選單後，使用者直接重開即可選 iOS，KSU 模組 `fuxi_ios_boot`（v9）只剩重開按鈕而無存在必要，使用者要求移除；但仍須保留原先模組執行的 iOS 根映像區塊表與 pin 檢查能力。
- **根因**：選單常駐 `boot_a` 後不再需要透過 KSU 模組切到 `boot_b` 開機。
- **做法**：
  - 移除前先逐檔核對手機 `/data/adb/modules/fuxi_ios_boot`，打包封存至 `D:\xiaomi13\fuxi_Backup\ksu-module-remove-ksu-module-20261118\fuxi_ios_boot-module.tar`（SHA-256 `4cb1cb09...2b79`）。
  - 將模組原本的檢查邏輯（確認未被 Android mapper 掛載、`inspect-fiemap` 比對 `expected-fiemap.txt`、`check-pin` 確認 `flags=0x800000` 與 `moved=0`）移至電腦端 `dualboot/phone-check.py` 的 `verify_ios_root()`，執行時暫時推送 `work/ksu-ios-entry/fuxi_ios_boot/bin/{inspect-fiemap,check-pin}` 檢查後立即刪除。
  - 更新 `start-daily.py`、`install-persistent.py`、`deploy-verified.py` 改用主機端檢查與 `boot_a` 流程。
- **復現**：
  ```powershell
  python -X utf8 dualboot\tests\test-remove-ksu.py
  ```
- **主機測試**：`dualboot/tests/test-remove-ksu.py`（模擬手機驗證先檢查未掛載與封存才移除、拒絕尚有額外開機腳本或參考雜湊不符的模組、拒絕在 b 槽移除）。
- **驗證紀錄**：2026-11-18 04:44:45–04:44:58 完成封存、電腦端執行一次 iOS 根映像區塊表與 pin 檢查後移除模組並確認 `/data/adb/modules/fuxi_ios_boot` 不復存在；不寫分區、不重開、不切槽（紀錄 `work/dualboot/remove-ksu-module-20261118/`）。
- **參考來源**：原 `work/ksu-ios-entry/fuxi_ios_boot/action.sh`。
- **結論／未完成**：完成。
- **出處**：`ws:dualboot/tests/test-remove-ksu.py`、`dualboot/phone-check.py`、`dualboot/start-daily.py`、`dualboot/install-persistent.py`；ws 提交 `3583605`；`dualboot/VALIDATION.zh-TW.md` L16, L24。（2026-11-18 主代理分配範圍與來源，Gemini 依原始檔撰寫，主代理核對欄位、提交時間與路徑）

### B-006 KSU 開機模組 fuxi_ios_boot（已移除）

- **日期**：2026-11-14 建立～2026-11-18 04:44 移除（`c75a3b6` 補納入追蹤）
- **狀態**：已停用／已從手機移除（封存於 `D:\xiaomi13\fuxi_Backup\ksu-module-remove-ksu-module-20261118\`；主機端保留參考 binary 與 `expected-fiemap.txt`）
- **開關**：`build-module.py <chain-vN>`；手機端點擊 KSU「執行」（`action.sh`）
- **問題／現象**：在 `boot_a` 未常駐選單前，需要安全地從 Android 切到 `boot_b`（分流器）重開進 iOS，且必須防止 iOS 根映像被 Android 掛載、碎片移動或 `recovery_b` 被覆寫導致無法回 Android。
- **根因**：直接用 `bootctl set-active-boot-slot 1` 缺乏開機前映像與 TWRP 完整性檢查。
- **做法**：
  - `work/ksu-ios-entry/build-module.py`：讀取指定 `chain-vN` 的 `boot.img` 與 `payload.bin`，解出 ADT `fuxi-root-extents`，在手機上跑 `inspect-fiemap` 確認完全一致後寫入 `expected-fiemap.txt`、`loader.sha256`、`loader.size`、`twrp.sha256`，打包 `fuxi_ios_boot-<chain>.zip`（已產出 v1、v2、v3、v4、v6、v7、v8、v9）。
  - `fuxi_ios_boot/action.sh`（五步安全檢查）：`[1/5]` 確認在 `_a` 槽；`[2/5]` 確認 `/dev/block/mapper/codexnative_root_1108b` 不存在、FIEMAP 等於 `expected-fiemap.txt`、`check-pin` 確認 `flags=0x800000` 且 `moved=0`；`[3/5]` 核對 `boot_b` 前綴雜湊與 `recovery_b` TWRP 雜湊；`[4/5]` `bootctl set-active-boot-slot 1`；`[5/5]` `sleep 3 && sync && reboot`。
- **復現**：
  ```powershell
  python -X utf8 work\ksu-ios-entry\build-module.py chain-v9-dualboot-launching
  ```
- **主機測試**：無（模組檢查邏輯已隨 B-005 移入 `phone-check.py` 並由 `test-remove-ksu.py` 覆蓋）。
- **驗證紀錄**：v1～v9 皆曾搭配對應 `boot_b` 分流器實際使用；2026-11-18 04:27 v9 實測切 `boot_b` 成功進入 iOS boot #93；04:44 封存移除。
- **參考來源**：`work/xiaomi13-ios-gpu-20261108/{inspect-fiemap,check-pin-readonly}`。
- **結論／未完成**：功成身退，已移除；主機端僅保留 `fuxi_ios_boot/` 內的 `bin/` 與 `expected-fiemap.txt`／`twrp.sha256` 供 `phone-check.py` 使用。
- **出處**：`ws:work/ksu-ios-entry/` 全部檔案；ws 提交 `c75a3b6`、`3583605`；`dualboot/VALIDATION.zh-TW.md`。（2026-11-18 主代理分配範圍與來源，Gemini 依原始檔撰寫，主代理核對欄位、提交時間與路徑）

### B-007 日常鏈合併（chain-v7、chain-v8、chain-v9）

- **日期**：2026-11-18 00:10（`6cc9feb`）～2026-11-18 04:45
- **狀態**：已採用（日常已合併至 `boot_a` 常駐 `menu-launching-20261118`＋`boot_b` `chain-v9` 備援）
- **開關**：依 `comparison.json` 記錄之 `chain-v8` 差異：命令列移除 `debug=12`、新增 `fuxiwlan=1`、`sweetfb_noconsole` `1->2`、`sweetnull` 改為 `backboardd,SpringBoard,wifid`；ADT 新增 `fuxi,adsp-wlan=1`、`fuxi,args="fuxiwlanath=1 fuxiwlanmlo=192 fuxiwifiios=2 fuxiwlandwell=1 fuxiwlanbatch=1"`、`fuxi,audio-spf=1`、`fuxi,haptic=2`、`fuxi,haptic-amp=128`、`fuxi,haptic-env=1`、`fuxi,haptic-ui=15`、`fuxi,inject` `2->14`、`fuxi,wifi-rssi=1`、`fuxi,wifi-sb=1`、`fuxi,wifi-ux=31`、`/product/artwork-display-gamut` `P3->sRGB`、`/product/haptics` 與 `/wlan` 節點。
- **問題／現象**：將散落於多趟實驗的核心調校、原生 Wi-Fi（回退後 `ux=31`）、sRGB、震動執行期與雙系統選單逐步合併進日常開機鏈。
- **根因**：各硬體功能在不同實驗 loader 驗證完成，須統一收斂至日常映像並保留可對照的開關差異清單。
- **做法**：
  - `chain-v7-ram9g-rtctorch`（`6cc9feb` 2026-11-18 00:10）：整合 9g RAM、ADT RTC 校時與手電筒五檔調校。
  - `chain-v8-dualboot-wifi`（2026-11-18 03:14）：以 `chain-v7` 為基底合併 `menu-direct-launch-20261118`、`kernel-local-wifiux2-9g-artifact`（`ux=31`，排除已拒絕的 `ux=127`）與共享 iOS root（`sb-local-hapticamp-sbwifi-artifact`），並產生完整比對檔 `work/dualboot/daily-merge-20261118/comparison.json`。
  - `chain-v9-dualboot-launching`（`b37dcde` 2026-11-18 04:26）：合併「進入系統中」提示選單（`menu-launching-20261118`），同步安裝於 `boot_a` 與 `boot_b`。
- **復現**：
  ```powershell
  # 對照 daily-merge-20261118/comparison.json 內列舉之 loader 與參數，使用 dualboot/build.py 建置並以 deploy-verified.py 部署
  ```
- **主機測試**：`comparison.json` 記錄 380 項 dualboot 主機斷言、32 項 `deploy-verified` 恢復測試、已存 815,061-byte FDT 重播皆通過。
- **驗證紀錄**：`chain-v8` 03:10:44–03:13:42 RAM iOS 實機啟動見 SpringBoard、原生掃描、`join finished 0` 與回程正常（`work/dualboot/daily-v8-ios-validation-20261118/report.json`）；`chain-v9` 04:14–04:45 `boot_a` 與 `boot_b` 皆通過實機進 SpringBoard（boot #92、#93，無 panic）。
- **參考來源**：`dualboot/`、`work/boot-chain/`。
- **結論／未完成**：合併完成；`comparison.json` 明列尚未驗收項目：Wi-Fi 掃描加速與 300 Mbps 目標、互動觸覺、藍牙與喇叭音訊、b 槽 Android 與冷開機、選單 Fastboot／重啟／關機。
- **出處**：`ws:work/dualboot/daily-merge-20261118/comparison.json`；ws 提交 `6cc9feb`、`b37dcde`；`dualboot/VALIDATION.zh-TW.md`。（2026-11-18 主代理分配範圍與來源，Gemini 依原始檔撰寫，主代理核對欄位、提交時間與路徑）

### B-008 獨立開機 console 紀錄的主機端（mtdblk 擷取與解析）

- **日期**：2026-11-16（`c75a3b6` 納入追蹤）
- **狀態**：日常使用中（每次 `launch-loader-check.ps1` 或 `start-daily.py` 進 iOS 前皆自動拉回上一趟紀錄）
- **開關**：`fuxi-log.sh <survey|stamp|pull>`；`fuxi-log-parse.py <area.bin> <out.txt>`；ADT `fuxi,log=1`（核心端見 K-019）
- **問題／現象**：小米 13 在 b 槽／日常獨立開機時沒有接電腦 USB，且裝置沒有 `logdump` 分區，若 iOS 發生問題無法取得該趟核心 console。
- **根因**：必須借用 Android／bootloader 皆不使用的空分區 `mtdblk`（`/dev/block/sda19`，32 MiB）作為持久化 console 環形緩衝區，並由主機端安全蓋章與解析。
- **做法**：
  - `work/adsp-battery/hw000/fuxi-log.sh`：
    - 持有 `WORK/PHONE-LOCK-000` 鎖、檢查 Android 已啟動與 `mtdblk` 大小足夠 `2049 * 4096` bytes。
    - `survey`：唯讀檢查 `mtdblk` 實體裝置、非零位元組數與標記。
    - `stamp`：先將整顆 `mtdblk` 備份至 `D:\xiaomi13\fuxi_Backup\mtdblk-<日期>\mtdblk.img` 並核對雜湊，再於區塊 0 的 byte 2048 寫入 `"FUXILOG1 STAMPED"` 並讀回確認（未蓋章前核心一字不寫）。
    - `pull`：讀回前 2049 個 4 KiB 區塊至 `work/adsp-battery/fuxilog/<時間>/area.bin`，呼叫 `fuxi-log-parse.py` 解析為 `console.txt`。
  - `work/adsp-battery/hw000/fuxi-log-parse.py`：解析 `SWEETLOG` v2 標頭（含 `boot`、`seq`、`secs`、`total`、`head`），將前 4 MiB head（區塊 1025..2048）與最後 4 MiB ring（區塊 1..1024）拼回完整文字。
- **復現**：
  ```bash
  # 首次蓋章（會先備份 mtdblk）
  bash work/adsp-battery/hw000/fuxi-log.sh stamp
  # 拉回並解析上一趟獨立開機紀錄
  bash work/adsp-battery/hw000/fuxi-log.sh pull
  ```
- **主機測試**：無
- **驗證紀錄**：2026-11-16 07:46 完成備份與蓋章，bl7t 實測拉回 799,006 bytes（139 次寫入），包含 USB 監看停止後的電量讀數；截至 2026-11-18 已穩定記錄至 boot #96（`adsp-battery/fuxilog/20261118-122545`）。
- **參考來源**：核心端 `fx_log`（K-019）。
- **結論／未完成**：已穩定運作；每趟開機會覆寫上一趟紀錄，故每次進 iOS 前須先 `pull`。
- **出處**：`ws:work/adsp-battery/hw000/fuxi-log.sh`、`fuxi-log-parse.py`；ws 提交 `c75a3b6`；`work/hw000-adaptation-status.zh-TW.md` L14, L388。（2026-11-18 主代理分配範圍與來源，Gemini 依原始檔撰寫，主代理核對欄位、提交時間與路徑）

### B-009 上機前差分備份工具

- **日期**：2026-11-16（`c75a3b6` 納入追蹤）
- **狀態**：已採用（用於建立 `ios-root-hw000-*` 等多層與單層差分還原點）
- **開關**：`hw000-delta-backup.py --backup-base <raw> --backup-base-sha <sha256> --name <暫存名> --keep-delta <新備份目錄> [--parent-snapshot <父還原點>] [--ntfs-compress] [--resume-download]`
- **問題／現象**：完整 iOS root 為 20 GiB，每輪若複製整份會耗盡 D: 空間並與 001 線的 `delta-pull-inplace.py` 衝突；且備份過程必須絕對唯讀，不能改動手機上的 iOS root。
- **根因**：需要獨立的唯讀差分拉回工具，只保存相對於固定基底（或父還原點）變動的 4 MiB 區塊。
- **做法**：
  - 檢查 Android 已啟動、`mapper`（`/dev/block/mapper/codexnative_root_1108b`）大小為 20 GiB 且掛載數為 0。
  - 若指定 `--ntfs-compress`，對目標目錄啟用 Windows NTFS 壓縮（`compact.exe /C /I /Q`）並用 `GetFileAttributesW` 驗證。
  - 推送並執行手機端 `blockhash.sh` 計算全 5,120 個 4 MiB 區塊的 SHA-256，存為 `phone-blocks.txt`。
  - 核對 `--backup-base` 整份雜湊，若指定 `--parent-snapshot` 則透過 `root_layers.py` 的 `load_layers()` 計算父層狀態並寫入 `parent.json`。
  - 找出差異區塊（上限 2,000 塊），由手機端打包成 `pull.bin` 拉回主機，逐塊驗證雜湊並重組驗證整份 SHA-256；支援 `--resume-download` 於中斷後重核手機與父層再續拉。
- **復現**：
  ```powershell
  python -I work\adsp-battery\hw000\hw000-delta-backup.py `
    --backup-base D:\xiaomi13\fuxi_Backup\ios-root-images\native-layout0025-root.img `
    --backup-base-sha 9dd1e7a05186de8d5cb0dff1410a59c4e8bf4a859347165d067a7b62011bc0d9 `
    --name hw000-20261116 --keep-delta D:\xiaomi13\fuxi_Backup\ios-root-hw000-20261116
  ```
- **主機測試**：`work/statusbar-fix/test-root-layers.py`（測試多層重建與 `SnapshotReader`）。
- **驗證紀錄**：已成功建立 `ios-root-hw000-20261116`、`hw000sb`、`hw000shape`、`hw000shape2`、`hw000provider`、`hw000torch-runtime`（含 `--resume-download` 續驗成功）及 `dualboot-before-validation-20261118-014941/ios-root`。
- **參考來源**：`work/statusbar-fix/root_layers.py`。
- **結論／未完成**：完成；2026-11-16 13:37 起日常迭代改用 `phone-readthrough.py` 加速，本工具保留用於重大節點前完整還原點建立。
- **出處**：`ws:work/adsp-battery/hw000/hw000-delta-backup.py`；ws 提交 `c75a3b6`。（2026-11-18 主代理分配範圍與來源，Gemini 依原始檔撰寫，主代理核對欄位、提交時間與路徑）

### B-010 loader 安全核對

- **日期**：2026-11-08～2026-11-18 05:37（`b7d80cf`、`c4de3f4`、`2257c91`、`c75a3b6`、`2442f1f`）
- **狀態**：日常使用中（每次上機前強制核對 iOS 映像 FIEMAP、pin 狀態與 loader ADT extents）
- **開關**：`verify-native-map.py`（無參數）；`verify-loader-safety.py <loader 目錄名>`；`verify-args-on-loader-payload.py <loader 目錄名>`；`check-pin-readonly.c`（`check-pin-readonly <檔案...>`）
- **問題／現象**：iOS 直接透過 ADT `fuxi-root-extents` 對 UFS 實體區塊做讀寫；若 iOS root 檔案未固定（pin）、被移動過或與其他候選映像重疊，開機寫入會直接毀損 Android userdata 分區。
- **根因**：必須在任何 loader 寫入或暫時開機前，將手機實際 FIEMAP／DM-linear／pin 狀態與 loader 內建 payload／ADT／核心／RAM layout／RTC 校時逐一比對。
- **做法**：
  - `check-pin-readonly.c`：唯讀開啟檔案（`O_RDONLY|O_CLOEXEC|O_NOFOLLOW`），呼叫 `FS_IOC_GETFLAGS` 與 `_IOR(0xf5, 14, uint32_t)`（`GET_PIN`）確認 `flags=0x800000` 且 `moved=0`。
  - `verify-native-map.py`：在手機上執行 `codex-inspect-fiemap` 檢查所有 iOS 映像檔無自重疊且彼此互斥（disjoint）、檢查 `codex-check-pin` 與 `codex-inspect-native-dm` 目標完全吻合，並輸出 `managed-native-map-current.json`。
  - `verify-loader-safety.py`：核對 `payload.bin`、`boot.img`、`kernel.development.fuxi` 雜湊、Mach-O `cputype`、實機 `ddr_size` 12GB SKU、9g/5g `dram-base`/`dram-size`/`fuxi-gpu-memory`、ADT `fuxi-root-extents` 與 `managed-native-map-current.json` 一致、`fuxi-root-blocks` 為 20 GiB，以及若命令列帶 `fuxirtcoffset=` 則呼叫 `rtc-calibration.py` 核對即時裝置計數器。
  - `verify-args-on-loader-payload.py`：專用於僅修改 boot-args 的變體 loader，驗證除了 `swaps.txt` 指定差異外，其餘 `payload.bin` 位元組（核心與 ADT）與來源完全一致，且無越界實驗參數混入。
- **復現**：
  ```powershell
  python -X utf8 work\xiaomi13-ios-gpu-20261108\verify-native-map.py
  python -X utf8 work\Codex-Xiaomi13-GPU-20261109\verify-loader-safety.py <loader目錄名>
  ```
- **主機測試**：`work/boot-chain/test-loader-sku-preflight.py` 覆蓋 `verify-loader-safety.py` 的 SKU 檢查路徑。
- **驗證紀錄**：所有 000 線與 dualboot 上機趟次皆經此安全核對鏈把關，未發生因 extents 錯位導致的 Android 資料毀損。
- **參考來源**：`work/Codex-Xiaomi13-GPU-20261109/kernel-source-979/tools/{adt.py,mkpayload.py}`、`work/adsp-battery/rtc-calibration.py`。
- **結論／未完成**：完成並持續作為上機必要閘門。
- **出處**：`ws:work/Codex-Xiaomi13-GPU-20261109/verify-loader-safety.py`、`verify-args-on-loader-payload.py`、`work/xiaomi13-ios-gpu-20261108/verify-native-map.py`、`check-pin-readonly.c`；ws 提交 `b7d80cf`、`c4de3f4`、`2257c91`、`c75a3b6`、`2442f1f`。（2026-11-18 主代理分配範圍與來源，Gemini 依原始檔撰寫，主代理核對欄位、提交時間與路徑）

### B-011 a 槽選單顯示倒數，10 秒後自動進 Android

- **日期**：2026-11-18 13:52–13:58（台北時間，工作區 e6e8ec8）
- **狀態**：已寫入 a 槽，Android 自動交接與讀回通過
- **開關**：dualboot/build.py 的 --timeout 預設 10 秒、0 等待手動選擇、1–60 秒自動倒數。--default 改初始焦點，逾時固定選 Android。
- **問題／現象**：常駐選單原先停在首頁等待選擇。日常開機需要可見倒數和無操作自動進 Android。
- **根因**：舊版常駐映像建於停用自動倒數的設定。原選單已有 timer 與取消事件，可沿用其狀態機。
- **做法**：menu.c／menu.h 讓倒數到期交接 Android，render.c 繪製秒數，board.c 只在必要時更新。任一實體按鍵取消當次倒數，音量移焦點、電源短按確認。install-persistent.py 登記通過 RAM 試開的輸入，核對上一版分區內容後升級並讀回。
- **復現**：依 dualboot/README.zh-TW.md 取與在機版本相符的 Android／iOS 來源，以 dualboot/build.py --timeout 10 --output <候選目錄> 建置。RAM 驗證用 `.\work\statusbar-fix\launch-loader-check.ps1 -DualBootCheck android -RestorePoint <已核對還原點> -Candidate <候選目錄> -Out <RAM 趟次>`。已登記且 RAM 通過的候選用 `.\work\statusbar-fix\launch-loader-check.ps1 -UpgradePersistent -InstallReport <現行安裝報告目錄名> -Out <升級趟次>`。歷史產物 menu-android-countdown-current-20261118 的 SHA-256 為 1e1b149b40170bc55b891229cbec88cb03d9573892f614bdf1c9b508d54347a0。
- **主機測試**：dualboot/tests/host.c 共 454 項斷言，涵蓋倒數、按鍵取消與交接。可用 python -X utf8 dualboot/build.py --host-only --output <新的輸出目錄> 重跑。
- **驗證紀錄**：13:52:42–13:54:05 RAM 試開，13:54:44–13:56:05 寫入並讀回。Android sys.boot_completed=1，交接位址 0xa9210000，a 槽 active／successful。內嵌 Android 核心與 iOS payload 雜湊對上 menu-launching-20261118。回退點見 R-028。
- **參考來源**：[雙系統驗證](../../dualboot/VALIDATION.zh-TW.md) · [選單操作與建置](../../dualboot/README.zh-TW.md)
- **結論／未完成**：10 秒自動進 Android 已在安裝版驗證。iOS／Recovery 的三路驗收是 04:45 版的結果。新版另有主機驗證與 payload 相符佐證。
- **出處**：work/dualboot/countdown-ram-validation-20261118/report.json、work/dualboot/persistent-slot-a-upgrade-countdown-20261118/report.json。升級目錄一併保留回退映像與讀回報告。

### B-012 日常 a 槽整合 Wi-Fi 修正與前置鏡頭對齊

- **日期**：2026-11-18 15:24 起。
- **狀態**：日常 a 槽已寫入並讀回。10 秒自動進 Android 已在常駐版驗證。
- **開關**：從 B-011 保留 10 秒倒數。ADT 採用 `fuxi,sbshape=300103`、`fuxi,wifi-ux=2047`、`fuxi,inject=30`、`fuxi,wifi-sb=3`。
- **問題／現象**：原日常狀態欄的圖示中心在 87.5 px，使用者回報偏低。掃描、手動密碼框與熱點圖示修正原先使用暫時開機的 loader。
- **根因**：常駐選單內嵌前版 iOS payload，狀態欄高度仍為 127，暫時開機使用的新 Wi-Fi 核心及 ADT 開關尚未進入該 payload。
- **做法**：以 U-001 確認的高度 103，合併 K-073～K-075、U-018、U-019。使用本機 9g 核心 `kernel-local-wifi-password-prompt-1118-9g-artifact`（ksrc `8cd0756`）及已寫入根映像的 `libfuxi-sb.dylib`（harness `d2b447e`）。將已試開的 iOS payload 雜湊與新選單一起登記在 `dualboot/install-persistent.py`。
- **復現**：使用 U-019 的 loader 產生指令，加上 `--statusbar-height 103`、`--name native-daily-wifi-sb103-1118-loader`，並帶入當次 RTC 校時。依序使用現有工具建置基底分流器和選單。

  ```powershell
  python -X utf8 -B work/boot-chain/build-chain.py chain-daily-wifi-sb103-1118 --payload native-daily-wifi-sb103-1118-loader --auto-launch-s 30 --ram-sku 12g
  python -X utf8 -B dualboot/build.py --base-chain work/boot-chain/chain-daily-wifi-sb103-1118 --output work/dualboot/menu-daily-wifi-sb103-20261118 --default android --timeout 10
  ```

  執行 B-011 的 RAM Android 驗證，再用 `-UpgradePersistent -InstallReport persistent-slot-a-upgrade-countdown-20261118` 寫入經登記的候選。常駐 iOS 可用 `-ValidatePersistent -IosOnly` 驗證。
- **主機測試**：454 項選單斷言、基底重連結、payload 雜湊及 Android 核心一致性通過。安裝器接受原版和已驗證新版，拒絕雜湊不符的 iOS payload。
- **驗證紀錄**：15:24 的高度測試得到 63.5 px，實體對齊由使用者確認。15:34 的合併版實測載入狀態欄、密碼框及熱點修正，在 5200／5745 MHz 加入成功，使用者核對正常。15:39 RAM 選單回 Android，15:42 安裝版回 Android 並釋鎖。
- **參考來源**：共用部署工具、U-001 與 [雙系統驗證](../../dualboot/VALIDATION.zh-TW.md)。
- **結論／未完成**：常駐版倒數與 Android 回程通過，使用者手動選擇 iOS 並確認日常版正常。
- **出處**：`work/dualboot/menu-daily-wifi-sb103-20261118/report.json`、`work/dualboot/persistent-slot-a-upgrade-wifi-sb103-20261118/report.json`。安裝目錄包含前一版可還原的 `boot_a` 映像。

### B-013 日常 a 槽整合原生藍牙、關藍牙修補與 Safari 影片前置

- **日期**：2026-11-20 09:43–10:01。
- **狀態**：日常 a 槽已寫入並讀回，10 秒自動進 Android 在常駐版通過。10:47 確認使用者從常駐選單進 iOS（第 174 次開機）、在設定頁關→開藍牙 5 輪、回 Android，都通過。
- **開關**：保留 B-012 的倒數、狀態欄高度 103 與 Wi-Fi 開關。新增原生藍牙：`fuxi,bt=5`、`--bt-native`（`sweetnoexec` 不再擋 bluetoothd）、IBS、`bt-optional`、`bt-pad`、`bt-mask`、`bt-txmask`、3.2 Mbaud、`bt-nolog`、控制器身分節點，以及 U-023 的 `fuxi,bt-fix=3`、`fuxiprivs=1`、disp0 `fuxi,mfb-align=1`。`fuxi,inject` 從 30 改為 126。不帶 `fuxi,bt-oslog`、`fuxi,bt-link-log`、點擊腳本或 bluetoothd stderr。
- **問題／現象**：K-077 的原生藍牙、U-023 的關藍牙修補和 Safari 非 DRM 影片前置（趟 22 的 `mfb-align`）都只在暫時開機的測試 loader。日常 boot_a 仍是 B-012，iOS 進去沒有藍牙。
- **根因**：常駐選單內嵌的是 B-012 的 iOS payload，核心與 ADT 都還沒有這些開關。
- **做法**：
  - 核心沿用趟 32／33 的本機 9g `kernel-local-bt-complete-1120-9g-artifact`（SHA-256 `9d151b75050d9058eb33533fec198ddb7523940f01251149a50e00b1314a95b2`）。
  - `libfuxi-sb.dylib` 沿用趟 33 以 `-Dylib` 寫進根映像的 `sb-local-btfix-h4main-1120-artifact`（SHA-256 `c2c3184af48cb7288392e5c45182770d9aa78a98203b5eedcc12b87773a15ef1`，9 塊 PRE_OK／WRITE_OK）。這次沒有改根映像。
  - 產生器修正：`--bt-native` 移除 `sweetnoexec` 裡的 bluetoothd 改在 `arguments()` 之前，命令列長度檢查才量到最後結果。原本先量後刪，日常版被誤判為 615 bytes，實際是 604。token 順序不變，`native-bt5-btfix-h4main-1120-loader` 的命令列可以逐位元重現。
  - `dualboot/install-persistent.py` 登記新選單與 iOS payload 雜湊，允許從 B-012 升級。`dualboot/phone-check.py` 認得新的安裝報告。
- **復現**：
  1. 校時：`.\work\statusbar-fix\launch-loader-check.ps1 -CalibrateRtc -Out daily-bt-1120`。輸出的 JSON 含 adb 序號，不收進紀錄。
  2. 產生 loader：

     ```powershell
     python -X utf8 -B work/adsp-battery/prepare-wlan-w18-loader.py build --like native-daily-wifi-sb103-1118-loader --wifi-ios --artifact kernel-local-bt-complete-1120-9g-artifact --name native-daily-bt-1120-loader --rtc-calibration work/adsp-battery/rtc-calibration-daily-bt-1120.json --scan-dwell --scan-batch --quiet-boot --stderr-log Preferences --with-adsp --audio-spf --haptic 2 --inject-media --product-haptics --haptic-ui-ms 15 --haptic-envelope --haptic-amp 128 --wifi-audit --wifi-password-prompt --wifi-sb-audit --wifi-sb-refresh --wifi-rssi-bounds --wifi-ux 2047 --display-gamut sRGB --daily-tuning --wifi-args-adt --statusbar-height 103 --bt 5 --bt-native --bt-ibs --bt-optional '0x0c5b,0xfd3c,0xfd47' --bt-pad 0xfc00 --bt-mask '0x2003:2:0x30' --bt-txmask '0x200b:5:1,0x200d:12:1,0x2006:5:1,0x2041:0:1,0x2043:1:1' --bt-baud 3200000 --bt-nolog --bt-controller-identity native-hw000bt5-mfbalign-1119 native-hw000bt5-fce9-status-1119 --bt-fix 3 --privs --mfb-align
     ```

     `--like` 只複製平台選項，功能開關都要明列。結果是命令列 604 bytes、payload `6d8a0b7c450bea4928817c26cb7fd2874264ca304923526ca11db902275b8734`、boot.img `88dbf60baa872ff90402b8aabf69ce6039e37a71b27bb255c07393e00ee5fbd9`。
  3. 建基底分流器與選單：

     ```powershell
     python -X utf8 -B work/boot-chain/build-chain.py chain-daily-bt-1120 --payload native-daily-bt-1120-loader --auto-launch-s 30 --ram-sku 12g
     python -X utf8 -B dualboot/build.py --base-chain work/boot-chain/chain-daily-bt-1120 --output work/dualboot/menu-daily-bt-20261120 --default android --timeout 10
     ```

  4. 暫時開機 payload：`-Loader native-daily-bt-1120-loader -Kernel kernel-local-bt-complete-1120-9g-artifact -Out native-hw000bt-daily-1120`。
  5. RAM 選單試開：`-DualBootCheck android -RestorePoint D:\xiaomi13\fuxi_Backup\dualboot-before-validation-20261118-014941 -Candidate work\dualboot\menu-daily-bt-20261120 -Out daily-bt-ram-android-20261120`。
  6. 寫入：`-UpgradePersistent -InstallReport persistent-slot-a-upgrade-wifi-sb103-20261118 -Out persistent-slot-a-upgrade-daily-bt-20261120`。
  7. 常駐 iOS 驗證：`-ValidatePersistent -IosOnly -InstallReport persistent-slot-a-upgrade-daily-bt-20261120 -Out persistent-slot-a-validate-daily-bt-20261120`，要在 10 秒倒數內按音量鍵選 iOS。10:23 這趟倒數到期自動進了 Android（前後都是第 173 次開機），判為 `No fresh installed iOS boot to SpringBoard`。使用者之後自己重開選 iOS、測完回 Android，再用 `-ResumeAfterIos persistent-slot-a-validate-daily-bt-20261120 -Out persistent-slot-a-validate-daily-bt-20261120-r2` 接續核對同一份開機前紀錄。
- **主機測試**：選單 454 項斷言通過，`SAVED_FDT_REPLAY_OK`，基底重連結逐位元相同（`base_relink_identical`）。Android 核心 `f5acfde209dbc87f86ed5f0ecff1dc52d3b51638cc30932fd5cab38e7f71afee` 與 B-012 相同。產生器自測通過，修正前後同一組參數產生相同命令列。
- **驗證紀錄**：
  - 趟 34（09:46:51 開機、09:57:37 回 Android 並釋鎖）的 boot.log 共 7397 行，SHA-256 `d41ec9d406c2994f360601b2a31a1f69fe5530a8fb6bcce3dfaff22f27ec1bf7`。
    - 第 908 行 `exec SpringBoard`，第 994 行 `exec bluetoothd`，全程只執行 1 次。
    - 第 3324 行 `setup status=ok`，韌體 1427 ms。第 3333 行 3.2 Mbaud 生效，第 3338 行 `resident ready baud=3200000`。
    - 第 4476 行 Wi-Fi `join finished 0`。
    - 最後的 stats 為 `links=1`、`dropped=0`、`RXDIAG desync=0`、`wake_timeouts=0`。0 次 panic，bluetoothd 0 次 CRASH。
    - AegirPoster SIGABRT 59 次，是使用者操作鎖定畫面／桌布時的既有現象：同日手動那趟有 78 次，自動那幾趟 0 次。與這份 payload 無關。
    - 主機 TSENS 最高 48.5°C。上一趟的 mtd 紀錄先拉到 `work/adsp-battery/fuxilog/20261120-094631`（第 172 次開機）。
  - 09:58:45–10:00:02 RAM 選單試開，10 秒後自動進 Android，交接位址 `0xa9210000`，boot 分區與槽位未變。
  - 10:47:01–10:47:07 接續核對：`INSTALLED_IOS_AND_RETURN_VERIFIED`，`ios_boot_number=174`，回程交接位址 `0xa9210000`。mtdblk 紀錄 42696 行，SHA-256 `c1bf885dda95ee84a0d397f390ddb2d79288fbb6f926a201336620f5db1202ee`。
    - 第 908 行 `exec SpringBoard`，0 次 panic。第 987 行 `exec bluetoothd`，全程只執行 1 次，0 次 `CRASH bluetoothd`。
    - 第 3377 行 `setup status=ok`（韌體 1399 ms），第 3391 行 `resident ready baud=3200000`。第 4564 行 Wi-Fi `join finished 0`。
    - 關→開 5 輪：第 36812、37508、38319、39486、40887 行 `FUXI BT SK: disconnect unit=1 owner=85 caller=85 owner_exiting=0`，每次後面都接 `connect unit=1 pid=85`（最後一次在第 41552 行），共 6 條 link。
    - 最後的 stats 為 `links=6`、`acl=1708/695`、`desync=0`、`wake_timeouts=0`。`dropped=67` 都發生在第 5 輪關閉後、`rings=0` 的期間，是主機斷線後控制器送來的事件，重新連線後不再增加。
    - AegirPoster SIGABRT 66 次，仍是鎖定畫面／桌布的既有現象。`lskdd` 反覆重啟（已知，見 K-077）。
  - 10:00:23–10:01:42 寫入：`IOS_ROOT_EXTENTS_AND_PIN_OK`、`BOOT_A_READBACK_OK`、`UPGRADED_ANDROID_VERIFIED`。整份 boot_a 的 SHA-256 是 `56565b1ca60cc99d446ea55dc66653e3c053c1c861729e014f3f0bf65fdfb63d`，候選 `0c1a02c3a6a947731d943bc8dff51d779db01b734069be9b52d863d48569c124`。前一版回退映像 `boot-a-previous-menu.img` 的 SHA-256 是 `c69fbbf3c1fe60f9449d2bdbf17006ed22f4d5c8e17b3042f64b69af65e86fc2`。
- **參考來源**：B-011／B-012 的部署流程、K-077、U-023 與 [雙系統驗證](../../dualboot/VALIDATION.zh-TW.md)。
- **結論／未完成**：
  - 日常版已經有原生藍牙、關藍牙修補與 Safari 非 DRM 影片前置，安裝版 Android 回程通過。
  - 常駐選單的 iOS 路徑與日常版關→開藍牙（5 輪）已驗。日常版不收 bluetoothd stderr，判斷依核心的 `SK: disconnect … owner_exiting=0`、同 pid 重新 `connect`、`exec bluetoothd` 次數與 `CRASH`。
  - 待驗：日常版耳機回連與聲音（ACL 有 1708／695 筆，但核心看不到是哪個裝置）、Safari 影片、長時間穩定性。
  - 要退回 B-012：用升級目錄內的 `boot-a-previous-menu.img`。
- **出處**：
  - `work/Codex-Xiaomi13-GPU-20261109/native-hw000bt-daily-1120/boot.log`
  - `work/dualboot/menu-daily-bt-20261120/report.json`
  - `work/dualboot/daily-bt-ram-android-20261120/report.json`
  - `work/dualboot/persistent-slot-a-upgrade-daily-bt-20261120/report.json`（含回退映像）
  - `work/dualboot/persistent-slot-a-validate-daily-bt-20261120/report.json`（倒數到期那趟）、`work/dualboot/persistent-slot-a-validate-daily-bt-20261120-r2/report.json` 與 `after-ios/previous-console.txt`

### B-014 日常版全寬填滿與前鏡頭狀態欄間距

- **日期**：2026-11-20
- **狀態**：日常 iOS payload 已暫時開機；校正版已寫入常駐 boot_a，寫後整區讀回及選單倒數回 Android 通過。
- **開關**：B-013 的完整指令加 `--display-fit fill --statusbar-width 360`，並將 `--statusbar-height 103` 改為 `86`。日常維持 mode 2 震動，命令列仍為原來的 604 bytes。
- **問題／現象**：鍵盤左右被裁，等比模式上下有黑邊。填滿後狀態欄偏低，時間與電池偏內。
- **根因**：舊狀態欄是為置中裁切座標調整。全寬填滿時，同一組尺寸使兩側中心落在面板 y=80。
- **做法**：從 B-013 的確切 ksrc `231479a` 單獨移植顯示及觸控修改成 `b4361c6`，harness 從 `da0ac11` 套入 `a3fbb97`。與 B-013 私有 ADT 的 /chosen 除了 RTC 只差 `fuxi,fit` 和 `fuxi,sbshape`。Android 映像雜湊與 10 秒預設 Android 均沿用 B-013。
- **復現**：依 B-013 第 1～3 步，用當次 RTC、核心 `kernel-local-daily-fit-1120-9g-artifact`、loader `native-daily-fit-sb360x86-1120-loader`、基底 `chain-daily-fit-1120` 與選單 `menu-daily-fit-20261120`，套用本條目開關。`build-kernel.ps1 -Ram 9g -Ath12k work/adsp-battery/ath12k-objects-b2876c6 -Tests`。此處本機使用裝有日常補丁的獨立 harness worktree，完整 loader 指令存於 `work/localbuild/display-fit-1120/daily-build.txt`。
- **主機測試**：日常核心 41 個 workflow 命令通過。選單 454 項斷言、FDT 重播、基底逐位元重連結通過。
- **驗證紀錄**：趟 38 使用者確認實體前鏡頭高度及兩側間距。日常 payload 趟 39 暫時開機進 SpringBoard，載入 360×86，Wi-Fi 加入成功、藍牙 3.2 Mbaud。與趟 37 相比，時間中心 (213,80) → (198.5,63)，右側圖示中心 (894,80) → (908,63)。舊版經使用者確認的垂直中心是 63.5。量測來源 `native-hw000-daily-fit-sb360x86-1120/framebuffer.png` 頂部 200 列。
  - 趟 40 RAM 選單試開紀錄 `work/dualboot/daily-fit-ram-android-20261120/report.json`。候選 `2bc74cd7561d147f092e13d55e75adaed68992c276a9fa483880236512819e29`、交接位址 `0xa9210000`，槽位與 boot 分區未變。
  - 趟 41 由 B-013 的安裝報告升級。12:20:35 iOS root extents 與 pin 核對成功；12:20:40 寫後完整 boot_a 讀回 `f6b4acb161dfdab43bd9348f136139c626bd13adce4007a4d40972a1dae899e8`。12:21:46 常駐選單倒數進 Android 通過。回退映像 `work/dualboot/persistent-slot-a-upgrade-daily-fit-20261120/boot-a-previous-menu.img`，SHA-256 `56565b1ca60cc99d446ea55dc66653e3c053c1c861729e014f3f0bf65fdfb63d`，與升級前完整 boot_a 相同。安裝報告在同目錄 report.json。
- **參考來源**：B-013 的日常部署、U-001 的 FixedSplit 避讓區與 G-050 的全寬顯示。
- **結論／未完成**：常駐 boot_a 與 Android 路徑已驗；同一 iOS payload 的 SpringBoard 與間距在趟 39 的 RAM 試開驗證。升級後從實體按鍵選 iOS 的路徑未另測。
- **出處**：`work/dualboot/menu-daily-fit-20261120/report.json`、`work/Codex-Xiaomi13-GPU-20261109/native-hw000-daily-fit-sb360x86-1120/`。
