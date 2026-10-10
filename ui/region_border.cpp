#include "ui/region_border.h"

namespace {
const int DASH = 8;  // 每段实线长 px
const int GAP = 6;   // 段间空隙 px
const int ARM = 24;  // 角臂长 px
}  // namespace

void draw_region_border(HDC dc, int L, int T, int R, int B) {
  if (R <= L || B <= T) return;

  // 边：2px 白虚线（GDI 的 PS_DASH 只有 1px，故手工画短段）。
  HPEN dash = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
  HGDIOBJ oldPen = SelectObject(dc, dash);
  for (int x = L; x < R; x += DASH + GAP) {
    int x2 = x + DASH; if (x2 > R) x2 = R;
    MoveToEx(dc, x, T, nullptr); LineTo(dc, x2, T);  // 上
    MoveToEx(dc, x, B, nullptr); LineTo(dc, x2, B);  // 下
  }
  for (int y = T; y < B; y += DASH + GAP) {
    int y2 = y + DASH; if (y2 > B) y2 = B;
    MoveToEx(dc, L, y, nullptr); LineTo(dc, L, y2);  // 左
    MoveToEx(dc, R, y, nullptr); LineTo(dc, R, y2);  // 右
  }

  // 角：3px 白实心直角，每臂 24px。
  HPEN solid = CreatePen(PS_SOLID, 3, RGB(255, 255, 255));
  SelectObject(dc, solid);
  MoveToEx(dc, L, T, nullptr); LineTo(dc, L + ARM, T);  // 左上
  MoveToEx(dc, L, T, nullptr); LineTo(dc, L, T + ARM);
  MoveToEx(dc, R, T, nullptr); LineTo(dc, R - ARM, T);  // 右上
  MoveToEx(dc, R, T, nullptr); LineTo(dc, R, T + ARM);
  MoveToEx(dc, L, B, nullptr); LineTo(dc, L + ARM, B);  // 左下
  MoveToEx(dc, L, B, nullptr); LineTo(dc, L, B - ARM);
  MoveToEx(dc, R, B, nullptr); LineTo(dc, R - ARM, B);  // 右下
  MoveToEx(dc, R, B, nullptr); LineTo(dc, R, B - ARM);

  SelectObject(dc, oldPen);
  DeleteObject(solid);
  DeleteObject(dash);
}
