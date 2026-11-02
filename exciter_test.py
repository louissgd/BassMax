# -*- coding: utf-8 -*-
# 验证"高频高通门控偶次谐波激励器"假设: 稳态正弦(含20k)->0谐波, 方波->大偶次谐波。
import wave, math, struct

SR = 44100
def read_wav(path):
    w=wave.open(path,"r"); nch=w.getnchannels(); sr=w.getframerate(); n=w.getnframes()
    raw=w.readframes(n); w.close()
    return sr,[v/32768.0 for v in struct.unpack("<%dh"%(n*nch),raw)]

def fft(x):
    n=len(x)
    if n<=1: return x
    ev=fft(x[0::2]); od=fft(x[1::2])
    T=[complex(math.cos(-2*math.pi*k/n),math.sin(-2*math.pi*k/n))*od[k] for k in range(n//2)]
    return [ev[k]+T[k] for k in range(n//2)]+[ev[k]-T[k] for k in range(n//2)]

# 一阶双线性高通
def hp_coeffs(fc, sr):
    K=2*sr; wc=2*sr*math.tan(math.pi*fc/sr)
    b0=K; b1=-K; a0=K+wc; a1=wc-K
    return (b0/a0, b1/a0, a1/a0)
def hp_run(c, x, st):
    b0,b1,a1=c
    y=b0*x + b1*st[0] - a1*st[1]
    st[0]=x; st[1]=y
    return y

# 偶次谐波激励器: hp 信号经 x + k*x^2 (偶次), 混回原信号
def process(sig, fc, k, mix):
    c=hp_coeffs(fc, SR); st=[0,0]
    out=[]
    for x in sig:
        h=hp_run(c, x, st)
        ex = h + k*h*h      # 偶次谐波(含直流, 由下游 DC 阻断处理)
        out.append(x + mix*ex)
    return out

def h2_ratio(sig, f0, N=16384):
    s=len(sig)//2-N//2
    if s<0: s=0
    seg=sig[s:s+N]
    win=[seg[i]*0.5*(1-math.cos(2*math.pi*i/(N-1))) for i in range(N)]
    sp=fft(win)
    c=int(round(f0*N/SR))
    h1=max((abs(v) for v in sp[c-2:c+3]))+1e-9
    h2=max((abs(v) for v in sp[2*c-2:2*c+3])) if 2*c+3<len(sp) else 1e-9
    return h2/h1

def gen_tone(f, amp=0.5, dur=0.5):
    return [amp*math.sin(2*math.pi*f*i/SR) for i in range(int(dur*SR))]

# 载入真实方波输出(只看输入侧用程序生成也可, 这里用原插件方波的输入正弦对照)
_, sq = read_wav("./measure_out/square.wav")   # 注意这是原插件输出, 用作"有强偶次谐波"参照
# 我们用自己的激励器处理"纯净方波输入"看能否复现强偶次谐波
def gen_square(f=1000, amp=0.5, dur=0.5):
    n=int(dur*SR); out=[]
    for i in range(n):
        out.append(amp*(1.0 if (i*f//SR)%2==0 else -1.0))
    return out

for fc in [12000, 16000, 18000, 20000]:
    for k in [2.0, 5.0]:
        for mix in [0.3, 0.6]:
            sq_in = gen_square(1000)
            sq_out = process(sq_in, fc, k, mix)
            t10 = gen_tone(10000)
            t10_out = process(t10, fc, k, mix)
            t5 = gen_tone(5000)
            t5_out = process(t5, fc, k, mix)
            print("fc=%5d k=%.1f mix=%.2f | 方波H2=%.3f  10k正弦H2=%.4f  5k正弦H2=%.4f"
                  % (fc,k,mix, h2_ratio(sq_out,1000), h2_ratio(t10_out,10000), h2_ratio(t5_out,5000)))
