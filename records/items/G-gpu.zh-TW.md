# G 系列：GPU／顯示流暢度線（2026-11-08～11-16）

> 由 2026-11-18 的逐項盤點整合（原始紀錄：`work/xiaomi13-ios-gpu-20261108/continuation.md`（Codex 與主代理的 GPU 主紀錄）、`work/Codex-Xiaomi13-GPU-20261109/` 的 evidence、`work/hybrid-driver/PLAN.zh-TW.md`、`STOPLOSS-0043.zh-TW.md`、harness 補丁與 workflow，逐項見「出處」；`continuation.md:N` 指該檔行號）。補丁與開機參數對照見 `records/catalog/`。

## NativeMetal 與首次 GPU 合成（2026-11-08～11-12）

### G-001 Managed root 與首顆 GPU 測試環境建立

- **日期**：2026-11-08
- **狀態**：實驗：建立 `codexnative_root_1108b` 與 ODM zap 驗證，但 Home 合成因缺失 Metal 功能回退 CPU
- **開關**：無
- **問題／現象**：在小米 13（fuxi）建立可安全增量更新且保留資料的 iOS 16 root 映像，並確認 ODM zap 韌體與 GPU 驅動能否載入並進桌面。
- **根因**：初期測試發現缺乏對應 ABI 開關與正確的 Metal 前端路徑，Home 合成嘗試 GPU 時回退 CPU。
- **做法**：於 Android `userdata` 建立官方 managed image `codexnative_root_1108b`（namespace `codexnative1108b`，mapper `/dev/block/mapper/codexnative_root_1108b`），並以 `verify-native-map.py` 驗證 39 個 extents、DM/FIEMAP 及與備份區不重疊。
- **復現**：透過官方 service 掛載與 `verify-native-map.py` 檢查後 unmap，再以 `fastboot boot` 暫時開機。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：確認 CPU boot 正常進入桌面；active ODM zap driver 可開機，但 GPU 合成嘗試回退 CPU。
- **參考來源**：continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：採用（作為後續 `native-layout00NN` 系列的基礎掛載架構）。
- **出處**：`continuation.md:104-157`，`xiaomi13-gpu-records.md:19`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-002 NativeMetal 補丁堆疊與裝置冒煙測試（stages 0–7）

- **日期**：2026-11-08 ～ 2026-11-09
- **狀態**：修復：通過 GPU 裝置探索、三角形與明確 ASTC 像素測試，證實底層渲染可行
- **開關**：無
- **問題／現象**：以 NativeMetal 取代原有實作，驗證從設備探索到 ASTC 像素取樣的完整底層 Metal/Vulkan/Turnip 管線。
- **根因**：建置腳本原本不允許補丁新增未追蹤檔案，且冒煙測試寫死檢查舊類別名稱 `FuxiMTLDevice`。
- **做法**：  - `native-patch-stack.patch`：修改 `tools/build-fuxi-nativemetal.py` 使用暫存 index 追蹤補丁新增檔案。
  - `native-smoke-class.patch`、`native-ca-smoke-class.patch`：讓 `fuxi-metal-smoke.m` 與 `fuxi-ca-smoke.m` 同時接受 `FuxiMTLDevice` 與 `NativeBridgeMetalDevice`。
  - `astc-pixel-smoke.patch`：於 `fuxi-metal-smoke.m` 加入 stage 7 ASTC 4×4 LDR 壓縮紋理環回與取樣著色測試。
- **復現**：經由 CI 建置並打包寫入測試映像後執行 `fuxi-metal-smoke`。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：實機日誌 `native-caller-astc-smoke-boot-1/boot.log:1447-1776` 顯示 stages 0–7（含 stage 7 ASTC PASS）全數通過。
- **參考來源**：Turnip、continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：採用（證實 Turnip 與 NativeMetal 離屏渲染與 ASTC 像素取樣路徑可行）。
- **出處**：`continuation.md:158-177`，`research.zh-TW.md:9`，`astc-pixel-smoke.patch:1-40`，`native-patch-stack.patch:1-40`，`native-smoke-class.patch:1-17`，`native-ca-smoke-class.patch:1-18`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-003 NativeMetal 補丁 0004–0006 與優先權診斷

- **日期**：2026-11-09
- **狀態**：修復：修復可選回答故障誤判、completion handler 與 `uchar3` 常數；CA 離屏 stage 5 達 60 幀 PASS
- **開關**：無
- **問題／現象**：解決 NativeMetal 啟動與 CA 離屏測試時的誤判故障、completion handler 順序與 `uchar3` 函式常數長度問題。
- **根因**：(1) 缺少可選 device answer 時 `NBLOG` 觸發 `fuxiFailure` 鎖死 gate；(2) `addCompletedHandler:` 在交付完成後才掛載者未立即回呼；(3) `constantBytes()` 未處理 `MTLDataTypeUChar3`（type 51）；(4) `peakpowermanagerd` 預設 `Adaptive` 影響顯示執行緒排程。
- **做法**：  - `nativemetal-fuxi-0004-optional-answers.patch`：對 `"the device has no answer for ..."` 改用 `NSLog` 略過。
  - `nativemetal-fuxi-0005-completion-handlers.patch`：新增 `completionDelivered` 狀態與交付迴圈。
  - `nativemetal-fuxi-0006-uchar3-constant.patch`：`case 51: return 3;`。
  - `priority` 映像將 `peakpowermanagerd` 改為 `Interactive` 進行對照測試。
- **復現**：於 Alpine guest 掛載 overlay 修改並以 `fastboot boot` 驗證 `native-ca-uchar3-boot-1/2` 與 `native-ca-priority-boot-1/2`。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：`native-ca-priority-boot-2/boot.log` 顯示 stages 0–5 PASS（2,592,000 pixels，60 幀，0/59 late，warm wall min/med/max = 5.271/6.224/11.576 ms，2640 draws，4 cache hits）。
- **參考來源**：continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：採用（0004–0006 納入後續所有前端建置）。
- **出處**：`continuation.md:220-240`，`nativemetal-fuxi-0004-optional-answers.patch:1-38`，`nativemetal-fuxi-0005-completion-handlers.patch:1-40`，`nativemetal-fuxi-0006-uchar3-constant.patch:1-22`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-004 唯讀 Host Import 調查與 `fuxigpuncimport=1024`

- **日期**：2026-11-09
- **狀態**：實驗：啟用 bit 10 唯讀匯入通過 CA 圖片階段，但揭露計時器與排程異常
- **開關**：`fuxigpuncimport`、`fuxigpuncimport=1024`
- **問題／現象**：`native-ca-uchar3-boot-2` 在 CA image stage 3 失敗，出現 `host import refused stage 4 ... detail 0x4`。
- **根因**：bitmap bit 2（`0x4`）代表唯讀記憶區描述符成功，但讀寫測試失敗，核心需啟用 opt-in 唯讀匯入路徑（`fuxigpuncimport` bit 10 = 1024）。
- **做法**：使用 `prepare-roimport-loader.py` 於開機參數加入 `fuxigpuncimport=1024` 產出 `native-ca-roimport-loader/boot.img`（SHA-256 `5d73882f57944b081989a1fccaa29f2af2219a50a839d94323f0511a09abc6bb`）。
- **復現**：驗證 39 extents 與 payload 後 `fastboot boot` 執行 `native-ca-roimport-boot-1`。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：CA stages 0–4 通過，stage 5 完成 60 GPU 幀但因 47/59 warm frames 逾時（60 Hz）而 FAIL（pacing sleep overshoot 中位數 27.538 ms，獨立 4 ms sleep 探針空閒超時 100.031 ms）。
- **參考來源**：continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：採用（`fuxigpuncimport` 包含 1024 成為後續所有 GPU 開機參數基礎）。
- **出處**：`continuation.md:222-231`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-005 `nocopy` 診斷補丁 0007/0008/0010 與 ATX 來源調查

- **日期**：2026-11-09 ～ 2026-11-11
- **狀態**：實驗：鎖定阻礙為 QuartzCore 傳入 1125×2436 `pixelFormat=204`（2,752,512 bytes）之無 deallocator 請求
- **開關**：無
- **問題／現象**：開啟 `backboardd` 桌面合成（`survey` 開機）僅畫出 22 幀即被 CPU fallback gate 鎖死（58 次 refusal，`next-respawn=CPU`）。
- **根因**：`backboardd` 呼叫 `newTextureWithBytesNoCopy:length:descriptor:deallocator:`（`pixelFormat=204` ASTC 4×4 LDR、1125×2436、1 mip、`length=2752512`、`deallocator=nil`），NativeMetal 因未知原生記憶體排列而拒絕。
- **做法**：依序建立診斷補丁：
  - `nativemetal-fuxi-0007-nocopy-diagnostics.patch`：前 8 次呼叫印出 `FUXI NOCOPY:` 描述符欄位與頁對齊資訊。
  - `nativemetal-fuxi-0008-nocopy-callers.patch`：首次呼叫印出 `FUXI NOCOPY CALL:` 12 層 `backtrace`（鎖定 QuartzCore `+0x200000`）。
  - `nativemetal-fuxi-0010-nocopy-provenance.patch`：透過 `FuxiNocopyObservation.h` 嘗試讀出物件來源中繼資料（V2/V3 實機驗證失敗回報 metadata validation failure）。
- **復現**：建立 `survey` / `provenance-survey` 映像並實機開機抓取 `boot.log`。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：`native-caller-survey-boot-1/boot.log:2708, 3133-3165` 鎖定上述尺寸與 22 幀拒絕；對照裝置內 ATX 快取檔分析證實其為 `AAPL`/`HEAD`/`FILL`/`astc` 檔案，payload 位於 `0x4000`（16 KiB 對齊），282×609 blocks × 16 = 2,747,808 bytes（向上對齊 16 KiB 為 2,752,512 bytes），為標準 row-major ASTC。
- **參考來源**：原始紀錄未列
- **結論／未完成**：完成診斷，引導出 0011/0012 ASTC layout 回移植修復路線。
- **出處**：`research.zh-TW.md:1-46`，`ASTC_LAYOUT.md:29-45`，`gemini-local-audit.md:1-67`，`nativemetal-fuxi-0007/0008/0010*.patch`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-006 Copy-span 邊界校驗補丁（m2v 0001 / nativemetal 0009）

- **日期**：2026-11-11 ～ 2026-11-12
- **狀態**：修復：為 `HostLayout` 與 bridge 上傳／讀取加入 checked arithmetic 防止越界
- **開關**：無
- **問題／現象**：確保紋理上傳（`replaceRegion`）與讀回（`getBytes`）在 FFI 邊界計算所需位元組數時不會發生整數溢位或越界存取。
- **根因**：原實作缺乏 FFI span 的 checked arithmetic 校驗。
- **做法**：  - `metal2vulkan-fuxi-0001-copy-span.patch`：於 `copy_plan.rs` 新增 `HostLayout::checked_host_len()` 及 `ffi_span_tests`。
  - `nativemetal-fuxi-0009-copy-span.patch`：於 bridge 新增 `nmb_texture_copy_span` 並在 `nmb_texture_replace` 呼叫校驗。
  - `fuxi-copy-span-builder.patch`：讓 `build-fuxi-nativemetal.py` 納入 `metal2vulkan-fuxi-*.patch`。
- **復現**：經 `codex/astc-layout-backport` CI（43060844001）於 macOS ARM64 執行 `ffi_span_tests`。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：兩項 `ffi_span_tests`（含 1125×2436 4512×609 = 2,747,808 bytes 測試）全數通過。
- **參考來源**：metal2vulkan（出現在本項內文，細節見出處）
- **結論／未完成**：採用（納入後續所有前端與 bridge 建置）。
- **出處**：`implementation.zh-TW.md:5-22`，`metal2vulkan-fuxi-0001-copy-span.patch:1-40`，`nativemetal-fuxi-0009-copy-span.patch:1-40`，`fuxi-copy-span-builder.patch:1-30`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-007 ASTC layout 最小回移植（補丁 0011/0012）

- **日期**：2026-11-12 ～ 2026-11-13
- **狀態**：修復：完成同行程 producer/consumer 配對與跨行程 ATX row-major 快照匯入（`FUXI_NATIVE_FOREIGN_LINEAR`）
- **開關**：`FUXI_NATIVE_LINEAR_LAYOUT=0`、`FUXI_NATIVE_FOREIGN_LINEAR=0`
- **問題／現象**：讓 NativeMetal 支援 QuartzCore/ImageIO 的私有紋理排列協定與跨行程 ATX row-major ASTC 快照匯入。
- **根因**：Pinned NativeMetal（`a96fece`）缺乏 `newTextureLayoutWithDescriptor:isHeapOrBufferBacked:`；且 SpringBoard/poster 擴充功能無法開啟 Metal 裝置，係以檔案映射傳遞 ATX 檔給 render server。
- **做法**：  - 補丁 `0011`（分支 `codex/astc-layout-backport`，參考 UniXNU/AdrenoMetal `4d789e9c`）：實作 `FuxiLinearLayout`（watermark `"Fuxi ASTC pair 1"`），登記 producer 寫入之 block 覆蓋數並透過受保護自讀取建立擁有快照後 `replaceRegion` 上傳；`FUXI_NATIVE_LINEAR_LAYOUT=0` 可關閉。成功訊息改用 `NSLog` 避開 `fuxiFailure`。
  - 補丁 `0012`（跨行程快照）：對無 producer 記錄但滿足 ASTC 4×4 LDR/sRGB、`usage == ShaderRead`、1 mip、1 slice、基底位址 16384 對齊且 `length >= totalBytes` 者，透過 `fuxiSnapshotForeign` 建立快照上傳；`FUXI_NATIVE_FOREIGN_LINEAR=0` 可關閉。
- **復現**：於 CI（43056824434、43060844001、43056837931）跑 Windows ASan、Linux ASan/UBSan、ARM64 整合測試與 iOS 16 編譯打包。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：ARM64 主機測試全數通過（ZIP SHA-256 `d91e9407f1371a508ea85b1f1ec6ef3195f5d184c3a5c57c798a9ce278896298`）。
- **參考來源**：UniXNU、Linux（出現在本項內文，細節見出處）
- **結論／未完成**：採用（納入 `0011`、`0012` 實機映像）。
- **出處**：`ASTC_LAYOUT.md:1-80`，`implementation.zh-TW.md:1-36`，`pr-body.md:1-6`，`gemini-foreign-tests-task.md:1-101`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-008 `native-layout0011` 部署與實機驗證

- **日期**：2026-11-12 ～ 2026-11-13
- **狀態**：實驗：部署補丁 0011，但實機拒絕未變，證明 1125×2436 請求來自跨行程 ATX 映像而非同行程 producer
- **開關**：無
- **問題／現象**：實機驗證回移植補丁 `0011`（同行程 producer/consumer 配對）是否解除 `backboardd` 的 CPU fallback。
- **根因**：實際拒絕的 1125×2436 紋理並非在 `backboardd` 同行程內經由 `newTextureLayoutWithDescriptor:` 建立，而是跨行程傳入的 ATX 映像。
- **做法**：建置並寫入 `native-layout0011` root 映像至手機 `/dev/block/mapper/codexnative_root_1108b`（相對前版加入 NativeMetal 補丁 `0011`）。
- **復現**：以現有驗證與部署腳本寫入手機並暫時開機。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：實機日誌顯示拒絕行為未變，仍回退 CPU（該次 boot line 5125 記錄 CPU fallback copy avg 15,925 µs；presents GPU 571 / CPU 22）。
- **參考來源**：continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：被 `native-layout0012` 取代（證實需要支援跨行程 foreign linear 快照）。
- **出處**：`ASTC_LAYOUT.md:31-38`，`continuation.md:372`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-009 `native-layout0012` 部署與首次持續 GPU 桌面合成

- **日期**：2026-11-13
- **狀態**：修復：部署補丁 0012，首次達成持續桌面 GPU 合成，但受限於未調頻與同步提交僅約 16–21 Hz
- **開關**：`fuxigpuncimport=1024`
- **問題／現象**：驗證補丁 `0012` 是否能接納 `backboardd` 的跨行程 ATX 壁紙快照，達成持續桌面 GPU 合成。
- **根因**：已於 `0012` 加入 `fuxiSnapshotForeign` 支援 row-major ATX 匯入；但此時尚未調整 GPU 時脈與 queue 模式。
- **做法**：相對 `0011` 加入 NativeMetal 補丁 `0012`，寫入 `native-layout0012-root.img` 並部署至手機（部署前映像雜湊 `dea8de6d...`，部署後 `native-layout0012-survey-boot-1` 使用 `fuxigpuncimport=1024`）。
- **復現**：部署 `native-layout0012-root.img` 後以 `fastboot boot` 執行 `native-layout0012-survey-boot-1`。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：首次實現持續 GPU 桌面合成（509/613 frames late，忙碌視窗約 **15.7–20.9 Hz**，`gpu_ms` 10–30 ms，`gpu_busy_est_pct` 最高 44.9%）；每幀複製平均 5,270 µs（vs 0011 CPU fallback 15,925 µs 便宜三倍）；發現 GPU 處於 220 MHz（level 0）無 DDR 投票，且無 queue 模式導致每次 submit `retry_sleep` 4.91 ms + 每次清理 66 MB 匯入 cache 2.33 ms。
- **參考來源**：continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：採用（確立持續 GPU 合成基底；開展後續效能瓶頸排查）。並記下與 U-026／B-019 之跨線防回歸契約：桌布行程無 Metal 時，`FuxiNativeMetal` 須持續保留本項 Patch 0012（`FUXI_NATIVE_FOREIGN_LINEAR`／`format=204`）以接納 `QuartzCore` 零拷貝；而 CPU 模糊／染色路徑則依賴 `FuxiAtxFix`（`fuxi,atx-fix=1`）辨識全零 watermark 改走線性解碼，後續修 GPU 不可把磁碟 `.atx` 改寫為 `'ASTC'` 或繞過 `FuxiAtxFix`。
- **出處**：`continuation.md:360-386`，`ASTC_LAYOUT.md:46-54`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

