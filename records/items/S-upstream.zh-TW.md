# S 系列：上游繼承補丁（0001–0116）

> ksrc `work/adsp-battery/ksrc/xnu/patches/` 的 115 份 XNU 補丁（沒有 0094），每份一項，`S-NNN` 對應補丁號 `0NNN`。0001–0081 來自 ios-sweet（Redmi Note 10 Pro「sweet」的 iOS 移植），0082–0116 來自 ios-fuxi（小米 13「fuxi」延伸）；基底是 zgl97/ios-fuxi `979e2f08`（2026-11-06）。0117 以後與 `xnu/variants/` 是本機新增，記在 K 系列（0117 → K-001 起、0118 → K-020、0119 → K-065、0121 → K-067、`ram9g.patch` → K-021、`ath12k-link.patch` → K-050）。

> 撰寫方式（2026-11-18）：主代理先用 git 與補丁機械產生每份的事實卷宗（檔案、開關、上游提交完整說明、本機提交、記錄字串），Gemini 依卷宗逐份撰寫，主代理逐項核對欄位、開關、上游與本機 hash，並補上本機修改的交叉對照。「演進」逐筆列出上游提交；上游提交說明本身是最原始的紀錄，可用 `git -C work/adsp-battery/ksrc show <hash>` 查。

> 「見 README 共用步驟 1–3」指 `records/README.zh-TW.md`「共用步驟」：取得 ksrc、依序套補丁、建置核心。

## ios-sweet（0001–0081）

### S-001 移除 Xcode 16 已刪除的 clang 自動變數初始化旗標

- **日期**：2026-10-14 21:19
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：使用新版 Xcode 16 clang 編譯 XNU 時，建置旗標中包含已被移除的 auto-var-init 相關旗標。
- **根因**：`makedefs/MakeInc.def` 的 `CFLAGS_GEN` 仍保留 Xcode 16 clang 已移除的編譯旗標。
- **做法**：修改 `makedefs/MakeInc.def`（刪除 1 行），從 `CFLAGS_GEN` 中移除該已棄用的 clang 旗標；該筆提交同時移除未使用的 KDK cache 步驟。
- **復現**：見 README 共用步驟 1–3；無須額外開機參數；編譯階段生效，無 console 記錄字串。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：來源未記
- **結論／未完成**：編譯旗標修正已套用，無未完成項目。
- **出處**：`ksrc:xnu/patches/0001-new-clang-drop-removed-auto-var-init-flag.patch`；上游提交 `31360c7`

### S-002 為新版 clang 回移植 UIO_STACKBUF 巨集

- **日期**：2026-10-14 21:24
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：新版 clang 拒絕過對齊（over-aligned）的 `uio_stackbuf_t` 陣列宣告，導致核心編譯失敗。
- **根因**：舊版 XNU 直接宣告 `uio_stackbuf_t` 陣列作為 `uio_createwithbuffer()` 的堆疊緩衝區，與新版 clang 對過對齊型別陣列的檢查不相容。
- **做法**：修改 13 個檔案（`bsd/sys/uio_internal.h`、`bsd/kern/decmpfs.c`、`bsd/kern/kpi_socket.c`、`bsd/kern/sys_generic.c`、`bsd/kern/uipc_syscalls.c`、`bsd/nfs/nfs_serv.c`、`bsd/vfs/vfs_attrlist.c`、`bsd/vfs/vfs_lookup.c`、`bsd/vfs/vfs_quota.c`、`bsd/vfs/vfs_subr.c`、`bsd/vfs/vfs_syscalls.c`、`bsd/vfs/vfs_vnops.c`、`security/mac_vfs_subr.c`），回移植 `UIO_STACKBUF` 巨集以宣告正確大小與對齊的堆疊緩衝區，並替換上述各檔（如 `decmpfs_fetch_compressed_header`、`sock_receive_internal`、`dofileread`、`dofilewrite`、`sendfile`、`nfsrv_read*`、`getattrlist*`、`vn_rdwr_64` 等函式）中的舊宣告。
- **復現**：見 README 共用步驟 1–3；無須額外開機參數；編譯階段生效（上游提交註明 `build with make -k`），無 console 記錄字串。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：較新版 XNU 原始碼（註解載明 `Backported from later xnu`）
- **結論／未完成**：編譯相容性修正已套用。
- **出處**：`ksrc:xnu/patches/0002-new-clang-backport-UIO_STACKBUF.patch`；上游提交 `762fd4d`

### S-003 新增 sweet-fbcon 幀緩衝區序列主控台

- **日期**：2026-10-14 21:36
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：Redmi Note 10 Pro（sweet）的除錯 UART 引擎運作於 SPI 模式，沒有可用的實體 UART 序列埠可輸出 `kprintf`。
- **根因**：缺乏可用的序列硬體，需透過開機幀緩衝區顯示早期核心訊息。
- **做法**：修改 `pexpert/arm/pe_serial.c` 與 `pexpert/pexpert/arm/protos.h`，擴充 `serial_device` 列舉並透過 `register_serial_functions()` 與 `serial_init()` 實作 `sweet-fbcon` 幀緩衝區序列主控台：從 ADT 節點 `"sweet-fbcon"` 讀取 `reg`（絕對實體位址與大小，非 arm-io 相對位址）、`width`、`height` 與 `stride`（32bpp 每列位元組數），將 `kprintf` 輸出繪製到開機幀緩衝區；文字滿頁後折返頂端而不捲動，並將游標後一行留白以便辨識最新輸出。
- **復現**：見 README 共用步驟 1–3；需 ADT 包含 `"sweet-fbcon"` 節點；無額外開機參數，無記錄字串（輸出即為繪製於螢幕的 `kprintf` 文字）。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：來源未記
- **結論／未完成**：提供 `sweet-fbcon` 幀緩衝區序列輸出；後續由 `0010`、`0012` 補上更早期的啟動與 TTBR0 撤除後保留機制。
- **出處**：`ksrc:xnu/patches/0003-sweet-framebuffer-serial-console.patch`；上游提交 `cfe02d3`

### S-004 新增 SWEET 機型組態與 GICv3 Group 1 中斷路徑

- **日期**：2026-10-14 21:44～2026-10-14 21:48
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：XNU 缺乏 Redmi Note 10 Pro（sweet，Qualcomm SM7150 / Snapdragon 732G）的 arm64 機型組態，且 Qualcomm 平台的 GICv3 與時脈中斷配置與 Apple SoC／VMAPPLE 不同，無法直接開機。
- **根因**：Kryo 470（Cortex-A76 + Cortex-A55，ARMv8.2，無 PAC）沒有 Apple 實作定義系統暫存器（無 `APPLE_ARM64_ARCH_FAMILY`）；GICv3 具有雙安全狀態（`GICD_TYPER` `SecurityExtn=1`），Group 0 屬於 TrustZone，非安全端寫入無效，計時器與 IPI 皆以 Group 1 NS IRQ 抵達；此外 Qualcomm 將 architected timer PPI 編號為 1/2/3/0，虛擬計時器 CNTV 為 INTID `16 + 3 = 19`。
- **做法**：修改 7 個檔案並新增 `pexpert/pexpert/arm64/SWEET.h`（共 8 檔：`makedefs/MakeInc.def`、`osfmk/arm64/proc_reg.h`、`osfmk/arm64/sleh.c`、`osfmk/arm64/start.s`、`pexpert/arm/pe_fiq.c`、`pexpert/pexpert/arm64/Makefile`、`pexpert/pexpert/arm64/SWEET.h`、`pexpert/pexpert/arm64/board_config.h`）。定義 `SWEET` 機型組態（arm64 無 PAC、16K 分頁、GICv3 暫存器與非安全端 `GICD_CTLR` 設定 `ARE_NS | EnableGrp1A | EnableGrp1`）、在 `sleh_irq()` 與 `pe_init_fiq()` 建立 GICv3 Group 1 IRQ 計時器／IPI 處理路徑（先 ack 再處理以免遺失 IPI），並匯出 `SWEET.h`。
  - 演進：
    - `e1be083` 2026-10-14 21:44：新增 SWEET 機型組態（arm64 無 PAC、16K 分頁、GICv3 Group 1 IRQ 計時器／IPI 路徑）。
    - `1024515` 2026-10-14 21:48：匯出 `SWEET.h`（同筆提交亦包含 mkadt/mkpayload 工具與 xnu loader milestone 4）。
- **復現**：見 README 共用步驟 1–3；以 SWEET 機型組態編譯；無記錄字串。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：GICv3 規格書（註解載明 `Arm IHI 0069G`）及 Linux GICv3 初始化行為
- **結論／未完成**：完成 SWEET 基本機型組態與 GICv3 Group 1 IRQ 路徑；虛擬計時器 INTID 後續於 `0017` 改為可從 device tree 覆寫。
- **出處**：`ksrc:xnu/patches/0004-sweet-machine-config.patch`；上游提交 `e1be083`、`1024515`

### S-005 修正無 PAC 且無 Rosetta 組態下的 pmap 成員重複定義

- **日期**：2026-10-14 21:53
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：在無 PAC（Pointer Authentication）且無 Rosetta 的建置組態下，`struct pmap` 出現重複成員導致編譯失敗。
- **根因**：`osfmk/arm/pmap/pmap.h` 中條件編譯條件未正確排除無 PAC 且無 Rosetta 的組合。
- **做法**：修改 `osfmk/arm/pmap/pmap.h`（新增 1 行、刪除 1 行），修正 `struct pmap` 內的條件編譯保護。
- **復現**：見 README 共用步驟 1–3；編譯階段生效，無開機參數與記錄字串。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：來源未記
- **結論／未完成**：無 PAC 組態可通過 `pmap.h` 編譯。
- **出處**：`ksrc:xnu/patches/0005-non-pac-build-fixes.patch`；上游提交 `26a53c1`

### S-006 新增使用開源 arm asm／pmap 的 SWEET MASTER 組態檔

- **日期**：2026-10-14 21:59
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：SWEET 缺少獨立的核心組態檔，無法以開源 arm 組語與 pmap 實作進行編譯。
- **根因**：Apple 原廠組態依賴閉源 per-SoC 程式庫。
- **做法**：新增 `config/MASTER.arm64.sweet`（114 行），設定 SWEET 核心組態並啟用開源 arm asm／pmap 選項。
- **復現**：見 README 共用步驟 1–3；建置組態階段生效，無開機參數與記錄字串。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：來源未記
- **結論／未完成**：建立 `MASTER.arm64.sweet` 基礎組態；後續 `0007`、`0008` 繼續擴充此檔。
- **出處**：`ksrc:xnu/patches/0006-sweet-master-config-open-source-asm-pmap.patch`；上游提交 `e4582f1`

### S-007 為 Apple 內部標頭檔（tunables／SART／UAT）提供空實作佔位並停用 kpc／kperf

- **日期**：2026-10-14 22:04
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：編譯 SWEET 核心時缺少 Apple 內部未開源的 `tunables`、`SART`、`UAT` 標頭檔，且 `kpc`／`kperf` 相依缺漏。
- **根因**：Apple 的 SART（NVMe/ANS 用 IOMMU）、UAT（GPU 統一位址轉換器）與 per-SoC CPU tunables（實作定義的 HID/ACC 暫存器設定）皆未包含在開源釋出中，且 Qualcomm Cortex-A76/A55 硬體上並不存在這些元件。
- **做法**：修改 `config/MASTER.arm64.sweet` 與 `osfmk/kern/task.c`（`task_create_internal`），並新增 `osfmk/arm64/ppl/sart.h`、`osfmk/arm64/ppl/uat.h`、`osfmk/arm64/tunables/tunables.s`（共 5 檔）。將 SART／UAT bootstrap 設為 no-op，提供接收 `start.s` 三個暫存器參數的 `APPLY_TUNABLES` 空巨集，並調整不依賴 `kpc`／`kperf` 的組態。
  - 演進：
    - `973d037` 2026-10-14 22:04：替 Apple 內部 tunables/SART/UAT 標頭檔提供 stub，無 kpc/kperf 建置。
    - `c3ae998` 2026-10-14 22:04：修正 `APPLY_TUNABLES` stub 以接受 `start.s` 傳入的三個暫存器參數。
- **復現**：見 README 共用步驟 1–3；編譯階段生效，無開機參數與記錄字串。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：來源未記
- **結論／未完成**：解決 Apple 內部標頭檔缺漏；`kpc` 後續於 `0008` 改以空後端 stub 實作以滿足 `kperf` 相依。
- **出處**：`ksrc:xnu/patches/0007-sweet-stub-apple-internal-headers.patch`；上游提交 `973d037`、`c3ae998`

### S-008 補齊 SWEET 閉源程式庫缺口與 kpc 空後端

- **日期**：2026-10-14 22:43
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：連結階段缺少多個僅存在於 Apple 閉源 per-SoC KDK 程式庫中的符號（`kpc`、快取／啟動組語符號、`pmap_pending_preemption`、`ml_enable_monitor`、`amcc_rorgn` 等），導致核心無法連結。
- **根因**：XNU 必須啟用 `kperf` 才能編譯，而 `kperf` 依賴 `kpc`；此外 `files.arm64` 為 `nos_arm_asm` 編譯 AMCC 唯讀區保護程式，且 `arm_vm_init()` 無條件呼叫 `ml_enable_monitor()`，但 SWEET 無 Apple PMU、無 PPL/monitor、無 HID 位元、無 AMCC 硬體。
- **做法**：修改 6 個檔案並新增 3 個檔案（共 9 檔：`config/MASTER.arm64.sweet`、`osfmk/arm/pmap/pmap.c`、`osfmk/arm/pmap/pmap_misc.c`、`osfmk/arm64/amcc_rorgn_common.c`【新檔】、`osfmk/arm64/amcc_rorgn_ppl.c`【新檔】、`osfmk/arm64/caches_asm.s`、`osfmk/arm64/kpc.c`【新檔】、`osfmk/arm64/machine_routines.c`、`osfmk/arm64/start.s`）。實作空 `kpc` 後端（所有計數器類別為空、設定請求皆乾淨失敗）；提供 `CleanPoC_DcacheRegion_Force_nopreempt` 與 `_nohid`（通用 ARMv8 直接做區域清理）；將 `pmap_cs_lockdown_pages`／`unlockdown` 與 `pmap_pending_preemption`（推測，卷宗見 `pmap_misc.c` 註解）及 `ml_enable_monitor` 設為無 PPL 空實作；在 `pmap_page_protect_options_with_flush_range()` 以原始 PV entry 取代 `ptep_get_iommu()`；提供 `amcc_rorgn` 佔位檔案。
- **復現**：見 README 共用步驟 1–3；編譯連結階段生效；若觸發錯誤可見 `pmap_page_protect: attempt to compress ppnum 0x%x owned by iommu pte %p, pve_p=%p`。
- **主機測試**：無（提交說明提及 local compile checker）
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：來源未記
- **結論／未完成**：核心閉源符號缺口已補齊並可通過編譯檢查。
- **出處**：`ksrc:xnu/patches/0008-sweet-fill-closed-lib-gaps-kpc-stub.patch`；上游提交 `6a6d4fc`

### S-009 移除僅限 Apple SoC 的 KPI 匯出符號

- **日期**：2026-10-14 22:49
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：KPI 匯出表包含僅存在於 Apple SoC 的符號（如 `_mach_bridge_recv_timestamps`、`_kernel_image4_get_monitor_exports`），導致 SWEET 連結或匯出檢查失敗。
- **根因**：SWEET 未包含 Apple SoC 專用子系統，無法提供對應的私有 KPI 符號。
- **做法**：修改 `config/Private.arm64.exports` 與 `config/Private.exports`（共刪除 37 行），移除 Apple SoC 專屬的匯出符號。
- **復現**：見 README 共用步驟 1–3；編譯連結階段生效，無開機參數與記錄字串。
- **主機測試**：無（提交說明提及 offline export checker）
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：來源未記
- **結論／未完成**：匯出表已清理完成，可通過離線匯出檢查。
- **出處**：`ksrc:xnu/patches/0009-sweet-drop-apple-soc-only-exports.patch`；上游提交 `3a063a4`

### S-010 新增開機早期幀緩衝區除錯標記與早期 panic 主控台

- **日期**：2026-10-14 23:14
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：在 `arm_vm_init` 建立完整核心虛擬記憶體與 ADT 剖析之前若發生卡死或 `panic()`，螢幕無任何輸出，無法判斷開機進度與當機原因。
- **根因**：既有的 `sweet-fbcon` 序列主控台需要 ADT 與較晚期的初始化，在 bootstrap TTBR0 存活期間（甚至沒有 current thread 與鎖時）無法使用。
- **做法**：修改 5 個檔案並新增 `osfmk/arm64/sweet_early.c`、`osfmk/arm64/sweet_early.h`（共 7 檔：`osfmk/arm/arm_init.c`、`osfmk/arm64/arm_vm_init.c`、`osfmk/arm64/start.s`、`osfmk/arm64/sweet_early.c`、`osfmk/arm64/sweet_early.h`、`osfmk/conf/files.arm64`、`osfmk/kern/debug.c`）。在 `start.s` 的既定 `V=P` L2 分頁表（`bootstrap_pagetables + 2*PGBYTES`）中將開機幀緩衝區映射為一塊 32MB Normal Non-Cacheable 區塊（Stage 0 先畫出白色 64x32 標記帶，不需堆疊或分頁表）；實作無配置、無鎖、無需執行緒狀態的早期主控台，並於 `panic_trap_to_debugger()` 掛入早期 panic 輸出；在 `arm_vm_init` 撤除 bootstrap TTBR0 前停用該 `VA=PA` 映射。
- **復現**：見 README 共用步驟 1–3；無須開機參數；早期啟動時螢幕會出現 Stage 0 白色 64x32 標記帶，早期 panic 時直接輸出至螢幕。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：來源未記
- **結論／未完成**：提供 TTBR0 撤除前的早期畫面診斷；`0012` 進一步將其延伸至 TTBR0 撤除後仍持續可用。
- **出處**：`ksrc:xnu/patches/0010-sweet-early-framebuffer-debug.patch`；上游提交 `dcf4ad0`

### S-011 為大記憶體非 PPL 核心預留 physmap 區塊對齊填充空間

- **日期**：2026-10-15 06:51
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：SWEET 使用 `ARM_LARGE_MEMORY` 非 PPL 核心組態時，`arm_vm_physmap_slide()` 會保留每個實體位址的區塊偏移，使每個 PTOV 項目可能多消耗一個 twig 的 VA 空間，若未預留可能造成空間不足。
- **根因**：`ARM_LARGE_MEMORY` 組態下的 aperture 邊界與 L1 配置計算未計入 physmap 區塊對齊所需的額外填充空間。
- **做法**：修改 `osfmk/arm64/arm_vm_init.c`（新增 16 行、刪除 4 行），在 aperture 邊界與 L1 配置中為 `ARM_LARGE_MEMORY` 組態預留每個 PTOV 項目最多一個額外 twig 的 VA 空間。
- **復現**：見 README 共用步驟 1–3；無須開機參數，無記錄字串。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：來源未記
- **結論／未完成**：修正非 PPL 大記憶體組態的 physmap 空間預留。
- **出處**：`ksrc:xnu/patches/0011-sweet-physmap-alignment-padding.patch`；上游提交 `9ebfbc1`

### S-012 在 TTBR0 撤除後保留幀緩衝區除錯主控台

- **日期**：2026-10-15 07:08
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：`0010` 安裝的 `VA=PA` 幀緩衝區映射在 `arm_vm_init` 撤除 bootstrap TTBR0 後即失效，導致在正規 `sweet-fbcon` 就緒前的這段空窗期失去螢幕診斷輸出。
- **根因**：bootstrap TTBR0 撤除與正規幀緩衝區序列主控台就緒之間缺乏有效的核心 VA 映射。
- **做法**：修改 6 個檔案（`osfmk/arm64/arm_vm_init.c`、`osfmk/arm64/sweet_early.c`、`osfmk/arm64/sweet_early.h`、`osfmk/arm64/Makefile`、`osfmk/arm/arm_init.c`、`pexpert/arm/pe_serial.c`）。在 `arm_vm_init` 移除 TTBR0 前，利用 low globals 之上、`gVirtBase` 之下的閒置 32MB 保留區安裝 Normal-NC 幀緩衝區別名（`slot = 0x9c000000ULL | ARM_TTE_VALID | ARM_TTE_TYPE_BLOCK | ...`），待正規幀緩衝區序列主控台就緒後才交接。
- **復現**：見 README 共用步驟 1–3；無須開機參數，無新增記錄字串（持續在螢幕顯示除錯訊息）。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：來源未記
- **結論／未完成**：早期診斷主控台可無縫跨越 TTBR0 撤除至最終核心 TTBR1 運作期。
- **出處**：`ksrc:xnu/patches/0012-sweet-keep-debug-console-after-ttbr0.patch`；上游提交 `93ac869`

### S-013 在無 corecrypto 下為 kmem 配置器提供每 CPU 亂數產生器

- **日期**：2026-10-15 07:21
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：SWEET 尚未載入 `corecrypto` kext，導致 `kmem` 記憶體配置器缺乏亂數來源。
- **根因**：XNU 預設由 `corecrypto` 提供核心亂數，但 `kmem` RNG 介面允許使用非密碼學產生器並要求無鎖產生。
- **做法**：修改 `osfmk/vm/vm_kern.c`（新增 53 行、刪除 4 行），以公開領域的 `SplitMix64` 實作每 CPU／sizeclass 的 `kmem` 專用 RNG 狀態，於單 CPU 初始化期間由 XNU 內建早期 HMAC-SHA256 DRBG 播種（在 `EARLY_BOOT` 尚未初始化前先使用 early random），並以拒絕採樣排除不完整餘數區間以避免 modulo bias；僅供 `kmem` 私有使用，不註冊或取代加密服務。
- **復現**：見 README 共用步驟 1–3；無須開機參數，無記錄字串。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：Sebastiano Vigna, 2015, public-domain SplitMix64（註解載明 `https://prng.di.unimi.it/splitmix64.c`）
- **結論／未完成**：解除 `kmem` 對 `corecrypto` kext 的啟動依賴；通用核心 PRNG 則由 `0014` 處理。
- **出處**：`ksrc:xnu/patches/0013-sweet-kmem-rng-without-corecrypto.patch`；上游提交 `61b3d2c`

### S-014 註冊 SWEET 內建 HMAC-SHA256 核心 PRNG

- **日期**：2026-10-15 16:07
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：缺乏 `corecrypto` kext 時，核心 PRNG 與熵累積器無法註冊與雜湊，導致依賴核心亂數的子系統失敗。
- **根因**：Apple 平台仰賴 `corecrypto` kext 註冊核心 PRNG 並透過 `g_crypto_funcs` 分派表提供 SHA256。
- **做法**：修改 `osfmk/prng/prng_random.c` 與 `osfmk/prng/entropy.c`（新增 196 行、刪除 16 行）。使用 XNU 內建的 HMAC-SHA256 DRBG（與 `early_random` 相同）註冊核心 PRNG（不註冊無關的 `g_crypto_funcs`），以單一 mutex 序列化跨 CPU 產生與重新播種（熵回呼採 try-lock 避免阻塞），並確保在抹除 early DRBG 前已取得開機種子；`entropy.c` 改為直接連結核心內建 SHA256 摘要並於首次使用時延遲初始化累積器。
- **復現**：見 README 共用步驟 1–3；無須開機參數；成功時 console 輸出 `SWEET PRNG: HMAC-SHA256 registered\n`，失敗則輸出 `SWEET HMAC DRBG error %d` 或 `SWEET PRNG entropy health test failed`。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：來源未記
- **結論／未完成**：核心 PRNG 可獨立於 `corecrypto` kext 運作，採單一 mutex 設計以利早期除錯與稽核。
- **出處**：`ksrc:xnu/patches/0014-sweet-kernel-hmac-prng.patch`；上游提交 `577d23c`

### S-015 明確呈現 SWEET 缺少 trust-cache 服務的狀態

- **日期**：2026-10-15 16:13
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：獨立啟動 SWEET 時沒有 AppleImage4／AMFI kext，若保留部分初始化的 trust-cache runtime，後續解除參考空回呼函式將導致當機。
- **根因**：缺乏 Image4／AMFI kext 提供的 trust-cache runtime 實作。
- **做法**：修改 `bsd/kern/kern_trustcache.c`（新增 48 行），在 lockdown 期間明確維持 `NULL` runtime 並回報缺少 trust-cache 服務（無法載入或比對任何快取），避免後續呼叫到未初始化的回呼指標。
- **復現**：見 README 共用步驟 1–3；無須開機參數；啟動時 console 輸出 `SWEET: trust caches unavailable (Image4/AMFI not installed)\n`。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：來源未記
- **結論／未完成**：明確處理無 Image4/AMFI 時的 trust-cache 狀態（註：後續 `0025`、`0035`、`0036` 會再加入內建 AMFI 相關支援）。
- **出處**：`ksrc:xnu/patches/0015-sweet-unavailable-trust-cache-runtime.patch`；上游提交 `6d997bc`

### S-016 新增 SWEET 內建裝置樹 Platform Expert

- **日期**：2026-10-15 16:21
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：SWEET 沒有 Apple NVRAM 控制器與 `/options` 節點，XNU 原廠 Platform Expert 無法正常啟動並發佈 CPU／裝置節點。
- **根因**：Apple 原廠 Platform Expert 實作要求存在 `/options` NVRAM 節點。
- **做法**：修改 `iokit/KernelConfigTables.cpp`、`iokit/conf/files.arm64` 並新增 `iokit/Kernel/arm/SweetPlatformExpert.cpp`（共 3 檔）。實作極簡裝置樹 `SweetPlatformExpert`，使用 XNU 標準的平台、電源域與 nub 初始化流程，透過 `createNubs` 依拓樸發佈真實邏輯 CPU／cluster ID 與裝置節點，而不虛擬 NVRAM 節點（中斷與電源管理服務由其他驅動程式提供）。
- **復現**：見 README 共用步驟 1–3；無須開機參數；成功時 console 依序輸出 `SWEET: starting device-tree platform expert\n` 與 `SWEET: platform expert registered\n`。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：來源未記
- **結論／未完成**：完成 SWEET 基本裝置樹 Platform Expert 註冊。
- **出處**：`ksrc:xnu/patches/0016-sweet-device-tree-platform-expert.patch`；上游提交 `fec0dbe`

### S-017 從裝置樹選擇虛擬計時器 PPI 中斷編號（支援 QEMU）

- **日期**：2026-10-15 16:27
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無（透過 ADT 既有節點屬性 `virtual-timer-intid` 控制，非新增開機參數）
- **問題／現象**：`0004` 將虛擬計時器寫死為 Qualcomm 手機預設的 INTID 19，但在 QEMU `virt` 機型上虛擬計時器接在 INTID 27，導致無法在 QEMU 上運行。
- **根因**：不同平台（實機 vs QEMU）對 ARM architected virtual timer 的 PPI 編號不同。
- **做法**：修改 `pexpert/arm/pe_fiq.c`、`osfmk/arm64/sleh.c`、`pexpert/pexpert/arm/protos.h`（新增 28 行、刪除 2 行）。僅在開機 CPU、GIC 與 RO-late lockdown 之前從裝置樹讀取 `virtual-timer-intid`（檢查大小與是否為 PPI），若無則維持手機預設值。
- **復現**：見 README 共用步驟 1–3；若於 QEMU 執行，ADT 需設定 `virtual-timer-intid` 為 27；console 會輸出 `SWEET GIC: virtual timer INTID %u\n`，異常時輸出 `%s: invalid virtual-timer-intid size %u` 或 `%s: virtual timer INTID %u is not a PPI`。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：來源未記
- **結論／未完成**：同時支援實機與 QEMU 的虛擬計時器 PPI 切換。
- **出處**：`ksrc:xnu/patches/0017-sweet-device-tree-virtual-timer-irq.patch`；上游提交 `04cda7b`

### S-018 註冊 SWEET 開機 CPU 並驗證 GIC SGI 傳遞

- **日期**：2026-10-15 17:24
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：系統需要註冊開機 CPU（boot CPU）至排程器與 IOCPU 架構，並確認 GIC 軟體產生中斷（SGI1）可正常送達處理器。
- **根因**：缺乏 SWEET 的 `IOCPU` 驅動程式，未執行 `ml_processor_register()` 與 `processor_start()`。
- **做法**：修改 `iokit/KernelConfigTables.cpp`、`iokit/conf/files.arm64` 並新增 `iokit/Kernel/arm/SweetCPU.cpp`（共 3 檔）。實作單核開機 `SweetCPU` 驅動（繼承 XNU 傳統 `IOCPU` 介面），在比對 `phys_id` 後呼叫 `ml_processor_register()` 與 `processor_start()`，並執行 SGI1 自我測試（真實 GIC 中斷會在 `cpu_signal_handler` 清除 `SIGPdisabled`），驗證完成後才宣告初始化成功；次核啟動、quiesce、halt/suspend、resume 與 PMI 路由均暫未實作。
- **復現**：見 README 共用步驟 1–3；無須開機參數；成功時 console 輸出 `SWEET CPU: boot CPU %u registered, SGI1 verified, initialization complete\n`，超時則輸出 `SWEET CPU: SGI1 self-test timed out`。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：來源未記
- **結論／未完成**：僅支援單核（boot CPU）啟動；多核支援後續於 `0078`（sweet 8 核）與 `0090`（fuxi 7 核）補齊。
- **出處**：`ksrc:xnu/patches/0018-sweet-boot-cpu-driver.patch`；上游提交 `837f2b2`

### S-019 整合相符的開源 libpthread 核心提供者

- **日期**：2026-10-15 17:45
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：缺少 Apple `pthread` kext 提供者，userland 執行緒同步與 `pthread` 系統呼叫無法運作。
- **根因**：Apple `pthread` 核心提供者通常以獨立 kext 形式提供，獨立開機環境未載入外部 kext。
- **做法**：修改 `bsd/conf/files`、`bsd/conf/Makefile.template`、`bsd/sys/pthread_internal.h`，並新增 `bsd/pthread/sweet_pthread_compat.h`、`bsd/pthread/sweet_pthread_init.c`、`bsd/pthread/sweet_pthread_support.c`、`bsd/pthread/sweet_pthread_synch.c`（共 7 檔）。內建與 `xnu-8796.141.3` 配對的 Apple 開源 `libpthread-514.100.2` 提供者：以 kext 視角的 BSD 標頭編譯、保留 `ksyn_waitq_element` 尾端 `kwe_uth` 指標配置、透過回呼介面與私有 BSD 狀態溝通（並將 7 參數 kext `vm_fault` 轉發至 8 參數核心 `vm_fault`，`os/log` 傳入 `&__dso_handle`）。
- **復現**：見 README 共用步驟 1–3；無須開機參數；成功時 console 輸出 `SWEET pthread: libpthread-514.100.2 registered\n`，失敗則輸出 `SWEET: pthread provider registration failed: %d`。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：Apple 開源 `libpthread-514.100.2`（配對 `xnu-8796.141.3`）
- **結論／未完成**：內建 pthread 核心提供者整合完成。
- **出處**：`ksrc:xnu/patches/0019-sweet-builtin-pthread.patch`；上游提交 `fe4f9e3`

### S-020 整合 BearSSL 可攜式常數時間 AES CBC/ECB 至傳統加密 API

- **日期**：2026-10-15 17:58～2026-10-15 18:01
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：缺乏 `corecrypto` kext 時，核心傳統 AES CBC/ECB API 無法使用；且核心程式不得使用 SIMD／浮點暫存器。
- **根因**：SWEET 未載入 `corecrypto` kext，而 XNU 傳統 AES 介面需要不依賴 heap、SIMD 或 CPU 加密指令延伸的實作。
- **做法**：修改 `libkern/crypto/corecrypto_aes.c`、`libkern/conf/files`、`libkern/conf/Makefile.template`，並新增 `libkern/crypto/sweet_aes.c`、`sweet_aes_ct.c`、`sweet_aes_ct_enc.c`、`sweet_aes_ct_dec.c`、`sweet_aes_ct_cbcenc.c`、`sweet_aes_ct_cbcdec.c`（共 9 檔）。以 BearSSL 可攜式 bitslice 常數時間 AES 實作後端（上游原始碼與 MIT 授權由 `0020-stage-bearssl.sh` 暫存），並確保編譯限制在通用暫存器內執行。
  - 演進：
    - `3ca00fe` 2026-10-15 17:58：將可攜式常數時間 AES 接上 SWEET 傳統加密 API。
    - `e6c1853` 2026-10-15 18:01：限制可攜式 AES 核心程式碼僅使用通用暫存器（general registers）。
- **復現**：見 README 共用步驟 1–3；無須開機參數，無記錄字串。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：BearSSL（MIT 授權，由 `0020-stage-bearssl.sh` 依固定 commit 取得）
- **結論／未完成**：完成無 SIMD／無 `corecrypto` 的核心傳統 AES CBC/ECB 提供者。
- **出處**：`ksrc:xnu/patches/0020-sweet-portable-aes-cbc.patch`；上游提交 `3ca00fe`、`e6c1853`

### S-021 透過 mockfs 啟動 RAM disk 根檔案系統

- **日期**：2026-10-15 18:22～2026-10-15 18:30
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：需要透過 RAM disk 啟動第一個 userspace 程式（`/sbin/launchd`），但建置 `mockfs` 時發生型別檢查與配置大小錯誤（CI 38768500280 失敗：`mockfs_fsnode_t` 為指標 typedef，使 `kfree_type` 與 `kalloc_type` 配置錯誤），且實機／QEMU 走到 `BSD root: md0` 後，`IOService::setRootMedia` 斷言 `IOBlockStorageDriver` 必須存在而當機。
- **根因**：`mockfs_fsnode.c` 誤用指標型別 `mockfs_fsnode_t` 呼叫 `kalloc_type`/`kfree_type`；此外 SWEET 沒有 `IOStorageFamily`，不存在任何 block storage 驅動程式。
- **做法**：修改 `config/MASTER.arm64.sweet`、`bsd/miscfs/mockfs/mockfs_fsnode.c` 與 `iokit/Kernel/IOService.cpp`（新增 13 行、刪除 3 行）。啟用 `mockfs` 建置、將 `kalloc_type`/`kfree_type` 改用 `struct mockfs_fsnode`，並讓 `IOService::setRootMedia` 在 SWEET 無 `IOStorageFamily` 時比照 image boot 將 root 視為非區塊媒體（Apple 建置保留原斷言）。
  - 演進：
    - `a843f20` 2026-10-15 18:22：啟用 `mockfs` 以從 RAM disk（`rd=md0`）啟動記憶體映像中的 `/sbin/launchd`（同筆提交亦更新 `mkpayload.py --ramdisk`、`sweet-init.S` 與 `mkinit.py`）。
    - `72bb6a1` 2026-10-15 18:30：修正 `mockfs_fsnode.c` 配置器型別為 `struct mockfs_fsnode` 並允許無 `IOStorageFamily` 時掛載 RAM disk root。
- **復現**：見 README 共用步驟 1–3；搭配 `rd=md0` 與 `--ramdisk` 映像開機；console 出現 `BSD root: md0`。
- **主機測試**：無（CI 38768500280 編譯驗證）
- **驗證紀錄**：QEMU 以 `0020` 核心與 `md0` RAM disk 抵達 `BSD root: md0` 後觸發 `IOService::setRootMedia` 斷言，經第二筆提交修正（`0021` 自身修正後進入 `0022` 驗證）。
- **參考來源**：來源未記
- **結論／未完成**：完成 `mockfs` 與 `setRootMedia` 支援；緊接著於 `IOService::publishHiddenMedia` 遇到下一個 `IOStorageFamily` 斷言（由 `0022` 處理）。
- **出處**：`ksrc:xnu/patches/0021-sweet-mockfs-ramdisk-root.patch`；上游提交 `a843f20`、`72bb6a1`

### S-022 允許在無 IOStorageFamily / IOMedia 下執行 publishHiddenMedia

- **日期**：2026-10-15 18:38
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：QEMU 使用 `0021` 核心與 `md0` RAM disk 通過 `setRootMedia` 後，`IOService::publishHiddenMedia` 斷言 `IOMedia` 類別必須存在而當機。
- **根因**：SWEET 沒有 `IOStorageFamily`，因此不可能存在任何 `IOMedia` 實例。
- **做法**：修改 `iokit/Kernel/IOService.cpp`（新增 9 行），仍清除 hide 旗標，但略過走訪不存在的 `IOMedia` 類別實例（Apple 建置保留原斷言）。
- **復現**：見 README 共用步驟 1–3；以 `rd=md0` 開機，無新增開機參數與記錄字串。
- **主機測試**：無
- **驗證紀錄**：上游提交記明 QEMU 使用 `0021` 核心卡在 `publishHiddenMedia`，未另記 `0022` 後實機結果（由 `0023` 接續推進）。
- **參考來源**：來源未記
- **結論／未完成**：解決無 `IOStorageFamily` 時的 `publishHiddenMedia` 斷言。
- **出處**：`ksrc:xnu/patches/0022-sweet-ramdisk-root-without-iomedia.patch`；上游提交 `8401ee4`

### S-023 將 SWEET 的 libkern SHA-256 導向內建實作

- **日期**：2026-10-15 18:47
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：QEMU 使用 `0022` 核心與 `md0` RAM disk 抵達掛載 root 後，在 `SHA256_Init`（由 `vm_kernel_addrhash` 呼叫）發生 data abort。
- **根因**：缺少 `corecrypto` kext 時 `g_crypto_funcs` 保持 `NULL`，而 `SHA256_Init`/`Update` 直接讀取 `g_crypto_funcs`。
- **做法**：修改 `libkern/crypto/corecrypto_sha2.c`（新增 21 行、刪除 5 行），在 SWEET 上改透過 `ccdigest_init`/`update` 直接使用 XNU 內建的 `ccsha256_ltc_di`（同時支援程式碼簽章頁面雜湊）；SHA-384/512 當時仍無內建提供者。
- **復現**：見 README 共用步驟 1–3；無須開機參數，無記錄字串。
- **主機測試**：無
- **驗證紀錄**：上游提交記明 QEMU `0022` 核心於 `SHA256_Init` 發生 data abort，本補丁修復後於 `0024` 驗證通過此階段。
- **參考來源**：來源未記
- **結論／未完成**：libkern SHA-256 已由內建實作接手；SHA-384/512 後續於 `0033` 由 BearSSL 補齊。
- **出處**：`ksrc:xnu/patches/0023-sweet-builtin-sha256-for-libkern.patch`；上游提交 `5ebbdda`

### S-024 修正 mockfs 透過裝置 I/O 讀取虛擬 RAM disk

