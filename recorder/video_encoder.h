#pragma once
#include "common/types.h"
#include <functional>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
}

class VideoEncoder {
public:
  // 按 kind 选定编码器（Auto 走 NVENC→QSV→AMF→x264 回退），成功则写 chosen_name。
  bool open(int width, int height, int fps, Quality q, EncoderKind kind,
            std::string& chosen_name);
  AVFrame* alloc_frame();
  void encode(AVFrame* frame, const std::function<void(AVPacket*)>& on_packet);
  void flush(const std::function<void(AVPacket*)>& on_packet);
  void close();

  AVCodecContext* ctx() const { return ctx_; }
  bool is_open() const { return ctx_ != nullptr; }

private:
  bool try_open(const char* name, int w, int h, int fps, Quality q);
  AVCodecContext* ctx_ = nullptr;
  AVPacket* pkt_ = nullptr;
};
