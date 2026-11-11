# BassMax — 64 位音频增强器 (Clean-room)

**版本 1.0.4** · 2026-11 · MIT License

一个从零重写的三段分频动态增强器。**一份 DSP 核心驱动三种形态**：

| 形态 | 产物 | 场景 | 速度 |
|---|---|---|---|
| DirectShow 滤镜 | `bassmax_dshow.ax` (188 KB) | MPC-HC / PotPlayer 实时播放 | 44.1k 184× |
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
│   ├── bassmax_filter.h/.cpp #   CTransInPlaceFilter 子类, 格式协商/PCM↔float, ISpecifyPropertyPages
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
├── bassmax_decoder/          # MPC-BE 专用「解码器角色」壳 (独立 .ax)
│   ├── bassmax_decoder.cpp   #   代理压缩输入 + 私有 LAV 图 + PCM 出口做 DSP
│   ├── build.sh              #   复用 ../bassmax_dshow 的 baseclasses / DSP / settings
│   ├── test/host.cpp         #   x64 真实播放宿主 (普通/seek/切轨三种模式)
│   ├── install.bat / uninstall.bat
│   └── (构建产物 bassmax_decoder.ax / reg_helper.exe 不入库, CLSID ...C9B4)
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

一个 `.ax` 只注册**一个滤镜 + 一张属性页**（1.0.6 起回归单管线）：

| 显示名 | CLSID | 基类 | 给谁用 |
|---|---|---|---|
| BassMax Audio Enhancer | `{C41F7D22-…-C9B1}` | `CTransInPlaceFilter` | PotPlayer / MPC-HC 等支持 in-place 变换的播放器 |

**MPC-BE 用另一个 `.ax`**：`bassmax_decoder.ax`（C9B4，解码器角色），见「六·补」。
开发过程中（该版本未单独发布）曾在本 `.ax` 里多注册一个 `CTransformFilter` 版（C9B3）试图兼容 MPC-BE，
查官方源码后确认那条路走不通，1.0.6 已移除；注册时会自动清除它的注册表残留。

### 构建
```bash
cd bassmax_dshow
bash build.sh          # 需要 baseclasses/ , Windows SDK 10, MSVC 2022
# 产物: bassmax_dshow.ax

bash test/build_test.sh   # 64 位测试宿主，必须和 .ax 同位数
./test/test_host.exe ./bassmax_dshow.ax
```
源码分工：`bassmax_dsp.*`（格式解析 + DSP，两个 `.ax` 共用）、`bassmax_filter.*`（CTIP 版）、
`bassmax_settings/tray/page`（设置与界面）。`bassmax_xform.*` 是那次双管线尝试的
遗留源码，已标注停用且不再编入 `build.sh`，保留仅供追溯。

要点：编译带 `/utf-8`（源码含中文注释）；链接用 `/LD` 并 `LINK /EXPORT:...` 显式导出四个 COM 入口；
仅排除 baseclasses 的 `dllentry.cpp`（其 DllMain/类厂由我们自己的 `dllmain.cpp` 提供）。

### 注册（需管理员）
双击 `install.bat`（会检查管理员权限、注册返回码，并回读注册表确认真的写进去了）；卸载用 `uninstall.bat`。
命令行等价：
```cmd
regsvr32 "bassmax_dshow.ax"
```
> 若环境把 `regsvr32` / `rundll32` 列入 LOLBin 黑名单（直接被安全策略拦掉），用仓库自带的等价工具：
> `bash bassmax_dshow/tools/build_reg.sh` 编出 `reg_helper.exe`，然后
> `reg_helper.exe <path>\bassmax_dshow.ax`（反注册加 `/u`，`/enum` 可列出映射器里能看到哪些滤波器）。

注册为**这一个滤镜**写入两组内容，缺任一组播放器都"看不见"：

| 位置 | 作用 |
|---|---|
| `HKCR\CLSID\{C41F7D22-…-C9B1}`（默认值=显示名）+ `\InprocServer32`（= .ax 绝对路径，ThreadingModel=Both） | COM 能按 CLSID 创建滤镜 |
| `HKCR\CLSID\{083863F1-70DE-11D0-BD40-00A0C911CE86}\Instance\<CLSID>`，含 `FriendlyName`、`CLSID`、`FilterData`（REGFILTER2 v1，merit=`MERIT_UNLIKELY`，2 个引脚 × {PCM, IEEE_FLOAT}） | **滤镜映射器**能枚举到它 —— 各播放器的"添加滤镜"列表就是走 `IFilterMapper2::EnumMatchingFilters` |