- **日期**：2026-10-15 18:57
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：QEMU 使用 `0023` 核心掛載 `md0` 並執行 `/sbin/launchd` 的 `load_init_program` 時，`mockfs` 發生 panic。
- **根因**：`mockfs` 直接將 init vnode 映射到 `mi_base << PAGE_SHIFT` 的「實體」記憶體；但對 IOKit 的 `RAMDisk` 而言，`memdev` 是虛擬的，`mi_base` 是核心虛擬位址截斷後的 4K 分頁號，且 `memdev` 單位永遠是 4K 而非 arm64 的 `PAGE_SHIFT`（16K）。
- **做法**：修改 `bsd/miscfs/mockfs/mockfs_vfsops.c` 與 `bsd/miscfs/mockfs/mockfs_fsnode.c`（新增 8 行、刪除 2 行），僅在 `mi_phys` 為真的實體記憶體裝置上使用 4K 單位的零複製 pager 映射；虛擬記憶體裝置則走既有的 `pagein`/`blockmap`/`strategy` 路徑存取 `memdev`。
- **復現**：見 README 共用步驟 1–3；以 `rd=md0` 開機，無記錄字串。
- **主機測試**：無
- **驗證紀錄**：`0024` 實機與 QEMU 皆成功掛載 `md0` 並進入 `load_init_program`（見 `0025` 提交說明：`boot 0024, photo IMG_20261015_190715`）。
- **參考來源**：來源未記
- **結論／未完成**：修復虛擬 RAM disk 在 `mockfs` 中的分頁大小與定址錯誤。
- **出處**：`ksrc:xnu/patches/0024-sweet-mockfs-virtual-ramdisk-io.patch`；上游提交 `a05694f`

### S-025 註冊 SWEET 內建無 entitlements 的 AMFI 介面

- **日期**：2026-10-15 19:08
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：QEMU 與手機（`boot 0024`，照片 `IMG_20261015_190715`）皆掛載 `md0` 並進入 `/sbin/launchd` 的 `load_init_program`，隨後在 `accelerate_entitlement_queries`（`FAR 0x1a0`）發生 data abort。
- **根因**：`exec` 的 `ubc_cs_blob_add` 會呼叫 AMFI 介面，但在沒有閉源 AMFI kext 時沒有任何模組註冊該介面。
- **做法**：修改 `bsd/conf/files` 並新增 `bsd/kern/sweet_amfi.c`（新增 177 行），在 `EARLY_BOOT` 註冊 SWEET 內建 AMFI 介面：老實回報沒有 entitlements（`csb_entitlements` 維持 `NULL`，所有權限查詢失敗、複製回傳空值），將程式碼簽章與頁面雜湊驗證保留在核心，無 Image4 故 `TrustCache` 介面維持版本 0；僅其他 Apple kext 會用到的介面則明確 panic 而非留空指標。
- **復現**：見 README 共用步驟 1–3；無須開機參數；console 出現 `SWEET AMFI: built-in interface registered (no entitlements)\n`。
- **主機測試**：無
- **驗證紀錄**：上游提交記明 QEMU 與實機 `0024` 在 `accelerate_entitlement_queries` 發生 data abort，此補丁接續註冊介面。
- **參考來源**：來源未記
- **結論／未完成**：建立無 entitlements 的 AMFI 介面；後續 `0035` 擴充為可剖析 XML entitlements 並標記 platform binary。
- **出處**：`ksrc:xnu/patches/0025-sweet-builtin-amfi-no-entitlements.patch`；上游提交 `a0d3934`

### S-026 將 Apple 開源 HFS 內建至 SWEET 核心

- **日期**：2026-10-15 19:26～2026-10-15 19:31
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：`hfs_corruption_panics`：開機參數（於 HFS 原始碼／設定中使用；卷宗未記預設值，細節見補丁）
- **問題／現象**：需要真實的根檔案系統以取代單一 Mach-O RAM disk（`mockfs` 會無條件接管任何裝置並當作單一執行檔）。
- **根因**：Apple 原廠 HFS 與 HFSEncodings 是獨立 kext，依賴 IOKit `IOStorageFamily` 與 `AppleKeyStore`；且 `mockfs` 未檢查裝置是否為 Mach-O 即強行掛載。
- **做法**：修改 6 個檔案（`config/MASTER`、`config/MASTER.arm64.sweet`、`bsd/conf/files`、`bsd/conf/Makefile.template`、`bsd/kern/bsd_init.c`、`bsd/miscfs/mockfs/mockfs_vfsops.c`）並新增 `bsd/sweet_hfs/sweet_hfs_compat.h`、`bsd/sweet_hfs/sweet_hfs_iokit.c`（共 8 檔）。由 `0026-stage-hfs.sh` 將 Apple 開源 `hfs-627.100.6`（39 個 C 檔）暫存至 `bsd/sweet_hfs`，以 kext KPI 視角編譯（清除核心 `CONFIG_*` 改由 HFS `kext-config.h` 決定，關閉無 `AppleKeyStore` 下的 content protection）；`bsd_init` 在 `vfsinit()` 後呼叫 `sweet_hfs_register()` 執行 HFSEncodings 與 `vfs_fsadd(VFS_TBLCANMOUNTROOT)`；`mockfs_check_macho` 改為僅接受 thin/fat Mach-O 映像（且不快取 `DEV_BSIZE` 區塊），讓真實檔案系統落入 HFS；並分離 `hfs_vfsutils.o` 與 `hfs_encodings.o` 的 `_hfs_allocated` 計數器以免重複定義。
  - 演進：
    - `a56dd56` 2026-10-15 19:26：將 Apple 開源 `hfs-627.100.6` 內建進 SWEET 核心（`sweet_hfs` 選項），實作 `sweet_hfs_iokit.c` 並讓 `mockfs` 僅認領 Mach-O 裝置。
    - `fe0a59a` 2026-10-15 19:31：CI `38777325964` 連結失敗於 `_hfs_allocated` 重複定義，將 encodings 的配置計數器獨立更名。
- **復現**：見 README 共用步驟 1–3；無須開機參數；成功時 console 輸出 `SWEET HFS: hfs-627.100.6 registered\n`，失敗則輸出 `SWEET HFS: vfs_fsadd failed: %d`。
- **主機測試**：無（本機 40 個檔案全數編譯通過、CI 38777325964 連結驗證）
- **驗證紀錄**：上游提交僅記編譯連結驗證，實機／QEMU 掛載 HFS+ RAM disk 測試紀錄於 `0027` 說明。
- **參考來源**：Apple 開源 `hfs-627.100.6`（`macos-135 release.json` 中與 `xnu-8796.141.3` 配對，由 `0026-stage-hfs.sh` 取得）
- **結論／未完成**：完成內建 HFS 檔案系統註冊；實機掛載隨即在 `0027` 遇到 `MD5Init` 缺漏。
- **出處**：`ksrc:xnu/patches/0026-sweet-builtin-hfs.patch`；上游提交 `a56dd56`、`fe0a59a`

### S-027 使用 BearSSL 提供 MD5 以支援 HFS 卷宗 UUID

- **日期**：2026-10-15 19:41
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：QEMU 使用 `0026` 核心與 HFS+ RAM disk root（含 `/sbin/launchd`）掛載時，在 `hfs_getvoluuid` 呼叫 `MD5Init` 發生 data abort。
- **根因**：無 `corecrypto` kext 時 `g_crypto_funcs` 為 `NULL`，傳統 MD5 API 直接讀取該指標導致空指標解參考。
- **做法**：修改 `libkern/conf/files`、`libkern/conf/Makefile.template`、`libkern/crypto/corecrypto_md5.c`，並新增 `libkern/crypto/sweet_md5.c`、`sweet_dec32le.c`、`sweet_enc32le.c`（共 6 檔）。由 `0020-stage-bearssl.sh` 同時暫存 BearSSL 的 `hash/md5.c` 與小端編解碼器，讓 `MD5Init`/`Update`/`Final` 在每次呼叫前後將 `MD5_CTX` 轉換為 `br_md5_context`。
- **復現**：見 README 共用步驟 1–3；無須開機參數，無記錄字串。
- **主機測試**：提交說明記載針對 `md5sum` 的主機測試通過 40 個案例（空輸入、55/56/63/64/65-byte 填充邊界、大輸入與奇數大小分段更新）。
- **驗證紀錄**：上游提交記明 QEMU `0026` 卡在 `MD5Init`，本補丁通過主機測試；未記補丁後實機結果。
- **參考來源**：BearSSL `hash/md5.c` 與編解碼器（由 `0020-stage-bearssl.sh` 取得）
- **結論／未完成**：解決 HFS 卷宗 UUID 計算所需的 MD5 相依。
- **出處**：`ksrc:xnu/patches/0027-sweet-bearssl-md5.patch`；上游提交 `c8117c6`

### S-028 新增 SWEET 內部 UFS 唯讀根裝置驅動（ufsroot0）

- **日期**：2026-10-15 21:42
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：`ufsroot=<分區名>`：開機參數，指定要掛載的 GPT 分區名稱，預設為 `userdata`。
- **問題／現象**：僅 iOS 16 shared cache 即達 3.1 GiB，而核心僅管理 1.2 GiB 且 fastboot boot image 上限為 768 MiB，無法將根檔案系統放進 RAM disk，必須從手機內部 UFS 快閃記憶體掛載根檔案系統。
- **根因**：SWEET 沒有 `IOStorageFamily` 與完整 UFS 驅動堆疊，需直接接管 bootloader（ABL）已初始化的 UFSHCI 2.1 主機控制器。
- **做法**：修改 `bsd/conf/files`、`iokit/bsddev/IOKitBSDInit.cpp` 並新增 `bsd/dev/sweet_ufs.c`（共 3 檔）。利用 ABL 留下的已啟用 UFSHCI 2.1 連線狀態：從裝置樹 `/sweet-ufs` 讀取暫存器位址，將傳輸請求列表指向驅動自行配置的連續實體記憶體（發送 doorbell 前先讀回確認主機已接受），在 LUN 0 每次以輪詢方式發送單一 SCSI `READ(10)` 指令，透過 1 MiB bounce buffer 與前後快取清理／無效化搬運資料（主機非快取一致性）；依 `ufsroot=` 尋找 GPT 分區並以 4 KiB 實體區塊模擬 512-byte 邏輯磁區暴露為 `ufsroot0`/`rufsroot0`（唯讀，拒絕寫入），供 `IOFindBSDRoot` 以 `rd=ufsroot0` 開機。
- **復現**：見 README 共用步驟 1–3；開機參數帶 `rd=ufsroot0`（選配 `ufsroot=<分區名>`）；成功時 console 輸出 `BSD root: %s, major %d, minor %d\n`，異常時輸出 `IOFindBSDRoot: SWEET UFS root %s unavailable: %d`、`SWEET UFS: command 0x%02x timed out\n` 等。
- **主機測試**：無
- **驗證紀錄**：實機已透過 `rd=ufsroot0` 成功自 UFS 掛載 HFS 測試卷宗（見 `0029` 提交說明：`"hfs: mounted SweetRoot on device b(1, 0)"`）。
- **參考來源**：來源未記
- **結論／未完成**：完成唯讀 UFS 根裝置 `ufsroot0`；寫入支援於 `0030` 加入，高記憶體（>4G）DMA 檢查於 `0034` 補上。
- **出處**：`ksrc:xnu/patches/0028-sweet-ufs-root-device.patch`；上游提交 `7fd6423`

### S-029 允許 SWEET 在未驗證／未密封的根卷宗下繼續開機

- **日期**：2026-10-15 21:56
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：將 HFS 測試卷宗刷入 `userdata` 並以 `rd=ufsroot0` 開機後，手機成功掛載 UFS 根目錄（`hfs: mounted SweetRoot on device b(1, 0)`），隨即在 `bsd_init` 發生 panic。
- **根因**：arm64 macOS 組態核心要求非 RAM disk 的根卷宗必須通過 `FSIOC_KERNEL_ROOTAUTH`（sealed system volume），但 SWEET 透過 fastboot 無簽章開機、沒有 Image4/AMFI 信任鏈，且 HFS 無法成為 sealed volume。
- **做法**：修改 `bsd/kern/bsd_init.c`（新增 11 行），在 SWEET 上記錄未驗證根卷宗訊息後繼續開機（Apple 組態保留 panic；userland `pivot_root` 仍要求驗證）。
- **復現**：見 README 共用步驟 1–3；無須開機參數；console 會輸出 `SWEET: root volume is not authenticated (error %d); continuing unsealed\n`。
- **主機測試**：無
- **驗證紀錄**：實機以 `0028` 掛載 `SweetRoot` 後觸發此 panic，本補丁放行之。
- **參考來源**：來源未記
- **結論／未完成**：解除 HFS 根卷宗在 `bsd_init` 的密封驗證阻擋。
- **出處**：`ksrc:xnu/patches/0029-sweet-unsealed-root-volume.patch`；上游提交 `063e285`

### S-030 新增由開機參數 ufsrw=1 控制的 UFS 寫入支援

- **日期**：2026-10-16 06:50
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：`ufsrw=1`：開機參數，啟用 `sweet_ufs` 寫入功能；預設關閉（未設時拒絕所有寫入）。
- **問題／現象**：`0028` 的 `sweet_ufs.c` 為純唯讀實作，無法對根卷宗寫入資料（如 `/private/var`）。
- **根因**：UFS 驅動未實作 `WRITE(10)` 與快取同步指令。
- **做法**：修改 `bsd/dev/sweet_ufs.c`（新增 87 行、刪除 19 行）。在 `ufsrw=1` 時允許透過 bounce buffer 執行 `WRITE(10)`（首尾不滿 4 KiB 裝置區塊時執行 read-modify-write），並支援 `DKIOCSYNCHRONIZE` 對應的 `SYNCHRONIZE CACHE`；每筆寫入在發送前皆再次檢查是否完全落在目標 GPT 分區邊界內，越界則拒絕。
- **復現**：見 README 共用步驟 1–3；開機參數帶 `ufsrw=1`；若拒絕越界寫入 console 輸出 `SWEET UFS: refusing write of %u blocks at 0x%llx\n`。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：來源未記
- **結論／未完成**：完成受 `ufsrw=1` 與邊界檢查保護的 UFS 寫入功能，需搭配 `0031` 的 `rootrw=1` 才能以讀寫模式掛載根目錄。
- **出處**：`ksrc:xnu/patches/0030-sweet-ufs-writes.patch`；上游提交 `1a3aefe`

### S-031 新增開機參數 rootrw=1 並允許掛載非日誌髒 HFS 根卷宗

- **日期**：2026-10-16 06:52
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：`rootrw=1`：開機參數，從開機初始即以讀寫模式掛載根檔案系統；預設維持原唯讀流程。
- **問題／現象**：SWEET 沒有獨立的 data volume，`/private/var` 位於根卷宗上，需要可寫入的根檔案系統；且系統尚無乾淨關機卸載與開機前 `fsck_hfs`，HFS 卷宗常處於 dirty 且無日誌（non-journaled）狀態而被拒掛。
- **根因**：XNU 預設唯讀掛載根目錄，且 HFS 拒絕直接掛載未經檢查的 dirty non-journaled 卷宗。
- **做法**：修改 `bsd/vfs/vfs_subr.c` 與 `bsd/sweet_hfs/core/hfs_vfsutils.c`（新增 24 行），當設定 `rootrw=1` 時直接以讀寫模式掛載根卷宗，並讓 `hfs_mounthfsplus` 接受 dirty non-journaled 根卷宗。
- **復現**：見 README 共用步驟 1–3；開機參數帶 `rootrw=1`（搭配 `ufsrw=1`）；掛載時 console 輸出 `hfs_mounthfsplus: SWEET: mounting dirty non-journaled root read-write\n`。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：來源未記
- **結論／未完成**：解決無獨立 `/private/var` 分區與無乾淨關機時的可寫根掛載問題。
- **出處**：`ksrc:xnu/patches/0031-sweet-read-write-root.patch`；上游提交 `0f0c256`

### S-032 使用 BearSSL 提供傳統 SHA1 API 以修復 IPv6 lo0 掛載崩潰

- **日期**：2026-10-16 07:15
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：IPv6 掛載 `lo0` 時呼叫 `in6_nigroup` → `SHA1Init` 發生崩潰。
- **根因**：無 `corecrypto` kext 時 `g_crypto_funcs` 為 `NULL`，導致傳統 SHA-1 API 讀取空指標。
- **做法**：修改 `libkern/conf/files`、`libkern/conf/Makefile.template`、`libkern/crypto/corecrypto_sha1.c`，並新增 `libkern/crypto/sweet_sha1.c`、`sweet_dec32be.c`、`sweet_enc32be.c`（共 6 檔）。由 `0020-stage-bearssl.sh` 暫存 BearSSL `sha1.c` 與大端編解碼器，於 `SHA1Init`/`Update`/`Final` 轉換 `SHA1_CTX` 與 BearSSL 狀態。
- **復現**：見 README 共用步驟 1–3；無須開機參數，無記錄字串。
- **主機測試**：無
- **驗證紀錄**：上游提交記明 IPv6 掛載 `lo0` 時於 `SHA1Init` 崩潰，未另記補丁後結果。
- **參考來源**：BearSSL `sha1.c` 與大端編解碼器（由 `0020-stage-bearssl.sh` 取得）
- **結論／未完成**：解決核心傳統 SHA-1 API 對 `corecrypto` kext 的依賴。
- **出處**：`ksrc:xnu/patches/0032-sweet-bearssl-sha1.patch`；上游提交 `84ab556`

### S-033 使用 BearSSL 提供 SHA-384／SHA-512 API

- **日期**：2026-10-16 07:16
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：程式碼簽章（`ubc_subr.c`）可能使用 SHA-384 code directory，呼叫 `SHA384_*`/`SHA512_*` 時會解參考 `NULL` 的 `g_crypto_funcs` 而崩潰。
- **根因**：`0023` 僅接通 SHA-256，SHA-384/512 仍依賴不存在的 `corecrypto` kext。
- **做法**：修改 `libkern/conf/files`、`libkern/conf/Makefile.template`、`libkern/crypto/corecrypto_sha2.c`，並新增 `libkern/crypto/sweet_sha2big.c`、`sweet_dec64be.c`、`sweet_enc64be.c`（共 6 檔），以 BearSSL 可攜式實作提供 `SHA384_*`/`SHA512_*`。
- **復現**：見 README 共用步驟 1–3；無須開機參數，無記錄字串。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：BearSSL（由 `0020-stage-bearssl.sh` 取得）
- **結論／未完成**：補齊 `libkern` 傳統雜湊家族最後的 SHA-384/512 支援。
- **出處**：`ksrc:xnu/patches/0033-sweet-bearssl-sha384-sha512.patch`；上游提交 `4ba09a4`

### S-034 保護 UFS DMA 在高記憶體（>4G）下檢查 64-bit 定址能力

- **日期**：2026-10-16 15:10
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：當核心使用手機上段 DDR bank（`mkadt.py --highmem`，`0x1_00000000 + 0xfcc00000`，約 3.95 GiB 連續記憶體且無韌體保留區）時，UFS DMA 記憶體可能位於 4 GiB 以上，若主機未宣告支援 64-bit 定址將無法正常運作。
- **根因**：原 `sweet_ufs.c` 假設開機 RAM 位於 4G 以下（1.2 GiB），未檢查主機 64-bit DMA 能力。
- **做法**：修改 `bsd/dev/sweet_ufs.c`（新增 10 行、刪除 2 行），記錄配置到的 DMA 實體位址，且當位址超過 4 GiB 時檢查主機是否宣告支援 64-bit 定址，不支援則拒絕啟動。
- **復現**：見 README 共用步驟 1–3；搭配 `mkadt.py --highmem` 使用；若主機不支援高於 4G 的 DMA，console 輸出 `SWEET UFS: DMA memory at 0x%llx but the host has no 64-bit addressing\n`。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：來源未記
- **結論／未完成**：確保高記憶體組態下 UFS DMA 定址安全。
- **出處**：`ksrc:xnu/patches/0034-sweet-ufs-dma-above-4g.patch`；上游提交 `5c48961`

### S-035 在 SWEET 內建 AMFI 中加入 XML entitlements 剖析與 platform binary 標記

- **日期**：2026-10-16 15:32～2026-10-16 15:41
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：`sweet_amfi_debug=1`：開機參數（註解於 `sweet_amfi.c` 記載，記錄每個簽章附加的 entitlements 摘要；預設關閉）。
- **問題／現象**：僅回報無 entitlements 的 AMFI stub（`0025`）無法讓需要權限的系統二進位檔運作；此外若在 `adjustContextWithoutMonitor` 才取得 XML entitlements，將 4K 分頁簽章轉換為多層雜湊時會丟棄 XML blob，導致每次 `exec` 失敗（QEMU 出現 `'unable to adjust OSEntitlements context without monitor: 53'`）。
- **根因**：Apple AMFI 依賴閉源 `CoreEntitlements` 與靜態 trust cache 來提供 entitlements 並標記 platform binary；且必須在 `vnode_check_signature` hook 階段（針對 `ubc_cs_blob_add` 傳入的可寫副本）附加 entitlements。
- **做法**：修改 `bsd/kern/bsd_init.c`、`bsd/kern/sweet_amfi.c`、`iokit/conf/files.arm64` 並新增 `iokit/Kernel/arm/SweetEntitlements.cpp`（共 4 檔）。實作 `SweetEntitlements` 類別將已通過 code directory 驗證的 XML entitlements blob 解析為 `OSDictionary` 並保留原始 XML 供 `csops(CS_OPS_ENTITLEMENTS_BLOB)` 使用（僅限 DER 簽章者不予授權）；在 `vnode_check_signature` MAC hook 附加 `csb_entitlements`（不因 entitlements 失敗而阻擋 `exec`），並將根檔案系統上所有已簽章的二進位檔標記為 `CS_PLATFORM_BINARY` 以替代靜態 trust cache。
  - 演進：
    - `813e1d7` 2026-10-16 15:32：於 SWEET 內建 AMFI 加入 `SweetEntitlements` 物件、XML entitlements 查詢／複製介面，以及將根檔案系統已簽章二進位檔標記為 platform binary 的 MAC 政策；新增 `sweet_amfi_debug=1`。
    - `b40a01b` 2026-10-16 15:41：修正為在 `vnode_check_signature` MAC hook（於 4K 分頁轉換丟棄 XML blob 前）附加 entitlements，解決 QEMU 錯誤 `unable to adjust OSEntitlements context without monitor: 53`。
- **復現**：見 README 共用步驟 1–3；選配 `sweet_amfi_debug=1`；console 輸出 `SWEET AMFI: built-in interface registered (XML entitlements)\n` 與 `SWEET AMFI: platform binary policy %s (%d)\n`，開啟除錯則輸出 `SWEET AMFI: %s: %u entitlements (test: bool %d string %d array %d)\n`。
- **主機測試**：無（提及 `tools/mkinit.py` 測試簽章金鑰）
- **驗證紀錄**：上游提交記明 QEMU 出現 `unable to adjust OSEntitlements context without monitor: 53` 並於第二筆提交修正，未記實機後續結果。
- **參考來源**：來源未記
- **結論／未完成**：完成 XML entitlements 與 platform binary 標記，但遺漏設定 `CS_SIGNED` 旗標（隨即由 `0036` 補上）。
- **出處**：`ksrc:xnu/patches/0035-sweet-amfi-entitlements-platform-binaries.patch`；上游提交 `813e1d7`、`b40a01b`

### S-036 SWEET AMFI 為通過驗證的簽章標記 CS_SIGNED

- **日期**：2026-10-16 18:08
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：實機上 `launchd` 發生 panic：`boot task is not a platform binary (fsck)`。子行程的 `csops` 狀態只有 `CS_VALID` 而沒有 `CS_SIGNED`，導致 `csproc_get_blob()` 回傳 `NULL` 且 `CS_PLATFORM_BINARY` 永遠無法被回報。
- **根因**：`0035` 的 `sweet_amfi_vnode_check_signature` 未比照 Apple AMFI 設定 `CS_SIGNED`，使 `exec` 複製狀態後 `csproc_get_blob()` 忽略該行程。
- **做法**：修改 `bsd/kern/sweet_amfi.c`（新增 7 行、刪除 3 行），在 `vnode_check_signature` 內為通過驗證的簽章加上 `cs_flags |= CS_SIGNED`。
- **復現**：見 README 共用步驟 1–3；無須開機參數，無新增記錄字串。
- **主機測試**：無
- **驗證紀錄**：已於實機重現 `boot task is not a platform binary (fsck)` 並於 QEMU 以輸出 `csops status 0x02000001` 的測試 init 驗證問題，本補丁依此修正。
- **參考來源**：來源未記
- **結論／未完成**：補齊 `CS_SIGNED` 旗標後，`fsck` 等開機任務可正確被識別為 platform binary。
- **出處**：`ksrc:xnu/patches/0036-sweet-amfi-cs-signed.patch`；上游提交 `d0cbdf9`

### S-037 在 SWEET 核心組態啟用 bindfs

- **日期**：2026-10-16 19:02
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：實機開機（`phone boot 0036mw`）時，iOS `/sbin/mount -P 1` 嘗試以 `bindfs` bind-mount `/private/preboot/<boot manifest hash>/usr/standalone/firmware`，因核心僅啟用 `nullfs` 而導致 `mount()` 回傳 `ENODEV`、`/sbin/mount` 以 exit code `74` 結束。
- **根因**：衍生自 macOS 的 `MASTER.arm64.sweet` 組態未開啟 `bindfs`。
- **做法**：修改 `config/MASTER.arm64.sweet`（新增 1 行、刪除 1 行），啟用 `bindfs` 檔案系統支援（缺少來源目錄時 `mount` 在 env 0 可容忍 `ENOENT`）。
- **復現**：見 README 共用步驟 1–3；無須開機參數，無記錄字串。
- **主機測試**：無
- **驗證紀錄**：上游提交記明實機 `0036mw` 因 `ENODEV` exit 74 失敗，本補丁啟用 `bindfs`（下一階段於 `0038` 繼續推進至 `keybagd`）。
- **參考來源**：來源未記
- **結論／未完成**：解決 iOS preboot 韌體 bind-mount 所需的 `bindfs` 支援。
- **出處**：`ksrc:xnu/patches/0037-sweet-config-bindfs.patch`；上游提交 `fe93acb`

### S-038 新增 SWEET 內建 AppleKeyStore（無密碼已解鎖與鑰匙圈金鑰封裝）

- **日期**：2026-10-16 19:19（本機提交至 2026-11-17 07:58）
- **狀態**：上游繼承（ios-sweet）；ksrc：本機修改 +100／-1（本機提交 `31241b2`、`7294068`）；本機修改見 K-059
- **開關**：無
- **問題／現象**：實機開機（`phone boot 0037w`）時，launchd 開機任務 `keybagd --init` 開啟 `IOServiceMatching("AppleKeyStore")` 失敗並重啟進 recovery（`FATAL KEYBAG ERROR: failed to load system bag`）。此外在沒有 SEP 的環境下，`securityd` 無法封裝／解封鑰匙圈項目金鑰，導致無法儲存已加入的 Wi-Fi 網路密碼。
- **根因**：Apple 原廠 `AppleKeyStore` kext 為閉源且依賴 SEP；缺少該服務時 `libaks`（`AppleKeyStore.framework`）無法取得系統 keybag handle 與鎖定狀態，也無法處理 selector 10/11（`aks_wrap_key`/`aks_unwrap_key`）。
- **做法**：修改 `iokit/conf/files.arm64`、`iokit/KernelConfigTables.cpp` 並新增 `iokit/Kernel/arm/SweetKeyStore.cpp`（共 3 檔）。在 `IOResources` 註冊 `AppleKeyStore` 與 `AppleKeyStoreUserClient`，回應 iOS 16.7 `libaks` selector：0（open）、14（`get_system` 回傳 handle 0 讓 `keybagd` 略過 `systembag.kb`）、7（lock state `0x4` 表示開機起已解鎖）、17（以 DER SET 回傳 `decode_extended_state` 預期欄位，含 `sls 0 = unlocked`），對 unload/set_system/unlock/lock/assertions 回傳空操作成功，未知 selector 回傳 `kIOReturnUnsupported` 並記錄呼叫者與參數形狀。本機後續於 selector 10/11 實作以固定核心種子進行 HMAC-SHA256 驗證封裝（`blob = tag(8) || key ^ pad(32)`，40 bytes），使鑰匙圈金鑰可跨重啟保存（不防範持有磁碟者）。
- **復現**：見 README 共用步驟 1–3；無須開機參數；console 輸出 `SWEET AKS: AppleKeyStore registered (no passcode, unlocked)\n` 與各 selector 呼叫紀錄 `SWEET AKS: %s sel %u arg0 %llu in %u/%u out %u/%u -> 0x%x\n`。
- **主機測試**：無
- **驗證紀錄**：實機 `0037w` 曾觸發 `FATAL KEYBAG ERROR: failed to load system bag`；本機加入 selector 10/11 wrap/unwrap 並將 `0038` 與 `0043` context 分離（`31241b2`、`7294068`）。
- **參考來源**：iOS 16.7 `AppleKeyStore.framework`（`libaks` selector 編號與 DER 結構對照）
- **結論／未完成**：`keybagd` 可順利通過初始化，且 `securityd` 具備跨重啟的鑰匙圈金鑰封裝／解封能力（註：本機提交 `31241b2` 說明亦提及 Wi-Fi shim 記錄 `kev_post_msg` 失敗與前三次 `RSSI_CHANGED` 投遞）。
  - 基底 `979e2f08` 之後的本機提交（舊到新，箭頭後是記錄該修改的項目）：
    - `31241b2` 2026-11-17 07:56：Wrap and unwrap keychain item keys in the AKS stand-in so securityd can store a joined network's password, and say whether RSSI_CHANGED events reach wifid → K-059
    - `7294068` 2026-11-17 07:58：Keep the AKS wrap definitions clear of the context patch 0043 relies on → K-059
- **出處**：`ksrc:xnu/patches/0038-sweet-applekeystore.patch`；上游提交 `28a1b1d`；本機提交 `31241b2`、`7294068`

### S-039 將 SWEET 的 coredump 預設設為關閉

- **日期**：2026-10-16 20:10
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無（沿用既有 sysctl `kern.coredump`，SWEET 預設改為 `0`，設為 `1` 可重新開啟）
- **問題／現象**：實機（`boot 0038e`）`launchd_cache_loader` 崩潰時，核心花費 91 秒寫入 2.7 GB 的 core dump 至根卷宗，導致 `launchd` 超時 panic。
- **根因**：崩潰的 iOS daemon 會映射整個 shared region，單一 core dump 動輒數 GB，在手機 UFS 上寫入需耗時數分鐘。
- **做法**：修改 `bsd/kern/kern_core.c`（新增 7 行、刪除 1 行），在 SWEET 上將 `coredump` 預設值改為關閉，保留 `kern.coredump` sysctl 供手動開啟。
- **復現**：見 README 共用步驟 1–3；無須開機參數，無記錄字串；需除錯 core dump 時於執行期設定 `kern.coredump=1`。
- **主機測試**：無
- **驗證紀錄**：實機 `0038e` 量測寫入 2.7 GB core 耗時 91 秒並觸發 `launchd` panic；本補丁關閉預設 dump 以避免超時。
- **參考來源**：來源未記
- **結論／未完成**：預設關閉 coredump 以縮短 daemon 崩潰時的阻斷時間。
- **出處**：`ksrc:xnu/patches/0039-sweet-coredump-default-off.patch`；上游提交 `abf31b9`

### S-040 新增 SWEET 內建 IOWatchdog 服務並上限化 HFS 連結修復紀錄

- **日期**：2026-10-16 20:33
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：實機開機（`phone boot 0039u`）進入 launchd daemon 階段後，具 `_PanicOnCrash` 屬性的 `watchdogd` 因 `'failed to discover watchdog KEXT service'` 結束而引發系統 panic；此外 Linux `rsync` 建立的硬連結缺少 `kHFSHasLinkChainBit`，HFS 每次查詢都會在記憶體修復並輸出一行 console 紀錄，數百行輸出嚴重拖慢開機。
- **根因**：SWEET 缺少 Apple `IOWatchdog` kext；同時本補丁上游提交一併限制 HFS link-repair 訊息（註：卷宗檔案清單僅顯示 `SweetWatchdog.cpp` 等 3 檔，HFS 紀錄限制另見 `0041`）。
- **做法**：修改 `iokit/conf/files.arm64`、`iokit/KernelConfigTables.cpp` 並新增 `iokit/Kernel/arm/SweetWatchdog.cpp`（共 3 檔）。在 `IOResources` 註冊 `IOWatchdog`（以 type 1 開啟 user client），接受所有 selector（check-in 1、enable/disable userspace monitoring 3/4、0/5、struct calls 2/9）並回傳全零輸出與成功狀態，每個 selector 僅於首次使用時記錄一次。
- **復現**：見 README 共用步驟 1–3；無須開機參數；console 輸出 `SWEET WDT: IOWatchdog registered (accepts check-ins, never fires)\n` 與首次呼叫紀錄 `SWEET WDT: %s sel %u in %u/%u out %u/%u -> ok\n`。
- **主機測試**：無
- **驗證紀錄**：實機 `0039u` 因缺少 `IOWatchdog` 觸發 `watchdogd` panic；本補丁提供空接受服務以通過開機。
- **參考來源**：來源未記
- **結論／未完成**：解決 `watchdogd` 開機崩潰；不實作實際硬體重置。
- **出處**：`ksrc:xnu/patches/0040-sweet-iowatchdog.patch`；上游提交 `0c4e04f`

### S-041 限制 HFS 硬連結修復訊息輸出次數

- **日期**：2026-10-16 20:33
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：Linux `rsync` 建立的硬連結缺少 `kHFSHasLinkChainBit`，HFS 每次查詢都會在記憶體中修復該旗標並輸出一行 console 訊息，開機期間數百行輸出嚴重拖慢啟動速度。
- **根因**：`bsd/sweet_hfs/core/hfs_catalog.c` 對每次記憶體中修復硬連結旗標皆無上限地印出訊息。
- **做法**：修改 `bsd/sweet_hfs/core/hfs_catalog.c`（新增 25 行、刪除 3 行），在 SWEET 上限制硬連結修復／計數修正紀錄僅印出前 8 筆（註：與 `0040` 同屬上游提交 `0c4e04f`）。
- **復現**：見 README 共用步驟 1–3；無須開機參數；若觸發修復，console 僅於前幾次出現 `hfs: set hardlink bit on vol=%s cnid=%u inoid=%u\n`、`hfs: clear hardlink bit on vol=%s cnid=%u\n`、`hfs: set linkcount=1 on vol=%s cnid=%u old=%u\n`。
- **主機測試**：無
- **驗證紀錄**：實機 `0039u` 觀察到數百行 HFS 連結修復紀錄拖慢開機，本補丁將其上限設為前 8 筆。
- **參考來源**：來源未記
- **結論／未完成**：減少 HFS 硬連結修復對 console 與開機時間的干擾。
- **出處**：`ksrc:xnu/patches/0041-sweet-hfs-link-notes.patch`；上游提交 `0c4e04f`

### S-042 精簡 SWEET 上每次 iOS exec 的 console 紀錄為單行

- **日期**：2026-10-16 20:42
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：實機開機（`phone boot 0041u`）時，每個 daemon 啟動都會印出三行的 `'Allowing spawn of iOS binary'` 提示；當 `launchd` 啟動數十個 daemon 時，關鍵錯誤與 panic 訊息會被捲出螢幕。
- **根因**：`bsd/kern/kern_exec.c` 對每次執行 iOS 二進位檔印出過多冗長提示。
- **做法**：修改 `bsd/kern/kern_exec.c`（新增 5 行），在 SWEET 上將每次 iOS `exec` 改為印出一行簡短的 `exec %s\n`。
- **復現**：見 README 共用步驟 1–3；無須開機參數；console 輸出 `exec %s\n`。
- **主機測試**：無
- **驗證紀錄**：實機 `0041u` 因三行 exec 訊息洗掉 panic 畫面而進行精簡；未另記補丁後實機結果。
- **參考來源**：來源未記
- **結論／未完成**：縮減開機過程的 console 洗版，保留空間給關鍵異常訊息。
- **出處**：`ksrc:xnu/patches/0042-sweet-short-exec-log.patch`；上游提交 `da25951`

### S-043 在 console 印出 SWEET 行程崩潰報告並實作 AKS selector 103

- **日期**：2026-10-16 21:23
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：實機開機（`phone boot 0042u`）時，`watchdogd`、`SpringBoard`、`backboardd`、`lockdownd` 等行程接連崩潰，corpse 很快耗盡，且手機無法取回 crash log，完全看不出崩潰原因。
- **根因**：`proc_prepareexit` 未在 console 輸出足以離線符號化的暫存器與堆疊資訊；此外 `AppleKeyStore` 缺少 selector 103（`notify_state_is_set`）。
- **做法**：修改 `osfmk/kern/exception.c`、`bsd/kern/kern_exit.c`、`iokit/Kernel/arm/SweetKeyStore.cpp`（新增 71 行）。在 `proc_prepareexit` 於退出／崩潰執行緒上印出訊號與例外碼、shared cache slide（`unslid = pc - slide`）、主二進位檔 Mach-O header 位址（`offset = pc - mh`）、user `pc`/`lr`/`sp`/`fp`/`far` 與 10 層 frame-pointer backtrace；同時讓 `AppleKeyStore` 回應 selector 103 為 `0`（not set）。
- **復現**：見 README 共用步驟 1–3；無須開機參數；行程崩潰時 console 輸出 `CRASH %s[%d] sig %d code 0x%llx sub 0x%llx slide 0x%llx mh 0x%llx\n`、`CRASH %s pc 0x%llx lr 0x%llx sp 0x%llx fp 0x%llx far 0x%llx\n` 與 `CRASH %s bt ...`。
- **主機測試**：無
- **驗證紀錄**：實機 `0042u` 觀察到多支 daemon 崩潰無訊息，促使加入 console 崩潰報告；`0043p` 隨即顯示 `CommCenter` 與 `identityservicesd` 於 `imp_implementationWithBlock` 觸發 `_objc_fatal`。
- **參考來源**：來源未記
- **結論／未完成**：可於 console 取得離線符號化所需暫存器與堆疊；`0044` 緊接著補上 exit reason 文字輸出。
- **出處**：`ksrc:xnu/patches/0043-sweet-console-crash-log.patch`；上游提交 `5780917`

### S-044 在 console 崩潰報告印出 exit reason 文字

