#include "ui/region_border.h"

namespace {
const int ARM = 24;  // 角臂长 px
}  // namespace

void draw_region_border(HDC dc, int L, int T, int R, int B) {
  if (R <= L || B <= T) return;

  // 边：1px 粉红实线。实线无虚线相位，拖拽移动窗口时不会随原点漂移而闪。
  HPEN pink = CreatePen(PS_SOLID, 1, RGB(255, 105, 180));
  HGDIOBJ oldPen = SelectObject(dc, pink);
  HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
  Rectangle(dc, L, T, R, B);
  SelectObject(dc, oldBrush);

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
  DeleteObject(pink);
}
