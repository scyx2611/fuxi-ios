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

### B-015 以面板比例排版的日常版

- **日期**：2026-11-20
- **狀態**：日常 payload 暫時開機通過，鍵盤、狀態欄與滑動已實機驗收。已安裝至 boot_a，整區讀回與常駐選單回 Android 均通過。
- **開關**：`--display-fit aspect` 設定 ADT `/chosen fuxi,fit=3` 及 disp0 的 `canvas-height=2500`。保留 B-014 的狀態欄 360×86、震動 mode 2、Wi-Fi、藍牙與原 604-byte 命令列。
- **問題／現象**：B-014 填滿時，垂直與水平縮放率相差 2.63%。直接改成面板像素尺寸的趟 42 出現紅色狀態欄、注音鍵盤右端被裁、Metal 退回 CPU。
- **根因**：360 pt 寬無法套用日常的 UIKit 狀態欄及鍵盤排版；另在匯入 1080×2400 顯示表面時出現 HostMemory 拒絕，見 G-050。
- **做法**：保留 375 pt 寬度，邏輯畫布加高 64 列到 1125×2500。Adreno 對 CA 表面的寬高皆乘以 24/25，觸控以整個 1080×2400 面板回報。
- **復現**：在 B-014 乾淨核心與 harness 上分別套用 `3628125` 與 `f959f54`。執行 `work/localbuild/build-kernel.ps1 -Ram 9g -Tests -Name kernel-local-daily-aspect-1120-9g-artifact -Harness work/localbuild/display-fit-1120/harness -Ath12k work/adsp-battery/ath12k-objects-b2876c6`。依 B-014 的指令改用此核心、上述開關與 loader `native-daily-aspect-sb360x86-1120-loader`。完整指令見 `work/localbuild/native-canvas-1120/aspect-build.txt`。依 B-013 的選單建置命令使用 `chain-daily-aspect-1120`、`menu-daily-aspect-20261120` 及 10 秒預設 Android。
- **主機測試**：本機 WSL 通過 41 個工作流程命令，ISA 禁用指令 0。Adreno 模型重播第三種縮放封包，檢查完整覆蓋、取樣與寫入邊界。另用 1125×2500 輸入重播 SweetFramebuffer 的 CPU 全畫面及局部更新。舊核心製作 aspect loader 的負測試依預期拒絕。乾淨 main 的產生器重建出相同 payload。選單通過 454 項斷言、FDT 重播與基底重連結核對。
- **驗證紀錄**：趟 43 使用者驗收狀態欄、鍵盤兩側點按及滑動。在 mtd 最後一次回報中 backboardd 以 Metal 合成 8967 格，failures=0。12:47:11 回 Android。此趟全高截圖跨過鎖屏過場，右側圖示中心可量到 (908,61.5)。趟 44 全高擷取中斷；趟 45 改於兩個手勢標記各取頂部 200 列，兩圖皆見正常時間、Wi-Fi 與電池。時間 x=143..254、右群 x=843..973，兩者中心 y=61.5。與 B-014 比較，水平中心同為 198.5／908，整體上移 1.5 px。截圖在 `work/Codex-Xiaomi13-GPU-20261109/native-hw000-aspect-topshots-1120/`。
  - 趟 46 RAM 選單 10 秒倒數回 Android，交接位址 0xa9210000、槽位與 boot 分區相同。趟 47 從 B-014 升級，12:58:03 全 boot_a 讀回相符。12:59:10 常駐倒數回 Android 通過，槽位 _a。整區 SHA-256 `6a6ef1acf343839504a714d0f089768df529feb833c363da4a3a2ed84b841f26`。升級目錄保存前版完整 `boot-a-previous-menu.img`，SHA-256 `f6b4acb161dfdab43bd9348f136139c626bd13adce4007a4d40972a1dae899e8`，與 B-014 整區相同。
- **參考來源**：G-050 的 Adreno、SweetFramebuffer 與 SweetHID 路徑，U-001 的 FixedSplit 及 B-014 選單、回退流程。
- **結論／未完成**：等比顯示、觸控、Metal 合成在日常 payload 的 RAM 試開驗收。寫入驗證涵蓋整區讀回與常駐 Android 自動交接；此次安裝後未另以按鍵從新選單進入 iOS。
- **出處**：`work/Codex-Xiaomi13-GPU-20261109/native-hw000-daily-aspect-1120/`、`work/adsp-battery/fuxilog/20261120-124727/console.txt`、`work/dualboot/menu-daily-aspect-20261120/report.json`、`work/dualboot/daily-aspect-ram-android-20261120/report.json`、`work/dualboot/persistent-slot-a-upgrade-daily-aspect-20261120/report.json`。

### B-016 動態 RTC 校時日常版（persist:/time/ats_2 與 timed 持久化）

- **日期**：2026-11-22
- **狀態**：方案 A、A+B 自測、純 B 獨立還原、使用者互動檢視與日常 payload 暫時開機全數通過；已安裝至 `boot_a`，整區讀回與常駐選單倒數回 Android 均通過。
- **開關**：`--rtc-persist --rtc-user 1`（ADT `/chosen fuxi,rtc-persist=1`、`fuxi,rtc-user=1`、`fuxi,inject=382`），移除靜態 `--rtc-calibration`／`fuxi,rtc-offset`。完整保留 B-015 的等比 `1125×2500`（`fuxi,fit=3`）、狀態欄 `360×86`、震動 mode 2、Wi-Fi、原生藍牙（`bt-fix=3`）、ADSP 與 604-byte 命令列。
- **問題／現象**：B-015 以前的日常版與暫時開機均依賴靜態 ADT `fuxi,rtc-offset`（須於開機前 1 小時內執行 `rtc-calibration.py`），重包頻繁且無法反映跨天重開或 iOS 端改時／NTP 校正。
- **根因**：先前核心未直接讀取 Android `persist:/time/ats_2`，且 iOS `timed` 校時後沒有將最新偏移持久化回 iOS 檔案系統。
- **做法**：整合 K-036（方案 A 唯讀 `persist:/time/ats_2`＋方案 B 唯讀 `kern.fuxirtc_raw`/`kern.fuxirtc_info` 與 `timed` 注入）及 U-025（`FuxiRtcPersist.c` 監聽 `HOST_NOTIFY_CALENDAR_SET` 持久化至 `/private/var/db/timed/fuxi-rtc-offset.bin`），建置不含靜態 offset 的日常核心、loader 與 10 秒預設 Android 雙系統選單，並從 B-015 升級常駐 `boot_a`。
- **復現**：
  1. 在 B-015 乾淨日常核心與 harness 上分別套用 `codex/daily-rtc-ab-1122`（ksrc `6edc9fd`、harness `af4b30b`）。
  2. 本機建置乾淨日常核心：`work/localbuild/build-kernel.ps1 -Ram 9g -Tests -Name kernel-local-daily-rtc-ab-1122-9g-artifact -Harness work/localbuild/rtc-persist-1122/harness -Ath12k work/adsp-battery/ath12k-objects-b2876c6`（`dirty=0`，`kernel.development.fuxi` SHA-256 `1edd309f3e35576b00d0a60b8fed15c6b60742ac62123ee1ace27ce9a4be5c99`）。
  3. 建置日常 loader `native-daily-rtc-ab-1122-loader`（沿用 B-015 參數，將 `--rtc-calibration` 換為 `--rtc-persist --rtc-user 1`；`payload.bin` SHA-256 `6ea24bcb20921cb79ebad766e8f085d1ddc2c6764fafae2cfb3e3d232b0c04ec`、`boot.img` SHA-256 `35ccbeec4dc8ba1a981bb2842c9dfb739f798e54c55b5b7a4b03873adcec159e`）。
  4. 建置分流器與選單：`python -X utf8 -B work/boot-chain/build-chain.py chain-daily-rtc-ab-1122 --payload native-daily-rtc-ab-1122-loader --auto-launch-s 30 --ram-sku 12g`，再執行 `python -X utf8 -B dualboot/build.py --base-chain work/boot-chain/chain-daily-rtc-ab-1122 --output work/dualboot/menu-daily-rtc-ab-20261122 --default android --timeout 10`（候選 `boot.img` SHA-256 `8758a8e62417e2edff1ce23d81f4ad9c26bc59e3f7dcefb9786480ea7b234a22`）。
  5. 依序經 `launch-loader-check.ps1` 完成日常 payload 暫時開機、`-DualBootCheck android` RAM 選單驗收，並於 `dualboot/install-persistent.py`／`dualboot/phone-check.py` 登記後以 `-UpgradePersistent -InstallReport persistent-slot-a-upgrade-daily-aspect-20261120 -Out persistent-slot-a-upgrade-daily-rtc-ab-20261122` 升級 `boot_a`。
- **主機測試**：本機 WSL 通過 42 個核心 workflow 主機測試命令，ISA 2,570,399 指令／0 禁止；乾淨日常 9 單元 `sb-local-daily-rtc-ab-1122-artifact`（`4c260c42…`，`dirty=0`）與主開發樹 11 單元 `sb-local-main-rtc-1122-artifact`（`336119c1…`）均通過全部主機測試與 `verify-sb-shape.py` 驗簽；舊 B-015 核心套用 `--rtc-persist`／`--rtc-user 1` 之負測試依預期拒絕；乾淨 `main` 版 `prepare-wlan-w18-loader.py self-test` 通過；選單通過 454 項斷言、`SAVED_FDT_REPLAY_OK` 與 `BASE_RELINK_IDENTICAL`。
- **驗證紀錄**：
  - 前置分項驗收：方案 A 單獨實機（`native-hw000-rtc-persist-a-1122`，3,940 行）、A+B 自測（`native-hw000-rtc-ab-selftest-1122`，3,951 行，`rtc-b selftest PASS`）、純方案 B 從 1970 還原（`native-hw000-rtc-b-only-1122`，3,997 行）及使用者現場檢視（`native-hw000-rtc-ab-user-1122`，9,660 行，實測捕捉 `calendar_set` `-2310 ms` 落盤 `gen=4`，使用者確認沒問題）。
  - 08:59–09:04 日常 payload 暫時開機 `native-hw000-daily-rtc-ab-1122`（`boot.log` 6,463 行，SHA-256 `3d1896ef…`）：L228–230 方案 A 由 `persist:/time/ats_2` 設時 `2026-11-22 09:00:22 +08:00`；L996 `FUXI INJECT: timed`；L1175 `FUXI RTC: sysctl info first read by timed`；ADSP 529 ms、SpringBoard 360×86、BT 3.2 Mbaud `desync=0`／0 閃退、Wi-Fi `join finished 0`、`backboardd frames=536 failures=0`、0 panic。
  - 09:05–09:06 RAM 選單驗證 `daily-rtc-ab-ram-android-20261122`：10 秒倒數交接 Android `0xa9210000`，`BOOT_PARTITIONS_UNCHANGED; SLOT_UNCHANGED`。
  - 09:07–09:08 常駐 `boot_a` 升級 `persistent-slot-a-upgrade-daily-rtc-ab-20261122`：09:07:36 `BOOT_A_READBACK_OK`，09:08:44 `UPGRADED_ANDROID_VERIFIED`（槽位 `_a`），整區 `boot_a` SHA-256 `49185db810b8cc00521b6bcbc0e8d4e81eab7d592aef039d70dfe8a7d8bc6548`；保存前一版完整回退映像 `boot-a-previous-menu.img`（SHA-256 `6a6ef1acf343839504a714d0f089768df529feb833c363da4a3a2ed84b841f26`，與 B-015 整區相符），保護分區均未變動。
