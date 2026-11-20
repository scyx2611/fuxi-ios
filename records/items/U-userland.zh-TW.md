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
- **結論／未完成**：試裝配置的原生震動路徑已能驅動馬達。使用者兩次回報所有場景都有震感，但手感像轉子拖尾；起振加強的 mode 4 實機亦未通過手感驗收。11-20 使用者回報「有些地方沒震、震感不像 iOS」，因此加了 mode 6 觸感整形（預設關，見上）。趟 30 實機已驗到短觸感改播原廠點按（VMAX 依 level 調整）、長觸感照舊走 DIRECT，這兩項通過；使用者手感驗收待做。
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
- **狀態**：已實機驗證（趟 32／33：設定頁關→開兩次無閃退，電源循環後 Windows SDP 成功）。只在測試 loader，日常 boot_a 未改。
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
    - 併入日常 loader 需要另外部署，日常 boot_a 目前未改。
    - 日常使用時可以考慮關掉 `--bt-oslog`；`--bt-fix` 不需要它。
- **出處**：K-077 趟 18 第 15028 行（關藍牙閃退初次記錄，趟 16 第 11192 行也有）。
