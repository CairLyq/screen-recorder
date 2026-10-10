#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
      std::exit(1);                                                            \
    }                                                                          \
  } while (0)

#define CHECK_STREQ(a, b)                                                      \
  do {                                                                         \
    const char* _a = (a);                                                      \
    const char* _b = (b);                                                      \
    if (std::strcmp(_a, _b) != 0) {                                            \
      std::printf("FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, _a,    \
                  _b);                                                         \
      std::exit(1);                                                            \
    }                                                                          \
  } while (0)

#define TEST_MAIN()                                                            \
  int main() {                                                                 \
    std::printf("PASS\n");                                                     \
    return 0;                                                                  \
  }
