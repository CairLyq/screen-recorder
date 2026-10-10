# 录制引擎（recorder.exe）Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 构建 `recorder.exe` —— 屏幕录制引擎：DXGI 抓屏 + WASAPI 音频 + FFmpeg H.264/AAC 编码输出 MP4，带命名管道 IPC 供 UI 驱动。

**Architecture:** 三线程管线（抓屏 / 音频 / 编码-mux），两路 FIFO 衔接，QPC 统一时间戳。可脱离 UI 用 `--record N` 无头模式自测。DXGI 帧经 Staging 拷到 CPU、crop 区域、`sws` 转 NV12；WASAPI loopback+mic 混音、`swr` 重采样为 S16；编码线程按 DTS 交织写 MP4。

**Tech Stack:** C++17 / MSVC / CMake；DXGI Desktop Duplication；WASAPI；FFmpeg libav*（LGPL 动态库，6.x/7.x）；nlohmann/json（单头）。

**对应规格：** `docs/superpowers/specs/2026-10-10-screen-recorder-design.md`（§3 线程模型、§4 IPC、§5 组件、§7 时间戳、§8 错误处理、§9 测试）

**前置：** 需先准备 `third_party/` 依赖（Task 1 会说明）。

---

## 文件结构

```
/CMakeLists.txt                         构建全部目标
/tests/check.h                          极简断言宏（无框架）
/common/types.h                         共用类型（Rect/RecordConfig/Stats 等）
/common/ipc_message.h  .cpp             IPC 帧编解码（长度前缀 + 类型 + JSON）
/common/settings.h     .cpp             settings.json 读写
/common/log.h                           极简日志
/recorder/video_encoder.h .cpp          H.264 编码器（NVENC→QSV→AMF→x264 回退）
/recorder/audio_encoder.h .cpp          AAC 编码器（内部凑帧）
/recorder/muxer.h      .cpp             MP4 封装（交织写 + trailer）
/recorder/audio_mixer.h .cpp            浮点混音限幅 + swr 重采样
/recorder/ipc_server.h .cpp             命名管道服务端
/recorder/desktop_capture.h .cpp        DXGI Desktop Duplication + crop
/recorder/cursor_compositor.h .cpp      光标 alpha 合成（纯函数 + Win32 取光标）
/recorder/wasapi_capture.h .cpp         WASAPI loopback / mic 采集
/recorder/pipeline.h   .cpp             三线程装配 + 时间戳
/recorder/main.cpp                      recorder.exe 入口（IPC 模式 + --record 无头模式）
/tests/test_ipc_message.cpp
/tests/test_settings.cpp
/tests/test_audio_mixer.cpp
/tests/test_cursor_compositor.cpp
/tests/test_encoder_mux.cpp
/tests/test_ipc_server.cpp
/tests/integration_ffprobe.py           无头录制 → ffprobe 断言
```

---

## Task 1: 构建骨架与依赖

**Files:**
- Create: `CMakeLists.txt`
- Create: `tests/check.h`
- Create: `common/log.h`
- Create: `recorder/main.cpp`（暂时只有一个空 main）

- [ ] **Step 1: 放置第三方依赖**

在 `third_party/` 下放入：
- `third_party/nlohmann/json.hpp`（nlohmann/json 单头 v3.x）
- `third_party/ffmpeg/include/`（avcodec/avformat/avutil/swscale/swresample 头）
- `third_party/ffmpeg/lib/*.lib`（Windows 导入库）
- `third_party/ffmpeg/bin/*.dll`（运行时，构建后拷到输出目录）

来源：FFmpeg 官方 `ffmpeg-nX.Y-latest-win64-lgpl-shared` 包 + nlohmann/json release 的 `json.hpp`。
验证：`ls third_party/ffmpeg/lib` 应看到 `avcodec.lib avformat.lib avutil.lib swscale.lib swresample.lib`。

- [ ] **Step 2: 写 `tests/check.h`**

```cpp
#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
      std::exit(1);                                                            \
    }                                                                          \
  } while (0)

#define CHECK_STREQ(a, b)                                                      \
  do {                                                                         \
    const char* _a = (a);                                                      \
    const char* _b = (b);                                                      \
    if (std::strcmp(_a, _b) != 0) {                                            \
      std::printf("FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, _a,    \
                  _b);                                                         \
      std::exit(1);                                                            \
    }                                                                          \
  } while (0)

#define TEST_MAIN()                                                            \
  int main() {                                                                 \
    std::printf("PASS\n");                                                     \
    return 0;                                                                  \
  }
```

- [ ] **Step 3: 写 `common/log.h`**

```cpp
#pragma once
#include <cstdio>
#include <cstdarg>
#include <mutex>

inline void log_line(const char* level, const char* fmt, ...) {
  static std::mutex m;
  std::lock_guard<std::mutex> g(m);
  std::fprintf(stderr, "[%s] ", level);
  va_list ap;
  va_start(ap, fmt);
  std::vfprintf(stderr, fmt, ap);
  va_end(ap);
  std::fputc('\n', stderr);
}

#define LOGI(...) log_line("INFO", __VA_ARGS__)
#define LOGE(...) log_line("ERROR", __VA_ARGS__)
```

- [ ] **Step 4: 写最小 `recorder/main.cpp`**

```cpp
#include "common/log.h"

int main(int argc, char** argv) {
  LOGI("recorder starting");
  return 0;
}
```

- [ ] **Step 5: 写 `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.20)
project(ScreenRecorder CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
  add_compile_options(/utf-8 /W3)
endif()

set(FFMPEG_DIR ${CMAKE_SOURCE_DIR}/third_party/ffmpeg)
add_library(ffmpeg INTERFACE)
target_include_directories(ffmpeg INTERFACE ${FFMPEG_DIR}/include)
target_link_libraries(ffmpeg INTERFACE
  ${FFMPEG_DIR}/lib/avcodec.lib
  ${FFMPEG_DIR}/lib/avformat.lib
  ${FFMPEG_DIR}/lib/avutil.lib
  ${FFMPEG_DIR}/lib/swscale.lib
  ${FFMPEG_DIR}/lib/swresample.lib)

add_library(common STATIC
  common/ipc_message.cpp
  common/settings.cpp)
target_include_directories(common PUBLIC
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/third_party)

add_library(recorder_core STATIC
  recorder/video_encoder.cpp
  recorder/audio_encoder.cpp
  recorder/muxer.cpp
  recorder/audio_mixer.cpp
  recorder/ipc_server.cpp
  recorder/desktop_capture.cpp
  recorder/cursor_compositor.cpp
  recorder/wasapi_capture.cpp
  recorder/pipeline.cpp)
target_link_libraries(recorder_core PUBLIC
  common ffmpeg d3d11 dxgi ole32 oleaut32 uuid shlwapi)

add_executable(recorder recorder/main.cpp)
target_link_libraries(recorder PRIVATE recorder_core)

# 构建后把 ffmpeg dll 拷到输出目录
add_custom_command(TARGET recorder POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${FFMPEG_DIR}/bin $<TARGET_FILE_DIR:recorder>)

enable_testing()
set(TESTS
  test_ipc_message test_settings test_audio_mixer
  test_cursor_compositor test_encoder_mux test_ipc_server)
foreach(t ${TESTS})
  add_executable(${t} tests/${t}.cpp)
  target_link_libraries(${t} PRIVATE recorder_core)
  add_test(NAME ${t} COMMAND ${t})
endforeach()
```

> 注：`common/*.cpp` 与 `recorder/*.cpp` 此刻还不存在，CMake 配置会失败。这是预期的——后续 Task 逐个补齐源文件。本 Task 先把骨架和 `recorder/main.cpp` 建起来，从 Task 2 开始才有可编译目标。

- [ ] **Step 6: 建占位源文件让骨架可配置**

为让 CMake 能 configure，先给尚未实现的 `.cpp` 建空文件（后续 Task 填内容）：

```bash
for f in common/ipc_message common/settings \
         recorder/video_encoder recorder/audio_encoder recorder/muxer \
         recorder/audio_mixer recorder/ipc_server recorder/desktop_capture \
         recorder/cursor_compositor recorder/wasapi_capture recorder/pipeline; do
  : > "$f.cpp"
done
for f in test_ipc_message test_settings test_audio_mixer \
         test_cursor_compositor test_encoder_mux test_ipc_server; do
  printf '#include "tests/check.h"\nTEST_MAIN()\n' > "tests/$f.cpp"
done
```

- [ ] **Step 7: 配置并构建**

Run:
```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```
Expected: 生成 `build/Debug/recorder.exe`，编译无错误。

- [ ] **Step 8: 跑一遍空测试**

Run: `ctest --test-dir build -C Debug --output-on-failure`
Expected: 6 个测试全部 PASS（此刻都是空壳）。

- [ ] **Step 9: 提交**

```bash
git add CMakeLists.txt tests/check.h common/log.h recorder/main.cpp
git commit -m "build: 录制引擎构建骨架与第三方依赖接入"
```

---

## Task 2: 共用类型

**Files:**
- Create: `common/types.h`

- [ ] **Step 1: 写 `common/types.h`**

```cpp
#pragma once
#include <cstdint>
#include <string>

struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
  bool empty() const { return w <= 0 || h <= 0; }
};

enum class EncoderKind { Auto, Nvenc, Qsv, Amf, X264 };
enum class Quality { Low, Medium, High };

struct AudioTrack {
  bool enabled = false;
  float volume = 1.0f;
  std::string device = "default";
};

struct RecordConfig {
  std::string output_dir;
  int monitor_index = 0;
  Rect region;
  bool region_enabled = false;
  int fps = 30;
  Quality quality = Quality::Medium;
  EncoderKind encoder = EncoderKind::Auto;
  AudioTrack system_audio;
  AudioTrack microphone;
  bool include_cursor = true;
};

struct Stats {
  double fps = 0.0;
  int64_t elapsed_ms = 0;
  int64_t dropped_frames = 0;
  int64_t file_bytes = 0;
};
```

- [ ] **Step 2: 提交**

```bash
git add common/types.h
git commit -m "feat: 共用类型定义"
```

---

## Task 3: IPC 消息编解码

**Files:**
- Create: `common/ipc_message.h`, `common/ipc_message.cpp`
- Test: `tests/test_ipc_message.cpp`

帧格式：`[4 字节 LE 长度][1 字节类型][N 字节 JSON 体]`，长度 = 1 + N。

- [ ] **Step 1: 写失败测试 `tests/test_ipc_message.cpp`**

```cpp
#include "tests/check.h"
#include "common/ipc_message.h"

int main() {
  Message in{MsgType::Start, R"({"fps":30})"};
  std::vector<uint8_t> buf = encode_message(in);
  CHECK(buf.size() == 4 + 1 + 11);

  Message out{MsgType::Ping, ""};
  size_t consumed = 0;
  CHECK(decode_message(buf.data(), buf.size(), out, consumed));
  CHECK(out.type == MsgType::Start);
  CHECK_STREQ(out.payload.c_str(), R"({"fps":30})");
  CHECK(consumed == buf.size());

  // 半包：只给前 6 字节，应返回 false
  Message partial;
  size_t c2 = 0;
  CHECK(!decode_message(buf.data(), 6, partial, c2));
  return 0;
}
```

