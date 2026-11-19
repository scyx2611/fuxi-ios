import argparse
import ast
from contextlib import contextmanager, nullcontext
import copy
import importlib.util
import json
from pathlib import Path
import socket
import stat
import struct
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch


sys.dont_write_bytecode = True
DIRECTORY = Path(__file__).resolve().parent
specification = importlib.util.spec_from_file_location('fuxi_wlan_stage', DIRECTORY / 'stage-wlan-assets.py')
stage = importlib.util.module_from_spec(specification)
specification.loader.exec_module(stage)
phone = stage.load_phone()


class WlanStageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='fuxi-wlan-stage-test-')
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name).resolve()
        self.inputs = self.directory / 'inputs'
        self.inputs.mkdir()
        self.manifest_path = self.directory / 'manifest.json'
        self.manifest = {
            'root_path': stage.ROOT_PATH, 'bdf_basename': 'bdwlan.test.bin',
            'required': list(stage.FIXED_FILES) + ['bdwlan.test.bin'],
            'selection_provenance': {'evidence': 'Offline selection fixture only'},
            'm3_provenance': {'source': 'Offline test bytes only'}, 'files': {},
        }
        for position, name in enumerate(stage.asset_names(self.manifest)):
            contents = (f'OFFLINE_FIXTURE_{position}_{name}\n').encode('ascii') * 7
            path = self.inputs / name
            path.write_bytes(contents)
            self.manifest['files'][name] = {
                'host_path': str(path), 'bytes': len(contents), 'sha256': stage.digest(contents),
            }
        self.save_manifest()

    def save_manifest(self, manifest=None):
        value = self.manifest if manifest is None else manifest
        self.manifest_path.write_bytes((json.dumps(value, indent=2) + '\n').encode('utf-8'))

    def make_share(self):
        manifest, checksum = stage.load_manifest(self.manifest_path)
        output = self.directory / 'bundle'
        output.mkdir()
        return stage.prepare_share(output, self.manifest_path, manifest, checksum, stage.snapshot_sources(phone))

    def make_root(self):
        root = self.directory / 'root'
        root.mkdir()
        sentinel_paths = (
            'usr/local/lib/fuxi/libfuxi-sb.dylib',
            'private/var/mobile/Library/Preferences/com.apple.UIKit.plist',
            'private/var/mobile/Library/Preferences/.GlobalPreferences.plist',
            'private/var/mobile/Library/SpringBoard/IconState.plist',
            'usr/local/share/fuxi/wlan/keep.bin',
        )
        for name in sentinel_paths:
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'PRESERVE_EXISTING_DATA\n' + name.encode('ascii'))
        return root

    @contextmanager
    def guest_metadata_mock(self):
        with patch.object(stage.os, 'chown', create=True) as owner, patch.object(stage.os, 'chmod') as permissions:
            with patch.object(stage, 'installed_metadata', return_value=True):
                yield owner, permissions

    def guest_log(self, changed=True):
        records = []
        for name in stage.asset_names(self.manifest):
            record = self.manifest['files'][name]
            records.append({'runtime_path': str(stage.WLAN_PATH / name), 'sha256': record['sha256'],
                            'bytes': record['bytes'], 'changed': changed})
        return ('WLAN_STAGE_RESULT ' + json.dumps(records) + '\nFAST_WLAN_STAGE_SUCCESS\nGUEST_OK\n').encode('utf-8')

    def save_delta(self, output, indices=(0,)):
        before = bytes(stage.BLOCK_BYTES)
        after = b'W' * stage.BLOCK_BYTES
        old_hash, new_hash = stage.digest(before), stage.digest(after)
        (output / 'sb-old.bin').write_bytes(before * len(indices))
        (output / 'sb-new.bin').write_bytes(after * len(indices))
        (output / 'sb-list.txt').write_bytes(''.join(f'{index} {old_hash} {new_hash}\n' for index in indices).encode('ascii'))
        (output / 'sb-undo-list.txt').write_bytes(''.join(f'{index} {new_hash} {old_hash}\n' for index in indices).encode('ascii'))
        return before, after

    def test_valid_manifest_reads_all_five_physical_files(self):
        manifest, checksum = stage.load_manifest(self.manifest_path)
        self.assertEqual(manifest, self.manifest)
        self.assertEqual(checksum, stage.digest(self.manifest_path.read_bytes()))
        self.assertEqual(len(stage.asset_names(manifest)), 5)

    def test_windows_ctime_difference_preserves_identity_and_mutation_checks(self):
        before = SimpleNamespace(st_dev=1, st_ino=2, st_size=3, st_mtime_ns=4, st_ctime_ns=5)
        after = SimpleNamespace(st_dev=1, st_ino=2, st_size=3, st_mtime_ns=4, st_ctime_ns=6)
        self.assertEqual(stage.stat_identity(before), stage.stat_identity(after))
        for key in ('st_dev', 'st_ino', 'st_size', 'st_mtime_ns'):
            mutated = copy.copy(after)
            setattr(mutated, key, getattr(mutated, key) + 1)
            self.assertNotEqual(stage.stat_identity(before), stage.stat_identity(mutated))

    def test_input_mutation_during_read_is_rejected(self):
        path = self.inputs / 'm3.bin'
        original_fstat = stage.os.fstat
        calls = []

        def changing_fstat(descriptor):
            metadata = original_fstat(descriptor)
            calls.append(descriptor)
            values = {key: getattr(metadata, key) for key in
                      ('st_mode', 'st_dev', 'st_ino', 'st_size', 'st_mtime_ns', 'st_ctime_ns')}
            if len(calls) == 2:
                values['st_mtime_ns'] += 1
            return SimpleNamespace(**values)

        with patch.object(stage.os, 'fstat', side_effect=changing_fstat), self.assertRaises(ValueError):
            stage.read_verified_file(path, self.manifest['files']['m3.bin'])

    def test_missing_required_manifest_fields_fail(self):
        for key in ('root_path', 'bdf_basename', 'required', 'selection_provenance', 'm3_provenance', 'files'):
            with self.subTest(key=key):
                manifest = copy.deepcopy(self.manifest)
                del manifest[key]
                with self.assertRaises(ValueError):
                    stage.validate_manifest_schema(manifest)

    def test_missing_m3_or_file_metadata_fail(self):
        manifest = copy.deepcopy(self.manifest)
        del manifest['files']['m3.bin']
        with self.assertRaises(ValueError):
            stage.validate_manifest_schema(manifest)
        for key in ('sha256', 'bytes', 'host_path'):
            with self.subTest(key=key):
                manifest = copy.deepcopy(self.manifest)
                del manifest['files']['m3.bin'][key]
                with self.assertRaises(ValueError):
                    stage.validate_manifest_schema(manifest)

    def test_empty_provenance_fails(self):
        for key in ('selection_provenance', 'm3_provenance'):
            for value in ({}, {'source': '  '}, '', '  ', None, False, {'verified': True}):
                with self.subTest(key=key, value=value):
                    manifest = copy.deepcopy(self.manifest)
                    manifest[key] = value
                    with self.assertRaises(ValueError):
                        stage.validate_manifest_schema(manifest)

    def test_bdf_paths_collisions_and_unsafe_names_fail(self):
        for name in ('../board.bin', '/board.bin', 'dir/board.bin', 'dir\\board.bin', '.', '..',
                     'C:\\board.bin', 'board name.bin', 'board;id', 'board\n.bin', '-board.bin',
                     'AMSS.BIN', 'm3.bin', 'a' * 129):
            with self.subTest(name=name):
                manifest = copy.deepcopy(self.manifest)
                manifest['bdf_basename'] = name
                with self.assertRaises(ValueError):
                    stage.validate_manifest_schema(manifest)

    def test_root_and_strict_filename_set_fail(self):
        for root in ('/data/ios16-codex.img', stage.ROOT_PATH + '/..', '', None):
            with self.subTest(root=root):
                manifest = copy.deepcopy(self.manifest)
                manifest['root_path'] = root
                with self.assertRaises(ValueError):
                    stage.validate_manifest_schema(manifest)
        manifest = copy.deepcopy(self.manifest)
        manifest['files']['../amss.bin'] = manifest['files'].pop('amss.bin')
        with self.assertRaises(ValueError):
            stage.validate_manifest_schema(manifest)
        manifest = copy.deepcopy(self.manifest)
        manifest['files']['unexpected.bin'] = dict(manifest['files']['amss.bin'])
        with self.assertRaises(ValueError):
            stage.validate_manifest_schema(manifest)

    def test_duplicate_required_json_and_host_paths_fail(self):
        manifest = copy.deepcopy(self.manifest)
        manifest['required'][-1] = manifest['required'][0]
        with self.assertRaises(ValueError):
            stage.validate_manifest_schema(manifest)
        contents = self.manifest_path.read_bytes().replace(b'"m3_provenance":', b'"m3_provenance": "duplicate", "m3_provenance":', 1)
        self.manifest_path.write_bytes(contents)
        with self.assertRaises(ValueError):
            stage.load_manifest(self.manifest_path)
        with self.assertRaises(ValueError):
            stage.decode_json(b'{"files":{"m3.bin":{},"m3.bin":{}}}')
        manifest = copy.deepcopy(self.manifest)
        manifest['files']['m3.bin']['host_path'] = manifest['files']['amss.bin']['host_path']
        with self.assertRaises(ValueError):
            stage.validate_manifest_schema(manifest)

    def test_nonabsolute_host_paths_and_bad_hashes_fail(self):
        for path in ('relative.bin', 'C:relative.bin', '', 'bad\npath', None):
            with self.subTest(path=path):
                manifest = copy.deepcopy(self.manifest)
                manifest['files']['m3.bin']['host_path'] = path
                with self.assertRaises(ValueError):
                    stage.validate_manifest_schema(manifest)
        for checksum in ('', 'a' * 63, 'g' * 64, 'A' * 64, None):
            with self.subTest(checksum=checksum):
                manifest = copy.deepcopy(self.manifest)
                manifest['files']['m3.bin']['sha256'] = checksum
                with self.assertRaises(ValueError):
                    stage.validate_manifest_schema(manifest)

    def test_file_and_m3_bounds_reject_bad_types_and_lengths(self):
        for name in ('amss.bin', 'm3.bin'):
            maximum = stage.MAX_M3_BYTES if name == 'm3.bin' else stage.MAX_FILE_BYTES
            for length in (0, -1, True, 1.0, maximum + 1):
                with self.subTest(name=name, length=length):
                    manifest = copy.deepcopy(self.manifest)
                    manifest['files'][name]['bytes'] = length
                    with self.assertRaises(ValueError):
                        stage.validate_manifest_schema(manifest)
            manifest = copy.deepcopy(self.manifest)
            manifest['files'][name]['bytes'] = maximum
            stage.validate_manifest_schema(manifest)

    def test_total_bound_and_exact_boundary(self):
        manifest = copy.deepcopy(self.manifest)
        manifest['files']['amss.bin']['bytes'] = stage.MAX_FILE_BYTES
        manifest['files']['amss20.bin']['bytes'] = stage.MAX_FILE_BYTES
        with self.assertRaises(ValueError):
            stage.validate_manifest_schema(manifest)
        other_bytes = sum(record['bytes'] for name, record in manifest['files'].items()
                          if name not in ('amss.bin', 'amss20.bin'))
        manifest['files']['amss20.bin']['bytes'] -= other_bytes
        stage.validate_manifest_schema(manifest)

    def test_physical_corruption_short_missing_and_wrong_length_fail(self):
        path = self.inputs / 'm3.bin'
        original = path.read_bytes()
        for contents in (b'X' + original[1:], original[:-1], original + b'X'):
            with self.subTest(length=len(contents)):
                path.write_bytes(contents)
                with self.assertRaises(ValueError):
                    stage.load_manifest(self.manifest_path)
        path.unlink()
        with self.assertRaises(FileNotFoundError):
            stage.load_manifest(self.manifest_path)

    def test_lf_nonfinite_json_and_text_bounds_fail(self):
        original = self.manifest_path.read_bytes()
        for contents in (original.replace(b'\n', b'\r\n'), original.rstrip(b'\n'), b'{}\r\n',
                         b' ' * (stage.MAX_TEXT_BYTES + 1) + b'\n'):
            with self.subTest(length=len(contents)):
                self.manifest_path.write_bytes(contents)
                with self.assertRaises(ValueError):
                    stage.load_manifest(self.manifest_path)
        with self.assertRaises(ValueError):
            stage.decode_json(b'{"value":NaN}')

    def test_manifest_and_input_mutation_before_copy_are_rejected(self):
        manifest, checksum = stage.load_manifest(self.manifest_path)
        self.manifest_path.write_bytes(self.manifest_path.read_bytes() + b'\n')
        with self.assertRaises(ValueError):
            stage.load_manifest(self.manifest_path, checksum)
        self.save_manifest()
        sources = stage.snapshot_sources(phone)
        path = self.inputs / 'm3.bin'
        path.write_bytes(b'X' + path.read_bytes()[1:])
        output = self.directory / 'bundle'
        output.mkdir()
        with self.assertRaises(ValueError):
            stage.prepare_share(output, self.manifest_path, manifest, checksum, sources)
        self.assertFalse((output / 'share').exists())

    def test_staged_bundle_mutation_extra_file_and_source_lf_fail(self):
        share, hashes = self.make_share()
        source = share / 'stage-wlan-assets-guest.sh'
        original = source.read_bytes()
        source.write_bytes(original.replace(b'\n', b'\r\n'))
        with self.assertRaises(ValueError):
            stage.validate_share(share)
        with self.assertRaises(ValueError):
            stage.verify_bundle(share, hashes)
        source.write_bytes(original)
        (share / 'extra.bin').write_bytes(b'EXTRA')
        with self.assertRaises(ValueError):
            stage.verify_bundle(share, hashes)

    def test_checksum_duplicates_missing_hash_and_corruption_fail(self):
        share, unused_hashes = self.make_share()
        checksum_file = share / 'input-sha256.txt'
        original = checksum_file.read_bytes()
        for contents in (original + original.splitlines(keepends=True)[0],
                         b''.join(original.splitlines(keepends=True)[1:]),
                         original.replace(b'assets/m3.bin', b'assets/../m3.bin')):
            with self.subTest(contents=contents[:30]):
                checksum_file.write_bytes(contents)
                with self.assertRaises(ValueError):
                    stage.validate_share(share)

    def test_source_receipt_tracks_original_and_lf_copy(self):
        sources = stage.snapshot_sources(phone)
        stage.verify_sources(sources)
        for source in sources.values():
            self.assertNotIn(b'\r', source['contents'])
            self.assertEqual(stage.digest(source['contents']), source['copied_sha256'])
        copied = dict(sources['stage-wlan-assets-guest.sh'])
        path = self.directory / 'source.sh'
        path.write_bytes(copied['contents'] + b'\n')
        copied['path'] = str(path)
        with self.assertRaises(ValueError):
            stage.verify_sources({'source.sh': copied})

    def test_guest_changes_only_allowed_files_and_is_idempotent(self):
        share, hashes = self.make_share()
        root = self.make_root()
        before = {path.relative_to(root).as_posix(): path.read_bytes() for path in root.rglob('*') if path.is_file()}
        target_directory = stage.checked_directory(root)
        for name in stage.asset_names(self.manifest):
            (target_directory / name).write_bytes(b'OLD_WLAN_ASSET')
        with self.guest_metadata_mock() as (owner, permissions):
            records = stage.install_guest_assets(root, share)
            self.assertTrue(all(record['changed'] for record in records))
            self.assertEqual(owner.call_count, 5)
            self.assertEqual(permissions.call_count, 5)
            for call in owner.call_args_list:
                self.assertEqual(call.args[1:], (0, 0))
                self.assertEqual(Path(call.args[0]).parent, target_directory)
            for call in permissions.call_args_list:
                self.assertEqual(call.args[1], 0o644)
                self.assertEqual(Path(call.args[0]).parent, target_directory)
            owner.reset_mock()
            permissions.reset_mock()
            records = stage.install_guest_assets(root, share)
            self.assertFalse(any(record['changed'] for record in records))
            owner.assert_not_called()
            permissions.assert_not_called()
        for name, contents in before.items():
            self.assertEqual((root / name).read_bytes(), contents)
        expected_files = set(before) | {str(stage.WLAN_PATH / name).lstrip('/') for name in stage.asset_names(self.manifest)}
        actual_files = {path.relative_to(root).as_posix() for path in root.rglob('*') if path.is_file()}
        self.assertEqual(actual_files, expected_files)
        stage.verify_bundle(share, hashes)

    def test_guest_corrupt_input_fails_before_root_changes(self):
        share, unused_hashes = self.make_share()
        root = self.make_root()
        path = share / 'assets/m3.bin'
        path.write_bytes(b'X' + path.read_bytes()[1:])
        before = {str(path): path.read_bytes() for path in root.rglob('*') if path.is_file()}
        with self.guest_metadata_mock(), self.assertRaises(ValueError):
            stage.install_guest_assets(root, share)
        self.assertEqual(before, {str(path): path.read_bytes() for path in root.rglob('*') if path.is_file()})

    def test_guest_rejects_target_symlinks_reparse_points_and_directories(self):
        share, unused_hashes = self.make_share()
        root = self.make_root()
        target_directory = stage.checked_directory(root)
        bad_target = target_directory / 'm3.bin'
        original_lstat = Path.lstat
        for bad_path, mode, attributes in (
            (bad_target, stat.S_IFLNK, 0),
            (target_directory.parent, stat.S_IFLNK, 0),
            (target_directory.parent, stat.S_IFDIR, 1024),
        ):
            def fake_lstat(path, *arguments, **options):
                if path == bad_path:
                    return SimpleNamespace(st_mode=mode, st_file_attributes=attributes)
                return original_lstat(path, *arguments, **options)

            with self.subTest(path=bad_path, mode=mode, attributes=attributes):
                with patch.object(Path, 'lstat', new=fake_lstat), self.guest_metadata_mock(), self.assertRaises(ValueError):
                    stage.install_guest_assets(root, share)
                self.assertFalse((target_directory / 'amss.bin').exists())
        bad_target.mkdir()
        with self.guest_metadata_mock(), self.assertRaises(ValueError):
            stage.install_guest_assets(root, share)
        self.assertFalse((target_directory / 'amss.bin').exists())

    def test_failed_atomic_replace_cleans_only_its_temporary(self):
        share, unused_hashes = self.make_share()
        root = self.make_root()
        target_directory = stage.checked_directory(root)
        target = target_directory / 'amss.bin'
        target.write_bytes(b'KEEP_PREVIOUS_ASSET')
        with self.guest_metadata_mock(), patch.object(Path, 'replace', side_effect=OSError('Offline replace failure')):
            with self.assertRaises(OSError):
                stage.install_guest_assets(root, share)
        self.assertEqual(target.read_bytes(), b'KEEP_PREVIOUS_ASSET')
        self.assertFalse(list(target_directory.glob('.fuxi-wlan-*')))
        self.assertTrue((target_directory / 'keep.bin').is_file())

    def test_output_requires_new_safe_fixed_parent(self):
        good = self.directory / 'run-wlan-offline-test'
        self.assertEqual(stage.validate_output(good, self.directory), good)
        for path in (self.directory / 'run-other', self.directory / 'run-wlan-a;b',
                     self.directory / 'run-wlan-Upper', self.directory / 'run-wlan--bad',
                     self.directory.parent / 'run-wlan-outside', self.directory / ('run-wlan-' + 'a' * 120)):
            with self.subTest(path=path), self.assertRaises(ValueError):
                stage.validate_output(path, self.directory)
        good.mkdir()
        with self.assertRaises(ValueError):
            stage.validate_output(good, self.directory)

    def test_delta_validates_both_binaries_and_undo_list(self):
        before, after = self.save_delta(self.directory, indices=(0, 17))
        records = stage.validate_delta(self.directory)
        self.assertEqual([record['index'] for record in records], [0, 17])
        self.assertEqual(records[0]['before_sha256'], stage.digest(before))
        self.assertEqual(records[0]['after_sha256'], stage.digest(after))
        for filename in ('sb-old.bin', 'sb-new.bin'):
            path = self.directory / filename
            original = path.read_bytes()
            for contents in (b'X' + original[1:], original[:-1]):
                with self.subTest(filename=filename, length=len(contents)):
                    path.write_bytes(contents)
                    with self.assertRaises(ValueError):
                        stage.validate_delta(self.directory)
            path.write_bytes(original)
        path = self.directory / 'sb-undo-list.txt'
        path.write_bytes(path.read_bytes().replace(b'17 ', b'18 '))
        with self.assertRaises(ValueError):
            stage.validate_delta(self.directory)

    def test_delta_rejects_duplicate_outside_indices_and_crlf(self):
        for indices in ((0, 0), (17, 0), (5120,), (-1,)):
            with self.subTest(indices=indices):
                self.save_delta(self.directory, indices)
                with self.assertRaises(ValueError):
                    stage.validate_delta(self.directory)
        self.save_delta(self.directory)
        path = self.directory / 'sb-list.txt'
        path.write_bytes(path.read_bytes().replace(b'\n', b'\r\n'))
        with self.assertRaises(ValueError):
            stage.validate_delta(self.directory)

    def test_guest_tokens_and_exact_paths_are_required(self):
        log = self.guest_log()
        self.assertEqual(len(stage.guest_records(log, self.manifest)), 5)
        for token in (b'FAST_WLAN_STAGE_SUCCESS', b'GUEST_OK'):
            for replacement in (b'', b'prefix_' + token, token + b'_suffix'):
                with self.subTest(token=token, replacement=replacement), self.assertRaises(ValueError):
                    stage.guest_records(log.replace(token, replacement), self.manifest)
        with self.assertRaises(ValueError):
            stage.guest_records(log.replace(b'/usr/local/share/fuxi/wlan/amss.bin', b'/usr/local/lib/fuxi/libfuxi-sb.dylib'), self.manifest)

    def test_guest_serial_crcrlf_and_duplicate_reports(self):
        log = self.guest_log()
        expected = stage.guest_records(log, self.manifest)
        for ending in (b'\r\n', b'\r\r\n'):
            converted = log.replace(b'\n', ending)
            self.assertEqual(stage.guest_records(converted, self.manifest), expected)
            with self.assertRaises(ValueError):
                stage.guest_records(converted + converted.split(ending)[0] + ending, self.manifest)
            for token in (b'FAST_WLAN_STAGE_SUCCESS', b'GUEST_OK'):
                with self.assertRaises(ValueError):
                    stage.guest_records(converted.replace(token, b'echo ' + token), self.manifest)

    def test_guest_script_ast_and_three_fsck_rules_match_existing(self):
        script = (DIRECTORY / 'stage-wlan-assets-guest.sh').read_text(encoding='utf-8')
        existing = (DIRECTORY / 'stage-shape.sh').read_text(encoding='utf-8')
        check_function = script[script.index('check() {'):script.index('\n}\n', script.index('check() {')) + 3]
        existing_function = existing[existing.index('check() {'):existing.index('\n}\n', existing.index('check() {')) + 3]
        self.assertEqual(check_function, existing_function)
        expected_order = (
            'python3 "$P" /dev/vda --apply', 'check before', '--mark-unmounted',
            'mount -t hfsplus -o force,rw /dev/vda /dst', '--guest-stage',
            'umount /dst', 'check after', '--mark-journaled', 'check final',
            'echo FAST_WLAN_STAGE_SUCCESS',
        )
        positions = [script.index(value) for value in expected_order]
        self.assertEqual(positions, sorted(positions))
        self.assertEqual(script.count('\ncheck '), 3)
        syntax = ast.parse((DIRECTORY / 'stage-wlan-assets.py').read_bytes())
        helper = next(node for node in syntax.body if isinstance(node, ast.FunctionDef) and node.name == 'install_guest_assets')
        helper_text = ast.unparse(helper)
        self.assertIn('temporary.replace(target)', helper_text)
        self.assertNotIn('plistlib', helper_text)
        self.assertNotIn('dylib', helper_text)

    def test_fresh_mapping_uses_existing_verifier_and_fixed_root_identity(self):
        candidate = {'path': stage.ROOT_PATH, 'size': stage.ROOT_BYTES, 'inode': stage.ROOT_INODE}
        mapping = {'serial': stage.SERIAL, 'items': [candidate], 'dm': [], 'pin': 'verified'}

        def mapped_adb(serial, command):
            if command.startswith('readlink '):
                return b'/dev/block/dm-99\n'
            if command.startswith('blockdev '):
                return f'{stage.ROOT_BYTES}\n'.encode('ascii')
            return b'/dev/block/other /other ext4 rw 0 0\n'

        fake_phone = SimpleNamespace(ROOT=phone.ROOT, MAPPER=phone.MAPPER,
                                     run=Mock(return_value=(json.dumps(mapping) + '\n').encode('utf-8')),
                                     adb=Mock(side_effect=mapped_adb))
        result = stage.verify_mapping(fake_phone, stage.SERIAL, self.directory, 'mapping-good.json')
        self.assertEqual(result, (candidate, '/dev/block/dm-99'))
        invocation = fake_phone.run.call_args.args[0]
        self.assertEqual(invocation[-1], phone.ROOT / 'work/xiaomi13-ios-gpu-20261108/verify-native-map.py')
        self.assertIn('runpy.run_path', invocation[-2])
        for key, value in (('path', '/data/other.img'), ('size', 1), ('inode', stage.ROOT_INODE + 1)):
            with self.subTest(key=key):
                bad_mapping = copy.deepcopy(mapping)
                bad_mapping['items'][0][key] = value
                fake_phone.run.return_value = (json.dumps(bad_mapping) + '\n').encode('utf-8')
                fake_phone.adb.reset_mock()
                with self.assertRaises(ValueError):
                    stage.verify_mapping(fake_phone, stage.SERIAL, self.directory, f'mapping-bad-{key}.json')
                fake_phone.adb.assert_not_called()

    @contextmanager
    def mock_pipeline(self, log=None, mutate_input=False, wrong_precondition=False, corrupt_remote=False):
        output = self.directory / 'run-wlan-mock-test'
        remote_data = {}
        image = {0: bytes(stage.BLOCK_BYTES)}
        writes = []
        commands = []
        original_validate_output = stage.validate_output
        fake_phone = SimpleNamespace(
            ROOT=phone.ROOT, BLOCK=phone.BLOCK, SIZE=phone.SIZE, QEMU_IMG=phone.QEMU_IMG,
            MAPPER=phone.MAPPER, MANAGER=phone.MANAGER, loaded_source_sha256=phone.loaded_source_sha256,
            PhoneBlocks=phone.PhoneBlocks, nbd=Mock(side_effect=lambda blocks: nullcontext(('nbd://127.0.0.1:1', []))),
            phone_locks=Mock(side_effect=lambda label: nullcontext()), say=Mock(),
        )

        def fake_run(arguments, **options):
            if arguments[0] == 'adb':
                if arguments[3] == 'shell':
                    return b'1\n'
                if arguments[3] == 'push':
                    remote_data[str(arguments[5])] = Path(arguments[4]).read_bytes()
                    return b'PUSHED\n'
            Path(arguments[-1]).write_bytes(b'OFFLINE_QCOW2_PLACEHOLDER')
            return b''

        def fake_adb(serial, command):
            commands.append(command)
            if command.startswith('sha256sum '):
                paths = command.split()[1:]
                if corrupt_remote:
                    remote_data[paths[0]] = b'CORRUPT_REMOTE'
                return ''.join(f'{stage.digest(remote_data[path])}  {path}\n' for path in paths).encode('ascii')
            if command.startswith('sh '):
                script_path, list_path, data_path = command.split()[1:]
                self.assertTrue(script_path.endswith('/sb-blocks-write.sh'))
                rows = [line.split() for line in remote_data[list_path].decode('ascii').splitlines()]
                if wrong_precondition:
                    image[0] = b'P' * stage.BLOCK_BYTES
                for position, row in enumerate(rows):
                    index, before_hash, after_hash = int(row[0]), row[1], row[2]
                    region = remote_data[data_path][position * stage.BLOCK_BYTES:(position + 1) * stage.BLOCK_BYTES]
                    if stage.digest(image[index]) != before_hash:
                        raise subprocess.CalledProcessError(12, command, output=f'PRE_MISMATCH {index}\n'.encode('ascii'))
                    self.assertEqual(stage.digest(region), after_hash)
                for position, row in enumerate(rows):
                    image[int(row[0])] = remote_data[data_path][position * stage.BLOCK_BYTES:(position + 1) * stage.BLOCK_BYTES]
                    writes.append(int(row[0]))
                return f'PRE_OK {len(rows)}\nWRITE_OK {len(rows)}\n'.encode('ascii')
            return b'OFFLINE_PHONE_MOCK\n'

        def fake_guest(arguments, stdout, **options):
            stdout.write(self.guest_log() if log is None else log)
            if mutate_input:
                path = self.inputs / 'm3.bin'
                path.write_bytes(b'X' + path.read_bytes()[1:])
            return SimpleNamespace(returncode=0)

        fake_phone.run = Mock(side_effect=fake_run)
        fake_phone.adb = Mock(side_effect=fake_adb)
        fake_phone.extract = Mock(side_effect=lambda overlay, blocks, target: self.save_delta(target))
        arguments = argparse.Namespace(output=str(output), manifest=str(self.manifest_path), serial=stage.SERIAL, port=34616)
        mapping = ({'path': stage.ROOT_PATH, 'size': stage.ROOT_BYTES, 'inode': stage.ROOT_INODE}, '/dev/block/dm-99')
        with patch.object(stage, 'validate_output', side_effect=lambda path: original_validate_output(path, self.directory)):
            with patch.object(stage, 'load_phone', return_value=fake_phone), patch.object(stage, 'verify_mapping', return_value=mapping):
                with patch.object(stage.subprocess, 'run', side_effect=fake_guest):
                    yield arguments, fake_phone, output, writes, commands, image

    def test_mocked_pipeline_writes_verified_delta_and_receipt(self):
        with self.mock_pipeline() as (arguments, fake_phone, output, writes, commands, image):
            receipt = stage.stage(arguments)
            self.assertEqual(receipt['state'], 'written')
            self.assertEqual(writes, [0])
            self.assertEqual(stage.digest(image[0]), receipt['changed_blocks'][0]['after_sha256'])
            self.assertTrue((output / 'sb-old.bin').is_file())
            self.assertTrue((output / 'delta.json').is_file())
            self.assertEqual(stage.decode_json((output / 'receipt.json').read_bytes()), receipt)
            fake_phone.extract.assert_called_once()
            cleanup = next(command for command in commands if command.startswith('rm '))
            self.assertNotIn('-r', cleanup.split())
            self.assertNotIn('*', cleanup)
            self.assertIn('/sb-new.bin', cleanup)
            self.assertIn('/sb-list.txt', cleanup)
            self.assertIn('/sb-blocks-write.sh', cleanup)

    def test_mocked_pipeline_blocks_input_mutation_before_extraction(self):
        with self.mock_pipeline(mutate_input=True) as (arguments, fake_phone, output, writes, commands, image):
            with self.assertRaises(ValueError):
                stage.stage(arguments)
            fake_phone.extract.assert_not_called()
            self.assertEqual(writes, [])
            self.assertFalse((output / 'receipt.json').exists())

    def test_mocked_pipeline_blocks_missing_guest_tokens_before_extraction(self):
        for token in (b'FAST_WLAN_STAGE_SUCCESS', b'GUEST_OK'):
            with self.subTest(token=token):
                output = self.directory / 'run-wlan-mock-test'
                with self.mock_pipeline(log=self.guest_log().replace(token, b'')) as (arguments, fake_phone, unused_output, writes, commands, image):
                    with self.assertRaises(ValueError):
                        stage.stage(arguments)
                    fake_phone.extract.assert_not_called()
                    self.assertEqual(writes, [])
                output.rename(self.directory / ('failed-' + token.decode('ascii').lower()))

    def test_mocked_pipeline_refuses_wrong_precondition_without_writes(self):
        with self.mock_pipeline(wrong_precondition=True) as (arguments, fake_phone, output, writes, commands, image):
            with self.assertRaises(subprocess.CalledProcessError):
                stage.stage(arguments)
            self.assertEqual(writes, [])
            self.assertIn(b'PRE_MISMATCH', (output / 'write.log').read_bytes())
            self.assertFalse((output / 'receipt.json').exists())

    def test_mocked_pipeline_refuses_remote_corruption_without_writes(self):
        with self.mock_pipeline(corrupt_remote=True) as (arguments, fake_phone, output, writes, commands, image):
            with self.assertRaises(ValueError):
                stage.stage(arguments)
            self.assertEqual(writes, [])
            self.assertFalse((output / 'receipt.json').exists())

    def test_mocked_pipeline_unchanged_assets_skip_extraction_and_writes(self):
        with self.mock_pipeline(log=self.guest_log(changed=False)) as (arguments, fake_phone, output, writes, commands, image):
            receipt = stage.stage(arguments)
            self.assertEqual(receipt['state'], 'unchanged')
            fake_phone.extract.assert_not_called()
            self.assertEqual(writes, [])
            self.assertFalse(any(command.startswith('mkdir /data/local/tmp/') for command in commands))
            self.assertEqual(stage.decode_json((output / 'delta.json').read_bytes()), [])

    def test_invalid_manifest_does_not_reach_phone_or_locks(self):
        self.manifest['m3_provenance'] = ''
        self.save_manifest()
        arguments = argparse.Namespace(output=str(self.directory / 'run-wlan-invalid'), manifest=str(self.manifest_path), serial=stage.SERIAL, port=34616)
        original_validate_output = stage.validate_output
        with patch.object(stage, 'validate_output', side_effect=lambda path: original_validate_output(path, self.directory)):
            with patch.object(stage, 'load_phone') as loader, self.assertRaises(ValueError):
                stage.stage(arguments)
            loader.assert_not_called()

    def test_localhost_nbd_rejects_writes_and_reads_only_requested_blocks(self):
        calls = []

        def fetch(index):
            calls.append(index)
            return bytes([index % 251]) * stage.BLOCK_BYTES

        def receive(connection, length):
            result = bytearray()
            while len(result) < length:
                contents = connection.recv(length - len(result))
                if not contents:
                    raise EOFError('Offline NBD connection closed')
                result.extend(contents)
            return bytes(result)

        blocks = phone.PhoneBlocks(self.directory / 'cache', fetch)
        with phone.nbd(blocks) as (address, failures):
            port = int(address.rsplit(':', 1)[1])
            with socket.create_connection(('127.0.0.1', port), timeout=5) as connection:
                greeting = receive(connection, 152)
                self.assertEqual(greeting[:8], b'NBDMAGIC')
                connection.sendall(struct.pack('>IIQQI', 0x25609513, 1, 1, 0, 4) + b'TEST')
                self.assertEqual(struct.unpack('>IIQ', receive(connection, 16))[1], 1)
                self.assertEqual(calls, [])
                connection.sendall(struct.pack('>IIQQI', 0x25609513, 0, 2, stage.BLOCK_BYTES - 3, 7))
                self.assertEqual(struct.unpack('>IIQ', receive(connection, 16))[1], 0)
                self.assertEqual(receive(connection, 7), bytes(3) + bytes([1]) * 4)
                connection.sendall(struct.pack('>IIQQI', 0x25609513, 2, 3, 0, 0))
            self.assertEqual(failures, [])
        self.assertEqual(calls, [0, 1])
        for offset, length in ((-1, 1), (stage.ROOT_BYTES, 1), (0, -1)):
            with self.subTest(offset=offset, length=length), self.assertRaises(ValueError):
                blocks.read(offset, length)

    def test_local_qcow2_delta_extraction_preserves_readonly_base(self):
        qemu_io = phone.QEMU_IMG.with_name('qemu-io.exe')
        if not phone.QEMU_IMG.is_file() or not qemu_io.is_file():
            self.skipTest('Local qemu-img/qemu-io are unavailable')
        calls = []

        def fetch(index):
            calls.append(index)
            return bytes([index % 251]) * stage.BLOCK_BYTES

        blocks = phone.PhoneBlocks(self.directory / 'cache', fetch)
        with phone.nbd(blocks) as (address, failures):
            overlay = self.directory / 'overlay.qcow2'
            phone.run([phone.QEMU_IMG, 'create', '-q', '-f', 'qcow2', '-b', address, '-F', 'raw', overlay], timeout=30)
            phone.run([qemu_io, '-f', 'qcow2', '-c', 'write -P 0x57 4096 4096', overlay], timeout=30)
            phone.extract(overlay, blocks, self.directory)
            records = stage.validate_delta(self.directory)
            self.assertEqual([record['index'] for record in records], [0])
            self.assertEqual((self.directory / 'sb-old.bin').read_bytes(), bytes(stage.BLOCK_BYTES))
            expected = bytes(4096) + b'W' * 4096 + bytes(stage.BLOCK_BYTES - 8192)
            self.assertEqual((self.directory / 'sb-new.bin').read_bytes(), expected)
            self.assertEqual(blocks.block(0), bytes(stage.BLOCK_BYTES))
            self.assertFalse(failures)
        self.assertLess(len(calls), 8)
        self.assertEqual(len(calls), len(set(calls)))


