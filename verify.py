# -*- coding: utf-8 -*-
# 对比参考模型(ref_)与原始插件(measure_out)在各信号上的行为。
import wave, math, os, struct
from bassmax_model import run_signal, SR, read_wav

def fft(x):
    n=len(x)
    if n<=1: return x
    ev=fft(x[0::2]); od=fft(x[1::2])
    T=[complex(math.cos(-2*math.pi*k/n),math.sin(-2*math.pi*k/n))*od[k] for k in range(n//2)]
    return [ev[k]+T[k] for k in range(n//2)]+[ev[k]-T[k] for k in range(n//2)]

def rms(x): return math.sqrt(sum(v*v for v in x)/len(x)) if x else 0.0
def peak_near(sp,f,sr,N):
    c=int(round(f*N/sr)); return max((abs(v) for v in sp[c-2:c+3]))+1e-9
def h2(sig,f0,N=16384):
    s=len(sig)//2-N//2
    if s<0: s=0
    seg=sig[s:s+N]; win=[seg[i]*0.5*(1-math.cos(2*math.pi*i/(N-1))) for i in range(N)]
    sp=fft(win); c=int(round(f0*N/SR))
    return peak_near(sp,2*f0,SR,N)/peak_near(sp,f0,SR,N)

ti=read_wav("./test_signals/impulse.wav")
oi=read_wav("./measure_out/impulse.wav")
rl,rr=run_signal("impulse",0.0)
print("==== IMPULSE ====")
for tag,sig in [("orig",oi[0]),("ref",rl)]:
    pk=max(abs(v) for v in sig)
    thr=pk*0.05; onset=next(i for i,v in enumerate(sig) if abs(v)>thr)
    print("  %s: peak=%.3f (%.1f dB) latency=%d smp (%.1f ms)"%(tag,pk,20*math.log10(pk/0.5+1e-9),onset,onset/SR*1000))

ti=read_wav("./test_signals/tones.wav")
ot=read_wav("./measure_out/tones.wav")
rl,rr=run_signal("tones",0.0)
freqs=[50,100,200,500,1000,2000,5000,10000,15000,20000]; seg=int(0.5*SR)
print("\n==== TONES EQ (rms 增益 dB) ====")
print("  freq:  orig    ref    diff")
for i,f in enumerate(freqs):
    a,b=i*seg,min((i+1)*seg,len(ot[0]))
    s,e=a+2000,b-2000
    og=20*math.log10(rms(ot[0][s:e])/(rms(ti[0][s:e])+1e-9))
    rg=20*math.log10(rms(rl[s:e])/(rms(ti[0][s:e])+1e-9))
    print("  %5d: %+.2f  %+.2f  %+.2f"%(f,og,rg,rg-og))

od=read_wav("./measure_out/dc.wav"); rl,rr=run_signal("dc",0.0)
print("\n==== DC ====")
print("  orig mean=%.4f  ref mean=%.4f"%(sum(od[0])/len(od[0]), sum(rl)/len(rl)))

osq=read_wav("./measure_out/square.wav"); rl,rr=run_signal("square",0.0)
print("\n==== SQUARE H2 ====")
print("  orig H2=%.3f  ref H2=%.3f"%(h2(osq[0],1000), h2(rl,1000)))

tn=read_wav("./test_signals/noise.wav"); on=read_wav("./measure_out/noise.wav"); rl,rr=run_signal("noise",0.0)
print("\n==== NOISE 宽带增益 ====")
print("  orig =%.2f dB  ref =%.2f dB"%(20*math.log10(rms(on[0])/(rms(tn[0])+1e-9)),
      20*math.log10(rms(rl)/(rms(tn[0])+1e-9))))

ts=read_wav("./test_signals/sweep.wav"); osw=read_wav("./measure_out/sweep.wav"); rl,rr=run_signal("sweep",0.0)
print("\n==== SWEEP 宽带增益 ====")
print("  orig =%.2f dB  ref =%.2f dB"%(20*math.log10(rms(osw[0])/(rms(ts[0])+1e-9)),
      20*math.log10(rms(rl)/(rms(ts[0])+1e-9))))
