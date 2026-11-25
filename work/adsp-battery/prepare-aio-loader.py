#!/usr/bin/env python3
"""Build unified All-In-One iOS 16 loader (B-020 v2 + M7 Wi-Fi join speed + M9/M11 GPU kernel layer)
without modifying work/adsp-battery/prepare-wlan-w18-loader.py or committed B-020 tools.

Loads committed HEAD of work/adsp-battery/prepare-wlan-w18-loader.py and adds:
  - Full B-020 v2 merge properties & assets (/baseband, .sensor_pack, audio mic cfg/dsp, camera tabs/isp, atx-fix)
  - M7 Wi-Fi UX bitmask extension up to 8191 (0x1fff: active scan reuse + bounded 110ms default dwell)
  - M9/M11 GPU kernel parameters (--gpu-ncimport, default 606385736; optional --gpu-hyb8-ovsh)
  - Optional ADT /chosen fuxi,sweetnull (--adt-sweetnull) to keep the 607-byte boot command line within budget
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import types

WORKSPACE = Path(__file__).resolve().parents[2]
STORAGE = WORKSPACE / 'work/Codex-Xiaomi13-GPU-20261109'
ARCHIVE_STORAGE = Path(r'E:\fuxi_Backup-archive\codex-artifacts-archive-20261124')
GENERATOR = 'work/adsp-battery/prepare-wlan-w18-loader.py'
SENSOR_PACK_BASE = 0xE8800000
SENSOR_PACK_MAX = 0x200000


def _resolve_input_dir(name: str) -> Path:
    p = STORAGE / name
    if p.is_dir():
        return p
    a = ARCHIVE_STORAGE / name
    if a.is_dir():
        return a
    return p

AP_LOCAL_OBJECT = '13b0f3dda4ff90b9e591c2fd4357b3826c66e79613095336ddf1a36264e701d9'
AP_NW_AP_SHA256 = 'f8aa2bdd28e8abe45b0457dbca7f48d9e44d7ca07bd65d8f9777d2f9982ecfdf'

GPU_CLIENT_PROFILES = {
    'core': (
        'Maps,com.apple.MapKit.SnapshotService,Weather,Stocks,'
        'MobileNotes,MobileTimer,Measure,com.apple.WebKit.GPU'
    ),
    'maps-extended': (
        'Maps,com.apple.MapKit.SnapshotService,Weather,Stocks,'
        'MobileNotes,MobileTimer,Measure,com.apple.WebKit.GPU,GeneralMapsWidget'
    ),
    'minimal': 'Maps,com.apple.MapKit.SnapshotService,Weather',
}

FORBIDDEN_GPU_CLIENT_LOWER = frozenset({
    'springboard',
    'mediaserverd',
    'backboardd',
    'com.apple.springboard',
    'com.apple.mediaserverd',
    'com.apple.backboardd',
})


def _require_gpu(cond: bool, msg: str) -> None:
    if not cond:
        raise ValueError(msg)


def validate_gpu_clients(value: str | None) -> str | None:
    """Validate Class 1 foreground Metal allowlist against U-026 and 254B ADT limit."""
    if value is None or (isinstance(value, str) and value.lower() == 'none'):
        return None
    _require_gpu(isinstance(value, str), '--gpu-clients must be a string')
    _require_gpu('\0' not in value, '--gpu-clients must not contain NUL')
    raw_bytes = value.encode('ascii')
    _require_gpu(1 <= len(raw_bytes) <= 254, '--gpu-clients must be 1..254 ASCII bytes (+ NUL <= 255 < 256)')
    tokens = [tok.strip() for tok in value.split(',')]
    _require_gpu(bool(tokens) and all(tokens), '--gpu-clients contains empty process token')
    for tok in tokens:
        _require_gpu(
            bool(re.fullmatch(r'[A-Za-z0-9_.-]{1,63}', tok)),
            f'--gpu-clients contains invalid or >63B process token: {tok!r}',
        )
        _require_gpu(tok not in ('.', '..'), f'--gpu-clients cannot include dot path token: {tok!r}')
        low = tok.lower()
        _require_gpu(
            'poster' not in low and low not in FORBIDDEN_GPU_CLIENT_LOWER,
            f'--gpu-clients cannot include U-026 forbidden or compositor process (case-insensitive): {tok}',
        )
    seen_lower: set[str] = set()
    for tok in tokens:
        low = tok.lower()
        _require_gpu(low not in seen_lower, f'--gpu-clients contains duplicate process token: {tok}')
        seen_lower.add(low)
    normalized = ','.join(tokens)
    _require_gpu(1 <= len(normalized.encode('ascii')) <= 254, '--gpu-clients normalized length exceeds 254 ASCII bytes')
    return normalized


def resolve_gpu_clients(value: str | None) -> str | None:
    if value is None or (isinstance(value, str) and value.lower() == 'none'):
        return None
    _require_gpu(isinstance(value, str), '--gpu-clients must be a string')
    raw = GPU_CLIENT_PROFILES.get(value, value)
    return validate_gpu_clients(raw)


def verify_adt_u026(
    adt_bytes: bytes,
    adt_module,
    expected_gpu_clients: str | None = None,
    camera_dylib: bool = False,
) -> None:
    """Post-build binary ADT verification for Plan 1 + U-026 + --gpu-clients."""
    _require_gpu(len(adt_bytes) <= 0x20000, f'generated private.adt size {len(adt_bytes)} exceeds 128 KiB limit')
    out_tree, consumed = adt_module.parse(adt_bytes)
    _require_gpu(consumed == len(adt_bytes) and adt_module.serialize(out_tree) == adt_bytes,
                 'generated private.adt round-trip mismatch')
    chosen_nodes = [n for n in out_tree.children if n.name == 'chosen']
    _require_gpu(len(chosen_nodes) == 1, 'generated private.adt needs one /chosen node')
    out_chosen = chosen_nodes[0]
    _require_gpu(out_chosen.prop('fuxi,atx-fix') == struct.pack('<I', 1),
                 'U-026 violation: generated private.adt missing fuxi,atx-fix=1')
    raw_inject = out_chosen.prop('fuxi,inject')
    _require_gpu(raw_inject is not None and len(raw_inject) == 4,
                 'U-026 violation: generated private.adt missing fuxi,inject')
    inj_val = struct.unpack('<I', raw_inject)[0]
    _require_gpu((inj_val & 2) != 0,
                 f'U-026 violation: generated private.adt fuxi,inject={inj_val} missing SpringBoard bit 2')
    if camera_dylib:
        _require_gpu(out_chosen.prop('fuxi,capsrc-stub') is None and b'fuxi,capsrc-stub' not in adt_bytes,
                     'generated private.adt must not carry fuxi,capsrc-stub when --camera-dylib is enabled')
    raw_gc = out_chosen.prop('fuxi,gpu-clients')
    if expected_gpu_clients is not None:
        _require_gpu(
            raw_gc is not None and 2 <= len(raw_gc) <= 255 and raw_gc.endswith(b'\0') and b'\0' not in raw_gc[:-1],
            'generated private.adt fuxi,gpu-clients invalid NUL/length encoding',
        )
        decoded_gc = raw_gc[:-1].decode('ascii')
        _require_gpu(
            validate_gpu_clients(decoded_gc) == expected_gpu_clients,
            f'generated private.adt fuxi,gpu-clients mismatch: {decoded_gc!r} != {expected_gpu_clients!r}',
        )
    else:
        _require_gpu(raw_gc is None, 'generated private.adt unexpectedly carries fuxi,gpu-clients')

SELFTEST_PRESETS = {
    'selftest6': {
        'tap_delay': 55,
        'tap_script': (
            'k0;w2500;s540,2390,540,999,99;w2000;s950,50,950,600,200;w2000;'
            't435,1453;w2500;t435,1453;w1500;t187,1670;w6000;t540,2050;w5000;'
            't420,1600;w4500;t540,2050;w5000;t660,1600;w4500;t540,2050;w5000;'
            't540,1600;w3500;t380,1840;w4000;t540,1840;w4000;t964,2129;w4500;'
            't540,2050;w5000;t964,2129;w4000'
        ),
        'description': 'Legacy selftest6 sequence preserved for backward compatibility',
    },
    'selftest7': {
        'tap_delay': 55,
        'tap_script': (
            'k0;w2500;s540,2390,540,999,99;w2000;s950,50,950,600,200;w2000;'
            't435,1453;w2000;t435,1453;w1500;s540,2350,540,1200,200;w1500;'
            't187,1670;w6000;t540,2075;w4500;t420,1600;w4000;t540,2075;w4500;'
            't660,1600;w4000;t540,2075;w4500;t540,1600;w3500;t372,1837;w4000;'
            't540,2075;w4000;t540,2075;w3500;t704,1837;w4000;t985,2075;w4500;'
            't540,2075;w4500;t985,2075;w5000'
        ),
        'description': (
            'M8 immediate torch toggle -> close CC -> Camera 1x/0.5x/3x stills -> '
            'video start/stop (540,2075) -> shifted photo mode (704,1837) -> '
            'front flip/still (985,2075) -> rear preview'
        ),
    },
    'selftest7b': {
        'tap_delay': 55,
        'tap_script': (
            'k0;w2500;s540,2390,540,999,99;w2000;s950,50,950,600,200;w2000;'
            't435,1453;w2000;t435,1453;w1500;'
            't187,1670;w6000;t540,2075;w4500;t420,1600;w4000;t540,2075;w4500;'
            't660,1600;w4000;t540,2075;w4500;t540,1600;w3500;t372,1837;w4000;'
            't540,2075;w4000;t540,2075;w3500;t704,1837;w4000;t985,2075;w4500;'
            't540,2075;w4500;t985,2075;w5000'
        ),
        'description': (
            'M8 immediate torch toggle -> launch Camera directly from open CC (187,1670) -> '
            'Camera 1x/0.5x/3x stills -> video start/stop (540,2075) -> shifted photo mode (704,1837) -> '
            'front flip/still (985,2075) -> rear preview'
        ),
    },
    'selftest8': {
        'tap_delay': 55,
        'tap_script': (
            'k0;w2500;s540,2390,540,999,99;w2000;s950,50,950,600,200;w2000;'
            't435,1453;w2000;t435,1453;w1500;'
            't187,1670;w6000;t540,2075;w4500;t420,1600;w4000;t540,2075;w4500;'
            't660,1600;w4000;t540,2075;w4500;t964,2129;w4500;t540,2075;w4500;'
            't964,2129;w4000;t372,1837;w4000;t540,2075;w4000;t540,2075;w4000;'
            't704,1837;w5000'
        ),
        'description': (
            'M8 immediate torch toggle -> launch Camera from CC (187,1670) -> '
            'all stills first: 1x/0.5x/3x -> front flip (964,2129)/still -> rear flip (964,2129) -> '
            'video mode (372,1837) start/stop (540,2075) -> photo mode (704,1837) unlocked rear preview'
        ),
    },
}


def _has_cli_flag(tokens: list[str], flag: str) -> bool:
    prefix = flag + '='
    return any(t == flag or t.startswith(prefix) for t in tokens)


def _apply_selftest_preset(rest: list[str], preset_name: str | None) -> tuple[list[str], dict | None]:
    if not preset_name:
        return rest, None
    if preset_name not in SELFTEST_PRESETS:
        raise SystemExit(f'unknown --selftest-preset: {preset_name!r}')
    spec = SELFTEST_PRESETS[preset_name]
    tap_script = spec['tap_script']
    raw = tap_script.encode('ascii')
    if not (1 <= len(raw) < 400):
        raise SystemExit(f'selftest preset {preset_name} tap_script length {len(raw)} out of range 1..399')
    out = list(rest)
    injected_delay = False
    injected_script = False
    if not _has_cli_flag(out, '--tap-delay'):
        out.extend(['--tap-delay', str(spec['tap_delay'])])
        injected_delay = True
    if not _has_cli_flag(out, '--tap-script'):
        out.extend(['--tap-script', tap_script])
        injected_script = True
    return out, {
        'name': preset_name,
        'tap_delay': spec['tap_delay'],
        'tap_script_bytes': len(raw),
        'tap_script_sha256': hashlib.sha256(raw).hexdigest(),
        'injected_tap_delay': injected_delay,
        'injected_tap_script': injected_script,
    }

STAGE_SENSOR_PACK_C = """
static const char *stage_sensor_pack(uint64_t rd_start, uint64_t rd_end,
    uint64_t phys_base, uint64_t mem_size)
{
 extern const unsigned char _head[], _end[];
 extern const uint8_t _sensor_pack_start[], _sensor_pack_end[];
 uintptr_t start = (uintptr_t)_sensor_pack_start;
 uintptr_t end = (uintptr_t)_sensor_pack_end;
 uint64_t n = end - start;
 if (!n) return 0;
 if (n > SENSOR_PACK_MAX) return "sensor pack exceeds 2 MiB window";
 if (((uintptr_t)_head < SENSOR_PACK_BASE + SENSOR_PACK_MAX && SENSOR_PACK_BASE < (uintptr_t)_end) ||
     (rd_start < SENSOR_PACK_BASE + SENSOR_PACK_MAX && SENSOR_PACK_BASE < rd_end) ||
     (probe_fdt_address() < SENSOR_PACK_BASE + SENSOR_PACK_MAX &&
      SENSOR_PACK_BASE < probe_fdt_address() + probe_fdt_bytes()) ||
     (phys_base < SENSOR_PACK_BASE + SENSOR_PACK_MAX &&
      SENSOR_PACK_BASE < phys_base + mem_size))
  return "sensor pack window overlaps loader/FDT/kernel";
 copy64(SENSOR_PACK_BASE, start, n);
 __asm__ volatile("dsb sy" ::: "memory");
 con_hex2("SNSPACK", "pa=", SENSOR_PACK_BASE, "bytes=", n);
 return 0;
}
"""


def _load_and_validate_tab(path: Path, expected_sensors: tuple[int, ...] | None = None) -> bytes:
    data = path.read_bytes()
    if len(data) < 28 or len(data) > 49152:
        raise SystemExit(f'invalid camera tab size: {path} ({len(data)} bytes)')
    magic = data[:8]
    if magic not in (b'FUXICAM1', b'FUXICAM2'):
        raise SystemExit(f'invalid FUXICAM1/FUXICAM2 magic in {path}: {magic!r}')
    if magic == b'FUXICAM2':
        if len(data) < 32:
            raise SystemExit(f'truncated FUXICAM2 header: {path}')
        sid = data[28]
        if expected_sensors is not None and sid not in expected_sensors:
            raise SystemExit(f'{path} FUXICAM2 sensor_id={sid} not in {expected_sensors}')
    return data


def _reconcile_capsrc_stub(
    camera_mode: int,
    camera_dylib: bool,
    capsrc_stub: int | None,
    inject_media: bool,
) -> tuple[int | None, bool, dict | None]:
    """Avoid writing ADT fuxi,capsrc-stub!=0 when --camera-dylib is enabled."""
    if capsrc_stub is None:
        return None, inject_media, None
    if not (0 <= capsrc_stub <= 15):
        raise ValueError(
            '--capsrc-stub takes 0 (off) or bitmask 1..15 '
            '(1 capturesource, 2 capturesession, 4 deferred-cm, 8 flashlight)'
        )
    if camera_mode != 0 and camera_dylib and capsrc_stub != 0:
        return 0, True, {
            'requested': capsrc_stub,
            'reason': (
                'camera-dylib owns CoreMedia capture XPC servers; '
                'libfuxi-camera self-disables when ADT fuxi,capsrc-stub != 0'
            ),
        }
    return capsrc_stub, inject_media, None


def _validate_adt_sweetnull(adt_sweetnull: str, stderr_log: str | None) -> str:
    if stderr_log:
        raise SystemExit('--adt-sweetnull is exclusive with --stderr-log')
    if (not isinstance(adt_sweetnull, str) or
            not re.fullmatch(r'[A-Za-z0-9._-]+(,[A-Za-z0-9._-]+)*', adt_sweetnull) or
            ({'backboardd', 'SpringBoard'} & set(adt_sweetnull.split(',')))):
        raise SystemExit(f'invalid --adt-sweetnull process list: {adt_sweetnull!r}')
    full = 'backboardd,SpringBoard,' + adt_sweetnull
    if len(full.encode('ascii')) > 127:
        raise SystemExit('--adt-sweetnull full list exceeds 127 ASCII bytes')
    return full


def _patch_generator_source(source: bytes, has_drop: bool, sensor_pack_path: Path | None) -> bytes:
    # 1. Extended v1 ADT (> 16 KiB, <= 64 KiB)
    ext_old_1 = b"(['--extended-v1-adt'] if args.audio_bottom_full else [])"
    ext_new_1 = b"(['--extended-v1-adt'] if (args.audio_bottom_full or len(private.read_bytes()) > 0x4000) else [])"
    ext_old_2 = b"        loader_source = shim / 'fuxi-loader/loader.c'\n        if args.audio_bottom_full:"
    ext_new_2 = b"        loader_source = shim / 'fuxi-loader/loader.c'\n        if args.audio_bottom_full or len(private.read_bytes()) > 0x4000:"
    assert source.count(ext_old_1) == 1, 'unexpected --extended-v1-adt site'
    assert source.count(ext_old_2) == 1, 'unexpected loader_source site'
    source = source.replace(ext_old_1, ext_new_1, 1).replace(ext_old_2, ext_new_2, 1)

    # 2. M7 Wi-Fi UX 0x1fff (8191): bits 11 (2048 active scan reuse) & 12 (4096 bounded 110ms default dwell)
    ux_range_old = b"require(type(value) is int and 1 <= value <= 2047, '--wifi-ux must be 1..2047')"
    ux_range_new = b"require(type(value) is int and 1 <= value <= 8191, '--wifi-ux must be 1..8191')"
    assert source.count(ux_range_old) == 1, 'unexpected wifi_ux range check site'
    source = source.replace(ux_range_old, ux_range_new, 1)

    ux_tbl_old = b"        (1024, b'FUXI ATH12K: WMI scan trace enabled', 'WMI scan trace'),\n"
    ux_tbl_new = ux_tbl_old + (
        b"        (2048, b'FUXI WIFI IOS: ux scan options', 'active scan reuse'),\n"
        b"        (4096, b'FUXI WIFI IOS: ux scan options', 'bounded 110ms default dwell'),\n"
    )
    assert source.count(ux_tbl_old) == 1, 'unexpected wifi_ux marker table site'
    source = source.replace(ux_tbl_old, ux_tbl_new, 1)

    # 3. Optional cmdline token dropping / allow-exec
    if has_drop:
        line_old = b"        line = ' '.join(kept + ['fuxiwlan=1', 'fuxiwlanath=1', 'fuxiwlanmlo=192'] +"
        line_new = b"        line = ' '.join(_fuxi_wrapper_drop(kept) + ['fuxiwlan=1', 'fuxiwlanath=1', 'fuxiwlanmlo=192'] +"
        assert source.count(line_old) == 1, 'unexpected native cmdline site'
        source = source.replace(line_old, line_new, 1)

    # 4. Hook GPU M9/M11 boot-arg rewrite right before ADT_WIFI_KEYS split & 608-byte limit check
    final_old = b"        final = line.split(' ')\n        require(len({token.partition('=')[0] for token in final}) == len(final), 'duplicate native boot argument')"
    final_new = (
        b"        final = _fuxi_aio_cmdline_transform(line.split(' '))\n"
        b"        require(len({token.partition('=')[0] for token in final}) == len(final), 'duplicate native boot argument')"
    )
    assert source.count(final_old) == 1, 'unexpected final cmdline site'
    source = source.replace(final_old, final_new, 1)


    # 5b. Fall back to ARCHIVE_STORAGE for archived input loader / shim_reference directories
    for old_s, new_s in (
        (b"source, artifact = STORAGE / args.source, STORAGE / args.artifact",
         b"source, artifact = _resolve_input_dir(args.source), _resolve_input_dir(args.artifact)"),
        (b"shim_reference = STORAGE / args.shim_reference if args.shim_reference else source",
         b"shim_reference = _resolve_input_dir(args.shim_reference) if args.shim_reference else source"),
        (b"kernel = STORAGE / args.artifact / 'kernel.development.fuxi'",
         b"kernel = _resolve_input_dir(args.artifact) / 'kernel.development.fuxi'"),
        (b"paths = [STORAGE / n / 'boot.log' for n in (first, second)]",
         b"paths = [_resolve_input_dir(n) / 'boot.log' for n in (first, second)]"),
        (b"with open(STORAGE / args.like / 'wlan-ath12k-profile.json', encoding='utf-8') as handle:",
         b"with open(_resolve_input_dir(args.like) / 'wlan-ath12k-profile.json', encoding='utf-8') as handle:"),
        (b"    inputs = sums(shim_reference / 'source-input-sha256.txt')\n"
         b"    shim = WORKSPACE / 'ios-fuxi/shim'\n"
         b"    for name, checksum in inputs.items():\n"
         b"        checked_file(shim / name, inputs, name)",
         b"    inputs = sums(shim_reference / 'source-input-sha256.txt')\n"
         b"    shim = WORKSPACE / 'ios-fuxi/shim'\n"
         b"    for name, checksum in list(inputs.items()):\n"
         b"        if name in _ALLOW_SHIM_DRIFT:\n"
         b"            require(name == 'fuxi-loader/loader-chain.ld', f'cannot waive shim check for used file {name}')\n"
         b"            del inputs[name]\n"
         b"            continue\n"
         b"        checked_file(shim / name, inputs, name)"),
    ):
        assert source.count(old_s) == 1, f'unexpected archive path site: {old_s!r}'
        source = source.replace(old_s, new_s, 1)

    # 6. Expand v1/v2 extended ADT limit from 64 KiB to 128 KiB (for all 6 camera .tab tables + full assets)
    mkp_old = b"subprocess.run([sys.executable, str(TOOLS / 'mkpayload.py'),"
    mkp_new = b"subprocess.run([sys.executable, str(_stage_mkpayload_128k(TOOLS, stage / 'mkpayload-128k.py')),"
    assert source.count(mkp_old) == 1, 'unexpected mkpayload.py invocation site'
    source = source.replace(mkp_old, mkp_new, 1)

    sz_old = b"require(0x4000 < size <= 0x10000, 'full stock firmware needs the extended device tree')"
    sz_new = b"require(0x4000 < size <= 0x20000, 'full stock firmware needs the extended device tree')"
    assert source.count(sz_old) == 1, 'unexpected extended ADT size require site'
    source = source.replace(sz_old, sz_new, 1)

    ldr_old = b"            loader_source.write_bytes(original.replace(old, b'const uint64_t adt_max = ADT_MAX_V2;'))"
    ldr_new = b"            loader_source.write_bytes(_patch_loader_c(original.replace(old, b'const uint64_t adt_max = ADT_MAX_V2;'), _SENSOR_PACK_PATH is not None))"
    assert source.count(ldr_old) == 1, 'unexpected loader-speaker.c write site'
    source = source.replace(ldr_old, ldr_new, 1)

    # 7. Embed _sensor_pack in .sensor_pack (after _payload_end) & stage_sensor_pack in loader-speaker.c
    if sensor_pack_path is not None:
        gfw_old = b"        (stage / 'gpu-fw.S').write_text('\\n'.join(assembly) + '\\n', encoding='utf-8', newline='\\n')"
        gfw_new = (
            b"        assembly.extend(('.section .sensor_pack,\"a\"', '.balign 8', '.global _sensor_pack_start', '_sensor_pack_start:',\n"
            b"                         '.incbin ' + json.dumps(_SENSOR_PACK_PATH.as_posix()),\n"
            b"                         '.global _sensor_pack_end', '_sensor_pack_end:', '.balign 8'))\n"
            b"        (stage / 'gpu-fw.S').write_text('\\n'.join(assembly) + '\\n', encoding='utf-8', newline='\\n')"
        )
        assert source.count(gfw_old) == 1, 'unexpected gpu-fw.S site'
        source = source.replace(gfw_old, gfw_new, 1)

        ld_old = b"        subprocess.run([str(args.compiler_dir / 'ld.lld.exe'), '-T', str(shim / 'fuxi-loader/loader.ld'),"
        ld_new = b"        subprocess.run([str(args.compiler_dir / 'ld.lld.exe'), '-T', str(_write_sensor_ld(shim / 'fuxi-loader/loader.ld', stage / 'loader-sensor.ld')),"
        assert source.count(ld_old) == 1, 'unexpected ld.lld invocation site'
        source = source.replace(ld_old, ld_new, 1)

        objcopy_old = b"        subprocess.run([str(args.compiler_dir / 'llvm-objcopy.exe'), '-O', 'binary', str(stage / 'loader.elf'),"
        objcopy_new = (
            b"        _verify_sensor_pack_elf(args.compiler_dir, stage / 'loader.elf', _SENSOR_PACK_PATH)\n"
            b"        subprocess.run([str(args.compiler_dir / 'llvm-objcopy.exe'), '-O', 'binary', str(stage / 'loader.elf'),"
        )
        assert source.count(objcopy_old) == 1, 'unexpected llvm-objcopy site'
        source = source.replace(objcopy_old, objcopy_new, 1)

    return source


def _stage_mkpayload_128k(tools_dir: Path, out_path: Path) -> Path:
    src = (tools_dir / 'mkpayload.py').read_text(encoding='utf-8').replace('\r\n', '\n')
    old_path = "sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))"
    new_path = f"sys.path.insert(0, {json.dumps(str(tools_dir))})"
    old_max = "ADT_MAX = {1: 0x4000, 2: 0x10000}"
    new_max = "ADT_MAX = {1: 0x4000, 2: 0x20000}"
    assert src.count(old_path) == 1, 'unexpected sys.path line in mkpayload.py'
    assert src.count(old_max) == 1, 'unexpected ADT_MAX line in mkpayload.py'
    src = src.replace(old_path, new_path, 1).replace(old_max, new_max, 1)
    out_path.write_text(src, encoding='utf-8', newline='\n')
    return out_path


def _write_sensor_ld(shim_ld: Path, out_ld: Path) -> Path:
    ld_txt = shim_ld.read_text(encoding='utf-8').replace('\r\n', '\n')
    target = '_payload_end = .;\n'
    replacement = '_payload_end = .;\n . = ALIGN(8);\n .sensor_pack : { *(.sensor_pack) }\n . = ALIGN(8);\n'
    assert ld_txt.count(target) == 1, 'unexpected _payload_end in loader.ld'
    out_ld.write_text(ld_txt.replace(target, replacement, 1), encoding='utf-8', newline='\n')
    return out_ld


def _verify_sensor_pack_elf(compiler_dir: Path, elf_path: Path, pack_path: Path) -> None:
    out = subprocess.check_output([str(compiler_dir / 'llvm-readelf.exe'), '-sW', str(elf_path)], text=True)
    need = {'_payload_end', '_sensor_pack_start', '_sensor_pack_end', '_image_end'}
    syms: dict[str, int] = {}
    for line in out.splitlines():
        fields = line.split()
        if len(fields) >= 8 and fields[-1] in need:
            syms[fields[-1]] = int(fields[1], 16)
    missing = sorted(need - set(syms))
    if missing:
        raise AssertionError(f'loader.elf missing symbols: {missing}')
    pack_size = pack_path.stat().st_size
    assert syms['_sensor_pack_start'] % 8 == 0, f"_sensor_pack_start unaligned: {syms['_sensor_pack_start']:#x}"
    actual_size = syms['_sensor_pack_end'] - syms['_sensor_pack_start']
    assert actual_size == pack_size, f'sensor pack ELF size {actual_size} != {pack_size}'
    assert syms['_payload_end'] <= syms['_sensor_pack_start'] < syms['_sensor_pack_end'] <= syms['_image_end'], (
        f'invalid .sensor_pack bounds: {syms}'
    )
    padded_end = syms['_sensor_pack_start'] + ((pack_size + 7) & ~7)
    assert padded_end <= syms['_image_end'], f'padded sensor pack end {padded_end:#x} > _image_end {syms["_image_end"]:#x}'


def _patch_loader_c(raw: bytes, has_sensor_pack: bool) -> bytes:
    txt = raw.decode('utf-8').replace('\r\n', '\n')
    def_old = '#define ADT_MAX_V2\t\t(4 * PAGE_16K)\n'
    def_new = '#define ADT_MAX_V2\t\t(8 * PAGE_16K)\n'
    if has_sensor_pack:
        def_new += '#define SENSOR_PACK_BASE\t0xe8800000ULL\n#define SENSOR_PACK_MAX\t\t0x200000ULL\n'
    assert txt.count(def_old) == 1, 'unexpected ADT_MAX_V2 define in loader.c'
    txt = txt.replace(def_old, def_new, 1)

    if has_sensor_pack:
        fn_old = ' __asm__ volatile("dsb sy" ::: "memory");\n h->magic = FG_MAGIC;\n}\n'
        fn_new = fn_old + STAGE_SENSOR_PACK_C
        assert txt.count(fn_old) == 1, 'unexpected stage_gpu_firmware end in loader.c'
        txt = txt.replace(fn_old, fn_new, 1)

        call_old = '  stage_gpu_firmware();\n }'
        call_new = (
            '  stage_gpu_firmware();\n'
            ' }\n'
            ' {\n'
            '  const char *sp_err = stage_sensor_pack(rd_start, rd_end, p->phys_base, p->mem_size);\n'
            '  if (sp_err) return sp_err;\n'
            ' }'
        )
        assert txt.count(call_old) == 1, 'unexpected stage_gpu_firmware call in loader.c'
        txt = txt.replace(call_old, call_new, 1)
    return txt.encode('utf-8')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    # B-019 modem / voice / hotspot
    ap.add_argument('--wifi-ap', type=lambda v: int(v, 0), default=15)
    ap.add_argument('--modem', type=int, choices=(1, 2, 3), default=3)
    ap.add_argument('--modem-tftp', action='store_true', default=True)
    ap.add_argument('--modem-ipa', type=lambda v: int(v, 0), default=6131)
    ap.add_argument('--modem-bb', type=lambda v: int(v, 0), default=0x7EB)
    ap.add_argument('--audio-voice', type=int, choices=(1, 2, 3, 4, 5), default=4)
    # Audio mic cfg + DSP
    ap.add_argument('--audio-mic-cfg', type=int, choices=(0, 1), default=1)
    ap.add_argument('--audio-mic-dsp', type=int, choices=(0, 1, 2, 3), default=1)
    # Camera K-080 + U-028
    ap.add_argument('--camera', type=int, choices=range(1, 9), default=7)
    ap.add_argument('--camera-pll', action='store_true')
    ap.add_argument('--camera-dylib', action='store_true')
    ap.add_argument('--camera-isp', action='store_true')
    ap.add_argument('--camera-back-sensor', type=int, choices=(1, 3, 4), default=1)
    ap.add_argument('--camera-multicam', type=lambda v: int(v, 0), default=3)
    ap.add_argument('--camera-tab', type=Path)
    ap.add_argument('--camera-tab-wide', type=Path)
    ap.add_argument('--camera-tab-wide-4k60', type=Path)
    ap.add_argument('--camera-tab-ultra', type=Path)
    ap.add_argument('--camera-tab-tele', type=Path)
    ap.add_argument('--camera-tab-tele-60', type=Path)
    # Sensors v8
    ap.add_argument('--sensors', type=lambda v: int(v, 0), default=255)
    ap.add_argument('--sensors-compass-mode', type=int, choices=(0, 1), default=0)
    ap.add_argument('--sensors-mag-bias', default='233400,56400,447780')
    ap.add_argument('--sensor-pack', type=Path)
    # M9 + M11 GPU kernel layer switches
    ap.add_argument('--gpu-ncimport', default='606385736',
                    help='Override fuxigpuncimport=2405960 (default 606385736 for M11 IRQ bit26 + CBO bit29; empty to keep)')
    ap.add_argument('--gpu-hyb8-ovsh', action='store_true',
                    help='Rewrite fuxitudebug=nocb -> nocb,ovsh and append fuxihyb=8 (keeps fuxiperfidle=1000)')
    ap.add_argument('--gpu-bb-recover', type=int, default=3,
                    help='ADT /chosen fuxi,gpu-bb-recover Class 0 bounded soft recovery limit 0..8 (default 3)')
    ap.add_argument('--gpu-clients', default='none',
                    help='Class 1 foreground Metal allowlist written to ADT /chosen fuxi,gpu-clients '
                         '(none [default], core, maps-extended, minimal, or comma-separated list <=254B; '
                         'U-026 permanently rejects *poster*, SpringBoard, mediaserverd, backboardd)')
    # M6 GNSS & Camera diagnostics
    ap.add_argument('--modem-gnss', type=lambda v: int(v, 0), default=3,
                    help='ADT /chosen fuxi,modem-gnss bitmask (default 3=probe+bridge, 0=off)')
    ap.add_argument('--camera-diag', type=lambda v: int(v, 0), default=0,
                    help='Optional ADT /chosen fuxi,camera-diag bitmask (default 0=off)')
    # ADT sweetnull (keeps boot cmdline < 608 B while preserving full stderr capture)
    ap.add_argument('--adt-sweetnull', metavar='PROCS',
                    help='Write backboardd,SpringBoard,<PROCS> to ADT /chosen fuxi,sweetnull')
    # Cmdline helpers & optional selftest preset
    ap.add_argument('--selftest-preset', choices=sorted(SELFTEST_PRESETS), default=None,
                    help='Optional built-in tapcmd preset (e.g. selftest7) applied after -- when --tap-script is omitted')
    ap.add_argument('--drop-arg', action='append', default=[])
    ap.add_argument('--allow-exec', action='append', default=[])
    ap.add_argument('--allow-shim-drift', action='append', default=[])
    ap.add_argument('--generator-rev', default='HEAD')
    ap.add_argument('rest', nargs=argparse.REMAINDER)
    args = ap.parse_args()

    try:
        gpu_clients_val = resolve_gpu_clients(args.gpu_clients)
    except ValueError as exc:
        raise SystemExit(str(exc)) from exc

    rest = args.rest[1:] if args.rest[:1] == ['--'] else args.rest
    if not rest or rest[0] != 'build':
        raise SystemExit('pass generator arguments after --, starting with build')
    rest, preset_meta = _apply_selftest_preset(rest, args.selftest_preset)

    if args.gpu_ncimport:
        if not re.fullmatch(r'(?:0x[0-9a-fA-F]+|[0-9]+)', args.gpu_ncimport):
            raise SystemExit(f'invalid --gpu-ncimport value: {args.gpu_ncimport!r}')
    if not (0 <= args.gpu_bb_recover <= 8):
        raise SystemExit(f'--gpu-bb-recover must be 0..8, got {args.gpu_bb_recover}')

    sensor_pack_path = args.sensor_pack.resolve() if args.sensor_pack else None
    sensor_pack_bytes = 0
    if sensor_pack_path is not None:
        if not sensor_pack_path.is_file():
            raise SystemExit(f'missing sensor pack: {sensor_pack_path}')
        sensor_pack_bytes = sensor_pack_path.stat().st_size
        if sensor_pack_bytes < 32 or sensor_pack_bytes > SENSOR_PACK_MAX:
            raise SystemExit(f'sensor pack size out of range: {sensor_pack_bytes}')

    tab_blobs: list[tuple[str, Path, bytes]] = []
    if args.camera_tab:
        tab_blobs.append(('fuxi,camera-tab', args.camera_tab, _load_and_validate_tab(args.camera_tab, (1, 2, 3, 4))))
    if args.camera_tab_wide:
        tab_blobs.append(('fuxi,camera-tab-wide', args.camera_tab_wide, _load_and_validate_tab(args.camera_tab_wide, (1,))))
    if args.camera_tab_wide_4k60:
        tab_blobs.append(('fuxi,camera-tab-wide-4k60', args.camera_tab_wide_4k60, _load_and_validate_tab(args.camera_tab_wide_4k60, (1,))))
    if args.camera_tab_ultra:
        tab_blobs.append(('fuxi,camera-tab-ultra', args.camera_tab_ultra, _load_and_validate_tab(args.camera_tab_ultra, (3,))))
    if args.camera_tab_tele:
        tab_blobs.append(('fuxi,camera-tab-tele', args.camera_tab_tele, _load_and_validate_tab(args.camera_tab_tele, (4,))))
    if args.camera_tab_tele_60:
        tab_blobs.append(('fuxi,camera-tab-tele-60', args.camera_tab_tele_60, _load_and_validate_tab(args.camera_tab_tele_60, (4,))))

    u32_switches = [
        ('fuxi,modem', args.modem),
        ('fuxi,modem-ipa', args.modem_ipa),
        ('fuxi,modem-bb', args.modem_bb),
        ('fuxi,audio-mic-cfg', args.audio_mic_cfg),
        ('fuxi,audio-mic-dsp', args.audio_mic_dsp),
        ('fuxi,camera', args.camera),
        ('fuxi,camera-back-sensor', args.camera_back_sensor),
        ('fuxi,camera-multicam', args.camera_multicam),
        ('fuxi,sensors', args.sensors),
        ('fuxi,sensors-compass-mode', args.sensors_compass_mode),
    ]
    if args.modem_tftp:
        u32_switches.append(('fuxi,modem-tftp', 1))
    if args.audio_voice:
        u32_switches.append(('fuxi,audio-voice', args.audio_voice))
    if args.wifi_ap:
        u32_switches.append(('fuxi,wifi-ap', args.wifi_ap))
    if args.camera_pll:
        u32_switches.append(('fuxi,camera-pll', 1))
    if args.camera_dylib:
        u32_switches.append(('fuxi,camera-dylib', 1))
    if args.modem_gnss:
        u32_switches.append(('fuxi,modem-gnss', args.modem_gnss))
    if args.gpu_bb_recover:
        u32_switches.append(('fuxi,gpu-bb-recover', args.gpu_bb_recover))
    if args.camera_diag:
        u32_switches.append(('fuxi,camera-diag', args.camera_diag))

    markers = [
        b'FUXI MODEM: started mode %u',
        b'FUXI BB: %s <- %s (mode=%#x)',
        b'/Developer/libfuxi-bb.dylib',
        b'/Developer/libfuxi-camera.dylib',
        b'COACHZ baseband:',
        b'COACHZ gnss:',
        b'fuxi,modem-gnss',
        b'fuxi,gpu-bb-recover',
        b'Class 0 bounded soft recovery enabled',
        b'fuxi,wifi-ap',
        b'FuxiWLANAP',
        b'FUXI CAMERA: started mode %u',
        b'FUXI CAMERA: mounted /Developer read-only',
        b'fuxi,sensors',
        b'fuxi,sensors-mag-bias',
        b'fuxi,audio-mic-cfg',
        b'hpf=%s',
        b'haptic stream v4 on',
        b'fuxi,haptic-damp',
        b'NativeAdrenoGpuPublished v2',
        b'cached-coherent BOs enabled',
        b'FUXI WIFI IOS: ux scan options',
        b'fuxi,sweetnull',
    ]

    path = WORKSPACE / GENERATOR
    revision = subprocess.check_output(
        ['git', '-C', str(WORKSPACE), 'rev-parse', args.generator_rev + '^{commit}'],
        text=True,
    ).strip()
    source = subprocess.check_output(['git', '-C', str(WORKSPACE), 'show', f'{revision}:{GENERATOR}'])
    drop = tuple(args.drop_arg)
    allow_exec = tuple(args.allow_exec)
    source = _patch_generator_source(source, bool(drop or allow_exec), sensor_pack_path)

    module = types.ModuleType('fuxi_aio_loader_generator')
    module.__file__ = str(path)
    sys.modules[module.__name__] = module

    def _fuxi_wrapper_drop(kept):
        missing = [t for t in drop if t not in kept]
        if missing:
            raise SystemExit(f'--drop-arg not in source command line: {" ".join(missing)}')
        kept = [t for t in kept if t not in drop]
        if allow_exec:
            blocks = [i for i, t in enumerate(kept) if t.startswith('sweetnoexec=')]
            if len(blocks) != 1:
                raise SystemExit('--allow-exec needs one sweetnoexec= in source command line')
            names = kept[blocks[0]].partition('=')[2].split(',')
            miss = [n for n in allow_exec if n not in names]
            if miss:
                raise SystemExit(f'--allow-exec not in sweetnoexec: {",".join(miss)}')
            names = [n for n in names if n not in allow_exec]
            if names:
                kept[blocks[0]] = 'sweetnoexec=' + ','.join(names)
            else:
                del kept[blocks[0]]
        return kept

    def _fuxi_aio_cmdline_transform(tokens):
        out = list(tokens)
        if args.gpu_ncimport:
            hits = [i for i, t in enumerate(out) if t == 'fuxigpuncimport=2405960']
            if len(hits) != 1:
                raise SystemExit('expected exact fuxigpuncimport=2405960 token before --gpu-ncimport rewrite')
            out[hits[0]] = f'fuxigpuncimport={args.gpu_ncimport}'
        if args.gpu_hyb8_ovsh:
            tudbg_hits = [i for i, t in enumerate(out) if t == 'fuxitudebug=nocb']
            if len(tudbg_hits) != 1:
                raise SystemExit('--gpu-hyb8-ovsh requires fuxitudebug=nocb (e.g. --daily-tuning)')
            if any(t.startswith('fuxihyb=') for t in out):
                raise SystemExit('duplicate fuxihyb= token')
            out[tudbg_hits[0]] = 'fuxitudebug=nocb,ovsh'
            out.append('fuxihyb=8')
        return out

    module.__dict__['_resolve_input_dir'] = _resolve_input_dir
    module.__dict__['_ALLOW_SHIM_DRIFT'] = set(args.allow_shim_drift)
    module.__dict__['_fuxi_wrapper_drop'] = _fuxi_wrapper_drop
    module.__dict__['_fuxi_aio_cmdline_transform'] = _fuxi_aio_cmdline_transform
    module.__dict__['_SENSOR_PACK_PATH'] = sensor_pack_path
    module.__dict__['_patch_loader_c'] = _patch_loader_c
    module.__dict__['_stage_mkpayload_128k'] = _stage_mkpayload_128k
    module.__dict__['_write_sensor_ld'] = _write_sensor_ld
    module.__dict__['_verify_sensor_pack_elf'] = _verify_sensor_pack_elf
    exec(compile(source, str(path), 'exec'), module.__dict__)

    module.NATIVE_OBJECTS[AP_LOCAL_OBJECT] = module.NATIVE_OBJECTS[module.SCAN_PRIORITY_OBJECT]
    original = module.build

    def build(gargs, report, tree, source_inputs):
        linked = json.loads((module.STORAGE / gargs.artifact / 'ath12k-link.json').read_text(encoding='utf-8'))
        if linked.get('object_sha256') == AP_LOCAL_OBJECT:
            module.require(
                linked.get('peer_zero_provenance_sha256') == module.PEER_ZERO_PROVENANCE and
                linked.get('scan_priority_wmi_sha256') == module.SCAN_PRIORITY_WMI_SHA256 and
                linked.get('softap_nw_ap_sha256') == AP_NW_AP_SHA256,
                'artifact AP ath12k provenance mismatch',
            )
        kernel = (module.STORAGE / gargs.artifact / 'kernel.development.fuxi').read_bytes()
        for m in markers:
            module.require(m in kernel, f'kernel missing marker: {m!r}')
        for prop_name, _, _ in tab_blobs:
            module.require(prop_name.encode('ascii') in kernel, f'kernel missing camera tab prop {prop_name}')

        full_sweetnull = None
        if args.adt_sweetnull is not None:
            full_sweetnull = _validate_adt_sweetnull(args.adt_sweetnull, getattr(gargs, 'stderr_log', None))
            module.require(gargs.native_ath12k, '--adt-sweetnull requires --native-ath12k')

        chosen = module.chosen_node(tree)
        forbidden_prefixes = ('fuxi,modem', 'fuxi,wifi-ap', 'fuxi,audio-voice', 'fuxi,camera',
                              'fuxi,sensors', 'fuxi,sensor-pack', 'fuxi,audio-mic-cfg', 'fuxi,audio-mic-dsp',
                              'fuxi,sweetnull', 'fuxi,gpu-bb-recover', 'fuxi,gpu-clients')
        module.require(not any(n.startswith(forbidden_prefixes) for n, _, _ in chosen.props),
                       'source ADT already carries merged switches')

        if gpu_clients_val is not None:
            module.require(gargs.atx_fix == 1, 'U-026 requires --atx-fix 1 when --gpu-clients is enabled')
            module.require(
                b'FUXI GPU ABI: U-026 blacklist denied' in kernel and
                b'FUXI GPU RECOVER: isolated Class 1 VM' in kernel and
                b'fuxi,gpu-clients' in kernel,
                'kernel missing M9 multi-client GPU / fuxi,gpu-clients markers',
            )
            chosen.props.append(('fuxi,gpu-clients', 0, validate_gpu_clients(gpu_clients_val).encode('ascii') + b'\0'))

        for name, value in u32_switches:
            chosen.props.append((name, 0, struct.pack('<I', value)))
        if full_sweetnull is not None:
            chosen.props.append(('fuxi,sweetnull', 0, full_sweetnull.encode('ascii') + b'\0'))
        if args.sensors_mag_bias:
            chosen.props.append(('fuxi,sensors-mag-bias', 0, args.sensors_mag_bias.encode('ascii') + b'\0'))
        if sensor_pack_path is not None:
            chosen.props.append(('fuxi,sensor-pack', 0, struct.pack('<QQ', SENSOR_PACK_BASE, sensor_pack_bytes)))
        for prop_name, _tpath, blob in tab_blobs:
            chosen.props.append((prop_name, 0, blob))

        def max_phandle(node):
            v = node.prop('AAPL,phandle')
            own = struct.unpack('<I', v)[0] if v and len(v) == 4 else 0
            return max([own] + [max_phandle(c) for c in node.children])

        next_ph = max_phandle(tree) + 1

        if args.modem_bb:
            product = [n for n in tree.children if n.name == 'product']
            module.require(len(product) == 1 and product[0].prop('baseband-chipset') == b'mav17\0',
                           'source ADT missing /product mav17')
            module.require(not any(n.name == 'baseband' for n in tree.children), 'ADT already has /baseband')
            bb_node = module.adt.Node()
            bb_node.props = [
                ('name', 0, b'baseband\0'),
                ('AAPL,phandle', 0, struct.pack('<I', next_ph)),
                ('device_type', 0, b'baseband\0'),
                ('compatible', 0, b'baseband,n41\0'),
                ('class', 0, struct.pack('<I', 3)),
                ('imeisv', 0, struct.pack('<I', 0x39)),
                ('device-imei', 0, b'\x00' * 32),
                ('config', 0,
                 b'\xfa\x00\x00\x00\xf4\x01\x00\x00DH\x00\x00\xe8\x03\x00\x00'
                 b'd\x00\x00\x00d\x00\x00\x00\x83\x00\x00\x00%\x00\x00\x00'
                 b'\x14\x00\x00\x00d\x00\x00\x00'),
            ]
            tree.children.append(bb_node)
            next_ph += 1

        if args.camera_isp:
            arm_io = [n for n in tree.children if n.name == 'arm-io']
            product = [n for n in tree.children if n.name == 'product']
            module.require(len(arm_io) == 1 and len(product) == 1, 'ADT needs /arm-io and /product')
            module.require(not any(n.name == 'isp' for n in arm_io[0].children), 'ADT already has /arm-io/isp')
            module.require(not any(n.name == 'camera' for n in product[0].children), 'ADT already has /product/camera')
            isp_node = module.adt.Node()
            isp_node.props = [
                ('name', 0, b'isp\0'),
                ('AAPL,phandle', 0, struct.pack('<I', next_ph)),
                ('camera-front', 0, struct.pack('<I', 1)),
                ('camera-rear', 0, struct.pack('<I', 1)),
                ('face-detection-support', 0, struct.pack('<I', 1)),
            ]
            arm_io[0].children.append(isp_node)
            next_ph += 1
            cam_node = module.adt.Node()
            cam_node.props = [
                ('name', 0, b'camera\0'),
                ('AAPL,phandle', 0, struct.pack('<I', next_ph)),
            ]
            product[0].children.append(cam_node)
            next_ph += 1

        report['modem'] = {k: v for k, v in u32_switches if k.startswith('fuxi,modem')}
        report['modem']['adt_baseband'] = True
        report['audio_voice'] = args.audio_voice
        report['wifi_ap'] = args.wifi_ap
        report['audio_mic_cfg'] = args.audio_mic_cfg
        report['audio_mic_dsp'] = args.audio_mic_dsp
        report['camera'] = {k: v for k, v in u32_switches if k.startswith('fuxi,camera')}
        report['camera']['adt_isp'] = args.camera_isp
        for prop_name, tpath, blob in tab_blobs:
            report['camera'][prop_name] = {
                'file': tpath.name,
                'bytes': len(blob),
                'sha256': hashlib.sha256(blob).hexdigest(),
            }
        report['sensors'] = {
            'fuxi,sensors': args.sensors,
            'fuxi,sensors-compass-mode': args.sensors_compass_mode,
            'fuxi,sensors-mag-bias': args.sensors_mag_bias,
            'sensor_pack_bytes': sensor_pack_bytes,
            'sensor_pack_sha256': hashlib.sha256(sensor_pack_path.read_bytes()).hexdigest() if sensor_pack_path else None,
        }
        report['adt_sweetnull'] = full_sweetnull
        report['gpu_clients'] = gpu_clients_val
        report['gpu_aio'] = {
            'fuxigpuncimport': args.gpu_ncimport or '2405960',
            'hyb8_ovsh': bool(args.gpu_hyb8_ovsh),
            'gpu_bb_recover': args.gpu_bb_recover,
            'gpu_clients': gpu_clients_val,
        }
        report['aio_generator'] = {
            'revision': revision,
            'wrapper': 'work/adsp-battery/prepare-aio-loader.py',
            'allowed_exec': list(allow_exec),
            'dropped_boot_args': list(drop),
        }
        if preset_meta is not None:
            report['selftest_preset'] = preset_meta

        new_capsrc, new_inject_media, suppressed_capsrc = _reconcile_capsrc_stub(
            args.camera,
            bool(args.camera_dylib),
            getattr(gargs, 'capsrc_stub', None),
            bool(getattr(gargs, 'inject_media', False)),
        )
        if suppressed_capsrc is not None:
            gargs.capsrc_stub = new_capsrc
            gargs.inject_media = new_inject_media
            report['capsrc_stub_suppressed'] = suppressed_capsrc
            print(
                f"NOTE: suppressing --capsrc-stub {suppressed_capsrc['requested']} -> 0 "
                f"because --camera {args.camera} --camera-dylib is enabled "
                f"(libfuxi-camera owns CoreMedia capture XPC servers)",
                file=sys.stderr,
            )

        original(gargs, report, tree, source_inputs)

        adt_bytes = (module.STORAGE / gargs.name / 'private.adt').read_bytes()
        verify_adt_u026(
            adt_bytes,
            module.adt,
            expected_gpu_clients=gpu_clients_val,
            camera_dylib=bool(args.camera and args.camera_dylib),
        )
        for prop_name in (b'fuxi,modem', b'fuxi,wifi-ap', b'fuxi,atx-fix', b'fuxi,audio-voice',
                          b'fuxi,camera', b'fuxi,camera-back-sensor', b'fuxi,camera-multicam',
                          b'fuxi,sensors', b'fuxi,sensor-pack',
                          b'fuxi,audio-mic-cfg', b'fuxi,audio-mic-dsp', b'fuxi,haptic-damp',
                          b'fuxi,wifi-ux'):
            module.require(prop_name in adt_bytes, f'generated private.adt lost {prop_name!r}')
        for prop_name, _, _ in tab_blobs:
            module.require(prop_name.encode('ascii') in adt_bytes,
                           f'generated private.adt lost {prop_name!r}')
        if args.modem_gnss:
            module.require(b'fuxi,modem-gnss' in adt_bytes, 'generated private.adt lost fuxi,modem-gnss')
        if args.gpu_bb_recover:
            module.require(b'fuxi,gpu-bb-recover' in adt_bytes, 'generated private.adt lost fuxi,gpu-bb-recover')
        if args.camera_diag:
            module.require(b'fuxi,camera-diag' in adt_bytes, 'generated private.adt lost fuxi,camera-diag')
        if full_sweetnull is not None:
            module.require(b'fuxi,sweetnull' in adt_bytes, 'generated private.adt lost fuxi,sweetnull')
            module.require(full_sweetnull.encode('ascii') + b'\0' in adt_bytes,
                           'generated private.adt fuxi,sweetnull value mismatch')
        if getattr(gargs, 'wifi_ux', None) == 0x1fff:
            # Confirm fuxi,wifi-ux encoded as LE u32 0x1fff (8191)
            out_tree, _ = module.adt.parse(adt_bytes)
            out_chosen = module.chosen_node(out_tree)
            module.require(out_chosen.prop('fuxi,wifi-ux') == struct.pack('<I', 0x1fff),
                           'generated private.adt fuxi,wifi-ux != 0x1fff')
        if args.camera and args.camera_dylib:
            module.require(
                b'fuxi,capsrc-stub' not in adt_bytes,
                'generated private.adt must not carry fuxi,capsrc-stub when --camera-dylib is enabled',
            )
        if preset_meta is not None and preset_meta.get('injected_tap_script'):
            out_tree, _ = module.adt.parse(adt_bytes)
            out_chosen = module.chosen_node(out_tree)
            expected_tap = SELFTEST_PRESETS[preset_meta['name']]['tap_script'].encode('ascii') + b'\0'
            module.require(out_chosen.prop('fuxi,tapcmd') == expected_tap,
                           f"generated private.adt fuxi,tapcmd mismatch for preset {preset_meta['name']}")

    module.build = build
    sys.argv = [str(path)] + rest
    module.main()


if __name__ == '__main__':
    main()
