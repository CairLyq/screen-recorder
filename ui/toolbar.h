#pragma once
#include <windows.h>
#include <functional>

// 悬浮条：显示 REC 计时 + FPS，带停止按钮，可拖动。尺寸约 260x40。
class Toolbar {
public:
  // on_stop: 用户点停止按钮时回调。返回创建出的窗口（失败 nullptr）。
  HWND create(HINSTANCE inst, std::function<void()> on_stop);
  void show();
  void hide();
  void set_status(int64_t elapsed_ms, double fps);

private:
  HWND hwnd_ = nullptr;
  std::function<void()> on_stop_;
  static LRESULT CALLBACK wnd_proc(HWND, UINT, WPARAM, LPARAM);
  LRESULT handle(HWND, UINT, WPARAM, LPARAM);
};
