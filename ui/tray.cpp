#include "ui/tray.h"
#include <shellapi.h>

#define WM_TRAY (WM_APP + 1)
#define IDM_TOGGLE 9001
#define IDM_SETTINGS 9002
#define IDM_QUIT 9003

bool Tray::add(HWND owner, UINT callback_msg) {
  owner_ = owner;
  msg_ = callback_msg;
  NOTIFYICONDATAA nid = {};
  nid.cbSize = sizeof(nid);
  nid.hWnd = owner;
  nid.uID = 1;
  nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  nid.uCallbackMessage = callback_msg;
  nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
  strcpy_s(nid.szTip, "屏幕录制");
  return Shell_NotifyIconA(NIM_ADD, &nid) != 0;
}

void Tray::remove() {
  NOTIFYICONDATAA nid = {};
  nid.cbSize = sizeof(nid);
  nid.hWnd = owner_;
  nid.uID = 1;
  Shell_NotifyIconA(NIM_DELETE, &nid);
}

void Tray::set_callbacks(std::function<void()> t, std::function<void()> s,
                         std::function<void()> q) {
  on_toggle_ = std::move(t);
  on_settings_ = std::move(s);
  on_quit_ = std::move(q);
}

void Tray::show_menu(HWND owner) {
  HMENU menu = CreatePopupMenu();
  AppendMenuA(menu, MF_STRING, IDM_TOGGLE, "开始/停止录制");
  AppendMenuA(menu, MF_STRING, IDM_SETTINGS, "设置...");
  AppendMenuA(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuA(menu, MF_STRING, IDM_QUIT, "退出");
  POINT p;
  GetCursorPos(&p);
  SetForegroundWindow(owner);
  TrackPopupMenu(menu, TPM_RIGHTBUTTON, p.x, p.y, 0, owner, nullptr);
  DestroyMenu(menu);
}

bool Tray::on_command(HWND owner, int cmd) {
  switch (cmd) {
    case IDM_TOGGLE: if (on_toggle_) on_toggle_(); return true;
    case IDM_SETTINGS: if (on_settings_) on_settings_(); return true;
    case IDM_QUIT: if (on_quit_) on_quit_(); return true;
  }
  return false;
}
