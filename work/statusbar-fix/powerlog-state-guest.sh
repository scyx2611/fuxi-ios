#!/bin/sh
set -eu
trap 'echo POWERLOG_STATE_FAILED' EXIT
modprobe hfsplus
blockdev --setro /dev/vda
[ "$(cat /sys/block/vda/ro)" = 1 ]
[ "$(cat /sys/block/vda/size)" = 41943040 ]
mkdir -p /dst
mount -t hfsplus -o ro /dev/vda /dst >/tmp/powerlog-mount.log 2>&1
python3 - <<'PY'
import base64
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import stat
import sys

ROOT = Path('/dst')
MAX_JSON = 65536
MAX_ENTRIES = 128
MAX_SCAN = 4096
MAX_PLIST = 1024 * 1024
MAX_LARGE_PLIST = 16 * 1024 * 1024
PLIST_KEYS = ('Label', 'Program', 'ProgramArguments', 'Disabled', 'RunAtLoad', 'KeepAlive')
APPSTORE_BASELINE_JOBS = (
    'com.apple.fairplayd.H2',
    'com.apple.fairplaydeviceidentityd',
    'com.apple.absd',
    'com.apple.adid',
    'com.apple.itunesstored',
    'com.apple.appstored',
    'com.apple.amsaccountsd',
    'com.apple.akd',
    'com.apple.accountsd',
)
APPSTORE_CRASH_HINT = re.compile(
    r'(AppStore|appstored|itunesstored|fairplay|adid|absd|amsaccountsd|akd|accountsd)',
    re.IGNORECASE,
)


def target(value):
    return isinstance(value, str) and (value.lower() in ('com.apple.timed', 'com.apple.powerd')
        or 'powerlog' in value.lower() or 'perfpowerservices' in value.lower())


def checked(path):
    current = ROOT
    information = current.lstat()
    if not stat.S_ISDIR(information.st_mode):
        raise ValueError('invalid_root')
    for component in Path(path).parts[1:]:
        if component in ('.', '..'):
            raise ValueError('invalid_path')
        current = current / component
        try:
            information = current.lstat()
        except FileNotFoundError:
            return current, None
        if stat.S_ISLNK(information.st_mode):
            raise ValueError('symlink_not_followed')
    return current, information


def metadata(path):
    record = {'path': path}
    try:
        physical, information = checked(path)
        record['exists'] = information is not None
        if information is not None:
            record.update(size=information.st_size, mtime_ns=information.st_mtime_ns,
                          kind='directory' if stat.S_ISDIR(information.st_mode) else
                          'file' if stat.S_ISREG(information.st_mode) else 'other')
    except (OSError, ValueError):
        record.update(exists=None, error='path_unavailable_or_symlink')
    return record


def timezone_link(path):
    record = {'path': path}
    try:
        parent, information = checked(str(Path(path).parent))
        if information is None:
            record['exists'] = False
            return record
        physical = parent / Path(path).name
        information = physical.lstat()
        record['exists'] = True
        record['kind'] = 'symlink' if stat.S_ISLNK(information.st_mode) else 'other'
        if stat.S_ISLNK(information.st_mode):
            destination = os.readlink(physical)
            if len(destination) > 512:
                raise ValueError('timezone_target_limit')
            record['target'] = destination
    except FileNotFoundError:
        record['exists'] = False
    except (OSError, ValueError):
        record.update(exists=None, error='timezone_link_unavailable')
    return record


def read_plist_bounded(path, max_bytes=MAX_PLIST):
    physical, information = checked(path)
    if information is None:
        return None
    if not stat.S_ISREG(information.st_mode) or not 0 < information.st_size <= max_bytes:
        raise ValueError('plist_size_or_type')
    with physical.open('rb') as stream:
        contents = stream.read(max_bytes + 1)
    if len(contents) != information.st_size:
        raise ValueError('plist_read_size')
    value = plistlib.loads(contents)
    if not isinstance(value, dict):
        raise ValueError('plist_not_dictionary')
    return value


