# make_preview.py — 把 popup.html 转成可在普通浏览器里看的静态预览 (桩掉 chrome API)
import re, io

html = io.open('../popup.html', encoding='utf-8').read()
html = html.replace('href="popup.css"', 'href="../popup.css"')
html = html.replace('src="common.js"', 'src="../common.js"')
html = html.replace('src="popup.js"', 'src="../popup.js"')

stub = '''<script>
// 预览桩: 模拟"已接管"状态与电平表
const _state = {
  active: true, tabId: 1, error: null, enabled: true,
  params: { drive: 0.85, bass: 3, preset: 4, makeupDb: 0, trim9k: 2, trim1k: 0 }
};
window.chrome = {
  runtime: {
    sendMessage: (m) => Promise.resolve(
      m.type === 'getState' ? _state : _state
    ),
    onMessage: { addListener: (fn) => { window.__push = fn; } }
  },
  tabs: { query: () => Promise.resolve([{ id: 1 }]) },
  storage: { local: { get: () => Promise.resolve({}), set: () => {} } }
};
</script>
'''

html = html.replace('<script src="../common.js"></script>', stub + '<script src="../common.js"></script>')

# 表头数值: 由 popup.js 首帧填 0, 这里塞一个"运行中"的瞬时值, 只影响截图观感
html = html.replace('</body>', '''<script>
setTimeout(() => {
  if (window.__push) window.__push({ target: 'popup', type: 'meter',
    lvl: [-14, -22, -30], gain: [9.4, 14.1, 6.8], lim: 0.86 });
}, 300);
</script>
</body>''')

io.open('preview.html', 'w', encoding='utf-8').write(html)
print('preview.html written')
