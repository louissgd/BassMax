# -*- coding: utf-8 -*-
"""BassMax 动态版原型: 三段分频 + 向上压缩(动态增强) + 前瞻限幅
用于在真实歌曲上与参考(原 OctiMax)比对, 参数定下来后移植到 C++ core。"""
import numpy as np
from scipy.signal import butter, sosfilt, lfilter
from scipy.ndimage import maximum_filter1d
from pair_io import load, ORIG

SR = 48000

def _follow(v, ta, tr, sr=SR):
    """峰值跟随: 快攻慢放 (近似)"""
    r = np.abs(v)
    a = 1-np.exp(-1/(ta*sr)); b = 1-np.exp(-1/(tr*sr))
    ea = lfilter([a],[1,-(1-a)], r, axis=0)
    er = lfilter([b],[1,-(1-b)], r, axis=0)
    return np.maximum(ea, er)

def render(x, drive=1.0, bass_db=4.5, ceiling=0.985, sr=SR):
    """x: (n,2) float"""
    # 分频 (LR2)
    sosL = butter(2, 180/(sr/2), 'low', output='sos')
    sosH = butter(2, 2500/(sr/2), 'high', output='sos')
    low  = sosfilt(sosL, x, axis=0)
    high = sosfilt(sosH, x, axis=0)
    mid  = x - low - high

    mono = x.mean(1)
    lvl = 20*np.log10(_follow(mono, 0.008, 0.150)+1e-9)
    up   = drive*np.minimum(18.0, 0.55*np.maximum(0.0, -15.0-lvl))
    lb   = drive*np.minimum(6.0,  0.30*np.maximum(0.0, -22.0-lvl))
    lh   = drive*np.minimum(3.0,  0.15*np.maximum(0.0, -22.0-lvl))

    gL = 10**((bass_db+lb)/20)
    gH = 10**(lh/20)
    s = low*gL[:,None] + mid + high*gH[:,None]
    s = s*10**((up + 9.0*drive)/20)[:,None]

    # 前瞻限幅
    W = int(0.005*sr)
    pk = np.abs(s).max(1)
    pk_pad = np.pad(pk, (W,0), constant_values=0)
    win = maximum_filter1d(pk_pad, size=W+1)[W:]
    req = np.minimum(1.0, ceiling/np.maximum(win,1e-9))
    # 攻/放平滑(近似): 快攻 -> 取 min 于平滑值
    a = 1-np.exp(-1/(0.0005*sr)); b = 1-np.exp(-1/(0.120*sr))
    ga = lfilter([a],[1,-(1-a)], req)
    gr = lfilter([b],[1,-(1-b)], req)
    g  = np.minimum(ga, gr)
    out = np.concatenate([np.zeros((W,2)), s[:-W]]) * g[:,None]
    return np.clip(out, -ceiling, ceiling)

if __name__ == '__main__':
    x,sr = load(ORIG)
    y,_= load("Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(音效).wav")
    for drive,bass in [(1.0,4.5),(1.0,0.0),(0.85,4.5)]:
        o = render(x[:sr*60].astype(np.float64), drive=drive, bass_db=bass)
        r = 20*np.log10(np.sqrt((o**2).mean())+1e-12)
        t = 20*np.log10(np.sqrt((y[:sr*60]**2).mean())+1e-12)
        print(f"drive={drive} bass={bass}: rms={r:6.2f} (target {t:6.2f}) peak={np.abs(o).max():.3f}")
