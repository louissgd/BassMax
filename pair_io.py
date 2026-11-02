import numpy as np, wave, os

ORIG = os.environ.get("PAIR_ORG", "Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(原始).wav")
FX   = os.environ.get("PAIR_FX",  "Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(音效).wav")

def load(path):
    with wave.open(path,'rb') as w:
        n = w.getnframes(); ch = w.getnchannels(); sw = w.getsampwidth(); sr = w.getframerate()
        raw = w.readframes(n)
    if sw == 3:
        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1,3)
        v = (b[:,0].astype(np.int32) | (b[:,1].astype(np.int32)<<8) | (b[:,2].astype(np.int32)<<16))
        v = np.where(v & 0x800000, v - 0x1000000, v).astype(np.float32)/8388608.0
    elif sw == 2:
        v = np.frombuffer(raw, dtype='<i2').astype(np.float32)/32768.0
    else:
        raise SystemExit("unsupported")
    return v.reshape(-1, ch), sr
