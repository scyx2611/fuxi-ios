#!/usr/bin/env python3
"""000 第三方 App 對稱安全移除（guest 端）：對稱 install-app-guest.py，依 /mnt/share/remove-app.txt
安全移除經由 --install-app 裝入 /Applications 的無 DRM 第三方 App。
嚴守 7 層縱深防誤刪保護（格式正則、com.apple.* 大小寫不敏感封鎖、52 項系統 App 保護集、
動態 rootfs staged/placeholder 比對、非 symlink 與路徑正規檢查、Info.plist CFBundleIdentifier 精確一致、
launchd.plist LaunchDaemons 不變與 IconState.plist SHA-256 前後不變）。
移除後僅清理 /Applications/<Name>/ 前綴的 AppExtensions 快取、呼叫 sweet-root-tweaks.py
rebuild_launchservices() 重建資料庫、以及精確對應此 bundle_id 的 Preferences/Caches/SavedState。

usage: remove-app-guest.py remove ROOT SHARE          （guest，root 執行）
       remove-app-guest.py self-test TWEAKS            （主機測試，WSL root 執行）
"""
import copy
import glob
import hashlib
import importlib.util
import os
import plistlib
import re
import shutil
import sys
import tempfile

CHUNK = 8 << 20
NAME_RE = re.compile(r'[A-Za-z0-9][A-Za-z0-9._-]{0,63}\.app')
BUNDLE_ID_RE = re.compile(r'[A-Za-z0-9][A-Za-z0-9.-]{0,127}')

# 32 個 staged_system_apps + 專案實證核心系統 App + 常見系統 App 補強（共 52 項，大小寫不敏感）
PROTECTED_APP_NAMES = (
    # 32 個 staged_system_apps (apps-install-1117/guest.log:34)
    'AppleTV.app', 'Books.app', 'Bridge.app', 'Calculator.app', 'Compass.app',
    'Contacts.app', 'FaceTime.app', 'Files.app', 'FindMy.app', 'Fitness.app',
    'Freeform.app', 'Health.app', 'Home.app', 'Magnifier.app', 'Maps.app',
    'Measure.app', 'MobileCal.app', 'MobileMail.app', 'MobileNotes.app',
    'MobileStore.app', 'MobileTimer.app', 'Music.app', 'News.app',
    'Passbook.app', 'Podcasts.app', 'Reminders.app', 'SequoiaTranslator.app',
    'Shortcuts.app', 'Stocks.app', 'Tips.app', 'VoiceMemos.app', 'Weather.app',
    # 專案實證核心系統 App (9 項)
    'AppStore.app', 'Batteries.app', 'Camera.app', 'MobilePhone.app',
    'MobileSafari.app', 'MobileSlideShow.app', 'MobileSMS.app',
    'Preferences.app', 'SpringBoard.app',
    # 常見 iOS 系統 App 防禦補強 (11 項)
    'Diagnostics.app', 'Feedback.app', 'FieldTest.app', 'Messages.app',
    'Phone.app', 'Photos.app', 'Safari.app', 'Settings.app', 'Setup.app',
    'Web.app', 'WebSheet.app',
)
PROTECTED_APP_NAMES_LOWER = frozenset(item.lower() for item in PROTECTED_APP_NAMES)

SYSTEM_APP_DIRS = (
    'System/Library/AppPlaceholders',
    'System/Library/AppPlaceholders.fuxi-staged',
    'private/var/staged_system_apps',
)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def load_tweaks(path):
    require(os.path.lexists(path) and not os.path.islink(path) and os.path.isfile(path),
            f'missing or unsafe tweaks script: {path}')
    spec = importlib.util.spec_from_file_location('sweet_root_tweaks', path)
    require(spec is not None and spec.loader is not None, f'cannot load tweaks spec: {path}')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def read_inputs(share):
    req_path = os.path.join(share, 'remove-app.txt')
    require(os.path.lexists(req_path) and not os.path.islink(req_path) and os.path.isfile(req_path),
            'missing or unsafe remove-app.txt')
    with open(req_path, 'rb') as stream:
        raw = stream.read()
    require(b'\r' not in raw, 'remove-app.txt must use LF line endings')
    text = raw.decode('ascii')
    lines = text.split('\n')
    require(len(lines) == 3 and lines[2] == '', 'remove-app.txt must hold two lines')
    name, bundle_id = lines[:2]
    # 第 1 層：輸入語法正則
    require(NAME_RE.fullmatch(name) is not None, f'unexpected app name: {name!r}')
    require(BUNDLE_ID_RE.fullmatch(bundle_id) is not None, f'unexpected bundle identifier: {bundle_id!r}')
    return name, bundle_id


def check_system_protection(root, name, bundle_id):
    # 第 2 層：com.apple.* 前綴封鎖（大小寫不敏感）
    require(not bundle_id.lower().startswith('com.apple.'),
            f'refusing to remove Apple bundle identifier: {bundle_id}')
    # 第 3 層：顯式系統 App 保護集（大小寫不敏感）
    require(name.lower() not in PROTECTED_APP_NAMES_LOWER,
            f'refusing to remove protected system app: {name}')

    # 第 4 層：動態 rootfs 系統保護比對
    staged_manifest = os.path.join(
        root, 'System/Library/AppPlaceholders.fuxi-staged/.fuxi-installed-staged-apps.plist')
    if os.path.lexists(staged_manifest):
        require(not os.path.islink(staged_manifest) and os.path.isfile(staged_manifest),
                'unsafe staged apps manifest')
        with open(staged_manifest, 'rb') as stream:
            entries = plistlib.load(stream)
        require(isinstance(entries, list), 'unsafe staged apps manifest format')
        for entry in entries:
            if not isinstance(entry, dict):
                continue
            entry_name = entry.get('name')
            entry_id = entry.get('bundleIdentifier')
            entry_placeholder = entry.get('placeholder')
            if isinstance(entry_name, str) and entry_name.lower() == name.lower():
                raise ValueError(f'refusing to remove staged system app listed in manifest: {name}')
            if isinstance(entry_placeholder, str) and entry_placeholder.lower() == name.lower():
                raise ValueError(f'refusing to remove staged placeholder listed in manifest: {name}')
            if isinstance(entry_id, str) and entry_id.lower() == bundle_id.lower():
                raise ValueError(f'refusing to remove staged bundle identifier: {bundle_id}')

    for rel_dir in SYSTEM_APP_DIRS:
        sys_dir = os.path.join(root, rel_dir)
        if not os.path.lexists(sys_dir):
            continue
        require(not os.path.islink(sys_dir) and os.path.isdir(sys_dir),
                f'unsafe system directory: {rel_dir}')
        for entry in sorted(os.listdir(sys_dir)):
            if not entry.lower().endswith('.app'):
                continue
            if entry.lower() == name.lower():
                raise ValueError(f'refusing to remove app matching {rel_dir}/{entry}')
            app_path = os.path.join(sys_dir, entry)
            if os.path.islink(app_path) or not os.path.isdir(app_path):
                continue
            info_path = os.path.join(app_path, 'Info.plist')
            if os.path.lexists(info_path) and not os.path.islink(info_path) and os.path.isfile(info_path):
                try:
                    with open(info_path, 'rb') as stream:
                        info_data = plistlib.load(stream)
                except Exception:
                    continue
                if isinstance(info_data, dict):
                    sys_id = info_data.get('CFBundleIdentifier')
                    if isinstance(sys_id, str) and sys_id.lower() == bundle_id.lower():
                        raise ValueError(f'refusing to remove bundle identifier matching {rel_dir}/{entry}: {bundle_id}')


