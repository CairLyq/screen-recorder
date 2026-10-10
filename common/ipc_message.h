#pragma once
#include <cstdint>
#include <string>
#include <vector>

enum class MsgType : uint8_t {
  Start = 1, Stop, Ping, Ready, State, Stats, Error, Done
};

struct Message {
  MsgType type = MsgType::Ping;
  std::string payload;  // JSON text, may be empty
};

std::vector<uint8_t> encode_message(const Message& m);

// If a complete frame is at the front of data, decode into out and set consumed; return true. Otherwise return false.
bool decode_message(const uint8_t* data, size_t len, Message& out, size_t& consumed);
