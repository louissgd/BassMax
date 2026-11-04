// bassmax-worklet.js — BassMax v4 的 JS 移植 (AudioWorkletProcessor)
//
// 逐行对应 bassmax_dshow/bassmax_core.hpp:
//   输入 -> 静态 EQ(6 预设 + 低音/高音) -> DC 阻断 -> 三段分频(LR4 120Hz / 2500Hz)
//        -> 每段「测电平 -> 拉伸到目标电平」-> 相加 -> 末端前瞻峰值保护 -> 软削波
//
// 为了和 C++ 版保持数值一致, 所有 C++ 里是 float 的地方都用 Math.fround 显式截断:
//   双二阶状态是 double, 但输入/输出是 float; 包络/增益状态是 float; 输出缓冲是 float。
//   JS 的 number 全是 double, 不主动 fround 的话, 长时间累积后滤波器和包络会偏离 C++ 版。
'use strict';

const BX_PI2 = 2 * Math.PI;
const BX_F = Math.fround;

const bxG2L = (db) => Math.exp(db * 0.1151292546);
const bxL2G = (lin) => 8.685889638 * Math.log(Math.max(lin, 1e-9));
const bxClamp = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);

// ---------------------------------------------------------------------------
// 二阶 RBJ 双二阶
// ---------------------------------------------------------------------------
class BXBiquad {
  constructor() { this.b0 = 1; this.b1 = 0; this.b2 = 0; this.a1 = 0; this.a2 = 0; this.clear(); }
  set(b0, b1, b2, a1, a2) {
    this.b0 = b0; this.b1 = b1; this.b2 = b2; this.a1 = a1; this.a2 = a2;
    this.clear();
  }
  // 只换系数, 保留状态 (拖滑块时避免状态清零造成的爆音)
  setCoef(b0, b1, b2, a1, a2) { this.b0 = b0; this.b1 = b1; this.b2 = b2; this.a1 = a1; this.a2 = a2; }
  clear() { this.x1 = 0; this.x2 = 0; this.y1 = 0; this.y2 = 0; }
  run(x) {
    const xf = BX_F(x);
    const y = this.b0 * xf + this.b1 * this.x1 + this.b2 * this.x2 - this.a1 * this.y1 - this.a2 * this.y2;
    this.x2 = this.x1; this.x1 = xf; this.y2 = this.y1; this.y1 = y;
    return BX_F(y);
  }
}

// 系数写进这个复用数组: [b0,b1,b2,a1,a2]
const BX_CO = new Float64Array(5);

function bxApply(bq, keep) {
  if (keep) bq.setCoef(BX_CO[0], BX_CO[1], BX_CO[2], BX_CO[3], BX_CO[4]);
  else bq.set(BX_CO[0], BX_CO[1], BX_CO[2], BX_CO[3], BX_CO[4]);
}

function bxSet5(bq, a, keep) {
  if (keep) bq.setCoef(a[0], a[1], a[2], a[3], a[4]);
  else bq.set(a[0], a[1], a[2], a[3], a[4]);
}

function bxButterLp(f0, sr) {
  const Q = 0.70710678118654752;
  const w0 = BX_PI2 * f0 / sr;
  const cw = Math.cos(w0), sw = Math.sin(w0);
  const alpha = sw / (2 * Q);
  const a0 = 1 + alpha;
  BX_CO[0] = (1 - cw) * 0.5 / a0; BX_CO[1] = (1 - cw) / a0; BX_CO[2] = BX_CO[0];
  BX_CO[3] = -2 * cw / a0;        BX_CO[4] = (1 - alpha) / a0;
}

function bxButterHp(f0, sr) {
  const Q = 0.70710678118654752;
  const w0 = BX_PI2 * f0 / sr;
  const cw = Math.cos(w0), sw = Math.sin(w0);
  const alpha = sw / (2 * Q);
  const a0 = 1 + alpha;
  BX_CO[0] = (1 + cw) * 0.5 / a0; BX_CO[1] = -(1 + cw) / a0; BX_CO[2] = BX_CO[0];
  BX_CO[3] = -2 * cw / a0;        BX_CO[4] = (1 - alpha) / a0;
}

