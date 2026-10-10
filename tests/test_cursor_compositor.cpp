#include "tests/check.h"
#include "recorder/cursor_compositor.h"
#include <vector>

int main() {
  // 8x8 目标，全 0
  const int W = 8, H = 8;
  std::vector<uint8_t> dst(W * H * 4, 0);
  // 2x2 光标：半透明红 (BGRA) alpha=128
  uint8_t cur[2 * 2 * 4] = {
      0, 0, 255, 128,  0, 0, 255, 128,
      0, 0, 255, 128,  0, 0, 255, 128};
  // 注意：cur_alpha 是「额外」整体透明度，与位图自身 alpha 相乘；
  // 此处位图 alpha=128 已是 0.5，故 cur_alpha 传 255（中性），避免二次折半。
  draw_cursor_bgra(dst.data(), W, H, 1, 1, cur, 2, 2, 255);

  // (1,1) 像素：B=0，R≈128（0*0.5 + 255*0.5）
  size_t i = (1 * W + 1) * 4;
  CHECK(dst[i + 0] < 5);          // B
  CHECK(dst[i + 2] >= 125 && dst[i + 2] <= 131);  // R 钉死 0.5 混合值，排除“直接覆盖”的错误实现

  // 越界：负坐标不崩、不写
  draw_cursor_bgra(dst.data(), W, H, -1, -1, cur, 2, 2, 255);
  return 0;
}
