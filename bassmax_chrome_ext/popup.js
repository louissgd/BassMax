// popup.js — 控制面板逻辑
'use strict';

const $ = (id) => document.getElementById(id);

let st = { active: false, tabId: null, error: null, params: Object.assign({}, BM_DEFAULT_PARAMS), enabled: true };
let myTabId = null;
let busy = false;
let dragging = false;

function send(msg) {
  return chrome.runtime.sendMessage(Object.assign({ target: 'bg' }, msg)).catch(() => null);
}

// ---------------- 渲染 ----------------
function fmtDb(v, unit = ' dB') {
  const s = v >= 0 ? '+' : '';
  return s + v.toFixed(1) + unit;
}

function setFill(input) {
  const min = parseFloat(input.min), max = parseFloat(input.max);
  const p = ((parseFloat(input.value) - min) / (max - min)) * 100;
  input.style.setProperty('--p', p.toFixed(2) + '%');
}

function renderParams() {
  const p = st.params;
  if (document.activeElement !== $('drive')) $('drive').value = (p.drive * 10).toFixed(1);
  if (document.activeElement !== $('bass')) $('bass').value = p.bass;
  if (document.activeElement !== $('treble')) $('treble').value = p.trim9k;
  if (document.activeElement !== $('makeup')) $('makeup').value = p.makeupDb > 0 ? p.makeupDb : 2.5;
  if (document.activeElement !== $('trim1k')) $('trim1k').value = p.trim1k;

  $('v-drive').textContent = (p.drive * 10).toFixed(1);
  $('v-bass').textContent = fmtDb(p.bass);
  $('v-treble').textContent = fmtDb(p.trim9k);
  $('v-makeup').textContent = fmtDb(p.makeupDb > 0 ? p.makeupDb : 2.5);
  $('v-trim1k').textContent = fmtDb(p.trim1k);

  ['drive', 'bass', 'treble', 'makeup', 'trim1k'].forEach((id) => setFill($(id)));

  document.querySelectorAll('.preset').forEach((el, i) => {
    el.classList.toggle('on', i === p.preset);
  });
}

function renderState() {
  const on = st.active;
  $('power').setAttribute('aria-checked', on ? 'true' : 'false');

  const s = $('status');
  s.classList.remove('on', 'err');
  if (on) s.classList.add('on');

  const otherTab = on && st.tabId !== myTabId;
  $('retarget').hidden = !otherTab;

  if (st.error && !on) {
    s.classList.add('err');
    $('statusText').textContent = st.error;
  } else if (on && otherTab) {
    $('statusText').textContent = '正在处理另一个标签页';
  } else if (on) {
    $('statusText').textContent = '已接管当前标签页音频';
  } else {
    $('statusText').textContent = '未接管当前标签页';
  }

  $('footNote').textContent = on
    ? '声音已由本地算法处理 · 关掉开关即恢复原声'
    : '点击右上角开关接管当前标签页音频';

  document.querySelector('.meters').classList.toggle('off', !on);
  if (!on) resetMeters();
}

function resetMeters() {
  document.querySelectorAll('.mrow').forEach((row) => {
    row.querySelector('.fill').style.width = '0%';
    row.querySelector('.mval').textContent = '0.0';
  });
}

// ---------------- 电平表 ----------------
const BAND_ROWS = { lo: 'lo', mid: 'mid', hi: 'hi' };

function renderMeter(m) {
  if (!st.active || !m) return;
  const rows = document.querySelectorAll('.mrow');
  const gains = m.gain || [0, 0, 0];
  const order = [0, 1, 2];
  rows.forEach((row, idx) => {
    if (idx > 2) return;
    const g = gains[order[idx]] || 0;
    const pct = Math.max(0, Math.min(100, ((g + 6) / 36) * 100));
    row.querySelector('.fill').style.width = pct.toFixed(1) + '%';
    row.querySelector('.mval').textContent = (g >= 0 ? '+' : '') + g.toFixed(1);
  });
  // 限幅器: gain 1.0 = 无动作, 换算成衰减量 dB
  const lim = m.lim === undefined ? 1 : m.lim;
  const gr = 20 * Math.log10(Math.max(lim, 1e-4));
  const limRow = document.querySelector('.mrow.limit');
  limRow.querySelector('.fill').style.width = Math.max(0, Math.min(100, (-gr / 12) * 100)).toFixed(1) + '%';
  limRow.querySelector('.mval').textContent = gr.toFixed(1);
}

