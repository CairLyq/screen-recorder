#pragma once
#include <windows.h>

// 常驻区域边框：置顶、点击穿透、色键透明。边框画在区域之外，
// 因此录制裁剪区域时不会把边框录进视频。
class RegionFrame {
public:
  bool create(HINSTANCE inst);
  // 屏幕坐标；w<=0 或 h<=0 表示清除/隐藏
  void set_region(int x, int y, int w, int h);
  void show();
  void hide();

private:
  HWND hwnd_ = nullptr;
  RECT region_ = {};   // 区域（屏幕坐标，不含边框）
  bool has_ = false;
  static LRESULT CALLBACK wnd_proc(HWND, UINT, WPARAM, LPARAM);
  LRESULT handle(HWND, UINT, WPARAM, LPARAM);
};
