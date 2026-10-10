#pragma once
#include <windows.h>
#include <functional>

// 悬浮控制条：半透明黑色 Aero 圆角置顶条。左侧 MM:SS 计时，右侧 ▶/■ 切换按钮，
// 可拖动，排除出屏幕录制（不进视频）。
class Toolbar {
public:
  // on_toggle: 点 ▶/■ 时回调。返回创建出的窗口（失败 nullptr）。
  HWND create(HINSTANCE inst, std::function<void()> on_toggle);
  void show();
  void set_recording(bool recording);  // 切换按钮图标：false=▶(开始) true=■(停止)
  void set_elapsed(int64_t ms);        // 更新计时（MM:SS）
  void set_saving(bool saving);        // 停止后保存中：左上角显示"保存中…"

private:
  HWND hwnd_ = nullptr;
  std::function<void()> on_toggle_;
  bool recording_ = false;
  bool saving_ = false;
  bool hover_ = false;
  bool pressed_ = false;
  int64_t elapsed_ = 0;
  HFONT font_ = nullptr;  // 缓存，WM_DESTROY 释放
  static LRESULT CALLBACK wnd_proc(HWND, UINT, WPARAM, LPARAM);
  LRESULT handle(HWND, UINT, WPARAM, LPARAM);
};
