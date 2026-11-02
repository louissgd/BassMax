#include "bassmax_core.hpp"
#include <cstdio>
#include <chrono>
#include <vector>
#include <cmath>
int main(){
  const int SR=48000, N=SR*10;   // 10 秒立体声
  std::vector<float> buf(N*2);
  for(int i=0;i<N;++i){ float t=(float)i/SR;
    buf[2*i]=0.4f*std::sin(2*3.14159265f*220.f*t);
    buf[2*i+1]=0.4f*std::sin(2*3.14159265f*330.f*t); }
  bassmax::Params p; p.drive=1.f; p.bass=5.9f;
  bassmax::Core c; c.setParams(p,(float)SR);
  auto t0=std::chrono::high_resolution_clock::now();
  c.process(buf.data(),N,(float)SR);
  auto t1=std::chrono::high_resolution_clock::now();
  double ms=std::chrono::duration<double,std::milli>(t1-t0).count();
  printf("10s 立体声 48k 处理耗时 %.1f ms  -> 实时率 %.1f%% (单核)\n", ms, ms/10000.0*100.0);
  return 0;
}
