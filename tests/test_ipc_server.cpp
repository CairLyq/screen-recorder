#include "tests/check.h"
#include "recorder/ipc_server.h"
#include "common/ipc_message.h"
#include <thread>
#include <windows.h>

int main() {
  const char* name = "\\\\.\\pipe\\screenrec-test-1";
  IpcServer server;
  CHECK(server.create(name));

  std::thread client([&] {
    Sleep(100);
    HANDLE h = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    Message m{MsgType::Start, R"({"fps":30})"};
    auto buf = encode_message(m);
    DWORD wrote = 0;
    WriteFile(h, buf.data(), (DWORD)buf.size(), &wrote, nullptr);
    // 读回一条 STATE
    uint8_t rb[256];
    DWORD got = 0;
    ReadFile(h, rb, sizeof(rb), &got, nullptr);
    CHECK(got > 0);
    CloseHandle(h);
  });

  CHECK(server.wait_client());
  Message recv;
  CHECK(server.read_message(recv));
  CHECK(recv.type == MsgType::Start);
  CHECK_STREQ(recv.payload.c_str(), R"({"fps":30})");
  CHECK(server.send_message(Message{MsgType::State, R"("running")"}));

  client.join();
  server.close();
  return 0;
}