## 流暢度實驗與 native-layout 部署（0013～0024）

### G-010 實驗 1：`fuxigpulevel=4`（401 MHz + DDR 投票）

- **日期**：2026-11-13
- **狀態**：實驗：升頻後觀察幀率變化與溫度，確認單純升頻無法突破 ~30 Hz 瓶頸
- **開關**：`fuxigpulevel=4`
- **問題／現象**：測試將 GPU 時脈從預設 220 MHz（無 DDR 投票）提升至 401 MHz（DDR 6,074,218 kB/s）對桌面流暢度的影響。
- **根因**：`0012` survey 在 level 0（220 MHz）運行，懷疑 GPU 執行時間過長。
- **做法**：於 `0012` root 上僅變更開機參數加入 `fuxigpulevel=4` 暫時開機（未寫入手機）。
- **復現**：使用新 loader 以 `fastboot boot` 暫時開機一次並記錄 `boot.log`。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：使用者目視回報「沒有感覺變流暢，目標是 120 Hz」；實測最佳視窗仍卡在約 ~30 Hz，顯示路徑被 16.67 ms 節拍限制，非單純 GPU 時脈問題。
- **參考來源**：continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：確立「120 Hz 目標」與 401 MHz 搭配後續實驗的基準。
- **出處**：`continuation.md:388-449`，`xiaomi13-goal-120hz.md:11-13`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-011 實驗 2：`fuxigpuncimport=1536`（queue 模式）與閃爍

- **日期**：2026-11-13
- **狀態**：實驗：開啟 bit 9 queue 模式消除 noq 重試睡眠，但滑動時螢幕出現嚴重閃爍
- **開關**：`fuxigpuncimport`、`fuxigpuncimport=1536`
- **問題／現象**：在 `0012` 基礎上開啟 `fuxigpuncimport` bit 9（512，即 1024 + 512 = 1536）啟用 queue 提交模式，消除 GPU 未閒置時 Turnip 反覆睡眠重試的 `noq` 延遲。
- **根因**：queue 模式允許 GPU 在工作中繼續提交下一個 job，但 NativeMetal 的 `commitAndWaitUntilSubmitted` 在 `scheduled` 時即返回，未等待 GPU 畫完便將 `IOSurface` 交給 IOMFB swap，且顯示端無 writer lease 可查詢，導致畫面撕裂／閃爍。
- **做法**：使用 `fuxigpuncimport=1536` 暫時開機一次（220 MHz，未寫入手機）。
- **復現**：`fastboot boot` 並請使用者滑動測試。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：使用者回報「滑動時螢幕一直閃爍（screen flickers all the time while swiping）」。
- **參考來源**：Turnip、continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：定位閃爍根因為缺少 `swap wait`，促成 NativeMetal 補丁 `0013`。
- **出處**：`continuation.md:450-530`，`SWAP_WAIT.md:1-4`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-012 `native-layout0013` 部署（NativeMetal 補丁 0013 swap wait）

- **日期**：2026-11-13
- **狀態**：修復：實作 `fuxiswapwait` 等待 GPU 完成才回傳 swap，實機目視確認滑動閃爍完全消除
- **開關**：`fuxigpuncimport=1536`、`FUXI_NATIVE_SWAP_WAIT=0`、`fuxiswapwait=0`
- **問題／現象**：解決 `fuxigpuncimport=1536`（queue 模式）滑動時產生的全螢幕閃爍。
- **根因**：`-[NBCommandBuffer commitAndWaitUntilSubmitted]` 未等待寫入 `IOSurface` 的 GPU job 完成即交給顯示輸出。
- **做法**：  - 相對 `0012` 加入 NativeMetal 補丁 `0013`（`SWAP_WAIT.md`）：對儲存了 `IOSurface`（`encodedWork.storedSurface`）的 command buffer，等待 `nmb_command_buffer_wait` 完成才從 `commitAndWaitUntilSubmitted` 返回。開關：`FUXI_NATIVE_SWAP_WAIT=0` 或開機參數 `fuxiswapwait=0`（預設開啟）。
  - 於 `native-layout0012-root.img` 上以 `native-layout0013-overlay.qcow2`（`prepare-layout0013-guest.py`）製成 `native-layout0013-root.img`（21,474,836,480 bytes）。
  - 使用 `delta-stage-hash.py` 計算 287 個差異 4 MiB blocks（1148 MiB），經 `native-layout0013-write.sh`（sha256 `bbafa77e...`）寫入並讀回驗證，並以 `native-layout0013-queue1536-boot-1` 測試。還原點：`native-layout0012-root.img` 與 commit `17fbbec`。
- **復現**：`delta-stage-hash.py` -> `native-layout0013-supervisor.sh` 部署 -> `fastboot boot` 執行 `native-layout0013-queue1536-boot-1`（`boot.log` 9986 行，sha256 `aeb70264...`）。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：使用者目視確認「完全無閃爍了」；日誌呈現 `FUXI MTL: native swap wait`，但幀率仍未達 120 Hz。
- **參考來源**：continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：採用（`0013` 成為新基準，後續可用 `fuxiswapwait=0` 配合 core/fence 機制關閉）。
- **出處**：`continuation.md:531-624`，`SWAP_WAIT.md:1-22`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-013 實驗 3（queue+level4）與實驗 4（`scanout120` 120 Hz 面板接管）

- **日期**：2026-11-13
- **狀態**：實驗：啟用 `fuxipanel120=1` 與 `fuxivsyncte=1` 解除 60 Hz 面板鎖定，vsync 達 120.9 Hz
- **開關**：`fuxigpulevel=4`、`fuxigpuncimport=1536`、`fuxigpuscanout=1`、`fuxiscandouble=1`、`fuxiscantriple=1`、`fuxipanel120=1`、`fuxivsyncte=1`
- **問題／現象**：解除面板 60 Hz 更新率限制並測試 `fuxigpulevel=4` + `queue`（1536）。
- **根因**：預設核心顯示路徑以 60 Hz（16.67 ms）節拍運作。
- **做法**：  - 實驗 3（11:40:09）：`0013` + `fuxigpuncimport=1536` + `fuxigpulevel=4` 暫時開機（`native-layout0013-queue1536-gpulevel4-boot-1`）。
  - 實驗 4（`scanout120`）：於開機參數加入 `fuxigpuscanout=1 fuxiscandouble=1 fuxiscantriple=1 fuxipanel120=1 fuxivsyncte=1` 暫時開機（`native-layout0013-scanout120-boot-1`，`boot.log` 13976 行，sha256 `58488c46...`）。
- **復現**：各自以對應 loader 執行 `fastboot boot`（未寫入手機）。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：實驗 4 成功將面板與 vsync 推升至 **120.9 Hz**；但繪製端仍卡在單幀 GPU/cache 清理成本，僅出現 3–8% 單週期翻頁，整體未達 120 Hz。
- **參考來源**：continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：採用 `scanout120` 參數組合為後續 120 Hz 實驗固定基底。
- **出處**：`continuation.md:625-849`，`xiaomi13-goal-120hz.md:15`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-014 實驗 5（`kernel-bo-slots` 4096 + `9728`）、`prefdiag` 與實驗 6（dirty19）

- **日期**：2026-11-13 14:58 ～ 15:41
- **狀態**：實驗：解除 BO slot 上限與 START floor 達 52.5 Hz（使用者「有變順」）；回報「關於本機」卡死與多工馬賽克；bit 19 自測拒測
- **開關**：`fuxigpuncimport=9728`、`fuxigpuncimport`
- **問題／現象**：擴大核心 GPU BO slot 上限以防 `alloc` BO 耗盡導致崩潰，解除 10 ms START floor，並測試 dirty tracking（bit 19）。
- **根因**：`FG_ABI_MAX_BOS` 原為 1024；START floor 預設 10 ms 限制高刷新率；未開啟一致性或 dirty tracking 導致每次 submit 清理 36–47 MB cache（1.5–1.9 ms）。
- **做法**：  - 建置套用 `kernel-bo-slots.patch`（`FG_ABI_MAX_BOS` 1024 -> 4096）之新核心（branch `codex/kernel-bo-slots`）。
  - 實驗 5（14:58:59，`native-layout0013-bo4096-boot-1`）：加 `fuxigpuncimport=9728`（含 bit 8192 將 START floor 10 ms 降至 6.25 ms）。
  - `prefdiag`（15:17–15:21，`native-layout0013-prefdiag-boot-1`）：針對實驗 5 使用者回報「關於本機打不開」與「多工預覽馬賽克」進行診斷採樣。
  - 實驗 6（15:37:06，`native-layout0013-dirty19-boot-1`）：測試 `fuxigpuncimport` 加入 bit 19（524288）dirty tracking。
- **復現**：以 `prepare-bo4096-loader.py` 打包並逐次 `fastboot boot`（240 s）。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：  - 實驗 5：最佳視窗 **52.5 Hz**，使用者首次正面回饋「有變順」；BO 峰值 990（尚未超 1024）。
  - `prefdiag`：確認「關於本機」未崩潰而是頁面載入永不完成（後續 `0014` 查明原因）。
  - 實驗 6：核心自我測試拒絕（code 1：`pmap_clear_modify` 後 modify bit 仍在），未生效。
- **參考來源**：continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：`kernel-bo-slots.patch` 採用；bit 19 dirty tracking 停損淘汰。
- **出處**：`continuation.md:979-1252`，`kernel-bo-slots.patch:1-36`，`xiaomi13-goal-120hz.md:15`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-015 實驗 7（`9792` bit 6 coherent self-test）與實驗 8（level 8 680 MHz）

- **日期**：2026-11-13 15:47
- **狀態**：實驗：`wb-ish` 通過 8/8，消除每幀 1.1–1.9 ms cache 清理，跳升至 75.4 Hz；level 8 升溫至 73.8 °C 不適合常駐
- **開關**：`fuxigpuncimport`、`fuxigpuncimport=9792`、`fuxigpulevel=8`
- **問題／現象**：測試 `fuxigpuncimport` bit 6（64，`9728 + 64 = 9792`）write-back inner-shareable（`wb-ish`）匯入一致性自我測試，並在高基準上測試最高 GPU 時脈 level 8（680 MHz）。
- **根因**：若 `wb-ish` 一致性自測通過，核心可完全略過每次 submit 對所有 mapped imports 的 cache clean/invalidate。
- **做法**：  - 實驗 7（`native-coh6-loader`）：`fuxigpuncimport=9792` 暫時開機（`native-layout0013-coh6-boot-1`，`boot.log` 8526 行，sha256 `c42dca6fe1d806bd6f4e428fc5a5dfb849f65f530aa9146dca3aba801e74e590`）。
  - 實驗 8（`native-coh6-lvl8-loader`）：`9792` + `fuxigpulevel=8`（680 MHz）暫時開機（`native-layout0013-lvl8-boot-1`，`boot.log` 11059 行，sha256 `c2086ebc...58a1af`）。
- **復現**：使用對應 loader 執行 `fastboot boot`（240 s）。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：  - 實驗 7：`wb-ish` 變體 **8/8 pass**，cache 清理時間由 1.1–1.9 ms 驟降至 **2.2–2.7 µs**；BO 峰值達 1062（正式驗證 4096 slots）無崩潰；最佳視窗躍升至 **75.4 Hz**；使用者確認無異常但多工馬賽克仍在。
  - 實驗 8：`gpu_ms` 降至 4.8–6.5 ms、late 由 10.6% 降至 2.4%、持續視窗 52–62 Hz（最高 67.6 Hz），但溫度升至 **73.8 °C**（原 63.5 °C）。
- **參考來源**：continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：`9792` 採用為新基準；無溫控調頻器下不長期使用 level 8。
- **出處**：`continuation.md:1253-1414`，`xiaomi13-goal-120hz.md:15`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-016 實驗 9（`kernel-surfrow-mode2` `sweetsurfrow=2`）與實驗 10（TE spin）

- **日期**：2026-11-13
- **狀態**：修復：修復 app-switcher 卡片馬賽克（best 97.6 Hz）；實驗 10（`304704`）確認 `swap wait` 為下一段串行瓶頸
- **開關**：`fuxigpuncimport=304704`、`sweetsurfrow=1`、`sweetsurfrow`、`sweetsurfrow=2`
- **問題／現象**：修復多工介面（app switcher）app 預覽卡片呈現馬賽克的問題，並測試 TE spin + hard spin（`fuxigpuncimport=304704`）。
- **根因**：原本 `sweetsurfrow=1` 強制將所有表面 `bytesPerRow` 進位至 256-byte 對齊，但 app-switcher 787×1705 卡片的 `bpr` 從 3200 被改成 3328 而與預期步長不符。
- **做法**：  - 實作 `kernel-surfrow-mode2.patch`（`sweetsurfrow` 預設改為 `2`：僅對未 64-byte 對齊的 row 進 256-byte 對齊，64-byte 對齊者保持不變），重建核心。
  - 實驗 9（`native-layout0013` + `surfrow mode 2` 核心 + 實驗 8 loader）暫時開機。
  - 實驗 10（`fuxigpuncimport=304704`）：開啟 TE spin 與 hard spin 暫時開機。
- **復現**：更新核心產物重包 loader 後 `fastboot boot`。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：實驗 9 修復 app switcher 卡片馬賽克（787×1705 `bpr` 維持 3200），最佳視窗達 **97.6 Hz**，同時發現 command-mode panel START 只有 ~0.3 ms 窗口限制；實驗 10 確認生產端 `swap wait` 仍是下一段主要串行瓶頸。
- **參考來源**：continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：`kernel-surfrow-mode2.patch` 永久採用。
- **出處**：`continuation.md:1415-1576`，`kernel-surfrow-mode2.patch:1-40`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-017 `native-layout0014` 部署（m2v 補丁 0002 `fresh-zero`）與實驗 11（關 swap wait）

- **日期**：2026-11-13 晚
- **狀態**：修復：新建未寫材質改為歸零（除紫色背景），`aboutdiag` 定位 `InterfaceNamer` 卡死；bit 12 關 swap wait（`308800`）無閃爍
- **開關**：`fuxiswapwait=0`、`fuxigpuncimport=308800`
- **問題／現象**：解決使用者回報「Safari 分頁總覽背景呈紫色」，診斷「關於本機」卡死，並測試關閉 `swap wait` 搭配核心 `reuse_before_show=0`。
- **根因**：  - 紫色現象：`metal2vulkan-runtime` 模擬 Apple Silicon Mac 未壓縮前之 `Private` 紋理預設填入不透明洋紅色（`fresh_pattern`），疊加 60% 灰後呈紫色（`179, 77, 179`）。
  - 「關於本機」卡死：`aboutdiag` 採樣顯示 `Preferences` 卡在 `InterfaceNamer` 等候 IOKit `WaitStack`/`WaitQuiet`（預設 240/300 秒）。
- **做法**：  - 相對 `0013` 加入 `metal2vulkan-fuxi-0002-fresh-zero.patch`（CI 43417101386，iOS 預設關閉 `fresh_pattern` 改為歸零；可用 `FUXI_M2V_FRESH_PATTERN=1` 還原）。
  - 於 `native-layout0013-root.img`（`b5c10b14...`）以 `native-layout0014-overlay.qcow2`（`prepare-layout0014-guest.py`）產出 `native-layout0014-root.img`（21,474,836,480 bytes，staged bridge `fd9a30a1...`），計算 419 blocks（1676 MiB）差異並以 `native-layout0014-write.sh`（sha256 `a486346f...f9b0`）寫入。
  - 開機：`native-layout0014-aboutdiag-boot-1`（`boot.log` 16109 行，sha256 `245cfce3...cfc6`）與實驗 11 `native-layout0014-nowait-boot-1`（`boot.log` 5244 行，sha256 `e7038f4a...73ac`，`fuxiswapwait=0` + `fuxigpuncimport=308800` 含 bit 12 關 swap wait）。
- **復現**：`delta-stage-hash.py --name layout0014` -> `native-layout0014-supervisor.sh` 部署 -> `fastboot boot`。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：`fresh-zero` 生效（但舊已寫入磁碟之壁紙模糊快取仍留紫）；實驗 11 證實 bit 12 關閉 swap wait 無閃爍、`reuse_before_show=0`。
- **參考來源**：metal2vulkan、continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：採用 `fresh-zero`；依 `aboutdiag` 結果準備 `0015` 修復「關於本機」。
- **出處**：`continuation.md:1594-1724`，`FRESH_ZERO.md:1-35`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-018 `native-layout0015` 部署與實驗 12/13（CPU 調頻器）

