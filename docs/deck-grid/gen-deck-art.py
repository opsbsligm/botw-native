#!/usr/bin/env python3
"""从 docs/deck-grid/src 里的任天堂官方素材重生成 Steam 库四件套。

源图随仓库走：任天堂旧媒体站 zelda.com/breath-of-the-wild 的 CDN 现已 403，
只记 URL 的话换机器就再也取不回同一批像素。两张源图的 SHA-256 见 src/SHA256SUMS。
生成过程只做绝对坐标裁切与 Lanczos 缩放，不重绘、不加滤镜，所以任何人跑一次
本脚本都得到与仓库里逐字节可复现的同四件（PNG 编码器差异除外）。

    python3 docs/deck-grid/gen-deck-art.py [输出目录，默认本目录]

四件的尺寸是 Steam Deck 库用的规格：横卡 920x430、竖海报 600x900、
详情页 hero 1920x620、快捷方式图标 1254x1254。装到掌机用同目录的
deploy-deck-art.sh。
"""
import hashlib
import pathlib
import sys
from PIL import Image

HERE = pathlib.Path(__file__).resolve().parent
OUT = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else HERE
SRC = HERE / "src"
KEYART = SRC / "botw-keyart.jpg"        # 2560x1440，Link 崖 + 大师之剑字标
PORTRAIT = SRC / "botw-portrait.jpg"    # 1080x1920，官方竖版


def cut(src, box, out, size):
    """按 (left, top, right, bottom) 绝对裁切后 resize 到 size 存 PNG。"""
    im = Image.open(src).convert("RGB")
    W, H = im.size
    l, t, r, b = box
    assert 0 <= l < r <= W and 0 <= t < b <= H, "%s %dx%d box %s" % (src.name, W, H, box)
    im.crop(box).resize(size, Image.LANCZOS).save(OUT / out, "PNG")
    print("%-26s %sx%s  %s" % (out, size[0], size[1],
                               hashlib.sha256((OUT / out).read_bytes()).hexdigest()[:16]))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    # 横卡与 hero 从横版主视觉取：字标在右下半区，裁到 13:6 / 1920:620 时避开其断字。
    cut(KEYART, (0, 122, 2560, 1318), "botw-grid.png", (920, 430))   # 2560x1196，与 920x430 同比例
    cut(KEYART, (0, 250, 2560, 1076), "botw-hero.png", (1920, 620))
    # 竖海报取官方竖版去掉顶部安全边后的 2:3。
    cut(PORTRAIT, (0, 150, 1080, 1770), "botw-grid-p.png", (600, 900))
    # 图标同一竖版取 1:1，但框往下压到 y=656..1736：让 Link 站岩与大师之剑字标
    # 同框。原先的 y=180..1260 只框进天空与一个背影，库缩略图尺寸下认不出是哪部游戏；
    # 下沿 1736 仍在底部版权行（y≈1870）之上。
    cut(PORTRAIT, (0, 656, 1080, 1736), "botw-shortcut-icon.png", (1254, 1254))


if __name__ == "__main__":
    main()
