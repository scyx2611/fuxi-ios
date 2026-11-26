**小米 13 GPU 適配調查，2026-11-11**

目前最先阻斷桌面 GPU 合成的是 NativeMetal 的私有貼圖匯入介面。實機已有基本 GPU 渲染能力，但 backboardd 呼叫 `newTextureWithBytesNoCopy:length:descriptor:deallocator:` 時被拒絕，Fuxi 的保護機制隨後讓它以 CPU 模式重啟。優先工作是補齊「生產端 layout 協定＋消費端貼圖匯入」並驗證兩端一致，接著才有條件量測持續桌面效能。

這是依保存的實機日誌、目前本機原始碼及本次查閱的上游原始碼得出的結論。本次沒有啟動手機、修改映像或執行新的實機測試，不能據此宣稱當下手機狀態或 GPU 桌面已修好。

| 已核對的證據 | 可以建立的結論 | 限制 |
| --- | --- | --- |
| [實機 smoke 日誌](E:/Codex-Xiaomi13-GPU-20261109/native-caller-astc-smoke-boot-1/boot.log:1447) 的 stages 0–7，尤其 1776 行 ASTC PASS | 裝置探索、buffer、三角形、IOSurface、圖層合成及明確上傳的 ASTC 像素路徑可用 | 離屏測試，未涵蓋桌面這筆原始記憶體 |
| [桌面 survey](E:/Codex-Xiaomi13-GPU-20261109/native-caller-survey-boot-1/boot.log:2708) | 拒絕項為 format 204、1125×2436、1 mip、2,752,512 bytes、nil deallocator | format 204 是標準 ASTC 4×4 LDR；未知的是原始記憶體排列與來源 |
| 同一日誌 3133、3140、3165 行 | GPU frame 計數停在 22，58 次 refusal，最後 next-respawn=CPU | 短暫 GPU frame 不等於流暢桌面；日誌中的 hz 也不能當作穩態效能 |
| [V2 survey](E:/Codex-Xiaomi13-GPU-20261109/native-provenance-survey-boot-1/boot.log:2707) | metadata 驗證失敗；2715 行記錄 CPU fallback | 泛用錯誤不能確定是哪個 identity、unwind、讀取或欄位檢查失敗 |
| [目前操作紀錄](%USERPROFILE%/Documents/Codex/xiaomi13-ios-gpu-20261108/continuation.md) 最後記載 | V3 已建置與寫入，待取得 validation code | 沒有 V3 實機啟動結果；V3 本身只改善診斷，沒有實作匯入 |

**已經存在的可參考實作**

