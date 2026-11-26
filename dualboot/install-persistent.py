#!/usr/bin/env python3
"""Install or roll back the verified common boot_a entry via the shared launcher."""
import argparse
import gzip
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import time


def load(name):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


phone = load("phone-check")
rescue = load("rescue-android")
BOOT = "/dev/block/by-name/boot_a"
# 已驗證的 boot_b 備援選單。
DAILY_CHAIN = phone.WORKSPACE / "work/boot-chain/chain-v9-dualboot-launching"
DUALBOOT = phone.WORKSPACE / "work/dualboot"
PIN = "05e0318d95875bf0879815fd8755f6f0e9ea9e20d3c9ce20e2e583ed8478f928"
LAUNCHING_PIN = "7977fbf0c8813ec16d76608bd24b1a930dda769fc1b2a8ed06c690fe013a2773"
COUNTDOWN_PIN = "1e1b149b40170bc55b891229cbec88cb03d9573892f614bdf1c9b508d54347a0"
DAILY_WIFI_PIN = "2327a2a93e37eca81e6d9c97c6807c489f44398fabbbde29976d75c149d9557f"
# The payload reached SpringBoard, Wi-Fi and Settings in the standalone RAM trial.
DAILY_WIFI_PAYLOAD = "784efe595e6e889814d9b88494197313e2aff8fd680dd0d36c500f1d40b4ca7e"
# 2026-11-20 日常藍牙版（U-023 fuxi,bt-fix＋原生藍牙、disp0 fuxi,mfb-align）。
# payload 已在 RAM 試開到 SpringBoard、藍牙 3.2 Mbaud 就緒、Wi-Fi 自動加入（native-hw000bt-daily-1120）。
DAILY_BT_PIN = "0c1a02c3a6a947731d943bc8dff51d779db01b734069be9b52d863d48569c124"
DAILY_BT_PAYLOAD = "6d8a0b7c450bea4928817c26cb7fd2874264ca304923526ca11db902275b8734"
# B-014 全寬日常版：填滿與 360×86 狀態欄，沿用 B-013 的其他選項。
DAILY_FIT_PIN = "2bc74cd7561d147f092e13d55e75adaed68992c276a9fa483880236512819e29"
DAILY_FIT_PAYLOAD = "11ae7cec2a23f31265060e9912ce745d18de254a3fafc8c28d27cf34e7a9e159"
# 1125×2500 兩軸均縮到 96%，趟 43 暫時開機驗證。
DAILY_ASPECT_PIN = "f2080657c34166b492533ddee82ffab79a7d8b11b6eb75d68e7ac5ce897bd8cf"
DAILY_ASPECT_PAYLOAD = "c6e87393379d2b884cafa4de67a3bf62e87ab9eb19f5d451b6c56cbaa6695928"
# B-016 動態 RTC 日常版（Plan A persist:/time/ats_2 + Plan B timed 持久化，沿用 B-015 全功能，不帶靜態 fuxi,rtc-offset）。
DAILY_RTC_PIN = "8758a8e62417e2edff1ce23d81f4ad9c26bc59e3f7dcefb9786480ea7b234a22"
DAILY_RTC_PAYLOAD = "6ea24bcb20921cb79ebad766e8f085d1ddc2c6764fafae2cfb3e3d232b0c04ec"
# B-017 音訊／麥克風／震動／右側音量條日常版（沿用 B-016 全功能，加入 CS35L41 mode 8＋idle-pdn=600、WCD9385 mic、HAP525 mode 9/hal 6、VolHUD 238×128）。
DAILY_AUDIO_HAP_PIN = "885b73f29e13f1bbbeadd41ae8e2b14cf157e94c1646907115603e777b714869"
DAILY_AUDIO_HAP_PAYLOAD = "4533de868349d0ce79a01c4ea82fd0f113b279b491c555359038158f663231b3"
# B-018 音訊／手電筒零轉子／hap8 核心日常原點（保留 B-017 全功能與好評之 damp=6，喇叭調為 gain=16/max_db=-4/idle-pdn=1500＋防靜音誤喚醒、audio-mic=1，對齊根映像 FuxiVibrate.c 修復並內建 mode 6/7/8）。
DAILY_AUDIO_HAP8_M6_PIN = "32c0b26f79db0aab4122bc2ae1dbfd267f757f60a6db1d180abed89a67e665b6"
DAILY_AUDIO_HAP8_M6_PAYLOAD = "230fafa76a7140d70ab3b856e9fc6a2485d1629f262762e0649f848509460018"
# B-019 系統模糊背景去紫修復日常版（完整保留 B-018 全功能並啟用 U-026 ADT fuxi,atx-fix=1）。
DAILY_ATXFIX_PIN = "a326460ef5747ed20ab3e50581fa3a745ccc95b9505fd08a53b6106085551f92"
DAILY_ATXFIX_PAYLOAD = "2c022930a8f3491db3f038ba24bf6208417f7b2cfe27273453322f7773e5b972"
# B-019/B-020 整合日常版（完整保留 B-018 與 U-026 去紫 atx-fix 44 項日常參數，疊加 4G＋VoLTE 行動數據與 K-079 WCN7850 SoftAP 個人熱點 ap7）。
DAILY_CELL_HOTSPOT_AP7_PIN = "fe1e9fcc6b059e05d0578c91cd3634a382fc664524f36137a4ede4ddf7da12dd"
DAILY_CELL_HOTSPOT_AP7_PAYLOAD = "b72c3a2737d1b288caa84355be78417b67e491555e06eff25b1c70c0864bfd33"
# B-020 大一統整合日常版（整合 B-019、hap9e Mode 9 v2.2、U-027 錄音聽寫、K-080/U-028 原生相機、感測器 v8、WDA 時序與關熱點 Wi-Fi 穩定性修復）。
DAILY_B020_PIN = "70f2b2fa0dcd3a34754c92f1a238e1b244a7e76605c8af59211f71b8c3d8946e"
DAILY_B020_PAYLOAD = "539d8596931b049bbf318fd6c9e98ee8e68c2c55887fc23bb7f5dce7e01a892b"
# B-021 無 Maps 日常版（整合 IMX800 四攝 C-AF／3.2x 零發紫／閃光燈、GNSS QMI_LOC+PDS、Wi-Fi 掃描與連線加速、GPU Class 0 1280MiB／120Hz TE、FuxiFlashlightLogic 控制中心修復）。
DAILY_B021_PIN = "babd50b6a48444f62a1e43beb4f33ebd387441e4c77b470b1eee69631eebd087"
DAILY_B021_PAYLOAD = "4b34b1c67b8c5b078a24dcefa1d733ec29abacdd168d2fadefe07f1acc99e544"
# B-022 無 Maps 日常版（整合 Wave 2 Pure 120Hz、Maps Class 1 quantum、M4-2 probe_0x4e、Option B A-1..A-5）。
DAILY_B022_PIN = "3f021363380fa117f933417c0afa48a09245072c8abfcc0c50e980fa20d313f4"
DAILY_B022_PAYLOAD = "43eff0a657bc6f35d2aef53282aad7f115b23961374f49615b7f6ed8479196be"
DAILY_PAYLOADS = {DAILY_WIFI_PIN: DAILY_WIFI_PAYLOAD, DAILY_BT_PIN: DAILY_BT_PAYLOAD,
                  DAILY_FIT_PIN: DAILY_FIT_PAYLOAD, DAILY_ASPECT_PIN: DAILY_ASPECT_PAYLOAD,
                  DAILY_RTC_PIN: DAILY_RTC_PAYLOAD, DAILY_AUDIO_HAP_PIN: DAILY_AUDIO_HAP_PAYLOAD,
                  DAILY_AUDIO_HAP8_M6_PIN: DAILY_AUDIO_HAP8_M6_PAYLOAD,
                  DAILY_ATXFIX_PIN: DAILY_ATXFIX_PAYLOAD,
                  DAILY_CELL_HOTSPOT_AP7_PIN: DAILY_CELL_HOTSPOT_AP7_PAYLOAD,
                  DAILY_B020_PIN: DAILY_B020_PAYLOAD,
                  DAILY_B021_PIN: DAILY_B021_PAYLOAD,
                  DAILY_B022_PIN: DAILY_B022_PAYLOAD}
