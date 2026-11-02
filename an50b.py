import numpy as np
from scipy.signal import butter, sosfiltfilt
from pair_io import load
SR=48000
x,_=load("Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(原始).wav")
t,_=load("Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(音效).wav")
o,_=load("Rewrite The Stars (BassMax 驱动85%).wav")
lo=butter(4,120/(SR/2),'low',output='sos')
def band(v): return sosfiltfilt(lo, v.mean(1).astype(np.float64))
bx,bt,bo=band(x),band(t),band(o)
def env(v,ms=5):
    w=int(ms*SR/1000); p=v*v
    c=np.cumsum(np.concatenate([[0],p])); e=(c[w:]-c[:-w])/w
    return np.sqrt(e+1e-20)
ex,et,eo=env(bx),env(bt),env(bo)
print("低频(<=120Hz) 包络, 5ms 步长")
print(" t     orig   tgt   ours | tgtG oursG")
for i in range(int(49.0*SR),int(51.0*SR),int(0.01*SR)):
    print(f"{i/SR:6.2f} {20*np.log10(ex[i]+1e-12):7.1f} {20*np.log10(et[i]+1e-12):7.1f} {20*np.log10(eo[i]+1e-12):7.1f} | {20*np.log10(et[i]/ex[i]+1e-12):6.1f} {20*np.log10(eo[i]/ex[i]+1e-12):6.1f}")
