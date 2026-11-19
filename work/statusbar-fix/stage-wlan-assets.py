import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath, PureWindowsPath
import re
import secrets
import stat
import subprocess
import sys
import tempfile


DIRECTORY = Path(__file__).resolve().parent
ROOT_PATH = '/data/gsi/codexnative1108b/codexnative_root_1108b.img.0000'
ROOT_BYTES = 21474836480
ROOT_INODE = 199819
BLOCK_BYTES = 4194304
SERIAL = os.environ.get('FUXI_ANDROID_SERIAL', '')
WLAN_PATH = PurePosixPath('/usr/local/share/fuxi/wlan')
FIXED_FILES = ('amss.bin', 'amss20.bin', 'regdb_xiaomi.bin', 'm3.bin')
# 2026-11-18：manifest 帶 "kind": "bt" 時改部署 WCN7850 藍牙韌體（核心 fuxi,bt=4 由這個目錄讀
# hmtbtfw20.tlv 與 hmtnv20.b<board ID>／.bin）；覆蓋層、fsck、逐塊比對與讀回流程與 WLAN 相同。
BT_PATH = PurePosixPath('/usr/local/share/fuxi/bt')
BT_PATCH = 'hmtbtfw20.tlv'
BT_NAME = re.compile(r'hmtbtfw20\.(?:tlv|ver)|hmtnv20\.(?:b[0-9a-f]{2,4}|bin)')
BT_MAX_FILES = 64
BT_MAX_PATCH_BYTES = 1024 * 1024  # 核心 FUXI_BT_PATCH_MAX
BT_MAX_NVM_BYTES = 65536  # 核心 FUXI_BT_NVM_MAX
MAX_FILE_BYTES = 32 * 1024 * 1024
MAX_TOTAL_BYTES = 64 * 1024 * 1024
MAX_M3_BYTES = 512 * 1024
MAX_TEXT_BYTES = 1024 * 1024
SOURCE_NAMES = (
    'stage-wlan-assets.py', 'stage-wlan-assets-guest.sh', 'phone-readthrough.py',
    'stage-shape.sh', 'sb-guest-run.py', 'sb-blocks-write.sh',
    'hfs-journal-block-probe.py', 'verify-native-map.py',
)


def digest(contents):
    return hashlib.sha256(contents).hexdigest()


def unique_object(pairs):
    result = {}
    for name, value in pairs:
        if name in result:
            raise ValueError(f'Duplicate JSON key: {name}')
        result[name] = value
    return result


def reject_constant(value):
    raise ValueError(f'Invalid JSON constant: {value}')


def decode_json(contents):
    return json.loads(contents.decode('utf-8'), object_pairs_hook=unique_object,
                      parse_constant=reject_constant)


def read_text_bytes(path):
    with Path(path).open('rb') as stream:
        if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
            raise ValueError(f'Expected a regular file: {path}')
        contents = stream.read(MAX_TEXT_BYTES + 1)
    if not contents or len(contents) > MAX_TEXT_BYTES:
        raise ValueError(f'Invalid text length: {path}')
    if b'\r' in contents or not contents.endswith(b'\n'):
        raise ValueError(f'Expected LF text with a final newline: {path}')
    contents.decode('utf-8')
    return contents


def provenance_has_text(value):
    if isinstance(value, str):
        return bool(value.strip())
    if isinstance(value, dict):
        return any(provenance_has_text(item) for item in value.values())
    if isinstance(value, list):
        return any(provenance_has_text(item) for item in value)
    return False


def is_bt(manifest):
    return manifest.get('kind') == 'bt'


def target_path(manifest):
    return BT_PATH if is_bt(manifest) else WLAN_PATH


def asset_names(manifest):
    if is_bt(manifest):
        return tuple(manifest['required'])
    return FIXED_FILES + (manifest['bdf_basename'],)


def file_limit(manifest, name):
    if is_bt(manifest):
        if name == BT_PATCH:
            return BT_MAX_PATCH_BYTES
        return BT_MAX_NVM_BYTES if name.startswith('hmtnv') else MAX_TEXT_BYTES
    return MAX_M3_BYTES if name == 'm3.bin' else MAX_FILE_BYTES