def validate_target_bundle(root, name, bundle_id):
    # 第 5 層：路徑與非 symlink 防護
    require(os.path.lexists(root) and not os.path.islink(root) and os.path.isdir(root),
            f'invalid root directory: {root}')
    applications = os.path.join(root, 'Applications')
    require(os.path.lexists(applications) and not os.path.islink(applications) and os.path.isdir(applications),
            'root has no real /Applications directory')

    for suffix in ('.fuxi-partial', '.fuxi-removing'):
        leftover = os.path.join(applications, '.' + name + suffix)
        require(not os.path.lexists(leftover), f'leftover staging entry present: {leftover}')

    target = os.path.join(applications, name)
    require(os.path.lexists(target), f'{name} is not present in /Applications')
    require(not os.path.islink(target), f'refusing symlink app target: {target}')
    require(os.path.isdir(target), f'app target is not a directory: {target}')

    real_apps = os.path.realpath(applications)
    real_target = os.path.realpath(target)
    require(real_target == os.path.join(real_apps, name),
            f'unsafe target realpath: {real_target}')

    # 預掃 os.walk(..., followlinks=False)：不跟隨任何 symlink
    for dirpath, dirnames, _filenames in os.walk(target, followlinks=False):
        require(not os.path.islink(dirpath), f'unexpected symlink directory during pre-scan: {dirpath}')
        real_dir = os.path.realpath(dirpath)
        require(real_dir == real_target or real_dir.startswith(real_target + os.sep),
                f'directory escapes target root: {dirpath}')
        dirnames[:] = [d for d in dirnames if not os.path.islink(os.path.join(dirpath, d))]

    info_plist = os.path.join(target, 'Info.plist')
    require(os.path.lexists(info_plist) and not os.path.islink(info_plist) and os.path.isfile(info_plist),
            f'missing or unsafe Info.plist in {name}')

    # 第 6 層：Info.plist CFBundleIdentifier 精確一致與二次 com.apple.* 拒絕
    try:
        with open(info_plist, 'rb') as stream:
            info_data = plistlib.load(stream)
    except Exception as exc:
        raise ValueError(f'unreadable Info.plist in {name}: {exc}') from exc
    require(isinstance(info_data, dict), f'Info.plist in {name} is not a dictionary')
    actual_id = info_data.get('CFBundleIdentifier')
    require(isinstance(actual_id, str) and actual_id == bundle_id,
            f'CFBundleIdentifier mismatch for {name}: expected {bundle_id!r}, got {actual_id!r}')
    require(not actual_id.lower().startswith('com.apple.'),
            f'refusing to remove Apple app discovered in Info.plist: {actual_id}')
    return applications, target


def file_sha256(path):
    if not os.path.lexists(path):
        return None
    require(not os.path.islink(path) and os.path.isfile(path), f'unsafe file for digest: {path}')
    digest = hashlib.sha256()
    with open(path, 'rb') as stream:
        for chunk in iter(lambda: stream.read(CHUNK), b''):
            digest.update(chunk)
    return digest.hexdigest()


def atomic_write_launchd(launchd, data, uid, gid, mode):
    temporary = os.path.join(os.path.dirname(launchd), 'launchd.plist.fuxitmp')
    if os.path.lexists(temporary):
        require(not os.path.islink(temporary) and os.path.isfile(temporary), 'unsafe temporary launchd file')
        os.unlink(temporary)
    with open(temporary, 'wb') as stream:
        plistlib.dump(data, stream, fmt=plistlib.FMT_BINARY)
        stream.flush()
        os.fsync(stream.fileno())
    os.chown(temporary, uid, gid)
    os.chmod(temporary, mode)
    os.replace(temporary, launchd)


def clean_user_data(root, bundle_id):
    # 僅在通過非 com.apple.* 與完整驗證後，精確清理對應此 bundle_id 之使用者資料
    require(BUNDLE_ID_RE.fullmatch(bundle_id) is not None and not bundle_id.lower().startswith('com.apple.'),
            'unsafe bundle identifier for user data cleanup')
    mobile_var = os.path.join(root, 'private/var/mobile')
    mobile_lib = os.path.join(mobile_var, 'Library')
    if not os.path.lexists(mobile_lib) or os.path.islink(mobile_var) or os.path.islink(mobile_lib):
        return
    if not os.path.isdir(mobile_lib):
        return

    prefs_dir = os.path.join(mobile_lib, 'Preferences')
    pref_plist = os.path.join(prefs_dir, f'{bundle_id}.plist')
    if (os.path.lexists(prefs_dir) and not os.path.islink(prefs_dir) and
            os.path.lexists(pref_plist) and not os.path.islink(pref_plist) and os.path.isfile(pref_plist)):
        os.remove(pref_plist)

    caches_dir = os.path.join(mobile_lib, 'Caches')
    app_cache = os.path.join(caches_dir, bundle_id)
    if (os.path.lexists(caches_dir) and not os.path.islink(caches_dir) and
            os.path.lexists(app_cache) and not os.path.islink(app_cache) and os.path.isdir(app_cache)):
        shutil.rmtree(app_cache)

    saved_dir = os.path.join(mobile_lib, 'Saved Application State')
    app_saved = os.path.join(saved_dir, f'{bundle_id}.savedState')
    if (os.path.lexists(saved_dir) and not os.path.islink(saved_dir) and
            os.path.lexists(app_saved) and not os.path.islink(app_saved) and os.path.isdir(app_saved)):
        shutil.rmtree(app_saved)


def validate_launchd_path(root):
    cur = root
    for part in ('System', 'Library', 'xpc'):
        cur = os.path.join(cur, part)
        require(os.path.lexists(cur) and not os.path.islink(cur) and os.path.isdir(cur),
                f'unsafe symlink or missing launchd ancestor directory: {cur}')
    launchd = os.path.join(cur, 'launchd.plist')
    require(os.path.lexists(launchd) and not os.path.islink(launchd) and os.path.isfile(launchd),
            'root has no regular launchd.plist')
    tmp = launchd + '.fuxitmp'
    require(not os.path.islink(tmp), f'unsafe temporary launchd file: {tmp}')
    if os.path.lexists(tmp):
        require(os.path.isfile(tmp), f'unsafe temporary launchd file: {tmp}')
    return launchd


