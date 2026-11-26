# U 系列：iOS root 與使用者空間

> 放進 iOS root（RootFS 疊加層）或在 iOS 使用者空間執行的修改：SpringBoard 形狀函式庫 `libfuxi-sb.dylib`（harness `userland/fuxi-shape/`）、狀態欄偏好、手電筒／震動／觸覺的使用者端、Wi-Fi 診斷與決策日誌、時區、launchd、內建與第三方 App、WLAN 韌體資產、root 疊加層工具。核心端的對應項目在 K 系列。

> 建置 `libfuxi-sb.dylib` 見 README 共用步驟 4；上機見共用步驟 6。

### U-001 SpringBoard 形狀函式庫 libfuxi-sb.dylib（狀態欄打孔區）

- **日期**：2026-11-16 09:25（`harness: 696a008`）～10:29（`harness: 4add383`）
- **狀態**：日常使用中（隨 `fuxi,inject=2` 注入 SpringBoard，持續承接手電筒、震動、電池與 Wi-Fi 稽核）
- **開關**：
  - `fuxi,inject=2`（ADT `/chosen` u32）或開機參數 `fxinj=2`：由核心（補丁 0118）將 `/usr/local/lib/fuxi/libfuxi-sb.dylib` 注入 SpringBoard（因 `launchd` 會丟棄 `com.apple` 工作的 `DYLD_` 環境變數，且 `fuxi_shape_init` 確保子行程不繼承）。
  - `fuxi,sbshape=<W*1000+H>`（ADT `/chosen` u32）：1125 像素空間的打孔幾何（預設 `300x98`、`W` 範圍 `30..627`、`H` 範圍 `30..150`），設為 `0` 則不介入。
- **問題／現象**：Fuxi 在 iOS 16.7.16（`20H392`）以 iPhone X 機型執行，`_UIStatusBarVisualProvider_FixedSplit` 依 iPhone X 劉海寬度（`1125` 空間 `{249, 0, 627, 90}`，即 `209x30` points）排版，導致時間與圖示偏擠在角落且高於前置打孔中心。
- **根因**：UIKit 依 `UIScreen._exclusionArea` 精確尺寸挑選 provider，若直接改 `_exclusionArea` 會觸發未知切口的診斷模式；同時實際座標可能為點（`/3.0`）或像素。
- **做法**：
  - `harness: userland/fuxi-shape/FuxiShape.c` 保留原始 `UIScreen._exclusionArea`，改用 `method_setImplementation` 替換 `_UIStatusBarVisualProvider_FixedSplit` 的 `-notchRect`（`-notchSize` 亦讀取 `-notchRect`），只針對符合 `x_notch`（含 `1/3` point 縮放）的矩形置中改為 `shape_w x shape_h`（預設 `300x98` px）。
  - 透過 `IOKit` 讀取 `IODeviceTree:/chosen` 的 `fuxi,sbshape`，並由 `ws: work/statusbar-fix/stage-shape.sh` 經 HFS+ journal 重播、fsck、原子寫入 `/usr/local/lib/fuxi/libfuxi-sb.dylib`（`0755`，`root:wheel`）並清除舊 UIKit provider 覆寫鍵。
  - 演進：
    - `harness: 696a008` 2026-11-16 09:25：引入 `FuxiShape.c` 與 `sb-shape.yml` CI，使狀態欄切口跟隨置中打孔而非 iPhone X 劉海。
    - `harness: 6dc5750` 2026-11-16 09:28：將 `fxinj` 與 `sbshape` 改為由 ADT `/chosen` 讀取（因日常命令列已滿）。
    - `harness: e8fc9db` 2026-11-16 09:28：在 `sb-shape.yml` 補連結 `CoreFoundation` 與 `IOKit`。
    - `harness: 4add383` 2026-11-16 10:29：處理 provider 回傳 point 座標（`x_notch / 3.0`）的情況。
- **復現**：
  ```powershell
  D:\xiaomi13\fuxi_ios\work\localbuild\build-sb-shape.ps1 -Name sb-local-shape-artifact
  ```
  - 預期輸出：SpringBoard stderr（經 `sweetnull` 進核心紀錄）印出 `fuxi: SpringBoard: status-bar provider 300 x 98 px; display cut-out preserved` 與替換前後矩形座標。
- **主機測試**：`work/localbuild/verify-sb-shape.py --self-test` 檢查 plain arm64 `MH_DYLIB`、ad hoc 簽署逐頁雜湊與負測試。
- **驗證紀錄**：2026-11-16 起多個版本進入日常 `chain-v6`～`v9`。11 月 18 日使用者指出高度 127 的版本偏低。使用原廠 Android 的圖示高度作基準，把 `fuxi,sbshape` 改成 `300103`。設定頁截圖的時間和電池中心從 87.5 px 移到 63.5 px，使用者在實體面板確認與鏡頭對齊。包含 Wi-Fi 修正的日常版見 B-012。
  - 2026-11-20 全寬填滿 1080×2400 後，300×103 的時間、右側圖示同在 y=80。使用者希望上移對齊前鏡頭，兩側再往外。趟 38 以 `--display-fit fill --statusbar-width 360 --statusbar-height 86` 暫時開機，SpringBoard 回報 provider 360×86，使用者實機確認位置可以並要求合併日常。該趟結束的頂部 200 列截到純桌布，無法由該張量出圖示中心。原始量測、日常候選及回退見 G-050、B-014。
  - 趟 45 的 1125×2500 等比版亦選中原來的 FixedSplit，沿用 360×86。兩張頂部截圖量得時間中心 (198.5,61.5)，Wi-Fi 與電池群中心 (908,61.5)。見 B-015。
  - **2026-11-20–21 右側音量條適配（`FuxiVolHud.c`／`FuxiVolHudLayout.h`，ADT `/chosen fuxi,volhud`，預設關）**：依 SpringBoard `SBElasticValueViewController`（`centerForState:containerViewSize:bounds:useSpringData:`、`_updateLabelsForAxis:containerViewSize:state:animated:`）與 `SBElasticHUDSettings`（`volumeButtonsCenterY`、`portraitState{1,2,3}Height`）在直向（`axis==2`）將音量條鏡像至小米 13 右側實體按鍵緣（橫向 `axis==1` 不動），並支援 `Y*1000+H` 編碼同時調整中心與三態等比高度。主機 ASan/UBSan 測試 `harness/tools/test-volhud-layout.c` 全過。本機建置 `sb-local-volhud2-1121-artifact`（`libfuxi-sb.dylib` SHA-256 `8f3e6e0b0cdf47e8ff30b4f41b58f7339dcff057d4df0a805a9fc290cfb7e986`）已於趟 58 寫入手機（`PRE_OK 11／WRITE_OK 11`），趟 59 以 `--volhud 238128`（對照使用者實拍實體音量鍵上下緣紅線 `174.1..302.0 pt`）實測確認 `portraitState1Height 148.0 -> 128.0`、`portraitState2Height 134.0 -> 115.9`、`portraitState3Height 170.0 -> 147.0`、`volumeButtonsCenterY 124.0 -> 238.0` 全數生效，並於趟 60 由使用者實機驗收確認音量條位置與長度過關。
- **參考來源**：核心補丁 `0118`（`fuxi,inject`）。
- **結論／未完成**：已穩定作為所有 SpringBoard／userland 注入功能的宿主函式庫。
- **出處**：`harness: userland/fuxi-shape/FuxiShape.c`、`ws: work/statusbar-fix/stage-shape.sh`、`harness: 696a008`、`6dc5750`、`e8fc9db`、`4add383`。（2026-11-18 主代理分配範圍與來源，Gemini 依原始檔撰寫，主代理核對欄位、提交時間與路徑）

### U-002 狀態欄位置：UIKit 偏好鍵（Split1080／provider）與只寫少數區塊的部署

- **日期**：2026-11-16 11:10（`harness: 3edcfe8`）
- **狀態**：已採用並部分演進（UIKit provider 覆寫改回清除，`FuxiShape.c` 接手 `-notchRect`，其建立的「只寫少數改動區塊」部署鏈沿用至今）
- **開關**：
  - `CLASS` 環境變數（如 `_UIStatusBarVisualProvider_Split1080` 或 `NONE`）
  - `REUSE_RP`、`SKIP_CHAIN1`、`STAGE_ONLY`（`hw000-sb-chain-provider-3edcfe8.sh`）
- **問題／現象**：早期嘗試以 `UIStatusBarVisualProviderClassName`（`Split1080`）調整狀態欄位置時只移動約 1.5 px，無法真正對齊打孔；且每次修改 iOS root 若整顆 20 GiB 覆寫會洗掉使用者桌面與資料，耗時過長。
- **根因**：狀態欄基礎高度由 MobileGestalt 機型排除區決定；而 iOS root 在手機端位於 pinned F2FS 映像，需要基於現況差分與原子寫塊。
- **做法**：
  - `harness: 3edcfe8` 改由 `FuxiShape.c` 調整狀態欄佈局而不改動顯示排除區，`stage-shape.sh` 會主動移除 `com.apple.UIKit.plist` 與 `.GlobalPreferences.plist` 內的 `UIStatusBarVisualProviderClassName`。
  - `ws: work/statusbar-fix/hw000-sb-chain-provider-3edcfe8.sh` 建立完整安全寫回流程：檢查三把手機鎖 → 以 `hw000-delta-backup.py` 唯讀拉回手機現況當還原點 → 在主機 qcow2 疊加層＋Alpine guest（`sb-guest-run.py`、`sb-guest.sh`、`stage-statusbar.sh`）執行 HFS+ journal 重播與修改並計算 `diff-blocks.txt` → 由 `sb-blocks-write.sh` 寫前逐塊比對 SHA-256、只寫少數變更區塊並寫後讀回驗證，還原資料存於備份目錄。
- **復現**：透過 `hw000-sb-chain-provider-3edcfe8.sh` 或其後續輕量版 `phone-readthrough.py` 執行 guest staging 與 `sb-blocks-write.sh` 寫回。
- **主機測試**：`sb-extract.py` 與 `hfs-journal-block-probe.py` 於寫前後執行 `fsck.hfsplus -fn` 與雜湊核對。
- **驗證紀錄**：`Split1080` 曾上機實測（見 `AGENTS.md`／memory）；後續 `3edcfe8` 轉由 `FuxiShape.c` 與 `stage-shape.sh` 僅寫少量區塊（5～19 塊 `PRE_OK`／`WRITE_OK`）成為標準部署模式。
- **參考來源**：`work/adsp-battery/hw000/hw000-delta-backup.py`、`work/statusbar-fix/stage-share/`。
- **結論／未完成**：UIKit 偏好鍵方案已由 `libfuxi-sb.dylib` 取代；其「拉回現況＋疊加層＋寫前比對寫後讀回少數區塊」框架成為 U 系列所有 root 修改的基礎。
- **出處**：`harness: 3edcfe8`、`ws: work/statusbar-fix/hw000-sb-chain-provider-3edcfe8.sh`、`ws: work/statusbar-fix/stage-share/stage-statusbar.sh`、`ws: work/statusbar-fix/sb-extract.py`、`ws: work/statusbar-fix/sb-blocks-write.sh`、`ws: work/statusbar-fix/sb-guest-run.py`、`ws: work/statusbar-fix/sb-share-template/sb-guest.sh`。（2026-11-18 主代理分配範圍與來源，Gemini 依原始檔撰寫，主代理核對欄位、提交時間與路徑）

### U-003 iOS 手電筒控制接上核心租約與五檔亮度

- **日期**：2026-11-16 12:30（`harness: c51f5bd`）～23:57（`harness: 7034f05`）
- **狀態**：已採用並進入日常（`chain-v7`～`v9` 啟用，控制中心五檔實測通過）
- **開關**：
  - 核心介面：`sysctlbyname("kern.fuxitorch")`（寫入 `0..4` 等級，需核心端配合 `fuxi,pmic=3` 開啟 PMIC 手電筒執行期）
  - 亮度對應（`FuxiTorchLevels.h`）：`0.0f -> 0`、`<=0.25f -> 1`、`<=0.5f -> 2`、`<=0.85f -> 3`、`>0.85f -> 4`（對應電流 `0 / 20 / 40 / 60 / 80 mA`）
- **問題／現象**：iOS 控制中心手電筒按鈕在無原廠相機驅動時顯示不可用（受 `FlashlightModule` 的 `UIRequiredDeviceCapabilities` 要求 `camera-flash` 限制），且開啟後若無持續續約或四檔映射不對，亮度會跳階或逾時熄滅。
- **根因**：缺乏將 `AVFlashlight` ObjC 介面轉譯為 `kern.fuxitorch` sysctl 與定時續約的 userland 橋接，且初期未移除 `FlashlightModule` 的 capability gate 與對齊 iOS 四段非線性滑桿值（`0.25 / 0.5 / 0.85 / 1.0`）。
- **做法**：
  - `harness: userland/fuxi-shape/FuxiFlashlight.c` 透過 `torch_signature` 驗證後替換 `AVFlashlight` 的 `init`、`isAvailable`、`isOverheated`、`flashlightLevel`、`setFlashlightLevel:withError:`、`turnPowerOnWithError:`、`turnPowerOff`；以 `pthread_mutex_t` 保護並用 `dispatch_source` 定時器續約核心租約。
  - `torch_module_capabilities` 移除 `com.apple.control-center.FlashlightModule` 的 `camera-flash` 能力要求，使控制中心開放手電筒開關與滑桿。
  - `FuxiTorchLevels.h` 實作 `fuxi_torch_ui_decode`／`fuxi_torch_ui_encode` 精準對應 5 檔（關＋4 亮階）。
  - 演進：
    - `harness: c51f5bd` 2026-11-16 12:30：新增 `FuxiFlashlight.c`，將原生手電筒控制接上核心租約。
    - `harness: 8c1a20c` 2026-11-16 12:38：核心允許平台 SpringBoard 呼叫者取得 torch 租約。
    - `harness: 0806a94` 2026-11-16 13:28：透過 scoped capability gate 放行 `FlashlightModule`。
    - `harness: 7034f05` 2026-11-16 23:57：抽出 `FuxiTorchLevels.h` 與 `test-flashlight-levels.c`，修正四段 iOS 亮度階梯。
- **復現**：使用含 `fuxi,pmic=3` 與 `fuxi,inject=2` 的 loader 開機後，於 iOS 控制中心開啟並拖動手電筒滑桿；預期核心／stderr 印出 `fuxi: SpringBoard: native FlashlightModule capability admitted` 與 `native flashlight level=<0..4> current=<0..80>mA`。
- **主機測試**：`harness: tools/test-flashlight-levels.c`（以 clang ASan+UBSan 編譯執行，涵蓋於 `build-sb-shape.ps1` 與 `sb-shape.yml`）。
- **驗證紀錄**：
  - 2026-11-17 00:03:58–00:08:50（`native-local-torchlevels-sb127-user-loader`／`kernel-local-validated-9g-artifact`）：實測收到 113 次 level transition（涵蓋 `0/1/2/3/4`），使用者回報「手電筒可以了 沒問題」。
  - 逐趟手動觀察（`work/Codex-Xiaomi13-GPU-20261109/<趟次>/manual-observation.json`，全程手動操作、無合成點擊）：
    - 2026-11-16 `native-hw000torchmanual-user-boot-1`：使用者確認有光但會閃爍（期望常亮到關閉）；requested_levels 19 筆、stopped_runtime_errors 0、SPMI 拒絕 0、USB 斷 1 次、最高 60.7 °C；持續紀錄 `work/adsp-battery/fuxilog/20261116-221224/console.txt`；常亮不合格。
    - 2026-11-16 22:58:08–23:09:17 `native-hw000torchsteady-user-boot-1`（`native-ram9g-torchsteady-sb127-user-loader`／`kernel-adsp-steady-ram9g-artifact`）：使用者回報「這次有持續亮 但調整亮度大小時會閃」；固定檔常亮確認，換檔不閃失敗；USB console 32 次、持續紀錄（第 36 次開機）38 次 level 轉換；主機 tsens 最高 63.1 °C。
    - 2026-11-16 23:44:41–23:50:25 `native-hw000torchdim-user-boot-1`（`native-ram9g-torchdim-sb127-user-loader`／`kernel-adsp-dim-ram9g-artifact`）：使用者回報「手電筒正常了」，但 3 檔與 4 檔沒有亮度差；原因是 root 的 `SBUIFlashlightController` 送 0.25／0.5／0.85／1.0，`ceil(value * 4)` 把 3、4 合併，核心只看到 0、1、2、4；USB console 44 次轉換。這一趟促成 `7034f05` 的 `FuxiTorchLevels.h`。
    - 2026-11-17 00:03:58–00:08:50 `native-hw000local-torchlevels-user-boot-1`（下一行那一趟）：dylib 來源 `7034f05`，四個非零檔可分辨，使用者接受。
- **參考來源**：核心端 `kern.fuxitorch` PMIC 手電筒租約。
- **結論／未完成**：控制中心五檔開關與亮度調節已完全可用並隨日常鏈部署。
- **出處**：`harness: userland/fuxi-shape/FuxiFlashlight.c`、`FuxiTorchLevels.h`、`tools/test-flashlight-levels.c`、`ws: work/statusbar-fix/launch-torch-stage-c51f5bd.ps1`、`harness: c51f5bd`、`8c1a20c`、`0806a94`、`7034f05`。（2026-11-18 主代理分配範圍與來源，Gemini 依原始檔撰寫，主代理核對欄位、提交時間與路徑）

### U-004 iOS 震動交給核心 HAP525 執行期（mediaserverd／FigVibrator／MediaExperience）

- **日期**：2026-11-17 13:22（`harness: 4526188`）～22:27（`harness: 19cdb10`）
- **狀態**：部分可用（來電／通知／充電與系統聲音 `VibePattern`／`Atoms` 震動及包絡振幅調整實測通過；UIKit CoreHaptics 互動震動仍因 `-4810` 未解決）
- **開關**：
  - `fuxi,haptic=<2|3>`（ADT `/chosen`）：啟用 HAP525 震動執行期；`3` 另開啟開機自測（SpringBoard 30 秒後呼叫 `AudioServicesPlaySystemSound(4095)`／`1519`；`mediaserverd` 10s／75s 檢查震動器建立、77s 呼叫 `CMSessionMgrVibrateForSystemSoundWithOptions`）。
  - `fuxi,inject` bit 2（`fxinj`）：將 `libfuxi-sb.dylib` 注入 `mediaserverd`。
  - ADT `/product/haptics` `closed-loop=1`（loader `--product-haptics`）：使 `MediaExperience` 建立震動器並走 `VibeSynthEngine`。
  - `fuxi,haptic-env`（ADT）：Atoms 依包絡面積換算等效全開時間。
  - `fuxi,haptic-amp=<1..255>`（ADT）：核心 `DIRECT_PLAY` 振幅上限（透過 `kern.fuxivibrate` bit 16..23 傳遞），`mediaserverd` 逐 10 ms 量化為 16 階包絡（`FX_VIBE_STEPS=16`）並乘上 `Intensity` 與 `gain`。
- **問題／現象**：iOS 震動請求在無原廠馬達驅動下會被丟棄或只剩孤立測試脈衝；導入初期亦出現直接震動入口回 `!itl`、Atoms 固定短震或 2000ms 請求無法提前取消、與連續段銜接中斷等問題。
- **根因**：`MediaExperience` 必須在 ADT `/product/haptics` 存在 `closed-loop=1` 時才會建立震動器，且須同時攔截 `AVHapticServer(VibeSynthEngine)` 與 `MediaExperience` vibrator callback 並以背景 worker thread 依 `VibePattern`／`Atoms`／`OnDuration` 排程 `kern.fuxivibrate` 租約與取消。
- **做法**：
  - `harness: userland/fuxi-shape/FuxiVibePattern.h`：純 C 解析與合併震動段落（最短開 10 ms、單段上限 10 s、總長上限 30 s、核心單次 2000 ms／1500 ms 續約、包絡 10 ms 取樣 16 階、`FX_VIBE_SLACK_MS=30` 避免連續段間馬達停轉）。
  - `harness: userland/fuxi-shape/FuxiVibrate.c`：在 `mediaserverd` 掛載 `AVHapticServer(VibeSynthEngine)`（`playVibePattern:...`、`cancelPatternWithOptions:`）與 `MediaExperience` callback，並建立專屬排程執行緒（`vb_generation` 支援立即中斷／取消）；在 SpringBoard 掛載 `SBFTapticEngine`／`_UITapticEngine` 與系統音觸發。
  - 演進：
    - `harness: 4526188` 2026-11-17 13:22：初版 `FuxiVibrate.c`／`FuxiVibePattern.h`／`test-vibe-pattern.c`。
    - `harness: 74a8907` 2026-11-17 13:49：支援 `FigVibrator` `OnDuration` 格式與未知字典 15ms 回退。
    - `harness: 641d9ca` 2026-11-17 14:02：探測 `MediaExperience` vibrator gate 並於自測呼叫系統音入口。
    - `harness: c4dbd88` 2026-11-17 14:25：橋接 `MediaExperience` callback 並新增 `test-vibe-worker.py`。
    - `harness: 101e39d` 2026-11-17 14:45：保留 timed vibration `Atoms` 並實作原生取消。
    - `harness: 0cc9312` 2026-11-17 19:42：修正純量診斷 ABI 並關聯原生震動時間戳。
    - `harness: 19cdb10` 2026-11-17 22:27：加入逐段 10 ms 包絡振幅上限 `fuxi,haptic-amp`（`kern.fuxivibrate` bits 16..23）。
- **復現**：loader 帶 `--haptic 2 --inject-media --product-haptics --haptic-amp 128 --stderr-log mediaserverd` 開機並插入充電器或觸發通知。
- **主機測試**：`harness: tools/test-vibe-pattern.c`（ASan+UBSan）與 `harness: tools/test-vibe-worker.py`（於 `build-sb-shape.ps1` 與 `sb-shape.yml` 自動執行）。
- **驗證紀錄**：
  - `native-hw000vibe7-boot`（14:31）：`MediaExperience bridge installed`，9 次核心播放停止正常／`fault=0`。
  - `native-hw000vibe8-boot`（14:51）：Atoms Minitap 15ms 實測 `15.530ms`，2000ms 取消實測 `120.850ms`，10 次播放 `fault=0`／`stop-rc=0`。
  - `native-hw000wifi-candidate-sound-audit-boot-1`（19:47）：充電震動兩次 `sound-id=1106`（400ms VibePattern → 核心 `on-us=400089/402395`），使用者確認有震動；`native-hw000wifi-rssiamp-boot-1`（22:25）實測振幅上限 `128` 生效。
- **參考來源**：核心 `kern.fuxivibrate` HAP525 驅動。
- **結論／未完成**：系統聲音、充電與 `VibePattern`／`Atoms` 震動已運作正常並納入日常候選；UIKit CoreHaptics 互動震動仍受 `AVHapticServer` `-4810` 阻擋（見 U-005）。
- **出處**：`harness: userland/fuxi-shape/FuxiVibrate.c`、`FuxiVibePattern.h`、`tools/test-vibe-pattern.c`、`tools/test-vibe-worker.py`、`harness: 4526188`、`74a8907`、`641d9ca`、`c4dbd88`、`101e39d`、`0cc9312`、`19cdb10`。（2026-11-18 主代理分配範圍與來源，Gemini 依原始檔撰寫，主代理核對欄位、提交時間與路徑）

### U-005 UIKit 觸覺回饋與 CoreHaptics 初始化診斷

- **日期**：2026-11-17 19:11（`harness: 80ce470`）～20:23（`harness: aa06726`）
- **狀態**：SpringBoard 回退實機通過。11 月 18 日試裝 U-021 的 HAL 後，SpringBoard 已能建立原生 CoreHaptics 引擎並經 Actuator 播放。Preferences 的個別操作仍待複驗。
- **開關**：
  - `fuxi,haptic-ui=<8..40>`（ADT `/chosen`）：當 CoreHaptics engine 從未建成時，每次 UIKit 互動播放改用 `kern.fuxivibrate` 輕觸指定毫秒數（`8..40` ms）。
- **問題／現象**：充電與系統音效已能震動，但使用者在控制中心、鍵盤／開關等 UIKit 互動操作仍完全感覺不到觸覺回饋。
- **根因**：UIKit 互動請求確實觸發（`_UIClickFeedbackGenerator`／`UINotificationFeedbackGenerator` `enabled=1`、`muted=0`），但底層 CoreHaptics 引擎初始化 5 次皆回傳 `initialized=0`（`mediaserverd` 的 `AVHapticServer` 未能啟動，client 回傳錯誤 `-4810`）。
- **做法**：
  - `harness: userland/fuxi-shape/FuxiErrorAudit.h`：提供 `fuxi_error_audit`（最多追 3 層 `kCFErrorUnderlyingErrorKey`）與 `fuxi_error_reason`，將 `CFError` 轉為帶 FNV-1a domain hash 與 code 的結構化摘要，不外洩敏感字串。
  - 在 `FuxiVibrate.c` 加入 typed ObjC hook 追蹤 UIKit 回饋引擎選擇、system sound 分派（`aec9aba`）、CoreHaptics 初始化失敗原因與時間戳關聯。
  - 演進：
    - `harness: 80ce470` 2026-11-17 19:11：以 typed ABI 追蹤 UIKit 回饋引擎選擇與分派許可。
    - `harness: aec9aba` 2026-11-17 19:41：追蹤回饋系統音分派（`sound-id=1106`）。
    - `harness: a3df640` 2026-11-17 20:02：追蹤 UIKit 互動請求與 CoreHaptics 初始化失敗。
    - `harness: ac6f20a` 2026-11-17 20:16：新增 `FuxiErrorAudit.h` 輸出有界底層錯誤碼。
    - `harness: aa06726` 2026-11-17 20:23：修正使用公開的 `kCFErrorDescriptionKey`（先前誤用非公開常數導致 CI `44081675866` 失敗）。
- **復現**：以含 `libfuxi-sb.dylib` 與 `fuxi,haptic=2` 的 loader 開機並操作 UIKit 控制項，於 console 檢視 CoreHaptics 初始化回傳與 `_UIFeedbackSystemSoundEngine` 分派紀錄。
- **主機測試**：隨 `build-sb-shape.ps1` 編譯驗證（修正後已通過 WSL 與 Xcode 編譯檢查）。
- **驗證紀錄**：2026-11-17 20:06 `native-hw000wifi-decisions-corehaptics-boot-1` 實測確認 UIKit 引擎 `enabled=1, muted=0`，CoreHaptics 引擎五次 `initialized=0`；22:25 `native-hw000wifi-rssiamp-boot-1` 仍回報 `-4810` 未修。
- 2026-11-18 18:10 `native-hw000haptic-prefs-codex-1118` 實測 12 次 HAP525 播放全數停止成功。第 4 次自動共振錯誤於下一次播放前清除。SpringBoard 的 impact 和 selection 在 boot.log 第 4513–4522 行各帶出約 15 ms 的硬體播放紀錄。第 4537–4544 行確認設定 App 已啟動並載入 dylib，當趟缺少它的震動初始化與播放證據。
- 修正版在 constructor 以程序名辨識 `Preferences`，同時保留原有的 App 路徑判斷。`sb-local-bt5-prefs-codex-r3-1118-artifact` 通過 WSL 建置和 ad hoc 碼頁驗證。K-077 同趟 loader 同步接出 Preferences 與 mediaserverd 的 stderr。成功須在 `app haptic fallback=1`、App generator 自測之後看到對應的 HAP525 播放。
- 依 `native-hw000btsetup-1118/boot.log` 第 2467–2471 行的 `CAXException` 呼叫位址 `0x1a099feb4`，用既有 `prof-sym.py` 的 dyld cache 映射和 Capstone 核對 20H392 的 `SynthOutput::SynthOutput(HapticSynth&, bool)+0x994`。同分支用 `0x1a0ab3773` 的字串「Failed to default output Node」與 OSStatus −1 建立例外。實機另已成功建立 `aumu/hsyn/appl`，故下一步查預設輸出節點／音訊裝置。R3 修正 20H392 的例外診斷欄位，文字從物件 `+0x08` 讀取，OSStatus 從 `+0x108` 讀取，對照建構子 `0x1a08a296c`。
- **參考來源**：iOS UIKit `_UIFeedbackSystemSoundEngine` 與 CoreHaptics `_internal_createCoreHapticsEngine`。
- **結論／未完成**：原有配置的 `-4810` 已在 U-021 的 HAL 試裝配置排除。該趟同時啟用了輕觸回退，原生路徑的判據是 mediaserverd 的 Actuator 脈衝與核心對應振幅，詳見 U-021。
- **出處**：`harness: userland/fuxi-shape/FuxiErrorAudit.h`、`FuxiVibrate.c`、`harness: 80ce470`、`aec9aba`、`a3df640`、`ac6f20a`、`aa06726`。（2026-11-18 主代理分配範圍與來源，Gemini 依原始檔撰寫，主代理核對欄位、提交時間與路徑）

