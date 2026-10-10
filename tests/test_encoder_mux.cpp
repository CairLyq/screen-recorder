#include "tests/check.h"
#include "recorder/video_encoder.h"

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
  return 0;
}
