# 分析 Bryan Adams 极端测试曲: 原插件在驱动100时的拉伸极限与是否下压
import numpy as np, scipy.signal as sg
from pair_io import load

ORG = 'Bryan Adams - Here I Am(原始).wav'
FX  = 'Bryan Adams - Here I Am(音效).wav'

o, sr = load(ORG); f, _ = load(FX)
o = o.mean(1).astype(float); f = f.mean(1).astype(float)
print('sr', sr, 'len s %.1f' % (len(o)/sr))

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

def lagof(a, b, lo, hi):
    pp = int(0.05*sr); s = int(40*sr); N = int(20*sr)
    x = band(a[s:s+N], lo, hi); y = band(b[s-pp:s+N+pp], lo, hi)
    return int(np.argmax(np.abs(np.correlate(y, x, 'valid')))) - pp

def db(v): return 20*np.log10(v + 1e-12)

L = lagof(o, f, 0, 120)
print('LO lag', L, '=%.2f ms' % (L/sr*1000))
fo = np.concatenate([f[L:], np.zeros(L)])

print('\n%s %9s %9s %9s' % ('', 'peak', 'rms', 'crest'))
for nm, x in [('ORIG', o), ('FX', fo)]:
    print('%-6s %9.3f %9.1f %9.2f' % (nm, np.abs(x).max(), db(np.sqrt((x**2).mean())),
                                      np.abs(x).max()/np.sqrt((x**2).mean())))

print('\n分段增益 vs 参考自身电平 (10ms 包络)')
for nm, lo, hi in [('LO', 0, 120), ('MID', 120, 2500), ('HI', 2500, 0)]:
    eo = env(band(o, lo, hi), 10); ef = env(band(fo, lo, hi), 10)
    lv = db(eo); g = db(ef) - lv
    print(' %s  n=%d  gain: p1 %.1f med %.1f p99 %.1f max %.1f min %.1f' %
          (nm, len(lv), np.percentile(g, 1), np.median(g), np.percentile(g, 99), g.max(), g.min()))
    edges = [-90, -70, -60, -50, -45, -40, -35, -30, -27, -24, -21, -18, -15, -12, -6]
    for i in range(len(edges)-1):
        m = (lv >= edges[i]) & (lv < edges[i+1])
        if m.sum() > 500:
            print('    %4d..%4d n=%7d  ref gain %6.1f  (p10 %5.1f p90 %5.1f)' %
                  (edges[i], edges[i+1], m.sum(), np.median(g[m]),
                   np.percentile(g[m], 10), np.percentile(g[m], 90)))
print('\n全带增益 (宽带电平) 与是否下压')
eo = env(o, 10); ef = env(fo, 10)
lv = db(eo); g = db(ef) - lv
print(' 全带 gain: p0.1 %.1f med %.1f p99.9 %.1f max %.1f' %
      (np.percentile(g, 0.1), np.median(g), np.percentile(g, 99.9), g.max()))
m = lv > -12
print(' 高电平(>-12dBFS) n=%d 中位增益 %.2f  p5 %.2f  p95 %.2f' %
      (m.sum(), np.median(g[m]), np.percentile(g[m], 5), np.percentile(g[m], 95)))
