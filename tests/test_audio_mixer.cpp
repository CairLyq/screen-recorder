#include "tests/check.h"
#include "recorder/audio_mixer.h"

int main() {
  // 单路：增益 + 限幅
  float a[4] = {0.5f, -0.5f, 2.0f, -2.0f};
  float dst[4] = {0, 0, 0, 0};
  mix_float(dst, a, nullptr, 4, 1.0f, 0.0f);
  CHECK(dst[0] > 0.49f && dst[0] < 0.51f);
  CHECK(dst[1] > -0.51f && dst[1] < -0.49f);
  CHECK(dst[2] == 1.0f);    // 2.0 被限幅到 1.0
  CHECK(dst[3] == -1.0f);

  // 双路相加 + 各自增益
  float b[2] = {0.3f, 0.3f};
  float c[2] = {0.3f, -0.1f};
  float d2[2] = {0, 0};
  mix_float(d2, b, c, 2, 1.0f, 1.0f);
  CHECK(d2[0] > 0.59f && d2[0] < 0.61f);
  CHECK(d2[1] > 0.19f && d2[1] < 0.21f);

  // 双路饱和限幅
  float e[1] = {0.8f};
  float f[1] = {0.8f};
  float g[1] = {0};
  mix_float(g, e, f, 1, 1.0f, 1.0f);
  CHECK(g[0] == 1.0f);
  return 0;
}
