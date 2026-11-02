# -*- coding: utf-8 -*-
# BassMax 重写核心校准台（Python 版，与 bassmax_core.hpp 算法一一对应）。
# 目的: 用 measure_out/ 原插件实测，反推 SAT_MAX 与 EQ 系数。
# 用法: python3 bassmax_calib.py
import wave, math, os, struct

BASE = r"."
IN_DIR = os.path.join(BASE, "test_signals")
OUT_DIR = os.path.join(BASE, "measure_out")
SR = 44100

# ---- 与 C++ bassmax_core.hpp 完全一致的 DSP ----
class Biquad:
    def __init__(self):
        self.b0=self.b1=self.b2=self.a1=self.a2=0.0
        self.z1=self.z2=0.0
    def set(self, f0, q, db, sr, shelf):
        A = 10**(db/20.0)
        w0 = 2*math.pi*f0/sr
        cw=math.cos(w0); sw=math.sin(w0)
        alpha = sw/(2*q)
        if shelf:
            sq = 2*math.sqrt(A)*alpha
            b0 = A*((A+1)-(A-1)*cw+sq); b1 = 2*A*((A-1)-(A+1)*cw); b2 = A*((A+1)-(A-1)*cw-sq)
            a0 = (A+1)+(A-1)*cw+sq; a1 = -2*((A+1)+(A-1)*cw); a2 = (A+1)+(A-1)*cw-sq
        else:
            b0=1+alpha*A; b1=-2*cw; b2=1-alpha*A
            a0=1+alpha/A; a1=-2*cw; a2=1-alpha/A
        self.b0=b0/a0; self.b1=b1/a0; self.b2=b2/a0; self.a1=a1/a0; self.a2=a2/a0
    def run(self, x):
        y = self.b0*x + self.b1*self.z1 + self.b2*self.z2 - self.a1*self.z1 - self.a2*self.z2
        self.z2=self.z1; self.z1=y
        return y

class Channel:
    SAT_MAX = 2.2  # 校准目标
    def __init__(self):
        self.sh=Biquad(); self.pk=Biquad()
        self.dcX=0.0; self.dcY=0.0
        self.satK=2.0; self.ceiling=1.0; self.makeup=1.0
        self.gain=1.0; self.env=0.0
        self.W=0; self.wp=0; self.delay=[]
    def set_params(self, drive, excite, sr):
        self.sh.set(150.0,0.9,4.0,sr,True)
        self.pk.set(4000.0,0.8,-5.0,sr,False)
        self.ceiling = 1.0 - 0.9*drive
        self.satK = self.SAT_MAX * excite
        self.makeup = 1.0
        self.relCoef = 1-math.exp(-1/(0.9*sr))
        self.attCoef = 1-math.exp(-1/(0.001*sr))
        self.W = int(0.0075*sr)
        if len(self.delay)!=self.W: self.delay=[0.0]*self.W
        self.gain=1.0; self.env=0.0; self.wp=0
    def process(self, x):
        x = self.sh.run(x)
        x = self.pk.run(x)
        x = x + self.satK*x*x
        y = x - self.dcX + 0.995*self.dcY; self.dcX=x; self.dcY=y
        self.delay[self.wp]=y
        outPos=(self.wp+1)%self.W
        pk=0.0
        for v in self.delay:
            if abs(v)>pk: pk=abs(v)
        if pk>self.env: self.env=pk
        else: self.env*=self.relCoef
        target = (self.env>self.ceiling) and (self.ceiling/self.env) or 1.0
        coef = self.attCoef if target<self.gain else (1-self.relCoef)
        self.gain += (target-self.gain)*coef
        out = self.delay[outPos]*self.gain*self.makeup
        self.wp=(self.wp+1)%self.W
        return out