def validate_bt_names(manifest):
    if set(manifest) - {'kind', 'root_path', 'required', 'provenance', 'files'}:
        raise ValueError('Unexpected Bluetooth manifest fields')
    required = manifest.get('required')
    if not isinstance(required, list) or not all(isinstance(name, str) for name in required):
        raise ValueError('required must list the Bluetooth filenames')
    if not 2 <= len(required) <= BT_MAX_FILES or len({name.casefold() for name in required}) != len(required):
        raise ValueError('required must contain 2-64 distinct Bluetooth filenames')
    if not all(BT_NAME.fullmatch(name) for name in required):
        raise ValueError('Bluetooth filenames must be WCN7850 rampatch, version or NVM names')
    if BT_PATCH not in required or not any(name.startswith('hmtnv20.') for name in required):
        raise ValueError('The Bluetooth set needs the rampatch and at least one NVM')
    provenance = manifest.get('provenance')
    if not isinstance(provenance, dict) or not provenance_has_text(provenance):
        raise ValueError('Bluetooth provenance is required')
    return tuple(required)


def validate_manifest_schema(manifest):
    if not isinstance(manifest, dict) or manifest.get('root_path') != ROOT_PATH:
        raise ValueError('The manifest must name the fixed native root path')
    if 'kind' in manifest and manifest['kind'] != 'bt':
        raise ValueError('Unknown asset kind')
    if is_bt(manifest):
        names = validate_bt_names(manifest)
        return validate_file_records(manifest, names)
    basename = manifest.get('bdf_basename')
    if not isinstance(basename, str) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]{0,127}', basename):
        raise ValueError('bdf_basename must be a safe basename')
    if basename.casefold() in {name.casefold() for name in FIXED_FILES}:
        raise ValueError('The selected BDF collides with a fixed filename')
    names = asset_names(manifest)
    required = manifest.get('required')
    if not isinstance(required, list) or not all(isinstance(name, str) for name in required):
        raise ValueError('required must list all five filenames')
    if len(required) != 5 or len(set(required)) != 5 or set(required) != set(names):
        raise ValueError('required must contain exactly the five selected filenames')
    selection = manifest.get('selection_provenance')
    if not isinstance(selection, dict) or not provenance_has_text(selection):
        raise ValueError('Original BDF selection_provenance data is required')
    m3 = manifest.get('m3_provenance')
    if not isinstance(m3, (dict, str)) or not provenance_has_text(m3):
        raise ValueError('Nonempty m3_provenance is required')
    return validate_file_records(manifest, names)


def validate_file_records(manifest, names):
    files = manifest.get('files')
    if not isinstance(files, dict) or set(files) != set(names):
        raise ValueError('files must contain exactly the five selected filenames')
    total = 0
    host_paths = set()
    for name in names:
        record = files[name]
        if not isinstance(record, dict):
            raise ValueError(f'Invalid file record: {name}')
        checksum, length, host_path = (record.get(key) for key in ('sha256', 'bytes', 'host_path'))
        if not isinstance(checksum, str) or not re.fullmatch(r'[a-f0-9]{64}', checksum):
            raise ValueError(f'A SHA-256 is required: {name}')
        limit = file_limit(manifest, name)
        if type(length) is not int or not 1 <= length <= limit:
            raise ValueError(f'File size exceeds its bound: {name}')
        if not isinstance(host_path, str) or not host_path or any(character in host_path for character in '\0\r\n'):
            raise ValueError(f'Invalid host_path: {name}')
        windows_path = PureWindowsPath(host_path)
        if windows_path.is_absolute():
            path_key = str(windows_path).casefold()
        elif PurePosixPath(host_path).is_absolute():
            path_key = str(PurePosixPath(host_path))
        else:
            raise ValueError(f'host_path must be absolute: {name}')
        if path_key in host_paths:
            raise ValueError('Repeated host_path')
        host_paths.add(path_key)
        total += length
    if total > MAX_TOTAL_BYTES:
        raise ValueError('WLAN inputs exceed 64 MiB')
    return manifest


def stat_identity(metadata):
    return (metadata.st_dev, metadata.st_ino, metadata.st_size,
            metadata.st_mtime_ns)


