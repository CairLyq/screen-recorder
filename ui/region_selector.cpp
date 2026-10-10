#include "ui/region_selector.h"
#include "ui/region_frame.h"
#include <windows.h>
#include <windowsx.h>

namespace {
RECT g_start = {};
RECT g_cur = {};
bool g_dragging = false;
bool g_ok = false;

// 遮罩窗原点（虚拟屏幕左上角）与上层不透明边框窗，供 WndProc 访问。
int g_ox = 0;
int g_oy = 0;
RegionFrame* p_screen_frame = nullptr;

// 用当前拖拽矩形（客户区坐标）更新上层边框窗（屏幕坐标）。
void update_frame_region() {
  if (!p_screen_frame) return;
  int L = g_start.left, T = g_start.top, R = g_cur.left, B = g_cur.top;
  if (R < L) { int t = L; L = R; R = t; }
  if (B < T) { int t = T; T = B; B = t; }
  p_screen_frame->set_region(g_ox + L, g_oy + T, R - L, B - T);
}

LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_LBUTTONDOWN:
      g_start.left = g_cur.left = GET_X_LPARAM(lp);
      g_start.top = g_cur.top = GET_Y_LPARAM(lp);
      g_dragging = true;
      update_frame_region();
      return 0;
    case WM_MOUSEMOVE:
      if (g_dragging) {
        g_cur.left = GET_X_LPARAM(lp);
        g_cur.top = GET_Y_LPARAM(lp);
        update_frame_region();
        InvalidateRect(h, nullptr, TRUE);
      }
      return 0;
    case WM_LBUTTONUP:
      if (g_dragging) {
        g_cur.left = GET_X_LPARAM(lp);
        g_cur.top = GET_Y_LPARAM(lp);
        g_dragging = false;
        g_ok = true;
        if (p_screen_frame) p_screen_frame->hide();
        DestroyWindow(h);
      }
      return 0;
    case WM_RBUTTONUP:
    case WM_KEYDOWN:
      if (msg == WM_KEYDOWN && wp != VK_ESCAPE) return 0;
      if (p_screen_frame) p_screen_frame->hide();
      DestroyWindow(h);
      return 0;
    case WM_ERASEBKGND: {
      HDC dc = (HDC)wp;
      RECT full;
      GetClientRect(h, &full);
      HBRUSH b = CreateSolidBrush(RGB(0, 0, 0));
      FillRect(dc, &full, b);
      DeleteObject(b);
      return 1;
    }
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(h, &ps);
      // 每次重绘整个客户区 → 不留拖影。边框由上层不透明边框窗绘制。
      RECT full;
      GetClientRect(h, &full);
      HBRUSH b = CreateSolidBrush(RGB(0, 0, 0));
      FillRect(dc, &full, b);
      DeleteObject(b);
      EndPaint(h, &ps);
      return 0;
    }
    case WM_DESTROY:
      if (p_screen_frame) p_screen_frame->hide();
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProc(h, msg, wp, lp);
}
}  // namespace

bool pick_region(HWND owner, RECT& out) {
  (void)owner;
  HINSTANCE inst = GetModuleHandleA(nullptr);
  WNDCLASSW wc = {};
  wc.lpfnWndProc = proc;
  wc.hInstance = inst;
  wc.lpszClassName = L"ScreenRecRegionClass";
  wc.hCursor = LoadCursor(nullptr, IDC_CROSS);
  RegisterClassW(&wc);

  int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
  int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
  int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
  g_ox = x;
  g_oy = y;
  // WS_EX_LAYERED must be set at creation, otherwise the later
  // SetLayeredWindowAttributes call fails and the overlay stays opaque.
  HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
                              wc.lpszClassName, L"", WS_POPUP, x, y, w, h, nullptr,
                              nullptr, inst, nullptr);
  if (!hwnd) return false;
  SetLayeredWindowAttributes(hwnd, 0, 90, LWA_ALPHA);
  SetWindowLongPtrA(hwnd, GWL_EXSTYLE, GetWindowLongPtrA(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
  ShowWindow(hwnd, SW_SHOW);
  SetForegroundWindow(hwnd);

  // 在遮罩窗之后创建不透明边框窗 → 叠在其上（同 TOPMOST，后者在上）。
  RegionFrame frame;
  frame.create(inst);
  p_screen_frame = &frame;

  g_ok = false;
  MSG msg;
  while (GetMessage(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  p_screen_frame = nullptr;
  frame.destroy();
  // 覆盖层销毁后桌面可能残留旧像素，强制重绘整个桌面。
  RedrawWindow(GetDesktopWindow(), nullptr, nullptr,
               RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
  if (!g_ok) return false;
  RECT r;
  r.left = g_start.left + x; r.top = g_start.top + y;
  r.right = g_cur.left + x;  r.bottom = g_cur.top + y;
  if (r.right < r.left) { LONG t = r.left; r.left = r.right; r.right = t; }
  if (r.bottom < r.top) { LONG t = r.top; r.top = r.bottom; r.bottom = t; }
  if (r.right - r.left < 8 || r.bottom - r.top < 8) return false;
  out = r;
  return true;
}
