# -*- coding: utf-8 -*-
# BassMax 核心 DSP 原型（纯 Python，验证算法机制，不依赖 numpy）。
# 验证三点：限制器钳到上限、激励器产 2 次谐波、包络平滑不抽吸。
import math

SR = 44100

def dc_block(x, a=0.995):
    y = [0.0]*len(x); px=0.0; py=0.0
    for n in range(len(x)):
        c = x[n]-px + a*py
        y[n]=c; px=x[n]; py=c
    return y

def saturator(x, k):
    # 偶对称激励器：y = x + k*x^2（去 DC 后产 2 次谐波）
    y = [xi + k*xi*xi for xi in x]
    m = sum(y)/len(y)
    return [v-m for v in y]

def limiter(x, ceiling, release_s=0.9, att_ms=1.0):
    out=[0.0]*len(x); env=0.0; gain=1.0
    rc=math.exp(-1.0/(release_s*SR)); ac=math.exp(-1.0/(att_ms/1000.0*SR))
    for n in range(len(x)):
        a=abs(x[n])
        env = a if a>env else env*rc
        target = ceiling/env if env>ceiling else 1.0
        coef = ac if target<gain else (1-rc)
        gain = gain + (target-gain)*coef
        out[n]=x[n]*gain
    return out

def sine(f, amp, dur):
    return [amp*math.sin(2*math.pi*f*i/SR) for i in range(int(dur*SR))]

def goertzel(samples, freq):
    N=len(samples); k=int(0.5+freq*N/SR); w=2*math.pi*k/N; cw=math.cos(w); coeff=2*cw
    s1=s2=0.0
    for x in samples:
        s0=x+s1*coeff-s2; s2=s1; s1=s0
    return math.sqrt(s1*s1+s2*s2-s1*s2*coeff)

def report(tag, x):
    pk=max(abs(v) for v in x)
    h1=goertzel(x,1000); h2=goertzel(x,2000)
    print("  [%s] peak=%.3f  2次谐波/基波=%.3f" % (tag, pk, h2/(h1+1e-9)))

def main():
    s = sine(1000, 0.5, 0.3)
    sat = saturator(s, 0.6)              # 激励器
    print("激励器(ceiling=1.0, 默认):")
    report("default", limiter(sat, 1.0))  # 默认上限 ~0dBFS，峰值基本不动
    print("驱动拉满(ceiling=0.1):")
    report("maxdrive", limiter(sat, 0.1)) # 上限掉到 0.1
    # 平滑性：幅度阶跃，看增益包络是否平缓过渡（不抽吸）
    sig = sine(1000,0.5,0.3) + [0.0]* (int(0.3*SR))  # 后半静音看释放
    lim = limiter(saturator(sig,0.6), 0.1)
    print("  释放段末值(应平滑趋近0，无突变): %.4f" % lim[-1])

if __name__=="__main__":
    main()
