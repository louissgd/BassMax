// bassmax_core.hpp  —  BassMax v4 核心 (clean-room, 框架无关)
//
// ── v4 结构 (按原版 OctiMax 的信号链推断, 已在三首配对曲上验证) ──────────────
//
//   输入 -> [静态 EQ: 6 个预设 + 低音旋钮] -> DC 阻断 -> 三段分频
//        -> 每段「测电平 -> 拉伸到目标电平 (上下都有界)」-> 相加
//        -> 末端峰值保护 -> 软保护 -> 输出
//
// 关键点: 驱动不再是一堆分段曲线, 而是**拉伸量**。
// 每段只做一件事: 把本段电平往目标电平 T 拉, 拉伸量 = S*(T - L), 并夹在 [FLOOR, C]。
//   * L 很低的段落 -> 拉伸量顶到 C (这就是"极限值", 再低也不多给了)
//   * L 接近目标的段落 -> 拉伸量自然变小
//   * L 已经超过目标的段落 -> 增益为负, 主动**压回**目标
// 三首配对曲 (Rewrite / 风雨无阻 / Bryan Adams) 拟合出的 T/S/C 见 Tuning。
//
// 驱动滑块: 0 = 完全旁路 (逐样本不变), 100 = 上面的完整拉伸量。
// 每级对应固定值, 所以驱动是线性的 dB 缩放 (dr * 拉伸量), 不做任何非线性。
//
// 6 个 EQ 预设 (原版界面): 标准乐 / 古典乐 / 俱乐部 / 流行乐 / 爵士乐 / 摇滚乐
//   (原版把爵士乐和摇滚乐两个标签写反了, 这里按实际曲线命名)
#pragma once
#include <cmath>
#include <vector>
#include <algorithm>