- **參考來源**：B-015 日常選單與升級／回退流程、K-036（方案 A＋B 核心）與 U-025（`timed` 持久化）。
- **結論／未完成**：動態 RTC（方案 A＋B）已併入日常 `boot_a` 與 `main` 工具鏈，後續建置帶 `--rtc-persist --rtc-user 1` 時不再需要每次重跑 `rtc-calibration.py`。寫入驗證涵蓋整區讀回與常駐 Android 自動交接；此次安裝後未另以實體按鍵從新選單進入 iOS。
- **出處**：`work/Codex-Xiaomi13-GPU-20261109/native-hw000-daily-rtc-ab-1122/`、`work/dualboot/menu-daily-rtc-ab-20261122/report.json`、`work/dualboot/daily-rtc-ab-ram-android-20261122/report.json`、`work/dualboot/persistent-slot-a-upgrade-daily-rtc-ab-20261122/report.json`。

### B-017 音訊／麥克風／震動／右側音量條整合日常版（合併 B-016 與 hap6f-m6 原點）

- **日期**：2026-11-22
- **狀態**：已建立乾淨主分支與子樹基準點（`checkpoint-hap6f-m6-20261122`），並完成合併日常 payload 暫時開機、雙系統選單 RAM Android 交接與 `boot_a` 常駐升級（整區讀回與 10 秒倒數回 Android 全過）。
- **開關**：完整保留 B-016 全部參數（`--rtc-persist --rtc-user 1`、`fuxi,inject=382`、`fuxi,sbshape=360086`、`fuxi,fit=3`、背光 `fuxibacklight=4`、手電筒 `fuxi,pmic=3`、Wi-Fi、原生藍牙 `bt-fix=3`＋真實 MAC、ADSP、604-byte 靜音命令列），疊加音訊／麥克風／震動／音量條參數：`--audio-tdm-live --amp 8 --audio-gain 14 --audio-max-db -8 --audio-chime 0 --audio-idle-pdn 600 --audio-mic 2 --haptic 9 --haptic-rtp-vmax 163 --haptic-damp 6 --hal-haptics-adt 6 --haptic-fast-apid --product-haptics-3p --volhud 238128 --sb-ext --capsrc-stub 7`。
- **問題／現象**：Run 81（`hap6f-m6`）實測後，喇叭出聲、關機零爆音、系統錄音、右側音量條對齊與 HAP525 震動均達到目前最佳且一致表現（使用者回饋「進步明顯、最接近原生的一次、未聽到關機爆音」），需要將其併入主分支與日常 `boot_a` 設為穩定原點，再於此基準上繼續微調震動最後些微差異。
- **根因**：既有的 `work/boot-chain/build-chain.py` 與 `dualboot/build.py` 僅連結 `loader.o`，尚未連結即時喇叭／音訊 DMA 所需的 `loader-speaker.o` 與 `loader-audio-dma.o`（`--wrap=probe_main`），因此無法直接將 K-078／K-033 合併進常駐雙系統選單。
- **做法**：先建立主分支與子樹乾淨原點 `checkpoint-hap6f-m6-20261122`，擴充 `work/boot-chain/build-chain.py` 與 `dualboot/build.py` 支援連結 `loader-speaker.o` 與 `loader-audio-dma.o`（`--wrap=probe_main`，Android 路徑不進入 `probe_main`），建置合併 B-016 與 `hap6f-m6` 的日常核心、loader 與 10 秒預設 Android 雙系統選單，並從 B-016 升級常駐 `boot_a`。
- **復現**：
  1. 依序提交並標記乾淨原點 `checkpoint-hap6f-m6-20261122`（`ksrc d425de4`、`harness 123e34e`、`main 8580e2b`）。
  2. 本機 WSL 建置乾淨 9g＋ath12k 核心 `kernel-local-daily-audio-hap6f-1122-9g-artifact`（`dirty=0`，`kernel.development.fuxi` SHA-256 `ee99b6a0b42797524adc4c02e40791ad8a76129cb9c0de41c2496a39223c644e`）；手機端 `libfuxi-sb.dylib`（`336119c1…`，與乾淨重編 11 單元 `sb-local-daily-audio-hap6f-1122-artifact` 逐位元相同）與 `FuxiAudio.driver`（`bbb2a127…`，`audio-local-hap6f-1122-artifact`）均已就位。
  3. 建置合併日常 loader `native-daily-audio-hap6f-1122-loader`（`payload.bin` SHA-256 `4533de868349d0ce79a01c4ea82fd0f113b279b491c555359038158f663231b3`、`boot.img` SHA-256 `b37d665e49c70fb9ab2d1629d02d7e11aaa096244a9982fd638f7c23cc39c2c6`）。
  4. 擴充 `work/boot-chain/build-chain.py` 與 `dualboot/build.py`，於偵測到 `loader-speaker.o`／`loader-audio-dma.o` 時以 `--wrap=probe_main` 連結並檢查 `audio_dma` 16 KiB 對齊（Android 路徑不進入 `probe_main`），建置 `chain-daily-audio-hap6f-1122` 與 `menu-daily-audio-hap6f-20261122`（`--default android --timeout 10`，候選 `boot.img` SHA-256 `885b73f29e13f1bbbeadd41ae8e2b14cf157e94c1646907115603e777b714869`）。
  5. 經 `launch-loader-check.ps1` 依序完成合併日常 payload 暫時開機、`-DualBootCheck android` RAM 選單驗證，再於 `dualboot/install-persistent.py`／`dualboot/phone-check.py` 登記後以 `-UpgradePersistent -InstallReport persistent-slot-a-upgrade-daily-rtc-ab-20261122 -Out persistent-slot-a-upgrade-daily-audio-hap6f-20261122` 升級 `boot_a`。
- **主機測試**：本機 WSL 通過 46 個核心 workflow 主機測試命令，ISA 2,580,527 指令／0 禁止；`libfuxi-sb.dylib` 通過全部主機測試與 `verify-sb-shape.py` 驗簽；雙系統選單通過 454 項斷言、`SAVED_FDT_REPLAY_OK`、`BASE_RELINK_IDENTICAL`、`linux_offset=0x1200000` 及 `__wrap_probe_main`／`audio_dma` 對齊斷言。
- **驗證紀錄**：
  - 10:15–10:20 合併日常 payload 暫時開機 `native-hw000-daily-audio-hap6f-1122`（`boot.log` 6,768 行，SHA-256 `b9d5a719…`）：動態 RTC Plan A＋B 設時 `2026-11-22 10:15:45`；CS35L41 `live on idle-pdn=600`、`chime=0`、`live standby #1..#7` 全 `bst_off=1`、`live wake #1..#6` `pup=1 ms`；麥克風 `selftest peak=7071 errs=0`；`FUXI SBEXT` 啟用；HAP525 `haptic=9`/`damp=6` 32 筆 burst 全 `underruns=0`/`tails=1`/`hpre=0`；右側音量條 `y=238 h=128`；手電筒 `kern.fuxitorch` 就緒並實測點滅（`level=1 20mA` → `0mA`）；狀態欄 `360×86`、背光 DCS `0x51`、Wi-Fi `join finished 0`（573.5 Mbps）、BT 3.2 Mbaud `desync=0`（真實 MAC）、ADSP 電量 100%、`SWEET FB: 2000 swaps`、`backboardd 2284 frames / failures=0`、0 panic。
  - 10:26–10:27 RAM 選單驗證 `daily-audio-hap6f-ram-android-20261122`：10 秒倒數交接 Android `0xa9210000`，`BOOT_PARTITIONS_UNCHANGED; SLOT_UNCHANGED`。
  - 10:28–10:29 常駐 `boot_a` 升級 `persistent-slot-a-upgrade-daily-audio-hap6f-20261122`：10:28:24 `BOOT_A_READBACK_OK`，10:29:32 `UPGRADED_ANDROID_VERIFIED`（槽位 `_a`），整區 `boot_a` SHA-256 `86a22ffe450093b527530c10d25cf36c3090786231f7f9a5525cbf9f078263f4`；保存前一版完整回退映像 `boot-a-previous-menu.img`（SHA-256 `49185db810b8cc00521b6bcbc0e8d4e81eab7d592aef039d70dfe8a7d8bc6548`，與 B-016 整區完全相符），其餘保護分區均未變動。
- **參考來源**：B-016 日常選單與升級／回退流程、K-033／U-021（`hap6f-m6` 震動與右側音量條）、K-078（CS35L41＋AudioReach TDM＋WCD9385 麥克風）。
- **結論／未完成**：音訊、錄音、右側音量條與 `hap6f-m6` 震動已連同 B-016 全功能穩定併入主分支與常駐 `boot_a` 作為後續調整原點；後續將在目標內繼續精修最後些微震動手感差距，並待使用者持續確認零關機爆音與實體按鍵進日常選單體驗。
- **出處**：`work/Codex-Xiaomi13-GPU-20261109/native-hw000-daily-audio-hap6f-1122/`、`work/dualboot/menu-daily-audio-hap6f-20261122/report.json`、`work/dualboot/daily-audio-hap6f-ram-android-20261122/report.json`、`work/dualboot/persistent-slot-a-upgrade-daily-audio-hap6f-20261122/report.json`。

### B-018 手電筒零轉子、喇叭音量／防靜音誤喚醒與 `hap8` 內核日常原點（日常維持 `damp=6`）

