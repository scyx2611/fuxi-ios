import os
import argparse
import base64
import hashlib
import importlib.util
import json
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile

DIRECTORY = Path(__file__).resolve().parent
MAX_JSON = 65536
MAX_LOG = 2 * 1024 * 1024
SUCCESS = 'POWERLOG_STATE_SUCCESS'


def decode(text):
    if len(text.encode('utf-8')) > MAX_LOG:
        raise ValueError('Guest log exceeds bound')
    lines = [line.strip('\r') for line in text.split('\n')]
    records = [line for line in lines if line.startswith('POWERLOG_STATE_')]
    if any(len(line.encode('ascii')) > 200 for line in records):
        raise ValueError('Protocol line exceeds bound')
    if len(records) < 4 or records[-1] != SUCCESS or lines.count('GUEST_OK') != 1 or 'GUEST_FAILED' in lines:
        raise ValueError('Guest did not complete')
    header = re.fullmatch(r'POWERLOG_STATE_BEGIN bytes=([0-9]{1,5}) sha256=([0-9a-f]{64})', records[0])
    ending = re.fullmatch(r'POWERLOG_STATE_END chunks=([0-9]{1,3})', records[-2])
    if header is None or ending is None or not 1 <= int(header[1]) <= MAX_JSON:
        raise ValueError('Invalid summary header or ending')
    chunks = records[1:-2]
    if len(chunks) != int(ending[1]) or not 1 <= len(chunks) <= 607:
        raise ValueError('Invalid chunk count')
    encoded = []
    for index, line in enumerate(chunks):
        match = re.fullmatch(r'POWERLOG_STATE_CHUNK ([0-9]{4}) ([A-Za-z0-9+/=]{1,144})', line)
        if match is None or int(match[1]) != index or (index < len(chunks) - 1 and len(match[2]) != 144):
            raise ValueError('Invalid or missing chunk')
        encoded.append(match[2])
    payload = base64.b64decode(''.join(encoded), validate=True)
    if len(payload) != int(header[1]) or hashlib.sha256(payload).hexdigest() != header[2]:
        raise ValueError('Summary length or checksum differs')
    result = json.loads(payload)
    if (not isinstance(result, dict) or result.get('schema') != 1
            or result.get('hfs_readonly') is not True or result.get('block_readonly') is not True
            or result.get('database_contents_read') is not False or type(result.get('root_writes')) is not int
            or result['root_writes'] != 0):
        raise ValueError('Read-only summary contract differs')
    return result, payload


