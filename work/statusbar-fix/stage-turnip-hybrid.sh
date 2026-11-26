#!/bin/sh
set -eu
trap 'echo TURNIP_STAGE_FAILED' EXIT
P=/mnt/share/hfs-journal-block-probe.py
[ "$(cat /sys/block/vda/ro)" = 0 ]
[ "$(cat /sys/block/vda/size)" = 41943040 ]
(cd /mnt/share && sha256sum -c shape-sha256.txt)

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
from pathlib import Path

source = Path('/mnt/share/libfuxi-sb.dylib')
expected = Path('/mnt/share/shape-sha256.txt').read_text().split()[0]
payload = source.read_bytes()
assert hashlib.sha256(payload).hexdigest() == expected
target = Path('/dst/usr/local/lib/libvulkan_freedreno_hyb.dylib')
assert target.is_file() and not target.is_symlink()
before = hashlib.sha256(target.read_bytes()).hexdigest()
assert before in ('90051d41c6b2ed7497e8618c6cd0447b5973e257898904ce79356d0474cf32a6', '65b032dda535ed4763494c13c5a78a9e7d8e2ade2b7433db7bc910db8ccab702', 'f9f010adb3466d934f32d6328a0cfdcbc946ae125429a78ac1b6090c872e53c3', expected), before
information = target.stat()
assert information.st_uid == 0 and information.st_mode & 0o7777 == 0o755
temporary = target.with_suffix('.fuxitmp')
assert not temporary.exists()
temporary.write_bytes(payload)
os.chown(temporary, information.st_uid, information.st_gid)
os.chmod(temporary, information.st_mode & 0o7777)
temporary.replace(target)
assert hashlib.sha256(target.read_bytes()).hexdigest() == expected
print('TURNIP_INSTALLED', target, 'before', before, 'after', expected)
PY
sync
umount /dst
check after
python3 "$P" /dev/vda --mark-journaled
python3 "$P" /dev/vda | grep -q '^JOURNAL_EMPTY$'
check final
trap - EXIT
echo TURNIP_STAGE_SUCCESS
