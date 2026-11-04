// common.js — 后台 / 弹窗 / offscreen 共用的默认参数与预设元数据
// (worklet 里有自己的 bxDefaultParams(), 键名必须与此处一致)
'use strict';

const BM_DEFAULT_PARAMS = {
  drive: 0.0,
  bass: 0.0,
  preset: 0,
  makeupDb: 0.0,
  trim1k: 0.0,
  trim9k: 0.0,
  // 0 = 用 Tuning 默认值, 这里不暴露到界面
  segSatLo: 0, segSatMid: 0, segSatHi: 0,
  envMs: 0, gAttMs: 0, gRelMs: 0, g2RelMs: 0,
  loAttMs: 0, loRelMs: 0, limAttMs: 0, limRelMs: 0,
  envHiMs: 0, gAttHiMs: 0, gRelHiMs: 0,
  envMidMs: 0, gAttMidMs: 0, gRelMidMs: 0,
};

// 6 个预设的实际曲线 (dB), 与 bassmax_core.hpp 的 presets() 一一对应
const BM_PRESETS = [
  { name: '标准乐', diff: '原声' },
  { name: '古典乐', diff: '轻低音 / 柔高音' },
  { name: '俱乐部', diff: '重低音' },
  { name: '流行乐', diff: '人声靠前' },
  { name: '爵士乐', diff: '均衡' },
  { name: '摇滚乐', diff: 'V 型' },
];

const BM_LIMITS = {
  drive: [0, 1],
  bass: [-12, 12],
  trim9k: [-12, 12],
  trim1k: [-6, 6],
  makeupDb: [0, 6],
};

function bmClamp(v, lo, hi) { return v < lo ? lo : v > hi ? hi : v; }

function bmSanitize(p) {
  const out = Object.assign({}, BM_DEFAULT_PARAMS, p || {});
  out.drive = bmClamp(Number(out.drive) || 0, 0, 1);
  out.bass = bmClamp(Number(out.bass) || 0, -12, 12);
  out.trim9k = bmClamp(Number(out.trim9k) || 0, -12, 12);
  out.trim1k = bmClamp(Number(out.trim1k) || 0, -6, 6);
  out.makeupDb = bmClamp(Number(out.makeupDb) || 0, 0, 6);
  out.preset = bmClamp(Math.round(Number(out.preset) || 0), 0, 5);
  return out;
}