// ---------------- 交互 ----------------
async function refresh() {
  const s = await send({ type: 'getState' });
  if (s) { st = s; renderState(); renderParams(); }
}

async function togglePower() {
  if (busy) return;
  busy = true;
  $('power').disabled = true;
  try {
    if (st.active && st.tabId === myTabId) {
      const s = await send({ type: 'stop' });
      if (s) st = s;
    } else {
      const s = await send({ type: 'start', tabId: myTabId });
      if (s) st = s;
    }
    renderState();
  } finally {
    busy = false;
    $('power').disabled = false;
  }
}

let patchTimer = null;
let pendingPatch = {};

function queuePatch(patch) {
  Object.assign(pendingPatch, patch);
  if (patchTimer) return;
  patchTimer = setTimeout(async () => {
    const p = pendingPatch;
    pendingPatch = {};
    patchTimer = null;
    const s = await send({ type: 'setParams', patch: p });
    if (s) { st = s; renderState(); }
  }, 60);
}

function wire() {
  $('power').addEventListener('click', togglePower);
  $('retarget').addEventListener('click', async () => {
    if (busy) return;
    busy = true;
    try {
      const s = await send({ type: 'start', tabId: myTabId });
      if (s) st = s;
      renderState();
    } finally { busy = false; }
  });

  // 预设
  const wrap = $('presets');
  BM_PRESETS.forEach((p, i) => {
    const b = document.createElement('button');
    b.className = 'preset';
    b.type = 'button';
    b.textContent = p.name;
    b.title = p.diff;
    b.addEventListener('click', () => {
      st.params.preset = i;
      renderParams();
      queuePatch({ preset: i });
    });
    wrap.appendChild(b);
  });

  const bind = (id, key, fn) => {
    const el = $(id);
    el.addEventListener('pointerdown', () => { dragging = true; });
    el.addEventListener('pointerup', () => { dragging = false; });
    el.addEventListener('pointercancel', () => { dragging = false; });
    el.addEventListener('input', () => {
      const v = parseFloat(el.value);
      const val = fn(v);
      // makeupDb = 0 在算法里表示"用默认 2.5 dB", 界面上要说清楚
      if (id === 'makeup' && v === 0) $('v-makeup').textContent = '+2.5 dB (默认)';
      else if (id === 'drive') $('v-drive').textContent = v.toFixed(1);
      else if (id === 'bass') $('v-bass').textContent = fmtDb(v);
      else if (id === 'treble') $('v-treble').textContent = fmtDb(v);
      else if (id === 'makeup') $('v-makeup').textContent = fmtDb(v);
      else if (id === 'trim1k') $('v-trim1k').textContent = fmtDb(v);
      setFill(el);
      st.params[key] = val;
      queuePatch({ [key]: val });
    });
  };

  bind('drive', 'drive', (v) => v / 10);
  bind('bass', 'bass', (v) => v);
  bind('treble', 'trim9k', (v) => v);
  bind('makeup', 'makeupDb', (v) => v);
  bind('trim1k', 'trim1k', (v) => v);

  $('reset').addEventListener('click', async () => {
    st.params = Object.assign({}, BM_DEFAULT_PARAMS, { makeupDb: 0 });
    renderParams();
    const s = await send({ type: 'setParams', patch: st.params });
    if (s) { st = s; renderState(); }
  });
}

chrome.runtime.onMessage.addListener((msg) => {
  if (!msg) return;
  if (msg.target === 'popup' && msg.type === 'meter') renderMeter(msg);
});

(async () => {
  const tabs = await chrome.tabs.query({ active: true, currentWindow: true });
  myTabId = tabs && tabs[0] ? tabs[0].id : null;
  await refresh();
  wire();
  // 后台状态可能被外部改变 (标签页关闭 / 流中断), 定时对齐
  setInterval(() => { if (!dragging) refresh(); }, 1500);
})();
