# 雙系統引導驗證（2026-11-18）

## 常駐 a 槽選單（boot_a）

**2026-11-20 日常 a 槽已整合原生藍牙、關藍牙修補與 Safari 影片前置（B-013）。** `menu-daily-bt-20261120` 通過 454 項主機斷言，基底重連結逐位元相同，Android 核心與前版相同。09:58:45–10:00:02 RAM 試開，10 秒後自動進 Android；10:00:23–10:01:42 寫入並讀回整個 `boot_a`，再自動回 Android。交接位址 `0xa9210000`。前一版 `menu-daily-wifi-sb103-20261118` 的回退映像與雜湊在安裝報告。 [RAM 試開](../work/dualboot/daily-bt-ram-android-20261120/report.json) · [安裝與回退紀錄](../work/dualboot/persistent-slot-a-upgrade-daily-bt-20261120/report.json)

同一份 iOS payload 已在 09:46:51–09:57:37 暫時開機（趟 34）：SpringBoard、藍牙 3.2 Mbaud 就緒、bluetoothd 只執行一次且 0 次閃退、Wi-Fi 自動加入。10:47 接續核對確認使用者從常駐選單進 iOS（第 174 次開機）、SpringBoard、0 panic，設定頁關→開藍牙 5 輪 bluetoothd 同一個 pid、0 次閃退，再回 Android。 [常駐驗證](../work/dualboot/persistent-slot-a-validate-daily-bt-20261120-r2/report.json) · [趟 34 實機紀錄](../work/Codex-Xiaomi13-GPU-20261109/native-hw000bt-daily-1120/boot.log)

以下是 2026-11-18 的前一版：

**日常 a 槽已整合 Wi-Fi 修正、狀態欄前置鏡頭對齊和 10 秒 Android 倒數。** `menu-daily-wifi-sb103-20261118` 共通過 454 項主機斷言，15:39:47–15:41:14 RAM 試開，15:41:15–15:42:41 寫入並讀回整個 `boot_a`，再實測倒數自動回 Android。交接位址為 `0xa9210000`，a 槽 active／successful。前版完整回退映像與雜湊在安裝報告。 [RAM 試開](../work/dualboot/daily-wifi-sb103-ram-android-20261118/report.json) · [安裝與回退紀錄](../work/dualboot/persistent-slot-a-upgrade-wifi-sb103-20261118/report.json)

同一份 iOS payload 已在 15:34:09–15:39:05 暫時開機。設定頁時間中心 64 px、右側圖示中心 63.5 px。實機載入手動密碼框和熱點補查，在 5200／5745 MHz 加入成功並回 Android。使用者核對合併版正常。 [合併版實機紀錄](../work/Codex-Xiaomi13-GPU-20261109/native-hw000wifi-sb103-daily-1118/boot.log)

15:43 自動監看回 Android 時，前後的 mtd 雜湊一致。使用者後續從常駐選單手動進 iOS，確認日常版正常並同意合併發布。 [常駐驗證報告](../work/dualboot/persistent-slot-a-validate-wifi-sb103-20261118/report.json)

13:56 版的常駐選單已先驗證 10 秒倒數的 RAM 試開、寫入讀回及回 Android。 [前版安裝報告](../work/dualboot/persistent-slot-a-upgrade-countdown-20261118/report.json)

04:45 版的常駐選單為 `menu-launching-20261118`，Android、iOS、Recovery 三條路徑已驗過。boot_b 保留 `chain-v9-dualboot-launching` 作備援。KSU 模組「雙系統選單」於 04:44 移除。

