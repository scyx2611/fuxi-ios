# T 系列：工具、建置與流程

> T-001～T-047 是 2026-11-07～08 早期開機用的工具（工作區根目錄與 `scripts/`），由 2026-11-18 的逐項盤點整合；T-048 起是 000 線（2026-11-14 起）的現行工具。

## 早期開機工具（2026-11-07～08）

### T-001 工具 `scripts/fuxi_env.py`

- **日期**：2026-11-08（推估，來源未記確切時分）
- **狀態**：工具：供多數 `scripts/` 與根目錄 Python 腳本匯入使用
- **開關**：`FUXI_WORKSPACE`、`FUXI_DEVICE_SERIAL`、`FUXI_NDK_BIN`、`FUXI_ADB`、`FUXI_FASTBOOT`、`fuxi_env`
- **問題／現象**：提供跨平台路徑、Android SDK 工具、NDK LLVM、libusb 與單一連線裝置序號之自動解析函式庫。
- **根因**：避免將本地絕對路徑或裝置序號寫死在各腳本中。
- **做法**：提供 `get_workspace`、`find_platform_tool`、`find_ndk_bin`、`find_libusb_dll`、`resolve_adb_serial`、`resolve_fastboot_serial` 等函式；支援環境變數 `FUXI_WORKSPACE`、`FUXI_DEVICE_SERIAL`、`ANDROID_SERIAL`、`ANDROID_NDK_HOME`、`FUXI_NDK_BIN`、`FUXI_ADB`、`FUXI_FASTBOOT`。
- **復現**：由其他腳本 `from fuxi_env import ...` 匯入使用。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：無獨立 CLI 參數。
- **參考來源**：原始紀錄未列
- **結論／未完成**：**仍在使用中**（被 `setup_upstream.py`、`build_diag_loader.py`、`build_fuxi_loader_win.py`、`stream_ios16_candidate.py`、`verify_ios16_candidate.py` 等大量腳本匯入）。
- **出處**：`scripts/fuxi_env.py:L1-L60`; `README.md:L99-L109,L183`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-002 工具 `scripts/setup_upstream.py`

- **日期**：2026-11-08（推估）
- **狀態**：工具：寫進 `README.md` Step 1 作為初始建置入口
- **開關**：無
- **問題／現象**：自動 clone 上游 `bangprovn/ios-fuxi`（鎖定 commit `27f6a31`）、套用 `patches/ios-fuxi-fuxi13.patch`、下載 `mkbootimg.py` 並檢查本機環境與 `firmware/` 雜湊。
- **根因**：標準化工作區初始建置流程。
- **做法**：支援 `--check-only`、強制重套補丁等選項（詳見檔內 `argparse`）。預設目標目錄為 `ios-fuxi/`，讀取 `patches/ios-fuxi-fuxi13.patch` 與 `firmware/`。
- **復現**：  ```sh
  python scripts/setup_upstream.py
  python scripts/setup_upstream.py --check-only
  ```
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：檢查 `adb`、`fastboot`、NDK Clang、Python 套件與 3 份 GPU 韌體 SHA-256。
- **參考來源**：ios-fuxi（出現在本項內文，細節見出處）
- **結論／未完成**：**仍在使用中**（列於 `README.md` Step 1 作為環境準備標準入口）。
- **出處**：`scripts/setup_upstream.py:L1-L60`; `README.md:L122-L129,L183`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-003 工具 `scripts/build_diag_loader.py`

- **日期**：2026-11-08
- **狀態**：工具：產出 `fuxi-diag-ram.img` 或 `fuxi-ios16-root.img`（Track 1 仍引用）
- **開關**：`rd=md0`
- **問題／現象**：在不覆寫既有開機映像的前提下，編譯 `ios-fuxi/shim/fuxi-loader` 並嵌入 GPU 韌體與 payload 產出隔離的 iOS 16 開機映像。
- **根因**：支援純 RAM 診斷開機（`rd=md0`）與 extent-root 正式開機兩種隔離輸出，並強制檢查韌體 SHA-256 與開機參數安全性。
- **做法**：參數：`--production`（切換正式版 extent-root 檢查與預設輸出）、`--payload <path>`、`--out <dir>`。預設基準目錄 `candidates/kernel-metal4-zgl-42244366810/`，預設 payload `diag-payload.bin` 或 `ios16-production-payload.bin`，預設輸出 `diag-loader/fuxi-diag-ram.img` 或 `ios16-root-loader/fuxi-ios16-root.img`。
- **復現**：  ```sh
  python scripts/build_diag_loader.py
  python scripts/build_diag_loader.py --production
  ```
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：驗證 3 份 GPU 韌體 SHA-256（`gmu_gen70200.bin`、`a740_sqe_sheng.fw`、`a740_zap_fuxi.mbn`）並拒絕不安全之儲存開機參數混用。
- **參考來源**：ios-fuxi（出現在本項內文，細節見出處）
- **結論／未完成**：**仍在使用中**（列於 `README.md` Track 1 Step 2）。
- **出處**：`scripts/build_diag_loader.py:L1-L60`; `README.md:L140-L144,L184`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-004 工具 `scripts/build_fuxi_loader_win.py`

- **日期**：2026-11-08（推估）
- **狀態**：工具：編譯 `fuxi-loader` 並與 `payload27-dada.bin` 打包成 `fuxi-xnu-boot.img`
- **開關**：無
- **問題／現象**：在 Windows 原生環境下使用 Android NDK Clang/LLD/Objcopy 建置 `fuxi-loader` 並打包 `fuxi-xnu-boot.img`。
- **根因**：無須依賴 macOS 即可於 Windows 重包 Track 2（`xnu-12377`）開機映像。
- **做法**：無命令列參數。預設讀取 `payload27-dada.bin`、`firmware/{gmu_gen70200.bin,a740_sqe_sheng.fw,a740_zap_fuxi.mbn}`、`ios-fuxi/shim/fuxi-loader`，中間輸出至 `out/fuxi-loader-logo/`，最終產出工作區根目錄 `fuxi-xnu-boot.img`。
- **復現**：  ```sh
  python scripts/build_fuxi_loader_win.py
  ```
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：檢查 `SWEETXNU` magic header 與工具存在性。
- **參考來源**：ios-fuxi（出現在本項內文，細節見出處）
- **結論／未完成**：**仍在使用中**（列於 `README.md` Track 2 Step 3）。
- **出處**：`scripts/build_fuxi_loader_win.py:L1-L60`; `README.md:L167-L170,L184`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-005 工具 `scripts/build_ios16_root_guest.sh`

- **日期**：2026-11-08
- **狀態**：工具：從只讀 `vda/vdb/vdc` 建置 `vdd` 20 GiB iOS 16 root 映像
- **開關**：無
- **問題／現象**：在隔離的 QEMU Alpine Linux guest 內將只讀 OS／SystemOS／AppOS APFS 映像組合成 20 GiB 可讀寫 HFS+ RootFS。
- **根因**：避免直接將手機實體區塊裝置暴露給 VM，同時在 Linux 下完成 HFS+ 格式化與硬連結／權限配置。
- **做法**：無命令列參數。檢查 `/dev/vda`、`/dev/vdb`、`/dev/vdc` 為唯讀、`/dev/vdd` 為可讀寫 20 GiB（41,943,040 sectors）且前 4 KiB 符合全零 SHA-256；以 `apfs-fuse` 掛載並 `rsync -aH --numeric-ids` 複製後執行 `/tmp/fuxi-stage-root.py` 與 `/tmp/sweet-root-tweaks.py`，最後卸載並執行 `fsck.hfsplus -fn /dev/vdd`。
- **復現**：於隔離 QEMU guest 內以 root 執行 `sh build_ios16_root_guest.sh`。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：比對兩份 Cryptex 卷內容零差異並輸出 `BUILD_IOS16_ROOT_SUCCESS`。
- **參考來源**：Linux（出現在本項內文，細節見出處）
- **結論／未完成**：**仍在使用中**（列於 `README.md` Track 1 Step 2）。
- **出處**：`scripts/build_ios16_root_guest.sh:L1-L48`; `README.md:L134,L185`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-006 工具 `scripts/prepare_ios16_perf_payload.py`

- **日期**：2026-11-08
- **狀態**：工具：由 `ios16-production-payload.bin` 附加 `fuxiperf=1 fuxicpuload=10`
- **開關**：`fuxiperf=1`、`fuxicpuload=10`
- **問題／現象**：從已驗證的 `ios16-production-payload.bin` 衍生帶效能參數 `fuxiperf=1 fuxicpuload=10` 之 A/B 測試 payload。
- **根因**：測試拉高 CPU 頻率對 CPU 渲染效能之影響。
- **做法**：無命令列參數。預設讀取 `candidates/kernel-metal4-zgl-42244366810/ios16-production-payload.bin`，檢查 v1 payload 與 608 位元組 cmdline 上限後，以獨佔寫入（`xb`）輸出 `candidates/kernel-metal4-zgl-42244366810/ios16-perf-payload.bin`。
- **復現**：  ```sh
  python scripts/prepare_ios16_perf_payload.py
  ```
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：印出新 payload SHA-256 與完整 cmdline。
- **參考來源**：原始紀錄未列
- **結論／未完成**：一次性／實驗性工具（不確定後續是否仍用，已於 GPU 主線演進後被較新 loader 產生器取代）。
- **出處**：`scripts/prepare_ios16_perf_payload.py:L1-L25`; `CONTINUATION_20261108.md:L51`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-007 工具 `scripts/stream_ios16_candidate.py`

