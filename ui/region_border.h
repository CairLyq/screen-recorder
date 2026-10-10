#pragma once
#include <windows.h>

// 在 dc 上沿矩形 [L,T,R,B] 画「2px 白色虚线边 + 3px 白色实心角」，供拖拽框与录制框共用。
void draw_region_border(HDC dc, int L, int T, int R, int B);