namespace bassmax {

struct Params {
    float drive  = 0.0f;    // 驱动 0..1 (线性电平拉伸量)
    float bass   = 0.0f;    // dB, 低音旋钮 (-12..+12, 100Hz 低架)
    int   preset = 0;       // EQ 预设 0..5
    float makeupDb = 0.f;   // 总增益 (0 = 用默认)
    float segSatLo = 0.f, segSatMid = 0.f, segSatHi = 0.f;  // 段内饱和上限 (0 = 默认)
    float trim1k = 0.f, trim9k = 0.f;   // EQ 微调 (dB): 叠加到预设的 900Hz 峰 / 9kHz 高架
};

inline float g2l(float db) { return std::exp(db * 0.1151292546f); }
inline float l2g(float lin) { return 8.685889638f * std::log((std::max)(lin, 1e-9f)); }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ---- 二阶 RBJ 双二阶 ----
class Biquad {
public:
    void set(float b0_, float b1_, float b2_, float a1_, float a2_) {
        b0=b0_; b1=b1_; b2=b2_; a1=a1_; a2=a2_; x1=x2=y1=y2=0;
    }
    void clear() { x1=x2=y1=y2=0; }
    float run(float x) {
        float y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
        x2=x1; x1=x; y2=y1; y1=y; return y;
    }
private:
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
};

// ---- 系数生成 ----
inline void make_butter_lp(float f0, float sr, float& b0, float& b1, float& b2,
                           float& a1, float& a2) {
    const float Q = 0.70710678f;
    float w0 = 2.0f*3.14159265f*f0/sr;
    float cw = std::cos(w0), sw = std::sin(w0);
    float alpha = sw/(2.0f*Q);
    float a0 = 1.0f + alpha;
    b0 = (1.0f - cw)*0.5f/a0; b1 = (1.0f - cw)/a0; b2 = b0;
    a1 = (-2.0f*cw)/a0;       a2 = (1.0f - alpha)/a0;
}
inline void make_butter_hp(float f0, float sr, float& b0, float& b1, float& b2,
                           float& a1, float& a2) {
    const float Q = 0.70710678f;
    float w0 = 2.0f*3.14159265f*f0/sr;
    float cw = std::cos(w0), sw = std::sin(w0);
    float alpha = sw/(2.0f*Q);
    float a0 = 1.0f + alpha;
    b0 = (1.0f + cw)*0.5f/a0; b1 = -(1.0f + cw)/a0; b2 = b0;
    a1 = (-2.0f*cw)/a0;       a2 = (1.0f - alpha)/a0;
}
inline void make_lr4_lp(float f0, float sr, float& b0, float& b1, float& b2,
                        float& a1, float& a2) { make_butter_lp(f0, sr, b0,b1,b2,a1,a2); }
inline void make_lr4_hp(float f0, float sr, float& b0, float& b1, float& b2,
                        float& a1, float& a2) { make_butter_hp(f0, sr, b0,b1,b2,a1,a2); }

// RBJ 峰值 (bell)
inline void make_peak(float f0, float Q, float gainDb, float sr,
                      float& b0, float& b1, float& b2, float& a1, float& a2) {
    if (std::fabs(gainDb) < 0.01f) { b0=1; b1=0; b2=0; a1=0; a2=0; return; }
    float A = std::pow(10.0f, gainDb/40.0f);
    float w0 = 2.0f*3.14159265f*f0/sr;
    float cw = std::cos(w0), sw = std::sin(w0);
    float alpha = sw/(2.0f*Q);
    float a0 = 1.0f + alpha/A;
    b0 = (1.0f + alpha*A)/a0; b1 = (-2.0f*cw)/a0; b2 = (1.0f - alpha*A)/a0;
    a1 = (-2.0f*cw)/a0;       a2 = (1.0f - alpha/A)/a0;
}

// RBJ 低架 / 高架 (S = 1)
inline void make_lowshelf(float f0, float gainDb, float sr,
                          float& b0, float& b1, float& b2, float& a1, float& a2) {
    if (std::fabs(gainDb) < 0.01f) { b0=1; b1=0; b2=0; a1=0; a2=0; return; }
    float A = std::pow(10.0f, gainDb/40.0f);
    float w0 = 2.0f*3.14159265f*f0/sr;
    float cw = std::cos(w0), sw = std::sin(w0);
    float alpha = sw*0.5f*std::sqrt(2.0f);      // S = 1 时 sin(w0)/2 * sqrt(2)
    float a0 = (A+1.0f) + (A-1.0f)*cw + 2.0f*std::sqrt(A)*alpha;
    b0 =      A*((A+1.0f) - (A-1.0f)*cw + 2.0f*std::sqrt(A)*alpha)/a0;
    b1 =  2.0f*A*((A-1.0f) - (A+1.0f)*cw)/a0;
    b2 =      A*((A+1.0f) - (A-1.0f)*cw - 2.0f*std::sqrt(A)*alpha)/a0;
    a1 = -2.0f*((A-1.0f) + (A+1.0f)*cw)/a0;
    a2 =      ((A+1.0f) + (A-1.0f)*cw - 2.0f*std::sqrt(A)*alpha)/a0;
}
inline void make_highshelf(float f0, float gainDb, float sr,
                           float& b0, float& b1, float& b2, float& a1, float& a2) {
    if (std::fabs(gainDb) < 0.01f) { b0=1; b1=0; b2=0; a1=0; a2=0; return; }
    float A = std::pow(10.0f, gainDb/40.0f);
    float w0 = 2.0f*3.14159265f*f0/sr;
    float cw = std::cos(w0), sw = std::sin(w0);
    float alpha = sw*0.5f*std::sqrt(2.0f);
    float a0 = (A+1.0f) - (A-1.0f)*cw + 2.0f*std::sqrt(A)*alpha;
    b0 =      A*((A+1.0f) + (A-1.0f)*cw + 2.0f*std::sqrt(A)*alpha)/a0;
    b1 = -2.0f*A*((A-1.0f) + (A+1.0f)*cw)/a0;
    b2 =      A*((A+1.0f) + (A-1.0f)*cw - 2.0f*std::sqrt(A)*alpha)/a0;
    a1 =  2.0f*((A-1.0f) - (A+1.0f)*cw)/a0;
    a2 =      ((A+1.0f) - (A-1.0f)*cw - 2.0f*std::sqrt(A)*alpha)/a0;
}

// ---- 6 个 EQ 预设: { 100Hz 低架, 300Hz, 900Hz, 3kHz, 9kHz 高架 } (dB) ----
// 原版界面顺序: 标准乐 / 古典乐 / 俱乐部 / 流行乐 / 爵士乐 / 摇滚乐
// (原版把爵士乐和摇滚乐标签写反了; 这里按实际曲线命名)
struct Preset { float ls, p300, p900, p3k, hs; const char* name; };
inline const Preset* presets() {
    static const Preset k[6] = {
        {  0.0f,  0.0f,  0.0f,  0.0f,  0.0f, "标准乐" },
        {  2.5f,  0.5f,  0.0f, -1.0f, -3.5f, "古典乐" },
        {  6.0f,  1.5f,  2.5f,  3.0f,  1.5f, "俱乐部" },
        { -1.0f,  5.0f,  5.5f,  1.0f, -1.0f, "流行乐" },
        {  3.0f,  1.5f, -1.0f,  2.0f,  3.5f, "摇滚乐" },
        {  7.0f, -3.5f,  1.5f,  6.5f,  8.0f, "爵士乐" },
    };
    return k;
}

// ---- 电平拉伸参数 (驱动=100) ----
// gain_dB = clamp( S*(T - L), FLOOR, C )
//   L = 本段包络电平 dBFS, T = 目标电平, C = 拉伸极限, S = 斜率(0..1)
struct BandTune {
    float S, T, C, FLOOR;
};

struct Tuning {
    // 三首配对曲联合拟合 (Rewrite / 风雨无阻 / Bryan Adams)
    static constexpr BandTune LO  = { 0.50f, -1.0f, 17.5f,  0.0f };
    static constexpr BandTune MID = { 0.80f, -12.5f, 27.0f, -1.0f };
    static constexpr BandTune HI  = { 0.50f, -12.5f, 28.0f, -1.0f };