def read_plist(path):
    return read_plist_bounded(path, MAX_PLIST)


def selected(value, depth=0):
    if type(value) in (bool, int) or value is None:
        return value
    if isinstance(value, str) and len(value) <= 256:
        return value
    if depth < 2 and isinstance(value, list) and len(value) <= 24:
        return [selected(item, depth + 1) for item in value]
    if depth < 2 and isinstance(value, dict) and len(value) <= 16:
        return {key: selected(item, depth + 1) for key, item in value.items()
                if isinstance(key, str) and len(key) <= 128}
    return {'unknown': 'unsupported_or_over_limit'}


def names(path):
    physical, information = checked(path)
    if information is None:
        return []
    if not stat.S_ISDIR(information.st_mode):
        raise ValueError('not_directory')
    result = []
    with os.scandir(physical) as entries:
        for entry in entries:
            if len(result) == MAX_SCAN:
                raise ValueError('directory_scan_limit')
            if len(entry.name) > 256:
                raise ValueError('filename_limit')
            result.append(entry.name)
    return sorted(result)


def wifi_summary(value):
    fields = ('SSID_STR', 'SSID', 'AutoJoin', 'AutoLogin', 'Disabled', 'HIDDEN_NETWORK',
              'Hidden', 'Secure', 'SecurityMode', 'IsPersonalHotspot', 'JoinedByUser',
              'WasCaptive', 'Captive', 'AP_MODE')
    profiles = []
    visited = 0

    def settings(item, depth=0):
        if depth > 4 or len(item) > 128:
            raise ValueError('wifi_settings_limit')
        result = {}
        for key, entry in item.items():
            if any(word in key.lower() for word in ('pass', 'secret', 'token', 'credential', 'key')):
                continue
            if type(entry) in (bool, int, float):
                result[key] = entry
            elif isinstance(entry, datetime):
                result[key] = entry.isoformat()
            elif key in ('SSID_STR', 'BSSID', 'SecurityMode', 'JoinMode', 'JoinRecommendationMode') and (
                    isinstance(entry, str) and len(entry) <= 128):
                result[key] = entry
            elif isinstance(entry, dict):
                nested = settings(entry, depth + 1)
                if nested:
                    result[key] = nested
        return result

    def field_types(item):
        if len(item) > 128 or any(not isinstance(key, str) or len(key) > 160 for key in item):
            raise ValueError('wifi_field_limit')
        return {key: type(entry).__name__ for key, entry in item.items()}

    def visit(item, depth, context):
        nonlocal visited
        visited += 1
        if visited > 4096 or depth > 8:
            raise ValueError('wifi_profile_scan_limit')
        if isinstance(item, dict):
            if any(key in item for key in ('SSID_STR', 'SSID')):
                if len(profiles) >= 32:
                    raise ValueError('wifi_profile_count_limit')
                profile = {'context': context, 'field_types': field_types(item), 'settings': settings(item)}
                for key in fields:
                    entry = item.get(key)
                    if type(entry) in (bool, int):
                        profile[key] = entry
                    elif isinstance(entry, str) and len(entry) <= 128:
                        profile[key] = entry
                    elif key == 'SSID' and isinstance(entry, bytes) and len(entry) <= 32:
                        profile[key] = entry.decode('utf-8', 'replace')
                profiles.append(profile)
            for key, entry in item.items():
                if isinstance(entry, (dict, list)):
                    visit(entry, depth + 1, context + '/' + key[:80])
        elif isinstance(item, list):
            for index, entry in enumerate(item):
                if isinstance(entry, (dict, list)):
                    visit(entry, depth + 1, context + '/' + str(index))

    visit(value, 0, '')
    return {'flags': {key: entry for key, entry in value.items()
                      if isinstance(key, str) and len(key) <= 80 and type(entry) is bool},
            'field_types': field_types(value), 'settings': settings(value), 'profiles': profiles}