- **日期**：2026-11-22
- **狀態**：已建立乾淨三倉庫基準點（`checkpoint-hap8-m6-20261122`），並完成合併日常 payload 暫時開機、雙系統選單 RAM Android 交接與常駐 `boot_a` 升級（整區讀回與 10 秒倒數回 Android 全過）。
- **開關**：完整保留 B-017 全功能（`--rtc-persist --rtc-user 1`、`fuxi,inject=382`、`fuxi,sbshape=360086`、`fuxi,fit=3`、背光 `fuxibacklight=4`、手電筒 `fuxi,pmic=3`、Wi-Fi、原生藍牙 `bt-fix=3`＋真實 MAC、ADSP、右側音量條 `volhud=238128`、`sb-ext`、`capsrc-stub=7`、604-byte 靜音命令列與好評的 `--haptic-damp 6`），調整音訊參數為 `--audio-gain 16 --audio-max-db -4 --audio-chime 0 --audio-idle-pdn 1500 --audio-mic 1`；核心內建 `mode 6/7/8` 可由後續 loader 免重編切換。
- **問題／現象**：B-017 後續實測修掉了鎖定畫面手電筒長按偶發轉子震動（已寫入手機 iOS root 的未提交 `FuxiVibrate.c`）、補齊 CS35L41 防靜音誤喚醒並將長按提示音量微調至 `gain=16 / max_db=-4 / idle-pdn=1500`；另透過 `native-hw000-hap8-user-1122` 確認 `mode 8` 預設（`damp=0x8`）在中檔上滑後台會頂滿 `drv_pk=126` 削波、觸感退步於 `damp=0x6`。需要先把已驗通過的修正與手機 root 對齊收口為乾淨新原點，並讓日常 `boot_a` 維持零退步的 `damp=0x6`。
- **根因**：手機 iOS root 已裝入未提交的 `FuxiVibrate.c`（`6c5ce8c3…`），且 `ksrc`／`harness`／`main` 累積了喇叭防靜音誤喚醒與 `mode 7/8` 核心實作，先對齊收口才能安全推進後續震動微調。
- **做法**：依序提交並標記三倉庫乾淨原點 `checkpoint-hap8-m6-20261122`（`ksrc 42e6c9f`、`harness ef96d36`、`main 4805221`），以 `dirty=0` 重編 9g＋ath12k 核心與 11 單元 `libfuxi-sb.dylib`（確認與手機 root 逐位元一致，不重寫 root），打包日常 `damp=6` loader 與 10 秒預設 Android 雙系統選單，經 RAM 試開與選單交接驗收後從 B-017 升級常駐 `boot_a`。
- **復現**：
  1. 三倉原點提交與標籤 `checkpoint-hap8-m6-20261122`（`ksrc 42e6c9f`、`harness ef96d36`、`main 4805221`）。
  2. 本機 WSL 建置乾淨 9g＋ath12k 核心 `kernel-local-daily-audio-hap8-m6-1122-9g-artifact`（`dirty=0`，`kernel.development.fuxi` SHA-256 `ce941c1bbbb364e83cb605da20180ef04404838f6fa70ffc7492938bfc37125f`，與實測兩趟的 `hap8` 核心逐位元相同）與 11 單元 `sb-local-daily-audio-hap8-m6-1122-artifact`（`dirty=0`，`libfuxi-sb.dylib` SHA-256 `6c5ce8c3563b880e69de696fe6724b81d82ad423a820895388e8769fbf6c2048`，與手機 root 現行檔案逐位元相同）。
  3. 建置合併日常 loader `native-daily-audio-hap8-m6-1122-loader`（沿用 B-017 全參數，換用新核與 `--audio-gain 16 --audio-max-db -4 --audio-idle-pdn 1500 --audio-mic 1 --haptic-damp 6`；`payload.bin` SHA-256 `230fafa76a7140d70ab3b856e9fc6a2485d1629f262762e0649f848509460018`、`boot.img` SHA-256 `f1a0c75f646ac511ac07184ab722ebc56d3bc87ba61c1222f51c612c7969f304`）。
  4. 建置 `chain-daily-audio-hap8-m6-1122` 與 `menu-daily-audio-hap8-m6-20261122`（`--default android --timeout 10`，候選 `boot.img` SHA-256 `32c0b26f79db0aab4122bc2ae1dbfd267f757f60a6db1d180abed89a67e665b6`）。
  5. 經 `launch-loader-check.ps1` 依序完成日常 payload 暫時開機、`-DualBootCheck android` RAM 選單驗證，登記至 `dualboot/install-persistent.py`／`dualboot/phone-check.py` 後以 `-UpgradePersistent -InstallReport persistent-slot-a-upgrade-daily-audio-hap6f-20261122 -Out persistent-slot-a-upgrade-daily-audio-hap8-m6-20261122` 升級 `boot_a`。
- **主機測試**：本機 WSL 通過 46 個核心 workflow 主機測試命令，ISA 2,581,516 指令／0 禁止；`libfuxi-sb.dylib` 通過全部主機測試與 `verify-sb-shape.py` 驗簽；`prepare-wlan-w18-loader.py self-test` 通過；雙系統選單通過 454 項斷言、`SAVED_FDT_REPLAY_OK`、`BASE_RELINK_IDENTICAL`、`linux_offset=0x1200000` 及 `__wrap_probe_main`／`audio_dma` 16 KiB 對齊斷言。
- **驗證紀錄**：
  - 20:47–20:53 合併日常 payload 暫時開機 `native-hw000-daily-audio-hap8-m6-1122`（`boot.log` 6,415 行，SHA-256 `86706db3…`，0 panic）：HAP525 `haptic=9`/`damp=6` 32/32 burst 全 `underruns=0`/`tails=1`（上滑後台 `raw_pk=75 → drv_pk=84` 未削波，使用者現場確認「這版震動可以」）；CS35L41 `gain=16 max_db=-4 idle-pdn=1500` 7/7 `bst_off=1`；麥克風 `ready mode=1`＋實測錄音 `session=1 samples=100320 peak=18434 errs=0 drops=0`；`FUXI SBEXT` 啟用；SpringBoard／Preferences `ui-fallback-ms=0 native-pcm=1`；手電筒 `kern.fuxitorch` 就緒；動態 RTC Plan A＋B 設時 `1795351730.701`；VolHUD `y=238 h=128`；Wi-Fi `join finished 0`；BT `3.2 Mbaud desync=0`；ADSP 100%；`SWEET FB: 3000 swaps`、`FUXI GPU FIT: mode 3 frames 3000 refused 0`。
  - 20:53–20:55 RAM 選單驗證 `daily-audio-hap8-m6-ram-android-20261122`：10 秒倒數交接 Android `0xa9210000`，`BOOT_PARTITIONS_UNCHANGED; SLOT_UNCHANGED`。
  - 21:20–21:21 常駐 `boot_a` 升級 `persistent-slot-a-upgrade-daily-audio-hap8-m6-20261122`：21:20:07 `IOS_ROOT_EXTENTS_AND_PIN_OK`，21:20:13 `BOOT_A_READBACK_OK`，21:21:11 `UPGRADED_ANDROID_VERIFIED`（槽位 `_a`，整區 `boot_a` SHA-256 `51ed45320498ae11044a54d4be867a9ddb65989e00ecbe08b343ebed76ea311b`；完整 B-017 回退映像 `boot-a-previous-menu.img` SHA-256 `86a22ffe450093b527530c10d25cf36c3090786231f7f9a5525cbf9f078263f4` 已保存，保護分區未變動）。
- **參考來源**：B-017 日常選單與升級／回退流程、K-033／U-021（手電筒零轉子 `FuxiVibrate.c` 與 `mode 6/7/8` 核心）、K-078（CS35L41 喇叭音量與防靜音誤喚醒）。
- **結論／未完成**：手電筒零轉子、喇叭音量／防靜音誤喚醒與 `damp=6` 日常震動已與三倉庫原點及手機 root 完整對齊並寫入常駐 `boot_a`；後續將於此原點繼續修正 `mode 8` 中檔削波與尾段觸頂、進一步將震動手感打磨至原生品質並持續核對關機零爆音。
- **出處**：`work/Codex-Xiaomi13-GPU-20261109/native-hw000-daily-audio-hap8-m6-1122/`、`work/dualboot/menu-daily-audio-hap8-m6-20261122/report.json`、`work/dualboot/daily-audio-hap8-m6-ram-android-20261122/report.json`、`work/dualboot/persistent-slot-a-upgrade-daily-audio-hap8-m6-20261122/report.json`。

### B-019 系統去紫（U-026）、4G／VoLTE 行動數據與個人熱點（K-079 `ap7`）整合日常版

- **日期**：2026-11-23
- **狀態**：已於乾淨隔離整合工作樹建立並通過 48 項核心主機測試＋454 項雙系統選單斷言，完成全子系統 RAM 共存試開、雙系統選單 RAM Android 交接與常駐 `boot_a` 升級（全區讀回與 10 秒倒數回 Android 全過）。
- **開關**：以 `verify-daily-loader.py` 逐位元核對完整保留 B-018 及 U-026 去紫全部 44 項 ADT `/chosen` 屬性（含 `--atx-fix 1`、`--rtc-persist --rtc-user 1`、`fuxi,inject=382`、`fuxi,sbshape=360086`、`fuxi,fit=3`、背光 `fuxibacklight=4`、手電筒 `fuxi,pmic=3`、Wi-Fi、原生藍牙 `bt-fix=3`＋真實 MAC、ADSP、CS35L41 `--audio-tdm-live --amp 8 --audio-gain 16 --audio-max-db -4 --audio-chime 0 --audio-idle-pdn 1500 --audio-mic 1`、HAP525 `--haptic 9 --haptic-rtp-vmax 163 --haptic-damp 6 --hal-haptics-adt 6 --haptic-fast-apid --product-haptics-3p`、右側音量條 `volhud=238128`、`sb-ext`、`capsrc-stub=7`），精準疊加 6 項行動數據／VoLTE／熱點開關：`fuxi,modem=3`、`fuxi,modem-ipa=6131`、`fuxi,modem-bb=0x7eb (2027)`、`fuxi,modem-tftp=1`、`fuxi,audio-voice=4`、`fuxi,wifi-ap=15`（另注入 `/baseband` 節點，命令列放行 `vmd` 維持 600 bytes ≤ 607）。
- **問題／現象**：個人熱點（K-079 `ap7`）已完成 5 GHz 80 MHz Wi-Fi 6、Apple 雙圓圈熱點 IE、`hostapd` 退場同步及切換「最大化相容性」綠標自動恢復實機驗收，惟先前常駐 `boot_a`（B-018）與待升的純去紫選單（`menu-daily-atxfix-20261123`）尚未包含行動數據（`fuxi-modem`）與熱點（`fuxi,wifi-ap`），且主線 `ksrc`／`harness` 工作目錄尚有其他並行線未提交改動，須以乾淨隔離方式整合並一次納入常駐 `boot_a`。
- **根因**：行動數據／VoLTE（`0126`/`0127`/`0128`/`0129`，含 `0127` 內嵌 `/Developer/libfuxi-bb.dylib` `/dev/md1` 掛載與注入，相機開關 `fuxi,camera` 預設維持 `0` 關閉）與 K-079 SoftAP（`kernel-wifi-softap.patch`＋與日常 `b2876c6` 同源 `source_lock_sha256` 之 SoftAP 版 `fuxi_ath12k.o`）原分別隔離於 `cellcam-1120/b017` 與 `hotspot-ap-1122`。此外整合後 payload 略增使 `build-chain.py` 將嵌入式 Android 核心對齊至 `linux_offset=0x1400000`（`0xa9410000`），`dualboot/install-persistent.py` 原硬編碼比對 `0xa9210000` 須改為依 `report["candidate"]["linux_offset"]` 動態計算。
- **做法**：於 `work/localbuild/daily-hotspot-merge-1123/` 自 B-018 乾淨原點（`ksrc 42e6c9f`、`harness ef96d36`）建分支 `daily-b019-cell-hotspot-ap7`（不碰主線未提交並行改動），以獨立乾淨補丁疊加 `kernel-modem-volte.patch`（`0126`/`0127`/`0128`/`0129`）與 `kernel-wifi-softap.patch`（K-079 `ap7`）並同步留存於 `export-patches/`；連結 SoftAP 版 `fuxi_ath12k.o`（`13b0f3dd…`）建置 `dirty=0` 整合核心與日常整合 loader；建置 10 秒預設 Android 雙系統選單並更新 `dualboot/install-persistent.py`（支援依 `linux_offset` 動態核對交接位址，並支援由 B-018 或 `DAILY_ATXFIX_PIN` 升級）與 `dualboot/phone-check.py`，經全子系統 RAM 共存驗證與 RAM Android 交接後由 B-018 升級常駐 `boot_a`。
- **復現**：
  1. 隔離整合樹提交：`work/localbuild/daily-hotspot-merge-1123/ksrc`（`a6b19f3`，`dirty=0`）、`harness`（`0a3685b`，`dirty=0`），獨立補丁存於 `work/localbuild/daily-hotspot-merge-1123/export-patches/{kernel-modem-volte.patch,kernel-wifi-softap.patch}`。
  2. 本機 WSL 建置整合 9g＋ath12k 核心並跑全套測試：`work\localbuild\build-kernel.ps1 -Ram 9g -Tests -Harness work\localbuild\daily-hotspot-merge-1123\harness -Ath12k work\localbuild\hotspot-ap-1122\ath12k-ap-out -Name kernel-local-daily-cell-hotspot-ap7-1123-9g-artifact`（非 CI 產物，`kernel.development.fuxi` SHA-256 `94621465ddffe9cb5731079ebced0315ff4cd29773e20dd51d77ef48b8fc3920`）。
  3. 透過 `work/localbuild/daily-hotspot-merge-1123/prepare-daily-hotspot-loader.py`（讀自主倉已提交 `HEAD edebfb4` 之 `prepare-wlan-w18-loader.py`）建置 `native-daily-cell-hotspot-ap7-1123-loader`（`payload.bin` SHA-256 `b72c3a2737d1b288caa84355be78417b67e491555e06eff25b1c70c0864bfd33`、`boot.img` SHA-256 `9e9c0b853a8e9cac06f731a1e6ed888a34a22b482ce6e40339de22acb638e89a`，含 `loader-speaker.o` 與 `loader-audio-dma.o`），再以 `verify-daily-loader.py` 逐位元核對 44 項 B-018/U-026 屬性＋6 項新增屬性全數相符。
  4. 建置 `chain-daily-cell-hotspot-ap7-1123`（`--auto-launch-s 30`）與 `menu-daily-cell-hotspot-ap7-20261123`（`--default android --timeout 10`，候選 `boot.img` SHA-256 `fe1e9fcc6b059e05d0578c91cd3634a382fc664524f36137a4ede4ddf7da12dd`，`linux_offset=0x1400000`、`audio_dma_offset=573440`）。
  5. 經 `launch-loader-check.ps1` 依序完成整合 payload RAM 共存驗證（`native-daily-cell-hotspot-ap7-ram-1123`）、`-DualBootCheck android` RAM 選單交接（`daily-cell-hotspot-ap7-ram-android-20261123`），最後以 `-UpgradePersistent -InstallReport persistent-slot-a-upgrade-daily-audio-hap8-m6-20261122 -Out persistent-slot-a-upgrade-daily-cell-hotspot-ap7-20261123` 升級常駐 `boot_a`。