def digest_file(path, limit=64 * BLOCK_BYTES):
    path = Path(path)
    with path.open('rb') as stream:
        before = os.fstat(stream.fileno())
        if not stat.S_ISREG(before.st_mode) or before.st_size > limit:
            raise ValueError(f'File type or hash length exceeds its bound: {path}')
        checksum = hashlib.file_digest(stream, 'sha256').hexdigest()
        after = os.fstat(stream.fileno())
    if stat_identity(before) != stat_identity(after) or stat_identity(after) != stat_identity(path.stat()):
        raise ValueError(f'File changed during hashing: {path}')
    return checksum


def read_verified_file(path, record):
    path = Path(path)
    with path.open('rb') as stream:
        before = os.fstat(stream.fileno())
        if not stat.S_ISREG(before.st_mode) or before.st_size != record['bytes']:
            raise ValueError(f'File size or type differs: {path}')
        contents = stream.read(record['bytes'] + 1)
        after = os.fstat(stream.fileno())
    if stat_identity(before) != stat_identity(after) or stat_identity(after) != stat_identity(path.stat()):
        raise ValueError(f'File changed during verification: {path}')
    if len(contents) != record['bytes'] or digest(contents) != record['sha256']:
        raise ValueError(f'File SHA-256 differs: {path}')
    return contents


def load_manifest(path, expected_sha256=None, assets_directory=None):
    contents = read_text_bytes(path)
    checksum = digest(contents)
    if expected_sha256 is not None and checksum != expected_sha256:
        raise ValueError('Input manifest changed')
    manifest = validate_manifest_schema(decode_json(contents))
    resolved_sources = set()
    for name in asset_names(manifest):
        record = manifest['files'][name]
        source = Path(record['host_path']) if assets_directory is None else Path(assets_directory) / name
        if assets_directory is None and not source.is_absolute():
            raise ValueError('host_path is not absolute on this host')
        resolved = source.resolve(strict=True)
        if resolved in resolved_sources:
            raise ValueError('Repeated physical input path')
        resolved_sources.add(resolved)
        read_verified_file(source, record)
    if read_text_bytes(path) != contents:
        raise ValueError('Input manifest changed during verification')
    return manifest, checksum


def load_phone():
    path = DIRECTORY / 'phone-readthrough.py'
    specification = importlib.util.spec_from_file_location('fuxi_wlan_phone_readthrough', path)
    phone = importlib.util.module_from_spec(specification)
    contents = path.read_bytes()
    exec(compile(contents, str(path), 'exec'), phone.__dict__)
    phone.loaded_source_sha256 = digest(contents)
    if phone.BLOCK != BLOCK_BYTES or phone.SIZE != ROOT_BYTES:
        raise ValueError('The read-through module has unexpected root geometry')
    return phone


def snapshot_sources(phone):
    paths = {name: DIRECTORY / name for name in SOURCE_NAMES}
    paths['hfs-journal-block-probe.py'] = DIRECTORY / 'stage-share/hfs-journal-block-probe.py'
    paths['verify-native-map.py'] = phone.ROOT / 'work/xiaomi13-ios-gpu-20261108/verify-native-map.py'
    sources = {}
    for name, path in paths.items():
        original = path.read_bytes()
        copied = original.replace(b'\r\n', b'\n') if name.endswith('.py') else original
        if not copied or len(copied) > MAX_TEXT_BYTES or b'\r' in copied or not copied.endswith(b'\n'):
            raise ValueError(f'Invalid source text or shell line endings: {path}')
        copied.decode('utf-8')
        sources[name] = {'path': str(path), 'original_sha256': digest(original),
                         'copied_sha256': digest(copied), 'contents': copied}
    if sources['phone-readthrough.py']['original_sha256'] != phone.loaded_source_sha256:
        raise ValueError('The loaded read-through module differs from its current source')
    return sources


def verify_sources(sources):
    for source in sources.values():
        if digest_file(source['path'], MAX_TEXT_BYTES) != source['original_sha256']:
            raise ValueError(f'Source changed: {source["path"]}')


def save_exclusive(path, contents):
    with Path(path).open('xb') as stream:
        stream.write(contents)


