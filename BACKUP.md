# BassMax 备份说明（2026-11-02 定稿）

> 用户试听结论：**"效果好了很多 已经非常接近 OctIMAX"**、**"之前测试曲目效果不错"**、
> **"高音加+3效果不错"**、**"慢释放确实更好"**、**"很好"**（双图标 + 右键开关）。
> 本快照为测试基线：核心参数、界面、托盘交互均已定稿，请勿随意改动 `bassmax_core.hpp` 的标定参数。

## 一、版本标识

| 项目 | 值 |
|---|---|
| DSP 版本 | v4（简化拉伸模型）+ 2026-11-03 高采样率修复 |
| DspVer（注册表） | 3（未变，不影响现有用户参数） |
| 编译产物 | `bassmax_dshow/bassmax_dshow.ax`（179,200 字节，2026-11-03 01:28） |
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
   “应用最大值后还不到 −0.1 dB 的也就不再提升”—— 即 CAP 上限（用户原话，指 −0.1 dBFS）。
2. **电平检测**：低频用峰值跟随（5 ms / 200 ms），避免 40–60 Hz 逐周期起伏；
   中高频用 50 ms 能量（RMS）平滑。
3. **增益平滑**：收 30 ms、放 **400 ms** —— 专治“波浪起伏 / 不平稳”。
   2026-11-02 用户试听定稿：250 ms 在吉他连续分解和弦（风雨无阻）下仍有轻微起伏，400 ms 明显更稳且不发闷。
4. **末端**：慢速峰值归一化（50 ms / 1 s，目标 0.98）+ 软削波 knee 0.93 + 硬限幅 0.99。
   旧的“全带硬限幅压 6 dB”是“突发低音糊掉”的根因，已废弃；改为段内饱和。

### 分段参数

| 段 | 分频 | 斜率 S | 目标 T | 上限 C | 段内饱和上限 |
|---|---|---|---|---|---|
| LO | < 120 Hz | 0.50 | −1.0 dB | +17.5 dB | 0.82 |
| MID | 120–2500 Hz | 0.80 | −12.5 dB | +27.0 dB | 0.75 |
| HI | > 2500 Hz | 0.50 | −12.5 dB | +28.0 dB | 0.67 |

**高频段独立时间常数**（2026-11-02 加）：`ENV_HI=10ms`、`G_ATT_HI=20ms`、`G_REL_HI=150ms`。
低频与中频仍用 400 ms 释放（吉他弦音稳定靠这个），只有高频段快收快放。原因：高频瞬态短、
混响尾巴长，400 ms 释放会把尾巴整体托住，听感就是「回音变重」。实测电子乐（Aqua）高频
2.5k+ 瞬态后 80 ms 衰减：慢释放 5.8 dB → 快释放 10.9 dB（原版音效 12.9，原版 9.1）。

时间常数：`ENV_MS=50`、`LO_ATT=5 / LO_REL=200`、`G_ATT=30 / G_REL=400`、
`LIM_LOOK=5 / LIM_ATT=0.3 / LIM_REL=90`、`CEILING=0.99`、`SOFT_KNEE=0.93`、`MAKEUP_DB=2.5`。

## 三、用户界面

- **总开关**（2026-11-02 补）：勾选才处理；未勾选时整条链路画到旁路，逐样本原样输出，其余控件置灰但参数保留。
- **驱动**：0–10 共 11 档（每档 0.1）。**驱动 = 0 时逐样本完全旁路，无任何染色**（已验证最大误差 1.2e-7）。
- **低音**：−12..+12 dB，100 Hz 低架。
- **高音**（2026-11-02 补）：−12..+12 dB，9 kHz 高架。给「高音不够 / 空气感不足」一个手动出口，默认 +2。
  **必须做成后置 EQ**（放在三段合成之后、限幅之前），不能加进预设 EQ 里——加前面会抬高高频段的检测电平，
  压缩器跟着改高频增益，用户会听到「波浪起伏」回来。当前实测：+3 只改 9k 以上（16k +3.3dB、12.5k +2.3dB），
  2.5k–9k 仅 +0.16dB，低+中 −0.14dB，峰值仍 0.979（−0.19 dBFS）。
