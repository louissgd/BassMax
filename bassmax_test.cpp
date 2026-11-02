// bassmax_test.cpp — 验证 bassmax_core.hpp 与 Python 参考模型(bassmax_model.py)一致。
// 直接用单声道 Channel, 逐样本处理。
#include "bassmax_core.hpp"
#include <cstdio>
#include <cmath>
#include <complex>
#include <vector>

static float rms(const std::vector<float>& v, int a, int b){
    double s=0; for(int i=a;i<b;++i) s+=v[i]*v[i]; return (float)std::sqrt(s/(b-a));
}
static void fft(std::vector<std::complex<float>>& a){
    int n=(int)a.size(); if(n<=1) return;
    std::vector<std::complex<float>> ev(n/2), od(n/2);
    for(int i=0;i<n/2;++i){ ev[i]=a[2*i]; od[i]=a[2*i+1]; }
    fft(ev); fft(od);
    for(int k=0;k<n/2;++k){
        auto t=std::complex<float>(std::cos(-2*3.14159265f*k/n), std::sin(-2*3.14159265f*k/n))*od[k];
        a[k]=ev[k]+t; a[k+n/2]=ev[k]-t;
    }
}
static float h2(const std::vector<float>& sig, float f0, int sr){
    int N=16384, s=(int)sig.size()/2-N/2; if(s<0) s=0;
    std::vector<std::complex<float>> sp(N);
    for(int i=0;i<N;++i){ float w=0.5f*(1-std::cos(2*3.14159265f*i/(N-1))); sp[i]={(float)(sig[s+i]*w),0}; }
    fft(sp);
    int c=(int)(f0*N/sr+0.5f);
    float h1=0,h2v=0;
    for(int k=c-2;k<=c+2;++k) h1=std::max(h1,std::abs(sp[k]));
    int c2=(int)(2*f0*N/sr+0.5f);
    for(int k=c2-2;k<=c2+2;++k) h2v=std::max(h2v,std::abs(sp[k]));
    return h2v/(h1+1e-9f);
}

int main(){
    const int SR=44100;
    bassmax::Params p;

    bassmax::Channel chT; chT.setParams(p, (float)SR);
    std::vector<float> tone(SR/2);
    for(int i=0;i<(int)tone.size();++i) tone[i]=0.5f*std::sin(2*3.14159265f*1000.f*i/SR);
    std::vector<float> toneOut(tone.size());
    for(int i=0;i<(int)tone.size();++i) toneOut[i]=chT.process(tone[i]);

    bassmax::Channel chI; chI.setParams(p, (float)SR);
    std::vector<float> imp(SR); imp[0]=0.5f;
    std::vector<float> impOut(imp.size());
    for(int i=0;i<(int)imp.size();++i) impOut[i]=chI.process(imp[i]);

    bassmax::Channel chS; chS.setParams(p, (float)SR);
    std::vector<float> sq(SR/2);
    for(int i=0;i<(int)sq.size();++i) sq[i]=0.5f*((i*1000/SR)%2==0?1.f:-1.f);
    std::vector<float> sqOut(sq.size());
    for(int i=0;i<(int)sq.size();++i) sqOut[i]=chS.process(sq[i]);

    float pk=0; int argmax=0; for(int i=0;i<(int)impOut.size();++i){ if(std::fabs(impOut[i])>pk){pk=std::fabs(impOut[i]); argmax=i;} }
    float thr=pk*0.05; int onset=0; for(int i=0;i<(int)impOut.size();++i) if(std::fabs(impOut[i])>thr){onset=i;break;}
    printf("IMPULSE debug: out[0]=%.4f out[1]=%.4f out[330]=%.4f out[331]=%.4f argmax=%d\n",
           impOut[0], impOut[1], impOut[330], impOut[331], argmax);
    printf("IMPULSE peak=%.4f (%.2f dB) latency=%d smp (%.1f ms)\n", pk, 20*log10(pk/0.5f), onset, onset*1000.f/SR);
    printf("1k TONE inRMS=%.4f outRMS=%.4f gain=%.2f dB\n", rms(tone,2000,(int)tone.size()-2000), rms(toneOut,2000,(int)tone.size()-2000),
           20*log10(rms(toneOut,2000,(int)tone.size()-2000)/(rms(tone,2000,(int)tone.size()-2000)+1e-9f)));
    printf("SQUARE H2=%.3f\n", h2(sqOut,1000,SR));
    return 0;
}
