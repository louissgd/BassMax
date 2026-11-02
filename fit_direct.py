# fit_direct.py — 用「同一批样本」把参考增益曲线映射到我们检测器的电平口径
#
# 之前 fit_bins 的病根: 用我们的 v 分箱, 却查参考的增益 -> 两个电平口径不同, 曲线错位。
# 这里改成: 用参考自身的 10ms RMS 电平分箱, 同时取 (参考增益, 我们检测器读数) 的中位,
# 得到 (v_ours -> gain_ref) 的对应关系, 再 PAVA 单调化, 直接就是能用的曲线。
import numpy as np, scipy.signal as sg, sys
from pair_io import load, ORIG, FX
from fit_bins import pava   # 复用单调化

SR = 48000
D = 328   # 原插件延迟
BANDS = [('LO', 0, 120), ('MID', 120, 2500), ('HI', 2500, 0)]


def lr4(x, lo, hi):
    y = x
    if hi:
        b, a = sg.butter(2, hi/(SR/2), 'low')
        y = sg.lfilter(b, a, y)
    if lo:
        b, a = sg.butter(2, lo/(SR/2), 'high')
        y = sg.lfilter(b, a, y)
    return y


def env10(x):
    w = int(SR*0.01)
    k = np.ones(w)/w
    return np.sqrt(np.convolve(x*x, k, 'same') + 1e-18)


def main():
    o, _ = load(ORIG)
    f, _ = load(FX)
    o = o.mean(1)
    f = f.mean(1)
    f = np.concatenate([f[D:], np.zeros(D)])

    import os
    src = sys.argv[1] if len(sys.argv) > 1 else 'g_lim.csv'
    dump = np.loadtxt(src, delimiter=',', skiprows=1)
    t = dump[:, 0]
    idx = np.clip((t*SR).astype(int), 0, len(o)-1)

    out = {}
    for k, (nm, lo, hi) in enumerate(BANDS):
        bo = lr4(o, lo, hi)
        bf = lr4(f, lo, hi)
        eo, ef = env10(bo), env10(bf)
        lv = 20*np.log10(eo[idx] + 1e-12)
        g = 20*np.log10(ef[idx]/(eo[idx] + 1e-12))
        v = dump[:, 1+k]

        pts = []
        edges = np.arange(-72, -8, 2.0)
        for i in range(len(edges)-1):
            m = (lv >= edges[i]) & (lv < edges[i+1])
            if m.sum() >= 400:
                pts.append((np.median(v[m]), np.median(g[m]), m.sum()))
        pts.sort()
        vx = np.array([p[0] for p in pts])
        gy = np.array([p[1] for p in pts])
        w  = np.array([float(p[2]) for p in pts])
        gy = pava(gy, w)                   # 单调不增 (电平越高增益越低)
        out[nm] = (vx, gy)
        print(f'--- {nm} (v_ours -> gain_ref) ---')
        for a, b, n in pts:
            print(f'   v={a:7.1f}  gain={b:6.1f}  n={n}')

    return out


if __name__ == '__main__':
    main()
