# BassMax — 64 位音频增强器 (Clean-room)

**版本 1.0.3** · 2026-11 · MIT License

一个从零重写的三段分频动态增强器。**一份 DSP 核心驱动三种形态**：

| 形态 | 产物 | 场景 | 速度 |
|---|---|---|---|
| DirectShow 滤镜 | `bassmax_dshow.ax` (184 KB) | MPC-HC / PotPlayer 实时播放 | 44.1k 184× |
| Chrome 扩展 | `bassmax_chrome_v1.0.0.zip` | 浏览器标签页实时处理 | 实时 |
| 命令行转码器 | `bassmax_cli.exe` (283 KB) | 离线批量 / 管道 | 165–210× |

三者共用同一份 `bassmax_core.hpp`，DSP 一行没改。Chrome 版逐行移植到 AudioWorklet，与 C++ 参考实现做过逐样本比对（相对差 −97 ~ −101 dB）；CLI 版直接 `#include` 同一个头文件，与 DirectShow 版输出偏差 ≤1 LSB。

算法来源：对一个 20 年前的 32 位 Winamp DSP 插件（iZotope 早期的 **OctiMax** 音频增强器）做「黑盒测量 → 从零重写」——只用输入/输出音频的测量结果反推行为，独立实现等效链路。产物命名 **BassMax**，与那个商业老插件区分。

原始插件受版权保护（企业作品，中国约 50 年 / 美国 95 年），因此本项目**不包含、不反编译、不复制**原插件的任何代码或二进制。

> 本文是总览与设计说明；逐次改动、测量数据与踩坑记录见 [`BACKUP.md`](BACKUP.md)。
> 各子项目另有详细文档：[DirectShow](bassmax_dshow/) · [Chrome 扩展](bassmax_chrome_ext/README.md) · [CLI](bassmax_cli/README.md)。


---

## 一、目录结构

```
BassMax/
├── README.md                 # 本文件（总览）
├── BACKUP.md                 # 改动记录 / 测量数据 / 踩坑
├── LICENSE                   # MIT
├── NOTICE                    # 第三方代码声明（baseclasses 不受 MIT 覆盖）
├── CONTRIBUTING.md           # 改进指南：改哪里、怎么构建、怎么验证
├── bassmax_core.hpp          # ★ DSP 核心 (header-only, 框架无关) 唯一权威实现
├── render_wav.cpp            # 离线 A/B: 把 WAV 过一遍核心, 与参考输出做统计比对
├── pair_io.py / metrics.py / wcut.py   # A/B 素材切分与指标统计
├── make_icon.py              # 生成 bassmax.ico / bassmax_off.ico
├── build_tools.sh            # 编译上述命令行工具
│
├── bassmax_dshow/            # DirectShow 滤镜（Windows）
│   ├── bassmax_core.hpp      #   核心副本, 构建时用这份
│   ├── bassmax_filter.h/.cpp #   CTransformFilter 子类, 格式协商/PCM↔float, ISpecifyPropertyPages
│   ├── bassmax_page.h/.cpp   #   属性页 (CBasePropertyPage)
│   ├── bassmax_tray.h/.cpp   #   托盘图标 + 设置窗口 + 独立设置入口
│   ├── bassmax_settings.*    #   全局参数 (线程安全 + 注册表持久化 + 热更新代号)
│   ├── bassmax_ui.h          #   属性页/托盘窗口共用的控件读写
│   ├── bassmax.rc / bassmax.manifest   # 对话框、图标、字符串、v6 清单
│   ├── dllmain.cpp           #   COM 入口 + 类厂 + 自注册
│   ├── settings_test.cpp     #   注册表读写回归测试 (29 项)
│   ├── baseclasses/          #   Microsoft DirectShow 基础类 (版权见下)
│   ├── install.bat / uninstall.bat     # 管理员一键注册 / 卸载
│   └── build.sh              #   一键构建 → bassmax_dshow.ax
│
├── bassmax_chrome_ext/       # Chrome / Edge 扩展 (MV3)
│   ├── manifest.json
│   ├── background.js         #   tabCapture → offscreen document
│   ├── offscreen.js          #   AudioContext + AudioWorklet 装配
│   ├── worklet/bassmax-worklet.js   # ★ DSP 移植（逐行对应 core.hpp）
│   ├── popup.html/.css/.js   #   控制面板
│   └── test/                 #   与 C++ 参考的逐样本比对脚本
│
└── bassmax_cli/              # 命令行转码器（Windows）
    ├── bassmax_cli.cpp       #   WAV / 管道读写, 流式处理
    ├── bmx.cmd / bmrun.cmd   #   ffmpeg 包装脚本
    ├── build.sh              #   MSVC /MT 静态链接（无 VC 运行库依赖）
    └── test/                 #   与 C++ 参考的比对脚本
```

