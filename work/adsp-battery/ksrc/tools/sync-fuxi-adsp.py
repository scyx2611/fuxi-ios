#!/usr/bin/env python3
"""Generate/check patch 0117 (fuxi-adsp): the audio DSP owner (boot-arg fuxiadsp) and its battery manager client.

Canonical sources are xnu/fuxi-adsp/: fuxi_adsp*.c are ours; the protocol modules (Qcom*.c/h, FatVolume.c/h,
ShengBattmgr.c/h, ShengAdsp.h, ShengQrtr.c) are UniXNU-sheng package/ files (6880068) copied unchanged, with that
repository's LICENSE as LICENSE.UniXNU-sheng. Every file goes to bsd/dev/ as a new file; the .c files are added to
bsd/conf/files right after bsd/dev/sweet_usb.c (the last of the board drivers, unchanged by any later patch).
xnu/fuxi-adsp/SweetBattery.cpp is iokit/Kernel/arm/SweetBattery.cpp as 0062 creates it (no later patch touches it) plus the
fuxiadsp=3 source; 0117 carries that difference.
--check also verifies that no earlier patch creates these paths and that the build-list context is what the earlier
patches leave (their bsd/conf/files hunks, merged).
"""
import argparse
import difflib
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'xnu/fuxi-adsp'
PATCH = ROOT / 'xnu/patches/0117-fuxi-adsp.patch'
SOURCES = ('fuxi_adsp.c', 'fuxi_adsp_fw.c', 'fuxi_adsp_battery.c', 'fuxi_amp.c', 'fuxi_pmic.c', 'fuxi_ramtest.c', 'fuxi_rpmh_shared.c', 'QcomSmem.c', 'QcomSmp2p.c', 'QcomGlink.c',
           'QcomMdt.c', 'QcomQmp.c', 'QcomRpmh.c', 'QcomQrtr.c', 'FatVolume.c', 'ShengBattmgr.c', 'ShengQrtr.c', 'GeniI2c.c')
HEADERS = ('fuxi_torch_audit.h', 'fuxi_haptic_pulse.h', 'QcomSmem.h', 'QcomSmp2p.h', 'QcomGlink.h', 'QcomMdt.h', 'QcomQmp.h', 'QcomRpmh.h', 'QcomScm.h', 'QcomQrtr.h',
           'FatVolume.h', 'ShengBattmgr.h', 'ShengAdsp.h', 'fuxi_adsp_battery.h', 'fuxi_rpmh_shared.h', 'fuxi_torch_pulse.h', 'fuxi_torch_runtime.h', 'fuxi_ramtest_pattern.h', 'GeniI2c.h')
MODIFIED = {'iokit/Kernel/arm/SweetBattery.cpp': ('0062-sweet-touch-power-battery.patch', 'SweetBattery.cpp')}
# bsd/dev/LICENSE.UniXNU-sheng already comes with 0098 (the GPU fence interrupt is adapted from sheng too); --check
# verifies that the local copy is the same text.
LICENSE = 'LICENSE.UniXNU-sheng'
SOURCES += ('fuxi_bt.c', 'fuxi_bt_xnu.c')
HEADERS += ('fuxi_bt.h',)
BLUETOOTH_SOURCE = ROOT / 'xnu/fuxi-bluetooth'
SOURCE_PATHS = {
    'fuxi_bt.c': BLUETOOTH_SOURCE / 'fuxi_bt.c',
    'fuxi_bt_xnu.c': BLUETOOTH_SOURCE / 'fuxi_bt_xnu.c',
    'fuxi_bt.h': BLUETOOTH_SOURCE / 'fuxi_bt.h',
}
# WCN7850 藍牙 UART：GENI FIFO 驅動與板級準備（fuxi,bt=3）
SOURCES += ('fuxi_bt_geni_fifo.c', 'fuxi_bt_board.c')
HEADERS += ('fuxi_bt_geni_fifo.h', 'fuxi_bt_board.h')
SOURCE_PATHS.update({name: BLUETOOTH_SOURCE / name for name in
                     ('fuxi_bt_geni_fifo.c', 'fuxi_bt_board.c', 'fuxi_bt_geni_fifo.h', 'fuxi_bt_board.h')})
