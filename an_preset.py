# -*- coding: utf-8 -*-
"""六预设 vs 原版音效 的 1/3 倍频程谱差"""
import sys, numpy as np, scipy.signal as sg
from pair_io import load

SETS = [
    ("风雨无阻", "周华健 - 风雨无阻.24bit(原始).wav", "周华健 - 风雨无阻.24bit(音效).wav", "t3_%d.wav"),
    ("Bryan", "Bryan Adams - Here I Am(原始).wav", "Bryan Adams - Here I Am(音效).wav", "tb_%d.wav"),
]
NAMES = ["标准乐", "古典乐", "俱乐部", "流行乐", "爵士乐", "摇滚乐"]

CENTERS = [50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000,
           1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000]


def band_db(x, sr, nfft=8192):
    """返回 1/3 oct 各频带功率中位数 (dB)"""
    n = len(x)
    hop = nfft // 2
    frames = []
    for s in range(0, n - nfft, hop):
        frames.append(x[s:s + nfft])
    if not frames:
        frames = [x]
    X = np.fft.rfft(np.array(frames) * np.hanning(nfft), axis=1)
    P = (np.abs(X) ** 2)                     # frames x bins
    f = np.fft.rfftfreq(nfft, 1.0 / sr)
    out = []
    for c in CENTERS:
        lo, hi = c / 2 ** (1 / 6), c * 2 ** (1 / 6)
        m = (f >= lo) & (f < hi)
        if not m.any():
            out.append(np.nan); continue
        e = P[:, m].sum(1)
        out.append(10 * np.log10(np.median(e) + 1e-20))
    return np.array(out)


for nm, org, ref, pat in SETS:
    o, sr = load(org); t, _ = load(ref)
    N = int(60 * sr)
    om, tm = o[:N].mean(1), t[:N].mean(1)
    bo = band_db(om, sr)
    br = band_db(tm, sr)
    print(f"\n===== {nm} =====")
    hdr = "预设     " + " ".join(f"{c:>5d}" for c in CENTERS)
    print(hdr)
    print("ref-org  " + " ".join(f"{v:>5.1f}" for v in (br - bo)))
    scores = []
    for p in range(6):
        x, _ = load(pat % p)
        bx = band_db(x[:N].mean(1), sr)
        d = bx - br
        # 只统计 63Hz-12.5k 的偏差
        sel = (np.array(CENTERS) >= 63) & (np.array(CENTERS) <= 12500)
        rms = np.sqrt(np.nanmean(d[sel] ** 2))
        scores.append((rms, p, d))
        print(f"P{p} {NAMES[p]:<4s}" + " ".join(f"{v:>5.1f}" for v in d) + f"   rms={rms:.2f}")
    scores.sort()
    print("最接近:", " > ".join(f"{NAMES[p]}({r:.2f}dB)" for r, p, _ in scores[:3]))