function bxLpQ(f0, Q, sr) {
  const w0 = BX_PI2 * f0 / sr;
  const cw = Math.cos(w0), sw = Math.sin(w0);
  const alpha = sw / (2 * Q);
  const a0 = 1 + alpha;
  BX_CO[0] = (1 - cw) * 0.5 / a0; BX_CO[1] = (1 - cw) / a0; BX_CO[2] = BX_CO[0];
  BX_CO[3] = -2 * cw / a0;        BX_CO[4] = (1 - alpha) / a0;
}

function bxPeak(f0, Q, gainDb, sr) {
  if (Math.abs(gainDb) < 0.01) { BX_CO[0] = 1; BX_CO[1] = 0; BX_CO[2] = 0; BX_CO[3] = 0; BX_CO[4] = 0; return; }
  const A = Math.pow(10, gainDb / 40);
  const w0 = BX_PI2 * f0 / sr;
  const cw = Math.cos(w0), sw = Math.sin(w0);
  const alpha = sw / (2 * Q);
  const a0 = 1 + alpha / A;
  BX_CO[0] = (1 + alpha * A) / a0; BX_CO[1] = -2 * cw / a0; BX_CO[2] = (1 - alpha * A) / a0;
  BX_CO[3] = -2 * cw / a0;         BX_CO[4] = (1 - alpha / A) / a0;
}

function bxLowShelf(f0, gainDb, sr) {
  if (Math.abs(gainDb) < 0.01) { BX_CO[0] = 1; BX_CO[1] = 0; BX_CO[2] = 0; BX_CO[3] = 0; BX_CO[4] = 0; return; }
  const A = Math.pow(10, gainDb / 40);
  const w0 = BX_PI2 * f0 / sr;
  const cw = Math.cos(w0), sw = Math.sin(w0);
  const alpha = sw * 0.5 * 1.4142135623730951;   // S = 1
  const sA = Math.sqrt(A);
  const a0 = (A + 1) + (A - 1) * cw + 2 * sA * alpha;
  BX_CO[0] = A * ((A + 1) - (A - 1) * cw + 2 * sA * alpha) / a0;
  BX_CO[1] = 2 * A * ((A - 1) - (A + 1) * cw) / a0;
  BX_CO[2] = A * ((A + 1) - (A - 1) * cw - 2 * sA * alpha) / a0;
  BX_CO[3] = -2 * ((A - 1) + (A + 1) * cw) / a0;
  BX_CO[4] = ((A + 1) + (A - 1) * cw - 2 * sA * alpha) / a0;
}

function bxHighShelf(f0, gainDb, sr) {
  if (Math.abs(gainDb) < 0.01) { BX_CO[0] = 1; BX_CO[1] = 0; BX_CO[2] = 0; BX_CO[3] = 0; BX_CO[4] = 0; return; }
  const A = Math.pow(10, gainDb / 40);
  const w0 = BX_PI2 * f0 / sr;
  const cw = Math.cos(w0), sw = Math.sin(w0);
  const alpha = sw * 0.5 * 1.4142135623730951;
  const sA = Math.sqrt(A);
  const a0 = (A + 1) - (A - 1) * cw + 2 * sA * alpha;
  BX_CO[0] = A * ((A + 1) + (A - 1) * cw + 2 * sA * alpha) / a0;
  BX_CO[1] = -2 * A * ((A - 1) + (A + 1) * cw) / a0;
  BX_CO[2] = A * ((A + 1) + (A - 1) * cw - 2 * sA * alpha) / a0;
  BX_CO[3] = 2 * ((A - 1) - (A + 1) * cw) / a0;
  BX_CO[4] = ((A + 1) - (A - 1) * cw - 2 * sA * alpha) / a0;
}

