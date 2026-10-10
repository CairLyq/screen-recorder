#include "ui/region_selector.h"
#include <windows.h>
#include <windowsx.h>

namespace {
RECT g_start = {};
RECT g_cur = {};
bool g_dragging = false;
bool g_ok = false;

LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_LBUTTONDOWN:
      g_start.left = g_cur.left = GET_X_LPARAM(lp);
      g_start.top = g_cur.top = GET_Y_LPARAM(lp);
      g_dragging = true;
      return 0;
    case WM_MOUSEMOVE:
      if (g_dragging) {
        g_cur.left = GET_X_LPARAM(lp);
        g_cur.top = GET_Y_LPARAM(lp);
        InvalidateRect(h, nullptr, TRUE);
      }
      return 0;
    case WM_LBUTTONUP:
      if (g_dragging) {
        g_cur.left = GET_X_LPARAM(lp);
        g_cur.top = GET_Y_LPARAM(lp);
        g_dragging = false;
        g_ok = true;
        DestroyWindow(h);
      }
      return 0;
    case WM_RBUTTONUP:
    case WM_KEYDOWN:
      if (msg == WM_KEYDOWN && wp != VK_ESCAPE) return 0;
      DestroyWindow(h);
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(h, &ps);
      HBRUSH b = CreateSolidBrush(RGB(0, 0, 0));
      RECT full = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
      FillRect(dc, &full, b);
      DeleteObject(b);
      HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 40, 40));
      HGDIOBJ oldp = SelectObject(dc, pen);
      HGDIOBJ oldb = SelectObject(dc, GetStockObject(NULL_BRUSH));
      Rectangle(dc, g_start.left, g_start.top, g_cur.left, g_cur.top);
      SelectObject(dc, oldb);
      SelectObject(dc, oldp);
      DeleteObject(pen);
      EndPaint(h, &ps);
      return 0;
    }
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProc(h, msg, wp, lp);
}
}  // namespace

bool pick_region(HWND owner, RECT& out) {
  (void)owner;
  HINSTANCE inst = GetModuleHandleA(nullptr);
  WNDCLASSA wc = {};
  wc.lpfnWndProc = proc;
  wc.hInstance = inst;
  wc.lpszClassName = "ScreenRecRegionClass";
  wc.hCursor = LoadCursor(nullptr, IDC_CROSS);
  RegisterClassA(&wc);

  int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
  int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
  int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
  // WS_EX_LAYERED must be set at creation, otherwise the later
  // SetLayeredWindowAttributes call fails and the overlay stays opaque.
  HWND hwnd = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
                              wc.lpszClassName, "", WS_POPUP, x, y, w, h, nullptr,
                              nullptr, inst, nullptr);
  if (!hwnd) return false;
  SetLayeredWindowAttributes(hwnd, 0, 90, LWA_ALPHA);
  SetWindowLongPtrA(hwnd, GWL_EXSTYLE, GetWindowLongPtrA(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
  ShowWindow(hwnd, SW_SHOW);
  SetForegroundWindow(hwnd);

  g_ok = false;
  MSG msg;
  while (GetMessage(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
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