- **日期**：2026-10-16 22:00
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：實機開機（`phone boot 0043p`）顯示 `CommCenter` 與 `identityservicesd` 在 `imp_implementationWithBlock` 發生 `_objc_fatal`，但透過 `abort_with_payload`（`_objc_fatal`、`os_crash_fmt` 等）送出的錯誤原因文字在 console 看不到。
- **根因**：核心已持有該 exit reason 文字，但 `0043` 的 `kern_exit.c` 尚未將其印出。
- **做法**：修改 `bsd/kern/kern_exit.c`（新增 7 行），在崩潰報告中附加印出 exit reason 的 namespace、code 與文字內容（同筆提交亦於 `mkadt` 設定 `/arm-io/wdt` `no-pmu`，但註明 customer build `watchdogd` 會忽略 `-wdt_no_pmu`）。
- **復現**：見 README 共用步驟 1–3；無須開機參數；有 exit reason 時 console 輸出 `CRASH %s reason ns %u code 0x%llx: %s\n`。
- **主機測試**：無
- **驗證紀錄**：實機 `0043p` 發現看不到 `_objc_fatal` 原因，故補上 exit reason 輸出。
- **參考來源**：來源未記
- **結論／未完成**：補齊 `abort_with_payload` 原因文字；同時發現 `watchdogd` 改查 `IOPMUPrimary` 問題，由 `0045` 解決。
- **出處**：`ksrc:xnu/patches/0044-sweet-crash-log-reason.patch`；上游提交 `72cb993`

### S-045 在 IOWatchdog 上標記 IOPMUPrimary 以通過 watchdogd 的 PMU 階段檢查

- **日期**：2026-10-16 22:22
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：`watchdogd` 持續崩潰（`main+0xb82c via 0x9bb0`）：它在 check-in 之前會尋找符合 `IOPropertyMatch {IOPMUPrimary: 1}` 的 `IOService` 以寫入裝置啟動階段，找不到就直接呼叫 `_os_crash()`。
- **根因**：`-wdt_no_pmu` 開機參數與 `/arm-io/wdt` 的 `no-pmu` 屬性僅在 internal build 有效，customer build 會忽略；而 SWEET 沒有 Apple PMU 服務。
- **做法**：修改 `iokit/Kernel/arm/SweetWatchdog.cpp`（新增 13 行），讓內建 `IOWatchdog` 服務帶上 `IOPMUPrimary = 1` 屬性並接受屬性寫入。
- **復現**：見 README 共用步驟 1–3；無須開機參數，無新增記錄字串。
- **主機測試**：無
- **驗證紀錄**：實機先前因缺少 `IOPMUPrimary` 導致 `watchdogd` 在 `main+0xb82c` 崩潰，未記補丁後單獨結果。
- **參考來源**：來源未記
- **結論／未完成**：解決 customer build `watchdogd` 找不到 PMU 服務而崩潰的問題。
- **出處**：`ksrc:xnu/patches/0045-sweet-iowatchdog-pmu-primary.patch`；上游提交 `307686f`

### S-046 實作 SWEET 內建 IOSurface 與 IOMobileFramebuffer（CPU 顯示路徑）

- **日期**：2026-10-17 11:22～2026-10-17 12:13
- **狀態**：上游繼承（ios-sweet）；ksrc：本機修改 +4／-1（本機提交 `75148d2`）；本機修改見 G-019、K-072
- **開關**：`sweetfb_noconsole=1`：開機參數，於首次 swap 時暫停螢幕文字主控台（改走序列）；預設為 `0`（保留核心文字疊加，以便 panic 仍可在螢幕看到）。
- **問題／現象**：`backboardd` 的視窗伺服器因 CoreAnimation 找不到顯示器而死亡：QuartzCore 僅開啟 `IOMobileFramebuffer.framework` 列出的顯示器，且所有 CoreAnimation 緩衝區皆為 `IOSurface`；此外初期實作在 QEMU 遇到 `AppleCLCD` 無法配對 `/arm-io` 子節點，以及 swap 越界／非同步 vsync 參考未歸零等安全性問題。
- **根因**：Apple 的 `IOSurface` 與 `IOMobileFramebuffer` kext 皆為閉源；且 `IOPlatformDevice::compareName` 會向其 provider（`IOPlatformExpert`）查詢名稱，使掛在 `/arm-io` 節點下的 nub 無法與 `AppleCLCD` 配對。
- **做法**：修改 `iokit/conf/files.arm64`、`iokit/KernelConfigTables.cpp`、`iokit/Kernel/arm/SweetPlatformExpert.cpp`，並新增 `iokit/Kernel/arm/SweetSurface.h`、`SweetSurface.cpp`、`SweetFramebuffer.cpp`（共 6 檔）。依逆向之 iOS 16.7 user-client ABI 實作：
  - `IOCoreSurfaceRoot` + `IOSurfaceRootUserClient`：支援 selector 13（limits 0x18、ABI version 1）、0（create，解析二進位／XML 屬性字典，配置 wired 記憶體並映射至呼叫者，總預算 1.5 GiB）、4（id 查詢）、34（mach port 查詢）、35（建立 mach port）、0x68/0xa68 長度 lock result（含 `+0x78` per-plane row stride），以及 traps 0/1（use count）、2/3（lock/unlock）、4/5（release）。
  - `AppleCLCD`（繼承 `IOMobileFramebuffer`，掛在直屬 `SweetPlatformExpert` 的 `/arm-io/disp0` nub）：提供 selector 3（`GetLayerDefaultSurface`，將 ABL 面板記憶體發布為預設 surface）、4（`SwapBegin`）、5（`SwapEnd`，struct 0x46c，最多 4 層 CPU 複製至面板記憶體並檢查緩衝區邊界與 1x1-element）、6（`SwapWait`）、7（`GetID`）、8（`GetDisplaySize`）、18（`IsMainDisplay`）、28（`GetDotPitch`）、72（60 Hz vsync timer 通知，歸零並標記 64-bit async ref）。
  - 演進：
    - `6ed5e3f` 2026-10-17 11:22：實作內建 `IOCoreSurfaceRoot` 與 `AppleCLCD`（CPU 複製顯示路徑、60 Hz vsync 通知、預設首次 swap 暫停文字主控台並提供 `sweetfb_console=1`）。
    - `1b5ffe0` 2026-10-17 11:33：QEMU 上 `AppleCLCD` 無法配對，將 `/arm-io` 子 nub 直接掛在 `SweetPlatformExpert` 下。
    - `6b618f2` 2026-10-17 11:49：改為預設保留核心文字主控台（因 `kPEDisableScreen` 會使螢幕看不到後續 panic），改以 `sweetfb_noconsole=1` 暫停。
    - `042bb7b` 2026-10-17 12:13：審查修正（swap 列數邊界與 1x1 元素檢查、vsync async ref 歸零與 64-bit 標記、notification port 加鎖釋放、1.5 GiB wired 預算與同執行緒重複映射防護等）。
- **復現**：見 README 共用步驟 1–3；選配 `sweetfb_noconsole=1`；console 輸出 `SWEET SURF: IOCoreSurfaceRoot registered\n` 與 `SWEET FB: AppleCLCD %ux%u stride %u at 0x%llx, default surface %u\n`。
- **主機測試**：無
- **驗證紀錄**：QEMU 驗證 `IOCoreSurfaceRoot` 註冊並修正 `/arm-io` nub 掛載讓 `AppleCLCD` 成功配對；實機後續整合驗證見 `0053`（`backboardd` 成功開啟 `AppleCLCD` 與 `IOCoreSurfaceRoot`）。
- **參考來源**：逆向 iOS 16.7 `IOSurface.framework` 與 `IOMobileFramebuffer.framework` ABI
- **結論／未完成**：建立核心內建顯示與 surface 基礎架構；後續 `0056` 將 swap 改為 vsync 節流複製，並於 fuxi 系列（`0098`、`0106`、`0108`）擴充硬體／GPU scanout。
  - 基底 `979e2f08` 之後的本機提交（舊到新，箭頭後是記錄該修改的項目）：
    - `75148d2` 2026-11-14 19:45：base: 979e2f08 + harness patches (HEAD 2aae087)（本份是 `kernel-surface-stride.patch`：SweetSurface +0x10 row stride） → G-019、K-072
- **出處**：`ksrc:xnu/patches/0046-sweet-iosurface-iomfb.patch`；上游提交 `6ed5e3f`、`1b5ffe0`、`6b618f2`、`042bb7b`；本機提交 `75148d2`

### S-047 新增 userdata 尾端持續性 console 紀錄環（sweetlog）

- **日期**：2026-10-17 15:25
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：`sweetlog=0`：開機參數，停用持續性 console 紀錄寫入；預設啟用。
- **問題／現象**：當時只能靠錄影手機螢幕來讀 console，影片模糊難辨且無法完整留存。
- **根因**：手機無可用序列埠且尚無其他關機後可讀取的 log 管道。
- **做法**：修改 `bsd/dev/sweet_ufs.c` 與 `osfmk/console/serial_console.c`（新增 120 行）。所有透過 `_cnputs` 輸出至 console 的字元（含核心 `printf` 與 daemon 的 `/dev/console`）同步寫入 4 MiB 環形緩衝區；當 UFS 分區可寫後，背景執行緒約每秒一次將新區塊寫入 `userdata` 分區**最後 4 MiB + 4 KiB**（遠離分區前端的 HFS 映像，首個區塊為 `"SWEETLOG"` header：版本 1、ring size、總寫入位元組數、flush sequence），供從 recovery 以 `tools/sweet-log-pull.sh`／`sweet-log.py` 拉取解碼。
- **復現**：見 README 共用步驟 1–3；預設啟用（`sweetlog=0` 關閉）；啟動寫入時 console 輸出 `SWEET LOG: console log to the last %u KiB of the partition (block 0x%llx)\n`。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果（下一筆 `0048` 隨即使用 `boot 0047n` 取得持續性 log）。
- **參考來源**：來源未記
- **結論／未完成**：建立離線讀取開機 console 紀錄的基礎設施（註：後續 fuxi 系列仍沿用類似寫入尾端區塊之概念與調整）。
- **出處**：`ksrc:xnu/patches/0047-sweet-persistent-console-log.patch`；上游提交 `0355a03`

### S-048 將指定 daemon 寫入 /dev/null 的輸出側錄至 console（sweetnull）

- **日期**：2026-10-17 15:43
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：`sweetnull=<名單>`：開機參數，逗號分隔要側錄 `/dev/null` 輸出的行程名稱；預設 `"backboardd,SpringBoard"`，設為 `"-"` 關閉。
- **問題／現象**：從 `boot 0047n`（v8 映像）的持續性 log 發現：若將 `backboardd` 的 `launchd.plist` `StandardOutPath`/`StandardErrorPath` 指向 `/dev/console` 會導致它完全無法啟動；改回原本後它雖啟動但因未捕獲例外而死亡，例外文字全寫進 `/dev/null` 看不到。
- **根因**：`launchd` 啟動的 daemon 預設將 stdout/stderr 導向 `/dev/null`，而指向 `/dev/console` 會阻礙啟動。
- **做法**：修改 `bsd/dev/mem.c`（新增 54 行），攔截名稱符合 `sweetnull` 清單之行程對 `/dev/null` 的寫入，以 `|name| text` 格式複製一份到 console（進而寫入 `sweetlog`）。
- **復現**：見 README 共用步驟 1–3；預設側錄 `backboardd,SpringBoard`（可用 `sweetnull=-` 關閉）；console 輸出 `|%s| %.*s%s`。
- **主機測試**：無
- **驗證紀錄**：上游提交記明 `boot 0047n` 觀察到 `backboardd` 因 stdout/stderr 導向 `/dev/console` 無法啟動且例外寫入 `/dev/null`，本補丁以此側錄之。
- **參考來源**：來源未記
- **結論／未完成**：可在不影響 daemon 啟動的前提下取得其 stdout/stderr 例外訊息。
- **出處**：`ksrc:xnu/patches/0048-sweet-devnull-tee.patch`；上游提交 `38322a4`

### S-049 追蹤指定行程的 IOKit 查詢與配對呼叫（sweetiolog）

- **日期**：2026-10-17 16:06
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：`sweetiolog=<名單>`：開機參數，逗號分隔要追蹤 IOKit 查詢的行程名稱；預設 `"backboardd"`（`"-"` 關閉）。
- **問題／現象**：`backboardd` 在開啟 `AppleCLCD` 或 `IOCoreSurfaceRoot` 之前就死亡，且例外原因沒有寫到任何可讀位置，無法得知它在尋找什麼 IOKit 服務。
- **根因**：缺乏針對 userland 行程呼叫 IOKit registry／matching 過程的即時追蹤紀錄。
- **做法**：修改 `iokit/Kernel/IOUserClient.cpp`（新增 114 行、刪除 2 行），對符合 `sweetiolog` 名單的行程記錄其 registry 路徑查詢、child iterator、matching（含結果筆數）、matching notification 與 `IOServiceOpen`，以 `IOK name: ...` 印出（每次開機最多 400 行）。
- **復現**：見 README 共用步驟 1–3；預設追蹤 `backboardd`；console 輸出 `IOK %s: %s %.240s -> %s\n`、`IOK %s: path %.200s -> %s\n`、`IOK %s: open %s type %u -> 0x%x\n` 等。
- **主機測試**：無
- **驗證紀錄**：已於 `boot 0049n` 取得 `backboardd` 的 IOKit 追蹤紀錄（見 `0050` 提交說明）。
- **參考來源**：來源未記
- **結論／未完成**：成功定位顯示探索與 Sandbox 阻擋問題；後續 `0054` 將預設名單擴充包含 `SpringBoard`。
- **出處**：`ksrc:xnu/patches/0049-sweet-iokit-trace.patch`；上游提交 `40cb442`

### S-050 新增寬鬆的 SWEET 內建 Sandbox MAC 政策

- **日期**：2026-10-17 16:25
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：從 `boot 0049n` 與 `tracev3` 紀錄發現，`libsystem_sandbox` 透過 `__mac_syscall("Sandbox", op, arg)` 呼叫 Sandbox kext，因無該名稱的 MAC 政策導致每次呼叫回傳 `ENOPOLICY`；`launchd` 對 Mach lookup 報錯 `'error = 159: Sandbox restriction'`（`backboardd` 連不上 `mobilegestalt`、`iohideventsystem`、`notification_center`、`logd`），`tccd` 則報 `'profile not found'`。
- **根因**：Apple 的 Sandbox kext 為閉源，核心未註冊 `"Sandbox"` MAC policy。
- **做法**：修改 `bsd/kern/sweet_amfi.c`（新增 47 行），註冊名為 `"Sandbox"` 的寬鬆 MAC 政策：允許所有檢查與 profile 初始化，需要回傳資料的操作（如 container 路徑、extension token）則回傳 `ENOENT`，並記錄每個 op 編號的首次使用。
- **復現**：見 README 共用步驟 1–3；無須開機參數；console 輸出 `SWEET Sandbox: permissive policy %s (%d)\n` 與各 op 首次呼叫 `SWEET Sandbox: %s op %d\n`。
- **主機測試**：無
- **驗證紀錄**：基於 `boot 0049n` 與 `tracev3` 發現實作；套用後 `backboardd` 得以進行 Mach lookup（下一個卡點為 `0051` 的未捕獲 `NSException`）。
- **參考來源**：來源未記
- **結論／未完成**：解除 `Sandbox restriction`（error 159）對系統 daemon Mach 通訊的阻擋。
- **出處**：`ksrc:xnu/patches/0050-sweet-sandbox-policy.patch`；上游提交 `289f15b`

### S-051 行程 SIGABRT 崩潰時掃描並印出 malloc heap 可列印字串（sweetscan）

- **日期**：2026-10-17 16:35
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：`sweetscan=<名單>`：開機參數，逗號分隔於 `SIGABRT` 時掃描 heap 字串的行程名稱；預設 `"backboardd"`（`"-"` 關閉）。
- **問題／現象**：加入 Sandbox 政策後，`backboardd` 仍因未捕獲 `NSException` 死亡；abort 訊息僅指出例外型別，CoreFoundation 將詳細原因寫入無法讀取的 `os_log`。
- **根因**：缺少取得 `os_log` 或堆疊中 `NSException` reason 字串的除錯管道。
- **做法**：修改 `osfmk/kern/exception.c`（新增 98 行），當 `sweetscan` 名單中的行程因 `SIGABRT` 終止時，掃描其 malloc heap（上限 96 MiB），將長度 ≥24 字元且含 ≥2 個空白的可列印字串以 `STR text` 印出（上限 700 行）。
- **復現**：見 README 共用步驟 1–3；預設掃描 `backboardd`；觸發時 console 輸出 `STR scan of %s heap\n`、`STR %s\n`、`STR done: %llu KiB scanned, %d strings\n`。
- **主機測試**：無
- **驗證紀錄**：上游提交未記套用後實機結果（後續 `0053` 擴充至 `SpringBoard` 與 `SIGTRAP`）。
- **參考來源**：來源未記
- **結論／未完成**：提供崩潰當下自 heap 撈取 `NSException` 原因字串的除錯能力。
- **出處**：`ksrc:xnu/patches/0051-sweet-crash-heap-strings.patch`；上游提交 `a65ee61`

### S-052 在 HFS 上以 VNOP_EXCHANGE 模擬 RENAME_SWAP

- **日期**：2026-10-17 16:54
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：`usermanagerd` 無法儲存 `usersession.kb` 或 `persona.kb`，報錯 `'Failed to atomically swap writing file: Operation not supported (45)'`。
- **根因**：Foundation 的原子寫入使用 `renamex_np(RENAME_SWAP)`，而 HFS 不支援 `RENAME_SWAP`（回傳 `ENOTSUP`）。
- **做法**：修改 `bsd/vfs/kpi_vfs.c`（新增 14 行），當 `VNOP_RENAMEX` 對同一卷宗上兩個一般檔案的交換回傳 `ENOTSUP` 時，改以 `VNOP_EXCHANGE`（`exchangedata`）交換資料與 cnode 描述子。
- **復現**：見 README 共用步驟 1–3；無須開機參數，無記錄字串。
- **主機測試**：無
- **驗證紀錄**：實機先前因 `ENOTSUP (45)` 失敗，套用後於 `boot 0052n`（image v9）觀察到後續進展（見 `0053`）。
- **參考來源**：來源未記
- **結論／未完成**：解決 HFS 根卷宗不支援 Foundation 原子交換寫入的問題。
- **出處**：`ksrc:xnu/patches/0052-sweet-hfs-rename-swap.patch`；上游提交 `c2543cc`

### S-053 將 heap 字串掃描擴充至 SpringBoard 與 SIGTRAP

- **日期**：2026-10-17 17:13
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無（修改 `0051` 既有的 `sweetscan` 預設值為 `"backboardd,SpringBoard"`）
- **問題／現象**：`boot 0052n`（image v9）上 `backboardd` 不再崩潰並已開啟 `AppleCLCD` 與 `IOCoreSurfaceRoot`；但 `SpringBoard` 因 `FBSDisplayMode` 有效性檢查失敗而以 `sig 5`（`SIGTRAP`）崩潰，且原因只留在其崩潰訊息中。
- **根因**：`0051` 的 heap 掃描僅涵蓋 `SIGABRT` 且預設名單只有 `backboardd`，未涵蓋 `BSAssert`/`NSAssert` 觸發的 `SIGTRAP` 與 `SpringBoard`。
- **做法**：修改 `osfmk/kern/exception.c`（新增 5 行、刪除 4 行），將 `sweetscan` 預設名單改為 `"backboardd,SpringBoard"`，並同時於 `SIGABRT` 與 `SIGTRAP` 時執行 heap 字串掃描。
- **復現**：見 README 共用步驟 1–3；預設生效；`SpringBoard` 觸發 `SIGTRAP` 時輸出 `STR ...`。
- **主機測試**：無
- **驗證紀錄**：上游提交記明 `boot 0052n`（image v9）觀察到 `backboardd` 已能開啟 `AppleCLCD` 與 `IOCoreSurfaceRoot`，而 `SpringBoard` 以 `sig 5` 崩潰。
- **參考來源**：來源未記
- **結論／未完成**：可捕捉 `SpringBoard` 等行程因 `SIGTRAP` 斷言失敗時的 heap 除錯字串。
- **出處**：`ksrc:xnu/patches/0053-sweet-crash-scan-springboard.patch`；上游提交 `adacf09`

### S-054 新增使用者空間行程抽樣器、swap 計數器並追蹤 SpringBoard IOKit

- **日期**：2026-10-17 18:48
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：`sweetsample=<名單>`：開機參數，逗號分隔要定期抽樣各執行緒 user state 的行程名稱；預設 `"SpringBoard,backboardd"`（`"-"` 關閉）。
- **問題／現象**：經過資料遷移畫面後，`SpringBoard` 維持執行但畫面不再繪製任何新內容，且 log 沒有顯示原因，無法得知 daemon 卡在哪個系統呼叫或函式。
- **根因**：缺乏無除錯器環境下的行程各執行緒使用者態堆疊抽樣與畫面 swap 進度觀察工具。
- **做法**：修改 `bsd/conf/files`、`bsd/dev/sweet_ufs.c`、`osfmk/kern/exception.c`、`iokit/Kernel/IOUserClient.cpp`、`iokit/Kernel/arm/SweetFramebuffer.cpp` 並新增 `bsd/dev/sweet_sample.c`（共 6 檔）。建立核心執行緒於啟動 90 秒後、每 120 秒抽樣一次（最多 10 次）`sweetsample` 行程各執行緒已儲存的 `pc`/`lr`/`sp` 與 frame-pointer backtrace（含 shared region slide 與主執行檔 Mach-O header 供 `tools/sweet-crash.py` 符號化）；在 `SweetFramebuffer` 每 100 次 swap 印出計數以確認 `backboardd` 是否持續送幀；並將 `sweetiolog` 預設名單擴充為 `"backboardd,SpringBoard"`。
- **復現**：見 README 共用步驟 1–3；預設啟用；console 輸出 `SMP --- sample %d\n`、`SMP %s[%d] slide 0x%llx mh 0x%llx\n`、`SMP %s t%d pc 0x%llx lr 0x%llx sp 0x%llx bt ...` 與 `SWEET FB: %llu swaps\n`。
- **主機測試**：無
- **驗證紀錄**：上游提交未記補丁後實機結果（隨即於 `0055`、`0056` 依抽樣結果修正）。
- **參考來源**：來源未記
- **結論／未完成**：建立背景行程卡死定位工具；`0055` 隨即加深其 backtrace 深度與抽樣次數。
- **出處**：`ksrc:xnu/patches/0054-sweet-process-sampler.patch`；上游提交 `e3af71e`

### S-055 實作 AKS selector 35／101 並加深行程抽樣器深度與次數

- **日期**：2026-10-17 19:11
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：`SpringBoard` 與多個 daemon 呼叫 `aks_get_extended_device_state`（selector 35），`datamigrator` 呼叫 `aks_assert_consume`（selector 101），兩者先前都回傳 `unsupported`；此外資料遷移耗時長，需要更多抽樣次數與更深堆疊才能看到遷移後的狀態。
- **根因**：`SweetKeyStore.cpp` 尚未實作 selector 35 與 101；且 `0054` 僅取 10 次樣本。
- **做法**：修改 `iokit/Kernel/arm/SweetKeyStore.cpp`、`osfmk/kern/exception.c`、`bsd/dev/sweet_sample.c`（新增 8 行、刪除 4 行）。讓 selector 35 回傳與 selector 17 相同的 DER 裝置狀態，selector 101 直接回傳成功；抽樣器改為擷取 16 層 backtrace 與最多 40 次抽樣。
- **復現**：見 README 共用步驟 1–3；無須開機參數，無新增記錄字串。
- **主機測試**：無
- **驗證紀錄**：上游提交未記補丁後實機結果。
- **參考來源**：來源未記
- **結論／未完成**：補齊 `SpringBoard`／`datamigrator` 所需的 AKS selector，並延長抽樣涵蓋時間。
- **出處**：`ksrc:xnu/patches/0055-sweet-aks-ext-state-sampler-depth.patch`；上游提交 `9004c60`

### S-056 將 SweetFramebuffer swap 改由 vsync 計時器以 10 Hz 節流呈現

- **日期**：2026-10-17 20:06
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：`sweetfb_hz=<頻率>`：開機參數，設定 vsync 計時器將最新畫面複製到面板的頻率（Hz）；預設 `10` Hz。
- **問題／現象**：`backboardd` 以約 58 Hz 呈現資料遷移畫面，核心在每次 swap 時立即將整張 10 MB 畫面複製進面板記憶體，造成單一 CPU 上約 600 MB/s 的 `memcpy` 負載，使資料遷移（以及在 `BKSSystemShellService` 等待 check-in 的 `SpringBoard`）慢如牛步。
- **根因**：`0046` 在每次 `SwapEnd` 同步執行整張幀緩衝區的 CPU `memcpy`。
- **做法**：修改 `iokit/Kernel/arm/SweetFramebuffer.cpp`（新增 38 行、刪除 3 行），改為 swap 僅記錄最新幀資訊，由 vsync 計時器每 `fFlushTicks` tick（依 `sweetfb_hz`，預設 10 Hz）才複製一次最新畫面到面板。
- **復現**：見 README 共用步驟 1–3；選配 `sweetfb_hz=<Hz>`（預設 10）；無新增記錄字串。
- **主機測試**：無
- **驗證紀錄**：實測觀察到 `backboardd` 約 58 Hz 觸發 ~600 MB/s `memcpy` 拖慢單核遷移與 `SpringBoard` check-in，本補丁改以 10 Hz 節流呈現。
- **參考來源**：來源未記
- **結論／未完成**：大幅減輕單核 CPU 顯示複製負擔（註：後續在多核與 GPU scanout 上線後仍會再調整）。
- **出處**：`ksrc:xnu/patches/0056-sweet-fb-throttled-present.patch`；上游提交 `1bb4976`

### S-057 停用外部身分解析器（config_ext_resolver）使 iOS ACL 生效

- **日期**：2026-10-17 22:37
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：`installcoordinationd` 建立資料目錄時因 `EACCES` 中止：system-group 容器的 `Library` 目錄僅設定 ACL（群組 299 `systemusers`）。
- **根因**：`MASTER.arm64.sweet` 沿用 macOS 設定開啟了 `config_ext_resolver`，導致核心等待 iOS 上並不存在的 `memberd`/`opendirectoryd` 解析器，逾時／失敗後略過 ACE 而退回 `0000` 權限模式。關閉該選項後 XNU 會自行將相容性 GUID 對應至 uid/gid。
- **做法**：修改 `config/MASTER.arm64.sweet`（新增 1 行、刪除 1 行），關閉 `config_ext_resolver`。
- **復現**：見 README 共用步驟 1–3；編譯組態生效，無開機參數與記錄字串。
- **主機測試**：無
- **驗證紀錄**：上游提交記明由 Codex 分析報告（`codex/installcoordinationd-report.md`）定位原因，並提及配合 image v12 測試。
- **參考來源**：`codex/installcoordinationd-report.md`
- **結論／未完成**：修正 iOS system-group 容器僅有 ACL 時的權限判定問題。
- **出處**：`ksrc:xnu/patches/0057-sweet-no-ext-resolver.patch`；上游提交 `6690ef9`

### S-058 新增 IOHIDFamily-1915.140.3 HID 事件服務（按鍵與觸控）

- **日期**：2026-10-18 10:49
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：
  - `sweethidtest=<N>`：開機參數，每隔 `N` 秒模擬按下一次 Home 鍵，供無實體輸入驅動時測試 HID 路徑。
  - `hidq_size`、`hidq_no_full_msg`、`hidq_force_usage_pair`：沿自 `IOHIDFamily-1915.140.3` 之 HID 事件佇列相關開機參數（卷宗未記預設與細部值）。
- **問題／現象**：`backboardd` 的 HID 事件系統需要開啟具備 `HIDServiceSupport` 的 `IOService` 並透過 `IOHIDEventServiceUserClient`（`'esuc'`）共享佇列讀取 `IOHIDEvent`，缺此介面則無法向系統注入按鍵與觸控事件。
- **根因**：Apple 的 `IOHIDFamily` 為獨立驅動家族，XNU 核心樹預設未將 `IOHIDEventServiceUserClient` 與其事件佇列編進 SWEET 核心。
- **做法**：修改 `iokit/conf/files.arm64`、`iokit/KernelConfigTables.cpp` 並新增 31 個標頭與實作檔於 `iokit/Kernel/arm/hid/`（含 `IOHIDEvent.cpp`、`IOHIDEventServiceQueue.cpp`、`IOHIDEventServiceUserClient.cpp`、`SweetHID.cpp` 等，共 33 檔、21,201 行）。在極簡 `IOHIDEventService` 上原封不動建置來自 `IOHIDFamily-1915.140.3`（iOS 16.7 userland 對應版本）的 user client／queue／event，並實作 `SweetHIDButtons`（consumer control：power、volume、home）與 `SweetHIDTouch`（digitizer collections），對外提供 `sweet_hid_key()` 與 `sweet_hid_touch()` 供輸入驅動呼叫。
- **復現**：見 README 共用步驟 1–3；選配 `sweethidtest=<秒數>`；console 輸出 `SWEET HID: buttons registered (test every %u s)\n`、`SWEET HID: touch registered\n`、`SWEET HID: %s open by %s -> %d\n`、`SWEET HID: key 0x%x %s (%u clients)\n`、`SWEET HID: touch %d %s at %d,%d\n`。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果（實體按鍵與觸控驅動於 `0060` 接上此介面）。
- **參考來源**：Apple 開源 `IOHIDFamily-1915.140.3`（APSL 2.0 授權標頭）；`IOGraphicsTypes.h` 參照 `IOKit/graphics/IOGraphicsTypes.h` 佈局
- **結論／未完成**：建立核心內建 iOS 16.7 相容的 HID 事件輸送管線。
- **出處**：`ksrc:xnu/patches/0058-sweet-hid-event-services.patch`；上游提交 `062ec19`

### S-059 從 ADT 讀取 hw.machine／hw.model 以呈現 iPhone X 裝置身分

- **日期**：2026-10-18 10:52
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：衍生自 macOS 的核心對 iOS 行程回報 `hw.machine = "iPad8,6"` 與 `hw.model = "Redmi Note 10 Pro"`，導致 `MobileGestalt` 選錯裝置類別。
- **根因**：`bsd/kern/kern_mib.c` 與 `SweetPlatformExpert.cpp` 未將 `hw.machine`/`hw.model` 統一導向裝置樹中 iPhone X（`D22AP` / `iPhone10,3`）身分。
- **做法**：修改 `bsd/kern/kern_mib.c` 與 `iokit/Kernel/arm/SweetPlatformExpert.cpp`（新增 8 行、刪除 22 行），讓 `hw.machine` 與 `hw.model` 改從裝置樹根節點第一個 `"compatible"` 與 `"model"` 讀取（搭配 `mkadt` 填入 `D22AP` / `iPhone10,3` 與 `/product`）。
- **復現**：見 README 共用步驟 1–3；需 ADT 根節點包含 `D22AP`/`iPhone10,3` 身分；無額外開機參數與記錄字串。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：來源未記
- **結論／未完成**：讓 `MobileGestalt` 與 iOS userland 正確識別為 iPhone X（`D22AP`）裝置類別。
- **出處**：`ksrc:xnu/patches/0059-sweet-iphone-identity.patch`；上游提交 `9f2f881`

### S-060 新增輪詢式電源／音量鍵（SPMI）與觸控（GENI I2C）驅動

- **日期**：2026-10-18 11:10
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：
  - `sweetkeys=0`：開機參數，停用輪詢按鍵驅動（預設啟用）。
  - `sweettouch=0`：開機參數，停用觸控驅動（預設啟用）。
  - `sweettouchdelay=<秒>`：開機參數，延遲觸控驅動啟動之秒數；預設 45 秒（避免匯流排異常卡死 SoC 時來不及寫出持續性 log 與啟動 GUI）。
  - `sweettouchirq`：開機參數，啟用選配 IRQ 提示（註解提及仍每 100ms 輪詢，按下時持續讀取）。
- **問題／現象**：系統已有 `SweetHID` 介面（`0058`），但缺乏實體電源鍵、音量鍵與觸控螢幕的硬體輸入驅動。
- **根因**：Qualcomm SM7150 平台透過 SPMI 仲裁器連接 PM6150/PM6150L 讀取按鍵，並透過 QUP wrapper1 SE7 GENI I2C 連接觸控 IC，XNU 沒有對應驅動。
- **做法**：修改 `bsd/conf/files`、`bsd/dev/sweet_ufs.c` 並新增 `bsd/dev/sweet_keys.c`、`bsd/dev/sweet_touch.c`（共 4 檔、757 行）。
  - `sweet_keys.c`：每 20 ms 透過 SPMI arbiter observer channel 唯讀讀取 PM6150 PON（電源、音量下）與 PM6150L SID 4 GPIO2（音量上），絕不寫入 PMIC 組態，並呼叫 `sweet_hid_key()`。
  - `sweet_touch.c`：為 QUP wrapper1/SE7 投票啟用時脈（19.2 MHz XO，不改 PLL/電壓），在 GENI I2C FIFO 模式下探測 Goodix GT9896（`0x5d`）與 Focaltech（`0x38`），回報 1080x2400 座標給 `sweet_hid_touch()`（依賴 ABL 預載 I2C 韌體與已供電的 L11C 電源軌）。
- **復現**：見 README 共用步驟 1–3；預設啟用（`sweetkeys=0`、`sweettouch=0` 可關）；console 輸出 `SWEET KEYS: arb=%#x ...`、`SWEET TOUCH: starting in %u s\n`、`SWEET TOUCH: selected %s, 1080x2400, poll=10ms irq-hint=%d\n`。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：Codex 硬體報告（`hw-report.md`）；Linux 封包格式參考（註解載明 `Four 8-bit bytes per 32-bit word, LSB byte first (Linux packing)`）
- **結論／未完成**：建立 sweet 機型實體按鍵與 I2C 觸控輸入（註：fuxi 機型後續改用 `0091` FTS SPI 與 `0092` fuxi 按鍵）。
- **出處**：`ksrc:xnu/patches/0060-sweet-keys-touch-drivers.patch`；上游提交 `0678bf3`

### S-061 將顯示畫面與觸控座標呈現為 iPhone X（1125x2436）

- **日期**：2026-10-18 11:52
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：`sweetfb_native=1`：開機參數，恢復回報實體面板解析度（1080x2400）；預設關閉（呈現為 1125x2436）。
- **問題／現象**：UIKit 對 `360x800` points 的螢幕沒有狀態欄視覺提供者（`"No status bar visual provider for screen"`），會畫出紅色的 `rdar://45025538` 錯誤條。
- **根因**：iOS userland 僅針對真實 iPhone 螢幕尺寸內建狀態欄與版面配置資料，1080x2400 @3x（360x800 pt）不在支援清單內。
- **做法**：修改 `iokit/Kernel/arm/SweetFramebuffer.cpp` 與 `iokit/Kernel/arm/hid/SweetHID.cpp`（新增 50 行、刪除 7 行）。`AppleCLCD` 對外回報 `1125x2436`（`375x812` points @3x、458 ppi），並將中央 `1080x2400` 區域複製到實體面板；觸控座標亦映射至相同呈現空間（ADT `canvas-width`/`height` 同步跟隨）。
- **復現**：見 README 共用步驟 1–3；預設生效（`sweetfb_native=1` 回復原尺寸）；console 輸出 `SWEET FB: AppleCLCD %ux%u stride %u at 0x%llx, default surface %u, presented as %ux%u\n`。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：來源未記
- **結論／未完成**：消除 UIKit 紅色狀態欄錯誤條；後續在 fuxi（`0106` 等）會再依小米 13 解析度與 `fuxi,refresh-mhz` 等演進。
- **出處**：`ksrc:xnu/patches/0061-sweet-iphone-x-display-size.patch`；上游提交 `663ccee`

### S-062 新增觸控電源軌 RPMh 投票（L11C）與 IOPMPowerSource 電池服務

- **日期**：2026-10-18 12:05
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：`sweetbattery=1`：開機參數，每 30 秒透過 `sweet_spmi.c` 唯讀輪詢 PM6150 以電壓估算未校正電量（SOC）；預設關閉（回傳靜態 100% / 外接電源）。
- **問題／現象**：觸控 IC 需要 L11C 電源軌與特定重置時序才能啟動；且 iOS `powerd` 需要匹配 `IOPMPowerSource` 電池服務。
- **根因**：缺乏 Qualcomm RPMh / cmd-db 投票實作、SPMI 唯讀存取介面與 XNU 內建電池 `IOPMPowerSource` 提供者。
- **做法**：修改 `bsd/conf/files`、`bsd/dev/sweet_touch.c`、`iokit/conf/files.arm64`、`iokit/KernelConfigTables.cpp`，並新增 `bsd/dev/sweet_rpmh.c`、`sweet_spmi.c`、`sweet_spmi.h`、`iokit/Kernel/arm/SweetBattery.cpp`（共 8 檔、849 行）。
  - `sweet_rpmh.c`：透過 cmd-db 解析資源並經空閒 apps-DRV TCS 送出 ACTIVE-set VRM 指令（僅限 `ldoc11`、2950–3400 mV、mode 7）；`sweet_touch.c` 先壓住重置腳、投票 L11C 為 3.2 V、拉高 GPIO90 並依 GT9896 時序脈衝重置後再探測。
  - `SweetBattery`：註冊 `IOPMPowerSource`（`powerd` 配對目標），預設靜態 100% / 外接電源不充電，或於 `sweetbattery=1` 時經 `sweet_spmi.c` 唯讀讀取 PM6150（固定 25°C、DT 標稱 5020 mAh，不觸發低電量關機）。
- **復現**：見 README 共用步驟 1–3；選配 `sweetbattery=1`；console 輸出 `SWEET RPMH: L11C ACTIVE vote complete ...`、`SWEET BATTERY: SPMI arb=%#x APIDs=%u, observer reads only\n`、`SWEET BATTERY: %s, %u%% %umV external=%d charging=%d\n`。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：Codex 輔助撰寫（`report.md`）與 Linux 下游 DT / cmd-db / QG 參考
- **結論／未完成**：建立 sweet 觸控 L11C 投票與唯讀電池服務（註：fuxi 電量後續改由 ADSP 相關補丁處理）。
- **出處**：`ksrc:xnu/patches/0062-sweet-touch-power-battery.patch`；上游提交 `c32accc`

### S-063 修正 GENI I2C FIFO packing 與 GT9896（Yellowstone）協定

- **日期**：2026-10-18 12:52
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：`0060` 的 TX/RX packing 組態將 Goodix 暫存器位址位元組對調（送出 `0x0244` 而非 `0x4022`），導致 Goodix IC 全數回傳零值。
- **根因**：GENI SE packing 設定與 GT9896（Xiaomi `sweet-r-oss` 為 Yellowstone 16-bit 大端暫存器協定，而非 Berlin 4-byte 指標）不相符。
- **做法**：修改 `bsd/dev/sweet_touch.c`（新增 113 行、刪除 32 行），修正 `geni_se_config_packing(8, 4, true, true, true)` 與核對讀回、採用 GT9896 16-bit 大端暫存器協定，並加入 32-byte 資訊暫存器原始傾印與每筆傳輸 `M_IRQ_STATUS` 記錄。
- **復現**：見 README 共用步驟 1–3；無須開機參數；console 輸出 `SWEET TOUCH: packing tx=%#x/%#x rx=%#x/%#x gran=%#x\n`、`SWEET TOUCH: probe raw %#x: %s\n`。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：Codex 輔助（`touch2/report.md`）與 Xiaomi `sweet-r-oss` Goodix Yellowstone 驅動對照
- **結論／未完成**：修復 Goodix GT9896 I2C 暫存器位址位元序問題。
- **出處**：`ksrc:xnu/patches/0063-sweet-goodix-geni-packing.patch`；上游提交 `7875815`