> **`baseclasses/` 的版权**：这是 Microsoft DirectShow Base Classes，源自 Windows SDK 样例，文件头保留原始版权声明，**未做修改**。它按 SDK 条款随项目分发，仅用于构建本滤镜；本项目的 MIT 许可不覆盖这部分代码。

---

## 二、DSP 核心 (`bassmax_core.hpp`)

### 2.1 为什么是「动态」而不是「均衡 + 限幅」

v1 是「静态 EQ → 前瞻限幅」，和原插件差在**方向**上：v1 是"声音太大才压下来"，原插件是"声音太小就抬上去"。用同一首歌的「原始 / 原插件处理后」两个 WAV 做配对测量（48k/24bit、样本对齐、零延迟），结论很明确：

| 输入电平 | 原插件总增益 | v1（旧） |
|---|---|---|
| −50 dBFS | +22.2 dB | 恒定 |
| −40 dBFS | +18.7 dB | 恒定 |
| −30 dBFS | +14.6 dB | 恒定 |
| −18 dBFS | +9.8 dB | 恒定 |

也就是说：**安静段抬 20 dB 以上，大声段只抬 10 dB**，约 2.2:1 的向上压缩；峰值统一被限到 1.0（只有 40 个样本触顶、无削顶平台）。所以"小音量乐器突然很清楚"而"响的时候不炸"。

频段层面（相对 400–800 Hz）：安静段低频 +5 dB、10 kHz 以上 +3 dB，大声段回落 —— 这就是「低音增强不闷」的原因：低频增益随低频能量反向变化。

### 2.2 现行链路

```
输入 → DC 阻断 → 三段分频 (LP 120Hz / HP 2.5kHz, 互补相加 == 原信号)
     → 低频段: 低音旋钮 + 动态低音(安静时最多 +6dB)
     → 高频段: 动态空气感(安静时最多 +3dB)
     → 宽带向上压缩 (0.52 × max(0, −15dB − 电平), 上限 20dB) + 响度补偿 (驱动 × 13.4dB)
     → 前瞻限幅 (6ms 前瞻, 0.7ms 攻, 120ms 放, ceiling 0.985) + 软保护
```

- 检测器：立体声**平均**整流 + 8ms/150ms 跟随，左右联动（声像不飘）
- 分频互补重构：`mid = x − low − high`，三路增益相同时逐样本等于输入 —— 所以**驱动 = 0 时就是纯低音均衡**，加了分频也不变味
- 全部系数按采样率现算，8k…768k 都成立
- 限幅器共享一路增益，左右声道同步压放，不会把立体声拉开

### 2.3 与参考的实测比对（整首 281 秒，驱动 85%）

| 指标 | BassMax v2 | 原插件 |
|---|---|---|
| 长时 RMS | −10.87 dBFS | −10.54 dBFS |
| 峰值因数 | 3.40 | 3.37 |
| 0.5s 响度轨迹相关 | 0.90 | — |
| 触顶样本 (≥0.999) | 0 | 40 |
| 频段平衡偏差 | < 1.7 dB (1/3 oct) | — |

## 三、DirectShow 滤镜 (MPC-HC / PotPlayer)

