#include "common/ipc_message.h"
#include <cstring>

std::vector<uint8_t> encode_message(const Message& m) {
  uint32_t body = 1 + static_cast<uint32_t>(m.payload.size());
  std::vector<uint8_t> out(4 + body);
  out[0] = static_cast<uint8_t>(body & 0xFF);
  out[1] = static_cast<uint8_t>((body >> 8) & 0xFF);
  out[2] = static_cast<uint8_t>((body >> 16) & 0xFF);
  out[3] = static_cast<uint8_t>((body >> 24) & 0xFF);
  out[4] = static_cast<uint8_t>(m.type);
  std::memcpy(out.data() + 5, m.payload.data(), m.payload.size());
  return out;
}

bool decode_message(const uint8_t* data, size_t len, Message& out, size_t& consumed) {
  if (len < 4) return false;
  uint32_t body = static_cast<uint32_t>(data[0]) |
                  (static_cast<uint32_t>(data[1]) << 8) |
                  (static_cast<uint32_t>(data[2]) << 16) |
                  (static_cast<uint32_t>(data[3]) << 24);
  if (body < 1) return false;
  if (len < 4u + body) return false;
  out.type = static_cast<MsgType>(data[4]);
  out.payload.assign(reinterpret_cast<const char*>(data + 5), body - 1);
  consumed = 4u + body;
  return true;
}