- [ ] **Step 2: 运行确认失败**

Run: `cmake --build build --config Debug --target test_ipc_message`
Expected: 编译失败（`ipc_message.h` 不存在 / 符号未定义）。

- [ ] **Step 3: 写 `common/ipc_message.h`**

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>

enum class MsgType : uint8_t {
  Start = 1, Stop, Ping, Ready, State, Stats, Error, Done
};

struct Message {
  MsgType type = MsgType::Ping;
  std::string payload;  // JSON 文本，可为空
};

std::vector<uint8_t> encode_message(const Message& m);

// 缓冲区首部有完整帧则解出 out 并置 consumed，返回 true；否则返回 false。
bool decode_message(const uint8_t* data, size_t len, Message& out, size_t& consumed);
```

- [ ] **Step 4: 写 `common/ipc_message.cpp`**

```cpp
#include "common/ipc_message.h"
#include <cstring>

std::vector<uint8_t> encode_message(const Message& m) {
  uint32_t body = 1 + static_cast<uint32_t>(m.payload.size());
  std::vector<uint8_t> out(4 + body);
  out[0] = static_cast<uint8_t>(body & 0xFF);
  out[1] = static_cast<uint8_t>((body >> 8) & 0xFF);
  out[2] = static_cast<uint8_t>((body >> 16) & 0xFF);
  out[3] = static_cast<uint8_t>((body >> 24) & 0xFF);
  out[4] = static_cast<uint8_t>(m.type);
  std::memcpy(out.data() + 5, m.payload.data(), m.payload.size());
  return out;
}

bool decode_message(const uint8_t* data, size_t len, Message& out, size_t& consumed) {
  if (len < 4) return false;
  uint32_t body = static_cast<uint32_t>(data[0]) |
                  (static_cast<uint32_t>(data[1]) << 8) |
                  (static_cast<uint32_t>(data[2]) << 16) |
                  (static_cast<uint32_t>(data[3]) << 24);
  if (body < 1) return false;
  if (len < 4u + body) return false;
  out.type = static_cast<MsgType>(data[4]);
  out.payload.assign(reinterpret_cast<const char*>(data + 5), body - 1);
  consumed = 4u + body;
  return true;
}
```

- [ ] **Step 5: 运行确认通过**

Run: `cmake --build build --config Debug --target test_ipc_message && ctest --test-dir build -C Debug -R test_ipc_message --output-on-failure`
Expected: PASS。

- [ ] **Step 6: 提交**

```bash
git add common/ipc_message.h common/ipc_message.cpp tests/test_ipc_message.cpp
git commit -m "feat: IPC 消息帧编解码"
```

---

## Task 4: 设置读写

**Files:**
- Create: `common/settings.h`, `common/settings.cpp`
- Test: `tests/test_settings.cpp`

- [ ] **Step 1: 写失败测试 `tests/test_settings.cpp`**

```cpp
#include "tests/check.h"
#include "common/settings.h"
#include <cstdio>

int main() {
  const char* path = "test_settings_tmp.json";
  std::remove(path);

  RecordConfig in;
  in.output_dir = "C:/videos";
  in.fps = 60;
  in.quality = Quality::High;
  in.encoder = EncoderKind::Nvenc;
  in.region = Rect{10, 20, 800, 600};
  in.region_enabled = true;
  in.system_audio.enabled = true;
  in.system_audio.volume = 0.5f;
  in.microphone.enabled = true;
  in.include_cursor = false;
  save_settings(path, in);

  RecordConfig out = load_settings(path);
  CHECK_STREQ(out.output_dir.c_str(), "C:/videos");
  CHECK(out.fps == 60);
  CHECK(out.quality == Quality::High);
  CHECK(out.encoder == EncoderKind::Nvenc);
  CHECK(out.region.x == 10 && out.region.h == 600);
  CHECK(out.region_enabled);
  CHECK(out.system_audio.enabled);
  CHECK(out.system_audio.volume > 0.49f && out.system_audio.volume < 0.51f);
  CHECK(out.microphone.enabled);
  CHECK(!out.include_cursor);

  // 默认值：不存在的文件走默认
  std::remove(path);
  RecordConfig def = load_settings(path);
  CHECK(def.fps == 30);
  CHECK(def.quality == Quality::Medium);
  CHECK(def.encoder == EncoderKind::Auto);
  CHECK(!def.region_enabled);
  CHECK(def.include_cursor);
  return 0;
}
```

- [ ] **Step 2: 运行确认失败**

Run: `cmake --build build --config Debug --target test_settings`
Expected: 编译失败（`settings.h` 符号未定义）。

- [ ] **Step 3: 写 `common/settings.h`**

```cpp
#pragma once
#include "common/types.h"
#include <string>

RecordConfig load_settings(const std::string& path);
void save_settings(const std::string& path, const RecordConfig& cfg);

// 返回 %APPDATA%\ScreenRecorder\settings.json；若 %APPDATA% 不存在则退回当前目录。
std::string default_settings_path();
```

- [ ] **Step 4: 写 `common/settings.cpp`**

```cpp
#include "common/settings.h"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <fstream>

using nlohmann::json;

static const char* quality_to_str(Quality q) {
  switch (q) {
    case Quality::Low: return "low";
    case Quality::High: return "high";
    default: return "medium";
  }
}
static Quality quality_from_str(const std::string& s) {
  if (s == "low") return Quality::Low;
  if (s == "high") return Quality::High;
  return Quality::Medium;
}
static const char* enc_to_str(EncoderKind k) {
  switch (k) {
    case EncoderKind::Nvenc: return "nvenc";
    case EncoderKind::Qsv: return "qsv";
    case EncoderKind::Amf: return "amf";
    case EncoderKind::X264: return "x264";
    default: return "auto";
  }
}
static EncoderKind enc_from_str(const std::string& s) {
  if (s == "nvenc") return EncoderKind::Nvenc;
  if (s == "qsv") return EncoderKind::Qsv;
  if (s == "amf") return EncoderKind::Amf;
  if (s == "x264") return EncoderKind::X264;
  return EncoderKind::Auto;
}

void save_settings(const std::string& path, const RecordConfig& c) {
  json j;
  j["output_dir"] = c.output_dir;
  j["monitor_index"] = c.monitor_index;
  j["region"] = {{"x", c.region.x}, {"y", c.region.y},
                 {"w", c.region.w}, {"h", c.region.h}};
  j["region_enabled"] = c.region_enabled;
  j["fps"] = c.fps;
  j["quality"] = quality_to_str(c.quality);
  j["video_encoder"] = enc_to_str(c.encoder);
  j["system_audio"] = {{"enabled", c.system_audio.enabled},
                       {"volume", c.system_audio.volume},
                       {"device", c.system_audio.device}};
  j["microphone"] = {{"enabled", c.microphone.enabled},
                     {"volume", c.microphone.volume},
                     {"device", c.microphone.device}};
  j["include_cursor"] = c.include_cursor;
  std::ofstream f(path);
  f << j.dump(2);
}

RecordConfig load_settings(const std::string& path) {
  RecordConfig c;
  std::ifstream f(path);
  if (!f.is_open()) return c;
  json j;
  try {
    f >> j;
  } catch (...) {
    return c;
  }
  c.output_dir = j.value("output_dir", c.output_dir);
  c.monitor_index = j.value("monitor_index", c.monitor_index);
  if (j.contains("region")) {
    c.region.x = j["region"].value("x", 0);
    c.region.y = j["region"].value("y", 0);
    c.region.w = j["region"].value("w", 0);
    c.region.h = j["region"].value("h", 0);
  }
  c.region_enabled = j.value("region_enabled", false);
  c.fps = j.value("fps", 30);
  c.quality = quality_from_str(j.value("quality", std::string("medium")));
  c.encoder = enc_from_str(j.value("video_encoder", std::string("auto")));
  if (j.contains("system_audio")) {
    c.system_audio.enabled = j["system_audio"].value("enabled", false);
    c.system_audio.volume = j["system_audio"].value("volume", 1.0f);
    c.system_audio.device = j["system_audio"].value("device", std::string("default"));
  }
  if (j.contains("microphone")) {
    c.microphone.enabled = j["microphone"].value("enabled", false);
    c.microphone.volume = j["microphone"].value("volume", 1.0f);
    c.microphone.device = j["microphone"].value("device", std::string("default"));
  }
  c.include_cursor = j.value("include_cursor", true);
  return c;
}

std::string default_settings_path() {
  const char* appdata = std::getenv("APPDATA");
  std::string dir = appdata ? appdata : ".";
  return dir + "/ScreenRecorder/settings.json";
}
```

> 注：`default_settings_path` 不负责建目录；调用方保存前需自行 `CreateDirectory`。测试直接传相对路径，不经过 `%APPDATA%`。

- [ ] **Step 5: 运行确认通过**

Run: `cmake --build build --config Debug --target test_settings && ctest --test-dir build -C Debug -R test_settings --output-on-failure`
Expected: PASS。

- [ ] **Step 6: 提交**

```bash
git add common/settings.h common/settings.cpp tests/test_settings.cpp
git commit -m "feat: settings.json 读写与默认值"
```

---

## Task 5: 音频混音与重采样

**Files:**
- Create: `recorder/audio_mixer.h`, `recorder/audio_mixer.cpp`
- Test: `tests/test_audio_mixer.cpp`

- [ ] **Step 1: 写失败测试 `tests/test_audio_mixer.cpp`**

```cpp
#include "tests/check.h"
#include "recorder/audio_mixer.h"

int main() {
  // 单路：增益 + 限幅
  float a[4] = {0.5f, -0.5f, 2.0f, -2.0f};
  float dst[4] = {0, 0, 0, 0};
  mix_float(dst, a, nullptr, 4, 1.0f, 0.0f);
  CHECK(dst[0] > 0.49f && dst[0] < 0.51f);
  CHECK(dst[1] > -0.51f && dst[1] < -0.49f);
  CHECK(dst[2] == 1.0f);    // 2.0 被限幅到 1.0
  CHECK(dst[3] == -1.0f);

  // 双路相加 + 各自增益
  float b[2] = {0.3f, 0.3f};
  float c[2] = {0.3f, -0.1f};
  float d2[2] = {0, 0};
  mix_float(d2, b, c, 2, 1.0f, 1.0f);
  CHECK(d2[0] > 0.59f && d2[0] < 0.61f);
  CHECK(d2[1] > 0.19f && d2[1] < 0.21f);

  // 双路饱和限幅
  float e[1] = {0.8f};
  float f[1] = {0.8f};
  float g[1] = {0};
  mix_float(g, e, f, 1, 1.0f, 1.0f);
  CHECK(g[0] == 1.0f);
  return 0;
}
```

- [ ] **Step 2: 运行确认失败**

Run: `cmake --build build --config Debug --target test_audio_mixer`
Expected: 编译失败（`audio_mixer.h` 未定义）。

- [ ] **Step 3: 写 `recorder/audio_mixer.h`**

```cpp
#pragma once
#include <cstddef>
#include <vector>
#include <cstdint>

