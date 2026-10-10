#include "ui/toolbar.h"
#include "common/log.h"
#include <cstdio>

// WDA_EXCLUDEFROMCAPTURE 需要较新的 SDK；旧头文件里缺失时手补。
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

namespace {
const int W = 170;
const int H = 48;
const int TOGGLE_W = 56;  // 右侧切换按钮带宽
}  // namespace

LRESULT CALLBACK Toolbar::wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  Toolbar* self = (Toolbar*)GetWindowLongPtrW(h, GWLP_USERDATA);
  if (m == WM_NCCREATE) {
    CREATESTRUCTW* cs = (CREATESTRUCTW*)l;
    self = (Toolbar*)cs->lpCreateParams;
    SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
    self->hwnd_ = h;
  }
  if (self) return self->handle(h, m, w, l);
  return DefWindowProcW(h, m, w, l);
}

LRESULT Toolbar::handle(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(h, &ps);
      RECT rc;
      GetClientRect(h, &rc);
      // 整块铺黑，圆角由窗口区域裁剪出来。
      FillRect(dc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
      if (!font_) {
        font_ = CreateFontW(20, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        if (!font_) font_ = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
      }
      HGDIOBJ oldFont = SelectObject(dc, font_);
      SetBkMode(dc, TRANSPARENT);
      if (saving_) {
        SetTextColor(dc, RGB(200, 200, 200));
        const wchar_t* s = L"保存中…";
        TextOutW(dc, 16, (H - 20) / 2, s, (int)wcslen(s));
      } else {
        SetTextColor(dc, RGB(255, 255, 255));
        int sec = (int)(elapsed_ / 1000);
        wchar_t t[16];
        swprintf_s(t, L"%02d:%02d", sec / 60, sec % 60);
        TextOutW(dc, 16, (H - 20) / 2, t, (int)wcslen(t));
      }
      // 悬停/按下：先铺按钮底色，再画图标。
      if (hover_ || pressed_) {
        RECT br = {W - TOGGLE_W + 6, 8, W - 6, H - 8};
        HBRUSH b = CreateSolidBrush(pressed_ ? RGB(95, 95, 95) : RGB(55, 55, 55));
        FillRect(dc, &br, b);
        DeleteObject(b);
      }
      // ▶ (U+25B6) / ■ (U+25A0)，右侧 ~56px 居中。
      const wchar_t* g = (recording_ || saving_) ? L"■" : L"▶";
      RECT tb = {W - TOGGLE_W, 0, W, H};
      DrawTextW(dc, g, -1, &tb, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      SelectObject(dc, oldFont);
      EndPaint(h, &ps);
      return 0;
    }
    case WM_MOUSEMOVE: {
      int x = (short)LOWORD(l);
      bool over = (x >= W - TOGGLE_W);
      if (pressed_ && !over) {
        pressed_ = false;
        InvalidateRect(h, nullptr, FALSE);
      }
      if (over != hover_) {
        hover_ = over;
        InvalidateRect(h, nullptr, FALSE);
      }
      if (over) {
        TRACKMOUSEEVENT tme = {};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = h;
        TrackMouseEvent(&tme);
      }
      return 0;
    }
    case WM_MOUSELEAVE:
      if (hover_) {
        hover_ = false;
        InvalidateRect(h, nullptr, FALSE);
      }
      return 0;
    case WM_LBUTTONDOWN: {
      int x = (short)LOWORD(l);
      if (x >= W - TOGGLE_W) {
        pressed_ = true;
        SetCapture(h);
        InvalidateRect(h, nullptr, FALSE);
      } else {
        ReleaseCapture();
        SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, 0);
      }
      return 0;
    }
    case WM_LBUTTONUP:
      if (pressed_) {
        pressed_ = false;
        ReleaseCapture();
        InvalidateRect(h, nullptr, FALSE);
        LOGI("toolbar toggle clicked, rec=%d", (int)recording_);
        if (on_toggle_) on_toggle_();
      }
      return 0;
    case WM_TIMER:
      // 定时重申置顶，避免被后出现的置顶窗口盖住。
      if (w == 1)
        SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
      return 0;
    case WM_DESTROY:
      KillTimer(h, 1);
      if (font_) { DeleteObject(font_); font_ = nullptr; }
      return 0;
    case WM_CLOSE:
      return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

HWND Toolbar::create(HINSTANCE inst, std::function<void()> on_toggle) {
  on_toggle_ = std::move(on_toggle);
  WNDCLASSW wc = {};
  wc.lpfnWndProc = &Toolbar::wnd_proc;
  wc.hInstance = inst;
  wc.lpszClassName = L"ScreenRecToolbarClass";
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
  RegisterClassW(&wc);
  // 默认位置：主屏工作区顶部居中。
  RECT wa;
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
  int x = wa.left + ((wa.right - wa.left) - W) / 2;
  int y = wa.top + 16;
  hwnd_ = CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE,
      wc.lpszClassName, L"", WS_POPUP, x, y, W, H, nullptr, nullptr, inst, this);
  if (!hwnd_) return nullptr;
  SetLayeredWindowAttributes(hwnd_, 0, 190, LWA_ALPHA);
  SetWindowRgn(hwnd_, CreateRoundRectRgn(0, 0, W + 1, H + 1, 14, 14), TRUE);
  SetTimer(hwnd_, 1, 1500, nullptr);
  SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE);
  LOGI("toolbar created hwnd=%p at (%d,%d)", (void*)hwnd_, x, y);
  return hwnd_;
}

void Toolbar::show() {
  if (!hwnd_) return;
  ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
  SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void Toolbar::set_recording(bool recording) {
  recording_ = recording;
  if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void Toolbar::set_elapsed(int64_t ms) {
  elapsed_ = ms;
  if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void Toolbar::set_saving(bool saving) {
  saving_ = saving;
  if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}
