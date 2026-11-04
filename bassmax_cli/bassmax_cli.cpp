// bassmax_cli.cpp — BassMax v4 离线转码器
//
// 把 bassmax_dshow/bassmax_core.hpp 包成一个命令行程序:
//   WAV 文件  ─┐
//   原始 PCM  ─┴─> 三段电平拉伸 ─> WAV / 原始 PCM
//   管线 (stdin)                   (stdout, 可接 ffmpeg 编码)
//
// 设计要点:
//   * 全程流式: 按块读写, 内存占用只跟 --block 有关, 不吃整文件
//   * 位深/声道/采样率一律透传, 不做重采样 (重采样是 ffmpeg 的事)
//   * 1~8 声道按对处理 (与 DirectShow 滤镜一致); >2 声道时驱动上限压到 0.60,
//     否则每对各自拉满电平, 下混后必然爆表 -> 限幅器反复触发, 听感发糊
//   * 输出到不可 seek 的 stdout 时 WAV 尺寸字段写 0xFFFFFFFF, ffmpeg 能读到 EOF
//
// 编译: 见 build.sh (MSVC, /MT 静态链接, 无外部依赖)
#define _CRT_SECURE_NO_WARNINGS
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <io.h>
#include <fcntl.h>
#include <windows.h>

#include "bassmax_core.hpp"

// ---------------------------------------------------------------------------
// 基础
// ---------------------------------------------------------------------------
enum class SampFmt { U8, S16, S24, S32, F32, F64 };

static int bytesOf(SampFmt f) {
    switch (f) {
        case SampFmt::U8:  return 1;
        case SampFmt::S16: return 2;
        case SampFmt::S24: return 3;
        case SampFmt::S32: return 4;
        case SampFmt::F32: return 4;
        case SampFmt::F64: return 8;
    }
    return 4;
}

static const char* nameOf(SampFmt f) {
    switch (f) {
        case SampFmt::U8:  return "u8";
        case SampFmt::S16: return "s16";
        case SampFmt::S24: return "s24";
        case SampFmt::S32: return "s32";
        case SampFmt::F32: return "f32";
        case SampFmt::F64: return "f64";
    }
    return "?";
}

static bool isFloatFmt(SampFmt f) { return f == SampFmt::F32 || f == SampFmt::F64; }

struct AudioFmt {
    int      ch = 2;
    uint32_t sr = 48000;
    SampFmt  samp = SampFmt::F32;
    int bytes() const { return bytesOf(samp); }
    int frameBytes() const { return ch * bytes(); }
};

// ---- 控制台输出 (UTF-8 -> 控制台时转宽字符, 避免中文乱码) ----
static void say(const char* utf8, FILE* to = stderr) {
    int fd = _fileno(to);
    if (_isatty(fd)) {
        int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
        if (n > 0) {
            std::vector<wchar_t> w((size_t)n);
            MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w.data(), n);
            DWORD written = 0;
            WriteConsoleW((HANDLE)_get_osfhandle(fd), w.data(), (DWORD)(n - 1), &written, nullptr);
            return;
        }
    }
    fwrite(utf8, 1, strlen(utf8), to);
    fflush(to);
}

static std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static bool fileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// ---------------------------------------------------------------------------
// 输入流: 文件或 stdin, 支持"回推"已读字节 (用于格式嗅探)
// ---------------------------------------------------------------------------
class InStream {
public:
    ~InStream() { close(); }

    bool openFile(const std::wstring& path, std::string& err) {
        f_ = _wfopen(path.c_str(), L"rb");
        if (!f_) { err = "打不开输入文件"; return false; }
        return true;
    }
    void attachStdin() {
        _setmode(_fileno(stdin), _O_BINARY);
        f_ = stdin;
        ownStdin_ = true;
    }
    bool isStdin() const { return f_ == stdin; }

    void close() {
        if (f_ && !ownStdin_) fclose(f_);
        f_ = nullptr;
    }

    // 返回实际读到的字节数 (短读 = 到末尾)
    size_t read(void* dst, size_t bytes) {
        unsigned char* p = static_cast<unsigned char*>(dst);
        size_t got = 0;
        while (got < bytes && prePos_ < preLen_) p[got++] = pre_[prePos_++];
        if (got < bytes && f_) {
            size_t r = fread(p + got, 1, bytes - got, f_);
            got += r;
        }
        return got;
    }
    bool readExact(void* dst, size_t bytes) { return read(dst, bytes) == bytes; }

    // 只读不前进: 取前 n 字节
    bool peek(unsigned char* dst, size_t n) {
        if (preLen_ - prePos_ >= n) { memcpy(dst, pre_.data() + prePos_, n); return true; }
        // 简单实现: 把剩余回推区 + 新读的一起放进回推区
        std::vector<unsigned char> tmp(n);
        size_t have = preLen_ - prePos_;
        if (have) memcpy(tmp.data(), pre_.data() + prePos_, have);
        if (f_) {
            size_t r = fread(tmp.data() + have, 1, n - have, f_);
            have += r;
            if (have < n) return false;
        } else if (have < n) {
            return false;
        }
        pre_ = tmp;
        preLen_ = n;
        prePos_ = 0;
        memcpy(dst, pre_.data(), n);
        return true;
    }

    void skip(uint64_t bytes) {
        unsigned char tmp[4096];
        while (bytes > 0) {
            size_t n = (size_t)(bytes > sizeof(tmp) ? sizeof(tmp) : bytes);
            size_t r = read(tmp, n);
            if (r == 0) return;
            bytes -= r;
        }
    }

private:
    FILE* f_ = nullptr;
    bool  ownStdin_ = false;
    std::vector<unsigned char> pre_;
    size_t preLen_ = 0, prePos_ = 0;
};

