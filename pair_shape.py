import numpy as np
from scipy.signal import stft
from pair_io import load, ORIG, FX
SR=48000
EDGES=[20,40,80,160,320,640,1280,2560,5120,10240,20000]

def analyze(ch=0):
    x,_ = load(ORIG); y,_ = load(FX)
    xm = x[:,ch].astype(np.float64); ym = y[:,ch].astype(np.float64)
    kws = dict(fs=SR, nperseg=4096, noverlap=4096-240, window='hann', boundary=None, padded=False)
    f,_,Xc = stft(xm, **kws); _,_,Yc = stft(ym, **kws)
    X=np.abs(Xc); Y=np.abs(Yc)
    g={}; lv={}
    for i in range(len(EDGES)-1):
        m=(f>=EDGES[i])&(f<EDGES[i+1])
        ex=np.sqrt((X[m]**2).sum(0)+1e-20); ey=np.sqrt((Y[m]**2).sum(0)+1e-20)
        g[i]=20*np.log10(ey/ex); lv[i]=20*np.log10(ex)
    return f,g,lv

if __name__=='__main__':
    f,g,lv = analyze()
    ref = 6  # 1280-2560 参考
    print("每段 EQ 形状(相对 1.3-2.5k),按各段自身电平分组 -> 看是否动态")
    print(f"{'band':>10} " + " ".join(f"L{p:<6d}" for p in (-60,-40,-25)))
    for i in range(len(EDGES)-1):
        row=[]
        for target in (-60,-40,-25):
            m=(np.abs(lv[i]-target)<3)
            if m.sum()<50: row.append("   -   "); continue
            gg=np.median(g[i][m])-np.median(g[ref][m])
            row.append(f"{gg:6.1f} ")
        print(f"{EDGES[i]:5d}-{EDGES[i+1]:<5d} " + " ".join(row))
