#!/usr/bin/env python3
"""產生／檢查補丁 0124（fuxi-modem）：基頻 MPSS 擁有者（開機參數 fuximodem 或 ADT /chosen fuxi,modem，預設關）。

正本在 xnu/fuxi-modem/：fuxi_modem.c、fuxi_modem_fw.c（fuxi_adsp_fw.c 的 modem 版，自己的 FAT 快取）與純邏輯
fuxi_modem_proto.h（主機測試 tools/test-fuxi-modem.py 用同一份）。三個檔都以新檔放進 bsd/dev/；它們用到的 Qcom*
協定模組由 0117 安裝。兩個 .c 加進 bsd/conf/files，接在 0117 最後兩行（fuxi_gpr.c、fuxi_audioreach.c，0113 的音訊
來源）之後；sweet_ufs.c 在 fuxi_adsp_init() 之後呼叫 fuxi_modem_init()（UFS 已就緒，ADSP 執行緒已起）。
--check 另外確認：沒有其他補丁建立這三個路徑；0117 之後、0124 之前的補丁不碰 bsd/conf/files 與 sweet_ufs.c；
上下文行確實是 0106／0117 留下的樣子。
"""
import argparse
import difflib
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'xnu/fuxi-modem'
PATCH = ROOT / 'xnu/patches/0126-fuxi-modem.patch'
SOURCES = ('fuxi_modem.c', 'fuxi_modem_fw.c')
HEADERS = ('fuxi_modem_proto.h', 'QcomBaseband.h')
FILES = SOURCES + HEADERS
IOKIT_SOURCES = ('FuxiBaseband.cpp',)
IOKIT_EXTRA = (
    'coachz_bb_channels.h',
    'coachz_bb_control.h',
    'coachz_bb_data.h',
    'coachz_bb_esim.h',
    'coachz_bb_pdp.h',
    'coachz_bb_pdp.cpp',
    'coachz_bb_qmux.h',
    'coachz_bb_sahara.h',
    'fuxi_gnss_loc.h',
    'fuxi_gnss_pds_bridge.h',
)
IOKIT_FILES = IOKIT_SOURCES + IOKIT_EXTRA
ALL_FILES = FILES + IOKIT_FILES
BEFORE = ('bsd/dev/fuxi_gpr.c\t\t\tstandard', 'bsd/dev/fuxi_audioreach.c\t\tstandard')
AFTER = ('bsd/dev/mem.c\t\t\t\tstandard', 'bsd/dev/munge.c \t\t\toptional config_requires_u32_munging',
         'bsd/dev/unix_startup.c\t\t\tstandard')
FILES_LINE = 154  # fuxi_gpr.c 在 0001-0123 之後的位置（git apply 會找上下文，行號只是起點）
IOKIT_FILES_LINE = 21
IOKIT_BEFORE = (
    'iokit/Kernel/arm/FuxiWLAN.cpp                                    optional iokitcpp',
    'iokit/Kernel/arm/FuxiNetworkStack.cpp                            optional iokitcpp',
)
IOKIT_ADD = (
    'iokit/Kernel/arm/FuxiBaseband.cpp                                optional iokitcpp',
)
IOKIT_AFTER = (
    'iokit/Kernel/arm/SweetBattery.cpp                                optional iokitcpp',
    'iokit/Kernel/arm/hid/SweetHID.cpp                                optional iokitcpp',
)
EXTERN_BEFORE = ('extern void fuxi_keys_init(void);', 'extern void fuxi_wlan_init(void);', 'extern void fuxi_adsp_init(void);')
EXTERN_ADD = ('extern void fuxi_modem_init(void);',)
EXTERN_AFTER = ('extern void sweet_usb_probe(void);', '#if defined(FUXI)', 'static void fx_log_start(void);')
EXTERN_LINE = 63
CALL_BEFORE = ('\tfx_log_start();',
               '\t/* Audio DSP (ADSP) and its battery manager, boot-arg fuxiadsp (bsd/dev/fuxi_adsp.c); needs this driver for the '
               'firmware. */',
               '\tfuxi_adsp_init();')
CALL_ADD = ('\t/* Modem (MPSS) owner, boot-arg fuximodem or ADT fuxi,modem (bsd/dev/fuxi_modem.c); off by default, waits for the '
            'ADSP. */',
            '\tfuxi_modem_init();')
CALL_AFTER = ('#else', '\tsweet_log_start();', '\tsweet_sample_start();')
CALL_LINE = 1084
AMFI_SB_LINE = 310
AMFI_SB_BEFORE = (
    '\t\tprintf("SWEET Sandbox: %s op %d\\n", name, call);',
    '\t}',
    '\tswitch (call) {',
)
AMFI_SB_ADD = (
    '\tcase 2:         /* sandbox_check */',
    '\t{',
    '\t\textern int fuxi_modem_sandbox_check(int pid, user_addr_t arg);',
    '\t\treturn fuxi_modem_sandbox_check(proc_pid(p), arg);',
    '\t}',
)
AMFI_SB_AFTER = (
    '\tcase 4:         /* container path (audit token / pid) */',
    '\t\treturn fuxi_amfi_enabled ? fuxi_sys_container_result(fuxi_sandbox_container(arg)) : ENOENT;',
    '\tcase 5:         /* extension issue */',
)