def self_test():
    source = (DIRECTORY / 'powerlog-state-guest.sh').read_text(encoding='utf-8')
    embedded = source.split("python3 - <<'PY'\n", 1)[1].split('\nPY\n', 1)[0]
    namespace = {'__name__': 'self_test_guest'}
    exec(compile(embedded, 'powerlog-state-guest.sh:python', 'exec'), namespace)
    import contextlib
    import io
    result = {'schema': 1, 'hfs_readonly': True, 'block_readonly': True,
              'database_contents_read': False, 'root_writes': 0, 'padding': 'x' * 64000}

    def transfer(value):
        stream = io.StringIO()
        with contextlib.redirect_stdout(stream):
            namespace['emit'](value)
        return stream.getvalue() + SUCCESS + '\nGUEST_OK\n'

    text = transfer(result)
    assert decode(text)[0] == result
    assert decode(text.replace('\n', '\r\n'))[0] == result
    assert max(map(len, text.splitlines())) <= 200
    wifi = namespace['wifi_summary']({
        'AllowJoin': True,
        'KnownNetworks': {
            'wifi.network.example': {
                'SSID_STR': 'example',
                'AutoJoin': True,
                'SecurityMode': 'WPA2 Personal',
                'Password': 'private-test-value',
                'KeychainItem': b'private-test-key',
            },
        },
    })
    assert wifi['flags'] == {'AllowJoin': True}
    assert wifi['profiles'] == [{
        'context': '/KnownNetworks/wifi.network.example',
        'field_types': {'SSID_STR': 'str', 'AutoJoin': 'bool', 'SecurityMode': 'str',
                        'Password': 'str', 'KeychainItem': 'bytes'},
        'settings': {'SSID_STR': 'example', 'AutoJoin': True, 'SecurityMode': 'WPA2 Personal'},
        'SSID_STR': 'example', 'AutoJoin': True, 'SecurityMode': 'WPA2 Personal',
    }]
    assert 'private-test' not in json.dumps(wifi)
    assert namespace['wifi_summary']({'KnownNetworks': [{'SSID': b'example', 'AutoJoin': False}]})[
        'profiles'] == [{'context': '/KnownNetworks/0', 'field_types': {'SSID': 'bytes', 'AutoJoin': 'bool'},
                        'settings': {'AutoJoin': False},
                        'SSID': 'example', 'AutoJoin': False}]
    numeric_secret = namespace['wifi_summary']({
        'SSID_STR': 'example', '__OSSpecific__': {'AutoJoin': False, 'Passcode': 123456}})
    assert numeric_secret['profiles'][0]['settings']['__OSSpecific__'] == {'AutoJoin': False}
    assert '123456' not in json.dumps(numeric_secret)
    with tempfile.TemporaryDirectory() as directory:
        namespace['ROOT'] = Path(directory)
        configuration = namespace['ROOT'] / 'private/var/preferences/SystemConfiguration/com.apple.wifi.plist'
        configuration.parent.mkdir(parents=True)
        configuration.write_bytes(namespace['plistlib'].dumps({'LoggingFileEnabled': True}))
        native_log = namespace['ROOT'] / 'private/var/logs/wifimanager.log'
        native_log.parent.mkdir(parents=True)
        private_marker = 'synthetic-sensitive-marker'
        native_log.write_text('Autojoin is processing 1 candidate\n'
                              'Autojoin Password=' + private_marker + '\n'
                              'unrelated diagnostic\n'
                              'scan results count=2\n'
                              'WiFiBatteryMgmt power request=1\n', encoding='utf-8')
        log_summary = namespace['wifi_log_summary']()
        assert log_summary['configuration']['logging_enabled'] is True
        assert log_summary['configuration_summary']['settings']['LoggingFileEnabled'] is True
        assert log_summary['log']['path'] == '/private/var/logs/wifimanager.log'
        assert log_summary['records'] == [{'offset': 0, 'text': 'Autojoin is processing 1 candidate'}]
        assert private_marker not in json.dumps(log_summary)
        assert log_summary['selection'] == {'focus': None, 'matched': 2, 'redacted': 1, 'omitted': 0}
        assert [record['text'] for record in namespace['wifi_log_summary'](True)['records']] == [
            'Autojoin is processing 1 candidate', 'scan results count=2', 'WiFiBatteryMgmt power request=1']
        focused = namespace['wifi_log_summary'](focus=r'candidate|password|scan')
        assert [record['text'] for record in focused['records']] == [
            'Autojoin is processing 1 candidate', 'scan results count=2']
        assert focused['selection'] == {
            'focus': r'candidate|password|scan', 'matched': 3, 'redacted': 1, 'omitted': 0}
        assert private_marker not in json.dumps(focused)
        assert focused['redacted_sources'] == {'unclassified': 1}
        native_log.write_text(
            'device specific key used ' + private_marker + '\n'
            'rotation key used ' + private_marker + '\n'
            'diagnostic <CFData ' + private_marker + '>\n'
            'diagnostic bytes = ' + private_marker + '\n', encoding='utf-8')
        key_summary = namespace['wifi_log_summary'](focus='.')
        assert key_summary['records'] == []
        assert key_summary['selection']['redacted'] == 4
        assert private_marker not in json.dumps(key_summary)
        native_log.write_text('01/01/2026 12:34:56.789 ScanDecision: candidate credential='
                              + private_marker + '\n', encoding='utf-8')
        source_only = namespace['wifi_log_summary'](focus='candidate')
        assert source_only['records'] == []
        assert source_only['redacted_sources'] == {'ScanDecision': 1}
        assert private_marker not in json.dumps(source_only)
        diagnostic_prefix = '01/01/2026 12:34:56.789 '
        native_log.write_text(
            diagnostic_prefix + '__WiFiDeviceManagerKnownNetworkSuitabilityCheck: No password found for '
            + private_marker + ', return as unsuitable\n'
            + diagnostic_prefix + '__WiFiDeviceManagerKnownNetworkSuitabilityCheck: '
            'disabled for missing password Network ' + private_marker + ' isFilteringAJCandidates 1\n'
            + diagnostic_prefix + 'WiFiSecurityCopyNonSyncablePasswordWithTimeout_block_invoke:['
            + private_marker + '] Error result -25300 \n'
            + diagnostic_prefix + 'WiFiSecurityCopyPasswordWithTimeout_block_invoke:['
            + private_marker + '] Error result -25293 \n'
            + diagnostic_prefix + 'WiFiSecurityCopyPasswordWithTimeout_block_invoke:['
            + private_marker + '] Error result -9999999999\n'
            + diagnostic_prefix + '__WiFiSecuritySetPassword:[' + private_marker + '] Error result -25291\n'
            + diagnostic_prefix + '__WiFiSecuritySetPassword:[' + private_marker + '] Error result 9999999999\n'
            + diagnostic_prefix + 'Error saving password in keychain\n'
            + diagnostic_prefix + 'Error saving new password in keychain\n'
            + diagnostic_prefix + 'JoinDecision: removing password for ' + private_marker
            + ' due to failed association\n', encoding='utf-8')
        diagnostics = namespace['wifi_log_summary'](focus='password')
        assert diagnostics['records'] == []
        assert diagnostics['credential_decisions'] == {
            'password_missing': 1, 'missing_password_disabled': 1,
            'keychain_lookup_errors': {'nonsyncable:-25300': 1, 'standard:-25293': 1},
            'keychain_save_errors': {'-25291': 1}, 'save_failures': 2,
            'removed_after_failed_association': 1}
        assert private_marker not in json.dumps(diagnostics)
        for invalid_focus in ('', 'x' * 257, '['):
            try:
                namespace['wifi_log_summary'](focus=invalid_focus)
            except (ValueError, re.error):
                pass
            else:
                raise AssertionError('Invalid focus expression accepted')
        native_log.write_text(''.join(f'Autojoin decision {index}\n' for index in range(120)), encoding='utf-8')
        bounded = namespace['wifi_log_summary']()
        retained = [record['text'] for record in bounded['records']]
        assert retained == [f'Autojoin decision {index}' for index in (*range(50), *range(70, 120))]
        assert bounded['selection'] == {'focus': None, 'matched': 120, 'redacted': 0, 'omitted': 20}
        rotated = native_log.with_name('wifimanager.log.1')
        rotated.write_text('Autojoin skipped candidate reason=host-test\n', encoding='utf-8')
        namespace['os'].utime(native_log, ns=(1, 1))
        assert namespace['wifi_log_summary']()['log']['path'] == '/private/var/logs/wifimanager.log.1'
        # 當機報告：合成一份 309 型、一份 Jetsam、一份壞檔與一份不取內容的檔案
        crash_root = namespace['ROOT'] / 'private/var/mobile/Library/Logs/CrashReporter'
        (crash_root / 'Retired').mkdir(parents=True)
        header = {'app_name': 'com.apple.WebKit.GPU', 'bug_type': '309', 'timestamp': 'host-test',
                  'os_version': 'iPhone OS 16.7', 'incident_id': 'not-copied'}
        body = {'exception': {'type': 'EXC_BAD_ACCESS', 'signal': 'SIGSEGV', 'subtype': 'KERN_INVALID_ADDRESS'},
                'termination': {'namespace': 'SIGNAL', 'code': 11, 'indicator': 'Segmentation fault: 11'},
                'faultingThread': 1, 'usedImages': [{'name': 'WebKit'}, {'name': 'Metal'}],
                'threads': [{'frames': []}, {'queue': 'q', 'frames': [
                    {'imageIndex': 1, 'symbol': 'MTLDevice_new', 'symbolLocation': 8}] * 20}]}
        (crash_root / 'com.apple.WebKit.GPU-2026-11-17-174900.ips').write_text(
            json.dumps(header) + '\n' + json.dumps(body), encoding='utf-8')
        (crash_root / 'Retired' / 'JetsamEvent-2026-11-17-174950.ips').write_text(
            json.dumps({'bug_type': '298'}) + '\n' + json.dumps({'largestProcess': 'com.apple.WebKit.WebContent',
                'processes': [{'name': 'com.apple.WebKit.WebContent', 'reason': 'per-process-limit', 'rpages': 9},
                              {'name': 'unrelated', 'rpages': 1}]}), encoding='utf-8')
        (crash_root / 'MobileSafari-broken.ips').write_text('{not json', encoding='utf-8')
        (crash_root / 'Analytics-2026-11-17.ips.ca.synced').write_text('x', encoding='utf-8')
        for offset, name in enumerate(('com.apple.WebKit.GPU-2026-11-17-174900.ips', 'Retired/JetsamEvent-2026-11-17-174950.ips',
                                       'MobileSafari-broken.ips', 'Analytics-2026-11-17.ips.ca.synced')):
            namespace['os'].utime(crash_root / name, ns=((10 + offset) * 10**9, (10 + offset) * 10**9))
        crashes = namespace['crash_summary']()
        assert crashes['listing_complete'] is True and len(crashes['entries']) == 4
        assert [item['path'].rsplit('/', 1)[1] for item in crashes['details']] == [
            'MobileSafari-broken.ips', 'JetsamEvent-2026-11-17-174950.ips', 'com.apple.WebKit.GPU-2026-11-17-174900.ips']
        broken, jetsam, gpu = crashes['details']
        assert broken['error'] == 'crash_unreadable_or_over_limit'
        assert jetsam['largest_process'] == 'com.apple.WebKit.WebContent'
        assert jetsam['processes'] == [{'name': 'com.apple.WebKit.WebContent', 'reason': 'per-process-limit',
                                        'rpages': 9, 'lifetimeMax': None}]
        assert gpu['exception']['type'] == 'EXC_BAD_ACCESS' and gpu['termination']['code'] == 11
        assert 'incident_id' not in gpu['header'] and 'not-copied' not in json.dumps(crashes)
        assert len(gpu['faulting_thread']['frames']) == 12
        assert gpu['faulting_thread']['frames'][0] == {'image': 'Metal', 'symbol': 'MTLDevice_new', 'offset': 8}
        # AppStore 基線唯讀摘要測試：含 >1MiB launchd.plist 與不應被讀取的機密檔案內容
        xpc_dir = namespace['ROOT'] / 'System/Library/xpc'
        xpc_dir.mkdir(parents=True)
        large_launchd = {
            'LaunchDaemons': {
                '/System/Library/LaunchDaemons/com.apple.fairplayd.H2.plist': {
                    'Label': 'com.apple.fairplayd.H2', 'Disabled': True, 'Program': '/usr/sbin/fairplayd.H2',
                },
                '/System/Library/LaunchDaemons/com.apple.appstored.plist': {
                    'Label': 'com.apple.appstored', 'Disabled': False,
                    'Program': '/System/Library/PrivateFrameworks/AppStoreDaemon.framework/Support/appstored',
                },
                '/System/Library/LaunchDaemons/com.apple.padding.plist': {
                    'Label': 'com.apple.padding', 'Note': 'P' * (1200 * 1024),
                },
            }
        }
        (xpc_dir / 'launchd.plist').write_bytes(plistlib.dumps(large_launchd))
        sys_ld = namespace['ROOT'] / 'System/Library/LaunchDaemons'
        sys_ld.mkdir(parents=True)
        (sys_ld / 'com.apple.appstored.plist').write_bytes(plistlib.dumps({'Label': 'com.apple.appstored'}))
        disabled_dir = namespace['ROOT'] / 'private/var/db/com.apple.xpc.launchd'
        disabled_dir.mkdir(parents=True)
        (disabled_dir / 'disabled.plist').write_bytes(
            plistlib.dumps({'com.apple.fairplayd.H2': True, 'com.apple.unrelated': True}))
        fp_dir = namespace['ROOT'] / 'private/var/mobile/Library/FairPlay'
        fp_dir.mkdir(parents=True)
        (fp_dir / 'IC-Info.sidb').write_text('SECRET_FAIRPLAY_BYTES', encoding='utf-8')
        acc_dir = namespace['ROOT'] / 'private/var/mobile/Library/Accounts'
        acc_dir.mkdir(parents=True)
        (acc_dir / 'Accounts3.sqlite').write_text('SECRET_ACCOUNTS_DB', encoding='utf-8')
        timed_dir = namespace['ROOT'] / 'private/var/db/timed'
        timed_dir.mkdir(parents=True)
        (timed_dir / 'fuxi-rtc-offset.bin').write_bytes(b'\x01' * 16)
        (crash_root / 'appstored-2026-11-23.ips').write_text('SECRET_CRASH_BODY', encoding='utf-8')
        baseline = namespace['appstore_baseline_summary']()
        baseline_json = json.dumps(baseline)
        assert baseline['xpc_launchd']['launch_daemons_count'] == 3
        assert baseline['xpc_launchd']['jobs']['com.apple.fairplayd.H2']['configuration']['Disabled'] is True
        assert baseline['xpc_launchd']['jobs']['com.apple.appstored']['configuration']['Disabled'] is False
        assert baseline['disabled_plists'][0]['jobs'] == {'com.apple.fairplayd.H2': True}
        assert baseline['disabled_plists'][0]['other_entries_count'] == 1
        assert baseline['crash_reporter']['contents_read'] is False
        assert any(e['path'].endswith('appstored-2026-11-23.ips') and e['appstore_related'] is True
                   for e in baseline['crash_reporter']['entries'])
        for secret in ('SECRET_FAIRPLAY_BYTES', 'SECRET_ACCOUNTS_DB', 'SECRET_CRASH_BODY'):
            assert secret not in baseline_json
    mutations = [text.replace('CHUNK 0000 ', 'CHUNK 0001 ', 1),
                 text.replace('CHUNK 0000 ', 'CHUNK 0000 !', 1),
                 text.replace(SUCCESS, 'POWERLOG_STATE_FAILED'),
                 text.replace('GUEST_OK', ''), text + SUCCESS + '\n',
                 text.replace('sha256=', 'sha256=0', 1),
                 '\n'.join(line for line in text.splitlines() if 'CHUNK 0002 ' not in line),
                 transfer(result | {'root_writes': 1}), transfer(result | {'hfs_readonly': False}),
                 transfer(result | {'database_contents_read': True})]
    for invalid in mutations:
        try:
            decode(invalid)
        except ValueError:
            pass
        else:
            raise AssertionError('Invalid transfer accepted')
    try:
        transfer(result | {'padding': 'x' * MAX_JSON})
    except ValueError:
        pass
    else:
        raise AssertionError('Oversize JSON accepted')
    print('POWERLOG_STATE_SELF_TEST_OK checks=46 guest_python_compiled=1 phone_operations=0')


