# make_icons.py — 生成扩展图标 (深色圆角底 + 橙色电平柱)
from PIL import Image, ImageDraw

S = 512
ACC = (255, 122, 61)
ACC2 = (255, 200, 120)


def draw(size, bars):
    img = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    r = int(S * 0.22)
    d.rounded_rectangle([0, 0, S - 1, S - 1], radius=r, fill=(15, 20, 27, 255))
    d.rounded_rectangle([1, 1, S - 2, S - 2], radius=r - 1, outline=(46, 56, 68, 255), width=max(2, S // 128))

    n = len(bars)
    gap = S * 0.055
    bw = (S - gap * (n + 1)) / n
    hmax = S * 0.62
    for i, h in enumerate(bars):
        x0 = gap + i * (bw + gap)
        hh = hmax * h
        y0 = (S - hh) / 2
        col = ACC2 if i == max(range(n), key=lambda k: bars[k]) else ACC
        d.rounded_rectangle([x0, y0, x0 + bw, y0 + hh], radius=bw / 2, fill=col)
    return img.resize((size, size), Image.LANCZOS)


full = [0.45, 0.72, 1.0, 0.66, 0.34]
for sz in (128, 48, 32):
    draw(sz, full).save(f'../icons/icon{sz}.png')

# 16px 只保留三根柱, 否则糊成一团
draw(16, [0.55, 1.0, 0.62]).save('../icons/icon16.png')
print('icons written')
