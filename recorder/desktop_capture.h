#pragma once
#include "common/types.h"
#include <cstdint>
#include <vector>

struct CapturedFrame {
  int width = 0, height = 0;   // 裁剪后尺寸
  std::vector<uint8_t> bgra;   // 紧密打包，width*height*4
};

class DesktopCapture {
public:
  // monitor_index: 第几个输出；region 为该显示器本地坐标，empty 表示整屏。
  bool init(int monitor_index, const Rect& region);
  // 抓到新帧返回 true；超时（无变化）返回 false。
  bool next(CapturedFrame& out, int timeout_ms);
  void close();

  Rect output_rect() const { return crop_; }   // 裁剪后区域（本地坐标）
  int monitor_left() const { return mon_left_; }
  int monitor_top() const { return mon_top_; }

private:
  void* device_ = nullptr;      // ID3D11Device*
  void* context_ = nullptr;     // ID3D11DeviceContext*
  void* duplication_ = nullptr; // IDXGIOutputDuplication*
  void* staging_ = nullptr;     // ID3D11Texture2D*
  Rect crop_;
  int mon_left_ = 0, mon_top_ = 0;
  int tex_w_ = 0, tex_h_ = 0;
};
