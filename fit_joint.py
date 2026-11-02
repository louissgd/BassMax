# -*- coding: utf-8 -*-
"""跨曲目联合拟合: 把参考增益映射到我们检测器的电平口径

病根 (以前版本):
  1) 参考文件与原始文件在低频段有 ~5ms 的额外延迟 (中高频只有 ~7ms)。
     用单一延迟对齐, 低频包络比会算错 -> 低电平端增益被高估到 20dB+。
  2) 两边的电平口径不同。

做法: 每段各自用互相关找延迟 -> 10ms 包络 -> 在 dump 采样时刻取
      (我们的电平 v, 参考增益 g) -> 多首歌合并 -> 分箱取中位 -> PAVA 单调化。
"""
import numpy as np
import scipy.signal as sg
import sys
from pair_io import load
from fit_bins import pava

BANDS = [('LO', 0, 120), ('MID', 120, 2500), ('HI', 2500, 0)]

SONGS = [
    ("Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(原始).wav",
     "Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(音效).wav", "r.csv", 1.0),
    ("周华健 - 风雨无阻.24bit(原始).wav",
     "周华健 - 风雨无阻.24bit(音效).wav", "z.csv", 1.0),
]


def band(x, sr, lo, hi):
    y = x
    if hi:
        b, a = sg.butter(4, hi / (sr / 2), 'low')
        y = sg.lfilter(b, a, y)
    if lo:
        b, a = sg.butter(4, lo / (sr / 2), 'high')
        y = sg.lfilter(b, a, y)
    return y


def best_lag(o, f, sr, lo, hi, probe=60.0, dur=20.0):
    s, n, pad = int(probe * sr), int(dur * sr), int(0.04 * sr)
    a = band(o[s:s + n], sr, lo, hi)
    if s + n + pad > len(o):
        return 0
    b = band(f[s - pad:s + n + pad], sr, lo, hi)
    c = np.correlate(b, a, 'valid')
    return int(np.argmax(np.abs(c))) - pad


def env10(x, sr):
    w = int(sr * 0.01)
    k = np.ones(w) / w
    return np.sqrt(np.convolve(x * x, k, 'same') + 1e-18)


def gather():
    allpts = {nm: [] for nm, _, _ in BANDS}
    for org, fx, dump_path, drv in SONGS:
        o, sr = load(org)
        f, _ = load(fx)
        o = o.mean(1).astype(np.float64)
        f = f.mean(1).astype(np.float64)
        dump = np.loadtxt(dump_path, delimiter=',', skiprows=1)
        tv = dump[:, 0]
        print(f'== {org[:24]}  sr={sr}  dump={len(tv)} 点')
        for k, (nm, lo, hi) in enumerate(BANDS):
            lag = best_lag(o, f, sr, lo, hi)
            fo = np.concatenate([np.zeros(max(0, lag)), f])[:len(f)]
            if lag > 0:
                fo = np.concatenate([f[lag:], np.zeros(lag)])
            bo = band(o, sr, lo, hi)
            bf = band(fo, sr, lo, hi)
            eo, ef = env10(bo, sr), env10(bf, sr)
            idx = np.clip((tv * sr).astype(int), 0, len(o) - 1)
            lv = 20 * np.log10(eo[idx] + 1e-12)
            g = 20 * np.log10(ef[idx] / (eo[idx] + 1e-12))
            v = dump[:, 1 + k]
            # 只保留有声、且参考增益合理的点
            m = (lv > -60) & (g > -2) & (g < 40)
            print(f'   {nm} lag={lag} ({lag/sr*1000:.2f}ms)  有效点 {m.sum()}/{len(m)}'
                  f'  v范围 {v[m].min():.0f}..{v[m].max():.0f}  g中位 {np.median(g[m]):.1f}')
            for a, b in zip(v[m], g[m]):
                allpts[nm].append((a, b))
    return allpts


def bin_curve(v, g, vmin=-72, vmax=-4, step=2.0, minn=600):
    edges = np.arange(vmin, vmax + step, step)
    pts = []
    for i in range(len(edges) - 1):
        m = (v >= edges[i]) & (v < edges[i + 1])
        if m.sum() >= minn:
            pts.append((np.median(v[m]), np.median(g[m]), int(m.sum())))
    pts.sort()
    if not pts:
        return pts
    vx = np.array([p[0] for p in pts])
    gy = np.array([p[1] for p in pts])
    w = np.array([float(p[2]) for p in pts])
    return list(zip(vx, pava(gy, w), w.astype(int)))


def main():
    allpts = gather()
    for nm, _, _ in BANDS:
        arr = np.array(allpts[nm])
        print(f'--- {nm}  ({len(arr)} 点) ---')
        pts = bin_curve(arr[:, 0], arr[:, 1])
        for a, b, n in pts:
            print(f'   v={a:7.1f}  gain={b:6.1f}  n={n}')
        # 输出可供 C++ 使用的 10 点曲线 (等距 x 网格上插值)
        if len(pts) >= 2:
            xs = np.array([p[0] for p in pts])
            ys = np.array([p[1] for p in pts])
            grid = np.linspace(-70, -12, 10)
            val = np.interp(grid, xs, ys)
            print('   X = {%s}' % ','.join(f'{v:.1f}' for v in grid))
            print('   Y = {%s}' % ','.join(f'{v:.1f}' for v in val))


if __name__ == '__main__':
    main()
