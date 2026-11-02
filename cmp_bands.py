import numpy as np, sys
from scipy.signal import butter, sosfiltfilt
from pair_io import load
SR=48000
x,_=load("Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(原始).wav")
t,_=load("Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(音效).wav")
o,_=load(sys.argv[1] if len(sys.argv)>1 else "out3.wav")
def bp(v,lo,hi):
    sos=butter(4,[lo/(SR/2),hi/(SR/2)],'band',output='sos')
    return sosfiltfilt(sos,v.mean(1).astype(np.float64))
def env(v,ms=20):
    w=int(ms*SR/1000); p=v*v; c=np.cumsum(np.concatenate([[0],p])); e=(c[w:]-c[:-w])/w
    return 20*np.log10(np.sqrt(e+1e-20))
BANDS=[(30,120),(120,2500),(2500,16000)]
N=SR*150
print(f"{'band':>12} {'tgtG':>6} {'ourG':>6}   tgtG-ourG(中位)")
for lo,hi in BANDS:
    sx=bp(x,lo,hi)[:N]; st=bp(t,lo,hi)[:N]; so=bp(o,lo,hi)[:N]
    Lx=env(sx); Lt=env(st); Lo=env(so)
    m=Lx>-70
    gt=np.median((Lt-Lx)[m]); go=np.median((Lo-Lx)[m])
    print(f"{lo:5d}-{hi:<6d} {gt:6.1f} {go:6.1f}   {np.median((Lt-Lo)[m]):6.1f}")
def blk(v,sec=0.5):
    h=int(sec*SR); n=len(v)//h
    return 20*np.log10(np.sqrt((v[:n*h].reshape(n,h,-1)**2).mean(axis=(1,2)))+1e-12)
ro,rt=blk(o),blk(t); n=min(len(ro),len(rt))
print(f"0.5s RMS差中位={np.median(ro[:n]-rt[:n]):+.1f} 相关={np.corrcoef(ro[:n],rt[:n])[0,1]:.3f}")
print(f"RMS our={20*np.log10(np.sqrt((o[:N]**2).mean())):.2f} tgt={20*np.log10(np.sqrt((t[:N]**2).mean())):.2f} 峰={np.abs(o).max():.3f}/{np.abs(t).max():.3f} 峰因={np.abs(o).max()/np.sqrt((o**2).mean()):.2f}/{np.abs(t).max()/np.sqrt((t**2).mean()):.2f}")
