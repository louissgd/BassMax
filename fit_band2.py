# -*- coding: utf-8 -*-
"""逐频段拟合原插件的向上压缩参数 (增益-电平曲线 + 攻击/释放)"""
import numpy as np
from scipy.signal import butter, sosfiltfilt, lfilter
from scipy.optimize import minimize
from pair_io import load

SR = 48000

def bp(v, lo, hi):
    sos = butter(4, [lo/(SR/2), hi/(SR/2)], 'band', output='sos')
    return sosfiltfilt(sos, v)

def rms_env(v, ms):
    w = int(ms*SR/1000)
    p = v*v
    c = np.cumsum(np.concatenate([[0], p]))
    e = (c[w:]-c[:-w])/w
    out = np.sqrt(e+1e-20)
    lead = w//2
    head = np.full(lead, out[0])
    tail = np.full(max(0, len(v)-lead-len(out)), out[-1])
    return np.concatenate([head, out, tail])[:len(v)]

def follow(v, ta, tr):
    a = 1-np.exp(-1/(ta*SR)); b = 1-np.exp(-1/(tr*SR))
    ea = lfilter([a], [1, -(1-a)], np.abs(v))
    er = lfilter([b], [1, -(1-b)], np.abs(v))
    return np.maximum(ea, er)

def main():
    x, _ = load("Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(原始).wav")
    t, _ = load("Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(音效).wav")
    N = SR*120
    xm = x[:N].mean(1).astype(np.float64)
    ym = t[:N].mean(1).astype(np.float64)
    step = 48                      # 1ms 抽样
    BANDS = [(30,120),(120,2500),(2500,16000)]
    out = []
    for lo, hi in BANDS:
        sx = bp(xm, lo, hi)[::step]
        st = bp(ym, lo, hi)[::step]
        Lx = 20*np.log10(rms_env(sx, 20)+1e-12)
        Ly = 20*np.log10(rms_env(st, 20)+1e-12)
        gm = Ly - Lx
        mask = (Lx > -70) & (Lx < -5)
        def loss(pv):
            make, slope, thr, cap, ta, tr = pv
            if slope <= 0 or cap <= 0 or ta <= 0.0003 or tr <= 0.01: return 1e6
            e = follow(sx, ta, tr)
            L = 20*np.log10(e+1e-12)
            g = make + np.minimum(cap, slope*np.maximum(0.0, thr-L))
            return float(np.mean(np.abs(g[mask]-gm[mask])))
        best = None
        for ta0 in (0.003, 0.010, 0.030):
            for tr0 in (0.06, 0.15, 0.40):
                r = minimize(loss, [12, 0.5, -20, 15, ta0, tr0], method='Nelder-Mead',
                             options=dict(maxiter=1500, xatol=1e-3, fatol=1e-3))
                if best is None or r.fun < best.fun: best = r
        make, slope, thr, cap, ta, tr = best.x
        print(f"band {lo:5d}-{hi:<5d}: makeup={make:5.1f} slope={slope:4.2f} thr={thr:6.1f} "
              f"cap={cap:4.1f} att={ta*1000:5.1f}ms rel={tr*1000:5.1f}ms err={best.fun:.2f}dB", flush=True)
        out.append([lo, hi, make, slope, thr, cap, ta, tr])
    np.save('band_fit.npy', np.array(out))

if __name__ == '__main__':
    main()
