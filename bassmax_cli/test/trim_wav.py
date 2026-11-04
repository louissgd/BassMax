# trim_wav.py — 从 24bit WAV 截取一段, 便于做对比测试
import struct, sys

def read24(p):
    b = open(p, 'rb').read()
    off, ch, bits, doff, dlen = 12, 0, 0, -1, 0
    while off + 8 <= len(b):
        cid = b[off:off+4]
        ln = struct.unpack('<I', b[off+4:off+8])[0]
        if cid == b'fmt ':
            ch = struct.unpack('<H', b[off+10:off+12])[0]
            sr = struct.unpack('<I', b[off+12:off+16])[0]
            bits = struct.unpack('<H', b[off+22:off+24])[0]
        elif cid == b'data':
            doff, dlen = off + 8, ln
        off += 8 + ln + (ln & 1)
    assert bits == 24, f'only 24bit supported, got {bits}'
    return sr, ch, b[doff:doff+dlen]

src, dst, start_s, dur_s = sys.argv[1], sys.argv[2], float(sys.argv[3]), float(sys.argv[4])
sr, ch, data = read24(src)
fb = ch * 3
a = int(start_s * sr) * fb
b = min(len(data), a + int(dur_s * sr) * fb)
seg = data[a:b]
hdr = struct.pack('<IHHIIHH', 16, 1, ch, sr, sr * fb, fb, 24)
out = b'RIFF' + struct.pack('<I', 4 + 8 + 16 + 8 + len(seg)) + b'WAVE' + b'fmt ' + hdr + b'data' + struct.pack('<I', len(seg)) + seg
open(dst, 'wb').write(out)
print(f'{dst}: {len(seg)//fb} frames, {ch}ch, {sr}Hz')