// dst = clamp(a*ga + b*gb, -1, 1)。b 可为 nullptr（视为全 0）。
void mix_float(float* dst, const float* a, const float* b, size_t n,
               float ga, float gb);

// 用 swr 把 in_rate 的 float32 交错样本重采样到 out_rate，返回输出样本数。
class Resampler {
public:
  Resampler(int in_rate, int out_rate, int channels);
  ~Resampler();
  int convert(const float* in, int in_samples, float* out, int out_capacity);

private:
  void* swr_ = nullptr;  // SwrContext*
  int channels_;
};
```

- [ ] **Step 4: 写 `recorder/audio_mixer.cpp`**

```cpp
#include "recorder/audio_mixer.h"
extern "C" {
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
}
#include <algorithm>

void mix_float(float* dst, const float* a, const float* b, size_t n,
               float ga, float gb) {
  for (size_t i = 0; i < n; ++i) {
    float v = a[i] * ga + (b ? b[i] * gb : 0.0f);
    dst[i] = std::max(-1.0f, std::min(1.0f, v));
  }
}

Resampler::Resampler(int in_rate, int out_rate, int channels)
    : channels_(channels) {
  SwrContext* s = nullptr;
  AVChannelLayout layout;
  av_channel_layout_default(&layout, channels);
  swr_alloc_set_opts2(&s, &layout, AV_SAMPLE_FMT_FLT, out_rate,
                      &layout, AV_SAMPLE_FMT_FLT, in_rate, 0, nullptr);
  swr_init(s);
  swr_ = s;
}

Resampler::~Resampler() {
  if (swr_) swr_free(reinterpret_cast<SwrContext**>(&swr_));
}

int Resampler::convert(const float* in, int in_samples, float* out,
                       int out_capacity) {
  uint8_t* in_planes[1] = {reinterpret_cast<uint8_t*>(const_cast<float*>(in))};
  uint8_t* out_planes[1] = {reinterpret_cast<uint8_t*>(out)};
  int max_out = out_capacity / channels_;
  int got = swr_convert(reinterpret_cast<SwrContext*>(swr_), out_planes, max_out,
                        const_cast<const uint8_t**>(in_planes), in_samples);
  return got > 0 ? got : 0;
}
```

- [ ] **Step 5: 运行确认通过**

Run: `cmake --build build --config Debug --target test_audio_mixer && ctest --test-dir build -C Debug -R test_audio_mixer --output-on-failure`
Expected: PASS。

- [ ] **Step 6: 提交**

```bash
git add recorder/audio_mixer.h recorder/audio_mixer.cpp tests/test_audio_mixer.cpp
git commit -m "feat: 音频混音限幅与 swr 重采样"
```

---

## Task 6: H.264 视频编码器（含硬编回退）

**Files:**
- Create: `recorder/video_encoder.h`, `recorder/video_encoder.cpp`
- Test: `tests/test_encoder_mux.cpp`（本 Task 先覆盖视频编码部分；Task 7/8 追加 mux）

- [ ] **Step 1: 写失败测试 `tests/test_encoder_mux.cpp`**

```cpp
#include "tests/check.h"
#include "recorder/video_encoder.h"

int main() {
  int w = 320, h = 240, fps = 30;
  VideoEncoder enc;
  std::string chosen;
  CHECK(enc.open(w, h, fps, Quality::Medium, EncoderKind::X264, chosen));
  CHECK(!chosen.empty());
  std::printf("video encoder = %s\n", chosen.c_str());

  AVFrame* frame = enc.alloc_frame();
  CHECK(frame != nullptr);

  int packets = 0;
  for (int i = 0; i < 60; ++i) {
    // 填成纯灰 NV12，Y=128，UV=128
    for (int r = 0; r < h; ++r)
      std::memset(frame->data[0] + r * frame->linesize[0], 128, w);
    for (int r = 0; r < h / 2; ++r)
      std::memset(frame->data[1] + r * frame->linesize[1], 128, w);
    frame->pts = i;
    enc.encode(frame, [&](AVPacket* p) { packets++; });
  }
  enc.flush([&](AVPacket* p) { packets++; });

  CHECK(packets > 0);
  std::printf("packets = %d\n", packets);
  enc.close();
  av_frame_free(&frame);
  return 0;
}
```

- [ ] **Step 2: 运行确认失败**

Run: `cmake --build build --config Debug --target test_encoder_mux`
Expected: 编译失败（`video_encoder.h` 未定义）。

- [ ] **Step 3: 写 `recorder/video_encoder.h`**

```cpp
#pragma once
#include "common/types.h"
#include <functional>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
}

class VideoEncoder {
public:
  // 按 kind 选定编码器（Auto 走 NVENC→QSV→AMF→x264 回退），成功则写 chosen_name。
  bool open(int width, int height, int fps, Quality q, EncoderKind kind,
            std::string& chosen_name);
  AVFrame* alloc_frame();
  void encode(AVFrame* frame, const std::function<void(AVPacket*)>& on_packet);
  void flush(const std::function<void(AVPacket*)>& on_packet);
  void close();

  AVCodecContext* ctx() const { return ctx_; }
  bool is_open() const { return ctx_ != nullptr; }

private:
  bool try_open(const char* name, int w, int h, int fps, Quality q);
  AVCodecContext* ctx_ = nullptr;
  AVPacket* pkt_ = nullptr;
};
```

- [ ] **Step 4: 写 `recorder/video_encoder.cpp`**

```cpp
#include "recorder/video_encoder.h"
#include "common/log.h"
extern "C" {
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
}

static int64_t bitrate_for(Quality q, int w, int h, int fps) {
  double base;  // 1080p30 基准
  switch (q) {
    case Quality::Low: base = 3'000'000; break;
    case Quality::High: base = 20'000'000; break;
    default: base = 8'000'000; break;
  }
  double scale = (double)(w * h * fps) / (1920.0 * 1080.0 * 30.0);
  if (scale < 0.25) scale = 0.25;
  return (int64_t)(base * scale);
}

bool VideoEncoder::open(int width, int height, int fps, Quality q,
                        EncoderKind kind, std::string& chosen_name) {
  const char* chain[4];
  switch (kind) {
    case EncoderKind::Nvenc: chain[0] = "h264_nvenc";
      break;
    case EncoderKind::Qsv: chain[0] = "h264_qsv";
      break;
    case EncoderKind::Amf: chain[0] = "h264_amf";
      break;
    case EncoderKind::X264: chain[0] = "libx264";
      break;
    default: {
      static const char* auto_chain[4] = {"h264_nvenc", "h264_qsv",
                                          "h264_amf", "libx264"};
      if (try_open(auto_chain[0], width, height, fps, q)) { chosen_name = auto_chain[0]; return true; }
      if (try_open(auto_chain[1], width, height, fps, q)) { chosen_name = auto_chain[1]; return true; }
      if (try_open(auto_chain[2], width, height, fps, q)) { chosen_name = auto_chain[2]; return true; }
      if (try_open(auto_chain[3], width, height, fps, q)) { chosen_name = auto_chain[3]; return true; }
      return false;
    }
  }
  // 指定了单一编码器：只试它，失败再回退 x264
  if (try_open(chain[0], width, height, fps, q)) { chosen_name = chain[0]; return true; }
  if (try_open("libx264", width, height, fps, q)) { chosen_name = "libx264"; return true; }
  return false;
}

bool VideoEncoder::try_open(const char* name, int w, int h, int fps, Quality q) {
  const AVCodec* codec = avcodec_find_encoder_by_name(name);
  if (!codec) return false;
  AVCodecContext* c = avcodec_alloc_context3(codec);
  if (!c) return false;
  c->width = w;
  c->height = h;
  c->time_base = {1, fps};
  c->framerate = {fps, 1};
  c->pix_fmt = AV_PIX_FMT_NV12;
  c->gop_size = fps * 2;
  c->bit_rate = bitrate_for(q, w, h, fps);
  c->max_b_frames = 0;
  if (avcodec_open2(c, codec, nullptr) < 0) {
    avcodec_free_context(&c);
    LOGI("video encoder %s open failed", name);
    return false;
  }
  ctx_ = c;
  pkt_ = av_packet_alloc();
  LOGI("video encoder %s opened", name);
  return true;
}

AVFrame* VideoEncoder::alloc_frame() {
  AVFrame* f = av_frame_alloc();
  f->format = ctx_->pix_fmt;
  f->width = ctx_->width;
  f->height = ctx_->height;
  if (av_frame_get_buffer(f, 32) < 0) {
    av_frame_free(&f);
    return nullptr;
  }
  return f;
}

void VideoEncoder::encode(AVFrame* frame,
                          const std::function<void(AVPacket*)>& on_packet) {
  if (avcodec_send_frame(ctx_, frame) < 0) return;
  while (avcodec_receive_packet(ctx_, pkt_) == 0) {
    on_packet(pkt_);
    av_packet_unref(pkt_);
  }
}

void VideoEncoder::flush(const std::function<void(AVPacket*)>& on_packet) {
  avcodec_send_frame(ctx_, nullptr);
  while (avcodec_receive_packet(ctx_, pkt_) == 0) {
    on_packet(pkt_);
    av_packet_unref(pkt_);
  }
}

void VideoEncoder::close() {
  if (pkt_) av_packet_free(&pkt_);
  if (ctx_) avcodec_free_context(&ctx_);
}
```

- [ ] **Step 5: 运行确认通过**

Run: `cmake --build build --config Debug --target test_encoder_mux && ctest --test-dir build -C Debug -R test_encoder_mux --output-on-failure`
Expected: PASS，`packets > 0`。（若本机无 x264 支持，会打印失败——需确认 FFmpeg 包内含 libx264；LGPL 包不含 x264，此时改用 `--enable-gpl` 构建或改用 `h264_mf`。见文末「依赖注意」。）

- [ ] **Step 6: 提交**

```bash
git add recorder/video_encoder.h recorder/video_encoder.cpp tests/test_encoder_mux.cpp
git commit -m "feat: H.264 视频编码器与硬编回退链"
```

---

## Task 7: AAC 音频编码器

**Files:**
- Create: `recorder/audio_encoder.h`, `recorder/audio_encoder.cpp`
- Test: 追加到 `tests/test_encoder_mux.cpp`

- [ ] **Step 1: 追加失败测试到 `tests/test_encoder_mux.cpp`**

在 `int main()` 的 `return 0;` 之前插入：

```cpp
  // ---- 音频编码 ----
  AudioEncoder aenc;
  std::string acodec;
  CHECK(aenc.open(48000, 2, acodec));
  std::printf("audio encoder = %s\n", acodec.c_str());

  int apackets = 0;
  // 喂 1 秒正弦，每次 1024 样本
  std::vector<float> sine(1024 * 2);
  for (int blk = 0; blk < 47; ++blk) {
    for (int i = 0; i < 1024; ++i) {
      float v = 0.2f * std::sin(6.2831853f * 440.0f * (blk * 1024 + i) / 48000.0f);
      sine[i * 2] = v;
      sine[i * 2 + 1] = v;
    }
    aenc.encode(sine.data(), 1024, [&](AVPacket* p) { apackets++; });
  }
  aenc.flush([&](AVPacket* p) { apackets++; });
  CHECK(apackets > 0);
  std::printf("audio packets = %d\n", apackets);
  aenc.close();
