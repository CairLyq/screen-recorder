#include "recorder/audio_encoder.h"
#include "common/log.h"
extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

bool AudioEncoder::open(int sample_rate, int channels, std::string& chosen_name) {
  const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
  if (!codec) return false;
  ctx_ = avcodec_alloc_context3(codec);
  ctx_->sample_rate = sample_rate;
  ctx_->sample_fmt = AV_SAMPLE_FMT_FLTP;
  av_channel_layout_default(&ctx_->ch_layout, channels);
  ctx_->bit_rate = 128000;
  ctx_->time_base = {1, sample_rate};
  if (avcodec_open2(ctx_, codec, nullptr) < 0) {
    avcodec_free_context(&ctx_);
    return false;
  }
  channels_ = channels;
  frame_ = av_frame_alloc();
  frame_->format = ctx_->sample_fmt;
  frame_->sample_rate = sample_rate;
  av_channel_layout_default(&frame_->ch_layout, channels);
  frame_->nb_samples = ctx_->frame_size;
  av_frame_get_buffer(frame_, 0);
  pkt_ = av_packet_alloc();
  chosen_name = codec->name;
  return true;
}

void AudioEncoder::drain(const std::function<void(AVPacket*)>& on_packet) {
  while (avcodec_receive_packet(ctx_, pkt_) == 0) {
    on_packet(pkt_);
    av_packet_unref(pkt_);
  }
}

void AudioEncoder::encode(const float* interleaved, int samples,
                          const std::function<void(AVPacket*)>& on_packet) {
  pending_.insert(pending_.end(), interleaved, interleaved + samples * channels_);
  int fs = ctx_->frame_size;
  while ((int)(pending_.size() / channels_) >= fs) {
    av_frame_make_writable(frame_);
    // 交错 → 平面 FLTP
    for (int ch = 0; ch < channels_; ++ch) {
      float* dst = reinterpret_cast<float*>(frame_->data[ch]);
      for (int i = 0; i < fs; ++i)
        dst[i] = pending_[i * channels_ + ch];
    }
    frame_->pts = pts_;
    pts_ += fs;
    if (avcodec_send_frame(ctx_, frame_) >= 0) drain(on_packet);
    pending_.erase(pending_.begin(), pending_.begin() + fs * channels_);
  }
}

void AudioEncoder::flush(const std::function<void(AVPacket*)>& on_packet) {
  // 残余样本补零凑满一帧
  int fs = ctx_->frame_size;
  if (!pending_.empty()) {
    pending_.resize(fs * channels_, 0.0f);
    av_frame_make_writable(frame_);
    for (int ch = 0; ch < channels_; ++ch) {
      float* dst = reinterpret_cast<float*>(frame_->data[ch]);
      for (int i = 0; i < fs; ++i) dst[i] = pending_[i * channels_ + ch];
    }
    frame_->pts = pts_;
    pts_ += fs;
    if (avcodec_send_frame(ctx_, frame_) >= 0) drain(on_packet);
    pending_.clear();
  }
  avcodec_send_frame(ctx_, nullptr);
  drain(on_packet);
}

void AudioEncoder::close() {
  if (pkt_) av_packet_free(&pkt_);
  if (frame_) av_frame_free(&frame_);
  if (ctx_) avcodec_free_context(&ctx_);
}