// ---------------------------------------------------------------------------
// 6 个 EQ 预设: [100Hz 低架, 300Hz, 900Hz, 3kHz, 9kHz 高架] dB
// 顺序与原版界面一致: 标准乐 / 古典乐 / 俱乐部 / 流行乐 / 爵士乐 / 摇滚乐
// ---------------------------------------------------------------------------
const BX_PRESETS = [
  { ls: 0.0, p300: 0.0, p900: 0.0, p3k: 0.0, hs: 0.0, name: '标准乐' },
  { ls: 2.5, p300: 0.5, p900: 0.0, p3k: -1.0, hs: -3.5, name: '古典乐' },
  { ls: 6.0, p300: 1.5, p900: 2.5, p3k: 3.0, hs: 1.5, name: '俱乐部' },
  { ls: -1.0, p300: 5.0, p900: 5.5, p3k: 1.0, hs: -1.0, name: '流行乐' },
  { ls: 3.0, p300: 1.5, p900: -1.0, p3k: 2.0, hs: 3.5, name: '爵士乐' },
  { ls: 7.0, p300: -3.5, p900: 1.5, p3k: 6.5, hs: 8.0, name: '摇滚乐' },
];

// 电平拉伸参数 (驱动 = 100%): gain_dB = clamp(S*(T-L), FLOOR, C)
const BX_TUNE = {
  LO:  { S: 0.50, T: -1.0, C: 17.5, FLOOR: 0.0 },
  MID: { S: 0.80, T: -12.5, C: 27.0, FLOOR: -1.0 },
  HI:  { S: 0.50, T: -12.5, C: 28.0, FLOOR: -1.0 },
  XOVER_LO: 120.0,
  XOVER_HI: 2500.0,
  BASS_FREQ: 100.0,
  ENV_MS: 0.050,
  LO_ATT: 0.005, LO_REL: 0.200,
  G_ATT: 0.030, G_REL: 0.400,
  ENV_HI: 0.010, G_ATT_HI: 0.020, G_REL_HI: 0.150,
  LIM_LOOK: 0.005, LIM_ATT: 0.0003, LIM_REL: 0.090,
  CEILING: 0.99, SOFT_KNEE: 0.93, MAKEUP_DB: 2.5,
  SEG_SAT_LO: 0.82, SEG_SAT_MID: 0.75, SEG_SAT_HI: 0.67,
  US_FREQ: 20000.0, US_MIN_SR: 176400.0,
};

// ---------------------------------------------------------------------------
// 一阶平滑 / 包络跟随
// ---------------------------------------------------------------------------
class BXSmooth {
  constructor() { this.y = 0; this.c = 1; }
  setParams(t, sr) { this.c = BX_F(1 - Math.exp(-1 / (t * sr))); this.y = 0; }
  clear() { this.y = 0; }
  run(x) { this.y = BX_F(this.y + (x - this.y) * this.c); return this.y; }
}

class BXFollower {
  constructor() { this.ea = 0; this.er = 0; this.att = 0; this.rel = 0; }
  setParams(ta, tr, sr) {
    this.att = BX_F(1 - Math.exp(-1 / (ta * sr)));
    this.rel = BX_F(1 - Math.exp(-1 / (tr * sr)));
    this.ea = 0; this.er = 0;
  }
  clear() { this.ea = 0; this.er = 0; }
  run(x) {
    const ax = Math.abs(x);
    this.ea = BX_F(this.ea + (ax - this.ea) * this.att);
    this.er = BX_F(this.er + (ax - this.er) * this.rel);
    return this.ea > this.er ? this.ea : this.er;
  }
}