- **日期**：2026-11-13 21:10 ～ 晚
- **狀態**：修復：修改 `InterfaceNamer` 逾時為 15 s 解決「關於本機」卡死並清舊模糊快取；實驗 12/13 揭露 62 °C / 200 ms 限頻
- **開關**：`fuxiperfbase`、`fuxiperfmax`、`fuxiperfbudget`、`fuxicores`、`fuxiperf`
- **問題／現象**：修復「關於本機」卡死與舊紫色快取，並測試動態 CPU 調頻器（`kernel-perf-governor`）。
- **根因**：`InterfaceNamer.bundle/Info.plist` 等候 IOKit quiet 逾時過長（240/300 s）；CPU 調頻器因 62 °C 門檻與 200 ms boost 預算而持續降頻。
- **做法**：  - 相對 `0014` 使用 `guest-run.py` + `stage-0015.sh` 產出 `native-layout0015-root.img`（195 blocks 差異），修改 `InterfaceNamer.bundle/Info.plist` 之 `WaitStackTimeout=15.0`、`WaitQuietTimeout=15.0` 並刪除已寫入的舊紫色模糊快取，以 `native-layout0015-write.sh`（sha256 `a2f970b9...d593`）寫入並執行 `native-layout0015-nowait-boot-1`（`boot.log` 197844 行，sha256 `066d580d...4576`）。
  - 實驗 12／13：實作 `kernel-perf-governor.patch`（`fuxiperfbase` / `fuxiperfmax` / `fuxiperfbudget`）與搭配核心 `fuxicores`，於 `0015` 執行暫時開機測試。
- **復現**：`stage-0015.sh` -> 部署 `0015` -> `fastboot boot` 實驗 12/13（240 s）。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：`0015` 解決「關於本機」卡死與紫色模糊殘留；實驗 12/13 揭露 `fuxiperf` 受 62 °C 門檻與 200 ms 預算限制頻繁切斷升頻，需更高門檻或解決渲染瓶頸。
- **參考來源**：continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：`0015` 採用；調頻器微調暫擱置。
- **出處**：`continuation.md:1725-2000`，`kernel-perf-governor.patch:1-40`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-019 `native-layout0016` 部署、實驗 15/16 與首輪 IB profile

- **日期**：2026-11-14 01:43
- **狀態**：實驗：加入 `kernel-surface-stride` 與 `novmd`，`nowait` 達 86.0 fps / 94.98 Hz；`kernel-ib-profile` 定位 GPU exec 7.5–9.4 ms
- **開關**：`fuxiearly`、`fuxigpulevel=6`
- **問題／現象**：修正 iOS 16.7 `IOSurface` stride 欄位，消除 `vmd` 背景噪聲並以硬體 IB profile 找出 GPU 執行時間去向。
- **根因**：IOSurface 16.7 讀取 client `+0xe0`（result `+0x10`）之 `bytesPerRow`；背景 `vmd` 耗用資源。
- **做法**：  - 核心補丁 `kernel-surface-stride.patch`（寫入 `put32(p, 0x10, pl->bytesPerRow)`）與 `kernel-ib-profile.patch`（`fuxiearly` bit 16 每 96 jobs 塞入 `CP_WAIT_FOR_IDLE` + `CP_REG_TO_MEM` 採樣 IB1/IB2 時間與 blit/draw/RP 統計，bit 3 記 job stamps）。
  - `native-layout0016` 加入前端 `novmd` 設定。
  - 實驗 15/16：`novmd` + `nowait` 測試；並測試 `fuxigpulevel=6`（550 MHz）能否省電換取 CPU boost。
  - 執行 `native-layout0016-s25-ibp-boot-1`（`boot.log` 13485 行，sha256 `14dba67a...635a`）。
- **復現**：部署 `0016` 後以 `fastboot boot` 跑實驗 15/16 與 `s25-ibp`。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：實驗 15/16（`novmd` + `nowait`）達 **86.0 fps shown / 94.98 Hz produced**；`fuxigpulevel=6`（550 MHz）無助於省出 CPU boost；`ibp` 測得每幀 GPU exec 平均 7.5–9.4 ms。
- **參考來源**：continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：`kernel-surface-stride.patch`、`novmd`、`kernel-ib-profile.patch` 均採用。
- **出處**：`continuation.md:2001-2265`，`kernel-surface-stride.patch:1-40`，`kernel-ib-profile.patch:1-40`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-020 `native-layout0017` 部署（m2v 補丁 0003 單獨測試）

- **日期**：2026-11-14
- **狀態**：實驗：僅套用 0003（`FLUSH_PER_OVERLAP`），`s25-ibp` 測得 34.0 Hz / `gpu_ms` 8.62
- **開關**：無
- **問題／現象**：測試 `metal2vulkan` 補丁 `0003`（`FLUSH_PER_OVERLAP`）減少不必要 pass/flush 對 GPU 時間的影響。
- **根因**：懷疑過多的 render pass 與 flush 造成 GPU 時間偏高。
- **做法**：相對 `0016` 僅加入 m2v 補丁 `0003`，製成 `native-layout0017-root.img`（sha256 `f9979cc84d866a73dce93890e110d5bf19e1beae124418c9892a83294c227390`，與 `0016` 差 22 blocks），以 `native-layout0017-write.sh`（`eb23e4ac...26e8`）部署，執行 `native-layout0017-s25-ibp-boot-1`。
- **復現**：部署 `0017` 後使用同 `0016` 之 `s25-ibp` loader 開機。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：`backboardd` 測得 **34.0 Hz**、`gpu_ms` **8.62**、40% 單週期翻頁。
- **參考來源**：metal2vulkan、continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：過渡版本，待搭配 0004 一同評估。
- **出處**：`continuation.md:2266-2275`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-021 `native-layout0018` 部署（m2v 0003+0004）與 `sweetprof` 剖析

- **日期**：2026-11-14 02:40
- **狀態**：實驗：`gpu_ms` 降至 6.64 ms，但 `sweetprof` 發現顯示執行緒 20% CPU 耗在 `supportsFamily:` 稽核
- **開關**：`sweetprof`
- **問題／現象**：加入 m2v 補丁 `0004` 並利用 `sweetprof` 剖析 `backboardd` 顯示執行緒為何受限於 CPU。
- **根因**：`sweetprof`（搭配 `macho-fit.py`）揭露顯示執行緒有高達五分之一時間耗在 `supportsFamily:`／`families::audit` 稽核。
- **做法**：相對 `0017` 加入 m2v 補丁 `0004`，產出 `native-layout0018-root.img`（sha256 `4c10332db264d2c3b0939fdf8f32641f765298e7d63ab275a7fd2c9d86128b34`）並部署。先後跑 `native-layout0018-s25-ibp-boot-1`、`native-layout0018-drag-ibp-boot-1`（`native-ibprof-drag-loader`，`boot.log` 18721 行）與 `native-layout0018-dragprof-boot-1`（`boot.log` 17763 行，sha256 `46467024...78eb`）。
- **復現**：部署 `0018` -> `fastboot boot` 跑 `s25-ibp`、`drag-ibp` 與 `dragprof`。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：`s25-ibp` `gpu_ms` 降至 **6.64 ms**；拖曳負載下 `backboardd` 為 **68.1 Hz**，`sweetprof` 鎖定 `supportsFamily:` 為最大可立即移除之 CPU 熱點。
- **參考來源**：continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：採用診斷結果，催生 `0019` 的 `family-cache` 補丁。
- **出處**：`continuation.md:2276-2335`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-022 `native-layout0019` 部署（m2v 補丁 0005 `family-cache`）與退回 0003

- **日期**：2026-11-14 ～ 06:45
- **狀態**：修復/停損：消除 `supportsFamily:` 開銷，推升至 83.4 Hz（dragreg 86.2 Hz）；但視覺檢查發現材質／毛玻璃毀損，決定移除 0003
- **開關**：`fuxiearly=16`、`fuxiperf=2`、`fuxiregtrace=1`
- **問題／現象**：快取 `supports_family` 結果以消除 20% CPU 稽核開銷，並由使用者進行視覺驗收。
- **根因**：雖然 `0005` 大幅提升幀率，但先前的 `0003` 補丁破壞了 CoreAnimation framebuffer fetch 順序，導致毛玻璃／材質毀損。
- **做法**：相對 `0018` 加入 m2v 補丁 `0005`（`family-cache`）產出 `native-layout0019-root.img` 並部署。跑 `native-layout0019-dragprof-boot-1`、`native-layout0019-dragreg-boot-1`（含 `fuxiearly=16`、`fuxiperf=2`、`fuxiregtrace=1`）與使用者目視開機 `native-layout0019-user-boot-1`。
- **復現**：部署 `0019` 後執行 `dragprof`、`dragreg` 與 `native-profpc-user-loader`。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：`dragprof` 達 **83.4 Hz**（0018 為 68.1 Hz），`dragreg` 達 **86.2 Hz**；但使用者目視確認「更順了，但材質／毛玻璃壞掉」。
- **參考來源**：continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：`0005` 採用；**`0003` 因破壞畫面質感而停損淘汰**。
- **出處**：`continuation.md:2336-2385`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-023 `native-layout0020` 部署（移除 0003）

- **日期**：2026-11-14 07:21 ～ 07:39
- **狀態**：修復：僅保留 0004+0005，兩輪使用者目視確認材質／毛玻璃恢復正常
- **開關**：無
- **問題／現象**：在保留 `0004` 與 `0005` 效能收益下，移除造成毛玻璃破圖的 `0003`。
- **根因**：`0003` 的 `FLUSH_PER_OVERLAP` 影響 fetch 順序。
- **做法**：相對 `0019` 僅剔除 m2v 補丁 `0003`，製成 `native-layout0020-root.img` 並部署。
- **復現**：部署 `0020` 後跑兩次使用者目視開機 `native-layout0020-user-boot-1`（07:21–07:29）與 `-boot-2`（07:31–07:39）。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：使用者目視確認材質、毛玻璃恢復完全正常。
- **參考來源**：continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：採用（確立後續快速通道皆須排除或謹慎對待 framebuffer fetch 之原則）。
- **出處**：`continuation.md:2386-2395`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-024 `native-layout0021` 部署（m2v 補丁 0006–0008）

- **日期**：2026-11-14 08:00
- **狀態**：實驗：加入 m2v 逐 draw 與 shadow copy 診斷計數，確認大量多餘 shadow copy
- **開關**：無
- **問題／現象**：重讀 `0019` `dragreg` IB profile 找出 GPU 與 m2v 時間去向，加入逐 draw／render pass 診斷與 shadow copy 統計。
- **根因**：`metal2vulkan` 內部對每筆 draw 執行大量無意義的 shadow copies。
- **做法**：相對 `0020` 加入 m2v 補丁 `0006`–`0008`，產出 `native-layout0021-root.img` 並部署；撰寫 `reassemble-m2v.py` 解析被切斷的日誌。
- **復現**：部署 `0021` 後以 `native-ibprof3-dragreg-loader`（`kernel-ibprof3-artifact`）執行 `native-layout0021-dragreg-boot-1`。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：透過 `native-layout0021-dragreg-boot-1-m2v.txt` 量化 shadow copies 與 m2v 內部開銷。
- **參考來源**：metal2vulkan、continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：採用診斷數據，直接推動 `0022` 補丁 `0009`。
- **出處**：`continuation.md:2396-2446`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-025 `native-layout0022` 部署（m2v 0009）與 GMEM 重試／`touch10`