- **主機測試**：本機 WSL 通過 48 個核心工作流主機測試命令（含 `sync-fuxi-modem`／`test-fuxi-modem --sanitize`、`sync-fuxi-camera`／`test-fuxi-camera --sanitize`、`test-fuxi-live-audio-haptics`、`test-fuxi-wlan-ios`、`test-fuxi-wifi-ios`），ISA 2,641,942 指令／0 禁止；pinned SoftAP `fuxi_ath12k.o` 通過 ABI 與 source lock 核對；`verify-daily-loader.py` 通過 44＋6 屬性逐位元比對；雙系統選單通過 454 項斷言、`SAVED_FDT_REPLAY_OK`、`BASE_RELINK_IDENTICAL` 及 `audio_dma` 16 KiB 對齊斷言。
- **驗證紀錄**：
  - 11:13–11:19 整合日常 payload 純 RAM 共存試開 `native-daily-cell-hotspot-ap7-ram-1123`（`boot.log` 7,961 行，SHA-256 `e9112a63…`，`host_tsens_max=54.8°C`，0 panic）：`SpringBoard: atx-fix installed`、`CS35L41 live on gain=16 max_db=-4 idle-pdn=1500`＋2/2 `bst_off=1`、`WCD9385 ready mode=1`、`HAP525 haptic=9 vmax=0xa3 damp=0x6`、`BT baud=3200000 ibs=1 desync=0 stalls=0`（398,418 steps 零脫步）、Wi-Fi STA `join finished 0`（40 BSS 零錯）、`ap1` SoftAP 同步掛載、`libfuxi-bb (mode=0x7eb)`＋`67 QMI clients`＋`data port step 3, 25 endpoints named`＋`ims-probe err=0x0`（本趟未插 SIM 卡 `gsm.sim.state=ABSENT`，無卡待機零報錯）、動態 RTC／狀態欄 `360×86`／VolHUD `y=238 h=128`／Metal `frames=1141 failures=0`／ADSP 電量 `100%` 全數無回歸。
  - 11:22–11:24 RAM 雙系統選單驗收 `daily-cell-hotspot-ap7-ram-android-20261123`：10 秒自動倒數交接 Android `0xa9410000`（`embedded_range [0xa9400000, 0xac1e0000]`），`BOOT_PARTITIONS_UNCHANGED; SLOT_UNCHANGED`。
  - 11:26–11:27 常駐 `boot_a` 升級 `persistent-slot-a-upgrade-daily-cell-hotspot-ap7-20261123`：11:26:48 `IOS_ROOT_EXTENTS_AND_PIN_OK`、11:26:54 `BOOT_A_READBACK_OK`、11:27:59 `UPGRADED_ANDROID_VERIFIED`（槽位 `_a`，整區 `boot_a` SHA-256 `ed6b85c0e071ab89fc8cde838f68ec9d330b8d77ed84fe037a96ba166bc77600`；完整 B-018 回退映像 `boot-a-previous-menu.img` SHA-256 `51ed45320498ae11044a54d4be867a9ddb65989e00ecbe08b343ebed76ea311b` 已保存，全部保護分區零變動）。
- **參考來源**：B-018 日常原點、U-026 去紫修復、K-079 SoftAP 個人熱點（`ap7`）、`cellcam-1120/b017`（4G＋VoLTE 行動數據）。
- **結論／未完成**：去紫（U-026）、B-018 日常音訊／震動／藍牙／動態 RTC、4G＋VoLTE 行動數據與個人熱點（K-079 `ap7`）已整合並正式寫入常駐 `boot_a`；主線 `work/adsp-battery/` 因尚有其他並行線未提交改動未直接覆寫，獨立乾淨補丁已置於 `work/localbuild/daily-hotspot-merge-1123/export-patches/` 供後續統一收口；此次寫入後尚未於插入 SIM 卡狀態下由常駐選單手動切入 iOS 複驗 VoLTE 通話與熱點（此前各功能已分別實機驗收，大一統 RAM 共存已於無卡狀態完成全子系統驗收）。
- **出處**：`work/localbuild/daily-hotspot-merge-1123/`、`work/Codex-Xiaomi13-GPU-20261109/native-daily-cell-hotspot-ap7-ram-1123/`、`work/dualboot/menu-daily-cell-hotspot-ap7-20261123/report.json`、`work/dualboot/daily-cell-hotspot-ap7-ram-android-20261123/report.json`、`work/dualboot/persistent-slot-a-upgrade-daily-cell-hotspot-ap7-20261123/report.json`。

### B-020 大一統整合日常版（震動交響樂／斷音修復、錄音聽寫、原生相機、感測器／指南針、WDA 時序與關熱點 Wi-Fi 穩定性修復）

- **日期**：2026-11-23–2026-11-24（台北時間）
- **狀態**：已於乾淨隔離樹完成 `v1 → v2` 實機 60 分鐘看門狗人工驗收並回合併入主分支 `ksrc`（`7f7b2857`）、`harness`（`686f486a`）與工作區 `main`，支援 `.sensor_pack` 雙系統選單連結並通過 454 項斷言與 RAM 試開，已正式寫入常駐 `boot_a`（整區讀回與 10 秒倒數回 Android 全過，保留 B-019 完整回退映像）。
- **開關**：完整保留 B-019 全部 50 項 ADT `/chosen` 參數（含 U-026 `--atx-fix 1`、`--rtc-persist --rtc-user 1`、`fuxi,fit=3`、背光 `fuxibacklight=4`、`fuxi,pmic=3`、Wi-Fi、原生藍牙 `bt-fix=3`＋真實 MAC、ADSP、CS35L41、VolHUD `volhud=238128`、`sb-ext`、4G＋VoLTE `fuxi,modem=3`/`fuxi,modem-ipa=6131`/`fuxi,modem-bb=2027`/`fuxi,modem-tftp=1`/`fuxi,audio-voice=4`、K-079 `fuxi,wifi-ap=15`），並整合收斂：(1) `hap9e` Mode 9 v2.2 震動（`fuxi,haptic=9`、`fuxi,haptic-rtp-vmax=163`、`fuxi,haptic-damp=9`、`fuxi,hal-haptic=6`、`fuxi,haptic-ui=15`、`fuxi,haptic-amp=128`）；(2) U-027 麥克風 DSP bypass 與增益（`fuxi,audio-mic=2`、`fuxi,audio-mic-gain=3080 (0x0c08)`、`fuxi,audio-mic-cfg=1`、`fuxi,audio-mic-dsp=1`、`FuxiAudio.driver` `MIC_PRIME_FRAMES=1440`／4ch VoLTE）；(3) K-080＋U-028 原生相機（`fuxi,camera=7`、`fuxi,camera-pll=1`、`fuxi,camera-dylib=1`、`fuxi,camera-back-sensor=3`、`fuxi,camera-tab`／`-ultra`／`-tele`，並於啟用 `--camera-dylib` 時自動移除 `fuxi,capsrc-stub` 以退讓偽造相機且維持 `fuxi,inject=382`）；(4) 感測器／指南針 v8（`fuxi,sensors=255`、`fuxi,sensors-compass-mode=0`、`fuxi,sensors-mag-bias` 與 `.sensor_pack` `0xe8800000`）；(5) Adreno 500 ms inline present 與 `0109` 背光校準。不含 `fuxi,tapcmd`、`fuxi,sensors-test-rot`、`fuxi,camera-multicam`、`fuxi,camera-mmnoc` 除錯旗標；命令列維持 600 bytes（≤ 607）。
- **問題／現象**：
  1. 初版大一統整合 `B-020 v1`（`manual-b020-merge-accept-1123`）實機驗收暴露 4 項阻塞與體驗缺陷：(a) 原生相機開啟即崩潰（`Camera[237] sig 11 far 0x10` 14 次）；(b) 數據機開機早於 QRTR `WDA 0x1a` 上線時直接以 `WDA set data format error 0xffff` 放棄設定，導致行動數據無網路且無法撥號；(c) 開啟再關閉個人熱點後 Wi-Fi 極不穩定，連發 11 次 `Reason: 15=4WAY_HANDSHAKE_TIMEOUT`；(d) 鈴聲振動「交響樂」與「斷音」出現明顯爆震撞擊聲。
  2. 此外，`pack.bin`（`440,332 B`）若併入 `.gpu_fw` 會推擠 `_secondary_stack_top` 超出 AArch64 `adr` ±1 MiB 位址範圍（`1,058,644 > 1,048,575`），且雙系統連結腳本 `loader-chain.ld` 尚未包含 `.sensor_pack` 段。