def validate_ls_database_paths(root):
    cur = root
    for part in ('private', 'var', 'mobile', 'Containers', 'Data', 'InternalDaemon'):
        cur = os.path.join(cur, part)
        if not os.path.lexists(cur):
            return
        require(not os.path.islink(cur), f'unsafe symlink on LaunchServices path: {cur}')
        require(os.path.isdir(cur), f'unsafe non-directory on LaunchServices path: {cur}')
    with os.scandir(cur) as daemon_it:
        for daemon_entry in daemon_it:
            require(not daemon_entry.is_symlink(), f'unsafe symlink on LaunchServices path: {daemon_entry.path}')
            if not daemon_entry.is_dir(follow_symlinks=False):
                continue
            lib_dir = os.path.join(daemon_entry.path, 'Library')
            if not os.path.lexists(lib_dir):
                continue
            require(not os.path.islink(lib_dir), f'unsafe symlink on LaunchServices path: {lib_dir}')
            if not os.path.isdir(lib_dir):
                continue
            caches_dir = os.path.join(lib_dir, 'Caches')
            if not os.path.lexists(caches_dir):
                continue
            require(not os.path.islink(caches_dir), f'unsafe symlink on LaunchServices path: {caches_dir}')
            if not os.path.isdir(caches_dir):
                continue
            with os.scandir(caches_dir) as cache_it:
                for cs_entry in cache_it:
                    if 'com.apple.LaunchServices-' in cs_entry.name and cs_entry.name.endswith('.csstore'):
                        require(not cs_entry.is_symlink() and cs_entry.is_file(follow_symlinks=False),
                                f'unsafe LaunchServices database entry: {cs_entry.path}')


def validate_chrono_snapshots(root, chrono_rel):
    parts = [part for part in chrono_rel.strip('/').split('/') if part]
    cur = root
    for part in parts:
        cur = os.path.join(cur, part)
        if not os.path.lexists(cur):
            return os.path.join(root, *parts), []
        require(not os.path.islink(cur), f'unsafe symlink on chrono snapshot cache path: {cur}')
        require(os.path.isdir(cur), f'unsafe non-directory on chrono snapshot cache path: {cur}')
    chrono_dir = os.path.join(root, *parts)
    regular_files = []
    with os.scandir(chrono_dir) as it:
        for entry in it:
            require(not entry.is_symlink(), f'unsafe symlink in chrono snapshot cache: {entry.path}')
            if entry.is_dir(follow_symlinks=False):
                continue
            require(entry.is_file(follow_symlinks=False),
                    f'unsafe non-regular entry in chrono snapshot cache: {entry.path}')
            regular_files.append(entry.path)
    return chrono_dir, regular_files


def remove(root, share, tweaks=None):
    name, bundle_id = read_inputs(share)
    tweaks = tweaks or load_tweaks(os.path.join(share, 'sweet-root-tweaks.py'))

    # 第 2–6 層全部在任何刪除或寫入前執行
    check_system_protection(root, name, bundle_id)
    _applications, target = validate_target_bundle(root, name, bundle_id)

    # 第 7 層前置快照與 launchd / LaunchServices / chrono 檢查（全於刪除前完成）
    icon_state = os.path.join(root, 'private/var/mobile/Library/SpringBoard/IconState.plist')
    icon_before = file_sha256(icon_state)

    launchd = validate_launchd_path(root)
    info = os.lstat(launchd)
    before_plist = tweaks.load(launchd)
    require(isinstance(before_plist, dict), 'launchd.plist is not a dictionary')
    before_daemons = copy.deepcopy(before_plist.get('LaunchDaemons'))
    before_ext = before_plist.get('AppExtensions', {})
    require(isinstance(before_ext, dict), 'launchd.plist AppExtensions is not a dictionary')
    prefix = f'/Applications/{name}/'
    stale_keys = [key for key in before_ext if key.startswith(prefix)]
    kept_ext_before = copy.deepcopy({k: v for k, v in before_ext.items() if not k.startswith(prefix)})
    other_top_before = copy.deepcopy({k: v for k, v in before_plist.items() if k != 'AppExtensions'})

    validate_ls_database_paths(root)
    chrono_dir, chrono_regular_files = validate_chrono_snapshots(root, tweaks.CHRONO_SNAPSHOTS)

    # 執行安全刪除 target，確保無殘留
    shutil.rmtree(target)
    require(not os.path.lexists(target), f'target still exists after removal: {target}')
    print('APP_REMOVED', name, bundle_id)

    # 僅移除 /Applications/<name>/ 前綴的 AppExtensions 鍵
    if stale_keys:
        updated = copy.deepcopy(before_plist)
        ext_cache = updated.get('AppExtensions', {})
        for key in stale_keys:
            del ext_cache[key]
        atomic_write_launchd(launchd, updated, info.st_uid, info.st_gid, info.st_mode & 0o7777)
        after_info = os.lstat(launchd)
        require((after_info.st_uid, after_info.st_gid, after_info.st_mode & 0o7777) ==
                (info.st_uid, info.st_gid, info.st_mode & 0o7777),
                'launchd.plist owner or mode changed')
        after_plist = tweaks.load(launchd)
        require(after_plist.get('LaunchDaemons') == before_daemons, 'LaunchDaemons changed during removal')
        require({k: v for k, v in after_plist.items() if k != 'AppExtensions'} == other_top_before,
                'launchd.plist top-level keys changed during removal')
        require(after_plist.get('AppExtensions', {}) == kept_ext_before,
                'unrelated AppExtensions entries changed during removal')
    print('EXTENSION_CACHE_REMOVED', len(stale_keys))

    # 在呼叫 tweaks.rebuild_launchservices 前安全移除 chrono_dir 內一般正規檔案（避免 shutil.rmtree 拋出 NotADirectoryError）
    for file_path in chrono_regular_files:
        if os.path.lexists(file_path):
            require(not os.path.islink(file_path) and os.path.isfile(file_path),
                    f'unsafe chrono file before unlink: {file_path}')
            os.unlink(file_path)

    # 重建 LaunchServices 快取（不觸碰其他系統狀態）
    tweaks.rebuild_launchservices(root)
    require(not glob.glob(os.path.join(root, tweaks.LS_DATABASES)), 'LaunchServices database left')
    if os.path.lexists(chrono_dir):
        require(not os.path.islink(chrono_dir) and os.path.isdir(chrono_dir),
                'unsafe chrono snapshot cache path after rebuild')
        require(os.listdir(chrono_dir) == [], 'chrono snapshot cache not empty after rebuild')

    # 精確清理此第三方 bundle_id 使用者資料
    clean_user_data(root, bundle_id)

    # 再次核對 IconState.plist SHA-256 與執行前完全一致
    icon_after = file_sha256(icon_state)
    require(icon_after == icon_before, 'IconState.plist changed during removal')
    print('REMOVE_APP_VERIFIED', name, bundle_id)