- **日期**：2026-11-14 09:11 ～ 10:50
- **狀態**：修復：消除 shadow copies，sysmem 達 90.1 Hz / 71% 單週期；GMEM 達 101.9 Hz / 89%；`fuxitouchdelay=10` 提前觸控；大檔集中工作區
- **開關**：`fuxitouchdelay`、`fuxitouchdelay=10`
- **問題／現象**：消除 `0021` 查明的冗餘 shadow copies，重試 GMEM 模式，並將觸控延遲從 30 s 提前至 10 s。
- **根因**：Shadow copies 拖慢 sysmem 並使 GMEM 失效／低效；鎖定畫面初期無觸控是因 `fuxitouchdelay` 預設 30 s 過晚。
- **做法**：  - 相對 `0021` 加入 m2v 補丁 `0009`（消除 shadow copies），產出 `native-layout0022-root.img`（sha256 `6d26e1c071842ee14a4293ed1676b95d751e3a8b...`）並部署。
  - 使用 `tour` 截圖（`native-layout0022-tour-boot-1`）、`native-layout0022-draggmem-boot-1`（payload `41760492...06cc`、boot.img `85f98544...2b9e`）、`native-layout0022-tourgmem-boot-1` 與 `native-touch10-user-loader`（`fuxitouchdelay=10`）。
  - 依使用者要求「大檔案一律只能放到工作區文件夾內」，將舊 C/E 槽大檔移轉集中至 `D:\xiaomi13\fuxi_ios\work\`，並將 `0018`/`0019` raw 無損轉為壓縮 qcow2 釋放空間。
- **復現**：部署 `0022` -> 分別跑 `dragreg`、tour、GMEM drag、GMEM tour 與 `touch10` 開機。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：  - sysmem `dragreg` 達 **90.1 Hz**（原 83.4 Hz）、71% 單週期翻頁（原 56%）。
  - `tour-boot-1` 確認 sysmem 下材質完全正常。
  - GMEM `draggmem` 達 **101.9 Hz**、89% 單週期翻頁；但 `tourgmem` 截圖出現水平舊內容殘影條帶，誤判為渲染毀損。
- **參考來源**：continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：`0009` 採用；`fuxitouchdelay=10` 採用；觸發 `0023` GMEM 二分調查。
- **出處**：`continuation.md:2447-2562`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-026 `native-layout0023` 部署（`fuxitudebug=`）與 GMEM 二分法

- **日期**：2026-11-14 10:24 ～ 12:00
- **狀態**：實驗：以 `fuxitudebug=` 二分排除 `flushall`/`syncdraw`/`nobin`，當時判為 `noconcurrentresolves` 修復條紋
- **開關**：`fuxitudebug=`
- **問題／現象**：透過 `TU_DEBUG` 旗標二分法找出 GMEM 截圖水平殘影的 Turnip 根因。
- **根因**：當時觀察到關閉 `concurrent resolves` 後截圖正常，誤判為 Turnip A740 concurrent resolves 缺陷（後於 `0024` 查明為擷取假象）。
- **做法**：相對 `0022` 加入 `fuxi-tudebug-bootarg.patch` 支援開機參數 `fuxitudebug=`，產出 `native-layout0023-root.img`（sha256 `24cce13b71e14635cc92c4b4c5ea1249423e50ef0f2dd0e6ebc53e891198c843`）並以 `native-layout0023-write.sh`（`11025230...2ced`）部署。依序跑 `tux`（`nocb,flushall,syncdraw`）、`tunb`（`nocb,nobin`）、`tuy`（`nocb,noconcurrentresolves,noconcurrentunresolves`）、`tuun`（`nocb,noconcurrentunresolves`）、`tures`（`nocb,noconcurrentresolves`）。
- **復現**：部署 `0023` 後依序變更 `fuxitudebug=` 跑 tour 截圖驗證。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：`tux`、`tunb`、`tuun` 截圖皆出現殘影；`tuy` 與 `tures`（含 `noconcurrentresolves`）截圖與 sysmem 逐像素相同。
- **參考來源**：Turnip、continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：當下誤判為 `noconcurrentresolves` 根除問題，導出 `0024` 預設修改。
- **出處**：`continuation.md:2563-2626`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-027 `native-layout0024` 部署、假象平反、`boot_b` 開機鏈與 `vt6` 突破

- **日期**：2026-11-14 12:05 ～ 20:45
- **狀態**：修復/結論：`--tour-verify` 證實 GMEM 條紋為擷取假象；`PROFINC`/`PROFEDGE` 鎖定 m2v 逐 draw 為 CPU 瓶頸；建立 `boot_b` `chain-v1`；`fuxivsynctoff=6000` 讓捲動達 ~119 Hz
- **開關**：`fuxigpugmem=1`、`fuxitudebug=nocb`、`fuxitapcmd=@1`、`fuxigpuncimport`、`fuxigpulevel=24`、`fuxiperfidle=1000`、`fuxivsynctoff=6000`
- **問題／現象**：部署 GMEM 預設設定、修正截圖驗證方法、剖析剩餘 CPU 瓶頸、調整 vsync 相位、建立不接電腦開機鏈與省電模式。
- **根因**：  - GMEM 條紋假象：`fuxi-xnu-monitor.py` 單張截圖需 16 s 以 512-byte control transfer 讀取掃描 buffer，遇使用者觸控畫面移動就會讀到 triple-buffer 交錯條紋。
  - 設定捲動掉幀：vsync 通知相位預設 `TE+7800 µs`（為 60 Hz 設計），120 Hz 下預算僅剩 ~0.4 ms。
  - CPU 瓶頸：`PROFINC`/`PROFEDGE` 證實 `backboardd` 74% 在 CPU（26% 等 vsync），`m2v` 佔 ~45%，CA 僅佔 ~5%。
  - 觸控腳本 `@2`（90 px/s 慢拖曳）因像素對齊上限約 ~90 Hz，無法測出 120 Hz 能力。
- **做法**：  - 部署 `native-layout0024-root.img`（僅改 `FuxiMetal` 預設 `fuxigpugmem=1` 為 `nocb,noconcurrentresolves`）。
  - 實作 `--tour-verify`（每張截圖連讀兩次比對，移動中標為 moving 不採信），在 `0024` 以 `fuxitudebug=nocb`（`tourplain-boot-1`）開回完整 GMEM 驗證逐像素正確。
  - 核心補丁群：`kernel-prof-inclusive.patch`、`kernel-prof-inclusive-wait.patch`、`kernel-prof-edges.patch`、`kernel-prof-live.patch`（IPI 跨核讀取真實執行 pc/lr/fp）、`kernel-swipe-pacing.patch`（8 ms 絕對節拍）、`kernel-ibprof-draws.patch`、`kernel-taps-presets.patch`、`kernel-input-fixes.patch`、`kernel-taps-page2.patch`（`fuxitapcmd=@1` 與 `@2`）。
  - 測試 `fuxigpuncimport` bit 21（inline copy，`2405960`）、`cool-user`（`fuxigpulevel=24` + `fuxiperfidle=1000` + `-MonitorIdle 2` 降低靜態溫度）。
  - 建立獨立開機鏈：先試 `recovery_a`（遭小米 ABL 阻擋）後改寫入 `boot_b`（`chain-v1`）實現不接電腦重開機進 iOS。
  - 測試 `fuxivsynctoff=6000`（`native-layout0024-settings-vt6-boot-1`）釋放 ~10.5 ms 幀預算。
- **復現**：分別執行 `tourplain`、`inlinegmem`、`profinc`/`profwait`/`profedge`、`cool-user`、`chain-v1`、`settings-vt6`。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：  - `tourplain`：完整 GMEM 截圖與 sysmem 逐像素相同，平反假象（先前加 `noconcurrentresolves` 白損約 7 Hz）。
  - `inlinegmem`：0 fallback，但吞吐維持 102 Hz。
  - `settings-vt6`（@1 設定甩動）：捲動階段從 ~105 Hz 躍升至 **~119 Hz**（逐幀漏 TE 由 19 降至 5）；但開 app／回桌面重幀仍超出 CPU 預算。
- **參考來源**：metal2vulkan、continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：確認剩餘主槓桿為 `metal2vulkan` 逐 draw CPU 開銷；確立 `fuxivsynctoff=6000` 與 `boot_b` 雙系統方案。
- **出處**：`continuation.md:2627-3054`，`PLAN.zh-TW.md:11-22`，`xiaomi13-goal-120hz.md:47-51`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

## Bridge LTO 之後：0025～0043 與混合式快速通道

### G-028 `native-layout0025` 部署（Bridge LTO + `fuxitcache=1`）與 `@3` 基準

- **日期**：2026-11-14 21:00 ～ 22:00
- **狀態**：修復：Bridge 改 fat LTO + tcache，每 draw 由 ~72 µs 降至 57.7 µs；`@3` 達 81.7 Hz / 5.73 ms CPU
- **開關**：`fuxitcache=1`、`fuxitapcmd=@3`
- **問題／現象**：降低 `metal2vulkan` bridge 函式呼叫與記憶體配置器鎖競爭開銷，並建立第 1 頁開設定腳本 `@3` 基準。
- **根因**：原本 bridge 未開啟 LTO，且多執行緒頻繁配置釋放產生鎖競爭；使用者重排桌面後 `@1` 失效，需改用 `@3`。
- **做法**：  - 相對 `0024`（`f128ca55...1641`）加入 bridge `lto="fat"`、`codegen-units=1` 與前端補丁 `0015`（`FuxiThreadCache.rs`，`fuxitcache=1` 啟用每執行緒快取），製成 `D:\xiaomi13\fuxi_Backup\ios-root-images\native-layout0025-root.img`（作為後續所有 `0026–0043` 增量差分備份之乾淨基底）。
  - 核心補丁 `kernel-taps-page1.patch`（`fuxitapcmd=@3`，由首頁 148,570 點擊設定，產出 `kernel-page1-artifact`）。
- **復現**：部署 `0025` 後，以 `kernel-page1-artifact` 跑 `@3` A/B 測試 `native-layout0025-p1-vt6-boot-1`（tcache 關）與 `native-layout0025-p1-vt6tc-boot-1`（`fuxitcache=1`）。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：  - tcache 關：**79.6 Hz**、82% 單週期、每幀 6.04 ms CPU、每 draw **62.5 µs**。
  - tcache 開：**81.7 Hz**、84% 單週期、每幀 5.73 ms CPU、每 draw **57.7 µs**（由 0024 的 72 µs 降至 57.7 µs）。
- **參考來源**：metal2vulkan、continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：採用（`0025` 成為後續保留手機使用者資料之增量基準映像）。
- **出處**：`continuation.md:3055-3099`，`kernel-taps-page1.patch:1-40`，`xiaomi13-goal-120hz.md:51-52`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-029 `native-layout0026` 部署（Hybrid P1 普查 `fuxihyb=1`）與 `fuxibacklight=4`

- **日期**：2026-11-14 22:00 ～ 23:15
- **狀態**：實驗：79.0% draw 符合 L1 資格（唯一排除為 fetch 21%），每 draw m2v 37.4 µs；離線完成 `kernel-backlight-dsi.patch`
- **開關**：`fuxihyb=1`、`fuxibacklight=4`
- **問題／現象**：執行混合式驅動（Hybrid Lane）P1 普查，量化 CoreAnimation draw 符合 L1 快速通道條件的比例與內部耗時；同時於離線實作面板亮度模式 4。
- **根因**：需先掌握實際 pipeline 結構與綁定變更頻率，方能設計不破壞渲染（避開 0003 fetch 坑）的快速通道。
- **做法**：  - 啟用保留資料增量部署：先拉回手機 22:21 現況（`10410d0e...`）重播 HFS+ journal（`stage-test-phonebase.sh`）再換上 m2v 補丁 `0010`（CI 43602677972，`fuxihyb=1` 普查）製成 `native-layout0026-root.img`（`c5cac89a...b678b`）並部署。
  - 跑 `native-layout0026-hyb1-user-boot-1`（`native-hyb1-user-loader`，使用者手動操作 671 秒）。
  - 離線實作 `kernel-backlight-dsi.patch`（`fuxibacklight=4`，`FuxiDsiBacklight.cpp` 發布 `AppleARMBacklight` 服務並發送 DCS 0x51，當時未寫入手機）。
- **復現**：`deploy-layout0026.sh` -> `fastboot boot` 跑 `hyb1-user` -> 以 `hyb-census.py` 解析。
- **主機測試**：`stage-test-phonebase.sh`
- **驗證紀錄**：`backboardd` T1 671 s 共 353 萬筆 draw：
  - **79.0% 符合 L1 快速通道結構資格**，唯一排除原因為 framebuffer fetch（21%），0 筆 tess/mesh/runtime steps。
  - 每 10 秒視窗使用 5–127 個 pipeline，top 12 覆蓋 67%，同上筆 pipeline 僅 19%。
  - m2v 內每筆 draw **37.4 µs** = pre 2.5 + settle 5.4 + inst 3.7 + **rest 24.8** + post 1.0；兩筆之間 29.7 µs。
- **參考來源**：continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：P1 達標，確立進入 P2（L1 快速通道）並細分 `rest`（補丁 `0011`）。
- **出處**：`continuation.md:3100-3250`，`PLAN.zh-TW.md:75-86`，`kernel-backlight-dsi.patch:1-40`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-030 `native-layout0027` 部署（m2v 補丁 0011 細分 `rest`）

- **日期**：2026-11-14 23:15 ～ 23:55
- **狀態**：實驗：細分 `rest` 與修復日誌截斷；86.6% 合格、29.6 µs/draw，`sweetprof` 指明 `Recording::record` 與每 encoder 成本
- **開關**：`sweetprof`
- **問題／現象**：細分 P1 中佔比最大的 `rest`（24.8 µs），並解決長日誌行在 `boot.log` 被切斷的問題。
- **根因**：單靠 `rest` 總數無法判斷該快取哪個步驟；且多欄位寫入同一行超過緩衝區導致截斷。
- **做法**：拉回手機現況後換上 m2v 補丁 `0011`（CI 43609675593，bridge `66fdf295...`）製成 `native-layout0027-root.img` 並部署（phone delta 30 blocks，READBACK_MATCH）。執行 `native-layout0027-hyb1-p1-boot-1`（23:51，`native-hyb1-p1-loader`，`kernel-page1-artifact`，@3，420 s）。
- **復現**：部署 `0027` -> `fastboot boot` 跑 `hyb1-p1` -> `hyb-census.py` + `draws-census.py` + `sweetprof`。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：日誌不再被切斷；T1 2.26 M draws（6041/s），合格 86.6%，**29.6 µs/draw**（pre 1.9 + settle 3.7 + inst 2.6 + **rest 20.5** + post 0.8）。`rest` 細分：`rec 9.6, bind 3.0, uses 2.2, instance 1.1, tables 1.1, fixed 0.9, pipe 0.8, notices 0.8, shadows 0.6, heap 0.4`。`sweetprof` 顯示 `Recording::record` 約 3.7 µs/draw（內含 Turnip `vkCmdDrawIndexed` ~2 µs），且每個 encoder 平均僅 ~3 筆 draw（32 RP/job），證明每 encoder 開銷亦大。
- **參考來源**：Turnip、continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：採用（鎖定 `rec`、`bind` 與 `pre`/`settle` 進一步細分）。
- **出處**：`continuation.md:3251-3320`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-031 `native-layout0028` 部署（m2v 補丁 0012 細分 `pre`/`settle`/`rec`）

- **日期**：2026-11-15 00:10 ～ 00:45
- **狀態**：實驗：細分三大階段比例，`rec` 佔 11.9 µs（`record` 6.8 µs），CA 平均每個 encoder 僅 ~3 筆 draw
- **開關**：無
- **問題／現象**：將 `pre`、`settle`、`rec` 再細分至函式層級，精準定位 L1 第一刀的目標。
- **根因**：需要各子步驟比例作為 P2 刀法依據。
- **做法**：`pull-phone-20261115.sh` 拉回 `0027` 後手機狀態（sha256 `dc5d74744ccf0b3dd3194492eabc643b7f49ffd2b4b6cef9825ed76f7cd39b6a`），換上 m2v 補丁 `0012`（CI 43614089135，bridge `921b1fcc...`）產出 `native-layout0028-root.img`（sha256 `ae99988992d079ea6cdccd16f4618f9ff357e82b4149d8146d6eb445ca3ae282`），以 `chain-0028.sh` 部署（19 blocks）並執行 `native-layout0028-hyb1-p1-boot-1`。
- **復現**：`hybrid-driver\chain-0028.sh`。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：因 26 個計時 mark 本身增加開銷，總時間升至 35.7 µs/draw（81.8 Hz，6.64 ms CPU/frame），依比例分析：`pre 2.8`（settings 1.1, tables 1.0）、`settle 4.8`（refresh 2.2, shape 1.0, lookup 0.9）、`rec 11.9`（Turnip 錄製 `record` 6.8, retain 2.1, tail 1.5, setup 0.8）。讀碼發現每次 draw 都鎖定資源 ID 對每個 live sampler 做 Weak 升級與比較。
- **參考來源**：Turnip、continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：採用數據，設計 `0013`（sampler 清單世代快取）。
- **出處**：`continuation.md:3321-3436`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-032 `native-layout0029` 部署（m2v 0013 `fuxihyb=2` sampler 快取）

- **日期**：2026-11-15 01:33 ～ 02:23
- **狀態**：實驗：快取 argument sampler 清單與位址掃描；但每 draw 僅 2.0 sampler 且開機漂移大於微調收益
- **開關**：`fuxihyb=2`、`fuxihyb=3`
- **問題／現象**：實作 L1 第一刀（`fuxihyb=2`）：快取 argument sampler 清單世代、跳過重複 `lists()` 比對，以及 `settle_shape` 優先比對 Arc 位址。
- **根因**：前述普查與讀碼認為 sampler 清單與 shape 查找具最佳化空間。
- **做法**：`pull-phone-20261115b.sh` 拉回手機（sha256 `30451ef70c93f167b4315a77d64bf042b948ea4f5191ce2c947096e6065e8d83`），換上 m2v 補丁 `0013`（CI 43621663423）產出 `native-layout0029-root.img`（sha256 `29d6d426cde2fd5c5336cf4ae522a9f996e0306597221d650c29a97adca53c3b`），`chain-0029.sh` 部署（20 blocks）後跑 `hyb1-p1`、`hyb2-p1`、`tour-hyb2`。
- **復現**：`hybrid-driver\chain-0029.sh`。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：`hyb2` 顯示 `samplerskept` 達 89%，但每筆 draw 平均僅 2.0 個 argument sampler，收益極小；且跨開機漂移（0028 hyb1 35.7 vs 0029 hyb1 31.7 µs）大於單刀微調收益。
- **參考來源**：continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：促使建立單次開機內交替 A/B 之 `fuxihyb=3`（補丁 `0014`）。
- **出處**：`continuation.md:3437-3470`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-033 `native-layout0030` 部署（m2v 0014 set-0 重用 + `fuxihyb=3`）

- **日期**：2026-11-15 02:44 ～ 03:14
- **狀態**：實驗/停損：`fuxihyb=3` 單次開機 A/B 實測 set-0 重用淨損失 +0.59 µs (+1.8%)，決定移除
- **開關**：`fuxihyb=3`、`sweetprof`
- **問題／現象**：於 lane 下重用 set-0 descriptor set（`DescriptorArena` 快取最近 32 組），並新增 `fuxihyb=3` 交替切換 `lane=0|1` 以消除開機漂移。
- **根因**：`0029` 觀察到 `set0same` 約 22–23%，嘗試快取 set-0 減少配置與寫入。
- **做法**：`pull-phone-20261115c.sh`（sha256 `82808d65f673a6beb8fc595221e282f0203431a1400dae485647730a30d52f30`），換上 m2v 補丁 `0014`（CI 43628206211）產出 `native-layout0030-root.img`（sha256 `fa8d08df7e148019d82cb9f94b09f148f8fb29cf5a064dfba544b8cd0c45a07a`），`chain-0030.sh` 部署（19 blocks）並跑 `native-layout0030-hyb3-p1-boot-1`（590 s）。
- **復現**：`hybrid-driver\chain-0030.sh`。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：單次開機 A/B（off 15 視窗 / on 16 視窗）：tot off 35.11 -> on 35.87 µs/draw（**+0.76 µs**），同 pipeline 配對 **+0.59 µs (+1.8%)**，`rest.bind +0.82`。因每筆 draw 都將寫入攤平為 `Vec` 並線性比對 32 組，反成淨損失。同時 `sweetprof` 確認 `tu_CmdBindPipeline` 與 `tu_CmdDrawIndexed` 為 Turnip 最大宗（L2 目標）。
- **參考來源**：Turnip、continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：**`fuxihyb=3` 單次開機 A/B 方法採用；set-0 descriptor 重用停損移除**。
- **出處**：`continuation.md:3471-3505`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-034 `native-layout0031` 部署（m2v 0015 移除 set-0 + `calls` 普查）

- **日期**：2026-11-15 03:44 ～ 04:12
- **狀態**：實驗：發現 100% encoder 都在記錄 rebind call log（107,077 次），而 NativeMetal 前端從不 rebind
- **開關**：無
- **問題／現象**：移除 set-0 重用，並量測 `setters`、`render_command_encoder`、`end_encoding` 與 `commit` 耗時。
- **根因**：需釐清 encoder 邊界與 setter 呼叫的真實成本。
- **做法**：`pull-phone-20261115d.sh`（sha256 `01b94862eae27aa5188f41dbb5f0fa51c67e489aa0fb09dff5432e73e5b88409`），換上 m2v 補丁 `0015`（CI 43633647543）產出 `native-layout0031-root.img`（sha256 `634fac4f880902781ad80e8b57de83865ece22277f75f84d4b899b23d9ddaa54`），`chain-0031.sh` 部署（20 blocks）後跑 `hyb3-p1-boot-1`。
- **復現**：`hybrid-driver\chain-0031.sh`。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：`calls` 普查揭露驚人事實：**`logging encoders 107077 = 100% 每個 encoder`**。因行程內存在 host pages 上的 buffer，m2v 為支援 rebind 而對每個 encoder 記錄每次呼叫（含大量 Arc clone 與清單 push），但 `NativeMetalObjects.h` 明載 NativeMetal 前端從不 rebind。
- **參考來源**：continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：採用診斷結果，直接導出補丁 `0016` 大幅降本。
- **出處**：`continuation.md:3506-3528`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-035 `native-layout0032` 部署（m2v 0016 關閉 call log/rebind）

- **日期**：2026-11-15 04:29 ～ 04:53
- **狀態**：修復：lane 下關閉 call logging 與拒絕 rebind 並延後重設 pool，同 pipeline 淨省 -4.49 µs/draw (-13.8%)
- **開關**：`fuxihyb=2|3`、`fuxihyb=3`
- **問題／現象**：於 lane（`fuxihyb=2|3`）關閉 encoder call logging、在 `buffer_descriptor.rs` 拒絕 rebind，並將 command pool reset 移至完成執行緒；同時修正 `fuxihyb=3` 改用 `xorshift64*` 隨機切換避免與腳本 18.7 s 週期產生混疊。
- **根因**：無謂的 rebind 記帳與主執行緒 pool reset 浪費大量 CPU。
- **做法**：`pull-phone-20261115e.sh`（sha256 `134f5b2b44a044c6b19d4284d89b1bed5abdd5f0cc71e298f6ba7bdd4487c514`），換上 m2v 補丁 `0016`（CI 43637621979）產出 `native-layout0032-root.img`（sha256 `4d23c56b59a22b89871b916a039fbe5d16bd8cae2db40ee43c70d395b4d70e68`），`chain-0032.sh` 部署（20 blocks）後跑 `hyb3-p1-boot-1`。
- **復現**：`hybrid-driver\chain-0032.sh`。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：`fuxihyb=3` 隨機 A/B 獲得明確大勝：
  - tot off 34.97 -> on 30.68 µs/draw（**-4.29 µs**），同 pipeline 配對 **-4.49 µs (-13.8%)**，between draws -2.00，setters -0.77。
  - encoder open 26.3 -> 17.9 µs，end 73.9 -> 69.0 µs，commit 307 -> 279 µs。
- **參考來源**：continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：採用（確立 L1 核心收益）。
- **出處**：`continuation.md:3529-3560`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-036 `native-layout0033` 部署（m2v 0017 `fuxihyb=4` 日常靜音）

- **日期**：2026-11-15 05:23 ～ 05:54
- **狀態**：修復：無普查 A/B 確認 `hyb4` 達 87.1 Hz / 5.58 ms CPU（vs 基線 81.2 Hz / 6.68 ms，-16.5% CPU）
- **開關**：`fuxihyb=4`、`sweetprof`
- **問題／現象**：新增 `fuxihyb=4`（開啟 lane 但關閉耗時的普查計時標記），以無普查開銷的 A/B 測試實測真實 Hz 與 CPU 幀時間提升。
- **根因**：普查的 `Timespec::now` 呼叫本身在 `sweetprof` 佔顯示執行緒 10–20% 樣本，需關閉普查方能測量真實效能。
- **做法**：`pull-phone-20261115f.sh`（sha256 `6f622a275fcbcca15f0e260ad185ba1f031a9777372288f3c2d54591b44d7a9c`），換上 m2v 補丁 `0017`（CI 43642453133）產出 `native-layout0033-root.img`（sha256 `ce61614fc9768fbafe64f5225841fdd63a58c316bb2f751bfd1d9c689ba8db9b`），`chain-0033.sh` 部署（19 blocks）並跑 `hyb3-p1` 與無普查 A/B（`p1-vt6tc` vs `hyb4-p1` 420 s）。
- **復現**：`hybrid-driver\chain-0033.sh`。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：  - `hyb3-p1`：tot off 36.23 -> on 29.17 µs/draw（-7.05），同 pipeline -6.95 µs（-20.6%）。
  - 無普查 A/B：`p1-vt6tc`（無 lane）**81.2 Hz**、CPU 54.3%（**6.68 ms/frame**、66.4 µs/draw） vs `hyb4-p1`（`fuxihyb=4`）**87.1 Hz**、CPU 48.6%（**5.58 ms/frame**、-16.5% CPU、55.7 µs/draw）。
- **參考來源**：continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：採用（`fuxihyb=4` 取代原本無參數路徑成為新日常／實驗基準）。
- **出處**：`continuation.md:3561-3594`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-037 `native-layout0034` 部署（m2v 0018 延後釋放 leftover）與 `vt` A/B

- **日期**：2026-11-15 06:37 ～ 09:10
- **狀態**：修復：延後 25 個欄位釋放省 ~1.7 µs/encoder；tour 10 張截圖正常；vsync offset 4000 中性、2000 掉 DeviceLost
- **開關**：`fuxivsynctoff`
- **問題／現象**：將 `end_pass` 結束時不需要讀取的 25 個 encoder 欄位保留至完成執行緒釋放（省去 `end` split p13 11.2 µs）；並以 tour 截圖確認 `hyb4` 視覺正確；同時重測 `fuxivsynctoff` 6000/4000/2000。
- **根因**：`end_pass` 結束 drop 大量 Rust 欄位佔用主顯示執行緒時間；又 87.1 Hz 下 `CPU 5.58 + GPU 4.27 + copy 0.6 = 10.45 ms` 貼近 `vt6` 的 10.54 ms 期限，需測更早相位。
- **做法**：刪除舊備份 `20261114`–`20261115f` 清出 20 GB 空間；`pull-phone-20261115g.sh`（sha256 `40272c6e5b7203223b85d57e8f3c837481d8ff8d968e558f93e61977fde71cc7`），換上 m2v 補丁 `0018`（CI 43648986405）產出 `native-layout0034-root.img`（sha256 `31edbabea35924eb8887d3a44eac9f5bc9476d5ddb277aa9475b3007a8fd1ee1`）部署。跑 `hyb3-p1`、`tour-hyb4` 與 `vt-ab-0034b.sh`（offset 4000 / 2000）。
- **復現**：`chain-0034.sh`、`vt-ab-0034b.sh`。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：  - `hyb3-p1`：end 78.7 -> 67.0 µs（0018 貢獻 ~1.7 µs/encoder），同 pipeline -16.4%。
  - `tour-hyb4-boot-1`：取得 10 張截圖（鎖屏、主畫面、App 資源庫、今日、Spotlight、控制中心、設定、一般、關於），目視全部正確無破圖。
  - `vt` A/B：6000（86.4 Hz）vs 4000（86.5 Hz）完全中性（顯示執行緒待機多在 run loop 等 app 新畫面）；2000 在 ~85 s 發生 **Vulkan DeviceLost** 退回 CPU。
- **參考來源**：continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：`0018` 採用；`fuxivsynctoff` 維持 `6000`，不再使用 `2000`。
- **出處**：`continuation.md:3595-3689`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-038 `native-layout0035` 部署（nativemetal 0016 `fuxisplit=1500`）

- **日期**：2026-11-15 09:44 ～ 10:45
- **狀態**：實驗：分段 commit 降低顯示延遲 ~2.4 ms 且渲染逐像素相同，但平均 Hz 中性（84.7 Hz），留作選項
- **開關**：`fuxisplit=<us>`、`fuxisplit=1500`、`fuxisplit`
- **問題／現象**：打破單一幀 `CPU 5 ms -> GPU 4 ms -> copy 0.6 ms` 的完全串行，讓編碼過半時即提前切段送交 GPU 並行執行。
- **根因**：CA 將整個影格（~11 個 encoder）錄入單一 command buffer 並於最後才 commit，導致 GPU 全程閒置等待 CPU 編碼結束。
- **做法**：`pull-phone-20261115i.sh`（sha256 `1be16a690c86b17717ee6d1c854a3d7dced39acbe929cb379efe92c5c99ab46a`），加入 NativeMetal 補丁 `0016`（`nmb_command_buffer_split`，開機參數 `fuxisplit=<us>` 啟用，預設關閉，CI 43663211587）產出 `native-layout0035-root.img`（sha256 `1f2f29845a08befdbdacde696d333b2d17d02b9dc55a63ed10a8e18f3c2053f3`）部署，以 `fuxisplit=1500` 跑 `hyb4-split` 與 `tour-split`。
- **復現**：`chain-0035.sh`。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：`FUXI SPLIT: parts=36000 command_buffers=18005 refused=0 failed=0`（平均每幀切 2 段、零失敗），tour 截圖與 `0034` 逐像素一致；`copy_submit->fence` 降至 2289 µs（vs 4284），顯示延遲少 **~2.4 ms**，但平均 Hz 仍為 **84.7 Hz**（中性），剩餘掉幀原因為該幀 CA 自身 CPU 編碼超時 1–2 ms。
- **參考來源**：continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：保留 `fuxisplit` 作為延遲選項，暫不預設開啟；推進 `0036` 逐幀診斷釐清重幀內容。
- **出處**：`continuation.md:3649-3741`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-039 `native-layout0036` 部署（`FUXI FRAME` + `passes` 普查）

- **日期**：2026-11-15 10:46 ～ 11:30
- **狀態**：實驗：確認重幀（132.7 draws / 15.1 passes / 8.0 ms encode）超越 8.33 ms 預算；SpringBoard 93% 閒置
- **開關**：`sweetprof=10090`
- **問題／現象**：實作逐幀 `FUXI FRAME` 診斷（encode 時間、gap 週期、資源建立耗時）與 render encoder pass 結構普查，並採樣 SpringBoard 主執行緒排除其為瓶頸的可能。
- **根因**：重負載影格單純 CPU 編碼就超過 8.33 ms TE 預算。
- **做法**：`pull-phone-20261115j.sh`（sha256 `068e9ee3c3f409af6ff4cc59f466a67e08edfe5bd125888d530ccc12bba381bb`），加入 NativeMetal 補丁 `0017`（`FUXI FRAME`）與 m2v 補丁 `0019`（`passes` 普查，CI 43666120640）產出 `native-layout0036-root.img`（sha256 `c1db260b...0d5f`）部署。跑 `hyb3-p1`、`hyb4-p1-boot-2`（重跑）與 `hyb4-sbprof`（`sweetprof=10090` 採樣 SpringBoard）。
- **復現**：`chain-0036.sh`、`rerun-0036-hyb4.sh`、`frame-summary.py`。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：  - SpringBoard 主執行緒 `t243` **93% 在 run loop 等候**（非瓶頸）。
  - 重幀（>= 6 ms，n=5660）平均編碼 **7964 µs**、**132.7 draws**、**15.1 passes**、make 187 µs（30% 建紋理）；輕幀（n=10249）2413 µs、60.8 draws、7.2 passes。
  - `passes` 普查顯示每 encoder 有 0.39 個 reach clear pass，且 97.7% encoder 的 color attachment 大於 render area。
- **參考來源**：continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：採用診斷框架（`FUXI FRAME` 留存後續所有測試基準）。
- **出處**：`continuation.md:3697-3760`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-040 `native-layout0037` 部署（m2v 0020 `fuxihyb=5`/`6` 邊條 reach clear）

- **日期**：2026-11-15 11:48 ～ 12:57
- **狀態**：實驗/停損：邊條 clear 全數命中但表面極小（~10K px），Hz 與 `gpu_ms` 中性（86.1 vs 86.9 Hz），日常維持 `hyb4`
- **開關**：`fuxihyb=5`、`fuxihyb=4`
- **問題／現象**：Metal `Clear` load 需清至 32 px tile 邊界（reach），m2v 原先另開一個涵蓋整塊 reach 的 render pass 再 LOAD；改為僅對 render area 外的「邊條」開小 pass 清、主 pass 保留 `CLEAR`（`fuxihyb=5` 關閉普查、`6` 開啟普查）。
- **根因**：期望省下 GPU 整塊 GMEM clear+store 與 area load 的頻寬與時間。
- **做法**：`pull-phone-20261115k.sh`（sha256 `c8f6519542b660e1d3a630faa194f76a6a4defb2c445363bae7926befc2f70b1`），加入 m2v 補丁 `0020`（CI 43672582848）產出 `native-layout0037-root.img`（sha256 `ed0dd50339b749974fc2ccc00c9184be7a617627445a24a04fcd01729c6354df`）部署，跑 `tour-hyb5`、`hyb5`、`hyb4`、`hyb6`。
- **復現**：`chain-0037.sh`、`rerun-0037-tour.sh`。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：95331/95331 個 reach clear 全走邊條且渲染正確；但每個 reach clear 僅約 ~10K px（小表面），對 GPU 無明顯增益：**hyb5 86.1 Hz vs hyb4 86.9 Hz**（`gpu_ms` 同為 4.28 vs 4.25 ms，中性），且 `hyb6` 顯示 CPU end p7 微增。
- **參考來源**：continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：**停損（日常維持 `fuxihyb=4`，`5`/`6` 僅保留於碼中不啟用）**。
- **出處**：`continuation.md:3761-3812`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-041 `native-layout0038` 部署（nativemetal 0018 `fuxihyb=7` 延遲編碼 worker）

- **日期**：2026-11-15 13:40 ～ 14:50
- **狀態**：實驗：`hyb7` 達 87.2/87.8 vs `hyb4` 85.3 Hz（中性偏正），因 CA commit 仍需 drain 等待 80% m2v 時間而無跨幀重疊
- **開關**：`fuxihyb=7`、`sweetprof`、`fuxihyb=8`
- **問題／現象**：將 `backboardd` 顯示執行緒開啟之 render encoder 透過 FIFO 佇列交給背景 worker 執行緒（`fuxi.deferred-render`）執行 `metal2vulkan`（`fuxihyb=7`），讓 CA/前端組樹與 m2v 錄製重疊。
- **根因**：`0037 hyb4` `sweetprof` 顯示顯示執行緒約 49% 時間花在 m2v（與 CA 完全串行）。
- **做法**：`pull-phone-20261115l.sh`，加入 NativeMetal 補丁 `0018`（`FuxiDeferred.rs`，CI 43681679209，限 `backboardd` + `coreanimation.display` 執行緒，遇到其他 bridge 呼叫才 `drain` 等 worker 完成），部署 `native-layout0038` 並跑 `tour-hyb7`、`hyb7` ×2 與 `hyb4`。
- **復現**：`hybrid-driver\chain-0038.sh`，以 `worker-stats.py` 解析。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：`tour-hyb7` 渲染正確；`hyb7` **87.2 / 87.8 Hz**（84/85% 單週期）vs `hyb4` **85.3 Hz**（79% 單週期），中性偏正但溫度高 2–4 °C（72.2–74.6 °C）。`worker-stats` 揭露問題：因 CA 呼叫 `commitAndWaitUntilSubmitted` 且 `swap wait` 關閉，顯示執行緒在 `on_completed` drain 等掉 worker 約 80% 的 m2v 時間（每次 ~2.37 ms），未能形成跨幀重疊。
- **參考來源**：metal2vulkan、continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：過渡至 `0041`（`fuxihyb=8` 重疊 commit）的必要基礎。
- **出處**：`continuation.md:3813-3875`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-042 `native-layout0039` 部署（`fuxi-bridge-lse-builder` `+lse`）

- **日期**：2026-11-15 15:06 ～ 16:25
- **狀態**：實驗：導入 ARMv8.1 LSE 原子指令後整體 Hz 中性（84.4/86.3 vs 86.4 Hz），`hyb7` 首度出現 1/5 偶發 DeviceLost
- **開關**：`fuxihyb=8`
- **問題／現象**：原本 bridge/runtime/translator 針對 `aarch64-apple-ios`（apple-a7 ARMv8.0）編譯，內含 9396 個 `LDXR`/`STXR` LL/SC 迴圈、0 個 LSE 原子指令；改以 `-C target-feature=+lse` 編譯測試收益。
- **根因**：ARMv9 核心支援 EL0 LSE 原子指令，且 `retain_draw` 等 Arc 計數佔 m2v 時間 ~6%。
- **做法**：`pull-phone-20261115m.sh`，加入 harness `patches/fuxi-bridge-lse-builder.patch`（CI 43682227587，要求產物 >1000 LSE 原子指令）部署 `native-layout0039`，跑 `hyb4` ×2 與 `hyb7` ×2。
- **復現**：`hybrid-driver\chain-0039.sh`、`tools\lse-count.py`。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：`retain_draw` 樣本數減少 25%，但整體 Hz **中性**（`hyb4` 84.4/86.3 Hz vs 0038 85.3 Hz；`hyb7` 86.4 Hz vs 0038 87.2/87.8 Hz）。`hyb7-p1-boot-1` 發生 **1 次偶發 Vulkan DeviceLost**（邊緣上滑 `asynchronous` 執行緒首呼 bridge 時觸發，核心端無 fault/timeout，機率約 1/5）。
- **參考來源**：continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：保留 `+lse` 建置設定作為基底；推導出針對 `hyb7` commit 等待的 `fuxihyb=8` 構想與 `0021` 錯誤名補丁。
- **出處**：`continuation.md:3841-3953`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-043 `native-layout0040` 部署（Hybrid Turnip `hyb4t` + nativemetal 0019）

- **日期**：2026-11-15 15:06 ～ 17:53
- **狀態**：實驗：消除每次 `vkCmdBindPipeline` 10096 bytes `memmove`，`memmove` 佔比降但 Hz 中性（85.2 vs 85.4 Hz）
- **開關**：`fuxitudebug=`
- **問題／現象**：消除 Turnip 每次 `vkCmdBindPipeline` 複製 `10096 bytes`（`0x2770`）`tu_program_state` 結構體的開銷（約佔 worker m2v 時間 ~14%）。
- **根因**：Turnip `tu_cmd_state::program` 為內嵌結構體而非指標。
- **做法**：  - `patches/mesa-hyb-program-pointer.patch`（Turnip CI 43688079557，`fuxi-turnip-hybrid`）：將 `program` 改為 `struct tu_program_state *`（非 const）。
  - NativeMetal 補丁 `0019`（CI 43687954922）：當 `fuxitudebug=` 含 `hyb` 旗標（如 `nocb,hyb`，避免超過 608-byte boot-args 上限）改載入 `/usr/local/lib/libvulkan_freedreno_hyb.dylib`。
  - `pull-phone-20261115n.sh` 部署 `0040`，跑 `tour-hyb4t`、`hyb4t` ×2 與 `hyb4` ×2。
- **復現**：`hybrid-driver\chain-0040.sh`。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：`memmove` 剖面佔比確實下降（13.0% vs 15.2%），且反組譯確認 `0x2770` 複製已消失；但平均 Hz **完全中性**（`hyb4t` **83.2 / 87.1 Hz** mean 85.2 vs `hyb4` **84.9 / 85.9 Hz** mean 85.4），無 DeviceLost。
- **參考來源**：Turnip、continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：**`libvulkan_freedreno_hyb.dylib` 留存映像內，預設仍維持原版 Turnip（`hyb4`）**。
- **出處**：`continuation.md:3876-3912`, `4054-4059`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-044 `native-layout0041` 部署（nativemetal 0020 `fuxihyb=8` + 0021 錯誤名）

- **日期**：2026-11-15 16:30 ～ 19:30
- **狀態**：實驗：實作重疊 commit + pending writer lease，但因 QuartzCore `addScheduledHandler:` 閘門僅 3.4% 重疊
- **開關**：`fuxihyb=8`、`fuxiswapwait=0`
- **問題／現象**：實作 `fuxihyb=8`（在 `hyb7` 上讓顯示執行緒 commit 立即返回，改取核心 pending writer lease 擋住過早 present），讓兩幀之 CA 與 worker 真正並行；同時加入 `0021` 印出 DeviceLost 詳細拒絕原因。
- **根因**：實測發現 `hyb8` 僅 **3.4%** 的 commit 走重疊（785 / 22800），主因 QuartzCore `MetalContext::delete_image` 在編碼中有 current command buffer 時都會掛 `addScheduledHandler:`，而 `0020` 閘門一遇 scheduled handler 就退回同步等待。
- **做法**：  - NativeMetal 補丁 `0020`（`nativemetal-fuxi-0020-overlap-commits.patch`）：`commit` 時對 `_surfaces` 每個不重複 IOSurface 取 `IOSurfaceLock(s, kIOSurfaceLockAvoidSync)` pending lease，並把 `on_completed`/`on_scheduled`/`commit` 排入 worker 佇列立即返回；`didSchedule` 時釋放 lease；需要 `fuxiswapwait=0` 配合。
  - NativeMetal 補丁 `0021`（CI 43697223049）：`nmb_command_buffer_error` 印出 `{why:?}: {refusal}`。
  - `pull-phone-20261115o.sh` 部署 `0041`，跑 `tour-hyb8`、`hyb8` ×2、`hyb7` ×2。
- **復現**：`hybrid-driver\chain-0041.sh 43697223049`；以 `objcdis.py --sel addScheduledHandler:` 查明原因。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：`tour-hyb8` 14 張截圖全部正確；`hyb8-p1-boot-1` 測得 **88.5 Hz**（85% 單週期，無 DeviceLost），`hyb7` 84.5 / 85.8 Hz（無 DeviceLost）；但因重疊率僅 3.4%，尚未真正測到重疊效應。
- **參考來源**：continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：確認需放寬 `addScheduledHandler:` 閘門（導出 `0043` 的補丁 `0023` `ovsh`）。
- **出處**：`continuation.md:3954-3987`, `4077-4110`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-045 `native-layout0042` 部署（nativemetal 0022 預熱 + IOMFB analytics）

- **日期**：2026-11-15 17:20 ～ 22:35
- **狀態**：修復：冷啟動首次開啟 late 幀大減 ~90%（29 vs 279）；`hyb8k` 消除每幀 3 次失敗 analytics IPC，兩者均 Hz 中性
- **開關**：`fuxitudebug=`
- **問題／現象**：(1) 解決使用者回報「每次冷啟動首次開 App 資源庫、app、控制中心必卡頓」；(2) 移除 `AppleCLCD` 缺乏屬性導致 `kern_SwapEnd` 從第 62 次 swap 起每幀發出 3 次失敗 analytics IPC 的開銷。
- **根因**：  - 冷啟動卡頓：CA 於首次繪製時在顯示執行緒臨時建置 14–33 個 render pipeline（30–75 ms），即使命中 m2v/Turnip 磁碟快取仍需 ~1.8 ms/個；先前 12 次開機共僅使用 174 個不同描述子。
  - Analytics IPC：`AppleCLCD` 缺少 `underrun`、`decomp_fail`、`downscale_fail` 屬性，導致計數不歸零，每幀觸發 3 次失敗 `IORegistryEntrySearchCFProperty` IPC（估 60–150 µs/frame）。
- **做法**：  - NativeMetal 補丁 `0022`（CI 43698287714）：CA 建首個 pipeline 後由背景 utility queue 讀取本次開機前之 `pipelines.log` 最後 4 MiB，直接呼叫 bridge `nmb_render_pipeline_new_with_options` 預建最多 400 個歷史管線並保留弱參考 memo；`fuxitudebug=` 加 `nopw` 可關閉（`hyb8n`）。
  - 核心補丁 `kernel-iomfb-analytics.patch`（`kernel-iomfb-artifact`，CI 43701482816）：`AppleCLCD::start` 發布 3 個屬性為 0 並於 sel 68 記錄 `n68` 計數。
  - `pull-phone-20261115p.sh` 部署 `0042`，跑 tour 預熱開／關（`hyb8` vs `hyb8n`）與 p1 `hyb8`、`hyb8n`、`hyb8k`。
- **復現**：`chain-0042.sh 43698287714` -> `post-0042.sh`（抽取 `pipelog-0042/pipelines.log`）。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：  - 冷啟動預熱：`tour` 首次開啟「今日、Spotlight、控制中心、時鐘」之 **late 幀總計由 279 降至 29（約少 90%）**，其中 Spotlight 最顯著（143 -> 22）。p1 乾淨視窗 Hz 中性（`hyb8` 88.0/89.1 vs `hyb8n` 89.3 Hz）。
  - IOMFB analytics：`hyb8k` `SWEET FB: ... n68` 成功達 512，`SwapEnd>SearchCFProperty` 從 20.6% 降至 **0%**（另發現剩餘 12.7% 來自 `Brightness_Scale`），p1 乾淨視窗 **89.4 / 88.5 Hz**（Hz 中性）。
- **參考來源**：Turnip、continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：補丁 `0022` 預熱與 `kernel-iomfb-analytics.patch` 均有效採用。
- **出處**：`continuation.md:3994-4076`, `4191-4215`, `xiaomi13-cold-start-prewarm.md:1-25`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-046 `native-layout0043` 部署（`hyb8s`/`hyb8i`/`tourl`）與 **0043 停損結論**

- **日期**：2026-11-15 22:47 ～ 2026-11-16 00:46
- **狀態**：停損/結論：`hyb8s` 重疊 97%、`hyb8i` 中斷睡眠皆 88.1–88.5 Hz；`4+` 空檔屬渲染上游限制（腳本上限 ~90.5），建議不做 P3
- **開關**：`fuxitudebug=...,ovsh`、`fuxigpuncimport=69514824`
- **問題／現象**：實測高重疊率（`hyb8s`）、GPU fence 中斷睡眠（`hyb8i`）與修正主畫面座標導覽（`tourl`），並依預定規則進行 **P3（Turnip 直出 PM4）停損判定**（≥95 Hz 繼續；≤90 Hz 交使用者決定停損或做 P3）。
- **根因**：`0041` 顯示 `hyb8` 重疊率低、`backboardd` queue-wait 卡在每 100 µs 輪詢 WaitFence（avg 3739 µs、25 次輪詢）；又自 `0022` 起舊 tour 座標已與使用者桌面不符。
- **做法**：  - NativeMetal 補丁 `0023`（CI 43706249664）：`fuxitudebug=...,ovsh`（`hyb8s`）允許含 `addScheduledHandler:` 之 commit 仍走重疊路徑（因 `delete_image` handler 僅 `dispatch_async` 釋放 3 個物件，任意執行緒皆安全）。
  - `hyb8i`（`native-hyb8i-p1-loader`）：`fuxigpuncimport=69514824` 開啟 bit 26（67108864）GPU fence 中斷睡眠（INTID 332）。
  - `kernel-tourl-artifact`（CI 43718699612）：更新 `fk_tour` 主畫面與 app 座標跑 16 張截圖。
  - 以 `20261115q`（sha256 `4cced778...`）部署 `0043`，跑 `tour-hyb8s`、`hyb8s` ×2、`hyb8` ×2、`hyb8i` ×2 與 `tourl-hyb8s`（720 s）。並以 `p1-clean.py` 排除誤滑出 Spotlight（51–53 Hz）之視窗計算乾淨 Hz。
- **復現**：`hybrid-driver\chain-0043.sh 43706249664`，結束後執行 `tools\analyze-p1.sh`。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：  - 六趟 p1 乾淨視窗 Hz 全部 ≤90 Hz（彼此差在 ±0.4 Hz 雜訊內）：
    - **`hyb8`**：**88.1 / 88.5 Hz**（重疊 3.4%，最高溫 70.6 / 74.2 °C）
    - **`hyb8s`**：**88.2 / 88.5 Hz**（重疊 **96.8 / 96.9%**，最高溫 74.9 / 73.8 °C）
    - **`hyb8i`**：**88.4 / 88.1 Hz**（重疊 3.3 / 3.2%，中斷 ~2.4–2.5 萬次／零競態，最高溫 71.0 / 71.0 °C）
  - 瓶頸拆解（`gap-time.py`、`shown-rate.py`）：
    - 螢幕實際輸出比 MTL Hz 少約 3.5%（`replaced`，hyb8 約 84.6 Hz）。
    - `4+` 空檔（29–50 ms）從 `0036 hyb3` 到 `0043 hyb8s` 固定約 **930 次**（佔 10–17% 時間）且與腳本每步結束同步，與渲染無關，故 `@3` 腳本物理上限約 **90.5 Hz**。
    - 肉眼頓點源自約 1600 個準時 commit 但 worker 編碼＋GPU 超出 TE 的中／重幀（encode ≥6 ms 重幀約 440 個，平均 8.4–10.7 ms，其中約一半耗在建材質／IOSurface import，85% 建材質、65% import）。
  - `tourl-hyb8s`：Messages、App Store 正常；Photos 因 `assetsd` sig 6 崩潰黑畫面，Phone 無崩潰但黑畫面（325 幀）。
- **參考來源**：Turnip、continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：**0043 停損（建議不做 P3）**——P3 僅能省每筆 draw 錄製，對已極快的輕幀與「建材質／import 佔一半」的重幀最多僅拉高 1–2 Hz；GPU 線建議停在 ~88 Hz（螢幕 ~85 Hz）轉做系統可用性。
- **出處**：`continuation.md:4090-4280`, `4291-4303`, `STOPLOSS-0043.zh-TW.md:1-56`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

## 其他

### G-047 `kernel-perf-wide.patch`（`fuxiperf=2` 寬域調頻）

- **日期**：2026-11-15 ～ 11-16
- **狀態**：工具/實驗：將 `fuxiperfmax=720`、`fuxiperfbudget=1000` 整合為單一 `fuxiperf=2` 節省 boot-args 長度；提出 CPU 時脈重測假說
- **開關**：`fuxiperf=2`、`fuxiperfmax=720`、`fuxiperfbudget=1000`、`fuxiperfmax`、`fuxiperfbudget`、`fuxiperfbase=2400`
- **問題／現象**：`p1` 開機參數已接近 608-byte 上限，需單一參數開啟寬域調頻（72 °C / 1000 ms budget），並在 GPU 變快後提出重測 CPU 時脈之假說。
- **根因**：11-14 的 boost 測試是在 GPU 受限（`gpu_ms` 7.5–8.5）時進行；現 `gpu_ms` 降至 4.3 ms 而遲到幀改為 CPU 受限，且開 app 變慢可能與 gold/prime 核心被限在 ~1.4 GHz 有關。
- **做法**：`kernel-perf-wide.patch` 支援 `fuxiperf=2`（自動預設 `fuxiperfmax=720`、`fuxiperfbudget=1000`，明確給定 `fuxiperfmax`/`fuxiperfbudget` 仍優先）。
- **復現**：待後續可用性／CPU 階段在開機參數騰出空間後，以 `hyb8 + fuxiperfbase=2400` 進行 A/B。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：`0043 hyb8-p1-boot-2` 記錄 `fuxiperf=2` 下未升頻 gold 1401 / prime 1708 MHz，升頻視窗 avg gold 1415–1721（max 2323）、prime 1170–1471（max 2592），275 s 腳本內升頻 150–170 s。
- **參考來源**：continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：工具已合入核心；`fuxiperfbase=2400` A/B 待測。
- **出處**：`continuation.md:4281-4290`，`kernel-perf-wide.patch:1-40`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-048 雙系統開機鏈 `chain-v2` 與 `chain-v3` 寫入 `boot_b`

- **日期**：2026-11-16 06:20 ～ 08:50
- **狀態**：修復：將 `fuxibacklight=4`、`fuxi,adsp=3` 與 `fuxi,log=1` 寫入 `boot_b`（修正亮度連滑停止並加入 `mtdblk` 獨立開機日誌）
- **開關**：`fuxibacklight=4`、`fuxi,adsp=3`、`fuxi,log=1`
- **問題／現象**：將 `fuxibacklight=4` 與 ADSP 電池（`fuxi,adsp=3`）合入不接電腦開機的 `boot_b` 鏈（`chain-v2`）；隨後修正使用者實測回報「亮度滑兩下就停」並加入離線開機日誌 `fx_log`（`chain-v3`）。
- **根因**：  - `chain-v2`（06:20–06:35）：`native-dly1-user-loader`（`vt6tc-user` + `debug=12` + `fuxibacklight=4` + `fuxi,adsp=3`，`kernel-adsp-bl5-artifact`），`boot_b` 寫入 `994c95d6b78118ae625bb5919da2c7e416a5644fb403abc655ec4c6a85bcf25d`（KSU 模組 v3）。
  - 使用者獨立開機回報亮度滑兩下停止（舊核心在首次 DCS 傳送未見乾淨完成即全開機鎖死 DSI）且拔線仍顯示 100% 充電中（缺獨立開機日誌）。
  - 修正（`kernel-adsp-bl7-artifact`，CI 43776623221）：ksrc `ba65d38` 改為連續 6 次錯誤才停止 DSI，ksrc `7af418a` 等待 `CMD_DMA_DONE` 最長 20 ms；新增 `fx_log`（ksrc `d96adda`，`fuxi,log=1`）每 10 s 將 console 寫入已蓋章 `mtdblk`（sda19）。
- **做法**：以 `native-dly2-user-loader` 建置 `chain-v3`（`build-chain.py chain-v3 --payload native-dly2-user-loader`，boot.img `2965b2cc...c27c`，66,326,528 bytes），於 08:47 經 `hw000-deploy-bootb-v3.sh` 寫入 `boot_b` 並讀回驗證，KSU 模組升至 v4，保留 `boot_a` 不變。還原點：`fuxi_Backup\boot_b-chain-v2-20261116`（及前次 `boot_b-chain-v1-20261116`）。
- **復現**：`build-chain.py chain-v3 --payload native-dly2-user-loader` -> `fastboot boot` 測試 -> `hw000-deploy-bootb-v3.sh`。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：`bl6t`（~2900 次）／`bl7t`（~3600 次含 8 次拖曳）均無 halt；`native-chainv3-user-loader-boot-1` 測試通過 SpringBoard、ADSP 電池、亮度 1463 發送、0 panic、0 USB 斷線。拔線獨立開機行為待使用者實際操作後以 `fuxi-log.sh pull` 驗證。
- **參考來源**：continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：採用（日常 `boot_b` 鏈推進至 `chain-v3`）。
- **出處**：`continuation.md:4314-4365`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-049 未解決項目（鎖定自訂 DeviceLost、4+ 空檔、重幀材質重用等）

- **日期**：2026-11-14 ～ 2026-11-18
- **狀態**：未完成。已錄到一次 `DeviceLost` 後的顯示程序重啟與保護性停用，首次錯誤的原因待查。
- **開關**：無
- **問題／現象**：彙整截至 `0043` 停損時尚未完全解決或待後續處理之議題：
  1. **鎖定畫面自訂／桌布挑選觸發 Vulkan `DeviceLost`**：11-14 手動 6 趟掉 2 次（皆先有 `AegirPoster`/`PosterBoard` sig 6 崩潰）；11-16 00:51 `tourl-hyb8s` 結束後使用者手動重複開 app 再進鎖定自訂又掉一次，但主機僅錄 `-Seconds 720` 未錄到該段手動操作。
  2. **`4+` 空檔分類**：需於前端依腳本步驟記錄空檔序列，區分「動畫收尾亞像素不動」或「app 端 CPU 慢」。
  3. **重幀材質建置與 IOSurface import 重用**：重幀一半時間花在建立材質與 import，尚未實作可重用快取。
  4. **Usability 畫面問題**：`Photos` 因 `assetsd` sig 6 崩潰黑畫面；`MobilePhone` 無崩潰但黑畫面。
- **根因**：11 月 18 日的 GPU 加速停用發生在 `DeviceLost` 後的 `backboardd` 重啟。最初讓 GPU 失效的事件仍待定位。
- **做法**：建議以無腳本使用者開機搭配 `-Seconds 1800` 全程錄製重現鎖定畫面自訂 `DeviceLost`；其餘依 `STOPLOSS-0043` 與 000 線規劃排程。
- **復現**：來源未記具體修補指令（待後續重現與設計）。
- **主機測試**：無（原始紀錄未提）
- **驗證紀錄**：歷史案例見 `continuation.md:4291-4313` 與 `STOPLOSS-0043.zh-TW.md:47-55`。
  - 14:36–14:37 `native-hw000wifi-hotspot-refresh-1118` 的 mtd boot #104 在 14:36:44.806 已見舊 `backboardd` 的 Metal `DeviceLost`。14:37:05 重啟後記錄 `gate deny reason=strikes strikes=1`，餘程以 CPU 顯示。熱點故障前後均有收發，後續切回家庭網路成功。環形日誌在第一筆留存的 `DeviceLost` 前缺 3,998,921 bytes，唯讀 CrashReporter 摘要未見該時段的顯示程序崩潰檔。證據在 `work/adsp-battery/fuxilog/20261118-144054/console.txt` 第 7856、7859–7860、90417、90535、91234 行及 `work/statusbar-fix/inspect-hotspot-gpu-crash-1118/powerlog-state.json`。
  - 14:53:21–15:02:23 `native-wifi-hotspot-refresh-control-1118-loader` 用同核心、命令列與 dylib 對照，將 `fuxi,wifi-sb` 從 3 改為 1 並校時。第二次切熱點重現 0 格，390 秒 USB 紀錄與無缺段的 mtd boot #105 未見 `DeviceLost` 或 GPU 閘門拒絕。最後 SCANOUT 為 GPU 17,879／17,880 幀，回 Android 後釋鎖。證據在 `work/Codex-Xiaomi13-GPU-20261109/native-hw000wifi-gpu-control-1118/boot.log` 與 `work/adsp-battery/fuxilog/20261118-150313/console.txt`。
  - 15:03:41–15:12:46 `native-wifi-hotspot-refresh-repeat-1118-loader` 重新啟用補查。比對控制版時 RTC offset 與其餘 ADT 相同。390 秒 USB 紀錄未見 `DeviceLost` 或 GPU 閘門拒絕，只捕捉初始家庭網路加入，沒有完成熱點切換重現。已回 Android 並釋鎖。證據在 `work/Codex-Xiaomi13-GPU-20261109/native-hw000wifi-gpu-repeat-1118/boot.log`。
- **參考來源**：continuation.md（出現在本項內文，細節見出處）
- **結論／未完成**：`DeviceLost` 後的 GPU 加速停用已定位至顯示程序的同次開機保護機制。較早的 `native-surfrow2-nowait-loader` 也曾留下相同的 Metal 錯誤與重生拒絕紀錄。11 月 18 日這一次的首個 GPU 錯誤仍待取得。防復發備忘：若後續為處理桌布挑選／鎖定自訂等 GPU 議題而開放 Metal 給 `PosterBoard`／`CollectionsPoster`／`SpringBoard`，`FuxiNativeMetal` Patch 0011 之 `MTLTextureLayout` 會寫入非全零 16-byte watermark `Fuxi ASTC pair 1` 的線性 `.atx`，將繞過目前 U-026 的全零 watermark 檢查而使 CPU 模糊再次退回 `decodeRGBXFromTwiddled` 發紫；開放前務必同步讓 `FuxiAtxFix` 識別 `Fuxi ASTC pair 1`（或改採真 hw-twiddled 反解）、清除既有快取並重驗單手模式／通話／Safari 分頁總覽／多工切換器（見 U-026、B-019）。
- **出處**：`continuation.md:4291-4313`，`STOPLOSS-0043.zh-TW.md:47-55`，`xiaomi13-devicelost-lockscreen.md:1-18`。（2026-11-18 由 GPU 紀錄盤點逐項整理，Gemini 撰寫、主代理整合與抽查）

### G-050 在 1080 寬面板顯示完整 1125 寬畫面並對齊觸控

- **日期**：2026-11-20
- **狀態**：兩種模式皆通過主機驗證；實機結果見下方。
- **開關**：loader 的 `--display-fit uniform|fill` 設定 ADT `/chosen fuxi,fit` 為 1 或 2。缺省值 0 沿用置中裁切。
- **問題／現象**：iOS 按 1125×2436 排版，置中裁到 1080×2400 時，軟體鍵盤左右合計少了 45 欄。
- **根因**：面板與邏輯畫面的寬高比不同。等比顯示全寬後畫面高 2338，會留出 62 列。
- **做法**：沿用日常 Adreno 2D 複製路徑，依奇偶列分兩趟雙線性縮放；DPU 繼續讀取面板大小的輸出。局部更新和 GPU 拒絕時，CPU 以最近鄰複製對應範圍。SweetHID 依同一畫面區域反算觸點。填滿模式兩軸分別縮至 96.00% 與 98.52%。
- **復現**：核心 `73d4d60`，0117 同步檢查修正 `af92e2e`。在工作區執行 `work/localbuild/build-kernel.ps1 -Ram 9g -Name kernel-local-display-fit-1120-9g-artifact -Ath12k work/adsp-battery/ath12k-objects-b2876c6 -Tests`。兩個 loader 的完整指令在 `work/localbuild/display-fit-1120/{uniform,fill}-build.txt`；重新建置時先經 launcher 取得當次 RTC。兩者均沿用日常 sRGB、狀態欄 103 與 Bluetooth 修正；暫時開機用現有 HAL 的命令列開關，在 575 bytes 內容納 mode 6。
  - 從 B-013 的 loader 產生指令重現實機 A/B 時，替換 `--artifact`、`--name`、`--rtc-calibration` 為本次值，移除 `--stderr-log Preferences`，將 `--haptic 2` 改成 `--haptic 6 --hal-haptics --haptic-rtp work/adsp-battery/android-haptic-waves-1118-2352/0_click_P_RTP.bin`，加入 `--display-fit uniform` 或 `fill`。狀態欄校正額外使用 `--statusbar-width 360 --statusbar-height 86`。日常版保留原 604-byte 命令列與 mode 2 震動，完整步驟見 B-014。
- **主機測試**：`work/localbuild/display-fit-1120/` 保存 adreno、gpu-scanout、unixnu、touch、h1、unixnu-port2、mfb-align 測試輸出。Adreno 測試重播兩種實際封包與 1,022 個舊複製案例；unixnu 驗全畫面及局部損傷、觸控測試驗邊緣及黑邊夾限。實驗主機的 SCM 反組譯輔助程式使用本機 LLVM 21。核心建置另通過 43 個 workflow 測試，ISA 禁用指令 0。
- **驗證紀錄**：等比模式已啟動 SpringBoard。第 120、1200 張各抽查 384 點，雙線性參考的通道絕對差總和 11、3，最近鄰 435、206；兩次黑邊抽查皆無非黑點。使用者確認鍵盤及介面左右均完整，並回報上下各多出一道粗黑邊。證據 `work/Codex-Xiaomi13-GPU-20261109/native-hw000-fit-uniform-hap6-1120/boot.log`。
  - 填滿模式第 1200 張的 384 點誤差總和為 8（雙線性）、338（角點對齊）、382（最近鄰）。使用者確認黑邊消失且兩側點按正常。全高截圖中時間與右側圖示中心都是 y=80，舊裁切版已驗收的位置是 63.5。舊版右側圖示 x=841..977，填滿版 x=829..959。證據 `work/Codex-Xiaomi13-GPU-20261109/native-hw000-fit-fill-hap6-1120/framebuffer.png` 與同目錄 boot.log。
  - 趟 42 直接輸出面板原生 1080×2400。實機回報與截圖顯示 UIKit 紅色診斷條、注音鍵盤右端裁切。第 2420–2427 行：backboardd 匯入 row=4320、1080×2400 的 BGRA IOSurface 時被拒絕（stage 7、HostMemory），重啟後退回 CPU 合成。此時 SCANOUT 仍記錄 GPU 完成搬運。USB 紀錄在 `native-hw000-canvas1080-sb360x86-1120/boot.log`，mtd 在 `work/adsp-battery/fuxilog/20261120-124047/console.txt`。
  - 趟 43 等比全螢幕使用 `--display-fit aspect`（ADT 值 3），UIKit 保有 375 pt 寬度，兩軸同為 0.96。第 2461 行 CA 完整表面 1125×2500、stride=4608，第 2467 行送入 1080×2400；第 2683 行調整原生 FixedSplit，4147 行 Metal 累計 120 格。USB 最後一筆 backboardd Metal 統計為 4863 格／0 failures，回收的 mtd 記到 8967 格／0 failures，沒有 runtime-failure 或 strikes 退回 CPU。第 1200 張的 384 點雙線性誤差總和 8，最近鄰 284。使用者在設定頁驗收狀態欄、鍵盤邊緣點按與滑動。第 43 趟截圖中間跨畫面更新，僅頂部右側圖示可量，x=908、y=61.5。見 B-015。
- **參考來源**：既有 `fg_rect`、SweetFramebuffer、SweetHID、Turnip 的 r2d 矩形及 Mesa a6xx 的 FILTER 欄位。依現行雙／三緩衝路徑選用 GPU 縮放，未變更 DPU QSEED 設定。
- **結論／未完成**：填滿模式的全寬畫面、無黑邊及邊緣觸點已經使用者驗收。狀態欄依 U-001 續校，震動已有反應、手感仍需調整。
- **出處**：本機核心產物 `work/Codex-Xiaomi13-GPU-20261109/kernel-local-display-fit-1120-9g-artifact/`，原始建置輸入、43 項 PASS 與 ISA 稽核分別在 INPUTS.txt、logs/tests.txt、ISA.txt。

### G-051 GPU Class 0 1280 MiB 配額、120 Hz TE 鎖定、backboardd 恢復與多用戶端閘門

- **日期**：2026-11-25
- **狀態**：Class 0（1280 MiB）＋120 Hz TE 鎖定＋相機重進防掉 GPU 已通過並納入 B-021；Class 1（Apple Maps）因仍觸發 `ERROR_DEVICE_LOST`，依指示暫緩排除於 B-021
- **開關**：ADT `/chosen fuxi,gpu-bb-recover=3`、`fuxi,gpu-ncimport=606385736`、`fuxi,gpu-clients=none`（B-021 日常版安全關閉 Class 1 Maps；實驗開放時另有其他 client 位元）
- **問題／現象**：
  1. 長焦鏡頭退出並重新進入時容易觸發 GPU `DeviceLost` 與顯示服務降級。
  2. Baton #17 嘗試以 NativeMetal Stick-14（CI `45629170472`，Patch `0028`/`0029`）開放 Apple Maps Class 1 Metal 渲染時，Maps 通過 selector 檢查進入渲染，但撞上 512 MiB 舊記憶體配額報 `OutOfMemory(Device)`。
  3. Baton #18 將 Class 0 擴至 `1280 MiB`、Class 1 擴至 `1024 MiB` 後，相機重進不掉 GPU 且 120 Hz TE 鎖定極穩，但開啟 Apple Maps 時仍觸發 Class 1 GPU hang／SMMU fault，連帶導致 `backboardd` 出現 `ERROR_DEVICE_LOST`。
- **根因**：多用戶端 Metal 配額規劃與外部 App（Maps Class 1）之著色器／command buffer 提交仍存在造成 GPU hang 與 SMMU fault 的未解邊界；而 Class 0 桌面合成與相機重進問題源於配額不足與恢復狀態機。
- **做法**：在 `patches/kernel-metal-multiclient.patch` 實作核心多用戶端配額與恢復機制（Class 0 提高至 `1280 MiB`、`fuxi,gpu-bb-recover=3`、120 Hz TE 鎖定），更新 `ksrc:shim/fuxi-probe/gpu.c` 與 `ksrc:tools/fuxi-metal/{BackboardGate.hpp,BackboardGateTests.cpp,ClientGate.hpp,ClientGateTests.cpp,Frontend.mm,NativeSelectionTests.cpp}`、主機測試 `ksrc:tools/test-fuxi-hostimport.py` 與 `ksrc:tools/test-fuxi-m9-multiclient.py`。彙整主分支與日常版 B-021 時，依使用者決策設定 `--gpu-clients none` 暫緩排除 Maps Class 1，保留 Class 0 `1280 MiB`、120 Hz TE 與恢復保護。
- **復現**：主分支 `ksrc:db86c4e` + `harness:541b10b`，由 `work/adsp-battery/prepare-aio-loader.py --gpu-clients none --gpu-bb-recover 3` 組裝 `native-b021-daily-1125-loader`。
- **主機測試**：`BackboardGateTests`、`ClientGateTests`、`NativeSelectionTests`、`test-fuxi-hostimport.py`、`test-fuxi-m9-multiclient.py` 隨 56 項 workflow 主機測試全 PASS。
- **驗證紀錄**：
  - Baton #16：二次進出相機 0 次掉 GPU，`backboardd frames=4215 failures=0`。
  - Baton #17（`run-aio-stick14-maps-cam-1125`，`17,029` 行）：`nativemetal-stick14-ci-45629170472`（`0029`）實證 Maps 通過 selector 檢查進入渲染，於 512 MiB 配額撞 OOM；`backboardd frames=4227 failures=0`。
  - Baton #18（`run-aio-stick15-maps-cam-1125`，`216,141` 行）：`SWEET FB: vsync te-locked=600 fallback=0 period_us 8249..8256`（120 Hz TE 全程鎖定）；開啟 Maps 時發生 Class 1 hang／SMMU fault 導致 `backboardd` `ERROR_DEVICE_LOST`。
  - B-021 日常短測（`run-daily-b021-smoke-1125`，`8,203` 行，無 Maps）：`FUXI GPU: through stage 16 PASS`、`gpu_fault_lost=0`、`FUXI GPU FIT: mode 3 frames 1 refused 0`、`backboardd frames=135 failures=0`、`SWEET FB: vsync te-locked=600 fallback=0 period_us 8253`。
- **參考來源**：NativeMetal Stick-14（Run `45629170472` Patch `0028`/`0029`）、`G-050` 與 `BackboardGate`/`ClientGate` 測試框架。
- **結論／未完成**：Class 0 `1280 MiB` 配額、120 Hz TE 鎖定與 `gpu-bb-recover=3` 已納入 B-021 常駐日常版且驗證無回歸；Maps Class 1 hang／SMMU fault 修復與系統 120 fps 全鏈路優化明確排除於 B-021，列為下一階段獨立專項目標。
- **出處**：`work/adsp-battery/harness/patches/kernel-metal-multiclient.patch`、`ksrc:shim/fuxi-probe/gpu.c`、`ksrc:tools/fuxi-metal/`、`ksrc:tools/test-fuxi-hostimport.py`、`ksrc:tools/test-fuxi-m9-multiclient.py`、`work/Codex-Xiaomi13-GPU-20261109/run-aio-stick14-maps-cam-1125/`、`work/Codex-Xiaomi13-GPU-20261109/run-aio-stick15-maps-cam-1125/`、`work/Codex-Xiaomi13-GPU-20261109/run-daily-b021-smoke-1125/`。

### G-052 Wave 2 120Hz 純化整合、M4-2 `probe_0x4e`、`*poster*` 硬防護、冷機 120Hz ProMotion 定論與 Maps Phase 11/12/12B／`#469` 隔離實驗實證

