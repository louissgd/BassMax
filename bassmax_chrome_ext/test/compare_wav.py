# compare_wav.py — 逐样本比较两个 24bit WAV
import struct, sys, math

def read(p):
    b = open(p, 'rb').read()
    off, ch, bits, doff, dlen = 12, 0, 0, -1, 0
    while off + 8 <= len(b):
        cid = b[off:off+4]
        ln = struct.unpack('<I', b[off+4:off+8])[0]
        if cid == b'fmt ':
            ch, bits = struct.unpack('<H', b[off+10:off+12])[0], struct.unpack('<H', b[off+22:off+24])[0]
        elif cid == b'data':
            doff, dlen = off + 8, ln
        off += 8 + ln + (ln & 1)
    n = dlen // (bits // 8)
    out = []
    for i in range(n):
        o = doff + i * 3
        v = b[o] | (b[o+1] << 8) | (b[o+2] << 16)
        if v & 0x800000:
            v -= 0x1000000
        out.append(v / 8388608.0)
    return out

a = read(sys.argv[1])
b = read(sys.argv[2])
n = min(len(a), len(b))
maxd = 0.0
maxi = -1
sdiff = 0.0
sref = 0.0
for i in range(n):
    d = abs(a[i] - b[i])
    sdiff += d * d
    sref += a[i] * a[i]
    if d > maxd:
        maxd = d
        maxi = i

rms_d = math.sqrt(sdiff / n)
rms_a = math.sqrt(sref / n)
print(f'frames        : {n}  (ch = {len(a)//n if n else 0})')
print(f'max |diff|    : {maxd:.3e}  ({20*math.log10(maxd+1e-12):.1f} dBFS) at sample {maxi}')
print(f'RMS diff      : {rms_d:.3e}  ({20*math.log10(rms_d+1e-12):.1f} dBFS)')
print(f'RMS ref       : {rms_a:.3e}  ({20*math.log10(rms_a+1e-12):.1f} dBFS)')
print(f'diff / signal : {20*math.log10((rms_d+1e-12)/(rms_a+1e-12)):.1f} dB')
