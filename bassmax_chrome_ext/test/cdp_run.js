// cdp_run.js — 真机验证: 在 Edge/Chrome 里跑 AudioWorklet 版 DSP, 用 CDP 取回结果
//
// 做法: 把 test/wtest.html + wtest.js 临时拷到扩展根目录 (chrome-extension:// 页面才能
// 加载 worklet 模块, file:// 会被 CORS 挡), 起一个装了本扩展的无头浏览器, 离线渲染
// 48000 帧后把统计量读回来; 结果应与 `node test/wt_ref.js` 完全一致。
//
// 用法: node test/cdp_run.js
'use strict';
const { spawn } = require('child_process');
const path = require('path');
const fs = require('fs');

const BROWSER = process.env.BROWSER ||
  'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe';
const ROOT = path.resolve(__dirname, '..');
const PROF = path.join(__dirname, 'prof_cdp');
const PORT = 9345;
const EXT_ID = process.env.EXT_ID || 'bflpahpenlaljgjnknpcfnhnoaklehjc';
const PAGE = `chrome-extension://${EXT_ID}/wtest.html`;

const tmpHtml = path.join(ROOT, 'wtest.html');
const tmpJs = path.join(ROOT, 'wtest.js');
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function cleanup() {
  for (const f of [tmpHtml, tmpJs]) { try { fs.unlinkSync(f); } catch (e) {} }
}

(async () => {
  fs.copyFileSync(path.join(__dirname, 'wtest.html'), tmpHtml);
  fs.copyFileSync(path.join(__dirname, 'wtest.js'), tmpJs);
  fs.rmSync(PROF, { recursive: true, force: true });

  const child = spawn(BROWSER, [
    '--headless=new', '--disable-gpu', '--no-first-run', '--mute-audio',
    '--autoplay-policy=no-user-gesture-required',
    `--user-data-dir=${PROF}`,
    `--load-extension=${ROOT}`,
    `--remote-debugging-port=${PORT}`,
    'about:blank',
  ], { stdio: 'ignore' });

  const list = async () => (await (await fetch(`http://127.0.0.1:${PORT}/json/list`)).json());
  let target = null;
  for (let i = 0; i < 40 && !target; i++) {
    await sleep(300);
    try {
      const l = await list();
      target = l.find((t) => t.type === 'page');
    } catch (e) { /* 还没起来 */ }
  }
  if (!target) { console.error('浏览器没有起来'); cleanup(); child.kill(); process.exit(1); }

  const ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((r) => { ws.onopen = r; });
  let id = 0;
  const pending = new Map();
  ws.onmessage = (ev) => {
    const m = JSON.parse(ev.data);
    if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
  };
  const cmd = (method, params) => new Promise((res) => {
    const i = ++id;
    pending.set(i, res);
    ws.send(JSON.stringify({ id: i, method, params }));
  });

  await cmd('Page.enable', {});
  await cmd('Page.navigate', { url: PAGE });

  let text = '';
  for (let i = 0; i < 60; i++) {
    await sleep(400);
    const r = await cmd('Runtime.evaluate', {
      expression: "(document.getElementById('out')||{}).textContent || ''",
      returnByValue: true,
    });
    text = (r.result && r.result.result && r.result.result.value) || '';
    if (text && !text.startsWith('STEP') && text !== 'pending') break;
  }
  console.log(text);
  ws.close();
  child.kill();
  cleanup();
  process.exit(text.startsWith('{"ok":true') ? 0 : 1);
})();
