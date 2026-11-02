import numpy as np
from scipy.signal import stft
from pair_io import load, ORIG, FX
SR=48000
EDGES=[20,40,80,160,320,640,1280,2560,5120,10240,20000]
NAMES=[f"{EDGES[i]}-{EDGES[i+1]}" for i in range(len(EDGES)-1)]

def get(ch=0):
    x,_=load(ORIG); y,_=load(FX)
    kws=dict(fs=SR,nperseg=4096,noverlap=4096-240,window='hann',boundary=None,padded=False)
    f,_,Xc=stft(x[:,ch].astype(np.float64),**kws)
    _,_,Yc=stft(y[:,ch].astype(np.float64),**kws)
    X=np.abs(Xc); Y=np.abs(Yc)
    G=np.zeros((len(EDGES)-1,X.shape[1])); L=np.zeros_like(G)
    for i in range(len(EDGES)-1):
        m=(f>=EDGES[i])&(f<EDGES[i+1])
        ex=np.sqrt((X[m]**2).sum(0)+1e-20); ey=np.sqrt((Y[m]**2).sum(0)+1e-20)
        G[i]=20*np.log10(ey/ex); L[i]=20*np.log10(ex)
    return G,L,f

if __name__=='__main__':
    G,L,f=get()
    hop=240/SR
    print("== 各段增益曲线相关性 (看是否独立多段) ==")
    sel=slice(0,None,5)
    print("      "+" ".join(f"{n:>9}" for n in NAMES))
    for i in range(len(NAMES)):
        row=[f"{np.corrcoef(G[i][sel],G[j][sel])[0,1]:9.2f}" for j in range(len(NAMES))]
        print(f"{NAMES[i]:>10} "+" ".join(row))
    print("\n== 增益-电平 分段线性拟合: gain = a + b*(L - thr), 仅 L<thr ==")
    for i,n in enumerate(NAMES):
        l=L[i]; g=G[i]
        m=(l>-70)&(l<-8)
        if m.sum()<200: continue
        x=l[m]; yv=g[m]
        best=None
        for thr in range(-50,0,2):
            mm=x<thr
            if mm.sum()<100: continue
            A=np.vstack([np.ones(mm.sum()), x[mm]-thr]).T
            coef,res,_,_=np.linalg.lstsq(A,yv[mm],rcond=None)
            err=np.sqrt(np.mean((A@coef-yv[mm])**2))
            if best is None or err<best[0]: best=(err,thr,coef)
        err,thr,coef=best
        mm=x>thr
        top=np.median(yv[mm]) if mm.sum()>50 else np.nan
        print(f"{n:>10} thr={thr:4d}dB  slope={coef[1]:5.2f} (ratio={1/(1+coef[1]) if coef[1]>-1 else 99:4.2f}:1) g@thr={coef[0]:5.1f} g@high={top:5.1f} rms_err={err:4.2f}")
