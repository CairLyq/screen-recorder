#include "recorder/ipc_server.h"
#include <windows.h>

bool IpcServer::create(const std::string& pipe_name) {
  HANDLE h = CreateNamedPipeA(
      pipe_name.c_str(), PIPE_ACCESS_DUPLEX,
      PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
      1, 64 * 1024, 64 * 1024, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  handle_ = h;
  return true;
}

bool IpcServer::wait_client() {
  HANDLE h = static_cast<HANDLE>(handle_);
  BOOL ok = ConnectNamedPipe(h, nullptr);
  return ok || GetLastError() == ERROR_PIPE_CONNECTED;
}

bool IpcServer::read_message(Message& out) {
  HANDLE h = static_cast<HANDLE>(handle_);
  for (;;) {
    // 已缓冲的完整帧？
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
      if (got == 0) return false;  // 零长读 → 对端关闭
      continue;                    // 有字节，回到顶部再试解码
    }
    if (GetLastError() == ERROR_MORE_DATA) continue;  // 本条消息还有后续分片
    return false;  // 真正的错误 / 断连
  }
}

bool IpcServer::send_message(const Message& m) {
  HANDLE h = static_cast<HANDLE>(handle_);
  auto buf = encode_message(m);
  DWORD wrote = 0;
  return WriteFile(h, buf.data(), (DWORD)buf.size(), &wrote, nullptr) != 0;
}

void IpcServer::close() {
  if (handle_) {
    FlushFileBuffers(static_cast<HANDLE>(handle_));
    DisconnectNamedPipe(static_cast<HANDLE>(handle_));
    CloseHandle(static_cast<HANDLE>(handle_));
    handle_ = nullptr;
  }
}
