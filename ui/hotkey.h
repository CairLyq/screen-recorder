#pragma once
#include <windows.h>

// 注册/注销全局切换热键（窗体收到 WM_HOTKEY，id=1）。mods 为 MOD_* 组合。
bool register_toggle_hotkey(HWND hwnd, UINT vk, UINT mods);
void unregister_toggle_hotkey(HWND hwnd);
