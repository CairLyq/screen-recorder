#include "tests/check.h"
#include "recorder/video_encoder.h"
#include "recorder/audio_encoder.h"
#include <vector>
#include <cmath>

int main() {
  int w = 320, h = 240, fps = 30;
  VideoEncoder enc;
  std::string chosen;
  CHECK(enc.open(w, h, fps, Quality::Medium, EncoderKind::X264, chosen));
  CHECK(!chosen.empty());
  std::printf("video encoder = %s\n", chosen.c_str());

  AVFrame* frame = enc.alloc_frame();
  CHECK(frame != nullptr);

  int packets = 0;
  for (int i = 0; i < 60; ++i) {
    // 填成纯灰 NV12，Y=128，UV=128
    for (int r = 0; r < h; ++r)
      std::memset(frame->data[0] + r * frame->linesize[0], 128, w);
    for (int r = 0; r < h / 2; ++r)
      std::memset(frame->data[1] + r * frame->linesize[1], 128, w);
    frame->pts = i;
    enc.encode(frame, [&](AVPacket* p) { packets++; });
  }
  enc.flush([&](AVPacket* p) { packets++; });

  CHECK(packets > 0);
  std::printf("packets = %d\n", packets);
  enc.close();
  av_frame_free(&frame);

  // ---- 音频编码 ----
  AudioEncoder aenc;
  std::string acodec;
  CHECK(aenc.open(48000, 2, acodec));
  std::printf("audio encoder = %s\n", acodec.c_str());

  int apackets = 0;
  // 喂 1 秒正弦，每次 1024 样本
  std::vector<float> sine(1024 * 2);
  for (int blk = 0; blk < 47; ++blk) {
    for (int i = 0; i < 1024; ++i) {
      float v = 0.2f * std::sin(6.2831853f * 440.0f * (blk * 1024 + i) / 48000.0f);
      sine[i * 2] = v;
      sine[i * 2 + 1] = v;
    }
    aenc.encode(sine.data(), 1024, [&](AVPacket* p) { apackets++; });
  }
  aenc.flush([&](AVPacket* p) { apackets++; });
  CHECK(apackets > 0);
  std::printf("audio packets = %d\n", apackets);
  aenc.close();
  return 0;
}