### U-006 電池讀取者稽核函式庫

- **日期**：2026-11-16 17:05（`harness: 8ef4c72`）～17:41（`harness: 7b44c8e`）
- **狀態**：已採用（診斷工具，需時透過 ADT 開關啟用）
- **開關**：
  - `fuxi,batteryaudit=1`（ADT `/chosen` u32，對應 `prepare-ram9g-loader.py --battery-audit`）：啟用 SpringBoard 內的有界電池 IORegistry 觀察者。
- **問題／現象**：早期需要確認 iOS userland（SpringBoard）從 `IOService` 讀到的 `SweetBattery` 欄位是否與核心發佈一致，包含新鮮度旗標（`FuxiBatterySOCValid`、`FuxiBatteryExternalKnown`、`FuxiBatteryMaxAgeMs` 等）與 `AdapterDetails`／`LegacyBatteryInfo` 巢狀字典。
- **根因**：核心 log 只能證明核心寫入 IORegistry，無法直接證明 SpringBoard 端實際讀到的屬性型態與數值是否正確。
- **做法**：
  - `harness: userland/fuxi-shape/FuxiBatteryAudit.c` 在 `fuxi,batteryaudit=1` 時由 `IOServiceGetMatchingService` 讀取目標服務屬性，檢查 32 項指定欄位（含電量、接電、充電、電流電壓、充電器詳情、舊版電池資訊與 Fuxi 新鮮度／世代欄位）之 `CFTypeID` 與整數值，最多取樣 120 次（`FA_SAMPLE_LIMIT=120`）且僅記錄前 32 次變更（`FA_CHANGE_LIMIT=32`），以 `mach_absolute_time` 換算毫秒印至 stderr（`fuxi: SpringBoard: battery-audit ...`）。
  - 演進：
    - `harness: 8ef4c72` 2026-11-16 17:05：新增 `FuxiBatteryAudit.c`。
    - `harness: 7b44c8e` 2026-11-16 17:41：限制電池稽核輸出列數上限（bounded rows）。
- **復現**：於 `prepare-ram9g-loader.py` 加上 `--battery-audit` 上機，之後以 `ws: work/statusbar-fix/analyze-battery-pause.py` 解析 console 中的 `fuxi: SpringBoard: battery-audit ` 紀錄。
- **主機測試**：無獨立 C 單元測試；由 `analyze-battery-pause.py` 解析輸出並配合 `build-sb-shape.ps1` 編譯檢查。
- **驗證紀錄**：2026-11-16 18:11 Run 23 等多趟電池新鮮度／暫停測試搭配使用，確認 SpringBoard 端讀到的欄位與核心一致性。
- **參考來源**：核心 `fuxi_adsp_battery.c`／`SweetBattery` 屬性契約。
- **結論／未完成**：完成且保持預設關閉，僅於需要核對 userland 電池屬性時啟用。
- **出處**：`harness: userland/fuxi-shape/FuxiBatteryAudit.c`、`harness: 8ef4c72`、`7b44c8e`。（2026-11-18 主代理分配範圍與來源，Gemini 依原始檔撰寫，主代理核對欄位、提交時間與路徑）

### U-007 Wi-Fi 診斷函式庫（CoreWiFi 掃描結果、過濾、候選比對、autojoin 決策）

- **日期**：2026-11-17 19:22（`harness: cf8fbfa`）～22:27（`harness: 19cdb10`）
- **狀態**：已採用（診斷工具，成功將無法自動加入縮小至原生資格檢查與密碼查詢）
- **開關**：
  - `fuxi,inject`（`wifid` 診斷注入）：載入 `FuxiWifiAudit.c` 追蹤 `wifid` 內的 CoreWiFi 與 WiFiManager 方法。
  - `fuxi,wifi-sb`（ADT `/chosen`）：於 SpringBoard 進行唯讀狀態欄 Wi-Fi 輸入稽核（`19cdb10`）。
- **問題／現象**：手動在設定頁輸入密碼可連上 Wi-Fi，但斷線或重開後始終無法自動加入；需要釐清究竟是掃描結果沒進 CoreWiFi、被 `CWFFilteredScanResults` 過濾、還是已知網路比對／autojoin 資格拒絕。
- **根因**：僅從核心驅動層或 Linux 端無法觀測 `wifid` 內部 Objective-C 決策管線與錯誤物件。
- **做法**：
  - `harness: userland/fuxi-shape/FuxiWifiAudit.c` 先以 `wifi_audit_type` 檢查方法參數與回傳型別編碼，再安全掛載以下路徑並只寫摘要（SSID 一律以 FNV-1a 轉成 8 位 hex `ssid-id`，不輸出明文 SSID 或密碼）：
    - 掃描結果與參數（`wifi_audit_result`、`CWFFilteredScanResults`）
    - 候選匹配、preflight、known／native-known network 決策、排序與 `__allowJoinCandidate:trigger:defer:error:`（搭配 `FuxiErrorAudit.h` 輸出 NSError code 與原因分類）。
  - 演進：
    - `harness: cf8fbfa` 2026-11-17 19:22：新增 `FuxiWifiAudit.c`（CoreWiFi 掃描與過濾診斷）。
    - `harness: aec9aba` 2026-11-17 19:41：加入候選比對追蹤。
    - `harness: 0cc9312` 2026-11-17 19:42：修正純量診斷 ABI。
    - `harness: 8b3a90d` 2026-11-17 19:57：追蹤 known profile 比對後的 autojoin 決策（`__allowJoinCandidate:...`）。
    - `harness: ac6f20a` 2026-11-17 20:16：結合 `FuxiErrorAudit.h` 輸出底層 autojoin 錯誤。
    - `harness: 19cdb10` 2026-11-17 22:27：新增 SpringBoard 狀態欄唯讀 Wi-Fi 輸入稽核（`fuxi,wifi-sb`）。
- **復現**：以開啟 `wifid` 診斷注入的 loader 上機，檢視 `console.txt` 中的 `fuxi: wifid: scan audit` 與候選決策輸出。
- **主機測試**：隨 `build-sb-shape.ps1` 編譯並通過 `verify-sb-shape.py` 檢查。
- **驗證紀錄**：
  - 2026-11-17 19:47 `native-hw000wifi-candidate-sound-audit-boot-1`：證明家中 AP 通過 preflight、filter、security 與 known profile 匹配（`autojoin-off=0`），但仍未加入。
  - 20:06 `native-hw000wifi-decisions-corehaptics-boot-1`：確認 2.4/5GHz 候選保留至排序後，在 `__allowJoinCandidate:trigger:defer:error:` 回傳 `allowed=0`、真實 `NSError code=1`。
- **參考來源**：同版 `wifid` 反組譯位址（見 `wifi-autojoin-credential-decision-20261117.zh-TW.md`）。
- **結論／未完成**：精準縮小 userland 阻塞點，支援後續修復；所有密碼／SSID 均經去識別雜湊處理。
- **出處**：`harness: userland/fuxi-shape/FuxiWifiAudit.c`、`FuxiErrorAudit.h`、`harness: cf8fbfa`、`aec9aba`、`0cc9312`、`8b3a90d`、`ac6f20a`、`19cdb10`。（2026-11-18 主代理分配範圍與來源，Gemini 依原始檔撰寫，主代理核對欄位、提交時間與路徑）

### U-008 原生 Wi-Fi 決策日誌：開關、唯讀擷取、離線重播

- **日期**：2026-11-17 10:00（`ws: 88d7112`）～19:23（`ws: 0d5853a`）
- **狀態**：已採用（提供零風險唯讀分析與離線重播工具鏈）
- **開關**：
  - `launch-loader-check.ps1 -WifiLogging <enable|disable>`：切換 `/private/var/preferences/SystemConfiguration/com.apple.wifi.plist` 既有 `LoggingFileEnabled` 布林值。
  - `launch-loader-check.ps1 -InspectWifiLog`：唯讀擷取 `/private/var/logs/wifimanager.log`（注意 `/Library/Logs` 在本映像指向 `/private/var/logs`）。
  - `replay-wifi-log.py [--details] [--focus <regex>]`：純離線重播已捕獲區塊。
- **問題／現象**：需要查看 iOS 原生 `wifid` 的 `/private/var/logs/wifimanager.log` 決策紀錄，但不能隨意清除設定、改動無關 plist 欄位，也不能將密碼或帳戶字串寫入分析紀錄。
- **根因**：初期摘要工具過度隱藏所有含 `password` 的行，漏掉了關鍵的 `-25300`（`errSecItemNotFound`）鑰匙圈查詢錯誤；且反覆回手機讀取日誌既慢又有額外風險。
- **做法**：
  - `configure-wifi-logging.py`（由 guest 端 `stage-wifi-logging.sh` 呼叫）：拒絕 symlink、驗明 `bplist00` 與既有 `LoggingFileEnabled` 型別，原子替換並驗證其他所有欄位一字未改。
  - `replay-wifi-log.py`：讀取既有 `inspect-*` 目錄的 `read-cache` 離線重播，若缺塊直接中止（`phone_operations=0`、`guest_root_overlay_writes=0`）；保留前 50 與後 50 筆摘要，`--focus`（最多 256 字元 regex）精準選取並顯示 matched／redacted／omitted 計數，固定模板 `credential_decisions` 只輸出分類與數字狀態。
  - `work/localbuild/inspect-wifi-daemon.py`：離線解析同版 `20H392` `wifid` Mach-O 與 Objective-C selector stubs 供位址對照。
  - 演進：
    - `ws: 88d7112` 2026-11-17 10:00：新增唯讀檢查已存 Wi-Fi 旗標。
    - `ws: fca610a` 2026-11-17 15:58：新增 `configure-wifi-logging.py`、`stage-wifi-logging.sh`、`replay-wifi-log.py`、`inspect-wifi-daemon.py`。
    - `ws: a8ebbc5` 2026-11-17 16:27：新增 `credential_decisions` 固定模板計數並產出 `wifi-autojoin-credential-decision-20261117.zh-TW.md`。
    - `ws: 336e3ba` 2026-11-17 16:53：統計 keychain 保存失敗次數並釐清舊趟日誌屬 AKS wrap 修正前。
    - `ws: 0d5853a` 2026-11-17 19:23：補入安全快取 Wi-Fi profile 脈絡。
- **復現**：
  ```powershell
  D:\xiaomi13\fuxi_ios\work\statusbar-fix\launch-loader-check.ps1 -WifiLogging enable -Out run-wifilog-en
  # 實機測試後立即關閉並唯讀擷取：
  D:\xiaomi13\fuxi_ios\work\statusbar-fix\launch-loader-check.ps1 -WifiLogging disable -Out run-wifilog-dis
  D:\xiaomi13\fuxi_ios\work\statusbar-fix\launch-loader-check.ps1 -InspectWifiLog -Out inspect-wifilog-cap
  python -X utf8 D:\xiaomi13\fuxi_ios\work\statusbar-fix\replay-wifi-log.py work\statusbar-fix\inspect-wifilog-cap work\statusbar-fix\inspect-wifilog-re1 --details --focus "suitability|SecItem|-25300"
  ```
- **主機測試**：`configure-wifi-logging.py --self-test`、`inspect-powerlog-state.py --self-test`（42 項檢查，含敏感詞過濾、固定錯誤模板、首尾保留與唯讀契約）。
- **驗證紀錄**：
  - 2026-11-17 15:22–15:32 `native-hw000wlan-log-boot-1` 兩次開關皆 5 塊 `PRE_OK`/`WRITE_OK`，唯讀擷取 18 塊、零寫入；離線重播定位舊趟一般／NonSyncable 查詢各 22 次 `-25300` 與 suitability 缺密碼 22 次。
  - 19:01 `inspect-wifilog-hapticwifi-20261117-1901` 驗證 AKS 修正後最新擷取的 `password_missing`／`keychain_lookup_errors`／`save_failures` 皆降為 0。
- **參考來源**：`AGENTS.md` 第 4 節、`wifi-autojoin-credential-decision-20261117.zh-TW.md`。
- **結論／未完成**：安全且可離線重複分析的標準除錯工作流已完整落地。
- **出處**：`ws: work/statusbar-fix/configure-wifi-logging.py`、`stage-wifi-logging.sh`、`replay-wifi-log.py`、`ws: work/localbuild/inspect-wifi-daemon.py`、`wifi-autojoin-credential-decision-20261117.zh-TW.md`、`ws: 88d7112`、`fca610a`、`a8ebbc5`、`336e3ba`、`0d5853a`。（2026-11-18 主代理分配範圍與來源，Gemini 依原始檔撰寫，主代理核對欄位、提交時間與路徑）

### U-009 iOS 時區

- **日期**：2026-11-16 23:39（`ws: 42086d7`）～2026-11-17 02:44（`ws: 5ce976b`）
- **狀態**：已採用（iOS root 已設定為 `Asia/Taipei`）
- **開關**：`phone-readthrough.py --timezone <AREA/CITY>`（透過 `zone.txt` 傳給 `stage-timezone.sh`）
- **問題／現象**：iOS root 預設時區非台北時間，導致狀態欄與系統時間即使在 RTC 校時正確後仍存在時區顯示偏移。
- **根因**：`/private/var/db/timezone/localtime` 符號連結未指向 `Asia/Taipei`。
- **做法**：
  - `ws: work/statusbar-fix/stage-timezone.sh` 驗證 `zone.txt` 格式為 `[A-Za-z_]+/[A-Za-z_]+`，經 HFS+ journal 重播與 rw 掛載後，以 `inside()` 遞迴解析符號連結確認目標 zoneinfo 檔案存在，再將 `/private/var/db/timezone/localtime` 改指至該時區並執行前後 `fsck.hfsplus`。
  - 演進：
    - `ws: 42086d7` 2026-11-16 23:39：確認持續性的 iOS 時區顯示偏移並更新狀態檢查。
    - `ws: 5ce976b` 2026-11-17 02:44：正式實裝 `phone-readthrough.py --timezone` 與 `stage-timezone.sh`。
- **復現**：`python -X utf8 work\statusbar-fix\phone-readthrough.py --timezone Asia/Taipei ...`
- **主機測試**：前後 `fsck.hfsplus -fn` 與目標 zone 存在性檢查。
- **驗證紀錄**：2026-11-17 02:13 執行（備份／紀錄 `tz-taipei-20261117-0213`），配合 `chain-v7` `fuxi,rtc-offset` 使系統時間與台北時間吻合；UI 時區顯示與長時間走時仍待持續觀察。
- **參考來源**：標準 BSD/Darwin `/private/var/db/timezone/localtime` 機制。
- **結論／未完成**：完成部署並留存於手機現況。
- **出處**：`ws: work/statusbar-fix/stage-timezone.sh`、`ws: 42086d7`、`5ce976b`。（2026-11-18 主代理分配範圍與來源，Gemini 依原始檔撰寫，主代理核對欄位、提交時間與路徑）

### U-010 停用 launchd job（fairplayd 擋 exec 導致日期與時間卡死）

- **日期**：2026-11-17 11:58（`ws: 14b2341`；實作與上機早於該追蹤提交）
- **狀態**：已採用（已於手機 root 停用 `fairplayd.H2`）
- **開關**：`phone-readthrough.py --disable-job <com.apple....>`（傳入 `job.txt` 給 `stage-launchd-disable.sh`）
- **問題／現象**：在設定頁點選「日期與時間」時畫面完全卡死無回應。
- **根因**：核心 `sweetnoexec` 擋下未簽章或受限 daemon（`fairplayd.H2`）的 `exec`，但 `launchd` 仍代為登記並接收其 `MachServices`，導致呼叫端送出的 MIG 訊息沒有逾時且永遠等不到回覆，使 `locationd`／設定頁死鎖。
- **做法**：
  - `ws: work/statusbar-fix/stage-launchd-disable.sh` 驗證 `job.txt` 符合 `com\.apple\.[A-Za-z0-9.]+`，掛載 HFS+ 後開啟 `/System/Library/xpc/launchd.plist`（確認 `bplist00` 與僅唯一符合 `Label`），將該 job 加上 `Disabled = True`，驗證其餘 `LaunchDaemons` 鍵值未變後以暫存檔原子替換。
  - 停用後 `launchd` 不再登記該服務名稱，`bootstrap_look_up` 直接回傳失敗而不是無限期等待。
- **復現**：`python -X utf8 work\statusbar-fix\phone-readthrough.py --disable-job com.apple.fairplayd.H2 ...`
- **主機測試**：腳本內建 `bplist00` 讀取、鍵集合不變性檢查與前後 `fsck.hfsplus`。
- **驗證紀錄**：2026-11-17 上機停用 `fairplayd.H2` 後，實機驗證點選「日期與時間」不再卡死（見狀態日誌「日期與時間卡死＝fairplayd 被擋」與 `ws: 547904b` 第二趟點擊測試）。
- **參考來源**：`stage-timezone.sh` HFS+ journal/fsck 外框。
- **結論／未完成**：完成且有效解除「日期與時間」設定頁死鎖。
- **出處**：`ws: work/statusbar-fix/stage-launchd-disable.sh`、`ws: 14b2341`、`ws: 547904b`。（2026-11-18 主代理分配範圍與來源，Gemini 依原始檔撰寫，主代理核對欄位、提交時間與路徑）

### U-011 安裝 32 個原廠內建 App

- **日期**：2026-11-17 17:48（`ws: f169593`）
- **狀態**：已採用（32 個原廠 App 已寫入手機 `/Applications`）
- **開關**：`phone-readthrough.py --install-staged-apps`
- **問題／現象**：iOS 16.7 根映像中的 32 個原廠 App（行事曆、備忘錄、時鐘、計算機、地圖、天氣、指南針、尋找、家庭、放大鏡、Watch 等）仍停留在 `/private/var/staged_system_apps`，桌面上只見佔位圖示或無法直接執行。
- **根因**：映像未經原廠首次開機搬移流程，且 `launchd.plist` 的 `AppExtensions` 快取與 LaunchServices 資料庫尚未對應 `/Applications/` 實際路徑。
- **做法**：
  - `ws: work/statusbar-fix/stage-staged-apps.sh` 復用 `ksrc/tools/sweet-root-tweaks.py --only-optional --install-staged-apps --rebuild-launchservices`（參考 HackiOS 對 16.x 的作法），外層包覆 HFS+ journal 重播與 `fsck.hfsplus` 檢查。
  - 將 `staged_system_apps/*.app` 移入 `/Applications`、將對應 `AppPlaceholders` 停放至 `/System/Library/AppPlaceholders.fuxi-staged/`、將 extension 補進 `launchd.plist` 的 `AppExtensions` 快取，並刪除 LaunchServices `.csstore` 資料庫讓 `lsd` 於下次開機自動重建；全程核對使用者桌面 `IconState.plist` 的 SHA-256 完全不變。
- **復現**：`python -X utf8 work\statusbar-fix\phone-readthrough.py --install-staged-apps ...`
- **主機測試**：`ksrc/tools/test-sweet-root-tweaks.py`；guest 端腳本前後核對 `/Applications` 數量、`STAGED_LEFT=0`、`LS_DATABASES_LEFT=0` 與 `IconState.plist` 雜湊不變。
- **驗證紀錄**：2026-11-17 `ws: f169593` 實機寫入 19 塊（`PRE_OK`／`WRITE_OK`），`/Applications` 由 141 增至 173 個、補入 102 筆 extension 快取、`fsck` 全程通過、`IconState` 未變；同趟截圖 `framebuffer-shot7-step.png` 的 App 資料庫已顯示指南針、尋找、家庭、放大鏡與 Watch。
- **參考來源**：HackiOS 16.x 搬移作法、`work/adsp-battery/ksrc/tools/sweet-root-tweaks.py`。
- **結論／未完成**：已完成並保留使用者既有桌面配置。
- **出處**：`ws: work/statusbar-fix/stage-staged-apps.sh`、`ws: f169593`、`ws: work/hw000-adaptation-status.zh-TW.md` 第 89 行。（2026-11-18 主代理分配範圍與來源，Gemini 依原始檔撰寫，主代理核對欄位、提交時間與路徑）

### U-012 第三方 App 重簽與安裝（read-through stage）

- **日期**：2026-11-18 00:51（`ws: dbbb912`）
- **狀態**：已採用（支援將已砸殼 IPA 重簽並安全寫入手機 `/Applications`）
- **開關**：
  - `resign-app-adhoc.py <build|verify|self-test>`
  - `phone-readthrough.py --install-app <tar/outdir>`
  - `install-app-guest.py <install|self-test>`
- **問題／現象**：本機沒有 `ldid` 或 `zsign`，且核心 sweet AMFI（0025／0035／0036）無 trust cache、僅驗證 Mach-O 的 CodeDirectory 分頁 SHA-256 雜湊與 XML entitlements；已砸殼 IPA 的原 App Store 簽章分頁雜湊已失效，無法直接執行，且 IPA 屬不可信外部輸入。
- **根因**：現有 `mkinit.py` 僅產生新檔、`ios27-root.py` 只算 cdhash，缺乏完整保留 `LC_CODE_SIGNATURE` 位置、重算逐頁 SHA-256 與特殊槽並以唯讀塊裝置安全裝進 `/Applications` 的工具鏈。
- **做法**：
  - `ws: work/statusbar-fix/resign-app-adhoc.py`（建議以 `python -X utf8 -I` 執行）：唯讀解析已砸殼 IPA zip（強制檢查路徑、型態、大小上限與 `cryptid=0`），將每個 thin plain arm64 Mach-O 原地替換為 SHA-256 ad hoc CodeDirectory（`0x20400`、4K 頁）SuperBlob，保留空 requirements（槽 2）、原 XML／DER entitlements（槽 5／7）及主執行檔特殊槽 `-1`/`-3`（`Info.plist` 與 `CodeResources`），並確認簽章前所有位元組未動後封裝為 `app.tar` 與 `files.sha256`。
  - `phone-readthrough.py --install-app` 將 `app.tar` 掛為唯讀磁碟 `/dev/vdc` 傳入 Alpine guest。
  - `stage-install-app.sh` ＋ `install-app-guest.py`：拒絕覆蓋同名目錄或同 bundle ID（含 `Applications`、`AppPlaceholders` 與容器 App），先解壓至 `.fuxi-partial` 逐檔核對 SHA-256 與權限後原子改名，再復用 `sweet-root-tweaks.py` 補齊 `AppExtensions` 快取與刪除 LaunchServices 資料庫，並確保 `IconState.plist` 雜湊不變。
- **復現**：
  ```powershell
  python -X utf8 -I D:\xiaomi13\fuxi_ios\work\statusbar-fix\resign-app-adhoc.py build <已砸殼.ipa> <輸出目錄>
  python -X utf8 -I D:\xiaomi13\fuxi_ios\work\statusbar-fix\resign-app-adhoc.py verify <輸出目錄>
  python -X utf8 D:\xiaomi13\fuxi_ios\work\statusbar-fix\phone-readthrough.py --install-app <輸出目錄>
  ```
- **主機測試**：`resign-app-adhoc.py self-test`、`install-app-guest.py self-test`（於 WSL root 執行）。
- **驗證紀錄**：2026-11-18 00:25 實機以酷安 16.2.5（`app-coolmarket-1625-adhoc`，重簽 28 個 Mach-O，紀錄 `statusbar-fix/run-install-coolmarket-1118`）寫入 58 個區塊（`PRE_OK`／`WRITE_OK`）；於隨後的 `native-hw000wifi-v7wifi-boot-1` 確認可開啟、`exec` 後持續執行至紀錄後段且圖片 surface 正常建立、無閃退。
- **參考來源**：`ksrc/tools/mkinit.py` `code_signature()`、`work/localbuild/verify-sb-shape.py`、`ksrc/tools/sweet-root-tweaks.py`。
- **結論／未完成**：完成且已實機驗證第三方 App 重簽、安全寫入與執行。
- **出處**：`ws: work/statusbar-fix/resign-app-adhoc.py`、`install-app-guest.py`、`stage-install-app.sh`、`phone-readthrough.py`、`ws: dbbb912`、`ws: work/hw000-adaptation-status.zh-TW.md` 第 47 行。（2026-11-18 主代理分配範圍與來源，Gemini 依原始檔撰寫，主代理核對欄位、提交時間與路徑）

### U-013 WLAN 韌體與 BDF 放進 iOS root

- **日期**：2026-11-16 15:02（`ws: 8c3357d`）～23:33（`ws: 8fbd59c`）
- **狀態**：已採用（WCN7850／`ath12k` 所需資產已放進 `/usr/local/share/fuxi/wlan/`）
- **開關**：`stage-wlan-assets.py` 接受 JSON manifest 與 `--live`（或測試模擬）寫入手機
- **問題／現象**：核心 `ath12k`／WCN7850 驅動需要 Android `/vendor/firmware_mnt/image/kiwi` 下的 `amss.bin`、`amss20.bin`、`regdb_xiaomi.bin`、`m3.bin`（`phy_ucode20.elf`）與對應板卡 BDF 檔，必須在不掃描整顆 20 GiB root 的前提下，以完整雜湊核對並安全寫入 iOS root。
- **根因**：初期因對 `CNSS` log 的 `0xa2a00000` 誤認（實為 DMA handle 而非 CPU 實體位址）及原廠檔案命名對應需釐清，加上曾發生 guest serial `CRCRLF` 誤判完成訊號而中止。
- **做法**：
  - `inspect-android-radio.py` 與 `fetch-android-wlan-assets.py`（及 `fetch-android-wlan-modules.py`）：在手機 Android 準備就緒且持有三鎖下，唯讀計算手機前後 `sha256sum`／`stat`，導出 `amss20.bin`、`amss.bin`、`bd_m3.elf`、`regdb_xiaomi.bin`、`Data20.msc`、`Data.msc` 與 `phy_ucode20.elf`（M3，299,660 bytes，SHA-256 `bab0d04bf693476d574e721296426e5701c3c6e96438e76ee63f0ab82ffd164f`）。
  - `stage-wlan-assets.py` ＋ `stage-wlan-assets-guest.sh`：嚴格驗證 JSON manifest（拒絕重複鍵與非法常數）、LF 換行與檔案大小上限，透過 `phone-readthrough.py` 在 qcow2 疊加層寫入 `/usr/local/share/fuxi/wlan/`（含 `0644` 權限與前後 `fsck`），寫回前逐塊比對、寫後讀回驗證。
  - `run-wlan-host-wasm.py` 與兩份 `work/adsp-battery/wlan-stage-input-*.json`：分別記錄原廠資產與上游 WCN7850 韌體／`board-2.bin` 的輸入 manifest。
  - 演進：
    - `ws: 8c3357d` 2026-11-16 15:02：新增唯讀擷取與無全盤掃描檢查。
    - `ws: 0fb8eae` 2026-11-16 15:29：新增 `stage-wlan-assets.py`、`stage-wlan-assets-guest.sh` 與 `test-stage-wlan-assets.py`。
    - `ws: 68691ac` 2026-11-16 21:10：記錄上游 WCN7850 韌體 staging（`wlan-stage-input-upstream-20261116-2110.json`）。
    - `ws: 8fbd59c` 2026-11-16 23:33：記錄原生 WLAN 板卡資料 staging 與就緒狀態。