def wifi_log_summary(details=False, focus=None):
    configuration_path = '/private/var/preferences/SystemConfiguration/com.apple.wifi.plist'
    log_directory = '/private/var/logs'
    candidates = [metadata(log_directory + '/wifimanager.log')]
    if metadata(log_directory).get('kind') == 'directory':
        rotations = [name for name in names(log_directory)
                     if re.fullmatch(r'wifimanager\.log\.[0-9]{1,3}', name)]
        if len(rotations) > 8:
            raise ValueError('wifi_log_rotation_limit')
        candidates.extend(metadata(log_directory + '/' + name) for name in rotations)
    available = [record for record in candidates if record.get('kind') == 'file']
    result = {'configuration': metadata(configuration_path), 'records': [],
              'path_candidates': candidates,
              'links': [timezone_link('/Library'), timezone_link('/Library/Logs')],
              'log': max(available, key=lambda record: record['mtime_ns']) if available else candidates[0]}
    try:
        configuration = read_plist(configuration_path)
        enabled = configuration.get('LoggingFileEnabled')
        result['configuration']['logging_enabled'] = enabled if type(enabled) is bool else None
        result['configuration_summary'] = wifi_summary(configuration)
    except (OSError, ValueError, AttributeError):
        result['configuration']['error'] = 'configuration_unavailable'
    if result['log'].get('kind') != 'file':
        return result
    physical, information = checked(result['log']['path'])
    start = max(0, information.st_size - 2 * 1024 * 1024)
    with physical.open('rb') as stream:
        stream.seek(start)
        content = stream.read(2 * 1024 * 1024)
    result['log']['tail_bytes'] = len(content)
    result['log']['tail_start'] = start
    selected_pattern = re.compile(r'auto.?join|skipped candidate|joinmode|known network|blacklist', re.I)
    if details:
        selected_pattern = re.compile(
            r'scan.*(result|network)|suitability|filter|candidate|unified|bssid|ssid|WiFiBatteryMgmt|CPMS|power budget', re.I)
    if focus is not None:
        if not 1 <= len(focus) <= 256:
            raise ValueError('wifi_log_focus_limit')
        selected_pattern = re.compile(focus, re.I)
    private_pattern = re.compile(
        r'pass|secret|credential|keychain|\b(key|pmk|psk|ptk|gtk|token)\b|CFData|\bbytes\s*=', re.I)
    result['selection'] = {'focus': focus, 'matched': 0, 'redacted': 0, 'omitted': 0}
    result['redacted_sources'] = {}
    result['credential_decisions'] = {
        'password_missing': 0, 'missing_password_disabled': 0, 'keychain_lookup_errors': {},
        'keychain_save_errors': {}, 'save_failures': 0, 'removed_after_failed_association': 0}
    credential_prefix = r'^\d{2}/\d{2}/\d{4} \d{2}:\d{2}:\d{2}\.\d{3} '
    missing_password = re.compile(
        credential_prefix + r'__WiFiDeviceManagerKnownNetworkSuitabilityCheck: '
        r'No password found for .{1,160}, return as unsuitable\s*$')
    disabled_password = re.compile(
        credential_prefix + r'__WiFiDeviceManagerKnownNetworkSuitabilityCheck: '
        r'disabled for missing password Network .{1,160} isFilteringAJCandidates [01]\s*$')
    lookup_error = re.compile(
        credential_prefix + r'WiFiSecurityCopy(NonSyncable)?PasswordWithTimeout_block_invoke:'
        r'\[.{1,160}\] Error result (-?[0-9]{1,10})\s*$')
    save_error = re.compile(
        credential_prefix + r'__WiFiSecuritySetPassword:\[.{1,160}\] Error result (-?[0-9]{1,10})\s*$')
    save_failure = re.compile(credential_prefix + r'Error saving (?:new )?password in keychain\s*$')
    removed_password = re.compile(
        credential_prefix + r'[A-Za-z_][A-Za-z0-9_]{0,95}: removing password for .{1,160}'
        r' due to failed association\s*$')
    position = start
    for index, raw in enumerate(content.splitlines(keepends=True)):
        offset = position
        position += len(raw)
        if (start and index == 0) or len(raw) > 4096:
            continue
        text = raw.decode('utf-8', 'replace').strip('\r\n')
        if missing_password.fullmatch(text):
            result['credential_decisions']['password_missing'] += 1
        if disabled_password.fullmatch(text):
            result['credential_decisions']['missing_password_disabled'] += 1
        error = lookup_error.fullmatch(text)
        if error and -(2 ** 31) <= int(error[2]) < 2 ** 31:
            operation = 'nonsyncable' if error[1] else 'standard'
            status_key = operation + ':' + str(int(error[2]))
            errors = result['credential_decisions']['keychain_lookup_errors']
            if status_key not in errors and len(errors) >= 16:
                status_key = 'other'
            errors[status_key] = errors.get(status_key, 0) + 1
        error = save_error.fullmatch(text)
        if error and -(2 ** 31) <= int(error[1]) < 2 ** 31:
            status_key = str(int(error[1]))
            errors = result['credential_decisions']['keychain_save_errors']
            if status_key not in errors and len(errors) >= 16:
                status_key = 'other'
            errors[status_key] = errors.get(status_key, 0) + 1
        if save_failure.fullmatch(text):
            result['credential_decisions']['save_failures'] += 1
        if removed_password.fullmatch(text):
            result['credential_decisions']['removed_after_failed_association'] += 1
        if selected_pattern.search(text):
            result['selection']['matched'] += 1
            if private_pattern.search(text):
                result['selection']['redacted'] += 1
                source = re.match(
                    r'^\d{2}/\d{2}/\d{4} \d{2}:\d{2}:\d{2}\.\d{3} ([A-Za-z_][A-Za-z0-9_]{0,95}):', text)
                source_name = source[1] if source else 'unclassified'
                if source_name not in result['redacted_sources'] and len(result['redacted_sources']) >= 32:
                    source_name = 'other'
                result['redacted_sources'][source_name] = result['redacted_sources'].get(source_name, 0) + 1
                continue
            result['records'].append({'offset': offset, 'text': text[:480]})
            if len(result['records']) > 100:
                result['records'].pop(50)
                result['selection']['omitted'] += 1
    return result


