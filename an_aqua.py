# -*- coding: utf-8 -*-
"""电子乐「回音变重」诊断: 包络/尾巴/立体声/低频"""
import numpy as np, scipy.signal as sg
from pair_io import load

ORG = 'Aqua - An Apple A Day(原版).wav'
REF = 'Aqua - An Apple A Day(音效).wav'


def band(x, sr, lo, hi):
    y = x
    if hi:
        y = sg.lfilter(*sg.butter(4, hi / (sr / 2), 'low'), y)
    if lo:
        y = sg.lfilter(*sg.butter(4, lo / (sr / 2), 'high'), y)
    return y


def env(x, sr, ms=10):
    w = max(1, int(sr * ms / 1000))
    return np.sqrt(np.convolve(x * x, np.ones(w) / w, 'same') + 1e-20)


def db(v):
    return 20 * np.log10(v + 1e-12)


def bestlag(a, b, sr, lo=0, hi=120):
    A = band(a, sr, lo, hi); B = band(b, sr, lo, hi)
    pp = int(0.1 * sr); s = int(20 * sr); N = int(30 * sr)
    seg = A[s:s + N]
    win = B[max(0, s - pp):s + N + pp]
    c = np.correlate(win, seg, 'valid')
    return int(np.argmax(np.abs(c))) - pp


def decay_curve(e, sr, steps=(30, 80, 150, 300, 600, 1000)):
    """瞬态峰后各时刻的衰减量 (dB), 取中位数"""
    hop = max(1, int(sr * 0.005))
    thr = np.percentile(e, 88)
    out = {s: [] for s in steps}
    for i in range(hop * 4, len(e) - int(1.4 * sr), hop):
        if e[i] < thr:
            continue
        seg = e[max(0, i - hop * 4):i + hop * 4]
        if e[i] < seg.max() * 0.999:
            continue
        for s in steps:
            j = i + int(s * sr / 1000)
            if j < len(e):
                out[s].append(db(e[i]) - db(e[j]))
    return {s: (np.median(v) if v else np.nan) for s, v in out.items()}


o, sr = load(ORG); r, _ = load(REF)
om, rm = o.mean(1), r.mean(1)
N = min(len(om), len(rm))
om, rm = om[:N], rm[:N]
lag = bestlag(om, rm, sr)
print(f'音效相对原版延迟 {lag} 样本 = {lag/sr*1000:.1f} ms\n')

sig = {}
sig['原版'] = om
sig['音效'] = np.concatenate([rm[lag:], np.zeros(lag)])
for f, lab in [('aq_b45.wav', '旧 bass+4.5'), ('aq_new.wav', '新 bass+4.5')]:
    x, _ = load(f)
    sig[lab] = x[:N].mean(1)

print('=== 1. 10ms 包络分位数 (dBFS) ===')
print('%-14s %7s %7s %7s %7s %7s %7s' % ('', '1%', '5%', '10%', '50%', '90%', '99%'))
q = [1, 5, 10, 50, 90, 99]
E = {}
for lab, x in sig.items():
    e = db(env(x, sr))
    E[lab] = e
    print('%-14s ' % lab + ' '.join(f'{v:7.1f}' for v in np.percentile(e, q)))

print('\n=== 2. 原版最安静 10% 时刻的电平 (回音/底噪暴露的直接证据) ===')
eo = E['原版']
m = eo <= np.percentile(eo, 10)
print(f'  原版中位数 {np.median(eo[m]):7.1f}')
for lab in sig:
    if lab == '原版':
        continue
    d = np.median(E[lab][m]) - np.median(eo[m])
    print(f'  {lab:14s} {np.median(E[lab][m]):7.1f}  (抬 {d:+.1f} dB)')

print('\n=== 3. 瞬态后衰减 (dB, 越大=掉得越快) ===')
steps = (30, 80, 150, 300, 600, 1000)
print('%-14s ' % 'ms' + ' '.join(f'{s:6d}' for s in steps))
for lab, x in sig.items():
    e = env(x, sr)
    d = decay_curve(e, sr, steps)
    print('%-14s ' % lab + ' '.join(f'{d[s]:6.1f}' for s in steps))

for nm, lo, hi in [('低频 30-120', 0, 120), ('中低 120-500', 120, 500), ('中高 500-2.5k', 500, 2500), ('高频 2.5k+', 2500, 16000)]:
    print(f'\n--- 分段 {nm} 瞬态后衰减 ---')
    print('%-14s ' % 'ms' + ' '.join(f'{s:6d}' for s in steps))
    for lab, x in sig.items():
        e = env(band(x, sr, lo, hi), sr)
        d = decay_curve(e, sr, steps)
        print('%-14s ' % lab + ' '.join(f'{d[s]:6.1f}' for s in steps))

print('\n=== 4. 立体声 ===')
for nm, path in [('原版', ORG), ('音效', REF)]:
    x, _ = load(path)
    L, R = x[:, 0], x[:, 1]
    mid = (L + R) / 2; side = (L - R) / 2
    print(f'  {nm}  side/mid = {db(np.sqrt((side**2).mean())/np.sqrt((mid**2).mean())):+.1f} dB, '
          f'L/R 相关 {np.corrcoef(L,R)[0,1]:.3f}')
for nm, path in [('我们 bass+4.5', 'aq_new.wav')]:
    x, _ = load(path)
    L, R = x[:, 0], x[:, 1]
    mid = (L + R) / 2; side = (L - R) / 2
    print(f'  {nm}  side/mid = {db(np.sqrt((side**2).mean())/np.sqrt((mid**2).mean())):+.1f} dB, '
          f'L/R 相关 {np.corrcoef(L,R)[0,1]:.3f}')

print('\n=== 5. 频谱 (1/3 倍频程, 相对音效) ===')
C = [50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250,
     1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000]


def bd(x):
    nfft = 8192; hop = nfft // 2
    fr = [x[s:s + nfft] for s in range(0, len(x) - nfft, hop)]
    X = np.fft.rfft(np.array(fr) * np.hanning(nfft), axis=1)
    P = np.abs(X) ** 2
    f = np.fft.rfftfreq(nfft, 1 / sr)
    return np.array([10 * np.log10(np.median(P[:, (f >= c / 2 ** (1 / 6)) & (f < c * 2 ** (1 / 6))].sum(1)) + 1e-20) for c in C])


br = bd(sig['音效'])
print('频段   ' + ' '.join(f'{c:>6d}' for c in C))
for lab in ['原版', '旧 bass+4.5', '新 bass+4.5']:
    d = bd(sig[lab]) - br
    print(f'{lab:12s} ' + ' '.join(f'{v:+6.1f}' for v in d))