- **6 个 EQ 预设**：标准乐 / 古典乐 / 俱乐部 / 流行乐 / **爵士乐** / 摇滚乐。
  `preset4 = 爵士乐 = {+3.0, +1.5, −1.0, +2.0, +3.5}`，三首参考音逐条实测最接近（谱差 0.74 dB），
  **不要再和 preset5 对调**——2026-11-02 曾误对调，用户当场听出「电话音效」（preset5 在 200–630 Hz 有 −3~−4 dB 坑）。
- **定稿默认值**（2026-11-02）：爵士乐 + 低音 +2 dB + 高音 +2 dB + 驱动 100%，总开关开。
  通过 `DspVer` 4 一次性迁移写入注册表；旧配置（DspVer<4）会被这套值覆盖。
- **托盘交互**（2026-11-02 定稿）：**左键单击** = 打开/收起设置窗口；**右键单击** = 直接切总开关
  （开启 <-> 关闭，参数全部保留）。关闭时托盘图标变灰，开启时是彩色，一眼可辨。
  设置窗口里的勾选框、属性页、右键开关三处改动都会同步托盘图标（窗口内即时刷新；
  外部改动由托盘线程 400ms 轮询注册表兜底）。
- **窗口位置**：弹出时居中于当前显示器工作区（`MONITORINFO::rcWork`，自动避开任务栏），不再落在左上角。
- **图标**：`make_icon.py` 生成两套 `ICO`（16/20/24/32/40/48/64/128/256 九档，每档单独超采样绘制）。
  - `bassmax.ico` = `IDI_APP`（301），开启态：深色圆角底 + 三条递升圆头竖柱（蓝→青），
    最长柱顶一个亮点，对应「拉伸到 −0.1 dBFS 封顶」。
  - `bassmax_off.ico` = `IDI_APP_OFF`（302），关闭态：同形状灰阶，亮点压暗。
  - 16/20 px 自动简化成两根柱，否则小尺寸下三条会糊成一片。

### 注册表项（`HKEY_CURRENT_USER\Software\BassMax`，写入统一 REG_SZ，读取兼容 REG_DWORD）

| 值名 | 当前值 | 范围 | 含义 |
|---|---|---|---|
| `Bass` | `2.0` | −12.0 … +12.0 | 低音，100 Hz 低架（dB），**在预设 EQ 链内，参与电平检测** |
| `Treble` | `2.0` | −12.0 … +12.0 | 高音，9 kHz 高架（dB），**后置 EQ，不进压缩** |
| `Drive` | `1.00` | 0.00 … 1.00 | 驱动（界面 0–100%，共 11 档）；0 = 逐样本旁路 |
| `Preset` | `4` | 0 … 5 | 风格：0 标准乐 / 1 古典乐 / 2 俱乐部 / 3 流行乐 / **4 爵士乐** / 5 摇滚乐 |
| `Enabled` | `1` | 0 / 1 | 总开关；0 时 DSP 参数全部按零处理，逐样本原样输出 |
| `DspVer` | `4` | 内部 | 配置版本。**手动改小会触发参数重写**（<3 驱动归零；<4 整套写回定稿默认） |
| `TrayIcon` | `1` | 0 / 1 | 是否显示托盘图标，默认 1（**首次运行显性写入**）；改 0 后 400ms 内图标消失，改 1 立刻回来 |

> 恢复定稿默认：删掉整个 `BassMax` 键再重启宿主，会自动重建为「爵士 +2 / +2 / 100% / 开启」。
> **写入用 REG_SZ，读取同时认 REG_DWORD**——手改时新建哪种类型都行。
> 播放器在跑时改完 **400ms 内热加载**，不用重开文件；播放器没跑则是下次启动生效。
> 超范围的值会被钳住，不会出错。

## 四、三曲评测（定稿默认：爵士 / 低音+2 / 高音+2 / 驱动 100，2026-11-02 23:44 实测）

| 曲目 | 包络相关 | 响度差 | 峰因（本版 / 原版） | 峰值 |
|---|---|---|---|---|
| Rewrite The Stars | 0.968 | −0.18 dB | 3.53 / 3.54 | 0.979 |
| 风雨无阻 | 0.888 | −0.34 dB | 3.75 / 3.64 | 0.979 |
| Here I Am（极端） | 0.971 | +0.18 dB | 3.69 / 3.86 | 0.979 |

