#include "recorder/muxer.h"
#include "common/log.h"

bool Muxer::open(const std::string& path, AVCodecContext* video,
                 AVCodecContext* audio) {
  if (avformat_alloc_output_context2(&fmt_, nullptr, nullptr, path.c_str()) < 0) {
    error_ = "alloc_output_context failed";
    return false;
  }
  vstream_ = avformat_new_stream(fmt_, nullptr);
  vstream_->time_base = video->time_base;
  avcodec_parameters_from_context(vstream_->codecpar, video);

  if (audio) {
    astream_ = avformat_new_stream(fmt_, nullptr);
    astream_->time_base = audio->time_base;
    avcodec_parameters_from_context(astream_->codecpar, audio);
    audio_ctx_ = audio;
  }

  if (!(fmt_->oformat->flags & AVFMT_NOFILE)) {
    if (avio_open(&fmt_->pb, path.c_str(), AVIO_FLAG_WRITE) < 0) {
      error_ = "avio_open failed";
      avformat_free_context(fmt_);
      fmt_ = nullptr;
      return false;
    }
  }
  if (avformat_write_header(fmt_, nullptr) < 0) {
    error_ = "write_header failed";
    if (!(fmt_->oformat->flags & AVFMT_NOFILE) && fmt_->pb) avio_closep(&fmt_->pb);
    avformat_free_context(fmt_);
    fmt_ = nullptr;
    return false;
  }
  return true;
}

bool Muxer::write(AVPacket* pkt, AVCodecContext* from) {
  AVStream* st = (from && from == audio_ctx_) ? astream_ : vstream_;
  if (!st) return false;
  pkt->stream_index = st->index;
  av_packet_rescale_ts(pkt, from->time_base, st->time_base);
  return av_interleaved_write_frame(fmt_, pkt) >= 0;
}

void Muxer::close() {
  if (!fmt_) return;
  av_write_trailer(fmt_);
  if (!(fmt_->oformat->flags & AVFMT_NOFILE) && fmt_->pb) avio_closep(&fmt_->pb);
  avformat_free_context(fmt_);
  fmt_ = nullptr;
}
