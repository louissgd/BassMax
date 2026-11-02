# sweep2.py — 扫 LO 检测时间常数 / 中高频能量窗, 同时看「慢起伏」和「跟随性」
import numpy as np, os, subprocess, sys
from pair_io import load

SRC = '周华健 - 风雨无阻.24bit(原始).wav'
REF = '周华健 - 风雨无阻.24bit(音效).wav'
N = int(40 * 44100)
BASE = {'BASSMAX_PRESET': '4', 'BASSMAX_DRIVE': '1', 'BASSMAX_BASS': '0', 'BASSMAX_TRIM_9K': '3'}

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
    idx = np.arange(len(g))
    a = np.clip(idx - h, 0, len(g)); b = np.clip(idx - h + w, 0, len(g))
    return (c[b] - c[a]) / np.maximum(1, b - a)


def gain_curve(x, bn, lag=0):
    lo, hi = [b[1:] for b in BANDS if b[0] == bn][0]
    eo = env(band(o, lo, hi)); ex = env(band(x, lo, hi))
    if lag: ex = np.roll(ex, -lag)
    mask = eo > 3e-4
    g = np.full(N, np.nan)
    g[mask] = 20 * np.log10((ex[mask] + 1e-9) / (eo[mask] + 1e-9))
    g = np.clip(g, -20.0, 40.0)
    i = np.arange(N)
    return np.interp(i, i[mask], g[mask])


def wob(g):
    s = ma(g, 3000) - ma(g, 500)
    return s.std()


def bestlag(bn):
    from scipy import signal as sg
    lo, hi = [b[1:] for b in BANDS if b[0] == bn][0]
    eo = env(band(o, lo, hi)); er = env(band(r, lo, hi))
    best = (0, -2.0)
    for lag in range(-1200, 1201, 5):
        c = np.corrcoef(eo[20000:-2000], np.roll(er, -lag)[20000:-2000])[0, 1]
        if c > best[1]: best = (lag, c)
    return best[0]


LAGS = {bn: bestlag(bn) for bn, _, _ in BANDS}


def report(tag, x, lags):
    w = [wob(gain_curve(x, bn, lags[bn])) for bn, _, _ in BANDS]
    # 跟随性: 全带 10ms 包络与参考的相关 (先对齐)
    eo = env(band(o, 30, 16000)); ex = env(band(x, 30, 16000))
    cc = np.corrcoef(eo[20000:-2000], np.roll(ex, -LAGS['LO'])[20000:-2000])[0, 1]
    lum = 20 * np.log10(np.sqrt((x ** 2).mean()) / np.sqrt((r ** 2).mean()))
    print('%-22s wob %5.2f %5.2f %5.2f | 包络相关 %.4f | 响度 %+5.2f dB | 峰 %.3f'
          % (tag, w[0], w[1], w[2], cc, lum, np.abs(x).max()))


print('%-22s     LO    MID    HI' % 'case')
report('参考音效', r, LAGS)
CASES = [
    ('base', {}),
    ('limrel300', {'BASSMAX_LIMREL': '0.300'}),
    ('limrel600', {'BASSMAX_LIMREL': '0.600'}),
    ('limrel1000', {'BASSMAX_LIMREL': '1.000'}),
    ('limrel600+limatt3', {'BASSMAX_LIMREL': '0.600', 'BASSMAX_LIMATT': '0.003'}),
    ('mk0.5', {'BASSMAX_MAKEUP': '0.5'}),
]
for nm, ev in CASES:
    e = dict(os.environ); e.update(BASE); e.update(ev)
    f = 'sw2_%d.wav' % CASES.index((nm, ev))
    subprocess.run(['./render_wav.exe', SRC, f, '1', '0', '40'], env=e,
                   stdout=subprocess.DEVNULL, check=True)
    x, _ = load(f); x = x[:N].mean(1).astype(float)
    report(nm, x, {b[0]: 0 for b in BANDS})
