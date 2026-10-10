#include "common/settings.h"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <fstream>

using nlohmann::json;

static const char* quality_to_str(Quality q) {
  switch (q) {
    case Quality::Low: return "low";
    case Quality::High: return "high";
    default: return "medium";
  }
}
static Quality quality_from_str(const std::string& s) {
  if (s == "low") return Quality::Low;
  if (s == "high") return Quality::High;
  return Quality::Medium;
}
static const char* enc_to_str(EncoderKind k) {
  switch (k) {
    case EncoderKind::Nvenc: return "nvenc";
    case EncoderKind::Qsv: return "qsv";
    case EncoderKind::Amf: return "amf";
    case EncoderKind::X264: return "x264";
    default: return "auto";
  }
}
static EncoderKind enc_from_str(const std::string& s) {
  if (s == "nvenc") return EncoderKind::Nvenc;
  if (s == "qsv") return EncoderKind::Qsv;
  if (s == "amf") return EncoderKind::Amf;
  if (s == "x264") return EncoderKind::X264;
  return EncoderKind::Auto;
}

void save_settings(const std::string& path, const RecordConfig& c) {
  json j;
  j["output_dir"] = c.output_dir;
  j["monitor_index"] = c.monitor_index;
  j["region"] = {{"x", c.region.x}, {"y", c.region.y},
                 {"w", c.region.w}, {"h", c.region.h}};
  j["region_enabled"] = c.region_enabled;
  j["fps"] = c.fps;
  j["quality"] = quality_to_str(c.quality);
  j["video_encoder"] = enc_to_str(c.encoder);
  j["system_audio"] = {{"enabled", c.system_audio.enabled},
                       {"volume", c.system_audio.volume},
                       {"device", c.system_audio.device}};
  j["microphone"] = {{"enabled", c.microphone.enabled},
                     {"volume", c.microphone.volume},
                     {"device", c.microphone.device}};
  j["include_cursor"] = c.include_cursor;
  std::ofstream f(path);
  f << j.dump(2);
}

RecordConfig load_settings(const std::string& path) {
  RecordConfig c;
  std::ifstream f(path);
  if (!f.is_open()) return c;
  json j;
  try {
    f >> j;
  } catch (...) {
    return c;
  }
  c.output_dir = j.value("output_dir", c.output_dir);
  c.monitor_index = j.value("monitor_index", c.monitor_index);
  if (j.contains("region")) {
    c.region.x = j["region"].value("x", 0);
    c.region.y = j["region"].value("y", 0);
    c.region.w = j["region"].value("w", 0);
    c.region.h = j["region"].value("h", 0);
  }
  c.region_enabled = j.value("region_enabled", false);
  c.fps = j.value("fps", 30);
  c.quality = quality_from_str(j.value("quality", std::string("medium")));
  c.encoder = enc_from_str(j.value("video_encoder", std::string("auto")));
  if (j.contains("system_audio")) {
    c.system_audio.enabled = j["system_audio"].value("enabled", false);
    c.system_audio.volume = j["system_audio"].value("volume", 1.0f);
    c.system_audio.device = j["system_audio"].value("device", std::string("default"));
  }
  if (j.contains("microphone")) {
    c.microphone.enabled = j["microphone"].value("enabled", false);
    c.microphone.volume = j["microphone"].value("volume", 1.0f);
    c.microphone.device = j["microphone"].value("device", std::string("default"));
  }
  c.include_cursor = j.value("include_cursor", true);
  return c;
}

std::string default_settings_path() {
  const char* appdata = std::getenv("APPDATA");
  std::string dir = appdata ? appdata : ".";
  return dir + "/ScreenRecorder/settings.json";
}