def entry(name):
    path = 'bsd/dev/' + name
    tabs = '\t' * max(1, (40 - len(path) + 7) // 8)
    return path + tabs + 'standard'


def hunk(line, before, added, after, shift=0):
    lines = [' ' + x for x in before] + ['+' + x for x in added] + [' ' + x for x in after]
    old = len(before) + len(after)
    return f'@@ -{line},{old} +{line + shift},{old + len(added)} @@\n' + ''.join(x + '\n' for x in lines)


def expected():
    for name in ALL_FILES:
        data = (SRC / name).read_bytes()
        assert b'\r' not in data, name + ' has CR line endings'
        assert data.endswith(b'\n'), name + ' lacks a final newline'
    out = '--- a/bsd/conf/files\n+++ b/bsd/conf/files\n'
    out += hunk(FILES_LINE, BEFORE, [entry(name) for name in SOURCES], AFTER)
    out += '--- a/iokit/conf/files.arm64\n+++ b/iokit/conf/files.arm64\n'
    out += hunk(IOKIT_FILES_LINE, IOKIT_BEFORE, IOKIT_ADD, IOKIT_AFTER)
    out += '--- a/bsd/dev/sweet_ufs.c\n+++ b/bsd/dev/sweet_ufs.c\n'
    out += hunk(EXTERN_LINE, EXTERN_BEFORE, EXTERN_ADD, EXTERN_AFTER)
    out += hunk(CALL_LINE, CALL_BEFORE, CALL_ADD, CALL_AFTER, shift=len(EXTERN_ADD))
    out += '--- a/bsd/kern/sweet_amfi.c\n+++ b/bsd/kern/sweet_amfi.c\n'
    out += hunk(AMFI_SB_LINE, AMFI_SB_BEFORE, AMFI_SB_ADD, AMFI_SB_AFTER)
    for name in FILES:
        out += ''.join(difflib.unified_diff([], (SRC / name).read_text(encoding='utf-8').splitlines(True), fromfile='/dev/null',
                                            tofile='b/bsd/dev/' + name))
    for name in IOKIT_FILES:
        out += ''.join(difflib.unified_diff([], (SRC / name).read_text(encoding='utf-8').splitlines(True), fromfile='/dev/null',
                                            tofile='b/iokit/Kernel/arm/' + name))
    return out


def chunks(path):
    return re.split(r'(?=^--- (?:a/|/dev/null))', path.read_text(errors='surrogateescape'), flags=re.M)


def check_context():
    patches = sorted(PATCH.parent.glob('*.patch'))
    for prior in patches:
        if prior.name == PATCH.name:
            continue
        text = prior.read_text(errors='surrogateescape')
        for name in FILES:
            header = r'^\+\+\+ b/bsd/dev/' if prior.name < PATCH.name else r'^--- /dev/null\n\+\+\+ b/bsd/dev/'
            assert not re.search(header + re.escape(name) + '$', text, re.M), prior.name + ' already creates ' + name
        for name in IOKIT_FILES:
            header = r'^\+\+\+ b/iokit/Kernel/arm/' if prior.name < PATCH.name else r'^--- /dev/null\n\+\+\+ b/iokit/Kernel/arm/'
            assert not re.search(header + re.escape(name) + '$', text, re.M), prior.name + ' already creates ' + name
        if '0117-fuxi-adsp.patch' < prior.name < PATCH.name:
            for path in ('bsd/conf/files', 'bsd/dev/sweet_ufs.c'):
                assert not re.search(r'^\+\+\+ b/' + re.escape(path) + '$', text, re.M), prior.name + ' touches ' + path
        if '0109-fuxi-backlight.patch' < prior.name < PATCH.name:
            assert not re.search(r'^\+\+\+ b/iokit/conf/files\.arm64$', text, re.M), prior.name + ' touches iokit/conf/files.arm64'
    # 0117 的建置清單 hunk：最後加的兩行就是 BEFORE，緊接 AFTER 的三行上下文。
    adsp = (PATCH.parent / '0117-fuxi-adsp.patch').read_text(errors='surrogateescape')
    tail = ''.join('+' + x + '\n' for x in BEFORE) + ''.join(' ' + x + '\n' for x in AFTER)
    assert tail in adsp, '0117 build list no longer ends with ' + repr(BEFORE)
    # 0104 的 iokit/conf/files.arm64：FuxiWLAN / FuxiNetworkStack 之後緊接 SweetBattery / SweetHID。
    wifi = (PATCH.parent / '0104-fuxi-wifi-ios.patch').read_text(errors='surrogateescape')
    iokit_tail = ''.join('+' + x + '\n' for x in IOKIT_BEFORE) + ''.join(' ' + x + '\n' for x in IOKIT_AFTER)
    assert iokit_tail in wifi, '0104 iokit/conf/files.arm64 context changed'
    # 0106 留下的 sweet_ufs.c：宣告與呼叫的上下文（新側）。
    unixnu = (PATCH.parent / '0106-fuxi-unixnu.patch').read_text(errors='surrogateescape')
    for group in (EXTERN_BEFORE + EXTERN_AFTER, CALL_BEFORE + CALL_AFTER):
        body = ''.join(x[1:] + '\n' for x in unixnu.split('\n') if x[:1] in (' ', '+'))
        assert ''.join(x + '\n' for x in group) in body, 'sweet_ufs.c context changed: ' + repr(group[0])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--check', action='store_true')
    args = ap.parse_args()
    patch = expected()
    check_context()
    if args.check:
        assert PATCH.read_text(encoding='utf-8') == patch, 'fuxi-modem sources differ; run tools/sync-fuxi-modem.py'
    else:
        PATCH.write_text(patch, encoding='utf-8', newline='\n')
    print(f'PASS: fuxi-modem canonical sources match 0126 ({len(SOURCES)} bsd + {len(IOKIT_SOURCES)} iokit sources, {len(HEADERS) + len(IOKIT_EXTRA)} headers/extra)')


if __name__ == '__main__':
    main()