- **日期**：2026-11-26（台北時間）
- **狀態**：**(A) 已併入 `B-022`**：Wave 2 三開關、`M4-2 probe_0x4e`、`stage-fuxi-metal.py` `*poster*` 硬防護、`Maps Class 1 quantum=100` 架構（日常預設 `--gpu-clients none` 關閉）、`#454` vs `#456` 冷機 120Hz 定論與 `Measure (P2-AR)`（`#457`）；**(B) 未併入 `B-022` 隔離實驗**：`#461` (`M4-3 v3`)、Phase 11 (`#464`/`#465`)、Phase 12 (`#466`)、Phase 12B (`#467`/`#468`) 與 `#469` 手動驗收暴露 `backboardd` 4096 BO 洩漏並經 `stage-rollback-stick14-b022-1126` 回退 `Stick-14`，交由 `B-023` 處理
- **開關**：開機參數 `fuxigpuquantum`（預設 `40`，實驗設 `100`）、`fuxigpuirqslice`（預設 `8000`）、`fuxigpulocksplit`（`1`）、`fuxinopoll`（`1`）；ADT `/chosen` 屬性 `fuxi,gpu-irqslice`、`fuxi,gpu-locksplit`、`fuxi,nopoll`、`fuxi,gpu-clients`（`none` / `minimal` / `core` / `maps-extended`，`B-022` 日常維持 `none`）、沿用 `fuxi,atx-fix=1`（`U-026`）；*[註：隔離實驗開關 `fuxi,gpu-c1-bos`（`B-022` `prepare-aio-loader.py` 僅以預設關閉 opt-in `0|1024..4096` 支援）與 `fuxi,fb-blend`（屬獨立實驗線）未納入 `B-022` 日常開啟]*
- **問題／現象**：
  1. **(A) 已併入 `B-022` 範圍之議題**：GPU `W1-A2`（`fuxiearly=7` + `fuxifbprio=1` + `refresh-mhz=121180`）與 Wave 2 混用導致 `MobileNotes` 滑動退化至 `67.19 Hz`；全高 2400 列 FB 截圖跨頁易受 framebuffer base 切換影響發生 USB 短斷；`ksrc:tools/stage-fuxi-metal.py:43` 遺漏同步檢查 `*poster*`；`B-022` 120Hz 複測 `#454` 出現末窗 `29.90 Hz`，須釐清是否為熱機或 `m43` bundle 退化；`Measure (P2-AR)` (`#457`) 進介面顯示 Undo/Trash 按鈕惟取景全黑。
  2. **(B) 未併入 `B-022` 之隔離實驗議題**：Maps Class 1 於 `M4-3 v1` (`#448`) 畫布全黑、`M4-3 v2` (`#455`) 於 `BGRA8UnormSRGB 900x900` 觸發 `ERROR_FORMAT_NOT_SUPPORTED` + `100 ms GPU FAULT` + `CRASH Maps[298]`；`M4-3 v3` (`#461`) 消 `seq 19` hang 但背景線程撞 `tu_CmdBindPipeline+0x42c` `sig 11`（`peak_bos=1024`）；Phase 11 (`#465`) `c1-bos=4096` 消 `sig 11` 繪出頂部實景但遇 `y=127` 截斷／條紋／`BoAlloc wired refused`；Phase 12 (`#466`) 與 Phase 12B (`#467`/`#468`) 試驗 `fb-blend`/`fbplanes` 後，15 分鐘手動驗收 `#469`（`run-b022-p12-user-accept-1126`）暴露 **`M4-3 v3` `backboardd` 4096 BO 洩漏崩潰（`bos=4096/4096`、`sig 11` 兩次）**。
