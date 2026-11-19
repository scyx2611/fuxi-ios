#!/usr/bin/env bash
set -euo pipefail
HARNESS=
VERIFIER=
NAME=sb-local-$(date +%m%d-%H%M%S)-artifact
NOTE=
while [ $# -gt 0 ]; do
    case "$1" in
        --harness) HARNESS=$2; shift 2 ;;
        --verifier) VERIFIER=$2; shift 2 ;;
        --name) NAME=$2; shift 2 ;;
        --note) NOTE=$2; shift 2 ;;
        *) echo "Unknown argument: $1" >&2; exit 2 ;;
    esac
done
[[ "$NAME" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]] || exit 2
TC=/opt/unixnu-linux
SDK=$TC/sdks/iPhoneOS16.5.sdk
SDK_ARCHIVE=$TC/sdks/iPhoneOS16.5.sdk.tar.xz
SDK_SHA=5e0fd3f01266cce4ce012d4a99b38eb56578fca40d09edc81cd83dee958202fb
BUILD=/root/fuxi-build/sb-shape-local
OUT=/mnt/d/xiaomi13/fuxi_ios/work/Codex-Xiaomi13-GPU-20261109/$NAME
CLANG=$TC/llvm/bin/clang
LINKER=/usr/bin/ld64.lld-21
SOURCE=$HARNESS/userland/fuxi-shape
mkdir -p "$BUILD"
exec 9>"$BUILD/.lock"
flock -n 9 || { echo 'Another local dylib build is active' >&2; exit 1; }
[ ! -e "$OUT" ] || { echo "Output already exists: $OUT" >&2; exit 1; }
[ -f "$SOURCE/FuxiShape.c" ] && [ -f "$VERIFIER" ] && [ -x "$CLANG" ] && [ -x "$LINKER" ]
[ "$(sha256sum "$SDK_ARCHIVE" | cut -d' ' -f1)" = "$SDK_SHA" ]
[ -f "$SDK/System/Library/Frameworks/CoreFoundation.framework/Headers/CFError.h" ]
mkdir -p "$OUT/logs"
START=$SECONDS
trap 'printf "result FAILED\nelapsed_seconds %s\n" "$((SECONDS - START))" > "$OUT/BUILD.txt"' ERR
(
    printf '%s\n' "$NOTE" 'sdk theos/sdks master-146e41f iPhoneOS16.5.sdk'
    sha256sum "$SDK_ARCHIVE" "$CLANG" "$LINKER" "$VERIFIER" \
        "$(dirname "$VERIFIER")/fuxi-sb-build.sh" "$(dirname "$VERIFIER")/build-sb-shape.ps1"
    find "$SOURCE" -maxdepth 1 -type f \( -name '*.c' -o -name '*.h' \) -print0 |
        sort -z | xargs -0 sha256sum
) > "$OUT/INPUTS.txt"
clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I"$SOURCE" \
    "$HARNESS/tools/test-flashlight-levels.c" -o "$BUILD/test-flashlight"
ASAN_OPTIONS=detect_leaks=1 "$BUILD/test-flashlight" > "$OUT/logs/flashlight.txt" 2>&1
clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I"$SOURCE" \
    "$HARNESS/tools/test-vibe-pattern.c" -o "$BUILD/test-vibe-pattern"
ASAN_OPTIONS=detect_leaks=1 "$BUILD/test-vibe-pattern" > "$OUT/logs/vibe-pattern.txt" 2>&1
python3 "$HARNESS/tools/test-vibe-worker.py" > "$OUT/logs/vibe-worker.txt" 2>&1
clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I"$SOURCE" \
    "$HARNESS/tools/test-oslog-format.c" -o "$BUILD/test-oslog-format"
ASAN_OPTIONS=detect_leaks=1 "$BUILD/test-oslog-format" > "$OUT/logs/oslog-format.txt" 2>&1
(
    OBJECTS=()
    for unit in FuxiShape FuxiFlashlight FuxiBatteryAudit FuxiVibrate FuxiWifiAudit FuxiBluetoothProbe FuxiBlueTool \
        FuxiBluetoothLog; do
        "$CLANG" -target arm64-apple-ios16.0 -isysroot "$SDK" -O2 -Wall -Wextra -Werror \
            -fstack-protector-strong -c "$SOURCE/$unit.c" -o "$BUILD/$unit.o"
        OBJECTS+=("$BUILD/$unit.o")
    done
    "$LINKER" -arch arm64 -platform_version ios 16.0 16.5 -dylib -syslibroot "$SDK" \
        -L"$SDK/usr/lib" -F"$SDK/System/Library/Frameworks" "${OBJECTS[@]}" \
        -lSystem -lobjc -framework CoreFoundation -framework IOKit -adhoc_codesign \
        -install_name /usr/local/lib/fuxi/libfuxi-sb.dylib -o "$OUT/libfuxi-sb.dylib"
) > "$OUT/logs/build.txt" 2>&1
python3 "$VERIFIER" "$OUT/libfuxi-sb.dylib" --self-test > "$OUT/signature.json"
"$TC/CommandLineTools/usr/bin/nm" -u "$OUT/libfuxi-sb.dylib" | sort > "$OUT/undefined.txt"
(cd "$OUT" && sha256sum libfuxi-sb.dylib signature.json INPUTS.txt > SHA256SUMS)
printf 'result ok\norigin local-wsl\nelapsed_seconds %s\n%s\n' "$((SECONDS - START))" "$NOTE" > "$OUT/BUILD.txt"
printf 'LOCAL_DYLIB_OK %s elapsed_seconds=%s\n' "$NAME" "$((SECONDS - START))"
cat "$OUT/SHA256SUMS"
