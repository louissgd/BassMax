# 比较增益轨迹的平滑度: 参考 vs 我们 (风雨无阻开头 0-40s)
import numpy as np, scipy.signal as sg, sys
from pair_io import load

o, sr = load('周华健 - 风雨无阻.24bit(原始).wav')
t, _ = load('周华健 - 风雨无阻.24bit(音效).wav')
p, _ = load(sys.argv[1] if len(sys.argv)>1 else 'g4_z.wav')
o = o.mean(1).astype(float); t = t.mean(1).astype(float); p = p.mean(1).astype(float)

def band(x, lo, hi):
    y = x
    if hi:
        b, a = sg.butter(4, hi/(sr/2), 'low'); y = sg.lfilter(b, a, y)
    if lo:
        b, a = sg.butter(4, lo/(sr/2), 'high'); y = sg.lfilter(b, a, y)
    return y

def env(x, ms):
    w = max(1, int(sr*ms/1000)); k = np.ones(w)/w
    return np.sqrt(np.convolve(x*x, k, 'same') + 1e-18)

N = int(40*sr)
LAG = {'LO': 483, 'MID': 329, 'HI': 330}
print('%-5s %-6s | 增益 std (dB)   | 10ms |dG| p95   | 100ms 起伏' % ('band', 'src'))
for bn, lo, hi in [('LO', 0, 120), ('MID', 120, 2500), ('HI', 2500, 0)]:
    lag = LAG[bn]
    tf = np.concatenate([t[lag:], np.zeros(lag)])
    eo = env(band(o, lo, hi), 10)[:N]
    for nm, x in [('ref', tf), ('ours', p)]:
        ex = env(band(x, lo, hi), 10)[:N]
        g = 20*np.log10((ex + 1e-12)/(eo + 1e-12))
        ok = eo > 1e-4
        g = g[ok]
        # 100ms 平滑后的慢变化, 减掉慢变化就是抖动
        w = int(0.1*sr)
        gs = np.convolve(g, np.ones(w)/w, 'same')
        res = g - gs
        print('%-5s %-6s | %8.2f       | %8.3f      | %6.2f' %
              (bn, nm, np.std(g), np.percentile(np.abs(np.diff(g[::int(0.01*sr)])), 95), np.std(res)))