# ---------------------------------------------------------- self-test ----

def write_remove_txt(share, content):
    os.makedirs(share, exist_ok=True)
    with open(os.path.join(share, 'remove-app.txt'), 'wb') as stream:
        stream.write(content)


def build_synthetic_root(root, tweaks, installer):
    installer.fixture_root(root, tweaks)
    # 補強 LaunchDaemons、AppExtensions、staged manifest、Maps placeholder、chrono cache、IconState 與 UserData
    for rel in (
        'System/Library/AppPlaceholders/Maps.app',
        'System/Library/AppPlaceholders.fuxi-staged',
        'private/var/mobile/Library/SpringBoard',
        'private/var/mobile/Library/Caches/com.apple.chrono/snapshot-cache/snap1',
        'private/var/mobile/Library/Preferences',
        'private/var/mobile/Library/Caches/com.fuxi.nodrmverify.der',
        'private/var/mobile/Library/Saved Application State/com.fuxi.nodrmverify.der.savedState',
        'private/var/mobile/Library/Caches/com.example.old',
        'private/var/mobile/Library/Saved Application State/com.example.old.savedState',
    ):
        os.makedirs(os.path.join(root, rel), exist_ok=True)

    with open(os.path.join(root, 'System/Library/AppPlaceholders/Maps.app/Info.plist'), 'wb') as stream:
        plistlib.dump({'CFBundleIdentifier': 'com.apple.Maps'}, stream)

    staged_manifest = os.path.join(
        root, 'System/Library/AppPlaceholders.fuxi-staged/.fuxi-installed-staged-apps.plist')
    with open(staged_manifest, 'wb') as stream:
        plistlib.dump([
            {'name': 'Weather.app', 'bundleIdentifier': 'com.apple.weather', 'placeholder': 'Weather.app'},
            {'name': 'StagedCustom.app', 'bundleIdentifier': 'com.custom.staged', 'placeholder': ''},
        ], stream, fmt=plistlib.FMT_XML)

    launchd_path = os.path.join(root, 'System/Library/xpc/launchd.plist')
    tweaks.save(launchd_path, {
        'LaunchDaemons': {
            '/System/Library/LaunchDaemons/com.apple.SpringBoard.plist': {
                'Label': 'com.apple.SpringBoard',
                'RunAtLoad': True,
            }
        },
        'AppExtensions': {
            '/Applications/Old.app/PlugIns/A.appex': {'_executablePath': '/Applications/Old.app/PlugIns/A.appex/A'},
            '/Applications/App.app2/PlugIns/B.appex': {'_executablePath': '/Applications/App.app2/PlugIns/B.appex/B'},
        },
        'ExtraTopKey': {'keep': 1},
    }, 0, 0, 0o644)

    icon_state_path = os.path.join(root, 'private/var/mobile/Library/SpringBoard/IconState.plist')
    with open(icon_state_path, 'wb') as stream:
        plistlib.dump({'iconLists': [['com.apple.mobilesafari', 'com.example.old']]}, stream, fmt=plistlib.FMT_BINARY)

    with open(os.path.join(root, 'private/var/mobile/Library/Caches/com.apple.chrono/snapshot-cache/snap1/s.bin'), 'wb') as stream:
        stream.write(b'snap')
    for bid in ('com.fuxi.nodrmverify.der', 'com.example.old'):
        with open(os.path.join(root, f'private/var/mobile/Library/Preferences/{bid}.plist'), 'wb') as stream:
            plistlib.dump({'k': bid}, stream)
        with open(os.path.join(root, f'private/var/mobile/Library/Caches/{bid}/c.bin'), 'wb') as stream:
            stream.write(b'cache')
        with open(os.path.join(root, f'private/var/mobile/Library/Saved Application State/{bid}.savedState/s.bin'), 'wb') as stream:
            stream.write(b'saved')


def locate_slot7der(tweaks_path):
    candidates = [
        os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                     '..', 'localbuild', 'nodrm-apps-1126', 'b1-test-app', 'out', 'out-slot7der')),
        os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(tweaks_path)),
                                     '..', '..', '..', '..', 'work', 'localbuild', 'nodrm-apps-1126', 'b1-test-app', 'out', 'out-slot7der')),
    ]
    for path in candidates:
        if os.path.isfile(os.path.join(path, 'app.tar')) and os.path.isfile(os.path.join(path, 'files.sha256')):
            return path
    raise ValueError(f'cannot locate out-slot7der fixture from {candidates}')


def prepare_slot7der_share(slot7_dir, install_share):
    import json
    os.makedirs(install_share, exist_ok=True)
    with open(os.path.join(slot7_dir, 'manifest.json'), 'r', encoding='utf-8') as stream:
        manifest = json.load(stream)
    tar_path = os.path.join(slot7_dir, 'app.tar')
    tar_bytes = open(tar_path, 'rb').read()
    tar_sha = hashlib.sha256(tar_bytes).hexdigest()
    app_txt = f"{manifest['app']}\n{manifest['bundle_identifier']}\n{len(tar_bytes)}\n{tar_sha}\n"
    with open(os.path.join(install_share, 'app.txt'), 'w', encoding='ascii', newline='\n') as stream:
        stream.write(app_txt)
    shutil.copy2(os.path.join(slot7_dir, 'files.sha256'), os.path.join(install_share, 'files.sha256'))
    return tar_path, manifest['app'], manifest['bundle_identifier']


def refused(function, *values):
    try:
        function(*values)
    except (ValueError, KeyError, OSError, plistlib.InvalidFileException):
        return True
    return False