| 趟次 | 時間 | 內容與結果 | 紀錄 |
| --- | --- | --- | --- |
| 首次安裝 | 03:50:09–03:52:21 | 把 `menu-partial-refresh-20261118`（`05e0318d…`）用 dd 寫進 boot_a，分區尾段保留原廠內容，寫後讀回相符。從 boot_a 選單選 Android，交接位址 `0xa9210000`。整個分區的雜湊為 `2bd9d7aa…2813` | `work/dualboot/persistent-slot-a-20261118/` |
| 首版 iOS／回程 | 03:52:51–04:06:05 | 原本的監看在使用者還在 iOS 時就逾時（270 秒），改由續跑模式接手。第 91 次開機有 exec SpringBoard、沒有 panic，回 Android `0xa9210000`。Recovery 那段因使用者直接選了 Android 而逾時，不算失敗 | `persistent-slot-a-validation-20261118/`、`persistent-slot-a-validation-resume2-20261118/` |
| 新版 RAM 試開 | 04:11:23–04:12:45 | `menu-launching-20261118` 暫時載入。使用者選 Android，確認下方提示換成「進入系統中」。交接位址 `0xa9210000`，兩槽 boot 不變 | `launching-ram-validation-20261118/` |
| 升級寫入 | 04:13:05–04:14:26 | 先確認 boot_a 等於首版，再寫入新版並讀回（04:13:25）。重開後選 Android，04:14:26 核對通過。整個分區新雜湊為 `275dac0f…b4b1`。回退映像 `boot-a-previous-menu.img` 由舊版加原廠尾段重組，雜湊等於 `2bd9d7aa…` | `persistent-slot-a-upgrade-launching-20261118/` |
| 新版驗收 | 04:14:55–04:19:58 | 從 boot_a 選單進 iOS（第 92 次開機，exec SpringBoard、無 panic），再回 Android `0xa9210000`。之後從「其他選項 → Recovery」到 TWRP `3.7.1_12-Xiaomi13_v9.0_A15`，再回 Android `0xa9210000`。使用者確認全程正常 | `persistent-slot-a-validation-launching-20261118/` |
| b 槽換新版 | 04:26:29 | 把 `chain-v9-dualboot-launching`（同一份 `7977fbf0…` 選單）寫進 boot_b，KSU 模組更新為 v9，讀回相符，boot_a 不變 | 還原點 `fuxi_Backup/boot_b-chain-v8-dualboot-wifi-before-chain-v9-dualboot-launching-20261118-042629` |
| b 槽 iOS | 04:27:21 起 | KSU v9 動作切 b 槽重開，從 b 槽選單進 iOS。獨立開機沒有 USB，監看逾時；之後從 mtd 取回第 93 次開機紀錄：exec SpringBoard、無 panic。使用者確認進入 iOS，經 TWRP 切回 a 槽 | `daily-v9-installed-user-20261118/`、console 在 `remove-ksu-module-20261118/previous-console.txt` |
| 移除 KSU 模組 | 04:44:45–04:44:58 | 模組沒有開機腳本，只剩「執行」按鈕，使用者要求移除。先逐檔核對並封存（tar `4cb1cb09…2b79`），用電腦端工具做一次 iOS 根映像區塊表與 pin 檢查，再刪除並確認已不在。不寫分區、不重開、不切槽 | `remove-ksu-module-20261118/`，封存在 `fuxi_Backup/ksu-module-remove-ksu-module-20261118/` |

「進入系統中」的修改：使用者回報選擇系統後，畫面會停約 10 秒像沒反應。現在按下確認後，`menu_paint_status` 會把下方按鍵提示與 notice（y=1820–2144）換成「進入系統中」，送出一幀、等 20 ms 再交接，標題和按鈕保留到所選系統接手面板。沒有加計時。444 項主機斷言通過，載入器重連結逐位元相符，payload、核心與 Android 雜湊和上一版相同。boot.img 的 SHA256 為 `7977fbf0c8813ec16d76608bd24b1a930dda769fc1b2a8ed06c690fe013a2773`（66326528 bytes），前後對照圖在 `work/dualboot/menu-launching-20261118/host/compare-launching.png`。

首次安裝使用 `launch-loader-check.ps1 -InstallPersistent`。升級用 `-UpgradePersistent -InstallReport <目前安裝紀錄>`。驗收用 `-ValidatePersistent -InstallReport <安裝紀錄>`，可加上 `-IosOnly` 驗證 iOS 和 Android 回程，或用 `-ResumeAfterIos <前一趟驗證紀錄>` 接續手動試開後的回程。安裝接受 `install-persistent.py` 中已登記且通過 RAM 試開 Android 的映像。等待 iOS 回程為 15 分鐘，續跑為 30 分鐘。若要手動選 iOS 或 Recovery，先在倒數結束前按音量鍵。

**尚未驗收**：其他選項的 Fastboot、重新啟動、關機；完全關機後的冷開機；ABL 重試與回退。b 槽選 Android 仍會循環（b 槽的 system_b 等邏輯分區是空的），不修；不切到 b 槽就不會遇到。

