# -*- coding: utf-8 -*-
"""扫 HI 段时间常数: 看高频瞬态衰减是否追上参考, 安静段抬升是否回落"""
import os, subprocess
import numpy as np, scipy.signal as sg
from pair_io import load

SRC = 'Aqua - An Apple A Day(原版).wav'
REF = 'Aqua - An Apple A Day(音效).wav'
SEC = 90          # 渲染/分析前 90 秒
BASE = {'BASSMAX_PRESET': '4', 'BASSMAX_DRIVE': '1', 'BASSMAX_BASS': '4.5', 'BASSMAX_TRIM_9K': '0'}
CASES = [
    ('基线 400', {}),
    ('hiRel150', {'BASSMAX_GRELHI': '0.15'}),
    ('hiRel250', {'BASSMAX_GRELHI': '0.25'}),
    ('hiRel150+env20', {'BASSMAX_GRELHI': '0.15', 'BASSMAX_ENVHI': '0.02'}),
    ('hiRel150+env10', {'BASSMAX_GRELHI': '0.15', 'BASSMAX_ENVHI': '0.01'}),
]

o, sr = load(SRC); r, _ = load(REF)
N = int(SEC * sr)
om = o[:N].mean(1); rm = r[:N].mean(1)


def band(x, lo, hi):
    y = x
    if hi: y = sg.lfilter(*sg.butter(4, hi / (sr / 2), 'low'), y)
    if lo: y = sg.lfilter(*sg.butter(4, lo / (sr / 2), 'high'), y)
    return y


def env(x, ms=10):
    w = max(1, int(sr * ms / 1000))
    return np.sqrt(np.convolve(x * x, np.ones(w) / w, 'same') + 1e-20)


def db(v): return 20 * np.log10(v + 1e-12)


def decay(e, steps=(30, 80, 150, 300)):
    hop = max(1, int(sr * 0.005)); thr = np.percentile(e, 88)
    out = {s: [] for s in steps}
    for i in range(hop * 4, len(e) - int(0.5 * sr), hop):
        if e[i] < thr: continue
        if e[i] < e[max(0, i - hop * 4):i + hop * 4].max() * 0.999: continue
        for s in steps:
            j = i + int(s * sr / 1000)
            if j < len(e): out[s].append(db(e[i]) - db(e[j]))
    return {s: (np.median(v) if v else np.nan) for s, v in out.items()}


def quiet_lift(x):
    eo = db(env(om)); ex = db(env(x))
    m = eo <= np.percentile(eo, 10)
    return np.median(ex[m]) - np.median(eo[m])


rows = []
for nm, extra in CASES:
    e = dict(os.environ); e.update(BASE); e.update(extra)
    f = 'hi_' + nm.replace(' ', '').replace('+', '_').replace('=', '') + '.wav'
    subprocess.run(['./render_wav.exe', SRC, f, '1', '4.5', str(SEC)],
                   env=e, stdout=subprocess.DEVNULL, check=True)
    rows.append((nm, f))

for nm, x in [('原版', om), ('音效', rm)] + rows:
    if isinstance(x, str):
        y, _ = load(x); y = y[:N].mean(1)
    else:
        y = x
    dh = decay(env(band(y, 2500, 16000)))
    da = decay(env(y))
    q = quiet_lift(y) if nm not in ('原版',) else 0.0
    print('%-16s 高频衰减 30/80/150/300 = %5.1f %5.1f %5.1f %5.1f | 全带 %5.1f %5.1f | 安静段抬 %+5.1f'
          % (nm, dh[30], dh[80], dh[150], dh[300], da[30], da[80], q))
