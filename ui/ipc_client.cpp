#include "ui/ipc_client.h"
#include <windows.h>

bool IpcClient::connect(const std::string& pipe_name, int retries, int delay_ms) {
  for (int i = 0; i < retries; ++i) {
    HANDLE h = CreateFileA(pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
      DWORD mode = PIPE_READMODE_MESSAGE;
      SetNamedPipeHandleState(h, &mode, nullptr, nullptr);
      handle_ = h;
      return true;
    }
    Sleep(delay_ms);
  }
  return false;
}

bool IpcClient::send_message(const Message& m) {
  if (!handle_) return false;
  HANDLE h = static_cast<HANDLE>(handle_);
  auto buf = encode_message(m);
  DWORD wrote = 0;
  return WriteFile(h, buf.data(), (DWORD)buf.size(), &wrote, nullptr) != 0;
}

bool IpcClient::read_message(Message& out) {
  if (!handle_) return false;
  HANDLE h = static_cast<HANDLE>(handle_);
  for (;;) {
    size_t consumed = 0;
    if (decode_message(buf_.data(), buf_.size(), out, consumed)) {
      buf_.erase(buf_.begin(), buf_.begin() + consumed);
      return true;
    }
    uint8_t tmp[4096];
    DWORD got = 0;
    BOOL ok = ReadFile(h, tmp, sizeof(tmp), &got, nullptr);
    if (got > 0) buf_.insert(buf_.end(), tmp, tmp + got);
    if (ok) {
      if (got == 0) return false;
      continue;
    }
    if (GetLastError() == ERROR_MORE_DATA) continue;
    return false;
  }
}

int IpcClient::poll_message(Message& out) {
  if (!handle_) return -1;
  HANDLE h = static_cast<HANDLE>(handle_);
  size_t consumed = 0;
  if (decode_message(buf_.data(), buf_.size(), out, consumed)) {
    buf_.erase(buf_.begin(), buf_.begin() + consumed);
    return 1;
  }
  DWORD avail = 0;
  if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) return -1;
  if (avail == 0) return 0;
  uint8_t tmp[4096];
  DWORD got = 0;
  BOOL ok = ReadFile(h, tmp, sizeof(tmp), &got, nullptr);
  if (got > 0) buf_.insert(buf_.end(), tmp, tmp + got);
  if (!ok && GetLastError() != ERROR_MORE_DATA) return -1;
  if (decode_message(buf_.data(), buf_.size(), out, consumed)) {
    buf_.erase(buf_.begin(), buf_.begin() + consumed);
    return 1;
  }
  return 0;
}

void IpcClient::close() {
  if (handle_) {
    CloseHandle(static_cast<HANDLE>(handle_));
    handle_ = nullptr;
  }
}