### S-064 在 SWEET Sandbox 政策中授予 PRIV_WORK_INTERVAL 權限

- **日期**：2026-10-18 13:18
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：`SpringBoard`（uid `mobile`）因 `'Failed to create work interval'` 中止：UIKit 的 CoreAnimation commit 路徑呼叫 `work_interval_create(CA_CLIENT)`，需要 `PRIV_WORK_INTERVAL` 特權。
- **根因**：非 root 行程的特權須由 MAC 政策（`priv_check_cred` → `mac_priv_grant`）授予（iOS 上由 Sandbox/AMFI 授予），`0050` 的 Sandbox 政策尚未實作此 hook。
- **做法**：修改 `bsd/kern/sweet_amfi.c`（新增 39 行），在 SWEET Sandbox 政策實作 `mac_priv_grant` 授予 `PRIV_WORK_INTERVAL`，並對每個行程記錄其他特權的首次拒絕。
- **復現**：見 README 共用步驟 1–3；無須開機參數；若有其他特權被拒，console 輸出 `SWEET Sandbox: %s privilege %d denied\n`。
- **主機測試**：無
- **驗證紀錄**：上游提交記明 `SpringBoard` 先前因 `'Failed to create work interval'` 中止，未另記補丁後實機結果。
- **參考來源**：來源未記
- **結論／未完成**：解除 CoreAnimation `work_interval_create` 權限阻擋。
- **出處**：`ksrc:xnu/patches/0064-sweet-priv-work-interval.patch`；上游提交 `20f9a3b`

### S-065 以 PSCI CPU_ON 啟動 A55 次要核心（cpus=2..6）

- **日期**：2026-10-18 13:19
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：`cpus=<N>`：開機參數，啟用最多 `N` 顆 A55 核心（`2..6`）；預設為 `1`（單核）。
- **問題／現象**：系統先前僅在單一開機核心（CPU 0）上執行，整體效能受限。
- **根因**：`SweetCPU.cpp` 僅支援單核開機，且 `start.s`／`machine_routines.c` 尚未將 SM7150 以 Aff1 編碼的 MPIDR 對應至邏輯 CPU 與處理 EL1/EL2 PSCI 喚醒入口。
- **做法**：修改 `iokit/Kernel/arm/SweetCPU.cpp`、`osfmk/arm64/cpu.c`、`osfmk/arm64/machine_routines.c`、`osfmk/arm64/start.s`、`pexpert/pexpert/arm64/SWEET.h`（新增 239 行、刪除 37 行）。透過 SMCCC 1.0 `PSCI CPU_ON` 於 XNU 重置向量啟動 A55 次要核心（處理 EL2→EL1 降級、無快取下透過實體位址載入重置向量與清除本地 TLB/I-cache），補上含 Aff1 的目標親和性 SGI 傳送與逾時診斷；A76 核心尚未納入。
- **復現**：見 README 共用步驟 1–3；開機參數帶 `cpus=6`（或 `2..6`）；成功時 console 輸出 `SWEET CPU: requested=%u selected=%u (A55 limit=%u)\n`、`SWEET CPU: cpu=%u mpidr=0x%x came alive; SGI1 OK\n`、`SWEET CPU: %u/%u CPUs initialized, SGI1 verified\n`。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：Codex 輔助（`codex/smp/report.md`）
- **結論／未完成**：達成 6 核 A55 同質 SMP；全 8 核（含兩顆 A76）於 `0078` 補齊。
- **出處**：`ksrc:xnu/patches/0065-sweet-smp-psci.patch`；上游提交 `d838e69`

### S-066 在崩潰報告印出錯誤位址所屬的 VM 映射區段

- **日期**：2026-10-18 13:48
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：發生 `SIGSEGV`/`SIGBUS`（例如 `backboardd` 在 `CGDataProviderCreateWithData` 發生 `BufferIsNotReadable` 截圖崩潰）時，僅有 `far` 位址無法判斷是否為越界讀取或落在哪兩個映射區間之間。
- **根因**：`0043` 的崩潰報告未查詢並印出行程 VM map 中對應 `far` 的區段。
- **做法**：修改 `osfmk/kern/exception.c`（新增 19 行），當發生 `SIGSEGV`/`SIGBUS` 時，印出包含 `far` 的 VM map entry（或夾在其前後的兩個 entry）之位址範圍、保護權限與 VM tag。
- **復現**：見 README 共用步驟 1–3；無須開機參數；觸發 `SIGSEGV`/`SIGBUS` 時 console 輸出 `CRASH %s map%s 0x%llx-0x%llx prot %d/%d tag %d\n`。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果（後續用於定位 `0068` 的 `IOSurface` 重複釋放越界讀取）。
- **參考來源**：來源未記
- **結論／未完成**：強化記憶體錯誤崩潰的定位能力。
- **出處**：`ksrc:xnu/patches/0066-sweet-crash-fault-region.patch`；上游提交 `c66741e`

### S-067 將 sweetfb_noconsole 改為靜音螢幕主控台旗標而非切換到序列埠

- **日期**：2026-10-18 14:05
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無（修改 `0046` 既有的 `sweetfb_noconsole=1` 行為）
- **問題／現象**：啟用 `sweetfb_noconsole=1` 時，`AppleCLCD::vsyncFired` 呼叫 `PE_initialize_console(kPEDisableScreen)` 觸發 `switch_to_serial_console` 核心 data abort panic。
- **根因**：`PE_initialize_console(kPEDisableScreen)` 會寫入位於 read-only-late 保護區的 `cons_ops_index`，導致核心寫入唯讀記憶體而 panic。
- **做法**：修改 `osfmk/console/serial_console.c` 與 `iokit/Kernel/arm/SweetFramebuffer.cpp`（新增 16 行、刪除 5 行），新增 SWEET 專用的主控台靜音旗標：開啟 `sweetfb_noconsole=1` 後僅停止將文字繪製到 GUI 上，文字仍持續寫入持續性紀錄，且發生 panic 時自動恢復繪製。
- **復現**：見 README 共用步驟 1–3；開機參數帶 `sweetfb_noconsole=1` 不再於首次 swap 時 panic；無新增記錄字串。
- **主機測試**：無
- **驗證紀錄**：實機先前於 `AppleCLCD::vsyncFired` → `switch_to_serial_console` 觸發 data abort panic，本補丁改為靜音旗標以避開寫入 `cons_ops_index`。
- **參考來源**：來源未記
- **結論／未完成**：修復 `sweetfb_noconsole=1` 導致的必現 panic。
- **出處**：`ksrc:xnu/patches/0067-sweet-console-mute.patch`；上游提交 `07b1156`

### S-068 重複查詢已映射的 IOSurface 時回傳基底位址 -1

- **日期**：2026-10-18 14:32
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：在設定（Setup）畫面切換語言時，`backboardd` 讀取顯示快照發生崩潰（`CGDataProviderCreateWithData` `BufferIsNotReadable`，錯誤位址位於 surface 映射末端）。
- **根因**：`IOSurface.framework` 將 lock result 基底位址 `-1` 視為「本行程已持有此 surface」而重用快取的 `IOSurfaceClient`；`SweetSurface.cpp` 先前每次查詢都重新計數並回傳映射位址，導致 framework 為同一個 id 建立第二個 client，當釋放其中一個（traps 4+5）時就把第一個仍在使用的記憶體 unmap 了。
- **做法**：修改 `iokit/Kernel/arm/SweetSurface.cpp`（新增 17 行、刪除 5 行），當客戶端已映射該 surface 時不再重複計數或映射（`*existing = true`），並於 lock result 回傳基底位址 `-1` 讓 `IOSurface.framework` 重用快取 client。
- **復現**：見 README 共用步驟 1–3；無須開機參數，無記錄字串。
- **主機測試**：無
- **驗證紀錄**：實機於 Setup 切換語言時重現 `backboardd` `CGDataProviderCreateWithData` 崩潰並依 `0066` 定位；未另記補丁後結果。
- **參考來源**：來源未記
- **結論／未完成**：修復 `IOSurfaceClient` 重複映射與提早 unmap 導致的崩潰。
- **出處**：`ksrc:xnu/patches/0068-sweet-surface-lookup-cached.patch`；上游提交 `03d1a5f`

### S-069 將 sweetlog 刷寫週期改為 10 秒並新增選配 UFS 快取與等待調校

- **日期**：2026-10-18 14:49
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：
  - `ufslogsecs=<秒>`：開機參數，設定將持續性 console 環形緩衝區刷寫至 `userdata` 尾端的間隔秒數；預設由 1 秒改為 `10` 秒。
  - `ufscache=1`：開機參數，將快取維護限制在 DMA 方向（預設關閉）。
  - `ufswait=1`：開機參數，等待 UFS 完成時先短暫自旋再睡眠（預設關閉）。
- **問題／現象**：每秒刷寫一次 `sweetlog` 與每次 DMA 做雙向快取維護／純輪詢等待造成不必要的 UFS 競爭與 CPU 開銷。
- **根因**：`0047` 與 `0028` 採用較保守的每秒刷寫與全套快取／等待策略。
- **做法**：修改 `bsd/dev/sweet_ufs.c`（新增 70 行、刪除 10 行），將 `ufslogsecs` 預設拉長至 10 秒以批次寫入，並加入預設關閉的 `ufscache=1` 與 `ufswait=1` 調校開關。
- **復現**：見 README 共用步驟 1–3；預設即採用 10 秒刷寫（可選帶 `ufscache=1`、`ufswait=1`、`ufslogsecs=<N>`）；無新增記錄字串。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機數據。
- **參考來源**：Codex 輔助 UFS 效能計畫（`ufs-performance.md`）
- **結論／未完成**：降低 `sweetlog` 對 UFS 的頻繁寫入干擾並保留選配調校開關。
- **出處**：`ksrc:xnu/patches/0069-sweet-ufs-faster.patch`；上游提交 `06a2fd9`

### S-070 在 SWEET 啟用 pid_hibernate 系統呼叫（修復 SpringBoard 退回桌面崩潰）

- **日期**：2026-10-18 15:54
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：當 App 離開前景時，`SpringBoard` 的 `SBToAppsWorkspaceTransaction` 呼叫 `pid_hibernate`（syscall 435）立即收到 `SIGSYS` 而死亡，導致使用者無法從「設定」等 App 回到主畫面。
- **根因**：`pid_hibernate` 在 `syscalls.master` 僅為 `CONFIG_EMBEDDED` 核心啟用，衍生自 macOS 的核心對該系統呼叫回應 `nosys`（`SIGSYS`）。
- **做法**：修改 `bsd/kern/syscalls.master` 與 `bsd/vm/vm_unix.c`（新增 8 行、刪除 3 行），在 SWEET 上啟用真實的 `pid_hibernate` 系統呼叫（在無 `CONFIG_FREEZE` 下與 iOS 相同直接回傳 `0`）。
- **復現**：見 README 共用步驟 1–3；無須開機參數，無記錄字串；操作上可從 App 返回主畫面不再觸發 `SpringBoard` `SIGSYS`。
- **主機測試**：無
- **驗證紀錄**：實機先前離開「設定」回主畫面時 `SpringBoard` 因 `SIGSYS` 崩潰，本補丁修正 `syscalls.master` 唯一嵌入式限定項目。
- **參考來源**：來源未記
- **結論／未完成**：解決離開 App 時 `SpringBoard` 觸發 `SIGSYS` 崩潰的問題。
- **出處**：`ksrc:xnu/patches/0070-sweet-pid-hibernate.patch`；上游提交 `278cd7a`

### S-071 為螢幕邊緣起始的觸控標記 FromEdgeTip 位元

- **日期**：2026-10-18 16:20
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：從螢幕底部邊緣往上滑動回主畫面或切換 App 的手勢被當成一般 App 內拖曳，使用者無法離開 App。
- **根因**：UIKit 透過 digitizer 事件遮罩（`_UIEventHIDTouchEdgeTypeForHIDEvent`：`FromEdgeTip` / `FromCorner` / `SwipePending`）判定 `UITouch` 邊緣類型，而 `SweetHID` 從未設定該旗標。
- **做法**：修改 `iokit/Kernel/arm/hid/SweetHID.cpp`（新增 16 行、刪除 2 行），對起始位置距離面板邊緣 48 px（約 3 mm）以內的觸控，在整個接觸生命週期直到抬手為止持續帶上 `kIOHIDDigitizerEventFromEdgeTip`。
- **復現**：見 README 共用步驟 1–3；無須開機參數；觸控紀錄 `SWEET HID: touch %d %s at %d,%d%s\n` 會附加邊緣標記。
- **主機測試**：無
- **驗證紀錄**：上游提交未記補丁後實機結果（先前記明無此位元時無法由底部手勢離開 App）。
- **參考來源**：來源未記
- **結論／未完成**：啟用 iPhone X 風格底部邊緣回主畫面與多工切換手勢。
- **出處**：`ksrc:xnu/patches/0071-sweet-touch-edge.patch`；上游提交 `7b55c89`

### S-072 新增唯讀 USB 狀態探測（DWC3 / QUSB2 / GCC）

- **日期**：2026-10-18 16:48
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：`sweetusb=0`：開機參數，停用 USB 探測；預設啟用唯讀探測（`1`）。
- **問題／現象**：需要了解手機經 `fastboot boot` 進入 XNU 後，ABL 對 USB（Synopsys DWC3 + QUSB2 PHY + GCC USB30_PRIM GDSC）留下的供電、時脈與暫存器狀態，才能安全實作 USB 裝置模式。
- **根因**：直接存取未供電或未開時脈的 DWC3/PHY 暫存器區段會導致系統掛死，須先依 GCC 狀態唯讀確認。
- **做法**：修改 `bsd/conf/files`、`bsd/dev/sweet_ufs.c` 並新增 `bsd/dev/sweet_usb.c`（共 3 檔、111 行）。讀取 GCC `USB30_PRIM` GDSC 與 USB 時脈分支暫存器，僅在電源域與主／組態時脈開啟時讀取 DWC3 全域／裝置暫存器（`GSNPSID`、`GCTL`、`DCFG`/`DCTL`/`DSTS`、event buffer），並於 `ahb2phy` 時脈開啟時讀取 QUSB2 PHY 狀態；全程不寫入任何暫存器。
- **復現**：見 README 共用步驟 1–3；預設啟用唯讀探測（`sweetusb=0` 跳過）；console 輸出 `SWEET USB: GDSC usb30_prim 0x%08x (%s)\n`、`SWEET USB: DWC3 GSNPSID 0x%08x ...`、`SWEET USB: QUSB2 PLL_STATUS 0x%08x ...`。
- **主機測試**：無
- **驗證紀錄**：實機探測確認 ABL 留下 DWC3 已供電、有時脈、處於裝置模式且暫停（halted），QUSB2 PHY 已組態完成（見 `0073` 提交說明）。
- **參考來源**：Linux `gcc-sdmmagpie.c`、`phy-msm-qusb-v2.c`
- **結論／未完成**：完成 USB 硬體狀態安全盤點，為 `0073` 啟動 DWC3 endpoint 0 鋪路。
- **出處**：`ksrc:xnu/patches/0072-sweet-usb-probe.patch`；上游提交 `b4843e5`

### S-073 新增 USB 裝置模式與 Endpoint 0 列舉（sweetusb=2）

- **日期**：2026-10-18 17:22
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無（擴充 `0072` 既有 `sweetusb` 開機參數：`0` 關閉、`1` 預設僅探測、`2` 啟動 USB 裝置模式）
- **問題／現象**：需要讓主機端（PC）能透過 USB 列舉手機裝置，作為後續 USB 網路與即時 console log 的載體。
- **根因**：`0072` 僅做唯讀探測，DWC3 處於 halted 狀態且沒有事件緩衝區與 EP0 控制端點處理程式。
- **做法**：修改 `bsd/dev/sweet_usb.c`（新增 395 行）。當 `sweetusb=2` 時，對 DWC3 裝置控制器執行軟體重置（保留 ABL 留下的 PHY 與全域組態），配置非快取一致 DMA 記憶體（含清理／無效化維護）作為事件緩衝區並啟動預設控制端點（EP0 OUT/IN）；由核心執行緒輪詢事件並依 Synopsys DWC3 / Linux `dwc3` 狀態機回應標準請求（`SET_ADDRESS`、`SET_CONFIGURATION`、descriptor），以測試 VID/PID `1209:0001`（`'Sweet iOS'`）呈現（尚未含資料端點）。
- **復現**：見 README 共用步驟 1–3；開機參數帶 `sweetusb=2`；console 輸出 `SWEET USB: soft reset %s after %d ms ...`、`SWEET USB: running (DSTS 0x%08x after %d ms), DMA at 0x%llx\n`、`SWEET USB: setup %02x %02x %04x len %u\n`。
- **主機測試**：無
- **驗證紀錄**：實機運行 `0073` 時 DWC3 已脫離 halted 狀態，但未收到任何 USB 事件且主機端無反應（隨即於 `0074` 查出缺少 VBUS session override）。
- **參考來源**：Synopsys DWC3 databook 與 Linux `drivers/usb/dwc3`（`gadget.c`、`ep0.c`）；`pid.codes` 測試 ID `0x1209/0x0001`
- **結論／未完成**：完成 DWC3 EP0 控制狀態機；因缺 VBUS session 訊號尚無法被主機偵測，由 `0074` 修復。
- **出處**：`ksrc:xnu/patches/0073-sweet-usb-device-ep0.patch`；上游提交 `af3eab3`

### S-074 在 Qualcomm USB wrapper 強制啟用 VBUS session valid

- **日期**：2026-10-18 17:37
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：`0073` 雖啟動了 DWC3（`DSTS` 不再 halted），但從未收到任何 USB 事件，主機端也看不到裝置。
- **根因**：DWC3 在未見有效 VBUS session 前不會對 D+ 拉起上拉電阻（pull-up）；Linux `dwc3-qcom` 會在充電器回報線材連接時透過 Qualcomm wrapper（`QSCRATCH`，core base + `0xf8800`）以軟體強制設定 VBUS valid。
- **做法**：修改 `bsd/dev/sweet_usb.c`（新增 25 行），在 `QSCRATCH_HS_PHY_CTRL` 設定 `UTMI_OTG_VBUS_VALID | SW_SESSVLD_SEL`，並在 `QSCRATCH_SS_PHY_CTRL` 設定 `LANE0_PWR_PRESENT`，同時記錄設定前後暫存器值。
- **復現**：見 README 共用步驟 1–3；搭配 `sweetusb=2`；console 輸出 `SWEET USB: QSCRATCH HS_PHY_CTRL 0x%08x ...` 與 `SWEET USB: VBUS override on: HS_PHY_CTRL 0x%08x SS_PHY_CTRL 0x%08x\n`。
- **主機測試**：無
- **驗證紀錄**：上游提交記明解決 `0073` 無事件與主機不可見的問題，未另單獨記列舉結果（隨即於 `0075` 擴充為 CDC-ECM）。
- **參考來源**：Linux `dwc3-qcom`（`dwc3_qcom_vbus_override_enable`）
- **結論／未完成**：DWC3 可成功拉起 D+ 讓主機進行 USB 列舉。
- **出處**：`ksrc:xnu/patches/0074-sweet-usb-vbus-override.patch`；上游提交 `a23838d`

### S-075 新增 USB 乙太網路（CDC-ECM）與 ue0 網路介面（192.168.7.2/24）

- **日期**：2026-10-18 17:52
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：`0073` 僅有 EP0 控制端點，缺乏可用的主機-手機資料通道與網路介面。
- **根因**：缺乏 CDC-ECM USB descriptor、bulk/interrupt 資料端點設定以及對應的核心 `ifnet` 網路介面。
- **做法**：修改 `bsd/dev/sweet_usb.c`（新增 318 行、刪除 13 行）。在 `sweetusb=2` 時將裝置組態為 CDC-ECM 乙太網路（bulk `0x81`/`0x01` 512 bytes、interrupt `0x82` 發送 `NETWORK_CONNECTION`，主機側 MAC `02:00:5e:77:ee:01`、手機側 `..:02`；512 倍數長度封包補 1 pad byte 取代 ZLP）；並掛載核心乙太網路介面 `ue0`（傳統 ifnet 輸出、32 筆 TX 佇列、收包比照 `if_fake` 原地剝除標頭），由核心直接配置 IPv4 `192.168.7.2/24`（尚未接 `IONetworkInterface`，iOS 上層尚不可見）。
- **復現**：見 README 共用步驟 1–3；開機參數帶 `sweetusb=2`；console 輸出 `SWEET USB: ECM endpoints configured\n` 與 `SWEET USB: ue0 192.168.7.2/24 %s (err %d)\n`。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果（隨即於 `0076` 遇到首個收包 `mbuf` panic）。
- **參考來源**：Linux `cdc_ether` 相容 CDC-ECM 規格；XNU `if_fake` 輸入標頭處理方式
- **結論／未完成**：建立核心層 USB 乙太網路通道 `ue0`；收包路徑於 `0076` 修復，連結狀態於 `0081` 補齊。
- **出處**：`ksrc:xnu/patches/0075-sweet-usb-ecm.patch`；上游提交 `037ea5d`

### S-076 修正 USB 乙太網路收包使用單一 2 KiB cluster mbuf

- **日期**：2026-10-18 18:12
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：收到來自主機的第一個乙太網路封包時核心發生 panic：`'Failed mbuf validity check: len -14'`。
- **根因**：`mbuf_allocpacket` 回傳的第一個 `mbuf` 長度為 `0`，從該空 `mbuf` 剝除 14-byte 乙太網路標頭導致長度變成 `-14`。
- **做法**：修改 `bsd/dev/sweet_usb.c`（新增 11 行、刪除 2 行），改為將每個收到的封包放進單一配置 2 KiB cluster 的 `mbuf` 並明確設定各欄位長度。
- **復現**：見 README 共用步驟 1–3；搭配 `sweetusb=2` 收包不再觸發 `-14` panic；無新增記錄字串。
- **主機測試**：無
- **驗證紀錄**：實機先前收到首個主機封包即 panic，修正後於 `0077` 成功透過 `ue0` 串流即時 log。
- **參考來源**：來源未記
- **結論／未完成**：解決 `ue0` RX 路徑的 `mbuf` 長度計算 panic。
- **出處**：`ksrc:xnu/patches/0076-sweet-usb-rx-mbuf.patch`；上游提交 `798273d`

### S-077 透過 USB 乙太網路即時串流 console log（UDP 至 192.168.7.1:5140）

- **日期**：2026-10-18 18:28
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：`sweetusblog=0`：開機參數，停用 USB 即時 console log 串流；預設啟用（搭配 `sweetusb=2`）。
- **問題／現象**：每次查看 console log 都必須重啟進 recovery 才能從 UFS 拉取，迭代除錯耗時。
- **根因**：`0047` 的持續性 log 僅寫入本地環形緩衝區與 UFS，雖已有 `ue0` 網路但尚無即時外送執行緒。
- **做法**：修改 `bsd/dev/sweet_ufs.c` 與 `bsd/dev/sweet_usb.c`（新增 83 行）。在 `sweet_ufs.c` 新增 `sweet_log_copy`/`sweet_log_position` 讀取器；由核心執行緒每 100 ms 將 `sweetlog` 新增位元組以 UDP datagram 送往 `192.168.7.1:5140`（PC 端以 `nc -ukl 5140` 接收，啟動時先補送最後 64 KiB 含開機過程，串流路徑本身禁止呼叫 `printf` 以免自我回饋）。
- **復現**：見 README 共用步驟 1–3；開機參數帶 `sweetusb=2`（`sweetusblog=0` 可關），PC 端設定同網段並執行 `nc -ukl 5140`；無新增 console 記錄字串。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：來源未記
- **結論／未完成**：實現免重啟進 recovery 的即時 USB console log 串流。
- **出處**：`ksrc:xnu/patches/0077-sweet-usb-live-log.patch`；上游提交 `7f9e64e`

### S-078 啟用全部八顆核心（六顆 A55 ＋ 兩顆 A76，cpus=8）

- **日期**：2026-10-18 18:40
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無（將 `0065` 既有 `cpus` 參數上限從 6 提高到 8）
- **問題／現象**：`0065` 僅開啟六顆 A55 小核，兩顆效能較高的 Cortex-A76 大核（2.3 GHz）尚未啟用。
- **根因**：`SWEET_SMP_MAX_CPUS` 上限設為 6，且 `SweetCPU` 與 `machine_routines.c` 僅允許 MPIDR 至 `0x500`。
- **做法**：修改 `pexpert/pexpert/arm64/SWEET.h`、`iokit/Kernel/arm/SweetCPU.cpp`、`osfmk/arm64/machine_routines.c`（新增 12 行、刪除 6 行）。將 `SWEET_SMP_MAX_CPUS` 提高至 8，`SweetCPU` 接受 MPIDR 至 `0x700`（`0x600`/`0x700` 為 A76，搭配 ADT 列出 `cpu6`/`cpu7`）；仍作為單一同質 SMP 叢集運作（同 ISA 等級、64-byte 快取線、`PSCI CPU_ON`，排程器尚未區分大小核）。
- **復現**：見 README 共用步驟 1–3；開機參數帶 `cpus=8`；console 輸出 `SWEET CPU: requested=%u selected=%u (limit=%u)\n`。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：來源未記
- **結論／未完成**：解鎖 SM7150 全部 8 顆 CPU（註：fuxi Snapdragon 8 Gen 2 後續由 `0090` 調整為 7 核）。
- **出處**：`ksrc:xnu/patches/0078-sweet-smp-8-cores.patch`；上游提交 `34d5d22`

### S-079 實作 IOMFB RequestPowerChange(0) 漸暗息屏與恢復顯示

- **日期**：2026-10-18 18:56
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：`backboardd` 透過 `IOMobileFramebuffer` selector 12 `kern_RequestPowerChange(state)` 關閉螢幕（電源鍵或自動鎖定）時，`AppleCLCD` 忽略該呼叫導致最後一幀畫面持續殘留在螢幕上。
- **根因**：`0046` 的 `SweetFramebuffer.cpp` 未實作 selector 12 `RequestPowerChange`。
- **做法**：修改 `iokit/Kernel/arm/SweetFramebuffer.cpp`（新增 40 行、刪除 1 行）。當 state 為 `0` 時，於 6 個 vsync 內將面板像素逐次減半淡出並維持純黑畫面直到再次開啟（重新繪製最新幀）；面板本身保持供電，利用 AMOLED 黑像素不發光的特性實現息屏。
- **復現**：見 README 共用步驟 1–3；無須開機參數；切換顯示電源時 console 輸出 `SWEET FB: display %s\n`。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：來源未記
- **結論／未完成**：完成螢幕開關的軟體淡出黑屏實作（註：不關閉實體面板供電／背光）。
- **出處**：`ksrc:xnu/patches/0079-sweet-display-power.patch`；上游提交 `f6ac742`

### S-080 記錄 IPv4 路由與位址變更（SWEET NET 追蹤）

- **日期**：2026-10-18 20:26
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：需要確認 `configd` 是否將 USB 乙太網路（`ue0`）套用為網路服務並建立預設路由。
- **根因**：缺乏核心層級追蹤哪個行程修改 IPv4 路由與介面位址的診斷輸出。
- **做法**：修改 `bsd/net/rtsock.c` 與 `bsd/netinet/in.c`（新增 33 行），記錄 IPv4 routing socket 的 `RTM_ADD`/`DELETE`/`CHANGE`（目的、閘道、interface index，前 200 筆）與 `SIOCAIFADDR`/`SIOCDIFADDR`（介面、位址，前 100 筆）及其操作行程名稱。
- **復現**：見 README 共用步驟 1–3；無須開機參數；console 輸出 `SWEET NET: %s route %s %u.%u.%u.%u gw %u.%u.%u.%u ifindex %u flags 0x%x\n` 與 `SWEET NET: %s %s %s %u.%u.%u.%u\n`。
- **主機測試**：無
- **驗證紀錄**：成功追蹤到 `configd` 觸碰 `ue0`（CLAT 位址清理、multicast 路由）但未新增預設路由（見 `0081` 提交說明）。
- **參考來源**：來源未記
- **結論／未完成**：診斷並定位出 `ue0` 缺乏 `SIOCGIFMEDIA` 連結狀態，催生 `0081`。
- **出處**：`ksrc:xnu/patches/0080-sweet-net-trace.patch`；上游提交 `535cf30`

### S-081 讓 ue0 回報作用中的乙太網路連結狀態（SIOCGIFMEDIA 與 KEV_DL_LINK_ON）

- **日期**：2026-10-18 21:18
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：即使在 `preferences.plist` 加入 USB Ethernet 服務，`configd`（經 `0080` 追蹤確認）對 `ue0` 做了 CLAT 清理與 multicast 路由後仍不建立預設路由。
- **根因**：`KernelEventMonitor` 讀取 `SIOCGIFMEDIA` 與 `KEV_DL_LINK_ON`/`OFF` 事件，而 `ue0` 對 `SIOCGIFMEDIA` 回傳 `EOPNOTSUPP`，導致 `IPMonitor` 認定該介面沒有作用中的實體連結而不透過它設定路由。
- **做法**：修改 `bsd/dev/sweet_usb.c`（新增 37 行、刪除 1 行），在主機組態好裝置後讓 `ue0` 對 `SIOCGIFMEDIA` 回報 `IFM_ETHER`（active、1000baseT full duplex），並在主機選擇資料介面時發送 `KEV_DL_LINK_ON`。
- **復現**：見 README 共用步驟 1–3；搭配 `sweetusb=2`；無新增記錄字串。
- **主機測試**：無
- **驗證紀錄**：上游提交記明已透過 `0080` 追蹤確認原因並補上 `SIOCGIFMEDIA`/`KEV_DL_LINK_ON`，未另記補丁後實機路由結果。
- **參考來源**：來源未記
- **結論／未完成**：完成 `sweet` 系列的最後一筆補丁（0001–0081）；自 `0082` 起進入小米 13（`fuxi`）專屬移植補丁。
- **出處**：`ksrc:xnu/patches/0081-sweet-usb-link-state.patch`；上游提交 `c237db6`

## ios-fuxi（0082–0116）

### S-082 建立小米 13（fuxi）RAM-only 開機與顯示移植目標

- **日期**：2026-10-19 20:09～2026-10-19 20:20
- **狀態**：上游繼承（ios-fuxi）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：將 `sweet`（SM7150）核心移植到小米 13（`fuxi`，Snapdragon 8 Gen 2）時，RAM 佈局、幀緩衝區格式與面板驅動方式皆不同（fuxi 為 M3 command-mode 面板且尚未完成 UFS 交接／SMMU 驗證），若直接套用 `sweet` 設定無法開機。
- **根因**：`sweet` 的 bootstrap 別名假設低記憶體與不同幀緩衝區；且當時尚未驗證 fuxi UFS 交接與 SMMU，須先以 RAM disk 啟動。
- **做法**：修改 12 個檔案（`bsd/conf/Makefile.template`、`bsd/dev/sweet_ufs.c`、`iokit/Kernel/arm/SweetCPU.cpp`、`iokit/Kernel/arm/SweetFramebuffer.cpp`、`iokit/KernelConfigTables.cpp`、`libkern/conf/Makefile.template`、`makedefs/MakeInc.def`、`osfmk/arm64/arm_vm_init.c`、`osfmk/arm64/start.s`、`osfmk/arm64/sweet_early.c`、`pexpert/arm/pe_serial.c`、`pexpert/pexpert/arm64/SWEET.h`）並新增 `config/MASTER.arm64.fuxi`（共 13 檔）。建立 `fuxi` 建置目標，在純 RAM 啟動階段暫時停用 UFS、調整早期別名與 M3 command-mode 面板 `CTL0` 觸發新幀流程，並支援將受限大小的 payload 嵌入 v4 boot image。
  - 演進：
    - `e9a54d5` 2026-10-19 20:09：準備經過量測的 Fuxi RAM 與顯示啟動目標。
    - `2f19999` 2026-10-19 20:20：建置 Fuxi XNU 並將受限 payload 嵌入 v4 boot image。
- **復現**：見 README 共用步驟 1–3；編譯 `MASTER.arm64.fuxi`；UFS 停用時 console 輸出 `FUXI UFS: disabled during RAM-only bring-up\n`。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果（於 `0083` 進一步完成 RAM-root PID 1 開機）。
- **參考來源**：來源未記
- **結論／未完成**：建立 fuxi 核心建置目標與 M3 顯示基礎；UFS 探測／啟用隨後於 `0084`–`0088` 補上。
- **出處**：`ksrc:xnu/patches/0082-fuxi-ram-bringup.patch`；上游提交 `e9a54d5`、`2f19999`

### S-083 獨立原生 USB 監看器診斷通道（啟動至 RAM-root PID 1）

- **日期**：2026-10-19 21:54
- **狀態**：上游繼承（ios-fuxi）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：fuxi 初期缺乏 USB 網路與主控台輸出，需讓核心將開機進度寫入一個可被 `cpu@100` 上獨立執行的原生 USB 監看器（native monitor）讀取的固定記憶體紀錄區。
- **根因**：`cpu@100` 上的原生 USB 監看器關閉了 MMU／快取且無法 snoop XNU 的快取，若未顯式清理快取發佈紀錄，監看器無法看到核心狀態。
- **做法**：修改 `bsd/dev/sweet_ufs.c`、`bsd/kern/bsd_init.c`、`osfmk/arm64/start.s`、`osfmk/arm64/sweet_early.c`（新增 68 行、刪除 2 行），讓 XNU 將初始化資料紀錄清理至記憶體供 `cpu@100` 的獨立原生 USB 監看器讀取。
- **復現**：見 README 共用步驟 1–3；搭配載入器原生 USB 監看器使用；無新增開機參數與記錄字串。
- **主機測試**：無
- **驗證紀錄**：上游提交標題記明 `Boot Fuxi XNU to RAM-root PID 1 with independent USB diagnostics`，已可開機至 RAM-root PID 1 並透過獨立 USB 監看器回報。
- **參考來源**：來源未記
- **結論／未完成**：打通 fuxi 早期階段免 UFS 的 USB 診斷機制。
- **出處**：`ksrc:xnu/patches/0083-fuxi-native-monitor-diagnostics.patch`；上游提交 `37d2352`

### S-084 Fuxi 唯讀 UFS 交接探測與時脈／AHB 驗證（fuxi_ufs_probe）

- **日期**：2026-10-19 22:07～2026-10-19 22:08
- **狀態**：上游繼承（ios-fuxi）；ksrc：上游原樣
- **開關**：`fuxi_ufs_probe`：開機參數，啟用 fuxi 時脈驗證的唯讀 UFS 交接測試（`TEST UNIT READY` 與 `READ(10)`，確認 GPT 邊界）；預設狀態見補丁。
- **問題／現象**：在 fuxi 上啟用實際 UFS 根目錄之前，需先唯讀驗證電源／AXI／UniPro、自動 AHB gating 與 GPT 邊界是否正常。
- **根因**：fuxi UFS 控制器需要關閉自動 AHB gating（`ahb = old & ~2u`，經原生 `probe-09` 驗證），且須先確認主機狀態再發送指令。
- **做法**：修改 `bsd/dev/sweet_ufs.c` 與 `bsd/kern/bsd_init.c`（新增 56 行、刪除 5 行）。加入受 `fuxi_ufs_probe` 控制的唯讀 UFS 探測（僅允許 `TEST UNIT READY` 與 `READ(10)`），關閉自動 AHB gating、印出 `CAP`/`HCS`/`HCE`/`DMA` 與 GPT 邊界，並修正 Fuxi-only 診斷輸出條件編譯保護。
  - 演進：
    - `64bb021` 2026-10-19 22:07：準備經時脈驗證的唯讀 Fuxi UFS 交接測試。
    - `14cadb7` 2026-10-19 22:08：修正 UFS 診斷列印周圍的 Fuxi-only 保護條件。
- **復現**：見 README 共用步驟 1–3；開機參數帶 `fuxi_ufs_probe`；console 輸出 `FUXI UFS diagnostic: CAP=0x%x HCS=0x%x HCE=0x%x DMA=0x%llx\n`、`FUXI UFS read-only probe result=%d\n`、`FUXI UFS GPT bounds: first=0x%llx count=0x%llx (4K blocks)\n`。
- **主機測試**：無
- **驗證紀錄**：上游提交提及 `native probe-09` 已驗證關閉自動 AHB gating，未另記本補丁實機跑出結果。
- **參考來源**：原生 `probe-09` 實測
- **結論／未完成**：建立安全的唯讀 UFS 交接驗證；緊接著於 `0085`、`0086` 處理探測時遇到的 `CHECK CONDITION` 與電源狀態。
- **出處**：`ksrc:xnu/patches/0084-fuxi-readonly-ufs-probe.patch`；上游提交 `64bb021`、`14cadb7`

### S-085 診斷 Fuxi UFS CHECK CONDITION 原始回應封包

- **日期**：2026-10-19 22:16
- **狀態**：上游繼承（ios-fuxi）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：Fuxi UFS 探測遇到 `CHECK CONDITION` 時，既有 log 無法看到完整的 SCSI/UPIU sense 回應內容。
- **根因**：`sweet_ufs.c` 發生錯誤時未傾印 UPIU response 的 0x00–0x3f 位元組。
- **做法**：修改 `bsd/dev/sweet_ufs.c`（新增 7 行），在不擴充允許指令清單的前提下，於錯誤時印出 `response 00` 與 `response 20` 各 8 個 32-bit word。
- **復現**：見 README 共用步驟 1–3；觸發 UFS `CHECK CONDITION` 時 console 輸出 `FUXI UFS response 00: ...` 與 `FUXI UFS response 20: ...`。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：來源未記
- **結論／未完成**：補齊 UFS `CHECK CONDITION` 的 sense 診斷輸出。
- **出處**：`ksrc:xnu/patches/0085-fuxi-ufs-sense-diagnostics.patch`；上游提交 `6c18b89`

### S-086 在唯讀 I/O 前發送精確的 UFS device-WLUN ACTIVE 電源指令

- **日期**：2026-10-19 22:24
- **狀態**：上游繼承（ios-fuxi）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：ABL 交接後的 UFS 裝置可能尚未處於 ACTIVE 電源模式，需在唯讀 I/O 之前測試並切換至 ACTIVE。
- **根因**：`0084` 僅允許 `TEST UNIT READY` 與 `READ(10)`，未包含啟動 UFS 裝置電源狀態所需的 device-WLUN 指令。
- **做法**：修改 `bsd/dev/sweet_ufs.c`（新增 13 行、刪除 2 行），比照 Linux 額外允許並執行精確的 `device-WLUN ACTIVE` 電源指令。
- **復現**：見 README 共用步驟 1–3；搭配 UFS 探測／掛載；console 輸出 `FUXI UFS device ACTIVE power command result=%d\n`。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：Linux UFS 驅動（註解載明 `exact device-WLUN ACTIVE power command used by Linux`）
- **結論／未完成**：確保 UFS 裝置進入 ACTIVE 狀態以進行後續區塊讀寫。
- **出處**：`ksrc:xnu/patches/0086-fuxi-ufs-active-power.patch`；上游提交 `fbd580b`

