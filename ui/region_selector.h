#pragma once
#include <windows.h>

// 全屏覆盖层让用户拖拽框选，返回屏幕坐标矩形。取消则返回 false。
bool pick_region(HWND owner, RECT& out);
