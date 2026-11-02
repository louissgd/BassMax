# -*- coding: utf-8 -*-
# 生成干净测试信号（纯标准库，64 位 Python 即可运行）。
# 产物在 test_signals/，交给 foobar2000 转码（开启 OctiMax DSP）后，
# 把输出 WAV 用相同文件名放进 measure_out/，再用 analyze.py 对比。
import wave, math, os, struct, random

OUT = r"./test_signals"
SR = 44100
NCH = 2

def write_wav(path, samples_int16):
    w = wave.open(path, "w")
    w.setnchannels(NCH); w.setsampwidth(2); w.setframerate(SR)
    w.writeframes(struct.pack("<%dh" % len(samples_int16), *samples_int16))
    w.close()

def to_int16(flt):
    return [max(-32768, min(32767, int(round(x * 32767)))) for x in flt]

def stereo(mono):
    return [v for x in mono for v in (x, x)]

def impulse():
    # 0.5 冲激 + 1 秒静音：捕获冲激响应/拖尾/前瞻延迟
    return [0.5] + [0.0] * SR

def sweep():
    dur = 5.0; n = int(dur * SR); out = [0.0] * n
    f0, f1 = 20.0, 20000.0
    for i in range(n):
        t = i / SR
        f = f0 * (f1 / f0) ** (i / n)
        out[i] = 0.5 * math.sin(2 * math.pi * f * t)
    return out

def tones():
    freqs = [50, 100, 200, 500, 1000, 2000, 5000, 10000, 15000, 20000]
    out = []
    for f in freqs:
        for i in range(int(0.5 * SR)):
            out.append(0.5 * math.sin(2 * math.pi * f * i / SR))
    return out

def noise():
    return [0.3 * (random.random() * 2 - 1) for _ in range(int(1.0 * SR))]

def dc():
    return [0.5] * int(0.5 * SR)

def square():
    return [0.5 * (1.0 if (i * 1000 // SR) % 2 == 0 else -1.0)
            for i in range(int(0.5 * SR))]

def probe_side():
    # 纯侧信号：L=扫频, R=-扫频（完全反相，相关系 -1）。
    # 测插件是否动 side（加宽）、是否把反相信号单声道化。
    dur = 5.0; n = int(dur * SR)
    out = []
    f0, f1 = 20.0, 20000.0
    for i in range(n):
        t = i / SR
        f = f0 * (f1 / f0) ** (i / n)
        s = 0.5 * math.sin(2 * math.pi * f * t)
        out.append(s); out.append(-s)  # 已是交错立体声
    return out

def probe_decorr():
    # 左右去相关噪声（独立种子）：测立体声扩展量（S/M 变化、相关系数变化）。
    rnd = random.Random(12345)
    n = int(2.0 * SR)
    out = []
    for i in range(n):
        out.append(0.3 * (rnd.random() * 2 - 1))   # L
        out.append(0.3 * (rnd.random() * 2 - 1))   # R
    return out

def main():
    os.makedirs(OUT, exist_ok=True)
    tests = {
        "impulse": impulse(),
        "sweep": sweep(),
        "tones": tones(),
        "noise": noise(),
        "dc": dc(),
        "square": square(),
        "probe_side": probe_side(),      # 已是交错立体声
        "probe_decorr": probe_decorr(),  # 已是交错立体声
    }
    for name, sig in tests.items():
        int16 = sig if name.startswith("probe_") else stereo(sig)
        write_wav(os.path.join(OUT, name + ".wav"), to_int16(int16))
        print("generated", name)
    print("完成。把 test_signals/ 里的文件拖进 foobar2000，开启 OctiMax 转码成 WAV，"
          "输出用同名放进 measure_out/")

if __name__ == "__main__":
    main()