**KSU 模組移除後的用法與工具**：開 iOS＝重開，在選單用音量鍵選 iOS、短按電源確認。模組原本做的檢查改由電腦端執行：`phone-check.py` 的 `verify_ios_root()` 把 `work/ksu-ios-entry/fuxi_ios_boot/bin/` 的 `inspect-fiemap`、`check-pin` 暫時推到手機跑一次，核對 `expected-fiemap.txt` 後刪除；TWRP 雜湊也讀同一資料夾。`-StartDailyChain` 改為檢查 a 槽、boot_a 選單、TWRP、iOS 根映像並保存上一趟 mtd 後直接重開進選單。`-DeployChain` 會重裝模組，已停用：模組不在時會在拿鎖前停下。`-DualBootCheck removeksu` 是這次的移除步驟，模組已不在時只回報 `ALREADY_ABSENT`。要恢復模組：把封存的 tar 解回 `/data/adb/modules` 後重開，或在 KSU 安裝 `work/ksu-ios-entry` 的 zip。

## 選單刷新與 Android 開機時間

`menu-partial-refresh-20261118` 通過 426 項主機檢查。移動焦點只更新相關外框和箭頭，切換頁面保留共用標題。新版在 a 槽暫時載入，使用者確認閃爍消失。實機由選單交接 Android 核心 `0xa9210000`，回程仍在 a 槽，兩槽 boot 雜湊前後一致。見 `work/dualboot/partial-refresh-ram-validation-20261118/`。

