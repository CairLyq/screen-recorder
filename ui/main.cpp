#include "ui/app.h"
#include "common/log.h"
#include <windows.h>
#include <commctrl.h>
#include <cstdio>

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  // 单实例：同名互斥体已存在 → 已有进程在跑，提示后直接退出，避免多开。
  // 句柄随进程退出自动释放，无需 ReleaseMutex。
  HANDLE instance_mtx = CreateMutexW(nullptr, TRUE, L"ScreenRecorder_SingleInstance");
  if (instance_mtx && GetLastError() == ERROR_ALREADY_EXISTS) {
    MessageBoxW(nullptr, L"屏幕录制已在运行（见系统托盘）。", L"提示",
                MB_OK | MB_ICONINFORMATION);
    return 0;
  }
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES | ICC_HOTKEY_CLASS};
  InitCommonControlsEx(&icc);

  App app;
  if (!app.init(inst)) {
    LOGE("ui init failed");
    wchar_t msg[256];
    swprintf_s(msg, L"初始化失败，详见 %%TEMP%%\\screenrec.log");
    MessageBoxW(nullptr, msg, L"错误", MB_OK | MB_ICONERROR);
    return 1;
  }
  MSG msg;
  while (GetMessage(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  return 0;
}
