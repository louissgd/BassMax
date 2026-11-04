# 参与改进

欢迎改进 BassMax。**算法效果、多声道处理、跨平台移植**这三类改动最有价值。

## 一、改哪里

| 想做的事 | 改这个文件 | 需要同步 |
|---|---|---|
| 调 DSP 算法 / 参数映射 / 加预设 | `bassmax_core.hpp`（根目录） | ① 复制到 `bassmax_dshow/bassmax_core.hpp` ② 手工移植到 `bassmax_chrome_ext/worklet/bassmax-worklet.js` |
| 改播放器滤镜行为 / 设置界面 | `bassmax_dshow/*.cpp`, `*.h` | 无 |
| 改扩展 UI / 采集逻辑 | `bassmax_chrome_ext/*.js`, `*.html` | 无 |
| 改 CLI 参数 / 读写格式 | `bassmax_cli/bassmax_cli.cpp` | 无 |

**`bassmax_core.hpp` 是唯一权威实现**，另外两处是它的副本（JS 那份是逐行移植，用 `Math.fround` 对齐 C++ 的 float 精度）。改核心必须三处一起改，否则三个产物听感会不一致。

## 二、构建

需要 **Windows + MSVC**（三个产物都只依赖 Windows SDK，不需要额外库）。

```bash
# DirectShow 滤镜 → bassmax_dshow/bassmax_dshow.ax
cd bassmax_dshow && bash build.sh

# 命令行转码器 → bassmax_cli/dist/bassmax_cli.exe（/MT 静态链接）
cd bassmax_cli && bash build.sh

# Chrome 扩展：无需构建，改完在 chrome://extensions 里点"重新加载"
```

## 三、验证（重要）

改完 DSP 不要只看波形或者靠耳朵，跑一遍逐样本比对：

```bash
# 编译参考渲染器，再把同一份 WAV 分别过 C++ 和 JS，比较样本差
cd bassmax_cli/test && bash build_ref.sh
python compare_wav.py 参考.wav 输出.wav
```

现有三个产物的相对误差基线是 **−97 dB 以内**（约 1 LSB）。如果你的改动让这个数字明显变差，说明有实现偏差，而不是算法改进。

改动直接影响听感时，请附上：

- 改动前后的频谱图或分频段 RMS 对比
- 用了什么素材（**不要提交素材本身**，说明来源即可）
- 主观听感的简短描述

## 四、不要提交这些

`.gitignore` 已经排除，但提 PR 时注意别用 `git add -f` 绕过：

- `*.wav` `*.flac` `*.m4a` —— 测试素材，版权不明，不入库
- `*.ax` `*.exe` `*.dll` `*.zip` —— 产物走 Releases，不进仓库
- `bassmax_cli/dist/`、`bassmax_chrome_ext/dist/`
- 任何商业音频插件的二进制或反编译产物

## 五、提 PR

1. Fork 本仓库
2. 新建分支，名字说清改什么（如 `fix/multichannel-clip`）
3. 提交信息用「类别：内容」格式，例如 `修复：5.1 输入时低频段溢出`
4. 开 Pull Request，说明**改了什么、为什么改、怎么验证的**

如果只是想法还没实现，开 Issue 说清楚现象和你的判断，比直接提一个大 PR 更容易推进。

## 六、已知可改进方向

这些是当前明确的短板，欢迎认领：

- **多声道（>2）处理质量**：现在建议先下混成 2.0，直接处理 5.1 时驱动会自动限到 60%，效果不如 2.0 干净
- **参数感知映射**：`--drive` 的 0–10 分档是测量拟合出来的，不同素材的手感一致性还能再调
- **跨平台**：CLI 和 DSP 核心本身与平台无关，理论上可以在 Linux/macOS 编译（缺少构建脚本和测试）
- **自动化测试**：`test/` 里是手动脚本，没有 CI
