#pragma once
#include "common/ipc_message.h"
#include <cstdint>
#include <string>
#include <vector>

// UI 侧命名管道客户端（recorder.exe 是服务端）。
class IpcClient {
public:
  // 反复尝试连接（recorder 可能尚未建好管道）。成功 true。
  bool connect(const std::string& pipe_name, int retries, int delay_ms);
  bool send_message(const Message& m);
  bool read_message(Message& out);
  int poll_message(Message& out);  // 1=有消息, 0=暂无, -1=断开/错误
  void close();

private:
  void* handle_ = nullptr;  // HANDLE
  std::vector<uint8_t> buf_;
};