> 早期版本只写了第一组，所以 `regsvr32` 显示成功、注册表也有键，但播放器列表里永远找不到。现在两组都由 `DllRegisterServer` 写入（`IFilterMapper2::RegisterFilter`），且失败时返回真实 HRESULT，不会假成功。
> merit 取 `MERIT_UNLIKELY`(0x00400000)：高于部分播放器列表用的 `MERIT_DO_NOT_USE+1` 门槛，又低到不会被自动构链插入（必须手动添加）。
> `DllRegisterServer` 与 `DllUnregisterServer` 都会清除早期双管线尝试遗留的 `C9B3` 条目（CLSID + 两个类别的 `Instance`/`FilterData`），所以从旧版升级时跑一次 `install.bat` 就能自愈，不会留下幽灵滤镜。

### 在播放器中使用
- **MPC-HC**：`选项 → 外部滤镜 → 添加滤镜 → 选择 “BassMax Audio Enhancer”`，并勾选“优先/强制”。或 `播放 → 滤镜` 里查看已加载。
- **PotPlayer**：`F5 首选项 → 滤镜 → 音频滤镜 → 添加系统滤镜 → BassMax Audio Enhancer`，勾选启用。
- **MPC-BE**：**不要用本滤镜**。MPC-BE 内置 `Audio Switcher` 会挡住所有 PCM→PCM 外部滤镜，
  这里无论选哪一条都是「列表里有、右键滤镜里没有、没效果」。请改用 `bassmax_decoder.ax`，
  配置与判据见「六·补」。

滤镜会做**透传式变换**：输出格式与输入完全一致（不重采样）。支持 LAV Audio Decoder 的全部输出格式：

- 位深：8-bit（无符号）、16-bit、24-bit、32-bit 整数；32-bit / 64-bit 浮点；`WAVEFORMATEXTENSIBLE` 的 24-in-32 之类按**容器位深**解析
- 声道：1 ~ 8（单声道 / 2.0 / 5.1 / 7.1）都能接，按声道对（1-2、3-4、5-6、7-8）独立计算；超过 8 声道明确拒绝，不会硬算。
  **但实际只建议单声道或双声道使用**，多声道请先下混成 2.0，原因见下面「多声道素材」
- 采样率：8 kHz ~ 1.536 MHz（覆盖 DSD256 的 1.4112 MHz；DSD512 的 2.8224 MHz 仍拒绝）

> 之前"播放器偶尔卡住、然后自动把滤镜卸掉"的根因：位深按**有效位深**而不是**容器位深**算步长（24-in-32 会算错 → 帧长偏移 → 越界写 → 崩溃/挂死），并且输出缓冲没有上限检查。v2 里所有读写都按 `GetSize()` 钳位，指针/长度全程校验。

### 多音轨切换（1.0.4 修复：切音轨导致整条流停掉）

**症状**：多音轨文件在播放中切音轨，画面和声音一起停住；即使把音效关掉也不行；把滤镜从播放器移除后切好音轨再加回来，这个文件仍然播不了，必须彻底移除滤镜才能恢复。

**根因不是缓存，是基类选错了。** 1.0.3 及之前用 `CTransformFilter`（拷贝式变换），它的输出类型只能由 `GetMediaType` 静态生成。当输出引脚已经连接、而输入引脚被塞进一个新格式时，基类会拿新输入类型去比对**已经过期的输出类型**，并且要求 isFloat / 位深 / 声道 / 采样率完全相等。LAV 切音轨后只要这几项中任意一项变了，比对必然失败 → `QueryAccept` 返回 `S_FALSE` → 动态切换失败；万一硬连上则更糟：输入已换新格式、输出还停在旧格式，下游按旧块长收新数据，整条音频链错位，播放器判定音频流坏死并把整个图停掉（所以画面也停）。

`Enabled=0` 只是把 DSP 参数归零，滤镜仍然留在图里参与格式协商，因此关音效没有用。

**1.0.4 的改法**：