### 构建
```bash
cd bassmax_dshow
bash build.sh          # 需要 baseclasses/ , Windows SDK 10, MSVC 2022
# 产物: bassmax_dshow.ax
```
要点：编译带 `/utf-8`（源码含中文注释）；链接用 `/LD` 并 `LINK /EXPORT:...` 显式导出四个 COM 入口；
仅排除 baseclasses 的 `dllentry.cpp`（其 DllMain/类厂由我们自己的 `dllmain.cpp` 提供）。

### 注册（需管理员）
双击 `install.bat`（会检查管理员权限、regsvr32 返回码，并回读注册表确认真的写进去了）；卸载用 `uninstall.bat`。
命令行等价：
```cmd
regsvr32 "bassmax_dshow.ax"
```

注册会写入两组内容，缺任一组播放器都"看不见"：

| 位置 | 作用 |
|---|---|
| `HKCR\CLSID\{C41F7D22-...}`（默认值=显示名）+ `\InprocServer32`（= .ax 绝对路径，ThreadingModel=Both） | COM 能按 CLSID 创建这个滤镜 |
| `HKCR\CLSID\{083863F1-70DE-11D0-BD40-00A0C911CE86}\Instance\{C41F7D22-...}`，含 `FriendlyName`、`CLSID`、`FilterData`（REGFILTER2 v2，merit=`MERIT_UNLIKELY`，2 个引脚 × {PCM, IEEE_FLOAT}） | **滤镜映射器**能枚举到它 —— MPC-HC/PotPlayer 的"添加滤镜"列表就是走 `IFilterMapper2::EnumMatchingFilters` 枚举的 |

> 早期版本只写了第一组，所以 `regsvr32` 显示成功、注册表也有键，但播放器列表里永远找不到。现在两组都由 `DllRegisterServer` 写入（`IFilterMapper2::RegisterFilter`），且失败时返回真实 HRESULT，`regsvr32` 会报错而不是假成功。
> merit 取 `MERIT_UNLIKELY`(0x00400000)：高于部分播放器列表用的 `MERIT_DO_NOT_USE+1` 门槛，又低到不会被自动构链插入（必须手动添加）。

### 在播放器中使用
- **MPC-HC**：`选项 → 外部滤镜 → 添加滤镜 → 选择 “BassMax Audio Enhancer”`，并勾选“优先/强制”。或 `播放 → 滤镜` 里查看已加载。
- **PotPlayer**：`F5 首选项 → 滤镜 → 音频滤镜 → 添加系统滤镜 → BassMax Audio Enhancer`，勾选启用。

滤镜会做**透传式变换**：输出格式与输入完全一致（不重采样）。支持 LAV Audio Decoder 的全部输出格式：

- 位深：8-bit（无符号）、16-bit、24-bit、32-bit 整数；32-bit / 64-bit 浮点；`WAVEFORMATEXTENSIBLE` 的 24-in-32 之类按**容器位深**解析
- 声道：1 ~ 8（单声道 / 2.0 / 5.1 / 7.1）都能接，按声道对（1-2、3-4、5-6、7-8）独立计算；超过 8 声道明确拒绝，不会硬算。
  **但实际只建议单声道或双声道使用**，多声道请先下混成 2.0，原因见下面「多声道素材」
- 采样率：8 kHz ~ 1.536 MHz（覆盖 DSD256 的 1.4112 MHz；DSD512 的 2.8224 MHz 仍拒绝）

> 之前"播放器偶尔卡住、然后自动把滤镜卸掉"的根因：位深按**有效位深**而不是**容器位深**算步长（24-in-32 会算错 → 帧长偏移 → 越界写 → 崩溃/挂死），并且输出缓冲没有上限检查。v2 里所有读写都按 `GetSize()` 钳位，指针/长度全程校验。

### 多声道素材（5.1 / 7.1）：驱动自动限到 60%，但仍建议混缩成 2.0

7.1 文件确实会被这个滤镜处理，但结果不好听，而且是**结构上必然**的：

- 滤镜按声道对独立处理，7.1 = 4 对 = 4 套检测器 + 4 路限幅，**每一对都各自把自己的动态拉到接近满电平**
- 耳机 / 立体声输出会把这些声道下混求和，4 路都接近满电平的信号相加 → 远超 0 dBFS
- 限幅器因此反复触发、谐波堆积，听感就是「糊 + 一直顶着上限」

