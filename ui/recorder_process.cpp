#include "ui/recorder_process.h"
#include "common/log.h"
#include <windows.h>
#include <vector>

void* spawn_recorder(const std::string& exe_path, const std::string& pipe_name) {
  std::string cmd = "\"" + exe_path + "\" --pipe " + pipe_name;
  std::vector<char> buf(cmd.begin(), cmd.end());
  buf.push_back('\0');
  STARTUPINFOA si = {sizeof(si)};
  PROCESS_INFORMATION pi = {};
  if (!CreateProcessA(nullptr, buf.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    LOGE("CreateProcess failed: %lu", GetLastError());
    return nullptr;
  }
  CloseHandle(pi.hThread);
  return pi.hProcess;
}

void kill_recorder(void* process) {
  HANDLE h = static_cast<HANDLE>(process);
  if (!h) return;
  if (WaitForSingleObject(h, 3000) == WAIT_TIMEOUT) {
    TerminateProcess(h, 0);
    WaitForSingleObject(h, 1000);
  }
  CloseHandle(h);
}
