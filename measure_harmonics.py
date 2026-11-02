# -*- coding: utf-8 -*-
# 仔细测量原插件的谐波行为：方波各次谐波、tones 各频率 2 次谐波(就近找峰避免 bin 错位)。
import wave, math, os, struct

IN_DIR = r"./test_signals"
OUT_DIR = r"./measure_out"
SR = 44100

def read_wav(path):
    w = wave.open(path, "r")
    nch = w.getnchannels(); sr = w.getframerate(); n = w.getnframes()
    raw = w.readframes(n); w.close()
    data = list(struct.unpack("<%dh" % (n*nch), raw))
    ch = [[], []]
    for i in range(0, len(data), nch):
        for c in range(nch):
            ch[c].append(data[i+c]/32768.0)
    return sr, ch

def fft(x):
    n = len(x)
    if n <= 1: return x
    ev = fft(x[0::2]); od = fft(x[1::2])
    T = [complex(math.cos(-2*math.pi*k/n), math.sin(-2*math.pi*k/n))*od[k] for k in range(n//2)]
    return [ev[k]+T[k] for k in range(n//2)] + [ev[k]-T[k] for k in range(n//2)]

def peak_mag(sp, f, sr, N, span=3):
    # 在期望频率 f 附近 ±span 个 bin 找最大幅度（避免 bin 错位）
    c = int(round(f*N/sr)); m = 0.0; bf = c
    for b in range(max(1,c-span), c+span+1):
        if b < len(sp) and abs(sp[b]) > m: m = abs(sp[b]); bf = b
    return m, bf

def harm_profile(path, f0, N=16384, seg_from_end=0):
    sr, ch = read_wav(path)
    sig = ch[0]
    s = len(sig)//2 - N//2
    if s < 0: s = 0
    seg = sig[s:s+N]
    win = [seg[i]*0.5*(1-math.cos(2*math.pi*i/(N-1))) for i in range(N)]
    sp = fft(win)
    out = {}
    for h in (1,2,3,4,5):
        m, _ = peak_mag(sp, f0*h, sr, N)
        out[h] = m
    h1 = out[1]+1e-9
    print("  %s @%dHz: H1=%.4f H2/H1=%.3f H3/H1=%.3f H4/H1=%.3f H5/H1=%.3f"
          % (os.path.basename(path), f0, out[1], out[2]/h1, out[3]/h1, out[4]/h1, out[5]/h1))
    return out

print("=== 原插件 方波 谐波(measure_out/square.wav) ===")
harm_profile(os.path.join(OUT_DIR,"square.wav"), 1000)

print("\n=== 原插件 tones 各频率 2 次谐波 near-peak ===")
sr, tin = read_wav(os.path.join(IN_DIR,"tones.wav"))
_,   tout = read_wav(os.path.join(OUT_DIR,"tones.wav"))
freqs = [50,100,200,500,1000,2000,5000,10000,15000,20000]
seg = int(0.5*SR)
N = 8192
for i,f in enumerate(freqs):
    a,b = i*seg, min((i+1)*seg, len(tout[0]))
    mid = (a+b)//2
    s = mid - N//2
    if s < 0: s = 0
    if s+N > len(tout[0]): s = len(tout[0])-N
    win = [tout[0][j]*0.5*(1-math.cos(2*math.pi*(j-s)/(N-1))) for j in range(s,s+N)]
    sp = fft(win)
    h1,_ = peak_mag(sp, f, SR, N)
    h2,_ = peak_mag(sp, 2*f, SR, N)
    r = (h2/(h1+1e-9)) if h1>1e-9 else 0.0
    print("  %6d Hz: H1=%.4f H2/H1=%.4f" % (f, h1, r))

print("\n=== 原插件 noise 频谱包络(看激励器是否抬高频) ===")
sr, nin = read_wav(os.path.join(IN_DIR,"noise.wav"))
_,   nout = read_wav(os.path.join(OUT_DIR,"noise.wav"))
N = 32768
s = len(nout[0])//2 - N//2
def band_db(sig, lo, hi):
    seg = sig[s:s+N]
    win = [seg[i]*0.5*(1-math.cos(2*math.pi*i/(N-1))) for i in range(N)]
    sp = fft(win)
    lo_b, hi_b = int(lo*N/SR), int(hi*N/SR)
    p = sum(abs(sp[b])**2 for b in range(lo_b,hi_b))/max(1,(hi_b-lo_b))
    return math.sqrt(p)
for lo,hi in [(20,200),(200,1000),(1000,4000),(4000,8000),(8000,16000)]:
    gi = band_db(nin[0], lo, hi); go = band_db(nout[0], lo, hi)
    print("  %5d-%5d Hz: in=%.4f out=%.4f 增益 %.2f dB" % (lo,hi,gi,go,20*math.log10(go/(gi+1e-9))))
