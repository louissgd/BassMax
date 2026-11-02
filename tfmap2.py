import numpy as np
from scipy.signal import butter, sosfiltfilt
from pair_io import load
SR=48000
x,_=load("Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(原始).wav")
t,_=load("Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(音效).wav")
import sys
o,_=load(sys.argv[1] if len(sys.argv)>1 else "out3.wav")
def bp(v,lo,hi):
    sos=butter(4,[lo/(SR/2),hi/(SR/2)],'band',output='sos')
    return sosfiltfilt(sos, v.mean(1).astype(np.float64))
bands=[(30,120),(120,500),(500,2500),(2500,16000)]
sigX=[bp(x,a,b) for a,b in bands]
sigT=[bp(t,a,b) for a,b in bands]
sigO=[bp(o,a,b) for a,b in bands]
def env(v,ms=40):
    w=int(ms*SR/1000); p=v*v; c=np.cumsum(np.concatenate([[0],p])); e=(c[w:]-c[:-w])/w
    return 20*np.log10(np.sqrt(e+1e-20))
EX=[env(s) for s in sigX]; ET=[env(s) for s in sigT]; EO=[env(s) for s in sigO]
print("各段增益: tgt/ours  (40ms 平滑)")
print(f"{'t':>7} " + " ".join(f"{a}-{b:<6}".rjust(14) for a,b in bands))
for i in range(int(49.4*SR),int(51.2*SR),int(0.04*SR)):
    row=f"{i/SR:7.2f} "
    for k in range(4):
        row += f"{ET[k][i]-EX[k][i]:6.1f}/{EO[k][i]-EX[k][i]:<6.1f}"
    print(row)