def self_test(tweaks_path):
    require(os.geteuid() == 0, 'self-test needs root (chown)')
    tweaks = load_tweaks(tweaks_path)
    installer_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'install-app-guest.py')
    spec = importlib.util.spec_from_file_location('install_app_guest', installer_path)
    installer = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(installer)
    slot7_dir = locate_slot7der(tweaks_path)

    with tempfile.TemporaryDirectory(prefix='fuxi-remove-app-') as temporary:
        # 1. 整合正向閉環：真實 out-slot7der 安裝 -> 移除 -> 驗證
        base1 = os.path.join(temporary, 'slot7der-cycle')
        root1 = os.path.join(base1, 'root')
        build_synthetic_root(root1, tweaks, installer)
        slot7_tar, slot7_name, slot7_id = prepare_slot7der_share(slot7_dir, os.path.join(base1, 'install-share'))
        installer.install(root1, slot7_tar, os.path.join(base1, 'install-share'), tweaks)
        require(os.path.isdir(os.path.join(root1, 'Applications', slot7_name)), 'slot7der not installed')

        # 補回 .csstore 與 chrono 快照以驗證移除步驟會確實清除
        csstore1 = os.path.join(
            root1, 'private/var/mobile/Containers/Data/InternalDaemon/X/Library/Caches/com.apple.LaunchServices-1.csstore')
        open(csstore1, 'wb').close()
        os.makedirs(os.path.join(root1, tweaks.CHRONO_SNAPSHOTS, 'snap-after-install'), exist_ok=True)

        icon_path1 = os.path.join(root1, 'private/var/mobile/Library/SpringBoard/IconState.plist')
        icon_bytes_before = open(icon_path1, 'rb').read()
        icon_sha_before = file_sha256(icon_path1)
        daemons_before = copy.deepcopy(tweaks.load(os.path.join(root1, 'System/Library/xpc/launchd.plist'))['LaunchDaemons'])

        share1 = os.path.join(base1, 'remove-share')
        write_remove_txt(share1, f'{slot7_name}\n{slot7_id}\n'.encode('ascii'))
        remove(root1, share1, tweaks)

        require(not os.path.lexists(os.path.join(root1, 'Applications', slot7_name)), 'slot7der app not removed')
        require(os.path.isfile(os.path.join(root1, 'Applications/Old.app/Info.plist')), 'Old.app damaged')
        launchd1 = tweaks.load(os.path.join(root1, 'System/Library/xpc/launchd.plist'))
        require(launchd1['LaunchDaemons'] == daemons_before, 'LaunchDaemons mutated')
        require('/Applications/Old.app/PlugIns/A.appex' in launchd1['AppExtensions'], 'Old.app AppExtensions lost')
        require(not glob.glob(os.path.join(root1, tweaks.LS_DATABASES)), 'csstore left behind')
        require(os.listdir(os.path.join(root1, tweaks.CHRONO_SNAPSHOTS)) == [], 'chrono cache not cleared')
        require(not os.path.lexists(os.path.join(root1, f'private/var/mobile/Library/Preferences/{slot7_id}.plist')),
                'slot7der pref left behind')
        require(not os.path.lexists(os.path.join(root1, f'private/var/mobile/Library/Caches/{slot7_id}')),
                'slot7der cache left behind')
        require(not os.path.lexists(os.path.join(root1, f'private/var/mobile/Library/Saved Application State/{slot7_id}.savedState')),
                'slot7der savedState left behind')
        require(os.path.isfile(os.path.join(root1, 'private/var/mobile/Library/Preferences/com.example.old.plist')),
                'Old.app pref deleted')
        require(os.path.isdir(os.path.join(root1, 'private/var/mobile/Library/Caches/com.example.old')),
                'Old.app cache deleted')
        require(os.path.isdir(os.path.join(root1, 'private/var/mobile/Library/Saved Application State/com.example.old.savedState')),
                'Old.app savedState deleted')
        require(open(icon_path1, 'rb').read() == icon_bytes_before and file_sha256(icon_path1) == icon_sha_before,
                'IconState.plist mutated')
        require(refused(remove, root1, share1, tweaks), 'duplicate remove accepted')

        # 2. 含 PlugIns/Share.appex 之第三方 App 安裝 -> 移除驗證（含 /Applications/App.app2 前綴隔離驗證）
        base2 = os.path.join(temporary, 'appex-cycle')
        root2 = os.path.join(base2, 'root')
        build_synthetic_root(root2, tweaks, installer)
        appex_info = plistlib.dumps({
            'CFBundleExecutable': 'Share',
            'CFBundleIdentifier': 'com.example.app.share',
            'NSExtension': {'NSExtensionPointIdentifier': 'com.apple.share-services'},
            'MinimumOSVersion': '16.0',
        })
        entries = [
            ('Info.plist', plistlib.dumps({'CFBundleExecutable': 'App', 'CFBundleIdentifier': 'com.example.app'}), installer.FILE_MODE),
            ('App', b'\xcf\xfa\xed\xfe' + bytes(4096), installer.EXEC_MODE),
            ('PlugIns', None, installer.DIR_MODE),
            ('PlugIns/Share.appex', None, installer.DIR_MODE),
            ('PlugIns/Share.appex/Info.plist', appex_info, installer.FILE_MODE),
            ('PlugIns/Share.appex/Share', b'\xcf\xfa\xed\xfe' + bytes(128), installer.EXEC_MODE),
        ]
        tar2 = os.path.join(base2, 'app.tar')
        install_share2 = os.path.join(base2, 'install-share')
        installer.fixture_tar(tar2, 'App.app', entries)
        installer.fixture_share(install_share2, 'App.app', 'com.example.app', tar2, entries)
        installer.install(root2, tar2, install_share2, tweaks)
        open(os.path.join(
            root2, 'private/var/mobile/Containers/Data/InternalDaemon/X/Library/Caches/com.apple.LaunchServices-2.csstore'),
            'wb').close()
        launchd_before2 = tweaks.load(os.path.join(root2, 'System/Library/xpc/launchd.plist'))
        require('/Applications/App.app/PlugIns/Share.appex' in launchd_before2['AppExtensions'],
                'appex was not registered before removal test')
        icon_sha2 = file_sha256(os.path.join(root2, 'private/var/mobile/Library/SpringBoard/IconState.plist'))
        share2 = os.path.join(base2, 'remove-share')
        write_remove_txt(share2, b'App.app\ncom.example.app\n')
        remove(root2, share2, tweaks)
        launchd_after2 = tweaks.load(os.path.join(root2, 'System/Library/xpc/launchd.plist'))
        require('/Applications/App.app/PlugIns/Share.appex' not in launchd_after2['AppExtensions'],
                'App.app AppExtensions key not removed')
        require('/Applications/App.app2/PlugIns/B.appex' in launchd_after2['AppExtensions'],
                'App.app2 AppExtensions key damaged by missing trailing slash')
        require('/Applications/Old.app/PlugIns/A.appex' in launchd_after2['AppExtensions'],
                'Old.app AppExtensions key lost')
        require(launchd_after2['LaunchDaemons'] == launchd_before2['LaunchDaemons'], 'LaunchDaemons mutated')
        require(launchd_after2['ExtraTopKey'] == {'keep': 1}, 'launchd top-level key lost')
        require(file_sha256(os.path.join(root2, 'private/var/mobile/Library/SpringBoard/IconState.plist')) == icon_sha2,
                'IconState.plist changed in appex test')
        require(refused(remove, root2, share2, tweaks), 'duplicate appex remove accepted')

        # 3. 完整負向拒絕案例（驗證拒絕且目標與 IconState.plist 完好）
        def make_neg_root(label):
            b = os.path.join(temporary, 'neg-' + label)
            r = os.path.join(b, 'root')
            s = os.path.join(b, 'share')
            build_synthetic_root(r, tweaks, installer)
            return b, r, s

        def plant_app(root, app_name, bundle_id):
            app_dir = os.path.join(root, 'Applications', app_name)
            os.makedirs(app_dir, exist_ok=True)
            with open(os.path.join(app_dir, 'Info.plist'), 'wb') as stream:
                plistlib.dump({'CFBundleIdentifier': bundle_id}, stream)
            return app_dir

        # 3a. 保護名稱與大小寫變形
        for label, app_name, bid in (
            ('Maps', 'Maps.app', 'com.example.fake'),
            ('Maps-lower', 'maps.app', 'com.example.fake'),
            ('MobileNotes', 'MobileNotes.app', 'com.example.fake'),
            ('SpringBoard', 'SpringBoard.app', 'com.example.fake'),
            ('Weather', 'Weather.app', 'com.example.fake'),
            ('AppStore', 'AppStore.app', 'com.example.fake'),
            ('StagedCustom', 'StagedCustom.app', 'com.example.other'),
        ):
            _, r, s = make_neg_root(label)
            planted = plant_app(r, app_name, bid)
            icon_p = os.path.join(r, 'private/var/mobile/Library/SpringBoard/IconState.plist')
            sha_before = file_sha256(icon_p)
            write_remove_txt(s, f'{app_name}\n{bid}\n'.encode('ascii'))
            require(refused(remove, r, s, tweaks), f'protected case {label} accepted')
            require(os.path.isdir(planted) and file_sha256(icon_p) == sha_before,
                    f'protected case {label} mutated state')

        # 3a-extra. 動態 rootfs 系統目錄覆蓋測試（AppPlaceholders.fuxi-staged 與 staged_system_apps）
        for label, rel_dir, sys_name, sys_bid in (
            ('parked-name', 'System/Library/AppPlaceholders.fuxi-staged', 'ParkedSys.app', 'com.example.parkedsys'),
            ('staged-name', 'private/var/staged_system_apps', 'HiddenSys.app', 'com.example.hiddensys'),
        ):
            _, r, s = make_neg_root(label)
            os.makedirs(os.path.join(r, rel_dir, sys_name), exist_ok=True)
            with open(os.path.join(r, rel_dir, sys_name, 'Info.plist'), 'wb') as stream:
                plistlib.dump({'CFBundleIdentifier': sys_bid}, stream)
            # 名稱相同但 bundle_id 偽裝為其他值
            planted1 = plant_app(r, sys_name, 'com.example.other')
            icon_p = os.path.join(r, 'private/var/mobile/Library/SpringBoard/IconState.plist')
            sha_before = file_sha256(icon_p)
            write_remove_txt(s, f'{sys_name}\ncom.example.other\n'.encode('ascii'))
            require(refused(remove, r, s, tweaks), f'{label} name match accepted')
            require(os.path.isdir(planted1) and file_sha256(icon_p) == sha_before, f'{label} mutated state')
            # 名稱不同但 bundle_id 撞到系統 placeholder/staged app
            planted2 = plant_app(r, 'AliasApp.app', sys_bid)
            write_remove_txt(s, f'AliasApp.app\n{sys_bid}\n'.encode('ascii'))
            require(refused(remove, r, s, tweaks), f'{label} bundle_id match accepted')
            require(os.path.isdir(planted2) and file_sha256(icon_p) == sha_before, f'{label} bundle_id mutated state')

        # 3a-internal-symlink. 第三方 App 內部含指向外部目錄的 symlink 時，rmtree 僅刪連結不跟隨刪除外部目標
        b, r, s = make_neg_root('internal-symlink')
        ext_keep = os.path.join(b, 'external_keep_dir')
        os.makedirs(ext_keep)
        with open(os.path.join(ext_keep, 'precious.txt'), 'wb') as stream:
            stream.write(b'precious')
        planted = plant_app(r, 'HasInnerLink.app', 'com.example.innerlink')
        os.symlink(ext_keep, os.path.join(planted, 'InnerLink'))
        write_remove_txt(s, b'HasInnerLink.app\ncom.example.innerlink\n')
        remove(r, s, tweaks)
        require(not os.path.lexists(planted), 'HasInnerLink.app not removed')
        require(os.path.isfile(os.path.join(ext_keep, 'precious.txt')), 'internal symlink target was deleted')

        # 3b. com.apple.* 前綴（傳入或實際 Info.plist）
        for label, txt_id, plist_id in (
            ('apple-arg', 'com.apple.Custom', 'com.apple.Custom'),
            ('apple-arg-upper', 'COM.APPLE.Custom', 'COM.APPLE.Custom'),
            ('apple-plist', 'com.evil.spoof', 'com.apple.Custom'),
        ):
            _, r, s = make_neg_root(label)
            planted = plant_app(r, 'CustomApple.app', plist_id)
            icon_p = os.path.join(r, 'private/var/mobile/Library/SpringBoard/IconState.plist')
            sha_before = file_sha256(icon_p)
            write_remove_txt(s, f'CustomApple.app\n{txt_id}\n'.encode('ascii'))
            require(refused(remove, r, s, tweaks), f'{label} accepted')
            require(os.path.isdir(planted) and file_sha256(icon_p) == sha_before, f'{label} mutated state')

        # 3c. Info.plist CFBundleIdentifier 不符 & 缺 Info.plist
        _, r, s = make_neg_root('mismatch')
        planted = plant_app(r, 'ThirdParty.app', 'com.other.app')
        icon_p = os.path.join(r, 'private/var/mobile/Library/SpringBoard/IconState.plist')
        sha_before = file_sha256(icon_p)
        write_remove_txt(s, b'ThirdParty.app\ncom.expected.app\n')
        require(refused(remove, r, s, tweaks), 'mismatch accepted')
        require(os.path.isdir(planted) and file_sha256(icon_p) == sha_before, 'mismatch mutated state')

        _, r, s = make_neg_root('missing-info')
        planted = os.path.join(r, 'Applications/NoInfo.app')
        os.makedirs(planted)
        icon_p = os.path.join(r, 'private/var/mobile/Library/SpringBoard/IconState.plist')
        sha_before = file_sha256(icon_p)
        write_remove_txt(s, b'NoInfo.app\ncom.example.noinfo\n')
        require(refused(remove, r, s, tweaks), 'missing Info.plist accepted')
        require(os.path.isdir(planted) and file_sha256(icon_p) == sha_before, 'missing Info.plist mutated state')

        # 3d. 語法與壞 remove-app.txt（路徑穿越、前導點、萬用字元、少行/多行/CRLF、壞 bundle_id）
        for label, raw in (
            ('traversal', b'../Applications/Old.app\ncom.example.old\n'),
            ('leading-dot', b'.fuxi-partial\ncom.example.old\n'),
            ('wildcard-star', b'*.app\ncom.example.old\n'),
            ('wildcard-bare', b'*\ncom.example.old\n'),
            ('bad-bid-underscore', b'Old.app\ncom_example_old\n'),
            ('one-line', b'Old.app\n'),
            ('three-lines', b'Old.app\ncom.example.old\nextra\n'),
            ('crlf', b'Old.app\r\ncom.example.old\r\n'),
            ('no-trailing-lf', b'Old.app\ncom.example.old'),
        ):
            _, r, s = make_neg_root('fmt-' + label)
            icon_p = os.path.join(r, 'private/var/mobile/Library/SpringBoard/IconState.plist')
            sha_before = file_sha256(icon_p)
            write_remove_txt(s, raw)
            require(refused(remove, r, s, tweaks), f'bad format {label} accepted')
            require(os.path.isdir(os.path.join(r, 'Applications/Old.app')) and file_sha256(icon_p) == sha_before,
                    f'bad format {label} mutated state')

        # 3e. symlink .app 目標（驗證拒絕且 symlink 指向的真實目錄完好無損）
        b, r, s = make_neg_root('symlink-app')
        victim_dir = os.path.join(b, 'victim_system_dir')
        os.makedirs(victim_dir)
        with open(os.path.join(victim_dir, 'Info.plist'), 'wb') as stream:
            plistlib.dump({'CFBundleIdentifier': 'com.example.evil'}, stream)
        with open(os.path.join(victim_dir, 'keep.txt'), 'wb') as stream:
            stream.write(b'keep')
        os.symlink(victim_dir, os.path.join(r, 'Applications/Evil.app'))
        icon_p = os.path.join(r, 'private/var/mobile/Library/SpringBoard/IconState.plist')
        sha_before = file_sha256(icon_p)
        write_remove_txt(s, b'Evil.app\ncom.example.evil\n')
        require(refused(remove, r, s, tweaks), 'symlink .app accepted')
        require(os.path.isfile(os.path.join(victim_dir, 'keep.txt')) and file_sha256(icon_p) == sha_before,
                'symlink target was touched')

        # 3f. symlink Info.plist
        b, r, s = make_neg_root('symlink-info')
        app_dir = os.path.join(r, 'Applications/SymInfo.app')
        os.makedirs(app_dir)
        ext_plist = os.path.join(b, 'ext_Info.plist')
        with open(ext_plist, 'wb') as stream:
            plistlib.dump({'CFBundleIdentifier': 'com.example.syminfo'}, stream)
        os.symlink(ext_plist, os.path.join(app_dir, 'Info.plist'))
        icon_p = os.path.join(r, 'private/var/mobile/Library/SpringBoard/IconState.plist')
        sha_before = file_sha256(icon_p)
        write_remove_txt(s, b'SymInfo.app\ncom.example.syminfo\n')
        require(refused(remove, r, s, tweaks), 'symlink Info.plist accepted')
        require(os.path.isdir(app_dir) and file_sha256(icon_p) == sha_before, 'symlink Info.plist mutated state')

        # 3g. symlink launchd.plist
        b, r, s = make_neg_root('symlink-launchd')
        planted = plant_app(r, 'SymLaunchd.app', 'com.example.symlaunchd')
        launchd_real = os.path.join(b, 'real_launchd.plist')
        shutil.move(os.path.join(r, 'System/Library/xpc/launchd.plist'), launchd_real)
        os.symlink(launchd_real, os.path.join(r, 'System/Library/xpc/launchd.plist'))
        icon_p = os.path.join(r, 'private/var/mobile/Library/SpringBoard/IconState.plist')
        sha_before = file_sha256(icon_p)
        write_remove_txt(s, b'SymLaunchd.app\ncom.example.symlaunchd\n')
        require(refused(remove, r, s, tweaks), 'symlink launchd.plist accepted')
        require(os.path.isdir(planted) and file_sha256(icon_p) == sha_before, 'symlink launchd mutated state')

        # 3h. 目標不存在
        _, r, s = make_neg_root('not-found')
        icon_p = os.path.join(r, 'private/var/mobile/Library/SpringBoard/IconState.plist')
        sha_before = file_sha256(icon_p)
        write_remove_txt(s, b'MissingApp.app\ncom.example.missing\n')
        require(refused(remove, r, s, tweaks), 'missing target accepted')
        require(file_sha256(icon_p) == sha_before, 'missing target mutated IconState')

        # 3i. CHRONO_SNAPSHOTS 含一般檔案 .DS_Store（不崩潰、清理 cache、不跳過 clean_user_data 與 IconState 後檢）
        _, r, s = make_neg_root('chrono-regular-file')
        planted = plant_app(r, 'ChronoFileApp.app', 'com.example.chronofile')
        chrono_path = os.path.join(r, tweaks.CHRONO_SNAPSHOTS)
        with open(os.path.join(chrono_path, '.DS_Store'), 'wb') as stream:
            stream.write(b'Bud1')
        with open(os.path.join(r, 'private/var/mobile/Library/Preferences/com.example.chronofile.plist'), 'wb') as stream:
            plistlib.dump({'k': 1}, stream)
        os.makedirs(os.path.join(r, 'private/var/mobile/Library/Caches/com.example.chronofile'), exist_ok=True)
        os.makedirs(os.path.join(r, 'private/var/mobile/Library/Saved Application State/com.example.chronofile.savedState'), exist_ok=True)
        icon_p = os.path.join(r, 'private/var/mobile/Library/SpringBoard/IconState.plist')
        sha_before = file_sha256(icon_p)
        write_remove_txt(s, b'ChronoFileApp.app\ncom.example.chronofile\n')
        remove(r, s, tweaks)
        require(not os.path.lexists(planted), 'ChronoFileApp.app not removed')
        require(os.listdir(chrono_path) == [], 'chrono cache still contains entries')
        require(not os.path.lexists(os.path.join(r, 'private/var/mobile/Library/Preferences/com.example.chronofile.plist')),
                'clean_user_data skipped pref after chrono file')
        require(not os.path.lexists(os.path.join(r, 'private/var/mobile/Library/Caches/com.example.chronofile')),
                'clean_user_data skipped cache after chrono file')
        require(not os.path.lexists(os.path.join(r, 'private/var/mobile/Library/Saved Application State/com.example.chronofile.savedState')),
                'clean_user_data skipped savedState after chrono file')
        require(file_sha256(icon_p) == sha_before, 'IconState changed in chrono regular file test')

        # 3j. CHRONO_SNAPSHOTS 含子項 symlink（於刪除 App 前拒絕且 App 與外部目標完好）
        b, r, s = make_neg_root('chrono-child-symlink')
        planted = plant_app(r, 'ChronoSymApp.app', 'com.example.chronosym')
        ext_snap = os.path.join(b, 'ext_snap_target')
        os.makedirs(ext_snap)
        with open(os.path.join(ext_snap, 'keep.txt'), 'wb') as stream:
            stream.write(b'keep')
        os.symlink(ext_snap, os.path.join(r, tweaks.CHRONO_SNAPSHOTS, 'snap_symlink'))
        icon_p = os.path.join(r, 'private/var/mobile/Library/SpringBoard/IconState.plist')
        sha_before = file_sha256(icon_p)
        write_remove_txt(s, b'ChronoSymApp.app\ncom.example.chronosym\n')
        require(refused(remove, r, s, tweaks), 'chrono child symlink accepted')
        require(os.path.isdir(planted) and os.path.isfile(os.path.join(ext_snap, 'keep.txt')) and
                file_sha256(icon_p) == sha_before, 'chrono child symlink mutated state')

        # 3k. 殘留 .Foo.app.fuxi-partial（於刪除 App 前拒絕且 Foo.app 完好）
        _, r, s = make_neg_root('leftover-partial')
        planted = plant_app(r, 'Foo.app', 'com.example.foo')
        os.makedirs(os.path.join(r, 'Applications/.Foo.app.fuxi-partial'))
        icon_p = os.path.join(r, 'private/var/mobile/Library/SpringBoard/IconState.plist')
        sha_before = file_sha256(icon_p)
        write_remove_txt(s, b'Foo.app\ncom.example.foo\n')
        require(refused(remove, r, s, tweaks), 'leftover .fuxi-partial accepted')
        require(os.path.isdir(planted) and file_sha256(icon_p) == sha_before,
                'leftover .fuxi-partial deleted Foo.app before failure')

        # 3l. LS_DATABASES 父目錄 symlink（於刪除 App 前拒絕且外部 .csstore 完好）
        b, r, s = make_neg_root('lsdb-parent-symlink')
        planted = plant_app(r, 'LsSymApp.app', 'com.example.lssym')
        ext_caches = os.path.join(b, 'ext_ls_caches')
        os.makedirs(ext_caches)
        ext_cs = os.path.join(ext_caches, 'com.apple.LaunchServices-999.csstore')
        with open(ext_cs, 'wb') as stream:
            stream.write(b'keep-csstore')
        daemon_lib = os.path.join(r, 'private/var/mobile/Containers/Data/InternalDaemon/X/Library')
        shutil.rmtree(os.path.join(daemon_lib, 'Caches'))
        os.symlink(ext_caches, os.path.join(daemon_lib, 'Caches'))
        icon_p = os.path.join(r, 'private/var/mobile/Library/SpringBoard/IconState.plist')
        sha_before = file_sha256(icon_p)
        write_remove_txt(s, b'LsSymApp.app\ncom.example.lssym\n')
        require(refused(remove, r, s, tweaks), 'LS_DATABASES parent symlink accepted')
        require(os.path.isdir(planted) and os.path.isfile(ext_cs) and file_sha256(icon_p) == sha_before,
                'LS_DATABASES parent symlink deleted app or external csstore')

        # 3m. System/Library/xpc 目錄為 symlink（於刪除 App 前拒絕且外部 launchd.plist 完好）
        b, r, s = make_neg_root('xpc-dir-symlink')
        planted = plant_app(r, 'XpcSymApp.app', 'com.example.xpcsym')
        ext_xpc = os.path.join(b, 'ext_xpc')
        shutil.move(os.path.join(r, 'System/Library/xpc'), ext_xpc)
        os.symlink(ext_xpc, os.path.join(r, 'System/Library/xpc'))
        ext_launchd_bytes = open(os.path.join(ext_xpc, 'launchd.plist'), 'rb').read()
        icon_p = os.path.join(r, 'private/var/mobile/Library/SpringBoard/IconState.plist')
        sha_before = file_sha256(icon_p)
        write_remove_txt(s, b'XpcSymApp.app\ncom.example.xpcsym\n')
        require(refused(remove, r, s, tweaks), 'System/Library/xpc symlink accepted')
        require(os.path.isdir(planted) and open(os.path.join(ext_xpc, 'launchd.plist'), 'rb').read() == ext_launchd_bytes and
                file_sha256(icon_p) == sha_before, 'System/Library/xpc symlink mutated state')

        # 3n. 非 list .fuxi-installed-staged-apps.plist 拒絕（fail-closed）
        _, r, s = make_neg_root('nondict-staged-manifest')
        planted = plant_app(r, 'ThirdPartyManifest.app', 'com.example.tpmanifest')
        staged_manifest = os.path.join(
            r, 'System/Library/AppPlaceholders.fuxi-staged/.fuxi-installed-staged-apps.plist')
        with open(staged_manifest, 'wb') as stream:
            plistlib.dump({'unexpected': 1}, stream, fmt=plistlib.FMT_XML)
        icon_p = os.path.join(r, 'private/var/mobile/Library/SpringBoard/IconState.plist')
        sha_before = file_sha256(icon_p)
        write_remove_txt(s, b'ThirdPartyManifest.app\ncom.example.tpmanifest\n')
        require(refused(remove, r, s, tweaks), 'non-list staged apps manifest accepted')
        require(os.path.isdir(planted) and file_sha256(icon_p) == sha_before,
                'non-list staged apps manifest mutated state')

        # 3o. 預置 System/Library/xpc/launchd.plist.fuxitmp symlink（於刪除 App 前拒絕且目標 .app 與外部目標完好）
        b, r, s = make_neg_root('launchd-fuxitmp-symlink')
        planted = plant_app(r, 'TmpLinkApp.app', 'com.example.tmplink')
        ext_tmp_target = os.path.join(b, 'ext_launchd_tmp.plist')
        with open(ext_tmp_target, 'wb') as stream:
            stream.write(b'keep-ext-tmp')
        os.symlink(ext_tmp_target, os.path.join(r, 'System/Library/xpc/launchd.plist.fuxitmp'))
        icon_p = os.path.join(r, 'private/var/mobile/Library/SpringBoard/IconState.plist')
        sha_before = file_sha256(icon_p)
        write_remove_txt(s, b'TmpLinkApp.app\ncom.example.tmplink\n')
        require(refused(remove, r, s, tweaks), 'launchd.plist.fuxitmp symlink accepted')
        require(os.path.isdir(planted) and open(ext_tmp_target, 'rb').read() == b'keep-ext-tmp' and
                file_sha256(icon_p) == sha_before, 'launchd.plist.fuxitmp symlink mutated state')

    print('PASS remove-app guest: slot7der cycle, appex cache cleanup, 7-layer protection & refusals, IconState unchanged')


if __name__ == '__main__':
    if sys.argv[1:2] == ['remove'] and len(sys.argv) == 4:
        remove(*sys.argv[2:4])
    elif sys.argv[1:2] == ['self-test'] and len(sys.argv) == 3:
        self_test(sys.argv[2])
    else:
        raise SystemExit(__doc__)
