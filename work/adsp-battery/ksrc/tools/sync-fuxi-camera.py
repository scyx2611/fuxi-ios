#!/usr/bin/env python3
"""產生／檢查補丁 0125（fuxi-camera）：前鏡頭 cam-sensor1 上電與辨識（開機參數 fuxicamera 或 ADT /chosen fuxi,camera，預設關）。

正本在 xnu/fuxi-camera/：fuxi_camera.c 與純邏輯 fuxi_camera_proto.h（主機測試 tools/test-fuxi-camera.py 用同一份）。
兩個檔都以新檔放進 bsd/dev/；它用到的 QcomRpmh.h、fuxi_rpmh_shared.h 由 0117 安裝。.c 加進 bsd/conf/files，接在 0124
最後兩行（fuxi_modem.c、fuxi_modem_fw.c）之後；sweet_ufs.c 在 fuxi_modem_init() 之後呼叫 fuxi_camera_init()。
做法照 tools/sync-fuxi-modem.py。--check 另外確認：沒有其他補丁建立這兩個路徑；0124 之後、0125 之前的補丁不碰
bsd/conf/files 與 sweet_ufs.c；上下文行確實是 0124 加的那幾行。
"""
import argparse
import difflib
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'xnu/fuxi-camera'
PATCH = ROOT / 'xnu/patches/0127-fuxi-camera.patch'
PRIOR = ROOT / 'xnu/patches/0126-fuxi-modem.patch'
SOURCES = ('fuxi_camera.c', 'fuxi_camera_dylib.c')
DEV_HEADERS = ('fuxi_camera_proto.h', 'fuxi_camera_dylib_img.h')
KERN_HEADERS = ()
DEV_FILES = SOURCES + DEV_HEADERS
ALL_FILES = DEV_FILES + KERN_HEADERS


