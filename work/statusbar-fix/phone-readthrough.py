import argparse
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import socketserver
import struct
import subprocess
import sys
import threading
import time

ROOT = Path(__file__).resolve().parents[2]
BLOCK = 4194304
SIZE = 21474836480
QEMU_IMG = ROOT / 'candidates/vm-tools/qemu/qemu-img.exe'
MANAGER = 'CLASSPATH=/data/local/tmp/codex-managed-native-image.jar app_process /system/bin ManagedNativeImage'
MAPPER = '/dev/block/mapper/codexnative_root_1108b'
# 內建 App 模式復用 ksrc 的 sweet-root-tweaks.py（--install-staged-apps，有主機測試）。
TWEAKS = ROOT / 'work/adsp-battery/ksrc/tools/sweet-root-tweaks.py'
# 第三方 App 模式：resign-app-adhoc.py 的產物（app.tar 掛成唯讀 vdc），guest 端由 install-app-guest.py 安裝。
HERE = Path(__file__).resolve().parent
RESIGN = HERE / 'resign-app-adhoc.py'
GUEST_INSTALLER = HERE / 'install-app-guest.py'


def say(*values):
    print(time.strftime('%H:%M:%S'), *values, flush=True)


def run(arguments, **options):
    result = subprocess.run(list(map(str, arguments)), check=True, capture_output=True, **options)
    return result.stdout


def adb(serial, command):
    assert "'" not in command
    return run(['adb', '-s', serial, 'exec-out', f"su -c '{command}'"], timeout=120)


class PhoneBlocks:
    def __init__(self, directory, fetch):
        self.directory = directory
        self.directory.mkdir()
        self.fetch = fetch
        self.lock = threading.Lock()
        self.reads = 0

    def block(self, index):
        if not 0 <= index < SIZE // BLOCK:
            raise ValueError('Block outside the iOS root')
        with self.lock:
            path = self.directory / f'{index}.bin'
            if not path.exists():
                value = self.fetch(index)
                if len(value) != BLOCK:
                    raise IOError(f'Short phone read at {index}: {len(value)} bytes')
                path.write_bytes(value)
                self.reads += 1
                if self.reads % 32 == 0:
                    say('READ_THROUGH', self.reads, 'blocks', self.reads * 4, 'MiB')
            return path.read_bytes()

    def read(self, offset, length):
        if offset < 0 or length < 0 or offset + length > SIZE:
            raise ValueError('Read outside the iOS root')
        chunks = []
        while length:
            index, within = divmod(offset, BLOCK)
            count = min(length, BLOCK - within)
            chunks.append(self.block(index)[within:within + count])
            length -= count
            offset += count
        return b''.join(chunks)


class ReadOnlyNbd(socketserver.BaseRequestHandler):
    def receive(self, length):
        result = bytearray()
        while len(result) < length:
            value = self.request.recv(length - len(result))
            if not value:
                raise EOFError
            result.extend(value)
        return bytes(result)

    def handle(self):
        self.request.sendall(b'NBDMAGIC' + struct.pack('>QQI', 0x420281861253, SIZE, 3) + bytes(124))
        try:
            while True:
                magic, operation, handle, offset, length = struct.unpack('>IIQQI', self.receive(28))
                if magic != 0x25609513 or length > 32 * 1024 * 1024:
                    raise ValueError('Invalid NBD request')
                if operation == 2:
                    return
                error, value = 0, b''
                if operation == 1:
                    self.receive(length)
                    error = 1
                elif operation == 0:
                    try:
                        value = self.server.blocks.read(offset, length)
                    except Exception as failure:
                        self.server.failures.append(str(failure))
                        error = 5
                elif operation != 3:
                    error = 95
                self.request.sendall(struct.pack('>IIQ', 0x67446698, error, handle) + value)
        except (EOFError, ConnectionError):
            return


@contextmanager
def nbd(blocks):
    class Server(socketserver.ThreadingTCPServer):
        daemon_threads = True

    server = Server(('127.0.0.1', 0), ReadOnlyNbd)
    server.blocks = blocks
    server.failures = []
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        yield f'nbd://127.0.0.1:{server.server_address[1]}', server.failures
    finally:
        server.shutdown()
        server.server_close()
        worker.join()


