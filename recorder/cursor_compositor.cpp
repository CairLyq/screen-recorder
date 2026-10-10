#include "recorder/cursor_compositor.h"
#include <windows.h>

void draw_cursor_bgra(uint8_t* dst, int dst_w, int dst_h, int px, int py,
                      const uint8_t* cur, int cw, int ch, int cur_alpha) {
  for (int y = 0; y < ch; ++y) {
    int dy = py + y;
    if (dy < 0 || dy >= dst_h) continue;
    for (int x = 0; x < cw; ++x) {
      int dx = px + x;
      if (dx < 0 || dx >= dst_w) continue;
      const uint8_t* s = cur + (size_t)(y * cw + x) * 4;
      uint8_t a = (uint8_t)((s[3] * cur_alpha) / 255);
      if (a == 0) continue;
      uint8_t* d = dst + (size_t)(dy * dst_w + dx) * 4;
      for (int c = 0; c < 3; ++c)
        d[c] = (uint8_t)((s[c] * a + d[c] * (255 - a)) / 255);
    }
  }
}

bool fetch_system_cursor(std::vector<uint8_t>& out_bgra, int& cw, int& ch,
                         int& hot_x, int& hot_y) {
  CURSORINFO ci = {sizeof(CURSORINFO)};
  if (!GetCursorInfo(&ci) || ci.flags != CURSOR_SHOWING) return false;
  ICONINFO ii;
  if (!GetIconInfo(ci.hCursor, &ii)) return false;

  BITMAP bm;
  if (!GetObject(ii.hbmColor, sizeof(bm), &bm)) {
    DeleteObject(ii.hbmColor);
    DeleteObject(ii.hbmMask);
    return false;
  }
  cw = bm.bmWidth;
  ch = bm.bmHeight;
  hot_x = (int)ii.xHotspot;
  hot_y = (int)ii.yHotspot;

  BITMAPINFO bi = {};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = cw;
  bi.bmiHeader.biHeight = -ch;  // top-down
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;

  out_bgra.assign((size_t)cw * ch * 4, 0);
  HDC hdc = GetDC(nullptr);
  int got = GetDIBits(hdc, ii.hbmColor, 0, ch, out_bgra.data(), &bi, DIB_RGB_COLORS);
  ReleaseDC(nullptr, hdc);
  DeleteObject(ii.hbmColor);
  DeleteObject(ii.hbmMask);

  if (got == 0) return false;
  // 若 alpha 全 0（非 32bpp 光标），用 mask 生成 alpha
  bool any_alpha = false;
  for (size_t i = 3; i < out_bgra.size(); i += 4)
    if (out_bgra[i]) { any_alpha = true; break; }
  if (!any_alpha) {
    for (size_t i = 3; i < out_bgra.size(); i += 4) out_bgra[i] = 255;
  }
  return true;
}
