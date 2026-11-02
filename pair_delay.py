import numpy as np
from scipy.signal import butter, sosfiltfilt
from pair_io import load, ORIG, FX
SR=48000
x,sr = load(ORIG); y,_ = load(FX)
xm = x.mean(1); ym = y.mean(1)

def gcc(a, b, span):
    # GCC-PHAT on segment
    n = 1
    while n < len(a)+len(b): n*=2
    A = np.fft.rfft(a, n); B = np.fft.rfft(b, n)
    R = A*np.conj(B); R /= (np.abs(R)+1e-12)
    cc = np.fft.irfft(R, n)
    cc = np.concatenate([cc[-span:], cc[:span]])
    return np.argmax(cc) - span

def band(v, lo, hi):
    sos = butter(4, [lo/(SR/2), hi/(SR/2)], btype='band', output='sos')
    return sosfiltfilt(sos, v)

for lo,hi in [(40,120),(120,400),(400,1200),(1200,4000),(4000,12000)]:
    xb = band(xm, lo, hi); yb = band(ym, lo, hi)
    lags=[]
    for t in [20,45,70,95,120,150,180,210,240,265]:
        a = xb[int(t*SR):int((t+0.5)*SR)]
        b = yb[int(t*SR)-1000:int((t+0.5)*SR)+1000]
        lags.append(gcc(a,b,1000))
    print(f"{lo:5d}-{hi:<6d}", lags, "median", int(np.median(lags)))
