// bench.cpp — 测量 bassmax::Core 在不同采样率下的实时倍率
// 目的: 定位 DSD (352.8k/705.6k) 下断断续续的原因
#include "bassmax_core.hpp"
#include <cstdio>
#include <chrono>
#include <vector>
#include <cmath>

using namespace bassmax;

struct Res { double sr, sec, rt; };

static Res runOne(float sr, float seconds, int preset, float drive) {
    Core c;
    Params p;
    p.preset = preset;
    p.drive  = drive;
    p.trim9k = 0.f;
    c.setParams(p, sr);

    const int block = 1024;                       // 每块帧数 (模拟 DShow 缓冲)
    std::vector<float> buf(block * 2);
    // 造一段有内容的信号, 别让包络一直为 0 (bypass 分支会掩盖开销)
    for (int i = 0; i < block; ++i) {
        float t = (float)i / sr;
        float v = 0.35f * std::sin(2 * 3.14159265f * 440.f * t)
                + 0.20f * std::sin(2 * 3.14159265f * 60.f  * t);
        buf[2*i] = buf[2*i+1] = v;
    }

    long total = (long)(sr * seconds);
    long done = 0;
    auto t0 = std::chrono::high_resolution_clock::now();
    while (done < total) {
        c.process(buf.data(), block, sr);
        done += block;
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double wall = std::chrono::duration<double>(t1 - t0).count();
    Res r;
    r.sr  = sr;
    r.sec = (double)done / sr;
    r.rt  = r.sec / wall;          // >1 = 能实时, <1 = 必然卡
    return r;
}

int main(int argc, char** argv) {
    int   preset = argc > 1 ? atoi(argv[1]) : 4;   // 4 = 爵士乐
    float drive  = argc > 2 ? (float)atof(argv[2]) : 1.0f;
    printf("preset=%d drive=%.2f  (限幅器窗口 W = 5ms*sr)\n\n", preset, drive);
    printf("%-10s %-10s %-12s %s\n", "采样率", "限幅窗口W", "实时倍率", "判定");

    const float rates[] = { 44100.f, 48000.f, 96000.f, 176400.f, 352800.f, 705600.f, 1411200.f };
    for (float sr : rates) {
        Res r = runOne(sr, 3.0f, preset, drive);
        int W = (int)(Tuning::LIM_LOOK * sr);
        const char* verdict = r.rt >= 4.0 ? "充裕" : (r.rt >= 1.5 ? "勉强" : (r.rt >= 1.0 ? "临界" : "算不动 -> 断音"));
        printf("%-10.0f %-10d %-12.2f %s\n", sr, W, r.rt, verdict);
    }
    return 0;
}
