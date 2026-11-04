// offscreen.js — 音频管线本体
//   tabCapture 流 -> MediaStreamSource -> AudioWorklet(BassMax DSP) -> destination
// 放在离屏文档里而不是内容脚本里, 原因:
//   1) 内容脚本的 AudioWorklet 模块要受页面 CSP 约束 (YouTube / 各类 SPA 常直接拦掉)
//   2) createMediaElementSource 遇到跨域音源会静音, tabCapture 拿的是整条混音, 没有这个问题
//   3) 不污染页面, 也不受页面 WebAudio 图影响
'use strict';

const WORKLET_URL = 'worklet/bassmax-worklet.js';

let ctx = null;
let stream = null;
let srcNode = null;
let worklet = null;
let meterOn = true;

function notify(payload) {
  try {
    const p = chrome.runtime.sendMessage(Object.assign({ target: 'bg' }, payload));
    if (p && p.catch) p.catch(() => {});
  } catch (e) { /* service worker 睡着时忽略 */ }
}

function postToPopup(payload) {
  try {
    const p = chrome.runtime.sendMessage(Object.assign({ target: 'popup' }, payload));
    if (p && p.catch) p.catch(() => {});
  } catch (e) {}
}

function applyParams(params, enabled) {
  if (!worklet) return;
  worklet.port.postMessage({ type: 'params', params, enabled });
}

async function teardown() {
  try { if (worklet) { worklet.port.onmessage = null; worklet.disconnect(); } } catch (e) {}
  try { if (srcNode) srcNode.disconnect(); } catch (e) {}
  try { if (stream) stream.getTracks().forEach((t) => t.stop()); } catch (e) {}
  try { if (ctx) await ctx.close(); } catch (e) {}
  worklet = null; srcNode = null; stream = null; ctx = null;
}

async function start(streamId, params, enabled) {
  await teardown();

  // getUserMedia 拿整条标签页混音 (Chrome 会把该标签页的本地播放改为由我们输出)
  stream = await navigator.mediaDevices.getUserMedia({
    audio: { mandatory: { chromeMediaSource: 'tab', chromeMediaSourceId: streamId } },
    video: false,
  });

  ctx = new AudioContext({ latencyHint: 'interactive' });
  await ctx.audioWorklet.addModule(WORKLET_URL);
  if (ctx.state !== 'running') { try { await ctx.resume(); } catch (e) {} }

  srcNode = ctx.createMediaStreamSource(stream);
  worklet = new AudioWorkletNode(ctx, 'bassmax', {
    numberOfInputs: 1,
    numberOfOutputs: 1,
    outputChannelCount: [2],
    channelCount: 2,
    channelCountMode: 'explicit',
    channelInterpretation: 'speakers',
    processorOptions: { params, enabled },
  });
  worklet.port.onmessage = (e) => {
    const d = e.data;
    if (!d) return;
    if (d.type === 'ready') {
      applyParams(params, enabled);
    } else if (d.type === 'meter' && meterOn) {
      postToPopup({ type: 'meter', lvl: d.lvl, gain: d.gain, lim: d.lim });
    }
  };

  srcNode.connect(worklet);
  worklet.connect(ctx.destination);

  applyParams(params, enabled);   // 端口会缓存, 处理器就绪前发也没问题

  // 流结束 (标签页关闭 / 导航) -> 通知后台收尾
  stream.getAudioTracks().forEach((t) => {
    t.addEventListener('ended', () => notify({ type: 'ended' }));
  });

  if (ctx.state !== 'running') {
    // 极少数情况下离屏文档被自动播放策略挂起, 报给上层而不是静音装死
    notify({ type: 'warn', error: 'AudioContext 未启动 (state=' + ctx.state + ')' });
  }
  return { sr: ctx.sampleRate, state: ctx.state };
}

chrome.runtime.onMessage.addListener((msg, sender, sendResponse) => {
  if (!msg || msg.target !== 'offscreen') return;

  (async () => {
    try {
      switch (msg.type) {
        case 'start': {
          const info = await start(msg.streamId, msg.params, msg.enabled);
          sendResponse({ ok: true, info });
          break;
        }
        case 'stop':
          await teardown();
          sendResponse({ ok: true });
          break;
        case 'params':
          applyParams(msg.params, msg.enabled);
          sendResponse({ ok: true });
          break;
        case 'meters':
          meterOn = !!msg.on;
          sendResponse({ ok: true });
          break;
        case 'ping':
          sendResponse({ ok: true, sr: ctx ? ctx.sampleRate : 0 });
          break;
        default:
          sendResponse({ ok: false, error: 'unknown' });
      }
    } catch (e) {
      sendResponse({ ok: false, error: String(e && e.message || e) });
    }
  })();

  return true;
});

// 离屏文档被销毁前确保轨道关掉, 否则标签页可能一直处于"被捕获"状态
window.addEventListener('pagehide', () => { teardown(); });