def inspect(arguments):
    output = Path(arguments.output).resolve()
    if output.parent != DIRECTORY or output.exists() or not re.fullmatch('inspect-[a-z0-9-]+', output.name):
        raise ValueError('Use a new inspect directory under statusbar-fix')
    specification = importlib.util.spec_from_file_location('phone_readthrough', DIRECTORY / 'phone-readthrough.py')
    phone = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(phone)
    with phone.phone_locks(output.name):
        if phone.run(['adb', '-s', arguments.serial, 'shell', 'getprop', 'sys.boot_completed']).strip() != b'1':
            raise RuntimeError('Android is not ready')
        output.mkdir()
        mapped = False
        try:
            mapped = True
            phone.say(phone.adb(arguments.serial, f'{phone.MANAGER} map').decode().strip())
            verification = phone.run([sys.executable, '-X', 'utf8',
                phone.ROOT / 'work/xiaomi13-ios-gpu-20261108/verify-native-map.py'],
                cwd=phone.ROOT / 'work/Codex-Xiaomi13-GPU-20261109', timeout=120)
            (output / 'mapping.txt').write_bytes(verification)
            device = phone.adb(arguments.serial, f'readlink -f {phone.MAPPER}').decode().strip()
            if not re.fullmatch('/dev/block/dm-[0-9]+', device):
                raise RuntimeError('Unexpected mapper device')
            if int(phone.adb(arguments.serial, f'blockdev --getsize64 {phone.MAPPER}')) != phone.SIZE:
                raise RuntimeError('Unexpected root size')
            mounts = phone.adb(arguments.serial, 'cat /proc/mounts').decode().splitlines()
            if any(line.split()[0] in (device, phone.MAPPER) for line in mounts):
                raise RuntimeError('The root is mounted on the phone')
            share = output / 'share'
            share.mkdir()
            shutil.copy2(DIRECTORY / 'powerlog-state-guest.sh', share)
            if arguments.wifi:
                (share / 'inspect-wifi').write_bytes(b'1\n')
            if arguments.wifi_log:
                (share / 'inspect-wifi-log').write_bytes(b'1\n')
            if arguments.crash_reports:
                (share / 'inspect-crash').write_bytes(b'1\n')
            if arguments.appstore_baseline:
                (share / 'inspect-appstore-baseline').write_bytes(b'1\n')
            blocks = phone.PhoneBlocks(output / 'read-cache', lambda index: phone.adb(
                arguments.serial, f'dd if={phone.MAPPER} bs={phone.BLOCK} skip={index} count=1 2>/dev/null'))
            with phone.nbd(blocks) as (address, failures):
                overlay = output / 'inspect.qcow2'
                phone.run([phone.QEMU_IMG, 'create', '-q', '-f', 'qcow2', '-b', address, '-F', 'raw', overlay])
                with (output / 'guest.log').open('wb') as logfile:
                    subprocess.run([sys.executable, '-X', 'utf8', DIRECTORY / 'sb-guest-run.py',
                        overlay, share, 'powerlog-state-guest.sh', SUCCESS, '-', str(arguments.port)],
                        stdout=logfile, stderr=subprocess.STDOUT, check=True, timeout=1500)
                allocation = json.loads(phone.run([phone.QEMU_IMG, 'map', '--output=json', overlay], timeout=120))
                if any(entry.get('depth') == 0 and entry.get('present') for entry in allocation):
                    raise RuntimeError('Guest wrote root overlay; refusing summary')
                if failures:
                    raise IOError('NBD read failure')
            log = output / 'guest.log'
            if log.stat().st_size > MAX_LOG:
                raise ValueError('Guest log exceeds bound')
            result, payload = decode(log.read_text(encoding='utf-8', errors='strict'))
            (output / 'powerlog-state.json').write_bytes(payload)
            provenance = {'method': 'locked read-only phone NBD; HFS and guest block read-only',
                          'root_bytes': phone.SIZE, 'phone_read_blocks': blocks.reads,
                          'guest_root_overlay_writes': 0, 'summary_sha256': hashlib.sha256(payload).hexdigest(),
                          'runtime_state_observed': False, 'clock_correctness_established': False,
                          'appstore_baseline': bool(arguments.appstore_baseline)}
            (output / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n',
                                                   encoding='utf-8', newline='\n')
            phone.say('POWERLOG_STATE_INSPECT_OK', len(payload), 'bytes', blocks.reads, 'blocks')
        finally:
            if mapped:
                phone.say(phone.adb(arguments.serial, f'{phone.MANAGER} unmap').decode().strip())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Read-only root metadata inspector; device mode is main-agent-only.')
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--self-test', action='store_true')
    mode.add_argument('--output')
    mode.add_argument('--parse-log', type=Path)
    parser.add_argument('--serial', default=os.environ.get('FUXI_ANDROID_SERIAL', ''))
    parser.add_argument('--port', type=int, default=34615)
    parser.add_argument('--wifi-log', action='store_true',
                        help='Read only filtered native Wi-Fi decision messages and the logging flag')
    parser.add_argument('--wifi', action='store_true',
                        help='Also summarize saved Wi-Fi profile flags; omit passwords and keychain contents')
    parser.add_argument('--crash-reports', action='store_true',
                        help='List CrashReporter files and summarize recent WebKit/Safari/backboardd/Jetsam reports')
    parser.add_argument('--appstore-baseline', action='store_true',
                        help='Read-only baseline for AppStore/FairPlay: xpc launchd, disabled plists, directory listings, CrashReporter metadata')
    arguments = parser.parse_args()
    if arguments.crash_reports and (arguments.wifi or arguments.wifi_log or arguments.appstore_baseline):
        parser.error('--crash-reports is a separate inspection')
    if arguments.appstore_baseline and (arguments.wifi or arguments.wifi_log or arguments.crash_reports):
        parser.error('--appstore-baseline is a separate inspection')
    if not 1024 <= arguments.port <= 65535:
        parser.error('--port must be 1024..65535')
    if arguments.self_test:
        self_test()
    elif arguments.parse_log:
        if arguments.parse_log.stat().st_size > MAX_LOG:
            raise ValueError('Guest log exceeds bound')
        summary, payload = decode(arguments.parse_log.read_text(encoding='utf-8'))
        print(json.dumps({'bytes': len(payload), 'sha256': hashlib.sha256(payload).hexdigest(),
                          'hfs_readonly': summary['hfs_readonly'], 'runtime_daemon_state': 'not_observed'}))
    else:
        inspect(arguments)
