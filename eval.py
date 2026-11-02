# -*- coding: utf-8 -*-
"""统一评估: 与参考 WAV 的响度/分段/瞬态一致性"""
import sys, os
import numpy as np
from scipy.signal import butter, sosfiltfilt
from pair_io import load

SR = 48000
TGT = os.environ.get("PAIR_FX",  "Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(音效).wav")
ORG = os.environ.get("PAIR_ORG", "Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(原始).wav")

def bp(v, lo, hi):
    sos = butter(4, [lo/(SR/2), hi/(SR/2)], 'band', output='sos')
    return sosfiltfilt(sos, v.mean(1).astype(np.float64))

def env(v, ms=20):
    w = int(ms*SR/1000); p = v*v
    c = np.cumsum(np.concatenate([[0], p])); e = (c[w:]-c[:-w])/w
    out = np.sqrt(e+1e-20)
    return np.concatenate([np.full(w//2, out[0]), out, np.full(max(0, len(v)-w//2-len(out)), out[-1])])[:len(v)]

def blk(v, sec=0.5):
    h = int(sec*SR); n = len(v)//h
    return 20*np.log10(np.sqrt((v[:n*h].reshape(n, h, -1)**2).mean(axis=(1, 2)))+1e-12)

def report(path, label=""):
    x, _ = load(ORG); t, _ = load(TGT); o, _ = load(path)
    N = min(len(x), len(t), len(o))
    x, t, o = x[:N], t[:N], o[:N]
    ro, rt = blk(o), blk(t); n = min(len(ro), len(rt))
    print(f"[{label}] 0.5s RMS: 差中位={np.median(ro[:n]-rt[:n]):+.2f} 相关={np.corrcoef(ro[:n], rt[:n])[0,1]:.3f}", end="")
    print(f" | RMS={20*np.log10(np.sqrt((o**2).mean())):.2f}/{20*np.log10(np.sqrt((t**2).mean())):.2f}"
          f" 峰={np.abs(o).max():.3f}/{np.abs(t).max():.3f}"
          f" 峰因={np.abs(o).max()/np.sqrt((o**2).mean()):.2f}/{np.abs(t).max()/np.sqrt((t**2).mean()):.2f}")
    # 分段增益 (中位)
    row = []
    for lo, hi in [(30,120),(120,500),(500,2500),(2500,16000)]:
        sx, st, so = bp(x,lo,hi), bp(t,lo,hi), bp(o,lo,hi)
        db=lambda a: 20*np.log10(a+1e-12)
        Lx, Lt, Lo = db(env(sx)), db(env(st)), db(env(so))
        m = Lx > -70
        row.append(f"{lo}-{hi}:{np.median((Lt-Lx)[m]):.1f}/{np.median((Lo-Lx)[m]):.1f}")
    print("    分段增益 tgt/ours  " + "  ".join(row))
    # 瞬态段 50.0-51.0 的分段增益
    i0, i1 = int(50.0*SR), int(51.0*SR)
    row = []
    for lo, hi in [(30,120),(120,500),(500,2500),(2500,16000)]:
        sx, st, so = bp(x,lo,hi), bp(t,lo,hi), bp(o,lo,hi)
        db=lambda a: 20*np.log10(a+1e-12)
        row.append(f"{lo}-{hi}:{np.median((db(env(st))-db(env(sx)))[i0:i1]):.1f}/{np.median((db(env(so))-db(env(sx)))[i0:i1]):.1f}")
    print("    50-51s 增益 tgt/ours  " + "  ".join(row))
    # 瞬态处 30ms 内的电平跳变 (泵动指标): 前后 200ms 的电平差
    seg = np.abs(o[i0:i1]).max(1)
    print(f"    50-51s 电平抖动 (10ms RMS 标准差) ours={np.std(blk(o[i0:i1],0.01)):.2f} tgt={np.std(blk(t[i0:i1],0.01)):.2f}")

if __name__ == '__main__':
    for p in sys.argv[1:] or ['out3.wav']:
        report(p, p)
