import numpy as np
from pair_io import load
SR=48000
x,_=load("Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(原始).wav")
t,_=load("Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(音效).wav")
xm=x.mean(1).astype(np.float64); ym=t.mean(1).astype(np.float64)
N=SR*200
xm,ym=xm[:N],ym[:N]
def win_rms(v,ms=10):
    w=int(ms*SR/1000); p=v*v
    c=np.cumsum(np.concatenate([[0],p])); e=(c[w:]-c[:-w])/w
    return np.sqrt(e+1e-20)
hop=int(0.005*SR)
def blocks(v):
    n=len(v)//hop
    return v[:n*hop].reshape(n,hop)
Lx=20*np.log10(win_rms(xm)); Ly=20*np.log10(win_rms(ym))
Lxb=blocks(Lx).mean(1); Lyb=blocks(Ly).mean(1)   # 5ms 步长
g=Lyb-Lxb
# 光标区域
i0,i1=int(49.4/0.005),int(51.2/0.005)
print("测量: target 总增益 & 输入电平 (5ms)")
print(f"{'t':>7} {'Lx':>7} {'g':>6}")
for i in range(i0,i1,4):
    print(f"{i*0.005:7.3f} {Lxb[i]:7.1f} {g[i]:6.1f}")
