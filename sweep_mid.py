# -*- coding: utf-8 -*-
"""MID 段释放时间折中测试: 电子乐尾巴 vs 风雨吉他稳定性"""
import os, subprocess
import numpy as np, scipy.signal as sg
from pair_io import load

AQ_SRC = 'Aqua - An Apple A Day(原版).wav'
AQ_REF = 'Aqua - An Apple A Day(音效).wav'
FY_SRC = '周华健 - 风雨无阻.24bit(原始).wav'
FY_REF = '周华健 - 风雨无阻.24bit(音效).wav'

CASES = [
    ('mid400(现状)', {}),
    ('mid300', {'BASSMAX_GRELMID': '0.30'}),
    ('mid250', {'BASSMAX_GRELMID': '0.25'}),
    ('mid200', {'BASSMAX_GRELMID': '0.20'}),
]
BASE = {'BASSMAX_PRESET': '4', 'BASSMAX_DRIVE': '1', 'BASSMAX_TRIM_9K': '0'}


def band(x, sr, lo, hi):
    y = x
    if hi: y = sg.lfilter(*sg.butter(4, hi / (sr / 2), 'low'), y)
    if lo: y = sg.lfilter(*sg.butter(4, lo / (sr / 2), 'high'), y)
    return y


def env(x, sr, ms=10):
    w = max(1, int(sr * ms / 1000))
    return np.sqrt(np.convolve(x * x, np.ones(w) / w, 'same') + 1e-20)


def db(v): return 20 * np.log10(v + 1e-12)


def decay(e, sr, steps=(30, 80, 300, 1000)):
    hop = max(1, int(sr * 0.005)); thr = np.percentile(e, 88)
    out = {s: [] for s in steps}
    for i in range(hop * 4, len(e) - int(1.3 * sr), hop):
        if e[i] < thr: continue
        if e[i] < e[max(0, i - hop * 4):i + hop * 4].max() * 0.999: continue
        for s in steps:
            j = i + int(s * sr / 1000)
            if j < len(e): out[s].append(db(e[i]) - db(e[j]))
    return {s: (np.median(v) if v else np.nan) for s, v in out.items()}


def slow_pp(g, sr, a=3000, b=500):
    def ma(x, ms):
        w = max(1, int(sr * ms / 1000)); c = np.cumsum(np.r_[0., x]); h = w // 2
        i = np.arange(len(x)); lo = np.clip(i - h, 0, len(x)); hi = np.clip(i - h + w, 0, len(x))
        return (c[hi] - c[lo]) / np.maximum(1, hi - lo)
    s = ma(g, a) - ma(g, b)
    return np.percentile(s, 95) - np.percentile(s, 5)


def gain_track(org, sr, lo, hi, ms=10, lag=0):
    eo = env(band(org, sr, lo, hi), sr, ms)
    return eo


# 电子乐
o, sr = load(AQ_SRC); r, _ = load(AQ_REF)
NA = int(90 * sr)
om = o[:NA].mean(1); rm = r[:NA].mean(1)
eo_mid = env(band(om, sr, 120, 2500), sr)
eo_hi = env(band(om, sr, 2500, 16000), sr)

print('=== 电子乐 (前 90s) ===')
print('%-14s %-28s %-22s' % ('', '中频 500-2.5k 衰减 30/80/300/1000', '高频 2.5k+ 30/80'))
rows = []
for nm, extra in CASES:
    e = dict(os.environ); e.update(BASE); e.update({'BASSMAX_BASS': '4.5'}); e.update(extra)
    f = 'sm_' + nm.replace('(', '').replace(')', '').replace('现状', '') + '.wav'
    subprocess.run(['./render_wav.exe', AQ_SRC, f, '1', '4.5', '90'],
                   env=e, stdout=subprocess.DEVNULL, check=True)
    x, _ = load(f); x = x[:NA].mean(1)
    d1 = decay(env(band(x, sr, 500, 2500), sr), sr)
    d2 = decay(env(band(x, sr, 2500, 16000), sr), sr, steps=(30, 80))
    print('%-14s %5.1f %5.1f %5.1f %5.1f          %5.1f %5.1f' %
          (nm, d1[30], d1[80], d1[300], d1[1000], d2[30], d2[80]))
    rows.append((nm, x))
for nm, x in [('原版', om), ('音效', rm)]:
    d1 = decay(env(band(x, sr, 500, 2500), sr), sr)
    d2 = decay(env(band(x, sr, 2500, 16000), sr), sr, steps=(30, 80))
    print('%-14s %5.1f %5.1f %5.1f %5.1f          %5.1f %5.1f' %
          (nm, d1[30], d1[80], d1[300], d1[1000], d2[30], d2[80]))

# 风雨: 吉他前 40 秒的中频增益慢起伏
o2, sr2 = load(FY_SRC); r2, _ = load(FY_REF)
NF = int(40 * sr2)
om2 = o2[:NF].mean(1); rm2 = r2[:NF].mean(1)
print('\n=== 风雨无阻 0-40s: 中频(120-2500)增益轨迹的慢起伏 (p95-p5 dB, 越小越稳) ===')
eo = env(band(om2, sr2, 120, 2500), sr2)


def slow_measure(x):
    ex = env(band(x, sr2, 120, 2500), sr2)
    m = eo > 3e-4
    g = np.full(NF, np.nan)
    g[m] = db(ex[m]) - db(eo[m])
    i = np.arange(NF)
    g = np.interp(i, i[m], np.clip(g[m], -25, 25))
    return slow_pp(g, sr2)


print('  参考音效  %.2f dB' % slow_measure(rm2))
for nm, extra in CASES:
    e = dict(os.environ); e.update(BASE); e.update({'BASSMAX_BASS': '0'}); e.update(extra)
    f = 'fy_' + nm.replace('(', '').replace(')', '').replace('现状', '') + '.wav'
    subprocess.run(['./render_wav.exe', FY_SRC, f, '1', '0', '40'],
                   env=e, stdout=subprocess.DEVNULL, check=True)
    x, _ = load(f); x = x[:NF].mean(1)
    print('  %-14s %.2f dB' % (nm, slow_measure(x)))