@contextmanager
def phone_locks(label):
    token = f'000 read-through {label} pid={os.getpid()} {time.time()}'
    locks = [ROOT / 'work/PHONE-LOCK-000', ROOT / 'work/hybrid-driver/HOLD-PHONE']
    pause = ROOT / 'work/adsp-battery/hw000/HOLD'
    acquired = []
    try:
        if pause.exists():
            raise RuntimeError('Phone work is paused')
        for path in locks:
            with path.open('x', encoding='utf-8') as stream:
                stream.write(token)
            acquired.append(path)
        if pause.exists():
            raise RuntimeError('Phone work is paused')
        yield
    finally:
        for path in acquired:
            if path.exists() and path.read_text(encoding='utf-8') == token:
                path.unlink()
        say('PHONE_RELEASED', label)


def extract(overlay, blocks, output, limit=64):
    mapping = json.loads(run([QEMU_IMG, 'map', '--output=json', '-f', 'qcow2', overlay], timeout=120))
    changed = set()
    for entry in mapping:
        if entry['depth'] == 0 and entry['present']:
            first = entry['start'] // BLOCK
            last = (entry['start'] + entry['length'] - 1) // BLOCK
            changed.update(range(first, last + 1))
    if not 1 <= len(changed) <= limit:
        raise RuntimeError(f'Unexpected number of written blocks: {len(changed)}')
    records = []
    with (output / 'sb-new.bin').open('wb') as newer, (output / 'sb-old.bin').open('wb') as older:
        for index in sorted(changed):
            temporary = output / f'extract-{index}.bin'
            run([QEMU_IMG, 'dd', '-f', 'qcow2', '-O', 'raw', f'bs={BLOCK}', f'skip={index}',
                 f'count={index + 1}', f'if={overlay}', f'of={temporary}'], timeout=180)
            after = temporary.read_bytes()
            temporary.unlink()
            before = blocks.block(index)
            if len(after) != BLOCK:
                raise IOError(f'Short extracted block: {index}')
            if before == after:
                continue
            newer.write(after)
            older.write(before)
            records.append((index, hashlib.sha256(before).hexdigest(), hashlib.sha256(after).hexdigest()))
    if not records:
        raise RuntimeError('Staging made no changes')
    (output / 'sb-list.txt').write_text(
        ''.join(f'{index} {before} {after}\n' for index, before, after in records),
        encoding='utf-8', newline='\n')
    (output / 'sb-undo-list.txt').write_text(
        ''.join(f'{index} {after} {before}\n' for index, before, after in records),
        encoding='utf-8', newline='\n')
    say('CHANGED_BLOCKS', [record[0] for record in records])


def self_test():
    import tempfile

    with tempfile.TemporaryDirectory(prefix='fuxi-readthrough-') as temporary:
        directory = Path(temporary)
        calls = []

        def fetch(index):
            calls.append(index)
            return bytes([index % 251]) * BLOCK

        blocks = PhoneBlocks(directory / 'cache', fetch)
        with nbd(blocks) as (address, failures):
            overlay = directory / 'overlay.qcow2'
            run([QEMU_IMG, 'create', '-q', '-f', 'qcow2', '-b', address, '-F', 'raw', overlay])
            extracted = directory / 'read.bin'
            run([QEMU_IMG, 'dd', '-f', 'qcow2', f'if={overlay}', f'of={extracted}',
                 f'bs={BLOCK}', 'skip=1', 'count=3'])
            assert extracted.read_bytes() == bytes([1]) * BLOCK + bytes([2]) * BLOCK
            assert blocks.read(BLOCK - 3, 7) == bytes(3) + bytes([1]) * 4
            assert len(calls) == len(set(calls)) and len(calls) < 8
            assert not failures
            assert json.loads(run([QEMU_IMG, 'info', '--output=json', overlay]))['virtual-size'] == SIZE
        say('READ_THROUGH_SELF_TEST_OK', len(calls), 'blocks')


def app_description(directory):
    """重新核對重簽產物，回傳 guest 的 app.txt 內容、tar 與 files.sha256 路徑。"""
    run([sys.executable, '-X', 'utf8', '-I', RESIGN, 'verify', directory], timeout=600)
    manifest = json.loads((directory / 'manifest.json').read_text(encoding='utf-8'))
    sums = {}
    for line in (directory / 'SHA256SUMS').read_text(encoding='ascii').splitlines():
        digest, name = line.split('  ', 1)
        sums[name] = digest
    tar = directory / 'app.tar'
    text = '\n'.join([manifest['app'], manifest['bundle_identifier'], str(tar.stat().st_size), sums['app.tar']]) + '\n'
    return text, tar, directory / 'files.sha256'