- **根因**：
  1. **相機崩潰**：`prepare-b020-merge-loader.py` 沿用 `--like` 保留了 `fuxi,capsrc-stub=7`，且 `FuxiCaptureStub.c` 未判別 `fuxi,camera != 0 && fuxi,camera-dylib != 0` 而繼續勾掛 `mediaserverd`，與 `/Developer/libfuxi-camera.dylib` 衝突；另外單純關閉 `inject`（使 `fuxi,inject < 4`）會觸發內嵌相機 dylib 的自我停用門檻，必須改為維持 `fuxi,inject=382` 並由 ADT 與 `FuxiCaptureStub` 邏輯層退讓。
  2. **行動數據／WDA 競態**：`coachz_bb_data.h` 與 `FuxiBaseband.cpp` 當 `CommCenter` 先於 QRTR `0x1a`（WDA 服務）就緒時未進入 `CBD_WAIT_WDA` 等候即送出資料格式設定並遺漏服務斷線清理，導致 `WDA set data format error 0xffff`。
  3. **熱點後 Wi-Fi 不穩定**：`fuxi_wlan_arena.h` 對 `< 4096 B` DMA 配置未保證自然 2 的冪次對齊，使子頁緩衝跨 4 KiB 邊界且打破 WCN7850 128 B 對齊（`.iova_mask = 127`）並在 `nw_net.c` LIFO 池反覆重用壞緩衝；同時 `fuxi_wlan_ios.inc` 在 `SoftAP stop` 後未對主 STA 介面執行乾淨 `nw_wifi_power(0)->(1)` 回收。
  4. **交響樂／斷音爆震**：Mode 9 v2.1 在 44 ms 長驅動未對 `PROF_STD` 上移至 175.13 Hz 共振作零點早切斜坡、缺乏跨 burst 指數衰減重複機狀態機與即時位移護欄，導致連續拍點共振疊加觸底撞擊。
  5. **Loader 佈局**：`.sensor_pack` 置於 `_payload_end` 之前撐爆 `start.S` `adr` 範圍；移至 `_payload_end` 之後則可零侵擾保留 `audio_dma` 與啟動棧位址。
- **做法**：
  1. **四線並行修復 `v2`**（隔離提交於 `daily-b020-merge-1123` `ksrc 18b1af9`、`harness 6e3edb1`，隨後回併主分支 `ksrc 7f7b2857`、`harness 686f486a`）：
     - 相機退讓：`harness/userland/fuxi-shape/{FuxiCaptureStubLogic.h,FuxiCaptureStub.c}` 新增 `FX_CAPSRC_MASK_CAMERA_OWNED`，於 `fuxi,camera != 0 && fuxi,camera-dylib != 0` 時主動作廢 stub；`work/localbuild/daily-b020-merge-1123/prepare-b020-merge-loader.py` `_reconcile_capsrc_stub()` 於 `--camera-dylib` 時移除 `fuxi,capsrc-stub` 且維持 `fuxi,inject=382`。
     - 行動數據：`ksrc/xnu/fuxi-modem/{coachz_bb_data.h,FuxiBaseband.cpp}` 及 `0126-fuxi-modem.patch` 新增 `CBD_WAIT_WDA`、`CBD_WDA_NEXT`、`cbd_server_lost()`、即時 `cbq_server_find()` 與佇列滿回捲機制。
     - Wi-Fi／SoftAP：`ksrc/xnu/fuxi-wlan/{fuxi_wlan_arena.h,fuxi_wlan_ios.inc}` 及 `0099-fuxi-wlan.patch` 改採 `< 4096 B` 自然對齊（修正 `.iova_mask = 127`）、`wli_ap_start_op()` 2000 ms 有界等 `WLI_LEAVE`、首次關熱點後掃描／加入或連續 2 次 `Reason 15` 自動 `nw_wifi_power(0)->(1)` 回收主 STA，並補上 `0x107` EAPOL TX ack/nack 遙測。
     - 震動 Mode 9 v2.2：`ksrc/xnu/patches/0124-fuxi-live-audio-haptics.patch` 對照原廠 `Beethoven - Symphony No. 5.plist`／`Staccato.plist`，補強 `PROF_STD` 零點早切 `SUS`＋4 ms 斜坡、跨 burst 指數衰減狀態機與即時位移護欄（保留 24 種單次 UI 觸覺位元不變）。
  2. **Loader 與雙系統選單 `.sensor_pack` 支援**：`stage_sensor_pack()` 將 `.sensor_pack`（`pack.bin`，魔數 `b'SSCPACK1'`）置於 `_payload_end` 之後複製到 `0xe8800000`；擴充 `ios-fuxi/shim/fuxi-loader/loader-chain.ld`、`work/boot-chain/build-chain.py`、`dualboot/build.py` 納入並驗證 `.sensor_pack` 位址／8-byte 對齊／魔數與 `audio_dma` 16 KiB 對齊，再登記至 `dualboot/install-persistent.py`（`DAILY_B020_PIN`）與 `dualboot/phone-check.py`。
- **復現**：
  1. 主線倉庫合併：`work/adsp-battery/ksrc`（分支 `adsp-battery`，HEAD `7f7b28571310d232ae5568a09b576650b0b4cb7c`，`dirty=0`）與 `work/adsp-battery/harness`（分支 `codex/adsp-battery`，HEAD `686f486a18d60d1a6054d904a6a30275a3b170c6`，`dirty=0`）。
  2. 本機 WSL 建置（**`local-wsl (not a CI artifact)`**）：`work\localbuild\build-kernel.ps1 -Ram 9g -Tests -Harness work\localbuild\daily-b020-merge-1123\harness -Ath12k work\localbuild\hotspot-ap-1122\ath12k-ap-out -Name kernel-local-b020-v2-1123-9g-artifact`（產物 `kernel.development.fuxi` `19,165,928 B`，SHA-256 `2b51bf8281b76591036f08df67fcffd0721a161f202189f416a1808058776d5f`）；`work\localbuild\build-sb-shape.ps1 -Name sb-local-b020-v2-1123-artifact`（`libfuxi-sb.dylib` `212,112 B`，SHA-256 `cfbf16c55fac7437de281b635d9521d7ad6e26795b1e223c0088bc92ece53ac6`）；`FuxiAudio`（`audio-local-b020-merge-1123-artifact`，`335,600 B`，SHA-256 `e71fd15d832bf13ac8a42f7b9057b911f9400f003adf451d92c76d5269f5aa9f`）。
  3. 建置整合 loader 與雙系統選單：`work\localbuild\daily-b020-merge-1123\prepare-b020-merge-loader.py` 產出 `native-b020-v2-1123-loader`（`payload.bin` `19,229,792 B`，`539d8596931b049bbf318fd6c9e98ee8e68c2c55887fc23bb7f5dce7e01a892b`；`boot.img` `20,303,872 B`，`df68a7b54eea5f978540b920fd97c3b9ca856dfa7af956d4a86718b2ebf0569c`）；`python -X utf8 -B work\boot-chain\build-chain.py chain-daily-b020-v2-1124 --payload native-b020-v2-1123-loader --auto-launch-s 30 --ram-sku 12g`（`772fd75c…`）與 `python -X utf8 -B dualboot\build.py --base-chain work\boot-chain\chain-daily-b020-v2-1124 --output work\dualboot\menu-daily-b020-v2-20261124 --default android --timeout 10`（候選 `boot.img` `68,423,680 B`，SHA-256 `70f2b2fa0dcd3a34754c92f1a238e1b244a7e76605c8af59211f71b8c3d8946e`、`linux_offset=0x1400000`、`audio_dma_offset=573440`、`sensor_pack_offset=19934336`、`sensor_pack_size=440332`）。
  4. 實機驗收與常駐升級（經 `launch-loader-check.ps1`）：暫時開機 `manual-b020-v2-accept-1124`（`-Dylib ...sb-local-b020-v2-1123-artifact\libfuxi-sb.dylib -Seconds 3600 -Reset 60`）→ `-DualBootCheck android`（`daily-b020-v2-ram-android-20261124`）→ `-UpgradePersistent -InstallReport persistent-slot-a-upgrade-daily-cell-hotspot-ap7-20261123 -Out persistent-slot-a-upgrade-daily-b020-v2-20261124`。
- **主機測試**：
  - 核心建置：`kernel-local-b020-v2-1123-9g-artifact` `BUILD.txt` `result ok`（130 s / make 30 s，`local-wsl`）、49/49 workflow 主機測試＋3 項額外 Wi-Fi 測試全 PASS（含 `test-fuxi-modem.py --sanitize` 304,449 checks、`test-fuxi-wlan-ath12k.py --sanitize` 266,739 checks、`test-fuxi-wlan-ios.py`、`test-fuxi-wifi-ios.py`、`test-fuxi-live-audio-haptics.py --sanitize`、`test-fuxi-sensors.py`、`test-fuxi-camera.py`）、`ISA.txt` 解碼 `2,688,611`／`forbidden 0`；主倉庫 `harness` 21 份補丁於 WSL 經 `lf_copy` 驗證 100% 重現 `ksrc 7f7b2857`。
  - 使用者態：`sb-local-b020-v2-1123-artifact` 10/10 主機測試 PASS（`capsrc-stub.txt` `camera yield`）與 52 碼頁簽署＋3 項負測試 PASS；`verify_b020_v2_loader.py` 與選單 `SAVED_FDT_REPLAY_OK`、454 項斷言、`BASE_RELINK_IDENTICAL` 全過。
- **驗證紀錄**：
  - `manual-b020-merge-accept-1123`（v1，2026-11-23 21:32–22:02）：0 panic（`32,440` 行、`2a2c8e73…`），實證 4 項問題點（`WDA 0xffff`、`Camera sig 11` 14 次、`Reason: 15` 11 次、交響樂／斷音爆震）。
  - `manual-b020-v2-accept-1124`（v2，2026-11-24 07:16–07:43）：0 panic（`36,949` 行，`boot.log` SHA-256 `4d787ee675173883e8727e614f2323a5bc783e23fc825466aebc4fba7fd5295`，`host_tsens_max=78.9°C`）：
    - **相機**：`Camera`/`mediaserverd` 0 crash（`fuxi-capsrc-stub` 未插手），`ultra-ov13b10` 完成 `3,130` 幀與 `front-ov32c` `14` 幀（全程 `guard_dirty=0/262464`／`0/268544`），產出 `3024×4032 HEIF` 照片；使用者實測未閃退，回報「1x 即為廣角、1x–5x 變焦且畫質模糊」，與目前後攝僅接 `OV13B10`（`2000×1500` 定焦超廣角，未接主攝 50 MP IMX800 VCM AF/OIS）設計一致。
    - **行動數據／基帶**：`L2962 WDA set data format error 0x0`、`pdp_ip0/1/2` 掛載＋`qmap_bad=0`＋`IMS Voice registration -> true`、`FUXI MODEM: bb mode=0x7eb tx=9166(d=0) rx=18028(d=0) gsi_tx=9166/9166/1393291B`（本輪無條件撥測通話）。
    - **Wi-Fi／熱點**：`Reason: 15=0`、5 次 `recycling primary STA interface before op 2`、2 次 `handshake message 3 verified`；熱點 5 GHz 80 MHz `rx_data=2838 / tx_data=5873 / tx_drop=0 / tx_err=0`。使用者實測未提到的項目皆目測正常，並確認開啟「最大化相容性」時狀態欄綠點暫失數秒後恢復、關閉則不消失（對應 `misd` 重建 `bridge100` 與 `ap1 disassoc early STA` 喚醒 `bootpd` 機制，屬預期行為）。
    - **振動與基礎回歸**：117 次 haptic burst（`prof={3:47, 0:45, 2:22, 1:3}`、`clp=0`、`underruns=0`），交響樂／斷音爆震消除；`backboardd frames=47496 failures=0`、`FUXI GPU FIT: mode 3 frames 43200 refused 0`、`atx-fix`、`sensors rep=390167 drop=0/0`、`mag_bias saved=268,49,434`、`AUDIO MIC hpf=4Hz` 自檢 `errs=0`、英文聽寫可用（中文因缺離線包且雲端回 `-42184` 仍不可用）。非關鍵背景崩潰計有 `AegirPoster` 86 次、`Preferences` `sig 11` 1 次、`Measure` `sig 5` 1 次。
  - RAM 選單驗收 `daily-b020-v2-ram-android-20261124`（08:09–08:11）：`ANDROID_CHAIN_HANDOFF_OK 0xa9410000`、`BOOT_PARTITIONS_UNCHANGED; SLOT_UNCHANGED`。
  - 常駐 `boot_a` 升級 `persistent-slot-a-upgrade-daily-b020-v2-20261124`（08:11:56–08:13:19）：`BOOT_A_READBACK_OK`、`UPGRADED_ANDROID_VERIFIED`（槽位 `_a`，整區 `boot_a` SHA-256 `66f62c01563a78050f9557b32c3e8a3d0d07ac03f3284f03ef0dd703b0648561`；B-019 完整回退映像 `boot-a-previous-menu.img` SHA-256 `ed6b85c0e071ab89fc8cde838f68ec9d330b8d77ed84fe037a96ba166bc77600` 已保存，5 個保護分區零變動）。