# 只有已在 RAM 試開驗過 Android 交接的映像可以寫入：雜湊 → (產物, RAM 試開紀錄)。
VERIFIED_IMAGES = {
    PIN:
        (DUALBOOT / "menu-partial-refresh-20261118", DUALBOOT / "partial-refresh-ram-validation-20261118"),
    LAUNCHING_PIN:
        (DUALBOOT / "menu-launching-20261118", DUALBOOT / "launching-ram-validation-20261118"),
    COUNTDOWN_PIN:
        (DUALBOOT / "menu-android-countdown-current-20261118", DUALBOOT / "countdown-ram-validation-20261118"),
    DAILY_WIFI_PIN:
        (DUALBOOT / "menu-daily-wifi-sb103-20261118", DUALBOOT / "daily-wifi-sb103-ram-android-20261118"),
    DAILY_BT_PIN:
        (DUALBOOT / "menu-daily-bt-20261120", DUALBOOT / "daily-bt-ram-android-20261120"),
    DAILY_FIT_PIN:
        (DUALBOOT / "menu-daily-fit-20261120", DUALBOOT / "daily-fit-ram-android-20261120"),
    DAILY_ASPECT_PIN:
        (DUALBOOT / "menu-daily-aspect-20261120", DUALBOOT / "daily-aspect-ram-android-20261120"),
    DAILY_RTC_PIN:
        (DUALBOOT / "menu-daily-rtc-ab-20261122", DUALBOOT / "daily-rtc-ab-ram-android-20261122"),
    DAILY_AUDIO_HAP_PIN:
        (DUALBOOT / "menu-daily-audio-hap6f-20261122", DUALBOOT / "daily-audio-hap6f-ram-android-20261122"),
    DAILY_AUDIO_HAP8_M6_PIN:
        (DUALBOOT / "menu-daily-audio-hap8-m6-20261122", DUALBOOT / "daily-audio-hap8-m6-ram-android-20261122"),
    DAILY_ATXFIX_PIN:
        (DUALBOOT / "menu-daily-atxfix-20261123", DUALBOOT / "daily-atxfix-ram-android-20261123"),
    DAILY_CELL_HOTSPOT_AP7_PIN:
        (DUALBOOT / "menu-daily-cell-hotspot-ap7-20261123", DUALBOOT / "daily-cell-hotspot-ap7-ram-android-20261123"),
    DAILY_B020_PIN:
        (DUALBOOT / "menu-daily-b020-v2-20261124", DUALBOOT / "daily-b020-v2-ram-android-20261124"),
    DAILY_B021_PIN:
        (DUALBOOT / "menu-daily-b021-20261125", DUALBOOT / "daily-b021-ram-android-20261125"),
    DAILY_B022_PIN:
        (DUALBOOT / "menu-daily-b022-20261126", DUALBOOT / "daily-b022-ram-android-20261126"),
}
FIRST_INSTALL = "persistent-slot-a-20261118"
UPGRADE_PINS = {PIN: LAUNCHING_PIN, LAUNCHING_PIN: COUNTDOWN_PIN, COUNTDOWN_PIN: DAILY_WIFI_PIN,
                DAILY_WIFI_PIN: DAILY_BT_PIN, DAILY_BT_PIN: DAILY_FIT_PIN, DAILY_FIT_PIN: DAILY_ASPECT_PIN,
                DAILY_ASPECT_PIN: DAILY_RTC_PIN, DAILY_RTC_PIN: DAILY_AUDIO_HAP_PIN,
                DAILY_AUDIO_HAP_PIN: DAILY_AUDIO_HAP8_M6_PIN,
                DAILY_AUDIO_HAP8_M6_PIN: DAILY_CELL_HOTSPOT_AP7_PIN,
                DAILY_ATXFIX_PIN: DAILY_CELL_HOTSPOT_AP7_PIN,
                DAILY_CELL_HOTSPOT_AP7_PIN: DAILY_B020_PIN,
                DAILY_B020_PIN: DAILY_B021_PIN,
                DAILY_B021_PIN: DAILY_B022_PIN}
