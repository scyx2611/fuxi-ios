本診斷針對 Xiaomi 13 (fuxi, Adreno 740) 移植 iOS 16 環境下，`backboardd` 調用私有 `newTextureWithBytesNoCopy`（PixelFormat 204, 1125x2436, 2752512 bytes）的 V3 provenance patch 進行本地靜態審查。

---

### 一、已確認的阻礙 (Established Blocker) vs 不確定性 (Uncertainty)

1. **已確認的阻礙 (Established Blocker)**
   - **格式拒絕機制攔截**：`NativeBridgeMetalFrontend.mm:1379-1382` 攔截了 `backboardd` 請求建立的私有紋理（`pixelFormat: 204`、`length: 2752512`、`deallocator: nil`），Runtime 因無法識別 Apple 私有底層紋理佈局（proprietary/micro-tiled layout）而拒絕建立，直接觸發後續 CPU fallback 或 respawn。
   - **尺寸與長度非線性對應**：$1125 \times 2436 = 2,740,500$ 像素，若為常見的 32-bit (4 bpp) 像素長度應近 11 MB；而傳入的 `length: 2752512`（剛好為 $0\text{x}2\text{A}0000$ 字節，672 個 4KB 頁），$2752512 / 2436 \approx 1129.93$，並非整數的 row bytes。
   - **V2 探測全面失效**：V2 在 generic identity 與 safe read 階段失敗，無法確認物件來源；V3 雖細分 11 種狀態，但尚未在實機啟動驗證。

2. **不確定性 (Uncertainty)**
   - **底層記憶體佈局未知**：完全未知該緩衝區是 Apple 專有的 Micro-tiled、壓縮封裝、YUV 平面還是某種特殊的 Render Target / Scanout 格式。
   - **呼叫者物件結構與生命週期未知**：該指針的持有者、其生命週期歸屬，以及由哪個 framework 在何時 free/reuse 完全不透明。

---

### 二、提供的診斷程式碼之邏輯風險 (Diagnostic Logic Risk)

檢視 `FuxiNocopyObservation.h` 及 `NocopyObjectMetadata.h`，存在以下主要脆弱點與診斷風險：

1. **Unwind 暫存器推導假設極度脆弱 (`FuxiNocopyObservation.h:22`)**
   - 程式直接透過 `_Unwind_GetGR(context, 19)` 提取 `x19`。依據 ARM64 ABI，`x19` 為 Callee-Saved 暫存器；DWARF/Compact Unwind 僅在該 frame 的 unwind table 明確標記並還原 `x19` 時才能取得有效值。
   - 更嚴重的是，即使暫存器值被還原，該暫存器在 `PC` 當下是否必定存放該 Metadata 物件指針完全取決於編譯器暫存器分配，不能作為確定物件位址的可靠依據。
2. **寫死返回偏移量與 Image UUID (`FuxiNocopyObservation.h:19, 53` 與 `NocopyObjectMetadata.h:20, 24`)**
   - 程式寫死了 `returnOffset == 0x200000` 與特定的 QuartzCore UUID `{0x9e, 0x40, ...}`。
   - 在不同 dyld shared cache 建構版本、小版本更新或不同的 slide/ASLR 下，該偏移量極大概率不吻合，會直接導致 `matchingFrames == 0`，使診斷在實機上永遠回傳 `FuxiNocopyStatus::FrameCount` (2) 或 `Identity` (4)。
3. **物件內部偏移硬編碼 (`NocopyObjectMetadata.h:47-51`)**
   - 程式寫死讀取 `0x10` (width), `0x14` (height), `0x58` (actualBytes), `0x60` (claimed), `0x68` (cleanup), `0xa8` (flags)。
   - 若物件非預期的具體結構體，此類偏移量讀取將直接造成 `PointerMismatch` (9) 或 `DimensionMismatch` (10)，無法獲得有意義的 metadata。
4. **Dyld/Mach-O 解析依賴**
   - `fuxiImageUUID` (`FuxiNocopyObservation.h:34-42`) 在每次捕捉到目標 frame 時動態 `vm_read_overwrite` 並 `malloc` 解析 Load Command；若目標 image 位於 shared cache 的非獨立映射區塊，直接讀取 `image` 頭部可能失真。

---

### 三、有界限的下一步實驗 (Bounded Next Experiment)

在**不寫入實機、不變更現有 refusal gate** 的前提下，嚴格局限於「被動只讀日誌驗證」：

1. **驗證 V3 Status 分類輸出**
   - 將已編寫的 V3 patch 佈署至測試建構，觀察 real `backboardd` 觸發時印出的 `FuxiNocopyStatus` 列舉值（`validation=%u`，`NativeBridgeMetalFrontend.mm:1394`）。
2. **判定失效階段**
   - 若回傳值為 `FrameCount` (2) 或 `Identity` (4)：證明硬編碼的 `0x200000` 偏移量或 UUID 假設失效。下一步僅需記錄實際的 `pc - info.dli_fbase` 與 dylib 名稱，不進行硬編碼斷言。
   - 若回傳值為 `PointerMismatch` (9)：確認呼叫者確為該 dylib，但 `x19` 暫存器所指非該 descriptor 封裝物件。
3. **實驗邊界**
   - 保持 `newTextureWithBytesNoCopy` 現有的拒絕回傳（保持 gate logs next-respawn CPU），不分配紋理，不修改任何記憶體頁面屬性。

---

### 四、Staging Copy / Normal Upload 或 Producer-Side 線性路徑可行性分析

**結論：在現階段技術前提下不可行。**

必須滿足的前提條件及其現狀分析如下：

1. **佈局前提 (Layout Prerequisite) —— 致命阻礙**
   - 題目明確指出：**不得由整數長度、nil deallocator、ASTC 測試或私有標誌推斷為 Linear 佈局**。
   - Adreno 740 硬體紋理單元僅支援線性佈局、Qualcomm 專有 UBWC (Universal Bandwidth Compression) 或標準壓縮格式（如 ASTC/ETC）。
   - 若 Apple 端的 producer 寫入的是 Apple GPU 專屬的 micro-tiling 排列，無論採用 Staging Copy（`memcpy`）還是常規紋理上傳，複製出來的資料對 Adreno 而言均為亂碼（導致嚴重的視覺損毀或破圖）。在未有確定逆向出的 detile 演算法前，直接進行拷貝或上傳毫無意義。
2. **生命週期前提 (Lifetime Prerequisite)**
   - API 調用給定 `deallocator = nil`，意味著該記憶體的釋放與覆寫完全由呼叫者外部機制隱式掌控。
   - 若實施非同步的 GPU staging copy，在 GPU command buffer 實際排程執行讀取前，producer 隨時可能在下一幀刷新時覆寫或取消映射該記憶體區塊，引發 Data Race 或 Use-After-Free。若要進行 staging，必須在 CPU 調用返回前完成同步阻塞式 `memcpy`。
3. **同步前提 (Synchronization Prerequisite)**
   - 該調用未附帶任何 `MTLEvent`、`MTLFence` 或回呼信號。
   - Producer 何時完成該記憶體區塊的 CPU/硬體快取刷寫（Cache Clean/Invalidate），以及 Consumer (Adreno) 何時可以安全讀取，完全缺乏跨進程/跨子系統的同步依據。

**總結**：目前 runtime 維持拒絕未知 raw texture layout 是唯一正確且安全的架構決策；在解除佈局與呼叫端物件真實身份的不確定性前，不應嘗試導入 staging copy 或強制視為線性紋理。