# dsf_psd.py — 直接从 DSF 的 1-bit 码流算噪声整形频谱
# 目的: 量出真实 DSD 文件在 20kHz 以上的噪声电平, 验证"超声噪声"这个前提
import numpy as np
from scipy import signal
import struct, sys

PATH = r"G:\音乐\DSD\Wham! - Careless Whisper.dsf"

with open(PATH, "rb") as f:
    head = f.read(28)
    assert head[:4] == b"DSD "
    # fmt chunk
    f.seek(28)
    fmt = f.read(52)
    assert fmt[:4] == b"fmt ", fmt[:4]
    ch_num   = struct.unpack("<I", fmt[24:28])[0]
    rate     = struct.unpack("<I", fmt[28:32])[0]
    bits     = struct.unpack("<I", fmt[32:36])[0]
    # data chunk
    dc = f.read(12)
    assert dc[:4] == b"data", dc[:4]
    dsize = struct.unpack("<Q", dc[4:12])[0]
    raw = f.read(min(dsize - 12, 1 << 24))

print(f"DSD 采样率 {rate} Hz, {ch_num} 声道, {bits} bit/sample, 数据块 {dsize/1e6:.1f} MB")
print(f"解码后 PCM 率 = {rate}/{bits} = {rate//bits} Hz")

# DSF: 每个数据块内按声道分块, 4096 字节/声道; 位序 LSB first
BLK = 4096
nblk = len(raw) // (BLK * ch_num)
print(f"可用块数 {nblk}, 取 ch0")
ch0 = np.zeros(nblk * BLK * 8, dtype=np.uint8)
k = 0
for b in range(nblk):
    seg = raw[b * BLK * ch_num : b * BLK * ch_num + BLK]
    bits = np.unpackbits(np.frombuffer(seg, dtype=np.uint8), bitorder="little")
    ch0[k:k + BLK * 8] = bits
    k += BLK * 8
x = ch0[:k].astype(np.float64) * 2.0 - 1.0     # 0/1 -> -1/+1

f, P = signal.welch(x, rate, nperseg=8192, return_onesided=True)
Pdb = 10 * np.log10(P + 1e-30)
# 以 1 kHz 附近的电平为 0 dB 参考
ref = Pdb[np.argmin(np.abs(f - 1000))]
print("\n频率(Hz)   噪声/1kHz 相对电平(dB)")
for fr in [1000, 5000, 10000, 15000, 18000, 20000, 22050, 25000, 30000, 40000, 60000, 100000]:
    i = np.argmin(np.abs(f - fr))
    print(f"{fr:>9}   {Pdb[i]-ref:>8.1f}")

# 汇总: 20k 以上 vs 20k 以下的能量
def e(lo, hi):
    m = (f >= lo) & (f < hi)
    return 10 * np.log10(np.sum(P[m]) + 1e-30)
print(f"\n总能量 0-20kHz   : {e(0,20000):.1f} dB")
print(f"总能量 20k-100kHz: {e(20000,100000):.1f} dB")
print(f"总能量 >100kHz   : {e(100000,rate/2):.1f} dB")
print(f"\n超声段比可听段高 {e(20000,100000)-e(0,20000):.1f} dB")
