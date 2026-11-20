"""000 狀態欄驗收：量截圖頂部的時間（左）與右側圖示（飛航／電池）外框與中心 y。

用法：python -I measure-sb.py <截圖或巡覽目錄> [...]
背景＝每列在該區域的中位色；和背景差 > 80（RGB 最大分量差）的像素算內容。只看 y < 150。
Android cutout 是 x506..574、y0..121 的矩形避讓區；它不是實體孔洞，不能由高度一半推算鏡頭中心。
"""
import glob
import os
import sys

from PIL import Image

H = 150
REGIONS = {'time': (20, 330), 'right': (760, 1078)}


def bbox(im, x0, x1):
    px = im.load()
    bounds = []
    for y in range(min(H, im.height)):
        row = [px[x, y] for x in range(x0, x1)]
        med = tuple(sorted(c[k] for c in row)[len(row) // 2] for k in range(3))
        hit = [x0 + i for i, c in enumerate(row) if max(abs(c[k] - med[k]) for k in range(3)) > 80]
        if hit:
            if bounds and y - bounds[-1][0] > 6:
                break
            bounds.append((y, hit[0], hit[-1]))
    if not bounds:
        return None
    # 水平與垂直都只量第一段，以免把下方頁面標題誤算進時間或電池的位置。
    top, bottom = bounds[0][0], bounds[-1][0]
    return top, bottom, (top + bottom) / 2, min(b[1] for b in bounds), max(b[2] for b in bounds)


for d in sys.argv[1:]:
    print('==', os.path.basename(os.path.normpath(d)))
    shots = [d] if os.path.isfile(d) else sorted(glob.glob(os.path.join(d, 'framebuffer*.png')))
    for f in shots:
        im = Image.open(f).convert('RGB')
        out = []
        for name, (x0, x1) in REGIONS.items():
            b = bbox(im, x0, x1)
            out.append(f'{name}=' + ('none' if b is None else f'rows {b[0]}-{b[1]} center {b[2]:.1f} x {b[3]}-{b[4]}'))
        print(f'{os.path.basename(f):42s}', '  '.join(out))
