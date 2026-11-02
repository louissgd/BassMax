import numpy as np
from scipy.signal import butter, sosfiltfilt
from pair_io import load
SR=48000
x,_=load("Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(原始).wav")
t,_=load("Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(音效).wav")
def bp(v,lo,hi):
    sos=butter(4,[lo/(SR/2),hi/(SR/2)],'band',output='sos')
    return sosfiltfilt(sos, v.mean(1).astype(np.float64))
BANDS=[(30,120),(120,2500),(2500,16000)]
def env(v,ms):
    w=int(ms*SR/1000); p=v*v; c=np.cumsum(np.concatenate([[0],p])); e=(c[w:]-c[:-w])/w
    return 20*np.log10(np.sqrt(e+1e-20))
for lo,hi in BANDS:
    sx=bp(x,lo,hi); st=bp(t,lo,hi)
    for ms in (20,40,80):
        Lx=env(sx,ms); Ly=env(st,ms); g=Ly-Lx
        m=(Lx>-70)&(Lx<-5)
        print(f"band {lo}-{hi} smooth={ms}ms")
        for lv in (-60,-50,-45,-40,-35,-30,-25,-20,-15,-12):
            mm=m&(np.abs(Lx-lv)<1.5)
            if mm.sum()<100: continue
            print(f"   L={lv:4d}  gain={np.median(g[mm]):6.1f}  IQR={np.percentile(g[mm],75)-np.percentile(g[mm],25):5.1f}")
