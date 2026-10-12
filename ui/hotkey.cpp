#include "ui/hotkey.h"

bool register_toggle_hotkey(HWND hwnd, UINT vk, UINT mods) {
  if (vk == 0) return false;
  return RegisterHotKey(hwnd, 1, mods | MOD_NOREPEAT, vk) != 0;
}

void unregister_toggle_hotkey(HWND hwnd) {
  UnregisterHotKey(hwnd, 1);
}
