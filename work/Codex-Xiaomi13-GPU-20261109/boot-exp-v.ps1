# ?急???銝憿?args 霈? loader嚗astboot boot嚗?撖怠??嚗????敺???? Android??# ?冽?嚗oot-exp.ps1 -Loader <loader ?桅??? -Out <霅??桅??? [-Kernel <?詨? artifact>] [-Seconds 240] [-MaxStartTemp 400]
param([string]$Loader, [string]$Out, [string]$Kernel = 'kernel-perfgov-artifact', [int]$Seconds = 240, [int]$MaxStartTemp = 400, [int]$Reset = 4, [switch]$TourShots, [switch]$Framebuffer, [int]$MonitorIdle = -1, [string]$Verifier = 'verify-args-on-loader-payload.py', [int]$FramebufferHeight = 2400)
$ErrorActionPreference = 'Stop'
$s = $env:FUXI_ANDROID_SERIAL; if (-not $s) { throw 'Set FUXI_ANDROID_SERIAL' }; $e = 'D:\xiaomi13\fuxi_ios\work\Codex-Xiaomi13-GPU-20261109'
$j = 'CLASSPATH=/data/local/tmp/codex-managed-native-image.jar app_process /system/bin ManagedNativeImage'
$env:PYTHONIOENCODING = 'utf-8'
if (Test-Path "$e\$Out") { throw 'out dir exists' }
if (Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match 'fuxi-xnu-monitor' }) { throw 'a monitor is already running' }
if ("$(adb -s $s shell getprop sys.boot_completed)" -notmatch '1') { throw 'android not booted' }
# 蝑瘙澈摨阡??圈?瑼颱誑銝??憭?6 ??嚗?霈?甈⊿?璈?韏瑕?皞怠漲?亥?
foreach ($i in 1..36) {
    $t = 999; $m = [regex]::Match("$(adb -s $s shell 'dumpsys battery | grep -m1 temperature')", '\d+'); if ($m.Success) { $t = [int]$m.Value }
    if ($t -le $MaxStartTemp) { break }
    Start-Sleep -Seconds 10
}
"start_battery_dC=$t"
adb -s $s shell "su -c '$j map'"
python "D:\xiaomi13\fuxi_ios\work\xiaomi13-ios-gpu-20261108\verify-native-map.py"; $rc = $LASTEXITCODE
if ($rc -eq 0) { Set-Location $e; python $Verifier $Loader; $rc = $LASTEXITCODE }
adb -s $s shell "su -c 'sync; $j unmap; $j status'"
if ($rc -ne 0) { throw 'verify failed' }
$img = "$e\$Loader\boot.img"
adb -s $s reboot bootloader
$ok = $false; foreach ($i in 1..40) { Start-Sleep -Seconds 2; if ("$(fastboot devices)" -match $s) { $ok = $true; break } }
if (-not $ok) { throw 'no fastboot device' }
"boot_time=$(Get-Date -Format 'HH:mm:ss')"
fastboot -s $s boot $img; "fastboot_exit=$LASTEXITCODE"
$log = "$Out-monitor.log"
$cl = "powershell.exe -WindowStyle Hidden -NoProfile -ExecutionPolicy Bypass -File `"$e\run-monitor.ps1`" -Loader $Loader -Out $Out -Log $log -Seconds $Seconds -Reset $Reset -Kernel $Kernel -FramebufferHeight $FramebufferHeight$(if ($TourShots) { ' -TourShots' })$(if ($Framebuffer) { ' -Framebuffer' })$(if ($MonitorIdle -ge 0) { " -MonitorIdle $MonitorIdle" })"
$si = New-CimInstance -ClassName Win32_ProcessStartup -Property @{ ShowWindow = [uint16]0 } -ClientOnly
$r = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = $cl; CurrentDirectory = 'D:\xiaomi13\fuxi_ios'; ProcessStartupInformation = $si }
"wmi_return=$($r.ReturnValue) pid=$($r.ProcessId)"
Start-Sleep -Seconds 20
foreach ($i in 1..([int](($Seconds + 90) / 5))) { Start-Sleep -Seconds 5; if (-not (Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match 'fuxi-xnu-monitor' })) { break } }
$ErrorActionPreference = 'Continue'
foreach ($i in 1..40) { $b = cmd /c "adb -s $s shell getprop sys.boot_completed 2>nul"; if ("$b" -match '1') { break }; Start-Sleep -Seconds 4 }
"android boot_completed=$b"
adb -s $s shell "su -c '$j status'"
adb -s $s shell "dumpsys battery | grep -E 'level|temperature'"
$f = "$e\$Out\boot.log"
$logSha = python -c "import hashlib,sys; print(hashlib.file_digest(open(sys.argv[1],'rb'),'sha256').hexdigest())" $f
"lines=$((Get-Content $f | Measure-Object -Line).Lines) sha=$logSha"
(Get-Content "$e\$log" | Select-String 'thermal \+' | ForEach-Object { [double](($_.Line -split 'max \S+ ')[1] -replace 'C,.*', '') } | Measure-Object -Maximum).Maximum | ForEach-Object { "host_tsens_max=$_" }
