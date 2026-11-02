// limiter_eq.cpp — 验证新限幅器 (单调队列) 与旧实现 (线性扫描) 逐位等价
// 覆盖多种窗口长度和输入形态, 不依赖任何音频文件
#include "bassmax_core.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <cstdint>

using namespace bassmax;

// 旧实现原样复制 (线性扫描)
class OldLimiter {
public:
    void setParams(float sr, float ceilDb, float attMs, float relMs) {
        ceil = ceilDb;
        att = 1.f - std::exp(-1.f/(attMs*sr));
        rel = 1.f - std::exp(-1.f/(relMs*sr));
        int w = (int)(Tuning::LIM_LOOK*sr);
        if (w < 8) w = 8;
        dl.assign(w, 0.f); dr.assign(w, 0.f);
        W = w; wp = 0; gain = 1.f;
    }
    void run(float& l, float& r) {
        dl[wp] = l; dr[wp] = r;
        int outPos = (wp + 1) % W;
        float pk = 0.f;
        for (int i = 0; i < W; ++i) {
            float a = std::fabs(dl[i]); if (a > pk) pk = a;
            float b = std::fabs(dr[i]); if (b > pk) pk = b;
        }
        float tgt = (pk > ceil) ? (ceil / pk) : 1.f;
        float c = (tgt < gain) ? att : rel;
        gain += (tgt - gain) * c;
        if (gain > 1.f) gain = 1.f;
        l = dl[outPos] * gain;
        r = dr[outPos] * gain;
        wp = outPos;
    }
private:
    std::vector<float> dl, dr;
    int W = 0, wp = 0;
    float gain = 1.f, att = 0.f, rel = 0.f, ceil = Tuning::CEILING;
};

static uint32_t g_seed = 12345;
static float rnd() { g_seed = g_seed * 1664525u + 1013904223u; return (g_seed >> 8) / 16777216.0f; }

// 4 种信号形态: 正弦 / 白噪 / 严格递减(永远不弹队尾) / 稀疏脉冲
static float gen(int kind, long i, float sr) {
    switch (kind) {
        case 0: return 0.9f * std::sin(2*3.14159f*220.f*i/sr);
        case 1: return (rnd()*2.f - 1.f);
        case 2: return 1.0f - (float)(i % 100000) * 1e-6f;   // 缓慢递减 -> 队列塞满
        case 3: return (i % 997 == 0) ? 1.0f : 0.001f;       // 脉冲
    }
    return 0.f;
}

int main() {
    const float rates[] = { 8000.f, 44100.f, 48000.f, 96000.f, 176400.f, 352800.f, 705600.f };
    int bad = 0, cases = 0;
    for (float sr : rates) {
        for (int kind = 0; kind < 4; ++kind) {
            OldLimiter o; Limiter n;
            o.setParams(sr, Tuning::CEILING, Tuning::LIM_ATT, Tuning::LIM_REL);
            n.setParams(sr, Tuning::CEILING, Tuning::LIM_ATT, Tuning::LIM_REL);
            long total = (long)(sr * 0.5f);          // 0.5 秒, 远大于窗口
            long diff = 0;
            for (long i = 0; i < total; ++i) {
                float base = gen(kind, i, sr);
                float l1 = base, r1 = base * 0.7f;
                float l2 = l1,   r2 = r1;
                o.run(l1, r1);
                n.run(l2, r2);
                if (memcmp(&l1, &l2, 4) || memcmp(&r1, &r2, 4)) { if (diff < 3) printf("    diff @%ld: old=(%g,%g) new=(%g,%g)\n", i, l1, r1, l2, r2); ++diff; }
            }
            ++cases;
            printf("%-8.0f kind=%d  %s  (%ld 样本, 不一致 %ld)\n", sr, kind, diff ? "FAIL" : "OK", total, diff);
            if (diff) ++bad;
        }
    }
    printf("\n%d/%d 组逐位一致\n", cases - bad, cases);
    return bad ? 1 : 0;
}
