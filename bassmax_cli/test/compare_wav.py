# compare_wav.py — 逐样本比较两个 WAV (16/24/32bit 整型 + 32/64bit 浮点)
# 用法: python compare_wav.py a.wav b.wav
import struct, sys, math

def read(p):
    b = open(p, 'rb').read()
    off, ch, bits, fmt, doff, dlen = 12, 0, 0, 1, -1, 0
    while off + 8 <= len(b):
        cid = b[off:off+4]
        ln = struct.unpack('<I', b[off+4:off+8])[0]
        if cid == b'fmt ':
            fmt = struct.unpack('<H', b[off+8:off+10])[0]
            ch = struct.unpack('<H', b[off+10:off+12])[0]
            bits = struct.unpack('<H', b[off+22:off+24])[0]
            if ln >= 40 and fmt == 0xFFFE:            # EXTENSIBLE: 真实格式在子格式 GUID 前两字节
                fmt = struct.unpack('<H', b[off+32:off+34])[0]
        elif cid == b'data':
            doff, dlen = off + 8, ln
        off += 8 + ln + (ln & 1)
    if doff < 0:
        raise SystemExit('%s: no data chunk' % p)
    n = dlen // (bits // 8)
    if fmt == 3 and bits == 32:
        out = list(struct.unpack('<%df' % n, b[doff:doff + 4 * n]))
    elif fmt == 3 and bits == 64:
        out = list(struct.unpack('<%dd' % n, b[doff:doff + 8 * n]))
    elif bits == 16:
        v = struct.unpack('<%dh' % n, b[doff:doff + 2 * n])
        out = [x / 32768.0 for x in v]
    elif bits == 24:
        out = []
        for i in range(n):
            o = doff + i * 3
            x = b[o] | (b[o+1] << 8) | (b[o+2] << 16)
            if x & 0x800000:
                x -= 0x1000000
            out.append(x / 8388608.0)
    elif bits == 32:
        v = struct.unpack('<%di' % n, b[doff:doff + 4 * n])
        out = [x / 2147483648.0 for x in v]
    elif bits == 8:
        out = [(x - 128) / 128.0 for x in b[doff:doff + n]]
    else:
        raise SystemExit('%s: unsupported fmt=%d bits=%d' % (p, fmt, bits))
    return out, ch

a, cha = read(sys.argv[1])
b, chb = read(sys.argv[2])
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

db = lambda x: 20 * math.log10(x) if x > 1e-15 else -240.0
rms_d = math.sqrt(sdiff / n) if n else 0.0
rms_r = math.sqrt(sref / n) if n else 0.0
print('frames        : %d  (ch = %d / %d)' % (n, cha, chb))
print('max |diff|    : %.3e  (%.1f dBFS) at sample %d' % (maxd, db(maxd), maxi))
print('RMS diff      : %.3e  (%.1f dBFS)' % (rms_d, db(rms_d)))
print('RMS ref       : %.3e  (%.1f dBFS)' % (rms_r, db(rms_r)))
print('diff / signal : %.1f dB' % db(rms_d / rms_r if rms_r > 0 else 0.0))
if len(a) != len(b):
    print('!! length mismatch: %d vs %d' % (len(a), len(b)))
