#!/bin/sh
# 000 launchd（guest 端）：在 iOS root 的 /System/Library/xpc/launchd.plist 把 job.txt 指定的 job 標成 Disabled。
# 被核心 sweetnoexec 擋下的 daemon（fairplayd.H2）仍由 launchd 代收它的 MachServices，送過去的 MIG 訊息永遠等不到回覆；
# 停用後服務名稱不再登記，bootstrap_look_up 直接失敗，呼叫端立刻拿到錯誤而不是卡死。
# 流程與 stage-timezone.sh 相同：先重播 HFS+ journal、fsck、暫時標成未掛載後 rw 掛載，改完再 fsck 並標回 journaled。
set -eu
trap 'echo LAUNCHD_STAGE_FAILED' EXIT
P=/mnt/share/hfs-journal-block-probe.py
[ "$(cat /sys/block/vda/ro)" = 0 ]
[ "$(cat /sys/block/vda/size)" = 41943040 ]
[ "$(wc -l < /mnt/share/job.txt)" = 1 ]
grep -qxE 'com\.apple\.[A-Za-z0-9.]+' /mnt/share/job.txt

check() {
  fsck.hfsplus -fn /dev/vda > "/tmp/fsck-$1.txt" 2>&1 || true
  cat "/tmp/fsck-$1.txt"
  if grep -q "appears to be OK" "/tmp/fsck-$1.txt"; then
    echo "FSCK_$1 OK"
    return 0
  fi
  bad=$(grep -E '^[[:space:]]+[^[:space:]]' "/tmp/fsck-$1.txt" | grep -vE 'Executing fsck_hfs|The volume name is|Orphaned open unlinked file' || true)
  [ -z "$bad" ] || { echo "FSCK_$1 NOT_CLEAN"; return 1; }
  echo "FSCK_$1 OK_APART_FROM_ORPHANS"
}

python3 "$P" /dev/vda --apply > /tmp/replay.txt
cat /tmp/replay.txt
grep -qxE 'JOURNAL_REPLAYED|JOURNAL_EMPTY' /tmp/replay.txt
python3 "$P" /dev/vda | grep -q '^JOURNAL_EMPTY$'
check before
python3 "$P" /dev/vda --mark-unmounted
mkdir -p /dst
mount -t hfsplus -o force,rw /dev/vda /dst
grep -q ' /dst hfsplus rw' /proc/mounts
python3 - <<'PY'
import hashlib
import os
import plistlib
from pathlib import Path

label = Path('/mnt/share/job.txt').read_text().strip()
path = Path('/dst/System/Library/xpc/launchd.plist')
assert path.is_file() and not path.is_symlink()
raw = path.read_bytes()
assert raw[:8] == b'bplist00'
print('LAUNCHD_BEFORE', len(raw), hashlib.sha256(raw).hexdigest())
data = plistlib.loads(raw)
jobs = [job for job in data['LaunchDaemons'].values() if job.get('Label') == label]
assert len(jobs) == 1, ('job count', len(jobs))
job = jobs[0]
print('LAUNCHD_JOB', label, 'Disabled', job.get('Disabled'), 'services', sorted(job.get('MachServices', {})))
if job.get('Disabled') is not True:
    job['Disabled'] = True
    new = plistlib.dumps(data, fmt=plistlib.FMT_BINARY, sort_keys=False)
    check = plistlib.loads(new)
    assert check['LaunchDaemons'].keys() == data['LaunchDaemons'].keys()
    assert [j for j in check['LaunchDaemons'].values() if j.get('Label') == label][0]['Disabled'] is True
    information = path.stat()
    temporary = path.with_name('launchd.plist.fuxitmp')
    if temporary.exists():
        temporary.unlink()
    temporary.write_bytes(new)
    os.chown(temporary, information.st_uid, information.st_gid)
    os.chmod(temporary, information.st_mode & 0o7777)
    with open(temporary, 'rb') as stream:
        os.fsync(stream.fileno())
    os.replace(temporary, path)
after = path.read_bytes()
assert [j for j in plistlib.loads(after)['LaunchDaemons'].values() if j.get('Label') == label][0]['Disabled'] is True
print('LAUNCHD_AFTER', len(after), hashlib.sha256(after).hexdigest())
PY
sync
umount /dst
check after
python3 "$P" /dev/vda --mark-journaled
python3 "$P" /dev/vda | grep -q '^JOURNAL_EMPTY$'
check final
trap - EXIT
echo LAUNCHD_STAGE_SUCCESS
