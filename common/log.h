#pragma once
#include <cstdio>
#include <cstdarg>
#include <mutex>

inline void log_line(const char* level, const char* fmt, ...) {
  static std::mutex m;
  std::lock_guard<std::mutex> g(m);
  std::fprintf(stderr, "[%s] ", level);
  va_list ap;
  va_start(ap, fmt);
  std::vfprintf(stderr, fmt, ap);
  va_end(ap);
  std::fputc('\n', stderr);
}

#define LOGI(...) log_line("INFO", __VA_ARGS__)
#define LOGE(...) log_line("ERROR", __VA_ARGS__)
