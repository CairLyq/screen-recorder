#pragma once
#include <windows.h>
#include <functional>

// 托盘图标 + 右键菜单。菜单项回调由 owner 窗口的 WM_COMMAND 处理。
class Tray {
public:
  bool add(HWND owner, UINT callback_msg);
  void remove();
  // 在 owner 窗口处理 WM_COMMAND 时调用；命中右键菜单项则执行对应回调并返回 true。
  bool on_command(HWND owner, int cmd);
  void set_callbacks(std::function<void()> on_toggle, std::function<void()> on_settings,
                     std::function<void()> on_quit);
  void show_menu(HWND owner);

private:
  HWND owner_ = nullptr;
  UINT msg_ = 0;
  std::function<void()> on_toggle_, on_settings_, on_quit_;
};
