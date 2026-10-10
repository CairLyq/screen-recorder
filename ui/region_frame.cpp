#include "ui/region_frame.h"

namespace {
const COLORREF kKey = RGB(1, 2, 3);  // 色键：该颜色处透明
const int BORDER = 4;                // px，窗口在区域外扩的边距
const int CORNER = 22;               // 角臂长度 px
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
      if (r.right > r.left && r.bottom > r.top) {
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        // 白色虚线边界（GDI 虚线要求 1px 宽）。
        HPEN dash = CreatePen(PS_DASH, 1, RGB(255, 255, 255));
        HGDIOBJ oldPen = SelectObject(dc, dash);
        Rectangle(dc, r.left, r.top, r.right, r.bottom);
        // 加粗实心角（3px，沿同一边界矩形，每臂 ~22px）。
        HPEN solid = CreatePen(PS_SOLID, 3, RGB(255, 255, 255));
        SelectObject(dc, solid);
        int L = r.left, T = r.top, R = r.right, B = r.bottom;
        MoveToEx(dc, L, T, nullptr); LineTo(dc, L + CORNER, T);  // 左上
        MoveToEx(dc, L, T, nullptr); LineTo(dc, L, T + CORNER);
        MoveToEx(dc, R, T, nullptr); LineTo(dc, R - CORNER, T);  // 右上
        MoveToEx(dc, R, T, nullptr); LineTo(dc, R, T + CORNER);
        MoveToEx(dc, L, B, nullptr); LineTo(dc, L + CORNER, B);  // 左下
        MoveToEx(dc, L, B, nullptr); LineTo(dc, L, B - CORNER);
        MoveToEx(dc, R, B, nullptr); LineTo(dc, R - CORNER, B);  // 右下
        MoveToEx(dc, R, B, nullptr); LineTo(dc, R, B - CORNER);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(dash);
        DeleteObject(solid);
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
