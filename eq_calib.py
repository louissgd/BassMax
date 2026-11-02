# -*- coding: utf-8 -*-
# EQ 校准(正确版): 一阶双线性高架/低架(可直接推导验证) + RBJ 峰值。
import math

SR = 44100
freqs = [50,100,200,500,1000,2000,5000,10000,15000,20000]
target = {50:3.52,100:4.20,200:0.80,500:-0.58,1000:-2.24,2000:-3.15,
          5000:-5.07,10000:-4.06,15000:-3.30,20000:-2.37}

def g2l(db): return 10.0**(db/20.0)   # dB -> 线性增益

# 一阶低架: 低频提升 G(线性), 高频=1
def lowshelf(G, f0, sr):
    K = 2.0*sr
    wc = 2.0*sr*math.tan(math.pi*f0/sr)
    n0 = K + wc*G; n1 = wc*G - K
    d0 = K + wc;     d1 = wc - K
    b0=n0/d0; b1=n1/d0; a1=d1/d0
    return (b0,b1,0.0,a1,0.0)

# 一阶高架: 高频提升 G(线性), 低频=1
def highshelf(G, f0, sr):
    K = 2.0*sr
    wc = 2.0*sr*math.tan(math.pi*f0/sr)
    n0 = K*G + wc; n1 = wc - K*G
    d0 = K + wc;    d1 = wc - K
    b0=n0/d0; b1=n1/d0; a1=d1/d0
    return (b0,b1,0.0,a1,0.0)

# 一阶高通(次声滤波): 拐点 f0
def highpass(f0, sr):
    K = 2.0*sr
    wc = 2.0*sr*math.tan(math.pi*f0/sr)
    n0 = K; n1 = -K
    d0 = K+wc; d1 = wc-K
    b0=n0/d0; b1=n1/d0; a1=d1/d0
    return (b0,b1,0.0,a1,0.0)

# RBJ 峰值(已验证正确)
def peaking(f0, q, db, sr):
    A = 10.0**(db/40.0)
    w0 = 2*math.pi*f0/sr
    cw = math.cos(w0); sw = math.sin(w0)
    alpha = sw/(2.0*q)
    b0 = 1+alpha*A; b1 = -2*cw; b2 = 1-alpha*A
    a0 = 1+alpha/A; a1 = -2*cw; a2 = 1-alpha/A
    return (b0/a0, b1/a0, b2/a0, a1/a0, a2/a0)

def resp_db(c, f, sr):
    b0,b1,b2,a1,a2 = c
    w = 2*math.pi*f/sr
    z1 = complex(math.cos(-w), math.sin(-w)); z2 = z1*z1
    num = b0 + b1*z1 + b2*z2
    den = 1 + a1*z1 + a2*z2
    return 20*math.log10(abs(num/den)+1e-12)

def cascade(cs, f, sr):
    return sum(resp_db(c,f,sr) for c in cs)

def eval_params(ls_db, ls_f, pk_db, pk_f, pk_q, hs_db, hs_f, hp_f):
    cs = []
    if hp_f>0: cs.append(highpass(hp_f, SR))
    cs.append(lowshelf(g2l(ls_db), ls_f, SR))
    cs.append(peaking(pk_f, pk_q, pk_db, SR))
    cs.append(highshelf(g2l(hs_db), hs_f, SR))
    err = sum((cascade(cs,f,SR)-target[f])**2 for f in freqs)
    return math.sqrt(err/len(freqs)), cs

best = None
for hp_f in [0, 60, 80]:
  for ls_db in [3.0,3.5,4.0,4.5,5.0,5.5]:
    for ls_f in [100,120,140,160,180,200]:
      for pk_db in [-4.0,-4.5,-5.0,-5.5,-6.0,-6.5]:
        for pk_f in [2500,3000,3500,4000,5000]:
          for pk_q in [0.4,0.5,0.6,0.7,0.8,0.9,1.0]:
            for hs_db in [-4.0,-2.0,0.0,1.0,2.0,3.0,4.0,5.0]:
              for hs_f in [4000,5000,6000,8000,10000,12000,14000,16000]:
                rms,cs = eval_params(ls_db,ls_f,pk_db,pk_f,pk_q,hs_db,hs_f,hp_f)
                if best is None or rms<best[0]:
                    best=(rms,ls_db,ls_f,pk_db,pk_f,pk_q,hs_db,hs_f,hp_f,cs)

rms,ls_db,ls_f,pk_db,pk_f,pk_q,hs_db,hs_f,hp_f,cs = best
print("最佳 RMS 误差 = %.3f dB" % rms)
print("高通(次声): %d Hz" % hp_f)
print("低架: %.1f dB @ %d Hz" % (ls_db,ls_f))
print("峰值: %.1f dB @ %d Hz, q=%.2f" % (pk_db,pk_f,pk_q))
print("高架: %.1f dB @ %d Hz" % (hs_db,hs_f))
print("\n频率:  target   ours   差")
for f in freqs:
    got = cascade(cs,f,SR)
    print("  %5d: %+.2f  %+.2f  %+.2f" % (f, target[f], got, got-target[f]))