    static constexpr float XOVER_LO = 120.0f;
    static constexpr float XOVER_HI = 2500.0f;
    static constexpr float BASS_FREQ = 100.0f;

    // 测电平:
    //   低频 -> 峰值跟随 (攻 5ms / 放 200ms)。10ms 均方根在 40-60Hz 上不足一个周期,
    //          读数逐周期起伏 -> 增益跟着抖 -> 听感「波浪起伏」。峰值跟随没有这个问题。
    //   中高 -> 10ms 能量平均 (一个窗口里有几十个周期, 天然平滑)。
    static constexpr float ENV_MS = 0.050f;
    static constexpr float LO_ATT = 0.005f, LO_REL = 0.200f;
    // 增益平滑: 电平抖动会 1:1 变成增益抖动 -> 听感「波浪起伏」。
    // 参考插件的增益 10ms 变化 p95 只有 0.7~1.2dB, 我们不加平滑是 4~12dB。
    // 收(gain 变小)用 8ms 保瞬态, 放(gain 变大)用 200ms 防抖。
    static constexpr float G_ATT = 0.030f, G_REL = 0.250f;

    // 末端: 前瞻削峰 + 软保护
    static constexpr float LIM_LOOK = 0.005f;
    static constexpr float LIM_ATT  = 0.0003f;
    static constexpr float LIM_REL  = 0.090f;
    static constexpr float CEILING  = 0.99f;
    static constexpr float SOFT_KNEE = 0.93f;
    static constexpr float MAKEUP_DB = 2.5f;
    // 段内饱和上限 (实测参考: 低 0.81 / 中 0.75 / 高 0.67)。
    // 每段自己把峰值磨圆, 末端宽带限幅就不用干活 -> 鼓点不会把中高频一起压下去。
    static constexpr float SEG_SAT_LO = 0.82f, SEG_SAT_MID = 0.75f, SEG_SAT_HI = 0.67f;
};

// ---- 一阶平滑 (能量域检测用) ----
class Smooth {
public:
    void setParams(float t, float sr) { c = 1.f - std::exp(-1.f/(t*sr)); y = 0.f; }
    void clear() { y = 0.f; }
    float run(float x) { y += (x - y) * c; return y; }
private:
    float y = 0.f, c = 1.f;
};

// ---- 包络跟随 (攻快放慢, 两支取大) ----
class Follower {
public:
    void setParams(float ta, float tr, float sr) {
        att = 1.f - std::exp(-1.f/(ta*sr));
        rel = 1.f - std::exp(-1.f/(tr*sr));
        ea = 0.f; er = 0.f;
    }
    void clear() { ea = 0.f; er = 0.f; }
    float run(float x) {
        float ax = std::fabs(x);
        ea += (ax - ea) * att;
        er += (ax - er) * rel;
        return ea > er ? ea : er;
    }
private:
    float ea = 0.f, er = 0.f, att = 0.f, rel = 0.f;
};

// ---- 前瞻峰值保护 (立体声联动, 窗口取最大) ----
class Limiter {
public:
    void setParams(float sr, float ceilDb) {
        ceil = ceilDb;
        att = 1.f - std::exp(-1.f/(Tuning::LIM_ATT*sr));
        rel = 1.f - std::exp(-1.f/(Tuning::LIM_REL*sr));
        int w = (int)(Tuning::LIM_LOOK*sr);
        if (w < 8) w = 8;
        if ((int)dl.size() != w) { dl.assign(w, 0.f); dr.assign(w, 0.f); }
        W = w; wp = 0; gain = 1.f;
    }
    void clear() {
        std::fill(dl.begin(), dl.end(), 0.f);
        std::fill(dr.begin(), dr.end(), 0.f);
        wp = 0; gain = 1.f;
    }
    float curGain() const { return gain; }
    void run(float& l, float& r) {
        if (W <= 0) return;
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

// ---- 主处理 (立体声) ----
class Core {
public:
    void reset(float sr) {
        (void)sr;
        for (int c = 0; c < 2; ++c) {
            sumA[c].clear(); sumB[c].clear();
            hiA[c].clear();  hiB[c].clear();
            loA[c].clear();  loB[c].clear();
            midA[c].clear(); midB[c].clear();
            for (int k = 0; k < 5; ++k) eq[c][k].clear();
        }
        dcx[0]=dcx[1]=dcy[0]=dcy[1]=0.f;
        fLo.clear(); smMid.clear(); smHi.clear();
        gsLo = gsMid = gsHi = 0.f;
        lim.clear();
#ifdef BASSMAX_DEBUG
        dbgG[0]=dbgG[1]=dbgG[2]=0.f;
#endif
    }

    void setParams(const Params& p, float sr) {
        // ---- 静态 EQ: 预设 + 低音旋钮 ----
        const Preset& ps = presets()[clampi(p.preset, 0, 5)];
        static const float kFreq[4] = { 300.f, 900.f, 3000.f };
        float b0,b1,b2,a1,a2;
        for (int c = 0; c < 2; ++c) {
            make_lowshelf(Tuning::BASS_FREQ, ps.ls + p.bass, sr, b0,b1,b2,a1,a2);
            eq[c][0].set(b0,b1,b2,a1,a2);
            make_peak(kFreq[0], 1.0f, ps.p300, sr, b0,b1,b2,a1,a2);
            eq[c][1].set(b0,b1,b2,a1,a2);
            make_peak(kFreq[1], 1.0f, ps.p900 + p.trim1k, sr, b0,b1,b2,a1,a2);
            eq[c][2].set(b0,b1,b2,a1,a2);
            make_peak(kFreq[2], 1.0f, ps.p3k, sr, b0,b1,b2,a1,a2);
            eq[c][3].set(b0,b1,b2,a1,a2);
            make_highshelf(9000.f, ps.hs + p.trim9k, sr, b0,b1,b2,a1,a2);
            eq[c][4].set(b0,b1,b2,a1,a2);
        }
        eqOn = (std::fabs(ps.ls) + std::fabs(ps.p300) + std::fabs(ps.p900) +
                std::fabs(ps.p3k) + std::fabs(ps.hs) + std::fabs(p.bass) +
                std::fabs(p.trim1k) + std::fabs(p.trim9k)) > 0.01f;

        // ---- 三段分频 ----
        float lb0,lb1,lb2,la1,la2, hb0,hb1,hb2,ha1,ha2;
        make_lr4_lp(Tuning::XOVER_LO, sr, lb0,lb1,lb2,la1,la2);
        make_lr4_hp(Tuning::XOVER_LO, sr, hb0,hb1,hb2,ha1,ha2);
        float sl0,sl1,sl2,sa1,sa2, sh0,sh1,sh2,sh_a1,sh_a2;
        make_lr4_lp(Tuning::XOVER_HI, sr, sl0,sl1,sl2,sa1,sa2);
        make_lr4_hp(Tuning::XOVER_HI, sr, sh0,sh1,sh2,sh_a1,sh_a2);
        for (int c = 0; c < 2; ++c) {
            sumA[c].set(sl0,sl1,sl2,sa1,sa2);  sumB[c].set(sl0,sl1,sl2,sa1,sa2);
            hiA[c].set(sh0,sh1,sh2,sh_a1,sh_a2); hiB[c].set(sh0,sh1,sh2,sh_a1,sh_a2);
            loA[c].set(lb0,lb1,lb2,la1,la2);  loB[c].set(lb0,lb1,lb2,la1,la2);
            midA[c].set(hb0,hb1,hb2,ha1,ha2); midB[c].set(hb0,hb1,hb2,ha1,ha2);
        }

        dr     = clampf(p.drive, 0.f, 1.f);
        bypass = (dr <= 1e-4f) && !eqOn;
        mkDb   = (p.makeupDb != 0.f) ? p.makeupDb : Tuning::MAKEUP_DB;
        softK  = Tuning::SOFT_KNEE;
        satLo  = p.segSatLo  > 0.f ? p.segSatLo  : Tuning::SEG_SAT_LO;
        satMid = p.segSatMid > 0.f ? p.segSatMid : Tuning::SEG_SAT_MID;
        satHi  = p.segSatHi  > 0.f ? p.segSatHi  : Tuning::SEG_SAT_HI;
        fLo.setParams(Tuning::LO_ATT, Tuning::LO_REL, sr);
        gAtt = 1.f - std::exp(-1.f/(Tuning::G_ATT*sr));
        gRel = 1.f - std::exp(-1.f/(Tuning::G_REL*sr));
        smMid.setParams(Tuning::ENV_MS, sr);
        smHi.setParams(Tuning::ENV_MS, sr);
        lim.setParams(sr, Tuning::CEILING);
        reset(sr);
    }

    // 交错立体声 [L,R,L,R,...]
    void process(float* buf, int frames, float sr) {
        (void)sr;
        if (bypass) return;                 // 驱动=0 且 EQ 全平: 一个样本都不动
        for (int i = 0; i < frames; ++i) {
            float xl = buf[2*i], xr = buf[2*i+1];
            // 静态 EQ (预设 + 低音)
            if (eqOn) {
                for (int k = 0; k < 5; ++k) { xl = eq[0][k].run(xl); xr = eq[1][k].run(xr); }
            }
            // DC 阻断
            float tl = xl - dcx[0] + 0.995f*dcy[0]; dcx[0]=xl; dcy[0]=tl; xl = tl;
            float tr = xr - dcx[1] + 0.995f*dcy[1]; dcx[1]=xr; dcy[1]=tr; xr = tr;
            // 三段分频: 先在 2500Hz 分高低, 再从低段在 120Hz 分低/中 (LR4)
            float al = sumB[0].run(sumA[0].run(xl));
            float hl = hiB[0].run(hiA[0].run(xl));
            float ll = loB[0].run(loA[0].run(al));
            float ml = midB[0].run(midA[0].run(al));
            float ar = sumB[1].run(sumA[1].run(xr));
            float hr = hiB[1].run(hiA[1].run(xr));
            float lr = loB[1].run(loA[1].run(ar));
            float mr = midB[1].run(midA[1].run(ar));
            // 测电平: 每段自己的 10ms 能量平均 (立体声合并)
            float vLo  = l2g(fLo.run(0.5f*(std::fabs(ll) + std::fabs(lr))));
            float vMid = l2g(std::sqrt(smMid.run(0.5f*(ml*ml + mr*mr))));
            float vHi  = l2g(std::sqrt(smHi.run(0.5f*(hl*hl + hr*hr))));
            // 拉伸: gain = clamp(S*(T - L), FLOOR, C), 再按驱动线性缩放
            float tLo = dr * stretch(Tuning::LO,  vLo);
            float tMid = dr * stretch(Tuning::MID, vMid);
            float tHi = dr * stretch(Tuning::HI,  vHi);
            gsLo  += (tLo  - gsLo ) * (tLo  < gsLo  ? gAtt : gRel);
            gsMid += (tMid - gsMid) * (tMid < gsMid ? gAtt : gRel);
            gsHi  += (tHi  - gsHi ) * (tHi  < gsHi  ? gAtt : gRel);
            float gLo = gsLo, gMid = gsMid, gHi = gsHi;
            float kLo = g2l(gLo + mkDb), kMid = g2l(gMid + mkDb), kHi = g2l(gHi + mkDb);
            float ol = (softsat(ll*kLo, satLo) + softsat(ml*kMid, satMid) + softsat(hl*kHi, satHi));
            float orr= (softsat(lr*kLo, satLo) + softsat(mr*kMid, satMid) + softsat(hr*kHi, satHi));
            lim.run(ol, orr);
            buf[2*i]   = softclip(ol);
            buf[2*i+1] = softclip(orr);
#ifdef BASSMAX_DEBUG
            dbgG[0] = gLo; dbgG[1] = gMid; dbgG[2] = gHi;
            dbgL[0] = vLo; dbgL[1] = vMid; dbgL[2] = vHi;
            dbgLim = lim.curGain();
#endif
        }
    }

private:
    static float softsat(float x, float d) { return d * std::tanh(x / d); }
    static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
    static float stretch(const BandTune& t, float levelDb) {
        float g = t.S * (t.T - levelDb);
        if (g > t.C)     g = t.C;
        if (g < t.FLOOR) g = t.FLOOR;
        return g;
    }
    float softclip(float x) const {
        const float k = softK;
        float ax = std::fabs(x);
        if (ax <= k) return x;
        float t = (ax - k) / (1.0f - k);
        if (t > 1.f) t = 1.f;
        float y = k + (1.0f - k) * std::tanh(t);
        return x < 0.f ? -y : y;
    }
    Biquad sumA[2], sumB[2], hiA[2], hiB[2], loA[2], loB[2], midA[2], midB[2];
    Biquad eq[2][5];
    float dcx[2] = {0,0}, dcy[2] = {0,0};
    Follower fLo;
    Smooth smMid, smHi;
    float gAtt = 0.f, gRel = 0.f, gsLo = 0.f, gsMid = 0.f, gsHi = 0.f;
    float dr = 0.f, mkDb = 0.f;
    float softK = Tuning::SOFT_KNEE;
    float satLo = 0.f, satMid = 0.f, satHi = 0.f;
    bool  eqOn = false, bypass = false;
    Limiter lim;
public:
#ifdef BASSMAX_DEBUG
    float dbgG[3] = {0,0,0}, dbgL[3] = {-90,-90,-90}, dbgLim = 1.f;
#endif
};

// ---- 单声道兼容包装 (测试/离线工具用) ----
class Channel {
public:
    void setParams(const Params& p, float sr) { c.setParams(p, sr); }
    void reset() { c.reset(0.f); }
    float process(float x) {
        float b[2] = { x, 0.f };
        c.process(b, 1, 0.f);
        return b[0];
    }
private:
    Core c;
};

} // namespace bassmax