// ---------------------------------------------------------------------------
// 前瞻峰值保护: 立体声联动, 窗口取最大。单调双端队列 O(1) 摊还。
// ---------------------------------------------------------------------------
class BXLimiter {
  constructor() { this.W = 0; this.wp = 0; this.cap = 0; this.gain = 1; this.ceil = BX_TUNE.CEILING; this.att = 0; this.rel = 0; }
  setParams(sr, ceil, att = BX_TUNE.LIM_ATT, rel = BX_TUNE.LIM_REL) {
    this.ceil = ceil;
    this.att = BX_F(1 - Math.exp(-1 / (att * sr)));
    this.rel = BX_F(1 - Math.exp(-1 / (rel * sr)));
    let w = (BX_TUNE.LIM_LOOK * sr) | 0;
    if (w < 8) w = 8;
    if (this.W !== w) {
      this.dl = new Float32Array(w);
      this.dr = new Float32Array(w);
      this.cap = w + 2;
      this.dqv = new Float32Array(this.cap);
      this.dqi = new Float64Array(this.cap);
    }
    this.W = w;
    this.clear();
  }
  clear() {
    if (this.dl) { this.dl.fill(0); this.dr.fill(0); }
    this.wp = 0; this.gain = 1; this.dqn = 0; this.qh = 0; this.qt = 0;
  }
  run(l, r) {
    const W = this.W;
    if (W <= 0) { this.ol = l; this.or = r; return; }
    const dl = this.dl, dr = this.dr, cap = this.cap;
    dl[this.wp] = l; dr[this.wp] = r;
    const outPos = (this.wp + 1) % W;

    const al = Math.abs(l), ar = Math.abs(r);
    const v = al > ar ? al : ar;
    const idx = ++this.dqn;

    while (this.qt > this.qh) {
      const bi = (this.qt - 1) % cap;
      if (this.dqv[bi] > v) break;
      --this.qt;
    }
    if (this.qt === this.qh) { this.qh = 0; this.qt = 0; }
    const ti = this.qt % cap;
    this.dqv[ti] = v; this.dqi[ti] = idx; ++this.qt;

    const oldest = idx - W;
    while (this.qh < this.qt && this.dqi[this.qh % cap] <= oldest) {
      ++this.qh;
      if (this.qh >= this.qt) { this.qh = 0; this.qt = 0; break; }
    }
    const pk = this.dqv[this.qh % cap];

    const tgt = pk > this.ceil ? this.ceil / pk : 1;
    const c = tgt < this.gain ? this.att : this.rel;
    this.gain = BX_F(this.gain + (tgt - this.gain) * c);
    if (this.gain > 1) this.gain = 1;
    this.ol = BX_F(dl[outPos] * this.gain);
    this.or = BX_F(dr[outPos] * this.gain);
    this.wp = outPos;
  }
}

// ---------------------------------------------------------------------------
// 参数 (字段名与 Params 一致, 单位: 秒)
// ---------------------------------------------------------------------------
function bxDefaultParams() {
  return {
    drive: 0.0,      // 0..1
    bass: 0.0,       // dB, -12..+12 (100Hz 低架)
    preset: 0,       // 0..5
    makeupDb: 0.0,   // 0 = 用默认 2.5
    segSatLo: 0, segSatMid: 0, segSatHi: 0,   // 0 = 默认
    trim1k: 0, trim9k: 0,                    // dB, EQ 微调
    envMs: 0, gAttMs: 0, gRelMs: 0, g2RelMs: 0,
    loAttMs: 0, loRelMs: 0,
    limAttMs: 0, limRelMs: 0,
    envHiMs: 0, gAttHiMs: 0, gRelHiMs: 0,
    envMidMs: 0, gAttMidMs: 0, gRelMidMs: 0,
  };
}

// ---------------------------------------------------------------------------
// 主处理 (立体声)
// ---------------------------------------------------------------------------
class BXCore {
  constructor() {
    this.sumA = [new BXBiquad(), new BXBiquad()]; this.sumB = [new BXBiquad(), new BXBiquad()];
    this.hiA = [new BXBiquad(), new BXBiquad()]; this.hiB = [new BXBiquad(), new BXBiquad()];
    this.loA = [new BXBiquad(), new BXBiquad()]; this.loB = [new BXBiquad(), new BXBiquad()];
    this.midA = [new BXBiquad(), new BXBiquad()]; this.midB = [new BXBiquad(), new BXBiquad()];
    this.eq = [[], []];
    for (let c = 0; c < 2; c++) for (let k = 0; k < 5; k++) this.eq[c].push(new BXBiquad());
    this.peq = [new BXBiquad(), new BXBiquad()];
    this.us = [[], []];
    for (let c = 0; c < 2; c++) for (let k = 0; k < 4; k++) this.us[c].push(new BXBiquad());
    this.fLo = new BXFollower();
    this.smMid = new BXSmooth();
    this.smHi = new BXSmooth();
    this.lim = new BXLimiter();
    this.dcx = [0, 0]; this.dcy = [0, 0];
    this.dcR = 0.995;
    this.reset(44100);
  }

