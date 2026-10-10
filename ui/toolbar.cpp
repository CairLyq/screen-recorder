#include "ui/toolbar.h"
#include "common/log.h"
#include <cstdio>
#include <string>

#define IDC_STOP 2001
#define IDC_LABEL 2002

LRESULT CALLBACK Toolbar::wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  Toolbar* self = (Toolbar*)GetWindowLongPtrW(h, GWLP_USERDATA);
  if (m == WM_NCCREATE) {
    CREATESTRUCTW* cs = (CREATESTRUCTW*)l;
    self = (Toolbar*)cs->lpCreateParams;
    SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
    self->hwnd_ = h;
  }
  if (self) return self->handle(h, m, w, l);
  return DefWindowProc(h, m, w, l);
}

LRESULT Toolbar::handle(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_CREATE: {
      CreateWindowExW(0, L"STATIC", L"REC", WS_CHILD | WS_VISIBLE | SS_CENTER,
                      6, 12, 34, 16, h, nullptr, nullptr, nullptr);
      CreateWindowExW(0, L"STATIC", L"00:00  30 fps", WS_CHILD | WS_VISIBLE,
                      44, 12, 150, 16, h, (HMENU)IDC_LABEL, nullptr, nullptr);
      CreateWindowExW(0, L"BUTTON", L"停止", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                      198, 8, 56, 24, h, (HMENU)IDC_STOP, nullptr, nullptr);
      LOGI("toolbar WM_CREATE, label=%p", (void*)GetDlgItem(h, IDC_LABEL));
      return 0;
    }
    case WM_COMMAND:
      LOGI("toolbar WM_COMMAND id=%d", LOWORD(w));
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
  WNDCLASSW wc = {};
  wc.lpfnWndProc = &Toolbar::wnd_proc;
  wc.hInstance = inst;
  wc.lpszClassName = L"ScreenRecToolbarClass";
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
  RegisterClassW(&wc);
  int sw = GetSystemMetrics(SM_CXSCREEN);
  int sh = GetSystemMetrics(SM_CYSCREEN);
  hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, wc.lpszClassName, L"",
                          WS_POPUP, sw - 280, sh - 60, 260, 40, nullptr, nullptr,
                          inst, this);
  return hwnd_;
}

void Toolbar::show() {
  if (hwnd_) { ShowWindow(hwnd_, SW_SHOW); SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); }
}
void Toolbar::hide() { if (hwnd_) ShowWindow(hwnd_, SW_HIDE); }

void Toolbar::set_status(int64_t elapsed_ms, double fps) {
  LOGI("toolbar set_status elapsed=%lld fps=%.1f hwnd=%p", (long long)elapsed_ms, fps, (void*)hwnd_);
  if (!hwnd_) return;
  int sec = (int)(elapsed_ms / 1000);
  wchar_t buf[64];
  swprintf_s(buf, L"%02d:%02d  %.0f fps", sec / 60, sec % 60, fps);
  SetWindowTextW(GetDlgItem(hwnd_, IDC_LABEL), buf);
}