def audio_hal_plan(action, bundle, checksum):
    names = ('FuxiAudio', 'Info.plist')
    if not bundle.is_dir() or sorted(path.name for path in bundle.iterdir()) != list(names):
        raise ValueError('Expected the verified FuxiAudio.driver bundle')
    if any((bundle / name).is_symlink() or not (bundle / name).is_file() for name in names):
        raise ValueError('Audio trial requires regular bundle files')
    verified = json.loads(run([sys.executable, '-X', 'utf8', ROOT / 'work/localbuild/verify-sb-shape.py',
                               '--audio-bundle', bundle]))
    if verified['sha256'] != checksum:
        raise ValueError('FuxiAudio hash differs from the verified build')
    editor = ROOT / 'work/adsp-battery/ksrc/tools/fuxi-sys-root.py'
    plan = {'action': action, 'files': dict(zip(names, (verified['sha256'], verified['info_sha256']))),
            'editor_sha256': hashlib.sha256(editor.read_bytes()).hexdigest()}
    return editor, json.dumps(plan, indent=2) + '\n'


def stage(arguments):
    output = Path(arguments.output).resolve()
    if output.parent != Path(__file__).resolve().parent:
        raise ValueError('Use a run directory under statusbar-fix')
    if output.exists() != arguments.write_staged:
        raise ValueError('Use a new directory, or --write-staged for a completed guest run')
    zone = arguments.timezone
    job = arguments.disable_job
    turnip = arguments.turnip_hybrid
    wifi_logging = arguments.wifi_logging
    apps = arguments.install_staged_apps
    app_dir = Path(arguments.install_app).resolve() if arguments.install_app else None
    audio_hal = arguments.audio_hal
    # 32 個 App 目錄改名＋placeholder 停放會改到分散的 catalog 節點，上限放寬到 256 區塊（1 GiB）；
    # 第三方 App 約 140 MiB 資料＋catalog／extents／bitmap，同樣用 256。
    limit = 256 if apps or app_dir else 64
    if apps and (zone is not None or job is not None or turnip or wifi_logging is not None or
                 arguments.dylib or arguments.sha256):
        raise ValueError('--install-staged-apps is a separate staging run')
    if app_dir and (apps or zone is not None or job is not None or turnip or wifi_logging is not None or
                    arguments.dylib or arguments.sha256):
        raise ValueError('--install-app is a separate staging run')
    if wifi_logging is not None and (zone is not None or job is not None or turnip or
                                     arguments.dylib or arguments.sha256):
        raise ValueError('--wifi-logging is a separate configuration staging run')
    if turnip and (zone is not None or job is not None or arguments.dylib):
        raise ValueError('--turnip-hybrid is a separate library staging run')
    if zone is not None and job is not None:
        raise ValueError('--timezone and --disable-job are separate runs')
    if audio_hal:
        if any((zone, job, turnip, wifi_logging, apps, app_dir, arguments.dylib)):
            raise ValueError('--audio-hal is a separate staging run')
        bundle = Path(arguments.audio_bundle).resolve(strict=True)
        editor, audio_plan = audio_hal_plan(audio_hal, bundle, arguments.sha256)
        guest = Path(__file__).resolve().parent / 'stage-audio-hal-guest.py'
        script, token = 'stage-audio-hal.sh', 'AUDIO_HAL_STAGE_SUCCESS'
    elif app_dir:
        script, token = 'stage-install-app.sh', 'INSTALL_APP_STAGE_SUCCESS'
        tweaks_sha = hashlib.sha256(TWEAKS.read_bytes()).hexdigest()
        app_text, app_tar, app_files = app_description(app_dir)
    elif apps:
        script, token = 'stage-staged-apps.sh', 'STAGED_APPS_STAGE_SUCCESS'
        tweaks_sha = hashlib.sha256(TWEAKS.read_bytes()).hexdigest()
    elif wifi_logging is not None:
        script, token = 'stage-wifi-logging.sh', 'WIFI_LOGGING_STAGE_SUCCESS'
    elif zone is not None:
        # 時區模式：只換 /private/var/db/timezone/localtime 這個 symlink，不帶 dylib。
        if not re.fullmatch(r'[A-Za-z_]+/[A-Za-z_]+', zone) or arguments.dylib or arguments.sha256:
            raise ValueError('--timezone takes Area/City and no dylib')
        script, token = 'stage-timezone.sh', 'TIMEZONE_STAGE_SUCCESS'
    elif job is not None:
        # launchd 模式：在 launchd.plist 把一個 job 標成 Disabled，不帶 dylib。
        if not re.fullmatch(r'com\.apple\.[A-Za-z0-9.]+', job) or arguments.dylib or arguments.sha256:
            raise ValueError('--disable-job takes one com.apple label and no dylib')
        script, token = 'stage-launchd-disable.sh', 'LAUNCHD_STAGE_SUCCESS'
    else:
        dylib = Path(turnip or arguments.dylib).resolve()
        if hashlib.sha256(dylib.read_bytes()).hexdigest() != arguments.sha256:
            raise ValueError('Dylib hash differs from the CI manifest')
        script = 'stage-turnip-hybrid.sh' if turnip else 'stage-shape.sh'
        token = 'FAST_STAGE_SUCCESS'
    serial = arguments.serial
    if run(['adb', '-s', serial, 'shell', 'getprop', 'sys.boot_completed']).strip() != b'1':
        raise RuntimeError('Android is not ready')
    if not arguments.write_staged:
        output.mkdir()
    mapped = False
    with phone_locks(output.name):
        try:
            say('PHONE_START', output.name)
            say(adb(serial, f'{MANAGER} map').decode().strip())
            mapped = True
            verification = run(
                [sys.executable, '-X', 'utf8', ROOT / 'work/xiaomi13-ios-gpu-20261108/verify-native-map.py'],
                cwd=ROOT / 'work/Codex-Xiaomi13-GPU-20261109', timeout=120)
            (output / 'mapping.txt').write_bytes(verification)
            device = adb(serial, f'readlink -f {MAPPER}').decode().strip()
            if not re.fullmatch(r'/dev/block/dm-\d+', device):
                raise RuntimeError('Unexpected mapped device')
            if int(adb(serial, f'blockdev --getsize64 {MAPPER}')) != SIZE:
                raise RuntimeError('Unexpected root size')
            mounts = adb(serial, 'cat /proc/mounts').decode()
            if any(line.split()[0] in (device, MAPPER) for line in mounts.splitlines()):
                raise RuntimeError('The iOS root is mounted')
            share = output / 'share'
            read_count = 0
            if arguments.write_staged:
                guest_log = (output / 'guest.log').read_text(encoding='utf-8')
                if token not in guest_log or 'FAST_STAGE_SUCCESS' not in guest_log or 'GUEST_OK' not in guest_log:
                    raise RuntimeError('The staged guest did not finish')
                if audio_hal:
                    if ((share / 'audio-hal.json').read_text(encoding='ascii') != audio_plan or
                            (share / 'stage-audio-hal-guest.py').read_bytes() != guest.read_bytes() or
                            (share / 'fuxi-sys-root.py').read_bytes() != editor.read_bytes() or
                            any((share / 'FuxiAudio.driver' / name).read_bytes() != (bundle / name).read_bytes()
                                for name in ('FuxiAudio', 'Info.plist'))):
                        raise ValueError('The staged audio trial differs')
                elif apps or app_dir:
                    if hashlib.sha256((share / 'sweet-root-tweaks.py').read_bytes()).hexdigest() != tweaks_sha:
                        raise ValueError('The staged root tweak script differs')
                    if app_dir and ((share / 'app.txt').read_text(encoding='ascii') != app_text or
                                    (share / 'files.sha256').read_bytes() != app_files.read_bytes() or
                                    (share / 'install-app-guest.py').read_bytes() != GUEST_INSTALLER.read_bytes()):
                        raise ValueError('The staged app differs')
                elif wifi_logging is not None:
                    if (share / 'wifi-logging.txt').read_text(encoding='ascii').strip() != wifi_logging:
                        raise ValueError('The staged Wi-Fi logging action differs')
                elif zone is not None:
                    if (share / 'zone.txt').read_text(encoding='ascii').strip() != zone:
                        raise ValueError('The staged time zone differs')
                elif job is not None:
                    if (share / 'job.txt').read_text(encoding='ascii').strip() != job:
                        raise ValueError('The staged launchd job differs')
                elif hashlib.sha256((share / 'libfuxi-sb.dylib').read_bytes()).hexdigest() != arguments.sha256:
                    raise ValueError('The staged dylib differs')
                if zone is None and job is None and wifi_logging is None and not apps and not app_dir and not audio_hal:
                    kind_path = share / 'library-kind.txt'
                    staged_kind = kind_path.read_text(encoding='ascii').strip() if kind_path.exists() else 'shape'
                    if staged_kind != ('turnip-hybrid' if turnip else 'shape'):
                        raise ValueError('The staged library target differs')
            else:
                blocks = PhoneBlocks(output / 'read-cache', lambda index: adb(
                    serial, f'dd if={MAPPER} bs={BLOCK} skip={index} count=1 2>/dev/null'))
                share.mkdir()
                for name in (script, 'stage-share/hfs-journal-block-probe.py'):
                    shutil.copy2(Path(__file__).resolve().parent / name, share)
                if audio_hal:
                    shutil.copytree(bundle, share / 'FuxiAudio.driver')
                    shutil.copy2(editor, share)
                    shutil.copy2(guest, share)
                    (share / 'audio-hal.json').write_text(audio_plan, encoding='ascii', newline='\n')
                elif apps or app_dir:
                    shutil.copy2(TWEAKS, share / 'sweet-root-tweaks.py')
                    (share / 'tweaks-sha256.txt').write_text(
                        f'{tweaks_sha}  sweet-root-tweaks.py\n', encoding='ascii', newline='\n')
                    if app_dir:
                        shutil.copy2(GUEST_INSTALLER, share / 'install-app-guest.py')
                        shutil.copy2(app_files, share / 'files.sha256')
                        (share / 'app.txt').write_text(app_text, encoding='ascii', newline='\n')
                elif wifi_logging is not None:
                    shutil.copy2(Path(__file__).resolve().parent / 'configure-wifi-logging.py', share)
                    (share / 'wifi-logging.txt').write_text(wifi_logging + '\n', encoding='ascii', newline='\n')
                elif zone is not None:
                    (share / 'zone.txt').write_text(zone + '\n', encoding='ascii', newline='\n')
                elif job is not None:
                    (share / 'job.txt').write_text(job + '\n', encoding='ascii', newline='\n')
                else:
                    shutil.copy2(dylib, share / 'libfuxi-sb.dylib')
                    (share / 'library-kind.txt').write_text(
                        ('turnip-hybrid' if turnip else 'shape') + '\n', encoding='ascii', newline='\n')
                    (share / 'shape-sha256.txt').write_text(
                        f'{arguments.sha256}  libfuxi-sb.dylib\n', encoding='ascii', newline='\n')
                (share / 'fast-stage.sh').write_text(
                    f'#!/bin/sh\nset -eu\nsh /mnt/share/{script}\n'
                    'sync\necho FAST_STAGE_SUCCESS\n', encoding='ascii', newline='\n')
                with nbd(blocks) as (address, failures):
                    overlay = output / 'overlay.qcow2'
                    run([QEMU_IMG, 'create', '-q', '-f', 'qcow2', '-b', address, '-F', 'raw', overlay])
                    say('GUEST_READ_THROUGH')
                    with (output / 'guest.log').open('wb') as logfile:
                        subprocess.run(
                            [sys.executable, '-X', 'utf8', Path(__file__).resolve().parent / 'sb-guest-run.py',
                             overlay, share, 'fast-stage.sh', 'FAST_STAGE_SUCCESS',
                             app_tar if app_dir else '-', '34612'],
                            stdout=logfile, stderr=subprocess.STDOUT, check=True, timeout=2400 if app_dir else 1500)
                    if failures:
                        raise IOError(f'Phone reads failed: {failures[:3]}')
                    extract(overlay, blocks, output, limit)
                    if failures:
                        raise IOError(f'Phone reads failed: {failures[:3]}')
                read_count = blocks.reads
            records = [line.split() for line in (output / 'sb-list.txt').read_text(encoding='utf-8').splitlines()]
            if not 1 <= len(records) <= limit:
                raise ValueError('Invalid block list')
            new_bytes = (output / 'sb-new.bin').read_bytes()
            old_bytes = (output / 'sb-old.bin').read_bytes()
            if len(new_bytes) != len(records) * BLOCK or len(old_bytes) != len(new_bytes):
                raise ValueError('Staged data length differs')
            seen = set()
            for position, record in enumerate(records):
                if len(record) != 3 or not record[0].isdigit():
                    raise ValueError('Invalid block record')
                index, before, after = int(record[0]), record[1], record[2]
                if not 0 <= index < SIZE // BLOCK or index in seen:
                    raise ValueError('Invalid or repeated block index')
                seen.add(index)
                region = slice(position * BLOCK, (position + 1) * BLOCK)
                if hashlib.sha256(new_bytes[region]).hexdigest() != after:
                    raise ValueError('Staged new data hash differs')
                if hashlib.sha256(old_bytes[region]).hexdigest() != before:
                    raise ValueError('Staged precondition hash differs')
            (output / 'sb-list.txt').write_text(
                ''.join(' '.join(record) + '\n' for record in records), encoding='ascii', newline='\n')
            remote = f'/data/local/tmp/codex-{output.name}'
            if not re.fullmatch(r'/data/local/tmp/codex-[a-z0-9-]+', remote):
                raise ValueError('Invalid remote staging path')
            adb(serial, f'mkdir {remote}; chmod 777 {remote}')
            for name in ('sb-new.bin', 'sb-list.txt'):
                run(['adb', '-s', serial, 'push', output / name, f'{remote}/{name}'], timeout=120)
            run(['adb', '-s', serial, 'push', Path(__file__).resolve().parent / 'sb-blocks-write.sh',
                 f'{remote}/sb-blocks-write.sh'], timeout=120)
            say('WRITE_CHANGED_BLOCKS')
            result = adb(serial, f'sh {remote}/sb-blocks-write.sh {remote}/sb-list.txt {remote}/sb-new.bin')
            (output / 'write.log').write_bytes(result)
            say(result.decode().strip())
            if b'WRITE_OK' not in result:
                raise RuntimeError('Changed-block verification did not finish')
            adb(serial, f'rm {remote}/sb-new.bin {remote}/sb-list.txt {remote}/sb-blocks-write.sh; rmdir {remote}')
            say('FAST_STAGE_DONE', 'phone read', read_count * 4, 'MiB')
        finally:
            if mapped:
                say(adb(serial, f'sync; {MANAGER} unmap').decode().strip())


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--dylib')
    parser.add_argument('--turnip-hybrid')
    parser.add_argument('--sha256')
    parser.add_argument('--output')
    parser.add_argument('--write-staged', action='store_true')
    parser.add_argument('--timezone', metavar='AREA/CITY',
                        help='Point the iOS localtime symlink at this zone instead of staging a dylib')
    parser.add_argument('--disable-job', metavar='LABEL',
                        help='Mark this launchd job Disabled in /System/Library/xpc/launchd.plist instead of staging a dylib')
    parser.add_argument('--wifi-logging', choices=('enable', 'disable'),
                        help='Toggle only the existing boolean LoggingFileEnabled in the Wi-Fi configuration')
    parser.add_argument('--install-staged-apps', action='store_true',
                        help='Move the 32 staged stock apps into /Applications (sweet-root-tweaks.py) and reset LaunchServices')
    parser.add_argument('--install-app', metavar='DIR',
                        help='Install a resign-app-adhoc.py output (app.tar) into /Applications and reset LaunchServices')
    parser.add_argument('--audio-hal', choices=('enable', 'disable'),
                        help='Install or back out the FuxiAudio HAL trial and VirtualAudio move')
    parser.add_argument('--audio-bundle', metavar='DIR')
    parser.add_argument('--serial', default=os.environ.get('FUXI_ANDROID_SERIAL', ''))
    arguments = parser.parse_args()
    if arguments.self_test:
        self_test()
    else:
        if bool(arguments.audio_hal) != bool(arguments.audio_bundle) or (arguments.audio_hal and not arguments.sha256):
            parser.error('--audio-hal needs --audio-bundle and the verified binary --sha256')
        if not arguments.output or not (arguments.timezone or arguments.disable_job or arguments.wifi_logging or
                                        arguments.install_staged_apps or arguments.install_app or arguments.audio_hal or
                                        ((arguments.dylib or arguments.turnip_hybrid) and arguments.sha256)):
            parser.error('--output plus a configuration action or a library and --sha256 are required')
        stage(arguments)
