#!/usr/bin/env python3
"""Phone validation through launch-loader-check.ps1 only.

Reuses the boot-chain deployment backup/hash pattern and the launcher's three
locks. Backup and Android-only RAM boot do not write any phone boot partition.
"""
import argparse
from datetime import datetime
from zoneinfo import ZoneInfo
import gzip
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

WORKSPACE = Path(__file__).resolve().parents[1]
SERIAL = os.environ.get('FUXI_ANDROID_SERIAL', '')
LOCKS = [WORKSPACE / "work/PHONE-LOCK-000", WORKSPACE / "work/hybrid-driver/HOLD-PHONE"]
HOLD = WORKSPACE / "work/adsp-battery/hw000/HOLD"
PARTITIONS = ("mtdblk", "boot_a", "boot_b", "init_boot_a", "init_boot_b",
              "vendor_boot_a", "vendor_boot_b", "dtbo_a", "dtbo_b",
              "recovery_a", "recovery_b", "vbmeta_a", "vbmeta_b", "misc")
RESTORE_RECEIPT = WORKSPACE / "work/dualboot/restore-point-current.json"
ROOT_IMAGE = "/data/gsi/codexnative1108b/codexnative_root_1108b.img.0000"
ROOT_MAPPER = "/dev/block/mapper/codexnative_root_1108b"
# 2026-11-18 起手機上的 KSU 模組 fuxi_ios_boot 已移除：boot_a 常駐選單，重開即可選 iOS。
# 模組原本的檢查（iOS 根映像區塊表、pin、TWRP 雜湊）改由電腦端這份參考資料與工具執行；
# expected-fiemap.txt 由 work/ksu-ios-entry/build-module.py 產生時已核對過 loader ADT 的根映像區塊。
IOS_ENTRY = WORKSPACE / "work/ksu-ios-entry/fuxi_ios_boot"
KSU_MODULE = "/data/adb/modules/fuxi_ios_boot"
KSU_MODULE_UPDATE = "/data/adb/modules_update/fuxi_ios_boot"


def stamp():
    return datetime.now(ZoneInfo("Asia/Taipei")).isoformat(timespec="seconds")


def need(ok, message):
    if not ok:
        raise RuntimeError(message)


def say(message):
    print(stamp(), message, flush=True)


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def command(*argv, timeout=60):
    result = subprocess.run([str(x) for x in argv], capture_output=True, timeout=timeout)
    need(result.returncode == 0, f"Command failed ({result.returncode}): {argv[0]} {result.stderr.decode('utf-8', 'replace')[-1000:]}")
    return result.stdout.decode("utf-8", "replace").replace("\r", "").strip()


def adb(*args, timeout=60):
    return command("adb", "-s", SERIAL, *args, timeout=timeout)


def rooted(script, timeout=60):
    need("'" not in script, "Unexpected shell quote")
    return adb("shell", "su -c '" + script + "'", timeout=timeout)


def android_state():
    need(adb("shell", "getprop sys.boot_completed") == "1", "Android must be fully booted")
    need(adb("shell", "getprop ro.product.device") == "fuxi", "Not the expected Fuxi device")
    need(rooted("id -u") == "0", "Root access unavailable")
    sku = rooted("od -An -t u1 /sys/firmware/devicetree/base/memory/ddr_size").split()
    need(sku == ["0", "0", "0", "12"], "Only the measured 12GB SKU is supported")
    slot = adb("shell", "getprop ro.boot.slot_suffix")
    need(slot == "_a", "Temporary validation requires the original Android slot a")
    return {"slot": slot, "active_slot": rooted("bootctl get-active-boot-slot"),
            "kernel": rooted("cat /proc/version"), "iomem": rooted("cat /proc/iomem"),
            "captured_at": stamp()}


def phone_exists(path):
    return rooted(f"[ -e {path} ] && echo present || echo absent") == "present"


def run_ios_tool(name):
    """把電腦上的唯讀檢查工具暫放手機執行一次，核對雜湊，用完刪除。"""
    local = IOS_ENTRY / "bin" / name
    remote = "/data/local/tmp/fuxi-" + name
    adb("push", local, remote)
    try:
        need(rooted("sha256sum " + remote).split()[0] == digest(local), "Phone copy differs: " + name)
        return rooted(f"chmod 755 {remote} && {remote} {ROOT_IMAGE}")
    finally:
        rooted("rm -f " + remote)


def verify_ios_root():
    """取代 KSU 模組 action.sh 的 [2/5]：iOS 根映像未被對應、區塊位置與 loader 內建相同、已固定。"""
    need(not phone_exists(ROOT_MAPPER), "The iOS root image is mapped by Android")
    expected = (IOS_ENTRY / "expected-fiemap.txt").read_text(encoding="utf-8").strip()
    fiemap = run_ios_tool("inspect-fiemap")
    need(fiemap and fiemap == expected, "The daily iOS root extents have changed")
    pin = run_ios_tool("check-pin")
    need("flags_rc=0 flags=0x800000 " in pin and "pin_rc=0 moved=0 " in pin, "iOS root is not pinned")
    say("IOS_ROOT_EXTENTS_AND_PIN_OK")
    return {"fiemap_sha256": hashlib.sha256(fiemap.encode()).hexdigest(), "pin": pin}


