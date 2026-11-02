# -*- coding: utf-8 -*-
"""闭环校准: 按「我们自己检测器电平」分箱, 计算参考增益与我们增益之差, 直接修正曲线
用法: python calib.py <我们的渲染.wav> [damp]
需要 gfull.csv (当前构建的 dump_gain 输出)
"""
import re, sys
import numpy as np
from scipy.signal import butter, sosfiltfilt
from pair_io import load

SR = 48000
GRID = np.array([-70, -56, -48, -42, -38, -34, -30, -25, -19, -12], float)
BANDS = [(30, 120), (120, 2500), (2500, 16000)]
KEYS = ['LO', 'MID', 'HI']
NAMES = ['lv_lo', 'lv_mid', 'lv_hi']

def bp(v, lo, hi):
    sos = butter(4, [lo/(SR/2), hi/(SR/2)], 'band', output='sos')
    return sosfiltfilt(sos, v.mean(1).astype(np.float64))

def env(v, ms=20):
    w = int(ms*SR/1000); p = v*v
    c = np.cumsum(np.concatenate([[0], p])); e = (c[w:]-c[:-w])/w
    out = np.sqrt(e+1e-20)
    return np.concatenate([np.full(w//2, out[0]), out, np.full(max(0, len(v)-w//2-len(out)), out[-1])])[:len(v)]

def get_y(s, key):
    m = re.search(rf"static constexpr float {key}_Y\[10\]\s*=\s*\{{([^}}]*)\}}", s)
    return [float(v.strip().rstrip('f')) for v in m.group(1).split(',') if v.strip()]

def set_y(s, key, ys):
    body = ",".join(f"{v:.1f}f" for v in ys)
    return re.sub(rf"static constexpr float {key}_Y\[10\]\s*=\s*\{{[^}}]*\}}",
                  f"static constexpr float {key}_Y[10]  = {{{body}}}", s)

def main():
    ours_path = sys.argv[1]
    damp = float(sys.argv[2]) if len(sys.argv) > 2 else 0.8
    x, _ = load("Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(原始).wav")
    t, _ = load("Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(音效).wav")
    o, _ = load(ours_path)
    d = np.genfromtxt('gfull.csv', delimiter=',', names=True)
    N = min(len(x), len(t), len(o))
    idx = (d['t']*SR).astype(int)
    s = open("base_core.hpp", encoding='utf-8').read()
    for (lo, hi), key, nm in zip(BANDS, KEYS, NAMES):
        Lx = 20*np.log10(env(bp(x[:N], lo, hi))+1e-12)
        Lt = 20*np.log10(env(bp(t[:N], lo, hi))+1e-12)
        Lo = 20*np.log10(env(bp(o[:N], lo, hi))+1e-12)
        gt, go = (Lt-Lx)[idx], (Lo-Lx)[idx]
        L = d[nm]
        cx, cy, cw = [], [], []
        for i in range(len(GRID)-1):
            m = (L >= GRID[i]) & (L < GRID[i+1])
            if m.sum() < 300: continue
            cx.append((GRID[i]+GRID[i+1])/2)
            cy.append(np.median(gt[m]-go[m])); cw.append(m.sum())
        if len(cx) < 3:
            print(f"{key}: 数据不足, 跳过"); continue
        delta = np.clip(np.interp(GRID, np.array(cx), np.array(cy)), -3.0, 3.0)
        ys = np.array(get_y(s, key))
        new = ys + damp*delta
        print(f"{key}: 修正 {np.round(delta,1)}  (有效箱 {len(cx)})")
        s = set_y(s, key, new)
    open("base_core.hpp", 'w', encoding='utf-8', newline='').write(s)
    open("bassmax_core.hpp", 'w', encoding='utf-8', newline='').write(s)
    print("曲线已更新")

if __name__ == '__main__':
    main()
