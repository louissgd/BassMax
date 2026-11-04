// background.js — 状态中枢: 负责请求 tabCapture 流 ID、拉起 offscreen 文档、持久化参数
'use strict';

importScripts('common.js');

const OFFSCREEN_URL = 'offscreen.html';

/** @type {{active:boolean, tabId:number|null, error:string|null}} */
let state = { active: false, tabId: null, error: null };
let params = Object.assign({}, BM_DEFAULT_PARAMS);
let enabled = true;
let creating = null;

const ready = (async () => {
  try {
    const st = await chrome.storage.local.get(['params', 'enabled']);
    if (st.params) params = bmSanitize(Object.assign(params, st.params));
    if (typeof st.enabled === 'boolean') enabled = st.enabled;
  } catch (e) { /* 首次运行 */ }
})();

function persist() {
  chrome.storage.local.set({ params, enabled }).catch(() => {});
}

async function hasOffscreen() {
  if (chrome.offscreen && chrome.offscreen.hasDocument) {
    try { return await chrome.offscreen.hasDocument(); } catch (e) { return false; }
  }
  return false;
}

async function ensureOffscreen() {
  if (await hasOffscreen()) return;
  if (creating) { await creating; return; }
  creating = (async () => {
    try {
      await chrome.offscreen.createDocument({
        url: OFFSCREEN_URL,
        reasons: ['USER_MEDIA', 'AUDIO_PLAYBACK'],
        justification: '捕获标签页音频, 经 BassMax DSP 处理后播放',
      });
    } catch (e) {
      const msg = String(e && e.message || e);
      if (!/single offscreen/i.test(msg)) throw e;   // 已经存在就当作成功
    }
  })();
  try { await creating; } finally { creating = null; }
}

function toOffscreen(msg) {
  return chrome.runtime.sendMessage(Object.assign({ target: 'offscreen' }, msg));
}

// 离屏文档刚创建时监听器可能还没挂上, 这里重试几次
async function toOffscreenRetry(msg, tries = 8) {
  let lastErr = null;
  for (let i = 0; i < tries; i++) {
    try {
      const r = await toOffscreen(msg);
      if (r) return r;
      lastErr = new Error('离屏文档无响应');
    } catch (e) {
      lastErr = e;
    }
    await new Promise((res) => setTimeout(res, 120));
  }
  throw lastErr || new Error('离屏文档无响应');
}

async function startCapture(tabId) {
  await ready;
  if (state.active) await stopCapture();
  state.error = null;
  try {
    await ensureOffscreen();
    const streamId = await chrome.tabCapture.getMediaStreamId({ targetTabId: tabId });
    if (!streamId) throw new Error('拿不到标签页音频流 (请先在该标签页点击扩展图标)');
    const resp = await toOffscreenRetry({
      type: 'start', streamId, params, enabled,
    });
    if (!resp || !resp.ok) throw new Error(resp && resp.error || '音频管线启动失败');
    state.active = true;
    state.tabId = tabId;
  } catch (e) {
    state.active = false;
    state.tabId = null;
    state.error = String(e && e.message || e);
    await safeCloseOffscreen();
  }
  return getState();
}

async function safeCloseOffscreen() {
  try { if (await hasOffscreen()) await chrome.offscreen.closeDocument(); } catch (e) {}
}

async function stopCapture() {
  try { await toOffscreen({ type: 'stop' }); } catch (e) {}
  await safeCloseOffscreen();
  state.active = false;
  state.tabId = null;
  return getState();
}

function getState() {
  return { active: state.active, tabId: state.tabId, error: state.error, params, enabled };
}

async function pushParams() {
  if (!state.active) return;
  try { await toOffscreen({ type: 'params', params, enabled }); } catch (e) {}
}

chrome.runtime.onMessage.addListener((msg, sender, sendResponse) => {
  if (!msg || msg.target !== 'bg') return;

  (async () => {
    await ready;
    switch (msg.type) {
      case 'getState':
        sendResponse(getState());
        break;

      case 'start':
        sendResponse(await startCapture(msg.tabId));
        break;

      case 'stop':
        sendResponse(await stopCapture());
        break;

      case 'setParams': {
        params = bmSanitize(Object.assign({}, params, msg.patch || {}));
        if (typeof msg.enabled === 'boolean') enabled = msg.enabled;
        persist();
        pushParams();
        sendResponse(getState());
        break;
      }

      case 'ended':
        state.active = false;
        state.tabId = null;
        state.error = '标签页音频已中断';
        await safeCloseOffscreen();
        sendResponse(getState());
        break;

      default:
        sendResponse({ ok: false, error: 'unknown message' });
    }
  })();

  return true;   // 异步响应
});

// 被接管的标签页关掉 -> 收尾
chrome.tabs.onRemoved.addListener(async (tabId) => {
  if (state.active && state.tabId === tabId) {
    await stopCapture();
  }
});

// 标签页导航/刷新 -> tabCapture 流会断, 流结束事件由 offscreen 上报; 这里兜底
chrome.tabs.onUpdated.addListener(async (tabId, info) => {
  if (state.active && state.tabId === tabId && info.status === 'loading') {
    state.active = false;
    state.tabId = null;
    await safeCloseOffscreen();
  }
});
