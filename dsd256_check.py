# dsd256_check.py — DSD256 (1,411,200 Hz) 端到端音质对照
#
# 关键点: 这是个**动态**处理器, 增益取决于绝对电平。所以所有测试文件的音乐 RMS 必须
# 与 44.1k 参考完全一致, 否则测出来的是"电平差异"而不是"采样率差异"。
#
# 产出:
#   A_44100             参考 (音乐, 原速)
#   C_705600_clean/noisy DSD128 速率对照
#   D_1411200_clean/noisy DSD256 速率 (目标)
# noisy = 叠加 DSD 型超声噪声 (20k 以上抬升到接近满刻度)
import os
import numpy as np
from scipy import signal
from scipy.io import wavfile

ROOT = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(ROOT, "Aqua - An Apple A Day(原版).wav")
OUT = os.path.join(ROOT, "_t256")
os.makedirs(OUT, exist_ok=True)

sr, x = wavfile.read(SRC)
x = x.astype(np.float64) / 32768.0
if x.ndim == 1:
    x = np.stack([x, x], 1)
x = x[: int(20.0 * sr)]
rms_src = np.sqrt(np.mean(x ** 2))
print(f"源: {sr} Hz {x.shape[1]}ch {len(x)/sr:.1f}s peak={np.abs(x).max():.4f} rms={rms_src:.4f}")

# A: 44.1k 参考
wavfile.write(os.path.join(OUT, "A_44100.wav"), sr, (x * 32767).astype(np.int16))


def dsd_noise(n, rate, seed):
    """宏观形状模拟 DSD 噪声整形: 20k 以上单调抬升, 到奈奎斯特前接近满刻度"""
    rng = np.random.default_rng(seed)
    w = rng.standard_normal(n)
    f = np.fft.rfftfreq(n, 1.0 / rate)
    shape = np.zeros_like(f)
    band = (f >= 20000.0) & (f < rate / 2)
    shape[band] = np.linspace(0.0, 1.0, band.sum()) ** 0.9
    out = np.fft.irfft(np.fft.rfft(w) * shape, n)
    return out / (np.sqrt(np.mean(out ** 2)) + 1e-12)


def make(tag, rate, seed):
    up = rate // sr
    y = signal.resample_poly(x, up, 1, axis=0)
    # 音乐 RMS 对齐到源 -> 动态处理器看到同样的电平
    y *= rms_src / (np.sqrt(np.mean(y ** 2)) + 1e-12)

    nz = dsd_noise(y.shape[0], rate, seed)
    nz *= rms_src * 10 ** (-10 / 20.0)          # 超声段比音乐低 10 dB (保守)
    tot = y + nz[:, None]

    def w32(path, d):
        # float WAV 允许超 1.0, 不削波; 升采样本来就会产生峰间过冲, 削掉就不是原信号了
        wavfile.write(path, rate, d.astype(np.float32))

    w32(os.path.join(OUT, f"{tag}_clean.wav"), y)
    w32(os.path.join(OUT, f"{tag}_noisy.wav"), tot)
    print(f"{tag}: 音乐 rms={np.sqrt(np.mean(y**2)):.4f} peak={np.abs(y).max():.3f} | "
          f"噪声 rms={np.sqrt(np.mean(nz**2)):.4f} peak={np.abs(nz).max():.3f} | "
          f"合计 peak={np.abs(tot).max():.3f}")


make("C_705600", 705600, 11)
make("D_1411200", 1411200, 13)
print("已写出到", OUT)
