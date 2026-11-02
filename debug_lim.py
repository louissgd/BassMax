# -*- coding: utf-8 -*-
# 逐阶段打印 noise 的 RMS, 定位额外衰减来源。
import math
from bassmax_model import Channel, read_wav

def rms(x): return math.sqrt(sum(v*v for v in x)/len(x)) if x else 0.0

tn = read_wav("./test_signals/noise.wav")[0]
L = Channel(); L.set_drive(0.0)
after_eq=[]; after_exc=[]; after_dc=[]
seq=[]
for i in range(len(tn)):
    x=tn[i]
    x=L.ls.run(x); x=L.pk.run(x); x=L.hs.run(x)
    after_eq.append(x)
    h=L.hp.run(x); x = x + L.EXC_MIX*(h + L.EXC_K*h*h)
    after_exc.append(x)
    x=L.dc.run(x)
    after_dc.append(x)
    # limiter
    L.delay[L.wp]=x; outPos=(L.wp+1)%L.W
    pk=max((abs(v) for v in L.delay), default=0.0)
    if pk>L.env: L.env=pk
    else: L.env*=L.relCoef
    tgt=(L.env>L.ceiling) and (L.ceiling/L.env) or 1.0
    coef=L.attCoef if tgt<L.gain else (1-L.relCoef)
    L.gain+=(tgt-L.gain)*coef
    out=L.delay[outPos]*L.gain*L.makeup
    L.wp=(L.wp+1)%L.W
    seq.append(out)

print("input  RMS = %.4f" % rms(tn))
print("after EQ   RMS = %.4f (%.2f dB)" % (rms(after_eq), 20*math.log10(rms(after_eq)/(rms(tn)+1e-9))))
print("after EXC  RMS = %.4f (%.2f dB)" % (rms(after_exc), 20*math.log10(rms(after_exc)/(rms(tn)+1e-9))))
print("after DC   RMS = %.4f (%.2f dB)" % (rms(after_dc), 20*math.log10(rms(after_dc)/(rms(tn)+1e-9))))
print("final out  RMS = %.4f (%.2f dB)" % (rms(seq), 20*math.log10(rms(seq)/(rms(tn)+1e-9))))