def capture_raw(remote, target, expected_size):
    before = rooted(f"sha256sum {remote}", timeout=180).split()[0]
    p = subprocess.Popen(["adb", "-s", SERIAL, "exec-out", f'su -c "cat {remote}"'], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    checksum = hashlib.sha256()
    total = 0
    last = time.monotonic()
    with gzip.open(target, "xb", compresslevel=1) as compressed:
        while chunk := p.stdout.read(1024 * 1024):
            checksum.update(chunk)
            total += len(chunk)
            compressed.write(chunk)
            if time.monotonic() - last >= 10:
                say(f"BACKUP_PROGRESS {target.name} {total // 1048576} MiB")
                last = time.monotonic()
    need(p.wait(timeout=30) == 0, "Phone backup stream failed")
    need(total == expected_size and checksum.hexdigest() == before, "Backup size/hash mismatch")
    need(rooted(f"sha256sum {remote}", timeout=180).split()[0] == before, "Phone source changed during backup")
    with gzip.open(target, "rb") as stream:
        need(hashlib.file_digest(stream, "sha256").hexdigest() == before, "Compressed backup readback mismatch")
    return {"device": remote, "bytes": total, "raw_sha256": before, "file": str(target), "file_sha256": digest(target)}


def preserve_mtd(out):
    out.mkdir(parents=True, exist_ok=True)
    path = out / "previous-mtdblk.img.gz"
    size = int(rooted("blockdev --getsize64 /dev/block/by-name/mtdblk"))
    record = capture_raw("/dev/block/by-name/mtdblk", path, size)
    area = out / "previous-mtd-area.bin"
    with gzip.open(path, "rb") as stream:
        area.write_bytes(stream.read(2049 * 4096))
    command(sys.executable, "-X", "utf8", WORKSPACE / "work/adsp-battery/hw000/fuxi-log-parse.py", area, out / "previous-console.txt")
    say("PREVIOUS_MTD_SAVED")
    return record


def backup_phone(restore, out, state):
    target = restore / "phone"
    target.mkdir(exist_ok=False)
    records = {}
    # Save the previous boot log before any new iOS boot can overwrite it.
    records["mtdblk"] = preserve_mtd(target)
    for name in PARTITIONS[1:]:
        remote = "/dev/block/by-name/" + name
        size = int(rooted(f"blockdev --getsize64 {remote}"))
        need(0 < size <= 256 * 1048576, f"Unexpected partition size: {name}")
        say(f"BACKUP_BEGIN {name} {size // 1048576} MiB")
        records[name] = capture_raw(remote, target / (name + ".img.gz"), size)
        say(f"BACKUP_VERIFIED {name}")
    module_archive = None  # 2026-11-18 起模組已移除；仍在的舊手機才封存
    if phone_exists(KSU_MODULE):
        archive = target / "fuxi-ios-boot-module.tar"
        with archive.open("xb") as stream:
            subprocess.run(["adb", "-s", SERIAL, "exec-out", 'su -c "tar -c -C /data/adb/modules fuxi_ios_boot"'],
                           stdout=stream, check=True, timeout=60)
        module_archive = {"file": str(archive), "sha256": digest(archive)}
    record = {"captured_at": stamp(), "before": state, "partitions": records,
              "module_archive": module_archive,
              "ios_root": "NOT_BACKED_UP; do not start iOS before a separate current root snapshot",
              "boot_partition_writes": 0, "slot_changes": 0}
    (target / "manifest.json").write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    (target / "RESTORE.txt").write_text(
        "Verified current phone boot partitions, misc, mtd log and (if installed) KSU module.\n"
        "Use manifest.json for exact partition sizes and raw/compressed SHA256.\n"
        "No partition was modified by this capture. Current iOS root has not been backed up.\n"
        "Any future restore must enter through launch-loader-check.ps1 and verify current preimages/write readback.\n",
        encoding="utf-8", newline="\n")
    return record


# 常駐選單已寫入 boot_a 後，RAM 試開仍要能跑：接受原廠或這些已驗證安裝讀回的整顆雜湊。
INSTALLED_BOOT_A_REPORTS = ("persistent-slot-a-20261118", "persistent-slot-a-upgrade-launching-20261118",
                            "persistent-slot-a-upgrade-countdown-20261118",
                            "persistent-slot-a-upgrade-wifi-sb103-20261118",
                            "persistent-slot-a-upgrade-daily-bt-20261120",
                            "persistent-slot-a-upgrade-daily-fit-20261120",
                            "persistent-slot-a-upgrade-daily-aspect-20261120",
                            "persistent-slot-a-upgrade-daily-rtc-ab-20261122",
                            "persistent-slot-a-upgrade-daily-audio-hap6f-20261122",
                            "persistent-slot-a-upgrade-daily-audio-hap8-m6-20261122",
                            "persistent-slot-a-upgrade-daily-atxfix-20261123",
                            "persistent-slot-a-upgrade-daily-cell-hotspot-ap7-20261123",
                            "persistent-slot-a-upgrade-daily-b020-v2-20261124",
                            "persistent-slot-a-upgrade-daily-b021-20261125")


def installed_boot_a_shas():
    shas = set()
    for name in INSTALLED_BOOT_A_REPORTS:
        path = WORKSPACE / "work/dualboot" / name / "report.json"
        if path.is_file():
            report = json.loads(path.read_text(encoding="utf-8"))
            if report.get("result") == "INSTALLED_ANDROID_VERIFIED":
                shas.add(report["expected_full_boot_a_sha256"])
    return shas


def verify_backup(restore, check_boot_b=True, allow_installed_boot_a=False):
    record = json.loads((restore / "phone/manifest.json").read_text(encoding="utf-8"))
    if RESTORE_RECEIPT.is_file():
        receipt = json.loads(RESTORE_RECEIPT.read_text(encoding="utf-8"))
        if Path(receipt["restore_point"]).resolve() == restore:
            record["ios_root"] = receipt["ios_root"]
    for name in ("boot_a", "boot_b", "init_boot_a", "init_boot_b", "vendor_boot_a", "vendor_boot_b"):
        if name == "boot_b" and not check_boot_b:
            continue
        item = record["partitions"][name]
        need(digest(Path(item["file"])) == item["file_sha256"], "Saved backup changed: " + name)
        allowed = {item["raw_sha256"]}
        if name == "boot_a" and allow_installed_boot_a:
            allowed |= installed_boot_a_shas()
        need(rooted("sha256sum " + item["device"]).split()[0] in allowed, "Current phone differs from restore point: " + name)
    return record


def require_owned_locks(token):
    need(not HOLD.exists(), "Phone work was paused")
    for path in LOCKS:
        need(path.is_file() and path.read_text(encoding="utf-8") == token,
             "Phone lock ownership changed: " + str(path))


def capture_rtc_owned(out, token):
    require_owned_locks(token)
    path = WORKSPACE / "work/adsp-battery/rtc-calibration.py"
    spec = importlib.util.spec_from_file_location("dualboot_rtc_calibration", path)
    rtc = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(rtc)
    # The existing API is also used by the locked loader safety verifier.
    # Keep standalone CLI's foreign-lock rejection intact; validate our exact
    # ownership before and after this read-only call instead of dropping locks.
    record = rtc.capture()
    require_owned_locks(token)
    calibration = out / "rtc-calibration.json"
    with calibration.open("x", encoding="utf-8", newline="\n") as stream:
        stream.write(json.dumps(record, ensure_ascii=False, indent=2) + "\n")
    say("RTC_CALIBRATED_WITH_OWNED_LOCKS")
    return calibration


def finalize_restore(restore, out, token):
    record = verify_backup(restore)
    target = restore / "ios-root"
    readme = (target / "README.txt").read_text(encoding="utf-8")
    whole = re.search(r"整份 sha256 ([a-f0-9]{64})", readme)
    pull = re.search(r"pull.bin（sha256 ([a-f0-9]{64})）", readme)
    need(whole and pull, "The existing iOS snapshot did not finalize")
    need(digest(target / "pull.bin") == pull[1], "Existing iOS delta hash differs")
    tool = "CLASSPATH=/data/local/tmp/codex-managed-native-image.jar app_process /system/bin ManagedNativeImage"
    need("mapped=true" not in rooted(tool + " status"), "A foreign iOS mapping is active")
    say("VERIFY_EXISTING_IOS_RESTORE_POINT: use the same raw mapper as the backup")
    current = None
    try:
        rooted(tool + " map", timeout=120)
        subprocess.run([sys.executable, "-X", "utf8", WORKSPACE / "work/xiaomi13-ios-gpu-20261108/verify-native-map.py"], check=True, timeout=120)
        mapper = "/dev/block/mapper/codexnative_root_1108b"
        need(int(rooted(f"blockdev --getsize64 {mapper}")) == 21474836480, "iOS mapper size differs")
        current = rooted(f"sha256sum {mapper}", timeout=240).split()[0]
        diagnostic = {"source": mapper, "expected_snapshot_sha256": whole[1], "current_sha256": current}
        (out / "root-comparison.json").write_text(json.dumps(diagnostic, indent=2) + "\n", encoding="utf-8", newline="\n")
        need(current == whole[1], "Raw iOS root changed since the restore point; stop before boot")
    finally:
        rooted(tool + " unmap", timeout=60)
        need("mapped=true" not in rooted(tool + " status"), "iOS root mapping did not unmap")
        say("IOS_ROOT_UNMAPPED")
    need(rooted("sha256sum /dev/block/by-name/mtdblk").split()[0] == record["partitions"]["mtdblk"]["raw_sha256"],
         "An iOS boot changed the saved log after backup")
    root = {"snapshot": str(target), "readme_sha256": digest(target / "README.txt"),
            "blocks_sha256": digest(target / "phone-blocks.txt"), "whole_sha256": current,
            "pull_sha256": pull[1], "base": str(WORKSPACE / "work/images-build/native-layout0043-root.img"),
            "base_sha256": "86e55716c6655bf374f7b21706f773d78a3ccea7bfe4bc26fe9ca39311fa71d9"}
    fdt = out / "current-fdt.dtb"
    with fdt.open("xb") as stream:
        subprocess.run(["adb", "-s", SERIAL, "exec-out", 'su -c "cat /sys/firmware/fdt"'], stdout=stream, check=True, timeout=30)
    calibration = capture_rtc_owned(out, token)
    receipt = {"restore_point": str(restore), "ios_root": root, "verified_at": stamp(),
               "live_fdt": str(fdt), "rtc_calibration": str(calibration), "boot_partition_writes": 0, "slot_changes": 0}
    RESTORE_RECEIPT.write_text(json.dumps(receipt, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    say("RESTORE_POINT_READY; PHONE_PARTITIONS_AND_IOS_ROOT_VERIFIED")
    return receipt


def backup_ios_root(restore, out, token):
    verify_backup(restore)
    tool = "CLASSPATH=/data/local/tmp/codex-managed-native-image.jar app_process /system/bin ManagedNativeImage"
    need("mapped=true" not in rooted(tool + " status"), "A foreign iOS root mapping is active")
    mapped = False
    try:
        rooted(tool + " map", timeout=120)
        mapped = True
        subprocess.run([sys.executable, "-X", "utf8", WORKSPACE / "work/xiaomi13-ios-gpu-20261108/verify-native-map.py"], check=True, timeout=120)
        target = restore / "ios-root"
        say("IOS_ROOT_DELTA_BACKUP_BEGIN: reuse verified 20GiB base; phone reads only")
        subprocess.run([sys.executable, "-I", WORKSPACE / "work/adsp-battery/hw000/hw000-delta-backup.py",
                        "--backup-base", WORKSPACE / "work/images-build/native-layout0043-root.img",
                        "--backup-base-sha", "86e55716c6655bf374f7b21706f773d78a3ccea7bfe4bc26fe9ca39311fa71d9",
                        "--name", "dualboot1118", "--keep-delta", target], check=True, timeout=1800)
        need((target / "README.txt").is_file() and (target / "diff-blocks.txt").is_file(), "iOS restore point did not finalize")
        fdt = restore / "phone/current-fdt.dtb"
        with fdt.open("xb") as stream:
            subprocess.run(["adb", "-s", SERIAL, "exec-out", 'su -c "cat /sys/firmware/fdt"'], stdout=stream, check=True, timeout=30)
        calibration = restore / "phone/rtc-calibration.json"
        calibration = capture_rtc_owned(out, token)
        state = json.loads((restore / "phone/manifest.json").read_text(encoding="utf-8"))
        state["ios_root"] = {"snapshot": str(target), "readme_sha256": digest(target / "README.txt"),
                             "blocks_sha256": digest(target / "phone-blocks.txt"), "base": str(WORKSPACE / "work/images-build/native-layout0043-root.img"),
                             "base_sha256": "86e55716c6655bf374f7b21706f773d78a3ccea7bfe4bc26fe9ca39311fa71d9"}
        (restore / "phone/manifest.json").write_text(json.dumps(state, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
        say("IOS_ROOT_RESTORE_POINT_VERIFIED")
        return state["ios_root"]
    finally:
        if mapped:
            rooted(tool + " unmap", timeout=60)
            need("mapped=true" not in rooted(tool + " status"), "iOS root mapping did not unmap")
            say("IOS_ROOT_UNMAPPED")


def survey_daily(restore, out, before, token):
    """Read the installed daily entry and slot state; no reboot or writes."""
    saved = verify_backup(restore, check_boot_b=False)
    previous = preserve_mtd(out)
    # KSU 模組已移除：直接以 boot_b 前綴比對電腦上的各條 chain，不再讀模組記下的雜湊。
    by_size = {}
    for p in (WORKSPACE / "work/boot-chain").glob("chain-*/boot.img"):
        if 0 < p.stat().st_size <= 201326592:
            by_size.setdefault(p.stat().st_size, []).append(p)
    matches = []
    for size, paths in sorted(by_size.items()):
        current = rooted(f"head -c {size} /dev/block/by-name/boot_b | sha256sum", timeout=120).split()[0]
        matches += [(str(p.parent), size, current) for p in paths if digest(p) == current]
    need(len(matches) == 1, "Cannot uniquely identify the current daily iOS chain")
    daily_chain, daily_size, daily_sha = matches[0]
    slot = {"current": rooted("bootctl get-current-slot"), "active": rooted("bootctl get-active-boot-slot"),
            "a_successful": rooted("bootctl is-slot-marked-successful 0; echo result=$?"),
            "b_bootable": rooted("bootctl is-slot-bootable 1; echo result=$?")}
    fdt = out / "current-fdt.dtb"
    with fdt.open("xb") as stream:
        subprocess.run(["adb", "-s", SERIAL, "exec-out", 'su -c "cat /sys/firmware/fdt"'], stdout=stream, check=True, timeout=30)
    calibration = capture_rtc_owned(out, token)
    current_b_sha = rooted("sha256sum /dev/block/by-name/boot_b").split()[0]
    if current_b_sha != saved["partitions"]["boot_b"]["raw_sha256"]:
        capture_raw("/dev/block/by-name/boot_b", out / "current-boot_b.img.gz", 201326592)
    report = {"daily_chain": daily_chain, "daily_boot_b_prefix_sha256": daily_sha, "daily_size": daily_size,
              "current_state": before, "slot_state": slot, "saved_mtd": previous, "rtc_calibration": str(calibration),
              "fdt": str(fdt), "boot_a_full_sha256": saved["partitions"]["boot_a"]["raw_sha256"],
              "boot_b_full_sha256": current_b_sha, "phone_boot_partition_writes": 0}
    (out / "daily-survey.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    say("DAILY_IOS_IDENTIFIED " + daily_chain)
    return report


def inspect_android_return(candidate, restore, out, before, seconds):
    """Inspect the user's current Android after a menu choice; never reboot."""
    del out, seconds
    record = verify_backup(restore)
    report = json.loads((candidate / "report.json").read_text(encoding="utf-8"))
    need(digest(candidate / "boot.img") == report["candidate"]["sha256"], "Candidate hash differs")
    match = re.search(r"([0-9a-fA-F]+)-[0-9a-fA-F]+\s*:\s*Kernel code", before["iomem"])
    need(match, "Cannot verify Android execution address")
    address = int(match[1], 16)
    start = 0xa8000000 + report["candidate"]["linux_offset"]
    end = 0xa8000000 + report["candidate"]["ram_footprint"]
    need(start <= address < end, "Current Android is stock; candidate handoff cannot be proved")
    tool = "CLASSPATH=/data/local/tmp/codex-managed-native-image.jar app_process /system/bin ManagedNativeImage"
    need("mapped=true" not in rooted(tool + " status"), "iOS root mapping is active")
    need(before["active_slot"] == record["before"]["active_slot"], "Active slot changed")
    say("DIRECT_MENU_ANDROID_HANDOFF_OK; BOOT_PARTITIONS_UNCHANGED; SLOT_UNCHANGED; UNMAPPED")
    return {"after": before, "kernel_code_address": hex(address), "candidate_sha256": report["candidate"]["sha256"],
            "embedded_range": [hex(start), hex(end)], "boot_partition_writes": 0, "slot_changes": 0,
            "reboot_performed": False, "unmapped": True, "ios_new_version_tested": False}


def ram_boot_menu(candidate):
    say("REBOOT_TO_FASTBOOT")
    adb("reboot", "bootloader")
    deadline = time.monotonic() + 80
    while time.monotonic() < deadline:
        if any(line.split() == [SERIAL, "fastboot"] for line in command("fastboot", "devices").splitlines()):
            break
        time.sleep(2)
    else:
        raise RuntimeError("Fastboot did not appear")
    product = subprocess.run(["fastboot", "-s", SERIAL, "getvar", "product"], capture_output=True, timeout=15)
    product_text = (product.stdout + product.stderr).decode("utf-8", "replace")
    need(product.returncode == 0 and re.search(r"product:\s*fuxi\b", product_text), "Fastboot device identity differs")
    # RAM boot only. Never flash or set_active.
    command("fastboot", "-s", SERIAL, "boot", candidate / "boot.img", timeout=90)


def wait_for_android(seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        try:
            if adb("shell", "getprop sys.boot_completed", timeout=6) == "1":
                return android_state()
        except (RuntimeError, subprocess.TimeoutExpired):
            pass
        time.sleep(2)
    raise RuntimeError("Android did not return within the observation window")


def verify_android_handoff(report, after):
    match = re.search(r"([0-9a-fA-F]+)-[0-9a-fA-F]+\s*:\s*Kernel code", after["iomem"])
    need(match, "Cannot verify embedded Linux execution address")
    address = int(match[1], 16)
    start = 0xa8000000 + report["candidate"]["linux_offset"]
    end = 0xa8000000 + report["candidate"]["ram_footprint"]
    need(start <= address < end, "Android booted from stock address, not the embedded chain kernel")
    return {"kernel_code_address": hex(address), "embedded_range": [hex(start), hex(end)]}


def android_boot_timing():
    # init service timestamps are nanoseconds since Linux boot. These measure
    # the Android phase, not the first visible panel frame or the Power key.
    timings = {}
    for name in ("bootanim", "surfaceflinger"):
        value = adb("shell", "getprop ro.boottime." + name, timeout=5)
        if value.isdecimal():
            timings[name + "_start_seconds_since_kernel_boot"] = int(value) / 1_000_000_000
    return timings


def capture_boot_timing(out):
    sources = {
        "boot-properties": lambda: rooted('getprop | grep -E "^\\[ro\\.boottime\\.(init|bootanim|surfaceflinger|vendor\\.bootanim)(\\.|])" || true', timeout=10),
        "boot-events": lambda: adb("shell", "logcat", "-d", "-b", "events", "-v", "monotonic", "-s",
                                   "boot_progress_start", "boot_progress_enable_screen", "sf_stop_bootanim",
                                   "wm_boot_animation_done", timeout=10),
        "animation": lambda: adb("shell", "logcat", "-d", "-b", "system", "-b", "main", "-v", "monotonic",
                                 "-s", "BootAnimation:V", timeout=10),
    }
    for name, capture in sources.items():
        text = capture()
        (out / (name + ".txt")).write_text(text + "\n", encoding="utf-8", newline="\n")


def inspect_boot_timing(candidate, out, before):
    if candidate:
        report = json.loads((candidate / "report.json").read_text(encoding="utf-8"))
        need(report["result"] == "ok" and digest(candidate / "boot.img") == report["candidate"]["sha256"],
             "Candidate did not pass build verification")
        handoff = verify_android_handoff(report, before)
    else:
        handoff = {}
    capture_boot_timing(out)
    kernel = rooted(
        'dmesg | grep -iE "fs_mgr|mount_all|wait for.*(/dev/|partition|block)|timed out.*(/dev/|partition|block)|timed out waiting" || true',
        timeout=15,
    )
    userland = adb("shell", "logcat", "-d", "-b", "system", "-b", "main", "-v", "monotonic",
                   "-s", "init:V", "fs_mgr:V", "libfs_mgr:V", timeout=15)
    (out / "mount-kernel.txt").write_text(kernel + "\n", encoding="utf-8", newline="\n")
    (out / "mount-userspace.txt").write_text(userland + "\n", encoding="utf-8", newline="\n")
    product = rooted(
        'grep -nF "mi_product" /vendor/etc/fstab.qcom; '
        'if test -e /mnt/vendor/mi_product; then echo mi_product_present=1; else echo mi_product_present=0; fi; '
        'grep -F " /mnt/vendor/mi_product " /proc/mounts || true',
        timeout=10,
    )
    (out / "mi-product.txt").write_text(product + "\n", encoding="utf-8", newline="\n")
    return {**handoff, "slot": before["slot"], "active_slot": before["active_slot"], "reboot_performed": False}


def stock_boot_timing(out, restore, before, seconds, token):
    needed_slot = "0"
    need(before["active_slot"] == needed_slot, "Original Android slot a must be active")
    record = verify_backup(restore, check_boot_b=False)
    (out / "chain-before-stock").mkdir()
    capture_boot_timing(out / "chain-before-stock")
    require_owned_locks(token)
    say("REBOOTING_ORIGINAL_ANDROID_FOR_TIMING")
    adb("reboot")
    # Wait for the current adbd to disconnect before accepting boot_completed.
    deadline = time.monotonic() + 45
    while time.monotonic() < deadline:
        try:
            if adb("shell", "getprop sys.boot_completed", timeout=4) != "1":
                break
        except (RuntimeError, subprocess.TimeoutExpired):
            break
        time.sleep(1)
    else:
        raise RuntimeError("Did not observe a reboot transition")
    after = wait_for_android(seconds)
    need(after["active_slot"] == needed_slot, "Active slot changed")
    code = re.search(r"([0-9a-fA-F]+)-[0-9a-fA-F]+\s*:\s*Kernel code", after["iomem"])
    need(code and int(code[1], 16) < 0xa9200000, "Did not return through the original Android kernel")
    need(rooted("sha256sum /dev/block/by-name/boot_a").split()[0] == record["partitions"]["boot_a"]["raw_sha256"],
         "Stock Android image changed")
    capture_boot_timing(out)
    return {"slot": after["slot"], "active_slot": after["active_slot"],
            "kernel_code_address": hex(int(code[1], 16)), "reboot_performed": True}


def boot_android(candidate, restore, out, before, seconds, token, test_recovery=False):
    record = verify_backup(restore, check_boot_b=False, allow_installed_boot_a=True)
    need(isinstance(record["ios_root"], dict) and Path(record["ios_root"]["snapshot"]).joinpath("README.txt").is_file(),
         "Complete the iOS root restore point before exposing both menu choices")
    report = json.loads((candidate / "report.json").read_text(encoding="utf-8"))
    need(report["result"] == "ok" and report["candidate"], "No successful candidate report")
    need(digest(candidate / "boot.img") == report["candidate"]["sha256"], "Candidate hash changed")
    need(before["active_slot"] == "0", "RAM test requires stock Android slot a active")
    # boot_a 可能是原廠或已驗證的常駐選單；以目前值為基準，試開前後必須不變。
    protected = {"boot_a": rooted("sha256sum /dev/block/by-name/boot_a").split()[0],
                 "boot_b": rooted("sha256sum /dev/block/by-name/boot_b").split()[0]}
    if test_recovery:
        for name in ("recovery_a", "recovery_b"):
            item = record["partitions"][name]
            need(digest(Path(item["file"])) == item["file_sha256"], "Recovery backup changed: " + name)
            actual = rooted("sha256sum " + item["device"]).split()[0]
            need(actual == item["raw_sha256"], "Recovery differs from backup: " + name)
            protected[name] = actual
        need(protected["recovery_a"] == (IOS_ENTRY / "twrp.sha256")
             .read_text(encoding="utf-8").strip(), "Active Recovery is not the expected TWRP image")
    preserve_mtd(out)
    need("mapped=true" not in rooted("CLASSPATH=/data/local/tmp/codex-managed-native-image.jar app_process /system/bin ManagedNativeImage status"), "iOS root mapping is still active")
    require_owned_locks(token)
    ram_boot_menu(candidate)
    countdown = report.get("timeout_seconds", 0)
    if countdown and not test_recovery:
        say(f"MENU_WINDOW_OPEN: wait {countdown} seconds for automatic Android boot")
    else:
        say("MENU_WINDOW_OPEN: focus Android and short-press Power to start")
    after = wait_for_android(seconds)
    need(after["active_slot"] == before["active_slot"], "Active slot changed")
    handoff = verify_android_handoff(report, after)
    say("ANDROID_CHAIN_HANDOFF_OK " + handoff["kernel_code_address"])
    try:
        handoff["android_boot_timing"] = android_boot_timing()
    except (RuntimeError, subprocess.TimeoutExpired) as error:
        handoff["android_boot_timing_error"] = str(error)
    recovery = None
    if test_recovery:
        require_owned_locks(token)
        say("STARTING_SECOND_RAM_BOOT_FOR_RECOVERY")
        ram_boot_menu(candidate)
        say("RECOVERY_MENU_OPEN: choose Other Options > Recovery")
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            require_owned_locks(token)
            try:
                version = adb("shell", "getprop ro.twrp.version", timeout=5)
                if version:
                    recovery = {"twrp_version": version, "transport": "adb"}
                    say("TWRP_REACHED " + version)
                    adb("reboot", timeout=10)
                    break
                if adb("shell", "getprop sys.boot_completed", timeout=5) == "1":
                    raise ValueError("Device returned to Android before ADB could verify TWRP")
            except (RuntimeError, subprocess.TimeoutExpired):
                # Reboots disconnect adb. If we saw TWRP, check Android next.
                if recovery:
                    break
            time.sleep(2)
        need(recovery is not None, "TWRP did not appear over ADB")
        after = wait_for_android(seconds)
        need(after["active_slot"] == before["active_slot"], "Active slot changed during Recovery test")
        say("TWRP_RETURNED_TO_ANDROID")
    for name, expected in protected.items():
        need(rooted("sha256sum /dev/block/by-name/" + name).split()[0] == expected, "Partition changed: " + name)
    say("BOOT_PARTITIONS_UNCHANGED; SLOT_UNCHANGED")
    return {"before": before, "after": after, **handoff, "recovery": recovery, "protected_sha256": protected,
            "candidate_sha256": report["candidate"]["sha256"],
            "configured_android_timeout_seconds": countdown,
            "boot_partition_writes": 0, "slot_changes": 0, "menu_visual_and_keys": "NEEDS_USER_OBSERVATION"}


def boot_ios(candidate, restore, out, before, seconds):
    record = verify_backup(restore)
    need(isinstance(record["ios_root"], dict), "Missing complete iOS root restore point")
    root = record["ios_root"]
    need(digest(Path(root["snapshot"]) / "README.txt") == root["readme_sha256"], "iOS snapshot metadata changed")
    need(digest(Path(root["snapshot"]) / "phone-blocks.txt") == root["blocks_sha256"], "iOS snapshot manifest changed")
    meta = json.loads((candidate / "report.json").read_text(encoding="utf-8"))
    wrapper_record = candidate / "phone-wrapper.txt"
    wrapper = wrapper_record.read_text(encoding="utf-8").strip() if wrapper_record.is_file() else "native-dualboot-validation-1118-loader"
    need(re.fullmatch(r"native-[a-z0-9-]+-loader", wrapper), "Invalid monitor wrapper name")
    directory = WORKSPACE / "work/Codex-Xiaomi13-GPU-20261109" / wrapper
    need(digest(directory / "boot.img") == meta["candidate"]["sha256"], "Wrapped candidate hash differs")
    artifact = (directory / "source.txt").read_text().split()[1]
    preserve_mtd(out)
    # The established boot experiment performs fresh map/RTC safety checks,
    # unmaps, RAM boots and uses the established monitor/reset return path.
    subprocess.run(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                    WORKSPACE / "work/Codex-Xiaomi13-GPU-20261109/boot-exp-v.ps1",
                    "-Loader", wrapper, "-Out", out.name, "-Kernel", artifact,
                    "-Seconds", str(seconds), "-Reset", "3", "-Framebuffer", "-FramebufferHeight", "1200",
                    "-Verifier", "verify-loader-safety.py"], check=True, timeout=900)
    try:
        returned = adb("shell", "getprop sys.boot_completed", timeout=6) == "1"
    except (RuntimeError, subprocess.TimeoutExpired):
        returned = False
    if not returned:
        subprocess.run([sys.executable, WORKSPACE / "work/Codex-Xiaomi13-GPU-20261109/reset-soon.py", "1"],
                       cwd=WORKSPACE / "work/Codex-Xiaomi13-GPU-20261109", check=False, timeout=30)
        deadline = time.monotonic() + 120
        while time.monotonic() < deadline:
            try:
                if adb("shell", "getprop sys.boot_completed", timeout=6) == "1":
                    break
            except (RuntimeError, subprocess.TimeoutExpired):
                pass
            time.sleep(3)
    after = android_state()
    need(after["active_slot"] == before["active_slot"], "Active slot changed")
    for name in ("boot_a", "boot_b"):
        item = record["partitions"][name]
        need(rooted("sha256sum " + item["device"]).split()[0] == item["raw_sha256"], "Boot partition changed")
    log = WORKSPACE / "work/Codex-Xiaomi13-GPU-20261109" / out.name / "boot.log"
    need(log.is_file(), "iOS console was not captured")
    text = log.read_text(encoding="utf-8", errors="replace")
    need("exec SpringBoard" in text and "panic(cpu" not in text, "iOS boot evidence is incomplete or contains a panic")
    say("IOS_CONSOLE_CAPTURED; ANDROID_RETURN_OK; BOOT_PARTITIONS_UNCHANGED")
    preserve_mtd(out / "after-ios")
    return {"before": before, "after": after, "console": str(log), "console_sha256": digest(log),
            "boot_partition_writes": 0, "slot_changes": 0, "root_restore_point": root,
            "menu_visual_and_keys": "NEEDS_USER_OBSERVATION"}


# 模組若有這些項目，就不只是「執行」按鈕（開機腳本、掛載、規則），不自動移除。
MODULE_ACTIVE_ITEMS = ("service.sh", "post-fs-data.sh", "post-mount.sh", "boot-completed.sh",
                       "system", "sepolicy.rule", "system.prop", "webroot")
MODULE_HOST_REFERENCES = ("expected-fiemap.txt", "twrp.sha256", "bin/inspect-fiemap", "bin/check-pin")


def remove_ksu_module(restore, out, before):
    """移除只剩「執行」按鈕的 KSU 模組：先封存並核對，再刪除並讀回。不寫分區、不重開、不切槽。"""
    need(before["active_slot"] == "0", "Expected active slot a")
    verify_backup(restore, check_boot_b=False, allow_installed_boot_a=True)
    result = {"previous_mtd": preserve_mtd(out), "phone_partition_writes": 0, "reboots": 0, "slot_changes": 0}
    need(not phone_exists(KSU_MODULE_UPDATE), "A pending KSU module update exists; inspect it first")
    if not phone_exists(KSU_MODULE):
        result["module"] = "ALREADY_ABSENT"
        say("KSU_MODULE_ALREADY_ABSENT")
        return result
    active = rooted(f"for f in {' '.join(MODULE_ACTIVE_ITEMS)}; do [ -e {KSU_MODULE}/$f ] && echo $f; done; true")
    need(not active, "Module does more than the action button: " + active.replace("\n", ","))
    listing = rooted(f"cd {KSU_MODULE} && find . -type f -exec sha256sum {{}} +")
    phone_files = {}
    for line in listing.splitlines():
        sha, name = line.split(maxsplit=1)
        phone_files[name.removeprefix("./")] = sha
    need("module.prop" in phone_files and "action.sh" in phone_files, "Unexpected module contents")
    for name in MODULE_HOST_REFERENCES:
        need(phone_files.get(name) == digest(IOS_ENTRY / name), "Host reference differs from the phone module: " + name)
    keep = WORKSPACE.parent / "fuxi_Backup" / ("ksu-module-" + out.name)
    keep.mkdir(exist_ok=False)
    archive = keep / "fuxi_ios_boot-module.tar"
    with archive.open("xb") as stream:
        subprocess.run(["adb", "-s", SERIAL, "exec-out", 'su -c "tar -c -C /data/adb/modules fuxi_ios_boot"'],
                       stdout=stream, check=True, timeout=60)
    import tarfile
    archived = {}
    with tarfile.open(archive) as tar:
        for member in tar.getmembers():
            need(member.name == "fuxi_ios_boot" or member.name.startswith("fuxi_ios_boot/"), "Unexpected tar member")
            need(".." not in Path(member.name).parts, "Unsafe tar member")
            if member.isfile():
                archived[member.name.removeprefix("fuxi_ios_boot/")] = hashlib.sha256(tar.extractfile(member).read()).hexdigest()
        prop = tar.extractfile("fuxi_ios_boot/module.prop").read().decode("utf-8", "replace")
    need(archived == phone_files, "Module archive differs from the phone files")
    result["module_archive"] = {"file": str(archive), "sha256": digest(archive), "files": phone_files,
                                "module_prop": prop}
    (keep / "README.txt").write_text(
        "KSU 模組 fuxi_ios_boot 移除前的完整封存（" + stamp() + "，" + out.name + "）。\n"
        "2026-11-18 起 boot_a 常駐 dualboot 選單，重開即可選 iOS，模組只剩重開按鈕而移除。\n"
        "還原：經 launch-loader-check.ps1 把 tar 解到 /data/adb/modules 後重開，"
        "或在 KSU 安裝 work/ksu-ios-entry 的模組 zip。\n", encoding="utf-8", newline="\n")
    # 先用電腦端工具做一次模組原本的 iOS 根映像檢查，確認替代流程在手機上可用，再刪。
    result["ios_root"] = verify_ios_root()
    rooted(f"rm -rf {KSU_MODULE} && sync")
    need(not phone_exists(KSU_MODULE) and not phone_exists(KSU_MODULE_UPDATE), "Module still present after removal")
    result["modules_after"] = rooted("ls -A /data/adb/modules; true")
    result["module"] = "REMOVED_AND_VERIFIED"
    say("KSU_MODULE_REMOVED_AND_VERIFIED " + str(archive))
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--from-launcher", required=True)
    p.add_argument("--action", choices=("backup", "rootbackup", "prepare", "survey", "android", "recovery", "ios", "inspectreturn", "timing", "stocktiming", "removeksu"), required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--restore-point", type=Path, required=True)
    p.add_argument("--candidate", type=Path)
    p.add_argument("--seconds", type=int, default=180)
    args = p.parse_args()
    need(args.from_launcher == str(os.getppid()), "Start this script through launch-loader-check.ps1")
    need(re.fullmatch(r"[a-z0-9-]+", args.out), "Invalid output name")
    need(60 <= args.seconds <= 600, "Invalid observation time")
    restore = args.restore_point.resolve()
    need(restore.is_relative_to(WORKSPACE.parent / "fuxi_Backup") and (restore / "manifest.json").is_file(), "Missing host restore point")
    out = WORKSPACE / "work/dualboot" / args.out
    out.mkdir(exist_ok=False)
    token = f"dualboot validation {args.action} pid={os.getpid()} {stamp()}"
    owned = []
    report = {"result": "FAILED", "action": args.action, "started": stamp(), "restore_point": str(restore)}
    try:
        for lock in (*LOCKS, HOLD):
            need(not lock.exists(), "Phone is reserved: " + str(lock))
        for lock in LOCKS:
            with lock.open("x", encoding="utf-8") as stream:
                stream.write(token)
            owned.append(lock)
        need(not HOLD.exists(), "Phone work was paused")
        say("PHONE_START " + token)
        before = android_state()
        if args.action == "backup":
            report["capture"] = backup_phone(restore, out, before)
        elif args.action == "rootbackup":
            report["capture"] = backup_ios_root(restore, out, token)
        elif args.action == "prepare":
            report["capture"] = finalize_restore(restore, out, token)
        elif args.action == "survey":
            report["capture"] = survey_daily(restore, out, before, token)
        elif args.action == "removeksu":
            report["capture"] = remove_ksu_module(restore, out, before)
        elif args.action == "stocktiming":
            report["validation"] = stock_boot_timing(out, restore, before, args.seconds, token)
        elif args.action == "timing":
            candidate = args.candidate.resolve() if args.candidate else None
            if candidate:
                need(candidate.is_relative_to(WORKSPACE / "work/dualboot"), "Candidate outside dualboot outputs")
            report["validation"] = inspect_boot_timing(candidate, out, before)
        else:
            candidate = args.candidate.resolve()
            need(candidate.is_relative_to(WORKSPACE / "work/dualboot"), "Candidate outside dualboot outputs")
            if args.action in ("android", "recovery"):
                report["validation"] = boot_android(candidate, restore, out, before, args.seconds, token,
                                                    test_recovery=args.action == "recovery")
            else:
                action = inspect_android_return if args.action == "inspectreturn" else boot_ios
                report["validation"] = action(candidate, restore, out, before, args.seconds)
        report["result"] = "ok"
    except BaseException as error:
        report["error"] = str(error)
        raise
    finally:
        report["finished"] = stamp()
        release = args.action == "backup" or report["result"] == "ok"
        if not release:
            try:
                release = adb("shell", "getprop sys.boot_completed", timeout=6) == "1"
            except BaseException:
                pass
        report["lock_state"] = "released" if release else "retained_pending_android_return"
        report["lock_token"] = token
        (out / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
        if release:
            for lock in reversed(owned):
                if lock.read_text(encoding="utf-8") == token:
                    lock.unlink()
            say("PHONE_RELEASED")
        else:
            say("PHONE_NOT_RETURNED; OWNED_LOCKS_RETAINED; use Power+Vol-down to return to unchanged Android")


if __name__ == "__main__":
    main()
