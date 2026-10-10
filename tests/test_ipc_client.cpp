#include "tests/check.h"
#include "recorder/ipc_server.h"
#include "ui/ipc_client.h"
#include <thread>
#include <windows.h>

int main() {
  const char* name = "\\\\.\\pipe\\screenrec-uitest-1";
  IpcServer server;
  CHECK(server.create(name));

  std::thread srv([&] {
    CHECK(server.wait_client());
    server.send_message({MsgType::Ready, R"({"version":1})"});
    Message m;
    CHECK(server.read_message(m));
    CHECK(m.type == MsgType::Start);
    CHECK_STREQ(m.payload.c_str(), R"({"fps":30})");
  });

  IpcClient client;
  CHECK(client.connect(name, 30, 50));
  Message ready;
  CHECK(client.read_message(ready));
  CHECK(ready.type == MsgType::Ready);
  CHECK(client.send_message({MsgType::Start, R"({"fps":30})"}));

  srv.join();
  client.close();
  server.close();
  return 0;
}
