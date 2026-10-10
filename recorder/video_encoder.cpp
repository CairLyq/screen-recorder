#include "recorder/video_encoder.h"
#include "common/log.h"
extern "C" {
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
}

static int64_t bitrate_for(Quality q, int w, int h, int fps) {
  double base;  // 1080p30 基准
  switch (q) {
    case Quality::Low: base = 3'000'000; break;
    case Quality::High: base = 20'000'000; break;
    default: base = 8'000'000; break;
  }
  double scale = (double)(w * h * fps) / (1920.0 * 1080.0 * 30.0);
  if (scale < 0.25) scale = 0.25;
  return (int64_t)(base * scale);
}

bool VideoEncoder::open(int width, int height, int fps, Quality q,
                        EncoderKind kind, std::string& chosen_name,
                        OutputFormat format) {
  if (format == OutputFormat::Webp)
    return try_open_webp(width, height, fps, q, chosen_name);
  const char* chain[4];
  switch (kind) {
    case EncoderKind::Nvenc: chain[0] = "h264_nvenc";
      break;
    case EncoderKind::Qsv: chain[0] = "h264_qsv";
      break;
    case EncoderKind::Amf: chain[0] = "h264_amf";
      break;
    case EncoderKind::X264: chain[0] = "libx264";
      break;
    default: {
      static const char* auto_chain[4] = {"h264_nvenc", "h264_qsv",
                                          "h264_amf", "libx264"};
      if (try_open(auto_chain[0], width, height, fps, q)) { chosen_name = auto_chain[0]; return true; }
      if (try_open(auto_chain[1], width, height, fps, q)) { chosen_name = auto_chain[1]; return true; }
      if (try_open(auto_chain[2], width, height, fps, q)) { chosen_name = auto_chain[2]; return true; }
      if (try_open(auto_chain[3], width, height, fps, q)) { chosen_name = auto_chain[3]; return true; }
      return false;
    }
  }
  // 指定了单一编码器：只试它，失败再回退 x264
  if (try_open(chain[0], width, height, fps, q)) { chosen_name = chain[0]; return true; }
  if (try_open("libx264", width, height, fps, q)) { chosen_name = "libx264"; return true; }
  return false;
}

bool VideoEncoder::try_open(const char* name, int w, int h, int fps, Quality q) {
  const AVCodec* codec = avcodec_find_encoder_by_name(name);
  if (!codec) return false;
  AVCodecContext* c = avcodec_alloc_context3(codec);
  if (!c) return false;
  c->width = w;
  c->height = h;
  c->time_base = {1, fps};
  c->framerate = {fps, 1};
  c->pix_fmt = AV_PIX_FMT_NV12;
  c->gop_size = fps * 2;
  c->bit_rate = bitrate_for(q, w, h, fps);
  c->max_b_frames = 0;
  if (avcodec_open2(c, codec, nullptr) < 0) {
    avcodec_free_context(&c);
    LOGI("video encoder %s open failed", name);
    return false;
  }
  ctx_ = c;
  pkt_ = av_packet_alloc();
  LOGI("video encoder %s opened", name);
  return true;
}

bool VideoEncoder::try_open_webp(int w, int h, int fps, Quality q,
                                 std::string& name) {
  const AVCodec* codec = avcodec_find_encoder_by_name("libwebp_anim");
  if (!codec) return false;
  AVCodecContext* c = avcodec_alloc_context3(codec);
  if (!c) return false;
  c->width = w;
  c->height = h;
  c->time_base = {1, fps};
  c->framerate = {fps, 1};
  c->pix_fmt = AV_PIX_FMT_YUV420P;  // libwebp_anim 要平面 YUV
  c->gop_size = fps * 2;
  int quality = q == Quality::Low ? 50 : (q == Quality::High ? 90 : 75);
  av_opt_set_int(c, "quality", quality, 0);  // libwebp 用 quality(0-100)，非码率
  if (avcodec_open2(c, codec, nullptr) < 0) {
    avcodec_free_context(&c);
    LOGI("video encoder libwebp_anim open failed");
    return false;
  }
  ctx_ = c;
  pkt_ = av_packet_alloc();
  name = "libwebp_anim";
  LOGI("video encoder libwebp_anim opened");
  return true;
}

AVFrame* VideoEncoder::alloc_frame() {
  AVFrame* f = av_frame_alloc();
  f->format = ctx_->pix_fmt;
  f->width = ctx_->width;
  f->height = ctx_->height;
  if (av_frame_get_buffer(f, 32) < 0) {
    av_frame_free(&f);
    return nullptr;
  }
  return f;
}

void VideoEncoder::encode(AVFrame* frame,
                          const std::function<void(AVPacket*)>& on_packet) {
  if (avcodec_send_frame(ctx_, frame) < 0) return;
  while (avcodec_receive_packet(ctx_, pkt_) == 0) {
    on_packet(pkt_);
    av_packet_unref(pkt_);
  }
}

void VideoEncoder::flush(const std::function<void(AVPacket*)>& on_packet) {
  avcodec_send_frame(ctx_, nullptr);
  while (avcodec_receive_packet(ctx_, pkt_) == 0) {
    on_packet(pkt_);
    av_packet_unref(pkt_);
  }
}

void VideoEncoder::close() {
  if (pkt_) av_packet_free(&pkt_);
  if (ctx_) avcodec_free_context(&ctx_);
}