### S-087 當 ABL autorefresh 開啟時略過手動 CTL0 START 以避免 DSC 損毀

- **日期**：2026-10-19 23:21
- **狀態**：上游繼承（ios-fuxi）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：在 M3 command-mode 面板上，當 ABL 留下 `INTF1` tear `autorefresh` 開啟時，手動觸發 `CTL0 START` 會導致 DSC（Display Stream Compression）串流損毀與面板靜止（`panel static, ar-01`）。
- **根因**：`autorefresh` 已會在每個 TE 自動送出畫面，疊加手動 `CTL0 START` 會破壞 DSC 串流。
- **做法**：修改 `pexpert/arm/pe_serial.c`（新增 11 行、刪除 2 行），僅在 `autorefresh` 關閉時才手動觸發 `CTL0 START`。
- **復現**：見 README 共用步驟 1–3；無須開機參數與記錄字串。
- **主機測試**：無
- **驗證紀錄**：`ar-01` 測試顯示僅靠 autorefresh 傳送以及關閉 autorefresh 後單次 START 皆可得到乾淨畫面，疊加則損毀 DSC。
- **參考來源**：實測實驗 `ar-01`
- **結論／未完成**：解決 fuxi M3 面板早期主控台繪製導致的 DSC 畫面撕裂／卡死。
- **出處**：`ksrc:xnu/patches/0087-fuxi-display-autorefresh.patch`；上游提交 `0f1de3a`

### S-088 將 Fuxi UFS 根裝置限制在 userdata 內預先釘選的單一 /data 檔案（extent-mapped）

- **日期**：2026-10-19 23:40
- **狀態**：上游繼承（ios-fuxi）；ksrc：上游原樣
- **開關**：無（讀取 ADT `/chosen` 既有屬性 `fuxi-root-extents` 與 `fuxi-root-blocks`）
- **問題／現象**：小米 13 仍保留 Android 系統於 `userdata`（f2fs），不可像 `sweet` 一樣覆蓋整個 `userdata` 分區，只能使用在 Android `/data` 內預先配置並釘選（pinned）的單一映像檔作為 iOS 根裝置。
- **根因**：`sweet_ufs.c` 原先直接將整個 GPT 分區暴露為 `ufsroot0` 並允許分區內任意寫入。
- **做法**：修改 `bsd/dev/sweet_ufs.c`（新增 138 行、刪除 5 行）。從 ADT `/chosen` 的 `fuxi-root-extents`（`{file block, partition block, blocks}` 4 KiB 三元組，由 `mkadt-fuxi.py --root-extents/--root-bytes` 產生）與 `fuxi-root-blocks` 載入並嚴格驗證不重疊、不越界的 extent 表；`ufsroot0` 僅映射該檔案的區塊，且 `WRITE(10)`／`SYNCHRONIZE CACHE` 僅在 `ufsrw=1`、extent 表有效、LUN 0 且寫入範圍完全落在單一 extent 內時才允許執行（同時在 `ufs_write_blocks` 與 `ufs_scsi` 雙重檢查）。
- **復現**：見 README 共用步驟 1–3；需 ADT `/chosen` 帶 `fuxi-root-extents`／`fuxi-root-blocks`；成功時 console 輸出 `FUXI UFS: root file in userdata: %u extents, %llu blocks\n`，異常時輸出 `FUXI UFS: no valid fuxi-root-extents in /chosen\n`、`FUXI UFS: bad extent %u\n` 等。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：來源未記
- **結論／未完成**：確保 fuxi 雙系統共用 `userdata` 時 iOS 讀寫不會破壞 Android f2fs 資料。
- **出處**：`ksrc:xnu/patches/0088-fuxi-extent-root.patch`；上游提交 `2762de0`

### S-089 在 Fuxi 上於指定 sweetsample= 時啟動使用者空間堆疊抽樣器

- **日期**：2026-10-20 00:10
- **狀態**：上游繼承（ios-fuxi）；ksrc：上游原樣
- **開關**：無（沿用 `0054` 的 `sweetsample=` 開機參數，僅在明確指定時於 Fuxi 啟動）
- **問題／現象**：Fuxi 停用了 `sweet_log_start()`（因為寫入 `ufsroot0` 尾端會覆蓋到同一檔案內的 HFS 映像本身），但也連帶未啟動 `sweet_sample` 行程抽樣器。
- **根因**：`sweet_ufs.c` 原本將抽樣器與 `sweetlog` 綁在同一處初始化路徑。
- **做法**：修改 `bsd/dev/sweet_ufs.c`（新增 10 行、刪除 1 行），當開機參數給定 `sweetsample=` 時單獨啟動使用者堆疊抽樣器，同時保持 `sweet_log_start()` 關閉以保護 HFS 映像。
- **復現**：見 README 共用步驟 1–3；開機參數帶 `sweetsample=<行程名單>`；console 輸出 `SMP ...`。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：來源未記
- **結論／未完成**：在不開啟 UFS 尾端 `sweetlog` 的前提下恢復 fuxi 的 `sweetsample` 診斷。
- **出處**：`ksrc:xnu/patches/0089-fuxi-stack-sampler.patch`；上游提交 `8025054`

### S-090 將 Fuxi SMP 上限設為 7 核（保留 cpu@100 給 USB 監看器）

- **日期**：2026-10-20 00:23
- **狀態**：上游繼承（ios-fuxi）；ksrc：上游原樣
- **開關**：無（將 fuxi 的 `cpus=` 上限夾在 7 核）
- **問題／現象**：Fuxi 8 顆 CPU 中，`cpu@100`（MPIDR `0x100`）正持續執行 loader 的獨立 USB 除錯監看器，若 XNU 接管該核心會導致監看器中斷或衝突。
- **根因**：`0078` 預設上限為 8 核且假設全部納入 XNU。
- **做法**：修改 `iokit/Kernel/arm/SweetCPU.cpp` 與 `pexpert/pexpert/arm64/SWEET.h`（新增 4 行、刪除 2 行），在 fuxi 上將 `cpus=N` 上限設為 7 核，把 `cpu@100` 留給 loader USB monitor 且不列入 ADT（搭配 `ADT --smp`）。
- **復現**：見 README 共用步驟 1–3；開機參數帶 `cpus=7`；無新增記錄字串。
- **主機測試**：無
- **驗證紀錄**：於 `0091` 第一筆提交說明記載 `SMP verified on Fuxi`。
- **參考來源**：來源未記
- **結論／未完成**：達成 fuxi 7 核 SMP 運作且不干擾 `cpu@100` USB 監看器。
- **出處**：`ksrc:xnu/patches/0090-fuxi-smp-seven-cores.patch`；上游提交 `334e3ef`

### S-091 新增 Fuxi ST FTS（FTM5）SPI 觸控驅動（透過 QUP1 SE4 GPI DMA）

- **日期**：2026-10-20 00:56～2026-10-20 03:35
- **狀態**：上游繼承（ios-fuxi）；ksrc：本機修改 +1／-1（本機提交 `75148d2`）；本機修改見 E-037、G-025、K-072
- **開關**：
  - `fuxitouch=0`：開機參數，停用 fuxi 觸控驅動（預設啟用）。
  - `fuxitouchdelay=<秒>`：開機參數，延遲啟動觸控驅動的秒數。
- **問題／現象**：小米 13 使用 STMicroelectronics FTS（FTM5）觸控 IC 接在 QUP1 SE4（僅支援 GSI/GPI DMA，無 FIFO 模式），且須透過 apps-smmu SMRs 繞過故障並處理 GPII0 通道重初始化與快取維護，否則無法讀取觸控座標。
- **根因**：`sweet_touch.c` 為 I2C 輪詢（GT9896/Focaltech），不適用於 fuxi 的 SPI + GPI DMA 架構。
- **做法**：修改 `bsd/conf/files`、`bsd/dev/sweet_ufs.c` 並新增 `bsd/dev/fuxi_touch.c`（共 3 檔、601 行）。利用 loader 預先投票的電源軌（`ldob12` 1.8 V、`ldom6` 3.2 V），設定 wrapper1/SE4 時脈（9.6 MHz）、將 GPI stream `0xb6` 與 QUP1 stream `0xa3` 的 apps-smmu SMR 導向 context bank 4（stage 1 off）、停止並重置 loader 留下的 GPII 0（TX ch0、RX ch1、event ring），在快取一致維護下以 SPI opcode + address 讀取 FTS FIFO（10x 超解析度座標對應 1080x2400 面板）並送入 `sweet_hid_touch()`。
  - 演進：
    - `83f6d83` 2026-10-20 00:56：完成 Fuxi SMP 驗證並提出 `0091` 觸控驅動草稿。
    - `40d99aa` 2026-10-20 02:01：實作經 GPI DMA 的 FTS 觸控（GPII0 重初始化、apps-smmu SMRs、快取維護）。
    - `21cef5e` 2026-10-20 03:01：記錄觸控回報，且 monitor 每 10 分鐘更新重置期限。
    - `a96128b` 2026-10-20 03:35：即使沒有 IRQ 也每 32 ms 讀取一次 FTS FIFO，並記錄首批事件。
- **復現**：見 README 共用步驟 1–3；預設啟用（`fuxitouch=0` 關閉）；console 輸出 `FUXI TOUCH: GPI up, DMA page %#llx\n`、`FUXI TOUCH: reset err=%d ... chip id err=%d %02x%02x\n`、`FUXI TOUCH: sensing, polling GPIO%u\n`。
- **主機測試**：無
- **驗證紀錄**：註解記明該序列首先在 loader（`shim/fuxi-probe/probe.c request 0x55`、`ts-12`）實證成功；XNU 驅動實機整合於 `0093` 繼續優化。
- **參考來源**：`research/touch/hw-report.md`、`research/touch/gpi-recipe.md`（`ts-12`）與 `shim/fuxi-probe/probe.c`
- **結論／未完成**：建立小米 13 ST FTS SPI 觸控輸入；後續 `0093` 優化為 4 ms 自適應輪詢與錯誤恢復。
  - 基底 `979e2f08` 之後的本機提交（舊到新，箭頭後是記錄該修改的項目）：
    - `75148d2` 2026-11-14 19:45：base: 979e2f08 + harness patches (HEAD 2aae087)（本份是 `kernel-input-fixes.patch`：觸控延遲 30→10 秒） → E-037、G-025、K-072
- **出處**：`ksrc:xnu/patches/0091-fuxi-touch-fts-spi.patch`；上游提交 `83f6d83`、`40d99aa`、`21cef5e`、`a96128b`；本機提交 `75148d2`

### S-092 新增 Fuxi 電源／音量鍵驅動（SPMI arbiter v7 唯讀輪詢）

- **日期**：2026-10-20 05:48
- **狀態**：上游繼承（ios-fuxi）；ksrc：上游原樣
- **開關**：
  - `fuxikeys=0`：開機參數，停用 fuxi 按鍵驅動（預設啟用）。
  - `fuxikeys_gpiocfg=1`：開機參數，僅當 PM8550 GPIO6 尚未啟用為輸入時，透過其 RW 通道寫入 Linux pinctrl 預設組態（volatile）；預設 `0`（全程唯讀）。
- **問題／現象**：小米 13 的電源鍵與音量鍵接在 PMK8550（SID 0）與 PM8550（SID 1）且使用 SPMI arbiter v7（`qcom,spmi@c42d000`），與 `sweet` 的 SPMI 位址與暫存器不同。
- **根因**：`sweet_keys.c` 的 PMIC 位址與 arbiter 版本不適用於 SM8550 平台。
- **做法**：修改 `bsd/conf/files`、`bsd/dev/sweet_ufs.c` 並新增 `bsd/dev/fuxi_keys.c`（共 3 檔、295 行）。每 20 ms（需連續兩次相同取樣去彈跳）透過 SPMI arbiter v7 observer 通道唯讀讀取 PMK8550 PON `RT_STS 0x1310` bit 7（電源）與 bit 6（音量下），以及 PM8550 SID 1 `GPIO6 @0x8d00` `RT_STS 0x8d10` bit 0（音量上，active low），送入 `sweet_hid_key()`。
- **復現**：見 README 共用步驟 1–3；預設啟用（`fuxikeys=0` 關閉、選配 `fuxikeys_gpiocfg=1`）；console 輸出 `FUXI KEYS: polling every 20 ms (power=%d volume-up=%d volume-down=%d)\n`。
- **主機測試**：無
- **驗證紀錄**：上游提交提及測試計畫於 `docs-lanes/buttons.md`，未記實機結果。
- **參考來源**：實機 DT `research/device/probe-09/live.dtb`、`docs-lanes/buttons.md` 與 Linux `drivers/spmi/spmi-pmic-arb.c` v7／`pinctrl-spmi-gpio`
- **結論／未完成**：完成小米 13 實體電源鍵與音量鍵支援。
- **出處**：`ksrc:xnu/patches/0092-fuxi-buttons.patch`；上游提交 `55a7c6d`

### S-093 優化 Fuxi 60 Hz 顯示呈現與 4 ms 自適應觸控輪詢

- **日期**：2026-10-20 05:47～2026-11-05 23:28
- **狀態**：上游繼承（ios-fuxi）；ksrc：本機修改 +1／-1（本機提交 `75148d2`）；本機修改見 E-037、G-025、K-072
- **開關**：
  - `fuxitouch_poll_us=<微秒>`：開機參數，手指按下或剛見 IRQ 時的活躍輪詢週期；預設 `4000`（4 ms）。
  - `fuxitouch_idle_us=<微秒>`：開機參數，閒置時的輪詢週期；預設 `8000`（8 ms；息屏時為 32 ms）。
  - `fuxitouch_offsuppress=0`：開機參數，關閉「息屏時抑制上報觸控」的行為；預設啟用抑制（`1`）。
- **問題／現象**：`SweetFramebuffer` 預設僅 10 Hz、息屏時執行對 write-combined 記憶體的 uncached read-modify-write 漸暗非常緩慢，且觸控輪詢與錯誤恢復尚不夠靈敏。
- **根因**：`0056`/`0079` 與 `0091` 沿用早期保守參數與單核最佳化。
- **做法**：修改 `bsd/dev/fuxi_touch.c` 與 `iokit/Kernel/arm/SweetFramebuffer.cpp`（新增 261 行、刪除 54 行）。
  - `SweetFramebuffer`：Fuxi 預設 `sweetfb_hz` 提升至 `60` Hz，改用絕對時間期限（absolute deadline）避免複製時間拉長週期，每 600 幀印出統計 `SWEET FB: stats`，息屏改為單次純寫入填黑並透過 `sweet_fb_display_off` 通知觸控驅動。
  - `fuxi_touch`：優先權 92、無 leeway 絕對時間期限 4/8/32 ms 自適應輪詢，兩次 SPI 讀取排空 FIFO、錯誤時自動釋放觸點並重初始化 GPII、IC 重置後重新開啟 sensing，並以每次接觸摘要取代逐事件傾印。
  - 演進：
    - `52eea4b` 2026-10-20 05:47：實作 60 Hz 呈現、4 ms 自適應觸控輪詢與息屏觸控抑制（CI `ci-lane-touchdisp` 編譯 `rc=0`，未開機）。
    - `31e6bd5` 2026-11-05 23:23：曾允許 `sweetfb_hz` 高於 60 以縮短 vsync tick（120 Hz 實驗）。
    - `65b508d` 2026-11-05 23:28：將 120 Hz 實驗移至 P-UNIXNU overlay 並還原對 `0093` 的修改。
- **復現**：見 README 共用步驟 1–3；預設生效（選配 `fuxitouch_poll_us`、`fuxitouch_idle_us`、`fuxitouch_offsuppress=0`）；console 輸出 `SWEET FB: present up to %u Hz (every %u ticks)\n`、`SWEET FB: stats %u frames in %llu ms ...`、`FUXI TOUCH: contact %u: %u reports in %llu ms ...`。
- **主機測試**：無（CI `ci-lane-touchdisp` 編譯 `rc=0`）
- **驗證紀錄**：上游提交記明 `ci-lane-touchdisp` 編譯通過但當時尚未開機（`not booted`）。
- **參考來源**：`docs-lanes/touch-display.md` 與 `fts_event_handler` 兩段式 FIFO 讀取流程
- **結論／未完成**：建立 60 Hz 顯示與 4 ms 觸控基礎；後續 120 Hz 與 GPU scanout 於 `0098`、`0106`、`0108` 接手。
  - 基底 `979e2f08` 之後的本機提交（舊到新，箭頭後是記錄該修改的項目）：
    - `75148d2` 2026-11-14 19:45：base: 979e2f08 + harness patches (HEAD 2aae087)（本份是 `kernel-input-fixes.patch`：觸控延遲 30→10 秒（上下文）） → E-037、G-025、K-072
- **出處**：`ksrc:xnu/patches/0093-fuxi-touch-display-polish.patch`；上游提交 `52eea4b`、`31e6bd5`、`65b508d`；本機提交 `75148d2`

### S-095 新增 sweetnoexec= 開機參數以拒絕重試循環 daemon 的 exec

- **日期**：2026-10-20 05:42
- **狀態**：上游繼承（ios-sweet）；ksrc：上游原樣
- **開關**：`sweetnoexec=<名單>`：開機參數，逗號分隔要在 `exec` 階段直接拒絕執行的二進位檔名稱（例如 `bluetoothd,fairplayd.H2`）；預設無。
- **問題／現象**：缺乏對應硬體或服務的 daemon（如 `bluetoothd`、`fairplayd.H2`）會不斷崩潰並被 `launchd` 反覆重新啟動（respawn-loop），浪費 CPU 與 I/O。
- **根因**：每次重啟都得進入 `dyld` 載入並崩潰，無低成本攔截機制。
- **做法**：修改 `bsd/kern/kern_exec.c`（新增 44 行），在進入 `dyld` 之前即比對 `sweetnoexec` 名單並直接拒絕 `exec`，每個名稱於首次拒絕時記錄一行。
- **復現**：見 README 共用步驟 1–3；開機參數帶 `sweetnoexec=bluetoothd,fairplayd.H2`；console 輸出 `exec %s blocked (sweetnoexec)\n`。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果（後續實作中此參數曾用於阻擋 `fairplayd.H2` 等，後另有 `Disabled` plist 解法）。
- **參考來源**：來源未記
- **結論／未完成**：提供透過開機參數快速止血 respawn-loop daemon 的機制。
- **出處**：`ksrc:xnu/patches/0095-sweet-noexec-bootarg.patch`；上游提交 `de5dd60`

### S-096 整合 Adreno A740 GPU 冷啟動、PAS13 韌體載入與非同步 2D 呈現（fuxigpu）

- **日期**：2026-10-20 11:51～2026-10-20 13:27
- **狀態**：上游繼承（ios-fuxi）；ksrc：上游原樣
- **開關**：
  - `fuxigpu=<值>`：開機參數，opt-in 啟用 A740 GPU 冷啟動與 GPU present 路徑；預設關閉（維持 CPU 呈現）。
  - `fuxigpu_stage=<階段>`：開機參數，設定 A740 階段式啟動目標階段限制。
  - `fuxigpu_fail`：開機參數，用於注入或測試 GPU 失敗情境（預設關閉；詳見補丁）。
- **問題／現象**：CoreAnimation／`SweetFramebuffer` 僅靠 CPU 複製 1080x2400 畫面負荷高；需要讓 Qualcomm Adreno A740 GPU 在 XNU 下完成冷啟動、PAS13 韌體認證／載入、SMMU 映射與非同步 2D BLT 畫面呈現（BGRA strided rects + fence）。
- **根因**：XNU 缺乏 Qualcomm A740/GMU 驅動與 Darwin 相容的 `qcom_scm` SMC 呼叫封裝（Darwin AArch64 組譯器將 `;` 視為註解），且不可在 8 MiB IO submap 中使用 `KMA_NOFAIL` 耗盡空間；此外固定 GPU 記憶體須在 VM 啟動前從 `boot_args` 與 ADT RAM 排除。
- **做法**：修改 `bsd/conf/files`、`iokit/Kernel/arm/SweetFramebuffer.cpp`、`osfmk/arm64/machine_routines_asm.s`、`osfmk/arm64/sleh.c`、`iokit/Kernel/arm/SweetSurface.cpp`、`iokit/Kernel/arm/SweetSurface.h` 與新增／同步 `bsd/dev/fuxi_adreno.c`、`fuxi_adreno.h`、`gpu.c`、`gpu-a740.h`、`gpu-handoff.h`、`gpu-zap.h`（共 12 檔、3,128 行）。
  - 使用 `kernel_map` 虛擬位址與可失敗的分頁表擴展（不佔 IO submap），並保留 32 MiB NC（高記憶體 bank 頂端含 GPU scratch）。
  - 在 `machine_routines_asm.s` 實作錯誤隔離（fault-contained）的 32/64-bit MMIO 存取與 Darwin 正確的 SMC 呼叫（PAS13 ELF32 LE 載入、CPU0 綁定、單次有界重試）。
  - 以 4 組快取 backing、4K SMMU granules（2 MiB / 64 KiB 葉群組）、220 MHz 預設時脈與非同步 one-in-flight（無定時器睡眠）執行 GPU present，異常時立即永久隔離並退回 CPU 呈現。
  - 演進：
    - `8b36bdc` 2026-10-20 11:51：為 XNU 加入 opt-in 的 A740 GPU 畫面呈現（M6 平台轉接器與共享探測碼）。
    - `e289e5f` 2026-10-20 12:20：使 GPU 映射移出 IO submap 並改為可失敗配置（`kernel_map` NC）。
    - `1bc3668` 2026-10-20 12:28：修正 Darwin 核心組譯器下的 GPU SCM/SMC 呼叫（以換行取代 `;` 分隔指令）。
    - `cceacff` 2026-10-20 12:47：透過 4 組快取 backing 與睡眠等待降低 GPU present 的 CPU 成本。
    - `c86a44c` 2026-10-20 13:25：使用完整 Fuxi RAM bank 並將 GPU scratch 置於頂端。
    - `e9fb8fe` 2026-10-20 13:27：改為在顯示 tick 上非同步退休（retire）GPU present（one-in-flight，無定時器睡眠）。
- **復現**：見 README 共用步驟 1–3；開機參數帶 `fuxigpu=1`（及選配 `fuxigpu_stage`）；成功時 console 輸出 `FUXI GPU: through stage %u PASS, GMU CB%u GPU CB%u ASID%u, ...`、`FUXI GPU: ready, BGRA strided rects + one fence, 4 cached backings, 220 MHz, async one-in-flight, no timer sleep\n`、`FUXI GPU: presents GPU %u CPU %u, GPU retire avg %llu us CPU avg %llu us, ...`。
- **主機測試**：無
- **驗證紀錄**：上游提交未記單獨實機數據（後續 `0098` 在此基礎上擴充 NativeAdreno ABI 與時脈／掃描輸出）。
- **參考來源**：Linux 6.12 `msm/adreno`（`a6xx_catalog.c` / `a6xx.xml`、`a6xx_bus_clear_pending_transactions`、`mdt_loader`、`qcom_scm`）、`ianchb/sm8550-mainline`（`42f3b40c`）、Mesa `25.0.7` XML / `875160ed32cc`、UniXNU-sheng `GpuPowerBoard.h`、`docs-lanes/gpu.md`；授權 `GPL-2.0-only`
- **結論／未完成**：建立 A740 冷啟動、PAS13 與非同步 GPU 2D present 基礎，預設關閉；3D/Metal 計算與掃描輸出由 `0098` 接續。
- **出處**：`ksrc:xnu/patches/0096-fuxi-adreno.patch`；上游提交 `8b36bdc`、`e289e5f`、`1bc3668`、`cceacff`、`c86a44c`、`e9fb8fe`

### S-097 實作 Fuxi 觸控／編譯 CPU 加速、推薦核心遮罩與熱壓力橋接（fuxiperf／fuxitp／fuxicores）

- **日期**：2026-10-20 13:33～2026-10-26 01:35
- **狀態**：上游繼承（ios-fuxi）；ksrc：本機修改 +44／-11（本機提交 `75148d2`）；本機修改見 G-018、G-047、K-072
- **開關**：
  - `fuxiperf=1`：開機參數，啟用 SM8550 EPSS LUT 觸控／swap CPU 頻率投票與 TSENS v2 溫度保護（可搭配 `fuxiperfstart` 預設 600 dC、`fuxiperfhot`、`fuxiperflimit` 等；預設關閉）。
  - `fuxicores=<遮罩>`：開機參數，設定推薦核心遮罩（如 `0x7c` 將一般執行緒留在 gold 與 prime 核心，避開 A510）；預設關閉。
  - `fuxitp=1`：開機參數，暴露唯讀 sysctl `kern.fuxipressure` 橋接 HackiOS 熱壓力等級（需同時 `fuxiperf=1`）；預設關閉。
  - `fuxicompileboost`：開機參數與可寫 sysctl `kern.fuxicompileboost`（`1` enter、`0` leave），提供 FuxiMetal 著色器編譯期間有界租約（最長 2000 ms，不遷移即時執行緒）；預設關閉。
- **問題／現象**：同質排程器將大量 UI 工作放在小核（A510）上，且 CPU 維持低頻；但若無溫度保護直接升頻可能過熱，而初期實作也遇到編譯拒絕回傳 `EAGAIN` 導致 `userland_sysctl()` 無限重試讓 `backboardd` 卡死、即時渲染執行緒無法綁定被拒 49 次、20 ms TSENS 斜率誤判、以及熱壓力等級在開機 90 秒內切換 11 次等問題。
- **根因**：XNU 缺乏 SM8550 EPSS 頻率投票、TSENS v2 溫度／斜率／佔空比保護，以及對應 iOS `OSThermalPressureLevel` 的核心橋接。
- **做法**：修改 `bsd/conf/files`、`bsd/dev/fuxi_touch.c`、`bsd/dev/fuxi_keys.c`、`iokit/Kernel/arm/SweetFramebuffer.cpp`、`osfmk/kern/sched_prim.c` 並新增 `bsd/dev/fuxi_perf.c`、`fuxi_perf_logic.h`、`fuxi_compile_abi.h`、`osfmk/kern/fuxi_compile_sched.h`（共 9 檔、964 行）。由單一序列化背景 worker 管理 MMIO（input/swap hook 僅發布時間戳並排程），依 EPSS LUT 進行 cool/high/idle 投票、100 ms 前瞻斜率預測、TSENS v2 逐感測器有效性檢查、200 ms 斜率窗口與滾動佔空比限制。
  - 演進：
    - `d483419` 2026-10-20 13:33：輸入時搭配 TSENS 熱保護提升 Fuxi CPU 頻率投票。
    - `8dae9f4` 2026-10-20 13:59：限制 boost LUT 讀取範圍並容忍無效的 CPU 溫度感測器。
    - `deb8e76` 2026-10-20 14:07：在 CPU 電源域喚醒期間重試 governor 初始化。
    - `d69aa59` 2026-10-20 14:15：為 Fuxi TSENS v2 溫度採用逐感測器有效性判定。
    - `0728ffe` 2026-10-21 12:06：將觸控加速改為具備熱預測與有界滾動佔空比（rolling duty）。
    - `e6ad973` 2026-10-23 18:02：新增輸入與 swap 不活躍後的 opt-in idle LUT 降頻投票（`fuxiperfidle`）。
    - `4d8467f` 2026-10-23 19:10：修正首次 swap 前不得進入 idle 投票（先前提早降頻導致 `backboardd` 首幀從 7.9 秒退步至 14.0 秒）。
    - `75aa310` 2026-10-24 10:30：新增 opt-in 有界的 FuxiMetal 編譯加速（`kern.fuxicompileboost`）。
    - `08c5f37` 2026-10-24 11:13：編譯加速拒絕改回傳 `EBUSY` 而非 `EAGAIN`（因 `userland_sysctl()` 會對 `EAGAIN` 無限重試導致 `backboardd` 卡死）。
    - `496f196` 2026-10-24 11:37：為無法綁核的即時執行緒（CA 渲染執行緒）提供不遷移的編譯租約並記錄拒絕（解決實機 49 次進入全被拒）。
    - `d2dbfd9` 2026-10-24 12:00：將熱斜率量測窗口改為 200 ms（避免單次 0.4°C 量化跳動在 20 ms 換算成 200 dC/s 誤鎖）。
    - `4c6bc1d` 2026-10-24 13:44：新增 opt-in `fuxicores` 推薦核心遮罩（如 `0x7c` 導向 gold/prime）。
    - `b9d6691` 2026-10-24 22:48：移植 opt-in HackiOS 熱壓力橋接（`fuxitp=1`、`kern.fuxipressure`、`thermald`）。
    - `4bf23f9` 2026-10-25 00:31：將熱壓力下降遲滯延長為維持 10 秒（解決首次 `fuxitp=1` 開機前 90 秒內在 nominal 與 moderate 間切換 11 次）。
    - `6100a10` 2026-10-26 00:33：將觸控加速起始溫度改為開機參數 `fuxiperfstart`（預設 600 dC；因合成器下 SoC 常處 60–64°C 導致一直停在 cool LUT）。
    - `6a46556` 2026-10-26 01:35：記錄 boost 與未 boost 時 gold/prime 實際 EPSS 頻率（`+0x20` 狀態暫存器）。
- **復現**：見 README 共用步驟 1–3；開機參數帶 `fuxiperf=1`（及選配 `fuxicores=0x7c`、`fuxitp=1`、`fuxicompileboost=1`、`fuxiperfstart=...`）；console 輸出 `FUXI PERF: ready, LUT cool %u/%u/%u kHz high %u/%u/%u kHz, hold 1500 ms after %u retries\n`、`FUXI PERF: thermal hot %u ...`、`FUXI PERF: stats boosts %llu ...`。
- **主機測試**：提交 `b9d6691` 記載 47 項 Linux 主機測試與新 ASan/UBSan daemon/staging 測試通過，Metal staging 與 108 份 XNU 補丁順序套用檢查通過。
- **驗證紀錄**：實機觀察到 `fuxiperfidle` 首幀從 7.9 秒變 14.0 秒（已修）、即時執行緒 49 次編譯加速被拒（已修）、`fuxitp=1` 前 90 秒切換 11 次（已修）、合成器下 60–64°C 鎖在 cool LUT（已參數化）。
- **參考來源**：HackiOS（`https://github.com/oosexgang/hackios-xnu-userland` commits `fcc3214` + `a79ff07`，`thermald.c` 與 `com.redwood.thermald.plist`，MIT 授權）
- **結論／未完成**：完成 SM8550 溫度受控的觸控／編譯升頻、核心導向與熱壓力回報（預設皆需開機參數 opt-in）。
  - 基底 `979e2f08` 之後的本機提交（舊到新，箭頭後是記錄該修改的項目）：
    - `75148d2` 2026-11-14 19:45：base: 979e2f08 + harness patches (HEAD 2aae087)（本份是 `kernel-perf-governor.patch`／`kernel-perf-wide.patch`：`fuxiperfbase`、`fuxiperfmax`、`fuxiperfbudget`、`fuxiperf=2`） → G-018、G-047、K-072
- **出處**：`ksrc:xnu/patches/0097-fuxi-touch-boost.patch`；上游提交 `d483419`、`8dae9f4`、`deb8e76`、`d69aa59`、`0728ffe`、`e6ad973`、`4d8467f`、`75aa310`、`08c5f37`、`496f196`、`d2dbfd9`、`4c6bc1d`、`b9d6691`、`4bf23f9`、`6100a10`、`6a46556`；本機提交 `75148d2`

### S-098 實作 NativeAdreno v2 使用者空間 GPU ABI、時脈／DDR DCVS 與掃描輸出最佳化

- **日期**：2026-10-20 14:36～2026-11-06 23:34（本機提交至 2026-11-16 07:43）
- **狀態**：上游繼承（ios-fuxi）；ksrc：本機修改 +515／-6（本機提交 `75148d2`、`3277d2c`、`eafc778`、`ba65d38`、`7af418a`）；本機修改見 G-014、G-019、G-027、G-048、K-005、K-017、K-018、K-019、K-072
- **開關**：
  - `fuxigpuabi`：開機參數，啟用 `NativeAdreno` user client ABI v2（open `0x4147`，僅 root，100 ms watchdog）。
  - `fuxigpuclock`、`fuxigpulevel`、`fuxigpuddr`：開機參數，控制 GPU 時脈等級（含 615/680 MHz）、DDR 頻寬投票與 bit 4 idle DCVS。
  - `fuxigpuncimport`：開機參數位元遮罩（bits 0–29），控制 NC import 快取清理策略、coherency 自測（bit 64）、範圍 TLBIVAL（128）、免等閒置 BoMap（256 還原）、提交佇列（512）、唯讀 import（1024）、present 鎖定（4096）、120Hz 門檻（8192）、DSI 探針（16384）、TE spin（32768）、mid-period START（65536）、autorefresh（131072，標註勿啟用）、hard spin（262144）、dirty-bit 追蹤（524288）、pre-clean（bit 20）、inline present（bit 21）、direct DPU scanout 階段 1–3（bits 22–24）、GPU fence 中斷 SPI 300（bit 26）、copy_first_park（bit 27）、觸發型 128 幀紀錄（bit 28）、cached-coherent BO（bit 29）等。
  - `fuxipanel120`：開機參數，啟用 120 Hz 面板掃描輸出模式與 TE 相位鎖定。
  - `fuxiscandouble`、`fuxiscantriple`、`fuxiscandrain`、`fuxiscante`、`fuxiscancpuflip`：開機參數，分別控制雙／三重掃描輸出緩衝區、傳輸排空下限、TE 閘控與 CPU 雙緩衝翻頁。
  - `fuxiearly`：開機參數位元遮罩（bits 1/2/4/8），控制提前退休、admission retire（bit 2）、present FIFO（bit 4）與 per-job GPU ring timestamps（bit 8）。
