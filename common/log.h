#pragma once
#include <cstdio>
#include <cstdarg>
#include <mutex>
#if defined(_MSC_VER)
#include <sal.h>
#endif

#ifdef _MSC_VER
#define SR_PRINTF_FMT _Printf_format_string_
#else
#define SR_PRINTF_FMT
#endif

inline void log_line(const char* level, SR_PRINTF_FMT const char* fmt, ...) {
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
