# -*- coding: utf-8 -*-
"""检查压缩器释放是否把底噪/尾巴抬起来 (「房间混响感」的机制)"""
import numpy as np, scipy.signal as sg
from pair_io import load

i, sr = load("probe_in.wav")
o, _ = load("probe_out.wav")
b, _ = load("probe_bypass.wav")
i, o, b = i.mean(1), o.mean(1), b.mean(1)
n = min(len(i), len(o))
i, o, b = i[:n], o[:n], b[:n]


def band(x, lo, hi, sr):
    y = x
    if lo:
        y = sg.lfilter(*sg.butter(4, lo / (sr / 2), 'high'), y)
    if hi:
        y = sg.lfilter(*sg.butter(4, hi / (sr / 2), 'low'), y)
    return y


def env_db(x, ms=10):
    w = max(1, int(sr * ms / 1000))
    k = np.ones(w) / w
    e = np.sqrt(np.convolve(x * x, k, 'same') + 1e-20)
    return 20 * np.log10(e + 1e-20)


print("=== 全带包络 (dB) ===")
for t in [0.05, 0.09, 0.10, 0.15, 0.20, 0.30, 0.40, 0.50, 0.55, 0.60, 0.70, 0.90, 1.20]:
    s = int(t * sr)
    seg = slice(s, s + int(0.02 * sr))
    print(f"t={t:.2f}s  in {20*np.log10(np.sqrt((i[seg]**2).mean())+1e-12):7.1f}"
          f"   bypass {20*np.log10(np.sqrt((b[seg]**2).mean())+1e-12):7.1f}"
          f"   out {20*np.log10(np.sqrt((o[seg]**2).mean())+1e-12):7.1f}")

print("\n=== 40-120Hz 带包络 (dB) ===")
li, lo, lb = band(i, 40, 120, sr), band(o, 40, 120, sr), band(b, 40, 120, sr)
for t in [0.09, 0.12, 0.16, 0.20, 0.25, 0.30, 0.35, 0.40, 0.50, 0.60, 0.80, 1.00]:
    s = int(t * sr)
    seg = slice(s, s + int(0.02 * sr))
    a = 20 * np.log10(np.sqrt((li[seg]**2).mean()) + 1e-12)
    c = 20 * np.log10(np.sqrt((lb[seg]**2).mean()) + 1e-12)
    d = 20 * np.log10(np.sqrt((lo[seg]**2).mean()) + 1e-12)
    print(f"t={t:.2f}s  in {a:7.1f}   out {d:7.1f}   gain {d-c:+6.1f}dB")

print("\n=== 爆发后尾巴: 输出比输入多出来的能量 (dB, 40-120Hz) ===")
for t in [0.20, 0.30, 0.45, 0.60, 0.90]:
    s = int(t * sr)
    seg = slice(s, s + int(0.05 * sr))
    gi = 20 * np.log10(np.sqrt((li[seg]**2).mean()) + 1e-12)
    go = 20 * np.log10(np.sqrt((lo[seg]**2).mean()) + 1e-12)
    print(f"t={t:.2f}s  提升 {go-gi:+6.1f} dB")
