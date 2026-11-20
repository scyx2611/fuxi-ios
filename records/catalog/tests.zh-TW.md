# 主機測試對照

> 本檔由 `records/tools/build-catalog.py` 產生，請勿手改；改 `records/map.tsv` 或紀錄項目後重新產生。

「workflow」欄是 harness 裡會執行它的 workflow；空白表示只在本機或由其他測試間接執行。

| 測試 | workflow | 紀錄 |
|---|---|---|
| `ksrc:tools/test-fuxi-adreno-abi-perf.py` |  | [S-100](../items/S-upstream.zh-TW.md#s-100-實作受閘控執行路徑代換測試路由與-nativeadreno-abi-效能除錯追蹤sweetexecswap) |
| `ksrc:tools/test-fuxi-adreno-cache.py` |  | [S-096](../items/S-upstream.zh-TW.md#s-096-整合-adreno-a740-gpu-冷啟動pas13-韌體載入與非同步-2d-呈現fuxigpu)、[S-108](../items/S-upstream.zh-TW.md#s-108-實作-sweetsurface-scanout-租約管理列對齊zero-copy-調查與-dpu-stage-3-直接掃描sweetsurfrow) |
| `ksrc:tools/test-fuxi-adreno-mapping.py` |  | [S-096](../items/S-upstream.zh-TW.md#s-096-整合-adreno-a740-gpu-冷啟動pas13-韌體載入與非同步-2d-呈現fuxigpu)、[S-108](../items/S-upstream.zh-TW.md#s-108-實作-sweetsurface-scanout-租約管理列對齊zero-copy-調查與-dpu-stage-3-直接掃描sweetsurfrow) |
| `ksrc:tools/test-fuxi-adreno.py` |  | [S-096](../items/S-upstream.zh-TW.md#s-096-整合-adreno-a740-gpu-冷啟動pas13-韌體載入與非同步-2d-呈現fuxigpu)、[G-050](../items/G-gpu.zh-TW.md#g-050-在-1080-寬面板顯示完整-1125-寬畫面並對齊觸控) |
| `ksrc:tools/test-fuxi-adt-args.py` |  | [K-067](../items/K-kernel.zh-TW.md#k-067-國家碼相同電源狀態不重做計時adt-fuxiargsbit-34補丁-0121) |
| `ksrc:tools/test-fuxi-arm64e.py` |  | [S-115](../items/S-upstream.zh-TW.md#s-115-實作實驗性原生-arm64e-核心建置與開機金鑰初始化fuxi_arm64e1)、[S-116](../items/S-upstream.zh-TW.md#s-116-調整-arm64e-核心下-fat-binary-評分以停用-user-jop-避免-fpac-panicfuxiarm64e) |
| `ksrc:tools/test-fuxi-ath12k-link.py` | kernel-adsp-battery.yml | [K-074](../items/K-kernel.zh-TW.md#k-074-ath12k-接受韌體配置的-peer-id-0)、[K-050](../items/K-kernel.zh-TW.md#k-050-ath12k-模組連結與-ci)、[K-075](../items/K-kernel.zh-TW.md#k-075-ath12k-wmi-掃描優先級與首輪掃描時間) |
| `ksrc:tools/test-fuxi-audio-prep.py` | kernel-adsp-battery.yml | [K-015](../items/K-kernel.zh-TW.md#k-015-gpr-音訊準備主機模型) |
| `ksrc:tools/test-fuxi-audio-spf.py` | hw000-preparation.yml、kernel-adsp-battery.yml | [K-014](../items/K-kernel.zh-TW.md#k-014-音訊-spf-狀態查詢)、[K-078](../items/K-kernel.zh-TW.md#k-078-用目前-adsp-驗證-fuxi-喇叭與播放圖) |
| `ksrc:tools/test-fuxi-audio.py` |  | [S-113](../items/S-upstream.zh-TW.md#s-113-移植音訊-gpraudioreach-封包編碼與共用-rsc-active-tcs-仲裁器基礎p-au1)、[K-015](../items/K-kernel.zh-TW.md#k-015-gpr-音訊準備主機模型) |
| `ksrc:tools/test-fuxi-backlight.py` |  | [S-109](../items/S-upstream.zh-TW.md#s-109-實作-applearmbacklight-服務與-dcs-0x51-螢幕亮度控制fuxibacklight)、[K-017](../items/K-kernel.zh-TW.md#k-017-背光模式-4applearmbacklightdcs-0x51) |
| `ksrc:tools/test-fuxi-battery-pause.py` | hw000-preparation.yml | [K-008](../items/K-kernel.zh-TW.md#k-008-電池查詢暫停get-pause實驗) |
| `ksrc:tools/test-fuxi-battery-prep.py` | kernel-adsp-battery.yml | [K-001](../items/K-kernel.zh-TW.md#k-001-adsp-開機與-glink-電池讀值)、[K-003](../items/K-kernel.zh-TW.md#k-003-單電芯電量換算)、[K-007](../items/K-kernel.zh-TW.md#k-007-電池欄位逐項過期ttl) |
| `ksrc:tools/test-fuxi-battery-publication.py` | hw000-preparation.yml、kernel-adsp-battery.yml | [K-002](../items/K-kernel.zh-TW.md#k-002-sweetbattery-發布真實電量)、[K-004](../items/K-kernel.zh-TW.md#k-004-充電器種類真實充電資訊與-10-秒刷新)、[K-005](../items/K-kernel.zh-TW.md#k-005-拔線測試讀值fuxiadsp4)、[K-007](../items/K-kernel.zh-TW.md#k-007-電池欄位逐項過期ttl)、[K-009](../items/K-kernel.zh-TW.md#k-009-sweetbattery-改走-iokit-work-loop-發布) |
| `ksrc:tools/test-fuxi-bluetooth-pump.py` | kernel-adsp-battery.yml | [K-077](../items/K-kernel.zh-TW.md#k-077-wcn7850-uartsk-與-skywalk-常駐收送) |
| `ksrc:tools/test-fuxi-bluetooth-setup.py` | kernel-adsp-battery.yml | [K-076](../items/K-kernel.zh-TW.md#k-076-wcn7850-藍牙韌體下載與-hci-resetfuxibt4) |
| `ksrc:tools/test-fuxi-bluetooth-xnu.py` | hw000-preparation.yml、kernel-adsp-battery.yml | [K-037](../items/K-kernel.zh-TW.md#k-037-藍牙-geni-fifo-與-ios-16-藍牙-abi-準備) |
| `ksrc:tools/test-fuxi-bluetooth.py` | kernel-adsp-battery.yml | [K-037](../items/K-kernel.zh-TW.md#k-037-藍牙-geni-fifo-與-ios-16-藍牙-abi-準備) |
| `ksrc:tools/test-fuxi-bt-board.py` | kernel-adsp-battery.yml | [K-039](../items/K-kernel.zh-TW.md#k-039-藍牙板級上電與-qca-版本查詢fuxibt3) |
| `ksrc:tools/test-fuxi-bt-clock.py` | hw000-preparation.yml | [K-038](../items/K-kernel.zh-TW.md#k-038-藍牙五條時脈租約準備) |
| `ksrc:tools/test-fuxi-bt-geni-fifo.py` | hw000-preparation.yml、kernel-adsp-battery.yml | [K-037](../items/K-kernel.zh-TW.md#k-037-藍牙-geni-fifo-與-ios-16-藍牙-abi-準備) |
| `ksrc:tools/test-fuxi-charge-evidence.py` | hw000-preparation.yml | [K-010](../items/K-kernel.zh-TW.md#k-010-充電證據工具) |
| `ksrc:tools/test-fuxi-compile-sched.py` |  | [S-097](../items/S-upstream.zh-TW.md#s-097-實作-fuxi-觸控編譯-cpu-加速推薦核心遮罩與熱壓力橋接fuxiperffuxitpfuxicores) |
| `ksrc:tools/test-fuxi-exec-inject.py` | kernel-adsp-battery.yml | [K-020](../items/K-kernel.zh-TW.md#k-020-核心在-exec-時注入函式庫fxinj補丁-0118)、[U-018](../items/U-userland.zh-TW.md#u-018-手動加入-wi-fi-時的密碼框等待)、[K-077](../items/K-kernel.zh-TW.md#k-077-wcn7850-uartsk-與-skywalk-常駐收送) |
| `ksrc:tools/test-fuxi-gpu-clock.py` |  | [S-098](../items/S-upstream.zh-TW.md#s-098-實作-nativeadreno-v2-使用者空間-gpu-abi時脈ddr-dcvs-與掃描輸出最佳化) |
| `ksrc:tools/test-fuxi-gpu-dcvs.py` |  | [S-098](../items/S-upstream.zh-TW.md#s-098-實作-nativeadreno-v2-使用者空間-gpu-abi時脈ddr-dcvs-與掃描輸出最佳化) |
| `ksrc:tools/test-fuxi-gpu-irq.py` |  | [S-098](../items/S-upstream.zh-TW.md#s-098-實作-nativeadreno-v2-使用者空間-gpu-abi時脈ddr-dcvs-與掃描輸出最佳化) |
| `ksrc:tools/test-fuxi-gpu-k1.py` |  | [S-098](../items/S-upstream.zh-TW.md#s-098-實作-nativeadreno-v2-使用者空間-gpu-abi時脈ddr-dcvs-與掃描輸出最佳化) |
| `ksrc:tools/test-fuxi-gpu-probe.py` |  | [S-096](../items/S-upstream.zh-TW.md#s-096-整合-adreno-a740-gpu-冷啟動pas13-韌體載入與非同步-2d-呈現fuxigpu) |
| `ksrc:tools/test-fuxi-gpu-scanout.py` |  | [S-108](../items/S-upstream.zh-TW.md#s-108-實作-sweetsurface-scanout-租約管理列對齊zero-copy-調查與-dpu-stage-3-直接掃描sweetsurfrow)、[G-050](../items/G-gpu.zh-TW.md#g-050-在-1080-寬面板顯示完整-1125-寬畫面並對齊觸控) |
| `ksrc:tools/test-fuxi-h1.py` |  | [S-103](../items/S-upstream.zh-TW.md#s-103-實作-fuxi-h1-amfi-相容層按鍵仲裁注入自動鎖屏ui-腳本導覽與-pmk8550-rtc-校時) |
| `ksrc:tools/test-fuxi-haptic-fifo.py` | hw000-preparation.yml | [K-034](../items/K-kernel.zh-TW.md#k-034-震動-fifo-準備主機模型) |
| `ksrc:tools/test-fuxi-haptic-pulse.py` | hw000-preparation.yml、kernel-adsp-battery.yml | [K-032](../items/K-kernel.zh-TW.md#k-032-hap525-震動盤點與有界脈衝) |
| `ksrc:tools/test-fuxi-haptic-runtime.py` | kernel-adsp-battery.yml | [K-033](../items/K-kernel.zh-TW.md#k-033-hap525-direct_play-震動執行期) |
| `ksrc:tools/test-fuxi-haptic-shape.py` | kernel-adsp-battery.yml | [U-021](../items/U-userland.zh-TW.md#u-021-從-ios-hal-的-actuator-輸出驅動-hap525) |
| `ksrc:tools/test-fuxi-haptic-survey.py` | hw000-preparation.yml、kernel-adsp-battery.yml | [K-025](../items/K-kernel.zh-TW.md#k-025-pmic-唯讀盤點flashhaptics) |
| `ksrc:tools/test-fuxi-hostimport.py` |  | [K-070](../items/K-kernel.zh-TW.md#k-070-nativeadreno-host-import-配額失敗回報-vulkan-oom) |
| `ksrc:tools/test-fuxi-htc-prep.py` | hw000-preparation.yml | [K-040](../items/K-kernel.zh-TW.md#k-040-wlan-htc-協定準備主機模型) |
| `ksrc:tools/test-fuxi-idle.py` |  | [S-105](../items/S-upstream.zh-TW.md#s-105-實作-cpu-負載ipiidle-l1-快取清理診斷禁用系統睡眠與-xpcproxy-身份保留fuxicpuloadfuxinosleepfuxixpcidfuxinoidleflush) |
| `ksrc:tools/test-fuxi-iig-cache.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-ios27.py` |  | [S-117](../items/S-upstream.zh-TW.md#s-117-xnu-27-與-kernel-libpthread-的各層補丁)、[T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-iosurface.py` |  | [S-108](../items/S-upstream.zh-TW.md#s-108-實作-sweetsurface-scanout-租約管理列對齊zero-copy-調查與-dpu-stage-3-直接掃描sweetsurfrow) |
| `ksrc:tools/test-fuxi-media-signal.py` | kernel-adsp-battery.yml | [K-077](../items/K-kernel.zh-TW.md#k-077-wcn7850-uartsk-與-skywalk-常駐收送) |
| `ksrc:tools/test-fuxi-memory.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-mesa-ir3.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-artifacts.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-backend.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-ca-offline.cpp` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-ca-offline.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-cache-hits.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-cache.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-defaults.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-draw-plans.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-frametrace.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-frontend.py` | frontend.yml、native-metal-host-tests.yml | [T-059](../items/T-tools.zh-TW.md#t-059-harness-gpu-時期的-ci-與主機測試2026-11-0811-14) |
| `ksrc:tools/test-fuxi-metal-m6.cpp` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-m6.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-m6b.cpp` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-m6b.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-m7.cpp` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-m7.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-m9a.cpp` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-m9a.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-m9b.cpp` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-m9b.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-m9g.cpp` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-m9g.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-m9h.cpp` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-m9h.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-m9i.cpp` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-m9i.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-m9k.cpp` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-m9k.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-m9l.cpp` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-m9l.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-m9m.cpp` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-m9m.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-m9n.cpp` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-m9n.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-m9o.cpp` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-m9o.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-m9p.cpp` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-m9p.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-memory.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-merge.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-mipfeedback.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-occlude.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-overlap.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-pipeline.cpp` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-pipeline.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-shadows.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-stage.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal-translation.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-metal.py` |  | [S-098](../items/S-upstream.zh-TW.md#s-098-實作-nativeadreno-v2-使用者空間-gpu-abi時脈ddr-dcvs-與掃描輸出最佳化) |
| `ksrc:tools/test-fuxi-metal2.py` |  | [S-098](../items/S-upstream.zh-TW.md#s-098-實作-nativeadreno-v2-使用者空間-gpu-abi時脈ddr-dcvs-與掃描輸出最佳化)、[S-100](../items/S-upstream.zh-TW.md#s-100-實作受閘控執行路徑代換測試路由與-nativeadreno-abi-效能除錯追蹤sweetexecswap) |
| `ksrc:tools/test-fuxi-metal2g.py` |  | [S-098](../items/S-upstream.zh-TW.md#s-098-實作-nativeadreno-v2-使用者空間-gpu-abi時脈ddr-dcvs-與掃描輸出最佳化) |
| `ksrc:tools/test-fuxi-metal2h.py` |  | [S-098](../items/S-upstream.zh-TW.md#s-098-實作-nativeadreno-v2-使用者空間-gpu-abi時脈ddr-dcvs-與掃描輸出最佳化) |
| `ksrc:tools/test-fuxi-mfb-align.py` | kernel-adsp-battery.yml | [K-077](../items/K-kernel.zh-TW.md#k-077-wcn7850-uartsk-與-skywalk-常駐收送) |
| `ksrc:tools/test-fuxi-nativemetal-bridge.cpp` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-nativemetal.py` | native-metal-host-tests.yml | [S-118](../items/S-upstream.zh-TW.md#s-118-上游-nativemetalmetal2vulkan-與-mesa-建置補丁)、[T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試)、[T-059](../items/T-tools.zh-TW.md#t-059-harness-gpu-時期的-ci-與主機測試2026-11-0811-14) |
| `ksrc:tools/test-fuxi-pac.py` |  | [S-114](../items/S-upstream.zh-TW.md#s-114-實作全核心-el1-pac-指針驗證硬體自我測試fuxigpuncimport-bit-25) |
| `ksrc:tools/test-fuxi-patch-stack.py` |  | [S-118](../items/S-upstream.zh-TW.md#s-118-上游-nativemetalmetal2vulkan-與-mesa-建置補丁)、[T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-perf.py` |  | [S-097](../items/S-upstream.zh-TW.md#s-097-實作-fuxi-觸控編譯-cpu-加速推薦核心遮罩與熱壓力橋接fuxiperffuxitpfuxicores) |
| `ksrc:tools/test-fuxi-prof.py` | kernel-adsp-battery.yml、kernel-backlight-dsi.yml、kernel-bo-slots-check.yml | [S-111](../items/S-upstream.zh-TW.md#s-111-實作可選使用者空間程序堆疊ipi-即時取樣剖析器sweetprof)、[G-021](../items/G-gpu.zh-TW.md#g-021-native-layout0018-部署m2v-00030004與-sweetprof-剖析) |
| `ksrc:tools/test-fuxi-ram-admission.py` | hw000-preparation.yml | [K-024](../items/K-kernel.zh-TW.md#k-024-多段-ram-admissionbank-shadow-與記憶體盤點) |
| `ksrc:tools/test-fuxi-ram-bank.py` | hw000-preparation.yml | [K-024](../items/K-kernel.zh-TW.md#k-024-多段-ram-admissionbank-shadow-與記憶體盤點) |
| `ksrc:tools/test-fuxi-ram-inventory.py` | kernel-adsp-battery.yml | [K-024](../items/K-kernel.zh-TW.md#k-024-多段-ram-admissionbank-shadow-與記憶體盤點) |
| `ksrc:tools/test-fuxi-ram-sku.py` | hw000-preparation.yml、kernel-adsp-battery.yml | [K-022](../items/K-kernel.zh-TW.md#k-022-ram-sku-與實際配置分開) |
| `ksrc:tools/test-fuxi-ram9g.py` | kernel-adsp-battery.yml | [K-021](../items/K-kernel.zh-TW.md#k-021-9-gib-ram-配置ram9g-變體) |
| `ksrc:tools/test-fuxi-ramtest.py` | kernel-adsp-battery.yml | [K-023](../items/K-kernel.zh-TW.md#k-023-高位址-ram-樣式測試ramtest) |
| `ksrc:tools/test-fuxi-regtrace.py` |  | [S-112](../items/S-upstream.zh-TW.md#s-112-實作-backboardd-ioregistry-查詢與-work-interval-耗時追蹤fuxiregtrace) |
| `ksrc:tools/test-fuxi-rpmh-shared.py` | kernel-adsp-battery.yml | [K-011](../items/K-kernel.zh-TW.md#k-011-adsp-與-wlan-共用-rpmh序列化與隔離)、[K-012](../items/K-kernel.zh-TW.md#k-012-adsp-與-wlan-聯合測試開關) |
| `ksrc:tools/test-fuxi-rtc-read.py` | hw000-preparation.yml、kernel-adsp-battery.yml | [K-036](../items/K-kernel.zh-TW.md#k-036-rtc-校時adt-fuxirtc-offset) |
| `ksrc:tools/test-fuxi-scm.py` |  | [S-096](../items/S-upstream.zh-TW.md#s-096-整合-adreno-a740-gpu-冷啟動pas13-韌體載入與非同步-2d-呈現fuxigpu) |
| `ksrc:tools/test-fuxi-sys.py` |  | [U-021](../items/U-userland.zh-TW.md#u-021-從-ios-hal-的-actuator-輸出驅動-hap525)、[S-107](../items/S-upstream.zh-TW.md#s-107-實作依-entitlement-授權特權與無容器程序-sandbox-路徑回應fuxiprivsfuxisbpath) |
| `ksrc:tools/test-fuxi-tap-command.py` | kernel-adsp-battery.yml | [K-035](../items/K-kernel.zh-TW.md#k-035-裝置端-ui-測試指令tapstourtapcmd) |
| `ksrc:tools/test-fuxi-thermal-summary.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-thermald.py` |  | [S-097](../items/S-upstream.zh-TW.md#s-097-實作-fuxi-觸控編譯-cpu-加速推薦核心遮罩與熱壓力橋接fuxiperffuxitpfuxicores) |
| `ksrc:tools/test-fuxi-torch-audit.py` | hw000-preparation.yml、kernel-adsp-battery.yml | [K-028](../items/K-kernel.zh-TW.md#k-028-手電筒失敗追查關閉穩定計時器audit原始拒絕) |
| `ksrc:tools/test-fuxi-torch-pulse.py` | hw000-preparation.yml、kernel-adsp-battery.yml | [K-026](../items/K-kernel.zh-TW.md#k-026-手電筒有界脈衝診斷mode-2) |
| `ksrc:tools/test-fuxi-torch-runtime.py` | kernel-adsp-battery.yml | [K-027](../items/K-kernel.zh-TW.md#k-027-手電筒租約控制mode-3與-springboard-放行) |
| `ksrc:tools/test-fuxi-torch-steady.py` | hw000-preparation.yml、kernel-adsp-battery.yml | [K-030](../items/K-kernel.zh-TW.md#k-030-手電筒常亮) |
| `ksrc:tools/test-fuxi-touch.py` |  | [S-103](../items/S-upstream.zh-TW.md#s-103-實作-fuxi-h1-amfi-相容層按鍵仲裁注入自動鎖屏ui-腳本導覽與-pmk8550-rtc-校時)、[G-050](../items/G-gpu.zh-TW.md#g-050-在-1080-寬面板顯示完整-1125-寬畫面並對齊觸控) |
| `ksrc:tools/test-fuxi-turnip-hostimport.py` |  | [S-118](../items/S-upstream.zh-TW.md#s-118-上游-nativemetalmetal2vulkan-與-mesa-建置補丁)、[T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-turnip-submit.py` |  | [S-118](../items/S-upstream.zh-TW.md#s-118-上游-nativemetalmetal2vulkan-與-mesa-建置補丁)、[T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-unixnu-port2.py` |  | [S-106](../items/S-upstream.zh-TW.md#s-106-整合-unixnu-顯示ufs-安全策略120-hz-te-同步掃描輸出追蹤與-mtdblk-獨立紀錄fx_log)、[S-110](../items/S-upstream.zh-TW.md#s-110-移植-unixnu-union-lower-directory-查找資格修正fuxiuniondir) |
| `ksrc:tools/test-fuxi-unixnu.py` |  | [S-106](../items/S-upstream.zh-TW.md#s-106-整合-unixnu-顯示ufs-安全策略120-hz-te-同步掃描輸出追蹤與-mtdblk-獨立紀錄fx_log)、[G-050](../items/G-gpu.zh-TW.md#g-050-在-1080-寬面板顯示完整-1125-寬畫面並對齊觸控) |
| `ksrc:tools/test-fuxi-vk-smoke.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-vulkan.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-fuxi-wifi-ios.py` |  | [S-104](../items/S-upstream.zh-TW.md#s-104-實作-ios-apple80211ionetworkstack-wi-fi-模擬與原生-wcn7850-站台對接層fuxiwifiios)、[K-053](../items/K-kernel.zh-TW.md#k-053-ios-station-後端電源與掃描) |
| `ksrc:tools/test-fuxi-wlan-ath12k.py` | hw000-preparation.yml、kernel-adsp-battery.yml | [K-049](../items/K-kernel.zh-TW.md#k-049-ath12k-板級轉接層)、[K-050](../items/K-kernel.zh-TW.md#k-050-ath12k-模組連結與-ci) |
| `ksrc:tools/test-fuxi-wlan-bdf-inventory.py` | hw000-preparation.yml | [K-048](../items/K-kernel.zh-TW.md#k-048-wlan-bdf-來源盤點) |
| `ksrc:tools/test-fuxi-wlan-control.c` |  | [K-051](../items/K-kernel.zh-TW.md#k-051-隔離-native-imports-與控制通道) |
| `ksrc:tools/test-fuxi-wlan-ios.py` | hw000-preparation.yml、kernel-adsp-battery.yml | [K-053](../items/K-kernel.zh-TW.md#k-053-ios-station-後端電源與掃描)、[K-069](../items/K-kernel.zh-TW.md#k-069-掃描中止逾時恢復與公布頻道掃描bit-56)、[K-073](../items/K-kernel.zh-TW.md#k-073-優先處理-wi-fi-關閉批次接收與連線統計bit-79) |
| `ksrc:tools/test-fuxi-wlan-m5.py` |  | [E-049](../items/E-early.zh-TW.md#e-049-wcn7850-原廠韌體路線的-m5w17-主機模型)、[S-099](../items/S-upstream.zh-TW.md#s-099-實作-wcn7850-pcie0mhibhi-探索qmi-wlfw-帶起與原生-ath12k-wi-fi-ios-站台橋接fuxiwlan) |
| `ksrc:tools/test-fuxi-wlan-m6-artifacts.py` |  | [E-049](../items/E-early.zh-TW.md#e-049-wcn7850-原廠韌體路線的-m5w17-主機模型)、[S-099](../items/S-upstream.zh-TW.md#s-099-實作-wcn7850-pcie0mhibhi-探索qmi-wlfw-帶起與原生-ath12k-wi-fi-ios-站台橋接fuxiwlan) |
| `ksrc:tools/test-fuxi-wlan-m6.py` |  | [E-049](../items/E-early.zh-TW.md#e-049-wcn7850-原廠韌體路線的-m5w17-主機模型)、[S-099](../items/S-upstream.zh-TW.md#s-099-實作-wcn7850-pcie0mhibhi-探索qmi-wlfw-帶起與原生-ath12k-wi-fi-ios-站台橋接fuxiwlan) |
| `ksrc:tools/test-fuxi-wlan-m6i.py` | hw000-preparation.yml、kernel-adsp-battery.yml | [E-049](../items/E-early.zh-TW.md#e-049-wcn7850-原廠韌體路線的-m5w17-主機模型)、[K-011](../items/K-kernel.zh-TW.md#k-011-adsp-與-wlan-共用-rpmh序列化與隔離) |
| `ksrc:tools/test-fuxi-wlan-m6k.py` |  | [E-049](../items/E-early.zh-TW.md#e-049-wcn7850-原廠韌體路線的-m5w17-主機模型)、[S-099](../items/S-upstream.zh-TW.md#s-099-實作-wcn7850-pcie0mhibhi-探索qmi-wlfw-帶起與原生-ath12k-wi-fi-ios-站台橋接fuxiwlan) |
| `ksrc:tools/test-fuxi-wlan-m6l.py` |  | [E-049](../items/E-early.zh-TW.md#e-049-wcn7850-原廠韌體路線的-m5w17-主機模型)、[S-099](../items/S-upstream.zh-TW.md#s-099-實作-wcn7850-pcie0mhibhi-探索qmi-wlfw-帶起與原生-ath12k-wi-fi-ios-站台橋接fuxiwlan) |
| `ksrc:tools/test-fuxi-wlan-m6m.py` |  | [E-049](../items/E-early.zh-TW.md#e-049-wcn7850-原廠韌體路線的-m5w17-主機模型)、[S-099](../items/S-upstream.zh-TW.md#s-099-實作-wcn7850-pcie0mhibhi-探索qmi-wlfw-帶起與原生-ath12k-wi-fi-ios-站台橋接fuxiwlan) |
| `ksrc:tools/test-fuxi-wlan-m6n.py` |  | [K-046](../items/K-kernel.zh-TW.md#k-046-aop-pdc-設定) |
| `ksrc:tools/test-fuxi-wlan-m6o.py` |  | [E-049](../items/E-early.zh-TW.md#e-049-wcn7850-原廠韌體路線的-m5w17-主機模型)、[S-099](../items/S-upstream.zh-TW.md#s-099-實作-wcn7850-pcie0mhibhi-探索qmi-wlfw-帶起與原生-ath12k-wi-fi-ios-站台橋接fuxiwlan) |
| `ksrc:tools/test-fuxi-wlan-rails.py` | hw000-preparation.yml、kernel-adsp-battery.yml | [E-049](../items/E-early.zh-TW.md#e-049-wcn7850-原廠韌體路線的-m5w17-主機模型)、[K-011](../items/K-kernel.zh-TW.md#k-011-adsp-與-wlan-共用-rpmh序列化與隔離) |
| `ksrc:tools/test-fuxi-wlan-w10.py` |  | [E-049](../items/E-early.zh-TW.md#e-049-wcn7850-原廠韌體路線的-m5w17-主機模型)、[S-099](../items/S-upstream.zh-TW.md#s-099-實作-wcn7850-pcie0mhibhi-探索qmi-wlfw-帶起與原生-ath12k-wi-fi-ios-站台橋接fuxiwlan) |
| `ksrc:tools/test-fuxi-wlan-w11.py` |  | [E-049](../items/E-early.zh-TW.md#e-049-wcn7850-原廠韌體路線的-m5w17-主機模型)、[S-099](../items/S-upstream.zh-TW.md#s-099-實作-wcn7850-pcie0mhibhi-探索qmi-wlfw-帶起與原生-ath12k-wi-fi-ios-站台橋接fuxiwlan) |
| `ksrc:tools/test-fuxi-wlan-w12.py` |  | [E-049](../items/E-early.zh-TW.md#e-049-wcn7850-原廠韌體路線的-m5w17-主機模型)、[S-099](../items/S-upstream.zh-TW.md#s-099-實作-wcn7850-pcie0mhibhi-探索qmi-wlfw-帶起與原生-ath12k-wi-fi-ios-站台橋接fuxiwlan) |
| `ksrc:tools/test-fuxi-wlan-w13.py` |  | [E-049](../items/E-early.zh-TW.md#e-049-wcn7850-原廠韌體路線的-m5w17-主機模型)、[S-099](../items/S-upstream.zh-TW.md#s-099-實作-wcn7850-pcie0mhibhi-探索qmi-wlfw-帶起與原生-ath12k-wi-fi-ios-站台橋接fuxiwlan)、[K-041](../items/K-kernel.zh-TW.md#k-041-w18-追蹤與原廠韌體-fw_ready-停損) |
| `ksrc:tools/test-fuxi-wlan-w14.py` |  | [E-049](../items/E-early.zh-TW.md#e-049-wcn7850-原廠韌體路線的-m5w17-主機模型)、[S-099](../items/S-upstream.zh-TW.md#s-099-實作-wcn7850-pcie0mhibhi-探索qmi-wlfw-帶起與原生-ath12k-wi-fi-ios-站台橋接fuxiwlan) |
| `ksrc:tools/test-fuxi-wlan-w15-audit.py` |  | [E-049](../items/E-early.zh-TW.md#e-049-wcn7850-原廠韌體路線的-m5w17-主機模型)、[S-099](../items/S-upstream.zh-TW.md#s-099-實作-wcn7850-pcie0mhibhi-探索qmi-wlfw-帶起與原生-ath12k-wi-fi-ios-站台橋接fuxiwlan) |
| `ksrc:tools/test-fuxi-wlan-w15.py` |  | [K-047](../items/K-kernel.zh-TW.md#k-047-上游韌體睡眠時脈wlan_mode-通過)、[E-049](../items/E-early.zh-TW.md#e-049-wcn7850-原廠韌體路線的-m5w17-主機模型)、[S-099](../items/S-upstream.zh-TW.md#s-099-實作-wcn7850-pcie0mhibhi-探索qmi-wlfw-帶起與原生-ath12k-wi-fi-ios-站台橋接fuxiwlan) |
| `ksrc:tools/test-fuxi-wlan-w16.py` |  | [E-049](../items/E-early.zh-TW.md#e-049-wcn7850-原廠韌體路線的-m5w17-主機模型)、[S-099](../items/S-upstream.zh-TW.md#s-099-實作-wcn7850-pcie0mhibhi-探索qmi-wlfw-帶起與原生-ath12k-wi-fi-ios-站台橋接fuxiwlan) |
| `ksrc:tools/test-fuxi-wlan-w17.py` |  | [E-049](../items/E-early.zh-TW.md#e-049-wcn7850-原廠韌體路線的-m5w17-主機模型)、[K-044](../items/K-kernel.zh-TW.md#k-044-pmk8550-睡眠時脈與大塊記憶體p17)、[K-045](../items/K-kernel.zh-TW.md#k-045-errdecwlfw-逾時後統計韌體寫過的主機記憶體) |
| `ksrc:tools/test-fuxi-wlan-w18.py` | hw000-preparation.yml、kernel-adsp-battery.yml | [K-041](../items/K-kernel.zh-TW.md#k-041-w18-追蹤與原廠韌體-fw_ready-停損) |
| `ksrc:tools/test-ios27-prelink.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-ios27-root.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-launchd-on-demand.py` |  | [T-058](../items/T-tools.zh-TW.md#t-058-ksrc-繼承的-gpu建置與其他主機測試) |
| `ksrc:tools/test-sweet-root-tweaks.py` |  | [U-011](../items/U-userland.zh-TW.md#u-011-安裝-32-個原廠內建-app) |
| `ksrc:tools/xnuprelink/test_codeless.py` |  | [S-117](../items/S-upstream.zh-TW.md#s-117-xnu-27-與-kernel-libpthread-的各層補丁) |
| `ksrc:tools/xnuprelink/test_emit_prelink.py` |  | [S-117](../items/S-upstream.zh-TW.md#s-117-xnu-27-與-kernel-libpthread-的各層補丁) |
| `ksrc:tools/xnuprelink/test_fileset_emit.py` |  | [S-117](../items/S-upstream.zh-TW.md#s-117-xnu-27-與-kernel-libpthread-的各層補丁) |
| `ksrc:tools/xnuprelink/test_fileset_layout.py` |  | [S-117](../items/S-upstream.zh-TW.md#s-117-xnu-27-與-kernel-libpthread-的各層補丁) |
| `ksrc:tools/xnuprelink/test_fileset_match.py` |  | [S-117](../items/S-upstream.zh-TW.md#s-117-xnu-27-與-kernel-libpthread-的各層補丁) |
| `ksrc:tools/xnuprelink/test_fileset_shape.py` |  | [S-117](../items/S-upstream.zh-TW.md#s-117-xnu-27-與-kernel-libpthread-的各層補丁) |
| `ksrc:tools/xnuprelink/test_fuxi_changes.py` |  | [S-117](../items/S-upstream.zh-TW.md#s-117-xnu-27-與-kernel-libpthread-的各層補丁) |
| `ksrc:tools/xnuprelink/test_native_prelink.py` |  | [S-117](../items/S-upstream.zh-TW.md#s-117-xnu-27-與-kernel-libpthread-的各層補丁) |
| `ksrc:xnu/fuxi-audio-prep/test_gpr.c` |  | [K-015](../items/K-kernel.zh-TW.md#k-015-gpr-音訊準備主機模型) |
| `ksrc:xnu/fuxi-battery-prep/test_freshness.c` |  | [K-007](../items/K-kernel.zh-TW.md#k-007-電池欄位逐項過期ttl) |
| `ksrc:xnu/fuxi-haptic-prep/test_fifo.c` |  | [K-034](../items/K-kernel.zh-TW.md#k-034-震動-fifo-準備主機模型) |
| `ksrc:xnu/fuxi-ram-prep/test_fuxi_ram_bank.c` |  | [K-024](../items/K-kernel.zh-TW.md#k-024-多段-ram-admissionbank-shadow-與記憶體盤點) |
| `harness:tests/FuxiNocopyObservationTests.cpp` | native-metal-host-tests.yml、nocopy-observation-check.yml | [T-059](../items/T-tools.zh-TW.md#t-059-harness-gpu-時期的-ci-與主機測試2026-11-0811-14) |
| `harness:tests/NativePortTests.mm` | native-metal-host-tests.yml | [T-059](../items/T-tools.zh-TW.md#t-059-harness-gpu-時期的-ci-與主機測試2026-11-0811-14) |
| `harness:tests/NocopyDeviceControls.cpp` | unwind-register-check.yml | [T-059](../items/T-tools.zh-TW.md#t-059-harness-gpu-時期的-ci-與主機測試2026-11-0811-14) |
| `harness:tests/NocopyMachOUUID.h` | nocopy-observation-check.yml | [T-059](../items/T-tools.zh-TW.md#t-059-harness-gpu-時期的-ci-與主機測試2026-11-0811-14) |
| `harness:tests/NocopyMachOUUIDTests.cpp` | unwind-register-check.yml | [T-059](../items/T-tools.zh-TW.md#t-059-harness-gpu-時期的-ci-與主機測試2026-11-0811-14) |
| `harness:tests/NocopyObjectMetadata.h` | nocopy-observation-check.yml | [T-059](../items/T-tools.zh-TW.md#t-059-harness-gpu-時期的-ci-與主機測試2026-11-0811-14) |
| `harness:tests/NocopyObjectMetadataTests.cpp` | unwind-register-check.yml | [T-059](../items/T-tools.zh-TW.md#t-059-harness-gpu-時期的-ci-與主機測試2026-11-0811-14) |
| `harness:tests/NocopySafeRead.h` | nocopy-observation-check.yml | [T-059](../items/T-tools.zh-TW.md#t-059-harness-gpu-時期的-ci-與主機測試2026-11-0811-14) |
| `harness:tests/NocopySafeReadTests.cpp` | unwind-register-check.yml | [T-059](../items/T-tools.zh-TW.md#t-059-harness-gpu-時期的-ci-與主機測試2026-11-0811-14) |
| `harness:tests/UnwindRegisterTests.cpp` | unwind-register-check.yml | [T-059](../items/T-tools.zh-TW.md#t-059-harness-gpu-時期的-ci-與主機測試2026-11-0811-14) |
| `harness:tests/test-ath12k-scan-dwell.py` | ath12k-objects.yml | [K-060](../items/K-kernel.zh-TW.md#k-060-協商掃描停留時間與-fast-掃描快取) |
| `harness:tests/test-backlight-dsi.py` | kernel-adsp-battery.yml、kernel-backlight-dsi.yml | [K-017](../items/K-kernel.zh-TW.md#k-017-背光模式-4applearmbacklightdcs-0x51)、[K-018](../items/K-kernel.zh-TW.md#k-018-dsi-錯誤容忍與等滿一幀)、[T-059](../items/T-tools.zh-TW.md#t-059-harness-gpu-時期的-ci-與主機測試2026-11-0811-14) |
| `harness:tests/test-bo-slots.py` | kernel-adsp-battery.yml、kernel-backlight-dsi.yml、kernel-bo-slots-check.yml | [T-059](../items/T-tools.zh-TW.md#t-059-harness-gpu-時期的-ci-與主機測試2026-11-0811-14) |
| `harness:tests/test-ib-profile.py` | kernel-adsp-battery.yml、kernel-backlight-dsi.yml、kernel-bo-slots-check.yml | [T-059](../items/T-tools.zh-TW.md#t-059-harness-gpu-時期的-ci-與主機測試2026-11-0811-14) |
| `harness:tests/test-native-patch-stack.py` | native-metal-check.yml、native-metal-host-tests.yml | [T-059](../items/T-tools.zh-TW.md#t-059-harness-gpu-時期的-ci-與主機測試2026-11-0811-14) |
| `harness:tests/test-perf-governor.py` | kernel-adsp-battery.yml、kernel-backlight-dsi.yml、kernel-bo-slots-check.yml | [T-059](../items/T-tools.zh-TW.md#t-059-harness-gpu-時期的-ci-與主機測試2026-11-0811-14) |
| `harness:tests/test-surface-plane-result.py` | kernel-adsp-battery.yml、kernel-backlight-dsi.yml、kernel-bo-slots-check.yml、kernel-surface-check.yml | [T-059](../items/T-tools.zh-TW.md#t-059-harness-gpu-時期的-ci-與主機測試2026-11-0811-14) |
| `harness:tests/test-surfrow-mode2.py` | kernel-adsp-battery.yml、kernel-backlight-dsi.yml、kernel-bo-slots-check.yml | [T-059](../items/T-tools.zh-TW.md#t-059-harness-gpu-時期的-ci-與主機測試2026-11-0811-14) |
| `harness:tools/test-bluetooth-patch.c` | sb-shape.yml | [U-023](../items/U-userland.zh-TW.md#u-023-bluetoothd-相容修補關藍牙閃退與單實例-ble-廣播fuxibt-fix) |
| `harness:tools/test-flashlight-levels.c` | sb-shape.yml | [K-031](../items/K-kernel.zh-TW.md#k-031-手電筒調光不重開輸出與五檔)、[U-003](../items/U-userland.zh-TW.md#u-003-ios-手電筒控制接上核心租約與五檔亮度) |
| `harness:tools/test-oslog-format.c` | sb-shape.yml | [U-022](../items/U-userland.zh-TW.md#u-022-bluetoothd-os_log-轉錄到-consolefuxibt-oslog) |
| `harness:tools/test-vibe-pattern.c` | sb-shape.yml | [K-033](../items/K-kernel.zh-TW.md#k-033-hap525-direct_play-震動執行期)、[U-004](../items/U-userland.zh-TW.md#u-004-ios-震動交給核心-hap525-執行期mediaserverdfigvibratormediaexperience) |
| `harness:tools/test-vibe-worker.py` | sb-shape.yml | [U-004](../items/U-userland.zh-TW.md#u-004-ios-震動交給核心-hap525-執行期mediaserverdfigvibratormediaexperience) |
| `ws:dualboot/tests/host.c` |  | [B-004](../items/B-boot.zh-TW.md#b-004-雙系統開機選單boot_a-常駐)、[B-011](../items/B-boot.zh-TW.md#b-011-a-槽選單顯示倒數10-秒後自動進-android) |
| `ws:dualboot/tests/test-remove-ksu.py` |  | [B-005](../items/B-boot.zh-TW.md#b-005-移除-ksu-模組檢查移到主機) |
| `ws:work/adsp-battery/test-amp-run-cs35l41.py` |  | [K-078](../items/K-kernel.zh-TW.md#k-078-用目前-adsp-驗證-fuxi-喇叭與播放圖) |
| `ws:work/adsp-battery/test-amp-tone-cs35l41.py` |  | [K-078](../items/K-kernel.zh-TW.md#k-078-用目前-adsp-驗證-fuxi-喇叭與播放圖) |
| `ws:work/adsp-battery/test-haptic-apid.py` |  | [K-033](../items/K-kernel.zh-TW.md#k-033-hap525-direct_play-震動執行期) |
| `ws:work/adsp-battery/test-muted-cs35l41.py` |  | [K-078](../items/K-kernel.zh-TW.md#k-078-用目前-adsp-驗證-fuxi-喇叭與播放圖) |
| `ws:work/adsp-battery/test-rtc-calibration.py` |  | [K-036](../items/K-kernel.zh-TW.md#k-036-rtc-校時adt-fuxirtc-offset) |
| `ws:work/boot-chain/test-deploy-recovery.py` |  | [B-002](../items/B-boot.zh-TW.md#b-002-已驗證部署與還原deploy-verifiedprepare-test) |
| `ws:work/boot-chain/test-loader-entry-ram.py` |  | [B-003](../items/B-boot.zh-TW.md#b-003-loader-的-ram-sku-預檢與-ram-入口防護) |
| `ws:work/boot-chain/test-loader-sku-preflight.py` |  | [B-001](../items/B-boot.zh-TW.md#b-001-b-槽開機分流器-chain-與版本史chain-v1v9)、[B-003](../items/B-boot.zh-TW.md#b-003-loader-的-ram-sku-預檢與-ram-入口防護)、[B-010](../items/B-boot.zh-TW.md#b-010-loader-安全核對)、[K-022](../items/K-kernel.zh-TW.md#k-022-ram-sku-與實際配置分開) |
| `ws:work/boot-chain/test-runtime-ram-profile.py` |  | [B-003](../items/B-boot.zh-TW.md#b-003-loader-的-ram-sku-預檢與-ram-入口防護) |
| `ws:work/statusbar-fix/test-launcher-framebuffer.ps1` |  | [K-035](../items/K-kernel.zh-TW.md#k-035-裝置端-ui-測試指令tapstourtapcmd)、[T-048](../items/T-tools.zh-TW.md#t-048-手機啟動器-launch-loader-checkps1) |
| `ws:work/statusbar-fix/test-root-layers.py` |  | [B-009](../items/B-boot.zh-TW.md#b-009-上機前差分備份工具)、[U-014](../items/U-userland.zh-TW.md#u-014-ios-root-疊加層快照底檔hfs-journal-重播與差分寫入) |
| `ws:work/statusbar-fix/test-stage-wlan-assets.py` |  | [U-013](../items/U-userland.zh-TW.md#u-013-wlan-韌體與-bdf-放進-ios-root)、[U-020](../items/U-userland.zh-TW.md#u-020-wcn7850-藍牙韌體放進-ios-rootstage-wlan-assets-kindbt) |
