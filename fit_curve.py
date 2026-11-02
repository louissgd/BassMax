# -*- coding: utf-8 -*-
"""用我们的检测器电平为横轴, 反推原插件每段的「电平->增益」曲线
先拟合 4 参数模型 gain = a + min(cap, slope*max(0, thr - L)), 再在固定网格上取值,
这样曲线天然单调、无拟合噪声。"""
import json
import numpy as np
from scipy.signal import butter, sosfiltfilt
from scipy.optimize import minimize
from pair_io import load

SR = 48000
GRID = np.array([-70, -56, -48, -42, -38, -34, -30, -25, -19, -12], float)
BANDS = [(30, 120), (120, 2500), (2500, 16000)]
NAMES = ['lv_lo', 'lv_mid', 'lv_hi']

def bp(v, lo, hi):
    sos = butter(4, [lo/(SR/2), hi/(SR/2)], 'band', output='sos')
    return sosfiltfilt(sos, v.mean(1).astype(np.float64))

def env(v, ms=20):
    w = int(ms*SR/1000); p = v*v
    c = np.cumsum(np.concatenate([[0], p])); e = (c[w:]-c[:-w])/w
    out = np.sqrt(e+1e-20)
    return np.concatenate([np.full(w//2, out[0]), out, np.full(max(0, len(v)-w//2-len(out)), out[-1])])[:len(v)]

def main():
    x, _ = load("Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(原始).wav")
    t, _ = load("Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(音效).wav")
    d = np.genfromtxt('gfull.csv', delimiter=',', names=True)
    idx = (d['t']*SR).astype(int)
    out = {}
    for (lo, hi), nm, key in zip(BANDS, NAMES, ('LO', 'MID', 'HI')):
        Lt = 20*np.log10(env(bp(t, lo, hi))+1e-12)
        Lx = 20*np.log10(env(bp(x, lo, hi))+1e-12)
        g = (Lt-Lx)[idx]
        L = d[nm]
        m = (L > -68) & (L < -6)
        Ls, gs = L[m], g[m]
        def model(p):
            a, slope, thr, cap = p
            return a + np.minimum(cap, slope*np.maximum(0.0, thr - Ls))
        def loss(p):
            a, slope, thr, cap = p
            if slope <= 0.02 or cap <= 0.5 or thr < -60 or thr > -3: return 1e6
            return float(np.mean(np.abs(model(p)-gs)))
        best = None
        for s0 in (0.3, 0.5, 0.8, 1.1):
            for t0 in (-46, -38, -30, -22):
                r = minimize(loss, [8.0, s0, t0, 12.0], method='Nelder-Mead',
                             options=dict(maxiter=600, xatol=1e-2, fatol=1e-3))
                if best is None or r.fun < best.fun: best = r
        a, slope, thr, cap = best.x
        print(f"{key}: a(loud)={a:5.1f} slope={slope:4.2f} thr={thr:6.1f} cap={cap:5.1f} err={best.fun:.2f}dB")
        y = a + np.minimum(cap, slope*np.maximum(0.0, thr - GRID))
        out[key] = [round(float(v), 1) for v in y]
        print(f"    表: {','.join(f'{v:.1f}' for v in y)}")
    json.dump(out, open('curve.json', 'w'), indent=1)

if __name__ == '__main__':
    main()
