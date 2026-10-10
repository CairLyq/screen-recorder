#include "ui/app.h"
#include "common/settings.h"
#include "common/log.h"
#include "ui/recorder_process.h"
#include "ui/region_selector.h"
#include "ui/settings_dialog.h"
#include "ui/hotkey.h"
#include "ui/resource.h"
#include <shellapi.h>
#include <shlobj.h>
#include <nlohmann/json.hpp>
#include <string>

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
  wc.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                               GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
  wc.lpszClassName = L"ScreenRecUiClass";
  if (!RegisterClassW(&wc)) LOGE("RegisterClassW failed: %lu", GetLastError());
  hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"ScreenRecorder", 0, 0, 0, 0, 0,
                          nullptr, nullptr, inst, this);
  if (hwnd_ == nullptr) { LOGE("CreateWindowExW failed: %lu", GetLastError()); return false; }

  toolbar_.create(inst, [this] { toggle(); });
  toolbar_.show();      // 悬浮条常驻：顶部居中
  frame_.create(inst);  // 区域不持久化：启动时不恢复，每次录制重新框选
  tray_.add(hwnd_, WM_TRAY);
  tray_.set_callbacks([this] { toggle(); }, [this] { open_settings(); },
                      [this] { select_region(); }, [this] { quit(); });
  register_toggle_hotkey(hwnd_, VK_F9);
  LOGI("ui ready, output -> %s", cfg_.output_dir.c_str());
  return true;
}

bool App::select_region() {
  RECT r;
  if (!pick_region(hwnd_, r)) return false;
  cfg_.region.x = r.left; cfg_.region.y = r.top;
  cfg_.region.w = r.right - r.left; cfg_.region.h = r.bottom - r.top;
  cfg_.region_enabled = true;
  frame_.set_region(cfg_.region.x, cfg_.region.y, cfg_.region.w, cfg_.region.h);
  return true;
}

bool App::start_recording() {
  if (recording_) return false;
  out_path_.clear();
  // 每次录制都重新框选区域（区域不保存）
  if (!select_region()) {
    MessageBoxW(hwnd_, L"未设置录制区域", L"提示", MB_OK | MB_ICONINFORMATION);
    return false;
  }
  pipe_name_ = "\\\\.\\pipe\\screenrec-default";
  proc_ = spawn_recorder(exe_dir() + "\\recorder.exe", pipe_name_);
  if (!proc_) { MessageBoxW(hwnd_, L"启动录制进程失败", L"错误", MB_OK | MB_ICONERROR); return false; }
  if (!client_.connect(pipe_name_, 60, 50)) {
    MessageBoxW(hwnd_, L"连接录制进程失败", L"错误", MB_OK | MB_ICONERROR);
    kill_recorder(proc_); proc_ = nullptr;
    return false;
  }
  client_.send_message({MsgType::Start, config_to_json(cfg_)});
  recording_ = true;
  rec_start_tick_ = GetTickCount64();
  toolbar_.set_recording(true);
  toolbar_.set_saving(false);
  SetTimer(hwnd_, TIMER_POLL, 20, nullptr);
  return true;
}

void App::stop_recording() {
  if (!recording_) return;
  client_.send_message({MsgType::Stop, ""});
  toolbar_.set_saving(true);
  // 等 poll_ipc 收到 Done 后收尾
}

void App::poll_ipc() {
  for (;;) {
    Message m;
    int r = client_.poll_message(m);
    if (r < 0) { LOGE("ui << pipe EOF"); end_session(); return; }
    if (r == 0) break;
    if (m.type == MsgType::State) {
      LOGI("state: %s", m.payload.c_str());
      try {
        json j = json::parse(m.payload);
        if (j.is_object() && j.contains("path")) out_path_ = j.value("path", std::string());
      } catch (...) {}
    } else if (m.type == MsgType::Stats) {
      try {
        json j = json::parse(m.payload);
        toolbar_.set_elapsed(j.value("elapsed_ms", (int64_t)0));
      } catch (...) {}
    } else if (m.type == MsgType::Error) {
      MessageBoxW(hwnd_, L"录制出错", L"错误", MB_OK | MB_ICONERROR);
      end_session();
      return;
    } else if (m.type == MsgType::Done) {
      open_output_folder();  // 保存完自动打开输出目录
      end_session();
      return;
    }
  }
}

void App::end_session() {
  KillTimer(hwnd_, TIMER_POLL);
  kill_recorder(proc_); proc_ = nullptr;
  client_.close();
  recording_ = false;
  toolbar_.set_recording(false);
  toolbar_.set_elapsed(0);
  toolbar_.set_saving(false);
  frame_.hide();  // 录制结束隐藏区域框
}

void App::toggle() {
  if (recording_) stop_recording(); else start_recording();
}

void App::open_settings() {
  if (recording_) { MessageBoxW(hwnd_, L"录制中不能改设置", L"提示", MB_OK); return; }
  if (show_settings_dialog(hwnd_, cfg_)) {
    save_settings(default_settings_path(), cfg_);
    // 区域每次录制时重新框选，设置对话框不碰区域框
  }
}

void App::open_output_folder() {
  if (out_path_.empty()) return;
  int n = MultiByteToWideChar(CP_UTF8, 0, out_path_.c_str(), (int)out_path_.size(), nullptr, 0);
  if (n <= 0) return;
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, out_path_.c_str(), (int)out_path_.size(), &w[0], n);
  for (auto& c : w) if (c == L'/') c = L'\\';   // 统一分隔符，否则 explorer /select 解析失败
  std::wstring args = L"/select,\"" + w + L"\"";
  ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

void App::quit() {
  if (recording_) client_.send_message({MsgType::Stop, ""});
  end_session();
  frame_.hide();
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
    case WM_TIMER:
      if (wp == TIMER_POLL) {
        poll_ipc();
        if (recording_) toolbar_.set_elapsed(GetTickCount64() - rec_start_tick_);
      }
      return 0;
  }
  return DefWindowProcW(h, msg, wp, lp);
}
