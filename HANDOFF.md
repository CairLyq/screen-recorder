# HANDOFF.md

项目交接文档。面向接手的开发者。

## 一句话

Windows 屏幕录制软件，**v0.0.1 已发布**。区域框选录制，输出 MP4 或动画 WebP。

- 仓库：https://github.com/CairLyq/screen-recorder （分支 `master`）
- 发布：https://github.com/CairLyq/screen-recorder/releases/tag/v0.0.1
- 构建：干净从零 0 错误；单元测试 **8/8** 通过

## 交付物

**发布包** `screen-recorder-v0.0.1-win64.zip`（~55MB，解压即用，含 VC 运行时）：

```
屏幕录制.exe           主程序（= CMake 目标 ui，OUTPUT_NAME 改名）
recorder.exe           录制引擎（被主程序按需启动）
avcodec-63.dll  avformat-63.dll  avutil-61.dll  swscale-10.dll  swresample-7.dll   FFmpeg
msvcp140.dll  vcruntime140.dll  vcruntime140_1.dll                               VC++ 运行时
```

> 147MB 未压缩（`avcodec-63.dll` 单独 119MB——全量编解码库，我们只用到极少数编码器）。精简需自建 FFmpeg（本机缺 make/nasm，见下）。

## 架构

```
屏幕录制.exe (ui)  ──启动──▶  recorder.exe (管道服务端)
      │  ◀──命名管道(消息模式)──┘
      │  下行: Start <json>/ Stop / Ping
      │  上行: Ready / State{path} / Stats{fps,elapsed_ms} / Error / Done
```

- **`ui`**：常驻进程。托盘 + 常驻悬浮条（计时/▶■）+ 设置对话框 + 全局热键 F9 + 区域框选覆盖层 + 区域边窗。
- **`recorder`**：录制进程。三线程管线——抓屏（DXGI Desktop Duplication → 光标合成 → sws 转 NV12/YUV420P）、音频（WASAPI loopback+mic → swr → 混音）、编码/mux（FFmpeg → MP4 或 WebP）。两者各自单线程 `PeekNamedPipe` 轮询 IPC。

**一次录制的流程**：按 F9 → `ui` 弹区域框选（每次重选，不持久化）→ 启动 `recorder.exe --pipe <name>` → 客户端连上 → 发 `Start <config-json>` → 每秒收 `Stats` 更新悬浮条 → F9 停止发 `Stop` → 收到 `Done` 后 `ui` 自动打开输出目录。

## 已实现

- 区域框选录制（屏幕坐标 → 按所在显示器抓屏；框选边框常驻、白虚线+加粗角、画在裁剪区外）
- 系统声音 + 麦克风混音（独立开关与音量）
- 输出 **MP4**（H.264 + AAC）/ **动画 WebP**（`libwebp_anim`，无音频——格式限制）
- 硬件编码 NVENC → QSV → AMF，不可用回退 `libx264`
- 托盘菜单、常驻悬浮条（Aero 半透明、置顶、悬停/按下反馈、"保存中…"）、全局热键、设置对话框、应用图标
- 保存后自动打开输出目录并选中文件

## 已知限制 / 未做

- **未实现**：游戏（独占全屏）抓帧、摄像头叠加、实时标注、直播推流。
- DXGI AccessLost（分辨率切换/独占全屏）**不自动重建**。
- 音频端点声道数 **≠2** 时该路跳过。
- 动画 WebP 无音频。
- 待补：README 截图。

## 构建 / 测试 / 发布

```bash
# cmake 不在 PATH，用 VS 自带（见 CLAUDE.md）
export PATH="/c/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin:$PATH"

cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure     # 8/8

# 打 Release 包（示例）
rm -rf dist && mkdir dist
cp "build/Release/屏幕录制.exe" build/Release/recorder.exe dist/
cp third_party/ffmpeg/bin/{avcodec-63,avformat-63,avutil-61,swscale-10,swresample-7}.dll dist/
cp "<VS>/VC/Redist/MSVC/<ver>/x64/Microsoft.VC143.CRT/"{msvcp140,vcruntime140,vcruntime140_1}.dll dist/
```

发布：`git tag -a vX.Y.Z -m … && git push origin vX.Y.Z && gh release create vX.Y.Z <zip> --title … --notes …`

## 环境备忘（本机）

- `third_party/`（FFmpeg dev 包 + json.hpp）**不入库**，需另行放置（FFmpeg 用 **GPL shared** 版，含 `include/lib/bin`）。
- `github.com:443` **被墙**；SSH 可用；`gh` 已登录。下载走 `gh-proxy.com` 代理。
- **无** make / nasm / msys2 / vcpkg → 想做**精简 FFmpeg**（147MB → ~10MB）需先补这些工具链，再编 `libx264`/`libwebp`（+ 可选保留硬编的 NV/QSV/AMF 头文件）。已评估：成本高，**暂缓**。

## 后续子项目（各自独立 spec → plan）

1. 游戏录制模式（DX/OpenGL/Vulkan 注入抓帧）
2. 设备录制（摄像头 / 采集卡）
3. 实时标注与鼠标效果（绘图、点击高亮）
4. 直播推流（RTMP）+ 定时录制 + 水印 + 多音轨

## 工作方式

本项目用 `superpowers` 的 **brainstorming → writing-plans → subagent-driven-development** 流程推进：规格在 `docs/superpowers/specs/`，计划在 `docs/superpowers/plans/`。新功能建议沿用（先规格、再计划、逐任务实现 + 审查）。