- **問題／現象**：系統需要將 Adreno A740 開放給使用者空間 Turnip/FuxiMetal 使用，並逐步解決效能與畫面更新瓶頸（包括 HostImport 權限／頁表查詢、BO 數量／記憶體上限、提交／映射鎖定與快取維護成本、120 Hz 面板 TE 同步、DPU 直接掃描輸出以及幀排隊延遲）。
- **根因**：Apple 原廠 Metal/AGX 驅動為閉源；且 A740 須透過與 UniXNU `NativeAdreno` 相容的 ABI v2 提供 userland GPU 提交與記憶體映射，同時配合小米 13 M3 面板與 MDSS/SMMU 硬體特性進行掃描輸出同步。
- **做法**：修改／新增 26 個檔案（含 `bsd/dev/fuxi_adreno.c`、`fuxi_adreno.h`、`gpu.c`、`gpu-clock.h`、`fuxi_gpu_scanout.inc`、`fuxi_gpu_abi_backend.inc`、`fuxi_gpu_irq.inc`、`fuxi_gpu_dcvs.h`、`NativeAdrenoABI.h`、`fuxi_abi_wire.h`、`fuxi_gpu_abi.h`、`osfmk/arm/fuxi_adreno_host.c`、`iokit/Kernel/arm/FuxiAdrenoUserClient.cpp` 及各授權檔 `LICENSE.*`，共 6,605 新增行）。涵蓋 ABI v2 提交／時間戳／狀態查詢、16K-to-4K `HostImport`、IO-coherent 自測與 cached-coherent BO、HFI GPU/DDR DCVS、SPI 300 fence 中斷、inline present 複製與插隊機制、DPU SMMUv2 stage 1–3 直接掃描輸出及 120 Hz TE 同步。本機另整合 `IBPROF` draw census、`fuxibacklight=4`、`logdump` 紀錄與 `SweetBattery` 調整。
  - 演進（全部 68 筆上游提交）：
    - `19fbf5e` 2026-10-20 14:36：加入受閘控的 `NativeAdreno` 使用者空間 ABI。
    - `3ff6388` 2026-10-20 16:05：讓 Fuxi GPU ABI 初始化失敗可自我描述。
    - `93da876` 2026-10-20 16:35：在 A740 上接受分頁粒度的 UCHE 位址讀回。
    - `5c0b2da` 2026-10-20 20:15：修正 GPU BO cache-sync PA 截斷並打包 `metal2g`。
    - `5fd2350` 2026-10-20 21:54：加入階段式 Turnip smoke 測試與有界 GPU hang 診斷。
    - `eea1c7b` 2026-10-21 13:00：實作 `NativeAdreno` ABI 2 host 記憶體匯入（`HostImport`）。
    - `62d1aed` 2026-10-21 13:22：將私有 pmap 查詢移至 FUXI 專屬 `osfmk/arm/fuxi_adreno_host.c`（固定 16K 轉換、僅接受 DEFAULT/WCOMB）。
    - `aa2c98f` 2026-10-23 01:45：實作 `NativeAdreno` timestamp（method 7）與 status（method 8），提高 `MaxBos` 至 1024、wired 上限至 768 MiB、import 表至 512（K1）。
    - `40d8e79` 2026-10-23 13:19：加入有界 GPU 時脈等級與主動 DDR 投票及啟動診斷。
    - `0c7a34c` 2026-10-23 13:33：加入 opt-in 有 fence 的合成幀 GPU 掃描輸出（scanout）。
    - `06dc4b4` 2026-10-23 16:36：scanout 改等待 INTF 寫入線 `TEAR_LINE_COUNT (+0x2b0)` 而非面板讀取指標 `+0x2a8`。
    - `4c26c3e` 2026-10-24 22:50：移植 `fuxigpuncimport=1`（映射時清理 NC import、省略後續提交清理）並延伸現有 present 統計。
    - `06a27ab` 2026-10-25 17:15：加入 `fuxiscandrain` 排空下限開機參數與逐閘門等待計數器。
    - `d1425aa` 2026-10-25 21:00：改讀 `INT_COUNT_VAL (+0x298)` 記錄 TE 速率與 START 間距，新增 opt-in TE 閘門 `fuxiscante=1`。
    - `66326cd` 2026-10-25 22:01：新增 opt-in Fuxi 120 Hz 面板掃描輸出模式（`fuxipanel120`）。
    - `86b9a2e` 2026-10-25 22:31：opt-in 將合成器 vsync 相位鎖定至面板 TE。
    - `8275b18` 2026-10-25 23:54：新增 opt-in GPU DDR 投票與 680 MHz 檔位選擇。
    - `4acbbf5` 2026-10-26 19:01：跨 TE 週期雙緩衝 opt-in Fuxi GPU 掃描輸出（`fuxiscandouble`）。
    - `40dccb8` 2026-10-26 19:30：在 CPU 幀之後的下一個 GPU 幀自動重新進入雙緩衝（避免開機閃黑一次即永久退回單緩衝）。
    - `a72b285` 2026-10-27 01:27：佇列化 scanout swap 並在寫入端退休時喚醒呈現。
    - `fffb7a4` 2026-10-27 02:04：直接在喚醒時執行呈現並量測 scanout 階段延遲。
    - `7857d9a` 2026-10-27 04:22：為 Fuxi GPU scanout 加入 opt-in 三重緩衝（`fuxiscantriple`）。
    - `8cce9ed` 2026-10-27 04:51：為三重 scanout 緩衝區 C 預留 GPU 分頁表空間（來源紋理表改從 slot 32 開始，解決 `'scanout geometry/mapping (0x4)'`）。
    - `8c1bd3b` 2026-10-27 08:56：降低 ABI submit/map/free/wait 核心成本（`BoMap` 每 16K 頁單次 pmap walk、`WaitFence` 釋放鎖睡眠等）並為每個 selector 計時。
    - `05613d3` 2026-10-27 09:43：加入快取重疊鏡像（per-4K bitmap）、區段執行 `BoImport`、`fuxigpuncimport` 改為位元遮罩（64 IO-coherent 自測、128 範圍 `TLBIVAL+TLBSYNC`）。
    - `038138b` 2026-10-27 09:53：加入 opt-in idle DCVS（`fuxigpulevel` bit 4）與 UniXNU SM8550 比較筆記。
    - `4e30dbb` 2026-10-27 09:51：移植 Adreno 740 submit/ring 改進：handle index、128-IB 提交、fenced WPTR、opt-in stall recovery 與 stall 快照。
    - `3af1cfd` 2026-10-27 12:21：`BoMap` 不再等待 GPU 閒置（bit 256 還原等待），將子階段耗時（`mapwait`/`maplock`/`mapwork`/`allocmem`/`bind`）印於獨立 `stages` 行。
    - `74642f1` 2026-10-27 12:28：opt-in 客戶端提交佇列（`fuxigpuncimport` 512，深度 4）：接續在執行中工作之後，省去 TTBR 切換與 idle 等待。
    - `9efbaeb` 2026-10-27 16:00：host import stage-4 探測、opt-in 唯讀 import（1024）與 1/64-tick scanout 複製輪詢（2048）。
    - `49015ea` 2026-10-27 18:34：opt-in present hold（`fuxigpuncimport` 4096）：被拒 present 暫擋客戶端至其複製提交（上限 4 ms）。
    - `e488177` 2026-10-27 19:07：量測 START-to-transfer-done 與 ready-to-START；opt-in panel120 3/4 週期下限與快速 START 重試輪詢（8192）。
    - `42ddd43` 2026-10-27 20:10：opt-in DSI 傳輸探針（16384）：每 8 次 START 記錄細粒度 TE 邊緣／START 清除／寫入線結束時間與直方圖。
    - `627766f` 2026-10-27 21:27：修正 DSI 探針提早退出；加入傳輸完成後的 opt-in TE 對齊 START 自旋（32768）。
    - `5b12759` 2026-10-27 21:59：僅在必要或 ≤1 MiB 時嘗試連續 BO 記憶體配置（解決大連續搜尋每次卡頓 50–58 ms）。
    - `529f7bc` 2026-10-27 23:28：將 TE 對齊 START 自旋改以 TE 邊緣相位模型為基準（於預測完成前 400 us 喚醒並重新同步）。
    - `25a3a9d` 2026-10-28 00:36：TE 自旋支援邊緣加最短傳輸時間後的閒置判定、以 1.5x 平滑延遲提早喚醒，並拒絕邊緣 60 us 內的 START。
    - `ae792ba` 2026-10-28 02:06：TE 自旋偵測錯過 TE 的 START 並重排至下一邊緣，要求 500 us 餘裕並排除偏差 >500 us 的取樣。
    - `38abc8c` 2026-10-28 02:41：自旋執行緒在顯示路徑持有寫入權杖進行複製時不再觸發 START，修正被搶權杖還原與卡死看門狗。
    - `10abc48` 2026-10-28 05:14：opt-in 防撕裂週期中段 START（`fuxigpuncimport` bit 65536）：放寬 tear-check 起始門檻、軟體阻擋 TE 後前幾條線並自我驗證退回。
    - `f1dfb45` 2026-10-28 05:55：週期中段 START 在單次開機內依序嘗試多種開閘變體（`SYNC_THRESH`、`START_POS`、清除 tear-check enable）並失敗即還原。
    - `0a96638` 2026-10-28 06:36：將 `dsi_te`（每次窗內 START 清除 DSI `TRIG_CTRL` TE 位元）設為週期中段 START 首選變體並記錄首次讀回。
    - `b3125a9` 2026-10-28 07:11：opt-in pingpong autorefresh scanout（131072）：START 後重啟 AR、以 `SRC0` 翻頁追蹤鎖存位址並附多項退回機制。
    - `0828ddf` 2026-10-28 07:43：autorefresh 依序嘗試 frame count 0 與 1 及自適應佔空比門檻（因 AR 下 TE 計數不前進改用時鐘計時）。
    - `f933f7f` 2026-10-28 08:16：opt-in hard spin（262144）：提早喚醒與中斷切片連續完成輪詢；autorefresh 位元標記為勿啟用（do-not-enable）。
    - `4bc1340` 2026-10-28 08:48：hard spin v6：逐緩衝區 `copy_open` 保護、20 us `near_edge` 下限與細分 `spin_not_running` 原因。
    - `a5fc21a` 2026-10-28 09:27：常駐啟用每 600 次提交的 `submit_split` 報告（拆解 client/backend 階段、拒絕原因、快取清理量與鎖等待歸因）。
    - `1f28342` 2026-10-28 10:22：新增 `GPU_ONLY` import 旗標（`BoMap.reserved` bit 0）略過每次提交清理與退休無效化，並將被拒提交改為事件驅動等待。
    - `7479763` 2026-10-28 14:03：coherency 自測第二變體（write-back, inner-shareable 葉節點），通過時以此映射 import 並略過所有 import 維護（bit 64）。
    - `144de2d` 2026-10-28 14:00：透過 pmap modify bit 對 import 清理進行逐頁髒頁追蹤（`fuxigpuncimport` bit 524288，自測 fail-closed）。
    - `8973b60` 2026-10-28 14:15：記錄 zero-copy 判定；opt-in 於鎖外預先清理複製來源並加入 VIG0 調查（bit 20）。
    - `233f6c0` 2026-10-28 14:24：為核心建置宣告 `osfmk` pmap reference/modify 函式原型。
    - `84dd470` 2026-10-28 15:19：wb 模式一致性探測通過時將 present 複製來源映射為 write-back inner-shareable 並略過清理（關閉 bit 20）。
    - `d49884e` 2026-10-28 15:32：inline present 複製於 ABI 客戶端位址空間執行（`fuxigpuncimport` bit 21，需 512）：同 ring 排隊並帶獨立 fence，不阻塞或被 busy 拒絕。
    - `6166890` 2026-10-28 21:19：每 600 次複製記錄逐複製 GPU 時間戳與 `submit_to_start`/`poll_late` 拆解；bit 20 在一致性下作為強制 NC 複製來源的 A/B 開關。
    - `6b1b1e1` 2026-10-30 18:15：inline present 排序控制（`fuxigpuncimport` 8/16/32）：8 觸發 128 幀事件紀錄、16 copy priority（有到期 swap 寫入工作時拒收新 client 提交）、32 丟棄已被較新就緒 swap 取代的舊 swap。
    - `cea58ac` 2026-10-30 19:14：direct-DPU-scanout stage 1（bit 22）：接管時唯讀調查顯示 SMMU（stream `0x1c00`/`0x1c02`）與 VIG0 狀態。
    - `38ded11` 2026-10-30 19:40：opt-in GPU fence 中斷（`fuxigpuncimport` bit 26 = `67108864`）：將 SPI 300 / INTID 332 路由至 CPU 0，清除 `RBBM_INT_0` bit 20 並喚醒 `WaitFence`（附異常中斷自動遮罩保護）。
    - `82d06f1` 2026-10-30 20:08：copy priority park（bit 27 = `134217728`，需 16）：被 `copy_first` 延後的工作先建置與設 fence 但不按 doorbell 並立即返回，避免 FuxiMetal 後端鎖阻塞工作退休；由 `fuxi_gpu_copy` 緊接排入 doorbell。
    - `9565b45` 2026-10-30 20:31：direct-DPU-scanout stage 2（bits 22+23 = `12582912`）：在顯示用 `apps_smmu` bank 安裝 stage-1 分頁表（2–4 GiB 1 GiB 恆等映射 + `0x10000000` 32 MiB IOVA 窗），執行 60 次恆等 START 與 1125x2436 離散測試 surface 切換驗證後完整還原。
    - `4c374df` 2026-10-30 21:13：新增 `'SWEET FB: unshown paths'` 計數器細分未顯示原因、修正自旋執行緒背景啟動被誤算為 `'P'`，並加入 bit 28（`268435456`，搭 8）於首次未顯示突發時觸發 128 幀紀錄。
    - `b314d43` 2026-10-30 21:28：修復 X229 實機錯誤（`far=0xb8000100 fsr=0x402`）：因 `TCR2.SEP` 維持 bootloader 的 `0` 導致高位元被符號擴展進禁用的 TTBR1，改設 `SEP=7 | PASize`（`0x38061`）並新增 live 讀回與 fault walk 診斷。
    - `2ae1d27` 2026-10-30 23:26：direct-DPU-scanout stage 3（bit 24，需 22+23 與 `fuxiscantriple=1`，開機值 `502070904`）：建立 64 MiB 4-slot IOVA 窗，對單層 BGRA8 1125x2436 頁面齊對 IOBuffer surface 直接以 VIG0 `SRC0` 進行零複製硬體掃描輸出（失敗自動退回 A 幾何 GPU 複製）。
    - `e139cd9` 2026-10-31 08:46：IO-coherent 快取 BO 支援（`fuxigpuncimport` bit 29 = `536870912`，需 bit 64 自測 wb-ish 通過，開機值 `1038941816`；Turnip `HOST_CACHED` 與 FuxiMetal `FUXI_MTL_BUFFER_CACHED=1` 搭配）。
    - `70451f3` 2026-10-31 10:46：常駐節奏紀錄（200 字元換行續接、`SWEET FB: swapwait` 計數與 `job_stamps`）並加入 `fuxiearly` bit 2（admission retire）與 bit 4（present FIFO，需三重緩衝）。
    - `112fb5d` 2026-10-31 10:49：將每工作 GPU ring timestamps（12 個 ring word `CP_REG_TO_MEM`）移至 `fuxiearly` bit 8（值 8，預設關閉），關閉時 ring 與 `e139cd9` 逐位元組相同。
    - `c8650c1` 2026-11-06 23:17：`fuxiscancpuflip` CPU 雙緩衝（面板掃描 B/C、CA 保留 A、TE 閘控複製與翻頁）。
    - `979e2f0` 2026-11-06 23:34：`fuxiscancpuflip` CPU 雙緩衝更新（同日第二筆提交）。
- **復現**：見 README 共用步驟 1–3；依實驗目的傳入對應開機參數（如 `fuxigpuabi=1`、`fuxigpuncimport=...`、`fuxipanel120=1`、`fuxiscantriple=1`、`fuxiearly=...`）；console 輸出 `FUXI GPU ABI: NativeAdrenoGpuPublished v2 HostImport 16K, open 0x4147, root only, watchdog 100 ms\n`、`FUXI GPU CLOCK: ...`、`FUXI SCANOUT: ...` 等。
- **主機測試**：提交說明記載多項主機測試通過，包括 `test-fuxi-metal`、`test-fuxi-gpu-irq`、`dpu_stage3_tests`、HostImport/GPU/cache/SMMUv2 walk 模型、X229 重播、`gpu-k1`、`abi-perf`、`unixnu`、`gpu-scanout`、`MemoryTypeTests`、`TranslationPolicyTests`。
- **驗證紀錄**：實機觀察記載 `06dc4b4` 首次實機因誤判 `+0x2a8` 100 ms 後停止（已修）、`496f196` 前相關實測、X229 stage 2 `TCR2.SEP=0` 發生 L0 fault `0xb8000100`（已修）、X232 stage 2 60 次恆等 START 與 2741 散頁測試通過、Q222/R223 與 V251（102 Hz，目標 120 Hz）分析；而 `05613d3`/`038138b`/`3af1cfd`/`74642f1`/`e139cd9`/`112fb5d` 等當時各提交亦註明尚未實機觀察（Device effect not yet observed）。
- **參考來源**：UniXNU `NativeAdreno`（`b686b905` MIT）、UniXNU `NativeMetal`（`c6d29caa` MIT）、UniXNU superproject（`cb074994` BSD-2-Clause-Patent）、UniXNU `AdrenoHfiVotes.c`（BSD-2-Clause-Patent）、`ShengUfs`（`LICENSE.ShengUfs`）、`GopFramebuffer`（`LICENSE.GopFramebuffer`）、`ArmCpu`（`LICENSE.ArmCpu`）、Linux `arm-smmu`、`docs-lanes/gpu-k1.md`、`gpu-scanout.md`
- **結論／未完成**：涵蓋 Fuxi GPU ABI v2、120 Hz TE 掃描輸出與 DPU 直接掃描輸出核心演進，各項高階旗標多預設關閉以開機參數 opt-in；後續 `0100`、`0101`、`0102`、`0108`、`0114` 針對執行交換、快取同步、`backboardd` 開啟與獨立 scanout 補丁繼續延伸。
  - 基底 `979e2f08` 之後的本機提交（舊到新，箭頭後是記錄該修改的項目）：
    - `75148d2` 2026-11-14 19:45：base: 979e2f08 + harness patches (HEAD 2aae087)（本份是 `kernel-bo-slots.patch`、`kernel-ib-profile.patch`：BO 槽 4096、IB profile） → G-014、G-019、K-072
    - `3277d2c` 2026-11-14 20:02：IBPROF draw census (= harness f56f8a5 kernel-ibprof-draws.patch) → G-027、K-072
    - `eafc778` 2026-11-14 22:52：fuxibacklight=4: AppleARMBacklight service; the display owner sends DCS 0x51 between transfers → K-017
    - `ba65d38` 2026-11-16 07:02：fuxibacklight=4: an uncertain DCS command no longer halts brightness (DSI errors read/cleared as Linux dsi_error, 6 in a row halt); SweetBattery logs each published change; fuxi,adsp=4 test: tour drags the slider 8x and stages an unplugged 57% reading → G-048、K-005、K-018
    - `7af418a` 2026-11-16 07:43：fuxi: logdump console log for standalone boots; backlight DMA-done waits a full frame → G-048、K-018、K-019
- **出處**：`ksrc:xnu/patches/0098-fuxi-native-adreno-abi.patch`；上游提交 `19fbf5e`～`979e2f0`（共 68 筆，詳見演進清單）；本機提交 `75148d2`、`3277d2c`、`eafc778`、`ba65d38`、`7af418a`

### S-099 實作 WCN7850 PCIe0/MHI/BHI 探索、QMI WLFW 帶起與原生 ath12k Wi-Fi iOS 站台橋接（fuxiwlan）

- **日期**：2026-10-20 15:23～2026-10-27 09:59（本機提交至 2026-11-18 12:05）
- **狀態**：上游繼承（ios-fuxi）；ksrc：本機修改 +2816／-14（本機提交 `93201ca`、`51e7ca9`、`a066a05`、`723e740`、`e82f103`、`86cfb9d`、`42df429`、`15e6d0a`、`62dd9b0`、`d24fc49`、`e575a32`、`4b52021`、`d020ece`、`70470b9`、`54beb9b`、`d3f7631`、`21e2a91`、`8741795`、`69ea46c`、`aa89a3e`、`497a117`、`a23e701`、`490764d`、`a41069f`、`af3c264`）；本機修改見 K-011、K-015、K-024、K-028、K-032、K-037、K-041、K-042、K-043、K-044、K-045、K-049、K-050、K-051、K-052、K-053、K-054、K-055、K-056、K-057、K-058、K-060、K-061、K-062、K-066、K-067、K-068、K-069、K-073
- **開關**：
  - `fuxiwlan`：開機參數，啟用 WCN7850 M4–M6/W9–P17 階段式探索與電源／PCIe0／MHI／BHI/QMI 帶起（`1` 跑全部 28 步，`2..28` 停在指定步驟以便二分除錯；預設 `0` 關閉）。
  - `fuxiwlanoldiova`、`fuxiwlanclk`、`fuxiwlanicc`、`fuxiwlaniccbw`、`fuxiwlaniboff`、`fuxiwlanprobe`、`fuxiwlanaspm`、`fuxiwlancap`、`fuxiwlanbdf`、`fuxiwlanstop22`、`fuxiwlankeep`：開機參數，分別控制 M6g/M6f IOVA 池切換、PCIe fabric 時脈投票、M6i BCM 頻寬投票（預設 1）與 CNSS 頻寬列（0..8）、M6k BHI 探針（`unmapped`/`pattern`/`civac`/`nc`）、ASPM 保留、目標記憶體上限選項（0/1/2）、BDF 檔名覆蓋、第 22 步停止診斷與保留 DMA／裝置狀態。
  - `fuxiwlanseq`、`fuxiwlanrails`、`fuxiwlanqrtr`、`fuxiwlanmlo`、`fuxiwlanw15`、`fuxiwlanw16`、`fuxiwlanw18`、`fuxiwlandwell`、`fuxiwlancache`、`fuxiwlanath`：開機參數，分別控制 M6o 下電／重啟序列（`cycle`/`parf`/`dbi`/`eq`/`hostirq`/`msi`/`por`/`prefix`/`cyclerails`）、六路電源軌重投（1/2）、QRTR/WLFW 階段（1=W11..5=W15）、MLO 與 P17 探針位元遮罩（0x4..0x100）、W15/W16/W18 變體、協商掃描駐留與原生 ath12k 模組開關。
  - `fuxiwifiios`、`fuxiwifitest`、`fuxiwifitestssid`：開機參數，控制將原生 WCN7850 橋接至 iOS `wifid`/station 後端（Open/WPA2-PSK 連線、RX/TX、RSSI 回報）與測試模式（如 `=2` 假密碼測試、`=3` 僅列命名網路）。
  - `fuxiwlanbatch=1`：本機新增（`69ea46c`，見 K-062）；協商 dwell 掃描分批，loader 產生器 `--scan-batch` 帶入；預設 0（不分批）。
- **問題／現象**：XNU 下缺乏小米 13（WCN7850 / KIWI `17cb:1107`）的 PCIe0／MHI／BHI 驅動與 iOS Wi-Fi 介面，探索過程中依序遇到：cmd-db ACV width=0 導致 M6i 停止、RPMh TCS IRQ 被遮罩導致逾時、M6o PME_TURNOFF 自清位元與 L23_READY 未達導致中止、link down 讀 DBI 凍結、KIWI 僅支援 32-bit MSI、QMI WLFW 回覆缺少 board_info、ath12k `FW_READY_IND` 為 `0x38` 而非 ath11k 的 `0x21`、`WLAN_MODE` 逾時靜默、模組掃描紀錄截斷於 384 bytes 隱藏 RSN IE 導致 `wifid` 無法匹配 WPA2 網路等問題。
- **根因**：Apple 原廠 WLAN 驅動不支援 Qualcomm WCN7850 PCIe0/MHI/QMI 協議與 SM8550 RPMh/cmd-db/apps-SMMU；且固定 ath12k 模組在 XNU 需獨立的板級轉接器、DMA/kfree 契約對齊與完整 IE 回傳。
- **做法**：修改 `bsd/conf/files`、`bsd/dev/sweet_ufs.c`、`bsd/dev/fuxi_wlan.c`、`bsd/dev/wlan.c` 並新增 `bsd/dev/fuxi_wlan_arena.h`、`fuxi_wlan_ath12k.h`、`fuxi_wlan_ath12k.inc`、`fuxi_wlan_control.h`、`fuxi_wlan_ios.inc`（共 9 檔、8,657 行）。實作由 `wlan.c`（M3）至 PCIe0 iATU1 MEM 窗、BAR0 `0x60400000` 分配、owned apps-SMMU stage-1 轉換、RPMh BCM/regulator 投票、BHI/BHIe ELF/BDF 韌體載入、QRTR/WLFW 交握，以及本機接續完成的原生 `ath12k` 板級轉接器、iOS station 後端（authenticate/associate/4-way handshake/`nw_wifi_tx`/RX batching/RSSI_CHANGED）。
  - 演進（全部 42 筆上游提交）：
    - `b978770` 2026-10-20 15:23：加入受閘控的 XNU WCN7850 PCIe0/BAR0/MHI 探索（wlan M4，開機參數 `fuxiwlan`），重跑 M3 序列、配置 outbound MEM iATU 與 BAR0 `0x60400000` 並讀取 MHI/BHI 暫存器。
    - `2800fd5` 2026-10-20 16:16：加入受閘控的 WCN7850 BHI 與 BHIe 韌體帶起（steps 17..28）。
    - `caf3cac` 2026-10-20 18:47：為 WLAN DMA 加入 owned apps-SMMU 轉換（M6）。
    - `b34ab37` 2026-10-20 20:16：在 SMMU 設定前初始化 bypassed WLAN RC SID 表。
    - `ed8ddcc` 2026-10-20 21:26：在認領前探測 WLAN SMMU context banks 並具名回報寫入失敗。
    - `c1b8ba5` 2026-10-20 22:04：驗證 WLAN SMMU 可寫欄位並接受固定的 TBI 讀回。
    - `5143d7a` 2026-10-21 00:08：驗證 WLAN BHI 設定並區分 PBL 錯誤與傳輸中斷。
    - `748799f` 2026-10-21 09:33：驗證線上 WLAN BHI 映射並擷取有界 KIWI 開機日誌（PBL/SBL）。
    - `8e0d95a` 2026-10-21 11:04：將 WLAN DMA 窗與 SMMU 屬性對齊 CNSS。
    - `526124b` 2026-10-21 14:04：審計 M6h 上游 DMA 路由與 fabric。
    - `4e4aa91` 2026-10-21 14:16：為 M6i 從 cmd-db 投票 PCIe0 BCM 頻寬。
    - `133988b` 2026-10-21 14:31：在 M6i cmd-db 查詢中接受無 unit/width 的遮罩投票 BCM（實機 ACV width 0、VCD 12）。
    - `e10595a` 2026-10-21 15:15：序列化 M6j BCM 群組並診斷 RPMh 逾時。
    - `1c9d984` 2026-10-21 15:33：M6j BCM 請求改依 TCS idle 與每命令狀態（ISSUED|COMPLETED `0x10101`）完成，因 IRQ 被遮罩 `IRQ_STATUS` 維持 0。
    - `05622cb` 2026-10-21 15:49：將所有 RPMh 請求（含 regulator）改以 TCS idle 加每命令狀態判定完成。
    - `cd9a059` 2026-10-21 16:26：加入具鑑別力的 BHI DMA 與快取探針（`fuxiwlanprobe`）。
    - `73e158c` 2026-10-21 16:49：為 `bsd` M6k NC 探針宣告 `pmap_cache_attributes`/`pmap_set_cache_attributes`（修正 CI 編譯失敗）。
    - `09a8def` 2026-10-21 20:25：M6l 審計 BHI 前設定並加入可選主機實驗（`fuxiwlanpre`/`bhi`/`vec`）。
    - `89cd850` 2026-10-21 21:39：加入 M6m 原廠映像（`fuxiwlanfw=stock`）與 L1SS PBL 實驗。
    - `3359c14` 2026-10-22 18:54：審計小米 CNSS 開機並加入 opt-in M6n AOP PDC 設定（`fuxiwlancnss=pdc`）。
    - `a68eaf5` 2026-10-22 19:36：加入預設關閉的 M6o 下游電源循環序列實驗（`fuxiwlanseq`）。
    - `2fa18ed` 2026-10-22 20:21：M6o 循環接受會自我清除的 `PME_TURNOFF` 位元（實機讀回 0 導致第 17 步誤判中止）。
    - `6d0951d` 2026-10-22 21:28：當 `L23_READY`（PARF `PM_STTS` bit 5）於 100 ms 內未達時，M6o 循環比照小米 `pci-msm` 繼續下電。
    - `d6d719a` 2026-10-22 22:10：在預設 M6o 循環中保留共用電源軌（避免 `cyclerails` 凍結 SoC）。
    - `79419be` 2026-10-22 23:05：M6o 循環在 link down 期間禁止存取 PCIe0 `0x60000000-0x63ffffff`（實機於 PERST low 後讀 DBI Link Status 凍結）；link down 駐留 1 ms、link up 等待 PARF LTSSM L0。
    - `ae9becc` 2026-10-22 23:58：M6q 循環恢復時重新初始化 PCIe 核心與 PHY。
    - `fcd9309` 2026-10-23 00:51：於第 18 步具名記錄失敗的 M6o 上電階段（因第 18 步於 1 ms 內失敗 `0x126f` 無原因）。
    - `03949e3` 2026-10-23 01:41：接受 KIWI 的 32-bit MSI 能力（實機回報 cap `0x010a7005`，32 訊息、每向量遮罩、32-bit 定址）。
    - `95e34e5` 2026-10-23 02:45：診斷 W9 POR 中止並允許未知的初始 INTVEC。
    - `e769180` 2026-10-23 03:45：在 SBL READY 後繼續進行有界的 W10 AMSS 傳輸。
    - `6414df5` 2026-10-23 04:19：加入 opt-in W11 IPCR QRTR 能力探針（`fuxiwlanqrtr=1`）。
    - `ef4b643` 2026-10-23 05:12：加入 W12 WLFW 設定與目標記憶體交握（`fuxiwlanqrtr=2`）。
    - `89c100a` 2026-10-23 05:59：審計記憶體回應並加入有界 W13 小區塊協商（`fuxiwlanqrtr=3`）。
    - `3c84cad` 2026-10-23 06:51：加入 W14 主機下載與韌體就緒邊界（`fuxiwlanqrtr=4`）。
    - `5d9566d` 2026-10-23 08:05：當 WLFW capability 回覆不含 `board_info`（未燒錄 OTP）時繼續執行 W14。
    - `00045ae` 2026-10-23 11:30：W14 改為等待 ath12k 的 `FW_READY_IND`（`0x38`），而非 ath11k 的 `0x21`（實機只送 `0x38` 導致逾時）。
    - `edba3c4` 2026-10-23 11:39：加入 W15 WLFW INI/config/normal mode 里程碑（`fuxiwlanqrtr=5`）。
    - `6caece9` 2026-10-23 12:48：審計 W15 WLFW 位元組並在不寫入下擷取 `MODE` 逾時快照。
    - `a1172ad` 2026-10-23 12:59：加入 opt-in ath12k PHY 能力與 MLO 協商（`fuxiwlanmlo=1|2`）。
    - `1c9a3be` 2026-10-23 15:38：加入 opt-in W16 IPCR 容量與 doorbell 實驗（`fuxiwlanw16=1..4`）並補齊主機 ASan/UBSan 測試。
    - `6dc7bbf` 2026-10-23 16:33：加入 opt-in ON-only 六路電源軌重投與供電審計（`fuxiwlanrails=1|2`）。
    - `3779dfd` 2026-10-27 09:59：P17 opt-in `MODE` 靜默探針（`HOST_CAP` TLV `0x33`、MLO always、PHY `0x17`、DMA 對齊、sleep clock、錯誤解碼）。
- **復現**：見 README 共用步驟 1–3；依目的傳入 `fuxiwlan=1`（帶 `fuxiwlanqrtr=...`、`fuxiwlanmlo=...` 等）或原生 Wi-Fi 開機參數（`fuxiwifitest`、`fuxi,wifi-ux` 等）；console 輸出 `FUXI WLAN: BAR0 size %#x assigned ...`、`FUXI WLAN: MHI REGLEN ...`、`FUXI WLAN: BHI version ...`、`FUXI WLAN: W12 ...` 等。
- **主機測試**：卷宗記載 W16 與供電重投具備 production-C ASan/UBSan 主機測試；本機另持續擴充 WLAN 建置與單元檢查。
- **驗證紀錄**：上游實機觀察包含：M6i cmd-db ACV width 0/VCD 12、M6j TCS `CMD_STATUS 0x10101`/`IRQ_STATUS=0`、M6o `PME_TURNOFF` 自清與 `L23_READY` 未達、link down DBI 凍結、M6q L0 11 ms 後第 18 步 `0x126f` 失敗並確認 KIWI 32-bit MSI (`0x010a7005`)、W14 無 `board_info` 與 `FW_READY_IND 0x38`、W15 `WLAN_MODE` 逾時；本機後續已於實機完成掃描、WPA2-PSK 連線、四向握手與 DHCP（詳見工作區紀錄）。
- **參考來源**：Linux `v6.12`/`v6.18` `ath12k` (`mhi.c`、`wmi.c`、`qmi.c`)、`drivers/bus/mhi/common.h`、`sm8550.c`、`bcm-voter.c`、`icc-rpmh.c`、`cmd-db.c`、`tcs.h`、`rpmh-rsc.c`、`pci-msm`、OpenBSD `if_qwz_pci.c` / `itlwm`、小米 CNSS2 審計、`docs/wlan-m6i.md`、`docs-lanes/wlan-bt.md`
- **結論／未完成**：上游建構完整 WCN7850 PCIe0/SMMU/MHI/BHI/QMI 探索框架；本機在其上接通 `ath12k` 模組與 iOS `wifid` 站台連線並持續精進掃描與省電（5GHz/熱點/跨重啟自動加入等仍依工作區最新驗收為準）。
  - 基底 `979e2f08` 之後的本機提交（舊到新，箭頭後是記錄該修改的項目）：
    - `93201ca` 2026-11-16 11:26：Serialize ADSP and WLAN RPMh transactions and quarantine uncertain state → K-011
    - `51e7ca9` 2026-11-16 14:09：Add bounded RAM and peripheral checkpoints with W18 tracing → K-015、K-024、K-032、K-037、K-041
    - `a066a05` 2026-11-16 17:04：Guard WLAN secondary HOST_CAP and lower source-decoded torch deadline → K-028、K-042
    - `723e740` 2026-11-16 18:37：Add isolated SECONDARY host capability omission for firmware-ready comparison → K-043
    - `e82f103` 2026-11-16 19:37：Add opt-in large-chunk WLAN memory grant and record sleep clock device evidence → K-044
    - `86cfb9d` 2026-11-16 20:27：Summarize granted WLAN target memory after a WLFW timeout under the ERRDEC selector → K-045
    - `42df429` 2026-11-16 22:37：feat: add an opt-in Fuxi board adapter for the pinned ath12k module → K-049
    - `15e6d0a` 2026-11-16 23:25：fix: isolate native WLAN imports and match the kernel link platform → K-050、K-051
    - `62dd9b0` 2026-11-17 00:19：fix: attach the native WLAN control transport and verify scan replies → K-051
    - `d24fc49` 2026-11-17 00:31：fix: honor the module DMA kfree ownership contract → K-052
    - `e575a32` 2026-11-17 00:56：Connect native WCN7850 power and scan to the iOS station backend → K-053
    - `4b52021` 2026-11-17 01:00：Match native WLAN identity to the iOS device tree → K-053、K-054
    - `d020ece` 2026-11-17 02:00：Join open and WPA2-PSK networks from the native WCN7850 bridge with full scan elements → K-055
    - `70470b9` 2026-11-17 02:56：Let a UI test list only the named network (fuxiwifitest=3) so scripted taps find it → K-056
    - `54beb9b` 2026-11-17 07:18：Tell iOS how strongly the joined network is heard (RSSI_CHANGED every 5 s, noise floor) so the status bar shows Wi-Fi bars → K-057
    - `d3f7631` 2026-11-17 07:27：Rescan only the network's last channel before an old-scan join, every channel if it is not there → K-058
    - `21e2a91` 2026-11-17 10:31：Add opt-in negotiated scan dwell and bounded FAST scan reuse → K-060
    - `8741795` 2026-11-17 10:56：Bound native wifid caller tracing and report rejected scan parameters → K-061
    - `69ea46c` 2026-11-17 11:09：Batch negotiated dwell scans within existing module and bridge timeouts → K-062
    - `aa89a3e` 2026-11-17 23:16：Add fuxi,wifi-ux: report the asked radio power, pick access points by estimated rate, keep the link on a join of the same one → K-066
    - `497a117` 2026-11-18 00:02：Keep the country code, skip same-state power requests, time the switch and scans, read Wi-Fi switches from ADT fuxi,args → K-067、K-068
    - `a23e701` 2026-11-18 01:03：Bound implicit iOS Wi-Fi scans and add opt-in per-channel dwell groups → K-068
    - `490764d` 2026-11-18 01:09：Revert "Bound implicit iOS Wi-Fi scans and add opt-in per-channel dwell groups" → K-068
    - `a41069f` 2026-11-18 11:11：Recover native Wi-Fi scans and make advertised-channel scans opt-in → K-069
    - `af3c264` 2026-11-18 12:05：Prioritize Wi-Fi power off and batch native network receive → K-073
- **出處**：`ksrc:xnu/patches/0099-fuxi-wlan.patch`；上游提交 `b978770`～`3779dfd`（共 42 筆）；本機提交 `93201ca`～`af3c264`（共 25 筆）

### S-100 實作受閘控執行路徑代換測試路由與 NativeAdreno ABI 效能／除錯追蹤（sweetexecswap）

- **日期**：2026-10-20 15:35～2026-10-31 08:46
- **狀態**：上游繼承（ios-fuxi）；ksrc：上游原樣
- **開關**：
  - `sweetexecswap=<來源>:<目標>`：開機參數，於 SWEET 板級下僅代換單一指定路徑之執行檔名（不改 argv/env，抑制後期 `noexec` 檢查），供不改 root 映像直接測試 GPU ABI / Metal binary；預設關閉。
  - `fuxigpuwaitus=<微秒>`：開機參數，設定 `WaitFence` 輪詢週期微秒數（預設 `100`；`0` 還原為 `IOSleep(1)`）。
- **問題／現象**：需要無須重寫 rootfs 即能將測試 binary 路由至受測版本，同時 `NativeAdreno` ABI v2 在實機面臨多重延遲與容量瓶頸：`WaitFence` 固定 `IOSleep(1)` 拖慢每幀退休、16-entry/416-byte 舊限制與 16 KiB `OSObject` 上限（加入 1024-entry BO 表導致建置失敗）、大於 1 MiB 連續記憶體搜尋每次觸發 `zone_gc` 卡頓 50–58 ms、不對齊的 `HostImport` 無法映射、以及缺少鎖等待歸因與等待時間直方圖。
- **根因**：XNU `kern_exec.c` 無測試代換路由；`FuxiAdrenoUserClient.cpp` 初期缺少 handle 雜湊索引、大表外置配置、分頁/區段查詢與細粒度階段計時。
- **做法**：修改 `bsd/kern/kern_exec.c`、`bsd/kern/sweet_execswap.h`、`iokit/Kernel/arm/FuxiHandleIndex.h`、`iokit/Kernel/arm/FuxiAdrenoUserClient.cpp`（共 4 檔、675 行）。實作 `sweetexecswap` 單次路徑代換（受開機配額與鎖保護）、`NativeAdreno` ABI v2 的 `Method 7`（timestamp）與 `Method 8`（status）、`MaxBos=1024`（外置配置避開 16 KiB `OSObject` 限制）、768 MiB wired 上限與 512 import 表、`NativeAdrenoHandleIndex` 開放定址索引、`fuxigpuwaitus` 與 IRQ 喚醒、`submit_split`（每 600 次提交報告與鎖等待歸因）、`fence pending` 直方圖（每 600 次等待）、`GPU_ONLY` import、髒頁追蹤與 IO-coherent cached BO 支援。
  - 演進（全部 24 筆上游提交）：
    - `860570a` 2026-10-20 15:35：以受閘控的 `execswap` 與有界核心追蹤路由現有 GPU ABI 測試。
    - `bdf7711` 2026-10-20 18:33：修正 GPU ABI 診斷主機匹配並打包 `metal2e`。
    - `cd3adfb` 2026-10-20 20:25：透過 buffer subdescriptor 映射受管 GPU BO 並建置 `metal2h`。
    - `103e71f` 2026-10-20 22:43：加入原生 Metal 探勘（`IOAcceleratorES` metaclass）與 Turnip buffer 前端。
    - `eea1c7b` 2026-10-21 13:00：實作 `NativeAdreno` ABI 2 主機記憶體匯入（`HostImport`）。
    - `62d1aed` 2026-10-21 13:22：將私有 pmap 查詢移至 Mach C 輔助函式（保留 16K 轉換與 DEFAULT/WCOMB 檢查）。
    - `aa2c98f` 2026-10-23 01:45：加入 `NativeAdreno` timestamp（method 7）/status（method 8），提高 `MaxBos` 至 1024、wired 上限至 768 MiB、import 表至 512（K1）。
    - `039a3bc` 2026-10-23 02:01：將 BO 表配置移至 `FuxiAdrenoUserClient` 物件外（`initWithTask`/`free`），解決 1024 項超過 16 KiB `OSObject` 上限導致核心建置失敗。
    - `905d917` 2026-10-26 21:26：`WaitFence` 改為每 100 us 輪詢（`fuxigpuwaitus=N`，`0` 還原 `IOSleep(1)`）並每 600 次等待印出摘要。
    - `f7b0c56` 2026-10-27 08:45：為未對齊的 no-copy buffers 匯入外包頁（enclosing pages）並記錄 host-import 拒絕階段（stages 1..9）。
    - `8c1bd3b` 2026-10-27 08:56：降低 ABI submit/map/free/wait 核心成本（`BoMap` 每 16K 頁單次 pmap walk、`WaitFence` 釋放鎖睡眠等）並為每個 selector 計時。
    - `05613d3` 2026-10-27 09:43：加入快取重疊鏡像（per-4K bitmap）、區段執行 `BoImport`、`fuxigpuncimport` 位元遮罩（64 IO-coherent 自測、128 範圍 TLBIVAL）。
    - `4e30dbb` 2026-10-27 09:51：移植 Adreno 740 submit/ring 改進：handle index（`FuxiHandleIndex.h`）、128-IB 提交、fenced WPTR、opt-in stall recovery 與快照。
    - `3af1cfd` 2026-10-27 12:21：`BoMap` 不再等待 GPU 閒置（`fuxigpuncimport` 256 還原等待），將 alloc/map 子階段耗時輸出至獨立 `stages` 行。
    - `74642f1` 2026-10-27 12:28：opt-in 客戶端提交佇列（`fuxigpuncimport` 512，深度 4）：接續於執行中工作後，省去 TTBR 切換與 idle 等待。
    - `9efbaeb` 2026-10-27 16:00：加入 host import stage-4 探測、opt-in 唯讀 import（1024）與 1/64-tick scanout 複製輪詢（2048）。
    - `467c12a` 2026-10-27 16:54：唯讀 host import 以 `kIODirectionIn`（READ）wire 其核心別名，stage 5/8/9 日誌帶探針位元圖與 `kr`。
    - `49015ea` 2026-10-27 18:34：opt-in present hold（`fuxigpuncimport` 4096）：被拒 present 暫擋客戶端至其複製提交（4 ms 上限）。
    - `5b12759` 2026-10-27 21:59：僅於必要或 ≤1 MiB 時嘗試連續 BO 配置（避免大連續搜尋觸發 `zone_gc` 每次卡頓 50–58 ms）。
    - `a5fc21a` 2026-10-28 09:27：常駐啟用每 600 次提交的 `submit_split` 報告與 `lock_` 鎖等待歸因。
    - `1f28342` 2026-10-28 10:22：加入 `GPU_ONLY` import 旗標（`BoMap.reserved` bit 0）略過每次提交清理與退休無效化，被拒提交改採事件驅動喚醒。
    - `144de2d` 2026-10-28 14:00：透過 pmap modify bit 對 import 清理進行逐頁髒頁追蹤（`fuxigpuncimport` bit 524288，fail-closed）。
    - `38ded11` 2026-10-30 19:40：`WaitFence` 支援 `fuxigpuncimport` bit 26 GPU fence 中斷喚醒，並常駐統計 per-600-wait `fence pending` 直方圖。
    - `e139cd9` 2026-10-31 08:46：支援 IO-coherent cached BOs（`fuxigpuncimport` bit 29、`NativeAdrenoAllocCachedCoherent`、`kIOMapDefaultCache`）與 per-600-submit `cached BOs` 統計。
