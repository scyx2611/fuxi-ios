# 小米 13 雙系統引導選單

2026-11-20 10:01 起，日常 a 槽使用 `menu-daily-bt-20261120`。這版在 B-012 的前置鏡頭對齊與 Wi-Fi 修正之外，加入原生藍牙、關藍牙閃退修補（U-023）與 Safari 非 DRM 影片前置（`fuxi,mfb-align`）。10:47 已確認從常駐選單進 iOS、關→開藍牙 5 輪並回 Android；前一版 `menu-daily-wifi-sb103-20261118` 的回退映像在 `work/dualboot/persistent-slot-a-upgrade-daily-bt-20261120/`。

2026-11-18 15:42 起，日常 a 槽使用 `menu-daily-wifi-sb103-20261118`，已整合前置鏡頭對齊及 Wi-Fi 修正。選單顯示倒數，10 秒內無按鍵即自動進 Android。按音量鍵可取消倒數、移動焦點，再短按電源鍵確認。 [安裝驗證](VALIDATION.zh-TW.md)

2026-11-18 04:45：**KSU 模組「雙系統選單」已移除**。boot_a 常駐選單後，一般重開就會進選單，開 iOS＝重開後選 iOS；模組只剩重開按鈕。移除前已封存（`D:\xiaomi13\fuxi_Backup\ksu-module-remove-ksu-module-20261118\`），原本的 iOS 根映像檢查改由電腦端工具執行。boot_b 已換成同一份選單（`chain-v9-dualboot-launching`）只當備援；b 槽選 Android 會循環，不要切到 b 槽。細節見 `VALIDATION.zh-TW.md` 開頭。

2026-11-18 04:20：**boot_a 已常駐 `menu-launching-20261118`**。Android 重開與 iOS 重開都會先進選單；選定後下方提示改成「進入系統中」，直到所選系統接手畫面。安裝後已驗 Android、iOS、回程與 Recovery（TWRP），細節與回退映像見 `VALIDATION.zh-TW.md` 開頭。以下 03:14 的 boot_b 段落是當時 KSU 切 b 槽的路徑，已被 04:45 的狀態取代。

2026-11-18 03:14：**日常 `chain-v8-dualboot-wifi` 已安裝 boot_b，KSU 更新為「雙系統選單」v8**。boot_a 保留原本 Android；從 KSU「執行」通過檢查後切到 b 槽進入選單。候選包含回退後的原生 Wi-Fi（ux=31）、chain-v7 調校及現有 iOS root，完整比較與還原點見 `work/dualboot/daily-merge-20261118/`。最新三按鈕映像已完成 a 槽 RAM 啟動 Android 與 iOS 驗證；安裝後獨立開機另記 `VALIDATION.zh-TW.md`。

選單為黑底白字，首頁與其他選項都顯示主標 **dualboot**、副標 **for fuxi**。首頁上下排列 **Android**、**iOS**、**其他選項** 三個按鈕。其他選項由上到下為返回、Fastboot、Recovery、重新啟動、關機。

音量上／下移動焦點；短按電源鍵直接啟動焦點所在的 Android／iOS，或進入其他選項。沒有額外啟動按鈕。焦點以粗白框與箭頭表示。開機時尚未放開的按鍵、接點彈跳和組合鍵不會誤觸啟動；短按電源在放開後執行，不改變 PMIC 原有的長按重置設定。

目前常駐版本及預覽位於 `work/dualboot/menu-daily-bt-20261120/`。

預設選擇 Android 並倒數 10 秒。建置時可用 `--timeout` 指定 1–60 秒，或以 `--timeout 0` 等待手動選擇。任何實體按鍵都會取消當次倒數。`--default ios` 只改初始焦點，倒數到期仍啟動 Android。

## 實作與參考

實作先查閱現有 `work/boot-chain`、`ios-fuxi/shim`，再查本機 UniXNU、UniXNU-sheng 及使用者指定專案。

| 來源 | 採用方式 |
| --- | --- |
| 本機 `work/boot-chain/chain-v7-ram9g-rtctorch`、`build-chain.py` | 復用已保存的入口、loader、console、probe、GPU 韌體與 payload 物件、Linux 核心及鏈接配置；不用新 XNU 核心 |
| 本機 `ios-fuxi/shim/fuxi-loader/chain.c` | 復用 Recovery 分流原則；重新整理成有邊界檢查、可在主機測試的 FDT／bootconfig 解析器 |
| 本機 `ios-fuxi/shim/console.c`、`fuxi-probe/probe.c` | 復用已量測的 1080×2400／4320-byte stride／0xb8000000 splash 檢查，保留 autorefresh 時不重複 START 的處理 |
| 本機 `unixnu-fuxi/package/BootResetCore.h`、`FuxiButtons.cpp`、`ksrc/docs-lanes/buttons.md` | 復用 SM8550 SPMI v7、PON／GPIO6 與 Fastboot reboot-reason 的暫存器契約；未新增中斷、GPIO 配置或分區寫入 |
| [OddlyLabs/unixnu-fuxi](https://github.com/OddlyLabs/unixnu-fuxi/tree/78f30b651c0ca6ea325682832bb25767523212f5) | 參考同機型板級分層；它的 UniXNU／DarwinBoot 選單是 UEFI 模組，目前這條裸機 shim 不引入完整 UEFI |
| [Mi8 Dual-Boot Lab](https://github.com/Hercules-Zhaoziyi/mi8-dualboot-lab/tree/9dfb6f9492a6b129bdb6342ac1a2d89cbd4c21a5) 的 `bootmenu_hercules.inc`、`docs/PORTING.md` | 參考按鍵、首鍵取消倒數與選項頁返回；依本項目要求改為三個按鈕直接執行。未複製 LVGL、玻璃介面、私有字庫或 Mi8 reset／magic-address 實作 |
| 本機 UniXNU `Menu.c`、UniXNU-sheng 按鍵與 BootReset 實作 | 參考按鍵與啟動契約；UEFI Protocol／XNU IOKit 服務不能直接在目前的早期 shim 執行 |
| 本機 Linux v6.12 `spmi-pmic-arb` 與按鍵 DTS 紀錄 | 交叉核對 SPMI 指令、owner、APID、狀態與按鍵極性；不移植整個 Linux 驅動 |
| iOS 原廠框架 | 選單在作業系統啟動前執行，沒有可直接呼叫的原廠引導介面 |

程式碼在本目錄，與其他工作目錄的未提交修改隔離。`font-generated.h` 是開源 Noto Sans TC 的必要字形子集；來源、字重與雜湊寫在 header，OFL 條款在 `licenses/`。不用 Windows 字庫或既有 ASCII console font。

## 電腦端建置

在工作區根目錄執行，Python 需要 Pillow，主機測試使用既有 WSL `Ubuntu-26.04` 的 clang／ASan／UBSan，ARM64 使用 Windows Android NDK：

```powershell
python -X utf8 dualboot/build.py --output work/dualboot/menu-local
```

輸出目錄必須尚不存在，且位於 `work/dualboot/`。腳本不下載、不找裝置，不執行 adb、fastboot、bootctl 或任何部署流程；`Fastboot` 文字和韌體內的明確按鍵動作不代表建置時有操作手機。

預設讀取已保存的 `chain-v7-ram9g-rtctorch` 和其 `build.txt` 指定的 payload。需要另一個現有正常分流器時使用 `--base-chain <目錄>`，且須具備同格式的來源、物件與校驗檔。這裡的 `9g` 是 12GB 實機的 usable RAM 配置；沒有新增 8GB SKU 支援。

只跑主機測試、產生預覽：

```powershell
python -X utf8 dualboot/build.py --host-only --output work/dualboot/menu-host
```

預設重播 `work/hw-survey/fuxi-live-20261116.dtb`，可用 `--fdt <已存DTB>` 更換；不會向手機補讀。調整文字後，使用指定 OFL 字型重建子集，再建置：

```powershell
python -X utf8 dualboot/tools/make-font.py work/dualboot/font-source/NotoSansTC.ttf
```

字型下載來源與固定雜湊見 `THIRD_PARTY.md`；只有重新產生字形才需要完整 TTF，正常建置直接使用已生成的 header。

產物包括 `boot.img`、`Image`、`loader.elf`、`sha256.txt`、`report.json`、`BUILD.txt`、`commands.log` 與 `host/*.png`。`source/` 保存實際新程式碼，`inputs/` 保存實際復用的物件、FDT 和 linker/header；`report.json` 記錄來源 SHA256，建置結束會檢查來源在過程中沒有改變。

## 啟動交接

Recovery 判斷先於顯示、SPMI、USB 與 XNU 初始化：驗證過的 bootconfig 缺少 `androidboot.force_normal_boot` 時，直接交接內嵌 Linux 核心，沿用 ABL 提供的 Recovery ramdisk。無有效 bootconfig 時保留既有 64MiB initrd 大小判斷。FDT、initrd 或內嵌核心結構無效時不畫選單，沿用舊分流器回到 `loader_main` 的行為；它會獨立檢查 RAM profile，通過後仍可能進入 iOS。

正常開機通過原有 12GB RAM profile 檢查後才顯示選單。Android 選項直接交接內嵌的原 Android 核心，保留該次 ABL 的 FDT／ramdisk；iOS 選項回到既有 `loader_main`。**不修改 A/B active-slot 或 successful／retry metadata**。因此從 b 槽選 Android 仍是 b 槽上下文；這條正常開機路徑與 ABL 重試次數的日常行為尚未驗收。不能把舊的 a 槽 force-Linux 測試當作本次 b 槽 Android 選單通過。

選單透過 PON／GPIO 讀取按鍵。讀取失敗時取消倒數，等待重新放開按鍵。若顯示幾何或初始按鍵讀取不受支援，正常路徑回退到內嵌 Android 核心。進入 Fastboot 和 Recovery 共用 SDAM 開機模式，在 EE0 通道寫入原因並讀回核對後才重啟。Recovery 交由 ABL 載入目前槽位的 Recovery 分區。重啟與關機使用既有 PSCI 呼叫。

## 驗證範圍

主機驗證覆蓋三個首頁焦點、確認後直接啟動對應系統、其他選項返回、倒數、首鍵／組合鍵／去彈跳、FDT／bootconfig 截斷與變異、SPMI 錯誤／owner／讀回、畫面邊界／stride／黑白像素。預覽由實際韌體的 `menu_draw` 產生。

建置另檢查原分流器重連結逐位元相符、新 ARM64 物件不含 ABS 固定地址重定位、Linux 2MiB 對齊與完整 RAM footprint、內嵌 Linux／iOS payload 原樣保存、Android boot header v4、無 ramdisk，以及映像不超過已存紀錄的 192MiB 分區容量。

**尚未完整驗收**：b 槽 Android（已知會循環：b 槽系統分區為空，不修）、其他選項中的 Fastboot／重啟／關機，以及完全關機後冷開機、ABL 重試／回退。boot_a 常駐選單的 Android、iOS、Recovery 已在安裝後驗過；新日常版的安裝後開機另記驗證檔。上機前仍須核對當前 root extents 及有效 RTC 校時。

2026-11-18 的暫時開機試驗未更換 KSU 模組、未寫 boot、未切槽。手機操作一律經 `work/statusbar-fix/launch-loader-check.ps1`、三把鎖與上一趟 mtd 保存；`-DualBootCheck` 提供備份、核對、Android／iOS 暫時開機。校時在確認本流程持有精確鎖 token 後使用既有唯讀 API，不移除或跳過外來鎖。還原點在 `D:\xiaomi13\fuxi_Backup\dualboot-before-validation-20261118-014941`，完成核對資料為 `RESTORE_POINT_READY.json`；iOS 為差異備份，須保留其 README 指定的基底。