[UniXNU/AdrenoMetal 的 4d789e9c 提交](https://github.com/UniXNU/AdrenoMetal/commit/4d789e9c369c8daff06854432f5328ad515a1058) 同時加入以下配套：

1. `DVMLinearTextureLayout`：定義壓縮區塊逐列排列、mip/slice offset、size、alignment、初始化、雙向 copy 及 finalize。
2. `watermark`：以 layout 身分區分快取資料。
3. `newTextureLayoutWithDescriptor:isHeapOrBufferBacked:`：讓 ImageIO/QuartzCore 有機會依我們提供的排列產生資料。
4. `newTextureWithBytesNoCopy:...`：建立普通 texture，再透過 `replaceRegion` 上傳每個 mip/slice，並保留 deallocator。

這提供一條具體修復路線：讓生產端先依自訂 layout 輸出 ASTC，再由消費端讀取相同排列。這個上游實作在內部會複製上傳；需驗證這筆唯讀素材是否允許快照語義，以及使用期間是否還會修改或共用來源。

本次固定檢查的上游來源為 [54ac1a00 的 frontend](https://github.com/UniXNU/AdrenoMetal/blob/54ac1a006d274a9b8ca66b13d1db2f5d09e92900/NativeBridgeMetalFrontend.mm#L3989)，layout 位於 3989–4110 行，兩個入口位於 4383–4404 行。對應原始碼已存於本研究目錄的 sources。

我們的映像建置固定在 `NATIVEMETAL_SHA=a96feceda55e8fed1e20c0a2998736cef7aab006`，缺少上述兩個入口的實作。GitHub compare 顯示，layout 引入提交相對該 pin 已前進 58 個提交，涉及 frontend、bridge 與 runtime patch；因此適合先抽出必要配套回移植，整套升級會擴大 iOS 16 ABI 與既有補丁的驗證範圍。

另一份本機 checkout [AdrenoMetal frontend](D:/xiaomi13/fuxi_ios/UniXNU/drivers/AdrenoMetal/NativeBridgeMetalFrontend.mm:3744) 已經有這套 layout，該 checkout 本次讀到 HEAD `a722f998f0d2db79bc111ac5a6d29570b6e6e703`。所以「本機有新版程式碼」與「映像使用新版程式碼」是兩件要分別核對的事。

先前的 [layout 調查](E:/Codex-Xiaomi13-GPU-20261109/nocopy-layout-investigation.md) 已記下相同候選方向。本次補充的是上游具體提交、目前 pin 的差距、現有測試範圍及移植陷阱。

**為何這條路適合 iOS 16，以及還缺什麼**

保存的同版 ImageIO/QuartzCore 反組譯存在實際 layout 呼叫點：

- QuartzCore producer 會取得 layout，使用 size/alignment、initialize、copyFromLinearBytes 與 finalize。
- ImageIO 會呼叫 watermark；其中一條路徑比較輸入 metadata 與目前 layout 的兩個 64-bit 身分值。
- 動態堆疊已對到 QuartzCore +0x200000 的 no-copy 消費點。
- no-copy 回傳 nil 後，這個消費點不會重試旁邊的普通 upload 分支。

這些證據支持協定配對的可行性，但尚未證明「目前失敗的 allocation」由哪條 producer 或快取路徑產生。證據見 [原有調查](E:/Codex-Xiaomi13-GPU-20261109/nocopy-layout-investigation.md)、[消費點反組譯](E:/Codex-Xiaomi13-GPU-20261109/nocopy-quartz-function-disassembly.txt:170) 及 nocopy-imageio-layout.log、nocopy-watermark-xrefs.log。

尺寸計算為 ceil(1125/4) × ceil(2436/4) × 16 = 2,747,808 bytes，向上對齊 16 KiB 得 2,752,512 bytes。它與實機 length 相符，但無法據此判定區塊順序、mip 排列、所有權或同步方式。

移植前要解決三項具體問題：

- **資訊日誌會被誤判故障。** 上游 layoutWithDescriptor 第一次成功建立時呼叫 NBLOG；我們的 [safety-hooks 補丁](%USERPROFILE%/Documents/Codex/xiaomi13-ios-gpu-20261108/ios-native-lf/tools/patches/nativemetal-fuxi-0002-safety-hooks.patch:31) 讓 noteProblem 呼叫 fuxiFailure，最後進入 [failureLog](%USERPROFILE%/Documents/Codex/xiaomi13-ios-gpu-20261108/ios-native-lf/tools/fuxi-metal/Frontend.mm:240) 並 fault 桌面 gate。成功資訊應使用普通日誌，真實拒絕仍保留故障處理。
- **消費入口沒有自行證明來源 layout。** 這份上游程式檢查 format、pointer 與 length，沒有在入口驗證這個 pointer 的資料曾經通過自訂 layout producer 或 watermark。回移植需要驗證來源和快取處理，不能直接放行現有未知 bytes。
- **測試範圍有限。** [上游 regression](%USERPROFILE%/Documents/Codex/xiaomi13-gpu-research-20261111/sources/upstream-interface_regressions_host.mm:148) 涵蓋單一 4×4 ASTC block、length=0 推導、byte readback 與 deallocator。該段測試未涵蓋實際 1125×2436、ImageIO producer、舊 cache、來源修改或實機桌面。本次只檢查測試碼，沒有執行它。

**其他方案的適用程度**

| 來源 | 可以借用的思路 | 與本問題的距離 |
| --- | --- | --- |
| [Darling Indium texture.cpp](https://github.com/darlinghq/indium/blob/main/src/indium/texture.cpp) | staging buffer、明確 row pitch、Vulkan buffer-to-image copy；DMA-BUF 匯入需明確 linear modifier | 可參考上傳與同步；沒有替這筆 Apple raw texture 提供 layout 證明 |
| [另一個 fuxi 的 main 分支](https://github.com/ljhhkx529/unixnu-fuxi/blob/050e35b862ff9cabee4dac56f28c5f867dbc6f6b/README.md) | 同型號已有 iPadOS 17.7.11 GPU UI 合成的作者紀錄；適合對照 OS、driver、display 配套 | 作者仍列 GPU 為部分支援，缺 panel-vblank 與 DCVS；不是本機 iOS 16 的直接驗證 |
| [小米 Pad 6 pipa](https://github.com/xVanTuring/UniXNU-pipa/blob/main/README.md) | 作者紀錄分開量測 render、commit-to-complete、scanout copy、CPU cluster；sysmem/GMEM 做對照 | 可借用後續效能診斷；A650 寄存器修復不能直接套到已通過像素 smoke 的 A740 |
| [Mesa Asahi layout 文件](https://docs.mesa3d.org/drivers/asahi.html) | Apple layout 的 tile、block 與排列模型可供備選研究 | 必須先確認 allocation 確實符合該模型，現階段無法選定 detiler |
| [KosmicKrisp](https://docs.mesa3d.org/drivers/kosmickrisp.html) | Vulkan 疊在 Metal 上的架構參考 | 翻譯方向與需求相反，不能替代這裡的 Metal→Vulkan frontend |

**建議執行順序**

1. 在隔離 source checkout 回移植 layout producer、watermark、雙向 block copy、消費入口及 owner 管理；沿用既有 iOS 16 ABI、completion 與 copy-span 修正，修正成功日誌的故障語義。
2. 用已知 ASTC fixtures 驗證完整 producer→memory→consumer→GPU sampling，覆蓋 1125×2436 的部分末列/末行、mip、短輸入、整數邊界、錯誤 layout、來源修改及 owner/in-flight 釋放；測試真實拒絕仍觸發 gate。
3. 以有界 metadata 記錄確認實際 ImageIO/QuartzCore 使用自訂 layout，核對 watermark 不符時是否重建或轉換資料。V3 validation code 可協助辨識舊路徑，但不應讓它成為唯一修復方向。
4. 再驗收持續桌面 GPU frames、refusal 數、CPU respawn、像素及實體螢幕/觸控；相同操作至少跑一段持續量測，分開冷啟動與暖機後資料。
5. 若合成持續但仍卡，分別量測 shader compile、CPU encoding、GPU submit/retire、scanout copy 與 frame pacing，再決定 cache、排程或 display 優化。

這條路的成功判準是持續桌面合成與正確畫面。僅編譯通過、V3 印出 metadata、或 ASTC smoke 再次通過，都還不構成完成。

**委派與核對紀錄**

本次兩個靜態審查工作透過本機 Magpie 呼叫，請求與回傳 model 均為 `antigravity/gemini-3.8-flash`。原始回答與 JSON 保留在本目錄。主代理已核對來源，未採用以下推論：ASLR 會改變 image-relative offset、format 204 可能是 YUV、V3 某一錯誤碼可唯一判定根因、尚未證實的 3D layout bug，以及失敗建立一定必須呼叫 deallocator。它們都超出目前證據。

本次沒有新增測試結果。source-manifest.json 記錄來源快照；evidence-excerpts.json 記錄本次從保存日誌核對的關鍵行。

