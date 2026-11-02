# -*- coding: utf-8 -*-
"""闭环校准: 按「我们自己的检测电平 v」分箱, 比较参考增益与我们实得增益,
把差值(阻尼)加回曲线。比一次性拟合更准, 因为口径完全一致。"""
import numpy as np, scipy.signal as sg, sys, re
from pair_io import load

BANDS = [('LO', 0, 120), ('MID', 120, 2500), ('HI', 2500, 0)]
SONGS = [
    ("Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(原始).wav",
     "Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(音效).wav", "cr_r.csv"),
    ("周华健 - 风雨无阻.24bit(原始).wav",
     "周华健 - 风雨无阻.24bit(音效).wav", "cr_z.csv"),
]
GRID = np.array([-70.0, -63.6, -57.1, -50.7, -44.2, -37.8, -31.3, -24.9, -18.4, -12.0])
DAMP = 0.8
CLAMP = 2.5


def band(x, sr, lo, hi):
    y = x
    if hi:
        b, a = sg.butter(4, hi/(sr/2), 'low'); y = sg.lfilter(b, a, y)
    if lo:
        b, a = sg.butter(4, lo/(sr/2), 'high'); y = sg.lfilter(b, a, y)
    return y


def env10(x, sr):
    w = int(sr*0.01); k = np.ones(w)/w
    return np.sqrt(np.convolve(x*x, k, 'same') + 1e-18)


def lagof(o, f, sr, lo, hi):
    pp = int(0.04*sr); s = int(40*sr); N = int(20*sr)
    a = band(o[s:s+N], sr, lo, hi); b = band(f[s-pp:s+N+pp], sr, lo, hi)
    return int(np.argmax(np.abs(np.correlate(b, a, 'valid')))) - pp


def collect():
    data = {nm: ([], []) for nm, _, _ in BANDS}
    for org, fx, dump_path in SONGS:
        o, sr = load(org); f, _ = load(fx)
        o = o.mean(1).astype(np.float64); f = f.mean(1).astype(np.float64)
        d = np.loadtxt(dump_path, delimiter=',', skiprows=1)
        tv = d[:, 0]
        for k, (nm, lo, hi) in enumerate(BANDS):
            L = lagof(o, f, sr, lo, hi)
            fo = np.concatenate([f[L:], np.zeros(L)])
            eo = env10(band(o, sr, lo, hi), sr)
            ef = env10(band(fo, sr, lo, hi), sr)
            ep = env10(band(o, sr, lo, hi), sr)  # 用原始做基准, 增益直接比
            idx = np.clip((tv*sr).astype(int), 0, len(o)-1)
            g_ref = 20*np.log10(ef[idx]/(eo[idx]+1e-12))
            v = d[:, 1+k]
            g_our = d[:, 4+k]          # 我们实际施加的增益 (dB)
            m = (eo[idx] > 1e-3) & (g_ref > -2) & (g_ref < 40)
            data[nm][0].append(v[m]); data[nm][1].append(g_ref[m] - g_our[m])
    return data


def main():
    data = collect()
    core = open('bassmax_core.hpp', encoding='utf-8').read()
    for nm, _, _ in BANDS:
        v = np.concatenate(data[nm][0]); dg = np.concatenate(data[nm][1])
        cur = np.array([float(x) for x in
                        re.search(rf'{nm}_Y\[10\]\s*=\s*\{{([^}}]*)\}}', core).group(1).replace('f', '').split(',')])
        new = cur.copy()
        print(f'--- {nm} ---')
        for i, g in enumerate(GRID):
            m = np.abs(v - g) < 2.0
            if m.sum() >= 400:
                delta = np.median(dg[m])
                delta = max(-CLAMP, min(CLAMP, delta))
                new[i] = cur[i] + DAMP*delta
                print(f'  v={g:7.1f}  n={m.sum():6d}  Δ={delta:+5.1f}  {cur[i]:5.1f} -> {new[i]:5.1f}')
            else:
                print(f'  v={g:7.1f}  n={m.sum():6d}  (样本不足, 保持)')
        core = re.sub(rf'{nm}_Y\[10\]\s*=\s*\{{[^}}]*\}};',
                      f"{nm}_Y[10]  = {{ " + ",".join(f'{x:.1f}f' for x in new) + " };", core)
    open('bassmax_core.hpp', 'w', encoding='utf-8').write(core)
    print('bassmax_core.hpp 已更新')


if __name__ == '__main__':
    main()