- **復現**：見 README 共用步驟 1–3；搭配 `fuxigpuabi=1`（及選配 `sweetexecswap=...`、`fuxigpuwaitus=...`、`fuxigpuncimport=...`）；console 輸出 `SWEET EXECSWAP: %s -> %s pid %d%s\n`、`FUXI GPU ABI: submit_split ...`、`FUXI ABI: fence pending per 600 waits ...`、`FUXI GPU ABI: cached BOs ...`。
- **主機測試**：提交說明記載 HostImport、GPU/backend/cache/mapping、Metal2h、`test-fuxi-metal`、`test-fuxi-gpu-irq`、`gpu-k1`、`abi-perf`、`MemoryTypeTests`、`TranslationPolicyTests` 等主機測試通過。
- **驗證紀錄**：實機量測顯示大於 1 MiB 連續 BO 搜尋曾造成 50–58 ms 卡頓（`5b12759` 已修）、`BoMap` 曾於 ~40% 抵達時平均等 1.75 ms / 最大 13 ms（`3af1cfd` 已修）；而 `05613d3`、`3af1cfd`、`74642f1`、`e139cd9` 當時提交均註明「Device effect not yet observed」。
- **參考來源**：UniXNU `NativeAdreno`（`NativeAdrenoHandleIndex.h`、`native_adreno_fence_pending`，MIT）、UniXNU-sheng（`[gpu pending]`，`BSD-2-Clause-Patent`）、`docs-lanes/gpu-k1.md`
- **結論／未完成**：完成 `FuxiAdrenoUserClient` ABI v2 的效能剖析、無等待映射／提交、中斷喚醒與 IO-coherent cached BO 核心端實作；預設高階旗標需 opt-in 啟用。
- **出處**：`ksrc:xnu/patches/0100-sweet-execswap-gpu-abi-trace.patch`；上游提交 `860570a`～`e139cd9`（共 24 筆）

### S-101 修復 4 GiB 以上實體位址進行 GPU BO 快取同步時的截斷問題

- **日期**：2026-10-20 20:15
- **狀態**：上游繼承（ios-fuxi）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：當實體記憶體位於 4 GiB 以上時，進行 GPU BO cache-sync 會因實體頁號（`ppnum_t`）左移位元寬度不足而發生高位元截斷回繞（wrap），進而同步到錯誤的實體位址或觸及未映射區域。
- **根因**：`ppnum_t` 為 32-bit 型別，在 `pmap_sync_page_attributes_phys` 中先左移頁偏移再轉為 64-bit 導致 4 GiB 以上 RAM 位址截斷；此外 GPU 保留區已被排除於 VM/physmap 之外，不可把該保留區 PA 直接送入實體快取輔助函式。
- **做法**：修改 `osfmk/arm/pmap/pmap.c` 中的 `pmap_sync_page_attributes_phys`（新增 16 行、刪除 1 行），在位移前先將 `ppnum_t` 擴展為 64-bit，且對位於 RAM/NC 視窗外的位址（如 GPU 保留區）避免送入實體快取清理，改為沿用其既有有界 NC 別名並於異常時記錄警告。
- **復現**：見 README 共用步驟 1–3；啟用 GPU ABI 與 BO 映射同步時自動生效，異常時 console 輸出 `FUXI GPU: cache sync outside RAM/NC windows: %#llx`。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：來源未記
- **結論／未完成**：修復 4 GiB 以上 PA 在 `pmap_sync_page_attributes_phys` 的位移截斷與保留區越界清理。
- **出處**：`ksrc:xnu/patches/0101-fuxi-gpu-nc-cache-sync.patch`；上游提交 `5c0b2da`

### S-102 允許 backboardd 在指定開機參數下開啟 NativeAdreno User Client（fuxibbgpu）

- **日期**：2026-10-22 21:28～2026-10-23 00:00
- **狀態**：上游繼承（ios-fuxi）；ksrc：上游原樣
- **開關**：
  - `fuxibbgpu=<值>`：開機參數，允許以 `mobile` 身分執行的平台執行檔 `backboardd` 為自身開啟 `FuxiAdrenoUserClient`（`1` 為正常啟用，`2` 為 M9l survey 模式；預設關閉，維持僅限 root）。
- **問題／現象**：iOS 渲染伺服器 `backboardd` 以 `mobile` 使用者身分執行，而 `FuxiAdrenoUserClient` 預設僅允許管理員（root）開啟，導致 `backboardd` 無法使用 Adreno GPU；且初版以 `fuxigpu=backboardd` 啟用會與核心 GPU 驅動開關 `fuxigpu=1` 衝突並被 FuxiMetal 雙鍵閘門誤判拒絕。
- **根因**：`NativeAdreno` user client 原始設計為 root-only，需在不開放一般 non-root 程式的前提下，單獨為 `backboardd` 放行並搭配獨立開機參數。
- **做法**：修改 `iokit/Kernel/arm/FuxiAdrenoUserClient.cpp`（新增 22 行、刪除 1 行），檢查開機參數 `fuxibbgpu=1` 或 `2`，並驗證呼叫者為名為 `backboardd` 的 platform binary 且為自身開啟時放行，同時記錄每次決策。
  - 演進：
    - `0ed462b` 2026-10-22 21:28：當開機參數要求時允許 `backboardd` 開啟 Adreno user client 並記錄決策。
    - `c20d15f` 2026-10-22 21:30：改用獨立開機參數 `fuxibbgpu=1`，避免與 `fuxigpu=1` 衝突。
    - `6ee7d4c` 2026-10-23 00:00：補齊 `fuxibbgpu=2`（M9l survey 模式）的放行判定。
- **復現**：見 README 共用步驟 1–3；開機參數帶 `fuxigpu=1 fuxigpuabi=1 fuxibbgpu=1`（或 `2`）；console 輸出 `FUXI GPU ABI: non-root open backboardd pid %d platform %d -> %s\n`。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：來源未記
- **結論／未完成**：完成 `backboardd` 受控存取 `FuxiAdrenoUserClient` 的核心放行閘門，預設關閉。
- **出處**：`ksrc:xnu/patches/0102-fuxi-gpu-backboardd-open.patch`；上游提交 `0ed462b`、`c20d15f`、`6ee7d4c`

### S-103 實作 Fuxi H1 AMFI 相容層、按鍵仲裁／注入／自動鎖屏、UI 腳本導覽與 PMK8550 RTC 校時

- **日期**：2026-10-23 02:17～2026-11-06 18:16（本機提交至 2026-11-17 02:09）
- **狀態**：上游繼承（ios-fuxi）；ksrc：本機修改 +109／-30（本機提交 `75148d2`、`6fc377f`、`7928af8`、`ba65d38`、`aab893e`、`1836f06`、`406f096`）；本機修改見 G-027、G-028、G-048、K-005、K-017、K-018、K-035、K-036、K-072
- **開關**：
  - `fuxiamfi`：控制 H1 AMFI 相容層（為目標執行環境提供 container 路徑與 `CS_SIGNED | CS_VALID` 標記；設為 `0` 可停用）。
  - `fuxikeyinject=1`、`debug.fuxikeys.inject`（可寫 sysctl，僅 root）、`debug.fuxikeys.held`（唯讀 sysctl）：開機參數與 sysctl，經仲裁器注入電源／音量鍵（按下最長 10 秒自動釋放）並回報 HID/PMIC/注入/tap-to-wake 按住狀態。
  - `fuxitaptowake=1`、`fuxiautolock=<秒>`、`fuxitbatch=1`：開機參數，分別啟用螢幕關閉且 `TouchDetectionMode == 0xfe` 時的輕觸喚醒（並吞掉該次觸控）、無操作後自動按電源鍵鎖屏（10..3600 秒）、與 UniXNU `NativeHidInput` 整批觸控集合回報；皆預設關閉。
  - `fuxikeyscript=<秒>` / `fuxikeyscriptn=<步數>`、`fuxitouchscript=<秒>` / `fuxitouchscriptn=<次數>`、`fuxitour=<秒>` / `fuxitourhold=<秒>` / `fuxitourg=<Y>` / `fuxitoura=<Y>`、`fuxitaps=<秒>` / `fuxitapcmd=<指令>` / `fuxitapsloop=<次數>`、`fuxi,tapcmd`（ADT）：開機自動化測試按鍵、觸控滑動、截圖導覽與明確座標指令（如 `@1`、`@3`）。
  - `fuxirtc=<0|1>`、`fuxirtcoffset=<秒>`、`fuxi,rtc-offset`（ADT `/chosen` 4-byte LE）：控制開機讀取 PMK8550 RTC（`rtc@6100`）加校時偏移設定日曆時鐘（boot-arg 優先於 ADT）。
- **問題／現象**：iOS userland 服務需有效 `CS_VALID`/container 查詢，PMIC 按鍵需避免多來源（PMIC/注入/tap-to-wake）造成卡鍵，iOS 首次亮屏後約 30 秒自動熄屏但電源鍵喚醒後不再自動鎖屏，自動化遠端測試需要穩定的觸控／截圖／亮度滑桿操作腳本，且小米 13 開機缺少正確 UTC 日曆時間。
- **根因**：XNU 原樣缺乏 Fuxi H1 AMFI 補齊、統一的按鍵狀態仲裁器、自動化 HID 腳本驅動以及 PMK8550 SPMI RTC 讀取＋Android 校時偏移套用。
- **做法**：修改 `bsd/kern/sweet_amfi.c`、`bsd/dev/fuxi_keys.c`、`bsd/dev/sweet_keys.c`、`bsd/dev/sweet_spmi.c`、`bsd/dev/fuxi_touch.c`、`iokit/Kernel/arm/hid/SweetHID.cpp` 並新增 `iokit/Kernel/arm/hid/LICENSE.NativeHidInput`、`bsd/kern/fuxi_amfi.h`、`bsd/dev/fuxi_key_state.h`、`bsd/dev/sweet_spmi_bus.h`（共 10 檔、1,797 行）。所有按鍵經單一 `fuxi_key_state.h` 仲裁器送入 HID；`SweetHID` 記錄 `TouchDetectionMode` 供 `fuxitaptowake` 判斷；`fuxitbatch=1` 以單一 digitizer collection 送出完整 ID 集合並避免誤判掌壓；SPMI 讀取 PMK8550 `0x6148` 加上 `fuxirtcoffset` 或 ADT `fuxi,rtc-offset` 設定系統時間（含進位防倒退檢查）。
  - 演進（全部 20 筆上游提交）：
    - `5ac8af3` 2026-10-23 02:17：加入 H1 AMFI 相容層與強健的 PMIC 按鍵處理。
    - `4614c93` 2026-10-23 10:41：實作按鍵時間戳、單一按鍵仲裁器、受閘控的 `debug.fuxikeys.inject` 與 `fuxitaptowake=1`（`TouchDetectionMode 0xfe`）。
    - `1fb59ae` 2026-10-23 17:24：加入 `fuxikeyscript=S` 開機按鍵測試腳本（Power×2、VolUp×2、VolDown×1）。
    - `6be04e9` 2026-10-23 17:32：於開機按鍵腳本前預先宣告 `fk_inject`。
    - `85a816a` 2026-10-23 20:09：加入 `fuxikeyscriptn=N` 僅執行前 N 步（`N=1` 測自動鎖屏）。
    - `2a0d071` 2026-10-24 08:28：加入 opt-in `fuxiautolock=S`，於首次熄屏重新亮屏後在無輸入 S 秒自動按電源鍵鎖屏。
    - `34836ab` 2026-10-24 22:49：加入 opt-in UniXNU `NativeHidInput` 觸控集合批次（`fuxitbatch=1`）與各模式接觸計數器。
    - `15ec6ac` 2026-10-25 10:57：加入 opt-in `fuxitouchscript=S` 固定手勢負載（解鎖／主畫面滑動／Spotlight／控制中心）供無人在場比較時序。
    - `fb8c5cb` 2026-10-26 09:55：為 `fuxitour` 重新產生 H1 overlay 補丁。
    - `a4c9e3c` 2026-10-26 10:28：修正 `fuxitour` 進入「設定」前先連按兩次主畫面手勢回到第 1 頁（實機 Today 滑動未註冊導致點到空白格）。
    - `d167dd0` 2026-10-26 10:56：修正 `fuxitour` 開啟「設定」後點擊兩次返回鍵（因「設定」會恢復上次頁面）。
    - `0c948c0` 2026-10-27 20:14：截圖導覽（`fuxitour`）新增造訪 App 資料庫（抵達、8 秒閒置、30 秒閒置）。
    - `391a7fb` 2026-11-05 16:48：加入 `fuxitaps`/`fuxitapcmd` 明確點擊與滑動指令腳本。
    - `e035697` 2026-11-05 18:27：加入 `fuxidiag` 程序／執行緒傾印以診斷卡死 App。
    - `2888d47` 2026-11-05 18:42：修正 `fuxidiag` 以符合 `xnu-8796` API 編譯。
    - `3fa9353` 2026-11-06 00:27：移除未完成的 `fuxidiag` 探針讓核心重新編譯通過。
    - `5482bec` 2026-11-06 13:16：開機讀取 PMK8550 RTC 並設定系統日曆時鐘。
    - `4a2bee2` 2026-11-06 13:50：RTC epoch 改為硬體計數器加上 `fuxirtcoffset` 開機參數。
    - `1c5286c` 2026-11-06 16:14：加入 `fuxitapsloop` 開機參數重複執行 tap 腳本以利長時間動畫量測。
    - `23c2363` 2026-11-06 18:16：AMFI 將已簽署 root binary 標記 `CS_VALID`，使 `csops` entitlement 查詢成功。
- **復現**：見 README 共用步驟 1–3；依測試需求搭配 `fuxitaptowake=1`、`fuxitbatch=1`、`fuxiautolock=...`、`fuxitaps=... fuxitapcmd=...`、`fuxirtcoffset=...` 或 ADT `fuxi,rtc-offset`；console 輸出 `FUXI RTC: clock set, now=%llu.%06u\n`、`FUXI KEYS: tap-to-wake power click (touch mode %#x)\n`、`FUXI TOUCH: report collections enabled (fuxitbatch=1)\n`、`FUXI TAPS: done, %u shots, %u loops\n`。
- **主機測試**：提交 `4614c93` 記載按鍵仲裁器、時間戳、指令字、輕觸策略與真實 sysctl/inject/tap 主機測試通過；`34836ab` 記載 ASan/UBSan 下 2,000 次隨機觸控集合、配置失敗重試、十指與 50 項 Linux 主機腳本通過。
- **驗證紀錄**：實機觀察到 `fuxitour` 初版於 Today 頁滑動未註冊及「設定」記住上次頁面（`a4c9e3c`/`d167dd0` 已修）；本機後續驗證控制中心亮度滑桿拖曳 8 次及 ADT `/chosen fuxi,rtc-offset` 校時。
- **參考來源**：HackiOS（`hackios-xnu-userland/userland/src/inject.c` MIT、`hackios-xnu-kernel/bsd/dev/redwood_buttons.c` APSL 2.0）、UniXNU `NativeHidInput`（`NativeHidInput.cpp` `75efe1c`..`ba03e5d`，`LICENSE.NativeHidInput` BSD-2-Clause-Patent）、Linux `rtc-pm8xxx.c`（`pmk8350_regs`）
- **結論／未完成**：完成 H1 AMFI、按鍵仲裁與測試腳本、觸控批次與 PMK8550 RTC 校時整合；`fuxidiag` 已於上游移除。
  - 基底 `979e2f08` 之後的本機提交（舊到新，箭頭後是記錄該修改的項目）：
    - `75148d2` 2026-11-14 19:45：base: 979e2f08 + harness patches (HEAD 2aae087)（本份是 `kernel-taps-presets.patch`、`kernel-taps-page1/2.patch`、`kernel-swipe-pacing.patch`：tapcmd 預設巨集與 8 ms 拖曳節拍） → G-027、G-028、K-072
    - `6fc377f` 2026-11-14 21:29：taps preset @3: Settings from home page 1 → K-035
    - `7928af8` 2026-11-16 03:02：fuxitour: drag the Control Center brightness slider down and up (shots brightness-low / brightness-high) so fuxibacklight=4 boots log slider levels → K-017、K-035
    - `ba65d38` 2026-11-16 07:02：fuxibacklight=4: an uncertain DCS command no longer halts brightness (DSI errors read/cleared as Linux dsi_error, 6 in a row halt); SweetBattery logs each published change; fuxi,adsp=4 test: tour drags the slider 8x and stages an unplugged 57% reading → G-048、K-005、K-018
    - `aab893e` 2026-11-16 13:14：Read bounded device UI test commands from ADT → K-035
    - `1836f06` 2026-11-16 18:39：Synchronize H1 overlay with verified RTC rollover fault handling → K-036
    - `406f096` 2026-11-17 02:09：Read the calibrated RTC offset from ADT /chosen when the boot argument is absent → K-036
- **出處**：`ksrc:xnu/patches/0103-fuxi-kernel-h1.patch`；上游提交 `5ac8af3`～`23c2363`（共 20 筆）；本機提交 `75148d2`～`406f096`（共 7 筆）

### S-104 實作 iOS Apple80211／IONetworkStack Wi-Fi 模擬與原生 WCN7850 站台對接層（fuxiwifiios）

- **日期**：2026-10-23 10:39（本機提交至 2026-11-18 12:05）
- **狀態**：上游繼承（ios-fuxi）；ksrc：本機修改 +557／-37（本機提交 `e575a32`、`54beb9b`、`31241b2`、`21e2a91`、`8741795`、`05ed0fa`、`aa89a3e`、`497a117`、`a23e701`、`490764d`、`a41069f`、`af3c264`）；本機修改見 K-033、K-053、K-057、K-059、K-060、K-061、K-064、K-065、K-066、K-067、K-068、K-069、K-073
- **開關**：
  - `fuxiwifiios`：開機參數，啟用 `FuxiNetworkStack` / `FuxiWLAN` 與 Apple80211 IOCTL/事件橋接層（預設關閉）。
  - `fuxiwifitrace`：開機參數，控制 `wifid` 請求與事件追蹤記錄。
  - `fuxi,wifi-rssi`（ADT `/chosen` u32）：設為 `1` 時回答 `APPLE80211_IOC_RSSI_BOUNDS (106)`（version 1、`-128/127`、floor `-128 dBm`），避免 `wifid` 維持 0 dBm 門檻拒收所有負 dBm 網路；預設關閉。
  - `fuxi,wifi-ux`（ADT `/chosen`）：位元旗標，控制回報請求電力、依估算速率選 AP、同網路重連保鏈路、保留國家碼與跳過同狀態電源請求等；預設關閉。
- **問題／現象**：iOS `wifid` 與系統設定需要標準 `IO80211` / `IONetworkStack` 與 `Apple80211` IOCTL 介面才能發現並管理 Wi-Fi；若缺乏 `RSSI_BOUNDS` 會因門檻為 0 出現 `"Below minimum RSSI threshold"` 拒連、缺乏 `RSSI_CHANGED` 通知會導致狀態欄無訊號格、缺乏 `aks_wrap_key`/`aks_unwrap_key` 會使 `securityd` 無法儲存已連網路密碼。
- **根因**：XNU 原樣缺乏 Qualcomm WCN7850 對應的 Apple80211 驅動與 `IO80211` 使用者空間契約實作。
- **做法**：修改 `bsd/conf/files`、`iokit/conf/files.arm64`、`bsd/kern/bsd_init.c` 並新增 `FuxiNetworkStack.cpp`、`FuxiWLAN.cpp`、`FuxiWiFi.LICENSE.HackiOS`、`fuxi_wifi_apple80211.[ch]`、`fuxi_wifi_ios.[ch]`、`fuxi_wifi_net.[ch]`、`fuxi_wifi_sta.[ch]`、`fuxi_wifi_wpa.[ch]`（共 16 檔、3,592 行）。
  - 上游從 HackiOS 移植 `fuxiwifiios` 合成後端；本機後續實作與 `fuxi_wlan` 原生站台對接（電源／掃描／WPA2-PSK 連線）、每 5 秒 `RSSI_CHANGED` 上報、`RSSI_BOUNDS (106)`、`fuxi,wifi-ux` 國家碼／速率／電源請求最佳化、以及 AKS keychain wrap/unwrap 支援。
- **復現**：見 README 共用步驟 1–3；開機參數帶 `fuxiwifiios=1`（及選配 ADT `fuxi,wifi-rssi=1`、`fuxi,wifi-ux`）；console 輸出 `FUXI WIFI IOS: ux options ...`、`FUXI WIFI IOS: apple80211: RSSI_CHANGED ...`、`FUXI WIFI IOS: apple80211: in this run: ...`。
- **主機測試**：無（本機另有跨檔 Wi-Fi 橋接與掃描單元檢查）
- **驗證紀錄**：上游提交註明 16.7 payloads UNVERIFIED；本機後續已完成實機掃描、WPA2-PSK 連線、訊號格顯示與密碼保存驗證（詳見工作區紀錄）。
- **參考來源**：HackiOS（`FuxiWiFi.LICENSE.HackiOS`）、UniXNU `zizhan`（`RSSI_BOUNDS` 修復，g331 紀錄）、`IO80211` 20H392（16.5）結構佈局、`apple80211_var.h`、`docs-lanes/wifi-ios.md`
- **結論／未完成**：完成 iOS `wifid` 與原生 WCN7850 站台之完整 Apple80211 橋接層（預設 opt-in）。
  - 基底 `979e2f08` 之後的本機提交（舊到新，箭頭後是記錄該修改的項目）：
    - `e575a32` 2026-11-17 00:56：Connect native WCN7850 power and scan to the iOS station backend → K-053
    - `54beb9b` 2026-11-17 07:18：Tell iOS how strongly the joined network is heard (RSSI_CHANGED every 5 s, noise floor) so the status bar shows Wi-Fi bars → K-057
    - `31241b2` 2026-11-17 07:56：Wrap and unwrap keychain item keys in the AKS stand-in so securityd can store a joined network's password, and say whether RSSI_CHANGED events reach wifid → K-059
    - `21e2a91` 2026-11-17 10:31：Add opt-in negotiated scan dwell and bounded FAST scan reuse → K-060
    - `8741795` 2026-11-17 10:56：Bound native wifid caller tracing and report rejected scan parameters → K-061
    - `05ed0fa` 2026-11-17 22:17：Answer RSSI_BOUNDS, add DIRECT_PLAY amplitude and a quiet boot console → K-033、K-064、K-065
    - `aa89a3e` 2026-11-17 23:16：Add fuxi,wifi-ux: report the asked radio power, pick access points by estimated rate, keep the link on a join of the same one → K-066
    - `497a117` 2026-11-18 00:02：Keep the country code, skip same-state power requests, time the switch and scans, read Wi-Fi switches from ADT fuxi,args → K-067、K-068
    - `a23e701` 2026-11-18 01:03：Bound implicit iOS Wi-Fi scans and add opt-in per-channel dwell groups → K-068
    - `490764d` 2026-11-18 01:09：Revert "Bound implicit iOS Wi-Fi scans and add opt-in per-channel dwell groups" → K-068
    - `a41069f` 2026-11-18 11:11：Recover native Wi-Fi scans and make advertised-channel scans opt-in → K-069
    - `af3c264` 2026-11-18 12:05：Prioritize Wi-Fi power off and batch native network receive → K-073
- **出處**：`ksrc:xnu/patches/0104-fuxi-wifi-ios.patch`；上游提交 `c900ca1`；本機提交 `e575a32`～`af3c264`（共 12 筆）

### S-105 實作 CPU 負載／IPI／Idle L1 快取清理診斷、禁用系統睡眠與 xpcproxy 身份保留（fuxicpuload／fuxinosleep／fuxixpcid／fuxinoidleflush）

- **日期**：2026-10-23 10:40～2026-10-23 12:49
- **狀態**：上游繼承（ios-fuxi）；ksrc：上游原樣
- **開關**：
  - `fuxicpuload=<秒>`：開機參數，每 `N` 秒（1..3600）輸出逐核 CPU 忙碌/user/sys 與 IRQ/IPI/timer 速率，並附 `ipirx`（IPI 來源分類）、`idleflush`（WFI 次數與 flush 耗時）與 `ipiwake`（排程器喚醒配對排行）；預設 `0` 關閉。
  - `fuxinoidleflush=1`：開機參數，在 `cpu_idle()` 僅執行 WFI 時略過非一致性 `platform_cache_idle_enter()` 全 L1 D-cache set/way 清理無效化；預設關閉。
  - `fuxinosleep=1`：開機參數，讓 `IOPMrootDomain::checkSystemSleepAllowed` 拒絕所有系統睡眠請求（避免熄屏後 `powerd` 觸發 `IOCPUSleepKernel` 卡死）；預設關閉。
  - `fuxixpcid=1`：開機參數，於 `xpcproxy` 原地 `exec` 真實 daemon 時保留 `pidversion`，避免 `launchd` 誤認新實例而重複啟動；預設關閉。
- **問題／現象**：系統缺乏逐核 IDLE 熱源與 IPI 喚醒歸因數據；SWEET 未定義 `__ARM_COHERENT_IO__` 導致每次 WFI 前均執行昂貴的 L1 set/way 清理；熄屏後 `powerd` 要求睡眠會造成核心卡死；且 `xpcproxy` `exec` 改變 `pidversion` 會觸發 `launchd` audit token 不符。
- **根因**：XNU 原樣缺乏 SM8550 待機／熱源診斷鉤子與 HackiOS 針對睡眠／`xpcproxy` 的相容調整。
- **做法**：修改 `bsd/kern/kern_exec.c`、`iokit/Kernel/IOPMrootDomain.cpp`、`osfmk/arm/cpu_common.c`、`osfmk/arm64/cpu.c`、`osfmk/arm64/sleh.c`、`osfmk/conf/files.arm64`、`osfmk/kern/sched_prim.c`、`osfmk/arm64/fuxi_idle_logic.h`、`osfmk/arm64/fuxi_idle.c`（共 9 檔、820 行）。
  - 演進：
    - `dad318c` 2026-10-23 10:40：加入 0105 `fuxicpuload=N`、`fuxinosleep=1`、`fuxixpcid=1` 診斷與修復（自 HackiOS 移植）。
    - `187e935` 2026-10-23 12:49：新增 `fuxinoidleflush=1` 與 `ipirx` / `idleflush` / `ipiwake` 三行診斷輸出及補丁同步腳本。
- **復現**：見 README 共用步驟 1–3；依需求帶入 `fuxicpuload=10`、`fuxinoidleflush=1`、`fuxinosleep=1`、`fuxixpcid=1`；console 輸出 `FUXI IDLE: cpuload ...`、`FUXI IDLE: ipirx ...`、`FUXI IDLE: idleflush ...`、`FUXI IDLE: ipiwake ...`、`FUXI IDLE: system sleep refused (fuxinosleep=1)`、`FUXI IDLE: xpcproxy pid ...`。
- **主機測試**：`tools/test-fuxi-idle.py`（涵蓋快照、閘門、格式化器與新鉤子檢查）通過。
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：HackiOS（`hackios-xnu-kernel`：`redwood_cpufreq.c`、`redwood.patch`，APSL 2.0）、`docs-lanes/cooling.md`
- **結論／未完成**：完成待機／熱源／IPI 診斷與睡眠／`xpcproxy` 保護（皆以開機參數 opt-in，預設關閉）。
- **出處**：`ksrc:xnu/patches/0105-fuxi-idle.patch`；上游提交 `dad318c`、`187e935`

### S-106 整合 UniXNU 顯示／UFS 安全策略、120 Hz TE 同步、掃描輸出追蹤與 mtdblk 獨立紀錄（fx_log）

- **日期**：2026-10-23 10:39～2026-11-06 15:38（本機提交至 2026-11-17 22:17）
- **狀態**：上游繼承（ios-fuxi）；ksrc：本機修改 +396／-42（本機提交 `d5c44c3`、`7af418a`、`d96adda`、`05ed0fa`）；本機修改見 G-048、K-001、K-018、K-019、K-033、K-064、K-065
- **開關**：
  - `fuxi_unixnu_ufs`、`ufslogsecs`：開機參數，控制 UniXNU UFS 防護完成與致命／逾時隔離（quarantine）政策。
  - `fuxipanel120`、`sweetfb_hz`、`sweetfb_noconsole`、`fuxifbprio`、`fuxipresent`、`fuxi_unixnu_diff`、`fuxi_unixnu_idlefb`、`fuxivsyncte`、`fuxivsynctoff`：開機參數，分別控制 120 Hz 心跳、更新率上限、停用 console 以啟用 scanout、present timer 高優先序、per-frame trace、差分陰影複製、按需 60 Hz 軟體計時器與 TE 相位鎖定。
  - `fuxigpuscanout`、`fuxigpuscanoutcheck`、`fuxigpuscanoutlog`、`fuxigpuncimport`、`fuxiearly`：開機參數，控制 GPU scanout、階段檢查／每幀日誌、128 幀事件紀錄（bit 8/28）、copy priority（bit 16/27）與 `fuxiearly`（bit 2 admission retire、bit 4 present FIFO）。
  - `fuxiuniondir`：開機參數，啟用 UniXNU union lower-directory 查詢（`0110` 整合）。
  - `fuxibacklight`：開機參數，於 `AppleCLCD` 下發佈背光服務（`1..3` 觀察與通知實驗、`4` `AppleARMBacklight` + DCS `0x51`）。
  - `fuxilog`、`fuxi,log`（ADT `/chosen`）：啟用 `fx_log` 每 10 秒將 console 環狀緩衝寫入 `mtdblk`（LUN 0、32 MiB 前 2049 blocks）供獨立開機拉取。
  - `fuxi,refresh-mhz`、`fuxi,swap-hist`（ADT `disp0`）：設定實測面板刷新率（mHz，如 `120800`）與 `SwapEnd` 間隔直方圖。
- **問題／現象**：`SweetFramebuffer` 缺乏 120 Hz 面板心跳與 TE 相位鎖定、GOP 裝置記憶體複製慢、缺乏 UFS 異常隔離與獨立開機持久化紀錄（小米 13 無 `logdump` 分區）；此外 128-entry inline 陣列曾使 `AppleCLCD` 超過 16 KiB `kalloc_type` 靜態斷言導致 CI 建置失敗，以及 240 字元以上 console 單行遭截斷。
- **根因**：Apple 原廠 `IOMFB` 為閉源；XNU 原樣 `SweetFramebuffer` 僅支援 60 Hz 自由計時器與純同步複製，且不具備 UFS 錯誤隔離或無 USB 時的日誌落地路徑。
- **做法**：修改 `bsd/dev/sweet_ufs.c`、`iokit/Kernel/arm/SweetFramebuffer.cpp` 並新增 `fuxi_unixnu_lookup.h`、`fuxi_unixnu_ufs.h`、`fuxi_unixnu_copy.h`、`fuxi_unixnu_display.h` 與授權檔 `LICENSE.NativeUfs`、`LICENSE.UniXNU`、`LICENSE.NativeMobileDisplay`（共 9 檔、2,011 行）。整合 UniXNU UFS 防護、64-byte 差分複製與 demand timer、`IOMFBDisplayRefresh` 120 Hz、TE 相位鎖定、`SweetFramebuffer` scanout 排隊／`fuxiearly` bits 2/4／`unshown paths`／200 字元自動換行續接；本機補入 `modem_a` 韌體讀取、背光 `CMD_DMA_DONE` 等待 20 ms、與 `fx_log` 改寫入 `mtdblk`。
  - 演進（全部 29 筆上游提交，部分提交於卷宗中與相關補丁共用說明）：
    - `1c74fa7` 2026-10-23 10:39：移植 opt-in UniXNU 顯示與 UFS 安全策略並審計其餘驅動。
    - `0c7a34c` 2026-10-23 13:33：加入 opt-in 有 fence 的合成幀 GPU 掃描輸出。
    - `7572f50` 2026-10-23 19:45：於 `fuxibacklight=1` 下發佈觀察用背光 IOService。
    - `43ea647` 2026-10-23 20:57：背光服務加入 opt-in 邏輯電源興趣通知。
    - `31ec41f` 2026-10-23 22:04：背光模式 3 隔離 ON 通知以釐清電源策略。
    - `4c26c3e` 2026-10-24 22:50：移植可選 UniXNU GPU handoff（`fuxigpuncimport=1`）、present 高優先序（`fuxifbprio=1`）與 union lookup（`fuxiuniondir=1`）。
    - `86b9a2e` 2026-10-25 22:31：opt-in 將 Fuxi 合成器 vsync 相位鎖定至面板 TE。
    - `4acbbf5` 2026-10-26 19:01：跨 TE 週期雙緩衝 opt-in GPU scanout。
    - `068faa4` 2026-10-26 20:03：僅在 `fuxigpuscanoutlog=1` 時輸出每幀複製日誌，保留 120 次摘要。
    - `7a0decf` 2026-10-26 22:55：於 commit 時預留 scanout surface 以釋放 CA scheduled waits。
    - `a72b285` 2026-10-27 01:27：佇列化 scanout swap 並在寫入端退休時喚醒呈現。
    - `fffb7a4` 2026-10-27 02:04：直接於喚醒時執行呈現並量測 scanout 階段延遲。
    - `0f81f40` 2026-10-27 02:48：誠實追蹤 IOSurface 使用狀態與顯示 swap 退休。
    - `41f65cf` 2026-10-27 03:30：在 START 前退休已複製的 swap 來源並隔離 Metal 完成工作。
    - `7857d9a` 2026-10-27 04:22：為 Fuxi GPU scanout 加入 opt-in 三重緩衝。
    - `8c1bd3b` 2026-10-27 08:56：於 `SWEET FB` 統計行附上各 ABI selector 次數／平均／最大耗時。
    - `206c03d` 2026-10-27 09:37：加入 `IOMFBDisplayRefresh` 120 Hz 心跳、early `SwapWait` retire 與 direct scanout 差異文件。
    - `4e30dbb` 2026-10-27 09:51：整合 Adreno 740 submit/ring 排程統計至 stats 視窗。
    - `9efbaeb` 2026-10-27 16:00：加入 1/64-tick scanout 複製輪詢（`fuxigpuncimport` 2048）以於退休時釋放 surface 租約。
    - `e488177` 2026-10-27 19:07：量測 START-to-transfer-done 與 ready-to-START；加入 panel120 3/4 週期下限。
    - `cebc2a5` 2026-10-28 14:03：加入 `disp0` `fuxi,refresh-mhz`（實測 TE 刷新率）與 `fuxi,swap-hist`（`SwapEnd` 間隔直方圖）。
    - `6b1b1e1` 2026-10-30 18:15：實作 inline present 排序控制與 128 幀事件紀錄（`fuxigpuncimport` 8/16/32）。
    - `9589ace` 2026-10-30 19:02：僅在 `fuxigpuncimport` bit 8 啟用時以堆積配置 128-entry frame log，解決 `AppleCLCD` 超過 16 KB `kalloc_type` 限制（CI `41046931721`）。
    - `82d06f1` 2026-10-30 20:08：整合 `copy priority park`（bit 27）計數器與 `pk=` 欄位至 `flog` 與 `fuxi-flog.py`。
    - `4c374df` 2026-10-30 21:13：新增 `'SWEET FB: unshown paths'` 分類計數、`dp=` 丟棄欄位與 bit 28 突發觸發紀錄。
    - `2ae1d27` 2026-10-30 23:26：整合 DPU stage 3 直接掃描輸出（`sweet_surface_direct`）於 `SweetFramebuffer` 呈現路徑。
    - `70451f3` 2026-10-31 10:46：常駐輸出 200 字元自動換行節奏日誌（`swapwait`、`job_stamps`）並加入 `fuxiearly` bits 2/4。
    - `de8cfff` 2026-11-05 23:28：將 120 Hz 實驗集中於 `P-UNIXNU` overlay 並還原 `0093` 編輯。
    - `10ae953` 2026-11-06 15:38：`P-UNIXNU` 加入 `fuxipresent` 每幀追蹤與日誌中的實際 tick 週期。
- **復現**：見 README 共用步驟 1–3；依目的傳入 `fuxipanel120=1`、`fuxifbprio=1`、`fuxiearly=...`、`fuxi,log=1`（ADT）等；console 輸出 `SWEET FB: IOMFBDisplayRefresh displayMinRefreshInterval=...`、`SWEET FB: swapwait ...`、`FUXI LOG: console to ...`、`FUXI UNIXNU UFS: guarded completion...`。
- **主機測試**：`unixnu`（`sizeof(AppleCLCD)` 保護、200 字元換行續接、FIFO fates 3/4、admission retire）、45 項預設 Linux 主機腳本與 `dpu_stage3_tests` 通過。
- **驗證紀錄**：實機 V251 量測顯示 102 Hz（目標 120 Hz）、lost ticks ~51% client / ~49% delivery、`replaced` 9–34/600；本機另驗證 `fx_log` 寫入 `mtdblk` 供獨立開機拉取與背光 20 ms DMA 完成等待。
- **參考來源**：UniXNU（`NativeUfs`、`NativeMobileDisplay/NativeLegacyRequest.h`、`cb074994` BSD-2-Clause-Patent）、`LICENSE.NativeUfs`、`LICENSE.UniXNU`、`LICENSE.NativeMobileDisplay`
- **結論／未完成**：完成顯示呈現與 UFS 安全策略核心整合；高階 120 Hz/early/scanout 旗標維持開機參數 opt-in，獨立開機 log 轉寫 `mtdblk`。
  - 基底 `979e2f08` 之後的本機提交（舊到新，箭頭後是記錄該修改的項目）：
    - `d5c44c3` 2026-11-15 00:02：fuxiadsp: start the audio DSP from XNU and read its battery manager (patch 0117, port of UniXNU-sheng AdspProbe/ShengBattery; firmware read in place from modem_a) → K-001
    - `7af418a` 2026-11-16 07:43：fuxi: logdump console log for standalone boots; backlight DMA-done waits a full frame → G-048、K-018、K-019
    - `d96adda` 2026-11-16 07:46：fx_log: Fuxi has no logdump partition; use the empty, unused mtdblk (LUN 0, 32 MiB) → G-048、K-019
    - `05ed0fa` 2026-11-17 22:17：Answer RSSI_BOUNDS, add DIRECT_PLAY amplitude and a quiet boot console → K-033、K-064、K-065
