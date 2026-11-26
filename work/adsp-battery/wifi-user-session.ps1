# 讓使用者在 iOS 上用真密碼試連 Wi-Fi 的一趟暫時開機（不改日常 boot_b）。
# 依序：唯讀 RTC 校時 → 產生 native Wi-Fi loader（offset 放 ADT，日期正確 HTTPS 才能用）→ 經 launch-loader-check 開機。
# 密碼由使用者在手機上自己輸入；本腳本、loader、開機參數與紀錄都不含密碼。
# 例：.\wifi-user-session.ps1 -Minutes 15
param(
    [ValidateRange(3, 40)][int]$Minutes = 15,
    [string]$Kernel = 'kernel-local-bt2-9g-artifact',
    [string]$Like = 'native-srgb-safari-ram9g-sb127-loader'
)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$stamp = Get-Date -Format 'MMddHHmm'
$name = "native-wlan-user-$stamp-ram9g-sb127-loader"
$calibration = Join-Path $here "rtc-calibration-wifi-$stamp.json"

# 校時量測一小時內有效，所以每次重量。
& python -X utf8 (Join-Path $here 'rtc-calibration.py') capture $calibration
if ($LASTEXITCODE) { throw 'RTC calibration failed' }
# 沿用 2026-11-17 Safari sRGB 配置，明確保留 dwell／batch、ADSP／SPF 與震動執行期，
# 注入 mediaserverd 並啟用 closed-loop；不開震動自測。互動震動與自動加入仍待修復。
& python -X utf8 (Join-Path $here 'prepare-wlan-w18-loader.py') build --like $Like --wifi-ios --artifact $Kernel `
    --scan-dwell --wifi-trace --scan-batch --with-adsp --audio-spf --quiet-screen --display-gamut sRGB `
    --haptic 2 --inject-media --product-haptics --stderr-log wifid,mediaserverd `
    --name $name --rtc-calibration $calibration | Out-Null
if ($LASTEXITCODE) { throw 'Wi-Fi loader build failed' }

$seconds = $Minutes * 60
$reset = $Minutes + 5
& (Join-Path $here '..\statusbar-fix\launch-loader-check.ps1') -Loader $name -Kernel $Kernel `
    -Out "native-hw000wlan-user-$stamp-boot" -Seconds $seconds -Reset $reset
exit $LASTEXITCODE
