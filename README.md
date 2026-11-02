# BassMax — 64 位通用重写 (Clean-room)

> **⚠ 本文是早期设计文档，部分内容已过时**（写于 v1 阶段：旧 10 点曲线表、驱动以百分比粗调、
> 曾计划同时出 VST3、目录里提到的 `bassmax_model.py` / `verify.py` / `measure_out/` /
> `test_signals/` 已在清理中删除）。
>
> **当前实况以 [`BACKUP.md`](BACKUP.md) 为准**：简化拉伸模型、三段分段参数、高频独立时间常数、
> 6 个 EQ 预设、驱动 0–10 分档、定稿默认值、注册表项、三曲评测、快照与回退方式。
> 本文的「为什么是动态而不是均衡+限幅」「构建 / 注册 / 在播放器里使用」等章节仍然有效。


把一个 20 年前的 32 位 Winamp DSP 插件（iZotope 早期的 **OctiMax** 音频增强器）通过「黑盒测量 → 从零重写」的方式，做成可在现代 64 位播放器里运行的通用 DSP。产物命名为 **BassMax**，避免与那个仍在流通的商业老插件重名。

- **DirectShow 音频变换滤镜 (`bassmax_dshow.ax`)** —— 供 **MPC-HC / PotPlayer** 等 DirectShow 播放器
- **VST3 插件 (`BassMax.vst3`)** —— 供 **foobar2000** 及任何 VST3 宿主

原始插件受版权保护（企业作品，中国约 50 年 / 美国 95 年），因此本项目**不包含、不反编译、不复制**原插件任何代码；仅通过输入/输出音频的测量结果，独立重写等效算法。核心 DSP 见 `bassmax_core.hpp`（唯一权威实现）；离线 A/B 与参数标定用 `render_wav.cpp`（把 WAV 过一遍核心，直接和原插件处理结果做统计比对）。

---

## 一、目录结构

```
Claw/
├── bassmax_core.hpp          # ★ DSP 核心 (header-only, 框架无关) 唯一权威实现
├── bassmax_model.py          # Python 参考模型 (与 C++ 完全对应)
├── verify.py                 # 参考模型 vs 原始插件实测 的对比脚本
├── measure_out/              # 原始插件对各类测试信号的实测输出 (黑盒)
├── test_signals/             # 合成测试信号 (脉冲/正弦/噪声/方波/扫频/直流)
│
├── bassmax_dshow/            # ★ DirectShow 滤镜工程
│   ├── bassmax_core.hpp      #   (核心副本)
│   ├── bassmax_filter.h/.cpp #   CTransformFilter 子类, 格式协商/PCM↔float 转换, ISpecifyPropertyPages
│   ├── bassmax_page.h/.cpp   #   属性页 (CBasePropertyPage)
│   ├── bassmax_tray.h/.cpp   #   托盘图标 + 设置窗口
│   ├── bassmax_settings.h/.cpp # 全局参数 (线程安全 + 注册表持久化 + 热更新代号)
│   ├── bassmax_ui.h          #   属性页/托盘窗口共用的控件读写
│   ├── bassmax.rc / resource.h # 对话框与字符串资源
│   ├── dllmain.cpp           #   COM 入口 + 类厂 (滤镜 + 属性页两个 CLSID) + 自注册
│   ├── baseclasses/          #   Microsoft DirectShow 基础类 (MIT 样例代码)
│   ├── install.bat / uninstall.bat # 管理员一键注册 / 卸载 (带校验)
│   ├── build.sh              #   一键构建
│   └── bassmax_dshow.ax      #   ★ 构建产物 (64 位)
│
└── bassmax_vst3/             # ★ VST3 工程 (JUCE)
    ├── Source/
    │   ├── bassmax_core.hpp
    │   ├── PluginProcessor.h/.cpp   # AudioProcessor: 交错↔planar, 参数, 状态
    │   └── PluginEditor.h/.cpp      # 界面: 低音 / 驱动 两个滑块
    ├── CMakeLists.txt        #   juce_add_plugin, 仅 VST3 (可加 Standalone)
    ├── build.sh              #   一键构建 (NMake + 本地 JUCE)
    └── build_nm/BassMax_artefacts/Release/VST3/BassMax.vst3   # ★ 构建产物 (64 位)
```

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
regsvr32 "C:\...\bassmax_dshow\bassmax_dshow.ax"
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
- 声道：1 ~ 8（单声道、2.0、5.1、7.1 都支持），按声道对处理；超过 8 声道明确拒绝，不会硬算
- 采样率：8 kHz ~ 768 kHz

