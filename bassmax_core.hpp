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
//   preset4 = 爵士乐 = 实测与「摇滚+50%低音+100%驱动」参考音最接近的曲线 (谱差 0.74dB)
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
    // 时间常数微调 (单位秒, 0 = 用 Tuning 默认值); 主要给离线扫参用
    float envMs = 0.f, gAttMs = 0.f, gRelMs = 0.f, g2RelMs = 0.f;
    float loAttMs = 0.f, loRelMs = 0.f;
    float limAttMs = 0.f, limRelMs = 0.f;
    float envHiMs = 0.f, gAttHiMs = 0.f, gRelHiMs = 0.f;   // HI 段专用 (0 = 同全段)
    float envMidMs = 0.f, gAttMidMs = 0.f, gRelMidMs = 0.f; // MID 段专用 (0 = 同全段)
};

inline float g2l(float db) { return std::exp(db * 0.1151292546f); }
inline float l2g(float lin) { return 8.685889638f * std::log((std::max)(lin, 1e-9f)); }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ---- 二阶 RBJ 双二阶 ----
// 系数与状态一律用 double。原因: 分频低点 120 Hz 在 1,411,200 Hz 下 f0/sr 只有 8.5e-5,
// 用 float 算 cos(w0) 时 1-cw 只剩 1~2 位有效数字, 系数本身就已经错了 (实测 40 Hz 偏 1.7 dB)。
// 这是定点/浮点小频率的经典陷阱 —— 精确计算必须留够位宽, 而不是靠公式。
class Biquad {
public:
    void set(double b0_, double b1_, double b2_, double a1_, double a2_) {
        b0=b0_; b1=b1_; b2=b2_; a1=a1_; a2=a2_; x1=x2=y1=y2=0;
    }
    void clear() { x1=x2=y1=y2=0; }
    float run(float x) {
        double y = b0*(double)x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
        x2=x1; x1=(double)x; y2=y1; y1=y; return (float)y;
    }
private:
    double b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
};

// ---- 系数生成 ----
// 全部用 double 计算: 极低归一化频率下 (DSD 速率) float 的 cos(w0) 精度不足以描述滤波器。
inline void make_butter_lp(float f0, float sr, double& b0, double& b1, double& b2,
                           double& a1, double& a2) {
    const double Q = 0.70710678118654752;
    double w0 = 2.0*3.14159265358979324*(double)f0/(double)sr;
    double cw = std::cos(w0), sw = std::sin(w0);
    double alpha = sw/(2.0*Q);
    double a0 = 1.0 + alpha;
    b0 = (1.0 - cw)*0.5/a0; b1 = (1.0 - cw)/a0; b2 = b0;
    a1 = (-2.0*cw)/a0;      a2 = (1.0 - alpha)/a0;
}
inline void make_butter_hp(float f0, float sr, double& b0, double& b1, double& b2,
                           double& a1, double& a2) {
    const double Q = 0.70710678118654752;
    double w0 = 2.0*3.14159265358979324*(double)f0/(double)sr;
    double cw = std::cos(w0), sw = std::sin(w0);
    double alpha = sw/(2.0*Q);
    double a0 = 1.0 + alpha;
    b0 = (1.0 + cw)*0.5/a0; b1 = -(1.0 + cw)/a0; b2 = b0;
    a1 = (-2.0*cw)/a0;      a2 = (1.0 - alpha)/a0;
}
inline void make_lr4_lp(float f0, float sr, double& b0, double& b1, double& b2,
                        double& a1, double& a2) { make_butter_lp(f0, sr, b0,b1,b2,a1,a2); }
inline void make_lr4_hp(float f0, float sr, double& b0, double& b1, double& b2,
                        double& a1, double& a2) { make_butter_hp(f0, sr, b0,b1,b2,a1,a2); }

// 指定 Q 的二阶低通。用于多级级联凑高阶 (超声保护用)。
inline void make_lp_q(float f0, float Q, float sr, double& b0, double& b1, double& b2,
                      double& a1, double& a2) {
    double w0 = 2.0*3.14159265358979324*(double)f0/(double)sr;
    double cw = std::cos(w0), sw = std::sin(w0);
    double alpha = sw/(2.0*(double)Q);
    double a0 = 1.0 + alpha;
    b0 = (1.0 - cw)*0.5/a0; b1 = (1.0 - cw)/a0; b2 = b0;
    a1 = (-2.0*cw)/a0;      a2 = (1.0 - alpha)/a0;
}

