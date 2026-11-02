// test_drive.cpp — 驱动(Drive)参数效果扫描 + drive=0 基线一致性
#include "bassmax_core.hpp"
#include <cstdio>
#include <cmath>
#include <vector>
#include <random>

static float rmsOf(const std::vector<float>& v, int a, int b) {
    double s = 0; for (int i = a; i < b; ++i) s += (double)v[i] * v[i];
    return (float)std::sqrt(s / (b - a));
}
static float peakOf(const std::vector<float>& v, int a, int b) {
    float p = 0; for (int i = a; i < b; ++i) p = std::max(p, std::fabs(v[i])); return p;
}

int main() {
    const int SR = 44100;
    const int N  = SR * 2;

    // 类音乐测试信号: 100Hz 低频 + 1kHz 中频 + 6kHz 高频 + 轻微噪声, 峰值 ~0.85
    std::vector<float> music(N), a(N), b(N);
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> nd(-1.f, 1.f);
    for (int i = 0; i < N; ++i) {
        float t = (float)i / SR;
        float hit = std::exp(-std::fmod(t, 0.5f) * 9.f);            // 每 0.5s 一次瞬态
        float s = hit * (0.55f * std::sin(2 * 3.14159265f * 60.f * t)
                       + 0.30f * std::sin(2 * 3.14159265f * 900.f * t))
                + 0.10f * std::sin(2 * 3.14159265f * 220.f * t)      // 持续铺底
                + 0.01f * nd(rng);
        music[i] = s;
    }
    // 归一化到峰值 0.85
    float pk = peakOf(music, 0, N);
    for (auto& v : music) v *= 0.85f / pk;

    printf("== 驱动扫描 (音乐型信号, 峰值输入 0.85, 44.1k) ==\n");
    printf("%5s %9s %9s %9s %9s\n", "drive", "outPeak", "outRMS", "gainRMS", "crest");
    for (int k = 0; k <= 5; ++k) {
        float dr = k * 0.2f;
        bassmax::Params p; p.drive = dr; p.bass = 4.5f; p.makeup = 1.f; p.excite = 1.f;
        bassmax::Channel ch; ch.setParams(p, (float)SR);
        std::vector<float> out(N);
        for (int i = 0; i < N; ++i) out[i] = ch.process(music[i]);
        int s0 = SR / 4, s1 = N - SR / 10;
        float oi = rmsOf(music, s0, s1), oo = rmsOf(out, s0, s1);
        printf("%5.1f %9.3f %9.4f %+8.2fdB %9.2f\n", dr, peakOf(out, s0, s1), oo,
               20 * log10(oo / (oi + 1e-9f)), peakOf(out, s0, s1) / (oo + 1e-9f));
    }

    // drive=0 基线: 1kHz 正弦增益 / 方波 H2 / 冲激延迟 (应与改动前完全一致)
    bassmax::Params p0;  // drive=0
    bassmax::Channel c1; c1.setParams(p0, (float)SR);
    std::vector<float> tone(SR), toneOut(SR);
    for (int i = 0; i < SR; ++i) tone[i] = 0.5f * std::sin(2 * 3.14159265f * 1000.f * i / SR);
    for (int i = 0; i < SR; ++i) toneOut[i] = c1.process(tone[i]);

    bassmax::Channel c2; c2.setParams(p0, (float)SR);
    std::vector<float> imp(SR, 0.f), impOut(SR);
    imp[0] = 0.5f;
    for (int i = 0; i < SR; ++i) impOut[i] = c2.process(imp[i]);
    float ipk = 0; int argmax = 0;
    for (int i = 0; i < SR; ++i) if (std::fabs(impOut[i]) > ipk) { ipk = std::fabs(impOut[i]); argmax = i; }
    float thr = ipk * 0.05f; int onset = 0;
    for (int i = 0; i < SR; ++i) if (std::fabs(impOut[i]) > thr) { onset = i; break; }

    printf("\n== drive=0 基线 ==\n");
    printf("1k tone gain=%.2f dB (基线 -1.28)\n",
           20 * log10(rmsOf(toneOut, 2000, SR - 2000) / (rmsOf(tone, 2000, SR - 2000) + 1e-9f)));
    printf("impulse peak=%.4f latency=%d (基线 0.3614 / 329)\n", ipk, onset);

    // drive=1 时 1k 正弦(0.5) 是否被推出削波
    bassmax::Params p1; p1.drive = 1.f;
    bassmax::Channel c3; c3.setParams(p1, (float)SR);
    float mx = 0; for (int i = 0; i < SR; ++i) mx = std::max(mx, std::fabs(c3.process(tone[i])));
    printf("drive=1 1k tone 输出峰值=%.4f (应 <=1.0 不硬削)\n", mx);
    return 0;
}
