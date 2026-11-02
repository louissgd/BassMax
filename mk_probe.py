# -*- coding: utf-8 -*-
"""生成探针信号: 鼓点 + 静音尾巴, 用来检查压缩器释放是否把底噪抬起来"""
import numpy as np, scipy.io.wavfile as wf

sr = 44100
T = 1.6
n = int(T * sr)
rng = np.random.default_rng(7)

x = rng.normal(0, 10 ** (-78 / 20), n)          # -78dB 极低底噪

# 0.10s 处: 60Hz 低音爆发 4 周期
t0 = int(0.10 * sr)
f0 = 60.0
for i in range(int(4 * sr / f0)):
    tt = i / sr
    x[t0 + i] += 0.55 * np.exp(-tt / 0.03) * np.sin(2 * np.pi * f0 * tt)

# 0.55s 处: 2kHz 短促瞬态 (模拟镲片/军鼓)
t1 = int(0.55 * sr)
for i in range(int(0.03 * sr)):
    tt = i / sr
    x[t1 + i] += 0.35 * np.exp(-tt / 0.008) * np.sin(2 * np.pi * 2000 * tt)

st = np.stack([x, x], 1)
wf.write("probe_in.wav", sr, (np.clip(st, -1, 1) * 32767).astype(np.int16))
print("probe_in.wav", len(x) / sr, "s")