def read_checksums(path):
    result = {}
    for line in read_text_bytes(path).decode('ascii').splitlines():
        match = re.fullmatch(r'([a-f0-9]{64})  ((?:assets/)?[A-Za-z0-9_.-]+)', line)
        if not match or match[2] in result:
            raise ValueError('Invalid or duplicate checksum record')
        result[match[2]] = match[1]
    return result


def validate_share(share):
    share = Path(share)
    source_hashes = read_checksums(share / 'source-sha256.txt')
    if set(source_hashes) != set(SOURCE_NAMES):
        raise ValueError('Unexpected staged sources')
    for name, checksum in source_hashes.items():
        if digest(read_text_bytes(share / name)) != checksum:
            raise ValueError(f'Staged source changed: {name}')
    input_hashes = read_checksums(share / 'input-sha256.txt')
    if 'input-manifest.json' not in input_hashes:
        raise ValueError('The staged manifest hash is missing')
    manifest, checksum = load_manifest(share / 'input-manifest.json',
                                     input_hashes['input-manifest.json'], share / 'assets')
    expected_names = {'input-manifest.json'} | {f'assets/{name}' for name in asset_names(manifest)}
    if set(input_hashes) != expected_names:
        raise ValueError('Unexpected staged inputs')
    for name in asset_names(manifest):
        if input_hashes[f'assets/{name}'] != manifest['files'][name]['sha256']:
            raise ValueError('The staged input checksum list differs from the manifest')
    return manifest, checksum


def prepare_share(output, manifest_path, manifest, manifest_sha256, sources):
    load_manifest(manifest_path, manifest_sha256)
    verify_sources(sources)
    share = Path(output) / 'share'
    share.mkdir()
    (share / 'assets').mkdir()
    hashes = {}
    for name, source in sources.items():
        save_exclusive(share / name, source['contents'])
        hashes[name] = source['copied_sha256']
    contents = read_text_bytes(manifest_path)
    if digest(contents) != manifest_sha256:
        raise ValueError('Input manifest changed before copying')
    save_exclusive(share / 'input-manifest.json', contents)
    hashes['input-manifest.json'] = manifest_sha256
    for name in asset_names(manifest):
        record = manifest['files'][name]
        contents = read_verified_file(record['host_path'], record)
        save_exclusive(share / 'assets' / name, contents)
        hashes[f'assets/{name}'] = record['sha256']
    for filename, names in (
        ('source-sha256.txt', SOURCE_NAMES),
        ('input-sha256.txt', ('input-manifest.json',) + tuple(f'assets/{name}' for name in asset_names(manifest))),
    ):
        contents = ''.join(f'{hashes[name]}  {name}\n' for name in names).encode('ascii')
        save_exclusive(share / filename, contents)
        hashes[filename] = digest(contents)
    load_manifest(manifest_path, manifest_sha256)
    verify_sources(sources)
    verify_bundle(share, hashes)
    validate_share(share)
    return share, hashes


def verify_bundle(share, hashes):
    found = set()
    for path in Path(share).rglob('*'):
        if path.is_symlink() or is_reparse_point(path.lstat()):
            raise ValueError('A staged path is a symlink or reparse point')
        relative = path.relative_to(share).as_posix()
        if path.is_dir():
            if relative != 'assets':
                raise ValueError('Unexpected staged directory')
            continue
        found.add(relative)
        limit = MAX_FILE_BYTES if relative.startswith('assets/') else MAX_TEXT_BYTES
        if relative not in hashes or digest_file(path, limit) != hashes[relative]:
            raise ValueError(f'Staged bundle changed: {relative}')
    if found != set(hashes):
        raise ValueError('Staged bundle files are missing')


def is_reparse_point(metadata):
    return bool(getattr(metadata, 'st_file_attributes', 0) & getattr(stat, 'FILE_ATTRIBUTE_REPARSE_POINT', 0))


def checked_directory(root, create=False, path=WLAN_PATH):
    root = Path(root)
    metadata = root.lstat()
    if stat.S_ISLNK(metadata.st_mode) or is_reparse_point(metadata) or not stat.S_ISDIR(metadata.st_mode):
        raise ValueError('The guest root must be a real directory')
    current = root.resolve(strict=True)
    for component in path.parts[1:]:
        current = current / component
        try:
            metadata = current.lstat()
        except FileNotFoundError:
            if not create:
                return root.resolve(strict=True).joinpath(*path.parts[1:])
            current.mkdir(mode=0o755)
            metadata = current.lstat()
        if stat.S_ISLNK(metadata.st_mode) or is_reparse_point(metadata) or not stat.S_ISDIR(metadata.st_mode):
            raise ValueError(f'Target directory is a symlink or non-directory: {current}')
    return current