- **出處**：`ksrc:xnu/patches/0106-fuxi-unixnu.patch`；上游提交 `1c74fa7`～`10ae953`（共 29 筆）；本機提交 `d5c44c3`、`7af418a`、`d96adda`、`05ed0fa`

### S-107 實作依 Entitlement 授權特權與無容器程序 Sandbox 路徑回應（fuxiprivs／fuxisbpath）

- **日期**：2026-10-23 10:46
- **狀態**：上游繼承（ios-fuxi）；ksrc：上游原樣
- **開關**：
  - `fuxiprivs=1`（註解開關，沿用 `0103`）：開機參數，當某項特權未被既有政策允許、但呼叫者自身程式簽章帶有 iOS 對應的 entitlement（如 `dasd` 的 `vm-pressure-level`、`symptomsd` 的 `com.apple.private.network.statistics`）時予以放行；預設關閉。
  - `fuxisbpath=1`（註解開關，沿用 `0103`）：開機參數，使無 `CFFIXED_USER_HOME`（無容器）的程序在 Sandbox request 4 取得成功且不改寫呼叫者緩衝區（取代 H1 的 `ENOENT`）；預設關閉。
- **問題／現象**：部分系統 daemon（如 `dasd`、`symptomsd`）因缺少對應特權或無容器路徑查詢回傳 `ENOENT` 而被 Sandbox 拒絕。
- **根因**：XNU 原樣無 Apple Sandbox kext；H1 `sweet_amfi.c` 僅提供基本容器查詢與固定清單，未依 entitlement 查表放行特權或對無容器程序回傳成功。
- **做法**：修改 `bsd/kern/sweet_amfi.c` 並新增 `bsd/kern/fuxi_sys.h`（共 2 檔、188 行）。將 HackiOS `kern_minimac.c` 的 `minisandbox_priv_grant` entitlement 對照表與無容器 Sandbox request 4 處理移植為有界輔助標頭，僅檢查呼叫者自身憑證且每項特權開機首次授權記錄一次。
- **復現**：見 README 共用步驟 1–3；開機參數帶 `fuxiprivs=1` 與／或 `fuxisbpath=1`；console 輸出 `FUXI SYS: privileges by entitlement %s (%u pairs), uncontained container path %s\n` 與 `FUXI SYS: %s privilege %d granted by %s\n`。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果。000 線 2026-11-19 20:54 首次上機（K-077 趟 18）：bluetoothd、rapportd、identityservicesd 的 Skywalk 特權（12000／12001）及網路特權依 entitlement 放行，藍牙開關與 Windows SDP 無退步；沒有對應 entitlement 的 1002 照舊被拒。
- **參考來源**：HackiOS（`hackios-xnu-kernel/bsd/kern/kern_minimac.c`，APSL 2.0）、`docs-lanes/sys-port.md`
- **結論／未完成**：完成可選特權與無容器 Sandbox 路徑相容層，預設皆關閉。
- **出處**：`ksrc:xnu/patches/0107-fuxi-sys.patch`；上游提交 `7234ecc`

### S-108 實作 SweetSurface Scanout 租約管理、列對齊、Zero-Copy 調查與 DPU Stage 3 直接掃描（sweetsurfrow）

- **日期**：2026-10-23 13:33～2026-10-31 10:46（本機提交至 2026-11-14 19:45）
- **狀態**：上游繼承（ios-fuxi）；ksrc：本機修改 +10／-10（本機提交 `75148d2`）；本機修改見 G-016、K-072
- **開關**：
  - `sweetsurfrow=<0|1|2>`：開機參數，控制 4-byte 單平面 `IOSurface` 列對齊進位至 256 bytes（`2` 預設：僅對非 64-byte 對齊的列進位、`1` 全部進位、`0` 不進位）。
- **問題／現象**：實機測試曾遇到 MaterialKit 快照 surface 採 32-byte 對齊導致 `backboardd` Metal 渲染器不包裹而使 App 資料庫背景全黑；`1065x282` 對齊 256 後高度未補齊 4 列導致 Turnip 線性匯入失敗退回 CPU；以及 GPU scanout／DPU stage 3 直接掃描輸出時須協調 CA 寫入租約與顯示端讀取同步。
- **根因**：`backboardd` Metal 僅包裹 256-byte 對齊的 4-byte 單平面 surface，Turnip 線性影像對高度採 4 列對齊過取樣，且 `SweetSurface` 原缺乏顯示佔用追蹤、`fuxiearly` 租約等待與 DPU direct-scanout slot 保護。
- **做法**：修改 `iokit/Kernel/arm/SweetSurface.cpp`、`iokit/Kernel/arm/SweetSurface.h`、`pexpert/arm/pe_serial.c`（共 3 檔、431 行）。實作 `sweetsurfrow` 對齊與 4 列高度填充、`sweet_surface_direct` 供 DPU stage 3（`fuxigpuncimport` bit 24）查詢單層 BGRA8 `1125x2436` stride 4608 crop `22,18 1080x2400` 頁面齊對 `IOBuffer` 並鎖定 slotted surface 防寫撕裂、bit 20 預先清理與 zero-copy 分類調查、`fuxiearly` bit 2 `sweet_surface_display_wait`、以及 `pe_serial.c` 手動 START 期間靜音保護。
  - 演進（全部 13 筆上游提交）：
    - `0c7a34c` 2026-10-23 13:33：加入 opt-in 有 fence 的合成幀 GPU scanout。
    - `467d961` 2026-10-23 15:23：修正 `pe_serial.c` display-kick hunk 行號錨點以通過 CI `git apply`。
    - `7a0decf` 2026-10-26 22:55：於 commit 時預留 scanout surfaces 以釋放 CA scheduled waits。
    - `a72b285` 2026-10-27 01:27：佇列化 scanout swap 並在寫入端退休時喚醒呈現。
    - `fffb7a4` 2026-10-27 02:04：直接於喚醒時執行呈現並量測 scanout 階段延遲。
    - `0f81f40` 2026-10-27 02:48：誠實追蹤 IOSurface 使用狀態與顯示 swap 退休。
    - `beb5b57` 2026-10-27 21:18：針對前 8 次多平面非 BGRA surface 建立記錄 user backtrace（App 資料庫 `b3a8` 診斷）。
    - `450d4c3` 2026-10-27 23:17：將未對齊 4-byte 單平面列進位至 256（修復 App 資料庫背景全黑）；`sweetsurfrow=0` 可關閉。
    - `a1cd053` 2026-10-28 00:53：為 4-byte 單平面 surface 依高度進位至 4 列倍數配置記憶體（修復 `1065x282` Turnip 線性匯入失敗）。
    - `8973b60` 2026-10-28 14:15：加入 `sweet_surface_scanout_class` zero-copy 調查與 bit 20 鎖外複製來源預先清理。
    - `6b1b1e1` 2026-10-30 18:15：支援 inline present 排序控制與 `fuxigpuncimport` 8/16/32 計數與事件追蹤。
    - `2ae1d27` 2026-10-30 23:26：實作 `sweet_surface_direct` 支援 DPU stage 3 零複製直接掃描輸出（`fuxigpuncimport` bit 24）。
    - `70451f3` 2026-10-31 10:46：實作 `fuxiearly` bit 2 寫入租約顯示端等待（25 ms 上限）以配合 admission retire。
- **復現**：見 README 共用步驟 1–3；列對齊預設啟用（`sweetsurfrow=2`），DPU stage 3 與 early retire 須分別搭配 `fuxigpuncimport` bit 24 及 `fuxiearly` bit 2；console 輸出 `FUXI SCANOUT: preclean cleans=...`、`SWEET SURF: bt ...`。
- **主機測試**：`dpu_stage3_tests`、`gpu-scanout`（writer display wait 等）通過。
- **驗證紀錄**：實機確認修復 App 資料庫背景全黑（`450d4c3`）與 `1065x282` 線性匯入退回 CPU（`a1cd053`）；`fuxiearly` bit 2 當時提交註明「Not yet observed on device」。
- **參考來源**：來源未記（配合 Turnip / `backboardd` Metal / DPU VIG0 硬體規格實作）
- **結論／未完成**：`SweetSurface` 列對齊與 4 列高度補齊已預設生效；DPU stage 3 直接掃描與 `fuxiearly` 租約等待採開機參數 opt-in。
  - 基底 `979e2f08` 之後的本機提交（舊到新，箭頭後是記錄該修改的項目）：
    - `75148d2` 2026-11-14 19:45：base: 979e2f08 + harness patches (HEAD 2aae087)（本份是 `kernel-surfrow-mode2.patch`：`sweetsurfrow=2`） → G-016、K-072
- **出處**：`ksrc:xnu/patches/0108-fuxi-gpu-scanout.patch`；上游提交 `0c7a34c`～`70451f3`（共 13 筆）；本機提交 `75148d2`

### S-109 實作 AppleARMBacklight 服務與 DCS 0x51 螢幕亮度控制（fuxibacklight）

- **日期**：2026-10-23 19:45～2026-10-24 00:11（本機提交至 2026-11-14 22:52）
- **狀態**：上游繼承（ios-fuxi）；ksrc：本機修改 +295／-2（本機提交 `eafc778`）；本機修改見 K-017
- **開關**：
  - `fuxibacklight=<1..4>`：開機參數，於 `AppleCLCD` 下發佈背光控制服務：`1` 僅觀察屬性存取與亮度請求、`2` 加邏輯電源興趣通知、`3` 隔離 ON 通知僅送 OFF、`4`（本機新增）發佈具 `AppleARMBacklight` 類別與 `backlight-control=true` 的服務，支援 DBV 0..2047 並透過顯示擁有者於傳輸間隙送出 MIPI DCS `0x51`；預設關閉。
  - `fuxibacklighttrace`：開機參數，記錄啟動時序追蹤。
- **問題／現象**：iOS 16 `CoreBrightness`（`-[CBDisplayContaineriOS findBacklight]`）僅承認具備 `backlight-control = true` 且符合 `AppleARMBacklight` 類別的內建顯示背光服務；上游僅實作 `FuxiBacklight`（modes 1–3）不符該類別，導致 `CoreBrightness` 從未下發亮度等級。
- **根因**：核心缺乏 `AppleARMPlatform` kext 提供的 `AppleARMBacklight` 類別與將 `IODisplayParameters` `brightness`/`FadePeriod` 轉為面板 DCS `0x51` 的通路。
- **做法**：修改 `iokit/conf/files.arm64` 並新增 `FuxiBacklight.cpp`、`fuxi_backlight_logic.h`、`FuxiDsiBacklight.cpp`、`fuxi_dsi_backlight_logic.h`（共 5 檔、680 行）。上游建置 `FuxiBacklight` 觀察服務與電源通知實驗；本機新增 `FuxiDsiBacklight` 作為 `AppleARMBacklight` 類別本身，於獨立 workloop 以 16 ms 步進線性漸變 DBV（0..2047，首請求立即套用），交由 `fuxi_gpu_scanout_backlight` 下發 DCS `0x51`。
  - 演進（全部 4 筆上游提交）：
    - `7572f50` 2026-10-23 19:45：加入 opt-in 觀察用背光 IOService（`fuxibacklight=1`）。
    - `43ea647` 2026-10-23 20:57：加入 opt-in 邏輯電源興趣通知（mode 2）。
    - `31ec41f` 2026-10-23 22:04：加入 opt-in mode 3 隔離 ON 通知僅送 OFF。
    - `d9f7b21` 2026-10-24 00:11：加入 opt-in 啟動時序追蹤並記錄未解延遲。
- **復現**：見 README 共用步驟 1–3；開機參數帶 `fuxibacklight=4`（日常/實測模式）；console 輸出 `FUXI BACKLIGHT DSI: published AppleARMBacklight min=0 max=%u value=%u; levels go to the display owner (DCS 0x51)\n`、`FUXI BACKLIGHT DSI: brightness #%u goal=%u sent=%u fade_ms=%u published=%u\n`。
- **主機測試**：提交 `7572f50` 記載 5g/9g 雙配置背光主機測試通過（本機另有背光策略檢查）。
- **驗證紀錄**：上游 modes 1–3 僅觀察；本機 `fuxibacklight=4` 後續已於實機控制中心驗證亮度滑桿連續調整（2900 次不停，詳見工作區紀錄）。
- **參考來源**：iOS 16 `CoreBrightness` / `IODisplayLib` 行為觀察
- **結論／未完成**：完成可實際調整 Xiaomi 13 面板亮度的 `AppleARMBacklight` 服務（`fuxibacklight=4`）。
  - 基底 `979e2f08` 之後的本機提交（舊到新，箭頭後是記錄該修改的項目）：
    - `eafc778` 2026-11-14 22:52：fuxibacklight=4: AppleARMBacklight service; the display owner sends DCS 0x51 between transfers → K-017
- **出處**：`ksrc:xnu/patches/0109-fuxi-backlight.patch`；上游提交 `7572f50`、`43ea647`、`31ec41f`、`d9f7b21`；本機提交 `eafc778`

### S-110 移植 UniXNU Union Lower-Directory 查找資格修正（fuxiuniondir）

- **日期**：2026-10-24 22:50
- **狀態**：上游繼承（ios-fuxi）；ksrc：上游原樣
- **開關**：
  - `fuxiuniondir=1`（開機參數定義於 `0106`）：啟用下層目錄查找資格並正確釋放 `iocount`；預設關閉。
- **問題／現象**：在 union 掛載下進行路徑查找時，需要安全地啟用下層目錄資格並確保 vnode `iocount` 正確釋放，避免參考計數洩漏或查找異常。
- **根因**：XNU 原樣在特定 union 查找條件下未套用 UniXNU 的 lower-directory 放行與計數修正。
- **做法**：修改 `bsd/vfs/vfs_lookup.c`（新增 9 行、刪除 1 行），於受 `fuxiuniondir=1` 閘控下整合 UniXNU 的 union lower-directory 查找與 `new_dvp = nd.ni_vp` 處理。
- **復現**：見 README 共用步驟 1–3；開機參數帶 `fuxiuniondir=1`。
- **主機測試**：提交 `4c26c3e` 記載 production-code ASan/UBSan vnode ownership 模型、ARM64 語法檢查與 45 項預設 Linux 主機腳本通過。
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：UniXNU superproject（`cb0749940c598f2bd7fb113c1a660fcc2fd825ce`，BSD-2-Clause-Patent）
- **結論／未完成**：完成與 `0106` 配套的 union lower-directory 查找整合補丁，預設關閉。
- **出處**：`ksrc:xnu/patches/0110-fuxi-union-lower-directory.patch`；上游提交 `4c26c3e`

### S-111 實作可選使用者空間程序堆疊／IPI 即時取樣剖析器（sweetprof）

- **日期**：2026-10-26 22:07～2026-10-27 21:18（本機提交至 2026-11-17 09:53）
- **狀態**：上游繼承（ios-fuxi）；ksrc：本機修改 +198／-4（本機提交 `75148d2`、`f1a0846`、`7af418a`、`d96adda`、`d10a41d`）；本機修改見 G-021、G-027、G-048、K-018、K-019、K-071、K-072
- **開關**：
  - `sweetprof=<N>`：開機參數，延遲 `N` 秒後開始取樣目標程序的使用者堆疊（`1` 表 60 秒、`2..3600` 表 `N` 秒、`10000+N` 改取樣 `SpringBoard` 而非 `backboardd`；預設 `0` 關閉）。
  - `sweetprofname=<名稱>`（本機新增）：開機參數，指定取樣任意具名程序（有界 opt-in）。
- **問題／現象**：分析 `backboardd` 或 `SpringBoard` 卡頓時，核心僅採 4 層堆疊會停在 IOKit/Turnip wrapper 中看不到是哪條 CoreAnimation 路徑發出同步 GPU 等待；且正在核心執行使用者程式的執行緒未發生例外，無法直接讀到最新使用者暫存器 `pc`/`lr`。
- **根因**：XNU 缺乏不暫停目標程序、可透過跨核 IPI 擷取正在執行中執行緒的即時使用者暫存器並彙整 flat/inclusive/call-edge 的輕量取樣剖析器。
- **做法**：修改 `bsd/conf/files`、`bsd/dev/sweet_ufs.c`、`osfmk/conf/files` 並新增 `bsd/dev/sweet_prof.c`、`osfmk/kern/sweet_prof_sample.c`、`osfmk/kern/sweet_prof_logic.h`（共 6 檔、401 行）。紀錄 8 層 user frames、透過每核單一請求 `cpu_xcall` IPI 中斷上下文複製使用者模式暫存器（`x16 = 0x7fffffff` 標記）、產出 64-byte bucket 的 `PROFPC`、針對 `com.apple.coreanimation.display*` 與主執行緒的 inclusive `PROFINC` 及呼叫做邊 `PROFEDGE`，並在多平面非 BGRA surface 建立時記錄 backtrace。
  - 演進（全部 5 筆上游提交）：
    - `11a03e0` 2026-10-26 22:07：加入 opt-in `backboardd` 使用者堆疊剖析器。
    - `3cfe076` 2026-10-26 22:09：`sweetprof=N` 支援延遲 N 秒開始取樣。
    - `e45d045` 2026-10-27 05:29：每筆取樣改記 8 層 user frames（解決 4 層停在 IOKit/Turnip wrapper）。
    - `7827f46` 2026-10-27 19:37：`sweetprof=10000+N` 支援改為取樣 `SpringBoard`。
    - `beb5b57` 2026-10-27 21:18：於前幾次多平面非 BGRA surface 建立時記錄 user backtrace（App 資料庫 `b3a8` 診斷）。
- **復現**：見 README 共用步驟 1–3；開機參數帶 `sweetprof=60`（或 `sweetprof=10060`、`sweetprofname=...`）；console 輸出 `PROF target %s\n`、`PROF backboardd[%d] slide 0x%llx mh 0x%llx\n`、`PROFPC t%llu 0x%llx %u\n`、`PROFINC ...`、`PROFEDGE ...`、`PROF done ...`。
- **主機測試**：無
- **驗證紀錄**：上游提交未記單獨實機數據（已於多輪 GPU/CA 效能調查搭配使用）
- **參考來源**：來源未記
- **結論／未完成**：完成低干擾跨核使用者堆疊剖析器（支援 `backboardd`、`SpringBoard` 及本機擴充的具名程序），預設關閉。
  - 基底 `979e2f08` 之後的本機提交（舊到新，箭頭後是記錄該修改的項目）：
    - `75148d2` 2026-11-14 19:45：base: 979e2f08 + harness patches (HEAD 2aae087)（本份是 `kernel-prof-edges／inclusive／inclusive-wait／live.patch`：`sweetprof` 剖析） → G-021、G-027、K-072
    - `f1a0846` 2026-11-15 00:06：0111: regenerate after 0106 grew the firmware-partition reads (hunk offset only) → K-072
    - `7af418a` 2026-11-16 07:43：fuxi: logdump console log for standalone boots; backlight DMA-done waits a full frame → G-048、K-018、K-019
    - `d96adda` 2026-11-16 07:46：fx_log: Fuxi has no logdump partition; use the empty, unused mtdblk (LUN 0, 32 MiB) → G-048、K-019
    - `d10a41d` 2026-11-17 09:53：Allow bounded opt-in profiling of a named process → K-071
- **出處**：`ksrc:xnu/patches/0111-fuxi-user-profiler.patch`；上游提交 `11a03e0`～`beb5b57`（共 5 筆）；本機提交 `75148d2`～`d10a41d`（共 5 筆）

### S-112 實作 backboardd IORegistry 查詢與 Work-Interval 耗時追蹤（fuxiregtrace）

- **日期**：2026-10-27 08:22～2026-10-27 08:31
- **狀態**：上游繼承（ios-fuxi）；ksrc：上游原樣
- **開關**：
  - `fuxiregtrace`：開機參數，啟用對 `backboardd` 的 IORegistry 查詢／序列化／複製及 work-interval 耗時追蹤（固定緩衝區、不保留 registry 物件）；預設關閉。
- **問題／現象**：需要量測 `backboardd` 在 IORegistry 屬性查找、序列化、複製與 work-interval 呼叫上的真實時間開銷，同時避免動態配置或持有 registry 物件影響行為。
- **根因**：XNU 原樣缺乏針對特定使用者空間程序（`backboardd`）的 IORegistry 與 work-interval 階段計時掛鉤。
- **做法**：修改 `bsd/conf/files`、`bsd/dev/sweet_ufs.c`、`bsd/kern/sys_work_interval.c`、`iokit/Kernel/IOUserClient.cpp`、`osfmk/kern/Makefile` 並新增 `bsd/dev/fuxi_regtrace.c`、`osfmk/kern/fuxi_regtrace.h`、`iokit/Kernel/fuxi_regtrace_iokit.h`（共 8 檔、172 行）。
  - 演進：
    - `c45a0de` 2026-10-27 08:22：加入 opt-in `backboardd` registry 與 work-interval 計時追蹤。
    - `60d8283` 2026-10-27 08:31：將 scope-exit work-interval 計時器標記為未使用以消除編譯警告。
- **復現**：見 README 共用步驟 1–3；開機參數帶 `fuxiregtrace=1`；console 輸出 `REGTRACE backboardd span_us=%llu dropped=%llu/%llu/%llu wi=%llu/%llu/%llu` 與 `[class=%s key=%s ... lookup_us=%llu serialize_us=%llu copy_us=%llu]`。
- **主機測試**：無
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：來源未記
- **結論／未完成**：完成可選 `backboardd` registry 與 work-interval 剖析補丁，預設關閉。
- **出處**：`ksrc:xnu/patches/0112-fuxi-registry-trace.patch`；上游提交 `c45a0de`、`60d8283`

### S-113 移植音訊 GPR／AudioReach 封包編碼與共用 RSC ACTIVE TCS 仲裁器基礎（P-AU1）

- **日期**：2026-10-27 09:41
- **狀態**：上游繼承（ios-fuxi）；ksrc：上游原樣
- **開關**：無
- **問題／現象**：為小米 13 ADSP 音訊帶起預備 GPR（Generic Packet Router）狀態機、AudioReach 圖管理器（APM）／PRM 封包編碼，以及防止音訊 DSP 與數據機／WLAN 驅動同時操作 apps RSC（DRV2）ACTIVE TCS 序列器時互相破壞命令字。
- **根因**：XNU 原樣缺乏 Qualcomm GPR/AudioReach 封包編解碼與無鎖獨立（freestanding）RSC ACTIVE TCS 原子自旋仲裁器；且注記 `fuxi_wlan.c` 尚未接線到此仲裁器、而 AudioReach 實例編號為 Sheng 拓撲尚未依 Fuxi ACDB 替換。
- **做法**：新增 `bsd/dev/fuxi_gpr.h`、`bsd/dev/fuxi_gpr.c`、`bsd/dev/fuxi_audioreach.h`、`bsd/dev/fuxi_audioreach.c`、`bsd/dev/fuxi_rpmh_arb.h`（共 5 檔、723 行，未掛載至 kernel build 的純演算法／封包層與標頭基礎）。實作 24-byte GPR 標頭編解碼與 APM 就緒輪詢狀態機、AudioReach 裝置／串流子圖與 PRM 時脈／區塊投票封包建構，以及基於 C11 `__atomic` 的有界自旋與毒化（`FUXI_RPMH_POISONED`）仲裁器。
- **復現**：見 README 共用步驟 1–3（本補丁為主機驗證之獨立編碼與仲裁模組，無開機參數與 console 輸出；實機 ADSP 帶起由本機後續 `0117` 接手）。
- **主機測試**：提交 `3efc963` 註明「host-tested」。
- **驗證紀錄**：上游提交未記實機結果
- **參考來源**：UniXNU-sheng（`package/ShengGpr.h`、`ShengGpr.c`、`ShengAudioReach.h`、`ShengAudioReach.c` commit `08cde20edc32`，BSD-2-Clause-Patent）、HackiOS單一 RSC mutex 概念、`docs-lanes/audio.md`（P-AU1）
- **結論／未完成**：完成 P-AU1 GPR/AudioReach 封包與 RSC 仲裁器主機驗證基礎；待接線至實際 ADSP/WLAN 驅動並依 Fuxi ACDB 替換拓撲實例編號。
- **出處**：`ksrc:xnu/patches/0113-fuxi-audio.patch`；上游提交 `3efc963`

### S-114 實作全核心 EL1 PAC 指針驗證硬體自我測試（fuxigpuncimport bit 25）

- **日期**：2026-10-30 19:21～2026-10-30 20:41
- **狀態**：上游繼承（ios-fuxi）；ksrc：上游原樣
- **開關**：
  - `fuxigpuncimport` bit 25（值 `33554432`）：開機參數，於 `SweetCPU::initCPU` 在每顆 CPU 的 EL1（IRQ/FIQ 遮罩）執行 PAC 硬體自測（arm64 暫時寫入測試金鑰並還原；`arm64e` 下以 `mode=live` 只讀金鑰不寫入，並印出 `FUXI PAC: native`）；預設關閉。
- **問題／現象**：進入 `arm64e` 核心前，須在實機確認 SM8550 每一顆 CPU 核心於 EL1 是否確實具備 PAC（QARMA5 / FPAC）、讀寫 APIA/APIB/APDA/APDB/APGA 金鑰是否遭 Hypervisor 攔截、篡改後的 `AUT*` 是否正確失效或觸發 FPAC 例外，以及多核心產出的簽章是否一致。
- **根因**：不同虛擬化／韌體環境可能對系統暫存器或 PAC 指令注入 `UNDEF` 陷阱，直接在關鍵路徑啟用 `arm64e` 若無階段 0 實機自測會難以區分陷阱與金鑰不一致問題。
- **做法**：修改 `iokit/Kernel/arm/SweetCPU.cpp`、`osfmk/arm64/sleh.c`、`osfmk/conf/files.arm64` 並新增 `osfmk/arm64/fuxi_pac_logic.h`、`osfmk/arm64/fuxi_pac.c`（共 5 檔、868 行）。將所有探測指令集中於 `fuxi_pac_stub` 表，搭配 `sleh_synchronous` 的單核心武裝陷阱跳過保護（先以 `UDF #0` 驗證保護有效），依次檢查 `ID_AA64ISAR1_EL1`/`ISAR2`、`SCTLR_EL1`、十個金鑰半部讀寫、IA/IB/DA/DB 簽章／驗證與雙重篡改測試及 `PACGA` 跨核心一致性。
  - 演進：
    - `920a149` 2026-10-30 19:21：加入 opt-in 全核心 EL1 PAC 自我測試（`fuxigpuncimport` bit 25）。
    - `371fec5` 2026-10-30 20:41：為 `0115` `arm64e` 核心加入 `mode=live` 唯讀模式與 `FUXI PAC: native` 摘要輸出。
- **復現**：見 README 共用步驟 1–3；開機參數帶 `fuxigpuncimport=33554432`（或與其他位元 OR）；console 輸出 `FUXI PAC: cpu=%u probe begin`、`FUXI PAC: cpu=N ... verdict=ready`、`FUXI PAC: summary ...`（`arm64e` 另印 `FUXI PAC: native ...`）。
- **主機測試**：`test-fuxi-pac`（含嚴格 arm64 Mach-O 編譯與 live/native 模擬）通過。
- **驗證紀錄**：`371fec5` 提及 S224 stage-0 結果並記錄於 `docs-lanes/arm64e.md`。
- **參考來源**：`docs-lanes/arm64e.md`（stage 0/1）、`osfmk/arm64/pac_asm.h`
- **結論／未完成**：完成 PAC 硬體能力、金鑰可寫性與跨核一致性自測工具，預設關閉。
- **出處**：`ksrc:xnu/patches/0114-fuxi-pac-probe.patch`；上游提交 `920a149`、`371fec5`

### S-115 實作實驗性原生 arm64e 核心建置與開機金鑰初始化（FUXI_ARM64E=1）

- **日期**：2026-10-30 20:41
- **狀態**：上游繼承（ios-fuxi）；ksrc：上游原樣
- **開關**：無（建置時期以 `FUXI_ARM64E=1` 切換目標 `kernel=metal4e`，非一般開機參數）
- **問題／現象**：需要為小米 13（SM8550 支援 QARMA5 與 FPAC）建置原生 `arm64e`（ARMv8.3-A PAC）XNU 核心，使核心與使用者空間共享一組開機隨機產生的 PAC 金鑰並完成所有核心同步驗證。
- **根因**：XNU 原樣 `SWEET.h` 未開啟 `HAS_APPLE_PAC`/`HAS_NATIVE_PAC`，`start.s` 未於 EL1 初始化原生 PAC 金鑰，EL2 未設定 `HCR_EL2.API/APK`，且 `SweetCPU` 傳入 PSCI 的進入點指標在 `arm64e` 下帶有 IA 簽章需先 strip。
- **做法**：修改 `iokit/Kernel/arm/SweetCPU.cpp`、`makedefs/MakeInc.def`、`osfmk/arm64/machine_routines_asm.s`、`osfmk/arm64/pac_asm.h`、`osfmk/arm64/start.s`、`pexpert/pexpert/arm64/SWEET.h`（共 6 檔、213 行）。在 `FUXI_ARM64E=1` 下匯出 `ARCH_STRING=arm64e`（`-march=armv8.3-a+crypto`、`ld-classic -threaded_starts_section`）；`start.s` `PAC_INIT_KEY_STATE` 由開機 CPU 以 `CNTPCT_EL0` 經 `splitmix64` 衍生唯一金鑰組，各核心載入、讀回並交叉比對 `PACGA`（不符者停駐於 stage `0x35`）；設定 `HCR_EL2.API/APK`、修復 `ml_enable/disable_user_jop_key` 提早返回，並於 PSCI 呼叫前對函式指標執行 strip。
- **復現**：見 README 共用步驟 1–3；建置時設定 `FUXI_ARM64E=1`（CI `kernel=metal4e`）；可搭配 `0114`（`fuxigpuncimport` bit 25）觀察 `FUXI PAC: native mode=... verdict=ok`。
- **主機測試**：`test-fuxi-arm64e`（結構、MakeInc 求值、佈局、arm64e 組語、payload、CI 計畫）通過。
- **驗證紀錄**：後續 `0116` 提交記載 CI `41065534474`（E231）中 `native keys verdict=ok` 且 `live probe ready`，惟隨即因 fat binary 選用 `arm64e` slice 配上純 `arm64` `dyld_shared_cache` 發生 FPAC panic（由 `0116` 緩解）。
- **參考來源**：UniXNU `native-pac-keys.patch`（BSD-2-Clause-Patent）、`docs-lanes/arm64e.md`（stage 1）
- **結論／未完成**：完成可開機的原生 `arm64e` 核心與金鑰初始化；因 rootfs 為純 `arm64` 系統庫，仍需 `0116` 調整二進位評分並待解決 stage-2 user JOP 問題。
- **出處**：`ksrc:xnu/patches/0115-fuxi-arm64e.patch`；上游提交 `371fec5`

### S-116 調整 arm64e 核心下 Fat Binary 評分以停用 User JOP 避免 FPAC Panic（fuxiarm64e）

- **日期**：2026-10-30 21:58
- **狀態**：上游繼承（ios-fuxi）；ksrc：上游原樣
- **開關**：
  - `fuxiarm64e=1`：開機參數，在 `FUXI && __arm64e__` 核心下還原 Apple 預設偏好（優先執行 fat binary 的 `arm64e` slice 並啟用 user JOP）；預設關閉（將 `arm64e` slice 評分降為 8，低於 `arm64` 10 與 `arm64_all` 9）。
- **問題／現象**：在 `0115` `arm64e` 核心（E231，CI `41065534474`）上完成 PAC 金鑰初始化後，CPU 5 在 `ml_auth_ptr_unchecked`（源自 `pmap_auth_user_ptr`，IA 金鑰、modifier 0、`x0 0x1f79be724`）發生 FPAC panic，原因為 `HAS_APPLE_PAC` 使 `grade_binary` 偏好執行 fat binary（如 `pid 84 SafariBookmarksSyncAgent`）的 `arm64e` slice，但 rootfs 僅有未簽章的 `arm64` `dyld_shared_cache`，導致 `bsdthread_register`（`status.c:812`）驗證函式指標時觸發硬體 FPAC 錯誤。
- **根因**：`arm64e` 核心預設賦予 `arm64e` slice 最高評分（12 > `arm64` 10），在缺乏 `arm64e` `dyld_shared_cache` 的 rootfs 上會讓 fat 程序以 user JOP 執行於未簽章共享快取之上。
- **做法**：修改 `bsd/dev/arm/kern_machdep.c` 與 `bsd/kern/kern_exec.c`（新增 34 行）。在 `FUXI && __arm64e__` 且未設 `fuxiarm64e=1` 時，將 `arm64e` slice 評分調降為 `8`（使 fat binary 選用 `arm64` slice 並套用 `IMGPF_NOJOP`；純 `arm64e` binary 仍以 `arm64e` user JOP 執行），並於 `exec_mach_imgact` 對保留 user JOP 的程序輸出記錄。
- **復現**：見 README 共用步驟 1–3；於 `FUXI_ARM64E=1` 核心預設生效（`fuxiarm64e=1` 可還原原行為）；若執行純 `arm64e` 程式，console 輸出 `FUXI ARM64E: exec %s arm64e slice cpusub=0x%x user_jop=1\n`。
- **主機測試**：`test-fuxi-arm64e`（檢查 `0116` 閘控、執行 `fuxi_arm64e_grade` 並在 `--tree` 下為 arm64e 主機編譯真實 `grade_binary` 驗證選片邏輯）通過。
- **驗證紀錄**：上游提交引用 E231 實機 panic 證據與 `docs-lanes/arm64e.md` 開放的 stage-2 `ml_auth_ptr_unchecked` 議題。
- **參考來源**：`docs-lanes/arm64e.md`
- **結論／未完成**：完成 `arm64e` 核心搭配 `arm64` rootfs 的使用者空間降級執行保護；完整的 `arm64e` userland 與 stage-2 `ml_auth_ptr_unchecked` 議題尚待後續解決。
- **出處**：`ksrc:xnu/patches/0116-fuxi-arm64e-user-jop.patch`；上游提交 `57ba619`

## 其他上游建置補丁

### S-117 XNU 27 與 kernel libpthread 的各層補丁

- **日期**：ksrc 基底 979e2f08 與現有上游補丁序列，2026-11-18 盤點。
- **狀態**：保留早期 iOS 27 路線，相關實機紀錄見 E-001～E-029。
- **開關**：上游工具的 kernel-ios27 目標。FUXI_XNU27_CHAINED 與 FUXI_XNU27_RELEASE_REF 控制附加建置。
- **問題／現象**：早期 xnu-12377 路線的機型層、平台層與 libpthread 補丁有不同套用順序。
- **根因**：XNU 27 使用自己的 xnu27/series。kernel libpthread 是另一個原始碼樹，用獨立的 libpthread/series。
- **做法**：補丁對照逐份列出 xnu27/patches/unixnu、sheng、sm8550、fuxi 和 xnu27/libpthread。來源、排除項目及授權見 xnu27/NOTICE。xnu27/sources.env 釘選核心和 libpthread 版本。xnu27/apply.sh 依序套核心補丁，之後放入 ArmCpu、ArmDTPlatform。tools/ci/build-libpthread27.py 管理 libpthread 層。
- **復現**：在 ksrc 根目錄用 bash tools/ci/build.sh kernel-ios27 --dry-run 查實際釘選與計畫。主機套用驗證以全新的 xnu-12377.121.6 來源執行 python3 tools/test-fuxi-ios27.py --xnu <來源目錄>。需重建完整產物時依 docs-lanes/ios27.md 的 Xcode 與所需輸入。
- **主機測試**：tools/test-fuxi-ios27.py 與 tools/xnuprelink/test_*.py。前者有獨立的套用檢查，後者核對 prelink、fileset 與 Fuxi 的輸入轉換。
- **驗證紀錄**：本次核對來源、檔名與 series。各早期實機結果、panic 與修復見 E-001～E-029。
- **參考來源**：docs-lanes/ios27.md、xnu27/README.md、xnu27/NOTICE。
- **結論／未完成**：保留歷史序列和各補丁位置，給早期實驗復核及後續比較。
- **出處**：ksrc xnu27/series、xnu27/libpthread/series、tools/ci/build-xnu27.sh。

### S-118 上游 NativeMetal、metal2vulkan 與 Mesa 建置補丁

- **日期**：ksrc 基底 979e2f08 的建置鏈，2026-11-18 盤點。
- **狀態**：保留上游建置依存與每份補丁索引。
- **開關**：FUXI_NATIVE_M2V_DRAW=0 跳過上游 Metal 轉譯 draw 補丁。完整建置選項見各工具。
- **問題／現象**：GPU 使用者空間連結到獨立來源樹，建置需可查其補丁與釘選版本。
- **根因**：NativeMetal 與 metal2vulkan 補丁由 NativeMetal 建置腳本套用。Mesa／Turnip 有自己的套用順序。
- **做法**：補丁目錄逐份列出 tools/patches 的 nativemetal-fuxi、metal2vulkan-adrenometal、mesa-fuxi。tools/build-fuxi-nativemetal.py 取前兩組並按檔名順序套用；tools/build-fuxi-turnip.py 明列 Mesa 套用順序。兩個腳本也定義各自的來源與必要工具。
- **復現**：於 ksrc 根目錄以 python3 tools/build-fuxi-nativemetal.py --help、python3 tools/build-fuxi-turnip.py --help 查入口，依 docs-lanes/nativemetal-n1.md 和 docs-lanes/mesa-port.md 準備釘選來源及獨立輸出目錄。依 T-058 進行對應主機測試。
- **主機測試**：tools/test-fuxi-patch-stack.py、tools/test-fuxi-nativemetal.py、tools/test-fuxi-turnip-hostimport.py、tools/test-fuxi-turnip-submit.py。
- **驗證紀錄**：本次核對補丁與使用它的腳本。G 系列保存本專案另加的補丁、部署與實機紀錄。
- **參考來源**：docs-lanes/nativemetal-n1.md、docs-lanes/mesa-port.md、G-002～G-049。
- **結論／未完成**：可由補丁對照定位單份變更，再從釘選來源核對建置。
- **出處**：ksrc tools/patches、tools/build-fuxi-nativemetal.py、tools/build-fuxi-turnip.py。
