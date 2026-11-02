# an_mod.py — 量「波浪感」: 各段增益的 0.1-3Hz 慢调制深度 (dB)
import numpy as np, scipy.signal as sg, sys
from pair_io import load

SRC = '周华健 - 风雨无阻.24bit(原始).wav'
REF = '周华健 - 风雨无阻.24bit(音效).wav'
N = int(40 * 44100)

o, sr = load(SRC); r, _ = load(REF)
o = o[:N].mean(1).astype(float)
r = r[:N].mean(1).astype(float)

BANDS = [('LO', 0, 120), ('MID', 120, 2500), ('HI', 2500, 0)]


def band(x, lo, hi):
    y = x
    if hi:
        y = sg.lfilter(*sg.butter(4, hi / (sr / 2), 'low'), y)
    if lo:
        y = sg.lfilter(*sg.butter(4, lo / (sr / 2), 'high'), y)
    return y


def env(x, ms=10):
    w = max(1, int(sr * ms / 1000))
    return np.sqrt(np.convolve(x * x, np.ones(w) / w, 'same') + 1e-18)


def gain_curve(x, bn, lag=0):
    lo, hi = [b[1:] for b in BANDS if b[0] == bn][0]
    eo = env(band(o, lo, hi))
    ex = env(band(x, lo, hi))
    if lag:
        ex = np.roll(ex, -lag)
    mask = eo > 3e-4
    g = np.full(N, np.nan)
    g[mask] = 20 * np.log10((ex[mask] + 1e-9) / (eo[mask] + 1e-9))
    g = np.clip(g, -20.0, 40.0)
    idx = np.arange(N)
    return np.interp(idx, idx[mask], g[mask])


def ma(g, ms):
    w = max(1, int(sr * ms / 1000))
    c = np.cumsum(np.concatenate([[0.0], g]))
    out = np.empty(len(g))
    h = w // 2
    for i in range(len(g)):
        a = max(0, i - h); b = min(len(g), i - h + w)
        out[i] = (c[b] - c[a]) / (b - a)
    return out


def slow(g):
    # 0.33-2Hz 的慢起伏: 3 秒平均 减 0.5 秒平均
    s = ma(g, 3000) - ma(g, 500)
    return s.std(), np.percentile(s, 95) - np.percentile(s, 5)


def bestlag(bn):
    lo, hi = [b[1:] for b in BANDS if b[0] == bn][0]
    eo = env(band(o, lo, hi)); er = env(band(r, lo, hi))
    best = (0, -2.0)
    for lag in range(-1200, 1201, 5):
        er2 = np.roll(er, -lag)
        c = np.corrcoef(eo[20000:-2000], er2[20000:-2000])[0, 1]
        if c > best[1]:
            best = (lag, c)
    return best[0]


def report(tag, x, lags):
    row = []
    for bn, _, _ in BANDS:
        s, pp = slow(gain_curve(x, bn, lags[bn]))
        row.append('%5.2f/%5.2f' % (s, pp))
    print('%-16s %s' % (tag, '  '.join(row)))


lags = {bn: bestlag(bn) for bn, _, _ in BANDS}
print('band                LO  std/pp     MID std/pp     HI  std/pp   (0.1-3Hz)')
report('参考音效', r, lags)
for f in sys.argv[1:]:
    x, _ = load(f)
    x = x[:N].mean(1).astype(float)
    report(f.replace('.wav', '')[:16], x, {k: 0 for k in lags})