def check_target(target):
    try:
        metadata = Path(target).lstat()
    except FileNotFoundError:
        return
    if stat.S_ISLNK(metadata.st_mode) or is_reparse_point(metadata) or not stat.S_ISREG(metadata.st_mode):
        raise ValueError(f'Target is a symlink or non-regular file: {target}')


def installed_metadata(path):
    metadata = Path(path).stat()
    return (metadata.st_uid, metadata.st_gid, stat.S_IMODE(metadata.st_mode)) == (0, 0, 0o644)


def install_guest_assets(root, share):
    manifest, manifest_sha256 = validate_share(share)
    names = asset_names(manifest)
    destination = target_path(manifest)
    target_directory = checked_directory(root, path=destination)
    for name in names:
        check_target(target_directory / name)
    contents = {}
    for name in names:
        contents[name] = read_verified_file(Path(share) / 'assets' / name, manifest['files'][name])
    target_directory = checked_directory(root, create=True, path=destination)
    records = []
    for name in names:
        record = manifest['files'][name]
        target = target_directory / name
        checked_directory(root, path=destination)
        check_target(target)
        changed = True
        if target.exists() and target.stat().st_size == record['bytes'] and installed_metadata(target):
            changed = digest_file(target, MAX_FILE_BYTES) != record['sha256']
        if changed:
            descriptor, temporary_name = tempfile.mkstemp(prefix='.fuxi-wlan-', dir=target_directory)
            temporary = Path(temporary_name)
            try:
                with os.fdopen(descriptor, 'wb') as stream:
                    stream.write(contents[name])
                    stream.flush()
                    os.fsync(stream.fileno())
                os.chown(temporary, 0, 0)
                os.chmod(temporary, 0o644)
                checked_directory(root, path=destination)
                check_target(target)
                temporary.replace(target)
            finally:
                if temporary.exists():
                    temporary.unlink()
        read_verified_file(target, record)
        if not installed_metadata(target):
            raise ValueError(f'Installed ownership or permissions differ: {name}')
        records.append({'runtime_path': str(destination / name), 'sha256': record['sha256'],
                        'bytes': record['bytes'], 'changed': changed})
    validate_share(share)
    if digest(read_text_bytes(Path(share) / 'input-manifest.json')) != manifest_sha256:
        raise ValueError('The staged manifest changed during installation')
    return records


def validate_output(path, directory=DIRECTORY):
    path = Path(path).absolute()
    if path.exists() or path.is_symlink():
        raise ValueError('The output already exists')
    output = path.resolve()
    if output.parent != Path(directory).resolve() or not re.fullmatch(r'run-(?:wlan|bt)-[a-z0-9]+(?:-[a-z0-9]+)*', output.name):
        raise ValueError('Use a new statusbar-fix/run-wlan-[safe-name] or run-bt-[safe-name] directory')
    if len(output.name) > 110:
        raise ValueError('The output name is too long')
    return output