CRASH_DIRECTORY = '/private/var/mobile/Library/Logs/CrashReporter'
# 只對這些名稱取內容摘要；其餘報告只列檔名、大小、時間
CRASH_DETAIL = re.compile(r'com\.apple\.WebKit\.|MobileSafari|backboardd|SpringBoard|JetsamEvent|ExcResource'
                          r'|^stacks|Preferences|wifid|mDNSResponder|Maps|MapKit')
MAX_CRASH_BYTES = 4 * 1024 * 1024
MAX_CRASH_DETAILS = 10
MAX_CRASH_ENTRIES = 64
MAX_CRASH_FRAMES = 12


def short(value, limit=200):
    if value is None or type(value) in (bool, int, float):
        return value
    return str(value)[:limit]


def crash_frames(body):
    images = body.get('usedImages') if isinstance(body.get('usedImages'), list) else []
    threads = body.get('threads') if isinstance(body.get('threads'), list) else []
    index = body.get('faultingThread')
    if type(index) is int and 0 <= index < len(threads):
        thread = threads[index]
    else:
        thread = next((item for item in threads if isinstance(item, dict) and item.get('triggered')), None)
    if not isinstance(thread, dict) or not isinstance(thread.get('frames'), list):
        return None
    frames = []
    for frame in thread['frames'][:MAX_CRASH_FRAMES]:
        if not isinstance(frame, dict):
            continue
        image = frame.get('imageIndex')
        name = (images[image].get('name') if type(image) is int and 0 <= image < len(images)
                and isinstance(images[image], dict) else None)
        offset = frame.get('symbolLocation') if type(frame.get('symbolLocation')) is int else (
            frame.get('imageOffset') if type(frame.get('imageOffset')) is int else None)
        frames.append({'image': short(name, 60), 'symbol': short(frame.get('symbol'), 120), 'offset': offset})
    return {'queue': short(thread.get('queue'), 80), 'name': short(thread.get('name'), 80), 'frames': frames}