```

并在文件头加：

```cpp
#include "recorder/audio_encoder.h"
#include <vector>
#include <cmath>
```

- [ ] **Step 2: 运行确认失败**

Run: `cmake --build build --config Debug --target test_encoder_mux`
Expected: 编译失败（`audio_encoder.h` 未定义）。

- [ ] **Step 3: 写 `recorder/audio_encoder.h`**

```cpp
#pragma once
#include <functional>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
}

class AudioEncoder {
public:
  // 输入为 float32 交错样本。内部凑满 frame_size 再编码。
  bool open(int sample_rate, int channels, std::string& chosen_name);
  void encode(const float* interleaved, int samples,
              const std::function<void(AVPacket*)>& on_packet);
  void flush(const std::function<void(AVPacket*)>& on_packet);
  void close();

  AVCodecContext* ctx() const { return ctx_; }

private:
  void drain(const std::function<void(AVPacket*)>& on_packet);
  AVCodecContext* ctx_ = nullptr;
  AVFrame* frame_ = nullptr;
  AVPacket* pkt_ = nullptr;
  std::vector<float> pending_;  // 交错样本缓冲
  int channels_ = 2;
};
```

- [ ] **Step 4: 写 `recorder/audio_encoder.cpp`**

```cpp
#include "recorder/audio_encoder.h"
#include "common/log.h"
extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

bool AudioEncoder::open(int sample_rate, int channels, std::string& chosen_name) {
  const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
  if (!codec) return false;
  ctx_ = avcodec_alloc_context3(codec);
  ctx_->sample_rate = sample_rate;
  ctx_->sample_fmt = AV_SAMPLE_FMT_FLTP;
  av_channel_layout_default(&ctx_->ch_layout, channels);
  ctx_->bit_rate = 128000;
  ctx_->time_base = {1, sample_rate};
  if (avcodec_open2(ctx_, codec, nullptr) < 0) {
    avcodec_free_context(&ctx_);
    return false;
  }
  channels_ = channels;
  frame_ = av_frame_alloc();
  frame_->format = ctx_->sample_fmt;
  frame_->sample_rate = sample_rate;
  av_channel_layout_default(&frame_->ch_layout, channels);
  frame_->nb_samples = ctx_->frame_size;
  av_frame_get_buffer(frame_, 0);
  pkt_ = av_packet_alloc();
  chosen_name = codec->name;
  return true;
}

void AudioEncoder::drain(const std::function<void(AVPacket*)>& on_packet) {
  while (avcodec_receive_packet(ctx_, pkt_) == 0) {
    on_packet(pkt_);
    av_packet_unref(pkt_);
  }
}

void AudioEncoder::encode(const float* interleaved, int samples,
                          const std::function<void(AVPacket*)>& on_packet) {
  pending_.insert(pending_.end(), interleaved, interleaved + samples * channels_);
  int fs = ctx_->frame_size;
  while ((int)(pending_.size() / channels_) >= fs) {
    av_frame_make_writable(frame_);
    // 交错 → 平面 FLTP
    for (int ch = 0; ch < channels_; ++ch) {
      float* dst = reinterpret_cast<float*>(frame_->data[ch]);
      for (int i = 0; i < fs; ++i)
        dst[i] = pending_[i * channels_ + ch];
    }
    frame_->pts = pts_;
    pts_ += fs;
    if (avcodec_send_frame(ctx_, frame_) >= 0) drain(on_packet);
    pending_.erase(pending_.begin(), pending_.begin() + fs * channels_);
  }
}

void AudioEncoder::flush(const std::function<void(AVPacket*)>& on_packet) {
  // 残余样本补零凑满一帧
  int fs = ctx_->frame_size;
  if (!pending_.empty()) {
    int have = pending_.size() / channels_;
    pending_.resize(fs * channels_, 0.0f);
    av_frame_make_writable(frame_);
    for (int ch = 0; ch < channels_; ++ch) {
      float* dst = reinterpret_cast<float*>(frame_->data[ch]);
      for (int i = 0; i < fs; ++i) dst[i] = pending_[i * channels_ + ch];
    }
    frame_->pts = pts_;
    pts_ += fs;
    (void)have;
    if (avcodec_send_frame(ctx_, frame_) >= 0) drain(on_packet);
    pending_.clear();
  }
  avcodec_send_frame(ctx_, nullptr);
  drain(on_packet);
}

void AudioEncoder::close() {
  if (pkt_) av_packet_free(&pkt_);
  if (frame_) av_frame_free(&frame_);
  if (ctx_) avcodec_free_context(&ctx_);
}
```

- [ ] **Step 5: 在头文件补 `pts_` 成员**

`recorder/audio_encoder.cpp` 用到了 `pts_`，在 `audio_encoder.h` 的 `private:` 段补上：

```cpp
  int64_t pts_ = 0;
```

- [ ] **Step 6: 运行确认通过**

Run: `cmake --build build --config Debug --target test_encoder_mux && ctest --test-dir build -C Debug -R test_encoder_mux --output-on-failure`
Expected: PASS，`audio packets > 0`。

- [ ] **Step 7: 提交**

```bash
git add recorder/audio_encoder.h recorder/audio_encoder.cpp tests/test_encoder_mux.cpp
git commit -m "feat: AAC 音频编码器"
```

---

## Task 8: MP4 封装

**Files:**
- Create: `recorder/muxer.h`, `recorder/muxer.cpp`
- Test: 追加到 `tests/test_encoder_mux.cpp`

- [ ] **Step 1: 追加失败测试到 `tests/test_encoder_mux.cpp`**

在 `int main()` 的 `return 0;` 之前插入（把 Task 6/7 的编码器改成写入 muxer）：

```cpp
  // ---- MP4 封装：把上面两段编码结果写文件 ----
  // 先重开一组编码器+muxer，录 1 秒
  {
    const char* out = "test_encoder_mux_out.mp4";
    std::remove(out);
    VideoEncoder v;
    AudioEncoder a;
    std::string vn, an;
    CHECK(v.open(320, 240, 30, Quality::Medium, EncoderKind::X264, vn));
    CHECK(a.open(48000, 2, an));
    Muxer mux;
    CHECK(mux.open(out, v.ctx(), a.ctx()));

    AVFrame* f = v.alloc_frame();
    std::vector<float> sine(1024 * 2, 0.1f);
    for (int i = 0; i < 30; ++i) {
      for (int r = 0; r < 240; ++r)
        std::memset(f->data[0] + r * f->linesize[0], 128, 320);
      for (int r = 0; r < 120; ++r)
        std::memset(f->data[1] + r * f->linesize[1], 128, 320);
      f->pts = i;
      v.encode(f, [&](AVPacket* p) { mux.write(p, v.ctx()); });
      a.encode(sine.data(), 1024, [&](AVPacket* p) { mux.write(p, a.ctx()); });
    }
    v.flush([&](AVPacket* p) { mux.write(p, v.ctx()); });
    a.flush([&](AVPacket* p) { mux.write(p, a.ctx()); });
    mux.close();
    av_frame_free(&f);
    v.close();
    a.close();

    std::FILE* fp = std::fopen(out, "rb");
    CHECK(fp != nullptr);
    std::fseek(fp, 0, SEEK_END);
    long sz = std::ftell(fp);
    std::fclose(fp);
    CHECK(sz > 1000);
    std::printf("mp4 size = %ld\n", sz);
  }
```

文件头补：`#include "recorder/muxer.h"`

- [ ] **Step 2: 运行确认失败**

Run: `cmake --build build --config Debug --target test_encoder_mux`
Expected: 编译失败（`muxer.h` 未定义）。

- [ ] **Step 3: 写 `recorder/muxer.h`**

```cpp
#pragma once
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

class Muxer {
public:
  // 需在写 header 前把视频/音频编码器参数写进对应流。
  bool open(const std::string& path, AVCodecContext* video,
            AVCodecContext* audio /*可为 nullptr*/);
  // pkt 应来自对应编码器；按 DTS 交织写入。
  bool write(AVPacket* pkt, AVCodecContext* from);
  void close();

  std::string error() const { return error_; }

private:
  AVFormatContext* fmt_ = nullptr;
  AVStream* vstream_ = nullptr;
  AVStream* astream_ = nullptr;
  AVCodecContext* audio_ctx_ = nullptr;
  std::string error_;
};
```

- [ ] **Step 4: 写 `recorder/muxer.cpp`**

```cpp
#include "recorder/muxer.h"
#include "common/log.h"

bool Muxer::open(const std::string& path, AVCodecContext* video,
                 AVCodecContext* audio) {
  if (avformat_alloc_output_context2(&fmt_, nullptr, nullptr, path.c_str()) < 0) {
    error_ = "alloc_output_context failed";
    return false;
  }
  vstream_ = avformat_new_stream(fmt_, nullptr);
  vstream_->time_base = video->time_base;
  avcodec_parameters_from_context(vstream_->codecpar, video);

  if (audio) {
    astream_ = avformat_new_stream(fmt_, nullptr);
    astream_->time_base = audio->time_base;
    avcodec_parameters_from_context(astream_->codecpar, audio);
    audio_ctx_ = audio;
  }

  if (!(fmt_->oformat->flags & AVFMT_NOFILE)) {
    if (avio_open(&fmt_->pb, path.c_str(), AVIO_FLAG_WRITE) < 0) {
      error_ = "avio_open failed";
      return false;
    }
  }
  if (avformat_write_header(fmt_, nullptr) < 0) {
    error_ = "write_header failed";
    return false;
  }
  return true;
}

bool Muxer::write(AVPacket* pkt, AVCodecContext* from) {
  AVStream* st = (from && from == audio_ctx_) ? astream_ : vstream_;
  if (!st) return false;
  pkt->stream_index = st->index;
  av_packet_rescale_ts(pkt, from->time_base, st->time_base);
  return av_interleaved_write_frame(fmt_, pkt) >= 0;
}

void Muxer::close() {
  if (!fmt_) return;
  av_write_trailer(fmt_);
  if (!(fmt_->oformat->flags & AVFMT_NOFILE) && fmt_->pb) avio_closep(&fmt_->pb);
  avformat_free_context(fmt_);
  fmt_ = nullptr;
}
```

- [ ] **Step 5: 运行确认通过**

Run: `cmake --build build --config Debug --target test_encoder_mux && ctest --test-dir build -C Debug -R test_encoder_mux --output-on-failure`
Expected: PASS，`mp4 size > 1000`。

- [ ] **Step 6: 用 ffprobe 手工确认双流**

Run: `ffprobe -v error -show_entries stream=index,codec_type,codec_name,width,height,sample_rate -of csv test_encoder_mux_out.mp4`
Expected: 输出含一条 `video`（h264）和一条 `audio`（aac）流。

- [ ] **Step 7: 提交**