**1.0.3 起加了自动保护**：输入声道数 > 2 时，驱动自动压到 60%（`kBassMaxMultiChDriveCap`）。规则：

| 情况 | 生效驱动 |
|---|---|
| 1–2 声道 | 用户设置值，完全不干预 |
| > 2 声道，用户设了 100% | 60% |
| > 2 声道，用户本来就设了 35% | 35%（只压不抬） |

注册表里的值**一个字节都不改**，切回 2.0 输入立刻恢复你原本的设置；4.0 / 5.1 / 6.1 / 7.1 一律适用。
这只能把过载压下来，**不能修好声道配对**——5.1 时中置与 LFE 共用一路增益（第 2 对），
大鼓一来对白会跟着伏动。所以正解还是在下混之后、进滤镜之前收成 2.0：

1. **LAV Audio Decoder**（MPC-HC / PotPlayer 常用）→ 属性 → `Mixing` → 勾选 `Enable Mixing` →
   `Output Speaker Configuration` 选 `Stereo`（2.0），其余保持默认
2. 用播放器自带解码器时，找对应的「下混 / 混缩到立体声 / Speaker Configuration = 2.0」选项
3. 这样滤镜收到的就是 2.0，和设计目标一致

> **例外：WAV（PCM）走不到 LAV。** PCM 是未压缩数据，DirectShow 链路里不需要解码器，
> 播放器会用自己的 PCM 解码器直通到渲染器，**LAV Audio 根本不被创建**，它的 `Mixing` 设置对 WAV 无效。
> `Formats` 页里的 `pcm` 勾选只影响「LAV 被选为 PCM 解码器」的场合。
> 多声道 WAV 要下混，只有两条路：
>
> - **预先转成 2.0**（最省事，一次搞定）：
>   ```cmd
>   ffmpeg -i "5.1.wav" -ac 2 -c:a pcm_s24le "5.1_stereo.wav"
>   ```
> - **转成需要解码的格式**（AC3 / DTS / AAC 等）让 LAV 真正接管，此时 `Mixing` 才生效
>
> 顺带：`MixingLayout` 的枚举是 Mono(0) / Stereo(1) / 4.0(2) / 5.1(3) / 6.1(4) / 7.1(5)，
> 设成 Stereo 后注册表 `HKCU\Software\LAV\Audio` 里应当是 `MixingLayout = 1`，
> 仍然是 `3` 就说明选的还是 5.1。

> 单声道不受此影响：`m_ch == 1` 时它被当作复制成一对（L = R）处理，检测器看到的就是原始信号，
> 与「双单声道立体声」完全等价。不过目前只用真实立体声素材验证过，单声道尚未实测。

参数：默认为「爵士乐 + 低音 +2 dB + 高音 +2 dB + 驱动 100%」。全部存在 `HKCU\Software\BassMax`，共六个字符串值
（`Bass` / `Treble` / `Drive` / `Preset` / `Enabled` / `TrayIcon`），值名固定、可以直接手改；属性页与托盘界面改的就是这一份，多播放器共用。
（1.0.1 起取消了按 `DspVer` 的参数迁移——删掉值也不会再被强行写成默认值。）

### 设置界面（三种入口）

**1) 播放器里的标准属性页**（DirectShow `ISpecifyPropertyPages`，属性页 CLSID `{C41F7D22-6B18-4A93-8E52-1D7A4F60C9B2}`，与滤镜同一个 .ax 导出）
- MPC-HC：播放时**右键画面 → 滤镜 → BassMax Audio Enhancer**，即弹出属性页
- PotPlayer：右键画面 → 滤镜 → 选中该滤镜 → 属性
- 6 个 EQ 预设按钮 + 三个滑块：**低音**（−12 … +12 dB，100 Hz 低架）、**高音**（−12 … +12 dB，9 kHz 高架，后置 EQ 不进压缩）、**驱动**
  （0 … 100%：动态增强量 = 向上压缩深度 + 响度补偿；0 = 逐样本旁路，100% 最强）
- 改动实时生效（音频线程检测到参数代号变化后重建系数），无需重开文件

