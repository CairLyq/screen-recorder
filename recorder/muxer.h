#pragma once
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

class Muxer {
public:
  // 需在写 header 前把视频/音频编码器参数写进对应流。
  bool open(const std::string& path, AVCodecContext* video,
            AVCodecContext* audio /*可为 nullptr*/);
  // pkt 应来自对应编码器；按 DTS 交织写入。
  bool write(AVPacket* pkt, AVCodecContext* from);
  void close();

  std::string error() const { return error_; }

private:
  AVFormatContext* fmt_ = nullptr;
  AVStream* vstream_ = nullptr;
  AVStream* astream_ = nullptr;
  AVCodecContext* audio_ctx_ = nullptr;
  std::string error_;
  bool failed_ = false;
};