> 之前"播放器偶尔卡住、然后自动把滤镜卸掉"的根因：位深按**有效位深**而不是**容器位深**算步长（24-in-32 会算错 → 帧长偏移 → 越界写 → 崩溃/挂死），并且输出缓冲没有上限检查。v2 里所有读写都按 `GetSize()` 钳位，指针/长度全程校验。

参数：默认为 `低音=+4.5dB、驱动=45%`（首次运行会把 v1 的旧「驱动」值迁移成 45%，因为旋钮含义从「推入限幅器」改成了「动态增强量」）。所有参数存在 `HKCU\Software\BassMax`（`Bass`/`Drive`/`TrayIcon` 字符串值），属性页与托盘界面改的就是它，多播放器共用同一份。

### 设置界面（三种入口）

**1) 播放器里的标准属性页**（DirectShow `ISpecifyPropertyPages`，属性页 CLSID `{C41F7D22-6B18-4A93-8E52-1D7A4F60C9B2}`，与滤镜同一个 .ax 导出）
- MPC-HC：播放时**右键画面 → 滤镜 → BassMax Audio Enhancer**，即弹出属性页
- PotPlayer：右键画面 → 滤镜 → 选中该滤镜 → 属性
- 两个滑块：**低音增强**（−12 … +12 dB，120 Hz 以下低频段增益）、**驱动**（0 … 100%：动态增强量 = 向上压缩深度 + 响度补偿。0 = 只做低音均衡，45% 接近原插件默认听感，100% 最强）
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

## 四、VST3 插件 (foobar2000 等)

### 构建（本机已装 JUCE 于 `D:\Program Files\JUCE`）
```bash
cd bassmax_vst3
bash build.sh          # NMake + 本地 JUCE；可加 JUCE_DIR=... 覆盖
# 产物: build_nm/BassMax_artefacts/Release/VST3/BassMax.vst3
```
- CMake 用 **NMake Makefiles** 生成器 + 显式 `cl.exe`/`nmake.exe`（VS 生成器在本机沙箱下探测不到工具集）。
- 源码含中文注释，**必须** `/utf-8` 编译（否则 GBK 误读注释会把后续代码“吃掉”）。CMake 中已加 `target_compile_options(BassMax PRIVATE /utf-8)`。

### 安装 / 使用
把 `BassMax.vst3` 整个文件夹复制到：
- 通用 VST3 目录：`C:\Program Files\Common Files\VST3\`
- 或 foobar2000 的 VST 目录（视你的 VST 组件而定）

**foobar2000** 使用 VST：`Preferences → Playback → DSP Manager`（或安装 VST 适配组件）→ 添加 VST/VST3 → 选择 BassMax。界面上有 **低音(Bass, 0–12dB)** 与 **驱动(Drive, 0–1)** 两个滑块；参数会随工程状态保存。

### 冒烟测试（已验证）
`GetPluginFactory` 返回有效工厂；DLL 为 x64，导出 `GetPluginFactory/InitDll/ExitDll`。

---

## 五、验证结果汇总

| 项目 | 结果 |
|---|---|
| DSP 核心 vs 原插件（真实歌曲配对） | 响度轨迹相关 0.90、峰值因数 3.40 vs 3.37、频段偏差 <1.7dB |
| DirectShow .ax 构建 | ✅ x64，导出 4 个 COM 入口，加载/实例化/自注册全部通过 |
| VST3 构建 | ✅ x64，导出 VST3 入口，加载/取工厂通过 |

*备注：v1 曾把原插件的「安静段抬升」误判成「电平相关 auto-gain 噪声」，用恒定 makeup 把它抹掉了 —— 现在已按配对测量还原成向上压缩。*
