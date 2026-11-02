import numpy as np
from scipy.signal import stft
from pair_io import load, ORIG, FX
SR=48000

def spec(v):
    f,t,Z = stft(v, fs=SR, nperseg=4096, noverlap=4096-480, window='hann', boundary=None, padded=False)
    return f, t, np.abs(Z)

def bands_of(f):
    edges = [20,40,80,160,320,640,1280,2560,5120,10240,20000]
    return [(edges[i],edges[i+1]) for i in range(len(edges)-1)]

if __name__ == '__main__':
    x,sr = load(ORIG); y,_ = load(FX)
    xm = x.mean(1).astype(np.float64); ym = y.mean(1).astype(np.float64)
    f,t,X = spec(xm); _,_,Y = spec(ym)
    print("frames", X.shape, "freq bins", len(f))
    for lo,hi in bands_of(f):
        m = (f>=lo)&(f<hi)
        ex = np.sqrt((X[m]**2).sum(0)+1e-20)
        ey = np.sqrt((Y[m]**2).sum(0)+1e-20)
        lx = 20*np.log10(ex); ly = 20*np.log10(ey)
        g = ly-lx
        sel = (lx>-75)&(lx<-5)
        if sel.sum()<100: 
            print(f"{lo:5d}-{hi:<5d} insufficient"); continue
        lv = lx[sel]; gv = g[sel]
        qs = np.percentile(lv,[10,30,50,70,90])
        out=[]
        for q in qs:
            mm = sel & (np.abs(lx-q)<1.5)
            out.append(f"{np.median(g[mm]):6.1f}" if mm.sum()>30 else "     -")
        print(f"{lo:5d}-{hi:<5d} lvl={qs.round(1)} gain@{np.round(qs,1)}: {' '.join(out)}  med_gain={np.median(gv):5.1f}")
