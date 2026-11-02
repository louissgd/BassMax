# -*- coding: utf-8 -*-
"""对每段拟合「包络参数 + 增益曲线」, 用参考插件自己的增益轨迹当目标
包络定义与 C++ 实现一致: env = max(one-pole_att(|x|), one-pole_rel(|x|))"""
import json
import numpy as np
from scipy.signal import butter, sosfiltfilt, lfilter
from scipy.optimize import minimize
from pair_io import load

SR = 48000
STEP = 48                     # 1ms 抽样
BANDS = [(30, 120), (120, 2500), (2500, 16000)]
KEYS = ['LO', 'MID', 'HI']
GRID = np.array([-70, -56, -48, -42, -38, -34, -30, -25, -19, -12], float)

def bp(v, lo, hi):
    sos = butter(4, [lo/(SR/2), hi/(SR/2)], 'band', output='sos')
    return sosfiltfilt(sos, v.mean(1).astype(np.float64))

def env_ms(v, ms):
    w = int(ms*SR/1000); p = v*v
    c = np.cumsum(np.concatenate([[0], p])); e = (c[w:]-c[:-w])/w
    out = np.sqrt(e+1e-20)
    return np.concatenate([np.full(w//2, out[0]), out, np.full(max(0, len(v)-w//2-len(out)), out[-1])])[:len(v)]

def follow(v, ta, tr):
    a = 1-np.exp(-1/(ta*SR)); b = 1-np.exp(-1/(tr*SR))
    ax = np.abs(v)
    ea = lfilter([a], [1, -(1-a)], ax)
    er = lfilter([b], [1, -(1-b)], ax)
    return np.maximum(ea, er)

def main():
    x, _ = load("Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(原始).wav")
    t, _ = load("Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(音效).wav")
    N = SR*150
    out = {}
    for (lo, hi), key in zip(BANDS, KEYS):
        sx = bp(x[:N], lo, hi)[::STEP]
        st = bp(t[:N], lo, hi)[::STEP]
        Lx = 20*np.log10(env_ms(sx, 20)+1e-12)
        Lt = 20*np.log10(env_ms(st, 20)+1e-12)
        g = Lt - Lx
        m = (Lx > -70) & (g > -40)
        gs = g[m]
        print(f"--- {key} ({lo}-{hi})  样本 {int(m.sum())}", flush=True)
        best = None
        for ta in (0.002, 0.005, 0.012, 0.025, 0.05, 0.1):
            for tr in (0.05, 0.12, 0.25, 0.5, 1.0, 2.0):
                e = follow(sx, ta, tr)
                L = 20*np.log10(e+1e-12)[m]
                def loss(p):
                    a, slope, thr, cap = p
                    if slope <= 0.02 or cap <= 0.5 or thr < -55 or thr > -4: return 1e6
                    return float(np.mean(np.abs(a + np.minimum(cap, slope*np.maximum(0.0, thr-L)) - gs)))
                r = minimize(loss, [8.0, 0.6, -25.0, 12.0], method='Nelder-Mead',
                             options=dict(maxiter=400, xatol=1e-2, fatol=1e-3))
                if best is None or r.fun < best[0]:
                    best = (r.fun, ta, tr, r.x)
        err, ta, tr, p = best
        a, slope, thr, cap = p
        print(f"    att={ta*1000:5.1f}ms rel={tr*1000:5.1f}ms  a={a:5.1f} slope={slope:4.2f} thr={thr:6.1f} cap={cap:5.1f}  err={err:.2f}dB")
        y = a + np.minimum(cap, slope*np.maximum(0.0, thr - GRID))
        out[key] = dict(att=ta, rel=tr, y=[round(float(v), 1) for v in y])
        print("    表:", ",".join(f"{v:.1f}" for v in y), flush=True)
    json.dump(out, open('dyn.json', 'w'), indent=1)

if __name__ == '__main__':
    main()
