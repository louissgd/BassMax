# wcut.py — 从整曲 wav 切片段, 统一输出 24bit 立体声
import sys, numpy as np, wave
from pair_io import load


def wr(path, v, sr):
    v = np.clip(v, -1, 1)
    i = np.round(v * 8388607).astype(np.int32).reshape(-1)
    b = np.zeros((len(i), 3), np.uint8)
    b[:, 0] = i & 255; b[:, 1] = (i >> 8) & 255; b[:, 2] = (i >> 16) & 255
    with wave.open(path, 'wb') as w:
        w.setnchannels(2); w.setsampwidth(3); w.setframerate(sr)
        w.writeframes(b.tobytes())


def cut(src, dst, a, b):
    v, sr = load(src)
    wr(dst, v[int(a*sr):int(b*sr)], sr)
    return dst