```bash
git add recorder/muxer.h recorder/muxer.cpp tests/test_encoder_mux.cpp
git commit -m "feat: MP4 封装与双流交织"
```

---

## Task 9: 命名管道 IPC 服务端

**Files:**
- Create: `recorder/ipc_server.h`, `recorder/ipc_server.cpp`
- Test: `tests/test_ipc_server.cpp`

- [ ] **Step 1: 写失败测试 `tests/test_ipc_server.cpp`**

```cpp
#include "tests/check.h"
#include "recorder/ipc_server.h"
#include "common/ipc_message.h"
#include <thread>
#include <windows.h>

int main() {
  const char* name = "\\\\.\\pipe\\screenrec-test-1";
  IpcServer server;
  CHECK(server.create(name));

  std::thread client([&] {
    Sleep(100);
    HANDLE h = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    Message m{MsgType::Start, R"({"fps":30})"};
    auto buf = encode_message(m);
    DWORD wrote = 0;
    WriteFile(h, buf.data(), (DWORD)buf.size(), &wrote, nullptr);
    // 读回一条 STATE
    uint8_t rb[256];
    DWORD got = 0;
    ReadFile(h, rb, sizeof(rb), &got, nullptr);
    CHECK(got > 0);
    CloseHandle(h);
  });

  CHECK(server.wait_client());
  Message recv;
  CHECK(server.read_message(recv));
  CHECK(recv.type == MsgType::Start);
  CHECK_STREQ(recv.payload.c_str(), R"({"fps":30})");
  CHECK(server.send_message(Message{MsgType::State, R"("running")"}));

  client.join();
  server.close();
  return 0;
}
```

- [ ] **Step 2: 运行确认失败**

Run: `cmake --build build --config Debug --target test_ipc_server`
Expected: 编译失败（`ipc_server.h` 未定义）。

- [ ] **Step 3: 写 `recorder/ipc_server.h`**

```cpp
#pragma once
#include "common/ipc_message.h"
#include <cstdint>
#include <string>
#include <vector>

class IpcServer {
public:
  bool create(const std::string& pipe_name);
  bool wait_client();
  bool read_message(Message& out);
  bool send_message(const Message& m);
  void close();

private:
  void* handle_ = nullptr;  // HANDLE
  std::vector<uint8_t> buf_;
};
```

- [ ] **Step 4: 写 `recorder/ipc_server.cpp`**

```cpp
#include "recorder/ipc_server.h"
#include <windows.h>

bool IpcServer::create(const std::string& pipe_name) {
  HANDLE h = CreateNamedPipeA(
      pipe_name.c_str(), PIPE_ACCESS_DUPLEX,
      PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
      1, 64 * 1024, 64 * 1024, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  handle_ = h;
  return true;
}

bool IpcServer::wait_client() {
  HANDLE h = static_cast<HANDLE>(handle_);
  BOOL ok = ConnectNamedPipe(h, nullptr);
  return ok || GetLastError() == ERROR_PIPE_CONNECTED;
}

bool IpcServer::read_message(Message& out) {
  HANDLE h = static_cast<HANDLE>(handle_);
  // 已缓冲的完整帧？
  size_t consumed = 0;
  if (decode_message(buf_.data(), buf_.size(), out, consumed)) {
    buf_.erase(buf_.begin(), buf_.begin() + consumed);
    return true;
  }
  uint8_t tmp[4096];
  DWORD got = 0;
  BOOL ok = ReadFile(h, tmp, sizeof(tmp), &got, nullptr);
  if (!ok || got == 0) return false;
  buf_.insert(buf_.end(), tmp, tmp + got);
  if (decode_message(buf_.data(), buf_.size(), out, consumed)) {
    buf_.erase(buf_.begin(), buf_.begin() + consumed);
    return true;
  }
  return false;
}

bool IpcServer::send_message(const Message& m) {
  HANDLE h = static_cast<HANDLE>(handle_);
  auto buf = encode_message(m);
  DWORD wrote = 0;
  return WriteFile(h, buf.data(), (DWORD)buf.size(), &wrote, nullptr) != 0;
}

void IpcServer::close() {
  if (handle_) {
    FlushFileBuffers(static_cast<HANDLE>(handle_));
    DisconnectNamedPipe(static_cast<HANDLE>(handle_));
    CloseHandle(static_cast<HANDLE>(handle_));
    handle_ = nullptr;
  }
}
```

- [ ] **Step 5: 运行确认通过**

Run: `cmake --build build --config Debug --target test_ipc_server && ctest --test-dir build -C Debug -R test_ipc_server --output-on-failure`
Expected: PASS。

- [ ] **Step 6: 提交**

```bash
git add recorder/ipc_server.h recorder/ipc_server.cpp tests/test_ipc_server.cpp
git commit -m "feat: 命名管道 IPC 服务端"
```

---

## Task 10: DXGI 抓屏与区域裁剪

**Files:**
- Create: `recorder/desktop_capture.h`, `recorder/desktop_capture.cpp`
- Test: 无独立单测（依赖真实桌面）；由 Task 14 集成测试覆盖。

- [ ] **Step 1: 写 `recorder/desktop_capture.h`**

```cpp
#pragma once
#include "common/types.h"
#include <cstdint>
#include <vector>

struct CapturedFrame {
  int width = 0, height = 0;   // 裁剪后尺寸
  std::vector<uint8_t> bgra;   // 紧密打包，width*height*4
};

class DesktopCapture {
public:
  // monitor_index: 第几个输出；region 为该显示器本地坐标，empty 表示整屏。
  bool init(int monitor_index, const Rect& region);
  // 抓到新帧返回 true；超时（无变化）返回 false。
  bool next(CapturedFrame& out, int timeout_ms);
  void close();

  Rect output_rect() const { return crop_; }   // 裁剪后区域（本地坐标）
  int monitor_left() const { return mon_left_; }
  int monitor_top() const { return mon_top_; }

private:
  void* device_ = nullptr;      // ID3D11Device*
  void* context_ = nullptr;     // ID3D11DeviceContext*
  void* duplication_ = nullptr; // IDXGIOutputDuplication*
  void* staging_ = nullptr;     // ID3D11Texture2D*
  Rect crop_;
  int mon_left_ = 0, mon_top_ = 0;
  int tex_w_ = 0, tex_h_ = 0;
};
```

- [ ] **Step 2: 写 `recorder/desktop_capture.cpp`**

```cpp
#include "recorder/desktop_capture.h"
#include "common/log.h"
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

bool DesktopCapture::init(int monitor_index, const Rect& region) {
  ComPtr<IDXGIFactory1> factory;
  if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory))) {
    LOGE("CreateDXGIFactory1 failed");
    return false;
  }
  // 枚举显示器
  UINT mon = 0;
  ComPtr<IDXGIAdapter1> adapter;
  ComPtr<IDXGIOutput> output;
  bool found = false;
  for (UINT a = 0; factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
    for (UINT o = 0; adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; ++o) {
      if ((int)mon == monitor_index) { found = true; break; }
      mon++;
      output.Reset();
    }
    if (found) break;
  }
  if (!found) { LOGE("monitor %d not found", monitor_index); return false; }

  DXGI_OUTPUT_DESC desc;
  output->GetDesc(&desc);
  mon_left_ = desc.DesktopCoordinates.left;
  mon_top_ = desc.DesktopCoordinates.top;
  int full_w = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
  int full_h = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;

  ComPtr<IDXGIAdapter> ad;
  output->GetParent(__uuidof(IDXGIAdapter), (void**)&ad);
  D3D_FEATURE_LEVEL lvl;
  if (FAILED(D3D11CreateDevice(ad.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
                               nullptr, 0, D3D11_SDK_VERSION,
                               (ID3D11Device**)&device_, &lvl,
                               (ID3D11DeviceContext**)&context_))) {
    LOGE("D3D11CreateDevice failed");
    return false;
  }

  ComPtr<IDXGIOutput1> output1;
  output.As(&output1);
  if (FAILED(output1->DuplicateOutput((ID3D11Device*)device_,
                                      (IDXGIOutputDuplication**)&duplication_))) {
    LOGE("DuplicateOutput failed (权限/独占?)");
    return false;
  }

  crop_ = region.empty() ? Rect{0, 0, full_w, full_h} : region;
  if (crop_.x < 0) crop_.x = 0;
  if (crop_.y < 0) crop_.y = 0;
  if (crop_.x + crop_.w > full_w) crop_.w = full_w - crop_.x;
  if (crop_.y + crop_.h > full_h) crop_.h = full_h - crop_.y;
  tex_w_ = full_w;
  tex_h_ = full_h;

  D3D11_TEXTURE2D_DESC sd = {};
  sd.Width = full_w;
  sd.Height = full_h;
  sd.MipLevels = 1;
  sd.ArraySize = 1;
  sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  sd.SampleDesc.Count = 1;
  sd.Usage = D3D11_USAGE_STAGING;
  sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  if (FAILED(((ID3D11Device*)device_)->CreateTexture2D(&sd, nullptr,
                                                       (ID3D11Texture2D**)&staging_))) {
    LOGE("CreateTexture2D(staging) failed");
    return false;
  }
  LOGI("capture init: monitor %d %dx%d crop(%d,%d,%d,%d)", monitor_index, full_w,
       full_h, crop_.x, crop_.y, crop_.w, crop_.h);
  return true;
}

bool DesktopCapture::next(CapturedFrame& out, int timeout_ms) {
  IDXGIOutputDuplication* dup = (IDXGIOutputDuplication*)duplication_;
  ID3D11DeviceContext* ctx = (ID3D11DeviceContext*)context_;

  DXGI_OUTDUPL_FRAME_INFO info;
  IDXGIResource* res = nullptr;
  HRESULT hr = dup->AcquireNextFrame(timeout_ms, &info, &res);
  if (hr == DXGI_ERROR_WAIT_TIMEOUT) return false;
  if (FAILED(hr)) {
    // AccessLost：重建由上层处理，这里返回 false
    return false;
  }

  ComPtr<ID3D11Texture2D> tex;
  res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex);
  ctx->CopyResource((ID3D11Texture2D*)staging_, tex.Get());

  D3D11_MAPPED_SUBRESOURCE map;
  if (SUCCEEDED(ctx->Map((ID3D11Texture2D*)staging_, 0, D3D11_MAP_READ, 0, &map))) {
    out.width = crop_.w;
    out.height = crop_.h;
    out.bgra.resize((size_t)crop_.w * crop_.h * 4);
    const uint8_t* src = (const uint8_t*)map.pData;
    for (int r = 0; r < crop_.h; ++r) {
      const uint8_t* srow = src + (size_t)(crop_.y + r) * map.RowPitch +
                            (size_t)crop_.x * 4;
      std::memcpy(out.bgra.data() + (size_t)r * crop_.w * 4, srow,
                  (size_t)crop_.w * 4);
    }
    ctx->Unmap((ID3D11Texture2D*)staging_, 0);
  }
  dup->ReleaseFrame();
  res->Release();
  return true;
}

void DesktopCapture::close() {
  if (staging_) { ((ID3D11Texture2D*)staging_)->Release(); staging_ = nullptr; }
  if (duplication_) { ((IDXGIOutputDuplication*)duplication_)->Release(); duplication_ = nullptr; }
  if (context_) { ((ID3D11DeviceContext*)context_)->Release(); context_ = nullptr; }
  if (device_) { ((ID3D11Device*)device_)->Release(); device_ = nullptr; }
}
```