  reset(sr) {
    if (sr > 0) this.dcR = Math.pow(0.995, 44100 / sr);
    for (let c = 0; c < 2; c++) {
      this.sumA[c].clear(); this.sumB[c].clear();
      this.hiA[c].clear(); this.hiB[c].clear();
      this.loA[c].clear(); this.loB[c].clear();
      this.midA[c].clear(); this.midB[c].clear();
      for (let k = 0; k < 5; k++) this.eq[c][k].clear();
      for (let k = 0; k < 4; k++) this.us[c][k].clear();
      this.peq[c].clear();
    }
    this.dcx[0] = this.dcx[1] = this.dcy[0] = this.dcy[1] = 0;
    this.fLo.clear(); this.smMid.clear(); this.smHi.clear();
    this.gsLo = this.gsMid = this.gsHi = 0;
    this.g2Lo = this.g2Mid = this.g2Hi = 0;
    this.lim.clear();
    this.lvlLo = this.lvlMid = this.lvlHi = 0;
    this.gLo = this.gMid = this.gHi = 0;
  }

  // C++ 语义: setParams 末尾会 reset -> 状态清零。工作线程里连续拖滑块用 update() 免重置。
  setParams(p, sr) { this._compute(p, sr, true); }
  update(p, sr) { this._compute(p, sr, false); }

