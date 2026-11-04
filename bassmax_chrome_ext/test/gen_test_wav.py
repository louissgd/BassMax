# gen_test_wav.py — 生成用于 JS/C++ 一致性对比的测试信号 (24bit 立体声)
import struct, math, random

SR = 48000
SEC = 8.0
N = int(SR * SEC)
random.seed(1234)

L = [0.0] * N
R = [0.0] * N

def add(i, l, r):
    if 0 <= i < N:
        L[i] += l
        R[i] += r

# 1) 55Hz + 110Hz 低音正弦, 带 2Hz 幅度起伏 (考验低频包络跟随)
for i in range(N):
    t = i / SR
    env = 0.30 * (0.65 + 0.35 * math.sin(2 * math.pi * 2.0 * t))
    s = math.sin(2 * math.pi * 55 * t) + 0.4 * math.sin(2 * math.pi * 110 * t + 1.1)
    add(i, env * s, env * s * 0.93)

# 2) 每 0.5s 一个鼓点 (短促冲击, 考验增益攻/放与限幅)
for k in range(int(SEC / 0.5)):
    st = int(k * 0.5 * SR) + 137
    for j in range(int(0.18 * SR)):
        t = j / SR
        e = math.exp(-t * 26.0)
        v = 0.55 * e * math.sin(2 * math.pi * 62 * t)
        n = 0.20 * e * (random.random() * 2 - 1)
        add(st + j, v + n, v * 0.96 + n * 0.8)

# 3) 中频和弦 (300~3000Hz), 每 2s 换一次
for seg in range(4):
    st = int(seg * 2.0 * SR)
    dur = int(2.0 * SR)
    freqs = [[220, 277, 330], [247, 311, 370], [196, 247, 294], [262, 330, 392]][seg]
    for j in range(dur):
        t = j / SR
        a = 0.10 * min(1.0, j / (0.05 * SR)) * math.exp(-t * 0.5)
        s = sum(math.sin(2 * math.pi * f * t + f) for f in freqs) / 3.0
        add(st + j, a * s, a * s * 0.8)

# 4) 高频: 8k 短促镲片 + 12k 微弱持续
for k in range(int(SEC / 0.25)):
    st = int(k * 0.25 * SR) + 61
    for j in range(int(0.06 * SR)):
        t = j / SR
        e = math.exp(-t * 90.0)
        v = 0.25 * e * (random.random() * 2 - 1)
        add(st + j, v, -v)
for i in range(N):
    t = i / SR
    v = 0.012 * math.sin(2 * math.pi * 12000 * t)
    add(i, v, v)

# 5) 最后 1.2s 静音 (考验释放)
for i in range(int(6.8 * SR), N):
    L[i] = 0.0
    R[i] = 0.0

peak = max(1e-9, max(max(abs(x) for x in L), max(abs(x) for x in R)))
g = 0.89 / peak
data = bytearray()
for i in range(N):
    for x in (L[i] * g, R[i] * g):
        v = int(round(max(-1.0, min(1.0, x)) * 8388607))
        data += struct.pack('<i', v)[:3]

with open('in.wav', 'wb') as f:
    f.write(b'RIFF'); f.write(struct.pack('<I', 36 + len(data))); f.write(b'WAVE')
    f.write(b'fmt '); f.write(struct.pack('<IHHIIHH', 16, 1, 2, SR, SR * 6, 6, 24))
    f.write(b'data'); f.write(struct.pack('<I', len(data))); f.write(data)
print('in.wav written', N, 'frames')
