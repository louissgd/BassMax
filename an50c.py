import numpy as np
from scipy.signal import butter, sosfiltfilt
from pair_io import load
SR=48000
x,_=load("Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(原始).wav")
t,_=load("Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(音效).wav")
o,_=load("Rewrite The Stars (BassMax 驱动85%).wav")
b_lo=butter(4,120/(SR/2),'low',output='sos')
b_hi=butter(4,2500/(SR/2),'high',output='sos')
def split(v):
    m=v.mean(1).astype(np.float64)
    lo=sosfiltfilt(b_lo,m); hi=sosfiltfilt(b_hi,m); mid=m-lo-hi
    return lo,mid,hi
def env(v,ms=20):
    w=max(1,int(ms*SR/1000)); p=v*v
    c=np.cumsum(np.concatenate([[0],p])); e=(c[w:]-c[:-w])/w
    return np.sqrt(e+1e-20)
Lx,Mx,Hx=split(x); Lt,Mt,Ht=split(t); Lo,Mo,Ho=split(o)
eLx,eMx,eHx=env(Lx),env(Mx),env(Hx)
eLt,eMt,eHt=env(Lt),env(Mt),env(Ht)
eLo,eMo,eHo=env(Lo),env(Mo),env(Ho)
def db(v): return 20*np.log10(v+1e-12)
A,B=49.4,51.0
print(f"{'t':>7} | {'inLF':>6}{'inMID':>7} | {'tgtLF':>6}{'tgtMID':>7} | {'ourLF':>6}{'ourMID':>7} || {'tgt gLF':>8}{'tgt gMID':>8} | {'our gLF':>8}{'our gMID':>8}")
for i in range(int(A*SR),int(B*SR),int(0.02*SR)):
    print(f"{i/SR:7.2f} | {db(eLx[i]):6.1f}{db(eMx[i]):7.1f} | {db(eLt[i]):6.1f}{db(eMt[i]):7.1f} | {db(eLo[i]):6.1f}{db(eMo[i]):7.1f} || "
          f"{db(eLt[i]/eLx[i]):8.1f}{db(eMt[i]/eMx[i]):8.1f} | {db(eLo[i]/eLx[i]):8.1f}{db(eMo[i]/eMx[i]):8.1f}")