def crash_detail(item):
    detail = {'path': item['path'], 'mtime_ns': item['mtime_ns']}
    try:
        physical, information = checked(item['path'])
        if information is None or not stat.S_ISREG(information.st_mode) or not 0 < information.st_size <= MAX_CRASH_BYTES:
            raise ValueError('crash_size_or_type')
        with physical.open('rb') as stream:
            text = stream.read(MAX_CRASH_BYTES + 1).decode('utf-8', 'replace')
        first, _, rest = text.partition('\n')
        header = json.loads(first)
        if not isinstance(header, dict):
            raise ValueError('crash_header')
        detail['header'] = {key: short(header[key], 80) for key in (
            'app_name', 'name', 'bug_type', 'timestamp', 'os_version') if key in header}
        try:
            body = json.loads(rest)
        except ValueError:
            detail['body'] = 'not_json'
            return detail
        if not isinstance(body, dict):
            detail['body'] = 'not_object'
            return detail
        if isinstance(body.get('exception'), dict):
            detail['exception'] = {key: short(body['exception'][key]) for key in (
                'type', 'signal', 'subtype', 'codes') if key in body['exception']}
        if isinstance(body.get('termination'), dict):
            detail['termination'] = {key: short(body['termination'][key], 300) for key in (
                'namespace', 'code', 'indicator', 'byProc', 'reasons', 'details') if key in body['termination']}
        if isinstance(body.get('asi'), dict):
            detail['asi'] = {short(key, 60): short(value, 300) for key, value in list(body['asi'].items())[:6]}
        if body.get('ktriageinfo'):
            detail['ktriageinfo'] = short(body['ktriageinfo'], 300)
        frames = crash_frames(body)
        if frames:
            detail['faulting_thread'] = frames
        if isinstance(body.get('processes'), list):
            detail['largest_process'] = short(body.get('largestProcess'), 80)
            detail['processes'] = [{
                'name': short(process.get('name'), 80), 'reason': short(process.get('reason'), 80),
                'rpages': process.get('rpages') if type(process.get('rpages')) is int else None,
                'lifetimeMax': process.get('lifetimeMax') if type(process.get('lifetimeMax')) is int else None,
            } for process in body['processes'] if isinstance(process, dict) and (
                process.get('reason') or 'WebKit' in str(process.get('name')) or 'Safari' in str(process.get('name')))][:24]
    except Exception:
        detail['error'] = 'crash_unreadable_or_over_limit'
    return detail


def crash_summary():
    result = {'directory': metadata(CRASH_DIRECTORY), 'entries': [], 'listing_complete': False, 'details': []}
    if result['directory'].get('kind') != 'directory':
        return result
    candidates = []
    for directory in (CRASH_DIRECTORY, CRASH_DIRECTORY + '/Retired'):
        if metadata(directory).get('kind') != 'directory':
            continue
        for name in names(directory):
            item = metadata(directory + '/' + name)
            if item.get('kind') == 'file':
                candidates.append(item)
    candidates.sort(key=lambda item: item.get('mtime_ns', 0), reverse=True)
    result['listing_complete'] = len(candidates) <= MAX_CRASH_ENTRIES
    result['entries'] = [{'path': item['path'], 'size': item['size'], 'mtime_ns': item['mtime_ns']}
                         for item in candidates[:MAX_CRASH_ENTRIES]]
    for item in candidates:
        if len(result['details']) == MAX_CRASH_DETAILS:
            break
        name = item['path'].rsplit('/', 1)[1]
        if name.endswith('.ips') and CRASH_DETAIL.search(name):
            result['details'].append(crash_detail(item))
    return result


