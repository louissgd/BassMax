# 用三首配对曲拟合简化模型: gain = clamp(s*(T - L), -A, +C)
import numpy as np, scipy.signal as sg, itertools, json
from pair_io import load

PAIRS = [
    ('Rewrite', 'Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(原始).wav',
                'Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(音效).wav'),
    ('FengYu',  '周华健 - 风雨无阻.24bit(原始).wav', '周华健 - 风雨无阻.24bit(音效).wav'),
    ('Bryan',   'Bryan Adams - Here I Am(原始).wav', 'Bryan Adams - Here I Am(音效).wav'),
]
BANDS = [('LO', 0, 120), ('MID', 120, 2500), ('HI', 2500, 0)]


def band(x, sr, lo, hi):
    y = x
    if hi:
        b, a = sg.butter(4, hi/(sr/2), 'low'); y = sg.lfilter(b, a, y)
    if lo:
        b, a = sg.butter(4, lo/(sr/2), 'high'); y = sg.lfilter(b, a, y)
    return y


def env(x, sr, ms=10):
    w = max(1, int(sr*ms/1000)); k = np.ones(w)/w
    return np.sqrt(np.convolve(x*x, k, 'same') + 1e-18)


def lagof(o, f, sr, lo, hi):
    pp = int(0.05*sr); s = int(40*sr); N = int(20*sr)
    x = band(o[s:s+N], sr, lo, hi); y = band(f[s-pp:s+N+pp], sr, lo, hi)
    return int(np.argmax(np.abs(np.correlate(y, x, 'valid')))) - pp


data = {}
for name, org, fx in PAIRS:
    o, sr = load(org); f, _ = load(fx)
    o = o.mean(1).astype(float); f = f.mean(1).astype(float)
    data[name] = {}
    for bn, lo, hi in BANDS:
        L = lagof(o, f, sr, lo, hi)
        fo = np.concatenate([f[L:], np.zeros(L)])
        eo = env(band(o, sr, lo, hi), sr); ef = env(band(fo, sr, lo, hi), sr)
        lv = 20*np.log10(eo + 1e-12); g = 20*np.log10(ef + 1e-12) - lv
        ok = lv > -75
        # 1dB 分箱中位数
        edges = np.arange(-75, -3, 1.0)
        xs, ys, ws = [], [], []
        for i in range(len(edges)-1):
            m = ok & (lv >= edges[i]) & (lv < edges[i+1])
            if m.sum() > 2000:
                xs.append((edges[i]+edges[i+1])/2); ys.append(np.median(g[m])); ws.append(m.sum())
        data[name][bn] = (np.array(xs), np.array(ys), np.array(ws, float), L)
        print('%s %s lag=%d bins=%d' % (name, bn, L, len(xs)), flush=True)

np.save('bryan_fit.npy', np.array([1]))
json.dump({k: {b: [v[0].tolist(), v[1].tolist(), v[2].tolist(), v[3]] for b, v in d.items()}
           for k, d in data.items()}, open('fit_simple_input.json', 'w'))

print('\n拟合 gain = clamp(s*(T-L), -A, C)')
res = {}
for bn, _, _ in BANDS:
    allx = np.concatenate([data[n][bn][0] for n in data])
    ally = np.concatenate([data[n][bn][1] for n in data])
    allw = np.concatenate([data[n][bn][2] for n in data])
    best = None
    for s in np.arange(0.35, 1.05, 0.05):
        for T in np.arange(-40, 2, 0.5):
            for C in np.arange(12, 34, 0.5):
                for A in (0, 1, 2, 3, 4):
                    pred = np.clip(s*(T - allx), -A, C)
                    err = np.sum(allw*np.abs(pred - ally)) / allw.sum()
                    if best is None or err < best[0]:
                        best = (err, s, T, C, A)
    err, s, T, C, A = best
    res[bn] = dict(err=err, s=s, T=T, C=C, A=A)
    print('%s  MAE %.2f dB   s=%.2f  T=%.1f  C=%.1f  A=%.0f' % (bn, err, s, T, C, A))
json.dump(res, open('fit_simple.json', 'w'), indent=1)
