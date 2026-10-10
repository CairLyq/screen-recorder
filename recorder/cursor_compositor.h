#pragma once
#include <cstdint>
#include <vector>

// 把 BGRA 光标图（含 alpha）以 alpha/255 混合到 dst 的 (px,py)。
// 处理负坐标与越界裁剪。cur_alpha 为额外整体透明度（0-255）。
void draw_cursor_bgra(uint8_t* dst, int dst_w, int dst_h, int px, int py,
                      const uint8_t* cur_bgra, int cw, int ch, int cur_alpha);

// 从 Win32 取当前光标（含热点）。成功返回 true，把光标位图写入 out_bgra/cw/ch。
bool fetch_system_cursor(std::vector<uint8_t>& out_bgra, int& cw, int& ch,
                         int& hot_x, int& hot_y);