def appstore_baseline_summary():
    result = {
        'xpc_launchd': {},
        'launchd_plists': [],
        'disabled_plists': [],
        'directories': [],
        'crash_reporter': {},
    }
    xpc_path = '/System/Library/xpc/launchd.plist'
    xpc_record = metadata(xpc_path)
    xpc_record['jobs'] = {}
    if xpc_record.get('exists'):
        try:
            xpc_plist = read_plist_bounded(xpc_path, MAX_LARGE_PLIST)
            daemons = xpc_plist.get('LaunchDaemons') if isinstance(xpc_plist.get('LaunchDaemons'), dict) else xpc_plist
            xpc_record['launch_daemons_count'] = len(daemons) if isinstance(daemons, dict) else None
            if isinstance(daemons, dict):
                for key, entry in daemons.items():
                    if not isinstance(key, str) or not isinstance(entry, dict):
                        continue
                    label = entry.get('Label') if isinstance(entry.get('Label'), str) else None
                    matched = [job for job in APPSTORE_BASELINE_JOBS if job == label or job in key]
                    if matched:
                        job_name = matched[0]
                        xpc_record['jobs'][job_name] = {
                            'key': key[:160],
                            'configuration': {
                                field: selected(entry[field]) if field in entry else {'unknown': 'missing'}
                                for field in PLIST_KEYS
                            },
                        }
        except Exception:
            xpc_record['error'] = 'xpc_launchd_unreadable_or_over_limit'
    result['xpc_launchd'] = xpc_record

    for job in APPSTORE_BASELINE_JOBS:
        plist_path = '/System/Library/LaunchDaemons/' + job + '.plist'
        item = metadata(plist_path)
        if item.get('exists'):
            try:
                value = read_plist(plist_path)
                item['configuration'] = {
                    field: selected(value[field]) if field in value else {'unknown': 'missing'}
                    for field in PLIST_KEYS
                }
            except Exception:
                item['error'] = 'plist_unreadable_or_over_limit'
        result['launchd_plists'].append(item)

    for path in (
        '/private/var/db/com.apple.xpc.launchd/disabled.plist',
        '/private/var/db/com.apple.xpc.launchd/disabled.501.plist',
        '/private/var/db/launchd.db/com.apple.launchd/overrides.plist',
        '/private/var/db/launchd.db/com.apple.launchd.peruser.501/overrides.plist',
    ):
        record = metadata(path)
        if record.get('exists'):
            try:
                value = read_plist(path)
                matched_jobs = {}
                other_count = 0
                for key, entry in value.items():
                    if isinstance(key, str) and key in APPSTORE_BASELINE_JOBS:
                        matched_jobs[key] = selected(entry)
                    else:
                        other_count += 1
                record['jobs'] = matched_jobs
                record['other_entries_count'] = other_count
            except Exception:
                record['error'] = 'plist_unreadable_or_over_limit'
        result['disabled_plists'].append(record)

    for directory in (
        '/private/var/mobile/Library/FairPlay',
        '/private/var/mobile/Library/Accounts',
        '/private/var/db/timed',
    ):
        dir_record = metadata(directory)
        if dir_record.get('kind') == 'directory':
            try:
                all_names = names(directory)
                dir_record['total_entries'] = len(all_names)
                dir_record['entries'] = [metadata(directory + '/' + name) for name in all_names[:64]]
                dir_record['listing_truncated'] = len(all_names) > 64
            except Exception:
                dir_record['error'] = 'directory_unreadable_or_over_limit'
        result['directories'].append(dir_record)

    crash_summary_record = {
        'directory': metadata(CRASH_DIRECTORY),
        'entries': [],
        'listing_complete': False,
        'contents_read': False,
    }
    if crash_summary_record['directory'].get('kind') == 'directory':
        candidates = []
        for directory in (CRASH_DIRECTORY, CRASH_DIRECTORY + '/Retired'):
            if metadata(directory).get('kind') != 'directory':
                continue
            for name in names(directory):
                item = metadata(directory + '/' + name)
                if item.get('kind') == 'file':
                    item['appstore_related'] = bool(APPSTORE_CRASH_HINT.search(name))
                    candidates.append(item)
        candidates.sort(key=lambda item: item.get('mtime_ns', 0), reverse=True)
        crash_summary_record['total_files'] = len(candidates)
        crash_summary_record['listing_complete'] = len(candidates) <= 20
        crash_summary_record['entries'] = [
            {
                'path': item['path'],
                'size': item['size'],
                'mtime_ns': item['mtime_ns'],
                'appstore_related': item['appstore_related'],
            }
            for item in candidates[:20]
        ]
    result['crash_reporter'] = crash_summary_record
    return result