- **參考來源**：B-019 整合基底、`hap9e` Mode 9 v2.1、U-027 麥克風 DSP bypass、K-080＋U-028 相機（`cam-1122`）、感測器／指南針 v8、原廠 `Beethoven - Symphony No. 5.plist`／`Staccato.plist`。
- **結論／未完成**：
  - `B-020`（基於 **`local-wsl (not a CI artifact)`** 建置）已合併主分支三倉並正式升級至常駐 `boot_a`，其餘未逐項點名項目使用者確認目測正常。
  - **後續待辦與已知邊界**：(1) 主攝 50 MP IMX800（含 VCM AF/OIS）與長焦 `S5K3K1` 尚未接入日常後攝多攝切換（目前 1x 固定為超廣角 `OV13B10` 定焦 2000×1500）；(2) 插入真實可用 SIM 條件下之 VoLTE 通話待另行補測；(3) 繁體中文聽寫缺少離線語音包且雲端離線協定受限；(4) 5g 核心與正式 CI 核心尚未對照；(5) `08:13` 升級完成當下，因 App Store `M3 Phase 3` 夜間背景探針剛執行，手機 iOS root 上現駐 App Store 測試版 `libfuxi-sb.dylib`（`686551b8…`）；`B-020` 常駐選單已透過 ADT 抑制 `fuxi,capsrc-stub` 故相機／行動數據／Wi-Fi／熱點／震動／去紫皆不受影響，僅 `U-027 FuxiAudioMicDsp` 需待 App Store 線收尾後換回 `sb-local-b020-v2-1123-artifact\libfuxi-sb.dylib`（`cfbf16c5…`）或整合進同一版 dylib 方可重啟生效。
- **出處**：`work/localbuild/daily-b020-merge-1123/`、`work/Codex-Xiaomi13-GPU-20261109/manual-b020-merge-accept-1123/`、`work/Codex-Xiaomi13-GPU-20261109/manual-b020-v2-accept-1124/`、`work/dualboot/menu-daily-b020-v2-20261124/report.json`、`work/dualboot/daily-b020-v2-ram-android-20261124/report.json`、`work/dualboot/persistent-slot-a-upgrade-daily-b020-v2-20261124/report.json`。

### B-021 雙系統日常常駐 boot_a 升級（25 補丁主分支：IMX800 四鏡頭 C-AF／閃光燈、GNSS、Wi-Fi 加速、GPU Class 0 1280 MiB／120 Hz TE、手電筒控制中心修復；暫緩 Maps）

- **日期**：2026-11-25 23:20–23:31（台北時間）
- **狀態**：已安全升級至日常常駐雙系統 `boot_a`（槽位 `_a`、`UPGRADED_ANDROID_VERIFIED`；Android 10 秒倒數、回退映像與 5 個保護分區完好；日常短測通過；Maps 依使用者指示暫緩排除於本版）
- **開關**：常駐雙系統選單 `--default android --timeout 10`（iOS `--auto-launch-s 30`）；iOS loader 由 `work/adsp-battery/prepare-aio-loader.py` 組裝，啟用 `4` 顆鏡頭 `.tab`、`fuxi,camera=7`、`fuxi,camera-back-sensor=1`、`fuxi,camera-multicam=3`、`fuxi,camera-dylib=1`、`fuxi,modem-gnss=3`、`fuxi,wifi-ux=8191`、`fuxiwlancache=1`、`fuxi,gpu-bb-recover=3`、`--gpu-clients none`（關閉 Class 1 Maps）、`--atx-fix 1`、`--statusbar-width 360 --statusbar-height 86`（`360086`）、`--display-fit aspect`、`--rtc-persist --rtc-user 1`、命令列 `602 B <= 607 B`（無 `--tap-script`、無 `fuxi,capsrc-stub`）
- **問題／現象**：2026-11-25 多線整合驗證（Baton #16–#18）顯示 IMX800 四鏡頭（0.5x/1x/3.2x 零發紫＋閃光燈＋C-AF）、GNSS `QMI_LOC`+PDS、Wi-Fi 連線加速、GPU Class 0 `1280 MiB`＋120 Hz TE 與控制中心手電筒修復皆已就緒，唯開啟 Apple Maps（Class 1 GPU）仍觸發 hang／SMMU fault 導致 `backboardd` `ERROR_DEVICE_LOST`。使用者指示先排除 Maps 修復，將其餘已驗證項目整合進主分支與日常常駐版。
- **根因**：將已驗證的 Kernel／Harness 25 補丁堆疊與 Userland 修復收斂為 `dirty=0` 乾淨主分支提交、乾淨重建核心與 `libfuxi-sb.dylib`、關閉 Class 1 GPU 客戶端（`--gpu-clients none`），並重走「安全檢查 → 實機短測 → 雙系統鏈 RAM 試開 → 常駐 `boot_a` 升級」流程。
- **做法**：
  1. **雙倉乾淨同步與重建（`dirty=0`）**：清理工作區誤建空檔並同步 10 份 `harness` 檔案與 44 份 `ksrc` 檔案；於 `work/adsp-battery/ksrc`（分支 `adsp-battery` `db86c4e6569fbace983904deb3713f4c35cdc462`）與 `work/adsp-battery/harness`（分支 `codex/adsp-battery` `541b10b0d555dc2693c08f66959ba194536fe54a`）`dirty=0` 後，於 WSL 重新編譯乾淨正式產物（**`local-wsl (not a CI artifact)`**）。
  2. **建置無 Maps 日常 loader 與乾淨部署 dylib**：以 `work/adsp-battery/prepare-aio-loader.py`（加 `--allow-shim-drift fuxi-loader/loader-chain.ld`）生成 `native-b021-daily-1125-loader`，通過 `verify-loader-safety.py ... 12g` 與 17/17 項 ADT 核對；執行短測 `run-daily-b021-smoke-1125` 同時部署乾淨 `sb-local-daily-b021-clean-artifact\libfuxi-sb.dylib`（`PRE_OK 9 / WRITE_OK 9`）並驗證全系統開機通過。
  3. **建置雙系統鏈與升級常駐 `boot_a`**：經 `work/boot-chain/build-chain.py`（`chain-daily-b021-1125`）與 `dualboot/build.py` 產生 `menu-daily-b021-20261125`（通過 `SAVED_FDT_REPLAY_OK`、`454` assertions、`BASE_RELINK_IDENTICAL`），登記 B-021 PIN 至 `dualboot/install-persistent.py` 與 `dualboot/phone-check.py`，通過 `-DualBootCheck android` 後以 `-UpgradePersistent` 自 B-020 v2 升級至 B-021。
- **復現**：
  1. 主分支提交：`ksrc` `db86c4e6569fbace983904deb3713f4c35cdc462`（`dirty=0`）、`harness` `541b10b0d555dc2693c08f66959ba194536fe54a`（`dirty=0`）。
  2. 本機 WSL 建置（**`local-wsl (not a CI artifact)`**）：`kernel-local-daily-b021-clean-9g-artifact`（`kernel.development.fuxi` SHA-256 `5d56cbec5855fb907d721cfb14ad75dfa4c32712165d14a9a093131ee0dd48fe`、`ISA decoded 2703368 forbidden 0`）；`sb-local-daily-b021-clean-artifact`（`libfuxi-sb.dylib` SHA-256 `e8cfaa01edbe17c8fc85eff6cc1c41b3c7a909df8aa5be00dce36227c6f0d641`）。
  3. Loader 與選單：`native-b021-daily-1125-loader`（`payload_sha256=4b34b1c67b8c5b078a24dcefa1d733ec29abacdd168d2fadefe07f1acc99e544`、`boot_sha256=a7e678c67959bbc907d7d70609815006de0b36ffa71697a13f6f52ea28f68eeb`）；`menu-daily-b021-20261125`（`candidate.sha256=babd50b6a48444f62a1e43beb4f33ebd387441e4c77b470b1eee69631eebd087`、`68,423,680 B`、`linux_offset=20971520`、`audio_dma_offset=573440`、`sensor_pack_offset=20113712`、`sensor_pack_size=440332`）。
  4. 常駐升級：`launch-loader-check.ps1 -UpgradePersistent -InstallReport persistent-slot-a-upgrade-daily-b020-v2-20261124 -Out persistent-slot-a-upgrade-daily-b021-20261125`。
- **主機測試**：
  - 核心建置 `kernel-local-daily-b021-1125-9g-artifact`（同樹驗證）：56/56 workflow 主機測試全 PASS、`ISA check ok (2703368 instructions, forbidden 0)`；乾淨重編 `kernel-local-daily-b021-clean-9g-artifact`（24 s / make 4 s，`local-wsl`）`ISA decoded 2703368 forbidden 0`。
  - `sb-local-daily-b021-clean-artifact`（8 s，`harness 541b10b0... dirty=0`）主機 ASan/UBSan 測試與簽署校驗 PASS。
  - `verify-loader-safety.py native-b021-daily-1125-loader 12g` PASS、ADT `17/17` checks True；雙系統選單 `SAVED_FDT_REPLAY_OK`、`454 assertions`、`BASE_RELINK_IDENTICAL` 全 PASS。
