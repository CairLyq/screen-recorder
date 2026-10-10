# 录制输出动画 WebP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** 让录制可选输出**动画 WebP**（`.webp`，无音频），与现有 MP4（H.264/AAC）并存，由设置里的「格式」决定。

**Architecture:** 在 `RecordConfig` 加 `OutputFormat {Mp4, Webp}`。WebP 时：视频编码器用 FFmpeg `libwebp_anim`（像素格式 YUV420P，用 `quality` 选项而非码率），**完全跳过音频**（不建 AudioEncoder、不开 WASAPI、muxer 无音频流），输出文件名后缀 `.webp` 让 `avformat_alloc_output_context2` 自动选 webp muxer。视频 FIFO 的打包/解包泛化为「按平面遍历」，同时兼容 NV12（MP4）与 YUV420P（WebP）。

**Tech Stack:** C++17 / MSVC / FFmpeg libav*（含 libwebp_anim）；已核验 vendored `third_party/ffmpeg/bin/avcodec-63.dll` 含 `libwebp_anim`，`avformat-63.dll` 含 webp muxer。

**前置：** master 上已合并的 recorder + ui。环境同前（cmake 用 VS 自带全路径）。

---

## Task 1: common —— OutputFormat 类型与序列化

**Files:** `common/types.h`, `common/settings.cpp`, `common/settings.h`(无需改), `tests/test_config_json.cpp`

- [ ] **Step 1: `common/types.h` 加枚举与字段**

```cpp
enum class OutputFormat { Mp4, Webp };
```
并给 `struct RecordConfig` 加一行（放在 `encoder` 之后）：
```cpp
  OutputFormat format = OutputFormat::Mp4;
```

- [ ] **Step 2: `common/settings.cpp` 加映射并在 save/load/config_to_json/config_from_json 里带上**

在匿名枚举映射区加：
```cpp
static const char* fmt_to_str(OutputFormat f) {
  return f == OutputFormat::Webp ? "webp" : "mp4";
}
static OutputFormat fmt_from_str(const std::string& s) {
  return s == "webp" ? OutputFormat::Webp : OutputFormat::Mp4;
}
```
- `save_settings`：加 `j["format"] = fmt_to_str(c.format);`
- `load_settings`：加 `c.format = fmt_from_str(j.value("format", std::string("mp4")));`
- `config_to_json`：加 `j["format"] = fmt_to_str(c.format);`
- `config_from_json`：加 `c.format = fmt_from_str(j.value("format", std::string("mp4")));`

- [ ] **Step 3: `tests/test_config_json.cpp` 加断言**

在 round-trip 段加：`in.format = OutputFormat::Webp;` … `CHECK(out.format == OutputFormat::Webp);`；并在默认值段加 `CHECK(config_from_json("{}").format == OutputFormat::Mp4);`

- [ ] **Step 4: 构建 + 测试**

Run: `export PATH="/c/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin:$PATH"; cmake --build build --config Debug && ctest --test-dir build -C Debug --output-on-failure`
Expected: 8/8 PASS。

- [ ] **Step 5: 提交**

```bash
git add common/types.h common/settings.cpp tests/test_config_json.cpp
git commit -m "feat: RecordConfig 增加 OutputFormat（mp4/webp）"
```

---

## Task 2: recorder —— 编码器/管线/muxer 支持 WebP

**Files:** `recorder/video_encoder.{h,cpp}`, `recorder/pipeline.cpp`

- [ ] **Step 1: `recorder/video_encoder.h` 的 `open` 增加格式参数（默认 Mp4，保持既有调用/测试可编译）**

```cpp
  bool open(int width, int height, int fps, Quality q, EncoderKind kind,
            std::string& chosen_name, OutputFormat format = OutputFormat::Mp4);
```
（`#include "common/types.h"` 已有 `OutputFormat`。）

- [ ] **Step 2: `recorder/video_encoder.cpp` 支持 libwebp_anim**