  _compute(p, sr, doReset) {
    const keep = !doReset;
    const ps = BX_PRESETS[bxClamp(p.preset | 0, 0, 5)];
    const kFreq = [300, 900, 3000];
    for (let c = 0; c < 2; c++) {
      bxLowShelf(BX_TUNE.BASS_FREQ, ps.ls + p.bass, sr);   bxApply(this.eq[c][0], keep);
      bxPeak(kFreq[0], 1.0, ps.p300, sr);                  bxApply(this.eq[c][1], keep);
      bxPeak(kFreq[1], 1.0, ps.p900 + p.trim1k, sr);       bxApply(this.eq[c][2], keep);
      bxPeak(kFreq[2], 1.0, ps.p3k, sr);                   bxApply(this.eq[c][3], keep);
      bxHighShelf(9000, ps.hs, sr);                        bxApply(this.eq[c][4], keep);
      bxHighShelf(9000, p.trim9k, sr);                     bxApply(this.peq[c], keep);
    }
    this.postOn = Math.abs(p.trim9k) > 0.01;
    this.eqOn = (Math.abs(ps.ls) + Math.abs(ps.p300) + Math.abs(ps.p900) +
                 Math.abs(ps.p3k) + Math.abs(ps.hs) + Math.abs(p.bass) +
                 Math.abs(p.trim1k)) > 0.01;

    // 三段分频 (LR4 = 两级 Butterworth 级联)
    bxButterLp(BX_TUNE.XOVER_LO, sr); const lb = BX_CO.slice();
    bxButterHp(BX_TUNE.XOVER_LO, sr); const hb = BX_CO.slice();
    bxButterLp(BX_TUNE.XOVER_HI, sr); const sl = BX_CO.slice();
    bxButterHp(BX_TUNE.XOVER_HI, sr); const sh = BX_CO.slice();
    for (let c = 0; c < 2; c++) {
      bxSet5(this.sumA[c], sl, keep); bxSet5(this.sumB[c], sl, keep);
      bxSet5(this.hiA[c], sh, keep);  bxSet5(this.hiB[c], sh, keep);
      bxSet5(this.loA[c], lb, keep);  bxSet5(this.loB[c], lb, keep);
      bxSet5(this.midA[c], hb, keep); bxSet5(this.midB[c], hb, keep);
    }

    // 超声保护: 仅 DSD 派生采样率 (浏览器 tabCapture 恒为 48k, 基本不触发)
    this.usOn = sr >= BX_TUNE.US_MIN_SR;
    if (this.usOn) {
      const kUsQ = [0.50980, 0.60135, 0.89998, 2.56292];
      for (let c = 0; c < 2; c++) for (let k = 0; k < 4; k++) {
        bxLpQ(BX_TUNE.US_FREQ, kUsQ[k], sr);
        bxApply(this.us[c][k], keep);
      }
    }

    this.dr = bxClamp(p.drive, 0, 1);
    this.bypass = (this.dr <= 1e-4) && !this.eqOn;
    this.mkDb = p.makeupDb !== 0 ? p.makeupDb : BX_TUNE.MAKEUP_DB;
    this.softK = BX_TUNE.SOFT_KNEE;
    this.satLo = p.segSatLo > 0 ? p.segSatLo : BX_TUNE.SEG_SAT_LO;
    this.satMid = p.segSatMid > 0 ? p.segSatMid : BX_TUNE.SEG_SAT_MID;
    this.satHi = p.segSatHi > 0 ? p.segSatHi : BX_TUNE.SEG_SAT_HI;

    const envS = p.envMs > 0 ? p.envMs : BX_TUNE.ENV_MS;
    const gAttS = p.gAttMs > 0 ? p.gAttMs : BX_TUNE.G_ATT;
    const gRelS = p.gRelMs > 0 ? p.gRelMs : BX_TUNE.G_REL;
    const g2RelS = p.g2RelMs;
    const loA = p.loAttMs > 0 ? p.loAttMs : BX_TUNE.LO_ATT;
    const loR = p.loRelMs > 0 ? p.loRelMs : BX_TUNE.LO_REL;
    this.fLo.setParams(loA, loR, sr);
    this.gAtt = BX_F(1 - Math.exp(-1 / (gAttS * sr)));
    this.gRel = BX_F(1 - Math.exp(-1 / (gRelS * sr)));
    this.g2On = g2RelS > 0;
    this.g2Rel = this.g2On ? BX_F(1 - Math.exp(-1 / (g2RelS * sr))) : 1;

    const envHiS = p.envHiMs > 0 ? p.envHiMs : BX_TUNE.ENV_HI;
    const gAttHiS = p.gAttHiMs > 0 ? p.gAttHiMs : BX_TUNE.G_ATT_HI;
    const gRelHiS = p.gRelHiMs > 0 ? p.gRelHiMs : BX_TUNE.G_REL_HI;
    const envMidS = p.envMidMs > 0 ? p.envMidMs : envS;
    const gAttMidS = p.gAttMidMs > 0 ? p.gAttMidMs : gAttS;
    const gRelMidS = p.gRelMidMs > 0 ? p.gRelMidMs : gRelS;
    this.smMid.setParams(envMidS, sr);
    this.smHi.setParams(envHiS, sr);
    this.gAttMid = BX_F(1 - Math.exp(-1 / (gAttMidS * sr)));
    this.gRelMid = BX_F(1 - Math.exp(-1 / (gRelMidS * sr)));
    this.gAttHi = BX_F(1 - Math.exp(-1 / (gAttHiS * sr)));
    this.gRelHi = BX_F(1 - Math.exp(-1 / (gRelHiS * sr)));
    this.lim.setParams(sr, BX_TUNE.CEILING,
      p.limAttMs > 0 ? p.limAttMs : BX_TUNE.LIM_ATT,
      p.limRelMs > 0 ? p.limRelMs : BX_TUNE.LIM_REL);

    if (doReset) this.reset(sr);
  }

  static stretch(t, levelDb) {
    let g = t.S * (t.T - levelDb);
    if (g > t.C) g = t.C;
    if (g < t.FLOOR) g = t.FLOOR;
    return g;
  }

  static softsat(x, d) { return BX_F(d * Math.tanh(x / d)); }

  softclip(x) {
    const k = this.softK;
    const ax = Math.abs(x);
    if (ax <= k) return BX_F(x);
    let t = (ax - k) / (1 - k);
    if (t > 1) t = 1;
    const y = BX_F(k + (1 - k) * Math.tanh(t));
    return BX_F(x < 0 ? -y : y);
  }

