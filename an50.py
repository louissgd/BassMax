import numpy as np
from scipy.signal import stft
from pair_io import load
SR=48000
x,sr=load("Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(原始).wav")
t,_=load("Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(音效).wav")
o,_=load("Rewrite The Stars (BassMax 驱动85%).wav")
def seg(v,a,b): return v[int(a*SR):int(b*SR)]
A,B=46,54
for name,v in [("orig",x),("tgt",t),("ours",o)]:
    s=seg(v,A,B)
    print(f"{name:5s} peak={np.abs(s).max():.3f} rms={20*np.log10(np.sqrt((s**2).mean())):6.2f}")
# 包络: 1ms RMS
def env(v,ms=1.0):
    w=int(ms*SR/1000); p=v.mean(1).astype(np.float64)**2
    c=np.cumsum(np.concatenate([[0],p])); e=(c[w:]-c[:-w])/w
    return 20*np.log10(np.sqrt(e+1e-20)), w
eo,_=env(x); et,_=env(t); oo,_=env(o)
n=min(len(eo),len(et),len(oo))
i0,i1=int(A*SR),int(B*SR)
print("\n时间(s)  origRMS  tgtRMS  oursRMS | tgtGain oursGain")
for i in range(i0,i1,int(0.05*SR)):
    print(f"{i/SR:7.2f} {eo[i]:7.1f} {et[i]:7.1f} {oo[i]:7.1f} | {et[i]-eo[i]:7.1f} {oo[i]-eo[i]:7.1f}")