def entry(name):
    path = 'bsd/dev/' + name
    tabs = '\t' * max(1, (40 - len(path) + 7) // 8)
    return path + tabs + 'standard'


BEFORE = ('bsd/dev/fuxi_audioreach.c\t\tstandard', entry('fuxi_modem.c'), entry('fuxi_modem_fw.c'))
AFTER = ('bsd/dev/mem.c\t\t\t\tstandard', 'bsd/dev/munge.c \t\t\toptional config_requires_u32_munging',
         'bsd/dev/unix_startup.c\t\t\tstandard')
FILES_LINE = 155  # fuxi_audioreach.c 在 0001-0124 之後的位置（git apply 會找上下文，行號只是起點）
EXTERN_BEFORE = ('extern void fuxi_wlan_init(void);', 'extern void fuxi_adsp_init(void);', 'extern void fuxi_modem_init(void);')
EXTERN_ADD = ('extern void fuxi_camera_init(void);',)
EXTERN_AFTER = ('extern void sweet_usb_probe(void);', '#if defined(FUXI)', 'static void fx_log_start(void);')
EXTERN_LINE = 64
CALL_BEFORE = ('\tfuxi_adsp_init();',
               '\t/* Modem (MPSS) owner, boot-arg fuximodem or ADT fuxi,modem (bsd/dev/fuxi_modem.c); off by default, waits for the '
               'ADSP. */',
               '\tfuxi_modem_init();')
CALL_ADD = ('\t/* Front camera (cam-sensor1) power-up and probe, boot-arg fuxicamera or ADT fuxi,camera (bsd/dev/fuxi_camera.c); '
            'off by default. */',
            '\tfuxi_camera_init();')
CALL_AFTER = ('#else', '\tsweet_log_start();', '\tsweet_sample_start();')
CALL_LINE = 1087

BSD_INIT_LINE = 1011
BSD_INIT_BEFORE = (
    '#if DEVFS',
    '\t{',
    '\t\tchar mounthere[] = "/dev"; /* !const because of internal casting */',
    '',
    '\t\tbsd_init_kprintf("calling devfs_kernel_mount\\n");',
    '\t\tdevfs_kernel_mount(mounthere);',
    '\t}',
    '#endif /* DEVFS */',
)
BSD_INIT_ADD = (
    '',
    '#if defined(FUXI)',
    '\t{',
    '\t\textern void fuxi_camera_dylib_mount_init(void);',
    '\t\tfuxi_camera_dylib_mount_init();',
    '\t}',
    '#endif /* FUXI */',
)
BSD_INIT_AFTER = (
    '',
    '#if CONFIG_BASESYSTEMROOT',
    '#if CONFIG_IMAGEBOOT',
)

AMFI_SIG_LINES = (
    ' \tmount_t mp = vnode_mount(vp);',
    ' \t*cs_flags |= CS_SIGNED | CS_VALID;',
    '-\tif (mp != NULL && (vfs_flags(mp) & MNT_ROOTFS)) {',
    '+\textern int fuxi_camera_dylib_is_mounted(void);',
    '+\tif (mp != NULL && ((vfs_flags(mp) & MNT_ROOTFS) ||',
    '+\t    (fuxi_camera_dylib_is_mounted() &&',
    '+\t     strcmp(vfs_statfs(mp)->f_mntonname, "/Developer") == 0))) {',
    ' \t\t*cs_flags |= CS_PLATFORM_BINARY;',
    ' \t}',
    ' \tif (cs_blob != cs_blob->csb_ro_addr && cs_blob->csb_entitlements == NULL) {',
    ' \t\tsweet_amfi_attach_entitlements(cs_blob);',
    ' \t}',
    ' \treturn 0;',
    ' }',
)

EXEC_LINES = (
    ' #if defined(ARM64_BOARD_CONFIG_SWEET)',
    ' \t/* fxinj: launchd drops DYLD_ variables from com.apple jobs; the library is added here, after the own environment */',
    ' \tif (imgp->ip_ndp != NULL) {',
    '+\t\textern int fuxi_camera_dylib_is_mounted(void);',
    '+\t\textern int fuxi_camera_dylib_enabled(void);',
    '+\t\textern uint32_t fuxi_modem_bb_mode(void);',
    ' \t\tunsigned mode = fuxi_inject_mode();',
    ' \t\tconst char *library = mode != 0 ? fuxi_inject_library(mode, imgp->ip_ndp->ni_cnd.cn_pnbuf,',
    ' \t\t    imgp->ip_ndp->ni_cnd.cn_nameptr) : NULL;',
    ' \t\tchar inject_env[FUXI_INJECT_ENV_MAX];',
    '+\t\tstatic char bb_oslog_env[] = "FUXI_BB_OSLOG=1";',
    '+\t\tchar bb_mode_env[32];',
    '+\t\tchar *extra_env = NULL;',
    '+\t\tbool have_env = false;',
    '+\t\tbool cam_inject = false;',
    '+\t\tuint32_t bb_inject = 0;',
    ' ',
    '-\t\tif (fuxi_inject_env(library, inject_env, sizeof(inject_env))) {',
    '+\t\tif (fuxi_camera_dylib_is_mounted() &&',
    '+\t\t    imgp->ip_ndp->ni_cnd.cn_nameptr != NULL &&',
    '+\t\t    fuxi_camera_dylib_enabled() &&',
    '+\t\t    ((strcmp(imgp->ip_ndp->ni_cnd.cn_nameptr, "mediaserverd") == 0 &&',
    '+\t\t      strcmp(imgp->ip_ndp->ni_cnd.cn_pnbuf, "/usr/sbin/mediaserverd") == 0) ||',
    '+\t\t     (strcmp(imgp->ip_ndp->ni_cnd.cn_nameptr, "Camera") == 0 &&',
    '+\t\t      strcmp(imgp->ip_ndp->ni_cnd.cn_pnbuf, "/Applications/Camera.app/Camera") == 0))) {',
    '+\t\t\tint n = library != NULL ?',
    '+\t\t\t    snprintf(inject_env, sizeof(inject_env), "DYLD_INSERT_LIBRARIES=%s:/Developer/libfuxi-camera.dylib", library) :',
    '+\t\t\t    snprintf(inject_env, sizeof(inject_env), "DYLD_INSERT_LIBRARIES=/Developer/libfuxi-camera.dylib");',
    '+\t\t\thave_env = n > 0 && (size_t)n < sizeof(inject_env);',
    '+\t\t\tif (have_env) {',
    '+\t\t\t\tlibrary = inject_env + sizeof("DYLD_INSERT_LIBRARIES=") - 1;',
    '+\t\t\t\tcam_inject = true;',
    '+\t\t\t}',
    '+\t\t} else if (fuxi_camera_dylib_is_mounted() &&',
    '+\t\t    imgp->ip_ndp->ni_cnd.cn_nameptr != NULL &&',
    '+\t\t    fuxi_modem_bb_mode() != 0 &&',
    '+\t\t    ((strcmp(imgp->ip_ndp->ni_cnd.cn_nameptr, "launchd") == 0 &&',
    '+\t\t      strcmp(imgp->ip_ndp->ni_cnd.cn_pnbuf, "/sbin/launchd") == 0) ||',
    '+\t\t     ((fuxi_modem_bb_mode() & 4u) == 0 &&',
    '+\t\t      strcmp(imgp->ip_ndp->ni_cnd.cn_nameptr, "CommCenter") == 0 &&',
    '+\t\t      strcmp(imgp->ip_ndp->ni_cnd.cn_pnbuf, "/System/Library/Frameworks/CoreTelephony.framework/Support/CommCenter") == 0) ||',
    '+\t\t     ((fuxi_modem_bb_mode() & 0x800u) != 0 &&',
    '+\t\t      strcmp(imgp->ip_ndp->ni_cnd.cn_nameptr, "avconferenced") == 0 &&',
    '+\t\t      (strcmp(imgp->ip_ndp->ni_cnd.cn_pnbuf, "/usr/libexec/avconferenced") == 0 ||',
    '+\t\t       strcmp(imgp->ip_ndp->ni_cnd.cn_pnbuf, "/usr/sbin/avconferenced") == 0)))) {',
    '+\t\t\tuint32_t bb_mode = fuxi_modem_bb_mode();',
    '+\t\t\tbool is_launchd = imgp->ip_ndp->ni_cnd.cn_nameptr[0] == \'l\';',
    '+\t\t\tint n = library != NULL ?',
    '+\t\t\t    snprintf(inject_env, sizeof(inject_env), "DYLD_INSERT_LIBRARIES=%s:/Developer/libfuxi-bb.dylib", library) :',
    '+\t\t\t    snprintf(inject_env, sizeof(inject_env), "DYLD_INSERT_LIBRARIES=/Developer/libfuxi-bb.dylib");',
    '+\t\t\thave_env = n > 0 && (size_t)n < sizeof(inject_env);',
    '+\t\t\tif (have_env) {',
    '+\t\t\t\tlibrary = inject_env + sizeof("DYLD_INSERT_LIBRARIES=") - 1;',
    '+\t\t\t\tbb_inject = bb_mode;',
    '+\t\t\t\tif (is_launchd && (bb_mode & 2u) != 0) {',
    '+\t\t\t\t\textra_env = bb_oslog_env;',
    '+\t\t\t\t} else if (!is_launchd && (bb_mode & ~7u) != 0) {',
    '+\t\t\t\t\tif (snprintf(bb_mode_env, sizeof(bb_mode_env), "FUXI_BB_MODE=%#x", bb_mode) > 0) {',
    '+\t\t\t\t\t\textra_env = bb_mode_env;',
    '+\t\t\t\t\t}',
    '+\t\t\t\t}',
    '+\t\t\t}',
    '+\t\t} else {',
    '+\t\t\thave_env = fuxi_inject_env(library, inject_env, sizeof(inject_env));',
    '+\t\t}',
    '+\t\tif (have_env) {',
    ' \t\t\terror = exec_add_user_string(imgp, CAST_USER_ADDR_T(inject_env), UIO_SYSSPACE, TRUE);',
    ' \t\t\tif (error) {',
    ' \t\t\t\tgoto bad;',
    ' \t\t\t}',
    ' \t\t\tif (imgp->ip_argspace < new_ptr_size) {',
    ' \t\t\t\terror = E2BIG;',
    ' \t\t\t\tgoto bad;',
    ' \t\t\t}',
    ' \t\t\timgp->ip_argspace -= new_ptr_size;',
    ' \t\t\timgp->ip_envc++;',
    '+\t\t\tif (extra_env != NULL) {',
    '+\t\t\t\terror = exec_add_user_string(imgp, CAST_USER_ADDR_T(extra_env), UIO_SYSSPACE, TRUE);',
    '+\t\t\t\tif (error) {',
    '+\t\t\t\t\tgoto bad;',
    '+\t\t\t\t}',
    '+\t\t\t\tif (imgp->ip_argspace < new_ptr_size) {',
    '+\t\t\t\t\terror = E2BIG;',
    '+\t\t\t\t\tgoto bad;',
    '+\t\t\t\t}',
    '+\t\t\t\timgp->ip_argspace -= new_ptr_size;',
    '+\t\t\t\timgp->ip_envc++;',
    '+\t\t\t}',
    ' \t\t\tfuxi_inject_said(mode, imgp->ip_ndp->ni_cnd.cn_nameptr, library);',
    '+\t\t\tif (cam_inject) {',
    '+\t\t\t\tprintf("FUXI CAMERA: %s <- %s\\n", imgp->ip_ndp->ni_cnd.cn_nameptr, library);',
    '+\t\t\t}',
    '+\t\t\tif (bb_inject != 0) {',
    '+\t\t\t\tprintf("FUXI BB: %s <- %s (mode=%#x)\\n",',
    '+\t\t\t\t    imgp->ip_ndp->ni_cnd.cn_nameptr, library, bb_inject);',
    '+\t\t\t}',
    ' \t\t}',
    ' \t}',
    ' #endif /* ARM64_BOARD_CONFIG_SWEET */',
)


def hunk(line, before, added, after, shift=0):
    lines = [' ' + x for x in before] + ['+' + x for x in added] + [' ' + x for x in after]
    old = len(before) + len(after)
    return f'@@ -{line},{old} +{line + shift},{old + len(added)} @@\n' + ''.join(x + '\n' for x in lines)


def raw_hunk(line, lines, shift=0):
    old = sum(1 for x in lines if x[:1] in (' ', '-'))
    new = sum(1 for x in lines if x[:1] in (' ', '+'))
    return f'@@ -{line},{old} +{line + shift},{new} @@\n' + ''.join(x + '\n' for x in lines)


def expected():
    for name in ALL_FILES:
        data = (SRC / name).read_bytes()
        assert b'\r' not in data, name + ' has CR line endings'
        assert data.endswith(b'\n'), name + ' lacks a final newline'
    out = '--- a/bsd/conf/files\n+++ b/bsd/conf/files\n'
    out += hunk(FILES_LINE, BEFORE, [entry(name) for name in SOURCES], AFTER)
    out += '--- a/bsd/dev/sweet_ufs.c\n+++ b/bsd/dev/sweet_ufs.c\n'
    out += hunk(EXTERN_LINE, EXTERN_BEFORE, EXTERN_ADD, EXTERN_AFTER)
    out += hunk(CALL_LINE, CALL_BEFORE, CALL_ADD, CALL_AFTER, shift=len(EXTERN_ADD))
    out += '--- a/bsd/kern/bsd_init.c\n+++ b/bsd/kern/bsd_init.c\n'
    out += hunk(BSD_INIT_LINE, BSD_INIT_BEFORE, BSD_INIT_ADD, BSD_INIT_AFTER)
    out += '--- a/bsd/kern/sweet_amfi.c\n+++ b/bsd/kern/sweet_amfi.c\n'
    out += raw_hunk(275, AMFI_SIG_LINES)
    out += '--- a/bsd/kern/kern_exec.c\n+++ b/bsd/kern/kern_exec.c\n'
    out += raw_hunk(5876, EXEC_LINES)
    for name in DEV_FILES:
        out += ''.join(difflib.unified_diff([], (SRC / name).read_text(encoding='utf-8').splitlines(True), fromfile='/dev/null',
                                            tofile='b/bsd/dev/' + name))
    for name in KERN_HEADERS:
        out += ''.join(difflib.unified_diff([], (SRC / name).read_text(encoding='utf-8').splitlines(True), fromfile='/dev/null',
                                            tofile='b/bsd/kern/' + name))
    return out


def check_context():
    for prior in sorted(PATCH.parent.glob('*.patch')):
        if prior.name == PATCH.name:
            continue
        text = prior.read_text(errors='surrogateescape')
        for name in DEV_FILES:
            header = r'^\+\+\+ b/bsd/dev/' if prior.name < PATCH.name else r'^--- /dev/null\n\+\+\+ b/bsd/dev/'
            assert not re.search(header + re.escape(name) + '$', text, re.M), prior.name + ' already creates ' + name
        for name in KERN_HEADERS:
            header = r'^\+\+\+ b/bsd/kern/' if prior.name < PATCH.name else r'^--- /dev/null\n\+\+\+ b/bsd/kern/'
            assert not re.search(header + re.escape(name) + '$', text, re.M), prior.name + ' already creates ' + name
        if PRIOR.name < prior.name < PATCH.name:
            for path in ('bsd/conf/files', 'bsd/dev/sweet_ufs.c'):
                assert not re.search(r'^\+\+\+ b/' + re.escape(path) + '$', text, re.M), prior.name + ' touches ' + path
    # 0124 加的行與它的上下文（新側）必須正好是這裡用的上下文。
    modem = PRIOR.read_text(errors='surrogateescape')
    body = ''.join(x[1:] + '\n' for x in modem.split('\n') if x[:1] in (' ', '+'))
    for group in (BEFORE + AFTER, EXTERN_BEFORE + EXTERN_AFTER, CALL_BEFORE + CALL_AFTER):
        assert ''.join(x + '\n' for x in group) in body, '0124 context changed: ' + repr(group[0])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--check', action='store_true')
    args = ap.parse_args()
    patch = expected()
    check_context()
    if args.check:
        assert PATCH.read_text(encoding='utf-8') == patch, 'fuxi-camera sources differ; run tools/sync-fuxi-camera.py'
    else:
        PATCH.write_text(patch, encoding='utf-8', newline='\n')
    print(f'PASS: fuxi-camera canonical sources match 0127 ({len(SOURCES)} sources, {len(DEV_HEADERS) + len(KERN_HEADERS)} headers)')


if __name__ == '__main__':
    main()