- **日期**：2026-11-08
- **狀態**：工具：經 ADB 轉發寫入 `write-pinned-extents` 並記錄寫入日誌
- **開關**：無
- **問題／現象**：經 ADB TCP 轉發（port `29865`）將 20 GiB HFS+ 映像串流寫入 Recovery 下的裝置端 `/tmp/write-pinned-extents`。
- **根因**：繞過 Windows `adb` `stdin` 將 `0x1a` 視為 `EOF` 的問題，確保 20 GiB 完整無誤寫入實體 extent。
- **做法**：參數：`--serial`、`--image`（預設 `candidates/vm-tools/ios16-root-build.img`）、`--expected-sha256`、`--device-map`（預設 `/tmp/ios16-root.map`）、`--writer-bin`（預設 `/tmp/write-pinned-extents`）、`--block-bytes`、`--log`（預設 `diagnostics/ios16-candidate-write.log`）。強制檢查裝置處於 `recovery` 狀態且映像大小精確為 `21474836480` B。
- **復現**：  ```sh
  adb forward tcp:29865 tcp:29865
  python scripts/stream_ios16_candidate.py --image <PATH_TO_20G_IMG> --block-bytes <SDA32_BYTES>
  ```
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：記錄於 `diagnostics/ios16-candidate-write-v4.log`；注意 `ios16-root-build.img` 已於 2026-11-18 歸檔至 `E:\fuxi_Backup-archive\workspace-20261118\`，使用須傳 `--image`。
- **參考來源**：原始紀錄未列
- **結論／未完成**：**仍在使用中**（列於 `README.md` Track 1 Step 2）。
- **出處**：`scripts/stream_ios16_candidate.py:L1-L60`; `README.md:L149,L186`; `D:\xiaomi13\fuxi_Backup\README.md:L43`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-008 工具 `scripts/verify_ios16_candidate.py`

- **日期**：2026-11-08
- **狀態**：工具：寫入後逐 extent `dd` 讀回比對並輸出 `ios16-candidate-readback.json`
- **開關**：無
- **問題／現象**：在 `userdata` 未掛載狀態下，從 `/dev/block/sda32` 逐 extent `dd` 讀回並與主機端 20 GiB 映像逐位元組比對。
- **根因**：確保寫入實體 UFS 磁區之資料 100% 正確無損。
- **做法**：參數：`--serial`、`--image`（預設 `candidates/vm-tools/ios16-root-build.img`）、`--table`（預設 `diagnostics/ios16-extents.json`）、`--block-dev`（預設 `/dev/block/sda32`）、`--expected-sha256`、`--report`（預設 `diagnostics/ios16-candidate-readback.json`）。執行前檢查 `/proc/mounts` 確認 `/data` 與 `/sdcard` 未掛載並執行 `blockdev --flushbufs`。
- **復現**：  ```sh
  python scripts/verify_ios16_candidate.py --image <PATH_TO_20G_IMG> --table <EXTENTS_JSON>
  ```
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：輸出 `diagnostics/ios16-candidate-readback.json`。
- **參考來源**：原始紀錄未列
- **結論／未完成**：**仍在使用中**（列於 `README.md` Track 1 Step 2）。
- **出處**：`scripts/verify_ios16_candidate.py:L1-L60`; `README.md:L150,L186`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-009 工具 `scripts/read_root27_backup.py`

- **日期**：2026-11-08
- **狀態**：工具：依 `f2fs_io-codex` extent 表讀回 8 GB 映像至 `candidates/recovered-root27/`
- **開關**：無
- **問題／現象**：在 Recovery 模式下透過 `/tmp/f2fs_io-codex` 讀取手機 `/data/ios27-root.img` 之實體 extent 並只讀備份回主機。
- **根因**：保存 Track 2 的 8 GB 根映像而不寫入手機。
- **做法**：無命令列參數。預設讀取 `/data/ios27-root.img` 與 `root_report.json`，輸出至 `candidates/recovered-root27/`（`extents-before.json`、`ios27-root.img`）。
- **復現**：手機進 Recovery 且放置 `/tmp/f2fs_io-codex` 後執行 `python scripts/read_root27_backup.py`。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：成功備份至 `candidates/recovered-root27/`（現已歸檔於 `E:\fuxi_Backup-archive\ios27-root-recovered-20261108\`）。
- **參考來源**：原始紀錄未列
- **結論／未完成**：歷史一次性備份工具（任務已完成，不確定是否仍需使用）。
- **出處**：`scripts/read_root27_backup.py:L1-L60`; `CONTINUATION_20261108.md:L30`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-010 工具 `scripts/capture_probe_framebuffer.py`

- **日期**：2026-11-08
- **狀態**：工具：不重開機自 USB `0x4e` 讀取 `1080x2400` BGRA 並存成 PNG
- **開關**：無
- **問題／現象**：在不重開機、不重新觸發 XNU handoff 的情況下，自 USB 探針（`VID 0x1209, PID 0x0001`）控制傳輸 `0x4e` 讀取即時 Framebuffer。
- **根因**：`fuxi-xnu-monitor.py` 單純重連時應避免重新觸發初始化；獨立腳本可於 `stage == 8` 隨時擷取畫面。
- **做法**：選填位置參數 `sys.argv[1]` 為輸出目錄（預設 `diagnostics/ios16-first-boot`），輸出 `framebuffer.bgra` 與 `framebuffer.png`（`1080x2400`）。
- **復現**：  ```sh
  python scripts/capture_probe_framebuffer.py diagnostics/my-snapshot
  ```
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：實測每次 512-byte 讀取全螢幕約需 16 秒。
- **參考來源**：原始紀錄未列
- **結論／未完成**：**仍在使用中**（列於 `README.md` Track 1 Step 2）。
- **出處**：`scripts/capture_probe_framebuffer.py:L1-L30`; `README.md:L161`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-011 工具 `scripts/inspect_device.py`

- **日期**：2026-11-08（推估）
- **狀態**：工具：印出 `monitor_info` 11 欄位與 `kernel_stage`
- **開關**：無
- **問題／現象**：只讀查詢 USB 探針（`0x1209:0x0001`）之 `monitor_info`（`0x46`，88 B）與早期核心階段（`0x41`，512 B）。
- **根因**：快速確認探針 `launch`、`stage`、`active`、`deadline` 等狀態而不動手機。
- **做法**：無命令列參數，使用 `fuxi_env.find_libusb_dll` 或 `libusb_package`。
- **復現**：`python scripts/inspect_device.py`
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：輸出 `magic`、`delegate`、`active`、`usb_mpidr`、`launch`、`stage`、`boot_args`、`entry`、`deadline`、`image`、`end` 與 `kernel_stage`。
- **參考來源**：原始紀錄未列
- **結論／未完成**：診斷小工具（不確定日常是否仍使用，現多由 `launch-loader-check.ps1` 取代）。
- **出處**：`scripts/inspect_device.py:L1-L43`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-012 工具 `scripts/stream_host.py`

- **日期**：2026-11-07/08
- **狀態**：工具：早期 iOS 27 root 映像串流工具，已被 Track 1 之 `stream_ios16_candidate.py` 取代
- **開關**：無
- **問題／現象**：經本機 TCP port `9999` 將 `resources/ios27-root.img` 依 `resources/extents27.json` 串流傳送至裝置端接收器。
- **根因**：對應 Boot 4–5 解法，避開 Android 檔案系統內聯加密寫入 Track 2 root 映像。
- **做法**：無命令列參數。預設讀取 `resources/extents27.json` 與 `resources/ios27-root.img`，以 1 MB 分塊傳送至 `127.0.0.1:9999`。
- **復現**：`python scripts/stream_host.py`
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：用於 Boot 4–5 成功寫入 `ios27-root.img`。
- **參考來源**：原始紀錄未列
- **結論／未完成**：歷史工具（Track 1 改用 `stream_ios16_candidate.py`）。
- **出處**：`scripts/stream_host.py:L1-L50`; `boot_failure_and_fix_log.md:L14`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-013 工具 `scripts/patch_kernel_guard.py`

- **日期**：2026-11-07/08
- **狀態**：工具：早期 `xnu-12377` 靜態二進位補丁腳本（歷史工具）
- **開關**：無
- **問題／現象**：針對 `resources/k2-dada/kernel.prelinked` 套用 Boot 13 Mach 埠安全策略二進位補丁（`_ool_port_array_enforced`、Policy 7/4、`_mach_port_guard_ast`）。
- **根因**：見 E-012。
- **做法**：無參數，先備份 `.orig`，原地修改 4 處位元組並更新 `resources/k2-dada/kernel.json`。
- **復現**：`python scripts/patch_kernel_guard.py`
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：見 E-012。
- **參考來源**：原始紀錄未列
- **結論／未完成**：歷史補丁腳本（`README.md` 標記為 `Historical & active xnu-12377 kernel binary patch scripts`）。
- **出處**：`scripts/patch_kernel_guard.py:L1-L50`; `README.md:L187`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-014 工具 `scripts/patch_kernel_guard_ast.py`

- **日期**：2026-11-07/08
- **狀態**：工具：早期 `xnu-12377` 靜態二進位補丁腳本（歷史工具）
- **開關**：無
- **問題／現象**：將 `resources/k2-dada/kernel.prelinked` 偏移 `0x3c80edc`（`_PROC_VIOLATED_GUARD__SEND_EXC_GUARD`）改為 `retab`（`0xd65f0fff`）並更新 `kernel.json` 的 `sha256` 與 `candidate_sha256`。
- **根因**：見 E-023。
- **做法**：無參數，直接原地修改並覆寫 `kernel.json`。
- **復現**：`python scripts/patch_kernel_guard_ast.py`
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：見 E-023。
- **參考來源**：原始紀錄未列
- **結論／未完成**：歷史補丁腳本（`xnu-12377` Track 2）。
- **出處**：`scripts/patch_kernel_guard_ast.py:L1-L47`; `README.md:L187`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-015 工具 `scripts/patch_kernel_inheritance.py`

- **日期**：2026-11-07/08
- **狀態**：工具：早期 `xnu-12377` 靜態二進位補丁腳本（歷史工具）
- **開關**：無
- **問題／現象**：套用 Boot 15 繼承埠註冊與 MIG demux 繞過補丁至 `resources/k2-dada/kernel.prelinked`。
- **根因**：見 E-013、E-014。
- **做法**：無參數，備份為 `.boot14_orig` 並修改 8 處位元組。
- **復現**：`python scripts/patch_kernel_inheritance.py`
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：見 E-013、E-014。
- **參考來源**：原始紀錄未列
- **結論／未完成**：歷史補丁腳本（部分已被 `patch_kernel_register.py` 取代）。
- **出處**：`scripts/patch_kernel_inheritance.py:L1-L50`; `README.md:L187`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-016 工具 `scripts/patch_kernel_lookup.py`

- **日期**：2026-11-07/08
- **狀態**：工具：早期 `xnu-12377` 靜態二進位補丁腳本（歷史工具）
- **開關**：無
- **問題／現象**：重寫 `_X_kernelrpc_mach_ports_lookup3`（`0x3dad368`）及跳過 `kfree_zone` 檢查（`0x3ce38e4`）。
- **根因**：見 E-016、E-017。
- **做法**：無參數，備份為 `.boot17_orig`，使用外部組譯器編譯 ARM64 機器碼寫入 `resources/k2-dada/kernel.prelinked` 並更新 `kernel.json`。
- **復現**：`python scripts/patch_kernel_lookup.py`
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：見 E-016、E-017。
- **參考來源**：原始紀錄未列
- **結論／未完成**：歷史補丁腳本（`xnu-12377` Track 2）。
- **出處**：`scripts/patch_kernel_lookup.py:L1-L50`; `README.md:L187`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-017 工具 `scripts/patch_kernel_register.py`

- **日期**：2026-11-07/08
- **狀態**：工具：早期 `xnu-12377` 靜態二進位補丁腳本（歷史工具）
- **開關**：無
- **問題／現象**：重寫 `__X_kernelrpc_mach_ports_register3`（`0x3dad204`，356 B）以支援 `0 <= count <= 3` 動態 OOL 埠陣列解包。
- **根因**：見 E-018、E-019。
- **做法**：無參數，備份為 `.boot19_orig`，原地寫入 `resources/k2-dada/kernel.prelinked` 並更新 `kernel.json`。
- **復現**：`python scripts/patch_kernel_register.py`
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：見 E-018、E-019。
- **參考來源**：原始紀錄未列
- **結論／未完成**：歷史補丁腳本（`xnu-12377` Track 2）。
- **出處**：`scripts/patch_kernel_register.py:L1-L50`; `README.md:L187`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-018 工具 `scripts/patch_kernel_boot17.py`

- **日期**：2026-11-07/08
- **狀態**：工具：早期 `xnu-12377` 靜態二進位補丁腳本（歷史工具）
- **開關**：無
- **問題／現象**：為所有進程開啟 `postsig_locked` 使用者態回溯列印，並讓 `_setsid`、`_spawn_validate_persona`、`_spawn_persona_adopt` 回傳 0。
- **根因**：見 E-015。
- **做法**：無參數，備份為 `.boot16_orig`，原地修改 `resources/k2-dada/kernel.prelinked` 4 處並更新 `kernel.json`。
- **復現**：`python scripts/patch_kernel_boot17.py`
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：見 E-015。
- **參考來源**：原始紀錄未列
- **結論／未完成**：歷史補丁腳本（`xnu-12377` Track 2）。
- **出處**：`scripts/patch_kernel_boot17.py:L1-L60`; `README.md:L187`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-019 工具 `scripts/patch_kernel_trampoline.py`

- **日期**：2026-11-07/08
- **狀態**：工具：早期 `xnu-12377` 靜態二進位補丁腳本（歷史工具）
- **開關**：無
- **問題／現象**：Hook `lookup_check_for_resolve_prefix`（`0x3f5d4e0`，428 B）將 `/usr/lib/libobjc-trampolines.dylib` 與 `libffi-trampolines.dylib` 重導向至 SystemOS Cryptex。
- **根因**：見 E-020。
- **做法**：無參數，備份為 `.boot21_orig`，原地修改 `resources/k2-dada/kernel.prelinked` 並更新 `kernel.json`。
- **復現**：`python scripts/patch_kernel_trampoline.py`
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：見 E-020。
- **參考來源**：原始紀錄未列
- **結論／未完成**：歷史補丁腳本（後續整併進 `patch_kernel_boot24.py`～`boot28.py`）。
- **出處**：`scripts/patch_kernel_trampoline.py:L1-L50`; `README.md:L187`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-020 工具 `scripts/patch_kernel_boot24.py`

- **日期**：2026-11-07/08
- **狀態**：工具：早期 `xnu-12377` 靜態二進位補丁腳本（歷史工具）
- **開關**：無
- **問題／現象**：於 code cave（`0xfffffff00b62d880`／`0x4629880`）寫入 `_hook_lookup_prefix`、`_hook_vn_stat`、`_hook_postsig_exit` 並關閉 `_PROC_VIOLATED_GUARD__SEND_EXC_GUARD`。
- **根因**：見 E-023。
- **做法**：無參數，以 `.boot23_orig` 為基底並依賴 `capstone` 反組譯確認，修改 `resources/k2-dada/kernel.prelinked`。
- **復現**：`python scripts/patch_kernel_boot24.py`
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：見 E-023。
- **參考來源**：原始紀錄未列
- **結論／未完成**：歷史補丁腳本，已被後續 Boot 25–28 腳本取代。
- **出處**：`scripts/patch_kernel_boot24.py:L1-L60`; `README.md:L187`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-021 工具 `scripts/patch_kernel_boot25.py`

- **日期**：2026-11-07/08
- **狀態**：工具：早期 `xnu-12377` 靜態二進位補丁腳本（歷史工具）
- **開關**：無
- **問題／現象**：新增 `_hook_namei`（`0xfffffff00af60018`）截斷 `.migrated`/`timesync`/`uuidtext` 並保留 `_hook_vn_stat` 為 `logd` 偽裝屬性。
- **根因**：見 E-024。
- **做法**：無參數，修改 `resources/k2-dada/kernel.prelinked`。
- **復現**：`python scripts/patch_kernel_boot25.py`
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：見 E-024。
- **參考來源**：原始紀錄未列
- **結論／未完成**：歷史補丁腳本，已被 Boot 26–28 腳本取代。
- **出處**：`scripts/patch_kernel_boot25.py:L1-L60`; `README.md:L187`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-022 工具 `scripts/patch_kernel_boot26.py`

- **日期**：2026-11-07/08
- **狀態**：工具：早期 `xnu-12377` 靜態二進位補丁腳本（歷史工具）
- **開關**：無
- **問題／現象**：新增 `_hook_exit_with_reason`（`0xfffffff00b238664`）攔截 `backboardd`/`logd` 執行緒退出。
- **根因**：見 E-025。
- **做法**：無參數，修改 `resources/k2-dada/kernel.prelinked`。
- **復現**：`python scripts/patch_kernel_boot26.py`
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：見 E-025（含誤呼叫 `proc_unlock` 之缺陷）。
- **參考來源**：原始紀錄未列
- **結論／未完成**：歷史補丁腳本，已被 `patch_kernel_boot27.py`/`28.py` 取代。
- **出處**：`scripts/patch_kernel_boot26.py:L1-L60`; `README.md:L187`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-023 工具 `scripts/patch_kernel_boot27.py`

- **日期**：2026-11-07/08
- **狀態**：工具：含 `far: 0x100` 錯誤之歷史補丁腳本，已被 `patch_kernel_boot28.py` 取代
- **開關**：無
- **問題／現象**：移除 `L_intercept_thread` 內之 `proc_unlock`（改無鎖呼叫 `_thread_terminate_self`），並重構 `_hook_lookup_prefix` 至 cave `0xfffffff00b62da00`。
- **根因**：見 E-025、E-026。
- **做法**：無參數，修改 `resources/k2-dada/kernel.prelinked`。
- **復現**：`python scripts/patch_kernel_boot27.py`
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：見 E-026（因誤解參考 `x1` 導致 `far: 0x100` panic）。
- **參考來源**：原始紀錄未列
- **結論／未完成**：已停用，由 `patch_kernel_boot28.py` 修復。
- **出處**：`scripts/patch_kernel_boot27.py:L1-L60`; `README.md:L187`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-024 工具 `scripts/patch_kernel_boot28.py`

- **日期**：2026-11-07/08
- **狀態**：工具：早期 `xnu-12377` 靜態二進位補丁腳本（歷史工具）
- **開關**：無
- **問題／現象**：保留 Boot 27 無鎖執行緒退出攔截，並還原正確的 `_hook_lookup_prefix` 實作。
- **根因**：見 E-026。
- **做法**：無參數，修改 `resources/k2-dada/kernel.prelinked`。
- **復現**：`python scripts/patch_kernel_boot28.py`
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：見 E-027（Boot 28 300 秒零 panic）。
- **參考來源**：原始紀錄未列
- **結論／未完成**：Track 2（`xnu-12377`）目前最新一版核心二進位補丁腳本。
- **出處**：`scripts/patch_kernel_boot28.py:L1-L50`; `README.md:L187`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-025 工具 `scripts/patch_loader_c.py`

- **日期**：2026-11-07/08
- **狀態**：工具：已由 `remove_fake_logo.py` 撤銷之歷史腳本
- **開關**：無
- **問題／現象**：於 `ios-fuxi/shim/fuxi-loader/loader.c` 插入 `draw_apple_logo()` 呼叫。
- **根因**：早期開機畫面美化嘗試（後因使用者要求移除假 logo 而撤銷）。
- **做法**：無參數，直接修改 `ios-fuxi/shim/fuxi-loader/loader.c`。
- **復現**：`python scripts/patch_loader_c.py`
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：已被 `remove_fake_logo.py` 清除。
- **參考來源**：ios-fuxi（出現在本項內文，細節見出處）
- **結論／未完成**：已廢棄（歷史腳本）。
- **出處**：`scripts/patch_loader_c.py:L1-L20`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-026 工具 `scripts/update_loader_flawless.py`

- **日期**：2026-11-07/08
- **狀態**：工具：已由 `remove_fake_logo.py` 撤銷之歷史腳本
- **開關**：無
- **問題／現象**：將 `loader.c` 內的 `apple_logo_data.h` 替換為 `apple_boot_asset.h`（320x320 假 Apple Logo＋進度條）。
- **根因**：早期假 logo 繪製迭代（後依使用者指示移除）。
- **做法**：無參數，直接修改 `ios-fuxi/shim/fuxi-loader/loader.c`。
- **復現**：`python scripts/update_loader_flawless.py`
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：已被 `remove_fake_logo.py` 清除。
- **參考來源**：ios-fuxi（出現在本項內文，細節見出處）
- **結論／未完成**：已廢棄（歷史腳本）。
- **出處**：`scripts/update_loader_flawless.py:L1-L50`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-027 工具 `scripts/remove_fake_logo.py`

- **日期**：2026-11-08
- **狀態**：工具：執行後清除 `loader.c` 內 `draw_apple_logo` 與 `apple_boot_asset.h`
- **開關**：無
- **問題／現象**：從 `ios-fuxi/shim/fuxi-loader/loader.c` 徹底移除 `apple_boot_asset.h`、`draw_apple_logo` 函式及其呼叫，改回 `zero64(FB_BASE, ...)`。
- **根因**：依 2026-11-08 使用者明確要求移除自繪假 Apple Logo。
- **做法**：無參數，直接修改 `ios-fuxi/shim/fuxi-loader/loader.c`。
- **復現**：`python scripts/remove_fake_logo.py`
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：`CONTINUATION_20261108.md` 確認 `loader.c` 已無 `draw_apple_logo`、`apple_boot_asset`、`apple_logo`。
- **參考來源**：ios-fuxi（出現在本項內文，細節見出處）
- **結論／未完成**：已完成任務之歷史工具。
- **出處**：`scripts/remove_fake_logo.py:L1-L33`; `CONTINUATION_20261108.md:L8`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-028 工具 `scripts/update_log.py`

- **日期**：2026-11-07/08
- **狀態**：工具：一次性文件更新腳本（含舊使用者路徑），已停用
- **開關**：無
- **問題／現象**：早期於 `<使用者目錄>\.gemini\antigravity\brain\...` 之間自動替換 `boot_failure_and_fix_log.md` Boot 20–21 段落之一次性腳本。
- **根因**：早期工程紀錄整理。
- **做法**：無參數，寫死舊本機路徑。
- **復現**：已不適用。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：來源未記。
- **參考來源**：原始紀錄未列
- **結論／未完成**：已廢棄之一次性腳本。
- **出處**：`scripts/update_log.py:L1-L60`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-029 工具 `scripts/do_boot.py`

- **日期**：2026-11-07/08
- **狀態**：工具：早期 `xnu-12377` 開機腳本（已被 `do_boot29_win.py` 等取代）
- **開關**：無
- **問題／現象**：執行 `fastboot boot` 並以 `fuxi-xnu-monitor.py` 進行 300 秒監看（含 `--reset-minutes 30 --framebuffer --touch-power`）。
- **根因**：用於 Boot 28 實機驗證。
- **做法**：無參數；預設 `BOOT_IMG=ios-fuxi/shim/out/fuxi-loader/fuxi-xnu-boot.img`、`KERNEL=resources/k2-dada/kernel.prelinked`、`PAYLOAD=resources/payload27-dada.bin`、`OUT_DIR=resources/boot28-logs`。
- **復現**：`python scripts/do_boot.py`
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：用於 Boot 28 300 秒測試。
- **參考來源**：ios-fuxi（出現在本項內文，細節見出處）
- **結論／未完成**：歷史開機腳本（被 `do_boot29.py`／`do_boot29_win.py` 取代）。
- **出處**：`scripts/do_boot.py:L1-L50`; `boot_failure_and_fix_log.md:L341,L349`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-030 工具 `scripts/do_boot29.py`

- **日期**：2026-11-08
- **狀態**：工具：早期 `xnu-12377` 開機腳本
- **開關**：無
- **問題／現象**：執行 Boot 29 `fastboot boot` 與 300 秒監看（輸出至 `resources/boot29-logs`）。
- **根因**：與 `do_boot.py` 相同，僅輸出目錄改為 `resources/boot29-logs`。
- **做法**：無參數，預設路徑同 `do_boot.py`。
- **復現**：`python scripts/do_boot29.py`
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：見 E-028。
- **參考來源**：原始紀錄未列
- **結論／未完成**：歷史開機腳本（Windows 版改用 `do_boot29_win.py`）。
- **出處**：`scripts/do_boot29.py:L1-L50`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-031 工具 `scripts/do_boot29_win.py`

- **日期**：2026-11-08
- **狀態**：工具：被 `boot_ios_monitor.bat` 呼叫
- **開關**：無
- **問題／現象**：Windows 原生版 Boot 29 執行器：檢查 Fastboot 裝置、執行 `fastboot boot fuxi-xnu-boot.img` 並啟動 `fuxi-xnu-monitor.py`。
- **根因**：配合 macOS → Windows 交接包裹之目錄結構。
- **做法**：無參數；預設 `BOOT_IMG=fuxi-xnu-boot.img`、`KERNEL=kernel/fuxi-kernel-ios27/mach.development.fuxi.unstripped`、`PAYLOAD=payload27-dada.bin`、`OUT_DIR=boot29-live-logs`。
- **復現**：由根目錄 `boot_ios_monitor.bat` 呼叫，或執行 `python scripts/do_boot29_win.py`。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：見 `boot_ios_monitor.bat`。
- **參考來源**：原始紀錄未列
- **結論／未完成**：仍被 `boot_ios_monitor.bat` 引用（Track 2 交接工具）。
- **出處**：`scripts/do_boot29_win.py:L1-L60`; `boot_ios_monitor.bat:L21-L32`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-032 工具 `scripts/do_boot30_win.py`

- **日期**：2026-11-08
- **狀態**：工具：早期 `xnu-12377` Windows 開機測試腳本（不確定是否仍在用）
- **開關**：無
- **問題／現象**：Windows 版 Boot 30 調度器，持續輪詢等待 Fastboot 裝置後執行 `fastboot boot fuxi-xnu-boot.img` 並監看。
- **根因**：支援從 console 畫面手動長按重開進 Fastboot 後自動接續開機。
- **做法**：無參數；預設 `KERNEL=resources/k2-dada/kernel.prelinked`、`PAYLOAD=payload27-dada.bin`、`OUT_DIR=boot30-live-logs`。
- **復現**：`python scripts/do_boot30_win.py`
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：來源未記。
- **參考來源**：原始紀錄未列
- **結論／未完成**：不確定是否仍在使用（無其他腳本或 `AGENTS.md` 引用）。
- **出處**：`scripts/do_boot30_win.py:L1-L60`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-033 工具 `scripts/auto_stage.py`

- **日期**：2026-11-07/08
- **狀態**：工具：早期自動化輪詢腳本（不確定是否仍在用）
- **開關**：`FUXI_DEVICE_SERIAL`
- **問題／現象**：早期自動偵測 ADB／Fastboot 狀態並觸發後續開機步驟之監看腳本。
- **根因**：自動化裝置重啟與階段切換。
- **做法**：無命令列參數，支援 `FUXI_DEVICE_SERIAL` 環境變數。
- **復現**：`python scripts/auto_stage.py`
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：來源未記。
- **參考來源**：原始紀錄未列
- **結論／未完成**：不確定是否仍在使用（無其他腳本或 `AGENTS.md` 引用）。
- **出處**：`scripts/auto_stage.py:L1-L50`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-034 工具 `scripts/wait_for_fastboot_and_boot.py`

- **日期**：2026-11-07/08
- **狀態**：工具：早期 Boot 26 專用調度腳本
- **開關**：無
- **問題／現象**：在 180 秒內等待裝置進入 Fastboot（或 ADB），偵測到後自動呼叫 `scripts/do_boot.py` 執行 Boot 26。
- **根因**：自動化 Boot 26 連續測試。
- **做法**：無參數，呼叫 `scripts/do_boot.py`。
- **復現**：`python scripts/wait_for_fastboot_and_boot.py`
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：見 E-025。
- **參考來源**：原始紀錄未列
- **結論／未完成**：歷史腳本（Boot 26 專用）。
- **出處**：`scripts/wait_for_fastboot_and_boot.py:L1-L50`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-035 工具 `scripts/watch_and_boot27.py`

- **日期**：2026-11-07/08
- **狀態**：工具：早期 Boot 27 專用調度腳本
- **開關**：無
- **問題／現象**：監看裝置重開進 Fastboot／ADB 後觸發 Boot 27。
- **根因**：自動化 Boot 27 連續測試。
- **做法**：無參數，輪詢 `adb devices` 與 `fastboot devices`。
- **復現**：`python scripts/watch_and_boot27.py`
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：見 E-026。
- **參考來源**：原始紀錄未列
- **結論／未完成**：歷史腳本（Boot 27 專用）。
- **出處**：`scripts/watch_and_boot27.py:L1-L50`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-036 工具 `scripts/watch_and_boot28.py`

- **日期**：2026-11-07/08
- **狀態**：工具：早期 Boot 28 專用調度腳本
- **開關**：無
- **問題／現象**：監看裝置重開進 Fastboot／ADB 後觸發 Boot 28。
- **根因**：自動化 Boot 28 連續測試。
- **做法**：無參數，輪詢 `adb devices` 與 `fastboot devices`。
- **復現**：`python scripts/watch_and_boot28.py`
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：見 E-027。
- **參考來源**：原始紀錄未列
- **結論／未完成**：歷史腳本（Boot 28 專用）。
- **出處**：`scripts/watch_and_boot28.py:L1-L50`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-037 工具 `scripts/watch_and_boot29.py`

- **日期**：2026-11-08
- **狀態**：工具：早期 Boot 29 專用調度腳本
- **開關**：無
- **問題／現象**：監看裝置重開進 Fastboot 後觸發 Boot 29。
- **根因**：自動化 Boot 29 驗證。
- **做法**：無參數，輪詢 `fastboot devices`。
- **復現**：`python scripts/watch_and_boot29.py`
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：見 `boot_failure_and_fix_log.md` Boot 29。
- **參考來源**：原始紀錄未列
- **結論／未完成**：歷史腳本（Boot 29 專用）。
- **出處**：`scripts/watch_and_boot29.py:L1-L50`; `boot_failure_and_fix_log.md:L458`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-038 工具 `scripts/boot_and_drive_userland.py`

- **日期**：2026-11-07/08
- **狀態**：工具：早期 `xnu-12377` 使用者態圖形驅動測試腳本
- **開關**：無
- **問題／現象**：自動執行 `fastboot boot`、開啟觸控電源軌（`0x55`）、進行原生 XNU handoff（`0x45 -> 0x46 -> 0x49`）、讀取 ring buffer 日誌並偵測／擷取 framebuffer swap。
- **根因**：不依賴外部 `fuxi-xnu-monitor.py` 獨立驅動與觀察 `xnu-12377` 使用者態圖形。
- **做法**：無參數；預設 `BOOT_IMG=fuxi-xnu-boot.img`、`KERNEL_UNSTRIPPED=kernel/fuxi-kernel-ios27/mach.development.fuxi.chained.unstripped`、`PAYLOAD=payload27-applelogo.bin`、`OUT_DIR=userland-drive-logs/`。
- **復現**：`python scripts/boot_and_drive_userland.py`
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：曾產出 `userland-drive-logs/live_stream.log`（記錄 `SpringBoard`/`backboardd` 進入 `running` 及 `mediaserverd` `0x8` 重複退出）。
- **參考來源**：原始紀錄未列
- **結論／未完成**：歷史工具（曾產出 `CONTINUATION_20261108.md` 所引用之 `live_stream.log`）。
- **出處**：`scripts/boot_and_drive_userland.py:L1-L60`; `CONTINUATION_20261108.md:L7`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-039 工具 `scripts/boot_and_verify_logo.py`

- **日期**：2026-11-07/08
- **狀態**：工具：已過時（假 Apple Logo 已移除）
- **開關**：無
- **問題／現象**：等待 Fastboot、`fastboot boot fuxi-xnu-boot.img`、延長 watchdog +30m、dump `0xb8000000` framebuffer 並以 `numpy`/`PIL` 驗證假 Apple Logo 是否正常顯示且無文字洗版。
- **根因**：自動化驗收早期假 logo 補丁。
- **做法**：無參數；輸出至 `out/verify-artifacts/`。
- **復現**：`python scripts/boot_and_verify_logo.py`
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：假 Apple Logo 已於 11-08 移除。
- **參考來源**：原始紀錄未列
- **結論／未完成**：已廢棄。
- **出處**：`scripts/boot_and_verify_logo.py:L1-L50`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-040 工具 `scripts/boot_clean_verify.py`

- **日期**：2026-11-08
- **狀態**：工具：早期驗證腳本（不確定是否仍在用）
- **開關**：無
- **問題／現象**：純 RAM 開機並驗證假 Apple Logo 與進度條已 100% 移除、擷取真實 framebuffer 狀態。
- **根因**：確認 `remove_fake_logo.py` 成效。
- **做法**：無參數；預設 `BOOT_IMG=fuxi-xnu-boot.img`、輸出至 `out/verify-artifacts/`。
- **復現**：`python scripts/boot_clean_verify.py`
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：來源未記。
- **參考來源**：原始紀錄未列
- **結論／未完成**：不確定是否仍在使用（無其他腳本或 `AGENTS.md` 引用）。
- **出處**：`scripts/boot_clean_verify.py:L1-L50`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-041 工具 根目錄 `boot_ios.ps1`／`boot_ios.bat`

- **日期**：2026-11-08
- **狀態**：工具：Track 2 交接快速開機腳本，載入 `README.md`
- **開關**：無
- **問題／現象**：Windows 一鍵開機腳本：若無 `fuxi-xnu-boot.img` 則自動解壓 `fuxi-xnu-boot.img.gz`，檢查 Fastboot 連線與 `product`，執行純 RAM `fastboot boot fuxi-xnu-boot.img`。
- **根因**：讓 Windows 使用者雙擊即可啟動 Track 2 交接映像。
- **做法**：無參數，自動偵測 `%LOCALAPPDATA%\Android\Sdk\platform-tools`（僅限 Track 2 `fuxi-xnu-boot.img`）。
- **復現**：雙擊 `boot_ios.bat` 或執行 `.\boot_ios.ps1`。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：列於 `HANDOVER.md`、`README_WINDOWS.md`、`README.md`。
- **參考來源**：原始紀錄未列
- **結論／未完成**：Track 2 交接入口工具（日常實機驗證另已改用 `launch-loader-check.ps1`）。
- **出處**：`boot_ios.ps1:L1-L58`; `boot_ios.bat:L1-L60`; `README.md:L172,L188`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-042 工具 根目錄 `2_boot_ios.bat`／`boot_ios_monitor.bat`／`reboot_to_fastboot.bat`

- **日期**：2026-11-08
- **狀態**：工具：Track 2 一鍵開機＋監看／重開進 Fastboot 批次檔
- **開關**：無
- **問題／現象**：Windows 一鍵開機＋監看批次檔與 ADB 重開進 Fastboot 輔助檔。
- **根因**：方便 Windows 使用者免打指令完成重啟至 Fastboot 並啟動監看。
- **做法**：  - `2_boot_ios.bat`：執行 `python -u run_complete_ios_boot.py`；
  - `boot_ios_monitor.bat`：執行 `scripts\do_boot29_win.py`；
  - `reboot_to_fastboot.bat`：執行 `adb reboot bootloader` 並等待 5 秒確認 `fastboot devices`。
- **復現**：雙擊對應 `.bat` 檔。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：列於 `HANDOVER.md`、`README_WINDOWS.md`、`README.md`。
- **參考來源**：原始紀錄未列
- **結論／未完成**：Track 2 交接工具（`README.md` 仍列為 Track 2 Step 3 選項）。
- **出處**：`2_boot_ios.bat:L1-L11`; `boot_ios_monitor.bat:L1-L43`; `reboot_to_fastboot.bat:L1-L32`; `README.md:L173`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-043 工具 根目錄 `auto_boot_orchestrator.py`／`run_complete_ios_boot.py`／`wait_for_adb_and_boot.py`

- **日期**：2026-11-08
- **狀態**：工具：Track 2 Windows 全自動開機調度腳本
- **開關**：無
- **問題／現象**：Windows 全自動開機調度腳本（檢查 WinUSB `VID_1209&PID_0001` 登錄檔、等待 ADB／Fastboot、`fastboot boot fuxi-xnu-boot.img`、呼叫 `fuxi-xnu-monitor.py`）。
- **根因**：整合 Android → Fastboot → XNU → USB 監看之單一 Python 入口。
- **做法**：無命令列參數；皆使用 `fuxi_env.py` 解析工具與序號，預設 `BOOT_IMG=fuxi-xnu-boot.img`、`PAYLOAD=payload27-dada.bin`、`OUT_DIR=boot29-live-logs`。
- **復現**：`python run_complete_ios_boot.py`（由 `2_boot_ios.bat` 呼叫）。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：列於 `README.md` 與 `README_WINDOWS.md`。
- **參考來源**：原始紀錄未列
- **結論／未完成**：Track 2 交接工具（`run_complete_ios_boot.py` 被 `2_boot_ios.bat` 與 `README.md` 引用；另兩支不確定是否仍用）。
- **出處**：`auto_boot_orchestrator.py:L1-L60`; `run_complete_ios_boot.py:L1-L60`; `wait_for_adb_and_boot.py:L1-L60`; `README.md:L173`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-044 工具 根目錄 `fuxi_monitor.py`

- **日期**：2026-11-08
- **狀態**：工具：輕量讀取器（完整日誌串流改用 `ios-fuxi/tools/fuxi-xnu-monitor.py`）
- **開關**：無
- **問題／現象**：Windows 輕量級獨立 USB 探針（`VID 0x1209, PID 0x0001`）讀取器與 Framebuffer 擷取器。
- **根因**：提供不依賴 `ios-fuxi` 上游目錄的輕量測試工具（完整日誌串流與次核心交接須使用 `ios-fuxi/tools/fuxi-xnu-monitor.py`）。
- **做法**：參數：`--out`（預設 `boot_logs`）、`--seconds`（預設 `300`）、`--reset-minutes`（預設 `30`）、`--framebuffer`（預設 `True`）。
- **復現**：`python fuxi_monitor.py --out boot_logs --seconds 300`
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：列於 `HANDOVER.md` 與 `README_WINDOWS.md`。
- **參考來源**：ios-fuxi（出現在本項內文，細節見出處）
- **結論／未完成**：交接輕量工具（完整測試已改用 `ios-fuxi/tools/fuxi-xnu-monitor.py`）。
- **出處**：`fuxi_monitor.py:L1-L60`; `HANDOVER.md:L54`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-045 工具 根目錄 `build-iosroot-mac.sh`／`finish-iosroot-mac.sh`

- **日期**：2026-11-08
- **狀態**：工具：macOS `hdiutil`＋`rsync` 建置 16 GB HFS+ RootFS 腳本
- **開關**：`FUXI_WORKSPACE`
- **問題／現象**：在 macOS 主機（須 `sudo`）使用 `hdiutil` 與 `rsync` 將 3 份官方 DMG（`087-86698-044.dmg`、`087-86593-047.dmg`、`087-86715-047.dmg`）組合成 16 GB Case-sensitive Journaled HFS+ `resources/ios16-root.img`。
- **根因**：提供 macOS 原生替代方案（相對於 Linux/Windows QEMU 方案 `build_ios16_root_guest.sh`）。
- **做法**：環境變數 `FUXI_WORKSPACE`、`DMG_DIR`（預設 `resources/dmgs`）、`OUT_IMG`（預設 `resources/ios16-root.img`）、`TMP_DMG`（預設 `resources/iOSRoot_raw.dmg`）；`finish-iosroot-mac.sh` 接續複製 App Cryptex 並執行 `ios-fuxi/tools/fuxi-stage-root.py`。
- **復現**：  ```sh
  sudo ./build-iosroot-mac.sh
  ```
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：列於 `README.md` Track 1 Step 2 macOS 替代方案。
- **參考來源**：ios-fuxi、Linux（出現在本項內文，細節見出處）
- **結論／未完成**：**仍可用於 macOS 建置**（`README.md` 明確收錄）。
- **出處**：`build-iosroot-mac.sh:L1-L60`; `finish-iosroot-mac.sh:L1-L60`; `README.md:L135,L185`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-046 工具 根目錄 `fuxi_probe.cfg`／`fuxi_winusb.inf`／`zadig.ini`

- **日期**：2026-11-08
- **狀態**：工具：為 `VID 0x1209 / PID 0x0001` 探針裝置安裝 WinUSB 驅動
- **開關**：無
- **問題／現象**：為 Windows 主機上的「Fuxi hardware probe」（`USB\VID_1209&PID_0001`）安裝 WinUSB 驅動程式。
- **根因**：PyUSB／libusb 在 Windows 上存取 USB 探針控制傳輸需要 WinUSB 或 libusbK 驅動。
- **做法**：`fuxi_probe.cfg`（Zadig 裝置設定）、`fuxi_winusb.inf`（Windows INF，DeviceInterfaceGUID `{dee824ef-729b-4a0e-9c14-b7117d33a817}`）、`zadig.ini`（Zadig 工具設定；`zadig.exe` 本體已於 2026-11-18 歸檔至 E:）。
- **復現**：搭配 `1_install_winusb_driver.bat` 或 Zadig 載入 `fuxi_probe.cfg`／右鍵安裝 `fuxi_winusb.inf`。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：見 `README.md` Prerequisites。
- **參考來源**：原始紀錄未列
- **結論／未完成**：**仍在使用中**（列於 `README.md` Step 3 與 Prerequisites）。
- **出處**：`fuxi_probe.cfg:L1-L5`; `fuxi_winusb.inf:L1-L31`; `zadig.ini:L1-L17`; `README.md:L82,L166,L189`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### T-047 工具 `work/ksu-ios-entry/build-module.py`、`action.sh`、`customize.sh`

- **日期**：2026-11-14～2026-11-16
- **狀態**：工具：打包與執行 KSU 模組 `fuxi_ios_boot`（已於 2026-11-18 停用並封存）
- **開關**：`fuxi_ios_boot`
- **問題／現象**：打包與執行 KernelSU 模組 `fuxi_ios_boot`，透過 `boot_b` 分流器實作免電腦重開進 iOS。
- **根因**：見 E-036。
- **做法**：  - `build-module.py <分流器目錄名>`：讀取 `work\boot-chain\<名>\` 之 `boot.img`、`sha256.txt`、`build.txt` 與對應 payload 內 ADT `fuxi-root-extents`，透過 `adb -s <序號>` 執行手機端 `/data/local/tmp/fuxi-inspect-fiemap` 比對 `/data/gsi/codexnative1108b/codexnative_root_1108b.img.0000` 之 FIEMAP，確認無誤後打包為 `fuxi_ios_boot-<名>.zip`（並強制將 `.sh`/`.prop`/`.txt` 轉為 LF、設定 `bin/*` 與 `.sh` 權限 `0755`）；
  - `customize.sh`：安裝時賦予 `$MODPATH/bin` 與 `action.sh` 執行權限；
  - `action.sh`：5 步安全檢查後切換 `bootctl set-active-boot-slot 1` 並 `reboot`。
- **復現**：  ```sh
  python work\ksu-ios-entry\build-module.py chain-v9-dualboot-launching
  ```
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：已建立 `chain-v1`～`v9` 多版 zip（`v9` `loader.size=66326528`、`loader.sha256=7977fbf0...2773`、`twrp.sha256=7d589d3c...ac1`）。
- **參考來源**：原始紀錄未列
- **結論／未完成**：**已停用**（2026-11-18 已自手機移除模組並封存於 `D:\xiaomi13\fuxi_Backup\ksu-module-remove-ksu-module-20261118\`；模組功能由 `boot_a` 常駐選單與 `dualboot/phone-check.py` 取代）。
- **出處**：`work/ksu-ios-entry/build-module.py:L1-L75`; `work/ksu-ios-entry/fuxi_ios_boot/action.sh:L1-L47`; `work/ksu-ios-entry/fuxi_ios_boot/customize.sh:L1-L5`; `xiaomi13-standalone-boot.md:L11`（2026-11-18 由早期紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

## 現行工具與流程（2026-11-14 起）

### T-048 手機啟動器 launch-loader-check.ps1

- **日期**：2026-11-16～2026-11-18 11:27（`ws: 3a1319e`；由 `launch-loader-check.ps1` 引入後持續擴充）
- **狀態**：日常使用中（AGENTS.md 第 7 節規定所有手機操作一律經此腳本）
- **開關**：
  - `-Loader <字串>`：指定 `work\Codex-Xiaomi13-GPU-20261109\<Loader>` 暫時開機（與各互斥功能開關區隔）
  - `-Kernel <字串>`：指定核心產物目錄，核對 `SHA256SUMS` 與 `loader/source.txt` 一致性
  - `-Out <字串>`：指定實機紀錄輸出資料夾名（預設依測試模式）
  - `-Seconds <整數>`：監看秒數，預設 300（建議設為「預期結果時間 + 30 秒」）
  - `-Reset <整數>`：USB 逾時保底自動重開回 Android 的期限（分鐘），必須大於 `-Seconds / 60`
  - `-FinalFramebuffer`／`-FramebufferHeight <整數>`：結束前擷取 framebuffer（建議裁切 `1200` 列以降低 USB 短斷風險）
  - `-InspectWifi`：唯讀檢查已存 Wi-Fi 設定，確認 HFS 與 block 唯讀且 overlay 零寫入，不讀密碼／鑰匙圈
  - `-WifiLogging <enable|disable>`：切換既有的原生 `LoggingFileEnabled` 布林值，保留差分寫前／寫後核對
  - `-InspectWifiLog`：唯讀擷取固定原生 Wi-Fi 日誌位置的決策摘要（略過敏感欄位）
  - `-CalibrateRtc`：透過 Recovery 唯讀量測 RTC 偏移並寫入校時 JSON
  - `-InspectCrash`：唯讀檢查手機 crash 紀錄
  - `-DualBoot`／`-DeployChain`／`-InstallPersistent`／`-ValidatePersistent`／`-UpgradePersistent`／`-RescueAndroid`／`-StartDaily`：雙系統選單與開機鏈部署／救援／驗證模式
- **問題／現象**：過去手動下 `fastboot boot` 与監看腳本容易在鎖檔未清時撞機、漏拉上一趟 `mtdblk` 紀錄，或因 PowerShell 5.1 的 `$ErrorActionPreference = 'Stop'` 將 `adb`/`fastboot` 寫到 stderr 的正常進度誤判為致命錯誤。
- **根因**：缺乏單一安全閘門整合三鎖檢查、自動回拉 `mtdblk`、前置驗證與逾時保底機制。
- **做法**：
  - 核心啟動器 `ws: work/statusbar-fix/launch-loader-check.ps1` 執行前先檢查三把手機鎖：`work\PHONE-LOCK-000`、`work\hybrid-driver\HOLD-PHONE`、`work\adsp-battery\hw000\HOLD`，任一存在即拒絕操作手機。
  - 在進入 iOS 前先經 `fuxi-log.sh pull` 保存上一趟 `mtdblk` 紀錄，避免新開機覆寫。
  - 呼叫 `boot-exp-v.ps1`：依序等待機體溫度、執行 `verify-loader-safety.py`／`verify-args-on-loader-payload.py` 核對 payload 與開機參數、透過 ADB 執行 `map`/`verify`/`unmap`、進 bootloader 執行 `fastboot boot`，並由 `run-monitor.ps1` 啟動 `fuxi-xnu-monitor.py`（搭配 `reset-soon.py` 透過 USB `FXIMON01` `0xc0, 0x54` 設定保底重置倒數）。
  - 腳本須在背景直接執行，**不可加 `*>` 或 `2>&1` 轉向**，避免 PowerShell 5.1 因 stderr 進度訊息中斷。
  - 演進（部分）：
    - `ws: 3a1319e` 2026-11-18 11:27：補齊 Wi-Fi 掃描恢復選項與首趟紀錄流程。
    - 先前提交陸續加入 `-InspectWifi`、`-WifiLogging`、`-InspectWifiLog`、`-FinalFramebuffer`、`-CalibrateRtc` 與 `-DualBoot` 等雙系統命令。
- **復現**：
  ```powershell
  D:\xiaomi13\fuxi_ios\work\statusbar-fix\launch-loader-check.ps1 -Loader <loader名> -Kernel <核心產物名> -Out <輸出目錄名> -Seconds 180 -Reset 4
  ```
  - 預期輸出：回拉上一趟 `mtdblk`、通過安全與 map 檢查、成功 `fastboot boot` 進入 iOS 並錄得 `console.txt`，期滿自動重開回 Android 並釋鎖。
- **主機測試**：`ws: work/statusbar-fix/test-launcher-framebuffer.ps1`（以 PowerShell AST 驗證語法、各參數互斥規則與參數轉發）。
- **驗證紀錄**：2026-11-17 00:03:58–00:08:50 以 `-Seconds 180 -Reset 4` 實測 `native-local-torchlevels-sb127-user-loader` 成功進 SpringBoard、回 Android 並釋鎖；`1200` 列裁切圖實測 9.2 秒／0 重連成功。
- **參考來源**：工作區 `AGENTS.md` 第 4、7 節；`boot-exp-v.ps1`、`run-monitor.ps1`、`reset-soon.py`。
- **結論／未完成**：已穩定作為全工作區唯一手機操作入口；已知瓶頸為全高 `2400` 列 framebuffer 截圖期間 USB 仍可能短斷（見 AGENTS.md 第 8 節）。
- **出處**：`ws: work/statusbar-fix/launch-loader-check.ps1`、`ws: work/statusbar-fix/test-launcher-framebuffer.ps1`、`ws: work/Codex-Xiaomi13-GPU-20261109/boot-exp-v.ps1`、`ws: work/Codex-Xiaomi13-GPU-20261109/run-monitor.ps1`、`ws: work/Codex-Xiaomi13-GPU-20261109/reset-soon.py`、`ws: AGENTS.md` 第 4、7、8 節、`ws: 3a1319e`。（2026-11-18 主代理分配範圍與來源，Gemini 依原始檔撰寫，主代理核對欄位、提交時間與路徑）

### T-049 loader 產生器 prepare-wlan-w18-loader.py

- **日期**：2026-11-15～2026-11-18 11:27（`ws: 3a1319e`；多筆提交持續擴充）
- **狀態**：日常使用中（所有 W18／Wi-Fi／9g 實驗與日常 loader 的標準封裝工具）
- **開關**：
  - 子命令 `inspect`（唯讀檢查來源 loader 與產物）與 `build`（封裝新 loader）
  - `--like <已驗 loader>`：沿用既有 loader 的 root extents、命令列與基本配置，只置換本次要試的開關
  - `--artifact <核心產物>`：指定目標核心目錄
  - `--ram-layout <full|9g>`、`--ram-sku <auto|8g|12g>`：RAM 版面與 SKU 選擇
  - `--rtc-calibration <json>`：讀取一小時內有效的校時 JSON，將偏移寫入 ADT `/chosen fuxi,rtc-offset`（不佔 607/608 bytes 命令列上限）
  - `--wifi-ios`、`--scan-dwell`、`--scan-cache`、`--scan-batch`、`--wifi-trace`、`--wifi-self-test`、`--wifi-rssi-bounds`、`--wifi-join-ssid <SSID>`、`--wifi-ui-only <SSID>`：原生 `ath12k`／iOS `wifid` 橋接、掃描駐留、快取、追蹤與測試開關
  - `--with-adsp`、`--audio-spf`、`--bt <1|3>`、`--haptic <mode>`、`--inject-media`、`--product-haptics`、`--haptic-amp`：ADSP、SPF 音訊、藍牙 (`fuxi,bt`) 與 HAP525 震動執行期開關
  - `--quiet-screen`（`sweetfb_noconsole=1`）／`--quiet-boot`（`sweetfb_noconsole=2`，兩者互斥）
  - `--display-gamut <sRGB|...>`、`--tap-script <script>`、`--tap-delay <ms>`、`--stderr-log <list>`、`--sample <list>`、`--profile <proc>`、`--crash-strings <list>`
  - MLO／供電實驗選項：`--sleep-clock`（`fuxiwlanmlo=64/192`）、`--big-mem`（`704`）、`--errdec`（`960`）、`--cnss-pdc`（`fuxiwlancnss=pdc`）
- **問題／現象**：iOS 16 開機命令列上限僅 607 bytes（含結尾 NUL 為 608 bytes），日常命令列已滿；每次測試單一驅動變數若人工改寫 `payload.bin` 與 `boot.img` 極易出錯或誤帶舊除錯參數。
- **根因**：需要一個具備嚴格型別與互斥防呆、能檢查核心標記（marker）並自動選擇寫入命令列或 ADT `/chosen` 屬性的統一封裝器。
- **做法**：
  - 驗證來源 `payload.bin`（`SWEETXNU` v1）與 `sha256.txt`、核心 `SHA256SUMS`。
  - 透過 `kernel.read_bytes()` 檢查目標核心是否真正編入對應開關的偵錯／執行期字串（例如 `fuxi,rtc-offset`、`fuxi,wifi-ux`、`fuxi,audio-spf`），缺少即拒絕打包。
  - 自動剔除來源中的一次性除錯鍵（`DROP_KEYS`），將允許的調整寫入 `/chosen` 屬性或重組不超過 607 bytes 的命令列。
  - 確認 GPU 韌體（`gmu`、`sqe`、`zap`）SHA-256、重新編譯 `ios-fuxi/shim` 並以 `mkbootimg.py` 包裝 Android boot v4 `boot.img`，輸出完整 manifest（`sha256.txt`、`memory-layout.json`）。
  - 演進：
    - `ws: 18c6990` 2026-11-17 22:27：加入 `--quiet-boot`、`--wifi-rssi-bounds` 與 `--haptic-amp`。
    - `ws: 3a1319e` 2026-11-18 11:27：加入 Wi-Fi 掃描恢復相關選項。
- **復現**：
  ```powershell
  python -X utf8 D:\xiaomi13\fuxi_ios\work\adsp-battery\prepare-wlan-w18-loader.py build --like <已驗loader> --artifact <核心產物> --name <新loader名> --rtc-calibration <校時json>
  ```
  - 預期輸出：產生 `work\Codex-Xiaomi13-GPU-20261109\<新loader名>\boot.img`、`payload.bin` 與 `sha256.txt`，命令列長度 `< 608` bytes。
- **主機測試**：無獨立 `test-*.py`；腳本內建多重斷言與核心二進位字串／ADT 屬性檢查。
- **驗證紀錄**：2026-11-17～11-18 多趟原生 Wi-Fi、ADSP、手電筒、震動與日常 `chain-v7`～`v9` 實驗均由此工具建置並通過 `verify-loader-safety.py` 與實機開機。
- **參考來源**：`work/adsp-battery/ksrc/tools/adt.py`、`mkpayload.py`、`mkadt-fuxi.py`、`uefi-fuxi/Resources/Scripts/mkbootimg.py`。
- **結論／未完成**：持續維護中的主力 loader 封裝器；使用時在 Windows 須加 `python -X utf8` 以免 cp950 解碼錯誤。
- **出處**：`ws: work/adsp-battery/prepare-wlan-w18-loader.py`、`ws: AGENTS.md` 第 1、8 節、`ws: 18c6990`、`ws: 3a1319e`。（2026-11-18 主代理分配範圍與來源，Gemini 依原始檔撰寫，主代理核對欄位、提交時間與路徑）

### T-050 loader 產生器 prepare-ram9g-loader.py

- **日期**：2026-11-15～2026-11-16（由 `ws-commits.md` 多筆修改紀錄確認）
- **狀態**：已採用（針對 9g RAM 配置與 ADSP／PMIC／手電筒／狀態欄／藍牙初期測試）
- **開關**：
  - 位置參數：`name`、`source`、`artifact`
  - `--ram-layout <full|9g>`（預設 `9g`）、`--ram-sku <auto|8g|12g>`（預設 `auto`）
  - `--pmic-survey`（`fuxi,pmic=1`）／`--pmic-torch-pulse`（`2`）／`--pmic-torch-runtime`（`3`）／`--torch-audit`（`fuxi,torchaudit=1`）
  - `--battery-audit`（`fuxi,batteryaudit=1`）、`--battery-get-pause-ms <0|130000>`、`--battery-get-pause-delay-ms <0..60000>`
  - `--rtc-calibration <json>`、`--rtc-adt`（寫入 `fuxi,rtc-offset`）
  - `--statusbar-height <30..150>`（寫入 `fuxi,sbshape`）、`--statusbar-check`（`fuxitaps=85`）、`--idle`、`--statusbar-inject`（`fuxi,inject=2`）
  - `--haptic-probe`（`fuxi,haptic=1`）、`--bluetooth-survey`（`fuxibt=1`）、`--ramtest`／`--ramtest-mib <128|512>`（`fuxi,ramtest=1|2`）
  - `--tap-script`／`--tap-script-adt`（`fuxi,tapcmd`）
- **問題／現象**：為驗證 9 GiB RAM 版面配置（`MEM_SIZE=0x23e000000`、`GPU_BASE=0xafe000000`）並早期測試 ADSP、PMIC 手電筒、藍牙與狀態欄高度，需要一個能將 12GB live DTB 驗證邏輯與新 ADT 屬性打包進 `boot.img` 的專用腳本。
- **根因**：早期 5g loader 產生器未涵蓋 9g RAM 與多項硬體診斷 ADT 旗標。
- **做法**：
  - 讀入來源 `payload.bin` 與核心 `kernel.development.fuxi`，透過 `mkadt-fuxi.py` 執行 `configure_ram(args.ram_layout)`、`validate_live(args.live_fdt, True, args.ram_sku)` 與 `reserve_gpu(tree)`。
  - 比對 `artifact/fuxi.adt` 與私有 ADT 的 `dram-size`、`fuxi-gpu-memory`，確認僅更動允許清單（`allowed_props`）內的屬性。
  - 以 Android NDK r26b clang（`aarch64-none-elf`）搭配 `-DFUXI_RAM_9G=1` 編譯 `ios-fuxi/shim/fuxi-loader`（禁止 `R_AARCH64_ABS` 重定位），嵌入三份已釘選 SHA-256 的 GPU 韌體與新 `payload.bin`，以 `mkbootimg.py` 生成 boot v4 映像並寫出 `memory-layout.json` 與 `source-input-sha256.txt`。
- **復現**：
  ```powershell
  python -X utf8 D:\xiaomi13\fuxi_ios\work\adsp-battery\prepare-ram9g-loader.py <新loader名> <來源loader> <核心產物> --ram-layout 9g --pmic-torch-runtime
  ```
  - 預期輸出：印出 `LOADER_BUILT 9g ...`、`RAM 0x8c0000000 0x23e000000 GPU 0xafe000000` 及 `boot.img` SHA-256。
- **主機測試**：無獨立測試檔；腳本內建 `llvm-readelf` 重定位檢查、ADT 屬性白名單比對與 firmware SHA-256 核對。
- **驗證紀錄**：2026-11-16 `work/localbuild/validation-20261116.zh-TW.md` 記錄以本工具離線封裝 `native-localbuild-9g-validation-loader`（`boot.img` SHA-256 `ff739aa45a7169614a238332e4ab2fd1287d7f8584bb869375237cc4e150b89c`），且多趟 ADSP／手電筒／9g 實機均使用本工具封裝。
- **參考來源**：`mkadt-fuxi.py`、`ios-fuxi/shim/fuxi-loader/`、`uefi-fuxi/Resources/Scripts/mkbootimg.py`。
- **結論／未完成**：功能完整穩定；後續 Wi-Fi W18 與雙系統日常調校主要改由功能更豐富的 `prepare-wlan-w18-loader.py` 承接。
- **出處**：`ws: work/adsp-battery/prepare-ram9g-loader.py`、`ws: work/localbuild/validation-20261116.zh-TW.md`。（2026-11-18 主代理分配範圍與來源，Gemini 依原始檔撰寫，主代理核對欄位、提交時間與路徑）

### T-051 本機核心建置（WSL）

- **日期**：2026-11-16～2026-11-18 05:29（`ws: 956d5a8`、`ws: 586c54e`）
- **狀態**：日常使用中（9g 本機核心與 ath12k 連結均已實機驗證）
- **開關**：
  - `build-kernel.ps1`：`-Ram <full|9g>`、`-Name <名稱>`、`-Harness <路徑>`、`-Ath12k <資料夾>`、`-Clean`、`-Tests`、`-Jobs <N>`、`-Distro Ubuntu-26.04`
- **問題／現象**：GitHub macOS CI 每次從零建置需約 12.5～16.5 分鐘，嚴重拖慢核心補丁迭代速度；且 Windows 上執行 WSL 時常因「Failed to mount T:\」stderr 警告導致 PowerShell 5.1 提前中止。
- **根因**：缺乏本機增量編譯環境與 Linux 端 Darwin 交叉工具鏈／IIG replay 流程。
- **做法**：
  - 在 WSL `Ubuntu-26.04`（虛擬磁碟位於 `E:\wsl\Ubuntu-26.04`，建置樹 `/root/fuxi-build`）安裝 UniXNU Linux 工具鏈（`/opt/unixnu-linux`）並參考 `ios-redfin`（MIT）改寫 `fuxi-kernel-build.sh`。
  - 從 `harness/.github/workflows/kernel-adsp-battery.yml` 自動解析 `ios-fuxi` 釘選 commit 與補丁順序（公開倉庫 `fuxi-ios` 則改由 `upstream/EXPORT-SOURCES.txt` 的 `kernel-base` tree 本地取出）；自動處理 Windows CRLF→LF 轉換。
  - 為每種 RAM 配置維護獨立增量樹（`work-full`／`work-9g`）並以 `flock` 互斥保護；加入提前執行的 IIG replay（17 組 header/implementation）、`__udivti3`／`udivmodti4` 補充編譯、`-Ath12k` 連結與 `llvm-nm` 檢查，以及最終 ISA 禁止指令掃描。
  - `build-kernel.ps1` 每次複製獨立暫存腳本進 `/root/` 執行，並設 `$ErrorActionPreference = 'Continue'` 僅依 `wsl.exe` 結束碼判定成敗。
  - 演進：
    - `ws: 956d5a8` 2026-11-18 05:23：支援從公開倉庫歷史 `kernel-base` 取出基底。
    - `ws: 586c54e` 2026-11-18 05:29：當 `work\adsp-battery\harness` 不存在時自動改用倉庫根目錄作為 `-Harness`。
- **復現**：
  ```powershell
  D:\xiaomi13\fuxi_ios\work\localbuild\build-kernel.ps1 -Ram 9g -Name kernel-local-<實驗名>-9g-artifact
  ```
  - 預期輸出：產物寫入 `work\Codex-Xiaomi13-GPU-20261109\<Name>\`，`BUILD.txt` 第一行顯示 `result ok`，包含 `kernel.development.fuxi`、`.unstripped`、`fuxi.adt`、`SHA256SUMS`、`INPUTS.txt`、`ISA.txt`。
- **主機測試**：加 `-Tests` 參數會依 workflow 原始順序穿插套用補丁與執行 33 個主機測試命令（Ubuntu 原生 clang＋ASan／UBSan），結果記於 `logs\tests.txt`。
- **驗證紀錄**：
  - 2026-11-16：`full`/`9g` 完整重編各 70 秒（make 38–39 秒），`9g` 無修改增量 21 秒（make 2 秒），`-Tests` 33 個命令全通過（96 秒）；ISA 檢查 2,270,074 條指令、禁止指令 0。
  - 2026-11-17 00:03:58：首次 9g 本機核心 `kernel-local-validated-9g-artifact`（`f30bc339...`）上機進 SpringBoard 成功；後續 `-Ath12k` 本機核心 `kernel-local-wlandma-9g-artifact`（`2daafc81...`）通過 34-BSS 真實掃描。
- **參考來源**：UniXNU Linux 工具鏈；`DenialOfGravity/ios-redfin` 的 `tools-claude/localbuild/xnu.sh`（MIT）；`blacktop/darwin-xnu-build`。
- **結論／未完成**：已完全取代日常等待 macOS CI 的流程；但因使用上游 clang（無 CTF/dSYM），正式結論依據或行為異常排錯仍需 CI 對照；5g 本機核心尚未實機開機驗證。
- **出處**：`ws: work/localbuild/build-kernel.ps1`、`ws: work/localbuild/fuxi-kernel-build.sh`、`ws: work/localbuild/validation-20261116.zh-TW.md`、`ws: AGENTS.md` 第 9 節、`ws: 956d5a8`、`ws: 586c54e`。（2026-11-18 主代理分配範圍與來源，Gemini 依原始檔撰寫，主代理核對欄位、提交時間與路徑）

### T-052 本機 iOS 函式庫建置與簽署校驗（WSL）

- **日期**：2026-11-17 20:46（`ws: c0cadee`）～2026-11-18 05:29（`ws: 586c54e`）
- **狀態**：日常使用中（已通過主機測試、Mach-O 簽署校驗與實機載入）
- **開關**：
  - `build-sb-shape.ps1`：`-Name <名稱>`、`-Harness <路徑>`、`-Distro Ubuntu-26.04`
- **問題／現象**：`libfuxi-sb.dylib` 原本依賴 GitHub macOS runner（`sb-shape.yml`）編譯與 `codesign`，每改一次 userland 都要排隊等待，難以快速迭代。
- **根因**：核心用的 `ld64` 未連 TAPI，無法讀取 iOS SDK `.tbd` 檔；且 WSL 內沒有 Apple 原生 `codesign`。
- **做法**：
  - 在 WSL 內以 `/opt/unixnu-linux/llvm/bin/clang` 編譯（`-target arm64-apple-ios16.0`），並改用支援 TAPI 的 `/usr/bin/ld64.lld-21`（LLVM 21.1.8）連結 `iPhoneOS16.5.sdk`（Theos `sdks` `master-146e41f`，每次核對封存 SHA-256 `5e0fd3f01266cce4ce012d4a99b38eb56578fca40d09edc81cd83dee958202fb`），搭配 `-adhoc_codesign` 產生 ad hoc 簽署（不使用 no-op `codesign` shim）。
  - `fuxi-sb-build.sh` 同次執行 ASan／UBSan 主機測試（`test-flashlight-levels.c`、`test-vibe-pattern.c`、`test-vibe-worker.py`），並編譯連結 `FuxiShape.c`、`FuxiFlashlight.c`、`FuxiBatteryAudit.c`、`FuxiVibrate.c`、`FuxiWifiAudit.c` 為 `libfuxi-sb.dylib`。
  - `verify-sb-shape.py` 檢查 plain arm64 `MH_DYLIB`、iOS 16.0 部署版本、`install_name`、相依函式庫、CodeDirectory 逐頁 4K SHA-256 雜湊，並執行 3 個位元翻轉負測試（`--self-test`）。
  - 嚴禁重用已存在的輸出目錄，失敗寫 `result FAILED`。
- **復現**：
  ```powershell
  D:\xiaomi13\fuxi_ios\work\localbuild\build-sb-shape.ps1 -Name sb-local-<實驗名>-artifact
  ```
  - 預期輸出：約 3～7 秒完成，印出 `LOCAL_DYLIB_OK ...`，於 `work\Codex-Xiaomi13-GPU-20261109\<Name>\` 產出 `libfuxi-sb.dylib`、`signature.json`、`BUILD.txt`、`INPUTS.txt`、`SHA256SUMS`。
- **主機測試**：建置腳本內自動執行 `test-flashlight-levels.c`（ASan+UBSan）、`test-vibe-pattern.c`（ASan+UBSan）、`test-vibe-worker.py` 及 `verify-sb-shape.py --self-test`。
- **驗證紀錄**：2026-11-17 首份 `sb-local-native-errors-validated-artifact` 通過主機測試、plain arm64 與 28 頁簽署校驗，且重複建置逐位元相同；但需注意此為自簽碼頁校驗，不代表 Apple 憑證信任。
- **參考來源**：Theos Linux／WSL 建置文件、LLVM Mach-O LLD 文件、UniXNU `scripts/audit-target-userspace.py`。
- **結論／未完成**：完成並投入日常使用；因與 Xcode CI 編譯器及 SDK 不同，產物不逐位元相同，正式來源對照仍可跑 `sb-shape.yml`。
- **出處**：`ws: work/localbuild/build-sb-shape.ps1`、`ws: work/localbuild/fuxi-sb-build.sh`、`ws: work/localbuild/verify-sb-shape.py`、`ws: AGENTS.md` 第 11 節、`ws: c0cadee`、`ws: 586c54e`。（2026-11-18 主代理分配範圍與來源，Gemini 依原始檔撰寫，主代理核對欄位、提交時間與路徑）

### T-053 harness 的硬體線 CI

- **日期**：2026-11-15～2026-11-18（harness `codex/adsp-battery` 分支持續維護）
- **狀態**：已採用（作為正式可追溯產物與對照基準，日常迭代已轉由本機 WSL 建置分擔）
- **開關**：各 workflow 皆支援 `workflow_dispatch` 與 `push` 到 `codex/adsp-battery` 分支（依 `paths` 過濾觸發）；`kernel-adsp-battery.yml` 另提供 `workflow_call` 參數 `ram_layout`（`full`/`9g`）、`artifact_name`、`runner_label`、`ath12k`（布林值）
- **問題／現象**：需要一套公開、可重現的 GitHub Actions 流水線來從釘選 `zgl97/ios-fuxi`（`979e2f08`）套用 18 份補丁、編譯 XNU 核心、`ath12k` 模組物件與 `libfuxi-sb.dylib`，並提供快速的主機端單元測試閘門。
- **根因**：Apple Xcode 工具鏈與原生 Mach-O 連結環境在雲端 macOS runner 上才能獲得官方對照產物。
- **做法**：
  - `kernel-adsp-battery.yml`：主核心 workflow（預設 5g `full`），拉取 `979e2f08`，依序套用 18 份 `kernel-*.patch` 並穿插執行主機端測試，開啟 `FUXI_IIG_MODE: replay`，於 `macos-15-intel` 建置核心與 `fuxi.adt`。
  - `kernel-adsp-ram9g.yml` 與 `kernel-adsp-ram9g-intel.yml`：呼叫 `kernel-adsp-battery.yml` 傳入 `ram_layout: 9g` 產出 `fuxi-kernel-adsp-ram9g`（`kernel-adsp-ram9g-intel` 為固定 `macos-15-intel` 變體）。
  - `ath12k-objects.yml`：於 `macos-15-intel` 執行 `tools/build-ath12k-objects.sh` 編譯並合併 `ath12k` 物件，輸出 `fuxi-ath12k-objects` artifact 供本地或核心 CI 連結。
  - `kernel-ath12k-ram9g.yml`：呼叫 `kernel-adsp-battery.yml` 傳入 `ram_layout: 9g` 與 `ath12k: true`，產出帶 `ath12k` 的 9g 核心 `fuxi-kernel-ath12k-ram9g`。
  - `sb-shape.yml`：於 `macos-15` 執行手電筒／震動主機測試，並用 Xcode `clang` 與 `codesign -f -s -` 建置 `libfuxi-sb.dylib` 上傳 `fuxi-sb-shape`。
  - `hw000-preparation.yml` ＋ `patches/hw000-preparation.patch`（目前為空檔）：約 1.5 分鐘的快速前置檢查 CI，分 `wlan`（Ubuntu `clang` 跑 `sync-fuxi-*.py --check` 與 WLAN／RTC／Torch 主機模型）與 `host`（`macos-15-intel` 安裝 `capstone==5.0.7`）兩組 job。
- **復現**：在 harness 倉庫推送變動至 `codex/adsp-battery` 分支或於 GitHub Actions 手動觸發對應 workflow（公開倉庫 `fuxi-ios` 匯出後已無 push-to-main 自動觸發）。
- **主機測試**：`hw000-preparation.yml` 與 `kernel-adsp-battery.yml` 內建全套 `tools/test-fuxi-*.py` 與 `tests/test-*.py`。
- **驗證紀錄**：CI run `43896202141`（`9g` 核心，16 分 36 秒，40 項 SHA256SUMS 通過）、`43907693114`、`43914132655` 均已成功產出並與本機建置對照驗證。
- **參考來源**：`harness/README.md`、`AGENTS.md` 第 1、2、9、12 節。
- **結論／未完成**：CI 缺乏編譯快取（見 AGENTS.md 第 8 節），故日常改採本機 WSL 建置，僅在需要官方 Apple clang 對照或重編 `ath12k` 物件時使用 CI。
- **出處**：`harness: .github/workflows/kernel-adsp-battery.yml`、`kernel-adsp-ram9g.yml`、`kernel-adsp-ram9g-intel.yml`、`kernel-ath12k-ram9g.yml`、`ath12k-objects.yml`、`sb-shape.yml`、`hw000-preparation.yml`、`patches/hw000-preparation.patch`、`README.md`。（2026-11-18 主代理分配範圍與來源，Gemini 依原始檔撰寫，主代理核對欄位、提交時間與路徑）

### T-054 Wi-Fi 使用者自測流程

- **日期**：2026-11-16～2026-11-17（由 `ws-commits.md` 第 385、458、542 行確認）
- **狀態**：已採用（供使用者親自輸入真實 Wi-Fi 密碼測試連線）
- **開關**：
  - `-Minutes <3..40>`：測試開機時間（預設 15 分鐘），自動換算 `-Seconds` 與 `-Reset`（`Minutes + 5`）
  - `-Kernel <產物名>`：預設 `kernel-local-bt2-9g-artifact`
  - `-Like <來源loader>`：預設 `native-srgb-safari-ram9g-sb127-loader`
- **問題／現象**：自動化測試只帶假密碼驗證 `EAUTH`，無法驗證真實密碼的四向握手、DHCP 與設定頁使用者操作；同時又必須絕對保證真實密碼不進入開機參數、loader 或紀錄。
- **根因**：需要一條一鍵完成校時、組裝帶安全選項的 native Wi-Fi loader 並交由使用者手動操作的安全暫時開機流程。
- **做法**：
  - 先執行 `rtc-calibration.py capture` 量測有效 RTC 偏移（確保 iOS 系統日期正確，讓 HTTPS 可用）。
  - 呼叫 `prepare-wlan-w18-loader.py build`，固定開啟 `--wifi-ios --scan-dwell --wifi-trace --scan-batch --with-adsp --audio-spf --quiet-screen --display-gamut sRGB --haptic 2 --inject-media --product-haptics --stderr-log wifid,mediaserverd` 並帶入 `--rtc-calibration`（偏移放 ADT，不佔命令列）。
  - 透過 `launch-loader-check.ps1` 以暫時開機方式啟動，完全不改動日常 `boot_b`／`boot_a`，密碼全程僅由使用者在手機螢幕上輸入。
- **復現**：
  ```powershell
  D:\xiaomi13\fuxi_ios\work\adsp-battery\wifi-user-session.ps1 -Minutes 15
  ```
  - 預期輸出：手機進入 iOS 後可於設定頁手動輸入密碼連線，期滿自動重開回 Android 並保存 `console.txt`。
- **主機測試**：無（串接 `rtc-calibration.py`、`prepare-wlan-w18-loader.py` 與 `launch-loader-check.ps1` 之包裝腳本）。
- **驗證紀錄**：2026-11-17 06:50 起多次實測成功（含對 `<SSID>` 完成 WPA2 四向握手與取得 `<區網IP>`），確認腳本、loader 與紀錄中均無密碼殘留。
- **參考來源**：`AGENTS.md` 第 4 節。
- **結論／未完成**：完成且可重複使用；互動震動與跨重啟自動加入仍依各項目進度持續修復。
- **出處**：`ws: work/adsp-battery/wifi-user-session.ps1`、`ws: AGENTS.md` 第 4 節。（2026-11-18 主代理分配範圍與來源，Gemini 依原始檔撰寫，主代理核對欄位、提交時間與路徑）

### T-055 公開倉庫匯出（export-fuxi-ios.py）

- **日期**：2026-11-18 05:22（`ws: c75a3b6`）～05:54（`ws: b105cba`）
- **狀態**：已採用（2026-11-18 起用於同步公開倉庫 `scyx2611/fuxi-ios`）
- **開關**：
  - `--dest <目錄>`：指定本機 `fuxi-ios` clone 路徑
  - `--check`：對整個 clone 掃描序號、SSID、使用者路徑、私有位址、金鑰樣式與未允許的二進位／大檔
  - `--verify`：逐檔核對 `HEAD` 與 `.git/fuxi-export-manifest.tsv` 的模式與 blob SHA-1
- **問題／現象**：工作區包含大量個人備份、執行日誌、adb 序號、家用 Wi-Fi SSID、區網 IP 與本機使用者路徑，無法直接將私人工作區公開推送到 GitHub。
- **根因**：必須從 git 物件（而非工作目錄）做確定性匯出，執行嚴格次數核對的字串去識別化，並補上 Windows git 無法記錄的執行權限位元與上游原始碼樹資訊。
- **做法**：
  - 只讀取工作區 `HEAD`、`harness HEAD`、`ios-fuxi`（`bangprovn/ios-fuxi@27f6a31`＋本機修改）、`ksrc`（`zgl97/ios-fuxi@979e2f08`＋本機 commit）與 `uefi-fuxi` 的 `mkbootimg.py`（AOSP `57969ab`）git 物件；拒絕 clone 存在未提交修改。
  - 套用 `LITERAL_RULES` 逐條取代序號、使用者路徑、家用 SSID 與區網 IP（次數必須剛好符合預期，否則中止）；排除 `.json`/`.log`/`.txt` 等執行紀錄（保留 `WORKSPACE_FORCE` 內的授權與 `expected-fiemap.txt`）。
  - 寫出 `.git/fuxi-export-manifest.tsv`（供 Windows 上 `git add --chmod=+x` 後用 `--verify` 核對），並以 `--check` 掃描全庫（僅豁免與上游 `ios-sweet` 逐位元相同的 `192.168.7.x` USB 網段）。
  - 演進：
    - `ws: c75a3b6` 2026-11-18 05:22：新增匯出腳本與上游原始碼樹收錄。
    - `ws: 586c54e` 2026-11-18 05:29：僅從 `uefi-fuxi` 讀取 `mkbootimg.py`，避開 submodule 拒絕。
    - `ws: 2442f1f` 2026-11-18 05:37：補齊被匯出工具載入的 `root_layers.py`、`verify-args-on-loader-payload.py` 等輔助腳本。
    - `ws: b105cba` 2026-11-18 05:54：將公開匯出流程寫入 `AGENTS.md` 第 12 節。
- **復現**：
  ```powershell
  python -X utf8 D:\xiaomi13\fuxi_ios\work\repo-export\export-fuxi-ios.py
  python -X utf8 D:\xiaomi13\fuxi_ios\work\repo-export\export-fuxi-ios.py --verify
  python -X utf8 D:\xiaomi13\fuxi_ios\work\repo-export\export-fuxi-ios.py --check
  ```
  - 預期輸出：匯出無錯誤，`--verify` 與 `--check` 皆通過後方可 `git push`。
- **主機測試**：`--verify` 與 `--check` 內建完整性與洩漏掃描。
- **驗證紀錄**：2026-11-18 成功建立公開倉庫 `scyx2611/fuxi-ios`，並使匯出後的倉庫可直接執行 `build-kernel.ps1` 與 `build-sb-shape.ps1`。
- **參考來源**：`AGENTS.md` 第 12 節。
- **結論／未完成**：已穩定運作；手寫文件（`README.md`、`docs/`、`THIRD_PARTY.md` 等）不由本腳本管理，需於 clone 手動維護；上游 `ios-fuxi` 與 `ksrc` 未宣告授權，權利人要求時須移除。
- **出處**：`ws: work/repo-export/export-fuxi-ios.py`、`ws: AGENTS.md` 第 12 節、`ws: c75a3b6`、`ws: 586c54e`、`ws: 2442f1f`、`ws: b105cba`。（2026-11-18 主代理分配範圍與來源，Gemini 依原始檔撰寫，主代理核對欄位、提交時間與路徑）

### T-056 工作區規範與文件

- **日期**：2026-11-08～2026-11-18 05:54（`ws: b105cba` 更新 `AGENTS.md`）
- **狀態**：日常使用中（全專案規範、授權與對外說明入口）
- **開關**：無
- **問題／現象**：多條開發線（GPU、ADSP、Wi-Fi、雙系統、本機建置）並行時，若無統一操作規範與依賴說明，極易誤碰手機、重跑冗長 CI 或漏掉安全檢查。
- **根因**：需要一份集中記錄省時流程、手機鎖閘門、本機建置規則與第三方授權的文件集。
- **做法**：
  - `AGENTS.md`：工作區最高指導規範，定義第 0–12 節（先找可復用實作、最便宜驗證、一次多實驗、背景等 CI、`launch-loader-check.ps1` 規則、手機三鎖、WSL 本機建置與公開匯出流程）。
  - `.gitignore`：排除 `work\`、大型二進位與暫存產物，避免誤提交。
  - `LICENSE`（MIT）與 `NOTICE`：專案授權與第三方組件（`ios-fuxi`、`zgl97/ios-fuxi`、`mkbootimg.py` 及專有韌體排除聲明）歸屬。
  - `README.md`、`README.zh-TW.md`、`README.zh-CN.md`、`README_WINDOWS.md`：專案總覽、雙路線驗證狀態、架構圖、Windows WinUSB 驅動與本地準備指引。
  - `requirements.txt`：主機端 Python 套件依賴（`pyusb>=1.2.1`、`libusb-package>=1.0.26`、`pillow>=10.0.0`、`capstone>=5.0.0`）。
  - `patches/ios-fuxi-fuxi13.patch`：早期對 `ios-fuxi` `shim/fuxi-loader/` 的修補檔（讓 `a740_zap_fuxi.mbn` 允許雙雜湊、加 `-Wno-error=unterminated-string-initialization`，並精簡 `loader.c` 的部分 `con_str`/`con_clear` 除錯輸出）。
- **復現**：依 `requirements.txt` 安裝 Python 依賴並閱讀 `AGENTS.md` 與 `README.zh-TW.md` 即可建立主機操作環境。
- **主機測試**：無。
- **驗證紀錄**：`AGENTS.md` 第 9、11、12 節流程皆已於 2026-11-16～11-18 實機與匯出驗證。
- **參考來源**：專案內各線實作紀錄。
- **結論／未完成**：持續隨工程進度更新（如 `AGENTS.md` 第 8 節已知瓶頸尚未全數解除）。
- **出處**：`ws: AGENTS.md`、`ws: .gitignore`、`ws: LICENSE`、`ws: NOTICE`、`ws: README.md`、`ws: README.zh-TW.md`、`ws: README.zh-CN.md`、`ws: README_WINDOWS.md`、`ws: requirements.txt`、`ws: patches/ios-fuxi-fuxi13.patch`。（2026-11-18 主代理分配範圍與來源，Gemini 依原始檔撰寫，主代理核對欄位、提交時間與路徑）

### T-057 主機驗證資料（hostcheck WLAN 標記規範）

- **日期**：2026-11-17
- **狀態**：已採用（作為本機 `ath12k` 實機驗收的結構化判準資料）
- **開關**：無
- **問題／現象**：實機測試 `console.txt` 動輒數千行，依 AGENTS.md 第 3 節須先寫明「console 出現哪一行算成功、哪一行算失敗」，避免每次回來通讀整份 log 或誤判成功範圍。
- **根因**：需要機器與人工皆可讀的固定 JSON 規格，明確綁定 `source_commit`、`harness_commit`、預期成功／失敗標記與驗證範圍限制。
- **做法**：
  - `wlan-control-20261117.json`：對應 `native-wlan-control-ram9g-sb127-loader`（`native-hw000ath12k-control-boot-1`，120 秒／3 分鐘 reset），定義 6 條 `success_markers`（含 `FUXI ATH12K: diagnostic control transport installed`、`control operation 1/2/258 status 0`、掃描完成且 error/fault 皆 0）與 7 條 `failure_markers`，並註明範圍僅限原生診斷掃描（不含 iOS 設定頁與連線）。
  - `wlan-dma-20261117.json`：繼承 `wlan-control-20261117.json`（`native-wlan-dma-ram9g-sb127-loader`、`native-hw000ath12k-dma-boot-1`，90 秒／2 分鐘 reset），記錄變更變數為支援模組 `dma_kfree(0, owned_base, 0)` 回呼，並追加 `DMA free refused`、`DMA sync refused` 兩條失敗標記。
- **復現**：上機後依 JSON 中的 `success_markers` 與 `failure_markers` 檢索對應 run 的 `console.txt`。
- **主機測試**：無（判準規格資料檔）。
- **驗證紀錄**：兩份檔案分別對應 2026-11-17 00:26:28 `native-hw000ath12k-control-boot-1`（驗證 control transport 成功、因 DMA 釋放缺接鎖停）與 00:34:12 `native-hw000ath12k-dma-boot-1`（通過 34-BSS 掃描、error 0／faults 0）。
- **參考來源**：`AGENTS.md` 第 3 節、`work/localbuild/validation-20261116.zh-TW.md`。
- **結論／未完成**：完成並留存於 `work/adsp-battery/hostcheck/`（依匯出規則不進公開倉庫）。
- **出處**：`ws: work/adsp-battery/hostcheck/wlan-control-20261117.json`、`ws: work/adsp-battery/hostcheck/wlan-dma-20261117.json`、`ws: work/localbuild/validation-20261116.zh-TW.md`。（2026-11-18 主代理分配範圍與來源，Gemini 依原始檔撰寫，主代理核對欄位、提交時間與路徑）

### T-058 ksrc 繼承的 GPU、建置與其他主機測試

- **日期**：上游加入於 2026-10-20～2026-10-31（zgl97/ios-fuxi，基底 `979e2f08` 之前）；2026-11-18 收錄進紀錄庫
- **狀態**：繼承、原樣保留；本專案的 CI 與本機建置（`build-kernel.ps1 -Tests`）都不執行，只有 `tools/test-fuxi-nativemetal.py` 由 harness `native-metal-host-tests.yml` 呼叫（見 T-059）
- **開關**：測試本身不讀開機參數；被測的上游功能用到的環境變數與開機參數：`FUXI_MTL_OCCLUDE`、`FUXI_MTL_NOFETCH_RP`、`FUXI_MTL_DRAW_AREA`、`FUXI_MTL_SPIRV_UNROLL`、`FUXI_MTL_HALF_ROUND`（`test-fuxi-metal-occlude.py`）、`fuxigpucache`（`test-fuxi-metal-cache.py`）
- **問題／現象**：ksrc（`work/adsp-battery/ksrc`，上游 zgl97/ios-fuxi）的 `tools/` 裡有 50 個主機測試檔（35 個 `.py`、15 個 `.cpp`）不屬於本專案的任何 XNU 補丁，`build-catalog.py --check` 列為「測試未對應」。它們驗證上游的使用者空間 GPU 路線：FuxiMetal 前端與 metal2vulkan 轉譯 bridge（M5b～M9p 各里程碑）、NativeMetal、Turnip／Mesa 交叉建置、IIG 預產物快取、iOS 27 root／prelink 工具，以及冷卻 A/B 工具。
- **根因**：上游把主機測試和實作工具放在同一個 `tools/`；本專案從上游取 ksrc 做 XNU 補丁層，這些檔案隨基底一起進來。上游自己的 `.github/workflows/fuxi-build.yml` 也沒有執行它們，執行方式只寫在上游 `docs-lanes/*.md`。
- **做法**：逐檔盤點（2026-11-18 主代理以 `git -C work/adsp-battery/ksrc log --diff-filter=A` 取加入提交、以 Python `ast` 取 `argparse` 參數、從 `docs-lanes/`／`docs/` 找引用文件），結果如下表。說明欄保留上游英文原文，不翻譯以免失真。
  | 檔案 | 分類 | 上游加入提交 | 修改次數 | 參數（* 必填） | 上游文件 | 上游原文說明（docstring 或檔頭註解） |
  | --- | --- | --- | --- | --- | --- | --- |
  | `tools/test-fuxi-metal-m6.py` | Metal 轉譯里程碑（M5b～M9p） | `dace335` 2026-10-21 | 7 | `--headers* --icd --glslang` | `docs-lanes/gpu-fps.md`、`docs-lanes/gpu-translation.md`、`docs-lanes/kernel-h1.md` | Linux software Vulkan M6 resource tests; no Apple SDK or device required. |
  | `tools/test-fuxi-metal-m6b.cpp` | Metal 轉譯里程碑（M5b～M9p） | `9a81f41` 2026-10-21 | 1 | 由 `test-fuxi-metal-m6b.py` 編譯執行 | 無 | Linux: CI AIR through the actual pinned bridge, imported source and target. |
  | `tools/test-fuxi-metal-m6b.py` | Metal 轉譯里程碑（M5b～M9p） | `9a81f41` 2026-10-21 | 2 | `--source* --build --metallib* --sums* --headers* --icd` | `docs-lanes/metal-m6b.md` | Build our bridge against clean pinned external sources; test CI AIR on Linux Vulkan. Inputs and build products stay outside tracked source. No device  |
  | `tools/test-fuxi-metal-m7.cpp` | Metal 轉譯里程碑（M5b～M9p） | `d4d5848` 2026-10-21 | 5 | 由 `test-fuxi-metal-m7.py` 編譯執行 | `docs-lanes/metal-m9e.md` | Actual translator plan + lavapipe; AIR control is original, not Xcode AIR. |
  | `tools/test-fuxi-metal-m7.py` | Metal 轉譯里程碑（M5b～M9p） | `d4d5848` 2026-10-21 | 2 | `--source* --build* --metallib* --sums* --headers* --icd --llvm-as --ci-m7` | `docs-lanes/metal-m7.md` | Pinned bridge + verified CI M6b regression + original AIR M7 lavapipe control. The hand-authored AIR control does not qualify Xcode's stage-6 MSL. Add |
  | `tools/test-fuxi-metal-m9a.cpp` | Metal 轉譯里程碑（M5b～M9p） | `a48c32e` 2026-10-21 | 7 | 由 `test-fuxi-metal-m9a.py` 編譯執行 | `docs-lanes/metal-m9e.md` | Linux host checks: typed constants cross the real C/Rust bridge; no submits. |
  | `tools/test-fuxi-metal-m9a.py` | Metal 轉譯里程碑（M5b～M9p） | `a48c32e` 2026-10-21 | 3 | `--source* --build* --icd --llvm-as --spirv-val` | `docs-lanes/metal-m9a.md` | Linux M9a bridge specialization and staging controls; original generated AIR. No Apple shader inputs or device access. Native CA compilation belongs t |
  | `tools/test-fuxi-metal-m9b.cpp` | Metal 轉譯里程碑（M5b～M9p） | `82c154a` 2026-10-21 | 5 | 由 `test-fuxi-metal-m9b.py` 編譯執行 | `docs-lanes/metal-m9e.md` | Linux source controls: production validation/cache lifetime, no submissions. |
  | `tools/test-fuxi-metal-m9b.py` | Metal 轉譯里程碑（M5b～M9p） | `82c154a` 2026-10-21 | 3 | `--compile-only --source* --build* --headers --icd --llvm-as --spirv-val` | `docs-lanes/metal-m9b.md`、`docs-lanes/metal-m9e.md` | Linux M9b production-cache checks plus existing owned-source bridge controls. No device access or GPU submissions. Native CA adapters require Mac test |
  | `tools/test-fuxi-metal-m9g.cpp` | Metal 轉譯里程碑（M5b～M9p） | `ff84537` 2026-10-22 | 1 | 由 `test-fuxi-metal-m9g.py` 編譯執行 | 無 | Original authored AIR / production Backend lavapipe regression. No Apple data. |
  | `tools/test-fuxi-metal-m9g.py` | Metal 轉譯里程碑（M5b～M9p） | `ff84537` 2026-10-22 | 1 | `--compile-only --translator* --icd --headers --llvm-as` | `docs-lanes/gpu-translation.md`、`docs-lanes/metal-m9g.md`、`docs-lanes/metal-m9j.md` | Compile or run authored M9g lavapipe controls with the separately built bridge. Build the pinned bridge using the existing offline or M9a harness firs |
  | `tools/test-fuxi-metal-m9h.cpp` | Metal 轉譯里程碑（M5b～M9p） | `8253ecf` 2026-10-22 | 1 | 由 `test-fuxi-metal-m9h.py` 編譯執行 | 無 | Authored AIR only: production Backend reuse/rollback/pixel controls. |
  | `tools/test-fuxi-metal-m9h.py` | Metal 轉譯里程碑（M5b～M9p） | `8253ecf` 2026-10-22 | 1 | `--compile-only --translator* --icd --headers --llvm-as` | `docs-lanes/gpu-translation.md`、`docs-lanes/metal-m9h.md`、`docs-lanes/metal-m9j.md` | Compile or run authored M9h lavapipe controls with the separately built bridge. Build the pinned bridge using the existing offline or M9a harness firs |
  | `tools/test-fuxi-metal-m9i.cpp` | Metal 轉譯里程碑（M5b～M9p） | `c5168a6` 2026-10-22 | 4 | 由 `test-fuxi-metal-m9i.py`、`test-fuxi-metal-cache.py` 編譯執行 | 無 | Authored AIR/pixels only. Production persistence and deterministic driver controls. |
  | `tools/test-fuxi-metal-m9i.py` | Metal 轉譯里程碑（M5b～M9p） | `c5168a6` 2026-10-22 | 1 | `--compile-only --source --translator --icd --headers --llvm-as` | `docs-lanes/metal-gpu-cache.md`、`docs-lanes/metal-m9i.md`、`docs-lanes/metal-m9j.md` | Compile/run authored-AIR M9i persistence controls on Linux lavapipe. --compile-only builds without running test executables or submitting GPU work. Th |
  | `tools/test-fuxi-metal-m9k.cpp` | Metal 轉譯里程碑（M5b～M9p） | `87bc871` 2026-10-22 | 4 | 由 `test-fuxi-metal-m9k.py` 編譯執行 | 無 | Authored mip/format controls through production Backend + external bridge. |
  | `tools/test-fuxi-metal-m9k.py` | Metal 轉譯里程碑（M5b～M9p） | `87bc871` 2026-10-22 | 3 | `--compile-only --translator* --icd --headers --llvm-as` | `docs-lanes/gpu-translation.md`、`docs-lanes/metal-m9k.md` | Compile or run authored M9k lavapipe controls with the separately built bridge. Build the pinned bridge using the existing offline or M9a harness firs |
  | `tools/test-fuxi-metal-m9l.cpp` | Metal 轉譯里程碑（M5b～M9p） | `17c9d54` 2026-10-22 | 3 | 由 `test-fuxi-metal-m9l.py` 編譯執行 | 無 | Authored MRT pixels through production Backend + dynamically loaded bridge. |
  | `tools/test-fuxi-metal-m9l.py` | Metal 轉譯里程碑（M5b～M9p） | `17c9d54` 2026-10-22 | 1 | `--compile-only --translator* --icd --headers --llvm-as` | `docs-lanes/gpu-translation.md`、`docs-lanes/metal-m9l.md` | Compile or run authored M9l lavapipe controls with the separately built bridge. Build the pinned bridge using the existing offline or M9a harness firs |
  | `tools/test-fuxi-metal-m9m.cpp` | Metal 轉譯里程碑（M5b～M9p） | `5827190` 2026-10-23 | 1 | 由 `test-fuxi-metal-m9m.py` 編譯執行 | 無 | Actual Vulkan surface-offset and draw-safety controls; no Apple inputs. |
  | `tools/test-fuxi-metal-m9m.py` | Metal 轉譯里程碑（M5b～M9p） | `5827190` 2026-10-23 | 1 | `--compile-only --icd --headers` | `docs-lanes/gpu-fps.md`、`docs-lanes/metal-m9m.md` | Local M9m Vulkan controls; no phone, shaders, external translator or network. |
  | `tools/test-fuxi-metal-m9n.cpp` | Metal 轉譯里程碑（M5b～M9p） | `e76bf68` 2026-10-23 | 3 | 由 `test-fuxi-metal-m9n.py` 編譯執行 | 無 | Production Vulkan transfers only: no translator, Apple inputs or device. |
  | `tools/test-fuxi-metal-m9n.py` | Metal 轉譯里程碑（M5b～M9p） | `e76bf68` 2026-10-23 | 1 | `--compile-only --icd --headers` | `docs-lanes/gpu-fps.md`、`docs-lanes/metal-m9n.md` | Local M9n Vulkan controls; no phone, shaders, external translator or network. |
  | `tools/test-fuxi-metal-m9o.py` | Metal 轉譯里程碑（M5b～M9p） | `9a5068e` 2026-10-23 | 2 | `--compile-only --icd --headers --translator --llvm-as` | `docs-lanes/gpu-fps.md`、`docs-lanes/gpu-shadow-copies.md`、`docs-lanes/gpu-translation.md` | Local M9o Vulkan controls; optional authored AIR draws with an external bridge. No phone access, extracted shaders, downloads or network. |
  | `tools/test-fuxi-metal-m9p.cpp` | Metal 轉譯里程碑（M5b～M9p） | `9304a93` 2026-10-27 | 1 | 由 `test-fuxi-metal-m9p.py` 編譯執行 | 無 | Production Vulkan 3D sampled textures: allocation, view type, strided uploads, copies and refusals. No translator or device. |
  | `tools/test-fuxi-metal-m9p.py` | Metal 轉譯里程碑（M5b～M9p） | `9304a93` 2026-10-27 | 1 | `--compile-only --icd --headers` | `docs-lanes/metal.md` | Local M9p Vulkan controls; no phone, shaders, external translator or network. |
  | `tools/test-fuxi-metal-pipeline.cpp` | Metal 轉譯里程碑（M5b～M9p） | `9266cb6` 2026-10-21 | 1 | 由 `test-fuxi-metal-pipeline.py` 編譯執行 | 無 | Original integration test: actual AIR translator + actual software Vulkan backend. * GPU behavior here is host-only, not Turnip/A740 device qualificat |
  | `tools/test-fuxi-metal-pipeline.py` | Metal 轉譯里程碑（M5b～M9p） | `9266cb6` 2026-10-21 | 1 | `--headers* --translator* --metallib* --icd` | `docs-lanes/metal-m6.md`、`docs-lanes/metal.md` | Actual external AIR translator + software Vulkan integration, sanitizer/fault tests. Requires our Mac-built metallib, external host translator and Vul |
  | `tools/test-fuxi-metal-ca-offline.cpp` | Metal 快取、效能與正確性 | `b117955` 2026-10-22 | 5 | 由 `test-fuxi-metal-ca-offline.py` 編譯執行 | 無 | Linux-only qualifier: the production Backend and dynamic bridge, local inputs. * No Apple-derived shader products are serialized. stdout is consumed p |
  | `tools/test-fuxi-metal-ca-offline.py` | Metal 快取、效能與正確性 | `b117955` 2026-10-22 | 1 | `library* --source* --output --icd* --headers --toggle-limit --pairs --ca-pool --sweep --timeout --pair-limit` | `docs-lanes/metal-m9f.md` | Qualify local QuartzCore fixed pipelines with OUR bridge + Backend on lavapipe. Requires a clean, external pinned translator and cached Cargo dependen |
  | `tools/test-fuxi-metal-cache-hits.py` | Metal 快取、效能與正確性 | `917eacf` 2026-10-24 | 1 | `--translator* --baseline --headers --icd --llvm-as` | `docs-lanes/gpu-translation.md` | Offline translation hit/capacity tests and host timings; no timing thresholds. The translator must be built from the current bridge and pinned depende |
  | `tools/test-fuxi-metal-cache.py` | Metal 快取、效能與正確性 | `7cc8edc` 2026-10-23 | 1 | `--source --translator --build --headers --icd --llvm-as` | `docs-lanes/gpu-translation.md`、`docs-lanes/metal-gpu-cache.md` | GPU-CACHE: pinned external translator + actual lavapipe pixels across processes. No Apple inputs or device access. Lavapipe does not establish Turnip  |
  | `tools/test-fuxi-metal-defaults.py` | Metal 快取、效能與正確性 | `1c6ccbb` 2026-10-21 | 5 | `--translator* --metallib*` | `docs-lanes/metal-m9e.md`、`docs-lanes/metal.md` | Mac regression with 20H392 public defaults and real external function metadata. |
  | `tools/test-fuxi-metal-draw-plans.py` | Metal 快取、效能與正確性 | `17b007b` 2026-10-26 | 1 | `--translator* --headers --icd --llvm-as` | `docs/fuxi-draw-plans.md` | Pinned external draw-planner differential tests and ring-buffer SubmitBench A/B; no downloads. Build --translator from tools/fuxi-metal2vulkan-bridge. |
  | `tools/test-fuxi-metal-frametrace.py` | Metal 快取、效能與正確性 | `6b599c9` 2026-10-24 | 2 | `--headers --icd` | `docs-lanes/gpu-fps.md`、`docs-lanes/gpu-shadow-copies.md`、`docs-lanes/gpu-translation.md` | Local frame tracing and opt-in scratch reuse controls; no external translator. |
  | `tools/test-fuxi-metal-memory.py` | Metal 快取、效能與正確性 | `293004e` 2026-10-27 | 2 | `--headers --icd` | 無 | Portable production memory pool tests with a local software Vulkan ICD. |
  | `tools/test-fuxi-metal-merge.py` | Metal 快取、效能與正確性 | `6a8c566` 2026-10-25 | 13 | `--headers --icd --glslang --translator --llvm-as` | `docs-lanes/gpu-barriers.md`、`docs-lanes/gpu-fps.md`、`docs-lanes/gpu-translation.md` | Production render-pass merge A/B pixels and 120-draw host benchmark; no downloads. |
  | `tools/test-fuxi-metal-mipfeedback.py` | Metal 快取、效能與正確性 | `9ad03a8` 2026-10-25 | 3 | `--headers --icd --glslang` | `docs-lanes/gpu-fps.md`、`docs-lanes/gpu-translation.md` | ASan/UBSan mip analysis and actual production Backend on local software Vulkan. Uses authored GLSL and the test adapter; no Mac/device/downloads. |
  | `tools/test-fuxi-metal-occlude.py` | Metal 快取、效能與正確性 | `5097542` 2026-10-30 | 5 | `--headers --icd --glslang --spirv-tools` | `docs-lanes/metal.md` | FUXI_MTL_OCCLUDE / FUXI_MTL_NOFETCH_RP / FUXI_MTL_DRAW_AREA / FUXI_MTL_SPIRV_UNROLL / FUXI_MTL_HALF_ROUND: CPU vertex evaluator and proof unit tests,  |
  | `tools/test-fuxi-metal-overlap.py` | Metal 快取、效能與正確性 | `da1149a` 2026-10-25 | 3 | `--headers --icd --glslang` | `docs-lanes/gpu-fps.md`、`docs-lanes/gpu-translation.md`、`docs-lanes/metal.md` | Local Vulkan overlap correctness and 120 sampled draw A/B benchmark. No downloads. |
  | `tools/test-fuxi-metal-shadows.py` | Metal 快取、效能與正確性 | `57f1c45` 2026-10-26 | 11 | `--headers --icd --translator --llvm-as --glslang` | `docs-lanes/gpu-shadow-copies.md` | Shadow snapshot host sanitizers and optional production lavapipe pixels; no downloads. |
  | `tools/test-fuxi-nativemetal-bridge.cpp` | NativeMetal 前端 | `78eb587` 2026-10-23 | 1 | 由 `test-fuxi-nativemetal.py` 編譯執行 | 無 | Original MIT controls against the external patched Rust cdylib, lavapipe. * No shaders/Apple fixtures. CPU-coherent x86 cache flush is a test-only stu |
  | `tools/test-fuxi-nativemetal.py` | NativeMetal 前端 | `78eb587` 2026-10-23 | 1 | `--native* --metal2vulkan* --bridge --icd --compile-only` | `docs-lanes/nativemetal-n1.md` | Linux source/policy checks; Mac wrapper and actual native frontend tests. Source paths must be disposable PINNED external checkouts. Tests apply patch |
  | `tools/test-fuxi-mesa-ir3.py` | Turnip／Mesa | `1e537c2` 2026-10-23 | 1 | `--mesa* --patches*` | `docs-lanes/mesa-port.md` | Compile actual pinned ir3 rounding/offset blocks against a small host model. External Mesa only; verifies/applies the recipe without building Mesa. No |
  | `tools/test-fuxi-vulkan.py` | Turnip／Mesa | `8ceb7c0` 2026-10-20 | 1 | `--headers*` | `docs-lanes/kernel-h1.md`、`docs-lanes/mesa-port.md`、`docs-lanes/metal.md` | Exercise probe lifetime/no-submit protocol against a fake ICD and offline staging. Requires --headers pointing to an external Mesa include/ directory; |
  | `tools/test-fuxi-iig-cache.py` | IIG 預產物快取 | `062be50` 2026-10-30 | 1 | 無 | 無 | Host test: IIG pregen capture/verify/replay refuses every mismatch (synthetic inputs only). |
  | `tools/test-ios27-prelink.py` | iOS 27 工具 | `842c423` 2026-10-31 | 1 | 無 | 無 | Host tests for tools/ios27-prelink.py (K2, docs-lanes/ios27.md#k2): profile roots, preflight refusals, the check-prelink-xml lexer rules and the struc |
  | `tools/test-ios27-root.py` | iOS 27 工具 | `344b450` 2026-10-31 | 1 | 無 | `docs-lanes/ios27-userland.md` | Host tests for the iOS 27 userland tools: apfs-ro.py decoders, ios27-root.py (IM4P, trust caches, cdhash, windows image, Data staging), ios27-mockfs.p |
  | `tools/test-fuxi-thermal-summary.py` | 冷卻與 launchd | `2fb7b97` 2026-10-23 | 1 | 無 | `docs-lanes/cooling.md` | Host checks for fuxi-thermal-summary.py on synthetic monitor output. |
  | `tools/test-launchd-on-demand.py` | 冷卻與 launchd | `ae1154e` 2026-10-23 | 1 | 無 | `docs-lanes/cooling.md` | Host checks for launchd-on-demand.py on a synthetic launchd cache. |

  其餘與上游工具相連的測試：

  | 入口（ksrc tools/） | 涵蓋內容 | 原始文件 |
  | --- | --- | --- |
  | test-fuxi-metal-artifacts.py、test-fuxi-metal-stage.py | 核心及 boot 封裝、以簽署產物核對暫存 root | docs-lanes/metal.md、docs-lanes/nativemetal-n1.md |
  | test-fuxi-metal-backend.py、test-fuxi-metal-translation.py | M4 軟體 Vulkan ICD、AIR 轉譯、stage 重用與計時 | docs-lanes/gpu-translation.md、docs-lanes/metal-gpu-cache.md |
  | test-fuxi-metal-m6.cpp、test-fuxi-metal-m9o.cpp | Vulkan transfer/host import、緩衝區一致性；由同里程碑 Python 入口編譯 | 各 C++ 檔首、對應 Python 入口 |
  | test-fuxi-turnip-hostimport.py、test-fuxi-turnip-submit.py | 釘選 Turnip 的 import/free 與 Darwin KMD wait/submit ABI 模型 | docs-lanes/metal-hostimport.md、docs-lanes/turnip-t1.md |
  | test-fuxi-patch-stack.py、test-fuxi-vk-smoke.py | 建置補丁清單與軟體 ICD 上的失敗案例 | docs-lanes/unixnu-port.md、測試檔首 |
  | test-fuxi-memory.py、test-fuxi-ios27.py | 離線記憶體配置檢查與 iOS 27 lane | docs-lanes/gpu.md、docs-lanes/ios27.md |

- **復現**：
  1. 取得 ksrc：見 README 共用步驟 1（`work/adsp-battery/ksrc`，或公開倉庫 fuxi-ios 依 `upstream/EXPORT-SOURCES.txt` 的 `kernel-base` 取出）。所有檔案都在 `tools/` 下，在 ksrc 根目錄執行。
  2. 只需 Python 標準函式庫、可直接跑的：`python3 tools/test-fuxi-thermal-summary.py`、`python3 tools/test-launchd-on-demand.py`、`python3 tools/test-ios27-root.py`、`python3 tools/test-ios27-prelink.py`、`python3 tools/test-fuxi-iig-cache.py`（皆用合成輸入）。
  3. 需要 Vulkan 標頭與軟體 Vulkan（lavapipe ICD）的：`--headers <Vulkan-Headers 的 include 目錄>`、`--icd <lvp_icd.x86_64.json>`；用 GLSL 的加 `--glslang <glslangValidator>`；用 AIR 的加 `--llvm-as <llvm-as>`。例：`python3 tools/test-fuxi-metal-m9m.py --headers <vk-include> --icd <lvp_icd.json>`（無轉譯器），`python3 tools/test-fuxi-metal-overlap.py --glslang <glslangValidator>`。
  4. 需要外部轉譯 bridge 的（`--translator`／`--source`／`--build`）：先依上游 `docs-lanes/metal-m9a.md`／`gpu-translation.md` 從釘選的 metal2vulkan 原始碼建 `libmetal2vulkan.so`，再傳 `--translator <libmetal2vulkan.so>`。例：`python3 tools/test-fuxi-metal-m9g.py --translator "$HOST_TRANSLATOR"`、`python3 tools/test-fuxi-metal-cache.py --source "$M2V_SOURCE" --build "$BRIDGE_BUILD"`。
  5. 需要 Mac 產的 metallib 或 Apple 原廠函式庫的（`--metallib`、`library`）：`test-fuxi-metal-defaults.py`、`test-fuxi-metal-pipeline.py`、`test-fuxi-metal-m6b.py`、`test-fuxi-metal-m7.py`、`test-fuxi-metal-ca-offline.py`；輸入檔不在任何倉庫內（上游設計如此），要自備。
  6. Turnip／Mesa：`python3 tools/test-fuxi-vulkan.py --headers "$MESA_SRC/include"`、`python3 tools/test-fuxi-mesa-ir3.py --mesa "$MESA_SRC" --patches "$PATCH_DIR"`（見上游 `docs-lanes/mesa-port.md`）。
  7. NativeMetal：`python3 tools/test-fuxi-nativemetal.py --native <釘選 NativeMetal> --metal2vulkan <釘選 metal2vulkan> --bridge <libnative_metal>`；harness `native-metal-host-tests.yml` 有完整的取源與建置步驟（T-059）。
  8. `.cpp` 檔不單獨執行，由表中同名 `.py` 編譯後執行。
- **主機測試**：本項即主機測試本身；全部只在主機執行，不碰手機。
- **驗證紀錄**：本專案沒有重跑這些測試（2026-11-18 收錄只做靜態盤點：檔案、加入提交、修改次數、參數與文件）。上游的通過紀錄在各 `docs-lanes/*.md`。用到同一批元件的本專案實機結果見 G 系列（G-002～G-006 的 NativeMetal／metal2vulkan 補丁、G-017 起的 m2v 補丁部署）。
- **參考來源**：上游 zgl97/ios-fuxi `docs-lanes/`（`metal*.md`、`gpu-*.md`、`mesa-port.md`、`nativemetal-n1.md`、`ios27-userland.md`、`cooling.md`、`kernel-h1.md`）；表中「上游文件」欄。
- **結論／未完成**：原樣保留，不改、不刪。要在本專案重新驗證 Metal 路線時，依第 3～7 步補齊依賴後再跑；屆時把結果補進本項「驗證紀錄」。
- **出處**：`ksrc: tools/`（表中 50 個檔案）；`git -C work/adsp-battery/ksrc log --diff-filter=A -- <檔案>`；`ksrc: docs-lanes/`。（2026-11-18 主代理逐檔盤點撰寫）

### T-059 harness GPU 時期的 CI 與主機測試（2026-11-08～11-14）

- **日期**：2026-11-08 20:21（`frontend.yml`）～2026-11-14 22:56（`kernel-backlight-dsi.yml`）
- **狀態**：已完成、留作紀錄；日常 CI 已改用 `kernel-adsp-battery.yml` 系列（T-053），GPU 時期的 17 份核心補丁也已併進 ksrc `979e2f08..eafc778`（`75148d2` 一次併入 harness `2aae087` 當時的補丁，`3277d2c`、`6fc377f`、`eafc778` 再依序補 `kernel-ibprof-draws`、`kernel-taps-page1`、`kernel-backlight-dsi`；README 共用步驟 2）
- **開關**：workflow 都支援 `workflow_dispatch`；`kernel-bo-slots-check.yml` 另在 push 到 `codex/kernel-bo-slots`、`kernel-backlight-dsi.yml` 在 push 到 `codex/backlight-dsi` 時觸發（依 `paths` 過濾）。被測補丁的開機參數見各 G 項目（如 `fuxiperf=2`、`sweetsurfrow`、`sweetprof`、`fuxitapcmd`、`fuxibacklight=4`）
- **問題／現象**：GPU 調校期間（G-002～G-029）需要在 GitHub macOS runner 上用 Apple clang 建置 NativeMetal 與實驗核心，並在套補丁的每一步做主機檢查；這些 workflow、測試與 40 個 harness 提交當時只記在 G 系列的部署紀錄裡，沒有逐檔清單。
- **根因**：NativeMetal 前端與 iOS 16 Metal 函式庫只能用 Xcode 工具鏈編；核心實驗要和 CI 產物逐趟對照。每加一個實驗補丁就在同一個 workflow 追加一份 `patches/*.patch` 與一個 `tests/test-*.py`。
- **做法**：
  - workflow（8 個，全部 `runs-on: macos-15`）：
  | workflow | 加入提交 | 修改次數 | 觸發 | 內容 | 對應 |
  | --- | --- | --- | --- | --- | --- |
  | `.github/workflows/frontend.yml` | `9b7045d` 2026-11-08 | 1 | 手動（`workflow_dispatch`） | 取上游 `4b8faded` 後跑 `python3 tools/test-fuxi-metal-frontend.py`（FuxiMetal 前端與模型測試） | G-002 |
  | `.github/workflows/native-metal-check.yml` | `6a5e80d` 2026-11-08 | 9 | 手動 | 取上游 `4b8faded`，`git apply` `native-patch-stack`、`fuxi-copy-span-builder` 後跑 `test-native-patch-stack.py source`，再 `git apply` `astc-pixel-smoke`、`native-smoke-class`、`native-ca-smoke-class`，把 `nativemetal-fuxi-0004`～`0010` 與 `metal2vulkan-fuxi-0001` 複製到 `source/tools/patches/`（建置時由上游建置腳本套用，共 13 份），用 Xcode 建置 iOS 16 NativeMetal 並稽核，上傳產物與紀錄 | G-002 |
  | `.github/workflows/native-metal-host-tests.yml` | `8d6b0c3` 2026-11-08 | 7 | 手動 | 同上取源與套補丁後建主機 bridge，跑 `tools/test-fuxi-metal-frontend.py`、`tools/test-fuxi-nativemetal.py`、`NativePortTests.mm`、`FuxiNocopyObservationTests.cpp`（Mac 與 `arm64-apple-ios16.0` 各編一次） | G-002 |
  | `.github/workflows/nocopy-observation-check.yml` | `3516a8e` 2026-11-09 | 1 | 手動 | 從 `nativemetal-fuxi-0010-nocopy-provenance.patch` 取出觀察用標頭，`xcrun clang++` 編 `FuxiNocopyObservationTests.cpp` 等並執行，另以 iOS SDK 交叉編譯 | G-005 |
  | `.github/workflows/unwind-register-check.yml` | `4912789` 2026-11-09 | 5 | 手動 | 編譯並執行 `UnwindRegisterTests.cpp`、`NocopyObjectMetadataTests.cpp`、`NocopyMachOUUIDTests.cpp`、`NocopySafeReadTests.cpp`、`NocopyDeviceControls.cpp`；iOS 版用 `xcrun vtool -show-build` 核對部署版本 | G-005 |
  | `.github/workflows/kernel-surface-check.yml` | `a6fafa6` 2026-11-09 | 1 | 手動 | 取 ksrc 基底 `979e2f08`，套 `kernel-surface-stride.patch`，跑 `test-surface-plane-result.py`，建置並稽核核心 | G-019 |
  | `.github/workflows/kernel-bo-slots-check.yml` | `6143d1f` 2026-11-13 | 15 | 手動或 push 到 `codex/kernel-bo-slots`（依 `paths`） | 取 `979e2f08`，依序套 16 份 `kernel-*.patch`（見下表）並穿插 `test-bo-slots.py`、`test-surfrow-mode2.py`、`test-perf-governor.py`、`test-ib-profile.py`、`test-surface-plane-result.py`，建置稽核核心並上傳 | G-014 |
  | `.github/workflows/kernel-backlight-dsi.yml` | `795e97a` 2026-11-14 | 1 | 手動或 push 到 `codex/backlight-dsi` | 同 `kernel-bo-slots-check.yml` 再加 `kernel-backlight-dsi.patch` 與 `test-backlight-dsi.py`（共 17 份補丁；ksrc 中對應 `979e2f08..eafc778`） | G-029 |
  - 補丁（30 份；`native-*`／`nativemetal-*`／`metal2vulkan-*`／`astc-*`／`fuxi-copy-span-builder` 套在上游 `4b8faded` 的使用者空間來源，`kernel-*` 套在 ksrc 基底 `979e2f08`）：
  | 補丁 | 加入提交 | 碰過的提交 | 大小（位元組） | 對應 |
  | --- | --- | --- | --- | --- |
  | `patches/astc-pixel-smoke.patch` | `984e0ef` 2026-11-09 | `4506b26` `984e0ef` | 10724 | G-002 |
  | `patches/fuxi-copy-span-builder.patch` | `984e0ef` 2026-11-09 | `984e0ef` | 2158 | G-006 |
  | `patches/kernel-backlight-dsi.patch` | `795e97a` 2026-11-14 | `795e97a` | 59320 | G-029、G-048、K-017 |
  | `patches/kernel-bo-slots.patch` | `6143d1f` 2026-11-13 | `6143d1f` | 1787 | G-014 |
  | `patches/kernel-ib-profile.patch` | `1b75b23` 2026-11-14 | `29df71b` `f476117` `1b75b23` | 38131 | G-019 |
  | `patches/kernel-ibprof-draws.patch` | `f56f8a5` 2026-11-14 | `f56f8a5` | 17320 | G-027 |
  | `patches/kernel-input-fixes.patch` | `2ffeeeb` 2026-11-14 | `2ffeeeb` | 4489 | G-025、G-027 |
  | `patches/kernel-perf-governor.patch` | `9b9ca88` 2026-11-13 | `9b9ca88` | 17054 | G-018 |
  | `patches/kernel-perf-wide.patch` | `29df71b` 2026-11-14 | `29df71b` | 3963 | G-047 |
  | `patches/kernel-prof-edges.patch` | `407c8ba` 2026-11-14 | `9bbac97` `407c8ba` | 9200 | G-027 |
  | `patches/kernel-prof-inclusive-wait.patch` | `b7f462b` 2026-11-14 | `b7f462b` | 8556 | G-027 |
  | `patches/kernel-prof-inclusive.patch` | `63cd106` 2026-11-14 | `97358bc` `63cd106` | 10595 | G-027 |
  | `patches/kernel-prof-live.patch` | `4b04bcc` 2026-11-14 | `2239377` `4b04bcc` | 13160 | G-027 |
  | `patches/kernel-surface-stride.patch` | `a6fafa6` 2026-11-09 | `a6fafa6` | 2745 | G-019 |
  | `patches/kernel-surfrow-mode2.patch` | `bd72669` 2026-11-13 | `bd72669` | 4105 | G-016 |
  | `patches/kernel-swipe-pacing.patch` | `0c3bfad` 2026-11-14 | `0c3bfad` | 2304 | G-027 |
  | `patches/kernel-taps-page1.patch` | `0b71826` 2026-11-14 | `0b71826` | 4208 | G-028 |
  | `patches/kernel-taps-page2.patch` | `2aae087` 2026-11-14 | `2aae087` | 2623 | G-027 |
  | `patches/kernel-taps-presets.patch` | `67a88bf` 2026-11-14 | `67a88bf` | 2480 | G-027 |
  | `patches/metal2vulkan-fuxi-0001-copy-span.patch` | `984e0ef` 2026-11-09 | `984e0ef` | 3931 | G-006 |
  | `patches/native-ca-smoke-class.patch` | `8d70628` 2026-11-09 | `8d70628` | 1234 | G-002 |
  | `patches/native-patch-stack.patch` | `e0bd223` 2026-11-08 | `e0bd223` | 3265 | G-002 |
  | `patches/native-smoke-class.patch` | `8be95c8` 2026-11-09 | `8be95c8` | 1507 | G-002 |
  | `patches/nativemetal-fuxi-0004-optional-answers.patch` | `4642ce5` 2026-11-09 | `4642ce5` | 2055 | G-003 |
  | `patches/nativemetal-fuxi-0005-completion-handlers.patch` | `4642ce5` 2026-11-09 | `4642ce5` | 4155 | G-003 |
  | `patches/nativemetal-fuxi-0006-uchar3-constant.patch` | `f6b9872` 2026-11-09 | `f6b9872` | 1058 | G-003 |
  | `patches/nativemetal-fuxi-0007-nocopy-diagnostics.patch` | `d141d68` 2026-11-09 | `d141d68` | 2015 | G-005 |
  | `patches/nativemetal-fuxi-0008-nocopy-callers.patch` | `984e0ef` 2026-11-09 | `984e0ef` | 1498 | G-005 |
  | `patches/nativemetal-fuxi-0009-copy-span.patch` | `984e0ef` 2026-11-09 | `984e0ef` | 7067 | G-006 |
  | `patches/nativemetal-fuxi-0010-nocopy-provenance.patch` | `94d5818` 2026-11-09 | `7a06fe8` `8091432` `94d5818` | 11921 | G-005 |
  - 測試（17 個）：
  | 測試 | 加入提交 | 碰過的提交 | 由哪個 workflow 執行 | 對應 |
  | --- | --- | --- | --- | --- |
  | `tests/FuxiNocopyObservationTests.cpp` | `94d5818` 2026-11-09 | `7a06fe8` `94d5818` | `native-metal-host-tests.yml`、`nocopy-observation-check.yml` | G-005 |
  | `tests/NativePortTests.mm` | `4642ce5` 2026-11-09 | `3b35485` `984e0ef` `d141d68` `f6b9872` `4642ce5` | `native-metal-host-tests.yml` | G-003 |
  | `tests/NocopyDeviceControls.cpp` | `470e017` 2026-11-09 | `470e017` | `unwind-register-check.yml` | G-005 |
  | `tests/NocopyMachOUUID.h` | `3d120d2` 2026-11-09 | `3d120d2` | （標頭，被同名 `*Tests.cpp` 引入） | G-005 |
  | `tests/NocopyMachOUUIDTests.cpp` | `3d120d2` 2026-11-09 | `3d120d2` | `unwind-register-check.yml` | G-005 |
  | `tests/NocopyObjectMetadata.h` | `449ec9a` 2026-11-09 | `7a06fe8` `449ec9a` | （標頭，被同名 `*Tests.cpp` 引入） | G-005 |
  | `tests/NocopyObjectMetadataTests.cpp` | `449ec9a` 2026-11-09 | `7a06fe8` `449ec9a` | `unwind-register-check.yml` | G-005 |
  | `tests/NocopySafeRead.h` | `20e9aa7` 2026-11-09 | `3d120d2` `20e9aa7` | （標頭，被同名 `*Tests.cpp` 引入） | G-005 |
  | `tests/NocopySafeReadTests.cpp` | `20e9aa7` 2026-11-09 | `470e017` `3d120d2` `20e9aa7` | `unwind-register-check.yml` | G-005 |
  | `tests/UnwindRegisterTests.cpp` | `4912789` 2026-11-09 | `470e017` `d4fbcbd` `4912789` | `unwind-register-check.yml` | G-005 |
  | `tests/test-backlight-dsi.py` | `795e97a` 2026-11-14 | `77940d8` `795e97a` | `kernel-backlight-dsi.yml` | G-029、K-017 |
  | `tests/test-bo-slots.py` | `6143d1f` 2026-11-13 | `6143d1f` | `kernel-bo-slots-check.yml`、`kernel-backlight-dsi.yml` | G-014 |
  | `tests/test-ib-profile.py` | `1b75b23` 2026-11-14 | `1b75b23` | `kernel-bo-slots-check.yml`、`kernel-backlight-dsi.yml` | G-019 |
  | `tests/test-native-patch-stack.py` | `e0bd223` 2026-11-08 | `e0bd223` | `native-metal-check.yml`、`native-metal-host-tests.yml` | G-002 |
  | `tests/test-perf-governor.py` | `9b9ca88` 2026-11-13 | `9b9ca88` | `kernel-bo-slots-check.yml`、`kernel-backlight-dsi.yml` | G-018 |
  | `tests/test-surface-plane-result.py` | `a6fafa6` 2026-11-09 | `a6fafa6` | `kernel-surface-check.yml`、`kernel-bo-slots-check.yml`、`kernel-backlight-dsi.yml` | G-019 |
  | `tests/test-surfrow-mode2.py` | `bd72669` 2026-11-13 | `bd72669` | `kernel-bo-slots-check.yml`、`kernel-backlight-dsi.yml` | G-016 |
  - harness 提交（GPU 時期 40 個，未在其他項目引用的部分）：
  | 提交 | 時間（台北） | 說明（原文） | 對應 |
  | --- | --- | --- | --- |
  | `9b7045d` | 2026-11-08 20:21 | Add pinned FuxiMetal frontend verification workflow | G-002 |
  | `6a5e80d` | 2026-11-08 22:40 | Add NativeMetal build validation | G-002 |
  | `8d6b0c3` | 2026-11-08 22:46 | Add NativeMetal host integration tests | G-002 |
  | `e0bd223` | 2026-11-08 22:54 | Fix patch stack checks for added files | G-002 |
  | `8be95c8` | 2026-11-09 00:21 | Allow NativeMetal device in smoke discovery | G-002 |
  | `4642ce5` | 2026-11-09 09:18 | Repair NativeMetal capability and completion handling | G-003 |
  | `8d70628` | 2026-11-09 10:01 | Allow NativeMetal in Core Animation diagnostic | G-002 |
  | `f6b9872` | 2026-11-09 11:05 | Support uchar3 function constants | G-003 |
  | `a6fafa6` | 2026-11-09 13:48 | Fix IOSurface plane stride result | G-019 |
  | `d141d68` | 2026-11-09 15:21 | Diagnose raw no-copy texture requests | G-005 |
  | `984e0ef` | 2026-11-09 18:33 | Diagnose no-copy textures and validate compressed uploads | G-002、G-005、G-006 |
  | `4506b26` | 2026-11-09 18:45 | Update smoke stage boundary host checks | G-002 |
  | `4912789` | 2026-11-09 21:15 | Check caller register recovery on macOS | G-005 |
  | `d4fbcbd` | 2026-11-09 21:22 | Distinguish saved caller register from callee value | G-005 |
  | `449ec9a` | 2026-11-09 21:26 | Validate bounded no-copy metadata identification | G-005 |
  | `20e9aa7` | 2026-11-09 21:30 | Test guarded self-memory reads for diagnostics | G-005 |
  | `3d120d2` | 2026-11-09 21:38 | Use iOS VM interface for bounded diagnostic reads | G-005 |
  | `470e017` | 2026-11-09 21:40 | Build synthetic iOS provenance controls | G-005 |
  | `94d5818` | 2026-11-09 22:32 | Add bounded no-copy provenance observation | G-005 |
  | `3516a8e` | 2026-11-09 22:33 | Run bounded observation controls independently | G-005 |
  | `3b35485` | 2026-11-09 22:36 | Test refusal for unrecognized no-copy caller | G-005 |
  | `8091432` | 2026-11-09 23:02 | Restrict diagnostic output to known metadata | G-005 |
  | `7a06fe8` | 2026-11-09 23:47 | Report bounded provenance validation failures | G-005 |
  | `6143d1f` | 2026-11-13 14:46 | Build a diagnostic kernel with 4096 BO slots | G-014 |
  | `bd72669` | 2026-11-13 17:03 | Default sweetsurfrow to mode 2 in the diagnostic kernel | G-016 |
  | `9b9ca88` | 2026-11-13 23:24 | Add opt-in governor diagnostics to the diagnostic kernel | G-018 |
  | `1b75b23` | 2026-11-14 01:08 | Add an opt-in per-IB GPU profile to the diagnostic kernel | G-019 |
  | `f476117` | 2026-11-14 01:17 | Count event writes and CP waits in the per-IB profile | G-019 |
  | `29df71b` | 2026-11-14 02:53 | Classify blits in the per-IB profile; fuxiperf=2 for the wide governor defaults | G-019、G-047 |
  | `4b04bcc` | 2026-11-14 05:50 | Sample live user registers of on-core threads in sweetprof | G-027 |
  | `2239377` | 2026-11-14 06:11 | Flat pc-bucket profile of the live sweetprof samples | G-027 |
  | `63cd106` | 2026-11-14 13:23 | Inclusive live profile of CoreAnimation's display thread in sweetprof (PROFINC) | G-027 |
  | `97358bc` | 2026-11-14 13:26 | PROFINC also covers the profiled task's main thread (SpringBoard with sweetprof=10000+N) | G-027 |
  | `0c3bfad` | 2026-11-14 13:33 | Synthetic swipes step on an absolute 8 ms grid (the relative sleep drifted to ~10.5 ms, ~95 Hz input) | G-027 |
  | `b7f462b` | 2026-11-14 13:51 | PROFINC also counts the off-core (waiting / runnable) samples of the same threads, by class | G-027 |
  | `407c8ba` | 2026-11-14 14:01 | PROFEDGE: caller edges of the PROFINC samples | G-027 |
  | `9bbac97` | 2026-11-14 14:02 | PROFINC/PROFEDGE keep exact return addresses (a 64-byte bucket can start in the previous function) | G-027 |
  | `67a88bf` | 2026-11-14 15:01 | fuxitapcmd=@1/@2: built-in scripted loads (a Settings session; the slow home drag) that do not fit the boot-args field | G-027 |
  | `2ffeeeb` | 2026-11-14 16:18 | Scripted taps report every 8 ms while held (a lone down/up opened no app); touch sensing starts after 10 s by default | G-025、G-027 |
  | `0b71826` | 2026-11-14 21:29 | fuxitapcmd=@3: open Settings from home page 1 (row 2 col 1), the default layout the root now has | G-028 |
- **復現**：
  1. harness 倉庫取 GPU 時期狀態：`git -C work/adsp-battery/harness checkout 0b71826`（`kernel-bo-slots-check.yml` 最後一次修改）或 `795e97a`（加上背光）。
  2. CI：在 GitHub Actions 手動觸發對應 workflow（或推到上表的分支）；產物名與上傳步驟見各 workflow 的 `Upload ...` 步驟。
  3. 本機重現核心層：GPU 時期的 17 份 `kernel-*.patch` 已併進 ksrc `979e2f08..eafc778`，直接照 README 共用步驟 1～3 建置即可得到相同的補丁層；`git -C work/adsp-battery/ksrc log --oneline 979e2f08..eafc778` 列出這 4 個提交，`git -C work/adsp-battery/ksrc show 75148d2 --stat` 看一次併入的範圍。
  4. 主機測試單獨跑（在 harness 根目錄）：`python3 tests/test-bo-slots.py <已套補丁的 ios-fuxi 原始碼目錄>`（`test-surface-plane-result.py`、`test-surfrow-mode2.py`、`test-perf-governor.py`、`test-ib-profile.py`、`test-backlight-dsi.py` 用法相同，CI 裡就是 `python3 harness/tests/<名稱> source`；`test-native-patch-stack.py` 吃 `4b8faded` 的使用者空間來源）；C++ 測試照上表 workflow 裡的 `xcrun clang++ -std=c++17 -O2 -Wall -Wextra -Werror tests/<名稱>.cpp -o <輸出>` 編譯後執行，需 macOS。
- **主機測試**：本項即主機測試；全部在 runner 或主機執行，不碰手機。
- **驗證紀錄**：各 workflow 的 run 結果與實機結果記在對應 G 項目（如 G-014 的 4096 BO slots、G-019 的 IB profile、G-027 的 PROFINC／PROFEDGE、G-029 的背光）；本項只做清單，2026-11-18 收錄時沒有重跑。
- **參考來源**：harness `.github/workflows/`、`patches/`、`tests/`；上游 zgl97/ios-fuxi（`4b8faded`、`979e2f08`）。
- **結論／未完成**：GPU 時期 CI 保留在 harness 倉庫，不再更新；新的核心實驗走 `kernel-adsp-battery.yml` 與本機建置（T-051、T-053）。
- **出處**：`git -C work/adsp-battery/harness log -- .github/workflows patches tests`；上表各檔。（2026-11-18 主代理逐檔盤點撰寫）

### T-060 修復紀錄庫的維護、驗證與匯出

- **日期**：2026-11-18。
- **狀態**：工作區使用，公開版由匯出工具產生。
- **開關**：`records/tools/build-catalog.py --check` 唯讀檢查。`work/repo-export/export-fuxi-ios.py --verify` 比對已提交的匯出，`--check` 掃描 clone。
- **問題／現象**：主機測試、補丁、趟次與結論原先分散在多個倉庫和工作目錄，復現時需要反覆搜尋。
- **根因**：工作區、harness 與 ksrc 各有獨立歷史，原始檔名不足以辨別採用版本、失敗試驗與上游經驗。
- **做法**：`records/items` 用固定編號追蹤修復。`records/map.tsv` 補明確的檔案、測試與開關對應。產生器從三棵樹重建索引。`records/journal/sources.tsv` 留下原始檔與去識別化副本的 SHA-256 和行數。
- **復現**：在私有工作區執行 `python -X utf8 records/tools/build-catalog.py`，確認 `CATALOG_OK`。加 `--check` 驗證最新來源，然後只提交本輪相關檔案。公開倉庫用 `export-fuxi-ios.py` 從已提交內容匯出，提交匯出 clone 後依 AGENTS.md 用 `--verify` 和 `--check` 核對。
- **主機測試**：用目錄檢查驗證編號、來源欄位、日誌副本 SHA-256/行數、目錄對應與識別字串。公共 clone 另做 manifest 與掃描。
- **驗證紀錄**：總索引列出本次產生的各類筆數。歷史趟次、輸入和原始日誌出處見日誌索引。
- **參考來源**：[總索引](../README.zh-TW.md) · [日誌索引](../journal/README.zh-TW.md)
- **結論／未完成**：日誌副本給閱讀與回查行號使用。原始 binary、console、mtdblk、映像與分區備份循各條目的原路徑查驗。
- **出處**：`AGENTS.md`、`records/tools/build-catalog.py`、`records/map.tsv`、`work/repo-export/export-fuxi-ios.py`。

### T-061 Option B 無 DRM（`cryptid=0`）20+1 靜態預檢／側載指南／對稱卸載工具鏈、`launch-loader-check.ps1` 擴充、`prepare-aio-loader.py` 升級與磁碟空間歸檔評估

- **日期**：2026-11-26（台北時間）
- **狀態**：Option B 20+1 靜態預檢／側載指南／對稱卸載工具鏈與 `out-slot7der-b0` 合規 `0xB0` 雙端實機驗證閉環（`#462`/`#463`＋`stage-b1-remove-slot7der-b0-1126`）、`launch-loader-check.ps1` `-InstallApp`/`-RemoveApp` 擴充、`prepare-aio-loader.py` 正式升級（`79,889 B`, `9eca8290...`）、`LEAKS` 清障（`verify_b022_loaders.py:24`、`appstore-e2e-1123\`、`test-fuxi-net-trace.py`）與 `DISK_RECLAIM_CANDIDATES_1126.md` / `DISK_RECLAIM_ASSESSMENT_1126.md` 均已就緒；B2 沙盒容器實作待續
- **開關**：`check-nodrm-ipa.py [--json] [--strict-webkit] [--strict-b1-naming] [--fail-on-der-only] [--self-test]`；`resign-app-adhoc.py build|verify`；`launch-loader-check.ps1 -InstallAppDir <DIR> -Out <^[a-z0-9-]+$>` 與 `-RemoveAppName <APP> -RemoveAppBundleId <BUNDLE_ID> -Out <^[a-z0-9-]+$>`（對應 `phone-readthrough.py`）；`work/adsp-battery/prepare-aio-loader.py`（淘汰 `--gpu-hyb8-ovsh`、預設 `--probe-source None`、新增 `--user-app-noplatform 0|1` 與 opt-in `--gpu-c1-bos 0|1024..4096`）；`verify_b022_loaders.py [--loaders-only | --negative-only]`；`remove-app-guest.py self-test`
- **問題／現象**：在 R4 商業 App Store（`cryptid=1`）遇 Apple SEP/GID/UID 硬體邊界（`SAPSign -42036 -> http=401`）下，需將生態轉向合法無加密（`cryptid=0`）側載工具鏈（Option B），防範未經檢查 ZIP 送入 `resign-app-adhoc.py:260-261` 之安全風險，並補齊開過 iOS 後因 `PRE_MISMATCH` 無法回寫 `sb-undo-list.txt` 時之對稱 HFS+ 安全卸載機制、`prepare-aio-loader.py` Wave 2 / `U-026` 統一入口防退步，以及解決 `D:\` 空間逼近紅線的無損歸檔盤點與評估。
- **根因**：既有 `U-012` 缺乏針對 `cryptid=0`、plain `arm64`、`Path X`、ZIP Central Directory/LFH 一致性與 Slot 5 XML vs Slot 7 DER（CE v1 `0xB0` + UTF-8 字典序鍵排序）的獨立靜態閘門，且缺乏 7 層系統 App 防誤刪保護；主線 `prepare-aio-loader.py` 尚含舊版 `--gpu-hyb8-ovsh` 與本機路徑假設；而多輪 `stage-*` `read-cache` 與 `sb-{new,old}.bin` 累積造成 `D:\` 剩餘空間降破 `21 GiB`。
- **做法**：
  1. **Option B 20+1 份離線預檢與側載指南（`work/localbuild/nodrm-apps-1126/`）**：建置 `check-nodrm-ipa.py`（`109,238 B`, `2,558 L`, 9 種互斥 Verdict，強制 CE v1 `0xB0`＋UTF-8 鍵排序）、`test_check_nodrm_ipa.py`（`75,428 B`, `1,607 L`, 33 項）、`b1-test-app/` 雙變體與合規 `slot7der-b0`（`FuxiNoDrmVerify-slot5xml.ipa`、`slot7der.ipa` 歷史對照、`FuxiNoDrmVerify-slot7der-b0.ipa` 與 `out-slot7der-b0/`）、`test_b1_nodrm.py`（`40,577 B`, `954 L`, 10 項）、`NODRM_CONTAINER_B2_SPEC.md`（B2 沙盒規格，待實作）與 `NODRM_SIDELOAD_GUIDE.zh-TW.md`（`71,598 B`, `499 L`, SHA-256 `28FFC5CCB6AB7C5B7C294F9EC66B9D1F3E07E48473903F19C24CC5130656BC70`）。
  2. **`launch-loader-check.ps1` 擴充與對稱安全卸載閉環**：擴充 `launch-loader-check.ps1`（`-InstallAppDir` / `-RemoveAppName` / `-RemoveAppBundleId`）與 `phone-readthrough.py`；於 `work/statusbar-fix/` 實作 Guest 端 `remove-app-guest.py`（`50,490 B`, `887 L`, pure LF, SHA-256 `01920ee079f6...`；7 層縱深防誤刪保護：`52` 項顯式系統保護集、`com.apple.*` 封鎖、rootfs staged 比對、非 symlink、`Info.plist` 比對、保留 `LaunchDaemons` 與 `IconState.plist` 不變）及 `stage-remove-app.sh`（`3,172 B`, `71 L`, pure LF, SHA-256 `7c350938a9d3...`）。
  3. **`prepare-aio-loader.py` 主線升級與 `LEAKS` 清障**：於 Stage E-1 將升級候選正式升格覆寫 `work/adsp-battery/prepare-aio-loader.py`（`79,889 B`, `1,629 L`, SHA-256 `9eca8290231781a653034142076c526c57e622f99ed073d146a7aee874fdfbf8`：淘汰 `--gpu-hyb8-ovsh` 並納入 `BANNED_WAVE1_TOKENS`、預設 `--probe-source None`、移除 `EXPECTED_PROBE_SHA256` 與硬編碼路徑假設、補 `--user-app-noplatform` 與 opt-in `--gpu-c1-bos`），完成 `verify_b022_loaders.py:24` 去本機路徑化與 `appstore-e2e-1123` 序號清障。
  4. **磁碟空間盤點與無損歸檔評估**：產出 `work/localbuild/stage-e-prep-1126/DISK_RECLAIM_CANDIDATES_1126.md`（截點 `14:05`，盤點 11 項已結案／取代／回滾 `stage-*` 可無損搬移 `read-cache + sb-{new,old}.bin` 釋放 `6,408,896,512 B` / `5.9688 GiB`、原位保留 `487.11 MiB` 審計殼，並嚴格保護進行中證據、回退基準、Junction 與 App Store 隔離區），並由 Stage E-3 `DISK_RECLAIM_ASSESSMENT_1126.md` 複核更新至 `19:06`（`D:\` `20.12 GiB`）確認 Batch 1（`5.9688 GiB`）與 Batch 2 延伸候選。
- **復現**：
  1. Option B 離線自測：`PYTHONDONTWRITEBYTECODE=1` 下執行 `python -X utf8 -B work\localbuild\nodrm-apps-1126\b1-test-app\test_b1_nodrm.py`、`python -X utf8 -B -m unittest discover -s work\localbuild\nodrm-apps-1126 -p "test_check_*.py" -v`、`python -X utf8 -B work\localbuild\nodrm-apps-1126\check-nodrm-ipa.py --self-test`。
  2. WSL 卸載測試：`sh -n` / `bash -n` / `dash -n work/statusbar-fix/stage-remove-app.sh` 與 `python3 -X utf8 -B work/statusbar-fix/remove-app-guest.py self-test work/adsp-battery/ksrc/tools/sweet-root-tweaks.py`。
  3. Loader 工具檢核：`python -X utf8 -B work\adsp-battery\prepare-aio-loader.py --help`、`python -X utf8 -B work\localbuild\integ-b022-1126\loader\verify_b022_loaders.py`。
- **主機測試**：
  - `test_b1_nodrm.py` **`10/10 OK`**；`test_check_nodrm_ipa.py` **`33/33 OK`**；`check-nodrm-ipa.py --self-test` **`SELF_TEST_PASS`**。
  - `stage-remove-app.sh` `sh/bash/dash -n` **`SYNTAX_ALL_OK`**；`remove-app-guest.py self-test` 通過 **`41` 項檢核情境**（`PASS remove-app guest: slot7der cycle, appex cache cleanup, 7-layer protection & refusals, IconState unchanged`）。
  - `verify_b022_loaders.py` 4+7 項校驗通過；`prepare-aio-loader.py` 通過 AST、`--help`、CLI 正負測試與 `sensitive_matches() == []`；`launch-loader-check.ps1` 通過 PowerShell AST `ParseFile` 零語法錯誤。
- **驗證紀錄**：
  - Option B `0xB0` 雙端實機通過＋卸載閉環（`stage-b1-nodrm-slot7der-b0-1126` → `#462` 冷開 → `#463` `-r2` → `stage-b1-remove-slot7der-b0-1126`）：完成實機安裝 `175->176`、`SecTask` 消除 `Code=33 (EDOM)` 讀回 4 項 Entitlements、安全卸載還原 `176->175`（`LaunchServices: 2 database(s) removed; chrono: 1 snapshot dir(s) removed`、`ICONSTATE_UNCHANGED 0f3ec839...`、`FSCK_final OK`、`REMOVE_APP_STAGE_SUCCESS`）。
  - `prepare-aio-loader.py`（`79,889 B`, `9eca8290...`）於 Stage E-1 實產日常版 `native-b022-daily-1126-loader` 並通過 `verify-loader-safety.py ... 12g`（17/17 checks、`cmdline 602 B <= 607 B`、`private.adt 71,764 B`）。
- **參考來源**：`U-012`、`T-060`、`K-084`、`B-022`。
- **結論／未完成**：Option B 20+1 靜態預檢／指南／對稱卸載工具鏈、`launch-loader-check.ps1` 擴充、`prepare-aio-loader.py` 主線升級與磁碟歸檔評估均已完成並實證通過；後續 B2 沙盒實作依 `NODRM_CONTAINER_B2_SPEC.md` 推進，磁碟無損搬移須待主代理統一授權並於手機鎖釋放後依 `robocopy` + SHA-256 核對規章執行。
- **出處**：`work/adsp-battery/prepare-aio-loader.py`、`work/statusbar-fix/launch-loader-check.ps1`、`work/statusbar-fix/phone-readthrough.py`、`work/statusbar-fix/remove-app-guest.py`、`work/statusbar-fix/stage-remove-app.sh`、`work/localbuild/nodrm-apps-1126/`、`work/localbuild/integ-b022-1126/loader/verify_b022_loaders.py`、`work/localbuild/stage-e-prep-1126/DISK_RECLAIM_CANDIDATES_1126.md`、`work/statusbar-fix/stage-b1-nodrm-slot7der-b0-1126/`、`work/statusbar-fix/stage-b1-remove-slot7der-b0-1126/`。