**2) 右下角托盘图标**
- 播放器加载滤镜时自动出现，提示文字 “BassMax 已开启 / 已关闭”
- **左键单击** = 打开/收起设置窗口；**右键单击** = 直接切总开关（参数保留）
- 播放器退出 / 滤镜销毁时图标自动移除
- 想彻底不显示：注册表 `HKCU\Software\BassMax` 的 `TrayIcon` 改 `0`，**400ms 内图标就消失**（不用重开播放器；改回 `1` 立刻长回来）

**3) 播放器没启动时：用系统自带的 `rundll32` 调 `.ax` 的导出（不附带任何额外 EXE）**
```cmd
rundll32.exe "C:\Users\<你>\MyMPC\Codecs\bassmax_dshow.ax",BassMaxConfig
```
- 弹出的是同一个设置窗口（`IDD_BASSMAX_POPUP`），改完立刻写注册表，关窗即退出该进程
- 入口是 `.ax` 自己导出的 `BassMaxConfig`（`extern "C" __declspec(dllexport) void CALLBACK(HWND, HINSTANCE, LPSTR, int)`，即 `rundll32` 的标准签名）
- **界面和播放器里完全一致**：`.ax` 内嵌了一份 comctl32 v6 清单，独立运行时自己 `CreateActCtx` 激活，
  再动态加载 `comctl32.dll`（解析到 WinSxS 里的 v6）并 `SetThemeAppProperties`；
  同时 `SetProcessDPIAware` 关掉 DPI 虚拟化。否则 `rundll32.exe` 没有清单，
  控件会退回 Win95 经典外观，且整窗被位图拉伸导致字体发虚
- 重复执行不会开出第二个窗口：已在运行的那个会被拉到前台
- 装包时 `install.bat` 会把这条命令直接打印出来，复制即用

> **正在播放时也能改**：滤镜的托盘线程每 400ms 读一遍注册表，`Bass / Treble / Drive / Preset / Enabled / TrayIcon`
> 任何一项被外部改动（手改注册表、从 rundll32 设置窗口写入）都会在一拍内套用，不需要重开文件或重启播放器。

> 说明：托盘图标由滤镜在播放器进程内创建（独立消息线程）。图标资源是 `bassmax.ico`（开启）/ `bassmax_off.ico`（关闭）。

### 冒烟测试（已验证）
- 加载 DLL → `DllGetClassObject` → `IClassFactory::CreateInstance(IBaseFilter)` 全部 `S_OK`
- `DllRegisterServer` 写入上述两组键；`IFilterMapper2::EnumMatchingFilters`（音频输入、merit > DO_NOT_USE）能枚举到本滤镜
- 用注册表信息 `CoCreateInstance(IBaseFilter)` 成功，`FindPin("In"/"Out")` 都能找到
- 直接调 `Transform`：16-bit PCM 与 32-bit float 均正常出音，1kHz 增益 −1.45 dB（与校准值一致），前 331 样本为空是 7.5 ms 前瞻延迟（与原插件相同）
- 属性页：`QI ISpecifyPropertyPages` → `GetPages` 返回正确 CLSID → 创建属性页 → `SetObjects` / `Activate` 全 `S_OK`，对话框里枚举到 2 个轨迹条 + 6 个静态文本
- 托盘：滤镜创建后 `BassMaxTrayWnd` 隐藏窗口出现，滤镜释放后窗口与图标一并消失

<details>
<summary>排查：列表里看不到滤镜</summary>

1. 播放器只在**启动时**读滤镜列表 —— 注册后必须完全退出再打开。
2. 确认注册表里两处都在：
   ```cmd
   reg query "HKCR\CLSID\{C41F7D22-6B18-4A93-8E52-1D7A4F60C9B1}" /s
   reg query "HKCR\CLSID\{083863F1-70DE-11D0-BD40-00A0C911CE86}\Instance\{C41F7D22-6B18-4A93-8E52-1D7A4F60C9B1}" /s
   ```
   第二条（`Instance`/`FilterData`）缺失 = 播放器列表就看不到。
