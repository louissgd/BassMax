# dsd_probe.py — 判断 DSD 采样率下音质变差的原因
#
# 对照三条路径 (同一段 44.1k 音乐):
#   A: 直接 44.1k 处理                    (基准, 已验证音色正确)
#   B: 升到 352.8k 处理再降回 44.1k        (只有采样率变高, 内容干净)
#   C: 升到 352.8k + 加 DSD 超声噪声, 处理后再降回 44.1k
#
# 判读:
#   B 明显差于 A  -> 双二阶系数在 float 下的精度问题 (低频滤波器失谐)
#   C 明显差于 B  -> 超声噪声进入非线性/检测器, 产生互调并把高音段压死
import numpy as np
from scipy.io import wavfile
from scipy import signal
import sys, os

SRC = r"Aqua - An Apple A Day(原版).wav"
OUT = r"C:\Users\Administrator\WorkBuddy\Claw\_probe"
os.makedirs(OUT, exist_ok=True)

sr, x = wavfile.read(SRC)
assert x.dtype == np.int16 and sr == 44100
x = x.astype(np.float64) / 32768.0
if x.ndim == 1:
    x = np.stack([x, x], 1)
dur = 20.0
x = x[: int(dur * sr)]

# 关键: 所有分支必须 RMS 对齐, 否则压缩器会按电平给出不同增益, 比较就失效了
TARGET_RMS = 0.10                       # -20 dBFS
def norm_rms(d):
    return d * (TARGET_RMS / (np.sqrt(np.mean(d ** 2)) + 1e-12))
x = norm_rms(x)
print(f"源: {sr} Hz, {x.shape[1]}ch, {len(x)/sr:.1f}s  RMS={np.sqrt(np.mean(x**2)):.4f}  peak={np.abs(x).max():.4f}")

# ---- 升采样 (polyphase, 高质量) ----
UP = 8
HI = sr * UP                      # 352800
y = norm_rms(signal.resample_poly(x, UP, 1, axis=0))
print(f"升到 {HI} Hz: RMS={np.sqrt(np.mean(y**2)):.4f}  peak={np.abs(y).max():.4f}")

# ---- DSD 风格超声噪声 ----
# DSD64 的噪声整形把量化噪声堆到 20k 以上, 靠近 fs/2 处接近满刻度
def dsd_noise(n, fs, seed=7):
    rng = np.random.default_rng(seed)
    w = rng.standard_normal(n)
    W = np.fft.rfft(w)
    f = np.fft.rfftfreq(n, 1.0 / fs)
    # 20k 以下清零; 20k 以上按 (f/20k)^2.0 爬升 (约 +12dB/oct, 接近二阶整形)
    shape = np.where(f > 20000.0, (f / 20000.0) ** 2.0, 0.0)
    W = W * shape
    z = np.fft.irfft(W, n)
    z /= (np.abs(z).max() + 1e-12)
    return z

n = y.shape[0]
nz = dsd_noise(n, HI)
# 真实 DSD: 噪声在 20k 以上接近满刻度。这里把噪声超声段 RMS 设成比音乐低 14 dB,
# 再把总和压到不削波, 保持和原始音乐同量级的 RMS。
nz *= (10 ** (-14 / 20.0)) / (np.sqrt(np.mean(nz ** 2)) + 1e-12)
tot = y + nz[:, None]
pk = np.abs(tot).max()
if pk > 0.98:
    k = 0.98 / pk
    y  = y  * k
    nz = nz * k
print(f"超声噪声: peak={np.abs(nz).max():.4f}  rms={np.sqrt(np.mean(nz**2)):.4f}")

def write(path, data, fs):
    d = np.clip(data, -1.0, 1.0)
    wavfile.write(path, fs, (d * 32767.0).astype(np.int16))

write(os.path.join(OUT, "A_44100.wav"), x, sr)
write(os.path.join(OUT, "B_352800_clean.wav"), y, HI)
write(os.path.join(OUT, "C_352800_noisy.wav"), y + nz[:, None], HI)

# 705.6k (DSD128) 也来一份
UP2 = 16
HI2 = sr * UP2
y2 = norm_rms(signal.resample_poly(x, UP2, 1, axis=0))
nz2 = dsd_noise(y2.shape[0], HI2, seed=11)
nz2 *= (10 ** (-14 / 20.0)) / (np.sqrt(np.mean(nz2 ** 2)) + 1e-12)
pk2 = np.abs(y2 + nz2[:, None]).max()
if pk2 > 0.98:
    k2 = 0.98 / pk2
    y2  = y2  * k2
    nz2 = nz2 * k2
write(os.path.join(OUT, "D_705600_clean.wav"), y2, HI2)
write(os.path.join(OUT, "E_705600_noisy.wav"), y2 + nz2[:, None], HI2)
print("已写出 5 个测试文件到", OUT)