// RBJ 峰值 (bell)
inline void make_peak(float f0, float Q, float gainDb, float sr,
                      double& b0, double& b1, double& b2, double& a1, double& a2) {
    if (std::fabs(gainDb) < 0.01f) { b0=1; b1=0; b2=0; a1=0; a2=0; return; }
    double A = std::pow(10.0, (double)gainDb/40.0);
    double w0 = 2.0*3.14159265358979324*(double)f0/(double)sr;
    double cw = std::cos(w0), sw = std::sin(w0);
    double alpha = sw/(2.0*(double)Q);
    double a0 = 1.0 + alpha/A;
    b0 = (1.0 + alpha*A)/a0; b1 = (-2.0*cw)/a0; b2 = (1.0 - alpha*A)/a0;
    a1 = (-2.0*cw)/a0;       a2 = (1.0 - alpha/A)/a0;
}

// RBJ 低架 / 高架 (S = 1)
inline void make_lowshelf(float f0, float gainDb, float sr,
                          double& b0, double& b1, double& b2, double& a1, double& a2) {
    if (std::fabs(gainDb) < 0.01f) { b0=1; b1=0; b2=0; a1=0; a2=0; return; }
    double A = std::pow(10.0, (double)gainDb/40.0);
    double w0 = 2.0*3.14159265358979324*(double)f0/(double)sr;
    double cw = std::cos(w0), sw = std::sin(w0);
    double alpha = sw*0.5*1.4142135623730951;      // S = 1 时 sin(w0)/2 * sqrt(2)
    double a0 = (A+1.0) + (A-1.0)*cw + 2.0*std::sqrt(A)*alpha;
    b0 =      A*((A+1.0) - (A-1.0)*cw + 2.0*std::sqrt(A)*alpha)/a0;
    b1 =  2.0*A*((A-1.0) - (A+1.0)*cw)/a0;
    b2 =      A*((A+1.0) - (A-1.0)*cw - 2.0*std::sqrt(A)*alpha)/a0;
    a1 = -2.0*((A-1.0) + (A+1.0)*cw)/a0;
    a2 =      ((A+1.0) + (A-1.0)*cw - 2.0*std::sqrt(A)*alpha)/a0;
}
inline void make_highshelf(float f0, float gainDb, float sr,
                           double& b0, double& b1, double& b2, double& a1, double& a2) {
    if (std::fabs(gainDb) < 0.01f) { b0=1; b1=0; b2=0; a1=0; a2=0; return; }
    double A = std::pow(10.0, (double)gainDb/40.0);
    double w0 = 2.0*3.14159265358979324*(double)f0/(double)sr;
    double cw = std::cos(w0), sw = std::sin(w0);
    double alpha = sw*0.5*1.4142135623730951;
    double a0 = (A+1.0) - (A-1.0)*cw + 2.0*std::sqrt(A)*alpha;
    b0 =      A*((A+1.0) + (A-1.0)*cw + 2.0*std::sqrt(A)*alpha)/a0;
    b1 = -2.0*A*((A-1.0) + (A+1.0)*cw)/a0;
    b2 =      A*((A+1.0) + (A-1.0)*cw - 2.0*std::sqrt(A)*alpha)/a0;
    a1 =  2.0*((A-1.0) - (A+1.0)*cw)/a0;
    a2 =      ((A+1.0) - (A-1.0)*cw - 2.0*std::sqrt(A)*alpha)/a0;
}

