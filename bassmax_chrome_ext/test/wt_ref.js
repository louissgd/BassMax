// wt_ref.js — 与 _wtest.html 完全相同的输入与参数, 用 node 里的 BXCore 算出参考结果
'use strict';
const path = require('path');
require(path.join(__dirname, '..', 'worklet', 'bassmax-worklet.js'));
const { BXCore, bxDefaultParams } = globalThis.BassMaxDSP;

const SR = 48000, N = 48000;
const PARAMS = { drive: 0.85, bass: 3, preset: 4, trim9k: 2 };

const sig = (i) => {
  const t = i / SR;
  return 0.40 * Math.sin(2 * Math.PI * 55 * t)
       + 0.20 * Math.sin(2 * Math.PI * 900 * t + 0.7)
       + 0.10 * Math.sin(2 * Math.PI * 6000 * t);
};

// 输入必须是 float32 (AudioBuffer 存的是 float32)
const bl = new Float32Array(N), br = new Float32Array(N);
for (let i = 0; i < N; i++) { const v = sig(i); bl[i] = v; br[i] = v * 0.9; }

const p = Object.assign(bxDefaultParams(), PARAMS);
const core = new BXCore();
core.setParams(p, SR);

const al = new Float32Array(N), ar = new Float32Array(N);
const BLOCK = 128;   // AudioWorklet 的渲染量子
for (let off = 0; off < N; off += BLOCK) {
  const n = Math.min(BLOCK, N - off);
  core.processBlock(bl.subarray(off), br.subarray(off), al.subarray(off), ar.subarray(off), n);
}

let sum = 0, absSum = 0, peak = 0;
const probes = [];
for (let i = 0; i < N; i++) {
  sum += al[i] * al[i];
  absSum += Math.abs(al[i]);
  if (Math.abs(al[i]) > peak) peak = Math.abs(al[i]);
  if (i % 8000 === 0) probes.push([i, +al[i].toFixed(9), +ar[i].toFixed(9)]);
}

console.log(JSON.stringify({
  ok: true, len: N, rms: Math.sqrt(sum / N), absSum, peak, probes,
}));
