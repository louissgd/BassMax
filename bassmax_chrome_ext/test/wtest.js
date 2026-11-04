'use strict';
const SR = 48000, N = 48000, CH = 2;
const PARAMS = { drive: 0.85, bass: 3, preset: 4, trim9k: 2 };

function sig(i) {
  const t = i / SR;
  return 0.40 * Math.sin(2 * Math.PI * 55 * t)
       + 0.20 * Math.sin(2 * Math.PI * 900 * t + 0.7)
       + 0.10 * Math.sin(2 * Math.PI * 6000 * t);
}

(async () => {
  const out = document.getElementById('out');
  const step = (s) => { out.textContent = "STEP:" + s; };
  try {
    step('ctx');
    const oc = new OfflineAudioContext(CH, N, SR);
    step('module');
    await oc.audioWorklet.addModule('worklet/bassmax-worklet.js');
    step('buffer');
    const buf = oc.createBuffer(CH, N, SR);
    const bl = buf.getChannelData(0), br = buf.getChannelData(1);
    for (let i = 0; i < N; i++) { const v = sig(i); bl[i] = v; br[i] = v * 0.9; }

    const src = oc.createBufferSource();
    src.buffer = buf;
    const node = new AudioWorkletNode(oc, 'bassmax', {
      numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2],
      channelCount: 2, channelCountMode: 'explicit',
      processorOptions: { params: PARAMS, enabled: true },
    });
    step('render');
    src.connect(node); node.connect(oc.destination); src.start();

    const rendered = await oc.startRendering();
    const a = rendered.getChannelData(0), b = rendered.getChannelData(1);

    let sum = 0, absSum = 0, peak = 0;
    const probes = [];
    for (let i = 0; i < a.length; i++) {
      sum += a[i] * a[i];
      absSum += Math.abs(a[i]);
      if (Math.abs(a[i]) > peak) peak = Math.abs(a[i]);
      if (i % 8000 === 0) probes.push([i, +a[i].toFixed(9), +b[i].toFixed(9)]);
    }
    out.textContent = JSON.stringify({
      ok: true, len: a.length,
      rms: Math.sqrt(sum / a.length), absSum, peak, probes,
    });
  } catch (e) {
    out.textContent = JSON.stringify({ ok: false, error: String(e && e.message || e) });
  }
})();
