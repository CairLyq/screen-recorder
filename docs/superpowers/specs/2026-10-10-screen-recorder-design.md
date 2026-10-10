# 屏幕录屏软件设计（对齐 Bandicam 屏幕录制模式）

日期：2026-10-10
定位：内部工具（非商用产品）

## 1. 背景与目标

做一个 Windows 平台的录屏软件，长期目标全面对齐 Bandicam。Bandicam 本体由多个独立子系统组成：屏幕录制模式、游戏录制模式（DX/OpenGL/Vulkan hook）、设备录制模式（摄像头/采集卡），外加音频、编码、实时标注、推流等横切能力。

一次性全量对齐是数人月工程，因此按子系统拆分为多个子项目，逐个走 spec → plan → 实现。**本设计只覆盖第一个子项目：屏幕录制模式 MVP。**

### 目标

- 录制整个显示器或屏幕上的一块矩形区域
- 录制系统声音、麦克风声音，并混音（各带独立开关与音量）
- 用 H.264 硬件编码，导出 MP4
- 热键启动/停止；录制时显示悬浮计时与 FPS
- 设置持久化，选择输出目录

### 非目标（明确砍掉，留给后续子项目）

游戏 hook、webcam 小窗叠加、录制中实时绘图标注、鼠标点击高亮/点击音效、多显示器拼接、RTMP 推流、定时录制、水印、多音轨分轨导出、暂停/续录。

## 2. 技术选型

| 项 | 选择 | 理由 |
|---|---|---|
| 语言 | C++17 | 对标 Bandicam 本体，体积小、性能高、无运行时依赖 |
| 工具链 | MSVC (VS2022) + CMake | 现代、多目标友好 |
| 抓屏 | DXGI Desktop Duplication (`IDXGIOutputDuplication`) | Win8+ 通用，GPU 出帧，可直喂硬编 |
| 音频 | WASAPI：系统声用 loopback，麦克风用 capture | 系统原生，无第三方依赖 |
| 编码/封装 | FFmpeg libav*（LGPL 动态库）：`avcodec`/`avformat`/`swresample`/`swscale` | 编解码覆盖最全、硬编成熟、码率控制强 |
| UI | 纯 Win32 + `.rc` 资源对话框 | 原生，不引框架 |
| 配置 | nlohmann/json（单头）→ `%APPDATA%\ScreenRec\settings.json` | 零构建负担 |
| 热键 | `RegisterHotKey` | 系统原生全局热键 |

FFmpeg 使用 LGPL 动态库（不静态链接 GPL 组件），随程序分发 `av*.dll`。

## 3. 进程与线程模型

采用**抓屏独立子进程**架构（Bandicam 做法）：UI 崩溃不影响正在进行中的录制。

### 主进程 `ui.exe`

- UI 线程（Win32 消息循环）：主窗口（设置）、托盘图标、悬浮工具条、区域选取覆盖层
- 热键：`RegisterHotKey` 监听起停热键
- IPC 服务端：`CreateNamedPipe` 建管道，向录制子进程下发命令、接收状态

### 录制子进程 `recorder.exe`

- **抓屏线程**：DXGI Desktop Duplication → 取 BGRA 纹理 → copy 到 Staging → Map → crop 区域 → `sws_scale` 转 NV12 → 推入视频 FIFO
- **音频线程**：WASAPI loopback（系统声）+ WASAPI capture（麦克风）→ 混音 → `swr_convert` 重采样为 S16 交错 → 推入音频 FIFO
- **编码/mux 线程**：从两个 FIFO 取数据 → 两个 `libavcodec` 编码器（视频 H.264、音频 AAC）→ 按 DTS 交织 → `libavformat` 写 MP4
- 抓屏线程还负责**光标合成**（DXGI 帧不含光标）

音频编码在编码/mux 线程内完成。输出文件名由 recorder 生成：`ScreenRec_YYYYMMDD_HHMMSS.mp4`，落在 `output_dir`。