- 基类换成 `CTransInPlaceFilter`（就地变换，本来就是它该干的活）。它把输入引脚的 `CheckMediaType` 直接转发给下游 `QueryAccept`，`CompleteConnect` 发现上下游类型不一致会自动 `ReconnectPin` 把新类型推下去。同时删掉了 `GetMediaType` / `CheckTransform` / `DecideBufferSize` 三个易错点，也不再需要输出分配器和样本拷贝。
- 宽进透传：任何 `MEDIATYPE_Audio` + `FORMAT_WaveFormatEx` 都接受；改不动的格式（AC3/DTS passthrough、>8 声道、怪位深）原样放过，`Transform` 恒返回 `S_OK`。**音效滤镜不该成为图建不起来的原因。**
- 运行中格式变更两条入口都处理：`SetMediaType(PINDIR_INPUT)` 与采样自带的 `AM_MEDIA_TYPE`（`IMediaSample::GetMediaType`）。
- 顺带修掉托盘线程的定时炸弹：析构里的 `WaitForSingleObject(2000)` 已删（它跑在播放器拆图线程上，等 2 秒就是画面卡 2 秒）；托盘线程改为每进程一个、活到进程结束。

实测：MPC-HC / PotPlayer + LAV 全套，MKV 多音轨来回切换正常，不再停流。

### MPC-BE 为什么不认 PCM 外部滤镜（1.0.6 起移除 C9B3）

**症状**：MPC-BE 的「外部滤镜」里能看到并添加了 BassMax，播放时「右键 → 滤镜」里却没有它，音效完全没有；
但进程模块里确实加载了 `bassmax_dshow.ax`（被创建，然后被丢弃）。

**根因（查 MPC-BE 官方源码确认）**：`CFGManagerPlayer` 会无条件把内置 `Audio Switcher` 以
`MERIT64_HIGHEST`（1<<58）插入音频链，外部滤镜上限只有 `MERIT64_EXT_FILTERS_PREFER`（≈1<<48）；
且上游连接协商在第三方 transform 的 `ReceiveConnection` 之前就失败。所以 **PCM→PCM 形态的外部 DSP
在 MPC-BE 里根本进不了链路**，与用 `CTransformFilter` 还是 `CTransInPlaceFilter`、注册多少媒体类型、
merit 调多高都无关。官方设置里也没有禁用 Audio Switcher / 外部 DSP / Winamp 插件的入口
（`SetAudioFilter1` 只接受 `compand`，裁剪版 libavfilter 只注册 `aresample / atempo / compand`）。

**双管线尝试已被推翻（该版本未单独发布）**：当时新增 `bassmax_xform.*`（C9B3，`CTransformFilter`），并把
1.0.3 的三个缺陷逐个堵掉（`GetMediaType` 返回当前输入类型、`CheckTransform` 不做精确比对、
`DecideBufferSize` 按输入格式算块长）。但诊断日志证明它在 MPC-BE 里的生命周期始终是
`CREATE → QI → JoinFilterGraph → GetMediaType → DESTROY`，`CheckInputType` 一次都没被调用；
把媒体类型扩到 224 种组合、merit 提到 `0x800010` 也无效。1.0.6 起这条管线从构建、类厂、
注册中全部移除，源码保留在 `bassmax_xform.*` 供追溯，不再编译。

**MPC-BE 的正解**：外部滤镜可以顶替「解码器」角色（实测 `LAV Audio Decoder` 就是这样替换
`MPC Audio Decoder` 的）。独立的 `bassmax_decoder.ax` 内部驱动 LAV 解码、对输出 PCM 做 DSP，
见「六·补」。

**沿用下来的修复：`wBitsPerSample = 0`**

解析格式时原来直接读 `wBitsPerSample`，而 WAV Parser（以及部分分离器/解码器）给出的 `WAVEFORMATEX`
只填了 `nBlockAlign` 和 `nAvgBytesPerSec`，位深是 0。旧代码于是判定"不支持这个格式" → 整条链路透传，
滤镜在链路上、参数字段也在，就是**一个样本都不处理**。改为用 `nBlockAlign / nChannels` 反推每样本
字节数，两者不一致时以 `nBlockAlign` 为准（24-in-32 这类容器填充只有它能说清）。这段逻辑在
`bassmax_dsp.cpp`，`bassmax_dshow.ax` 与 `bassmax_decoder.ax` 共用。

**回归**（`test/test_host.exe`，x64，单 CLSID C9B1，5 轮 2ch/6ch 交替）：