# 保留手動選取 iOS／Recovery 與返回 Android 的操作時間。
IOS_RETURN_SECONDS = 900
RESUME_RETURN_SECONDS = 1800
RECOVERY_SECONDS = 300
MENU_CHOICE_SECONDS = 300
IOS_TIMEOUT_ERROR = "Android did not return within the observation window"
IOS_NOT_OBSERVED = "No fresh installed iOS boot to SpringBoard"


def part_sha(name):
    return phone.rooted("sha256sum /dev/block/by-name/" + name, timeout=45).split()[0]


def stable_android():
    state = phone.android_state()
    phone.need(state["active_slot"] == "0", "Expected active slot a")
    phone.need(phone.rooted("bootctl is-slot-marked-successful 0 && echo successful") == "successful",
               "Slot a is not marked successful")
    return state


def in_fastboot():
    return any(line.split() == [phone.SERIAL, "fastboot"]
               for line in phone.command("fastboot", "devices", timeout=6).splitlines())


def wait_fastboot(seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if in_fastboot():
            return
        time.sleep(2)
    raise RuntimeError("Fastboot did not appear")


def assert_fastboot(size):
    def get(name):
        return rescue.getvar(phone, name)

    phone.need(in_fastboot() and get("product") == "fuxi" and get("current-slot") == "a",
               "Expected device in slot-a Fastboot")
    phone.need(get("is-userspace") == "no" and get("unlocked") == "yes",
               "Expected unlocked bootloader Fastboot")
    phone.need(int(get("max-download-size"), 0) >= size and int(get("partition-size:boot_a"), 0) == size,
               "Fastboot cannot restore the complete boot_a image")


def stage_original(out, info):
    archive = Path(info["file"]).resolve()
    phone.need(phone.digest(archive) == info["file_sha256"], "Backup archive hash changed")
    original = out / "boot-a-original.img"
    sha = hashlib.sha256()
    with gzip.open(archive, "rb") as source, original.open("xb") as target:
        while chunk := source.read(1024 * 1024):
            target.write(chunk)
            sha.update(chunk)
    phone.need(original.stat().st_size == info["bytes"] and sha.hexdigest() == info["raw_sha256"]
               and phone.digest(original) == info["raw_sha256"], "Restoration image verification failed")
    return original


def prepare_candidate(original, pin=PIN):
    candidate, trial_dir = VERIFIED_IMAGES[pin]
    report = json.loads((candidate / "report.json").read_text(encoding="utf-8"))
    trial = json.loads((trial_dir / "report.json").read_text(encoding="utf-8"))
    image = candidate / "boot.img"
    size = image.stat().st_size
    # 首版的試開紀錄沒有記映像雜湊；之後的版本要求試開紀錄綁定同一份映像。
    trial_pin = trial.get("validation", {}).get("candidate_sha256")
    phone.need(report["result"] == trial["result"] == "ok" and trial["action"] == "android" and
               report["candidate"]["sha256"] == phone.digest(image) == pin and
               (trial_pin == pin or (pin == PIN and trial_pin is None)) and
               trial["validation"]["kernel_code_address"] == hex(0xa8000000 + report["candidate"]["linux_offset"] + 0x10000) and
               size == report["candidate"]["bytes"] and size < original.stat().st_size,
               "Only the verified RAM trial image may be installed")
    if pin == COUNTDOWN_PIN or pin in DAILY_PAYLOADS:
        phone.need(report["timeout_seconds"] == trial["validation"].get("configured_android_timeout_seconds") == 10,
                   "The RAM trial must use the 10-second Android countdown")
    sha = hashlib.sha256(image.read_bytes())
    with original.open("rb") as stream:
        stream.seek(size)
        while chunk := stream.read(1024 * 1024):
            sha.update(chunk)
    return image, report, sha.hexdigest()


def write_image(image, expected, remote):
    phone.adb("push", image, remote, timeout=90)
    phone.need(phone.rooted("sha256sum " + remote).split()[0] == phone.digest(image),
               "Phone staging hash differs")
    phone.rooted(f"dd if={remote} of={BOOT} bs=4194304 conv=fsync && sync", timeout=90)
    phone.need(part_sha("boot_a") == expected, "Full boot_a readback differs")
    phone.rooted("rm -f " + remote)


def verify_ios_source(report, image_pin):
    image = DAILY_CHAIN / "boot.img"
    proven = json.loads((DAILY_CHAIN / "menu-build-report.json").read_text(encoding="utf-8"))
    pin = phone.digest(image)
    size = image.stat().st_size
    phone.need(0 < size <= 201326592 and pin == proven["candidate"]["sha256"] and
               phone.rooted(f"head -c {size} /dev/block/by-name/boot_b | sha256sum", timeout=120).split()[0] == pin,
               "The boot_b fallback image no longer matches")
    expected_payload = DAILY_PAYLOADS.get(image_pin, proven["payload_sha256"])
    phone.need(report["candidate"]["sha256"] == image_pin and report["payload_sha256"] == expected_payload,
               "The iOS payload differs from the validated image")
    mapping = "CLASSPATH=/data/local/tmp/codex-managed-native-image.jar app_process /system/bin ManagedNativeImage"
    phone.need("mapped=false" in phone.rooted(mapping + " status"), "The iOS mapping is not idle")
    phone.verify_ios_root()


def restore_original(original, expected, token, report):
    phone.require_owned_locks(token)
    if phone.digest(original) != expected:
        raise RuntimeError("Original image changed on the host")
    try:
        android = phone.adb("shell", "getprop sys.boot_completed", timeout=5) == "1"
    except (RuntimeError, subprocess.TimeoutExpired):
        android = False
    if android:
        stable_android()
        write_image(original, expected, "/data/local/tmp/fuxi-original-boot-a.img")
        phone.adb("reboot")
    else:
        phone.say("ROLLBACK_WAITING_FOR_FASTBOOT")
        wait_fastboot(150)
        phone.require_owned_locks(token)
        assert_fastboot(original.stat().st_size)
        phone.command("fastboot", "-s", phone.SERIAL, "flash", "boot_a", original, timeout=120)
        phone.command("fastboot", "-s", phone.SERIAL, "reboot", timeout=15)
    after = phone.wait_for_android(MENU_CHOICE_SECONDS)
    phone.need(after["active_slot"] == "0" and part_sha("boot_a") == expected, "Restoration readback differs")
    # 首次安裝回退到原廠；升級回退到上一版已驗證選單。
    report["rollback"] = "PREVIOUS_BOOT_A_RESTORED_AND_VERIFIED"
    phone.say("PREVIOUS_BOOT_A_RESTORED")


def install(original, backup, out, token, result, save):
    before = stable_android()
    image, report, expected = prepare_candidate(original)
    phone.need(part_sha("boot_a") == backup["boot_a"]["raw_sha256"], "Current boot_a differs from restore point")
    protected = {name: part_sha(name) for name in ("boot_b", "recovery_a", "recovery_b", "init_boot_a", "vendor_boot_a")}
    for name in ("recovery_a", "recovery_b", "init_boot_a", "vendor_boot_a"):
        phone.need(protected[name] == backup[name]["raw_sha256"], "Phone differs from the restore point: " + name)
    verify_ios_source(report, PIN)
    phone.preserve_mtd(out)
    result.update(candidate_sha256=PIN, expected_full_boot_a_sha256=expected, protected_sha256=protected,
                  slot=before["slot"], stage="CHECKING_FASTBOOT_ROLLBACK")
    save()
    phone.say("CHECKING_FASTBOOT_ROLLBACK")
    phone.adb("reboot", "bootloader")
    wait_fastboot(65)
    try:
        assert_fastboot(original.stat().st_size)
    finally:
        phone.command("fastboot", "-s", phone.SERIAL, "reboot", timeout=15)
    phone.wait_for_android(MENU_CHOICE_SECONDS)
    stable_android()
    phone.require_owned_locks(token)
    phone.need(part_sha("boot_a") == backup["boot_a"]["raw_sha256"], "boot_a changed during preflight")
    result["stage"] = "WRITE_POSSIBLE"
    save()
    write_image(image, expected, "/data/local/tmp/fuxi-common-menu.img")
    result["stage"] = "WRITTEN_AND_VERIFIED"
    save()
    phone.say("BOOT_A_READBACK_OK; REBOOT_TO_INSTALLED_MENU")
    phone.adb("reboot")
    # Allow the old adbd to go away before accepting sys.boot_completed.
    time.sleep(6)
    phone.say("INSTALLED_MENU_TEST_ANDROID")
    after = phone.wait_for_android(MENU_CHOICE_SECONDS)
    handoff = phone.verify_android_handoff(report, after)
    stable_android()
    phone.need(part_sha("boot_a") == expected, "Installed image changed after boot")
    for name, sha in protected.items():
        phone.need(part_sha(name) == sha, "Untargeted partition changed: " + name)
    result.update(android=handoff, stage="INSTALLED_ANDROID_VERIFIED")
    phone.say("INSTALLED_ANDROID_VERIFIED")


def upgrade(original, out, token, result, save, install_name):
    """Upgrade the verified boot_a menu and retain its complete rollback image."""
    previous = json.loads((DUALBOOT / install_name / "report.json").read_text(encoding="utf-8"))
    previous_pin = previous["candidate_sha256"]
    phone.need(previous["result"] == "INSTALLED_ANDROID_VERIFIED" and previous_pin in UPGRADE_PINS,
               "No verified installation to upgrade from")
    next_pin = UPGRADE_PINS[previous_pin]
    before = stable_android()
    old_image, _, old_expected = prepare_candidate(original, previous_pin)
    phone.need(old_expected == previous["expected_full_boot_a_sha256"] and part_sha("boot_a") == old_expected,
               "boot_a differs from the verified menu installation")
    # 回退映像由前一版選單和原廠尾段重組，再比對整個分區。
    rollback = out / "boot-a-previous-menu.img"
    with rollback.open("xb") as target, original.open("rb") as tail:
        target.write(old_image.read_bytes())
        tail.seek(old_image.stat().st_size)
        while chunk := tail.read(1024 * 1024):
            target.write(chunk)
    phone.need(phone.digest(rollback) == old_expected and
               rollback.stat().st_size == original.stat().st_size, "Rollback image reconstruction differs")
    image, report, expected = prepare_candidate(original, next_pin)
    expected_protected = previous["protected_sha256"].copy()
    protected = {name: part_sha(name) for name in expected_protected}
    verify_ios_source(report, next_pin)
    if previous_pin == LAUNCHING_PIN:
        # The verified chain-v9 deployment took place after the launching-menu installation.
        expected_protected["boot_b"] = protected["boot_b"]
    phone.need(protected == expected_protected, "Untargeted partitions changed since the verified installation")
    phone.preserve_mtd(out)
    phone.require_owned_locks(token)
    phone.need(part_sha("boot_a") == old_expected, "boot_a changed before upgrade")
    result.update(upgrade_from=install_name, candidate_sha256=next_pin, expected_full_boot_a_sha256=expected,
                  protected_sha256=protected, slot=before["slot"], rollback_image=str(rollback),
                  rollback_sha256=old_expected, stage="WRITE_POSSIBLE")
    save()
    write_image(image, expected, "/data/local/tmp/fuxi-common-menu.img")
    result["stage"] = "WRITTEN_AND_VERIFIED"
    save()
    phone.say("BOOT_A_READBACK_OK; REBOOT_TO_UPGRADED_MENU")
    phone.adb("reboot")
    time.sleep(6)
    countdown = report.get("timeout_seconds", 0)
    phone.say(f"UPGRADED_MENU_TEST_ANDROID: automatic boot after {countdown} seconds" if countdown else
              "UPGRADED_MENU_TEST_ANDROID")
    after = phone.wait_for_android(MENU_CHOICE_SECONDS)
    handoff = phone.verify_android_handoff(report, after)
    stable_android()
    phone.need(part_sha("boot_a") == expected, "Upgraded image changed after boot")
    for name, sha in protected.items():
        phone.need(part_sha(name) == sha, "Untargeted partition changed: " + name)
    result.update(android=handoff, stage="INSTALLED_ANDROID_VERIFIED")
    phone.say("UPGRADED_ANDROID_VERIFIED")


def check_installed(install_report, image_report):
    state = stable_android()
    handoff = phone.verify_android_handoff(image_report, state)
    phone.need(part_sha("boot_a") == install_report["expected_full_boot_a_sha256"],
               "Installed boot_a differs")
    for name, sha in install_report["protected_sha256"].items():
        phone.need(part_sha(name) == sha, "Untargeted partition changed: " + name)
    return handoff


def resumable_ios_run(resume):
    """Reuse the saved pre-iOS console when the user retries the menu choice."""
    prior = json.loads((resume / "report.json").read_text(encoding="utf-8"))
    phone.need(prior.get("stage") == "REBOOTING_TO_TEST_IOS" and prior.get("result") == "FAILED" and
               prior.get("error") in (IOS_TIMEOUT_ERROR, IOS_NOT_OBSERVED) and
               prior.get("lock_state") == "released" and "resumed_by" not in prior,
               "Earlier run does not have a released iOS observation to resume")
    with gzip.open(resume / "previous-mtdblk.img.gz", "rb") as stream:
        before_mtd = hashlib.file_digest(stream, "sha256").hexdigest()
    phone.need((resume / "previous-console.txt").is_file(), "Earlier run has no pre-iOS console")
    return before_mtd


def validate_installation(out, token, result, save, resume=None, install_name=FIRST_INSTALL, ios_only=False):
    install_report = json.loads((DUALBOOT / install_name / "report.json").read_text(encoding="utf-8"))
    pin = install_report.get("candidate_sha256")
    phone.need(install_report["result"] == "INSTALLED_ANDROID_VERIFIED" and pin in VERIFIED_IMAGES and
               phone.digest(VERIFIED_IMAGES[pin][0] / "boot.img") == pin,
               "No verified persistent installation")
    result.update(install_report=install_name, candidate_sha256=pin)
    image_report = json.loads((VERIFIED_IMAGES[pin][0] / "report.json").read_text(encoding="utf-8"))
    if resume is None:
        check_installed(install_report, image_report)
        verify_ios_source(image_report, pin)
        before_mtd = phone.preserve_mtd(out)["raw_sha256"]
        before_dir = out
        phone.require_owned_locks(token)
        result["stage"] = "REBOOTING_TO_TEST_IOS"
        save()
        phone.adb("reboot")
        time.sleep(6)
        phone.say("INSTALLED_MENU_TEST_IOS")
        phone.wait_for_android(IOS_RETURN_SECONDS)
    else:
        # 手機在 iOS：不重開，等使用者經選單回 Android 後再核對同一次 iOS 開機。
        before_mtd = resumable_ios_run(resume)
        # 標記舊紀錄已被接續，避免之後拿同一份 iOS 前紀錄比對另一次開機。
        earlier = json.loads((resume / "report.json").read_text(encoding="utf-8"))
        phone.need(earlier.get("candidate_sha256") == pin and earlier.get("install_report") == install_name,
                   "Earlier iOS observation used a different installation")
        earlier["resumed_by"] = out.name
        (resume / "report.json").write_text(json.dumps(earlier, ensure_ascii=False, indent=2) + "\n",
                                            encoding="utf-8")
        before_dir = resume
        result.update(resumed_from=str(resume), stage="WAITING_FOR_ANDROID_AFTER_IOS")
        save()
        phone.say("WAITING_FOR_ANDROID_AFTER_IOS")
        phone.wait_for_android(RESUME_RETURN_SECONDS)
    result["android_after_ios"] = check_installed(install_report, image_report)
    after_mtd = phone.preserve_mtd(out / "after-ios")["raw_sha256"]
    log = (out / "after-ios/previous-console.txt").read_text(encoding="utf-8", errors="replace")
    prior = (before_dir / "previous-console.txt").read_text(encoding="utf-8", errors="replace")
    boot_number = re.search(r"console to mtdblk .*boot #(\d+)", log)
    previous_boot_number = re.search(r"console to mtdblk .*boot #(\d+)", prior)
    phone.need(after_mtd != before_mtd and boot_number and
               (not previous_boot_number or boot_number[1] != previous_boot_number[1]) and
               "exec SpringBoard" in log and "panic(cpu" not in log, IOS_NOT_OBSERVED)
    result["ios_boot_number"] = int(boot_number[1])
    result["stage"] = "INSTALLED_IOS_AND_RETURN_VERIFIED"
    save()
    phone.say("INSTALLED_IOS_AND_RETURN_VERIFIED")
    if ios_only:
        return
    phone.require_owned_locks(token)
    phone.adb("reboot")
    time.sleep(6)
    phone.say("INSTALLED_MENU_TEST_RECOVERY")
    deadline = time.monotonic() + RECOVERY_SECONDS
    while time.monotonic() < deadline:
        phone.require_owned_locks(token)
        try:
            version = phone.adb("shell", "getprop ro.twrp.version", timeout=5)
        except (RuntimeError, subprocess.TimeoutExpired):
            version = ""
        if version:
            result["twrp"] = version
            save()
            phone.say("INSTALLED_TWRP_VERIFIED")
            phone.adb("reboot")
            break
        time.sleep(2)
    else:
        raise RuntimeError("TWRP did not appear over ADB")
    time.sleep(6)
    phone.say("INSTALLED_MENU_TEST_RECOVERY_RETURN")
    phone.wait_for_android(RECOVERY_SECONDS)
    result["android_after_recovery"] = check_installed(install_report, image_report)
    result["stage"] = "INSTALLED_DUALBOOT_AND_RECOVERY_VERIFIED"
    phone.say(result["stage"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--from-launcher", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--validate", action="store_true")
    parser.add_argument("--ios-only", action="store_true", help="Validate the installed iOS boot and Android return")
    parser.add_argument("--resume-after-ios")
    parser.add_argument("--upgrade", action="store_true")
    parser.add_argument("--install-report", default=FIRST_INSTALL)
    args = parser.parse_args()
    phone.need(not (args.upgrade and args.validate) and (not args.ios_only or args.validate) and
               re.fullmatch(r"[a-z0-9-]+", args.install_report) and
               (args.validate or args.upgrade or args.install_report == FIRST_INSTALL), "Invalid mode combination")
    phone.need(args.from_launcher == str(os.getppid()) and re.fullmatch(r"[a-z0-9-]+", args.out),
               "Start through launch-loader-check.ps1")
    resume = None
    if args.resume_after_ios is not None:
        phone.need(args.validate and re.fullmatch(r"[a-z0-9-]+", args.resume_after_ios) and
                   args.resume_after_ios != args.out, "Invalid resume run")
        resume = phone.WORKSPACE / "work/dualboot" / args.resume_after_ios
        resumable_ios_run(resume)
    receipt = json.loads(phone.RESTORE_RECEIPT.read_text(encoding="utf-8"))
    restore = Path(receipt["restore_point"]).resolve()
    phone.need(restore.is_relative_to(phone.WORKSPACE.parent / "fuxi_Backup"), "Unexpected restore point")
    backup = json.loads((restore / "phone/manifest.json").read_text(encoding="utf-8"))["partitions"]
    out = phone.WORKSPACE / "work/dualboot" / args.out
    out.mkdir(exist_ok=False)
    result = {"started": phone.stamp(), "stage": "RESTORE_POINT_READY", "restore_point": str(restore)}
    original = None
    if not args.validate:
        original = stage_original(out, backup["boot_a"])
        if not args.upgrade:
            result.update(rollback_image=str(original), rollback_sha256=backup["boot_a"]["raw_sha256"])

    def save():
        temp = out / "report.tmp"
        temp.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        temp.replace(out / "report.json")

    save()
    token = f"persistent dualboot pid={os.getpid()} {phone.stamp()}"
    owned = []
    try:
        for path in (*phone.LOCKS, phone.HOLD):
            phone.need(not path.exists(), "Phone is reserved: " + str(path))
        for path in phone.LOCKS:
            with path.open("x", encoding="utf-8") as stream:
                stream.write(token)
            owned.append(path)
        phone.require_owned_locks(token)
        if args.validate:
            validate_installation(out, token, result, save, resume, args.install_report, args.ios_only)
        elif args.upgrade:
            upgrade(original, out, token, result, save, args.install_report)
        else:
            install(original, backup, out, token, result, save)
        result["result"] = result["stage"]
    except BaseException as error:
        result["error"] = str(error)
        result["result"] = "FAILED"
        if result.get("rollback_image") and result["stage"] in ("WRITE_POSSIBLE", "WRITTEN_AND_VERIFIED"):
            try:
                restore_original(Path(result["rollback_image"]), result["rollback_sha256"], token, result)
                result["result"] = "ROLLED_BACK"
            except BaseException as rollback_error:
                result["rollback_error"] = str(rollback_error)
                result["result"] = "ROLLBACK_NEEDS_ATTENTION"
        raise
    finally:
        result["finished"] = phone.stamp()
        result["lock_token"] = token
        result["lock_state"] = "retained" if result["result"] == "ROLLBACK_NEEDS_ATTENTION" else "released"
        save()
        if result["lock_state"] == "released":
            for path in reversed(owned):
                if path.is_file() and path.read_text(encoding="utf-8") == token:
                    path.unlink()


if __name__ == "__main__":
    main()
