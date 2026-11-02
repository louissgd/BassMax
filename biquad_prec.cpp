// biquad_prec.cpp — 检验极低归一化频率(f0/sr)下 float 二阶节的精度
// 背景: 分频低点 120 Hz, 在 1,411,200 Hz 下 f0/sr = 8.5e-5, 系数 a1≈-2 且互相抵消,
// float32 舍入误差可能让实际响应偏离设计值。这里对同一组系数分别用 float / double 运算,
// 直接量出偏差。
#include "bassmax_core.hpp"
#include <cstdio>
#include <cmath>
#include <vector>

using namespace bassmax;

// double 版二阶节, 系数与 float 版完全相同 (set 时转换), 只把累加换成 double
struct BiquadD {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void set(double B0, double B1, double B2, double A1, double A2) {
        b0 = B0; b1 = B1; b2 = B2; a1 = A1; a2 = A2; x1 = x2 = y1 = y2 = 0;
    }
    double run(double x) {
        double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y; return y;
    }
};

// 同一组系数的 double 内部运算版 (系数也按 double 精度算), 作为"理想"参考
struct BiquadIdeal {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void design(float f0, float Q, float sr) {
        double w0 = 2.0 * 3.14159265358979 * (double)f0 / (double)sr;
        double cw = std::cos(w0), sw = std::sin(w0);
        double alpha = sw / (2.0 * (double)Q);
        double a0 = 1.0 + alpha;
        b0 = (1.0 - cw) * 0.5 / a0; b1 = (1.0 - cw) / a0; b2 = b0;
        a1 = (-2.0 * cw) / a0; a2 = (1.0 - alpha) / a0;
    }
    double run(double x) {
        double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y; return y;
    }
};

// 单级 LP 在频率 f 处的稳态增益 (dB)
template <class BQ>
static double gainOf(BQ bq, float sr, double f) {
    long total = (long)((double)sr * 0.5);
    long skip = total / 2;
    double acc = 0, ref = 0; long n = 0;
    for (long i = 0; i < total; ++i) {
        double x = std::sin(2.0 * 3.14159265358979 * f * i / (double)sr) * 0.25;
        double y = bq.run(x);
        if (i >= skip) { acc += y * y; ref += x * x; ++n; }
    }
    return 10.0 * std::log10((acc / n) / (ref / n) + 1e-30);
}

int main() {
    const float rates[] = { 44100.f, 705600.f, 1411200.f };
    const double freqs[] = { 40, 80, 120, 160, 320 };

    printf("120 Hz 低通 (LR4 的前一级, Q=0.7071) 稳态增益, 数值精度对照\n\n");
    printf("%-8s %-10s %14s %14s %14s\n", "freq", "sr", "float32运算", "double运算", "理想(double设计)");
    for (float sr : rates) {
        for (double f : freqs) {
            double b0, b1, b2, a1, a2;
            make_butter_lp(120.0f, sr, b0, b1, b2, a1, a2);   // 与 Core 完全相同的系数生成

            Biquad f32; f32.set(b0, b1, b2, a1, a2);
            BiquadD f64; f64.set(b0, b1, b2, a1, a2);          // 同系数, double 累加
            BiquadIdeal ideal; ideal.design(120.f, 0.70710678f, sr); // 系数按 double 算

            printf("%-8.0f %-10.0f %14.2f %14.2f %14.2f\n", f, sr,
                   gainOf(f32, sr, f), gainOf(f64, sr, f), gainOf(ideal, sr, f));
        }
        printf("\n");
    }
    return 0;
}
