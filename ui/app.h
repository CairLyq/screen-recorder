#pragma once
#include "common/types.h"
#include "ui/ipc_client.h"
#include "ui/toolbar.h"
#include "ui/tray.h"
#include <windows.h>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

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
  bool start_recording();
  void stop_recording();
  void reader_loop();     // 后台线程：收 State/Stats/Done/Error

  HINSTANCE inst_ = nullptr;
  HWND hwnd_ = nullptr;
  Toolbar toolbar_;
  Tray tray_;
  IpcClient client_;
  void* proc_ = nullptr;      // recorder 进程 HANDLE
  RecordConfig cfg_;
  std::atomic<bool> recording_{false};
  std::atomic<bool> reader_run_{false};
  std::thread reader_t_;
  std::mutex stats_mtx_;
  int64_t pending_elapsed_ = 0;
  double pending_fps_ = 0.0;
  std::string pipe_name_;
  static constexpr UINT WM_TRAY = WM_APP + 1;
};