def fft(x):
    n=len(x)
    if n<=1: return x
    ev=fft(x[0::2]); od=fft(x[1::2])
    T=[complex(math.cos(-2*math.pi*k/n),math.sin(-2*math.pi*k/n))*od[k] for k in range(n//2)]
    return [ev[k]+T[k] for k in range(n//2)]+[ev[k]-T[k] for k in range(n//2)]

def read_wav(path):
    w=wave.open(path,"r"); nch=w.getnchannels(); sr=w.getframerate(); n=w.getnframes()
    raw=w.readframes(n); w.close()
    data=list(struct.unpack("<%dh"%(n*nch),raw))
    ch=[[],[]]
    for i in range(0,len(data),nch):
        for c in range(nch): ch[c].append(data[i+c]/32768.0)
    return sr, ch

def snd_harmonic(sig, sr, f):
    N=4096; off=5000; seg=sig[off:off+N]
    if len(seg)<N: return 0.0
    win=[seg[i]*0.5*(1-math.cos(2*math.pi*i/(N-1))) for i in range(N)]
    sp=fft(win)
    mag=[abs(v) for v in sp[:N//2]]
    f0=int(f*N/sr); h1=mag[f0]+1e-9; h2=mag[2*f0] if 2*f0<len(mag) else 0
    return h2/h1

# ---- 加载原始 tones 输入/输出 ----
sr, tin = read_wav(os.path.join(IN_DIR,"tones.wav"))
_,   tout = read_wav(os.path.join(OUT_DIR,"tones.wav"))
freqs=[50,100,200,500,1000,2000,5000,10000,15000,20000]
seg=int(0.5*SR)

print("=== 原插件 tones EQ (rms 增益 dB, 来自 measure_out) ===")
orig_eq=[]
for i,f in enumerate(freqs):
    a,b=i*seg,min((i+1)*seg,len(tout[0]))
    s,e=a+2000,b-2000
    g=math.sqrt(sum(v*v for v in tout[0][s:e])/len(tout[0][s:e]))/(math.sqrt(sum(v*v for v in tin[0][s:e])/len(tin[0][s:e]))+1e-9)
    orig_eq.append(20*math.log10(g))
    print("  %6d Hz: %+.2f dB"%(f,orig_eq[-1]))

print("=== 原插件 各频率 2 次谐波比 (来自 measure_out/tones) ===")
orig_h2=[]
for i,f in enumerate(freqs):
    a,b=i*seg,min((i+1)*seg,len(tout[0]))
    s,e=a+2000,b-2000
    r=snd_harmonic(tout[0][s:e],sr,f); orig_h2.append(r)
    print("  %6d Hz: %.3f"%(f,r))

# ---- 我们的模型: 扫描 excite ----
for excite in [0.0,0.5,1.0,2.0]:
    chL=Channel(); chR=Channel(); chL.set_params(0.0,excite,SR); chR.set_params(0.0,excite,SR)
    outL=[chL.process(tin[0][i]) for i in range(len(tin[0]))]
    # EQ 曲线 (rms per freq) 与 2 次谐波
    our_eq=[]; our_h2=[]
    for i,f in enumerate(freqs):
        a,b=i*seg,min((i+1)*seg,len(outL))
        s,e=a+2000,b-2000
        g=math.sqrt(sum(v*v for v in outL[s:e])/len(outL[s:e]))/(math.sqrt(sum(v*v for v in tin[0][s:e])/len(tin[0][s:e]))+1e-9)
        our_eq.append(20*math.log10(g))
        our_h2.append(snd_harmonic(outL[s:e],sr,f))
    print("\n--- 我们模型 excite=%.2f (SAT_MAX=%.2f -> satK=%.2f) ---"%(excite,Channel.SAT_MAX,Channel.SAT_MAX*excite))
    print("  频率:  origEQ  ourEQ | origH2  ourH2")
    for i,f in enumerate(freqs):
        print("  %6d: %+.2f %+.2f | %.3f %.3f"%(f,orig_eq[i],our_eq[i],orig_h2[i],our_h2[i]))
