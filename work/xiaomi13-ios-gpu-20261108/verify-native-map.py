import os
import itertools, json, re, subprocess, time
from pathlib import Path

SERIAL = os.environ.get('FUXI_ANDROID_SERIAL', '')
BASE = Path(r'D:\xiaomi13\fuxi_ios\work\xiaomi13-ios-gpu-20261108')
paths = ['/data/gsi/codexnative1108b/codexnative_root_1108b.img.0000', '/data/gsi/codexgpu1108a/codexgpu_root_1108a.img.0000', '/data/ios16-codex-b.img', '/data/ios16-gpu-c.img', '/data/ios16-codex.img', '/data/ios27-root.img']

def shell(command):
    return subprocess.run(['adb','-s',SERIAL,'shell','su','-c', command], check=True, text=True, capture_output=True).stdout

items = []
for path in paths:
    lines = shell('/data/local/tmp/codex-inspect-fiemap ' + path).strip().splitlines()
    header = re.fullmatch(r'size=(\d+) inode=(\d+) flags=0x([0-9a-fA-F]+)', lines[0])
    if not header: raise ValueError('bad header ' + path)
    size, inode, flags = int(header[1]), int(header[2]), int(header[3], 16)
    if not flags & 0x800000: raise ValueError('not pinned ' + path)
    extents, pos = [], 0
    for line in lines[1:-1]:
        m = re.fullmatch(r'(\d+) (\d+) (\d+) 0x([0-9a-fA-F]+)', line)
        if not m: raise ValueError('bad extent ' + line)
        logical, physical, length, ef = int(m[1]), int(m[2]), int(m[3]), int(m[4],16)
        if logical != pos or not length or (logical | physical | length) & 4095 or ef & ~0x1001 or physical+length > 240597577728: raise ValueError('bad range ' + line)
        extents.append([logical, physical, length, ef]); pos += length
    if pos != size or lines[-1] != f'total={len(extents)} covered={pos}': raise ValueError('incomplete ' + path)
    ordered = sorted((e[1],e[1]+e[2]) for e in extents)
    if any(a[1] > b[0] for a,b in zip(ordered,ordered[1:])): raise ValueError('self overlap ' + path)
    items.append(dict(path=path,size=size,inode=inode,flags=flags,extents=extents))
for a,b in itertools.combinations(items,2):
    aa=sorted((e[1],e[1]+e[2]) for e in a['extents']); bb=sorted((e[1],e[1]+e[2]) for e in b['extents']); i=j=0
    while i<len(aa) and j<len(bb):
        if aa[i][0]<bb[j][1] and bb[j][0]<aa[i][1]: raise ValueError('candidate overlap')
        if aa[i][1]<=bb[j][1]: i+=1
        else: j+=1
gpu=items[0]
if gpu['size'] != 21474836480 or gpu['inode'] != json.loads((BASE/'native-candidate-identity.json').read_text())['inode'] or len(gpu['extents']) > 512: raise ValueError('wrong GPU candidate')
pin = shell('/data/local/tmp/codex-check-pin ' + paths[0])
if 'flags_rc=0' not in pin or 'pin_rc=0 moved=0' not in pin: raise ValueError('pin check failed')
dm = shell('/data/local/tmp/codex-inspect-native-dm').strip().splitlines()
m = re.fullmatch(r'targets=(\d+) flags=0x[0-9a-fA-F]+',dm[0])
if not m or int(m[1]) != len(gpu['extents']) or len(dm)-1 != len(gpu['extents']): raise ValueError('DM target count')
for line, (logical, physical, length, _) in zip(dm[1:], gpu['extents']):
    match=re.fullmatch(r'target=linear start=(\d+) length=(\d+) backing=259:16 offset=(\d+)',line)
    if not match or tuple(map(int,match.groups())) != (logical//512,length//512,physical//512): raise ValueError('DM differs from FIEMAP: '+line)
out=BASE/'managed-native-map-current.json'
out.write_text(json.dumps(dict(time=time.strftime('%Y-%m-%dT%H:%M:%S%z'),items=items,dm=dm,pin=pin),indent=2)+'\n')
print('OK: full FIEMAP coverage; managed DM targets match; pinned; existing candidates disjoint')
print('candidate inode',gpu['inode'],'extents',len(gpu['extents']))

