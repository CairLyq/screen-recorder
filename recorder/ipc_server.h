#pragma once
#include "common/ipc_message.h"
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

class IpcServer {
public:
  bool create(const std::string& pipe_name);
  bool wait_client();
  bool read_message(Message& out);
  int poll_message(Message& out);  // 1=有消息, 0=暂无, -1=断开/错误
  bool send_message(const Message& m);
  void close();

private:
  void* handle_ = nullptr;  // HANDLE
  std::vector<uint8_t> buf_;
  std::mutex send_mtx_;
};
