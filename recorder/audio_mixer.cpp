#include "recorder/audio_mixer.h"
extern "C" {
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
}
#include <algorithm>

void mix_float(float* dst, const float* a, const float* b, size_t n,
               float ga, float gb) {
  for (size_t i = 0; i < n; ++i) {
    float v = a[i] * ga + (b ? b[i] * gb : 0.0f);
    dst[i] = std::max(-1.0f, std::min(1.0f, v));
  }
}

Resampler::Resampler(int in_rate, int out_rate, int channels)
    : channels_(channels) {
  SwrContext* s = nullptr;
  AVChannelLayout layout;
  av_channel_layout_default(&layout, channels);
  swr_alloc_set_opts2(&s, &layout, AV_SAMPLE_FMT_FLT, out_rate,
                      &layout, AV_SAMPLE_FMT_FLT, in_rate, 0, nullptr);
  swr_init(s);
  swr_ = s;
}

Resampler::~Resampler() {
  if (swr_) swr_free(reinterpret_cast<SwrContext**>(&swr_));
}

int Resampler::convert(const float* in, int in_samples, float* out,
                       int out_capacity) {
  uint8_t* in_planes[1] = {reinterpret_cast<uint8_t*>(const_cast<float*>(in))};
  uint8_t* out_planes[1] = {reinterpret_cast<uint8_t*>(out)};
  int max_out = out_capacity / channels_;
  int got = swr_convert(reinterpret_cast<SwrContext*>(swr_), out_planes, max_out,
                        const_cast<const uint8_t**>(in_planes), in_samples);
  return got > 0 ? got : 0;
}