// ---------------------------------------------------------------------------
// 采样值转换
// ---------------------------------------------------------------------------
static inline uint16_t rdU16(const unsigned char* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t rdU32(const unsigned char* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// 解码一整个块 (交错) 成 float [-1,1]
static void decodeBlock(const unsigned char* src, size_t nsamp, SampFmt f, float* dst) {
    switch (f) {
        case SampFmt::U8:
            for (size_t i = 0; i < nsamp; ++i) dst[i] = ((float)src[i] - 128.0f) * (1.0f / 128.0f);
            break;
        case SampFmt::S16: {
            const int16_t* s = reinterpret_cast<const int16_t*>(src);
            for (size_t i = 0; i < nsamp; ++i) dst[i] = (float)s[i] * (1.0f / 32768.0f);
            break;
        }
        case SampFmt::S24:
            for (size_t i = 0; i < nsamp; ++i) {
                const unsigned char* p = src + i * 3;
                int32_t v = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16));
                if (v & 0x800000) v -= 0x1000000;
                dst[i] = (float)v * (1.0f / 8388608.0f);
            }
            break;
        case SampFmt::S32: {
            const int32_t* s = reinterpret_cast<const int32_t*>(src);
            for (size_t i = 0; i < nsamp; ++i) dst[i] = (float)((double)s[i] / 2147483648.0);
            break;
        }
        case SampFmt::F32: {
            const float* s = reinterpret_cast<const float*>(src);
            for (size_t i = 0; i < nsamp; ++i) dst[i] = s[i];
            break;
        }
        case SampFmt::F64: {
            const double* s = reinterpret_cast<const double*>(src);
            for (size_t i = 0; i < nsamp; ++i) dst[i] = (float)s[i];
            break;
        }
    }
}

static inline int32_t clipI32(double v, int32_t lo, int32_t hi) {
    if (!(v == v)) v = 0.0;                 // NaN
    if (v < (double)lo) return lo;
    if (v > (double)hi) return hi;
    return (int32_t)v;
}

