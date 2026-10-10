#include "recorder/pipeline.h"
#include "recorder/desktop_capture.h"
#include "recorder/cursor_compositor.h"
#include "recorder/video_encoder.h"
#include "recorder/audio_encoder.h"
#include "recorder/audio_mixer.h"
#include "recorder/muxer.h"
#include "recorder/wasapi_capture.h"
#include "common/log.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
extern "C" {
#include <libswscale/swscale.h>
}

static int64_t now_qpc() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

Stats Pipeline::stats() const {
  Stats s;
  s.fps = measured_fps_.load();
  s.dropped_frames = dropped_.load();
  if (start_qpc_ && running_) {
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    s.elapsed_ms = (t.QuadPart - start_qpc_) * 1000 / qpc_freq_;
  }
  return s;
}

bool Pipeline::start(const RecordConfig& cfg, std::string& out_path) {
  cfg_ = cfg;
  LARGE_INTEGER f; QueryPerformanceFrequency(&f); qpc_freq_ = f.QuadPart;

  // 起抓屏
  DesktopCapture* cap = new DesktopCapture();
  if (!cap->init(cfg_.monitor_index, cfg_.region_enabled ? cfg_.region : Rect{})) {
    LOGE("capture init failed");
    delete cap;
    return false;
  }

  // 文件路径
  SYSTEMTIME st; GetLocalTime(&st);
  char name[64];
  snprintf(name, sizeof(name), "ScreenRec_%04d%02d%02d_%02d%02d%02d.mp4",
           st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
  out_path_ = cfg_.output_dir + "/" + name;
  out_path = out_path_;

  // 编码器
  VideoEncoder* v = new VideoEncoder();
  std::string vn;
  if (!v->open(cap->output_rect().w, cap->output_rect().h, cfg_.fps,
               cfg_.quality, cfg_.encoder, vn)) {
    LOGE("video encoder open failed");
    delete v; delete cap; return false;
  }
  venc_ = v;
  LOGI("chosen video encoder: %s", vn.c_str());

  AudioEncoder* a = new AudioEncoder();
  std::string an;
  if (!a->open(48000, 2, an)) { delete a; delete v; delete cap; return false; }
  aenc_ = a;

  Muxer* mux = new Muxer();
  if (!mux->open(out_path_, v->ctx(), cfg_.system_audio.enabled || cfg_.microphone.enabled ? a->ctx() : nullptr)) {
    LOGE("mux open failed");
    delete mux; delete a; delete v; delete cap; return false;
  }

  start_qpc_ = now_qpc();
  running_ = true;
  vt_ = std::thread([this, cap] {
    VideoEncoder* ve = (VideoEncoder*)venc_;
    AVFrame* f = ve->alloc_frame();
    int w = f->width, h = f->height;
    SwsContext* sws = sws_getContext(w, h, AV_PIX_FMT_BGRA, w, h, AV_PIX_FMT_NV12,
                                     SWS_BILINEAR, nullptr, nullptr, nullptr);
    CapturedFrame cf;
    int frames = 0;
    int64_t t0 = now_qpc();
    // 上一帧缓存，用于无新帧时补帧
    std::vector<uint8_t> last_bgra;
    int64_t next_pts = 0;
    int64_t frame_dur = qpc_freq_ / cfg_.fps;
    while (running_) {
      bool got = cap->next(cf, 500);
      if (got) {
        if (cfg_.include_cursor) {
          std::vector<uint8_t> cb;
          int cw, chh, hx, hy;
          if (fetch_system_cursor(cb, cw, chh, hx, hy)) {
            // 全局鼠标坐标 → 抓屏本地坐标
            POINT p; GetCursorPos(&p);
            int lx = p.x - cap->monitor_left() - cap->output_rect().x - hx;
            int ly = p.y - cap->monitor_top() - cap->output_rect().y - hy;
            draw_cursor_bgra(cf.bgra.data(), cf.width, cf.height, lx, ly,
                             cb.data(), cw, chh, 255);
          }
        }
        last_bgra = cf.bgra;
      } else if (!last_bgra.empty()) {
        cf.bgra = last_bgra;  // 补帧
        cf.width = cap->output_rect().w;
        cf.height = cap->output_rect().h;
      } else {
        continue;
      }

      const uint8_t* src[1] = {cf.bgra.data()};
      int srcstride[1] = {cf.width * 4};
      if (av_frame_make_writable(f) < 0) continue;
      sws_scale(sws, src, srcstride, 0, cf.height, f->data, f->linesize);
      f->pts = next_pts++;
      {
        std::lock_guard<std::mutex> g(vmtx_);
        VItem it;
        size_t sz = (size_t)f->linesize[0] * h + (size_t)f->linesize[1] * (h / 2);
        it.nv12.resize(sz);
        size_t off = 0;
        for (int r = 0; r < h; ++r) { std::memcpy(it.nv12.data() + off, f->data[0] + r * f->linesize[0], f->linesize[0]); off += f->linesize[0]; }
        for (int r = 0; r < h / 2; ++r) { std::memcpy(it.nv12.data() + off, f->data[1] + r * f->linesize[1], f->linesize[1]); off += f->linesize[1]; }
        it.pts = (int)f->pts;
        if (vq_.size() < (size_t)cfg_.fps * 2) vq_.push_back(std::move(it));
        else dropped_++;
      }
      frames++;
      int64_t el = now_qpc() - t0;
      if (el > qpc_freq_) { measured_fps_ = frames * (double)qpc_freq_ / el; frames = 0; t0 = now_qpc(); }
      // 节流到目标帧率
      int64_t target = start_qpc_ + (int64_t)(next_pts) * frame_dur;
      int64_t delay = (target - now_qpc()) * 1000 / qpc_freq_;
      if (delay > 0 && delay < 1000) Sleep((DWORD)delay);
    }
    sws_freeContext(sws);
    av_frame_free(&f);
  });

  et_ = std::thread([this, mux, cap] {
    VideoEncoder* ve = (VideoEncoder*)venc_;
    AudioEncoder* ae = (AudioEncoder*)aenc_;
    AVFrame* f = ve->alloc_frame();
    int h = f->height;
    while (true) {
      bool did = false;
      {
        std::lock_guard<std::mutex> g(vmtx_);
        if (!vq_.empty()) {
          VItem it = std::move(vq_.front());
          vq_.erase(vq_.begin());
          av_frame_make_writable(f);
          size_t off = 0;
          for (int r = 0; r < h; ++r) { std::memcpy(f->data[0] + r * f->linesize[0], it.nv12.data() + off, f->linesize[0]); off += f->linesize[0]; }
          for (int r = 0; r < h / 2; ++r) { std::memcpy(f->data[1] + r * f->linesize[1], it.nv12.data() + off, f->linesize[1]); off += f->linesize[1]; }
          f->pts = it.pts;
          ve->encode(f, [&](AVPacket* p) { mux->write(p, ve->ctx()); });
          did = true;
        }
      }
      {
        std::lock_guard<std::mutex> g(amtx_);
        if (!aq_.empty()) {
          ae->encode(aq_.data(), (int)(aq_.size() / a_channels_),
                     [&](AVPacket* p) { mux->write(p, ae->ctx()); });
          aq_.clear();
          did = true;
        }
      }
      if (!did) {
        if (!running_) {
          std::lock_guard<std::mutex> g(vmtx_);
          if (vq_.empty()) break;
        }
        Sleep(3);
      }
    }
    ve->flush([&](AVPacket* p) { mux->write(p, ve->ctx()); });
    ae->flush([&](AVPacket* p) { mux->write(p, ae->ctx()); });
    mux->close();
    av_frame_free(&f);
    delete mux; delete ae; delete ve; delete cap;
  });

  // 音频线程：两路采集，混音后入队
  at_ = std::thread([this, cap] {
    (void)cap;
    WasapiCapture sys, mic;
    Resampler* rsys = nullptr;
    Resampler* rmic = nullptr;
    if (cfg_.system_audio.enabled) {
      if (sys.init(true, 2)) rsys = new Resampler(sys.sample_rate(), a_rate_, 2);
    }
    if (cfg_.microphone.enabled) {
      if (mic.init(false, 2)) rmic = new Resampler(mic.sample_rate(), a_rate_, 2);
    }
    std::vector<float> sysbuf, micbuf;
    std::thread sys_t, mic_t;
    if (cfg_.system_audio.enabled && rsys) {
      sys_t = std::thread([&] {
        sys.start([&](const float* d, int n) {
          std::vector<float> out(n * 4 + 64);
          int got = rsys->convert(d, n, out.data(), (int)out.size());
          std::lock_guard<std::mutex> g(amtx_);
          sysbuf.assign(out.begin(), out.begin() + got * 2);
        });
      });
    }
    if (cfg_.microphone.enabled && rmic) {
      mic_t = std::thread([&] {
        mic.start([&](const float* d, int n) {
          std::vector<float> out(n * 4 + 64);
          int got = rmic->convert(d, n, out.data(), (int)out.size());
          std::lock_guard<std::mutex> g(amtx_);
          micbuf.assign(out.begin(), out.begin() + got * 2);
        });
      });
    }
    // 主循环：定期把两路混音进 aq_
    while (running_) {
      Sleep(10);
      std::lock_guard<std::mutex> g(amtx_);
      if ((!sysbuf.empty() || !micbuf.empty()) && aq_.empty()) {
        size_t n = sysbuf.size() > micbuf.size() ? sysbuf.size() : micbuf.size();
        std::vector<float> mixed(n, 0.0f);
        const float* a = sysbuf.empty() ? nullptr : sysbuf.data();
        const float* b = micbuf.empty() ? nullptr : micbuf.data();
        // 补齐短的一路
        std::vector<float> a2, b2;
        if (a && sysbuf.size() < n) { a2 = sysbuf; a2.resize(n, 0.0f); a = a2.data(); }
        if (b && micbuf.size() < n) { b2 = micbuf; b2.resize(n, 0.0f); b = b2.data(); }
        mix_float(mixed.data(), a ? a : mixed.data(), b, n,
                  a ? cfg_.system_audio.volume : 0.0f,
                  b ? cfg_.microphone.volume : 0.0f);
        aq_.assign(mixed.begin(), mixed.end());
        sysbuf.clear();
        micbuf.clear();
      }
    }
    sys.stop(); mic.stop();
    if (sys_t.joinable()) sys_t.join();
    if (mic_t.joinable()) mic_t.join();
    sys.close(); mic.close();
    delete rsys; delete rmic;
  });

  LOGI("recording -> %s", out_path_.c_str());
  return true;
}

void Pipeline::stop() {
  running_ = false;
  if (vt_.joinable()) vt_.join();
  if (at_.joinable()) at_.join();
  if (et_.joinable()) et_.join();
}
