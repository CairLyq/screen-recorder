#pragma once
#include <windows.h>

// 注册/注销全局热键（窗体收到 WM_HOTKEY，id=1）。
bool register_toggle_hotkey(HWND hwnd, UINT vk);
void unregister_toggle_hotkey(HWND hwnd);
UINT vk_from_name(const char* name);  // "F9".."F12" → VK_*
