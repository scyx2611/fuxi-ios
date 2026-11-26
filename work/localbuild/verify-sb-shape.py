import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import struct


def require(condition, message):
    if not condition:
        raise ValueError(message)


def inspect(data, *, audio_bundle=False, install_name='/usr/local/lib/fuxi/libfuxi-sb.dylib', forbid_interpose=False):
    require(len(data) >= 32, 'Truncated Mach-O header')
    magic, cpu, subtype, kind, commands, commands_size = struct.unpack_from('<6I', data)
    binary_type, type_name = (8, 'MH_BUNDLE') if audio_bundle else (6, 'MH_DYLIB')
    require((magic, cpu, subtype, kind) == (0xfeedfacf, 0x100000c, 0, binary_type),
            f'Requires plain arm64 {type_name}')
    commands_end = 32 + commands_size
    require(commands_end <= len(data) and commands <= commands_size // 8, 'Invalid load command bounds')
    offset = 32
    signature = None
    minimum = None
    identifier = None
    dependencies = []
    interpose_sections = 0
    for _ in range(commands):
        command, size = struct.unpack_from('<II', data, offset)
        require(size >= 8 and size % 8 == 0 and offset + size <= commands_end, 'Invalid load command')
        if command == 0x19:
            require(size >= 72, 'Truncated segment')
            segment = data[offset + 8:offset + 24].split(b'\0', 1)[0]
            address, memory_size, file_offset, file_size = struct.unpack_from('<4Q', data, offset + 24)
            require(address % 4096 == 0 and file_offset % 4096 == 0 and
                    (memory_size % 4096 == 0 or segment == b'__LINKEDIT'),
                    'Segment cannot map on 4K pages')
            require(file_offset + file_size <= len(data) and file_size <= memory_size, 'Invalid segment bounds')
            nsects = struct.unpack_from('<I', data, offset + 64)[0]
            require(size >= 72 + nsects * 80, 'Truncated segment sections')
            for sec_index in range(nsects):
                sec_offset = offset + 72 + sec_index * 80
                sectname = data[sec_offset:sec_offset + 16].split(b'\0', 1)[0]
                segname = data[sec_offset + 16:sec_offset + 32].split(b'\0', 1)[0]
                sec_flags = struct.unpack_from('<I', data, sec_offset + 64)[0]
                if sectname == b'__interpose' or (sec_flags & 0xff) == 0x0e:
                    interpose_sections += 1
                    if forbid_interpose:
                        seg_label = segname.decode('ascii', 'replace')
                        sec_label = sectname.decode('ascii', 'replace')
                        raise ValueError(
                            f'Forbidden ({seg_label},{sec_label}) interpose section (flags=0x{sec_flags:x})'
                        )
        elif command == 0x32:
            require(size >= 24 and minimum is None, 'Invalid build version')
            platform, minimum, sdk = struct.unpack_from('<3I', data, offset + 8)
            require(platform == 2 and minimum == 0x100000, 'Requires iOS 16.0 deployment')
        elif command == 0x1d:
            require(size == 16 and signature is None, 'Invalid code signature command')
            signature = struct.unpack_from('<II', data, offset + 8)
        elif command in (0xc, 0xd, 0x80000018, 0x8000001f):
            require(size >= 24, 'Truncated dylib command')
            name_offset = struct.unpack_from('<I', data, offset + 8)[0]
            require(24 <= name_offset < size, 'Invalid dylib name offset')
            raw_name = data[offset + name_offset:offset + size]
            require(b'\0' in raw_name, 'Unterminated dylib name')
            name = raw_name.split(b'\0', 1)[0].decode('utf-8')
            if command == 0xd:
                require(identifier is None, 'Duplicate dylib identifier')
                identifier = name
            else:
                dependencies.append(name)
        offset += size
    require(offset == commands_end and minimum is not None and signature is not None, 'Missing load commands')
    expected_name = None if audio_bundle else install_name
    require(identifier == expected_name, 'Unexpected dylib identifier')
    expected_dependencies = {
        '/usr/lib/libSystem.B.dylib',
        '/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation',
    }
    normalized = {name.replace('/IOKit.framework/Versions/A/', '/IOKit.framework/') for name in dependencies}
    # 2026-11-20 起 FuxiAudio 也讀 ADT /chosen fuxi,hal-haptic，同樣連 IOKit。
    if not audio_bundle or b'fuxi,hal-haptic\0' in data:
        expected_dependencies.add('/System/Library/Frameworks/IOKit.framework/IOKit')
    if not audio_bundle:
        expected_dependencies.add('/usr/lib/libobjc.A.dylib')
    require(normalized == expected_dependencies, 'Unexpected dependencies')
    signature_offset, signature_size = signature
    require(signature_offset >= commands_end and signature_offset + signature_size <= len(data),
            'Code signature outside file')
    blob = data[signature_offset:signature_offset + signature_size]
    require(len(blob) >= 12, 'Truncated signature')
    blob_magic, blob_size, blob_count = struct.unpack_from('>3I', blob)
    require(blob_magic == 0xfade0cc0 and 12 <= blob_size <= len(blob), 'Invalid signature superblob')
    require(blob_count <= (blob_size - 12) // 8, 'Invalid signature slot count')
    slots = {}
    for index in range(blob_count):
        slot, start = struct.unpack_from('>2I', blob, 12 + index * 8)
        require(start >= 12 + 8 * blob_count and start + 8 <= blob_size and slot not in slots,
                'Invalid signature slot')
        leaf_magic, length = struct.unpack_from('>2I', blob, start)
        require(length >= 8 and start + length <= blob_size, 'Signature slot outside superblob')
        slots[slot] = (leaf_magic, blob[start:start + length])
    directories = []
    for slot, (leaf_magic, directory) in slots.items():
        if leaf_magic != 0xfade0c02:
            continue
        require(slot == 0 or 0x1000 <= slot <= 0x1005, 'Unexpected code directory slot')
        require(len(directory) >= 44, 'Truncated code directory')
        version, flags, hash_offset, name_offset, special, code_slots, limit = struct.unpack_from('>7I', directory, 8)
        hash_size, hash_type, platform, page_shift = struct.unpack_from('4B', directory, 36)
        if version >= 0x20300:
            require(len(directory) >= 64, 'Truncated 64-bit code limit')
            limit = struct.unpack_from('>Q', directory, 56)[0] or limit
        require(flags & 2 and hash_type == 2 and hash_size == 32 and page_shift == 12,
                'Requires ad hoc SHA256 signature with 4K code pages')
        require(limit == signature_offset and code_slots == (limit + 4095) // 4096, 'Invalid code page bounds')
        require(special <= hash_offset // hash_size and hash_offset + code_slots * hash_size <= len(directory),
                'Invalid code hash table')
        require(0 < name_offset < hash_offset and b'\0' in directory[name_offset:hash_offset],
                'Invalid signature identifier')
        for index in range(code_slots):
            actual = hashlib.sha256(data[index * 4096:min((index + 1) * 4096, limit)]).digest()
            expected = directory[hash_offset + index * hash_size:hash_offset + (index + 1) * hash_size]
            require(actual == expected, f'Code page {index} hash mismatch')
        for index in range(1, special + 1):
            actual = hashlib.sha256(slots[index][1]).digest() if index in slots else bytes(hash_size)
            expected = directory[hash_offset - index * hash_size:hash_offset - (index - 1) * hash_size]
            require(actual == expected, f'Special slot {index} hash mismatch')
        directories.append({
            'slot': slot, 'flags': flags, 'code_pages_verified': code_slots,
            'cdhash': hashlib.sha256(directory).hexdigest()[:40],
        })
    require(directories, 'No verified code directory')
    return {
        'architecture': 'arm64', 'minimum_ios': '16.0', 'sdk': f'{sdk >> 16}.{sdk >> 8 & 255}',
        'install_name': identifier, 'dependencies': sorted(dependencies), 'interpose_sections': interpose_sections, 'signatures': directories,
        'sha256': hashlib.sha256(data).hexdigest(),
        'verification': 'Mach-O structure and ad hoc page hashes; no Apple certificate trust claim',
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('path', type=Path)
    parser.add_argument('--audio-bundle', action='store_true', help='verify an arm64 FuxiAudio.driver')
    parser.add_argument('--install-name', default='/usr/local/lib/fuxi/libfuxi-sb.dylib',
                        help='expected LC_ID_DYLIB install name')
    parser.add_argument('--forbid-interpose', action='store_true',
                        help='reject any __interpose or S_INTERPOSING (0x0e) Mach-O section')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.audio_bundle and args.install_name != '/usr/local/lib/fuxi/libfuxi-sb.dylib':
        parser.error('--install-name cannot be used with --audio-bundle')
    binary = args.path
    if args.audio_bundle:
        binary = args.path / 'FuxiAudio'
        info_bytes = (args.path / 'Info.plist').read_bytes()
        info = plistlib.loads(info_bytes)
        require(info.get('CFBundleExecutable') == binary.name and info.get('CFBundlePackageType') == 'BNDL'
                and info.get('CFBundleIdentifier') == 'org.iosfuxi.audio', 'Unexpected FuxiAudio bundle metadata')
        factory, = info.get('CFPlugInTypes', {}).get('443ABAB8-E7B3-491A-B985-BEB9187030DB', [])
        require(info.get('CFPlugInFactories', {}).get(factory) == 'FuxiAudio_Create', 'Missing FuxiAudio factory')
    data = binary.read_bytes()
    result = inspect(data, audio_bundle=args.audio_bundle, install_name=args.install_name,
                     forbid_interpose=args.forbid_interpose)
    if args.audio_bundle:
        result.update(factory='FuxiAudio_Create', info_sha256=hashlib.sha256(info_bytes).hexdigest())
    if args.self_test:
        for position in (4, 12, 4096):
            changed = bytearray(data)
            changed[position] ^= 1
            try:
                inspect(changed, audio_bundle=args.audio_bundle, install_name=args.install_name,
                        forbid_interpose=args.forbid_interpose)
            except (ValueError, struct.error):
                continue
            raise ValueError(f'Corruption at {position} was accepted')
        result['negative_checks'] = 3
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
