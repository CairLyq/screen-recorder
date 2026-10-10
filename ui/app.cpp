#include "ui/app.h"
#include "common/settings.h"
#include "common/log.h"
#include "ui/recorder_process.h"
#include "ui/settings_dialog.h"
#include "ui/hotkey.h"
#include <shlobj.h>
#include <nlohmann/json.hpp>

using nlohmann::json;

static std::string exe_dir() {
  char buf[MAX_PATH];
  GetModuleFileNameA(nullptr, buf, MAX_PATH);
  std::string s = buf;
  size_t p = s.find_last_of("\\/");
  return p == std::string::npos ? "." : s.substr(0, p);
}

bool App::init(HINSTANCE inst) {
  inst_ = inst;
  cfg_ = load_settings(default_settings_path());
  if (cfg_.output_dir.empty()) {
    char buf[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_MYVIDEO, nullptr, 0, buf)))
      cfg_.output_dir = buf;
    else
      cfg_.output_dir = ".";
  }

  WNDCLASSW wc = {};
  wc.lpfnWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
    App* self = (App*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (m == WM_NCCREATE) {
      self = (App*)((CREATESTRUCTW*)l)->lpCreateParams;
      SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
      return TRUE;  // WM_NCCREATE 返回 FALSE 会让 CreateWindow 中止创建
    }
    if (self) return self->on_message(h, m, w, l);
    return DefWindowProcW(h, m, w, l);
  };
  wc.hInstance = inst;
  wc.lpszClassName = L"ScreenRecUiClass";
  if (!RegisterClassW(&wc)) LOGE("RegisterClassW failed: %lu", GetLastError());
  hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"ScreenRecorder", 0, 0, 0, 0, 0,
                          nullptr, nullptr, inst, this);
  if (hwnd_ == nullptr) { LOGE("CreateWindowExW failed: %lu", GetLastError()); return false; }

  toolbar_.create(inst, [this] { stop_recording(); });
  tray_.add(hwnd_, WM_TRAY);
  tray_.set_callbacks([this] { toggle(); }, [this] { open_settings(); },
                      [this] { quit(); });
  register_toggle_hotkey(hwnd_, VK_F9);
  LOGI("ui ready, output -> %s", cfg_.output_dir.c_str());
  return true;
}

bool App::start_recording() {
  if (recording_) return false;
  if (reader_t_.joinable()) reader_t_.join();
  pipe_name_ = "\\\\.\\pipe\\screenrec-default";
  proc_ = spawn_recorder(exe_dir() + "\\recorder.exe", pipe_name_);
  if (!proc_) { MessageBoxW(hwnd_, L"启动录制进程失败", L"错误", MB_OK | MB_ICONERROR); return false; }
  if (!client_.connect(pipe_name_, 60, 50)) {
    MessageBoxW(hwnd_, L"连接录制进程失败", L"错误", MB_OK | MB_ICONERROR);
    kill_recorder(proc_); proc_ = nullptr;
    return false;
  }
  Message ready;
  if (!client_.read_message(ready)) { MessageBoxW(hwnd_, L"recorder 未响应", L"错误", MB_OK | MB_ICONERROR); kill_recorder(proc_); proc_ = nullptr; client_.close(); return false; }
  client_.send_message({MsgType::Start, config_to_json(cfg_)});

  recording_ = true;
  reader_run_ = true;
  reader_t_ = std::thread([this] { reader_loop(); });
  toolbar_.show();
  return true;
}

void App::stop_recording() {
  if (!recording_) return;
  client_.send_message({MsgType::Stop, ""});
  // reader 线程收到 Done 后收尾
}

void App::reader_loop() {
  while (reader_run_) {
    Message m;
    if (!client_.read_message(m)) break;
    if (m.type == MsgType::State) {
      LOGI("state: %s", m.payload.c_str());
    } else if (m.type == MsgType::Stats) {
      try {
        json j = json::parse(m.payload);
        {
          std::lock_guard<std::mutex> g(stats_mtx_);
          pending_elapsed_ = j.value("elapsed_ms", (int64_t)0);
          pending_fps_ = j.value("fps", 0.0);
        }
        PostMessageA(hwnd_, WM_APP + 3, 0, 0);
      } catch (...) {}
    } else if (m.type == MsgType::Error) {
      PostMessageA(hwnd_, WM_APP + 4, 0, 0);
      break;
    } else if (m.type == MsgType::Done) {
      break;
    }
  }
  recording_ = false;
  reader_run_ = false;
  client_.close();
  PostMessageA(hwnd_, WM_APP + 2, 0, 0);  // 通知主线程收尾
}

void App::toggle() { if (recording_) stop_recording(); else start_recording(); }

void App::open_settings() {
  if (recording_) { MessageBoxW(hwnd_, L"录制中不能改设置", L"提示", MB_OK); return; }
  if (show_settings_dialog(hwnd_, cfg_)) {
    save_settings(default_settings_path(), cfg_);
  }
}

void App::quit() {
  if (recording_) stop_recording();
  kill_recorder(proc_); proc_ = nullptr;
  if (reader_t_.joinable()) reader_t_.join();
  client_.close();
  tray_.remove();
  unregister_toggle_hotkey(hwnd_);
  PostQuitMessage(0);
}

LRESULT App::on_message(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_TRAY:
      if (lp == WM_RBUTTONUP) tray_.show_menu(h);
      else if (lp == WM_LBUTTONDBLCLK) toggle();
      return 0;
    case WM_COMMAND:
      tray_.on_command(h, LOWORD(wp));
      return 0;
    case WM_HOTKEY:
      if (wp == 1) toggle();
      return 0;
    case WM_APP + 2:  // reader 结束 → 收尾
      if (reader_t_.joinable()) reader_t_.join();
      kill_recorder(proc_); proc_ = nullptr;
      toolbar_.hide();
      return 0;
    case WM_APP + 3: {  // reader 上报 Stats → 主线程更新悬浮条
      std::lock_guard<std::mutex> g(stats_mtx_);
      toolbar_.set_status(pending_elapsed_, pending_fps_);
      return 0;
    }
    case WM_APP + 4:  // reader 上报 Error
      MessageBoxW(hwnd_, L"录制出错", L"错误", MB_OK | MB_ICONERROR);
      return 0;
  }
  return DefWindowProcW(h, msg, wp, lp);
}
