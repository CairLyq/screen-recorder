#include "tests/check.h"
#include "common/ipc_message.h"

int main() {
  Message in{MsgType::Start, R"({"fps":30})"};
  std::vector<uint8_t> buf = encode_message(in);
  CHECK(buf.size() == 4 + 1 + 10);

  Message out{MsgType::Ping, ""};
  size_t consumed = 0;
  CHECK(decode_message(buf.data(), buf.size(), out, consumed));
  CHECK(out.type == MsgType::Start);
  CHECK_STREQ(out.payload.c_str(), R"({"fps":30})");
  CHECK(consumed == buf.size());

  // 半包：只给前 6 字节，应返回 false
  Message partial;
  size_t c2 = 0;
  CHECK(!decode_message(buf.data(), 6, partial, c2));
  return 0;
}
