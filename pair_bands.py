import numpy as np
from scipy.signal import butter, sosfiltfilt, hilbert
from pair_io import load, ORIG, FX

SR = 48000
BANDS = [(30,60),(60,120),(120,250),(250,500),(500,1000),(1000,2000),
         (2000,4000),(4000,8000),(8000,12000),(12000,16000)]

def band_env(v, lo, hi, sr=SR):
    sos = butter(4, [lo/(sr/2), min(hi,sr/2*0.95)/(sr/2)], btype='band', output='sos')
    b = sosfiltfilt(sos, v)
    # 10ms 滑动 RMS
    w = int(0.010*sr)
    e = np.convolve(b*b, np.ones(w)/w, 'same')
    return np.sqrt(e + 1e-20)

if __name__ == '__main__':
    x,sr = load(ORIG); y,_ = load(FX)
    xm = x.mean(1); ym = y.mean(1)
    L = len(xm)
    shift = 328
    ym2 = np.concatenate([np.zeros(shift), ym[:-shift]])
    print(f"{'band':>12} {'medIn':>7} {'medGain':>8} {'g@-40':>7} {'g@-30':>7} {'g@-20':>7} {'g@-12':>7}")
    for lo,hi in BANDS:
        ex = band_env(xm, lo, hi)
        ey = band_env(ym2, lo, hi)
        lx = 20*np.log10(ex+1e-12); ly = 20*np.log10(ey+1e-12)
        g = ly - lx
        m = (lx > -70) & (ly > -70)
        med = np.median(g[m]) if m.any() else float('nan')
        row = [f"{lo:5d}-{hi:<5d}"]
        row.append(f"{np.median(lx[m]):7.1f}")
        row.append(f"{med:8.1f}")
        for lvl in (-40,-30,-20,-12):
            sel = m & (np.abs(lx-lvl) < 2.0)
            row.append(f"{np.median(g[sel]):7.1f}" if sel.sum() > 50 else f"{'-':>7}")
        print(' '.join(row))
