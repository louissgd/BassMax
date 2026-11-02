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
    lo=sosfiltfilt(b_lo,m); hi=sosfiltfilt(b_hi,m); return lo, m-lo-hi, hi
def smooth(v,ms):
    w=max(1,int(ms*SR/1000)); k=np.ones(w)/w
    return np.convolve(v,k,'same')
def resid(sig_orig, sig_out, a, b, smooth_ms=40):
    """最优慢增益 g(t) 后的残差 / 输出能量 -> 失真度(dB)"""
    i0,i1=int(a*SR),int(b*SR)
    xo=sig_orig[i0:i1]; yo=sig_out[i0:i1]
    g=smooth(xo*yo,-smooth_ms if False else smooth_ms)/ (smooth(xo*xo,smooth_ms)+1e-12)
    r=yo-g*xo
    return 10*np.log10((r*r).sum()/(yo*yo).sum()+1e-20)
def band_thd(sig, a, b, f0=55.0):
    i0,i1=int(a*SR),int(b*SR)
    s=sig[i0:i1]*np.hanning(i1-i0)
    S=np.abs(np.fft.rfft(s)); f=np.fft.rfftfreq(len(s),1/SR)
    def e(fc,w=3.0):
        m=np.abs(f-fc)<w; return S[m].mean() if m.any() else 1e-9
    base=e(f0)
    hs=[e(2*f0),e(3*f0),e(4*f0)]
    return [20*np.log10(h/base+1e-12) for h in hs]
for name,v in [("orig",x),("tgt ",t),("ours",o)]:
    L,M,H=split(v)
    print(f"{name}: 残差dB LF={resid(Lx:=split(x)[0],L,50.0,51.0):6.1f} MID={resid(split(x)[1],M,50.0,51.0):6.1f}  "
          f"THD@55Hz (2nd,3rd,4th)={['%.1f'%v2 for v2 in band_thd(L,50.1,50.9)]}")