def collect():
    mounts = [line.split() for line in Path('/proc/mounts').read_text().splitlines()]
    matches = [entry for entry in mounts if entry[1] == str(ROOT)]
    if len(matches) != 1 or matches[0][0] != '/dev/vda' or matches[0][2] != 'hfsplus':
        raise ValueError('mount_identity')
    options = matches[0][3].split(',')
    if 'ro' not in options or 'rw' in options or Path('/sys/block/vda/ro').read_text().strip() != '1':
        raise ValueError('mount_not_readonly')
    result = {'schema': 1, 'hfs_readonly': True, 'block_readonly': True,
              'database_contents_read': False, 'root_writes': 0,
              'runtime_daemon_state': 'not_observed', 'clock_correctness': 'not_established',
              'scope': 'fixed system plist allowlist and database metadata; not exhaustive',
              'launchd': [], 'overrides': [], 'databases': [],
              'timezone_links': [timezone_link(path) for path in (
                  '/private/var/db/timezone/localtime',
                  '/private/var/db/timezone/zoneinfo',
                  '/private/etc/localtime')]}
    if Path('/mnt/share/inspect-crash').is_file():
        result['scope'] = 'fixed CrashReporter directory; listing plus selected report headers, exceptions and faulting frames'
        result['crash_reports'] = crash_summary()
        return result
    if Path('/mnt/share/inspect-wifi-log').is_file():
        result['scope'] = 'fixed native Wi-Fi log and logging flag; filtered decision messages only'
        focus_path = Path('/mnt/share/inspect-wifi-log-focus')
        focus = focus_path.read_text(encoding='utf-8') if focus_path.is_file() else None
        result['wifi_log'] = wifi_log_summary(Path('/mnt/share/inspect-wifi-log-details').is_file(), focus)
        return result
    if Path('/mnt/share/inspect-appstore-baseline').is_file():
        result['scope'] = 'fixed read-only AppStore/FairPlay baseline: xpc launchd, disabled plists, directory listings, CrashReporter metadata'
        result['appstore_baseline'] = appstore_baseline_summary()
        return result
    directory = '/System/Library/LaunchDaemons'
    directory_record = metadata(directory)
    result['launchd_directory'] = directory_record
    if directory_record.get('kind') == 'directory':
        candidates = [name for name in names(directory) if name.endswith('.plist') and target(name[:-6])]
        if len(candidates) > 32:
            raise ValueError('launchd_count_limit')
        for name in candidates:
            path = directory + '/' + name
            record = metadata(path)
            try:
                value = read_plist(path)
                record['configuration'] = {key: selected(value[key]) if key in value else
                                           {'unknown': 'missing'} for key in PLIST_KEYS}
            except Exception:
                record['configuration'] = {'unknown': 'plist_unreadable_or_over_limit'}
            result['launchd'].append(record)
    for path in ('/private/var/db/com.apple.xpc.launchd/disabled.plist',
                 '/private/var/db/com.apple.xpc.launchd/disabled.501.plist',
                 '/private/var/db/launchd.db/com.apple.launchd/overrides.plist',
                 '/private/var/db/launchd.db/com.apple.launchd.peruser.501/overrides.plist'):
        record = metadata(path)
        if record.get('exists'):
            try:
                value = read_plist(path)
                labels = [key for key in value if target(key)]
                if len(labels) > 32 or any(len(key) > 256 for key in labels):
                    raise ValueError('override_count_limit')
                record['target_overrides'] = {key: selected(value[key]) if type(value[key]) is bool else
                    {'Disabled': selected(value[key]['Disabled']) if 'Disabled' in value[key] else
                     {'unknown': 'missing'}} if isinstance(value[key], dict) else
                    {'unknown': 'unexpected_type'} for key in sorted(labels)}
                record['meaning'] = 'configuration_only; missing_target_is_not_runtime_enabled'
            except Exception:
                record['target_overrides'] = None
                record['error'] = 'plist_unreadable_or_over_limit'
        result['overrides'].append(record)
    for directory in ('/private/var/db/powerlog', '/private/var/db/Powerlog',
                      '/private/var/db/PerfPowerServices', '/private/var/mobile/Library/BatteryLife',
                      '/private/var/mobile/Library/Logs/Powerlog'):
        record = metadata(directory)
        record['entries'] = []
        record['listing_complete'] = False
        if record.get('kind') == 'directory':
            entries = names(directory)
            record['listing_complete'] = len(entries) <= MAX_ENTRIES
            for name in entries[:MAX_ENTRIES]:
                record['entries'].append(metadata(directory + '/' + name))
        result['databases'].append(record)
    if Path('/mnt/share/inspect-wifi').is_file():
        result['wifi'] = []
        for path in (
                '/private/var/preferences/SystemConfiguration/com.apple.wifi.plist',
                '/private/var/preferences/com.apple.wifi.known-networks.plist',
                '/private/var/preferences/com.apple.wifi.plist',
                '/private/var/mobile/Library/Preferences/com.apple.wifi.plist'):
            record = metadata(path)
            if record.get('exists'):
                try:
                    record['configuration'] = wifi_summary(read_plist(path))
                except Exception:
                    record['error'] = 'wifi_plist_unreadable_or_over_limit'
            result['wifi'].append(record)
        result['keychain_metadata'] = [metadata(path) for path in (
            '/private/var/Keychains/keychain-2.db',
            '/private/var/Keychains/keychain-2.db-wal',
            '/private/var/Keychains/keychain-2.db-shm')]
    return result


def emit(result):
    payload = json.dumps(result, ensure_ascii=True, separators=(',', ':'), allow_nan=False).encode('ascii')
    if len(payload) > MAX_JSON:
        raise ValueError('json_size_limit')
    print('POWERLOG_STATE_BEGIN bytes=' + str(len(payload)) + ' sha256=' + hashlib.sha256(payload).hexdigest())
    encoded = base64.b64encode(payload).decode('ascii')
    chunks = [encoded[offset:offset + 144] for offset in range(0, len(encoded), 144)]
    for index, chunk in enumerate(chunks):
        print('POWERLOG_STATE_CHUNK ' + str(index).zfill(4) + ' ' + chunk)
    print('POWERLOG_STATE_END chunks=' + str(len(chunks)), flush=True)


if __name__ == '__main__':
    try:
        emit(collect())
    except Exception:
        print('POWERLOG_STATE_FAILED phase=collect', flush=True)
        sys.exit(1)
PY
umount /dst
trap - EXIT
echo POWERLOG_STATE_SUCCESS