# WCN7850 控制器初始化：rampatch／NVM 下載與 HCI reset（fuxi,bt=4）
SOURCES += ('fuxi_bt_setup.c',)
HEADERS += ('fuxi_bt_setup.h',)
SOURCE_PATHS.update({name: BLUETOOTH_SOURCE / name for name in ('fuxi_bt_setup.c', 'fuxi_bt_setup.h')})
SOURCES += ('fuxi_bt_pump.c', 'fuxi_bt_sk.c')
HEADERS += ('fuxi_bt_pump.h', 'fuxi_bt_sk.h')
SOURCE_PATHS.update({name: BLUETOOTH_SOURCE / name for name in
                     ('fuxi_bt_pump.c', 'fuxi_bt_sk.c', 'fuxi_bt_pump.h', 'fuxi_bt_sk.h')})
SOURCES += ('fuxi_battery_freshness.c',)
HEADERS += ('fuxi_battery_freshness.h', 'fuxi_battery_poll_pause.h')
SOURCE_PATHS.update({
    'fuxi_battery_freshness.c': ROOT / 'xnu/fuxi-battery-prep/fuxi_battery_freshness.c',
    'fuxi_battery_freshness.h': ROOT / 'xnu/fuxi-battery-prep/fuxi_battery_freshness.h',
    'fuxi_battery_poll_pause.h': ROOT / 'xnu/fuxi-battery-prep/fuxi_battery_poll_pause.h',
})
HEADERS += ('fuxi_torch_steady.h',)
SOURCES += ('fuxi_audio_spf.c', 'fuxi_audio_prep_gpr.c')
HEADERS += ('fuxi_audio_prep_gpr.h', 'fuxi_audio_smmu.h', 'fuxi_amp_tone.h')
# HAP525 DIRECT_PLAY runtime (fuxi,haptic=2|3, sysctl kern.fuxivibrate)
HEADERS += ('fuxi_haptic_runtime.h', 'fuxi_haptic_rtp.h')
SOURCE_PATHS.update({
    'fuxi_audio_prep_gpr.c': ROOT / 'xnu/fuxi-audio-prep/fuxi_audio_prep_gpr.c',
    'fuxi_audio_prep_gpr.h': ROOT / 'xnu/fuxi-audio-prep/fuxi_audio_prep_gpr.h',
})
FILES = SOURCES + HEADERS
# 0113 已安裝這兩個可攜來源，PRM probe 需要將它們連進核心。
AUDIO_LINK_SOURCES = ('fuxi_gpr.c', 'fuxi_audioreach.c')
BEFORE = ('bsd/dev/sweet_rpmh.c\t\t\tstandard', 'bsd/dev/sweet_spmi.c\t\t\tstandard', 'bsd/dev/sweet_usb.c\t\t\tstandard')
AFTER = ('bsd/dev/mem.c\t\t\t\tstandard', 'bsd/dev/munge.c \t\t\toptional config_requires_u32_munging',
         'bsd/dev/unix_startup.c\t\t\tstandard')
LINE = 121  # where sweet_rpmh.c sits after patches 0001-0116 (git apply finds the context at any offset anyway)