- **根因**：
  1. **(A) 已併入 `B-022` 根因**：`W1-A2` 干擾 1-tick cadence；`probe.c` 舊版 `req=0x4e` 逐包重讀 DPU 暫存器 `0x14` 未鎖存 `latched_base`；`stage-fuxi-metal.py` 僅攔截 `SpringBoard`／`mediaserverd`；`#456` 冷機回退 `stick14` 證實 `#454`/`#456` 末段 `~30 Hz` 源自 swipe 間隔期間 CoreAnimation ProMotion VRR 主動降頻疊加 `encode_ms > 8 ms` UI 重幀長尾，與過熱或 `m43` 無關；`Measure` 黑取景源自 `metallib LibraryKind(1)` 與 `RenderPass(ResolveWithoutTexture(Depth))` 缺 `supportsMSAADepthResolve` 及 `BGRA8UnormSRGB`/tile OOB。
  2. **(B) 隔離實驗根因**：`M4-3 v3` 背景線程 `tu_CmdBindPipeline+0x42c`（`0x568c4`）因 `tu_cs.h:326-328` Release 移除 `assert` 且 `peak_bos=1024` 撞 `FG_ABI_CLASS1_MAX_BOS=1024u` 解參考 `0x0` 崩潰；升至 `c1-bos=4096`（`#465`–`#468`）雖消除 `Maps` `sig 11`，但 `#469` 長時操作暴露 `M4-3 v3` 存在 `backboardd`（Class 0 `pid=57`、`pid=355`）BO 槽位持續累積耗盡至 `bos=4096/4096` 觸發 `BoAlloc slots refused` + `sig 11` 洩漏缺陷。