def validate_delta(output):
    output = Path(output)
    list_bytes = read_text_bytes(output / 'sb-list.txt')
    lines = list_bytes.decode('ascii').splitlines()
    if not 1 <= len(lines) <= 64:
        raise ValueError('Unexpected changed-block count')
    records = []
    previous = -1
    for line in lines:
        match = re.fullmatch(r'(0|[1-9][0-9]*) ([a-f0-9]{64}) ([a-f0-9]{64})', line)
        if not match:
            raise ValueError('Invalid changed-block record')
        index, before, after = int(match[1]), match[2], match[3]
        if not previous < index < ROOT_BYTES // BLOCK_BYTES or before == after:
            raise ValueError('Changed-block indices or hashes are invalid')
        previous = index
        records.append({'index': index, 'before_sha256': before, 'after_sha256': after})
    undo = ''.join(f'{record["index"]} {record["after_sha256"]} {record["before_sha256"]}\n' for record in records)
    if read_text_bytes(output / 'sb-undo-list.txt') != undo.encode('ascii'):
        raise ValueError('The undo list differs')
    expected_length = len(records) * BLOCK_BYTES
    for name, field in (('sb-old.bin', 'before_sha256'), ('sb-new.bin', 'after_sha256')):
        with (output / name).open('rb') as stream:
            before_stat = os.fstat(stream.fileno())
            if not stat.S_ISREG(before_stat.st_mode) or before_stat.st_size != expected_length:
                raise ValueError('Changed-block binary length or type differs')
            for record in records:
                if digest(stream.read(BLOCK_BYTES)) != record[field]:
                    raise ValueError(f'Changed-block binary hash differs: {name}')
            if stat_identity(os.fstat(stream.fileno())) != stat_identity(before_stat) or stat_identity((output / name).stat()) != stat_identity(before_stat):
                raise ValueError('Changed-block binary changed during verification')
    if read_text_bytes(output / 'sb-list.txt') != list_bytes:
        raise ValueError('The changed-block list changed during verification')
    return records


def guest_records(log, manifest):
    text = log.decode('utf-8', errors='strict')
    lines = text.splitlines()
    for token in ('FAST_WLAN_STAGE_SUCCESS', 'GUEST_OK'):
        if token not in lines:
            raise ValueError(f'The guest completion token is missing: {token}')
    matches = [match[1] for line in lines
               if (match := re.fullmatch(r'WLAN_STAGE_RESULT (\[.*\])', line))]
    if len(matches) != 1:
        raise ValueError('The guest file report is missing or repeated')
    records = decode_json(matches[0].encode('utf-8'))
    if not isinstance(records, list) or len(records) != len(asset_names(manifest)):
        raise ValueError('Invalid guest file report')
    for name, record in zip(asset_names(manifest), records):
        expected = manifest['files'][name]
        if not isinstance(record, dict) or set(record) != {'runtime_path', 'sha256', 'bytes', 'changed'}:
            raise ValueError('Invalid guest file record')
        if record['runtime_path'] != str(target_path(manifest) / name) or record['sha256'] != expected['sha256']:
            raise ValueError('The guest wrote unexpected file paths or contents')
        if type(record['bytes']) is not int or record['bytes'] != expected['bytes'] or type(record['changed']) is not bool:
            raise ValueError('The guest file size or change flag differs')
    return records


def verify_mapping(phone, serial, output, filename):
    verifier = phone.ROOT / 'work/xiaomi13-ios-gpu-20261108/verify-native-map.py'
    code = (
        'import contextlib, json, runpy, sys\n'
        'with contextlib.redirect_stdout(sys.stderr):\n'
        '    verified = runpy.run_path(sys.argv[1])\n'
        'print(json.dumps({"serial": verified["SERIAL"], "items": verified["items"], '
        '"dm": verified["dm"], "pin": verified["pin"]}))\n'
    )
    contents = phone.run([sys.executable, '-B', '-X', 'utf8', '-c', code, verifier],
                         cwd=phone.ROOT / 'work/Codex-Xiaomi13-GPU-20261109', timeout=120)
    mapping = decode_json(contents)
    candidate = mapping['items'][0]
    if mapping['serial'] != serial or (candidate['path'], candidate['size'], candidate['inode']) != (ROOT_PATH, ROOT_BYTES, ROOT_INODE):
        raise ValueError('Fresh native-map verification identified a different root')
    save_exclusive(Path(output) / filename, contents)
    device = phone.adb(serial, f'readlink -f {phone.MAPPER}').decode('ascii').strip()
    if not re.fullmatch(r'/dev/block/dm-[0-9]+', device):
        raise ValueError('Unexpected native mapper device')
    if int(phone.adb(serial, f'blockdev --getsize64 {phone.MAPPER}')) != ROOT_BYTES:
        raise ValueError('The mapped root size differs')
    mounts = phone.adb(serial, 'cat /proc/mounts').decode('utf-8').splitlines()
    if any(line.split()[0] in (device, phone.MAPPER) for line in mounts if line.split()):
        raise ValueError('The native root is mounted on the phone')
    return candidate, device


def check_inputs(manifest_path, manifest_sha256, sources, share, bundle_hashes):
    load_manifest(manifest_path, manifest_sha256)
    verify_sources(sources)
    verify_bundle(share, bundle_hashes)


