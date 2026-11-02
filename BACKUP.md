# BassMax v4 备份说明（2026-11-02）

> 用户试听结论：**"效果好了很多 已经非常接近 OctIMAX"**。
> 本快照为多日测试基线，测试期间请勿改动 `bassmax_core.hpp` 的标定参数。

## 一、版本标识

| 项目 | 值 |
|---|---|
| DSP 版本 | v4（简化拉伸模型） |
| DspVer（注册表） | 3 |
| 编译产物 | `bassmax_dshow/bassmax_dshow.ax`（104,960 字节，2026-11-02 10:18） |
| 部署路径 | `C:\Users\Administrator\MyMPC\Codecs\bassmax_dshow.ax` |
| 基线对照 | 原版 OctIMAX，**驱动 100**（= 驱动滑块满格） |

## 二、算法核心（v4）

每样本流程：

```
DC 阻断 → 三段分频 LR4 → 各段电平检测 → 线性拉伸 → 增益平滑
        → 段内 tanh 饱和 → 合并 × make-up → 慢速归一化 → 软削波 → 硬限幅兜底
```

1. **线性拉伸**（用户提出的模型，替代旧的 10 点曲线表 + 段预算）：
   ```cpp
   gain = clamp(S * (T - level), FLOOR, CAP)
   ```
   电平低于目标 T 就抬，抬到 CAP 封顶；已经高于 T 的反而压回 T。
   “应用最大值后还不到 0.1 的也就不再提升”—— 即 CAP 上限。
2. **电平检测**：低频用峰值跟随（5 ms / 200 ms），避免 40–60 Hz 逐周期起伏；
   中高频用 50 ms 能量（RMS）平滑。
3. **增益平滑**：收 30 ms、放 250 ms —— 专治“波浪起伏 / 不平稳”。
4. **末端**：慢速峰值归一化（50 ms / 1 s，目标 0.98）+ 软削波 knee 0.93 + 硬限幅 0.99。
   旧的“全带硬限幅压 6 dB”是“突发低音糊掉”的根因，已废弃；改为段内饱和。

### 分段参数

| 段 | 分频 | 斜率 S | 目标 T | 上限 C | 段内饱和上限 |
|---|---|---|---|---|---|
| LO | < 120 Hz | 0.50 | −1.0 dB | +17.5 dB | 0.82 |
| MID | 120–2500 Hz | 0.80 | −12.5 dB | +27.0 dB | 0.75 |
| HI | > 2500 Hz | 0.50 | −12.5 dB | +28.0 dB | 0.67 |

时间常数：`ENV_MS=50`、`LO_ATT=5 / LO_REL=200`、`G_ATT=30 / G_REL=250`、
`LIM_LOOK=5 / LIM_ATT=0.3 / LIM_REL=90`、`CEILING=0.99`、`SOFT_KNEE=0.93`、`MAKEUP_DB=2.5`。

## 三、用户界面

- **总开关**（2026-11-02 补）：勾选才处理；未勾选时整条链路画到旁路，逐样本原样输出，其余控件置灰但参数保留。
- **驱动**：0–10 共 11 档（每档 0.1）。**驱动 = 0 时逐样本完全旁路，无任何染色**（已验证最大误差 1.2e-7）。
- **低音**：−12..+12 dB，100 Hz 低架。
- **高音**（2026-11-02 补）：−12..+12 dB，9 kHz 高架。给「高音不够 / 空气感不足」一个手动出口，默认 0。
  **必须做成后置 EQ**（放在三段合成之后、限幅之前），不能加进预设 EQ 里——加前面会抬高高频段的检测电平，
  压缩器跟着改高频增益，用户会听到「波浪起伏」回来。当前实测：+3 只改 9k 以上（16k +3.3dB、12.5k +2.3dB），
  2.5k–9k 仅 +0.16dB，低+中 −0.14dB，峰值仍 0.979（−0.19 dBFS）。
- **6 个 EQ 预设**：标准乐 / 古典乐 / 俱乐部 / 流行乐 / **爵士乐** / 摇滚乐。
  `preset4 = 爵士乐 = {+3.0, +1.5, −1.0, +2.0, +3.5}`，三首参考音逐条实测最接近（谱差 0.74 dB），
  **不要再和 preset5 对调**——2026-11-02 曾误对调，用户当场听出「电话音效」（preset5 在 200–630 Hz 有 −3~−4 dB 坑）。