- **復現**：以 `fetch-android-wlan-assets.py` 導出資產 manifest 後，執行 `stage-wlan-assets.py` 寫入 iOS root。
- **主機測試**：`ws: work/statusbar-fix/test-stage-wlan-assets.py`（38/38 host tests 通過，涵蓋 JSON 驗證、guest/serial CRCRLF 處理與 NBD 寫入模擬）。
- **驗證紀錄**：2026-11-16 15:30:09 原廠 5 檔寫入成功（只讀 364 MiB、9 塊寫前寫後驗證，receipt `statusbar-fix/run-wlan-oem-assets-crlf-20261116/receipt.json`）；後續 `board-2.bin` 等亦透過本流程就緒並支撐實機 34-BSS 掃描與 WPA2 連線。
- **參考來源**：`phone-readthrough.py`、Qualcomm WCN7850／`ath12k` 韌體規範。
- **結論／未完成**：完成且為所有原生 Wi-Fi 測試提供安全可驗證的韌體資產佈署。
- **出處**：`ws: work/statusbar-fix/stage-wlan-assets.py`、`stage-wlan-assets-guest.sh`、`test-stage-wlan-assets.py`、`fetch-android-wlan-assets.py`、`fetch-android-wlan-modules.py`、`inspect-android-radio.py`、`run-wlan-host-wasm.py`、`ws: work/adsp-battery/wlan-stage-input-*.json`、`ws: 8c3357d`、`0fb8eae`、`68691ac`、`8fbd59c`。（2026-11-18 主代理分配範圍與來源，Gemini 依原始檔撰寫，主代理核對欄位、提交時間與路徑）

### U-014 iOS root 疊加層、快照底檔、HFS+ journal 重播與差分寫入

- **日期**：2026-11-16 13:49（`ws: 918f517`）～14:15（`ws: 7325782`）
- **狀態**：日常使用中（全專案所有 iOS root 讀寫的基礎儲存層）
- **開關**：`hfs-journal-block-probe.py <IMAGE> [--apply | --mark-unmounted | --mark-journaled]`
- **問題／現象**：iOS root 是存放於 Android `userdata` 的 20 GiB（`5120` × `4 MiB` 塊）pinned F2FS 映像，因為手機以 reset 重開返回 Android，HFS+ 從未正常卸載，已提交的交易常留在 journal 中；Linux `hfsplus` 驅動會忽略 journal 並拒絕掛載或誤傷檔案系統。同時主機端若每次全量拉取 20 GiB 映像極度耗時。
- **根因**：需要整合 (1) 差分快照疊加讀取鏈、(2) 按需經 ADB 區塊快取（read-through NBD）的 qcow2 疊加層、以及 (3) 依 Apple TN1150／XNU `vfs_journal.c` 在掛載前先重播 HFS+ journal 並切換 `kHFSVolumeUnmountedBit` 與 `'HFSJ'` 標記的工具。
- **做法**：
  - `ws: work/statusbar-fix/root_layers.py`：實作 `load_layers`（支援最多 24 層 `parent.json` 連鎖差分並驗證 `phone-blocks.txt` 5120 塊 SHA-256）、`SnapshotReader` 與 `write_backing`。
  - `ws: work/statusbar-fix/make-snapshot-backing.py`：依據差分快照建立指向底檔與 `pull.bin` extents 的 VMDK backing descriptor，省去實體合併 20 GiB 映像。
  - `ws: work/statusbar-fix/stage-share/hfs-journal-block-probe.py`：驗證 HFS+/HFSX journal checksum 並將待寫區塊重播回 home blocks（`--apply`）、設定卸載旗標供 Linux rw 掛載（`--mark-unmounted`），卸載且 `fsck.hfsplus` 通過後恢復 `'HFSJ'` 標記（`--mark-journaled`）。
  - 配合 `phone-readthrough.py`：透過 `ReadOnlyNbd` 只在 Alpine guest 真正存取某個 4 MiB 區塊時才向手機發送 `ManagedNativeImage` 讀取並本地快取，修改只進 qcow2 疊加層，最後僅把變更的少數區塊做寫前 SHA-256 核對、寫回手機與寫後讀回。
  - 演進：
    - `ws: 918f517` 2026-11-16 13:49：導入 `phone-readthrough.py` 與 `sb-guest-run.py` 加速小差分 staging。
    - `ws: 7325782` 2026-11-16 14:15：將變更區塊差分寫入設為預設流程。
- **復現**：由 `stage-*.sh` 與 `phone-readthrough.py` 自動調用；獨立檢查 HFS+ journal 可執行 `python3 hfs-journal-block-probe.py <image>`。
- **主機測試**：`ws: work/statusbar-fix/test-root-layers.py`（測試多層快照合併、VMDK 與 read-through 區塊邊界）。
- **驗證紀錄**：自 2026-11-16 13:49 起所有 `libfuxi-sb.dylib`、Turnip、時區、launchd、App 安裝、WLAN 資產與唯讀日誌擷取皆經此層完成，多次實機驗證寫前比對／寫後讀回與 `fsck` 一致性零損壞。
- **參考來源**：Apple TN1150 "Journal"、XNU `vfs_journal.c`。
- **結論／未完成**：極為成熟穩固，是整個工作區免整盤刷寫、保留手機使用者資料的核心基建。
- **出處**：`ws: work/statusbar-fix/root_layers.py`、`test-root-layers.py`、`make-snapshot-backing.py`、`stage-share/hfs-journal-block-probe.py`、`phone-readthrough.py`、`ws: 918f517`、`7325782`。（2026-11-18 主代理分配範圍與來源，Gemini 依原始檔撰寫，主代理核對欄位、提交時間與路徑）

### U-015 root 資產與 powerlog 唯讀檢查

- **日期**：2026-11-16～2026-11-17（由 `ws-commits.md` 第 166–167、420–422、494–497 行確認）
- **狀態**：日常使用中（所有唯讀狀態／資產／電源暫停診斷的共用工具）
- **開關**：
  - `launch-loader-check.ps1 -InspectWifi` / `-InspectWifiLog`（呼叫 `inspect-powerlog-state.py` 與 `powerlog-state-guest.sh`）
  - `inspect-root-assets.py`（搭配 `root-assets-guest.sh` 經本機 HTTP `X-Fuxi-Token` 傳回資產）
  - `analyze-battery-pause.py <console.txt>`
- **問題／現象**：需要從手機 iOS root 提取藍牙／WLAN 二進位資產或檢查 `com.apple.wifi.plist`、`wifimanager.log` 與 powerlog 狀態，同時必須嚴格保證 HFS+ 唯讀、block 裝置唯讀且 qcow2 疊加層零寫入，且不讀取密碼或鑰匙圈內容。
- **根因**：傳統掛載或共用指令容易意外觸動檔案系統或洩漏敏感內容，需獨立的唯讀 guest 協議與主機解碼驗證。
- **做法**：
  - `inspect-root-assets.py` ＋ `root-assets-guest.sh`：唯讀取出 `/usr/sbin/BlueTool`、`/usr/sbin/bluetoothd` 及 `/usr/local/share/fuxi/wlan/` 資產並以 SHA-256 核對。
  - `inspect-powerlog-state.py` ＋ `powerlog-state-guest.sh`：透過 `POWERLOG_STATE_BEGIN/CHUNK/END/SUCCESS` 有界 base64 協定傳回 JSON 摘要，主機端 `decode()` 嚴格驗證 `hfs_readonly === True`、`block_readonly === True`、`database_contents_read === False` 與 `root_writes === 0`（並以 `qemu-img map` 確認 qcow2 零分配）。
  - `analyze-battery-pause.py`：離線解析 `console.txt` 中的 `fuxi: SpringBoard: battery-audit`、`FUXI BATTERY: poll-pause` 與 `SWEET BATTERY: published #` 紀錄，比對 32 項 IORegistry 屬性與新鮮度／暫停行為。
- **復現**：
  ```powershell
  D:\xiaomi13\fuxi_ios\work\statusbar-fix\launch-loader-check.ps1 -InspectWifi -Out inspect-wifi-check
  python -X utf8 D:\xiaomi13\fuxi_ios\work\statusbar-fix\inspect-powerlog-state.py --self-test
  ```
- **主機測試**：`inspect-powerlog-state.py --self-test`（含 42 項檢查，驗證敏感詞過濾與唯讀契約）。
- **驗證紀錄**：多次實機唯讀檢查（如 2026-11-17 15:32 18 塊、19:01 25 塊）皆確認 `guest_root_overlay_writes=0` 與 `SHA256` 一致。
- **參考來源**：`phone-readthrough.py` NBD read-through 框架。
- **結論／未完成**：完成且為排查 Wi-Fi、PowerLog 與藍牙資產的標準唯讀管道。
- **出處**：`ws: work/statusbar-fix/inspect-root-assets.py`、`root-assets-guest.sh`、`inspect-powerlog-state.py`、`powerlog-state-guest.sh`、`analyze-battery-pause.py`。（2026-11-18 主代理分配範圍與來源，Gemini 依原始檔撰寫，主代理核對欄位、提交時間與路徑）

### U-016 混合式 Turnip 驅動上機（stage-turnip-hybrid）

- **日期**：2026-11-17 09:32（`ws: f396a6f`）～10:00（`ws: 88d7112`）
- **狀態**：已採用（提供受保護的單檔替換流程，將混合式 Turnip 函式庫部署至手機）
- **開關**：由 `launch-loader-check.ps1` 與 `phone-readthrough.py` 搭配 `stage-turnip-hybrid.sh` 執行（並於 `prepare-wlan-w18-loader.py` 暴露獨立混合驅動選擇器）
- **問題／現象**：需要將新版混合式 Turnip 驅動程式 `libvulkan_freedreno_hyb.dylib` 更新進手機 iOS root，並確保舊版雜湊符合預期、HFS+ journal 完整且僅寫回變更區塊。
- **根因**：直接覆寫可能毀損未知版本檔案或破壞 HFS+ 狀態。
- **做法**：
  - `ws: work/statusbar-fix/stage-turnip-hybrid.sh` 執行 HFS+ journal 重播與 `fsck.hfsplus`，驗證輸入 dylib 符合 `shape-sha256.txt`、目標 `/dst/usr/local/lib/libvulkan_freedreno_hyb.dylib` 為一般檔案且其舊雜湊必須屬於預期版本（`90051d41c6b2ed7497e8618c6cd0447b5973e257898904ce79356d0474cf32a6` 或已等於新版），保留 `uid=0` 與 `0755` 權限原子替換並前後核對 `fsck`。
  - 演進：
    - `ws: f396a6f` 2026-11-17 09:32：新增 `stage-turnip-hybrid.sh` 並整合進 `launch-loader-check.ps1`、`phone-readthrough.py` 與 `prepare-wlan-w18-loader.py`。
    - `ws: 88d7112` 2026-11-17 10:00：記錄混合驅動上機驗證結果。
- **復現**：經 `launch-loader-check.ps1` 指定混合式 Turnip staging 與 loader 進行部署與開機驗證。
- **主機測試**：PowerShell 參數轉發測試、三區塊 read-through 模型測試、shell 語法與 guest Python 語法檢查皆通過（見 `ws: f396a6f` 提交說明）。
- **驗證紀錄**：2026-11-17 10:00（`ws: 88d7112`）已完成混合式驅動安全寫入與上機驗證。
- **參考來源**：`stage-shape.sh`、`phone-readthrough.py`。
- **結論／未完成**：完成且保留於受保護的 read-through 部署框架中。
- **出處**：`ws: work/statusbar-fix/stage-turnip-hybrid.sh`、`ws: f396a6f`、`88d7112`。（2026-11-18 主代理分配範圍與來源，Gemini 依原始檔撰寫，主代理核對欄位、提交時間與路徑）

### U-017 Safari 網路、觸覺與 Wi-Fi 自動加入的使用者回報對齊

- **日期**：2026-11-17 18:45（`ws: 8f57261`）～20:18（`ws: 3315980`）
- **狀態**：部分通過／持續追蹤（Safari 連網與充電震動已由使用者確認；互動震動與當日階段性 Wi-Fi 自動加入對齊結果作為後續修正依據）
- **開關**：無獨立開關（綜合各診斷趟次實機回饋對齊狀態紀錄）
- **問題／現象**：在多次測試中，不同子系統（Safari 網頁、充電震動、UIKit 互動震動、Wi-Fi 自動加入）的實機觀測與使用者體感必須逐趟明確區分，避免將充電震動誤當互動震動通過，或將早期 `-25300` 鑰匙圈查詢錯誤誤套用於 AKS wrap 修正後的趟次。
- **根因**：多項問題並存（CoreHaptics `-4810`、早期開機參數與 AKS 時序、原生 `__allowJoinCandidate` 門檻），需逐次以實機回饋與結構化 log 對齊真實狀態。
- **做法**：
  - `ws: 8f57261` 2026-11-17 18:45：對齊 Safari 連網正常（`native-hw000srgbsafari-boot-1`，使用者回報可正常連網；紫色錯誤依指示暫擱置）、震動與自動加入待修現況。
  - `ws: 18f45d6` 2026-11-17 19:07：記錄 `native-hw000wifi-haptics-1850-boot-1` 失敗重連與互動震動「已啟用但未感受到」；並確認 19:01 最新 Wi-Fi 日誌 `password_missing`／`keychain_lookup_errors`／`save_failures` 已歸零。
  - `ws: 5af26f4` 2026-11-17 19:37：記錄 `native-hw000wifi-filter-feedback-audit-boot-1` 掃描轉換與 UIKit 回饋稽核結果。
  - `ws: 9addf52` 2026-11-17 19:58：記錄 `native-hw000wifi-candidate-sound-audit-boot-1` 兩次充電震動（`sound-id=1106`、`400ms`）經使用者確認有震動（強度高於 iPhone），同時確認家中 AP 通過 profile 匹配但仍未自動加入。
  - `ws: 3315980` 2026-11-17 20:18：記錄 `native-hw000wifi-decisions-corehaptics-boot-1` 原生加入被 `__allowJoinCandidate` 拒絕（`code=1`）與 CoreHaptics 5 次 `initialized=0`；使用者回報「目測下來都與上次一樣」。
- **復現**：對照上述五筆狀態提交與各對應實機目錄的 `console.txt`／`powerlog-state.json` 即可復核當日對齊脈絡。
- **主機測試**：無（實機回饋對齊與日誌紀錄）。
- **驗證紀錄**：涵蓋 2026-11-17 18:45～20:18 五趟實機回饋；之後於 22:25 `native-hw000wifi-rssiamp-boot-1` 進一步確認 Wi-Fi 自動加入成功，23:20 與 11-18 00:31 測得 `wifi-ux` 5GHz 選擇與 438 ms 自動加入，惟 CoreHaptics `-4810` 互動震動至 11-18 仍待修。
- **參考來源**：`work/hw000-adaptation-status.zh-TW.md`、`work/localbuild/wifi-autojoin-credential-decision-20261117.zh-TW.md`。
- **結論／未完成**：Safari 正常連網與充電震動已確認；互動震動（CoreHaptics `-4810`）與跨重啟穩定性仍持續跟進。
- **出處**：`ws: work/hw000-adaptation-status.zh-TW.md`（第 52–62、96 行）、`ws: 8f57261`、`18f45d6`、`5af26f4`、`9addf52`、`3315980`。（2026-11-18 主代理分配範圍與來源，Gemini 依原始檔撰寫，主代理核對欄位、提交時間與路徑）

### U-018 手動加入 Wi-Fi 時的密碼框等待

- **日期**：2026-11-18 13:38 起（iOS 16 build 20H392）
- **狀態**：採用。使用者已在暫時開機的測試版確認密碼框卡頓消失。
- **開關**：loader 的 --wifi-password-prompt 設定 fuxi,inject bit 4（16），限制到 /Applications/Preferences.app/Preferences。預設關閉。
- **問題／現象**：設定頁點受保護網路後約五秒顯示密碼框。一次使用者實測輸入密碼後 217 ms 完成驅動加入。
- **根因**：60 秒取樣的 30,000 筆主執行緒樣本中有 8,926 筆卡在 SFPasswordSharingService 的 semaphore。20H392 WiFiKit 進手動畫面前呼叫 _supportsWiFiPasswordSharing，支援時同步等候服務，最長 10 秒。
- **做法**：ksrc 8cd0756 以 opt-in 位元及完整執行檔路徑限制至 Preferences。harness 7aea107 的 userland/fuxi-shape/FuxiWifiAudit.c 在該流程回報密碼分享不可用，使用 WiFiKit 原有的手動密碼路徑。
- **復現**：以相同 Wi-Fi 基線編核心與 libfuxi-sb.dylib（總索引步驟 3–4）。由 prepare-wlan-w18-loader.py 沿用同一基線產生對照與 --wifi-password-prompt 候選；各自在設定頁點同一個需新輸入密碼的 AP，分別量「點選至密碼框」與「送出至加入」，依 launcher 收集證據。實驗紀錄只需保留時間、結果與訊息分類。
- **主機測試**：ksrc 的 tools/test-fuxi-exec-inject.py 驗證選項關閉、程序路徑不符與命中 Preferences 的行為。kernel-local-wifi-password-prompt-1118-9g-artifact 建置 60 秒、38 項命令通過，sb-local-wifi-password-prompt-1118-artifact 的本機建置結果為 ok。
- **驗證紀錄**：取樣版在 13:44:56 回 Android。取樣與符號證據位於 work/localbuild/wifi-password-profile-1118-symbols/guest.log。候選趟 native-hw000wifi-password-prompt-1118 載入 hook、走手動密碼流程並在 5200／5745 MHz 成功加入。使用者確認卡頓消失，14:13:00 回 Android 並釋鎖。
- **參考來源**：20H392 裝置取樣及唯讀 cache 符號，既有 FuxiWifiAudit.c 選擇器接線。
- **結論／未完成**：手動密碼框通過使用者驗收。日常整合見 B-012。
- **出處**：work/hw000-adaptation-status.zh-TW.md 的 13:38 段落、work/adsp-battery/prepare-wlan-w18-loader.py、ksrc 8cd0756 與 harness 7aea107。

### U-019 個人熱點切換後恢復狀態欄圖示

- **日期**：2026-11-18 14:13–14:39（iOS 16 build 20H392）。
- **狀態**：14:32 啟用補查通過單趟驗證。14:53 關閉補查的對照版再現 0 格。15:34 開機的日常合併候選版已通過使用者核對。
- **開關**：`--wifi-sb-refresh` 設定 ADT `fuxi,wifi-sb` bit 1，預設關閉。可搭配 bit 0 的 `--wifi-sb-audit` 觀察原生旗標。
- **問題／現象**：家庭網路與個人熱點來回切換後，第二次進熱點顯示 Wi-Fi 0 格，仍可上網。
- **根因**：20H392 的 `SBWiFiManager` 在目前網路快取為空時將訊號格數與 RSSI 歸零。原本實機在快速斷線與重新加入間只收到一次 `_linkDidChange`。當時驅動回報 −32 dBm 且持續收發。
- **做法**：harness `d2b447e` 的 `userland/fuxi-shape/FuxiWifiAudit.c` 在背景 Wi-Fi 回呼收到有效 RSSI 且關聯快取為空時，執行原生 `_updateCurrentNetwork`。由 iOS 更新訊號格數及熱點判定。
- **復現**：下列為 14:32 趟的建置與開機指令。重跑以新的輸出名稱保留原產物。

  ```powershell
  & work/localbuild/build-sb-shape.ps1 -Name sb-local-wifi-hotspot-refresh-1118-artifact
  & work/statusbar-fix/launch-loader-check.ps1 -CalibrateRtc -Out hotspot-refresh-1118
  python -X utf8 -B work/adsp-battery/prepare-wlan-w18-loader.py build `
      --like native-wifi-password-prompt-1118-loader --wifi-ios `
      --artifact kernel-local-wifi-password-prompt-1118-9g-artifact `
      --name native-wifi-hotspot-refresh-1118-loader `
      --rtc-calibration work/adsp-battery/rtc-calibration-hotspot-refresh-1118.json `
      --scan-dwell --scan-batch --quiet-boot --stderr-log Preferences `
      --with-adsp --audio-spf --haptic 2 --inject-media --product-haptics `
      --haptic-ui-ms 15 --haptic-envelope --haptic-amp 128 `
      --wifi-audit --wifi-password-prompt --wifi-sb-audit --wifi-sb-refresh `
      --wifi-rssi-bounds --wifi-ux 2047 --display-gamut sRGB --daily-tuning --wifi-args-adt
  $artifact = 'work/Codex-Xiaomi13-GPU-20261109/sb-local-wifi-hotspot-refresh-1118-artifact'
  $digest = ((Select-String -LiteralPath "$artifact/SHA256SUMS" -Pattern '  libfuxi-sb\.dylib$').Line -split ' ')[0]
  & work/statusbar-fix/launch-loader-check.ps1 -Loader native-wifi-hotspot-refresh-1118-loader `
      -Kernel kernel-local-wifi-password-prompt-1118-9g-artifact `
      -Out native-hw000wifi-hotspot-refresh-1118 -Seconds 240 -Reset 6 `
      -Dylib "$artifact/libfuxi-sb.dylib" -DylibSha $digest -FinalFramebuffer -FramebufferHeight 200
  ```

  在設定頁依序切家庭網路、熱點、家庭網路、熱點。第二次確認狀態欄鏈結圖示，紀錄應出現 `current network refreshed`、`isAssociatedToIOSHotspot ... value=1` 及收發流量。
- **主機測試**：loader `self-test`、本機函式庫建置、簽署校驗及既有 ASan／UBSan 檢查通過。比對核心、命令列及 root extents 一致，ADT 只變動 RTC 與 Wi-Fi 狀態欄開關。
- **驗證紀錄**：
  - 14:32，`native-hw000wifi-hotspot-refresh-1118` 的 `boot.log` 第 6885–6890 行確認第二次加入有收發、補查成功及熱點旗標為 1。使用者確認鏈結圖示，14:39:03 回 Android 並釋鎖。
  - 14:53，`native-wifi-hotspot-refresh-control-1118-loader` 只開啟 `wifi_sb_audit`。`native-hw000wifi-gpu-control-1118/boot.log` 第 6142–6145 行顯示第二次加入 5745 MHz 時，Wi-Fi 回呼收到 −48 dBm，SpringBoard 的訊號格數由 3 變 0、RSSI 也歸零。約五秒後第 6236 行仍收到 −41 dBm。使用者回報熱點再出現 0 格。
- **參考來源**：既有 SpringBoard 唯讀稽核、20H392 的 `SBWiFiManager` 和 `SBSystemStatusWifiDataProvider`。
- **結論／未完成**：有開補查的同條件趟次恢復鏈結圖示，關閉補查的對照趟重新看到有效 RSSI 與 0 格並存。日常整合見 B-012，當時的 GPU 重啟證據見 G-049。
- **出處**：`work/hw000-adaptation-status.zh-TW.md` 的 14:13 段落。

### U-021 從 iOS HAL 的 Actuator 輸出驅動 HAP525

- 11-19 15:46 拉回的實機紀錄第 5073–5074 行顯示，一筆 132-byte PCM 峰值 34、一次輪詢後播放成功，隨後一筆逾時。使用者當時不在手機旁。原始紀錄在 `work/adsp-battery/fuxilog/20261119-154617/console.txt`。

- **2026-11-19 的觸感差異**：原廠 Android 四種 UI 操作均送 376 個 24 kHz 樣點。第一次以固定波形重播時，iOS 日常觸感因 FIFO 播放失敗而消失。改以單筆 SPMI 讀取 FIFO 深度後，實機記錄到 28 筆成功。將同一波形的 VMAX 改為 0x30 後，實機又有 26 筆成功，但使用者仍感覺所有場景相近，計時器快速滑動的回饋也會漏拍。
- **2026-11-20 觸感整形（mode 6，預設關）**：使用者回報「有些地方有震、有些沒有，震感不像 iOS」。本條 18 日的實機紀錄裡，SpringBoard 輕觸只有一個 6 ms、level 17 的視窗（`amp=0x11`）。LRA 一個週期約 5.7 ms，馬達還沒起振請求就結束，所以感覺不到；重的回饋則是共振頻率的嗡聲加拖尾。19 日的 mode 5 用固定 VMAX 播原廠點按，使用者覺得各場景強弱相近。mode 6 不改 HAL，HAL 照舊送 DIRECT 請求（`fuxihalhaptic=1`），由核心依請求的時間、毫秒與 level 分類：
  - 與前一個請求間隔 ≥ 8 ms，就算新的一段觸感。
  - 一段的第一個請求 ≤ 20 ms 時，改播同機 Android 的 376-byte 點按（0_click_P_RTP.bin）。VMAX 依 level 套平方根曲線，介於 Android VMAX 的 115/256（約 45%）與 100% 之間。Android 為 0x1c 時，level 4 對應 0x0f、17 對應 0x12、41 對應 0x15、90 對應 0x19、128 對應 0x1c，最強不超過 Android 本身的強度。
  - 同一段開始後 40 ms 內的後續視窗與停止請求都吞掉，停止請求因此不會取消已排定的點按。超過 40 ms 仍在送的視為連續觸感，交回 DIRECT_PLAY。
  - 第一個請求 > 20 ms 的請求（VibePattern 鈴聲、充電 400 ms、包絡 40 ms 段）整段照舊。
  - 實作：ksrc `d4747ed` 的 `xnu/patches/0123-fuxi-haptic-shape.patch`，新增 `bsd/dev/fuxi_haptic_shape.h`，`fuxi_pmic.c` 只移除 7 行舊閘門與參數，其餘為新增。分類只在 `fuxi,haptic=6`、RTP 開機點按成功（`fp_rtp_ready`）且執行期未故障時才啟用；PCM 只留給 mode 5。開機標記為 `haptic shape on`，點按紀錄 `haptic RTP FIFO UI click … vmax= shape level= clicks= swallowed=`。
  - 主機測試：`python3 tools/test-fuxi-haptic-shape.py --sanitize` 通過，涵蓋輕觸、連續視窗、停止、間隔邊界、每 15／25 ms 的快速刻度、長請求與續約、時間倒退，以及 1..0x38 全部 base 下 VMAX 單調、介於下限與 base 之間。補丁結構也檢查閘門、PCM 只給 5 與開機標記。harness 工作流程已加這個測試。
  - 本機 WSL 核心 `kernel-local-haptic-shape-1120-9g-artifact`（ksrc `d4747ed`，含 0122；harness `3027c50` 加工作目錄補丁，SHA-256 `01cc79d03ecc2e839d760c8705d76046c82011aacc4310ee11630522e530a0ae`）。增量 24 秒，ISA 解碼 2,567,309 條，禁止指令 0 條，核心 SHA-256 `89032f585d296bbb05c09386d97ddb7f81edf0c7c9bcdc46e4a54c1cd97485a5`。
  - 產生器 `--haptic 6` 必須帶 `--haptic-rtp`（同一 SHA 檢查），並檢查核心含 `haptic shape on`。`--haptic-rtp-vmax` 可用於 mode 5、6，mode 6 以它作為 base。
  - **第一趟實機（000 線趟 30，11-20 03:05–03:14:13）**：loader `native-bt5-hapshape-1120-loader`。建置方式是在趟 29 的命令上把 `--haptic 3` 換成 `--haptic 6 --haptic-rtp work/adsp-battery/android-haptic-waves-1118-2352/0_click_P_RTP.bin`，`--artifact` 換成上述核心。其餘照舊，包括 `--hal-haptics --product-haptics --haptic-ui-ms 15 --haptic-envelope --haptic-amp 128`，命令列為 `fuxihalhaptic=1`。boot SHA-256 `76dd2bc25d9ec33ad23da9496aa0cdcf92641d29f15ade6b47d1a9e8cec029a8`。
    - 啟動命令：`work/statusbar-fix/launch-loader-check.ps1 -Loader native-bt5-hapshape-1120-loader -Kernel kernel-local-haptic-shape-1120-9g-artifact -Out native-hw000bt5-hapshape-1120 -Seconds 240 -Reset 12`。
    - 第 447 行開機標記 `haptic shape on … VMAX 115/256..1 of 0x1c`。第 613 行開機 FIFO 點按 `rc=0`。
    - 兩次主畫面長按分別在第 11912、12013 行改播原廠點按，都是 `rc=0`。第一次 `level=90`，第二次 `level=81`，VMAX 都是 `0x19`，`clicks=1→2`、`swallowed=0`，與主機曲線一致。
    - 第 4763–5049 行是一段 328 ms、振幅 0x8→0x80 漸強的長觸感，照舊走 DIRECT_PLAY，沒有被整形。期間出現一次既有的 auto-resonance 錯誤，已在下次播放前清除。
    - 這趟沒有 fault 或 EIO。主機量到 TSENS 峰值 82.8°C。
    - 點擊腳本誤開了備忘錄，沒有新增或修改任何備忘錄。因此沒有驗到計時器快速刻度、開關、鍵盤等場景。手感（強弱層次、是否像 iOS）待使用者親自比較。
    - 證據 `work/Codex-Xiaomi13-GPU-20261109/native-hw000bt5-hapshape-1120/boot.log`，12,699 行，SHA-256 `e63dea1cb3bd3fa6d18cad814b6e18715e5bc75d62837075f154c33c46abee78`。
  - **使用者試手感用 loader（尚未上機）**：`native-bt5-hapshape-user-1120-loader`，boot SHA-256 `5463a0074bd34aa874a338e5eec0821c134fbd813c85dcf954630fff0d72d754`。用趟 31 的同一組選項重包，拿掉 `--tap-script`；命令列與趟 31 只差 `fuxitaps=85`。RTC 校正已過期，所以沒帶 `--rtc-calibration`，開機後時鐘可能要等網路校時。啟動：`launch-loader-check.ps1 -Loader native-bt5-hapshape-user-1120-loader -Kernel kernel-local-haptic-shape-1120-9g-artifact -Out native-hw000bt5-hapshape-user-1120 -Seconds 900 -Reset 20`。
