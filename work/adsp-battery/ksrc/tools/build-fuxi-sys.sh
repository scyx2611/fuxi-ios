#!/bin/bash
# Build the P-SYS userland pieces on a Mac with Xcode (iPhoneOS SDK):
#   OUT/FuxiAudio.driver   AudioServerPlugIn bundle for mediaserverd's HAL
#   OUT/libfuxi-sb.dylib   SpringBoard snapshot-format library
# Install them with tools/fuxi-sys-root.py (see docs-lanes/sys-port.md).
# WSL: build-fuxi-sys.sh --audio-only /path/to/new-artifact /path/to/verify-sb-shape.py
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ ${1:-} == --audio-only && $(uname -s) == Linux ]]; then
    [[ $# -eq 3 ]] || { echo 'Pass the output directory and the Mach-O verifier' >&2; exit 2; }
    out=$2
    verifier=$3
    tc=/opt/unixnu-linux
    sdk=$tc/sdks/iPhoneOS16.5.sdk
    archive=$tc/sdks/iPhoneOS16.5.sdk.tar.xz
    cc=$tc/llvm/bin/clang
    linker=/usr/bin/ld64.lld-21
    [[ "$(sha256sum "$archive" | cut -d' ' -f1)" == 5e0fd3f01266cce4ce012d4a99b38eb56578fca40d09edc81cd83dee958202fb ]]
    mkdir "$out"
    trap 'printf "result FAILED\n" > "$out/BUILD.txt"' ERR
    bundle=$out/FuxiAudio.driver
    mkdir "$bundle"
    cp tools/fuxi-audio/Info.plist "$bundle/Info.plist"
    "$cc" -target arm64-apple-ios16.0 -isysroot "$sdk" -O2 -Wall -Wextra -Werror \
        -fstack-protector-strong -c tools/fuxi-audio/FuxiAudio.c -o "$out/FuxiAudio.o"
    "$linker" -arch arm64 -platform_version ios 16.0 16.5 -bundle -syslibroot "$sdk" \
        -L"$sdk/usr/lib" -F"$sdk/System/Library/Frameworks" "$out/FuxiAudio.o" \
        -lSystem -framework CoreFoundation -adhoc_codesign -o "$bundle/FuxiAudio"
    llvm-nm-21 -gU "$bundle/FuxiAudio" > "$out/exports.txt"
    rg -q ' T _FuxiAudio_Create$' "$out/exports.txt"
    python3 "$verifier" --audio-bundle --self-test "$bundle" > "$out/signature.json"
    python3 tools/test-fuxi-sys.py --audio-only > "$out/host-tests.txt" 2>&1
    sha256sum "$cc" "$linker" "$archive" "$verifier" tools/build-fuxi-sys.sh \
        tools/fuxi-audio/FuxiAudio.c tools/fuxi-audio/Info.plist tools/test-fuxi-sys.py > "$out/INPUTS.txt"
    (cd "$out" && sha256sum FuxiAudio.driver/FuxiAudio FuxiAudio.driver/Info.plist signature.json INPUTS.txt > SHA256SUMS)
    printf 'result ok\norigin local-wsl\n' > "$out/BUILD.txt"
    cat "$out/host-tests.txt" "$out/SHA256SUMS"
    exit 0
fi
[[ $(uname -s) == Darwin ]] || { echo 'requires a Mac with Xcode' >&2; exit 1; }
out=${1:-tools/out/sys}
sdk=$(xcrun --sdk iphoneos --show-sdk-path)
cc=$(xcrun --sdk iphoneos --find clang)
flags=(-target arm64-apple-ios16.0 -isysroot "$sdk" -O2 -Wall -Wextra -Werror -fstack-protector-strong)
bundle=$out/FuxiAudio.driver
rm -rf "$bundle"
mkdir -p "$bundle"
cp tools/fuxi-audio/Info.plist "$bundle/Info.plist"
"$cc" "${flags[@]}" -bundle tools/fuxi-audio/FuxiAudio.c -framework CoreFoundation -o "$bundle/FuxiAudio"
"$cc" "${flags[@]}" -dynamiclib tools/fuxi-sb/FuxiSnapshots.c -lobjc \
 -install_name /usr/local/lib/fuxi/libfuxi-sb.dylib -o "$out/libfuxi-sb.dylib"
for binary in "$bundle" "$out/libfuxi-sb.dylib"; do
 codesign -f -s - "$binary"
 codesign --verify --strict "$binary"
done
shasum -a 256 "$bundle/FuxiAudio" "$bundle/Info.plist" "$out/libfuxi-sb.dylib" > "$out/SYS-SHA256SUMS"
cat "$out/SYS-SHA256SUMS"
