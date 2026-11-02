# -*- coding: utf-8 -*-
"""紧凑指标: 一次对比多首歌, 只看「动态保真」相关的几项"""
import sys, os
import numpy as np
import scipy.signal as sg
from pair_io import load

PAIRS = [
    ("Rewrite", "Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(原始).wav",
     "Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(音效).wav"),
    ("风雨无阻", "周华健 - 风雨无阻.24bit(原始).wav",
     "周华健 - 风雨无阻.24bit(音效).wav"),
    ("Bryan", "Bryan Adams - Here I Am(原始).wav",
     "Bryan Adams - Here I Am(音效).wav"),
]


def band(x, sr, lo, hi):
    y = x
    if hi:
        b, a = sg.butter(4, hi / (sr / 2), 'low')
        y = sg.lfilter(b, a, y)
    if lo:
        b, a = sg.butter(4, lo / (sr / 2), 'high')
        y = sg.lfilter(b, a, y)
    return y


def env(x, sr, ms):
    w = max(1, int(sr * ms / 1000))
    k = np.ones(w) / w
    return np.sqrt(np.convolve(x * x, k, 'same') + 1e-18)


def blk_rms_db(v, sr, sec=0.5):
    h = int(sec * sr)
    n = len(v) // h
    return 20 * np.log10(np.sqrt((v[:n * h].reshape(n, h) ** 2).mean(1)) + 1e-12)


def env_jitter(v, sr):
    """10ms 包络的一阶差分 RMS * 1000 -> 「动态活跃度」"""
    e = 20 * np.log10(env(v, sr, 10) + 1e-12)
    return np.percentile(np.abs(np.diff(e)), 95)


def metrics(path):
    out = []
    for nm, org, fx in PAIRS:
        o, sr = load(org)
        t, _ = load(fx)
        p, _ = load(path[0] if isinstance(path, tuple) else path) if False else (None, None)
        out.append(None)
    return out


def lagof(o, f, sr, lo, hi):
    pp = int(0.04 * sr); s = int(40 * sr); N = int(20 * sr)
    a = band(o[s:s+N], sr, lo, hi); b = band(f[s-pp:s+N+pp], sr, lo, hi)
    return int(np.argmax(np.abs(np.correlate(b, a, 'valid')))) - pp


def lo_corr(o, t, p, sr):
    L = lagof(o, t, sr, 0, 120)
    ft = np.concatenate([t[L:], np.zeros(L)])
    eo = env(band(o, sr, 0, 120), sr, 10)
    et = env(band(ft, sr, 0, 120), sr, 10)
    ep = env(band(p, sr, 0, 120), sr, 10)
    m = eo > 1e-3
    a = 20*np.log10(eo[m])
    ct = np.corrcoef(a, 20*np.log10(et[m]))[0,1]
    cp = np.corrcoef(a, 20*np.log10(ep[m]))[0,1]
    return ct, cp


def one(ours_paths, label=""):
    print(f'=== {label} ===')
    for (nm, org, fx), ours in zip(PAIRS, ours_paths):
        o, sr = load(org)
        t, _ = load(fx)
        p, _ = load(ours)
        N = min(len(o), len(t), len(p))
        o, t, p = o[:N], t[:N], p[:N]
        om, tm, pm = o.mean(1), t.mean(1), p.mean(1)
        ro, rt = blk_rms_db(pm, sr), blk_rms_db(tm, sr)
        n = min(len(ro), len(rt))
        corr = np.corrcoef(ro[:n], rt[:n])[0, 1]
        dmed = np.median(ro[:n] - rt[:n])
        # 动态活跃度 (低频 / 全带)
        bl = band(pm, sr, 0, 120); blo = band(om, sr, 0, 120); blt = band(tm, sr, 0, 120)
        jt = env_jitter(blt, sr); jo = env_jitter(bl, sr)
        jta = env_jitter(tm, sr); joa = env_jitter(pm, sr)
        # 分段增益
        gains = []
        for lo, hi in [(30, 120), (120, 500), (500, 2500), (2500, 16000)]:
            go = 20 * np.log10(env(band(tm, sr, lo, hi), sr, 20) / (env(band(om, sr, lo, hi), sr, 20) + 1e-12) + 1e-12)
            gp = 20 * np.log10(env(band(pm, sr, lo, hi), sr, 20) / (env(band(om, sr, lo, hi), sr, 20) + 1e-12) + 1e-12)
            gains.append((np.median(go), np.median(gp)))
        gtxt = '  '.join(f'{a:.1f}/{b:.1f}' for a, b in gains)
        peak = np.abs(p).max(); pk_t = np.abs(t).max()
        rms = 20 * np.log10(np.sqrt((pm ** 2).mean()))
        rms_t = 20 * np.log10(np.sqrt((tm ** 2).mean()))
        print(f'{nm:6s} 相关 {corr:.3f}  ΔRMS {dmed:+.2f}dB  响度 {rms:.2f}/{rms_t:.2f}  '
              f'峰 {peak:.3f}/{pk_t:.3f}  峰因 {peak/(10**(rms/20)):.2f}/{pk_t/(10**(rms_t/20)):.2f}')
        ct, cp = lo_corr(om, tm, pm, sr)
        print(f'       低频活跃 {jo:.3f}/{jt:.3f} ({jo/jt:.2f}x)   全带活跃 {joa:.3f}/{jta:.3f} ({joa/jta:.2f}x)'
              f'   低频包络相关 ref {ct:.3f} ours {cp:.3f}')
        print(f'       分段增益 tgt/ours {gtxt}')


if __name__ == '__main__':
    one(sys.argv[2:], sys.argv[1] if len(sys.argv) > 1 else "")