- **驗證紀錄**：
  - 日常短測 `run-daily-b021-smoke-1125`（`8,203` 行、`boot.log` SHA-256 `37bb2f93c49060dd2fef6cb5d5cf6cec514ef0157d4808e592b2583caf3b8ac5`、`host_tsens_max=51.3°C`、`PRE_OK 9 / WRITE_OK 9`、`FAST_STAGE_DONE`）：`panic=0`、`FUXI GPU: through stage 16 PASS`、`gpu_fault_lost=0`、`FUXI GPU FIT: mode 3 frames 1 refused 0`、`backboardd frames=135 failures=0`、`SWEET FB: vsync te-locked=600 fallback=0 period_us 8253`、`FUXI WIFI IOS: join finished 0`、`COACHZ gnss: mode 0x3` + `QMI_LOC`、`FUXI CAMERA: /dev/fuxi-camera standby`、`atx-fix installed`、`native FlashlightModule capability admitted`、`FUXI BT: resident ready baud=3200000 ibs=1 desync=0`、`FUXI AMP: live on gain=16 max_db=-4`、`FUXI AUDIO MIC: selftest periods=100 samples=48000 peak=3184 errs=0`、`FUXI PMIC: haptic stream v4 on (haptic=9) damp=0x9`、`FUXI RTC: persist LUN 0 ... clock set`。
  - 雙系統 RAM 試開 `daily-b021-ram-android-20261125`：`ANDROID_CHAIN_HANDOFF_OK 0xa9410000`、`BOOT_PARTITIONS_UNCHANGED; SLOT_UNCHANGED`。
  - 常駐 `boot_a` 升級 `persistent-slot-a-upgrade-daily-b021-20261125`（23:29:51–23:31:15）：`IOS_ROOT_EXTENTS_AND_PIN_OK` → `BOOT_A_READBACK_OK` → `INSTALLED_ANDROID_VERIFIED`（槽位 `_a`，`expected_full_boot_a_sha256=6b88c3e5358a79e5beda95bf97cd56d9e4260d51484898126ef36108a5822487`，B-020 v2 回退映像 `boot-a-previous-menu.img` `66f62c01563a78050f9557b32c3e8a3d0d07ac03f3284f03ef0dd703b0648561` 已保存，`boot_b`／`recovery_a`／`recovery_b`／`init_boot_a`／`vendor_boot_a` 5 保護分區 SHA-256 零變動，`lock_state=released`）。
- **參考來源**：`B-020`、`K-081`、`K-082`、`K-083`、`G-051`、`U-029`、`work/adsp-battery/prepare-aio-loader.py`。
- **結論／未完成**：
  - `B-021`（**`local-wsl (not a CI artifact)`** 建置）已整合主分支並正式升級常駐 `boot_a`，安全機制與雙系統回滾路徑完整正常。
  - **已知未納入／待續項**：(1) Apple Maps／Class 1 GPU 客戶端依使用者決策暫緩排除於本日常版（`--gpu-clients none`）；(2) AppStore 原生安裝團隊另線待命中，預計於整合收尾後接續第 10/11 棒實機驗證；(3) 地圖 Class 1 修復與 GPU 120 fps 優化列為後續長期目標；(4) 5g 核心與正式 CI 核心來源對照仍待另驗。
- **出處**：`work/adsp-battery/prepare-aio-loader.py`、`dualboot/install-persistent.py`、`dualboot/phone-check.py`、`work/Codex-Xiaomi13-GPU-20261109/run-daily-b021-smoke-1125/`、`work/dualboot/menu-daily-b021-20261125/report.json`、`work/dualboot/daily-b021-ram-android-20261125/report.json`、`work/dualboot/persistent-slot-a-upgrade-daily-b021-20261125/report.json`。

### B-022 雙系統日常常駐 boot_a 升級（Scheme A 29 補丁主分支：Wave 2 Pure 120Hz、M4-2 probe_0x4e、Option B 0131–0133/0080、*poster* 硬防護與系統 App 矩陣；Maps Class 1 預設關閉並排除隔離實驗）

- **日期**：2026-11-26（台北時間）
- **狀態**：Scheme A（29 補丁／63 主機測試）已正式合併主分支雙倉（`ksrc a00205b`、`harness 2ce4028`、`dirty=0`）並重建乾淨 9g 核心、日常 Loader（`--gpu-clients none`）與雙系統選單（`454 assertions`）；實機煙霧（`#470`）、RAM Android 交接與常駐 `boot_a` 升級：`-r2` 達成 `INSTALLED_ANDROID_VERIFIED`（槽位 `_a`、整區 `boot_a` `0e1e496b6e92b83160e8c5d7b6604589f270c6060ed5b687e64b2579922ba9d9`、`B-021` 回退映像 `6b88c3e5…` 已保存、5 個保護分區 SHA-256 零異動、`lock_state="released"`；首趟因本機 `adb` TCP 5037 daemon 瞬斷安全回退並補齊 `phone-check.py` 重試，隔離實驗 `#464`–`#469` 未併入本版，留待後續 `B-023`）
- **開關**：沿用 `B-021` 全套日常開關並整合純 Wave 2（`fuxi,gpu-locksplit=1`、`fuxi,nopoll=1`、`fuxi,gpu-irqslice=8000`）及 Option B 預設關閉開關（`fuxi,user-app-noplatform=0`、`fuxi,net-trace-filter=0`）；日常 Loader 由 `work/adsp-battery/prepare-aio-loader.py` 組裝，設定 `--gpu-clients none`（關閉 Class 1 Maps，亦未啟用 `--gpu-c1-bos`）、`--atx-fix 1`、`--display-gamut sRGB`、`--rtc-persist --rtc-user 1`、`--statusbar-width 360 --statusbar-height 86`（`360086`）、`--display-fit aspect`、`--gpu-bb-recover 3`、`--modem-gnss 3`、`--wifi-ux 8191`，無 `--tap-script` / 無 `fuxitaps`，命令列 `602 B <= 607 B`、`private.adt` `71,764 B <= 131,072 B`
- **問題／現象**：`B-021` 常駐版暫緩 Maps 與 Option B 核心基礎，需將已驗證之純 Wave 2 120Hz、M4-2 `probe_0x4e` 無斷線截圖、`stage-fuxi-metal.py` `*poster*` 硬防護、Maps Class 1 核心架構（預設維持關閉）與 Option B 核心基礎 `0131`–`0133`＋擴充 `0080` 整合進乾淨主分支雙倉與日常常駐版，並徹底清除匯出檢查發現的 `test-fuxi-net-trace.py` 私有位址與 `appstore-e2e-1123` / `verify_b022_loaders.py` 敏感字面；同時須嚴格區分未成熟之隔離實驗（Phase 11 `#464`/`#465`、Phase 12 `#466`、Phase 12B `#467`/`#468`、手動驗收 `#469` 暴露之 `M4-3 v3` `backboardd` 4096 BO 洩漏），不把實驗風險帶入日常 `boot_a`。
- **根因**：分散於多條隔離分支的 Wave 2、M4-2、Maps Class 1 與 Option B 補丁須統一 rebase 至 `B-021`（`ksrc db86c4e` / `harness 541b10b`）並同步升級主線 `prepare-aio-loader.py` 與 `ios-fuxi/shim/fuxi-probe/probe.c`；而隔離實驗線 `#464`–`#469` 涉及 `M4-3 v3` `FuxiNativeMetal` / Turnip 4096 BO 洩漏及 `fb-blend` / `c1-bos=4096` 未結案議題，故採 Scheme A 僅併入已驗證穩定之 29 補丁與 `Stick-14` 日常使用者態基準。
- **做法**：
  1. **雙倉乾淨合併（Scheme A，`dirty=0`）**：自 `work/localbuild/integ-b022-1126` 將 5 筆乾淨提交（`5a90dea` → `bb8fabc` → `e67a6ca` → `251339c` → `a00205b`，`131` 份 `.patch`）合回 `work/adsp-battery/ksrc`（`HEAD a00205b328f37c1661033425c0fd1b1f35b42c6e`，`dirty=0`）；同步更新 `work/adsp-battery/harness` 6 份異動檔（`.github/workflows/kernel-adsp-battery.yml` 與 `patches/kernel-{aks-der-noplatform,gpu120-wave2,m42-probe0x4e,metal-multiclient,net-trace-filter}.patch`）為單一乾淨提交 `2ce4028c3984e899b1858b13ce6124300e1c9f77`（`dirty=0`，43 `.patch` 追蹤／workflow 套用 29 補丁與 63 項主機測試），通過 `ksrc_check()` `differ_total=0`。
  2. **Loader 與 Probe 主線升級**：套用 `probe_0x4e` 至 `ios-fuxi/shim/fuxi-probe/probe.c`（`65,921 B`，SHA-256 `a8d64c47bcd450032f508bf6b0ce0c4053457fc40ddbd1f89e050d71379a2685`）；將 `prepare-aio-loader-b022-candidate.py` 正式升格覆寫 `work/adsp-battery/prepare-aio-loader.py`（`79,889 B`，SHA-256 `9eca8290231781a653034142076c526c57e622f99ed073d146a7aee874fdfbf8`，淘汰 `--gpu-hyb8-ovsh`、預設 `--probe-source None`、新增 `--user-app-noplatform` 與 opt-in `--gpu-c1-bos`）。
  3. **乾淨重建核心、沿用 Clean `libfuxi-sb.dylib` 與封裝日常 Loader／選單／常駐升級**：於 WSL 編譯 `kernel-local-b022-daily-9g-artifact`（**`local-wsl (not a CI artifact)`**，`seconds total=135 make=8`，`kernel.development.fuxi` SHA-256 `cd4bf32807af07ebb7862c49cf633bcad6b4ef1dea45bbaacede1f607ac1291d`）；因 `harness/userland/fuxi-shape/` 相對 `B-021` (`541b10b`) 零差異，沿用 `sb-local-daily-b021-clean-artifact`（`e8cfaa01edbe17c8fc85eff6cc1c41b3c7a909df8aa5be00dce36227c6f0d641`）並維持 `Stick-14` NativeMetal（`#469-rollback` `stage-rollback-stick14-b022-1126` 已將實機退回 `Stick-14`）；建置 `native-b022-daily-1126-loader`（`payload.bin` `43eff0a657bc6f35d2aef53282aad7f115b23961374f49615b7f6ed8479196be`、`boot.img` `00c4e7f7e0e0c61f9275a1118f9e34f9e7f9b2d648bc59396ae700cece196bdd`、`private.adt` `71,764 B`、`cmdline` `602 B`）、`chain-daily-b022-1126` 與 `menu-daily-b022-20261126`（PIN / `boot.img` SHA-256 `3f021363380fa117f933417c0afa48a09245072c8abfcc0c50e980fa20d313f4`、`68,423,680 B`），並登記至 `dualboot/install-persistent.py`（`DAILY_B022_PIN`、`DAILY_B022_PAYLOAD`、`UPGRADE_PINS[DAILY_B021_PIN] = DAILY_B022_PIN`）與 `dualboot/phone-check.py`；於 `-UpgradePersistent` 首趟寫入讀回後因本機 `adb` 5037 daemon 瞬斷安全回退，隨即在 `dualboot/phone-check.py` 補上狹義 daemon 連線重試（最多 2 次 × 1.5s）並登記 `-r2` 至 `INSTALLED_BOOT_A_REPORTS`，再以 `-r2` 完成 `boot_a` 升級驗收。
  4. **隔離未併入實驗**：明確將 `#461` (`M4-3 v3`)、Maps Phase 11 (`#464`/`#465`)、Phase 12 (`#466`)、Phase 12B (`#467`/`#468`) 與 `#469` (`run-b022-p12-user-accept-1126` 暴露 `backboardd` 4096 BO 洩漏) 排除於本日常版，留待後續 `B-023`（Phase 12D-A、Turnip BO 洩漏根治、原生 AppStore UI opt-in 藍圖）處理。