  // il/ir: 输入 (Float32Array 视图), ol/orr: 输出
  processBlock(il, ir, ol, orr, n) {
    if (this.bypass) {
      if (ol !== il) ol.set(il.subarray(0, n));
      if (orr !== ir) orr.set(ir.subarray(0, n));
      return;
    }
    const sumA0 = this.sumA[0], sumB0 = this.sumB[0], hiA0 = this.hiA[0], hiB0 = this.hiB[0];
    const loA0 = this.loA[0], loB0 = this.loB[0], midA0 = this.midA[0], midB0 = this.midB[0];
    const sumA1 = this.sumA[1], sumB1 = this.sumB[1], hiA1 = this.hiA[1], hiB1 = this.hiB[1];
    const loA1 = this.loA[1], loB1 = this.loB[1], midA1 = this.midA[1], midB1 = this.midB[1];
    const eq0 = this.eq[0], eq1 = this.eq[1], peq0 = this.peq[0], peq1 = this.peq[1];
    const us0 = this.us[0], us1 = this.us[1];
    const fLo = this.fLo, smMid = this.smMid, smHi = this.smHi, lim = this.lim;
    const dcR = this.dcR, dcx = this.dcx, dcy = this.dcy;
    const dr = this.dr, mkDb = this.mkDb, eqOn = this.eqOn, postOn = this.postOn, usOn = this.usOn;
    const satLo = this.satLo, satMid = this.satMid, satHi = this.satHi;
    const gAtt = this.gAtt, gRel = this.gRel, gAttMid = this.gAttMid, gRelMid = this.gRelMid;
    const gAttHi = this.gAttHi, gRelHi = this.gRelHi;
    const g2On = this.g2On, g2Rel = this.g2Rel;
    const T = BX_TUNE;
    let gsLo = this.gsLo, gsMid = this.gsMid, gsHi = this.gsHi;
    let g2Lo = this.g2Lo, g2Mid = this.g2Mid, g2Hi = this.g2Hi;
    const softsat = BXCore.softsat;
    const softclip = (x) => this.softclip(x);

    for (let i = 0; i < n; i++) {
      let xl = il[i], xr = ir[i];

      if (usOn) {
        for (let k = 0; k < 4; k++) { xl = us0[k].run(xl); xr = us1[k].run(xr); }
      }
      if (eqOn) {
        for (let k = 0; k < 5; k++) { xl = eq0[k].run(xl); xr = eq1[k].run(xr); }
      }
      // DC 阻断
      let tl = xl - dcx[0] + dcR * dcy[0]; dcx[0] = xl; dcy[0] = tl; xl = BX_F(tl);
      let tr = xr - dcx[1] + dcR * dcy[1]; dcx[1] = xr; dcy[1] = tr; xr = BX_F(tr);

      const al = sumB0.run(sumA0.run(xl));
      const hl = hiB0.run(hiA0.run(xl));
      const ll = loB0.run(loA0.run(al));
      const ml = midB0.run(midA0.run(al));
      const ar = sumB1.run(sumA1.run(xr));
      const hr = hiB1.run(hiA1.run(xr));
      const lr = loB1.run(loA1.run(ar));
      const mr = midB1.run(midA1.run(ar));

      const vLo = bxL2G(fLo.run(BX_F(0.5 * (Math.abs(ll) + Math.abs(lr)))));
      const vMid = bxL2G(Math.sqrt(smMid.run(BX_F(0.5 * (ml * ml + mr * mr)))));
      const vHi = bxL2G(Math.sqrt(smHi.run(BX_F(0.5 * (hl * hl + hr * hr)))));

      const tLo = dr * BXCore.stretch(T.LO, vLo);
      const tMid = dr * BXCore.stretch(T.MID, vMid);
      const tHi = dr * BXCore.stretch(T.HI, vHi);

      gsLo = BX_F(gsLo + (tLo - gsLo) * (tLo < gsLo ? gAtt : gRel));
      gsMid = BX_F(gsMid + (tMid - gsMid) * (tMid < gsMid ? gAttMid : gRelMid));
      gsHi = BX_F(gsHi + (tHi - gsHi) * (tHi < gsHi ? gAttHi : gRelHi));
      let gLo = gsLo, gMid = gsMid, gHi = gsHi;
      if (g2On) {
        g2Lo = BX_F(g2Lo + (gLo - g2Lo) * (gLo < g2Lo ? gAtt : g2Rel));
        g2Mid = BX_F(g2Mid + (gMid - g2Mid) * (gMid < g2Mid ? gAtt : g2Rel));
        g2Hi = BX_F(g2Hi + (gHi - g2Hi) * (gHi < g2Hi ? gAtt : g2Rel));
        gLo = g2Lo; gMid = g2Mid; gHi = g2Hi;
      }

      const kLo = bxG2L(gLo + mkDb), kMid = bxG2L(gMid + mkDb), kHi = bxG2L(gHi + mkDb);
      let oL = BX_F(softsat(ll * kLo, satLo) + softsat(ml * kMid, satMid) + softsat(hl * kHi, satHi));
      let oR = BX_F(softsat(lr * kLo, satLo) + softsat(mr * kMid, satMid) + softsat(hr * kHi, satHi));
      if (postOn) { oL = peq0.run(oL); oR = peq1.run(oR); }
      lim.run(oL, oR);
      ol[i] = softclip(lim.ol);
      orr[i] = softclip(lim.or);

      this.lvlLo = vLo; this.lvlMid = vMid; this.lvlHi = vHi;
      this.gLo = gLo; this.gMid = gMid; this.gHi = gHi;
    }
    this.gsLo = gsLo; this.gsMid = gsMid; this.gsHi = gsHi;
    this.g2Lo = g2Lo; this.g2Mid = g2Mid; this.g2Hi = g2Hi;
  }
}