三首峰值均 0.979（−0.19 dBFS），对应原版「限制到 −0.1 dBFS」，差 0.09 dB。
低频活跃度与原版一致（0.95–0.99 倍），说明拉伸出力方向正确。
风雨无阻的包络相关 0.888 是唯一偏低项，原因是高频段独立时间常数（10/150 ms）比全局 400 ms 快，
评估脚本按全带算包络时会把这段算作偏差；用户实际试听认为 400 ms 低频 + 快高频的组合更好。

## 五、已知遗留问题（后续迭代方向）

1. 驱动档位之间的主观一致性只在驱动 100 上标定过，中间档位（1–9）为线性插值，未逐一校准。
2. 极端曲 `Here I Am` 末端限幅仍在较多帧动作（最深约 −6 dB），用户长测未报告可闻问题，暂不动。
3. 电子乐类素材（混响多）的「空旷感」只减轻了一部分：高频段已改快收快放，
   剩下约 1.8 dB 的差异来自拉伸强度本身，属于与原版的取舍，未再压。
4. VST3 版本已暂停（用户明确不需要），`bassmax_vst3/` 仅留骨架，未参与编译。

## 六、目录结构

```
bassmax_core.hpp              DSP 核心（单头文件，无依赖）
bassmax_core_v2/v3.hpp.bak    旧方案留档（不入库）
bassmax.ico / bassmax_off.ico 图标源文件（开启 / 关闭），make_icon.py 生成
icon_compare.png              两套图标并排对比图（文档用）

bassmax_dshow/                DirectShow 滤镜（ax 宿主）
  bassmax_core.hpp            核心副本（编译前从上级复制）
  bassmax_filter.cpp          采样处理与参数下发 / 总开关旁路
  bassmax_settings.cpp/.h     注册表持久化（Bass/Treble/Drive/Preset/Enabled/DspVer）
  bassmax_ui.h                控件读写 + 预设按钮 + 总开关灰化
  bassmax_tray.cpp/.h         托盘线程（左键设置 / 右键开关 / 双图标 / 窗口居中）
  bassmax_page.cpp/.h         滤镜属性页
  bassmax.rc / resource.h     资源（6 预设按钮 + 3 滑块 + 总开关 + 两套图标）
  bassmax.ico / bassmax_off.ico  编进资源的图标副本
  build.sh                    插件编译脚本
  install.bat / uninstall.bat 注册脚本（需右键管理员运行）

render_wav.cpp                离线渲染工具（离线试听/标定用）
dump_gain.cpp                 逐样本导出增益曲线
build_tools.sh                离线工具编译脚本
make_icon.py                  两套 ICO + 对比图生成
metrics.py                    三曲联合评测
fit_simple.py / fit_joint.py / fit_bins.py   线性拉伸模型拟合
pair_io.py / wcut.py          wav 读写、试听片段切分
部署新版本.bat                宿主机运行中也能换 ax（先结束 MPC 再覆盖）
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

### 第二轮清理（2026-11-02 23:44，定稿后）

删除全部测试与临时产物，工程目录 981 MB → 864 MB（剩下 860 MB 全是 10 个素材 wav）：

- `*.pk` 3 个（播放器峰值缓存）
- `试听/` `试听A/` 共 114 MB 的 A/B 对照片段
- `render_wav.exe/.obj`、`dump_gain.exe/.obj`（`bash build_tools.sh` 一条命令重建）
- `bassmax_dshow/obj/`、3 个 `build_*.log`、`.exp`、`.lib`、`设置界面预览.bmp`
- `bassmax_dshow/` 里 9 个旧测试程序源码（`test_com/test_formats/test_props/test_reg/test_reg2/test_shot/test_transform/test_tray/unreg.cpp`）
- 12 个诊断脚本（`an_*.py`、`sweep_*.py`、`mk_probe.py`）——git 历史里都有，
  `git checkout <提交> -- 文件名` 可单取
- 6 个图标预览 png 精简为 `icon_compare.png` 一张（`python make_icon.py` 重新生成）
- `base_core.hpp`（与 `bassmax_core.hpp` 完全相同的残留副本）

**保留**：10 个素材 wav、原版 OctIMAX 本体（`单文件版/` `多文件版/`）、全部源码与 `bassmax_dshow.ax`、
标定与评测脚本、`bassmax_core_v2/v3.hpp.bak`、`部署新版本.bat`。

**重建方式**：

```
bash build_tools.sh                          # render_wav.exe / dump_gain.exe
cd bassmax_dshow && cp ../bassmax_core.hpp . && bash build.sh   # bassmax_dshow.ax
python wcut.py                               # 重新切试听片段
python metrics.py                            # 三曲指标
```

## 九、git 与快照

本目录已初始化为 git 仓库，首个提交 `84752b0` 是最初的 v4 基线，此后每次调参单独提交。
音频素材、CSV、中间产物、构建缓存均已忽略（见 `.gitignore`）。

**定稿快照**：`C://Users//Administrator//Desktop//BassMax_定稿_2026-11-02//`（另有同名 zip，21 MB）