在 `open` 开头分支：当 `format == OutputFormat::Webp` → 只走 WebP 路径：`try_open_webp(w,h,fps,q,chosen_name)`。新增私有 `bool try_open_webp(...)`：
```cpp
bool VideoEncoder::try_open_webp(int w, int h, int fps, Quality q, std::string& name) {
  const AVCodec* codec = avcodec_find_encoder_by_name("libwebp_anim");
  if (!codec) return false;
  AVCodecContext* c = avcodec_alloc_context3(codec);
  if (!c) return false;
  c->width = w; c->height = h;
  c->time_base = {1, fps}; c->framerate = {fps, 1};
  c->pix_fmt = AV_PIX_FMT_YUV420P;      // libwebp_anim 要平面 YUV
  c->gop_size = fps * 2;
  int quality = q == Quality::Low ? 50 : (q == Quality::High ? 90 : 75);
  av_opt_set_int(c, "quality", quality, 0);   // libwebp 用 quality(0-100)，非码率
  if (avcodec_open2(c, codec, nullptr) < 0) { avcodec_free_context(&c); return false; }
  ctx_ = c; pkt_ = av_packet_alloc();
  name = "libwebp_anim";
  return true;
}
```
并在 `open` 顶部：`if (format == OutputFormat::Webp) return try_open_webp(width,height,fps,q,chosen_name);`
（`{h,cpp}` 需 `#include "common/types.h"`——types 已声明 OutputFormat。）

- [ ] **Step 3: `recorder/pipeline.cpp` 泛化 FIFO 打包/解包为「按平面遍历」**

视频 FIFO 存的是编码器输入帧的各平面紧密排列，改为兼容 NV12（2 平面）与 YUV420P（3 平面）。把两处 NB12 专用循环（`vt_` 线程打包、`et_` 线程解包）替换为通用形式：
```cpp
// 打包（vt_，f 为编码器输入帧，h = 帧高）
size_t sz = 0;
for (int p = 0; f->data[p] && p < 3; ++p) {
  int rows = (p == 0) ? h : h / 2;
  sz += (size_t)f->linesize[p] * rows;
}
it.nv12.resize(sz);
size_t off = 0;
for (int p = 0; f->data[p] && p < 3; ++p) {
  int rows = (p == 0) ? h : h / 2;
  for (int r = 0; r < rows; ++r) {
    std::memcpy(it.nv12.data() + off, f->data[p] + (size_t)r * f->linesize[p], f->linesize[p]);
    off += f->linesize[p];
  }
}
```
```cpp
// 解包（et_，同样三平面循环，从 it.nv12 拷回 f->data[p]）
size_t off = 0;
for (int p = 0; f->data[p] && p < 3; ++p) {
  int rows = (p == 0) ? h : h / 2;
  for (int r = 0; r < rows; ++r) {
    std::memcpy(f->data[p] + (size_t)r * f->linesize[p], it.nv12.data() + off, f->linesize[p]);
    off += f->linesize[p];
  }
}
```

- [ ] **Step 4: `recorder/pipeline.cpp` `start()` 按格式分支**

在 `start()` 里算 `const bool webp = (cfg_.format == OutputFormat::Webp);`
- 文件名后缀：`snprintf(name, ... , webp ? "ScreenRec_%04d%02d%02d_%02d%02d%02d.webp" : "ScreenRec_%04d%02d%02d_%02d%02d%02d.mp4", ...);`
- 视频编码器：`v->open(w, h, cfg_.fps, cfg_.quality, cfg_.encoder, vn, cfg_.format)`。
- 音频：`webp` 时**不建** `AudioEncoder`（`a = nullptr; aenc_ = nullptr;`），Muxer 的 audio 传 `nullptr`。
- sws 输出格式：`webp ? AV_PIX_FMT_YUV420P : AV_PIX_FMT_NV12`。
- **跳过音频线程**：`webp` 时不要 spawn `at_`（把 `at_ = std::thread([this]{...});` 整段用 `if (!webp) { at_ = ...; }` 包住）。
- `et_` 线程里对 `aenc_`/`ae` 的调用需空安全：`webp` 时 `ae` 为 null → 用 `if (ae) { ... }` 包住音频解码分支与 `ae->flush(...)`。**注意**：`et_` 目前是 `ae = (AudioEncoder*)aenc_;` — 若为 null，所有 `ae->` 调用要判空。
- `stop()` 里 `if (at_.joinable()) at_.join();` 已判空，安全。