3. `InprocServer32` 必须是**当前实际存在的** .ax 绝对路径；换了文件位置要重新注册。
4. 用 Regshot 之类做前后对比时，先拍快照再注册 —— 若注册早已存在，对比结果会是"零变化"，容易被误判成注册失败。
</details>

---

## 四、Chrome / Edge 扩展

实时处理**当前标签页**的音频。用的是 `tabCapture` + 离屏文档 + AudioWorklet，不是内容脚本——所以跨域音源（YouTube、B 站、Spotify Web）不会静音，也不受页面 CSP 影响。

**安装**（未上架应用商店，走开发者模式）：

1. 下载 Release 里的 `bassmax_chrome_v1.0.0.zip` 并解压
2. 打开 `chrome://extensions`（Edge 是 `edge://extensions`）
3. 右上角打开「开发者模式」→ 点「加载已解压的扩展程序」→ 选解压出来的目录
4. 播放音频后点工具栏图标，选「开始处理」

限制：标签页必须正在发声（静音标签抓不到音轨）；关闭扩展或切走标签页即恢复原声。DSP 与 C++ 版逐样本一致（相对差 −97 ~ −101 dB）。

详见 [bassmax_chrome_ext/README.md](bassmax_chrome_ext/README.md)。

---

## 五、命令行转码器

离线批量，单文件 283 KB，`/MT` 静态链接不依赖 VC 运行库。

```bat
:: WAV 进 WAV 出，输出到 music.bmx.wav
bassmax_cli.exe music.wav --drive 8.5 --bass 3 --preset 爵士乐

:: 完整媒体文件：视频流原样复制，只重做音轨
bmx.cmd "D:\MV\old.mkv" --drive 9 --bass 4 --preset rock

:: ffmpeg 管道：需要前级滤镜时放在 BassMax 之前
ffmpeg -i "输入" -f wav - | bassmax_cli - --drive 10 --bass 2 --treble 2 --preset 爵士乐 -o out.wav
```

多个 `-af` 处理（降混、重采样）由 ffmpeg 负责，BassMax 只做音效本身。详见 [bassmax_cli/README.md](bassmax_cli/README.md)。

---

## 六、验证结果汇总

| 项目 | 结果 |
|---|---|
| DSP 核心 vs 原插件（真实歌曲配对） | 响度轨迹相关 0.90、峰值因数 3.40 vs 3.37、频段偏差 <1.7dB |
| DirectShow `.ax` 构建 | ✅ x64，导出 5 个符号（4 个 COM 入口 + `BassMaxConfig`），加载/实例化/自注册全部通过 |
| 吞吐（单核，立体声 float） | 44.1k 184×、96k 83.5×、352.8k 20.4×、705.6k 10.5×、1.4112M 5.3× |
| 设置层回归 | `settings_test` 29/29（含 DWORD 写法、缺值不采纳、超范围钳位、旧配置不被改写、全新安装落默认值） |
| 版本资源 | `FileVersion` / `ProductVersion` = 1.0.3.0 |

*备注：v1 曾把原插件的「安静段抬升」误判成「电平相关 auto-gain 噪声」，用恒定 makeup 把它抹掉了 —— 现在已按配对测量还原成向上压缩。*

---

## 七、参与改进

MIT 许可，欢迎 Fork 与 Pull Request。**算法效果、多声道处理、跨平台移植**这三类改动最有价值，具体流程见 [CONTRIBUTING.md](CONTRIBUTING.md)。

三条底线：`bassmax_core.hpp` 是唯一权威实现（改动需同步 DShow 副本与 JS 移植）、改完要跑逐样本比对、不要提交测试素材和二进制产物。

已知短板（欢迎认领）：

- 多声道（>2）处理质量 —— 现在建议先下混成 2.0
- `--drive` 0–10 分档的感知一致性
- 跨平台构建（核心本身与平台无关，缺构建脚本与测试）
- `test/` 下的脚本还没有 CI

## 八、许可

代码采用 [MIT](LICENSE)。`bassmax_dshow/baseclasses/` 是 Microsoft DirectShow Base Classes，**不受 MIT 覆盖**，详见 [NOTICE](NOTICE)。
