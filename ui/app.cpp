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

  WNDCLASSA wc = {};
  wc.lpfnWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
    App* self = (App*)GetWindowLongPtrA(h, GWLP_USERDATA);
    if (m == WM_NCCREATE) {
      self = (App*)((CREATESTRUCTA*)l)->lpCreateParams;
      SetWindowLongPtrA(h, GWLP_USERDATA, (LONG_PTR)self);
    }
    if (self) { self->on_message(h, m, w, l); return 0; }
    return DefWindowProc(h, m, w, l);
  };
  wc.hInstance = inst;
  wc.lpszClassName = "ScreenRecUiClass";
  RegisterClassA(&wc);
  hwnd_ = CreateWindowExA(0, wc.lpszClassName, "ScreenRecorder", 0, 0, 0, 0, 0,
                          nullptr, nullptr, inst, this);

  toolbar_.create(inst, [this] { stop_recording(); });
  tray_.add(hwnd_, WM_TRAY);
  tray_.set_callbacks([this] { toggle(); }, [this] { open_settings(); },
                      [this] { quit(); });
  register_toggle_hotkey(hwnd_, VK_F9);
  LOGI("ui ready, output -> %s", cfg_.output_dir.c_str());
  return hwnd_ != nullptr;
}

bool App::start_recording() {
  if (recording_) return false;
  if (reader_t_.joinable()) reader_t_.join();
  pipe_name_ = "\\\\.\\pipe\\screenrec-default";
  proc_ = spawn_recorder(exe_dir() + "\\recorder.exe", pipe_name_);
  if (!proc_) { MessageBoxA(hwnd_, "启动录制进程失败", "错误", MB_OK | MB_ICONERROR); return false; }
  if (!client_.connect(pipe_name_, 60, 50)) {
    MessageBoxA(hwnd_, "连接录制进程失败", "错误", MB_OK | MB_ICONERROR);
    kill_recorder(proc_); proc_ = nullptr;
    return false;
  }
  Message ready;
  if (!client_.read_message(ready)) { MessageBoxA(hwnd_, "recorder 未响应", "错误", MB_OK | MB_ICONERROR); kill_recorder(proc_); proc_ = nullptr; client_.close(); return false; }
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
  if (recording_) { MessageBoxA(hwnd_, "录制中不能改设置", "提示", MB_OK); return; }
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

void App::on_message(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_TRAY:
      if (lp == WM_RBUTTONUP) tray_.show_menu(h);
      else if (lp == WM_LBUTTONDBLCLK) toggle();
      break;
    case WM_COMMAND:
      tray_.on_command(h, LOWORD(wp));
      break;
    case WM_HOTKEY:
      if (wp == 1) toggle();
      break;
    case WM_APP + 2:  // reader 结束 → 收尾
      if (reader_t_.joinable()) reader_t_.join();
      kill_recorder(proc_); proc_ = nullptr;
      toolbar_.hide();
      break;
    case WM_APP + 3: {  // reader 上报 Stats → 主线程更新悬浮条
      std::lock_guard<std::mutex> g(stats_mtx_);
      toolbar_.set_status(pending_elapsed_, pending_fps_);
      break;
    }
    case WM_APP + 4:  // reader 上报 Error
      MessageBoxA(hwnd_, "录制出错", "错误", MB_OK | MB_ICONERROR);
      break;
  }
}
