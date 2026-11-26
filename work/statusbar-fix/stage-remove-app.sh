#!/bin/sh
# 000 第三方 App 對稱安全移除（guest 端）：對稱 stage-install-app.sh，依 /mnt/share/remove-app.txt 移除 /Applications/<Name>.app，
# 實際 7 層防誤刪與清理在 remove-app-guest.py（主機測試：remove-app-guest.py self-test）；這裡只做 journal／fsck 外框與前後核對，
# 外框與 stage-install-app.sh 相同：先重播 HFS+ journal、fsck、暫時標成未掛載後 rw 掛載，改完再 fsck 並標回 journaled。
# 使用者資料：桌面配置 IconState.plist、系統 App、LaunchDaemons 等都不動；開機後由 lsd 重建並自動同步桌面圖示。
set -eu
trap 'echo REMOVE_APP_STAGE_FAILED' EXIT
P=/mnt/share/hfs-journal-block-probe.py
T=/mnt/share/sweet-root-tweaks.py
G=/mnt/share/remove-app-guest.py
R=/mnt/share/remove-app.txt
[ "$(cat /sys/block/vda/ro)" = 0 ]
[ "$(cat /sys/block/vda/size)" = 41943040 ]
[ -f "$T" ] && [ -f "$G" ] && [ -f "$R" ]
(cd /mnt/share && sha256sum -c tweaks-sha256.txt)

# 掛載前先預檢 /mnt/share/remove-app.txt：嚴格 2 行 ASCII LF、無 CR、非 com.apple.*
[ "$(wc -l < "$R")" = 2 ]
if grep -q "$(printf '\r')" "$R"; then exit 1; fi
[ -z "$(sed -n '3p' "$R")" ]
app_name=$(sed -n '1p' "$R")
bundle_id=$(sed -n '2p' "$R")
printf '%s\n' "$app_name" | grep -qxE '[A-Za-z0-9][A-Za-z0-9._-]{0,63}\.app'
printf '%s\n' "$bundle_id" | grep -qxE '[A-Za-z0-9][A-Za-z0-9.-]{0,127}'
if printf '%s\n' "$bundle_id" | grep -qiE '^com\.apple\.'; then exit 1; fi

check() {
  fsck.hfsplus -fn /dev/vda > "/tmp/fsck-$1.txt" 2>&1 || true
  cat "/tmp/fsck-$1.txt"
  if grep -q "appears to be OK" "/tmp/fsck-$1.txt"; then
    echo "FSCK_$1 OK"
    return 0
  fi
  [ -s "/tmp/fsck-$1.txt" ] || { echo "FSCK_$1 NOT_CLEAN"; return 1; }
  bad=$(grep -vE '^[[:space:]]*$|^\*\* (/dev/|Checking |Detected |Look for )|^[[:space:]]*(Executing fsck_hfs|The volume name is|Orphaned open unlinked file)' "/tmp/fsck-$1.txt" || true)
  if [ -z "$bad" ] && ! grep -qiE 'cannot open|Permission denied|No such file|I/O error' "/tmp/fsck-$1.txt"; then
    echo "FSCK_$1 OK_APART_FROM_ORPHANS"
    return 0
  fi
  echo "FSCK_$1 NOT_CLEAN"
  return 1
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

echo "APPLICATIONS_BEFORE $(ls -d /dst/Applications/*.app | wc -l)"
sha_icon_before=$(sha256sum /dst/private/var/mobile/Library/SpringBoard/IconState.plist 2>/dev/null | cut -d' ' -f1 || true)

python3 -B -I "$G" remove /dst /mnt/share

echo "APPLICATIONS_AFTER $(ls -d /dst/Applications/*.app | wc -l)"
sha_icon_after=$(sha256sum /dst/private/var/mobile/Library/SpringBoard/IconState.plist 2>/dev/null | cut -d' ' -f1 || true)
[ "$sha_icon_before" = "$sha_icon_after" ]
echo "ICONSTATE_UNCHANGED ${sha_icon_after:-absent}"
df -k /dst | tail -1
sync
umount /dst
check after
python3 "$P" /dev/vda --mark-journaled
python3 "$P" /dev/vda | grep -q '^JOURNAL_EMPTY$'
check final
trap - EXIT
echo REMOVE_APP_STAGE_SUCCESS
