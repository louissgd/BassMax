# -*- coding: utf-8 -*-
"""分箱中位 + 单调化 (PAVA) 反推每段曲线, 输出 curve.json
横轴 = 我们自己的检测器电平 (gfull.csv 里的 lv_*), 纵轴 = 参考插件的该段增益"""
import json
import numpy as np
from scipy.signal import butter, sosfiltfilt
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

def pava(y, w):
    """非递增等张回归 (pool adjacent violators)"""
    y = y.astype(float).copy(); w = w.astype(float).copy()
    idx = list(range(len(y)))
    lv = [[i] for i in idx]; val = list(y); wts = list(w)
    i = 0
    while i < len(val)-1:
        if val[i] < val[i+1] - 1e-12:      # 违反非递增
            nw = wts[i] + wts[i+1]
            nv = (val[i]*wts[i] + val[i+1]*wts[i+1]) / nw
            val[i:i+2] = [nv]; wts[i:i+2] = [nw]; lv[i:i+2] = [lv[i]+lv[i+1]]
            if i > 0: i -= 1
        else:
            i += 1
    out = np.zeros(len(y))
    for v, ids in zip(val, lv):
        out[ids] = v
    return out

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
        edges = np.array([-72,-64,-58,-52,-46,-42,-38,-34,-30,-26,-22,-18,-14,-9], float)
        cx, cy, cw = [], [], []
        for i in range(len(edges)-1):
            m = (L > edges[i]) & (L <= edges[i+1])
            if m.sum() < 40: continue
            cx.append((edges[i]+edges[i+1])/2); cy.append(np.median(g[m])); cw.append(m.sum())
        if len(cx) < 3:
            print("数据不足", key); return
        cx = np.array(cx); cy = pava(np.array(cy), np.array(cw))
        # 端点延伸 + 插值到固定网格
        y = np.interp(GRID, cx, cy)
        out[key] = [round(float(v), 1) for v in y]
        print(f"{key}: {','.join(f'{v:.1f}' for v in y)}   (bins n={len(cx)})")
    json.dump(out, open('curve.json', 'w'), indent=1)

if __name__ == '__main__':
    main()
