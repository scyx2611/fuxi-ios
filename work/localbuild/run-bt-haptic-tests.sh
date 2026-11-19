#!/bin/bash
# 藍牙／震動相關主機測試（WSL）：ksrc 的 tools/test-fuxi-{haptic,bluetooth,bt}-*.py
set -u
cd /mnt/d/xiaomi13/fuxi_ios/work/adsp-battery/ksrc || exit 2
failed=0
for test in test-fuxi-haptic-fifo.py test-fuxi-haptic-pulse.py test-fuxi-haptic-runtime.py \
    test-fuxi-haptic-survey.py test-fuxi-bluetooth.py test-fuxi-bluetooth-xnu.py \
    test-fuxi-bluetooth-setup.py test-fuxi-bt-board.py test-fuxi-bt-geni-fifo.py test-fuxi-bt-clock.py; do
    echo "== $test"
    args=()
    case "$test" in
        test-fuxi-bluetooth*.py) args=(--sanitize) ;;
    esac
    if ! CC=clang python3 "tools/$test" "${args[@]}" > "/tmp/$test.log" 2>&1; then
        failed=1
        tail -20 "/tmp/$test.log"
    else
        tail -2 "/tmp/$test.log"
    fi
done
exit $failed
