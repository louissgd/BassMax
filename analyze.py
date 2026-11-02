# analyze.py — 比较各路径输出的频段平衡 (电平无关, 只看音色)
import numpy as np
from scipy.io import wavfile
from scipy import signal
import os

OUT = r"C:\Users\Administrator\WorkBuddy\Claw\_probe"
BANDS = [(40,80),(80,160),(160,320),(320,640),(640,1280),(1280,2560),
         (2560,5120),(5120,10240),(10240,16000)]
REF = (320,1280)      # 以此段为 0 dB 基准

def load(path):
    sr, d = wavfile.read(path)
    if d.dtype == np.int16: d = d.astype(np.float64)/32768.0
    elif d.dtype == np.int32: d = d.astype(np.float64)/2147483648.0
    if d.ndim == 1: d = np.stack([d,d],1)
    return sr, d

def to441(d, sr):
    if sr == 44100: return d
    return signal.resample_poly(d, 44100, sr, axis=0)

def bands(d, sr):
    n = len(d)
    f, P = signal.welch(d[:,0], sr, nperseg=16384)
    e = []
    for lo,hi in BANDS:
        m = (f>=lo)&(f<hi)
        e.append(10*np.log10(np.mean(P[m]) + 1e-20))
    e = np.array(e)
    ref = 10*np.log10(np.mean(P[(f>=REF[0])&(f<REF[1])]) + 1e-20)
    return e - ref

files = ["A_44100_out","B_352800_clean_out","C_352800_noisy_out",
         "D_705600_clean_out","E_705600_noisy_out"]
res = {}
for fn in files:
    sr, d = load(os.path.join(OUT, fn+".wav"))
    res[fn] = bands(to441(d, sr), 44100)

hdr = "频段(Hz)".ljust(12) + "".join(f"{fn.split('_')[0]:>8}" for fn in files)
print(hdr)
for i,(lo,hi) in enumerate(BANDS):
    row = f"{lo}-{hi}".ljust(12)
    for fn in files:
        row += f"{res[fn][i]:>8.1f}"
    print(row)

print("\n相对 A(44.1k 基准) 的偏差, 单位 dB  (负 = 该频段被压低)")
print("频段(Hz)".ljust(12) + "".join(f"{fn.split('_')[0]:>8}" for fn in files[1:]))
base = res["A_44100_out"]
for i,(lo,hi) in enumerate(BANDS):
    row = f"{lo}-{hi}".ljust(12)
    for fn in files[1:]:
        row += f"{res[fn][i]-base[i]:>8.1f}"
    print(row)