| 用例 | 结果 |
|---|---|
| 图接通（WAV Parser → 滤镜 → Null Renderer） | 5/5 通过 |
| 运行中 `QueryAccept(6ch/48k/float32)` | 5/5 `S_OK` |
| 2 秒音频完整推流（要求 `EC_COMPLETE` 且无 `EC_ERRORABORT`） | 5/5 通过 |
| 拆除耗时 | 5/5 < 1 ms |

> 旧版测试宿主是 32 位的，加载 64 位 `.ax` 直接 `193 ERROR_BAD_EXE_FORMAT`，等于什么都没验证；
> 现在 `test/build_test.sh` 强制 x64。

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
   后一条（`Instance`/`FilterData`）缺失 = 播放器列表就看不到。
   也可以直接跑 `bassmax_dshow/tools/reg_helper.exe /enum`，它会用播放器同一套
   `IFilterMapper2` 接口把映射器里能枚举到的 BassMax 条目打出来。正常应只有
   `BassMax Audio Enhancer`(C9B1) 与 `BassMax Audio Decoder (LAV Wrapper)`(C9B4) 两条；
   若还看到带 `(MPC-BE)` 后缀的旧条目，说明旧版残留未清，重跑一次 `install.bat` 即可。
3. `InprocServer32` 必须是**当前实际存在的** .ax 绝对路径；换了文件位置要重新注册。
4. 用 Regshot 之类做前后对比时，先拍快照再注册 —— 若注册早已存在，对比结果会是"零变化"，容易被误判成注册失败。

**MPC-BE 专属**：不要用本滤镜。请确认 `HKCU\Software\MPC-BE\ExternalFilters` 指向的是
`C9B4` 解码器壳，判据看 `%TEMP%\bassmax_decoder.log` 里 `changed=` 是否增长，见「六·补」。
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
| DirectShow `.ax` 构建 | ✅ x64，单管线（CTIP 一个滤镜 + 属性页），导出 5 个符号（4 个 COM 入口 + `BassMaxConfig`），加载/实例化/自注册/映射器枚举、5 轮真实推流回归全部通过 |
| 吞吐（单核，立体声 float） | 44.1k 184×、96k 83.5×、352.8k 20.4×、705.6k 10.5×、1.4112M 5.3× |
| 设置层回归 | `settings_test` 29/29（含 DWORD 写法、缺值不采纳、超范围钳位、旧配置不被改写、全新安装落默认值） |
| 版本资源 | `FileVersion` / `ProductVersion` = 1.0.6.0 |

*备注：v1 曾把原插件的「安静段抬升」误判成「电平相关 auto-gain 噪声」，用恒定 makeup 把它抹掉了 —— 现在已按配对测量还原成向上压缩。*

---

## 六·补、MPC-BE 专用解码器壳（`bassmax_decoder.ax`）

**为什么单独做一个 `.ax`**：MPC-BE 1.9.3 的 `CFGManagerPlayer` 会无条件把内置 `Audio Switcher` 以 `MERIT64_HIGHEST`（1<<58）插入音频链；外部滤镜上限只有 `MERIT64_EXT_FILTERS_PREFER`（≈1<<48），且上游连接协商在第三方 transform 的 `ReceiveConnection` 之前就失败。所以 **PCM→PCM 形态的外部 DSP 在 MPC-BE 里进不了链路**，无论注册多少媒体类型、把 merit 调多高、用 `CTransformFilter` 还是 `CTransInPlaceFilter` 都一样。官方源码里也没有禁用 Audio Switcher、外部 DSP / Winamp 插件入口；`SetAudioFilter1` 只接受 `compand`，裁剪版 libavfilter 只注册了 `aresample / atempo / compand`。

但 MPC-BE 允许外部滤镜顶替**解码器**角色（实测 `LAV Audio Decoder` 就是这样替换 `MPC Audio Decoder` 的）。所以这个独立 `.ax` 伪装成音频解码器：

```
MPC-BE graph: [Source] --(压缩)--> [BMD.In]   [BMD.Out] --(PCM)--> [Audio Switcher] --> [Renderer]
                                      |              ^
                                      | 零拷贝转发    | Deliver
                                      v              |
私有 graph:  [Feeder.Out] -> [LAV Audio Decoder] -> [Sink.In]
```