// ---------------------------------------------------------------------------
// AudioWorklet 入口
// ---------------------------------------------------------------------------
if (typeof AudioWorkletProcessor !== 'undefined' && typeof registerProcessor === 'function') {
  class BassMaxProcessor extends AudioWorkletProcessor {
    constructor(options) {
      super();
      this.sr = sampleRate;
      this.core = new BXCore();
      this.params = bxDefaultParams();
      this.enabled = true;
      // 构造时就吃进参数: 避免"渲染已经开始但端口消息还没到"导致的头几帧旁路
      const po = (options && options.processorOptions) || {};
      if (po.params) Object.assign(this.params, po.params);
      if (typeof po.enabled === 'boolean') this.enabled = po.enabled;
      this._apply(true);
      this._meterCount = 0;
      this.port.onmessage = (e) => {
        const d = e.data || {};
        if (d.type === 'params') {
          Object.assign(this.params, d.params || {});
          if (typeof d.enabled === 'boolean') this.enabled = d.enabled;
          this._apply(false);
        } else if (d.type === 'reset') {
          this.core.reset(this.sr);
        }
      };
      this.port.postMessage({ type: 'ready', sr: this.sr });
    }

    _apply(initial) {
      const p = Object.assign({}, this.params);
      if (!this.enabled) {          // 总开关关闭 -> 全零 -> Core 内部 bypass
        p.drive = 0; p.bass = 0; p.preset = 0; p.trim9k = 0; p.trim1k = 0;
      }
      if (initial) this.core.setParams(p, this.sr);
      else this.core.update(p, this.sr);
    }

    process(inputs, outputs) {
      const out = outputs[0];
      if (!out || out.length === 0) return true;
      const inp = inputs[0];
      const n = out[0].length;
      if (!inp || inp.length === 0) {
        for (let c = 0; c < out.length; c++) out[c].fill(0);
        return true;
      }
      const L = inp[0];
      const R = inp.length > 1 ? inp[1] : inp[0];
      const oL = out[0];
      const oR = out.length > 1 ? out[1] : out[0];
      this.core.processBlock(L, R, oL, oR, n);

      // 每 ~43ms 报一次表 (只在有监听时有意义, 开销可忽略)
      this._meterCount += n;
      if (this._meterCount >= 2048) {
        this._meterCount = 0;
        this.port.postMessage({
          type: 'meter',
          lvl: [this.core.lvlLo, this.core.lvlMid, this.core.lvlHi],
          gain: [this.core.gLo, this.core.gMid, this.core.gHi],
          lim: this.core.lim.gain,
        });
      }
      return true;
    }
  }
  registerProcessor('bassmax', BassMaxProcessor);
}

// 供离线测试 (node) 使用
if (typeof globalThis !== 'undefined') {
  globalThis.BassMaxDSP = { BXCore, BX_PRESETS, BX_TUNE, bxDefaultParams };
}
