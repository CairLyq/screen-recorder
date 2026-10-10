#include "ui/hotkey.h"
#include <cstring>

UINT vk_from_name(const char* name) {
  if (!name) return VK_F9;
  if (strcmp(name, "F10") == 0) return VK_F10;
  if (strcmp(name, "F11") == 0) return VK_F11;
  if (strcmp(name, "F12") == 0) return VK_F12;
  return VK_F9;
}

bool register_toggle_hotkey(HWND hwnd, UINT vk) {
  return RegisterHotKey(hwnd, 1, MOD_NOREPEAT, vk) != 0;
}

void unregister_toggle_hotkey(HWND hwnd) {
  UnregisterHotKey(hwnd, 1);
}
