# -*- coding: utf-8 -*-
"""与参考的 1/3 倍频程时频偏差 (越小越接近原插件)"""
import sys, numpy as np
from scipy.signal import stft
from pair_io import load
SR=48000
TGT="Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(音效).wav"
_,_=load(TGT)
def tf(v,a,b):
    m=v.mean(1).astype(np.float64)
    f,t,Z=stft(m[int(a*SR):int(b*SR)], fs=SR, nperseg=4096, noverlap=4096-960, window='hann', boundary=None, padded=False)
    return f,t,np.abs(Z)
def bands(f):
    fc=1000*2**(np.arange(-17,10)/3)
    return [c for c in fc if c>25 and c<18000]
def levels(Z,f,fc):
    return np.array([20*np.log10(np.sqrt((Z[(f>=c/2**(1/6))&(f<c*2**(1/6))]**2).mean(0))+1e-12) for c in fc])
t,_=load(TGT)
for path in sys.argv[1:]:
    o,_=load(path)
    out=[]
    for a,b in [(45,56),(0,60),(0,281)]:
        f,tt,Zt=tf(t,a,b); _,_,Zo=tf(o,a,b)
        fc=bands(f)
        Lt=levels(Zt,f,fc); Lo=levels(Zo,f,fc)
        mask=Lt>-60
        d=np.abs(Lt-Lo)
        off=np.median((Lo-Lt)[mask])          # 整体电平偏移
        dn=np.abs((Lt+off)-Lo)                # 电平对齐后的偏差 (只看动态/频谱)
        print(f"{path}  {a}-{b}s: 偏差={d[mask].mean():.2f}dB  对齐后={dn[mask].mean():.2f}dB  电平偏移={off:+.2f}dB")