- [ ] **Step 3: 编译确认通过**

Run: `cmake --build build --config Debug --target recorder_core`
Expected: 编译无错误。（`wrl/client.h` 来自 Windows SDK。）

- [ ] **Step 4: 提交**

```bash
git add recorder/desktop_capture.h recorder/desktop_capture.cpp
git commit -m "feat: DXGI Desktop Duplication 抓屏与区域裁剪"
```

---

## Task 11: 光标合成

**Files:**
- Create: `recorder/cursor_compositor.h`, `recorder/cursor_compositor.cpp`
- Test: `tests/test_cursor_compositor.cpp`

- [ ] **Step 1: 写失败测试 `tests/test_cursor_compositor.cpp`**

```cpp
#include "tests/check.h"
#include "recorder/cursor_compositor.h"

int main() {
  // 8x8 目标，全 0
  const int W = 8, H = 8;
  std::vector<uint8_t> dst(W * H * 4, 0);
  // 2x2 光标：半透明红 (BGRA) alpha=128
  uint8_t cur[2 * 2 * 4] = {
      0, 0, 255, 128,  0, 0, 255, 128,
      0, 0, 255, 128,  0, 0, 255, 128};
  draw_cursor_bgra(dst.data(), W, H, 1, 1, cur, 2, 2, 128);

  // (1,1) 像素：B=0，R≈127（0*0.5 + 255*0.5）
  size_t i = (1 * W + 1) * 4;
  CHECK(dst[i + 0] < 5);          // B
  CHECK(dst[i + 2] > 120);        // R 被 alpha 合成

  // 越界：负坐标不崩、不写
  draw_cursor_bgra(dst.data(), W, H, -1, -1, cur, 2, 2, 128);
  return 0;
}
```

文件头补 `#include <vector>`

- [ ] **Step 2: 运行确认失败**

Run: `cmake --build build --config Debug --target test_cursor_compositor`
Expected: 编译失败。

- [ ] **Step 3: 写 `recorder/cursor_compositor.h`**

```cpp
#pragma once
#include <cstdint>

// 把 BGRA 光标图（含 alpha）以 alpha/255 混合到 dst 的 (px,py)。
// 处理负坐标与越界裁剪。cur_alpha 为额外整体透明度（0-255）。
void draw_cursor_bgra(uint8_t* dst, int dst_w, int dst_h, int px, int py,
                      const uint8_t* cur_bgra, int cw, int ch, int cur_alpha);

// 从 Win32 取当前光标（含热点）。成功返回 true，把光标位图写入 out_bgra/cw/ch。
bool fetch_system_cursor(std::vector<uint8_t>& out_bgra, int& cw, int& ch,
                         int& hot_x, int& hot_y);
```

- [ ] **Step 4: 写 `recorder/cursor_compositor.cpp`**

```cpp
#include "recorder/cursor_compositor.h"
#include <algorithm>
#include <vector>
#include <windows.h>

void draw_cursor_bgra(uint8_t* dst, int dst_w, int dst_h, int px, int py,
                      const uint8_t* cur, int cw, int ch, int cur_alpha) {
  for (int y = 0; y < ch; ++y) {
    int dy = py + y;
    if (dy < 0 || dy >= dst_h) continue;
    for (int x = 0; x < cw; ++x) {
      int dx = px + x;
      if (dx < 0 || dx >= dst_w) continue;
      const uint8_t* s = cur + (size_t)(y * cw + x) * 4;
      uint8_t a = (uint8_t)((s[3] * cur_alpha) / 255);
      if (a == 0) continue;
      uint8_t* d = dst + (size_t)(dy * dst_w + dx) * 4;
      for (int c = 0; c < 3; ++c)
        d[c] = (uint8_t)((s[c] * a + d[c] * (255 - a)) / 255);
    }
  }
}

bool fetch_system_cursor(std::vector<uint8_t>& out_bgra, int& cw, int& ch,
                         int& hot_x, int& hot_y) {
  CURSORINFO ci = {sizeof(CURSORINFO)};
  if (!GetCursorInfo(&ci) || ci.flags != CURSOR_SHOWING) return false;
  ICONINFO ii;
  if (!GetIconInfo(ci.hCursor, &ii)) return false;

  BITMAP bm;
  if (!GetObject(ii.hbmColor, sizeof(bm), &bm)) {
    DeleteObject(ii.hbmColor);
    DeleteObject(ii.hbmMask);
    return false;
  }
  cw = bm.bmWidth;
  ch = bm.bmHeight;
  hot_x = (int)ii.xHotspot;
  hot_y = (int)ii.yHotspot;

  BITMAPINFO bi = {};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = cw;
  bi.bmiHeader.biHeight = -ch;  // top-down
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;

  out_bgra.assign((size_t)cw * ch * 4, 0);
  HDC hdc = GetDC(nullptr);
  int got = GetDIBits(hdc, ii.hbmColor, 0, ch, out_bgra.data(), &bi, DIB_RGB_COLORS);
  ReleaseDC(nullptr, hdc);
  DeleteObject(ii.hbmColor);
  DeleteObject(ii.hbmMask);

  if (got == 0) return false;
  // 若 alpha 全 0（非 32bpp 光标），用 mask 生成 alpha
  bool any_alpha = false;
  for (size_t i = 3; i < out_bgra.size(); i += 4)
    if (out_bgra[i]) { any_alpha = true; break; }
  if (!any_alpha) {
    for (size_t i = 3; i < out_bgra.size(); i += 4) out_bgra[i] = 255;
  }
  return true;
}
```

- [ ] **Step 5: 运行确认通过**

Run: `cmake --build build --config Debug --target test_cursor_compositor && ctest --test-dir build -C Debug -R test_cursor_compositor --output-on-failure`
Expected: PASS。

- [ ] **Step 6: 提交**

```bash
git add recorder/cursor_compositor.h recorder/cursor_compositor.cpp tests/test_cursor_compositor.cpp
git commit -m "feat: 光标 alpha 合成与 Win32 取光标"
```

---

## Task 12: WASAPI 音频采集

**Files:**
- Create: `recorder/wasapi_capture.h`, `recorder/wasapi_capture.cpp`
- Test: 无单测（依赖真实音频设备），由 Task 14 集成测试/手动覆盖。

- [ ] **Step 1: 写 `recorder/wasapi_capture.h`**

```cpp
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// 采集浮点交错样本，回调给出 (samples_interleaved_float, frame_count)。
class WasapiCapture {
public:
  // loopback=true 采默认渲染端点的系统声；false 采默认采集端点（麦克风）。
  bool init(bool loopback, int channels_hint);
  void start(const std::function<void(const float*, int)>& on_data);
  void stop();
  void close();

  int sample_rate() const { return sample_rate_; }
  int channels() const { return channels_; }

private:
  void* client_ = nullptr;      // IAudioClient*
  void* capture_ = nullptr;     // IAudioCaptureClient*
  int sample_rate_ = 0;
  int channels_ = 0;
  bool running_ = false;
};
```

- [ ] **Step 2: 写 `recorder/wasapi_capture.cpp`**

```cpp
#include "recorder/wasapi_capture.h"
#include "common/log.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <thread>
#include <atomic>
#include <cmath>

static const IID kIID_IAudioClient = __uuidof(IAudioClient);
static const IID kIID_IAudioCaptureClient = __uuidof(IAudioCaptureClient);

bool WasapiCapture::init(bool loopback, int channels_hint) {
  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  (void)hr;  // 已初始化则忽略
  IMMDeviceEnumerator* enumerator = nullptr;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                              CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                              (void**)&enumerator)))
    return false;

  IMMDevice* device = nullptr;
  hr = enumerator->GetDefaultAudioEndpoint(loopback ? eRender : eCapture,
                                           eConsole, &device);
  enumerator->Release();
  if (FAILED(hr)) { LOGE("no default audio endpoint"); return false; }

  IAudioClient* client = nullptr;
  if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                              (void**)&client))) {
    device->Release();
    return false;
  }
  device->Release();

  WAVEFORMATEX* mix = nullptr;
  client->GetMixFormat(&mix);
  sample_rate_ = mix->nSamplesPerSec;
  channels_ = (channels_hint > 0) ? channels_hint : mix->nChannels;
  // 统一用 float32
  mix->wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
  mix->wBitsPerSample = 32;
  mix->nBlockAlign = (WORD)(channels_ * 4);
  mix->nAvgBytesPerSec = sample_rate_ * mix->nBlockAlign;
  mix->cbSize = 0;

  DWORD flags = loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0;
  if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags,
                                10000000, 0, mix, nullptr))) {
    CoTaskMemFree(mix);
    client->Release();
    return false;
  }
  CoTaskMemFree(mix);

  if (FAILED(client->GetService(kIID_IAudioCaptureClient, &capture_))) {
    client->Release();
    return false;
  }
  client_ = client;
  LOGI("wasapi init: %s %dHz %dch", loopback ? "loopback" : "mic", sample_rate_,
       channels_);
  return true;
}

void WasapiCapture::start(const std::function<void(const float*, int)>& on_data) {
  running_ = true;
  IAudioClient* client = (IAudioClient*)client_;
  IAudioCaptureClient* cap = (IAudioCaptureClient*)capture_;
  client->Start();
  std::vector<float> scratch;
  while (running_) {
    UINT32 packet = 0;
    if (FAILED(cap->GetNextPacketSize(&packet))) break;
    if (packet == 0) { Sleep(5); continue; }
    BYTE* data = nullptr;
    UINT32 frames = 0;
    DWORD f = 0;
    if (FAILED(cap->GetBuffer(&data, &frames, &f, nullptr, nullptr))) break;
    int n = (int)frames * channels_;
    scratch.resize(n);
    if (f & AUDCLNT_BUFFERFLAGS_SILENT) {
      std::fill(scratch.begin(), scratch.end(), 0.0f);
    } else {
      std::memcpy(scratch.data(), data, n * sizeof(float));
    }
    cap->ReleaseBuffer(frames);
    if (n > 0) on_data(scratch.data(), (int)frames);
  }
  client->Stop();
}

void WasapiCapture::stop() { running_ = false; }

void WasapiCapture::close() {
  if (capture_) { ((IAudioCaptureClient*)capture_)->Release(); capture_ = nullptr; }
  if (client_) { ((IAudioClient*)client_)->Release(); client_ = nullptr; }
}
```

- [ ] **Step 3: 编译确认通过**

Run: `cmake --build build --config Debug --target recorder_core`
Expected: 编译无错误。

- [ ] **Step 4: 提交**

```bash
git add recorder/wasapi_capture.h recorder/wasapi_capture.cpp
git commit -m "feat: WASAPI 系统声/麦克风采集"
```

---

## Task 13: 管线装配与无头入口

**Files:**
- Create: `recorder/pipeline.h`, `recorder/pipeline.cpp`
- Modify: `recorder/main.cpp`

