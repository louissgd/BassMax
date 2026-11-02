# -*- coding: utf-8 -*-
# 测量台（黑盒）：加载 OctiMax 的 Winamp DSP 接口，喂已知信号，录下输出。
# 必须用工位上的 32 位 Python 运行（要加载 32 位 DLL）。
# 用法：py harness.py  （或显式 C:\Python313-32\python.exe harness.py）
import ctypes, wave, math, os, struct, random, sys

DLL_PATH = r"./单文件版/dsp_OctiMax.dll"
OUT_DIR = r"./measure_out"
SR = 44100
NCH = 2  # 立体声（它是声场增强类，必须立体声测）

# ---- Winamp DSP 接口（winampDSPHeader2）----
ModProto = ctypes.WINFUNCTYPE(ctypes.c_int,
                              ctypes.POINTER(ctypes.c_short), ctypes.c_int,
                              ctypes.c_int, ctypes.c_int, ctypes.c_int)
VoidProto = ctypes.WINFUNCTYPE(None)
HWNDProto = ctypes.WINFUNCTYPE(None, ctypes.c_void_p)

class Header2(ctypes.Structure):
    _fields_ = [
        ("version", ctypes.c_int),
        ("description", ctypes.c_char_p),
        ("hwndParent", ctypes.c_void_p),
        ("hDllInstance", ctypes.c_void_p),
        ("Config", HWNDProto),
        ("About", HWNDProto),
        ("Init", VoidProto),
        ("Quit", VoidProto),
        ("ModifySamples", ModProto),
    ]

def load_plugin(path):
    dll = ctypes.WinDLL(path)
    get = dll.winampDSPGetHeader2
    get.restype = ctypes.POINTER(Header2)
    get.argtypes = []
    hdr = get()
    # 关键：把 DLL 自己的实例句柄填进去，否则它的资源/初始化可能崩
    hdr.contents.hDllInstance = ctypes.c_void_p(dll._handle)
    try:
        hdr.contents.hwndParent = ctypes.c_void_p(ctypes.windll.user32.GetDesktopWindow())
    except Exception:
        hdr.contents.hwndParent = None
    print("plugin description:", hdr.contents.description)
    print("plugin version field:", hdr.contents.version)
    hdr.contents.Init()
    return dll, hdr

def process(hdr, samples_int16):
    n = len(samples_int16)
    buf = (ctypes.c_short * n)(*samples_int16)
    hdr.contents.ModifySamples(buf, n, 16, NCH, SR)
    return list(buf)

def to_int16(flt):
    return [max(-32768, min(32767, int(round(x * 32767)))) for x in flt]

def write_wav(path, samples_int16):
    w = wave.open(path, "w")
    w.setnchannels(NCH); w.setsampwidth(2); w.setframerate(SR)
    w.writeframes(struct.pack("<%dh" % len(samples_int16), *samples_int16))
    w.close()

def stereo(mono):
    return [v for x in mono for v in (x, x)]  # 单声道复制成 L/R

# ---------------- 测试信号 ----------------
def impulse():
    # 0.5 幅度冲激 + 1 秒静音：捕获冲激响应与拖尾（含前瞻延迟）
    return [0.5] + [0.0] * SR

def sweep():
    # 20Hz->20kHz 对数扫频 5 秒
    dur = 5.0; n = int(dur * SR); out = [0.0] * n
    f0, f1 = 20.0, 20000.0
    for i in range(n):
        t = i / SR
        f = f0 * (f1 / f0) ** (i / n)
        out[i] = 0.5 * math.sin(2 * math.pi * f * t)
    return out

def tones():
    # 10 个单频稳态音，各 0.5 秒：读各频率稳态增益（频率响应）
    freqs = [50, 100, 200, 500, 1000, 2000, 5000, 10000, 15000, 20000]
    out = []
    for f in freqs:
        for i in range(int(0.5 * SR)):
            out.append(0.5 * math.sin(2 * math.pi * f * i / SR))
    return out

def noise():
    # 伪随机噪声 1 秒：宽带增益
    return [0.3 * (random.random() * 2 - 1) for _ in range(int(1.0 * SR))]

def dc():
    # 直流 0.5，0.5 秒：测试是否隔直
    return [0.5] * int(0.5 * SR)

def square():
    # 1kHz 方波 0.5 秒：测试非线性/振铃
    return [0.5 * (1.0 if (i * 1000 // SR) % 2 == 0 else -1.0)
            for i in range(int(0.5 * SR))]

def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    dll, hdr = load_plugin(DLL_PATH)
    tests = {
        "impulse": impulse(),
        "sweep": sweep(),
        "tones": tones(),
        "noise": noise(),
        "dc": dc(),
        "square": square(),
    }
    for name, sig in tests.items():
        out = process(hdr, to_int16(stereo(sig)))
        write_wav(os.path.join(OUT_DIR, name + ".wav"), out)
        print("wrote", name, "samples:", len(out))
    hdr.contents.Quit()
    print("done. 输出在", OUT_DIR)

if __name__ == "__main__":
    main()