- [ ] **Step 5: 构建 + 测试**

Run: `cmake --build build --config Debug && ctest --test-dir build -C Debug --output-on-failure`
Expected: 编译通过，8/8 PASS（现有 headless 集成测试仍走 MP4，不回归）。

- [ ] **Step 6: 提交**

```bash
git add recorder/video_encoder.h recorder/video_encoder.cpp recorder/pipeline.cpp
git commit -m "feat: 录制可选动画 WebP 输出（libwebp_anim，无音频）"
```

---

## Task 3: ui —— 设置加「格式」下拉

**Files:** `ui/resource.h`, `ui/ui.rc`, `ui/settings_dialog.cpp`

- [ ] **Step 1: `ui/resource.h` 加控件 ID**：`#define IDC_FORMAT 1015`

- [ ] **Step 2: `ui/ui.rc` 加一行下拉**（放在 编码器 那行之后，改一下下面的 y 值腾位置；保持对话框整体高度够用）：

```
    LTEXT           "格式:", -1, 12, 66, 50, 10
    COMBOBOX        IDC_FORMAT, 70, 64, 80, 60, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP
```
（把其后的「质量/编码器/…」各行 y 统一下移 ~22px，并相应加大 `DIALOGEX` 的高度；保持不重叠。

- [ ] **Step 3: `ui/settings_dialog.cpp`**
  - 加 `static const wchar_t* kFormat[] = {L"MP4", L"WebP"};`
  - `WM_INITDIALOG`：`fill_combo(dlg, IDC_FORMAT, kFormat, 2, cfg->format == OutputFormat::Webp ? 1 : 0);`（`fill_combo` 是 `const wchar_t*` 版）。
  - `IDOK`：`cfg->format = combo_sel(dlg, IDC_FORMAT) == 1 ? OutputFormat::Webp : OutputFormat::Mp4;`

- [ ] **Step 4: `ui/app.cpp`**：`config_to_json(cfg_)` 已含 format（Task 1 已处理），无需改。

- [ ] **Step 5: 构建**

Run: `cmake --build build --config Debug`
Expected: `ui.exe` 编译通过，8/8。

- [ ] **Step 6: 提交**

```bash
git add ui/resource.h ui/ui.rc ui/settings_dialog.cpp
git commit -m "feat: 设置里可选输出格式（MP4/WebP）"
```

---

## Task 4: 手工验收（用户执行）

- [ ] 构建产物确认：`build/Debug/` 有 `ui.exe`/`recorder.exe`。
- [ ] 用户跑：`ui.exe` → 设置 → 格式选 **WebP** → 确定 → F9 框选录制 → F9 停 → 输出目录应出现 `ScreenRec_*.webp`，能被看图/浏览器播放（动画）。
- [ ] 再选 **MP4** 录一次，确认原有行为不变（有声音）。
- [ ] 记录问题反馈。

---

## Self-Review 记录

- **覆盖**：格式类型与持久化→Task1；编码（libwebp_anim）+管线分支+FIFO 泛化→Task2；UI 选择→Task3。
- **已知取舍**：WebP 无音频（格式本身限制）；libwebp 用 `quality`(0-100) 映射 low/medium/high=50/75/90；WebP 走 YUV420P 平面，需 FIFO 泛化；长录屏的 WebP 体积/可寻址性一般（格式特性，不处理）。
- **类型一致性**：`OutputFormat`（types.h）、`VideoEncoder::open(...,OutputFormat)`、`fmt_to_str/from_str`、`IDC_FORMAT` 跨任务一致。
- **未覆盖（后续）**：WebP 的循环次数/无限循环选项、帧率优化、UI 里选 WebP 时自动灰掉音频选项。
