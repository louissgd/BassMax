# an_dev.py — 相对于参考音的「慢偏差」(0.33-2Hz), 用来量听感上的「波浪」
import numpy as np, sys
from pair_io import load

SRC = '周华健 - 风雨无阻.24bit(原始).wav'
REF = '周华健 - 风雨无阻.24bit(音效).wav'
N = int(40 * 44100)
o, sr = load(SRC); r, _ = load(REF)
o = o[:N].mean(1).astype(float); r = r[:N].mean(1).astype(float)
BANDS = [('LO', 0, 120), ('MID', 120, 2500), ('HI', 2500, 0)]


def band(x, lo, hi):
    from scipy import signal as sg
    y = x
    if hi: y = sg.lfilter(*sg.butter(4, hi / (sr / 2), 'low'), y)
    if lo: y = sg.lfilter(*sg.butter(4, lo / (sr / 2), 'high'), y)
    return y


def env(x, ms=10):
    w = max(1, int(sr * ms / 1000))
    return np.sqrt(np.convolve(x * x, np.ones(w) / w, 'same') + 1e-18)


def ma(g, ms):
    w = max(1, int(sr * ms / 1000))
    c = np.cumsum(np.concatenate([[0.0], g]))
    h = w // 2
    i = np.arange(len(g))
    a = np.clip(i - h, 0, len(g)); b = np.clip(i - h + w, 0, len(g))
    return (c[b] - c[a]) / np.maximum(1, b - a)


def bestlag(bn):
    lo, hi = [b[1:] for b in BANDS if b[0] == bn][0]
    eo = env(band(o, lo, hi)); er = env(band(r, lo, hi))
    best = (0, -2.0)
    for lag in range(-1200, 1201, 5):
        c = np.corrcoef(eo[20000:-2000], np.roll(er, -lag)[20000:-2000])[0, 1]
        if c > best[1]: best = (lag, c)
    return best[0]


LAGS = {bn: bestlag(bn) for bn, _, _ in BANDS}


def slow_dev(x, bn):
    lo, hi = [b[1:] for b in BANDS if b[0] == bn][0]
    eo = env(band(o, lo, hi))
    ex = env(band(x, lo, hi))
    er = np.roll(env(band(r, lo, hi)), -LAGS[bn])
    mask = (eo > 3e-4) & (er > 3e-4)
    d = np.full(N, np.nan)
    d[mask] = 20 * np.log10((ex[mask] + 1e-9) / (er[mask] + 1e-9))
    i = np.arange(N)
    d = np.interp(i, i[mask], np.clip(d[mask], -25, 25))
    s = ma(d, 3000) - ma(d, 500)
    return s.std(), d[mask].std()


print('%-20s  LO慢偏差  MID慢偏差  HI慢偏差 | 全带总偏差')
x0 = r
row = [slow_dev(x0, bn)[0] for bn, _, _ in BANDS]
print('%-20s  %7.2f   %7.2f   %7.2f' % ('参考(自身)', *row))
for f in sys.argv[1:]:
    x, _ = load(f); x = x[:N].mean(1).astype(float)
    r3 = [slow_dev(x, bn) for bn, _, _ in BANDS]
    print('%-20s  %7.2f   %7.2f   %7.2f | %5.2f'
          % (f.replace('.wav', '')[:20], r3[0][0], r3[1][0], r3[2][0],
             np.sqrt(np.mean([t[1] ** 2 for t in r3]))))