```
bin\  bassmax_dshow.ax            定稿产物（172,544 字节，MD5 a31ae3d0…）
       bassmax_dshow_已部署.ax    从 MyMPC\Codecs 取回的一份，两者 MD5 一致
       bassmax.ico / bassmax_off.ico / icon_compare.png
source\ 完整源码（等价于 git archive 定稿提交）
试听样本\ 三首开头 20 秒，按定稿默认值渲染（风雨无阻 / Hotel California / Aqua）
备份说明.md  即本文档
```

已实测该校验：把 `source\` 单独拷到临时目录，`bash build_tools.sh` 与
`cd bassmax_dshow && bash build.sh` 都一次通过，产出 `bassmax_dshow.ax` 与快照里那份
**大小相同、仅 4 个字节不同**（PE 链接时间戳/校验和），说明备份自洽、不依赖本机残留。

**回退方式**（在工作目录里执行）：

```
git checkout -- .              # 只丢弃未提交的改动
git reset --hard v5-定稿       # 彻底回到定稿（tag 指向本快照对应的提交）
git log --oneline              # 看历史，挑任意提交
```

---

## 十、高采样率 / DSD 修复（2026-11-03）

**症状**：DSD 播放断断续续；修掉卡顿后音质又极差（用户描述"比 128K 的 MP3 还差"）。
44.1k/48k 用户无任何异常，问题只在 176.4k 以上暴露。

### 10.1 限幅器复杂度 O(sr²) → O(1)

`Limiter::run()` 每个样本线性扫描整窗，窗口 `W = 5ms × sr`，每秒开销随采样率平方增长。

改为**单调双端队列**求滑动窗口最大值，摊还 O(1)。窗口长度、取值、平滑系数全部不变，
因此输出与线性扫描**逐位相同**（`limiter_eq.cpp` 覆盖 7 采样率 × 4 信号形态 = 28/28 一致）。

### 10.2 DC 阻断器系数随采样率漂移

原代码 `dcy = x - dcx + 0.995 * dcy` 硬编码 0.995，截止频率 = `0.000796 × sr`：

| 采样率 | 实际截止 |
|---|---|
| 44.1k | 35 Hz（设计值） |
| 352.8k | 281 Hz |
| 705.6k | 562 Hz |

高采样率下低音被当直流滤掉。改为 `dcR = powf(0.995f, 44100.0f / sr)`，时间常数与采样率无关。
44.1k 下 `powf(x, 1) == x`，基准音色不变。

### 10.3 DSD 超声噪声喂进非线性段

DSD 噪声整形把量化噪声推到 20kHz 以上。实测 `Wham! - Careless Whisper.dsf` 的 1-bit 码流：
超声段（20k–100k）总能量比可听段（0–20k）**高 9.1 dB**，60–100k 处几乎与音乐同响。

这些能量进 tanh 段内饱和器会互调出可听失真，电平检测器也被顶满、把高音段整段压下去。
OctIMAX 是纯线性 EQ，超声噪声原样穿过，所以它"没事"。

修法：`process` 入口串 4 级二阶（8 阶巴特沃斯）低通，截止 20kHz，**仅 sr ≥ 176400 时启用**。
44.1k/48k/88.2k/96k 完全不进入该路径。

### 10.4 验收数据

以 `Wham! - Careless Whisper.dsf`（ffmpeg 解出 352800 Hz）为准，相对 44.1k 参考链的频段偏差：

| 频段 | 修复后 352.8k | 修复前 352.8k |
|---|---|---|
| 40–80 Hz | -0.6 dB | **-10.3 dB** |
| 80–160 Hz | -0.1 dB | -4.4 dB |
| 2.5–5 kHz | 0.0 dB | -3.9 dB |
| 5–10 kHz | +0.2 dB | **-8.5 dB** |
| 10–16 kHz | -0.3 dB | **-10.2 dB** |

实时倍率（preset 4 / drive 1.0）：

| 采样率 | 修复前 | 修复后 | 提升 |
|---|---|---|---|
| 44100 | 50.5x | 213.0x | 4.2× |
| 48000 | 44.0x | 200.7x | 4.6× |
| 96000 | 11.9x | 99.4x | 8.4× |
| 176400 | 3.7x | 51.0x | 13.8× |
| 352800 | 0.95x | 25.8x | 27× |
| 705600 | 0.24x | 13.1x | 54× |

**44.1k 兼容性**：与定稿版核心在同一参数下渲染 30 秒，输出**逐位相同**
（`3b6b28a9dce6dcf0040b9a8fb461e66f`），已有用户听感零变化。
（这是本节那一版的状态；之后第十一节的 double 系数改造带来 ≤0.007 dB 的频段平衡差异，
已单独量化。）

**新增工具**（保留在仓库）：`bench.cpp`（性能回归）、`limiter_eq.cpp`（限幅器等价性）。

---

## 十一、DSD256 支持 + 滤波器系数精度（2026-11-03 追加）

### 11.1 采样率上限 768,000 → 1,536,000

`bassmax_filter.cpp` 的 `ParseFmt` 里只有一个上限判断，改这一处就能覆盖 DSD256
（5,644,800/4 = 1,411,200 Hz）。缓冲是按需 `resize`，没有定长数组，不存在溢出点。

**活体验证**（`pin_test.cpp`：`LoadLibrary` + `DllGetClassObject` + `IPin::QueryAccept`，
走的正是播放器协商时那条 `CheckInputType → ParseFmt` 路径）：

| 采样率 | 结果 |
|---|---|
| 44.1k / 96k / 192k / 352.8k / 705.6k | 接受 |
| **1,411,200（DSD256）** | **接受** |
| 1,536,000（新上限） | 接受 |
| 2,822,400（DSD512） | 拒绝 `S_FALSE` |

性能 5.26x 实时（`bench.exe`），10 秒满刻度超声噪声无 NaN/失控。

### 11.2 真正的坑：float 系数在极低归一化频率下失效

只改上限是不够的。分频低点 120 Hz 在 1,411,200 Hz 下归一化频率只有 **8.5e-5**，
`make_butter_lp` 用 float 算 `cos(w0)` 时，`1-cw` 只剩 1~2 位有效数字——**系数本身就错了**。

`biquad_prec.cpp` 实测（120 Hz 低通，同一组系数）：

| 频率 | float 运算 | 理想值 | 误差 |
|---|---|---|---|
| 40 Hz @1.41M | -1.73 dB | -0.05 dB | **1.7 dB** |
| 120 Hz @1.41M | -4.52 dB | -3.01 dB | 1.5 dB |
| 40 Hz @705.6k | +0.84 dB | -0.05 dB | 0.9 dB |

注意：即使用 double 累加也没用（同表第二列），因为**误差在系数生成阶段就产生了**。
这类问题在小波器书上属于经典陷阱——只要 `f0/sr` 太小，float 的 `1-cos(w0)` 就废了。

**修法**：`Biquad` 的系数与状态、以及全部 `make_*` 系数生成函数改为 double。
44.1k 输出相对上一版变化 **max|Δ| = 1.7e-3（-45 dBFS 峰值）、rms 差 -61.7 dB**，
且差异能量 93% 落在 20–160 Hz——各频段平衡变化 **≤0.007 dB**，等同不可闻。

### 11.3 结果

- **1,411,200 与 705,600 的频段平衡差 ≤0.05 dB**（修复前 1.41M 在 80–160 Hz 掉 2.4 dB）
- 与 44.1k 相比残留 ≤1.65 dB 的偏亮，来源是升采样后**峰值因数**变化（0.99 → 1.16）
  导致分段检测器读数不同，是动态模型固有行为，不是采样率缺陷
- 44.1k 性能 213x → 184x（double 的代价约 15%），1.41M 5.26x

**新增工具**：`pin_test.cpp`（活体格式协商）、`us_test.cpp`（超声链响应+稳定性）、
`biquad_prec.cpp`（系数精度对照）、`dsd256_check.py`（DSD256 端到端音质对照）。

**已知边界**：上限 1,536,000，DSD512（2,822,400）仍会被拒。

---

## 十二、独立设置程序 + 注册表显性化 / 热加载（2026-11-03 追加）

### 12.1 原来的问题

1. `TrayIcon` 只在初始化时**被读取**，从来不写：注册表里根本没这个值（实测 `GetValueNames()`
   只有 Bass / Drive / DspVer / Preset / Enabled / Treble），想改也无从下手。
2. 托盘线程在 `BassMaxFilter` 构造函数里按 `TrayEnabled()` 决定启不启动，
   **播放器不启动就完全没有入口**调设置。
3. 参数只在滤镜构造时读一次，之后改注册表要等下次开文件才生效。

### 12.2 改法

**a) 六项值全部显性落盘。** `InitOnce` 里带 `KEY_WRITE` 打开键，读完后若发现缺项
（或触发 DspVer 迁移）就整套 `SaveTo()` 写回，`TrayIcon` 从此固定存在。

**b) 读值同时接受 REG_SZ 与 REG_DWORD。** `ReadNum()` 两种类型都认，
手改注册表时新建"字符串值"或"DWORD 值"都能生效，不必猜类型。

**c) 托盘线程常驻 + 400ms 轮询。**
- 构造函数无条件 `StartTray()`（不再按开关跳过），图标是否出现交给线程判断；
- 定时器里 `ReloadFromRegistry()` → 有任何一项变化就递增变更代号 → 音频线程下一拍 `ApplySettings()`；
- 同一拍里按 `TrayEnabled()` 调 `ShowIcon()` / `HideIcon()`，所以 `TrayIcon` 0↔1 **实时**生效；
- 顺带补了 `TaskbarCreated` 广播处理：资源管理器重启后图标自动重新挂上。

**d) 独立设置程序 `BassMaxConfig.exe`（x64，46,080 字节）。**
UI 不重写一份——`.ax` 导出 `BassMaxConfig`（`extern "C" __declspec(dllexport)`，`CALLBACK` 签名，
rundll32 兼容），EXE 只负责找 `.ax`（自身目录 → `%USERPROFILE%\MyMPC\Codecs` → `HKCR\CLSID\...\InprocServer32`）
然后 `LoadLibrary` + 调用。导出函数内部自建消息循环，关窗即返回。

**e) 顺带修的坑：** `build.sh` 里 `.ax` 链接用 `obj/*.obj` 通配符，
所以 EXE 的目标文件必须放 `obj/exe/`，否则会被一起链进 DLL。
另外链接器需要 `mt.exe` 嵌入清单，而本机 SDK 的 bin 不在 PATH —— 改成资源脚本里
`1 24 "config.manifest"` + `/MANIFEST:NO`，绕开 mt.exe。

### 12.3 验收

`settings_test.cpp`（与 `bassmax_settings.cpp` 一起编译，16 项全过）：

| 用例 | 结果 |
|---|---|
| 初始化后六项值都存在于注册表 | PASS |
| 外部改 `Bass` 字符串 → `ReloadFromRegistry` 代号+1、值变 5.5 | PASS |
| 外部用 DWORD 写 `Bass=7` → 同样识别 | PASS |
| `TrayIcon=0` → `TrayEnabled()` false；回 1 → true | PASS |
| 删掉某一项时本次不采纳（代号不变，避免刷成默认） | PASS |
| 删掉 `TrayIcon` 后新进程启动自动补回 | PASS（实测 `GetValueNames()` 又出现 `TrayIcon = 1`） |
| 超范围 `Bass=99` → 钳到 +12 | PASS |

活体验收：
- `BassMaxConfig.exe` 启动 → 出现窗口标题「BassMax 音频增强」→ `CloseMainWindow` 后进程**正常退出**（消息循环正确收尾）
- 从 `~\MyMPC\Codecs\` 和 `D:\MyMPC_CG_x86_x64\3.Core\1.Core\` 两处启动均正常
- `TrayIcon=0` 状态下创建滤镜（pin_test）**不崩不挂**，退出码 0
- 三处副本（源码产物 / 已安装 / D 盘母版）`.text` 段 SHA256 一致：`7a64d5d4ca469371…`（63,488 B）
- `.ax` 导出表新增 `BassMaxConfig`（ordinal 1），其余四个导出不变
- `BassMaxConfig.exe`：`machine=0x8664`、`subsystem=2 (Windows GUI)`、清单已嵌入（comctl32 v6 + permonitorv2 + asInvoker）

**没动 DSP**：这次只碰设置层与 UI 入口，`bassmax_core.hpp` 一字未改，音色零变化。
