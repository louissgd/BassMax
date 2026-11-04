# bassmax_cli — BassMax v4 离线转码器

和 DirectShow 滤镜、Chrome 扩展共用同一份 `bassmax_core.hpp`，DSP 一行没改。区别只是它不实时，跑多快看 CPU：

| 场景 | 速度 |
|---|---|
| 44.1 kHz 立体声 | **165–210x** 实时（10 分钟音乐 3.6 秒） |
| 48 kHz 立体声 | 105x |
| 44.1 kHz 5.1 | 70x |
| 152 MB 输入的内存峰值 | **7 MB**（全程流式，不吃整文件） |

单文件 `bassmax_cli.exe`，283 KB，`/MT` 静态链接，不依赖 VC 运行库，拷到哪都能跑。

---

## 快速开始

```bat
:: WAV 进 WAV 出，输出到 music.bmx.wav
bassmax_cli.exe music.wav --drive 8.5 --bass 3 --preset 爵士乐

:: 完整媒体文件（mp4/mkv/flac/mp3…）：视频流原样复制，只重做音轨
bmx.cmd "D:\MV\old.mkv" --drive 9 --bass 4 --preset rock

:: 任意输入 + 进度百分比 + f32 输出（交给 Audition 继续处理）
bmrun.cmd song.flac --drive 10 --bass 2 --treble 2 --preset 爵士乐
```

第一个命令没有任何 ffmpeg 参与。后两个是包装脚本，负责解码和封装。

---

## 输入 / 输出

**输入**

| 来源 | 写法 | 说明 |
|---|---|---|
| WAV 文件 | `bassmax_cli in.wav` | PCM 8/16/24/32 位 + IEEE float 32/64 位，支持 `WAVE_FORMAT_EXTENSIBLE` |
| 标准输入（WAV） | `cat in.wav \| bassmax_cli -` | 自动嗅探 RIFF 头，采样率/声道从头里取 |
| 标准输入（裸 PCM） | `ffmpeg ... -f f32le - \| bassmax_cli - --in-sr 44100 --in-ch 2` | 需要 `--in-fmt`（默认 f32le）+ 采样率 + 声道数 |

**输出**

| 目标 | 写法 |
|---|---|
| WAV 文件 | `-o out.wav`（默认 24 位，`--out-bits 16\|24\|32\|f32\|f64`） |
| 标准输出（WAV） | `-o -` |
| 标准输出（裸 PCM） | `-o - --out-fmt f32le` + ffmpeg 用 `-f f32le -ar ... -ac ...` 接收 |

省略 `-o` 时：输入是文件 → 生成 `原名.bmx.wav`；输入是管道 → 写 stdout。

采样率、声道数、位深一律**透传**，不做重采样。这是 ffmpeg 的活，别在这里做。

---

## 进度显示

在终端里默认就显示，被重定向时自动关掉（不污染日志）。一行原地刷新：

```
[bmx]  42.3%  00:01:52.3 / 00:04:26.0   168.4x  ETA 00:00:20.5
```

有百分比才有 ETA。**总时长从哪来**：

| 输入 | 总时长 | 显示 |
|---|---|---|
| WAV 文件 | 从 data 块长度算，精确 | 百分比 + ETA |
| 管道（`ffmpeg ... - \| ...`） | **未知** | 已处理时长 / 速度 / 已用时间 |
| 管道 + `--total 3:45` | 手动给定 | 百分比 + ETA |

管道拿不到总时长是格式决定的：ffmpeg 往管道写 WAV 时无法回填长度，只能填 `0xFFFFFFFF`。想要百分比就传 `--total`：

```bat
ffmpeg -i in.flac -f wav - | bassmax_cli - --total 3:45 --drive 10 --bass 2 -o out.wav
```

`--total` 接受秒（`225`）、`mm:ss`（`3:45`）、`hh:mm:ss`（`0:03:45`）三种写法。

| 参数 | 说明 |
|---|---|
| `--progress` | 强制开启（重定向到文件时也想看） |
| `--no-progress` | 关掉 |
| `--progress-interval <ms>` | 刷新间隔，默认 500；嫌刷得快可以拉到 2000 |
| `--quiet` | 过程信息和进度一起关，只剩错误 |

进度走 stderr，**不会**混进 stdout 的音频数据（已验证 `-o -` 时输出逐位不变）。

`bmrun.cmd` 把这一步自动做掉：先读一次 ffmpeg 的 `Duration` 行，作为 `--total` 传下去，所以用它就有百分比。

---

## 音色参数