### 生命周期

1. 用户按热键 / 点悬浮球开始
2. UI 生成录制配置，通过命名管道发给 recorder
3. UI 若无 recorder 进程则启动之，再发 START
4. recorder 初始化管线，回 `STATE(running)` + 每秒 `STATS`
5. 停止时 UI 发 STOP，recorder 冲刷编码器、写 MP4 的 moov、回 `DONE(path)` 后退出
6. UI 检测管道断开（recorder 崩溃）→ 提示用户录制中断

## 4. IPC 协议

载体：Windows 命名管道（message mode），管道名 `\\.\pipe\screenrec-<pid>`。

角色：UI 进程是管道**服务端**（`CreateNamedPipe` 等待连接），recorder 启动后作为**客户端**（`CreateFile`）连上。

消息统一为「4 字节小端长度 + JSON 体」。命令集：

**UI → recorder**

| 命令 | 载荷 |
|---|---|
| `START` | 完整录制配置（见 §6） |
| `STOP` | 无 |
| `PING` | 无（存活探测） |

**recorder → UI**

| 消息 | 载荷 |
|---|---|
| `READY` | 版本号 |
| `STATE` | `running` / `stopped` |
| `STATS` | `fps`、`elapsed_ms`、`dropped_frames`、`file_bytes`（每 1s 一条） |
| `ERROR` | `code`、`message` |
| `DONE` | `output_path`、`duration_ms` |

## 5. 组件详解

### `Capture`（recorder）

- `IDXGIOutputDuplication` 按显示器创建
- 只抓整显示器，**区域录制 = 取帧后 crop** 到区域矩形（区域坐标由 UI 换算成该显示器本地坐标后传入）
- 帧流转：`IDXGIResource` → `ID3D11Texture2D` → copy 到可读 Staging 纹理 → Map 得到 CPU 指针
- `AcquireNextFrame` 超时（默认 500ms）循环；超时视为无新帧，不补帧

### `CursorCompositor`（recorder）

DXGI 帧不含光标，需自行绘制：`GetCursorInfo` + `GetIconInfo` 取光标位图与热点 → 用 `DrawIconEx`（或手写 alpha 合成）画到 CPU 帧的对应位置。GPU 合成留作后续优化。

### `AudioCapture`（recorder）

- 系统声：对默认渲染端点开 `AUDCLNT_STREAMFLAGS_LOOPBACK` 的 capture
- 麦克风：对默认 capture 端点开普通 capture
- 均为 float32 → 按各自音量增益 → 相加并按 `[-1,1]` 限幅 → `swr` 重采样到统一采样率、S16、立体声

### `Encoder`（recorder）

- 视频 `AVCodecContext`：首选 `h264_nvenc`，失败依次 `h264_qsv` → `h264_amf` → `libx264`
- 音频 `AVCodecContext`：`aac`
- 输入视频为 NV12，音频为 S16 交错；用 `AVFrame` + PTS（由 QPC 换算）
- **回退链**在初始化阶段决定，写日志记录实际选中的编码器

### `Muxer`（recorder）

`avformat` MP4 muxer。编码/mux 线程按 DTS 交织写包；停止时写 trailer 并落盘 moov。

### `RegionSelector`（ui）

透明、置顶、全屏覆盖窗口，鼠标拖拽画矩形，得到屏幕坐标矩形，映射到对应显示器的本地坐标后写入配置。

### `Toolbar`（ui）

置顶分层小窗，显示 `REC` + 计时 + FPS + 停止按钮。数据来自 `STATS`。

### `Settings`（common）

`%APPDATA%\ScreenRec\settings.json`，字段见 §6。读写用 nlohmann/json。

### `Tray`（ui）

托盘图标 + 右键菜单：开始/停止、打开设置、打开输出目录、退出。

### `Hotkey`（ui）

