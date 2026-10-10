#pragma once
#include <cstdint>
#include <string>

struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
  bool empty() const { return w <= 0 || h <= 0; }
};

enum class EncoderKind { Auto, Nvenc, Qsv, Amf, X264 };
enum class Quality { Low, Medium, High };
enum class OutputFormat { Mp4, Webp };

struct AudioTrack {
  bool enabled = false;
  float volume = 1.0f;
  std::string device = "default";
};

struct RecordConfig {
  std::string output_dir;
  int monitor_index = 0;
  Rect region;
  bool region_enabled = false;
  int fps = 30;
  Quality quality = Quality::Medium;
  EncoderKind encoder = EncoderKind::Auto;
  OutputFormat format = OutputFormat::Mp4;
  AudioTrack system_audio;
  AudioTrack microphone;
  bool include_cursor = true;
};

struct Stats {
  double fps = 0.0;
  int64_t elapsed_ms = 0;
  int64_t dropped_frames = 0;
  int64_t file_bytes = 0;
};