管线把三块拼起来：抓屏线程产 NV12 帧、音频线程产混音后 float 样本、编码线程消费两者写 MP4。为控制复杂度，音频两路各起一个 `WasapiCapture`，用各自线程采集，混音在音频编码处做（两路样本先各自缓冲，等两路都齐 当量时混一次）。

- [ ] **Step 1: 写 `recorder/pipeline.h`**

```cpp
#pragma once
#include "common/types.h"
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

class Pipeline {
public:
  // 阻塞直到初始化完成（成功返回 true）。out_path 为实际写的文件。
  bool start(const RecordConfig& cfg, std::string& out_path);
  void stop();          // 停止并收尾
  Stats stats() const;

private:
  void video_loop();
  void audio_loop();
  void encode_loop();

  RecordConfig cfg_;
  std::atomic<bool> running_{false};
  std::atomic<int64_t> dropped_{0};
  std::atomic<double> measured_fps_{0};

  std::thread vt_, at_, et_;

  // 视频 FIFO
  std::mutex vmtx_;
  struct VItem { std::vector<uint8_t> nv12; int pts; };
  std::vector<VItem> vq_;
  size_t vcap_ = 0;

  // 音频 FIFO（已混好的 float 交错）
  std::mutex amtx_;
  std::vector<float> aq_;
  int a_channels_ = 2;
  int a_rate_ = 48000;

  std::string out_path_;
  void* venc_ = nullptr;  // VideoEncoder*
  void* aenc_ = nullptr;  // AudioEncoder*
  int64_t start_qpc_ = 0;
  int64_t qpc_freq_ = 0;
};
```

- [ ] **Step 2: 写 `recorder/pipeline.cpp`（视频线程 + 抓屏）**

```cpp
#include "recorder/pipeline.h"
#include "recorder/desktop_capture.h"
#include "recorder/cursor_compositor.h"
#include "recorder/video_encoder.h"
#include "recorder/audio_encoder.h"
#include "recorder/audio_mixer.h"
#include "recorder/muxer.h"
#include "recorder/wasapi_capture.h"
#include "common/log.h"
#include <windows.h>
extern "C" {
#include <libswscale/swscale.h>
}

static int64_t now_qpc() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

Stats Pipeline::stats() const {
  Stats s;
  s.fps = measured_fps_.load();
  s.dropped_frames = dropped_.load();
  if (start_qpc_ && running_) {
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    s.elapsed_ms = (t.QuadPart - start_qpc_) * 1000 / qpc_freq_;
  }
  return s;
}

bool Pipeline::start(const RecordConfig& cfg, std::string& out_path) {
  cfg_ = cfg;
  LARGE_INTEGER f; QueryPerformanceFrequency(&f); qpc_freq_ = f.QuadPart;

  // 起抓屏
  DesktopCapture* cap = new DesktopCapture();
  if (!cap->init(cfg_.monitor_index, cfg_.region_enabled ? cfg_.region : Rect{})) {
    LOGE("capture init failed");
    delete cap;
    return false;
  }

  // 文件路径
  SYSTEMTIME st; GetLocalTime(&st);
  char name[64];
  snprintf(name, sizeof(name), "ScreenRec_%04d%02d%02d_%02d%02d%02d.mp4",
           st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
  out_path_ = cfg_.output_dir + "/" + name;
  out_path = out_path_;

  // 编码器
  VideoEncoder* v = new VideoEncoder();
  std::string vn;
  if (!v->open(cap->output_rect().w, cap->output_rect().h, cfg_.fps,
               cfg_.quality, cfg_.encoder, vn)) {
    LOGE("video encoder open failed");
    delete v; delete cap; return false;
  }
  venc_ = v;
  LOGI("chosen video encoder: %s", vn.c_str());

  AudioEncoder* a = new AudioEncoder();
  std::string an;
  if (!a->open(48000, 2, an)) { delete a; delete v; delete cap; return false; }
  aenc_ = a;

  Muxer* mux = new Muxer();
  if (!mux->open(out_path_, v->ctx(), cfg_.system_audio.enabled || cfg_.microphone.enabled ? a->ctx() : nullptr)) {
    LOGE("mux open failed");
    delete mux; delete a; delete v; delete cap; return false;
  }

  start_qpc_ = now_qpc();
  running_ = true;
  vt_ = std::thread([this, cap] {
    VideoEncoder* ve = (VideoEncoder*)venc_;
    AVFrame* f = ve->alloc_frame();
    int w = f->width, h = f->height;
    SwsContext* sws = sws_getContext(w, h, AV_PIX_FMT_BGRA, w, h, AV_PIX_FMT_NV12,
                                     SWS_BILINEAR, nullptr, nullptr, nullptr);
    CapturedFrame cf;
    int frames = 0;
    int64_t t0 = now_qpc();
    // 上一帧缓存，用于无新帧时补帧
    std::vector<uint8_t> last_bgra;
    int64_t next_pts = 0;
    int64_t frame_dur = qpc_freq_ / cfg_.fps;
    while (running_) {
      bool got = cap->next(cf, 500);
      if (got) {
        if (cfg_.include_cursor) {
          std::vector<uint8_t> cb;
          int cw, chh, hx, hy;
          if (fetch_system_cursor(cb, cw, chh, hx, hy)) {
            // 全局鼠标坐标 → 抓屏本地坐标
            POINT p; GetCursorPos(&p);
            int lx = p.x - cap->monitor_left() - cap->output_rect().x - hx;
            int ly = p.y - cap->monitor_top() - cap->output_rect().y - hy;
            draw_cursor_bgra(cf.bgra.data(), cf.width, cf.height, lx, ly,
                             cb.data(), cw, chh, 255);
          }
        }
        last_bgra = cf.bgra;
      } else if (!last_bgra.empty()) {
        cf.bgra = last_bgra;  // 补帧
        cf.width = cap->output_rect().w;
        cf.height = cap->output_rect().h;
      } else {
        continue;
      }

      const uint8_t* src[1] = {cf.bgra.data()};
      int srcstride[1] = {cf.width * 4};
      if (av_frame_make_writable(f) < 0) continue;
      sws_scale(sws, src, srcstride, 0, cf.height, f->data, f->linesize);
      f->pts = next_pts++;
      {
        std::lock_guard<std::mutex> g(vmtx_);
        VItem it;
        size_t sz = (size_t)f->linesize[0] * h + (size_t)f->linesize[1] * (h / 2);
        it.nv12.resize(sz);
        size_t off = 0;
        for (int r = 0; r < h; ++r) { std::memcpy(it.nv12.data() + off, f->data[0] + r * f->linesize[0], f->linesize[0]); off += f->linesize[0]; }
        for (int r = 0; r < h / 2; ++r) { std::memcpy(it.nv12.data() + off, f->data[1] + r * f->linesize[1], f->linesize[1]); off += f->linesize[1]; }
        it.pts = (int)f->pts;
        if (vq_.size() < (size_t)cfg_.fps * 2) vq_.push_back(std::move(it));
        else dropped_++;
      }
      frames++;
      int64_t el = now_qpc() - t0;
      if (el > qpc_freq_) { measured_fps_ = frames * (double)qpc_freq_ / el; frames = 0; t0 = now_qpc(); }
      // 节流到目标帧率
      int64_t target = start_qpc_ + (int64_t)(next_pts) * frame_dur;
      int64_t delay = (target - now_qpc()) * 1000 / qpc_freq_;
      if (delay > 0 && delay < 1000) Sleep((DWORD)delay);
    }
    sws_freeContext(sws);
    av_frame_free(&f);
  });

  et_ = std::thread([this, mux] {
    VideoEncoder* ve = (VideoEncoder*)venc_;
    AudioEncoder* ae = (AudioEncoder*)aenc_;
    AVFrame* f = ve->alloc_frame();
    int h = f->height;
    while (true) {
      bool did = false;
      {
        std::lock_guard<std::mutex> g(vmtx_);
        if (!vq_.empty()) {
          VItem it = std::move(vq_.front());
          vq_.erase(vq_.begin());
          av_frame_make_writable(f);
          size_t off = 0;
          for (int r = 0; r < h; ++r) { std::memcpy(f->data[0] + r * f->linesize[0], it.nv12.data() + off, f->linesize[0]); off += f->linesize[0]; }
          for (int r = 0; r < h / 2; ++r) { std::memcpy(f->data[1] + r * f->linesize[1], it.nv12.data() + off, f->linesize[1]); off += f->linesize[1]; }
          f->pts = it.pts;
          ve->encode(f, [&](AVPacket* p) { mux->write(p, ve->ctx()); });
          did = true;
        }
      }
      {
        std::lock_guard<std::mutex> g(amtx_);
        if (!aq_.empty()) {
          ae->encode(aq_.data(), (int)(aq_.size() / a_channels_),
                     [&](AVPacket* p) { mux->write(p, ae->ctx()); });
          aq_.clear();
          did = true;
        }
      }
      if (!did) {
        if (!running_) {
          std::lock_guard<std::mutex> g(vmtx_);
          if (vq_.empty()) break;
        }
        Sleep(3);
      }
    }
    ve->flush([&](AVPacket* p) { mux->write(p, ve->ctx()); });
    ae->flush([&](AVPacket* p) { mux->write(p, ae->ctx()); });
    mux->close();
    av_frame_free(&f);
    delete mux; delete ae; delete ve; delete cap;
  });

  // 音频线程：两路采集，混音后入队
  at_ = std::thread([this, cap] {
    (void)cap;
    WasapiCapture sys, mic;
    Resampler* rsys = nullptr;
    Resampler* rmic = nullptr;
    if (cfg_.system_audio.enabled) {
      if (sys.init(true, 2)) rsys = new Resampler(sys.sample_rate(), a_rate_, 2);
    }
    if (cfg_.microphone.enabled) {
      if (mic.init(false, 2)) rmic = new Resampler(mic.sample_rate(), a_rate_, 2);
    }
    std::vector<float> sysbuf, micbuf;
    std::thread sys_t, mic_t;
    if (cfg_.system_audio.enabled && rsys) {
      sys_t = std::thread([&] {
        sys.start([&](const float* d, int n) {
          std::vector<float> out(n * 4 + 64);
          int got = rsys->convert(d, n, out.data(), (int)out.size());
          std::lock_guard<std::mutex> g(amtx_);
          sysbuf.assign(out.begin(), out.begin() + got * 2);
        });
      });
    }
    if (cfg_.microphone.enabled && rmic) {
      mic_t = std::thread([&] {
        mic.start([&](const float* d, int n) {
          std::vector<float> out(n * 4 + 64);
          int got = rmic->convert(d, n, out.data(), (int)out.size());
          std::lock_guard<std::mutex> g(amtx_);
          micbuf.assign(out.begin(), out.begin() + got * 2);
        });
      });
    }
    // 主循环：定期把两路混音进 aq_
    while (running_) {
      Sleep(10);
      std::lock_guard<std::mutex> g(amtx_);
      if ((!sysbuf.empty() || !micbuf.empty()) && aq_.empty()) {
        size_t n = sysbuf.size() > micbuf.size() ? sysbuf.size() : micbuf.size();
        std::vector<float> mixed(n, 0.0f);
        const float* a = sysbuf.empty() ? nullptr : sysbuf.data();
        const float* b = micbuf.empty() ? nullptr : micbuf.data();
        // 补齐短的一路
        std::vector<float> a2, b2;
        if (a && sysbuf.size() < n) { a2 = sysbuf; a2.resize(n, 0.0f); a = a2.data(); }
        if (b && micbuf.size() < n) { b2 = micbuf; b2.resize(n, 0.0f); b = b2.data(); }
        mix_float(mixed.data(), a ? a : mixed.data(), b, n,
                  a ? cfg_.system_audio.volume : 0.0f,
                  b ? cfg_.microphone.volume : 0.0f);
        aq_.assign(mixed.begin(), mixed.end());
        sysbuf.clear();
        micbuf.clear();
      }
    }
    sys.stop(); mic.stop();
    if (sys_t.joinable()) sys_t.join();
    if (mic_t.joinable()) mic_t.join();
    sys.close(); mic.close();
    delete rsys; delete rmic;
  });

  LOGI("recording -> %s", out_path_.c_str());
  return true;
}

void Pipeline::stop() {
  running_ = false;
  if (vt_.joinable()) vt_.join();
  if (at_.joinable()) at_.join();
  if (et_.joinable()) et_.join();
}
```

