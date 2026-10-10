#pragma once
#include <cstddef>
#include <vector>
#include <cstdint>

// dst = clamp(a*ga + b*gb, -1, 1)。b 可为 nullptr（视为全 0）。
void mix_float(float* dst, const float* a, const float* b, size_t n,
               float ga, float gb);

// 用 swr 把 in_rate 的 float32 交错样本重采样到 out_rate，返回输出样本数。
class Resampler {
public:
  Resampler(int in_rate, int out_rate, int channels);
  ~Resampler();
  int convert(const float* in, int in_samples, float* out, int out_capacity);

private:
  void* swr_ = nullptr;  // SwrContext*
  int channels_;
};