| 参数 | 范围 | 默认 | 说明 |
|---|---|---|---|
| `--drive` | 0–10 | 0 | 电平拉伸量，**与界面刻度一致**（8.5 = 界面上的 8.5）. 0 = 旁路 |
| `--bass` | ±12 dB | 0 | 100 Hz 低架 |
| `--treble` | ±12 dB | 0 | 9 kHz 高架，**后置**在限幅之后，不进电平检测 → 只改音色不改动态 |
| `--preset` | 0–5 或名字 | 0 | `标准乐 古典乐 俱乐部 流行乐 爵士乐 摇滚乐`（也认 `standard classical club pop jazz rock`） |
| `--makeup` | 0–12 dB | 0 | 末端总增益，0 表示用算法默认 2.5 dB |
| `--trim1k` | ±12 dB | 0 | 900 Hz 峰值微调 |

`--list-presets` 能看到 6 个预设的完整曲线；`--help` 有全部进阶参数（段饱和上限、包络窗口、攻放时间、限幅器时间常数）。

```bat
bassmax_cli.exe --list-presets
bassmax_cli.exe music.wav --drive 9 --dry-run      :: 只校验参数，不处理
```

---

## 和 ffmpeg 串起来

解码 → 处理 → 编码，两条管道，全程流式：

```bat
:: 任意格式 → 处理后编码成 FLAC
ffmpeg -i in.mp4 -map 0:a:0 -f f32le - ^
 | bassmax_cli - --in-sr 48000 --in-ch 2 -o - --out-fmt f32le --drive 8.5 ^
 | ffmpeg -f f32le -ar 48000 -ac 2 -i - -c:a flac out.flac
```

只处理一段（片段切片由 ffmpeg 做，别在 DSP 上想办法）：

```bat
ffmpeg -ss 00:01:30 -t 40 -i song.flac -f f32le - | bassmax_cli - --in-sr 44100 --in-ch 2 -o cut.wav --drive 9
```

**建议**：管道之间用 `--out-fmt f32le`（裸 PCM），别用 WAV。WAV 过管道时长度未知，只能写 `0xFFFFFFFF` 占位；输入长度已知时本工具会算准尺寸，但裸 PCM 永远没有这个问题，还省掉头解析。

---

## bmx.cmd（媒体文件一键处理）

```bat
bmx.cmd <输入> [-o <输出>] [bassmax 参数...]

bmx.cmd old.mkv                       → old.bmx.mkv   (视频 copy, 音轨 flac)
bmx.cmd clip.mp4 --drive 8 --bass 3   → clip.bmx.mp4  (视频 copy, 音轨 aac 320k)
bmx.cmd music.flac --preset jazz       → music.bmx.flac
```

输出编码按**输出后缀**选：`mp4/mov`→aac 320k + faststart、`mkv`→flac、`webm`→opus、`wav`→pcm_s24le、`flac`→flac、`mp3`→320k、`m4a/aac`→aac、`ogg/opus` 各自对应。视频分支用 `-map 0:v:0?`，输入没有视频流也能跑。

脚本里 ffmpeg 路径默认写死 `D:\Program Files\ffmpeg\ffmpeg.exe`，找不到就退回 PATH 里的 `ffmpeg.exe`。中间 WAV 落在 `%TEMP%`，跑完删掉。没有音轨的输入会明确报错，不会产出一个没声音的文件。

---

## bmrun.cmd（只做音效、输出交给音频编辑器）

如果你的流程是「任何格式 → 加音效 → 进 Audition 继续修」，用这个：

```bat
bmrun.cmd <输入> [-o <输出>] [--af "ffmpeg 滤镜"] [bassmax 参数...]

bmrun.cmd song.flac --drive 10 --bass 2 --treble 2 --preset 爵士乐
bmrun.cmd "D:\MV\a.mkv" -o a.wav --drive 9 --af "highpass=f=30"
```

和手写 `ffmpeg ... | bassmax_cli ...` 相比，它多做三件事：

1. **自动补 `--total`** —— 解析 ffmpeg 的 `Duration` 行，进度直接带百分比和 ETA。
2. **默认 `--out-bits f32`** —— 后面还要进 Audition 做增益、压限、降噪，中间量化到 24 位是白扔精度。要 24 位就显式写 `--out-bits 24`。
3. **默认输出 `<输入名>.bmx.wav`** —— 直接从管道读时省略 `-o` 会往 stdout 吐，脚本里帮你兜住。

`--af` 的滤镜串在 BassMax **之前**执行（`-af` 作用在 ffmpeg 的输出流上），所以 `--af loudnorm` 是先把素材拉到目标响度再染色，`--af highpass=f=30` 是先进来切掉次声。想放到 BassMax **之后**就得自己串两条管道，`bmrun.cmd` 不支持。

**关于你原来那条命令的一个隐患**：

```bat
ffmpeg -i in.flac -f wav - | bassmax_cli - ...
```

`-f wav` 的默认编码是 `pcm_s16le`。24 位母带这样进管道会先被砍成 16 位（96 dB 动态范围），BassMax 再把低电平段落往上拉的时候，量化噪声跟着一起被拉。加上 `-c:a pcm_f32le` 就没事了，`bmrun.cmd` 里已经是这么写的。

---

## 验证

移植没有"听起来差不多"这一说，都是逐样本比的：

