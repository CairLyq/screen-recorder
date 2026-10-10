#pragma once
#include "common/types.h"
#include "ui/ipc_client.h"
#include "ui/region_frame.h"
#include "ui/toolbar.h"
#include "ui/tray.h"
#include <windows.h>
#include <atomic>
#include <string>

// 应用状态与控制流。单实例。
class App {
public:
  bool init(HINSTANCE inst);
  void toggle();          // 热键/托盘：开始或停止
  void open_settings();
  void quit();
  LRESULT on_message(HWND h, UINT msg, WPARAM wp, LPARAM lp);
  HWND hwnd() const { return hwnd_; }

private:
  bool select_region();
  bool start_recording();
  void stop_recording();
  void poll_ipc();        // WM_TIMER：非阻塞收 State/Stats/Done/Error
  void end_session();     // 收尾：停 timer、杀进程、关管道
  void open_output_folder();  // 保存完打开输出目录并选中文件

  HINSTANCE inst_ = nullptr;
  HWND hwnd_ = nullptr;
  Toolbar toolbar_;
  Tray tray_;
  RegionFrame frame_;
  IpcClient client_;
  void* proc_ = nullptr;      // recorder 进程 HANDLE
  RecordConfig cfg_;
  std::atomic<bool> recording_{false};
  int64_t rec_start_tick_ = 0;
  std::string pipe_name_;
  std::string out_path_;      // 本次录制输出文件（来自 State 消息）
  static constexpr UINT WM_TRAY = WM_APP + 1;
  static constexpr UINT_PTR TIMER_POLL = 1;
};
