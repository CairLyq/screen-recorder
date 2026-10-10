#include "ui/app.h"
#include <windows.h>
#include <commctrl.h>

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES};
  InitCommonControlsEx(&icc);

  App app;
  if (!app.init(inst)) {
    MessageBoxA(nullptr, "初始化失败", "错误", MB_OK | MB_ICONERROR);
    return 1;
  }
  MSG msg;
  while (GetMessage(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  return 0;
}