- **做法**：
  - **(A) 已併入 `B-022`（`ksrc 5a90dea` / `bb8fabc` / `e67a6ca` / `251339c`、`harness 2ce4028`）**：
    1. 淘汰 `W1-A2`，採純 Wave 2 三開關（`fuxi,gpu-locksplit=1`, `fuxi,nopoll=1`, `fuxi,gpu-irqslice=8000`）納入 `patches/kernel-gpu120-wave2.patch`（同步 `xnu/fuxi-adreno/{FuxiAdrenoUserClient.cpp,fuxi_gpu_abi_backend.inc,fuxi_gpu_irq.inc}` 與 `xnu/fuxi-unixnu/SweetFramebuffer.cpp`）。
    2. 於 `patches/kernel-m42-probe0x4e.patch` 更新 `shim/fuxi-probe/probe.c`（`ud_setup` `req==0x4e` 於 `off==0` 鎖存 `latched_base`）與 `tools/test-fuxi-gpu-scanout.py`（增測 `screenshot(0,512)`）。
    3. 補強 `tools/stage-fuxi-metal.py:43-45`（大小寫不敏感永久封鎖 `springboard`、`mediaserverd` 及 `*poster*`）並同步更新 `tools/test-fuxi-metal-stage.py`、`tools/test-fuxi-metal-clientgate.py` 與 `patches/kernel-metal-multiclient.patch`（納 Class 1 `quantum=100` 架構，日常維持 `--gpu-clients none` 關閉）。
  - **(B) 未併入 `B-022` 隔離實驗處置**：`#469` 暴露 `backboardd` 4096 BO 洩漏後，立即透過 `work/statusbar-fix/stage-rollback-stick14-b022-1126`（`PRE_OK 21 / WRITE_OK 21 / FSCK_final OK`）將實機安全回退至 `Stick-14`（`FuxiNativeMetal 102d52d6...`、`libnative_metal_bridge.dylib 31a4cd97...`），所有 Phase 11/12/12B/`M4-3 v3` 變更均嚴格隔離於 `integ-b022-p11-1126` 等實驗區，不進入 `ksrc a00205b` / `harness 2ce4028`。
