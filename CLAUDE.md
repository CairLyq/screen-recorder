# CLAUDE.md

本仓库（ScreenRecorder）的协作约定，供 Claude Code 使用。

## 项目

Windows 屏幕录制工具。C++17 / MSVC / 纯 Win32 / FFmpeg libav。
两个进程：`ui`（发布名 **`屏幕录制.exe`**，控制界面）+ `recorder`（录制引擎，`recorder.exe`），通过命名管道通信。

## 构建与测试

- **cmake/ctest 不在 PATH**，用 VS 自带版本，命令前先：
  ```bash
  export PATH="/c/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin:$PATH"
  ```
- 配置 / 构建 / 测试：
  ```bash
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64
  cmake --build build --config Debug        # 或 Release
  ctest --test-dir build -C Debug --output-on-failure
  ```
- 依赖（不入库，见 `.gitignore`）：`third_party/ffmpeg/{include,lib,bin}`（**GPL shared**，含 `libx264`、`libwebp_anim`）+ `third_party/nlohmann/json.hpp`。

## 运行验证

- **不要**自行运行 `屏幕录制.exe` / `recorder.exe`（会真录屏）——**运行验收交用户**。
- 开发侧验证止于：编译 + `ctest` + 集成脚本 `python -m py_compile`。

## 关键约定与已踩过的坑（改代码前先读）

1. **中文与编码**：全局编译选项 `/utf-8`。所有 Win32 文本 API 用 **W 宽字符版**（`MessageBoxW` / `AppendMenuW` / `SetWindowTextW` / `RegisterClassW` / `CreateWindowExW` …）+ `L"中文"`；用 A 版会把 UTF-8 字节当 ANSI(936) → 乱码。
2. **`.rc` 资源**：必须挂在 **exe 目标**上（放进静态库会被链接器丢弃 → 对话框资源缺失、`DialogBoxParam` 返回 -1）。`.rc` 顶部有 `#pragma code_page(65001)`（源码 UTF-8）；对话框用 `DialogBoxParamW`。
3. **窗口过程**：`WM_NCCREATE` 必须返回 `TRUE`（返回 0 会让 `CreateWindow` 中止创建 → 窗口句柄为 null）。
4. **IPC 模型**：`recorder.exe` 是命名管道**服务端**（`CreateNamedPipe`），`ui` 是**客户端**（`CreateFile` 重试连接）。两个进程各自的管道 I/O 都走**单线程 + `PeekNamedPipe` 非阻塞轮询**——**禁止**在同一句柄上「挂起的阻塞 `ReadFile` + 另一线程 `WriteFile`」（会死锁，已成过 bug）。
5. **UI 与录制解耦**：悬浮条计时由 **UI 本地时钟**驱动（不依赖 IPC 到达节奏）；`State`/`Stats`/`Done`/`Error` 只做状态同步。
6. **区域坐标**：UI 返回**屏幕坐标**；`DesktopCapture::init` 按区域所在显示器选择输出并转换成**该显示器本地坐标**（多显示器可用）。
7. **DPI**：进程设为 `PerMonitorV2`。
8. **悬浮条 / 区域框**：置顶且定时重申置顶；悬浮条用 `SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)` 使其不进录制；区域框画在裁剪区**外侧**（不落进视频）。
9. **测试范围**：Win32 交互部分无单测（靠编译 + 用户验收）；纯逻辑（IPC 编解码、配置 JSON 往返、音频混音、时间戳）在 `tests/` 有单测。

## 目录

| 目录 | 内容 |
|---|---|
| `ui/` | 主程序（托盘、悬浮条、设置、热键、区域框选、图标、粘合） |
| `recorder/` | 录制引擎（DXGI 抓屏、WASAPI 音频、FFmpeg 编码/封装、管线、IPC 服务端） |
| `common/` | 共用（类型、配置读写、IPC 协议、日志） |
| `tests/` | 单元测试（CTest） |
| `docs/superpowers/specs` | 设计规格 |
| `docs/superpowers/plans` | 实施计划（按任务拆分） |

## 发布

- 打包：从 Release 产物取 `屏幕录制.exe` + `recorder.exe` + 5 个 FFmpeg DLL（`avcodec-63/avformat-63/avutil-61/swscale-10/swresample-7`）+ 3 个 VC 运行时 DLL（`msvcp140/vcruntime140/vcruntime140_1`）→ 压缩（见 `HANDOFF.md`）。
- 版本流程：commit → `git tag -a vX.Y.Z` → `git push origin vX.Y.Z` → `gh release create vX.Y.Z <zip> --title … --notes …`。

## 本机环境备忘

- `github.com:443` **被墙**；**SSH（`git@github.com`）可用**，`gh` 已登录（`api.github.com` 通）。`git push` / `gh release` 正常。
- 下载 GitHub 资源走代理：`https://gh-proxy.com/https://github.com/...`。
- 本机**无** `make` / `nasm` / `msys2` / `vcpkg`（若要做精简 FFmpeg 从源码构建，需先补这些）。