| 对比 | 条件 | 结果 |
|---|---|---|
| 本工具 ⇄ C++ `render_wav.exe`（同一个 `bassmax_core.hpp`） | 8 s 合成信号 48 kHz，驱动 8.5 / 低音 +2 / 爵士乐 / 9k +3 / 1k +1.5 | **≤1 LSB**（24 位量化单位，只是舍入方式差异：`lrintf` vs 四舍五入） |
| WAV 直读 ⇄ ffmpeg `f32le` 管道 | 同上参数，44.1 kHz 真实音乐 | **逐位相同**（0 差异） |
| 文件输入 ⇄ stdin 输入 | 同一 WAV 走 `cat \|` | **逐位相同** |
| `--block 128/4096/65536` | 同一输入 | **逐位相同**（DSP 与分块大小无关） |
| 写 stdout 后再让 ffmpeg 读回 | 管道 + 重定向两种情况 | **逐位相同** |
| 开进度 ⇄ `--quiet`（都是 `-o -` 写 stdout） | 同一输入 | **逐位相同**（进度只走 stderr） |
| `bmrun.cmd` ⇄ 直接 f32 管道处理 | 180 s 音乐，驱动 10 | **逐位相同** |

复现脚本在 `test/`：

```bash
bash build.sh                                  # 编译
cd test
python trim_wav.py "原曲.wav" music.wav 30 8
../dist/bassmax_cli.exe music.wav -o cli.wav --drive 8.5 --bass 2 --preset 4 --treble 3 --trim1k 1.5
BASSMAX_DRIVE=0.85 BASSMAX_BASS=2 BASSMAX_PRESET=4 BASSMAX_TRIM_9K=3 BASSMAX_TRIM_1K=1.5 \
  ./render_ref.exe music.wav ref.wav 0.85 2        # C++ 参考
python compare_wav.py ref.wav cli.wav
```

`render_ref.exe` 就是 `Claw/render_wav.cpp` 编出来的，用 MSVC 编译（见 `test/build_ref.sh` 思路，与 Chrome 扩展那份一致）。

---

## 边界与坑

1. **不重采样、不改声道布局**。要转换先用 ffmpeg。
2. **输入 >2 声道时驱动上限压到 0.60**（和滤镜同一条规则：每对声道各自拉满电平，下混后必然过 0 dBFS）。发生时会打印提示。
3. **压缩格式直接喂会报错**，提示你先用 ffmpeg 解码。这是设计：本工具只接 PCM/float。
4. **`--drive` 是 0–10 的界面刻度**，不是 0–1 的内部值。写 `--drive 0.85` 等于界面的 0.85，不是 85%。用 `--dry-run` 能立刻看到换算结果。
5. **写 stdout 的 WAV 长度未知时**会填 `0xFFFFFFFF`，ffmpeg 能读（会提示一句 "Ignoring maximum wav data size"）。介意就用 `--out-fmt f32le`。
6. 只处理音频。视频流要复制/重编码交给 ffmpeg 或 `bmx.cmd`。
7. 8000–1536000 Hz 之外直接拒绝；≥176.4 kHz 时核心会自动挂上那级 8 阶超声保护（DSD 解码后的噪声整形残留会毁掉 tanh 段）。
8. **`ffmpeg -f wav -` 默认是 16 位**（`pcm_s16le`），不是跟随源格式。日志里能直接看出来：`flac (native) -> pcm_s16le (native)`、`[bmx] in : ... s16`。`-af aformat=sample_fmts=s32` **不管用**——它只改滤镜图输出的样本格式，wav 封装器的默认编码器仍是 `pcm_s16le`，编码前又被转回 16 位；必须指定编码器本体 `-c:a pcm_f32le` 或 `-c:a pcm_s32le`。注意输出文件头写的 24 位是**本工具的默认输出位深**，跟管道里流过多少位无关。
   实测（24 位 FLAC，驱动 10，爵士乐）：s16 管道与 f32 管道的成品差异 RMS **−74.9 dBFS**、峰值 **−56.3 dBFS**；源本身相对 16 位网格的残差是 −101.1 dBFS（标准 24 位量化底噪），也就是说被砍掉的那 8 位是真内容，不是 padding。
9. `--total` 只影响显示：猜大了百分比到不了 100%，猜小了会提前显示 100%，音频本身不受影响。
10. `bmx.cmd` / `bmrun.cmd` 由 `cmd` 解析，文件必须是 **CRLF 换行**；LF 换行会让 `goto` / `if (...)` 块报"命令语法不正确"。

---

## 目录

```
bassmax_cli.cpp     源码 (单文件, 约 1130 行)
build.sh            MSVC 编译脚本 (/MT 静态链接)
bmx.cmd             媒体文件包装脚本 (视频 copy + 音轨重编码)
bmrun.cmd           音效专用包装脚本 (自动 --total, 默认 f32 输出)
dist/bassmax_cli.exe 产物
test/               验证脚本与样例
```