- **復現**：
  - (A) `B-022` 主線核心 `kernel-local-b022-daily-9g-artifact`（`cd4bf328...`，`local-wsl (not a CI artifact)`）及三合一基準 `kernel-local-wave2-maps-m42-1126-9g-artifact`（`2ab9c0c7...`），搭配 `compare_120hz_ab.py --single --json` 量測 13-shot `MobileNotes` 滑動；日常版 `native-b022-daily-1126-loader` 以 `--gpu-clients none` 維持關閉 Class 1。
  - (B) 隔離實驗線分別使用 `kernel-local-b022-p11-c1bos-1126-9g-artifact`（`aca34a85...`）、`kernel-local-b022-p12-fbblend-9g-artifact`、`kernel-local-b022-p12b-fbplanes-9g-artifact` 與 `nativemetal-m43v3-ci-45743892707`（已回退 `Stick-14`）。
- **主機測試**：
  - `test-fuxi-gpu-wave2.py --sanitize`、`test-fuxi-gpu-scanout.py`、`test-fuxi-gpu-irq.py`、`test-fuxi-hostimport.py`、`test-fuxi-gpu-k1.py`、`test-fuxi-metal.py`、`test-fuxi-m9-multiclient.py`、`test-fuxi-metal-stage.py --policy-storage-only`、`test-fuxi-metal-clientgate.py` 全數隨主分支 **`63/63 PASS`**（含 `PosterBoard`/`CollectionsPoster`/`WeatherPoster`/`WALLPAPERPOSTER` 負測試拒絕）。
- **驗證紀錄**：
  - **(A) 已併入 `B-022` 實證**：
    1. **Wave 2 + M4-2**：`#442` (`120.21 Hz`, `1-tick 97.13%`) 與 `#447` (`120.13 Hz`, `p95=1.660 ms`, `1-tick 97.48%`, `abi_lock_max=0 us`, `te_hz=121.22 Hz`) 過關；`#443` `W1-A2` 降至 `67.19 Hz` 淘汰；`#446` (`M4-2`) `read 5184000 bytes in 6.8s; reconnects=0` 驗證 `probe_0x4e` 零斷線。
    2. **冷機 120Hz 對照定論（`#454` vs `#456`）**：`#454`（`m43` bundle）峰值 `61.86 Hz` / 末窗 `29.90 Hz`（`4+` gap `441`, `late 6.94%`, `heavy_encode_avg 10,477.5 us`）；`#456`（冷機電池 `33.4°C` 起 / 互動前 `33.8°C` → `35.0°C`、SoC `39.0→59.6→51.7°C` + 回退 `stick14`）重現峰值 `64.02 Hz` / 末窗 `30.20 Hz`（末窗 `p95=1.283 ms`, `p99=1.354 ms`，中段 `L9764` `11.974/133.377 ms`，穩定段 `blit avg 681–693 us`, `FAULT=0`, `dropped=0`, `stuck=0`, `te_hz=121.09 Hz`, `4+` gap `286`, `late 3.73%`, `heavy_encode_avg 10,243.0 us`），正式排除 `M4-3 v1` 與熱機 120Hz 假退化，誠實確立「底層 120Hz 瞬時節奏解鎖，惟整體體驗仍受 ProMotion 30–60Hz VRR 降幀與重幀編碼長尾影響」。
    3. **`Measure (P2-AR)` (`#457`)**：`boot.log`（`12,161 L`, `ea9bcce9…`）`L10207 exec Measure` → `L10256 gate allow` → `L10265/10662/10663 admit(Class1)`×3、`Measure` `CRASH=0`（不含背景既有系統／extension 項目）、`FAULT=0`、`submits 1283 dropped 0 stuck 0`；進介面見 Undo/Trash 按鈕、黑背景源自 `LibraryKind(1)` + `DepthResolve` + `BGRA8UnormSRGB`/tile OOB。
  - **(B) 未併入 `B-022` 隔離實驗實證**：
    1. **`M4-3 v1/v2/v3` 演進（`#448`, `#455`, `#461`）**：`#448` (`v1`) `surf_pass=2` 畫布 `0/42000`；`#455` (`v2`) `rpasses=4 draws=12` 後 `BGRA8UnormSRGB 900x900 ERROR_FORMAT_NOT_SUPPORTED` + `100 ms GPU FAULT` + `CRASH Maps[298]`；`#461` (`v3`, `nativemetal-m43v3-ci-45743892707`) 消 `seq 19` hang（`FAULT=0`, `DEV_LOST=0`, `dc=0`），但背景執行緒於 `LF L9638..9641` 命中 `tu_CmdBindPipeline+0x42c` 空指標 `sig 11`（`alloc 1512 / walk 1504`, `peak_bos=1024`），隨即以 `stage-rollback-stick14-m43v3-1126` 回滾 `Stick-14`。
    2. **Phase 11 (`#464` 冷開 / `#465` `-r2`)**：`c1-bos=4096`（`L157 max_bos=4096`）於 `#465`（`run-b022-maps-m43v3-c1bos4096-r2-1126`，`11,380 L` / `a928894baa33`）`L9123/9148/9149 admit(Class1)`×3，**消除 `Maps` `sig 11`**（`FAULT=0`, `CRASH Maps=0`, `peak_bos=1257`, `cb=800`, `draws=178,299`, `turnip_res=265/0/0`），頂部 `y=0..126` 繪出台北「永安市場」／「中和」實景且 `all_purple=0`，惟 `y=127` 截斷／~13–14px 條紋與 `L9455 BoAlloc wired refused wired_mb=979/1024`。
    3. **Phase 12 (`#466`) & Phase 12B (`#467`/`#468`)**：`#466`（`run-b022-maps-p12-fbblend-1126`，`11,514 L` / `dbbd654d8460`）啟用 `fuxi,fb-blend`、`Maps[306]` `cb=2000 draws=455,303`、`FAULT=0`/`CRASH Maps=0`、7 次 `L10422–10454 BoAlloc wired refused` (`wired_mb=1022..1023/1024`)、`fb 329,201 B` (`6c996ab99fad`)；`#467`（`11,644 L` / `320ca63ae62c`，`cb=800 draws=195,526`）與 `#468`（`12,136 L` / `812d2c0f7bc8`，`cb=400 draws=71,661`）續測 `fbplanes`（`BoAlloc refused=0`/`FAULT=0`/`CRASH Maps=0`）。
    4. **15 分鐘手動驗收 `#469` 暴露 BO 洩漏與 `Stick-14` 回退（`run-b022-p12-user-accept-1126` + `stage-rollback-stick14-b022-1126`）**：`boot.log`（`33,274 L` / `b496b716fb65`）執行長時操作期間，`L14564–14646` / `L16114–16121` 出現 `Maps` `BoAlloc wired refused`（`wired_mb=968..1001/1024`），隨後暴露 **`M4-3 v3` `backboardd` 4096 BO 洩漏**：`L16458–16459 BoAlloc slots refused class=0 pid=57 proc=backboardd bos=4096/4096` → **`L16460 CRASH backboardd[57] sig 11`**；重啟後再次累積至 `L24640–24641 BoAlloc slots refused class=0 pid=355 proc=backboardd bos=4096/4096` → **`L24642 CRASH backboardd[355] sig 11`**（`peak_bos 4096`）。隨即以 **`stage-rollback-stick14-b022-1126`**（`PRE_OK 21 / WRITE_OK 21 / JOURNAL_REPLAYED / FSCK_final OK`）將 `FuxiNativeMetal` (`3f64a023... -> 102d52d6...`) 與 `libnative_metal_bridge.dylib` (`0539008e... -> 31a4cd97...`) 安全回退 `Stick-14`。
- **參考來源**：`G-051`、`U-026`、`B-022`、`work/localbuild/gpu120-wave1-1125/compare_120hz_ab.py`、`work/localbuild/maps-canvas-1125/SYSAPPS_MATRIX_1126.md`。
- **結論／未完成**：
  - **(A) 已併入 `B-022`**：純 Wave 2、M4-2 `probe_0x4e`、`ClientGate` `*poster*` 三層防護、`#454` vs `#456` 冷機 120Hz ProMotion 對照定論與 `Measure` 進介面 0 crash 均已成立並納入 Scheme A（`B-022` 日常維持 `--gpu-clients none` 與 `Stick-14`；對外亦嚴格遵守不宣稱全場景穩定 120 FPS 之誠實邊界）。
  - **(B) 留待 `B-023`**：`#461` (`M4-3 v3`)、Phase 11/12/12B（`#464`–`#468`）與 `#469` 暴露之 `backboardd` 4096 BO 洩漏均**未併入 `B-022`**；Turnip BO 洩漏根治、Phase 12D-A 與 Maps/Class 1 渲染完修統一交由後續 `B-023` 專線處理。
- **出處**：`ksrc:shim/fuxi-probe/probe.c`、`ksrc:xnu/fuxi-adreno/FuxiAdrenoUserClient.cpp`、`ksrc:xnu/fuxi-adreno/fuxi_gpu_abi_backend.inc`、`ksrc:xnu/fuxi-adreno/fuxi_gpu_irq.inc`、`ksrc:xnu/fuxi-adreno/fuxi_gpu_scanout.inc`、`ksrc:xnu/fuxi-unixnu/SweetFramebuffer.cpp`、`ksrc:tools/stage-fuxi-metal.py`、`ksrc:tools/test-fuxi-gpu-wave2.py`、`ksrc:tools/test-fuxi-gpu-scanout.py`、`ksrc:tools/test-fuxi-metal-stage.py`、`ksrc:tools/test-fuxi-metal-clientgate.py`、`harness:patches/kernel-gpu120-wave2.patch`、`harness:patches/kernel-m42-probe0x4e.patch`、`harness:patches/kernel-metal-multiclient.patch`、`work/Codex-Xiaomi13-GPU-20261109/run-w2-pure-1125/`、`work/Codex-Xiaomi13-GPU-20261109/run-w2-a2-taps-1125/`、`work/Codex-Xiaomi13-GPU-20261109/run-maps-m42-probe-1126/`、`work/Codex-Xiaomi13-GPU-20261109/run-wave2-m42-120hz-1126/`、`work/Codex-Xiaomi13-GPU-20261109/run-maps-m43-q100-1126/`、`work/Codex-Xiaomi13-GPU-20261109/run-maps-m43v2-q100-1126/`、`work/Codex-Xiaomi13-GPU-20261109/run-maps-m43v3-q100-1126/`、`work/Codex-Xiaomi13-GPU-20261109/run-b022-120hz-1126/`、`work/Codex-Xiaomi13-GPU-20261109/run-b022-120hz-stick14-cool-1126/`、`work/Codex-Xiaomi13-GPU-20261109/run-b022-measure-p2-1126/`、`work/statusbar-fix/stage-rollback-stick14-m43v3-1126/`、`work/Codex-Xiaomi13-GPU-20261109/run-b022-maps-m43v3-c1bos4096-1126/`、`work/Codex-Xiaomi13-GPU-20261109/run-b022-maps-m43v3-c1bos4096-r2-1126/`、`work/Codex-Xiaomi13-GPU-20261109/run-b022-maps-p12-fbblend-1126/`、`work/Codex-Xiaomi13-GPU-20261109/run-b022-maps-p12b-fbplanes-1126/`、`work/Codex-Xiaomi13-GPU-20261109/run-b022-maps-p12b-fbplanes-r2-1126/`、`work/Codex-Xiaomi13-GPU-20261109/run-b022-p12-user-accept-1126/`、`work/statusbar-fix/stage-rollback-stick14-b022-1126/`。

## 盤點附註（2026-11-18）

> 整理 GPU 紀錄時記下的漏讀與疑點，原文照錄（編號已改成本紀錄庫的編號）。處理後請在對應項目更新，並刪掉這裡的條目。

1. **`Codex-Xiaomi13-GPU-20261109\` 第一層 `*.md`**：經 `Glob` 確認該目錄第一層沒有任何 `.md` 檔案，所有 `.md` 皆位於 `compressed-copy-span-prototype\` 與 `kernel-source-979\` 等子目錄內（多為上游 `ios-source` 與 `m2v-work` 文件）。
2. **`continuation.md` 早期歷史脈絡與部分檔案雜湊**：
   - `native-layout0011` 到 `native-layout0012` 之間的精確部署時間與部分中間檔案 sha256 在 `continuation.md` 232–360 行中未逐一列出全碼（例如 `0011` 部署日誌未獨立成段，係透過 `0012` 對比行 5125/5128/5130 與 `ASTC_LAYOUT.md` 反推確認）。
   - `BackboardGate.hpp` 計數 `late` 的行號在早期 Gemini 報告曾引為 98 行，主代理核對 `mac-frontend-source` 副本實為第 104 行（`continuation.md:372` vs `376`）。
3. **GMEM「花屏」與 `0023`/`0024` 因果反轉**：`0023` 當時依截圖推斷 `noconcurrentresolves` 解決條紋並寫入 `0024` 預設；但同日稍晚以 `--tour-verify` 重測證實為 USB 讀屏時的 triple-buffer 交錯假象，完整 GMEM（`fuxitudebug=nocb`）本來即渲染正確。
4. **`0014` 紫色殘留敘述修正**：`0014` 初段曾有舊快取殘留的判讀，隨後於 `0015` 修正並由 guest 清除舊有 `CachedSnapshot` 模糊快取檔案。
5. **`hw000-preparation.patch`**：位於 `work\adsp-battery\harness\patches\` 內，但經 `Read` 確認為 0 bytes 空檔（僅用於觸發 `hw000-preparation` CI）。