以 Android 核心啟動為起點，選單交接和 a 槽直接啟動的動畫開始時間分別為 28.520 秒、28.565 秒。兩次 `mount_all.early` 都約 20.05 秒。直接開機的日誌顯示系統於第 22.779 秒跳過缺少的 `/mnt/vendor/mi_product`，符合 [AOSP 等待掛載來源 20 秒的邏輯](https://android.googlesource.com/platform/system/core/+/refs/heads/main/fs_mgr/fs_mgr.cpp#1551)。日誌分別在 `work/dualboot/stock-android-timing-20261118/` 和 `work/dualboot/android-mi-product-check-20261118/`。手機目前在 a 槽 Android，新選單仍為暫時載入產物。

直接開機的日誌另顯示 `system_b`、`vendor_b` 等邏輯分區長度為零。若以 b 槽的啟動參數交接 Android，缺少這些分區可解釋先前的開機循環。未用 b 槽再現此結果。

## Recovery 入口與回程

03:30:06–03:33:10 在原 Android a 槽暫時載入 `menu-recovery-20261118`。第一次從選單啟動 Android，核心位址為 `0xa9210000`，符合內嵌 Linux 的執行位置。第二次選 Recovery，ADB 核對到 TWRP `3.7.1_12-Xiaomi13_v9.0_A15`，並重啟回 a 槽 Android。兩槽 boot 與 Recovery 的雜湊前後一致，手機鎖已釋放。紀錄在 `work/dualboot/recovery-ram-validation-20261118/report.json`。

實機載入前唯讀核對日常 `chain-v8-dualboot-wifi` 並備份目前完整 boot_b。兩版的 Android 核心和 iOS payload 雜湊相符。備份與槽位盤點在 `work/dualboot/daily-recovery-preflight-20261118/`。

日常 v8 從 b 槽選 Android 曾回到選單循環。03:24 經 Fastboot 切回 a 槽，驗證 Android 啟動與原 `boot_a` 雜湊，紀錄在 `work/dualboot/android-rescue-20261118-0323/report.json`。b 槽循環原因與每次開機進入選單仍待確認。

## 日常 v8 合併與安裝

2026-11-18 03:14:38 已將 `menu-direct-launch-20261118` 同一份映像安裝為 `chain-v8-dualboot-wifi`（boot_b、KSU v8），寫後讀回相符，boot_a 雜湊保留。32項部署恢復測試通過；完整舊 boot_b 與模組還原點：

`D:/xiaomi13/fuxi_Backup/boot_b-chain-v7-ram9g-rtctorch-before-chain-v8-dualboot-wifi-20261118-031406`。

03:10:44–03:13:42 先完成同映像的 RAM iOS 啟動，exec SpringBoard、原生掃描、join finished 0 均出現，回 Android 後 boot 分區雜湊未變。USB 監看因回程重置消失而退出，Android 回程由獨立檢查核對成功；不把該退出當核心失敗，也不宣稱零斷線。

合併比較及安裝 receipt：`work/dualboot/daily-merge-20261118/`。安裝後透過 KSU 真正切 b 槽的開機正在另行擷取；未將 b 槽 Android、Recovery／其他選項及冷啟動視為通過。

以下保留各版本原始驗證範圍。

## 最新：三按鈕直接啟動

來源是目前 dualboot/，產物為 work/dualboot/menu-direct-launch-20261118/。

主標 dualboot、副標 for fuxi；首頁只有 Android、iOS、其他選項。音量鍵移動焦點，短按電源直接執行。主機測試驗證三個焦點、直接啟動與返回，ARM64 建置／重定位／映像完整性結果見該目錄 report.json。這版已於02:57:26以RAM暫時開機；使用者選Android，03:00:51唯讀核對核心位址0xa9210000、boot雜湊／槽位不變、unmapped及釋鎖。新版iOS未測，未安裝為日常開機入口。iOS專用監看器因使用者選Android而逾時，不列為Android失敗；完成證據見 work/dualboot/dualboot-direct-return-20261118-0301/report.json。

修改前的程式還原點：work/dualboot/before-direct-launch-20261118/。載入核心、Linux 交接、按鍵驅動與顯示接線沒有在這次介面修改中變動。

## 已完成實機：四按鈕版本

確切產物 work/dualboot/menu-device-20261118-r2/boot.img，66326528 bytes，SHA256：

7dcb3c9cf9b92bae63ce9cefa4cff979f7a80c32ed9398ffde9b96164f40b5ad

| 驗證 | 結果與證據 |
| --- | --- |
| 主機測試 | WSL clang ASan／UBSan，380 assertions；815061-byte 當前 FDT、12GB RAM guard 重播通過 |
| 選單與實體按鍵 | 使用者確認黑白選單顯示，音量／電源鍵可用 |
| Android 交接 | 02:35:24 暫時載入、02:36:39 核對完成；核心代碼位址 0xa9210000 在內嵌 Linux 範圍，符合新引導器交接 |
| iOS 交接 | 02:38:13 暫時開機；使用者確認進入 iOS 鎖定／主畫面；console 有 exec SpringBoard，無 panic(cpu |
| 回程 | 02:42:32 已回 Android a 槽、active slot 0、unmapped、兩把工作鎖釋放 |
| 寫入範圍 | boot_a／boot_b 雜湊前後相同，未刷 boot、切槽或更新 KSU 模組。iOS 正常執行仍會更新其檔案系統與持久日誌 |
| 截圖 | 取得 1080×1200 裁切 PNG，10.2秒／0 重連，但畫面全黑，未當作視覺驗收；畫面結果依使用者實際觀察 |

主機 JSON：work/dualboot/validation-result-20261118.json。個別執行與使用者觀察：work/dualboot/dualboot-android-20261118-0235/、work/dualboot/dualboot-ios-20261118-0237/。

iOS console：work/Codex-Xiaomi13-GPU-20261109/dualboot-ios-20261118-0237/boot.log，SHA256：

a3e0eda6feed0b50d3ca9d919295f63fdb1c0ca41638632261de7d95f3c9f0c3

核心沿用先前已上機的本機 kernel-local-wifiux2-9g-artifact；只在本機更新校時及 loader／menu，不是新建核心 CI。

## 還原點與限制

還原點：D:/xiaomi13/fuxi_Backup/dualboot-before-validation-20261118-014941。已保存原始碼、14 個分區、KSU 模組及 iOS 的 1229 個 4MiB 差異區塊。基底＋差異的整份原始映射 SHA256 已重新核對相同：

aad481888dea14cf829b40bbb32ba3118f6469971b6c0f9b1f8534bdfb91d519

（以下為 RAM 試驗當時的範圍；常駐 boot_a 的現況見本檔開頭。）常駐引導當時尚未安裝，正常開機仍走原本 Android。b 槽正常啟動 Android、Recovery 繞過選單、其他選項的 Fastboot／重啟／關機，以及安裝後冷啟動／ABL 重試與回退，均未在本次試驗驗收。

使用者回報確認Android後選單停留數秒才出現Android開機logo。程式確認後直接交接Linux，沒有新增秒級延遲；舊選單framebuffer在Android更新畫面前保留。各階段時間尚未量測，未宣稱效能退步或已定位特定Android元件耗時。
