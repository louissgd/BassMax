# -*- coding: utf-8 -*-
# BassMax 重写参考模型(与将要写的 C++ bassmax_core.hpp 一一对应)。
# 链路: 低架(+4.5@120) -> 峰值(-4.5@4k,Q0.4) -> 高架/高切(-2.0@5k)
#      -> 激励器(20k 高通门控偶次谐波) -> DC 阻断 -> 前瞻限制器(ceiling 随 drive)
import wave, math, os, struct

SR = 44100
g2l = lambda db: 10.0**(db/20.0)

# ---------- 双二阶/一阶系数 ----------
def lowshelf(G, f0, sr):
    K=2*sr; wc=2*sr*math.tan(math.pi*f0/sr)
    n0=K+wc*G; n1=wc*G-K; d0=K+wc; d1=wc-K
    return (n0/d0,n1/d0,d1/d0)            # (b0,b1,a1)
def highshelf(G, f0, sr):
    K=2*sr; wc=2*sr*math.tan(math.pi*f0/sr)
    n0=K*G+wc; n1=wc-K*G; d0=K+wc; d1=wc-K
    return (n0/d0,n1/d0,d1/d0)
def highpass(f0, sr):
    K=2*sr; wc=2*sr*math.tan(math.pi*f0/sr)
    return (K/(K+wc), -K/(K+wc), (wc-K)/(K+wc))
def peaking(f0, q, db, sr):
    A=10.0**(db/40.0); w0=2*math.pi*f0/sr; cw=math.cos(w0); sw=math.sin(w0)
    alpha=sw/(2*q)
    b0=1+alpha*A; b1=-2*cw; b2=1-alpha*A
    a0=1+alpha/A; a1=-2*cw; a2=1-alpha/A
    return (b0/a0,b1/a0,b2/a0,a1/a0,a2/a0)

# ---------- 处理单元(带状态) ----------
class Shelf1:   # 一阶 b0*x + b1*x1 - a1*y1
    def __init__(s,c): s.b0,s.b1,s.a1=c; s.x1=0; s.y1=0
    def run(s,x):
        y=s.b0*x + s.b1*s.x1 - s.a1*s.y1
        s.x1=x; s.y1=y; return y
class Peak2:   # 二阶 RBJ
    def __init__(s,c): s.b0,s.b1,s.b2,s.a1,s.a2=c; s.x1=s.x2=s.y1=s.y2=0
    def run(s,x):
        y=s.b0*x+s.b1*s.x1+s.b2*s.x2 - s.a1*s.y1 - s.a2*s.y2
        s.x2=s.x1; s.x1=x; s.y2=s.y1; s.y1=y; return y
class DCBlock:
    def __init__(s,p=0.995): s.p=p; s.x1=0; s.y1=0
    def run(s,x):
        y=x - s.x1 + s.p*s.y1; s.x1=x; s.y1=y; return y

class Channel:
    def __init__(s):
        s.ls=Shelf1(lowshelf(g2l(4.5),120,SR))
        s.pk=Peak2(peaking(4000,0.40,-4.5,SR))
        s.hs=Shelf1(highshelf(g2l(-2.0),5000,SR))
        s.hp=Shelf1(highpass(20000,SR))
        s.dc=DCBlock(0.995)
        s.EXC_K=2.0; s.EXC_MIX=0.30
        # 限制器
        s.W=int(0.0075*SR); s.delay=[0.0]*s.W; s.wp=0
        s.gain=1.0; s.env=0.0
        s.ceiling=1.0; s.makeup=1.0
        s.relCoef=1-math.exp(-1/(0.9*SR)); s.attCoef=1-math.exp(-1/(0.001*SR))
    def set_drive(s,drive): s.ceiling = 1.0 - 0.9*drive
    def process(s,x):
        x=s.ls.run(x); x=s.pk.run(x); x=s.hs.run(x)
        h=s.hp.run(x)
        x = x + s.EXC_MIX*(h + s.EXC_K*h*h)     # 激励器(偶次谐波), 下游 DC 阻断去直流
        x=s.dc.run(x)
        s.delay[s.wp]=x
        outPos=(s.wp+1)%s.W
        pk=max((abs(v) for v in s.delay), default=0.0)
        if pk>s.env: s.env=pk
        else: s.env*=s.relCoef
        tgt = (s.env>s.ceiling) and (s.ceiling/s.env) or 1.0
        coef = s.attCoef if tgt<s.gain else (1-s.relCoef)
        s.gain += (tgt-s.gain)*coef
        out = s.delay[outPos]*s.gain*s.makeup
        s.wp=(s.wp+1)%s.W
        return out

def read_wav(path):
    w=wave.open(path,"r"); nch=w.getnchannels(); sr=w.getframerate(); n=w.getnframes()
    raw=w.readframes(n); w.close()
    ch=[[],[]]
    data=struct.unpack("<%dh"%(n*nch),raw)
    for i in range(0,len(data),nch):
        for c in range(nch): ch[c].append(data[i+c]/32768.0)
    return ch

def write_wav(path, ch):
    w=wave.open(path,"w"); w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR)
    out=[]
    for i in range(len(ch[0])):
        for c in range(2):
            v=max(-1.0,min(1.0,ch[c][i])); out.append(int(round(v*32767)))
    w.writeframes(struct.pack("<%dh"%len(out),*out)); w.close()

def run_signal(name, drive=0.0, outpath=None):
    ich=read_wav(os.path.join("./test_signals",name+".wav"))
    L=Channel(); R=Channel(); L.set_drive(drive); R.set_drive(drive)
    ol=[]; orr=[]
    n=len(ich[0])
    for i in range(n):
        ol.append(L.process(ich[0][i]))
        orr.append(R.process(ich[1][i]))
    if outpath: write_wav(outpath,[ol,orr])
    return ol,orr

if __name__=="__main__":
    print("生成参考模型输出(驱动=0 默认)...")
    for name in ["impulse","tones","noise","dc","square","sweep"]:
        run_signal(name, 0.0, os.path.join("./calib_out","ref_%s.wav"%name))
    print("完成 -> calib_out/ref_*.wav")