def save_json(path, value):
    save_exclusive(path, (json.dumps(value, indent=2, ensure_ascii=False) + '\n').encode('utf-8'))


def stage(arguments):
    output = validate_output(arguments.output)
    manifest_path = Path(arguments.manifest).resolve(strict=True)
    manifest, manifest_sha256 = load_manifest(manifest_path)
    if output.name.startswith('run-bt-') != is_bt(manifest):
        raise ValueError('Bluetooth manifests use run-bt-*, WLAN manifests use run-wlan-*')
    if arguments.serial != SERIAL or not 1024 <= arguments.port <= 65535:
        raise ValueError('Use the verifier serial and an unprivileged guest port')
    phone = load_phone()
    sources = snapshot_sources(phone)
    output.mkdir()
    share, bundle_hashes = prepare_share(output, manifest_path, manifest, manifest_sha256, sources)
    source_receipt = {name: {key: value for key, value in source.items() if key != 'contents'}
                      for name, source in sources.items()}
    save_json(output / 'input-receipt.json', {
        'manifest_path': str(manifest_path), 'manifest_sha256': manifest_sha256,
        'sources': source_receipt, 'bundle_sha256': bundle_hashes,
    })
    mapped, remote_created = False, False
    remote = f'/data/local/tmp/codex-{output.name}-{secrets.token_hex(8)}'
    records, installed = [], []
    with phone.phone_locks(output.name):
        try:
            check_inputs(manifest_path, manifest_sha256, sources, share, bundle_hashes)
            if phone.run(['adb', '-s', arguments.serial, 'shell', 'getprop', 'sys.boot_completed']).strip() != b'1':
                raise RuntimeError('Android is not ready')
            phone.say('WLAN_PHONE_START', output.name)
            phone.say(phone.adb(arguments.serial, f'{phone.MANAGER} map').decode().strip())
            mapped = True
            initial_mapping = verify_mapping(phone, arguments.serial, output, 'mapping-before.json')
            blocks = phone.PhoneBlocks(output / 'read-cache', lambda index: phone.adb(
                arguments.serial, f'dd if={phone.MAPPER} bs={phone.BLOCK} skip={index} count=1 2>/dev/null'))
            with phone.nbd(blocks) as (address, failures):
                overlay = output / 'overlay.qcow2'
                phone.run([phone.QEMU_IMG, 'create', '-q', '-f', 'qcow2', '-b', address, '-F', 'raw', overlay], timeout=120)
                check_inputs(manifest_path, manifest_sha256, sources, share, bundle_hashes)
                phone.say('WLAN_GUEST_READ_THROUGH')
                with (output / 'guest.log').open('xb') as logfile:
                    subprocess.run([sys.executable, '-B', '-X', 'utf8', share / 'sb-guest-run.py',
                                    overlay, share, 'stage-wlan-assets-guest.sh', 'FAST_WLAN_STAGE_SUCCESS',
                                    '-', str(arguments.port)], stdout=logfile, stderr=subprocess.STDOUT,
                                   check=True, timeout=1500)
                installed = guest_records((output / 'guest.log').read_bytes(), manifest)
                check_inputs(manifest_path, manifest_sha256, sources, share, bundle_hashes)
                if failures:
                    raise IOError(f'Phone reads failed: {failures[:3]}')
                if any(record['changed'] for record in installed):
                    phone.extract(overlay, blocks, output)
                    records = validate_delta(output)
                if failures:
                    raise IOError(f'Phone reads failed: {failures[:3]}')
            save_json(output / 'delta.json', records)
            check_inputs(manifest_path, manifest_sha256, sources, share, bundle_hashes)
            if verify_mapping(phone, arguments.serial, output, 'mapping-before-write.json') != initial_mapping:
                raise ValueError('The native root mapping changed during staging')
            if records:
                validate_delta(output)
                remote_names = ('sb-new.bin', 'sb-list.txt', 'sb-blocks-write.sh')
                remote_sources = (output / 'sb-new.bin', output / 'sb-list.txt', share / 'sb-blocks-write.sh')
                remote_hashes = {f'{remote}/{name}': digest_file(path)
                                 for name, path in zip(remote_names, remote_sources)}
                phone.adb(arguments.serial, f'mkdir {remote}')
                remote_created = True
                phone.adb(arguments.serial, f'chmod 0777 {remote}')
                for name, path in zip(remote_names, remote_sources):
                    phone.run(['adb', '-s', arguments.serial, 'push', path, f'{remote}/{name}'], timeout=120)
                phone.adb(arguments.serial, f'chmod 0700 {remote}')
                remote_text = phone.adb(arguments.serial, 'sha256sum ' + ' '.join(remote_hashes)).decode('ascii')
                rows = [line.split() for line in remote_text.splitlines()]
                if len(rows) != 3 or any(len(row) != 2 for row in rows):
                    raise ValueError('Invalid remote hash response')
                if {row[1]: row[0] for row in rows} != remote_hashes:
                    raise ValueError('Remote writer, list, or data hash differs')
                check_inputs(manifest_path, manifest_sha256, sources, share, bundle_hashes)
                if validate_delta(output) != records:
                    raise ValueError('The local delta changed before writing')
                phone.say('WLAN_WRITE_CHANGED_BLOCKS', len(records))
                try:
                    result = phone.adb(arguments.serial, f'sh {remote}/sb-blocks-write.sh {remote}/sb-list.txt {remote}/sb-new.bin')
                except subprocess.CalledProcessError as failure:
                    save_exclusive(output / 'write.log', (failure.stdout or b'') + (failure.stderr or b''))
                    raise
                save_exclusive(output / 'write.log', result)
                text = result.decode('utf-8')
                for token in (f'PRE_OK {len(records)}', f'WRITE_OK {len(records)}'):
                    if not re.search(rf'^{token}\r?$', text, re.MULTILINE):
                        raise RuntimeError('The writer precondition or readback did not finish')
                check_inputs(manifest_path, manifest_sha256, sources, share, bundle_hashes)
                validate_delta(output)
        finally:
            try:
                if remote_created:
                    phone.adb(arguments.serial, f'rm -f {remote}/sb-new.bin {remote}/sb-list.txt {remote}/sb-blocks-write.sh && rmdir {remote}')
            finally:
                if mapped:
                    phone.say(phone.adb(arguments.serial, f'sync && {phone.MANAGER} unmap').decode().strip())
    receipt = {
        'state': 'written' if records else 'unchanged', 'serial': arguments.serial,
        'root_path': ROOT_PATH, 'root_bytes': ROOT_BYTES, 'root_inode': ROOT_INODE,
        'manifest_sha256': manifest_sha256, 'input_receipt_sha256': digest((output / 'input-receipt.json').read_bytes()),
        'files': installed, 'changed_blocks': records, 'phone_read_blocks': blocks.reads,
        'delta_sha256': {name: digest_file(output / name) for name in
                         ('sb-old.bin', 'sb-new.bin', 'sb-list.txt', 'sb-undo-list.txt') if records},
    }
    save_json(output / 'receipt.json', receipt)
    phone.say('WLAN_ASSETS_STAGE_DONE', receipt['state'], 'phone read', blocks.reads * 4, 'MiB')
    return receipt


def main():
    parser = argparse.ArgumentParser(description='Stage the five verified WLAN assets through a fresh phone read-through overlay and write only verified changed blocks.')
    parser.add_argument('--manifest')
    parser.add_argument('--output')
    parser.add_argument('--serial', default=SERIAL, choices=[SERIAL])
    parser.add_argument('--port', type=int, default=34616)
    guest = parser.add_mutually_exclusive_group()
    guest.add_argument('--guest-check', action='store_true', help=argparse.SUPPRESS)
    guest.add_argument('--guest-stage', action='store_true', help=argparse.SUPPRESS)
    arguments = parser.parse_args()
    if arguments.guest_check:
        validate_share(Path('/mnt/share'))
    elif arguments.guest_stage:
        records = install_guest_assets(Path('/dst'), Path('/mnt/share'))
        print('WLAN_STAGE_RESULT ' + json.dumps(records, separators=(',', ':')), flush=True)
    else:
        if not arguments.manifest or not arguments.output:
            parser.error('--manifest and --output are required')
        stage(arguments)


if __name__ == '__main__':
    main()