class BluetoothStageTests(WlanStageTests):
    """kind=bt：同一條覆蓋層流程部署 WCN7850 rampatch 與 NVM 到 /usr/local/share/fuxi/bt。"""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='fuxi-bt-stage-test-')
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name).resolve()
        self.inputs = self.directory / 'inputs'
        self.inputs.mkdir()
        self.manifest_path = self.directory / 'manifest.json'
        names = ['hmtbtfw20.tlv', 'hmtbtfw20.ver', 'hmtnv20.b0a', 'hmtnv20.b10a', 'hmtnv20.bin']
        self.manifest = {
            'kind': 'bt', 'root_path': stage.ROOT_PATH, 'required': names,
            'provenance': {'source': 'Offline Bluetooth fixture only'}, 'files': {},
        }
        for position, name in enumerate(names):
            contents = (f'OFFLINE_BT_FIXTURE_{position}_{name}\n').encode('ascii') * 5
            path = self.inputs / name
            path.write_bytes(contents)
            self.manifest['files'][name] = {
                'host_path': str(path), 'bytes': len(contents), 'sha256': stage.digest(contents),
            }
        self.save_manifest()

    def guest_log(self, changed=True):
        records = []
        for name in stage.asset_names(self.manifest):
            record = self.manifest['files'][name]
            records.append({'runtime_path': str(stage.BT_PATH / name), 'sha256': record['sha256'],
                            'bytes': record['bytes'], 'changed': changed})
        return ('WLAN_STAGE_RESULT ' + json.dumps(records) + '\nFAST_WLAN_STAGE_SUCCESS\nGUEST_OK\n').encode('utf-8')

    def test_bt_manifest_names_bounds_and_kind(self):
        manifest, unused_checksum = stage.load_manifest(self.manifest_path)
        self.assertEqual(stage.asset_names(manifest), tuple(self.manifest['required']))
        self.assertEqual(stage.target_path(manifest), stage.BT_PATH)
        bad_cases = []
        for names in (['hmtbtfw20.tlv'], ['hmtnv20.bin', 'hmtnv20.b0a'], ['hmtbtfw20.tlv', 'hmtbtfw20.ver'],
                      ['hmtbtfw20.tlv', 'hmtnv20.b0a', 'hmtnv20.B0A'], ['hmtbtfw20.tlv', '../hmtnv20.bin'],
                      ['hmtbtfw20.tlv', 'hmtnv20.bin', 'amss.bin'], ['hmtbtfw20.tlv', 'hmtnv20.b12345']):
            bad_cases.append(('required', names))
        for key, value in bad_cases:
            manifest = copy.deepcopy(self.manifest)
            manifest[key] = value
            manifest['files'] = {name: self.manifest['files'].get(name, self.manifest['files']['hmtnv20.bin'])
                                 for name in value}
            with self.subTest(names=value), self.assertRaises(ValueError):
                stage.validate_manifest_schema(manifest)
        for mutate in (lambda m: m.update(kind='other'), lambda m: m.update(provenance={}),
                       lambda m: m.update(bdf_basename='board-2.bin'),
                       lambda m: m['files']['hmtnv20.b0a'].update(bytes=stage.BT_MAX_NVM_BYTES + 1),
                       lambda m: m['files']['hmtbtfw20.tlv'].update(bytes=stage.BT_MAX_PATCH_BYTES + 1),
                       lambda m: m['files'].pop('hmtnv20.bin')):
            manifest = copy.deepcopy(self.manifest)
            mutate(manifest)
            with self.assertRaises(ValueError):
                stage.validate_manifest_schema(manifest)
        manifest = copy.deepcopy(self.manifest)
        manifest['files']['hmtbtfw20.tlv']['bytes'] = stage.BT_MAX_PATCH_BYTES
        stage.validate_manifest_schema(manifest)

    def test_bt_output_name_must_match_kind(self):
        self.assertEqual(stage.validate_output(self.directory / 'run-bt-offline', self.directory),
                         self.directory / 'run-bt-offline')
        arguments = SimpleNamespace(output=str(self.directory / 'run-wlan-offline'),
                                    manifest=str(self.manifest_path), serial=stage.SERIAL, port=34616)
        original_validate_output = stage.validate_output
        with patch.object(stage, 'validate_output', side_effect=lambda path: original_validate_output(path, self.directory)), \
                patch.object(stage, 'load_phone') as load_phone, self.assertRaises(ValueError):
            stage.stage(arguments)
        load_phone.assert_not_called()

    def test_guest_changes_only_allowed_files_and_is_idempotent(self):
        share, hashes = self.make_share()
        root = self.make_root()
        before = {path.relative_to(root).as_posix(): path.read_bytes() for path in root.rglob('*') if path.is_file()}
        with self.guest_metadata_mock() as (owner, permissions):
            records = stage.install_guest_assets(root, share)
            self.assertTrue(all(record['changed'] for record in records))
            self.assertEqual(owner.call_count, 5)
            self.assertEqual([record['runtime_path'] for record in records],
                             [str(stage.BT_PATH / name) for name in self.manifest['required']])
            owner.reset_mock()
            records = stage.install_guest_assets(root, share)
            self.assertFalse(any(record['changed'] for record in records))
            owner.assert_not_called()
        for name, contents in before.items():
            self.assertEqual((root / name).read_bytes(), contents)
        expected_files = set(before) | {str(stage.BT_PATH / name).lstrip('/') for name in self.manifest['required']}
        self.assertEqual({path.relative_to(root).as_posix() for path in root.rglob('*') if path.is_file()}, expected_files)
        stage.verify_bundle(share, hashes)

    def test_guest_tokens_and_exact_paths_are_required(self):
        log = self.guest_log()
        self.assertEqual(len(stage.guest_records(log, self.manifest)), 5)
        with self.assertRaises(ValueError):
            stage.guest_records(log.replace(b'/usr/local/share/fuxi/bt/', b'/usr/local/share/fuxi/wlan/'), self.manifest)

    # 以下 WLAN 專屬（m3／BDF 欄位、五檔固定名單、run-wlan 名稱）的案例不適用藍牙組合
    test_valid_manifest_reads_all_five_physical_files = None
    test_input_mutation_during_read_is_rejected = None
    test_missing_required_manifest_fields_fail = None
    test_missing_m3_or_file_metadata_fail = None
    test_empty_provenance_fails = None
    test_bdf_paths_collisions_and_unsafe_names_fail = None
    test_root_and_strict_filename_set_fail = None
    test_duplicate_required_json_and_host_paths_fail = None
    test_nonabsolute_host_paths_and_bad_hashes_fail = None
    test_file_and_m3_bounds_reject_bad_types_and_lengths = None
    test_total_bound_and_exact_boundary = None
    test_physical_corruption_short_missing_and_wrong_length_fail = None
    test_guest_corrupt_input_fails_before_root_changes = None
    test_guest_rejects_target_symlinks_reparse_points_and_directories = None
    test_failed_atomic_replace_cleans_only_its_temporary = None
    test_output_requires_new_safe_fixed_parent = None
    test_guest_serial_crcrlf_and_duplicate_reports = None
    test_fresh_mapping_uses_existing_verifier_and_fixed_root_identity = None
    test_mocked_pipeline_writes_verified_delta_and_receipt = None
    test_mocked_pipeline_blocks_input_mutation_before_extraction = None
    test_mocked_pipeline_blocks_missing_guest_tokens_before_extraction = None
    test_mocked_pipeline_refuses_wrong_precondition_without_writes = None
    test_mocked_pipeline_refuses_remote_corruption_without_writes = None
    test_mocked_pipeline_unchanged_assets_skip_extraction_and_writes = None
    test_invalid_manifest_does_not_reach_phone_or_locks = None
    test_local_qcow2_delta_extraction_preserves_readonly_base = None
    test_localhost_nbd_rejects_writes_and_reads_only_requested_blocks = None
    test_delta_validates_both_binaries_and_undo_list = None
    test_delta_rejects_duplicate_outside_indices_and_crlf = None
    test_guest_script_ast_and_three_fsck_rules_match_existing = None
    test_windows_ctime_difference_preserves_identity_and_mutation_checks = None
    test_manifest_and_input_mutation_before_copy_are_rejected = None
    test_checksum_duplicates_missing_hash_and_corruption_fail = None


if __name__ == '__main__':
    unittest.main(verbosity=2)
