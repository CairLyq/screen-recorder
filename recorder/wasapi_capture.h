#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// 采集浮点交错样本，回调给出 (samples_interleaved_float, frame_count)。
class WasapiCapture {
public:
  // loopback=true 采默认渲染端点的系统声；false 采默认采集端点（麦克风）。
  bool init(bool loopback, int channels_hint);
  void start(const std::function<void(const float*, int)>& on_data);
  void stop();
  void close();

  int sample_rate() const { return sample_rate_; }
  int channels() const { return channels_; }

private:
  void* client_ = nullptr;      // IAudioClient*
  void* capture_ = nullptr;     // IAudioCaptureClient*
  int sample_rate_ = 0;
  int channels_ = 0;
  std::atomic<bool> running_{false};
};
