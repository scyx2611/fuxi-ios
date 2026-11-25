#!/usr/bin/env python3
"""Generate/check patch 0130 (fuxi-sensors): SM8550 ADSP SSC sensors + FastRPC file service + native IOHIDEventService."""
import argparse
import difflib
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'xnu/fuxi-sensors'
ADSP_C = ROOT / 'xnu/fuxi-adsp/fuxi_adsp.c'
PATCH = ROOT / 'xnu/patches/0130-fuxi-sensors.patch'

BSD_SOURCES = (
    'SensorsPb.c',
    'SensorsSsc.c',
    'SensorsFixed.c',
    'SensorsOrient.c',
    'SensorsCompass.c',
    'SensorsPack.c',
    'SensorsRpc.c',
    'SensorsCore.c',
    'SensorsHid.c',
    'fuxi_sensors_rpc.c',
    'fuxi_sensors.c',
)

BSD_HEADERS = (
    'LICENSE.PaganiSensors',
    'PaganiSensors.h',
    'SensorsText.h',
    'SensorsPb.h',
    'SensorsSsc.h',
    'SensorsFixed.h',
    'SensorsOrient.h',
    'SensorsCompass.h',
    'SensorsPack.h',
    'SensorsRpc.h',
    'SensorsCore.h',
    'SensorsHid.h',
    'fuxi_sensors_smmu.h',
    'fuxi_sensors_hid.h',
)

IOKIT_HID_SOURCES = (
    'FuxiSensorsHid.cpp',
)

BSD_BEFORE = (
    'bsd/dev/fuxi_modem_fw.c\t\t\tstandard',
    'bsd/dev/fuxi_camera.c\t\t\tstandard',
    'bsd/dev/fuxi_camera_dylib.c\t\tstandard',
)
BSD_AFTER = (
    'bsd/dev/mem.c\t\t\t\tstandard',
    'bsd/dev/munge.c \t\t\toptional config_requires_u32_munging',
    'bsd/dev/unix_startup.c\t\t\tstandard',
)
BSD_LINE = 159

IOKIT_BEFORE = (
    'iokit/Kernel/arm/hid/IOHIDEvent.cpp                              optional iokitcpp',
    'iokit/Kernel/arm/hid/IOHIDEventServiceQueue.cpp                  optional iokitcpp',
    'iokit/Kernel/arm/hid/IOHIDEventServiceUserClient.cpp             optional iokitcpp',
)
IOKIT_LINE = 25


def bsd_entry(name: str) -> str:
    path = 'bsd/dev/' + name
    tabs = '\t' * max(1, (40 - len(path) + 7) // 8)
    return path + tabs + 'standard'


def bsd_files_hunk() -> str:
    added = [bsd_entry(n) for n in BSD_SOURCES]
    lines = [' ' + x for x in BSD_BEFORE] + ['+' + x for x in added] + [' ' + x for x in BSD_AFTER]
    head = f'@@ -{BSD_LINE},{len(BSD_BEFORE) + len(BSD_AFTER)} +{BSD_LINE},{len(BSD_BEFORE) + len(BSD_AFTER) + len(added)} @@\n'
    return '--- a/bsd/conf/files\n+++ b/bsd/conf/files\n' + head + ''.join(x + '\n' for x in lines)


def iokit_files_hunk() -> str:
    added = ['iokit/Kernel/arm/hid/FuxiSensorsHid.cpp                          optional iokitcpp']
    lines = [' ' + x for x in IOKIT_BEFORE] + ['+' + x for x in added]
    head = f'@@ -{IOKIT_LINE},{len(IOKIT_BEFORE)} +{IOKIT_LINE},{len(IOKIT_BEFORE) + len(added)} @@\n'
    return '--- a/iokit/conf/files.arm64\n+++ b/iokit/conf/files.arm64\n' + head + ''.join(x + '\n' for x in lines)


def adsp_hook_diff() -> str:
    orig_bytes = ADSP_C.read_bytes()
    assert b'\r' not in orig_bytes and orig_bytes.endswith(b'\n'), 'fuxi_adsp.c must be LF terminated'
    orig = orig_bytes.decode('utf-8')
    updated = orig
    decl_anchor = 'extern void fuxi_audio_spf_start(void);\n'
    call_anchor = '        fuxi_audio_spf_start();\n'
    assert updated.count(decl_anchor) == 1, 'decl anchor not unique in fuxi_adsp.c'
    assert updated.count(call_anchor) == 1, 'call anchor not unique in fuxi_adsp.c'
    updated = updated.replace(decl_anchor, decl_anchor + 'extern void fuxi_sensors_start(void);\n', 1)
    updated = updated.replace(call_anchor, call_anchor + '        fuxi_sensors_start();\n', 1)
    return ''.join(difflib.unified_diff(
        orig.splitlines(True), updated.splitlines(True),
        fromfile='a/bsd/dev/fuxi_adsp.c', tofile='b/bsd/dev/fuxi_adsp.c'))


def new_file_diff(src_name: str, target_path: str) -> str:
    raw = (SRC / src_name).read_bytes()
    assert b'\r' not in raw, f'{src_name} has CR line endings'
    assert raw.endswith(b'\n'), f'{src_name} lacks trailing LF'
    return ''.join(difflib.unified_diff(
        [], raw.decode('utf-8').splitlines(True),
        fromfile='/dev/null', tofile='b/' + target_path))


def expected() -> str:
    parts = [bsd_files_hunk(), iokit_files_hunk(), adsp_hook_diff()]
    for name in BSD_SOURCES + BSD_HEADERS:
        parts.append(new_file_diff(name, 'bsd/dev/' + name))
    for name in IOKIT_HID_SOURCES:
        parts.append(new_file_diff(name, 'iokit/Kernel/arm/hid/' + name))
    return ''.join(parts)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--tree', type=Path)
    args = ap.parse_args()
    patch = expected()
    if args.check:
        actual = PATCH.read_bytes().replace(b'\r\n', b'\n').decode('utf-8')
        assert actual == patch, '0130 differs; run tools/sync-fuxi-sensors.py'
    else:
        PATCH.write_text(patch, encoding='utf-8', newline='\n')
    if args.tree:
        subprocess.run(['git', 'apply', '--check', '-'], input=patch.encode('utf-8'), cwd=args.tree, check=True)
    print(f'PASS: fuxi-sensors canonical sources match 0130 ({len(BSD_SOURCES)} BSD C, {len(IOKIT_HID_SOURCES)} IOKit C++, {len(BSD_HEADERS)} headers)')


if __name__ == '__main__':
    main()
