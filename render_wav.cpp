// render_wav.cpp — 离线渲染: 用 bassmax::Core 处理一个 WAV, 输出 24bit WAV
// 用途: 与「原插件处理后的参考 WAV」做逐段比对, 验证动态行为是否一致。
#include "bassmax_core.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <vector>
#include <string>

struct Wav { int sr=0, ch=0, bits=0; std::vector<float> data; };

static bool readWav(const char* path, Wav& w) {
    FILE* f = fopen(path, "rb");
    if (!f) { printf("open fail %s\n", path); return false; }
    char id[5] = {0};
    fread(id, 1, 4, f);
    if (memcmp(id, "RIFF", 4) != 0) { fclose(f); return false; }
    uint32_t sz; fread(&sz, 4, 1, f);
    fread(id, 1, 4, f);
    int fmt = 0, dataBits = 0;
    while (!feof(f)) {
        char ck[5] = {0};
        if (fread(ck, 1, 4, f) != 4) break;
        uint32_t len; if (fread(&len, 4, 1, f) != 1) break;
        if (!memcmp(ck, "fmt ", 4)) {
            uint16_t af, ch, bps; uint32_t sr, br; uint16_t ba;
            fread(&af, 2, 1, f); fread(&ch, 2, 1, f); fread(&sr, 4, 1, f);
            fread(&br, 4, 1, f); fread(&ba, 2, 1, f); fread(&bps, 2, 1, f);
            w.sr = (int)sr; w.ch = ch; w.bits = bps;
            if (len > 16) { char skip[64]; size_t s = len - 16; if (s > sizeof skip) s = sizeof skip; fread(skip, 1, s, f); }
        } else if (!memcmp(ck, "data", 4)) {
            dataBits = (int)len;
            long pos = ftell(f);
            int bytes = w.bits / 8;
            size_t n = len / bytes;
            std::vector<uint8_t> raw(len);
            fread(raw.data(), 1, len, f);
            w.data.resize(n);
            for (size_t i = 0; i < n; ++i) {
                const uint8_t* p = &raw[i*bytes];
                float v = 0.f;
                if (bytes == 3) {
                    int32_t t = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16));
                    if (t & 0x800000) t -= 0x1000000;
                    v = (float)t / 8388608.0f;
                } else if (bytes == 2) {
                    int16_t t; memcpy(&t, p, 2); v = (float)t / 32768.0f;
                } else if (bytes == 4) {
                    int32_t t; memcpy(&t, p, 4); v = (float)(t / 2147483648.0);
                }
                w.data[i] = v;
            }
            fseek(f, pos + (long)len, SEEK_SET);
        } else {
            fseek(f, (long)len, SEEK_CUR);
        }
        if (len & 1) fseek(f, 1, SEEK_CUR);
    }
    fclose(f);
    (void)dataBits;
    return w.sr > 0 && w.ch > 0;
}

static void writeWav(const char* path, const Wav& w) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    uint32_t n = (uint32_t)w.data.size();
    uint32_t dataBytes = n * 3;
    uint32_t riff = 36 + dataBytes;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    uint32_t fl = 16; fwrite(&fl, 4, 1, f);
    uint16_t pcm = 1, ch = (uint16_t)w.ch, ba = (uint16_t)(w.ch*3), bps = 24;
    uint32_t sr = (uint32_t)w.sr, br = sr * ba;
    fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f);
    fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bps, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&dataBytes, 4, 1, f);
    for (uint32_t i = 0; i < n; ++i) {
        float v = w.data[i];
        if (v > 1.f) v = 1.f; if (v < -1.f) v = -1.f;
        int32_t t = (int32_t)lrintf(v * 8388607.0f);
        uint8_t b[3] = { (uint8_t)(t & 0xff), (uint8_t)((t >> 8) & 0xff), (uint8_t)((t >> 16) & 0xff) };
        fwrite(b, 1, 3, f);
    }
    fclose(f);
}

