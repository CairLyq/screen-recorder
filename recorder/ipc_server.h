#pragma once
#include "common/ipc_message.h"
#include <cstdint>
#include <string>
#include <vector>

class IpcServer {
public:
  bool create(const std::string& pipe_name);
  bool wait_client();
  bool read_message(Message& out);
  bool send_message(const Message& m);
  void close();

private:
  void* handle_ = nullptr;  // HANDLE
  std::vector<uint8_t> buf_;
};