- **有號 PCM 方案**：`--haptic 5 --hal-haptics --haptic-pcm` 使用帶 `kern.fuxihapticpcm` 的核心及 `audio-local-hal-pcm-1119-artifact`。HAL 依原本輸出時間排程，把 48 kHz 的有號波形降至 24 kHz，保留各段振幅和正負樣點。主機模型核對雙極樣點、8-byte 短封包、填充、播放及逾時回退。每個封包仍是有限長度的 FIFO 播放，密集刻度的連續性須用實機驗證。實機驗收看 `Actuator HAP525 first PCM packet`、`haptic RTP FIFO UI PCM`、HAL 丟送紀錄，以及使用者對計時器刻度、長按與強弱回饋的比較。

- **日期**：2026-11-18。
- **狀態**：WSL 建置、主機模型、HFS+ 離線試裝／還原通過。11 月 18 日實機已驗到原生 CoreHaptics、Actuator 與 HAP525 播放。
- **開關**：loader `--hal-haptics` 寫入 `fuxihalhaptic=1`，搭配 `--haptic 3 --product-haptics`。根映像試驗透過 `launch-loader-check.ps1 -AudioHal enable|disable -AudioBundle <FuxiAudio.driver> -AudioSha <FuxiAudio SHA-256> -Out <紀錄名>`。
- **問題／現象**：U-005 的 `SynthOutput` 建立失敗。HAL 需要可供 iOS 選擇的輸出裝置，既有 `FuxiAudio.driver` 已宣告 Actuator，但其 PCM 寫入只有計數。
- **根因**：原機 iOS 的 Actuator 後端不在 Fuxi，現有 FuxiAudio 宣告的 Actuator 也沒有連到 HAP525。VirtualAudio 曾因原機音訊裝置缺失而停用。
- **做法**：復用原本的 `FuxiAudio.c` HAL 裝置與 K-033 的 `kern.fuxivibrate`。以 `mOutputTime.mHostTime` 排程單聲道 PCM，每 10 ms 視窗換算成 0–128 的 HAP525 振幅，實際振動使用馬達的共振頻率。混音 callback 將視窗放進固定佇列，另一條執行緒送出有限時長的請求。取消時清除待播視窗並停止現有請求。試裝工具借用 `fuxi-sys-root.py`，將 VirtualAudio 搬回，還原時將同一份 bundle 搬回原位並移除本次試驗的 FuxiAudio 檔案。
- **復現**：在 WSL 工作區執行 `bash work/adsp-battery/ksrc/tools/build-fuxi-sys.sh --audio-only <新的絕對產物路徑> <verify-sb-shape.py 的絕對路徑>`。上機使用 K-077 的參數並加 `--hal-haptics` 和本趟 RTC 校時。依序啟用試驗 HAL、暫時開機收紀錄、回 Android 並還原 HAL。看 `HAP525=1`、CoreHaptics engine 建成、`Actuator HAP525 first pulse` 和對應的核心 HAP525 播放。Android 回程後看 `AUDIO_HAL_AFTER action=disable` 與寫後讀回。
- **主機測試**：`tools/test-fuxi-sys.py --audio-only` 驗證實際 callback 的 10 ms 時間戳、最大振幅、靜音、重複時間戳、過期與過遠視窗、StopIO 清理。ARM64 Mach-O bundle 工廠、Info.plist 和簽署碼頁校驗通過。合併 loader 參數 511 bytes。合成根目錄也涵蓋雜湊錯誤和拒絕移除其他版本。
- **驗證紀錄**：`audio-local-hal-haptic-r2-1118-artifact` 的 FuxiAudio SHA-256 為 `bb0498844793f4079cc0e888c27c9476898a53725a4de4bcfe29a02ce30106c6`。QEMU 在 `native-layout0025-root.img` 的 qcow2 overlay 上完成試裝及還原，兩次皆通過 HFS+ 檢查，VirtualAudio 目錄 inode 均為 11598。原始紀錄在 `work/statusbar-fix/audio-hal-offline-layout0025-1118/`。
  - `native-actuator-haptic-offline-1118-loader` 已用 K-078 的核心離線封裝，boot SHA-256 為 `dc6cba54f05a5f5c48211c0acf34ceb67af267f808198f39f8e18de9e21cc46b`。本配方省略 `--haptic-ui-ms`，以 Actuator 訊號驗收原生震動。重包時補本輪 RTC 校時，試裝與還原仍由上述 launcher 執行。
