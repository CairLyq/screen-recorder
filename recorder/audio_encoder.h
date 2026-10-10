#pragma once
#include <functional>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
}

class AudioEncoder {
public:
  // 输入为 float32 交错样本。内部凑满 frame_size 再编码。
  bool open(int sample_rate, int channels, std::string& chosen_name);
  void encode(const float* interleaved, int samples,
              const std::function<void(AVPacket*)>& on_packet);
  void flush(const std::function<void(AVPacket*)>& on_packet);
  void close();

  AVCodecContext* ctx() const { return ctx_; }

private:
  void drain(const std::function<void(AVPacket*)>& on_packet);
  AVCodecContext* ctx_ = nullptr;
  AVFrame* frame_ = nullptr;
  AVPacket* pkt_ = nullptr;
  std::vector<float> pending_;  // 交错样本缓冲
  int channels_ = 2;
  int64_t pts_ = 0;
};
