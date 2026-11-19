#!/usr/bin/env python3
"""disp0 fuxi,mfb-align：DisplayPipePlaneBaseAlignment 以字典發佈的主機測試。

VideoToolbox FigGetAlignmentForIOSurfaceOutputToDisplayService（iOS 16.7）以 IORegistryEntrySearchCFProperty
取得 AppleCLCD 的 DisplayPipePlaneBaseAlignment 後直接 CFDictionaryGetValue(…, "PlaneBaseAlignmentLinear")；
裸數字使 mediaserverd 在影片建立 render pipeline 時以 -[__NSCFNumber objectForKey:] abort。QuartzCore AppleDisplay
只在型別為字典時讀同一 key，否則留預設 64。

本測試抽出 SweetFramebuffer.cpp 的 sweet_fb_align_publish，以模擬的 OSDictionary／OSNumber 引用計數編譯執行，
逐點注入四個步驟的失敗；並檢查 start() 的閘門（預設關、失敗時保留原本的數字）。
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'xnu/fuxi-unixnu/SweetFramebuffer.cpp'

PREFIX = r'''
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static int live, failAt, calls;
static char logged[256];
static bool step() { return ++calls != failAt; }
struct OSObject {
 int refs = 1;
 virtual ~OSObject() {}
 void release() { if (--refs == 0) { live--; delete this; } }
};
struct OSNumber : OSObject {
 unsigned long long value = 0; unsigned bits = 0;
 static OSNumber *withNumber(unsigned long long value, unsigned bits) {
  if (!step()) return nullptr;
  OSNumber *number = new OSNumber; number->value = value; number->bits = bits; live++; return number;
 }
};
struct OSDictionary : OSObject {
 const char *key = nullptr; OSObject *object = nullptr;
 ~OSDictionary() override { if (object) object->release(); }
 static OSDictionary *withCapacity(unsigned) { if (!step()) return nullptr; live++; return new OSDictionary; }
 bool setObject(const char *name, OSObject *value) {
  if (!step()) return false;
  assert(!key); key = name; object = value; value->refs++; return true;
 }
};
#define OSSafeReleaseNULL(p) do { if (p) { (p)->release(); (p) = nullptr; } } while (0)
static OSDictionary *published;
struct AppleCLCD {
 bool setProperty(const char *name, OSDictionary *value) {
  if (!step()) return false;
  assert(!strcmp(name, "DisplayPipePlaneBaseAlignment")); value->refs++; published = value; return true;
 }
};
static void IOLog(const char *format, ...) {
 va_list args; va_start(args, format); vsnprintf(logged, sizeof(logged), format, args); va_end(args);
}
'''

TEST = r'''
int main() {
 AppleCLCD fb;
 assert(sweet_fb_align_publish(&fb) && calls == 4 && published);
 assert(!strcmp(published->key, "PlaneBaseAlignmentLinear"));
 OSNumber *linear = static_cast<OSNumber *>(published->object);
 assert(linear->value == 64 && linear->bits == 32 && linear->refs == 1 && published->refs == 1 && live == 2);
 assert(strstr(logged, "{PlaneBaseAlignmentLinear=64} published (fuxi,mfb-align)"));
 published->release();
 assert(live == 0);
 for (int fail = 1; fail <= 4; fail++) {
  calls = 0; failAt = fail; published = nullptr; logged[0] = 0;
  /* 兩個物件總是先建立；之後短路：setObject 失敗就不呼叫 setProperty */
  assert(!sweet_fb_align_publish(&fb) && !published && live == 0 && calls == (fail < 2 ? 2 : fail));
  assert(strstr(logged, "FAILED, number kept"));
 }
 puts("PASS: fuxi,mfb-align DisplayPipePlaneBaseAlignment {PlaneBaseAlignmentLinear=64}, refcounts, 4 fault sites");
 return 0;
}
'''

GATE = ('#if defined(FUXI)\n\tif (sweet_fb_adt_u32(provider, "fuxi,mfb-align") != 1 || '
        '!sweet_fb_align_publish(this))\n#endif\n\tsetProperty("DisplayPipePlaneBaseAlignment", 64ULL, 32);\n')


def function(source, name):
    match = re.search(r'^static bool\s*\n?' + re.escape(name) + r'\([^;]*?\)\s*\{', source, re.M)
    assert match, name
    depth, end = 1, match.end()
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end] + '\n'


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--sanitize', action='store_true', help='build with ASan/UBSan')
    ap.add_argument('--cxx', default='clang++')
    args = ap.parse_args()
    display = SOURCE.read_text()
    # 閘門：只有 disp0 fuxi,mfb-align=1 且字典發佈成功才跳過原本的數字，並且在 registerService 之前。
    assert display.count(GATE) == 1, 'start() gate changed'
    assert display.count('"DisplayPipePlaneBaseAlignment"') == 2
    assert display.index('static bool sweet_fb_align_publish(') < display.index(GATE) < \
        display.index('\tregisterService();\n\tIOLog("SWEET FB: AppleCLCD')
    # 幅距需求刻意不發佈（兩個讀者缺值都用 64）。
    assert 'DisplayPipeStrideRequirements' not in display.replace('DisplayPipeStrideRequirements 不發佈', '')
    print('PASS: fuxi,mfb-align gate defaults off, keeps the number on failure, precedes registerService')
    with tempfile.TemporaryDirectory(prefix='fuxi-mfb-align-') as tmp:
        cpp = Path(tmp) / 'align.cpp'
        cpp.write_text(PREFIX + function(display, 'sweet_fb_align_publish') + TEST)
        cmd = [args.cxx, '-std=c++17', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(Path(tmp) / 'align')]
        if args.sanitize:
            cmd[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-sanitize-recover=all']
        subprocess.run(cmd, check=True)
        subprocess.run([str(Path(tmp) / 'align')], check=True)


if __name__ == '__main__':
    main()