- 输入引脚接受任意非 PCM 的 `MEDIATYPE_Audio`，把上游 `IMediaSample` 直接交给 LAV 的 `IMemInputPin::Receive`（零拷贝）。
- 输出引脚用 LAV 解出的真实 PCM 格式协商，先把样本复制到下游 allocator，再做 `BassMaxDsp::Process`。
- `Run/Pause/Stop`、`Flush`、`EndOfStream`、`NewSegment`、时间戳 / 同步点 / discontinuity / preroll / 动态媒体类型全部双向转发。
- 私有图重建前先 `Stop`（LAV 有解码线程，Run 状态直接 Disconnect 会失败）；切轨后若 LAV 输出格式变了，在下一个输出 sample 上挂新格式通知下游。

**复用与冲突（已解决）**：

| 复用项 | 冲突 | 处理 |
|---|---|---|
| `baseclasses/`、`bassmax_dsp.cpp`、`bassmax_core.hpp` | 静态编进各自 DLL，符号不导出，无 DLL 级冲突 | 直接 `#include` / 一起编译 |
| `bassmax_settings.cpp` | `s_val/s_gen/s_cs` 是 DLL-local `static`，旧 `.ax` 属性页写入不会让新 `.ax` 的 `SettingsGen()` 变化 | 不在新壳暴露属性页；每 400ms 调 `ReloadFromRegistry()` 主动回读 `HKCU\Software\BassMax` |
| `CLSID_BassMaxPage` / `ISpecifyPropertyPages` | 属性页注册在旧 `.ax` 的 C9B2，两边都注册会互相覆盖 `InprocServer32`；同名常量还会重复定义 | 新壳完全移除属性页，统一走"设置页 / 托盘 / 注册表"改参数 |
| CLSID | 与 C9B1/C9B2 同族（C9B3 已停用并清除） | 新分配 `...C9B4` |

**依赖**：系统里必须已注册 `LAV Audio Decoder`（`{E8E73B6B-4CB3-44A4-BE99-4F7BCB96E491}`）。MPC-BE 自带 `LAVFilters64\LAVAudio.ax`，直接可用。

**部署**：`install.bat` 注册 `.ax`，并在 `HKCU\Software\MPC-BE\ExternalFilters` 写入 `SourceType=0 / LoadType=0(PREFERRED) / MT_000=Audio+NULL(通配)` 一条；脚本会复用已有的 C9B4 条目而非硬编号，因此 MPC-BE 退出时重排编号后重跑也不会产生重复项。

> 仓库不含任何二进制（`.ax` / `.exe` 已被 `.gitignore` 排除）：先运行 `bassmax_decoder/build.sh` 编出 `bassmax_decoder.ax`，成品二进制走 GitHub Releases。`install.bat` 优先用同目录的 `reg_helper.exe` 调 `DllRegisterServer`（`regsvr32` 常被安全策略当 LOLBin 拦截），缺失时自动回退 `regsvr32`；要生成它请执行 `bash bassmax_dshow/tools/build_reg.sh` 并把结果放到 `.ax` 同目录。

**托盘开关（1.0.7 新增）**：新壳不做属性页，改为在播放器**进程内**挂托盘图标 —— 播放时出现、滤镜释放后自动消失，不需要额外开任何程序，也不存在"关掉设置窗口把播放器带走"。

- **左键 / 右键单击都 = 开/关总开关**，与 `bassmax_dshow.ax` 的右键行为对齐。单击直接调本 DLL 的 `SetSettings()`，`s_gen` 立刻递增，音频线程下一拍 `Tick()` 重建系数；关闭是 Core 逐样本 bypass，不重建 graph、不切音轨。
- 托盘只是 `Shell_NotifyIcon` + 隐藏窗口（`BassMaxDecoderTrayWnd`）+ 一个消息线程，**不注册任何 CLSID、不实现 `ISpecifyPropertyPages`**，与属性页 `CLSID_BassMaxPage` 的 `InprocServer32` 抢注冲突零交集。只认 `WM_*BUTTONUP`，不处理 `WM_CONTEXTMENU`，避免某些 Windows 版本右键发两条消息导致翻转两次。
- 图标内嵌 `bassmax.ico`（开启，蓝色渐变柱）/ `bassmax_off.ico`（关闭，纯灰）的全部尺寸；`.ax` 同目录放同名 ico 可覆盖，换图标不用重编译。`HKCU\Software\BassMax\TrayIcon=0` 可关掉图标。
- 线程每进程只开一个、活到进程结束；`DecoderTrayAddRef/Release` 只发异步消息，绝不在滤镜析构里等线程（那条路径常在播放器拆图线程上，阻塞就是画面卡顿）。400ms 轮询只用于同步外部改动。
- 已知：**双击会翻转两次、回到原状**（单击切换的固有行为，旧 `.ax` 右键也一样）。两张 `.ax` 同时启用会出现两个托盘图标。

