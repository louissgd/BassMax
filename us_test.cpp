// us_test.cpp — 验证超声保护在高采样率(含 DSD256 1.4112M)下的幅频响应与数值稳定性
// (a) 用与 Core 完全相同的 4 级二阶链, 测各频点稳态增益
// (b) 让 Core 在 1.4112M 下跑 10 秒含超声噪声的信号, 检查 NaN/Inf/失控
#include "bassmax_core.hpp"
#include <cstdio>
#include <cmath>
#include <vector>

using namespace bassmax;

static const float kUsQ[4] = { 0.50980f, 0.60135f, 0.89998f, 2.56292f };

// 稳态增益: 喂单频正弦, 取后段 RMS / 输入 RMS
static double gainAt(float sr, double f) {
    Biquad b[4];
    double g0, g1, g2, ga1, ga2;
    for (int k = 0; k < 4; ++k) {
        make_lp_q(Tuning::US_FREQ, kUsQ[k], sr, g0, g1, g2, ga1, ga2);
        b[k].set(g0, g1, g2, ga1, ga2);
    }
    const double dt = 1.0 / sr;
    long total = (long)(sr * 0.5);          // 0.5 秒, 足够稳定
    long skip  = total / 2;
    double acc = 0.0, ref = 0.0;
    long n = 0;
    for (long i = 0; i < total; ++i) {
        float x = (float)std::sin(2.0 * 3.14159265358979 * f * i * dt) * 0.25f;
        float y = x;
        for (int k = 0; k < 4; ++k) y = b[k].run(y);
        if (i >= skip) { acc += (double)y * y; ref += (double)x * x; ++n; }
    }
    if (ref <= 0.0 || n == 0) return -999.0;
    return 10.0 * std::log10((acc / n) / (ref / n) + 1e-30);
}

int main() {
    const float rates[] = { 352800.f, 705600.f, 1411200.f };
    const double freqs[] = { 1000, 5000, 10000, 18000, 20000, 22000, 25000, 30000, 40000, 60000, 100000 };

    printf("=== 超声保护链 幅频响应 (dB, 0 = 不衰减) ===\n\n");
    printf("%-10s", "freq\\sr");
    for (float sr : rates) printf("%12.0f", sr);
    printf("\n");
    for (double f : freqs) {
        printf("%-10.0f", f);
        for (float sr : rates) {
            if (f >= sr / 2) { printf("%12s", "  --"); continue; }
            printf("%12.2f", gainAt(sr, f));
        }
        printf("\n");
    }

    // ---- 数值稳定性: 1.4112M 下跑 10 秒 ----
    printf("\n=== 稳定性 (Core @ 1,411,200 Hz, 10 秒) ===\n");
    for (float sr : rates) {
        Core c;
        Params p;
        p.preset = 4; p.drive = 1.0f; p.trim9k = 0.f;
        c.setParams(p, sr);
        c.reset(sr);

        const int block = 1024;
        std::vector<float> buf(block * 2);
        long total = (long)(sr * 10.0), done = 0;
        double peak = 0.0, acc = 0.0;
        long bad = 0, n = 0;
        unsigned s = 12345;
        for (long i = 0; done < total; ++i) {
            for (int k = 0; k < block; ++k) {
                long idx = done + k;
                float t = (float)((double)idx / sr);
                // 音乐 + 满刻度级超声噪声 (模拟 DSD256 解码后的形态)
                float music = 0.30f * std::sin(2 * 3.14159265f * 440.f * t)
                            + 0.18f * std::sin(2 * 3.14159265f * 63.f * t);
                s = s * 1664525u + 1013904223u;
                float rnd = (float)((s >> 8) & 0xFFFF) / 32768.0f - 1.0f;
                float uhf = 0.55f * rnd;           // 宽带, 接近 DSD 超声段能量
                buf[2*k] = buf[2*k+1] = music + uhf;
            }
            c.process(buf.data(), block, sr);
            for (int k = 0; k < block; ++k) {
                float l = buf[2*k], r = buf[2*k+1];
                if (!(l == l) || !(r == r) || std::fabs(l) > 100.f || std::fabs(r) > 100.f) ++bad;
                double a = std::fabs((double)l);
                if (a > peak) peak = a;
                acc += a; ++n;
            }
            done += block;
        }
        printf("  sr=%-9.0f  peak=%.4f  mean|y|=%.4f  NaN/失控样本=%ld\n",
               sr, peak, n ? acc / n : 0.0, bad);
    }
    return 0;
}
