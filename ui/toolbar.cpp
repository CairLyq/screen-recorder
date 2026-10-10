#include "ui/toolbar.h"
#include <cstdio>
#include <string>

#define IDC_STOP 2001
#define IDC_LABEL 2002

LRESULT CALLBACK Toolbar::wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  Toolbar* self = (Toolbar*)GetWindowLongPtrA(h, GWLP_USERDATA);
  if (m == WM_NCCREATE) {
    CREATESTRUCTA* cs = (CREATESTRUCTA*)l;
    self = (Toolbar*)cs->lpCreateParams;
    SetWindowLongPtrA(h, GWLP_USERDATA, (LONG_PTR)self);
    self->hwnd_ = h;
  }
  if (self) return self->handle(h, m, w, l);
  return DefWindowProc(h, m, w, l);
}

LRESULT Toolbar::handle(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_CREATE: {
      CreateWindowExA(0, "STATIC", "REC", WS_CHILD | WS_VISIBLE | SS_CENTER,
                      6, 12, 34, 16, h, nullptr, nullptr, nullptr);
      CreateWindowExA(0, "STATIC", "00:00  30 fps", WS_CHILD | WS_VISIBLE,
                      44, 12, 150, 16, h, (HMENU)IDC_LABEL, nullptr, nullptr);
      CreateWindowExA(0, "BUTTON", "停止", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                      198, 8, 56, 24, h, (HMENU)IDC_STOP, nullptr, nullptr);
      return 0;
    }
    case WM_COMMAND:
      if (LOWORD(w) == IDC_STOP && on_stop_) on_stop_();
      return 0;
    case WM_LBUTTONDOWN:
      ReleaseCapture();
      SendMessage(h, WM_NCLBUTTONDOWN, HTCAPTION, 0);
      return 0;
    case WM_NCHITTEST: {
      LRESULT r = DefWindowProc(h, m, w, l);
      if (r == HTCLIENT) return HTCLIENT;  // 让子控件收到点击
      return r;
    }
    case WM_CLOSE:
      return 0;  // 由 stop 流程关闭
  }
  return DefWindowProc(h, m, w, l);
}

HWND Toolbar::create(HINSTANCE inst, std::function<void()> on_stop) {
  on_stop_ = std::move(on_stop);
  WNDCLASSA wc = {};
  wc.lpfnWndProc = &Toolbar::wnd_proc;
  wc.hInstance = inst;
  wc.lpszClassName = "ScreenRecToolbarClass";
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
  RegisterClassA(&wc);
  int sw = GetSystemMetrics(SM_CXSCREEN);
  int sh = GetSystemMetrics(SM_CYSCREEN);
  hwnd_ = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, wc.lpszClassName, "",
                          WS_POPUP, sw - 280, sh - 60, 260, 40, nullptr, nullptr,
                          inst, this);
  return hwnd_;
}

void Toolbar::show() {
  if (hwnd_) { ShowWindow(hwnd_, SW_SHOW); SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); }
}
void Toolbar::hide() { if (hwnd_) ShowWindow(hwnd_, SW_HIDE); }

void Toolbar::set_status(int64_t elapsed_ms, double fps) {
  if (!hwnd_) return;
  int sec = (int)(elapsed_ms / 1000);
  char buf[64];
  snprintf(buf, sizeof(buf), "%02d:%02d  %.0f fps", sec / 60, sec % 60, fps);
  SetWindowTextA(GetDlgItem(hwnd_, IDC_LABEL), buf);
}