**回退机制（解码器壳第二版，修复"无法渲染文件"）**：

外部滤镜被配成 `PREFERRED + Audio/NULL 通配` 时，MPC-BE 会把**所有**音频流先塞给本滤镜。早期版本要等 `CompleteConnect` 里私有 LAV 图建失败才报错，这时整张图已经没救了，播放器直接弹"无法渲染文件"且不会回退到内置解码器。现在改成：

- `BmdInPin::CheckMediaType` 先用 `IPin::QueryAccept` 问内部 LAV 输入引脚认不认这个压缩类型；明确的 `S_FALSE` / `VFW_E_TYPE_NOT_ACCEPTED` 当场拒绝，图管理器自然继续尝试下一个候选。其它错误码放行，避免 LAV 未连接时 `QueryAccept` 不可用造成误拒。
- `BuildPrivateGraph` 里 `ConnectDirect(Feeder->LAV, &mt)` 失败后，先断开半连接，再试一次"不指定类型"的自动协商。
- `PcmMt` 在 LAV 还没就绪时**不再返回 `VFW_E_NOT_CONNECTED`**（那会让输出引脚 `GetMediaType` 失败、滤镜在枚举阶段被丢弃），而是给一个基于输入推断的合法占位 PCM 类型；等输入连上拿到真实格式后由 `m_mtDirty` 在下一块 sample 上挂动态媒体类型。

已知限制：部分 APE 是 **LAV 自身解不了**（ffmpeg 的 APE 解码器对部分新版/高压缩级 APE 支持不全，foobar2000 用 Monkey's Audio 官方解码器所以能放）。这类文件现在会提前让位给内置解码器，不会再拖崩整张图，但自然也就没有 BassMax 音效；若 DirectShow 侧根本没有能解它的解码器，播放器仍会报无法渲染 —— 那与本滤镜无关。

**总开关校验（避免"只有图标/只有透传"的假阳性）**：

日志现在打印 `DSP 总开关 enabled=... bass=... drive=...`，并新增 `changed=` 统计 —— 每块样本在 `Process()` 之后逐字节比对输入输出前 64 字节，只有数据真的被改动才计数。总开关关闭时 Core 走 bypass，`changed` 恒为 0。看 `changed` 增长才算 DSP 真在工作，光看 `samples/bytes`、滤镜名或托盘图标都不算。

**验证**（x64 真实播放，`test/host.exe` + MPC-BE 1.9.3，`Enabled=1`）：

| 场景 | 结果 |
|---|---|
| 普通播放 m4a / seeking 到 50% / 同格式切轨 | `RESULT: PASS`，`Flush → NewSegment → 继续出样本`，无 `EC_ERRORABORT` |
| 不同格式切轨（m4a AAC → APE） | 私有图 Stop→重建，LAV 输出 float32 → 16bit PCM，DSP 重新 `SetFormat`，下游收到新格式 |
| MPC-BE + VOB（AC3 5.1） | `QueryAccept=S_OK` → LAV 输出 6ch/48k float32，`STAT-FINAL samples=50 bytes=1.8MB changed=50` |
| MPC-BE + m4a / 24bit FLAC / 5.1 MKV | `changed` 随样本增长（107/110、28/29、141/142），DSP 确实改了数据 |
| MPC-BE + LAV 解不了的 APE | `QueryAccept=S_FALSE` → 当场让位，无 `ConnectDirect` 失败，不再拖崩整张图 |
| 诊断日志 | `%TEMP%\bassmax_decoder.log` 里 `STAT ... changed=N ... enabled=1` —— `changed` 增长且 `enabled=1` 才算生效 |

> 测试宿主的 `--switch` 要用**同格式**文件（裸 `File Source (Async)` 需要图管理器自动插 Splitter）；换成结构不同的容器会得到 `VFW_E_CANNOT_CONNECT`，那是宿主脚本的局限，不是滤镜回归。

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