// ---- 6 个 EQ 预设: { 100Hz 低架, 300Hz, 900Hz, 3kHz, 9kHz 高架 } (dB) ----
// 原版界面顺序: 标准乐 / 古典乐 / 俱乐部 / 流行乐 / 爵士乐 / 摇滚乐
// preset4(爵士乐) 已用三首参考音逐条实测确认, 不要随意对调
struct Preset { float ls, p300, p900, p3k, hs; const char* name; };
inline const Preset* presets() {
    static const Preset k[6] = {
        {  0.0f,  0.0f,  0.0f,  0.0f,  0.0f, "标准乐" },
        {  2.5f,  0.5f,  0.0f, -1.0f, -3.5f, "古典乐" },
        {  6.0f,  1.5f,  2.5f,  3.0f,  1.5f, "俱乐部" },
        { -1.0f,  5.0f,  5.5f,  1.0f, -1.0f, "流行乐" },
        {  3.0f,  1.5f, -1.0f,  2.0f,  3.5f, "爵士乐" },
        {  7.0f, -3.5f,  1.5f,  6.5f,  8.0f, "摇滚乐" },
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
    // 收(gain 变小)用 30ms 保瞬态, 放(gain 变大)用 400ms 防抖。
    // 2026-11-02 用户试听: 250ms 在「风雨无阻」吉他连续分解和弦下仍有起伏,
    //   400ms 明显更稳且不发闷, 定为默认。
    static constexpr float G_ATT = 0.030f, G_REL = 0.400f;
    // HI 段单独用更快的检测与释放。高频瞬态短、混响尾巴长, 用 400ms 释放会把尾巴整体抬起来,
    // 听感就是「回音变重」。实测电子乐(Aqua): 高频 80ms 衰减 6.4dB(慢释放) vs 参考 10.5dB;
    // 改成 10ms 窗 + 150ms 释放后为 10.6dB, 与参考一致。低频/中频仍用 400ms, 吉他弦音的稳定不受影响。
    static constexpr float ENV_HI = 0.010f, G_ATT_HI = 0.020f, G_REL_HI = 0.150f;

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

    // ---- 超声保护 (2026-11-03, 为 DSD 而加) ----
    // DSD 解码成 PCM 后是 352.8k (DSD64) / 705.6k (DSD128), 而 DSD 的噪声整形
    // 把量化噪声全部推到 20kHz 以上, 在 40-100kHz 一带接近满刻度。
    // 这套信号链的饱和器是 tanh (非线性): 满刻度的超声噪声进去, 会和音乐互调出
    // 落在可听频段的产物; 检测器也会读到这个能量, 把高音段整段压下去。
    // 实听表现为"发闷 + 发噪", 比 128k MP3 还差。
    // 处理办法: 在输入端加 8 阶巴特沃斯低通, 把 20kHz 以上切掉。
    // 只在 sr >= 176.4k (DSD 派生采样率) 时启用, 44.1k/48k/88.2k/96k 完全不受影响。
    static constexpr float US_FREQ   = 20000.0f;
    static constexpr float US_MIN_SR = 176400.0f;
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
//
// 2026-11-03 性能修复 (DSD 断音):
//   旧实现每个样本线性扫一遍整个窗口求最大值, 开销 O(W), 而 W = LIM_LOOK * sr
//   随采样率线性增长 -> 每秒开销约 O(sr^2)。
//     44.1k : W=220   -> 实时倍率 51x    (没问题, 所以一直没暴露)
//     96k   : W=480   -> 11.8x
//     352.8k: W=1764  -> 0.96x  <-- DSD64 解码后的实际采样率, 算不动 -> 断断续续
//     705.6k: W=3528  -> 0.24x
//   改成单调双端队列 (滑动窗口最大值的标准 O(1) 摊还解法) 后, 每样本只做几次比较。
//   窗口定义、比较顺序、float 取值与旧实现完全一致, 所以输出逐位相同 —— 音色不变,
//   只是不再随采样率平方级变慢。
class Limiter {
public:
    void setParams(float sr, float ceilDb, float attMs = Tuning::LIM_ATT,
                   float relMs = Tuning::LIM_REL) {
        ceil = ceilDb;
        att = 1.f - std::exp(-1.f/(attMs*sr));
        rel = 1.f - std::exp(-1.f/(relMs*sr));
        int w = (int)(Tuning::LIM_LOOK*sr);
        if (w < 8) w = 8;
        if ((int)dl.size() != w) { dl.assign(w, 0.f); dr.assign(w, 0.f); }
        W = w;
        // 单调队列容量: 极端情况 (输入严格递减) 窗口内每个样本都在队列里
        cap = W + 2;
        dqv.assign((size_t)cap, 0.f);
        dqi.assign((size_t)cap, 0);
        wp = 0; gain = 1.f;
        dqn = 0; qh = 0; qt = 0;
    }
    void clear() {
        std::fill(dl.begin(), dl.end(), 0.f);
        std::fill(dr.begin(), dr.end(), 0.f);
        wp = 0; gain = 1.f;
        dqn = 0; qh = 0; qt = 0;
    }
    float curGain() const { return gain; }
    void run(float& l, float& r) {
        if (W <= 0) return;
        dl[wp] = l; dr[wp] = r;
        int outPos = (wp + 1) % W;

        // 本样本对窗口峰值的贡献 = max(|l|, |r|)
        float al = std::fabs(l), ar = std::fabs(r);
        float v  = al > ar ? al : ar;
        const long long idx = ++dqn;

        // 单调递减队列: 从尾部弹掉所有不大于 v 的元素
        while (qt > qh) {
            int bi = (int)((qt - 1) % (long long)cap);
            if (dqv[bi] > v) break;
            --qt;
        }
        if (qt == qh) { qh = 0; qt = 0; }         // 队列空 -> 归零, 免得计数无限增长
        int ti = (int)(qt % (long long)cap);
        dqv[ti] = v; dqi[ti] = idx; ++qt;

        // 头部弹出滑出窗口 [idx-W+1, idx] 的元素
        const long long oldest = idx - W;
        while (qh < qt && dqi[(int)(qh % (long long)cap)] <= oldest) {
            ++qh;
            if (qh >= qt) { qh = 0; qt = 0; break; }
        }
        float pk = dqv[(int)(qh % (long long)cap)];

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
    std::vector<float> dqv;      // 单调递减队列: 值
    std::vector<long long> dqi;  // 单调递减队列: 绝对样本序号
    int W = 0, wp = 0, cap = 0;
    long long dqn = 0, qh = 0, qt = 0;
    float gain = 1.f, att = 0.f, rel = 0.f, ceil = Tuning::CEILING;
};

// ---- 主处理 (立体声) ----
class Core {
public:
    void reset(float sr) {
        // 直流阻断系数必须随采样率换算, 见 setParams 里的说明。
        // sr<=0 表示"只清状态, 不改参数"(Channel 包装会这样调)。
        if (sr > 0.f) dcR = std::pow(0.995, 44100.0 / (double)sr);
        for (int c = 0; c < 2; ++c) {
            sumA[c].clear(); sumB[c].clear();
            hiA[c].clear();  hiB[c].clear();
            loA[c].clear();  loB[c].clear();
            midA[c].clear(); midB[c].clear();
            for (int k = 0; k < 5; ++k) eq[c][k].clear();
            for (int k = 0; k < 4; ++k) us[c][k].clear();
            peq[c].clear();
        }
        dcx[0]=dcx[1]=dcy[0]=dcy[1]=0.f;
        fLo.clear(); smMid.clear(); smHi.clear();
        gsLo = gsMid = gsHi = 0.f;
        g2Lo = g2Mid = g2Hi = 0.f;
        lim.clear();
#ifdef BASSMAX_DEBUG
        dbgG[0]=dbgG[1]=dbgG[2]=0.f;
#endif
    }

    void setParams(const Params& p, float sr) {
        // ---- 静态 EQ: 预设 + 低音旋钮 ----
        const Preset& ps = presets()[clampi(p.preset, 0, 5)];
        static const float kFreq[4] = { 300.f, 900.f, 3000.f };
        double b0,b1,b2,a1,a2;
        for (int c = 0; c < 2; ++c) {
            make_lowshelf(Tuning::BASS_FREQ, ps.ls + p.bass, sr, b0,b1,b2,a1,a2);
            eq[c][0].set(b0,b1,b2,a1,a2);
            make_peak(kFreq[0], 1.0f, ps.p300, sr, b0,b1,b2,a1,a2);
            eq[c][1].set(b0,b1,b2,a1,a2);
            make_peak(kFreq[1], 1.0f, ps.p900 + p.trim1k, sr, b0,b1,b2,a1,a2);
            eq[c][2].set(b0,b1,b2,a1,a2);
            make_peak(kFreq[2], 1.0f, ps.p3k, sr, b0,b1,b2,a1,a2);
            eq[c][3].set(b0,b1,b2,a1,a2);
            make_highshelf(9000.f, ps.hs, sr, b0,b1,b2,a1,a2);
            eq[c][4].set(b0,b1,b2,a1,a2);
            // 高音旋钮: 后置 9kHz 高架, 不进电平检测/压缩 -> 纯音色控制, 不改变动态
            make_highshelf(9000.f, p.trim9k, sr, b0,b1,b2,a1,a2);
            peq[c].set(b0,b1,b2,a1,a2);
        }
        postOn = (std::fabs(p.trim9k) > 0.01f);
        eqOn = (std::fabs(ps.ls) + std::fabs(ps.p300) + std::fabs(ps.p900) +
                std::fabs(ps.p3k) + std::fabs(ps.hs) + std::fabs(p.bass) +
                std::fabs(p.trim1k)) > 0.01f;

        // ---- 三段分频 ----
        double lb0,lb1,lb2,la1,la2, hb0,hb1,hb2,ha1,ha2;
        make_lr4_lp(Tuning::XOVER_LO, sr, lb0,lb1,lb2,la1,la2);
        make_lr4_hp(Tuning::XOVER_LO, sr, hb0,hb1,hb2,ha1,ha2);
        double sl0,sl1,sl2,sa1,sa2, sh0,sh1,sh2,sh_a1,sh_a2;
        make_lr4_lp(Tuning::XOVER_HI, sr, sl0,sl1,sl2,sa1,sa2);
        make_lr4_hp(Tuning::XOVER_HI, sr, sh0,sh1,sh2,sh_a1,sh_a2);
        for (int c = 0; c < 2; ++c) {
            sumA[c].set(sl0,sl1,sl2,sa1,sa2);  sumB[c].set(sl0,sl1,sl2,sa1,sa2);
            hiA[c].set(sh0,sh1,sh2,sh_a1,sh_a2); hiB[c].set(sh0,sh1,sh2,sh_a1,sh_a2);
            loA[c].set(lb0,lb1,lb2,la1,la2);  loB[c].set(lb0,lb1,lb2,la1,la2);
            midA[c].set(hb0,hb1,hb2,ha1,ha2); midB[c].set(hb0,hb1,hb2,ha1,ha2);
        }

        // ---- 超声保护 (仅 DSD 派生采样率) ----
        usOn = (sr >= Tuning::US_MIN_SR);
        if (usOn) {
            // 8 阶巴特沃斯 -> 4 级二阶级联, 用巴特沃斯极点对应的 Q
            static const float kUsQ[4] = { 0.50980f, 0.60135f, 0.89998f, 2.56292f };
            double g0,g1,g2,ga1,ga2;
            for (int c = 0; c < 2; ++c)
                for (int k = 0; k < 4; ++k) {
                    make_lp_q(Tuning::US_FREQ, kUsQ[k], sr, g0,g1,g2,ga1,ga2);
                    us[c][k].set(g0,g1,g2,ga1,ga2);
                }
        }

        dr     = clampf(p.drive, 0.f, 1.f);
        bypass = (dr <= 1e-4f) && !eqOn;
        mkDb   = (p.makeupDb != 0.f) ? p.makeupDb : Tuning::MAKEUP_DB;
        softK  = Tuning::SOFT_KNEE;
        satLo  = p.segSatLo  > 0.f ? p.segSatLo  : Tuning::SEG_SAT_LO;
        satMid = p.segSatMid > 0.f ? p.segSatMid : Tuning::SEG_SAT_MID;
        satHi  = p.segSatHi  > 0.f ? p.segSatHi  : Tuning::SEG_SAT_HI;
        const float envMs  = p.envMs  > 0.f ? p.envMs  : Tuning::ENV_MS;
        const float gAttMs = p.gAttMs > 0.f ? p.gAttMs : Tuning::G_ATT;
        const float gRelMs = p.gRelMs > 0.f ? p.gRelMs : Tuning::G_REL;
        const float g2RelMs= p.g2RelMs;             // 0 = 关掉二级平滑
        const float loA    = p.loAttMs > 0.f ? p.loAttMs : Tuning::LO_ATT;
        const float loR    = p.loRelMs > 0.f ? p.loRelMs : Tuning::LO_REL;
        fLo.setParams(loA, loR, sr);
        gAtt = 1.f - std::exp(-1.f/(gAttMs*sr));
        gRel = 1.f - std::exp(-1.f/(gRelMs*sr));
        g2On = (g2RelMs > 0.f);
        g2Rel = g2On ? (1.f - std::exp(-1.f/(g2RelMs*sr))) : 1.f;
        const float envHiMs  = p.envHiMs  > 0.f ? p.envHiMs  : Tuning::ENV_HI;
        const float gAttHiMs = p.gAttHiMs > 0.f ? p.gAttHiMs : Tuning::G_ATT_HI;
        const float gRelHiMs = p.gRelHiMs > 0.f ? p.gRelHiMs : Tuning::G_REL_HI;
        const float envMidMs  = p.envMidMs  > 0.f ? p.envMidMs  : envMs;
        const float gAttMidMs = p.gAttMidMs > 0.f ? p.gAttMidMs : gAttMs;
        const float gRelMidMs = p.gRelMidMs > 0.f ? p.gRelMidMs : gRelMs;
        smMid.setParams(envMidMs, sr);
        smHi.setParams(envHiMs, sr);
        gAttMid = 1.f - std::exp(-1.f/(gAttMidMs*sr));
        gRelMid = 1.f - std::exp(-1.f/(gRelMidMs*sr));
        gAttHi = 1.f - std::exp(-1.f/(gAttHiMs*sr));
        gRelHi = 1.f - std::exp(-1.f/(gRelHiMs*sr));
        lim.setParams(sr, Tuning::CEILING,
                      p.limAttMs > 0.f ? p.limAttMs : Tuning::LIM_ATT,
                      p.limRelMs > 0.f ? p.limRelMs : Tuning::LIM_REL);
        reset(sr);
    }

    // 交错立体声 [L,R,L,R,...]
    void process(float* buf, int frames, float sr) {
        (void)sr;
        if (bypass) return;                 // 驱动=0 且 EQ 全平: 一个样本都不动
        for (int i = 0; i < frames; ++i) {
            float xl = buf[2*i], xr = buf[2*i+1];
            // 超声保护: DSD 派生采样率下, 先把 20k 以上的噪声切掉再进非线性段
            if (usOn) {
                for (int k = 0; k < 4; ++k) { xl = us[0][k].run(xl); xr = us[1][k].run(xr); }
            }
            // 静态 EQ (预设 + 低音)
            if (eqOn) {
                for (int k = 0; k < 5; ++k) { xl = eq[0][k].run(xl); xr = eq[1][k].run(xr); }
            }
            // DC 阻断 (系数按采样率换算, 见 dcR)。用 double 累加: 极点极靠近 1,
            // float 下 x-dcx 这一步会对低频产生可观的舍入噪声。
            double tl = (double)xl - dcx[0] + dcR*dcy[0]; dcx[0]=(double)xl; dcy[0]=tl; xl = (float)tl;
            double tr = (double)xr - dcx[1] + dcR*dcy[1]; dcx[1]=(double)xr; dcy[1]=tr; xr = (float)tr;
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
            gsMid += (tMid - gsMid) * (tMid < gsMid ? gAttMid : gRelMid);
            gsHi  += (tHi  - gsHi ) * (tHi  < gsHi  ? gAttHi : gRelHi);
            float gLo = gsLo, gMid = gsMid, gHi = gsHi;
            if (g2On) {
                // 二级平滑: 再串一级长释放, 专门压 0.2-2Hz 的慢起伏 (听感「波浪」)
                g2Lo  += (gLo  - g2Lo ) * (gLo  < g2Lo  ? gAtt : g2Rel);
                g2Mid += (gMid - g2Mid) * (gMid < g2Mid ? gAtt : g2Rel);
                g2Hi  += (gHi  - g2Hi ) * (gHi  < g2Hi  ? gAtt : g2Rel);
                gLo = g2Lo; gMid = g2Mid; gHi = g2Hi;
            }
            float kLo = g2l(gLo + mkDb), kMid = g2l(gMid + mkDb), kHi = g2l(gHi + mkDb);
            float ol = (softsat(ll*kLo, satLo) + softsat(ml*kMid, satMid) + softsat(hl*kHi, satHi));
            float orr= (softsat(lr*kLo, satLo) + softsat(mr*kMid, satMid) + softsat(hr*kHi, satHi));
            if (postOn) { ol = peq[0].run(ol); orr = peq[1].run(orr); }
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
    Biquad peq[2];
    Biquad us[2][4];                    // 超声保护 (8 阶低通) 4 级级联
    bool   usOn = false;
    bool   postOn = false;
    double dcR = 0.995;                 // DC 阻断极点, 实际值由采样率决定
    double dcx[2] = {0,0}, dcy[2] = {0,0};
    Follower fLo;
    Smooth smMid, smHi;
    float gAtt = 0.f, gRel = 0.f, gsLo = 0.f, gsMid = 0.f, gsHi = 0.f;
    float gAttHi = 0.f, gRelHi = 0.f;
    float gAttMid = 0.f, gRelMid = 0.f;
    float dr = 0.f, mkDb = 0.f;
    float softK = Tuning::SOFT_KNEE;
    bool  g2On = false;  float g2Rel = 1.f;
    float g2Lo = 0.f, g2Mid = 0.f, g2Hi = 0.f;
    float satLo = 0.f, satMid = 0.f, satHi = 0.f;
    bool  eqOn = false, bypass = false;
    Limiter lim;
public:
    float limGain() const { return lim.curGain(); }   // 诊断用
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