int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: render_wav in.wav out.wav [drive] [bass] [seconds]\n"); return 1; }
    float drive = (argc > 3) ? (float)atof(argv[3]) : 0.85f;
    float bass  = (argc > 4) ? (float)atof(argv[4]) : 2.0f;
    double secs = (argc > 5) ? atof(argv[5]) : 0.0;

    Wav w;
    if (!readWav(argv[1], w)) { printf("read fail\n"); return 2; }
    size_t frames = w.data.size() / w.ch;
    if (secs > 0 && (size_t)(secs*w.sr) < frames) frames = (size_t)(secs*w.sr);
    printf("in: %d Hz, %d ch, %d bit, %.2f s\n", w.sr, w.ch, w.bits, frames/(double)w.sr);

    bassmax::Params p; p.drive = drive; p.bass = bass; p.preset = 0;
    {   // 离线扫描用: 环境变量覆盖
        const char* e;
        if ((e = getenv("BASSMAX_PRESET")))  p.preset = atoi(e);
        if ((e = getenv("BASSMAX_DRIVE")))   p.drive  = (float)atof(e);
        if ((e = getenv("BASSMAX_BASS")))    p.bass   = (float)atof(e);
        if ((e = getenv("BASSMAX_MAKEUP")))  p.makeupDb = (float)atof(e);
        if ((e = getenv("BASSMAX_SAT")))   { p.segSatLo = p.segSatMid = p.segSatHi = (float)atof(e); }
        if ((e = getenv("BASSMAX_SAT_LO")))  p.segSatLo  = (float)atof(e);
        if ((e = getenv("BASSMAX_SAT_HI")))  { p.segSatMid = p.segSatHi = (float)atof(e); }
        if ((e = getenv("BASSMAX_TRIM_1K"))) p.trim1k = (float)atof(e);
        if ((e = getenv("BASSMAX_TRIM_9K"))) p.trim9k = (float)atof(e);
        if ((e = getenv("BASSMAX_ENV_MS")))  p.envMs   = (float)atof(e);
        if ((e = getenv("BASSMAX_GREL")))    p.gRelMs  = (float)atof(e);
        if ((e = getenv("BASSMAX_GATT")))    p.gAttMs  = (float)atof(e);
        if ((e = getenv("BASSMAX_G2REL")))   p.g2RelMs = (float)atof(e);
        if ((e = getenv("BASSMAX_LOATT")))   p.loAttMs = (float)atof(e);
        if ((e = getenv("BASSMAX_LOREL")))   p.loRelMs = (float)atof(e);
        if ((e = getenv("BASSMAX_LIMATT")))  p.limAttMs = (float)atof(e);
        if ((e = getenv("BASSMAX_LIMREL")))  p.limRelMs = (float)atof(e);
        if ((e = getenv("BASSMAX_ENVHI")))   p.envHiMs  = (float)atof(e);
        if ((e = getenv("BASSMAX_GRELHI")))  p.gRelHiMs = (float)atof(e);
        if ((e = getenv("BASSMAX_GATHI")))   p.gAttHiMs = (float)atof(e);
        if ((e = getenv("BASSMAX_ENVMID")))  p.envMidMs  = (float)atof(e);
        if ((e = getenv("BASSMAX_GRELMID"))) p.gRelMidMs = (float)atof(e);
    }
    bassmax::Core core;
    core.setParams(p, (float)w.sr);

    std::vector<float> buf((size_t)frames * 2);
    for (size_t i = 0; i < frames; ++i) {
        buf[2*i]   = w.data[i*w.ch];
        buf[2*i+1] = (w.ch > 1) ? w.data[i*w.ch+1] : w.data[i*w.ch];
    }
    const int block = 512;
    for (size_t off = 0; off < frames; off += block) {
        int n = (int)std::min<size_t>(block, frames - off);
        core.process(buf.data() + off*2, n, (float)w.sr);
    }
    Wav o; o.sr = w.sr; o.ch = 2; o.bits = 24; o.data = buf;
    writeWav(argv[2], o);

    float peak = 0.f; double sum = 0;
    for (size_t i = 0; i < buf.size(); ++i) { float a = std::fabs(buf[i]); if (a > peak) peak = a; sum += (double)buf[i]*buf[i]; }
    printf("out: peak=%.4f rms=%.2f dBFS\n", peak, 10.0*log10(sum/buf.size() + 1e-20));
    return 0;
}
