#include "ui/region_frame.h"
#include <cstdio>

namespace {
const COLORREF kKey = RGB(1, 2, 3);      // 色键：该颜色处透明
const COLORREF kBorder = RGB(230, 40, 40);
const int BORDER = 3;    // px
const int LABEL_H = 18;  // px
}  // namespace

LRESULT CALLBACK RegionFrame::wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  RegionFrame* self = (RegionFrame*)GetWindowLongPtrW(h, GWLP_USERDATA);
  if (m == WM_NCCREATE) {
    self = (RegionFrame*)((CREATESTRUCTW*)l)->lpCreateParams;
    SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
  }
  if (self) return self->handle(h, m, w, l);
  return DefWindowProcW(h, m, w, l);
}

LRESULT RegionFrame::handle(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(h, &ps);
      RECT full;
      GetClientRect(h, &full);
      // 整块客户区先铺边框色，再把区域内部填成色键 → 内部透明。
      HBRUSH border = CreateSolidBrush(kBorder);
      FillRect(dc, &full, border);
      DeleteObject(border);
      if (has_) {
        int w = region_.right - region_.left;
        int h = region_.bottom - region_.top;
        RECT inner = {BORDER, BORDER + LABEL_H, BORDER + w, BORDER + LABEL_H + h};
        HBRUSH key = CreateSolidBrush(kKey);
        FillRect(dc, &inner, key);
        DeleteObject(key);
        wchar_t t[64];
        swprintf_s(t, L"%dx%d", w, h);
        SetTextColor(dc, RGB(255, 255, 255));
        SetBkMode(dc, TRANSPARENT);
        TextOutW(dc, 6, 2, t, (int)wcslen(t));
      }
      EndPaint(h, &ps);
      return 0;
    }
  }
  return DefWindowProcW(h, msg, wp, lp);
}

bool RegionFrame::create(HINSTANCE inst) {
  WNDCLASSW wc = {};
  wc.lpfnWndProc = &RegionFrame::wnd_proc;
  wc.hInstance = inst;
  wc.lpszClassName = L"ScreenRecRegionFrameClass";
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  RegisterClassW(&wc);  // 已注册则失败，忽略
  hwnd_ = CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT |
          WS_EX_NOACTIVATE,
      wc.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst, this);
  if (!hwnd_) return false;
  SetLayeredWindowAttributes(hwnd_, kKey, 0, LWA_COLORKEY);
  ShowWindow(hwnd_, SW_HIDE);  // 初始隐藏
  return true;
}

void RegionFrame::set_region(int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) {
    has_ = false;
    hide();
    return;
  }
  region_.left = x; region_.top = y;
  region_.right = x + w; region_.bottom = y + h;
  has_ = true;
  SetWindowPos(hwnd_, HWND_TOPMOST, x - BORDER, y - BORDER - LABEL_H,
               w + 2 * BORDER, h + 2 * BORDER + LABEL_H, SWP_NOACTIVATE);
  InvalidateRect(hwnd_, nullptr, TRUE);
  ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
}

void RegionFrame::show() {
  if (!hwnd_) return;
  SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
}

void RegionFrame::hide() {
  if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
}