## 四、三曲评测（驱动 100）

| 曲目 | 包络相关 | 响度差 | 峰因（本版 / 原版） |
|---|---|---|---|
| Rewrite The Stars | 0.969 | −0.61 dB | 3.74 / 3.54 |
| 风雨无阻 | 0.901 | −0.59 dB | 3.90 / 3.64 |
| Here I Am（极端） | 0.972 | −0.11 dB | 3.85 / 3.86 |

## 五、已知遗留问题（后续迭代方向）

1. 三曲响度均偏原版 0.1–0.6 dB，可考虑补 1 dB。
2. 极端曲 `Here I Am` 末端限幅 41% 帧在动作（最深 −5.9 dB），是否可闻待长测确认。
3. 驱动档位之间的主观一致性只在驱动 100 上标定过，中间档位（1–9）为线性插值，未逐一校准。

## 六、目录结构

```
bassmax_core.hpp              DSP 核心（单头文件，无依赖）
bassmax_dshow/                DirectShow 滤镜（ax 宿主）
  bassmax_core.hpp            核心副本（编译前从上级复制）
  bassmax_filter.cpp          采样处理与参数下发
  bassmax_settings.cpp/.h     注册表持久化（Bass/DspVer/Preset）
  bassmax_ui.h                预设按钮逻辑
  bassmax.rc                  资源（6 按钮 3×2 布局）
  install.bat / uninstall.bat 注册脚本（需右键管理员运行）
render_wav.cpp                离线渲染工具（离线试听/标定用）
dump_gain.cpp                 逐样本导出增益曲线
build_tools.sh                离线工具编译脚本
metrics.py                    三曲联合评测
fit_simple.py                 线性拉伸模型拟合
```

## 七、重新编译

插件（需 MSVC + Windows SDK）：

```bash
cd bassmax_dshow && cp ../bassmax_core.hpp . && bash build.sh
```

离线工具：

```bash
bash build_tools.sh      # 生成 render_wav.exe / dump_gain.exe
```

注册（先彻底退出 MPC-HC 等宿主）：

```
右键 bassmax_dshow/install.bat → 以管理员身份运行
```

## 八、2026-11-02 清理记录

工程目录从 9.8 GB 清到 0.8 GB，删除的都是可再生成的中间产物与一次性脚本。

**保留**（全部是必要的）：

- 8 个原始素材 wav（Rewrite / 风雨无阻 / Bryan / Eagles 的 原始 + 音效配对）
- `单文件版\dsp_OctiMax.dll`、`多文件版\Dsp_omxe.dll / dsp_omxw.dll / vis_omx.dll` —— 原版插件本体，标定基准，不可再生
- 全部源码、`bassmax_dshow.ax`、`bassmax_core_v2/v3.hpp.bak`（旧方案留档）
- 保留脚本：`metrics.py`、`fit_simple.py`、`fit_joint.py`、`fit_bins.py`、`pair_io.py`、`wcut.py`、`an_wave2.py`、`an_bryan.py`
- `fit_simple.json`（当前拉伸参数的实际取值）

**已删**（git 历史里都有，`git checkout 84752b0 -- 文件名` 可随时取回）：

- 149 个中间输出 wav（8.4 GB）：整曲渲染、扫参试听、A/B/C 片段
- 21 个 CSV 追踪日志、3 个 npy、`curve.json` / `dyn.json` / `fit_simple_input.json`
- 38 个一次性探索脚本（旧拟合/调试/分析脚本）
- 6 个 exe + 4 个 obj、`calib/`、`calib_out/`、`measure_out/`、`test_signals/`
- `bassmax_vst3/build_nm/`（290 MB CMake 缓存，VST3 已暂停）

**重建方式**：

```
bash build_tools.sh                          # render_wav.exe / dump_gain.exe
cd bassmax_dshow && cp ../bassmax_core.hpp . && bash build.sh   # bassmax_dshow.ax
python wcut.py                               # 重新切试听片段
python metrics.py                            # 三曲指标
```

## 九、git

本目录已初始化为 git 仓库，首个提交即 v4 测试基线。
音频素材、CSV、中间产物、构建缓存均已忽略（见 `.gitignore`）。
后续每次调参建议单独提交，便于回退对比。