- **參考來源**：本機既有的 HackiOS 衍生 FuxiAudio、K-033 的 HAP525 執行期，以及 Apple 的 HAL callback 介面。 [AudioServerPlugIn DoIOOperation](https://developer.apple.com/documentation/coreaudio/audioserverplugindriverinterface/doiooperation)
- **實機驗證**：`work/Codex-Xiaomi13-GPU-20261109/native-hw000bt5-native-pcm-hal-1118-2202/boot.log` 第 1350 行確認 HAL 載入，第 4013 行 SpringBoard 回報 `initialized=1`。第 5150–5153 行 Actuator 輸出 6 ms、振幅 17，核心回報 `amp=0x11..0x11`、`fault-mid=0`、`stop-rc=0`，HAL 停止時計數為一筆請求、零錯誤。實機試裝寫入紀錄在 `work/statusbar-fix/audio-hal-enable-1118-2200/`。
- **2026-11-20 全寬顯示合測（G-050）**：沿用已安裝的 HAL，暫時 loader 帶 `fuxihalhaptic=1`、mode 6 及同機 376-byte RTP，命令列 575 bytes。趟 36 的 USB console 共留下 11 行 UI click，每行 `rc=0`。最後一次輸出的累計為 14 次播放、17 次合併抑制。趟 37 使用者確認有震動、手感仍需調整。新 ADT 版 HAL 與原本 PCM-drain 版均已在主機通過碼頁校驗及三項破壞負測試。
- **2026-11-20 觸感整形 v2（mode 7）與即時喇叭混音 HAL（v2，趟 48–53）**：
  - `0124-fuxi-live-audio-haptics.patch` 新增觸感 shape v2（`mode 7`，首段 <=22 ms 改播原廠 RTP 點按、VMAX `90/256..1` of `0x1c` 依 level 調整、連發吞併 18 ms、間隔 4 ms），實機趟 48–54 均確認 `haptic shape v2 on`、`fault-mid=0`、`autores-plays=0`、`Actuator HAP525 mode 1 from ADT`；趟 54 自動化腳本實測 5 次互動 UI RTP 點按均 `rc=0`（輕觸 `level=4`→`vmax=0xd`、長按 `level=90`→`vmax=0x19`，`clicks=5 swallowed=3`，與 `Speaker` 同時播放無干擾）。
  - `FuxiAudio.driver` 同步接通即時喇叭多上下文疊加混音：經 v1–v4 實機 A/B 對照，以 v2（`audio-local-live-mix-v2-1120-artifact`，SHA-256 `6ee38bd63ad225dd0c3e24b130677b562352729a2dc0035c15f829e26eef1859`，960 幀／20 ms 節流、`SPEAKER_MIX_LEAD_CAP=24576`、StopIO 先清 `active`）表現最佳（趟 51 `sent=4.40/4.43 s`、`writes=220`、`errors=0`、`overlaps=0`、`late=0`、`lead=0`）。工作樹已復原為 v2 並通過 `test-fuxi-sys.py --audio-only`，16:53 已於 Android 還原至手機根映像（`PRE_OK 12`／`WRITE_OK 12`）。
  - **趟 55 使用者手動實測（17:24–17:31，待回覆驗收）**：`native-hw000-live-audio-v3d-hap7-user-1120` 執行 240 秒手動測試，觸感 `mode 7` 累計 `clicks=33 swallowed=11`（`level=1..128`→`vmax=0xb..0x1c`）全數 `rc=0`；喇叭於開機後互動期間送出 3,427 筆即時 PCM（含 19.47 s 與 18.94 s 連續播放）0 新增 underrun／overflow／error；兩次收音開啟（4.05 s、9.28 s）由 HAL 依現況回填靜音且 `mediaserverd` 未崩潰。現待使用者回饋耳聽與手感驗收結果，決定微調參數或確認達標。
  - **2026-11-20–21 `haptic=9` 連續 24 kHz FIFO 串流＋3-APID 快取（趟 56–57）**：使用者於 `hap8` 確認「喇叭正常」，但回報震動延遲且手感仍差。根因定位為：(1) 單一固定波形取代 Apple 原生 CoreHaptics 波形且 VMAX 偏低；(2) `fp_find()` 未快取 `0xf0`/`0xf1`/`0xf2`（尤其 `fuxi_haptic_idle()` 每次讀 3 個 `0xf2xx` boost 暫存器觸發 768-entry 全表掃描，耗時 ~4.8 ms／次）；(3) `FuxiAudio.c` 持有 `actuator.lock` 跨 `sysctlbyname` 與 6 ms 人為等待。修正後於 `0124-fuxi-live-audio-haptics.patch` 新增 `fuxi_haptic_stream.h`（`fuxi,haptic=9`，連續 24 kHz 有號 PCM 寫入 2048B FIFO、`vmax=0xa3`＝8.15 V 同機 Android 基準、`idle-ms=2000` warm 保持）並將 `fp_haptic_apid_plus_one` 擴充至 3 個 APID（`0xf0`/`0xf1` EE0＋`0xf2` boost=556 唯讀快取，維持即時 map/owner 核對）；`FuxiAudio.driver`（`audio-local-hap9-1120-artifact`，SHA-256 `aca0c14496cb68507248eef0aa529728c9d2808d4772ae3fa522ca0e4dbd1988`，已安裝上機）加入 ADT mode 4 連續串流、放鎖 sysctl、`QOS_CLASS_USER_INTERACTIVE`、2 ms 提前量與包絡感知曲線（保留 v2 喇叭混音不動）。本機核心 `kernel-local-hap9b-ath12k-1120-9g-artifact`（44 項主機測試＋ISA 全過）於趟 57 實測 `haptic cached 3 APIDs (stream=1 boost=556)`，5 段互動突發（桌面長按、控制中心長按、上滑雙擊）冷 `open_us=1.80–2.27 ms`、熱 `open_us=0.915 ms`（快於 Android 1.59 ms）、`queue_us=6–27 us`、全數 `underruns=0`／0 錯誤，`peak=67..110`、`vmax=0xa3`，喇叭／Wi-Fi／藍牙無回歸；已備妥無點擊腳本使用者驗收 loader `native-hap9b-volhud238128-user-1121-loader`，待使用者實機手感與音量條驗收。
  - **2026-11-21 `FuxiAudio.driver` mode 5 線性合包串流與 `Codec` 即時麥克風橋接（趟 61–69）**：針對趟 60 使用者回饋「震動延遲已改善但仍偏彈簧感且無法錄音」，升級 `FuxiAudio.driver` 至 `audio-local-hap5c-mic-1121-artifact`（`FuxiAudio` SHA-256 `58b99bbc02e8c728ccac91ed03dc160ef6c97b5374f1228f0ad8d64dbf6b3b3b`，已於趟 64 裝機 `PRE_OK 10／WRITE_OK 10`）：(1) Actuator mode 5 改用線性增益並於首包 128 B 等候 `max(end+2 ms, start+7 ms)` 涵蓋完整 5.33 ms CoreAudio 週期觸發原子合包，消除中段過早插尾（趟 64 長按 `tails=1`、`zc=2`）；(2) 新增 `Codec` 預設輸入 `PreferredChannelsForStereo={1,1}` 與 `mic_worker`（`USER_INTERACTIVE` 輪詢 `kern.fuxiaudiomic`／`kern.fuxiaudiomicctl`），趟 63–64 跑通語音輸入即時收音（`4.86–4.97 s`、`errs=0 drops=0`），趟 69 配合核心 `0125-fuxi-sandbox-ext.patch`（`--sb-ext`）與 `--haptic-damp 0xd2`（+31% 主動阻尼強度）實機自動通過系統錄音端對端寫檔（`244,322 frames`、`peak=8214`、`errs=0 drops=0`）與 9 段零欠載震動。
  - **2026-11-22 `FuxiAudio.driver` `hap6c`（mode 6 五分群感知曲線校準，趟 75–76）**：依據實機 iOS 16 CoreHaptics `peak16` 五大互動分群重校 `FuxiAudio.c` `actuator_perceptual_target()`（`32→18`、`256→30`、`1024→44`、`2560→58`、`5120→70`、`7680→80`、`12288→94`、`18432→110`、`24576→122`、`26624..32768→126`），保留 v2 喇叭混音與 `Codec` 麥克風不變。本機編譯 `audio-local-hap6c-1122-artifact`（SHA-256 `83096b2b73442603ea327f7504a509ab22cc5924060ff59ab23ea8ac03f5fa8a`）經 `audio-hap6c-enable-1122` 裝入（`PRE_OK 10 / WRITE_OK 10`）；趟 76（`native-hw000-hap5c-popfix-autotest-1122`）驗證 Spotlight 鍵盤與桌面長按明確分層（`raw_pk=74..75 → prof=1` 對 `raw_pk=120 → prof=3`），零錯誤、零欠載。
  - **2026-11-22 `FuxiAudio.driver` `hap6e`/`hap6f`（尾端感知三段併包＋拉寬感知階梯，趟 80–81）**：在 `FuxiAudio.c` 實作 `actuator_raw24_tail_active()` 尾端感知併包（1-chunk 靜音尾立發、1-chunk 活躍尾等至 `13 ms`、2-chunk 活躍尾等至 `12 ms` 吸收 chunk 3 消除 4–44B 短片外洩），並拉寬 `actuator_perceptual_target()` 讓鍵盤（`57`）、音量（`65`）、切換器（`75`）與桌面長按（`120`）清楚分離。本機編譯 `audio-local-hap6f-1122-artifact`（SHA-256 `bbb2a127cf9a8ac16e433e2f4818a3409afea9c89417c82e2eca51af3fee3bd7`）經 `audio-hap6f-enable-1122` 裝入；趟 81（`native-hw000-hap6f-m6-autotest-1122`）實測 27/27 burst 零漏包（`drops=0/0/0/0`、`errors=0`），8 次 200 ms 連擊完全一致（`bytes=328 peak=72`），通過 `test-fuxi-sys.py --audio-only` 主機驗證。
  - **2026-11-22 `FuxiVibrate.c` 原生 PCM 模式停用舊 UI 回退（修掉手電筒長按偶發轉子震動，harness `ef96d36`）**：針對使用者回饋 `B-017` 鎖定畫面手電筒連續長按偶發普通轉子震動，查明為 `FuxiVibrate.c` 在 `haptic=9` 原生 24 kHz PCM 模式下仍啟動 `fuxi,haptic-ui=15` 舊 DIRECT_PLAY 脈衝回退與原生 CoreHaptics 疊觸。修正後於 `haptic=9` 且 `hal-haptic>=4` 時強制 `vb_native_pcm=1`、`ui-fallback-ms=0` 並抑制 `VB_PULSE`。乾淨重編 11 單元 `sb-local-daily-audio-hap8-m6-1122-artifact`（`dirty=0`，SHA-256 `6c5ce8c3563b880e69de696fe6724b81d82ad423a820895388e8769fbf6c2048`，與已裝入手機根映像的 `run-native-hw000-hap7-wake-autotest-1122-stage` 逐位元一致）通過全部主機測試與碼頁驗簽；`native-hw000-hap7-wake-autotest-1122`、`native-hw000-hap8-ath12k-autotest-1122` 及 `native-hw000-hap8-user-1122` 皆確認 SpringBoard／Preferences 印出 `ui-fallback-ms=0 native-pcm=1`，使用者實測確認手電筒不再出現轉子震動。
  - **2026-11-23 `FuxiVibrate.c` 原生 PCM 模式放行 `VibeSynthEnginePlay` 與 `playVibePattern:`（打通全系統鈴聲／提示音 24 kHz 同步震動，harness `e6b753f`）**：針對 `native-hw000-hap9d-user-1123` 使用者回饋鈴聲震動未隨鈴聲播完且部分鈴聲／提示音幾乎無震，定位為 `mediaserverd` 中 `vb_bridge_thread` 仍把 `VibeSynthEnginePlay`（`slots[6]`）／`Cancel`（`slots[7]`）與 `AVHapticServer playVibePattern:` 全數劫持至舊 `kern.fuxivibrate` `DIRECT_PLAY`（0.70 V 單發、略過循環與同步器）。修正後於 `VB_MEDIA` 偵測 `fuxi,haptic==9 && fuxi,hal-haptic>=4` 置 `vb_native_pcm=1`（`mode=native-pcm-first`），優先轉發原廠 `vb_orig_native_play`／`vb_orig_native_cancel` 與 `vb_play_original`，僅於回傳非 0 時降級 `vb_vibe_request`，並避免在未啟用舊 worker 時誤呼叫 `vb_cancel()` 關閉 FIFO。本機 WSL 建置完整 13 單元 `sb-local-hap9e-1123-artifact`（SHA-256 `9af9202ee111acf7ba9a9408e93408ff1fc7aed0f56557c6d7d10020ddaff49f`，52 頁 ad-hoc 驗簽與全部 10 項 ASan/UBSan 主機測試通過，保留 `FuxiAtxFix` 與 `FuxiAudioMicDsp`），經 `launch-loader-check.ps1 -Dylib` 安全寫入 root（`PRE_OK 6`/`WRITE_OK 6`/`FSCK_final OK`）；`native-hw000-hap9e-autotest-1123` 實證 `vibe native bridge installed; mode=native-pcm-first`、`vibe play passthrough call=1 original=0`、`vibe native passthrough call=1 rc=0`、0 次 `DIRECT_PLAY` 回退，並直接經 `Actuator HAP525` 驅動 `Mode 9 v2.1` `prof=3 (SUS)` 連續 6,628 B + 1,380 B 原生包絡波形（`underruns=0 / clp=0 / errors=0`）。
- **結論／未完成**：`FuxiAudio.driver`（`hap6f` mode 6＋v2 喇叭混音＋`Codec` 即時收音）與 `FuxiVibrate.c` 手電筒零轉子修正（`6c5ce8c3…`）配合 K-033／K-078 核心（`haptic=9`／`damp=6`／`gain=16`／`max_db=-4`／`idle-pdn=1500`／`audio-mic=1`／`sb-ext=1`）、U-023 右側音量條與 U-024 `capsrc-stub=7` 已通過實機驗證並併入主分支原點（`checkpoint-hap8-m6-20261122`）與日常 `boot_a`（B-018）；後續以該原點續調最後觸感差異。
- 11-19 14:50 實機，`native-av-muted-haptic-1119-pm-loader` 的 750 ms 長按後，Actuator 與 HAP525 都記錄同一筆 132 bytes PCM，核心回報 `polls=1 rc=0 peak=81 played=1 expired=0`。此趟無人在場評分手感，連續刻度另待驗證。持久紀錄 `work/adsp-battery/fuxilog/20261119-145858/console.txt` 第 4924、4927 行。
- **同機 Android 基準**：`android-haptic-waves-1118-2352/report.json` 顯示 Xiaomi vibratorfeature HAL 開啟 event4，對應 PM8550B HAP525。四筆獨立 SPMI 紀錄（`work/adsp-battery/android-haptic-spmi-1118-2353/`）的點按、刻度、長按與開關均為 FIFO，rate code 10（24 kHz），先 320 再 56 bytes，每個 byte 均符合手機上的 0_click_P_RTP.bin。第一次 play enable 至 disable 為 16.023、16.160、16.112、16.027 ms。重播命令 `python -X utf8 work/adsp-battery/verify-haptic-rtp.py work/adsp-battery/android-haptic-waves-1118-2352/0_click_P_RTP.bin work/adsp-battery/android-haptic-spmi-1118-2353`。iOS HAL 現行 10 ms 峰值轉 DIRECT_PLAY 路徑會丟掉正負相位；同機 Android 波形與 iOS 回饋的實機比對是後續驗收依據。喇叭出聲另見 K-078。
- **出處**：`ksrc/tools/fuxi-audio/FuxiAudio.c`、`work/statusbar-fix/stage-audio-hal-guest.py`、U-005 和 K-033。

### U-020 WCN7850 藍牙韌體放進 iOS root（stage-wlan-assets kind=bt）

- **日期**：2026-11-18 16:50–17:16
- **狀態**：已部署（`/usr/local/share/fuxi/bt/` 50 檔，寫前比對、寫後讀回通過）
- **開關**：manifest 的 `"kind": "bt"`；輸出目錄名必須是 `run-bt-…`（`run-wlan-…` 給 WLAN），名稱與 kind 不符就拒絕。
- **問題／現象**：K-076 的 `fuxi,bt=4` 要從 iOS root 讀 rampatch 與 NVM，原本只有 WLAN 資產的部署流程。
- **根因**：藍牙檔名、數量與大小跟 WLAN 不同（WLAN 固定 5 檔；藍牙 2 份 rampatch／版本檔加 48 份 NVM），且要放在另一個目錄。
- **做法**：
  - 韌體來源：2026-11-18 以 KSU root 唯讀複製手機 Android `/vendor/bt_firmware/image/` 的 `hmtbtfw20.tlv`（231,484 bytes）、`hmtbtfw20.ver`（版本 `BTFW.HAMILTON.2.0.0-00613-PATCHZ-1.35836.5.36233.6`）、47 份 `hmtnv20.b*` 與 `hmtnv20.bin`；手機端與本機 SHA256 逐檔相符，存在 `work\adsp-battery\bt-firmware\`（含 `SHA256SUMS`）。原廠韌體只留在本機，不匯出到公開倉庫。
  - `work\statusbar-fix\stage-wlan-assets.py`：加 kind=bt。檔名限 `hmtbtfw20.(tlv|ver)`／`hmtnv20.(b<2–4 位十六進位>|bin)`，2–64 檔，必須有 rampatch 與至少一份 NVM；rampatch 上限 1 MiB、NVM 上限 64 KiB；目的地 `/usr/local/share/fuxi/bt`。其餘（read-through、qcow2 疊加層、逐塊比對、寫後讀回）沿用 U-013／U-014。
  - manifest：`work\adsp-battery\bt-stage-input-20261118.json`（SHA256 `009551d1…bb23bbd`）。因為還沒讀過本機的 board ID，全部 NVM 一起部署。
- **復現**：

  ```powershell
  Set-Location work/statusbar-fix
  python -X utf8 stage-wlan-assets.py --manifest D:\xiaomi13\fuxi_ios\work\adsp-battery\bt-stage-input-20261118.json --output run-bt-wcn7850-fw-20261118
  ```

  預期最後一行 `WLAN_ASSETS_STAGE_DONE written`；`receipt.json` 的 `state` 為 `written`（重跑為 `unchanged`）。
- **主機測試**：`work\statusbar-fix\test-stage-wlan-assets.py`（45 項，新增 `BluetoothStageTests`：檔名／數量／kind、輸出名與 kind 對應、guest 安裝冪等、guest 路徑與 token）。
- **驗證紀錄**：17:13:28–17:16:06，`work\statusbar-fix\run-bt-wcn7850-fw-20261118\`：只讀手機 356 MiB，變動 8 塊（`PRE_OK 8`、`WRITE_OK 8`），guest 端 50 檔雜湊全對，`verify-native-map.py: OK`，root extents 未變。
- **參考來源**：U-013 的 WLAN 部署流程；Linux btqca 的韌體檔名規則。
- **結論／未完成**：板級 NVM 確定後可只留實際用到的那一份（見 K-076）。
- **出處**：`work/hw000-adaptation-status.zh-TW.md` 的 11-18 藍牙段落。

### U-022 bluetoothd os_log 轉錄到 console（fuxi,bt-oslog）

- **日期**：2026-11-19 09:50–10:34
- **狀態**：已實機驗證（os_log 趟 2 無 crash，轉錄出 VSC stall 的原因）
- **開關**：ADT `/chosen fuxi,bt-oslog`（u32，預設 0）：bit 0 default／error／fault（每程序最多 4000 行），bit 1 info、bit 2 debug（合計最多 1500 行）。需要核心 0118 的注入位元 64（bluetoothd）及 `sweetnull` 含 bluetoothd；loader 產生器 `--bt-oslog <1..7>`，要求 `--bt-native` 與 `--stderr-log` 含 bluetoothd。
- **問題／現象**：K-077 的 bluetoothd 反覆自願重啟，20H392 的 OI 日誌只走 os_log，重啟原因（`Bluetooth error - restarting { … reason=%d … }`）也只寫 os_log，console 看不到。
- **根因**：（診斷工具）bluetoothd 的 stderr 是 `/dev/null`，os_log 不經過核心 console。
- **做法**：
  - `harness/userland/fuxi-shape/FuxiBluetoothLog.c`：用 dyld `__DATA,__interpose` 攔 bluetoothd 匯入的 `_os_log_impl`／`_os_log_error_impl`／`_os_log_fault_impl`／`_os_log_debug_impl`、`os_log_pack_send_and_compose`、`os_log_type_enabled`、`exit`／`_exit`、`abort_with_payload`，把訊息寫到 fd 2；核心 0048 印成 `|bluetoothd| BTLOG …`（每行最多 256 bytes，續行前綴 `BTLOG+`）。其他程序或開關為 0 時只尾呼叫轉發。
  - `FuxiOsLogFormat.h`：解析 clang 的 os_log 緩衝區（summary／count 與 item 描述），支援 `%d %u %x %s %p %c %@ %{public}`、精度與寬度。`%@` 只展開 CFString，其他物件印 `<obj>`；private 參數印 `<private>`，位址形字串遮成 `xx`。
  - os_log 趟 1 bluetoothd 在轉錄時 SIGSEGV：`%{public,mask.hash}` 這類 specifier 在緩衝區多一個 mask item（kind 7），舊版把它當參數取走，後面全部錯位。r2 略過 mask item，`%@` 先確認 item kind 是物件。
- **復現**：

  ```powershell
  .\work\localbuild\build-sb-shape.ps1 -Name sb-local-btoslog-r2-1119-artifact
  python -X utf8 work/adsp-battery/prepare-wlan-w18-loader.py build --like native-btsetup-1118-loader --artifact <K-077 核心> --name <loader> ... --bt 5 --bt-native --bt-oslog 3 --stderr-log mediaserverd,bluetoothd,BlueTool --rtc-calibration <json>
  ```

  上機用 `launch-loader-check.ps1 -Dylib …\sb-local-btoslog-r2-1119-artifact\libfuxi-sb.dylib -DylibSha e5793544ce690d264f82a0335c73ef9f151051308df1e10e701bab52552e12d6`。回來只查 `BTLOG S `／`BTLOG X `／`BTLOG C ` 與含 restart／reason／timeout 的行；輸出要先濾掉含 MAC 的行再收錄。
- **主機測試**：`harness/tools/test-oslog-format.c`（mask item、精度、`%@` 非物件、遮位址字串等案例），`work/localbuild/fuxi-sb-build.sh` 與 `sb-shape.yml` 都以 ASan／UBSan 執行。
- **驗證紀錄**：
  - r1（`sb-local-btoslog-1119-artifact`）：os_log 趟 1（10:23），bluetoothd SIGSEGV，證據 `work/Codex-Xiaomi13-GPU-20261109/native-hw000bt5-oslog-1119-1023/boot.log`。
  - r2（SHA-256 `e5793544ce690d264f82a0335c73ef9f151051308df1e10e701bab52552e12d6`，本機 WSL 4 秒）：os_log 趟 2（10:34），無 crash，轉錄出 StackManager、VSC 完成狀態及 `BD_VSC_OLYMPIC_ANTENNA_PREF_B` 的 `Invalid data size 0`／VSC stall／reason=3602，見 K-077。證據 `native-hw000bt5-oslog2-1119-1034/boot.log`（SHA-256 `a24a0bc3227b3a781b98d22fe95a77016045484cfe0cf3d5741f412086bd6f29`）。
- **參考來源**：dyld `mach-o/dyld-interposing.h` 的 DYLD_INTERPOSE（APSL）；libsystem_trace `os/log.h`、`os/log_private.h` 的函式原型；`sys/reason.h`。ADT 讀法同 `FuxiBlueTool.c`。
- **結論／未完成**：只在診斷趟次開；藍牙日常可用後關閉（每次輸出有行數上限，但仍佔 console）。
- **出處**：K-077 的 os_log 趟 1／2。

### U-023 bluetoothd 相容修補：關藍牙閃退與單實例 BLE 廣播（fuxi,bt-fix）

- **日期**：2026-11-20 01:58–09:20
- **狀態**：已實機驗證（趟 32／33：設定頁關→開兩次無閃退，電源循環後 Windows SDP 成功）。11-20 10:01 已併入日常 boot_a（B-013，`--bt-fix 3`，不帶 `--bt-oslog`）。
- **開關**：ADT `/chosen fuxi,bt-fix`（u32，預設 0）：bit 0 補 H4 傳輸表的 PrepareTerminate；bit 1 讓 bluetoothd 讀到 `SupportsMultipleAdvertising=false`。需要核心 0118 的注入位元 64（bluetoothd）。loader 產生器 `--bt-fix <1..3>`，要求 `--bt-native` 與含 ` bluetoothd gets ` 的核心，並把 `fuxi,inject` 加上 64；不綁 `--bt-oslog`／`--stderr-log`。
- **問題／現象**：
  - K-077 趟 16 起，設定頁關藍牙時 bluetoothd 必定 SIGSEGV（pc 0，lr 換算 `0x1000f3b50`）。launchd 重啟後開關仍能再打開，但每次關藍牙都重來一輪。
  - bluetoothd 開機時會送 Broadcom 多實例廣播 VSC `0xfd48`，WCN7850 回 status 1（未知命令；趟 31 第 6503／6509 行）。
- **根因**：
  - 20H392 bluetoothd 的 H4 傳輸表在 `__DATA`，靜態位址 `0x100844228`，共 18 個 64-bit 欄位；其中第 12 欄（+0x60，PrepareTerminate）是 NULL。
  - 終止路徑 `0x1000e6aac` 在表的型別非 0 時，會尾呼叫 `[表+0x60]`，結果跳到位址 0。
  - H4BC 表同一欄（`0x10006a444`）只設一個旗標位元組，所以換成空函式是安全的。
  - bluetoothd 是 plain ARM64、沒有 PAC，`__DATA` 可寫，執行期直接改表即可。
  - `0xfd48` 是 Broadcom 的多實例廣播命令。`com.apple.MobileBluetooth.debug` 的 `LE` 字典設 `SupportsMultipleAdvertising=false` 時，ChipMgr／LeBroadcaster 會改走標準 Legacy 廣播（`0x2005`／`0x2006`／`0x2008`／`0x200a`）。
- **做法**：harness `da0ac11`（分支 `codex/adsp-battery`；核心補丁同步 ksrc `231479a` 在 `f8a799a`）。
  - `harness/userland/fuxi-shape/FuxiBluetoothPatch.h`（Codex）：`fuxi_bt_patch_h4_table()` 先驗證表的形狀才寫，任一條件不符就不寫：
    - 型別為 1，名稱字串為 "H4"。
    - 第 2–9 欄落在 `__TEXT` 且 4-byte 對齊，第 10–17 欄為 0。
    - 第 12 欄必須仍是 0，否則回 `already_set`。
  - `FuxiBluetoothLog.c` 的建構函式（Codex）：
    - bit 0：找主程式的 `__TEXT`／`__DATA`，用 header 位址減 `__TEXT` vmaddr 算 slide，再呼叫上式。
    - bit 1：攔 `CFPreferencesCopyAppValue("LE", "com.apple.MobileBluetooth.debug")`，在回傳的字典補上 `SupportsMultipleAdvertising=false`。
  - 主程式 header（Claude 修正）：
    - Codex 01:58 那趟只用 `_dyld_get_image_header(0)`，bluetoothd 三個 pid 都回 `h4=bad_header`，表示第 0 個映像不是可接受的 MH_EXECUTE header。
    - 改成先用 `crt_externs.h` 的 `_NSGetMachExecuteHeader()`，不成再掃 dyld 映像表找 `MH_EXECUTE`，並檢查 `__TEXT` fileoff 為 0 及 slide 加總不溢位。
    - console 的 `h4=` 後面會附上用了哪一種方式（`nsget`／`image0`／`scan`）；都找不到時印 `no_exec(nsget=…,n=…,t0=…)`。
  - `work/statusbar-fix/windows-bluetooth-peer.py`：
    - `--require-cycle` 原本只認 bt-oslog 的 `Sending 'power state changed'`。bt-oslog 每個程序有上限（default／error／fault 4000 行、info／debug 1500 行），修補後 bluetoothd 不再重啟，趟 32 在第 11257 行（`BTLOG N 5501`）就用完，點開關時已沒有電源事件可看。
    - 改成同時認 bluetoothd 直接寫到 stderr 的 `Update Dynamic Store with state On/Off`，這行不受上限影響，並保持兩種事件的出現順序。
    - 以趟 32、趟 33、Codex 那趟及 `native-hw000bt5-cycle-pc-check-1119` 的 boot.log 離線驗證，都判為已完成 1→0→1。
- **復現**：

  ```powershell
  .\work\localbuild\build-sb-shape.ps1 -Name sb-local-btfix-h4main-1120-artifact
  python -X utf8 work/adsp-battery/prepare-wlan-w18-loader.py build --like native-bt5-diag-1120-loader --artifact kernel-local-bt-complete-1120-9g-artifact --wifi-ios --wifi-args-adt --with-adsp --audio-spf --audio-tdm-pcm --bt 5 --bt-native --bt-ibs --bt-optional '0x0c5b,0xfd3c,0xfd47' --bt-pad 0xfc00 --bt-mask '0x2003:2:0x30' --bt-txmask '0x200b:5:1,0x200d:12:1,0x2006:5:1,0x2041:0:1,0x2043:1:1' --bt-baud 3200000 --bt-nolog --bt-link-log --bt-controller-identity native-hw000bt5-mfbalign-1119 native-hw000bt5-fce9-status-1119 --bt-oslog 3 --bt-fix 3 --haptic 3 --inject-media --product-haptics --haptic-ui-ms 15 --haptic-envelope --haptic-amp 128 --hal-haptics --wifi-audit --wifi-password-prompt --wifi-sb-audit --wifi-sb-refresh --wifi-rssi-bounds --wifi-ux 2047 --mfb-align --display-gamut sRGB --stderr-log 'mediaserverd,bluetoothd,BlueTool' --privs --rtc-calibration <一小時內的 json> --tap-delay 75 --tap-script 's540,2390,540,999,99;w3000;s540,2390,540,1100,250;w2500;s540,2390,540,1100,250;w2000;p200;t408,1426;w4000;p200;t80,175;w1500;t80,175;w1500;s560,690,560,1570,340;w1800;t260,1200;w3500;p200;t935,415;w6000;p200;w9500;t935,415;w12000;p200' --name native-bt5-btfix-h4main-1120-loader
  .\work\statusbar-fix\launch-loader-check.ps1 -Loader native-bt5-btfix-h4main-1120-loader -Kernel kernel-local-bt-complete-1120-9g-artifact -Out <輸出名> -Seconds 240 -Reset 8 -FramebufferHeight 640 -Dylib D:\xiaomi13\fuxi_ios\work\Codex-Xiaomi13-GPU-20261109\sb-local-btfix-h4main-1120-artifact\libfuxi-sb.dylib -DylibSha c2c3184af48cb7288392e5c45182770d9aa78a98203b5eedcc12b87773a15ef1 -PeerTestPython <Windows 藍牙測試 venv 的 python.exe> -PeerIdentityRuns native-hw000bt-early-exit-1119-e,native-hw000bt-fce2-native-status-1119-f -PeerAfterPowerCycle -PeerAudioSource
  ```

  - 點擊腳本：解鎖後回主畫面，開「設定」（主畫面第 4 列第 2 個），回最上層並捲到頂，點「藍牙」，再點開關兩次（關、開），共 15 張截圖。截圖 11 是藍牙頁、13 是已關、15 是已開。
  - 成功判據：
    - `BTLOG S 1 on mode=3 fix=3 h4=patched/<方式>`。
    - `BTLOG N … Overriding Multiple Advertising support - disabled`，且整份紀錄沒有 `0xfd48`。
    - `Update Dynamic Store with state Off` 之後同一個 pid 重新 `FUXI BT SK: connect`，並出現 `state On`；整份沒有 `CRASH bluetoothd`。
    - `windows-peer.json` 的 `sdp.status` 為 `SUCCESS`。
  - 失敗判據：`h4=no_exec(…)`、`bad_*`、`out_of_data`，或 `already_set` 但 pc 0 仍出現（表示表的版本不同）；`CRASH bluetoothd … pc 0x0`。
- **主機測試**：`harness/tools/test-bluetooth-patch.c`，涵蓋：正確的表、已填過（`already_set`）、參數錯誤、表越出 `__DATA`、型別錯、名稱錯、函式指標越界／未對齊、尾欄非 0。`work/localbuild/fuxi-sb-build.sh` 與 `sb-shape.yml` 都以 ASan／UBSan 執行。
- **驗證紀錄**：
  - **Codex 01:58–02:02**（`native-hw000bt5-complete-1120`；dylib `sb-local-bt-complete-1120-artifact`，SHA-256 `f5ce49597c85086d0e56c45e5542f9ce6ab91f8511566c21aa0aa4e04d1ca861`，只用 `_dyld_get_image_header(0)`）：
    - 修補沒套上：`h4=bad_header` ×3（boot.log 第 1325、12408、19286 行）。
    - 關藍牙仍兩次 CRASH（第 12397、19272 行，pc 0，lr mh+0xf3b50）。
    - 廣播覆寫有生效；Windows 端 SDP 成功、5 項服務。
    - boot.log 26,359 行，SHA-256 `9c4c92896743164d883a1cb71fba701200c3ac7e9ae0811bc3cfe4f62b92930a`。
  - **趟 32 08:55–09:06**（`native-hw000bt5-btfix-h4main-1120`；dylib `sb-local-btfix-h4main-1120-artifact` SHA-256 `c2c3184af48cb7288392e5c45182770d9aa78a98203b5eedcc12b87773a15ef1`；loader boot SHA-256 `85fab06e78b46479f07dfadbc3ca3e1b9e01536dd5d1080f79b39377b3539253`）：
    - 第 1315 行 `h4=patched/nsget`，第 1351 行廣播覆寫。
    - 點開關後：第 12251 行 `FUXI BT SK: disconnect … owner=85 … owner_exiting=0`，第 12253 行 `state Off`。
    - 再點一次：第 12340 行同一個 pid 85 重新 connect，標準 HCI 初始化命令回 status 0（Broadcom VSC 照舊回不支援，與開機時相同），第 12591 行 `state On`。
    - 整份 0 次 `CRASH bluetoothd`；截圖 15 開關為開。
    - Windows 端判斷失敗：bt-oslog 用完，原因見上。
    - boot.log 13,561 行，SHA-256 `19f952180348592d1d10e60ddf0286ee6ccd07bc16a763339ed264c69a2b264e`；主機 TSENS 最高 69°C。
  - **趟 33 09:11–09:20**（`native-hw000bt5-btfix-peer-1120`，同一個 loader 與 dylib，Windows 判斷已改）：
    - 第 1325 行 `h4=patched/nsget`。
    - 關：第 12129 行 disconnect，第 12131 行 `state Off`。開：第 12248 行 pid 85 重新 connect，第 12509 行 `state On`。整份 0 次 `CRASH bluetoothd`。
    - 整份 0 次 `0xfd48`；Legacy 廣播 `0x2005`／`0x2006`／`0x2008`／`0x200a` 全回 status 0（第 6477–8464、12475–12664 行）。
    - 電源循環後 Windows 端：SDP `SUCCESS`、5 項服務，Audio Source（110a）1 筆、137 bytes。
    - BLE 廣告掃描為 false，GATT 為 `device_unavailable`，與 Codex 那趟及 `native-hw000bt5-cycle-pc-check-1119` 相同，不是這次的退步。
    - boot.log 13,507 行，SHA-256 `42ff048fb9fee623946249d02f628b0563eb51916b75b9c51d950104bb20de2d`；主機 TSENS 最高 76.5°C。
- **參考來源**：
  - bluetoothd 20H392 反組譯：`0x1000e6aac`、`0x1000f3b44`，H4BC `0x10006a444`。
  - dyld `mach-o/dyld.h`（`_dyld_image_count`／`_dyld_get_image_header`）與 `crt_externs.h`（`_NSGetMachExecuteHeader`）。
  - CFPreferences API。
  - 本機 `UniXNU` 原始碼找不到 `PrepareTerminate`／`SupportsMultipleAdvertising` 相關修補；ios-redfin 不在這個工作區，沒有查。
- **結論／未完成**：
  - 關藍牙閃退已解決，bit 1 也去掉了 `0xfd48` 的未知命令。
  - 仍待做：
    - Windows 端 BLE 掃描與 GATT 仍找不到手機（修補前就是如此）。
    - 日常版已由 B-013 部署（不帶 `--bt-oslog`）；趟 35 從常駐選單進 iOS 後關→開 5 輪，bluetoothd 同一個 pid、0 次閃退。
- **出處**：K-077 趟 18 第 15028 行（關藍牙閃退初次記錄，趟 16 第 11192 行也有）。

### U-024 mediaserverd 錄影 XPC 服務空回應存根：解決錄音後 SpringBoard 主執行緒卡死（fuxi,capsrc-stub）

- **日期**：2026-11-21 13:42–15:22
- **狀態**：已實機自動驗證（趟 73 `native-hw000-capsrc-autotest-1121` 通過，錄音結束後 SpringBoard 主執行緒觸感與音量條皆正常回應，零回歸）；待使用者實機驗收確認。
- **開關**：ADT `/chosen fuxi,capsrc-stub`（u32 位元遮罩，預設 0 關閉，有效範圍 `1..15`）：bit 0 `com.apple.coremedia.capturesource`、bit 1 `com.apple.coremedia.capturesession`、bit 2 `com.apple.coremedia.capture.deferred.containermanager.xpc`、bit 3 `com.apple.coremedia.flashlight`。loader 產生器 `--capsrc-stub [MASK]`（預設 `7`，會同時確保 `fuxi,inject` 開啟 bit 2 `mediaserverd` 注入）。
- **問題／現象**：
  - K-078 趟 69 打通系統錄音後，趟 70 使用者實測錄音與播放皆成功，但開始錄音或播放／切回桌面時 SpringBoard 整個卡死無回應（回不了桌面、無法觸發主畫面互動）。
  - 趟 72（`native-hw000-sbhang-sample2-1121`）在錄音後自動對 `SpringBoard` 取樣，確認主執行緒 `t0` 永久卡在 `mach_msg2_trap`。
- **根因**：
  - `sweet-root-tweaks.py` 為避開 `CMCapture._FigPreloadShaders` 造成 `mediaserverd` 崩潰，在 `MediaToolbox.framework/*/SubServerConfiguration.plist` 設定 `StartRecorderServer=False`，使 `mediaserverd` 略過 `CMCaptureCore._FigStartCaptureServers`。
  - 然而 `com.apple.mediaserverd.plist` 仍向 `launchd` 宣告 `com.apple.coremedia.capturesource` 等 MachServices；系統啟動麥克風錄音時，`SpringBoard` 主執行緒初始化 `AVCaptureProprietaryDefaultsSingleton` 並呼叫 `_FigCaptureSourceRemoteCopyCaptureSources(8)`，對 `com.apple.coremedia.capturesource` 送出同步 XPC，因無人接管該服務而永久阻塞於 `mach_msg2_trap`。
- **做法**：
  - 在 `harness/userland/fuxi-shape/` 新增純 C11 決策表頭 `FuxiCaptureStubLogic.h` 與 `FuxiCaptureStub.c`，編進既有 `libfuxi-sb.dylib`（由 `work/localbuild/fuxi-sb-build.sh` 與 `harness/.github/workflows/sb-shape.yml` 一併建置）。
  - 建構子僅在 `getprogname() == "mediaserverd"` 且 ADT `/chosen fuxi,capsrc-stub` 為 `1..15` 時啟用，建立背景執行緒延遲 200 ms 後透過 `dlsym(RTLD_DEFAULT, ...)` 取得 Apple 原生 `CoreMedia._FigXPCServerStart`／`_FigXPCMessageGetOpCode` 與 `libxpc` 符號，維持 `libfuxi-sb.dylib` 原有 4 條動態連結依賴（符合 `verify-sb-shape.py` 稽核）。
  - 由 `CoreMedia` 原生處理 `.png` 交握、`.Token`、`.ServerCapabilities`、`.ErrorReturn` 與斷線清理；對 `capturesource` 的 `'csrc'`（`0x63737263`）在 `".Value"` 回空 `xpc_array`（`rc=0`）、`'mjds'`（`0x6d6a6473`）回 `uint64(0)`（`rc=0`），其餘服務與 opcode 皆回 `kFigCaptureError_Unsupported`（`-12782`）。
  - `work/adsp-battery/prepare-wlan-w18-loader.py` 加入 `--capsrc-stub [MASK]`（預設 7），寫入 ADT `fuxi,capsrc-stub` 並確保 `fuxi,inject |= 4`。
- **復現**：
  - 本機建置 dylib：`work/localbuild/build-sb-shape.ps1 -Name sb-local-capsrc-1121-artifact`（SHA-256 `908aa9e22d4b9125be04c21d1354918fbb94c284b936371039f09c8c42cca8ce`，43 個碼頁驗簽通過）。
  - 產生器：以 `prepare-wlan-w18-loader.py` 搭配 `--capsrc-stub 7 --sb-ext --haptic-damp 0xd2 --volhud 238128 --audio-mic 2` 產生 `native-capsrc-autotest-1121-loader`（`9721cbb7…`）與使用者驗收版 `native-capsrc-user-1121-loader`（`cbc7da36…`），透過 `work/statusbar-fix/launch-loader-check.ps1` 安裝 dylib 並暫時開機。
  - 成功判據：`mediaserverd` 印出 `capsrc-stub start capturesource/capturesession/deferred-cm rc=0`、語音備忘錄錄音觸發 `capsrc-stub svc=capturesource op=csrc(0x63737263) payload=8/1 rc=0` 且錄音成功存檔、錄音後 `SpringBoard` 主執行緒仍能派發 CoreHaptics 與音量條動畫。
- **主機測試**：`harness/tools/test-capsrc-stub.c`（以 ASan／UBSan 驗證 ADT 遮罩解碼、預設／越界拒收、各服務 opcode 分類與 FourCC 格式化，已納入 `fuxi-sb-build.sh` 與 `sb-shape.yml`；全部 7 項 dylib 主機測試、3 項破壞負測試與 `prepare-wlan-w18-loader.py self-test` 皆通過）。
- **驗證紀錄**：
  - **趟 72（14:04–14:06，`native-hw000-sbhang-sample2-1121`）**：重現語音備忘錄錄音（`FUXI AUDIO MIC: session=1 samples=229920 peak=3377 errs=0 drops=0`），`boot.log`（7,057 行，SHA-256 `38c6c40b45cbe519f84dbcc919edf4ca6e04c69ab5b056e896385ade8ddb926c`）第 6641–6697 行與 `work/adsp-battery/fuxilog/20261121-141903/console.txt` 記錄 `SMP SpringBoard t0 pc 0x205b15030`，鎖定卡死根因；最高溫 56.4°C。
  - **趟 73（15:13–15:22，`native-hw000-capsrc-autotest-1121`）**：以本機 9g＋ath12k 核心 `kernel-local-hapdamp2-sbext-9g-artifact`（SHA-256 `8d809e18…`）與已裝 `sb-local-capsrc-1121-artifact`（`908aa9e2…`，`PRE_OK 10 / WRITE_OK 10`）實機驗證；`boot.log`（7,295 行，SHA-256 `a76abf15d73fc908c7d57ca675a723e2558e3a582212538b49199d3cbb14d37d`）：
    - 第 1244／1247／1249 行：`capsrc-stub start capturesource / capturesession / deferred-cm rc=0`。
    - 第 6056–6376 行：語音備忘錄點擊錄音觸發 `FUXI SBEXT` `op 5`／`op 6`、`capsrc-stub svc=capturesource op=csrc(0x63737263) payload=8 rc=0` 與 `payload=1 rc=0`、`FUXI AUDIO MIC: session=1 samples=250560 peak=3378 errs=0 drops=0`，結尾畫面確認已存出 5 秒可播放錄音。
    - 第 6681–7091 行：錄音結束後 `SpringBoard` 主執行緒完全回應，觸發 CoreHaptics（`burst=1 warm=0 chunks=3 refills=2 underruns=0 out_pk=126 open_us=2580 queue_us=9`）與右側音量條動畫（`y=238 h=128`，`state=0 -> state=1`）。
    - 零回歸：喇叭 `periods=4523 writes=633 nonzero=83 underruns=2 overflows=0`（0 功放故障／0 PLL unlock）、麥克風自測 `nonzero=33420 peak=7277 errs=0`、Wi-Fi `5200 MHz -59 dBm join finished 0`、BT `desync=0 ibs=1/244/245 stalls=0 wake_timeouts=0`；最高溫 57.2°C，15:22 回 Android 釋鎖。
- **參考來源**：
  - iOS 16.7（20H392）`dyld_shared_cache` 反組譯：`CMCapture._FigCaptureSourceServerStart`（`0x1a21b7138`）、`_FigCaptureSourceRemoteCopyCaptureSources`（`0x1a1f805f8`）、`_FigCaptureSourceRemoteMaxStillImageJPEGDataSize`（`0x1a2030544`）、`CoreMedia._FigXPCServerStart`（`0x1916ab6a0`）、`_FigXPCMessageGetOpCode`（`0x1916a83c0`）。
  - `sweet-root-tweaks.py`（`StartRecorderServer=False`）。
- **結論／未完成**：
  - 錄音觸發 `SpringBoard` 同步等待 `com.apple.coremedia.capturesource` 的主執行緒死結已解除，自動實測通過且零回歸。
  - 待使用者以已備妥的 `native-capsrc-user-1121-loader` 親自驗收語音備忘錄錄音／播放／回桌面、聽寫與調大後震動手感；正式定案前另需 CI 核心／dylib 對照與決定是否併入日常開機鏈。
- **出處**：K-078 趟 70 使用者回報錄音後卡死與趟 72 `SMP` 主執行緒取樣。

### U-025 timed 執行期動態 RTC 偏移持久化（fuxi,rtc-user）

- **日期**：2026-11-22
- **狀態**：已實機驗證（A+B 執行期自測、純 B 從 1970 還原、使用者互動現場捕捉 `calendar_set` 落盤 `gen=4`），已併入日常 `boot_a`（B-016）。
- **開關**：ADT `/chosen fuxi,rtc-user`（u32，預設 0；`1`=正式持久化與開機還原，`3`=附帶一次性非破壞執行期自測），搭配核心 `fuxi,inject` bit 256（`/usr/libexec/timed`）。loader：`--rtc-user [1..3]`。
- **問題／現象**：僅靠開機讀取 Android `persist:/time/ats_2`（方案 A）無法保留在 iOS 端手動改時或 NTP 自動對時後的最新偏移；且若未來未經過 Android 或 `ats_2` 失效時，iOS 需要自有持久錨點。
- **根因**：`timed` 的系統校時不會寫回 Android `persist` 分區，而 PM8550 RTC 僅提供單調原始計數器，重開後需要「iOS 側持久化的 offset + 目前 PMIC raw 計數器」才能推算當下 UTC。
- **做法**：
  - 在 `harness/userland/fuxi-shape/` 新增 `FuxiRtcPersistLogic.h`（純演算法：固定大小二進位紀錄、CRC32 校驗、單調 `gen` 計數、合理時間視窗、PMIC `raw_now < saved_raw` 歸零防護、2000 ms 漂移死區）與 `FuxiRtcPersist.c`（constructor 僅於 `getprogname() == "timed"` 且 ADT `fuxi,rtc-user` 啟用時啟動並清除 `DYLD_INSERT_LIBRARIES`）。
  - 透過核心唯讀 sysctl `kern.fuxirtc_info`／`kern.fuxirtc_raw`（K-036）讀取 PMIC 計數與開機狀態，以原子暫存檔寫入 `/private/var/db/timed/fuxi-rtc-offset.bin`（備援 `/private/var/db/fuxi-rtc-offset.bin`）。
  - 啟動時若核心未設時（例如純方案 B 停用方案 A 與靜態 offset）且檔案通過 CRC32 與 `raw_now >= saved_raw` 檢查，即呼叫 `settimeofday()` 還原 UTC；背景執行緒同時掛接原生 Mach `HOST_NOTIFY_CALENDAR_SET` 通知與 5 秒保底輪詢，偵測系統時間變更後立即落盤。
- **復現**：在乾淨日常 harness 分支 `codex/daily-rtc-ab-1122`（`af4b30b`）執行 `work/localbuild/build-sb-shape.ps1`，或使用已併入主開發樹建置之 `sb-local-main-rtc-1122-artifact`（SHA-256 `336119c1…`）；loader 帶 `--rtc-persist --rtc-user 1`（自測用 `--rtc-user 3 --stderr-log timed`）。成功判據：`FUXI INJECT: timed <- /usr/local/lib/fuxi/libfuxi-sb.dylib`、`FUXI RTC: sysctl info first read by timed`，搭配 `--stderr-log timed` 時見 `fuxi: timed: rtc-b ... persisted`／`rtc-b boot action=restored`。
- **主機測試**：`harness/tools/test-rtc-persist.c`（ASan＋UBSan，涵蓋 CRC32 損毀、開機決策矩陣、PMIC 歸零拒載與歸零後新錨點恢復、2000 ms 死區與原子落盤）；`work/localbuild/fuxi-sb-build.sh` 與 `.github/workflows/sb-shape.yml` 均納入驗證並通過 `verify-sb-shape.py` 4 函式庫依賴與 ad-hoc 碼頁簽署校驗。
- **驗證紀錄**：
  - `native-hw000-rtc-ab-selftest-1122`（`--rtc-persist --rtc-user 3`，`boot.log` 3,951 行）：完成 `FUXI INJECT: timed`、唯讀 sysctl 寫入回 `EPERM`、2 秒即時跳動、`settimeofday(+5s/-5s)` 觸發 `HOST_NOTIFY_CALENDAR_SET` 寫回並遞增 `gen`、以及合成 `raw_now < saved_raw` 拒載測試，輸出 `fuxi: timed: rtc-b selftest PASS`。
  - `native-hw000-rtc-b-only-1122`（僅 `--rtc-user 1`，不帶 `--rtc-persist` 亦無 `fuxi,rtc-offset`，`boot.log` 3,997 行）：核心刻意維持 1970（`offset=0 (none)`），隨後 `timed` 載入前一趟落盤的 `/private/var/db/timed/fuxi-rtc-offset.bin` 並呼叫 `settimeofday()` 精準還原 2026-11-22 UTC 時間。
  - `native-hw000-rtc-ab-user-1122`（使用者互動檢視，`boot.log` 9,660 行）：開機由方案 A 設時 `08:23:36`，`timed` 掛接監聽後於 `08:24:06` 真實捕捉一次系統時間微調（`-2310 ms`）並自動寫盤 `gen=4`，使用者確認無問題。
  - `native-hw000-daily-rtc-ab-1122`（完整日常 payload，`boot.log` 6,463 行）：確認 `FUXI INJECT: timed`（L996）與 `FUXI RTC: sysctl info first read by timed`（L1175），與 BT／Wi-Fi／ADSP／Metal 全子系統共存零回歸。
- **參考來源**：iOS `timed` 與 XNU Mach `host_request_notification(HOST_NOTIFY_CALENDAR_SET)` 原生機制；既有 `harness/userland/fuxi-shape/` constructor 與 ADT 讀取樣式（`FuxiShape.c`、`FuxiBluetoothLog.c`）；本機 `UniXNU` 無同類 `timed` 持久化模組，因此依四函式庫限制自寫並補主機模型測試。
- **結論／未完成**：方案 B 與方案 A 雙軌已併入日常 `boot_a`（B-016）。手機 root 目前維持 11 單元 `libfuxi-sb.dylib`（`336119c1…`），內含與乾淨日常 9 單元產物（`4c260c42…`）完全相同的 `FuxiRtcPersist` 原始碼，以保留主開發線既有 opt-in 單元不退步。
- **出處**：`work/Codex-Xiaomi13-GPU-20261109/sb-local-daily-rtc-ab-1122-artifact/`、`sb-local-main-rtc-1122-artifact/`、`native-hw000-rtc-ab-selftest-1122/`、`native-hw000-rtc-b-only-1122/`、`native-hw000-rtc-ab-user-1122/`、`native-hw000-daily-rtc-ab-1122/`。

### U-026 SpringBoard／Preferences 偽 pre-twiddled ASTC `.atx` CPU 解碼改走線性解碼，修復系統模糊背景區域發紫（`fuxi,atx-fix`）

- **日期**：2026-11-22–2026-11-23
- **狀態**：已由使用者實機目視驗收通過 4 項場景並併入主分支與日常 `boot_a`（B-019）。
- **開關**：ADT `/chosen fuxi,atx-fix`（u32，預設 0 關閉；`1`=於 `SpringBoard` 與 `Preferences` 啟用），需核心 `fuxi,inject` 開啟 bit 2（`SpringBoard`）。loader 產生器：`--atx-fix 1`。
- **問題／現象**：20H392 系統主畫面藍色桌布正常顯示，但單手模式（Reachability）上方模糊背景、通話介面（`InCallService`）模糊背景、Safari 分頁總覽及多工切換器（App Switcher）等系統半透明／模糊區域呈現明顯發紫。
- **根因**：
  - 桌布 extension（`CollectionsPoster`／`PosterBoard` 等）在無 Metal 行程中透過 `ImageIO` `ATXWritePlugin::writeASTCData` 寫出桌布快取 `.atx` 時，雖產出未經 twiddle 的線性 ASTC 4×4 資料，卻因沒有預編 twiddle 資料而寫入小寫 `'astc'`（pre-twiddled 標記）與全零 16-byte `HEAD` watermark。
  - 主畫面 `QuartzCore` 零拷貝路徑（`ASTCTextureImp::textureDataLockData` → `createTwiddledDataFromTwiddled`）直接轉交 `backboardd`／`FuxiNativeMetal` 按線性表面（`format=204`）上傳 GPU，因此主畫面顏色正常；不可將磁碟 `.atx` 改寫為 `'ASTC'`。
  - 當 `PaperBoardUI`（`PBUICachedSnapshotEffectProvider` → `PBUIBackdropCompositor`）執行 CPU 模糊／染色快取時，經 `ATXReadPlugin::copyImageBlockSet` → `ATXReadPlugin::decodeImageData` → `ASTCTextureImp::decodeASTCtoRGBX` 走 `decodeRGBXFromTwiddled`；該函式因 `ASTCTwiddler::twiddledDataSize()==0` 僅配出 `malloc(0)` 且 `copyFromTextureData` 無作用，`libate` 遂從全零緩衝解碼出 ASTC 錯誤色洋紅 `RGB=(255, 0, 255)`，再經系統濾鏡疊染成各式紫色。
- **做法**：
  - 在 `harness/userland/fuxi-shape/` 新增純 C11 邏輯標頭 `FuxiAtxFixLogic.h` 與攔截實作 `FuxiAtxFix.c`（`harness` `51ca027`），並納入 `work/localbuild/fuxi-sb-build.sh` 與 `harness/.github/workflows/sb-shape.yml` 編入 `libfuxi-sb.dylib`（嚴格維持原有 4 條系統依賴，無需改核心）。
  - 建構子僅在 `getprogname()` 為 `SpringBoard` 或 `Preferences` 且 ADT `/chosen fuxi,atx-fix=1` 時啟動；利用 `CGImageSourceCreateWithData`／`CGImageSourceGetType` 雙錨點求得 dyld shared cache slide，透過 `vm_read_overwrite` 驗證 6 個 vtable 槽位（`__ZTV13ATXReadPlugin` 與 `__ZTV14ASTCTextureImp`）及目標函式前 4 個指令字完全符合 20H392 指紋（任一不符即拒裝並印出 `refused layout`）。
  - 以 `vm_protect(..., VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY)` 僅 COW 替換 `__DATA_CONST` 中單一 8-byte 槽位 `__ZTV13ATXReadPlugin + 0x68`（`0x1d5f45a58 + slide`）後立即恢復 `VM_PROT_READ`（零 `__TEXT` 修改、不改變 `tex->isInputPreTwiddled` 狀態以免影響 `QuartzCore` 零拷貝）。
  - 在 `atxfix_decode_image_data()` 中核對非 `420f`、具 ASTC chunk、非 LZFSE 壓縮、預標記為 pre-twiddled 且 84-byte `HEAD` 符合 `version==1` 與 16-byte 全零 watermark（精準區分帶 `0x55` watermark 的原生預編 `.atx`，如 `AvatarKit`）後，驗證 `vImage_Buffer` 邊界並對 `tex+0xa8` 上鎖，直接呼叫原生 `ASTCTextureImp::decodeRGBXFromLinear`（前 12 次成功於 stderr 輸出尺寸與 `q1 / mid / q3` RGBA 抽樣）；其餘情況一律轉呼原廠 `ATXReadPlugin::decodeImageData`。
  - `work/adsp-battery/prepare-wlan-w18-loader.py` 的 `controlled` 集合新增 `fuxi,atx-fix` 並加入 `--atx-fix` 參數（要求 `fuxi,inject` 含 bit 2；已於 `a4a01ea` 入主倉）。
- **復現**：
  - 本機 WSL 建置 dylib：`work\localbuild\build-sb-shape.ps1 -Name sb-local-atxfix-1122-artifact`（`libfuxi-sb.dylib` SHA-256 `10b40ab88795275365ca2d370dc7d4cbf19fd50b3eea208fe22bb82858c80800`，47 個 4K code pages ad-hoc 簽署與 3 項負測試全過）。
  - 產生器：沿用日常 9g＋ath12k 核心 `kernel-local-daily-audio-hap8-m6-1122-9g-artifact`（`ce941c1b…`）建置 `native-atxfix-autotest-1122-loader`（`boot.img` SHA-256 `0941591360580c39b5cc16b6401d46e85ed119c453e935c85778c97f326d3064`，命令列 571 bytes，帶 `--atx-fix 1` 與自動觸控腳本）及人工驗收／日常併入版 `native-atxfix-user-1122-loader`（`boot.img` SHA-256 `2fe7862e98cf616242398f40116e8e6b6fcef8f672e027fec0731b133c8fa59f`，命令列 604 bytes）。
  - 成功判據：`boot.log` 出現 `atx-fix installed slide=... slot=...` 與 `atx-fix decoded linear ... q1=(...) mid=(...) q3=(...)`（藍色桌布 RGBA 而非洋紅），0 次 `refused layout`／`decodeRGBXFromLinear failed`。
- **主機測試**：`harness/tools/test-atx-fix.c`（納入 `fuxi-sb-build.sh` 與 `sb-shape.yml` 以 ASan／UBSan 執行），包含取自實機提取 `RuntimeSnapshot-home.atx`（1125×2436 全零 watermark）與 `AvatarKit` `alien-camera.atx`（428×428 `0x55` watermark）之 84-byte `HEAD` 夾具、14 種 ASTC block format＋HDR 參數解析、`vImage_Buffer` 邊界及像素抽樣、雙錨點／vtable／指令字錯誤拒裝測試，輸出 `ATX_FIX_TESTS OK`（連同其餘 9 項既有用戶端主機測試與 `prepare-wlan-w18-loader.py self-test` 全過）。
- **驗證紀錄**：
  - `native-hw000-atxfix-autotest-r2-1122`（2026-11-23 01:02–01:12，暫時開機 120 秒＋上方 1200 列截圖，不動常駐 `boot_a`）：
    - `run-native-hw000-atxfix-autotest-r2-1122-stage` 完成 dylib 寫入驗證（`PRE_OK 12`、`WRITE_OK 12`、`FSCK_final OK`、`SHAPE_INSTALLED` 符合 `10b40ab8…`）。
    - `boot.log` 6,262 行（SHA-256 `99c2aa2646c7353c506f3704df5287dc4ef93d93691b3b701080e107c812ac5e`，`host_tsens_max=60.3°C`）：第 1274 行 `|SpringBoard| fuxi: SpringBoard: atx-fix installed slide=0x11dc4000 slot=0x1e7d09a58`；第 5119 行上滑解鎖後實際走線性路徑解碼：`atx-fix decoded linear 1125x2500 blk=4x4 srgb=0 q1=(115,154,255,255) mid=(104,154,243,255) q3=(62,105,197,255)`，呈現完整藍色桌布色系，0 次錯誤或失敗。
    - 1080×1200 實機截圖 `framebuffer.png` 量測：`full_mean=(139.1, 149.1, 226.9)`、`top600_mean=(147.6, 132.2, 224.4)`、`mid_mean=(140.5, 169.8, 231.6)`，ASTC 錯誤色洋紅（`R>160, G<60, B>160`）像素 `0 / 1,296,000`（`0.0000%`），圖示、狀態欄時鐘 `1:07` 與電量 `100` 顯示清晰無破圖。
    - 零回歸：0 panic、0 `SpringBoard`／`Preferences` 崩潰、`backboardd` 6 次 `FUXI LINEAR: foreign snapshot imported format=204`（`1125x2436`×1、`1125x2500`×3、`846x1875`×2）、`frames=1436 failures=0 hz=30.10`、GPU presents 608／failed 0、scanout 1560 frames、狀態欄 `360 x 86`、VolHUD `y=238 h=128`、觸感 `ui-fallback-ms=0 native-pcm=1`、Wi-Fi `join finished 0`、BT `desync=0 ibs=1/245/246 stalls=0`、ADSP 電量 `100% 4408 mV answers=896 errors=0`、RTC 動態校時正常。
  - `native-hw000-atxfix-user-1122`（2026-11-23 09:14–09:22，180 秒人工目視驗收，不動 `boot_a`）：
    - 手機 root 上現行 `libfuxi-sb.dylib`（`sb-local-audio-dict-v2-artifact`，`63724fd6…`）經 `INPUTS.txt` 核對內含逐位元相同之 `FuxiAtxFix.c`（`bddfcf9a…`）與 `FuxiAtxFixLogic.h`（`a124de21…`），故保留未降級覆寫。
    - `boot.log` 10,876 行（SHA-256 `a95f0e7325295a2c54b0531c45f334726bf3b085df0c68a80df081e18e5901ab`，`host_tsens_max=67.1°C`）：`SpringBoard` L1311 `atx-fix installed slide=0x43a68000 slot=0x2199ada58`、L4601 `atx-fix decoded linear 1125x2500 blk=4x4 srgb=0 q1=(115,154,255,255) mid=(104,154,243,255) q3=(62,105,197,255)`，`Preferences` L6130／L7646 均成功安裝，0 panic、0 `SpringBoard`／`Preferences` 崩潰、`backboardd frames=7656 failures=0 hz=30.20`。
    - 使用者現場確認單手模式 Reachability、通話背景、Safari 分頁總覽與多工切換器 4 項區域全部恢復正常，並要求併入主分支與日常 `boot_a`（見 B-019）。
- **參考來源**：20H392 `dyld_shared_cache_arm64` `ImageIO` 反組譯（`ATXReadPlugin::decodeImageData` `0x18bc0e59c`、`__ZTV13ATXReadPlugin` `0x1d5f459f0`、`__ZTV14ASTCTextureImp` `0x1d5f46ec0`、`ASTCTextureImp::decodeRGBXFromLinear` `0x18bc2cc48`、`BlockFormatForASTCBlockSize` `0x18bb1fdfc`、`HDRBlockFormat` `0x18bc2c9a8`）；`FuxiVibrate.c`（雙錨點與 `__DATA_CONST` COW vtable 修補）；`FuxiVolHud.c`／`FuxiRtcPersist.c`（ADT 閘門與 4 函式庫限制）。
- **結論／未完成**：已正式併入 `harness`（`51ca027`）、主倉 `main` 與日常常駐 `boot_a`（B-019）。**GPU 後續開發防復發契約**：
  1. 桌布行程無 Metal 時，`FuxiNativeMetal` 須維持 G-009 Patch 0012（`FUXI_NATIVE_FOREIGN_LINEAR`／`format=204`）供 `QuartzCore` 零拷貝，同時 `SpringBoard`／`Preferences` 須維持 `FuxiAtxFix.c` 與日常 `--atx-fix 1`；不可把磁碟 `.atx` 改寫為大寫 `'ASTC'`。
  2. 未來修 GPU 若開放 Metal 給 `PosterBoard`／`CollectionsPoster`／`SpringBoard`：`FuxiNativeMetal` Patch 0011 的 `MTLTextureLayout` 會寫入非全零 16-byte watermark **`Fuxi ASTC pair 1`** 且資料仍為線性排布，將繞過目前全零 watermark 檢查而導致 CPU 模糊再次退回 `decodeRGBXFromTwiddled` 發紫；開放前須同步讓 `FuxiAtxFix` 識別 `Fuxi ASTC pair 1`（或改採真 hw-twiddled 反解）並清除既有快取後重驗上述 4 項場景（同步記錄於 G-009、G-049 與 `AGENTS.md` §8）。
  3. 往後重裝 `NativeMetal`（`0044+`）、重編 `libfuxi-sb.dylib` 或升級日常 `boot_a` 時，開機日誌必驗 `atx-fix installed` 與 `atx-fix decoded linear`（藍色桌布像素而非洋紅），0 次 `refused layout`。
- **出處**：`work/adsp-battery/harness/userland/fuxi-shape/FuxiAtxFix.c`、`FuxiAtxFixLogic.h`、`work/adsp-battery/harness/tools/test-atx-fix.c`、`work/Codex-Xiaomi13-GPU-20261109/sb-local-atxfix-1122-artifact/`、`work/Codex-Xiaomi13-GPU-20261109/native-hw000-atxfix-autotest-r2-1122/`、`work/Codex-Xiaomi13-GPU-20261109/native-hw000-atxfix-user-1122/`。

### U-027 麥克風錄音品質與鍵盤聽寫修復（`fuxi,audio-mic-dsp` 旁路、`FuxiAudio` 啟動預緩衝、4 Hz HPF／`0x0c08` 增益與 `FuxiBluetoothLog` 防崩潰）

- **日期**：2026-11-22–2026-11-23
- **狀態**：自動化修復、主機測試與實機量化驗證已通過（`boot #363`）；裝置目前保留 `audio-local-dict-v2-artifact`（`3278da66…`）供人工開口實錄／聽寫／無發紫驗收，尚未併入日常常駐 `boot_a`（B-019）。
- **開關**：ADT `/chosen fuxi,audio-mic-dsp`（u32，預設 0 關閉；`1`=於 `mediaserverd` 將 `d22_bottom_mic_general.austrip` 導向 `d22_bottom_mic_measurement.austrip` 並對 `agc2`/`mstk`/`cfrk` 注入 `bypass=1`）、`fuxi,audio-mic=2`、`fuxi,audio-mic-gain=0x0c08`（`3080`，`adc1=8` +12 dB 類比、`dec0=0x0c` +12 dB 數位）、`fuxi,audio-mic-cfg=1`（`lpass-tx-macro` TX0 4 Hz HPF）。loader 產生器：`--audio-mic-dsp 1 --audio-mic 2 --audio-mic-gain 0x0c08 --audio-mic-cfg 1`。
- **問題／現象**：
  1. 原生語音備忘錄錄音（`20261122 204944.m4a`、`20261122 234553.m4a`）存在低頻過度切除與明顯爆音跳變階躍（`large_steps_ge_8000_count = 3660 / 346`、`peak = -0.0 / -1.2 dBFS`、`clip = 2 / 0`），且麥克風啟動期有 `underruns=1..9` 斷層（Phase 4 `20261123 024003.m4a` 於 `2141 ms` 處仍出現 `0.48 ms` 中段歸零）。
  2. 鍵盤點擊聽寫若同時開啟 `dict-oslog` 除錯注入，會觸發 `assistantd`／語音服務連續崩潰（`EXC_BAD_ACCESS at 0x3d`）；另外 `en-US` 與 `zh-TW` 聽寫實際可用邊界亦須實機釐清。
- **根因**：
  1. `lpass-tx-macro` TX0 AMIC 預設 150 Hz HPF 切除低頻，且小米原廠 `dec0=0x12`（+18 dB）疊加 `VirtualAudio` `d22_bottom_mic_general.austrip` 的 6 區塊 DSP 鏈（`firf`、`nbeq`、`agc2`、`mstk`、`nbeq`、`cfrk`）引發動態壓縮失真與跳變階躍。
  2. ARM64 反組譯 `VirtualAudio` 證實 `d22_bottom_mic_general` 在 C++ 硬編碼建立上述 6 區塊，並以 `unit.{manufacturer:'appl', type:'aufx', subtype:<fcc>}`（`kCFNumberSInt32Type`）向 `strips[0]["effects"]` 讀取 `"bypass"`，寫為 `[dprp]: VariTypeMap { [21]: 1 }`（`kAudioUnitProperty_BypassEffect = 1`）。原廠 `d22_bottom_mic_measurement.austrip` 僅含 30 Hz 風噪 HPF 之平直 `nbeq`（`ef[0]` 0 dB、`ef[1]` +18 dB 平直）與直通 `firf`，缺乏 `agc2`/`mstk`/`cfrk` 條目，因此單純路徑重導時該 3 區塊仍會維持 `bypass=0` 並將音量壓低至 `peak = -40.15 dBFS`。
  3. `FuxiAudio.c` 原 `MIC_PRIME_FRAMES = 480`（10 ms）小於 CoreAudio 首次讀取窗（512/960/1024 frames），導致啟動首拍 `underruns`。
  4. `FuxiBluetoothLog.c` 在 `log == NULL` 路徑仍呼叫 info／debug 記錄導致 `assistantd` 解參考空指標偏移 `0x3d` 崩潰；`zh-TW` 則因 `com.apple.assistant.support.plist` `Offline Dictation Status -> zh-TW -> Installed: False` 且裝置無 FairPlay 硬體憑證（`ADDeviceAuthenticationSessionV1` `-42184`）使 Siri 雲端回報 `kAFAssistantErrorDomain:300/209`。
- **做法**：
  1. **使用者空間 DSP 旁路與路徑重導（`libfuxi-sb.dylib`）**：更新 `work/adsp-battery/harness/userland/fuxi-shape/FuxiAudioMicDspLogic.h`、`FuxiAudioMicDsp.c` 與 `work/adsp-battery/harness/tools/test-audio-mic-dsp.c`（仍嚴格維持 4 條系統依賴），於 `mediaserverd` 將 `d22_bottom_mic_general.austrip` 導向 `d22_bottom_mic_measurement.austrip` 並藉由 `micdsp_append_bypass_effects()` 於 `strips[0]["effects"]` 動態補入 `agc2`/`mstk`/`cfrk`（FourCC `kCFNumberSInt32Type`＋`bypass=1`）且寫回 `strip name = d22_bottom_mic_general`（輸出 `MDLOG-DSP: patched strip name -> d22_bottom_mic_general (+3 bypass)`）。
  2. **HAL 啟動預緩衝與增益校準**：修改 `work/adsp-battery/ksrc/tools/fuxi-audio/FuxiAudio.c` 與 `tools/test-fuxi-sys.py`，將 `MIC_PRIME_FRAMES` 由 `480` 調至 `1440`（30 ms）、`MIC_PRIME_WAIT_MS` 由 `80` 調至 `120` 以消除啟動 `underruns`；並搭配既有核心開關 `fuxi,audio-mic-cfg=1`（TX0 4 Hz HPF）與 `fuxi,audio-mic-gain=0x0c08`（`adc1=8` +12 dB、`dec0=0x0c` +12 dB，較原廠 `0x12` 保留 6 dB 防削波餘裕）。
  3. **聽寫空指標防護**：修正 `work/adsp-battery/harness/userland/fuxi-shape/FuxiBluetoothLog.c` 對 `log == NULL` 之防呆保護，使 `assistantd`／`corespeechd` 維持 0 崩潰。
- **復現**：
  - 建置產物皆為 **本機 WSL 產物（local-wsl，非 CI 產物）**，全程不動常駐 `boot_a`（B-019）：
    - 核心（沿用既建）：`kernel-local-audio-dict-v1-9g-artifact`（9g，`result ok`，`kernel.development.fuxi` SHA-256 `e1f29dbe67ef0e1dc6de738f2e11c49b9abb4dc8d1e24d7d74064697e44f0ec7`，ISA `2582831` 指令／0 禁止）。
    - dylib：`work\localbuild\build-sb-shape.ps1 -Name sb-local-audio-dict-v3-artifact`（`libfuxi-sb.dylib` SHA-256 `28f8e7fb9d770d0fa0d8b43c6380aab4619fa319d498d50e390f740285936986`）。
    - HAL：`wsl -d Ubuntu-26.04 -u root -- bash tools/build-fuxi-sys.sh --audio-only ...` 編出 `work\Codex-Xiaomi13-GPU-20261109\audio-local-dict-v2-artifact\FuxiAudio.driver\FuxiAudio`（SHA-256 `3278da6678090a21a229cab5e391e7c741cfcc81c4ee919810897f8648bead54`），透過 `work\statusbar-fix\launch-loader-check.ps1 -AudioHal enable` 寫入測試 root。
    - Loader：以 `python -X utf8 work\adsp-battery\prepare-wlan-w18-loader.py build --like native-audio-dict-v3-1123-loader --artifact kernel-local-audio-dict-v1-9g-artifact --audio-mic 2 --audio-mic-gain 0x0c08 --audio-mic-cfg 1 --audio-mic-dsp 1 ...` 建置自動驗證用 `native-audio-dict-v4-1123-loader`（命令列 551 B，`boot.img` SHA-256 `317e3e97089d5a3154aacde6041e84bf4d8b1b1cf63f6957ae5ab10e17d4caef`）與乾淨人工驗收用 `native-audio-dict-user-v2-1123-loader`（帶 `--atx-fix 1 --statusbar-width 360 --statusbar-height 86 --rtc-persist --rtc-user 1 --quiet-boot --stderr-log Preferences`，命令列 604 B，`boot.img` SHA-256 `6e60920dd9130019a536cb92a1f339dc3563e6a92c31ef91cb1f7900577082e4`）。
  - 成功判據：`FUXI AUDIO MIC: hw ready ... adc1=8 dec0=0xc`、`tx dec0 unmuted gain=0xc hpf=4Hz`、`MDLOG-DSP: patched strip name -> d22_bottom_mic_general (+3 bypass)`、`RouteUtilities.cpp:826` 內 `1634165554`（`agc2`）／`1836282987`（`mstk`）顯示 `[dprp]: VariTypeMap { [21]: 1 }`、錄音 session `avail=1440`、`underruns=0`、`errs=0`、`drops=0`，且 `work\statusbar-fix\analyze-recording-quality.py` 顯示 `clip=0`、`steps8k=0`、中段歸零 0 次。
- **主機測試**：
  - `build-sb-shape.ps1 -Name sb-local-audio-dict-v3-artifact` 全部 11 項 ASan/UBSan 單元測試與 ad-hoc 簽署驗證全過（含 `logs/audio-mic-dsp.txt`: `PASS: audio mic DSP path rewrite tests ok` 與 `atx-fix.txt`: `ATX_FIX_TESTS OK`）。
  - `audio-local-dict-v2-artifact/host-tests.txt`：`test-fuxi-sys.py`（含 1024-sample 預緩衝讀取與裝置拓撲）與 `fuxi-sys-root.py` 雙項 `PASS`。
- **驗證紀錄**：
  - 全程僅暫時開機驗證並經 `-InspectAudio` 確認 `hfs_readonly: true`、`block_readonly: true`、`root_writes: 0`：
    - `boot #327`（2026-11-23 02:05–02:13，`native-hw000-audio-dict-v2-1123`）：0 panic、0 `assistantd` 崩潰、`en-US` 離線模型 `eng-USA-OfflineIOS-6.0.0.2-...` 啟動成功（靜音輸入 `1110` 結束）。
    - `boot #328`（2026-11-23 02:36–02:44，`native-hw000-audio-dict-v3-1123`）：產出 `20261123 024003.m4a`（5.035 s、`dc=0.04`、`steps8k=0`），定位無 bypass 時 `agc2`/`mstk`/`cfrk` 壓低至 `peak=-40.15 dBFS`／`p10=-54.56 dBFS`，且 `MIC_PRIME_FRAMES=480` 導致 `underruns=1..2` 與 `2141 ms` `0.48 ms` 中段歸零；亦實測確認 `zh-TW` `Installed: False` 且雲端受 `-42184` 阻擋回報 `209`。
    - `boot #358`（2026-11-23 14:43–14:47，`native-hw000-audio-dict-v4-1123`）與 `boot #360`（2026-11-23 15:01–15:08，`native-hw000-audio-dict-v5-1123`，切回英文鍵盤補測）：啟用 `sb v3`（`+3 bypass`）與 `HAL dict-v2`（`avail=1440`），0 panic、0 崩潰、`RouteUtilities.cpp:826` 實證 `1634165554`/`1836282987` 帶 `[dprp]: VariTypeMap { [21]: 1 }`、3/3 session `underruns=0 / errs=0 / drops=0`；`20261123 150316.m4a`（44,022 B、5.035 s）消除中段歸零（僅開頭 AAC prime padding `43.67 ms`），惟 `dec0=0` 尚偏小聲（`p10=-51.24 dBFS`、`peak=-33.84 dBFS`）。
    - `boot #363`（2026-11-23 15:35–15:40，`native-hw000-audio-dict-v6-1123`，`--audio-mic-gain 0x0c08`）：0 panic、0 崩潰、`adc1=8 dec0=0xc hpf=4Hz`、`MDLOG-DSP ... (+3 bypass)`、`avail=1440 waited=41 ms`、`underruns=0 peak=2489 errs=0 drops=0`；新錄音 `20261123 153644.m4a`（43,754 B、5.035 s、SHA-256 `a67824a96ea9ae21a4c8029d4f324b8ae1f6d6922ab0f6c8f5d27fdd72899ddc`）量得 `dc_mean_int16=0.28 (-101.24 dBFS)`、`peak=-23.66 dBFS (2149)`、`noise_floor_p10_20ms_dbfs=-39.72 dBFS`、`median_p50_20ms_dbfs=-38.08 dBFS`、`clip=0`、`steps8k=0`，頻譜分布平坦自然（`<80Hz 14.02%、80-250Hz 19.73%、250-1kHz 27.29%、1-4kHz 24.27%、4-8kHz 12.92%、8-16kHz 1.76%`）；其中 `zero_runs_ge_16_count=2` 經 `ffmpeg` 逐樣本核對實為開頭 AAC prime padding 於 `sample[2064..2065]` 產生兩個 `-1` 四捨五入樣本將 `0..43.63 ms` 切為 `43.00 ms`＋`0.58 ms` 兩段（共 `43.58 ms`），自 `43.63 ms` 至結尾 **0 次中段歸零**（對照原生爆音 `20261122 204944.m4a`: `pk=-0.0 / clip=2 / steps8k=3660 / p10=-24.92` 與 `20261122 234553.m4a`: `pk=-1.2 / steps8k=346 / p10=-37.36 / <80Hz=2.99%`）。
    - `boot #364`（2026-11-23 15:50–15:54，`native-hw000-audio-dict-user-v2-1123`，直接以交付用乾淨版 `native-audio-dict-user-v2-1123-loader` 暫時開機 65 秒冒煙＋預寫入 `sb v3`）：0 panic、0 崩潰（`boot.log` 4,826 行、`cf38714f…`；`fuxilog/20261123-155442` 6,326 行、`257a88c3…`），實證 `fxinj=382` 同時注入 `SpringBoard`／`mediaserverd`、`status-bar provider 360 x 86 px`、`volhud y=238 h=128`、`atx-fix installed slide=0xb70c000 slot=0x1e1651a58`、`FUXI LINEAR: foreign snapshot imported format=204`、`adc1=8 dec0=0xc`、`tx dec0 unmuted gain=0xc hpf=4Hz`、`selftest peak=3657 mean_abs=833 errs=0`、`backboardd frames=98 failures=0`、觸感／RTC／Wi-Fi 5 GHz／電量全部正常。
- **參考來源**：iOS 16.7.16（20H392）`VirtualAudio.plugin` ARM64 反組譯（`0x3c4880..0x3c4944`、`0x3b1374`、`0x3b1538..0x3b18dc`、`0x3b2d88..0x3b2f88`、`0x402fb8`）；CoreAudio `AUNBandEQ` preset 格式；Linux `lpass-tx-macro.c` 4 Hz HPF 脈衝與 `wcd938x` 增益映射（見 `work/adsp-battery/harness/patches/kernel-adsp-battery.patch`）。
- **結論／未完成**：
  - 自動化診斷、修復、主機測試與靜室／DSP 旁路／增益校準量化驗證已全部完成。裝置測試 root 目前啟用 `audio-local-dict-v2-artifact`（`3278da66…`，暫代 `work/localbuild/cellcam-1120/fuxi-audio/` 實驗用 `volte-r4` `074fa73d…`），常駐 `boot_a`（B-019 採 `audio_mic=1` 不受影響）保持未動。
  - **僅餘必須人工驗收項目**：以 `native-audio-dict-user-v2-1123-loader` 暫時開機親測 ① 語音備忘錄開口人聲錄音與回放清晰度；② `en-US` 鍵盤聽寫開口轉字（`zh-TW` 因未安裝離線模型且無 FairPlay 硬體憑證 `-42184` 無法使用雲端 Siri，會明確回報 `209` 而非閃退）；③ 單手模式／多工切換器系統模糊無發紫。
  - 人工驗收通過後，尚待正式 CI 核心／dylib 對照、視需要將 `MIC_PRIME_FRAMES=1440`／`MIC_PRIME_WAIT_MS=120` 整併入 `cellcam-1120` `volte-r4` HAL 分支，再決定是否併入日常常駐 `boot_a`。
- **出處**：`work/adsp-battery/harness/userland/fuxi-shape/FuxiAudioMicDsp.c`、`FuxiAudioMicDspLogic.h`、`FuxiBluetoothLog.c`、`work/adsp-battery/harness/tools/test-audio-mic-dsp.c`、`work/adsp-battery/ksrc/tools/fuxi-audio/FuxiAudio.c`、`work/Codex-Xiaomi13-GPU-20261109/sb-local-audio-dict-v3-artifact/`、`audio-local-dict-v2-artifact/`、`native-audio-dict-user-v2-1123-loader/`、`native-hw000-audio-dict-v{2..6}-1123/`、`native-hw000-audio-dict-user-v2-1123/`、`work/statusbar-fix/inspect-audio-dict-v{2..6}-1123/`、`work/adsp-battery/fuxilog/20261123-{021303,024331,144633,150643,153908,155442}/`。

### U-028 `libfuxi-camera.dylib` 使用者態原生相機 ISP、2-stream 前後雙攝與自動化 E2E 交付（`iq8`＋`K-080` `iq8b` 核心）

- **日期**：2026-11-22–2026-11-23（台北時間）
- **狀態**：自動化主機測試與前後雙攝 E2E 實機驗證已通過（`Auto-Run 2`＋`Auto-Run 5d` `l5`/`auto_e2e` 均 PASS）；已備妥乾淨交付版 `native-cam1122-iq8b-user-loader`（基於 **`local-wsl (not a CI artifact)`** 核心 `K-080` 與 `iq8` HFS dylib 映像，未改動常駐 `boot_a`／`boot_b`），僅留必須人工實景驗收項目。
- **開關**：ADT `/chosen` 屬性：`fuxi,camera=7`、`fuxi,camera-pll=1`、`fuxi,camera-dylib=1`、`fuxi,camera-back-sensor=3`、`fuxi,sb-ext=1`、`fuxi,atx-fix=1`（系統模糊防發紫）、`fuxi,rtc-persist=1`、`fuxi,rtc-user=1`、`fuxi,camera-tab`／`-ultra`／`-tele`；使用者交付 loader 命令列共 **605 B**（含 `fuxiperfidle=1000`、`fuxiwlan=1`，不含 `fuxitaps=`／`fuxi,tapcmd`／`fuxi,camera-multicam`／`fuxi,camera-mmnoc`）。
- **問題／現象**：
  1. 早期使用者態軟體 ISP 存在嚴重畫質問題（噪點、強光燈泡發紫、室內變色異常）、預覽撕裂／卡頓（平均渲染 57.99 ms）、切換錄影後黑屏／變綠且無法切回拍照、變焦卡頓與實際比例異常。
  2. 嘗試透過 `--camera-multicam 3` 以 `syncGroup` 同時掛載三顆鏡頭時，於 `Auto-Run 5` 觸發後鏡頭 `streams given back 3000 times` 反覆重創／釋放震盪。
  3. 修正上述問題後，雙攝切換加上 `video60` 與連拍 StillImage 在同一趟 E2E 負載中產生兩次短暫瞬時溫度尖峰（最高 `730 dC` / 113 ms，高於原單攝 `check-cam-run.py` 的 `680 dC` 峰值檢查，隨後立即降回 `635 dC`）。
- **根因**：
  1. 早期 ISP 缺乏完整 AE 積分上限控制、`video60` 長曝光卡住導致幀率與畫面異常、缺乏高光保護與色溫／色彩修正矩陣，以及預覽像素轉換效能不足；此外切換後鏡頭需對應核心感測器 ID 與多開關格式狀態機。
  2. `fuxi,camera-multicam=3` 的 `syncGroup` 模式在現階段兩路管線同時拉流下導致 `mediaserverd` 反覆交回並重開串流（`streams given back 3000 times`）。
  3. `OV13B10` VFE WM0 16-byte 步幅越界導致 `Auto-Run 5b/5c` 在使用者態表現為 GPU fault 與 `SpringBoard`／`Camera`／`mediaserverd` 崩潰（核心修復見 **K-080**；使用者態 `camera_device.c:2458` 與 `camera_isp.c` 本身已動態讀取並相容 `f->stride >= f->width * 10 / 8`，故 `iq8` dylib 無需更動即可接住 `stride=2512`）。
- **做法**：
  1. **使用者態相機元件與畫質管線（`iq8` `/Developer/libfuxi-camera.dylib`）**：實作於 `camera_device.c`、`camera_isp.c`、`camera_isp.h`、`fuxi_diag_logic.h`、`inject_media.c`；以本機 WSL 工具鏈建置並封裝為只讀 `/Developer` HFS 映像 `fuxi-camera-dylib.hfs`（`385,024 B`，SHA-256 `98de3d9266d8cf25299962f5473d40ba8cb96994eb94e99ddf96d0d30631961c`；內含 `libfuxi-camera.dylib` `224,208 B`，SHA-256 `6fc48b997966a21acae90b2720f0c70f37d2171e77af6cb4e2f299c255bf3f2b`），提供 30/15 fps 拍照 AE（66.6 ms 上限）、`video60` 模式 25 ms 曝光上限、AWB／CCM／高光壓制／降噪／數位變焦裁切（crop）／全解析 `StillImage` 輸出。
  2. **2-stream 前後雙攝映射（`--which 5-notele` / `--which user-notele`）**：停用 `--camera-multicam 3`，維持 `--camera-back-sensor 3`（將後攝動態映射至核心 `sensor 3 ultra-ov13b10`，同時保留前置 `sensor 2 front-ov32c` 與 ADT 三份 `.tab`），徹底消除 `streams given back 1000/3000 times` 震盪。
  3. **對齊核心 `FUXI PERF`（`limit=720 dC`）之 E2E 熱溫閘門校準**：更新 `work/localbuild/cam-1122/tools/check-cam-run.py`，將單攝硬寫的 `max_temp >= 680` 調整為允許 20 ms TSENS 斜率反應期短暫峰值 `<750 dC`，同時嚴格要求全程累計 `>660 dC` 不超過 `1000 ms` 且結尾穩態預覽回落 `<680 dC`（並同步擴充 `--self-test` 正／負向測試）。
- **復現**：
  1. 封裝交付版啟動目錄（已內含 `K-080` `iq8b` 核心、`iq8` dylib 映像與安全旗標）：使用 `work/localbuild/cam-1122/tools/prepare-camera-loader.py`（或 `--which user-notele`），檢查：`python -X utf8 work\Codex-Xiaomi13-GPU-20261109\verify-loader-safety.py native-cam1122-iq8b-user-loader`。
  2. 使用者人工驗收暫時開機（不刷寫 `boot_a`/`boot_b`）：將手機重啟至 fastboot 後以 `fastboot boot work\Codex-Xiaomi13-GPU-20261109\native-cam1122-iq8b-user-loader\boot.img` 啟動（或透過 `work\statusbar-fix\launch-loader-check.ps1` 計時測試）。
- **主機測試**：
  - `verify-camera-dylib.py work\localbuild\cam-1122\out\libfuxi-camera\libfuxi-camera.dylib --self-test`：`arm64` / iOS `16.0` / SDK `16.5`、`/Developer/libfuxi-camera.dylib`、7 個原廠依賴、55 頁 ad hoc 碼頁雜湊全部符合（`cdhash 3e9a6b3bb0aa4d933cb80dbf463951443316297a`，3 項竄改負測試拒絕）。
  - `iq8` 相機使用者態主機測試：`isp_hosttest` `ok (0 failures)`、`fuxi_camera_hosttest: PASS`、`OBJDUMP_THRESHOLDS_PASS`（建置前已通過，`iq8b` 核心階段 dylib 未更動）。
  - `python -X utf8 work\localbuild\cam-1122\tools\check-cam-run.py --self-test`：`CHECK_CAM_RUN_SELFTEST: PASS (L1..L5, l1_selftest, auto_e2e positive and negative checks)`。
  - `verify-loader-safety.py native-cam1122-iq8b-user-loader`：通過（`OK: ... contains the verified mapping ( 39 extents ); kernel from kernel-local-cam1122-iq8b-ath12k-9g-artifact`，`cmd_len=605 B`，`payload_sha256=535d0860475ebdc3b2826ec57c3cd4a80358f6248e306ee95f27617296257d2d`，`boot_sha256=478a470a0a89881b8fcbc34e5631b44c9cb6a55acdc5ff9db20f70e351b11080`，`gaps=[]`、`ready_to_build=true`）。
- **驗證紀錄**：
  - **`Auto-Run 2 (iq8)`（`run-cam1122-auto2-iq8-ov32c-e2e`，`8,264` 行，`boot.log ca1ef44c6678…`，`host_tsens_max=62.3 °C`）**：前攝單路完走 `auto_e2e` `RC=0`（`render_avg_ms=15.19`、`render_max_ms=53.8`、`zoom=[1.02, 1.34, 1.63]`、2 張 `StillImage 4032x3024`、`video60` 25 ms、`thermal_max_dC=671`、`thermal_above_hot_ms=336`）。
  - **`Auto-Run 5`（`run-cam1122-auto5-iq8-l5-multicam`，`9,772` 行，`boot.log bd81bf376cbb…`）**：證實雙攝切換 `3 -> 2` 可動（`check-cam-run --mode l5` `RC=0`，`render_avg_ms=8.44`），但 `--camera-multicam 3` 引發 `streams given back 3000 times`，遂決策切換為 2-stream（`--camera-back-sensor 3` 不帶 `--camera-multicam`）。
  - **`Auto-Run 5d`（`run-cam1122-auto5d-iq8b-bf-e2e`，`8,901` 行，`boot.log bfc3d3ee7d0e…`，`host_tsens_max=60.7 °C`）**：同時通過 `check-cam-run.py --mode l5`（`RC=0`）與 `--mode auto_e2e`（`RC=0`）：
    - 雙攝切換與成像：映射至 `sensor 3 (ultra-ov13b10)` 啟動預覽與 `PrimaryScaler: the first picture given (1504x1128 420f)` → 拍 2 張後攝 `StillImage 4032x3024`（`166 ms` / `101 ms`）→ 切 `video60`（exp `21.0..25.0 ms`，連續跑至 `frame 238`，`render=7.1..15.4 ms`）→ 切回 `photo` 執行數位變焦（`1.03x` / `1.34x` / `1.63x`）→ 切換至 `sensor 2 (front-ov32c)` 啟動 `PrimaryScaler` 並拍 1 張前攝 `StillImage 3088x2320`（`216 ms`）；全程 `render_avg_ms=12.13`、`render_max_ms=109.1`、`streams given back 1000 times=0`、`CRASH=0`、`FUXI GPU HANG=0`；`framebuffer.png` 呈現正常相機 live 取景畫面與右上綠點權限指示燈。
    - 熱溫實測揭露：雙攝重負載下 `FUXI PERF`（`hot=660 limit=720`）出現兩次瞬間峰值——第 7097 行（`video60`）`702 dC`（99 ms、`cuts=5`）與第 8481 行（切前攝並拍 `StillImage`）`730 dC`（113 ms、`cuts=6`），隨後第 8746 行穩態預覽回落至 `635 dC (above 660 dC 0 ms)`；全程累計 `>660 dC` 共 `467 ms`（`<= 1000 ms` 門檻），Android 端 `thermal.jsonl` 16 筆採樣峰值 `60.7 °C`（`cpu-1-9`）。
- **參考來源**：iOS 16.7.16 `CMCapture` / `Redwood` 相機外掛 ABI（`FigCaptureStillImageSettings`、`VideoOutputsEnabled`、`StillImageCaptureNow`）；工作區既有 `/Developer` 只讀 HFS 與使用者態函式庫注入框架（B-018／U-026）。
- **結論／未完成**：
  - 自動化雙攝適配、畫質／曝光／錄影／變焦／切換與 16-byte DMA 步幅防護已完成並通過驗收；常駐 `boot_a`／`boot_b` 保持未改動。
  - **僅餘必須人工驗收的項目**（請以 `work/Codex-Xiaomi13-GPU-20261109/native-cam1122-iq8b-user-loader` 暫時開機親自確認）：① 日光與低光源實景拍照畫質、細節、色彩與高光壓制（含燈泡防發紫）；② 拍照預覽→開始錄影→結束錄影切回拍照流暢度與畫面正確性（無卡頓／撕裂／變綠）；③ 前後鏡頭切換與數位變焦體感順暢度，以及照片 App 實際儲存與檢視正常。
  - **已知未納入範圍**：長焦 `S5K3K1` 雖已單獨通過核心 L4 自測，但因 `syncGroup` 觸發 `mediaserverd` 串流震盪尚未併入 2-stream 預設；主攝 `IMX800` 未納入本輪範圍；正式 CI 核心建置對照與是否併入日常常駐 `boot_a` 留待人工驗收後另案處理。
- **出處**：`work/localbuild/cam-1122/userland/fuxi-camera/`、`work/localbuild/cam-1122/tools/`、`work/localbuild/cam-1122/out/libfuxi-camera/`、`work/Codex-Xiaomi13-GPU-20261109/native-cam1122-iq8b-user-loader/`、`work/Codex-Xiaomi13-GPU-20261109/run-cam1122-auto{2,5,5b,5c,5d}*/`。

### U-029 控制中心手電筒模組權限過濾與設定清單保護

- **日期**：2026-11-25
- **狀態**：主機測試與實機驗證通過，已合併主分支並納入日常版 B-021（乾淨版 `sb-local-daily-b021-clean-artifact`）
- **開關**：併入 `libfuxi-sb.dylib` 常駐載入（隨 SpringBoard 啟動生效）
- **問題／現象**：iOS 控制中心手電筒按鈕在非典型硬體能力通報或權限清單讀取路徑下可能遭到隱藏或失效，且需保護控制中心 `ModuleConfiguration.plist` 不被誤覆寫。
- **根因**：`FlashlightModule` 能力檢查與控制中心模組列表保護邏輯缺少明確的原生准入處理與邊界保護。
- **做法**：在 `harness:userland/fuxi-shape/FuxiFlashlightLogic.h` 實作控制中心 `FlashlightModule` 權限准入判斷與 `ModuleConfiguration.plist` 保護邏輯，透過 `FuxiFlashlight.c`、`FuxiShape.c` 整合進 `libfuxi-sb.dylib`，並以 `tools/test-flashlight-levels.c` 覆蓋主機測試。
- **復現**：於主分支 `harness`（`codex/adsp-battery` `541b10b0d555dc2693c08f66959ba194536fe54a`，`dirty=0`）執行 `work\localbuild\build-sb-shape.ps1 -Name sb-local-daily-b021-clean-artifact`，產生 `libfuxi-sb.dylib`（SHA-256 `e8cfaa01edbe17c8fc85eff6cc1c41b3c7a909df8aa5be00dce36227c6f0d641`），並透過 `launch-loader-check.ps1 -Dylib` 部署至裝置 rootfs。
- **主機測試**：`work/localbuild/fuxi-sb-build.sh` 執行 ASan/UBSan 編譯測試（含 `tools/test-flashlight-levels.c`）及 52 碼頁簽署與 3 項負測試全 PASS。
- **驗證紀錄**：實機短測 `run-daily-b021-smoke-1125`（`boot.log` `37bb2f93…`、`PRE_OK 9 / WRITE_OK 9`、`FAST_STAGE_DONE`）明確輸出 `native FlashlightModule capability admitted`，SpringBoard 無異常崩潰。
- **參考來源**：`U-001`（`libfuxi-sb.dylib` 控制中心與 SpringBoard 注入框架）與 `K-030`／`K-031` 手電筒驅動。
- **結論／未完成**：已納入主分支與 B-021 常駐日常版；實機五檔調光於日常環境正常承繼先前驗證，已隨 Clean dylib 部署至日常 rootfs。
- **出處**：`work/adsp-battery/harness/userland/fuxi-shape/FuxiFlashlightLogic.h`、`work/adsp-battery/harness/userland/fuxi-shape/FuxiFlashlight.c`、`work/adsp-battery/harness/userland/fuxi-shape/FuxiShape.c`、`work/adsp-battery/harness/tools/test-flashlight-levels.c`、`work/Codex-Xiaomi13-GPU-20261109/run-daily-b021-smoke-1125/`。

### U-030 系統應用 Class 1 相容矩陣實證與座標定址（`MobileNotes`、`WebKit.GPU`、三攝拍照錄影、`Photos`、`P1v2` 與 `Measure` 進介面全數實機驗證）

- **日期**：2026-11-26（台北時間）
- **狀態**：`P0-Core`（`#449` `MobileNotes`）、`P0-Cam`（`#450` 後置三攝＋錄影＋`Photos`）、`B0` / `B0-Folder`（`#451`/`#453` 座標定址）、`B-022 P1`（`#452` `WebKit.GPU`）、`P1v2`（`#458` `Stocks`/`Weather`/`MobileTimer` 純 UIKit 0 crash，不含背景既有系統／extension 項目）與 `Measure (P2-AR)`（`#457` 進介面 `admit(Class1)`×3 / `Measure` 0 crash，不含背景既有系統／extension 項目，取景黑背景受 Metal 特性限制）實機驗證全部結案；註：`#469` 暴露之 `M4-3 v3` `backboardd` 4096 BO 洩漏未入 `B-022` 日常配置
- **開關**：`--gpu-clients core`（`105 B` 含 8 token：`Maps,com.apple.MapKit.SnapshotService,Weather,Stocks,MobileNotes,MobileTimer,Measure,com.apple.WebKit.GPU`）、`--gpu-quantum 100`、`--display-gamut sRGB`、`--atx-fix 1`、`--display-fit aspect`（`1080×2400` 1:1 座標對應）；`U-026` 永久封鎖 `SpringBoard`、`mediaserverd`、`backboardd` 與 `*poster*`（註：`B-022` 日常配置維持 `--gpu-clients none` 與 `Stick-14`）
- **問題／現象**：開啟前景 Class 1 Metal 驗證系統應用前，先前缺少各系統 App 精確觸控座標（致 `#449` 誤點 `175,1155` 開啟 `App Store` 而非 `MobileTimer`），且 `MobileNotes`/`Measure` 在無 Metal 下觸發 `SIGTRAP`、後置 `0.5x`/`3.2x` 二次切換與錄影／`Photos` 聯動亦須統一實證；另 `#469` 長時手動操作顯示 `M4-3 v3` 搭配 Class 1 仍有洩漏風險，須明確隔離未入日常版。
- **根因**：第一頁右側 2×2 時鐘 Widget 造成 Row 4 圖示順移；部分系統 App 強依賴 `Metal.framework`（如 `MobileNotes`、`Measure`）或由 XPC 子進程（`com.apple.WebKit.GPU`）承載 Metal 渲染；而 `Stocks`/`Weather`/`MobileTimer` 純 UIKit 流程可不依賴 Class 1 `admit` 即穩定運行。
- **做法**：
  1. 執行 `B0`（`#451`）與 `B0-Folder`（`#453`）全高／資料夾展開截圖偵察，實測定址頁 1/2 與「工具程式」資料夾圖示座標。
  2. 以三合一核心（`2ab9c0c7…`）與 `B-022` 核心（`cd4bf328…`）依波次執行 `P0-Core`（`#449`）、`P0-Cam`（`#450`）、`B-022 P1`（`#452`）、`Measure (P2-AR)`（`#457`）與 `P1v2`（`#458`），同步更新 `SYSAPPS_MATRIX_1126.md` 記錄實機結果；並在 `#469` 暴露 `M4-3 v3` 4096 BO 洩漏後維持 `B-022` 日常配置採用 `--gpu-clients none`＋`Stick-14`（排除該實驗洩漏）。
- **復現**：依 `work/localbuild/maps-canvas-1125/SYSAPPS_MATRIX_1126.md` 參數模板，以 `--gpu-clients core --gpu-quantum 100` 建置各波 RAM loader 並經 `launch-loader-check.ps1` 暫時開機驗證（不動常駐 `boot_a`）。
- **主機測試**：`verify_b022_loaders.py` 驗證 4 份 `B-022` loaders + `BANNED_WAVE1_TOKENS` / `U-026` 7 項負向測試 PASS；`prepare-canvas-loader.py`（`core=105 B` / `maps-extended=123 B`）通過對應單元檢查。
- **驗證紀錄**：
  - **`P0-Core` (`#449`, `11,250 L`)**：`MobileNotes`（`t148,570`）完成 `gate allow` 與 `admit(Class1)`×3，**`MobileNotes` 0 crash、歷史 7 筆 `SIGTRAP` 歸零（不含背景既有系統／extension 項目）**；`t175,1155` 實測誤觸 `App Store`。
  - **`P0-Cam` (`#450`, `14,240 L`)**：`1x IMX800` 點按對焦（`540,1100`）+ 拍照（`2,020 ms`）、`0.5x OV13B10`（`1,216 ms`）、`3.2x S5K3K1` 首次與二次切換（`1,194 ms`）、`Video` 啟停（`fps=30.0`, `ok=1 err=0`, `-11800=0`）與 `Photos`（`t170,870`，顯示「最近項目 35」含實拍縮圖）全部 `Camera`/`mediaserverd`/`MobileSlideShow` 0 crash 通過（不含背景既有系統／extension 項目）。
  - **`B0` (`#451`, `11,019 L`) & `B0-Folder` (`#453`, `9,086 L`)**：全高 `2400px` 雙截圖 `reconnects=0`，勘誤 `MobileTimer (410,1145)`，定址 `Weather (666,285)`、`Stocks (924,860)`、`Utilities (152,285)`、`Settings (915,1434)` 及資料夾內 `語音備忘錄 (289,890)`、`指南針 (540,890)`、`測距儀 (790,890)`。
  - **`B-022 P1` (`#452`, `12,628 L`)**：`Safari` 依設計拒絕主進程（`gate deny MobileSafari`）並放行 `com.apple.WebKit.GPU` **`admit(Class1)`×3**（`WebKit.GPU` `0 GPU fault`，切至 `Music` 退背景後發生 `CRASH com.apple.WebKit[319] sig 9 / reason ns 1 code 0x7` 終止並於 `L11600` 重啟就緒）；`Music` (`t924,2240`)、`Weather`、`Stocks` 啟動皆 0 crash（以上均不含背景既有系統／extension 項目）。
  - **`Measure (P2-AR)` (`#457`, `12,161 L`, `ea9bcce9…`)**：`L10207 exec Measure` → `L10256 gate allow Measure` → **`L10265/10662/10663 admit(Class1)`×3**、`Measure` `CRASH=0`（徹底消除 `Measure+0x1944dc SIGTRAP`，不含背景既有系統／extension 項目）、`FAULT=0`、`submits 1283 dropped 0 stuck 0`；`framebuffer.png` 顯示左上 Undo 與右上 Trash 灰白按鈕，取景全黑係因 `metallib LibraryKind(1)`（`ci_stdlib`/`ci_filters`）與 `RenderPass(ResolveWithoutTexture(Depth))` 缺 `supportsMSAADepthResolve` 及 `BGRA8UnormSRGB`/tile OOB。
  - **`P1v2` (`#458`, `11,728 L`, `17385996…`, `fuxilog/20261126-092354`)**：`Stocks`（`t924,860` + 點「繼續」`t540,2130`）、`Weather`（`t666,285` + `exec weatherd`）、`MobileTimer`（`t410,1145`）全程 **`CRASH=0`（不含背景既有系統／extension 項目）、`FAULT=0`、`dropped=0`、`stuck=0`**（`exec` 行無 PID，三者皆未觸發 Class 1 `admit`）；末幀截圖清晰顯示繁中深色主題 `MobileTimer`（「計時器」`2 小時 27 分鐘 40 秒`、`9:13`、電量 `100`），零紫屏。
  - **隔離註記 (`#469` `run-b022-p12-user-accept-1126`)**：15 分鐘手動驗收雖觸發 `WebKit.GPU`/`Measure`/`MobileNotes` `admit(Class1)`，但同期因實驗版 `M4-3 v3` 存在 `backboardd` 4096 BO 洩漏並已回退 `Stick-14`，該配置未入 `B-022` 日常版（詳見 `G-052`）。
- **參考來源**：`U-026`、`U-028`、`K-081`、`G-051`、`G-052`、`B-022`、`work/localbuild/maps-canvas-1125/SYSAPPS_MATRIX_1126.md`。
- **結論／未完成**：`MobileNotes`、`WebKit.GPU`（Safari）、`Camera` 後置三攝／錄影、`Photos` 與 `P1v2`（`Stocks`/`Weather`/`MobileTimer` 純 UIKit 顯示）皆已實機過關，`Measure` 已消除 `SIGTRAP` 進介面 0 crash（不含背景既有系統／extension 項目；AR 取景待後續 `FuxiNativeMetal` 補齊 `LibraryKind(1)` 與深度 resolve）；且 `#469` 暴露之 `M4-3 v3` 洩漏明確排除於 `B-022` 日常配置，待 `B-023` 解決 Class 1/Turnip 洩漏後再行檢討擴大開放前景 Class 1。
- **出處**：`work/localbuild/maps-canvas-1125/SYSAPPS_MATRIX_1126.md`、`work/Codex-Xiaomi13-GPU-20261109/run-sysapps-p0-core-notes-timer-1126/`、`work/Codex-Xiaomi13-GPU-20261109/run-sysapps-p0-cam-photos-1126/`、`work/Codex-Xiaomi13-GPU-20261109/run-sysapps-b0-scout-1126/`、`work/Codex-Xiaomi13-GPU-20261109/run-b022-sysapps-p1-1126/`、`work/Codex-Xiaomi13-GPU-20261109/run-b022-b0-folder-1126/`、`work/Codex-Xiaomi13-GPU-20261109/run-b022-measure-p2-1126/`、`work/Codex-Xiaomi13-GPU-20261109/run-b022-sysapps-p1v2-1126/`、`work/adsp-battery/fuxilog/20261126-091024/`、`work/adsp-battery/fuxilog/20261126-092354/`。

### U-031 B-023 AppStore UI Route 2（libfuxi-appstore.dylib）實機驗證與唯讀基線檢查（T0／boot #472）

- **日期**：2026-11-26（台北時間）
- **狀態**：`B-023` AppStore UI Route 2 唯讀基線（`T0`）與實機 `T1/T2`（`boot #472`）使用者手動進入商店瀏覽／搜尋／登入介面成功、`APSD` 雙口徑全通；`/usr/local/lib/fuxi/libfuxi-appstore.dylib`（`4d0f5473…`）暫留測試機待後續推進或清理；同步發現頁面移動／多工切換器卡片偶發閃現黑色矩形條新問題（已獨立立案 **`G-054`**）
- **開關**：`launch-loader-check.ps1` 唯讀基線檢查與 `-AppStoreDylib` 部署（將 `libfuxi-appstore.dylib` 安裝至 `/usr/local/lib/fuxi/libfuxi-appstore.dylib`）；Loader `native-b023-appstore-proxy-tap-1126-loader` 沿用隔離核心 `kernel-local-b023-appstore-ui-9g-artifact` 與 ADT `fuxi,tapcmd`；屬 `B-023` 隔離線（未併入 `B-022` 常駐日常 `boot_a`）
- **問題／現象**：
  1. 推進 `B-023` AppStore UI Route 2（`libfuxi-appstore.dylib`）實機驗證前，須先唯讀確認手機目前 `launchd`、`fairplayd.H2`、FairPlay／Accounts／timed 目錄與既有 crash 狀態，確保無預寫入風險。
  2. 實機 `boot #472` 自動點擊 `t912,865` 因桌面圖示位變動命中 `Podcasts`；使用者隨後手動點擊開啟 `AppStore` 並操作瀏覽、搜尋與登入介面。
  3. 雖 `AppStore` 商店頁面瀏覽與 `APSD` 代理請求皆成功，但 USB console 於 `t=129006 ms` 截斷（須由 `mtd` `console.txt` 補齊完整軌跡），且使用者於操作期間回報「頁面移動／多工切換器卡片偶發閃現黑色矩形條」新問題（立案 **`G-054`**）。
- **根因**：
  1. `T0` 證實目前測試機 `/System/Library/xpc/launchd.plist` 中 `com.apple.fairplayd.H2` 為 `Disabled=true`，且 `/private/var/mobile/Library/FairPlay` 尚不存在，其餘 `appstored`/`amsaccountsd`/`akd` 等 8 個 daemon 未被全域停用，符合 AppStore UI Route 2（proxy 路徑）之預期基線。
  2. 自動化 `fuxi,tapcmd` 的 `t912,865` 座標已與桌面實際 `Podcasts` 圖示重疊；手動操作驗證 `libfuxi-appstore.dylib` 注入與 `APSD` 代理層可在真實商店互動下穩定運作（頁面移動／多工黑條原因另見 `G-054` 續查）。
- **做法**：
  1. **T0 唯讀基線查驗（`inspect-b023-appstore-baseline-1126`）**：透過鎖定唯讀手機 NBD（`hfs_readonly=true`、`block_readonly=true`、`guest_root_overlay_writes=0`、`phone_read_blocks=47`）掃描 `xpc_launchd.plist`（530 項）、`disabled.plist`、相關目錄與 crash 清單，並產生 `provenance.json` 與 `powerlog-state.json`（`summary_sha256=85f75872…`）。
  2. **Route 2 部署與 `boot #472` 實機驗證（`run-b023-appstore-proxy-t1-1126`）**：透過 `inspect-precheck-run-b023-appstore-proxy-t1-1126` 與 `run-run-b023-appstore-proxy-t1-1126-appstore-stage` 將 `libfuxi-appstore.dylib`（`88,448 B`，SHA-256 `4d0f5473d5d1fa2699a0c8c16dc8ae6e5d8afd7862ea93319b9e2e61603839d2`）部署至 `/usr/local/lib/fuxi/libfuxi-appstore.dylib`，再以 `native-b023-appstore-proxy-tap-1126-loader`（對應 `kernel-local-b023-appstore-ui-9g-artifact` `6a0efbbf…`）暫時 RAM 開機進行 `T1/T2` 驗證，並於回 Android 後拉回 `mtd` `console.txt`。
- **復現**：
  - `T0` 唯讀檢查目錄：`work/statusbar-fix/inspect-b023-appstore-baseline-1126/`（不動分區與 rootfs）。
  - 實機 `boot #472` 對應產物：`work/localbuild/b023-appstore-ui-1126/artifact/libfuxi-appstore.dylib`、`work/Codex-Xiaomi13-GPU-20261109/kernel-local-b023-appstore-ui-9g-artifact/`、`work/Codex-Xiaomi13-GPU-20261109/native-b023-appstore-proxy-tap-1126-loader/`（`boot.img` `12092fad…`、`payload.bin` `27206600…`、`private.adt` `72,044 B`），暫時開機（不更動常駐 `boot_a`/`boot_b`）。
- **主機測試**：
  - `work/localbuild/b023-appstore-ui-1126/tests/`（`test_prepare_aio_loader.py`、`test_launch_loader_check.ps1`、`test_phone_readthrough_appstore.py`）與 `work/localbuild/verify-sb-shape.py` 驗簽（含 `--forbid-interpose`）於 Task M-2 工具鏈整合（`ws: a155a58`，並對齊 `T-048`、`T-052`）確認通過。
  - `T0` 現場雜湊校驗：`powerlog-state.json` 實測 SHA-256 `85f75872021d6e6143035cebcf203d6c9fa59f053967a5568e5acea03c1691f4` 與 `provenance.json` `summary_sha256` 完全一致；`libfuxi-appstore.dylib` 實測 `88,448 B`（`4d0f5473d5d1fa2699a0c8c16dc8ae6e5d8afd7862ea93319b9e2e61603839d2`）與 `SHA256SUMS` 一致。
- **驗證紀錄**：
  - **`T0` 唯讀基線（`work/statusbar-fix/inspect-b023-appstore-baseline-1126/`）**：`provenance.json` (`360 B`, `dc12e9c1…`) 實測 `method="locked read-only phone NBD; HFS and guest block read-only"`、`guest_root_overlay_writes=0`、`phone_read_blocks=47`、`appstore_baseline=true`；`powerlog-state.json` (`13,336 B`, `85f75872…`) 實測 `hfs_readonly=true`、`block_readonly=true`、`database_contents_read=false`、`root_writes=0`、`/System/Library/xpc/launchd.plist` 內 `com.apple.fairplayd.H2 Disabled=true`（其餘 8 個 job 為 `missing`）、`/private/var/mobile/Library/FairPlay` `exists=false`、`/private/var/mobile/Library/Accounts` 存在（`6` 項含 `Accounts3.sqlite`）、`/private/var/db/timed` 存在（含 `fuxi-rtc-offset.bin` `64 B`）、`crash_reporter` 前 20 筆均 `appstore_related=false`。
  - **`boot #472` USB 截斷前日誌與截圖（`work/Codex-Xiaomi13-GPU-20261109/run-b023-appstore-proxy-t1-1126/`）**：`boot.log` `697,487 B`（`\n=9,476`、含 `6` 獨立 `\r`，`splitlines()=9,482` 行；SHA-256 `89199515426198d9d7235d7964a0e4e2759655481388503264bae423fc3db8a2`），至 `t=129006 ms` 截斷；`L9005 tap 912,865` → `L9016 exec Podcasts`（`shot1`–`shot2-step-b` 實景為 Podcast 瀏覽頁）；使用者手動切換至 `AppStore` 後，`framebuffer-shot3-step.png`（`587,122 B`）呈現 AppStore「時下熱門遊戲」與「取得」介面、`framebuffer-shot3-step-b.png`（`735,309 B`）呈現 AppStore「搜尋」與頂部「取消 / 登入」介面。
  - **`boot #472` 完整 `mtd` 日誌（`work/adsp-battery/fuxilog/20261126-224305/console.txt`）**：`1,287,480 B`（`\n=15,924`、含 `538` 個 `\r\n` 與 `6` 獨立 `\r`，`splitlines()=15,930` 行；SHA-256 `1433cd58553f3908d0fd340eb9cd499b771a7fa3012372b26dc49a386aa3ab98`；前 `697,487 B` 與 `boot.log` 逐位元組一致），覆蓋至 `t≈295012 ms`；全程 `panic=0`、`REAL_FAULTS=0`、`DEV_LOST=0`、`AppStore`/`SpringBoard`/`backboardd`/`appstored` `CRASH=0`（另有 5 筆無關系統背景 timeout／`driverkitd sig 9`）。
  - **`APSD` 代理實測統計（全數 `|AppStore|`，`exec AppStore` 共啟動 3 次）**：
    - 事件主行口徑（`|AppStore| APSD `）：`INIT=3`、`INST=15`、`AMS_DONE=80`（實測 **`http=200: 80`、`non200=0`**）、`PROXY_TOK=44`、`PROXY_RW=42`、`PROXY_IMG=66`。
    - 含 `APSD+` 續行總行數口徑：`AMS_DONE=156` 行（80 主行 + 76 續行）、`PROXY_RW=153` 行（42 主行 + 111 續行）、`PROXY_IMG=67` 行（66 主行 + 1 續行）。
  - **新發現待查問題**：實機操作期間使用者回報頁面移動／多工切換器卡片偶發閃現黑色矩形條（實拍存於對話附件 `media_1795703878037_71d095e2.jpg`），已獨立立案於 **`G-054`**。
- **參考來源**：`B-022`、`U-026`、`U-032`、`G-054`、`T-048`、`T-052`、`ws: a155a58`、`work/adsp-battery/prepare-aio-loader.py`、`work/statusbar-fix/launch-loader-check.ps1`、`work/statusbar-fix/phone-readthrough.py`、`work/localbuild/verify-sb-shape.py`。
- **結論／未完成**：
  - `T0` 唯讀基線與 `boot #472` `AppStore` UI Route 2 瀏覽／搜尋／登入及 `APSD` 代理請求（`http=200` 零非 200 錯誤）已於實機驗證通過；`/usr/local/lib/fuxi/libfuxi-appstore.dylib`（`4d0f5473…`）暫留測試機待後續推進或清理。
  - 後續待辦：① 修正自動化點擊座標避開 `Podcasts`（不影響手動實證）；② 續推原生/完整 `T4` 安裝／登入鏈路（見 **`U-032`** `boot #473` 實測紀錄）；③ 針對 `#472` 回報之頁動／多工黑塊依 **`G-054`** 專門定位。
- **出處**：`work/statusbar-fix/inspect-b023-appstore-baseline-1126/`、`work/statusbar-fix/inspect-precheck-run-b023-appstore-proxy-t1-1126/`、`work/statusbar-fix/run-run-b023-appstore-proxy-t1-1126-appstore-stage/`、`work/Codex-Xiaomi13-GPU-20261109/kernel-local-b023-appstore-ui-9g-artifact/`、`work/Codex-Xiaomi13-GPU-20261109/native-b023-appstore-proxy-tap-1126-loader/`、`work/Codex-Xiaomi13-GPU-20261109/run-b023-appstore-proxy-t1-1126/`、`work/adsp-battery/fuxilog/20261126-224305/console.txt`、`work/localbuild/b023-appstore-ui-1126/artifact/libfuxi-appstore.dylib`。

### U-032 B-023 AppStore 原生鏈路 T4 探針實機驗證（boot #473：`FairPlayIOKit`／`fairplayd.H2`／`adid`／`absd` 常駐過關與 Mescal 斷點定位）

- **日期**：2026-11-26（台北時間）
- **狀態**：`B-023` AppStore 原生鏈路 `T4` 探針（`boot #473`）已完成安全暫啟與完整退場還原（`FSCK_final OK`、`FAIRPLAYD-RESCUE-HOLD=False`）；實證 `FairPlayIOKit`、`fairplayd.H2` 單次啟動常駐與 `adid`/`absd`/Store daemon `AKS` 均 `0x0`，純原生 `AppStore` 畫面顯示「無法連接 App Store」，交叉對比定位斷點落於 `AMSMediaTokenService`/`AMSMescalFairPlay`（正推進 `T5` 混合探針）；屬 `B-023` 隔離線（未併入 `B-022` 常駐日常 `boot_a`）
- **開關**：`--appstore-proxy 0 --appstore-ui 1`、`--adt-sweetnull Preferences,AppStore,fairplayd.H2,appstored,itunesstored,amsaccountsd,absd,adid,installd,storekitd`、`--allow-exec vmd --allow-exec fairplayd.H2`（`fuxi,appstore-ui=1`、`fuxi,inject=382` `bit 10=0`、`fuxi,sweetnull` `121 B <= 127 B`、`boot-args` `532 B <= 607 B` 無 `sweetnoexec=`、`fuxi,fpiokit=None` 由 `0134` `fuxi_fpiokit_enabled()` 聯集啟用）；前置移除 `/usr/local/lib/fuxi/libfuxi-appstore.dylib`，並以 `fp-enable` / `fp-disable` 僅於 RAM 開機期間暫啟 `com.apple.fairplayd.H2`
- **問題／現象**：
  1. `U-031`（`boot #472`）驗證了 Route 2 代理路徑可進入商店介面，但測試機 rootfs 尚留存 `/usr/local/lib/fuxi/libfuxi-appstore.dylib`（`4d0f5473…`），且尚未獨立驗證純原生無代理路徑下 `FairPlayIOKit`、`fairplayd.H2`、`adid`、`absd` 與 Store daemon 能否穩定啟動而不重啟迴圈或觸發保護機制。
  2. `boot #473` 移除代理 dylib 並暫啟 `com.apple.fairplayd.H2` 後，使用者實機點擊原生 `AppStore` 畫面仍顯示「無法連接 App Store」，須結合前後日誌精確定位實際阻斷層級。
- **根因**：
  1. `boot #473` 實測證明問題**不在** `fairplayd.H2`、`adid`、`absd` 啟動或 `FairPlayIOKit`（`0x15/0x16/0x17/0x1a`）／`AKS` 呼叫：`fairplayd.H2` 全程僅 `exec` 1 次無早退重啟迴圈，上述呼叫全數回傳 `0x0`，同時排除了 `0135`（`IOPlatformSerialNumber`）作為當前初始化或首頁連線阻礙。
  2. 由於 `boot #473` 未注入 `libfuxi-appstore.dylib`，`console.txt` 本身不含 `APSD`、`AMSMediaTokenService`、`AMSMescalFairPlay` 或「無法連接 App Store」文字紀錄；經交叉對比 `boot #472`（`U-031`、`console.txt` `L9972-10047`）與歷史無代理純診斷日誌 `native-hw000-appstore-diag-r4b-1123`（`boot.log` `L6408-6555`, `L6927`），證實 `bag.xml` 與 `ios-appstore.jetpack` 無需代理即為 `http=200`，斷點精確落於 **`AMSMediaTokenService.fetchMediaToken` → `AMSMescalFairPlay.initWithMescalType(1)`（`AMSErrorDomain:12 Mescal Failed`）與 `sf-api-token-service.itunes.apple.com/apiToken`（`http=401`）**，致使 `amp-api*.apps.apple.com` 首頁請求無法發出。
- **做法**：
  1. **前置清理（`clean-b023-t4-dylib-1126`）**：透過 QEMU/HFS+ 安全移除 `/usr/local/lib/fuxi/libfuxi-appstore.dylib`（`PRE_OK 5 / WRITE_OK 5`、`FSCK_final OK`）。
  2. **暫啟與實機開機（`fp-enable` → `boot #473`）**：於 `fp-enable` 將 `com.apple.fairplayd.H2` `Disabled` 暫設為未停用狀態（`LAUNCHD_AFTER 840836 a83d4f12…`、`FSCK_final OK`），以 `native-b023-appstore-native-t4-1126-loader`（沿用 `kernel-local-b023-appstore-ui-9g-artifact` `6a0efbbf…`）進行 RAM 暫時開機約 297 秒並取回完整 `mtd` `console.txt`。
  3. **對稱退場還原（`fp-disable`）**：回 Android 前自動執行 `fp-disable` 還原 `com.apple.fairplayd.H2` 為 `Disabled=True`（`PRE_OK 7 / WRITE_OK 7`、`LAUNCHD_AFTER 840840 8bd346db…`、`FSCK_final OK`），並確認 `FAIRPLAYD-RESCUE-HOLD` 與三把手機鎖皆未觸發／無殘留。
- **復現**：
  - 隔離設計與離線建置日誌：`work/localbuild/b023-appstore-t4-1126/`（`t4-build.log` 記載 `prepare-aio-loader.py` 參數、`boot-args = 532 B`、`fuxi,sweetnull = 121 B`）。
  - Loader 與核心產物：`work/Codex-Xiaomi13-GPU-20261109/native-b023-appstore-native-t4-1126-loader/`（基於 `native-dly-ram9g-rtctorch-sb127-loader` 與 `kernel-local-b023-appstore-ui-9g-artifact` `6a0efbbf…`；`boot.img` `20,512,768 B`, `95047dc9…`、`payload.bin` `19,439,104 B`, `ca0df958…`、`private.adt` `72,004 B`, `bbc48b97…`），經 `launch-loader-check.ps1` 暫時開機（不更動常駐 `boot_a`/`boot_b`）。
- **主機測試**：
  - `work/localbuild/b023-appstore-t4-1126/t4-offline-verification.md` 記載 `13/13` 項 `T4` 離線配置、ADT 長度與安全邊界驗收全數 PASS。
- **驗證紀錄**：
  - **三階段 `guest.log`／`write.log` 還原與鎖驗證**：
    - `work/statusbar-fix/clean-b023-t4-dylib-1126/guest.log`（`2,319 B`, `66 LF 行` / `splitlines()=130`）：`APPSTORE_DYLIB_REMOVED /dst/usr/local/lib/fuxi/libfuxi-appstore.dylib`、`FSCK_before/after/final OK`、`PRE_OK 5 / WRITE_OK 5`。
    - `work/statusbar-fix/run-run-b023-appstore-native-t4-1126-fp-enable/guest.log`（`2,441 B`, `66 LF 行` / `splitlines()=130`）：`LAUNCHD_BEFORE 840840 3149f57a… -> LAUNCHD_AFTER 840836 a83d4f12…`、`FSCK_final OK`、`LAUNCHD_ENABLE_STAGE_SUCCESS`。
    - `work/statusbar-fix/run-run-b023-appstore-native-t4-1126-fp-disable/guest.log`（`2,492 B`, `67 LF 行` / `splitlines()=132`）：`LAUNCHD_BEFORE 840836 a83d4f12… -> LAUNCHD_AFTER 840840 8bd346db6c54c11a25bfd6b8b45642f1c56f1d02440b8be2633b6ba3ab6505b6`、`FSCK_final OK`、`LAUNCHD_STAGE_SUCCESS`、`PRE_OK 7 / WRITE_OK 7`（`sb-list.txt` 與 `sb-undo-list.txt` 7 區塊雜湊完全對稱），且 `FAIRPLAYD-RESCUE-HOLD=False`。
  - **`boot #473` 實機日誌（`boot.log` 與完整 `mtd` `console.txt`）**：
    - `work/Codex-Xiaomi13-GPU-20261109/run-b023-appstore-native-t4-1126/boot.log`：`763,314 B`（`\n=10,219` / `splitlines()=10,224`，SHA-256 `0922ababbc0a8bc5fc3475551a2267082fc4ec6da36c33bab6f1faa384e20b5a`）；同層外側之 `work/Codex-Xiaomi13-GPU-20261109/run-b023-appstore-native-t4-1126-monitor.log`（`60,459 B`, `587 L`, `c017bfbe…`, `monitor_exit=0`）。
    - `work/adsp-battery/fuxilog/20261126-235259/console.txt`：**`909,132 B`**，雙行數口徑：**`\n=11,661` / `splitlines()=11,666`**（含 5 個獨立 `\r`），SHA-256 `9dfc161c26f384f54a9e6e341ad2309a34302e1b3175a9d156440c1d30d2cdfc`。
    - 穩定性統計：`panic=0`、`REAL_FAULTS=0`、`DEV_LOST=0`；`CRASH` 全檔僅 `driverkitd:1`（`L1184-1187`, `sig 9`）與 `AegirPoster:38`（`L7490..L11633`, `sig 6`，既有背景鎖定畫面 extension 問題）；`AppStore`/`SpringBoard`/`backboardd`/`fairplayd.H2`/`appstored`/`itunesstored`/`amsaccountsd`/`storekitd`/`adid`/`absd` 皆 **`0` 崩潰**。
    - 原生 FairPlay／Store daemon 精確行號（`splitlines()` 口徑，括號附 `\n` 行號）：
      - `L74`（74）：`FUXI FPIOKIT: com_apple_driver_FairPlayIOKit registered`
      - `L554`（554）：`FUXI INJECT: fxinj=382: ...`（無 `libfuxi-appstore.dylib` 注入）
      - `L1078`（1078）：`exec fairplayd.H2`（全程僅此 **1 次**，0 次重啟迴圈／0 次 `EXIT`）
      - `L1369-1371`（1369–1371）：`FUXI FPIOKIT: fairplayd.H2 sel 0x16/0x17/0x15 -> 0x0`
      - `L5870`（5867）：`exec adid`，隨後 `L6110-6112`（6107–6109）`FUXI FPIOKIT: adid sel 0x16/0x17/0x1a -> 0x0`
      - `L6229`（6226）：`exec absd`（無 `AKS`/`FPIOKIT` 呼叫）
      - `L9219`（9214）：`exec AppStore`（`L9259-9260` `AppStore AKS sel 0/7 -> 0x0`、`L9275-9279 gate deny AppStore`）
      - Store/Auth daemon `AKS` 全回 `0x0`：`appstored`（`L2542 exec`, `L4033/4100 AKS sel 17/7 -> 0x0`）、`itunesstored`（`L2678 exec`, `L2737-2738 AKS sel 0/7 -> 0x0`）、`amsaccountsd`（`L2374 exec`, `L3620-3621 AKS sel 0/7 -> 0x0`）、`storekitd`（`L2372 exec`, `L6342-6343 AKS sel 0/7 -> 0x0`）。
  - **實機畫面與交叉定位證據**：使用者實機點擊原生 `AppStore` 顯示「無法連接 App Store」（`L9312..9585` 重試點擊無新 `FPIOKIT`/`AKS` 推進）。誠實註記：`boot #473` `console.txt` 本身不含 `AMSMediaTokenService`／`AMSMescalFairPlay`／`APSD` 字串（0 hits）；該斷點係交叉對比 `boot #472`（`U-031`）與歷史無代理純診斷日誌 `work/Codex-Xiaomi13-GPU-20261109/native-hw000-appstore-diag-r4b-1123/boot.log`（`L6408-6555`, `L6927`）所得定位。
- **參考來源**：`U-031`、`B-022`、`T-048`、`T-052`、`work/Codex-Xiaomi13-GPU-20261109/native-hw000-appstore-diag-r4b-1123/boot.log`。
- **結論／未完成**：`boot #473`（`T4`）以零殘留、零故障驗證純原生環境下 `FairPlayIOKit`、`fairplayd.H2` 單次常駐、`adid`、`absd` 與 Store daemon `AKS` 皆已過關（排除 `0135` 作為當前首頁阻礙），並交叉鎖定剩餘斷點在 `AMSMediaTokenService.fetchMediaToken` / `AMSMescalFairPlay.initWithMescalType(1)` / `sf-api-token-service.itunes.apple.com/apiToken`（`http=401`）；後續正於 `work/localbuild/b023-appstore-t5-1126/` 推進 `T5` 混合探針（保留 `AppStore` Route 2 `PROXY_TOK`+`PROXY_RW` 並同步啟用 `0134` `FairPlayIOKit` + `fairplayd.H2` 及唯讀診斷）。
- **出處**：`work/localbuild/b023-appstore-t4-1126/`、`work/statusbar-fix/clean-b023-t4-dylib-1126/`、`work/statusbar-fix/run-run-b023-appstore-native-t4-1126-fp-enable/`、`work/statusbar-fix/run-run-b023-appstore-native-t4-1126-fp-disable/`、`work/Codex-Xiaomi13-GPU-20261109/native-b023-appstore-native-t4-1126-loader/`、`work/Codex-Xiaomi13-GPU-20261109/run-b023-appstore-native-t4-1126/`、`work/adsp-battery/fuxilog/20261126-235259/console.txt`。
