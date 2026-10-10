#pragma once
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <string>
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

  // 同时追加到 %TEMP%\screenrec.log（首次调用懒惰打开，失败则静默）
  static FILE* f = [] {
    const char* t = std::getenv("TEMP");
    std::string p = std::string(t ? t : ".") + "\\screenrec.log";
    return std::fopen(p.c_str(), "a");
  }();
  if (f) {
    std::fprintf(f, "[%s] ", level);
    va_start(ap, fmt);
    std::vfprintf(f, fmt, ap);
    va_end(ap);
    std::fputc('\n', f);
    std::fflush(f);
  }
}

#define LOGI(...) log_line("INFO", __VA_ARGS__)
#define LOGE(...) log_line("ERROR", __VA_ARGS__)
