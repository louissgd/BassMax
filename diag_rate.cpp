// diag_rate.cpp — 打印各采样率下每段的输入电平与增益, 定位高频差异来源
#define BASSMAX_DEBUG 1

#include "bassmax_core.hpp"

#include <cstdio>
#include <vector>
#include <cmath>
#include <string>

using namespace bassmax;

struct Wav { int sr=0, ch=0, bits=0; std::vector<float> d; };
static bool readWav(const char* p, Wav& w) {
    FILE* f = fopen(p, "rb"); if (!f) return false;
    char id[5]={0}; fread(id,1,4,f); if (memcmp(id,"RIFF",4)) { fclose(f); return false; }
    uint32_t sz; fread(&sz,4,1,f); fread(id,1,4,f);
    while (!feof(f)) {
        char ck[5]={0}; if (fread(ck,1,4,f)!=4) break;
        uint32_t len; if (fread(&len,4,1,f)!=1) break;
        if (!memcmp(ck,"fmt ",4)) {
            uint16_t af,ch,bps; uint32_t sr,br; uint16_t ba;
            fread(&af,2,1,f); fread(&ch,2,1,f); fread(&sr,4,1,f);
            fread(&br,4,1,f); fread(&ba,2,1,f); fread(&bps,2,1,f);
            w.sr=sr; w.ch=ch; w.bits=bps;
            if (len>16) { std::vector<char> s(len-16); fread(s.data(),1,len-16,f); }
        } else if (!memcmp(ck,"data",4)) {
            int bytes=w.bits/8; size_t n=len/bytes;
            std::vector<uint8_t> raw(len); fread(raw.data(),1,len,f);
            w.d.resize(n);
            for (size_t i=0;i<n;++i) {
                if (bytes==2) { int16_t v; memcpy(&v,&raw[i*2],2); w.d[i]=v/32768.0f; }
                else if (bytes==3) { int v=raw[i*3]|(raw[i*3+1]<<8)|(raw[i*3+2]<<16); if(v&0x800000)v-=0x1000000; w.d[i]=v/8388608.0f; }
                else if (bytes==4) { int32_t v; memcpy(&v,&raw[i*4],4); w.d[i]=(float)(v/2147483648.0); }
            }
            break;
        } else { fseek(f,len,SEEK_CUR); }
    }
    fclose(f); return true;
}

static void run(const char* path, const char* label) {
    Wav w; if (!readWav(path,w)) { printf("read fail %s\n", path); return; }
    size_t frames = w.d.size()/w.ch;
    bassmax::Params p; p.preset=4; p.drive=1.0f; p.bass=2.0f;
    Core c; c.setParams(p,(float)w.sr);
    std::vector<float> buf(frames*2);
    for (size_t i=0;i<frames;++i) {
        buf[2*i]   = w.d[i*w.ch];
        buf[2*i+1] = (w.ch>1)? w.d[i*w.ch+1] : w.d[i*w.ch];
    }
    const int blk=1024;
    double sLo=0,sMid=0,sHi=0, lLo=0,lMid=0,lHi=0, sLim=0, sMinHi=1e9;
    long cnt=0;
    for (size_t off=0; off<frames; off+=blk) {
        int n=(int)std::min<size_t>(blk, frames-off);
        c.process(buf.data()+off*2, n, (float)w.sr);
        sLo+=c.dbgG[0]; sMid+=c.dbgG[1]; sHi+=c.dbgG[2];
        lLo+=c.dbgL[0]; lMid+=c.dbgL[1]; lHi+=c.dbgL[2];
        sLim+=c.limGain();
        cnt++;
    }
    printf("%-16s sr=%-7d  增益 LO/MID/HI = %6.2f / %6.2f / %6.2f dB   电平 LO/MID/HI = %7.1f / %7.1f / %7.1f dB   限幅增益 %5.2f dB\n",
           label, w.sr, sLo/cnt, sMid/cnt, sHi/cnt, lLo/cnt, lMid/cnt, lHi/cnt,
           20*std::log10(sLim/cnt + 1e-12));
}

int main() {
    const char* base = "C:\\Users\\Administrator\\WorkBuddy\\Claw\\_probe\\";
    std::string b(base);
    run((b+"A_44100.wav").c_str(),       "44.1k");
    run((b+"B_352800_clean.wav").c_str(),"352.8k clean");
    run((b+"C_352800_noisy.wav").c_str(),"352.8k DSD噪声");
    run((b+"D_705600_clean.wav").c_str(),"705.6k clean");
    run((b+"E_705600_noisy.wav").c_str(),"705.6k DSD噪声");
    return 0;
}
