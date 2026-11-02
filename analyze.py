# -*- coding: utf-8 -*-
# 分析脚本（纯标准库，64 位 Python 即可）。
# 对比 test_signals/<name>.wav（原始）与 measure_out/<name>.wav（foobar 转码后）。
# 用法：py analyze.py
import wave, math, os, struct

IN_DIR = r"./test_signals"
OUT_DIR = r"./measure_out"
SR_EXPECT = 44100

def read_wav(path):
    w = wave.open(path, "r")
    nch = w.getnchannels(); sr = w.getframerate(); n = w.getnframes()
    raw = w.readframes(n)
    w.close()
    fmt = "<%dh" % (n * nch)
    data = list(struct.unpack(fmt, raw))
    return sr, nch, data  # data 为交错 int16

def to_float(data, nch):
    # 返回每个通道的 float 列表（[-1,1]）
    ch = [[] for _ in range(nch)]
    for i in range(0, len(data), nch):
        for c in range(nch):
            ch[c].append(data[i + c] / 32768.0)
    return ch

def fft(x):
    n = len(x)
    if n <= 1:
        return x
    even = fft(x[0::2]); odd = fft(x[1::2])
    T = [complex(math.cos(-2*math.pi*k/n), math.sin(-2*math.pi*k/n)) * odd[k] for k in range(n//2)]
    return [even[k] + T[k] for k in range(n//2)] + [even[k] - T[k] for k in range(n//2)]

def rms(x):
    if not x: return 0.0
    return math.sqrt(sum(v*v for v in x)/len(x))

def analyze_impulse(inL, outL):
    peak = max((abs(v) for v in outL), default=0)
    if peak < 1e-6:
        return "无输出（插件可能未生效或输出被静音）"
    thr = peak * 0.05
    onset = next((i for i, v in enumerate(outL) if abs(v) > thr), 0)
    tail = next((i for i in range(len(outL)-1, -1, -1) if abs(outL[i]) > peak*0.001), onset)
    ir_len = (tail - onset) / SR_EXPECT
    pre = any(abs(outL[i]) > thr for i in range(max(0, onset-50), onset))
    gain = peak / 0.5  # 输入冲激幅度 0.5
    lines = []
    lines.append("  起始延迟(latency): %d 样本 = %.1f ms" % (onset, onset/SR_EXPECT*1000))
    lines.append("  冲激响应长度: %.0f ms（到 -60dB）" % (ir_len*1000))
    lines.append("  前置回波(pre-ring): %s" % ("有（前瞻算法特征）" if pre else "无"))
    lines.append("  峰值增益: %.2f (%.1f dB)" % (gain, 20*math.log10(gain)))
    # IR 频率响应
    N = 32768
    seg = outL[onset:onset+N] + [0.0]*(N-len(outL[onset:onset+N]))
    if len(seg) >= N:
        sp = fft(seg)
        freqs = [31,63,125,250,500,1000,2000,4000,8000,16000]
        lines.append("  IR 频谱响应(dB，相对直流):")
        mag0 = abs(sp[0]) + 1e-9
        for f in freqs:
            b = int(f * N / SR_EXPECT)
            if b < len(sp):
                db = 20*math.log10((abs(sp[b])+1e-9)/mag0)
                lines.append("    %6d Hz: %+.1f dB" % (f, db))
    return "\n".join(lines)

def analyze_tones(inL, outL):
    seg = int(0.5 * SR_EXPECT)
    freqs = [50,100,200,500,1000,2000,5000,10000,15000,20000]
    lines = ["  频率响应(稳态增益 dB):"]
    for i, f in enumerate(freqs):
        a, b = i*seg, min((i+1)*seg, min(len(inL), len(outL)))
        if b-a < 1000: continue
        s, e = a+1000, b-1000
        inR = rms(inL[s:e]); outR = rms(outL[s:e])
        if inR < 1e-6: continue
        lines.append("    %6d Hz: %+.2f dB" % (f, 20*math.log10(outR/inR)))
    return "\n".join(lines)

def analyze_noise(inL, outL):
    m = min(len(inL), len(outL))
    g = rms(outL[:m]) / (rms(inL[:m]) + 1e-9)
    return "  宽带增益: %.2f (%.1f dB)" % (g, 20*math.log10(g))

def analyze_dc(outL):
    mean = sum(outL)/len(outL) if outL else 0
    verdict = "已隔直（好）" if abs(mean) < 0.002 else "未隔直/通过直流"
    return "  输出直流均值: %.4f -> %s" % (mean, verdict)

def analyze_square(inL, outL):
    N = 8192
    s = len(outL)//2 - N//2
    if s < 0: s = 0
    seg = outL[s:s+N]
    if len(seg) < N: return "  信号太短"
    sp = fft(seg)
    mag = [abs(v) for v in sp[:N//2]]
    # 基频 1kHz 对应 bin
    f0bin = int(1000 * N / SR_EXPECT)
    h1 = mag[f0bin] + 1e-9
    h2 = mag[2*f0bin] if 2*f0bin < len(mag) else 0
    h3 = mag[3*f0bin] if 3*f0bin < len(mag) else 0
    thd = math.sqrt(h2*h2 + h3*h3)/h1
    even = h2/h1
    lines = []
    lines.append("  2 次谐波比例: %.3f（>0.01 说明有偶次非线性/激励器特征）" % even)
    lines.append("  近似 THD(2,3 次): %.3f" % thd)
    lines.append("  判定: %s" % ("存在非线性处理" if even > 0.01 or thd > 0.02 else "基本线性"))
    return "\n".join(lines)

def cross_corr(a, b):
    n = min(len(a), len(b))
    if n == 0:
        return 0.0
    ma = sum(a[:n]) / n; mb = sum(b[:n]) / n
    num = sum((a[i]-ma)*(b[i]-mb) for i in range(n))
    da = math.sqrt(sum((a[i]-ma)**2 for i in range(n)))
    db = math.sqrt(sum((b[i]-mb)**2 for i in range(n)))
    if da < 1e-9 or db < 1e-9:
        return 0.0
    return num / (da * db)

def analyze_probe_side(inL, inR, outL, outR):
    n = min(len(inL), len(inR), len(outL), len(outR))
    inSide = rms([(inL[i]-inR[i])/2 for i in range(n)])
    outSide = rms([(outL[i]-outR[i])/2 for i in range(n)])
    outCorr = cross_corr(outL[:n], outR[:n])
    inMid = rms([(inL[i]+inR[i])/2 for i in range(n)])
    outMid = rms([(outL[i]+outR[i])/2 for i in range(n)])
    lines = []
    lines.append("  侧信号(Side)增益: %.2f dB" % (20*math.log10(outSide/(inSide+1e-9))))
    lines.append("  输出 L/R 相关系数: %.3f（输入为 -1.000；越接近 0 = 越加宽或单声道化）" % outCorr)
    lines.append("  中置(Mid)增益: %.2f dB" % (20*math.log10(outMid/(inMid+1e-9))))
    return "\n".join(lines)

def analyze_probe_decorr(inL, inR, outL, outR):
    n = min(len(inL), len(inR), len(outL), len(outR))
    inCorr = cross_corr(inL[:n], inR[:n])
    outCorr = cross_corr(outL[:n], outR[:n])
    def sm(x, y):
        M = rms([(x[i]+y[i])/2 for i in range(n)])
        S = rms([(x[i]-y[i])/2 for i in range(n)])
        return 20*math.log10((S+1e-9)/(M+1e-9))
    inSM = sm(inL[:n], inR[:n]); outSM = sm(outL[:n], outR[:n])
    gL = 20*math.log10(rms(outL[:n])/(rms(inL[:n])+1e-9))
    gR = 20*math.log10(rms(outR[:n])/(rms(inR[:n])+1e-9))
    lines = []
    lines.append("  L/R 相关系数: 输入 %.3f -> 输出 %.3f" % (inCorr, outCorr))
    lines.append("  S/M 比: 输入 %.1f dB -> 输出 %.1f dB（升高=加宽，降低=收窄/单声道化）" % (inSM, outSM))
    lines.append("  各声道增益: L %.2f dB, R %.2f dB" % (gL, gR))
    return "\n".join(lines)

def main():
    names = ["impulse", "sweep", "tones", "noise", "dc", "square",
             "probe_side", "probe_decorr"]
    for name in names:
        ip = os.path.join(IN_DIR, name + ".wav")
        op = os.path.join(OUT_DIR, name + ".wav")
        print("==== %s ====" % name)
        if not os.path.exists(op):
            print("  (缺少 measure_out/%s.wav，跳过)" % name)
            continue
        sr, nch, idata = read_wav(ip)
        _, _, odata = read_wav(op)
        ich = to_float(idata, nch); och = to_float(odata, nch)
        inL = ich[0]; inR = ich[1] if nch > 1 else ich[0]
        outL = och[0]; outR = och[1] if nch > 1 else och[0]
        if name == "impulse": print(analyze_impulse(inL, outL))
        elif name == "tones": print(analyze_tones(inL, outL))
        elif name in ("noise", "sweep"):
            print(analyze_noise(inL, outL))
            if name == "sweep":
                print("  (扫频精细分析建议用 ARTA/REW，此处只给宽带增益)")
        elif name == "dc": print(analyze_dc(outL))
        elif name == "square": print(analyze_square(inL, outL))
        elif name == "probe_side": print(analyze_probe_side(inL, inR, outL, outR))
        elif name == "probe_decorr": print(analyze_probe_decorr(inL, inR, outL, outR))
    print("\n完成。把上面的报告发给我，我来判断它属于哪类算法。")

if __name__ == "__main__":
    main()