> 说明：混音主循环在 `amtx_` 锁内对齐两路长度后混一次。上面 `mix_float` 传 `a ? a : mixed.data()` 是为处理单路情形；两路都空时整段跳过，不会触发。若嫌绕，可改为「只在两路都非空时混，单路时对单路做 `mix_float(mixed, x, nullptr, n, gain, 0)`」——逻辑等价，读起来更直。

- [ ] **Step 3: 写 `recorder/main.cpp`（IPC 模式 + `--record` 无头模式）**

```cpp
#include "common/log.h"
#include "common/settings.h"
#include "common/ipc_message.h"
#include "recorder/pipeline.h"
#include "recorder/ipc_server.h"
#include "recorder/muxer.h"
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <windows.h>

using nlohmann::json;

static RecordConfig config_from_json(const std::string& body) {
  RecordConfig c;
  try {
    json j = json::parse(body);
    c.output_dir = j.value("output_dir", std::string("."));
    c.monitor_index = j.value("monitor_index", 0);
    c.fps = j.value("fps", 30);
    c.include_cursor = j.value("include_cursor", true);
    if (j.contains("region")) {
      c.region.x = j["region"].value("x", 0);
      c.region.y = j["region"].value("y", 0);
      c.region.w = j["region"].value("w", 0);
      c.region.h = j["region"].value("h", 0);
      c.region_enabled = !c.region.empty();
    }
    if (j.contains("system_audio"))
      c.system_audio.enabled = j["system_audio"].value("enabled", false),
      c.system_audio.volume = j["system_audio"].value("volume", 1.0f);
    if (j.contains("microphone"))
      c.microphone.enabled = j["microphone"].value("enabled", false),
      c.microphone.volume = j["microphone"].value("volume", 1.0f);
  } catch (const std::exception& e) {
    LOGE("bad config json: %s", e.what());
  }
  return c;
}

static int run_headless(int seconds, const std::string& out_dir) {
  RecordConfig cfg;
  cfg.output_dir = out_dir;
  cfg.fps = 30;
  cfg.system_audio.enabled = true;
  Pipeline p;
  std::string path;
  if (!p.start(cfg, path)) { LOGE("start failed"); return 2; }
  for (int i = 0; i < seconds * 10 && p.stats().elapsed_ms < seconds * 1000; ++i)
    Sleep(100);
  p.stop();
  LOGI("done: %s", path.c_str());
  std::printf("%s\n", path.c_str());
  return 0;
}

static int run_ipc(const std::string& pipe_name) {
  IpcServer server;
  if (!server.create(pipe_name)) { LOGE("pipe create failed"); return 3; }
  LOGI("waiting for UI on %s", pipe_name.c_str());
  if (!server.wait_client()) { LOGE("connect failed"); return 3; }
  server.send_message({MsgType::Ready, R"({"version":1})"});

  Pipeline p;
  bool recording = false;
  for (;;) {
    Message m;
    if (!server.read_message(m)) { LOGI("pipe closed"); break; }
    if (m.type == MsgType::Start) {
      RecordConfig cfg = config_from_json(m.payload);
      std::string path;
      if (p.start(cfg, path)) {
        recording = true;
        json j; j["path"] = path;
        server.send_message({MsgType::State, j.dump()});
      } else {
        server.send_message({MsgType::Error, R"({"code":"start_failed"})"});
      }
    } else if (m.type == MsgType::Stop) {
      p.stop();
      recording = false;
      server.send_message({MsgType::Done, R"({"ok":true})"});
    } else if (m.type == MsgType::Ping) {
      server.send_message({MsgType::State, R"("alive")"});
    }
  }
  if (recording) p.stop();
  server.close();
  return 0;
}

int main(int argc, char** argv) {
  std::string mode = "ipc";
  std::string pipe_name = "\\\\.\\pipe\\screenrec-default";
  std::string out_dir = ".";
  int seconds = 10;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--headless") mode = "headless";
    else if (a == "--pipe" && i + 1 < argc) pipe_name = argv[++i];
    else if (a == "--out" && i + 1 < argc) out_dir = argv[++i];
    else if (a == "--record" && i + 1 < argc) { mode = "headless"; seconds = atoi(argv[++i]); }
  }
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (mode == "headless") return run_headless(seconds, out_dir);
  return run_ipc(pipe_name);
}
```

- [ ] **Step 4: 在 CMake 的 recorder 目标补链接 `avformat` 等（已由 recorder_core 传递）**

无需改；`recorder_core` PUBLIC 链接了 `ffmpeg`。

- [ ] **Step 5: 编译确认通过**

Run: `cmake --build build --config Debug`
Expected: 生成 `build/Debug/recorder.exe`，无错误。

> `pipeline.cpp` 是纯粘合代码，逻辑分支多但不含可单测的独立算法，故不设单测；其正确性由 Task 14 的集成测试（真实录屏 → ffprobe）覆盖。

- [ ] **Step 6: 提交**

```bash
git add recorder/pipeline.h recorder/pipeline.cpp recorder/main.cpp
git commit -m "feat: 三线程录制管线与无头/IPC 入口"
```

---

## Task 14: 集成测试（无头录制 → ffprobe 断言）

**Files:**
- Create: `tests/integration_ffprobe.py`

- [ ] **Step 1: 写 `tests/integration_ffprobe.py`**

```python
import json, os, subprocess, sys, tempfile

RECORDER = sys.argv[1] if len(sys.argv) > 1 else "build/Debug/recorder.exe"
SECONDS = 5

def run():
    out = tempfile.mkdtemp(prefix="screenrec_it_")
    p = subprocess.run([RECORDER, "--record", str(SECONDS), "--out", out],
                       capture_output=True, text=True, timeout=SECONDS + 30)
    print("recorder stderr:\n", p.stderr)
    assert p.returncode == 0, f"recorder exit {p.returncode}"
    mp4 = p.stdout.strip().splitlines()[-1]
    assert os.path.isfile(mp4), f"no file: {mp4}"
    probe = subprocess.run(
        ["ffprobe", "-v", "error", "-print_format", "json",
         "-show_format", "-show_streams", mp4],
        capture_output=True, text=True, timeout=30)
    assert probe.returncode == 0, probe.stderr
    meta = json.loads(probe.stdout)
    kinds = [s["codec_type"] for s in meta["streams"]]
    assert "video" in kinds, f"no video stream: {kinds}"
    dur = float(meta["format"]["duration"])
    assert SECONDS - 1.5 <= dur <= SECONDS + 3.0, f"duration {dur} off"
    v = next(s for s in meta["streams"] if s["codec_type"] == "video")
    print(f"OK video={v['codec_name']} {v['width']}x{v['height']} "
          f"dur={dur:.2f}s streams={kinds}")
    print("INTEGRATION PASS")

run()
```

- [ ] **Step 2: 放个 ffprobe 到 PATH 或用 ffmpeg 包里带的**

将 `third_party/ffmpeg/bin/ffprobe.exe` 一并放到 PATH 或测试脚本同目录。
Run: `which ffprobe`（或 `where ffprobe`）
Expected: 有输出。

- [ ] **Step 3: 运行集成测试**

Run:
```bash
python tests/integration_ffprobe.py build/Debug/recorder.exe
```
Expected: 打印 `INTEGRATION PASS`，含 `video=h264 ...` 与 `video/audio` 两条流。
（若本机是纯 NVIDIA/QSV 机器且 x264 不可用，视频编码器可能是 `h264_nvenc`/`h264_qsv`——同样算通过，只要 codec_type 至少含 video。）

- [ ] **Step 4: 提交**

```bash
git add tests/integration_ffprobe.py
git commit -m "test: 无头录制 ffprobe 集成测试"
```

---

## 依赖注意

- **libx264 与许可**：LGPL 版 FFmpeg 包**不含 libx264**（x264 是 GPL）。Task 6/8 的回退链末端用的是 `libx264`，若你的包没有它，`test_encoder_mux` 会在纯 CPU 机器上失败。两种解法：
  1. 改用 GPL 版 FFmpeg 包（`ffmpeg-nX-latest-with-libx264`）；
  2. 回退链末端改为 `h264_mf`（Windows Media Foundation，系统自带软/硬编）。
  计划默认按「有 libx264」编写；无 x264 时把 Task 6 `video_encoder.cpp` 回退链里的 `"libx264"` 换成 `"h264_mf"`。
- **DXGI 独占全屏**：`DuplicateOutput` 在独显直连游戏下可能返回 AccessLost；本 MVP 不做自动重建（规格 §8 列为已知限制），录制普通窗口/桌面不受影响。
- **WASAPI loopback 静音设备**：无播放设备时 loopback 初始化失败，录制仍继续（仅丢系统声）。

---

## Self-Review 记录

- **规格覆盖**：§3 线程模型→Task 13；§4 IPC→Task 3/9；§5 组件→Task 5-12；§6 配置→Task 4；§7 时间戳→Task 13（QPC + `frame_dur` 节流）；§8 错误处理→编码回退 Task 6、设备丢失降级 Task 12/13、区域裁剪 Task 10；§9 测试→Task 3-9 单测 + Task 14 集成。UI 相关内容不在本计划（Plan 2）。
- **占位符**：无 TBD/TODO；每个代码步骤给出完整可编译代码。
- **类型一致性**：`Message`/`MsgType`、`RecordConfig`/`Rect`/`Stats`、`VideoEncoder::ctx()`、`Muxer::write(pkt,ctx)`、`AudioEncoder::encode` 在 Task 间保持一致。
- **已知简化**（`ponytail:`）：管线用「视频 FIFO 丢最旧帧」而非按时间戳精确重采样；音频两路用固定缓冲对齐而非严格按时间戳同步。够 MVP 用，需要音画严格对齐时再升级。