- **復現**：
  1. 主分支建置（**`local-wsl (not a CI artifact)`**）：`D:\xiaomi13\fuxi_ios\work\localbuild\build-kernel.ps1 -Ram 9g -Tests -Ath12k D:\xiaomi13\fuxi_ios\work\localbuild\hotspot-ap-1122\ath12k-ap-out -Name kernel-local-b022-daily-9g-artifact`。
  2. 日常 Loader 與雙系統選單：`python -X utf8 -B work\adsp-battery\prepare-aio-loader.py build --like native-b021-daily-1125-loader --artifact kernel-local-b022-daily-9g-artifact --name native-b022-daily-1126-loader --gpu-clients none --atx-fix 1 --display-gamut sRGB --rtc-persist --rtc-user 1`，再經 `work\boot-chain\build-chain.py chain-daily-b022-1126 --payload native-b022-daily-1126-loader --auto-launch-s 30 --ram-sku 12g` 與 `dualboot\build.py --base-chain work\boot-chain\chain-daily-b022-1126 --output work\dualboot\menu-daily-b022-20261126 --default android --timeout 10`。
  3. 實機煙霧與常駐升級：`launch-loader-check.ps1` 執行 `run-daily-b022-smoke-1126` → `-DualBootCheck android`（`daily-b022-ram-android-20261126`）→ `-UpgradePersistent -InstallReport persistent-slot-a-upgrade-daily-b021-20261125 -Out persistent-slot-a-upgrade-daily-b022-20261126`（首趟因本機 `adb` 5037 瞬斷安全回退並於 `phone-check.py` 補重試後，續以 `-Out persistent-slot-a-upgrade-daily-b022-20261126-r2` 通過）。
- **主機測試**：
  - `kernel-local-b022-daily-9g-artifact`：`BUILD.txt` 第一行 `result ok`（`local-wsl (not a CI artifact)`、`seconds total=135 make=8`）、`ISA.txt` `decoded 2706839 forbidden 0`、`logs\tests.txt` **`63/63 PASS`**（非 PASS = 0），且 `kernel.development.fuxi` 與候選建置 `kernel-local-b022-cand-1126-9g-artifact` 逐位元一致（`cd4bf328...`）。
  - `native-b022-daily-1126-loader`：`verify-loader-safety.py native-b022-daily-1126-loader 12g` 通過（17/17 checks）、`cmdline 602 B <= 607 B`、0 個 Wave 1 禁用字樣、0 個 `R_AARCH64_ABS`、`ud_setup.latched_base` 存在、無 `tour-shots.txt`。
  - `menu-daily-b022-20261126`：`SAVED_FDT_REPLAY_OK 815061 bytes`、`PASS 454 assertions`、`BASE_RELINK_IDENTICAL`。
  - `appstore-e2e-1123` 三項離線測試全數 PASS（`107/107`、`224/224`、`RUNNER_SELF_TEST_OK`），零 `.pyc`；Stage E-3 寫入 `BOOT_A_B022_SHA256` 後複測擴增至 `108/108`、`224/224`、`RUNNER_SELF_TEST_OK` 與 `test-remove-ksu.py` `6/6 OK`。
- **驗證紀錄**：
  - **合併前候選核心驗證（`#452`–`#463`，已併入 Scheme A）**：`#452 B-022 P1`（`WebKit.GPU admit(Class1)`×3、`WebKit.GPU` `0 GPU fault`〔切至 `Music` 退背景後 `CRASH com.apple.WebKit[319] sig 9 / reason ns 1 code 0x7` 終止並於 `L11600` 重啟就緒〕、`Music`/`Weather`/`Stocks` 0 crash〔不含背景既有系統／extension 項目〕、`SWEET AMFI XML + DER`）、`#453 B0-Folder`（`語音備忘錄`/`指南針`/`Measure` 定址、`目標互動／SpringBoard 0 crash`〔不含背景既有系統／extension 項目〕、`reconnects=0`）、`#454` & `#456` 冷機 `120Hz` 對照定論（電池 `33.4°C` 起 / `33.8°C` 滑前 → `35.0°C`、`p95=1.283 ms`、`p99=1.354 ms`、`blit avg 681–693 us`、`FAULT=0`/`dropped=0`）、`#457 Measure (P2-AR)`（`admit(Class1)`×3、Undo/Trash 顯示、`Measure` `CRASH=0`〔不含背景既有系統／extension 項目〕/`FAULT=0`）、`#458 P1v2`（`Stocks`/`Weather`/`weatherd`/`MobileTimer` 全 `CRASH=0`〔不含背景既有系統／extension 項目〕/`FAULT=0`，`MobileTimer` `2 小時 27 分鐘 40 秒` 零紫屏），以及 Option B `B-1` 合規 `0xB0` 雙端實機通過＋安全卸載閉環（`stage-b1-nodrm-slot7der-b0-1126` → `#462` 冷開 → `#463` `run-b022-nodrm-slot7der-b0-r2-1126` 徹底消除 `Code=33 (EDOM)` 讀回 4 項 Entitlements、`FAULT_OR_LOST=0`、`1080×1200` 零紫屏 → `stage-b1-remove-slot7der-b0-1126` 還原 `176->175`）。
  - **合併前隔離實驗對照（未併入 `B-022`，留待 `B-023`）**：`#461` (`M4-3 v3`)、Phase 11 `#464`/`#465` (`c1-bos=4096`)、Phase 12 `#466` (`fb-blend`)、Phase 12B `#467`/`#468` (`fbplanes`) 與 `#469`（`run-b022-p12-user-accept-1126` 15 分鐘手動驗收暴露 `M4-3 v3` `backboardd` 4096 BO 洩漏並經 `stage-rollback-stick14-b022-1126` 安全回退至 `Stick-14`）。
  - **Stage E-2 日常煙霧實測（`#470`，`2026-11-26 19:04–19:07`）**：`9,062` 行（`LF=9,059`, `boot.log cfe2698e024b…`）：`panic=0`、`REAL_FAULTS=0`、`DEV_LOST=0`、`BoAlloc slots refused=0`、`backboardd`/`SpringBoard` `CRASH=0`（不含背景既有系統／extension 項目）、`dropped=0`、`mtl_failures_last=0`、`thermal_cuts=0`；`FUXI GPU: through stage 16 PASS`、`SWEET FB: vsync te-locked=600 fallback=0 period_us 8251`（`te_hz_est=121.2 Hz`, `p95=4.754 ms`, `gpu_ms=3.041`, `max_temp_dc=521`, `host_tsens_max=50.9°C`）、`atx-fix` 已安裝；`ath12k` stage 4（11 scans, 15 BSS, error 0, 5200 MHz join finished 0）、BT `desync=0`、ADSP `100%` (`4419 mV`)、RTC `persist-ats_2`、`FlashlightModule`（immediate dispatch + coalescing）、GNSS `QMI_LOC` (`err=0`, `sv=17`)、`CS35L41`/`AUDIO MIC` (`errs=0`)/`HAP525` (`haptic=9`, `underruns=0`) 全數正常（本趟無獨立 `fuxilog/` 目錄）。
  - **Stage E-2 雙系統 RAM Android 交接（`daily-b022-ram-android-20261126`）**：`result="ok"`, `ANDROID_CHAIN_HANDOFF_OK 0xa9410000` (`embedded_range=["0xa9400000","0xac1e0000"]`), `candidate_sha256=3f021363…`, `boot_partition_writes=0`, `slot_changes=0`（`protected_sha256.boot_a = 6b88c3e5…` 未動）。
  - **Stage E-2 常駐 `boot_a` 升級（`2026-11-26 19:13–19:21`）**：整區 `boot_a` SHA-256 `0e1e496b6e92b83160e8c5d7b6604589f270c6060ed5b687e64b2579922ba9d9`；首趟 `persistent-slot-a-upgrade-daily-b022-20261126`（`19:13–19:16`）寫入讀回後於 `install-persistent.py:346` 遇本機 `adb` 5037 daemon 瞬斷安全回退 `B-021`（`ROLLED_BACK`, `6b88c3e5…`, `lock_state="released"`）；`dualboot/phone-check.py` 補上狹義 daemon 重試並登記 `-r2` 後，第二趟 `persistent-slot-a-upgrade-daily-b022-20261126-r2`（`19:20–19:21`）達成 `INSTALLED_ANDROID_VERIFIED`（槽位 `_a`、整區 `boot_a` `0e1e496b…`、`B-021` 回退映像 `6b88c3e5…` 已保存、`boot_b`/`recovery_a`/`recovery_b`/`init_boot_a`/`vendor_boot_a` 5 保護分區 SHA-256 零異動、`lock_state="released"`）。
- **參考來源**：`B-021`、`K-084`、`G-052`、`U-030`、`T-061`、`work/adsp-battery/prepare-aio-loader.py`。
- **結論／未完成**：
  - Scheme A `B-022`（基於 **`local-wsl (not a CI artifact)`** 建置）已完成雙倉乾淨合併（`ksrc a00205b`、`harness 2ce4028`）、日常 Loader 與雙系統選單建置及 Stage E-2 常駐 `boot_a` 升級（整區 `boot_a` `0e1e496b6e92b83160e8c5d7b6604589f270c6060ed5b687e64b2579922ba9d9`），維持 `Stick-14` 與 `--gpu-clients none` 確保日常穩定性。
  - **已知未納入／後續交 `B-023`**：(1) `#464`–`#469` 隔離實驗（Maps Phase 11/12/12B、`M4-3 v3` `backboardd` 4096 BO 洩漏與 Phase 12D-A Turnip BO 洩漏根治）明確未入本版，交由 `B-023` 跟進；(2) 原生 AppStore UI opt-in 與 Option B B2 沙盒實作留待後續迭代；(3) 5g 核心與正式 CI 核心來源對照仍待另驗。
- **出處**：`work/adsp-battery/ksrc` (`a00205b`)、`work/adsp-battery/harness` (`2ce4028`)、`ios-fuxi/shim/fuxi-probe/probe.c`、`work/adsp-battery/prepare-aio-loader.py`、`dualboot/install-persistent.py`、`dualboot/phone-check.py`、`work/Codex-Xiaomi13-GPU-20261109/kernel-local-b022-daily-9g-artifact/`、`work/Codex-Xiaomi13-GPU-20261109/sb-local-daily-b021-clean-artifact/`、`work/Codex-Xiaomi13-GPU-20261109/native-b022-daily-1126-loader/`、`work/Codex-Xiaomi13-GPU-20261109/run-daily-b022-smoke-1126/`、`work/dualboot/menu-daily-b022-20261126/report.json`、`work/dualboot/daily-b022-ram-android-20261126/report.json`、`work/dualboot/persistent-slot-a-upgrade-daily-b022-20261126/report.json`、`work/dualboot/persistent-slot-a-upgrade-daily-b022-20261126-r2/report.json`、`work/statusbar-fix/stage-rollback-stick14-b022-1126/`。
