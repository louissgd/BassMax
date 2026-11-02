import numpy as np
from scipy.signal import stft
from pair_io import load
SR=48000
x,_=load("Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(原始).wav")
t,_=load("Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(音效).wav")
o,_=load("Rewrite The Stars (BassMax 驱动85%).wav")
A,B=49.5,51.0
seg=lambda v: v[int(A*SR):int(B*SR)].mean(1).astype(np.float64)
kws=dict(fs=SR,nperseg=2048,noverlap=2048-480,window='hann',boundary=None,padded=False)
f,tt,Xc=stft(seg(x),**kws); _,_,Tc=stft(seg(t),**kws); _,_,Oc=stft(seg(o),**kws)
X=np.abs(Xc); Tt=np.abs(Tc); Ot=np.abs(Oc)
edges=[30,60,120,240,480,960,1920,3840,7680,16000]
def band(X,e0,e1):
    m=(f>=e0)&(f<e1); return 20*np.log10(np.sqrt((X[m]**2).sum(0))+1e-12)
print("相对原信号的增益 (dB):  tgt / ours")
hdr="  t   " + "".join(f"{e0}-{e1:<5d}"[:9].rjust(10) for e0,e1 in zip(edges[:-1],edges[1:]))
print(hdr)
for k in range(0,len(tt),2):
    row=f"{tt[k]:6.2f} "
    for e0,e1 in zip(edges[:-1],edges[1:]):
        gx=band(X,e0,e1)[k]; gt=band(Tt,e0,e1)[k]; go=band(Ot,e0,e1)[k]
        row += f"{gt-gx:4.0f}/{go-gx:<4.0f}"
    print(row)