`RegisterHotKey` 全局热键，默认开始/停止同键（如 F9），可从设置改。

## 6. 录制配置（`START` 载荷 / settings.json）

```
{
  "output_dir":        "C:/Users/<user>/Videos",
  "monitor_index":     0,
  "region":            { "x": 0, "y": 0, "w": 1920, "h": 1080, "enabled": false },
  "fps":               30,
  "quality":           "medium",        // low | medium | high
  "video_encoder":     "auto",          // auto | nvenc | qsv | amf | x264
  "system_audio":      { "enabled": true,  "volume": 1.0 },
  "microphone":        { "enabled": false, "volume": 1.0, "device": "default" },
  "hotkey_start_stop": "F9",
  "include_cursor":    true
}
```

码率控制统一用**平均码率（ABR）**：视频设 `bit_rate`，硬编与软编均按此目标码率。预设按分辨率/帧率缩放，基准为 1080p30：
- `low` → 约 3 Mbps
- `medium` → 约 8 Mbps
- `high` → 约 20 Mbps

音频：AAC，采样率跟随源（44100 或 48000），立体声，128 kbps。

## 7. 数据流与时间戳

```
[抓屏线程] DXGI Dup → BGRA 纹理 → Staging → crop → BGRA→NV12 ─┐
                                                              ├→ [视频FIFO] ┐
[音频线程] loopback + mic → mix → resample → S16 ────────────┘            │
                                                                          ├→ [编码/mux 线程] → MP4
                                                       [音频FIFO] ────────┘
```

时间戳统一用 `QueryPerformanceCounter`，以「录制开始」为 0 时刻。视频 PTS 按帧到达时刻换算；音频 PTS 按累计样本数换算。两路 PTS 落在同一时基（`AV_TIME_BASE`）。

## 8. 错误处理

| 场景 | 处理 |
|---|---|
| 硬编码器初始化失败 | 按 `nvenc→qsv→amf→x264` 回退，记录实际编码器 |
| DXGI `AccessLost`（分辨率切换/独占全屏） | 重建 duplication；重建期间的帧丢弃，不中断录制 |
| 磁盘写失败 / 磁盘满 | 立即停录、冲刷已写数据、`ERROR` 上报、UI 弹窗 |
| 麦克风或系统声设备丢失 | 该路置零静音，录制继续 |
| 管道断开（recorder 崩溃） | UI 停止 STATS 刷新，提示「录制中断」 |
| 区域矩形超出显示器范围 | 启动前裁剪到有效范围，UI 侧校验 |

## 9. 测试策略

- **单元（assert demo）**：
  - `sws_scale` BGRA→NV12 像素正确性
  - 混音限幅、`swr` 重采样样本数
  - 时间戳差值单调递增
  - 硬编回退链选择逻辑（mock 掉 `avcodec_find_encoder_by_name`）
- **集成**：录制固定画面 10s → 调 `ffprobe` 校验时长 ≈ 10s、分辨率、帧数、**视频与音频双流都存在**
- **手动**：真人录屏验收（按用户规则，运行验证由用户执行，开发侧只到静态检查/构建为止）

## 10. 目录结构

```
/CMakeLists.txt
/app/          ui.exe：main_window, settings_dialog, toolbar, region_selector, tray, hotkey, ipc_client
/recorder/     recorder.exe：capture, cursor, audio, encoder, muxer, ipc_server, main
/common/       ipc 协议、settings、logging、共用类型
/third_party/  ffmpeg headers/libs、nlohmann/json（单头）
/tests/        assert demo 单元测试 + ffprobe 集成脚本
/docs/
```

## 11. 后续子项目（各自独立 spec）

1. 游戏录制模式（DX/OpenGL/Vulkan 注入抓帧）
2. 设备录制模式（摄像头 / 采集卡）
3. 实时标注与鼠标效果（绘图、点击高亮、光标特效）
4. 推流（RTMP）+ 定时录制 + 水印 + 多音轨
