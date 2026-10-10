# ScreenRecorder（屏幕录制）

轻量的 Windows 屏幕录制工具。区域框选录制，输出 MP4 或动画 WebP，带托盘、悬浮条和全局热键。

## 功能

- **区域录制**：每次录制时现场框选区域（框选边框常驻可见，且不会录进视频）
- **音频**：系统声音 + 麦克风混音（可分别开关与调节音量）
- **输出格式**：
  - `MP4`（H.264 + AAC，带音频）
  - `WebP`（动画 WebP，无音频）
- **硬件编码**：优先 NVENC / QSV / AMF，不可用时回退 libx264 软编
- **交互**：常驻悬浮条（计时 + ▶/■ 开始停止）、托盘菜单、全局热键 `F9`、区域框选覆盖层
- **保存后自动打开输出目录**并选中文件
- 应用图标（exe / 窗口 / 托盘）

## 系统要求

- Windows 10（1809+）/ Windows 11，64 位
- 无需另装运行时：发布包已内置 VC++ 运行时（app-local）

## 使用

1. 运行 **`屏幕录制.exe`** → 托盘出现图标，屏幕顶部中央出现悬浮条。
2. 按 **`F9`**（或点悬浮条的 ▶）→ 屏幕变暗，**拖拽框选**要录的区域 → 松开即开始录制。
3. 再按 **`F9`**（或点 ■）→ 停止录制，自动打开输出目录。
4. **托盘右键 → 设置**：帧率、质量、编码器、输出格式（MP4/WebP）、音频开关与音量、输出目录。

## 截图

_（待补）_

## 从源码构建

**依赖**
- Visual Studio 2022（C++ 工具集 + CMake + Windows SDK）
- FFmpeg 开发包（**GPL shared**，含 `include/` `lib/` `bin/`），如 gyan.dev 的 `ffmpeg-release-full-shared` 或 BtbN 的 `*-gpl-shared`
- nlohmann/json 单头

放置：
```
third_party/ffmpeg/{include,lib,bin}
third_party/nlohmann/json.hpp
```

**构建**
```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```
产物在 `build/Release/`：`屏幕录制.exe`、`recorder.exe` 及 FFmpeg DLL。

**测试**
```bash
ctest --test-dir build -C Release
```

## 目录结构

| 目录 | 说明 |
|---|---|
| `ui/` | 主程序（托盘、悬浮条、设置、热键、区域框选、图标） |
| `recorder/` | 录制引擎（DXGI 抓屏 + WASAPI 音频 + FFmpeg 编码/封装） |
| `common/` | 共用模块（类型、配置读写、IPC 协议） |
| `tests/` | 单元测试（CTest） |
| `docs/` | 设计与实施计划 |

`ui.exe`（发布时改名 **`屏幕录制.exe`**）是常驻的控制界面；录制时按需启动 `recorder.exe`，两者通过命名管道通信。

## 已知限制

- **未实现**：游戏（独占全屏）抓帧、摄像头叠加、实时标注、直播推流。
- DXGI 抓屏遇到分辨率切换 / 独占全屏（AccessLost）不会自动重建。
- 音频端点声道数不为 2 时，该路音频会被跳过。
- 动画 WebP 不带音频（格式本身限制）。
- 多显示器下区域按所在显示器抓屏（单屏坐标为屏幕本地坐标）。

## 许可

本项目链接 **GPL 版 FFmpeg**（含 libx264 / GPL 组件），故整体按 **GPL** 分发。分发时请一并遵循 FFmpeg 及所链接库的许可条款。
