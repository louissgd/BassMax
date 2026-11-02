# -*- coding: utf-8 -*-
"""生成 BassMax 图标 (.ico)

设计: 深色圆角底 + 三条递升的圆头竖柱(代表电平被拉向满刻度), 色阶深蓝 -> 青。
     最长的柱顶有一个亮点, 对应算法里「拉伸到 0.1 就封顶」。
每个尺寸单独超采样绘制, 保证 16x16 也清楚。

两套配色:
  bassmax.ico      开启: 蓝 -> 青, 有亮点
  bassmax_off.ico  关闭: 整体灰阶, 亮点压暗 (一眼看出滤镜没生效)
"""
from PIL import Image, ImageDraw, ImageChops
import os

SS = 8                     # 超采样倍数
SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]

PALETTES = {
    'on': {
        'top': (14, 21, 38), 'bot': (28, 43, 78),
        'stroke': (46, 66, 110),
        'cols': [(37, 99, 235), (14, 165, 233), (34, 211, 238)],
        'dot': (240, 254, 255),
    },
    'off': {
        'top': (30, 32, 36), 'bot': (54, 58, 64),
        'stroke': (78, 82, 88),
        'cols': [(112, 118, 126), (140, 146, 154), (172, 178, 186)],
        'dot': (120, 126, 134),
    },
}


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(len(a)))


def draw(size, mode='on'):
    pal = PALETTES[mode]
    S = size * SS
    k = S / 512.0
    def sc(v): return v * k

    radius = sc(116)
    rmask = Image.new('L', (S, S), 0)
    ImageDraw.Draw(rmask).rounded_rectangle([0, 0, S - 1, S - 1], radius=radius, fill=255)

    # ---- 底: 渐变 + 顶部高光, 画在同不透明层上, 最后一次性裁成圆角 ----
    top, bot = pal['top'], pal['bot']
    base = Image.new('RGBA', (S, S), (0, 0, 0, 255))
    bd = ImageDraw.Draw(base)
    for y in range(S):
        c = lerp(top, bot, y / max(1, S - 1))
        bd.line([(0, y), (S, y)], fill=c + (255,))
    # 顶部极淡白光
    for y in range(int(S * 0.5)):
        t = y / (S * 0.5)
        a = 22 * (1 - t)
        row = base.crop((0, y, S, y + 1)).convert('RGB')
        overlay = Image.new('RGB', (S, 1), (255, 255, 255))
        row = Image.blend(row, overlay, a / 255.0)
        base.paste(row, (0, y))

    img = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    img.paste(base, (0, 0), rmask)

    d = ImageDraw.Draw(img)

    # 外描边
    d.rounded_rectangle([sc(3), sc(3), S - 1 - sc(3), S - 1 - sc(3)],
                        radius=radius - sc(3), outline=pal['stroke'] + (255,),
                        width=max(1, int(sc(6))))

    # ---- 竖柱 (小尺寸简化成两根, 否则 16px 下三条会糊成一片) ----
    base_y = sc(388)
    if size <= 20:
        bw = sc(84)
        xs = [sc(190), sc(322)]
        hs = [sc(178), sc(318)]
        cols = [pal['cols'][0], pal['cols'][2]]
    elif size <= 32:
        bw = sc(50)
        xs = [sc(178), sc(256), sc(334)]
        hs = [sc(168), sc(248), sc(330)]
        cols = pal['cols']
    else:
        bw = sc(56)
        xs = [sc(184), sc(256), sc(328)]
        hs = [sc(168), sc(248), sc(330)]
        cols = pal['cols']

    for cx, h, c in zip(xs, hs, cols):
        top_y = base_y - h
        r = bw / 2.0
        box = [cx - r, top_y, cx + r, base_y]
        d.rounded_rectangle(box, radius=r, fill=c + (255,))
        # 柱内自上而下的淡白光, 制造体积感 (用 alpha 合成, 只影响柱内)
        ov = Image.new('RGBA', (S, S), (0, 0, 0, 0))
        od = ImageDraw.Draw(ov)
        for y in range(int(top_y), int(base_y) + 1):
            t = (y - top_y) / max(1.0, base_y - top_y)
            a = int(40 * (1 - t))
            od.line([(cx - r, y), (cx + r, y)], fill=(255, 255, 255, a))
        m2 = Image.new('L', (S, S), 0)
        ImageDraw.Draw(m2).rounded_rectangle(box, radius=r, fill=255)
        ov.putalpha(ImageChops.multiply(ov.getchannel('A'), m2))
        img = Image.alpha_composite(img, ov)
        d = ImageDraw.Draw(img)

    # 最长柱顶亮点 (封顶点); 关闭版保留位置但压暗, 表示「到顶但没在工作」
    if size >= 24:
        dr = sc(10)
        cx, cy = xs[-1], base_y - hs[-1] + sc(2)
        d.ellipse([cx - dr, cy - dr, cx + dr, cy + dr], fill=pal['dot'] + (255,))

    return img.resize((size, size), Image.LANCZOS)


here = os.path.dirname(os.path.abspath(__file__))
for mode, name in [('on', 'bassmax.ico'), ('off', 'bassmax_off.ico')]:
    imgs = [draw(s, mode) for s in SIZES]
    out = os.path.join(here, name)
    big = draw(256, mode)
    big.save(out, format='ICO', sizes=[(s, s) for s in SIZES], append_images=imgs)
    print('写了', out, os.path.getsize(out), 'bytes')

    if mode == 'on':
        big.save('bassmax_icon_preview.png')
        draw(16).resize((256, 256), Image.NEAREST).save('bassmax_icon_16x.png')
        draw(32).resize((256, 256), Image.NEAREST).save('bassmax_icon_32x.png')
    else:
        big.save('bassmax_icon_off_preview.png')
        draw(16, 'off').resize((256, 256), Image.NEAREST).save('bassmax_icon_off_16x.png')
        draw(32, 'off').resize((256, 256), Image.NEAREST).save('bassmax_icon_off_32x.png')
print('preview ok')
