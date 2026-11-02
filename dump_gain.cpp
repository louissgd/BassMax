// dump_gain.cpp — 导出内部增益曲线, 用于定位瞬态处的动作
#define BASSMAX_DEBUG
#include "bassmax_core.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <vector>

struct Wav { int sr=0, ch=0, bits=0; std::vector<float> data; };

static bool readWav(const char* path, Wav& w) {
    FILE* f = fopen(path, "rb");
    if (!f) { printf("open fail\n"); return false; }
    char id[5] = {0};
    fread(id, 1, 4, f);
    uint32_t sz; fread(&sz, 4, 1, f);
    fread(id, 1, 4, f);
    while (!feof(f)) {
        char ck[5] = {0};
        if (fread(ck, 1, 4, f) != 4) break;
        uint32_t len; if (fread(&len, 4, 1, f) != 1) break;
        if (!memcmp(ck, "fmt ", 4)) {
            uint16_t af, ch, bps; uint32_t sr, br; uint16_t ba;
            fread(&af, 2, 1, f); fread(&ch, 2, 1, f); fread(&sr, 4, 1, f);
            fread(&br, 4, 1, f); fread(&ba, 2, 1, f); fread(&bps, 2, 1, f);
            w.sr=(int)sr; w.ch=ch; w.bits=bps;
            if (len > 16) { char sk[64]; size_t s = len-16; if (s>sizeof sk) s=sizeof sk; fread(sk,1,s,f); }
        } else if (!memcmp(ck, "data", 4)) {
            int bytes = w.bits/8;
            size_t n = len/bytes;
            std::vector<uint8_t> raw(len);
            fread(raw.data(),1,len,f);
            w.data.resize(n);
            for (size_t i=0;i<n;++i){
                const uint8_t* p=&raw[i*bytes]; float v=0.f;
                if (bytes==3){ int32_t t=(int32_t)((uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16));
                               if (t&0x800000) t-=0x1000000; v=(float)t/8388608.0f; }
                else if (bytes==2){ int16_t t; memcpy(&t,p,2); v=(float)t/32768.0f; }
                else if (bytes==4){ int32_t t; memcpy(&t,p,4); v=(float)(t/2147483648.0); }
                w.data[i]=v;
            }
        } else fseek(f,(long)len,SEEK_CUR);
        if (len & 1) fseek(f,1,SEEK_CUR);
    }
    fclose(f);
    return w.sr>0;
}

int main(int argc, char** argv) {
    if (argc < 4) { printf("usage: dump_gain in.wav out.csv drive [bass] [t0] [t1]\n"); return 1; }
    float drive = (float)atof(argv[3]);
    float bass  = (argc>4)?(float)atof(argv[4]):4.5f;
    double t0 = (argc>5)?atof(argv[5]):0.0, t1=(argc>6)?atof(argv[6]):1e9;
    Wav w;
    if (!readWav(argv[1], w)) return 2;
    size_t frames = w.data.size()/w.ch;
    bassmax::Params p; p.drive=drive; p.bass=bass;
    {
        const char* e;
        if ((e=getenv("BASSMAX_PRESET")))  p.preset=(int)atoi(e);
        if ((e=getenv("BASSMAX_TRIM_9K"))) p.trim9k=(float)atof(e);
        if ((e=getenv("BASSMAX_TRIM_1K"))) p.trim1k=(float)atof(e);
        if ((e=getenv("BASSMAX_GREL")))    p.gRelMs=(float)atof(e);
        if ((e=getenv("BASSMAX_GATT")))    p.gAttMs=(float)atof(e);
    }
    bassmax::Core core; core.setParams(p,(float)w.sr);
    std::vector<float> buf(frames*2);
    for (size_t i=0;i<frames;++i){ buf[2*i]=w.data[i*w.ch]; buf[2*i+1]=(w.ch>1)?w.data[i*w.ch+1]:w.data[i*w.ch]; }
    FILE* o=fopen(argv[2],"w");
    fprintf(o,"t,lv_lo,lv_mid,lv_hi,g_lo,g_mid,g_hi,lim_db,out\n");
    size_t start=(size_t)(t0*w.sr), end=(size_t)(t1*w.sr); if(end>frames) end=frames;
    // 先跑前面部分保持状态
    const int block=1;
    for (size_t off=0; off<end; off+=block){
        (void)0;
        core.process(buf.data()+off*2, block, (float)w.sr);
        if (off>=start && (off%480)==0)
            fprintf(o,"%.6f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.4f\n", off/(double)w.sr,
                    core.dbgL[0], core.dbgL[1], core.dbgL[2],
                    core.dbgG[0], core.dbgG[1], core.dbgG[2],
                    20*log10(core.dbgLim+1e-9), buf[2*off]);
    }
    fclose(o);
    printf("wrote %s\n", argv[2]);
    return 0;
}
