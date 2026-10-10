#pragma once
#include "common/types.h"
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

class Pipeline {
public:
  // 阻塞直到初始化完成（成功返回 true）。out_path 为实际写的文件。
  bool start(const RecordConfig& cfg, std::string& out_path);
  void stop();          // 停止并收尾
  Stats stats() const;

private:
  void video_loop();
  void audio_loop();
  void encode_loop();

  RecordConfig cfg_;
  std::atomic<bool> running_{false};
  std::atomic<int64_t> dropped_{0};
  std::atomic<double> measured_fps_{0};

  std::thread vt_, at_, et_;

  // 视频 FIFO
  std::mutex vmtx_;
  struct VItem { std::vector<uint8_t> nv12; int pts; };
  std::vector<VItem> vq_;
  size_t vcap_ = 0;

  // 音频 FIFO（已混好的 float 交错）
  std::mutex amtx_;
  std::vector<float> aq_;
  int a_channels_ = 2;
  int a_rate_ = 48000;

  std::string out_path_;
  void* venc_ = nullptr;  // VideoEncoder*
  void* aenc_ = nullptr;  // AudioEncoder*
  int64_t start_qpc_ = 0;
  int64_t qpc_freq_ = 0;
};
