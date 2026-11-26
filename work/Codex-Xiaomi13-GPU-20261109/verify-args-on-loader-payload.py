"""Check a boot-args-only variant (built by prepare-args-on-loader.py) against the freshly verified phone mapping.

用法：python verify-args-on-loader-payload.py <loader 目錄名>
來源 loader 與核心 artifact 讀自該目錄的 source.txt；預期的 token 差異讀自 swaps.txt。
"""
import hashlib
import json
import struct
import sys
from pathlib import Path

base = Path(r'D:\xiaomi13\fuxi_ios\work\Codex-Xiaomi13-GPU-20261109')
sys.path.insert(0, str(base / 'kernel-source-979' / 'tools'))
import adt

sha = lambda b: hashlib.sha256(b).hexdigest()
out = base / sys.argv[1]
src_name, art_name = (out / 'source.txt').read_text().split()
lines = (out / 'swaps.txt').read_text().splitlines()
adds = [line[1:] for line in lines if line.startswith('+')]
swaps = [tuple(line.split('=>', 1)) for line in lines if not line.startswith('+')]
# managed-native-map-current.json 由 verify-native-map.py 剛寫出（手機現況）
item = json.loads(Path(r'D:\xiaomi13\fuxi_ios\work\xiaomi13-ios-gpu-20261108\managed-native-map-current.json').read_text())['items'][0]
words = [n // 4096 for logical, physical, length, _ in item['extents'] for n in (logical, physical, length)]
expected = struct.pack('<%dQ' % len(words), *words)
ref = (base / src_name / 'payload.bin').read_bytes()
ref_pinned = dict(line.split()[::-1] for line in (base / src_name / 'sha256.txt').read_text().splitlines())
assert sha(ref) == ref_pinned['payload.bin']
pinned = dict(line.split()[::-1] for line in (out / 'sha256.txt').read_text().splitlines())
payload = (out / 'payload.bin').read_bytes()
loader = (out / 'boot.img').read_bytes()
assert sha(payload) == pinned['payload.bin'] and sha(loader) == pinned['boot.img']
# 核心與來源 loader 相同
kernel = (base / art_name / 'kernel.development.fuxi').read_bytes()
assert sha(kernel) == pinned['kernel.development.fuxi'] == ref_pinned['kernel.development.fuxi']
# 除了 boot-args 欄位，payload 與來源逐位元組相同（同核心、同 ADT）
assert len(payload) == len(ref) and payload[:72] == ref[:72] and payload[680:] == ref[680:]
offset, size = struct.unpack_from('<2Q', payload, 32)
root, end = adt.parse(payload[offset:offset + size])
chosen = next(node for node in root.children if node.name == 'chosen')
assert payload[:8] == b'SWEETXNU' and end == size
# ADT 內的 extents 等於手機現況
assert chosen.prop('fuxi-root-extents') == expected
assert chosen.prop('fuxi-root-blocks') == struct.pack('<Q', 21474836480 // 4096)
args = payload[72:680].split(b'\0', 1)[0].decode('ascii').split()
ref_args = ref[72:680].split(b'\0', 1)[0].decode('ascii').split()
# 與來源相比，boot-args 只有 swaps.txt 列出的 token 不同
diff = [(a, b) for a, b in zip(ref_args, args) if a != b]
# 新增的 token 只會出現在最後，且與 swaps.txt 的 + 行一致
assert len(args) == len(ref_args) + len(adds) and diff == swaps and args[len(ref_args):] == adds
# 範圍外的參數不得混入；swaps.txt 以 + 明列的 token（該次實驗的對象，例如實驗 12 的 fuxiperf=1）不算
assert not any(arg.startswith(('sweetexecswap=', 'fuxigpuddr=', 'fuxiperf=', 'sweetfb_hz=', 'fuxiearly=', 'fuxiscante=', 'fuxiscandrain=', 'sweetsurfrow=')) for arg in args if arg not in adds and arg not in [new for _, new in swaps])
assert loader.count(payload) == 1
print('OK:', sys.argv[1], 'contains the verified mapping; same kernel and ADT as', src_name, '; args diff:', diff, 'added:', adds)
print('kernel', pinned['kernel.development.fuxi'])
print('payload', pinned['payload.bin'])
print('boot.img', pinned['boot.img'])
