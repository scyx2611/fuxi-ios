[CmdletBinding(DefaultParameterSetName = 'Boot')]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'Boot')][string]$Loader,
    [Parameter(Mandatory = $true, ParameterSetName = 'Boot')][string]$Kernel,
    [Parameter(Mandatory = $true)][string]$Out,
    [Parameter(ParameterSetName = 'Boot')][int]$Seconds = 300,
    [Parameter(ParameterSetName = 'Boot')][int]$Reset = 6,
    [Parameter(ParameterSetName = 'Boot')][int]$FramebufferHeight = 2400,
    [Parameter(ParameterSetName = 'Boot')][switch]$FinalFramebuffer,
    [Parameter(ParameterSetName = 'Boot')][string]$PeerTestPython,
    [Parameter(ParameterSetName = 'Boot')][switch]$PeerAfterPowerCycle,
    [Parameter(ParameterSetName = 'Boot')][switch]$PeerAudioSource,
    [Parameter(ParameterSetName = 'Boot')][ValidateCount(2,2)][ValidatePattern('^[a-z0-9-]+$')][string[]]$PeerIdentityRuns,
    [Parameter(ParameterSetName = 'Boot')][string]$Dylib,
    [Parameter(ParameterSetName = 'Boot')][ValidatePattern('^[a-f0-9]{64}$')][string]$DylibSha,
    [Parameter(ParameterSetName = 'Boot')][string]$TurnipHybrid,
    [Parameter(ParameterSetName = 'Boot')][ValidatePattern('^[a-f0-9]{64}$')][string]$TurnipSha,
    [Parameter(ParameterSetName = 'Boot')][switch]$TemporaryEnableFairPlayd,
    [Parameter(ParameterSetName = 'Boot')][string]$AppStoreDylib,
    [Parameter(ParameterSetName = 'Boot')][ValidatePattern('^[a-f0-9]{64}$')][string]$AppStoreDylibSha,
    [Parameter(Mandatory = $true, ParameterSetName = 'InspectWifi')][switch]$InspectWifi,
    [Parameter(Mandatory = $true, ParameterSetName = 'WifiLogging')][ValidateSet('enable', 'disable')][string]$WifiLogging,
    [Parameter(Mandatory = $true, ParameterSetName = 'InspectWifiLog')][switch]$InspectWifiLog,
    [Parameter(Mandatory = $true, ParameterSetName = 'CalibrateRtc')][switch]$CalibrateRtc,
    [Parameter(Mandatory = $true, ParameterSetName = 'PullConsole')][switch]$PullConsole,
    [Parameter(Mandatory = $true, ParameterSetName = 'CaptureAudioFirmware')][switch]$CaptureAudioFirmware,
    [Parameter(Mandatory = $true, ParameterSetName = 'CaptureAudioCalibration')][switch]$CaptureAudioCalibration,
    [Parameter(Mandatory = $true, ParameterSetName = 'InspectHaptic')][switch]$InspectHaptic,
    [Parameter(ParameterSetName = 'InspectHaptic')][switch]$TraceEffects,
    [Parameter(Mandatory = $true, ParameterSetName = 'InspectCrash')][switch]$InspectCrash,
    [Parameter(Mandatory = $true, ParameterSetName = 'InspectAppStoreBaseline')][switch]$InspectAppStoreBaseline,
    [Parameter(Mandatory = $true, ParameterSetName = 'LaunchdJobEnable')][ValidatePattern('^com\.apple\.[A-Za-z0-9.]+$')][string]$LaunchdJobEnable,
    [Parameter(Mandatory = $true, ParameterSetName = 'LaunchdJobDisable')][ValidatePattern('^com\.apple\.[A-Za-z0-9.]+$')][string]$LaunchdJobDisable,
    [Parameter(Mandatory = $true, ParameterSetName = 'InspectAudio')][switch]$InspectAudio,
    [Parameter(Mandatory = $true, ParameterSetName = 'AudioHal')][ValidateSet('enable', 'disable')][string]$AudioHal,
    [Parameter(Mandatory = $true, ParameterSetName = 'AudioHal')][string]$AudioBundle,
    [Parameter(Mandatory = $true, ParameterSetName = 'AudioHal')][ValidatePattern('^[a-f0-9]{64}$')][string]$AudioSha,
    [Parameter(Mandatory = $true, ParameterSetName = 'NativeMetal')][string]$NativeMetalDir,
    [Parameter(Mandatory = $true, ParameterSetName = 'InstallApp')][string]$InstallAppDir,
    [Parameter(Mandatory = $true, ParameterSetName = 'RemoveApp')][ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]{0,63}\.app$')][string]$RemoveAppName,
    [Parameter(Mandatory = $true, ParameterSetName = 'RemoveApp')][ValidatePattern('^[A-Za-z0-9][A-Za-z0-9.-]{0,127}$')][string]$RemoveAppBundleId,
    [Parameter(Mandatory = $true, ParameterSetName = 'DualBoot')][ValidateSet('backup', 'rootbackup', 'prepare', 'survey', 'android', 'recovery', 'ios', 'inspectreturn', 'timing', 'stocktiming', 'removeksu')][string]$DualBootCheck,
    [Parameter(Mandatory = $true, ParameterSetName = 'DualBoot')][string]$RestorePoint,
    [Parameter(ParameterSetName = 'DualBoot')][string]$Candidate,
    [Parameter(ParameterSetName = 'DualBoot')][int]$MenuSeconds = 180,
    [Parameter(Mandatory = $true, ParameterSetName = 'DeployChain')][ValidatePattern('^chain-[a-z0-9-]+$')][string]$DeployChain,
    [Parameter(Mandatory = $true, ParameterSetName = 'DeployChain')][ValidatePattern('^chain-[a-z0-9-]+$')][string]$PreviousChain,
    [Parameter(Mandatory = $true, ParameterSetName = 'DeployChain')][ValidateRange(5, 999)][int]$ModuleVersion,
    [Parameter(Mandatory = $true, ParameterSetName = 'InstallPersistent')][switch]$InstallPersistent,
    [Parameter(Mandatory = $true, ParameterSetName = 'ValidatePersistent')][switch]$ValidatePersistent,
    [Parameter(ParameterSetName = 'ValidatePersistent')][switch]$IosOnly,
    [Parameter(ParameterSetName = 'ValidatePersistent')][ValidatePattern('^[a-z0-9-]+$')][string]$ResumeAfterIos,
    [Parameter(ParameterSetName = 'ValidatePersistent')]
    [Parameter(ParameterSetName = 'UpgradePersistent')]
    [ValidatePattern('^[a-z0-9-]+$')][string]$InstallReport,
    [Parameter(Mandatory = $true, ParameterSetName = 'UpgradePersistent')][switch]$UpgradePersistent,
    [Parameter(Mandatory = $true, ParameterSetName = 'RescueAndroid')][switch]$RescueAndroid,
    [Parameter(Mandatory = $true, ParameterSetName = 'StartDaily')][ValidatePattern('^chain-[a-z0-9-]+$')][string]$StartDailyChain,
    [Parameter(Mandatory = $true, ParameterSetName = 'RemoveAppStoreDylib')][switch]$RemoveAppStoreDylib
)
$ErrorActionPreference = 'Stop'
$workspace = 'D:\xiaomi13\fuxi_ios'
$storage = Join-Path $workspace 'work\Codex-Xiaomi13-GPU-20261109'
$phoneLock = Join-Path $workspace 'work\PHONE-LOCK-000'
$phoneHold = Join-Path $workspace 'work\hybrid-driver\HOLD-PHONE'
$phonePause = Join-Path $workspace 'work\adsp-battery\hw000\HOLD'
$fairplaydRescueHold = Join-Path $workspace 'work\statusbar-fix\FAIRPLAYD-RESCUE-HOLD'
$fairplaydDisabledLaunchdSha = '8bd346db6c54c11a25bfd6b8b45642f1c56f1d02440b8be2633b6ba3ab6505b6'
$gitBash = 'C:\Program Files\Git\bin\bash.exe'
$serial = $env:FUXI_ANDROID_SERIAL; if (-not $serial) { throw 'Set FUXI_ANDROID_SERIAL to the phone serial shown by adb devices' }
$token = "000 loader check $Out pid=$PID $(Get-Date -Format o)"
$ownedLocks = @()
$peerJob = $null
$peerState = $null
# Windows PowerShell 5.1：Stop 模式下原生程式寫到 stderr 的字（即使 2>$null，或呼叫端用 *> 轉向）會變成致命錯誤。
# 手機還沒回 Android 時 adb 會寫「device not found」，曾讓本腳本跳過 reset-soon 與等待回程（2026-11-17 02:41）。
# 原生程式一律在 Continue 下執行，成敗只看結束碼。
function Invoke-Native([scriptblock]$Block) {
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { & $Block } finally { $ErrorActionPreference = $saved }
}
function Test-AndroidBooted {
    "$(Invoke-Native { & adb -s $serial shell getprop sys.boot_completed 2>$null })" -match '^1'
}
function Get-LaunchdAfterSha256([string]$GuestLogPath) {
    if (-not (Test-Path -LiteralPath $GuestLogPath)) { return $null }
    $last = $null
    foreach ($line in [System.IO.File]::ReadAllLines($GuestLogPath)) {
        if ($line -match '^LAUNCHD_AFTER\s+\d+\s+([a-f0-9]{64})\s*$') {
            $last = $Matches[1].ToLowerInvariant()
        }
    }
    return $last
}
function Test-FairPlaydDisabledSafe([int]$ExitCode, [string]$GuestLogPath, [string]$ExpectedSha) {
    if (-not (Test-Path -LiteralPath $GuestLogPath)) { return $false }
    $lines = [System.IO.File]::ReadAllLines($GuestLogPath)
    $afterSha = Get-LaunchdAfterSha256 -GuestLogPath $GuestLogPath
    if ($afterSha -ne $ExpectedSha) { return $false }
    if ($ExitCode -eq 0) { return $true }
    $beforeMatched = $false
    $stageOk = $false
    $guestOk = $false
    $wroteBlocks = $false
    foreach ($line in $lines) {
        if ($line -match ('^LAUNCHD_BEFORE\s+\d+\s+' + [regex]::Escape($ExpectedSha) + '\s*$')) { $beforeMatched = $true }
        if ($line.Trim() -eq 'LAUNCHD_STAGE_SUCCESS') { $stageOk = $true }
        if ($line.Trim() -eq 'GUEST_OK') { $guestOk = $true }
        if ($line -match 'WRITE_CHANGED_BLOCKS') { $wroteBlocks = $true }
    }
    return ($beforeMatched -and $stageOk -and $guestOk -and (-not $wroteBlocks))
}
function Assert-BootLaunchdPrecheck([string]$WorkspacePath, [string]$StoragePath, [string]$LoaderName, [string]$OutName, [bool]$TemporaryEnableFairPlayd) {
    $payloadPath = Join-Path (Join-Path $StoragePath $LoaderName) 'payload.bin'
    if (-not (Test-Path -LiteralPath $payloadPath)) {
        throw "Boot precheck missing loader payload.bin: $payloadPath"
    }
    $pyCode = @'
import importlib.util, json, pathlib, struct, sys
payload = pathlib.Path(sys.argv[1]).read_bytes()
adt_py = pathlib.Path(sys.argv[2])
if payload[:8] != b"SWEETXNU":
    raise SystemExit("invalid payload.bin magic")
raw_cmd = payload[72:680]
cmd_bytes, sep, _ = raw_cmd.partition(b"\0")
if sep != b"\0":
    raise SystemExit("unterminated payload.bin cmdline")
cmdline = cmd_bytes.decode("ascii")
sweetnoexec_has_fp = False
for tok in cmdline.split(" "):
    if tok.startswith("sweetnoexec="):
        sweetnoexec_has_fp = "fairplayd.H2" in tok.partition("=")[2].split(",")
adt_off, adt_size = struct.unpack_from("<2Q", payload, 32)
spec = importlib.util.spec_from_file_location("fuxi_adt", adt_py)
adt_mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(adt_mod)
tree, consumed = adt_mod.parse(payload[adt_off:adt_off + adt_size])
if consumed != adt_size:
    raise SystemExit("invalid embedded ADT size")
chosen = next((c for c in tree.children if c.name == "chosen"), None)
if chosen is None:
    raise SystemExit("missing /chosen in embedded ADT")
def u32(name):
    raw = chosen.prop(name)
    return struct.unpack("<I", raw)[0] if raw is not None and len(raw) == 4 else 0
print(json.dumps({
    "has_sweetnoexec_fairplayd": sweetnoexec_has_fp,
    "appstore_ui": u32("fuxi,appstore-ui"),
    "appstore_fix": u32("fuxi,appstore-fix"),
}))
'@
    $adtTool = Join-Path $WorkspacePath 'work\adsp-battery\ksrc\tools\adt.py'
    $jsonOut = Invoke-Native { $pyCode | & python -X utf8 -B - $payloadPath $adtTool }
    if ($LASTEXITCODE -ne 0 -or -not $jsonOut) {
        throw "Failed to parse loader payload.bin for launchd precheck: $payloadPath"
    }
    $meta = ($jsonOut -join "`n") | ConvertFrom-Json
    if ($TemporaryEnableFairPlayd) {
        if ($meta.appstore_ui -eq 0 -or $meta.appstore_fix -ne 0) {
            throw "TemporaryEnableFairPlayd requires ADT fuxi,appstore-ui != 0 and fuxi,appstore-fix == 0 (got ui=$($meta.appstore_ui), fix=$($meta.appstore_fix))"
        }
        return
    }
    if ($meta.has_sweetnoexec_fairplayd -and $meta.appstore_ui -eq 0) {
        if ($OutName -cnotmatch '^[a-z0-9-]+$') {
            throw "Boot launchd precheck requires lowercase alphanumeric-hyphen -Out name (got '$OutName')"
        }
        $precheckDir = Join-Path $WorkspacePath "work\statusbar-fix\inspect-precheck-$OutName"
        Invoke-Native { & python -X utf8 -B (Join-Path $WorkspacePath 'work\statusbar-fix\inspect-powerlog-state.py') `
            --output $precheckDir --appstore-baseline }
        if ($LASTEXITCODE -ne 0) {
            throw 'Read-only launchd.plist precheck failed; refusing to boot with sweetnoexec=fairplayd.H2'
        }
        $statePath = Join-Path $precheckDir 'powerlog-state.json'
        if (-not (Test-Path -LiteralPath $statePath)) {
            throw "Missing precheck state file: $statePath"
        }
        $state = [System.IO.File]::ReadAllText($statePath) | ConvertFrom-Json
        $disabled = $state.appstore_baseline.xpc_launchd.jobs.'com.apple.fairplayd.H2'.configuration.Disabled
        if ($disabled -ne $true) {
            throw "U-010 guard: com.apple.fairplayd.H2 Disabled is '$disabled' (expected True) under sweetnoexec=fairplayd.H2"
        }
    }
}
Set-Location $workspace
if ([bool]$PeerTestPython -ne [bool]$PeerIdentityRuns) { throw 'Provide both the peer-test Python and two reference runs' }
if ($PeerAfterPowerCycle -and -not $PeerTestPython) { throw 'A power-cycle check requires the Windows peer test' }
if ($PeerAudioSource -and -not $PeerTestPython) { throw 'An audio service check requires the Windows peer test' }
if ($PeerTestPython) {
    if ($Out -cnotmatch '^[a-z0-9-]+$') { throw 'Invalid peer-test run name' }
    $peerRun = Join-Path $storage $Out
    if (Test-Path -LiteralPath $peerRun) { throw 'Peer test needs a new run directory' }
    $peerScript = Join-Path $workspace 'work\statusbar-fix\windows-bluetooth-peer.py'
    $peerArgs = @('-X', 'utf8', '-B', $peerScript, '--reference')
    foreach ($name in $PeerIdentityRuns) { $peerArgs += (Join-Path $storage "$name\boot.log") }
    $peerArgs += @('--run', $peerRun, '--marker-timeout', "$Seconds")
    if ($PeerAfterPowerCycle) { $peerArgs += '--require-cycle' }
    if ($PeerAudioSource) { $peerArgs += '--audio-source' }
    & $PeerTestPython @peerArgs --preflight
    if ($LASTEXITCODE -ne 0) { throw 'Windows radio or phone identity preflight failed' }
}
if ([bool]$Dylib -ne [bool]$DylibSha) { throw 'Dylib and its verified SHA256 must be provided together' }
if ($Dylib -and (Get-FileHash -LiteralPath $Dylib -Algorithm SHA256).Hash.ToLowerInvariant() -ne $DylibSha) {
    throw 'Dylib hash differs from the verified build'
}
if ([bool]$TurnipHybrid -ne [bool]$TurnipSha) { throw 'Turnip and its verified SHA256 must be provided together' }
if ($TurnipHybrid -and $Dylib) { throw 'Stage one library per phone validation' }
if ($TurnipHybrid -and (Get-FileHash -LiteralPath $TurnipHybrid -Algorithm SHA256).Hash.ToLowerInvariant() -ne $TurnipSha) {
    throw 'Turnip hash differs from the verified build'
}
if ([bool]$AppStoreDylib -ne [bool]$AppStoreDylibSha) { throw 'AppStoreDylib and its verified SHA256 must be provided together' }
if ($AppStoreDylib -and ($Dylib -or $TurnipHybrid)) { throw 'Stage one library per phone validation' }
if ($AppStoreDylib -and (Get-FileHash -LiteralPath $AppStoreDylib -Algorithm SHA256).Hash.ToLowerInvariant() -ne $AppStoreDylibSha) {
    throw 'AppStoreDylib hash differs from the verified build'
}
if ($RemoveAppName) {
    if ($Out -cnotmatch '^[a-z0-9-]+$') { throw 'Invalid app removal output name' }
    if ($RemoveAppName -cnotmatch '\.app$') { throw 'RemoveAppName must end with lowercase .app' }
    if ($RemoveAppBundleId.ToLowerInvariant().StartsWith('com.apple.')) { throw "Refusing to remove Apple bundle id: $RemoveAppBundleId" }
}
foreach ($path in @($phoneLock, $phoneHold, $phonePause)) {
    if (Test-Path -LiteralPath $path) { throw "Phone is reserved: $path" }
}
$fairplaydRescueBlockedSets = @(
    'Boot', 'LaunchdJobEnable', 'DeployChain', 'StartDaily',
    'DualBoot', 'InstallPersistent', 'ValidatePersistent', 'UpgradePersistent'
)
if ((Test-Path -LiteralPath $fairplaydRescueHold) -and ($PSCmdlet.ParameterSetName -in $fairplaydRescueBlockedSets)) {
    throw "FairPlayd rescue hold active ($fairplaydRescueHold); run -RescueAndroid then -LaunchdJobDisable com.apple.fairplayd.H2 first"
}
$running = Get-CimInstance Win32_Process | Where-Object {
    $_.ProcessId -ne $PID -and $_.CommandLine -notmatch 'Get-CimInstance' -and
    $_.CommandLine -match 'fuxi-xnu-monitor\.py|boot-exp-v\.ps1|run-monitor\.ps1'
}
if ($running) { throw 'Another phone test is running' }
if ($CalibrateRtc) {
    if ($Out -cnotmatch '^[a-z0-9][a-z0-9-]{0,63}$') { throw 'Invalid RTC calibration name' }
    $calibration = Join-Path $workspace "work\adsp-battery\rtc-calibration-$Out.json"
    Invoke-Native { & python -X utf8 -B (Join-Path $workspace 'work\adsp-battery\rtc-calibration.py') capture $calibration }
    if ($LASTEXITCODE -ne 0) { throw 'Read-only RTC calibration failed' }
    return
}
if ($InspectHaptic) {
    if ($Out -cnotmatch '^[a-z0-9][a-z0-9-]{0,63}$') { throw 'Invalid haptic report name' }
    $hapticArgs = @('--serial', $serial, '--out', $Out)
    if ($TraceEffects) { $hapticArgs += '--trace' }
    Invoke-Native { & python -X utf8 -B (Join-Path $workspace 'work\statusbar-fix\inspect-android-haptic.py') @hapticArgs }
    if ($LASTEXITCODE -ne 0) { throw 'Android haptic inspection failed' }
    return
}
if ($CaptureAudioFirmware -or $CaptureAudioCalibration) {
    if ($Out -cnotmatch '^[a-z0-9][a-z0-9-]{0,63}$') { throw 'Invalid audio firmware capture name' }
    $capture = Join-Path $workspace "work\adsp-battery\$Out"
    $audioKind = if ($CaptureAudioCalibration) { '--calibration' } else { '--cs35l41' }
    Invoke-Native { & python -X utf8 -B (Join-Path $workspace 'work\statusbar-fix\fetch-android-audio-config.py') `
        $audioKind --output $capture --serial $serial }
    if ($LASTEXITCODE -ne 0) { throw 'Read-only Android audio firmware capture failed' }
    return
}
if ($InstallPersistent -or $ValidatePersistent -or $UpgradePersistent) {
    $persistArgs = @('-X', 'utf8', '-B', (Join-Path $workspace 'dualboot\install-persistent.py'),
        '--from-launcher', "$PID", '--out', $Out)
    if ($ValidatePersistent) { $persistArgs += '--validate' }
    if ($IosOnly) { $persistArgs += '--ios-only' }
    if ($UpgradePersistent) { $persistArgs += '--upgrade' }
    if ($ResumeAfterIos) { $persistArgs += @('--resume-after-ios', $ResumeAfterIos) }
    if ($InstallReport) { $persistArgs += @('--install-report', $InstallReport) }
    Invoke-Native { & python @persistArgs }
    if ($LASTEXITCODE -ne 0) { throw 'Persistent dualboot workflow failed; see its report.json' }
    return
}
if ($RescueAndroid) {
    # Fastboot rescue cannot pull mtd before Android returns. The helper saves
    # that log after returning to slot a and never starts iOS or flashes images.
    Invoke-Native { & python -X utf8 -B (Join-Path $workspace 'dualboot\rescue-android.py') --from-launcher "$PID" --out $Out }
    if ($LASTEXITCODE -ne 0) { throw 'Android rescue needs attention; see its report.json' }
    return
}
if ($StartDailyChain) {
    $dailyArgs = @('-X', 'utf8', '-B', (Join-Path $workspace 'dualboot\start-daily.py'),
        '--from-launcher', "$PID", '--chain', $StartDailyChain, '--out', $Out)
    Invoke-Native { & python @dailyArgs }
    if ($LASTEXITCODE -ne 0) { throw 'Installed daily boot needs attention; see its report.json' }
    return
}
if ($DualBootCheck) {
    $dualBootArgs = @('-X', 'utf8', '-B', (Join-Path $workspace 'dualboot\phone-check.py'),
        '--from-launcher', "$PID", '--action', $DualBootCheck, '--out', $Out,
        '--restore-point', $RestorePoint, '--seconds', "$MenuSeconds")
    if ($Candidate) { $dualBootArgs += @('--candidate', $Candidate) }
    Invoke-Native { & python @dualBootArgs }
    if ($LASTEXITCODE -ne 0) { throw 'Dual-boot validation failed; see its report.json' }
    return
}
Invoke-Native { & $gitBash 'work/adsp-battery/hw000/fuxi-log.sh' pull }
if ($LASTEXITCODE -ne 0) { throw 'Persistent log pull failed; next boot stopped' }
if ($PullConsole) { return }
if ($DeployChain) {
    # Reuse the existing writer and verified recovery flow. It acquires both
    # locks itself, verifies the 12GB SKU and old boot/module, reads back every
    # write and preserves a full boot_b plus module restore point.
    $deployArgs = @('-X', 'utf8', '-B', (Join-Path $workspace 'work\boot-chain\deploy-verified.py'),
        $PreviousChain, $DeployChain, '--version', "$ModuleVersion")
    if (Test-Path -LiteralPath (Join-Path $workspace "work\boot-chain\$DeployChain\menu-build-report.json")) {
        $deployArgs += '--menu-entry'
    }
    Invoke-Native { & python @deployArgs }
    if ($LASTEXITCODE -ne 0) { throw 'Daily chain deployment failed; see its recovery report' }
    return
}
if ($InspectWifi) {
    Invoke-Native { & python -X utf8 -B (Join-Path $workspace 'work\statusbar-fix\inspect-powerlog-state.py') `
        --output (Join-Path $workspace "work\statusbar-fix\$Out") --wifi }
    if ($LASTEXITCODE -ne 0) { throw 'Read-only Wi-Fi inspection failed' }
    return
}
if ($WifiLogging) {
    Invoke-Native { & python -X utf8 -B (Join-Path $workspace 'work\statusbar-fix\phone-readthrough.py') `
        --wifi-logging $WifiLogging --output (Join-Path $workspace "work\statusbar-fix\$Out") }
    if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi logging configuration failed' }
    return
}
if ($InspectWifiLog) {
    Invoke-Native { & python -X utf8 -B (Join-Path $workspace 'work\statusbar-fix\inspect-powerlog-state.py') `
        --output (Join-Path $workspace "work\statusbar-fix\$Out") --wifi-log }
    if ($LASTEXITCODE -ne 0) { throw 'Read-only Wi-Fi log inspection failed' }
    return
}
if ($InspectCrash) {
    Invoke-Native { & python -X utf8 -B (Join-Path $workspace 'work\statusbar-fix\inspect-powerlog-state.py') `
        --output (Join-Path $workspace "work\statusbar-fix\$Out") --crash-reports }
    if ($LASTEXITCODE -ne 0) { throw 'Read-only crash report inspection failed' }
    return
}
if ($InspectAppStoreBaseline) {
    Invoke-Native { & python -X utf8 -B (Join-Path $workspace 'work\statusbar-fix\inspect-powerlog-state.py') `
        --output (Join-Path $workspace "work\statusbar-fix\$Out") --appstore-baseline }
    if ($LASTEXITCODE -ne 0) { throw 'Read-only AppStore baseline inspection failed' }
    return
}
if ($LaunchdJobEnable) {
    Invoke-Native { & python -X utf8 -B (Join-Path $workspace 'work\statusbar-fix\phone-readthrough.py') `
        --enable-job $LaunchdJobEnable --output (Join-Path $workspace "work\statusbar-fix\$Out") }
    if ($LASTEXITCODE -ne 0) { throw 'Launchd job enable staging failed' }
    return
}
if ($LaunchdJobDisable) {
    $disableOut = Join-Path $workspace "work\statusbar-fix\$Out"
    $disableGuestLog = Join-Path $disableOut 'guest.log'
    Invoke-Native { & python -X utf8 -B (Join-Path $workspace 'work\statusbar-fix\phone-readthrough.py') `
        --disable-job $LaunchdJobDisable --output $disableOut }
    $disableExit = $LASTEXITCODE
    if ($LaunchdJobDisable -eq 'com.apple.fairplayd.H2') {
        if (-not (Test-FairPlaydDisabledSafe -ExitCode $disableExit -GuestLogPath $disableGuestLog -ExpectedSha $fairplaydDisabledLaunchdSha)) {
            throw "Launchd job disable failed or LAUNCHD_AFTER SHA-256 != $fairplaydDisabledLaunchdSha"
        }
        if (Test-Path -LiteralPath $fairplaydRescueHold) {
            Remove-Item -LiteralPath $fairplaydRescueHold
        }
    } elseif ($disableExit -ne 0) {
        throw 'Launchd job disable staging failed'
    }
    return
}
if ($InspectAudio) {
    Invoke-Native { & python -X utf8 -B (Join-Path $workspace 'work\statusbar-fix\inspect-audio-dictation.py') `
        --output (Join-Path $workspace "work\statusbar-fix\$Out") }
    if ($LASTEXITCODE -ne 0) { throw 'Read-only audio and dictation inspection failed' }
    return
}
if ($AudioHal) {
    Invoke-Native { & python -X utf8 -B (Join-Path $workspace 'work\statusbar-fix\phone-readthrough.py') `
        --audio-hal $AudioHal --audio-bundle $AudioBundle --sha256 $AudioSha `
        --output (Join-Path $workspace "work\statusbar-fix\$Out") }
    if ($LASTEXITCODE -ne 0) { throw 'Audio HAL staging failed' }
    return
}
if ($NativeMetalDir) {
    Invoke-Native { & python -X utf8 -B (Join-Path $workspace 'work\statusbar-fix\phone-readthrough.py') `
        --nativemetal-dir $NativeMetalDir `
        --output (Join-Path $workspace "work\statusbar-fix\$Out") }
    if ($LASTEXITCODE -ne 0) { throw 'NativeMetal staging failed' }
    return
}
if ($InstallAppDir) {
    if ($Out -cnotmatch '^[a-z0-9-]+$') { throw 'Invalid app install output name' }
    if (-not (Test-Path -LiteralPath $InstallAppDir)) { throw "InstallAppDir not found: $InstallAppDir" }
    Invoke-Native { & python -X utf8 -B (Join-Path $workspace 'work\statusbar-fix\phone-readthrough.py') `
        --install-app $InstallAppDir `
        --output (Join-Path $workspace "work\statusbar-fix\$Out") }
    if ($LASTEXITCODE -ne 0) { throw 'App installation staging failed' }
    return
}
if ($RemoveAppName) {
    Invoke-Native { & python -X utf8 -B (Join-Path $workspace 'work\statusbar-fix\phone-readthrough.py') `
        --remove-app-name $RemoveAppName --remove-app-bundle-id $RemoveAppBundleId `
        --output (Join-Path $workspace "work\statusbar-fix\$Out") }
    if ($LASTEXITCODE -ne 0) { throw 'App removal staging failed' }
    return
}
if ($RemoveAppStoreDylib) {
    Invoke-Native { & python -X utf8 -B (Join-Path $workspace 'work\statusbar-fix\phone-readthrough.py') `
        --remove-appstore-dylib --output (Join-Path $workspace "work\statusbar-fix\$Out") }
    if ($LASTEXITCODE -ne 0) { throw 'AppStore dylib removal staging failed' }
    return
}
Assert-BootLaunchdPrecheck -WorkspacePath $workspace -StoragePath $storage -LoaderName $Loader -OutName $Out -TemporaryEnableFairPlayd ([bool]$TemporaryEnableFairPlayd)
if ($Dylib) {
    $stageOutput = Join-Path $workspace "work\statusbar-fix\run-$Out-stage"
    Invoke-Native { & python -X utf8 -B (Join-Path $workspace 'work\statusbar-fix\phone-readthrough.py') `
        --dylib $Dylib --sha256 $DylibSha --output $stageOutput }
    if ($LASTEXITCODE -ne 0) { throw 'Dylib staging failed; next boot stopped' }
}
if ($TurnipHybrid) {
    $stageOutput = Join-Path $workspace "work\statusbar-fix\run-$Out-stage"
    Invoke-Native { & python -X utf8 -B (Join-Path $workspace 'work\statusbar-fix\phone-readthrough.py') `
        --turnip-hybrid $TurnipHybrid --sha256 $TurnipSha --output $stageOutput }
    if ($LASTEXITCODE -ne 0) { throw 'Turnip staging failed; next boot stopped' }
}
if ($AppStoreDylib) {
    $stageOutput = Join-Path $workspace "work\statusbar-fix\run-$Out-appstore-stage"
    Invoke-Native { & python -X utf8 -B (Join-Path $workspace 'work\statusbar-fix\phone-readthrough.py') `
        --appstore-dylib $AppStoreDylib --sha256 $AppStoreDylibSha --output $stageOutput }
    if ($LASTEXITCODE -ne 0) { throw 'AppStore dylib staging failed; next boot stopped' }
}
$fairplaydEnabledForRun = $false
$fairplaydRestoreError = $null
try {
    if ($TemporaryEnableFairPlayd) {
        $fpEnableOut = Join-Path $workspace "work\statusbar-fix\run-$Out-fp-enable"
        Invoke-Native { & python -X utf8 -B (Join-Path $workspace 'work\statusbar-fix\phone-readthrough.py') `
            --enable-job com.apple.fairplayd.H2 --output $fpEnableOut }
        if ($LASTEXITCODE -ne 0) { throw 'Temporary FairPlayd enable failed; next boot stopped' }
        $fairplaydEnabledForRun = $true
    }
    try {
        foreach ($path in @($phoneLock, $phoneHold)) {
            $stream = [System.IO.File]::Open($path, [System.IO.FileMode]::CreateNew)
            try {
                $bytes = [System.Text.Encoding]::UTF8.GetBytes($token)
                $stream.Write($bytes, 0, $bytes.Length)
            } finally {
                $stream.Dispose()
            }
            $ownedLocks += $path
        }
        if (Test-Path -LiteralPath $phonePause) { throw 'Phone work was paused' }
        "$(Get-Date -Format HH:mm:ss) PHONE_START $token"
        if ($PeerTestPython) {
            $peerJob = Start-Job -ScriptBlock {
                param($interpreter, $arguments)
                & $interpreter @arguments
                if ($LASTEXITCODE -ne 0) { throw 'Windows Bluetooth peer check failed' }
            } -ArgumentList $PeerTestPython,$peerArgs
        }
        & (Join-Path $storage 'boot-exp-v.ps1') -Loader $Loader -Out $Out -Kernel $Kernel `
            -Seconds $Seconds -Reset $Reset -Verifier 'verify-loader-safety.py' `
            -TourShots:(-not $FinalFramebuffer) -Framebuffer:$FinalFramebuffer `
            -FramebufferHeight $FramebufferHeight
        if (-not (Test-AndroidBooted)) {
            Push-Location $storage
            try {
                Invoke-Native { & python reset-soon.py 1 }
            } finally {
                Pop-Location
            }
        }
        $deadline = (Get-Date).AddMinutes(5)
        do {
            if (Test-AndroidBooted) { break }
            if ((Get-Date) -gt $deadline) { throw 'Android did not return after the test' }
            Start-Sleep -Seconds 5
        } while ($true)
        "$(Get-Date -Format HH:mm:ss) LOADER_CHECK_DONE $Out"
    } finally {
        if ($peerJob) {
            if ($peerJob.State -eq 'Running') { Stop-Job -Job $peerJob }
            $peerState = $peerJob.State
            Receive-Job -Job $peerJob -ErrorAction Continue
            Remove-Job -Job $peerJob -Force
        }
        foreach ($path in $ownedLocks) {
            if ((Test-Path -LiteralPath $path) -and
                [System.IO.File]::ReadAllText($path) -eq $token) {
                Remove-Item -LiteralPath $path
            }
        }
        "$(Get-Date -Format HH:mm:ss) PHONE_RELEASED $Out"
    }
} finally {
    if ($fairplaydEnabledForRun) {
        $fpDisableOut = Join-Path $workspace "work\statusbar-fix\run-$Out-fp-disable"
        $fpGuestLog = Join-Path $fpDisableOut 'guest.log'
        $fpRestored = $false
        try {
            Invoke-Native { & python -X utf8 -B (Join-Path $workspace 'work\statusbar-fix\phone-readthrough.py') `
                --disable-job com.apple.fairplayd.H2 --output $fpDisableOut }
            if (Test-FairPlaydDisabledSafe -ExitCode $LASTEXITCODE -GuestLogPath $fpGuestLog -ExpectedSha $fairplaydDisabledLaunchdSha) {
                $fpRestored = $true
            } else {
                $fairplaydRestoreError = "FairPlayd disable failed (exit=$LASTEXITCODE) or LAUNCHD_AFTER SHA-256 != $fairplaydDisabledLaunchdSha"
            }
        } catch {
            $fairplaydRestoreError = $_.Exception.Message
        }
        if (-not $fpRestored) {
            $holdBody = "FAIRPLAYD-RESCUE-HOLD out=$Out time=$(Get-Date -Format o) reason=$fairplaydRestoreError`n"
            [System.IO.File]::WriteAllText($fairplaydRescueHold, $holdBody, [System.Text.Encoding]::UTF8)
            Write-Warning "Wrote independent rescue hold $fairplaydRescueHold : $fairplaydRestoreError"
        }
    }
}
if ($fairplaydRestoreError) {
    throw "Temporary FairPlayd restore failed; rescue hold written to ${fairplaydRescueHold}: $fairplaydRestoreError"
}
if ($peerState -and $peerState -ne 'Completed') {
    throw "Windows peer test ended with state $peerState"
}
