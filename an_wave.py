# -*- coding: utf-8 -*-
"""检查开头段的增益波动 (波浪感来源)"""
import numpy as np
from pair_io import load

ORG = '周华健 - 风雨无阻.24bit(原始).wav'
FX = '周华健 - 风雨无阻.24bit(音效).wav'
OURS = 'f_z.wav'
SEC = 35

o, sr = load(ORG); t, _ = load(FX); p, _ = load(OURS)
L = int(SEC * sr)
o = o[:L].mean(1).astype(np.float64)
t = t[:L].mean(1).astype(np.float64)
p = p[:L].mean(1).astype(np.float64)
print('loaded', sr, L)


def env(x, ms):
    w = int(sr * ms / 1000)
    k = np.ones(w) / w
    return np.sqrt(np.convolve(x * x, k, 'same') + 1e-18)


eo100 = env(o, 100); eo300 = env(o, 300)
rows = {}
for nm, x in [('ref', t), ('ours', p)]:
    g100 = 20 * np.log10(env(x, 100) / (eo100 + 1e-12) + 1e-12)
    g300 = 20 * np.log10(env(x, 300) / (eo300 + 1e-12) + 1e-12)
    rows[nm] = (g100, g300)
    print(nm)
    for lo, hi in [(0, 5), (5, 10), (10, 15), (15, 20), (20, 30)]:
        s, e = int(lo * sr), int(hi * sr)
        print('  %2d-%2ds  100ms %5.1f±%.2f   300ms %5.1f±%.2f'
              % (lo, hi, g100[s:e].mean(), g100[s:e].std(), g300[s:e].mean(), g300[s:e].std()))