// 编码一个块; 整数格式做峰值钳位, 不让 NaN/超幅值穿透
static void encodeBlock(const float* src, size_t nsamp, SampFmt f, unsigned char* dst) {
    switch (f) {
        case SampFmt::U8:
            for (size_t i = 0; i < nsamp; ++i) {
                double v = (double)src[i] * 127.0;
                dst[i] = (unsigned char)(clipI32(v, -127, 127) + 128);
            }
            break;
        case SampFmt::S16: {
            int16_t* d = reinterpret_cast<int16_t*>(dst);
            for (size_t i = 0; i < nsamp; ++i) {
                double v = (double)src[i] * 32767.0;
                d[i] = (int16_t)clipI32(v < 0 ? v - 0.5 : v + 0.5, -32768, 32767);
            }
            break;
        }
        case SampFmt::S24:
            for (size_t i = 0; i < nsamp; ++i) {
                double v = (double)src[i] * 8388607.0;
                int32_t s = clipI32(v < 0 ? v - 0.5 : v + 0.5, -8388608, 8388607);
                unsigned char* p = dst + i * 3;
                p[0] = (unsigned char)(s & 0xff);
                p[1] = (unsigned char)((s >> 8) & 0xff);
                p[2] = (unsigned char)((s >> 16) & 0xff);
            }
            break;
        case SampFmt::S32: {
            int32_t* d = reinterpret_cast<int32_t*>(dst);
            for (size_t i = 0; i < nsamp; ++i) {
                double v = (double)src[i] * 2147483647.0;
                d[i] = clipI32(v < 0 ? v - 0.5 : v + 0.5, -2147483647 - 1, 2147483647);
            }
            break;
        }
        case SampFmt::F32: {
            float* d = reinterpret_cast<float*>(dst);
            for (size_t i = 0; i < nsamp; ++i) { float v = src[i]; d[i] = (v == v) ? v : 0.0f; }
            break;
        }
        case SampFmt::F64: {
            double* d = reinterpret_cast<double*>(dst);
            for (size_t i = 0; i < nsamp; ++i) { double v = (double)src[i]; d[i] = (v == v) ? v : 0.0; }
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// WAV 解析
// ---------------------------------------------------------------------------
struct Source {
    InStream in;
    AudioFmt fmt;
    uint64_t dataBytes = 0;     // 剩余可读的数据字节 (未知长度时给 UINT64_MAX)
    bool     wav = false;
    std::string label;
};

static bool parseFmtChunk(const unsigned char* p, size_t len, AudioFmt& f, std::string& err) {
    if (len < 16) { err = "fmt 块太短"; return false; }
    uint16_t tag = rdU16(p);
    f.ch = rdU16(p + 2);
    f.sr = rdU32(p + 4);
    uint16_t align = rdU16(p + 12);
    uint16_t bits = rdU16(p + 14);

    bool isFloat = false;
    if (tag == 0xFFFE) {                     // WAVEFORMATEXTENSIBLE
        if (len < 40) { err = "WAVEFORMATEXTENSIBLE 长度不足"; return false; }
        uint16_t sub = rdU16(p + 24);        // SubFormat GUID 的前两个字节
        if (sub == 3) isFloat = true;
        else if (sub != 1) { err = "不支持的子格式 (仅 PCM / IEEE float)"; return false; }
    } else if (tag == 3) {
        isFloat = true;
    } else if (tag != 1) {
        err = "不支持的 WAV 编码 (仅 PCM / IEEE float; 压缩格式请先让 ffmpeg 解码)";
        return false;
    }

    if (isFloat) {
        if (bits == 32) f.samp = SampFmt::F32;
        else if (bits == 64) f.samp = SampFmt::F64;
        else { err = "浮点 WAV 只支持 32/64 位"; return false; }
    } else {
        if (bits == 8) f.samp = SampFmt::U8;
        else if (bits == 16) f.samp = SampFmt::S16;
        else if (bits == 24) f.samp = SampFmt::S24;
        else if (bits == 32) f.samp = SampFmt::S32;
        else { err = "整数 PCM 只支持 8/16/24/32 位"; return false; }
    }

    if (f.ch < 1 || f.ch > 8) { err = "声道数只支持 1~8"; return false; }
    if (f.sr < 8000 || f.sr > 1536000) { err = "采样率只支持 8000~1536000 Hz"; return false; }
    if (align != 0 && align != f.frameBytes()) { err = "块对齐与位深/声道数不一致"; return false; }
    return true;
}

// 从当前流位置解析 WAV 头, 停在 data 起始处
static bool parseWav(Source& s, std::string& err) {
    unsigned char hdr[12];
    if (!s.in.readExact(hdr, 12)) { err = "文件太短, 不是 WAV"; return false; }
    if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) {
        err = "不是 RIFF/WAVE 文件";
        return false;
    }
    bool haveFmt = false;
    for (;;) {
        unsigned char ch[8];
        if (!s.in.readExact(ch, 8)) { err = "找不到 data 块"; return false; }
        uint32_t size = rdU32(ch + 4);
        if (memcmp(ch, "fmt ", 4) == 0) {
            unsigned char buf[64] = {0};
            size_t take = size < sizeof(buf) ? size : sizeof(buf);
            if (!s.in.readExact(buf, take)) { err = "fmt 块截断"; return false; }
            if (size > take) s.in.skip(size - take);
            if (!parseFmtChunk(buf, take, s.fmt, err)) return false;
            haveFmt = true;
            if (size & 1) s.in.skip(1);
        } else if (memcmp(ch, "data", 4) == 0) {
            if (!haveFmt) { err = "data 块出现在 fmt 之前"; return false; }
            if (size == 0 || size == 0xFFFFFFFFu) s.dataBytes = UINT64_MAX;   // 长度未知, 读到 EOF
            else s.dataBytes = size;
            s.wav = true;
            return true;
        } else {
            s.in.skip(size + (size & 1));
        }
    }
}

// 嗅探: stdin 里是不是 WAV
static bool looksLikeWav(InStream& in) {
    unsigned char m[12];
    if (!in.peek(m, 12)) return false;
    return memcmp(m, "RIFF", 4) == 0 && memcmp(m + 8, "WAVE", 4) == 0;
}

// ---------------------------------------------------------------------------
// 输出
// ---------------------------------------------------------------------------
class OutStream {
public:
    ~OutStream() { close(); }

    bool openFile(const std::wstring& path, std::string& err) {
        f_ = _wfopen(path.c_str(), L"wb");
        if (!f_) { err = "无法创建输出文件"; return false; }
        return true;
    }
    void attachStdout() {
        _setmode(_fileno(stdout), _O_BINARY);
        f_ = stdout;
        ownStdout_ = true;
        // stdout 被重定向到文件时其实可以 seek -> 能回填 WAV 尺寸; 接管道则不能
        fflush(stdout);
        long long pos = _ftelli64(stdout);
        seekProbe_ = (pos >= 0) && (_fseeki64(stdout, pos, SEEK_SET) == 0);
    }
    bool seekable() const { return f_ && (!ownStdout_ || seekProbe_); }

    size_t write(const void* p, size_t n) { return f_ ? fwrite(p, 1, n, f_) : 0; }
    bool seekTo(long long off) { return f_ && _fseeki64(f_, off, SEEK_SET) == 0; }
    long long tell() { return f_ ? _ftelli64(f_) : -1; }
    void flush() { if (f_) fflush(f_); }
    void close() {
        if (f_ && !ownStdout_) fclose(f_);
        f_ = nullptr;
    }

private:
    FILE* f_ = nullptr;
    bool ownStdout_ = false;
    bool seekProbe_ = false;
};

static uint32_t defaultChannelMask(int ch) {
    switch (ch) {
        case 1: return 0x4;          // FC
        case 2: return 0x3;          // FL FR
        case 4: return 0x33;         // quad
        case 6: return 0x3F;         // 5.1
        case 8: return 0x63F;        // 7.1
        default: return 0;
    }
}

class WavWriter {
public:
    // expectedBytes: 输出数据字节数 (未知时传 0)。
    //   能 seek -> 先写 0, 收尾回填;
    //   不能 seek 但长度已知 -> 头部直接写正确尺寸 (管道也能出标准 WAV, ffmpeg 不报警);
    //   都不能 -> 0xFFFFFFFF, 让下游读到 EOF。
    bool begin(OutStream& os, const AudioFmt& fmt, uint64_t expectedBytes, std::string& err) {
        os_ = &os;
        fmt_ = fmt;
        bool ext = (fmt.ch > 2) || isFloatFmt(fmt.samp) || fmt.samp == SampFmt::S32 || fmt.samp == SampFmt::U8;
        ext_ = ext;
        const int bps = fmt.bytes();
        const uint32_t avg = fmt.sr * (uint32_t)fmt.frameBytes();
        const uint32_t hdrTail = ext ? 60u : 36u;      // RIFF 头中除 data 外的字节数

        const bool seekable = os.seekable();
        uint32_t dataSize = 0, riffSize = 0;
        if (!seekable) {
            if (expectedBytes > 0) {
                dataSize = (uint32_t)expectedBytes;
                riffSize = dataSize + hdrTail;
            } else {
                dataSize = 0xFFFFFFFFu;
                riffSize = 0xFFFFFFFFu;
            }
        }

        std::vector<unsigned char> h;
        auto put16 = [&](uint16_t v) { h.push_back((unsigned char)(v & 0xff)); h.push_back((unsigned char)(v >> 8)); };
        auto put32 = [&](uint32_t v) {
            for (int i = 0; i < 4; ++i) h.push_back((unsigned char)((v >> (8 * i)) & 0xff));
        };
        auto putTag = [&](const char* t) { for (int i = 0; i < 4; ++i) h.push_back((unsigned char)t[i]); };

        putTag("RIFF");
        put32(riffSize);                       // seek 时回填
        putTag("WAVE");
        putTag("fmt ");
        put32(ext ? 40u : 16u);
        put16(ext ? (uint16_t)0xFFFE : (uint16_t)(isFloatFmt(fmt.samp) ? 3 : 1));
        put16((uint16_t)fmt.ch);
        put32(fmt.sr);
        put32(avg);
        put16((uint16_t)fmt.frameBytes());
        put16((uint16_t)(bps * 8));
        if (ext) {
            put16(22);                         // cbSize
            put16((uint16_t)(bps * 8));        // validBits
            put32(defaultChannelMask(fmt.ch));
            // KSDATAFORMAT_SUBTYPE_{PCM,IEEE_FLOAT}
            put16((uint16_t)(isFloatFmt(fmt.samp) ? 3 : 1));
            put16(0);
            put32(0x00100000);
            static const unsigned char tail[8] = {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71};
            for (int i = 0; i < 8; ++i) h.push_back(tail[i]);
        }
        putTag("data");
        put32(dataSize);                       // seek 时回填

        if (os.write(h.data(), h.size()) != h.size()) { err = "写文件头失败"; return false; }
        riffOff_ = 4;
        dataOff_ = (long long)h.size() - 4;
        dataBytes_ = 0;
        return true;
    }

    void writeFrames(const float* interleaved, size_t frames) {
        const size_t nsamp = frames * (size_t)fmt_.ch;
        buf_.resize(nsamp * (size_t)fmt_.bytes());
        encodeBlock(interleaved, nsamp, fmt_.samp, buf_.data());
        size_t wrote = os_->write(buf_.data(), buf_.size());
        dataBytes_ += wrote;
    }

    // 收尾: 能 seek 就回填真实尺寸; 不可 seek 时头部已经是 0xFFFFFFFF
    void end() {
        if (!os_) return;
        os_->flush();
        if (os_->seekable()) {
            unsigned char b[4];
            b[0] = (unsigned char)((dataBytes_) & 0xff);
            b[1] = (unsigned char)((dataBytes_ >> 8) & 0xff);
            b[2] = (unsigned char)((dataBytes_ >> 16) & 0xff);
            b[3] = (unsigned char)((dataBytes_ >> 24) & 0xff);
            os_->seekTo(dataOff_);
            os_->write(b, 4);
            uint64_t riff = (uint64_t)dataOff_ + 4 - 8 + dataBytes_;   // 36/60 + data
            b[0] = (unsigned char)(riff & 0xff);
            b[1] = (unsigned char)((riff >> 8) & 0xff);
            b[2] = (unsigned char)((riff >> 16) & 0xff);
            b[3] = (unsigned char)((riff >> 24) & 0xff);
            os_->seekTo(riffOff_);
            os_->write(b, 4);
        }
        os_->flush();
    }

    uint64_t dataBytes() const { return dataBytes_; }

private:
    OutStream* os_ = nullptr;
    AudioFmt fmt_;
    bool ext_ = false;
    long long riffOff_ = 4, dataOff_ = 0;
    uint64_t dataBytes_ = 0;
    std::vector<unsigned char> buf_;
};

class RawWriter {
public:
    bool begin(OutStream& os, const AudioFmt& fmt) { os_ = &os; fmt_ = fmt; return true; }
    void writeFrames(const float* interleaved, size_t frames) {
        const size_t nsamp = frames * (size_t)fmt_.ch;
        buf_.resize(nsamp * (size_t)fmt_.bytes());
        encodeBlock(interleaved, nsamp, fmt_.samp, buf_.data());
        os_->write(buf_.data(), buf_.size());
    }
    void end() { if (os_) os_->flush(); }
    uint64_t dataBytes() const { return 0; }

private:
    OutStream* os_ = nullptr;
    AudioFmt fmt_;
    std::vector<unsigned char> buf_;
};

// ---------------------------------------------------------------------------
// 参数
// ---------------------------------------------------------------------------
struct Options {
    std::string input;              // "" = stdin
    std::string output;             // "" = 自动
    bool        outGiven = false;

    AudioFmt inFmt;                 // 仅原始 PCM 用
    bool     inIsRaw = false;       // --in-fmt 指定了原始格式
    AudioFmt outFmt;                // 仅原始 PCM 输出用
    bool     outIsRaw = false;

    int block = 4096;
    bool quiet = false;
    bool dryRun = false;

    int  progressMode = -1;         // -1 自动(终端才显示) | 0 关 | 1 开
    int  progressMs = 500;
    double totalSec = 0.0;          // >0 = 手动给定的总时长 (管道输入用)

    bassmax::Params p;
    float driveUi = 0.0f;           // 0..10
};

static const char* kUsage =
"BassMax v4 离线转码器  (bassmax_cli)\n"
"\n"
"用法:\n"
"  bassmax_cli [输入] [选项]\n"
"\n"
"输入:\n"
"  <文件.wav>             WAV 文件 (PCM 8/16/24/32 或 IEEE float 32/64)\n"
"  - 或省略               stdin; 自动识别 WAV, 否则按 --in-fmt 当原始 PCM\n"
"  --in-fmt <fmt>         stdin 原始格式: f32le|s16le|s24le|s32le|f64le|u8 (默认 f32le)\n"
"  --in-sr <hz>           stdin 原始采样率 (默认 48000)\n"
"  --in-ch <n>            stdin 原始声道数 (默认 2)\n"
"\n"
"输出:\n"
"  -o, --out <文件|->     省略时: 输入是文件则生成 <名字>.bmx.wav, 输入是管道则写 stdout\n"
"  --out-fmt <fmt>        wav(默认) | f32le|s16le|s24le|s32le|f64le|u8\n"
"  --out-bits <n>         WAV 位深: 16 | 24(默认) | 32 | f32 | f64\n"
"\n"
"音色参数:\n"
"  --drive <0..10>        驱动 (与界面一致, 默认 0 = 旁路)\n"
"  --bass <dB>            低音, -12..12 (100Hz 低架)\n"
"  --treble <dB>          高音, -12..12 (9kHz 高架, 后置, 不改动态)\n"
"  --preset <0..5|名>     EQ 预设: 标准乐 古典乐 俱乐部 流行乐 爵士乐 摇滚乐\n"
"  --makeup <dB>          总增益, 0..12 (0 = 算法默认 2.5)\n"
"  --trim1k <dB>          900Hz 峰值微调, -12..12\n"
"\n"
"进阶参数 (默认值经过三首配对曲拟合, 慎改):\n"
"  --sat-lo/mid/hi <0..1> 段内饱和上限 (默认 0.82 / 0.75 / 0.67)\n"
"  --env <ms>             电平检测窗口 (默认 50; 高频段见 --envhi)\n"
"  --gatt <ms> --grel <ms>  增益收/放时间 (默认 30 / 400)\n"
"  --g2rel <ms>           二级平滑释放 (>0 开启, 默认关)\n"
"  --loatt <ms> --lorel <ms>  低频峰值跟随 (默认 5 / 200)\n"
"  --envhi <ms> --gathi <ms> --grehi <ms>   高频段 (默认 10 / 20 / 150)\n"
"  --envmid <ms> --gattmid <ms> --grelmid <ms>  中频段 (默认同全段)\n"
"  --limatt <ms> --limrel <ms>  限幅器 (默认 0.3 / 90)\n"
"\n"
"其他:\n"
"  --block <帧数>         每次处理的帧数 (默认 4096)\n"
"  --progress             强制显示进度 (默认只在终端显示, 重定向时关)\n"
"  --no-progress          不显示进度\n"
"  --progress-interval <ms>  进度刷新间隔 (默认 500)\n"
"  --total <时间>         手动指定总时长, 管道输入时才有百分比/ETA\n"
"                         支持 秒 / mm:ss / hh:mm:ss\n"
"  --quiet, -q            不打印过程信息和进度\n"
"  --dry-run              只校验参数并打印配置, 不做转码\n"
"  --list-presets         打印预设表\n"
"  -h, --help             本帮助\n"
"  --version              版本\n"
"\n"
"示例:\n"
"  bassmax_cli music.wav --drive 8.5 --bass 3 --preset 爵士乐\n"
"  ffmpeg -i in.flac -f f32le - | bassmax_cli - --in-sr 44100 --in-ch 2 -o out.wav\n"
"  ffmpeg -i in.flac -f wav - | bassmax_cli - --total 3:45 --drive 10 -o out.wav\n"
"  bassmax_cli in.wav --drive 7 -o - --out-fmt f32le | ffmpeg -f f32le -ar 48000 -ac 2 -i - out.flac\n";

static bool parseNum(const std::string& s, double& v) {
    if (s.empty()) return false;
    char* end = nullptr;
    v = strtod(s.c_str(), &end);
    return end && *end == '\0';
}

static bool parseFmtName(const std::string& s, SampFmt& f, bool& isRaw) {
    isRaw = true;
    if (s == "f32" || s == "f32le") { f = SampFmt::F32; return true; }
    if (s == "f64" || s == "f64le") { f = SampFmt::F64; return true; }
    if (s == "s16" || s == "s16le") { f = SampFmt::S16; return true; }
    if (s == "s24" || s == "s24le") { f = SampFmt::S24; return true; }
    if (s == "s32" || s == "s32le") { f = SampFmt::S32; return true; }
    if (s == "u8") { f = SampFmt::U8; return true; }
    isRaw = false;
    return false;
}

static int presetFromName(const std::string& s, bool& ok) {
    ok = true;
    const bassmax::Preset* k = bassmax::presets();
    static const char* en[6] = {"standard", "classical", "club", "pop", "jazz", "rock"};
    for (int i = 0; i < 6; ++i) {
        if (s == k[i].name || s == en[i]) return i;
    }
    // 允许只写"爵士""摇滚"这类前缀
    for (int i = 0; i < 6; ++i) {
        std::string cn = k[i].name;
        if (!s.empty() && cn.size() >= 2 && cn.compare(0, s.size(), s) == 0) return i;
    }
    ok = false;
    return 0;
}

// ---------------------------------------------------------------------------
// 主流程
// ---------------------------------------------------------------------------
static double nowSec() {
    static LARGE_INTEGER freq = {0};
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)freq.QuadPart;
}

// ---------------------------------------------------------------------------
// 进度显示
//   有总时长 -> "42.3%  00:01:52.3 / 00:04:26.0  8.2x  ETA 00:00:20.5"
//   无总时长 -> "00:01:52.3  8.2x  elapsed 00:00:13.0"
//   终端: \r 原地刷新; 重定向到文件/管道: 每次一行, 便于日志
// ---------------------------------------------------------------------------
static void fmtHms(char* out, size_t n, double sec) {
    if (sec < 0) sec = 0;
    long long t = (long long)(sec * 10.0 + 0.5);      // 0.1 s 精度
    long long d = t % 10; t /= 10;
    long long s = t % 60; t /= 60;
    long long m = t % 60; t /= 60;
    long long h = t;
    if (h > 0) snprintf(out, n, "%lld:%02lld:%02lld.%lld", h, m, s, d);
    else       snprintf(out, n, "%02lld:%02lld.%lld", m, s, d);
}

struct Progress {
    bool   on = false;
    bool   tty = false;
    double interval = 0.5;
    double total = 0.0;          // 秒, 0 = 未知
    double last = -1e9;
    bool   live = false;         // 当前终端上有一行未换行的进度

    void tick(uint64_t doneFrames, uint32_t sr, double t0) {
        if (!on) return;
        double now = nowSec();
        if (now - last < interval) return;
        last = now;

        double done = sr ? (double)doneFrames / (double)sr : 0.0;
        double el = now - t0;
        double sp = el > 0.0001 ? done / el : 0.0;
        char a[32], b[32], c[32], body[128];
        fmtHms(a, sizeof(a), done);

        if (total > 0.0) {
            fmtHms(b, sizeof(b), total);
            double pct = done / total * 100.0;
            if (pct > 100.0) pct = 100.0;
            if (sp > 0.01) {
                double eta = (total - done) / sp;
                fmtHms(c, sizeof(c), eta < 0 ? 0 : eta);
                snprintf(body, sizeof(body), "%5.1f%%  %s / %s  %6.1fx  ETA %s", pct, a, b, sp, c);
            } else {
                snprintf(body, sizeof(body), "%5.1f%%  %s / %s", pct, a, b);
            }
        } else {
            char e[32];
            fmtHms(e, sizeof(e), el);
            snprintf(body, sizeof(body), "%s  %6.1fx  elapsed %s", a, sp, e);
        }

        char line[200];
        int n = snprintf(line, sizeof(line), "[bmx] %s", body);
        if (n < 0) return;
        if (tty) {
            if (n < 76) {                                  // 补空格盖掉上一行的残留
                for (int k = n; k < 76; ++k) line[k] = ' ';
                line[76] = '\0'; n = 76;
            }
            fputc('\r', stderr);
            fwrite(line, 1, (size_t)n, stderr);
            live = true;
        } else {
            fwrite(line, 1, (size_t)n, stderr);
            fputc('\n', stderr);
        }
        fflush(stderr);
    }

    void finish() {
        if (live) { fputc('\n', stderr); fflush(stderr); live = false; }
    }
};

// "12.5" | "1:23.4" | "01:02:03" -> 秒
static bool parseTime(const std::string& s, double& out) {
    double acc = 0.0;
    int parts = 0;
    size_t p = 0;
    while (p <= s.size()) {
        size_t q = s.find(':', p);
        if (q == std::string::npos) q = s.size();
        std::string seg = s.substr(p, q - p);
        if (seg.empty()) return false;
        double v; if (!parseNum(seg, v) || v < 0) return false;
        acc = acc * 60.0 + v;
        ++parts;
        if (q == s.size()) break;
        p = q + 1;
    }
    if (parts == 0 || parts > 3) return false;
    out = acc;
    return true;
}

int wmain(int argc, wchar_t** wargv) {
    // 参数统一转成 UTF-8 处理
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        int n = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s((size_t)(n > 0 ? n - 1 : 0), '\0');
        if (n > 1) WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, &s[0], n, nullptr, nullptr);
        args.push_back(s);
    }

    Options o;
    o.inFmt.ch = 2; o.inFmt.sr = 48000; o.inFmt.samp = SampFmt::F32;
    o.outFmt.ch = 2; o.outFmt.sr = 48000; o.outFmt.samp = SampFmt::S24;

    auto need = [&](size_t& i, const char* what) -> bool {
        if (i + 1 >= args.size()) {
            say((std::string("参数 ") + what + " 缺少取值\n").c_str());
            return false;
        }
        ++i;
        return true;
    };

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto eq = [&](const char* s) { return a == s; };

        if (eq("-h") || eq("--help")) { say(kUsage, stdout); return 0; }
        if (eq("--version")) { say("bassmax_cli 1.2.0  (core: BassMax v4)\n", stdout); return 0; }
        if (eq("--list-presets")) {
            const bassmax::Preset* k = bassmax::presets();
            say("预设 (index 名称  100Hz低架/300/900/3k/9k  dB):\n", stdout);
            for (int j = 0; j < 6; ++j) {
                char buf[160];
                snprintf(buf, sizeof(buf), "  %d  %-9s %+5.1f %+5.1f %+5.1f %+5.1f %+5.1f\n", j, k[j].name,
                         k[j].ls, k[j].p300, k[j].p900, k[j].p3k, k[j].hs);
                say(buf, stdout);
            }
            return 0;
        }
        if (eq("--quiet") || eq("-q")) { o.quiet = true; o.progressMode = 0; continue; }
        if (eq("--dry-run")) { o.dryRun = true; continue; }
        if (eq("--progress")) { o.progressMode = 1; continue; }
        if (eq("--no-progress")) { o.progressMode = 0; continue; }
        if (eq("--progress-interval")) {
            if (!need(i, a.c_str())) return 2;
            double v; if (!parseNum(args[i], v) || v < 50 || v > 60000) {
                say("--progress-interval 范围 50..60000 (ms)\n"); return 2;
            }
            o.progressMs = (int)v;
            continue;
        }
        if (eq("--total")) {
            if (!need(i, a.c_str())) return 2;
            double v;
            if (!parseTime(args[i], v) || v <= 0.0) {
                say("--total 取值非法 (秒 / mm:ss / hh:mm:ss)\n"); return 2;
            }
            o.totalSec = v;
            continue;
        }

        if (eq("-o") || eq("--out")) { if (!need(i, a.c_str())) return 2; o.output = args[i]; o.outGiven = true; continue; }

        if (eq("--in-fmt")) {
            if (!need(i, a.c_str())) return 2;
            bool raw = false;
            if (!parseFmtName(args[i], o.inFmt.samp, raw) || !raw) {
                say("--in-fmt 只支持 f32le|s16le|s24le|s32le|f64le|u8\n");
                return 2;
            }
            o.inIsRaw = true;
            continue;
        }
        if (eq("--in-sr")) { if (!need(i, a.c_str())) return 2; o.inFmt.sr = (uint32_t)atoi(args[i].c_str()); continue; }
        if (eq("--in-ch")) { if (!need(i, a.c_str())) return 2; o.inFmt.ch = atoi(args[i].c_str()); continue; }

        if (eq("--out-fmt")) {
            if (!need(i, a.c_str())) return 2;
            if (args[i] == "wav") { o.outIsRaw = false; continue; }
            bool raw = false;
            if (!parseFmtName(args[i], o.outFmt.samp, raw) || !raw) {
                say("--out-fmt 只支持 wav|f32le|s16le|s24le|s32le|f64le|u8\n");
                return 2;
            }
            o.outIsRaw = true;
            continue;
        }
        if (eq("--out-bits")) {
            if (!need(i, a.c_str())) return 2;
            const std::string& v = args[i];
            if (v == "16") o.outFmt.samp = SampFmt::S16;
            else if (v == "24") o.outFmt.samp = SampFmt::S24;
            else if (v == "32") o.outFmt.samp = SampFmt::S32;
            else if (v == "f32") o.outFmt.samp = SampFmt::F32;
            else if (v == "f64") o.outFmt.samp = SampFmt::F64;
            else { say("--out-bits 只支持 16|24|32|f32|f64\n"); return 2; }
            continue;
        }

        if (eq("--drive")) {
            if (!need(i, a.c_str())) return 2;
            double v; if (!parseNum(args[i], v)) { say("--drive 取值非法\n"); return 2; }
            if (v < 0 || v > 10) { say("--drive 范围 0..10\n"); return 2; }
            o.driveUi = (float)v;
            continue;
        }
        if (eq("--bass") || eq("--treble") || eq("--makeup") || eq("--trim1k")) {
            if (!need(i, a.c_str())) return 2;
            double v; if (!parseNum(args[i], v)) { say((a + " 取值非法\n").c_str()); return 2; }
            if (eq("--bass")) o.p.bass = (float)v;
            else if (eq("--treble")) o.p.trim9k = (float)v;
            else if (eq("--makeup")) o.p.makeupDb = (float)v;
            else o.p.trim1k = (float)v;
            continue;
        }
        if (eq("--preset")) {
            if (!need(i, a.c_str())) return 2;
            double v;
            if (parseNum(args[i], v)) {
                int idx = (int)v;
                if (idx < 0 || idx > 5) { say("--preset 范围 0..5\n"); return 2; }
                o.p.preset = idx;
            } else {
                bool ok = false;
                int idx = presetFromName(args[i], ok);
                if (!ok) { say("认不出这个预设名, 用 --list-presets 看可用值\n"); return 2; }
                o.p.preset = idx;
            }
            continue;
        }

        // 进阶数值参数 (时间类以毫秒传入, Core 内部是秒)
        struct NumOpt { const char* name; float* dst; float scale; };
        const float MS = 0.001f;
        NumOpt nums[] = {
            {"--sat-lo",  &o.p.segSatLo,  1.0f},
            {"--sat-mid", &o.p.segSatMid, 1.0f},
            {"--sat-hi",  &o.p.segSatHi,  1.0f},
            {"--env",     &o.p.envMs,     MS},
            {"--gatt",    &o.p.gAttMs,    MS},
            {"--grel",    &o.p.gRelMs,    MS},
            {"--g2rel",   &o.p.g2RelMs,   MS},
            {"--loatt",   &o.p.loAttMs,   MS},
            {"--lorel",   &o.p.loRelMs,   MS},
            {"--limatt",  &o.p.limAttMs,  MS},
            {"--limrel",  &o.p.limRelMs,  MS},
            {"--envhi",   &o.p.envHiMs,   MS},
            {"--gathi",   &o.p.gAttHiMs,  MS},
            {"--grehi",   &o.p.gRelHiMs,  MS},
            {"--envmid",  &o.p.envMidMs,  MS},
            {"--gattmid", &o.p.gAttMidMs, MS},
            {"--grelmid", &o.p.gRelMidMs, MS},
        };
        bool handled = false;
        for (auto& n : nums) {
            if (a == n.name) {
                if (!need(i, a.c_str())) return 2;
                double v; if (!parseNum(args[i], v)) { say((a + " 取值非法\n").c_str()); return 2; }
                *n.dst = (float)(v * n.scale);
                handled = true;
                break;
            }
        }
        if (handled) continue;

        if (eq("--block")) { if (!need(i, a.c_str())) return 2; o.block = atoi(args[i].c_str()); continue; }

        if (!a.empty() && a[0] == '-' && a != "-") {
            say((std::string("未知参数: ") + a + "\n用 --help 看用法\n").c_str());
            return 2;
        }
        // 位置参数 = 输入
        if (!o.input.empty()) { say("只能指定一个输入\n"); return 2; }
        o.input = a;
    }

    if (o.block < 64) o.block = 64;
    if (o.block > 1 << 20) o.block = 1 << 20;
    if (o.p.bass < -12 || o.p.bass > 12) { say("--bass 范围 -12..12\n"); return 2; }
    if (o.p.trim9k < -12 || o.p.trim9k > 12) { say("--treble 范围 -12..12\n"); return 2; }
    if (o.p.makeupDb < 0 || o.p.makeupDb > 12) { say("--makeup 范围 0..12\n"); return 2; }

    // ---------------- 打开输入 ----------------
    Source src;
    std::string err;
    bool inputIsStdin = (o.input.empty() || o.input == "-");
    if (inputIsStdin) {
        src.in.attachStdin();
        src.label = "stdin";
        if (!o.inIsRaw) {
            if (looksLikeWav(src.in)) {
                if (!parseWav(src, err)) { say(("stdin: " + err + "\n").c_str()); return 3; }
            } else {
                src.fmt = o.inFmt;
                src.dataBytes = UINT64_MAX;
                src.label = "stdin (raw " + std::string(nameOf(src.fmt.samp)) + "le)";
            }
        } else {
            src.fmt = o.inFmt;
            src.dataBytes = UINT64_MAX;
            src.label = "stdin (raw " + std::string(nameOf(src.fmt.samp)) + "le)";
        }
    } else {
        std::wstring wp = utf8ToWide(o.input);
        if (!fileExists(wp)) { say(("找不到输入文件: " + o.input + "\n").c_str()); return 3; }
        if (!src.in.openFile(wp, err)) { say(("输入: " + err + "\n").c_str()); return 3; }
        src.label = o.input;
        if (!parseWav(src, err)) {
            say((o.input + ": " + err + "\n　如果是压缩格式/视频文件, 先用 ffmpeg 解码成 f32le 管道喂进来\n").c_str());
            return 3;
        }
    }

    if (src.fmt.ch < 1 || src.fmt.ch > 8) { say("声道数只支持 1~8\n"); return 3; }
    if (src.fmt.sr < 8000 || src.fmt.sr > 1536000) { say("采样率只支持 8000~1536000 Hz\n"); return 3; }

    // ---------------- 输出目标 ----------------
    if (!o.outGiven) {
        if (inputIsStdin) {
            o.output = "-";
        } else {
            std::string p = o.input;
            size_t dot = p.find_last_of('.');
            size_t slash = p.find_last_of("\\/");
            std::string ext = o.outIsRaw ? std::string(nameOf(o.outFmt.samp)) + "le" : "wav";
            if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) p = p.substr(0, dot);
            o.output = p + ".bmx." + ext;
        }
    }
    bool outIsStdout = (o.output == "-");

    o.outFmt.ch = src.fmt.ch;
    o.outFmt.sr = src.fmt.sr;

    // ---------------- 参数落进 Core ----------------
    bassmax::Params p = o.p;
    p.drive = o.driveUi * 0.1f;
    if (src.fmt.ch > 2 && p.drive > 0.60f) {
        if (!o.quiet) {
            char b[200];
            snprintf(b, sizeof(b), "[bmx] 输入 %d 声道, 驱动按多声道规则从 %.2f 压到 0.60\n",
                     src.fmt.ch, p.drive);
            say(b);
        }
        p.drive = 0.60f;
    }

    if (o.dryRun || !o.quiet) {
        char b[512];
        snprintf(b, sizeof(b), "[bmx] in  : %u Hz %d ch %s  (%s)\n", src.fmt.sr, src.fmt.ch,
                 nameOf(src.fmt.samp), src.label.c_str());
        say(b);
        snprintf(b, sizeof(b), "[bmx] out : %u Hz %d ch %s  (%s)\n", o.outFmt.sr, o.outFmt.ch,
                 o.outIsRaw ? nameOf(o.outFmt.samp) : nameOf(o.outFmt.samp), outIsStdout ? "stdout" : o.output.c_str());
        say(b);
        snprintf(b, sizeof(b), "[bmx] 参数: 驱动 %.1f/10  低音 %+.1f dB  高音 %+.1f dB  预设 %s  总增益 %+.1f dB\n",
                 o.driveUi, p.bass, p.trim9k, bassmax::presets()[p.preset].name,
                 p.makeupDb != 0.f ? p.makeupDb : bassmax::Tuning::MAKEUP_DB);
        say(b);
    }
    if (o.dryRun) {
        say("[bmx] dry-run: 参数校验通过, 未处理音频\n");
        return 0;
    }

    // ---------------- 打开输出 ----------------
    const int ch = src.fmt.ch;
    const int pairs = (ch + 1) / 2;
    const int frameBytes = src.fmt.frameBytes();
    const int block = o.block;

    // 输入长度已知时, 输出字节数也是确定的 -> 管道里也能写出尺寸正确的 WAV
    uint64_t expectedFrames = 0, expectedBytes = 0;
    if (src.dataBytes != UINT64_MAX) {
        expectedFrames = src.dataBytes / (uint64_t)frameBytes;
        expectedBytes = expectedFrames * (uint64_t)o.outFmt.frameBytes();
    }

    OutStream os;
    if (outIsStdout) {
        os.attachStdout();
    } else {
        std::wstring wp = utf8ToWide(o.output);
        if (!os.openFile(wp, err)) { say(("输出: " + err + " (" + o.output + ")\n").c_str()); return 4; }
    }

    WavWriter wav;
    RawWriter raw;
    bool useWav = !o.outIsRaw;
    if (useWav) {
        if (!wav.begin(os, o.outFmt, expectedBytes, err)) { say(("输出: " + err + "\n").c_str()); return 4; }
    } else {
        raw.begin(os, o.outFmt);
    }

    // ---------------- 处理 ----------------
    std::vector<bassmax::Core> cores((size_t)pairs);
    for (auto& c : cores) c.setParams(p, (float)src.fmt.sr);

    std::vector<unsigned char> inBuf((size_t)block * frameBytes);
    std::vector<float> buf((size_t)block * ch);
    std::vector<float> scratch((size_t)block * 2);

    double t0 = nowSec();
    uint64_t totalFrames = 0;
    double peak = 0.0, sumsq = 0.0;
    uint64_t remain = src.dataBytes;

    Progress prog;
    prog.tty = _isatty(_fileno(stderr)) != 0;
    prog.interval = (double)o.progressMs * 0.001;
    prog.on = !o.quiet && (o.progressMode == 1 || (o.progressMode < 0 && prog.tty));
    if (expectedFrames > 0)          prog.total = (double)expectedFrames / (double)src.fmt.sr;
    else if (o.totalSec > 0.0)       prog.total = o.totalSec;
    if (!o.quiet && prog.on && prog.total > 0.0 && src.dataBytes == UINT64_MAX) {
        char b[160];
        char ts[32];
        fmtHms(ts, sizeof(ts), prog.total);
        snprintf(b, sizeof(b), "[bmx] 总时长 (手动指定) %s, 进度按此计算\n", ts);
        say(b);
    }

    for (;;) {
        size_t wantFrames = (size_t)block;
        if (remain != UINT64_MAX) {
            size_t canDo = (size_t)(remain / (uint64_t)frameBytes);
            if (canDo == 0) break;
            if (wantFrames > canDo) wantFrames = canDo;
        }
        size_t gotBytes = src.in.read(inBuf.data(), wantFrames * (size_t)frameBytes);
        size_t frames = gotBytes / (size_t)frameBytes;
        if (frames == 0) break;
        if (remain != UINT64_MAX) remain -= (uint64_t)frames * (uint64_t)frameBytes;

        size_t nsamp = frames * (size_t)ch;
        decodeBlock(inBuf.data(), nsamp, src.fmt.samp, buf.data());

        // 按声道对处理 (与 DirectShow 滤镜一致; 奇数声道最后一对复制同一路)
        for (int pi = 0; pi < pairs; ++pi) {
            int c0 = 2 * pi;
            int c1 = (c0 + 1 < ch) ? (c0 + 1) : c0;
            for (size_t i = 0; i < frames; ++i) {
                scratch[2 * i] = buf[i * (size_t)ch + c0];
                scratch[2 * i + 1] = buf[i * (size_t)ch + c1];
            }
            cores[(size_t)pi].process(scratch.data(), (int)frames, (float)src.fmt.sr);
            for (size_t i = 0; i < frames; ++i) {
                buf[i * (size_t)ch + c0] = scratch[2 * i];
                if (c1 != c0) buf[i * (size_t)ch + c1] = scratch[2 * i + 1];
            }
        }

        for (size_t i = 0; i < nsamp; ++i) {
            double v = buf[i];
            double a = v < 0 ? -v : v;
            if (a > peak) peak = a;
            sumsq += v * v;
        }

        if (useWav) wav.writeFrames(buf.data(), frames);
        else raw.writeFrames(buf.data(), frames);
        totalFrames += frames;
        prog.tick(totalFrames, src.fmt.sr, t0);
    }

    prog.finish();

    if (useWav) wav.end(); else raw.end();
    os.close();

    if (totalFrames == 0) {
        say("[bmx] 没有读到任何音频帧 (输入没有音轨, 或者管道为空)\n");
        if (!outIsStdout) DeleteFileW(utf8ToWide(o.output).c_str());   // 别留个空壳
        return 3;
    }

    double dt = nowSec() - t0;
    double seconds = src.fmt.sr ? (double)totalFrames / (double)src.fmt.sr : 0.0;
    if (!o.quiet) {
        char b[400];
        double rms = totalFrames ? std::sqrt(sumsq / ((double)totalFrames * ch)) : 0.0;
        snprintf(b, sizeof(b),
                 "[bmx] 完成: %llu 帧 / %.2f s, 用时 %.2f s (%.1fx), peak %.4f, rms %.2f dBFS\n",
                 (unsigned long long)totalFrames, seconds, dt, dt > 0 ? seconds / dt : 0.0,
                 peak, rms > 1e-12 ? 20.0 * std::log10(rms) : -999.0);
        say(b);
    }
    return 0;
}