def entry(name):
    path = 'bsd/dev/' + name
    tabs = '\t' * max(1, (40 - len(path) + 7) // 8)
    return path + tabs + 'standard'


def build_list_hunk():
    added = [entry(name) for name in SOURCES + AUDIO_LINK_SOURCES]
    lines = [' ' + x for x in BEFORE] + ['+' + x for x in added] + [' ' + x for x in AFTER]
    head = f'@@ -{LINE},{len(BEFORE) + len(AFTER)} +{LINE},{len(BEFORE) + len(AFTER) + len(added)} @@\n'
    return '--- a/bsd/conf/files\n+++ b/bsd/conf/files\n' + head + ''.join(x + '\n' for x in lines)


def expected():
    for name in FILES:
        data = SOURCE_PATHS.get(name, SRC / name).read_bytes()
        assert b'\r' not in data, name + ' has CR line endings'
        assert data.endswith(b'\n'), name + ' lacks a final newline'
    new = ''.join(''.join(difflib.unified_diff([], SOURCE_PATHS.get(name, SRC / name).read_text(encoding='utf-8').splitlines(True), fromfile='/dev/null',
                                               tofile='b/bsd/dev/' + name)) for name in FILES)
    changed = ''
    for path, (_, local) in MODIFIED.items():
        data = (SRC / local).read_bytes()
        assert b'\r' not in data and data.endswith(b'\n'), local
        changed += ''.join(difflib.unified_diff(original(path).splitlines(True), (SRC / local).read_text(encoding='utf-8').splitlines(True),
                                                fromfile='a/' + path, tofile='b/' + path))
    return build_list_hunk() + new + changed


def original(path):
    """The file as the one earlier patch that creates it leaves it (asserting that no other earlier patch touches it)."""
    creator = MODIFIED[path][0]
    text = None
    for prior in sorted(PATCH.parent.glob('*.patch')):
        if prior.name >= PATCH.name:
            break
        for chunk in re.split(r'(?=^--- (?:a/|/dev/null))', prior.read_text(errors='surrogateescape'), flags=re.M):
            if not re.search(r'^\+\+\+ b/' + re.escape(path) + '$', chunk, re.M):
                continue
            assert prior.name == creator and chunk.startswith('--- /dev/null'), prior.name + ' touches ' + path
            body = chunk.split('\n@@', 1)[1].split('\n', 1)[1]
            lines = body.split('\n')
            if lines and lines[-1] == '':
                lines.pop()
            lines = re.split(r'^diff --git ', '\n'.join(lines), flags=re.M)[0].split('\n')
            assert all(x.startswith('+') for x in lines if x), path
            text = ''.join(x[1:] + '\n' for x in lines if x)
    assert text, creator + ' does not create ' + path
    return text


def prior_region():
    """bsd/conf/files around bsd/dev/ as the earlier patches leave it, merged from their hunks (each hunk's new side
    replaces its old side in the running region)."""
    region = None
    for prior in sorted(PATCH.parent.glob('*.patch')):
        if prior.name >= PATCH.name:
            break
        text = prior.read_text(errors='surrogateescape')
        for chunk in re.split(r'(?=^--- (?:a/|/dev/null))', text, flags=re.M):
            if not re.search(r'^\+\+\+ b/bsd/conf/files$', chunk, re.M):
                continue
            for hunk in re.split(r'(?=^@@ )', chunk, flags=re.M)[1:]:
                body = hunk.rstrip('\n').split('\n')[1:]
                old = [x[1:] for x in body if x[:1] in (' ', '-')]
                new = [x[1:] for x in body if x[:1] in (' ', '+')]
                if not any(x.startswith('bsd/dev/') for x in old + new):
                    continue
                if region is None:
                    region = new
                    continue
                for at in range(len(region) - len(old) + 1):
                    if region[at:at + len(old)] == old:
                        region[at:at + len(old)] = new
                        break
                else:
                    raise AssertionError(prior.name + ': bsd/dev hunk context not found in the merged region')
    return region or []


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--check', action='store_true')
    args = ap.parse_args()
    patch = expected()
    for prior in sorted(PATCH.parent.glob('*.patch')):
        if prior.name == PATCH.name:
            continue
        text = prior.read_text(errors='surrogateescape')
        for name in FILES:
            assert not re.search(r'^\+\+\+ b/bsd/dev/' + re.escape(name) + '$', text, re.M), prior.name + ' already creates ' + name
    audio_patch = (PATCH.parent / '0113-fuxi-audio.patch').read_text(errors='surrogateescape')
    assert all(f'+++ b/bsd/dev/{name}\n' in audio_patch for name in AUDIO_LINK_SOURCES), '0113 audio source missing'
    shipped = re.search(r'^\+\+\+ b/bsd/dev/' + re.escape(LICENSE) + r'\n@@[^\n]*\n((?:\+[^\n]*\n)+)',
                        (PATCH.parent / '0098-fuxi-native-adreno-abi.patch').read_text(errors='surrogateescape'), re.M)
    assert shipped and ''.join(x[1:] + '\n' for x in shipped[1].splitlines()) == (SRC / LICENSE).read_text(), \
        LICENSE + ' differs from the copy 0098 installs'
    region = prior_region()
    want = list(BEFORE + AFTER)
    assert any(region[i:i + len(want)] == want for i in range(len(region))), 'build-list context changed: ' + repr(region)
    if args.check:
        assert PATCH.read_text(encoding='utf-8') == patch, 'fuxi-adsp sources differ; run tools/sync-fuxi-adsp.py'
    else:
        PATCH.write_text(patch, encoding='utf-8', newline='\n')
    print(f'PASS: fuxi-adsp canonical sources match 0117 '
          f'({len(SOURCES) + len(AUDIO_LINK_SOURCES)} sources in the build list, {len(HEADERS)} other files)')


if __name__ == '__main__':
    main()
