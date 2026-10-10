#include "ui/region_frame.h"
#include "ui/region_border.h"

namespace {
const COLORREF kKey = RGB(1, 2, 3);  // 色键：该颜色处透明
const int BORDER = 4;                // px，窗口在区域外扩的边距
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
      // 整块铺色键色 → 区域内部透明，只留下画上去的线。
      HBRUSH key = CreateSolidBrush(kKey);
      FillRect(dc, &full, key);
      DeleteObject(key);
      // 区域轮廓（窗口坐标）：窗口已在区域外扩 BORDER；再外移 2px 画线，
      // 使 3px 线宽也完全落在裁剪区之外，绝不进视频。
      RECT r = {BORDER - 2, BORDER - 2, full.right - BORDER + 2,
                full.bottom - BORDER + 2};
      if (r.right > r.left && r.bottom > r.top)
        draw_region_border(dc, r.left, r.top, r.right, r.bottom);
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
  SetWindowPos(hwnd_, HWND_TOPMOST, x - BORDER, y - BORDER,
               w + 2 * BORDER, h + 2 * BORDER, SWP_NOACTIVATE);
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

void RegionFrame::destroy() {
  if (hwnd_) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
}
