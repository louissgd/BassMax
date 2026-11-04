// render_js.js — 用 JS 版 BXCore 离线渲染同一个 WAV, 输出 24bit WAV, 与 C++ 版逐样本比较
// 用法: node render_js.js in.wav out.wav [drive] [bass] ; 预设/微调走 env
'use strict';
const fs = require('fs');
const path = require('path');

require(path.join(__dirname, '..', 'worklet', 'bassmax-worklet.js'));
const { BXCore, bxDefaultParams } = globalThis.BassMaxDSP;

function readWav(p) {
  const b = fs.readFileSync(p);
  if (b.toString('ascii', 0, 4) !== 'RIFF') throw new Error('not riff');
  let off = 12, sr = 0, ch = 0, bits = 0, dataOff = -1, dataLen = 0;
  while (off + 8 <= b.length) {
    const id = b.toString('ascii', off, off + 4);
    const len = b.readUInt32LE(off + 4);
    if (id === 'fmt ') {
      ch = b.readUInt16LE(off + 10);
      sr = b.readUInt32LE(off + 12);
      bits = b.readUInt16LE(off + 22);
    } else if (id === 'data') { dataOff = off + 8; dataLen = len; }
    off += 8 + len + (len & 1);
  }
  const bytes = bits / 8;
  const n = Math.floor(dataLen / bytes);
  const out = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    const p2 = dataOff + i * bytes;
    if (bytes === 3) {
      let v = b[p2] | (b[p2 + 1] << 8) | (b[p2 + 2] << 16);
      if (v & 0x800000) v -= 0x1000000;
      out[i] = v / 8388608;
    } else if (bytes === 2) out[i] = b.readInt16LE(p2) / 32768;
    else out[i] = b.readInt32LE(p2) / 2147483648;
  }
  return { sr, ch, data: out };
}

function writeWav(p, sr, ch, data) {
  const n = data.length;
  const dataBytes = n * 3;
  const buf = Buffer.alloc(44 + dataBytes);
  buf.write('RIFF', 0, 'ascii'); buf.writeUInt32LE(36 + dataBytes, 4); buf.write('WAVE', 8, 'ascii');
  buf.write('fmt ', 12, 'ascii'); buf.writeUInt32LE(16, 16);
  buf.writeUInt16LE(1, 20); buf.writeUInt16LE(ch, 22);
  buf.writeUInt32LE(sr, 24); buf.writeUInt32LE(sr * ch * 3, 28);
  buf.writeUInt16LE(ch * 3, 32); buf.writeUInt16LE(24, 34);
  buf.write('data', 36, 'ascii'); buf.writeUInt32LE(dataBytes, 40);
  for (let i = 0; i < n; i++) {
    let v = data[i];
    if (v > 1) v = 1; if (v < -1) v = -1;
    const t = Math.round(v * 8388607);
    const o = 44 + i * 3;
    buf[o] = t & 0xff; buf[o + 1] = (t >> 8) & 0xff; buf[o + 2] = (t >> 16) & 0xff;
  }
  fs.writeFileSync(p, buf);
}

const inPath = process.argv[2], outPath = process.argv[3];
const drive = process.argv[4] !== undefined ? parseFloat(process.argv[4]) : 0.85;
const bass = process.argv[5] !== undefined ? parseFloat(process.argv[5]) : 2.0;

const w = readWav(inPath);
const frames = Math.floor(w.data.length / w.ch);
const p = bxDefaultParams();
p.drive = drive;
p.bass = bass;
p.preset = process.env.BASSMAX_PRESET ? parseInt(process.env.BASSMAX_PRESET, 10) : 0;
if (process.env.BASSMAX_TRIM_1K) p.trim1k = parseFloat(process.env.BASSMAX_TRIM_1K);
if (process.env.BASSMAX_TRIM_9K) p.trim9k = parseFloat(process.env.BASSMAX_TRIM_9K);
if (process.env.BASSMAX_MAKEUP) p.makeupDb = parseFloat(process.env.BASSMAX_MAKEUP);
if (process.env.BASSMAX_SAT) { p.segSatLo = p.segSatMid = p.segSatHi = parseFloat(process.env.BASSMAX_SAT); }

const core = new BXCore();
core.setParams(p, w.sr);

const L = new Float32Array(frames), R = new Float32Array(frames);
for (let i = 0; i < frames; i++) {
  L[i] = w.data[i * w.ch];
  R[i] = w.ch > 1 ? w.data[i * w.ch + 1] : w.data[i * w.ch];
}
const oL = new Float32Array(frames), oR = new Float32Array(frames);

const block = 512;
for (let off = 0; off < frames; off += block) {
  const n = Math.min(block, frames - off);
  core.processBlock(L.subarray(off), R.subarray(off), oL.subarray(off), oR.subarray(off), n);
}

const inter = new Float32Array(frames * 2);
for (let i = 0; i < frames; i++) { inter[2 * i] = oL[i]; inter[2 * i + 1] = oR[i]; }
writeWav(outPath, w.sr, 2, inter);

let peak = 0, sum = 0;
for (let i = 0; i < inter.length; i++) { const a = Math.abs(inter[i]); if (a > peak) peak = a; sum += inter[i] * inter[i]; }
console.log(`out: peak=${peak.toFixed(4)} rms=${(10 * Math.log10(sum / inter.length + 1e-20)).toFixed(2)} dBFS`);
