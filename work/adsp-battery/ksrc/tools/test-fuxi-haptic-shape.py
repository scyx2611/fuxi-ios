#!/usr/bin/env python3
"""0123 fuxi_haptic_shape：HAP525 短觸感改播原廠點按波形（fuxi,haptic=6）的主機測試。

2026-11-20 使用者回報 iOS 震動有些地方沒震、震感不像 iOS。iOS HAL 每 10 ms 視窗送 DIRECT_PLAY
（level<<16 | ms），輕觸只有一個 6 ms、level 17 左右的視窗，LRA 還沒起振就停。mode 6 在核心把
一段觸感的第一個短請求改成同機 Android 的 FIFO 點按（0_click_P_RTP.bin），VMAX 依 level 取平方根
曲線（Android VMAX 的約 45%..100%），同一段 40 ms 內的後續視窗吞掉，之後才交回 DIRECT_PLAY。

本測試從補丁抽出新標頭，以模擬的 HAL 請求序列編譯執行（可加 ASan／UBSan），檢查分段、吞掉、
停止不取消已排定點按、連續觸感交回、長請求照舊、VMAX 曲線單調且不超過 Android 值；
並檢查 fuxi_pmic.c 的閘門（只有 6 才整形、PCM 只給 5、RTP 未就緒或已故障時不整形）與開機標記字串。
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
P0123 = ROOT / 'xnu/patches/0123-fuxi-haptic-shape.patch'

TEST = r'''
#include <assert.h>
#include <stdio.h>
#include "fuxi_haptic_shape.h"
#define MS(x) ((uint64_t)(x) * 1000000ull)
static FuxiShapeBurst b;
static unsigned at(uint64_t ms_time, unsigned ms) { return fuxi_shape_classify(&b, MS(ms_time) + MS(5000), ms); }
int main(void) {
 assert(fuxi_shape_classify(0, 1, 6) == FUXI_SHAPE_PASS);

 /* 輕觸：單一 6 ms 視窗 */
 assert(at(0, 6) == FUXI_SHAPE_CLICK);
 /* 重的 transient／連續觸感：HAL 連續視窗 10 ms 一個、ms=10+5 */
 assert(at(100, 15) == FUXI_SHAPE_CLICK);
 assert(at(110, 15) == FUXI_SHAPE_SWALLOW);
 assert(at(120, 15) == FUXI_SHAPE_SWALLOW);
 assert(at(130, 15) == FUXI_SHAPE_SWALLOW);
 assert(at(140, 15) == FUXI_SHAPE_PASS);  /* 超過 40 ms：連續觸感交回 DIRECT */
 assert(at(150, 15) == FUXI_SHAPE_PASS);
 assert(at(160, 0) == FUXI_SHAPE_PASS);   /* 連續段的停止照送 */
 /* 停止之後 8 ms 起算新的一段 */
 assert(at(166, 6) == FUXI_SHAPE_PASS);   /* 停止把 end 拉到 160，166 < 168 仍是同一段 */
 assert(at(300, 6) == FUXI_SHAPE_CLICK);
 assert(at(303, 0) == FUXI_SHAPE_SWALLOW); /* 吞掉期間的停止不取消已排定的點按 */
 assert(at(350, 0) == FUXI_SHAPE_PASS);

 /* 間隔：結束後 < 8 ms 算同一段；≥ 8 ms 是新的一段 */
 assert(at(400, 6) == FUXI_SHAPE_CLICK);   /* end 406 */
 assert(at(410, 6) == FUXI_SHAPE_SWALLOW); /* 410 < 414：同一段，end 416 */
 assert(at(423, 6) == FUXI_SHAPE_SWALLOW); /* 423 < 424 */
 assert(at(437, 6) == FUXI_SHAPE_CLICK);   /* 437 ≥ 429+8 */

 /* 快速滾動：每 25 ms 一個 6 ms 刻度，每個都要點 */
 b.active = 0;
 for (unsigned i = 0; i < 20; i++) assert(at(1000 + 25 * i, 6) == FUXI_SHAPE_CLICK);
 /* 每 15 ms 一個：仍然每個都點（15 ≥ 6+8） */
 b.active = 0;
 for (unsigned i = 0; i < 20; i++) assert(at(2000 + 15 * i, 6) == FUXI_SHAPE_CLICK);

 /* 長請求（VibePattern 鈴聲、充電 400 ms、包絡 10+30 ms）整段照舊，續約也照舊 */
 b.active = 0;
 assert(at(3000, 400) == FUXI_SHAPE_PASS);
 assert(at(3300, 400) == FUXI_SHAPE_PASS);
 assert(at(4000, 21) == FUXI_SHAPE_PASS);
 assert(at(4010, 15) == FUXI_SHAPE_PASS);
 b.active = 0;
 assert(at(5000, 40) == FUXI_SHAPE_PASS);
 assert(at(5010, 40) == FUXI_SHAPE_PASS);
 /* SpringBoard 回退 15 ms、Atoms Minitap 15 ms：點 */
 assert(at(6000, 15) == FUXI_SHAPE_CLICK);
 assert(at(6000 + 2000, 2000) == FUXI_SHAPE_PASS);
 /* 上限 2000 ms 不溢位 */
 assert(at(9000, 2000) == FUXI_SHAPE_PASS && b.end == MS(11000) + MS(5000));

 /* 時間倒退（不應發生）當成新的一段 */
 b.active = 1; b.start = MS(99999); b.end = MS(99999); b.clicked = 1;
 assert(fuxi_shape_classify(&b, MS(10), 6) == FUXI_SHAPE_CLICK && b.start == MS(10));

 /* VMAX：Android 0x1c */
 assert(fuxi_shape_vmax(128, 0x1c) == 0x1c);
 assert(fuxi_shape_vmax(255, 0x1c) == 0x1c);
 assert(fuxi_shape_vmax(0, 0x1c) == fuxi_shape_vmax(FUXI_SHAPE_DEFAULT_LEVEL, 0x1c));
 assert(fuxi_shape_vmax(17, 0x1c) == 0x12);
 assert(fuxi_shape_vmax(4, 0x1c) == 0x0f);
 assert(fuxi_shape_vmax(64, 0) == 0);
 assert(fuxi_shape_vmax(64, 0x100) == 0);
 assert(fuxi_shape_vmax(1, 1) == 1);
 uint32_t last = 0;
 for (unsigned base = 1; base <= 0x38; base++) {
  last = 0;
  for (unsigned level = 1; level <= 128; level++) {
   uint32_t v = fuxi_shape_vmax(level, base);
   assert(v >= last && v <= base && v >= 1);
   assert(v * 256u + 128u >= base * FUXI_SHAPE_FLOOR);
   last = v;
  }
  assert(last == base);
 }
 for (unsigned v = 0; v < 70000; v++) {
  unsigned r = fuxi_shape_isqrt(v);
  assert(r * r <= v && (r + 1) * (r + 1) > v);
 }
 printf("PASS: haptic shape click/swallow/pass, stop keeps a scheduled click, VMAX 0x%x..0x%x of 0x1c\n",
     fuxi_shape_vmax(1, 0x1c), fuxi_shape_vmax(128, 0x1c));
 return 0;
}
'''


def added_text(patch, path):
    """取補丁中指定檔案的新增行（去掉 '+'）。"""
    text, keep = [], False
    for line in patch.splitlines():
        if line.startswith('+++ '):
            keep = line[4:].strip() == 'b/' + path
            continue
        if keep and line.startswith('+'):
            text.append(line[1:])
    return '\n'.join(text) + '\n'


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--sanitize', action='store_true', help='build with ASan/UBSan')
    ap.add_argument('--cc', default='clang')
    args = ap.parse_args()
    patch = P0123.read_text()

    assert re.findall(r'^\+\+\+ (\S+)', patch, re.M) == ['b/bsd/dev/fuxi_pmic.c', 'b/bsd/dev/fuxi_haptic_shape.h']
    pmic = added_text(patch, 'bsd/dev/fuxi_pmic.c')
    for needle in (
            '    if (mode > 3 || haptic > 6) {',
            '    if (haptic == 5 || haptic == 6) {',
            '            fp_rtp_pcm_enabled = haptic == 5 && PE_parse_boot_argn("fuxihalhaptic", &hal, sizeof(hal)) && hal == 3;',
            '    if (haptic >= 2 && haptic <= 6) {',
            '        fp_vibrate_shape = haptic == 6;',
            '    } else if (fp_vibrate_shape && !fp_vibrate_state.failed && __atomic_load_n(&fp_rtp_ready, __ATOMIC_ACQUIRE)) {',
            '        shape = fuxi_shape_classify(&fp_shape_burst, now, ms);',
            '    if (!error && shape != FUXI_SHAPE_SWALLOW) fp_vibrate_kick_worker();',
            '                click_vmax = base <= FUXI_RTP_TEST_VMAX_LIMIT ? fuxi_shape_vmax(shape_level, base) : 0;',
            'FUXI PMIC: haptic shape on: first request'):
        assert needle in pmic, needle
    # 移除的舊行只有閘門與 PCM 開關、點按 VMAX 參數
    removed = [l[1:] for l in patch.splitlines() if l.startswith('-') and not l.startswith('---')]
    assert len(removed) == 7, removed
    print('PASS: 0123 gates shaping on haptic=6 with RTP ready; PCM stays mode 5 only; boot marker present')

    header = added_text(patch, 'bsd/dev/fuxi_haptic_shape.h')
    with tempfile.TemporaryDirectory(prefix='fuxi-haptic-shape-') as tmp:
        (Path(tmp) / 'fuxi_haptic_shape.h').write_text(header)
        c = Path(tmp) / 'shape.c'
        c.write_text(TEST)
        cmd = [args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', tmp, str(c), '-o', str(Path(tmp) / 'shape')]
        if args.sanitize:
            cmd[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-sanitize-recover=all']
        subprocess.run(cmd, check=True)
        subprocess.run([str(Path(tmp) / 'shape')], check=True)


if __name__ == '__main__':
    main()
